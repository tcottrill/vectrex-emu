//==============================================================================
// rawinput.h -- Usage Guide & API Reference
//==============================================================================
//
// OVERVIEW
// --------
// Windows Raw Input wrapper providing low-level keyboard and mouse access
// with three complementary consumption models:
//
//   1. CALLBACK (GLFW-style)  -- register functions that fire on key/button/move
//   2. POLLED STATE ARRAYS    -- read key[] and mouse_b directly each frame
//   3. QUERY FUNCTIONS        -- IsKeyDown(), GetMouseX(), etc.
//
// All three models read from the same internal state, which is updated on a
// dedicated worker thread. Raw WM_INPUT messages are enqueued on the message
// pump thread and batch-processed by the worker, keeping input handling off
// the rendering path.
//
// Designed for arcade / retro emulator use: the key defines and mouse_b
// bitmask are Allegro-4 compatible, so legacy game code that reads key[]
// and mouse_b works without changes.
//
//
// QUICK START
// -----------
//   #include "rawinput.h"
//
//   // During window creation (after CreateWindow / HWND is valid):
//   if (FAILED(RawInput_Initialize(hwnd))) {
//       MessageBox(hwnd, "Raw input init failed", "Error", MB_ICONERROR);
//       return -1;
//   }
//
//   // In your WndProc:
//   case WM_INPUT:
//       return RawInput_ProcessInput(hWnd, wParam, lParam);
//
//   // --- Polled access (in your frame loop) ---
//   if (key[KEY_ESC])          { quit(); }
//   if (key[KEY_LEFT])         { move_left(); }
//   if (mouse_b & 0x01)       { fire(); }     // left button
//
//   // --- Query functions ---
//   if (IsKeyDown(KEY_SPACE))  { jump(); }
//
//   // --- Relative mouse (mickeys) ---
//   int mx, my;
//   get_mouse_mickeys(&mx, &my);              // reads and resets deltas
//
//   // --- Window-relative mouse position ---
//   int wx, wy;
//   get_mouse_win(&wx, &wy);                  // uses GetCursorPos + ScreenToClient
//
//   // --- Callbacks (register once, fires on every event) ---
//   SetKeyCallback([](int key, int scancode, int action, int mods) {
//       // action: 1 = pressed, 0 = released
//       // mods: bitmask of RI_MOD_SHIFT | RI_MOD_CONTROL | RI_MOD_ALT | RI_MOD_SUPER
//   });
//
//   SetMouseButtonCallback([](int button, int action, int mods) {
//       // button: 0=left, 1=right, 2=middle
//       // action: 1=pressed, 0=released
//   });
//
//   SetCursorPositionCallback([](double xpos, double ypos) {
//       // accumulated raw position (not window coords -- see notes below)
//   });
//
//   // At shutdown:
//   RawInput_Shutdown();
//
//
// INITIALIZATION & SHUTDOWN
// -------------------------
//   HRESULT RawInput_Initialize(HWND hWnd)
//       Registers keyboard (HID usage 0x06) and mouse (HID usage 0x02) as
//       Raw Input devices with RIDEV_INPUTSINK (receives input even when
//       window is not focused). Zeros all state arrays. Starts the input
//       worker thread. Returns S_OK on success, E_FAIL if registration fails.
//
//       Safe to call multiple times -- if already initialized, automatically
//       shuts down the previous instance before reinitializing.
//
//   void RawInput_Shutdown()
//       Signals the worker thread to exit, joins it. Safe to call if not
//       initialized (no-ops). Does NOT unregister the Raw Input devices --
//       Windows handles that on process exit.
//
//
// WM_INPUT MESSAGE HANDLING
// -------------------------
//   LRESULT RawInput_ProcessInput(HWND hWnd, WPARAM wParam, LPARAM lParam)
//       Call this from your WndProc on WM_INPUT. It:
//         1. Calls GetRawInputData() to extract the RAWINPUT struct
//         2. Pushes it onto the worker queue (under mutex)
//         3. Signals the worker thread
//         4. Returns DefWindowProc() result
//
//       If input is paused (via RawInput_SetPaused), the event is silently
//       discarded to DefWindowProc without queuing.
//
//       The worker thread batch-processes all queued events, updating key[],
//       lastkey[], mouse state, mouse_b, and firing callbacks.
//
//
// KEYBOARD STATE
// --------------
// Two parallel arrays track keyboard state:
//
//   unsigned char key[256]    -- Allegro-compatible keystate buffer
//       key[vkCode] = 1 when pressed, 0 when released.
//       Read directly in your frame loop: if (key[KEY_A]) { ... }
//       Indexed by virtual key code. Use the KEY_* defines for readability.
//
//   unsigned int lastkey[256] -- Hold counter (internal)
//       Increments each Raw Input message while a key is held. Useful for
//       auto-repeat / hold detection. Read via isKeyHeld(vkCode).
//       Returns 0 when released, 1+ when held (count of press messages).
//
// Key defines follow Allegro-4 naming mapped to Windows VK codes:
//   KEY_A..KEY_Z         (0x41..0x5A)
//   KEY_0..KEY_9         (0x30..0x39)
//   KEY_0_PAD..KEY_9_PAD (VK_NUMPAD0..VK_NUMPAD9)
//   KEY_F1..KEY_F12      (VK_F1..VK_F12)
//   KEY_ESC, KEY_ENTER, KEY_SPACE, KEY_TAB, KEY_BACKSPACE
//   KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN
//   KEY_INSERT, KEY_DEL, KEY_HOME, KEY_END, KEY_PGUP, KEY_PGDN
//   KEY_LSHIFT, KEY_RSHIFT, KEY_LCONTROL, KEY_RCONTROL
//   KEY_ALT (VK_LMENU), KEY_ALTGR (VK_RMENU)
//   KEY_LWIN, KEY_RWIN, KEY_CAPSLOCK, KEY_NUMLOCK, KEY_SCRLOCK
//   KEY_TILDE, KEY_MINUS, KEY_EQUALS, KEY_COMMA, KEY_STOP, KEY_SLASH
//   KEY_OPENBRACE, KEY_CLOSEBRACE, KEY_BACKSLASH, KEY_COLON, KEY_QUOTE
//   KEY_PRTSCR, KEY_PAUSE
//   KEY_MAX = 0xEF
//
// Left/right modifier disambiguation: VK_SHIFT is resolved to VK_LSHIFT
// or VK_RSHIFT via MapVirtualKey. VK_CONTROL and VK_MENU are resolved
// using the E0 escape flag. This means key[KEY_LSHIFT] and key[KEY_RSHIFT]
// work independently.
//
// Numpad vs. navigation: when NumLock is off, Insert/Home/PgUp/etc. are
// remapped to their numpad equivalents (VK_NUMPAD0, VK_NUMPAD7, etc.)
// based on the E0 flag. This matches standard Windows keyboard behavior.
//
//
// KEYBOARD QUERY FUNCTIONS
// ------------------------
//   int  isKeyHeld(INT vkCode)
//       Returns the hold counter from lastkey[]. 0 = not held, 1+ = held.
//       Useful for detecting key repeat (value increases each raw message
//       while the key remains down).
//
//   bool IsKeyDown(INT vkCode)
//       Returns true if key[vkCode] is nonzero (key is pressed).
//
//   bool IsKeyUp(INT vkCode)
//       Returns true if key[vkCode] is zero (key is released).
//
//
// KEYBOARD CALLBACK
// -----------------
//   typedef void (*KeyCallback)(int key, int scancode, int action, int mods);
//   void SetKeyCallback(KeyCallback callback);
//
//   Fires once per key press/release event on the worker thread.
//   Parameters:
//     key      -- Windows virtual key code (left/right disambiguated)
//     scancode -- hardware scan code
//     action   -- 1 = pressed, 0 = released
//     mods     -- bitmask: RI_MOD_SHIFT   (0x01)
//                          RI_MOD_CONTROL (0x02)
//                          RI_MOD_ALT     (0x04)
//                          RI_MOD_SUPER   (0x08)
//
//   Modifier state is sampled via GetAsyncKeyState() at callback time,
//   so it reflects the system-wide modifier state, not just raw input.
//
//   Pass nullptr to unregister the callback.
//
//
// MOUSE STATE
// -----------
// Internal mouse state tracks:
//   - Accumulated absolute position (x, y) -- sum of all raw deltas since init
//   - Per-frame relative deltas (dx, dy) -- accumulated between reads
//   - Wheel delta (dwheel)
//   - Button states (left, middle, right)
//
// IMPORTANT: The accumulated absolute position (GetMouseX/Y, SetMouseX/Y)
// is NOT the actual Windows cursor position. It is the running sum of all
// raw mouse deltas and can grow without bound. For actual window-relative
// cursor coordinates, use get_mouse_win() instead.
//
//
// MOUSE QUERY FUNCTIONS -- Relative Motion (Mickeys)
// ---------------------------------------------------
//   void get_mouse_mickeys(int* mx, int* my)
//       Returns accumulated relative motion since last call, scaled by the
//       mickey scale factor (default 1.0). Resets dx/dy to zero after read.
//       This is the primary mouse input function for game/emulator use.
//
//   void set_mouse_mickey_scale(float scale)
//       Sets the multiplier applied to raw deltas in get_mouse_mickeys().
//       Default is 1.0. Applied at read time, not accumulation time, so
//       changing it mid-frame does not lose precision.
//
//
// MOUSE QUERY FUNCTIONS -- Window Position
// -----------------------------------------
//   void get_mouse_win(int* mx, int* my)
//       Returns the actual Windows cursor position in client coordinates
//       via GetCursorPos() + ScreenToClient(). Independent of raw input
//       accumulation. Use this for UI/menu mouse interaction.
//
//
// MOUSE QUERY FUNCTIONS -- Accumulated Raw Position
// --------------------------------------------------
//   LONG GetMouseX()  / void SetMouseX(LONG x)
//   LONG GetMouseY()  / void SetMouseY(LONG y)
//   LONG GetMouseWheel()  / void SetMouseWheel(LONG w)
//       Get/set the accumulated raw position. This is the running sum of
//       all raw deltas, not the Windows cursor position. Useful if you want
//       to maintain your own coordinate space (e.g., for a virtual cursor).
//       SetMouse* can be used to reset or reposition the virtual origin.
//
//   LONG GetMouseXChange()
//   LONG GetMouseYChange()
//   LONG GetMouseWheelChange()
//       Returns current dx/dy/dwheel without resetting. Unlike
//       get_mouse_mickeys(), these do NOT zero the deltas after read
//       and do NOT apply the mickey scale factor.
//
//
// MOUSE BUTTON STATE
// ------------------
//   bool IsMouseLButtonDown() / IsMouseLButtonUp()
//   bool IsMouseRButtonDown() / IsMouseRButtonUp()
//   bool IsMouseMButtonDown() / IsMouseMButtonUp()
//       Query individual button state. Updates come from raw input events.
//
//   extern int mouse_b;
//       Allegro-compatible button bitmask, updated each raw input message:
//         bit 0 (0x01) = left button
//         bit 1 (0x02) = right button
//         bit 2 (0x04) = middle button
//       Usage: if (mouse_b & 0x01) { /* left held */ }
//
//   Mouse buttons are processed via bitmask flag tests (not a switch),
//   so simultaneous button state changes in a single RAWINPUT message
//   are all handled correctly.
//
//
// MOUSE BUTTON CALLBACK
// ---------------------
//   typedef void (*MouseButtonCallback)(int button, int action, int mods);
//   void SetMouseButtonCallback(MouseButtonCallback callback);
//
//   Fires on the worker thread when a mouse button changes state.
//   Parameters:
//     button -- 0=left, 1=right, 2=middle
//     action -- 1=pressed, 0=released
//     mods   -- same bitmask as key callback (RI_MOD_*)
//
//   Pass nullptr to unregister the callback.
//
//
// CURSOR POSITION CALLBACK
// ------------------------
//   typedef void (*CursorPositionCallback)(double xpos, double ypos);
//   void SetCursorPositionCallback(CursorPositionCallback callback);
//
//   Fires on the worker thread whenever raw mouse motion is received.
//   Receives the accumulated raw position (not window coords).
//   The mickey scale factor does NOT affect this callback -- it reports
//   the raw accumulated totals.
//
//   Pass nullptr to unregister the callback.
//
//
// PAUSE / RESUME
// --------------
//   void RawInput_SetPaused(bool paused)
//       When paused:
//         - Incoming WM_INPUT messages are discarded (not queued)
//         - All queued events are flushed
//         - key[] and lastkey[] are zeroed (prevents stuck keys)
//         - mouse_b and all button states are cleared
//         - Mouse deltas (dx, dy, dwheel) are zeroed
//       When resumed (paused=false):
//         - Processing resumes normally from a clean state
//
//       Designed for alt-tab / lost-focus handling. Call with true when
//       your window loses focus, false when it regains focus.
//
//       Calling SetPaused(true) when already paused is a safe no-op.
//       Only the transition from unpaused -> paused triggers the flush.
//
//
// MODIFIER FLAGS
// --------------
//   int GetModifierFlags()
//       Returns current modifier bitmask via GetAsyncKeyState():
//         RI_MOD_SHIFT   (0x01) -- either Shift key
//         RI_MOD_CONTROL (0x02) -- either Ctrl key
//         RI_MOD_ALT     (0x04) -- either Alt key
//         RI_MOD_SUPER   (0x08) -- either Win key
//
//       This reads system-wide key state, not the raw input buffer.
//       The same bitmask is passed to key and mouse button callbacks.
//
//
// UTILITIES
// ---------
//   void test_clr()
//       Zeros key[] and lastkey[]. Use to force-clear keyboard state
//       without going through the full pause/resume cycle. Does not
//       affect mouse state.
//
//   bset(p, m) / bclr(p, m)
//       Bitfield helper macros. bset sets bits, bclr clears bits.
//       Used internally for mouse_b; available for general use.
//
//   toUpper(ch)
//       Macro to convert lowercase ASCII to uppercase via bitmask.
//
//
// THREADING MODEL
// ---------------
// Input processing runs on a dedicated worker thread:
//
//   WndProc receives WM_INPUT
//     -> RawInput_ProcessInput() extracts RAWINPUT, pushes to queue
//     -> signals condition variable
//   Worker thread wakes
//     -> swaps queue to local copy (batch processing under lock)
//     -> processes each event: updates key[], lastkey[], mouse state
//     -> fires callbacks (on worker thread, NOT the main thread)
//     -> unlocks and waits for next signal
//
// IMPORTANT: Callbacks fire on the WORKER THREAD. If your callback
// touches rendering state or non-thread-safe data, you are responsible
// for synchronization (e.g., pushing events to a queue that the main
// thread drains each frame).
//
// The key[], lastkey[], and mouse state are written only by the worker
// thread and read by the main thread -- single-producer / single-consumer
// with natural-width writes, which is safe on x86 for the data sizes
// involved.
//
//
// INIT / SHUTDOWN SEQUENCE
// ------------------------
//   RawInput_Initialize(hWnd) -- Registers devices, zeros state, starts
//                                 worker thread. Returns S_OK or E_FAIL.
//                                 Safe to call again (auto-shuts down first).
//   RawInput_Shutdown()       -- Stops worker thread, joins. Safe to call
//                                 if not initialized (no-ops).
//
// Typical integration in a WndProc:
//
//   case WM_INPUT:
//       return RawInput_ProcessInput(hWnd, wParam, lParam);
//
//   case WM_ACTIVATEAPP:
//       RawInput_SetPaused(wParam == FALSE);  // pause on deactivate
//       break;
//
//
// DEPENDENCIES
// ------------
// Windows headers:  <windows.h> (Raw Input API, GetAsyncKeyState)
// C++ standard:     C++11 (thread, mutex, condition_variable, atomic, queue)
// Project headers:  "sys_sys_log.h"
//
//==============================================================================

