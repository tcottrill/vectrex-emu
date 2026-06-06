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

// Configurable controller input + the Controller Configuration dialog.
//
// Data model: g_bind[player][device][action] holds, for each of 2 players x
// {keyboard, joystick} x 8 actions, an integer code:
//   keyboard -> a Win32 virtual-key code (0 = unbound)
//   joystick -> 1..32  = gamepad button (index code-1)
//               200/201/202/203 = D-pad/stick up/down/left/right
// Persisted to data/ini/controls.ini via the Win32 profile API (a file fully
// independent of emulator.ini). The dialog embeds the controller PNG as an
// RCDATA resource and decodes it in-memory with stb_image, painting it with
// AlphaBlend so its transparency composites over the dialog on any theme.

#include <windows.h>
#include <commctrl.h>   // tab control
#include <cstdio>       // swprintf

#include "input_config.h"
#include "rawinput.h"   // unsigned char key[256]  (VK-indexed keystate)
#include "Joystick.h"   // joy[], install_joystick(), poll_joystick(), MAX_*
#include "../system/host_resource.h"
#include "../3rdparty/stb_image.h"   // stbi_load_from_memory (impl lives in sys_texture.cpp)

#pragma comment(lib, "msimg32.lib")   // AlphaBlend
#pragma comment(lib, "comctl32.lib")  // tab control

// --- Action / device indices ------------------------------------------------
enum { A_UP = 0, A_DOWN, A_LEFT, A_RIGHT, A_B1, A_B2, A_B3, A_B4, A_COUNT };
enum { DEV_KB = 0, DEV_JS = 1, DEV_COUNT };

// Joystick direction codes (anything 1..32 is a button).
enum { JS_UP = 200, JS_DOWN = 201, JS_LEFT = 202, JS_RIGHT = 203 };

static int g_bind[2][DEV_COUNT][A_COUNT];   // [player][device][action]

// --- controls.ini path + section/key tables --------------------------------
static wchar_t g_iniPath[MAX_PATH] = L"";
static const wchar_t* SECT[2][DEV_COUNT] = {
    { L"p1.keyboard", L"p1.joystick" },
    { L"p2.keyboard", L"p2.joystick" },
};
static const wchar_t* KEYN[A_COUNT] = {
    L"up", L"down", L"left", L"right", L"b1", L"b2", L"b3", L"b4",
};

static void set_defaults(void)
{
    // Keyboard defaults follow the MAME/MESS standard layout.
    // Player 1: arrows + Left Ctrl / Left Alt / Space / Left Shift.
    g_bind[0][DEV_KB][A_UP] = VK_UP;       g_bind[0][DEV_KB][A_DOWN]  = VK_DOWN;
    g_bind[0][DEV_KB][A_LEFT] = VK_LEFT;   g_bind[0][DEV_KB][A_RIGHT] = VK_RIGHT;
    g_bind[0][DEV_KB][A_B1] = VK_LCONTROL; g_bind[0][DEV_KB][A_B2] = VK_LMENU;
    g_bind[0][DEV_KB][A_B3] = VK_SPACE;    g_bind[0][DEV_KB][A_B4] = VK_LSHIFT;
    // Player 2: R/F/D/G + A / S / Q / W.
    g_bind[1][DEV_KB][A_UP] = 'R'; g_bind[1][DEV_KB][A_DOWN]  = 'F';
    g_bind[1][DEV_KB][A_LEFT] = 'D'; g_bind[1][DEV_KB][A_RIGHT] = 'G';
    g_bind[1][DEV_KB][A_B1] = 'A'; g_bind[1][DEV_KB][A_B2] = 'S';
    g_bind[1][DEV_KB][A_B3] = 'Q'; g_bind[1][DEV_KB][A_B4] = 'W';
    // Both players' gamepads: D-pad + buttons A/B/X/Y (P1 reads joy[0], P2 joy[1]).
    for (int p = 0; p < 2; p++) {
        g_bind[p][DEV_JS][A_UP] = JS_UP;     g_bind[p][DEV_JS][A_DOWN]  = JS_DOWN;
        g_bind[p][DEV_JS][A_LEFT] = JS_LEFT; g_bind[p][DEV_JS][A_RIGHT] = JS_RIGHT;
        g_bind[p][DEV_JS][A_B1] = 1; g_bind[p][DEV_JS][A_B2] = 2;
        g_bind[p][DEV_JS][A_B3] = 3; g_bind[p][DEV_JS][A_B4] = 4;
    }
}

