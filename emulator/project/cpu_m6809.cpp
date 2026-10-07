// -----------------------------------------------------------------------------
// Motorola MC6809 / MC6809E CPU Core - implementation
//
// See cpu_m6809.h for design notes.
// -----------------------------------------------------------------------------

#include "cpu_m6809.h"
#include "sys_log.h"
#include <cstdlib>

#pragma warning( disable : 4244 )

// =============================================================================
// Construction / reset / ticks
// =============================================================================

cpu_m6809::cpu_m6809(uint8_t* mem, MemoryReadByte* read_mem, MemoryWriteByte* write_mem, int num)
{
    MEM = mem;
    memory_read = read_mem;
    memory_write = write_mem;
    cpu_num = num;
    m_D.D = 0;
}


// One-shot interrupt request, edge style, to match the AAE scheduler which
// pulses cpu_do_int_imm() once per interrupt period. These pending requests
// are separate from the external pin levels and are consumed when serviced.
// Masked IRQ/FIRQ requests remain pending until their masks clear.
void cpu_m6809::m6809_Cause_Interrupt(int type)
{
    if ((type & M6809_INT_NMI) && m_nmi_enabled) m_nmi_pending = true;
    if (type & M6809_INT_FIRQ) m_firq_pending = true;
    if (type & M6809_INT_IRQ)  m_irq_pending = true;
}

void cpu_m6809::m6809_Clear_Pending_Interrupts()
{
    m_nmi_pending = m_firq_pending = m_irq_pending = false;
}

int cpu_m6809::get_ticks(int reset)
{
    int tmp = clocktickstotal;
    if (reset) clocktickstotal = 0;
    return tmp;
}

void cpu_m6809::reset()
{
    m_DP = 0;
    m_CC = CC_I | CC_F;     // IRQ and FIRQ masked at reset
    m_nmi_enabled = false;  // NMI disabled until first write to S
    m_sync = false;
    m_cwai = false;
    m_nmi_line = m_irq_line = m_firq_line = false;
    m_nmi_pending = m_irq_pending = m_firq_pending = false;
    m_PC = read16(0xFFFE);  // RESET vector
    m_PPC = m_PC;
    m_pc_after_last_fetch = m_PC;
    notify_pc_change();     // let any add-on prime itself for the entry point
}

// =============================================================================
// Memory access (handler-walk pattern, mirrors cpu_i8080)
// =============================================================================

uint8_t cpu_m6809::bus_read8(uint16_t addr)
{
    uint8_t temp = 0;
    MemoryReadByte* MemRead = memory_read;

    while (MemRead && MemRead->lowAddr != 0xffffffff)
    {
        if ((addr >= MemRead->lowAddr) && (addr <= MemRead->highAddr))
        {
            if (MemRead->memoryCall)
                temp = MemRead->memoryCall(addr - MemRead->lowAddr, MemRead);
            else
                temp = *((uint8_t*)MemRead->pUserArea + (addr - MemRead->lowAddr));
            MemRead = nullptr;
            break;
        }
        ++MemRead;
    }
    if ((!memory_read || MemRead) && !mmem)
        temp = MEM[addr];
    else if (MemRead && mmem)
        if (log_debug_rw) LOG_INFO("CPU%d: Unhandled Read at %04X", cpu_num, addr);

    return temp;
}

void cpu_m6809::bus_write8(uint16_t addr, uint8_t byte)
{
    MemoryWriteByte* MemWrite = memory_write;

    while (MemWrite && MemWrite->lowAddr != 0xffffffff)
    {
        if ((addr >= MemWrite->lowAddr) && (addr <= MemWrite->highAddr))
        {
            if (MemWrite->memoryCall)
                MemWrite->memoryCall(addr - MemWrite->lowAddr, byte, MemWrite);
            else
                *((uint8_t*)MemWrite->pUserArea + (addr - MemWrite->lowAddr)) = byte;
            MemWrite = nullptr;
            break;
        }
        ++MemWrite;
    }
    if ((!memory_write || MemWrite) && !mmem)
        MEM[addr] = byte;
    else if (MemWrite && mmem)
        if (log_debug_rw) LOG_INFO("CPU%d: Unhandled Write at %04X data: %02X", cpu_num, addr, byte);
}

// Data accessors: mark the access as NOT an opcode fetch, then walk the bus.
uint8_t cpu_m6809::read8(uint16_t addr)
{
    m_in_opcode_fetch = false;
    return bus_read8(addr);
}

void cpu_m6809::write8(uint16_t addr, uint8_t byte)
{
    m_in_opcode_fetch = false;
    bus_write8(addr, byte);
}

uint16_t cpu_m6809::read16(uint16_t addr)
{
    uint16_t hi = read8(addr);
    uint16_t lo = read8((uint16_t)(addr + 1));
    return (uint16_t)((hi << 8) | lo);
}

void cpu_m6809::write16(uint16_t addr, uint16_t v)
{
    write8(addr, (uint8_t)(v >> 8));
    write8((uint16_t)(addr + 1), (uint8_t)v);
}

// =============================================================================
// Addressing-mode helpers
// =============================================================================

uint16_t cpu_m6809::ea_direct()
{
    return (uint16_t)(((uint16_t)m_DP << 8) | fetch8());
}

uint16_t cpu_m6809::ea_extended()
{
    return fetch16();
}

