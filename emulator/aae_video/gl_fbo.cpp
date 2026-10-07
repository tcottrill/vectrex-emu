//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2025/2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
//
// gl_fbo.cpp
//
// Framebuffer Object (FBO) allocation and management for the AAE rendering
// pipeline. See gl_fbo.h for the full layout description.
//
// Key design decisions:
//
//   - fbo1..fbo4 are allocated once at startup via fbo_init() and remain
//     valid for the entire application lifetime. They do NOT depend on a
//     game being loaded, so the GUI driver can use them safely.
//
//   - fbo_raster is game-specific: its dimensions come from the current
//     game's screen_width/height, so it is allocated separately via
//     fbo_init_raster() after a game is set up, and released by
//     fbo_shutdown_raster() before switching games.
//
//   - IMPORTANT: fbo1, fbo2, fbo3 use GL_RGB8 (no alpha channel).
//     The vector blur, glow, and trail/feedback code uses additive blending
//     modes (GL_ONE, GL_ONE) and accumulation passes that were designed
//     without an alpha channel. Adding alpha to these buffers changes how
//     every blend operation composites, breaking the glow and persistence
//     effects. Only fbo4 (final composite) and fbo_raster use GL_RGBA8
//     since those may need alpha for the blit-to-screen step.
//
//   - Call fbo_generate_mipmaps() after rendering into an FBO and before
//     sampling from its textures.
//
//==========================================================================

#include "sys_gl.h"
#include "gl_fbo.h"
#include "sys_log.h"
#include <algorithm>
#include <array>
#include <initializer_list>


// ---------------------------------------------------------------------------
// FBO and texture handle definitions
// ---------------------------------------------------------------------------
rfbo_t fbo_pyr[GLOW_PYR_LEVELS] = {};
rtex_t img_pyr[GLOW_PYR_LEVELS] = {};
rfbo_t fbo_persist = 0;          // [vectrex-port] phosphor persistence
rtex_t img_persist[2] = {};

rfbo_t fbo1       = 0;
rfbo_t fbo2       = 0;
rfbo_t fbo3       = 0;
rfbo_t fbo4       = 0;

rtex_t img1a = 0;
rtex_t img1b = 0;
rtex_t img1c = 0;
rtex_t img2a = 0;
rtex_t img2b = 0;
rtex_t img3a = 0;
rtex_t img3b = 0;
rtex_t img4a = 0;
rtex_t img4b = 0;


// Pipeline texture dimensions (fixed for the whole pipeline).
// FBO1/FBO4 : 1024x1024 - main render and final composite targets.
// FBO2      :  512x512  - first glow downsample.
// FBO3      :  256x256  - second glow downsample and blur pingpong.
const float width   = 1024.0f;
const float height  = 1024.0f;
const float width2  =  512.0f;
const float height2 =  512.0f;
const float width3  =  256.0f;
const float height3 =  256.0f;

// ---------------------------------------------------------------------------
// get_max_anisotropy (internal)
// Queries the maximum anisotropy level supported by the GPU. Result is
// cached after the first call since it never changes at runtime.
// ---------------------------------------------------------------------------
static float get_max_anisotropy()
{
    static float maxAniso = 0.0f;
    if (maxAniso == 0.0f)
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
    return maxAniso;
}

