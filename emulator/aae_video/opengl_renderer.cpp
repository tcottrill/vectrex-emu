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
// [vectrex-port] The AAE core-profile OpenGL vector chain (AAE 9542500),
// trimmed to what a B/W vector display with an OVERLAY2 gel needs. Removed:
// Vulkan dispatch, the raster path (Fpoly, fbo_raster, mono/colour CRT
// monitors, scanline overlay), the colour-vector monitor blit, the vector font
// and UI overlays, the GUI starfield, and the Star Wars Fuzz state (uFuzz is
// always 0 here).
//
// Rendering pipeline overview:
//
//   STEP 1 - set_render()
//     Binds FBO1/img1a, sets 1024x1024 ortho. The emulator queues beam
//     geometry with add_line()/add_dot() (emu_vector_draw).
//
//   STEP 2 - render()
//     Draws the queued beams with beam_draw_all(), then calls final_render().
//
//   STEP 3 - final_render()
//     Composites all layers (game image, feedback trail, glow blur, overlay)
//     and writes the finished frame to FBO4. end_render_fbo4() then blits
//     FBO4 to the backbuffer, scaled to the window size and aspect ratio.
//
// FBO / Texture layout:
//   FBO1 - img1a (attachment 0): current frame render target (1024x1024)
//          img1b (attachment 1): feedback/trail accumulation buffers
//          img1c (attachment 2): AAE trail buffer (unused: see fbo_persist)
//   FBO2 - img2a: 512x512 downsampled image for glow blur pass 1
//   FBO3 - img3a (attachment 0): 256x256 pingpong blur target A
//          img3b (attachment 1): 256x256 pingpong blur target B
//   FBO4 - img4a: final composited frame, blitted to screen at window size
//          img4b: CRT image scratch (pre-backdrop)
//   fbo_pyr[0..4]: dual-filter glow pyramid (glow_filter=1)
//   fbo_persist  : img_persist[0/1], RGBA16F phosphor persistence ping-pong
//
// Artwork texture layout:
//   art_tex[0] - Backdrop (behind game screen)
//   art_tex[1] - Overlay  (color gel over game screen)
//   art_tex[3] - Bezel frame (rendered on top of everything)
//
//==========================================================================

#include "opengl_renderer.h"
#include "sys_gl.h"
#include "aae_mame_driver.h"
#include "aae_compat.h"        // [vectrex-port] WindowSetup/GetWindowSetup, emulator_is_gui_active
#include "texture_handler.h"   // [vectrex-port] trimmed local copy: art_tex[] + set_texture()
#include "gl_fbo.h"
#include "gl_texturing.h"
#include "gl_shader.h"
#include "vector_draw.h"
#include "MathUtils.h"
#include "shader_util.h"       // [vectrex-port] CompileShader / LinkShaderProgram (persistence pass)
#include "phosphor.h"          // [vectrex-port] phosphor_keep / phosphor_fade_seconds
#include <chrono>   // for optional frame-time profiling

// ---------------------------------------------------------------------------
// Module-level globals
// ---------------------------------------------------------------------------
// Calculated screen rectangle used to blit FBO4 to the window at the correct
// size and aspect ratio. Allocated in init_gl().
Rect2* screen_rect = nullptr;

// Projection mirrored from set_ortho for the core-profile quad shaders.
aae::math::mat4 g_proj;

// [vectrex-port] Phosphor persistence state (see render_phosphor_persistence).
static GLuint s_progPersist = 0;          // decay + max-combine program
static int    s_persist_cur = 0;          // img_persist[] index holding the latest image
static bool   s_persist_live = false;     // false: buffer stale, start from black
static float  s_frame_seconds = 0.02f;    // emulated time covered by this render

void set_frame_seconds(float seconds) { s_frame_seconds = seconds; }

static const char* kPersistVS = R"glsl(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
uniform mat4 uProj;
out vec2 TexCoord;
void main()
{
    TexCoord = aUV;
    gl_Position = uProj * vec4(aPos, 0.0, 1.0);
}
)glsl";

