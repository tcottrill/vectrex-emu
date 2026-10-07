// texrect.cpp

// -----------------------------------------------------------------------------
// Game Engine Alpha - Generic Module
// Generic component or utility file for the Game Engine Alpha project. This
// file may contain helpers, shared utilities, or subsystems that integrate
// seamlessly with the engine's rendering, audio, and gameplay frameworks.
//
// Integration:
//   This library is part of the **Game Engine Alpha** project and is tightly
//   integrated with its texture management, logging, and math utility systems.
//
// Usage:
//   Include this module where needed. It is designed to work as a building block
//   for engine subsystems such as rendering, input, audio, or game logic.
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
// -----------------------------------------------------------------------------

#include "texrect.h"
#include "sys_gl.h"
#include "shader_util.h"

#include <cstddef>   // offsetof

// ---------------------------------------------------------------------------
// Rect2 shaders - GLSL 330 core
//
// Explicit attribute locations + an explicit uProj uniform (no
// gl_ModelViewProjectionMatrix), with geometry fed from a VAO/VBO. Fully
// core-profile; the caller supplies the projection via Render(mvp).
// ---------------------------------------------------------------------------

static const char* rect2_vs = R"glsl(
#version 330 core

layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_texcoord;

out vec2 v_texcoord;

uniform mat4 uProj;

void main()
{
    gl_Position = uProj * vec4(a_position, 0.0, 1.0);
    v_texcoord = a_texcoord;
}
)glsl";

static const char* rect2_fs = R"glsl(
#version 330 core

in vec2 v_texcoord;
out vec4 FragColor;

uniform sampler2D u_texture;

void main()
{
    FragColor = texture(u_texture, v_texcoord);
}
)glsl";

Rect2::Rect2(int screen_width, int screen_height, float aspectRatio, int rotated) {

	GLuint vs = CompileShader(GL_VERTEX_SHADER, rect2_vs, "Rect2 VS");
	GLuint fs = CompileShader(GL_FRAGMENT_SHADER, rect2_fs, "Rect2 FS");
	prog_ = LinkShaderProgram(vs, fs);

	// cache uniform locations
	sampler_loc_ = glGetUniformLocation(prog_, "u_texture");
	uproj_loc_   = glGetUniformLocation(prog_, "uProj");

	// bind sampler once to texture-unit 0
	glUseProgram(prog_);
	glUniform1i(sampler_loc_, 0);
	glUseProgram(0);

	// Core-profile geometry: a VAO/VBO holding the 4 interleaved pos+uv corners
	// (_Point2DA: x,y,tx,ty), drawn as a triangle fan. Replaces the client-side
	// vertex arrays the old Render() passed to glVertexAttribPointer.
	glGenVertexArrays(1, &vao_);
	glGenBuffers(1, &vbo_);
	glBindVertexArray(vao_);
	glBindBuffer(GL_ARRAY_BUFFER, vbo_);
	glBufferData(GL_ARRAY_BUFFER, sizeof(verts_), nullptr, GL_DYNAMIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(_Point2DA), (void*)offsetof(_Point2DA, x));
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(_Point2DA), (void*)offsetof(_Point2DA, tx));
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	LOG_INFO("Rect2 shader program initialized");
	// Call the screen rect setup immediately
	UpdateScreenRect(screen_width, screen_height, aspectRatio, rotated);
}

Rect2::~Rect2() {
	if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
	if (vbo_) { glDeleteBuffers(1, &vbo_); vbo_ = 0; }
	if (prog_) {
		glDeleteProgram(prog_);
		LOG_INFO("Rect2 shader program deleted");
	}
}

void Rect2::SetVertex(int idx, float x, float y, float tx, float ty) {
	verts_[idx] = _Point2DA(x, y, tx, ty);
}

void Rect2::BottomLeft(float x, float y, float tx, float ty) { SetVertex(0, x, y, tx, ty); }
void Rect2::TopLeft(float x, float y, float tx, float ty) { SetVertex(1, x, y, tx, ty); }
void Rect2::TopRight(float x, float y, float tx, float ty) { SetVertex(2, x, y, tx, ty); }
void Rect2::BottomRight(float x, float y, float tx, float ty) { SetVertex(3, x, y, tx, ty); }

void Rect2::BottomLeft(float x, float y) { BottomLeft(x, y, 0.0f, 0.0f); }
void Rect2::TopLeft(float x, float y) { TopLeft(x, y, 0.0f, 1.0f); }
void Rect2::TopRight(float x, float y) { TopRight(x, y, 1.0f, 1.0f); }
void Rect2::BottomRight(float x, float y) { BottomRight(x, y, 1.0f, 0.0f); }

void Rect2::Render(const float* mvp) {
	// The full 1024x1024 FBO is mapped onto the aspect-correct screen rect
	// computed by UpdateScreenRect; the 4:3 squish happens naturally because the
	// rect is 4:3 and the texture is square. mvp (column-major 4x4) is the caller's
	// pixel-space ortho (previously the fixed-function gl_ModelViewProjectionMatrix).
	// Corners 0..3 (BL,TL,TR,BR) draw as a triangle fan.
	glUseProgram(prog_);
	glUniformMatrix4fv(uproj_loc_, 1, GL_FALSE, mvp);
	glActiveTexture(GL_TEXTURE0);

	glBindVertexArray(vao_);
	glBindBuffer(GL_ARRAY_BUFFER, vbo_);
	glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts_), verts_);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glUseProgram(0);
}

void Rect2::RenderGeometry() {
	glBindVertexArray(vao_);
	glBindBuffer(GL_ARRAY_BUFFER, vbo_);
	glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts_), verts_);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void Rect2::GetScreenRect(float* x, float* y, float* w, float* h) const {
	if (x) *x = rect_x_;
	if (y) *y = rect_y_;
	if (w) *w = rect_w_;
	if (h) *h = rect_h_;
}

void Rect2::UpdateScreenRect(int screen_width, int screen_height, float aspectRatio, int rotated)
{
	float indices[32] =
	{
		// normal
		0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f,
		// rotated right
		1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
		// rotated left
		0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f,
		// flip
		1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f
	};

	if (aspectRatio <= 0.0f) {
		LOG_INFO("Invalid aspect ratio: %f. Defaulting to 4:3.", aspectRatio);
		aspectRatio = 4.0f / 3.0f;
	}

	// NOTE: aspect inversion for rotation is now handled by the caller
	// (aae_emulator.cpp Step 12) which passes the post-rotation aspect
	// to WindowUtil_UpdateAspect. Rect2 only handles UV rotation and
	// letterbox/pillarbox fitting.

	float used_width = (float)screen_height * aspectRatio;
	float used_height = (float)screen_height;
	float xadj = 0.0f;
	float yadj = 0.0f;

	if (used_width > screen_width) {
		// Width doesn't fit - clamp to screen width instead
		used_width = (float)screen_width;
		used_height = used_width / aspectRatio;
		yadj = ((float)screen_height - used_height) / 2.0f;
	}
	else {
		xadj = ((float)screen_width - used_width) / 2.0f;
	}

	rect_x_ = xadj;
	rect_y_ = yadj;
	rect_w_ = used_width;
	rect_h_ = used_height;

	int v = 8 * rotated;

	BottomLeft(xadj, yadj, indices[v], indices[v + 1]);
	TopLeft(xadj, yadj + used_height, indices[v + 2], indices[v + 3]);
	TopRight(xadj + used_width, yadj + used_height, indices[v + 4], indices[v + 5]);
	BottomRight(xadj + used_width, yadj, indices[v + 6], indices[v + 7]);
}