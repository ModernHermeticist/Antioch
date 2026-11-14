#pragma once

// Fix-it messages: Mentalism's arrangement coach asks the other plugins to make a
// small, named change (for example "breakdowns 2 dB quieter"). Each plugin that can
// help applies it to its own parameters, so the user sees the knobs move, remembers
// the old values, and acknowledges. "Undo" restores them.
//
// Lock-free ring of 16 messages in shared memory; one sender (Mentalism) at a time.

#include "Watchdog.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <cstdint>
#include <functional>
#include <vector>
#include "MoveLog.h"
#include "SuiteProcessor.h"

namespace suite
{
    enum class Fix : uint8_t { None = 0, BreakdownQuieter, BreakdownLouder, BuildRise, IntroQuieter, LaterDropBigger, RoughMix, DjIntroOutro, ReferenceTone, Carve, GainStage, ArcMatch, DjPhrasing, ExtendSection, SessionHealth, ArrangeFromLoop, DraftSong,
                                    LearnStart, KickFit, PhaseFlip, FinishSong,     // the learning pass (Ouroboros)
                                    BalanceStep,                                    // Mentalism's balance loop: each track a step towards its balance
                                    VocalRide, MonoLows,                            // the problem watcher: ride a buried vocal; mono lows where they're off
                                    Darker, Brighter, Wider, MorePump,              // moods (Ouroboros): each plugin does its share
                                    SectionLowsDown,                                // the bass down 2 dB in one kind of section (data: the type)
                                    Relearn,                                        // new parts written: these roles measure afresh (data: a bit per role)
                                    MixItAll, ExportAll,                            // one-click finish (Ouroboros): Mentalism mixes, Correspondence exports
                                    MasterTone,                                     // Mentalism shares the master's tone out: one Quintessence, one band, its share
                                    // The problem watcher's fix ladders:
                                    Narrower,                                       // one Quintessence narrower (data: its id's low 12 bits)
                                    LessSquash,                                     // every Quintessence lets the drops breathe: dynamics and glue eased off
                                    ReverbLowCut,                                   // every Antioch's reverb low cut up (mud from the reverb returns)
                                    DriveSofter,                                    // every Vibration's drive down (harshness)
                                    SectionDown,                                    // every track 1 dB down in one kind of section (data: the type)
                                    BuildImpact,
                                    TrackPolarity,                                  // one Quintessence flips its polarity (data: its id's low 12 bits)
                                    SectionTrim,                                    // one Quintessence's level in one kind of section (packSectionTrim)
                                    TrackPunch,                                     // one Quintessence's transient shaper (packMasterTone: its id, band 0, signed dB)
                                    PreDelayTempo,                                  // every Antioch's reverb pre-delay set from the tempo
                                    ThrowsAtVocal,                                  // every Antioch throws at the end of each vocal phrase
                                    VocalDelay,                                     // one Quintessence delays its track (packMasterTone: its id, band 0, ms)
                                    SectionTone,                                    // one Quintessence's tone in one kind of section (packSectionTone)
                                    SectionWidth,                                   // one Quintessence's width in one kind of section (packSectionTrim: amount x10)
                                    EchoTighter,                                    // every Antioch's echo feedback down a quarter (a busy song)
                                    ReverbDuck,                                     // every Antioch's reverb steps back while the lead plays
                                    PhraseTrim,                                     // one Quintessence's level in one 4-bar phrase (packPhraseTrim)
                                    TrackPan,                                       // one Quintessence's pan offset (packMasterTone: its id, band 0, pan x 10)
                                    MixFixOff,                                      // one plugin's mix fix off for this song (packMixFix: its id, the feature)
                                    MixFixReset,                                    // every plugin's mix fixes start again (undoable)
                                    DeltaListen };                                  // one Quintessence plays only what its fixes change (packMixFix: its id, 1 on / 0 off)

    /** Fixes that only move a track's level, mutes or filters: Prima Materia makes those
        suggestions now (it never changes the track), so they're suggested once, not checked or
        retried. (Bigger later drops and build impact also act on Azoth and Transmutation.) */
    inline bool mixerOnly (Fix f) noexcept
    {
        switch (f)
        {
            case Fix::BreakdownQuieter: case Fix::BreakdownLouder: case Fix::IntroQuieter: case Fix::DjIntroOutro:
            case Fix::GainStage: case Fix::RoughMix: case Fix::SectionLowsDown: case Fix::SectionDown: case Fix::BalanceStep:
            case Fix::ArcMatch: case Fix::VocalRide: case Fix::MonoLows:
                return true;
            default:
                return false;
        }
    }                                  // a bigger build-to-drop jump (data 0: Transmutation's drops do more; 1: a gap before the drop)

