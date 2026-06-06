#pragma once

// -----------------------------------------------------------------------------
// [vectrex-port] Trimmed local copy of the AAE texture_handler.h.
//
// The real AAE texture_handler.{h,cpp} is a full ZIP-backed artwork/texture
// loader (stb_image, miniz, snapshot, palette pens, ...). The vector-glow
// renderer only needs:
//   - art_tex[]   : artwork texture slots (slot 1 = color overlay gel)
//   - set_texture : bind + set filtering/wrap state for a texture
//
// Both are provided by aae_compat.cpp. The loader entry points
// (load_texture / make_single_bitmap / get_texture_size / snapshot / ...) were
// only referenced by the CUT raster + scanline-overlay paths and are omitted.
//
// The Vectrex wiring step installs the overlay texture by writing its GL handle
// into art_tex[1] and setting art_loaded[1] = 1.
// -----------------------------------------------------------------------------

#ifndef LOADERS_H
#define LOADERS_H

#include "sys_gl.h"

// Artwork texture slots. Mirrors the AAE layout:
//   art_tex[0] backdrop, art_tex[1] overlay, art_tex[3] bezel frame.
// The Vectrex only uses slot 1 (overlay gel) for now.
extern GLuint art_tex[8];

// Bind 'texture' to GL_TEXTURE_2D and configure filtering/wrapping/blend state.
//   linear     : GL_LINEAR (true) vs GL_NEAREST (false) magnification
//   mipmapping : use trilinear min filter when true
//   blending   : enable standard alpha blending when true
//   set_color  : reset glColor to opaque white when true
void set_texture(GLuint* texture, GLboolean linear, GLboolean mipmapping, GLboolean blending, GLboolean set_color);

#endif // LOADERS_H
