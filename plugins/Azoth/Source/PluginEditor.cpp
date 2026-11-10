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
      swellLen (p.apvts, AzothProcessor::swellLenId, "Swell length"),
      rollStart (p.apvts, AzothProcessor::rollStartId, "Roll starts"),
      gap (p.apvts, AzothProcessor::gapId, "Gap before drop"),
      follow (p.apvts, AzothProcessor::followId, "Follow Ouroboros", Colours::ember),
      grooveCrash (p.apvts, AzothProcessor::grooveCrashId, "Crash into groove", accent)
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
    for (auto* c : std::initializer_list<juce::Component*> { &impact, &crash, &swell, &down, &roll, &level, &tone, &rollPitch,
                                                             &swellLen, &rollStart, &gap, &follow, &grooveCrash, &status, &next })
        addAndMakeVisible (c);
    setSize (980, 560);
    timerCallback();
    startTimerHz (10);
}

AzothEditor::~AzothEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
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
    shapeArea = bounds;

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
    layoutRow (s, { &level, &tone, &swellLen, &rollStart, &rollPitch, &gap });
}
