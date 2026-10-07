// -----------------------------------------------------------------------------
// AY-3-8910 / AY-3-8912 PSG emulator for AAE (Another Arcade Emulator)
//
// Attribution / Licensing:
//   The synthesis core in this file -- the volume (DAC) table construction, the
//   per-register write side-effects (_AYWriteReg), and the sample generation
//   loop (AY8910Update): tone/noise/envelope counters, the 17-bit Galois LFSR,
//   the per-sample area integration of the (tone | tone_disable) & (noise |
//   noise_disable) channel mix, and the envelope-shape progression -- is a port
//   of the AY-3-8910 emulator from the M.A.M.E.(TM) project (sound/ay8910.c).
//
//   That MAME code is copyright the M.A.M.E. Team and was based on various code
//   snippets by Ville Hallik, Michael Cuddy, Tatsuyuki Satoh, Fabrice Frances,
//   and Nicola Salmoria. Because this file is a derivative of that work, it is
//   distributed under the terms of the GNU General Public License (version 2 or,
//   at your option, any later version), the license under which the original
//   MAME source was made available. Redistribution must preserve this notice
//   and the original MAME copyright acknowledgement.
//
//   This program is distributed in the hope that it will be useful, but WITHOUT
//   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
//   FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
//   more details: <https://www.gnu.org/licenses/>.
//
//   The surrounding AAE integration -- the public API in ay8910.h (AY8910Config,
//   the ay8910_* entry points and MEM/PORT trampolines), the AY8910Bank
//   lifecycle and mixer-stream wiring, the mid-frame catch-up scheduler, and the
//   first-order DC blocker -- is AAE-original.
// -----------------------------------------------------------------------------

#include "ay8910.h"

#include <cstdint>
#include <cstdlib>      // malloc, free
#include <cstring>      // memset
#include "mixer.h"
#include "sys_log.h"
#include "cpu_control.h" // cpu_scale_by_cycles

#define AY8910_ENABLE_DC_BLOCK 1
#define AY8910_DC_COEF         4076   // ~0.995 * 4096
#define AY8910_DC_SHIFT        12     // matches 4096 = 1<<12
#define AY8910_STEP            0x8000  // 32768

// Host audio config for the vecx build (replaces MAME's Machine->gamedrv->fps
// and config.samplerate). 16-bit stream at 44.1 kHz, 50 fps Vectrex frame rate.
#define AY8910_HOST_SAMPLE_RATE 44100
#define AY8910_HOST_FPS         50

namespace {

class AY8910Chip {
public:
    AY8910Chip() = default;
    void configure(int chip_index, int master_clock, int sys_freq,
                   AY8910PortRead pa_r, AY8910PortRead pb_r,
                   AY8910PortWrite pa_w, AY8910PortWrite pb_w);
    void reset();

    void    write_addr(uint8_t addr);
    void    write_data(uint8_t data);
    uint8_t read_data();

    void render(int16_t* dst, int n);

private:
    int chip_index = 0;
    uint8_t regs[16] = {};
    uint8_t latched_addr = 0;
    int32_t lastEnable = -1;

    uint32_t UpdateStep = 0;
    int32_t  PeriodA = 0, PeriodB = 0, PeriodC = 0, PeriodN = 0, PeriodE = 0;
    int32_t  CountA  = 0, CountB  = 0, CountC  = 0, CountN  = 0, CountE  = 0;
    uint32_t VolA = 0, VolB = 0, VolC = 0, VolE = 0;
    uint8_t  EnvelopeA = 0, EnvelopeB = 0, EnvelopeC = 0;
    uint8_t  OutputA = 0, OutputB = 0, OutputC = 0, OutputN = 0xff;
    int8_t   CountEnv = 0;
    uint8_t  Hold = 0, Alternate = 0, Attack = 0, Holding = 0;
    uint32_t RNG = 1;

    uint32_t VolTable[32] = {};

    int32_t dc_prev_in = 0, dc_prev_out = 0;

    AY8910PortRead  pa_r = nullptr, pb_r = nullptr;
    AY8910PortWrite pa_w = nullptr, pb_w = nullptr;

