#ifndef VIA6522_H
#define VIA6522_H

#include <stdint.h>

// -----------------------------------------------------------------------------
// Vecx VIA6522 API
// Drop-in replacement with improved handshake and timer behavior.
//
// Vectrex signal map (for reference):
//   ORA          -> 8-bit DAC input (also AY-3-8910 data bus)
//   ORB bit 0    -> SWITCH   (0 = enable analog mux, 1 = disable)
//   ORB bit 1-2  -> mux SEL0/SEL1 (selects DAC destination when mux enabled)
//   ORB bit 3    -> AY-3-8910 BC1
//   ORB bit 4    -> AY-3-8910 BDIR
//   ORB bit 5    -> COMPARE   (input, from joystick comparator op-amp)
//   ORB bit 6    -> cartridge/external line (input -> reads high)
//   ORB bit 7    -> /RAMP     (active low; T1-controlled when ACR7 set)
//   CA1          -> AY-3-8910 IO7
//   CA2          -> ZERO      (active low; manual output mode on Vectrex)
//   CB2          -> /BLANK    (active low; SR shift-out or manual PCR control)
// -----------------------------------------------------------------------------

// ---- Vectrex-side hooks implemented in vecx.cpp -----------------------------
// Called after ORB write (used to update sound mux and analog S/H logic).
void via_hook_on_orb_written(uint8_t orb, uint8_t ora);
// Called after ORA write (used to update DAC -> X sample/hold, then analog).
void via_hook_on_ora_written(uint8_t orb, uint8_t ora);
// When reading ORA: if sound chip is driving PA (orb&0x18)==0x08, return it;
// otherwise return the VIA's own ORA.
uint8_t via_hook_read_port_a(uint8_t orb, uint8_t ora);
// Bit 5 “compare” input for PB reads (0x20 if >, else 0).
uint8_t via_hook_get_compare_bit(void);
// Called after a T2 high-byte write (timer 2 loaded and armed). The Vectrex
// BIOS Wait_Recal re-arms T2 once per game frame.
void via_hook_on_t2_armed(void);

// ---- VIA 6522 API -----------------------------------------------------------
void     via_reset(void);
uint8_t  via_read_reg(uint8_t reg);           // reg = address & 0x0F
void     via_write_reg(uint8_t reg, uint8_t data);
void     via_sstep0(void);                    // first half of a VIA cycle
void     via_sstep1(void);                    // second half of a VIA cycle

// Optional: helpers so vecx can query signals it used to read directly.
uint8_t  via_get_ifr(void);
uint8_t  via_get_ier(void);
uint8_t  via_get_acr(void);
uint8_t  via_get_pcr(void);
uint8_t  via_get_ora(void);
uint8_t  via_get_orb(void);
uint8_t  via_get_ca2(void);    // logical level
uint8_t  via_get_cb2h(void);   // handshake CB2 level
uint8_t  via_get_cb2s(void);   // shift-register CB2 level (when ACR selects it)
uint8_t  via_get_cb2_output(void); // FIX: effective CB2 (/BLANK): SR in shift-out modes, else cb2h
uint8_t  via_get_t1pb7(void);  // PB7 as driven by T1 (when ACR7 set)

// IRQ line to CPU: bit7 of IFR mirrors (IFR&IER) != 0.
uint8_t  via_irq_level(void);  // 0 or 0x80

// External edge input functions (PET SYNC, keyboard strobe, etc.)
void via_signal_ca1_edge(uint8_t new_level);  // CA1 edge, sets IFR1
void via_signal_cb1_edge(uint8_t new_level);  // CB1 edge, sets IFR4
void via_signal_ca2_input(uint8_t new_level); // CA2 input mode, sets IFR0
void via_signal_cb2_input(uint8_t new_level); // CB2 input mode, sets IFR3

// Control line levels (as currently driven or latched)
uint8_t via_get_ca1_level(void);
uint8_t via_get_ca2_level(void);
uint8_t via_get_cb1_level(void);
uint8_t via_get_cb2_level(void);

// For PET: ORB readback with live IEEE handshake lines
uint8_t via_hook_read_port_b(uint8_t orb);

#endif /* VIA6522_H */
