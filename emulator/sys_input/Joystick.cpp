//------------------------------------------------------------------------------
// joystick.cpp
// Unified joystick support with Allegro-compatible API
// Supports XInput (preferred) and Win32 WinMM fallback
//
// XInput hotplug is handled via polling - each poll() checks connection state
// and fires callbacks on changes. WinMM does not support hotplug reliably.
//------------------------------------------------------------------------------

#include "joystick.h"
#include "sys_log.h"

#include <windows.h>
#include <mmsystem.h>
#include <Xinput.h>
#include <cstring>

#pragma comment(lib, "winmm.lib")
#ifndef WIN7BUILD
// Modern Win8+ build: link the full XInput 1.4 import library.  This pulls
// in xinput1_4.dll at runtime which does not exist on Windows 7.
#pragma comment(lib, "xinput.lib")
#else
// Windows 7 build: link the legacy XInput 9.1.0 import library so the
// binary resolves against xinput9_1_0.dll, which ships with Win7.  All
// three XInput functions used here -- XInputGetState, XInputSetState,
// XInputGetCapabilities -- are present in xinput9_1_0.  Two compatibility
// notes already satisfied by the code below:
//   * XInputGetCapabilities must be called with dwFlags = 0 (no
//     XINPUT_FLAG_GAMEPAD support).  See xinput::poll().
//   * The Guide (Xbox) button is not reported in XInputGetState on this
//     version.  The code never reads it.
#pragma comment(lib, "xinput9_1_0.lib")
#endif

//------------------------------------------------------------------------------
// Global State (Allegro-compatible)
//------------------------------------------------------------------------------

int num_joysticks = 0;
int _joystick_installed = 0;
JOYSTICK_INFO joy[MAX_JOYSTICKS];

//------------------------------------------------------------------------------
// Internal State
//------------------------------------------------------------------------------

static int  s_comboHoldFrames[MAX_JOYSTICKS][JOY_MAX_COMBOS] = {};
static constexpr int COMBO_CONFIRM_FRAMES = 2; // must be held this many frames to trigger
static JoystickHotplugCallback s_hotplug_callback = nullptr;

// Generalized combo edge-detection state.
// Each distinct buttonMask gets its own slot so combos don't interfere.
static WORD s_comboMasks[JOY_MAX_COMBOS]          = {};
static int  s_numCombos                            = 0;
static bool s_comboWasHeld[MAX_JOYSTICKS][JOY_MAX_COMBOS] = {};

static int get_combo_index(WORD mask)
{
    for (int i = 0; i < s_numCombos; i++)
        if (s_comboMasks[i] == mask) return i;
    if (s_numCombos < JOY_MAX_COMBOS)
    {
        s_comboMasks[s_numCombos] = mask;
        return s_numCombos++;
    }
    return 0; // table full: fall back to slot 0 rather than crash
}

enum class JoystickDriver {
	None,
	XInput,
	WinMM
};

static JoystickDriver s_active_driver = JoystickDriver::None;

static const char* const NAME_UNUSED = "unused";

//------------------------------------------------------------------------------
// Utility Macros and Functions
//------------------------------------------------------------------------------

#ifndef MIN
#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#endif

#ifndef MAX
#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#endif

static int clamp_int(int v, int lo, int hi)
{
	if (v < lo) return lo;
	if (v > hi) return hi;
	return v;
}

//------------------------------------------------------------------------------
// Common: Clear all joystick state
//------------------------------------------------------------------------------

static void clear_all_joystick_state()
{
	for (int i = 0; i < MAX_JOYSTICKS; ++i) {
		joy[i].flags = 0;
		joy[i].num_sticks = 0;
		joy[i].num_buttons = 0;

		for (int s = 0; s < MAX_JOYSTICK_STICKS; ++s) {
			joy[i].stick[s].flags = 0;
			joy[i].stick[s].num_axis = 0;
			joy[i].stick[s].name = NAME_UNUSED;

			for (int a = 0; a < MAX_JOYSTICK_AXIS; ++a) {
				joy[i].stick[s].axis[a].pos = 0;
				joy[i].stick[s].axis[a].d1 = 0;
				joy[i].stick[s].axis[a].d2 = 0;
				joy[i].stick[s].axis[a].name = NAME_UNUSED;
			}
		}

		for (int b = 0; b < MAX_JOYSTICK_BUTTONS; ++b) {
			joy[i].button[b].b = 0;
			joy[i].button[b].name = NAME_UNUSED;
		}
	}

	num_joysticks = 0;
}