    void apply_register_side_effects(uint8_t addr, uint8_t data);
};

void AY8910Chip::configure(int chip_index_, int master_clock, int sys_freq,
                           AY8910PortRead pa_r_, AY8910PortRead pb_r_,
                           AY8910PortWrite pa_w_, AY8910PortWrite pb_w_)
{
    chip_index = chip_index_;
    pa_r = pa_r_;
    pb_r = pb_r_;
    pa_w = pa_w_;
    pb_w = pb_w_;

    if (sys_freq <= 0) sys_freq = 1;
    if (master_clock <= 0) master_clock = 1;

    // UpdateStep scales period_reg so that per output sample, CountX advances
    // by AY8910_STEP. PeriodX = period_reg * UpdateStep. Toggle period in
    // samples = period_reg * UpdateStep / AY8910_STEP = period_reg * sys_freq *
    // 8 / master_clock. 
    UpdateStep = static_cast<uint32_t>(
        ((double)AY8910_STEP * (double)sys_freq * 8.0) / (double)master_clock + 0.5);
    if (UpdateStep == 0) UpdateStep = 1;

    // Build the legacy log table. AY fixed and envelope levels use odd entries.
    // VolTable[31] = MAX_OUTPUT = 32767; each step down divides by 10^(1.5/20).
    // Nonzero AY level v reads VolTable[v*2+1]; level zero is silent.
    {
        double out_v = 32767.0;
        for (int i = 31; i > 0; --i) {
            VolTable[i] = static_cast<uint32_t>(out_v + 0.5);
            out_v /= 1.188502227;  // 10^(1.5/20)
        }
        VolTable[0] = 0;
    }
}

void AY8910Chip::reset()
{
    for (int i = 0; i < 16; ++i) regs[i] = 0;
    latched_addr = 0;
    lastEnable = -1;

    PeriodA = PeriodB = PeriodC = PeriodN = PeriodE = 0;
    CountA  = CountB  = CountC  = CountN  = CountE  = 0;
    VolA = VolB = VolC = VolE = 0;
    EnvelopeA = EnvelopeB = EnvelopeC = 0;
    OutputA = OutputB = OutputC = 0;
    OutputN = 0xff;
    CountEnv = 0;
    Hold = Alternate = Attack = Holding = 0;
    RNG = 1;

    dc_prev_in = 0;
    dc_prev_out = 0;

    // Drive R0..R13 through the side-effect handler to initialize all derived
    // state (periods, vol levels, envelope, port writes). Matches MAME's reset.
    for (int i = 0; i < 14; ++i) {
        apply_register_side_effects(static_cast<uint8_t>(i), 0);
    }
}

void AY8910Chip::write_addr(uint8_t addr)
{
    latched_addr = addr & 0x0F;
}

void AY8910Chip::write_data(uint8_t data)
{
    const uint8_t a = latched_addr & 0x0F;
    regs[a] = data;
    apply_register_side_effects(a, data);
}

uint8_t AY8910Chip::read_data()
{
    const uint8_t a = latched_addr & 0x0F;
    if (a == 14) {
        if (pa_r) regs[14] = pa_r();
        return regs[14];
    }
    if (a == 15) {
        if (pb_r) regs[15] = pb_r();
        return regs[15];
    }
    return regs[a];
}

void AY8910Chip::apply_register_side_effects(uint8_t addr, uint8_t data)
{
    (void)data;
    int32_t old_period;
    switch (addr) {
    case 0: case 1:
        regs[1] &= 0x0f;
        old_period = PeriodA;
        PeriodA = (regs[0] + 256 * regs[1]) * static_cast<int32_t>(UpdateStep);
        if (PeriodA == 0) PeriodA = static_cast<int32_t>(UpdateStep);
        CountA += PeriodA - old_period;
        if (CountA <= 0) CountA = 1;
        break;

    case 2: case 3:
        regs[3] &= 0x0f;
        old_period = PeriodB;
        PeriodB = (regs[2] + 256 * regs[3]) * static_cast<int32_t>(UpdateStep);
        if (PeriodB == 0) PeriodB = static_cast<int32_t>(UpdateStep);
        CountB += PeriodB - old_period;
        if (CountB <= 0) CountB = 1;
        break;

    case 4: case 5:
        regs[5] &= 0x0f;
        old_period = PeriodC;
        PeriodC = (regs[4] + 256 * regs[5]) * static_cast<int32_t>(UpdateStep);
        if (PeriodC == 0) PeriodC = static_cast<int32_t>(UpdateStep);
        CountC += PeriodC - old_period;
        if (CountC <= 0) CountC = 1;
        break;

    case 6:
        regs[6] &= 0x1f;
        old_period = PeriodN;
        PeriodN = regs[6] * static_cast<int32_t>(UpdateStep);
        if (PeriodN == 0) PeriodN = static_cast<int32_t>(UpdateStep);
        CountN += PeriodN - old_period;
        if (CountN <= 0) CountN = 1;
        break;

    case 7:
        // Port direction-change tracking. On first write (lastEnable == -1) or
        // when bit 6 / 7 flips, push the appropriate value to the port handler:
        //   port set to OUTPUT (bit=1): push the latched port reg value
        //   port set to INPUT  (bit=0): push 0xFF (open-bus)
        if (lastEnable == -1 ||
            ((lastEnable & 0x40) != (regs[7] & 0x40))) {
            if (pa_w) pa_w((regs[7] & 0x40) ? regs[14] : 0xFF);
        }
        if (lastEnable == -1 ||
            ((lastEnable & 0x80) != (regs[7] & 0x80))) {
            if (pb_w) pb_w((regs[7] & 0x80) ? regs[15] : 0xFF);
        }
        lastEnable = regs[7];
        break;

    case 8:
        regs[8] &= 0x1f;
        EnvelopeA = regs[8] & 0x10;
        VolA = EnvelopeA ? VolE
                         : VolTable[regs[8] ? regs[8] * 2 + 1 : 0];
        break;

    case 9:
        regs[9] &= 0x1f;
        EnvelopeB = regs[9] & 0x10;
        VolB = EnvelopeB ? VolE
                         : VolTable[regs[9] ? regs[9] * 2 + 1 : 0];
        break;

    case 10:
        regs[10] &= 0x1f;
        EnvelopeC = regs[10] & 0x10;
        VolC = EnvelopeC ? VolE
                         : VolTable[regs[10] ? regs[10] * 2 + 1 : 0];
        break;

    case 11: case 12:
        old_period = PeriodE;
        // AY has 16 envelope levels, each lasting 16 master clocks per period.
        PeriodE = (regs[11] + 256 * regs[12]) * static_cast<int32_t>(UpdateStep) * 2;
        if (PeriodE == 0) PeriodE = static_cast<int32_t>(UpdateStep);
        CountE += PeriodE - old_period;
        if (CountE <= 0) CountE = 1;
        break;

    case 13:
        regs[13] &= 0x0f;
        Attack = (regs[13] & 0x04) ? 0x0f : 0x00;
        if ((regs[13] & 0x08) == 0) {
            // Continue = 0: collapse to a one-shot shape (hold at end).
            Hold = 1;
            Alternate = Attack;
        } else {
            Hold = regs[13] & 0x01;
            Alternate = regs[13] & 0x02;
        }
        CountE = PeriodE;
        CountEnv = 0x0f;
        Holding = 0;
        VolE = (CountEnv ^ Attack) ? VolTable[(CountEnv ^ Attack) * 2 + 1] : 0;
        if (EnvelopeA) VolA = VolE;
        if (EnvelopeB) VolB = VolE;
        if (EnvelopeC) VolC = VolE;
        break;

    case 14:
        if (regs[7] & 0x40) {
            if (pa_w) pa_w(regs[14]);
        }
        break;

    case 15:
        if (regs[7] & 0x80) {
            if (pb_w) pb_w(regs[15]);
        }
        break;

    default:
        break;
    }
}

void AY8910Chip::render(int16_t* dst, int n)
{
    if (!dst || n <= 0) return;

    const int32_t length_step = n * AY8910_STEP;

    // Fast-forward disabled / zero-volume channels so their counters don't
    // drift past the sample window. Matches MAME's pre-loop guard.
    if (regs[7] & 0x01) {
        if (CountA <= length_step) CountA += length_step;
        OutputA = 1;
    } else if ((regs[8] & 0x1f) == 0) {
        if (CountA <= length_step) CountA += length_step;
    }
    if (regs[7] & 0x02) {
        if (CountB <= length_step) CountB += length_step;
        OutputB = 1;
    } else if ((regs[9] & 0x1f) == 0) {
        if (CountB <= length_step) CountB += length_step;
    }
    if (regs[7] & 0x04) {
        if (CountC <= length_step) CountC += length_step;
        OutputC = 1;
    } else if ((regs[10] & 0x1f) == 0) {
        if (CountC <= length_step) CountC += length_step;
    }
    if ((regs[7] & 0x38) == 0x38) {
        if (CountN <= length_step) CountN += length_step;
    }

    int32_t outn = (OutputN | regs[7]);

    int remaining = n;
    while (remaining--) {
        int32_t vola = 0, volb = 0, volc = 0;
        int32_t left = AY8910_STEP;

        do {
            const int32_t nextevent = (CountN < left) ? CountN : left;

            // ---- Channel A ----
            if (outn & 0x08) {
                if (OutputA) vola += CountA;
                CountA -= nextevent;
                while (CountA <= 0) {
                    CountA += PeriodA;
                    if (CountA > 0) {
                        OutputA ^= 1;
                        if (OutputA) vola += PeriodA;
                        break;
                    }
                    CountA += PeriodA;
                    vola += PeriodA;
                }
                if (OutputA) vola -= CountA;
            } else {
                CountA -= nextevent;
                while (CountA <= 0) {
                    CountA += PeriodA;
                    if (CountA > 0) { OutputA ^= 1; break; }
                    CountA += PeriodA;
                }
            }

            // ---- Channel B ----
            if (outn & 0x10) {
                if (OutputB) volb += CountB;
                CountB -= nextevent;
                while (CountB <= 0) {
                    CountB += PeriodB;
                    if (CountB > 0) {
                        OutputB ^= 1;
                        if (OutputB) volb += PeriodB;
                        break;
                    }
                    CountB += PeriodB;
                    volb += PeriodB;
                }
                if (OutputB) volb -= CountB;
            } else {
                CountB -= nextevent;
                while (CountB <= 0) {
                    CountB += PeriodB;
                    if (CountB > 0) { OutputB ^= 1; break; }
                    CountB += PeriodB;
                }
            }

            // ---- Channel C ----
            if (outn & 0x20) {
                if (OutputC) volc += CountC;
                CountC -= nextevent;
                while (CountC <= 0) {
                    CountC += PeriodC;
                    if (CountC > 0) {
                        OutputC ^= 1;
                        if (OutputC) volc += PeriodC;
                        break;
                    }
                    CountC += PeriodC;
                    volc += PeriodC;
                }
                if (OutputC) volc -= CountC;
            } else {
                CountC -= nextevent;
                while (CountC <= 0) {
                    CountC += PeriodC;
                    if (CountC > 0) { OutputC ^= 1; break; }
                    CountC += PeriodC;
                }
            }

            // ---- Noise ----
            CountN -= nextevent;
            if (CountN <= 0) {
                // Bit 0 ^ bit 1 of RNG controls whether OutputN toggles.
                if ((RNG + 1) & 2) {
                    OutputN = ~OutputN;
                    outn = (OutputN | regs[7]);
                }
                // 17-bit Galois LFSR with feedback at bits 14 and 17.
                if (RNG & 1) RNG ^= 0x24000;
                RNG >>= 1;
                CountN += PeriodN;
            }

            left -= nextevent;
        } while (left > 0);

        // ---- Envelope progression (one tick per output sample) ----
        if (Holding == 0) {
            CountE -= AY8910_STEP;
            if (CountE <= 0) {
                do {
                    CountEnv--;
                    CountE += PeriodE;
                } while (CountE <= 0);

                if (CountEnv < 0) {
                    if (Hold) {
                        if (Alternate) Attack ^= 0x0f;
                        Holding = 1;
                        CountEnv = 0;
                    } else {
                        if (Alternate && (CountEnv & 0x10)) Attack ^= 0x0f;
                        CountEnv &= 0x0f;
                    }
                }

                VolE = (CountEnv ^ Attack) ? VolTable[(CountEnv ^ Attack) * 2 + 1] : 0;
                if (EnvelopeA) VolA = VolE;
                if (EnvelopeB) VolB = VolE;
                if (EnvelopeC) VolC = VolE;
            }
        }

        // ---- Mix: (area * volume) / STEP per channel, sum -----------------
        int32_t sum = static_cast<int32_t>(
            (static_cast<int64_t>(vola) * VolA
           + static_cast<int64_t>(volb) * VolB
           + static_cast<int64_t>(volc) * VolC) / AY8910_STEP);

#if AY8910_ENABLE_DC_BLOCK
        const int32_t in_dc  = sum;
        const int32_t out_dc = in_dc - dc_prev_in
            + static_cast<int32_t>((static_cast<int64_t>(dc_prev_out) * AY8910_DC_COEF) >> AY8910_DC_SHIFT);
        dc_prev_in  = in_dc;
        dc_prev_out = out_dc;
        int32_t s = out_dc;
#else
        int32_t s = sum;
#endif

        if (s > 32767)  s = 32767;
        if (s < -32768) s = -32768;
        *dst++ = static_cast<int16_t>(s);
    }
}

struct AY8910Bank {
    int            num_chips                = 0;
    int            base_clock               = 0;
    int            sys_freq                 = 0;
    int            buffer_len               = 0;
    int            sample_pos               = 0;   // shared across chips (clocked together)
    int            mixing_level[MAX_8910]   = {};
    int            mixer_ch    [MAX_8910]   = { -1, -1, -1, -1, -1 };
    int16_t*       buffer      [MAX_8910]   = { nullptr, nullptr, nullptr, nullptr, nullptr };
    AY8910Chip     chip        [MAX_8910];
    bool           active                   = false;
};
AY8910Bank g_bank;

void update_bank_to_now()
{
    if (!g_bank.active) return;
    int newpos = cpu_scale_by_cycles(g_bank.buffer_len, g_bank.base_clock);
    if (newpos > g_bank.buffer_len) newpos = g_bank.buffer_len;
    if (newpos < 0) newpos = 0;
    const int delta = newpos - g_bank.sample_pos;
    if (delta <= 0) return;
    for (int i = 0; i < g_bank.num_chips; ++i) {
        g_bank.chip[i].render(g_bank.buffer[i] + g_bank.sample_pos, delta);
    }
    g_bank.sample_pos = newpos;
}

} // namespace

