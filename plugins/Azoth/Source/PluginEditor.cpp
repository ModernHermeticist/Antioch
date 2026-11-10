#include "PluginEditor.h"

using namespace suite::ui;

namespace { const juce::Colour accent { 0xffd6c06a }; }

AzothEditor::AzothEditor (AzothProcessor& p)
    : AudioProcessorEditor (p), processor (p),
      impact (p.apvts, AzothProcessor::impactId, "Impact", sectionColour (suite::SectionType::Drop)),
      crash (p.apvts, AzothProcessor::crashId, "Crash", sectionColour (suite::SectionType::Drop)),
      swell (p.apvts, AzothProcessor::swellId, "Swell into drop", sectionColour (suite::SectionType::Build)),
      down (p.apvts, AzothProcessor::downId, "Downlifter", sectionColour (suite::SectionType::Breakdown)),
      roll (p.apvts, AzothProcessor::rollId, "Snare roll", sectionColour (suite::SectionType::Build)),
      level (p.apvts, AzothProcessor::levelId, "FX level", Colours::text),
      tone (p.apvts, AzothProcessor::toneId, "Tone", accent),
      rollPitch (p.apvts, AzothProcessor::rollPitchId, "Roll pitch rise", accent),
      laterDrops (p.apvts, AzothProcessor::laterDropsId, "Bigger later drops", suite::ui::sectionColour (suite::SectionType::Drop)),
      swellLen (p.apvts, AzothProcessor::swellLenId, "Swell length"),
      rollStart (p.apvts, AzothProcessor::rollStartId, "Roll & riser start"),
      gap (p.apvts, AzothProcessor::gapId, "Gap before drop"),
      follow (p.apvts, AzothProcessor::followId, "Follow Ouroboros", Colours::ember),
      grooveCrash (p.apvts, AzothProcessor::grooveCrashId, "Crash into groove", accent),
      reverseOn (p.apvts, AzothProcessor::reverseOnId, "Reverse swells", Colours::amethyst),
      reverseLevel (p.apvts, AzothProcessor::reverseLevelId, "Level", Colours::amethyst),
      riser (p.apvts, AzothProcessor::riserId, "Tonal riser", sectionColour (suite::SectionType::Build)),
      trackRiser (p.apvts, AzothProcessor::trackRiserId, "Track riser", sectionColour (suite::SectionType::Build)),
      riserRange (p.apvts, AzothProcessor::riserRangeId, "Riser range"),
      reverseTarget (p.apvts, AzothProcessor::reverseTargetId, "Swell into"),
      reverseLength (p.apvts, AzothProcessor::reverseLengthId, "Length"),
      dropFx (p.apvts, AzothProcessor::dropFxId, "Into the drop"),
      dropFxLen (p.apvts, AzothProcessor::dropFxLenId, "Length"),
      buildShape (p.apvts, AzothProcessor::buildShapeId, "Build shape"),
      buildFilter (p.apvts, AzothProcessor::buildFilterId, "Build filter", sectionColour (suite::SectionType::Build))
{
    setLookAndFeel (&lnf);
    const char* names[] = { "Impact", "Crash", "Downlifter", "Snare" };
    const int auditionFlags[] = { AzothProcessor::aImpact, AzothProcessor::aCrash, AzothProcessor::aDown, AzothProcessor::aSnare };
    for (int i = 0; i < 4; ++i)
    {
        auditions[(size_t) i].setButtonText (juce::String ("Play ") + names[i]);
        auditions[(size_t) i].onClick = [this, f = auditionFlags[i]] { processor.audition (f); };
        addAndMakeVisible (auditions[(size_t) i]);
    }
    status.setFont (juce::FontOptions (13.0f));
    next.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    for (auto* c : std::initializer_list<juce::Component*> { &impact, &crash, &swell, &down, &roll, &level, &tone, &rollPitch, &laterDrops,
                                                             &swellLen, &rollStart, &gap, &follow, &grooveCrash, &status, &next,
                                                             &reverseOn, &reverseLevel, &reverseTarget, &reverseLength, &reverseNote,
                                                             &riser, &trackRiser, &riserRange, &riserNote,
                                                             &dropFx, &dropFxLen, &buildShape, &buildFilter, &samplesButton, &samplesNote })
        addAndMakeVisible (c);
    reverseNote.setFont (juce::FontOptions (12.0f));
    reverseNote.setJustificationType (juce::Justification::centredLeft);
    reverseNote.setText ("Put Azoth on the vocal or lead track: it captures the first beat of each target section from "
                         "this track's own audio and swells its reversed reverb into it. Adds 3 s of latency (FL compensates).",
                         juce::dontSendNotification);
    riserNote.setFont (juce::FontOptions (12.0f));
    riserNote.setJustificationType (juce::Justification::centredLeft);
    riserNote.setText ("Across each Build: a tonal riser on the song's root and fifth sweeping up, and the track riser, "
                       "this track's own audio pitched up to an octave and faded in. Both stop for the gap before the drop.",
                       juce::dontSendNotification);
    samplesNote.setFont (juce::FontOptions (12.0f));
    samplesNote.setJustificationType (juce::Justification::centredLeft);
    samplesButton.onClick = [this] { showSamplesMenu(); };
    updateSamplesNote();
    setSize (980, 980);
    timerCallback();
    startTimerHz (10);
}

