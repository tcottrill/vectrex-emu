//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2025-2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
// -----------------------------------------------------------------------------
// vector_draw.cpp - Modern shader-based vector beam renderer.
// Butt-capped, coverage-AA'd instanced beams + round joins + procedural shots.
// Draws into the currently bound FBO (fbo1/img1a) using an explicit uProj; no
// fixed-function state. See docs/superpowers/specs/2026-06-22-vector-beam-renderer-design.md
// -----------------------------------------------------------------------------
#include "aae_mame_driver.h"             // Machine, VECTOR_USES_COLOR
#include "vector_draw.h"
#include "sys_gl.h"
#include "shader_util.h"                 // CompileShader / LinkShaderProgram
#include "config.h"                      // config.linewidth / gain / fire_point_size
#include "vector_draw_gl.h"              // modulate_color
#include "sys_log.h"                     // LOG_INFO
#include <vector>
#include <algorithm>
#include <cstdint>
#include <unordered_map>

using namespace aae::math;

// End-cap scale for TRUE line terminations (vertices touched by a single segment,
// e.g. the tips of an "I" or the ends of a "T" crossbar): round cap radius as a
// multiple of the beam half-width. ~1.0 matches the legacy GL_POINTS tip length.
static float g_endcap = 1.0f;

static std::vector<BeamLine> g_lines;
static std::vector<BeamJoin> g_joins;
static std::vector<BeamShot> g_shots;

static GLuint vaoLine = 0, vboLine = 0;
static GLuint vaoJoin = 0, vboJoin = 0;
static GLuint vaoShot = 0, vboShot = 0;

static GLuint progLine = 0, progJoin = 0, progShot = 0;

static int   g_ssaa = 1;
static float g_uAA  = 1.0f;               // feather in logical units (= config.line_smoothing / ssaa)

// Connectivity for round joins: a join belongs at any vertex where two or more
// VISIBLE segment endpoints coincide. This covers closed loops, T-junctions, fans
// and chains regardless of the order the generator emits segments. Built each
// frame from a vertex accumulator keyed by quantized position.
struct VertAccum { int count; rgb_t color; float half; vec2 pos; };
static std::unordered_map<int64_t, VertAccum> g_verts;

static inline int beam_brightness(rgb_t c) {
    int r = c & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF;
    return (r > g) ? (r > b ? r : b) : (g > b ? g : b);  // max channel
}

static inline int64_t beam_vkey(float x, float y) {
    // Quantize to 0.1 logical units so coincident endpoints merge despite jitter.
    int64_t qx = (int64_t)(x * 10.0f + 0.5f);
    int64_t qy = (int64_t)(y * 10.0f + 0.5f);
    return (qx << 32) ^ (qy & 0xffffffffLL);
}

static inline void beam_record_vert(int64_t k, vec2 p, rgb_t c, float half) {
    VertAccum& v = g_verts[k];
    if (v.count == 0) { v.pos = p; v.color = c; v.half = half; }
    else {
        if (beam_brightness(c) > beam_brightness(v.color)) v.color = c;
        if (half > v.half) v.half = half;
    }
    v.count++;
}

// ----------------------------- shaders --------------------------------------
static const char* vsLine = R"GLSL(
#version 330 core
layout(location=0) in vec2  inP0;
layout(location=1) in vec2  inP1;
layout(location=2) in float inHalf;
layout(location=3) in vec4  inColor;
uniform mat4  uProj;
uniform float uAA;
out vec2  vLocal;   // x = longitudinal [0..len], y = perpendicular
out float vLen;
out float vHalf;
out vec4  vColor;
const vec2 kQuad[4] = vec2[](vec2(0,-1), vec2(1,-1), vec2(0,1), vec2(1,1));
void main() {
    vec2  d   = inP1 - inP0;
    float len = length(d);
    vec2  dir = (len > 0.0001) ? d/len : vec2(1.0,0.0);
    vec2  nrm = vec2(-dir.y, dir.x);
    vec2  q   = kQuad[gl_VertexID];
    float along = q.x * (len + 2.0*uAA) - uAA;       // butt cap + feather past ends
    float perp  = q.y * (inHalf + uAA);
    vec2  pos   = inP0 + dir*along + nrm*perp;
    gl_Position = uProj * vec4(pos, 0.0, 1.0);
    vLocal = vec2(along, perp);
    vLen = len; vHalf = inHalf; vColor = inColor;
}
)GLSL";

