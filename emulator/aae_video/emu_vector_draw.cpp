#define NOMINMAX
#include "emu_vector_draw.h"
#include "colordefs.h"
#include "sys_log.h"
#include "opengl_renderer.h"
#include "gl_texturing.h" // For game_tex[0]
#include <vector>

#pragma warning( disable :  4244 )

template<typename T>
inline T clip(T val, T minval, T maxval) {
    return std::min(std::max(val, minval), maxval);
}

float xoffset;
float yoffset;
GLuint* tex;

// Scales only the textured shot alpha; 1.0 = current, lower = dimmer
static float g_texShotAlphaScale = 1.0f; // trying 0.50-0.75
void set_tex_shot_alpha_scale(float s) { g_texShotAlphaScale = (s < 0.f) ? 0.f : (s > 1.f ? 1.f : s); }

colors vec_colors[256];

std::vector<fpoint> linelist;
std::vector<txdata> texlist;
std::vector<fpoint> dotlist;   // [vectrex-port] single dots (zero-length vectors)

#include <algorithm> // for std::sort

// -----------------------------------------------------------------------------
// sort_lines_by_color_desc
// Sorts linelist by raw rgb_t value (interpreted as uint32_t), highest first.
// Each line is two consecutive fpoints in linelist; endpoint order preserved.
//
// Notes:
// - If you want to ignore alpha when sorting, set MASK = 0x00FFFFFF.
// - Uses std::sort (not stable) for speed.
// -----------------------------------------------------------------------------

void sort_lines_by_color()
{
    const size_t n = linelist.size();
    if (n < 2) return;

    struct LineKey { size_t i0; uint32_t key; };
    constexpr uint32_t MASK = 0xFFFFFFFFu; // use 0x00FFFFFFu to ignore alpha

    std::vector<LineKey> keys;
    keys.reserve(n / 2);

    for (size_t i = 0; i + 1 < n; i += 2) {
        uint32_t c = static_cast<uint32_t>(linelist[i].color);
        keys.push_back({ i, c & MASK });
    }

    // Ascending: darker (smaller key) first
    std::sort(keys.begin(), keys.end(),
        [](const LineKey& a, const LineKey& b) { return a.key < b.key; });

    std::vector<fpoint> out;
    out.reserve((keys.size() * 2) + (n & 1));

    for (const auto& k : keys) {
        out.push_back(linelist[k.i0 + 0]);
        out.push_back(linelist[k.i0 + 1]);
    }
    if (n & 1) out.push_back(linelist.back()); // keep any stray vertex

    linelist.swap(out);
}
/*
void sort_lines_by_color()
{
    const size_t n = linelist.size();
    if (n < 2) return;

    const size_t m = n >> 1; // number of line pairs

    struct LineKey { size_t i0; uint32_t key; };

    // Reused scratch buffers (safe per-thread; change to static if single-threaded)
    static thread_local std::vector<LineKey> keys;
    static thread_local std::vector<LineKey> tmp;   // optional (radix variant below)
    static thread_local std::vector<fpoint>  out;

    keys.resize(m);
    out.resize(n);

    // If you want to ignore alpha, flip MASK to 0x00FFFFFFu.
    constexpr uint32_t MASK = 0xFFFFFFFFu;

    // Build keys
    for (size_t j = 0, i = 0; j < m; ++j, i += 2) {
        uint32_t c = static_cast<uint32_t>(linelist[i].color);
        keys[j] = { i, c & MASK };
    }

    // Ascending: darker first
    std::sort(keys.begin(), keys.end(),
        [](const LineKey& a, const LineKey& b) { return a.key < b.key; });

    // Repack in new order
    for (size_t j = 0; j < m; ++j) {
        const size_t dst = (j << 1);
        const size_t src = keys[j].i0;
        out[dst + 0] = linelist[src + 0];
        out[dst + 1] = linelist[src + 1];
    }
    if (n & 1) out[n - 1] = linelist[n - 1]; // preserve stray vertex if present

    linelist.swap(out);
}
*/
void set_texture_id(GLuint* id)
{
    tex = id;
}

void set_blendmode(GLenum sfactor, GLenum dfactor)
{
    glBlendFunc(sfactor, dfactor);
}

rgb_t modulate_color(rgb_t col, int intensity, int gain)
{
    if ((col & 0x00FFFFFF) == 0) { return 0; }

    uint8_t r = (col >> 0) & 0xFF;
    uint8_t g = (col >> 8) & 0xFF;
    uint8_t b = (col >> 16) & 0xFF;
    uint8_t a = 0xff;// (col >> 24) & 0xFF;

    r = clip((r & intensity) + gain, 0, 255);
    g = clip((g & intensity) + gain, 0, 255);
    b = clip((b & intensity) + gain, 0, 255);

    return  (a << 24) | (b << 16) | (g << 8) | r;
}