static void reset_single_joystick(int index)
{
	if (index < 0 || index >= MAX_JOYSTICKS)
		return;

	joy[index].flags = 0;
	joy[index].num_sticks = 0;
	joy[index].num_buttons = 0;

	for (int s = 0; s < MAX_JOYSTICK_STICKS; ++s) {
		joy[index].stick[s].flags = 0;
		joy[index].stick[s].num_axis = 0;
		joy[index].stick[s].name = NAME_UNUSED;

		for (int a = 0; a < MAX_JOYSTICK_AXIS; ++a) {
			joy[index].stick[s].axis[a].pos = 0;
			joy[index].stick[s].axis[a].d1 = 0;
			joy[index].stick[s].axis[a].d2 = 0;
			joy[index].stick[s].axis[a].name = NAME_UNUSED;
		}
	}

	for (int b = 0; b < MAX_JOYSTICK_BUTTONS; ++b) {
		joy[index].button[b].b = 0;
		joy[index].button[b].name = NAME_UNUSED;
	}
}

//==============================================================================
// XInput Implementation
//==============================================================================

//==============================================================================
// XInput Implementation
//==============================================================================

namespace xinput {
	static constexpr int MAX_CONTROLLERS = 4;
	static constexpr int DIGITAL_THRESHOLD = 32;
	static constexpr int TRIGGER_THRESHOLD = 30;

	// Stale packet detection
	static constexpr int STALE_FRAME_THRESHOLD = 120;

	// Per-controller state tracking
	static bool s_connected[MAX_CONTROLLERS] = {};
	static DWORD s_last_packet[MAX_CONTROLLERS] = {};
	static int s_stale_frames[MAX_CONTROLLERS] = {};

	// Performance throttling for offline controllers and state caching
	static int s_offline_check_timer[MAX_CONTROLLERS] = {};
	static XINPUT_STATE s_cached_states[MAX_CONTROLLERS] = {};
	static int s_joy_to_xinput[MAX_JOYSTICKS] = {};

	// Keep track of rumble duration internally
	static int s_rumble_timer[MAX_CONTROLLERS] = {};

	static const char* const BUTTON_NAMES[16] = {
		"A", "B", "X", "Y",
		"LB", "RB",
		"Back", "Start",
		"LStick", "RStick",
		"DPadUp", "DPadDown", "DPadLeft", "DPadRight",
		"LT", "RT"
	};

	static void reset_state()
	{
		for (int i = 0; i < MAX_CONTROLLERS; ++i) {
			s_connected[i] = false;
			s_last_packet[i] = 0;
			s_stale_frames[i] = 0;
			s_offline_check_timer[i] = 0;
			s_rumble_timer[i] = 0;
		}
		for (int i = 0; i < MAX_JOYSTICKS; ++i) {
			s_joy_to_xinput[i] = -1;
		}
	}

	static bool is_available()
	{
		for (DWORD i = 0; i < MAX_CONTROLLERS; ++i) {
			XINPUT_STATE st = {};
			if (XInputGetState(i, &st) == ERROR_SUCCESS)
				return true;
		}
		return false;
	}

	static int scale_thumb(SHORT v, int deadzone)
	{
		int iv = static_cast<int>(v);
		// Fix: Prevent magnitude overflow when input is exactly -32768
		if (iv < -32767) iv = -32767;

		int av = (iv < 0) ? -iv : iv;

		if (av <= deadzone)
			return 0;

		int sign = (iv < 0) ? -1 : 1;
		int mag = av - deadzone;
		int denom = 32767 - deadzone;

		if (denom <= 0)
			return 0;

		int out = (mag * 127) / denom;
		out = clamp_int(out, 0, 127);
		return out * sign;
	}

	static void set_axis(JOYSTICK_AXIS_INFO& axis, int pos)
	{
		axis.pos = clamp_int(pos, -128, 127);
		axis.d1 = (axis.pos < -DIGITAL_THRESHOLD) ? 1 : 0;
		axis.d2 = (axis.pos > DIGITAL_THRESHOLD) ? 1 : 0;
	}

	static void setup_descriptor(int index)
	{
		JOYSTICK_INFO& j = joy[index];

		j.flags = JOYFLAG_DIGITAL | JOYFLAG_ANALOGUE | JOYFLAG_SIGNED;
		j.num_sticks = 2;

		j.stick[0].flags = j.flags;
		j.stick[0].num_axis = 2;
		j.stick[0].name = "Left Stick";
		j.stick[0].axis[0].name = "X";
		j.stick[0].axis[1].name = "Y";

		j.stick[1].flags = j.flags;
		j.stick[1].num_axis = 2;
		j.stick[1].name = "Right Stick";
		j.stick[1].axis[0].name = "X";
		j.stick[1].axis[1].name = "Y";

		j.num_buttons = 16;
		for (int b = 0; b < j.num_buttons; ++b)
			j.button[b].name = BUTTON_NAMES[b];
	}

