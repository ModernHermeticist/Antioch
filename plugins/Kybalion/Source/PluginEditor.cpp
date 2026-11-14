#include "PluginEditor.h"
#include "KitBus.h"
#include "KeyBus.h"

using namespace suite::ui;

namespace
{
    const juce::Colour gateColour = Colours::amethyst, duckColour = Colours::moss;
}

//==============================================================================
StepGrid::StepGrid (KybalionProcessor& p) : processor (p)
{
    for (int i = 0; i < Params::numSteps; ++i)
        params[(size_t) i] = p.apvts.getParameter (Params::Gate::step (i));
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

juce::Rectangle<float> StepGrid::stepBounds (int i) const
{
    const auto w = (float) getWidth() / (float) Params::numSteps;
    return { (float) i * w, 0.0f, w, (float) getHeight() };
}

void StepGrid::paint (juce::Graphics& g)
{
    g.setColour (Colours::background);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

    for (int i = 0; i < Params::numSteps; ++i)
    {
        auto r = stepBounds (i).reduced (3.0f, 6.0f);
        const auto level = params[(size_t) i]->convertFrom0to1 (params[(size_t) i]->getValue());
        const auto beatStart = i % 4 == 0;

        g.setColour (beatStart ? Colours::outline.brighter (0.15f) : Colours::outline);
        g.fillRoundedRectangle (r, 3.0f);

        if (level > 0.001f)
        {
            const auto lit = i == playing;
            g.setColour (lit ? gateColour.brighter (0.4f) : gateColour.withAlpha (0.55f + 0.45f * level));
            g.fillRoundedRectangle (r.withTop (r.getBottom() - r.getHeight() * level), 3.0f);
        }
        if (i == playing)
        {
            g.setColour (Colours::text);
            g.drawRoundedRectangle (r, 3.0f, 1.5f);
        }
    }
}

void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    // Left-drag draws levels, right-drag erases.
    erasing = e.mods.isRightButtonDown();
    for (auto* p : params) p->beginChangeGesture();
    edit (e);
}

void StepGrid::mouseDrag (const juce::MouseEvent& e) { edit (e); }

void StepGrid::mouseUp (const juce::MouseEvent&)
{
    for (auto* p : params) p->endChangeGesture();
}

void StepGrid::edit (const juce::MouseEvent& e)
{
    const auto i = juce::jlimit (0, Params::numSteps - 1, (int) ((float) e.x / ((float) getWidth() / Params::numSteps)));
    const auto r = stepBounds (i).reduced (3.0f, 6.0f);
    auto level = erasing ? 0.0f : juce::jlimit (0.0f, 1.0f, (r.getBottom() - (float) e.y) / r.getHeight());
    if (level < 0.06f) level = 0.0f;          // bottom edge = off
    if (level > 0.94f) level = 1.0f;          // top edge = full
    params[(size_t) i]->setValueNotifyingHost (params[(size_t) i]->convertTo0to1 (level));
    repaint();
}

void StepGrid::refresh()
{
    playing = processor.gateStep.load();
    repaint();
}