// Authors:
//   - Jay Tennant (original implementation)
//   - TC (Allegro Compatibility, Full Keyboard Key Support, Modernization and GLFW style callback system)
// -----------------------------------------------------------------------------


#pragma once

#include <windows.h>

#define bset(p,m) ((p) |= (m))
#define bclr(p,m) ((p) &= ~(m))

#define KEY_A                 0x41  //65
#define KEY_B                 0x42
#define KEY_C                 0x43
#define KEY_D                 0x44
#define KEY_E                 0x45
#define KEY_F                 0x46
#define KEY_G                 0x47
#define KEY_H                 0x48
#define KEY_I                 0x49
#define KEY_J                 0x4a
#define KEY_K                 0x4b
#define KEY_L                 0x4c
#define KEY_M                 0x4d
#define KEY_N                 0x4e
#define KEY_O                 0x4f
#define KEY_P                 0x50
#define KEY_Q                 0x51
#define KEY_R                 0x52
#define KEY_S                 0x53
#define KEY_T                 0x54
#define KEY_U                 0x55
#define KEY_V                 0x56
#define KEY_W                 0x57
#define KEY_X                 0x58
#define KEY_Y                 0x59
#define KEY_Z                 0x5a
#define KEY_0                 0x30  //48
#define KEY_1                 0x31
#define KEY_2                 0x32
#define KEY_3                 0x33
#define KEY_4                 0x34
#define KEY_5                 0x35
#define KEY_6                 0x36
#define KEY_7                 0x37
#define KEY_8                 0x38
#define KEY_9                 0x39
#define KEY_0_PAD             VK_NUMPAD0  
#define KEY_1_PAD             VK_NUMPAD1
#define KEY_2_PAD             VK_NUMPAD2
#define KEY_3_PAD             VK_NUMPAD3
#define KEY_4_PAD             VK_NUMPAD4
#define KEY_5_PAD             VK_NUMPAD5
#define KEY_6_PAD             VK_NUMPAD6
#define KEY_7_PAD             VK_NUMPAD7
#define KEY_8_PAD             VK_NUMPAD8
#define KEY_9_PAD             VK_NUMPAD9
#define KEY_F1                VK_F1  
#define KEY_F2                VK_F2  
#define KEY_F3                VK_F3  
#define KEY_F4                VK_F4  
#define KEY_F5                VK_F5  
#define KEY_F6                VK_F6  
#define KEY_F7                VK_F7  
#define KEY_F8                VK_F8  
#define KEY_F9                VK_F9  
#define KEY_F10               VK_F10  
#define KEY_F11               VK_F11  
#define KEY_F12               VK_F12  
#define KEY_ESC               VK_ESCAPE  
#define KEY_TILDE             0xc0
#define KEY_MINUS             0xbd
#define KEY_EQUALS            0xbb
#define KEY_BACKSPACE         VK_BACK
#define KEY_TAB               VK_TAB
#define KEY_OPENBRACE         0xdb
#define KEY_CLOSEBRACE        0xdd
#define KEY_ENTER             VK_RETURN
#define KEY_COLON             0xba
#define KEY_QUOTE             0xde
#define KEY_BACKSLASH         0xdc
#define KEY_BACKSLASH2        0xdc
#define KEY_COMMA             0xbc
#define KEY_STOP              0xbe
#define KEY_SLASH             0xbf
#define KEY_SPACE             VK_SPACE
#define KEY_INSERT            VK_INSERT
#define KEY_DEL               VK_DELETE
#define KEY_HOME              VK_HOME
#define KEY_END               VK_END
#define KEY_PGUP              VK_PRIOR
#define KEY_PGDN              VK_NEXT
#define KEY_LEFT              VK_LEFT
#define KEY_RIGHT             VK_RIGHT
#define KEY_UP                VK_UP
#define KEY_DOWN              VK_DOWN
#define KEY_SLASH_PAD         VK_DIVIDE
#define KEY_ASTERISK          VK_MULTIPLY
#define KEY_MINUS_PAD         VK_SUBTRACT
#define KEY_PLUS_PAD          VK_ADD
#define KEY_DEL_PAD           VK_DECIMAL
#define KEY_ENTER_PAD         VK_SEPARATOR
#define KEY_PRTSCR            VK_SNAPSHOT
#define KEY_PAUSE             VK_PAUSE
#define KEY_ABNT_C1           0xc1
#define KEY_YEN               125
#define KEY_KANA              VK_KANA
#define KEY_CONVERT           121
#define KEY_NOCONVERT         123
#define KEY_AT                145
#define KEY_CIRCUMFLEX        144
#define KEY_COLON2            146
#define KEY_KANJI             148
#define KEY_EQUALS_PAD        0x00
#define KEY_BACKQUOTE         192
#define KEY_SEMICOLON         0xba
#define KEY_LSHIFT            VK_LSHIFT
#define KEY_RSHIFT            VK_RSHIFT
#define KEY_LCONTROL          VK_LCONTROL
#define KEY_RCONTROL          VK_RCONTROL
#define KEY_ALT               VK_LMENU
#define KEY_LMENU             VK_LMENU
#define KEY_RMENU             VK_RMENU
#define KEY_ALTGR             VK_RMENU
#define KEY_LWIN              VK_LWIN
#define KEY_RWIN              VK_RWIN
#define KEY_MENU              VK_MENU
#define KEY_SCRLOCK           VK_SCROLL
#define KEY_NUMLOCK           VK_NUMLOCK
#define KEY_CAPSLOCK          VK_CAPITAL

