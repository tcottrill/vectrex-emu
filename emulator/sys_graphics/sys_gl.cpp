// -----------------------------------------------------------------------------
// Description
// OpenGL Context Creation and Feature Reporting using GLEW/WGL on Windows
// Default build requests OpenGL 4.2; WIN7BUILD requests OpenGL 3.3.
// Falls back to 3.3 then 2.1 if the primary request fails.
// Logs capabilities, enables vsync control.
// -----------------------------------------------------------------------------

#include <windows.h>
#include <cstdio>
#include <string>
#include <sstream>

#include "glew.h"
#include "wglew.h"
#include "sys_log.h"
#include "framework.h"
#include "sys_gl.h"

#pragma comment(lib, "glu32.lib")
#pragma comment(lib, "opengl32.lib")

// -----------------------------------------------------------------------------
// Static Globals
// -----------------------------------------------------------------------------
static HDC hDC = nullptr;
static HGLRC hRC = nullptr;
static bool gOpenGLInitialized = false;

void CheckGLErrorEx(const char* label, const char* file, int line) {
	GLenum err;
	while ((err = glGetError()) != GL_NO_ERROR) {
		const char* errStr = "UNKNOWN_ERROR";
		switch (err) {
		case GL_INVALID_ENUM:                  errStr = "GL_INVALID_ENUM"; break;
		case GL_INVALID_VALUE:                 errStr = "GL_INVALID_VALUE"; break;
		case GL_INVALID_OPERATION:             errStr = "GL_INVALID_OPERATION"; break;
		case GL_STACK_OVERFLOW:                errStr = "GL_STACK_OVERFLOW"; break;
		case GL_STACK_UNDERFLOW:               errStr = "GL_STACK_UNDERFLOW"; break;
		case GL_OUT_OF_MEMORY:                 errStr = "GL_OUT_OF_MEMORY"; break;
		case GL_INVALID_FRAMEBUFFER_OPERATION: errStr = "GL_INVALID_FRAMEBUFFER_OPERATION"; break;
		}

		if (file && label) {
			LOG_ERROR("OpenGL Error [%s] (%#x) in '%s' at %s:%d", errStr, err, label, file, line);
		}
		else if (file) {
			LOG_ERROR("OpenGL Error [%s] (%#x) at %s:%d", errStr, err, file, line);
		}
		else if (label) {
			LOG_ERROR("OpenGL Error [%s] (%#x) in '%s'", errStr, err, label);
		}
		else {
			LOG_ERROR("OpenGL Error [%s] (%#x)", errStr, err);
		}
	}
}

