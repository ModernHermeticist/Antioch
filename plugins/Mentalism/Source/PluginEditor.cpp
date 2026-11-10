#include "PluginEditor.h"
#include "SceneUI.h"

using namespace suite::ui;

namespace
{
    const juce::Colour accent = Colours::verdigris;

    juce::String lufsText (float v) { return v <= -70.0f ? juce::String::fromUTF8 ("\xe2\x80\x93") : juce::String (v, 1); }
    juce::Colour diffColour (float d)
    {
        const auto a = std::abs (d);
        return a <= 1.5f ? Colours::moss : a <= advisor::tonalTolerance ? Colours::brass : Colours::ember;
    }
}

MentalismEditor::MentalismEditor (MentalismProcessor& p)
    : AudioProcessorEditor (p), processor (p),
      loudTarget (p.apvts, Params::loudnessTarget, "Loudness target"),
      tonalTarget (p.apvts, Params::tonalTarget, "Tonal target")
{
    setLookAndFeel (&lnf);

    resetButton.onClick = [this] { processor.resetMeasurements(); };
    roughMixButton.setTooltip ("Sets every Prima Materia track's Trim so it sits at the usual trance level for its role, "
                               "relative to the kick (measured in the drops). Play a drop first.");
    roughMixButton.onClick = [this]
    {
        lastFix = suite::Fix::RoughMix;
        lastFixSeq = suite::FixBus::send (suite::Fix::RoughMix);
        lastUndoSeq = 0;
        fixStatusTicks = 0;
    };
    roughUndoButton.onClick = [this] { undoFixButton.onClick(); };
    roughStatus.setFont (juce::FontOptions (12.0f));
    roughStatus.setJustificationType (juce::Justification::centredRight);
    for (auto* c : std::initializer_list<juce::Component*> { &roughMixButton, &roughUndoButton, &roughStatus })
        addAndMakeVisible (c);
    loadButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose a reference track",
                                                       juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                       "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  if (fc.getResult().existsAsFile())
                                      processor.loadReference (fc.getResult());
                              });
    };

    referenceLabel.setFont (juce::FontOptions (12.0f));
    referenceLabel.setJustificationType (juce::Justification::centredLeft);

    for (auto* c : std::initializer_list<juce::Component*> { &loudTarget, &tonalTarget, &resetButton, &loadButton, &referenceLabel, &balanceTabs })
        addAndMakeVisible (c);
    balanceTabs.setComponentID ("tabs");
    balanceTabs.onChange = [this] (int i)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &tonalTarget, &loadButton, &referenceLabel })
            c->setVisible (i == 0);
        undoFixButton.setVisible (i == 1);
        fixStatus.setVisible (i == 1);
        layoutFixButtons();
        repaint();
    };

    for (int i = 0; i < (int) fixButtons.size(); ++i)
    {
        auto& b = fixButtons[(size_t) i];
        b.setButtonText ("Fix");
        b.setColour (juce::TextButton::buttonColourId, Colours::ember.withAlpha (0.35f));
        b.onClick = [this, i] { sendFix (i); };
        addChildComponent (b);
    }
    undoFixButton.onClick = [this]
    {
        if (lastFixSeq == 0) return;
        lastUndoSeq = suite::FixBus::sendUndo (lastFixSeq);
        fixStatusTicks = 0;
    };
    undoFixButton.setEnabled (false);
    fixStatus.setFont (juce::FontOptions (12.0f));
    fixStatus.setJustificationType (juce::Justification::centredLeft);
    addChildComponent (undoFixButton);
    addChildComponent (fixStatus);

    setSize (1040, 720);
    timerCallback();
    startTimerHz (10);
}

