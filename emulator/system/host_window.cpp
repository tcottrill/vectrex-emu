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

// Reusable Win32/OpenGL host shell. Owns the window, menu, fullscreen, scaling,
// viewport, GL/RawInput init, and the message + frame loop. Drives a specific
// emulator via HostApp; contains no machine-specific code.
#include <windows.h>
#include <commdlg.h>
#include <commctrl.h> // trackbar (slider) controls for the Video Settings dialog
#include <stdlib.h>   // __argc, __argv
#include <cstdio>     // swprintf_s
#include <cctype>     // tolower (command-line parsing)
#include <ctime>      // timestamp for F12 snapshots
#include <string>

#include "host_window.h"
#include "host_app.h"
#include "host_view.h"
#include "host_resource.h"

#include "framework.h"     // glew, SCREEN_W/H externs, win_get_window decl
#include "sys_gl.h"        // InitOpenGLContext, GLSwapBuffers, DeleteGLContext, SetvSync
#include "sys_texture.h"   // TEX::Snapshot (F12 -> data/snaps PNG)
#include "rawinput.h"      // RawInput_Initialize/ProcessInput/Shutdown, key[], KEY_ESC
#include "input_config.h"  // controller config dialog + controls.ini
#include "wintimer.h"      // TimerInit, TimerGetTimeMS
#include "FrameLimiter.h"  // frame pacing (all speed limiting goes through this)
#include "path_helper.h"   // getpathU
#include "iniFile.h"       // SetIniFile, get/set_config_*
#include "utf8conv.h"      // win32::Utf16ToUtf8 / Utf8ToUtf16
#include "sys_log.h"

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "comctl32.lib")

// ---- Globals expected by the rest of the codebase (moved from winmain.cpp) ----
HWND hWnd = nullptr;
int  SCREEN_W = 768;
int  SCREEN_H = 960;

HWND win_get_window() { return hWnd; }

void osMessage(int ID, const char* fmt, ...)
{
    char text[1024] = "";
    if (!fmt) return;
    va_list ap; va_start(ap, fmt); vsprintf_s(text, fmt, ap); va_end(ap);
    UINT icon = (ID == IDOK) ? MB_ICONASTERISK : MB_ICONERROR;
    MessageBoxA(hWnd, text, "Message", MB_OK | icon);
}

void allegro_message(const char* title, const char* message)
{
    MessageBoxA(NULL, message, title, MB_ICONEXCLAMATION | MB_OK);
}

// ---- Host state ----
static HostApp      g_app{};
static HMENU        g_menu = nullptr;
static bool         g_running = false;
static bool         g_fromCommandLine = false;  // launched by a front-end with args -> Esc exits
static bool         g_snapRequested = false;    // F12 -> grab a snapshot just before the next swap
static HostViewRect g_vp{ 0, 0, 0, 0 };
static bool         g_fullscreen = false;
static RECT         g_savedRect{};
static DWORD        g_savedStyle = 0;
static int          g_scale = 2;            // 1,2,3 = preset; 0 = Fit (free resize)
static std::wstring g_lastRomDir;
static HWND         g_settingsDlg = nullptr;   // the open settings dialog (one at a time)
static int          g_settingsPage = 0;        // 0=Video, 1=Input, 2=Audio

// --- Per-game video settings (data/ini/<game>.ini) -------------------------
static std::string  g_currentGame = "minestorm"; // base name of the running ROM
static bool         g_applyingGameVideo = false;  // guard: don't save while restoring
// Global default video settings (read once at startup; the baseline before per-game).
static int   g_defGlow      = 8;
static float g_defLineWidth = 1.5f, g_defDotSize = 2.0f;
static float g_defSmoothing = 1.0f, g_defCorner = 0.85f;
static int   g_defGlowFilter = 0, g_defTrailLevel = 1;
static bool  g_defGlowOn = true, g_defTrailOn = false, g_defOverlayOn = true;

static std::string HostRomBaseName(const char* utf8_path);
static void HostApplyVideoForGame(const std::string& game);
static void HostSaveVideoForGame();
static void HostRefreshVideoMenu();
static void HostRefreshVideoDialog();

// Recompute the centered, aspect-locked viewport from the current client size.
static void HostUpdateViewport()
{
    RECT rc{};
    GetClientRect(hWnd, &rc);
    SCREEN_W = rc.right - rc.left;
    SCREEN_H = rc.bottom - rc.top;
    g_vp = host_fit_viewport(SCREEN_W, SCREEN_H, g_app.base_w, g_app.base_h);
}

// Detect the refresh rate (Hz) of the monitor the window is on. VREFRESH can
// report 0/1 for "default"; clamp to a sane range and fall back to 60.
static int HostDetectRefreshHz(HWND wnd)
{
    int hz = 0;
    HDC dc = GetDC(wnd);
    if (dc) { hz = GetDeviceCaps(dc, VREFRESH); ReleaseDC(wnd, dc); }
    if (hz < 50 || hz > 244) hz = 60;
    return hz;
}

// Enable per-monitor DPI awareness when available (Win10+); harmless otherwise.
static void HostEnableDpiAwareness()
{
    HMODULE u32 = GetModuleHandleW(L"user32");
    if (!u32) return;
    typedef BOOL(WINAPI* PFN)(DPI_AWARENESS_CONTEXT);
    PFN p = (PFN)GetProcAddress(u32, "SetProcessDpiAwarenessContext");
    if (p) p(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);
}

// Tick the active scale preset in the Video menu (radio style).
static void HostUpdateScaleChecks()
{
    if (!g_menu) return;
    UINT items[4] = { IDM_SCALE_FIT, IDM_SCALE_1X, IDM_SCALE_2X, IDM_SCALE_3X };
    UINT active = (g_scale >= 1 && g_scale <= 3) ? items[g_scale] : IDM_SCALE_FIT;
    CheckMenuRadioItem(g_menu, IDM_SCALE_1X, IDM_SCALE_FIT, active, MF_BYCOMMAND);
}

// Resize the windowed client to base*N, clamped to the monitor work area.
static void HostApplyScale(int n)
{
    g_scale = n;
    HostUpdateScaleChecks();
    if (n < 1 || g_fullscreen) return; // Fit, or no-op while fullscreen

    int cw = g_app.base_w * n;
    int ch = g_app.base_h * n;

    RECT wa{};
    SystemParametersInfo(SPI_GETWORKAREA, 0, &wa, 0);
    int maxw = wa.right - wa.left;
    int maxh = wa.bottom - wa.top;

    RECT wr{ 0, 0, cw, ch };
    AdjustWindowRect(&wr, (DWORD)GetWindowLongPtr(hWnd, GWL_STYLE), TRUE);
    int ww = wr.right - wr.left;
    int wh = wr.bottom - wr.top;
    if (ww > maxw || wh > maxh) return; // too big for this screen; keep current

    SetWindowPos(hWnd, NULL, 0, 0, ww, wh, SWP_NOMOVE | SWP_NOZORDER);
    HostUpdateViewport();
}