static const char* fsLine = R"GLSL(
#version 330 core
in vec2  vLocal;
in float vLen;
in float vHalf;
in vec4  vColor;
uniform float uAA;
out vec4 frag;
void main() {
    // Half-coverage exactly at the geometric edge -> width matches GL_LINES.
    float covPerp = clamp((vHalf - abs(vLocal.y))/uAA + 0.5, 0.0, 1.0);
    float covEnd0 = clamp((vLocal.x)/uAA + 0.5, 0.0, 1.0);
    float covEnd1 = clamp((vLen - vLocal.x)/uAA + 0.5, 0.0, 1.0);
    float cov = covPerp * covEnd0 * covEnd1;
    if (cov <= 0.0) discard;
    frag = vec4(vColor.rgb, vColor.a * cov);   // no pow(): linear coverage
}
)GLSL";

// Round join: a disc centred on the shared vertex. uStrength scales its radius
// relative to the beam half-width (corner emphasis); always round, never squared.
static const char* vsJoin = R"GLSL(
#version 330 core
layout(location=0) in vec2  inCenter;
layout(location=1) in float inHalf;
layout(location=2) in vec4  inColor;
uniform mat4  uProj;
uniform float uAA;
uniform float uStrength;
out vec2  vLocal;
out float vRad;
out vec4  vColor;
const vec2 kQuad[4] = vec2[](vec2(-1,-1), vec2(1,-1), vec2(-1,1), vec2(1,1));
void main() {
    float r = inHalf * uStrength;
    vec2 q = kQuad[gl_VertexID];
    vec2 ext = q * (r + uAA);
    gl_Position = uProj * vec4(inCenter + ext, 0.0, 1.0);
    vLocal = ext; vRad = r; vColor = inColor;
}
)GLSL";

static const char* fsJoin = R"GLSL(
#version 330 core
in vec2  vLocal;
in float vRad;
in vec4  vColor;
uniform float uAA;
uniform float uPremult;   // 1 = premultiplied output for the GL_MAX (additive/color) path
out vec4 frag;
void main() {
    float cov = clamp((vRad - length(vLocal))/uAA + 0.5, 0.0, 1.0);
    if (cov <= 0.0) discard;
    if (uPremult > 0.5)
        frag = vec4(vColor.rgb * cov, cov);        // GL_MAX: fills gaps, never sums over lines
    else
        frag = vec4(vColor.rgb, vColor.a * cov);   // straight alpha-over (B/W)
}
)GLSL";

static const char* vsShot = R"GLSL(
#version 330 core
layout(location=0) in vec2  inCenter;
layout(location=1) in float inSize;
layout(location=2) in vec4  inColor;
uniform mat4 uProj;
out vec2 vUV;
out vec4 vColor;
const vec2 kQuad[4] = vec2[](vec2(-1,-1), vec2(1,-1), vec2(-1,1), vec2(1,1));
void main() {
    vec2 q = kQuad[gl_VertexID];
    gl_Position = uProj * vec4(inCenter + q*inSize, 0.0, 1.0);
    vUV = q*0.5 + 0.5;
    vColor = inColor;
}
)GLSL";

static const char* fsShot = R"GLSL(
#version 330 core
in vec2 vUV;
in vec4 vColor;
out vec4 frag;
uniform float uCorePower;
uniform float uBloomPower;
uniform float uBloomIntensity;
uniform float uOverdrive;
void main() {
    float d = distance(vUV, vec2(0.5));
    float g = clamp(1.0 - d*2.0, 0.0, 1.0);
    float profile = pow(g, uCorePower) + pow(g, uBloomPower) * uBloomIntensity;
    float z = vColor.a;   // clean linear intensity (no gain floor)
    // GL_SRC_ALPHA, GL_ONE: the alpha re-multiplies profile, squaring it -> a sharp
    // core (not a fuzzy ball); the z factor dims the shot linearly toward nothing.
    frag = vec4(vColor.rgb * uOverdrive * profile, profile * z);
}
)GLSL";

