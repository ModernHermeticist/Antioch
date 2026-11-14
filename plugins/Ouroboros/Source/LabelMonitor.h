#pragma once

// Listening, on the master (Ouroboros), never in a render:
//  - the reference A/B: a reference track from the label profile plays instead of the song, at the
//    same place in the same kind of section (the song's 3rd bar of the drop = the reference's 3rd
//    bar of its drop), level-matched section by section;
//  - previews: the song as mono, on a phone (300 Hz-8 kHz, mono) or in a car (more lows, less top).
// Both switch off by themselves when FL renders (isNonRealtime) and say so.

#include "LabelProfile.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <memory>

namespace lab
{
    class Monitor
    {
    public:
        enum Preview { none = 0, mono, phone, car };
        std::atomic<int> preview { none };
        std::atomic<bool> referenceOn { false };

        Monitor() { thread.startThread(); }
        ~Monitor() { const juce::ScopedLock sl (lock); reader.reset(); thread.stopThread (2000); }

        /** Message thread: the reference to play (its file and its measured sections). */
        bool loadReference (const Reference& r)
        {
            juce::AudioFormatManager fm;
            fm.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> raw (fm.createReaderFor (juce::File (r.file)));
            if (raw == nullptr) return false;
            refRate = raw->sampleRate;
            refLength = raw->lengthInSamples;
            auto buffered = std::make_unique<juce::BufferingAudioReader> (raw.release(), thread, (int) (refRate * 4.0));
            buffered->setReadTimeout (0);
            const juce::ScopedLock sl (lock);
            reader = std::move (buffered);
            ref = r;
            refName = r.name;
            return true;
        }
        juce::String referenceName() const { return refName; }

        void prepare (double sampleRate)
        {
            fs = sampleRate;
            phoneHp.set (300.0f, 0.707f, (float) fs); phoneLp.set (8000.0f, 0.707f, (float) fs);
            carLow.set (80.0f, 0.707f, (float) fs); carTop.set (6000.0f, 0.707f, (float) fs);
            for (auto* f : { &phoneHp, &phoneLp, &carLow, &carTop }) juce::ignoreUnused (f);
            phoneHp2 = phoneHp; phoneLp2 = phoneLp; carLow2 = carLow; carTop2 = carTop;
        }

        /** Audio thread: replaces or colours the master (only when listening, never offline).
            `songSectionDb` is the song's level in the section now (from the bars), for the match. */
        void process (juce::AudioBuffer<float>& buf, bool offline, double ppq, double barQ, int sectionType, double sectionStartQ, float songSectionDb, bool playing)
        {
            if (offline) { rendering.store (true); return; }
            rendering.store (false);
            const auto n = buf.getNumSamples();
            auto* l = buf.getWritePointer (0);
            auto* r = buf.getNumChannels() > 1 ? buf.getWritePointer (1) : l;
            if (referenceOn.load() && playing && sectionType >= 0)
            {
                const juce::ScopedTryLock sl (lock);
                if (sl.isLocked() && reader != nullptr)
                {
                    // where the reference is: the same bar of its same kind of section
                    const SectionStat* match = nullptr;
                    for (const auto& s : ref.sections) if ((int) s.type == sectionType) { match = &s; break; }
                    if (match != nullptr)
                    {
                        const auto refBarSeconds = 4.0 * 60.0 / juce::jlimit (60.0, 200.0, ref.bpm);
                        const auto barsIn = (ppq - sectionStartQ) / juce::jmax (1.0, barQ);
                        const auto refSeconds = ((double) (match->startBar - 1) + std::fmod (juce::jmax (0.0, barsIn), (double) juce::jmax (1, match->bars))) * refBarSeconds;
                        const auto start = (juce::int64) (refSeconds * refRate);
                        temp.setSize (2, n, false, false, true);
                        temp.clear();
                        if (start >= 0 && start < refLength) reader->read (&temp, 0, n, start, true, true);
                        const auto gain = juce::Decibels::decibelsToGain (juce::jlimit (-24.0f, 24.0f, songSectionDb > -90.0f && match->db > -90.0f ? songSectionDb - match->db : 0.0f));
                        for (int i = 0; i < n; ++i) { l[i] = temp.getSample (0, i) * gain; r[i] = temp.getSample (1, i) * gain; }
                        playingReference.store (true);
                    }
                }
            }
            else playingReference.store (false);
            const auto pv = preview.load();
            if (pv == none) return;
            for (int i = 0; i < n; ++i)
            {
                auto L = l[i], R = r[i];
                if (pv == mono || pv == phone) { const auto m = 0.5f * (L + R); L = R = m; }
                if (pv == phone)
                {
                    L = phoneLp.lowpass (phoneHp.highpass (L)); R = L;
                }
                else if (pv == car)
                {
                    L += 0.6f * carLow.lowpass (L) - 0.4f * carTop.highpass (L);
                    R += 0.6f * carLow2.lowpass (R) - 0.4f * carTop2.highpass (R);
                }
                l[i] = L; if (r != l) r[i] = R;
            }
        }
        std::atomic<bool> rendering { false }, playingReference { false };

    private:
        juce::TimeSliceThread thread { "Label reference" };
        juce::CriticalSection lock;
        std::unique_ptr<juce::BufferingAudioReader> reader;
        Reference ref;
        juce::String refName;
        double refRate = 44100.0, fs = 44100.0;
        juce::int64 refLength = 0;
        juce::AudioBuffer<float> temp;
        suite::Svf phoneHp, phoneLp, carLow, carTop, phoneHp2, phoneLp2, carLow2, carTop2;
    };
}
