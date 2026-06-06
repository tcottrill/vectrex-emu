#ifndef GLCODE_H
#define GLCODE_H

#include "framework.h"
#include "texrect.h"
#include "sys_gl.h"

// Sane Global Rectangle Coordinates
extern int game_rect_left;
extern int game_rect_right;
extern int game_rect_bottom;
extern int game_rect_top;
// Custom flag(s) for Warlords, what a pain in the butt. I need to find a better way
extern int g_scanline_override;

void set_ortho(GLint width, GLint height);
// Y-down ortho for the raster rendering path (origin top-left, Y increases downward).
void set_ortho_raster(GLint width, GLint height);
void set_render();
void render();
void final_render(int left, int right, int bottom, int top);
// [vectrex-port] removed decl: final_render_raster() (raster path cut)
void set_render_fbo4();
void end_render_fbo4();
// [vectrex-port] render_ui_overlays is now a no-op (UI/font overlays cut) but
// kept on the API since final_render() still calls it.
void render_ui_overlays(int winW, int winH);
void glcode_vector_hard_clear_fbo1();
int init_gl(void);
void end_gl();
void emulator_on_window_resize(int newW, int newH);
void Widescreen_calc();
// [vectrex-port] removed decls: init_raster_overlay(), shutdown_raster_overlay(),
//                 glcode_get_scanrez_tex() (scanline overlay / raster path cut)

#endif