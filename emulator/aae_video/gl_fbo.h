//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2025/2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
//
// gl_fbo.h
//
// Framebuffer Object (FBO) management for the AAE rendering pipeline.
//
// FBO layout:
//
//   fbo1  (1024x1024, GL_RGB8) - main render target, three attachments:
//           img1a [attachment 0] - current frame draw target (set_render writes here)
//           img1b [attachment 1] - feedback/trail accumulation buffer
//           img1c [attachment 2] - secondary feedback blend buffer
//
//   fbo2  (512x512, GL_RGB8)   - first glow downsample step:
//           img2a [attachment 0] - 512x512 downsampled frame (for blur)
//           img2b [attachment 1] - reserved / spare
//
//   fbo3  (256x256, GL_RGB8)   - second glow downsample + pingpong blur:
//           img3a [attachment 0] - blur pingpong target A
//           img3b [attachment 1] - blur pingpong target B
//
//   fbo4  (1024x1024, GL_RGBA8) - final compositing target:
//           img4a [attachment 0] - fully composited frame, blitted to screen
//
//   fbo_raster (game-native x prescale, GL_RGBA8) - per-game raster target:
//           img5a [attachment 0] - game-size raster surface
//           NOTE: Only allocated when a game is loaded (Machine != nullptr).
//           NOT available during the GUI / no-game phase.
//           Call fbo_init_raster() after a game is set up.
//           Call fbo_shutdown_raster() before switching games.
//
// Format rationale:
//   fbo1/fbo2/fbo3 use GL_RGB8 (no alpha). The vector blur, glow, and
//   trail/feedback passes use additive blending (GL_ONE, GL_ONE) and
//   alpha-accumulation modes that were designed for RGB-only buffers.
//   Adding an alpha channel to these FBOs changes every blend result
//   and breaks the glow/persistence effects. fbo4 and fbo_raster use
//   GL_RGBA8 because the final blit-to-screen path may need alpha.
//
//==========================================================================

#ifndef GL_FBO_H
#define GL_FBO_H

#include "sys_gl.h"
#include <initializer_list>

// ---------------------------------------------------------------------------
// FBO handles
// ---------------------------------------------------------------------------
extern GLuint fbo1;
extern GLuint fbo2;
extern GLuint fbo3;
extern GLuint fbo4;
extern GLuint fbo_raster;

// ---------------------------------------------------------------------------
// Texture handles
// fbo1 textures (GL_RGB8)
extern GLuint img1a;    // current frame draw target
extern GLuint img1b;    // feedback / trail buffer
extern GLuint img1c;    // secondary feedback buffer
// fbo2 textures (GL_RGB8)
extern GLuint img2a;    // 512x512 downsample for glow
extern GLuint img2b;    // spare / reserved
// fbo3 textures (GL_RGB8)
extern GLuint img3a;    // 256x256 blur pingpong A
extern GLuint img3b;    // 256x256 blur pingpong B
// fbo4 textures (GL_RGBA8)
extern GLuint img4a;    // final composited frame
extern GLuint img4b;    // overlay1 scratch buffer (CRT-only, pre-backdrop)
// fbo_raster textures (GL_RGBA8)
extern GLuint img5a;    // game-native raster surface (game must be loaded)
// ---------------------------------------------------------------------------

// FBO1/FBO4 dimensions (1024x1024, fixed for the whole pipeline).
extern const float width;
extern const float height;

// FBO2 dimensions (512x512 downsample).
extern const float width2;
extern const float height2;

// FBO3 dimensions (256x256 downsample + blur).
extern const float width3;
extern const float height3;

// ---------------------------------------------------------------------------
// fbo_init
// Allocates fbo1..fbo4 and their textures. Safe to call once at startup
// before any game is loaded. Does NOT allocate fbo_raster.
// fbo1/fbo2/fbo3 use GL_RGB8; fbo4 uses GL_RGBA8.
// ---------------------------------------------------------------------------
void fbo_init();

// ---------------------------------------------------------------------------
// fbo_init_raster
// Allocates fbo_raster / img5a at the current game's native resolution
// scaled by prescale (GL_RGBA8). Must be called AFTER a game is loaded
// (Machine->gamedrv must be valid). Safe to call more than once - releases
// any previous allocation first.
// ---------------------------------------------------------------------------
void fbo_init_raster();

// ---------------------------------------------------------------------------
// fbo_shutdown
// Releases all FBO and texture handles allocated by fbo_init(). Call once
// on application exit or full GL context teardown.
// ---------------------------------------------------------------------------
void fbo_shutdown();

// ---------------------------------------------------------------------------
// fbo_shutdown_raster
// Releases fbo_raster and img5a. Call before switching games so the next
// fbo_init_raster() gets the correct new dimensions.
// ---------------------------------------------------------------------------
void fbo_shutdown_raster();

// ---------------------------------------------------------------------------
// fbo_generate_mipmaps
// Regenerates the full mip chain for each listed texture. Must be called
// after rendering into an FBO and before sampling from its textures so that
// trilinear and anisotropic filtering work correctly.
// Example: fbo_generate_mipmaps({ img1a, img1b, img1c });
// ---------------------------------------------------------------------------
void fbo_generate_mipmaps(std::initializer_list<GLuint> textures);

// ---------------------------------------------------------------------------
// CHECK_FRAMEBUFFER_STATUS
// Queries and logs the completeness status of the currently bound FBO.
// Returns the raw GL status enum. Used internally after each fbo creation.
// ---------------------------------------------------------------------------
GLenum CHECK_FRAMEBUFFER_STATUS();

#endif // GL_FBO_H