// Toggle borderless fullscreen on the window's current monitor.
static void HostToggleFullscreen()
{
    if (!g_fullscreen) {
        GetWindowRect(hWnd, &g_savedRect);
        g_savedStyle = (DWORD)GetWindowLongPtr(hWnd, GWL_STYLE);

        MONITORINFO mi{ sizeof(mi) };
        GetMonitorInfo(MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST), &mi);

        SetMenu(hWnd, NULL);
        SetWindowLongPtr(hWnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(hWnd, HWND_TOP,
                     mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        g_fullscreen = true;
    } else {
        SetWindowLongPtr(hWnd, GWL_STYLE, g_savedStyle);
        SetMenu(hWnd, g_menu);
        SetWindowPos(hWnd, HWND_NOTOPMOST,
                     g_savedRect.left, g_savedRect.top,
                     g_savedRect.right - g_savedRect.left,
                     g_savedRect.bottom - g_savedRect.top,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        g_fullscreen = false;
    }
    HostUpdateViewport();
}

// Reflect overlay availability in the Video > Overlay item: enabled + checked when
// an overlay is loaded for the current game, greyed + unchecked when none exists.
static void HostSetOverlayItemState(HMENU menu)
{
    if (!menu) return;
    // Enabled when art is available; CHECKED reflects the actual overlay state
    // (config.overlay), not merely art availability -- otherwise the box can read
    // "on" while the overlay is actually off.
    bool avail = g_app.has_overlay && (g_app.has_overlay() != 0);
    bool shown = avail && g_app.get_video && (g_app.get_video(HOST_VID_OVERLAY) != 0);
    EnableMenuItem(menu, IDM_VID_OVERLAY, MF_BYCOMMAND | (avail ? MF_ENABLED : MF_GRAYED));
    CheckMenuItem(menu, IDM_VID_OVERLAY, MF_BYCOMMAND | (shown ? MF_CHECKED : MF_UNCHECKED));
}

// Open the ROM file dialog and hand the chosen path to the emulator.
// --- Command-line options (parsed in host_run; the command line wins over ini) ---
struct HostCmdLine {
    std::string rom;          // -rom <path|name>, or a bare non-option token
    std::string overlayFile;  // optional file after -overlay
    int  fullscreen = -1;     // -fullscreen=1 / -window=0   (-1 = unset)
    int  scale      = -1;     // -scale: 0=fit, 1/2/3        (-1 = unset)
    int  overlay    = -1;     // -overlay=1 / -nooverlay=0   (-1 = unset)
    bool help       = false;
};
static HostCmdLine g_cmd;

static std::string HostLowerA(const char* s)
{
    std::string r(s ? s : "");
    for (char& c : r) c = (char)tolower((unsigned char)c);
    return r;
}

// Front-end-friendly command line:  vectrex-emu [options] [romfile]
//   -rom <file>  -fullscreen  -window  -scale <1|2|3|fit>
//   -overlay [file]  -nooverlay  -h
// A bare (non-dashed) token is taken as the ROM. Applied in host_run AFTER the
// ini is read, so anything here overrides the saved setting.
static void HostParseCommandLine(int argc, char** argv)
{
    for (int i = 1; i < argc; i++) {
        if (!argv[i] || !argv[i][0]) continue;
        std::string a = HostLowerA(argv[i]);
        if      (a == "-rom" && i + 1 < argc)   g_cmd.rom = argv[++i];
        else if (a == "-fullscreen")            g_cmd.fullscreen = 1;
        else if (a == "-window")                g_cmd.fullscreen = 0;
        else if (a == "-scale" && i + 1 < argc) {
            std::string v = HostLowerA(argv[++i]);
            g_cmd.scale = (v == "fit") ? 0 : atoi(v.c_str());
            if (g_cmd.scale < 0 || g_cmd.scale > 3) g_cmd.scale = 0;   // out of range -> fit
        }
        else if (a == "-nooverlay")             g_cmd.overlay = 0;
        else if (a == "-overlay") {
            g_cmd.overlay = 1;
            if (i + 1 < argc && argv[i + 1] && argv[i + 1][0] != '-' && argv[i + 1][0] != '/')
                g_cmd.overlayFile = argv[++i];
        }
        else if (a == "-h" || a == "-help" || a == "--help" || a == "-?" || a == "/?")
            g_cmd.help = true;
        else if (argv[i][0] != '-' && argv[i][0] != '/' && g_cmd.rom.empty())
            g_cmd.rom = argv[i];   // bare token = ROM
    }
}

static void HostShowUsage()
{
    MessageBoxW(NULL,
        L"vectrex-emu [options] [romfile]\n\n"
        L"  -rom <file>       Load a cartridge (full path, or a name under data\\roms)\n"
        L"  -fullscreen       Start in fullscreen\n"
        L"  -window           Start in a window\n"
        L"  -scale <n>        Window scale: 1, 2, 3, or fit\n"
        L"  -overlay [file]   Show the overlay (optionally a specific image)\n"
        L"  -nooverlay        Hide the overlay\n"
        L"  -h                Show this help\n\n"
        L"Command-line options override the matching settings saved in the ini.",
        L"vectrex-emu", MB_OK | MB_ICONINFORMATION);
}

// Resolve a -rom value: use it as-is if it exists, else try data\roms\<name>.
static std::string HostResolveRomPath(const std::string& rom)
{
    if (rom.empty()) return rom;
    if (GetFileAttributesA(rom.c_str()) != INVALID_FILE_ATTRIBUTES) return rom;
    std::string alt = "data\\roms\\" + rom;
    if (GetFileAttributesA(alt.c_str()) != INVALID_FILE_ATTRIBUTES) return alt;
    return rom;   // not found; let load_rom log the error
}

// Load a ROM by path and apply its per-game video settings. Shared by the File
// dialog and the -rom command-line option.
static void HostLoadRomPath(const char* utf8_path)
{
    if (g_app.load_rom) g_app.load_rom(utf8_path);
    g_currentGame = HostRomBaseName(utf8_path);
    HostApplyVideoForGame(g_currentGame);
}

static void HostLoadRomDialog()
{
    wchar_t file[MAX_PATH] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = g_app.rom_filter;
    ofn.nFilterIndex = 1;
    ofn.lpstrInitialDir = g_lastRomDir.empty() ? L"data\\roms" : g_lastRomDir.c_str();
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameW(&ofn)) {
        if (ofn.nFileOffset > 0) g_lastRomDir.assign(file, file + ofn.nFileOffset);
        std::string utf8 = win32::Utf16ToUtf8(file);
        HostLoadRomPath(utf8.c_str());   // load + apply this game's video settings
    }
}

// Open the overlay-image dialog and hand the chosen path to the emulator.
static void HostLoadOverlayDialog()
{
    if (!g_app.load_overlay) return;
    wchar_t file[MAX_PATH] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = g_app.overlay_filter ? g_app.overlay_filter
                                           : L"Images\0*.png;*.jpg;*.bmp\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrInitialDir = L"data\\artwork";   // overlays live under data/artwork
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameW(&ofn)) {
        std::string utf8 = win32::Utf16ToUtf8(file);
        g_app.load_overlay(utf8.c_str());
        HostSetOverlayItemState(g_menu);
        HostSaveVideoForGame();   // overlay now on for this game
    }
}

