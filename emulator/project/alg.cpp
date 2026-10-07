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

#include "alg.h"
#include <string.h>

/* --- Internal analog state (moved from vecx.cpp) ------------------------ */
static unsigned alg_rsh;  /* zero ref sample and hold */
static unsigned alg_xsh;  /* x sample and hold */
static unsigned alg_ysh;  /* y sample and hold */
static unsigned alg_zsh;  /* z sample and hold */
static unsigned alg_jsh;  /* joystick sample and hold */

static unsigned alg_compare;      /* 0 or 0x20 */
static long     alg_dx;           /* delta x (fixed-point <<4) */
static long     alg_dy;           /* delta y (fixed-point <<4) */
static long     alg_curr_x;       /* current x position (fixed-point <<4) */
static long     alg_curr_y;       /* current y position (fixed-point <<4) */

enum {
    VECTREX_PDECAY = 30,                       /* phosphor decay rate */
    FCYCLES_INIT = VECTREX_MHZ / VECTREX_PDECAY,
    VECTOR_CNT = VECTREX_MHZ / VECTREX_PDECAY,
    VECTOR_HASH = 65521
};

long alg_frame_cycles_init(void) { return FCYCLES_INIT; }

/* Vector builder state (moved) */
static unsigned      alg_vectoring; /* are we drawing a vector right now? */
static long          alg_vector_x0, alg_vector_y0;
static long          alg_vector_x1, alg_vector_y1;
static long          alg_vector_dx, alg_vector_dy;
static unsigned char alg_vector_color;

long      vector_draw_cnt;
long      vector_erse_cnt;
static    vector_t vectors_set[2 * VECTOR_CNT];
vector_t* vectors_draw = vectors_set;
vector_t* vectors_erse = vectors_set + VECTOR_CNT;

static long vector_hash[VECTOR_HASH];

/* Front-end exposed joystick channels */
unsigned alg_jch0, alg_jch1, alg_jch2, alg_jch3;

/* --- Small helpers ------------------------------------------------------- */
static inline void alg_addline(long x0, long y0, long x1, long y1, unsigned char color)
{
    unsigned long key = (unsigned long)x0;
    key = key * 31 + (unsigned long)y0;
    key = key * 31 + (unsigned long)x1;
    key = key * 31 + (unsigned long)y1;
    key %= VECTOR_HASH;

    long index = vector_hash[key];

    if (index >= 0 && index < vector_draw_cnt &&
        x0 == vectors_draw[index].x0 &&
        y0 == vectors_draw[index].y0 &&
        x1 == vectors_draw[index].x1 &&
        y1 == vectors_draw[index].y1) {
        vectors_draw[index].color = color;
        return;
    }

    if (index >= 0 && index < vector_erse_cnt &&
        x0 == vectors_erse[index].x0 &&
        y0 == vectors_erse[index].y0 &&
        x1 == vectors_erse[index].x1 &&
        y1 == vectors_erse[index].y1) {
        vectors_erse[index].color = VECTREX_COLORS;
    }

    /* A frame can run up to 150,000 cycles (vecx.cpp FRAME_LOCK_TIMEOUT), so
     * the list can in principle fill; drop rather than overrun it. */
    if (vector_draw_cnt >= VECTOR_CNT)
        return;

    vectors_draw[vector_draw_cnt].x0 = x0;
    vectors_draw[vector_draw_cnt].y0 = y0;
    vectors_draw[vector_draw_cnt].x1 = x1;
    vectors_draw[vector_draw_cnt].y1 = y1;
    vectors_draw[vector_draw_cnt].color = color;
    vector_hash[key] = vector_draw_cnt;
    vector_draw_cnt++;
}

/* --- Public API ---------------------------------------------------------- */
void alg_reset(void)
{
    /* JCH inputs */
    alg_jch0 = 128;
    alg_jch1 = 128;
    alg_jch2 = 128;
    alg_jch3 = 128;

    /* S/H + analog */
    alg_rsh = 128;
    alg_xsh = 128;
    alg_ysh = 128;
    alg_zsh = 0;
    alg_jsh = 128;

    alg_compare = 0;
    alg_dx = 0;
    alg_dy = 0;

    // FIXED-POINT: store current position with <<4
    alg_curr_x = (ALG_MAX_X / 2) << 4;
    alg_curr_y = (ALG_MAX_Y / 2) << 4;

    alg_vectoring = 0;

    vector_draw_cnt = 0;
    vector_erse_cnt = 0;
    vectors_draw = vectors_set;
    vectors_erse = vectors_set + VECTOR_CNT;

    /* clear hash */
    for (long i = 0; i < VECTOR_HASH; ++i) vector_hash[i] = 0;
}