int ay8910_sh_start(const AY8910Config* cfg)
{
    if (!cfg) {
        LOG_ERROR("ay8910_sh_start: null config");
        return 1;
    }
    if (cfg->num_chips < 1 || cfg->num_chips > MAX_8910) {
        LOG_ERROR("ay8910_sh_start: num_chips out of range (%d)", cfg->num_chips);
        return 1;
    }
    if (cfg->base_clock <= 0) {
        LOG_ERROR("ay8910_sh_start: base_clock must be > 0");
        return 1;
    }
    if (g_bank.active) {
        LOG_WARN("ay8910_sh_start called while bank already active; stopping previous bank first");
        ay8910_sh_stop();
    }

    g_bank.num_chips  = cfg->num_chips;
    g_bank.base_clock = cfg->base_clock;
    g_bank.sys_freq   = AY8910_HOST_SAMPLE_RATE;            // was config.samplerate
    g_bank.buffer_len = g_bank.sys_freq / AY8910_HOST_FPS;  // was / Machine->gamedrv->fps
    g_bank.sample_pos = 0;

    for (int i = 0; i < g_bank.num_chips; ++i) {
        g_bank.mixing_level[i] = cfg->mixing_level[i];
        g_bank.mixer_ch[i]     = -1;
        g_bank.buffer[i]       = static_cast<int16_t*>(std::malloc(sizeof(int16_t) * static_cast<size_t>(g_bank.buffer_len)));
        if (!g_bank.buffer[i]) {
            LOG_ERROR("ay8910_sh_start: buffer alloc failed for chip %d", i);
            ay8910_sh_stop();
            return 1;
        }
        std::memset(g_bank.buffer[i], 0, sizeof(int16_t) * static_cast<size_t>(g_bank.buffer_len));

        g_bank.chip[i].configure(i, cfg->base_clock, g_bank.sys_freq,
                                 cfg->port_a_read [i], cfg->port_b_read [i],
                                 cfg->port_a_write[i], cfg->port_b_write[i]);
        g_bank.chip[i].reset();

        g_bank.mixer_ch[i] = mixer_alloc_channel(MIXER_CHIP_STREAM_RANGE_LOW, MIXER_FIRST_RESERVED_CHANNEL);
        if (g_bank.mixer_ch[i] < 0) {
            LOG_ERROR("ay8910_sh_start: no free mixer channel for chip %d", i);
            ay8910_sh_stop();
            return 1;
        }
        stream_start(g_bank.mixer_ch[i], 0, 16, AY8910_HOST_FPS, false);
        sample_set_volume_mixer(g_bank.mixer_ch[i], cfg->mixing_level[i]);
    }

    g_bank.active = true;
    return 0;
}

