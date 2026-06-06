#include <Windows.h>
#include "framework.h"
#include "sys_log.h"

BOOL Calculate(int cx, int cy, RECT& rect)
{
	HWND hWnd = win_get_window();

	if (hWnd == NULL)
		return FALSE;

	HDC hDC = ::GetDC(NULL);
	const int w = GetDeviceCaps(hDC, HORZRES);
	const int h = GetDeviceCaps(hDC, VERTRES);
	::ReleaseDC(NULL, hDC);

	RECT rcWindow;
	GetWindowRect(hWnd, &rcWindow);
	RECT rcClient;
	GetClientRect(hWnd, &rcClient);
	cx += (rcWindow.right - rcWindow.left) - rcClient.right;
	cy += (rcWindow.bottom - rcWindow.top) - rcClient.bottom;

	rect.left = (w >> 1) - (cx >> 1);
	rect.top = (h >> 1) - (cy >> 1);
	rect.right = rect.left + cx;
	rect.bottom = rect.top + cy;

	return TRUE;
}

void ClientResize(int nWidth, int nHeight)
{
	HWND hWnd = win_get_window();
	RECT rcClient, rcWind;
	POINT ptDiff;
	GetClientRect(hWnd, &rcClient);
	GetWindowRect(hWnd, &rcWind);
	ptDiff.x = (rcWind.right - rcWind.left) - rcClient.right;
	ptDiff.y = (rcWind.bottom - rcWind.top) - rcClient.bottom;
	MoveWindow(hWnd, rcWind.left, rcWind.top, nWidth + ptDiff.x, nHeight + ptDiff.y, TRUE);
}

void center_window()
{
	//   long lWS;
	//    long lWSEX;

	HWND wnd = win_get_window();
	HWND desktop = GetDesktopWindow();
	RECT wndRect, desktopRect;
	int  w, h, dw, dh;

	GetWindowRect(wnd, &wndRect);
	GetWindowRect(desktop, &desktopRect);
	w = wndRect.right - wndRect.left;
	h = wndRect.bottom - wndRect.top;
	dw = desktopRect.right - desktopRect.left;
	dh = desktopRect.bottom - desktopRect.top;

	MoveWindow(wnd, (dw - w) / 2, (dh - h) / 2, w, h, TRUE);
}

void SetTopMost(const bool TopMost)
{
	//ASSERT(::IsWindow(hWnd));
	HWND hWndInsertAfter = (TopMost ? HWND_TOPMOST : HWND_NOTOPMOST);
	SetWindowPos(win_get_window(), hWndInsertAfter, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	//SetWindowPos(hWnd, HWND_TOP, 0, 0, 1920, 1080, SWP_NOMOVE | SWP_NOSIZE);//SWP_NOZORDER | SWP_NOACTIVATE);
}

void setwindow()
{
	::SetWindowLong(win_get_window(), GWL_STYLE, ::GetWindowLong(win_get_window(), GWL_STYLE) & ~(WS_BORDER | WS_DLGFRAME | WS_THICKFRAME));
	::SetWindowLong(win_get_window(), GWL_EXSTYLE, ::GetWindowLong(win_get_window(), GWL_EXSTYLE) & ~WS_EX_DLGMODALFRAME);
}

// Get the horizontal and vertical screen sizes in pixel
void GetDesktopResolution(int& horizontal, int& vertical)
{
	RECT desktop;
	// Get a handle to the desktop window
	const HWND hDesktop = GetDesktopWindow();
	// Get the size of screen to the variable desktop
	GetWindowRect(hDesktop, &desktop);
	// The top left corner will have coordinates (0,0)
	// and the bottom right corner will have coordinates
	// (horizontal, vertical)
	horizontal = desktop.right;
	vertical = desktop.bottom;
}

void GetRefresh()
{
	DEVMODE dm;
	// initialize the DEVMODE structure
	ZeroMemory(&dm, sizeof(dm));
	dm.dmSize = sizeof(dm);

	if (0 != EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &dm))
	{
		LOG_INFO("Primary Monitor refresh rate: %d", dm.dmDisplayFrequency);
		// inspect the DEVMODE structure to obtain details
		// about the display settings such as
		//  - Orientation
		//  - Width and Height
		//  - Frequency
		dm.dmDisplayFrequency = 60;
		ChangeDisplaySettings(&dm, 0);

		if (0 != EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &dm))
		{
			LOG_INFO("Primary Monitor refresh rate: %d", dm.dmDisplayFrequency);
		}
	}
}

