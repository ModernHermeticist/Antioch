#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout()
    {
        using namespace juce;
        using Attr = AudioParameterFloatAttributes;
        AudioProcessorValueTreeState::ParameterLayout l;
        auto db = Attr().withStringFromValueFunction ([] (float v, int) { return (v > 0.05f ? "+" : "") + String (v, 1) + " dB"; });

        l.add (std::make_unique<AudioParameterChoice> (ParameterID { LapisProcessor::targetId, 1 }, "Target",
                                                       StringArray { "Follow Mentalism", "Club (-8 LUFS)", "Loud club (-6 LUFS)", "Streaming (-14 LUFS)" }, 0));
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { LapisProcessor::modeId, 1 }, "Gain Mode",
                                                       StringArray { "Auto", "Hold", "Manual" }, 0));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { LapisProcessor::ceilingId, 1 }, "Ceiling (own target)",
                                                      NormalisableRange<float> (-3.0f, 0.0f), -0.3f,
                                                      Attr().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dBTP"; })));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { LapisProcessor::clipId, 1 }, "Clip", NormalisableRange<float> (0.0f, 1.0f), 0.3f,
                                                      Attr().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; })));
        NormalisableRange<float> rel (10.0f, 500.0f);
        rel.setSkewForCentre (80.0f);
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { LapisProcessor::releaseId, 1 }, "Release", rel, 80.0f,
                                                      Attr().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " ms"; })));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { LapisProcessor::toneLowId, 1 }, "Low Tone", NormalisableRange<float> (-6.0f, 6.0f), 0.0f, db));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { LapisProcessor::toneHighId, 1 }, "High Tone", NormalisableRange<float> (-6.0f, 6.0f), 0.0f, db));
        l.add (std::make_unique<AudioParameterBool> (ParameterID { LapisProcessor::autoToneId, 1 }, "Auto Tone", false));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { LapisProcessor::tiltId, 1 }, "Tilt", NormalisableRange<float> (-3.0f, 3.0f, 0.1f), 0.0f, db));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { LapisProcessor::manualGainId, 1 }, "Manual Gain",
                                                      NormalisableRange<float> (-6.0f, 24.0f), 0.0f, db));
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { LapisProcessor::bitDepthId, 1 }, "Export Bit Depth",
                                                       StringArray { "24-bit (no dither)", "16-bit (dither)" }, 0));
        return l;
    }
}

LapisProcessor::LapisProcessor()
    : SuiteProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "LapisState", layout())
{
    for (auto& g : gainForTarget) g.store (std::numeric_limits<float>::quiet_NaN());
    // Mentalism's "Match reference": nudge the tone shelves by the amounts it sends.
    fixes = std::make_unique<suite::FixReceiver> (apvts, [this] (suite::Fix fix, uint32_t data) -> std::vector<suite::FixReceiver::Change>
    {
        if (fix != suite::Fix::ReferenceTone) return {};
        float lowDb = 0.0f, highDb = 0.0f;
        suite::unpackTone (data, lowDb, highDb);
        const auto low = value (toneLowId), high = value (toneHighId);
        return { { toneLowId, juce::jlimit (-6.0f, 6.0f, low + lowDb) }, { toneHighId, juce::jlimit (-6.0f, 6.0f, high + highDb) } };
    });
}

float LapisProcessor::tiltOf (const std::array<float, 10>& measured, const std::array<float, 10>& target)
{
    // The straight line through measured minus target over 63 Hz..8 kHz: dB per octave (positive = brighter than the target)
    double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
    for (int b = 1; b <= 8; ++b)
    {
        const auto y = (double) (measured[(size_t) b] - target[(size_t) b]);
        sx += b; sy += y; sxx += (double) b * b; sxy += b * y;
    }
    const auto n = 8.0;
    return (float) ((n * sxy - sx * sy) / (n * sxx - sx * sx));
}

