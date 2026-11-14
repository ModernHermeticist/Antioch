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
#include "Roles.h"
#include "MorphBus.h"
#include "Session.h"
#include "Recipes.h"
#include "Watchdog.h"
#include "Worker.h"
#include <array>

namespace suite
{
    enum class SnapshotAction : uint8_t { Capture = 1, Recall = 2, Clear = 3, ApplyVersion = 4 };   // ApplyVersion: "slot" = version kind

    /** Song versions made from the current one (Ouroboros "Versions"). */
    enum SongVersion { radioEdit = 1, extendedMix = 2, clubEdit = 3 };

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

        struct SuiteStatus { uint32_t flags = 0; std::array<float, 4> metrics {}; std::array<float, 10> bands {}; };
        /** Message thread, about once a second. Override to report flags and numbers. */
        virtual SuiteStatus suiteStatus() { return {}; }
        /** Called when the host tells us the track's name (FL: the mixer track). */
        virtual void trackNameChanged (const juce::String&) {}

        /** Role-aware setup: a plugin on a track whose role is known (from its name, or from the
            Prima Materia on the same track) sets itself up for that role, once, and only while
            nobody has touched it (a new instance, not a saved project). Override to do it;
            return true if anything changed. Message thread. */
        virtual bool applyRoleSetup (int /*role*/, const juce::String& /*trackName*/) { return false; }
        bool roleSetupDone = false;
        int roleSetupApplied = -1;   // the role it set up for (-1 = none yet)

        /** True while every parameter is as the plugin started (not loaded from a project). */
        bool untouched() const
        {
            if (loadedVersion >= 0 || freshValues.empty()) return false;
            const auto& params = getParameters();
            if ((size_t) params.size() != freshValues.size()) return false;
            for (int i = 0; i < params.size(); ++i)
                if (std::abs (params[i]->getValue() - freshValues[(size_t) i]) > 1.0e-5f) return false;
            return true;
        }
        /** Remembers the current values as "as it started" (the poller does it on its first tick). */
        void captureFreshValues()
        {
            freshValues.clear();
            for (auto* p : getParameters()) freshValues.push_back (p->getValue());
        }
        /** The role for this track: Prima Materia's on the same track if it has one, else from the name. */
        int roleForTrack() const
        {
            const auto track = getTrackName();
            if (track.isEmpty()) return -1;
            for (const auto& e : Registry::list())
                if (e.plugin == "Prima Materia" && e.track == track && e.has (Status::roleSet) && e.metrics[0] >= 1.0f)
                    return (int) e.metrics[0];
            return roleFromTrackName (track);
        }
        /** A few times a second on the message thread (plugins with section scenes watch your tweaks here). */
        virtual void pollTick() {}

        /** Applies the role setup if it's due (the poller calls it; tests call it directly). */
        bool tryRoleSetup()
        {
            if (roleSetupDone || getTrackName().isEmpty()) return false;
            const auto role = roleForTrack();
            if (role < 0) return false;
            roleSetupDone = true;   // one chance: a plugin someone has set up keeps its settings
            if (! untouched()) return false;
            if (! applyRoleSetup (role, getTrackName())) return false;
            roleSetupApplied = role;
            captureFreshValues();
            return true;
        }

        void updateTrackProperties (const TrackProperties& properties) override
        {
            const auto name = properties.name.value_or (juce::String());
            {
                const juce::ScopedLock sl (nameLock);
                if (name == trackName) return;
                trackName = name;
            }
            trackNameChanged (name);
            for (auto& m : modules) m.proc->updateTrackProperties (properties);
        }

        juce::String getTrackName() const { const juce::ScopedLock sl (nameLock); return trackName; }

        /** Session health: the suite's saved-state version. A project saved by an older version
            loads fine (new options start at their defaults); Ouroboros says so. */
        static constexpr int stateVersion = 11;
        int loadedVersion = -1;   // -1: not loaded from a project; 0: saved before versions were kept
        uint32_t getInstanceId() const noexcept { return instanceId; }

        /** Announces this instance now (the timer does it every second; tests call it directly). */
        void announce()
        {
            const auto status = suiteStatus();
            const auto older = loadedVersion >= 0 && loadedVersion < stateVersion ? Status::olderState : 0u;
            Registry::announce (instanceId, getName(), getTrackName(), status.flags | older, status.metrics, status.bands);
        }

        /** The plugin's own settings (what it saved before snapshots existed). */
        virtual void getPluginState (juce::MemoryBlock&) = 0;
        virtual void setPluginState (const void* data, int sizeInBytes) = 0;