// ----------------------------- helpers --------------------------------------
static GLuint linkProg(const char* vs, const char* fs, const char* label) {
    return LinkShaderProgram(CompileShader(GL_VERTEX_SHADER,   vs, label),
                             CompileShader(GL_FRAGMENT_SHADER, fs, label));
}

// ----------------------------- lifecycle ------------------------------------
void beam_set_ssaa(int ssaa) { g_ssaa = (ssaa < 1) ? 1 : ssaa; }

// Lazily create the line + join programs and their instanced VAOs. Idempotent
// (guards on progLine) and safe to call from any path that needs the shared
// coverage-AA line shader -- the beam itself OR the vector fonts -- regardless of
// whether beam_init() ran (it only runs for vector games; fonts draw everywhere).
static void beam_ensure_lines() {
    if (progLine) return;

    progLine = linkProg(vsLine, fsLine, "beam_line");
    progJoin = linkProg(vsJoin, fsJoin, "beam_join");

    // Lines: one instance per segment.
    glGenVertexArrays(1, &vaoLine);
    glGenBuffers(1, &vboLine);
    glBindVertexArray(vaoLine);
    glBindBuffer(GL_ARRAY_BUFFER, vboLine);
    glVertexAttribPointer(0, 2, GL_FLOAT,         GL_FALSE, sizeof(BeamLine), (void*)offsetof(BeamLine, p0));    glEnableVertexAttribArray(0); glVertexAttribDivisor(0,1);
    glVertexAttribPointer(1, 2, GL_FLOAT,         GL_FALSE, sizeof(BeamLine), (void*)offsetof(BeamLine, p1));    glEnableVertexAttribArray(1); glVertexAttribDivisor(1,1);
    glVertexAttribPointer(2, 1, GL_FLOAT,         GL_FALSE, sizeof(BeamLine), (void*)offsetof(BeamLine, half));  glEnableVertexAttribArray(2); glVertexAttribDivisor(2,1);
    glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE,  sizeof(BeamLine), (void*)offsetof(BeamLine, color)); glEnableVertexAttribArray(3); glVertexAttribDivisor(3,1);

    // Joins.
    glGenVertexArrays(1, &vaoJoin);
    glGenBuffers(1, &vboJoin);
    glBindVertexArray(vaoJoin);
    glBindBuffer(GL_ARRAY_BUFFER, vboJoin);
    glVertexAttribPointer(0, 2, GL_FLOAT,         GL_FALSE, sizeof(BeamJoin), (void*)offsetof(BeamJoin, center)); glEnableVertexAttribArray(0); glVertexAttribDivisor(0,1);
    glVertexAttribPointer(1, 1, GL_FLOAT,         GL_FALSE, sizeof(BeamJoin), (void*)offsetof(BeamJoin, half));   glEnableVertexAttribArray(1); glVertexAttribDivisor(1,1);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE,  sizeof(BeamJoin), (void*)offsetof(BeamJoin, color));  glEnableVertexAttribArray(2); glVertexAttribDivisor(2,1);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void beam_init(int ssaa) {
    g_ssaa = (ssaa < 1) ? 1 : ssaa;

    beam_ensure_lines();   // line + join programs/VAOs (shared with the fonts)

    if (progShot) return;  // shots already built (idempotent re-init)
    progShot = linkProg(vsShot, fsShot, "beam_shot");

    // Shots.
    glGenVertexArrays(1, &vaoShot);
    glGenBuffers(1, &vboShot);
    glBindVertexArray(vaoShot);
    glBindBuffer(GL_ARRAY_BUFFER, vboShot);
    glVertexAttribPointer(0, 2, GL_FLOAT,         GL_FALSE, sizeof(BeamShot), (void*)offsetof(BeamShot, pos));   glEnableVertexAttribArray(0); glVertexAttribDivisor(0,1);
    glVertexAttribPointer(1, 1, GL_FLOAT,         GL_FALSE, sizeof(BeamShot), (void*)offsetof(BeamShot, size));  glEnableVertexAttribArray(1); glVertexAttribDivisor(1,1);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE,  sizeof(BeamShot), (void*)offsetof(BeamShot, color)); glEnableVertexAttribArray(2); glVertexAttribDivisor(2,1);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void beam_shutdown() {
    GLuint vaos[] = { vaoLine, vaoJoin, vaoShot };
    GLuint vbos[] = { vboLine, vboJoin, vboShot };
    glDeleteVertexArrays(3, vaos);
    glDeleteBuffers(3, vbos);
    glDeleteProgram(progLine);
    glDeleteProgram(progJoin);
    glDeleteProgram(progShot);
    vaoLine = vaoJoin = vaoShot = vboLine = vboJoin = vboShot = 0;
    progLine = progJoin = progShot = 0;
}

