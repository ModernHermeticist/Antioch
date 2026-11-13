#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ActivityBus.h"
#include "Analysis.h"
#include "Registry.h"
#include "MoveLog.h"
#include "StyleLog.h"
#include <map>

namespace
{
    const std::map<juce::String, int>& lockBits()
    {
        // Which lock a manual edit sets: bits 0-5 the bands, 6 low cut, 7 high cut, 8-10 the compressor bands.
        static const std::map<juce::String, int> m = []
        {
            std::map<juce::String, int> out;
            for (int s = 0; s < qn::numSlots; ++s)
                for (const auto& id : { qn::freqId (s), qn::gainId (s), qn::qId (s), qn::bdId (s), qn::grId (s) }) out[id] = s;
            out[qn::lowCutId] = 6;
            out[qn::highCutId] = 7;
            out[qn::widthId] = 11;
            for (int b = 0; b < qn::numMbBands; ++b) { out[qn::thrId (b)] = 8 + b; out[qn::ratioId (b)] = 8 + b; }
            return out;
        }();
        return m;
    }

    int nearestBand (float hz)
    {
        int best = 0;
        for (int b = 1; b < analysis::numBands; ++b)
            if (std::abs (std::log2 (hz / analysis::bandCentres()[(size_t) b])) < std::abs (std::log2 (hz / analysis::bandCentres()[(size_t) best]))) best = b;
        return best;
    }

    juce::String hzText (float hz) { return hz >= 1000.0f ? juce::String (hz / 1000.0f, hz >= 10000.0f ? 0 : 1) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz"; }
    juce::String dbText (float db) { return (db > 0.0f ? "+" : "") + juce::String (db, 1) + " dB"; }
}

QuintessenceProcessor::QuintessenceProcessor()
    : SuiteProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "QuintessenceState", qn::layout())
{
    for (const auto& [id, bit] : lockBits()) apvts.addParameterListener (id, this);

    // Mentalism's masking, mud and harshness fixes for this track land here (Prima Materia leaves
    // them to us when we share a track); Ouroboros' learning pass starts us afresh.
    fixes = std::make_unique<suite::FixReceiver> (apvts, [] (suite::Fix, uint32_t) { return std::vector<suite::FixReceiver::Change> {}; });
    fixes->setCustom ([this] (suite::Fix fix, uint32_t data) -> std::function<void()>
    {
        if (fix == suite::Fix::Carve) return carve (data);
        if (fix == suite::Fix::LearnStart) { relearn(); return [] {}; }
        if (fix == suite::Fix::MasterTone) return masterTone (data);
        if (fix == suite::Fix::Narrower) return narrower (data);
        if (fix == suite::Fix::LessSquash) return lessSquash();
        if (fix == suite::Fix::Relearn && (data & (1u << juce::jlimit (0, 31, effectiveRole()))) != 0)
        {
            relearn();
            addLog ("Re-learning: Correspondence wrote new notes for this part");
            return [] {};
        }
        if (fix == suite::Fix::Wider && value (qn::widthLockId) < 0.5f)
        {
            const auto r = effectiveRole();
            if (r == 1 || r == 2 || r == 6) return {};   // kick, bass and vocal stay where they are
            const auto before = value (qn::widthId), after = juce::jmin (2.0f, before * 1.15f);
            write (qn::widthId, after);
            addLog ("Width " + juce::String (juce::roundToInt (before * 100.0f)) + "% -> " + juce::String (juce::roundToInt (after * 100.0f)) + "% (mood: wider)");
            return [this, before, after] { if (std::abs (value (qn::widthId) - after) < 0.01f) write (qn::widthId, before); };
        }
        return {};
    });
    for (auto& a : passBandDb) a.store (-100.0f);
    loadLearned();
}

QuintessenceProcessor::~QuintessenceProcessor()
{
    suite::Conductor::forget (getInstanceId());
    live.forget (getInstanceId());
    for (const auto& [id, bit] : lockBits()) apvts.removeParameterListener (id, this);
}

bool QuintessenceProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono())
        && layouts.getMainInputChannelSet() == out;
}

void QuintessenceProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    const auto fs = (float) sr;
    for (auto& f : lowCuts) f.reset();
    for (auto& f : highCuts) f.reset();
    lowCutNow = highCutNow = -1.0f;
    for (auto& s : peaks) for (auto& f : s) f.reset();
    for (auto& p : peakNow) p = { -1.0f, 0.0f, 0.0f };
    for (int c = 0; c < 2; ++c)
    {
        xLow[(size_t) c].set (qn::crossLow, fs);   xLow[(size_t) c].reset();
        xHigh[(size_t) c].set (qn::crossHigh, fs); xHigh[(size_t) c].reset();
        apLow[(size_t) c].set (qn::crossHigh, fs); apLow[(size_t) c].reset();
    }
    const std::array<std::pair<float, float>, qn::numMbBands> times { { { 30.0f, 250.0f }, { 10.0f, 150.0f }, { 3.0f, 80.0f } } };
    for (int b = 0; b < qn::numMbBands; ++b) { env[(size_t) b].set (times[(size_t) b].first, times[(size_t) b].second, fs); env[(size_t) b].reset(); }
    makeupDb.fill (0.0f);
    makeupCoeff = 1.0f / (3.0f * fs);   // level-neutral: the make-up follows the average reduction over ~3 s
    for (int b = 0; b < analysis::numBands; ++b) { analysers[(size_t) b].set (analysis::bandCentres()[(size_t) b], 1.41f, fs); analysers[(size_t) b].reset(); }
    winLength = juce::jmax (1, (int) std::round (sr * 0.1));
    for (int k = 0; k < 4; ++k) { sibAnalysers[(size_t) k].set (qn::sibilanceCentres[(size_t) k], 1.5f, fs); sibAnalysers[(size_t) k].reset(); }
    auto coeff = [fs] (float seconds) { return 1.0f - std::exp (-1.0f / (seconds * fs)); };
    dipAttack = coeff (0.03f); dipRelease = coeff (0.3f); rideAttack = coeff (0.02f); rideRelease = coeff (0.5f);
    corrCoeff = (double) coeff (0.05f);
    harshDipDb = 0.0f; widthRide = 1.0f; corrLR = corrLL = corrRR = 0.0; harshBandNow = -1; dipping = riding = false;
    deessFreqNow = -1.0f;
    deessEnv.set (1.0f, 60.0f, fs);  deessEnv.reset();
    fastEnv.set (0.5f, 40.0f, fs);   fastEnv.reset();
    attackMax = 0.0f;
    slowEnv.set (40.0f, 60.0f, fs);  slowEnv.reset();
    outFast.set (0.5f, 40.0f, fs);   outFast.reset();
    outSlow.set (40.0f, 60.0f, fs);  outSlow.reset();
    glueReleaseNow = -1.0f; glueMakeup = 0.0f; deessSlow = 0.0f;
    deessSlowCoeff = 1.0f / (0.4f * fs);
    fifoData.assign ((size_t) fifo.getTotalSize(), 0.0f);
    blockMono.clear();
    blockMono.reserve (8192);
    mbWinLength = juce::jmax (1, (int) std::round (sr * 0.05));
    live.prepare (sr);
    for (auto& f : keyNotch) f.reset();
    keyNotchSet = 0.0f;
    for (auto& ps : fxShift) { ps.prepare (sr, 0.05f); ps.setSemitones (0.0f); }
    fxShiftSet = 0.0f;
    abInPow = abOutPow = 0.0;
    resetMeasurement();
}

void QuintessenceProcessor::resetMeasurement() noexcept
{
    winBand.fill (0.0); passBand.fill (0.0); winTotal = 0.0; winCount = 0; passWindows = 0;
    winMid = winSide = passMid = passSide = 0.0;
    sibWin.fill (0.0); sibPostWin = crestPow = punchSum = glueGrSum = glueLevelSum = 0.0; crestPeak = 0.0f; punchCount = 0; glueCount = 0;
    for (auto& h : histSib) for (auto& c : h) c.store (0, std::memory_order_relaxed);
    for (auto& c : histSibPost) c.store (0, std::memory_order_relaxed);
    passPunchDb.store (0.0f); passGlueGr.store (0.0f); passGlueLevel.store (-100.0f);
    passMidDb.store (-100.0f); passSideDb.store (-100.0f);
    mbWinPre.fill (0.0); mbWinPost.fill (0.0); mbWinCount = 0;
    for (auto& h : histPre) for (auto& c : h) c.store (0, std::memory_order_relaxed);
    for (auto& h : histPost) for (auto& c : h) c.store (0, std::memory_order_relaxed);
    for (auto& a : passBandDb) a.store (-100.0f);
    passWindowsShared.store (0);
    settle = (int) (sr * 0.15);   // let the filters settle after a change before measuring
}

void QuintessenceProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    if (resetRequested.exchange (false)) resetMeasurement();
    if (suite::AutomationBus::bypassed())
    {
        // "Hear without the automation": the track as it comes in - at the loudness it had with it,
        // so the comparison is about the sound, not the level
        bypassedNow.store (true);
        if (abInPow > 1.0e-9 && abOutPow > 1.0e-9)
            buffer.applyGain (juce::jlimit (0.25f, 4.0f, (float) std::sqrt (abOutPow / abInPow)));
        return;
    }
    const auto blockInPow = [&buffer]
    {
        double p = 0.0;
        for (int c = 0; c < juce::jmin (2, buffer.getNumChannels()); ++c)
            for (int i = 0; i < buffer.getNumSamples(); ++i) p += (double) buffer.getSample (c, i) * buffer.getSample (c, i);
        return p;
    }();
    bypassedNow.store (false);

    // Breakdowns get their own correction: a pass measures one kind of section only
    int group = 0;
    float buildProgress = 0.0f, energyRatio = 1.0f;
    double sectionStartQ = 0.0;
    int sectionType = -1;
    const auto info = suite::HostInfo::read (getPlayHead());
    if (follower.refresh() && info.hasPpq)
    {
        const auto pos = follower.locate (info.ppq, info.barQuarters());
        if (pos.valid && pos.type == suite::SectionType::Breakdown) group = 1;
        // The drops are the main correction; grooves, intro and outro get their own on top (with the autopilot)
        if (pos.valid && (pos.type == suite::SectionType::Groove || pos.type == suite::SectionType::Intro || pos.type == suite::SectionType::Outro)
            && suite::Autopilot::on (suite::Autopilot::mixing)) group = 3;
        if (pos.valid && pos.type == suite::SectionType::Build) { group = 2; buildProgress = (float) juce::jlimit (0.0, 1.0, pos.progress (info.ppq)); }
        if (pos.valid) { energyRatio = pos.energyRatio; sectionStartQ = pos.startQ; sectionType = (int) pos.type; }
    }
    if (info.hasPpq && info.playing) suite::MoveLog::setBar ((int) std::floor (info.ppq / info.barQuarters()) + 1);
    if (group != passGroup) { resetMeasurement(); passGroup = group; measureGroup.store (group); }

    const auto n = buffer.getNumSamples();
    const auto chs = juce::jmin (2, buffer.getNumChannels());
    if (chs == 0) return;
    float* ch[2] = { buffer.getWritePointer (0), chs > 1 ? buffer.getWritePointer (1) : nullptr };
    const auto fs = (float) sr;

    // Cuts (4th-order Butterworth)
    const auto lc = value (qn::lowCutId), hc = value (qn::highCutId);
    if (std::abs (lc - lowCutNow) > 1.0e-4f)  { for (int c = 0; c < 2; ++c) { lowCuts[(size_t) c * 2].set (lc, 0.5412f, fs);  lowCuts[(size_t) c * 2 + 1].set (lc, 1.3066f, fs); }  lowCutNow = lc; }
    if (std::abs (hc - highCutNow) > 1.0e-4f) { for (int c = 0; c < 2; ++c) { highCuts[(size_t) c * 2].set (hc, 0.5412f, fs); highCuts[(size_t) c * 2 + 1].set (hc, 1.3066f, fs); } highCutNow = hc; }
    const auto lowOn = lc > qn::lowCutOff, highOn = hc < qn::highCutOff;

    // Bands; a dip made for the vocal or the lead opens only while it plays (in half-dB steps)
    std::array<bool, qn::numSlots> on {};
    for (int s = 0; s < qn::numSlots; ++s)
    {
        const auto f = value (qn::freqId (s)), q = value (qn::qId (s));
        auto g = value (qn::gainId (s)) + (group == 1 ? value (qn::bdId (s)) : group == 3 ? value (qn::grId (s)) : 0.0f);
        if (const auto when = (int) value (qn::whenId (s)); when > 0)
            g = std::round (2.0f * g * suite::ActivityBus::read (when == 1 ? suite::ActivityBus::vocal : suite::ActivityBus::lead)) * 0.5f;
        auto& now = peakNow[(size_t) s];
        if (std::abs (f - now[0]) > 1.0e-4f || std::abs (g - now[1]) > 1.0e-4f || std::abs (q - now[2]) > 1.0e-4f)
        {
            for (auto& p : peaks[(size_t) s]) p.setPeak (f, q, g, fs);
            now = { f, g, q };
        }
        on[(size_t) s] = std::abs (g) > 0.05f;
    }

    // Transition-aware dynamics: through a build the glue tightens (up to 3 dB) and the image
    // widens (up to 25%; not on kick, bass or vocal); the drop's first beat lets both go.
    const auto transitions = value (qn::transitionsId) > 0.5f;
    const auto roleNow = roleShown.load();
    const auto widen = transitions && roleNow != 1 && roleNow != 2 && roleNow != 6 ? 1.0f + 0.25f * buildProgress : 1.0f;
    const auto width = value (qn::widthId) * widen;
    buildNow.store (buildProgress);
    blockMono.clear();

    // De-esser, transient shaper, glue
    const auto deessDepth = value (qn::deessDepthId), deessFreq = value (qn::deessFreqId);
    if (std::abs (deessFreq - deessFreqNow) > 0.5f)
    {
        for (auto& f : deessBand) { f.set (deessFreq, 1.5f, fs); f.reset(); }
        sibPostAnalyser.set (deessFreq, 1.5f, fs); sibPostAnalyser.reset();
        deessFreqNow = deessFreq;
    }
    const auto punch = value (qn::punchId);
    // Autopilot: Mentalism hears a harsh moment on the master and names this track as a cause:
    // its harsh band dips (up to 4 dB) only while the moment lasts
    int harshBandIdx = -1;
    const auto harsh = suite::Autopilot::on (suite::Autopilot::harshness) && suite::Autopilot::harshCulprit (getInstanceId())
                           ? suite::Autopilot::harshNow (&harshBandIdx) : 0.0f;
    if (harsh > 0.0f && harshBandIdx >= 0 && harshBandIdx != harshBandNow)
    {
        for (auto& f : harshFilter) { f.set (analysis::bandCentres()[(size_t) juce::jlimit (0, analysis::numBands - 1, harshBandIdx)], 1.0f, fs); f.reset(); }
        harshBandNow = harshBandIdx;
    }
    const auto harshTargetDb = -4.0f * juce::jlimit (0.0f, 1.0f, harsh);
    const auto rideOn = suite::Autopilot::on (suite::Autopilot::stability) && roleNow != 1 && roleNow != 2;   // (kick and bass are mono already)
    // The energy curve (Ouroboros) sets how hard the glue holds: up to 2 dB tighter where the energy's drawn up
    const auto glueThr = value (qn::glueThrId) - (transitions ? 3.0f * buildProgress : 0.0f) - juce::jlimit (-2.0f, 2.0f, 2.0f * (energyRatio - 1.0f));
    // Kick-keyed glue: on the music bus, the shared kick (Kybalion) adds up to 1.5 dB just after each hit
    float kickMs = 1.0e9f, msPerSample = 0.0f;
    if (roleShown.load() == qn::targets::musicBus && info.hasPpq && info.playing)
    {
        const auto kick = suite::KickBus::read();
        if (kick.valid)
        {
            const auto msPerQ = 60000.0 / juce::jmax (1.0, info.bpm);
            auto phase = std::fmod (info.ppq, info.barQuarters());
            if (phase < 0.0) phase += info.barQuarters();
            kickMs = kick.msSinceHit (sectionType, phase, info.barQuarters(), msPerQ);
            msPerSample = (float) (1000.0 / sr);
        }
    }
    kickKeyed.store (kickMs < 1.0e8f);
    // The multiband's releases follow the tempo (as set at 128 BPM)
    if (info.bpm > 1.0 && std::abs (info.bpm - tempoNow) > 0.5)
    {
        tempoNow = info.bpm;
        const auto scale = (float) juce::jlimit (0.7, 1.4, 128.0 / info.bpm);
        const std::array<std::pair<float, float>, qn::numMbBands> t { { { 30.0f, 250.0f }, { 10.0f, 150.0f }, { 3.0f, 80.0f } } };
        for (int b = 0; b < qn::numMbBands; ++b) env[(size_t) b].set (t[(size_t) b].first, t[(size_t) b].second * scale, fs);
    }
    // Chord-aware: pads and leads leave a little room at the current chord's root, down where the bass plays it
    const auto chordAware = (roleNow == 4 || roleNow == 5) && value (qn::chordAwareId) > 0.5f;
    float chordHz = 0.0f;
    if (chordAware && sectionType >= 0 && info.hasPpq)
        if (suite::Progression prog; suite::ChordBus::read (prog) && prog.valid && prog.count > 0)
        {
            const auto idx = prog.chordAt (info.ppq - sectionStartQ);
            chordHz = suite::music::midiToHz ((float) (36 + (prog.root + prog.chordRoots[(size_t) (idx & 7)]) % 12));
        }
    if (std::abs (chordHz - chordDipHz) > 0.5f)
    {
        for (auto& f : chordDip) { f.setPeak (juce::jmax (20.0f, chordHz), 3.0f, chordHz > 0.0f ? -2.0f : 0.0f, fs); }
        chordDipHz = chordHz;
    }
    chordHzNow.store (chordHz);
    // Riser-aware: while Azoth's riser or swell plays, pads and leads make room for it around 4 kHz
    const auto riserLevel = (roleNow == 4 || roleNow == 5) ? suite::ActivityBus::read (suite::ActivityBus::riser) : 0.0f;
    const auto riserDb = std::round (-3.0f * riserLevel * 2.0f) * 0.5f;
    if (std::abs (riserDb - riserDipDb) > 0.01f)
    {
        for (auto& f : riserDip) f.setPeak (4000.0f, 1.0f, riserDb, fs);
        riserDipDb = riserDb;
    }
    riserDipNow.store (riserDb);
    const auto glueOn = glueThr < -0.05f;
    const auto glueRelease = juce::jlimit (50.0f, 300.0f, 30000.0f / (float) (info.bpm > 1.0 ? info.bpm : 128.0));   // an 8th note
    if (std::abs (glueRelease - glueReleaseNow) > 0.5f) { glueEnv.set (30.0f, glueRelease, fs); glueReleaseNow = glueRelease; }
    float deessMax = 0.0f;

    // Compressor bands
    std::array<float, qn::numMbBands> thr {}, slope {}, grMax {};
    std::array<bool, qn::numMbBands> comp {};
    for (int b = 0; b < qn::numMbBands; ++b)
    {
        thr[(size_t) b] = value (qn::thrId (b));
        const auto ratio = value (qn::ratioId (b));
        comp[(size_t) b] = ratio > 1.01f;
        slope[(size_t) b] = 1.0f - 1.0f / juce::jmax (1.0f, ratio);
    }

    // Autopilot (mixing): the live moves, with the shared kick's position for every role
    const auto mixing = suite::Autopilot::on (suite::Autopilot::mixing);
    {
        qn::LiveMix::Context lc;
        lc.role = (int) value (qn::roleId) > 0 ? (int) value (qn::roleId) : roleNow;   // the chosen role from the start
        lc.id = getInstanceId(); lc.mixing = mixing; lc.stability = suite::Autopilot::on (suite::Autopilot::stability);
        if (mixing && info.hasPpq && info.playing)
            if (const auto kick = suite::KickBus::read(); kick.valid)
            {
                auto phase = std::fmod (info.ppq, info.barQuarters());
                if (phase < 0.0) phase += info.barQuarters();
                lc.kickMs = kick.msSinceHit (sectionType, phase, info.barQuarters(), 60000.0 / juce::jmax (1.0, info.bpm));
                lc.msPerSample = (float) (1000.0 / sr);
                lc.kickBodyHz = kick.bodyHz;
                lc.kickNextMs = kick.msUntilHit (sectionType, phase, info.barQuarters(), 60000.0 / juce::jmax (1.0, info.bpm));
            }
        if (const auto key = suite::KeyBus::read(); key.valid) { lc.keyValid = true; lc.keyRoot = key.root; lc.keyScale = key.scale; }
        if (info.hasPpq && info.playing && info.bpm > 1.0) { lc.ppq = info.ppq; lc.samplesPerQ = sr * 60.0 / info.bpm; }
        live.beginBlock (lc, n);
        if (mixing && ch[1] != nullptr && ! monoCounted) { monoCounted = true; suite::Autopilot::count (suite::Autopilot::monoLowsKept); }
    }
    const auto shiftNow = mixing && roleNow == 7 ? fxShiftSemis.load() : 0.0f;
    const auto notchHz = mixing && std::abs (shiftNow) < 0.05f ? keyNotchHz.load() : 0.0f;   // re-pitched, it needs no notch
    if (std::abs (shiftNow - fxShiftSet) > 0.01f) { for (auto& ps : fxShift) ps.setSemitones (shiftNow); fxShiftSet = shiftNow; }
    if (std::abs (notchHz - keyNotchSet) > 0.5f)
    {
        for (auto& f : keyNotch) { f.setPeak (juce::jmax (20.0f, notchHz), 8.0f, notchHz > 0.0f ? -6.0f : 0.0f, fs); f.reset(); }
        keyNotchSet = notchHz;
    }

    for (int i = 0; i < n; ++i)
    {
        float x[2] = { ch[0][i], ch[1] != nullptr ? ch[1][i] : ch[0][i] };
        {   // what comes in: its crest (peak over level), for the "squashed before it" suggestion
            const auto a = juce::jmax (std::abs (x[0]), std::abs (x[1]));
            inPeakWin = juce::jmax (inPeakWin, a);
            inPowWin += 0.5 * ((double) x[0] * x[0] + (double) x[1] * x[1]);
            if (++inWinCount >= (int) (sr * 0.4))
            {
                const auto rms = std::sqrt (inPowWin / inWinCount);
                if (rms > 1.0e-3)
                {
                    const auto crest = juce::Decibels::gainToDecibels ((float) (inPeakWin / rms));
                    inCrestDb.store (inCrestDb.load() < -50.0f ? crest : inCrestDb.load() + 0.05f * (crest - inCrestDb.load()));
                }
                inPeakWin = 0.0f; inPowWin = 0.0; inWinCount = 0;
            }
        }
        for (int c = 0; c < 2; ++c)
        {
            auto v = x[c];
            if (lowOn) v = lowCuts[(size_t) c * 2 + 1].highpass (lowCuts[(size_t) c * 2].highpass (v));
            for (int s = 0; s < qn::numSlots; ++s)
                if (on[(size_t) s]) v = peaks[(size_t) s][(size_t) c].process (v);
            if (highOn) v = highCuts[(size_t) c * 2 + 1].lowpass (highCuts[(size_t) c * 2].lowpass (v));
            if (chordDipHz > 0.0f) v = chordDip[(size_t) c].process (v);
            if (riserDipDb < -0.01f) v = riserDip[(size_t) c].process (v);
            if (keyNotchSet > 0.0f) v = keyNotch[(size_t) c].process (v);
            if (std::abs (fxShiftSet) > 0.05f) v = fxShift[(size_t) c].process (v);
            x[c] = v;
        }

        // De-esser: the sibilance band comes down only when it spikes 6 dB over its own recent level
        const auto monoPre = 0.5f * (x[0] + x[1]);
        for (int k = 0; k < 4; ++k) { const auto y = sibAnalysers[(size_t) k].bandpass (monoPre); sibWin[(size_t) k] += (double) y * y; }
        if (deessDepth > 0.05f)
        {
            const auto b0 = deessBand[0].bandpass (x[0]) / 1.5f, b1 = deessBand[1].bandpass (x[1]) / 1.5f;
            const auto e = deessEnv.process (0.5f * (std::abs (b0) + std::abs (b1)));
            deessSlow += deessSlowCoeff * (e * e - deessSlow);
            const auto deessThr = 2.0f * std::sqrt (deessSlow) + 1.0e-6f;
            const auto red = e > deessThr ? juce::jmin (deessDepth, 20.0f * std::log10 (e / deessThr)) : 0.0f;
            const auto g = juce::Decibels::decibelsToGain (-red);
            x[0] -= b0 * (1.0f - g);
            x[1] -= b1 * (1.0f - g);
            deessMax = juce::jmax (deessMax, red);
        }

        // Transient shaper: the attack (fast follower over slow) raised or softened
        {
            const auto a = 0.5f * (std::abs (x[0]) + std::abs (x[1]));
            const auto f = fastEnv.process (a), sl = slowEnv.process (a);
            if (std::abs (punch) > 0.005f)
            {
                const auto attack = juce::jlimit (0.0f, 12.0f, 20.0f * std::log10 ((f + 1.0e-6f) / (sl + 1.0e-6f)));
                const auto g = juce::Decibels::decibelsToGain (juce::jlimit (-9.0f, 9.0f, punch * attack));
                x[0] *= g; x[1] *= g;
            }
        }

        // Three bands (Linkwitz-Riley: they add back up flat)
        float band[qn::numMbBands][2];
        for (int c = 0; c < 2; ++c)
        {
            float lo, rest, mid, hi;
            xLow[(size_t) c].process (x[c], lo, rest);
            xHigh[(size_t) c].process (rest, mid, hi);
            band[0][c] = apLow[(size_t) c].allpass (lo);
            band[1][c] = mid;
            band[2][c] = hi;
        }

        float out[2] = { 0.0f, 0.0f };
        for (int b = 0; b < qn::numMbBands; ++b)
        {
            auto& l = band[b][0]; auto& r = band[b][1];
            mbWinPre[(size_t) b] += 0.5 * ((double) l * l + (double) r * r);
            const auto e = env[(size_t) b].process (juce::jmax (std::abs (l), std::abs (r)));
            float gr = 0.0f;
            if (comp[(size_t) b])
            {
                const auto over = 20.0f * std::log10 (e + 1.0e-9f) - thr[(size_t) b];   // 6 dB soft knee
                gr = over > 3.0f ? over * slope[(size_t) b] : over > -3.0f ? slope[(size_t) b] * (over + 3.0f) * (over + 3.0f) / 12.0f : 0.0f;
            }
            if (e > 1.0e-4f) makeupDb[(size_t) b] += makeupCoeff * (gr - makeupDb[(size_t) b]);
            const auto g = juce::Decibels::decibelsToGain (makeupDb[(size_t) b] - gr);
            l *= g; r *= g;
            grMax[(size_t) b] = juce::jmax (grMax[(size_t) b], gr - makeupDb[(size_t) b]);
            mbWinPost[(size_t) b] += 0.5 * ((double) l * l + (double) r * r);
            out[0] += l; out[1] += r;
        }
        // Glue (the music bus): 2:1, 30 ms attack, release an 8th note, level-neutral make-up
        {
            const auto ge = glueEnv.process (juce::jmax (std::abs (out[0]), std::abs (out[1])));
            const auto levelDb = 20.0f * std::log10 (ge + 1.0e-9f);
            float gr = 0.0f;
            if (glueOn)
            {
                const auto over = levelDb - glueThr;
                gr = over > 3.0f ? over * 0.5f : over > -3.0f ? 0.5f * (over + 3.0f) * (over + 3.0f) / 12.0f : 0.0f;
                if (kickMs < 1.0e8f) gr += 1.5f * juce::jmax (0.0f, 1.0f - (kickMs + (float) i * msPerSample) / 150.0f);
            }
            if (ge > 1.0e-4f)
            {
                glueMakeup += makeupCoeff * (gr - glueMakeup);
                if (settle == 0) { glueGrSum += gr; glueLevelSum += levelDb; ++glueCount; }
            }
            const auto g = juce::Decibels::decibelsToGain (glueMakeup - gr);
            out[0] *= g; out[1] *= g;
        }
        // Autopilot: the harsh-moment dip
        harshDipDb += (harshTargetDb < harshDipDb ? dipAttack : dipRelease) * (harshTargetDb - harshDipDb);
        if (harshDipDb < -0.05f && harshBandNow >= 0)
        {
            const auto g = juce::Decibels::decibelsToGain (harshDipDb);
            for (int c = 0; c < 2; ++c) out[c] -= harshFilter[(size_t) c].bandpass (out[c]) * (1.0f - g);
            if (! dipping && harshDipDb < -1.0f) { dipping = true; suite::Autopilot::count (suite::Autopilot::harshDips); }
        }
        if (harshDipDb > -0.5f) dipping = false;
        // Autopilot (mixing): masking, the low end's owner, transients on the kick, mono lows, bass
        // notes, harmonics, moving resonances, drum hits, the noise floor (LiveMix.h)
        live.process (out[0], out[1], ch[1] != nullptr);
        // Autopilot: stereo stability - where the image would cancel in mono (correlation under -0.25,
        // over the last 50 ms), the width rides down to 60% until it's safe again
        corrLR += corrCoeff * ((double) out[0] * out[1] - corrLR);
        corrLL += corrCoeff * ((double) out[0] * out[0] - corrLL);
        corrRR += corrCoeff * ((double) out[1] * out[1] - corrRR);
        const auto corr = corrLL * corrRR > 1.0e-12 ? corrLR / std::sqrt (corrLL * corrRR) : 1.0;
        const auto rideTarget = rideOn && ch[1] != nullptr && corr < -0.25 ? 0.6f : 1.0f;
        widthRide += (rideTarget < widthRide ? rideAttack : rideRelease) * (rideTarget - widthRide);
        if (! riding && widthRide < 0.8f) { riding = true; suite::Autopilot::count (suite::Autopilot::widthRidden); }
        if (widthRide > 0.95f) riding = false;
        widthRideNow.store (widthRide);
        // Width: the side scaled (mid unchanged)
        const auto mid = 0.5f * (out[0] + out[1]);
        const auto side = 0.5f * (out[0] - out[1]) * width * widthRide;
        if (ch[1] != nullptr) { out[0] = mid + side; out[1] = mid - side; }
        ch[0][i] = out[0];
        if (ch[1] != nullptr) ch[1][i] = out[1];

        // Measure (after a change, once the filters have settled)
        if (settle > 0) { --settle; continue; }
        const auto mono = 0.5f * (out[0] + out[1]);
        if (blockMono.size() < blockMono.capacity()) blockMono.push_back (mono);
        { const auto y = sibPostAnalyser.bandpass (mono); sibPostWin += (double) y * y; }
        crestPeak = juce::jmax (crestPeak, std::abs (mono));
        crestPow += (double) mono * mono;
        {
            const auto f = outFast.process (std::abs (mono)), sl = outSlow.process (std::abs (mono));
            if (f > 1.0e-3f) attackMax = juce::jmax (attackMax, 20.0f * std::log10 ((f + 1.0e-6f) / (sl + 1.0e-6f)));
        }
        winMid += (double) mid * mid;
        winSide += (double) side * side;
        for (int b = 0; b < analysis::numBands; ++b)
        {
            const auto y = analysers[(size_t) b].bandpass (mono);
            winBand[(size_t) b] += (double) y * y;
        }
        winTotal += (double) mono * mono;
        if (++winCount >= winLength)
        {
            if (winTotal / winCount > 1.0e-6)   // music, not silence
            {
                ++passWindows;
                for (int b = 0; b < analysis::numBands; ++b)
                {
                    passBand[(size_t) b] += winBand[(size_t) b] / winCount;
                    passBandDb[(size_t) b].store ((float) (10.0 * std::log10 (passBand[(size_t) b] / passWindows + 1.0e-12)));
                }
                passWindowsShared.store (passWindows);
                passMid += winMid / winCount;
                passSide += winSide / winCount;
                passMidDb.store ((float) (10.0 * std::log10 (passMid / passWindows + 1.0e-12)));
                passSideDb.store ((float) (10.0 * std::log10 (passSide / passWindows + 1.0e-12)));
            }
            winBand.fill (0.0); winTotal = 0.0; winCount = 0; winMid = winSide = 0.0;
        }
        if (++mbWinCount >= mbWinLength)
        {
            auto bin = [] (double p) { return juce::jlimit (0, histBins - 1, (int) (-20.0 * std::log10 (p + 1.0e-12))); };   // half-dB bins
            for (int b = 0; b < qn::numMbBands; ++b)
            {
                const auto pre = mbWinPre[(size_t) b] / mbWinCount, post = mbWinPost[(size_t) b] / mbWinCount;
                if (pre > 1.0e-7)
                {
                    histPre[(size_t) b][(size_t) bin (pre)].fetch_add (1, std::memory_order_relaxed);
                    histPost[(size_t) b][(size_t) bin (post)].fetch_add (1, std::memory_order_relaxed);
                }
            }
            for (int k = 0; k < 4; ++k)
                if (sibWin[(size_t) k] / mbWinCount > 1.0e-9) histSib[(size_t) k][(size_t) bin (sibWin[(size_t) k] / mbWinCount)].fetch_add (1, std::memory_order_relaxed);
            if (sibPostWin / mbWinCount > 1.0e-9) histSibPost[(size_t) bin (sibPostWin / mbWinCount)].fetch_add (1, std::memory_order_relaxed);
            if (crestPow / mbWinCount > 1.0e-7 && attackMax > 0.5f)   // windows with an attack in them
            {
                punchSum += attackMax;
                passPunchDb.store ((float) (punchSum / ++punchCount));
            }
            attackMax = 0.0f;
            if (glueCount > 0) { passGlueGr.store ((float) (glueGrSum / (double) glueCount)); passGlueLevel.store ((float) (glueLevelSum / (double) glueCount)); }
            sibWin.fill (0.0); sibPostWin = crestPow = 0.0; crestPeak = 0.0f;
            mbWinPre.fill (0.0); mbWinPost.fill (0.0); mbWinCount = 0;
        }
    }
    for (int b = 0; b < qn::numMbBands; ++b) grNow[(size_t) b].store (juce::jmax (0.0f, grMax[(size_t) b]));
    deessNow.store (deessMax);
    // For the resonance finder (the message thread reads it)
    if (! blockMono.empty())
    {
        int s1, n1, s2, n2;
        fifo.prepareToWrite ((int) blockMono.size(), s1, n1, s2, n2);
        if (n1 > 0) std::copy (blockMono.begin(), blockMono.begin() + n1, fifoData.begin() + s1);
        if (n2 > 0) std::copy (blockMono.begin() + n1, blockMono.begin() + n1 + n2, fifoData.begin() + s2);
        fifo.finishedWrite (n1 + n2);
    }
    live.endBlock();
    {
        double p = 0.0;
        for (int c = 0; c < chs; ++c)
            for (int i = 0; i < n; ++i) p += (double) buffer.getSample (c, i) * buffer.getSample (c, i);
        if (blockInPow > 1.0e-9)
        {
            const auto a = 1.0 - std::exp (-(double) n / (3.0 * sr));
            abInPow += a * (blockInPow - abInPow);
            abOutPow += a * (p - abOutPow);
        }
    }
}