// Show the menu items as a popup (so the menu is reachable in fullscreen).
static void HostShowPopupMenu(HWND wnd)
{
    POINT pt; GetCursorPos(&pt);
    HINSTANCE inst = (HINSTANCE)GetWindowLongPtr(wnd, GWLP_HINSTANCE);
    HMENU bar = LoadMenuW(inst, MAKEINTRESOURCEW(IDR_HOST_MENU));
    if (!bar) return;
    HMENU popup = CreatePopupMenu();
    int count = GetMenuItemCount(bar);
    for (int i = 0; i < count; i++) {
        wchar_t name[64] = {};
        GetMenuStringW(bar, i, name, 64, MF_BYPOSITION);
        HMENU sub = GetSubMenu(bar, i);
        AppendMenuW(popup, MF_POPUP, (UINT_PTR)sub, name);
    }
    HostSetOverlayItemState(popup);   // keep the popup's Overlay item in sync too
    TrackPopupMenu(popup, TPM_RIGHTBUTTON, pt.x, pt.y, 0, wnd, NULL);
    DestroyMenu(popup);   // submenus are owned by 'bar'
    DestroyMenu(bar);
}

// ---- Video Settings dialog (modeless: the game keeps running so slider drags
//      preview live). Glow slider is 0..15; the float sliders use a trackbar of
//      tenths (or hundredths for corner size) mapped onto the value range. ----
static void HostSetIntLabel(HWND dlg, int id, int v)
{
    wchar_t b[16]; wsprintfW(b, L"%d", v);
    SetDlgItemTextW(dlg, id, b);
}
// Float sliders: the trackbar position is value * scale, over [lo, hi].
struct HostFloatSlider { int sliderId, labelId; float lo, hi, scale; const wchar_t* fmt; };
static const HostFloatSlider kLineSlider   = { IDC_LINEWIDTH_SLIDER, IDC_LINEWIDTH_VALUE, 1.0f, 10.0f, 10.0f,  L"%.1f" };
static const HostFloatSlider kSmoothSlider = { IDC_SMOOTH_SLIDER,    IDC_SMOOTH_VALUE,    0.4f,  2.0f, 10.0f,  L"%.1f" };
static const HostFloatSlider kCornerSlider = { IDC_CORNER_SLIDER,    IDC_CORNER_VALUE,    0.3f,  2.5f, 100.0f, L"%.2f" };
static const HostFloatSlider kDotSlider    = { IDC_DOT_SLIDER,       IDC_DOT_VALUE,       1.0f, 10.0f, 10.0f,  L"%.1f" };

static void HostSetFloatLabel(HWND dlg, const HostFloatSlider& fs, float w)
{
    wchar_t b[16]; swprintf_s(b, 16, fs.fmt, w);
    SetDlgItemTextW(dlg, fs.labelId, b);
}
static void HostInitFloatSlider(HWND dlg, const HostFloatSlider& fs, float val)
{
    HWND s = GetDlgItem(dlg, fs.sliderId);
    SendMessageW(s, TBM_SETRANGE, TRUE, MAKELONG((int)(fs.lo * fs.scale + 0.5f), (int)(fs.hi * fs.scale + 0.5f)));
    SendMessageW(s, TBM_SETPAGESIZE, 0, (LPARAM)(fs.scale >= 100.0f ? 5 : 1));
    if (fs.scale >= 100.0f) SendMessageW(s, TBM_SETTICFREQ, 10, 0);   // a tick per 0.1
    SendMessageW(s, TBM_SETPOS, TRUE, (int)(val * fs.scale + 0.5f));
    HostSetFloatLabel(dlg, fs, val);
}
// Read a float slider's value and update its label.
static float HostReadFloatSlider(HWND dlg, const HostFloatSlider& fs)
{
    float v = (int)SendMessageW(GetDlgItem(dlg, fs.sliderId), TBM_GETPOS, 0, 0) / fs.scale;
    HostSetFloatLabel(dlg, fs, v);
    return v;
}
// Fill a drop-down list and select 'sel'.
static void HostInitCombo(HWND dlg, int id, const wchar_t* const* items, int count, int sel)
{
    HWND c = GetDlgItem(dlg, id);
    SendMessageW(c, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < count; ++i)
        SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)items[i]);
    SendMessageW(c, CB_SETCURSEL, (WPARAM)sel, 0);
}
static const wchar_t* const kGlowFilterItems[] = { L"Classic blur", L"Pyramid (dual filter)" };
static const wchar_t* const kTrailItems[]      = { L"Little", L"More", L"Max" };

// Set every Video page control from the emulator's current values.
static void HostFillVideoPage(HWND p)
{
    int g = g_app.get_glow ? g_app.get_glow() : 0;
    SendMessageW(GetDlgItem(p, IDC_GLOW_SLIDER), TBM_SETPOS, TRUE, g);
    HostSetIntLabel(p, IDC_GLOW_VALUE, g);
    HostInitCombo(p, IDC_GLOWFILTER_COMBO, kGlowFilterItems, 2,
                  g_app.get_glow_filter ? g_app.get_glow_filter() : 0);
    HostInitFloatSlider(p, kLineSlider,   g_app.get_line_width ? g_app.get_line_width() : 1.5f);
    HostInitFloatSlider(p, kSmoothSlider, g_app.get_smoothing  ? g_app.get_smoothing()  : 1.0f);
    HostInitFloatSlider(p, kCornerSlider, g_app.get_corner     ? g_app.get_corner()     : 0.85f);
    HostInitFloatSlider(p, kDotSlider,    g_app.get_dot_size   ? g_app.get_dot_size()   : 2.0f);
    HostInitCombo(p, IDC_TRAIL_COMBO, kTrailItems, 3,
                  (g_app.get_trail_level ? g_app.get_trail_level() : 1) - 1);
}