std::array<float, 10> LapisProcessor::toneTarget (bool& yours) const
{
    // Your finished masters (the median of each band), or the uplifting trance guide
    std::vector<std::array<float, 10>> shapes;
    for (const auto& e : suite::StyleLog::read())
    {
        if (e.kind != "master") continue;
        const auto v = juce::StringArray::fromTokens (e.get ("bands"), ",", "");
        if (v.size() != 10) continue;
        std::array<float, 10> b {};
        for (int i = 0; i < 10; ++i) b[(size_t) i] = v[i].getFloatValue();
        shapes.push_back (b);
    }
    yours = shapes.size() >= 2;
    if (! yours) return { -4.0f, 1.5f, 1.5f, -1.5f, -3.5f, -5.0f, -6.5f, -8.5f, -11.0f, -17.0f };
    std::array<float, 10> out {};
    for (size_t i = 0; i < 10; ++i)
    {
        std::vector<float> col;
        for (const auto& s : shapes) col.push_back (s[i]);
        out[i] = suite::StyleLog::median (col);
    }
    return out;
}

bool LapisProcessor::tonePass()
{
    if (toneReset.load() || value (autoToneId) < 0.5f || toneBands.longSeconds.load() < 8.0f) return false;
    std::array<float, 10> measured {};
    for (size_t b = 0; b < 10; ++b) measured[b] = toneBands.longDb[b].load();
    bool yours = false;
    const auto tilt = tiltOf (measured, toneTarget (yours));
    tiltNow.store (tilt);
    toneReset.store (true);
    const auto err = std::abs (tilt);
    const juce::String against = yours ? "your finished masters" : "the uplifting trance guide";
    if (tone.pending)
    {
        tone.pending = false;
        if (err < tone.errBefore - 0.05f)
        {
            ++toneKept;
            suite::MoveLog::add (getName(), getTrackName(), "Kept: master tone (tilt " + juce::String (tone.errBefore, 2) + " -> " + juce::String (err, 2)
                                 + " dB per octave from " + against + ")", suite::MoveLog::kept);
        }
        else
        {
            setParameterPlain (apvts, tiltId, tone.tiltBefore);
            ++toneUndone;
            tone.rest = 6;
            suite::MoveLog::add (getName(), getTrackName(), "Undid: master tone back: the tilt didn't get closer to " + against, suite::MoveLog::undone);
            return true;
        }
    }
    if (tone.rest > 0) { --tone.rest; return false; }
    if (err < 0.15f) return false;
    const auto now = value (tiltId);
    const auto next = juce::jlimit (-3.0f, 3.0f, now + (tilt > 0.0f ? -0.5f : 0.5f));   // too bright: darker
    if (std::abs (next - now) < 0.01f) return false;
    tone = { true, now, err, 0 };
    setParameterPlain (apvts, tiltId, next);
    suite::MoveLog::add (getName(), getTrackName(), "Master tilt " + juce::String (now, 1) + " -> " + juce::String (next, 1) + " dB (the master tilts "
                         + juce::String (std::abs (tilt), 2) + " dB per octave " + (tilt > 0.0f ? "brighter" : "darker") + " than " + against + ")");
    return true;
}

bool LapisProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo();
}

std::array<float, analysis::numBands> LapisProcessor::measuredShape() const
{
    std::array<float, analysis::numBands> s {};
    float mean = 0.0f;
    for (size_t b = 0; b < s.size(); ++b) { s[b] = outBands.longDb[b].load(); mean += s[b]; }
    mean /= (float) s.size();
    for (auto& v : s) v -= mean;
    return s;
}

static const juce::StringArray& masterSettingIds()
{
    static const juce::StringArray ids { LapisProcessor::targetId, LapisProcessor::ceilingId, LapisProcessor::clipId, LapisProcessor::releaseId,
                                         LapisProcessor::toneLowId, LapisProcessor::toneHighId };
    return ids;
}

