#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include "TargetBus.h"
#include "Checklist.h"
#include "Registry.h"
#include "StyleLog.h"

//==============================================================================
ReferenceProfile ReferenceProfile::analyse (const juce::File& file, const std::atomic<bool>* cancel)
{
    ReferenceProfile profile;
    profile.name = file.getFileNameWithoutExtension();

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->sampleRate <= 0.0)
        return profile;

    auto meterL = std::make_unique<analysis::LoudnessMeter>();
    auto meterTp = std::make_unique<analysis::TruePeakMeter>();
    auto meterBands = std::make_unique<analysis::BandAnalyzer>();
    meterL->prepare (reader->sampleRate);
    meterTp->reset();
    meterBands->prepare (reader->sampleRate);

    constexpr int chunk = 1 << 15;
    juce::AudioBuffer<float> buffer (2, chunk);
    for (juce::int64 pos = 0; pos < reader->lengthInSamples; pos += chunk)
    {
        if (cancel != nullptr && cancel->load())
            return profile;
        const auto n = (int) std::min<juce::int64> (chunk, reader->lengthInSamples - pos);
        reader->read (&buffer, 0, n, pos, true, true);
        const auto* l = buffer.getReadPointer (0);
        const auto* r = reader->numChannels > 1 ? buffer.getReadPointer (1) : l;
        for (int i = 0; i < n; ++i)
        {
            meterL->process (l[i], r[i]);
            meterTp->process (l[i], r[i]);
            meterBands->process (l[i], r[i]);
        }
    }

    profile.lufs = meterL->integrated();
    profile.truePeakDb = meterTp->peakDb();
    for (size_t b = 0; b < (size_t) analysis::numBands; ++b)
        profile.bands[b] = meterBands->longDb[b].load();
    profile.valid = profile.lufs > -70.0f;
    return profile;
}

//==============================================================================
MentalismProcessor::MentalismProcessor()
    : SuiteProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "MentalismState", Params::createLayout())
{
    mySound = MySound::load (MySound::file());
    refreshCoachThresholds();
    timerCallback();
    startTimerHz (4);
}

void MentalismProcessor::timerCallback()
{
    // Tell Lapis (and anything else listening) which loudness target we're measuring against.
    const auto r = report();
    suite::TargetBus::publish (r.targetLufs, r.ceiling);
    runAutomation();
}

uint32_t MentalismProcessor::applyCoachFix (suite::Fix fix, uint32_t data, const suite::SectionMap& map)
{
    if (fix == suite::Fix::None) return 0;
    const auto seq = sendFix (fix, data);
    using T = suite::SectionType;
    if (fix == suite::Fix::ArcMatch)
    {
        int index = 0; float db = 0.0f;
        suite::unpackArc (data, index, db);
        forgetSection (index);
    }
    else
    {
        const auto type = fix == suite::Fix::BuildRise ? T::Build : fix == suite::Fix::IntroQuieter ? T::Intro
                        : fix == suite::Fix::LaterDropBigger ? T::Drop : T::Breakdown;
        forgetSections (type, map);
    }
    return seq;
}

juce::StringArray MentalismProcessor::finishSong()
{
    juce::StringArray done;
    const auto rep = report();
    sendFix (suite::Fix::GainStage);
    done.add ("Gain staging: every Prima Materia input to -18 dBFS RMS");
    for (const auto& item : checklist::check (rep, clippedSamples.load(), suite::Registry::list()))
        if (item.fix == suite::Fix::DjIntroOutro)
        {
            sendFix (suite::Fix::DjIntroOutro);
            done.add ("DJ intro/outro: melodic parts out of the intro and outro");
        }
    if (getReference().valid)
        done.add (matchReference() != 0 ? "Reference: Lapis' tone moved towards it, loudness target matched"
                                        : "Reference: loudness target matched (tone already close)");
    setParameterPlain (apvts, Params::autoCoach, 1.0f);
    autoPasses.store (0);
    for (int i = 0; i < rep.map.count; ++i)
        if (rep.map.sections[(size_t) i].type == suite::SectionType::Drop) forgetSection (i);
    finishMeasuredAt = rep.measuredSeconds;
    finishState.store (FinishStage::WaitingForDrop);
    {
        const juce::ScopedLock sl (historyLock);
        finishText = "Play the drop again: the rough mix is next.";
    }
    done.add ("Next: play the drop for the rough mix, then the whole song for the coach");
    return done;
}