// ---- Per-game video settings: data/ini/<game>.ini -------------------------
// A separate file from emulator.ini (Win32 profile API). On ROM load we apply
// the global defaults, then overlay any per-game saved values; any video change
// during play is saved back to that game's file.

static std::string HostRomBaseName(const char* utf8_path)
{
    std::string p(utf8_path ? utf8_path : "");
    size_t slash = p.find_last_of("/\\");
    std::string f = (slash == std::string::npos) ? p : p.substr(slash + 1);
    size_t dot = f.find_last_of('.');
    return (dot == std::string::npos) ? f : f.substr(0, dot);
}

static std::wstring HostGameIniPath(const std::string& game)
{
    std::wstring rel = L"data\\ini\\" + win32::Utf8ToUtf16(game) + L".ini";
    wchar_t full[MAX_PATH] = {};
    GetFullPathNameW(rel.c_str(), MAX_PATH, full, NULL);
    return std::wstring(full);
}

static int  GameIniGetInt(const std::wstring& path, const wchar_t* key, int def)
{ return (int)GetPrivateProfileIntW(L"video", key, def, path.c_str()); }
static bool GameIniGetBool(const std::wstring& path, const wchar_t* key, bool def)
{ return GetPrivateProfileIntW(L"video", key, def ? 1 : 0, path.c_str()) != 0; }
static float GameIniGetFloat(const std::wstring& path, const wchar_t* key, float def)
{
    wchar_t buf[32] = {}, dbuf[32] = {};
    swprintf(dbuf, 32, L"%.3f", def);
    GetPrivateProfileStringW(L"video", key, dbuf, buf, 32, path.c_str());
    return (float)_wtof(buf);
}
static void GameIniSetInt(const std::wstring& path, const wchar_t* key, int v)
{ wchar_t b[16]; swprintf(b, 16, L"%d", v); WritePrivateProfileStringW(L"video", key, b, path.c_str()); }
static void GameIniSetBool(const std::wstring& path, const wchar_t* key, bool v)
{ WritePrivateProfileStringW(L"video", key, v ? L"1" : L"0", path.c_str()); }
static void GameIniSetFloat(const std::wstring& path, const wchar_t* key, float v)
{ wchar_t b[32]; swprintf(b, 32, L"%.3f", v); WritePrivateProfileStringW(L"video", key, b, path.c_str()); }