// Indexed addressing. Decodes the postbyte, applies auto inc/dec to the
// selected register in place, and (for the indirect forms) dereferences.
// Adds the variable cycle cost to extra_cycles.
uint16_t cpu_m6809::ea_indexed(int& extra_cycles)
{
    uint8_t pb = fetch8();

    // Select the base register pointer by bits 6..5.
    uint16_t* reg;
    switch ((pb >> 5) & 0x03) {
    case 0: reg = &m_X; break;
    case 1: reg = &m_Y; break;
    case 2: reg = &m_U; break;
    default: reg = &m_S; break;
    }

    // 5-bit signed offset: 0RRnnnnn (never indirect).
    if ((pb & 0x80) == 0) {
        // Sign-extend the low 5 bits.
        int soff = (pb & 0x10) ? (int)(pb & 0x1F) - 32 : (int)(pb & 0x1F);
        extra_cycles += 1;
        return (uint16_t)(*reg + soff);
    }

    bool indirect = (pb & 0x10) != 0;
    uint16_t ea = 0;

    switch (pb & 0x0F) {
    case 0x00: // ,R+
        ea = *reg; *reg = (uint16_t)(*reg + 1); extra_cycles += 2; break;
    case 0x01: // ,R++
        ea = *reg; *reg = (uint16_t)(*reg + 2); extra_cycles += 3; break;
    case 0x02: // ,-R
        *reg = (uint16_t)(*reg - 1); ea = *reg; extra_cycles += 2; break;
    case 0x03: // ,--R
        *reg = (uint16_t)(*reg - 2); ea = *reg; extra_cycles += 3; break;
    case 0x04: // ,R (no offset)
        ea = *reg; break;
    case 0x05: // B,R
        ea = (uint16_t)(*reg + (int8_t)m_D.d8.B); extra_cycles += 1; break;
    case 0x06: // A,R
        ea = (uint16_t)(*reg + (int8_t)m_D.d8.A); extra_cycles += 1; break;
    case 0x08: // n,R (8-bit offset)
        ea = (uint16_t)(*reg + (int8_t)fetch8()); extra_cycles += 1; break;
    case 0x09: // n,R (16-bit offset)
        ea = (uint16_t)(*reg + (int16_t)fetch16()); extra_cycles += 4; break;
    case 0x0B: // D,R
        ea = (uint16_t)(*reg + (int16_t)m_D.D); extra_cycles += 4; break;
    case 0x0C: // n,PCR (8-bit)
    {
        int8_t off = (int8_t)fetch8();
        ea = (uint16_t)(m_PC + off); extra_cycles += 1; break;
    }
    case 0x0D: // n,PCR (16-bit)
    {
        int16_t off = (int16_t)fetch16();
        ea = (uint16_t)(m_PC + off); extra_cycles += 5; break;
    }
    case 0x0F: // [n] extended indirect (only valid with indirect bit set)
        ea = fetch16(); extra_cycles += 2; break;
    default:
        // Reserved / illegal postbyte form; treat as ,R.
        ea = *reg; break;
    }

    if (indirect) {
        ea = read16(ea);
        extra_cycles += 3;
    }

    return ea;
}

// =============================================================================
// 8-bit operation helpers
// =============================================================================

void cpu_m6809::op_ld8(uint8_t& dst, uint8_t v)
{
    dst = v;
    set_NZ8(v);
    set_flag(CC_V, false);
}

void cpu_m6809::op_add8(uint8_t& dst, uint8_t v)
{
    uint16_t r = (uint16_t)dst + v;
    set_flag(CC_H, ((dst & 0x0F) + (v & 0x0F)) & 0x10);
    set_flag(CC_C, r & 0x100);
    set_flag(CC_V, (~(dst ^ v) & (dst ^ r)) & 0x80);
    dst = (uint8_t)r;
    set_NZ8(dst);
}

void cpu_m6809::op_adc8(uint8_t& dst, uint8_t v)
{
    uint8_t c = get_flag(CC_C) ? 1 : 0;
    uint16_t r = (uint16_t)dst + v + c;
    set_flag(CC_H, ((dst & 0x0F) + (v & 0x0F) + c) & 0x10);
    set_flag(CC_C, r & 0x100);
    set_flag(CC_V, (~(dst ^ v) & (dst ^ r)) & 0x80);
    dst = (uint8_t)r;
    set_NZ8(dst);
}

void cpu_m6809::op_sub8(uint8_t& dst, uint8_t v)
{
    uint16_t r = (uint16_t)dst - v;
    set_flag(CC_C, r & 0x100);
    set_flag(CC_V, ((dst ^ v) & (dst ^ r)) & 0x80);
    dst = (uint8_t)r;
    set_NZ8(dst);
}

void cpu_m6809::op_sbc8(uint8_t& dst, uint8_t v)
{
    uint8_t c = get_flag(CC_C) ? 1 : 0;
    uint16_t r = (uint16_t)dst - v - c;
    set_flag(CC_C, r & 0x100);
    set_flag(CC_V, ((dst ^ v) & (dst ^ r)) & 0x80);
    dst = (uint8_t)r;
    set_NZ8(dst);
}

void cpu_m6809::op_and8(uint8_t& dst, uint8_t v)
{
    dst &= v;
    set_NZ8(dst);
    set_flag(CC_V, false);
}

void cpu_m6809::op_or8(uint8_t& dst, uint8_t v)
{
    dst |= v;
    set_NZ8(dst);
    set_flag(CC_V, false);
}

void cpu_m6809::op_eor8(uint8_t& dst, uint8_t v)
{
    dst ^= v;
    set_NZ8(dst);
    set_flag(CC_V, false);
}

void cpu_m6809::op_cmp8(uint8_t a, uint8_t v)
{
    uint16_t r = (uint16_t)a - v;
    set_flag(CC_C, r & 0x100);
    set_flag(CC_V, ((a ^ v) & (a ^ r)) & 0x80);
    set_NZ8((uint8_t)r);
}

void cpu_m6809::op_bit8(uint8_t a, uint8_t v)
{
    uint8_t r = a & v;
    set_NZ8(r);
    set_flag(CC_V, false);
}

uint8_t cpu_m6809::op_neg8(uint8_t v)
{
    uint16_t r = (uint16_t)0 - v;
    set_flag(CC_C, r & 0x100);
    set_flag(CC_V, v == 0x80);
    uint8_t res = (uint8_t)r;
    set_NZ8(res);
    return res;
}