	static void apply_dpad_to_left_stick(JOYSTICK_INFO& j, WORD buttons)
	{
		// X axis
		if (buttons & XINPUT_GAMEPAD_DPAD_LEFT) {
			j.stick[0].axis[0].pos = -128;
			j.stick[0].axis[0].d1 = 1;
			j.stick[0].axis[0].d2 = 0;
		}
		else if (buttons & XINPUT_GAMEPAD_DPAD_RIGHT) {
			j.stick[0].axis[0].pos = 127;
			j.stick[0].axis[0].d1 = 0;
			j.stick[0].axis[0].d2 = 1;
		}

		// Y axis (inverted: negative = up)
		if (buttons & XINPUT_GAMEPAD_DPAD_UP) {
			j.stick[0].axis[1].pos = -128;
			j.stick[0].axis[1].d1 = 1;
			j.stick[0].axis[1].d2 = 0;
		}
		else if (buttons & XINPUT_GAMEPAD_DPAD_DOWN) {
			j.stick[0].axis[1].pos = 127;
			j.stick[0].axis[1].d1 = 0;
			j.stick[0].axis[1].d2 = 1;
		}
	}

	static void fill_state(JOYSTICK_INFO& j, const XINPUT_STATE& st)
	{
		const XINPUT_GAMEPAD& g = st.Gamepad;
		WORD b = g.wButtons;

		int lx = scale_thumb(g.sThumbLX, static_cast<int>(XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE));
		int ly = scale_thumb(g.sThumbLY, static_cast<int>(XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE));
		int rx = scale_thumb(g.sThumbRX, static_cast<int>(XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE));
		int ry = scale_thumb(g.sThumbRY, static_cast<int>(XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE));

		// Invert Y so "up" is negative
		set_axis(j.stick[0].axis[0], lx);
		set_axis(j.stick[0].axis[1], -ly);
		set_axis(j.stick[1].axis[0], rx);
		set_axis(j.stick[1].axis[1], -ry);

		apply_dpad_to_left_stick(j, b);

		j.button[0].b = (b & XINPUT_GAMEPAD_A) ? 1 : 0;
		j.button[1].b = (b & XINPUT_GAMEPAD_B) ? 1 : 0;
		j.button[2].b = (b & XINPUT_GAMEPAD_X) ? 1 : 0;
		j.button[3].b = (b & XINPUT_GAMEPAD_Y) ? 1 : 0;
		j.button[4].b = (b & XINPUT_GAMEPAD_LEFT_SHOULDER) ? 1 : 0;
		j.button[5].b = (b & XINPUT_GAMEPAD_RIGHT_SHOULDER) ? 1 : 0;
		j.button[6].b = (b & XINPUT_GAMEPAD_BACK) ? 1 : 0;
		j.button[7].b = (b & XINPUT_GAMEPAD_START) ? 1 : 0;
		j.button[8].b = (b & XINPUT_GAMEPAD_LEFT_THUMB) ? 1 : 0;
		j.button[9].b = (b & XINPUT_GAMEPAD_RIGHT_THUMB) ? 1 : 0;
		j.button[10].b = (b & XINPUT_GAMEPAD_DPAD_UP) ? 1 : 0;
		j.button[11].b = (b & XINPUT_GAMEPAD_DPAD_DOWN) ? 1 : 0;
		j.button[12].b = (b & XINPUT_GAMEPAD_DPAD_LEFT) ? 1 : 0;
		j.button[13].b = (b & XINPUT_GAMEPAD_DPAD_RIGHT) ? 1 : 0;
		j.button[14].b = (g.bLeftTrigger > TRIGGER_THRESHOLD) ? 1 : 0;
		j.button[15].b = (g.bRightTrigger > TRIGGER_THRESHOLD) ? 1 : 0;
	}

