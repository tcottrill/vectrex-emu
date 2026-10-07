// CPU-side tests for the AAE beam renderer port (vector_draw.cpp) and the
// Vectrex add_line/add_dot seam (emu_vector_draw.cpp). No GL context: only the
// batch-building and join/cap connectivity paths run.
#include <cstdio>
#include <cmath>
#include <vector>
#include "aae_mame_driver.h"
#include "vector_draw.h"
#include "emu_vector_draw.h"
#include "phosphor.h"
#include "sys_log.h"

namespace Log { void write(Level, const char*, const char*, int, const char*, ...) {} }

static AAEDriver s_drv = { "test", VIDEO_TYPE_VECTOR | VECTOR_USES_BW, ROT0, { 0, 1023, 0, 1023 } };
static RunningMachine s_machine = { &s_drv, &s_drv, 0, VIDEO_TYPE_VECTOR | VECTOR_USES_BW, { 0, 1023, 0, 1023 }, { 0 } };
RunningMachine* Machine = &s_machine;
int art_loaded[6] = { 0 };

static int failures = 0;
static void check(const char* name, bool ok)
{
    std::printf("%-58s %s\n", name, ok ? "ok" : "FAIL");
    if (!ok) ++failures;
}
static bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

static const rgb_t WHITE = 0xFFFFFFFFu;

// Count caps by radius among the ones built for the current batch.
static std::vector<BeamJoin> caps_for_current(float endcapMul, float cornerMul)
{
    std::vector<BeamJoin> out;
    const std::vector<BeamLine>& lines = beam_get_lines();
    beam_build_caps(lines.data(), (int)lines.size(), endcapMul, cornerMul, out);
    return out;
}

int main()
{
    const float half = 1.0f, endcap = 1.0f, corner = 0.85f;

    // Closed square: every vertex is shared by two segments -> 4 corner joins.
    beam_clear();
    beam_add_line(100, 100, 200, 100, 255, WHITE, half);
    beam_add_line(200, 100, 200, 200, 255, WHITE, half);
    beam_add_line(200, 200, 100, 200, 255, WHITE, half);
    beam_add_line(100, 200, 100, 100, 255, WHITE, half);
    {
        auto caps = caps_for_current(endcap, corner);
        bool allCorner = true;
        for (auto& c : caps) allCorner &= near(c.half, half * corner);
        check("square: 4 lines", beam_get_lines().size() == 4);
        check("square: 4 caps, all corner joins", caps.size() == 4 && allCorner);
    }

    // T-junction: B shared by 3 segments (corner), A/C/D are terminations.
    beam_clear();
    beam_add_line(100, 300, 200, 300, 255, WHITE, half);   // A-B
    beam_add_line(200, 300, 300, 300, 255, WHITE, half);   // B-C
    beam_add_line(200, 300, 200, 400, 255, WHITE, half);   // B-D
    {
        auto caps = caps_for_current(endcap, corner);
        int nCorner = 0, nEnd = 0;
        for (auto& c : caps) {
            if (near(c.half, half * corner)) ++nCorner;
            else if (near(c.half, half * endcap)) ++nEnd;
        }
        check("T-junction: 1 corner join + 3 end-caps", caps.size() == 4 && nCorner == 1 && nEnd == 3);
    }

    // Blanked segment (intensity 0) contributes no geometry and no vertex.
    beam_clear();
    beam_add_line(10, 10, 50, 50, 0, WHITE, half);
    check("blank segment: dropped", beam_get_lines().empty() && caps_for_current(endcap, corner).empty());

    // Vectrex seam: add_line uses config.linewidth / 2 as the half-width.
    beam_clear();
    config.linewidth = 3.0f;
    add_line(10, 10, 60, 10, 200, WHITE);
    check("add_line: half-width = linewidth / 2",
          beam_get_lines().size() == 1 && near(beam_get_lines()[0].half, 1.5f));

    // Vectrex seam: add_dot is a degenerate segment whose single vertex gets one
    // round end-cap of radius dot_size / 2, independent of the line width.
    beam_clear();
    add_dot(500, 500, 255, WHITE, 4.0f);
    {
        auto caps = caps_for_current(endcap, corner);
        check("add_dot: one degenerate segment", beam_get_lines().size() == 1 && near(beam_get_lines()[0].half, 2.0f));
        check("add_dot: one end-cap, radius = dot_size / 2",
              caps.size() == 1 && near(caps[0].half, 2.0f) && near(caps[0].center.x, 500.0f) && near(caps[0].center.y, 500.0f));
    }

    // Intensity reaches the beam colour (white & intensity, gain 0).
    beam_clear();
    config.gain = 0;
    add_line(0, 0, 10, 0, 0x80, WHITE);
    check("intensity 0x80 -> grey 0x80 beam, opaque",
          beam_get_lines().size() == 1 && beam_get_lines()[0].color == 0xFF808080u);

    // cache_clear empties the batch.
    cache_clear();
    check("cache_clear: batch empty", beam_get_lines().empty());

    // Phosphor persistence decay (phosphor.h).
    const float more = phosphor_fade_seconds(2);
    check("phosphor: no time passed keeps everything", near(phosphor_keep(0.0f, more), 1.0f));
    check("phosphor: 10% left after the fade time", near(phosphor_keep(more, more), 0.1f));
    check("phosphor: level 2 == Vectrexy 0.01^(dt*3) at 60 Hz",
          near(phosphor_keep(1.0f / 60, more), std::pow(0.01f, 3.0f / 60)));
    {
        // Rate independence: fifty 20 ms steps decay as much as one 1 s step.
        float k50 = 1.0f;
        for (int i = 0; i < 50; ++i) k50 *= phosphor_keep(0.02f, more);
        check("phosphor: 50 x 20 ms == 1 x 1 s", std::fabs(k50 - phosphor_keep(1.0f, more)) < 1e-6f);
    }
    check("phosphor: levels get longer",
          phosphor_fade_seconds(1) < phosphor_fade_seconds(2) && phosphor_fade_seconds(2) < phosphor_fade_seconds(3));

    std::printf(failures ? "\nSOME TESTS FAILED (%d)\n" : "\nALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
