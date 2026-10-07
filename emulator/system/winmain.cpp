// Vectrex-Emu
// Copyright (C) 2026 Tim Cottrill and Claude Code
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

// Thin entry point: build the Vectrex HostApp and run the reusable host shell.
// To base another emulator on this shell, swap the functions and the kVectrex
// struct below; nothing else here is machine-specific.
#include <windows.h>

#include "host_window.h"
#include "host_app.h"
#include "emulator.h"

static bool vx_init(int argc, char** argv) { emulator_init(argc, argv); return true; }
static void vx_load(const char* path)       { emulator_load_cart(path); }
static void vx_reset(void)                   { emulator_reset(); }
static bool vx_run(void)                      { return emulator_run() != 0; }
static void vx_shutdown(void)                 { emulator_end(); }
static void vx_load_overlay(const char* p)    { emulator_load_overlay(p); }
static void vx_clear_overlay(void)            { emulator_clear_overlay(); }
static int  vx_has_overlay(void)              { return emulator_has_overlay(); }
static int  vx_toggle_video(int which)        { return emulator_toggle_video(which); }
static int  vx_get_video(int which)           { return emulator_get_video(which); }
static void vx_set_video(int which, int on)   { emulator_set_video(which, on); }
static int   vx_get_glow(void)                { return emulator_get_glow(); }
static void  vx_set_glow(int a)               { emulator_set_glow(a); }
static float vx_get_line_width(void)          { return emulator_get_line_width(); }
static void  vx_set_line_width(float w)       { emulator_set_line_width(w); }
static float vx_get_dot_size(void)            { return emulator_get_dot_size(); }
static void  vx_set_dot_size(float w)         { emulator_set_dot_size(w); }
static float vx_get_smoothing(void)           { return emulator_get_smoothing(); }
static void  vx_set_smoothing(float v)        { emulator_set_smoothing(v); }
static float vx_get_corner(void)              { return emulator_get_corner(); }
static void  vx_set_corner(float v)           { emulator_set_corner(v); }
static int   vx_get_glow_filter(void)         { return emulator_get_glow_filter(); }
static void  vx_set_glow_filter(int f)        { emulator_set_glow_filter(f); }
static int   vx_get_trail_level(void)         { return emulator_get_trail_level(); }
static void  vx_set_trail_level(int l)        { emulator_set_trail_level(l); }
static void  vx_get_glow2(float* g, float* s, float* t, float* c) { emulator_get_glow2(g, s, t, c); }
static void  vx_set_glow2(float g, float s, float t, float c)     { emulator_set_glow2(g, s, t, c); }
static void  vx_set_frame_rate(double hz)     { emulator_set_frame_rate(hz); }
static int   vx_get_volume(void)              { return emulator_get_volume(); }
static void  vx_set_volume(int pct)           { emulator_set_volume(pct); }
static int   vx_has_ambient(void)             { return emulator_has_ambient(); }
static int   vx_get_ambient_enabled(void)     { return emulator_get_ambient_enabled(); }
static void  vx_set_ambient_enabled(int on)   { emulator_set_ambient_enabled(on); }
static int   vx_get_ambient_volume(void)      { return emulator_get_ambient_volume(); }
static void  vx_set_ambient_volume(int pct)   { emulator_set_ambient_volume(pct); }

static const HostApp kVectrex = {
    L"vectrex-emu",                                       // title
    384, 480,                                             // base size (portrait ~4:5)
    L"Vectrex ROMs\0*.vec;*.bin;*.gam\0All Files\0*.*\0", // ROM filter
    0.0,                                                  // self-paced (vecx throttles)
    vx_init, vx_load, vx_reset, vx_run, vx_shutdown,
    "Vectrex-Emu 1.0\n"
    "Copyright (C) 2026 Tim Cottrill and Claude Code\n"
    "\n"
    "Licensed under the GNU GPL v3.0 - comes with ABSOLUTELY NO WARRANTY.",
    L"Overlay Images\0*.png;*.jpg;*.bmp\0All Files\0*.*\0", // overlay filter
    vx_load_overlay, vx_clear_overlay, vx_has_overlay, vx_toggle_video,
    vx_get_video, vx_set_video,
    vx_get_glow, vx_set_glow, vx_get_line_width, vx_set_line_width,
    vx_get_dot_size, vx_set_dot_size,
    vx_get_smoothing, vx_set_smoothing, vx_get_corner, vx_set_corner,
    vx_get_glow_filter, vx_set_glow_filter, vx_get_trail_level, vx_set_trail_level,
    vx_get_glow2, vx_set_glow2,
    vx_set_frame_rate,
    vx_get_volume, vx_set_volume,
    vx_has_ambient, vx_get_ambient_enabled, vx_set_ambient_enabled,
    vx_get_ambient_volume, vx_set_ambient_volume
};

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrev,
                   _In_ LPSTR lpCmdLine, _In_ int nShowCmd)
{
    (void)hPrev; (void)lpCmdLine;
    return host_run(hInstance, nShowCmd, &kVectrex);
}