MentalismEditor::~MentalismEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void MentalismEditor::sendFix (int row)
{
    const auto fix = shownFixes[(size_t) row];
    if (fix == suite::Fix::None) return;
    lastFix = fix;
    lastFixSeq = suite::FixBus::send (fix);
    lastUndoSeq = 0;
    fixStatusTicks = 0;

    // The fixed sections need hearing again before the coach judges them.
    using T = suite::SectionType;
    const auto type = fix == suite::Fix::BuildRise ? T::Build : fix == suite::Fix::IntroQuieter ? T::Intro
                    : fix == suite::Fix::LaterDropBigger ? T::Drop : T::Breakdown;
    processor.forgetSections (type, rep.map);
}

void MentalismEditor::updateFixStatus()
{
    ++fixStatusTicks;
    if (lastFixSeq == 0)
    {
        fixStatus.setColour (juce::Label::textColourId, Colours::dimText);
        fixStatus.setText ("Fix applies a small step in the plugins that can help; click again if it needs more.", juce::dontSendNotification);
        undoFixButton.setEnabled (false);
        roughUndoButton.setEnabled (false);
        return;
    }
    if (lastUndoSeq != 0)
    {
        const auto n = suite::FixBus::acknowledgements (lastUndoSeq);
        fixStatus.setColour (juce::Label::textColourId, Colours::text);
        fixStatus.setText (n > 0 ? "Undone in " + juce::String (n) + (n == 1 ? " plugin." : " plugins.") : "Undoing...", juce::dontSendNotification);
        undoFixButton.setEnabled (false);
        roughUndoButton.setEnabled (false);
        if (lastFix == suite::Fix::RoughMix)
            roughStatus.setText (n > 0 ? "Rough mix undone" : "Undoing...", juce::dontSendNotification);
        return;
    }
    const auto n = suite::FixBus::acknowledgements (lastFixSeq);
    undoFixButton.setEnabled (n > 0);
    roughUndoButton.setEnabled (n > 0 && lastFix == suite::Fix::RoughMix);
    if (lastFix == suite::Fix::RoughMix)
    {
        roughStatus.setColour (juce::Label::textColourId, n > 0 ? Colours::moss : fixStatusTicks > 10 ? Colours::ember : Colours::dimText);
        roughStatus.setText (n > 0 ? "Rough mix: " + juce::String (n) + (n == 1 ? " track" : " tracks") + " trimmed"
                             : fixStatusTicks > 10 ? "Rough mix: no changes (needs Prima Materia with roles, including a Kick, and a drop played)"
                                                   : "Rough mix...", juce::dontSendNotification);
    }
    if (n > 0)
    {
        fixStatus.setColour (juce::Label::textColourId, Colours::moss);
        fixStatus.setText ("Applied in " + juce::String (n) + (n == 1 ? " plugin" : " plugins") + " (" + coach::describe (lastFix)
                           + "). Play the section again to re-check.", juce::dontSendNotification);
    }
    else if (fixStatusTicks > 10)
    {
        fixStatus.setColour (juce::Label::textColourId, Colours::ember);
        fixStatus.setText ("Nothing could apply it. It needs " + coach::describe (lastFix).upToFirstOccurrenceOf (":", false, false)
                           + " in the project.", juce::dontSendNotification);
    }
    else
        fixStatus.setText ("Applying...", juce::dontSendNotification);
}

void MentalismEditor::layoutFixButtons()
{
    const auto show = balanceTabs.getSelected() == 1 && rep.arrangementAvailable && rep.map.count > 0;
    auto notes = arrangementNotes;
    for (int i = 0; i < (int) fixButtons.size(); ++i)
    {
        auto row = notes.removeFromTop (21);
        const auto fix = i < rep.arrangement.problems && i < (int) rep.arrangement.fixes.size() ? rep.arrangement.fixes[(size_t) i] : suite::Fix::None;
        shownFixes[(size_t) i] = fix;
        auto& b = fixButtons[(size_t) i];
        b.setBounds (row.removeFromRight (56).reduced (2, 1));
        b.setTooltip (coach::describe (fix));
        b.setVisible (show && fix != suite::Fix::None);
    }
}

