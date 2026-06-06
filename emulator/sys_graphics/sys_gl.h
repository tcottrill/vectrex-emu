// -----------------------------------------------------------------------------
// gl_basics.h
// Basic OpenGL 2.1+ initialization and context management for Windows (GLEW/WGL)
// -----------------------------------------------------------------------------

#pragma once
#ifndef GL_BASICS_H
#define GL_BASICS_H

#include "glew.h"
#include "wglew.h"
#include "sys_log.h"

// -----------------------------------------------------------------------------
// Initializes the OpenGL context
// If forceLegacyGL2 is true, skips 4.x context creation
// Returns true on success
// -----------------------------------------------------------------------------
bool InitOpenGLContext(bool forceLegacyGL2, bool enableMultisample = false, bool useCoreProfile = false);

// -----------------------------------------------------------------------------
// Deletes the OpenGL rendering context and resets internal flags
// -----------------------------------------------------------------------------
void DeleteGLContext();

// -----------------------------------------------------------------------------
// Returns true if OpenGL was successfully initialized
// -----------------------------------------------------------------------------
bool IsOpenGLInitialized();

// -----------------------------------------------------------------------------
// Swaps front and back buffers
// -----------------------------------------------------------------------------
void GLSwapBuffers();

// -----------------------------------------------------------------------------
// Enables or disables vertical sync if WGL extension is available
// -----------------------------------------------------------------------------
void SetvSync(bool enabled);

// -----------------------------------------------------------------------------
// GetGLDC
// Returns the current Win32 device context used by OpenGL
// -----------------------------------------------------------------------------
HDC GetGLDC();

// -----------------------------------------------------------------------------
// GetGLRC
// Returns the current OpenGL rendering context handle
// -----------------------------------------------------------------------------
HGLRC GetGLRC();

// -----------------------------------------------------------------------------
// Logs OpenGL version support and optionally warns if < 2.0
// -----------------------------------------------------------------------------
void CheckGLVersionSupport();

// -----------------------------------------------------------------------------
// Sets OpenGL viewport
// -----------------------------------------------------------------------------
float ReSizeGLScene(GLsizei width, GLsizei height);

// -----------------------------------------------------------------------------
// Sets 2D orthographic projection for screen rendering
// -----------------------------------------------------------------------------
void ViewOrtho(int width, int height);

void CheckGLErrorEx(const char* label = nullptr, const char* file = nullptr, int line = 0);

#define check_gl_error()          CheckGLErrorEx(nullptr, __FILE__, __LINE__)
#define check_gl_error_named(x)   CheckGLErrorEx(x, __FILE__, __LINE__)
#define check_gl_error_simple()   CheckGLErrorEx()
#endif // GL_BASICS_H
