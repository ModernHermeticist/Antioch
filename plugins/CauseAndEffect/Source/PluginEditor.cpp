#include "PluginEditor.h"
#include "Music.h"

using namespace suite::ui;

//==============================================================================
void BuildMeter::update()
{
    level = processor.buildLevel.load();
    progress = processor.progress.load();
    lengthBars = processor.lengthBars.load();

    const auto drops = processor.dropCount.load();
    if (drops != lastDrops)
    {
        lastDrops = drops;
        dropFlashUntil = juce::Time::getMillisecondCounterHiRes() + 900.0;
    }
    repaint();
}

void BuildMeter::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    auto strip = bounds.removeFromBottom (22.0f);
    const auto size = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto arcBounds = bounds.withSizeKeepingCentre (size, size).reduced (10.0f);
    const auto centre = arcBounds.getCentre();
    const auto radius = arcBounds.getWidth() * 0.5f;
    const auto start = juce::MathConstants<float>::pi * -0.75f, end = juce::MathConstants<float>::pi * 0.75f;
    const auto stroke = juce::PathStrokeType (14.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, start, end, true);
    g.setColour (Colours::outline);
    g.strokePath (track, stroke);

    const auto flashing = juce::Time::getMillisecondCounterHiRes() < dropFlashUntil;
    if (level > 0.001f)
    {
        juce::Path fill;
        fill.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, start, start + (end - start) * level, true);
        g.setGradientFill (juce::ColourGradient (Colours::brass, arcBounds.getBottomLeft(), Colours::ember, arcBounds.getTopRight(), false));
        g.strokePath (fill, stroke);
    }

    // Centre read-out
    g.setColour (flashing ? Colours::ember : Colours::text);
    g.setFont (juce::FontOptions (flashing ? 40.0f : 44.0f, juce::Font::bold));
    g.drawText (flashing ? "DROP!" : juce::String (juce::roundToInt (level * 100.0f)) + "%",
                arcBounds.withSizeKeepingCentre (arcBounds.getWidth(), 50.0f).translated (0.0f, -6.0f),
                juce::Justification::centred);

    juce::String status = processor.ouroborosFound.load() ? "READY  (following Ouroboros)" : "READY";
    if (progress >= 0.0)
        status = juce::String (processor.sectionBuild.load() ? "OUROBOROS BUILD  bar " : "BUILDING  bar ")
                 + juce::String (juce::jmin ((int) lengthBars, (int) (progress * lengthBars) + 1))
                 + " / " + juce::String ((int) lengthBars);
    else if (level > 0.001f)
        status = "MANUAL BUILD";
    g.setColour (Colours::dimText);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (status, arcBounds.withSizeKeepingCentre (arcBounds.getWidth(), 20.0f).translated (0.0f, 32.0f),
                juce::Justification::centred);

    // Bar strip: one segment per bar of the timed build
    const auto bars = juce::jlimit (1, 32, (int) lengthBars);
    const auto segW = strip.getWidth() / (float) bars;
    for (int i = 0; i < bars; ++i)
    {
        const auto seg = juce::Rectangle<float> (strip.getX() + (float) i * segW, strip.getY() + 6.0f, segW, 10.0f).reduced (1.5f, 0.0f);
        const auto filled = progress >= 0.0 && (double) i < progress * bars;
        g.setColour (filled ? Colours::brass.interpolatedWith (Colours::ember, (float) i / (float) bars) : Colours::outline);
        g.fillRoundedRectangle (seg, 2.0f);
    }
}

//==============================================================================
CauseEffectEditor::CauseEffectEditor (CauseEffectProcessor& p)
    : AudioProcessorEditor (p), processor (p), meter (p),
      buildKnob (p.apvts, Params::build, "Build (automate me)", Colours::ember),
      curve (p.apvts, Params::curve, "Curve", Colours::brass),
      trigger (p.apvts, Params::trigger, "TRIGGER BUILD", Colours::ember),
      dropCut (p.apvts, Params::dropCut, "Cut tails at drop", Colours::brass),
      followSections (p.apvts, Params::follow, "Follow Ouroboros", Colours::ember),
      length (p.apvts, Params::length, "Timed length"),
      filterMax (p.apvts, Params::filterMax, "Sweep to", Colours::verdigris),
      filterRes (p.apvts, Params::filterRes, "Resonance", Colours::verdigris),
      wash (p.apvts, Params::wash, "Wash", Colours::amethyst),
      washDecay (p.apvts, Params::washDecay, "Decay", Colours::amethyst),
      noise (p.apvts, Params::noise, "Noise", Colours::brass),
      riser (p.apvts, Params::riser, "Tonal", Colours::brass),
      stutter (p.apvts, Params::stutter, "Stutter", Colours::ember),
      widen (p.apvts, Params::widen, "Widen", Colours::moss),
      output (p.apvts, Params::output, "Output", Colours::text),
      riserRange (p.apvts, Params::riserRange, "Range"),
      stutterMax (p.apvts, Params::stutterMax, "Fastest"),
      rootChoice (p.apvts, Params::rootNote, "Key"),
      followKey (p.apvts, Params::followKey, "Follow Key", Colours::verdigris)
{
    setLookAndFeel (&lnf);

    for (auto* c : std::initializer_list<juce::Component*> { &meter, &buildKnob, &curve, &trigger, &dropCut, &followSections, &length,
                                                             &filterMax, &filterRes, &wash, &washDecay, &noise, &riser,
                                                             &riserRange, &stutter, &stutterMax, &widen, &rootChoice,
                                                             &followKey, &keyStatus, &output, &hint })
        addAndMakeVisible (c);

    keyStatus.setJustificationType (juce::Justification::centred);
    keyStatus.setFont (juce::FontOptions (12.0f));
    hint.setText ("Automate Build with one ramp, or switch Trigger on at the start of the build: "
                  "it ramps for the chosen length and snaps back on the drop.",
                  juce::dontSendNotification);
    hint.setFont (juce::FontOptions (12.0f));
    hint.setJustificationType (juce::Justification::topLeft);

    setSize (980, 640);
    timerCallback();
    startTimerHz (30);
}

