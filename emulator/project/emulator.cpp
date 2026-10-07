// Vectrex-Emu
// Copyright (C) 2026 Tim Cottrill and Claude Code
//
// Derived from vecx (Valavan Manohararajah) and VecXGL (James Higgs);
// the original project citations are preserved immediately below.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

// Original Citations for VecX-GL and vecx Below:

// VecXGL 1.2 (SDL/Win32 and SDL/Linux)
//
// This is a port of the vectrex emulator "vecx", by Valavan Manohararajah.
// Portions of this code copyright James Higgs 2005/2007.
// These portions are:
// 1. Ay38910 PSG (audio) emulation wave-buffering code.
// 2. Drawing of vectors using OpenGL.
//
// Comand-line parsing code gratefully borrowed from vecxsdl (Thomas Mathys).
// Key mapping and command-line options were also changed
// to be compatible with Thomas Mathys' vecxsdl.
//
// Other vecx ports by JH:
// - VecXPS2 (Playsyation 2)
// - VecXWin32 (Windows/DirectX) (unreleased)

#include "vecx.h"
#include "cpu_control.h"   // g_cpu_mem (shared 64K address space)
#include "bios.h"						// bios rom data
#include "deftypes.h"
#include "sys_log.h"
#include "framework.h"
#include "rawinput.h"
#include "input_config.h"   // configurable P1/P2 keyboard + gamepad bindings

#include <stdlib.h>
#include <stdio.h>
#include "wintimer.h"
#include "ay8910.h"   // AAE AY-3-8910 core (cpu_m6809 build)
#include "mixer.h"
#include "aae_fileio.h"   // load_sample_core (ambient flyback sample)
#include "emulator.h"

// [vectrex-port] AAE beam renderer + glow pipeline (bloom + phosphor trail + OVERLAY2 gel).
#include "opengl_renderer.h"   // init_gl, set_render, render, emulator_on_window_resize, glcode_vector_hard_clear_fbo1
#include "emu_vector_draw.h"   // add_line, add_dot, cache_clear
#include "aae_mame_driver.h"   // config, art_loaded[], game_rect_*
#include "framework.h"         // GLEW + SCREEN_W / SCREEN_H
#include "texture_handler.h"   // art_tex[], art_loaded[] (OVERLAY2 gel slot)
#include "stb_image.h"         // overlay decode (implementation lives in sys_texture.cpp)
#include <string>
#include <cstdint>
#include <unordered_set>

// [vectrex-port] defined with the video-effect controls near the end of this file.
void emulator_apply_glow(void);

double millsec = 0;
double gametime = 0;
double starttime = 0;
#define BUFFER_SIZE 22050/50
unsigned char* ast_soundbuffer = NULL;

// [vectrex-port] Emulator frame rate = the host's present rate (monitor refresh).
// cycles/frame scales with this so total cycles/sec stays == VECTREX_MHZ (authentic
// speed). Set by the host via emulator_set_frame_rate() before the loop starts.
double g_emu_fps = 50.0;

// [vectrex-port] osint_render() fires from inside vecx_emu() once per Vectrex game
// frame (the BIOS re-arming VIA timer 2: 50 Hz for nearly every cart; see
// vecx.cpp). When we present faster (e.g. 60 Hz), some presents run a
// cycle chunk that completes no Vectrex frame -> osint_render() doesn't run. This
// flag lets emulator_run() detect that and re-blit the last composited frame
// instead of swapping to a stale/empty back buffer (which caused 60 Hz flicker).
static bool s_rendered_this_run = false;

static const char* version = "1.2";

#define EMU_TIMER			20			// milliseconds per frame
#define DEFAULT_WIDTH		330
#define DEFAULT_HEIGHT		410
#define DEFAULT_LINEWIDTH	1.0f
#define DEFAULT_OVERLAYTRANSPARENCY	0.5f

static const char* appname = "vecx";
static long screen_x = DEFAULT_WIDTH;
static long screen_y = DEFAULT_HEIGHT;
static long scl_factor;
static long bytes_per_pixel;

GLfloat color_set[VECTREX_COLORS];

double sclx, scly;