void MentalismProcessor::keepPassForCompare()
{
    const auto lufs = loudness.integrated();
    if (bands.longSeconds.load() < 20.0f || lufs <= -70.0f) return;
    Pass p;
    p.valid = true;
    p.lufs = lufs;
    p.truePeakDb = truePeak.peakDb();
    for (size_t b = 0; b < p.bands.size(); ++b) p.bands[b] = bands.longDb[b].load();
    setPreviousPass (p);
}

juce::String MentalismProcessor::comparePasses (const Pass& before, const Pass& now)
{
    if (! before.valid || ! now.valid) return {};
    const auto d = now.lufs - before.lufs;
    juce::String text = "Since the last pass: ";
    if (std::abs (d) < 0.3f) text << "the same loudness";
    else text << juce::String (std::abs (d), 1) << " LU " << (d > 0.0f ? "louder" : "quieter")
              << " (turn the master " << (d > 0.0f ? "down " : "up ") << juce::String (std::abs (d), 1) << " dB to compare at equal loudness)";
    const auto a = advisor::normalise (before.bands), b = advisor::normalise (now.bands);
    std::vector<std::pair<float, int>> changes;
    for (int i = 0; i < (int) a.size(); ++i)
        if (std::abs (b[(size_t) i] - a[(size_t) i]) >= 1.5f) changes.push_back ({ b[(size_t) i] - a[(size_t) i], i });
    std::sort (changes.begin(), changes.end(), [] (const auto& x, const auto& y) { return std::abs (x.first) > std::abs (y.first); });
    juce::StringArray bandsText;
    for (size_t k = 0; k < std::min<size_t> (3, changes.size()); ++k)
    {
        const auto hz = analysis::bandCentres()[(size_t) changes[k].second];
        bandsText.add ((hz >= 1000.0f ? juce::String (hz / 1000.0f, 0) + " kHz " : juce::String (juce::roundToInt (hz)) + " Hz ")
                       + (changes[k].first > 0.0f ? "+" : "") + juce::String (changes[k].first, 1) + " dB");
    }
    text << (bandsText.isEmpty() ? "; the tonal balance is unchanged." : "; " + bandsText.joinIntoString (", ") + ".");
    return text;
}

