//==========================================================================
// AAE - Another Arcade Emulator
// A MAME (TM) derivative based on early MAME code (0.29 through 0.90)
// mixed with original code. Created for amusement and archival purposes.
//
// All MAME code used in this emulator remains the copyright of the MAME
// Team. All MAME-derived code should be considered as belonging to them.
//
// Original AAE code copyright (C) 2025/2026 Tim Cottrill, released under
// the GNU GPL v3 or later. See accompanying source files for full details.
//==========================================================================
//
// opengl_renderer.cpp
//
// Core OpenGL rendering pipeline for AAE. Manages the multi-stage FBO
// compositing pipeline used to render both vector and raster games.
//
// Rendering pipeline overview:
//
//   STEP 1 - set_render()
//     Binds FBO1/img1a, sets 1024x1024 ortho. Vectors and raster polys
//     are drawn into this texture by the game-specific draw code.
//
//   STEP 2 - render()
//     Dispatches to draw_all() (vector) or raster_poly_update() + sc->Render()
//     (raster), then calls final_render().
//
//   STEP 3 - final_render()
//     Composites all layers (game image, overlay, feedback trail, glow blur,
//     bezel, scanlines) and writes the finished frame to FBO4.
//     end_render_fbo4() then blits FBO4 to the backbuffer, scaled to the
//     actual window size and aspect ratio.
//
// FBO / Texture layout:
//   FBO1 - img1a (attachment 0): current frame render target (1024x1024)
//          img1b (attachment 1): feedback/trail accumulation buffers
//          img1c (attachment 2): additional feedback blend buffer
//   FBO2 - img2a: 512x512 downsampled image for glow blur pass 1
//   FBO3 - img3a (attachment 0): 256x256 pingpong blur target A
//          img3b (attachment 1): 256x256 pingpong blur target B
//   FBO4 - img4a: final composited frame, blitted to screen at window size
//
// Artwork texture layout:
//   art_tex[0] - Backdrop (behind game screen)
//   art_tex[1] - Overlay  (color gel over game screen)
//   art_tex[2] - Bezel mask (used for Tempest/Tacscan rotation bezels, Depricated)
//   art_tex[3] - Bezel frame (rendered on top of everything)
//   art_tex[4] - Screen burn (reserved, not currently used)
//
//==========================================================================

#include "opengl_renderer.h"
#include "aae_mame_driver.h"
// [vectrex-port] removed: vector_fonts.h (VF font renderer - UI overlays cut)
#include "texture_handler.h"   // [vectrex-port] trimmed local copy: art_tex[] + set_texture()
#include "gl_fbo.h"
#include "gl_texturing.h"
#include "gl_shader.h"
#include "emu_vector_draw.h"
// [vectrex-port] removed: fast_poly.h (Fpoly raster-polygon renderer - raster path cut)
// [vectrex-port] removed: os_basic.h (not used by kept paths)
// [vectrex-port] removed: MathUtils.h (aae::math::ortho - only render_scanlines, cut)
// [vectrex-port] removed: menu.h (get_menu_status / video_loop - UI overlays cut)
// [vectrex-port] removed: aae_emulator.h (get_exit_confirm_* - UI overlays cut)
// [vectrex-port] removed: mame_layout.h (Layout_Render - raster path cut)
// [vectrex-port] removed: inifile.h (unused by kept paths)
// [vectrex-port] removed: mame_vector.h (vector_start/update/clear - fed via emu_vector_draw instead)
// [vectrex-port] removed: aae_avg.h (AVG_BUSY - unused)
#include "aae_compat.h"        // [vectrex-port] WindowSetup/GetWindowSetup, emulator_is_gui_active
#include <chrono>   // for optional frame-time profiling
#include <cstring>  // strcmp for driver name checks
// ---------------------------------------------------------------------------
// Module-level globals
// ---------------------------------------------------------------------------
// Calculated screen rectangle used to blit FBO4 to the window at the correct
// size and aspect ratio. Allocated in init_gl(), freed on shutdown.
Rect2* screen_rect = nullptr;

// [vectrex-port] removed: Fpoly* sc (raster polygon renderer - raster path cut)
// [vectrex-port] removed: g_scanrezTex / g_scanVAO / g_scanVBO / ScanQuadVert
//                 (scanline overlay - raster path cut)
// [vectrex-port] removed: extern int sx, sy, ex, ey (only referenced by cut code)