//==============================================================================
void DuckView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Colours::background);
    g.fillRoundedRectangle (r, 6.0f);

    auto meter = r.removeFromRight (26.0f).reduced (6.0f, 10.0f);
    auto plot = r.reduced (12.0f, 14.0f);

    auto& s = processor.apvts;
    auto v = [&] (const juce::String& id) { return s.getRawParameterValue (id)->load(); };
    const auto depth = v (Params::Duck::depth), attack = v (Params::Duck::attack), hold = v (Params::Duck::hold);
    const auto release = v (Params::Duck::release), curve = v (Params::Duck::curve);
    const auto breakdown = v (Params::Duck::breakdown) > 0.5f;
    const auto cycleMs = (float) (Params::Duck::rateQuarters ((int) v (Params::Duck::rate)) * 60000.0 / processor.currentBpm.load());

    auto amountAt = [&] (float ms)
    {
        if (breakdown) return 0.0f;
        if (ms < attack) return depth * ms / juce::jmax (0.001f, attack);
        ms -= attack;
        if (ms < hold) return depth;
        const auto x = (ms - hold) / juce::jmax (1.0f, release);
        return x >= 1.0f ? 0.0f : depth * (1.0f - std::pow (x, curve));
    };

    // Grid: beat lines
    g.setColour (Colours::outline);
    g.drawHorizontalLine ((int) plot.getY(), plot.getX(), plot.getRight());
    g.drawHorizontalLine ((int) plot.getBottom(), plot.getX(), plot.getRight());

    // Two cycles of the gain curve
    juce::Path path;
    const auto points = 240;
    for (int i = 0; i <= points; ++i)
    {
        const auto t = (float) i / (float) points * 2.0f * cycleMs;
        const auto gain = 1.0f - amountAt (std::fmod (t, cycleMs));
        const auto x = plot.getX() + plot.getWidth() * (float) i / (float) points;
        const auto y = plot.getBottom() - plot.getHeight() * gain;
        if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
    }
    juce::Path fill (path);
    fill.lineTo (plot.getRight(), plot.getBottom());
    fill.lineTo (plot.getX(), plot.getBottom());
    fill.closeSubPath();
    g.setColour (duckColour.withAlpha (0.15f));
    g.fillPath (fill);
    g.setColour (breakdown ? Colours::dimText : duckColour);
    g.strokePath (path, juce::PathStrokeType (2.0f));

    // Live marker (only meaningful while ducking is cycling)
    const auto ms = processor.duckMs.load();
    if (ms < cycleMs * 2.0f)
    {
        const auto x = plot.getX() + plot.getWidth() * 0.5f * std::fmod (ms, cycleMs) / cycleMs;
        g.setColour (Colours::text.withAlpha (0.6f));
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
    }

    g.setColour (Colours::dimText);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (breakdown ? "BREAKDOWN: ducking off" : "gain over two beats", plot.removeFromTop (14.0f), juce::Justification::topRight);

    // Gain-reduction meter
    g.setColour (Colours::outline);
    g.fillRoundedRectangle (meter, 3.0f);
    const auto gr = 1.0f - processor.duckGain.load();
    g.setColour (duckColour);
    g.fillRoundedRectangle (meter.withHeight (meter.getHeight() * juce::jlimit (0.0f, 1.0f, gr)), 3.0f);
}

