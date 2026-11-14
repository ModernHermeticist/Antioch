#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "KeyBus.h"
#include "KickBus.h"
#include "ChordBus.h"
#include "VocalBus.h"
#include "StyleLog.h"
#include "TransmuteBus.h"
#include "KitBus.h"
#include "WritingBus.h"
#include "Music.h"
#include "Roles.h"

namespace TParams
{
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using namespace juce;
        using Attr = AudioParameterFloatAttributes;
        AudioProcessorValueTreeState::ParameterLayout layout;
        auto pct = Attr().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; });
        auto spiceText = Attr().withStringFromValueFunction ([] (float v, int)
        {
            return v < 0.35f ? String ("Subtle") : v < 0.8f ? String ("Tasteful") : v < 1.25f ? String ("Lively") : v < 1.65f ? String ("Bold") : String ("Wild");
        });
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { spice, 1 }, "Spice", NormalisableRange<float> (0.0f, 2.0f), 1.0f, spiceText));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { builds, 1 }, "Builds", NormalisableRange<float> (0.0f, 2.0f), 1.0f, pct));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { drops, 1 }, "Drops", NormalisableRange<float> (0.0f, 2.0f), 1.0f, pct));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { amount, 1 }, "Amount", NormalisableRange<float> (0.0f, 1.0f), 1.0f, pct));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { output, 1 }, "Output", NormalisableRange<float> (-24.0f, 12.0f), 0.0f,
                                                           Attr().withStringFromValueFunction ([] (float v, int) { return (v > 0.05f ? "+" : "") + String (v, 1) + " dB"; })));
        StringArray genres;
        for (int g = 0; g < trm::numGenres; ++g) genres.add (trm::genreName (g));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { genre, 1 }, "Genre", genres, 0));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { role, 1 }, "Role",
                                                            StringArray { "Auto", "Kick", "Bass", "Hats & percussion", "Pads & chords", "Lead", "Vocal", "FX" }, 0));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { direct, 1 }, "Director", true));
        return layout;
    }
}

namespace
{
    uint32_t hashString (const juce::String& s) { return (uint32_t) s.hashCode64() ^ (uint32_t) (s.hashCode64() >> 32); }
    struct Hasher
    {
        uint64_t h = 1469598103934665603ull;
        void add (uint64_t v) { h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); }
        void add (double v) { add ((uint64_t) (int64_t) std::llround (v * 1000.0)); }
    };
}

TransmutationProcessor::TransmutationProcessor()
    : SuiteProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "TransmutationState", TParams::createLayout())
{
    addModule ("Azoth", std::make_unique<AzothProcessor>());
    // Mentalism's fixes: a build that doesn't rise -> the builds work harder; a drop that doesn't
    // jump out of its build (data 0) -> the drops do more
    fixes = std::make_unique<suite::FixReceiver> (apvts, [this] (suite::Fix fix, uint32_t data) -> std::vector<suite::FixReceiver::Change>
    {
        using R = suite::FixReceiver;
        if (fix == suite::Fix::BuildRise) return { R::nudge (apvts, TParams::builds, 0.125f) };
        if (fix == suite::Fix::BuildImpact && data == 0) return { R::nudge (apvts, TParams::drops, 0.125f), R::nudge (apvts, TParams::builds, 0.0625f) };
        return {};
    });
    for (auto& a : activeNow) a.store (0);
    captureFreshValues();
}

TransmutationProcessor::~TransmutationProcessor()
{
    suite::TransmuteBus::withdraw (getInstanceId());
}

bool TransmutationProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return in == out || in == juce::AudioChannelSet::mono();
}

void TransmutationProcessor::prepareToPlay (double newSampleRate, int blockSizeForModules)
{
    prepareModules (newSampleRate, blockSizeForModules);
    sampleRate = newSampleRate;
    engine.prepare (sampleRate);
    outGain.reset (sampleRate, 0.03);
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (value (TParams::output)));
    anchors.fill (-1);
    auditionMove.effect = 0;
    lastChordIndex = -1;
    lastPosQ = -1.0;
    memoryArmed = true;
    memoryEndQ = -1.0;
}

int TransmutationProcessor::effectiveRole() const
{
    const auto chosen = (int) apvts.getRawParameterValue (TParams::role)->load();
    if (chosen > 0) return chosen;
    return juce::jmax (0, roleForTrack());
}