/*
rgb_t cache_tex_color(int intensity, rgb_t col)
{
    rgb_t result = modulate_color(col, intensity, config.gain);
    return (result & 0x00FFFFFF) | (intensity << 24);
}
*/
rgb_t cache_tex_color(int intensity, rgb_t col)
{
    rgb_t result = modulate_color(col, intensity, config.gain);

    // Scale only the alpha used for textured shots
    int a = (int)(intensity * g_texShotAlphaScale + 0.5f);
    if (a < 0) a = 0; else if (a > 255) a = 255;

    return (result & 0x00FFFFFF) | (a << 24);
}

void cache_texpoint(float ex, float ey, float tx, float ty, int intensity, rgb_t col)
{
    texlist.emplace_back(ex - xoffset, ey - yoffset, tx, ty, cache_tex_color(intensity, col));
}

void add_line(float sx, float sy, float ex, float ey, int intensity, rgb_t col)
{
    rgb_t temp_col = modulate_color(col, intensity, config.gain);
    rgb_t temp_half_col = 0;

    if (Machine->drv->video_attributes & VECTOR_USES_COLOR)
    {
        temp_half_col = modulate_color(col, intensity, config.gain / 2);
        temp_half_col = (temp_half_col & 0x00FFFFFF) | (0x7F << 24);
    }

    linelist.emplace_back(sx, sy, temp_col, temp_half_col);
    linelist.emplace_back(ex, ey, temp_col, temp_half_col);
}

void add_tex(float ex, float ey, int intensity, rgb_t col)
{
    float xoff = config.fire_point_size;
    float yoff = config.fire_point_size;

    float x0 = ex - xoff;
    float y0 = ey - yoff;
    float x1 = ex + xoff;
    float y1 = ey + yoff;

    // First triangle
    cache_texpoint(x0, y0, 0.0f, 0.0f, intensity, col);
    cache_texpoint(x1, y0, 1.0f, 0.0f, intensity, col);
    cache_texpoint(x1, y1, 1.0f, 1.0f, intensity, col);

    // Second triangle
    cache_texpoint(x1, y1, 1.0f, 1.0f, intensity, col);
    cache_texpoint(x0, y1, 0.0f, 1.0f, intensity, col);
    cache_texpoint(x0, y0, 0.0f, 0.0f, intensity, col);
}

void cache_clear()
{
    texlist.clear();
    linelist.clear();
    dotlist.clear();
}

// [vectrex-port] Add a single dot (zero-length vector) to its own buffer.
void add_dot(float x, float y, int intensity, rgb_t col)
{
    rgb_t c = modulate_color(col, intensity, config.gain);
    dotlist.emplace_back(x, y, c, (rgb_t)0);
}

// [vectrex-port] Draw the dot buffer as GL_POINTS at the given size (untextured),
// using the same array path as draw_all()'s point pass. Independent of the line
// endpoint size (config.pointsize) used by draw_all().
void draw_dots(float point_size)
{
    if (dotlist.empty()) return;

    glDisable(GL_TEXTURE_2D);
    glPointSize(point_size);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(fpoint), &dotlist[0].color);
    glVertexPointer(2, GL_FLOAT, sizeof(fpoint), &dotlist[0].x);
    glDrawArrays(GL_POINTS, 0, (GLsizei)dotlist.size());
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
}

void draw_all()
{
    if (Machine->gamedrv->video_attributes & VECTOR_USES_COLOR)
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    else
    {
        // This fixes Battlezone and Red Baron, but costs a lot of cpu time
        sort_lines_by_color();
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    glDisable(GL_TEXTURE_2D);

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(fpoint), &linelist[0].color);
    glVertexPointer(2, GL_FLOAT, sizeof(fpoint), &linelist[0].x);
    glDrawArrays(GL_LINES, 0, (GLsizei)linelist.size());

    if (Machine->drv->video_attributes & VECTOR_USES_COLOR)
    {
        glColorPointer(3, GL_UNSIGNED_BYTE, sizeof(fpoint), &linelist[0].colorshalf);
    }
    glDrawArrays(GL_POINTS, 0, (GLsizei)linelist.size());

    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);

    if (!texlist.empty())
    {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, *tex);
       // glBlendFunc(GL_ONE, GL_ONE);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
       //glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE);

        glEnableClientState(GL_VERTEX_ARRAY);
        glVertexPointer(2, GL_FLOAT, sizeof(txdata), &texlist[0].x);

        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glTexCoordPointer(2, GL_FLOAT, sizeof(txdata), &texlist[0].tx);

        glEnableClientState(GL_COLOR_ARRAY);
        glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(txdata), &texlist[0].color);

        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)texlist.size());

        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glDisableClientState(GL_VERTEX_ARRAY);
        glDisableClientState(GL_COLOR_ARRAY);

        glDisable(GL_TEXTURE_2D);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
}