uint8_t cpu_m6809::op_com8(uint8_t v)
{
    uint8_t res = (uint8_t)~v;
    set_NZ8(res);
    set_flag(CC_V, false);
    set_flag(CC_C, true);
    return res;
}

uint8_t cpu_m6809::op_clr8()
{
    set_flag(CC_N, false);
    set_flag(CC_Z, true);
    set_flag(CC_V, false);
    set_flag(CC_C, false);
    return 0;
}

uint8_t cpu_m6809::op_inc8(uint8_t v)
{
    uint8_t res = (uint8_t)(v + 1);
    set_flag(CC_V, v == 0x7F);
    set_NZ8(res);
    return res;
}

uint8_t cpu_m6809::op_dec8(uint8_t v)
{
    uint8_t res = (uint8_t)(v - 1);
    set_flag(CC_V, v == 0x80);
    set_NZ8(res);
    return res;
}

uint8_t cpu_m6809::op_tst8(uint8_t v)
{
    set_NZ8(v);
    set_flag(CC_V, false);
    return v;
}

uint8_t cpu_m6809::op_lsr8(uint8_t v)
{
    set_flag(CC_C, v & 0x01);
    uint8_t res = (uint8_t)(v >> 1);
    set_flag(CC_N, false);
    set_flag(CC_Z, res == 0);
    return res;
}

uint8_t cpu_m6809::op_asr8(uint8_t v)
{
    set_flag(CC_C, v & 0x01);
    uint8_t res = (uint8_t)((v >> 1) | (v & 0x80));
    set_NZ8(res);
    return res;
}

uint8_t cpu_m6809::op_asl8(uint8_t v)
{
    set_flag(CC_C, v & 0x80);
    set_flag(CC_V, ((v ^ (v << 1)) & 0x80) != 0);
    uint8_t res = (uint8_t)(v << 1);
    set_NZ8(res);
    return res;
}

uint8_t cpu_m6809::op_rol8(uint8_t v)
{
    uint8_t c = get_flag(CC_C) ? 1 : 0;
    set_flag(CC_C, v & 0x80);
    set_flag(CC_V, ((v ^ (v << 1)) & 0x80) != 0);
    uint8_t res = (uint8_t)((v << 1) | c);
    set_NZ8(res);
    return res;
}

uint8_t cpu_m6809::op_ror8(uint8_t v)
{
    uint8_t c = get_flag(CC_C) ? 0x80 : 0;
    set_flag(CC_C, v & 0x01);
    uint8_t res = (uint8_t)((v >> 1) | c);
    set_NZ8(res);
    return res;
}

void cpu_m6809::op_daa()
{
    uint8_t a = m_D.d8.A;
    uint8_t correction = 0;
    bool cf = get_flag(CC_C);

    if (get_flag(CC_H) || (a & 0x0F) > 0x09) correction |= 0x06;
    if (cf || (a >> 4) > 0x09 || ((a >> 4) >= 0x09 && (a & 0x0F) > 0x09))
        correction |= 0x60;

    uint16_t r = (uint16_t)a + correction;
    set_flag(CC_C, cf || (r & 0x100));
    m_D.d8.A = (uint8_t)r;
    set_NZ8(m_D.d8.A);
    set_flag(CC_V, false);
}

// =============================================================================
// 16-bit operation helpers
// =============================================================================

void cpu_m6809::op_ld16(uint16_t& dst, uint16_t v)
{
    dst = v;
    set_NZ16(v);
    set_flag(CC_V, false);
}

uint16_t cpu_m6809::op_add16(uint16_t a, uint16_t b)
{
    uint32_t r = (uint32_t)a + b;
    set_flag(CC_C, r & 0x10000);
    set_flag(CC_V, (~(a ^ b) & (a ^ (uint16_t)r)) & 0x8000);
    uint16_t res = (uint16_t)r;
    set_NZ16(res);
    return res;
}

uint16_t cpu_m6809::op_sub16(uint16_t a, uint16_t b)
{
    uint32_t r = (uint32_t)a - b;
    set_flag(CC_C, r & 0x10000);
    set_flag(CC_V, ((a ^ b) & (a ^ (uint16_t)r)) & 0x8000);
    uint16_t res = (uint16_t)r;
    set_NZ16(res);
    return res;
}

void cpu_m6809::op_cmp16(uint16_t a, uint16_t v)
{
    uint32_t r = (uint32_t)a - v;
    set_flag(CC_C, r & 0x10000);
    set_flag(CC_V, ((a ^ v) & (a ^ (uint16_t)r)) & 0x8000);
    set_NZ16((uint16_t)r);
}

// =============================================================================
// PSH/PUL register-list helpers
//   PSHS/PSHU stacking order (high address first): PC,U/S,Y,X,DP,B,A,CC
//   PULS/PULU pull in the reverse order: CC,A,B,DP,X,Y,U/S,PC
//   Bit map of the postbyte: 7=PC 6=U/S 5=Y 4=X 3=DP 2=B 1=A 0=CC
// =============================================================================

