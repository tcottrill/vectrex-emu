// -----------------------------------------------------------------------------
// Motorola MC6809 / MC6809E CPU Core
//
// This file is part of the AAE (Another Arcade Emulator) project and follows the
// same Neil-Bradley-compatible memory-handler interface used by the other CPU
// cores in this folder (cpu_i8080, cpu_i8085, cpu_lr35902, cpu_z80).
//
// Design notes:
//   - MC6809 / MC6809E documented instruction set only (no HD6309 extensions,
//     no undocumented opcodes).
//   - Style modeled on cpu_i8080: a flat class with a big switch in exec().
//   - Addressing modes (direct / extended / indexed-postbyte) are factored into
//     shared helpers so the ~256 page-1 opcodes plus the 0x10 / 0x11 prefix
//     pages reuse one indexed-postbyte decoder.
//   - IRQ/FIRQ are level-sensitive; NMI latches assertion edges after S is
//     initialized. Interrupts are serviced on the next exec() iteration.
//   - Per-instruction nominal cycle counts from the MC6809 datasheet.
//   - The 6809 is BIG-ENDIAN; all 16-bit accesses go through read16()/write16().
//
// 2026-05-28  TC  Initial implementation.
// -----------------------------------------------------------------------------

#ifndef _CPU_M6809_H_
#define _CPU_M6809_H_

#pragma once

#include <cstdint>
#include "deftypes.h"

// Legacy interrupt-type bitmask, kept only for source-level compatibility with
// the AAE cpu_control layer (and the older cpu_6809 API it was written for).
// New / portable code should prefer the level inputs nmi_line()/irq_line()/
// firq_line() directly. 
enum {
    M6809_INT_NONE = 0x00,
    M6809_INT_IRQ  = 0x01,
    M6809_INT_FIRQ = 0x02,
    M6809_INT_NMI  = 0x04,
};

class cpu_m6809
{
public:

    // Condition Code register bit masks (hardware bit order).
    enum {
        CC_C = 0x01,  // carry / borrow
        CC_V = 0x02,  // two's-complement overflow
        CC_Z = 0x04,  // zero
        CC_N = 0x08,  // negative
        CC_I = 0x10,  // IRQ mask
        CC_H = 0x20,  // half carry (decimal)
        CC_F = 0x40,  // FIRQ mask
        CC_E = 0x80,  // entire flag (full register set was stacked)
    };

    // Pointer to the cpu memory map (raw 64 KiB fallback).
    uint8_t* MEM = nullptr;
    // Pointer to the handler structures.
    MemoryReadByte*  memory_read  = nullptr;
    MemoryWriteByte* memory_write = nullptr;

    // Constructor / destructor.
    cpu_m6809(uint8_t* mem, MemoryReadByte* read_mem, MemoryWriteByte* write_mem, int cpu_num);
   ~cpu_m6809() {}

    // Main entry points (mirror cpu_i8080).
    void reset();                 // load PC from the RESET vector $FFFE/$FFFF
    int  exec(int cycles);        // run ~cycles, return leftover (<= 0 on overshoot)
    int  step();                  // run exactly ONE instruction (or service one
                                  // pending interrupt); returns cycles consumed (>= 1).
                                  // Peripheral-agnostic: the caller advances timers.
    int  get_ticks(int reset);    // running, resettable cycle total

    // NMI assertion-edge input and level-sensitive IRQ/FIRQ inputs.
    void nmi_line(bool asserted)  {
        if (asserted && !m_nmi_line && m_nmi_enabled) m_nmi_pending = true;
        m_nmi_line = asserted;
    }
    void irq_line(bool asserted)  { m_irq_line  = asserted; }
    void firq_line(bool asserted) { m_firq_line = asserted; }

    // ---- Add-on / integration hooks (the core itself never depends on them) -
    // Optional callback invoked whenever the program counter changes
    // non-sequentially: jumps, calls, returns, taken branches, interrupt
    // vectors, RTI/RTS, PULS PC, TFR/EXG to PC. The argument is the new PC and
    // the return value is ignored (it mirrors the old cpu_setOPbase16 contract).
    // The Star Wars slapstic add-on registers esb_setopbase here; every other
    // target leaves it null and the core skips it entirely.
    // This replaces the previous hard dependency on the free function
    //  cpu_setOPbase16, keeping the core self-contained.
    int (*opbase_override)(int) = nullptr;

