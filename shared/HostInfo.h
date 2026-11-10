#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace suite
{
    /** Snapshot of the host transport for one block. Falls back to 120 BPM, 4/4, stopped. */
    struct HostInfo
    {
        double bpm = 120.0;
        double ppq = 0.0;          // position in quarter notes at block start
        bool   playing = false;
        bool   hasPpq = false;
        int    numerator = 4, denominator = 4;

        double barQuarters() const noexcept { return numerator * 4.0 / denominator; }
        double quartersPerSample (double sampleRate) const noexcept { return bpm / 60.0 / sampleRate; }

        static HostInfo read (juce::AudioPlayHead* head, double fallbackBpm = 120.0)
        {
            HostInfo h;
            h.bpm = fallbackBpm;
            if (head == nullptr)
                return h;

            if (auto pos = head->getPosition())
            {
                if (auto b = pos->getBpm())          h.bpm = *b;
                if (auto p = pos->getPpqPosition())  { h.ppq = *p; h.hasPpq = true; }
                if (auto ts = pos->getTimeSignature())
                {
                    h.numerator = juce::jmax (1, ts->numerator);
                    h.denominator = juce::jmax (1, ts->denominator);
                }
                h.playing = pos->getIsPlaying();
            }

            h.bpm = juce::jlimit (20.0, 999.0, h.bpm);
            return h;
        }
    };
}