//==============================================================================
QuintessenceProcessor::Measurement QuintessenceProcessor::measurement() const
{
    Measurement m;
    for (int b = 0; b < analysis::numBands; ++b) m.bandsDb[(size_t) b] = passBandDb[(size_t) b].load();
    m.seconds = (float) passWindowsShared.load() * 0.1f;
    m.group = measureGroup.load();
    m.midDb = passMidDb.load();
    m.sideDb = passSideDb.load();
    auto percentiles = [] (const std::array<std::atomic<int>, histBins>& h, float& p50, float& p95) -> int
    {
        int total = 0;
        for (auto& c : h) total += c.load (std::memory_order_relaxed);
        p50 = p95 = -100.0f;
        if (total == 0) return 0;
        int cum = 0;
        bool have50 = false;
        for (int bin = histBins - 1; bin >= 0; --bin)   // from quiet to loud
        {
            cum += h[(size_t) bin].load (std::memory_order_relaxed);
            if (! have50 && cum >= total / 2) { p50 = -0.5f * (float) bin; have50 = true; }
            if (cum >= (total * 95 + 99) / 100) { p95 = -0.5f * (float) bin; break; }
        }
        return total;
    };
    for (int k = 0; k < 4; ++k)
    {
        float p50, p95;
        const auto heard = percentiles (histSib[(size_t) k], p50, p95) >= 20;
        m.sibSwing[(size_t) k] = heard ? p95 - p50 : 0.0f;
        m.sibPeak[(size_t) k] = heard ? p95 : -100.0f;
    }
    {
        float p50, p95;
        m.sibPostSwing = percentiles (histSibPost, p50, p95) >= 20 ? p95 - p50 : 0.0f;
    }
    m.punchDb = passPunchDb.load();
    m.glueGrDb = passGlueGr.load();
    m.glueLevelDb = passGlueLevel.load();
    for (int b = 0; b < qn::numMbBands; ++b)
    {
        percentiles (histPre[(size_t) b], m.p50Pre[(size_t) b], m.p95Pre[(size_t) b]);
        m.windowsPost[(size_t) b] = percentiles (histPost[(size_t) b], m.p50Post[(size_t) b], m.p95Post[(size_t) b]);
    }
    return m;
}