//--------------------------------------------------------------------------------------
// Limit the current thread to one processor (the current one). This ensures that timing code
// runs on only one processor, and will not suffer any ill effects from power management.
// See "Game Timing and Multicore Processors" for more details
//--------------------------------------------------------------------------------------
void LimitThreadAffinityToCurrentProc()
{
	HANDLE hCurrentProcess = GetCurrentProcess();

	// Get the processor affinity mask for this process
	DWORD_PTR dwProcessAffinityMask = 0;
	DWORD_PTR dwSystemAffinityMask = 0;

	if (GetProcessAffinityMask(hCurrentProcess, &dwProcessAffinityMask, &dwSystemAffinityMask) != 0 && dwProcessAffinityMask)
	{
		// Find the lowest processor that our process is allows to run against
		DWORD_PTR dwAffinityMask = (dwProcessAffinityMask & ((~dwProcessAffinityMask) + 1));

		// Set this as the processor that our thread must always run against
		// This must be a subset of the process affinity mask
		HANDLE hCurrentThread = GetCurrentThread();
		if (INVALID_HANDLE_VALUE != hCurrentThread)
		{
			SetThreadAffinityMask(hCurrentThread, dwAffinityMask);
			CloseHandle(hCurrentThread);
		}
	}

	CloseHandle(hCurrentProcess);
}

//============================================================
//	osd_get_leds
//============================================================

int osd_get_leds()
{
	BYTE key_states[256];
	int result = 0;

	// get the current state
	if (GetKeyboardState(key_states))
	{
    	// set the numl0ck bit
		result |= (key_states[VK_NUMLOCK] & 1);
		result |= (key_states[VK_CAPITAL] & 1) << 1;
		result |= (key_states[VK_SCROLL] & 1) << 2;
	}
	return result;
}

//============================================================
//	osd_set_leds
//============================================================

void osd_set_leds(int state)
{
	BYTE key_states[256];
	int oldstate, newstate;

	// thanks to Lee Taylor for the original version of this code

	// get the current state
	if (GetKeyboardState(key_states))
	{

		// see if the numlock key matches the state
		oldstate = key_states[VK_NUMLOCK] & 1;
		newstate = state & 1;

		// if not, simulate a key up/down
		if (oldstate != newstate)
		{
			keybd_event(VK_NUMLOCK, 0x45, KEYEVENTF_EXTENDEDKEY | 0, 0);
			keybd_event(VK_NUMLOCK, 0x45, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0);
		}
		key_states[VK_NUMLOCK] = (key_states[VK_NUMLOCK] & ~1) | newstate;

		// see if the caps lock key matches the state
		oldstate = key_states[VK_CAPITAL] & 1;
		newstate = (state >> 1) & 1;

		// if not, simulate a key up/down
		if (oldstate != newstate)
		{
			keybd_event(VK_CAPITAL, 0x3a, 0, 0);
			keybd_event(VK_CAPITAL, 0x3a, KEYEVENTF_KEYUP, 0);
		}
		key_states[VK_CAPITAL] = (key_states[VK_CAPITAL] & ~1) | newstate;

		// see if the scroll lock key matches the state
		oldstate = key_states[VK_SCROLL] & 1;
		newstate = (state >> 2) & 1;

		// if not, simulate a key up/down
		if (oldstate != newstate)
		{
			keybd_event(VK_SCROLL, 0x46, 0, 0);
			keybd_event(VK_SCROLL, 0x46, KEYEVENTF_KEYUP, 0);
		}
		key_states[VK_SCROLL] = (key_states[VK_SCROLL] & ~1) | newstate;
	}
}