/* Apply the ORB-controlled analog mux side-effects (no sound logic here). */
void alg_on_orb(unsigned orb, unsigned xsh /* current X S/H */)
{
    switch (orb & 0x06) {
    case 0x00:
        alg_jsh = alg_jch0;
        if ((orb & 0x01) == 0) alg_ysh = alg_xsh;
        break;
    case 0x02:
        alg_jsh = alg_jch1;
        if ((orb & 0x01) == 0) alg_rsh = alg_xsh;
        break;
    case 0x04:
        alg_jsh = alg_jch2;
        if ((orb & 0x01) == 0) alg_zsh = (alg_xsh > 0x80) ? (alg_xsh - 0x80) : 0;
        break;
    case 0x06:
        alg_jsh = alg_jch3;
        break;
    }

    alg_compare = (alg_jsh > alg_xsh) ? 0x20 : 0;

    // FIXED-POINT: shift deltas up by 4
    alg_dx = ((long)alg_xsh - (long)alg_rsh) << 4;
    alg_dy = ((long)alg_rsh - (long)alg_ysh) << 4;
}

/* Apply ORA->DAC path and recompute compare/deltas */
void alg_on_ora(unsigned ora)
{
    alg_xsh = (uint8_t)(ora ^ 0x80);
    alg_compare = (alg_jsh > alg_xsh) ? 0x20 : 0;

    // FIXED-POINT: shift deltas up by 4
    alg_dx = ((long)alg_xsh - (long)alg_rsh) << 4;
    alg_dy = ((long)alg_rsh - (long)alg_ysh) << 4;
}

uint8_t alg_get_compare_bit(void)
{
    return (uint8_t)alg_compare;
}

/* One "analog" step (unchanged behavior), driven by VIA pins */
void alg_sstep(void)
{
    long sig_dx, sig_dy;
    unsigned sig_ramp;
    unsigned sig_blank;

    sig_blank = (via_get_acr() & 0x10) ? via_get_cb2s() : via_get_cb2h();

    if (via_get_ca2() == 0) {
        // FIXED-POINT: keep in <<4 space
        sig_dx = ((ALG_MAX_X / 2) << 4) - alg_curr_x;
        sig_dy = ((ALG_MAX_Y / 2) << 4) - alg_curr_y;
    }
    else {
        sig_ramp = (via_get_acr() & 0x80) ? via_get_t1pb7() : (via_get_orb() & 0x80);
        if (sig_ramp == 0) { sig_dx = alg_dx; sig_dy = alg_dy; }
        else { sig_dx = 0;      sig_dy = 0; }
    }

    if (alg_vectoring == 0) {
        if (sig_blank == 1 &&
            alg_curr_x >= 0 && alg_curr_x < (ALG_MAX_X << 4) &&
            alg_curr_y >= 0 && alg_curr_y < (ALG_MAX_Y << 4)) {

            alg_vectoring = 1;
            alg_vector_x0 = alg_curr_x;
            alg_vector_y0 = alg_curr_y;
            alg_vector_x1 = alg_curr_x;
            alg_vector_y1 = alg_curr_y;
            alg_vector_dx = sig_dx;
            alg_vector_dy = sig_dy;
            alg_vector_color = (unsigned char)alg_zsh;
        }
    }
    else {
        if (sig_blank == 0) {
            alg_vectoring = 0;
            // FIXED-POINT: shift back down when committing
            alg_addline(alg_vector_x0 >> 4, alg_vector_y0 >> 4,
                alg_vector_x1 >> 4, alg_vector_y1 >> 4,
                alg_vector_color);
        }
        else if (sig_dx != alg_vector_dx ||
            sig_dy != alg_vector_dy ||
            (unsigned char)alg_zsh != alg_vector_color) {

            alg_addline(alg_vector_x0 >> 4, alg_vector_y0 >> 4,
                alg_vector_x1 >> 4, alg_vector_y1 >> 4,
                alg_vector_color);

            if (alg_curr_x >= 0 && alg_curr_x < (ALG_MAX_X << 4) &&
                alg_curr_y >= 0 && alg_curr_y < (ALG_MAX_Y << 4)) {
                alg_vector_x0 = alg_curr_x;
                alg_vector_y0 = alg_curr_y;
                alg_vector_x1 = alg_curr_x;
                alg_vector_y1 = alg_curr_y;
                alg_vector_dx = sig_dx;
                alg_vector_dy = sig_dy;
                alg_vector_color = (unsigned char)alg_zsh;
            }
            else {
                alg_vectoring = 0;
            }
        }
    }

    alg_curr_x += sig_dx;
    alg_curr_y += sig_dy;

    if (alg_vectoring == 1 &&
        alg_curr_x >= 0 && alg_curr_x < (ALG_MAX_X << 4) &&
        alg_curr_y >= 0 && alg_curr_y < (ALG_MAX_Y << 4)) {

        alg_vector_x1 = alg_curr_x;
        alg_vector_y1 = alg_curr_y;
    }
}
