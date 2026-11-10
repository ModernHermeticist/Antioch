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

namespace suite
{
    enum class Fix : uint8_t { None = 0, BreakdownQuieter, BreakdownLouder, BuildRise, IntroQuieter, LaterDropBigger };

    namespace FixBus
    {
        /** Sender: returns the message's sequence number. */
        uint32_t send (Fix fix) noexcept;
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

        FixReceiver (juce::AudioProcessorValueTreeState& state, Planner planner)
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

        /** Helper for planners: parameter `id` moved by `delta` in normalised (0..1) terms, as a plain value. */
        static Change nudge (juce::AudioProcessorValueTreeState& state, const juce::String& id, float delta)
        {
            auto* p = state.getParameter (id);
            jassert (p != nullptr);
            return { id, p->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, p->getValue() + delta)) };
        }

    private:
        void timerCallback() override { poll(); }

        void apply (const FixBus::Message& m)
        {
            const auto changes = plan (m.fix);
            if (changes.empty())
                return;
            saved.clear();
            for (const auto& c : changes)
                if (auto* p = apvts.getParameter (c.paramId))
                {
                    saved.push_back ({ p, p->getValue() });
                    set (*p, p->convertTo0to1 (p->getNormalisableRange().snapToLegalValue (c.plainValue)));
                }
            savedSeq = m.seq;
            ++applied;
            FixBus::acknowledge (m.seq);
        }

        void undo (const FixBus::Message& m)
        {
            if (saved.empty() || (savedSeq & 0xffffu) != (m.target & 0xffffu))
                return;
            for (auto it = saved.rbegin(); it != saved.rend(); ++it)
                set (*it->first, it->second);
            saved.clear();
            FixBus::acknowledge (m.seq);
        }

        static void set (juce::RangedAudioParameter& p, float normalised)
        {
            p.beginChangeGesture();
            p.setValueNotifyingHost (normalised);
            p.endChangeGesture();
        }

        juce::AudioProcessorValueTreeState& apvts;
        Planner plan;
        uint32_t lastSeen = 0, savedSeq = 0;
        std::vector<std::pair<juce::RangedAudioParameter*, float>> saved;
        int applied = 0;
    };
}