void ay8910_sh_stop(void)
{
    for (int i = 0; i < MAX_8910; ++i) {
        if (g_bank.mixer_ch[i] >= 0) {
            stream_stop(g_bank.mixer_ch[i], 0);
            g_bank.mixer_ch[i] = -1;
        }
        if (g_bank.buffer[i]) {
            std::free(g_bank.buffer[i]);
            g_bank.buffer[i] = nullptr;
        }
    }
    g_bank.num_chips  = 0;
    g_bank.base_clock = 0;
    g_bank.sys_freq   = 0;
    g_bank.buffer_len = 0;
    g_bank.sample_pos = 0;
    g_bank.active     = false;
}

void ay8910_sh_update(void)
{
    if (!g_bank.active) return;
    const int remains = g_bank.buffer_len - g_bank.sample_pos;
    if (remains > 0) {
        for (int i = 0; i < g_bank.num_chips; ++i) {
            g_bank.chip[i].render(g_bank.buffer[i] + g_bank.sample_pos, remains);
        }
    }
    for (int i = 0; i < g_bank.num_chips; ++i) {
        if (g_bank.mixer_ch[i] >= 0) {
            stream_update(g_bank.mixer_ch[i], g_bank.buffer[i]);
        }
    }
    g_bank.sample_pos = 0;
}