static const char* kPersistFS = R"glsl(
#version 330 core
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D uHistory;   // previous persistence image
uniform sampler2D uFrame;     // this frame (img1b)
uniform float uKeep;          // fraction of the history kept this frame
uniform float uCutoff;        // decayed history below this goes black
void main()
{
    vec3 hist = texture(uHistory, TexCoord).rgb * uKeep;
    if (!any(greaterThan(hist, vec3(uCutoff))))
        hist = vec3(0.0);
    FragColor = vec4(max(hist, texture(uFrame, TexCoord).rgb), 1.0);
}
)glsl";

// ---------------------------------------------------------------------------
// orientation_to_rect2_rotation
// Converts ORIENTATION_xxx flags (config.system_rotation) to the Rect2
// rotation index used by UpdateScreenRect():
//   0 = normal, 1 = rotate right (CW 90), 2 = rotate left (CCW 90), 3 = 180
// ---------------------------------------------------------------------------
static int orientation_to_rect2_rotation(int orientation)
{
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
// Called whenever the client area changes size. Updates screen_rect so the
// final blit tracks the new window dimensions.
// ---------------------------------------------------------------------------
void emulator_on_window_resize(int newW, int newH)
{
	(void)newW; (void)newH;
	if (!screen_rect) return;

	auto& ws = GetWindowSetup();
	int rot = orientation_to_rect2_rotation(config.system_rotation);
	screen_rect->UpdateScreenRect(ws.clientWidth, ws.clientHeight, ws.aspectRatio, rot);
	LOG_INFO("Window resized - new client area: %d x %d (rotation=%d)", ws.clientWidth, ws.clientHeight, rot);
}

// ---------------------------------------------------------------------------
// set_ortho
// Sets the viewport and a bottom-left-origin 2D ortho projection (g_proj) to
// the given dimensions. Used throughout the pipeline to switch between
// 1024x1024 (FBO space) and window-size (backbuffer) spaces.
// ---------------------------------------------------------------------------
void set_ortho(int width, int height)
{
	glViewport(0, 0, width, height);
	g_proj = aae::math::ortho(0.0f, (float)width, 0.0f, (float)height);
}

// ---------------------------------------------------------------------------
// init_gl
// One-time OpenGL initialization. Creates FBOs, compiles shaders and the beam
// renderer. Protected by a static flag so it is safe to call more than once.
// ---------------------------------------------------------------------------
int init_gl(void)
{
	static int init_one = 0;
	check_gl_error_named("init_gl start");
	if (!init_one)
	{
		// --- VSync control ---
		SetvSync(config.forcesync != 0);
		LOG_INFO("VSync %s (config.forcesync=%d).", config.forcesync ? "enabled" : "disabled", config.forcesync);

		// --- Base GL state ---
		set_ortho(1024, 768);
		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);

		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		// --- Screen rectangle (tracks window size and aspect ratio) ---
		auto& ws = GetWindowSetup();
		int rot = orientation_to_rect2_rotation(config.system_rotation);
		screen_rect = new Rect2(ws.clientWidth, ws.clientHeight, ws.aspectRatio, rot);

		// --- FBO allocation ---
		LOG_INFO("Initializing FBOs...");
		fbo_init();
		beam_init(1);          // ssaa = 1: fbo1 is a plain 1024 buffer

		// --- Shader compilation ---
		init_shader();
		s_progPersist = LinkShaderProgram(CompileShader(GL_VERTEX_SHADER, kPersistVS, "persist"),
		                                  CompileShader(GL_FRAGMENT_SHADER, kPersistFS, "persist"));

		glClear(GL_COLOR_BUFFER_BIT);
		LOG_INFO("OpenGL initialization complete.");

		init_one++;
	}
	check_gl_error_named("init_gl");
	return 1;
}

// ---------------------------------------------------------------------------
// end_gl
// Shutdown: release GL resources. Call once when the application exits,
// with the context still current.
// ---------------------------------------------------------------------------
void end_gl()
{
	beam_shutdown();
	glDeleteProgram(s_progPersist);
	s_progPersist = 0;
	fbo_shutdown();
	delete screen_rect;
	screen_rect = nullptr;
	LOG_INFO("AAE GL shutdown.");
}

