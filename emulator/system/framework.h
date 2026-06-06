#pragma once

#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>
#include "glew.h"
#include "wglew.h"
#include "sys_log.h"

extern void osMessage(int ID, const char* fmt, ...);
extern void allegro_message(const char *title, const char *message);
extern HWND win_get_window();
extern int SCREEN_W;
extern int SCREEN_H;