int cpu_m6809::push_post(uint8_t pb, bool to_u)
{
    int bytes = 0;
    // The "other" stack pointer register is pushed when bit 6 is set.
    uint16_t other = to_u ? m_S : m_U;

    if (pb & 0x80) { if (to_u) { push_u8((uint8_t)m_PC); push_u8((uint8_t)(m_PC >> 8)); } else { push_s8((uint8_t)m_PC); push_s8((uint8_t)(m_PC >> 8)); } bytes += 2; }
    if (pb & 0x40) { if (to_u) { push_u8((uint8_t)other); push_u8((uint8_t)(other >> 8)); } else { push_s8((uint8_t)other); push_s8((uint8_t)(other >> 8)); } bytes += 2; }
    if (pb & 0x20) { if (to_u) { push_u8((uint8_t)m_Y); push_u8((uint8_t)(m_Y >> 8)); } else { push_s8((uint8_t)m_Y); push_s8((uint8_t)(m_Y >> 8)); } bytes += 2; }
    if (pb & 0x10) { if (to_u) { push_u8((uint8_t)m_X); push_u8((uint8_t)(m_X >> 8)); } else { push_s8((uint8_t)m_X); push_s8((uint8_t)(m_X >> 8)); } bytes += 2; }
    if (pb & 0x08) { if (to_u) push_u8(m_DP); else push_s8(m_DP); bytes += 1; }
    if (pb & 0x04) { if (to_u) push_u8(m_D.d8.B); else push_s8(m_D.d8.B); bytes += 1; }
    if (pb & 0x02) { if (to_u) push_u8(m_D.d8.A); else push_s8(m_D.d8.A); bytes += 1; }
    if (pb & 0x01) { if (to_u) push_u8(m_CC); else push_s8(m_CC); bytes += 1; }
    return bytes;
}

int cpu_m6809::pull_post(uint8_t pb, bool from_u)
{
    int bytes = 0;
    auto pull8 = [&](void) -> uint8_t { return from_u ? pull_u8() : pull_s8(); };

    if (pb & 0x01) { m_CC = pull8(); bytes += 1; }
    if (pb & 0x02) { m_D.d8.A = pull8(); bytes += 1; }
    if (pb & 0x04) { m_D.d8.B = pull8(); bytes += 1; }
    if (pb & 0x08) { m_DP = pull8(); bytes += 1; }
    if (pb & 0x10) { uint16_t h = pull8(); m_X = (uint16_t)((h << 8) | pull8()); bytes += 2; }
    if (pb & 0x20) { uint16_t h = pull8(); m_Y = (uint16_t)((h << 8) | pull8()); bytes += 2; }
    if (pb & 0x40) { uint16_t h = pull8(); uint16_t v = (uint16_t)((h << 8) | pull8());
                     if (from_u) { m_S = v; m_nmi_enabled = true; } else { m_U = v; } bytes += 2; }
    if (pb & 0x80) { uint16_t h = pull8(); m_PC = (uint16_t)((h << 8) | pull8()); bytes += 2; }
    return bytes;
}

// =============================================================================
// TFR / EXG register mapping
//   4-bit codes: 0=D 1=X 2=Y 3=U 4=S 5=PC 8=A 9=B 10=CC 11=DP
// =============================================================================

uint16_t cpu_m6809::reg16_by_tfr(uint8_t code)
{
    switch (code) {
    case 0x0: return m_D.D;
    case 0x1: return m_X;
    case 0x2: return m_Y;
    case 0x3: return m_U;
    case 0x4: return m_S;
    case 0x5: return m_PC;
    case 0x8: return (uint16_t)(0xFF00 | m_D.d8.A);
    case 0x9: return (uint16_t)(0xFF00 | m_D.d8.B);
    case 0xA: return (uint16_t)(0xFF00 | m_CC);
    case 0xB: return (uint16_t)(0xFF00 | m_DP);
    default:  return 0xFFFF;
    }
}

void cpu_m6809::set_reg16_by_tfr(uint8_t code, uint16_t v)
{
    switch (code) {
    case 0x0: m_D.D = v; break;
    case 0x1: m_X = v; break;
    case 0x2: m_Y = v; break;
    case 0x3: m_U = v; break;
    case 0x4: m_S = v; m_nmi_enabled = true; break;
    case 0x5: m_PC = v; break;
    case 0x8: m_D.d8.A = (uint8_t)v; break;
    case 0x9: m_D.d8.B = (uint8_t)v; break;
    case 0xA: m_CC = (uint8_t)v; break;
    case 0xB: m_DP = (uint8_t)v; break;
    default:  break;
    }
}

void cpu_m6809::op_tfr_exg(uint8_t pb, bool exchange)
{
    uint8_t src = (pb >> 4) & 0x0F;
    uint8_t dst = pb & 0x0F;

    uint16_t sval = reg16_by_tfr(src);
    if (exchange) {
        uint16_t dval = reg16_by_tfr(dst);
        set_reg16_by_tfr(src, dval);
        set_reg16_by_tfr(dst, sval);
    } else {
        set_reg16_by_tfr(dst, sval);
    }
}

// =============================================================================
// Branch helpers
// =============================================================================

bool cpu_m6809::test_branch_cond(uint8_t code)
{
    bool C = get_flag(CC_C), V = get_flag(CC_V), Z = get_flag(CC_Z), N = get_flag(CC_N);
    switch (code & 0x0F) {
    case 0x0: return true;                 // BRA
    case 0x1: return false;                // BRN
    case 0x2: return !C && !Z;             // BHI
    case 0x3: return C || Z;               // BLS
    case 0x4: return !C;                   // BCC / BHS
    case 0x5: return C;                    // BCS / BLO
    case 0x6: return !Z;                   // BNE
    case 0x7: return Z;                    // BEQ
    case 0x8: return !V;                   // BVC
    case 0x9: return V;                    // BVS
    case 0xA: return !N;                   // BPL
    case 0xB: return N;                    // BMI
    case 0xC: return N == V;               // BGE
    case 0xD: return N != V;               // BLT
    case 0xE: return !Z && (N == V);       // BGT
    case 0xF: return Z || (N != V);        // BLE
    }
    return false;
}

void cpu_m6809::branch_short(bool taken)
{
    int8_t off = (int8_t)fetch8();
    if (taken) m_PC = (uint16_t)(m_PC + off);
}

int cpu_m6809::branch_long(bool taken)
{
    int16_t off = (int16_t)fetch16();
    if (taken) { m_PC = (uint16_t)(m_PC + off); return 6; }
    return 5;
}

