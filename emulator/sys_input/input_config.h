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

// Configurable controller input for the host shell.
//
// Models the Vectrex's two controllers (each: 4 directions + 4 buttons) for
// both keyboard and gamepad, editable via a Controller Configuration dialog and
// persisted to data/ini/controls.ini. The emulator asks for a player's merged
// (keyboard OR gamepad) digital state each frame and maps it onto the core.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Per-frame digital state for one player's 8 Vectrex actions (1 = active).
typedef struct InputPlayerState {
    int up, down, left, right;
    int b1, b2, b3, b4;
} InputPlayerState;

// Load controls.ini (falling back to sensible defaults) and bring up the
// gamepad backend. Call once at startup, after the working directory is the
// exe dir and data/ini exists.
void input_config_init(void);

// Open the Input (controllers) settings dialog as a modeless popup owned by
// `parent`. Returns the dialog HWND (as void*). Bindings save to controls.ini
// immediately as they're changed.
void* input_config_open(void* parentHwnd);

// Refresh the gamepad snapshot. Call once per emulated frame before reading
// player state.
void input_poll_begin(void);

// Current digital state for a player (0 = P1, 1 = P2): the keyboard binding OR
// that player's gamepad binding for each action.
InputPlayerState input_player_state(int player);

#ifdef __cplusplus
} // extern "C"
#endif
