
// Original AAE code copyright (C) 2025/2026 Tim Cottrill, released under
// the GNU GPL v3 or later. See accompanying source files for full details.
//==============================================================================
// joystick.h -- Usage Guide & API Reference
//==============================================================================
//
// OVERVIEW
// --------
// Unified joystick/gamepad layer for Windows with two backends:
//   - XInput  (preferred) : Xbox controllers, up to 4 pads, hotplug support
//   - WinMM   (fallback)  : Legacy DirectInput-era devices, up to 8 devices
//
// Driver selection is automatic: if any XInput device responds at init time
// the XInput path is used for ALL controllers. If no XInput device is found,
// WinMM is tried. If neither finds hardware, XInput is selected anyway in
// "passive" mode so controllers can be hot-plugged later.
//
// The public data structures are Allegro-4 compatible, so legacy code that
// reads joy[], joy_x, joy_b1, etc. works without changes.
//
//
// QUICK START
// -----------
//   #include "joystick.h"
//
//   // At application startup (call once):
//   install_joystick();          // returns 0 always; safe even with no pads
//
//   // In your frame loop:
//   poll_joystick();             // updates joy[] array and num_joysticks
//
//   // Read state -- either via structs or convenience macros:
//   if (joy_left || joy_up)  { /* digital directions, player 1 */ }
//   if (joy_b1)              { /* button A / fire 1, player 1   */ }
//   int lx = joy_x;            // analog: -128..127 (signed)
//
//   // At shutdown:
//   remove_joystick();
//
//
// DATA MODEL
// ----------
// After poll_joystick(), the global state is:
//
//   num_joysticks          -- count of currently connected controllers (0..8)
//   joy[i]                 -- JOYSTICK_INFO for controller i
//     .num_sticks          -- number of stick/axis groups
//     .num_buttons         -- number of buttons
//     .stick[s]            -- JOYSTICK_STICK_INFO (one stick or axis group)
//       .num_axis          -- axes in this group (usually 2: X, Y)
//       .axis[a].pos       -- analog position: -128..127 (signed) or 0..255 (unsigned)
//       .axis[a].d1        -- digital flag: negative direction (left / up)
//       .axis[a].d2        -- digital flag: positive direction (right / down)
//     .button[b].b         -- 0 or 1
//
// XInput stick layout:
//   stick[0] = Left Stick + D-Pad (D-Pad overrides analog when pressed)
//     axis[0] = X   axis[1] = Y
//   stick[1] = Right Stick
//     axis[0] = X   axis[1] = Y
//
// XInput button layout (16 buttons):
//   [0]  A              [1]  B              [2]  X             [3]  Y
//   [4]  LB             [5]  RB             [6]  Back          [7]  Start
//   [8]  LStick         [9]  RStick         [10] DPadUp        [11] DPadDown
//   [12] DPadLeft       [13] DPadRight      [14] LT (digital)  [15] RT (digital)
//
// WinMM stick layout varies by device. Typically:
//   stick[0]      = Main stick (X/Y, optionally Z/throttle)
//   stick[1..N-1] = Rudder / sliders
//   stick[N]      = Hat (POV), if present -- 2 axes, digital only
//
// Y axis convention: negative = up, positive = down (screen-space).
//
//
// CONVENIENCE MACROS (PLAYER 1 & 2)
// ----------------------------------
// Player 1:
//   joy_x, joy_y                -- left stick analog position
//   joy_left, joy_right         -- left stick digital flags
//   joy_up, joy_down            -- left stick digital flags
//   joy_b1 .. joy_b8            -- buttons 0-7
//   joy_throttle                -- stick[2].axis[0] (WinMM Z-axis)
//   joy_hat                     -- hat as single int: 0=center 1=left 2=down 3=right 4=up
//
// Player 2:
//   joy2_x, joy2_y, joy2_left, joy2_right, joy2_up, joy2_down
//   joy2_b1 .. joy2_b8, joy2_throttle, joy2_hat
//
//
// HOTPLUG (XINPUT ONLY)
// ---------------------
// XInput controllers are re-scanned every poll(). When a controller connects
// or disconnects, an optional callback fires:
//
//   void my_hotplug(int index, bool connected, const char* message) {
//       if (connected)
//           printf("Pad %d plugged in\n", index);
//       else
//           printf("Pad %d removed: %s\n", index, message ? message : "");
//   }
//   set_joystick_hotplug_callback(my_hotplug);
//
// The 'index' parameter is the XInput slot (0-3). The joy[] array is
// rebuilt sequentially each frame -- only connected pads get slots, so
// joy[0] is always the first connected pad regardless of which XInput
// slot it occupies.
//
// Stale-packet detection: if dwPacketNumber doesn't change for 120
// consecutive polls, capabilities are re-queried to verify the pad is
// still physically present (guards against ghost controllers).
//
// WinMM does NOT support hotplug; the device list is fixed at init time.
//
//
// BUTTON COMBOS (XINPUT ONLY)
// ---------------------------
// Edge-triggered combo detection for system-level shortcuts. A combo
// fires once when ALL specified buttons are held simultaneously for
// COMBO_CONFIRM_FRAMES (2) consecutive polls, then won't fire again
// until the combo is fully released and re-pressed.
//
//   // Predefined combos:
//   JOY_COMBO_PAUSE   -- Start + Back          (pause / unpause)
//   JOY_COMBO_ESC     -- LStick + Back         (ESC / return to GUI)
//   JOY_COMBO_MENU    -- LStick + Start        (open / close menu)
//
//   // Usage in frame loop:
//   if (joystick_check_combo(0, JOY_COMBO_PAUSE))
//       toggle_pause();
//
//   if (joystick_check_combo(0, JOY_COMBO_ESC))
//       return_to_gui();
//
//   // Custom combos (any XINPUT_GAMEPAD_* mask):
//   #define MY_COMBO_SCREENSHOT  (XINPUT_GAMEPAD_BACK | XINPUT_GAMEPAD_Y)
//   if (joystick_check_combo(0, MY_COMBO_SCREENSHOT))
//       take_screenshot();
//
// Up to JOY_MAX_COMBOS (16) distinct combo masks can be tracked at once.
// Combo detection always returns false on the WinMM path.
//
// NOTE: joystick_check_combo() performs its own XInputGetState call
// independent of poll_joystick(). This means each combo check per
// frame is an additional XInput query. For typical use (2-3 combos)
// this is negligible; if checking many combos, consider batching.
//
//
// QUERY FUNCTIONS
// ---------------
//   joystick_using_xinput()    -- true if XInput driver is active
//   joystick_driver_name()     -- "XInput", "WinMM", or "None"
//   joystick_any_connected()   -- true if num_joysticks > 0
//
//
// THREADING
// ---------
// Not thread-safe. All calls (install, poll, combo check, remove) must
// happen on the same thread -- typically the main/game loop thread.
// The hotplug callback fires synchronously during poll_joystick().
//
//
// INIT / SHUTDOWN SEQUENCE
// ------------------------
//   install_joystick()  -- Safe to call multiple times (no-ops if already installed).
//                          Always returns 0. Sets _joystick_installed = 1.
//   remove_joystick()   -- Shuts down active driver, clears all state.
//                          Safe to call if not installed (no-ops).
//
// install_joystick() succeeds even with zero controllers connected,
// so the application can start without a gamepad and detect one later
// via hotplug (XInput) or by checking joystick_any_connected().
//
//
// DIGITAL THRESHOLDS
// ------------------
// XInput:  axis.d1/d2 trigger at |pos| > 32   (out of 127 range)
//          LT/RT digital threshold: raw byte > 30  (out of 255)
// WinMM:   axis.d1 at pos < 64, axis.d2 at pos > 192 (out of 0..255)
//
//
// DEPENDENCIES
// ------------
// Windows headers: <windows.h>, <Xinput.h>, <mmsystem.h>
// Link libraries:  xinput.lib, winmm.lib  (auto-linked via #pragma comment)
// Project header:  "sys_sys_log.h"  (LOG_INFO, LOG_ERROR macros)
//
//==============================================================================

