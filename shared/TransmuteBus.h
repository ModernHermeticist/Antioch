#pragma once

// Transmutation's shared state: the song-wide choices every instance must agree on (the song's
// spice and seed, set from Ouroboros) and each track's plan, so tracks can answer each other
// (call and response, contagion) and Ouroboros can show the whole plan in one place.
// Header-only; lock-free per slot (a seqlock).

#include "SharedMemory.h"
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <functional>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <vector>

namespace suite::TransmuteBus
{
    constexpr int maxTracks = 48, maxMoves = 48, nameChars = 40;

    /** One planned move, as shared: when (quarters from the song's start), which effect (1-118). */
    struct SharedMove { float startQ = 0.0f, endQ = 0.0f; int effect = 0; };

    struct TrackPlan
    {
        uint32_t id = 0;
        juce::String track;
        int role = 0;
        std::vector<SharedMove> moves;
    };

    namespace detail
    {
        struct Slot
        {
            std::atomic<uint32_t> seq, id, ms;
            std::atomic<int32_t> role, count;
            std::atomic<float> start[maxMoves], end[maxMoves];
            std::atomic<int32_t> effect[maxMoves];
            std::atomic<uint32_t> name[nameChars / 4];
        };
        struct Shared
        {
            std::atomic<float> songSpice;      // 0..2 (1 = as each track sets it)
            std::atomic<uint32_t> songSeed;    // the song's dice (Ouroboros' Re-roll the song)
            std::atomic<int32_t> songSpiceSet; // 0 until Ouroboros has set it
            Slot slots[maxTracks];
        };
        inline Shared& shared() noexcept
        {
            static Shared* s = []
            {
                auto* m = static_cast<Shared*> (openSharedBlock ("antioch_suite_transmute_v1", sizeof (Shared)));
                return m != nullptr ? m : new Shared {};
            }();
            return *s;
        }
        inline uint32_t nowMs() noexcept
        {
            using namespace std::chrono;
            return (uint32_t) duration_cast<milliseconds> (steady_clock::now().time_since_epoch()).count();
        }
    }

    inline float songSpice() noexcept { auto& s = detail::shared(); return s.songSpiceSet.load() != 0 ? s.songSpice.load() : 1.0f; }
    inline void setSongSpice (float v) noexcept { auto& s = detail::shared(); s.songSpice.store (v); s.songSpiceSet.store (1); }
    inline uint32_t songSeed() noexcept { return detail::shared().songSeed.load(); }
    inline void setSongSeed (uint32_t v) noexcept { detail::shared().songSeed.store (v); }

    inline void publish (const TrackPlan& p) noexcept
    {
        auto& s = detail::shared();
        int slot = -1, oldest = 0;
        for (int i = 0; i < maxTracks; ++i)
        {
            if (s.slots[i].id.load() == p.id) { slot = i; break; }
            if (s.slots[i].ms.load() < s.slots[oldest].ms.load()) oldest = i;
        }
        if (slot < 0) slot = oldest;
        auto& d = s.slots[slot];
        d.seq.fetch_add (1);
        d.id.store (p.id);
        d.role.store (p.role);
        const auto n = (int) std::min<size_t> (p.moves.size(), (size_t) maxMoves);
        for (int k = 0; k < n; ++k)
        {
            d.start[k].store (p.moves[(size_t) k].startQ);
            d.end[k].store (p.moves[(size_t) k].endQ);
            d.effect[k].store (p.moves[(size_t) k].effect);
        }
        d.count.store (n);
        const auto utf = p.track.toStdString();
        for (int w = 0; w < nameChars / 4; ++w)
        {
            uint32_t v = 0;
            for (int k = 0; k < 4; ++k)
            {
                const auto idx = (size_t) (w * 4 + k);
                v |= (uint32_t) (uint8_t) (idx < utf.size() ? utf[idx] : 0) << (8 * k);
            }
            d.name[w].store (v);
        }
        d.ms.store (detail::nowMs());
        d.seq.fetch_add (1);
    }
    inline void withdraw (uint32_t id) noexcept
    {
        for (auto& d : detail::shared().slots) if (d.id.load() == id) { d.id.store (0); d.ms.store (0); }
    }
    /** Every track's plan posted in the last 10 s, in track-name order. */
    inline std::vector<TrackPlan> read() noexcept
    {
        std::vector<TrackPlan> out;
        const auto now = detail::nowMs();
        for (auto& d : detail::shared().slots)
            for (int attempt = 0; attempt < 4; ++attempt)
            {
                const auto before = d.seq.load();
                if ((before & 1u) != 0) continue;
                if (d.id.load() == 0 || now - d.ms.load() > 10000u) break;
                TrackPlan p;
                p.id = d.id.load();
                p.role = d.role.load();
                for (int k = 0; k < std::min (d.count.load(), maxMoves); ++k)
                    p.moves.push_back ({ d.start[k].load(), d.end[k].load(), d.effect[k].load() });
                std::string name;
                for (int w = 0; w < nameChars / 4; ++w)
                {
                    const auto v = d.name[w].load();
                    for (int k = 0; k < 4; ++k) if (const auto c = (char) ((v >> (8 * k)) & 0xffu); c != 0) name.push_back (c);
                }
                p.track = juce::String::fromUTF8 (name.c_str());
                if (d.seq.load() != before) continue;
                out.push_back (std::move (p));
                break;
            }
        std::sort (out.begin(), out.end(), [] (const TrackPlan& a, const TrackPlan& b) { return a.track.compareNatural (b.track) < 0; });
        return out;
    }
    /** The plans printed: one MIDI track per FL track, a note per move (its number = the move's
        number, its length the move's), and a marker naming each move ("bar 33: Beat-repeat roll"),
        so the moves can be seen, edited or turned into automation in FL. */
    inline juce::MidiFile planMidi (const std::vector<TrackPlan>& plans, double bpm, const std::function<juce::String (int)>& nameOf)
    {
        constexpr int ppq = 960;
        juce::MidiFile file;
        file.setTicksPerQuarterNote (ppq);
        juce::MidiMessageSequence tempo;
        tempo.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::lround (60000000.0 / juce::jlimit (60.0, 200.0, bpm))), 0.0);
        file.addTrack (tempo);
        for (const auto& p : plans)
        {
            juce::MidiMessageSequence seq;
            seq.addEvent (juce::MidiMessage::textMetaEvent (3, p.track.isNotEmpty() ? p.track : juce::String ("Track")), 0.0);
            for (const auto& m : p.moves)
            {
                if (m.effect < 1 || m.effect > 127) continue;
                const auto on = (double) m.startQ * ppq, off = (double) juce::jmax (m.startQ + 0.05f, m.endQ) * ppq;
                seq.addEvent (juce::MidiMessage::textMetaEvent (6, "bar " + juce::String ((int) std::floor (m.startQ / 4.0f) + 1) + ": " + nameOf (m.effect)), juce::jmax (0.0, on));
                seq.addEvent (juce::MidiMessage::noteOn (1, m.effect, (juce::uint8) 100), juce::jmax (0.0, on));
                seq.addEvent (juce::MidiMessage::noteOff (1, m.effect), off);
            }
            seq.sort();
            seq.updateMatchedPairs();
            file.addTrack (seq);
        }
        return file;
    }

    inline void reset() noexcept   // tests
    {
        auto& s = detail::shared();
        s.songSpice.store (1.0f); s.songSpiceSet.store (0); s.songSeed.store (0);
        for (auto& d : s.slots) { d.id.store (0); d.ms.store (0); d.count.store (0); }
    }
}