// ---------------------------------------------------------------------------
// glcode_vector_hard_clear_fbo1
// Clears all three attachments of FBO1 (img1a, img1b, img1c) to black. Used
// when starting a new game to flush any leftover trail or feedback data.
// Saves and restores the previously bound FBO and viewport.
// ---------------------------------------------------------------------------
void glcode_vector_hard_clear_fbo1()
{
	if (!fbo1)
		return;

	GLint prevFbo = 0;
	GLint prevVP[4] = { 0, 0, 0, 0 };
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
	glGetIntegerv(GL_VIEWPORT, prevVP);

	glBindFramebuffer(GL_FRAMEBUFFER, fbo1);
	glViewport(0, 0, 1024, 1024);

	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_BLEND);
	glClearColor(0, 0, 0, 0);

	glDrawBuffer(GL_COLOR_ATTACHMENT0);
	glClear(GL_COLOR_BUFFER_BIT);

	glDrawBuffer(GL_COLOR_ATTACHMENT1);
	glClear(GL_COLOR_BUFFER_BIT);

	glDrawBuffer(GL_COLOR_ATTACHMENT2);
	glClear(GL_COLOR_BUFFER_BIT);

	s_persist_live = false;   // [vectrex-port] persistence restarts from black

	// Restore previous FBO and viewport.
	glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
	glViewport(prevVP[0], prevVP[1], prevVP[2], prevVP[3]);
}

// ---------------------------------------------------------------------------
// set_render_fbo4
// Binds FBO4 and prepares it for final compositing. All game image layers
// and the bezel are drawn here before end_render_fbo4() blits to the screen.
// ---------------------------------------------------------------------------
void set_render_fbo4()
{
	glBindFramebuffer(GL_FRAMEBUFFER, fbo4);
	glDrawBuffer(GL_COLOR_ATTACHMENT0);

	set_ortho(1024, 1024);

	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	glEnable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_DITHER);   // required for some older cards
}

