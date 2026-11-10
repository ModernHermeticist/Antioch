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
        std::array<Section, maxSections> sections {};

        bool operator== (const SectionMap& o) const noexcept
        {
            if (count != o.count || totalBars != o.totalBars) return false;
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
