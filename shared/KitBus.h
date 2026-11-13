#pragma once

// The song's kit rhythm: Correspondence shares the rhythm of its kit's chord part (and arp) as
// 16 gate steps, so Kybalion's gate can lock to the parts the song is written with.
// Header-only shared memory; a value older than 30 s counts as gone.

#include "SharedMemory.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <vector>

namespace suite::KitBus
{
    struct Rhythm
    {
        bool valid = false;
        std::array<float, 16> chords {}, arp {};   // 0 = rest, 0.4 ghost, 0.8 hit, 1 accent
        char name[48] {};
    };
    namespace detail
    {
        struct Shared
        {
            std::atomic<uint32_t> seq, stamp;
            std::atomic<float> chords[16], arp[16];
            std::atomic<uint32_t> name[12];
            // the hook's rests (a beat or longer), in song quarters: where the lead leaves room
            std::atomic<uint32_t> restSeq, restStamp;
            std::atomic<int32_t> restCount;
            std::atomic<float> restStart[256], restEnd[256];
        };
        inline Shared& shared() noexcept
        {
            static Shared* s = []
            {
                auto* m = static_cast<Shared*> (openSharedBlock ("antioch_suite_kitbus_v1", sizeof (Shared)));
                return m != nullptr ? m : new Shared {};
            }();
            return *s;
        }
        inline uint32_t nowSeconds() noexcept
        {
            using namespace std::chrono;
            return (uint32_t) duration_cast<seconds> (steady_clock::now().time_since_epoch()).count() + 1u;
        }
    }
    inline void publish (const Rhythm& r) noexcept
    {
        auto& s = detail::shared();
        s.seq.fetch_add (1);
        for (int i = 0; i < 16; ++i) { s.chords[i].store (r.chords[(size_t) i]); s.arp[i].store (r.arp[(size_t) i]); }
        for (int w = 0; w < 12; ++w)
        {
            uint32_t v = 0;
            for (int k = 0; k < 4; ++k) v |= (uint32_t) (uint8_t) r.name[w * 4 + k] << (8 * k);
            s.name[w].store (v);
        }
        s.stamp.store (detail::nowSeconds());
        s.seq.fetch_add (1);
    }
    inline Rhythm read (int maxAgeSeconds = 30) noexcept
    {
        Rhythm r;
        auto& s = detail::shared();
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            const auto before = s.seq.load();
            if ((before & 1u) != 0) continue;
            const auto stamp = s.stamp.load();
            if (stamp == 0 || detail::nowSeconds() - stamp > (uint32_t) maxAgeSeconds) return {};
            for (int i = 0; i < 16; ++i) { r.chords[(size_t) i] = s.chords[i].load(); r.arp[(size_t) i] = s.arp[i].load(); }
            for (int w = 0; w < 12; ++w)
            {
                const auto v = s.name[w].load();
                for (int k = 0; k < 4; ++k) r.name[w * 4 + k] = (char) ((v >> (8 * k)) & 0xffu);
            }
            r.name[47] = 0;
            if (s.seq.load() != before) continue;
            r.valid = true;
            return r;
        }
        return {};
    }
    inline void withdraw() noexcept { detail::shared().stamp.store (0); detail::shared().restStamp.store (0); }

    /** Correspondence: where the song's hook rests (a beat or more), start and end in quarters. */
    inline void publishRests (const std::vector<std::pair<float, float>>& rests) noexcept
    {
        auto& s = detail::shared();
        s.restSeq.fetch_add (1);
        const auto n = (int) std::min<size_t> (rests.size(), 256);
        for (int i = 0; i < n; ++i) { s.restStart[i].store (rests[(size_t) i].first); s.restEnd[i].store (rests[(size_t) i].second); }
        s.restCount.store (n);
        s.restStamp.store (detail::nowSeconds());
        s.restSeq.fetch_add (1);
    }
    inline std::vector<std::pair<float, float>> rests (int maxAgeSeconds = 30)
    {
        auto& s = detail::shared();
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            const auto before = s.restSeq.load();
            if ((before & 1u) != 0) continue;
            const auto stamp = s.restStamp.load();
            if (stamp == 0 || detail::nowSeconds() - stamp > (uint32_t) maxAgeSeconds) return {};
            std::vector<std::pair<float, float>> out;
            for (int i = 0; i < std::min (256, (int) s.restCount.load()); ++i) out.push_back ({ s.restStart[i].load(), s.restEnd[i].load() });
            if (s.restSeq.load() != before) continue;
            return out;
        }
        return {};
    }
}
