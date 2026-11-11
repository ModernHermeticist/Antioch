#pragma once

// What changed: every automatic move the suite makes, in one place. Each plugin adds a line
// when it changes something by itself (a fix applied or undone, an EQ move kept or undone,
// a balance step), with its track and the bar it happened on; Ouroboros shows the lot,
// newest first, and finds "what happened around bar 64?".
//
// A ring of 256 lines in shared memory (all the suite's plugins in the host see it).

#include "SharedMemory.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

namespace suite::MoveLog
{
    enum Kind { info = 0, kept = 1, undone = 2, fix = 3 };

    struct Entry
    {
        uint32_t seq = 0;
        juce::int64 timeMs = 0;
        int bar = -1, kind = info;
        juce::String plugin, track, text;
    };

    namespace detail
    {
        constexpr int capacity = 256, nameLen = 32, textLen = 224;
        struct Slot
        {
            std::atomic<uint32_t> seq;
            juce::int64 timeMs;
            int32_t bar, kind;
            char plugin[nameLen], track[nameLen], text[textLen];
        };
        struct Shared { std::atomic<uint32_t> next; std::atomic<int32_t> bar; Slot slots[capacity]; };
        inline Shared& shared() noexcept
        {
            static Shared* s = []
            {
                auto* m = static_cast<Shared*> (openSharedBlock ("antioch_suite_movelog_v1", sizeof (Shared)));
                return m != nullptr ? m : new Shared {};
            }();
            return *s;
        }
        inline void copy (char* dest, const juce::String& s, int len) noexcept
        {
            const auto utf8 = s.toRawUTF8();
            auto n = (int) std::strlen (utf8);
            if (n >= len)   // don't cut a UTF-8 character in half
            {
                n = len - 1;
                while (n > 0 && (utf8[n] & 0xc0) == 0x80) --n;
            }
            std::memcpy (dest, utf8, (size_t) n);
            dest[n] = 0;
        }
    }

    /** Where the song is (1-based bar): plugins that see the play head keep it current. */
    inline void setBar (int bar) noexcept { detail::shared().bar.store (bar, std::memory_order_relaxed); }
    inline int currentBar() noexcept { return detail::shared().bar.load (std::memory_order_relaxed); }

    /** Message thread: one line. `bar` -1: the bar the song is at now. */
    inline void add (const juce::String& plugin, const juce::String& track, const juce::String& text, int kind = info, int bar = -1)
    {
        auto& s = detail::shared();
        const auto n = s.next.fetch_add (1) + 1;
        auto& slot = s.slots[(n - 1) % (uint32_t) detail::capacity];
        slot.seq.store (0, std::memory_order_release);   // being written
        slot.timeMs = juce::Time::currentTimeMillis();
        slot.bar = bar >= 0 ? bar : currentBar();
        slot.kind = kind;
        detail::copy (slot.plugin, plugin, detail::nameLen);
        detail::copy (slot.track, track, detail::nameLen);
        detail::copy (slot.text, text, detail::textLen);
        slot.seq.store (n, std::memory_order_release);
    }

    /** Everything still in the ring, newest first. */
    inline std::vector<Entry> read()
    {
        std::vector<Entry> out;
        auto& s = detail::shared();
        for (auto& slot : s.slots)
        {
            const auto before = slot.seq.load (std::memory_order_acquire);
            if (before == 0) continue;
            Entry e;
            e.seq = before;
            e.timeMs = slot.timeMs;
            e.bar = slot.bar;
            e.kind = slot.kind;
            char plugin[detail::nameLen], track[detail::nameLen], text[detail::textLen];
            std::memcpy (plugin, slot.plugin, sizeof (plugin)); plugin[detail::nameLen - 1] = 0;
            std::memcpy (track, slot.track, sizeof (track));    track[detail::nameLen - 1] = 0;
            std::memcpy (text, slot.text, sizeof (text));       text[detail::textLen - 1] = 0;
            if (slot.seq.load (std::memory_order_acquire) != before) continue;   // rewritten while we read it
            e.plugin = juce::String::fromUTF8 (plugin);
            e.track = juce::String::fromUTF8 (track);
            e.text = juce::String::fromUTF8 (text);
            out.push_back (std::move (e));
        }
        std::sort (out.begin(), out.end(), [] (const Entry& a, const Entry& b) { return a.seq > b.seq; });
        return out;
    }

    /** The search: a number finds what happened in the 8 bars up to that bar ("why did it get
        quieter at bar 64?"); words find lines that contain them (plugin, track or text). */
    inline bool matches (const Entry& e, const juce::String& query)
    {
        const auto q = query.trim();
        if (q.isEmpty()) return true;
        const auto digits = q.retainCharacters ("0123456789 ").trim();
        juce::String number;
        for (const auto& t : juce::StringArray::fromTokens (digits, " ", "")) if (t.isNotEmpty()) number = t;
        if (number.isNotEmpty())
        {
            const auto bar = number.getIntValue();
            return e.bar >= bar - 8 && e.bar <= bar;
        }
        const auto all = (e.plugin + " " + e.track + " " + e.text).toLowerCase();
        for (const auto& w : juce::StringArray::fromTokens (q.toLowerCase(), " ", ""))
            if (w.length() > 2 && ! all.contains (w)) return false;
        return true;
    }

    inline void clear() noexcept
    {
        auto& s = detail::shared();
        for (auto& slot : s.slots) slot.seq.store (0);
        s.next.store (0);
    }
}
