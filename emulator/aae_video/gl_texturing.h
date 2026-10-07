//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2024-2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
#pragma once

#ifndef GL_TEX_H
#define GL_TEX_H

extern float wideadj; // No longer used.
extern int errorsound;

void quad_from_center(float x, float y, float width, float height, int r, int g, int b, int alpha);
void drawTexturedQuad(float left, float right, float bottom, float top, bool flip_v = false,
                      float rT = 1.0f, float gT = 1.0f, float bT = 1.0f, float aT = 1.0f,
                      float alphaTest = 0.0f);
void Any_Rect(int facing, int xmin, int xmax, int ymin, int ymax);
void FS_Rect(int facing, int size, float rT = 1.0f, float gT = 1.0f, float bT = 1.0f, float aT = 1.0f);
void Screen_Rect(int facing, int size);
void Resize_Rect(int facing, int size);
void Bezel_Rect(int xmin, int xmax, int ymin, int ymax);
// [vectrex-port] removed: show_error()

#endif
