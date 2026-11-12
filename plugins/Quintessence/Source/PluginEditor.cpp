#include "PluginEditor.h"
#include "Analysis.h"

using namespace suite::ui;

namespace
{
    const juce::Colour quint { 0xffe6d38a };   // pale gold: the fifth element
    juce::Colour reasonColour (int r)
    {
        switch (r)
        {
            case qn::roleTone:  return Colours::verdigris;
            case qn::masking:   return Colours::amethyst;
            case qn::mud:       return Colours::brass;
            case qn::harshness: return Colours::ember;
            case qn::yours:     return Colours::moss;
            case qn::resonance: return juce::Colour (0xffe89a5a);
            case qn::masterBalance: return juce::Colour (0xff9fb8ff);
            default:            return Colours::dimText;
        }
    }
}

QuintessenceEditor::QuintessenceEditor (QuintessenceProcessor& p)
    : AudioProcessorEditor (p), processor (p),
      autoEq (p.apvts, qn::autoEqId, "Auto EQ", quint),
      autoMb (p.apvts, qn::autoMbId, "Auto dynamics", quint),
      role (p.apvts, qn::roleId, "Role"),
      lowCut (p.apvts, qn::lowCutId, "Low cut", quint), highCut (p.apvts, qn::highCutId, "High cut", quint),
      width (p.apvts, qn::widthId, "Width", quint),
      lowLock (p.apvts, qn::lowCutLockId, "Locked", quint), highLock (p.apvts, qn::highCutLockId, "Locked", quint),
      widthLock (p.apvts, qn::widthLockId, "Locked", quint)
{
    setLookAndFeel (&lnf);
    relearnButton.onClick = [this] { processor.relearn(); };
    tipsButton.onClick = [this]
    {
        auto tips = processor.spireTips();
        const auto start = processor.spireStart();
        juce::String text;
        if (start.isNotEmpty()) text << "Starting point\n" << start << "\n\n";
        text << (tips.isEmpty() ? juce::String ("Nothing to change in Spire right now (play the track for a few seconds first).")
                                : "From what it hears\n- " + tips.joinIntoString ("\n- "));
        auto label = std::make_unique<juce::Label>();
        label->setText (text, juce::dontSendNotification);
        label->setFont (juce::FontOptions (13.0f));
        label->setJustificationType (juce::Justification::topLeft);
        label->setColour (juce::Label::textColourId, Colours::text);
        label->setSize (460, 80 + 36 * (tips.size() + (start.isNotEmpty() ? 2 : 0)));
        juce::CallOutBox::launchAsynchronously (std::move (label), tipsButton.getScreenBounds(), nullptr);
    };
    for (auto* c : std::initializer_list<juce::Component*> { &autoEq, &autoMb, &role, &relearnButton, &tipsButton, &lowCut, &highCut, &width,
                                                              &lowLock, &highLock, &widthLock })
        addAndMakeVisible (c);
    for (int s = 0; s < qn::numSlots; ++s)
    {
        for (auto [id, cap] : { std::pair { qn::freqId (s), "Freq" }, std::pair { qn::gainId (s), "Gain" }, std::pair { qn::qId (s), "Q" } })
        {
            bandKnobs.push_back (std::make_unique<Knob> (p.apvts, id, cap, quint));
            bandKnobs.back()->maxHeight = 74;
            addAndMakeVisible (*bandKnobs.back());
        }
        bandLocks.push_back (std::make_unique<Toggle> (p.apvts, qn::lockId (s), "Locked", quint));
        addAndMakeVisible (*bandLocks.back());
    }
    for (int b = 0; b < qn::numMbBands; ++b)
    {
        for (auto [id, cap] : { std::pair { qn::thrId (b), "Threshold" }, std::pair { qn::ratioId (b), "Ratio" } })
        {
            mbKnobs.push_back (std::make_unique<Knob> (p.apvts, id, cap, Colours::verdigris));
            mbKnobs.back()->maxHeight = 74;
            addAndMakeVisible (*mbKnobs.back());
        }
        mbLocks.push_back (std::make_unique<Toggle> (p.apvts, qn::mbLockId (b), "Locked", Colours::verdigris));
        addAndMakeVisible (*mbLocks.back());
    }
    lowCut.maxHeight = highCut.maxHeight = width.maxHeight = 74;
    autopilotStrip.moves = [&p]
    {
        juce::StringArray m;
        auto& l = p.live;
        if (l.maskNow.load() < -0.5f) m.add ("making room for a more important part (" + juce::String (l.maskNow.load(), 1) + " dB)");
        if (l.liftNow.load() > 0.5f) m.add ("lifting its presence through a busy moment");
        if (l.lowOwnerNow.load() > 0.5f) m.add ("leaving the lows to the kick and bass");
        if (l.sootheNow.load() < -1.0f) m.add ("soothing a ringing band (" + juce::String (l.sootheNow.load(), 1) + " dB)");
        if (l.harmonicsNow.load() > 0.5f) m.add ("adding harmonics for small speakers");
        if (l.hitNowDb.load() < -1.0f) m.add ("evening out a loud hit");
        if (l.noteGainDb.load() < -1.0f || l.noteGainDb.load() > 1.0f) m.add ("evening this bass note (" + juce::String (l.noteGainDb.load(), 1) + " dB)");
        if (l.floorNowDb.load() < -3.0f) m.add ("quietening the tail");
        if (std::abs (l.spreadNow.load()) > 0.08f) m.add (juce::String ("sitting a little ") + (l.spreadNow.load() > 0.0f ? "right" : "left") + " of the other layer");
        if (l.tuneRatioNow.load() < 0.999f) m.add ("kick tuned down " + juce::String (-12.0f * std::log2 (l.tuneRatioNow.load()), 1) + " semitones to the key");
        if (p.widthRideNow.load() < 0.9f) m.add ("narrowing an image that cancels in mono");
        if (p.keyNotchHz.load() > 0.0f) m.add ("notching an out-of-key note at " + juce::String (juce::roundToInt (p.keyNotchHz.load())) + " Hz");
        return m;
    };
    addAndMakeVisible (autopilotStrip);
    setSize (1100, 730);
    startTimerHz (10);
}