//==============================================================================
KybalionEditor::KybalionEditor (KybalionProcessor& p)
    : AudioProcessorEditor (p), processor (p),
      header ("KYBALION", juce::String::fromUTF8 ("rhythm \xc2\xb7 polarity \xc2\xb7 trance bus"), gateColour),
      gateOn (p.apvts, Params::gateOn, "RHYTHM ON", gateColour),
      duckOn (p.apvts, Params::duckOn, "POLARITY ON", duckColour),
      followSections (p.apvts, Params::follow, "Follow Ouroboros", Colours::ember),
      output (p.apvts, Params::output, "Output", Colours::text),
      tabs ({ "RHYTHM  (gate)", "POLARITY  (duck)", "SCENES" }, { gateColour, duckColour, Colours::ember }),
      grid (p),
      gateRate (p.apvts, Params::Gate::rate, "Step size"),
      fills (p.apvts, Params::Fill::when, "Fills before"),
      fillStyle (p.apvts, Params::Fill::style, "Fill style"),
      chordFollow (p.apvts, Params::Fill::chords, "Chord changes"),
      length (p.apvts, Params::Gate::length, "Length", gateColour),
      attack (p.apvts, Params::Gate::attack, "Attack", gateColour),
      release (p.apvts, Params::Gate::release, "Release", gateColour),
      depth (p.apvts, Params::Gate::depth, "Depth", gateColour),
      swing (p.apvts, Params::Gate::swing, "Swing", Colours::brass),
      stereo (p.apvts, Params::Gate::stereo, "Stereo offset", Colours::verdigris),
      filter (p.apvts, Params::Gate::filter, "Filter", Colours::ember),
      res (p.apvts, Params::Gate::res, "Resonance", Colours::ember),
      panSteps (p.apvts, Params::Gate::pan, "Pan steps"),
      duckView (p),
      source (p.apvts, Params::Duck::source, "Trigger"),
      duckRate (p.apvts, Params::Duck::rate, "Every"),
      nudge (p.apvts, Params::Duck::nudge, "Nudge", Colours::brass),
      duckDepth (p.apvts, Params::Duck::depth, "Depth", duckColour),
      duckAttack (p.apvts, Params::Duck::attack, "Attack", duckColour),
      hold (p.apvts, Params::Duck::hold, "Hold", duckColour),
      duckRelease (p.apvts, Params::Duck::release, "Release", duckColour),
      curve (p.apvts, Params::Duck::curve, "Curve", duckColour),
      split (p.apvts, Params::Duck::split, "Split", Colours::verdigris),
      threshold (p.apvts, Params::Duck::threshold, "SC threshold", Colours::brass),
      breakdown (p.apvts, Params::Duck::breakdown, "BREAKDOWN", Colours::ember),
      shareKick (p.apvts, Params::Duck::shareKick, "Share kick", Colours::ember),
      flip (p.apvts, Params::Duck::flip, "Flip polarity", Colours::ember)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (header);
    header.addRight (followSections, 170);
    header.actions = [this] { return actions(); };
    header.enableAdvanced (p.apvts.state, [this] (bool) { resized(); repaint(); });

    patternBox.setTextWhenNothingSelected ("Gate pattern...");
    fillPatterns();
    patternBox.onChange = [this]
    {
        const auto id = patternBox.getSelectedId();
        const auto builtIn = (int) Params::Gate::patterns().size();
        if (id <= 0) return;
        if (id == 990 || id == 991)
        {
            patternBox.setSelectedId (0, juce::dontSendNotification);
            if (! processor.applyKitRhythm (id == 991))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "Gate pattern",
                                                        "No song rhythm yet: switch on Correspondence's pattern library (Song page) and its key broadcast.");
            return;
        }
        if (id == 999)
        {
            patternBox.setSelectedId (0, juce::dontSendNotification);
            nameWindow = std::make_unique<juce::AlertWindow> ("Save gate pattern", "Name for this pattern:", juce::MessageBoxIconType::NoIcon);
            nameWindow->addTextEditor ("name", "My gate");
            nameWindow->addButton ("Save", 1);
            nameWindow->addButton ("Cancel", 0);
            nameWindow->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int result)
            {
                if (result == 1 && nameWindow != nullptr && nameWindow->getTextEditorContents ("name").trim().isNotEmpty())
                    processor.saveGatePattern (KybalionProcessor::userPatternFolder(), nameWindow->getTextEditorContents ("name").trim());
                nameWindow.reset();
                fillPatterns();
            }));
            return;
        }
        if (id >= 800 && id < 800 + (int) favourites.size()) { processor.applySteps (favourites[(size_t) (id - 800)].first); return; }
        if (id <= builtIn) processor.applyGatePattern (id - 1);
        else if (id - builtIn - 1 < (int) userPatterns.size()) processor.applySteps (userPatterns[(size_t) (id - builtIn - 1)].second);
    };
    randomButton.onClick = [this] { juce::Random rng; processor.randomiseGate (rng); patternBox.setSelectedId (0, juce::dontSendNotification); };

    presetBox.setTextWhenNothingSelected ("Ducking preset...");
    for (size_t i = 0; i < Params::Duck::presets().size(); ++i)
        presetBox.addItem (Params::Duck::presets()[i].name, (int) i + 1);
    presetBox.onChange = [this] { if (presetBox.getSelectedId() > 0) processor.applyDuckPreset (presetBox.getSelectedId() - 1); };

    fitButton.setTooltip ("Set Hold and Release from the kick on the sidechain, so the duck recovers as the kick's tail dies away");
    fitButton.onClick = [this] { processor.fitToKick(); };
    duckPanel.addAndMakeVisible (fitButton);
    shareKick.setTooltip ("On the kick track: learn where the kick hits and share it, so every Kybalion set to 'Suite kick' ducks to it with no routing");
    duckPanel.addAndMakeVisible (shareKick);
    sourceStatus.setFont (juce::FontOptions (12.0f));
    sourceStatus.setJustificationType (juce::Justification::centredLeft);

    for (auto* c : std::initializer_list<juce::Component*> { &grid, &patternBox, &randomButton, &gateRate, &length, &attack,
                                                             &release, &depth, &swing, &stereo, &panSteps, &filter, &res, &fills, &fillStyle, &chordFollow })
        gatePanel.addAndMakeVisible (c);
    dynamicGate = std::make_unique<suite::ui::Toggle> (p.apvts, Params::Gate::dynamic, "Dynamic gate", Colours::amethyst);
    dynamicGate->setTooltip ("With Ouroboros: each drop starts the gate at 60% of its depth and deepens to the full depth by its end; "
                             "other sections follow the energy curve");
    gatePanel.addAndMakeVisible (*dynamicGate);
    for (auto* c : std::initializer_list<juce::Component*> { &duckView, &presetBox, &source, &duckRate, &nudge, &duckDepth,
                                                             &duckAttack, &hold, &duckRelease, &curve, &split, &threshold,
                                                             &breakdown, &sourceStatus, &flip })
        duckPanel.addAndMakeVisible (c);

    sectionStatus.setFont (juce::FontOptions (12.0f));
    sectionStatus.setJustificationType (juce::Justification::centredLeft);
    for (auto* c : std::initializer_list<juce::Component*> { &gateOn, &duckOn, &sectionStatus, &output,
                                                             &tabs, &gatePanel, &duckPanel })
        addAndMakeVisible (c);

    scenePanel = std::make_unique<suite::ui::ScenePanel> (p.scenes, p.apvts);
    addAndMakeVisible (*scenePanel);
    tabs.onChange = [this] (int i) { gatePanel.setVisible (i == 0); duckPanel.setVisible (i == 1); scenePanel->setVisible (i == 2); };
    tabs.onChange (0);
    tabs.setComponentID ("tabs");

    tweakBanner = std::make_unique<suite::ui::TweakBanner> (p.scenes);   // "make it a scene?" after you tweak a knob in a section
    addChildComponent (*tweakBanner);
    setSize (980, 694);
    directorStrip.director = [] { return suite::DirectorBus::cue (suite::director::pKybalion); };
    addAndMakeVisible (directorStrip);
    timerCallback();
    startTimerHz (30);
}