static int osint_defaults(void)
{
	screen_x = DEFAULT_WIDTH;
	screen_y = DEFAULT_HEIGHT;

	// Update ALG Scaling
	sclx = ALG_MAX_X / screen_x;
	scly = ALG_MAX_Y / screen_y;

	if (sclx > scly) {
		scl_factor = sclx;
	}
	else {
		scl_factor = scly;
	}

	// JH - built-in BIOS -> 0xE000-0xFFFF of the shared 64K address space.
	memcpy(&g_cpu_mem[0xE000], bios_data, bios_data_size);
	LOG_INFO("ROM load: built-in BIOS %u bytes -> g_cpu_mem[E000]; reset-vector $FFFE/F = $%02X%02X",
		bios_data_size, g_cpu_mem[0xFFFE], g_cpu_mem[0xFFFF]);

	/* Leave the cart empty: with no cartridge present the built-in BIOS boots its
	   own Minestorm, so no separate cart image is needed. A real cart is loaded
	   later via emulator_load_cart() (File > Load ROM, or -rom). */
	memset(&g_cpu_mem[0x0000], 0, 0x8000);
	LOG_INFO("CART load: none (empty cart -> built-in BIOS Minestorm)");
	return 0;
}

// [vectrex-port] throttle_speed() / play_streamed_sample() / fillsoundbuffer()
// removed: frame pacing now lives in the host (FrameLimiter), and audio is driven
// by the AAE AY core (ay8910_sh_update) instead of the old manual ay8910_render +
// stream_update path.

static void osint_maskinfo(int mask, int* shift, int* precision)
{
	*shift = 0;

	while ((mask & 1L) == 0) {
		mask >>= 1;
		(*shift)++;
	}

	*precision = 0;

	while ((mask & 1L) != 0) {
		mask >>= 1;
		(*precision)++;
	}
}

static void osint_gencolors(void)
{
	int c;
	int rcomp, gcomp, bcomp;

	for (c = 0; c < VECTREX_COLORS; c++) {
		rcomp = c * 256 / VECTREX_COLORS;
		gcomp = c * 256 / VECTREX_COLORS;
		bcomp = c * 256 / VECTREX_COLORS;

		color_set[c] = (GLfloat)c / 128;
		if (color_set[c] > 1.0f) color_set[c] = 1.0f;
	}
}

static void osint_clearscreen(void)
{
	// Clear color buffer
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);
}