// ---------------------------------------------------------------------------
// end_render_fbo4
// Unbinds FBO4 and blits img4a (the composited frame) to the backbuffer,
// scaled and positioned by screen_rect to match the window size and aspect.
// [vectrex-port] The colour-vector monitor shader / overlay branches are cut:
// the Vectrex is a B/W tube, so this is always the plain copy.
// ---------------------------------------------------------------------------
void end_render_fbo4()
{
	check_gl_error_named("end_render_fbo4 (enter)");

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDrawBuffer(GL_BACK);
	glActiveTexture(GL_TEXTURE0);

	// Clear the backbuffer so pillarbox/letterbox bars are always clean.
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	auto& ws = GetWindowSetup();
	set_ortho(ws.clientWidth, ws.clientHeight);

	glDisable(GL_BLEND);

	// Blit img4a to the screen. Blending disabled: this is a straight copy.
	set_texture(&img4a, 1, 0, 0, 0);
	screen_rect->Render(aae::math::value_ptr(g_proj));   // g_proj == the set_ortho above

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
static void copy_main_img_to_fbo2()
{
	fbo_generate_mipmaps({ img1b });

	glBindFramebuffer(GL_FRAMEBUFFER, fbo2);
	glDrawBuffer(GL_COLOR_ATTACHMENT0);
	set_ortho(512, 512);
	glDisable(GL_BLEND);

	set_texture(&img1b, 1, 0, 0, 0);
	glActiveTexture(GL_TEXTURE0);

	bind_shader(fragBlur);
	check_gl_error_named("copy_main_img_to_fbo2");
	set_uniform1i(fragBlur, "colorMap", 0);
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
static void copy_fbo2_to_fbo3()
{
	glBindFramebuffer(GL_FRAMEBUFFER, fbo3);

	// Clear both pingpong buffers before each frame.
	glDrawBuffer(GL_COLOR_ATTACHMENT1);
	glClearColor(0.0, 0.0, 0.0, 0.0);
	glClear(GL_COLOR_BUFFER_BIT);

	glDrawBuffer(GL_COLOR_ATTACHMENT0);
	set_ortho(256, 256);
	glDisable(GL_BLEND);

	check_gl_error_named("copy_fbo2_to_fbo3");

	bind_shader(fragBlur);
	set_uniform1i(fragBlur, "colorMap", 0);
	set_uniform1f(fragBlur, "width", 256.0f);
	set_uniform1f(fragBlur, "height", 256.0f);

	set_texture(&img2a, 1, 0, 0, 1);
	FS_Rect(0, 256);
	unbind_shader();
}

// ---------------------------------------------------------------------------
// render_blur_image_fbo3
// Blur step: pingpongs between img3a and img3b in fbo3 across 4 passes,
// each time drawing with a small offset (fshifta / fshiftb arrays) and
// additive blending to accumulate a soft glow.
//
// v1 and v2 control the near and far sample distances. Increasing them
// widens the glow at the cost of some precision.
// ---------------------------------------------------------------------------
static void render_blur_image_fbo3()
{
	static constexpr float v1 = 1.0f;  // near sample offset (pixels at 256x256)
	static constexpr float v2 = 2.0f;  // far sample offset

	// Global sub-pixel correction applied to all quads to keep the blurred
	// image centered relative to the source.
	const float globalOffsetX = -0.05f;
	const float globalOffsetY = -0.20f;

	// Each row's first pair (x0,y0) is the tap direction for one pingpong pass;
	// (x1,y1) is currently unused. Rows 0-3 are the axis taps (E/W/N/S); rows 4-7
	// are the diagonals (NE/SW/NW/SE).
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

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE);  // additive blend accumulates glow

	// Draw one offset quad. Converts float offsets to screen-space by adding
	// globalOffset and sizing to height3 (the FBO3 height, 256).
	auto DrawQuadOffset = [&](float ox, float oy) {
		float x1 = ox + globalOffsetX;
		float y1 = oy + globalOffsetY;
		float x2 = (float)height3 + x1;
		float y2 = (float)height3 + y1;
		// (left,right)=X span [x1,x2]; (bottom,top)=Y span [y2,y1].
		// y2 maps size+y1 down to y1, matching the orientation of FS_Rect(0,size).
		drawTexturedQuad(x1, x2, y2, y1, 1);
		};

	const int kBlurPasses = 4;   // rows 0-3: axis only

	int i = 0;

	for (int pass = 0; pass < kBlurPasses; ++pass)
	{
		// A -> B: draw img3a into attachment 1 (img3b) with near offset.
		glDrawBuffer(GL_COLOR_ATTACHMENT1);
		set_texture(&img3a, 1, 0, 0, 0);
		DrawQuadOffset(fshifta[i], fshifta[i + 1]);

		// B -> A: draw img3b into attachment 0 (img3a) with far offset.
		glDrawBuffer(GL_COLOR_ATTACHMENT0);
		set_texture(&img3b, 1, 0, 0, 0);
		DrawQuadOffset(fshiftb[i], fshiftb[i + 1]);

		i += 4;
	}

	check_gl_error_named("render_blur_image_fbo3");
	unbind_shader();
}

// ---------------------------------------------------------------------------
// render_blur_dualfilter - glow_filter=1.
//
// Dual-filter pyramid (Kawase/Bjorge): img3a (256) is downsampled
// 128 -> 64 -> 32 with a 5-tap kernel, then upsampled back 64 -> 128 -> 256
// with an 8-tap kernel, ending in img3b so the composite (fragMulti's mytex3)
// needs no changes.
//
// The classic accumulate blur's signature - hot core, long soft tail - is
// reproduced by re-injecting each down level during the matching up pass
// (kTail) and the unblurred 256 source at the end (kCore).
//
// Tuning: config.glow2_* ([video] glow2_* keys). Read per frame:
//   glow2_spread  tap radius scale inside every level    (default 1.0)
//   glow2_tail    down-level re-injection weight          (default 0.6)
//   glow2_core    unblurred-source weight in final pass   (default 1.0)
//   glow2_gain    final output gain                       (default 10.0)
//
// The gain default is NOT arbitrary: this chain is energy-preserving, so at
// gain 1.0 it feeds the glowamt composite a signal ~50x dimmer than the
// classic path's saturating accumulate blur - an invisible glow. The large
// gain + RGB8's natural clamp at 1.0 reproduces the classic look on purpose.
// ---------------------------------------------------------------------------
static void render_blur_dualfilter()
{
	const float kSpread = config.glow2_spread;
	const float kTail   = config.glow2_tail;
	const float kCore   = config.glow2_core;
	const float kGain   = config.glow2_gain;

	glDisable(GL_BLEND);   // pure overwrites

	// One pass: bind dst FBO, source texture(s), draw a full-target quad.
	auto down = [&](rfbo_t dstFbo, int dstSize, rtex_t srcTex) {
		glBindFramebuffer(GL_FRAMEBUFFER, dstFbo);
		glDrawBuffer(GL_COLOR_ATTACHMENT0);
		set_ortho(dstSize, dstSize);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, srcTex);
		set_uniform2f(fragDualDown, "uHalfPixel",
		              0.5f / (float)dstSize * kSpread,
		              0.5f / (float)dstSize * kSpread);
		FS_Rect(0, dstSize);
	};

	auto up = [&](rfbo_t dstFbo, GLenum dstAttach, int dstSize, int srcSize,
	              rtex_t srcTex, rtex_t addTex, float addWeight, float gain) {
		glBindFramebuffer(GL_FRAMEBUFFER, dstFbo);
		glDrawBuffer(dstAttach);
		set_ortho(dstSize, dstSize);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, addTex);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, srcTex);
		set_uniform2f(fragDualUp, "uHalfPixel",
		              0.5f / (float)srcSize * kSpread,
		              0.5f / (float)srcSize * kSpread);
		set_uniform1f(fragDualUp, "uAddWeight", addWeight);
		set_uniform1f(fragDualUp, "uGain", gain);
		FS_Rect(0, dstSize);
	};

	// img3a is trilinear and the first down pass minifies it 2:1, which would
	// sample a STALE mip (mips are generated after the blur). Drop it to plain
	// bilinear for the pyramid read; restored after the chain.
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, img3a);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

	// Down chain: img3a 256 -> 128 -> 64 -> 32.
	bind_shader(fragDualDown);
	set_uniform1i(fragDualDown, "uSrc", 0);
	down(fbo_pyr[0], 128, img3a);
	down(fbo_pyr[1],  64, img_pyr[0]);
	down(fbo_pyr[2],  32, img_pyr[1]);

	// Up chain: 32 -> 64 -> 128 -> 256 (img3b), re-adding detail on the way.
	bind_shader(fragDualUp);
	set_uniform1i(fragDualUp, "uSrc", 0);
	set_uniform1i(fragDualUp, "uAdd", 1);
	up(fbo_pyr[3],          GL_COLOR_ATTACHMENT0,  64, 32, img_pyr[2], img_pyr[1], kTail, 1.0f);
	up(fbo_pyr[4],          GL_COLOR_ATTACHMENT0, 128, 64, img_pyr[3], img_pyr[0], kTail, 1.0f);
	up(fbo3,                GL_COLOR_ATTACHMENT1, 256, 128, img_pyr[4], img3a,     kCore, kGain);

	// Restore img3a's trilinear MIN filter.
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, img3a);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);

	check_gl_error_named("render_blur_dualfilter");
	unbind_shader();
}