bool TransmutationProcessor::applyRoleSetup (int role, const juce::String&)
{
    // How lively each role starts: the kick and vocal stay calmer, FX go further
    const auto spice = role == 1 ? 0.6f : role == 6 ? 0.85f : role == 7 ? 1.3f : role == 3 ? 1.1f : 1.0f;
    if (std::abs (spice - 1.0f) < 0.01f) return false;
    setParameterPlain (apvts, TParams::spice, spice);
    return true;
}

trm::Ctx TransmutationProcessor::contextFor (const trm::Move& mv, double posQ, const suite::SectionFollower::Position& sec, double barQ) const
{
    trm::Ctx c;
    c.posQ = posQ; c.barQ = barQ;
    c.lenQ = juce::jmax (0.01, (double) mv.endQ - mv.startQ);
    c.q = juce::jlimit (0.0, c.lenQ, posQ - mv.startQ);
    c.t = c.q / c.lenQ;
    c.amt = mv.amount;
    if (sec.valid)
    {
        c.sectionType = (int) sec.type;
        c.sectionProgress = juce::jlimit (0.0, 1.0, sec.progress (posQ));
        c.breakPeak = sec.type == suite::SectionType::Breakdown && sec.endQ - posQ <= 2.0 * barQ;
        const auto& map = sections.getMap();
        const auto next = sec.hasNext && sec.index + 1 < map.count ? map.energyOf (sec.index + 1) : sec.energy;
        const auto p = (float) c.sectionProgress;
        c.energy = sec.energy + (next - sec.energy) * p * p;
        c.phraseLenQ = 8.0 * barQ;
        c.phraseQ = std::fmod (juce::jmax (0.0, posQ - sec.startQ), c.phraseLenQ);
        c.finalLift = sec.nextKeyShift - sec.keyShift;
        c.nextIsDrop = sec.hasNext && sec.next == suite::SectionType::Drop;
    }
    else
    {
        c.phraseLenQ = 8.0 * barQ;
        c.phraseQ = std::fmod (juce::jmax (0.0, posQ), c.phraseLenQ);
    }
    return c;
}

void TransmutationProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiForModules)
{
    const auto runModules = modulesAfter (buffer, midiForModules);   // Azoth runs on the way out
    juce::ScopedNoDenormals noDenormals;
    const auto numSamples = buffer.getNumSamples();
    const auto numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();
    for (auto ch = numIn; ch < numOut; ++ch) buffer.clear (ch, 0, numSamples);
    if (numOut > 1 && numIn == 1) buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);

    const auto host = suite::HostInfo::read (getPlayHead());
    const auto haveSections = sections.refresh();
    mapFound.store (haveSections);
    const auto barQ = host.barQuarters();
    const auto bpm = host.bpm > 1.0 ? host.bpm : 138.0;
    const auto samplesPerQ = sampleRate * 60.0 / bpm;
    const auto running = host.playing && host.hasPpq;
    songPos.store (host.ppq); bpmNow.store (bpm); playingNow.store (running);

    // a new plan from the message thread
    if (pendingFresh.load())
    {
        const juce::SpinLock::ScopedTryLockType l (audioLock);
        if (l.isLocked())
        {
            moves = pending; moveCount = pendingCount;
            anchors.fill (-1);
            pendingFresh.store (false);
        }
    }
    if (const auto id = auditionRequest.exchange (0); id >= 1 && id <= trm::numEffects && running)
    {
        auditionMove = {};
        auditionMove.startQ = (float) host.ppq; auditionMove.endQ = (float) (host.ppq + barQ);
        auditionMove.effect = id; auditionMove.amount = 1.0f; auditionMove.flags = trm::mfAudition;
        auditionMove.seed = (uint32_t) juce::Time::getMillisecondCounter();
        const auto& e = trm::effect (id);
        auditionMove.slot = (e.slots & trm::sBuildEnd) ? trm::sBuildEnd : (e.slots & trm::sPhraseEnd) ? trm::sPhraseEnd : e.slots & (~e.slots + 1u);
        auditionAnchor = -1;
    }

    const auto director = value (TParams::direct) > 0.5f && suite::Autopilot::on (suite::Autopilot::transmuteF);
    const auto suiteLive = suiteAudio.refreshFor (getInstanceId());
    const auto chosenRole = (int) value (TParams::role);
    const auto role = chosenRole > 0 ? chosenRole : autoRole.load();
    const auto kicks = suite::KickBus::read();
    suite::Progression prog;
    const auto haveChords = suite::ChordBus::read (prog);
    const auto key = suite::KeyBus::read();
    const auto keyRoot = key.valid ? key.root : haveChords ? prog.root : 9;
    const auto scale = key.valid ? key.scale : haveChords ? prog.scale : 0;
    const auto leadHz = suite::Autopilot::leadPitch();
    const auto borrowed = borrowedMask.load();
    outGain.setTargetValue (juce::Decibels::decibelsToGain (value (TParams::output)));

    // a clean ending: past the map's last bar the echoes, reverb and layers fade over 2 bars
    auto endFade = 1.0f;
    if (suite::Autopilot::on (suite::Autopilot::cleanEndingF) && haveSections && running && sections.getMap().totalBars > 0)
    {
        const auto over = (host.ppq - sections.getMap().totalBars * barQ) / (2.0 * barQ);
        if (over > 0.0) endFade = (float) juce::jlimit (0.0, 1.0, 1.0 - over);
    }

    auto* L = buffer.getWritePointer (0);
    auto* R = numOut > 1 ? buffer.getWritePointer (1) : buffer.getWritePointer (0);
    std::array<int, 6> active {};
    int activeCount = 0;
    bool wantsShape = false;
    int shapeRole = 0;

    for (int off = 0; off < numSamples; off += trm::Engine::subBlock)
    {
        const auto n = juce::jmin (trm::Engine::subBlock, numSamples - off);
        trm::Env e;
        e.barQ = barQ; e.samplesPerQ = samplesPerQ; e.playing = running; e.endFade = endFade;
        e.amount = value (TParams::amount); e.leadHz = leadHz;
        trm::Mod mod;
        if (running)
        {
            const auto posQ = host.ppq + off / samplesPerQ;
            e.posQ = posQ;
            if (lastPosQ >= 0.0 && posQ < lastPosQ - 0.01) { anchors.fill (-1); memoryArmed = true; }   // looped back
            lastPosQ = posQ;
            const auto sec = haveSections ? sections.locate (posQ, barQ) : suite::SectionFollower::Position {};
            // key and chord
            const auto shift = sec.valid ? sec.keyShift : 0;
            const auto root = keyRoot + shift;
            e.rootHz = suite::music::midiToHz ((float) (33 + ((root % 12) + 12) % 12));
            int chordDegree = 0, chordIndex = 0;
            if (haveChords && prog.count > 0)
            {
                chordIndex = prog.chordAt (sec.valid ? posQ - sec.startQ : posQ);
                const auto cr = prog.chordRoots[(size_t) (chordIndex & 7)];
                const auto& steps = suite::music::scales()[(size_t) juce::jlimit (0, 5, scale)].steps;
                for (int d = 0; d < 7; ++d) if (steps[(size_t) d] == cr) chordDegree = d;
            }
            if (chordIndex != lastChordIndex) { lastChordIndex = chordIndex; chordAnchor = engine.now(); e.chordChanged = true; }
            const auto base = suite::music::scaleStep (scale, chordDegree);
            const auto third = suite::music::scaleStep (scale, chordDegree + 2) - base, fifth = suite::music::scaleStep (scale, chordDegree + 4) - base;
            const auto chordMidi = 57.0f + (float) (((root + base) % 12 + 12) % 12);
            e.chordHz = { suite::music::midiToHz (chordMidi), suite::music::midiToHz (chordMidi + (float) third), suite::music::midiToHz (chordMidi + (float) fifth) };
            // the kick
            const auto phaseQ = std::fmod (posQ, barQ);
            const auto msPerQ = 60000.0 / bpm;
            const auto typeIdx = sec.valid ? (int) sec.type : -1;
            e.kickSinceMs = kicks.msSinceHit (typeIdx, phaseQ, barQ, msPerQ);
            e.kickUntilMs = kicks.msUntilHit (typeIdx, phaseQ, barQ, msPerQ);
            uint16_t kickMask = 0;
            if (const auto* kp = kicks.patternFor (typeIdx); kp != nullptr && kp->kicks())
                for (int k = 0; k < kp->count; ++k) kickMask |= (uint16_t) (1u << juce::jlimit (0, 15, (int) (kp->positions[(size_t) k] / 0.25)));
            // the first drop is remembered (hook memories, mirror drops, self-remixes)
            if (sec.valid && sec.type == suite::SectionType::Drop && sec.occurrence == 0 && memoryArmed && posQ - sec.startQ < 0.25)
            {
                engine.startMemory();
                memoryArmed = false;
                memoryEndQ = sec.startQ + juce::jmin (4.0 * barQ, sec.endQ - sec.startQ);
            }
            if (engine.memoryRecording() && posQ >= memoryEndQ) engine.stopMemory();
            const auto bar = (int) std::floor (posQ / barQ) + 1;
            const auto vocalNow = suite::VocalBus::sings (bar);

            auto addMove = [&] (const trm::Move& mv, int64_t& anchor)
            {
                if (anchor < 0 && suiteLive && (mv.slot & trm::bodySlots) == 0 && (mv.flags & trm::mfAudition) == 0)
                {
                    // the fidelity check: a transition move the plan asked for, or one it didn't
                    if (const auto at = suiteAudio.actingAt (posQ, suite::director::pTransmute); at >= 0) suite::DirectorBus::reportActed (suiteAudio.hash(), at, suite::director::pTransmute);
                    else if ((mv.flags & trm::mfLocked) == 0) suite::DirectorBus::reportExtra (suite::director::pTransmute);
                }
                if (anchor < 0) anchor = engine.now() - (int64_t) std::llround ((posQ - mv.startQ) * samplesPerQ);
                auto c = contextFor (mv, posQ, sec, barQ);
                c.role = role; c.samplesPerQ = samplesPerQ; c.anchor = anchor; c.chordAnchor = chordAnchor;
                c.rootHz = e.rootHz; c.leadHz = leadHz; c.pitchHz = engine.pitchEstimate();
                c.third = third; c.fifth = fifth; c.scale = scale;
                c.chordTension = haveChords && prog.isTension (chordIndex);
                c.chordMinor = third == 3;
                c.chordQ = haveChords && prog.quartersPerChord > 0.0 ? std::fmod (juce::jmax (0.0, sec.valid ? posQ - sec.startQ : posQ), prog.quartersPerChord) : std::fmod (posQ, barQ);
                c.ownOnsets = engine.lastBarOnsets(); c.borrowedOnsets = borrowed; c.kickMask = kickMask;
                c.memLen = engine.memoryLength(); c.vocalNow = vocalNow; c.held = engine.heldAmount();
                if (! sec.valid) c.energy = 0.6f;
                trm::applyRecipe (mv, c, mod);
                if (mv.effect == 78 || mv.effect == 79) { wantsShape = true; shapeRole = mv.effect == 78 ? 5 : 6; }
                if (activeCount < (int) active.size() && std::find (active.begin(), active.begin() + activeCount, mv.effect) == active.begin() + activeCount)
                    active[(size_t) activeCount++] = mv.effect;
            };
            if (director)
                for (int k = 0; k < moveCount; ++k)
                {
                    const auto& mv = moves[(size_t) k];
                    if (posQ < mv.startQ) { anchors[(size_t) k] = -1; continue; }
                    if (posQ >= mv.endQ) continue;
                    addMove (mv, anchors[(size_t) k]);
                }
            if (auditionMove.effect > 0)
            {
                if (posQ >= auditionMove.endQ || posQ < auditionMove.startQ - 0.01) auditionMove.effect = 0;
                else addMove (auditionMove, auditionAnchor);
            }
            if (wantsShape)
            {
                // another track's live tone (Quintessence shares it every block)
                std::array<float, 10> sum {}; int found = 0;
                suite::Autopilot::forEachLive (getInstanceId(), [&] (int r, const float* bands)
                {
                    if (r != shapeRole) return;
                    float mean = 0.0f;
                    for (int b = 0; b < 10; ++b) mean += bands[b] / 10.0f;
                    for (int b = 0; b < 10; ++b) sum[(size_t) b] += bands[b] - mean;
                    ++found;
                });
                for (int b = 0; b < 10; ++b) e.shapeDb[(size_t) b] = found > 0 ? sum[(size_t) b] / (float) found : 0.0f;
                if (found == 0) mod.shapeMix = 0.0f;
            }
        }
        else
        {
            lastPosQ = -1.0;
        }
        engine.tgt = mod;
        engine.process (L + off, R + off, n, e);
    }

    // the director's silences (a snap, a breath, a kick-only bar, a fake drop's gap): this track
    // falls silent with a 3 ms fade, unless it's the kick in a kick-only bar
    const auto silenceStep = (float) (1.0 / (0.003 * sampleRate));
    float silenceTarget = 1.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        if ((i & 31) == 0)
        {
            silenceTarget = 1.0f;
            if (suiteLive && running)
                if (const auto at = suiteAudio.silenceAt (host.ppq + i / samplesPerQ); at >= 0
                    && ! (suiteAudio[at].silence == suite::director::gSiKickOnly && role == 1))
                {
                    silenceTarget = 0.0f;
                    suite::DirectorBus::reportActed (suiteAudio.hash(), at, suite::director::pSilence);
                }
        }
        silenceGain += juce::jlimit (-silenceStep, silenceStep, silenceTarget - silenceGain);
        if (silenceGain < 0.999f) { L[i] *= silenceGain; if (numOut > 1) R[i] *= silenceGain; }
    }

    // output (and a mono output gets both sides)
    for (int i = 0; i < numSamples; ++i)
    {
        const auto g = outGain.getNextValue();
        if (numOut > 1) { L[i] *= g; R[i] *= g; }
        else L[i] = 0.5f * (L[i] + R[i]) * g;
    }

    // Autopilot (stability): an effect running away on its own - the output far over its input and
    // over full scale for 50 ms - has its echoes and reverb cleared at once (the message thread logs it)
    {
        const auto runaway = engine.lastOutPeak > 1.0f && engine.lastOutPeak > 4.0f * engine.lastInPeak;
        runawaySeconds = runaway ? runawaySeconds + (float) (numSamples / sampleRate) : 0.0f;
        if (runawaySeconds > 0.05f && suite::Autopilot::on (suite::Autopilot::filterRingingF))
        {
            engine.clearTails();
            runawayFlag.store (true);
            runawaySeconds = 0.0f;
        }
        engine.lastInPeak = engine.lastOutPeak = 0.0f;
    }

    const auto st = engine.takeStats();
    statEnergy.store (statEnergy.load() + st.energy); statDiff.store (statDiff.load() + st.diff); statSide.store (statSide.load() + st.side);
    statOnsets.fetch_add (st.onsets); statSamples.fetch_add (st.samples); statHeld.fetch_add (st.heldSamples); statPitch.store (st.pitchHz);
    for (size_t k = 0; k < activeNow.size(); ++k) activeNow[k].store ((int) k < activeCount ? active[k] : 0);
    levelHoldDb.store (engine.levelHoldDb());
    memoryBars.store (engine.memoryLength() > 0 ? (int) std::lround (engine.memoryLength() / (samplesPerQ * barQ)) : 0);
}