// ---------------------------------------------------------------------------
// CHECK_FRAMEBUFFER_STATUS
// Queries and logs the completeness status of the currently bound FBO.
// Returns the raw GL status enum so callers can branch on it if needed.
// ---------------------------------------------------------------------------
static GLenum CHECK_FRAMEBUFFER_STATUS()
{
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    switch (status)
    {
    case GL_FRAMEBUFFER_COMPLETE:
        LOG_INFO("FBO complete.");
        break;
    case GL_FRAMEBUFFER_UNSUPPORTED:
        LOG_ERROR("FBO error: GL_FRAMEBUFFER_UNSUPPORTED_EXT");
        break;
    case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
        LOG_ERROR("FBO error: GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT_EXT");
        break;
    case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
        LOG_ERROR("FBO error: GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT_EXT");
        break;
    case GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS_EXT:
        LOG_ERROR("FBO error: GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS_EXT");
        break;
    case GL_FRAMEBUFFER_INCOMPLETE_FORMATS_EXT:
        LOG_ERROR("FBO error: GL_FRAMEBUFFER_INCOMPLETE_FORMATS_EXT");
        break;
    case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER:
        LOG_ERROR("FBO error: GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER_EXT");
        break;
    case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER:
        LOG_ERROR("FBO error: GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER_EXT");
        break;
    default:
        LOG_ERROR("FBO error: unknown status 0x%x", status);
        break;
    }
    return status;
}

// ---------------------------------------------------------------------------
// create_texture (internal)
// Allocates a single texture at the given dimensions.
//
// use_alpha controls the pixel format:
//   false -> GL_RGB8  / GL_RGB  (3 channels, no alpha)
//   true  -> GL_RGBA8 / GL_RGBA (4 channels, with alpha)
//
// Use GL_RGB8 for any FBO whose contents are combined with additive blending
// (GL_ONE, GL_ONE) or alpha-accumulation passes. The vector pipeline buffers
// (fbo1, fbo2, fbo3) fall into this category. Adding an alpha channel there
// changes the result of every blend operation and breaks the glow/trail effects.
//
// Use GL_RGBA8 only where alpha is genuinely needed, e.g. fbo4 (the final
// composite that gets blitted to the screen) or fbo_raster.
//
// When mipmaps=true:
//   - Pre-allocates the full mip chain so the FBO attachment is immediately complete.
//   - Enables trilinear filtering (GL_LINEAR_MIPMAP_LINEAR) and anisotropic
//     filtering up to the GPU maximum.
//   - Generates a placeholder mip chain. Real mips must be regenerated each frame
//     after rendering by calling fbo_generate_mipmaps().
//
// When mipmaps=false:
//   - Allocates level 0 only with GL_LINEAR min/mag.
//   - Useful for intermediate buffers that are never minified.
// ---------------------------------------------------------------------------
static GLuint create_texture(float w, float h, bool mipmaps = true, bool use_alpha = false,
                             bool half_float = false)
{
    // Select the correct sized internal format and base format.
    // These two must always be compatible with each other.
    // [vectrex-port] half_float: RGBA16F for the phosphor persistence buffers.
    const GLenum internalFmt = half_float ? GL_RGBA16F : (use_alpha ? GL_RGBA8 : GL_RGB8);
    const GLenum baseFmt     = (use_alpha || half_float) ? GL_RGBA : GL_RGB;

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    if (mipmaps)
    {
        // Calculate and pre-allocate the full mip chain.
        int maxDim = (int)(w > h ? w : h);
        int levels = 1;
        while (maxDim > 1) { maxDim >>= 1; ++levels; }

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, levels - 1);

        // Trilinear filtering: blends smoothly between mip levels.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);

        // Anisotropic filtering reduces aliasing on non-axis-aligned lines.
        float aniso = get_max_anisotropy();
        if (aniso >= 2.0f)
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, aniso);
    }
    else
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL,  0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }

    // Allocate texture storage (no pixel data - just reserve GPU memory).
    glTexImage2D(GL_TEXTURE_2D, 0,
        internalFmt,
        (GLsizei)w, (GLsizei)h,
        0,
        baseFmt,
        GL_UNSIGNED_BYTE,
        nullptr);           // no initial data - storage only

    if (mipmaps)
    {
        // Generate placeholder mips now. Real mips are rebuilt each frame
        // by fbo_generate_mipmaps() after rendering.
        glGenerateMipmap(GL_TEXTURE_2D);
    }

    return tex;
}