// =============================================================================
// Interrupt handling
// =============================================================================

void cpu_m6809::service_interrupt(uint16_t vector, bool set_F, bool entire)
{
    if (m_cwai) {
        // Registers already stacked by CWAI; just take the vector.
        m_cwai = false;
    } else {
        set_flag(CC_E, entire);
        if (entire) {
            // Full set: PC,U,Y,X,DP,B,A,CC (high address to low).
            push_s16(m_PC);
            push_s16(m_U);
            push_s16(m_Y);
            push_s16(m_X);
            push_s8(m_DP);
            push_s8(m_D.d8.B);
            push_s8(m_D.d8.A);
            push_s8(m_CC);
        } else {
            // Fast (FIRQ): PC,CC only.
            push_s16(m_PC);
            push_s8(m_CC);
        }
    }
    set_flag(CC_I, true);
    if (set_F) set_flag(CC_F, true);
    m_PC = read16(vector);
    m_pc_after_last_fetch = m_PC;
    notify_pc_change();     // entering the handler is a non-sequential PC change
}

void cpu_m6809::do_rti(int& cycles)
{
    m_CC = pull_s8();
    if (m_CC & CC_E) {
        m_D.d8.A = pull_s8();
        m_D.d8.B = pull_s8();
        m_DP     = pull_s8();
        m_X      = pull_s16();
        m_Y      = pull_s16();
        m_U      = pull_s16();
        m_PC     = pull_s16();
        cycles -= 15;
    } else {
        m_PC = pull_s16();
        cycles -= 6;
    }
}

// =============================================================================
// Main execution loop
// =============================================================================

// Single-instruction step, modeled on the z80 core's mz80step(): execute exactly
// one instruction (or service exactly one pending, unmasked interrupt) and return
// the number of cycles it consumed.
//
// Implemented on top of the validated exec() loop rather than duplicating the big
// opcode switch: handing exec() a 1-cycle budget runs its while-body exactly once
// (one interrupt-dispatch-or-fetch+execute), after which the budget goes negative
// and the loop exits. Consumed cycles = 1 - leftover.
//
// NOTE: unlike mz80step(), this does NOT update peripheral timers. In the vecx
// integration the host (vecx_emu) advances the VIA/analog engine for each returned
// cycle, so the CPU core stays peripheral-agnostic.
int cpu_m6809::step()
{
    int leftover = exec(1);
    int used = 1 - leftover;
    // SYNC/CWAI idle with no interrupt pending consumes 0 cycles in exec(); force
    // at least one cycle of forward progress so the caller's peripherals keep
    // advancing and can raise the interrupt that wakes the CPU. Without this the
    // vecx loop would spin with the VIA frozen and never deliver the wake IRQ.
    if (used < 1) {
        used = 1;
        clocktickstotal += 1;   // keep get_ticks() in step with the cycles reported
    }
    return used;
}