//==============================================================================
void TransmutationProcessor::updateProfile()
{
    const auto samples = statSamples.exchange (0);
    const auto energy = statEnergy.exchange (0.0);
    const auto onsets = statOnsets.exchange (0), heldS = statHeld.exchange (0);
    statDiff.store (0.0); statSide.store (0.0);
    if (profileLocked || samples < (int) sampleRate || energy / juce::jmax (1, samples) < 1.0e-6) return;   // silence tells nothing

    // pitched: a clear repeat in the waveform (autocorrelation of the decimated input)
    const auto w = engine.decimWrite.load (std::memory_order_acquire);
    constexpr int len = 2048;
    std::array<float, len> x {};
    float mean = 0.0f;
    for (int i = 0; i < len; ++i) { x[(size_t) i] = engine.decim[(size_t) ((w - len + i) & (trm::Engine::decimSize - 1))]; mean += x[(size_t) i] / len; }
    double r0 = 0.0;
    for (auto& v : x) { v -= mean; r0 += (double) v * v; }
    float best = 0.0f;
    if (r0 > 1.0e-9)
        for (int lag = 24; lag <= 240; ++lag)
        {
            double r = 0.0;
            for (int i = 0; i + lag < len; ++i) r += (double) x[(size_t) i] * x[(size_t) (i + lag)];
            best = juce::jmax (best, (float) (r / r0 * len / (len - lag)));
        }
    const auto seconds = samples / sampleRate;
    const auto onsetsPerBeat = onsets / juce::jmax (0.1, seconds * bpmNow.load() / 60.0);
    const auto heldFrac = (float) heldS / (float) samples;
    trm::Profile p;
    p.known = true;
    p.pitched = best > 0.6f;
    p.sustained = heldFrac > 0.35f;
    p.plucky = onsetsPerBeat >= 0.4 && heldFrac < 0.3f;
    p.busy = onsetsPerBeat > 1.5;
    const juce::ScopedLock sl (planLock);
    if (p == candidate) ++profileVotes; else { candidate = p; profileVotes = 1; }
    if (profileVotes >= 2) profile = candidate;
}

