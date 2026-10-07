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

// Original Citations for VecX-GL and vecx Below:

// VecXGL 1.2 (SDL/Win32 and SDL/Linux)
//
// This is a port of the vectrex emulator "vecx", by Valavan Manohararajah.
// Portions of this code copyright James Higgs 2005/2007.
// These portions are:
// 1. Ay38910 PSG (audio) emulation wave-buffering code.
// 2. Drawing of vectors using OpenGL.
//
// Comand-line parsing code gratefully borrowed from vecxsdl (Thomas Mathys).
// Key mapping and command-line options were also changed
// to be compatible with Thomas Mathys' vecxsdl.
//
// Other vecx ports by JH:
// - VecXPS2 (Playsyation 2)
// - VecXWin32 (Windows/DirectX) (unreleased)

#include <stdio.h>
#include "cpu_m6809.h"
#include "cpu_control.h"   // g_cpu_mem / g_cpu (shared with emulator.cpp)
#include "vecx.h"
#include "emulator.h"
#include "via6522.h"
#include "alg.h"
#include "ay8910.h"
#include "sys_log.h"
#include "mixer.h"

// The whole 64K Vectrex address space (extern in cpu_control.h). emulator.cpp
// loads the BIOS into 0xE000-0xFFFF and the cart into 0x0000-0x7FFF; the RAM
// handler below aliases the 1K RAM at 0xC800.
uint8_t    g_cpu_mem[0x10000];
cpu_m6809* g_cpu = nullptr;

#define VECX_RAM_BASE 0xC800   // 1K RAM, mirrored across 0xC800-0xCFFF and 0xD800-0xDFFF

/* the sound chip registers */

unsigned snd_regs[16];
static unsigned snd_select = 0;

/* Vectrex frame rate, used to size the AY sample-buffer mapping (must match the
 * AY core's AY8910_HOST_FPS and the mixer/stream frame rate). */
#define AY8910_HOST_FPS_VECX 50

static long fcycles;
static int audio_cycles = 0; // cycles within the current 1/50-second audio block

/* ===== Vectrex board glue: VIA callbacks (invoked from via6522.cpp) ======= */
/* These are the old snd_update()/alg_update() bodies, rewritten to use the
   values passed in by the VIA instead of touching via_* directly. */

   /* AY-3-8910 bus cycle: ORB bits 4..3 are BDIR/BC1, ORA carries the address or
	* data byte. Called on every ORB or ORA change (mirror of the original vecx
	* snd_update). 0x18 latches a register address; 0x10 writes the latched
	* register; 0x08 (PSG drives PA) is serviced on read in via_hook_read_port_a;
	* 0x00 is idle. Register 14 (button I/O port) is never written from the bus. */
static void psg_bus_io(uint8_t orb, uint8_t ora)
{
	switch (orb & 0x18) {
	case 0x10: // write data: ORA -> currently latched register
		if ((snd_select & 0x0F) != 14) {
			snd_regs[snd_select & 0x0F] = ora;   // mirror, for the button-read path
			ay8910_write(0, 1, ora);             // AY chip 0: data write (addr bit 0 = 1)
		}
		break;
	case 0x18: // latch register address (only when hi nibble of ORA is 0)
		if ((ora & 0xF0) == 0x00) {
			snd_select = ora & 0x0F;
			ay8910_write(0, 0, ora);             // AY chip 0: address latch (addr bit 0 = 0)
		}
		break;
	default:   // 0x00 idle, 0x08 PSG drives PA (handled on read)
		break;
	}
}

void via_hook_on_orb_written(uint8_t orb, uint8_t ora)
{
	psg_bus_io(orb, ora);       // AY-3-8910 bus (mirror of original snd_update)
	alg_on_orb(orb, 0);         // analog side-effects (xsh param unused internally)
}

void via_hook_on_ora_written(uint8_t orb, uint8_t ora)
{
	alg_on_ora(ora);            // ORA -> DAC -> X sample/hold (original)
	psg_bus_io(orb, ora);       // same PSG bus decode as on ORB writes
}