// Special Flag Just for Warlords with it's dual Monitor Types
// Near the top with other module-level state
int g_scanline_override = 0;  // 0 = default, 1 = force on, -1 = force off
// ---------------------------------------------------------------------------
// orientation_to_rect2_rotation
// Converts ORIENTATION_xxx flags (from Machine->orientation or
// config.system_rotation) to the Rect2 rotation index used by
// UpdateScreenRect():
//   0 = normal, 1 = rotate right (CW 90), 2 = rotate left (CCW 90), 3 = 180
// ---------------------------------------------------------------------------
static int orientation_to_rect2_rotation(int orientation)
{
	// Only the system rotation component determines the Rect2 index.
	// The driver rotation describes the cabinet monitor orientation and
	// is handled by the game's coordinate generation; the system rotation
	// is the user-requested display-time rotation (-ror / -rol).
	switch (orientation)
	{
	case ROT90:  return 1; // -ror: rotate right
	case ROT270: return 2; // -rol: rotate left
	case ROT180: return 3; // 180 flip
	default:     return 0; // no system rotation
	}
}


// ---------------------------------------------------------------------------
// emulator_on_window_resize
// Called by the OS message handler whenever the client area changes size.
// Updates screen_rect so the final blit tracks the new window dimensions.
// ---------------------------------------------------------------------------
void emulator_on_window_resize(int newW, int newH)
{
	if (!screen_rect) return;

	auto& ws = GetWindowSetup();
	int rot = orientation_to_rect2_rotation(config.system_rotation);
	screen_rect->UpdateScreenRect(ws.clientWidth, ws.clientHeight, ws.aspectRatio, rot);
	LOG_INFO("Window resized - new client area: %d x %d (rotation=%d)", ws.clientWidth, ws.clientHeight, rot);
}

// [vectrex-port] removed: raster_poly_update() - read the MAME main_bitmap and
//                 fed pixels to the Fpoly renderer. Raster path is cut; the
//                 Vectrex feeds vectors via emu_vector_draw add_line()/add_tex().

// TBD: Change or remove.
// ---------------------------------------------------------------------------
// Widescreen_calc
// Computes wideadj - a horizontal scale factor applied to the game viewport
// rectangle - so the game image fills the selected aspect ratio correctly.
//   0 = 4:3  (classic arcade)
//   1 = 16:9 (widescreen)
//   2 = 16:10
// ---------------------------------------------------------------------------
void Widescreen_calc()
{
	float val = 0;

	if (config.widescreen == 0) val = 1.3333f;
	if (config.widescreen == 1) val = 1.77f;
	if (config.widescreen == 2) val = 1.6f;

	//wideadj = (float)(1.3333 / val);
}

// ---------------------------------------------------------------------------
// set_ortho
// Convenience wrapper: sets viewport and a top-left-origin 2D ortho
// projection to the given dimensions. Used throughout the pipeline to
// switch between 1024x1024 (FBO space) and window-size (backbuffer) spaces.
// ---------------------------------------------------------------------------
void set_ortho(GLint width, GLint height)
{
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glViewport(0, 0, width, height);
	glOrtho(0, width, 0, height, -1.0f, 1.0f);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
}

// ---------------------------------------------------------------------------
// set_ortho_raster
// Y-DOWN ortho projection used by the raster rendering path.
// Unlike set_ortho (which is Y-up, origin at bottom-left), this sets origin
// at top-left with Y increasing downward, matching the raster bitmap layout.
// This fixes the Y-flip that would occur if the standard vector ortho were used.
// ---------------------------------------------------------------------------
void set_ortho_raster(GLint width, GLint height)
{
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glViewport(0, 0, width, height);
	glOrtho(0, width, height, 0, -1.0f, 1.0f);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
}

// [vectrex-port] removed: glcode_get_scanrez_tex(), init_scanline_quad(),
//                 shutdown_scanline_quad() - scanline overlay is part of the
//                 cut raster path (referenced removed g_scanrezTex/g_scanVAO/VBO).