// Reflect the emulator's current glow/trail state in the Video menu (overlay's
// enable/check is art-aware and handled by HostSetOverlayItemState).
static void HostRefreshVideoMenu()
{
    if (!g_menu || !g_app.get_video) return;
    CheckMenuItem(g_menu, IDM_VID_GLOW,  MF_BYCOMMAND | (g_app.get_video(HOST_VID_GLOW)  ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(g_menu, IDM_VID_TRAIL, MF_BYCOMMAND | (g_app.get_video(HOST_VID_TRAIL) ? MF_CHECKED : MF_UNCHECKED));
}

// Push current slider values into the Video settings dialog if it's open.
static void HostRefreshVideoDialog()
{
    if (!g_settingsDlg || g_settingsPage != 0) return;
    HostFillVideoPage(g_settingsDlg);
}

// Save the current video settings to data/ini/<current game>.ini.
static void HostSaveVideoForGame()
{
    if (g_applyingGameVideo || g_currentGame.empty()) return;
    std::wstring path = HostGameIniPath(g_currentGame);
    if (g_app.get_glow)       GameIniSetInt(path, L"glow", g_app.get_glow());
    if (g_app.get_line_width) GameIniSetFloat(path, L"linewidth", g_app.get_line_width());
    if (g_app.get_dot_size)   GameIniSetFloat(path, L"dotsize", g_app.get_dot_size());
    if (g_app.get_smoothing)  GameIniSetFloat(path, L"smoothing", g_app.get_smoothing());
    if (g_app.get_corner)     GameIniSetFloat(path, L"corner", g_app.get_corner());
    if (g_app.get_glow_filter) GameIniSetInt(path, L"glow_filter", g_app.get_glow_filter());
    if (g_app.get_trail_level) GameIniSetInt(path, L"trail_level", g_app.get_trail_level());
    if (g_app.get_video) {
        GameIniSetBool(path, L"glow_on",    g_app.get_video(HOST_VID_GLOW)    != 0);
        GameIniSetBool(path, L"trail_on",   g_app.get_video(HOST_VID_TRAIL)   != 0);
        GameIniSetBool(path, L"overlay_on", g_app.get_video(HOST_VID_OVERLAY) != 0);
    }
}

// Apply global defaults, then this game's saved overrides (if any), to the emulator.
static void HostApplyVideoForGame(const std::string& game)
{
    g_applyingGameVideo = true;

    int   glow   = g_defGlow;
    float lineW  = g_defLineWidth, dotS = g_defDotSize;
    float smooth = g_defSmoothing, corner = g_defCorner;
    int   glowFilter = g_defGlowFilter, trailLevel = g_defTrailLevel;
    bool  glowOn = g_defGlowOn, trailOn = g_defTrailOn;
    // Overlay defaults ON so a game's art shows automatically when present
    // (set_video guards on art availability, so this is a no-op without art).
    // A per-game ini's overlay_on (read below) still overrides this.
    bool  overlayOn = true;

    bool found = false;
    if (!game.empty()) {
        std::wstring path = HostGameIniPath(game);
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
            found = true;
            glow      = GameIniGetInt(path,   L"glow",       glow);
            lineW     = GameIniGetFloat(path, L"linewidth",  lineW);
            dotS      = GameIniGetFloat(path, L"dotsize",    dotS);
            smooth    = GameIniGetFloat(path, L"smoothing",  smooth);
            corner    = GameIniGetFloat(path, L"corner",     corner);
            glowFilter = GameIniGetInt(path,  L"glow_filter", glowFilter);
            trailLevel = GameIniGetInt(path,  L"trail_level", trailLevel);
            glowOn    = GameIniGetBool(path,  L"glow_on",    glowOn);
            trailOn   = GameIniGetBool(path,  L"trail_on",   trailOn);
            overlayOn = GameIniGetBool(path,  L"overlay_on", overlayOn);
        }
    }

    if (g_app.set_glow)       g_app.set_glow(glow);
    if (g_app.set_line_width) g_app.set_line_width(lineW);
    if (g_app.set_dot_size)   g_app.set_dot_size(dotS);
    if (g_app.set_smoothing)  g_app.set_smoothing(smooth);
    if (g_app.set_corner)     g_app.set_corner(corner);
    if (g_app.set_glow_filter) g_app.set_glow_filter(glowFilter);
    if (g_app.set_trail_level) g_app.set_trail_level(trailLevel);
    if (g_app.set_video) {
        g_app.set_video(HOST_VID_GLOW,    glowOn    ? 1 : 0);
        g_app.set_video(HOST_VID_TRAIL,   trailOn   ? 1 : 0);
        g_app.set_video(HOST_VID_OVERLAY, overlayOn ? 1 : 0);
    }

    HostRefreshVideoMenu();
    HostSetOverlayItemState(g_menu);
    HostRefreshVideoDialog();

    LOG_INFO("video: game '%s' -> glow=%d(%s, filter %d) line=%.2f smooth=%.2f corner=%.2f dot=%.2f trail=%s(level %d) overlay=%s (%s)",
             game.c_str(), glow, glowOn ? "on" : "off", glowFilter, lineW, smooth, corner, dotS,
             trailOn ? "on" : "off", trailLevel, overlayOn ? "on" : "off",
             found ? "per-game ini" : "defaults");

    g_applyingGameVideo = false;
}

// Capture the current video settings as the global defaults (emulator.ini
// [video]). These become the baseline for every game without its own override.
static void HostSaveCurrentAsDefault()
{
    if (g_app.get_glow)       g_defGlow      = g_app.get_glow();
    if (g_app.get_line_width) g_defLineWidth = g_app.get_line_width();
    if (g_app.get_dot_size)   g_defDotSize   = g_app.get_dot_size();
    if (g_app.get_smoothing)  g_defSmoothing = g_app.get_smoothing();
    if (g_app.get_corner)     g_defCorner    = g_app.get_corner();
    if (g_app.get_glow_filter) g_defGlowFilter = g_app.get_glow_filter();
    if (g_app.get_trail_level) g_defTrailLevel = g_app.get_trail_level();
    if (g_app.get_video) {
        g_defGlowOn    = g_app.get_video(HOST_VID_GLOW)    != 0;
        g_defTrailOn   = g_app.get_video(HOST_VID_TRAIL)   != 0;
        g_defOverlayOn = g_app.get_video(HOST_VID_OVERLAY) != 0;
    }
    set_config_int  ("video", "glow",       g_defGlow);
    set_config_float("video", "linewidth",  g_defLineWidth);
    set_config_float("video", "dotsize",    g_defDotSize);
    set_config_float("video", "smoothing",  g_defSmoothing);
    set_config_float("video", "corner",     g_defCorner);
    set_config_int  ("video", "glow_filter", g_defGlowFilter);
    set_config_int  ("video", "trail_level", g_defTrailLevel);
    set_config_bool ("video", "glow_on",    g_defGlowOn);
    set_config_bool ("video", "trail_on",   g_defTrailOn);
    set_config_bool ("video", "overlay_on", g_defOverlayOn);
    LOG_INFO("video: saved current settings as global defaults (glow=%d trail=%s overlay=%s)",
             g_defGlow, g_defTrailOn ? "on" : "off", g_defOverlayOn ? "on" : "off");
}

// Revert the current game to the global defaults: drop its per-game override
// file and re-apply the defaults (which also refreshes the menu + this dialog).
static void HostResetGameToDefault()
{
    if (!g_currentGame.empty())
        DeleteFileW(HostGameIniPath(g_currentGame).c_str());
    HostApplyVideoForGame(g_currentGame);
}

static void HostSetVolumeLabel(HWND page, int pct)
{
    wchar_t buf[16];
    swprintf(buf, 16, L"%d%%", pct);
    SetDlgItemTextW(page, IDC_VOLUME_VALUE, buf);
}

// --- Video child page: the four sliders + Save/Reset Default buttons ---
static INT_PTR CALLBACK HostVideoPageProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_INITDIALOG: {
        HWND gs = GetDlgItem(dlg, IDC_GLOW_SLIDER);
        SendMessageW(gs, TBM_SETRANGE, TRUE, MAKELONG(0, 15));
        SendMessageW(gs, TBM_SETPAGESIZE, 0, 1);
        HostFillVideoPage(dlg);
        return TRUE;
    }
    case WM_HSCROLL: {
        HWND ctrl = (HWND)lParam;
        if (ctrl == GetDlgItem(dlg, IDC_GLOW_SLIDER)) {
            int g = (int)SendMessageW(ctrl, TBM_GETPOS, 0, 0);
            if (g_app.set_glow) g_app.set_glow(g);
            HostSetIntLabel(dlg, IDC_GLOW_VALUE, g);
        } else if (ctrl == GetDlgItem(dlg, kLineSlider.sliderId)) {
            float w = HostReadFloatSlider(dlg, kLineSlider);
            if (g_app.set_line_width) g_app.set_line_width(w);
        } else if (ctrl == GetDlgItem(dlg, kSmoothSlider.sliderId)) {
            float v = HostReadFloatSlider(dlg, kSmoothSlider);
            if (g_app.set_smoothing) g_app.set_smoothing(v);
        } else if (ctrl == GetDlgItem(dlg, kCornerSlider.sliderId)) {
            float v = HostReadFloatSlider(dlg, kCornerSlider);
            if (g_app.set_corner) g_app.set_corner(v);
        } else if (ctrl == GetDlgItem(dlg, kDotSlider.sliderId)) {
            float w = HostReadFloatSlider(dlg, kDotSlider);
            if (g_app.set_dot_size) g_app.set_dot_size(w);
        }
        HostSaveVideoForGame();   // persist this change to the current game's ini
        return TRUE;
    }
    case WM_COMMAND:
        if (HIWORD(wParam) == CBN_SELCHANGE) {
            int sel = (int)SendMessageW((HWND)lParam, CB_GETCURSEL, 0, 0);
            if (sel < 0) return TRUE;
            if (LOWORD(wParam) == IDC_GLOWFILTER_COMBO && g_app.set_glow_filter) g_app.set_glow_filter(sel);
            if (LOWORD(wParam) == IDC_TRAIL_COMBO && g_app.set_trail_level)      g_app.set_trail_level(sel + 1);
            HostSaveVideoForGame();
            return TRUE;
        }
        if (LOWORD(wParam) == IDC_SAVE_DEFAULT)  { HostSaveCurrentAsDefault(); return TRUE; }
        if (LOWORD(wParam) == IDC_RESET_DEFAULT) { HostResetGameToDefault();   return TRUE; }
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) { DestroyWindow(dlg); return TRUE; }
        break;
    case WM_CLOSE:   DestroyWindow(dlg); return TRUE;
    case WM_DESTROY: if (g_settingsDlg == dlg) g_settingsDlg = nullptr; return TRUE;
    }
    return FALSE;
}