uint8_t via_hook_read_port_a(uint8_t orb, uint8_t ora)
{
	if ((orb & 0x18) == 0x08) {
		// PSG drives PA: return current latched register�s contents
		return snd_select == 14 ? (uint8_t)snd_regs[14] : ay8910_read(0);
	}
	return ora;
}

uint8_t via_hook_get_compare_bit(void)
{
	return alg_get_compare_bit();
}

/* ===== Vectrex memory map: explicit MEM_ADDR handlers over g_cpu_mem ========
 *   Cart  0x0000-0x7FFF : cartridge image     (read; writes ignored)
 *   ----  0x8000-0xC7FF : unmapped            (reads 0xFF; writes ignored)
 *   RAM   0xC800-0xCFFF : 1K RAM, mirrored     (& 0x3FF into VECX_RAM_BASE)
 *   VIA   0xD000-0xD7FF : 6522 registers       (& 0x0F)
 *   R+IO  0xD800-0xDFFF : RAM and VIA overlap  (read = RAM; write = both)
 *   BIOS  0xE000-0xFFFF : system ROM           (read; writes ignored)
 * Each handler receives (addr - lowAddr); g_cpu_mem is indexed by absolute
 * address. emulator.cpp loads the cart and BIOS regions of g_cpu_mem. */

READ_HANDLER(vecx_cart_r)    { return g_cpu_mem[address]; }                            /* 0x0000-0x7FFF */
READ_HANDLER(vecx_open_r)    { (void)address; return 0xFF; }                           /* 0x8000-0xC7FF */
READ_HANDLER(vecx_ram_r)     { return g_cpu_mem[VECX_RAM_BASE + (address & 0x3FF)]; }  /* 0xC800-0xCFFF */
READ_HANDLER(vecx_via_r)     { return via_read_reg((uint8_t)(address & 0x0F)); }       /* 0xD000-0xD7FF */
READ_HANDLER(vecx_ramvia_r)  { return g_cpu_mem[VECX_RAM_BASE + (address & 0x3FF)]; }  /* 0xD800-0xDFFF read=RAM */
READ_HANDLER(vecx_rom_r)     { return g_cpu_mem[0xE000 + address]; }                   /* 0xE000-0xFFFF */

WRITE_HANDLER(vecx_protect_w){ (void)address; (void)data; }                            /* cart/unmapped/BIOS: ignore */
WRITE_HANDLER(vecx_ram_w)    { g_cpu_mem[VECX_RAM_BASE + (address & 0x3FF)] = data; }
WRITE_HANDLER(vecx_via_w)    { via_write_reg((uint8_t)(address & 0x0F), data); }
WRITE_HANDLER(vecx_ramvia_w)                                                           /* 0xD800-0xDFFF: RAM + VIA */
{
	g_cpu_mem[VECX_RAM_BASE + (address & 0x3FF)] = data;
	via_write_reg((uint8_t)(address & 0x0F), data);
}

MEM_READ(m6809_readmem)
MEM_ADDR(0x0000, 0x7FFF, vecx_cart_r)
MEM_ADDR(0x8000, 0xC7FF, vecx_open_r)
MEM_ADDR(0xC800, 0xCFFF, vecx_ram_r)
MEM_ADDR(0xD000, 0xD7FF, vecx_via_r)
MEM_ADDR(0xD800, 0xDFFF, vecx_ramvia_r)
MEM_ADDR(0xE000, 0xFFFF, vecx_rom_r)
MEM_END

MEM_WRITE(m6809_writemem)
MEM_ADDR(0x0000, 0x7FFF, vecx_protect_w)
MEM_ADDR(0x8000, 0xC7FF, vecx_protect_w)
MEM_ADDR(0xC800, 0xCFFF, vecx_ram_w)
MEM_ADDR(0xD000, 0xD7FF, vecx_via_w)
MEM_ADDR(0xD800, 0xDFFF, vecx_ramvia_w)
MEM_ADDR(0xE000, 0xFFFF, vecx_protect_w)
MEM_END