void MentalismEditor::timerCallback()
{
    rep = processor.report();
    layoutFixButtons();
    updateFixStatus();
    const auto ref = processor.getReference();
    referenceLabel.setColour (juce::Label::textColourId, ref.valid ? accent : Colours::dimText);
    referenceLabel.setText (processor.isLoadingReference() ? "Analysing reference..."
                            : ref.valid ? "Reference: " + ref.name + "  (" + juce::String (ref.lufs, 1) + " LUFS, "
                                          + juce::String (ref.truePeakDb, 1) + " dBTP)"
                                        : "No reference loaded",
                            juce::dontSendNotification);
    repaint();
}

void MentalismEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);
    drawHeader (g, { 20, 12, 700, 32 }, "MENTALISM", "mix assistant", accent);
    drawPanel (g, loudArea, "Loudness", accent);
    drawPanel (g, stereoArea, "Stereo", Colours::amethyst);
    const auto arrangementTab = balanceTabs.getSelected() == 1;
    drawPanel (g, balanceArea, arrangementTab ? "Arrangement: loudness per section (Ouroboros)"
                                              : "Tonal balance vs target  (bars = whole song, line = last 3 s)",
               arrangementTab ? Colours::ember : Colours::brass);
    drawPanel (g, adviceArea, "Suggestions", Colours::ember);

    paintLoudness (g, loudContent);
    paintStereo (g, stereoContent);
    if (arrangementTab) paintArrangement (g, arrangementContent);
    else                paintBalance (g, balanceContent);
    paintAdvice (g, adviceContent);
}

void MentalismEditor::paintLoudness (juce::Graphics& g, juce::Rectangle<int> area)
{
    auto numbers = area.removeFromTop (78);
    const auto& m = rep.measurement;
    const std::pair<const char*, float> items[] = { { "Momentary", rep.momentary }, { "Short-term", rep.shortTerm },
                                                    { "Integrated", m.integratedLufs } };
    const auto w = numbers.getWidth() / 4;
    for (auto& [label, v] : items)
    {
        auto cell = numbers.removeFromLeft (w);
        g.setColour (Colours::dimText);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (label, cell.removeFromTop (16), juce::Justification::centred);
        g.setColour (juce::String (label) == "Integrated" ? accent : Colours::text);
        g.setFont (juce::FontOptions (30.0f, juce::Font::bold));
        g.drawText (lufsText (v), cell.removeFromTop (36), juce::Justification::centred);
        g.setColour (Colours::dimText);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("LUFS", cell, juce::Justification::centredTop);
    }
    {
        auto cell = numbers;
        const auto over = m.truePeakDb > rep.ceiling;
        g.setColour (Colours::dimText);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("True peak", cell.removeFromTop (16), juce::Justification::centred);
        g.setColour (over ? Colours::ember : Colours::text);
        g.setFont (juce::FontOptions (30.0f, juce::Font::bold));
        g.drawText (m.truePeakDb <= -99.0f ? juce::String::fromUTF8 ("\xe2\x80\x93") : juce::String (m.truePeakDb, 1),
                    cell.removeFromTop (36), juce::Justification::centred);
        g.setColour (Colours::dimText);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("dBTP", cell, juce::Justification::centredTop);
    }

    // Gauge: integrated loudness against the target (+/- 10 LU).
    area.removeFromTop (4);
    auto gauge = area.removeFromTop (22).toFloat().reduced (8.0f, 4.0f);
    g.setColour (Colours::outline);
    g.fillRoundedRectangle (gauge, 4.0f);
    auto xFor = [&] (float lu) { return gauge.getX() + gauge.getWidth() * juce::jlimit (0.0f, 1.0f, (lu + 10.0f) / 20.0f); };
    g.setColour (Colours::moss.withAlpha (0.35f));
    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xFor (-1.0f), gauge.getY(), xFor (1.0f), gauge.getBottom()));
    g.setColour (Colours::text);
    g.drawVerticalLine ((int) xFor (0.0f), gauge.getY() - 3.0f, gauge.getBottom() + 3.0f);
    if (m.integratedLufs > -70.0f)
    {
        const auto x = xFor (m.integratedLufs - rep.targetLufs);
        g.setColour (std::abs (m.integratedLufs - rep.targetLufs) <= 1.0f ? Colours::moss : Colours::brass);
        g.fillEllipse (x - 6.0f, gauge.getCentreY() - 6.0f, 12.0f, 12.0f);
    }
    g.setColour (Colours::dimText);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("-10 LU", area.removeFromTop (14).withTrimmedLeft (8), juce::Justification::left);
    g.drawText ("target " + juce::String (rep.targetLufs, 1) + " LUFS / " + juce::String (rep.ceiling, 1) + " dBTP  +10 LU",
                area.withTrimmedRight (8).withY (area.getY() - 14).withHeight (14), juce::Justification::right);
}

