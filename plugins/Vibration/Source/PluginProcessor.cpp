#include "PluginProcessor.h"
#include "ActivityBus.h"
#include "ChordBus.h"
#include "PluginEditor.h"
#include "HostInfo.h"

VibrationProcessor::VibrationProcessor()
    : SuiteProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "VibrationState", Params::createLayout())
{
    // The problem watcher's harshness ladder: the master is harsh at 2-8 kHz: the drive 3 dB
    // softer, and the richness target 3 dB lower so the automation doesn't drive it back
    fixes = std::make_unique<suite::FixReceiver> (apvts, [] (suite::Fix, uint32_t) { return std::vector<suite::FixReceiver::Change> {}; });
    fixes->setCustom ([this] (suite::Fix fix, uint32_t) -> std::function<void()>
    {
        if (fix != suite::Fix::DriveSofter || value (Params::drive) + value (Params::driveTrim) <= 0.5f) return {};
        const auto trim = value (Params::driveTrim), offset = richOffset();
        const auto next = juce::jmax (-12.0f, trim - 3.0f);
        if (trim - next < 0.05f) return {};
        apvts.state.setProperty ("rich_offset", juce::jmax (-9.0f, offset - 3.0f), nullptr);
        if (drivePending.pending) { drivePending.pending = false; suite::Conductor::release (getInstanceId()); }
        setParameterPlain (apvts, Params::driveTrim, next);
        suite::MoveLog::add (getName(), getTrackName(), "Drive " + juce::String (trim, 1) + " -> " + juce::String (next, 1) + " dB (Mentalism: the master is harsh)", suite::MoveLog::fix);
        return [this, trim, next, offset]
        {
            apvts.state.setProperty ("rich_offset", offset, nullptr);
            if (std::abs (value (Params::driveTrim) - next) < 0.05f) setParameterPlain (apvts, Params::driveTrim, trim);
        };
    });
    using T = suite::SectionType;
    scenes.setup (apvts,
                  { { Params::euphoria, "Euphoria" }, { Params::air, "Air" }, { Params::drive, "Drive" },
                    { Params::mbDepth, "Multiband depth" }, { Params::width, "Width" } },
                  { { T::Breakdown, sEuphoria, -0.15f }, { T::Breakdown, sWidth, 0.15f }, { T::Breakdown, sAir, 0.05f },
                    { T::Build, sDrive, 0.10f }, { T::Build, sAir, 0.05f },
                    { T::Drop, sEuphoria, 0.20f }, { T::Drop, sWidth, 0.10f },
                    { T::count, sEuphoria, 0.15f }, { T::count, sWidth, 0.10f } });   // Drop 2+: even bigger
}

bool VibrationProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Width processing needs stereo; mono-in/stereo-out is fine.
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo()
        && (in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono());
}

namespace
{
    inline float L0 (const juce::AudioBuffer<float>& b, int i) { return b.getSample (0, i); }
    inline float R0 (const juce::AudioBuffer<float>& b, int i) { return b.getSample (b.getNumChannels() > 1 ? 1 : 0, i); }
}

void VibrationProcessor::updateTension (const suite::HostInfo& host)
{
    // The energy curve (Ouroboros): drawn up, a little more Euphoria; drawn down, a little less
    float energy = 1.0f;
    if (host.hasPpq && chordSections.refresh())
        if (const auto at = chordSections.locate (host.ppq, host.barQuarters()); at.valid) energy = at.energyRatio;
    energyNow.store (energy);
    suite::Progression prog;
    float target = 0.0f;
    if (host.playing && host.hasPpq && suite::ChordBus::read (prog))
    {
        auto origin = 0.0;
        if (chordSections.refresh())
            if (const auto sec = chordSections.locate (host.ppq, host.barQuarters()); sec.valid) origin = sec.startQ;
        target = prog.isTension (prog.chordAt (host.ppq - origin)) ? 1.0f : 0.0f;
    }
    tensionNow.store (tensionNow.load() + 0.3f * (target - tensionNow.load()));
}