#define KEY_MAX               0xEF  //127 Not!

// Public modifier masks 
enum RI_Modifiers {
    RI_MOD_SHIFT = 0x01,
    RI_MOD_CONTROL = 0x02,
    RI_MOD_ALT = 0x04,
    RI_MOD_SUPER = 0x08
};

#define toUpper(ch) ((ch >= 'a' && ch <='z') ? ch & 0x5f : ch)
#define RI_MOUSE_HWHEEL 0x0800 

int GetModifierFlags();

// -----------------------------------------------------------------------------
// Keyboard Callback Support
// -----------------------------------------------------------------------------
typedef void (*KeyCallback)(int key, int scancode, int action, int mods);

// -----------------------------------------------------------------------------
// Registers a key callback (GLFW-style)
// -----------------------------------------------------------------------------
void SetKeyCallback(KeyCallback callback);

// -----------------------------------------------------------------------------
// Callback Type Definitions
// -----------------------------------------------------------------------------
typedef void (*MouseButtonCallback)(int button, int action, int mods);
typedef void (*CursorPositionCallback)(double xpos, double ypos);

// -----------------------------------------------------------------------------
// Set callback functions
// -----------------------------------------------------------------------------
void SetMouseButtonCallback(MouseButtonCallback callback);
void SetCursorPositionCallback(CursorPositionCallback callback);