static void load_ini(void)
{
    for (int p = 0; p < 2; p++)
        for (int d = 0; d < DEV_COUNT; d++)
            for (int a = 0; a < A_COUNT; a++)
                g_bind[p][d][a] = (int)GetPrivateProfileIntW(
                    SECT[p][d], KEYN[a], g_bind[p][d][a], g_iniPath);
}

static void save_ini(void)
{
    for (int p = 0; p < 2; p++)
        for (int d = 0; d < DEV_COUNT; d++)
            for (int a = 0; a < A_COUNT; a++) {
                wchar_t val[16];
                swprintf(val, 16, L"%d", g_bind[p][d][a]);
                WritePrivateProfileStringW(SECT[p][d], KEYN[a], val, g_iniPath);
            }
}

// ============================================================================
//  Runtime polling (used by the emulator each frame)
// ============================================================================

static int kb_down(int vk)
{
    return (vk > 0 && vk < 256 && key[vk]) ? 1 : 0;
}

static int js_active(int player, int code)
{
    if (code <= 0 || player < 0 || player >= MAX_JOYSTICKS) return 0;
    if (code >= 1 && code <= MAX_JOYSTICK_BUTTONS)
        return joy[player].button[code - 1].b ? 1 : 0;
    switch (code) {
    case JS_UP:    return joy[player].stick[0].axis[1].d1 ? 1 : 0;
    case JS_DOWN:  return joy[player].stick[0].axis[1].d2 ? 1 : 0;
    case JS_LEFT:  return joy[player].stick[0].axis[0].d1 ? 1 : 0;
    case JS_RIGHT: return joy[player].stick[0].axis[0].d2 ? 1 : 0;
    }
    return 0;
}

void input_poll_begin(void) { poll_joystick(); }

InputPlayerState input_player_state(int p)
{
    InputPlayerState s = { 0, 0, 0, 0, 0, 0, 0, 0 };
    if (p < 0 || p > 1) return s;
    const int* kb = g_bind[p][DEV_KB];
    const int* js = g_bind[p][DEV_JS];
    s.up    = (kb_down(kb[A_UP])    || js_active(p, js[A_UP]))    ? 1 : 0;
    s.down  = (kb_down(kb[A_DOWN])  || js_active(p, js[A_DOWN]))  ? 1 : 0;
    s.left  = (kb_down(kb[A_LEFT])  || js_active(p, js[A_LEFT]))  ? 1 : 0;
    s.right = (kb_down(kb[A_RIGHT]) || js_active(p, js[A_RIGHT])) ? 1 : 0;
    s.b1    = (kb_down(kb[A_B1])    || js_active(p, js[A_B1]))    ? 1 : 0;
    s.b2    = (kb_down(kb[A_B2])    || js_active(p, js[A_B2]))    ? 1 : 0;
    s.b3    = (kb_down(kb[A_B3])    || js_active(p, js[A_B3]))    ? 1 : 0;
    s.b4    = (kb_down(kb[A_B4])    || js_active(p, js[A_B4]))    ? 1 : 0;
    return s;
}

void input_config_init(void)
{
    set_defaults();
    GetFullPathNameW(L"data\\ini\\controls.ini", MAX_PATH, g_iniPath, NULL);
    if (GetFileAttributesW(g_iniPath) == INVALID_FILE_ATTRIBUTES)
        save_ini();     // first run: materialize the file with defaults
    load_ini();

    INITCOMMONCONTROLSEX icc = { sizeof(icc),
        ICC_TAB_CLASSES | ICC_STANDARD_CLASSES | ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);

    install_joystick();   // succeeds even with no pad attached (hotplug later)
}

// ============================================================================
//  Controller Configuration dialog
// ============================================================================

#define WM_APP_KEYCAP (WM_APP + 1)   // posted by the LL keyboard hook: wParam = vk (0 = Esc/cancel)