void VibrationProcessor::trackPitch (const float* mono, int n)
{
    // Autocorrelation pitch on the last 1024 samples, at half rate, 80 Hz to 1 kHz.
    constexpr int ringSize = 2048;
    if ((int) pitchRing.size() != ringSize) pitchRing.assign ((size_t) ringSize, 0.0f);
    for (int i = 0; i < n; ++i) { pitchRing[(size_t) pitchWrite] = mono[i]; pitchWrite = (pitchWrite + 1) % ringSize; }
    std::array<float, 512> w {};
    float energy = 0.0f;
    for (int k = 0; k < 512; ++k)
    {
        const auto idx = (pitchWrite - 1024 + 2 * k + 2 * ringSize) % ringSize;
        w[(size_t) k] = 0.5f * (pitchRing[(size_t) idx] + pitchRing[(size_t) ((idx + 1) % ringSize)]);
        energy += w[(size_t) k] * w[(size_t) k];
    }
    if (energy < 1.0e-3f) return;   // silence: keep the last reading
    const auto rate = sampleRate / 2.0;
    const auto minLag = juce::jmax (2, (int) (rate / 1000.0)), maxLag = juce::jmin (400, (int) (rate / 80.0));
    std::array<float, 401> corr {};
    float best = 0.0f;
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        float c = 0.0f;
        for (int k = 0; k + lag < 512; ++k) c += w[(size_t) k] * w[(size_t) (k + lag)];
        corr[(size_t) lag] = c / (float) (512 - lag);
        best = std::max (best, corr[(size_t) lag]);
    }
    // The shortest period that's nearly as strong as the best (multiples of it score as well).
    int bestLag = 0;
    for (int lag = minLag + 1; lag < maxLag && bestLag == 0; ++lag)
        if (corr[(size_t) lag] >= 0.9f * best && corr[(size_t) lag] >= corr[(size_t) lag - 1] && corr[(size_t) lag] >= corr[(size_t) lag + 1])
            bestLag = lag;
    if (bestLag == 0 || best < 0.3f * energy / 512.0f) return;   // not clearly pitched
    const auto hz = (float) (rate / bestLag);
    pitchHz.store (hz);
    suite::Autopilot::publishLeadPitch (hz);   // pads and FX make room for the lead's note
    const auto semis = 12.0f * std::log2 (hz / 440.0f);
    if (! pitchAverageSet) { pitchAverageSemis = semis; pitchAverageSet = true; }
    pitchAverageSemis += 0.002f * (semis - pitchAverageSemis);   // the lead's usual pitch (slow)
    pitchOffset.store (juce::jlimit (-1.0f, 1.0f, (semis - pitchAverageSemis) / 12.0f));
}

VibrationProcessor::Effective VibrationProcessor::effective() const
{
    using namespace Params;
    // Scene-able values come through the scene engine (base knob + section offset).
    const auto m = juce::jlimit (0.0f, 1.0f, scenes.plain (sEuphoria) + juce::jlimit (-0.25f, 0.25f, 0.5f * (energyNow.load() - 1.0f)));
    const auto bright = value (pitchBright) * pitchOffset.load();   // -1..+1: below / above the lead's usual pitch
    return { juce::jlimit (-6.0f, 12.0f, scenes.plain (sAir) + m * Macro::air + 4.0f * bright),
             juce::jlimit (-6.0f, 9.0f, value (presence) + m * Macro::presence + 2.0f * bright),
             juce::jlimit (-12.0f, 3.0f, value (mud) + m * Macro::mud),
             juce::jlimit (0.0f, 30.0f, scenes.plain (sDrive) + m * Macro::drive + (suite::AutomationBus::bypassed() ? 0.0f : value (driveTrim)) - driveEaseDb()),
             juce::jlimit (0.0f, 1.0f, scenes.plain (sMultiband) + m * Macro::mbDepth),
             juce::jlimit (0.0f, 2.5f, (scenes.plain (sWidth) + m * Macro::width) * (1.0f + 0.5f * value (tensionWidth) * tensionNow.load())) };
}

