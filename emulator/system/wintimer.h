#ifndef WINTIMER_H
#define WINTIMER_H

#include <windows.h>
#include <time.h>

// -----------------------------------------------------------------------------
// Description
// Simple high-resolution timer system using performance counter if available.
// Falls back to multimedia timer. Supports millisecond resolution with
// timeBeginPeriod(1) and timeEndPeriod(1).
// -----------------------------------------------------------------------------

// Timer structure
typedef struct timer_s
{
	__int64			frequency;					// Timer frequency
	float			resolution;					// Timer resolution
	unsigned long	mm_timer_start;				// Multimedia timer start
	unsigned long	mm_timer_elapsed;			// Multimedia timer elapsed
	BOOL			performance_timer;			// Are we using performance counter?
	__int64			performance_timer_start;	// Performance counter start
	__int64			performance_timer_elapsed;	// Performance counter elapsed
} timer_t;

extern timer_t g_timer;

// -----------------------------------------------------------------------------
// Initialization and shutdown
// -----------------------------------------------------------------------------
void TimerInit(void);       // Call once to initialize the timer
void TimerShutdown(void);   // Call on program exit to clean up

// -----------------------------------------------------------------------------
// Time query
// -----------------------------------------------------------------------------
float TimerGetTime(void);   // Returns time in seconds since TimerInit
float TimerGetTimeMS(void); // Returns time in milliseconds
float TimerElapsedSinceLastCall();
bool TimerIsHighResolution(void);
void TimerReset(void);

#endif
