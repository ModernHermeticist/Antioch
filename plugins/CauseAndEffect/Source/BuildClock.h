#pragma once

#include "HostInfo.h"
#include <cmath>

/** Turns the Trigger switch into a 0..1 ramp that lasts N bars.

    - Switching Trigger on starts a build, aligned to the nearest bar line, so an
      automation point placed slightly early or late still lines up with the bar.
    - The ramp reaches 1 exactly N bars later; that moment is "the drop": the ramp
      snaps back to 0 and stays there until Trigger is switched off and on again.
    - Switching Trigger off mid-build also counts as a drop (an early exit).
    - When the host is playing, positions come from the host; when stopped, an
      internal clock keeps running at the host tempo so builds can be previewed. */
class BuildClock
{
public:
    void reset()
    {
        armed = finished = false;
        progress = 0.0;
        position = 0.0;
    }

    void beginBlock (const suite::HostInfo& host, double sampleRate, int lengthBars)
    {
        hostDriven = host.playing && host.hasPpq;
        if (hostDriven)
            position = host.ppq;

        quartersPerSample = host.quartersPerSample (sampleRate);
        barQ = host.barQuarters();
        lengthQ = lengthBars * barQ;
    }

    /** Advances one sample. Returns the ramp (0..1); sets `drop` on the drop sample. */
    float next (bool trigger, bool& drop) noexcept
    {
        drop = false;

        if (trigger)
        {
            if (! armed)
            {
                armed = true;
                finished = false;
                startQ = hostDriven ? std::round (position / barQ) * barQ : position;
            }

            // Playback jumped back before the start (e.g. FL loop): restart from that bar.
            if (position < startQ - barQ * 0.5)
                startQ = std::round (position / barQ) * barQ;

            progress = (position - startQ) / lengthQ;

            if (progress >= 1.0 && ! finished)
            {
                finished = true;
                drop = true;
            }
        }
        else
        {
            if (armed && ! finished && progress > 0.0)
                drop = true;
            armed = false;
            finished = false;
            progress = 0.0;
        }

        position += quartersPerSample;

        if (! armed || finished)
            return 0.0f;
        return (float) std::fmin (1.0, std::fmax (0.0, progress));
    }

    double getPosition() const noexcept { return position; }

    /** Progress of the current timed build, or -1 when none is running. */
    double getProgress() const noexcept { return armed && ! finished ? std::fmax (0.0, progress) : -1.0; }

    double getLengthBars() const noexcept { return barQ > 0.0 ? lengthQ / barQ : 0.0; }

private:
    bool armed = false, finished = false, hostDriven = false;
    double position = 0.0, startQ = 0.0, progress = 0.0;
    double quartersPerSample = 0.0, barQ = 4.0, lengthQ = 64.0;
};