std::array<float, 10> QuintessenceProcessor::toneDiff (const Measurement& m, int role) const
{
    std::array<float, 10> diff {};
    const auto& t = qn::targets::roles()[(size_t) juce::jlimit (0, qn::targets::musicBus, role)];
    if (t.from > t.to) return diff;
    const auto& shape = shapeFor (role);

    // Bands that matter: the role's, minus bands a carve is dipping on purpose, minus bands with
    // nothing in them (40 dB under the loudest: there's nothing to shape).
    std::array<bool, 10> use {};
    float loudest = -200.0f;
    for (int b = t.from; b <= t.to; ++b) loudest = juce::jmax (loudest, m.bandsDb[(size_t) b]);
    for (int b = t.from; b <= t.to; ++b) use[(size_t) b] = m.bandsDb[(size_t) b] > loudest - 40.0f;
    for (int s = 0; s < qn::numSlots; ++s)
    {
        const auto reason = (int) value (qn::reasonId (s));
        if ((reason == qn::masking || reason == qn::mud || reason == qn::harshness || reason == qn::masterBalance) && std::abs (value (qn::gainId (s))) > 0.05f)
            use[(size_t) nearestBand (value (qn::freqId (s)))] = false;
    }
    double sumM = 0.0, sumT = 0.0;
    int count = 0;
    for (int b = 0; b < 10; ++b)
        if (use[(size_t) b]) { sumM += m.bandsDb[(size_t) b]; sumT += shape[(size_t) b]; ++count; }
    if (count < 2) return diff;
    const auto meanM = (float) (sumM / count), meanT = (float) (sumT / count);
    for (int b = 0; b < 10; ++b)
        if (use[(size_t) b]) diff[(size_t) b] = (m.bandsDb[(size_t) b] - meanM) - (shape[(size_t) b] - meanT);
    return diff;
}

float QuintessenceProcessor::toneError (const Measurement& m, int role) const
{
    const auto d = toneDiff (m, role);
    float sum = 0.0f;
    int count = 0;
    for (auto v : d) if (std::abs (v) > 1.0e-6f) { sum += std::abs (v); ++count; }
    return count > 0 ? sum / (float) count : -1.0f;
}

int QuintessenceProcessor::effectiveRole() const
{
    const auto chosen = (int) value (qn::roleId);
    if (chosen > 0) return chosen;
    const auto track = getTrackName().toLowerCase();
    if (track.contains ("bus") || track.contains ("group") || track.contains ("music")) return qn::targets::musicBus;   // the music bus: glue
    return juce::jlimit (0, 7, roleForTrack());
}

//==============================================================================
void QuintessenceProcessor::write (const juce::String& id, float plain)
{
    if (auto* p = apvts.getParameter (id))
    {
        autoWriting.store (true);   // not a manual edit
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (p->getNormalisableRange().snapToLegalValue (plain)));
        p->endChangeGesture();
        autoWriting.store (false);
    }
}

void QuintessenceProcessor::parameterChanged (const juce::String& id, float)
{
    if (autoWriting.load() || isMorphing()) return;
    const auto it = lockBits().find (id);
    if (it != lockBits().end()) pendingLocks.fetch_or (1u << it->second);
}

void QuintessenceProcessor::applyLocks()
{
    const auto bits = pendingLocks.exchange (0);
    if (bits == 0) return;
    for (int s = 0; s < qn::numSlots; ++s)
        if (bits & (1u << s))
        {
            if (value (qn::lockId (s)) < 0.5f) addLog ("Band " + juce::String (s + 1) + " is yours now: locked, the automation leaves it alone");
            write (qn::lockId (s), 1.0f);
            write (qn::reasonId (s), (float) qn::yours);
            if (pending.kind == Pending::eqSlot && pending.index == s) pending.kind = Pending::none;
        }
    if (bits & (1u << 6)) { write (qn::lowCutLockId, 1.0f);  if (pending.kind == Pending::lowCut) pending.kind = Pending::none; }
    if (bits & (1u << 7)) write (qn::highCutLockId, 1.0f);
    if (bits & (1u << 11)) { write (qn::widthLockId, 1.0f); if (pending.kind == Pending::width) pending.kind = Pending::none; }
    for (int b = 0; b < qn::numMbBands; ++b)
        if (bits & (1u << (8 + b)))
        {
            write (qn::mbLockId (b), 1.0f);
            if (pending.kind == Pending::mb && pending.index == b) pending.kind = Pending::none;
        }
}

void QuintessenceProcessor::addLog (const juce::String& line)
{
    {
        const juce::ScopedLock sl (logLock);
        log.insert (0, line);
        while (log.size() > 40) log.remove (log.size() - 1);
    }
    // ... and in the suite's What changed
    const auto kind = line.startsWith ("Kept") ? suite::MoveLog::kept : line.startsWith ("Undid") ? suite::MoveLog::undone : suite::MoveLog::info;
    suite::MoveLog::add (getName(), getTrackName(), line, kind);
}

const std::array<float, 10>& QuintessenceProcessor::shapeFor (int role) const
{
    const auto r = (size_t) juce::jlimit (0, 7, role);
    const auto& base = learnedCount[r] >= 3 ? learned[r] : qn::targets::roles()[r].shape;
    // A reference loaded in Mentalism colours every role's target the way it differs from the guide
    std::array<float, 10> offset {};
    if (! suite::ReferenceBus::offset (offset)) { referenceShown.store (false); return base; }
    referenceShown.store (true);
    for (size_t b = 0; b < 10; ++b) withReference[b] = base[b] + offset[b];
    return withReference;
}

void QuintessenceProcessor::loadLearned()
{
    // Mentalism logs each track's tone (relative to its loudest band) when you finish a song;
    // your last 8 songs per role make your target.
    std::array<std::vector<std::array<float, 10>>, 8> found;
    for (const auto& e : suite::StyleLog::read())
    {
        if (e.kind != "tone") continue;
        const auto role = e.get ("role").getIntValue();
        const auto parts = juce::StringArray::fromTokens (e.get ("bands"), ",", "");
        if (role < 1 || role > 7 || parts.size() != 10) continue;
        std::array<float, 10> shape {};
        for (int b = 0; b < 10; ++b) shape[(size_t) b] = parts[b].getFloatValue();
        found[(size_t) role].push_back (shape);
    }
    for (size_t r = 0; r < 8; ++r)
    {
        auto& list = found[r];
        if (list.size() > 8) list.erase (list.begin(), list.end() - 8);
        learnedCount[r] = (int) list.size();
        learned[r].fill (0.0f);
        for (const auto& s : list)
            for (size_t b = 0; b < 10; ++b) learned[r][b] += s[b] / (float) list.size();
    }
}

