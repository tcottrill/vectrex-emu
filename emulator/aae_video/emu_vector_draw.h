//-----------------------------------------------------------------------------
// Copyright (c) 2011-2012
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
//-----------------------------------------------------------------------------
#pragma once

#ifndef EMU_VECTOR_DRAW_H
#define EMU_VECTOR_DRAW_H

// ===========================================================================
// emu_vector_draw.h - the emulation-side vector seam.
//
// [vectrex-port] Trimmed from AAE: the Vectrex has no textured shots, so
// add_tex / set_texture_id / set_game_has_shots / set_shot_texture_ready are
// gone, and add_dot is added for zero-length vectors.
// ===========================================================================

#include "colordefs.h"      // rgb_t

void add_line(float sx, float sy, float ex, float ey, int intensity, rgb_t col);
// [vectrex-port] A single dot (zero-length vector), drawn as a round beam disc
// whose diameter is dot_size logical units, independent of the line width.
void add_dot(float x, float y, int intensity, rgb_t col, float dot_size);
void cache_clear();

#endif