int cpu_scale_by_cycles(int val, int clock)
{
	/* Map the CPU's cycle position within the current frame onto [0, val].
	 * `clock` is the chip/CPU clock (1.5 MHz on Vectrex); at 50 fps one frame is
	 * clock/50 cycles. audio_cycles persists across video presents and resets
	 * only after a complete audio block (or hardware reset). */
	int max = clock / AY8910_HOST_FPS_VECX;
	if (max <= 0) return 0;

	int current = audio_cycles;

	int k = (int)(val * ((float)current / (float)max));
	return k;
}

/* Diagnostics accessors (used by the heartbeat log in emulator.cpp). */
unsigned vecx_cpu_pc(void) { return g_cpu ? g_cpu->GetPC() : 0u; }
unsigned vecx_cpu_s(void) { return g_cpu ? g_cpu->GetS() : 0u; }

void vecx_reset(void)
{
	unsigned r;

	/* ram */

	for (r = 0; r < 1024; r++) {
		g_cpu_mem[VECX_RAM_BASE + r] = r & 0xff;
	}

	for (r = 0; r < 16; r++) {
		snd_regs[r] = 0;
	}

	/* input buttons */

	snd_regs[14] = 0xff;

	snd_select = 0;
	audio_cycles = 0;

	via_reset();

	alg_reset();

	ay8910_reset(-1);   /* reset all AY chips (no-op until ay8910_sh_start) */

	fcycles = alg_frame_cycles_init();

	if (!g_cpu)
		g_cpu = new cpu_m6809(g_cpu_mem, m6809_readmem, m6809_writemem, 0);

	g_cpu->reset();

	/* --- Boot diagnostics ------------------------------------------------- */
	LOG_INFO("vecx_reset: BIOS[E000]= %02X %02X %02X %02X  reset-vector $FFFE/F = $%02X%02X",
		g_cpu_mem[0xE000], g_cpu_mem[0xE001], g_cpu_mem[0xE002], g_cpu_mem[0xE003],
		g_cpu_mem[0xFFFE], g_cpu_mem[0xFFFF]);
	LOG_INFO("vecx_reset: cart[0000]= %02X %02X %02X %02X %02X %02X %02X %02X",
		g_cpu_mem[0], g_cpu_mem[1], g_cpu_mem[2], g_cpu_mem[3],
		g_cpu_mem[4], g_cpu_mem[5], g_cpu_mem[6], g_cpu_mem[7]);
	LOG_INFO("vecx_reset: post-reset PC=$%04X S=$%04X DP=$%02X CC=$%02X (reset should land at $F000)",
		g_cpu->GetPC(), g_cpu->GetS(), g_cpu->GetDP(), g_cpu->GetCC());
}

/* perform a single cycle worth of via emulation.
 * via_sstep0 is the first postion of the emulation.
 */

int vecx_emu(long cycles)
{
	unsigned c, icycles;
	int frames_drawn = 0;

	/* Keep diagnostic CPU ticks local to this video update. Audio has its own
	 * cycle position, retained across video updates of any refresh rate. */
	g_cpu->get6809ticks(1);

	while (cycles > 0) {
		/* Drive the level-sensitive IRQ line from the VIA, then run one
		 * instruction. step() returns the cycles it consumed (>= 1). */
		g_cpu->irq_line(via_irq_level() != 0);
		icycles = (unsigned)g_cpu->step();
		for (c = 0; c < icycles; c++) {
			via_sstep0();
			alg_sstep();
			via_sstep1();
			if (++audio_cycles == VECTREX_MHZ / AY8910_HOST_FPS_VECX) {
				ay8910_sh_update();
				mixer_update_sync();
				audio_cycles = 0;
			}
		}

		cycles -= (long)icycles;

		fcycles -= (long)icycles;

		if (fcycles < 0) {
			vector_t* tmp;

			fcycles += alg_frame_cycles_init();
			osint_render();
			frames_drawn++;

			/* Everything drawn this pass becomes next pass's erase list. */
			vector_erse_cnt = vector_draw_cnt;
			vector_draw_cnt = 0;

			tmp = vectors_erse;
			vectors_erse = vectors_draw;
			vectors_draw = tmp;
		}
	}

	return frames_drawn;
}
