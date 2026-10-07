// -----------------------------------------------------------------------------
// [vectrex-port] Trimmed from AAE vidhrdwr/emu_vector_draw.cpp.
//
// The modern beam renderer (vector_draw.cpp) owns all line/dot geometry; this
// file is just the seam the emulator calls plus modulate_color(). Removed from
// the AAE original: textured shots (texlist, add_tex, draw_textured_shots) and
// the per-driver beam scale (vector_get_beam, always 1.0 here).
// -----------------------------------------------------------------------------
#define NOMINMAX
#include "emu_vector_draw.h"
#include "vector_draw_gl.h" // modulate_color
#include "vector_draw.h"    // beam_add_line / beam_clear
#include "colordefs.h"
#include "config.h"         // config.linewidth
#include <algorithm>        // std::min / std::max (clip)
#include <cstdint>

template<typename T>
inline T clip(T val, T minval, T maxval) {
    return std::min(std::max(val, minval), maxval);
}

rgb_t modulate_color(rgb_t col, int intensity, int gain)
{
    if ((col & 0x00FFFFFF) == 0) { return 0; }

    uint8_t r = (col >> 0) & 0xFF;
    uint8_t g = (col >> 8) & 0xFF;
    uint8_t b = (col >> 16) & 0xFF;
    uint8_t a = 0xff;// (col >> 24) & 0xFF;

    r = clip((r & intensity) + gain, 0, 255);
    g = clip((g & intensity) + gain, 0, 255);
    b = clip((b & intensity) + gain, 0, 255);

    return  (a << 24) | (b << 16) | (g << 8) | r;
}

void add_line(float sx, float sy, float ex, float ey, int intensity, rgb_t col)
{
    beam_add_line(sx, sy, ex, ey, intensity, col, config.linewidth * 0.5f);
}

void add_dot(float x, float y, int intensity, rgb_t col, float dot_size)
{
    // Same degenerate segment AAE's add_tex emits for plain beam dots: the beam
    // renderer gives a single-segment vertex a round end-cap of radius 'half'.
    beam_add_line(x, y, x + 0.00001f, y + 0.00001f, intensity, col, dot_size * 0.5f);
}

void cache_clear()
{
    beam_clear();
}