float VibrationProcessor::driveEaseDb() const
{
    // Autopilot (harshness): high notes through heavy drive turn grainy - above the lead's usual
    // pitch (a quarter of an octave up and more), the drive eases off, up to 6 dB an octave up
    if (! suite::Autopilot::on (suite::Autopilot::harshness)) return 0.0f;
    const auto ease = 6.0f * juce::jlimit (0.0f, 1.0f, (pitchOffset.load() - 0.25f) / 0.75f);
    if (! easing && ease > 2.0f) { easing = true; suite::Autopilot::count (suite::Autopilot::driveEased); }
    if (ease < 0.5f) easing = false;
    return ease;
}

float VibrationProcessor::richnessNow (int) const
{
    return richDb.load();
}

bool VibrationProcessor::drivePass()
{
    if (driveReset.load()) return false;   // the audio thread hasn't started the new pass yet
    if (apvts.getRawParameterValue (Params::autoDrive)->load() < 0.5f || driveBands.longSeconds.load() < 4.0f) return false;
    const auto role = roleForTrack();
    const auto target = richnessTargetNow (role);
    const auto rich = richnessNow (role);
    driveReset.store (true);
    if (target == 0.0f || rich < -99.0f) return false;
    const auto err = std::abs (rich - target);
    if (drivePending.pending)
    {
        drivePending.pending = false;
        const auto masterWorse = suite::Conductor::verdictFor (getInstanceId()) == suite::Conductor::worse;   // Mentalism's verdict on the master
        suite::Conductor::release (getInstanceId());
        if (err < drivePending.errBefore - 0.3f && ! masterWorse)
        {
            ++driveKept;
            suite::MoveLog::add (getName(), getTrackName(), "Kept: drive " + juce::String (value (Params::driveTrim), 1) + " dB (richness "
                                 + juce::String (drivePending.errBefore, 1) + " -> " + juce::String (err, 1) + " dB from the target)", suite::MoveLog::kept);
        }
        else
        {
            setParameterPlain (apvts, Params::driveTrim, drivePending.trimBefore);
            ++driveUndone;
            drivePending.rest = 6;
            suite::MoveLog::add (getName(), getTrackName(), "Undid: drive back to " + juce::String (drivePending.trimBefore, 1) + " dB: it didn't get closer ("
                                 + juce::String (drivePending.errBefore, 1) + " -> " + juce::String (err, 1) + " dB)", suite::MoveLog::undone);
            return true;
        }
    }
    if (drivePending.rest > 0) { --drivePending.rest; return false; }
    const auto trim = value (Params::driveTrim);
    float next = trim;
    if (rich < target - 2.0f) next = juce::jmin (12.0f, trim + 3.0f);        // too clean: it vanishes on small speakers
    else if (rich > target + 3.0f) next = juce::jmax (-12.0f, trim - 3.0f);  // harsh
    if (std::abs (next - trim) < 0.05f) return false;
    if (! suite::Conductor::request (getInstanceId())) return false;   // one move at a time across the mix
    drivePending = { true, trim, err, 0 };
    setParameterPlain (apvts, Params::driveTrim, next);
    suite::MoveLog::add (getName(), getTrackName(), "Drive " + juce::String (trim, 1) + " -> " + juce::String (next, 1) + " dB (the upper harmonics sit "
                         + juce::String (-rich, 1) + " dB under the body; a " + (role == suite::rolePads ? "pad" : "lead") + " usually about "
                         + juce::String (juce::roundToInt (-target)) + (rich < target ? ": too clean to cut through on a phone)" : ": harsh)"));
    return true;
}