void MentalismEditor::paintStereo (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto& m = rep.measurement;
    auto meter = [&] (juce::Rectangle<int> r, const juce::String& label, float corr, float warnBelow)
    {
        g.setColour (Colours::dimText);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (label + "   " + juce::String (corr, 2), r.removeFromTop (16), juce::Justification::left);
        auto bar = r.removeFromTop (14).toFloat();
        g.setColour (Colours::outline);
        g.fillRoundedRectangle (bar, 3.0f);
        const auto centre = bar.getCentreX();
        const auto x = bar.getX() + bar.getWidth() * (corr + 1.0f) * 0.5f;
        g.setColour (corr < warnBelow ? Colours::ember : Colours::moss);
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (std::min (centre, x), bar.getY(), std::max (centre, x), bar.getBottom()));
        g.setColour (Colours::text.withAlpha (0.5f));
        g.drawVerticalLine ((int) centre, bar.getY(), bar.getBottom());
        g.setColour (Colours::dimText);
        g.setFont (juce::FontOptions (10.0f));
        g.drawText ("-1", r.removeFromTop (12), juce::Justification::left);
        g.drawText ("+1", r.withY (r.getY() - 12).withHeight (12), juce::Justification::right);
    };

    meter (area.removeFromTop (46), "Correlation (full)", m.correlation, 0.2f);
    area.removeFromTop (6);
    meter (area.removeFromTop (46), "Correlation below 120 Hz", m.lowCorrelation, 0.8f);
    area.removeFromTop (6);
    g.setColour (m.monoLossDb < -3.0f ? Colours::ember : Colours::dimText);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("Mono sum: " + juce::String (m.monoLossDb, 1) + " dB vs stereo", area.removeFromTop (18), juce::Justification::left);
}