juce::String MentalismProcessor::mixReport() const
{
    const auto rep = report();
    const auto& m = rep.measurement;
    juce::StringArray lines;
    lines.add ("MIX REPORT  " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M"));
    lines.add ("");
    lines.add ("Loudness:   " + (m.integratedLufs > -70.0f ? juce::String (m.integratedLufs, 1) + " LUFS integrated" : juce::String ("not measured"))
               + "  (target " + juce::String (rep.targetLufs, 1) + ": " + rep.targetName + ")");
    lines.add ("True peak:  " + (m.truePeakDb > -99.0f ? juce::String (m.truePeakDb, 1) + " dBTP" : juce::String ("not measured"))
               + "  (ceiling " + juce::String (rep.ceiling, 1) + ")");
    lines.add ("Stereo:     correlation " + juce::String (m.correlation, 2) + ", below 120 Hz " + juce::String (m.lowCorrelation, 2)
               + ", mono sum " + juce::String (juce::jmax (0.0f, m.monoLossDb), 1) + " dB down");
    lines.add ("Measured:   " + juce::String ((int) rep.measuredSeconds) + " s");
    if (rep.tonalAvailable && rep.measuredSeconds > 2.0f)
    {
        juce::StringArray t;
        for (size_t b = 0; b < rep.longDiff.size(); ++b)
        {
            const auto hz = analysis::bandCentres()[b];
            t.add ((hz >= 1000.0f ? juce::String (hz / 1000.0f, 0) + "k" : juce::String (juce::roundToInt (hz))) + " "
                   + (rep.longDiff[b] >= 0.0f ? "+" : "") + juce::String (rep.longDiff[b], 1));
        }
        lines.add ("Tonal:      " + t.joinIntoString ("  ") + "  (dB against the tonal target)");
    }
    if (rep.sinceLastPass.isNotEmpty()) lines.add (rep.sinceLastPass);
    lines.add ("");
    lines.add ("SUGGESTIONS");
    for (const auto& a : rep.advice) lines.add ("  - " + a);
    if (rep.arrangementAvailable && rep.map.count > 0)
    {
        lines.add ("");
        lines.add ("ARRANGEMENT");
        for (int i = 0; i < rep.map.count && i < (int) rep.sections.size(); ++i)
            if (rep.sections[(size_t) i].heard())
                lines.add ("  " + juce::String (suite::sectionName (rep.map.sections[(size_t) i].type)) + " (" + coach::barsOf (rep.map, i) + "): "
                           + juce::String (rep.sections[(size_t) i].lufs(), 1) + " LUFS");
        for (const auto& n : rep.arrangement.notes) lines.add ("  - " + n);
    }
    lines.add ("");
    lines.add ("CHECKLIST");
    for (const auto& item : checklist::check (rep, clippedSamples.load(), suite::Registry::list()))
    {
        const auto mark = item.state == checklist::State::Pass ? "PASS" : item.state == checklist::State::Warn ? "WARN"
                        : item.state == checklist::State::Fail ? "FAIL" : " ?  ";
        lines.add (juce::String ("  [") + mark + "] " + item.name + ": " + item.detail);
    }
    const auto history = getFixHistory();
    if (! history.empty())
    {
        lines.add ("");
        lines.add ("FIXES APPLIED");
        for (const auto& f : history)
            lines.add ("  " + f.when.formatted ("%H:%M") + "  " + coach::describe (f.fix) + (f.undoSeq != 0 ? "  (undone)" : ""));
    }
    return lines.joinIntoString ("\n") + "\n";
}

juce::File MentalismProcessor::saveMixReport() const
{
    const auto f = reportFolder().getChildFile ("Mix report " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S") + ".txt");
    f.getParentDirectory().createDirectory();
    return f.replaceWithText (mixReport()) ? f : juce::File();
}

void MentalismProcessor::logFinishedSong (const Report& rep)
{
    using T = suite::SectionType;
    // The arc: the first breakdown against the drop after it, and the first build's rise.
    float contrast = 0.0f, rise = 0.0f;
    bool haveContrast = false, haveRise = false;
    for (int i = 0; i < rep.map.count && i < (int) rep.sections.size(); ++i)
    {
        const auto& st = rep.sections[(size_t) i];
        const auto type = rep.map.sections[(size_t) i].type;
        if (type == T::Breakdown && ! haveContrast && st.heard())
            for (int k = i + 1; k < rep.map.count && k < (int) rep.sections.size(); ++k)
                if (rep.map.sections[(size_t) k].type == T::Drop && rep.sections[(size_t) k].heard())
                {
                    contrast = rep.sections[(size_t) k].lufs() - st.lufs();
                    haveContrast = true;
                    break;
                }
        if (type == T::Build && ! haveRise && st.firstBlocks >= 5 && st.secondBlocks >= 5)
        {
            rise = st.secondLufs() - st.firstLufs();
            haveRise = true;
        }
    }
    if (haveContrast)
        suite::StyleLog::add ("arc", { { "contrast", juce::String (contrast, 1) }, { "rise", haveRise ? juce::String (rise, 1) : juce::String() } });

    // The balance: every Prima Materia track's level against the kick.
    float kick = -100.0f;
    const auto entries = suite::Registry::list();
    for (const auto& e : entries)
        if (e.plugin == "Prima Materia" && (int) e.metrics[0] == 1 && e.has (suite::Status::measuring)) kick = std::max (kick, e.metrics[1]);
    if (kick > -60.0f)
        for (const auto& e : entries)
            if (e.plugin == "Prima Materia" && (int) e.metrics[0] > 1 && e.has (suite::Status::measuring))
                suite::StyleLog::add ("balance", { { "role", juce::String ((int) e.metrics[0]) }, { "offset", juce::String (e.metrics[1] - kick, 1) } });
    refreshCoachThresholds();
}

void MentalismProcessor::refreshCoachThresholds()
{
    std::vector<float> contrasts, rises;
    for (const auto& e : suite::StyleLog::read())
        if (e.kind == "arc")
        {
            contrasts.push_back (e.get ("contrast").getFloatValue());
            if (e.get ("rise").isNotEmpty()) rises.push_back (e.get ("rise").getFloatValue());
        }
    const auto t = coach::learnThresholds (contrasts, rises);
    const juce::ScopedLock sl (historyLock);
    coachLimits = t;
}

void MentalismProcessor::watchMasking (const std::vector<masking::Clash>& clashes)
{
    std::map<juce::String, int> next;
    juce::StringArray lasting;
    for (const auto& c : clashes)
    {
        const auto key = c.carveTrack + "|" + c.keepTrack + "|" + juce::String (c.band);
        const auto count = (maskingCounts.count (key) > 0 ? maskingCounts[key] : 0) + 1;
        next[key] = count;
        if (count >= maskingChecksToSuggest)
            lasting.add ("Masking: " + c.text + " (for over a minute): try Carve on the Masking tab.");
    }
    maskingCounts = std::move (next);   // a clash that went away starts again from zero
    const juce::ScopedLock sl (historyLock);
    persistentMasking = lasting;
}

void MentalismProcessor::runAutomation()
{
    const auto rep = report();
    if (rep.measuredSeconds >= lastMaskingCheckAt + 30.0f)
    {
        lastMaskingCheckAt = rep.measuredSeconds;
        watchMasking (masking::find (suite::Registry::list()));
    }
    else if (rep.measuredSeconds < lastMaskingCheckAt)
        lastMaskingCheckAt = rep.measuredSeconds;   // measurements were reset
    using T = suite::SectionType;
    auto setText = [this] (juce::String& which, const juce::String& text) { const juce::ScopedLock sl (historyLock); which = text; };

    // Finish: once the drops have been heard again, the rough mix.
    if (finishState.load() == FinishStage::WaitingForDrop)
    {
        bool dropsHeard = rep.arrangementAvailable && rep.map.count > 0;
        bool anyDrop = false;
        for (int i = 0; dropsHeard && i < rep.map.count && i < (int) rep.sections.size(); ++i)
            if (rep.map.sections[(size_t) i].type == T::Drop) { anyDrop = true; dropsHeard = rep.sections[(size_t) i].heard(); }
        if (! anyDrop) dropsHeard = rep.measuredSeconds >= finishMeasuredAt + 10.0f;   // no map: 10 s more of the song
        if (dropsHeard)
        {
            sendFix (suite::Fix::RoughMix);
            for (int i = 0; i < rep.map.count; ++i) forgetSection (i);   // the coach hears the new balance from scratch
            finishState.store (FinishStage::Coaching);
            setText (finishText, "Rough mix done. Play the whole song: the auto-coach shapes the arc.");
        }
        return;
    }

    if (apvts.getRawParameterValue (Params::autoCoach)->load() < 0.5f || ! rep.arrangementAvailable || rep.map.count == 0)
        return;
    if (! rep.arrangement.complete)
    {
        if (finishState.load() == FinishStage::Coaching)
            setText (finishText, "Coaching: play the whole song (pass " + juce::String (autoPasses.load() + 1) + ").");
        return;
    }
    if (rep.arrangement.problems == 0 || autoPasses.load() >= maxAutoPasses)
    {
        const auto happy = rep.arrangement.problems == 0;
        setText (autoStatus, happy ? juce::String ("The coach is happy.")
                                   : "Stopped after " + juce::String (maxAutoPasses) + " passes: " + juce::String (rep.arrangement.problems) + " note(s) left for you.");
        if (finishState.load() == FinishStage::Coaching)
        {
            const auto ready = checklist::readyToExport (checklist::check (rep, clippedSamples.load(), suite::Registry::list()));
            finishState.store (FinishStage::Done);
            saveMixReport();   // a report of the finished mix, next to the others
            logFinishedSong (rep);
            if (rep.measurement.integratedLufs > -70.0f)   // the style log: one finished master
                suite::StyleLog::add ("master", { { "lufs", juce::String (rep.measurement.integratedLufs, 1) },
                                                  { "truepeak", juce::String (rep.measurement.truePeakDb, 1) },
                                                  { "target", rep.targetName } });
            setText (finishText, juce::String (happy ? "Finished: the arc is right. " : "Finished what can be automated. ")
                                     + (ready ? "Ready to export." : "See the checklist before you export."));
        }
        return;
    }

    // One pass: each problem's fix once (arc fixes per section).
    juce::StringArray applied;
    std::vector<std::pair<suite::Fix, uint32_t>> sent;
    for (int i = 0; i < rep.arrangement.problems && i < (int) rep.arrangement.fixes.size(); ++i)
    {
        const auto fix = rep.arrangement.fixes[(size_t) i];
        const auto data = i < (int) rep.arrangement.fixData.size() ? rep.arrangement.fixData[(size_t) i] : 0u;
        if (fix == suite::Fix::None) continue;
        if (std::find (sent.begin(), sent.end(), std::make_pair (fix, data)) != sent.end()) continue;
        sent.push_back ({ fix, data });
        applyCoachFix (fix, data, rep.map);
        applied.add (coach::describe (fix).fromFirstOccurrenceOf (": ", false, false));
    }
    autoPasses.fetch_add (1);
    setText (autoStatus, "Auto-coach pass " + juce::String (autoPasses.load()) + ": " + applied.joinIntoString ("; ") + ". Play the song again.");
}

MentalismProcessor::~MentalismProcessor()
{
    stopTimer();
    suite::TargetBus::withdraw();
    cancelLoad.store (true);
    if (loader.joinable())
        loader.join();
    if (mySoundLoader.joinable())
        mySoundLoader.join();
}

MySound MentalismProcessor::analyseMySound (const juce::Array<juce::File>& tracks, const std::atomic<bool>* cancel)
{
    std::vector<ReferenceProfile> profiles;
    for (const auto& f : tracks)
    {
        if (cancel != nullptr && cancel->load()) break;
        profiles.push_back (ReferenceProfile::analyse (f, cancel));
    }
    return MySound::average (profiles);
}

void MentalismProcessor::learnMySound (const juce::Array<juce::File>& tracks)
{
    if (learningMySound.load()) return;
    if (mySoundLoader.joinable()) mySoundLoader.join();
    learningMySound.store (true);
    mySoundLoader = std::thread ([this, tracks]
    {
        const auto m = analyseMySound (tracks, &cancelLoad);
        if (m.valid)
        {
            m.save (MySound::file());
            setMySound (m);
        }
        learningMySound.store (false);
    });
}

bool MentalismProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return in == out;
}

void MentalismProcessor::prepareToPlay (double newSampleRate, int samplesPerBlock)
{
    sampleRate = newSampleRate;
    loudness.prepare (sampleRate);
    truePeak.reset();
    stereo.prepare (sampleRate);
    bands.prepare (sampleRate);
    mono.assign ((size_t) samplesPerBlock, 0.0f);
}

void MentalismProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    if (resetRequested.exchange (false))
    {
        loudness.reset();
        truePeak.reset();
        bands.reset();
        clippedSamples.store (0);
        for (auto& st : statsBySection) st.clear();
    }

    // Arrangement coach: attribute each 400 ms loudness reading to the section it falls in.
    const auto host = suite::HostInfo::read (getPlayHead());
    const auto haveMap = sections.refresh();
    if (haveMap && sections.getMap() != statsMap)
    {
        statsMap = sections.getMap();          // a different arrangement: start over
        for (auto& st : statsBySection) st.clear();
    }
    const auto followSections = haveMap && host.playing && host.hasPpq;
    const auto qPerSample = host.quartersPerSample (sampleRate);

    // Measure only: the audio passes through untouched.
    const auto n = buffer.getNumSamples();
    const auto* l = buffer.getReadPointer (0);
    const auto* r = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : l;
    if ((int) mono.size() < n) mono.resize ((size_t) n);

    for (int i = 0; i < n; ++i)
    {
        loudness.process (l[i], r[i]);
        if (loudness.subBlockCount() != lastSubBlock)
        {
            lastSubBlock = loudness.subBlockCount();
            const auto power = loudness.lastMomentaryPower;
            if (followSections && analysis::powerToLufs (power) > -70.0f)
            {
                const auto pos = host.ppq + qPerSample * i;
                const auto sec = sections.locate (pos, host.barQuarters());
                const auto windowQ = 0.4 * host.bpm / 60.0;   // the reading covers the last 400 ms
                if (sec.valid && pos - windowQ >= sec.startQ - 1.0e-9)   // skip readings that straddle the section start
                {
                    auto& st = statsBySection[(size_t) sec.index];
                    st.power.store (st.power.load() + power);
                    st.blocks.store (st.blocks.load() + 1);
                    if (sec.type == suite::SectionType::Build)
                    {
                        const auto firstHalf = sec.progress (pos) < 0.5;
                        auto& sum = firstHalf ? st.first : st.second;
                        auto& count = firstHalf ? st.firstBlocks : st.secondBlocks;
                        sum.store (sum.load() + power);
                        count.store (count.load() + 1);
                        auto& bright = firstHalf ? st.brightFirst : st.brightSecond;   // the top end against the mids
                        const auto top = bands.shortDb[8].load(), mids = bands.shortDb[5].load();
                        bright.store (bright.load() + (top < -90.0f || mids < -90.0f ? -100.0 : (double) (top - mids)));   // silent: no top end to judge
                    }
                }
            }
        }
        truePeak.process (l[i], r[i]);
        if (std::abs (l[i]) >= 0.9999f || std::abs (r[i]) >= 0.9999f) clippedSamples.fetch_add (1, std::memory_order_relaxed);
        stereo.process (l[i], r[i]);
        bands.process (l[i], r[i]);
        mono[(size_t) i] = 0.5f * (l[i] + r[i]);
    }
    spectrumTap.push (mono.data(), n);
}