// -----------------------------------------------------------------------------
// ReportOpenGLCapabilities
// Logs hardware limits, extensions, and feature support
// -----------------------------------------------------------------------------
static void ReportOpenGLCapabilities()
{
	LOG_INFO("GL_VENDOR: %s", glGetString(GL_VENDOR));
	LOG_INFO("GL_RENDERER: %s", glGetString(GL_RENDERER));
	LOG_INFO("GL_VERSION: %s", glGetString(GL_VERSION));

	const char* glslVersionStr = reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));
	if (glslVersionStr)
	{
		LOG_INFO("GL_SHADING_LANGUAGE_VERSION: %s", glslVersionStr);
		int major = 0, minor = 0;
		if (sscanf_s(glslVersionStr, "%d.%d", &major, &minor) == 2)
		{
			if (major >= 4)
				LOG_INFO("GLSL 4.x or higher is supported");
			else
				LOG_ERROR("GLSL version is below 4.x - some shaders may be incompatible");
		}
		else {
			LOG_ERROR("Unable to parse GLSL version string");
		}
	}
	else {
		LOG_ERROR("glGetString(GL_SHADING_LANGUAGE_VERSION) returned null");
	}

	GLint maxTexSize = 0;
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexSize);
	LOG_INFO("GL_MAX_TEXTURE_SIZE = %d", maxTexSize);

	GLint maxTexUnits = 0;
	glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &maxTexUnits);
	LOG_INFO("GL_MAX_TEXTURE_IMAGE_UNITS = %d", maxTexUnits);

	GLint maxDrawBuffers = 0;
	glGetIntegerv(GL_MAX_DRAW_BUFFERS, &maxDrawBuffers);
	LOG_INFO("GL_MAX_DRAW_BUFFERS = %d", maxDrawBuffers);

	if (GLEW_EXT_framebuffer_multisample) {
		GLint maxSamples = 0;
		glGetIntegerv(GL_MAX_SAMPLES_EXT, &maxSamples);
		LOG_INFO("GL_EXT_framebuffer_multisample supported: max samples = %d", maxSamples);
	}
	else {
		LOG_ERROR("GL_EXT_framebuffer_multisample NOT supported");
	}

	if (GLEW_ARB_texture_non_power_of_two)		LOG_INFO("GL_ARB_texture_non_power_of_two supported: NPOT textures available.");
	else
		LOG_ERROR("GL_ARB_texture_non_power_of_two NOT supported");

	if (GLEW_EXT_framebuffer_object)		LOG_INFO("GL_EXT_framebuffer_object supported: FBO rendering available.");
	else
		LOG_ERROR("GL_EXT_framebuffer_object NOT supported");

	if (GLEW_ARB_texture_float)		LOG_INFO("GL_ARB_texture_float supported: High precision textures available.");
	else
		LOG_ERROR("GL_ARB_texture_float NOT supported");

	if (GLEW_ARB_shader_objects && GLEW_ARB_shading_language_100)
		LOG_INFO("GLSL (ARB_shader_objects) supported");
	else
		LOG_ERROR("GLSL support NOT available");

	if (wglewIsSupported("WGL_EXT_swap_control")) {
		LOG_INFO("WGL_EXT_swap_control supported: vsync control available");
		if (wglGetSwapIntervalEXT)
			LOG_INFO("Current vsync swap interval = %d", wglGetSwapIntervalEXT());
		else
			LOG_ERROR("wglGetSwapIntervalEXT function pointer not available");
	}
	else {
		LOG_ERROR("WGL_EXT_swap_control NOT supported");
	}
}

// -----------------------------------------------------------------------------
// GetGLDC
// Returns the current device context used by the OpenGL subsystem
// -----------------------------------------------------------------------------
HDC GetGLDC()
{
	return hDC;
}

// -----------------------------------------------------------------------------
// GetGLRC
// Returns the current rendering context created by InitOpenGLContext()
// -----------------------------------------------------------------------------
HGLRC GetGLRC()
{
	return hRC;
}

