// Vectrex-Emu
// Copyright (C) 2026 Tim Cottrill and Claude Code
//
// Based on vecx, the portable Vectrex emulator by Valavan Manohararajah.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include "via6522.h"
#include <string.h>

/* ------------------- VIA internal state (split into CL/CH) ----------------- */
static uint8_t  via_ora;
static uint8_t  via_orb;
static uint8_t  via_ddra;
static uint8_t  via_ddrb;

/* Timer 1 */
static uint8_t  via_t1cl;     // counter low
static uint8_t  via_t1ch;     // counter high
static uint8_t  via_t1ll;     // latch low
static uint8_t  via_t1lh;     // latch high
static uint8_t  via_t1pb7;    // PB7 under T1 control (0x80 or 0x00)
static uint8_t  via_t1_oneshot; // one-shot armed?

/* Timer 2 */
static uint8_t  via_t2cl;
static uint8_t  via_t2ch;
static uint8_t  via_t2ll;
static uint8_t  via_t2_oneshot;

/* Shift Register */
static uint8_t  via_sr;
static uint8_t  via_srb;    // #bits shifted so far (0..8)
static uint8_t  via_src;    // shift rate counter (reloads from T2LL)
static uint8_t  via_srclk;  // toggles every time src underflows (T2 clock)

/* Control and status */
static uint8_t  via_acr;
static uint8_t  via_pcr;
static uint8_t  via_ifr;
static uint8_t  via_ier;

/* Handshake outputs */
static uint8_t  via_ca2;
static uint8_t  via_cb2h;
static uint8_t  via_cb2s;

// -----------------------------------------------------------------------------
// External edge/level input support for CA1 / CA2 / CB1 / CB2
// These let external devices (SYNC, IEEE bus, keyboard strobe, cassette, etc.)
// drive the VIA control pins. They set IFR bits according to PCR mode.
// -----------------------------------------------------------------------------

static uint8_t ca1_level = 0;
static uint8_t cb1_level = 0;
static uint8_t ca2_level = 1;  // idle high unless PCR config changes
static uint8_t cb2_level = 1;

/* ------------------- Internal helpers ------------------------------------- */
static inline void int_update(void)
{
	if ((via_ifr & 0x7F) & (via_ier & 0x7F))
		via_ifr |= 0x80;
	else
		via_ifr &= 0x7F;
}

