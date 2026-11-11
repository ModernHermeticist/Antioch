#include "PluginEditor.h"

using namespace suite::ui;

namespace { const juce::Colour accent = Colours::verdigris.interpolatedWith (Colours::brass, 0.35f); }

LapisEditor::LapisEditor (LapisProcessor& p)
    : AudioProcessorEditor (p), processor (p),
      target (p.apvts, LapisProcessor::targetId, "Target"),
      mode (p.apvts, LapisProcessor::modeId, "Gain"),
      ceiling (p.apvts, LapisProcessor::ceilingId, "Ceiling", accent),
      clip (p.apvts, LapisProcessor::clipId, "Clip", Colours::ember),
      release (p.apvts, LapisProcessor::releaseId, "Release", accent),
      manualGain (p.apvts, LapisProcessor::manualGainId, "Manual gain", Colours::brass),
      toneLow (p.apvts, LapisProcessor::toneLowId, "Low tone", accent),
      toneHigh (p.apvts, LapisProcessor::toneHighId, "High tone", accent)
{
    setLookAndFeel (&lnf);
    resetButton.onClick = [this] { processor.resetLearning(); };
    fingerprintButton.setTooltip ("Save this master chain and the finished master's tonal shape, or match a new song to one you saved");
    fingerprintButton.onClick = [this] { showFingerprintMenu(); };
    status.setFont (juce::FontOptions (13.0f));
    status.setJustificationType (juce::Justification::centredLeft);
    for (auto* c : std::initializer_list<juce::Component*> { &target, &mode, &ceiling, &clip, &release, &manualGain, &toneLow, &toneHigh, &resetButton, &fingerprintButton, &status })
        addAndMakeVisible (c);
    setSize (1000, 520);
    startTimerHz (20);
}

LapisEditor::~LapisEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void LapisEditor::timerCallback()
{
    const auto m = (int) processor.apvts.getRawParameterValue (LapisProcessor::modeId)->load();
    manualGain.setEnabled (m == LapisProcessor::Manual);
    ceiling.setEnabled (! processor.followingMentalism.load());

    juce::String text;
    if ((int) processor.apvts.getRawParameterValue (LapisProcessor::targetId)->load() == LapisProcessor::followMentalism)
        text = processor.followingMentalism.load() ? "Following Mentalism's target. " : "Mentalism not found: using the club target. ";
    if (m == LapisProcessor::Auto)
        text << (processor.limitedByReduction.load() ? "Holding back: already limiting over 8 dB. Turn the mix up before Lapis or choose a quieter target."
                 : processor.settled.load() ? "Settled: loud sections are on target. Switch to Hold to lock the gain."
                                            : "Learning: play the loudest part of the song (the drop).");
    else if (m == LapisProcessor::Hold)
        text << "Holding the learned gain.";
    else
        text << "Manual gain.";
    status.setColour (juce::Label::textColourId, processor.limitedByReduction.load() ? Colours::ember : Colours::text);
    status.setText (juce::Time::getMillisecondCounterHiRes() < noteUntil ? note : text, juce::dontSendNotification);

    grShown = std::max (processor.gainReductionDb.load(), grShown * 0.85f);
    repaint (meterArea);
}

void LapisEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);
    drawHeader (g, { 20, 12, 700, 32 }, "LAPIS", "master finisher", accent);
    drawPanel (g, meterArea, "Output");
    drawPanel (g, controlArea, "Controls", accent);

    auto r = meterArea.reduced (16, 10).withTrimmedTop (16);
    auto numbers = r.removeFromTop (90);
    const auto st = processor.outShortTerm.load();
    const auto tp = processor.outputPeak.peakDb();
    const std::tuple<juce::String, juce::String, juce::String, juce::Colour> items[] =
    {
        { "Short-term", st <= -70.0f ? juce::String::fromUTF8 ("\xe2\x80\x93") : juce::String (st, 1), "LUFS", Colours::text },
        { "Loud-section target", juce::String (processor.effectiveTarget.load() + LapisProcessor::loudSectionOffset, 1), "LUFS", accent },
        { "Gain", (processor.gainDb.load() > 0.05f ? "+" : "") + juce::String (processor.gainDb.load(), 1), "dB", Colours::brass },
        { "True peak", tp <= -99.0f ? juce::String::fromUTF8 ("\xe2\x80\x93") : juce::String (tp, 1),
          "dBTP (ceiling " + juce::String (processor.effectiveCeiling.load(), 1) + ")", tp > processor.effectiveCeiling.load() + 0.1f ? Colours::ember : Colours::text },
    };
    const auto w = numbers.getWidth() / 4;
    for (auto& [label, value, unit, colour] : items)
    {
        auto cell = numbers.removeFromLeft (w);
        g.setColour (Colours::dimText);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (label, cell.removeFromTop (16), juce::Justification::centred);
        g.setColour (colour);
        g.setFont (juce::FontOptions (32.0f, juce::Font::bold));
        g.drawText (value, cell.removeFromTop (40), juce::Justification::centred);
        g.setColour (Colours::dimText);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (unit, cell, juce::Justification::centredTop);
    }

    // Gain reduction meter (0..12 dB), with the max held.
    r.removeFromTop (8);
    auto meterRow = r.removeFromTop (40);
    g.setColour (Colours::dimText);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("Limiting", meterRow.removeFromLeft (80), juce::Justification::centredLeft);
    auto bar = meterRow.removeFromLeft (meterRow.getWidth() - 110).toFloat().reduced (0.0f, 12.0f);
    g.setColour (Colours::outline);
    g.fillRoundedRectangle (bar, 3.0f);
    auto widthFor = [&] (float db) { return bar.getWidth() * juce::jlimit (0.0f, 1.0f, db / 12.0f); };
    g.setColour (grShown > 8.0f ? Colours::ember : accent);
    g.fillRoundedRectangle (bar.withWidth (widthFor (grShown)), 3.0f);
    const auto maxGr = processor.maxReductionDb.load();
    g.setColour (Colours::text);
    g.drawVerticalLine ((int) (bar.getX() + widthFor (maxGr)), bar.getY() - 3.0f, bar.getBottom() + 3.0f);
    g.drawText (juce::String (grShown, 1) + " dB  (max " + juce::String (maxGr, 1) + ")", meterRow, juce::Justification::centredRight);
}

void LapisEditor::showFingerprintMenu()
{
    const auto saved = LapisProcessor::loadFingerprints (LapisProcessor::fingerprintFolder());
    juce::PopupMenu m;
    m.addItem (1, processor.fingerprintReady() ? "Save this master's fingerprint..." : "Save this master's fingerprint (play 10 s first)",
               processor.fingerprintReady());
    if (! saved.empty()) m.addSeparator();
    for (size_t i = 0; i < saved.size(); ++i) m.addItem (100 + (int) i, "Match: " + saved[i].name);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&fingerprintButton), [this, saved] (int id)
    {
        if (id >= 100 && id - 100 < (int) saved.size())
            showNote (processor.matchFingerprint (saved[(size_t) (id - 100)]));
        else if (id == 1)
        {
            nameWindow = std::make_unique<juce::AlertWindow> ("Master fingerprint", "Name this master sound:", juce::MessageBoxIconType::NoIcon);
            nameWindow->addTextEditor ("name", "My master");
            nameWindow->addButton ("Save", 1);
            nameWindow->addButton ("Cancel", 0);
            nameWindow->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int result)
            {
                if (result == 1 && nameWindow != nullptr)
                    showNote (processor.saveFingerprint (LapisProcessor::fingerprintFolder(), nameWindow->getTextEditorContents ("name"))
                                  ? juce::String ("Saved the master fingerprint") : juce::String ("Couldn't save it"));
            }));
        }
    });
}

void LapisEditor::resized()
{
    auto bounds = getLocalBounds().reduced (16);
    bounds.removeFromTop (48);
    meterArea = bounds.removeFromTop (200);
    bounds.removeFromTop (12);
    controlArea = bounds;

    auto c = controlArea.reduced (12, 8).withTrimmedTop (14);
    auto statusRow = c.removeFromBottom (30);
    resetButton.setBounds (statusRow.removeFromRight (140).reduced (2));
    fingerprintButton.setBounds (statusRow.removeFromRight (170).reduced (2));
    status.setBounds (statusRow);
    layoutRow (c, { &target, &mode, &ceiling, &clip, &release, &manualGain, &toneLow, &toneHigh });
}