// -----------------------------------------------------------------------------
// InitOpenGLContext
// Initializes the OpenGL rendering context using WGL, with support for:
// - Legacy OpenGL 2.1 fallback
// - OpenGL 4.2 core or compatibility profile (default build)
// - OpenGL 3.3 core or compatibility profile (WIN7BUILD)
// - Optional multisampling (MSAA) via command-line switch
//
// Parameters:
//   forceLegacyGL2    - Forces use of legacy OpenGL 2.1 context, ignoring modern support
//   enableMultisample - If true, requests 4x MSAA (multisample anti-aliasing) if available
//   useCoreProfile    - If true, requests a forward-compatible core profile context;
//                       otherwise a compatibility profile is requested
//
// Returns:
//   true if context initialization succeeded, false if any stage failed.
//
// Behavior:
//   - Uses a temporary OpenGL context to initialize GLEW and check WGL extensions
//   - Attempts to set a multisample pixel format if requested and supported
//   - Creates either a core or compatibility context at the build's target
//     version (4.2 default / 3.3 under WIN7BUILD), falling back to 3.3 then 2.1
//   - Enables GL_MULTISAMPLE if MSAA was successfully requested
//
// Usage:
//   bool msaa = strstr(lpCmdLine, "-msaa") != nullptr;
//   bool core = strstr(lpCmdLine, "-core") != nullptr;
//   InitOpenGLContext(false, msaa, core);
// -----------------------------------------------------------------------------
bool InitOpenGLContext(bool forceLegacyGL2, bool enableMultisample, bool useCoreProfile)
{
	HWND hwnd = win_get_window();
	hDC = GetDC(hwnd);

	// Step 1: Set temporary pixel format (required to create temp context)
	PIXELFORMATDESCRIPTOR tempPFD = {};
	tempPFD.nSize = sizeof(tempPFD);
	tempPFD.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
	tempPFD.iPixelType = PFD_TYPE_RGBA;
	tempPFD.cColorBits = 32;
	tempPFD.cDepthBits = 24;
	tempPFD.cStencilBits = 8;
	tempPFD.iLayerType = PFD_MAIN_PLANE;

	int tempFormat = ChoosePixelFormat(hDC, &tempPFD);
	if (!tempFormat || !SetPixelFormat(hDC, tempFormat, &tempPFD)) {
		LOG_ERROR("Failed to set temporary pixel format");
		return false;
	}

	// Step 2: Create temporary OpenGL context
	HGLRC tempContext = wglCreateContext(hDC);
	if (!tempContext || !wglMakeCurrent(hDC, tempContext)) {
		LOG_ERROR("Failed to create/make current temporary OpenGL context");
		return false;
	}

	// Step 3: Init GLEW
	glewExperimental = GL_TRUE;
	if (glewInit() != GLEW_OK) {
		LOG_ERROR("GLEW init failed");
		return false;
	}

	// Step 4: If requested, try MSAA with modern pixel format
	if (enableMultisample &&
		wglewIsSupported("WGL_ARB_multisample") &&
		wglewIsSupported("WGL_ARB_pixel_format"))
	{
		const int msaaAttribs[] = {
			WGL_DRAW_TO_WINDOW_ARB, GL_TRUE,
			WGL_SUPPORT_OPENGL_ARB, GL_TRUE,
			WGL_DOUBLE_BUFFER_ARB, GL_TRUE,
			WGL_PIXEL_TYPE_ARB, WGL_TYPE_RGBA_ARB,
			WGL_COLOR_BITS_ARB, 32,
			WGL_DEPTH_BITS_ARB, 24,
			WGL_STENCIL_BITS_ARB, 8,
			WGL_SAMPLE_BUFFERS_ARB, 1,
			WGL_SAMPLES_ARB, 4, // 4x MSAA
			0
		};

		int format;
		UINT numFormats;
		if (wglChoosePixelFormatARB(hDC, msaaAttribs, nullptr, 1, &format, &numFormats) && numFormats > 0) {
			PIXELFORMATDESCRIPTOR finalPFD;
			DescribePixelFormat(hDC, format, sizeof(finalPFD), &finalPFD);
			if (SetPixelFormat(hDC, format, &finalPFD)) {
				LOG_INFO("Using multisample pixel format (4x MSAA)");
			}
			else {
				LOG_INFO("Failed to set multisample pixel format, continuing without MSAA");
			}
		}
		else {
			LOG_INFO("Multisample format not supported, continuing without MSAA");
		}
	}
	else {
		LOG_INFO("MSAA not enabled or not supported - using legacy pixel format");
	}

	// Step 5: Create final OpenGL context
	if (forceLegacyGL2 || !wglewIsSupported("WGL_ARB_create_context")) {
		LOG_INFO("Using legacy OpenGL 2.1 context");
		hRC = tempContext;
	}
	else {
#ifdef WIN7BUILD
		const int reqMajor = 3, reqMinor = 3;
#else
		const int reqMajor = 4, reqMinor = 2;
#endif
		LOG_INFO("Creating OpenGL %d.%d %s profile context", reqMajor, reqMinor,
			useCoreProfile ? "core" : "compatibility");
		const int attribsPrimary[] = {
			WGL_CONTEXT_MAJOR_VERSION_ARB, reqMajor,
			WGL_CONTEXT_MINOR_VERSION_ARB, reqMinor,
			WGL_CONTEXT_FLAGS_ARB, useCoreProfile ? WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB : 0,
			WGL_CONTEXT_PROFILE_MASK_ARB,
				useCoreProfile ? WGL_CONTEXT_CORE_PROFILE_BIT_ARB : WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB,
			0
		};

		hRC = wglCreateContextAttribsARB(hDC, 0, attribsPrimary);

		if (!hRC) {
			LOG_INFO("OpenGL %d.%d not available, trying 3.3 compatibility...", reqMajor, reqMinor);
			const int attribs33[] = {
				WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
				WGL_CONTEXT_MINOR_VERSION_ARB, 3,
				WGL_CONTEXT_FLAGS_ARB, 0,
				WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB,
				0
			};
			hRC = wglCreateContextAttribsARB(hDC, 0, attribs33);
		}

		if (!hRC) {
			LOG_ERROR("wglCreateContextAttribsARB failed - falling back to OpenGL 2.1");
			hRC = tempContext;
		}
		else {
			wglMakeCurrent(nullptr, nullptr);
			wglDeleteContext(tempContext);
			wglMakeCurrent(hDC, hRC);
		}
	}

	if (enableMultisample)
		glEnable(GL_MULTISAMPLE);

	LOG_INFO("OpenGL %s, GLSL %s", glGetString(GL_VERSION), glGetString(GL_SHADING_LANGUAGE_VERSION));
	ReportOpenGLCapabilities();
	return true;
}