// ---------------------------------------------------------------------------
// init_gl
// One-time OpenGL initialization. Creates FBOs, compiles shaders, builds
// the font renderer, and initializes supporting subsystems.
//
// Protected by a static flag so it is safe to call more than once (e.g.,
// if the GUI calls it before a game is selected, and the emulator calls it
// again when launching - only the first call does anything).
//
// GUI note: This is intentionally called once and left active for the
// lifetime of the process. The GUI overlay driver can safely use all
// GL resources initialized here without re-initializing them.
// ---------------------------------------------------------------------------
int init_gl(void)
{
	static int init_one = 0;

	if (!init_one)
	{
		// --- VSync control ---
		if (wglewIsSupported("WGL_EXT_swap_control"))
		{
			if (config.forcesync)
			{
				wglSwapIntervalEXT(1);
				LOG_INFO("VSync enabled (config.forcesync).");
			}
			else
			{
				wglSwapIntervalEXT(0);
				LOG_INFO("VSync disabled.");
			}
		}
		else
		{
			LOG_INFO("WGL_EXT_swap_control not supported - VSync state unknown.");
		}

		// --- Base GL state ---
		set_ortho(1024, 768);
		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);

		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glEnable(GL_LINE_SMOOTH);
		glEnable(GL_POINT_SMOOTH);
		glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);

		glLineWidth(config.linewidth);
		glPointSize(config.pointsize);

		// --- Screen rectangle (tracks window size and aspect ratio) ---
		// --- Screen rectangle (tracks window size and aspect ratio) ---
		auto& ws = GetWindowSetup();
		int rot = orientation_to_rect2_rotation(config.system_rotation);
		screen_rect = new Rect2(ws.clientWidth, ws.clientHeight, ws.aspectRatio, rot);

		// NOTE: Scanlines texture loading is NOT done here. It is deferred to
		// init_raster_overlay(), which is called per-game from run_game() after
		// setup_game_config() has set the correct config.raster_effect value.

		// --- FBO allocation ---
		LOG_INFO("Initializing FBOs...");
		fbo_init();
		// [vectrex-port] removed: vector_start() (mame_vector init). The Vectrex
		//                 feeds geometry directly via emu_vector_draw add_line().

		// --- Shader compilation ---
		init_shader();

		// [vectrex-port] removed: init_scanline_quad() (scanline overlay - raster path cut)
		// [vectrex-port] removed: VF.Initialize() (vector font renderer - UI overlays cut)
		// [vectrex-port] removed: sc = new Fpoly() (raster polygon renderer - raster path cut)

		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		LOG_INFO("OpenGL initialization complete.");

		init_one++;
	}

	return 1;
}

// ---------------------------------------------------------------------------
// end_gl
// Shutdown: release subsystems that require explicit cleanup.
// Call this once when the application exits.
// ---------------------------------------------------------------------------
void end_gl()
{
	// [vectrex-port] removed: shutdown_scanline_quad() (scanline overlay cut)
	LOG_INFO("AAE GL shutdown.");
}

// [vectrex-port] removed: init_raster_overlay() and shutdown_raster_overlay()
//                 - loaded/freed the scanlines (g_scanrezTex) texture via
//                 make_single_bitmap(); both belong to the cut raster path.

// ---------------------------------------------------------------------------
// glcode_vector_hard_clear_fbo1
// Clears all three attachments of FBO1 (img1a, img1b, img1c) to opaque
// black. Used when starting a new vector game to flush any leftover trail
// or feedback data from a previous session.
// Saves and restores the previously bound FBO and viewport.
// ---------------------------------------------------------------------------
void glcode_vector_hard_clear_fbo1()
{
	if (!fbo1)
		return;

	GLint prevFbo = 0;
	GLint prevVP[4] = { 0, 0, 0, 0 };
	glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, &prevFbo);
	glGetIntegerv(GL_VIEWPORT, prevVP);

	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo1);
	glViewport(0, 0, 1024, 1024);

	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_BLEND);
	glClearColor(0, 0, 0, 0);

	glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
	glClear(GL_COLOR_BUFFER_BIT);

	glDrawBuffer(GL_COLOR_ATTACHMENT1_EXT);
	glClear(GL_COLOR_BUFFER_BIT);

	glDrawBuffer(GL_COLOR_ATTACHMENT2_EXT);
	glClear(GL_COLOR_BUFFER_BIT);

	// Restore previous FBO and viewport.
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, (GLuint)prevFbo);
	glViewport(prevVP[0], prevVP[1], prevVP[2], prevVP[3]);
}

// ---------------------------------------------------------------------------
// set_render_fbo4
// Binds FBO4 and prepares it for final compositing. All game image layers,
// the bezel, and UI overlays are drawn here before the result is blitted to
// the backbuffer by end_render_fbo4().
// ---------------------------------------------------------------------------
void set_render_fbo4()
{
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo4);
	glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);

	set_ortho(1024, 1024);

	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	glEnable(GL_BLEND);
	glDisable(GL_LIGHTING);
	glDisable(GL_DEPTH_TEST);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_LINE_SMOOTH);
	glEnable(GL_POINT_SMOOTH);
	glDisable(GL_DITHER);   // required for some older cards
}

