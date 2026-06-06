
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



/*
* Copyright (c) 2006-2007 Erin Catto http://www.gphysics.com
*
* Permission to use, copy, modify, distribute and sell this software
* and its documentation for any purpose is hereby granted without fee,
* provided that the above copyright notice appear in all copies.
* Erin Catto makes no representations about the suitability
* of this software for any purpose.
* It is provided "as is" without express or implied warranty.
*/

// Modifed 2018 & 2019 & radically changed in 11/2025 to add GLM replacement functions TC.

#ifndef MATHUTILS_H
#define MATHUTILS_H

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#if defined(clamp)
#pragma push_macro("clamp")
#undef clamp
#define AAE_RESTORE_CLAMP_MACRO 1
#endif
/*
	Minimal GLM-like math header for AAE.
	- Namespace: aae::math
	- Lowercase, GLM-style API names
	- Column-major matrices (OpenGL-friendly)
	- No GLM shim; this is a replacement
*/

#include <cmath>
#include <cfloat>
#include <cassert>
#include <cstdlib>
#include <cstdint>

#pragma warning(push)
#pragma warning(disable : 4244)

namespace aae {
	namespace math {
		// -----------------------------------------------------------------------------
		// constants
		// -----------------------------------------------------------------------------
		constexpr float pi = 3.14159265358979323846264f;
		struct ivec2;  // forward declaration so vec2 can declare a ctor that takes ivec2

		// -----------------------------------------------------------------------------
		// vec2
		// -----------------------------------------------------------------------------
		struct vec2 {
			float x = 0.0f;
			float y = 0.0f;

			vec2() = default;
			vec2(float x_, float y_) : x(x_), y(y_) {}
			explicit vec2(float v) : x(v), y(v) {}
			void set(float x_, float y_) { x = x_; y = y_; }

			// accept integers so brace-init {int,int} is not narrowing
			vec2(int x_, int y_) : x(static_cast<float>(x_)), y(static_cast<float>(y_)) {}
			explicit vec2(int v) : x(static_cast<float>(v)), y(static_cast<float>(v)) {}

			explicit vec2(const ivec2& v);  // defined later, after ivec2 is fully defined

			vec2 operator-() const { return vec2(-x, -y); }

			vec2& operator+=(const vec2& v) { x += v.x; y += v.y; return *this; }
			vec2& operator-=(const vec2& v) { x -= v.x; y -= v.y; return *this; }
			vec2& operator*=(float s) { x *= s;   y *= s;   return *this; }
			vec2& operator/=(float s) { float inv = 1.0f / s; x *= inv; y *= inv; return *this; }
			// Dot product with another vec2
			inline float dot(const vec2& v) const { return x * v.x + y * v.y; }

			// 90-degree CCW perpendicular (x, y) -> (-y, x)
			inline vec2 perp() const { return vec2(-y, x); }
		};

		// scalar min/max
		template<typename T>
		inline T min(T a, T b) { return (a < b) ? a : b; }

		template<typename T>
		inline T max(T a, T b) { return (a > b) ? a : b; }

		template<typename T>
		inline T clamp(T x, T lo, T hi) {
			return (x < lo) ? lo : ((x > hi) ? hi : x);
		}

		inline vec2 operator+(const vec2& a, const vec2& b) { return vec2(a.x + b.x, a.y + b.y); }
		inline vec2 operator-(const vec2& a, const vec2& b) { return vec2(a.x - b.x, a.y - b.y); }
		inline vec2 operator*(const vec2& v, float s) { return vec2(v.x * s, v.y * s); }
		inline vec2 operator*(float s, const vec2& v) { return vec2(v.x * s, v.y * s); }
		inline vec2 operator/(const vec2& v, float s) { float inv = 1.0f / s; return vec2(v.x * inv, v.y * inv); }

		// component-wise min/max for vec2
		inline vec2 min(const vec2& a, const vec2& b) {
			return vec2(min(a.x, b.x), min(a.y, b.y));
		}

		inline vec2 max(const vec2& a, const vec2& b) {
			return vec2(max(a.x, b.x), max(a.y, b.y));
		}
		// -----------------------------------------------------------------------------
		// ivec2 (GLM-like integer 2D vector)
		// -----------------------------------------------------------------------------
		struct ivec2 {
			int x = 0;
			int y = 0;

			ivec2() = default;
			ivec2(int x_, int y_) : x(x_), y(y_) {}
			explicit ivec2(int v) : x(v), y(v) {}