// --- Audio settings dialog: master volume + ambient sound (emulator-wide) ---
static INT_PTR CALLBACK HostAudioPageProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_INITDIALOG: {
        HWND vs = GetDlgItem(dlg, IDC_VOLUME_SLIDER);
        SendMessageW(vs, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
        SendMessageW(vs, TBM_SETPAGESIZE, 0, 5);
        int vol = g_app.get_volume ? g_app.get_volume() : 80;
        SendMessageW(vs, TBM_SETPOS, TRUE, vol);
        HostSetVolumeLabel(dlg, vol);

        // Ambient sound (flyback): on/off checkbox + its own 0..100 volume.
        HWND as = GetDlgItem(dlg, IDC_AMBIENT_SLIDER);
        SendMessageW(as, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
        SendMessageW(as, TBM_SETPAGESIZE, 0, 5);
        int avol = g_app.get_ambient_volume ? g_app.get_ambient_volume() : 20;
        SendMessageW(as, TBM_SETPOS, TRUE, avol);
        { wchar_t b[16]; swprintf(b, 16, L"%d%%", avol); SetDlgItemTextW(dlg, IDC_AMBIENT_VALUE, b); }
        CheckDlgButton(dlg, IDC_AMBIENT_CHECK,
                       (g_app.get_ambient_enabled && g_app.get_ambient_enabled()) ? BST_CHECKED : BST_UNCHECKED);
        BOOL have = (g_app.has_ambient && g_app.has_ambient()) ? TRUE : FALSE;
        EnableWindow(GetDlgItem(dlg, IDC_AMBIENT_CHECK), have);
        EnableWindow(GetDlgItem(dlg, IDC_AMBIENT_SLIDER), have);
        return TRUE;
    }
    case WM_HSCROLL: {
        HWND ctrl = (HWND)lParam;
        if (ctrl == GetDlgItem(dlg, IDC_VOLUME_SLIDER)) {
            int v = (int)SendMessageW(ctrl, TBM_GETPOS, 0, 0);
            if (g_app.set_volume) g_app.set_volume(v);
            set_config_int("audio", "volume", v);   // emulator-wide -> emulator.ini
            HostSetVolumeLabel(dlg, v);
        } else if (ctrl == GetDlgItem(dlg, IDC_AMBIENT_SLIDER)) {
            int v = (int)SendMessageW(ctrl, TBM_GETPOS, 0, 0);
            if (g_app.set_ambient_volume) g_app.set_ambient_volume(v);
            set_config_int("audio", "ambient_volume", v);
            wchar_t b[16]; swprintf(b, 16, L"%d%%", v); SetDlgItemTextW(dlg, IDC_AMBIENT_VALUE, b);
        }
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_AMBIENT_CHECK) {
            int on = (IsDlgButtonChecked(dlg, IDC_AMBIENT_CHECK) == BST_CHECKED) ? 1 : 0;
            if (g_app.set_ambient_enabled) g_app.set_ambient_enabled(on);
            set_config_bool("audio", "ambient_enabled", on != 0);
            return TRUE;
        }
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) { DestroyWindow(dlg); return TRUE; }
        break;
    case WM_CLOSE:   DestroyWindow(dlg); return TRUE;
    case WM_DESTROY: if (g_settingsDlg == dlg) g_settingsDlg = nullptr; return TRUE;
    }
    return FALSE;
}

// Open one settings section as its own focused, modeless dialog (0=Video,
// 1=Input, 2=Audio). Only one settings window is open at a time. Modeless so
// Video changes preview live while a game runs.
static void HostOpenSettings(int page)
{
    if (g_settingsDlg && IsWindow(g_settingsDlg)) {
        if (page == g_settingsPage) { SetForegroundWindow(g_settingsDlg); return; }
        DestroyWindow(g_settingsDlg);   // switching sections: close the current one
    }
    g_settingsDlg = nullptr;
    g_settingsPage = page;

    HINSTANCE inst = GetModuleHandleW(NULL);
    if (page == 0)
        g_settingsDlg = CreateDialogParamW(inst, MAKEINTRESOURCEW(IDD_PAGE_VIDEO), hWnd, HostVideoPageProc, 0);
    else if (page == 2)
        g_settingsDlg = CreateDialogParamW(inst, MAKEINTRESOURCEW(IDD_PAGE_AUDIO), hWnd, HostAudioPageProc, 0);
    else
        g_settingsDlg = (HWND)input_config_open(hWnd);   // owned by sys_input

    if (g_settingsDlg) {
        ShowWindow(g_settingsDlg, SW_SHOW);
        SetForegroundWindow(g_settingsDlg);
    }
}

// F12 snapshot: save the freshly rendered frame to data/snaps/<game>_<stamp>.png.
// TEX::Snapshot reads the back buffer, flips it upright, creates the folder, and
// appends .png. Called from the render loop just before the buffer swap so the
// back buffer still holds the current frame.
static void HostTakeSnapshot()
{
    char stamp[32] = "";
    time_t now = time(nullptr);
    struct tm tmNow{};
    localtime_s(&tmNow, &now);
    strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &tmNow);
    std::string base = g_currentGame.empty() ? std::string("vectrex") : g_currentGame;
    TEX::Snapshot(base + "_" + stamp, "data/snaps");
}