/* First half of VIA cycle (timers, SR progress) */
void via_sstep0(void)
{
	uint8_t t2shift = 0;

	/* Timer 1 */
	if (--via_t1cl == 0xFF) {
		if (--via_t1ch == 0xFF) {
			/* Underflow */
			if (via_acr & 0x40) {
				// continuous
				via_t1cl = via_t1ll;
				via_t1ch = via_t1lh;
				via_ifr |= 0x40;
				via_t1pb7 ^= 0x80;
				int_update();
				via_t1_oneshot = 1;
			}
			else {
				if (via_t1_oneshot) {
					via_ifr |= 0x40;
					int_update();
					via_t1pb7 = 0x80;   // one-shot done -> RAMP inactive (PB7 high)
					via_t1_oneshot = 0;
				}
			}
		}
	}

	/* Timer 2 free-run one-shot (no event count mode for Vectrex) */
	if ((via_acr & 0x20) == 0) {
		if (--via_t2cl == 0xFF) {
			if (--via_t2ch == 0xFF) {
				if (via_t2_oneshot) {
					via_ifr |= 0x20;
					int_update();
					via_t2_oneshot = 0;
				}
			}
		}
	}

	/* Shift rate counter (src toggles SR clock using T2LL as divider) */
	if (--via_src == 0xFF) {
		via_src = via_t2ll;
		if (via_srclk) {
			t2shift = 1;
			via_srclk = 0;
		}
		else {
			t2shift = 0;
			via_srclk = 1;
		}
	}

	if (via_srb < 8) {
		switch (via_acr & 0x1C) {
		case 0x00: /* disabled */ break;

		// FIX: shift-IN modes now sample the CB2 input pin into the LSB
		//      instead of always clocking in zeros.
		case 0x04: /* mode 001: shift in under T2 */
			if (t2shift) { via_sr = (uint8_t)((via_sr << 1) | (cb2_level & 1)); via_srb++; }
			break;
		case 0x08: /* mode 010: shift in under system clk */
			via_sr = (uint8_t)((via_sr << 1) | (cb2_level & 1)); via_srb++;
			break;
		case 0x0C: /* mode 011: shift in under CB1 (external clk) */ break;

		case 0x10: /* mode 100: shift out under T2, free-running */
			// Free-run recirculates the MSB so the byte repeats forever; this
			// is correct for mode 100 and never increments srb (never stops).
			if (t2shift) {
				via_cb2s = (via_sr >> 7) & 1;
				via_sr = (uint8_t)((via_sr << 1) | via_cb2s);
			}
			break;

		// FIX: controlled shift-OUT modes (101/110) shift in 0, per datasheet,
		//      rather than recirculating the MSB. For SR=0xFF this is identical
		//      to the old behavior (solid bright vectors). It only differs for
		//      dashed/patterned blanking, where it now matches real hardware.
		//      If you ever WANT a self-repeating pattern, recirculate here:
		//        via_sr = (via_sr << 1) | via_cb2s;
		case 0x14: /* mode 101: shift out under T2 control (8 bits, then stop) */
			if (t2shift) {
				via_cb2s = (via_sr >> 7) & 1;
				via_sr = (uint8_t)(via_sr << 1);
				via_srb++;
			}
			break;
		case 0x18: /* mode 110: shift out under system clk (8 bits, then stop) */
			via_cb2s = (via_sr >> 7) & 1;
			via_sr = (uint8_t)(via_sr << 1);
			via_srb++;
			break;

		case 0x1C: /* mode 111: shift out under CB1 (external clk) */ break;
		}
		if (via_srb == 8) { via_ifr |= 0x04; int_update(); }
	}
}

/* Second half (restore pulse-mode handshakes) */
void via_sstep1(void)
{
	if ((via_pcr & 0x0E) == 0x0A) via_ca2 = 1; // CA2 pulse returns high
	if ((via_pcr & 0xE0) == 0xA0) via_cb2h = 1; // CB2 pulse returns high
}

void via_reset(void)
{
	via_ora = 0; via_orb = 0;
	via_ddra = 0; via_ddrb = 0;
	via_t1cl = 0xFF; via_t1ch = 0xFF; via_t1ll = 0xFF; via_t1lh = 0xFF;
	via_t1pb7 = 0x80; via_t1_oneshot = 0;
	via_t2cl = 0xFF; via_t2ch = 0xFF; via_t2ll = 0xFF; via_t2_oneshot = 0;
	via_sr = 0; via_srb = 8; via_src = 0; via_srclk = 0;
	via_acr = 0; via_pcr = 0; via_ifr = 0; via_ier = 0;
	via_ca2 = 1; via_cb2h = 1; via_cb2s = 0;
}

/* --------- Register access ------------------------------------------------- */