// -----------------------------------------------------------------------------
// Sets the scaling factor for raw mouse mickeys (default is 1.0)
// -----------------------------------------------------------------------------
void set_mouse_mickey_scale(float scale);

// -----------------------------------------------------------------------------
// Allegro compatible C style keystate buffers.
// -----------------------------------------------------------------------------
extern int mouse_b;
extern unsigned char key[256];
// -----------------------------------------------------------------------------
// Registers a mouse and keyboard for raw input;
// Usage: if (FAILED(RawInput_Initialize(hwnd))) {
// MessageBox(hwnd, "Failed to initialize raw input", "Error", MB_ICONERROR);
// return -1;
// }
// -----------------------------------------------------------------------------
HRESULT RawInput_Initialize(HWND hWnd);

// -----------------------------------------------------------------------------
// Pauses/Resumes background input processing and clears stuck keys
// -----------------------------------------------------------------------------
void RawInput_SetPaused(bool paused);

// -----------------------------------------------------------------------------
// Processes WM_INPUT messages
// -----------------------------------------------------------------------------
LRESULT RawInput_ProcessInput(HWND hWnd, WPARAM wParam, LPARAM lParam);

// -----------------------------------------------------------------------------
// NEW: Shuts down the raw input worker thread gracefully
// -----------------------------------------------------------------------------
void RawInput_Shutdown();

