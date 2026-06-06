#pragma once

#ifndef SYS_TEXTURE_H
#define SYS_TEXTURE_H

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


// -----------------------------------------------------------------------------
// sys_texture.h
//
// Description:
// Simple 2D OpenGL texture wrapper class supporting both legacy (OpenGL 2.x)
// and modern (OpenGL 3/4 core) APIs. Handles loading from file or archive,
// setting filters, blend modes, and rendering configuration. Also includes a
// static snapshot utility to save the current framebuffer to PNG.
//
// Valid inputs include .PNG, .JPG, and other formats supported by stb_image.
// Handles power-of-two warnings, mipmapping, and optional anisotropic filtering.
//
// License: Unlicense / Public Domain
// -----------------------------------------------------------------------------

#include "glew.h"
#include <string>
#include <vector>
#include <mutex>

/// ----------------------------------------------------------------------------
/// Struct: TextureInfo
/// Returns metadata about a loaded texture.
/// ----------------------------------------------------------------------------
struct TextureInfo {
	int width = 0;             // Width in pixels
	int height = 0;            // Height in pixels
	int components = 0;        // Channels (e.g., 3 = RGB, 4 = RGBA)
	std::string name;          // File or resource name
	std::string archive;       // Optional archive ZIP source
	bool loaded = false;       // Whether the texture was successfully loaded
};

/// ----------------------------------------------------------------------------
/// Enum: TextureFilterMode
/// Defines texture filtering modes for min/mag filters and mipmapping.
/// ----------------------------------------------------------------------------
enum TextureFilterMode {
	FILTER_NEAREST = 0,
	FILTER_LINEAR = 1,
	FILTER_MIPMAP_NEAREST = 2,
	FILTER_MIPMAP_LINEAR = 3
};

/// ----------------------------------------------------------------------------
/// Class: TEX
///
/// Represents a single OpenGL texture with automatic upload and configuration.
/// Allows loading from file or archive, binding with filter control, and
/// performing a static framebuffer snapshot to PNG.
///
/// Supports OpenGL 2.x (glTexImage2D) and modern 3/4 (glTexStorage2D).
/// ----------------------------------------------------------------------------
class TEX {
public:
	// -------------------------------------------------------------------------
	// TEX()
	// Default constructor (creates an uninitialized texture)
	// -------------------------------------------------------------------------
	TEX();

	// -------------------------------------------------------------------------
	// TEX(filename, archive, numComponents, filter, modernGL)
	// Loads a texture from file or ZIP archive and uploads it to OpenGL.
	//
	// Parameters:
	// - filename:     Path to texture file (e.g., "myimage.png")
	// - archive:      Optional archive path for zipped resources
	// - numComponents: 0 = auto, 3 = force RGB, 4 = force RGBA
	// - filter:       TextureFilterMode (see enum)
	// - modernGL:     Use modern glTexStorage2D (true) or legacy path (false)
	// -------------------------------------------------------------------------
	TEX(const std::string& filename,
		const std::string& archive = "",
		int filter = FILTER_MIPMAP_LINEAR,
		bool modernGL = true);

	// -------------------------------------------------------------------------
	// ~TEX()
	// Destructor - automatically deletes the OpenGL texture
	// -------------------------------------------------------------------------
	~TEX();

	// -------------------------------------------------------------------------
	// UseTexture(linear, mipmapping)
	// Binds the texture and sets OpenGL filtering mode.
	//
	// Parameters:
	// - linear:     true for linear filtering, false for nearest
	// - mipmapping: true to enable mipmaps (if generated)
	// -------------------------------------------------------------------------
	void UseTexture(bool linear = true, bool mipmapping = true) const;

	// -------------------------------------------------------------------------
	// SetBlendMode(alphaTest)
	// Configures OpenGL alpha handling (alpha test or blend).
	//
	// Parameters:
	// - alphaTest: true = enable GL_ALPHA_TEST, false = enable GL_BLEND
	// -------------------------------------------------------------------------
	void SetBlendMode(bool alphaTest) const;

	// -------------------------------------------------------------------------
	// GetTexID()
	// Returns the OpenGL texture ID (GLuint)
	// -------------------------------------------------------------------------
	GLuint GetTexID() const;

	// -------------------------------------------------------------------------
	// GetInfo()
	// Returns texture metadata (dimensions, format, name, etc.)
	// -------------------------------------------------------------------------
	TextureInfo GetInfo() const;

	// -------------------------------------------------------------------------
    // SaveFramebufferToPNG(width, height, filename, folder)
    // Captures the currently bound framebuffer and writes to PNG.
    //
    // Parameters:
	// - width, height: Framebuffer dimensions in pixels
	// - filename: Desired output file name (".png" will be appended if missing)
	// - folder: Output directory (created if needed, may be blank)
	// -------------------------------------------------------------------------
	static void SaveFramebufferToPNG(int width, int height, const std::string& filename, const std::string& folder);

	// -------------------------------------------------------------------------
	// Snapshot(filename, folder)
	// Captures the current OpenGL framebuffer and writes it as a PNG image.
	//
	// Parameters:
	// - filename: Optional filename (auto-generated if blank)
	// - folder:   Output directory (created if needed, may be blank)
	// -------------------------------------------------------------------------
	static void Snapshot(const std::string& filename, const std::string& folder);

private:
	GLuint texid = 0;              // OpenGL texture handle
	int width = 0;                 // Texture width in pixels
	int height = 0;                // Texture height in pixels
	int comp = 0;                  // Number of components (channels)
	std::string name;              // File name or identifier
	std::string archive;           // ZIP archive name (if used)
	bool loaded = false;           // Load status
	int filter = FILTER_MIPMAP_LINEAR; // Chosen filter mode
	bool modernGL = true;          // Use modern OpenGL texture path
};

#endif // SYS_TEXTURE_H