QuintessenceEditor::~QuintessenceEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void QuintessenceEditor::drawCurve (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto r = area.toFloat();
    auto xFor = [&] (float hz) { return r.getX() + r.getWidth() * std::log (hz / 20.0f) / std::log (1000.0f); };
    auto yFor = [&] (float db) { return r.getCentreY() - db / 12.0f * r.getHeight() * 0.5f; };

    g.setColour (Colours::outline);
    for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        g.drawVerticalLine ((int) xFor (hz), r.getY(), r.getBottom());
    for (float db : { -6.0f, 0.0f, 6.0f }) g.drawHorizontalLine ((int) yFor (db), r.getX(), r.getRight());

    // Measured minus target, per octave band: what the automation is steering out
    for (int b = 0; b < analysis::numBands; ++b)
    {
        const auto d = juce::jlimit (-12.0f, 12.0f, processor.shownDiff[(size_t) b].load());
        if (std::abs (d) < 0.01f) continue;
        const auto x = xFor (analysis::bandCentres()[(size_t) b]);
        g.setColour ((std::abs (d) > 2.0f ? Colours::ember : Colours::dimText).withAlpha (0.45f));
        g.fillRect (juce::Rectangle<float> (x - 7.0f, juce::jmin (yFor (0.0f), yFor (d)), 14.0f, std::abs (yFor (d) - yFor (0.0f))));
    }

    // The EQ's response
    auto& s = processor.apvts;
    auto v = [&] (const juce::String& id) { return s.getRawParameterValue (id)->load(); };
    juce::Path curve;
    for (int i = 0; i <= 300; ++i)
    {
        const auto hz = 20.0f * std::pow (1000.0f, (float) i / 300.0f);
        float db = 0.0f;
        for (int sl = 0; sl < qn::numSlots; ++sl)
        {
            const auto f0 = v (qn::freqId (sl)), q = v (qn::qId (sl)), gain = v (qn::gainId (sl));
            const auto t = q * (hz / f0 - f0 / hz);
            db += gain / (1.0f + t * t);
        }
        if (v (qn::lowCutId) > qn::lowCutOff)   db -= 10.0f * std::log10 (1.0f + std::pow (v (qn::lowCutId) / hz, 8.0f));
        if (v (qn::highCutId) < qn::highCutOff) db -= 10.0f * std::log10 (1.0f + std::pow (hz / v (qn::highCutId), 8.0f));
        const auto pt = juce::Point<float> (xFor (hz), juce::jlimit (r.getY(), r.getBottom(), yFor (db)));
        if (i == 0) curve.startNewSubPath (pt); else curve.lineTo (pt);
    }
    g.setColour (quint);
    g.strokePath (curve, juce::PathStrokeType (2.0f));

    for (int sl = 0; sl < qn::numSlots; ++sl)
    {
        const auto gain = v (qn::gainId (sl));
        const auto reason = (int) v (qn::reasonId (sl));
        if (reason == qn::free && std::abs (gain) < 0.05f) continue;
        const auto c = juce::Point<float> (xFor (v (qn::freqId (sl))), yFor (juce::jlimit (-12.0f, 12.0f, gain)));
        g.setColour (reasonColour (reason));
        g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre (c));
        g.setColour (Colours::background);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (juce::String (sl + 1), juce::Rectangle<float> (12.0f, 12.0f).withCentre (c), juce::Justification::centred);
    }
}

void QuintessenceEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);
    drawHeader (g, { 20, 12, 1000, 32 }, "QUINTESSENCE", "self-managing EQ and multiband dynamics", quint);
    drawPanel (g, curveArea, "Where it is, and what it's steering out", quint);
    drawPanel (g, controlArea, "Automation");
    drawPanel (g, cutArea, "Cuts and width");
    drawPanel (g, mbArea, "Dynamics (level-neutral)", Colours::verdigris);
    drawPanel (g, logArea, "What it did");
    drawCurve (g, curveArea.reduced (14, 10).withTrimmedTop (18));

    static const char* roleNames[] = { "no role yet", "kick", "bass", "hats", "pads", "lead", "vocal", "FX", "the music bus (glue)" };
    auto c = controlArea.reduced (14, 10).withTrimmedTop (18);
    c.removeFromTop (140);
    g.setFont (juce::FontOptions (13.0f));
    g.setColour (Colours::text);
    const auto learnedSongs = processor.learnedShown.load();
    g.drawText ("Steering towards: " + juce::String (roleNames[juce::jlimit (0, 8, processor.roleShown.load())])
                + (learnedSongs > 0 ? " (your sound, " + juce::String (learnedSongs) + " songs)" : juce::String())
                + (processor.referenceShown.load() ? " + your reference" : "")
                + (processor.groupShown.load() == 1 ? ", breakdown" : ""), c.removeFromTop (20), juce::Justification::centredLeft);
    g.setColour (Colours::dimText);
    g.drawText (juce::String (processor.passes.load()) + " passes, " + juce::String (processor.kept.load()) + " moves kept, "
                + juce::String (processor.undone.load()) + " undone", c.removeFromTop (20), juce::Justification::centredLeft);
    {
        auto v = [this] (const juce::String& id) { return processor.apvts.getRawParameterValue (id)->load(); };
        juce::StringArray extra;
        if (std::abs (v (qn::punchId)) > 0.005f) extra.add ("Punch " + juce::String (v (qn::punchId) > 0.0f ? "+" : "") + juce::String (juce::roundToInt (v (qn::punchId) * 100.0f)) + "%");
        if (v (qn::glueThrId) < -0.05f) extra.add ("Glue " + juce::String (v (qn::glueThrId), 1) + " dB");
        if (v (qn::deessDepthId) > 0.05f) extra.add ("De-ess " + juce::String (v (qn::deessFreqId) / 1000.0f, 1) + " kHz, " + juce::String (processor.deessNow.load(), 1) + " dB now");
        if (! extra.isEmpty())
        {
            g.setColour (Colours::verdigris);
            g.drawText (extra.joinIntoString ("   "), controlArea.reduced (14, 10).withTrimmedTop (18).withTrimmedTop (112).withHeight (18), juce::Justification::centredLeft);
        }
    }
    g.setColour (Colours::dimText);
    g.setFont (juce::FontOptions (12.0f));
    g.drawFittedText ("Touch a band and it's yours: it locks. Unlock it to hand it back.", c.removeFromTop (18), juce::Justification::topLeft, 1);

    for (int s = 0; s < qn::numSlots; ++s)
    {
        const auto reason = (int) processor.apvts.getRawParameterValue (qn::reasonId (s))->load();
        const auto when = (int) processor.apvts.getRawParameterValue (qn::whenId (s))->load();
        drawPanel (g, slotAreas[(size_t) s], "Band " + juce::String (s + 1), reasonColour (reason));
        g.setColour (reasonColour (reason));
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (qn::reasonNames()[reason] + (when == 1 ? " (vocal)" : when == 2 ? " (lead)" : ""),
                    slotAreas[(size_t) s].reduced (10, 6).removeFromTop (16), juce::Justification::centredRight);
        const auto bd = processor.apvts.getRawParameterValue (qn::bdId (s))->load();
        if (std::abs (bd) > 0.05f)
        {
            g.setColour (Colours::dimText);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText ("Breakdowns " + juce::String (bd > 0.0f ? "+" : "") + juce::String (bd, 1) + " dB",
                        slotAreas[(size_t) s].reduced (10, 6).removeFromBottom (44).removeFromTop (14), juce::Justification::centred);
        }
    }

    for (int b = 0; b < qn::numMbBands; ++b)
    {
        const auto m = meterAreas[(size_t) b];
        g.setColour (Colours::outline);
        g.fillRoundedRectangle (m.toFloat(), 3.0f);
        const auto gr = juce::jlimit (0.0f, 12.0f, processor.grNow[(size_t) b].load());
        g.setColour (Colours::verdigris);
        g.fillRoundedRectangle (m.toFloat().withHeight ((float) m.getHeight() * gr / 12.0f), 3.0f);
        g.setColour (Colours::dimText);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (qn::mbBandNames()[b], m.withY (m.getBottom() + 2).withHeight (14).expanded (10, 0), juce::Justification::centred);
    }

    auto l = logArea.reduced (14, 10).withTrimmedTop (18);
    g.setFont (juce::FontOptions (12.0f));
    const auto lines = processor.moveLog();
    if (lines.isEmpty())
    {
        g.setColour (Colours::dimText);
        g.drawFittedText ("Play the track: it listens for a few seconds, makes one move, checks it, and so on.", l, juce::Justification::topLeft, 3);
    }
    for (const auto& line : lines)
    {
        if (l.getHeight() < 16) break;
        g.setColour (line.startsWith ("Undid") ? Colours::ember : line.startsWith ("Kept") ? Colours::moss : Colours::text);
        const auto h = line.length() > 70 ? 30 : 16;
        g.drawFittedText (line, l.removeFromTop (h), juce::Justification::topLeft, 2);
        l.removeFromTop (2);
    }
}