int cpu_m6809::exec(int cycles)
{
    while (cycles > 0)
    {
        int last_cycles = cycles;

        // ---- Interrupt / wait-state check (before fetch) ------------------
        if (m_sync) {
            if (m_nmi_line || m_nmi_pending || m_firq_line || m_firq_pending || m_irq_line || m_irq_pending) {
                m_sync = false;     // wake up; fall through to service if unmasked
            } else {
                clocktickstotal += abs(last_cycles - cycles);
                break;              // still waiting; give back the timeslice
            }
        }

        const bool was_cwai = m_cwai;
        if (m_nmi_pending && m_nmi_enabled) {
            service_interrupt(0xFFFC, true, true);
            m_nmi_pending = false;
            cycles -= was_cwai ? 7 : 19;
            clocktickstotal += abs(last_cycles - cycles);
            continue;
        }
        else if ((m_firq_line || m_firq_pending) && !get_flag(CC_F)) {
            service_interrupt(0xFFF6, true, false);
            m_firq_pending = false;
            cycles -= was_cwai ? 7 : 10;
            clocktickstotal += abs(last_cycles - cycles);
            continue;
        }
        else if ((m_irq_line || m_irq_pending) && !get_flag(CC_I)) {
            service_interrupt(0xFFF8, false, true);
            m_irq_pending = false;
            cycles -= was_cwai ? 7 : 19;
            clocktickstotal += abs(last_cycles - cycles);
            continue;
        }

        // CWAI only wakes for an accepted interrupt; masked inputs do not wake it.
        if (m_cwai) break;

        // ---- Fetch & decode ----------------------------------------------
        m_PPC = m_PC;
        uint8_t op = fetch_opcode();
        m_last_opcode = op;
        int extra = 0;

        switch (op)
        {
        // ----- Prefix pages -----
        case 0x10: cycles -= 1 + exec_page10(); break;
        case 0x11: cycles -= 1 + exec_page11(); break;

        // ----- Misc / inherent control -----
        case 0x12: cycles -= 2; break;                              // NOP
        case 0x13: m_sync = true; cycles -= 4; break;              // SYNC
        case 0x16: { int16_t o = (int16_t)fetch16(); m_PC = (uint16_t)(m_PC + o); cycles -= 5; } break; // LBRA
        case 0x17: { int16_t o = (int16_t)fetch16(); push_s16(m_PC); m_PC = (uint16_t)(m_PC + o); cycles -= 9; } break; // LBSR
        case 0x19: op_daa(); cycles -= 2; break;                   // DAA
        case 0x1A: m_CC |= fetch8(); cycles -= 3; break;           // ORCC
        case 0x1C: m_CC &= fetch8(); cycles -= 3; break;           // ANDCC
        case 0x1D: // SEX
            m_D.d8.A = (m_D.d8.B & 0x80) ? 0xFF : 0x00;
            set_NZ16(m_D.D);
            cycles -= 2; break;
        case 0x1E: op_tfr_exg(fetch8(), true);  cycles -= 8; break; // EXG
        case 0x1F: op_tfr_exg(fetch8(), false); cycles -= 6; break; // TFR

        // ----- Short branches 0x20-0x2F -----
        case 0x20: case 0x21: case 0x22: case 0x23:
        case 0x24: case 0x25: case 0x26: case 0x27:
        case 0x28: case 0x29: case 0x2A: case 0x2B:
        case 0x2C: case 0x2D: case 0x2E: case 0x2F:
            branch_short(test_branch_cond(op & 0x0F));
            cycles -= 3; break;

        // ----- LEA -----
        case 0x30: m_X = ea_indexed(extra); set_flag(CC_Z, m_X == 0); cycles -= 4 + extra; break; // LEAX
        case 0x31: m_Y = ea_indexed(extra); set_flag(CC_Z, m_Y == 0); cycles -= 4 + extra; break; // LEAY
        case 0x32: m_S = ea_indexed(extra); m_nmi_enabled = true;     cycles -= 4 + extra; break; // LEAS
        case 0x33: m_U = ea_indexed(extra);                          cycles -= 4 + extra; break; // LEAU

        // ----- Push / pull -----
        case 0x34: cycles -= 5 + push_post(fetch8(), false); break;  // PSHS
        case 0x35: cycles -= 5 + pull_post(fetch8(), false); break;  // PULS
        case 0x36: cycles -= 5 + push_post(fetch8(), true);  break;  // PSHU
        case 0x37: cycles -= 5 + pull_post(fetch8(), true);  break;  // PULU

        // ----- Returns / misc -----
        case 0x39: m_PC = pull_s16(); cycles -= 5; break;            // RTS
        case 0x3A: m_X = (uint16_t)(m_X + m_D.d8.B); cycles -= 3; break; // ABX
        case 0x3B: do_rti(cycles); break;                            // RTI
        case 0x3C: { // CWAI
            uint8_t mask = fetch8();
            m_CC &= mask;
            set_flag(CC_E, true);
            push_s16(m_PC); push_s16(m_U); push_s16(m_Y); push_s16(m_X);
            push_s8(m_DP); push_s8(m_D.d8.B); push_s8(m_D.d8.A); push_s8(m_CC);
            m_cwai = true; m_sync = false;
            cycles -= 20;
        } break;
        case 0x3D: { // MUL
            uint16_t r = (uint16_t)m_D.d8.A * (uint16_t)m_D.d8.B;
            m_D.D = r;
            set_flag(CC_Z, r == 0);
            set_flag(CC_C, r & 0x80);
            cycles -= 11;
        } break;
        case 0x3F: // SWI
            set_flag(CC_E, true);
            push_s16(m_PC); push_s16(m_U); push_s16(m_Y); push_s16(m_X);
            push_s8(m_DP); push_s8(m_D.d8.B); push_s8(m_D.d8.A); push_s8(m_CC);
            set_flag(CC_I, true); set_flag(CC_F, true);
            m_PC = read16(0xFFFA);
            cycles -= 19; break;

        // ----- Memory read-modify-write & inherent A/B (0x00-0x0F, 0x40-0x7F) -----
        case 0x00: case 0x03: case 0x04: case 0x06: case 0x07: case 0x08:
        case 0x09: case 0x0A: case 0x0C: case 0x0D: case 0x0E: case 0x0F: // direct RMW
        case 0x60: case 0x63: case 0x64: case 0x66: case 0x67: case 0x68:
        case 0x69: case 0x6A: case 0x6C: case 0x6D: case 0x6E: case 0x6F: // indexed RMW
        case 0x70: case 0x73: case 0x74: case 0x76: case 0x77: case 0x78:
        case 0x79: case 0x7A: case 0x7C: case 0x7D: case 0x7E: case 0x7F: // extended RMW
        {
            uint8_t hi = op & 0xF0, fn = op & 0x0F;
            uint16_t ea;
            int base;
            if (hi == 0x00)      { ea = ea_direct();        base = 6; }
            else if (hi == 0x60) { ea = ea_indexed(extra);  base = 6; }
            else                 { ea = ea_extended();      base = 7; }

            if (fn == 0x0E) { // JMP
                m_PC = ea;
                cycles -= (hi == 0x00) ? 3 : (hi == 0x60) ? (3 + extra) : 4;
                break;
            }
            if (fn == 0x0D) { // TST
                op_tst8(read8(ea));
                cycles -= base + extra;
                break;
            }
            if (fn == 0x0F) { // CLR
                write8(ea, op_clr8());
                cycles -= base + extra;
                break;
            }
            uint8_t v = read8(ea), r = v;
            switch (fn) {
            case 0x00: r = op_neg8(v); break;
            case 0x03: r = op_com8(v); break;
            case 0x04: r = op_lsr8(v); break;
            case 0x06: r = op_ror8(v); break;
            case 0x07: r = op_asr8(v); break;
            case 0x08: r = op_asl8(v); break;
            case 0x09: r = op_rol8(v); break;
            case 0x0A: r = op_dec8(v); break;
            case 0x0C: r = op_inc8(v); break;
            }
            write8(ea, r);
            cycles -= base + extra;
        } break;

        case 0x40: case 0x43: case 0x44: case 0x46: case 0x47: case 0x48:
        case 0x49: case 0x4A: case 0x4C: case 0x4D: case 0x4F:            // A inherent
        case 0x50: case 0x53: case 0x54: case 0x56: case 0x57: case 0x58:
        case 0x59: case 0x5A: case 0x5C: case 0x5D: case 0x5F:            // B inherent
        {
            uint8_t& reg = (op & 0x10) ? m_D.d8.B : m_D.d8.A;
            switch (op & 0x0F) {
            case 0x00: reg = op_neg8(reg); break;  // NEG
            case 0x03: reg = op_com8(reg); break;  // COM
            case 0x04: reg = op_lsr8(reg); break;  // LSR
            case 0x06: reg = op_ror8(reg); break;  // ROR
            case 0x07: reg = op_asr8(reg); break;  // ASR
            case 0x08: reg = op_asl8(reg); break;  // ASL/LSL
            case 0x09: reg = op_rol8(reg); break;  // ROL
            case 0x0A: reg = op_dec8(reg); break;  // DEC
            case 0x0C: reg = op_inc8(reg); break;  // INC
            case 0x0D: op_tst8(reg); break;        // TST
            case 0x0F: reg = op_clr8(); break;     // CLR
            }
            cycles -= 2;
        } break;

        // ----- ALU / load / store block 0x80-0xFF -----
        default:
            if (op >= 0x80) {
                uint8_t mode = (op >> 4) & 0x03;   // 0=imm 1=dir 2=idx 3=ext
                uint8_t fn   = op & 0x0F;
                bool    bpag = (op & 0x40) != 0;   // false:A-page, true:B-page
                uint16_t ea  = 0;

                if (mode == 1)      ea = ea_direct();
                else if (mode == 2) ea = ea_indexed(extra);
                else if (mode == 3) ea = ea_extended();

                // Cycle bases indexed by mode.
                static const int c8 [4] = { 2, 4, 4, 5 }; // 8-bit alu (+extra for idx)
                static const int c16l[4] = { 3, 5, 5, 6 }; // 16-bit load/store
                static const int c16a[4] = { 4, 6, 6, 7 }; // 16-bit arith
                static const int cjsr[4] = { 0, 7, 7, 8 }; // JSR (imm slot is BSR)

                #define RD8()  (mode == 0 ? fetch8()  : read8(ea))
                #define RD16() (mode == 0 ? fetch16() : read16(ea))

                if (!bpag) { // 0x80-0xBF : A-page
                    switch (fn) {
                    case 0x0: op_sub8(m_D.d8.A, RD8()); cycles -= c8[mode] + extra; break; // SUBA
                    case 0x1: op_cmp8(m_D.d8.A, RD8()); cycles -= c8[mode] + extra; break; // CMPA
                    case 0x2: op_sbc8(m_D.d8.A, RD8()); cycles -= c8[mode] + extra; break; // SBCA
                    case 0x3: m_D.D = op_sub16(m_D.D, RD16()); cycles -= c16a[mode] + extra; break; // SUBD
                    case 0x4: op_and8(m_D.d8.A, RD8()); cycles -= c8[mode] + extra; break; // ANDA
                    case 0x5: op_bit8(m_D.d8.A, RD8()); cycles -= c8[mode] + extra; break; // BITA
                    case 0x6: op_ld8(m_D.d8.A, RD8());  cycles -= c8[mode] + extra; break; // LDA
                    case 0x7: write8(ea, m_D.d8.A); set_NZ8(m_D.d8.A); set_flag(CC_V, false); cycles -= c8[mode] + extra; break; // STA
                    case 0x8: op_eor8(m_D.d8.A, RD8()); cycles -= c8[mode] + extra; break; // EORA
                    case 0x9: op_adc8(m_D.d8.A, RD8()); cycles -= c8[mode] + extra; break; // ADCA
                    case 0xA: op_or8(m_D.d8.A, RD8());  cycles -= c8[mode] + extra; break; // ORA
                    case 0xB: op_add8(m_D.d8.A, RD8()); cycles -= c8[mode] + extra; break; // ADDA
                    case 0xC: op_cmp16(m_X, RD16()); cycles -= c16a[mode] + extra; break; // CMPX
                    case 0xD: // JSR / BSR
                        if (mode == 0) { int8_t o = (int8_t)fetch8(); push_s16(m_PC); m_PC = (uint16_t)(m_PC + o); cycles -= 7; } // BSR
                        else { push_s16(m_PC); m_PC = ea; cycles -= cjsr[mode] + extra; }
                        break;
                    case 0xE: op_ld16(m_X, RD16()); cycles -= c16l[mode] + extra; break; // LDX
                    case 0xF: write16(ea, m_X); set_NZ16(m_X); set_flag(CC_V, false); cycles -= c16l[mode] + extra; break; // STX
                    }
                } else { // 0xC0-0xFF : B-page
                    switch (fn) {
                    case 0x0: op_sub8(m_D.d8.B, RD8()); cycles -= c8[mode] + extra; break; // SUBB
                    case 0x1: op_cmp8(m_D.d8.B, RD8()); cycles -= c8[mode] + extra; break; // CMPB
                    case 0x2: op_sbc8(m_D.d8.B, RD8()); cycles -= c8[mode] + extra; break; // SBCB
                    case 0x3: m_D.D = op_add16(m_D.D, RD16()); cycles -= c16a[mode] + extra; break; // ADDD
                    case 0x4: op_and8(m_D.d8.B, RD8()); cycles -= c8[mode] + extra; break; // ANDB
                    case 0x5: op_bit8(m_D.d8.B, RD8()); cycles -= c8[mode] + extra; break; // BITB
                    case 0x6: op_ld8(m_D.d8.B, RD8());  cycles -= c8[mode] + extra; break; // LDB
                    case 0x7: write8(ea, m_D.d8.B); set_NZ8(m_D.d8.B); set_flag(CC_V, false); cycles -= c8[mode] + extra; break; // STB
                    case 0x8: op_eor8(m_D.d8.B, RD8()); cycles -= c8[mode] + extra; break; // EORB
                    case 0x9: op_adc8(m_D.d8.B, RD8()); cycles -= c8[mode] + extra; break; // ADCB
                    case 0xA: op_or8(m_D.d8.B, RD8());  cycles -= c8[mode] + extra; break; // ORB
                    case 0xB: op_add8(m_D.d8.B, RD8()); cycles -= c8[mode] + extra; break; // ADDB
                    case 0xC: op_ld16(m_D.D, RD16()); cycles -= c16l[mode] + extra; break; // LDD
                    case 0xD: write16(ea, m_D.D); set_NZ16(m_D.D); set_flag(CC_V, false); cycles -= c16l[mode] + extra; break; // STD
                    case 0xE: op_ld16(m_U, RD16()); cycles -= c16l[mode] + extra; break; // LDU
                    case 0xF: write16(ea, m_U); set_NZ16(m_U); set_flag(CC_V, false); cycles -= c16l[mode] + extra; break; // STU
                    }
                }
                #undef RD8
                #undef RD16
            }
            else {
                LOG_INFO("CPU%d: Unrecognized opcode @%04X: %02X", cpu_num, (uint16_t)(m_PC - 1), op);
                cycles -= 2;
            }
            break;
        }

        // If the instruction moved the PC anywhere other than straight past its
        // own last operand byte, it was a control transfer (jump / call / return
        // / taken branch / SWI / PULS-PC / TFR-to-PC). Notify any registered
        // add-on exactly once, here, instead of sprinkling calls through the
        // decoder. (Interrupts/reset notify from their own paths.)
        if (m_PC != m_pc_after_last_fetch)
            notify_pc_change();

        clocktickstotal += abs(last_cycles - cycles);
        if (clocktickstotal > 0xfffffff) clocktickstotal = 0;
    }

    return cycles;
}