AzothEditor::~AzothEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void AzothEditor::updateSamplesNote()
{
    const char* names[] = { "Impact", "Crash", "Downlifter" };
    juce::StringArray mine;
    for (int i = 0; i < AzothProcessor::numSampleSlots; ++i)
        if (processor.sampleName (i).isNotEmpty()) mine.add (juce::String (names[i]) + ": " + processor.sampleName (i));
    samplesNote.setText ((mine.isEmpty() ? juce::String ("Synthesised impact, crash and downlifter (in key).") : mine.joinIntoString (", "))
                         + "\nTape stop / stutter replay this track's own audio before each drop; the build filter thins it out across the build.",
                         juce::dontSendNotification);
}

void AzothEditor::showSamplesMenu()
{
    const char* names[] = { "impact", "crash", "downlifter" };
    juce::PopupMenu m;
    for (int i = 0; i < AzothProcessor::numSampleSlots; ++i)
        m.addItem (i + 1, juce::String ("Use my own ") + names[i] + " sample...");
    m.addSeparator();
    for (int i = 0; i < AzothProcessor::numSampleSlots; ++i)
        m.addItem (100 + i, juce::String ("Back to the synthesised ") + names[i], processor.sampleName (i).isNotEmpty());
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&samplesButton), [this] (int id)
    {
        if (id >= 100) { processor.clearSample (id - 100); updateSamplesNote(); return; }
        if (id <= 0) return;
        const auto slot = id - 1;
        chooser = std::make_unique<juce::FileChooser> ("Choose a sample", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, slot] (const juce::FileChooser& fc)
        {
            if (fc.getResult().existsAsFile()) processor.loadSample (slot, fc.getResult());
            updateSamplesNote();
        });
    });
}

void AzothEditor::timerCallback()
{
    const auto type = processor.nextType.load();
    const auto bars = processor.barsToNext.load();
    juce::String what;
    if (type == (int) suite::SectionType::Drop)           what = "Impact + crash (Drop)";
    else if (type == (int) suite::SectionType::Breakdown) what = "Downlifter (Breakdown)";
    else if (type == (int) suite::SectionType::Groove)    what = "Crash (Groove)";
    else if (type >= 0)                                    what = juce::String (suite::sectionName ((suite::SectionType) type)) + " (no FX)";
    next.setColour (juce::Label::textColourId, type >= 0 ? sectionColour ((suite::SectionType) type) : Colours::dimText);
    next.setText (type >= 0 ? "Next: " + what + " in " + juce::String (bars, 1) + " bars" : juce::String ("Waiting for playback"), juce::dontSendNotification);
    status.setText (processor.mapAvailable.load() ? "Following the Ouroboros map. FX fired: " + juce::String (processor.eventsFired.load())
                                                  : "No Ouroboros map: load Ouroboros to place transitions automatically (audition buttons still work).",
                    juce::dontSendNotification);
}

void AzothEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);
    drawHeader (g, { 20, 12, 700, 32 }, "AZOTH", "automatic transition FX", accent);
    drawPanel (g, elementArea, "Transitions");
    drawPanel (g, shapeArea, "Shape", accent);
    drawPanel (g, riserArea, "Build risers", sectionColour (suite::SectionType::Build));
    drawPanel (g, reverseArea, "Reverse swells from this track", Colours::amethyst);
    drawPanel (g, entranceArea, "Into the drop, build shape and your samples", sectionColour (suite::SectionType::Drop));
}

void AzothEditor::resized()
{
    auto bounds = getLocalBounds().reduced (16);
    auto header = bounds.removeFromTop (48);
    follow.setBounds (header.removeFromRight (180).reduced (4, 8));
    auto info = bounds.removeFromTop (54);
    next.setBounds (info.removeFromTop (30));
    status.setBounds (info);
    bounds.removeFromTop (8);
    elementArea = bounds.removeFromTop (220);
    bounds.removeFromTop (12);
    shapeArea = bounds.removeFromTop (150);
    bounds.removeFromTop (12);
    riserArea = bounds.removeFromTop (130);
    bounds.removeFromTop (12);
    entranceArea = bounds.removeFromTop (130);
    bounds.removeFromTop (12);
    reverseArea = bounds;

    auto en = entranceArea.reduced (10, 6).withTrimmedTop (14);
    dropFx.setBounds (en.removeFromLeft (150));
    dropFxLen.setBounds (en.removeFromLeft (130));
    buildShape.setBounds (en.removeFromLeft (150));
    buildFilter.setBounds (en.removeFromLeft (110));
    samplesButton.setBounds (en.removeFromLeft (150).withSizeKeepingCentre (140, 30));
    samplesNote.setBounds (en.reduced (8, 0));

    auto ri = riserArea.reduced (10, 6).withTrimmedTop (14);
    riser.setBounds (ri.removeFromLeft (120));
    riserRange.setBounds (ri.removeFromLeft (160));
    trackRiser.setBounds (ri.removeFromLeft (120));
    riserNote.setBounds (ri.reduced (8, 0));

    auto rv = reverseArea.reduced (10, 6).withTrimmedTop (14);
    reverseOn.setBounds (rv.removeFromLeft (170).withSizeKeepingCentre (160, 30));
    reverseLevel.setBounds (rv.removeFromLeft (120));
    reverseTarget.setBounds (rv.removeFromLeft (170));
    reverseLength.setBounds (rv.removeFromLeft (150));
    reverseNote.setBounds (rv.reduced (8, 0));

    auto e = elementArea.reduced (10, 6).withTrimmedTop (14);
    auto buttons = e.removeFromBottom (34);
    layoutRow (e, { &impact, &crash, &swell, &down, &roll });
    const auto w = buttons.getWidth() / 5;
    auditions[0].setBounds (buttons.withX (buttons.getX()).withWidth (w).reduced (8, 2));
    auditions[1].setBounds (buttons.withX (buttons.getX() + w).withWidth (w).reduced (8, 2));
    auditions[2].setBounds (buttons.withX (buttons.getX() + 3 * w).withWidth (w).reduced (8, 2));
    auditions[3].setBounds (buttons.withX (buttons.getX() + 4 * w).withWidth (w).reduced (8, 2));

    auto s = shapeArea.reduced (10, 6).withTrimmedTop (14);
    auto toggleCell = s.removeFromRight (170);
    grooveCrash.setBounds (toggleCell.withSizeKeepingCentre (160, 30));
    layoutRow (s, { &level, &tone, &swellLen, &rollStart, &rollPitch, &gap, &laterDrops });
}