// -----------------------------------------------------------------------------
// Keyboard State Queries
// -----------------------------------------------------------------------------
int isKeyHeld(INT vkCode);
bool IsKeyDown(INT vkCode);
bool IsKeyUp(INT vkCode);

// -----------------------------------------------------------------------------
// Summed mouse state checks/sets;
// Use as convenience, ie. keeping track of movements without needing to maintain separate data set
// Added for Allegro Code Compatibility
// -----------------------------------------------------------------------------
void get_mouse_win(int *mickeyx, int *mickeyy);
void get_mouse_mickeys(int *mickeyx, int *mickeyy);
LONG GetMouseX();
LONG GetMouseY();
LONG GetMouseWheel();
void SetMouseX(LONG x);
void SetMouseY(LONG y);
void SetMouseWheel(LONG wheel);

// -----------------------------------------------------------------------------
// Relative mouse state changes
// -----------------------------------------------------------------------------
LONG GetMouseXChange();
LONG GetMouseYChange();
LONG GetMouseWheelChange();

// -----------------------------------------------------------------------------
// Mouse button state checks
// -----------------------------------------------------------------------------
bool IsMouseLButtonDown();
bool IsMouseLButtonUp();
bool IsMouseRButtonDown();
bool IsMouseRButtonUp();
bool IsMouseMButtonDown();
bool IsMouseMButtonUp();

// -----------------------------------------------------------------------------
// Utilities
// -----------------------------------------------------------------------------
void test_clr();