void osint_render(void)
{
	s_rendered_this_run = true;   // a Vectrex frame completed -> a fresh image is drawn

	// [vectrex-port] Drive the AAE beam renderer + glow pipeline. Vectrex beam
	// coordinates map into the renderer's 1024x1024 Y-up FBO space; the beam is
	// monochrome (white) with per-vector brightness carried as 'intensity'.
	// render() draws the coverage-AA beams, then final_render() applies the
	// phosphor trail, glow bloom and OVERLAY2 color gel and blits the composite
	// to the backbuffer. The host loop calls GLSwapBuffers() afterwards.

	// Keep the AAE present rectangle in sync with the host window size.
	static int s_lastW = -1, s_lastH = -1;
	if (SCREEN_W != s_lastW || SCREEN_H != s_lastH) {
		emulator_on_window_resize(SCREEN_W, SCREEN_H);
		s_lastW = SCREEN_W;
		s_lastH = SCREEN_H;
	}

	cache_clear();   // drop last frame's accumulated geometry
	set_render();    // bind FBO1, 1024x1024 Y-up ortho, clear

	const double scaleX = 1024.0 / (double)ALG_MAX_X;   // 33000
	const double scaleY = 1024.0 / (double)ALG_MAX_Y;   // 41000

	// Classify each vector. Beam Y is fed directly (AAE's FBO->screen blit already
	// flips vertically). A zero-length vector is only a STANDALONE dot if it does
	// not sit on a line-segment endpoint -- on moving objects the beam pauses at
	// each corner, emitting a zero-length vector right on a line vertex (a "cap"),
	// which the beam renderer already rounds with its own join/end-cap discs.
	// Sizing those by the dot slider is wrong, so we skip them. Pass 1 records
	// line endpoints; pass 2 keeps only the dots that are not on one.
	static std::unordered_set<uint64_t> lineEnds;
	lineEnds.clear();
	auto packPt = [](long x, long y) -> uint64_t {
		return ((uint64_t)(uint32_t)x << 32) | (uint32_t)y;
		};

	// Pass 1: real line segments -> beam lines; record both endpoints.
	for (long v = 0; v < vector_draw_cnt; v++) {
		if (vectors_draw[v].x0 == vectors_draw[v].x1 &&
			vectors_draw[v].y0 == vectors_draw[v].y1)
			continue; // zero-length -> pass 2

		int intensity = (int)vectors_draw[v].color << 1;   // 0..127 brightness -> 0..255
		if (intensity > 255) intensity = 255;
		add_line((float)(vectors_draw[v].x0 * scaleX), (float)(vectors_draw[v].y0 * scaleY),
			(float)(vectors_draw[v].x1 * scaleX), (float)(vectors_draw[v].y1 * scaleY),
			intensity, 0xFFFFFFFFu);
		lineEnds.insert(packPt(vectors_draw[v].x0, vectors_draw[v].y0));
		lineEnds.insert(packPt(vectors_draw[v].x1, vectors_draw[v].y1));
	}

	// Pass 2: zero-length vectors NOT on a line endpoint are true standalone dots,
	// drawn as round beam discs at the dot-size slider.
	const float dot_size = emulator_get_dot_size();
	for (long v = 0; v < vector_draw_cnt; v++) {
		if (!(vectors_draw[v].x0 == vectors_draw[v].x1 &&
			vectors_draw[v].y0 == vectors_draw[v].y1))
			continue; // line -> pass 1
		if (lineEnds.find(packPt(vectors_draw[v].x0, vectors_draw[v].y0)) != lineEnds.end())
			continue; // cap on a line endpoint -> the beam's end-cap covers it

		int intensity = (int)vectors_draw[v].color << 1;
		if (intensity > 255) intensity = 255;
		add_dot((float)(vectors_draw[v].x0 * scaleX), (float)(vectors_draw[v].y0 * scaleY),
			intensity, 0xFFFFFFFFu, dot_size);
	}

	// Draw the beams into FBO1, then composite phosphor trail + glow bloom +
	// OVERLAY2 gel -> FBO4 -> backbuffer.
	render();
}