// ---------------------------------------------------------------------------
// [vectrex-port] Phosphor persistence, after Vectrexy's DarkenTexture pass.
//
//   persist = max(decayed history, this frame)     (RGBA16F ping-pong)
//   img1b   = persist                              (feeds glow + CRT combine)
//
// The history decays by phosphor_keep(dt): exponential in elapsed EMULATED
// time, so a 30 Hz game trails for the same time as a 50 Hz one, and pausing
// freezes the fade. Decayed history under kPhosphorCutoff snaps to black.
// Lines are combined with max, not added, so overlapping trails never
// brighten past the beam.
// ---------------------------------------------------------------------------
static void clear_phosphor_persistence()
{
	if (!fbo_persist) return;
	glBindFramebuffer(GL_FRAMEBUFFER, fbo_persist);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glDrawBuffer(GL_COLOR_ATTACHMENT0); glClear(GL_COLOR_BUFFER_BIT);
	glDrawBuffer(GL_COLOR_ATTACHMENT1); glClear(GL_COLOR_BUFFER_BIT);
}

static void render_phosphor_persistence()
{
	// Trail just turned on (or the game changed): don't resurrect an old image.
	if (!s_persist_live) {
		clear_phosphor_persistence();
		s_persist_live = true;
	}

	const int src = s_persist_cur;
	const int dst = 1 - src;
	const float keep = phosphor_keep(s_frame_seconds, phosphor_fade_seconds(config.vectrail));

	// persist[dst] = max(persist[src] * keep, img1b)
	glBindFramebuffer(GL_FRAMEBUFFER, fbo_persist);
	glDrawBuffer(GL_COLOR_ATTACHMENT0 + dst);
	set_ortho(1024, 1024);
	glDisable(GL_BLEND);

	glUseProgram(s_progPersist);
	set_uniform1i(s_progPersist, "uHistory", 0);
	set_uniform1i(s_progPersist, "uFrame", 1);
	set_uniform1f(s_progPersist, "uKeep", keep);
	set_uniform1f(s_progPersist, "uCutoff", kPhosphorCutoff);
	glActiveTexture(GL_TEXTURE1); set_texture(&img1b, 1, 0, 0, 0);
	glActiveTexture(GL_TEXTURE0); set_texture(&img_persist[src], 1, 0, 0, 0);
	FS_Rect(0, 1024);
	glUseProgram(0);
	glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);

	// img1b = persist[dst], a straight copy for the glow and CRT combine.
	glBindFramebuffer(GL_FRAMEBUFFER, fbo1);
	glDrawBuffer(GL_COLOR_ATTACHMENT1);
	set_texture(&img_persist[dst], 1, 0, 0, 0);
	FS_Rect(0, 1024);
	glEnable(GL_BLEND);

	s_persist_cur = dst;
	check_gl_error_named("render_phosphor_persistence");
}

