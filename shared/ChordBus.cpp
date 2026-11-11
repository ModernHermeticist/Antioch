#include "ChordBus.h"
#include "SharedMemory.h"

#include <atomic>
#include <chrono>

namespace suite::ChordBus
{
    namespace
    {
        // word: [ valid 1 | scale 3 | root 4 | count 4 | quarters x2 : 6 | roots 8 x 4 : 32 ] ; stamp: seconds
        struct Shared { std::atomic<uint64_t> word; std::atomic<uint32_t> stamp; };

        uint32_t now() noexcept
        {
            using namespace std::chrono;
            return (uint32_t) duration_cast<seconds> (system_clock::now().time_since_epoch()).count();
        }

        Shared& shared() noexcept
        {
            static Shared local {};
            static Shared* s = [] { auto* m = static_cast<Shared*> (openSharedBlock ("antioch_suite_chordbus_v1", sizeof (Shared))); return m != nullptr ? m : &local; }();
            return *s;
        }
    }

    void publish (const Progression& p) noexcept
    {
        uint64_t w = 1ull << 63;
        w |= (uint64_t) (p.scale & 7) << 60;
        w |= (uint64_t) (p.root & 15) << 56;
        w |= (uint64_t) (p.count & 15) << 52;
        const auto q = (uint64_t) ((int) (p.quartersPerChord * 2.0 + 0.5) & 63);
        w |= q << 46;
        for (int i = 0; i < 8; ++i) w |= (uint64_t) (p.chordRoots[(size_t) i] & 15) << (4 * i);
        shared().word.store (w);
        shared().stamp.store (now());
    }

    void withdraw() noexcept { shared().word.store (0); }

    bool read (Progression& out, int maxAgeSeconds) noexcept
    {
        const auto w = shared().word.load();
        if ((w >> 63) == 0 || now() - shared().stamp.load() > (uint32_t) maxAgeSeconds) return false;
        out.valid = true;
        out.scale = (int) ((w >> 60) & 7);
        out.root = (int) ((w >> 56) & 15);
        out.count = (int) ((w >> 52) & 15);
        out.quartersPerChord = (double) ((w >> 46) & 63) / 2.0;
        for (int i = 0; i < 8; ++i) out.chordRoots[(size_t) i] = (int) ((w >> (4 * i)) & 15);
        return out.count > 0;
    }
}