// Runs exactly ONE emulated frame, then returns to the host loop (winmain),
// which presents the rendered vectors via GLSwapBuffers(). Returns 1 to keep
// running, 0 when the user requests quit (ESC). vecx_reset() is NOT called here
// any more -- it runs once in emulator_init(), so we don't reset every frame.
int emulator_run()
{
	// --- Per-frame input. Bindings come from the Controller Configuration
	//     dialog (controls.ini); each player merges keyboard + that player's
	//     gamepad. P1 -> analog ch0/ch1 + button bits 0-3; P2 -> ch2/ch3 +
	//     button bits 4-7. snd_regs[14] is active-low (1 = released). ---
	input_poll_begin();
	InputPlayerState p1 = input_player_state(0);
	InputPlayerState p2 = input_player_state(1);

	snd_regs[14] = 0xff;                                   // all 8 buttons released
	alg_jch0 = alg_jch1 = alg_jch2 = alg_jch3 = 0x80;     // both sticks centered

	if (p1.left)  alg_jch0 = 0x00;
	if (p1.right) alg_jch0 = 0xFF;
	if (p1.up)    alg_jch1 = 0xFF;
	if (p1.down)  alg_jch1 = 0x00;
	if (p1.b1) snd_regs[14] &= ~0x01;
	if (p1.b2) snd_regs[14] &= ~0x02;
	if (p1.b3) snd_regs[14] &= ~0x04;
	if (p1.b4) snd_regs[14] &= ~0x08;

	if (p2.left)  alg_jch2 = 0x00;
	if (p2.right) alg_jch2 = 0xFF;
	if (p2.up)    alg_jch3 = 0xFF;
	if (p2.down)  alg_jch3 = 0x00;
	if (p2.b1) snd_regs[14] &= ~0x10;
	if (p2.b2) snd_regs[14] &= ~0x20;
	if (p2.b3) snd_regs[14] &= ~0x40;
	if (p2.b4) snd_regs[14] &= ~0x80;

	// ESC asks the host to shut down.
	if (key[KEY_ESC]) return 0;

	// Advance one VIDEO frame of CPU cycles. The frame rate follows the host's
	// present rate (monitor refresh); cycles/frame scale so total cycles/sec stays
	// == VECTREX_MHZ -> game speed stays authentic and motion is sampled at the
	// display rate (no 50-on-60 cadence judder). osint_render() is invoked from
	// inside vecx_emu() at the frame boundary.
	const long frame_cycles = (long)((double)VECTREX_MHZ / g_emu_fps);
	s_rendered_this_run = false;
	vecx_emu(frame_cycles);

	// If no Vectrex frame completed during this present interval, osint_render()
	// did not run -- re-blit the last composited frame so the buffer swap shows it
	// again instead of stale/empty content (otherwise we flicker at > 50 Hz).
	if (!s_rendered_this_run) {
		end_render_fbo4();
	}

	// vecx_emu submits complete audio blocks from CPU cycles, independently of
	// this video's present cadence.

	// Heartbeat: roughly once per second, prove the CPU is executing and the
	// analog engine is producing vectors.
	static unsigned long s_frame = 0;
	unsigned long hb = (unsigned long)g_emu_fps; if (hb == 0) hb = 50;
	if ((++s_frame % hb) == 0UL) {
		LOG_INFO("frame %lu: PC=$%04X S=$%04X  vectors(draw=%ld last=%ld)",
			s_frame, vecx_cpu_pc(), vecx_cpu_s(), vector_draw_cnt, vector_erse_cnt);
	}

	// Frame pacing is handled by the host (FrameLimiter), not here.
	return 1;
}

// [vectrex-port] ----- Ambient sound (looping flyback buzz) -------------------
// Optional, emulator-wide. Loaded from data/samples/flyback.wav and played on a
// reserved mixer channel (the 17..19 ambient-FX range). Its channel volume is
// independent of the master volume (which scales everything on top). Declared
// here (above emulator_init/emulator_end) so both can use it.
static int  s_ambient_sample = -1;     // mixer sample id (-1 = not loaded)
static int  s_ambient_chan = -1;     // reserved mixer channel (-1 = none)
static bool s_ambient_on = false;
static int  s_ambient_vol = 20;    // 0..100

static void emulator_ambient_init(void)
{
	s_ambient_sample = load_sample_core("", "flyback.wav", "data/samples/flyback.wav");
	if (s_ambient_sample >= 0)
		s_ambient_chan = mixer_alloc_channel(MIXER_FIRST_RESERVED_CHANNEL, 20);
	LOG_INFO("ambient: flyback sample=%d chan=%d", s_ambient_sample, s_ambient_chan);
}

int emulator_has_ambient(void) { return (s_ambient_sample >= 0 && s_ambient_chan >= 0) ? 1 : 0; }

int emulator_get_ambient_enabled(void) { return s_ambient_on ? 1 : 0; }
void emulator_set_ambient_enabled(int on)
{
	if (!emulator_has_ambient()) { s_ambient_on = false; return; }
	s_ambient_on = (on != 0);
	if (s_ambient_on) {
		sample_start(s_ambient_chan, s_ambient_sample, 1);          // loop forever
		sample_set_volume_percent(s_ambient_chan, s_ambient_vol);
	}
	else {
		sample_stop(s_ambient_chan);
	}
}

int emulator_get_ambient_volume(void) { return s_ambient_vol; }
void emulator_set_ambient_volume(int pct)
{
	if (pct < 0)   pct = 0;
	if (pct > 100) pct = 100;
	s_ambient_vol = pct;
	if (s_ambient_on && s_ambient_chan >= 0)
		sample_set_volume_percent(s_ambient_chan, pct);
}

