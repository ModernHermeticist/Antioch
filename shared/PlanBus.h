#pragma once

// The mixer plan: every Prima Materia's suggested moves (it never changes its track), shared so
// Ouroboros can show them all in one table and export every track's fader moves in one MIDI file.
// Header-only; lock-free per slot (a seqlock), safe on any thread.

#include "SharedMemory.h"
#include "SectionBus.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <vector>

namespace suite::PlanBus
{
    constexpr int maxPlans = 64, numTypes = (int) SectionType::count, nameChars = 40;
    constexpr float offDb = -60.0f;   // at or below: muted

    /** One track's plan. Levels in dB relative to its fader (offDb = mute); cuts in Hz (0 = none). */
    struct Plan
    {
        uint32_t id = 0;
        juce::String track;
        int role = 0;                                  // Prima Materia's role (1 kick ... 7 FX)
        std::array<float, numTypes> levelDb {};
        std::array<float, numTypes> lowCutHz {}, highCutHz {};
        float laterDropDb = 0.0f;                      // drop 2 and later, on top (0 = same)
        float trimDb = 0.0f, gainDb = 0.0f;            // the suggested fader offset and channel gain
        double gapQuarters = 0.0;                      // muted for this long before each drop
        bool ramp = false;                             // rises through builds to the drop's level
    };

