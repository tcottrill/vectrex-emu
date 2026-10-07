//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
// [vectrex-port] Trimmed from AAE aae_video/opengl_renderer.h: GL chain only
// (no Vulkan dispatch, no raster path, no GUI points, no UI overlays).
#ifndef GLCODE_H
#define GLCODE_H

#include "texrect.h"
#include "render_types.h"

// Sane Global Rectangle Coordinates
extern int game_rect_left;
extern int game_rect_right;
extern int game_rect_bottom;
extern int game_rect_top;

// Current projection, mirrored from set_ortho so the core-profile quad shaders
// can read it as a uniform (replaces the fixed-function GL_PROJECTION matrix).
// Forward-declared to avoid pulling MathUtils into every includer.
namespace aae { namespace math { struct mat4; } }
extern aae::math::mat4 g_proj;

void set_ortho(int width, int height);
void set_render();
// Draw the queued beam geometry into fbo1 (unless paused), then composite and
// present via final_render().
void render();
void final_render(int left, int right, int bottom, int top);
void set_render_fbo4();
void end_render_fbo4();
void glcode_vector_hard_clear_fbo1();
int init_gl(void);
void end_gl();
void emulator_on_window_resize(int newW, int newH);

#endif