KybalionEditor::~KybalionEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void KybalionEditor::timerCallback()
{
    suite::Watchdog::Scope watch (processor.getName(), "editor refresh");
    {
        const auto sec = processor.currentSection.load();
        const auto on = processor.apvts.getRawParameterValue (Params::gateOn)->load() > 0.5f || processor.apvts.getRawParameterValue (Params::duckOn)->load() > 0.5f;
        header.setStatus (juce::String (processor.currentBpm.load(), 1) + " BPM" + (sec >= 0 ? juce::String::fromUTF8 (" \xc2\xb7 ") + suite::sectionName ((suite::SectionType) sec) : juce::String()),
                          on ? Colours::moss : Colours::faintText);
    }
    grid.refresh();
    duckView.repaint();
    suite::ui::updateSceneGhosts (*this, processor.scenes);

    const auto sec = processor.currentSection.load();
    sectionStatus.setColour (juce::Label::textColourId, processor.autoBreakdown.load() ? Colours::ember : Colours::dimText);
    sectionStatus.setText (sec < 0 ? juce::String()
                                   : juce::String ("Section: ") + suite::sectionName ((suite::SectionType) sec)
                                         + (processor.autoBreakdown.load() ? juce::String::fromUTF8 (" \xc2\xb7 ducking paused") : juce::String())
                                         + (processor.currentFill.load() > 0 ? juce::String::fromUTF8 (" \xc2\xb7 fill: ")
                                                + juce::StringArray { "", "stutter", "half-time", "gap", "roll" }[processor.currentFill.load()] : juce::String()),
                           juce::dontSendNotification);

    const auto wantsSidechain = (int) processor.apvts.getRawParameterValue (Params::Duck::source)->load() == Params::Duck::Sidechain;
    const auto connected = processor.sidechainConnected.load();
    threshold.setEnabled (wantsSidechain || connected);
    const auto heard = connected && processor.kick.hits() >= KybalionProcessor::kicksNeeded;
    fitButton.setEnabled (heard);
    duckRate.setEnabled (! wantsSidechain);
    nudge.setEnabled (! wantsSidechain);
    sourceStatus.setColour (juce::Label::textColourId, wantsSidechain && ! connected ? Colours::ember : Colours::dimText);
    const auto sourceIndex = (int) processor.apvts.getRawParameterValue (Params::Duck::source)->load();
    const auto sharingKick = processor.apvts.getRawParameterValue (Params::Duck::shareKick)->load() > 0.5f;
    if (sharingKick)
    {
        sourceStatus.setColour (juce::Label::textColourId, Colours::ember);
        sourceStatus.setText ("Sharing this track's kick with the suite (" + juce::String (processor.kickLearner.barsLearned()) + " bars learned)",
                              juce::dontSendNotification);
    }
    else if (sourceIndex == Params::Duck::SuiteKick)
    {
        const auto ok = processor.suiteKickAvailable.load();
        sourceStatus.setColour (juce::Label::textColourId, ok ? Colours::text : Colours::ember);
        sourceStatus.setText (ok ? "Ducking to the suite kick (no routing needed)"
                                 : "Suite kick: none shared yet, ducking on the beat grid. Turn on Share kick on the kick track.",
                              juce::dontSendNotification);
    }
    else if (heard)
    {
        const auto key = suite::KeyBus::read();
        sourceStatus.setColour (juce::Label::textColourId, Colours::text);
        juce::String phase;
        if (processor.phaseMeasured.load())
        {
            const auto c = processor.lowCorrelation.load();
            phase = c < -0.3f ? juce::String::fromUTF8 (" \xc2\xb7 low end OUT OF PHASE with the kick (") + juce::String (c, 2) + "): turn on Flip polarity"
                  : c > 0.3f ? juce::String::fromUTF8 (" \xc2\xb7 low end in phase with the kick")
                             : juce::String::fromUTF8 (" \xc2\xb7 low end and kick neither help nor cancel");
        }
        sourceStatus.setText ("Kick: " + juce::String (juce::roundToInt (processor.kick.lengthMs())) + " ms, "
                                  + describeKickTuning (processor.kick.pitchHz(), key.valid ? key.root : -1) + phase,
                              juce::dontSendNotification);
        sourceStatus.setTooltip (sourceStatus.getText());
    }
    else
        sourceStatus.setText (! wantsSidechain ? (connected ? "Ducks on the beat grid; play to analyse the kick on the sidechain"
                                                            : "Ducks on the beat grid: no routing needed")
                              : connected ? "Ducking from the sidechain input"
                                          : "No sidechain routed: using the beat grid",
                              juce::dontSendNotification);
}

void KybalionEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);
    drawPanel (g, masterArea, "Master");

    const auto drawSection = [&] (juce::Component& panel, std::initializer_list<std::pair<juce::Rectangle<int>, juce::String>> areas,
                                  juce::Colour colour)
    {
        if (! panel.isVisible()) return;
        for (auto& [area, title] : areas)
            drawPanel (g, area + panel.getPosition(), title, colour);
    };

    const auto gb = gatePanel.getLocalBounds();
    drawSection (gatePanel, { { gb.withHeight (300), "Pattern  (click or drag to draw, right-drag to erase)" },
                              { gb.withTrimmedTop (310), "Shape" } }, gateColour);
    const auto db = duckPanel.getLocalBounds();
    drawSection (duckPanel, { { db.withHeight (250), "Ducking curve" }, { db.withTrimmedTop (260), "Shape & trigger" } }, duckColour);
}

void KybalionEditor::fillPatterns()
{
    patternBox.clear (juce::dontSendNotification);
    userPatterns = KybalionProcessor::loadGatePatterns (KybalionProcessor::userPatternFolder());
    // Your most used patterns first (from the songs you played to the end).
    favourites = KybalionProcessor::favouritePatterns (suite::StyleLog::read());
    if (favourites.size() > 3) favourites.resize (3);
    if (! favourites.empty())
    {
        patternBox.addSectionHeading ("Your most used");
        for (size_t i = 0; i < favourites.size(); ++i)
        {
            juce::String name = "Your pattern";
            for (const auto& p : Params::Gate::patterns())
            {
                KybalionProcessor::Steps s {};
                std::copy (p.steps.begin(), p.steps.end(), s.begin());
                if (KybalionProcessor::stepsText (s) == KybalionProcessor::stepsText (favourites[i].first)) name = p.name;
            }
            for (const auto& [n, steps] : userPatterns)
                if (KybalionProcessor::stepsText (steps) == KybalionProcessor::stepsText (favourites[i].first)) name = "Mine: " + n;
            patternBox.addItem (name + " (" + juce::String (favourites[i].second) + (favourites[i].second == 1 ? " song)" : " songs)"), 800 + (int) i);
        }
        patternBox.addSeparator();
    }
    for (size_t i = 0; i < Params::Gate::patterns().size(); ++i)
        patternBox.addItem (Params::Gate::patterns()[i].name, (int) i + 1);
    if (! userPatterns.empty())
    {
        patternBox.addSeparator();
        for (size_t i = 0; i < userPatterns.size(); ++i)
            patternBox.addItem ("Mine: " + userPatterns[i].first, (int) (Params::Gate::patterns().size() + i + 1));
    }
    patternBox.addSeparator();
    {
        const auto kit = suite::KitBus::read();
        patternBox.addItem (kit.valid ? "From the song's chords (" + juce::String (kit.name) + ")" : juce::String ("From the song's chords (Correspondence's pattern library)"), 990);
        patternBox.addItem (kit.valid ? "From the song's arp" : juce::String ("From the song's arp"), 991);
    }
    patternBox.addItem ("Save this pattern...", 999);
}

