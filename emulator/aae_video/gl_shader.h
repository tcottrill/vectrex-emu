#pragma once

// -----------------------------------------------------------------------------
// This file is part of the AAE (Another Arcade Emulator) project.
// This Code is copyright (C) 2025/2026 Tim Cottrill and released
// under the GNU GPL v3 or later. See the accompanying source files for full
// license details.
// -----------------------------------------------------------------------------

#ifndef GL_SHADER_H
#define GL_SHADER_H

#include "render_types.h"
// Shader program handles
extern rprog_t fragBlur;
extern rprog_t fragMulti;
extern rprog_t fragBasicTex;
extern rprog_t fragBasicColor;
extern rprog_t fragScanlineMultiply;
extern rprog_t fragStarPoint;
extern rprog_t fragTexColor;   // textured + per-vertex color (legacy textured shots)
extern rprog_t fragMonoMonitor; // mono CRT simulation for B/W raster games
extern rprog_t fragColorMonitor; // color CRT simulation (shadow mask) for color raster games
extern rprog_t fragDualDown;   // dual-filter pyramid downsample ([main] glow_filter=1)
extern rprog_t fragDualUp;     // dual-filter pyramid upsample + core re-injection

int init_shader();
void bind_shader(rprog_t program);
void unbind_shader();
void delete_shader(rprog_t* program);

void set_uniform1i(rprog_t program, const char* name, int value);
void set_uniform1f(rprog_t program, const char* name, float value);
void set_uniform2f(rprog_t program, const char* name, float x, float y);
void set_uniform3f(rprog_t program, const char* name, float x, float y, float z);
void set_uniform4f(rprog_t program, const char* name, float x, float y, float z, float w);
void set_uniform_mat4f(rprog_t program, const char* name, const float* matrix);

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