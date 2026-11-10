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
#include "MorphBus.h"
#include "Session.h"
#include "Recipes.h"
#include <array>

namespace suite
{
    enum class SnapshotAction : uint8_t { Capture = 1, Recall = 2, Clear = 3, ApplyVersion = 4 };   // ApplyVersion: "slot" = version kind

    /** Song versions made from the current one (Ouroboros "Versions"). */
    enum SongVersion { radioEdit = 1, extendedMix = 2 };

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

        explicit SuiteProcessor (const BusesProperties& buses) : juce::AudioProcessor (buses)
        {
            poller.startTimerHz (5);
            morpher.startTimerHz (30);
        }
        ~SuiteProcessor() override
        {
            poller.stopTimer();
            morpher.stopTimer();
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
                        juce::StringArray values;
                        for (auto v : slotParams[(size_t) s]) values.add (juce::String (v, 6));
                        e->setAttribute ("params", values.joinIntoString (","));
                        e->addTextElement (slots[(size_t) s].toBase64Encoding());
                    }
            }
            if (morphing)   // saved mid-morph: remember the user's own values, not the morphed ones
            {
                juce::StringArray values;
                for (auto v : morphBase) values.add (juce::String (v, 6));
                xml.createNewChildElement ("MorphBase")->setAttribute ("params", values.joinIntoString (","));
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
                for (auto& p : slotParams) p.clear();
                for (auto* e : xml->getChildWithTagNameIterator ("Snapshot"))
                {
                    const auto s = e->getIntAttribute ("slot", -1);
                    if (juce::isPositiveAndBelow (s, numSnapshotSlots))
                    {
                        slots[(size_t) s].fromBase64Encoding (e->getAllSubText());
                        for (const auto& v : juce::StringArray::fromTokens (e->getStringAttribute ("params"), ",", ""))
                            if (v.isNotEmpty()) slotParams[(size_t) s].push_back (v.getFloatValue());
                    }
                }
            }
            if (auto* own = xml->getChildByName ("Plugin"))
            {
                juce::MemoryBlock block;
                if (block.fromBase64Encoding (own->getAllSubText()))
                    setPluginState (block.getData(), (int) block.getSize());
            }
            if (auto* base = xml->getChildByName ("MorphBase"))
            {
                const auto values = juce::StringArray::fromTokens (base->getStringAttribute ("params"), ",", "");
                const auto& params = getParameters();
                if (values.size() == params.size())
                    for (int i = 0; i < params.size(); ++i)
                        params[i]->setValueNotifyingHost (values[i].getFloatValue());
            }
            morphing = false;
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
            std::vector<float> values;
            const auto& params = getParameters();
            for (int i = 0; i < params.size(); ++i)   // while morphing, the user's own values are the ones to keep
                values.push_back (morphing && (size_t) i < morphBase.size() ? morphBase[(size_t) i] : params[i]->getValue());
            const juce::ScopedLock sl (slotLock);
            slots[(size_t) slot] = std::move (own);
            slotParams[(size_t) slot] = std::move (values);
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
            if (juce::isPositiveAndBelow (slot, numSnapshotSlots)) { slots[(size_t) slot].reset(); slotParams[(size_t) slot].clear(); }
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
                    case SnapshotAction::ApplyVersion: handled = applyVersion (slot); break;
                    default:                      handled = false; break;
                }
                if (handled)
                    SnapshotBus::acknowledge (seq);
            }
            lastSeen = latest;
        }

        //==============================================================================
        // Session presets (Ouroboros' "New song"): each plugin applies its part.

        /** Override to turn the current settings into a song version (radioEdit / extendedMix),
            e.g. a shorter map or a streaming loudness target. Return true if anything changed. */
        virtual bool applyVersion (int /*kind*/) { return false; }

        /** Recipes: save or load this plugin's settings under a recipe name (message thread). */
        void pollRecipes()
        {
            const auto latest = RecipeBus::latest();
            for (auto seq = lastRecipeSeen + 1; seq <= latest && seq != 0; ++seq)
            {
                RecipeAction action {};
                juce::String name;
                if (! RecipeBus::read (seq, action, name)) continue;
                if (action == RecipeAction::Save)
                {
                    juce::MemoryBlock own;
                    getPluginState (own);
                    const auto file = Recipes::fileFor (name, getName(), getTrackName());
                    file.getParentDirectory().createDirectory();
                    if (file.replaceWithData (own.getData(), own.getSize()))
                        RecipeBus::acknowledge (seq);
                }
                else if (action == RecipeAction::Load)
                {
                    const auto file = Recipes::find (name, getName(), getTrackName());
                    juce::MemoryBlock data;
                    if (file.existsAsFile() && file.loadFileAsData (data) && data.getSize() > 0)
                    {
                        setPluginState (data.getData(), (int) data.getSize());
                        RecipeBus::acknowledge (seq);
                    }
                }
            }
            lastRecipeSeen = latest;
        }

        /** Override to apply a session style (message thread). Return true if anything changed. */
        virtual bool applySession (const SessionStyle&, int /*root*/, int /*scale*/) { return false; }

        /** Handles new session messages (the timer calls it; tests call it directly). */
        void pollSession()
        {
            const auto latest = SessionBus::latest();
            for (auto seq = lastSessionSeen + 1; seq <= latest && seq != 0; ++seq)
            {
                SessionRequest r;
                if (SessionBus::read (seq, r) && juce::isPositiveAndBelow (r.style, (int) sessionStyles().size())
                    && applySession (sessionStyles()[(size_t) r.style], r.root, r.scale))
                    SessionBus::acknowledge (seq);
            }
            lastSessionSeen = latest;
        }

        /** Helper for overrides: set a parameter by its plain value, as if the user moved it. */
        void setParameterPlain (juce::AudioProcessorValueTreeState& state, const juce::String& id, float plainValue)
        {
            if (auto* p = state.getParameter (id))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
                p->endChangeGesture();
            }
        }

        //==============================================================================
        // Snapshot morph (Ouroboros' "Final drop snapshot"): glide every parameter from the
        // user's own values towards the snapshot as the morph amount rises, and put them back
        // when it returns to 0. Choices and switches change at the drop (amount 1).

        /** The sender (Ouroboros) doesn't morph itself. */
        virtual bool morphsWithSnapshots() const { return true; }
        bool isMorphing() const noexcept { return morphing; }

        /** Message thread, 30 times a second (tests call it directly). */
        void updateMorph()
        {
            if (! morphsWithSnapshots()) return;
            const auto m = MorphBus::read();
            const auto& params = getParameters();
            std::vector<float> target;
            {
                const juce::ScopedLock sl (slotLock);
                if (m.valid && juce::isPositiveAndBelow (m.slot, numSnapshotSlots))
                    target = slotParams[(size_t) m.slot];
            }
            const auto amount = target.size() == (size_t) params.size() ? m.amount : 0.0f;

            if (amount <= 0.0f)
            {
                if (morphing)
                {
                    for (int i = 0; i < params.size() && (size_t) i < morphBase.size(); ++i)
                        setIfDifferent (*params[i], morphBase[(size_t) i]);
                    morphing = false;
                }
                return;
            }
            if (! morphing)
            {
                morphBase.clear();
                for (auto* p : params) morphBase.push_back (p->getValue());
                morphing = true;
            }
            for (int i = 0; i < params.size(); ++i)
            {
                auto* p = params[i];
                const auto from = morphBase[(size_t) i], to = target[(size_t) i];
                const auto value = p->isDiscrete() || p->isBoolean() ? (amount >= 0.999f ? to : from) : from + (to - from) * amount;
                setIfDifferent (*p, value);
            }
        }

    private:
        static void setIfDifferent (juce::AudioProcessorParameter& p, float value)
        {
            if (std::abs (p.getValue() - value) > 1.0e-4f)
                p.setValueNotifyingHost (value);
        }

        struct Morpher : juce::Timer
        {
            explicit Morpher (SuiteProcessor& p) : owner (p) {}
            void timerCallback() override { owner.updateMorph(); }
            SuiteProcessor& owner;
        };

        struct Poller : juce::Timer
        {
            explicit Poller (SuiteProcessor& p) : owner (p) {}
            void timerCallback() override
            {
                owner.pollSnapshots();
                owner.pollSession();
                owner.pollRecipes();
                if (++ticks % 5 == 1) owner.announce();
            }
            int ticks = 0;
            SuiteProcessor& owner;
        };

        juce::CriticalSection slotLock, nameLock;
        juce::String trackName;
        const uint32_t instanceId = (uint32_t) juce::Random::getSystemRandom().nextInt (0x7fffffff) + 1u;
        std::array<juce::MemoryBlock, numSnapshotSlots> slots;
        std::array<std::vector<float>, numSnapshotSlots> slotParams;   // normalised parameter values per slot
        std::vector<float> morphBase;   // the user's values while a morph is under way
        bool morphing = false;
        uint32_t lastSeen = SnapshotBus::latest();   // commands sent before this plugin loaded don't apply
        uint32_t lastSessionSeen = SessionBus::latest();
        uint32_t lastRecipeSeen = RecipeBus::latest();
        Poller poller { *this };
        Morpher morpher { *this };
    };
}