void KybalionEditor::resized()
{
    directorStrip.setBounds (getLocalBounds().removeFromBottom (suite::ui::AutopilotStrip::height + 8).reduced (16, 4));
    auto bounds = getLocalBounds().withTrimmedBottom (suite::ui::AutopilotStrip::height + 4).reduced (16);
    header.setBounds (bounds.removeFromTop (HeaderBar::height).withTrimmedTop (-8));
    bounds.removeFromTop (10);
    const auto advanced = header.advanced();
    setShown ({ &length, &attack, &release, &stereo, &panSteps, &filter, &res, &fillStyle, &chordFollow,
                &duckRate, &nudge, &duckAttack, &hold, &curve, &split, &threshold }, advanced);
    masterArea = bounds.removeFromTop (70);
    auto m = masterArea.reduced (14, 8).withTrimmedTop (10);
    gateOn.setBounds (m.removeFromLeft (150).withSizeKeepingCentre (140, 30));
    duckOn.setBounds (m.removeFromLeft (150).withSizeKeepingCentre (140, 30));
    followSections.setBounds (m.removeFromLeft (170).withSizeKeepingCentre (160, 30));
    sectionStatus.setBounds (m.removeFromLeft (360).withTrimmedLeft (10));
    output.setBounds (m.removeFromRight (120).withTrimmedTop (-14).withTrimmedBottom (-4));
    output.slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 16);
    output.maxHeight = 60;

    bounds.removeFromTop (10);
    tabs.setBounds (bounds.removeFromTop (34));
    bounds.removeFromTop (10);
    gatePanel.setBounds (bounds);
    duckPanel.setBounds (bounds);
    if (scenePanel != nullptr) scenePanel->setBounds (bounds);

    // ---- Rhythm ----
    {
        auto top = gatePanel.getLocalBounds().withHeight (300).reduced (14, 8).withTrimmedTop (16);
        auto bar = top.removeFromTop (30);
        randomButton.setBounds (bar.removeFromRight (110).reduced (2));
        patternBox.setBounds (bar.removeFromRight (200).reduced (2));
        dynamicGate->setBounds (bar.removeFromRight (150).reduced (2));
        top.removeFromTop (8);
        grid.setBounds (top);

        auto shape = gatePanel.getLocalBounds().withTrimmedTop (310).reduced (10, 6).withTrimmedTop (12);
        if (advanced) layoutRow (shape, { &gateRate, &length, &attack, &release, &depth, &swing, &stereo, &panSteps, &filter, &res, &fills, &fillStyle, &chordFollow });
        else          layoutRow (shape.withSizeKeepingCentre (juce::jmin (shape.getWidth(), 600), shape.getHeight()), { &gateRate, &depth, &swing, &fills });
    }

    // ---- Polarity ----
    {
        auto top = duckPanel.getLocalBounds().withHeight (250).reduced (14, 8).withTrimmedTop (16);
        auto bar = top.removeFromTop (30);
        breakdown.setBounds (bar.removeFromRight (130).reduced (2));
        presetBox.setBounds (bar.removeFromRight (200).reduced (2));
        fitButton.setBounds (bar.removeFromRight (110).reduced (2));
        shareKick.setBounds (bar.removeFromRight (120).reduced (2));
        flip.setBounds (bar.removeFromRight (120).reduced (2));
        sourceStatus.setBounds (bar);
        top.removeFromTop (8);
        duckView.setBounds (top);

        auto shape = duckPanel.getLocalBounds().withTrimmedTop (260).reduced (10, 6).withTrimmedTop (12);
        if (advanced) layoutRow (shape, { &source, &duckRate, &nudge, &duckDepth, &duckAttack, &hold, &duckRelease, &curve, &split, &threshold });
        else          layoutRow (shape.withSizeKeepingCentre (juce::jmin (shape.getWidth(), 480), shape.getHeight()), { &source, &duckDepth, &duckRelease });
    }
}