    /** A fix in words (What changed). */
    inline juce::String fixName (Fix f)
    {
        static const char* names[] = { "None", "Breakdowns quieter", "Breakdowns louder", "Build rise", "Intro quieter", "Later drop bigger",
                                       "Rough mix", "DJ intro and outro", "Reference tone", "Masking carve", "Gain staging", "Arc match",
                                       "DJ phrasing", "Section extended", "Session health", "Arrange from loop", "Song draft",
                                       "Learning pass", "Kick fit", "Phase flip", "Finish the song", "Balance step",
                                       "Ride the vocal", "Mono lows", "Darker", "Brighter", "Wider", "More pump", "Section lows down",
                                       "Re-learn changed parts", "Mix it", "Export all", "Master tone share",
                                       "Narrower", "Drops breathe", "Reverb low cut up", "Drive softer", "Section down", "Build impact",
                                       "Polarity flip", "Section trim", "Punch", "Pre-delay from the tempo", "Throws at vocal ends", "Vocal onto the grid",
                                       "Section tone", "Section width", "Tighter echoes", "Reverb steps back", "Phrase ride", "Pan", "Mix fix off", "Mix fixes reset", "Hear what the fixes remove" };
        const auto i = (int) f;
        return i >= 0 && i < (int) (sizeof (names) / sizeof (names[0])) ? juce::String (names[i]) : juce::String ("Fix");
    }

    /** Carve: which Prima Materia (low 12 bits of its instance id) and which octave band. */
    inline uint32_t packCarve (uint32_t instanceId, int band) noexcept { return ((uint32_t) (band & 0xf) << 12) | (instanceId & 0xfffu); }
    /** Carve that only opens while the vocal (when = 1) or the lead (when = 2) plays: masking resolved automatically. */
    inline uint32_t packCarve (uint32_t instanceId, int band, int when) noexcept { return packCarve (instanceId, band) | ((uint32_t) (when & 0x3) << 16); }
    /** ...and only outside breakdowns (a clash heard in the drops isn't carved where the music thins out). */
    inline uint32_t packCarve (uint32_t instanceId, int band, int when, bool notInBreakdowns) noexcept { return packCarve (instanceId, band, when) | (notInBreakdowns ? (1u << 18) : 0u); }
    inline bool carveNotInBreakdowns (uint32_t data) noexcept { return (data & (1u << 18)) != 0; }

    /** MasterTone: which Quintessence (low 12 bits of its id), which octave band, and how much (signed half dB). */
    inline uint32_t packMasterTone (uint32_t instanceId, int band, float db) noexcept
    {
        return (instanceId & 0xfffu) | ((uint32_t) (band & 0xf) << 12) | ((uint32_t) (uint8_t) (int8_t) juce::jlimit (-127, 127, (int) std::lround (db * 2.0f)) << 16);
    }
    inline void unpackMasterTone (uint32_t data, uint32_t& instanceLow, int& band, float& db) noexcept
    {
        instanceLow = data & 0xfffu;
        band = (int) ((data >> 12) & 0xf);
        db = (float) (int8_t) (uint8_t) ((data >> 16) & 0xff) / 2.0f;
    }

    /** SectionTrim: which Quintessence (low 12 bits of its id), which kind of section, how much (signed half dB). */
    inline uint32_t packSectionTrim (uint32_t instanceId, int type, float db) noexcept
    {
        return (instanceId & 0xfffu) | ((uint32_t) (type & 0xf) << 12) | ((uint32_t) (uint8_t) (int8_t) juce::jlimit (-127, 127, (int) std::lround (db * 2.0f)) << 16);
    }
    inline void unpackSectionTrim (uint32_t data, uint32_t& instanceLow, int& type, float& db) noexcept
    {
        instanceLow = data & 0xfffu;
        type = (int) ((data >> 12) & 0xf);
        db = (float) (int8_t) (uint8_t) ((data >> 16) & 0xff) / 2.0f;
    }

    /** SectionTone: which Quintessence (12 bits), which kind of section (3 bits), which octave band (4 bits), a step in
        signed half dB (5 bits: up to +-7.5 dB) - 24 bits, all a fix message carries. */
    inline uint32_t packSectionTone (uint32_t instanceId, int type, int band, float db) noexcept
    {
        const auto half = juce::jlimit (-15, 15, (int) std::lround (db * 2.0f));
        return (instanceId & 0xfffu) | ((uint32_t) (type & 0x7) << 12) | ((uint32_t) (band & 0xf) << 15) | ((uint32_t) (half & 0x1f) << 19);
    }
    inline void unpackSectionTone (uint32_t data, uint32_t& instanceLow, int& type, int& band, float& db) noexcept
    {
        instanceLow = data & 0xfffu; type = (int) ((data >> 12) & 0x7); band = (int) ((data >> 15) & 0xf);
        auto half = (int) ((data >> 19) & 0x1f);
        if (half >= 16) half -= 32;
        db = (float) half / 2.0f;
    }