//==============================================================================
bool QuintessenceProcessor::autoPass()
{
    drainSpectrum();
    const auto m = measurement();
    if (m.seconds < (float) passSecondsNow() || suite::AutomationBus::bypassed()) return false;
    requestReset();
    if (m.group == 2) return false;   // builds: the transition moves things on purpose, nothing to judge
    struct ClearAfterPass { qn::ResonanceFinder& f; ~ClearAfterPass() { f.reset(); } } clearAfterPass { resonances };   // each pass hears its own spectrum
    const auto role = effectiveRole();
    roleShown.store (role);
    groupShown.store (m.group);
    if (m.sideMinusMid() > -90.0f) sideNow.store (m.sideMinusMid());
    if (++passes % 40 == 0) loadLearned();
    learnedShown.store (learnedSongs (role) >= 3 ? learnedSongs (role) : 0);

    if (! haveBands) { longBands = m.bandsDb; haveBands = true; }
    else for (int b = 0; b < 10; ++b) longBands[(size_t) b] += 0.3f * (m.bandsDb[(size_t) b] - longBands[(size_t) b]);
    const auto diff = toneDiff (m, role);
    for (int b = 0; b < 10; ++b) shownDiff[(size_t) b].store (diff[(size_t) b]);

    for (auto& c : toneCooldown) c = juce::jmax (0, c - 1);
    for (auto& c : mbCooldown) c = juce::jmax (0, c - 1);
    widthCooldown = juce::jmax (0, widthCooldown - 1);
    punchCooldown = juce::jmax (0, punchCooldown - 1);
    deessCooldown = juce::jmax (0, deessCooldown - 1);
    for (auto& c : resonanceCooldown) --c.second;
    resonanceCooldown.erase (std::remove_if (resonanceCooldown.begin(), resonanceCooldown.end(), [] (const std::pair<float, int>& c) { return c.second <= 0; }),
                             resonanceCooldown.end());

    // Ringing peaks: one that's there pass after pass is a resonance, not a note
    std::vector<float> found;
    if (resonances.framesHeard() >= 8)
        for (const auto& peak : resonances.peaks (sr))
        {
            found.push_back (peak.hz);
            auto it = std::find_if (candidates.begin(), candidates.end(), [&] (const Candidate& c) { return std::abs (std::log2 (c.hz / peak.hz)) < 0.04f; });
            if (it != candidates.end()) { it->hz = 0.5f * (it->hz + peak.hz); ++it->passes; it->prominence = peak.prominenceDb; it->seconds += m.seconds; }
            else candidates.push_back ({ peak.hz, 1, peak.prominenceDb, m.seconds });
        }
    candidates.erase (std::remove_if (candidates.begin(), candidates.end(), [&] (const Candidate& c)
                      { return std::none_of (found.begin(), found.end(), [&] (float hz) { return std::abs (std::log2 (c.hz / hz)) < 0.04f; }); }),
                      candidates.end());

    // Held notes (an FX's key check): any clear peak from 80 Hz, however far it stands out
    if (role == 7 && resonances.framesHeard() >= 8)
    {
        std::vector<float> heard;
        for (const auto& peak : resonances.peaks (sr, 15.0f, 2, 200.0f, 80.0f))
        {
            heard.push_back (peak.hz);
            auto it = std::find_if (noteCandidates.begin(), noteCandidates.end(), [&] (const Candidate& c) { return std::abs (std::log2 (c.hz / peak.hz)) < 0.04f; });
            if (it != noteCandidates.end()) { it->hz = 0.5f * (it->hz + peak.hz); ++it->passes; }
            else noteCandidates.push_back ({ peak.hz, 1, peak.prominenceDb, m.seconds });
        }
        noteCandidates.erase (std::remove_if (noteCandidates.begin(), noteCandidates.end(), [&] (const Candidate& c)
                              { return std::none_of (heard.begin(), heard.end(), [&] (float hz) { return std::abs (std::log2 (c.hz / hz)) < 0.04f; }); }),
                              noteCandidates.end());
    }

    // Spire tips from this pass
    {
        const auto& t = qn::targets::roles()[(size_t) role];
        qn::spire::Reading reading;
        reading.role = role;
        reading.toneDiff = diff;
        reading.sideMinusMidDb = m.sideMinusMid();
        reading.sideTargetDb = t.sideDb;
        reading.midSwingDb = m.windowsPost[1] >= 20 ? m.swingPost (1) : 0.0f;
        reading.midSwingTargetDb = t.swing[1];
        reading.breakdown = m.group == 1;
        for (const auto& c : candidates) if (c.passes >= 2) reading.resonancesHz.push_back (c.hz);
        const juce::ScopedLock sl (logLock);
        tipsNow = qn::spire::tips (reading);
        resonancesNow = reading.resonancesHz;
    }


    // Check the last move: keep it if the sound got closer to the target, otherwise undo it.
    if (pending.kind != Pending::none)
    {
        bool helped = false;
        juce::String measure;
        if (pending.kind == Pending::eqSlot)
        {
            const auto after = toneError (m, role);
            helped = after >= 0.0f && after < pending.errBefore - 0.15f;
            measure = "distance from a " + juce::String (qn::targets::roles()[(size_t) role].name) + "'s usual tone " + juce::String (pending.errBefore, 1) + " -> " + juce::String (after, 1) + " dB";
            if (! helped && pending.band >= 0) toneCooldown[(size_t) pending.band] = 8;
        }
        else if (pending.kind == Pending::lowCut)
        {
            // The lows went down and the part's own body didn't (the octave bands overlap, so a
            // band's reading never drops all the way)
            const auto lows = juce::jmax (m.bandsDb[0], m.bandsDb[1]);
            float body = -200.0f;
            for (int b = 2; b < 10; ++b) body = juce::jmax (body, m.bandsDb[(size_t) b]);
            helped = lows < pending.errBefore - 1.0f && body > pending.extra - 1.0f;
            measure = "lows " + juce::String (pending.errBefore - lows, 1) + " dB down, the body " + juce::String (body - pending.extra, 1) + " dB";
        }
        else if (pending.kind == Pending::width)
        {
            const auto after = std::abs (m.sideMinusMid() - (qn::targets::roles()[(size_t) role].sideDb + sideOffset()));
            helped = m.sideMinusMid() > -90.0f && after < pending.errBefore - 0.5f;
            measure = "width " + juce::String (pending.errBefore, 1) + " -> " + juce::String (after, 1) + " dB from a " + juce::String (qn::targets::roles()[(size_t) role].name) + "'s";
            if (! helped) widthCooldown = 8;
        }
        else if (pending.kind == Pending::budget)
        {
            // A deliberate dip: kept unless it pulls the track's own tone off
            const auto after = toneError (m, role);
            helped = pending.errBefore < 0.0f || after < pending.errBefore + 1.0f;
            measure = "the track's tone " + juce::String (pending.errBefore, 1) + " -> " + juce::String (after, 1) + " dB from its target";
        }
        else if (pending.kind == Pending::punch)
        {
            const auto after = std::abs (m.punchDb - qn::targets::punchTarget (role));
            helped = m.punchDb > 0.0f && after < pending.errBefore - 0.3f;
            measure = "punch " + juce::String (pending.errBefore, 1) + " -> " + juce::String (after, 1) + " dB from the target";
            if (! helped) punchCooldown = 8;
        }
        else if (pending.kind == Pending::glue)
        {
            const auto after = std::abs (m.glueGrDb - glueTarget());
            helped = after < pending.errBefore - 0.2f;
            measure = "glue " + juce::String (m.glueGrDb, 1) + " dB average reduction (aiming for " + juce::String (glueTarget(), 1) + ")";
        }
        else if (pending.kind == Pending::deess)
        {
            helped = m.sibPostSwing > 0.0f && m.sibPostSwing < pending.errBefore - 1.5f;
            measure = "the sibilance spikes " + juce::String (pending.errBefore, 1) + " -> " + juce::String (m.sibPostSwing, 1) + " dB over its usual level";
            if (! helped) deessCooldown = 12;
        }
        else if (pending.kind == Pending::resonance)
        {
            // The peak stands out less, and the tone around it didn't suffer
            const auto hz = value (qn::freqId (pending.index));
            const auto after = resonances.prominenceAt (hz, sr);
            const auto tone = toneError (m, role);
            helped = after < pending.errBefore - 1.0f && (tone < 0.0f || pending.extra < 0.0f || tone < pending.extra + 1.0f);
            measure = "the peak " + juce::String (pending.errBefore, 1) + " -> " + juce::String (after, 1) + " dB above its surroundings";
            if (! helped) resonanceCooldown.push_back ({ hz, 12 });
        }
        else
        {
            const auto& t = qn::targets::roles()[(size_t) role];
            const auto after = std::abs (m.swingPost (pending.index) - (t.swing[(size_t) pending.index] + swingOffset()));
            helped = after < pending.errBefore - 0.5f;
            measure = "loud moments " + juce::String (pending.errBefore, 1) + " -> " + juce::String (after, 1) + " dB from the target";
            if (! helped) mbCooldown[(size_t) pending.index] = 8;
        }
        // The conductor: Mentalism watched the master while this move held the turn
        const auto masterWorse = suite::Conductor::verdictFor (getInstanceId()) == suite::Conductor::worse;
        const auto what = pending.what;
        if (helped && ! masterWorse) { ++kept; addLog ("Kept: " + what + " (" + measure + ")"); }
        else
        {
            for (const auto& [id, v] : pending.before) write (id, v);
            ++undone;
            addLog ("Undid: " + what + (masterWorse ? juce::String (": it helped this track but moved the master away from its target")
                                                    : ": it didn't bring the sound closer (" + measure + ")"));
            helped = false;
        }
        pending.kind = Pending::none;
        suite::Conductor::release (getInstanceId());
        if (! helped) return true;   // this pass measured the undone move: decide again next pass
    }

    // The autopilot's quick, unjudged moves: an out-of-key FX note, an effect return's cuts
    if (suite::Autopilot::on (suite::Autopilot::mixing)) { keyMove (role); returnsMove(); }

    // At most one new move a pass, so the next pass can tell whether it helped. The kinds of
    // move (EQ, dynamics, width, resonances) take turns going first.
    // One move at a time across the whole mix: wait for the conductor's turn
    if (! suite::Conductor::request (getInstanceId(), needNow (m, role))) return false;
    constexpr int kinds = 8;
    for (int k = 0; k < kinds; ++k)
    {
        const auto kind = (nextFirst + k) % kinds;
        const auto moved = kind == 0 ? eqMove (m, role) : kind == 1 ? mbMove (m, role) : kind == 2 ? widthMove (m, role) : kind == 3 ? resonanceMove (m, role)
                         : kind == 4 ? punchMove (m, role) : kind == 5 ? glueMove (m, role) : kind == 6 ? deessMove (m, role) : budgetMove (m, role);
        if (moved) { nextFirst = (kind + 1) % kinds; return true; }
    }
    suite::Conductor::release (getInstanceId());   // nothing to do: the turn goes on
    return false;
}

void QuintessenceProcessor::keyMove (int role)
{
    // A pitched FX (a tonal riser, a vocal shot, a sample's note) holding a note outside the song's
    // key: that note is notched 6 dB while it's there. Moving keys clears a notch that now fits.
    const auto key = suite::KeyBus::read();
    if (role != 7 || ! key.valid) { if (keyNotchHz.load() > 0.0f && role != 7) keyNotchHz.store (0.0f); return; }
    const auto& steps = suite::music::scales()[(size_t) juce::jlimit (0, (int) suite::music::scales().size() - 1, key.scale)].steps;
    auto inKey = [&] (float hz)
    {
        const auto midi = 69.0f + 12.0f * std::log2 (hz / 440.0f);
        const auto pc = ((int) std::lround (midi) % 12 + 12) % 12;
        return std::find (steps.begin(), steps.end(), ((pc - key.root) % 12 + 12) % 12) != steps.end();
    };
    if (const auto now = keyNotchHz.load(); now > 0.0f && inKey (now)) { keyNotchHz.store (0.0f); fxShiftSemis.store (0.0f); }
    for (const auto& c : noteCandidates)
    {
        if (c.passes < 2 || c.hz < 80.0f || c.hz > 4000.0f) continue;
        const auto midi = 69.0f + 12.0f * std::log2 (c.hz / 440.0f);
        if (std::abs (midi - std::round (midi)) > 0.35f || inKey (c.hz)) continue;   // a clear note, out of the key
        if (std::abs (keyNotchHz.load() - c.hz) < 1.0f) return;
        keyNotchHz.store (c.hz);
        {
            // Re-pitch the FX to the nearest note of the key (a semitone at most, either way)
            const auto rounded = (int) std::lround (midi);
            float shift = 0.0f;
            for (const auto d : { 1, -1 })
            {
                const auto pc = (((rounded + d) % 12 - key.root) % 12 + 12) % 12;
                if (std::find (steps.begin(), steps.end(), pc) != steps.end()) { shift = (float) d + (float) (rounded - midi); break; }
            }
            fxShiftSemis.store (shift);
            if (std::abs (shift) > 0.05f) suite::Autopilot::count (suite::Autopilot::fxRepitched);
        }
        suite::Autopilot::count (suite::Autopilot::outOfKeyNotched);
        addLog (juce::String (juce::roundToInt (c.hz)) + " Hz notched 6 dB: the FX holds " + suite::music::noteNames()[((int) std::lround (midi) % 12 + 12) % 12]
                + ", outside the song's key");
        return;
    }
}

void QuintessenceProcessor::returnsMove()
{
    // An effect return (a track named reverb, delay, echo, return, send or space, or one with
    // Antioch on it): low cut at 180 Hz and high cut at 12 kHz, so the space doesn't muddy the low
    // end or fizz on top. Once; your own cut settings are left alone.
    if (returnsDone) return;
    const auto track = getTrackName().toLowerCase();
    if (track.isEmpty()) return;
    returnsDone = true;
    bool isReturn = false;
    for (const auto* w : { "reverb", "verb", "delay", "echo", "return", "send", "space" }) isReturn = isReturn || track.contains (w);
    for (const auto& e : suite::Registry::list()) isReturn = isReturn || (e.plugin == "Antioch" && e.track.toLowerCase() == track);
    if (! isReturn) return;
    juce::StringArray did;
    if (value (qn::lowCutLockId) < 0.5f && value (qn::lowCutId) < 180.0f) { write (qn::lowCutId, 180.0f); did.add ("low cut at 180 Hz"); }
    if (value (qn::highCutLockId) < 0.5f && value (qn::highCutId) > 12000.0f) { write (qn::highCutId, 12000.0f); did.add ("high cut at 12 kHz"); }
    if (did.isEmpty()) return;
    lowCutTried = true;
    suite::Autopilot::count (suite::Autopilot::returnsCleaned);
    addLog ("Effect return: " + did.joinIntoString (" and ") + ", so the space stays out of the low end and off the top");
}

float QuintessenceProcessor::needNow (const Measurement& m, int role) const
{
    // How far this track is from its own target, plus its share of the master's worst problem
    auto need = juce::jmax (0.0f, toneError (m, role));
    int band = -1;
    float diff = 0.0f;
    if (suite::Conductor::masterProblem (band, diff) && std::abs (diff) > 1.5f && band >= 0 && band < 10 && haveBands)
    {
        double mine = std::pow (10.0, longBands[(size_t) band] / 10.0), all = 0.0;
        for (const auto& e : suite::Registry::list())
            if (e.plugin == "Quintessence" && e.has (suite::Status::hasBands)) all += std::pow (10.0, e.bands[(size_t) band] / 10.0);
        if (all > 1.0e-12) need += (float) juce::jmin (1.0, mine / all) * std::abs (diff);
    }
    needShown.store (need);
    return need;
}

