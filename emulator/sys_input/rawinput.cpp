//Note: Updated bad mouse handling code. 8/5/25
// Updated 2/28/26 to support Lost Focus and Pausing Input.

#include "rawinput.h"
#include "sys_log.h"
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

/* Forces RAWINPUTDEVICE and related Win32 APIs to be visible.
 * Only compatible with WIndows XP and above. */
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0501

 // GLFW-style modifier flags -- use the RI_MOD_* values from the header
 // to keep internal and public modifier naming consistent.
 // (RI_MOD_SHIFT=0x01, RI_MOD_CONTROL=0x02, RI_MOD_ALT=0x04, RI_MOD_SUPER=0x08)

HWND windowHandle;
unsigned char key[256];
unsigned int lastkey[256];
int mouse_b;

// This defaults to 1x for AAE - To be removed..
static float g_mouseScale = 1.0f;

// --------------------
// Thread management
// --------------------
static std::thread inputThread;
static std::mutex inputQueueMutex;
static std::condition_variable inputCV;
static std::atomic<bool> inputThreadExit{ false };
static std::atomic<bool> inputThreadRun{ false };
static std::queue<RAWINPUT> inputQueue;

// --------------------
// Forward declarations
// --------------------
static void RawInput_ProcessInternal(const RAWINPUT& input);
static void input_thread_func();

// -----------------------------------------------------------------------------
// set_mouse_mickey_scale
// Description:
//   Sets the scaling multiplier applied to relative mouse motion (mickeys).
// -----------------------------------------------------------------------------
void set_mouse_mickey_scale(float scale) {
	g_mouseScale = scale;
}

struct DXTI_MOUSE_STATE
{
	long x, y, wheel; //current position
	long dx, dy, dwheel; //change in position
	bool left, middle, right; //buttons
};

enum DXTI_MOUSE_BUTTON_STATE //named state of mouse buttons
{
	UP = FALSE,
	DOWN = TRUE,
};
struct DXTI_MOUSE_STATE m_mouseStateRaw;

// GLFW style callbacks
static MouseButtonCallback g_mouseButtonCallback = nullptr;
static CursorPositionCallback g_cursorPositionCallback = nullptr;
static KeyCallback g_keyCallback = nullptr;

static std::atomic<bool> isInputPaused{ false };

// Track whether we have been initialized (thread is running)
static std::atomic<bool> s_initialized{ false };

// -----------------------------------------------------------------------------
// RawInput_SetPaused
// Description:
//  Pauses raw input processing and flushes current states so keys/buttons
//  don't get stuck down when losing focus.
// -----------------------------------------------------------------------------
void RawInput_SetPaused(bool paused) {
	bool wasPaused = isInputPaused.exchange(paused);

	if (paused && !wasPaused) {
		std::lock_guard<std::mutex> lock(inputQueueMutex);

		// Clear any queued raw inputs
		std::queue<RAWINPUT> emptyQueue;
		std::swap(inputQueue, emptyQueue);

		// Clear keyboard states to prevent stuck keys
		SecureZeroMemory(key, sizeof(key));
		SecureZeroMemory(lastkey, sizeof(lastkey));

		// Clear mouse button states
		mouse_b = 0;
		m_mouseStateRaw.left = UP;
		m_mouseStateRaw.right = UP;
		m_mouseStateRaw.middle = UP;

		// Clear relative motion deltas so the camera doesn't jump on return
		m_mouseStateRaw.dx = 0;
		m_mouseStateRaw.dy = 0;
		m_mouseStateRaw.dwheel = 0;
	}
}

// -----------------------------------------------------------------------------
// GetModifierFlags
// Description:
//   Returns a bitmask of currently-held modifier keys via GetAsyncKeyState().
//   Uses RI_MOD_SHIFT/CONTROL/ALT/SUPER flags.
// -----------------------------------------------------------------------------
int GetModifierFlags()
{
	int mods = 0;
	if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
		mods |= RI_MOD_SHIFT;
	if (GetAsyncKeyState(VK_CONTROL) & 0x8000)
		mods |= RI_MOD_CONTROL;
	if (GetAsyncKeyState(VK_MENU) & 0x8000)
		mods |= RI_MOD_ALT;
	if ((GetAsyncKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_RWIN) & 0x8000))
		mods |= RI_MOD_SUPER;
	return mods;
}

