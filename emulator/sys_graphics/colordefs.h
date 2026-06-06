#pragma once
// -----------------------------------------------------------------------------
// Legacy MAME-Derived Module
// This file contains code originally developed as part of the M.A.M.E. Project.
// Portions of this file remain under the copyright of the original MAME authors
// and contributors. It has since been adapted and modernized for integration
// with the Game Engine Alpha project.
//
// Integration:
//   This library is part of the **Game Engine Alpha** project and is tightly
//   integrated with its texture management, logging, and math utility systems.
//
// Licensing Notice:
//   - Original portions of this code remain @ the M.A.M.E. (TM) Project and its
//     respective contributors under their original terms of distribution.
//   - Modifications, enhancements, and new code are @ 2025 Tim Cottrill and
//     released under the GNU General Public License v3 (GPLv3) or later.
//   - Redistribution must preserve both this notice and the original MAME
//     copyright acknowledgement.
//
// License:
//   This program is free software: you can redistribute it and/or modify
//   it under the terms of the GNU General Public License as published by
//   the Free Software Foundation, either version 3 of the License, or
//   (at your option) any later version.
//
//   This program is distributed in the hope that it will be useful,
//   but WITHOUT ANY WARRANTY; without even the implied warranty of
//   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
//   GNU General Public License for more details.
//
//   You should have received a copy of the GNU General Public License
//   along with this program. If not, see <https://www.gnu.org/licenses/>.
//
// Original Copyright:
//   This file is originally part of and copyright the M.A.M.E.(TM) Project.
//   For more information about MAME licensing, see the original MAME source
//   distribution and its associated license files.
//
// -----------------------------------------------------------------------------


#ifndef __COLORDEF__
#define _COLORDEF_

typedef unsigned int rgb_t;

#ifndef MAKE_BGR
#define MAKE_BGR(r,g,b) ((((b) & 0xff) << 16) | (((g) & 0xff) << 8) | ((r) & 0xff))
#endif

#ifndef MAKE_RGB
//#define MAKE_RGB(r,g,b) ((((r) & 0xff) << 16) | (((g) & 0xff) << 8) | ((b) & 0xff))
#define MAKE_RGB(r,g,b)  (((r) & 0xFF) | (((g) & 0xFF) << 8) | (((b) & 0xFF) << 16))
#endif

#ifndef MAKE_RGBA
//#define MAKE_RGBA(r,g,b,a)  (r | (g << 8) | (b << 16) | (a << 24))
#define MAKE_RGBA(r,g,b,a)  (((r) & 0xFF) | (((g) & 0xFF) << 8) | (((b) & 0xFF) << 16) | (((a) & 0xFF) << 24))
#endif

#ifndef RGB_BLUE
#define RGB_BLUE(rgba)  (((rgba)>>16) & 0xff)
#endif

#ifndef RGB_GREEN
#define RGB_GREEN(rgba) (((rgba)>>8) & 0xff)
#endif

#ifndef RGB_RED
#define RGB_RED(rgba)   ((rgba) & 0xff)
#endif

#ifndef RGB_ALPHA
#define RGB_ALPHA(rgba) (((rgba)>>24) & 0xff)
#endif

// Add alpha to VECTOR_COLOR111 result
#define VECTOR_COLOR111_A(c, a) \
    MAKE_RGBA( (((c) >> 2) & 1) * 0xff, \
               (((c) >> 1) & 1) * 0xff, \
               (((c) >> 0) & 1) * 0xff, \
               (a) )

#define VECTOR_COLOR111_OPAQUE(c) VECTOR_COLOR111_A(c, 255)

#define VECTOR_COLOR111(c) \
	MAKE_RGB((((c) >> 2) & 1) * 0xff, (((c) >> 1) & 1) * 0xff, (((c) >> 0) & 1) * 0xff)

#define VECTOR_COLOR222(c) \
	MAKE_RGB((((c) >> 4) & 3) * 0x55, (((c) >> 2) & 3) * 0x55, (((c) >> 0) & 3) * 0x55)

#define VECTOR_COLOR444(c) \
	MAKE_RGB((((c) >> 8) & 15) * 0x11, (((c) >> 4) & 15) * 0x11, (((c) >> 0) & 15) * 0x11)

// -----------------------------------------------------------------------------
// Common color definitions (opaque by default)
// -----------------------------------------------------------------------------

// Grayscale
#define RGB_BLACK           (MAKE_RGBA(0, 0, 0, 255))         // Pure black
#define RGB_WHITE           (MAKE_RGBA(255, 255, 255, 255))   // Pure white
#define RGB_GREY            (MAKE_RGBA(128, 128, 128, 255))   // Neutral grey
#define RGB_LIGHTGREY       (MAKE_RGBA(192, 192, 192, 255))   // Lighter grey
#define RGB_DARKGREY        (MAKE_RGBA(64, 64, 64, 255))      // Darker grey

// Primary colors
#define RGB_RED_SOLID             (MAKE_RGBA(255, 0, 0, 255))       // Full red
#define RGB_GREEN_SOLID           (MAKE_RGBA(0, 255, 0, 255))       // Full green
#define RGB_BLUE_SOLID      (MAKE_RGBA(0, 0, 255, 255))       // Full blue

// Secondary colors
#define RGB_YELLOW          (MAKE_RGBA(255, 255, 0, 255))     // Red + green
#define RGB_CYAN            (MAKE_RGBA(0, 255, 255, 255))     // Green + blue
#define RGB_MAGENTA         (MAKE_RGBA(255, 0, 255, 255))     // Red + blue
#define RGB_PURPLE          (MAKE_RGBA( 20, 20, 80, 255 ))
#define RGB_WHAT			(MAKE_RGBA( 255,128,255,255 ))
#define RGB_PINK			(MAKE_RGBA( 255,0,255,255 ))

// Pastel/UI colors
#define RGB_SOFTRED         (MAKE_RGBA(255, 102, 102, 255))   // Light red
#define RGB_SOFTGREEN       (MAKE_RGBA(144, 238, 144, 255))   // Light green
#define RGB_SOFTBLUE        (MAKE_RGBA(173, 216, 230, 255))   // Light blue
#define RGB_SOFTYELLOW      (MAKE_RGBA(255, 255, 153, 255))   // Light yellow
#define RGB_SOFTPURPLE      (MAKE_RGBA(216, 191, 216, 255))   // Light purple

// Named/theme colors
#define RGB_CFBLUE          (MAKE_RGBA(100, 149, 237, 255))   // Cornflower blue
#define RGB_ORANGE          (MAKE_RGBA(255, 165, 0, 255))     // Bright orange
#define RGB_BROWN           (MAKE_RGBA(139, 69, 19, 255))     // Saddle brown
#define RGB_GOLD            (MAKE_RGBA(255, 215, 0, 255))     // Gold yellow

// Transparent / UI tints
#define RGB_BLACK_50        (MAKE_RGBA(0, 0, 0, 128))         // 50% transparent black
#define RGB_WHITE_50        (MAKE_RGBA(255, 255, 255, 128))   // 50% transparent white
#define RGB_CLEAR           (MAKE_RGBA(0, 0, 0, 0))           // Fully transparent

// -----------------------------------------------------------------------------
// Convenience macros for alpha-adjusted common colors
// -----------------------------------------------------------------------------

#define RGBA_WHITE(a)       (MAKE_RGBA(255, 255, 255, (a)))   // White with variable alpha
#define RGBA_BLACK(a)       (MAKE_RGBA(0, 0, 0, (a)))         // Black with variable alpha

#endif // __COLORDEF__