// =============================================================================
// Page 2 (0x10 prefix) : LBxx, CMPD, CMPY, LDY/STY, LDS/STS, SWI2
//   Returns cycles consumed by the sub-instruction (caller adds 1 prefix cycle).
// =============================================================================

int cpu_m6809::exec_page10()
{
    uint8_t op = fetch_opcode();
    m_last_opcode = op;
    int extra = 0;

    // Long conditional branches 0x21-0x2F.
    if (op >= 0x21 && op <= 0x2F)
        return branch_long(test_branch_cond(op & 0x0F)) - 1; // caller charges prefix

    if (op == 0x3F) { // SWI2 (does not set I/F)
        set_flag(CC_E, true);
        push_s16(m_PC); push_s16(m_U); push_s16(m_Y); push_s16(m_X);
        push_s8(m_DP); push_s8(m_D.d8.B); push_s8(m_D.d8.A); push_s8(m_CC);
        m_PC = read16(0xFFF4);
        return 19;
    }

    uint8_t mode = (op >> 4) & 0x03;  // 0=imm 1=dir 2=idx 3=ext
    uint8_t fn   = op & 0x0F;
    bool    bpag = (op & 0x40) != 0;  // false: CMPD/CMPY/LDY/STY ; true: LDS/STS
    uint16_t ea  = 0;
    if (mode == 1)      ea = ea_direct();
    else if (mode == 2) ea = ea_indexed(extra);
    else if (mode == 3) ea = ea_extended();

    static const int c16l[4] = { 3, 5, 5, 6 };
    static const int c16a[4] = { 4, 6, 6, 7 };
    #define RD16() (mode == 0 ? fetch16() : read16(ea))

    if (!bpag) { // A-page columns
        switch (fn) {
        case 0x3: op_cmp16(m_D.D, RD16()); return c16a[mode] + extra; // CMPD
        case 0xC: op_cmp16(m_Y,   RD16()); return c16a[mode] + extra; // CMPY
        case 0xE: op_ld16(m_Y,    RD16()); return c16l[mode] + extra; // LDY
        case 0xF: write16(ea, m_Y); set_NZ16(m_Y); set_flag(CC_V, false); return c16l[mode] + extra; // STY
        }
    } else {     // B-page columns: LDS / STS
        switch (fn) {
        case 0xE: op_ld16(m_S, RD16()); m_nmi_enabled = true; return c16l[mode] + extra; // LDS
        case 0xF: write16(ea, m_S); set_NZ16(m_S); set_flag(CC_V, false); return c16l[mode] + extra; // STS
        }
    }
    #undef RD16

    LOG_INFO("CPU%d: Unrecognized page-2 opcode @%04X: 10 %02X", cpu_num, (uint16_t)(m_PC - 1), op);
    return 2;
}