			// Explicit on purpose: GLM does not implicitly convert float -> int vector
			explicit ivec2(const vec2& v) : x(static_cast<int>(v.x)), y(static_cast<int>(v.y)) {}

			ivec2 operator-() const { return ivec2(-x, -y); }

			ivec2& operator+=(const ivec2& v) { x += v.x; y += v.y; return *this; }
			ivec2& operator-=(const ivec2& v) { x -= v.x; y -= v.y; return *this; }
			ivec2& operator*=(int s) { x *= s;   y *= s;   return *this; }
			ivec2& operator/=(int s) { x /= s;   y /= s;   return *this; }
		};
		inline vec2::vec2(const ivec2& v) : x(static_cast<float>(v.x)), y(static_cast<float>(v.y)) {}
		// Free operators
		inline ivec2 operator+(const ivec2& a, const ivec2& b) { return ivec2(a.x + b.x, a.y + b.y); }
		inline ivec2 operator-(const ivec2& a, const ivec2& b) { return ivec2(a.x - b.x, a.y - b.y); }
		inline ivec2 operator*(const ivec2& v, int s) { return ivec2(v.x * s, v.y * s); }
		inline ivec2 operator*(int s, const ivec2& v) { return ivec2(s * v.x, s * v.y); }
		inline ivec2 operator/(const ivec2& v, int s) { return ivec2(v.x / s, v.y / s); }

		// Equality
		inline bool operator==(const ivec2& a, const ivec2& b) { return a.x == b.x && a.y == b.y; }
		inline bool operator!=(const ivec2& a, const ivec2& b) { return !(a == b); }

		// Conversions to/from vec2 (explicit both ways to avoid surprises)
		inline vec2 to_vec2(const ivec2& v) { return vec2(static_cast<float>(v.x), static_cast<float>(v.y)); }
		inline ivec2 to_ivec2(const vec2& v) { return ivec2(static_cast<int>(v.x), static_cast<int>(v.y)); }

		inline ivec2 min(const ivec2& a, const ivec2& b) {
			return ivec2(aae::math::min<int>(a.x, b.x),
				aae::math::min<int>(a.y, b.y));
		}
		inline ivec2 max(const ivec2& a, const ivec2& b) {
			return ivec2(aae::math::max<int>(a.x, b.x),
				aae::math::max<int>(a.y, b.y));
		}

		inline ivec2 clamp(const ivec2& v, const ivec2& lo, const ivec2& hi) {
			return ivec2(
				clamp<int>(v.x, lo.x, hi.x),
				clamp<int>(v.y, lo.y, hi.y)
			);
		}

		// Integer dot (matches GLM behavior of returning the same value type)
		inline int dot(const ivec2& a, const ivec2& b) {
			return a.x * b.x + a.y * b.y;
		}

		// -----------------------------------------------------------------------------
		// vec3 (GLM-like 3D vector) and ivec3
		// -----------------------------------------------------------------------------
		struct vec3 {
			float x = 0.0f;
			float y = 0.0f;
			float z = 0.0f;

			vec3() = default;
			vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
			explicit vec3(float v) : x(v), y(v), z(v) {}

			// accept integers so brace-init {int,int,int} is not narrowing
			vec3(int x_, int y_, int z_) : x((float)x_), y((float)y_), z((float)z_) {}
			explicit vec3(int v) : x((float)v), y((float)v), z((float)v) {}

			vec3 operator-() const { return vec3(-x, -y, -z); }

			vec3& operator+=(const vec3& v) { x += v.x; y += v.y; z += v.z; return *this; }
			vec3& operator-=(const vec3& v) { x -= v.x; y -= v.y; z -= v.z; return *this; }
			vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
			vec3& operator/=(float s) { float inv = 1.0f / s; x *= inv; y *= inv; z *= inv; return *this; }

			inline float dot(const vec3& v) const { return x * v.x + y * v.y + z * v.z; }
		};

		// Free operators (vec3)
		inline vec3 operator+(const vec3& a, const vec3& b) { return vec3(a.x + b.x, a.y + b.y, a.z + b.z); }
		inline vec3 operator-(const vec3& a, const vec3& b) { return vec3(a.x - b.x, a.y - b.y, a.z - b.z); }
		inline vec3 operator*(const vec3& v, float s) { return vec3(v.x * s, v.y * s, v.z * s); }
		inline vec3 operator*(float s, const vec3& v) { return vec3(s * v.x, s * v.y, s * v.z); }
		inline vec3 operator/(const vec3& v, float s) { float inv = 1.0f / s; return vec3(v.x * inv, v.y * inv, v.z * inv); }
		