////////////////////////////////////////////////////////////////////////////////
// RENDERING PIPELINE - STEPS 1, 2, and 3                                    //
////////////////////////////////////////////////////////////////////////////////

// ---------------------------------------------------------------------------
// set_render [STEP 1]
// Binds FBO1 (img1a) at 1024x1024 with Y-up ortho and clears it.
// ---------------------------------------------------------------------------
void set_render()
{
	glBindFramebuffer(GL_FRAMEBUFFER, fbo1);
	glDrawBuffer(GL_COLOR_ATTACHMENT0);
	set_ortho(1024, 1024);

	// Only clear the frame if the game is actively running.
	if (!paused)
	{
		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT);
	}

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	check_gl_error_named("set_render");
}

// ---------------------------------------------------------------------------
// render [STEP 2]
// Draws the beam geometry queued since cache_clear() into img1a, then
// composites. [vectrex-port] The caller (osint_render) queues geometry with
// add_line()/add_dot() between set_render() and render(), in place of AAE's
// mame_vector vector_update()/vector_clear_list().
// ---------------------------------------------------------------------------
void render()
{
	if (!paused)
	{
		aae::math::mat4 proj = aae::math::ortho(0.0f, 1024.0f, 0.0f, 1024.0f);
		beam_draw_all(proj);
	}

	// ALWAYS composite the layers.
	final_render(game_rect_left, game_rect_right, game_rect_bottom, game_rect_top);
}