// ---------------------------------------------------------------------------
// end_render_fbo4
// Unbinds FBO4 and blits img4a (the composited frame) to the backbuffer,
// scaled and positioned by screen_rect to match the window size and aspect.
// ---------------------------------------------------------------------------
void end_render_fbo4()
{
	check_gl_error_named("end_render_fbo4 (enter)");

	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
	glDrawBuffer(GL_BACK);
	glActiveTexture(GL_TEXTURE0);

	// Clear the backbuffer so pillarbox/letterbox bars are always clean.
    // Without this, stale pixels persist outside the screen_rect quad
    // when the aspect ratio changes in fullscreen.
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);


	auto& ws = GetWindowSetup();
	set_ortho(ws.clientWidth, ws.clientHeight);

	glDisable(GL_BLEND);

	// Blit img4a to the screen. Blending disabled: this is a straight copy.
	// screen_rect->Render() handles letterboxing / pillarboxing for the
	// configured aspect ratio (1.33f = 4:3).
	set_texture(&img4a, 1, 0, 0, 0);
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	screen_rect->Render();

	check_gl_error_named("end_render_fbo4 (exit)");
}

////////////////////////////////////////////////////////////////////////////////
// FBO DOWNSAMPLING AND BLUR CODE (supports the vector glow effect)           //
//                                                                             //
// The glow effect is produced by downsampling the rendered frame to 512x512  //
// (fbo2), then to 256x256 (fbo3), and blurring at the lower resolution with  //
// a multi-pass offset shader. The blurred result is composited in the final   //
// shader as an additive glow layer.                                           //
////////////////////////////////////////////////////////////////////////////////

// ---------------------------------------------------------------------------
// copy_main_img_to_fbo2
// Downsample step 1: copies img1b (1024x1024 feedback buffer) into fbo2 at
// 512x512 via the blur shader. The downsampled image is stored in img2a.
// ---------------------------------------------------------------------------
void copy_main_img_to_fbo2()
{
	fbo_generate_mipmaps({ img1b });

	GLuint fbo2_tex = 0;
	glLoadIdentity();
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo2);
	glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
	set_ortho(512, 512);
	glDisable(GL_BLEND);

	set_texture(&img1b, 1, 0, 0, 0);
	glActiveTexture(GL_TEXTURE0);

	bind_shader(fragBlur);
	check_gl_error_named("copy_main_img_to_fbo2");
	set_uniform1i(fragBlur, "colorMap", fbo2_tex);
	set_uniform1f(fragBlur, "width", 512.0f);
	set_uniform1f(fragBlur, "height", 512.0f);

	FS_Rect(0, 512);
	unbind_shader();
}

// ---------------------------------------------------------------------------
// copy_fbo2_to_fbo3
// Downsample step 2: copies img2a (512x512) into fbo3 at 256x256 via the
// blur shader. Result is stored in img3a (attachment 0).
// Clears attachment 1 (img3b) first so the pingpong is clean each frame.
// ---------------------------------------------------------------------------
void copy_fbo2_to_fbo3()
{
	GLuint fbo3_tex = 0;
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo3);

	// Clear both pingpong buffers before each frame.
	glDrawBuffer(GL_COLOR_ATTACHMENT1_EXT);
	glClearColor(0.0, 0.0, 0.0, 0.0);
	glClear(GL_COLOR_BUFFER_BIT);

	glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
	set_ortho(256, 256);
	glDisable(GL_BLEND);

	check_gl_error_named("copy_fbo2_to_fbo3");

	bind_shader(fragBlur);
	set_uniform1i(fragBlur, "colorMap", fbo3_tex);
	set_uniform1f(fragBlur, "width", 256.0f);
	set_uniform1f(fragBlur, "height", 256.0f);

	set_texture(&img2a, 1, 0, 0, 1);
	FS_Rect(0, 256);
	unbind_shader();
}