    /** PhraseTrim: which Quintessence (12 bits), which 4-bar phrase (6 bits: the first 256 bars), a step in signed
        half dB (6 bits: -16..+15.5 half dB). */
    inline uint32_t packPhraseTrim (uint32_t instanceId, int phrase, float db) noexcept
    {
        const auto half = juce::jlimit (-31, 31, (int) std::lround (db * 2.0f));
        return (instanceId & 0xfffu) | ((uint32_t) (phrase & 0x3f) << 12) | ((uint32_t) (half & 0x3f) << 18);
    }
    inline void unpackPhraseTrim (uint32_t data, uint32_t& instanceLow, int& phrase, float& db) noexcept
    {
        instanceLow = data & 0xfffu; phrase = (int) ((data >> 12) & 0x3f);
        auto half = (int) ((data >> 18) & 0x3f);
        if (half >= 32) half -= 64;
        db = (float) half / 2.0f;
    }

    /** MixFixOff / DeltaListen: which plugin (the low 12 bits of its id) and a feature (or 1/0), 12 bits. */
    inline uint32_t packMixFix (uint32_t instanceId, int value) noexcept { return (instanceId & 0xfffu) | ((uint32_t) (value & 0xfff) << 12); }
    inline void unpackMixFix (uint32_t data, uint32_t& instanceLow, int& value) noexcept { instanceLow = data & 0xfffu; value = (int) ((data >> 12) & 0xfff); }

    /** ReferenceTone carries two signed amounts in quarter dB (low shelf, high shelf). */
    inline uint32_t packTone (float lowDb, float highDb) noexcept
    {
        auto q = [] (float db) { return (uint32_t) (uint8_t) (int8_t) juce::jlimit (-127, 127, (int) std::lround (db * 4.0f)); };
        return (q (lowDb) << 8) | q (highDb);
    }
    inline void unpackTone (uint32_t arg, float& lowDb, float& highDb) noexcept
    {
        lowDb = (float) (int8_t) (uint8_t) ((arg >> 8) & 0xff) / 4.0f;
        highDb = (float) (int8_t) (uint8_t) (arg & 0xff) / 4.0f;
    }

    /** ArcMatch: a section of the map (by index) and a level change in half dB. */
    inline uint32_t packArc (int sectionIndex, float db) noexcept
    {
        return ((uint32_t) (sectionIndex & 0xff) << 8) | (uint32_t) (uint8_t) (int8_t) juce::jlimit (-127, 127, (int) std::lround (db * 2.0f));
    }
    inline void unpackArc (uint32_t arg, int& sectionIndex, float& db) noexcept
    {
        sectionIndex = (int) ((arg >> 8) & 0xff);
        db = (float) (int8_t) (uint8_t) (arg & 0xff) / 2.0f;
    }

    namespace FixBus
    {
        /** Sender: returns the message's sequence number. */
        uint32_t send (Fix fix) noexcept;
        uint32_t send (Fix fix, uint32_t arg) noexcept;   // with 16 bits of data (e.g. packTone)
        uint32_t sendUndo (uint32_t fixSeq) noexcept;
        /** How many plugins applied (or undid) message `seq`. */
        int acknowledgements (uint32_t seq) noexcept;

        struct Message { uint32_t seq = 0; bool undo = false; Fix fix = Fix::None; uint32_t target = 0; };
        uint32_t latest() noexcept;
        /** Reads message `seq`, or returns false if it has been overwritten. */
        bool read (uint32_t seq, Message& out) noexcept;
        void acknowledge (uint32_t seq) noexcept;
        void reset() noexcept;   // tests
    }

    /** A plugin's side: polls the bus on the message thread and applies the fixes it knows. */
    class FixReceiver : private juce::Timer
    {
    public:
        struct Change { juce::String paramId; float plainValue; };
        using Planner = std::function<std::vector<Change> (Fix)>;
        using DataPlanner = std::function<std::vector<Change> (Fix, uint32_t data)>;

        FixReceiver (juce::AudioProcessorValueTreeState& state, Planner planner)
            : FixReceiver (state, DataPlanner ([p = std::move (planner)] (Fix f, uint32_t) { return p (f); })) {}

        FixReceiver (juce::AudioProcessorValueTreeState& state, DataPlanner planner)
            : apvts (state), plan (std::move (planner)), lastSeen (FixBus::latest())
        {
            startTimerHz (5);
        }