void MentalismProcessor::loadReference (const juce::File& file)
{
    if (loading.load())
        return;
    if (loader.joinable())
        loader.join();

    loading.store (true);
    cancelLoad.store (false);
    loader = std::thread ([this, file]
    {
        const auto profile = ReferenceProfile::analyse (file, &cancelLoad);
        if (profile.valid)
            setReference (profile);
        loading.store (false);
    });
}

std::vector<coach::SectionStat> MentalismProcessor::sectionStats() const
{
    std::vector<coach::SectionStat> out;
    for (const auto& a : statsBySection)
    {
        coach::SectionStat st;
        st.power = a.power.load();
        st.blocks = a.blocks.load();
        st.firstHalf = a.first.load(); st.firstBlocks = a.firstBlocks.load();
        st.secondHalf = a.second.load(); st.secondBlocks = a.secondBlocks.load();
        st.brightFirst = a.brightFirst.load(); st.brightSecond = a.brightSecond.load();
        out.push_back (st);
    }
    return out;
}

ReferenceProfile MentalismProcessor::getReference() const
{
    const juce::ScopedLock sl (referenceLock);
    return reference;
}

void MentalismProcessor::setReference (const ReferenceProfile& p)
{
    const juce::ScopedLock sl (referenceLock);
    reference = p;
}