// -----------------------------------------------------------------------------
// DeleteGLContext
// Shuts down and deletes the OpenGL rendering context
// -----------------------------------------------------------------------------
void DeleteGLContext()
{
	if (hRC) {
		wglMakeCurrent(nullptr, nullptr);
		wglDeleteContext(hRC);
		hRC = nullptr;
	}
	if (hDC) {
		HWND hwnd = win_get_window(); // <--- re-fetch here
		if (hwnd) {
			ReleaseDC(hwnd, hDC); // only call if hwnd is valid
		}
		hDC = nullptr;
	}
	gOpenGLInitialized = false;
}

// -----------------------------------------------------------------------------
// SetvSync
// Enables or disables vertical sync if available
// -----------------------------------------------------------------------------
void SetvSync(bool enabled)
{
	if (wglSwapIntervalEXT)
		wglSwapIntervalEXT(enabled ? 1 : 0);
	else
		LOG_INFO("SetvSync called, but WGL_EXT_swap_control not supported");
}

// -----------------------------------------------------------------------------
// GLSwapBuffers
// Swaps the front and back buffers
// -----------------------------------------------------------------------------
void GLSwapBuffers()
{
	if (hDC) {
		SwapBuffers(hDC);
	}
	else {
		LOG_ERROR("GLSwapBuffers called with null device context");
	}
}

// -----------------------------------------------------------------------------
// CheckGLVersionSupport
// Displays warning if OpenGL version is below 2.0
// -----------------------------------------------------------------------------
void CheckGLVersionSupport()
{
	const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
	int major = 0, minor = 0;

	// Use sscanf_s with format and provide buffer sizes
	if (sscanf_s(version, "%d.%d", &major, &minor) != 2)
	{
		LOG_DEBUG("Failed to parse OpenGL version string: %s", version);
		return;
	}

	LOG_DEBUG("OpenGL Version supported %d.%d", major, minor);

	if (major < 2)
	{
		MessageBox(nullptr, L"This program may not work. Your OpenGL version is less than 2.0.",
			L"OpenGL Version Warning", MB_ICONERROR | MB_OK);
	}
}

// -----------------------------------------------------------------------------
// ReSizeGLScene
// Resets OpenGL viewport
// -----------------------------------------------------------------------------

float ReSizeGLScene(GLsizei width, GLsizei height)
{
	if (height == 0) height = 1;
	glViewport(0, 0, width, height);
	return static_cast<float>(width) / height;
}

// -----------------------------------------------------------------------------
// ViewOrtho
// Sets up 2D orthographic projection
// -----------------------------------------------------------------------------
void ViewOrtho(int width, int height)
{
	//glViewport(0, 0, width, height);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, width, 0, height, -1, 1);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
}

// -----------------------------------------------------------------------------
// IsOpenGLInitialized
// Returns true if InitOpenGLContext() was successful
// -----------------------------------------------------------------------------
bool IsOpenGLInitialized()
{
	return gOpenGLInitialized;
}