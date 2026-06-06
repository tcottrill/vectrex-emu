#pragma once

#ifndef GL_TEX_H
#define GL_TEX_H

#include "sys_gl.h"

extern float wideadj; // No longer used.
extern int errorsound;

void quad_from_center(float x, float y, float width, float height, int r, int g, int b, int alpha);
void drawTexturedQuad(float left, float right, float bottom, float top, bool flip_v);
void Any_Rect(int facing, int xmin, int xmax, int ymin, int ymax);
void FS_Rect(int facing, int size);
void Screen_Rect(int facing, int size);
void Resize_Rect(int facing, int size);
void Bezel_Rect(int xmin, int xmax, int ymin, int ymax);
void show_error(void);

#endif