	static int poll()
	{
		bool connected_now[MAX_CONTROLLERS];
		std::memset(connected_now, 0, sizeof(connected_now));

		// Query all controller slots
		for (DWORD i = 0; i < MAX_CONTROLLERS; ++i) {
			// Fix: Throttle polling disconnected slots to avoid thread starvation (stuttering)
			if (!s_connected[i] && s_offline_check_timer[i] > 0) {
				s_offline_check_timer[i]--;
				continue;
			}

			XINPUT_STATE st = {};
			DWORD res = XInputGetState(i, &st);

			if (res == ERROR_SUCCESS) {
				// Stale packet detection logic
				if (st.dwPacketNumber == s_last_packet[i]) {
					s_stale_frames[i]++;
					if (s_stale_frames[i] >= STALE_FRAME_THRESHOLD) {
						XINPUT_CAPABILITIES caps = {};
						if (XInputGetCapabilities(i, 0, &caps) != ERROR_SUCCESS) {
							connected_now[i] = false;
							s_stale_frames[i] = 0;
							s_offline_check_timer[i] = 60; // Wait 60 frames before checking again
							continue;
						}
						s_stale_frames[i] = 0;
					}
				}
				else {
					s_last_packet[i] = st.dwPacketNumber;
					s_stale_frames[i] = 0;
				}

				s_cached_states[i] = st;
				connected_now[i] = true;
				s_offline_check_timer[i] = 0; // Reset timer since connected
			}
			else {
				connected_now[i] = false;
				s_stale_frames[i] = 0;
				s_offline_check_timer[i] = 60; // Slot empty; wait 60 frames before checking again
			}
		}

		// Detect hotplug events
		for (int i = 0; i < MAX_CONTROLLERS; ++i) {
			if (connected_now[i] && !s_connected[i]) {
				LOG_INFO("XInput controller %d connected", i);
				if (s_hotplug_callback)
					s_hotplug_callback(i, true, nullptr);

				// --- Trigger a crisp "connected" buzz on the right motor ---
				XINPUT_VIBRATION vib;
				vib.wLeftMotorSpeed = 0;
				vib.wRightMotorSpeed = static_cast<WORD>(0.6f * 65535.0f); // 60% high-frequency buzz
				XInputSetState(i, &vib);
				s_rumble_timer[i] = 15; // Hold for 15 frames (~0.25 seconds)

			}
			else if (!connected_now[i] && s_connected[i]) {
				LOG_INFO("XInput controller %d disconnected", i);
				if (s_hotplug_callback)
					s_hotplug_callback(i, false, "Controller disconnected");

				// Ensure rumble shuts off if disconnected while rumbling
				s_rumble_timer[i] = 0;
			}
			s_connected[i] = connected_now[i];
		}

		// --- NEW: Process internal rumble timers ---
		for (int i = 0; i < MAX_CONTROLLERS; ++i) {
			if (s_rumble_timer[i] > 0) {
				s_rumble_timer[i]--;

				if (s_rumble_timer[i] == 0) {
					// Timer hit 0, stop the motors
					XINPUT_VIBRATION vib = { 0, 0 };
					XInputSetState(i, &vib);
				}
			}
		}

		// Rebuild sequential mapping
		for (int j = 0; j < MAX_JOYSTICKS; ++j) {
			reset_single_joystick(j);
			s_joy_to_xinput[j] = -1; // Reset mapping
		}

		int joy_count = 0;
		for (int i = 0; i < MAX_CONTROLLERS; ++i) {
			if (!connected_now[i])
				continue;

			if (joy_count >= MAX_JOYSTICKS)
				break;

			setup_descriptor(joy_count);
			fill_state(joy[joy_count], s_cached_states[i]);
			s_joy_to_xinput[joy_count] = i; // Save physical slot mapping for combos & rumble
			joy_count++;
		}

		num_joysticks = joy_count;
		return 0;
	}

	// NEW: Expose cached buttons for combo system
	static WORD get_cached_buttons(int player)
	{
		if (player < 0 || player >= MAX_JOYSTICKS) return 0;
		int x_idx = s_joy_to_xinput[player];
		if (x_idx < 0 || x_idx >= MAX_CONTROLLERS) return 0;
		return s_cached_states[x_idx].Gamepad.wButtons;
	}

	// NEW: Rumble implementation
	static bool set_rumble(int player, float left_motor, float right_motor)
	{
		if (player < 0 || player >= MAX_JOYSTICKS) return false;
		int x_idx = s_joy_to_xinput[player];
		if (x_idx < 0 || x_idx >= MAX_CONTROLLERS) return false;

		// Clamp 0.0f to 1.0f
		left_motor = left_motor < 0.0f ? 0.0f : (left_motor > 1.0f ? 1.0f : left_motor);
		right_motor = right_motor < 0.0f ? 0.0f : (right_motor > 1.0f ? 1.0f : right_motor);

		XINPUT_VIBRATION vibration;
		vibration.wLeftMotorSpeed = static_cast<WORD>(left_motor * 65535.0f);
		vibration.wRightMotorSpeed = static_cast<WORD>(right_motor * 65535.0f);

		return XInputSetState(x_idx, &vibration) == ERROR_SUCCESS;
	}