void emulator_end()
{
	// Stop the AY core's mixer stream, then shut down the audio worker thread and
	// release the XAudio2 backend cleanly. Without mixer_end() the audio thread
	// runs into CRT teardown and faults on globals -- a 0xC0000409 fast-fail.
	if (s_ambient_chan >= 0) sample_stop(s_ambient_chan);
	ay8910_sh_stop();
	mixer_end();
}

// [vectrex-port] Set the emulation/present frame rate (the host passes the monitor
// refresh). cycles/frame = VECTREX_MHZ / fps, so total cycles/sec stays constant
// and game speed is unaffected; only the present cadence changes.
void emulator_set_frame_rate(double hz)
{
	if (hz < 30.0)  hz = 30.0;
	if (hz > 480.0) hz = 480.0;
	g_emu_fps = hz;
	LOG_INFO("emulator: frame rate %.2f fps (%ld cycles/frame)",
		hz, (long)((double)VECTREX_MHZ / hz));
}

//========================================================================
// main()
//========================================================================

void emulator_init(int argc, char** argv)
{
	(void)argc; (void)argv;   // command-line args are parsed host-side now

	// Set up default scale factors. The built-in cart boots by default; the host
	// loads any -rom via the load_rom seam and overlays via load_overlay.
	if (osint_defaults()) {
		exit(0);
	}

	/* determine a set of colors to use based */
	osint_gencolors();

	LOG_INFO("Scale X: %f Scale Y: %f", sclx, scly);

	// Audio: AAE AY-3-8910 core. mixer_init at 44.1 kHz / 50 fps; ay8910_sh_start
	// allocates its own 16-bit mixer stream (1 chip clocked at the 1.5 MHz Vectrex
	// CPU rate). vecx_emu renders and submits a block every 30,000 CPU cycles.
	mixer_init(44100, 50);
	AY8910Config ay_cfg = {};
	ay_cfg.num_chips = 1;
	ay_cfg.base_clock = VECTREX_MHZ;   // 1.5 MHz
	ay_cfg.mixing_level[0] = 255;
	ay8910_sh_start(&ay_cfg);

	// [vectrex-port] Load the optional ambient flyback loop (off until enabled).
	emulator_ambient_init();

	// [vectrex-port] Bring up the AAE vector-glow renderer (FBOs + shaders +
	// screen rect). The GL context and GLEW are already initialized by the host
	// shell before emulator_init() runs. Hard-clear FBO1 so no stale trail shows.
	init_gl();
	glcode_vector_hard_clear_fbo1();
	emulator_apply_glow();   // set config.vecglow from the default glow amount

	// Reset the Vectrex hardware ONCE, after ROM/cart are loaded and audio is up.
	// (Previously this lived at the top of emulator_run() which, before the
	//  per-frame refactor, was only entered once -- now it must be here.)
	vecx_reset();

	// [vectrex-port] The built-in default cart is Minestorm; show its overlay gel
	// from the artwork/ folder so OVERLAY2 is visible out of the box. Loading a
	// cart later swaps the overlay (see emulator_load_cart).
	emulator_load_overlay("data/artwork/minestorm.png");
}

// Load a cartridge image into g_cpu_mem[0x0000..0x7FFF] (over the empty cart) and reset.
// Bounded by sizeof(cart); shorter files leave the remainder zeroed.
void emulator_load_cart(const char* utf8_path)
{
	if (utf8_path == NULL) return;

	FILE* f = fopen(utf8_path, "rb");
	if (f == NULL) {
		LOG_ERROR("CART load: could NOT open '%s'", utf8_path);
		return;
	}

	// Load the cart image into 0x0000-0x7FFF of the shared 64K space (32K max).
	memset(&g_cpu_mem[0x0000], 0, 0x8000);
	size_t n = fread(&g_cpu_mem[0x0000], 1, 0x8000, f);
	fclose(f);

	LOG_INFO("CART load: %u bytes from '%s' (cart[0..3]= %02X %02X %02X %02X)",
		(unsigned)n, utf8_path, g_cpu_mem[0], g_cpu_mem[1], g_cpu_mem[2], g_cpu_mem[3]);

	vecx_reset();

	// [vectrex-port] Auto-load a matching overlay from the artwork/ folder by the
	// ROM's base filename, e.g. berzerk.bin -> artwork/berzerk.png (fopen is
	// case-insensitive on Windows). Fall back to <romname>.png beside the cart.
	// If neither exists, clear any previous overlay so a new game does not inherit
	// the old color gel.
	{
		std::string p(utf8_path);
		size_t slash = p.find_last_of("/\\");
		std::string fname = (slash == std::string::npos) ? p : p.substr(slash + 1);
		size_t dot = fname.find_last_of('.');
		std::string base = (dot == std::string::npos) ? fname : fname.substr(0, dot);

		std::string art = "data/artwork/" + base + ".png";     // preferred location
		std::string beside = (dot == std::string::npos ? p : p.substr(0, p.find_last_of('.'))) + ".png";

		FILE* of = fopen(art.c_str(), "rb");
		if (of) { fclose(of); emulator_load_overlay(art.c_str()); }
		else if ((of = fopen(beside.c_str(), "rb")) != NULL) { fclose(of); emulator_load_overlay(beside.c_str()); }
		else { emulator_clear_overlay(); }
	}
}