// ---------------------------------------------------------------------------
// render_blur_image_fbo3
// Blur step: pingpongs between img3a and img3b in fbo3 across 4 passes,
// each time drawing with a small sub-pixel offset (fshifta / fshiftb arrays)
// and additive blending to accumulate a soft glow.
//
// v1 and v2 control the near and far sample distances. Increasing them
// widens the glow at the cost of some precision.
// ---------------------------------------------------------------------------
void render_blur_image_fbo3()
{
	static constexpr float v1 = 1.0f;  // near sample offset (pixels at 256x256)
	static constexpr float v2 = 2.0f;  // far sample offset

	// Global sub-pixel correction applied to all quads to keep the blurred
	// image centered relative to the source.
	const float globalOffsetX = -0.05f;
	const float globalOffsetY = -0.20f;

	// Each row is: [x0, y0, x1, y1] for one half of a pingpong pass.
	// 8 rows * 4 values = 32 floats. We step by 4 per pass (4 passes total).
	float fshifta[] = {
		 v1,  0,  -v1,   0,
		-v1,  0,   v1,   0,
		  0,  v1,   0, -v1,
		  0, -v1,   0,  v1,
		 v1,  v1, -v1, -v1,
		-v1, -v1,  v1,  v1,
		-v1,  v1,  v1, -v1,
		 v1, -v1, -v1,  v1
	};

	float fshiftb[] = {
		 v2,  0,  -v2,   0,
		-v2,  0,   v2,   0,
		  0,  v2,   0, -v2,
		  0, -v2,   0,  v2,
		 v2,  v2, -v2, -v2,
		-v2, -v2,  v2,  v2,
		-v2,  v2,  v2, -v2,
		 v2, -v2, -v2,  v2
	};

	bind_shader(fragBlur);
	set_uniform1i(fragBlur, "colorMap", 0);
	set_uniform1f(fragBlur, "width", 256.0f);
	set_uniform1f(fragBlur, "height", 256.0f);

	glEnable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE);  // additive blend accumulates glow
	glColor4f(0.1f, 0.1f, 0.1f, 0.1f);

	// Lambda to draw one offset quad. Converts float offsets to screen-space
	// by adding globalOffset and sizing to height3 (the FBO3 height, 256).
	auto DrawQuadOffset = [&](float ox, float oy) {
		float x1 = ox + globalOffsetX;
		float y1 = oy + globalOffsetY;
		float x2 = (float)height3 + x1;
		// y2 maps size+y1 down to y1, matching the orientation of FS_Rect(0,size).
		drawTexturedQuad(x1, (float)height3 + y1, x2, y1, 1);
		};

	int i = 0;

	for (int pass = 0; pass < 4; ++pass)
	{
		// A -> B: draw img3a into attachment 1 (img3b) with near offset.
		glDrawBuffer(GL_COLOR_ATTACHMENT1_EXT);
		set_texture(&img3a, 1, 0, 0, 0);
		DrawQuadOffset(fshifta[i], fshifta[i + 1]);

		// B -> A: draw img3b into attachment 0 (img3a) with far offset.
		glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
		set_texture(&img3b, 1, 0, 0, 0);
		DrawQuadOffset(fshiftb[i], fshiftb[i + 1]);

		i += 4;
	}

	check_gl_error_named("render_blur_image_fbo3");
	unbind_shader();
}

// ====================================================================
// render_ui_overlays()
//
// Draws the pause dim, PAUSED text, exit confirmation dialog, menu,
// FPS counter, and debug overlays on top of the current backbuffer.
//
// Called by BOTH rendering paths:
//   - Vector pipeline: from final_render() after FBO4 blit
//   - Raster pipeline: from emulator_run() after RasterRender_Present()
//
// Sets up its own 1024x768 ortho projection on the backbuffer.
// The caller must have already rendered the game frame before calling.
// ====================================================================
void render_ui_overlays(int winW, int winH)
{
	// [vectrex-port] no-op. The original drew the pause dim, "PAUSED" text, the
	// exit-confirmation dialog (all via the VF vector-font renderer), and the
	// per-game video_loop()/menu overlays. All of those subsystems
	// (vector_fonts.h, menu.h, aae_emulator.h) are cut from this port, so the
	// body is intentionally empty. final_render() still calls this between the
	// bezel pass and end_render_fbo4(); it simply does nothing now.
	(void)winW;
	(void)winH;
}

////////////////////////////////////////////////////////////////////////////////
// RENDERING PIPELINE - STEPS 1, 2, and 3                                    //
////////////////////////////////////////////////////////////////////////////////

