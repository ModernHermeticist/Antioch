#pragma once

// Loudness target broadcast: Mentalism announces the target it's measuring against
// (integrated LUFS and true-peak ceiling); Lapis can aim the master at it.

namespace suite
{
    struct LoudnessTarget
    {
        bool valid = false;
        float lufs = -8.0f, ceilingDbtp = -0.3f;
    };

    namespace TargetBus
    {
        void publish (float lufs, float ceilingDbtp) noexcept;
        LoudnessTarget read (int maxAgeSeconds = 4) noexcept;
        void withdraw() noexcept;
    }
}
