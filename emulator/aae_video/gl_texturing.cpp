//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2024-2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
#include "aae_mame_driver.h"
#include "gl_texturing.h"
#include "sys_gl.h"
#include "opengl_renderer.h"
#include "texture_handler.h"
#include "colordefs.h"
#include "gl_shader.h" // Added to access the new basic shaders
#include "MathUtils.h"  // aae::math::mat4 / value_ptr for the core-profile quads


int errorsound = 0;

// Reusable VAO/VBO for the basic colored quads (core-profile path).
// Per-vertex layout matches fragBasicColor: position (vec2) + packed RGBA color.
struct QuadVtx { float x, y; unsigned int color; };
static GLuint s_quadVAO = 0;
static GLuint s_quadVBO = 0;
static void ensure_quad_buffers()
{
	if (s_quadVAO) return;
	glGenVertexArrays(1, &s_quadVAO);
	glGenBuffers(1, &s_quadVBO);
	glBindVertexArray(s_quadVAO);
	glBindBuffer(GL_ARRAY_BUFFER, s_quadVBO);
	glBufferData(GL_ARRAY_BUFFER, 6 * sizeof(QuadVtx), nullptr, GL_DYNAMIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(QuadVtx), (void*)offsetof(QuadVtx, x));
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(QuadVtx), (void*)offsetof(QuadVtx, color));
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

// ----
// Texturing and drawing rectangle code Below.
// ----

// Consolidated drawing routine. Uses standard OpenGL UVs (0=bottom, 1=top).
// Set flip_v = true for FBO copies that need to be inverted.
// Reusable VAO/VBO for textured quads (core-profile path: position + texcoord).
struct TexQuadVtx { float x, y, u, v; };
static GLuint s_texQuadVAO = 0;
static GLuint s_texQuadVBO = 0;
static void ensure_tex_quad_buffers()
{
	if (s_texQuadVAO) return;
	glGenVertexArrays(1, &s_texQuadVAO);
	glGenBuffers(1, &s_texQuadVBO);
	glBindVertexArray(s_texQuadVAO);
	glBindBuffer(GL_ARRAY_BUFFER, s_texQuadVBO);
	glBufferData(GL_ARRAY_BUFFER, 6 * sizeof(TexQuadVtx), nullptr, GL_DYNAMIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(TexQuadVtx), (void*)offsetof(TexQuadVtx, x));
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(TexQuadVtx), (void*)offsetof(TexQuadVtx, u));
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void drawTexturedQuad(float left, float right, float bottom, float top, bool flip_v,
                      float rT, float gT, float bT, float aT, float alphaTest)
{
	// flip_v: 'bottom' maps to v=1 and 'top' to v=0 (used for FBO copies).
	const float vb = flip_v ? 1.0f : 0.0f;
	const float vt = flip_v ? 0.0f : 1.0f;

	const TexQuadVtx verts[6] = {
		{ left,  bottom, 0.0f, vb }, { left,  top, 0.0f, vt }, { right, top,    1.0f, vt },
		{ left,  bottom, 0.0f, vb }, { right, top, 1.0f, vt }, { right, bottom, 1.0f, vb }
	};

	GLint current_prog = 0;
	glGetIntegerv(GL_CURRENT_PROGRAM, &current_prog);

	GLuint prog;
	if (current_prog == 0)
	{
		// Standalone: bind the basic textured shader and tint by uColor.
		bind_shader(fragBasicTex);
		prog = fragBasicTex;
		set_uniform1i(fragBasicTex, "u_texture", 0);
		set_uniform4f(fragBasicTex, "uColor", rT, gT, bT, aT);
		set_uniform1f(fragBasicTex, "uAlphaTest", alphaTest);
	}
	else
	{
		// A composite shader (fragMulti / fragBlur) is already bound with its own
		// samplers/uniforms; we just supply the projection and the geometry.
		prog = (GLuint)current_prog;
	}
	set_uniform_mat4f(prog, "uProj", aae::math::value_ptr(g_proj));

	ensure_tex_quad_buffers();
	glBindVertexArray(s_texQuadVAO);
	glBindBuffer(GL_ARRAY_BUFFER, s_texQuadVBO);
	glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
	glDrawArrays(GL_TRIANGLES, 0, 6);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	if (current_prog == 0)
		unbind_shader();
}

void quad_from_center(float x, float y, float width, float height, int r, int g, int b, int alpha)
{
	float minx = x - (width / 2.0f);
	float miny = y - (height / 2.0f);
	float maxx = x + (width / 2.0f);
	float maxy = y + (height / 2.0f);

	// Separate alpha (src factor GL_ONE) so the accumulated alpha is correct when
	// drawn into an offscreen RGBA target that is later alpha-blitted (the rotated
	// raster overlay). RGB is unchanged, so backbuffer and straight-copied vector
	// fbo4 paths are visually identical.
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_BLEND);

	// Pack RGBA so GL_UNSIGNED_BYTE-normalized reads it as (r,g,b,a).
	unsigned int c = (unsigned int)(r & 0xFF) | ((unsigned int)(g & 0xFF) << 8) |
	                 ((unsigned int)(b & 0xFF) << 16) | ((unsigned int)(alpha & 0xFF) << 24);
	const QuadVtx verts[6] = {
		{ minx, miny, c }, { maxx, miny, c }, { maxx, maxy, c },
		{ minx, miny, c }, { maxx, maxy, c }, { minx, maxy, c }
	};

	ensure_quad_buffers();
	bind_shader(fragBasicColor);
	set_uniform_mat4f(fragBasicColor, "uProj", aae::math::value_ptr(g_proj));

	glBindVertexArray(s_quadVAO);
	glBindBuffer(GL_ARRAY_BUFFER, s_quadVBO);
	glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
	glDrawArrays(GL_TRIANGLES, 0, 6);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	unbind_shader();
}

void Any_Rect(int facing, int left, int right, int bottom, int top)
{
	drawTexturedQuad((float)left, (float)right, (float)bottom, (float)top, true);
}

void FS_Rect(int facing, int size, float rT, float gT, float bT, float aT)
{
	drawTexturedQuad(0.0f, (float)size, 0.0f, (float)size, false, rT, gT, bT, aT);
}

void Screen_Rect(int facing, int size)
{
	drawTexturedQuad(0.0f, (float)size, 0.0f, (float)size, false);
}

void Resize_Rect(int facing, int size)
{
	float h = size * 0.75f;
	drawTexturedQuad(0.0f, (float)size, 0.0f, h, false);
}

void Bezel_Rect(int left, int right, int bottom, int top)
{
	drawTexturedQuad((float)left, (float)right, (float)bottom, (float)top, false);
}

// [vectrex-port] removed: show_error() (needs the AAE vector font + Vulkan
// dispatch; the Vectrex host reports errors through its own dialogs).