void VibrationProcessor::prepareToPlay (double newSampleRate, int samplesPerBlock)
{
    sampleRate = newSampleRate;
    const auto sr = (float) sampleRate;
    scenes.prepare();
    outBands.prepare (sampleRate);
    driveBands.prepare (sampleRate);
    richFilters[0].set (2000.0f, 0.5412f, sr); richFilters[1].set (2000.0f, 1.3066f, sr);
    richFilters[2].set (1000.0f, 0.5412f, sr); richFilters[3].set (1000.0f, 1.3066f, sr);
    for (auto& f : richFilters) f.reset();
    richHigh = richLow = 0.0;

    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        2, 1, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
    oversampler->initProcessing ((size_t) samplesPerBlock);
    setLatencySamples (juce::roundToInt (oversampler->getLatencyInSamples()));

    for (auto* f : { &lowCutFilters[0], &lowCutFilters[1] }) f->reset();
    for (auto* arr : { &mudEq, &presenceEq, &airEq }) for (auto& f : *arr) f.reset();
    for (int c = 0; c < 2; ++c)
    {
        splitLow[(size_t) c].set (Params::bandSplitLow, sr);
        splitHigh[(size_t) c].set (Params::bandSplitHigh, sr);
        alignLow[(size_t) c].set (Params::bandSplitHigh, sr);
        for (auto* x : { &splitLow[(size_t) c], &splitHigh[(size_t) c], &alignLow[(size_t) c] }) x->reset();
    }
    sideSplit.reset();
    midAlign.reset();

    // Base attack/release per band (scaled by the Time knob): lows slower, highs faster.
    bands[0].attackMs = 30.0f; bands[0].releaseMs = 250.0f;
    bands[1].attackMs = 12.0f; bands[1].releaseMs = 150.0f;
    bands[2].attackMs = 4.0f;  bands[2].releaseMs = 80.0f;
    for (auto& b : bands) b.env.reset();

    powerCoeff = 1.0f - std::exp (-1.0f / (0.4f * sr));
    autoGainCoeff = 1.0f - std::exp (-1.0f / (0.3f * sr));
    inPower = outPower = 0.0f;
    autoGain = 1.0f;
    outGain.reset (sampleRate, 0.03);
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (value (Params::output)));
    monoScratch.assign ((size_t) samplesPerBlock, 0.0f);
}

void VibrationProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // Level-neutral scenes: measure what comes in, process, then bring the level back to the drop's (when it's on).
    const auto n = buffer.getNumSamples();
    const auto inPower = suite::SceneEngine::blockPower (buffer, n);
    processSuiteBlock (buffer, midi);
    const auto g = scenes.levelKeepGain (inPower, suite::SceneEngine::blockPower (buffer, n));
    if (std::abs (g - 1.0f) > 1.0e-4f) buffer.applyGain (0, n, g);
}

