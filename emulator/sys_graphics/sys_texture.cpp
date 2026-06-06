#include <cmath>
#include <algorithm>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <mutex>
#define NOMINMAX
#include <windows.h>

#include "sys_texture.h"
#include "sys_gl.h"
#include "sys_log.h"
#include "framework.h"
#include "sys_fileio.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_write.h"

TEX::TEX() = default;

TEX::~TEX() {
	if (texid) {
		glDeleteTextures(1, &texid);
	}
}

TEX::TEX(const std::string& filename, const std::string& archive, int filter, bool modernGL)
	: name(filename), archive(archive),  filter(filter), modernGL(modernGL), loaded(false)
{
	stbi_set_flip_vertically_on_load(true);
	
	unsigned char* image_data = nullptr;

	if (archive.empty()) {
			image_data = stbi_load(filename.c_str(), &width, &height, &comp, STBI_rgb_alpha);
		
	}
	else {
		image_data = loadZip(archive.c_str(), filename.c_str());
		size_t zsize = getLastZSize();
		int buffer_size = static_cast<int>(zsize);
		comp = 4;
		image_data = stbi_load_from_memory(image_data, buffer_size, &width, &height, &comp, STBI_rgb_alpha);
	}

	if (!image_data) {
		LOG_INFO("ERROR: Could not load texture %s", filename.c_str());
		return;
	}

	if ((width & (width - 1)) || (height & (height - 1))) {
		LOG_INFO("WARNING: Texture %s is not power-of-two.", filename.c_str());
	}

	glGenTextures(1, &texid);
	glBindTexture(GL_TEXTURE_2D, texid);

	check_gl_error();

	if (!modernGL) {
		// OpenGL 2.x legacy path
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);

		switch (filter) {
		case FILTER_MIPMAP_NEAREST:
			gluBuild2DMipmaps(GL_TEXTURE_2D, GL_RGBA, width, height, GL_RGBA, GL_UNSIGNED_BYTE, image_data);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			break;
		case FILTER_MIPMAP_LINEAR:
			gluBuild2DMipmaps(GL_TEXTURE_2D, GL_RGBA, width, height, GL_RGBA, GL_UNSIGNED_BYTE, image_data);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			break;
		case FILTER_NEAREST:
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			break;
		default: // FILTER_LINEAR
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			break;
		}
	}
	else {
		// Modern OpenGL path
		int mipLevels = static_cast<int>(std::floor(std::log2(std::max(width, height)))) + 1;
		glTexStorage2D(GL_TEXTURE_2D, mipLevels, GL_RGBA8, width, height);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, image_data);

		switch (filter) {
		case FILTER_MIPMAP_NEAREST:
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glGenerateMipmap(GL_TEXTURE_2D);
			break;
		case FILTER_MIPMAP_LINEAR:
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glGenerateMipmap(GL_TEXTURE_2D);
			break;
		case FILTER_NEAREST:
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			break;
		default: // FILTER_LINEAR
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			break;
		}
	}

	check_gl_error();

	if (glewIsExtensionSupported("GL_EXT_texture_filter_anisotropic")) {
		GLfloat maxAniso = 0.0f;
		glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, maxAniso);
	}

	stbi_image_free(image_data);

	loaded = true;

	LOG_INFO("Texture Loaded: ID=%u Size=%dx%d Components=%d File=%s", texid, width, height, comp, name.c_str());
}

void TEX::UseTexture(bool linear, bool mipmapping) const {
	glBindTexture(GL_TEXTURE_2D, texid);

	GLint minFilter, magFilter;

	if (linear) {
		magFilter = GL_LINEAR;
		minFilter = mipmapping ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR;
	}
	else {
		magFilter = GL_NEAREST;
		minFilter = mipmapping ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST;
	}

	// Only legal for mutable textures in legacy GL
	if (!modernGL) {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);
	}
	else {
		// For immutable storage, filtering must have been set at creation time
		// Still safe to call, but only useful if texture is mutable or
		// OpenGL accepts override (some implementations do)
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);
	}
}

void TEX::SetBlendMode(bool alphaTest) const {
	if (alphaTest) {
		glEnable(GL_ALPHA_TEST);
		glDisable(GL_BLEND);
		glAlphaFunc(GL_GREATER, 0.5f);
	}
	else {
		glDisable(GL_ALPHA_TEST);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
}

GLuint TEX::GetTexID() const {
	return texid;
}

TextureInfo TEX::GetInfo() const {
	return TextureInfo{
		width,
		height,
		comp,
		name,
		archive,
		loaded
	};
}

// -----------------------------------------------------------------------------
// SaveFramebufferToPNG
// Static helper to dump the current framebuffer (bottom-left origin) to PNG
// -----------------------------------------------------------------------------
void TEX::SaveFramebufferToPNG(int width, int height, const std::string& filename, const std::string& folder)
{
	std::vector<unsigned char> buffer(static_cast<size_t>(width) * height * 4);
	glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, buffer.data());


	// Force RGB = 0 to have alpha = 0
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			unsigned char* px = &buffer[(y * width + x) * 4];
			if (px[0] == 0 && px[1] == 0 && px[2] == 0)
				px[3] = 0; // set alpha to 0
		}
	}

	// Flip vertically
	for (int y = 0; y < height / 2; ++y) {
		std::swap_ranges(
			buffer.data() + y * width * 4,
			buffer.data() + (y + 1) * width * 4,
			buffer.data() + (height - y - 1) * width * 4
		);
	}

	std::string outName = filename;
	std::filesystem::path pathCheck(outName);
	if (!pathCheck.has_extension())
		outName += ".png";

	std::filesystem::path outPath;
	if (!folder.empty()) {
		std::filesystem::create_directories(folder);
		outPath = std::filesystem::path(folder) / outName;
	}
	else {
		outPath = outName;
	}

	stbi_write_png(outPath.string().c_str(), width, height, 4, buffer.data(), width * 4);
	LOG_INFO("Sprite snapshot saved: %s", outPath.string().c_str());
}


void TEX::Snapshot(const std::string& filename, const std::string& folder) {
	RECT rc{};
	GetClientRect(win_get_window(), &rc);
	int width = rc.right - rc.left;
	int height = rc.bottom - rc.top;

	if (width <= 0 || height <= 0) {
		LOG_ERROR("TEX::Snapshot - invalid window size");
		return;
	}

	std::vector<unsigned char> buffer(static_cast<size_t>(width) * height * 4);
	glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, buffer.data());

	for (int y = 0; y < height / 2; ++y) {
		std::swap_ranges(buffer.data() + y * width * 4,
			buffer.data() + (y + 1) * width * 4,
			buffer.data() + (height - y - 1) * width * 4);
	}

	std::string outName = filename;
	if (outName.empty()) {
		const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
		tm tmNow{};
		localtime_s(&tmNow, &now);

		std::ostringstream oss;
		oss << std::put_time(&tmNow, "%Y%m%d_%H%M%S");
		std::string stamp = oss.str();

		outName = std::string("snapshot") + "_" + stamp + ".png";
	}
	else {
		std::filesystem::path pathCheck(outName);
		if (!pathCheck.has_extension()) {
			outName += ".png";
		}
	}

	std::filesystem::path outPath;
	if (!folder.empty()) {
		std::filesystem::create_directories(folder);
		outPath = std::filesystem::path(folder) / outName;
	}
	else {
		outPath = outName;
	}

	stbi_write_png(outPath.string().c_str(), width, height, 4, buffer.data(), width * 4);
	LOG_INFO("Snapshot saved: %s", outPath.string().c_str());
}