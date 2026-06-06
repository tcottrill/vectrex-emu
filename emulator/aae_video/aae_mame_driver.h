#pragma once

// -----------------------------------------------------------------------------
// [vectrex-port] Trimmed local copy of the AAE aae_mame_driver.h.
//
// The full AAE header (~400 lines) pulls in the entire emulator framework
// (inptport, cpu_control, memory, mixer, osd_video, driver_macros, ...).
// The vector-glow renderer only needs a tiny slice of it, reproduced here:
//
//   - rectangle struct
//   - the video_attribute / orientation flag #defines actually referenced
//   - a trimmed AAEDriver struct (name / rotation / video_attributes /
//     visible_area only)
//   - a trimmed RunningMachine struct (drv / gamedrv / orientation / pens /
//     video_attributes / visible_area)
//   - extern RunningMachine* Machine;
//   - the small set of inline pipeline globals the renderer reads.
//
// Values/flag bits are copied verbatim from the real AAE aae_mame_driver.h
// so they match the upstream renderer's expectations exactly.
// -----------------------------------------------------------------------------

#ifndef GLOBALS_H
#define GLOBALS_H

#include <cstdint>
#include "config.h"   // [vectrex-port] matches AAE: driver header provides `config`

#define MAX_PENS 256   // can't handle more than 256 colors on screen

// --- Vector / video attribute flags (verbatim from AAE) -----------------------
#define VECTOR_USES_OVERLAY1	0x0100  // Blending type 1 overlay
#define VECTOR_USES_OVERLAY2	0x0200  // An overlay that is visible like a gel.

#define VECTOR_USES_BW			0x1000
#define VECTOR_USES_COLOR		0x2000
#define VECTOR_DEFAULT_SCALE_2	0x4000
#define VECTOR_DEFAULT_SCALE_3	0x8000

// --- Orientation flags (verbatim from AAE) ------------------------------------
#define	ORIENTATION_DEFAULT		0x00
#define	ORIENTATION_FLIP_X		0x01	// mirror everything in the X direction
#define	ORIENTATION_FLIP_Y		0x02	// mirror everything in the Y direction
#define ORIENTATION_SWAP_XY		0x04	// mirror along the top-left/bottom-right diagonal
#define	ORIENTATION_ROTATE_90	(ORIENTATION_SWAP_XY|ORIENTATION_FLIP_X)
#define	ORIENTATION_ROTATE_180	(ORIENTATION_FLIP_X|ORIENTATION_FLIP_Y)
#define	ORIENTATION_ROTATE_270	(ORIENTATION_SWAP_XY|ORIENTATION_FLIP_Y)

#define	ROT0	0
#define	ROT90	(ORIENTATION_SWAP_XY|ORIENTATION_FLIP_X)	// rotate clockwise 90 degrees
#define	ROT180	(ORIENTATION_FLIP_X|ORIENTATION_FLIP_Y)		// rotate 180 degrees
#define	ROT270	(ORIENTATION_SWAP_XY|ORIENTATION_FLIP_Y)	// rotate counter-clockwise 90 degrees

// --- video_attributes flags (verbatim from AAE) -------------------------------
#define	VIDEO_TYPE_VECTOR			0x0001
#define VIDEO_TYPE_RASTER_COLOR		0x0008   // bit 3: raster, color
#define VIDEO_TYPE_RASTER_BW		0x0020   // bit 5: raster, monochrome
#define VIDEO_RASTER_CLASS_MASK		(VIDEO_TYPE_RASTER_COLOR | VIDEO_TYPE_RASTER_BW)
#define	VIDEO_SUPPORTS_DIRTY		0x0002
#define	VIDEO_MODIFIES_PALETTE		0x0004
#define	VIDEO_UPDATE_BEFORE_VBLANK	0x0000
#define	VIDEO_UPDATE_AFTER_VBLANK	0x0010

// --- Structs ------------------------------------------------------------------
struct rectangle
{
	int min_x, max_x;
	int min_y, max_y;
};

// Trimmed driver struct: only the fields the renderer reads. The real AAE
// AAEDriver has dozens more (ROMs, CPU table, sound, layout, ...) that the
// renderer never touches.
struct AAEDriver
{
	const char* name;
	int video_attributes;
	int rotation;
	struct rectangle visible_area;
};

// Trimmed running-machine struct.
struct RunningMachine
{
	const struct AAEDriver* gamedrv;   // game machine definition
	const struct AAEDriver* drv;        // same as gamedrv->drv
	int orientation;                    // ORIENTATION_* flags
	int video_attributes;
	struct rectangle visible_area;
	unsigned char pens[MAX_PENS];       // remapped palette pen numbers (raster path only)
};

extern struct RunningMachine* Machine;

// Artwork-loaded flags. art_loaded[1] gates the OVERLAY1/OVERLAY2 paths.
extern int art_loaded[6];

// --- Inline pipeline globals (single definition, header-only) ----------------
// Sane Global Rectangle Coordinates (replacing the old sx/sy/ex/ey).
inline int game_rect_left = 0;
inline int game_rect_right = 1024;
inline int game_rect_bottom = 0;
inline int game_rect_top = 1024;

inline float bezelzoom = 1.0f;
inline int bezelx = 0;
inline int bezely = 0;

inline int gamenum = 0;
inline int have_error = 0;
inline int paused = 0;

#endif // GLOBALS_H