void ay8910_reset(int chip)
{
    if (!g_bank.active) return;
    if (chip == -1) {
        g_bank.sample_pos = 0;
        for (int i = 0; i < g_bank.num_chips; ++i) {
            g_bank.chip[i].reset();
        }
        return;
    }
    if (chip < 0 || chip >= g_bank.num_chips) return;
    g_bank.chip[chip].reset();
}

void ay8910_write(int chip, int addr, uint8_t data)
{
	if (!g_bank.active) return;
    if (chip < 0 || chip >= g_bank.num_chips) {
        static bool warned[MAX_8910] = {};
        const int safe = (chip >= 0 && chip < MAX_8910) ? chip : 0;
        if (!warned[safe]) {
            LOG_WARN("ay8910_write: chip index %d out of range (num_chips=%d)", chip, g_bank.num_chips);
            warned[safe] = true;
        }
        return;
    }
    update_bank_to_now();
    if (addr & 1) {
        g_bank.chip[chip].write_data(data);
    } else {
        g_bank.chip[chip].write_addr(data);
    }
}

uint8_t ay8910_read(int chip)
{
    if (!g_bank.active) return 0xFF;
    if (chip < 0 || chip >= g_bank.num_chips) return 0xFF;
    update_bank_to_now();
    return g_bank.chip[chip].read_data();
}

