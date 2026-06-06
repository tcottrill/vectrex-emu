#pragma once
#ifndef TEXRECT_H
#define TEXRECT_H

#include "sys_gl.h"
#include "shader_util.h"

// Simple 2D point + texcoord
class _Point2DA {
public:
	float x, y, tx, ty;
	_Point2DA() : x(0), y(0), tx(0), ty(0) {}
	_Point2DA(float _x, float _y, float _tx, float _ty)
		: x(_x), y(_y), tx(_tx), ty(_ty) {
	}
};

class Rect2 {
public:
	Rect2(int screen_width, int screen_height, float aspectRatio = 4.0f / 3.0f, int rotated = 0);
	~Rect2();

	void UpdateScreenRect(int screen_width, int screen_height, float aspectRatio, int rotated);

	// set the four corners (+ optional texcoords)
	void BottomLeft(float x, float y, float tx, float ty);
	void TopLeft(float x, float y, float tx, float ty);
	void TopRight(float x, float y, float tx, float ty);
	void BottomRight(float x, float y, float tx, float ty);

	// convenience: full-quad [0..1] texcoords
	void BottomLeft(float x, float y);
	void TopLeft(float x, float y);
	void TopRight(float x, float y);
	void BottomRight(float x, float y);

	// Render the quad, scaling the y-coordinates of the top two verts by scaley
	// Must have bound your GL_TEXTURE_2D before calling.
	void Render();

private:
	void SetVertex(int idx, float x, float y, float tx, float ty);

	_Point2DA  verts_[4];
	GLuint     prog_;
	GLint      pos_loc_, texcoord_loc_, sampler_loc_;
};

#endif // TEXRECT_H