std::function<void()> QuintessenceProcessor::masterTone (uint32_t data)
{
    // Mentalism's share of the master's tone for this track: a band that moves only on its word
    uint32_t who = 0;
    int band = 0;
    float db = 0.0f;
    suite::unpackMasterTone (data, who, band, db);
    if (who != (getInstanceId() & 0xfffu) || band < 0 || band >= analysis::numBands || std::abs (db) < 0.05f) return {};
    const auto centre = analysis::bandCentres()[(size_t) band];
    int slot = -1;
    for (int s = 0; s < qn::numSlots; ++s)
        if (value (qn::lockId (s)) < 0.5f && (int) value (qn::reasonId (s)) == qn::masterBalance && std::abs (std::log2 (value (qn::freqId (s)) / centre)) < 0.2f) slot = s;
    if (slot < 0)   // a free band, else one doing role tone (never a masking carve)
        for (int pass = 0; pass < 2 && slot < 0; ++pass)
            for (int s = 0; s < qn::numSlots && slot < 0; ++s)
                if (value (qn::lockId (s)) < 0.5f && (int) value (qn::reasonId (s)) == (pass == 0 ? qn::free : qn::roleTone)) slot = s;
    if (slot < 0) { addLog ("Mentalism's master balance for " + hzText (centre) + " arrived, but every band is locked or busy"); return {}; }
    std::vector<std::pair<juce::String, float>> before;
    for (const auto& id : { qn::freqId (slot), qn::gainId (slot), qn::qId (slot), qn::reasonId (slot), qn::whenId (slot), qn::bdId (slot), qn::grId (slot) }) before.push_back ({ id, value (id) });
    const auto already = (int) value (qn::reasonId (slot)) == qn::masterBalance ? value (qn::gainId (slot)) : 0.0f;
    const auto gain = juce::jlimit (-qn::maxToneDb, qn::maxToneDb, already + db);
    if (pending.index == slot) pending.kind = Pending::none;
    write (qn::freqId (slot), centre);
    write (qn::qId (slot), 1.0f);
    write (qn::reasonId (slot), (float) qn::masterBalance);
    write (qn::whenId (slot), 0.0f);
    write (qn::bdId (slot), 0.0f); write (qn::grId (slot), 0.0f);
    write (qn::gainId (slot), gain);
    addLog ("Band " + juce::String (slot + 1) + ": " + hzText (centre) + " " + dbText (gain) + " (master balance: this track's share of the master's "
            + dbText (-db).replace ("+", "") + " at " + hzText (centre) + ", from Mentalism)");
    return [this, before, slot, gain]
    {
        if (std::abs (value (qn::gainId (slot)) - gain) > 0.05f) return;
        for (const auto& [id, v] : before) write (id, v);
        addLog ("Band " + juce::String (slot + 1) + ": master balance undone");
    };
}

void QuintessenceProcessor::setOffset (const juce::Identifier& id, float v)
{
    apvts.state.setProperty (id, v, nullptr);   // saved with the project
}

std::function<void()> QuintessenceProcessor::narrower (uint32_t data)
{
    // The problem watcher: this track is wider than the mix can take in mono (or than the side
    // should ever be against the middle): narrower, and the width target follows, so the
    // automation doesn't widen it back
    if ((data & 0xfffu) != (getInstanceId() & 0xfffu) || value (qn::widthLockId) > 0.5f) return {};
    const auto before = value (qn::widthId), after = juce::jmax (0.0f, before * 0.75f);
    if (before - after < 0.01f) return {};
    const auto offBefore = sideOffset();
    setOffset (sideOffsetKey, juce::jmax (-12.0f, offBefore - 3.0f));
    if (pending.kind == Pending::width) pending.kind = Pending::none;
    write (qn::widthId, after);
    addLog ("Width " + juce::String (juce::roundToInt (before * 100.0f)) + "% -> " + juce::String (juce::roundToInt (after * 100.0f))
            + "% (Mentalism: the mix loses too much in mono, and this is one of its widest parts)");
    return [this, before, after, offBefore]
    {
        setOffset (sideOffsetKey, offBefore);
        if (std::abs (value (qn::widthId) - after) < 0.01f) write (qn::widthId, before);
        addLog ("Narrowing undone");
    };
}

std::function<void()> QuintessenceProcessor::lessSquash()
{
    // The problem watcher: the drops are squashed (their peaks barely stand over their loudness).
    // Every Quintessence eases off what its automation set: the multiband bands 2 dB higher, the
    // glue 1.5 dB lighter, and the targets follow so they stay there
    if (value (qn::autoMbId) < 0.5f) return {};
    std::vector<std::pair<juce::String, float>> before;
    for (int b = 0; b < qn::numMbBands; ++b) { before.push_back ({ qn::thrId (b), value (qn::thrId (b)) }); before.push_back ({ qn::ratioId (b), value (qn::ratioId (b)) }); }
    before.push_back ({ qn::glueThrId, value (qn::glueThrId) });
    juce::StringArray moved;
    for (int b = 0; b < qn::numMbBands; ++b)
    {
        if (value (qn::mbLockId (b)) > 0.5f || value (qn::ratioId (b)) <= 1.01f) continue;
        const auto thr = value (qn::thrId (b)) + 2.0f;
        if (thr >= -0.5f) { write (qn::thrId (b), 0.0f); write (qn::ratioId (b), 1.0f); moved.add (qn::mbBandNames()[b].toLowerCase() + " band released"); }
        else { write (qn::thrId (b), thr); moved.add (qn::mbBandNames()[b].toLowerCase() + " band at " + juce::String (thr, 1) + " dB"); }
    }
    if (const auto g = value (qn::glueThrId); g < -0.05f)
    {
        write (qn::glueThrId, juce::jmin (-0.1f, g + 1.5f));
        moved.add ("glue at " + juce::String (juce::jmin (-0.1f, g + 1.5f), 1) + " dB");
    }
    if (moved.isEmpty()) return {};
    const auto offBefore = swingOffset();
    setOffset (swingOffsetKey, juce::jmin (6.0f, offBefore + 2.0f));
    if (pending.kind == Pending::mb || pending.kind == Pending::glue) pending.kind = Pending::none;
    addLog ("Dynamics eased off: " + moved.joinIntoString (", ") + " (Mentalism: the drops are squashed)");
    return [this, before, offBefore]
    {
        setOffset (swingOffsetKey, offBefore);
        for (const auto& [id, v] : before) write (id, v);
        addLog ("Easing off undone");
    };
}

bool QuintessenceProcessor::budgetMove (const Measurement& m, int role)
{
    // Tone budget: the vocal owns 2 kHz (where its words are), the lead 4 kHz over the pads.
    // Another part that's strong there gives it room: a gentle 1.5 dB dip that opens only while
    // the owner plays.
    if (value (qn::autoEqId) < 0.5f || (m.group != 0 && m.group != 3)) return false;
    const auto track = getTrackName();
    bool vocal = false, lead = false;
    juce::String vocalTrack, leadTrack;
    for (const auto& e : suite::Registry::list())
    {
        if ((e.plugin != "Prima Materia" && e.plugin != "Quintessence") || (track.isNotEmpty() && e.track == track) || e.id == getInstanceId()) continue;
        const auto r = (int) e.metrics[0];
        if (r == 6) { vocal = true; vocalTrack = e.track; }
        if (r == 5) { lead = true; leadTrack = e.track; }
    }
    int band = -1, when = 0;
    juce::String owner, ownerTrack;
    if (vocal && (role == 4 || role == 5 || role == 7)) { band = 6; when = 1; owner = "vocal"; ownerTrack = vocalTrack; }
    else if (lead && role == 4) { band = 7; when = 2; owner = "lead"; ownerTrack = leadTrack; }
    if (band < 0 || budgetDone == band) return false;
    float loudest = -200.0f;
    for (auto b : m.bandsDb) loudest = juce::jmax (loudest, b);
    budgetDone = band;
    if (m.bandsDb[(size_t) band] < loudest - 18.0f) return false;   // hardly anything there: no need
    const auto slot = slotFor (band, true);
    if (slot < 0) return false;
    if ((int) value (qn::reasonId (slot)) == qn::masking) return false;   // a carve is already there
    const auto centre = analysis::bandCentres()[(size_t) band];
    pending = { Pending::budget, slot, band,
                { { qn::freqId (slot), value (qn::freqId (slot)) }, { qn::gainId (slot), value (qn::gainId (slot)) }, { qn::qId (slot), value (qn::qId (slot)) },
                  { qn::reasonId (slot), value (qn::reasonId (slot)) }, { qn::whenId (slot), value (qn::whenId (slot)) }, { qn::bdId (slot), value (qn::bdId (slot)) }, { qn::grId (slot), value (qn::grId (slot)) } },
                toneError (m, role), "room for the " + owner + " at " + hzText (centre) };
    write (qn::freqId (slot), centre);
    write (qn::qId (slot), 1.0f);
    write (qn::reasonId (slot), (float) qn::masking);
    write (qn::whenId (slot), (float) when);
    write (qn::bdId (slot), 0.0f); write (qn::grId (slot), 0.0f);
    write (qn::gainId (slot), -1.5f);
    addLog ("Band " + juce::String (slot + 1) + ": " + hzText (centre) + " -1.5 dB while the " + owner + " plays: room for the " + owner
            + (ownerTrack.isNotEmpty() ? " ('" + ownerTrack + "')" : juce::String()) + ", which owns that part of the spectrum");
    return true;
}

bool QuintessenceProcessor::punchMove (const Measurement& m, int role)
{
    const auto target = qn::targets::punchTarget (role);
    if (target <= 0.0f || value (qn::autoMbId) < 0.5f || m.group == 1 || m.punchDb <= 0.0f || punchCooldown > 0) return false;
    const auto err = m.punchDb - target;
    if (std::abs (err) < 1.5f) return false;
    const auto punch = value (qn::punchId);
    const auto next = juce::jlimit (-1.0f, 1.0f, punch + (err < 0.0f ? 0.25f : -0.25f));
    if (std::abs (next - punch) < 0.01f) return false;
    pending = { Pending::punch, -1, -1, { { qn::punchId, punch } }, std::abs (err), "punch " + juce::String (juce::roundToInt (next * 100.0f)) + "%" };
    write (qn::punchId, next);
    addLog ("Punch " + juce::String (juce::roundToInt (punch * 100.0f)) + "% -> " + juce::String (juce::roundToInt (next * 100.0f)) + "% (its hits stand "
            + juce::String (m.punchDb, 1) + " dB over its body; a " + juce::String (qn::targets::roles()[(size_t) role].name) + " usually has about " + juce::String (juce::roundToInt (target)) + ")");
    return true;
}

bool QuintessenceProcessor::glueMove (const Measurement& m, int role)
{
    // The music bus, and (with the autopilot's mixing) a drum bus: a track named drums, perc or beats
    const auto track = getTrackName().toLowerCase();
    const auto drumBus = suite::Autopilot::on (suite::Autopilot::mixing) && (track.contains ("drum") || track.contains ("perc") || track.contains ("beat"));
    if ((role != qn::targets::musicBus && ! drumBus) || value (qn::autoMbId) < 0.5f || m.group == 1 || m.glueLevelDb < -80.0f) return false;
    if (drumBus && ! drumBusCounted) { drumBusCounted = true; suite::Autopilot::count (suite::Autopilot::drumBusGlued); }
    const auto thr = value (qn::glueThrId);
    const auto on = thr < -0.05f;
    const auto err = (on ? m.glueGrDb : 0.0f) - glueTarget();
    if (on && std::abs (err) < 0.5f) return false;
    // First: 3 dB under the bus's average level (about 1.5 dB of reduction at 2:1); then 1.5 dB steps towards the target
    const auto next = juce::jlimit (-40.0f, -0.1f, on ? thr + (err < 0.0f ? -1.5f : 1.5f) : std::round (m.glueLevelDb - 3.0f));
    if (std::abs (next - thr) < 0.05f) return false;
    pending = { Pending::glue, -1, -1, { { qn::glueThrId, thr } }, std::abs (err), "glue at " + juce::String (next, 1) + " dB" };
    write (qn::glueThrId, next);
    addLog ("Glue " + (on ? "threshold " + juce::String (thr, 1) + " -> " : juce::String ("on at ")) + juce::String (next, 1) + " dB (2:1, release an 8th note; "
            + (on ? "it was holding " + juce::String (m.glueGrDb, 1) + " dB on average" : juce::String ("the bus averages ") + juce::String (m.glueLevelDb, 1) + " dB")
            + ", aiming for " + juce::String (glueTarget(), 1) + ")");
    return true;
}