bool LapisProcessor::saveFingerprint (const juce::File& dir, const juce::String& name)
{
    if (! fingerprintReady() || name.trim().isEmpty()) return false;
    juce::StringArray shape, settings;
    for (auto v : measuredShape()) shape.add (juce::String (v, 2));
    for (const auto& pid : masterSettingIds()) settings.add (pid + "=" + juce::String (value (pid), 3));
    dir.createDirectory();
    return dir.getChildFile (juce::File::createLegalFileName (name.trim()) + ".master")
              .replaceWithText ("shape " + shape.joinIntoString (",") + "\nsettings " + settings.joinIntoString (",") + "\n");
}

std::vector<LapisProcessor::MasterFingerprint> LapisProcessor::loadFingerprints (const juce::File& dir)
{
    std::vector<MasterFingerprint> out;
    for (const auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.master"))
    {
        MasterFingerprint m;
        m.name = f.getFileNameWithoutExtension();
        bool ok = false;
        for (const auto& line : juce::StringArray::fromLines (f.loadFileAsString()))
        {
            const auto rest = line.fromFirstOccurrenceOf (" ", false, false);
            if (line.startsWith ("shape "))
            {
                const auto v = juce::StringArray::fromTokens (rest, ",", "");
                if (v.size() == (int) m.shape.size()) { for (int b = 0; b < v.size(); ++b) m.shape[(size_t) b] = v[b].getFloatValue(); ok = true; }
            }
            else if (line.startsWith ("settings "))
                for (const auto& kv : juce::StringArray::fromTokens (rest, ",", ""))
                    if (masterSettingIds().contains (kv.upToFirstOccurrenceOf ("=", false, false)))
                        m.settings[kv.upToFirstOccurrenceOf ("=", false, false)] = kv.fromFirstOccurrenceOf ("=", false, false).getFloatValue();
        }
        if (ok) out.push_back (std::move (m));
    }
    std::sort (out.begin(), out.end(), [] (const auto& a, const auto& b) { return a.name < b.name; });
    return out;
}

juce::String LapisProcessor::matchFingerprint (const MasterFingerprint& m)
{
    for (const auto& [pid, v] : m.settings) setParameterPlain (apvts, pid, v);
    if (! fingerprintReady())
        return "Recalled the chain of '" + m.name + "'. Play 10 s of the song and match again for its tone.";
    const auto now = measuredShape();
    auto diff = [&] (std::initializer_list<int> bands)
    {
        float d = 0.0f;
        for (auto b : bands) d += m.shape[(size_t) b] - now[(size_t) b];
        return d / (float) bands.size();
    };
    const auto low = juce::jlimit (-3.0f, 3.0f, 0.5f * diff ({ 0, 1, 2 })), high = juce::jlimit (-3.0f, 3.0f, 0.5f * diff ({ 8, 9 }));
    setParameterPlain (apvts, toneLowId, juce::jlimit (-6.0f, 6.0f, value (toneLowId) + low));
    setParameterPlain (apvts, toneHighId, juce::jlimit (-6.0f, 6.0f, value (toneHighId) + high));
    outBands.reset();
    auto fmt = [] (float v) { return (v >= 0.0f ? "+" : "") + juce::String (v, 1); };
    return "Matched '" + m.name + "': chain recalled; low tone " + fmt (low) + " dB, high tone " + fmt (high) + " dB. Match again to refine.";
}

void LapisProcessor::prepareToPlay (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    limiter.prepare (sampleRate);
    for (auto* f : { &lowShelf[0], &lowShelf[1], &highShelf[0], &highShelf[1] }) f->reset();
    shownLow = shownHigh = 1000.0f;   // recalculate the shelves for this sample rate
    setLatencySamples (limiter.getLatency());
    loudness.prepare (sampleRate);
    toneBands.prepare (sampleRate);
    tiltShown = 1000.0f;
    outBands.prepare (sampleRate);
    outputPeak.reset();
    passLoudness.prepare (sampleRate);
    passPeak.reset();
    wasPlaying = false;
    passSamples = 0;
    updateInterval = juce::jmax (1, (int) (sampleRate * 0.1));
    samplesSinceUpdate = 0;
    appliedGainDb = 0.0f;
}

void LapisProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    if (toneReset.exchange (false)) toneBands.reset();
    if (resetRequested.exchange (false))
    {
        learnedGainDb.store (0.0f);
        runningMax = -100.0f;
        settledUpdates = 0;
        stHistory.fill (-100.0f);
        maxReductionDb.store (0.0f);
        outputPeak.reset();
        loudness.reset();
    }

    // ---- Target: from Mentalism if it's running and chosen, else our own ----
    const auto targetIndex = (int) value (targetId);
    const auto shared = suite::TargetBus::read();
    const auto follow = targetIndex == followMentalism && shared.valid;
    const auto target = follow ? shared.lufs : targetLufsFor (targetIndex);
    const auto ceilingDb = follow ? shared.ceilingDbtp : value (ceilingId);
    effectiveTarget.store (target);

    // A different target (a new version): remember this target's gain, recall the new one's.
    if (! std::isnan (lastTarget) && std::abs (target - lastTarget) > 0.01f)
    {
        gainForTarget[(size_t) targetSlot (lastTarget)].store (learnedGainDb.load());
        const auto known = gainForTarget[(size_t) targetSlot (target)].load();
        learnedGainDb.store (std::isnan (known) ? juce::jlimit (-12.0f, 24.0f, learnedGainDb.load() + (target - lastTarget)) : known);
        settledUpdates = 0;
        runningMax = -100.0f;
    }
    lastTarget = target;
    effectiveCeiling.store (ceilingDb);
    followingMentalism.store (follow);

    const auto ceiling = juce::Decibels::decibelsToGain (ceilingDb);
    limiter.setCeiling (ceiling);
    limiter.setRelease (value (releaseId));

    const auto mode = (int) value (modeId);
    const auto wantedGainDb = mode == Manual ? value (manualGainId) : learnedGainDb.load();
    const auto clip = value (clipId);
    const auto clipThreshold = ceiling;

    // Tone shelves (only recalculated when they change; bypassed at 0 dB).
    const auto toneLow = value (toneLowId), toneHigh = value (toneHighId);
    if (std::abs (toneLow - shownLow) > 1.0e-3f || std::abs (toneHigh - shownHigh) > 1.0e-3f)
    {
        for (auto& f : lowShelf) f.setLowShelf (100.0f, toneLow, (float) sampleRate);
        for (auto& f : highShelf) f.setHighShelf (8000.0f, toneHigh, (float) sampleRate);
        shownLow = toneLow; shownHigh = toneHigh;
    }
    const auto toneOn = std::abs (toneLow) > 0.01f || std::abs (toneHigh) > 0.01f;
    const auto tilt = value (tiltId);
    if (std::abs (tilt - tiltShown) > 1.0e-3f)
    {
        for (auto& f : tiltLow) f.setLowShelf (300.0f, -0.5f * tilt, (float) sampleRate);
        for (auto& f : tiltHigh) f.setHighShelf (3000.0f, 0.5f * tilt, (float) sampleRate);
        tiltShown = tilt;
    }
    const auto tiltOn = std::abs (tilt) > 0.01f;

    auto* L = buffer.getWritePointer (0);
    auto* R = buffer.getWritePointer (1);
    float maxGr = 0.0f;

    // Export notes: a play-through starts when the transport starts and ends when it stops.
    const auto playing = suite::HostInfo::read (getPlayHead()).playing;
    if (playing && ! wasPlaying) { passLoudness.reset(); passPeak.reset(); passSamples = 0; passMaxGr = 0.0f; }
    if (! playing && wasPlaying && passSamples >= (int64_t) (30.0 * sampleRate) && ! passReady.load())
    {
        finished = { passLoudness.integrated(), passPeak.peakDb(), passMaxGr, (float) ((double) passSamples / sampleRate), target, ceilingDb,
                     (int) value (bitDepthId) == 1 };
        passReady.store (true);
    }
    wasPlaying = playing;
    // 16-bit exports: TPDF dither of one 16-bit step (24-bit needs none).
    const auto dither = (int) value (bitDepthId) == 1;
    constexpr float lsb16 = 1.0f / 32768.0f;

    for (int n = 0; n < buffer.getNumSamples(); ++n)
    {
        // Smooth gain changes (they come in small steps from the learning loop).
        appliedGainDb += 0.0005f * (wantedGainDb - appliedGainDb);
        const auto g = juce::Decibels::decibelsToGain (appliedGainDb);
        auto l = L[n] * g, r = R[n] * g;
        if (toneOn)
        {
            l = highShelf[0].process (lowShelf[0].process (l));
            r = highShelf[1].process (lowShelf[1].process (r));
        }
        if (tiltOn)
        {
            l = tiltHigh[0].process (tiltLow[0].process (l));
            r = tiltHigh[1].process (tiltLow[1].process (r));
        }

        // Soft clipper: shaves the sharpest peaks so the limiter works less hard.
        if (clip > 0.0f)
        {
            l += clip * (clipThreshold * std::tanh (l / clipThreshold) - l);
            r += clip * (clipThreshold * std::tanh (r / clipThreshold) - r);
        }

        limiter.process (l, r);

        if (dither)
        {
            l += (ditherRng.nextFloat() - ditherRng.nextFloat()) * lsb16;
            r += (ditherRng.nextFloat() - ditherRng.nextFloat()) * lsb16;
        }

        // Final safety: never exceed the ceiling by more than rounding.
        L[n] = juce::jlimit (-ceiling, ceiling, l);
        R[n] = juce::jlimit (-ceiling, ceiling, r);
        toneBands.process (L[n], R[n]);
        if (playing)
        {
            passLoudness.process (L[n], R[n]);
            passPeak.process (L[n], R[n]);
            ++passSamples;
        }

        loudness.process (L[n], R[n]);
        outBands.process (L[n], R[n]);
        outputPeak.process (L[n], R[n]);
        maxGr = std::max (maxGr, -juce::Decibels::gainToDecibels (limiter.getGain()));

        // ---- Learning loop, every 100 ms: aim loud sections at target + offset ----
        if (++samplesSinceUpdate >= updateInterval)
        {
            samplesSinceUpdate = 0;
            const auto st = loudness.shortTerm.load();
            runningMax = std::max (runningMax - 0.005f, st);     // forgets very slowly (~0.5 LU per 10 s)

            // Only learn from steady loud material: within 1 LU of the loudest so far and not
            // falling. (Short-term loudness takes ~3 s to fall at the start of a breakdown;
            // learning during that slide would push the gain up.)
            const auto secondAgo = stHistory[(size_t) stHistoryPos];
            stHistory[(size_t) stHistoryPos] = st;
            stHistoryPos = (stHistoryPos + 1) % (int) stHistory.size();
            const auto falling = st < secondAgo - 0.3f;
            const auto loud = st > -50.0f && st > runningMax - 1.0f && ! falling;
            const auto heavyLimiting = gainReductionDb.load() > 8.0f;
            limitedByReduction.store (heavyLimiting);

            if (mode == Auto && loud)
            {
                const auto error = (target + loudSectionOffset) - st;
                auto step = juce::jlimit (-0.2f, 0.2f, error * 0.08f);
                if (heavyLimiting && step > 0.0f) step = 0.0f;   // don't crush the master to chase loudness
                learnedGainDb.store (juce::jlimit (-12.0f, 24.0f, learnedGainDb.load() + step));
                settledUpdates = std::abs (error) < 0.5f ? settledUpdates + 1 : 0;
            }
            settled.store (settledUpdates > 100);   // ~10 s of loud material on target
        }
    }

    gainDb.store (appliedGainDb);
    gainReductionDb.store (maxGr);
    if (playing) passMaxGr = std::max (passMaxGr, maxGr);
    if (maxGr > maxReductionDb.load()) maxReductionDb.store (maxGr);
    outShortTerm.store (loudness.shortTerm.load());
}