// ----------------------------- producer -------------------------------------
void beam_add_line(float sx, float sy, float ex, float ey, int intensity, rgb_t col,
                   float halfOverride) {
    rgb_t c = modulate_color(col, intensity, config.gain);

    // Invisible (pen-up move): draw nothing, contribute no vertex.
    if ((c & 0x00FFFFFFu) == 0) return;

    vec2 p0(sx, sy), p1(ex, ey);
    // See the header: negative means "use the game beam width".
    float half = (halfOverride >= 0.0f) ? halfOverride : (config.linewidth * 0.5f);
    g_lines.push_back({ p0, p1, half, c });

    // Record both endpoints; a vertex shared by 2+ segments becomes a join.
    int64_t k0 = beam_vkey(sx, sy), k1 = beam_vkey(ex, ey);
    beam_record_vert(k0, p0, c, half);
    if (k1 != k0)
        beam_record_vert(k1, p1, c, half);
}

void beam_add_shot(float ex, float ey, int intensity, rgb_t col) {
    // Shots bake intensity into BOTH the colour and z, and modulate_color()'s
    // line-gain (config.gain) lifts every shot toward max, flattening the z range.
    // Separate hue from brightness: normalize the colour to a unit hue and pass z
    // straight through (in alpha) so brightness scales linearly with intensity.
    int r = col & 0xFF, g = (col >> 8) & 0xFF, b = (col >> 16) & 0xFF;
    int mx = (r > g) ? (r > b ? r : b) : (g > b ? g : b);
    int z  = intensity; if (z < 0) z = 0; else if (z > 255) z = 255;
    if (mx <= 0 || z <= 0) return;
    r = (r * 255) / mx; g = (g * 255) / mx; b = (b * 255) / mx;
    rgb_t c = ((rgb_t)z << 24) | ((rgb_t)b << 16) | ((rgb_t)g << 8) | (rgb_t)r;
    g_shots.push_back({ vec2(ex, ey), (float)config.fire_point_size, c });
}

void beam_clear() {
    g_lines.clear();
    g_joins.clear();
    g_shots.clear();
    g_verts.clear();
}

// ---- Retained-batch stash (see vector_draw.h for the ownership rationale) ---
// Swap-based, so the game's batches come back byte-identical and neither call
// allocates after the first frame. Only the Vulkan chain's RecordUiOverlays
// uses these; under GL the fonts never touch this queue.
static std::vector<BeamLine>                  s_stashLines;
static std::vector<BeamJoin>                  s_stashJoins;
static std::vector<BeamShot>                  s_stashShots;
static std::unordered_map<int64_t, VertAccum> s_stashVerts;

void beam_stash_push() {
    s_stashLines.swap(g_lines);  g_lines.clear();
    s_stashJoins.swap(g_joins);  g_joins.clear();
    s_stashShots.swap(g_shots);  g_shots.clear();
    s_stashVerts.swap(g_verts);  g_verts.clear();
}

void beam_stash_pop() {
    g_lines.clear();  g_lines.swap(s_stashLines);
    g_joins.clear();  g_joins.swap(s_stashJoins);
    g_shots.clear();  g_shots.swap(s_stashShots);
    g_verts.clear();  g_verts.swap(s_stashVerts);
}

// Backend-agnostic batch access (see vector_draw.h): read-only views of the
// CPU-side beam batches for the Vulkan backend. No GL, no behavior change.
const std::vector<BeamLine>& beam_get_lines() { return g_lines; }
const std::vector<BeamShot>& beam_get_shots() { return g_shots; }

