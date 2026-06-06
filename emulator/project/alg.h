#ifndef AAE_VECTREX_ALG_H
#define AAE_VECTREX_ALG_H

#include <stdint.h>
#include "vecx.h"
#include "via6522.h" // for via_get_* used by alg_sstep

/* -------------------------------------------------------------------------
   Public knobs read/written by the “front-end” layer (e.g. input).
   These remain plain 8-bit values, unchanged from original VecX.
   ------------------------------------------------------------------------- */
extern unsigned alg_jch0; /* joystick direction channel 0 */
extern unsigned alg_jch1; /* joystick direction channel 1 */
extern unsigned alg_jch2; /* joystick direction channel 2 */
extern unsigned alg_jch3; /* joystick direction channel 3 */

/* -------------------------------------------------------------------------
   Public vector lists consumed by the renderer.
   NOTE: Although internal math now runs at 16× precision (<<4 shift),
   the committed output vectors (vectors_draw / vectors_erse) are shifted
   back down to normal integer coordinates. These remain in the expected
   0..ALG_MAX_X / 0..ALG_MAX_Y range for rendering code.
   ------------------------------------------------------------------------- */
extern long      vector_draw_cnt;
extern long      vector_erse_cnt;
extern vector_t* vectors_draw;
extern vector_t* vectors_erse;

/* -------------------------------------------------------------------------
   Lifetime / stepping
   ------------------------------------------------------------------------- */
void alg_reset(void);
void alg_sstep(void);

/* -------------------------------------------------------------------------
   VIA-facing analog hooks (called by via_hook_* in vecx.cpp).
   These update the internal <<4 fixed-point deltas and comparator.
   ------------------------------------------------------------------------- */
void alg_on_orb(unsigned orb, unsigned xsh /* current X S/H */);
void alg_on_ora(unsigned ora);

/* VIA “compare” input bit (0x20 or 0) */
uint8_t alg_get_compare_bit(void);

/* Frame cadence in CPU cycles (VECTREX_MHZ / VECTREX_PDECAY) */
long alg_frame_cycles_init(void);

#endif /* AAE_VECTREX_ALG_H */