bool QuintessenceProcessor::deessMove (const Measurement& m, int role)
{
    // The vocal's de-esser; with the autopilot's harshness group on, the same spike tamer for hats,
    // leads, plucks and FX (stricter: their spikes must stand further out, and it holds less)
    const auto tamer = role != 6;
    if ((tamer && ! (suite::Autopilot::on (suite::Autopilot::harshness) && (role == 3 || role == 5 || role == 7)))
        || value (qn::autoMbId) < 0.5f || m.group == 1 || deessCooldown > 0) return false;
    const auto extra = tamer ? 2.0f : 0.0f;
    const auto depth = value (qn::deessDepthId);
    if (depth < 0.05f)
    {
        // Where the sibilance is: of the bands that spike, the one whose spikes are loudest
        int best = -1;
        for (int k = 0; k < 4; ++k)
            if (m.sibSwing[(size_t) k] >= qn::targets::sibilanceSwingDb + extra && m.sibPeak[(size_t) k] > m.p95Pre[1] - 30.0f
                && m.sibSwing[(size_t) k] > (m.p95Pre[1] - m.p50Pre[1]) + 4.0f + extra   // spikier than the part itself: 's' sounds and harsh transients, not its phrasing
                && (best < 0 || m.sibPeak[(size_t) k] > m.sibPeak[(size_t) best])) best = k;
        if (best < 0) return false;
        const auto hz = qn::sibilanceCentres[(size_t) best];
        pending = { Pending::deess, -1, -1, { { qn::deessFreqId, value (qn::deessFreqId) }, { qn::deessDepthId, depth } }, m.sibSwing[(size_t) best],
                    "de-esser at " + juce::String (hz / 1000.0f, 1) + " kHz" };
        write (qn::deessFreqId, hz);
        write (qn::deessDepthId, tamer ? 4.0f : 6.0f);
        if (tamer)
        {
            suite::Autopilot::count (suite::Autopilot::spikesTamed);
            addLog ("Spike tamer on at " + juce::String (hz / 1000.0f, 1) + " kHz, up to 4 dB (its harsh moments spike " + juce::String (m.sibSwing[(size_t) best], 1)
                    + " dB over its usual level there; only the spikes are held)");
            return true;
        }
        addLog ("De-esser on at " + juce::String (hz / 1000.0f, 1) + " kHz, up to 6 dB (the 's' sounds spike " + juce::String (m.sibSwing[(size_t) best], 1)
                + " dB over the vocal's usual level there; it acts only on the spikes)");
        return true;
    }
    if (m.sibPostSwing > qn::targets::sibilanceSwingDb + 2.0f + extra && depth < (tamer ? 6.0f : 10.0f))
    {
        pending = { Pending::deess, -1, -1, { { qn::deessDepthId, depth } }, m.sibPostSwing, "de-esser up to " + juce::String (depth + 2.0f, 0) + " dB" };
        write (qn::deessDepthId, depth + 2.0f);
        addLog ("De-esser deeper: up to " + juce::String (depth + 2.0f, 0) + " dB (the spikes still stand " + juce::String (m.sibPostSwing, 1) + " dB out)");
        return true;
    }
    return false;
}

void QuintessenceProcessor::drainSpectrum()
{
    const auto ready = fifo.getNumReady();
    if (ready <= 0) return;
    int s1, n1, s2, n2;
    fifo.prepareToRead (ready, s1, n1, s2, n2);
    if (n1 > 0) resonances.add (fifoData.data() + s1, n1);
    if (n2 > 0) resonances.add (fifoData.data() + s2, n2);
    fifo.finishedRead (n1 + n2);
}

bool QuintessenceProcessor::widthMove (const Measurement& m, int role)
{
    const auto& t = qn::targets::roles()[(size_t) role];
    if (value (qn::autoEqId) < 0.5f || t.sideDb == 0.0f || m.group == 1 || widthCooldown > 0 || value (qn::widthLockId) > 0.5f) return false;
    const auto now = m.sideMinusMid();
    if (now < -90.0f) return false;
    const auto diff = now - (t.sideDb + sideOffset());
    if (std::abs (diff) <= 2.0f) return false;
    if (diff < 0.0f && now < -40.0f) return false;   // (nearly) mono: there's no side to widen
    const auto width = value (qn::widthId);
    const auto next = juce::jlimit (0.0f, 2.0f, width * juce::Decibels::decibelsToGain (juce::jlimit (-2.0f, 2.0f, -diff)));
    if (std::abs (next - width) < 0.01f) { widthCooldown = 8; return false; }
    pending = { Pending::width, -1, -1, { { qn::widthId, width } }, std::abs (diff), "width " + juce::String (juce::roundToInt (next * 100.0f)) + "%" };
    write (qn::widthId, next);
    addLog ("Width " + juce::String (juce::roundToInt (width * 100.0f)) + "% -> " + juce::String (juce::roundToInt (next * 100.0f)) + "% (the side sits "
            + juce::String (-now, 1) + " dB under the middle; a " + juce::String (t.name) + " is usually about " + juce::String (juce::roundToInt (-t.sideDb)) + ")");
    return true;
}

bool QuintessenceProcessor::resonanceMove (const Measurement& m, int role)
{
    if (value (qn::autoEqId) < 0.5f || m.group == 1) return false;
    // Autopilot: tell the suite the resonance this track hears most, so tracks ringing at the same
    // frequency share the cut (they join in sooner, and each cuts less)
    const auto sharing = suite::Autopilot::on (suite::Autopilot::harshness);
    {
        float strongest = 0.0f, prom = 0.0f;
        for (const auto& c : candidates) if (c.passes >= 2 && c.prominence > prom) { prom = c.prominence; strongest = c.hz; }
        if (sharing) suite::Autopilot::postResonance (getInstanceId(), strongest);
    }
    for (const auto& c : candidates)
    {
        const auto others = sharing ? suite::Autopilot::othersRingingAt (getInstanceId(), c.hz) : 0;
        if (others == 0 && (c.passes < 3 || c.seconds < 12.0f)) continue;   // there pass after pass, for 12 s of music: not a passing note
        if (others > 0 && (c.passes < 2 || c.seconds < 6.0f)) continue;     // shared: sooner
        if (std::any_of (resonanceCooldown.begin(), resonanceCooldown.end(), [&] (const std::pair<float, int>& cd) { return std::abs (std::log2 (cd.first / c.hz)) < 0.05f; }))
            continue;
        // A resonance band already there: deeper; otherwise a free band
        int slot = -1;
        for (int s = 0; s < qn::numSlots; ++s)
            if ((int) value (qn::reasonId (s)) == qn::resonance && value (qn::lockId (s)) < 0.5f && std::abs (std::log2 (value (qn::freqId (s)) / c.hz)) < 0.05f) slot = s;
        if (slot < 0)
            for (int s = 0; s < qn::numSlots; ++s)
                if (value (qn::lockId (s)) < 0.5f && (int) value (qn::reasonId (s)) == qn::free) { slot = s; break; }
        if (slot < 0) return false;
        const auto again = (int) value (qn::reasonId (slot)) == qn::resonance;
        const auto share = others > 0 ? 2.0f / (float) (others + 2) : 1.0f;   // two tracks: 1.5 dB each instead of 3
        const auto gain = again ? juce::jmax (-6.0f * share, value (qn::gainId (slot)) - 1.5f * share) : std::round (-3.0f * share * 2.0f) * 0.5f;
        if (others > 0 && ! again) suite::Autopilot::count (suite::Autopilot::sharedResonances);
        if (again && std::abs (gain - value (qn::gainId (slot))) < 0.05f) continue;   // as deep as it goes
        pending = { Pending::resonance, slot, -1,
                    { { qn::freqId (slot), value (qn::freqId (slot)) }, { qn::gainId (slot), value (qn::gainId (slot)) },
                      { qn::qId (slot), value (qn::qId (slot)) }, { qn::reasonId (slot), value (qn::reasonId (slot)) }, { qn::bdId (slot), value (qn::bdId (slot)) }, { qn::grId (slot), value (qn::grId (slot)) } },
                    c.prominence, "band " + juce::String (slot + 1) + " " + juce::String (juce::roundToInt (c.hz)) + " Hz " + dbText (gain) + " (resonance)",
                    toneError (m, role) };
        write (qn::freqId (slot), c.hz);
        write (qn::qId (slot), 6.0f);
        write (qn::reasonId (slot), (float) qn::resonance);
        write (qn::bdId (slot), 0.0f); write (qn::grId (slot), 0.0f);
        write (qn::gainId (slot), gain);
        addLog ("Band " + juce::String (slot + 1) + ": " + juce::String (juce::roundToInt (c.hz)) + " Hz " + dbText (gain) + ", narrow (a ringing peak "
                + juce::String (c.prominence, 1) + " dB above the sound around it, there for " + juce::String (juce::roundToInt (c.seconds)) + " s"
                + (others > 0 ? juce::String (", shared with ") + juce::String (others) + (others == 1 ? " other track ringing there" : " other tracks ringing there") : juce::String()) + ")");
        return true;
    }
    return false;
}

juce::String QuintessenceProcessor::spireStart() const
{
    return qn::spire::startingPoint (roleShown.load(), groupShown.load() == 1);
}

int QuintessenceProcessor::slotFor (int band, bool forCarve) const
{
    const auto centre = analysis::bandCentres()[(size_t) band];
    auto reason = [this] (int s) { return (int) value (qn::reasonId (s)); };
    auto locked = [this] (int s) { return value (qn::lockId (s)) > 0.5f; };
    auto near = [&] (int s) { return std::abs (std::log2 (value (qn::freqId (s)) / centre)) < 0.35f; };

    // A band already working at this frequency for the same kind of reason
    for (int s = 0; s < qn::numSlots; ++s)
    {
        if (locked (s) || ! near (s)) continue;
        const auto r = reason (s);
        if (forCarve ? (r == qn::masking || r == qn::mud || r == qn::harshness) : (r == qn::roleTone || (r == qn::yours && std::abs (value (qn::gainId (s))) < 0.05f)))
            return s;
    }
    // A free band (the nearest by its resting place), or one handed back by unlocking it
    int best = -1;
    for (int s = 0; s < qn::numSlots; ++s)
    {
        if (locked (s)) continue;
        const auto r = reason (s);
        if (r != qn::free && ! (r == qn::yours && std::abs (value (qn::gainId (s))) < 0.05f)) continue;
        if (best < 0 || std::abs (std::log2 (qn::defaultFreqs()[(size_t) s] / centre)) < std::abs (std::log2 (qn::defaultFreqs()[(size_t) best] / centre))) best = s;
    }
    if (best >= 0 || ! forCarve) return best;
    // A fix outranks tone shaping: take over a role-tone band
    for (int s = 0; s < qn::numSlots; ++s)
        if (! locked (s) && reason (s) == qn::roleTone) return s;
    return -1;
}