void MentalismEditor::paintBalance (juce::Graphics& g, juce::Rectangle<int> area)
{
    auto labels = area.removeFromBottom (18);
    const auto r = area.toFloat();
    g.setColour (Colours::background);
    g.fillRoundedRectangle (r, 6.0f);

    if (! rep.tonalAvailable)
    {
        g.setColour (Colours::dimText);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText ("Load a reference track to compare against it", r, juce::Justification::centred);
        return;
    }

    constexpr float range = 12.0f; // +/- dB shown
    auto yFor = [&] (float d) { return r.getCentreY() - r.getHeight() * 0.5f * juce::jlimit (-1.0f, 1.0f, d / range); };

    // Tolerance zone and grid
    g.setColour (Colours::moss.withAlpha (0.08f));
    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (r.getX(), yFor (advisor::tonalTolerance), r.getRight(), yFor (-advisor::tonalTolerance)));
    g.setColour (Colours::outline);
    for (float d : { -9.0f, -6.0f, -3.0f, 3.0f, 6.0f, 9.0f })
        g.drawHorizontalLine ((int) yFor (d), r.getX(), r.getRight());
    g.setColour (Colours::dimText.withAlpha (0.6f));
    g.drawHorizontalLine ((int) yFor (0.0f), r.getX(), r.getRight());
    g.setFont (juce::FontOptions (10.0f));
    for (float d : { -6.0f, 6.0f })
        g.drawText ((d > 0 ? "+" : "") + juce::String ((int) d) + " dB", juce::Rectangle<float> (r.getX() + 4.0f, yFor (d) - 12.0f, 50.0f, 12.0f),
                    juce::Justification::left);

    const auto colW = r.getWidth() / (float) analysis::numBands;
    const auto haveLong = rep.measuredSeconds >= 2.0f;
    for (int b = 0; b < analysis::numBands; ++b)
    {
        const auto x = r.getX() + colW * (float) b;
        if (haveLong)
        {
            const auto d = rep.longDiff[(size_t) b];
            g.setColour (diffColour (d).withAlpha (0.8f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x + colW * 0.25f, std::min (yFor (0.0f), yFor (d)),
                                                                  x + colW * 0.75f, std::max (yFor (0.0f), yFor (d))));
        }
        const auto s = rep.shortDiff[(size_t) b];
        if (rep.measuredSeconds > 0.0f)
        {
            g.setColour (Colours::text);
            g.drawLine (x + colW * 0.15f, yFor (s), x + colW * 0.85f, yFor (s), 2.0f);
        }

        const auto hz = analysis::bandCentres()[(size_t) b];
        g.setColour (Colours::dimText);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (hz >= 1000.0f ? juce::String ((int) (hz / 1000.0f)) + "k" : juce::String ((int) hz),
                    juce::Rectangle<int> ((int) x, labels.getY(), (int) colW, labels.getHeight()), juce::Justification::centred);
    }

    g.setColour (Colours::dimText);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("measured " + juce::String ((int) rep.measuredSeconds) + " s of audio", r.reduced (8.0f, 4.0f),
                juce::Justification::topRight);
}

void MentalismEditor::paintArrangement (juce::Graphics& g, juce::Rectangle<int> area)
{
    if (! rep.arrangementAvailable || rep.map.count == 0)
    {
        g.setColour (Colours::dimText);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText ("Load Ouroboros and play the song to see loudness per section", area, juce::Justification::centred);
        return;
    }

    auto notes = area.removeFromBottom (juce::jmin (88, area.getHeight() / 3));
    notes.removeFromRight (60);   // room for the Fix buttons
    const auto chart = area.toFloat();
    g.setColour (Colours::background);
    g.fillRoundedRectangle (chart, 6.0f);

    // Loudest heard section sets the scale: bars show -24 LU .. 0 LU relative to it.
    float top = -100.0f;
    for (int i = 0; i < rep.map.count; ++i) if (rep.sections[(size_t) i].heard()) top = std::max (top, rep.sections[(size_t) i].lufs());
    const auto total = (float) juce::jmax (1, rep.map.totalBars);
    for (int i = 0; i < rep.map.count; ++i)
    {
        const auto& sec = rep.map.sections[(size_t) i];
        const auto x0 = chart.getX() + chart.getWidth() * (float) (sec.startBar - 1) / total;
        const auto x1 = chart.getX() + chart.getWidth() * (float) (rep.map.endBar (i) - 1) / total;
        const auto colour = suite::ui::sectionColour (sec.type);
        const auto& st = rep.sections[(size_t) i];
        auto column = juce::Rectangle<float>::leftTopRightBottom (x0, chart.getY(), x1, chart.getBottom()).reduced (2.0f, 4.0f);

        if (st.heard())
        {
            const auto rel = juce::jlimit (0.0f, 1.0f, (st.lufs() - (top - 24.0f)) / 24.0f);
            g.setColour (colour.withAlpha (0.55f));
            g.fillRoundedRectangle (column.withTop (column.getBottom() - column.getHeight() * 0.85f * rel), 3.0f);
            g.setColour (Colours::text);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (juce::String (st.lufs(), 1), column.withHeight (16.0f).translated (0.0f, 2.0f), juce::Justification::centred);
        }
        else
        {
            g.setColour (colour.withAlpha (0.12f));
            g.fillRoundedRectangle (column, 3.0f);
        }
        g.setColour (colour);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawFittedText (juce::String (suite::sectionName (sec.type)).toUpperCase(), column.removeFromBottom (14.0f).toNearestInt(),
                          juce::Justification::centred, 1, 0.5f);
    }

    g.setFont (juce::FontOptions (12.5f));
    for (int i = 0; i < juce::jmin (4, rep.arrangement.notes.size()); ++i)
    {
        auto row = notes.removeFromTop (21);
        const auto problem = i < rep.arrangement.problems;
        g.setColour (problem ? Colours::ember : Colours::moss);
        g.fillEllipse ((float) row.getX() + 2.0f, (float) row.getCentreY() - 3.0f, 6.0f, 6.0f);
        g.setColour (Colours::text);
        g.drawFittedText (rep.arrangement.notes[i], row.withTrimmedLeft (14), juce::Justification::centredLeft, 1, 0.8f);
    }
}