static HBITMAP g_ctrlBmp = nullptr;
static int     g_ctrlBmpW = 0, g_ctrlBmpH = 0;
static int     g_curPlayer = 0;           // which tab is showing
static int     g_capDev = -1, g_capAct = -1;  // active capture target (-1 = idle)
static HWND    g_capDlg = nullptr;
static HHOOK   g_kbHook = nullptr;
static int     g_capJsBtn[MAX_JOYSTICK_BUTTONS];
static int     g_capJsDir[4];

static int bind_id(int dev, int act) { return IDC_BIND_FIRST + dev * A_COUNT + act; }
static bool id_to_devact(int id, int* dev, int* act)
{
    int o = id - IDC_BIND_FIRST;
    if (o < 0 || o >= DEV_COUNT * A_COUNT) return false;
    *dev = o / A_COUNT; *act = o % A_COUNT;
    return true;
}

// --- PNG resource -> premultiplied 32-bit top-down DIB ----------------------
static HBITMAP LoadPngResourceToDIB(int resId, int* outW, int* outH)
{
    HMODULE hMod = GetModuleHandleW(NULL);
    HRSRC hRes = FindResourceW(hMod, MAKEINTRESOURCEW(resId), RT_RCDATA);
    if (!hRes) return nullptr;
    DWORD sz = SizeofResource(hMod, hRes);
    HGLOBAL hData = LoadResource(hMod, hRes);
    const void* p = hData ? LockResource(hData) : nullptr;
    if (!p || !sz) return nullptr;

    int w = 0, h = 0, ch = 0;
    stbi_uc* px = stbi_load_from_memory((const stbi_uc*)p, (int)sz, &w, &h, &ch, 4);
    if (!px) return nullptr;

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;          // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC hdc = GetDC(NULL);
    HBITMAP bmp = CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, hdc);

    if (bmp && bits) {
        unsigned char* d = (unsigned char*)bits;
        for (int i = 0; i < w * h; i++) {
            unsigned char r = px[i * 4 + 0], g = px[i * 4 + 1];
            unsigned char b = px[i * 4 + 2], a = px[i * 4 + 3];
            d[i * 4 + 0] = (unsigned char)(b * a / 255);  // BGRA, premultiplied
            d[i * 4 + 1] = (unsigned char)(g * a / 255);
            d[i * 4 + 2] = (unsigned char)(r * a / 255);
            d[i * 4 + 3] = a;
        }
    }
    stbi_image_free(px);
    if (outW) *outW = w;
    if (outH) *outH = h;
    return bmp;
}

// --- Binding -> display text -----------------------------------------------
static void key_name(int vk, wchar_t* out, int n)
{
    if (vk <= 0) { wcsncpy_s(out, n, L"(none)", _TRUNCATE); return; }
    UINT sc = MapVirtualKeyW((UINT)vk, MAPVK_VK_TO_VSC);
    bool ext = false;
    switch (vk) {
    case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
    case VK_PRIOR: case VK_NEXT: case VK_HOME: case VK_END:
    case VK_INSERT: case VK_DELETE: case VK_DIVIDE: case VK_NUMLOCK:
        ext = true; break;
    }
    LONG lp = (LONG)(sc << 16);
    if (ext) lp |= (1 << 24);
    wchar_t buf[64] = {};
    if (GetKeyNameTextW(lp, buf, 63) > 0) wcsncpy_s(out, n, buf, _TRUNCATE);
    else swprintf(out, n, L"Key 0x%02X", vk);
}

static void joy_name(int code, wchar_t* out, int n)
{
    switch (code) {
    case JS_UP:    wcsncpy_s(out, n, L"D-Pad Up", _TRUNCATE);    return;
    case JS_DOWN:  wcsncpy_s(out, n, L"D-Pad Down", _TRUNCATE);  return;
    case JS_LEFT:  wcsncpy_s(out, n, L"D-Pad Left", _TRUNCATE);  return;
    case JS_RIGHT: wcsncpy_s(out, n, L"D-Pad Right", _TRUNCATE); return;
    }
    if (code >= 1 && code <= MAX_JOYSTICK_BUTTONS) { swprintf(out, n, L"Button %d", code); return; }
    wcsncpy_s(out, n, L"(none)", _TRUNCATE);
}