void TransmutationProcessor::pollTick()
{
    const auto nowMs = juce::Time::getMillisecondCounterHiRes();
    autoRole.store (juce::jmax (0, roleForTrack()));
    if (nowMs - lastProfileMs > 2000.0) { lastProfileMs = nowMs; updateProfile(); }

    // the lead's rhythm, for rhythm borrowing (Quintessence shares every track's attacks)
    {
        uint16_t mask = 0;
        const auto pos = songPos.load();
        suite::Autopilot::forEachLiveOnsets (getInstanceId(), [&] (int r, const double* on)
        {
            if (r != 5) return;
            for (int k = 0; k < 16; ++k)
                if (on[k] > 0.0 && pos - on[k] < 8.0 && pos >= on[k])
                    mask |= (uint16_t) (1u << juce::jlimit (0, 15, (int) (std::fmod (on[k], 4.0) / 0.25)));
        });
        borrowedMask.store (mask);
    }

    if (runawayFlag.exchange (false))
    {
        ++runawaysCaught;
        suite::Autopilot::count (suite::Autopilot::runawaysCaught);
        suite::MoveLog::add (getName(), getTrackName(), "Runaway caught: an effect ran away on its own; the echoes and reverb were cleared", suite::MoveLog::fix);
    }

    replan (false);
    if (nowMs - lastPublishMs > 2000.0) publishPlan();
}