std::vector<suite::ui::Action> KybalionEditor::actions()
{
    std::vector<suite::ui::Action> a;
    auto flipToggle = [] (juce::Button& b) { b.setToggleState (! b.getToggleState(), juce::sendNotification); };
    a.push_back ({ gateOn.getToggleState() ? "Turn the rhythm gate off" : "Turn the rhythm gate on", "the trance gate", "Rhythm", [this, flipToggle] { flipToggle (gateOn); } });
    a.push_back ({ duckOn.getToggleState() ? "Turn the ducking off" : "Turn the ducking on", "polarity: sidechain-style ducking", "Polarity", [this, flipToggle] { flipToggle (duckOn); } });
    a.push_back ({ "Randomise the gate", "a new gate pattern", "Rhythm", [this] { tabs.setSelected (0); randomButton.triggerClick(); } });
    for (size_t i = 0; i < Params::Gate::patterns().size(); ++i)
        a.push_back ({ "Gate pattern: " + juce::String (Params::Gate::patterns()[i].name), "", "Rhythm",
                       [this, i] { tabs.setSelected (0); patternBox.setSelectedId ((int) i + 1); } });
    a.push_back ({ "Gate from the song's chords", "Correspondence's pattern library", "Rhythm", [this] { patternBox.setSelectedId (990); } });
    a.push_back ({ "Save this gate pattern", "", "Rhythm", [this] { patternBox.setSelectedId (999); } });
    a.push_back ({ "Fit to kick", "hold and release from the kick on the sidechain", "Polarity", [this] { tabs.setSelected (1); fitButton.triggerClick(); }, fitButton.isEnabled() });
    a.push_back ({ shareKick.getToggleState() ? "Stop sharing the kick" : "Share this track's kick", "every Kybalion on 'Suite kick' ducks to it", "Polarity",
                   [this, flipToggle] { flipToggle (shareKick); } });
    a.push_back ({ flip.getToggleState() ? "Flip polarity back" : "Flip polarity", "when the low end cancels with the kick", "Polarity", [this, flipToggle] { flipToggle (flip); } });
    for (size_t i = 0; i < Params::Duck::presets().size(); ++i)
        a.push_back ({ "Ducking preset: " + juce::String (Params::Duck::presets()[i].name), "", "Polarity",
                       [this, i] { tabs.setSelected (1); presetBox.setSelectedId ((int) i + 1); } });
    a.push_back ({ "Show the rhythm", "", "View", [this] { tabs.setSelected (0); } });
    a.push_back ({ "Show the ducking", "", "View", [this] { tabs.setSelected (1); } });
    a.push_back ({ "Show the section scenes", "", "View", [this] { tabs.setSelected (2); } });
    return a;
}