//Rumble Support howto:
/*
#include "joystick.h"

// Keep track of how many frames are left for each player's rumble
static int s_rumble_timers[MAX_JOYSTICKS] = {0};

// Flag to tell the main loop a controller just connected
static bool s_controller_just_connected = false;

// 1. Define the Callback
void OnJoystickHotplug(int xinput_slot, bool connected, const char* message)
{
    if (connected) {
        // Set the flag so our main loop knows to apply rumble
        s_controller_just_connected = true;
    }
}

// 2. Initialization
void InitInput()
{
    install_joystick();
    set_joystick_hotplug_callback(OnJoystickHotplug);
}

// 3. Your Main Frame Update Loop
void UpdateInput()
{
    // Poll updates joy[] array and triggers the hotplug callback if hardware changed
    poll_joystick();

    // Check if the callback flagged a new connection this frame
    if (s_controller_just_connected) {
        s_controller_just_connected = false;

        // Vibrate all currently connected controllers to acknowledge the hardware change.
        for (int i = 0; i < num_joysticks; ++i) {
            // joystick_set_rumble(player, left_motor, right_motor)
            // Left motor is the low-frequency rumble (heavy).
            // Right motor is the high-frequency rumble (sharp/buzzy).
            joystick_set_rumble(i, 0.4f, 0.4f); // 40% strength

            // Set timer for 15 frames (approx 0.25 seconds at 60 FPS)
            s_rumble_timers[i] = 15;
        }
    }

    // 4. Process all active rumble timers and turn them off when they expire
    for (int i = 0; i < num_joysticks; ++i) {
        if (s_rumble_timers[i] > 0) {
            s_rumble_timers[i]--;

            if (s_rumble_timers[i] == 0) {
                // The timer hit 0, shut off the motors!
                joystick_stop_rumble(i);
            }
        }
    }
}

// 4. Clean Shutdown
void ShutdownInput()
{
    remove_joystick(); // (This now safely stops any active rumbles automatically)
}
*/

