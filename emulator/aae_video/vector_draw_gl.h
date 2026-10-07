//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
#pragma once
// ===========================================================================
// vector_draw_gl.h - render-side vector helpers.
//
// Split out of emu_vector_draw.h: these are renderer concerns and must not
// be visible to drivers or vector generators.
// ===========================================================================
#include "colordefs.h"    // rgb_t
#include "MathUtils.h"    // aae::math::mat4

class txdata
{
public:
    float x, y;
    float tx, ty;
    rgb_t color;

    txdata() : x(0), y(0), tx(0), ty(0), color(0) {}
    txdata(float x, float y, float tx, float ty, rgb_t color) : x(x), y(y), tx(tx), ty(ty), color(color) {}
};

rgb_t modulate_color(rgb_t col, int intensity, int gain);
void  draw_textured_shots(const aae::math::mat4& proj);

// VK-chain read-only view of the frame's textured-shot list (GL draws it
// itself via draw_textured_shots above). 6 vertices per shot; cleared each
// frame by cache_clear(). Header is GL-free (colordefs + MathUtils only),
// so the VK TU can include it without header-leak concerns.
const txdata* tex_shot_verts(int* count);