void emulator_reset(void)
{
	LOG_INFO("emulator_reset: vecx_reset()");
	vecx_reset();
}

// [vectrex-port] ----- Overlay (OVERLAY2 color gel) + video-effect toggles -----

// Load a translucent overlay image into the AAE OVERLAY2 slot (art_tex[1]).
// NOTE: if the overlay shows upside down, set stbi_set_flip_vertically_on_load(1)
// before stbi_load (AAE composites in a Y-up FBO).
void emulator_load_overlay(const char* utf8_path)
{
	if (utf8_path == NULL) return;

	int w = 0, h = 0, comp = 0;
	unsigned char* data = stbi_load(utf8_path, &w, &h, &comp, 4); // force RGBA
	if (!data) {
		LOG_ERROR("Overlay load: could NOT decode '%s'", utf8_path);
		return;
	}

	if (art_loaded[1] && art_tex[1]) {     // replace any previous overlay
		glDeleteTextures(1, &art_tex[1]);
		art_tex[1] = 0;
	}

	GLuint tex = 0;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
	glGenerateMipmap(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, 0);
	stbi_image_free(data);

	art_tex[1] = tex;
	art_loaded[1] = 1;
	config.overlay = 1;
	LOG_INFO("Overlay load: '%s' (%dx%d) -> art_tex[1]=%u", utf8_path, w, h, tex);
}

void emulator_clear_overlay(void)
{
	if (art_loaded[1] && art_tex[1]) {
		glDeleteTextures(1, &art_tex[1]);
		art_tex[1] = 0;
	}
	art_loaded[1] = 0;
	LOG_INFO("Overlay cleared.");
}

// Whether an overlay image is currently loaded (for the host to grey/check the menu).
int emulator_has_overlay(void)
{
	return (art_loaded[1] && art_tex[1]) ? 1 : 0;
}

// [vectrex-port] Glow amount (0..15 slider) and on/off are tracked separately so
// the checkbox and the slider compose: effective glow = on ? amount : 0.
static int  s_glow_amount = 8;     // slider value, 0..15
static bool s_glow_on = true;

// The slider value 0..15 maps to AAE's config.vecglow 0..60 (x4). The composite
// shader uses glowamt = vecglow*0.01, so the slider spans glowamt 0.0..0.60 --
// i.e. slider 15 == the original full-strength glow, and a 0..15 dial still
// reaches the look we had before.
#define GLOW_SLIDER_TO_VECGLOW 4
void emulator_apply_glow(void) { config.vecglow = s_glow_on ? (s_glow_amount * GLOW_SLIDER_TO_VECGLOW) : 0; }

int  emulator_get_glow(void) { return s_glow_amount; }
void emulator_set_glow(int amt)
{
	if (amt < 0)  amt = 0;
	if (amt > 15) amt = 15;
	s_glow_amount = amt;
	emulator_apply_glow();
}

static float s_dot_size = 2.0f;   // single-dot diameter (1.0..10.0)

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

float emulator_get_line_width(void) { return config.linewidth; }
void  emulator_set_line_width(float w) { config.linewidth = clampf(w, 1.0f, 10.0f); }

