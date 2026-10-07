#pragma once
#ifndef TEXRECT_H
#define TEXRECT_H

#include "render_types.h"
#include <cstdint>

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

	// Render the quad. Must have bound your GL_TEXTURE_2D (unit 0) before calling.
	// mvp is a column-major 4x4 projection (pixel-space ortho) supplied by the
	// caller -- replaces the old fixed-function gl_ModelViewProjectionMatrix.
	void Render(const float* mvp);

	// Stream the quad through the VAO with the CALLER's shader program
	// already bound. The caller owns all program state (its own uProj,
	// sampler and uniforms); this only submits the geometry. Attribute
	// layout matches Render(): location 0 = vec2 position, 1 = vec2 uv.
	void RenderGeometry();

	// The letterboxed/pillarboxed destination rectangle in window pixels,
	// as computed by UpdateScreenRect. Lets a caller lay a second, TILED
	// quad (a scanline/aperture texture) over exactly the game image.
	void GetScreenRect(float* x, float* y, float* w, float* h) const;

private:
	void SetVertex(int idx, float x, float y, float tx, float ty);

	_Point2DA  verts_[4];
	rprog_t    prog_;
	rvao_t     vao_ = 0;
	rbuf_t     vbo_ = 0;
	std::int32_t sampler_loc_, uproj_loc_;
	float      rect_x_ = 0.0f, rect_y_ = 0.0f;
	float      rect_w_ = 0.0f, rect_h_ = 0.0f;
};

#endif // TEXRECT_H