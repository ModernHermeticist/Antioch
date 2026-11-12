#pragma once

// Fix-it messages: Mentalism's arrangement coach asks the other plugins to make a
// small, named change (for example "breakdowns 2 dB quieter"). Each plugin that can
// help applies it to its own parameters, so the user sees the knobs move, remembers
// the old values, and acknowledges. "Undo" restores them.
//
// Lock-free ring of 16 messages in shared memory; one sender (Mentalism) at a time.

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
                                    MixItAll, ExportAll };                          // one-click finish (Ouroboros): Mentalism mixes, Correspondence exports

    /** A fix in words (What changed). */
    inline juce::String fixName (Fix f)
    {
        static const char* names[] = { "None", "Breakdowns quieter", "Breakdowns louder", "Build rise", "Intro quieter", "Later drop bigger",
                                       "Rough mix", "DJ intro and outro", "Reference tone", "Masking carve", "Gain staging", "Arc match",
                                       "DJ phrasing", "Section extended", "Session health", "Arrange from loop", "Song draft",
                                       "Learning pass", "Kick fit", "Phase flip", "Finish the song", "Balance step",
                                       "Ride the vocal", "Mono lows", "Darker", "Brighter", "Wider", "More pump", "Section lows down",
                                       "Re-learn changed parts", "Mix it", "Export all" };
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
        void timerCallback() override { poll(); }

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