static void refresh_buttons(HWND dlg)
{
    int p = g_curPlayer;
    wchar_t buf[64];
    for (int a = 0; a < A_COUNT; a++) {
        key_name(g_bind[p][DEV_KB][a], buf, 64);
        SetDlgItemTextW(dlg, bind_id(DEV_KB, a), buf);
        joy_name(g_bind[p][DEV_JS][a], buf, 64);
        SetDlgItemTextW(dlg, bind_id(DEV_JS, a), buf);
    }
}

// --- Capture (click a button, then press the key / gamepad input) -----------
static LRESULT CALLBACK LlKbProc(int code, WPARAM w, LPARAM l)
{
    if (code == HC_ACTION) {
        if (w == WM_KEYDOWN || w == WM_SYSKEYDOWN) {
            KBDLLHOOKSTRUCT* k = (KBDLLHOOKSTRUCT*)l;
            int vk = (int)k->vkCode;
            if (g_capDlg)
                PostMessageW(g_capDlg, WM_APP_KEYCAP, (WPARAM)(vk == VK_ESCAPE ? 0 : vk), 0);
            return 1;   // swallow so the dialog never navigates / closes on the key
        }
        if (w == WM_KEYUP || w == WM_SYSKEYUP) return 1;
    }
    return CallNextHookEx(g_kbHook, code, w, l);
}

static void end_capture(HWND dlg)
{
    if (g_kbHook) { UnhookWindowsHookEx(g_kbHook); g_kbHook = nullptr; }
    KillTimer(dlg, 1);
    g_capDev = -1; g_capAct = -1;
    refresh_buttons(dlg);
}

static void begin_capture(HWND dlg, int dev, int act)
{
    if (g_capDev >= 0) end_capture(dlg);   // restart cleanly if already capturing
    g_capDev = dev; g_capAct = act; g_capDlg = dlg;
    SetDlgItemTextW(dlg, bind_id(dev, act),
        dev == DEV_KB ? L"Press a key...  (Esc=cancel)"
                      : L"Press a button...  (Esc=cancel)");
    // A low-level hook swallows the keyboard during capture: for KB it records
    // the pressed key; for JS it keeps Esc (cancel) from closing the dialog.
    g_kbHook = SetWindowsHookExW(WH_KEYBOARD_LL, LlKbProc, GetModuleHandleW(NULL), 0);
    if (dev == DEV_JS) {
        poll_joystick();
        for (int i = 0; i < MAX_JOYSTICK_BUTTONS; i++) g_capJsBtn[i] = joy[g_curPlayer].button[i].b;
        g_capJsDir[0] = joy[g_curPlayer].stick[0].axis[1].d1;  // up
        g_capJsDir[1] = joy[g_curPlayer].stick[0].axis[1].d2;  // down
        g_capJsDir[2] = joy[g_curPlayer].stick[0].axis[0].d1;  // left
        g_capJsDir[3] = joy[g_curPlayer].stick[0].axis[0].d2;  // right
        SetTimer(dlg, 1, 30, NULL);
    }
}