bool TransmutationProcessor::replan (bool force)
{
    trm::DirectorInput in;
    in.haveMap = suite::SectionBus::read (in.map);
    in.barQ = 4.0;
    in.bpm = bpmNow.load();
    in.role = effectiveRole();
    autoRole.store (juce::jmax (0, roleForTrack()));
    const auto track = getTrackName();
    in.pluckName = suite::isPluckName (track); in.arpName = suite::isArpName (track);
    in.songSpice = suite::TransmuteBus::songSpice();
    in.spice = value (TParams::spice) * in.songSpice;
    in.builds = value (TParams::builds); in.drops = value (TParams::drops);
    in.genre = (int) value (TParams::genre);
    in.songSeed = suite::TransmuteBus::songSeed();
    in.vocalBars = suite::VocalBus::read();
    for (const auto& [a, b] : suite::KitBus::rests()) in.hookRests.push_back ({ a, b });
    for (const auto& n : suite::WritingBus::sung()) in.vocalNotes.push_back ({ n.start, n.start + n.length });
    // the suite director: which transitions this track leads, supports or sits out
    if (suiteMsg.refreshFor (getInstanceId()))
    {
        namespace dr = suite::director;
        in.suiteActive = true;
        static const int families[] = { -1, trm::fRepeat, trm::fReverse, trm::fPitch, trm::fFilter, trm::fSpace, trm::fGrain, trm::fStereo, trm::fTape };
        for (int i = 0; i < suiteMsg.size(); ++i)
        {
            const auto& m = suiteMsg[i];
            if (m.silence != dr::gNone && ! (m.silence == dr::gSiKickOnly && in.role == 1)) in.suiteSilences.push_back ({ m.silenceStartQ, m.silenceEndQ });
            if (! m.acts (dr::pTransmute)) continue;
            const auto w = suite::DirectorBus::windowFor (suiteMsg, i, dr::pTransmute);
            const auto g = m.gesture[dr::pTransmute];
            in.suiteCues.push_back ({ w.first, w.second, m.hero == dr::pTransmute ? 1 : 2, g >= dr::gTmRepeat && g <= dr::gTmTape ? families[g] : -1, (int) m.heroRole });
        }
    }

    // what you've kept in earlier songs (the style log), re-read once a minute
    const auto nowMs = juce::Time::getMillisecondCounterHiRes();
    if (nowMs - signatureReadMs > 60000.0)
    {
        signatureReadMs = nowMs;
        std::array<float, trm::numEffects + 1> sig {};
        for (const auto& e : suite::StyleLog::read())
            if (e.kind == "transmute")
                if (const auto id = e.get ("effect").getIntValue(); id >= 1 && id <= trm::numEffects) sig[(size_t) id] += 1.0f;
        const juce::ScopedLock sl (planLock);
        signature = sig;
    }

    // tracks that decide before this one (more important roles first, then by name)
    const auto myPriority = trm::dir::priority (in.role);
    for (const auto& o : suite::TransmuteBus::read())
    {
        if (o.id == getInstanceId()) continue;
        const auto theirs = trm::dir::priority (o.role);
        if (theirs < myPriority || (theirs == myPriority && o.track.compareNatural (track) >= 0)) continue;
        trm::OtherTrack ot; ot.id = o.id; ot.role = o.role;
        for (const auto& m : o.moves)
        {
            trm::Move mv; mv.startQ = m.startQ; mv.endQ = m.endQ; mv.effect = m.effect;
            mv.slot = m.effect >= 1 && m.effect <= trm::numEffects ? (trm::effect (m.effect).slots & (trm::sPhraseEnd | trm::sSectionEnd)) : 0u;
            mv.key = trm::dir::mix ((uint32_t) std::lround (m.startQ * 4.0f), (uint32_t) m.effect);
            ot.moves.push_back (mv);
        }
        in.others.push_back (std::move (ot));
    }

    {
        const juce::ScopedLock sl (planLock);
        in.hear = profile;
        in.modes = modes; in.locks = locks; in.rerolls = rerolls; in.signature = signature;
        const auto roll = rerolls.count (0u) > 0 ? (uint32_t) rerolls.at (0u) : 0u;
        in.trackSeed = hashString (track.isNotEmpty() ? track : juce::String ((int) getInstanceId())) ^ (roll * 2654435761u);
    }
    const auto directorOn = value (TParams::direct) > 0.5f;

    Hasher h;
    h.add ((uint64_t) in.haveMap); h.add ((uint64_t) in.map.count); h.add ((uint64_t) in.map.totalBars); h.add ((uint64_t) (int64_t) in.map.finalLift);
    for (int i = 0; i < in.map.count; ++i) { const auto& s = in.map.sections[(size_t) i]; h.add ((uint64_t) s.startBar); h.add ((uint64_t) s.type); h.add ((uint64_t) (int64_t) s.energy); }
    h.add ((uint64_t) std::lround (in.bpm)); h.add ((uint64_t) in.role);
    h.add ((uint64_t) (in.hear.known | in.hear.pitched << 1 | in.hear.sustained << 2 | in.hear.plucky << 3 | in.hear.busy << 4));
    h.add ((double) in.spice); h.add ((double) in.builds); h.add ((double) in.drops); h.add ((uint64_t) in.genre);
    h.add ((uint64_t) in.songSeed); h.add ((uint64_t) in.trackSeed); h.add ((uint64_t) directorOn); h.add ((double) in.songSpice);
    for (auto m : in.modes) h.add ((uint64_t) m);
    for (const auto& [k, v] : in.locks) { h.add ((uint64_t) k); h.add ((uint64_t) v); }
    for (const auto& [k, v] : in.rerolls) { h.add ((uint64_t) k); h.add ((uint64_t) v); }
    for (auto s : in.signature) h.add ((double) s);
    for (size_t b = 0; b < in.vocalBars.size(); ++b) if (in.vocalBars[b]) h.add ((uint64_t) b);
    for (const auto& o : in.others) { h.add ((uint64_t) o.id); for (const auto& m : o.moves) { h.add ((double) m.startQ); h.add ((uint64_t) m.effect); } }
    h.add ((uint64_t) in.pluckName); h.add ((uint64_t) in.arpName);
    for (const auto& [a, b] : in.hookRests) { h.add (a); h.add (b); }
    for (const auto& [a, b] : in.vocalNotes) { h.add (a); h.add (b); }
    h.add ((uint64_t) in.suiteActive); h.add ((uint64_t) (in.suiteActive ? suiteMsg.hash() : 0u));

    if (! force && h.h == inputHash) return false;
    auto fresh = directorOn ? trm::makePlan (in) : trm::Plan {};
    {
        const juce::ScopedLock sl (planLock);
        plan = fresh;
        input = in;
        inputHash = h.h;
    }
    {
        const juce::SpinLock::ScopedLockType l (audioLock);
        pendingCount = juce::jmin ((int) fresh.moves.size(), maxAudioMoves);
        for (int k = 0; k < pendingCount; ++k) pending[(size_t) k] = fresh.moves[(size_t) k];
        pendingFresh.store (true);
    }
    publishPlan();
    return true;
}