    // Optional decrypted-opcode base. When set, instruction-stream fetches
    // (fetch8/fetch16: opcode/postbyte/immediate/branch-offset) read from this
    // flat 64K buffer instead of the normal bus, while DATA reads (read8/read16)
    // still hit the original memory. This is the AAE equivalent of MAME's
    // memory_set_opcode_base, used by the konami1 opcode-scramble (Gyruss 6809).
    void set_opcode_base(uint8_t* base) { m_opcode_base = base; }

    // True while the most recent bus access was an instruction-stream fetch
    // (opcode / postbyte / immediate / branch-offset) rather than a data
    // load/store. The slapstic add-on queries this to decide whether an
    // $8000-$9FFF access should drive the bank-switch state machine. This is the
    // clean per-CPU replacement for the old global 'slapstic_en' gate.
    bool in_opcode_fetch() const { return m_in_opcode_fetch; }

    // Debug surface.
    uint16_t GetPC()         const { return m_PC; }
    uint16_t GetPPC()        const { return m_PPC; }
    uint8_t  GetLastOpcode() const { return m_last_opcode; }
    int      GetCpuNum()     const { return cpu_num; }
    void mame_memory_handling(bool s) { mmem = s; }
    void log_unhandled_rw(bool s)     { log_debug_rw = s; }

    // ---- Legacy AAE / cpu_6809-compatible API ------------------------------
    // Thin name-compatibility wrappers so the existing cpu_control.cpp,
    void     reset6809()          { reset(); }
    int      exec6809(int cycles) { return exec(cycles); }
    int      step6809()           { return step(); }
    int      get6809ticks(int r)  { return get_ticks(r); }
    uint16_t get_pc()  const      { return m_PC; }
    uint16_t get_ppc() const      { return m_PPC; }
    uint8_t  get_last_ireg()  const { return m_last_opcode; }
    uint8_t  get_last_ireg2() const { return m_last_opcode; }
    // One-shot interrupt request (edge style), matching how the AAE scheduler
    // drives every other CPU core. Latches a request separately from the pin
    // level; the core clears the request when the interrupt is taken.
    void     m6809_Cause_Interrupt(int type);
    void     m6809_Clear_Pending_Interrupts();

    // Test/debug register accessors (read-only snapshots).
    uint8_t  GetA()  const { return m_D.d8.A; }
    uint8_t  GetB()  const { return m_D.d8.B; }
    uint16_t GetD()  const { return m_D.D; }
    uint16_t GetX()  const { return m_X; }
    uint16_t GetY()  const { return m_Y; }
    uint16_t GetU()  const { return m_U; }
    uint16_t GetS()  const { return m_S; }
    uint8_t  GetDP() const { return m_DP; }
    uint8_t  GetCC() const { return m_CC; }

    // Test/debug register setters (used by the test harness to seed state).
    void SetA(uint8_t v)  { m_D.d8.A = v; }
    void SetB(uint8_t v)  { m_D.d8.B = v; }
    void SetD(uint16_t v) { m_D.D = v; }
    void SetX(uint16_t v) { m_X = v; }
    void SetY(uint16_t v) { m_Y = v; }
    void SetU(uint16_t v) { m_U = v; }
    void SetS(uint16_t v) { m_S = v; }
    void SetDP(uint8_t v) { m_DP = v; }
    void SetCC(uint8_t v) { m_CC = v; }
    void SetPC(uint16_t v) { m_PC = v; }

private:

    // ---- Register file -----------------------------------------------------
    // D = A:B with A as the high byte. Union assumes a little-endian host
    // (true for all current Windows x64 targets).
    union {
        uint16_t D;
        struct { uint8_t B; uint8_t A; } d8;
    } m_D;