// ---------------------------------------------------------------------------
// create_fbo (internal)
// Creates one FBO and attaches one or more textures to it.
//
// Each element of 'attachments' is a tuple of:
//   - A pointer to the GLuint handle to receive the new texture.
//   - An array of {width, height} for that attachment.
//   - A bool indicating whether this attachment needs an alpha channel.
//
// Attachments are assigned to GL_COLOR_ATTACHMENT0, _1_EXT, _2_EXT, ...
// in the order they appear in the list.
//
// The FBO completeness status is checked and logged after creation.
// ---------------------------------------------------------------------------
struct FboAttachment
{
    rtex_t*               texOut;
    std::array<float, 2>  dims;
    bool                  use_alpha;
    // Default true preserves the historical behavior. The glow pyramid passes
    // false: its levels are sampled at fixed 2:1 ratios where plain bilinear
    // IS the correct box prefilter, and a mip-free texture can never sample a
    // stale mip - the failure mode that forces per-frame glGenerateMipmap
    // everywhere else.
    bool                  mipmaps = true;
    bool                  half_float = false;   // [vectrex-port] RGBA16F
};

static void create_fbo(rfbo_t& fbo,
    std::initializer_list<FboAttachment> attachments)
{
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    int slot = 0;
    for (const auto& a : attachments)
    {
        *a.texOut = create_texture(a.dims[0], a.dims[1], a.mipmaps, a.use_alpha, a.half_float);

        // Attach mip level 0. The rest of the mip chain is regenerated
        // separately after rendering (see fbo_generate_mipmaps).
        glFramebufferTexture2D(GL_FRAMEBUFFER,
            GL_COLOR_ATTACHMENT0 + slot,
            GL_TEXTURE_2D, *a.texOut, 0);

        ++slot;
    }

    CHECK_FRAMEBUFFER_STATUS();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ---------------------------------------------------------------------------
// fbo_generate_mipmaps
// Regenerates the full mip chain for each listed texture. Call this after
// rendering into an FBO and before sampling from those textures so that
// trilinear and anisotropic filtering work correctly.
// ---------------------------------------------------------------------------
void fbo_generate_mipmaps(std::initializer_list<rtex_t> textures)
{
    for (rtex_t tex : textures)
    {
        glBindTexture(GL_TEXTURE_2D, tex);
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

// ---------------------------------------------------------------------------
// fbo_init
// Allocates fbo1..fbo4 and all their associated textures. This is the main
// pipeline setup and should be called once at startup inside init_gl(),
// before any game or GUI rendering begins.
//
// Format notes:
//   fbo1 / fbo2 / fbo3  ->  GL_RGB8 (no alpha)
//     The vector pipeline uses additive blending and alpha-accumulation passes
//     designed around RGB-only buffers. An alpha channel in these FBOs changes
//     the result of every blend operation and breaks glow / trail / blur effects.
//
//   fbo4                ->  GL_RGBA8 (with alpha)
//     The final composited frame is blitted to the backbuffer; alpha may be
//     needed depending on the blit path.
//
// Does NOT allocate fbo_raster - call fbo_init_raster() after a game loads.
// ---------------------------------------------------------------------------
void fbo_init()
{
    LOG_INFO("fbo_init: allocating fbo1..fbo4");

    // FBO1 - main render target: current frame + feedback trail buffers.
    // RGB only - these are the core vector accumulation buffers.
    create_fbo(fbo1, {
        { &img1a, { width,  height  }, false },     // attachment 0: current frame
        { &img1b, { width,  height  }, false },     // attachment 1: trail/feedback
        { &img1c, { width,  height  }, false }      // attachment 2: secondary feedback
    });

    // FBO2 - first glow downsample (1024 -> 512).
    // RGB only - intermediate blur buffer, additive blending only.
    create_fbo(fbo2, {
        { &img2a, { width2, height2 }, false },     // attachment 0: 512x512 downsample
        { &img2b, { width2, height2 }, false }      // attachment 1: spare
    });

    // FBO3 - second glow downsample + pingpong blur (512 -> 256).
    // RGB only - pingpong blur targets, same reasoning as fbo2.
    create_fbo(fbo3, {
        { &img3a, { width3, height3 }, false },     // attachment 0: blur pingpong A
        { &img3b, { width3, height3 }, false }      // attachment 1: blur pingpong B
    });

    // FBO4 - final compositing target, blitted to the backbuffer.
    // RGBA - this is the output buffer; alpha may be needed for the blit step.
    create_fbo(fbo4, {
        { &img4a, { width,  height  }, true },      // attachment 0: composited frame
		{ &img4b, { width,  height  }, true }       // attachment 1: crt scratch area for overlay rendering (pre-backdrop)
    });

    // Glow pyramid - the dual-filter blur chain ([main] glow_filter=1).
    // 256 (img3a) -> 128 -> 64 -> 32 -> 64 -> 128 -> 256 (img3b). Five tiny
    // single-attachment FBOs, ~160 KB of RGB8 total, always allocated so the
    // ini toggle needs no re-init. mipmaps=false throughout: every sampling
    // step is an exact 2:1 or 1:2 ratio where bilinear alone is correct, and
    // it is precisely what lets this path skip glGenerateMipmap per frame.
    static constexpr int kPyrSize[GLOW_PYR_LEVELS] = { 128, 64, 32, 64, 128 };
    for (int i = 0; i < GLOW_PYR_LEVELS; ++i)
    {
        create_fbo(fbo_pyr[i], {
            { &img_pyr[i], { (float)kPyrSize[i], (float)kPyrSize[i] }, false, false }
        });
    }

    // [vectrex-port] Phosphor persistence ping-pong (replaces the img1c trail):
    // two 1024x1024 RGBA16F attachments, no mips. Float so the per-frame decay
    // multiply fades smoothly to black instead of sticking at low 8-bit values.
    create_fbo(fbo_persist, {
        { &img_persist[0], { width, height }, false, false, true },
        { &img_persist[1], { width, height }, false, false, true }
    });

    LOG_INFO("fbo_init: done.");
}

// [vectrex-port] removed: fbo_init_raster(), fbo_resize_mono() (raster and
// mono/colour CRT monitor targets; the Vectrex is a B/W vector display).

// ---------------------------------------------------------------------------
// fbo_shutdown
// Releases all handles allocated by fbo_init(). Call once on application
// exit or when tearing down the GL context.
// ---------------------------------------------------------------------------
void fbo_shutdown()
{
    LOG_INFO("fbo_shutdown: releasing fbo1..fbo4 and all textures.");

    rtex_t textures[] = { img1a, img1b, img1c, img2a, img2b, img3a, img3b, img4a, img4b };
    glDeleteTextures(9, textures);

    img1a = img1b = img1c = 0;
    img2a = img2b = 0;
    img3a = img3b = 0;
    img4a = 0;
    img4b = 0;

    rfbo_t fbos[] = { fbo1, fbo2, fbo3, fbo4 };
    glDeleteFramebuffers(4, fbos);

    fbo1 = fbo2 = fbo3 = fbo4 = 0;

    // Glow pyramid teardown.
    glDeleteTextures(GLOW_PYR_LEVELS, img_pyr);
    glDeleteFramebuffers(GLOW_PYR_LEVELS, fbo_pyr);
    for (int i = 0; i < GLOW_PYR_LEVELS; ++i) { img_pyr[i] = 0; fbo_pyr[i] = 0; }

    // [vectrex-port] Phosphor persistence teardown.
    glDeleteTextures(2, img_persist);
    glDeleteFramebuffers(1, &fbo_persist);
    img_persist[0] = img_persist[1] = 0;
    fbo_persist = 0;
}


// [vectrex-port] removed: fbo_shutdown_raster() (no raster targets).