void TransmutationProcessor::publishPlan()
{
    lastPublishMs = juce::Time::getMillisecondCounterHiRes();
    suite::TransmuteBus::TrackPlan tp;
    tp.id = getInstanceId();
    tp.track = getTrackName().isNotEmpty() ? getTrackName() : juce::String ("Transmutation");
    tp.role = effectiveRole();
    {
        const juce::ScopedLock sl (planLock);
        for (const auto& m : plan.moves)
            if ((m.slot & trm::sSong) == 0)
                tp.moves.push_back ({ m.startQ, m.endQ, m.effect });
    }
    suite::TransmuteBus::publish (tp);
}

bool TransmutationProcessor::exportPlan (const juce::File& file) const
{
    suite::TransmuteBus::TrackPlan tp;
    tp.track = getTrackName().isNotEmpty() ? getTrackName() : juce::String ("Transmutation");
    {
        const juce::ScopedLock sl (planLock);
        for (const auto& m : plan.moves) if ((m.slot & trm::sSong) == 0) tp.moves.push_back ({ m.startQ, m.endQ, m.effect });
    }
    if (tp.moves.empty()) return false;
    file.deleteFile();
    juce::FileOutputStream out (file);
    return out.openedOk() && suite::TransmuteBus::planMidi ({ tp }, bpmNow.load(), [] (int id) { return juce::String (trm::effect (id).name); }).writeTo (out);
}