float emulator_get_dot_size(void) { return s_dot_size; }         // single-dot size
void  emulator_set_dot_size(float w) { s_dot_size = clampf(w, 1.0f, 10.0f); }

// Beam edge feather (AAE "BEAM SMOOTHING", 0.4..2.0) and corner disc size
// (AAE "BEAM CORNERSIZE", 0.3..2.5).
float emulator_get_smoothing(void) { return config.line_smoothing; }
void  emulator_set_smoothing(float v) { config.line_smoothing = clampf(v, 0.4f, 2.0f); }

float emulator_get_corner(void) { return config.corner_strength; }
void  emulator_set_corner(float v) { config.corner_strength = clampf(v, 0.3f, 2.5f); }

// Glow blur path: 0 = classic accumulate blur, 1 = dual-filter pyramid.
int  emulator_get_glow_filter(void) { return config.glow_filter; }
void emulator_set_glow_filter(int f) { config.glow_filter = f ? 1 : 0; }

// Pyramid glow tuning (ini only). Ranges from AAE's VECTOR MONITOR SETUP menu.
void emulator_set_glow2(float gain, float spread, float tail, float core)
{
	config.glow2_gain   = clampf(gain,   0.0f, 30.0f);
	config.glow2_spread = clampf(spread, 0.2f,  3.0f);
	config.glow2_tail   = clampf(tail,   0.0f,  2.0f);
	config.glow2_core   = clampf(core,   0.0f,  2.0f);
}
void emulator_get_glow2(float* gain, float* spread, float* tail, float* core)
{
	if (gain)   *gain   = config.glow2_gain;
	if (spread) *spread = config.glow2_spread;
	if (tail)   *tail   = config.glow2_tail;
	if (core)   *core   = config.glow2_core;
}

// Phosphor trail: on/off (the toggle) and persistence level 1..3 (LITTLE / MORE /
// MAX, per-frame decay 0.825 / 0.86 / 0.93) compose as vectrail = on ? level : 0.
static int  s_trail_level = 1;
static bool s_trail_on = false;
static void emulator_apply_trail(void) { config.vectrail = s_trail_on ? s_trail_level : 0; }

int  emulator_get_trail_level(void) { return s_trail_level; }
void emulator_set_trail_level(int level)
{
	s_trail_level = level < 1 ? 1 : (level > 3 ? 3 : level);
	emulator_apply_trail();
}

// Toggle a video effect. which: 0=glow, 1=trail, 2=overlay. Returns new state (0/1).
int emulator_toggle_video(int which)
{
	switch (which) {
	case 0: s_glow_on = !s_glow_on; emulator_apply_glow();     return s_glow_on ? 1 : 0;
	case 1: s_trail_on = !s_trail_on; emulator_apply_trail();  return s_trail_on ? 1 : 0;
	case 2: config.overlay = config.overlay ? 0 : 1;           return config.overlay ? 1 : 0;
	}
	return -1;
}

// Current state of a video effect (0/1). Overlay is "on" only when art is loaded.
int emulator_get_video(int which)
{
	switch (which) {
	case 0: return s_glow_on ? 1 : 0;
	case 1: return s_trail_on ? 1 : 0;
	case 2: return (config.overlay && art_loaded[1]) ? 1 : 0;
	}
	return -1;
}

// Set a video effect on/off directly (restoring saved settings). Overlay only
// turns on if an overlay image is actually loaded for the current game.
void emulator_set_video(int which, int on)
{
	switch (which) {
	case 0: s_glow_on = (on != 0); emulator_apply_glow();      break;
	case 1: s_trail_on = (on != 0); emulator_apply_trail();    break;
	case 2: config.overlay = (on && art_loaded[1]) ? 1 : 0;   break;
	}
}

// Master audio volume (0..100), applied on the XAudio2 mastering voice.
int  emulator_get_volume(void) { return mixer_get_master_volume_percent(); }
void emulator_set_volume(int percent)
{
	if (percent < 0)   percent = 0;
	if (percent > 100) percent = 100;
	mixer_set_master_volume(percent);
}