        void getStateInformation (juce::MemoryBlock& dest) final
        {
            juce::MemoryBlock own;
            getPluginState (own);
            juce::XmlElement xml ("AntiochSuiteState");
            xml.setAttribute ("version", stateVersion);
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
            for (auto& m : modules)   // the plugins merged into this one keep their own settings, inside it
            {
                juce::MemoryBlock block;
                m.proc->getStateInformation (block);
                auto* e = xml.createNewChildElement ("Module");
                e->setAttribute ("key", m.key);
                e->addTextElement (block.toBase64Encoding());
            }
            copyXmlToBinary (xml, dest);
        }

        void setStateInformation (const void* data, int sizeInBytes) final
        {
            const auto xml = getXmlFromBinary (data, sizeInBytes);
            if (xml == nullptr || ! xml->hasTagName ("AntiochSuiteState"))
            {
                loadedVersion = 0;
                setPluginState (data, sizeInBytes);   // a project from before snapshots
                return;
            }
            loadedVersion = xml->getIntAttribute ("version", 0);
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
            for (auto* e : xml->getChildWithTagNameIterator ("Module"))
                if (auto* m = module (e->getStringAttribute ("key")))
                {
                    juce::MemoryBlock block;
                    if (block.fromBase64Encoding (e->getAllSubText()))
                        m->setStateInformation (block.getData(), (int) block.getSize());
                }
        }

        //==============================================================================
        // Modules: plugins merged into this one (Lapis in Mentalism, Emerald Tablet in Correspondence,
        // Prima Materia in Quintessence, Azoth in Transmutation). Each runs inside its host: the host's
        // audio passes through it after the host's own processing, its settings are saved inside the
        // host's, and it still tells the suite about itself under its own name.
        struct Module { juce::String key; std::unique_ptr<SuiteProcessor> proc; };
        SuiteProcessor* addModule (const juce::String& key, std::unique_ptr<SuiteProcessor> p)
        {
            auto* raw = p.get();
            raw->setPlayConfigDetails (2, 2, 44100.0, 512);
            modules.push_back ({ key, std::move (p) });
            return raw;
        }
        SuiteProcessor* module (const juce::String& key) const
        {
            for (const auto& m : modules) if (m.key == key) return m.proc.get();
            return nullptr;
        }
        const std::vector<Module>& getModules() const noexcept { return modules; }

