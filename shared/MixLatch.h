#pragma once

// MixLatch: how a plugin's mix fixes switch on and off, in one place for the whole suite.
//  - A fix latches on once Ouroboros' checks ask for it (LabelReadyBus::needed) and it applies to
//    this track; it stays on, saved with the project (a property per feature), while its switch is on.
//  - The self-check: Ouroboros revokes a fix that switched on and didn't help; it goes off for this
//    song (and so does a fix you switch off for one track from Ouroboros' list).
//  - Reset: every fix starts again (undoable).
//  - While FL renders nothing switches (the export matches what you heard).
//  - The whole-mix A/B bypasses every fix; when the CPU is high the least useful ones sleep.
//  - A knob a fix set that you then move is yours: the fix leaves it alone from then on.
//  - Publishes what's on (MixFixBus) about once a second.
// Message thread only (the plugin's pollTick); the DSP reads the plain on/off it hands out.

#include "AutopilotBus.h"
#include "LabelReadyBus.h"
#include "MixFixBus.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>
#include <map>

namespace suite
{
    class MixLatch
    {
    public:
        enum State { idle = 0, latched = 1, offForSong = 2 };
        std::function<void (const juce::String&)> log;   // the plugin's What changed

        explicit MixLatch (juce::AudioProcessorValueTreeState& s) : apvts (s) {}

        /** Called once per tick, before the updates: whether FL is rendering. */
        void beginTick (bool rendering) { frozen = rendering && Autopilot::on (Autopilot::mx2RenderFreezeF); active = {}; count = 0; }

        /** One fix: latch it if it's asked for and applies, and say whether it runs now.
            `lowValue`: one of the first to sleep when the CPU is high. */
        bool update (Autopilot::Feature f, bool applies, bool lowValue = false)
        {
            namespace AP = Autopilot;
            auto st = stateOf (f);
            if (! frozen)
            {
                if (st == idle && applies && AP::on (f) && LabelReadyBus::needed ((int) f) && ! LabelReadyBus::revoked ((int) f))
                {
                    st = latched; setState (f, st); changed();
                    if (log) log ("Mix: " + juce::String (AP::featureName ((int) f)).fromFirstOccurrenceOf ("Mix: ", false, false));
                }
                else if (st == latched && LabelReadyBus::revoked ((int) f) && AP::on (AP::mx2SelfCheckF))
                {
                    st = offForSong; setState (f, st); changed();
                    if (log) log ("Off for this song (no better): " + juce::String (AP::featureName ((int) f)));
                }
            }
            const auto sleeping = AP::on (AP::mx2CpuBudgetF) && AP::cpuSleepLevel() >= (lowValue ? 1 : 4);
            const auto on = st == latched && applies && AP::on (f) && ! AP::mixBypassed() && ! sleeping;
            const auto wasOn = lastOn.count ((int) f) && lastOn[(int) f];
            if (on != wasOn && ! frozen) { lastOn[(int) f] = on; changed(); }
            if (on) { LabelReadyBus::setBit (active, (int) f); ++count; }
            return frozen ? wasOn : on;
        }
        /** Latched (whether or not it runs right now). */
        bool isLatched (Autopilot::Feature f) const { return stateOf (f) == latched; }
        /** Latch it now (tests, and the older per-plugin properties carried over). */
        void force (Autopilot::Feature f) { setState (f, latched); }

        /** Ouroboros' list: one fix off for this song. */
        std::function<void()> switchOff (int feature)
        {
            const auto before = stateOf ((Autopilot::Feature) feature);
            setState ((Autopilot::Feature) feature, offForSong); changed();
            if (log) log ("Off for this song (from Ouroboros): " + juce::String (Autopilot::featureName (feature)));
            return [this, feature, before] { setState ((Autopilot::Feature) feature, before); changed(); };
        }
        /** Every fix starts again (undo puts them back). */
        std::function<void()> resetAll()
        {
            std::map<juce::String, juce::var> before;
            for (int i = 0; i < apvts.state.getNumProperties(); ++i)
            {
                const auto name = apvts.state.getPropertyName (i).toString();
                if (name.startsWith ("mx_") || name.startsWith ("mxOwned_")) before[name] = apvts.state.getProperty (name);
            }
            for (const auto& [k, v] : before) apvts.state.removeProperty (k, nullptr);
            changed();
            if (log) log ("Mix fixes reset for this song");
            return [this, before] { for (const auto& [k, v] : before) apvts.state.setProperty (k, v, nullptr); changed(); };
        }

        /** A knob set by a fix (it remembers the value, to notice you moving it). Returns false if it's yours. */
        bool setParam (const juce::String& id, float plain)
        {
            if (owned (id)) return false;
            if (auto* p = apvts.getParameter (id))
            {
                p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (plain)); p->endChangeGesture();
                setBy[id] = p->convertFrom0to1 (p->getValue());
            }
            return true;
        }
        bool owned (const juce::String& id) const { return (bool) apvts.state.getProperty ("mxOwned_" + id, false); }
        /** Each tick: a knob a fix set and you've moved since is yours from now on. */
        void watchOwnership()
        {
            if (! Autopilot::on (Autopilot::mx2OwnershipF)) return;
            for (auto it = setBy.begin(); it != setBy.end();)
            {
                auto* p = apvts.getParameter (it->first);
                if (p != nullptr && std::abs (p->convertFrom0to1 (p->getValue()) - it->second) > 1.0e-3f * juce::jmax (1.0f, std::abs (it->second)))
                {
                    apvts.state.setProperty ("mxOwned_" + it->first, true, nullptr);
                    if (log) log (p->getName (40) + " is yours now: the mix fixes leave it alone");
                    it = setBy.erase (it);
                }
                else ++it;
            }
        }

        /** About once a second: what's on, published for Ouroboros' list. */
        void publish (uint32_t id, const juce::String& plugin, const juce::String& track, float levelDb = 0.0f, bool busy = false, bool budgetHit = false)
        {
            MixFixBus::Entry e;
            e.id = id; plugin.copyToUTF8 (e.plugin, sizeof (e.plugin) - 1); track.copyToUTF8 (e.track, sizeof (e.track) - 1);
            e.active = active; e.count = count; e.levelDb = levelDb; e.budgetHit = budgetHit;
            e.busyUntilMs = busy ? MixFixBus::nowMs() + 4000u : busyUntil;
            MixFixBus::publish (e);
        }
        int activeCount() const { return count; }
        const LabelReadyBus::Needs& activeMask() const { return active; }
        bool rendering() const { return frozen; }

        int stateOf (Autopilot::Feature f) const { return (int) apvts.state.getProperty (key (f), 0); }
    private:
        juce::AudioProcessorValueTreeState& apvts;
        std::map<int, bool> lastOn;
        std::map<juce::String, float> setBy;
        LabelReadyBus::Needs active {};
        int count = 0;
        bool frozen = false;
        uint32_t busyUntil = 0;
        static juce::Identifier key (Autopilot::Feature f) { return juce::Identifier ("mx_" + juce::String ((int) f)); }
        void setState (Autopilot::Feature f, int st) { if (st == idle) apvts.state.removeProperty (key (f), nullptr); else apvts.state.setProperty (key (f), st, nullptr); }
        void changed() { busyUntil = MixFixBus::nowMs() + 8000u; }   // settling: Ouroboros waits eight seconds
    };
}