void MentalismEditor::paintAdvice (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setFont (juce::FontOptions (13.0f));
    for (const auto& line : rep.advice)
    {
        auto row = area.removeFromTop (22);
        g.setColour (Colours::ember);
        g.fillEllipse ((float) row.getX() + 2.0f, (float) row.getCentreY() - 3.0f, 6.0f, 6.0f);
        g.setColour (Colours::text);
        g.drawFittedText (line, row.withTrimmedLeft (16), juce::Justification::centredLeft, 1, 0.85f);
    }
}

void MentalismEditor::resized()
{
    auto bounds = getLocalBounds().reduced (16);
    auto header = bounds.removeFromTop (48);
    resetButton.setBounds (header.removeFromRight (110).reduced (4, 8));
    roughUndoButton.setBounds (header.removeFromRight (70).reduced (4, 8));
    roughMixButton.setBounds (header.removeFromRight (110).reduced (4, 8));
    roughStatus.setBounds (header.removeFromRight (300).reduced (4, 8));

    auto top = bounds.removeFromTop (190);
    loudArea = top.removeFromLeft (top.getWidth() * 62 / 100);
    top.removeFromLeft (12);
    stereoArea = top;
    bounds.removeFromTop (12);

    adviceArea = bounds.removeFromBottom (170);
    bounds.removeFromBottom (12);
    balanceArea = bounds;

    auto l = loudArea.reduced (12, 6).withTrimmedTop (14);
    loudTarget.setBounds (l.removeFromRight (180).withHeight (60));
    loudContent = l;

    stereoContent = stereoArea.reduced (14, 8).withTrimmedTop (18);

    auto b = balanceArea.reduced (12, 6).withTrimmedTop (14);
    auto controls = b.removeFromTop (54);
    tonalTarget.setBounds (controls.removeFromLeft (220));
    loadButton.setBounds (controls.removeFromLeft (150).withTrimmedTop (18).reduced (4, 2));
    referenceLabel.setBounds (controls.withTrimmedTop (18).withTrimmedLeft (8));
    b.removeFromTop (6);
    balanceContent = b;
    arrangementContent = balanceArea.reduced (12, 6).withTrimmedTop (60);
    arrangementNotes = arrangementContent.withTop (arrangementContent.getBottom() - juce::jmin (88, arrangementContent.getHeight() / 3));
    {
        auto c = balanceArea.reduced (12, 6).withTrimmedTop (14).removeFromTop (54).withTrimmedTop (18);
        undoFixButton.setBounds (c.removeFromLeft (130).reduced (0, 4));
        fixStatus.setBounds (c.withTrimmedLeft (8));
    }
    layoutFixButtons();
    balanceTabs.setBounds (balanceArea.getRight() - 372, balanceArea.getY() + 6, 360, 28);

    adviceContent = adviceArea.reduced (14, 8).withTrimmedTop (18);
}
