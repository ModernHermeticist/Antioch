#pragma once

// Writing and mixing feeding each other:
//  - The vocal's notes: Solve et Coagula publishes what the vocal sang (pitch, start, length in
//    song quarters), so Correspondence can write lines that don't rub against its held notes.
//  - Thin sections: Mentalism publishes which kinds of section sound thin in the mids, so
//    Correspondence can offer a layer for them.
// Shared memory; values older than 30 s count as gone.

#include "SharedMemory.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <vector>

namespace suite::WritingBus
{
    struct SungNote { int pitch = 60; double start = 0.0, length = 0.25; };

    namespace detail
    {
        constexpr int maxNotes = 1024;
        struct Note { int32_t pitch; float start, length; };
        struct Shared
        {
            std::atomic<uint32_t> notesStamp, notesSeq, thinStamp, thinMask;
            std::atomic<int32_t> noteCount;
            Note notes[maxNotes];
        };
        inline Shared& shared() noexcept
        {
            static Shared* s = []
            {
                auto* m = static_cast<Shared*> (openSharedBlock ("antioch_suite_writingbus_v1", sizeof (Shared)));
                return m != nullptr ? m : new Shared {};
            }();
            return *s;
        }
        inline uint32_t nowSeconds() noexcept
        {
            using namespace std::chrono;
            return (uint32_t) duration_cast<seconds> (steady_clock::now().time_since_epoch()).count() + 1;
        }
    }

    /** Message thread (Solve et Coagula): the vocal's notes so far. */
    inline void publishSung (const std::vector<SungNote>& notes) noexcept
    {
        auto& s = detail::shared();
        const auto n = (int) std::min (notes.size(), (size_t) detail::maxNotes);
        s.noteCount.store (0);
        for (int i = 0; i < n; ++i) s.notes[i] = { notes[(size_t) i].pitch, (float) notes[(size_t) i].start, (float) notes[(size_t) i].length };
        s.noteCount.store (n);
        s.notesSeq.fetch_add (1);
        s.notesStamp.store (detail::nowSeconds());
    }
    inline std::vector<SungNote> sung()
    {
        std::vector<SungNote> out;
        auto& s = detail::shared();
        const auto stamp = s.notesStamp.load();
        if (stamp == 0 || detail::nowSeconds() - stamp > 30) return out;
        const auto n = std::min ((int) s.noteCount.load(), detail::maxNotes);
        for (int i = 0; i < n; ++i) out.push_back ({ s.notes[i].pitch, (double) s.notes[i].start, (double) s.notes[i].length });
        return out;
    }
    inline void clearSung() noexcept { detail::shared().notesStamp.store (0); detail::shared().noteCount.store (0); }

    /** Mentalism: which kinds of section (bit = SectionType) sound thin in the mids. */
    inline void publishThin (uint32_t mask) noexcept
    {
        detail::shared().thinMask.store (mask);
        detail::shared().thinStamp.store (detail::nowSeconds());
    }
    inline uint32_t thin() noexcept
    {
        auto& s = detail::shared();
        const auto stamp = s.thinStamp.load();
        return stamp == 0 || detail::nowSeconds() - stamp > 30 ? 0u : s.thinMask.load();
    }
}