CauseEffectEditor::~CauseEffectEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void CauseEffectEditor::timerCallback()
{
    meter.update();

    const auto following = processor.followingBus.load();
    rootChoice.box.setEnabled (! following);
    keyStatus.setColour (juce::Label::textColourId, following ? Colours::verdigris : Colours::dimText);
    const auto riserNote = suite::music::noteNames()[processor.effectiveRoot.load()] + "4";
    keyStatus.setText (following ? "Following " + suite::music::keyName (processor.effectiveRoot.load(), processor.busScale.load())
                                       + juce::String::fromUTF8 (" \xc2\xb7 riser lands on ") + riserNote
                                 : "Riser lands on " + riserNote,
                       juce::dontSendNotification);
}

void CauseEffectEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);
    drawHeader (g, { 20, 12, 700, 32 }, "CAUSE & EFFECT", "build-up macro", Colours::ember);

    drawPanel (g, buildArea, "Build", Colours::ember);
    drawPanel (g, filterArea, "High-pass sweep", Colours::verdigris);
    drawPanel (g, washArea, "Reverb wash", Colours::amethyst);
    drawPanel (g, riserArea, "Risers", Colours::brass);
    drawPanel (g, motionArea, "Stutter & width", Colours::ember);
    drawPanel (g, keyArea, "Key & output");
}

void CauseEffectEditor::resized()
{
    auto bounds = getLocalBounds().reduced (16);
    bounds.removeFromTop (48);

    buildArea = bounds.removeFromLeft (360);
    bounds.removeFromLeft (12);

    // ---- Build column ----
    auto b = buildArea.reduced (14, 8).withTrimmedTop (14);
    meter.setBounds (b.removeFromTop (230));
    b.removeFromTop (8);
    trigger.setBounds (b.removeFromTop (40).reduced (30, 2));
    b.removeFromTop (8);
    auto controls = b.removeFromTop (130);
    layoutRow (controls, { &buildKnob, &length, &curve });
    b.removeFromTop (6);
    auto toggles = b.removeFromTop (30);
    dropCut.setBounds (toggles.removeFromLeft (toggles.getWidth() / 2).reduced (4, 0));
    followSections.setBounds (toggles.reduced (4, 0));
    b.removeFromTop (8);
    hint.setBounds (b);

    // ---- Effect panels ----
    const auto rowH = (bounds.getHeight() - 24) / 3;
    auto row1 = bounds.removeFromTop (rowH);
    bounds.removeFromTop (12);
    auto row2 = bounds.removeFromTop (rowH);
    bounds.removeFromTop (12);
    keyArea = bounds;

    filterArea = row1.removeFromLeft (row1.getWidth() / 2 - 6);
    row1.removeFromLeft (12);
    washArea = row1;
    riserArea = row2.removeFromLeft (row2.getWidth() / 2 - 6);
    row2.removeFromLeft (12);
    motionArea = row2;

    auto inner = [] (juce::Rectangle<int> a) { return a.reduced (10, 6).withTrimmedTop (12); };
    layoutRow (inner (filterArea), { &filterMax, &filterRes });
    layoutRow (inner (washArea), { &wash, &washDecay });
    layoutRow (inner (riserArea), { &noise, &riser, &riserRange });
    layoutRow (inner (motionArea), { &stutter, &stutterMax, &widen });

    auto k = inner (keyArea);
    const auto cell = k.getWidth() / 4;
    rootChoice.setBounds (k.removeFromLeft (cell));
    followKey.setBounds (k.removeFromLeft (cell).withSizeKeepingCentre (cell - 30, 30).translated (0, 8));
    output.setBounds (k.removeFromRight (cell));
    keyStatus.setBounds (k);
}
