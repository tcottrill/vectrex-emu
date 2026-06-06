#pragma once
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

// -----------------------------------------------------------------------------
// debug_draw.h
//
// Header-only debug draw system for OpenGL 2.0 and OpenGL 3.3+
// Draws points, lines, circles, polygons, and boxes using immediate-mode (GL2)
// or shader-based VAO rendering (GL3+)
//
// Usage:
//   debugAddPoint(x, y);
//   debugAddLine(a, b);
//   debugDrawAll();               // for OpenGL 2.0 (calls ViewOrtho() first)
//   debugDrawAll(projMatrix);     // for OpenGL 3.3+ with projection
//
// Automatically clears points after drawing.
// -----------------------------------------------------------------------------

#pragma once
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "sys_gl.h"
#include "shader_util.h"

// -----------------------------------------------------------------------------
// Debug draw mode (Points or Lines)
// -----------------------------------------------------------------------------
enum class DebugDrawMode {
	Points,
	Lines
};

// -----------------------------------------------------------------------------
// Internal state (header-only safe via inline function-local statics)
// -----------------------------------------------------------------------------
inline std::vector<glm::vec2>& get_debugVerts() {
	static std::vector<glm::vec2> verts;
	return verts;
}

inline DebugDrawMode& get_debugMode() {
	static DebugDrawMode mode = DebugDrawMode::Points;
	return mode;
}

inline void debugSetMode(DebugDrawMode mode) {
	get_debugMode() = mode;
}

// -----------------------------------------------------------------------------
// Automatically initialize default mode
// -----------------------------------------------------------------------------
inline void debugInit() {
	static bool initialized = false;
	if (!initialized) {
		debugSetMode(DebugDrawMode::Points);
		initialized = true;
	}
}

// -----------------------------------------------------------------------------
// Add primitives
// -----------------------------------------------------------------------------
inline void debugAddPoint(float x, float y) {
	get_debugVerts().push_back(glm::vec2(x, y));
}

inline void debugAddLine(const glm::vec2& a, const glm::vec2& b) {
	get_debugVerts().push_back(a);
	get_debugVerts().push_back(b);
}

inline void debugAddTriangle(const glm::vec2& a, const glm::vec2& b, const glm::vec2& c) {
	debugAddLine(a, b);
	debugAddLine(b, c);
	debugAddLine(c, a);
}

inline void debugAddPolygon(const std::vector<glm::vec2>& points) {
	if (points.size() < 2) return;
	for (size_t i = 0; i < points.size(); ++i) {
		debugAddLine(points[i], points[(i + 1) % points.size()]);
	}
}

inline void debugAddCircle(const glm::vec2& center, float radius, int segments = 32) {
	if (segments < 3) segments = 3;
	float angleStep = 2.0f * 3.1415926f / segments;
	glm::vec2 prev = center + glm::vec2(cosf(0.0f), sinf(0.0f)) * radius;
	for (int i = 1; i <= segments; ++i) {
		float angle = i * angleStep;
		glm::vec2 next = center + glm::vec2(cosf(angle), sinf(angle)) * radius;
		debugAddLine(prev, next);
		prev = next;
	}
}

inline void debugAddBox(const glm::vec2& min, const glm::vec2& max) {
	glm::vec2 a = min;
	glm::vec2 b = { max.x, min.y };
	glm::vec2 c = max;
	glm::vec2 d = { min.x, max.y };
	debugAddLine(a, b);
	debugAddLine(b, c);
	debugAddLine(c, d);
	debugAddLine(d, a);
}

// -----------------------------------------------------------------------------
// OpenGL 2.0 draw (immediate mode)
// Call ViewOrtho() before this if needed
// -----------------------------------------------------------------------------

inline void debugDrawAll()
{
	debugInit();

	auto& verts = get_debugVerts();
	if (verts.empty())
		return;

	glUseProgram(0);
	glDisable(GL_TEXTURE_2D);
	glPointSize(8.0f);
	glLineWidth(2.0f);
	glColor3f(1.0f, 0.0f, 0.0f);

	GLenum mode = (get_debugMode() == DebugDrawMode::Lines) ? GL_LINES : GL_POINTS;

	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(2, GL_FLOAT, 0, verts.data());
	glDrawArrays(mode, 0, static_cast<GLsizei>(verts.size()));
	glDisableClientState(GL_VERTEX_ARRAY);

	glEnable(GL_TEXTURE_2D);
	verts.clear();
}

// -----------------------------------------------------------------------------
// OpenGL 3.3+ draw (VAO + shader)
// -----------------------------------------------------------------------------

inline void debugDrawAll(const glm::mat4& proj)
{
	debugInit();

	auto& verts = get_debugVerts();
	if (verts.empty()) return;

	static GLuint vao = 0, vbo = 0, shader = 0;

	if (!vao) {
		glGenVertexArrays(1, &vao);
		glGenBuffers(1, &vbo);

		const char* vs = R"(
			#version 330 core
			layout(location = 0) in vec2 in_pos;
			uniform mat4 u_proj;
			void main() {
				gl_PointSize = 6.0;
				gl_Position = u_proj * vec4(in_pos, 0.0, 1.0);
			})";

		const char* fs = R"(
			#version 330 core
			out vec4 out_color;
			void main() {
				out_color = vec4(1.0, 0.0, 0.0, 1.0);
			})";

		GLuint vshader = CompileShader(GL_VERTEX_SHADER, vs, "debug_vertex");
		GLuint fshader = CompileShader(GL_FRAGMENT_SHADER, fs, "debug_frag");
		shader = LinkShaderProgram(vshader, fshader);
	}

	glDisable(GL_TEXTURE_2D);
	glPointSize(8.0f);

	glUseProgram(shader);
	glUniformMatrix4fv(glGetUniformLocation(shader, "u_proj"), 1, GL_FALSE, glm::value_ptr(proj));

	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(glm::vec2), verts.data(), GL_DYNAMIC_DRAW);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec2), (void*)0);

	GLenum mode = (get_debugMode() == DebugDrawMode::Lines) ? GL_LINES : GL_POINTS;
	glDrawArrays(mode, 0, static_cast<GLsizei>(verts.size()));

	glDisableVertexAttribArray(0);
	glBindVertexArray(0);
	glUseProgram(0);

	verts.clear();
}
