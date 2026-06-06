#include "wintimer.h"
#include <mmsystem.h>

#pragma comment(lib, "winmm.lib")  // Link to multimedia library

struct TimerScope {
	TimerScope() { TimerInit(); }
	~TimerScope() { TimerShutdown(); }
};
static TimerScope _timerLifetime;

BOOL bTimerInitialized = FALSE;
timer_t g_timer;

void TimerInit(void)
{
	memset(&g_timer, 0, sizeof(g_timer));

	// Request high-resolution multimedia timer
	timeBeginPeriod(1);

	if (!QueryPerformanceFrequency((LARGE_INTEGER*)&g_timer.frequency))
	{
		// Fallback to multimedia timer
		g_timer.performance_timer = FALSE;
		g_timer.mm_timer_start = timeGetTime();
		g_timer.resolution = 1.0f / 1000.0f;
		g_timer.frequency = 1000;
		g_timer.mm_timer_elapsed = g_timer.mm_timer_start;
	}
	else
	{
		// High-resolution performance counter available
		QueryPerformanceCounter((LARGE_INTEGER*)&g_timer.performance_timer_start);
		g_timer.performance_timer = TRUE;
		g_timer.resolution = (float)(1.0 / (double)g_timer.frequency);
		g_timer.performance_timer_elapsed = g_timer.performance_timer_start;
	}

	bTimerInitialized = TRUE;
}

static float lastTime = 0.0f;

float TimerElapsedSinceLastCall()
{
	float current = TimerGetTime();
	float delta = current - lastTime;
	lastTime = current;
	return delta;
}

void TimerReset(void)
{
	if (!bTimerInitialized)
		TimerInit();

	if (g_timer.performance_timer)
		QueryPerformanceCounter((LARGE_INTEGER*)&g_timer.performance_timer_start);
	else
		g_timer.mm_timer_start = timeGetTime();
}

bool TimerIsHighResolution()
{
	return g_timer.performance_timer != FALSE;
}

void TimerShutdown(void)
{
	if (bTimerInitialized)
	{
		timeEndPeriod(1);
		bTimerInitialized = FALSE;
	}
}

float TimerGetTime()
{
	LARGE_INTEGER time;

	if (!bTimerInitialized)
		TimerInit();

	if (g_timer.performance_timer)
	{
		QueryPerformanceCounter(&time);
		return ((float)(time.QuadPart - g_timer.performance_timer_start) * g_timer.resolution);
	}
	else
	{
		return ((float)(timeGetTime() - g_timer.mm_timer_start) * g_timer.resolution);
	}
}

float TimerGetTimeMS()
{
	return TimerGetTime() * 1000.0f;
}