	static bool init()
	{
		reset_state();
		poll(); // Prime initial state
		return true; // Always return true to allow hotplug upgrading
	}

	static void shutdown()
	{
		// Stop any active rumble before exiting
		for (int i = 0; i < num_joysticks; ++i) {
			set_rumble(i, 0.0f, 0.0f);
		}

		reset_state();
		for (int j = 0; j < MAX_JOYSTICKS; ++j)
			reset_single_joystick(j);
		num_joysticks = 0;
	}
} // namespace xinput

//==============================================================================
// WinMM Implementation
//==============================================================================

namespace winmm {
	static constexpr int MAX_AXES = 6;
	static constexpr int JOY_POVFORWARD_WRAP = 36000;

	struct DeviceInfo {
		int caps;
		int num_axes;
		int axis[MAX_AXES];
		char* axis_name[MAX_AXES];
		int hat;
		char* hat_name;
		int num_buttons;
		int button[MAX_JOYSTICK_BUTTONS];
		char* button_name[MAX_JOYSTICK_BUTTONS];
		int device;
		int axis_min[MAX_AXES];
		int axis_max[MAX_AXES];
	};

	static DeviceInfo s_devices[MAX_JOYSTICKS];
	static int s_num_devices = 0;
	static bool s_initialized = false;

	static char name_x[] = "X";
	static char name_y[] = "Y";
	static char name_stick[] = "stick";
	static char name_throttle[] = "throttle";
	static char name_rudder[] = "rudder";
	static char name_slider[] = "slider";
	static char name_hat[] = "hat";
	static const char* name_buttons[MAX_JOYSTICK_BUTTONS] = {
		"B1",  "B2",  "B3",  "B4",  "B5",  "B6",  "B7",  "B8",
		"B9",  "B10", "B11", "B12", "B13", "B14", "B15", "B16",
		"B17", "B18", "B19", "B20", "B21", "B22", "B23", "B24",
		"B25", "B26", "B27", "B28", "B29", "B30", "B31", "B32"
	};

	static int update_joystick_status(int n, DeviceInfo* dev)
	{
		if (n >= num_joysticks)
			return -1;

		int n_stick = 0;
		int win_axis = 0;
		int max_stick;

		if (dev->caps & JOYCAPS_HASPOV)
			max_stick = joy[n].num_sticks - 1;
		else
			max_stick = joy[n].num_sticks;

		for (n_stick = 0; n_stick < max_stick; n_stick++) {
			for (int n_axis = 0; n_axis < joy[n].stick[n_stick].num_axis; n_axis++) {
				int p = dev->axis[win_axis];

				if (joy[n].stick[n_stick].flags & JOYFLAG_ANALOGUE) {
					if (joy[n].stick[n_stick].flags & JOYFLAG_SIGNED)
						joy[n].stick[n_stick].axis[n_axis].pos = p - 128;
					else
						joy[n].stick[n_stick].axis[n_axis].pos = p;
				}

				if (joy[n].stick[n_stick].flags & JOYFLAG_DIGITAL) {
					joy[n].stick[n_stick].axis[n_axis].d1 = (p < 64) ? 1 : 0;
					joy[n].stick[n_stick].axis[n_axis].d2 = (p > 192) ? 1 : 0;
				}

				win_axis++;
			}
		}

		if (dev->caps & JOYCAPS_HASPOV) {
			joy[n].stick[n_stick].axis[0].pos = 0;
			joy[n].stick[n_stick].axis[1].pos = 0;

			// Left
			if ((dev->hat > JOY_POVBACKWARD) && (dev->hat < JOY_POVFORWARD_WRAP)) {
				joy[n].stick[n_stick].axis[0].d1 = 1;
				joy[n].stick[n_stick].axis[0].pos = -128;
			}
			else {
				joy[n].stick[n_stick].axis[0].d1 = 0;
			}

			// Right
			if ((dev->hat > JOY_POVFORWARD) && (dev->hat < JOY_POVBACKWARD)) {
				joy[n].stick[n_stick].axis[0].d2 = 1;
				joy[n].stick[n_stick].axis[0].pos = 128;
			}
			else {
				joy[n].stick[n_stick].axis[0].d2 = 0;
			}

			// Forward (up)
			if (((dev->hat > JOY_POVLEFT) && (dev->hat <= JOY_POVFORWARD_WRAP)) ||
				((dev->hat >= JOY_POVFORWARD) && (dev->hat < JOY_POVRIGHT))) {
				joy[n].stick[n_stick].axis[1].d1 = 1;
				joy[n].stick[n_stick].axis[1].pos = -128;
			}
			else {
				joy[n].stick[n_stick].axis[1].d1 = 0;
			}

			// Backward (down)
			if ((dev->hat > JOY_POVRIGHT) && (dev->hat < JOY_POVLEFT)) {
				joy[n].stick[n_stick].axis[1].d2 = 1;
				joy[n].stick[n_stick].axis[1].pos = 128;
			}
			else {
				joy[n].stick[n_stick].axis[1].d2 = 0;
			}
		}

		for (int n_but = 0; n_but < dev->num_buttons; n_but++)
			joy[n].button[n_but].b = dev->button[n_but];

		return 0;
	}

