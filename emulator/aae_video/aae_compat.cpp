// -----------------------------------------------------------------------------
// [vectrex-port] aae_compat.cpp
//
// Definitions backing the AAE vector-glow renderer shim. This is the single
// translation unit that supplies the external symbols the trimmed AAE renderer
// sources reference but that the Vectrex host does not otherwise provide:
//
//   - The Machine instance and its driver descriptor (configured for the
//     monochrome Vectrex with an OVERLAY2 color gel).
//   - art_tex[] / art_loaded[] artwork slots.
//   - set_texture() (verbatim behavior from AAE texture_handler.cpp).
//   - GetWindowSetup() (synthesizes a WindowSetup from SCREEN_W/SCREEN_H).
//   - emulator_is_gui_active() stub (always false).
//   - osd_get_pen() stub (cut raster path only).
//
// Nothing here drives the pipeline; emulator.cpp (osint_render) does.
// -----------------------------------------------------------------------------

#include "framework.h"          // SCREEN_W / SCREEN_H
#include "sys_gl.h"
#include "aae_mame_driver.h"
#include "texture_handler.h"
#include "aae_compat.h"

// -----------------------------------------------------------------------------
// Machine / driver descriptor
//
// The Vectrex is a single fixed "machine". gamedrv and drv both point at one
// static AAEDriver. Monochrome CRT with a translucent color overlay gel, so we
// set VECTOR + OVERLAY2 + BW (NOT VECTOR_USES_COLOR). 1024x1024 logical screen
// matches the FBO space the renderer composites in.
// -----------------------------------------------------------------------------
static AAEDriver s_vectrex_driver = {
	/* name             */ "vectrex",
	/* video_attributes */ VIDEO_TYPE_VECTOR | VECTOR_USES_OVERLAY2 | VECTOR_USES_BW,
	/* rotation         */ ROT0,
	/* visible_area     */ { 0, 1023, 0, 1023 }
};

static RunningMachine s_vectrex_machine = {
	/* gamedrv          */ &s_vectrex_driver,
	/* drv              */ &s_vectrex_driver,
	/* orientation      */ 0,
	/* video_attributes */ VIDEO_TYPE_VECTOR | VECTOR_USES_OVERLAY2 | VECTOR_USES_BW,
	/* visible_area     */ { 0, 1023, 0, 1023 },
	/* pens             */ { 0 }
};

RunningMachine* Machine = &s_vectrex_machine;

// -----------------------------------------------------------------------------
// Artwork slots
//
// art_tex[1] / art_loaded[1] hold the overlay color gel. The Vectrex wiring
// step loads the overlay texture into art_tex[1] and sets art_loaded[1] = 1.
// Everything else stays zero (no backdrop, no bezel).
// -----------------------------------------------------------------------------
rtex_t art_tex[8]    = { 0 };
int    art_loaded[6] = { 0 };

// -----------------------------------------------------------------------------
// set_texture
// Verbatim behavior from the current (core-profile) AAE texture_handler.cpp:
// bind the texture and set its filtering, wrapping and optional alpha blending.
// set_color is unused there too - core profile has no current vertex color.
// -----------------------------------------------------------------------------
void set_texture(rtex_t* texture, bool linear, bool mipmapping, bool blending, bool /*set_color*/)
{
	GLenum magFilter = linear ? GL_LINEAR : GL_NEAREST;
	GLenum minFilter = mipmapping ? GL_LINEAR_MIPMAP_LINEAR : magFilter;

	glBindTexture(GL_TEXTURE_2D, *texture);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	if (blending)
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
}

// -----------------------------------------------------------------------------
// GetWindowSetup
// Refresh client size from the Vectrex globals each call. SCREEN_W/SCREEN_H are
// owned and maintained by the Vectrex host window code.
// -----------------------------------------------------------------------------
WindowSetup& GetWindowSetup()
{
	static WindowSetup ws;
	ws.clientWidth  = (SCREEN_W > 0) ? SCREEN_W : 768;
	ws.clientHeight = (SCREEN_H > 0) ? SCREEN_H : 960;
	// [vectrex-port] The Vectrex is a PORTRAIT display. aspectRatio is width/height
	// of the beam extents (ALG_MAX_X/ALG_MAX_Y ~= 33000/41000 ~= 0.805), so the
	// final blit pillarboxes correctly on a landscape monitor.
	ws.aspectRatio  = 33000.0f / 41000.0f;
	return ws;
}

// -----------------------------------------------------------------------------
// emulator_is_gui_active - no front-end GUI on the Vectrex host.
// -----------------------------------------------------------------------------
bool emulator_is_gui_active()
{
	return false;
}

// -----------------------------------------------------------------------------
// osd_get_pen - cut raster path only; harmless black fallback.
// -----------------------------------------------------------------------------
void osd_get_pen(unsigned char /*pen*/, unsigned char* r, unsigned char* g, unsigned char* b)
{
	if (r) *r = 0;
	if (g) *g = 0;
	if (b) *b = 0;
}
