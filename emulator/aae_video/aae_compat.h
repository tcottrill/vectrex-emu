#pragma once

// -----------------------------------------------------------------------------
// [vectrex-port] Compatibility shim for the AAE vector-glow renderer.
//
// The AAE renderer pulls a handful of symbols from the wider AAE application
// (window setup, GUI-active query, palette pen lookup). The Vectrex host does
// not provide those, so this shim declares minimal stand-ins. Definitions live
// in aae_compat.cpp.
//
// This header is included by the trimmed AAE renderer sources via an explicit
// // [vectrex-port] include at the top of each .cpp that needs it.
// -----------------------------------------------------------------------------

#include "aae_mame_driver.h"

// -----------------------------------------------------------------------------
// WindowSetup / GetWindowSetup
// AAE's framework.h exposes a WindowSetup struct describing the live window
// (current client size + aspect ratio). The Vectrex framework.h only exposes
// SCREEN_W / SCREEN_H, so this shim provides an equivalent struct and a
// GetWindowSetup() that refreshes clientWidth/clientHeight from SCREEN_W/H on
// each call. The renderer only reads clientWidth, clientHeight, aspectRatio.
// -----------------------------------------------------------------------------
struct WindowSetup
{
	int   clientWidth  = 0;
	int   clientHeight = 0;
	float aspectRatio  = 4.0f / 3.0f;
};

WindowSetup& GetWindowSetup();

// -----------------------------------------------------------------------------
// emulator_is_gui_active
// In AAE this returns true while the front-end GUI (game picker) is showing,
// which suppresses the glow/trail passes. The Vectrex has no such GUI, so this
// always returns false (glow + trail always allowed).
// -----------------------------------------------------------------------------
bool emulator_is_gui_active();

// -----------------------------------------------------------------------------
// osd_get_pen
// Palette pen -> RGB lookup used only by the (cut) raster path. Declared here
// so any stray reference still links; the kept vector path never calls it.
// -----------------------------------------------------------------------------
void osd_get_pen(unsigned char pen, unsigned char* r, unsigned char* g, unsigned char* b);
