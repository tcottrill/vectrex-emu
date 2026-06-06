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
// shader_util.h
//
// Description:
// General-purpose OpenGL shader utility for compiling and linking GLSL shaders.
// Provides reusable functions for:
// - Compiling individual shaders (vertex, fragment, etc.)
// - Linking shader programs
// - Logging success/failure via LOG_INFO and LOG_ERROR
//
// Usage:
// GLuint vs = CompileShader(GL_VERTEX_SHADER, vs_src, "VS label");
// GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fs_src, "FS label");
// GLuint program = LinkShaderProgram(vs, fs);
//
// On success, shaders are linked and deleted. On failure, logs show error source.
//
// -----------------------------------------------------------------------------

#pragma once
#include "sys_gl.h"
#include "sys_log.h"

// -----------------------------------------------------------------------------
// Compiles a GLSL shader and logs success/failure
// -----------------------------------------------------------------------------
inline GLuint CompileShader(GLenum type, const char* src, const char* label = nullptr)
{
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &src, nullptr);
	glCompileShader(shader);

	GLint success = 0;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

	if (!success) {
		char log[512];
		glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
		LOG_ERROR("CompileShader FAILED (%s):\n%s", label ? label : "unnamed", log);
	}
	else {
		LOG_INFO("CompileShader OK (%s)", label ? label : "unnamed");
	}

	return shader;
}

// -----------------------------------------------------------------------------
// Links a GLSL program from vertex + fragment shaders
// -----------------------------------------------------------------------------
inline GLuint LinkShaderProgram(GLuint vertexShader, GLuint fragmentShader)
{
	GLuint program = glCreateProgram();
	glAttachShader(program, vertexShader);
	glAttachShader(program, fragmentShader);
	glLinkProgram(program);

	GLint success = 0;
	glGetProgramiv(program, GL_LINK_STATUS, &success);

	if (!success) {
		char log[512];
		glGetProgramInfoLog(program, sizeof(log), nullptr, log);
		LOG_ERROR("LinkShaderProgram FAILED:\n%s", log);
	}
	else {
		LOG_INFO("LinkShaderProgram OK");
	}

	// Cleanup individual shaders after linking
	glDeleteShader(vertexShader);
	glDeleteShader(fragmentShader);

	return program;
}