void QuintessenceEditor::resized()
{
    autopilotStrip.setBounds (getLocalBounds().removeFromBottom (suite::ui::AutopilotStrip::height + 8).reduced (16, 4));
    auto bounds = getLocalBounds().withTrimmedBottom (suite::ui::AutopilotStrip::height + 4).reduced (16);
    bounds.removeFromTop (48);
    auto top = bounds.removeFromTop (230);
    controlArea = top.removeFromRight (300);
    top.removeFromRight (12);
    curveArea = top;
    bounds.removeFromTop (12);

    auto slots = bounds.removeFromTop (150);
    const auto w = slots.getWidth() / qn::numSlots;
    for (int s = 0; s < qn::numSlots; ++s)
    {
        slotAreas[(size_t) s] = slots.removeFromLeft (w).reduced (s == 0 ? 0 : 4, 0).withTrimmedRight (s == qn::numSlots - 1 ? 0 : 0);
        auto r = slotAreas[(size_t) s].reduced (6, 8).withTrimmedTop (16);
        bandLocks[(size_t) s]->setBounds (r.removeFromBottom (24));
        const auto kw = r.getWidth() / 3;
        for (int k = 0; k < 3; ++k) bandKnobs[(size_t) (s * 3 + k)]->setBounds (r.removeFromLeft (kw));
    }
    bounds.removeFromTop (12);

    auto bottom = bounds;
    cutArea = bottom.removeFromLeft (280);
    bottom.removeFromLeft (12);
    mbArea = bottom.removeFromLeft (390);
    bottom.removeFromLeft (12);
    logArea = bottom;

    auto c = controlArea.reduced (14, 10).withTrimmedTop (20);
    auto toggles = c.removeFromTop (30);
    autoEq.setBounds (toggles.removeFromLeft (toggles.getWidth() / 2).reduced (2, 0));
    autoMb.setBounds (toggles.reduced (2, 0));
    role.setBounds (c.removeFromTop (54));
    c.removeFromTop (4);
    auto buttons = c.removeFromTop (28);
    relearnButton.setBounds (buttons.removeFromLeft (120));
    buttons.removeFromLeft (8);
    tipsButton.setBounds (buttons.removeFromLeft (120));

    auto cu = cutArea.reduced (10, 8).withTrimmedTop (18);
    const auto third = cu.getWidth() / 3;
    auto left = cu.removeFromLeft (third), middle = cu.removeFromLeft (third), right = cu;
    lowLock.setBounds (left.removeFromBottom (24).reduced (2, 0));
    lowCut.setBounds (left);
    highLock.setBounds (middle.removeFromBottom (24).reduced (2, 0));
    highCut.setBounds (middle);
    widthLock.setBounds (right.removeFromBottom (24).reduced (2, 0));
    width.setBounds (right);

    auto mb = mbArea.reduced (10, 8).withTrimmedTop (18);
    const auto bw = mb.getWidth() / qn::numMbBands;
    for (int b = 0; b < qn::numMbBands; ++b)
    {
        auto r = mb.removeFromLeft (bw).reduced (3, 0);
        mbLocks[(size_t) b]->setBounds (r.removeFromBottom (24));
        meterAreas[(size_t) b] = r.removeFromRight (12).withTrimmedBottom (18).withTrimmedTop (6);
        const auto kh = r.getHeight() / 2;
        mbKnobs[(size_t) (b * 2)]->setBounds (r.removeFromTop (kh));
        mbKnobs[(size_t) (b * 2 + 1)]->setBounds (r);
    }
}
