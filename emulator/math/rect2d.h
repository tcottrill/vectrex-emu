#pragma once
#include "MathUtils.h"

class Rect2D {
public:
	float x = 0.0f;
	float y = 0.0f;
	float width = 0.0f;
	float height = 0.0f;
	float halfWidth = 0.0f;
	float halfHeight = 0.0f;

	Rect2D() = default;

	Rect2D(float _x, float _y, float _width, float _height)
		: x(_x), y(_y), width(_width), height(_height),
		halfWidth(_width * 0.5f), halfHeight(_height * 0.5f) {
	}

	Rect2D(const aae::math::vec2& p, const aae::math::vec2& s)
		: x(p.x), y(p.y), width(s.x), height(s.y),
		halfWidth(s.x * 0.5f), halfHeight(s.y * 0.5f) {
	}

	bool contains(const aae::math::vec2& p) const {
		return p.x >= x && p.x <= x + width &&
			p.y >= y && p.y <= y + height;
	}

	void setX(float _x) { x = _x; }
	void setY(float _y) { y = _y; }
	void setWidth(float _width) {
		width = _width;
		halfWidth = _width * 0.5f;
	}
	void setHeight(float _height) {
		height = _height;
		halfHeight = _height * 0.5f;
	}

	void setPosition(const aae::math::vec2& position) {
		x = position.x;
		y = position.y;
	}

	void setSize(const aae::math::vec2& lowerLeft, const aae::math::vec2& upperRight) {
		x = lowerLeft.x;
		y = lowerLeft.y;
		width = upperRight.x - x;
		height = upperRight.y - y;
		halfWidth = width * 0.5f;
		halfHeight = height * 0.5f;
	}

	void extendTo(const aae::math::vec2& p) {
		if (p.x < x) {
			width += (x - p.x);
			x = p.x;
		}
		else if (p.x > x + width) {
			width = p.x - x;
		}
		if (p.y < y) {
			height += (y - p.y);
			y = p.y;
		}
		else if (p.y > y + height) {
			height = p.y - y;
		}
		halfWidth = width * 0.5f;
		halfHeight = height * 0.5f;
	}

	bool intersects(const Rect2D& other) const {
		return !(x + width < other.x ||      // this right < other left
			x > other.x + other.width ||// this left > other right
			y + height < other.y ||     // this top < other bottom
			y > other.y + other.height);// this bottom > other top
	}
	
	bool intersects(const aae::math::vec2& point) const {
		return contains(point);
	}

	float getX() const { return x; }
	float getY() const { return y; }
	float getWidth() const { return width; }
	float getHeight() const { return height; }
	aae::math::vec2 getPosition() const { return aae::math::vec2(x, y); }
	aae::math::vec2 getCenter() const { return aae::math::vec2(x + halfWidth, y + halfHeight); }
	aae::math::vec2 getSize() const { return aae::math::vec2(width, height); }
};