uint8_t via_read_reg(uint8_t reg)
{
	uint8_t data = 0xFF;
	switch (reg & 0x0F) {
	case 0x0: { /* ORB read (with compare, PB7 mux, IEEE/PET merge) */
		uint8_t cmp = via_hook_get_compare_bit(); // 0 or 0x20
		uint8_t live = via_orb;
		if (via_acr & 0x80) {
			// Timer 1 controls PB7
			data = (uint8_t)((live & 0x5F) | via_get_t1pb7() | cmp);
		}
		else {
			// PB7 comes from latch
			data = (uint8_t)((live & 0xDF) | cmp);
		}

		// FIX: a PB6 configured as input reads back high (Vectrex cartridge/
		//      external line behavior). Output PB6 keeps its latch value.
		if (!(via_ddrb & 0x40)) data |= 0x40;

		/* Clear CB1/CB2 interrupt flags if not independent mode.
		   NOTE: the 0xA0 mask here is intentional and correct -- it isolates
		   bits 7,5 so that only the independent CB2 input modes (001/011,
		   both bit7=0 bit5=1 -> 0x20) are exempted from auto-clear. */
		if ((via_pcr & 0xA0) != 0x20) via_ifr &= ~0x08; // CB2
		via_ifr &= ~0x10; // CB1 always cleared
		int_update();
	} break;
	case 0x1:   /* IRA read (handshake CA2 may pulse low, clears CA1/CA2 flags) */
		if ((via_pcr & 0x0E) == 0x08) via_ca2 = 0;
		/* Clear CA1/CA2 interrupt flags if not independent mode.
		   FIX: these clears belong to reg 1 only; reg $F (no handshake)
		   must not touch the flags, per datasheet. */
		if ((via_pcr & 0x0A) != 0x02) via_ifr &= ~0x01; // CA2
		via_ifr &= ~0x02; // CA1 always cleared
		int_update();
		/* fall through */
	case 0xF: { /* ORA (no handshake, no flag clears) */
		data = via_hook_read_port_a(via_orb, via_ora);
	} break;

	case 0x2: data = via_ddrb; break;
	case 0x3: data = via_ddra; break;

	case 0x4: /* T1CL (low) */
		// FIX: reading T1C-L on a real 6522 ONLY clears the T1 interrupt flag.
		//      It must NOT disarm the one-shot or touch PB7 (= /RAMP on the
		//      Vectrex). The old teardown here was killing vectors mid-draw.
		data = via_t1cl;
		via_ifr &= ~0x40; // clear T1 IFR
		int_update();
		break;
	case 0x5: /* T1CH (high) */ data = via_t1ch; break;
	case 0x6: /* T1LL */ data = via_t1ll; break;
	case 0x7: /* T1LH */ data = via_t1lh; break;

	case 0x8: /* T2CL */
		// FIX: reading T2C-L only clears the T2 interrupt flag; it must not
		//      disarm the timer.
		data = via_t2cl;
		via_ifr &= ~0x20;
		int_update();
		break;
	case 0x9: /* T2CH */ data = via_t2ch; break;

	case 0xA: /* SR */
		data = via_sr;
		via_ifr &= ~0x04;
		via_srb = 0; via_srclk = 1;
		int_update();
		break;

	case 0xB: data = via_acr; break;
	case 0xC: data = via_pcr; break;
	case 0xD: data = via_ifr; break;
	case 0xE: data = (uint8_t)(via_ier | 0x80); break;
	}
	return data;
}