    uint16_t m_X  = 0;   // index register X
    uint16_t m_Y  = 0;   // index register Y
    uint16_t m_U  = 0;   // user stack pointer
    uint16_t m_S  = 0;   // hardware stack pointer
    uint16_t m_PC = 0;   // program counter
    uint16_t m_PPC = 0;  // previous PC (debug)
    uint8_t  m_DP = 0;   // direct page
    uint8_t  m_CC = 0;   // condition codes

    uint8_t  m_last_opcode = 0;
    int      cpu_num = 0;

    // ---- Interrupt / wait state -------------------------------------------
    bool m_nmi_line    = false;
    bool m_irq_line    = false;
    bool m_firq_line   = false;
    bool m_nmi_pending = false;
    bool m_irq_pending = false;
    bool m_firq_pending = false;
    bool m_nmi_enabled = false;  // NMI masked from reset until first write to S
    bool m_sync        = false;  // SYNC: waiting for any interrupt line
    bool m_cwai        = false;  // CWAI: registers pre-stacked, waiting

    // ---- Add-on hook support state ----------------------------------------
    bool     m_in_opcode_fetch     = false; // last bus access was a fetch (see in_opcode_fetch)
    uint8_t* m_opcode_base         = nullptr; // decrypted-opcode base (see set_opcode_base)
    uint16_t m_pc_after_last_fetch = 0;     // PC right after the last fetch; used to detect non-sequential PC changes

    // ---- Debug / memory options -------------------------------------------
    bool mmem = false;            // MAME-style memory handling (block unhandled)
    bool log_debug_rw = false;    // log unhandled reads/writes
    bool debug = false;
    int  clocktickstotal = 0;

    // ---- Memory access -----------------------------------------------------
    // bus_read8 / bus_write8 perform the raw handler-table walk and never touch
    // the m_in_opcode_fetch flag. read8/write8/read16/write16 are the DATA
    // accessors (they mark m_in_opcode_fetch=false), while fetch8/fetch16 are
    // the INSTRUCTION-STREAM accessors (they mark it true). Keeping these
    // separate is what lets the slapstic add-on tell opcode fetches from data
    // reads without any global state.
    uint8_t  bus_read8 (uint16_t addr);        // raw handler walk, big-endian neutral
    void     bus_write8(uint16_t addr, uint8_t v);
    uint8_t  read8 (uint16_t addr);
    void     write8(uint16_t addr, uint8_t v);
    uint16_t read16(uint16_t addr);            // big-endian: hi @ addr, lo @ addr+1
    void     write16(uint16_t addr, uint16_t v);
    // Opcode fetch: the instruction-selecting byte(s) (opcode + 0x10/0x11
    // prefix sub-opcode). On a konami1-scrambled CPU these come from the
    // decrypted opcode base; on a normal CPU m_opcode_base is null and this is
    // identical to a raw fetch.
    uint8_t  fetch_opcode() { m_in_opcode_fetch = true; uint8_t v = (m_opcode_base ? m_opcode_base[m_PC] : bus_read8(m_PC)); ++m_PC; m_pc_after_last_fetch = m_PC; return v; }
    // Operand fetch: postbytes / immediates / offsets / addresses. konami1
    // leaves these UN-encrypted, so they are always read raw (bus_read8). For a
    // normal CPU this is the same memory the opcode came from.
    uint8_t  fetch8()  { m_in_opcode_fetch = true; uint8_t v = bus_read8(m_PC++); m_pc_after_last_fetch = m_PC; return v; }
    uint16_t fetch16() { m_in_opcode_fetch = true; uint16_t hi = bus_read8(m_PC++); uint16_t lo = bus_read8(m_PC++); m_pc_after_last_fetch = m_PC; return (uint16_t)((hi << 8) | lo); }

    // Fire the optional PC-change callback (no-op when unregistered).
    inline void notify_pc_change() { if (opbase_override) opbase_override((int)m_PC); }

    // ---- Addressing-mode helpers ------------------------------------------
    uint16_t ea_direct();                       // (DP << 8) | next byte
    uint16_t ea_extended();                     // next word
    uint16_t ea_indexed(int& extra_cycles);     // postbyte-driven