// =============================================================================
// Page 3 (0x11 prefix) : CMPU, CMPS, SWI3
// =============================================================================

int cpu_m6809::exec_page11()
{
    uint8_t op = fetch_opcode();
    m_last_opcode = op;
    int extra = 0;

    if (op == 0x3F) { // SWI3 (does not set I/F)
        set_flag(CC_E, true);
        push_s16(m_PC); push_s16(m_U); push_s16(m_Y); push_s16(m_X);
        push_s8(m_DP); push_s8(m_D.d8.B); push_s8(m_D.d8.A); push_s8(m_CC);
        m_PC = read16(0xFFF2);
        return 19;
    }

    uint8_t mode = (op >> 4) & 0x03;
    uint8_t fn   = op & 0x0F;
    uint16_t ea  = 0;
    if (mode == 1)      ea = ea_direct();
    else if (mode == 2) ea = ea_indexed(extra);
    else if (mode == 3) ea = ea_extended();

    static const int c16a[4] = { 4, 6, 6, 7 };
    #define RD16() (mode == 0 ? fetch16() : read16(ea))

    switch (fn) {
    case 0x3: op_cmp16(m_U, RD16()); return c16a[mode] + extra; // CMPU
    case 0xC: op_cmp16(m_S, RD16()); return c16a[mode] + extra; // CMPS
    }
    #undef RD16

    LOG_INFO("CPU%d: Unrecognized page-3 opcode @%04X: 11 %02X", cpu_num, (uint16_t)(m_PC - 1), op);
    return 2;
}