// -----------------------------------------------------------------------------
// SetKeyCallback
// Description:
//   Registers a callback function for keyboard key events.
// -----------------------------------------------------------------------------
void SetKeyCallback(KeyCallback callback) {
	g_keyCallback = callback;
}

// -----------------------------------------------------------------------------
// RawInput_Initialize
// Description:
//   Initializes raw input devices (keyboard and mouse) for the given window.
// -----------------------------------------------------------------------------
HRESULT RawInput_Initialize(HWND hWnd)
{
	// Guard against double-init: shut down previous instance first
	if (s_initialized.load()) {
		LOG_INFO("RawInput_Initialize: already initialized, shutting down previous instance");
		RawInput_Shutdown();
	}

	RAWINPUTDEVICE Rid[2]{};

	Rid[0].usUsagePage = 0x01;
	Rid[0].usUsage = 0x02;
	Rid[0].dwFlags = RIDEV_INPUTSINK;			//RIDEV_NOLEGACY | RIDEV_CAPTUREMOUSE | RIDEV_INPUTSINK;
	Rid[0].hwndTarget = hWnd;

	Rid[1].usUsagePage = 0x01;
	Rid[1].usUsage = 0x06;
	Rid[1].dwFlags = RIDEV_INPUTSINK;
	Rid[1].hwndTarget = hWnd;

	ZeroMemory(key, sizeof(key));
	ZeroMemory(lastkey, sizeof(lastkey));
	ZeroMemory(&m_mouseStateRaw, sizeof(m_mouseStateRaw));

	windowHandle = hWnd;
	if (FALSE == RegisterRawInputDevices(Rid, 2, sizeof(Rid[0]))) //registers both mouse and keyboard
		return E_FAIL;

	inputThreadExit = false;
	inputThreadRun = false;
	inputThread = std::thread(input_thread_func);
	s_initialized = true;
	LOG_INFO("RawInput thread: started");
	return S_OK;
}

// -----------------------------------------------------------------------------
// RawInput_ProcessInput - now only enqueues input and signals worker
// -----------------------------------------------------------------------------
LRESULT RawInput_ProcessInput(HWND hWnd, WPARAM wParam, LPARAM lParam) {
	// If input is paused, discard the event to Windows Default Proc
	if (isInputPaused.load()) {
		return DefWindowProc(hWnd, WM_INPUT, wParam, lParam);
	}

	RAWINPUT input;
	UINT size = sizeof(input);
	GetRawInputData((HRAWINPUT)lParam, RID_INPUT, &input, &size, sizeof(RAWINPUTHEADER));

	{
		std::lock_guard<std::mutex> lock(inputQueueMutex);
		inputQueue.push(input);
		inputThreadRun = true;
	}
	inputCV.notify_one();

	return DefWindowProc(hWnd, WM_INPUT, wParam, lParam);
}

// -----------------------------------------------------------------------------
// Input Thread Function - processes queued events in batches
// -----------------------------------------------------------------------------
static void input_thread_func() {
	std::unique_lock<std::mutex> lock(inputQueueMutex);
	while (!inputThreadExit) {
		inputCV.wait(lock, [] { return inputThreadExit || inputThreadRun.load(); });
		if (inputThreadExit) break;

		// Swap queue to local for batch processing
		std::queue<RAWINPUT> localQueue;
		std::swap(localQueue, inputQueue);
		inputThreadRun = false;
		lock.unlock();

		while (!localQueue.empty()) {
			RawInput_ProcessInternal(localQueue.front());
			localQueue.pop();
		}

		lock.lock();
	}
	LOG_INFO("RawInput thread: exiting");
}

// -----------------------------------------------------------------------------
// Shutdown helper
// -----------------------------------------------------------------------------
void RawInput_Shutdown() {
	if (!s_initialized.load()) return;

	{
		std::lock_guard<std::mutex> lock(inputQueueMutex);
		inputThreadExit = true;
	}
	inputCV.notify_one();
	if (inputThread.joinable()) inputThread.join();
	s_initialized = false;
	LOG_INFO("RawInput_Shutdown: complete");
}