// ---------------------------------------------------------------------------
// set_render [STEP 1]
// Binds the correct FBO for the current game type and prepares the render
// target for the frame. Vector games use FBO1 (img1a) at 1024x1024 with
// Y-up ortho. Raster games use fbo_raster (img5a) at the game's native
// visible_area size * prescale, with Y-DOWN ortho so the bitmap pixels
// land correctly without a vertical flip.
// ---------------------------------------------------------------------------
void set_render()
{
	// Set 1024x1024 ortho to match the FBO dimensions.
	if (Machine->drv->video_attributes & VIDEO_TYPE_VECTOR)
	{	// Bind FBO1 and direct output to attachment 0 (img1a).
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo1);
		glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
		set_ortho(1024, 1024);
		// [vectrex-port] removed: VF.SetOverrideViewport(false) (vector font cut)
	}
	else
	{
		const rectangle& va = Machine->drv->visible_area;

		int vw = (va.max_x - va.min_x + 1);
		int vh = (va.max_y - va.min_y + 1);

		// Match the raster FBO allocation shape for rotated games.
		if (Machine->drv->rotation & ORIENTATION_SWAP_XY)
		{
			int t = vw;
			vw = vh;
			vh = t;
		}

		const int rw = static_cast<int>((float)vw * config.prescale);
		const int rh = static_cast<int>((float)vh * config.prescale);

		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo_raster);
		glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);

		// Y-down ortho: matches the raster bitmap layout (origin top-left).
		// MUST match fbo_init_raster() dimensions exactly.
		set_ortho(rw, rh);
	}
	// Only clear the frame if the game is actively running!
	// This preserves the last frame in memory for the background while paused/in-menu.
	if (!paused)
	{
		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	}

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	check_gl_error_named("set_render");
}

// ---------------------------------------------------------------------------
// render [STEP 2]
// Main per-frame render dispatch. Handles the paused state, then routes to
// the vector or raster draw path before calling the appropriate final_render.
// ---------------------------------------------------------------------------
void render()
{
	// Only process new game geometry if we are not paused.
	// (If paused, FBO1 retains the image from the last active frame).
	//
	// [vectrex-port] Vector-only. The original called mame_vector's
	// vector_update() then draw_all() then vector_clear_list(); the Vectrex
	// instead pushes its own geometry into emu_vector_draw via add_line()/
	// add_tex() each frame BEFORE calling render(), so here we just flush it
	// with draw_all(). The caller is responsible for clearing the cache
	// (cache_clear()) after the frame. The raster branch (raster_poly_update /
	// sc->Render / final_render_raster) is cut.
	if (!paused)
	{
		draw_all();
	}

	// ALWAYS composite the layers. This applies game_rect boundaries and
	// shaders to the frozen frame exactly as it did when running.
	final_render(game_rect_left, game_rect_right, game_rect_bottom, game_rect_top);
}

// Composites all rendering layers into FBO4 and presents the result.
//
// Parameters define the game screen rectangle in 1024-space:
//   xmin/xmax = horizontal extent (sx/ex from game config)
//   ymin/ymax = vertical extent   (sy/ey from game config)
//
// Layer order (back to front):
//   1. img1a -> img1b : copy current frame with optional B/W or additive blend
//   2. art_tex[1]     : color overlay (if enabled)
//   3. img1b -> img1c : vector trail / phosphor persistence (if enabled)
//   4. FBO2/3 blur    : glow downsample+blur passes (if enabled)
//   5. fragMulti shader: composites img1b + blur + backdrop in one pass
//   6. Bezel frame    : art_tex[3] drawn on top with alpha test (if enabled)
//   7. Scanlines      : TiledEffect_Draw() for raster games (if enabled)
//   8. video_loop()   : any game-specific per-frame overlay (score display etc.)
// ---------------------------------------------------------------------------