uint32_t MentalismProcessor::matchReference()
{
    const auto ref = getReference();
    if (! ref.valid || bands.longSeconds.load() < 10.0f)
        return 0;
    advisor::Bands now {};
    for (size_t b = 0; b < (size_t) analysis::numBands; ++b) now[b] = bands.longDb[b].load();
    const auto diff = advisor::difference (now, ref.bands);   // + = we have more than the reference
    const auto low = (diff[0] + diff[1] + diff[2]) / 3.0f, high = (diff[8] + diff[9]) / 2.0f;
    auto correction = [] (float d) { return juce::jlimit (-4.0f, 4.0f, std::round (-0.5f * d * 4.0f) / 4.0f); };
    const auto cl = correction (low), ch = correction (high);
    lastMatchLow.store (cl);
    lastMatchHigh.store (ch);
    setParameterPlain (apvts, Params::loudnessTarget, (float) advisor::matchReferenceIndex);
    if (std::abs (cl) < 0.25f && std::abs (ch) < 0.25f)
        return 0;
    return sendFix (suite::Fix::ReferenceTone, suite::packTone (cl, ch));
}

MentalismProcessor::Report MentalismProcessor::report() const
{
    Report rep;
    const auto ref = getReference();
    const auto loudIndex = (int) apvts.getRawParameterValue (Params::loudnessTarget)->load();
    const auto tonalIndex = (int) apvts.getRawParameterValue (Params::tonalTarget)->load();

    const auto& lt = advisor::loudnessTargets()[(size_t) juce::jlimit (0, (int) advisor::loudnessTargets().size() - 1, loudIndex)];
    rep.ceiling = lt.ceilingDbtp;
    const auto mine = getMySound();
    if (loudIndex == advisor::mySoundIndex)
    {
        rep.targetLufs = mine.valid ? mine.lufs : advisor::loudnessTargets()[0].lufs;
        rep.ceiling = mine.valid ? mine.ceiling() : rep.ceiling;
        rep.targetName = mine.valid ? "your sound (" + juce::String (mine.lufs, 1) + " LUFS, from " + juce::String (mine.tracks) + " tracks)"
                                    : juce::String ("the club target (learn your sound from your tracks first)");
    }
    else if (loudIndex == advisor::matchReferenceIndex && ref.valid)
    {
        rep.targetLufs = ref.lufs;
        rep.targetName = "the reference (" + juce::String (ref.lufs, 1) + " LUFS)";
    }
    else
    {
        rep.targetLufs = loudIndex == advisor::matchReferenceIndex ? advisor::loudnessTargets()[0].lufs : lt.lufs;
        rep.targetName = loudIndex == advisor::matchReferenceIndex ? juce::String ("the club target (load a reference to match it)")
                                                                   : juce::String (lt.name);
    }

    advisor::Bands target {};
    if (tonalIndex == advisor::mySoundTonalIndex)
    {
        rep.tonalAvailable = mine.valid;
        target = mine.bands;
    }
    else if (tonalIndex == advisor::referenceTonalIndex)
    {
        rep.tonalAvailable = ref.valid;
        target = ref.bands;
    }
    else
    {
        rep.tonalAvailable = true;
        target = advisor::tonalTargets()[(size_t) juce::jlimit (0, 1, tonalIndex)].curve;
    }

    advisor::Bands shortNow {}, longNow {};
    for (size_t b = 0; b < (size_t) analysis::numBands; ++b)
    {
        shortNow[b] = bands.shortDb[b].load();
        longNow[b] = bands.longDb[b].load();
    }
    rep.measuredSeconds = bands.longSeconds.load();
    rep.shortDiff = advisor::difference (shortNow, target);
    rep.longDiff = advisor::difference (longNow, target);

    auto& m = rep.measurement;
    m.integratedLufs = loudness.integrated();
    m.truePeakDb = truePeak.peakDb();
    m.correlation = stereo.correlation.load();
    m.lowCorrelation = stereo.lowCorrelation.load();
    m.monoLossDb = stereo.monoLossDb.load();
    m.haveTonal = rep.tonalAvailable && rep.measuredSeconds >= 10.0f; // need some music before judging balance
    m.bandDiff = rep.longDiff;

    rep.momentary = loudness.momentary.load();
    rep.shortTerm = loudness.shortTerm.load();
    rep.advice = advisor::suggestions (m, rep.targetLufs, rep.ceiling, rep.targetName);

    // Arrangement coach (needs Ouroboros).
    rep.arrangementAvailable = suite::SectionBus::read (rep.map);
    if (rep.arrangementAvailable)
    {
        rep.sections = sectionStats();
        rep.arrangement = coach::analyse (rep.map, rep.sections, getCoachThresholds());
        if (rep.arrangement.problems > 0)
        {
            rep.advice.insert (0, "Arrangement: " + rep.arrangement.notes[0]);
            while (rep.advice.size() > 6) rep.advice.remove (rep.advice.size() - 1);
        }
    }
    for (const auto& m : maskingSuggestions()) rep.advice.insert (juce::jmin (1, rep.advice.size()), m);
    {
        Pass before;
        { const juce::ScopedLock sl (historyLock); before = previousPass; }
        if (before.valid && rep.measuredSeconds >= 20.0f && rep.measurement.integratedLufs > -70.0f)
        {
            Pass now;
            now.valid = true;
            now.lufs = rep.measurement.integratedLufs;
            for (size_t b = 0; b < now.bands.size(); ++b) now.bands[b] = bands.longDb[b].load();
            rep.sinceLastPass = comparePasses (before, now);
            rep.advice.add (rep.sinceLastPass);
        }
    }
    while (rep.advice.size() > 6) rep.advice.remove (rep.advice.size() - 1);
    return rep;
}

