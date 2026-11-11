#pragma once

// VocalBus: which bars of the song the vocal sings in. Solve et Coagula (which hears the
// vocal on its sidechain) marks bars as the song plays; Correspondence reads them so the
// exported lead leaves room for the vocal. Up to 1024 bars, in named shared memory.

#include <cstdint>
#include <vector>

namespace suite::VocalBus
{
    constexpr int maxBars = 1024;
    /** Marks bar (1-based) as having vocal in it, or not. */
    void mark (int bar, bool vocal) noexcept;
    void clear() noexcept;
    /** Bars with vocal (index 0 = bar 1); empty if nothing has been marked. */
    std::vector<bool> read();
    /** True if bar (1-based) was marked as sung in (no allocation: safe on the audio thread). */
    bool sings (int bar) noexcept;
    /** True if anything has been marked. */
    bool any() noexcept;
}