	static int add_joystick(DeviceInfo* dev)
	{
		if (num_joysticks >= MAX_JOYSTICKS - 1)
			return -1;

		joy[num_joysticks].flags = JOYFLAG_ANALOGUE | JOYFLAG_DIGITAL;

		int n_stick = 0;
		int win_axis = 0;

		if (dev->num_axes > 0) {
			if (dev->num_axes > 1) {
				joy[num_joysticks].stick[n_stick].flags = JOYFLAG_DIGITAL | JOYFLAG_ANALOGUE | JOYFLAG_SIGNED;
				joy[num_joysticks].stick[n_stick].axis[0].name = dev->axis_name[0] ? dev->axis_name[0] : name_x;
				joy[num_joysticks].stick[n_stick].axis[1].name = dev->axis_name[1] ? dev->axis_name[1] : name_y;
				joy[num_joysticks].stick[n_stick].name = name_stick;

				if (dev->caps & JOYCAPS_HASZ) {
					joy[num_joysticks].stick[n_stick].num_axis = 3;
					joy[num_joysticks].stick[n_stick].axis[2].name = dev->axis_name[2] ? dev->axis_name[2] : name_throttle;
					win_axis += 3;
				}
				else {
					joy[num_joysticks].stick[n_stick].num_axis = 2;
					win_axis += 2;
				}
				n_stick++;
			}

			if (dev->caps & JOYCAPS_HASR) {
				joy[num_joysticks].stick[n_stick].flags = JOYFLAG_DIGITAL | JOYFLAG_ANALOGUE | JOYFLAG_UNSIGNED;
				joy[num_joysticks].stick[n_stick].num_axis = 1;
				joy[num_joysticks].stick[n_stick].axis[0].name = "";
				joy[num_joysticks].stick[n_stick].name = dev->axis_name[win_axis] ? dev->axis_name[win_axis] : name_rudder;
				win_axis++;
				n_stick++;
			}

			int max_stick = (dev->caps & JOYCAPS_HASPOV) ? MAX_JOYSTICK_STICKS - 1 : MAX_JOYSTICK_STICKS;

			while ((win_axis < dev->num_axes) && (n_stick < max_stick)) {
				joy[num_joysticks].stick[n_stick].flags = JOYFLAG_DIGITAL | JOYFLAG_ANALOGUE | JOYFLAG_UNSIGNED;
				joy[num_joysticks].stick[n_stick].num_axis = 1;
				joy[num_joysticks].stick[n_stick].axis[0].name = "";
				joy[num_joysticks].stick[n_stick].name = dev->axis_name[win_axis] ? dev->axis_name[win_axis] : name_slider;
				win_axis++;
				n_stick++;
			}

			if (dev->caps & JOYCAPS_HASPOV) {
				joy[num_joysticks].stick[n_stick].flags = JOYFLAG_DIGITAL | JOYFLAG_SIGNED;
				joy[num_joysticks].stick[n_stick].num_axis = 2;
				joy[num_joysticks].stick[n_stick].axis[0].name = "left/right";
				joy[num_joysticks].stick[n_stick].axis[1].name = "up/down";
				joy[num_joysticks].stick[n_stick].name = dev->hat_name ? dev->hat_name : name_hat;
				n_stick++;
			}
		}

		joy[num_joysticks].num_sticks = n_stick;
		joy[num_joysticks].num_buttons = dev->num_buttons;

		for (int n_but = 0; n_but < joy[num_joysticks].num_buttons; n_but++)
			joy[num_joysticks].button[n_but].name = dev->button_name[n_but] ? dev->button_name[n_but] : name_buttons[n_but];

		num_joysticks++;
		return 0;
	}