    namespace detail
    {
        struct Slot
        {
            std::atomic<uint32_t> seq, id, ms;
            std::atomic<int32_t> role, flags;
            std::atomic<float> level[numTypes], lowCut[numTypes], highCut[numTypes];
            std::atomic<float> later, trim, gain, gapQ;
            std::atomic<uint32_t> name[nameChars / 4];
        };
        struct Shared { Slot slots[maxPlans]; };
        inline Shared& shared() noexcept
        {
            static Shared* s = []
            {
                auto* m = static_cast<Shared*> (openSharedBlock ("antioch_suite_planbus_v1", sizeof (Shared)));
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

    /** Prima Materia (message thread, a few times a second): this track's plan. */
    inline void publish (const Plan& p) noexcept
    {
        auto& s = detail::shared();
        int slot = -1, oldest = 0;
        for (int i = 0; i < maxPlans; ++i)
        {
            if (s.slots[i].id.load() == p.id) { slot = i; break; }
            if (s.slots[i].ms.load() < s.slots[oldest].ms.load()) oldest = i;
        }
        if (slot < 0) slot = oldest;
        auto& d = s.slots[slot];
        d.seq.fetch_add (1);   // odd: being written
        d.id.store (p.id);
        d.role.store (p.role);
        d.flags.store (p.ramp ? 1 : 0);
        for (int t = 0; t < numTypes; ++t) { d.level[t].store (p.levelDb[(size_t) t]); d.lowCut[t].store (p.lowCutHz[(size_t) t]); d.highCut[t].store (p.highCutHz[(size_t) t]); }
        d.later.store (p.laterDropDb); d.trim.store (p.trimDb); d.gain.store (p.gainDb); d.gapQ.store ((float) p.gapQuarters);
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
        auto& s = detail::shared();
        for (auto& d : s.slots) if (d.id.load() == id) { d.id.store (0); d.ms.store (0); }
    }
    /** Every plan posted in the last 10 s, in track-name order. */
    inline std::vector<Plan> read() noexcept
    {
        std::vector<Plan> out;
        auto& s = detail::shared();
        const auto now = detail::nowMs();
        for (auto& d : s.slots)
        {
            for (int attempt = 0; attempt < 4; ++attempt)
            {
                const auto before = d.seq.load();
                if ((before & 1u) != 0) continue;
                if (d.id.load() == 0 || now - d.ms.load() > 10000u) break;
                Plan p;
                p.id = d.id.load();
                p.role = d.role.load();
                p.ramp = (d.flags.load() & 1) != 0;
                for (int t = 0; t < numTypes; ++t) { p.levelDb[(size_t) t] = d.level[t].load(); p.lowCutHz[(size_t) t] = d.lowCut[t].load(); p.highCutHz[(size_t) t] = d.highCut[t].load(); }
                p.laterDropDb = d.later.load(); p.trimDb = d.trim.load(); p.gainDb = d.gain.load(); p.gapQuarters = d.gapQ.load();
                std::string name;
                for (int w = 0; w < nameChars / 4; ++w)
                {
                    const auto v = d.name[w].load();
                    for (int k = 0; k < 4; ++k) if (const auto c = (char) ((v >> (8 * k)) & 0xffu); c != 0) name.push_back (c);
                }
                p.track = juce::String::fromUTF8 (name.c_str());
                if (d.seq.load() != before) continue;
                out.push_back (p);
                break;
            }
        }
        std::sort (out.begin(), out.end(), [] (const Plan& a, const Plan& b) { return a.track.compareNatural (b.track) < 0; });
        return out;
    }
    inline void reset() noexcept { for (auto& d : detail::shared().slots) { d.id.store (0); d.ms.store (0); } }   // tests

    /** The CC 7 value for a fader change of `db` (100 = unchanged, 0 = muted). */
    inline int faderCc (float db) noexcept
    {
        return db <= offDb + 0.5f ? 0 : juce::jlimit (1, 127, juce::roundToInt (100.0f * std::pow (10.0f, db / 40.0f)));
    }

    /** One track's fader moves over the song, as CC 7 on channel 1 (960 ticks a quarter). */
    inline juce::MidiMessageSequence faderMoves (const Plan& p, const SectionMap& map)
    {
        using T = SectionType;
        juce::MidiMessageSequence seq;
        constexpr double ppq = 960.0, barQ = 4.0;
        auto cc = [&] (double q, int value) { seq.addEvent (juce::MidiMessage::controllerEvent (1, 7, juce::jlimit (0, 127, value)), q * ppq); };
        const auto dropDb = p.levelDb[(size_t) T::Drop];
        int drops = 0;
        for (int i = 0; i < map.count; ++i)
        {
            const auto type = map.sections[(size_t) i].type;
            const auto startQ = (map.sections[(size_t) i].startBar - 1) * barQ, endQ = (map.endBar (i) - 1) * barQ;
            auto level = p.levelDb[(size_t) type];
            if (type == T::Drop && drops++ > 0 && level > offDb) level += p.laterDropDb;
            if (type == T::Build && p.ramp && level > offDb && dropDb > offDb)
                for (double q = startQ; q < endQ - 1.0e-9; q += 1.0)   // a step a beat, rising to the drop's level
                    cc (q, faderCc (level + (dropDb - level) * (float) ((q - startQ) / std::max (1.0, endQ - startQ))));
            else
                cc (startQ, faderCc (level));
            if (p.gapQuarters > 0.0 && i + 1 < map.count && map.sections[(size_t) i + 1].type == T::Drop)
                cc (endQ - p.gapQuarters, 0);
        }
        seq.updateMatchedPairs();
        return seq;
    }

    /** Every track's fader moves in one MIDI file: one MIDI track each, named after the FL track. */
    inline juce::MidiFile allFaderMoves (const std::vector<Plan>& plans, const SectionMap& map, double bpm)
    {
        juce::MidiFile file;
        file.setTicksPerQuarterNote (960);
        juce::MidiMessageSequence tempo;
        tempo.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::lround (60000000.0 / std::clamp (bpm, 60.0, 200.0))), 0.0);
        file.addTrack (tempo);
        for (const auto& p : plans)
        {
            auto seq = faderMoves (p, map);
            seq.addEvent (juce::MidiMessage::textMetaEvent (3, p.track.isNotEmpty() ? p.track : juce::String ("Track")), 0.0);
            seq.updateMatchedPairs();
            file.addTrack (seq);
        }
        return file;
    }
}
