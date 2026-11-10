#pragma once

// Cross-plugin key broadcast. Correspondence publishes the song key; any plugin
// in the suite can read it. Emerald Tablet (key detection) can suggest a key that
// Correspondence adopts, and can publish the key itself when Correspondence isn't
// running. Backed by named shared memory; lock-free, safe on the audio thread.

#include <cstdint>

namespace suite
{
    struct KeyInfo
    {
        bool valid = false;  // false when nothing has published recently
        int root = 0;        // 0 = C ... 11 = B
        int scale = 0;       // index into suite::music::scales()
        bool fromDetector = false;  // published by Emerald Tablet rather than Correspondence
    };

    struct KeySuggestion
    {
        bool valid = false;
        int root = 0, scale = 0;
        uint32_t sequence = 0;   // increases with every new suggestion
    };

    namespace KeyBus
    {
        /** Publishes the key (call at least once a second to keep it "alive"). */
        void publish (int root, int scale, bool fromDetector = false) noexcept;

        /** Reads the current key; invalid if nothing was published within maxAgeSeconds. */
        KeyInfo read (int maxAgeSeconds = 4) noexcept;

        /** Marks the key as no longer published. */
        void withdraw() noexcept;

        /** Emerald Tablet: offer a detected key (Correspondence decides whether to adopt it). */
        void suggest (int root, int scale) noexcept;
        KeySuggestion readSuggestion (int maxAgeSeconds = 60) noexcept;
    }
}