	static int poll()
	{
		for (int n_joy = 0; n_joy < s_num_devices; n_joy++) {
			JOYINFOEX js;
			js.dwSize = sizeof(js);
			js.dwFlags = JOY_RETURNALL;

			if (joyGetPosEx(s_devices[n_joy].device, &js) == JOYERR_NOERROR) {
				s_devices[n_joy].axis[0] = js.dwXpos;
				s_devices[n_joy].axis[1] = js.dwYpos;
				int n_axis = 2;

				if (s_devices[n_joy].caps & JOYCAPS_HASZ) s_devices[n_joy].axis[n_axis++] = js.dwZpos;
				if (s_devices[n_joy].caps & JOYCAPS_HASR) s_devices[n_joy].axis[n_axis++] = js.dwRpos;
				if (s_devices[n_joy].caps & JOYCAPS_HASU) s_devices[n_joy].axis[n_axis++] = js.dwUpos;
				if (s_devices[n_joy].caps & JOYCAPS_HASV) s_devices[n_joy].axis[n_axis++] = js.dwVpos;

				for (n_axis = 0; n_axis < s_devices[n_joy].num_axes; n_axis++) {
					int p = s_devices[n_joy].axis[n_axis] - s_devices[n_joy].axis_min[n_axis];
					int range = s_devices[n_joy].axis_max[n_axis] - s_devices[n_joy].axis_min[n_axis];
					if (range > 0)
						s_devices[n_joy].axis[n_axis] = p * 256 / range;
					else
						s_devices[n_joy].axis[n_axis] = 0;
				}

				if (s_devices[n_joy].caps & JOYCAPS_HASPOV)
					s_devices[n_joy].hat = js.dwPOV;

				for (int n_but = 0; n_but < s_devices[n_joy].num_buttons; n_but++)
					s_devices[n_joy].button[n_but] = ((js.dwButtons & (1 << n_but)) != 0);
			}
			else {
				for (int n_axis = 0; n_axis < s_devices[n_joy].num_axes; n_axis++)
					s_devices[n_joy].axis[n_axis] = 0;
				if (s_devices[n_joy].caps & JOYCAPS_HASPOV)
					s_devices[n_joy].hat = 0;
				for (int n_but = 0; n_but < s_devices[n_joy].num_buttons; n_but++)
					s_devices[n_joy].button[n_but] = 0;
			}
			update_joystick_status(n_joy, &s_devices[n_joy]);
		}
		return 0;
	}

	static bool init()
	{
		if (s_initialized) return true;

		int max_devs = joyGetNumDevs();
		LOG_INFO("WinMM reports %d joystick device slot(s)", max_devs);

		s_num_devices = 0;

		for (int n_dev = 0; n_dev < max_devs; n_dev++) {
			if (s_num_devices >= MAX_JOYSTICKS) break;

			JOYCAPS caps;
			if (joyGetDevCaps(n_dev, &caps, sizeof(caps)) != JOYERR_NOERROR) continue;

			JOYINFOEX js;
			js.dwSize = sizeof(js);
			js.dwFlags = JOY_RETURNALL;
			if (joyGetPosEx(n_dev, &js) == JOYERR_UNPLUGGED) continue;

			LOG_INFO("Detected WinMM joystick %d: %s", n_dev, caps.szPname);

			std::memset(&s_devices[s_num_devices], 0, sizeof(DeviceInfo));
			s_devices[s_num_devices].device = n_dev;
			s_devices[s_num_devices].caps = caps.wCaps;
			s_devices[s_num_devices].num_buttons = MIN((int)caps.wNumButtons, MAX_JOYSTICK_BUTTONS);
			s_devices[s_num_devices].num_axes = MIN((int)caps.wNumAxes, MAX_AXES);

			s_devices[s_num_devices].axis_min[0] = caps.wXmin;
			s_devices[s_num_devices].axis_max[0] = caps.wXmax;
			s_devices[s_num_devices].axis_min[1] = caps.wYmin;
			s_devices[s_num_devices].axis_max[1] = caps.wYmax;
			int n_axis = 2;

			if (caps.wCaps & JOYCAPS_HASZ) {
				s_devices[s_num_devices].axis_min[n_axis] = caps.wZmin;
				s_devices[s_num_devices].axis_max[n_axis] = caps.wZmax;
				n_axis++;
			}
			if (caps.wCaps & JOYCAPS_HASR) {
				s_devices[s_num_devices].axis_min[n_axis] = caps.wRmin;
				s_devices[s_num_devices].axis_max[n_axis] = caps.wRmax;
				n_axis++;
			}
			if (caps.wCaps & JOYCAPS_HASU) {
				s_devices[s_num_devices].axis_min[n_axis] = caps.wUmin;
				s_devices[s_num_devices].axis_max[n_axis] = caps.wUmax;
				n_axis++;
			}
			if (caps.wCaps & JOYCAPS_HASV) {
				s_devices[s_num_devices].axis_min[n_axis] = caps.wVmin;
				s_devices[s_num_devices].axis_max[n_axis] = caps.wVmax;
				n_axis++;
			}

			if (add_joystick(&s_devices[s_num_devices]) != 0) {
				LOG_ERROR("Failed to register joystick %d (%s)", n_dev, caps.szPname);
				continue;
			}

			LOG_INFO("Joystick %d registered: %d button(s), %d axis/axes",
				n_dev, s_devices[s_num_devices].num_buttons, s_devices[s_num_devices].num_axes);

			s_num_devices++;
		}

		if (s_num_devices == 0) {
			LOG_INFO("No WinMM joysticks detected");
			return false;
		}

		s_initialized = true;
		LOG_INFO("WinMM joystick initialization complete: %d device(s)", s_num_devices);
		poll();
		return true;
	}