// -----------------------------------------------------------------------------
// RawInput_ProcessInput
// Description:
//   Processes WM_INPUT messages and updates internal key and mouse state.
// -----------------------------------------------------------------------------
//LRESULT RawInput_ProcessInput(HWND hWnd, WPARAM wParam, LPARAM lParam)
static void RawInput_ProcessInternal(const RAWINPUT& input)
{
	if (input.header.dwType == RIM_TYPEKEYBOARD) {
		const auto& kbd = input.data.keyboard;
		UINT virtualKey = kbd.VKey;
		UINT scanCode = kbd.MakeCode;
		UINT flags = kbd.Flags;

		if (virtualKey == 255) return;

		if (virtualKey == VK_SHIFT)
			virtualKey = MapVirtualKey(scanCode, MAPVK_VSC_TO_VK_EX);
		else if (virtualKey == VK_NUMLOCK)
			scanCode |= 0x100;

		// e0 and e1 are escape sequences used for certain special keys, such as PRINT and PAUSE/BREAK.
		// see http://www.win.tue.nl/~aeb/linux/kbd/scancodes-1.html
		const bool isE0 = (flags & RI_KEY_E0);
		const bool isE1 = (flags & RI_KEY_E1);

		if (isE1 && virtualKey == VK_PAUSE)
			scanCode = 0x45;
		else if (isE1)
			scanCode = MapVirtualKey(virtualKey, MAPVK_VK_TO_VSC);

		switch (virtualKey) {
		case VK_CONTROL: virtualKey = isE0 ? VK_RCONTROL : VK_LCONTROL; break;
		case VK_MENU: virtualKey = isE0 ? VK_RMENU : VK_LMENU; break;
		case VK_RETURN: if (isE0) virtualKey = VK_SEPARATOR; break;
		case VK_INSERT: if (!isE0) virtualKey = VK_NUMPAD0; break;
		case VK_DELETE: if (!isE0) virtualKey = VK_DECIMAL; break;
		case VK_HOME: if (!isE0) virtualKey = VK_NUMPAD7; break;
		case VK_END: if (!isE0) virtualKey = VK_NUMPAD1; break;
		case VK_PRIOR: if (!isE0) virtualKey = VK_NUMPAD9; break;
		case VK_NEXT: if (!isE0) virtualKey = VK_NUMPAD3; break;
		case VK_LEFT: if (!isE0) virtualKey = VK_NUMPAD4; break;
		case VK_RIGHT: if (!isE0) virtualKey = VK_NUMPAD6; break;
		case VK_UP: if (!isE0) virtualKey = VK_NUMPAD8; break;
		case VK_DOWN: if (!isE0) virtualKey = VK_NUMPAD2; break;
		case VK_CLEAR: if (!isE0) virtualKey = VK_NUMPAD5; break;
		}

		if (kbd.Flags & RI_KEY_BREAK) {
			key[virtualKey] = 0;
			lastkey[virtualKey] = 0;
		}
		else {
			key[virtualKey] = 1;
			lastkey[virtualKey] = (lastkey[virtualKey] + 1) % 0xFFFFFFFF;
			if (lastkey[virtualKey] == 0) lastkey[virtualKey] = 1;
		}

		if (g_keyCallback) {
			int mods = GetModifierFlags();
			int action = (kbd.Flags & RI_KEY_BREAK) ? 0 : 1;
			g_keyCallback((int)virtualKey, (int)scanCode, action, mods);
		}
	}
	else if (input.header.dwType == RIM_TYPEMOUSE)
	{
		int mods = GetModifierFlags();

		//
		// Explicitly accumulate all WM_INPUT deltas per frame.
		m_mouseStateRaw.dx += input.data.mouse.lLastX;
		m_mouseStateRaw.dy += input.data.mouse.lLastY;

		m_mouseStateRaw.x += m_mouseStateRaw.dx;
		m_mouseStateRaw.y += m_mouseStateRaw.dy;

		if (g_cursorPositionCallback)
			g_cursorPositionCallback((double)m_mouseStateRaw.x, (double)m_mouseStateRaw.y);

		// usButtonFlags is a bitmask -- multiple flags can be set in one message.
		// Use if-chains instead of switch to handle simultaneous button events.
		USHORT btnFlags = input.data.mouse.usButtonFlags;

		if (btnFlags & RI_MOUSE_LEFT_BUTTON_DOWN) {
			m_mouseStateRaw.left = DOWN;
			if (g_mouseButtonCallback) g_mouseButtonCallback(0, 1, mods);
		}
		if (btnFlags & RI_MOUSE_LEFT_BUTTON_UP) {
			m_mouseStateRaw.left = UP;
			if (g_mouseButtonCallback) g_mouseButtonCallback(0, 0, mods);
		}

		if (btnFlags & RI_MOUSE_RIGHT_BUTTON_DOWN) {
			m_mouseStateRaw.right = DOWN;
			if (g_mouseButtonCallback) g_mouseButtonCallback(1, 1, mods);
		}
		if (btnFlags & RI_MOUSE_RIGHT_BUTTON_UP) {
			m_mouseStateRaw.right = UP;
			if (g_mouseButtonCallback) g_mouseButtonCallback(1, 0, mods);
		}

		if (btnFlags & RI_MOUSE_MIDDLE_BUTTON_DOWN) {
			m_mouseStateRaw.middle = DOWN;
			if (g_mouseButtonCallback) g_mouseButtonCallback(2, 1, mods);
		}
		if (btnFlags & RI_MOUSE_MIDDLE_BUTTON_UP) {
			m_mouseStateRaw.middle = UP;
			if (g_mouseButtonCallback) g_mouseButtonCallback(2, 0, mods);
		}

		if (btnFlags & RI_MOUSE_WHEEL) {
			m_mouseStateRaw.dwheel += input.data.mouse.usButtonData;
		}

		// Allegro Mouse button support.
		if (m_mouseStateRaw.left)   bset(mouse_b, 0x01); else  bclr(mouse_b, 0x01);
		if (m_mouseStateRaw.right)  bset(mouse_b, 0x02); else  bclr(mouse_b, 0x02);
		if (m_mouseStateRaw.middle) bset(mouse_b, 0x04); else  bclr(mouse_b, 0x04);
	}
}