    // ---- Flag helpers ------------------------------------------------------
    inline void set_flag(uint8_t mask, bool cond) { if (cond) m_CC |= mask; else m_CC &= (uint8_t)~mask; }
    inline bool get_flag(uint8_t mask) const { return (m_CC & mask) != 0; }
    inline void set_NZ8(uint8_t v)  { set_flag(CC_N, v & 0x80);   set_flag(CC_Z, v == 0); }
    inline void set_NZ16(uint16_t v){ set_flag(CC_N, v & 0x8000); set_flag(CC_Z, v == 0); }

    // ---- 8-bit operation helpers ------------------------------------------
    void    op_ld8(uint8_t& dst, uint8_t v);
    void    op_add8(uint8_t& dst, uint8_t v);
    void    op_adc8(uint8_t& dst, uint8_t v);
    void    op_sub8(uint8_t& dst, uint8_t v);
    void    op_sbc8(uint8_t& dst, uint8_t v);
    void    op_and8(uint8_t& dst, uint8_t v);
    void    op_or8(uint8_t& dst, uint8_t v);
    void    op_eor8(uint8_t& dst, uint8_t v);
    void    op_cmp8(uint8_t a, uint8_t v);
    void    op_bit8(uint8_t a, uint8_t v);
    uint8_t op_neg8(uint8_t v);
    uint8_t op_com8(uint8_t v);
    uint8_t op_clr8();
    uint8_t op_inc8(uint8_t v);
    uint8_t op_dec8(uint8_t v);
    uint8_t op_tst8(uint8_t v);
    uint8_t op_lsr8(uint8_t v);
    uint8_t op_asr8(uint8_t v);
    uint8_t op_asl8(uint8_t v);
    uint8_t op_rol8(uint8_t v);
    uint8_t op_ror8(uint8_t v);
    void    op_daa();

    // ---- 16-bit operation helpers -----------------------------------------
    void     op_ld16(uint16_t& dst, uint16_t v);
    uint16_t op_add16(uint16_t a, uint16_t b);   // ADDD
    uint16_t op_sub16(uint16_t a, uint16_t b);   // SUBD
    void     op_cmp16(uint16_t a, uint16_t v);   // CMPD/CMPX/...

    // ---- Stack helpers -----------------------------------------------------
    void     push_s8(uint8_t v)  { write8(--m_S, v); }
    uint8_t  pull_s8()           { return read8(m_S++); }
    void     push_u8(uint8_t v)  { write8(--m_U, v); }
    uint8_t  pull_u8()           { return read8(m_U++); }
    void     push_s16(uint16_t v){ push_s8((uint8_t)v); push_s8((uint8_t)(v >> 8)); }
    uint16_t pull_s16()          { uint16_t h = pull_s8(); return (uint16_t)((h << 8) | pull_s8()); }
    int      push_post(uint8_t postbyte, bool to_u);  // PSHS/PSHU, returns bytes moved
    int      pull_post(uint8_t postbyte, bool from_u);// PULS/PULU, returns bytes moved

    // ---- Misc instruction helpers -----------------------------------------
    void     op_tfr_exg(uint8_t postbyte, bool exchange);
    uint16_t reg16_by_tfr(uint8_t code);
    void     set_reg16_by_tfr(uint8_t code, uint16_t v);

    // ---- Branch helpers ----------------------------------------------------
    bool     test_branch_cond(uint8_t code); // low-nibble branch condition (0..F)
    void     branch_short(bool taken);       // signed 8-bit relative (fetch + maybe take)
    int      branch_long(bool taken);        // signed 16-bit relative; returns 6 if taken else 5

    // ---- Interrupt handling -----------------------------------------------
    void     service_interrupt(uint16_t vector, bool set_F, bool entire);
    void     do_rti(int& cycles);

    // ---- Prefix pages ------------------------------------------------------
    int      exec_page10();   // 0x10 prefix; returns cycles consumed by sub-op
    int      exec_page11();   // 0x11 prefix; returns cycles consumed by sub-op
};

#endif // _CPU_M6809_H_
