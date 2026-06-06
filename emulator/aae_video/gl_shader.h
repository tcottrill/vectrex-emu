#pragma once

// -----------------------------------------------------------------------------
// This file is part of the AAE (Another Arcade Emulator) project.
// This Code is copyright (C) 2025/2026 Tim Cottrill and released
// under the GNU GPL v3 or later. See the accompanying source files for full
// license details.
// -----------------------------------------------------------------------------

#ifndef GL_SHADER_H
#define GL_SHADER_H

#include "sys_gl.h"
// Shader program handles
extern GLuint fragBlur;
extern GLuint fragMulti;
extern GLuint fragBasicTex;   
extern GLuint fragBasicColor; 
extern GLuint fragScanlineMultiply;
extern GLuint fragStarPoint;

int init_shader();
void bind_shader(GLuint program);
void unbind_shader();
void delete_shader(GLuint* program);

void set_uniform1i(GLuint program, const char* name, int value);
void set_uniform1f(GLuint program, const char* name, float value);
void set_uniform2f(GLuint program, const char* name, float x, float y);
void set_uniform3f(GLuint program, const char* name, float x, float y, float z);
void set_uniform4f(GLuint program, const char* name, float x, float y, float z, float w);
void set_uniform_mat4f(GLuint program, const char* name, const float* matrix);

#endif

/*
* Usage Notes for me, just until I get the code updated.
* 
bind_shader(fragBlur);
set_uniform1f(fragBlur, "width", (float)textureWidth);
set_uniform1f(fragBlur, "height", (float)textureHeight);
// Draw...
unbind_shader();
*/