// -----------------------------------------------------------------------------
// SetMouseButtonCallback
// Description:
//   Registers a callback function for mouse button events.
// -----------------------------------------------------------------------------
void SetMouseButtonCallback(MouseButtonCallback callback) {
	g_mouseButtonCallback = callback;
}

// -----------------------------------------------------------------------------
// SetCursorPositionCallback
// Description:
//   Registers a callback function for mouse cursor position changes.
// -----------------------------------------------------------------------------
void SetCursorPositionCallback(CursorPositionCallback callback) {
	g_cursorPositionCallback = callback;
}

// -----------------------------------------------------------------------------
// test_clr
// Description:
//   Clears internal input state buffers.
// -----------------------------------------------------------------------------
void test_clr()
{
	SecureZeroMemory(key, sizeof(key));
	SecureZeroMemory(lastkey, sizeof(lastkey));
}

// Function to get the window size
// -----------------------------------------------------------------------------
// getWindowSize
// Description:
//   Retrieves the current size of the client area of the window.
// -----------------------------------------------------------------------------
void getWindowSize(int* width, int* height) {
	RECT rect;
	GetClientRect(windowHandle, &rect);
	*width = rect.right - rect.left;
	*height = rect.bottom - rect.top;
}

// -----------------------------------------------------------------------------
// get_mouse_win
// Description:
//   Retrieves the current mouse position in window (client) coordinates.
// -----------------------------------------------------------------------------
void get_mouse_win(int* mickeyx, int* mickeyy)
{
	POINT cursor_pos;
	GetCursorPos(&cursor_pos);
	ScreenToClient(windowHandle, (LPPOINT)&cursor_pos);
	*mickeyx = cursor_pos.x;
	*mickeyy = cursor_pos.y;
}

// Your updated function
// -----------------------------------------------------------------------------
// get_mouse_mickeys
// Description:
//   Retrieves and resets relative mouse movement, scaled by the mouse mickey scale.
// -----------------------------------------------------------------------------
void get_mouse_mickeys(int* mickeyx, int* mickeyy)
{
	int temp_x = m_mouseStateRaw.dx;
	int temp_y = m_mouseStateRaw.dy;
	m_mouseStateRaw.dx = 0;
	m_mouseStateRaw.dy = 0;
	*mickeyx = static_cast<int>(temp_x * g_mouseScale);
	*mickeyy = static_cast<int>(temp_y * g_mouseScale);
}
//keyboard state checks
// -----------------------------------------------------------------------------
// isKeyHeld
// Description:
//   Returns non-zero if the given key has been pressed (held count).
// -----------------------------------------------------------------------------
int isKeyHeld(INT vkCode) { return lastkey[vkCode]; }
// -----------------------------------------------------------------------------
// IsKeyDown
// Description:
//   Returns true if the specified key is currently pressed.
// -----------------------------------------------------------------------------
bool IsKeyDown(INT vkCode) { return key[vkCode & 0xff] ? true : false; }
// -----------------------------------------------------------------------------
// IsKeyUp
// Description:
//   Returns true if the specified key is currently released.
// -----------------------------------------------------------------------------
bool IsKeyUp(INT vkCode) { return key[vkCode & 0xff] ? false : true; }

