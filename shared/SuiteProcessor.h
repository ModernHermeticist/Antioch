#pragma once

// Base class for every plugin in the suite: whole-suite snapshots.
//
// Ouroboros sends "capture slot B" / "recall slot B" on the SnapshotBus. Every suite
// plugin stores its own copy of its settings for each slot inside its own project
// state, so snapshots travel with the FL project and nothing is stored elsewhere.
//
// Plugins implement getPluginState / setPluginState (their normal state code); this
// class wraps that with the slots. Projects saved before snapshots existed load as is.

#include <juce_audio_processors/juce_audio_processors.h>
#include "Registry.h"
#include <array>

namespace suite
{
    enum class SnapshotAction : uint8_t { Capture = 1, Recall = 2, Clear = 3 };

    namespace SnapshotBus
    {
        uint32_t send (SnapshotAction, int slot) noexcept;
        uint32_t latest() noexcept;
        bool read (uint32_t seq, SnapshotAction& action, int& slot) noexcept;
        void acknowledge (uint32_t seq) noexcept;
        int acknowledgements (uint32_t seq) noexcept;   // how many plugins handled it
        void reset() noexcept;   // tests
    }

    class SuiteProcessor : public juce::AudioProcessor
    {
    public:
        static constexpr int numSnapshotSlots = 4;
        static juce::String slotName (int slot) { return juce::String::charToString ((juce::juce_wchar) ('A' + slot)); }

        explicit SuiteProcessor (const BusesProperties& buses) : juce::AudioProcessor (buses) { poller.startTimerHz (5); }
        ~SuiteProcessor() override
        {
            poller.stopTimer();
            Registry::remove (instanceId);
        }

        //==============================================================================
        // Suite registry: what this instance tells the rest of the suite about itself.

        struct SuiteStatus { uint32_t flags = 0; std::array<float, 4> metrics {}; };
        /** Message thread, about once a second. Override to report flags and numbers. */
        virtual SuiteStatus suiteStatus() { return {}; }
        /** Called when the host tells us the track's name (FL: the mixer track). */
        virtual void trackNameChanged (const juce::String&) {}

        void updateTrackProperties (const TrackProperties& properties) override
        {
            const auto name = properties.name.value_or (juce::String());
            {
                const juce::ScopedLock sl (nameLock);
                if (name == trackName) return;
                trackName = name;
            }
            trackNameChanged (name);
        }

        juce::String getTrackName() const { const juce::ScopedLock sl (nameLock); return trackName; }
        uint32_t getInstanceId() const noexcept { return instanceId; }

        /** Announces this instance now (the timer does it every second; tests call it directly). */
        void announce()
        {
            const auto status = suiteStatus();
            Registry::announce (instanceId, getName(), getTrackName(), status.flags, status.metrics);
        }

        /** The plugin's own settings (what it saved before snapshots existed). */
        virtual void getPluginState (juce::MemoryBlock&) = 0;
        virtual void setPluginState (const void* data, int sizeInBytes) = 0;

        void getStateInformation (juce::MemoryBlock& dest) final
        {
            juce::MemoryBlock own;
            getPluginState (own);
            juce::XmlElement xml ("AntiochSuiteState");
            xml.createNewChildElement ("Plugin")->addTextElement (own.toBase64Encoding());
            {
                const juce::ScopedLock sl (slotLock);
                for (int s = 0; s < numSnapshotSlots; ++s)
                    if (slots[(size_t) s].getSize() > 0)
                    {
                        auto* e = xml.createNewChildElement ("Snapshot");
                        e->setAttribute ("slot", s);
                        e->addTextElement (slots[(size_t) s].toBase64Encoding());
                    }
            }
            copyXmlToBinary (xml, dest);
        }

        void setStateInformation (const void* data, int sizeInBytes) final
        {
            const auto xml = getXmlFromBinary (data, sizeInBytes);
            if (xml == nullptr || ! xml->hasTagName ("AntiochSuiteState"))
            {
                setPluginState (data, sizeInBytes);   // a project from before snapshots
                return;
            }
            {
                const juce::ScopedLock sl (slotLock);
                for (auto& s : slots) s.reset();
                for (auto* e : xml->getChildWithTagNameIterator ("Snapshot"))
                {
                    const auto s = e->getIntAttribute ("slot", -1);
                    if (juce::isPositiveAndBelow (s, numSnapshotSlots))
                        slots[(size_t) s].fromBase64Encoding (e->getAllSubText());
                }
            }
            if (auto* own = xml->getChildByName ("Plugin"))
            {
                juce::MemoryBlock block;
                if (block.fromBase64Encoding (own->getAllSubText()))
                    setPluginState (block.getData(), (int) block.getSize());
            }
        }

        //==============================================================================
        bool hasSnapshot (int slot) const
        {
            const juce::ScopedLock sl (slotLock);
            return juce::isPositiveAndBelow (slot, numSnapshotSlots) && slots[(size_t) slot].getSize() > 0;
        }

        void captureSnapshot (int slot)
        {
            if (! juce::isPositiveAndBelow (slot, numSnapshotSlots)) return;
            juce::MemoryBlock own;
            getPluginState (own);
            const juce::ScopedLock sl (slotLock);
            slots[(size_t) slot] = std::move (own);
        }

        bool recallSnapshot (int slot)
        {
            juce::MemoryBlock copy;
            {
                const juce::ScopedLock sl (slotLock);
                if (! juce::isPositiveAndBelow (slot, numSnapshotSlots) || slots[(size_t) slot].getSize() == 0) return false;
                copy = slots[(size_t) slot];
            }
            setPluginState (copy.getData(), (int) copy.getSize());
            return true;
        }

        void clearSnapshot (int slot)
        {
            const juce::ScopedLock sl (slotLock);
            if (juce::isPositiveAndBelow (slot, numSnapshotSlots)) slots[(size_t) slot].reset();
        }

        /** Handles new snapshot commands (message thread; the timer calls it, tests call it directly). */
        void pollSnapshots()
        {
            const auto latest = SnapshotBus::latest();
            for (auto seq = lastSeen + 1; seq <= latest && seq != 0; ++seq)
            {
                SnapshotAction action {};
                int slot = 0;
                if (! SnapshotBus::read (seq, action, slot))
                    continue;
                bool handled = true;
                switch (action)
                {
                    case SnapshotAction::Capture: captureSnapshot (slot); break;
                    case SnapshotAction::Recall:  handled = recallSnapshot (slot); break;
                    case SnapshotAction::Clear:   clearSnapshot (slot); break;
                    default:                      handled = false; break;
                }
                if (handled)
                    SnapshotBus::acknowledge (seq);
            }
            lastSeen = latest;
        }

    private:
        struct Poller : juce::Timer
        {
            explicit Poller (SuiteProcessor& p) : owner (p) {}
            void timerCallback() override
            {
                owner.pollSnapshots();
                if (++ticks % 5 == 1) owner.announce();
            }
            int ticks = 0;
            SuiteProcessor& owner;
        };

        juce::CriticalSection slotLock, nameLock;
        juce::String trackName;
        const uint32_t instanceId = (uint32_t) juce::Random::getSystemRandom().nextInt (0x7fffffff) + 1u;
        std::array<juce::MemoryBlock, numSnapshotSlots> slots;
        uint32_t lastSeen = SnapshotBus::latest();   // commands sent before this plugin loaded don't apply
        Poller poller { *this };
    };
}