void via_write_reg(uint8_t reg, uint8_t data)
{
	switch (reg & 0x0F) {
	case 0x0: /* ORB */
		if (via_acr & 0x80) {
			// PB7 under Timer 1 control -> ignore bit 7
			via_orb = (data & 0x7F) | (via_orb & 0x80);
		}
		else {
			via_orb = data;
		}
		via_hook_on_orb_written(via_orb, via_ora);
		/* FIX (PET tier-2 port): ORB WRITES clear CB1/CB2 flags just like
		   reads do (same independent-mode exemption, see the read path). */
		if ((via_pcr & 0xA0) != 0x20) via_ifr &= ~0x08; // CB2
		via_ifr &= ~0x10; // CB1 always cleared
		int_update();
		if ((via_pcr & 0xE0) == 0x80) via_cb2h = 0; // CB2 handshake low
		break;

	case 0x1: /* IRA (with CA2 handshake) then fallthrough to ORA */
		if ((via_pcr & 0x0E) == 0x08) via_ca2 = 0;
		/* FIX (PET tier-2 port): ORA WRITES clear CA1/CA2 flags just like
		   reads do; reg $F below stays clear-free per datasheet. */
		if ((via_pcr & 0x0A) != 0x02) via_ifr &= ~0x01; // CA2
		via_ifr &= ~0x02; // CA1 always cleared
		int_update();
		/* fall through */
	case 0xF: /* ORA */
		via_ora = data;
		via_hook_on_ora_written(via_orb, via_ora);
		break;

	case 0x2: via_ddrb = data; break;
	case 0x3: via_ddra = data; break;

	case 0x4: /* T1LL */ via_t1ll = data; break;
	case 0x5: /* T1CH (load/arm) */
		via_t1lh = data;
		via_t1ch = via_t1lh;
		via_t1cl = via_t1ll;
		via_ifr &= ~0x40;
		via_t1_oneshot = 1;
		via_t1pb7 = 0x00;   // T1 start -> RAMP active (PB7 low) when ACR7 set
		int_update();
		break;
	case 0x6: via_t1ll = data; break;
	case 0x7:
		via_t1lh = data;
		/* FIX (PET tier-2 port): writing T1L-H also acks the T1 interrupt
		   ("re-program next interval from inside the IRQ handler" idiom). */
		via_ifr &= ~0x40;
		int_update();
		break;

	case 0x8: via_t2ll = data; break;
	case 0x9: /* T2CH (load/arm) */
		via_t2ch = data;
		via_t2cl = via_t2ll;
		via_ifr &= ~0x20;
		via_t2_oneshot = 1;
		int_update();
		break;

	case 0xA: /* SR */
		via_sr = data;
		via_ifr &= ~0x04;
		via_srb = 0; via_srclk = 1;
		int_update();
		break;

	case 0xB: via_acr = data; break;

	case 0xC: /* PCR */
		via_pcr = data;
		via_ca2 = ((via_pcr & 0x0E) == 0x0C) ? 0 : 1;
		via_cb2h = ((via_pcr & 0xE0) == 0xC0) ? 0 : 1;
		break;

	case 0xD: /* IFR: write-1-to-clear */
		via_ifr &= (uint8_t)~(data & 0x7F);
		int_update();
		break;

	case 0xE: /* IER: bit7=1 set, bit7=0 clear */
		if (data & 0x80) via_ier |= (data & 0x7F);
		else             via_ier &= ~(data & 0x7F);
		int_update();
		break;
	}
}

// -----------------------------------------------------------------------------
// External edge inputs for CA1 / CB1
// These let external devices (CPU SYNC, keyboard strobe, etc.) drive VIA pins.
// -----------------------------------------------------------------------------

// --- CA1 edge detection (IFR1) ---
void via_signal_ca1_edge(uint8_t new_level)
{
	if (new_level != ca1_level) {
		/* FIX: CA1 edge polarity is PCR bit 0 (0x02 was a CA2-mode bit). */
		bool active = ((via_pcr & 0x01) ? new_level : !new_level);
		if (active) {
			via_ifr |= 0x02;   // IFR1
			// FIX: in CA2 handshake-output mode (PCR[3:1]=100), an active CA1
			//      edge restores CA2 high. Previously CA2 was driven low on
			//      ORA access but never came back up.
			if ((via_pcr & 0x0E) == 0x08) via_ca2 = 1;
			int_update();
		}
		ca1_level = new_level;
	}
}

// --- CB1 edge detection (IFR4) ---
void via_signal_cb1_edge(uint8_t new_level)
{
	if (new_level != cb1_level) {
		/* FIX: CB1 edge polarity is PCR bit 4 (0x20 was a CB2-mode bit). */
		bool active = ((via_pcr & 0x10) ? new_level : !new_level);
		if (active) {
			via_ifr |= 0x10;   // IFR4
			// FIX: in CB2 handshake-output mode (PCR[7:5]=100), an active CB1
			//      edge restores CB2 high (symmetric with the CA2 fix above).
			if ((via_pcr & 0xE0) == 0x80) via_cb2h = 1;
			int_update();
		}
		cb1_level = new_level;
	}
}