void VibrationProcessor::processSuiteBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    using namespace Params;

    const auto n = buffer.getNumSamples();
    if (getTotalNumInputChannels() == 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, n);
    if ((int) monoScratch.size() < n)
        monoScratch.resize ((size_t) n); // only if the host exceeds the announced block size

    const auto sr = (float) sampleRate;
    const auto host = suite::HostInfo::read (getPlayHead());
    scenes.beginBlock (host, sampleRate, n);
    updateTension (host);
    if (value (Params::pitchBright) > 0.0f)
    {
        for (int i = 0; i < n; ++i) monoScratch[(size_t) i] = 0.5f * (L0 (buffer, i) + R0 (buffer, i));
        trackPitch (monoScratch.data(), n);
    }
    const auto e = effective();
    auto* L = buffer.getWritePointer (0);
    auto* R = buffer.getWritePointer (1);
    // Lead-aware space (Antioch on other tracks): how much the lead is playing now.
    if (host.playing && isLeadTrack.load())
        suite::ActivityBus::write (suite::ActivityBus::lead, buffer.getRMSLevel (0, 0, n) / 0.05f);

    if (driveReset.exchange (false)) { driveBands.reset(); richHigh = richLow = 0.0; richDb.store (-100.0f); }

    // ---- Input analysis ----
    for (int i = 0; i < n; ++i)
    {
        monoScratch[(size_t) i] = 0.5f * (L[i] + R[i]);
        outBands.process (L[i], R[i]);
        inPower += powerCoeff * (L[i] * L[i] + R[i] * R[i] - inPower);
    }
    inputTap.push (monoScratch.data(), n);

    // ---- EQ ----
    const auto cut = value (lowCut);
    for (int c = 0; c < 2; ++c)
    {
        lowCutFilters[(size_t) c].set (cut, 0.707f, sr);
        mudEq[(size_t) c].setPeak (mudHz, 1.0f, e.mud, sr);
        presenceEq[(size_t) c].setPeak (presenceHz, 0.8f, e.presence, sr);
        airEq[(size_t) c].setHighShelf (airHz, e.air, sr);
    }
    for (int c = 0; c < 2; ++c)
    {
        auto* x = buffer.getWritePointer (c);
        for (int i = 0; i < n; ++i)
        {
            auto v = x[i];
            if (cut >= lowCutOffBelow) v = lowCutFilters[(size_t) c].highpass (v);
            v = mudEq[(size_t) c].process (v);
            v = presenceEq[(size_t) c].process (v);
            x[i] = airEq[(size_t) c].process (v);
        }
    }

    // ---- Saturation (oversampled; dry/wet blended in the same domain so they stay aligned) ----
    {
        juce::dsp::AudioBlock<float> block (buffer);
        auto up = oversampler->processSamplesUp (block);
        const auto mix = value (satMix);
        const auto g = juce::Decibels::decibelsToGain (e.drive);
        const auto norm = std::tanh (g * 0.25f) / 0.25f; // unity gain for signals around -12 dBFS
        for (size_t c = 0; c < up.getNumChannels(); ++c)
        {
            auto* x = up.getChannelPointer (c);
            for (size_t i = 0; i < up.getNumSamples(); ++i)
                x[i] += mix * (std::tanh (g * x[i]) / norm - x[i]);
        }
        oversampler->processSamplesDown (block);
    }

    // ---- Multiband (OTT-style upward + downward compression, depth = mix) ----
    const auto depth = e.mbDepth;
    const auto timeScale = value (mbTime);
    for (auto& b : bands)
        b.env.set (b.attackMs * timeScale, b.releaseMs * timeScale, sr);

    for (int i = 0; i < n; ++i)
    {
        std::array<std::array<float, 3>, 2> split {};
        for (size_t c = 0; c < 2; ++c)
        {
            const auto x = c == 0 ? L[i] : R[i];
            float low, rest, mid, high;
            splitLow[c].process (x, low, rest);
            splitHigh[c].process (rest, mid, high);
            split[c] = { alignLow[c].allpass (low), mid, high };
        }

        float outL = 0.0f, outR = 0.0f;
        for (size_t b = 0; b < 3; ++b)
        {
            const auto level = bands[b].env.process (std::max (std::abs (split[0][b]), std::abs (split[1][b])));
            const auto lvlDb = juce::Decibels::gainToDecibels (level, -100.0f);
            auto gainDb = 0.0f;
            // Above -30 dB: 4:1 downward. Below -30 dB: 3:1 upward (max +18 dB), with a gate
            // at -70 dB so silence and noise floors aren't pulled up.
            if (lvlDb > -30.0f)                        gainDb = -(lvlDb + 30.0f) * (1.0f - 1.0f / 4.0f);
            else if (lvlDb > -70.0f)                   gainDb = std::min (18.0f, (-30.0f - lvlDb) * (2.0f / 3.0f));
            const auto gain = 1.0f + depth * (juce::Decibels::decibelsToGain (gainDb) - 1.0f);
            outL += split[0][b] * gain;
            outR += split[1][b] * gain;
        }

        // ---- Width: mono below the crossover, scaled sides above ----
        auto mid = 0.5f * (outL + outR), side = 0.5f * (outL - outR);
        const auto monoHz = value (monoBelow);
        if (monoHz >= lowCutOffBelow)
        {
            float sideLow, sideHigh;
            sideSplit.process (side, sideLow, sideHigh);
            side = sideHigh;
            mid = midAlign.allpass (mid);
        }
        side *= e.width;
        L[i] = mid + side;
        R[i] = mid - side;
    }
    const auto monoHz = value (monoBelow);
    if (monoHz >= lowCutOffBelow)
    {
        sideSplit.set (monoHz, sr);
        midAlign.set (monoHz, sr);
    }

    // ---- Auto gain (match long-term loudness in vs out) and output ----
    const auto autoOn = value (Params::autoGain) > 0.5f;
    outGain.setTargetValue (juce::Decibels::decibelsToGain (value (output)));
    for (int i = 0; i < n; ++i)
    {
        // outPower is measured before the auto gain, so the ratio is the gain needed directly.
        outPower += powerCoeff * (L[i] * L[i] + R[i] * R[i] - outPower);
        auto target = 1.0f;
        if (autoOn && inPower > 1.0e-8f && outPower > 1.0e-8f)
            target = juce::jlimit (0.125f, 8.0f, std::sqrt (inPower / outPower));
        autoGain += autoGainCoeff * (target - autoGain);
        const auto g = (autoOn ? autoGain : 1.0f) * outGain.getNextValue();
        L[i] *= g;
        R[i] *= g;
        monoScratch[(size_t) i] = 0.5f * (L[i] + R[i]);
        driveBands.process (L[i], R[i]);
        const auto mono = monoScratch[(size_t) i];
        const auto hi = richFilters[1].highpass (richFilters[0].highpass (mono));
        const auto lo = richFilters[3].lowpass (richFilters[2].lowpass (mono));
        richHigh += (double) hi * hi;
        richLow += (double) lo * lo;
    }
    if (richLow > 1.0e-9) richDb.store ((float) (10.0 * std::log10 ((richHigh + 1.0e-15) / richLow)));
    autoGainDb.store (autoOn ? juce::Decibels::gainToDecibels (autoGain) : 0.0f);
    outputTap.push (monoScratch.data(), n);
}