		// component-wise min/max/clamp (vec3)
		inline vec3 min(const vec3& a, const vec3& b) {
			return vec3(min(a.x, b.x), min(a.y, b.y),min(a.z, b.z));
		}
		inline vec3 max(const vec3& a, const vec3& b) {
			return vec3(max(a.x, b.x), max(a.y, b.y), max(a.z, b.z));
		}
		inline vec3 clamp(const vec3& v, const vec3& lo, const vec3& hi) {
			return vec3(
				clamp(v.x, lo.x, hi.x),
				clamp(v.y, lo.y, hi.y),
				clamp(v.z, lo.z, hi.z)
			);
		}
		
		// length helpers (vec3)
		inline float length2(const vec3& v) { return v.x * v.x + v.y * v.y + v.z * v.z; }
		inline float length(const vec3& v) { return std::sqrt(length2(v)); }
		inline float distance(const vec3& a, const vec3& b) {
			float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
			return std::sqrt(dx * dx + dy * dy + dz * dz);
		}
		inline vec3 normalize(const vec3& v) {
			float ls = length2(v);
			if (ls > FLT_EPSILON * FLT_EPSILON) {
				float inv = 1.0f / std::sqrt(ls);
				return vec3(v.x * inv, v.y * inv, v.z * inv);
			}
			return vec3(0.0f, 0.0f, 0.0f);
		}

		// cross product (right-handed)
		inline vec3 cross(const vec3& a, const vec3& b) {
			return vec3(
				a.y * b.z - a.z * b.y,
				a.z * b.x - a.x * b.z,
				a.x * b.y - a.y * b.x
			);
		}

		// -----------------------------------------------------------------------------
		// ivec3 (integer 3D vector, explicit float->int conversion like GLM)
		// -----------------------------------------------------------------------------
		struct ivec3 {
			int x = 0, y = 0, z = 0;

			ivec3() = default;
			ivec3(int x_, int y_, int z_) : x(x_), y(y_), z(z_) {}
			explicit ivec3(int v) : x(v), y(v), z(v) {}
			explicit ivec3(const vec3& v) : x((int)v.x), y((int)v.y), z((int)v.z) {}
			ivec3 operator-() const { return ivec3(-x, -y, -z); }

			ivec3& operator+=(const ivec3& v) { x += v.x; y += v.y; z += v.z; return *this; }
			ivec3& operator-=(const ivec3& v) { x -= v.x; y -= v.y; z -= v.z; return *this; }
			ivec3& operator*=(int s) { x *= s; y *= s; z *= s; return *this; }
			ivec3& operator/=(int s) { x /= s; y /= s; z /= s; return *this; }
		};

		// Free operators (ivec3)
		inline ivec3 operator+(const ivec3& a, const ivec3& b) { return ivec3(a.x + b.x, a.y + b.y, a.z + b.z); }
		inline ivec3 operator-(const ivec3& a, const ivec3& b) { return ivec3(a.x - b.x, a.y - b.y, a.z - b.z); }
		inline ivec3 operator*(const ivec3& v, int s) { return ivec3(v.x * s, v.y * s, v.z * s); }
		inline ivec3 operator*(int s, const ivec3& v) { return ivec3(s * v.x, s * v.y, s * v.z); }
		inline ivec3 operator/(const ivec3& v, int s) { return ivec3(v.x / s, v.y / s, v.z / s); }