	static void shutdown()
	{
		s_num_devices = 0;
		s_initialized = false;
	}
} // namespace winmm

//==============================================================================
// Public API Implementation
//==============================================================================

int install_joystick()
{
	if (_joystick_installed)
		return 0;

	clear_all_joystick_state();
	s_active_driver = JoystickDriver::None;

	LOG_INFO("Initializing joystick system...");

	if (xinput::is_available()) {
		if (xinput::init()) {
			s_active_driver = JoystickDriver::XInput;
			LOG_INFO("Using XInput joystick driver (supports hotplug)");
		}
	}

	if (s_active_driver == JoystickDriver::None) {
		if (winmm::init()) {
			s_active_driver = JoystickDriver::WinMM;
			LOG_INFO("Using WinMM joystick driver");
		}
		else {
			// Default to XInput passive mode if no WinMM found
			LOG_INFO("No joysticks found. Defaulting to XInput for hotplug detection.");
			xinput::init();
			s_active_driver = JoystickDriver::XInput;
		}
	}

	_joystick_installed = 1;
	return 0;
}

void remove_joystick()
{
	if (!_joystick_installed)
		return;

	switch (s_active_driver) {
	case JoystickDriver::XInput:
		xinput::shutdown();
		break;
	case JoystickDriver::WinMM:
		winmm::shutdown();
		break;
	default:
		break;
	}

	clear_all_joystick_state();
	s_active_driver = JoystickDriver::None;
	_joystick_installed = 0;
}



bool joystick_check_combo(int player, WORD buttonMask)
{
	if (!joystick_using_xinput()) return false;
	if (player < 0 || player >= MAX_JOYSTICKS) return false;

	// Fix: Read from cached state instead of invoking XInputGetState again.
	// This fixes a bug where `player` (joy index) was wrongly used as the physical XInput slot!
	WORD buttons = xinput::get_cached_buttons(player);
	bool comboHeld = (buttons & buttonMask) == buttonMask;

	int idx = get_combo_index(buttonMask);

	if (comboHeld)
		s_comboHoldFrames[player][idx]++;
	else
		s_comboHoldFrames[player][idx] = 0;

	// Trigger exactly once when hold count reaches the threshold
	bool triggered = (s_comboHoldFrames[player][idx] == COMBO_CONFIRM_FRAMES);

	s_comboWasHeld[player][idx] = comboHeld;
	return triggered;
}

int poll_joystick()
{
	if (!_joystick_installed)
		return -1;

	switch (s_active_driver) {
	case JoystickDriver::XInput:
		return xinput::poll();

	case JoystickDriver::WinMM:
		return winmm::poll();

	default:
		return -1;
	}
}

void set_joystick_hotplug_callback(JoystickHotplugCallback callback)
{
	s_hotplug_callback = callback;
}

bool joystick_using_xinput()
{
	return s_active_driver == JoystickDriver::XInput;
}

const char* joystick_driver_name()
{
	switch (s_active_driver) {
	case JoystickDriver::XInput: return "XInput";
	case JoystickDriver::WinMM:  return "WinMM";
	default:                     return "None";
	}
}

bool joystick_any_connected()
{
	return num_joysticks > 0;
}


//------------------------------------------------------------------------------
// Rumble Implementation
//------------------------------------------------------------------------------

bool joystick_set_rumble(int player, float left_motor_speed, float right_motor_speed)
{
	if (!_joystick_installed || s_active_driver != JoystickDriver::XInput)
		return false;

	return xinput::set_rumble(player, left_motor_speed, right_motor_speed);
}

void joystick_stop_rumble(int player)
{
	joystick_set_rumble(player, 0.0f, 0.0f);
}