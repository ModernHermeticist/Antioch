#pragma once

// Arrangement broadcast. Ouroboros publishes the song's section map (which bars
// are intro, breakdown, build, drop...). Other plugins read the map and work out
// the current section from their *own* host position, so reactions land exactly
// on the bar line regardless of which plugin processes first. Lock-free reads,
// safe on the audio thread.

#include <array>
#include <cstdint>
#include <cmath>

namespace suite
{
    enum class SectionType : uint8_t { Intro = 0, Groove, Breakdown, Build, Drop, Outro, count };

    inline const char* sectionName (SectionType t) noexcept
    {
        static const char* names[] = { "Intro", "Groove", "Breakdown", "Build", "Drop", "Outro" };
        return names[(int) t < (int) SectionType::count ? (int) t : 0];
    }

    /** True for sections where the kick normally drops out. */
    inline bool isKicklessSection (SectionType t) noexcept { return t == SectionType::Breakdown || t == SectionType::Build; }

    struct Section
    {
        int startBar = 1;                      // 1-based
        SectionType type = SectionType::Intro;
    };

    struct SectionMap
    {
        static constexpr int maxSections = 32;
        int count = 0;
        int totalBars = 0;                     // song length; the last section ends after this bar
        int finalLift = 0;                     // semitones the key moves up from the last drop on (0 = no key change)
        int dropGap = 0;                       // before every drop: 0 = per track, 1 = 1/2 beat silence, 2 = 1 beat, 3 = kick-only bar
        std::array<Section, maxSections> sections {};

        /** Index of the last Drop, or -1. */
        int lastDropIndex() const noexcept
        {
            for (int i = count - 1; i >= 0; --i)
                if (sections[(size_t) i].type == SectionType::Drop) return i;
            return -1;
        }
        /** Semitones the key is shifted in section `index` (the last drop and everything after it). */
        int keyShiftOf (int index) const noexcept
        {
            const auto last = lastDropIndex();
            return finalLift != 0 && last >= 0 && index >= last ? finalLift : 0;
        }

        bool operator== (const SectionMap& o) const noexcept
        {
            if (count != o.count || totalBars != o.totalBars || finalLift != o.finalLift || dropGap != o.dropGap) return false;
            for (int i = 0; i < count; ++i)
                if (sections[(size_t) i].startBar != o.sections[(size_t) i].startBar || sections[(size_t) i].type != o.sections[(size_t) i].type)
                    return false;
            return true;
        }
        bool operator!= (const SectionMap& o) const noexcept { return ! (*this == o); }

        int endBar (int index) const noexcept   // first bar after section `index`
        {
            return index + 1 < count ? sections[(size_t) index + 1].startBar : totalBars + 1;
        }
    };

    namespace SectionBus
    {
        void publish (const SectionMap&) noexcept;
        bool read (SectionMap& out, int maxAgeSeconds = 4) noexcept;   // false if nothing published recently
        void withdraw() noexcept;
    }

    //==============================================================================
    /** Audio-thread helper: refresh the map once per block, then locate sections per sample. */
    class SectionFollower
    {
    public:
        struct Position
        {
            bool valid = false;
            SectionType type = SectionType::Intro, next = SectionType::Intro;
            double startQ = 0.0, endQ = 0.0;   // section bounds in quarter notes
            bool hasNext = false;
            int index = -1;
            int occurrence = 0;   // how many earlier sections had the same type (0 = first drop, 1 = second drop...)
            int keyShift = 0;     // semitones above the song's key here (final-drop key change)
            int nextKeyShift = 0; // ... in the next section (a build leading into the lifted drop aims for the new key)

            bool isLaterDrop() const noexcept { return type == SectionType::Drop && occurrence > 0; }

            double progress (double ppq) const noexcept { return endQ > startQ ? (ppq - startQ) / (endQ - startQ) : 0.0; }
        };

        /** Call once per block. Returns true if a map is available. */
        bool refresh() noexcept
        {
            SectionMap fresh;
            if (SectionBus::read (fresh))
            {
                map = fresh;
                available = true;
                misses = 0;
            }
            else if (++misses > 64)   // keep the last map through brief contention; drop it once it's really gone
            {
                available = false;
            }
            return available;
        }

        /** Test/diagnostic helper: forget the cached map immediately. */
        void forget() noexcept { available = false; misses = 0; }

        Position locate (double ppq, double barQuarters) const noexcept
        {
            Position p;
            if (! available || map.count == 0 || barQuarters <= 0.0)
                return p;

            const auto bar = (int) std::floor (ppq / barQuarters + 1.0e-9) + 1;
            if (bar < map.sections[0].startBar || bar > map.totalBars)
                return p;

            int i = 0;
            while (i + 1 < map.count && map.sections[(size_t) i + 1].startBar <= bar)
                ++i;

            p.valid = true;
            p.index = i;
            p.type = map.sections[(size_t) i].type;
            p.startQ = (map.sections[(size_t) i].startBar - 1) * barQuarters;
            p.endQ = (map.endBar (i) - 1) * barQuarters;
            for (int k = 0; k < i; ++k)
                if (map.sections[(size_t) k].type == p.type) ++p.occurrence;
            p.hasNext = i + 1 < map.count;
            if (p.hasNext) p.next = map.sections[(size_t) i + 1].type;
            p.keyShift = map.keyShiftOf (i);
            p.nextKeyShift = p.hasNext ? map.keyShiftOf (i + 1) : p.keyShift;
            return p;
        }

        bool isAvailable() const noexcept { return available; }
        const SectionMap& getMap() const noexcept { return map; }

    private:
        SectionMap map;
        bool available = false;
        int misses = 0;
    };
}
