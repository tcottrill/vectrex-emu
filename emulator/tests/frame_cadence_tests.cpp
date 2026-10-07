// Frame-cadence test: the composed picture must be rendered once per Vectrex
// game frame (the BIOS Wait_Recal re-arm of VIA timer 2), not on a fixed cycle
// window. Boots the real BIOS with an empty cart (built-in Mine Storm) and
// measures the emulated-cycle interval between osint_render() calls.
//
// Extra arguments are cart images to survey (report only, no pass/fail). Each
// is run past the BIOS start-up screen (~15 s) to its title, then all four
// player-1 buttons are pressed for 0.2 s at 16 s, so the report shows both:
//   frame_cadence_tests.exe "..\..\x64\Release\data\roms\Bedlam (USA, Europe).vec"
#include <cstdio>
#include <cstring>
#include <vector>
#include <map>
#include "cpu_m6809.h"
#include "cpu_control.h"
#include "vecx.h"
#include "bios.h"
#include "sys_log.h"

namespace Log { void write(Level, const char*, const char*, int, const char*, ...) {} }
int  mixer_alloc_channel(int, int) { return 0; }
void stream_start(int, int, int, int, bool) {}
void stream_stop(int, int) {}
void sample_set_volume_mixer(int, int) {}
void stream_update(int, short*) {}
void mixer_update_sync() {}

static const long kSecond = 1500000;
static const long kChunk = kSecond / 100;   // run in 10 ms steps
static long s_base = 0;                     // cycles before the current vecx_emu call
static std::vector<long> s_renders;         // emulated cycle of each render

void osint_render() { s_renders.push_back(s_base + g_cpu->get_ticks(0)); }

// Run 'seconds' of emulated time. If press_at >= 0, hold all player-1 buttons
// from press_at for 0.2 s. Returns the cycle of every render.
static std::vector<long> run(const char* cart, double seconds, double press_at)
{
    std::memset(g_cpu_mem, 0, 65536);
    std::memcpy(&g_cpu_mem[0xE000], bios_data, bios_data_size);
    if (cart) {
        FILE* f = std::fopen(cart, "rb");
        if (!f) { std::printf("cannot open %s\n", cart); return {}; }
        std::fread(g_cpu_mem, 1, 0x8000, f);
        std::fclose(f);
    }
    vecx_reset();
    s_renders.clear();
    s_base = 0;
    while (s_base < (long)(seconds * kSecond)) {
        const double t = (double)s_base / kSecond;
        const bool held = press_at >= 0 && t >= press_at && t < press_at + 0.2;
        snd_regs[14] = held ? 0xF0 : 0xFF;      // active low: buttons 1-4
        vecx_emu(kChunk);
        s_base += g_cpu->get_ticks(0);
    }
    return s_renders;
}

// Intervals between renders that fall inside [from, to) seconds.
static std::vector<long> intervals(const std::vector<long>& r, double from, double to)
{
    std::vector<long> iv;
    for (size_t i = 1; i < r.size(); ++i)
        if (r[i] >= from * kSecond && r[i] < to * kSecond)
            iv.push_back(r[i] - r[i - 1]);
    return iv;
}

static void report(const char* label, const std::vector<long>& iv, double secs)
{
    std::map<long, int> hist;                    // interval rounded to 1000 cycles
    for (long v : iv) hist[(v + 500) / 1000 * 1000]++;
    std::printf("  %-14s %5.1f renders/s;", label, iv.size() / secs);
    for (auto& h : hist) std::printf(" %ldc x%d", h.first, h.second);
    std::printf("\n");
}

static bool all_within(const std::vector<long>& iv, long lo, long hi)
{
    if (iv.empty()) return false;
    for (long v : iv) if (v < lo || v > hi) return false;
    return true;
}

// A Wait_Recal-style frame loop at $0000: arm T2 with 'period', spin until the
// T2 interrupt flag sets, repeat. The final BRA is at offset 0x11.
static std::vector<unsigned char> t2_loop(unsigned period)
{
    return {
        0x86, (unsigned char)(period & 0xFF), 0xB7, 0xD0, 0x08,   // LDA #lo  ; STA $D008
        0x86, (unsigned char)(period >> 8),   0xB7, 0xD0, 0x09,   // LDA #hi  ; STA $D009 (arm)
        0xB6, 0xD0, 0x0D,                                         // wait: LDA $D00D
        0x85, 0x20,                                               //       BITA #$20
        0x27, 0xF9,                                               //       BEQ wait
        0x20, 0xED,                                               // BRA loop
    };
}