juce::AudioProcessorEditor* MentalismProcessor::createEditor()
{
    return new MentalismEditor (*this);
}

void MentalismProcessor::getPluginState (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    const auto ref = getReference();
    if (ref.valid)
    {
        juce::ValueTree r ("Reference");
        r.setProperty ("name", ref.name, nullptr);
        r.setProperty ("lufs", ref.lufs, nullptr);
        r.setProperty ("tp", ref.truePeakDb, nullptr);
        juce::StringArray b;
        for (auto v : ref.bands) b.add (juce::String (v, 3));
        r.setProperty ("bands", b.joinIntoString (","), nullptr);
        state.removeChild (state.getChildWithName ("Reference"), nullptr);
        state.appendChild (r, nullptr);
    }
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void MentalismProcessor::setPluginState (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    const auto r = state.getChildWithName ("Reference");
    if (r.isValid())
    {
        ReferenceProfile p;
        p.name = r["name"].toString();
        p.lufs = (float) r["lufs"];
        p.truePeakDb = (float) r["tp"];
        const auto values = juce::StringArray::fromTokens (r["bands"].toString(), ",", "");
        for (int i = 0; i < juce::jmin (values.size(), analysis::numBands); ++i)
            p.bands[(size_t) i] = values[i].getFloatValue();
        p.valid = values.size() == analysis::numBands;
        setReference (p);
        state.removeChild (r, nullptr);
    }
    apvts.replaceState (state);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MentalismProcessor();
}