juce::String LapisProcessor::exportNotes (const PassResult& p)
{
    const auto offTarget = p.lufs - p.targetLufs;
    juce::StringArray lines;
    lines.add ("EXPORT NOTES  " + juce::Time::getCurrentTime().toString (true, true, false, true));
    lines.add ("");
    lines.add ("Length:      " + juce::String ((int) p.seconds / 60) + ":" + juce::String ((int) p.seconds % 60).paddedLeft ('0', 2));
    lines.add ("Loudness:    " + juce::String (p.lufs, 1) + " LUFS integrated (target " + juce::String (p.targetLufs, 1) + ")");
    lines.add ("True peak:   " + juce::String (p.truePeakDb, 1) + " dBTP (ceiling " + juce::String (p.ceilingDb, 1) + ")");
    lines.add ("Limiting:    up to " + juce::String (p.maxGrDb, 1) + " dB");
    lines.add ("Format:      " + juce::String (p.dithered ? "16-bit WAV, TPDF dither on (in Lapis: don't add more in FL)"
                                                         : "24-bit WAV, no dither (Lapis: 24-bit)"));
    lines.add ("");
    // The integrated target sits below the drops (breakdowns pull it down), so allow the usual spread.
    lines.add (std::abs (offTarget) <= 2.0f ? "Loudness: on target." : offTarget > 0 ? "Loudness: " + juce::String (offTarget, 1) + " LU over the target."
                                                                                    : "Loudness: " + juce::String (-offTarget, 1) + " LU under the target.");
    lines.add (p.truePeakDb <= p.ceilingDb + 0.1f ? "Peak: within the ceiling." : "Peak: over the ceiling by " + juce::String (p.truePeakDb - p.ceilingDb, 1) + " dB.");
    return lines.joinIntoString ("\n") + "\n";
}