void TransmutationProcessor::setMode (int id, uint8_t mode)
{
    if (id < 1 || id > trm::numEffects) return;
    {
        const juce::ScopedLock sl (planLock);
        modes[(size_t) id] = mode;
    }
    if (mode == trm::mAlways) suite::StyleLog::add ("transmute", { { "effect", juce::String (id) }, { "how", "always" } });
    suite::MoveLog::add (getName(), getTrackName(), juce::String (trm::effect (id).name) + ": " + (mode == trm::mAuto ? "Auto" : mode == trm::mAlways ? "Always" : "Never"));
    replan (true);
}

void TransmutationProcessor::lockMove (const trm::Move& m)
{
    {
        const juce::ScopedLock sl (planLock);
        locks[m.key] = m.effect;
    }
    suite::StyleLog::add ("transmute", { { "effect", juce::String (m.effect) }, { "how", "lock" } });
    suite::MoveLog::add (getName(), getTrackName(), "Locked " + juce::String (trm::effect (m.effect).name) + " at bar " + juce::String ((int) (m.startQ / 4.0f) + 1));
    replan (true);
}

void TransmutationProcessor::unlockMoment (uint32_t key)
{
    { const juce::ScopedLock sl (planLock); locks.erase (key); }
    replan (true);
}

void TransmutationProcessor::rerollMoment (uint32_t key)
{
    {
        const juce::ScopedLock sl (planLock);
        locks.erase (key);
        ++rerolls[key];
    }
    replan (true);
}

void TransmutationProcessor::rerollAll()
{
    {
        const juce::ScopedLock sl (planLock);
        ++rerolls[0u];   // moment 0 = the whole track's dice
    }
    replan (true);
}

suite::SuiteProcessor::SuiteStatus TransmutationProcessor::suiteStatus()
{
    SuiteStatus s;
    const juce::ScopedLock sl (planLock);
    s.metrics[0] = (float) plan.moves.size();
    s.metrics[1] = (float) effectiveRole();
    return s;
}

//==============================================================================
juce::AudioProcessorEditor* TransmutationProcessor::createEditor() { return new TransmutationEditor (*this); }

void TransmutationProcessor::getPluginState (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    juce::ValueTree d ("Director");
    {
        const juce::ScopedLock sl (planLock);
        juce::String m;
        for (int id = 1; id <= trm::numEffects; ++id) m << (int) modes[(size_t) id];
        d.setProperty ("modes", m, nullptr);
        juce::StringArray l, r;
        for (const auto& [k, v] : locks) l.add (juce::String::toHexString ((int) k) + ":" + juce::String (v));
        for (const auto& [k, v] : rerolls) r.add (juce::String::toHexString ((int) k) + ":" + juce::String (v));
        d.setProperty ("locks", l.joinIntoString (","), nullptr);
        d.setProperty ("rerolls", r.joinIntoString (","), nullptr);
    }
    state.removeChild (state.getChildWithName ("Director"), nullptr);
    state.appendChild (d, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void TransmutationProcessor::setPluginState (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto state = juce::ValueTree::fromXml (*xml);
    const auto d = state.getChildWithName ("Director");
    {
        const juce::ScopedLock sl (planLock);
        modes = trm::defaultModes(); locks.clear(); rerolls.clear();
        if (d.isValid())
        {
            const auto m = d["modes"].toString();
            for (int id = 1; id <= trm::numEffects && id <= m.length(); ++id)
                modes[(size_t) id] = (uint8_t) juce::jlimit (0, 2, (int) (m[id - 1] - '0'));
            auto parse = [] (const juce::String& s, std::map<uint32_t, int>& out)
            {
                for (const auto& part : juce::StringArray::fromTokens (s, ",", ""))
                    if (part.contains (":"))
                        out[(uint32_t) part.upToFirstOccurrenceOf (":", false, false).getHexValue64()] = part.fromFirstOccurrenceOf (":", false, false).getIntValue();
            };
            parse (d["locks"].toString(), locks);
            parse (d["rerolls"].toString(), rerolls);
        }
    }
    state.removeChild (d, nullptr);
    apvts.replaceState (state);
    inputHash = 0;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TransmutationProcessor(); }