// ---------------------------------------------------------------------------
// final_render [STEP 3]
// Composites all rendering layers into FBO4 and presents the result.
//
// Parameters define the game screen rectangle in 1024-space.
//
// Layer order (back to front):
//   1. img1a -> img1b : copy current frame
//   3. img1b -> persist -> img1b : phosphor persistence (if enabled)
//   4. FBO2/3 blur    : glow downsample+blur passes (if enabled)
//   5. fragMulti shader: composites img1b + blur + trail in one pass
//   5C. backdrop + CRT image + OVERLAY2 gel into img4a
//   6. Bezel frame    : art_tex[3] drawn on top with alpha test (if enabled)
// ---------------------------------------------------------------------------
void final_render(int left, int right, int bottom, int top)
{
	const int vattr = (Machine && Machine->drv) ? Machine->drv->video_attributes : 0;
	const bool uses_overlay1 = (vattr & VECTOR_USES_OVERLAY1) != 0;
	const bool uses_overlay2 = (vattr & VECTOR_USES_OVERLAY2) != 0;

	GLint bleh = 0;
	int   useglow = 0;

	auto start = std::chrono::steady_clock::now();

	//--------------------------------------------------------------------------
	// LAYER 1: Copy img1a (current frame) into img1b.
	//--------------------------------------------------------------------------
	glBindFramebuffer(GL_FRAMEBUFFER, fbo1);
	glDrawBuffer(GL_COLOR_ATTACHMENT1);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	set_texture(&img1a, 1, 0, 0, 1);

	if (vattr & VIDEO_TYPE_RASTER_BW)
		glBlendFunc(GL_ONE, GL_ZERO);
	else
		glBlendFunc(GL_ONE, GL_ONE);

	// Always do a pure 1:1 copy for the FBO buffers!
	FS_Rect(0, 1024);

	//--------------------------------------------------------------------------
	// LAYER 3: Phosphor persistence (img1b -> persistence buffer -> img1b).
	// [vectrex-port] Replaces AAE's img1c trail (a fixed per-render fade added
	// at 0.25) with Vectrexy-style persistence: the image itself fades by
	// elapsed emulated time, and the glow downsample and CRT combine both read
	// the faded result from img1b.
	//--------------------------------------------------------------------------
	if (config.vectrail && !emulator_is_gui_active())
		render_phosphor_persistence();
	else
		s_persist_live = false;

	//--------------------------------------------------------------------------
	// LAYER 4: Glow blur passes (FBO2 and FBO3).
	//--------------------------------------------------------------------------
	if (config.vecglow && !emulator_is_gui_active())
	{
		// config.glow_filter: 0 = classic 8-pass accumulate blur (default),
		// 1 = dual-filter pyramid. Read live; log only on change.
		static int s_loggedFilter = -1;
		if (s_loggedFilter != config.glow_filter) {
			s_loggedFilter = config.glow_filter;
			LOG_INFO("Glow blur path: %s (glow_filter=%d)",
			         s_loggedFilter == 1 ? "dual-filter pyramid" : "classic accumulate",
			         s_loggedFilter);
		}

		copy_main_img_to_fbo2();
		copy_fbo2_to_fbo3();
		if (config.glow_filter == 1)
		{
			render_blur_dualfilter();
			// img2a only: next frame's 512->256 trilinear downsample needs fresh
			// mips; the pyramid samples img3a at level 0 and the composite
			// magnifies img3b.
			fbo_generate_mipmaps({ img2a });
		}
		else
		{
			render_blur_image_fbo3();
			fbo_generate_mipmaps({ img2a, img3a, img3b });
		}
	}

	//--------------------------------------------------------------------------
	// LAYER 5A: Build the CRT/game image into img4b (FBO4 attachment 1).
	//--------------------------------------------------------------------------
	glBindFramebuffer(GL_FRAMEBUFFER, fbo4);
	glDrawBuffer(GL_COLOR_ATTACHMENT1);
	set_ortho(1024, 1024);

	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_DEPTH_TEST);

	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	glDisable(GL_DITHER);
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE);

	fbo_generate_mipmaps({ img1a, img1b, img1c });
	if (config.vecglow && !emulator_is_gui_active()) useglow = 1;

	bind_shader(fragMulti);

	bleh = glGetUniformLocation(fragMulti, "mytex2"); glUniform1i(bleh, 1);
	bleh = glGetUniformLocation(fragMulti, "mytex3"); glUniform1i(bleh, 2);
	bleh = glGetUniformLocation(fragMulti, "mytex4"); glUniform1i(bleh, 3);

	// [vectrex-port] Persistence already lives in img1b (LAYER 3), so the
	// shader's separate img1c feedback layer stays off.
	set_uniform1i(fragMulti, "usefb", 0);
	set_uniform1i(fragMulti, "useglow", useglow);
	set_uniform1f(fragMulti, "glowamt", (float)(config.vecglow * 0.01));
	set_uniform1i(fragMulti, "brighten", gamenum);
	// [vectrex-port] Star Wars Death Star defocus only; the shader's uFuzz > 0
	// branch leaves every other game untouched.
	set_uniform1f(fragMulti, "uFuzz", 0.0f);

	glActiveTexture(GL_TEXTURE1); set_texture(&img1b, 1, 1, 0, 0);
	glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, img3a); set_texture(&img3b, 1, 0, 0, 0);
	glActiveTexture(GL_TEXTURE3); set_texture(&img1c, 1, 0, 0, 0);

	drawTexturedQuad((float)left, (float)right, (float)bottom, (float)top, true);

	unbind_shader();

	glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);

	//--------------------------------------------------------------------------
	// LAYER 5B: VECTOR_USES_OVERLAY1 - colorize the CRT-only image in-place.
	//--------------------------------------------------------------------------
	if (config.overlay && art_loaded[1] && uses_overlay1)
	{
		set_texture(&art_tex[1], 1, 0, 0, 0);

		glEnable(GL_BLEND);
		if (vattr & VIDEO_TYPE_RASTER_BW)
			glBlendFunc(GL_DST_COLOR, GL_ZERO);
		else
			glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR);

		drawTexturedQuad((float)left, (float)right, (float)top, (float)bottom, false);
	}

	//--------------------------------------------------------------------------
	// LAYER 5C: Composite to img4a (FBO4 attachment 0)
	//--------------------------------------------------------------------------
	set_render_fbo4();

	auto DrawCabinetScaledLayer = [&](GLuint tex,
		float rT = 1.0f, float gT = 1.0f, float bT = 1.0f, float aT = 1.0f, float alphaTest = 0.0f) {
			if (!tex) return;
			set_texture(&tex, 1, 0, 0, 0);

			float base_h = 1024;

			if (config.artcrop) {
				float x1 = (float)bezelx;
				float y1 = (float)bezely;
				float x2 = 1024.0f * bezelzoom + bezelx;
				float y2 = base_h * bezelzoom + bezely;
				drawTexturedQuad(x1, x2, y1, y2, false, rT, gT, bT, aT, alphaTest);
			}
			else {
				drawTexturedQuad(0.0f, 1024.0f, 0.0f, base_h, false, rT, gT, bT, aT, alphaTest);
			}
		};

	if (config.artwork && art_loaded[0]) {
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		DrawCabinetScaledLayer(art_tex[0], 0.5f, 0.5f, 0.5f, 1.0f);
	}

	glDisable(GL_DITHER);
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE);
	set_texture(&img4b, 1, 0, 0, 0);

	// Base draw of the CRT image
	FS_Rect(0, 1024);

	// CRT brightness boost over artwork: drawing over a backdrop/overlay washes
	// out the soft vector glow, so a secondary additive pass punches up the
	// midtones of the game image.
	if ((config.artwork && art_loaded[0]) || (config.overlay && art_loaded[1] && uses_overlay2))
	{
		float crt_boost = (config.artwork && art_loaded[0]) ? 0.2f : 0.25f;
		FS_Rect(0, 1024, crt_boost, crt_boost, crt_boost, 1.0f);
	}

	// VECTOR_USES_OVERLAY2 - visible overlay art on top of the CRT only.
	if (config.overlay && art_loaded[1] && uses_overlay2)
	{
		set_texture(&art_tex[1], 1, 0, 0, 0);

		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE_MINUS_SRC_ALPHA, GL_SRC_COLOR);

		drawTexturedQuad((float)left, (float)right, (float)top, (float)bottom, false, 1.0f, 1.0f, 1.0f, 0.5f);
	}

	//--------------------------------------------------------------------------
	// LAYER 6: Bezel frame overlay
	//--------------------------------------------------------------------------
	if (config.bezel && art_loaded[3])
	{
		glDisable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		// Hard alpha cutoff via shader discard (replaces fixed-function GL_ALPHA_TEST).
		DrawCabinetScaledLayer(art_tex[3], 1.0f, 1.0f, 1.0f, 1.0f, 0.2f);
	}

	// [vectrex-port] removed: render_ui_overlays() (vector font menus/PAUSED).

	end_render_fbo4();

	if (config.debug_profile_code)
	{
		auto end = std::chrono::steady_clock::now();
		auto diff = end - start;
		LOG_INFO("Profiler: final_render took %.3f ms",
			std::chrono::duration<double, std::milli>(diff).count());
	}
}