juce::File LapisProcessor::writeExportNotes()
{
    if (! passReady.load()) return {};
    const auto p = finished;
    passReady.store (false);
    const auto dir = notesFolder();
    dir.createDirectory();
    const auto name = juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S") + " - " + juce::String (p.lufs, 1) + " LUFS.txt";
    const auto f = dir.getChildFile (juce::File::createLegalFileName (name));
    f.replaceWithText (exportNotes (p));
    lastNotes = f;
    return f;
}

juce::AudioProcessorEditor* LapisProcessor::createEditor()
{
    return new LapisEditor (*this);
}

void LapisProcessor::getPluginState (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("learnedGain", learnedGainDb.load(), nullptr);
    juce::StringArray perTarget;
    for (int i = 0; i < numTargetSlots; ++i)
        if (const auto g = gainForTarget[(size_t) i].load(); ! std::isnan (g))
            perTarget.add (juce::String (i) + ":" + juce::String (g, 2));
    state.setProperty ("gainPerTarget", perTarget.joinIntoString (";"), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void LapisProcessor::setPluginState (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            learnedGainDb.store ((float) state.getProperty ("learnedGain", 0.0f));
            for (auto& g : gainForTarget) g.store (std::numeric_limits<float>::quiet_NaN());
            for (const auto& item : juce::StringArray::fromTokens (state["gainPerTarget"].toString(), ";", ""))
                if (const auto k = item.upToFirstOccurrenceOf (":", false, false).getIntValue(); juce::isPositiveAndBelow (k, numTargetSlots))
                    gainForTarget[(size_t) k].store (item.fromFirstOccurrenceOf (":", false, false).getFloatValue());
            lastTarget = std::numeric_limits<float>::quiet_NaN();
            appliedGainDb = learnedGainDb.load();
            apvts.replaceState (state);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LapisProcessor();
}