static LRESULT CALLBACK HostWndProc(HWND wnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_INPUT:
        return RawInput_ProcessInput(wnd, wParam, lParam);

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDM_LOADROM:    HostLoadRomDialog(); return 0;
        case IDM_RESET:      if (g_app.reset) g_app.reset(); return 0;
        case IDM_EXIT:       PostMessage(wnd, WM_CLOSE, 0, 0); return 0;
        case IDM_FULLSCREEN: HostToggleFullscreen(); return 0;
        case IDM_SNAPSHOT:   g_snapRequested = true; return 0;
        case IDM_SCALE_1X:   HostApplyScale(1); return 0;
        case IDM_SCALE_2X:   HostApplyScale(2); return 0;
        case IDM_SCALE_3X:   HostApplyScale(3); return 0;
        case IDM_SCALE_FIT:  HostApplyScale(0); return 0;
        case IDM_ABOUT:
            MessageBoxA(wnd, g_app.about_text ? g_app.about_text : "",
                        "About", MB_OK | MB_ICONINFORMATION);
            return 0;
        case IDM_LOADOVERLAY:  HostLoadOverlayDialog(); return 0;
        case IDM_CLEAROVERLAY:
            if (g_app.clear_overlay) g_app.clear_overlay();
            HostSetOverlayItemState(g_menu);
            HostSaveVideoForGame();   // overlay now off for this game
            return 0;
        case IDM_VID_GLOW:
        case IDM_VID_TRAIL:
        case IDM_VID_OVERLAY:
            if (g_app.toggle_video) {
                int which = (LOWORD(wParam) == IDM_VID_GLOW)  ? HOST_VID_GLOW
                          : (LOWORD(wParam) == IDM_VID_TRAIL) ? HOST_VID_TRAIL
                          :                                     HOST_VID_OVERLAY;
                int state = g_app.toggle_video(which);
                if (state >= 0 && g_menu)
                    CheckMenuItem(g_menu, LOWORD(wParam),
                                  MF_BYCOMMAND | (state ? MF_CHECKED : MF_UNCHECKED));
                HostSaveVideoForGame();   // persist effect toggle per game
            }
            return 0;
        case IDM_SETTINGS_VIDEO: HostOpenSettings(0); return 0;
        case IDM_SETTINGS_INPUT: HostOpenSettings(1); return 0;
        case IDM_SETTINGS_AUDIO: HostOpenSettings(2); return 0;
        }
        return 0;

    case WM_SYSCOMMAND:
        // Left Alt and F10 are game buttons (MAME default has P1 button 2 = Alt),
        // not menu activators. Swallow keyboard menu activation so pressing them
        // doesn't grab the menu bar and start eating keystrokes. The menu stays
        // reachable by mouse and the right-click popup; Alt+Enter / F11 fullscreen
        // still work (those fire via the accelerator table before this).
        if ((wParam & 0xFFF0) == SC_KEYMENU) return 0;
        return DefWindowProc(wnd, msg, wParam, lParam);

    case WM_RBUTTONUP:
        HostShowPopupMenu(wnd);
        return 0;

    case WM_SIZE:
        HostUpdateViewport();
        return 0;

    case WM_ERASEBKGND:
        return 1; // GL clears every frame; skip GDI erase to avoid flicker.

    case WM_SETCURSOR:
        // Hide the pointer over the render (client) area; Windows still draws the
        // normal arrow over the menu bar, title bar, and borders. The Vectrex takes
        // no mouse input, so the cursor just stays out of the way of the picture.
        // (In fullscreen the whole window is client area, so it hides entirely.)
        if (LOWORD(lParam) == HTCLIENT) { SetCursor(NULL); return TRUE; }
        return DefWindowProc(wnd, msg, wParam, lParam);

    case WM_CLOSE:
        g_running = false;
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProc(wnd, msg, wParam, lParam);
    }
}