bool QuintessenceProcessor::eqMove (const Measurement& m, int role)
{
    if (value (qn::autoEqId) < 0.5f) return false;
    const auto& t = qn::targets::roles()[(size_t) role];

    // Low cut where the part has no job (once; it's checked like any move)
    const auto breakdown = m.group == 1, groove = m.group == 3;   // each gets its own correction
    if (! breakdown && t.lowCutHz > 0.0f && ! lowCutTried && value (qn::lowCutLockId) < 0.5f && value (qn::lowCutId) <= qn::lowCutOff)
    {
        float below = -200.0f, loudest = -200.0f;
        for (int b = 0; b < 10; ++b)
        {
            loudest = juce::jmax (loudest, m.bandsDb[(size_t) b]);
            if (analysis::bandCentres()[(size_t) b] < t.lowCutHz * 0.75f) below = juce::jmax (below, m.bandsDb[(size_t) b]);
        }
        lowCutTried = true;
        if (below > loudest - 20.0f)
        {
            float body = -200.0f;
            for (int b = 2; b < 10; ++b) body = juce::jmax (body, m.bandsDb[(size_t) b]);
            pending = { Pending::lowCut, -1, -1, { { qn::lowCutId, value (qn::lowCutId) } }, juce::jmax (m.bandsDb[0], m.bandsDb[1]),
                        "low cut at " + hzText (t.lowCutHz), body };
            write (qn::lowCutId, t.lowCutHz);
            addLog ("Low cut at " + hzText (t.lowCutHz) + ": a " + juce::String (t.name) + " has no job down there, and this one had lows "
                    + juce::String (juce::roundToInt (loudest - below)) + " dB under its loudest");
            return true;
        }
    }

    // Tone: the band furthest from the role's usual shape, a step at a time
    const auto diff = toneDiff (m, role);
    const auto before = toneError (m, role);
    if (before < 0.0f) return false;
    std::vector<int> order;
    for (int b = 0; b < 10; ++b) if (std::abs (diff[(size_t) b]) > 2.0f && toneCooldown[(size_t) b] == 0) order.push_back (b);
    std::sort (order.begin(), order.end(), [&] (int a, int b) { return std::abs (diff[(size_t) a]) > std::abs (diff[(size_t) b]); });
    for (const auto band : order)
    {
        const auto slot = slotFor (band, false);
        if (slot < 0) continue;
        const auto err = diff[(size_t) band];
        // Bigger steps while a new track is far off; small ones once it's close
        const auto maxStep = passes.load() < 8 && std::abs (err) > 4.0f ? 2.5f : 1.5f;
        const auto step = std::round (juce::jlimit (0.5f, maxStep, std::abs (err) * 0.5f) * 2.0f) * 0.5f * (err > 0.0f ? -1.0f : 1.0f);
        const auto fresh = (int) value (qn::reasonId (slot)) != qn::roleTone;
        // In breakdowns the move goes into the band's breakdown gain (on top of its main gain), so
        // the breakdown gets its own correction without touching the drops
        const auto gainNow = fresh ? 0.0f : value (qn::gainId (slot));
        const auto& id = breakdown ? qn::bdId (slot) : groove ? qn::grId (slot) : qn::gainId (slot);
        const auto current = fresh ? 0.0f : value (id);
        const auto next = breakdown || groove ? juce::jlimit (-qn::maxToneDb - gainNow, qn::maxToneDb - gainNow, current + step)
                                    : juce::jlimit (-qn::maxToneDb, qn::maxToneDb, current + step);
        if (std::abs (next - current) < 0.05f) { toneCooldown[(size_t) band] = 8; continue; }   // as far as it goes

        const auto centre = analysis::bandCentres()[(size_t) band];
        const auto where = breakdown ? juce::String (" in breakdowns") : groove ? juce::String (" in grooves, intro and outro") : juce::String();
        if (groove) suite::Autopilot::count (suite::Autopilot::sectionCorrections);
        pending = { Pending::eqSlot, slot, band,
                    { { qn::freqId (slot), value (qn::freqId (slot)) }, { qn::gainId (slot), value (qn::gainId (slot)) },
                      { qn::qId (slot), value (qn::qId (slot)) }, { qn::reasonId (slot), value (qn::reasonId (slot)) },
                      { qn::bdId (slot), value (qn::bdId (slot)) }, { qn::grId (slot), value (qn::grId (slot)) } },
                    before,
                    "band " + juce::String (slot + 1) + " " + hzText (centre) + " " + dbText (next) + where };
        write (qn::freqId (slot), centre);
        write (qn::qId (slot), 1.0f);
        write (qn::reasonId (slot), (float) qn::roleTone);
        if (fresh) { write (qn::gainId (slot), 0.0f); write (qn::bdId (slot), 0.0f); write (qn::grId (slot), 0.0f); }
        write (id, next);
        const auto yours = learnedSongs (role) >= 3;
        addLog ("Band " + juce::String (slot + 1) + where + ": " + hzText (centre) + " " + dbText (next) + " (" + juce::String (std::abs (err), 1) + " dB "
                + (err > 0.0f ? "more" : "less") + " there than " + (yours ? "your " + juce::String (t.name) + "s usually have" : "a " + juce::String (t.name) + " usually has") + ")");
        return true;
    }
    return false;
}

bool QuintessenceProcessor::mbMove (const Measurement& m, int role)
{
    if (value (qn::autoMbId) < 0.5f || m.group == 1) return false;   // dynamics are set in the main sections
    const auto& t = qn::targets::roles()[(size_t) role];
    // Bands with something in them (not just the edges of another band's sound), furthest from their target first
    float loudest = -200.0f;
    for (int b = 0; b < qn::numMbBands; ++b) loudest = juce::jmax (loudest, m.p95Pre[(size_t) b]);
    std::vector<int> order;
    for (int b = 0; b < qn::numMbBands; ++b)
        if (t.swing[(size_t) b] > 0.0f && m.p95Pre[(size_t) b] > loudest - 30.0f) order.push_back (b);
    std::sort (order.begin(), order.end(), [&] (int a, int b)
               { return std::abs (m.swingPost (a) - t.swing[(size_t) a]) > std::abs (m.swingPost (b) - t.swing[(size_t) b]); });
    for (const auto b : order)
    {
        const auto target = t.swing[(size_t) b] + swingOffset();   // the drops let breathe: more swing allowed
        if (value (qn::mbLockId (b)) > 0.5f || mbCooldown[(size_t) b] > 0 || m.windowsPost[(size_t) b] < 20) continue;
        const auto swing = m.swingPost (b);
        const auto thr = value (qn::thrId (b)), ratio = value (qn::ratioId (b));
        float newThr = thr, newRatio = ratio;
        juce::String why;
        if (swing > target + 1.0f)
        {
            if (ratio <= 1.01f) { newRatio = t.ratio; newThr = std::round ((m.p50Pre[(size_t) b] + target) * 2.0f) * 0.5f; }
            else if (thr > m.p50Pre[(size_t) b] - 6.0f) newThr = thr - 2.0f;
            else continue;   // holding as hard as it should
            why = "its loud moments stand " + juce::String (swing, 1) + " dB out; a " + juce::String (t.name) + " wants about " + juce::String (juce::roundToInt (target));
        }
        else if (swing < target - 2.0f && ratio > 1.01f)
        {
            newThr = thr + 2.0f;
            if (newThr >= m.p95Pre[(size_t) b]) { newThr = 0.0f; newRatio = 1.0f; }
            why = "it's held more than it needs (" + juce::String (swing, 1) + " dB, wants " + juce::String (juce::roundToInt (target)) + ")";
        }
        else continue;

        newThr = juce::jlimit (-60.0f, 0.0f, newThr);
        pending = { Pending::mb, b, -1, { { qn::thrId (b), thr }, { qn::ratioId (b), ratio } }, std::abs (swing - target),
                    qn::mbBandNames()[b].toLowerCase() + " band " + (newRatio <= 1.01f ? juce::String ("released") : "at " + juce::String (newThr, 1) + " dB, " + juce::String (newRatio, 1) + ":1") };
        write (qn::thrId (b), newThr);
        write (qn::ratioId (b), newRatio);
        addLog ("Dynamics, " + qn::mbBandNames()[b].toLowerCase() + " band: " + (newRatio <= 1.01f ? juce::String ("released") : juce::String (newThr, 1) + " dB, " + juce::String (newRatio, 1) + ":1")
                + " (" + why + ")");
        return true;
    }
    return false;
}

void QuintessenceProcessor::relearn()
{
    suite::Conductor::release (getInstanceId());
    budgetDone = -1;
    for (int s = 0; s < qn::numSlots; ++s)
        if (value (qn::lockId (s)) < 0.5f && ((int) value (qn::reasonId (s)) == qn::roleTone || (int) value (qn::reasonId (s)) == qn::resonance))
        {
            write (qn::gainId (s), 0.0f);
            write (qn::freqId (s), qn::defaultFreqs()[(size_t) s]);
            write (qn::qId (s), 1.0f);
            write (qn::bdId (s), 0.0f);
            write (qn::reasonId (s), (float) qn::free);
        }
    if (value (qn::lowCutLockId) < 0.5f) write (qn::lowCutId, 20.0f);
    if (value (qn::widthLockId) < 0.5f) write (qn::widthId, 1.0f);
    write (qn::punchId, 0.0f);
    write (qn::glueThrId, 0.0f);
    write (qn::deessDepthId, 0.0f);
    candidates.clear();
    resonanceCooldown.clear();
    widthCooldown = 0;
    for (int b = 0; b < qn::numMbBands; ++b)
        if (value (qn::mbLockId (b)) < 0.5f) { write (qn::thrId (b), 0.0f); write (qn::ratioId (b), 1.0f); }
    pending.kind = Pending::none;
    toneCooldown.fill (0); mbCooldown.fill (0);
    lowCutTried = false;
    haveBands = false;
    loadLearned();
    requestReset();
    addLog ("Re-learning: automatic moves cleared (masking fixes and your own bands stay)");
}

//==============================================================================
uint32_t QuintessenceProcessor::primaMateriaIdHere() const
{
    const auto track = getTrackName();
    if (track.isEmpty()) return 0xffffffffu;
    for (const auto& e : suite::Registry::list())
        if (e.plugin == "Prima Materia" && e.track == track) return e.id & 0xfffu;
    return 0xffffffffu;
}

std::function<void()> QuintessenceProcessor::carve (uint32_t data)
{
    const auto target = data & 0xfffu;
    if (target != (getInstanceId() & 0xfffu) && target != primaMateriaIdHere()) return {};   // another track's
    const auto band = (int) ((data >> 12) & 0xf);
    const auto when = (int) ((data >> 16) & 0x3u);
    if (band >= analysis::numBands) return {};
    const auto slot = slotFor (band, true);
    if (slot < 0) { addLog ("A masking fix arrived, but every band is locked or busy"); return {}; }

    const auto reason = when > 0 ? qn::masking : band == 3 ? qn::mud : (band == 6 || band == 7) ? qn::harshness : qn::masking;
    const auto r = (int) value (qn::reasonId (slot));
    const auto again = r == qn::masking || r == qn::mud || r == qn::harshness;
    const auto gain = again ? juce::jmax (-9.0f, value (qn::gainId (slot)) - 1.5f) : -3.0f;   // again: deeper
    const auto centre = analysis::bandCentres()[(size_t) band];

    std::vector<std::pair<juce::String, float>> before;
    for (const auto& id : { qn::freqId (slot), qn::gainId (slot), qn::qId (slot), qn::reasonId (slot), qn::whenId (slot), qn::bdId (slot), qn::grId (slot) }) before.push_back ({ id, value (id) });
    if (pending.kind == Pending::eqSlot && pending.index == slot) pending.kind = Pending::none;
    write (qn::freqId (slot), centre);
    write (qn::qId (slot), 1.4f);
    write (qn::reasonId (slot), (float) reason);
    write (qn::whenId (slot), (float) when);
    write (qn::gainId (slot), gain);
    const auto notInBreakdowns = suite::carveNotInBreakdowns (data);
    write (qn::bdId (slot), notInBreakdowns ? -gain : 0.0f);   // heard in the drops: not carved where the music thins out
    addLog ("Band " + juce::String (slot + 1) + ": " + hzText (centre) + " " + dbText (gain) + (notInBreakdowns ? " (not in breakdowns)" : "") + " for "
            + qn::reasonNames()[reason].toLowerCase() + (when == 1 ? ", only while the vocal sings" : when == 2 ? ", only while the lead plays" : "") + " (from Mentalism)");

    return [this, before, slot, gain]
    {
        if (std::abs (value (qn::gainId (slot)) - gain) > 0.05f) return;   // moved since: that wins
        for (const auto& [id, v] : before) write (id, v);
        addLog ("Band " + juce::String (slot + 1) + ": fix undone");
    };
}

//==============================================================================
QuintessenceProcessor::SuiteStatus QuintessenceProcessor::suiteStatus()
{
    SuiteStatus s;
    int active = 0;
    for (int sl = 0; sl < qn::numSlots; ++sl) if (std::abs (value (qn::gainId (sl))) > 0.05f) ++active;
    float gr = 0.0f;
    for (auto& g : grNow) gr = juce::jmax (gr, g.load());
    s.metrics = { (float) effectiveRole(), (float) active, gr, sideNow.load() };   // [3]: the side under the middle (dB): the stereo field map
    if (haveBands) { s.flags |= suite::Status::hasBands; s.bands = longBands; }
    // Squashed before it arrives: a kick, percussion or vocal with almost no crest left
    {
        const auto role = effectiveRole();
        const auto limit = role == 1 ? 8.0f : role == 3 ? 7.0f : role == 6 ? 6.0f : -1.0f;
        if (limit > 0.0f && inCrestDb.load() > -50.0f && inCrestDb.load() < limit) s.flags |= suite::Status::squashedInput;
    }
    return s;
}

juce::AudioProcessorEditor* QuintessenceProcessor::createEditor()
{
    return new QuintessenceEditor (*this);
}

void QuintessenceProcessor::getPluginState (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
    {
        auto* l = xml->createNewChildElement ("Log");
        l->addTextElement (moveLog().joinIntoString ("\n"));
        copyXmlToBinary (*xml, destData);
    }
}

void QuintessenceProcessor::setPluginState (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            if (auto* l = xml->getChildByName ("Log"))
            {
                const juce::ScopedLock sl (logLock);
                log = juce::StringArray::fromLines (l->getAllSubText());
                log.removeEmptyStrings();
                xml->removeChildElement (l, true);
            }
            autoWriting.store (true);   // a project loading isn't a manual edit
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            autoWriting.store (false);
            pendingLocks.store (0);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new QuintessenceProcessor();
}