// ----------------------------- draw -----------------------------------------
void beam_draw_all(const mat4& proj) {
    // AA feather (logical units) from the configured smoothing, scaled by SSAA.
    g_uAA = config.line_smoothing / (float)((g_ssaa < 1) ? 1 : g_ssaa);

    // Build join/cap discs from the vertex accumulator: a vertex shared by 2+
    // segments is a corner (config.corner_strength); a single-segment vertex is a
    // true line termination and gets a round end-cap (g_endcap). Radius baked here
    // so config changes take effect each frame.
    g_joins.clear();
    for (const auto& kv : g_verts) {
        const VertAccum& v = kv.second;
        float r = v.half * ((v.count >= 2) ? config.corner_strength : g_endcap);
        if (r > 0.01f)
            g_joins.push_back({ v.pos, r, v.color });
    }

    if (g_lines.empty() && g_joins.empty() && g_shots.empty()) return;

    const bool additive = (Machine->drv->video_attributes & VECTOR_USES_COLOR) != 0;

    glEnable(GL_BLEND);

    if (!additive) {
        // Black-and-white: painter's sort, darkest first, so brighter beams
        // occlude darker ones under alpha-over.
        std::sort(g_lines.begin(), g_lines.end(),
            [](const BeamLine& a, const BeamLine& b) {
                return (uint32_t)a.color < (uint32_t)b.color; });
        std::sort(g_joins.begin(), g_joins.end(),
            [](const BeamJoin& a, const BeamJoin& b) {
                return (uint32_t)a.color < (uint32_t)b.color; });
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);   // color: additive
    }

    // Lines.
    if (!g_lines.empty()) {
        glUseProgram(progLine);
        glUniformMatrix4fv(glGetUniformLocation(progLine, "uProj"), 1, GL_FALSE, value_ptr(const_cast<mat4&>(proj)));
        glUniform1f(glGetUniformLocation(progLine, "uAA"), g_uAA);
        glBindBuffer(GL_ARRAY_BUFFER, vboLine);
        glBufferData(GL_ARRAY_BUFFER, g_lines.size()*sizeof(BeamLine), g_lines.data(), GL_STREAM_DRAW);
        glBindVertexArray(vaoLine);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)g_lines.size());
    }

    // Corner joins + end-caps. Under additive (color) we draw them with GL_MAX and
    // premultiplied coverage so they FILL gaps at the line's own brightness but
    // never sum on top of the lines (which otherwise makes endpoints/corners
    // brighter than the beam). Under B/W they use the same sorted alpha-over as the
    // lines (where same-colour overlap already does not double).
    if (!g_joins.empty()) {
        glUseProgram(progJoin);
        glUniformMatrix4fv(glGetUniformLocation(progJoin, "uProj"), 1, GL_FALSE, value_ptr(const_cast<mat4&>(proj)));
        glUniform1f(glGetUniformLocation(progJoin, "uAA"), g_uAA);
        glUniform1f(glGetUniformLocation(progJoin, "uStrength"), 1.0f);   // radius already baked per-disc
        glUniform1f(glGetUniformLocation(progJoin, "uPremult"), additive ? 1.0f : 0.0f);
        if (additive) glBlendEquation(GL_MAX);
        glBindBuffer(GL_ARRAY_BUFFER, vboJoin);
        glBufferData(GL_ARRAY_BUFFER, g_joins.size()*sizeof(BeamJoin), g_joins.data(), GL_STREAM_DRAW);
        glBindVertexArray(vaoJoin);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)g_joins.size());
        if (additive) glBlendEquation(GL_FUNC_ADD);   // restore default for the rest of the pipeline
    }

    // Shots: procedural radial core + halo, always additive.
    if (!g_shots.empty()) {
        glUseProgram(progShot);
        glUniformMatrix4fv(glGetUniformLocation(progShot, "uProj"), 1, GL_FALSE, value_ptr(const_cast<mat4&>(proj)));
        glUniform1f(glGetUniformLocation(progShot, "uCorePower"),      6.0f);
        glUniform1f(glGetUniformLocation(progShot, "uBloomPower"),     2.5f);
        glUniform1f(glGetUniformLocation(progShot, "uBloomIntensity"), 0.3f);
        glUniform1f(glGetUniformLocation(progShot, "uOverdrive"),      1.5f);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glBindBuffer(GL_ARRAY_BUFFER, vboShot);
        glBufferData(GL_ARRAY_BUFFER, g_shots.size()*sizeof(BeamShot), g_shots.data(), GL_STREAM_DRAW);
        glBindVertexArray(vaoShot);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)g_shots.size());
    }

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
}

