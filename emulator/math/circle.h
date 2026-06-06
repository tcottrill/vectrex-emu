#pragma once
#ifndef CIRCLEH
#define CIRCLEH

#include "MathUtils.h"

// -----------------------------------------------------------------------------
// Circle
// A 2D circle with floating-point position and radius. Allows direct access to
// position and radius and provides geometric utilities.
// -----------------------------------------------------------------------------
class Circle
{
public:
    // Constructors
    Circle() : radius(1.0f), _x(0.0f), _y(0.0f) {}
    Circle(float x, float y, float radi) : radius(radi), _x(x), _y(y) {}
    Circle(const aae::math::vec2& v, float radi) : radius(radi), _x(v.x), _y(v.y) {}

    // Set the circle radius
    void setRadius(float r) { radius = r; }

    // Get the circle radius
    float getRadius() const { return radius; }

    // Compute the area of the circle
    float getArea() const { return aae::math::pi * radius * radius; }

    // Compute the diameter of the circle
    float getDiameter() const { return radius * 2.0f; }

    // Compute the circumference of the circle
    float getCircumference() const { return 2.0f * aae::math::pi * radius; }

    // -------------------------------------------------------------------------
    // isInRectangle
    // Checks whether a point is inside the square bounding box of the circle.
    // -------------------------------------------------------------------------
    bool isInRectangle(float x, float y) const {
        return (x >= _x - radius && x <= _x + radius &&
            y >= _y - radius && y <= _y + radius);
    }

    // -------------------------------------------------------------------------
    // isPointInCircle
    // Returns true if the given point (x, y) lies inside the circle.
    // -------------------------------------------------------------------------
    bool isPointInCircle(float x, float y) const {
        if (!isInRectangle(x, y)) return false;
        float dx = _x - x;
        float dy = _y - y;
        return (dx * dx + dy * dy) <= (radius * radius);
    }

    // -------------------------------------------------------------------------
    // isLineInCircle
    // Returns true if either endpoint of the line is within the circle.
    // -------------------------------------------------------------------------
    bool isLineInCircle(const aae::math::vec2& p1, const aae::math::vec2& p2) const {
        return isPointInCircle(p1.x, p1.y) || isPointInCircle(p2.x, p2.y);
    }

    bool isLineInCircle(float sx, float sy, float ex, float ey) const {
        return isPointInCircle(sx, sy) || isPointInCircle(ex, ey);
    }

    // Direct access
    float radius;
    float _x;
    float _y;
};

#endif // CIRCLEH