// --- CA2 input handling (IFR0) ---
void via_signal_ca2_input(uint8_t new_level)
{
	// CA2 mode: PCR bits 0-3
	// - 000x: input negative edge
	// - 001x: input positive edge
	// - 01xx: independent interrupt input
	// Others: output modes (ignore here).
	uint8_t mode = via_pcr & 0x0E;
	if (mode == 0x00 || mode == 0x02) { // negative edge
		if (ca2_level == 1 && new_level == 0) {
			via_ifr |= 0x01; // IFR0
			int_update();
		}
	}
	else if (mode == 0x04 || mode == 0x06) { // positive edge
		if (ca2_level == 0 && new_level == 1) {
			via_ifr |= 0x01;
			int_update();
		}
	}
	ca2_level = new_level;
}

// --- CB2 input handling (IFR3) ---
void via_signal_cb2_input(uint8_t new_level)
{
	// CB2 mode: PCR bits 5-7.
	// FIX: mask is 0xE0 (bits 7,6,5), not 0xA0. The old 0xA0 dropped bit 6,
	//      collapsing the positive-edge modes (010/011) into the negative-edge
	//      cases so ALL CB2 input edges decoded as negative.
	//   000 (0x00): input negative edge
	//   001 (0x20): independent interrupt, negative edge
	//   010 (0x40): input positive edge
	//   011 (0x60): independent interrupt, positive edge
	uint8_t mode = via_pcr & 0xE0;
	if (mode == 0x00 || mode == 0x20) { // negative edge
		if (cb2_level == 1 && new_level == 0) {
			via_ifr |= 0x08; // IFR3
			int_update();
		}
	}
	else if (mode == 0x40 || mode == 0x60) { // positive edge
		if (cb2_level == 0 && new_level == 1) {
			via_ifr |= 0x08;
			int_update();
		}
	}
	cb2_level = new_level;
}

// -----------------------------------------------------------------------------
// ORB readback (live handshake + row select for PET, simple latch for Vectrex)
// -----------------------------------------------------------------------------
uint8_t via_hook_read_port_b(uint8_t orb)
{
	// Vectrex: ORB is not tied to IEEE bus, just return latched register
	return orb;
}

/* ---- Optional getters ---------------------------------------------------- */
uint8_t via_get_ifr(void) { return via_ifr; }
uint8_t via_get_ier(void) { return via_ier; }
uint8_t via_get_acr(void) { return via_acr; }
uint8_t via_get_pcr(void) { return via_pcr; }
uint8_t via_get_ora(void) { return via_ora; }
uint8_t via_get_orb(void) { return via_orb; }
uint8_t via_get_ca2(void) { return via_ca2; }
uint8_t via_get_cb2h(void) { return via_cb2h; }
uint8_t via_get_cb2s(void) { return via_cb2s; }

// FIX: single source of truth for the effective CB2 (/BLANK) level.
//   - In any shift-OUT mode (ACR[4:2] = 1xx, i.e. acr & 0x10) the SR drives CB2.
//   - Otherwise CB2 follows the manual/handshake level set by PCR.
// Vectrex draw code should read THIS for the beam-on (BLANK) signal, not
// via_get_cb2_level() (which is the CB2 *input* latch).
uint8_t via_get_cb2_output(void)
{
	if (via_acr & 0x10) return via_cb2s ? 1 : 0;
	return via_cb2h ? 1 : 0;
}

uint8_t via_get_t1pb7(void)
{
	if (via_acr & 0x80) {
		// Timer 1 PB7 mode
		return via_t1pb7;
	}
	else {
		// Otherwise PB7 = latched ORB bit 7
		return via_orb & 0x80;
	}
}
uint8_t via_irq_level(void) { return (via_ifr & 0x80); }

uint8_t via_get_ca1_level(void) { return ca1_level; }
uint8_t via_get_ca2_level(void) { return ca2_level; }
uint8_t via_get_cb1_level(void) { return cb1_level; }
uint8_t via_get_cb2_level(void) { return cb2_level; }
