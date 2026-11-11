#pragma once

// What Quintessence steers towards, per role (Prima Materia's roles: 0 none, 1 kick, 2 bass,
// 3 hats, 4 pads, 5 lead, 6 vocal, 7 FX).
//
//  - Tone: the usual shape of that part across the ten octave bands (31.5 Hz .. 16 kHz), in
//    dB relative to its loudest band, and which bands matter for it. Only the shape counts
//    (the level is the fader's job), and only bands that matter are compared.
//  - Low cut: below this the part has no job (0: leave the lows alone).
//  - Dynamics: how far the loud moments of each band (low / mid / high) may stand above its
//    typical level (95th minus 50th percentile of 50 ms levels, dB; 0 = leave it alone),
//    and the ratio to use when it needs holding.

#include <array>

namespace qn::targets
{
    struct Role
    {
        const char* name;
        std::array<float, 10> shape;   // dB per octave band
        int from, to;                  // bands that matter (inclusive); from > to: no tone moves
        float lowCutHz;
        std::array<float, 3> swing;    // dB per multiband band
        float ratio;
    };

    inline const std::array<Role, 8>& roles()
    {
        static const std::array<Role, 8> r {{
            { "track", {}, 1, 0, 0.0f,   { 0.0f, 0.0f, 0.0f }, 1.0f },
            { "kick",  { 0, 0, -2, -9, -13, -15, -14, -15, -20, -26 }, 1, 7, 0.0f,   { 0.0f, 0.0f, 0.0f }, 1.0f },   // transients are the point
            { "bass",  { -2, 0, -2, -8, -13, -17, -20, -24, -28, -32 }, 0, 5, 0.0f,  { 5.0f, 7.0f, 0.0f }, 4.0f },
            { "hi-hat",  { -40, -40, -40, -35, -25, -15, -6, -1, 0, -3 }, 6, 9, 300.0f, { 0.0f, 0.0f, 8.0f }, 2.0f },
            { "pad",  { -30, -20, -8, -4, -1, 0, -2, -5, -9, -14 }, 2, 9, 100.0f,   { 6.0f, 6.0f, 7.0f }, 2.5f },
            { "lead",  { -30, -24, -12, -5, -1, 0, -1, -3, -6, -11 }, 3, 9, 150.0f,  { 7.0f, 6.0f, 7.0f }, 3.0f },
            { "vocal", { -40, -30, -12, -5, -1, 0, 0, -2, -6, -12 }, 3, 9, 100.0f,   { 8.0f, 5.0f, 6.0f }, 3.0f },
            { "FX",    {}, 1, 0, 0.0f,   { 0.0f, 0.0f, 0.0f }, 1.0f },
        }};
        return r;
    }
}
