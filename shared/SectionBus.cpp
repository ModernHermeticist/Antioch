#include "SectionBus.h"
#include "SharedMemory.h"

#include <atomic>
#include <chrono>
#include <algorithm>

namespace suite::SectionBus
{
    namespace
    {
        constexpr uint32_t magic = 0x0B0B05u;

        struct Shared
        {
            std::atomic<uint32_t> sequence;      // odd while being written (seqlock)
            std::atomic<uint32_t> magicWord;
            std::atomic<uint32_t> stamp;         // seconds, for staleness
            std::atomic<uint32_t> count;
            std::atomic<uint32_t> totalBars;
            std::atomic<int32_t> finalLift;
            std::atomic<int32_t> dropGap;
            std::atomic<int32_t> intensity, liveType, livePrevType, liveIndex, dropEntrance;
            std::atomic<int64_t> liveStartMicroQ;   // live section start, in millionths of a quarter
            std::array<std::atomic<uint32_t>, SectionMap::maxSections> entries; // startBar << 8 | type
        };

        uint32_t nowSeconds() noexcept
        {
            using namespace std::chrono;
            return (uint32_t) duration_cast<seconds> (system_clock::now().time_since_epoch()).count();
        }

        Shared& shared() noexcept
        {
            static Shared local {};
            static Shared* s = []
            {
                auto* m = static_cast<Shared*> (openSharedBlock ("antioch_suite_sectionbus_v6", sizeof (Shared)));
                return m != nullptr ? m : &local;
            }();
            return *s;
        }
    }

    void publish (const SectionMap& map) noexcept
    {
        auto& s = shared();
        const auto seq = s.sequence.load (std::memory_order_relaxed);
        s.sequence.store (seq + 1, std::memory_order_release);   // odd: writing
        std::atomic_thread_fence (std::memory_order_release);

        const auto n = map.count < 0 ? 0 : (map.count > SectionMap::maxSections ? SectionMap::maxSections : map.count);
        for (int i = 0; i < n; ++i)
        {
            // [ startBar : 16 | energy (255 = default) : 8 | type : 8 ]
            const auto& sec = map.sections[(size_t) i];
            const auto energy = sec.energy < 0 ? 255u : (uint32_t) std::min (100, sec.energy);
            s.entries[(size_t) i].store (((uint32_t) sec.startBar << 16) | (energy << 8) | (uint32_t) sec.type, std::memory_order_relaxed);
        }
        s.count.store ((uint32_t) n, std::memory_order_relaxed);
        s.totalBars.store ((uint32_t) map.totalBars, std::memory_order_relaxed);
        s.finalLift.store (map.finalLift, std::memory_order_relaxed);
        s.dropGap.store (map.dropGap, std::memory_order_relaxed);
        s.intensity.store (map.intensity, std::memory_order_relaxed);
        s.dropEntrance.store (map.dropEntrance, std::memory_order_relaxed);
        s.liveType.store (map.liveType, std::memory_order_relaxed);
        s.livePrevType.store (map.livePrevType, std::memory_order_relaxed);
        s.liveIndex.store (map.liveIndex, std::memory_order_relaxed);
        s.liveStartMicroQ.store ((int64_t) std::llround (map.liveStartQ * 1.0e6), std::memory_order_relaxed);
        s.magicWord.store (magic, std::memory_order_relaxed);
        s.stamp.store (nowSeconds(), std::memory_order_relaxed);

        std::atomic_thread_fence (std::memory_order_release);
        s.sequence.store (seq + 2, std::memory_order_release);   // even: stable
    }

    void withdraw() noexcept
    {
        SectionMap empty;
        publish (empty);
    }

    bool read (SectionMap& out, int maxAgeSeconds) noexcept
    {
        auto& s = shared();
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            const auto before = s.sequence.load (std::memory_order_acquire);
            if (before & 1u)
                continue; // a write is in progress

            SectionMap m;
            const auto okMagic = s.magicWord.load (std::memory_order_relaxed) == magic;
            const auto stamp = s.stamp.load (std::memory_order_relaxed);
            m.count = (int) std::min<uint32_t> (s.count.load (std::memory_order_relaxed), SectionMap::maxSections);
            m.totalBars = (int) s.totalBars.load (std::memory_order_relaxed);
            m.finalLift = s.finalLift.load (std::memory_order_relaxed);
            m.dropGap = s.dropGap.load (std::memory_order_relaxed);
            m.intensity = s.intensity.load (std::memory_order_relaxed);
            m.dropEntrance = s.dropEntrance.load (std::memory_order_relaxed);
            m.liveType = s.liveType.load (std::memory_order_relaxed);
            m.livePrevType = s.livePrevType.load (std::memory_order_relaxed);
            m.liveIndex = s.liveIndex.load (std::memory_order_relaxed);
            m.liveStartQ = (double) s.liveStartMicroQ.load (std::memory_order_relaxed) * 1.0e-6;
            for (int i = 0; i < m.count; ++i)
            {
                const auto e = s.entries[(size_t) i].load (std::memory_order_relaxed);
                m.sections[(size_t) i].startBar = (int) (e >> 16);
                m.sections[(size_t) i].energy = ((e >> 8) & 0xffu) == 255u ? -1 : (int) ((e >> 8) & 0xffu);
                m.sections[(size_t) i].type = (SectionType) (e & 0xffu);
            }

            std::atomic_thread_fence (std::memory_order_acquire);
            if (s.sequence.load (std::memory_order_relaxed) != before)
                continue; // changed while reading: try again

            const auto age = (int64_t) nowSeconds() - (int64_t) stamp;
            if (! okMagic || m.count == 0 || age < -2 || age > maxAgeSeconds)
                return false;
            out = m;
            return true;
        }
        return false;
    }
}
