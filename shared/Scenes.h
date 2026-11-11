#pragma once

// Section scenes: per-section-type offsets for a handful of a plugin's parameters.
//
// The user's knobs stay where they set them (the "base"). While the song is in a
// section (from Ouroboros), the engine adds that section type's offset to the base,
// gliding between sections over the Morph time. Offsets live in normalised
// parameter space (-1..+1) so they work for any range. The value actually in use
// is published for the UI's ghost rings.
//
// Timing: the section is evaluated once per audio block and the glide starts in
// the block that crosses the boundary, so changes land within one block (~10 ms).

#include <juce_audio_processors/juce_audio_processors.h>
#include "HostInfo.h"
#include "SectionBus.h"

namespace suite
{
    constexpr int numSectionTypes = (int) SectionType::count;
    constexpr int laterDropSlot = numSectionTypes;       // extra scene for the 2nd, 3rd... drop (adds to the Drop scene)
    constexpr int numSceneSlots = numSectionTypes + 1;

    class SceneEngine
    {
    public:
        static constexpr int maxParams = 12;
        static inline const juce::String enabledId { "scenes_on" };
        static inline const juce::String morphId { "scene_morph" };

        /** A suggested offset. Use SectionType::count as the type for the "Drop 2+" scene. */
        struct Default { SectionType type; int param; float offset; };

