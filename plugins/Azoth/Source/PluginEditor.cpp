#include "PluginEditor.h"

using namespace suite::ui;

namespace { const juce::Colour accent { 0xffd6c06a }; }

AzothEditor::AzothEditor (AzothProcessor& p)
    : AudioProcessorEditor (p), processor (p),
      header ("AZOTH", "automatic transition FX", accent),
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
      buildFilter (p.apvts, AzothProcessor::buildFilterId, "Build filter", sectionColour (suite::SectionType::Build)),
      preLift (p.apvts, AzothProcessor::preLiftId, "Key-change pre-lift", sectionColour (suite::SectionType::Drop)),
      riserChords (p.apvts, AzothProcessor::riserChordsId, "Riser follows chords", sectionColour (suite::SectionType::Build)),
      rollPitchSpeed (p.apvts, AzothProcessor::rollPitchSpeedId, "Roll pitch with speed", sectionColour (suite::SectionType::Build)),
      impactTune (p.apvts, AzothProcessor::impactTuneId, "Impact on the kick", suite::ui::sectionColour (suite::SectionType::Drop)),
      rollFrom (p.apvts, AzothProcessor::rollFromId, "Roll from"),
      rollTo (p.apvts, AzothProcessor::rollToId, "Roll to"),
      rollCurve (p.apvts, AzothProcessor::rollCurveId, "Speed-up"),
      rollVel (p.apvts, AzothProcessor::rollVelId, "Roll start vel.", sectionColour (suite::SectionType::Build))
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (header);
    header.addRight (follow, 170);
    header.addRight (samplesButton, 190);
    header.actions = [this] { return actions(); };
    header.enableAdvanced (p.apvts.state, [this] (bool on) { setSize (getWidth(), heightFor (on)); resized(); repaint(); });
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
                                                             &swellLen, &rollStart, &gap, &grooveCrash, &status, &next,
                                                             &reverseOn, &reverseLevel, &reverseTarget, &reverseLength, &reverseNote,
                                                             &riser, &trackRiser, &riserRange, &riserNote,
                                                             &dropFx, &dropFxLen, &buildShape, &buildFilter, &samplesNote, &preLift,
                                                             &riserChords, &rollPitchSpeed, &impactTune, &rollFrom, &rollTo, &rollCurve, &rollVel })
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
    setSize (980, heightFor (header.advanced()));
    directorStrip.director = [] { return suite::DirectorBus::cue (suite::director::pAzoth); };
    addAndMakeVisible (directorStrip);
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
    const char* names[] = { "Impact", "Crash", "Downlifter", "Tag" };
    juce::StringArray mine;
    for (int i = 0; i < AzothProcessor::numSampleSlots; ++i)
        if (processor.sampleName (i).isNotEmpty()) mine.add (juce::String (names[i]) + ": " + processor.sampleName (i));
    samplesNote.setText ((mine.isEmpty() ? juce::String ("Synthesised impact, crash and downlifter (in key).") : mine.joinIntoString (", "))
                         + "\nTape stop / stutter replay this track's own audio before each drop; the build filter thins it out across the build.",
                         juce::dontSendNotification);
}

void AzothEditor::showSamplesMenu()
{
    const char* names[] = { "impact", "crash", "downlifter", "producer tag" };
    juce::PopupMenu m;
    for (int i = 0; i < AzothProcessor::numSampleSlots; ++i)
        m.addItem (i + 1, juce::String ("Use my own ") + names[i] + " sample...");
    m.addSeparator();
    for (int i = 0; i < AzothProcessor::numSampleSlots; ++i)
        m.addItem (100 + i, i == AzothProcessor::slotTag ? juce::String ("Remove the producer tag") : juce::String ("Back to the synthesised ") + names[i],
                   processor.sampleName (i).isNotEmpty());
    m.addSeparator();
    const auto tune = processor.apvts.getRawParameterValue (AzothProcessor::tuneSamplesId)->load() > 0.5f;
    m.addItem (200, "Tune my samples to the song's key (and match their level)", true, tune);
    const auto where = (int) processor.apvts.getRawParameterValue (AzothProcessor::tagWhereId)->load();
    const char* places[] = { "Tag: off", "Tag: on the first drop", "Tag: on every drop" };
    for (int k = 0; k < 3; ++k) m.addItem (201 + k, places[k], true, where == k);
    m.addSectionHeader ("Your sound design presets");
    const auto presets = AzothProcessor::presetFiles();
    for (int k = 0; k < presets.size(); ++k) m.addItem (300 + k, "Load: " + presets[k].getFileNameWithoutExtension());
    m.addItem (299, "Save everything here as a preset...");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&samplesButton), [this, tune, presets] (int id)
    {
        if (id >= 300 && id - 300 < presets.size()) { processor.loadPreset (presets[id - 300]); updateSamplesNote(); return; }
        if (id == 299)
        {
            nameWindow = std::make_unique<juce::AlertWindow> ("Sound design preset", "Name this preset (all settings and your samples):", juce::MessageBoxIconType::NoIcon);
            nameWindow->addTextEditor ("name", "My transitions");
            nameWindow->addButton ("Save", 1);
            nameWindow->addButton ("Cancel", 0);
            nameWindow->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int result)
            {
                if (result == 1 && nameWindow != nullptr) processor.savePreset (nameWindow->getTextEditorContents ("name"));
            }));
            return;
        }
        auto setParam = [this] (const juce::String& pid, float plain)
        {
            if (auto* p = processor.apvts.getParameter (pid))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->convertTo0to1 (plain));
                p->endChangeGesture();
            }
        };
        if (id == 200) { setParam (AzothProcessor::tuneSamplesId, tune ? 0.0f : 1.0f); return; }
        if (id >= 201 && id <= 203) { setParam (AzothProcessor::tagWhereId, (float) (id - 201)); return; }
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
    header.setStatus (type >= 0 ? "Next: " + what + " in " + juce::String (bars, 1) + " bars" : juce::String (processor.mapAvailable.load() ? "Waiting for playback" : "No Ouroboros map"),
                      type >= 0 ? sectionColour ((suite::SectionType) type) : Colours::faintText);
    status.setText (processor.mapAvailable.load() ? "Following the Ouroboros map. FX fired: " + juce::String (processor.eventsFired.load())
                                                  : "No Ouroboros map: load Ouroboros to place transitions automatically (audition buttons still work).",
                    juce::dontSendNotification);
}

void AzothEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);
    drawPanel (g, elementArea, "Transitions");
    drawPanel (g, shapeArea, "Shape", accent);
    drawPanel (g, riserArea, header.advanced() ? "Build: risers and snare roll" : "Build: risers", sectionColour (suite::SectionType::Build));
    drawPanel (g, reverseArea, "Reverse swells from this track", Colours::amethyst);
    if (header.advanced())
        drawPanel (g, entranceArea, "Into the drop, build shape and your samples", sectionColour (suite::SectionType::Drop));
}

void AzothEditor::resized()
{
    directorStrip.setBounds (getLocalBounds().removeFromBottom (suite::ui::AutopilotStrip::height + 8).reduced (16, 4));
    auto bounds = getLocalBounds().withTrimmedBottom (suite::ui::AutopilotStrip::height + 4).reduced (16);
    header.setBounds (bounds.removeFromTop (HeaderBar::height).withTrimmedTop (-8));
    bounds.removeFromTop (8);
    const auto advanced = header.advanced();
    setShown ({ &swellLen, &rollStart, &rollPitch, &gap, &dropFx, &dropFxLen, &buildShape, &buildFilter, &preLift, &impactTune, &samplesNote,
                &rollPitchSpeed, &rollFrom, &rollTo, &rollCurve, &rollVel, &reverseTarget, &reverseLength, &riserRange }, advanced);
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
    entranceArea = advanced ? bounds.removeFromTop (130) : juce::Rectangle<int>();
    if (advanced) bounds.removeFromTop (12);
    reverseArea = bounds;

    auto en = entranceArea.reduced (10, 6).withTrimmedTop (14);
    dropFx.setBounds (en.removeFromLeft (150));
    dropFxLen.setBounds (en.removeFromLeft (130));
    buildShape.setBounds (en.removeFromLeft (150));
    buildFilter.setBounds (en.removeFromLeft (110));
    {
        auto col = en.removeFromLeft (170);
        impactTune.setBounds (col.removeFromTop (col.getHeight() / 2).withSizeKeepingCentre (160, 30));
        preLift.setBounds (col.withSizeKeepingCentre (160, 30));
    }
    samplesNote.setBounds (en.reduced (8, 0));

    auto ri = riserArea.reduced (10, 6).withTrimmedTop (14);
    riser.setBounds (ri.removeFromLeft (120));
    if (advanced) riserRange.setBounds (ri.removeFromLeft (160));
    trackRiser.setBounds (ri.removeFromLeft (120));
    {
        auto col = ri.removeFromLeft (170);
        riserChords.setBounds (col.removeFromTop (col.getHeight() / 2).withSizeKeepingCentre (160, 30));
        rollPitchSpeed.setBounds (col.withSizeKeepingCentre (160, 30));
    }
    rollFrom.setBounds (ri.removeFromLeft (90));
    rollTo.setBounds (ri.removeFromLeft (90));
    rollCurve.setBounds (ri.removeFromLeft (110));
    rollVel.setBounds (ri.removeFromLeft (100));
    riserNote.setVisible (! advanced);
    if (! advanced) riserNote.setBounds (ri.reduced (12, 0));

    auto rv = reverseArea.reduced (10, 6).withTrimmedTop (14);
    reverseOn.setBounds (rv.removeFromLeft (170).withSizeKeepingCentre (160, 30));
    reverseLevel.setBounds (rv.removeFromLeft (120));
    if (advanced)
    {
        reverseTarget.setBounds (rv.removeFromLeft (170));
        reverseLength.setBounds (rv.removeFromLeft (150));
    }
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
    if (advanced) layoutRow (s, { &level, &tone, &swellLen, &rollStart, &rollPitch, &gap, &laterDrops });
    else          layoutRow (s.removeFromLeft (s.getWidth() * 3 / 7), { &level, &tone, &laterDrops });
}

std::vector<suite::ui::Action> AzothEditor::actions()
{
    std::vector<suite::ui::Action> a;
    const char* names[] = { "impact", "crash", "downlifter", "snare" };
    for (int i = 0; i < 4; ++i)
        a.push_back ({ juce::String ("Play the ") + names[i], "hear it now", "Audition", [this, i] { auditions[(size_t) i].triggerClick(); } });
    a.push_back ({ "Samples and presets", "your own impact, crash, downlifter and tag; sound design presets", "Samples", [this] { showSamplesMenu(); } });
    a.push_back ({ reverseOn.getToggleState() ? "Turn reverse swells off" : "Turn reverse swells on", "this track's own audio, reversed into the next section", "Swells",
                   [this] { reverseOn.setToggleState (! reverseOn.getToggleState(), juce::sendNotification); } });
    a.push_back ({ follow.getToggleState() ? "Stop following Ouroboros" : "Follow Ouroboros", "place the transitions from the song map", "Map",
                   [this] { follow.setToggleState (! follow.getToggleState(), juce::sendNotification); } });
    return a;
}
