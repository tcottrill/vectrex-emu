//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2025-2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
#pragma once
#ifndef VECTOR_DRAW_H
#define VECTOR_DRAW_H

#include "colordefs.h"     // rgb_t
#include "MathUtils.h"     // aae::math::vec2 / mat4
#include <vector>

// Per-segment beam (butt-capped, coverage-AA rectangle).
struct BeamLine {
    aae::math::vec2 p0;
    aae::math::vec2 p1;
    float           half;   // half-width, logical units
    rgb_t           color;  // packed RGBA (a = 0xff); coverage supplies edge alpha
};

// Round join disc placed at an interior shared vertex (radius == beam half-width).
struct BeamJoin {
    aae::math::vec2 center;
    float           half;
    rgb_t           color;
};

// Procedural shot/fire point (radial core + halo in the shader).
struct BeamShot {
    aae::math::vec2 pos;
    float           size;
    rgb_t           color;
};

// ssaa = supersample factor of the bound render target. It divides the AA
// feather, so a supersampled target keeps GL-matching beam widths. The GL
// chain passes 1 (fbo1 is a plain 1024 buffer); the VK post chain passes
// [main] vk_ssaa.
void beam_init(int ssaa);
void beam_shutdown();
void beam_set_ssaa(int ssaa);          // sets the supersample factor (affects AA feather)

// Mirrors add_line / add_tex exactly. Join connectivity is inferred internally by
// endpoint matching, so EVERY producer (vector_update, the DVG sim, cchasm) is
// covered by routing through these from add_line()/add_tex().
// halfOverride < 0 (the default) takes the beam half-width from
// config.linewidth, which is what every sim wants. The vector-font renderer
// passes its own font-tuned kFontHalf instead: under Vulkan its glyph strokes
// go through this queue rather than VF's GL-direct path, and inheriting the
// game beam width made menu text noticeably fatter and brighter than GL's.
void beam_add_line(float sx, float sy, float ex, float ey, int intensity, rgb_t col,
                   float halfOverride = -1.0f);
void beam_add_shot(float ex, float ey, int intensity, rgb_t col);

void beam_clear();
void beam_draw_all(const aae::math::mat4& proj);

// ---- Retained-batch stash (Vulkan UI-overlay isolation) --------------------
// The game's beam batches are RETAINED across video frames: the SIMS own the
// clear (they call cache_clear() when they start a new vector frame) and the
// renderer redraws whatever is in the arrays on the frames in between.
//
// Under Vulkan the in-game UI overlays (menu / PAUSED / exit dialog / FPS)
// route their glyph strokes through this SAME queue (VectorFont::End ->
// beam_add_line), so the VK chain brackets its overlay emit+record with these
// two calls: push swaps the game's batches aside and leaves the live arrays
// empty; pop discards whatever the overlay pass produced and swaps the game's
// batches back, bit-exactly. Both are container swaps - no element copying -
// and the emptied containers keep their capacity for the next frame.
//
// One level only (not nestable), which is all the chain needs. GL never calls
// these: its fonts draw with their own GL objects and never enter this queue.
void beam_stash_push();
void beam_stash_pop();

// ---- Shared AA-line path (also used by the vector-font renderer) -----------
// Draw a caller-owned batch of segments / caps with the beam's coverage-AA line
// and round-disc shaders under an explicit projection. Resources are created
// lazily, so these work even when beam_init() was never called (raster games and
// the front-end GUI, where only the fonts need the line shader). 'additive'
// selects blend: false = alpha-over (B/W text/menus), true = additive (color).
// 'aaFeather' is the edge feather in the projection's logical units.
void beam_draw_lines(const aae::math::mat4& proj, const BeamLine* lines, int count,
                     float aaFeather, bool additive);
void beam_draw_caps (const aae::math::mat4& proj, const BeamJoin* caps,  int count,
                     float aaFeather, bool additive);

// Build round end-caps / corner joins for a batch of segments via endpoint
// coincidence: a vertex touched by a single segment is a true termination
// (radius = half * endcapMul); two or more is a corner (half * cornerMul). This
// is the same connectivity logic the beam uses internally, exposed so the fonts
// get identical ties without duplicating it.
void beam_build_caps(const BeamLine* lines, int count, float endcapMul, float cornerMul,
                     std::vector<BeamJoin>& out);

// ---- Backend-agnostic batch access -----------------------------------------
// The current frame's segment / shot batches, built by beam_add_line /
// beam_add_shot. The Vulkan backend (vector_draw_vk) consumes these; the round
// joins are rebuilt from the lines via beam_build_caps(), so no other internal
// state is exposed. Pure CPU accessors - no GL, no behavior change.
const std::vector<BeamLine>& beam_get_lines();
const std::vector<BeamShot>& beam_get_shots();

#endif // VECTOR_DRAW_H