// ----------------------- shared AA-line path (fonts) ------------------------
void beam_draw_lines(const mat4& proj, const BeamLine* lines, int count,
                     float aaFeather, bool additive) {
    if (count <= 0 || !lines) return;
    beam_ensure_lines();

    glEnable(GL_BLEND);
    // Separate alpha (src factor GL_ONE) so the accumulated alpha is correct when
    // these are rendered into an offscreen RGBA target that is later alpha-blitted
    // (the rotated raster overlay). RGB blending is unchanged, so every other target
    // (vector fbo1 is RGB8; the backbuffer's alpha is unused) looks identical.
    if (additive) glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    else          glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(progLine);
    glUniformMatrix4fv(glGetUniformLocation(progLine, "uProj"), 1, GL_FALSE, value_ptr(const_cast<mat4&>(proj)));
    glUniform1f(glGetUniformLocation(progLine, "uAA"), (aaFeather > 0.0001f) ? aaFeather : 0.0001f);

    glBindBuffer(GL_ARRAY_BUFFER, vboLine);
    glBufferData(GL_ARRAY_BUFFER, count * sizeof(BeamLine), lines, GL_STREAM_DRAW);
    glBindVertexArray(vaoLine);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)count);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
}

void beam_draw_caps(const mat4& proj, const BeamJoin* caps, int count,
                    float aaFeather, bool additive) {
    if (count <= 0 || !caps) return;
    beam_ensure_lines();

    const float aa = (aaFeather > 0.0001f) ? aaFeather : 0.0001f;

    glEnable(GL_BLEND);

    glUseProgram(progJoin);
    glUniformMatrix4fv(glGetUniformLocation(progJoin, "uProj"), 1, GL_FALSE, value_ptr(const_cast<mat4&>(proj)));
    glUniform1f(glGetUniformLocation(progJoin, "uAA"), aa);
    glUniform1f(glGetUniformLocation(progJoin, "uStrength"), 1.0f);   // radius baked per-disc
    glUniform1f(glGetUniformLocation(progJoin, "uPremult"), additive ? 1.0f : 0.0f);

    if (additive) glBlendEquation(GL_MAX);
    else          glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    glBindBuffer(GL_ARRAY_BUFFER, vboJoin);
    glBufferData(GL_ARRAY_BUFFER, count * sizeof(BeamJoin), caps, GL_STREAM_DRAW);
    glBindVertexArray(vaoJoin);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)count);

    if (additive) glBlendEquation(GL_FUNC_ADD);   // restore default for the rest of the pipeline

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
}

void beam_build_caps(const BeamLine* lines, int count, float endcapMul, float cornerMul,
                     std::vector<BeamJoin>& out) {
    if (count <= 0 || !lines) return;

    std::unordered_map<int64_t, VertAccum> verts;
    verts.reserve((size_t)count * 2);

    auto rec = [&](int64_t k, vec2 p, rgb_t c, float half) {
        VertAccum& v = verts[k];
        if (v.count == 0) { v.pos = p; v.color = c; v.half = half; }
        else {
            if (beam_brightness(c) > beam_brightness(v.color)) v.color = c;
            if (half > v.half) v.half = half;
        }
        v.count++;
    };

    for (int i = 0; i < count; ++i) {
        const BeamLine& L = lines[i];
        int64_t k0 = beam_vkey(L.p0.x, L.p0.y), k1 = beam_vkey(L.p1.x, L.p1.y);
        rec(k0, L.p0, L.color, L.half);
        if (k1 != k0) rec(k1, L.p1, L.color, L.half);
    }

    for (const auto& kv : verts) {
        const VertAccum& v = kv.second;
        float r = v.half * ((v.count >= 2) ? cornerMul : endcapMul);
        if (r > 0.01f) out.push_back({ v.pos, r, v.color });
    }
}
