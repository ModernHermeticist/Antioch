#pragma once

// ChordBus: the song's progression as Correspondence plays it, so other plugins can follow
// the harmony (Azoth's riser climbs through the chord roots, Vibration widens on tension
// chords). The progression restarts at every section, as Correspondence writes it.

#include <array>
#include <cstdint>

namespace suite
{
    struct Progression
    {
        bool valid = false;
        int root = 9, scale = 0, count = 0;   // key, and how many chords
        double quartersPerChord = 4.0;
        std::array<int, 8> chordRoots {};     // each chord's root, semitones above the key's root (0..11)

        /** The chord playing `quartersIntoSection` into a section (index 0..count-1). */
        int chordAt (double quartersIntoSection) const noexcept
        {
            if (count <= 0 || quartersPerChord <= 0.0) return 0;
            const auto i = (long long) (quartersIntoSection / quartersPerChord);
            return (int) (((i % count) + count) % count);
        }
        /** True for chords that pull away from home: anything not on the tonic (or its relative). */
        bool isTension (int index) const noexcept
        {
            const auto r = chordRoots[(size_t) (index & 7)];
            return ! (r == 0 || (scale == 0 && r == 3) || (scale == 1 && r == 9));
        }
    };

    namespace ChordBus
    {
        void publish (const Progression&) noexcept;
        void withdraw() noexcept;
        /** False if nothing was published in the last few seconds. */
        bool read (Progression& out, int maxAgeSeconds = 4) noexcept;
    }
}