int host_run(HINSTANCE hInstance, int nCmdShow, const HostApp* app)
{
    g_app = *app;

    LogOpen("vectrex-emu-log.txt");
    LOG_INFO("host_run: starting '%ls'", app->title);
    HostEnableDpiAwareness();

    // Front-end command line (applied below; overrides matching ini settings).
    HostParseCommandLine(__argc, __argv);
    if (g_cmd.help) { HostShowUsage(); LogClose(); return 0; }
    // Any argument beyond argv[0] means a front-end launched us; in that mode Esc
    // shuts the emulator down (returns control to the launcher) instead of just
    // minimizing the window the way a standalone GUI session does.
    g_fromCommandLine = (__argc > 1);
    LOG_INFO("cmdline: rom='%s' scale=%d fullscreen=%d overlay=%d fromCmdLine=%d",
             g_cmd.rom.c_str(), g_cmd.scale, g_cmd.fullscreen, g_cmd.overlay,
             g_fromCommandLine ? 1 : 0);

    // Run from the executable's directory (front-ends may launch elsewhere).
    {
        std::wstring exedir = getpathU(0, 0);
        SetCurrentDirectoryW(exedir.c_str());
    }
    // Game data lives under data/ next to the exe (data/roms, data/ini, data/artwork).
    // Create the folders if missing so the ini write and the file dialogs work.
    CreateDirectoryW(L"data", NULL);
    CreateDirectoryW(L"data\\ini", NULL);
    CreateDirectoryW(L"data\\roms", NULL);
    CreateDirectoryW(L"data\\artwork", NULL);
    SetIniFile("./data/ini/emulator.ini");

    WNDCLASSW wc{};
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = HostWndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APPICON));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"EmulatorHost";
    if (!RegisterClassW(&wc)) {
        MessageBoxW(NULL, L"Window registration failed", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    // Common controls (trackbar/slider) for the Video Settings dialog.
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_BAR_CLASSES | ICC_TAB_CLASSES };
    InitCommonControlsEx(&icc);

    g_menu = LoadMenuW(hInstance, MAKEINTRESOURCEW(IDR_HOST_MENU));

    RECT wr{ 0, 0, app->base_w * 2, app->base_h * 2 }; // initial 2x; adjusted below
    const DWORD style = WS_OVERLAPPEDWINDOW;
    AdjustWindowRect(&wr, style, TRUE); // TRUE: window has a menu
    const int ww = wr.right - wr.left;
    const int wh = wr.bottom - wr.top;
    const int px = (GetSystemMetrics(SM_CXSCREEN) - ww) / 2;
    const int py = (GetSystemMetrics(SM_CYSCREEN) - wh) / 2;

    hWnd = CreateWindowW(L"EmulatorHost", app->title, style,
                         px, py, ww, wh, NULL, g_menu, hInstance, NULL);
    if (!hWnd) {
        MessageBoxW(NULL, L"Window creation failed", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    // Set the title-bar (small) and Alt-Tab/taskbar (big) icons explicitly so each
    // is rasterized from the best-matching size in the .ico rather than scaled.
    if (HICON big = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APPICON),
                                      IMAGE_ICON, 0, 0, LR_DEFAULTSIZE))
        SendMessageW(hWnd, WM_SETICON, ICON_BIG, (LPARAM)big);
    if (HICON sm = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APPICON),
                                     IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                     GetSystemMetrics(SM_CYSMICON), 0))
        SendMessageW(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)sm);

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);
    SetForegroundWindow(hWnd);
    SetFocus(hWnd);

    // GL 4.2 forward-compatible core profile, as AAE uses: the renderer is
    // shader/VAO only, so nothing needs the compatibility profile.
    InitOpenGLContext(false, false, true);
    glewInit();
    // vsync OFF: FrameLimiter is the sole pacer. vsync + FrameLimiter both target
    // the refresh and fight each other, so they are mutually exclusive here.
    if (WGLEW_EXT_swap_control) SetvSync(false);
    RawInput_Initialize(hWnd);
    input_config_init();   // load controls.ini (or defaults) + bring up the gamepad backend

    timeBeginPeriod(1);
    TimerInit();
    HostUpdateViewport();

    // Restore persisted view settings.
    g_scale = (g_cmd.scale >= 0) ? g_cmd.scale : get_config_int("video", "scale", 2);
    {
        char* dir = get_config_string("paths", "lastromdir", "");
        if (dir) { g_lastRomDir = win32::Utf8ToUtf16(dir); free(dir); }
    }
    HostApplyScale(g_scale);
    bool startFullscreen = (g_cmd.fullscreen >= 0) ? (g_cmd.fullscreen != 0)
                                                   : get_config_bool("video", "fullscreen", false);
    if (startFullscreen) HostToggleFullscreen();

    // Pace to the monitor's refresh rate so 50 Hz content doesn't judder/double on
    // a 60 Hz panel. The emulator scales cycles/frame to this so game speed stays
    // authentic; target_fps (if set) overrides the detected refresh.
    int refreshHz = HostDetectRefreshHz(hWnd);
    double paceHz = (app->target_fps > 0.0) ? app->target_fps : (double)refreshHz;
    if (g_app.set_frame_rate) g_app.set_frame_rate(paceHz);
    LOG_INFO("host_run: monitor %d Hz -> pacing %.2f fps", refreshHz, paceHz);

    if (app->init) app->init(__argc, __argv);

    // Read the global video defaults -- the baseline before any per-game override.
    // These come from emulator.ini [video] (hand-editable); trail defaults OFF.
    g_defGlow      = get_config_int("video", "glow", 8);
    g_defLineWidth = get_config_float("video", "linewidth", 1.5f);
    g_defDotSize   = get_config_float("video", "dotsize", 2.0f);
    g_defSmoothing = get_config_float("video", "smoothing", 1.0f);
    g_defCorner    = get_config_float("video", "corner", 0.85f);
    g_defGlowFilter = get_config_int("video", "glow_filter", 0);
    g_defTrailLevel = get_config_int("video", "trail_level", 1);
    // Pyramid-glow tuning is global and ini-only (AAE defaults).
    if (g_app.set_glow2)
        g_app.set_glow2(get_config_float("video", "glow2_gain", 10.0f),
                        get_config_float("video", "glow2_spread", 1.0f),
                        get_config_float("video", "glow2_tail", 0.6f),
                        get_config_float("video", "glow2_core", 1.0f));
    g_defGlowOn    = get_config_bool("video", "glow_on", true);
    g_defTrailOn   = get_config_bool("video", "trail_on", false);   // vector trail OFF by default
    g_defOverlayOn = get_config_bool("video", "overlay_on", true);

    // A -rom on the command line overrides the default Minestorm boot. Either way
    // this applies the defaults + that game's saved per-game video settings.
    if (!g_cmd.rom.empty()) HostLoadRomPath(HostResolveRomPath(g_cmd.rom).c_str());
    else                    HostApplyVideoForGame(g_currentGame);

    // -overlay / -nooverlay override whatever the loaded game set up.
    if (g_cmd.overlay == 0) {
        if (g_app.clear_overlay) g_app.clear_overlay();
        if (g_app.set_video)     g_app.set_video(HOST_VID_OVERLAY, 0);
        HostSetOverlayItemState(g_menu);
    } else if (g_cmd.overlay == 1) {
        if (!g_cmd.overlayFile.empty() && g_app.load_overlay) g_app.load_overlay(g_cmd.overlayFile.c_str());
        if (g_app.set_video) g_app.set_video(HOST_VID_OVERLAY, 1);
        HostSetOverlayItemState(g_menu);
    }

    // Restore the saved master volume (emulator-wide, not per-game; default 80%).
    if (g_app.set_volume) g_app.set_volume(get_config_int("audio", "volume", 80));

    // Restore ambient sound (emulator-wide; default off at 20%). Set the volume
    // before enabling so it starts at the saved level.
    if (g_app.set_ambient_volume)  g_app.set_ambient_volume(get_config_int("audio", "ambient_volume", 20));
    if (g_app.set_ambient_enabled) g_app.set_ambient_enabled(get_config_bool("audio", "ambient_enabled", false));

    HACCEL accel = LoadAcceleratorsW(hInstance, MAKEINTRESOURCEW(IDR_HOST_ACCEL));
    MSG msg{};
    g_running = true;

    FrameLimiter::Init(paceHz);   // all frame pacing goes through FrameLimiter

    while (g_running) {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                g_running = false;
            } else {
                if (g_settingsDlg && !IsWindow(g_settingsDlg)) g_settingsDlg = nullptr;
                if (g_settingsDlg && IsDialogMessage(g_settingsDlg, &msg)) {
                    // handled by the open settings dialog
                } else if (!TranslateAccelerator(hWnd, accel, &msg)) {
                    TranslateMessage(&msg);
                    DispatchMessage(&msg);
                }
            }
        } else {
            glViewport(g_vp.x, g_vp.y, g_vp.w, g_vp.h);
            if (!app->run_frame()) {
                // Esc: a standalone fullscreen window drops back to windowed first;
                // every other case (any window, or a front-end-launched instance) quits.
                if (g_fullscreen && !g_fromCommandLine) {
                    HostToggleFullscreen(); // leave fullscreen, keep running
                    key[KEY_ESC] = 0;       // consume so it doesn't re-trigger
                } else {
                    g_running = false;
                }
            } else {
                if (g_snapRequested) { HostTakeSnapshot(); g_snapRequested = false; }
                GLSwapBuffers();
            }

            // Pace to the target rate. vsync (if on) prevents tearing; this caps
            // the emulation rate precisely and works even when vsync is unavailable.
            FrameLimiter::Throttle();
        }
    }

    // Persist view settings. Video effect/slider settings are NOT written here --
    // they live per-game in data/ini/<game>.ini (saved on change). emulator.ini
    // [video] holds only the global defaults baseline (hand-editable).
    set_config_int("video", "scale", g_scale);
    set_config_bool("video", "fullscreen", g_fullscreen);
    if (!g_lastRomDir.empty())
        set_config_string("paths", "lastromdir", win32::Utf16ToUtf8(g_lastRomDir).c_str());

    if (app->shutdown) app->shutdown();
    FrameLimiter::Shutdown();
    DeleteGLContext();
    RawInput_Shutdown();
    LOG_INFO("host_run: exiting");
    LogClose();
    DestroyWindow(hWnd);
    return 0;
}
