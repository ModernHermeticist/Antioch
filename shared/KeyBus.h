#pragma once

// Cross-plugin key broadcast. Correspondence publishes the song key; any plugin
// in the suite can read it. Backed by a tiny named shared-memory block so it
// works across separate plugin DLLs and across bridged (out-of-process) plugins.
// Lock-free: safe to read from the audio thread.

namespace suite
{
    struct KeyInfo
    {
        bool valid = false;  // false when no Correspondence instance has published recently
        int root = 0;        // 0 = C ... 11 = B
        int scale = 0;       // index into suite::music::scales()
    };

    namespace KeyBus
    {
        /** Publishes the key (call at least once a second to keep it "alive"). */
        void publish (int root, int scale) noexcept;

        /** Reads the current key; invalid if nothing was published within maxAgeSeconds. */
        KeyInfo read (int maxAgeSeconds = 4) noexcept;

        /** Marks the key as no longer published (e.g. when Correspondence is removed). */
        void withdraw() noexcept;
    }
}