void final_render(int left, int right, int bottom, int top)
{
	// [vectrex-port] removed: the "gui" driver menu-dim clamp hack. Its body was
	// already commented out, and it referenced get_menu_status() (menu.h, cut).
	// NOTE:
	// Overlay behavior is controlled by the driver's video_attributes flags.
	// We MUST use the same source consistently here, otherwise overlay types
	// can be mis-detected and end up affecting the cabinet backdrop.
	const int vattr = (Machine && Machine->drv) ? Machine->drv->video_attributes : 0;
	const bool uses_overlay1 = (vattr & VECTOR_USES_OVERLAY1) != 0;
	const bool uses_overlay2 = (vattr & VECTOR_USES_OVERLAY2) != 0;

	GLint bleh = 0;
	int   useglow = 0;

	auto start = std::chrono::steady_clock::now();

	//--------------------------------------------------------------------------
	// LAYER 1: Copy img1a (current frame) into img1b.
	//--------------------------------------------------------------------------
	glEnable(GL_TEXTURE_2D);
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo1);
	glDrawBuffer(GL_COLOR_ATTACHMENT1_EXT);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	set_texture(&img1a, 1, 0, 0, 1);

	if (Machine->drv->video_attributes & VIDEO_TYPE_RASTER_BW)
		glBlendFunc(GL_ONE, GL_ZERO);
	else
		glBlendFunc(GL_ONE, GL_ONE);

	// Always do a pure 1:1 copy for the FBO buffers!
	FS_Rect(0, 1024);

	//--------------------------------------------------------------------------
	// LAYER 3: Vector trail / phosphor persistence (img1b -> img1c).
	//--------------------------------------------------------------------------
	if (config.vectrail && !emulator_is_gui_active()) //No vectrail for the gui
	{
		glDrawBuffer(GL_COLOR_ATTACHMENT2_EXT);
		glDisable(GL_DITHER);
		set_texture(&img1b, 1, 0, 0, 0);
		glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_SRC_ALPHA);

		switch (config.vectrail)
		{
		case 1:  glColor4f(1.0f, 1.0f, 1.0f, 0.825f); break;
		case 2:  glColor4f(1.0f, 1.0f, 1.0f, 0.86f);  break;
		case 3:  glColor4f(1.0f, 1.0f, 1.0f, 0.93f);  break;
		default: glColor4f(0.95f, 0.95f, 0.95f, 1.0f); break;
		}

		FS_Rect(0, 1024);
		fbo_generate_mipmaps({ img1b });
	}

	//--------------------------------------------------------------------------
	// LAYER 4: Glow blur passes (FBO2 and FBO3).
	//--------------------------------------------------------------------------
	if (config.vecglow && !emulator_is_gui_active()) // No Vecglow for the GUI
	{
		copy_main_img_to_fbo2();
		copy_fbo2_to_fbo3();
		render_blur_image_fbo3();
		fbo_generate_mipmaps({ img2a, img3a, img3b });
	}

	//--------------------------------------------------------------------------
	// LAYER 5A: Build the CRT/game image into img4b (FBO4 attachment 1).
	//--------------------------------------------------------------------------
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo4);
	glDrawBuffer(GL_COLOR_ATTACHMENT1_EXT);
	set_ortho(1024, 1024);

	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_LIGHTING);

	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glDisable(GL_DITHER);
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE);

	fbo_generate_mipmaps({ img1a, img1b, img1c });
	// I said, no glow for the GUI!
	if (config.vecglow && !emulator_is_gui_active()) useglow = 1;

	bind_shader(fragMulti);

	bleh = glGetUniformLocation(fragMulti, "mytex2"); glUniform1i(bleh, 1);
	bleh = glGetUniformLocation(fragMulti, "mytex3"); glUniform1i(bleh, 2);
	bleh = glGetUniformLocation(fragMulti, "mytex4"); glUniform1i(bleh, 3);

	set_uniform1i(fragMulti, "usefb", config.vectrail);

	set_uniform1i(fragMulti, "usefb", config.vectrail);
	set_uniform1i(fragMulti, "useglow", useglow);
	set_uniform1f(fragMulti, "glowamt", (float)(config.vecglow * 0.01));
	set_uniform1i(fragMulti, "brighten", gamenum);

	glActiveTexture(GL_TEXTURE1); set_texture(&img1b, 1, 1, 0, 0);
	glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, img3a); set_texture(&img3b, 1, 0, 0, 0);
	glActiveTexture(GL_TEXTURE3); set_texture(&img1c, 1, 0, 0, 0);

	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	//LOG_DEBUG("img4a into render: left=%d right=%d top=%d bottom=%d", left, right, top, bottom);
	drawTexturedQuad((float)left, (float)right, (float)bottom, (float)top, true);

	unbind_shader();

	glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, 0); glDisable(GL_TEXTURE_2D);
	glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, 0); glDisable(GL_TEXTURE_2D);
	glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, 0); glDisable(GL_TEXTURE_2D);
	glActiveTexture(GL_TEXTURE0);

	//--------------------------------------------------------------------------
	// LAYER 5B: VECTOR_USES_OVERLAY1 - colorize the CRT-only image in-place.
	//--------------------------------------------------------------------------
	if (config.overlay && art_loaded[1] && uses_overlay1)
	{
		//float overlay_height =  (Machine->drv->rotation & ORIENTATION_SWAP_XY) ? (float)bottom : ((float)bottom * 0.75f);

		glEnable(GL_TEXTURE_2D);
		set_texture(&art_tex[1], 1, 0, 0, 0);

		glEnable(GL_BLEND);
		if (Machine->drv->video_attributes & VIDEO_TYPE_RASTER_BW)
			glBlendFunc(GL_DST_COLOR, GL_ZERO);
		else
			glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR);

		glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

		drawTexturedQuad((float)left, (float)right, (float)top, (float) bottom, false);
	}

	//--------------------------------------------------------------------------
	// LAYER 5C: Composite to img4a (FBO4 attachment 0)
	//--------------------------------------------------------------------------
	set_render_fbo4();

	auto DrawCabinetScaledLayer = [&](GLuint tex, bool is_pre_squished) {
		if (!tex) return;
		glEnable(GL_TEXTURE_2D);
		set_texture(&tex, 1, 0, 0, 0);

		float base_h = 1024; //is_pre_squished ? 1024.0f : (1024.0f * 0.75f);

		if (config.artcrop) {
			float x1 = (float)bezelx;
			float y1 = (float)bezely;
			float x2 = 1024.0f * bezelzoom + bezelx;
			float y2 = base_h * bezelzoom + bezely;
			drawTexturedQuad(x1, x2, y1, y2, false);
		}
		else {
			drawTexturedQuad(0.0f, 1024.0f, 0.0f, base_h, false);
		}
		};

	if (config.artwork && art_loaded[0]) {
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glColor4f(0.5f, 0.5f, 0.5f, 1.0f);
		DrawCabinetScaledLayer(art_tex[0], false);
	}

	glDisable(GL_DITHER);
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE);
	glEnable(GL_TEXTURE_2D);
	set_texture(&img4b, 1, 0, 0, 0);

	// Base draw of the CRT image
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	FS_Rect(0, 1024);

	// --- TWEAK: CRT Brightness Boost over Artwork ---
	// Because drawing over a backdrop can visually wash out the soft vector glow,
	// we do a secondary additive pass to punch up the midtones of the game image.
	// Todo: if (config.vectrail == 0) adjust more
	if ((config.artwork && art_loaded[0]) || (config.overlay && art_loaded[1] && uses_overlay2))
	{
		// TWEAK THIS: 0.0f = no boost, 1.0f = double brightness.
		// Around 0.4f - 0.6f usually gives vectors enough punch against dark artwork.
		// TODO: Make this configurable per game, this sucks with certain artwork.
		float crt_boost = (config.artwork && art_loaded[0]) ? 0.2f : 0.25f;
		glColor4f(crt_boost, crt_boost, crt_boost, 1.0f);
		FS_Rect(0, 1024);
	}

	// VECTOR_USES_OVERLAY2 - visible overlay art on top of the CRT only.
	if (config.overlay && art_loaded[1] && uses_overlay2)
	{
		glEnable(GL_TEXTURE_2D);
		set_texture(&art_tex[1], 1, 0, 0, 0);

		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE_MINUS_SRC_ALPHA, GL_SRC_COLOR);
		glColor4f(1.0f, 1.0f, 1.0f, 0.5f);

		drawTexturedQuad((float)left, (float)right, (float)top, (float)bottom, false);
	}

	//--------------------------------------------------------------------------
	// LAYER 6: Bezel frame overlay
	//--------------------------------------------------------------------------
	if (config.bezel && art_loaded[3])
	{
		glEnable(GL_ALPHA_TEST);
		glDisable(GL_BLEND);
		glAlphaFunc(GL_GREATER, 0.2f);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		DrawCabinetScaledLayer(art_tex[3], false);

		glDisable(GL_DEPTH_TEST);
		glDisable(GL_ALPHA_TEST);
	}

	render_ui_overlays(1024, 768);

	end_render_fbo4();

	glDisable(GL_TEXTURE_2D);

	if (config.debug_profile_code)
	{
		auto end = std::chrono::steady_clock::now();
		auto diff = end - start;
		LOG_INFO("Profiler: final_render took %.3f ms",
			std::chrono::duration<double, std::milli>(diff).count());
	}
}

////////////////////////////////////////////////////////////////////////////////
// END RENDERING PIPELINE                                                      //
////////////////////////////////////////////////////////////////////////////////

// [vectrex-port] removed: render_scanlines() and final_render_raster().
//   render_scanlines() multiplied the g_scanrezTex scanline texture over the
//   raster FBO using aae::math ortho + the fragScanlineMultiply shader.
//   final_render_raster() composited the raster image (img5a) via
//   Layout_Render(*g_activeView, ...) (mame_layout) and presented it.
//   Both belong to the cut raster path and pulled in MathUtils.h, mame_layout.h
//   and the scanline texture, none of which the Vectrex vector pipeline needs.