        void prepareModules (double sampleRate, int blockSize)
        {
            for (auto& m : modules)
            {
                m.proc->setPlayConfigDetails (2, 2, sampleRate, blockSize);
                m.proc->setNonRealtime (isNonRealtime());
                m.proc->prepareToPlay (sampleRate, blockSize);
            }
            updateModuleLatency();
        }
        void releaseModules() { for (auto& m : modules) m.proc->releaseResources(); }
        /** Runs the modules on the host's main (stereo) channels, in order, after the host's own processing. */
        void processModules (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
        {
            if (modules.empty() || buffer.getNumChannels() < 2) return;
            juce::AudioBuffer<float> main (buffer.getArrayOfWritePointers(), 2, buffer.getNumSamples());
            for (auto& m : modules)
            {
                m.proc->setPlayHead (getPlayHead());
                m.proc->processBlock (main, midi);
            }
        }
        /** Put at the top of the host's processBlock: the modules run when it returns, whichever way it does. */
        struct ModulesAfter
        {
            SuiteProcessor& host; juce::AudioBuffer<float>& buffer; juce::MidiBuffer& midi;
            ~ModulesAfter() { host.processModules (buffer, midi); }
        };
        ModulesAfter modulesAfter (juce::AudioBuffer<float>& b, juce::MidiBuffer& m) { return { *this, b, m }; }
        int modulesLatency() const { int l = 0; for (const auto& m : modules) l += m.proc->getLatencySamples(); return l; }
        void updateModuleLatency() { if (! modules.empty() && getLatencySamples() != modulesLatency()) setLatencySamples (modulesLatency()); }
        void setNonRealtime (bool offline) noexcept override
        {
            juce::AudioProcessor::setNonRealtime (offline);
            for (auto& m : modules) m.proc->setNonRealtime (offline);
        }

        //==============================================================================
        bool hasSnapshot (int slot) const
        {
            const juce::ScopedLock sl (slotLock);
            return juce::isPositiveAndBelow (slot, numSnapshotSlots) && slots[(size_t) slot].getSize() > 0;
        }

        /** The plugin's own state saved in a snapshot slot (empty if none). */
        juce::MemoryBlock snapshotState (int slot) const
        {
            const juce::ScopedLock sl (slotLock);
            return juce::isPositiveAndBelow (slot, numSnapshotSlots) ? slots[(size_t) slot] : juce::MemoryBlock();
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

        /** Skips snapshot commands already sent (Ouroboros' section snapshots, which it sends itself). */
        void ignorePendingSnapshots() { lastSeen = SnapshotBus::latest(); }

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

        /** Whether this plugin is saved in a track's signature chain (Prima Materia keeps its
            own per-role settings instead). */
        virtual bool joinsChains() const { return true; }

        /** Skips recipe messages already sent (Ouroboros, which sent them itself). */
        void ignorePendingRecipes() { lastRecipeSeen = RecipeBus::latest(); }

        /** Recipes: save or load this plugin's settings under a recipe name (message thread). */
        void pollRecipes()
        {
            const auto latest = RecipeBus::latest();
            for (auto seq = lastRecipeSeen + 1; seq <= latest && seq != 0; ++seq)
            {
                RecipeAction action {};
                juce::String name;
                if (! RecipeBus::read (seq, action, name)) continue;
                // A chain ("Chain <role>@<track>"): only the plugins on that track take part, and
                // their files don't depend on the track name, so the chain loads onto any track.
                auto trackKey = getTrackName();
                if (name.startsWith ("Chain ") && name.contains ("@"))
                {
                    const auto track = name.fromLastOccurrenceOf ("@", false, false);
                    if (! joinsChains() || track.isEmpty() || track != getTrackName()) continue;
                    name = name.upToLastOccurrenceOf ("@", false, false);
                    trackKey = {};
                }
                if (action == RecipeAction::Save)
                {
                    // the state is taken here; the file is written on the worker thread (an autosave
                    // makes every plugin write at once: on a slow or synced disk that froze FL)
                    auto own = std::make_shared<juce::MemoryBlock>();
                    getPluginState (*own);
                    const auto file = Recipes::fileFor (name, getName(), trackKey);
                    suite::Worker::post ([file, own, seq]
                    {
                        file.getParentDirectory().createDirectory();
                        if (file.replaceWithData (own->getData(), own->getSize()))
                            RecipeBus::acknowledge (seq);
                    });
                }
                else if (action == RecipeAction::Load)
                {
                    const auto file = Recipes::find (name, getName(), trackKey);
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
        /** The mood of the session being applied (valid inside applySession). */
        const SessionMood& sessionMood() const { return suite::sessionMood (currentMood); }
        int sessionMoodIndex() const { return currentMood; }

        /** Handles new session messages (the timer calls it; tests call it directly). */
        void pollSession()
        {
            const auto latest = SessionBus::latest();
            for (auto seq = lastSessionSeen + 1; seq <= latest && seq != 0; ++seq)
            {
                SessionRequest r;
                if (! SessionBus::read (seq, r) || ! juce::isPositiveAndBelow (r.style, (int) sessionStyles().size()))
                    continue;
                currentMood = r.mood;
                if (applySession (sessionStyles()[(size_t) r.style], r.root, r.scale))
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
            void timerCallback() override { if (owner.morphing) { suite::Watchdog::Scope w (owner.getName(), "morph"); owner.updateMorph(); } else owner.updateMorph(); }
            SuiteProcessor& owner;
        };

        struct Poller : juce::Timer
        {
            explicit Poller (SuiteProcessor& p) : owner (p) {}
            void timerCallback() override
            {
                const auto name = owner.getName();
                if (first) { first = false; if (owner.loadedVersion < 0) owner.captureFreshValues(); }
                if (ticks % 5 == 3) { suite::Watchdog::Scope w (name, "role setup"); owner.tryRoleSetup(); }
                { suite::Watchdog::Scope w (name, "poll tick"); owner.pollTick(); }
                { suite::Watchdog::Scope w (name, "snapshots"); owner.pollSnapshots(); }
                { suite::Watchdog::Scope w (name, "recipes"); owner.pollRecipes(); }   // before sessions: a new song can load your signature, then set its key
                { suite::Watchdog::Scope w (name, "session"); owner.pollSession(); }
                if (++ticks % 5 == 1) { suite::Watchdog::Scope w (name, "suite status"); owner.announce(); }
                owner.updateModuleLatency();
            }
            // each instance starts at a different point of the cycle, so a project's plugins don't all
            // announce (and set themselves up) on the same tick
            int ticks = juce::Random::getSystemRandom().nextInt (5);
            bool first = true;
            SuiteProcessor& owner;
        };

        std::vector<Module> modules;
        std::vector<float> freshValues;
        juce::CriticalSection slotLock, nameLock;
        juce::String trackName;
        const uint32_t instanceId = (uint32_t) juce::Random::getSystemRandom().nextInt (0x7fffffff) + 1u;
        std::array<juce::MemoryBlock, numSnapshotSlots> slots;
        std::array<std::vector<float>, numSnapshotSlots> slotParams;   // normalised parameter values per slot
        std::vector<float> morphBase;   // the user's values while a morph is under way
        bool morphing = false;
        uint32_t lastSeen = SnapshotBus::latest();   // commands sent before this plugin loaded don't apply
        uint32_t lastSessionSeen = SessionBus::latest();
        int currentMood = 0;
        uint32_t lastRecipeSeen = RecipeBus::latest();
        Poller poller { *this };
        Morpher morpher { *this };
    };
}