// ----- MEM_ADDR trampolines (MemoryWriteByte / MemoryReadByte path) ----------
// Convention: control_w writes to addr offset 0 (latches the register selector);
// data_w writes to addr offset 1 (writes to the latched register).
// data_r reads the latched register (addr offset is ignored by AY).
void    ay8910_0_control_w(uint32_t, uint8_t data, struct MemoryWriteByte*)  { ay8910_write(0, 0, data); }
void    ay8910_0_data_w   (uint32_t, uint8_t data, struct MemoryWriteByte*)  { ay8910_write(0, 1, data); }
uint8_t ay8910_0_data_r   (uint32_t,                struct MemoryReadByte*)   { return ay8910_read(0); }
void    ay8910_1_control_w(uint32_t, uint8_t data, struct MemoryWriteByte*)  { ay8910_write(1, 0, data); }
void    ay8910_1_data_w   (uint32_t, uint8_t data, struct MemoryWriteByte*)  { ay8910_write(1, 1, data); }
uint8_t ay8910_1_data_r   (uint32_t,                struct MemoryReadByte*)   { return ay8910_read(1); }
void    ay8910_2_control_w(uint32_t, uint8_t data, struct MemoryWriteByte*)  { ay8910_write(2, 0, data); }
void    ay8910_2_data_w   (uint32_t, uint8_t data, struct MemoryWriteByte*)  { ay8910_write(2, 1, data); }
uint8_t ay8910_2_data_r   (uint32_t,                struct MemoryReadByte*)   { return ay8910_read(2); }
void    ay8910_3_control_w(uint32_t, uint8_t data, struct MemoryWriteByte*)  { ay8910_write(3, 0, data); }
void    ay8910_3_data_w   (uint32_t, uint8_t data, struct MemoryWriteByte*)  { ay8910_write(3, 1, data); }
uint8_t ay8910_3_data_r   (uint32_t,                struct MemoryReadByte*)   { return ay8910_read(3); }
void    ay8910_4_control_w(uint32_t, uint8_t data, struct MemoryWriteByte*)  { ay8910_write(4, 0, data); }
void    ay8910_4_data_w   (uint32_t, uint8_t data, struct MemoryWriteByte*)  { ay8910_write(4, 1, data); }
uint8_t ay8910_4_data_r   (uint32_t,                struct MemoryReadByte*)   { return ay8910_read(4); }