//summed mouse state checks/sets;
//use as convenience, ie. keeping track of movements without needing to maintain separate data set
//naming is left to C style for compatibility

// -----------------------------------------------------------------------------
// GetMouseX
// Description:
//   Returns the absolute X position of the mouse.
// -----------------------------------------------------------------------------
LONG GetMouseX() { return m_mouseStateRaw.x; }
// -----------------------------------------------------------------------------
// GetMouseY
// Description:
//   Returns the absolute Y position of the mouse.
// -----------------------------------------------------------------------------
LONG GetMouseY() { return m_mouseStateRaw.y; }
// -----------------------------------------------------------------------------
// GetMouseWheel
// Description:
//   Returns the absolute scroll wheel value.
// -----------------------------------------------------------------------------
LONG GetMouseWheel() { return m_mouseStateRaw.wheel; }
// -----------------------------------------------------------------------------
// SetMouseX
// Description:
//   Sets the internal X position of the mouse.
// -----------------------------------------------------------------------------
void SetMouseX(LONG x) { m_mouseStateRaw.x = x; }
// -----------------------------------------------------------------------------
// SetMouseY
// Description:
//   Sets the internal Y position of the mouse.
// -----------------------------------------------------------------------------
void SetMouseY(LONG y) { m_mouseStateRaw.y = y; }
// -----------------------------------------------------------------------------
// SetMouseWheel
// Description:
//   Sets the internal scroll wheel value.
// -----------------------------------------------------------------------------
void SetMouseWheel(LONG wheel) { m_mouseStateRaw.wheel = wheel; }

//relative mouse state changes
// -----------------------------------------------------------------------------
// GetMouseXChange
// Description:
//   Returns the change in mouse X position since last reset.
// -----------------------------------------------------------------------------
LONG GetMouseXChange() { return m_mouseStateRaw.dx; }
// -----------------------------------------------------------------------------
// GetMouseYChange
// Description:
//   Returns the change in mouse Y position since last reset.
// -----------------------------------------------------------------------------
LONG GetMouseYChange() { return m_mouseStateRaw.dy; }
// -----------------------------------------------------------------------------
// GetMouseWheelChange
// Description:
//   Returns the change in scroll wheel value since last reset.
// -----------------------------------------------------------------------------
LONG GetMouseWheelChange() { return m_mouseStateRaw.dwheel; }

//mouse button state checks
// -----------------------------------------------------------------------------
// IsMouseLButtonDown
// Description:
//   Returns true if the left mouse button is currently down.
// -----------------------------------------------------------------------------
bool IsMouseLButtonDown() { return (m_mouseStateRaw.left == DOWN) ? TRUE : FALSE; }
// -----------------------------------------------------------------------------
// IsMouseLButtonUp
// Description:
//   Returns true if the left mouse button is currently up.
// -----------------------------------------------------------------------------
bool IsMouseLButtonUp() { return (m_mouseStateRaw.left == UP) ? TRUE : FALSE; }
// -----------------------------------------------------------------------------
// IsMouseRButtonDown
// Description:
//   Returns true if the right mouse button is currently down.
// -----------------------------------------------------------------------------
bool IsMouseRButtonDown() { return (m_mouseStateRaw.right == DOWN) ? TRUE : FALSE; }
// -----------------------------------------------------------------------------
// IsMouseRButtonUp
// Description:
//   Returns true if the right mouse button is currently up.
// -----------------------------------------------------------------------------
bool IsMouseRButtonUp() { return (m_mouseStateRaw.right == UP) ? TRUE : FALSE; }
// -----------------------------------------------------------------------------
// IsMouseMButtonDown
// Description:
//   Returns true if the middle mouse button is currently down.
// -----------------------------------------------------------------------------
bool IsMouseMButtonDown() { return (m_mouseStateRaw.middle == DOWN) ? TRUE : FALSE; }
// -----------------------------------------------------------------------------
// IsMouseMButtonUp
// Description:
//   Returns true if the middle mouse button is currently up.
// -----------------------------------------------------------------------------
bool IsMouseMButtonUp() { return (m_mouseStateRaw.middle == UP) ? TRUE : FALSE; }