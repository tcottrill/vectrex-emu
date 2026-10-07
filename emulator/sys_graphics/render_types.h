#pragma once
// ====================================================================
// render_types.h - backend-neutral render handle types
//
// Public render headers use these instead of raw GL types so that
// non-render code (drivers, emulator core, menu, GUI) never sees
// glew.h. On the GL backend a handle holds a GL object name (GLuint);
// a Vulkan backend maps handles to its own object tables.
//
// uint32_t is bit-identical to GLuint on MSVC, so GL-backend .cpp
// files pass these straight into GL calls with no casts.
// ====================================================================
#include <cstdint>

using rtex_t  = std::uint32_t;  // texture object
using rprog_t = std::uint32_t;  // shader program
using rfbo_t  = std::uint32_t;  // framebuffer object
using rvao_t  = std::uint32_t;  // vertex array object
using rbuf_t  = std::uint32_t;  // vertex/index buffer object