// Boot a bare program at $0000 (reset vector pointed there) and run it for
// 'seconds'; at patch_at seconds, turn the instruction at $0011 into BRA *.
static void run_program(const unsigned char* code, size_t len, double seconds, double patch_at)
{
    std::memset(g_cpu_mem, 0, 65536);
    std::memcpy(&g_cpu_mem[0xE000], bios_data, bios_data_size);
    g_cpu_mem[0xFFFE] = 0x00; g_cpu_mem[0xFFFF] = 0x00;
    std::memcpy(g_cpu_mem, code, len);
    vecx_reset();
    s_renders.clear(); s_base = 0;
    while (s_base < (long)(seconds * kSecond)) {
        if (patch_at >= 0 && s_base >= (long)(patch_at * kSecond)) {
            g_cpu_mem[0x11] = 0x20; g_cpu_mem[0x12] = 0xFE;     // BRA *
            g_cpu_mem[0x0A] = 0x20; g_cpu_mem[0x0B] = 0xFE;     // and the wait loop
            patch_at = -1;
        }
        vecx_emu(kChunk);
        s_base += g_cpu->get_ticks(0);
    }
}

int main(int argc, char** argv)
{
    int failures = 0;

    // Built-in Mine Storm: every frame boundary 30,000 cycles apart (50 Hz).
    std::vector<long> iv = intervals(run(nullptr, 13, -1), 3, 13);
    std::printf("built-in Mine Storm\n");
    report("after boot", iv, 10);
    int at50 = 0;
    for (long v : iv) if (v >= 29900 && v <= 30100) ++at50;
    bool ok = !iv.empty() && at50 == (int)iv.size();
    std::printf("every render 30000 +/- 100 cycles apart (50 Hz): %s\n", ok ? "ok" : "FAIL");
    failures += !ok;

    // A cart that never touches T2 (an infinite BRA loop) free-runs at the
    // 50,000-cycle window once the 150,000-cycle lock timeout has passed.
    {
        static const unsigned char loop[] = { 0x20, 0xFE };   // BRA *
        run_program(loop, sizeof(loop), 3, -1);
        std::vector<long> nt = intervals(s_renders, 0.13, 3);
        ok = all_within(nt, 49990, 50010);
        std::printf("no-T2 cart: every render 50000 cycles apart (30 Hz): %s\n", ok ? "ok" : "FAIL");
        failures += !ok;
    }

    // Wait_Recal-style loops at long T2 periods: Bedlam's $C0FF (49,407) and the
    // maximum $FFFF. One render per T2 frame, never a short extra one.
    for (unsigned t2 : { 0xC0FFu, 0xFFFFu }) {
        std::vector<unsigned char> prog = t2_loop(t2);
        run_program(prog.data(), prog.size(), 4, -1);
        std::vector<long> fr = intervals(s_renders, 0.5, 4);
        ok = all_within(fr, (long)t2, (long)t2 + 40);
        std::printf("T2 loop $%04X: every render %u..%u cycles apart: %s\n",
                    t2, t2, t2 + 40, ok ? "ok" : "FAIL");
        if (!ok) report("got", fr, 3.5);
        failures += !ok;
    }

    // A frame whose drawing outlasts T2: arm 30,000, then ~80,000 cycles of
    // work before the wait (which falls straight through). One render per
    // overrunning frame - never a mid-frame timeout plus a near-empty one.
    {
        static const unsigned char overrun[] = {
            0x86, 0x30, 0xB7, 0xD0, 0x08,   // LDA #$30 ; STA $D008
            0x86, 0x75, 0xB7, 0xD0, 0x09,   // LDA #$75 ; STA $D009 (arm 30,000)
            0x8E, 0x27, 0x10,               // LDX #10000
            0x30, 0x1F,                     // work: LEAX -1,X   (5 cycles)
            0x26, 0xFC,                     //       BNE work    (3 cycles)
            0xB6, 0xD0, 0x0D,               // wait: LDA $D00D
            0x85, 0x20,                     //       BITA #$20
            0x27, 0xF9,                     //       BEQ wait
            0x20, 0xE6,                     // BRA $0000
        };
        run_program(overrun, sizeof(overrun), 4, -1);
        std::vector<long> fr = intervals(s_renders, 0.5, 4);
        ok = all_within(fr, 80000, 80100);
        std::printf("overrunning frame (~80000 cycles): one render each: %s\n", ok ? "ok" : "FAIL");
        if (!ok) report("got", fr, 3.5);
        failures += !ok;
    }

    // T2 frames that stop (the loop is patched to BRA * at 1 s): after the lock
    // timeout the picture keeps coming at the last measured frame period.
    {
        std::vector<unsigned char> prog = t2_loop(0x7530);   // 30,000 = 50 Hz
        run_program(prog.data(), prog.size(), 3, 1.0);
        std::vector<long> fr = intervals(s_renders, 1.2, 3);
        ok = all_within(fr, 30000, 30040);
        std::printf("T2 stops: free-runs at the last frame period (~30000): %s\n", ok ? "ok" : "FAIL");
        if (!ok) report("got", fr, 1.8);
        failures += !ok;
    }

    for (int i = 1; i < argc; ++i) {
        const char* base = std::strrchr(argv[i], '\\');
        std::vector<long> r = run(argv[i], 26, 16.0);
        std::printf("%s\n", base ? base + 1 : argv[i]);
        report("title (14-16s)", intervals(r, 14, 16), 2);
        report("game (18-26s)", intervals(r, 18, 26), 8);
    }

    std::printf(failures ? "\nSOME TESTS FAILED\n" : "\nALL TESTS PASSED\n");
    return failures ? 1 : 0;
}