		inline bool operator==(const ivec3& a, const ivec3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
		inline bool operator!=(const ivec3& a, const ivec3& b) { return !(a == b); }

		inline int dot(const ivec3& a, const ivec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
		
		inline ivec3 min(const ivec3& a, const ivec3& b) {
			return ivec3(aae::math::min<int>(a.x, b.x),
				aae::math::min<int>(a.y, b.y),
				aae::math::min<int>(a.z, b.z));
		}
		inline ivec3 max(const ivec3& a, const ivec3& b) {
			return ivec3(aae::math::max<int>(a.x, b.x),
				aae::math::max<int>(a.y, b.y),
				aae::math::max<int>(a.z, b.z));
		}
		inline ivec3 clamp(const ivec3& v, const ivec3& lo, const ivec3& hi) {
			return ivec3(
				clamp<int>(v.x, lo.x, hi.x),
				clamp<int>(v.y, lo.y, hi.y),
				clamp<int>(v.z, lo.z, hi.z)
			);
		}
		// -----------------------------------------------------------------------------
		// value_ptr overloads for vec3 / ivec3
		// -----------------------------------------------------------------------------
		inline const float* value_ptr(const vec3& v) { return &v.x; }
		inline float* value_ptr(vec3& v) { return &v.x; }
		inline const int* value_ptr(const ivec3& v) { return &v.x; }
		inline int* value_ptr(ivec3& v) { return &v.x; }

		// angles
		inline float radians(float deg) { return deg * (pi / 180.0f); }
		inline float degrees(float rad) { return rad * (180.0f / pi); }

		// -----------------------------------------------------------------------------
		// mat2 (2x2, column-major)
		// -----------------------------------------------------------------------------
		struct mat2 {
			// Stored as [col0.x, col0.y, col1.x, col1.y]
			float m[4];

			mat2() : m{ 1,0, 0,1 } {}
			mat2(float a, float b, float c, float d) : m{ a,b, c,d } {} // [a b; c d] in column-major is a,b,c,d

			static mat2 rotation(float radians) {
				float c = std::cos(radians), s = std::sin(radians);
				// [ c -s; s  c ]  -> column-major: c, s, -s, c
				return mat2(c, s, -s, c);
			}
		};

		inline vec2 operator*(const mat2& A, const vec2& v) {
			// column-major: [ m0 m2; m1 m3 ]
			return vec2(A.m[0] * v.x + A.m[2] * v.y,
				A.m[1] * v.x + A.m[3] * v.y);
		}

		inline mat2 operator*(const mat2& A, const mat2& B) {
			// A*B in column-major
			return mat2(
				A.m[0] * B.m[0] + A.m[2] * B.m[1],  // col0.x
				A.m[1] * B.m[0] + A.m[3] * B.m[1],  // col0.y
				A.m[0] * B.m[2] + A.m[2] * B.m[3],  // col1.x
				A.m[1] * B.m[2] + A.m[3] * B.m[3]   // col1.y
			);
		}

		// -----------------------------------------------------------------------------
		// mat4 (4x4, column-major)
		// -----------------------------------------------------------------------------
		struct mat4 {
			// Column-major 4x4: m[0..15], columns of 4
			float m[16];
			mat4() : m{
				1,0,0,0,
				0,1,0,0,
				0,0,1,0,
				0,0,0,1
			} {
			}
			operator const float* () const { return m; }
		};

		// -----------------------------------------------------------------------------
		// ortho (OpenGL-style NDC: z in [-1, 1])
		// -----------------------------------------------------------------------------
		inline mat4 make_ortho(float left, float right, float bottom, float top,
			float near_val, float far_val)
		{
			mat4 M{};
			float rl = right - left;
			float tb = top - bottom;
			float fn = far_val - near_val;

			assert(rl != 0.0f && tb != 0.0f && fn != 0.0f);

			M.m[0] = 2.0f / rl;  M.m[4] = 0.0f;        M.m[8] = 0.0f;         M.m[12] = -(right + left) / rl;
			M.m[1] = 0.0f;       M.m[5] = 2.0f / tb;   M.m[9] = 0.0f;         M.m[13] = -(top + bottom) / tb;
			M.m[2] = 0.0f;       M.m[6] = 0.0f;        M.m[10] = -2.0f / fn;   M.m[14] = -(far_val + near_val) / fn;
			M.m[3] = 0.0f;       M.m[7] = 0.0f;        M.m[11] = 0.0f;         M.m[15] = 1.0f;
			return M;
		}

		inline mat4 ortho(float left, float right, float bottom, float top,
			float near_val, float far_val)
		{
			return make_ortho(left, right, bottom, top, near_val, far_val);
		}

		inline mat4 ortho(float left, float right, float bottom, float top)
		{
			return make_ortho(left, right, bottom, top, -1.0f, 1.0f);
		}

		// -----------------------------------------------------------------------------
		// value_ptr (GLM-style)
		// Returns a pointer to the first scalar element of the object.
		// Overloads provided for const and non-const refs.
		// -----------------------------------------------------------------------------

		// vec2
		inline const float* value_ptr(const vec2& v) { return &v.x; }
		inline float* value_ptr(vec2& v) { return &v.x; }

		// ivec2
		inline const int* value_ptr(const ivec2& v) { return &v.x; }
		inline int* value_ptr(ivec2& v) { return &v.x; }

		// mat2 (column-major, contiguous in mat2::m)
		inline const float* value_ptr(const mat2& m) { return m.m; }
		inline float* value_ptr(mat2& m) { return m.m; }

		// mat4 (column-major, contiguous in mat4::m)
		inline const float* value_ptr(const mat4& m) { return m.m; }
		inline float* value_ptr(mat4& m) { return m.m; }

		// -----------------------------------------------------------------------------
		// free functions: dot, length, length2, distance, normalize, clamp
		// -----------------------------------------------------------------------------
		inline float dot(const vec2& a, const vec2& b) {
			return a.x * b.x + a.y * b.y;
		}

		inline float length2(const vec2& v) {
			return v.x * v.x + v.y * v.y;
		}

		inline float length(const vec2& v) {
			return std::sqrt(length2(v));
		}

		inline float distance(const vec2& a, const vec2& b) {
			float dx = a.x - b.x;
			float dy = a.y - b.y;
			return std::sqrt(dx * dx + dy * dy);
		}

		inline vec2 normalize(const vec2& v) {
			float ls = length2(v);
			if (ls > FLT_EPSILON * FLT_EPSILON) {
				float inv = 1.0f / std::sqrt(ls);
				return vec2(v.x * inv, v.y * inv);
			}
			return vec2(0.0f, 0.0f);
		}

		// Component-wise clamp for vec2
		inline vec2 clamp(const vec2& v, const vec2& lo, const vec2& hi) {
			float cx = (v.x < lo.x) ? lo.x : ((v.x > hi.x) ? hi.x : v.x);
			float cy = (v.y < lo.y) ? lo.y : ((v.y > hi.y) ? hi.y : v.y);
			return vec2(cx, cy);
		}

		inline vec2 perp(const vec2& v) { return vec2(-v.y, v.x); }        // CCW
		inline vec2 perp_cw(const vec2& v) { return vec2(v.y, -v.x); }     // CW (optional)

		// -----------------------------------------------------------------------------
		// lerp (aka mix) for scalars and vec2
		// -----------------------------------------------------------------------------
		template<typename T>
		inline T lerp(const T& a, const T& b, float t) {
			return a + (b - a) * t;
		}

		// Optional alias if you like GLM's name:
		template<typename T>
		inline T mix(const T& a, const T& b, float t) {
			return lerp(a, b, t);
		}

		// -----------------------------------------------------------------------------
		// sign (GLSL/GLM-style): returns -1, 0, or +1
		// -----------------------------------------------------------------------------
		inline int sign(int x) { return (x > 0) ? 1 : ((x < 0) ? -1 : 0); }
		inline float sign(float x) { return (x > 0.0f) ? 1.0f : ((x < 0.0f) ? -1.0f : 0.0f); }
		inline double sign(double x) { return (x > 0.0) ? 1.0 : ((x < 0.0) ? -1.0 : 0.0); }

		// -----------------------------------------------------------------------------
		// swap (generic)
		// -----------------------------------------------------------------------------
		template<typename T>
		inline void swap(T& a, T& b) {
			T tmp = static_cast<T&&>(a);
			a = static_cast<T&&>(b);
			b = static_cast<T&&>(tmp);
		}

		// -----------------------------------------------------------------------------
		// random helpers (lowercase, GLM-like style)
		// -----------------------------------------------------------------------------

		// Random float in [-1, 1]
		inline float random() {
			float r = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
			return 2.0f * r - 1.0f;
		}

		// Random float in [lo, hi]
		inline float random(float lo, float hi) {
			float r = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
			return (hi - lo) * r + lo;
		}
	}
} // namespace aae::math

// hash support for aae::math::ivec2
#include <functional>
namespace std {
	template<> struct hash<aae::math::ivec2> {
		size_t operator()(const aae::math::ivec2& v) const noexcept {
			// simple 64-bit mix of two 32-bit ints
			const uint64_t x = static_cast<uint32_t>(v.x);
			const uint64_t y = static_cast<uint32_t>(v.y);
			return static_cast<size_t>((x << 32) ^ y);
		}
	};
}

#pragma warning(pop)
// Restore prior clamp macro if we temporarily undefined it
#ifdef AAE_RESTORE_CLAMP_MACRO
#pragma pop_macro("clamp")
#undef AAE_RESTORE_CLAMP_MACRO
#endif
#endif // MATHUTILS_H