// ----- PORT_ADDR (Z80 IO) trampolines ----------------------------------------
uint16_t ay8910_0_data_port_r   (uint16_t,                struct z80PortRead*)  { return static_cast<uint16_t>(ay8910_read(0)); }
void     ay8910_0_control_port_w(uint16_t, uint8_t value, struct z80PortWrite*) { ay8910_write(0, 0, value); }
void     ay8910_0_data_port_w   (uint16_t, uint8_t value, struct z80PortWrite*) { ay8910_write(0, 1, value); }
uint16_t ay8910_1_data_port_r   (uint16_t,                struct z80PortRead*)  { return static_cast<uint16_t>(ay8910_read(1)); }
void     ay8910_1_control_port_w(uint16_t, uint8_t value, struct z80PortWrite*) { ay8910_write(1, 0, value); }
void     ay8910_1_data_port_w   (uint16_t, uint8_t value, struct z80PortWrite*) { ay8910_write(1, 1, value); }
uint16_t ay8910_2_data_port_r   (uint16_t,                struct z80PortRead*)  { return static_cast<uint16_t>(ay8910_read(2)); }
void     ay8910_2_control_port_w(uint16_t, uint8_t value, struct z80PortWrite*) { ay8910_write(2, 0, value); }
void     ay8910_2_data_port_w   (uint16_t, uint8_t value, struct z80PortWrite*) { ay8910_write(2, 1, value); }
uint16_t ay8910_3_data_port_r   (uint16_t,                struct z80PortRead*)  { return static_cast<uint16_t>(ay8910_read(3)); }
void     ay8910_3_control_port_w(uint16_t, uint8_t value, struct z80PortWrite*) { ay8910_write(3, 0, value); }
void     ay8910_3_data_port_w   (uint16_t, uint8_t value, struct z80PortWrite*) { ay8910_write(3, 1, value); }
uint16_t ay8910_4_data_port_r   (uint16_t,                struct z80PortRead*)  { return static_cast<uint16_t>(ay8910_read(4)); }
void     ay8910_4_control_port_w(uint16_t, uint8_t value, struct z80PortWrite*) { ay8910_write(4, 0, value); }
void     ay8910_4_data_port_w   (uint16_t, uint8_t value, struct z80PortWrite*) { ay8910_write(4, 1, value); }