#pragma once

#ifndef JOYSTICK_H
#define JOYSTICK_H

#include <windows.h>
#include <Xinput.h>

//------------------------------------------------------------------------------
// Configuration Constants
// Used by os_input.cpp to determine joystick mode
//------------------------------------------------------------------------------

#define JOY_TYPE_AUTODETECT    -1
#define JOY_TYPE_NONE           0

#define MAX_JOYSTICKS           8
#define MAX_JOYSTICK_AXIS       3
#define MAX_JOYSTICK_STICKS     5
#define MAX_JOYSTICK_BUTTONS    32

//------------------------------------------------------------------------------
// Joystick Status Flags
//------------------------------------------------------------------------------

#define JOYFLAG_DIGITAL             1
#define JOYFLAG_ANALOGUE            2
#define JOYFLAG_CALIB_DIGITAL       4
#define JOYFLAG_CALIB_ANALOGUE      8
#define JOYFLAG_CALIBRATE           16
#define JOYFLAG_SIGNED              32
#define JOYFLAG_UNSIGNED            64

// Alternative spellings for compatibility
#define JOYFLAG_ANALOG              JOYFLAG_ANALOGUE
#define JOYFLAG_CALIB_ANALOG        JOYFLAG_CALIB_ANALOGUE

//------------------------------------------------------------------------------
// Joystick Data Structures
//------------------------------------------------------------------------------

// Information about a single joystick axis
typedef struct JOYSTICK_AXIS_INFO {
    int pos;            // Position: -128..127 (signed) or 0..255 (unsigned)
    int d1;             // Digital flag: negative direction (Left/Up)
    int d2;             // Digital flag: positive direction (Right/Down)
    const char* name;
} JOYSTICK_AXIS_INFO;

// Information about one or more axes (a stick or slider)
typedef struct JOYSTICK_STICK_INFO {
    int flags;
    int num_axis;
    JOYSTICK_AXIS_INFO axis[MAX_JOYSTICK_AXIS];
    const char* name;
} JOYSTICK_STICK_INFO;

// Information about a joystick button
typedef struct JOYSTICK_BUTTON_INFO {
    int b;              // Button state: 0 or 1
    const char* name;
} JOYSTICK_BUTTON_INFO;

// Information about an entire joystick
typedef struct JOYSTICK_INFO {
    int flags;
    int num_sticks;
    int num_buttons;
    JOYSTICK_STICK_INFO stick[MAX_JOYSTICK_STICKS];
    JOYSTICK_BUTTON_INFO button[MAX_JOYSTICK_BUTTONS];
} JOYSTICK_INFO;

//------------------------------------------------------------------------------
// Global Joystick State (Allegro-compatible)
//------------------------------------------------------------------------------

extern int num_joysticks;
extern int _joystick_installed;
extern JOYSTICK_INFO joy[MAX_JOYSTICKS];

//------------------------------------------------------------------------------
// Allegro-Style Convenience Macros
// Used by os_input.cpp for legacy access
//------------------------------------------------------------------------------

// Joystick 1
#define joy_x           (joy[0].stick[0].axis[0].pos)
#define joy_y           (joy[0].stick[0].axis[1].pos)
#define joy_left        (joy[0].stick[0].axis[0].d1)
#define joy_right       (joy[0].stick[0].axis[0].d2)
#define joy_up          (joy[0].stick[0].axis[1].d1)
#define joy_down        (joy[0].stick[0].axis[1].d2)
#define joy_b1          (joy[0].button[0].b)
#define joy_b2          (joy[0].button[1].b)
#define joy_b3          (joy[0].button[2].b)
#define joy_b4          (joy[0].button[3].b)
#define joy_b5          (joy[0].button[4].b)
#define joy_b6          (joy[0].button[5].b)
#define joy_b7          (joy[0].button[6].b)
#define joy_b8          (joy[0].button[7].b)
#define joy_throttle    (joy[0].stick[2].axis[0].pos)