        ~FixReceiver() override { stopTimer(); }

        /** Handles any new messages (called by the timer; tests call it directly). */
        void poll()
        {
            const auto latest = FixBus::latest();
            for (auto seq = lastSeen + 1; seq <= latest && seq != 0; ++seq)
            {
                FixBus::Message m;
                if (! FixBus::read (seq, m))
                    continue;
                if (m.undo) undo (m);
                else apply (m);
            }
            lastSeen = latest;
        }

        int appliedCount() const noexcept { return applied; }

        /** For fixes that aren't parameter moves: apply it and return how to undo it (or an
            empty function if this plugin has nothing to do). Tried when the planner returns nothing. */
        using Custom = std::function<std::function<void()> (Fix, uint32_t data)>;
        void setCustom (Custom c) { custom = std::move (c); }

        /** Helper for planners: parameter `id` moved by `delta` in normalised (0..1) terms, as a plain value. */
        static Change nudge (juce::AudioProcessorValueTreeState& state, const juce::String& id, float delta)
        {
            auto* p = state.getParameter (id);
            jassert (p != nullptr);
            return { id, p->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, p->getValue() + delta)) };
        }

    private:
        void timerCallback() override { suite::Watchdog::Scope w ("Fix receiver", "fixes"); poll(); }

        // Each applied fix remembers, per parameter, the value before and after (last 16 fixes).
        struct Saved { juce::RangedAudioParameter* param; float before, after; };
        struct Entry { uint32_t seq; std::vector<Saved> changes; std::function<void()> customUndo; };

        void apply (const FixBus::Message& m)
        {
            const auto changes = plan (m.fix, m.target);
            if (changes.empty())
            {
                if (custom == nullptr) return;
                auto undoIt = custom (m.fix, m.target);
                if (undoIt == nullptr) return;
                history.push_back ({ m.seq, {}, std::move (undoIt) });
                if (history.size() > 16) history.erase (history.begin());
                ++applied;
                FixBus::acknowledge (m.seq);
                return;
            }
            Entry entry { m.seq, {}, {} };
            juce::StringArray moved;
            for (const auto& c : changes)
                if (auto* p = apvts.getParameter (c.paramId))
                {
                    const auto before = p->getValue();
                    set (*p, p->convertTo0to1 (p->getNormalisableRange().snapToLegalValue (c.plainValue)));
                    entry.changes.push_back ({ p, before, p->getValue() });
                    if (std::abs (p->getValue() - before) > 1.0e-5f)
                        moved.add (p->getName (40) + " " + p->getText (before, 40) + " -> " + p->getCurrentValueAsText());
                }
            if (! moved.isEmpty()) log (fixName (m.fix) + ": " + moved.joinIntoString (", "), MoveLog::fix);
            history.push_back (std::move (entry));
            if (history.size() > 16) history.erase (history.begin());
            ++applied;
            FixBus::acknowledge (m.seq);
        }

        /** Undo any remembered fix. A parameter is only put back if nothing has moved it since
            the fix (otherwise that later change wins). */
        void undo (const FixBus::Message& m)
        {
            for (auto it = history.begin(); it != history.end(); ++it)
                if ((it->seq & 0xffffffu) == (m.target & 0xffffffu))
                {
                    if (it->customUndo != nullptr) it->customUndo();
                    juce::StringArray moved;
                    for (auto c = it->changes.rbegin(); c != it->changes.rend(); ++c)
                        if (std::abs (c->param->getValue() - c->after) < 1.0e-4f)
                        {
                            set (*c->param, c->before);
                            if (std::abs (c->after - c->before) > 1.0e-5f) moved.add (c->param->getName (40) + " back to " + c->param->getCurrentValueAsText());
                        }
                    if (! moved.isEmpty()) log ("Undone: " + moved.joinIntoString (", "), MoveLog::undone);
                    history.erase (it);
                    FixBus::acknowledge (m.seq);
                    return;
                }
        }

        void log (const juce::String& text, int kind)
        {
            auto& proc = apvts.processor;
            const auto* sp = dynamic_cast<const SuiteProcessor*> (&proc);
            MoveLog::add (proc.getName(), sp != nullptr ? sp->getTrackName() : juce::String(), text, kind);
        }

        static void set (juce::RangedAudioParameter& p, float normalised)
        {
            p.beginChangeGesture();
            p.setValueNotifyingHost (normalised);
            p.endChangeGesture();
        }

        juce::AudioProcessorValueTreeState& apvts;
        DataPlanner plan;
        Custom custom;
        uint32_t lastSeen = 0;
        std::vector<Entry> history;
        int applied = 0;
    };
}
