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

#pragma once

#define IDI_APPICON      100   // app icon (vectrex-emu.ico); lowest id -> Explorer uses it for the .exe
#define IDR_HOST_MENU    101
#define IDR_HOST_ACCEL   102

#define IDM_LOADROM      40001
#define IDM_RESET        40002
#define IDM_EXIT         40003
#define IDM_FULLSCREEN   40004
#define IDM_SCALE_1X     40005
#define IDM_SCALE_2X     40006
#define IDM_SCALE_3X     40007
#define IDM_SCALE_FIT    40008
#define IDM_ABOUT        40009
#define IDM_LOADOVERLAY  40010
#define IDM_CLEAROVERLAY 40011
#define IDM_VID_GLOW     40012
#define IDM_VID_TRAIL    40013
#define IDM_VID_OVERLAY  40014
// Settings menu: each opens the consolidated Settings dialog on a given page.
#define IDM_SETTINGS_VIDEO 40017
#define IDM_SETTINGS_INPUT 40018
#define IDM_SETTINGS_AUDIO 40019
#define IDM_SNAPSHOT       40020   // F12: save the current frame as a PNG in data/snaps

// ---- Settings: one focused dialog per section (opened from the Settings menu) ----
// Video page + controls
#define IDD_PAGE_VIDEO       240
#define IDC_GLOW_SLIDER      201
#define IDC_GLOW_VALUE       202
#define IDC_LINEWIDTH_SLIDER 203
#define IDC_LINEWIDTH_VALUE  204
#define IDC_POINT_SLIDER     205
#define IDC_POINT_VALUE      206
#define IDC_DOT_SLIDER       207
#define IDC_DOT_VALUE        208
#define IDC_SAVE_DEFAULT     209
#define IDC_RESET_DEFAULT    210

// Audio page + controls
#define IDD_PAGE_AUDIO       243
#define IDC_VOLUME_SLIDER    212
#define IDC_VOLUME_VALUE     213
#define IDC_AMBIENT_CHECK    214
#define IDC_AMBIENT_SLIDER   215
#define IDC_AMBIENT_VALUE    216

// Input page (controller config) + controls
#define IDD_PAGE_INPUT        242
#define IDB_CONTROLLER_PNG    221   // embedded controller graphic (RCDATA, a PNG)
#define IDC_CTRL_IMAGE        222   // owner-draw static that paints the PNG
#define IDC_CTRL_TAB          223   // Player 1 / Player 2 tab control
#define IDC_CTRL_DEFAULTS     224   // "Restore Defaults" button
// 16 contiguous binding-button ids: keyboard col = BIND_FIRST+0..7,
// joystick col = BIND_FIRST+8..15; action order: up,down,left,right,b1..b4.
#define IDC_BIND_FIRST        230   // ..245

#ifndef IDC_STATIC
#define IDC_STATIC (-1)
#endif