// Joystick 2
#define joy2_x          (joy[1].stick[0].axis[0].pos)
#define joy2_y          (joy[1].stick[0].axis[1].pos)
#define joy2_left       (joy[1].stick[0].axis[0].d1)
#define joy2_right      (joy[1].stick[0].axis[0].d2)
#define joy2_up         (joy[1].stick[0].axis[1].d1)
#define joy2_down       (joy[1].stick[0].axis[1].d2)
#define joy2_b1         (joy[1].button[0].b)
#define joy2_b2         (joy[1].button[1].b)
#define joy2_b3         (joy[1].button[2].b)
#define joy2_b4         (joy[1].button[3].b)
#define joy2_b5         (joy[1].button[4].b)
#define joy2_b6         (joy[1].button[5].b)
#define joy2_b7         (joy[1].button[6].b)
#define joy2_b8         (joy[1].button[7].b)
#define joy2_throttle   (joy[1].stick[2].axis[0].pos)

// Hat (derived from stick[1] for WinMM compatibility)
#define joy_hat         ((joy[0].stick[1].axis[0].d1) ? 1 :             \
                           ((joy[0].stick[1].axis[0].d2) ? 3 :          \
                              ((joy[0].stick[1].axis[1].d1) ? 4 :       \
                                 ((joy[0].stick[1].axis[1].d2) ? 2 :    \
                                    0))))

// Hat for Joystick2
#define joy2_hat         ((joy[1].stick[1].axis[0].d1) ? 1 :             \
                           ((joy[1].stick[1].axis[0].d2) ? 3 :          \
                              ((joy[1].stick[1].axis[1].d1) ? 4 :       \
                                 ((joy[1].stick[1].axis[1].d2) ? 2 :    \
                                    0))))


#define JOY_HAT_CENTRE  0
#define JOY_HAT_CENTER  0
#define JOY_HAT_LEFT    1
#define JOY_HAT_DOWN    2
#define JOY_HAT_RIGHT   3
#define JOY_HAT_UP      4

//------------------------------------------------------------------------------
// Public API
//------------------------------------------------------------------------------

// Initialize joystick support. Returns 0 on success, -1 on failure.
// NOTE: Returns success even if no joysticks are currently connected,
// allowing hotplug detection during polling.
int install_joystick();

// Shut down joystick support.
void remove_joystick();

// Poll current joystick state. Returns 0 on success, -1 on failure.
// Automatically handles hotplug detection for XInput controllers.
int poll_joystick();

//------------------------------------------------------------------------------
// Rumble Support
//------------------------------------------------------------------------------

// Set vibration motors for a specific player (joy[] index).
// Speeds are normalized from 0.0f (off) to 1.0f (max).
// Returns true on success (XInput only).
bool joystick_set_rumble(int player, float left_motor_speed, float right_motor_speed);

// Stops all rumble on a specific player controller.
void joystick_stop_rumble(int player);

//------------------------------------------------------------------------------
// Hotplug Callback Support
//------------------------------------------------------------------------------

// Callback signature: index is the XInput slot (0-3) or joy[] index,
// connected indicates plug/unplug, message may contain error info.
typedef void (*JoystickHotplugCallback)(int index, bool connected, const char* message);

void set_joystick_hotplug_callback(JoystickHotplugCallback callback);

//------------------------------------------------------------------------------
// Button Combo Support (XInput only)
//
// Predefined combo masks - pass to joystick_check_combo().
// Additional combos can be defined inline using XINPUT_GAMEPAD_* flags.
//------------------------------------------------------------------------------

#define JOY_MAX_COMBOS 16  // Max number of distinct combo masks tracked simultaneously

#define JOY_COMBO_PAUSE   (XINPUT_GAMEPAD_START | XINPUT_GAMEPAD_BACK)       // Start + Back  : pause/unpause
#define JOY_COMBO_ESC     (XINPUT_GAMEPAD_LEFT_THUMB | XINPUT_GAMEPAD_BACK)  // LS + Back     : ESC / return to GUI
#define JOY_COMBO_MENU    (XINPUT_GAMEPAD_LEFT_THUMB | XINPUT_GAMEPAD_START) // LS + Start    : open/close menu

// Edge-triggered combo check: returns true once per press (not every frame while held).
// All bits in buttonMask must be simultaneously held to trigger.
// Always returns false on the WinMM fallback path.
bool joystick_check_combo(int player, WORD buttonMask);

//------------------------------------------------------------------------------
// Query Functions
//------------------------------------------------------------------------------

// Returns true if currently using XInput driver
bool joystick_using_xinput();

// Returns the name of the active driver ("XInput", "WinMM", or "None")
const char* joystick_driver_name();

// Returns true if any joystick is currently connected
bool joystick_any_connected();

#endif // JOYSTICK_H