void VibrationProcessor::applyPreset (int index)
{
    using namespace Params;
    if (! juce::isPositiveAndBelow (index, (int) presets().size()))
        return;
    const auto& p = presets()[(size_t) index];
    const std::pair<const juce::String*, float> values[] =
    {
        { &euphoria, p.euphoria }, { &lowCut, p.lowCut }, { &mud, p.mud }, { &presence, p.presence }, { &air, p.air },
        { &drive, p.drive }, { &satMix, p.satMix }, { &mbDepth, p.mbDepth }, { &mbTime, p.mbTime },
        { &width, p.width }, { &monoBelow, p.monoBelow },
    };
    for (auto& [pid, v] : values)
        if (auto* param = apvts.getParameter (*pid))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 (v));
            param->endChangeGesture();
        }
}

fingerprint::Shape VibrationProcessor::measuredShape() const
{
    fingerprint::Shape bands {};
    for (size_t b = 0; b < bands.size(); ++b) bands[b] = outBands.longDb[b].load();
    return fingerprint::shapeOf (bands);
}

bool VibrationProcessor::saveFingerprint (const juce::File& dir, const juce::String& name)
{
    if (! fingerprintReady() || name.trim().isEmpty()) return false;
    fingerprint::Fingerprint f;
    f.name = name.trim();
    f.shape = measuredShape();
    for (const auto& id : fingerprint::characterIds())
        f.character[id] = apvts.getRawParameterValue (id)->load();
    return fingerprint::save (dir, f);
}

juce::String VibrationProcessor::matchFingerprint (const fingerprint::Fingerprint& f)
{
    for (const auto& [id, v] : f.character) setParameterPlain (apvts, id, v);
    if (! fingerprintReady())
        return "Recalled the character of '" + f.name + "'. Play the lead for 5 s and match again for its tone.";
    const auto move = fingerprint::eqTowards (measuredShape(), f.shape);
    auto nudge = [this] (const juce::String& id, float db)
    {
        auto* p = apvts.getParameter (id);
        const auto now = apvts.getRawParameterValue (id)->load();
        setParameterPlain (apvts, id, p->getNormalisableRange().snapToLegalValue (now + db));
    };
    nudge (Params::mud, move.mud);
    nudge (Params::presence, move.presence);
    nudge (Params::air, move.air);
    outBands.reset();   // measure the new tone afresh
    auto fmt = [] (float v) { return (v >= 0.0f ? "+" : "") + juce::String (v, 1); };
    return "Matched '" + f.name + "': character recalled; mud " + fmt (move.mud) + ", presence " + fmt (move.presence)
         + ", air " + fmt (move.air) + " dB. Match again to refine.";
}

juce::AudioProcessorEditor* VibrationProcessor::createEditor()
{
    return new VibrationEditor (*this);
}

void VibrationProcessor::getPluginState (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    scenes.writeInto (state);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void VibrationProcessor::setPluginState (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            scenes.readFrom (state);
            apvts.replaceState (state);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VibrationProcessor();
}