static INT_PTR CALLBACK CtrlDlgProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_INITDIALOG: {
        HWND tab = GetDlgItem(dlg, IDC_CTRL_TAB);
        TCITEMW ti = {}; ti.mask = TCIF_TEXT;
        ti.pszText = (LPWSTR)L"Player 1"; TabCtrl_InsertItem(tab, 0, &ti);
        ti.pszText = (LPWSTR)L"Player 2"; TabCtrl_InsertItem(tab, 1, &ti);
        g_curPlayer = 0;
        g_capDev = -1; g_capAct = -1; g_kbHook = nullptr;
        g_ctrlBmp = LoadPngResourceToDIB(IDB_CONTROLLER_PNG, &g_ctrlBmpW, &g_ctrlBmpH);
        refresh_buttons(dlg);
        return TRUE;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (dis->CtlID == IDC_CTRL_IMAGE) {
            RECT rc = dis->rcItem;
            int dw = rc.right - rc.left, dh = rc.bottom - rc.top;
            FillRect(dis->hDC, &rc, GetSysColorBrush(COLOR_BTNFACE));
            if (g_ctrlBmp && g_ctrlBmpW > 0 && g_ctrlBmpH > 0) {
                double ar = (double)g_ctrlBmpW / g_ctrlBmpH;
                int tw = dw, th = (int)(dw / ar);
                if (th > dh) { th = dh; tw = (int)(dh * ar); }
                int ox = rc.left + (dw - tw) / 2, oy = rc.top + (dh - th) / 2;
                HDC mem = CreateCompatibleDC(dis->hDC);
                HGDIOBJ old = SelectObject(mem, g_ctrlBmp);
                BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
                AlphaBlend(dis->hDC, ox, oy, tw, th, mem, 0, 0, g_ctrlBmpW, g_ctrlBmpH, bf);
                SelectObject(mem, old);
                DeleteDC(mem);
            }
            return TRUE;
        }
        break;
    }

    case WM_NOTIFY: {
        LPNMHDR nm = (LPNMHDR)lParam;
        if (nm->idFrom == IDC_CTRL_TAB && nm->code == TCN_SELCHANGE) {
            if (g_capDev >= 0) end_capture(dlg);
            g_curPlayer = TabCtrl_GetCurSel(GetDlgItem(dlg, IDC_CTRL_TAB));
            if (g_curPlayer < 0) g_curPlayer = 0;
            refresh_buttons(dlg);
        }
        return TRUE;
    }

    case WM_APP_KEYCAP: {
        int vk = (int)wParam;
        if (g_capDev == DEV_KB) {
            if (vk > 0) { g_bind[g_curPlayer][DEV_KB][g_capAct] = vk; end_capture(dlg); save_ini(); }
            else        end_capture(dlg);   // vk==0 (Esc) just cancels
        } else if (g_capDev == DEV_JS) {
            if (vk == 0) end_capture(dlg);   // Esc cancels; other keys ignored for a pad slot
        }
        return TRUE;
    }

    case WM_TIMER: {
        if (g_capDev != DEV_JS) return TRUE;
        int p = g_curPlayer;
        poll_joystick();
        for (int i = 0; i < MAX_JOYSTICK_BUTTONS; i++) {
            if (joy[p].button[i].b && !g_capJsBtn[i]) {
                g_bind[p][DEV_JS][g_capAct] = i + 1;
                end_capture(dlg); save_ini();
                return TRUE;
            }
            g_capJsBtn[i] = joy[p].button[i].b;
        }
        int dirs[4] = {
            joy[p].stick[0].axis[1].d1, joy[p].stick[0].axis[1].d2,
            joy[p].stick[0].axis[0].d1, joy[p].stick[0].axis[0].d2,
        };
        int dcode[4] = { JS_UP, JS_DOWN, JS_LEFT, JS_RIGHT };
        for (int i = 0; i < 4; i++) {
            if (dirs[i] && !g_capJsDir[i]) {
                g_bind[p][DEV_JS][g_capAct] = dcode[i];
                end_capture(dlg); save_ini();
                return TRUE;
            }
            g_capJsDir[i] = dirs[i];
        }
        return TRUE;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == IDOK) {                 // Close button
            DestroyWindow(dlg);
            return TRUE;
        }
        if (id == IDC_CTRL_DEFAULTS) {
            if (g_capDev >= 0) end_capture(dlg);
            set_defaults();
            save_ini();
            refresh_buttons(dlg);
            return TRUE;
        }
        int dev, act;
        if (HIWORD(wParam) == BN_CLICKED && id_to_devact(id, &dev, &act)) {
            begin_capture(dlg, dev, act);
            return TRUE;
        }
        break;
    }

    case WM_CLOSE:
        DestroyWindow(dlg);
        return TRUE;

    case WM_DESTROY:
        if (g_capDev >= 0) end_capture(dlg);
        if (g_ctrlBmp) { DeleteObject(g_ctrlBmp); g_ctrlBmp = nullptr; }
        return TRUE;
    }
    return FALSE;
}

void* input_config_open(void* parentHwnd)
{
    // Modeless popup dialog; the host tracks the returned handle.
    return (void*)CreateDialogParamW(GetModuleHandleW(NULL),
        MAKEINTRESOURCEW(IDD_PAGE_INPUT),
        (HWND)parentHwnd, CtrlDlgProc, 0);
}
