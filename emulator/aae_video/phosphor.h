#pragma once
// -----------------------------------------------------------------------------
// [vectrex-port] Phosphor persistence decay, after Vectrexy's DarkenTexture
// pass (github.com/amaiorano/vectrexy, libs/sdl_engine/src/shaders): the image
// decays exponentially with elapsed time, so the fade looks the same at any
// game or display rate. Vectrexy's 0.01^(dt * 3) is "falls to 10% in 1/6 s".
// -----------------------------------------------------------------------------
#include <cmath>

// Seconds for a line to fall to 10% brightness at trail level 1..3.
inline float phosphor_fade_seconds(int level)
{
    switch (level) {
    case 1:  return 0.080f;   // Little
    case 3:  return 0.330f;   // Max
    default: return 1.0f / 6.0f;   // More: Vectrexy's default
    }
}

// Fraction of brightness kept after dt seconds.
inline float phosphor_keep(float dt, float fade_seconds)
{
    if (dt <= 0.0f || fade_seconds <= 0.0f) return dt <= 0.0f ? 1.0f : 0.0f;
    return std::pow(0.1f, dt / fade_seconds);
}

// Decayed history below this is snapped to black (Vectrexy uses 0.1), so the
// fade ends cleanly instead of leaving a long dim tail.
constexpr float kPhosphorCutoff = 0.1f;