        /** Adds the shared "Scenes on" and "Morph" parameters to a plugin's layout. */
        static void addParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
        {
            layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { enabledId, 1 }, "Section Scenes", true));
            layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { morphId, 1 }, "Scene Morph",
                                                                      juce::StringArray { "Instant", "1/4 beat", "1/2 beat", "1 beat", "2 beats", "1 bar" }, 3));
        }

        static double morphQuarters (int index) { static const double q[] = { 0.0, 0.25, 0.5, 1.0, 2.0, 4.0 }; return q[juce::jlimit (0, 5, index)]; }

        /** Message thread, after the APVTS exists: choose which parameters scenes can move. */
        void setup (juce::AudioProcessorValueTreeState& state, std::vector<std::pair<juce::String, juce::String>> idsAndLabels,
                    std::vector<Default> suggested)
        {
            apvts = &state;
            count = juce::jmin ((int) idsAndLabels.size(), maxParams);
            for (int i = 0; i < count; ++i)
            {
                ids[(size_t) i] = idsAndLabels[(size_t) i].first;
                labels[(size_t) i] = idsAndLabels[(size_t) i].second;
                params[(size_t) i] = state.getParameter (ids[(size_t) i]);
                jassert (params[(size_t) i] != nullptr);
            }
            defaults = std::move (suggested);
            enabled = state.getRawParameterValue (enabledId);
            morph = state.getRawParameterValue (morphId);
            applySuggested();
        }

        //==============================================================================
        // Audio thread

        void prepare() { for (auto& c : current) c.store (0.0f); }

        void beginBlock (const HostInfo& host, double sampleRate, int numSamples) noexcept
        {
            hostPlaying.store (host.playing, std::memory_order_relaxed);
            const auto haveMap = follower.refresh();
            const auto on = enabled != nullptr && enabled->load() > 0.5f;

            int type = -1;
            bool laterDrop = false;
            float energyScale = 1.0f;   // Ouroboros' energy curve: above a section's usual energy, its scene goes further
            if (haveMap && host.playing && host.hasPpq)
            {
                const auto mid = host.ppq + host.quartersPerSample (sampleRate) * numSamples * 0.5;
                const auto pos = follower.locate (mid, host.barQuarters());
                if (pos.valid)
                {
                    type = (int) pos.type;
                    laterDrop = pos.isLaterDrop();
                    // The energy curve, and Ouroboros' Intensity on top.
                    energyScale = juce::jlimit (0.0f, 4.0f, pos.energyRatio * follower.getMap().intensityScale());
                }
            }
            activeType.store (on ? type : -1);
            activeLaterDrop.store (on && laterDrop);

            const auto blockQ = host.quartersPerSample (sampleRate) * numSamples;
            const auto morphQ = morph != nullptr ? morphQuarters ((int) morph->load()) : 1.0;
            const auto step = morphQ <= 0.0 ? 2.0f : (float) (blockQ / morphQ) * 2.0f; // full -1..+1 travel in morphQ

            for (int i = 0; i < count; ++i)
            {
                auto target = (on && type >= 0) ? offsets[(size_t) type][(size_t) i].load (std::memory_order_relaxed) : 0.0f;
                if (on && laterDrop)
                    target += offsets[(size_t) laterDropSlot][(size_t) i].load (std::memory_order_relaxed);
                target *= energyScale;
                auto c = current[(size_t) i].load (std::memory_order_relaxed);
                c = c < target ? std::min (target, c + step) : std::max (target, c - step);
                current[(size_t) i].store (c, std::memory_order_relaxed);
            }
        }

        /** The value to use for scene parameter i this block, in the parameter's own units. */
        float plain (int i) const noexcept
        {
            return params[(size_t) i]->convertFrom0to1 (getEffective (i));
        }

        //==============================================================================
        // Any thread (UI)

        int getNumParams() const noexcept { return count; }
        const juce::String& getLabel (int i) const { return labels[(size_t) i]; }
        const juce::String& getParamId (int i) const { return ids[(size_t) i]; }
        int getCount() const noexcept { return count; }
        /** Normalised value in use: the knob's current value plus the current (gliding) offset. */
        float getEffective (int i) const noexcept
        {
            const auto base = usePrintBase.load (std::memory_order_relaxed) ? printBase[(size_t) i].load (std::memory_order_relaxed)
                                                                            : params[(size_t) i]->getValue();
            return juce::jlimit (0.0f, 1.0f, base + current[(size_t) i].load (std::memory_order_relaxed));
        }

        //==============================================================================
        // Print to automation (message thread): arm, press play in the host with its
        // automation recording on, and the scene moves are written into the plugin's own
        // parameters as the song plays, so the host records them as automation clips. When
        // playback stops, scenes switch off (the recorded clips now do the moving).

        enum class PrintState { Idle, Armed, Printing };
        PrintState getPrintState() const noexcept { return (PrintState) printState.load(); }
        int getPrintsDone() const noexcept { return printsDone.load(); }

        void armPrint()
        {
            if (apvts == nullptr || count == 0 || getPrintState() != PrintState::Idle) return;
            for (int i = 0; i < count; ++i)
            {
                printBase[(size_t) i].store (params[(size_t) i]->getValue());
                params[(size_t) i]->beginChangeGesture();
            }
            usePrintBase.store (true);
            printState.store ((int) PrintState::Armed);
            if (printTimer == nullptr) printTimer = std::make_unique<PrintTimer> (*this);
            printTimer->startTimerHz (30);
        }

        void cancelPrint()
        {
            if (getPrintState() == PrintState::Idle) return;
            for (int i = 0; i < count; ++i)
            {
                params[(size_t) i]->setValueNotifyingHost (printBase[(size_t) i].load());
                params[(size_t) i]->endChangeGesture();
            }
            endPrint();
        }

        /** The print's work, 30 times a second (tests call it directly). */
        void printTick()
        {
            const auto state = getPrintState();
            if (state == PrintState::Armed && hostPlaying.load())
                printState.store ((int) PrintState::Printing);
            if (getPrintState() != PrintState::Printing) return;
            if (! hostPlaying.load())
            {
                for (int i = 0; i < count; ++i) params[(size_t) i]->endChangeGesture();
                if (auto* on = apvts->getParameter (enabledId))
                {
                    on->beginChangeGesture();
                    on->setValueNotifyingHost (0.0f);   // the recorded automation takes over
                    on->endChangeGesture();
                }
                printsDone.fetch_add (1);
                endPrint();
                return;
            }
            for (int i = 0; i < count; ++i)
            {
                const auto v = juce::jlimit (0.0f, 1.0f, printBase[(size_t) i].load() + current[(size_t) i].load());
                if (std::abs (v - params[(size_t) i]->getValue()) > 1.0e-4f)
                    params[(size_t) i]->setValueNotifyingHost (v);
            }
        }
        int getActiveType() const noexcept { return activeType.load(); }
        bool isInLaterDrop() const noexcept { return activeLaterDrop.load(); }
        bool isOuroborosPresent() const noexcept { return follower.isAvailable(); }

        float getOffset (int type, int i) const noexcept { return offsets[(size_t) type][(size_t) i].load(); }
        void setOffset (int type, int i, float v) noexcept { offsets[(size_t) type][(size_t) i].store (juce::jlimit (-1.0f, 1.0f, v)); }

        //==============================================================================
        // Learning from your tweaks (message thread): a knob you move by hand while a section
        // plays becomes a proposal when that section ends: "make it the <section> scene?".
        // Accepting moves the change into that section's scene and puts the knob back, so the
        // other sections sound as before.

        struct Proposal
        {
            bool valid = false;
            int type = -1;
            std::array<float, maxParams> delta {};
        };
        /** Call a few times a second. `ignore`: the plugin is moving its own knobs (morph, print). */
        void watchTweaks (bool ignore = false)
        {
            if (apvts == nullptr || count == 0) return;
            const auto type = hostPlaying.load() && isEnabled() && printState.load() == 0 ? activeType.load() : -1;
            std::array<float, maxParams> now {};
            for (int i = 0; i < count; ++i) now[(size_t) i] = params[(size_t) i]->getValue();
            if (! watching || ignore)
            {
                lastSeen = now;
                watching = true;
                if (ignore) return;
            }
            if (type >= 0 && type == tweakType)
                for (int i = 0; i < count; ++i)
                    pending[(size_t) i] += now[(size_t) i] - lastSeen[(size_t) i];
            if (type != tweakType)
            {
                float total = 0.0f;
                for (int i = 0; i < count; ++i) total += std::abs (pending[(size_t) i]);
                if (tweakType >= 0 && total > 0.02f)
                {
                    proposal.valid = true;
                    proposal.type = tweakType;
                    proposal.delta = pending;
                }
                pending.fill (0.0f);
                tweakType = type;
            }
            lastSeen = now;
        }
        const Proposal& getProposal() const noexcept { return proposal; }
        bool isEnabled() const noexcept { return enabled != nullptr && enabled->load() > 0.5f; }
        juce::String proposalText() const
        {
            if (! proposal.valid) return {};
            juce::StringArray what;
            for (int i = 0; i < count; ++i)
                if (std::abs (proposal.delta[(size_t) i]) > 0.005f)
                    what.add (labels[(size_t) i] + (proposal.delta[(size_t) i] > 0 ? " up" : " down"));
            const auto name = juce::String (sectionName ((SectionType) proposal.type)).toLowerCase();
            return "You turned " + what.joinIntoString (", ") + " in the " + name + ": do it in every " + name + "?";
        }
        /** Makes the tweak the section's scene, and puts the knobs back for the other sections. */
        void acceptProposal()
        {
            if (! proposal.valid) return;
            for (int i = 0; i < count; ++i)
            {
                const auto d = proposal.delta[(size_t) i];
                if (std::abs (d) < 1.0e-4f) continue;
                setOffset (proposal.type, i, getOffset (proposal.type, i) + d);
                params[(size_t) i]->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, params[(size_t) i]->getValue() - d));
                lastSeen[(size_t) i] = params[(size_t) i]->getValue();
            }
            proposal = {};
        }
        void dismissProposal() { proposal = {}; }

        void clearAll() noexcept
        {
            for (auto& row : offsets) for (auto& o : row) o.store (0.0f);   // includes the Drop 2+ slot
        }

        void applySuggested() noexcept
        {
            clearAll();
            for (const auto& d : defaults)
                if (d.param < count)
                    setOffset ((int) d.type, d.param, d.offset);
        }

        //==============================================================================
        // State (stored inside the plugin's own state tree)

        juce::ValueTree toValueTree() const
        {
            juce::ValueTree t ("Scenes");
            for (int s = 0; s < numSceneSlots; ++s)
            {
                juce::StringArray values;
                for (int i = 0; i < count; ++i) values.add (juce::String (getOffset (s, i), 4));
                t.setProperty (juce::Identifier ("s" + juce::String (s)), values.joinIntoString (","), nullptr);
            }
            return t;
        }

        void fromValueTree (const juce::ValueTree& t)
        {
            if (! t.hasType ("Scenes"))
                return;
            for (int s = 0; s < numSceneSlots; ++s)
            {
                const auto values = juce::StringArray::fromTokens (t[juce::Identifier ("s" + juce::String (s))].toString(), ",", "");
                for (int i = 0; i < count; ++i)
                    setOffset (s, i, i < values.size() ? values[i].getFloatValue() : 0.0f);
            }
        }

        /** Helpers so a plugin can save/restore scenes next to its APVTS state. */
        void writeInto (juce::ValueTree& state) const
        {
            state.removeChild (state.getChildWithName ("Scenes"), nullptr);
            state.appendChild (toValueTree(), nullptr);
        }

        void readFrom (juce::ValueTree& state)
        {
            const auto t = state.getChildWithName ("Scenes");
            if (t.isValid())
            {
                fromValueTree (t);
                state.removeChild (t, nullptr);
            }
        }

    private:
        struct PrintTimer : juce::Timer
        {
            explicit PrintTimer (SceneEngine& e) : engine (e) {}
            void timerCallback() override { engine.printTick(); }
            SceneEngine& engine;
        };
        void endPrint()
        {
            usePrintBase.store (false);
            printState.store ((int) PrintState::Idle);
            if (printTimer != nullptr) printTimer->stopTimer();
        }
        std::array<std::atomic<float>, maxParams> printBase {};
        std::atomic<bool> usePrintBase { false }, hostPlaying { false };
        std::atomic<int> printState { 0 }, printsDone { 0 };
        std::unique_ptr<PrintTimer> printTimer;

        juce::AudioProcessorValueTreeState* apvts = nullptr;
        int count = 0;
        std::array<juce::String, maxParams> ids, labels;
        std::array<juce::RangedAudioParameter*, maxParams> params {};
        std::array<std::array<std::atomic<float>, maxParams>, numSceneSlots> offsets {};
        std::array<std::atomic<float>, maxParams> current {};   // gliding offset, written by the audio thread
        std::atomic<float>* enabled = nullptr;
        std::atomic<float>* morph = nullptr;
        std::atomic<int> activeType { -1 };
        std::atomic<bool> activeLaterDrop { false };
        std::vector<Default> defaults;
        SectionFollower follower;
        // Learning from your tweaks
        bool watching = false;
        int tweakType = -1;
        std::array<float, maxParams> lastSeen {}, pending {};
        Proposal proposal;
    };
}
