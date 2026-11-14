#include "EmeraldTabletEditor.h"
#include "Music.h"

using namespace suite::ui;

namespace
{
    const juce::Colour emerald { 0xff4fc38a };
    juce::String keyLabel (int k) { return suite::music::noteNames()[k % 12] + (k >= 12 ? " minor" : " major"); }
}

EmeraldTabletEditor::EmeraldTabletEditor (EmeraldTabletProcessor& p)
    : AudioProcessorEditor (p), processor (p),
      header ("EMERALD TABLET", "key detector", emerald),
      autoSend (p.apvts, EmeraldTabletProcessor::autoSendId, "Auto-send to Correspondence", emerald),
      broadcastAlone (p.apvts, EmeraldTabletProcessor::broadcastAloneId, "Broadcast if no Correspondence", emerald)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (header);
    makePrimary (sendButton, emerald);
    sendButton.setTooltip ("Send the detected key to Correspondence now");
    resetButton.setTooltip ("Forget what was heard and listen again");
    header.actions = [this]
    {
        return std::vector<Action> {
            { "Send the key now", "to Correspondence (or the whole suite)", "Key", [this] { processor.sendNow(); } },
            { "Reset detection", "forget what was heard and listen again", "Key", [this] { processor.resetDetection(); } },
            { autoSend.getToggleState() ? "Stop auto-sending" : "Auto-send to Correspondence", "send once the result is confident and stable", "Key",
              [this] { autoSend.setToggleState (! autoSend.getToggleState(), juce::sendNotification); } } };
    };
    header.enableAdvanced (p.apvts.state, [this] (bool) { resized(); });
    sendButton.onClick = [this] { processor.sendNow(); };
    resetButton.onClick = [this] { processor.resetDetection(); };
    for (auto* c : std::initializer_list<juce::Component*> { &autoSend, &broadcastAlone, &sendButton, &resetButton })
        addAndMakeVisible (c);
    setSize (820, 480);
    startTimerHz (10);
}

EmeraldTabletEditor::~EmeraldTabletEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void EmeraldTabletEditor::timerCallback()
{
    suite::Watchdog::Scope watch (processor.getName(), "editor refresh");
    const auto best = processor.detector.best.load();
    const auto sent = processor.lastSent.load();
    const juce::String where = processor.fromMaster.load() ? " (the master)" : " (this track)";
    header.setStatus (best < 0 ? "Listening" + where : (sent >= 0 ? "Sent: " + keyLabel (sent) : "Heard: " + keyLabel (best)) + where,
                      best < 0 ? Colours::faintText : sent >= 0 ? Colours::moss : Colours::brass);
    repaint();
}

void EmeraldTabletEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);
    drawPanel (g, resultArea, "Detected key", emerald);
    drawPanel (g, chromaArea, "Pitch classes heard");

    auto r = resultArea.reduced (16, 10).withTrimmedTop (18);
    const auto& det = processor.detector;
    const auto seconds = det.seconds.load();
    const auto ranked = det.ranked();
    const auto best = det.best.load();

    g.setFont (juce::FontOptions (40.0f, juce::Font::bold));
    g.setColour (best >= 0 ? emerald : Colours::dimText);
    g.drawText (best >= 0 ? keyLabel (best) : juce::String ("listening..."), r.removeFromTop (50), juce::Justification::centredLeft);

    const auto conf = det.confidence.load();
    g.setFont (juce::FontOptions (13.0f));
    g.setColour (Colours::text);
    const auto line = "Confidence " + juce::String (juce::roundToInt (conf * 100.0f)) + "%" + juce::String::fromUTF8 ("  \xc2\xb7  ")
                      + juce::String ((int) seconds) + " s heard";
    g.drawText (line, r.removeFromTop (20), juce::Justification::centredLeft);

    if (best >= 0)
    {
        juce::String others = "Also possible: ";
        for (int i = 1; i <= 2; ++i)
            others << keyLabel (ranked[(size_t) i].root + (ranked[(size_t) i].minor ? 12 : 0)) << (i == 1 ? ", " : "");
        g.setColour (Colours::dimText);
        g.drawText (others, r.removeFromTop (20), juce::Justification::centredLeft);
    }

    const auto sent = processor.lastSent.load();
    g.setColour (sent >= 0 ? Colours::moss : Colours::dimText);
    g.drawText (sent >= 0 ? "Sent: " + keyLabel (sent) + (processor.broadcastingAlone.load() ? " (broadcasting to the suite)" : " (offered to Correspondence)")
                          : seconds < EmeraldTabletProcessor::minSeconds ? juce::String ("Play the track: needs about 20 s of music")
                                                                         : juce::String ("Waiting for a confident, stable result"),
                r.removeFromTop (20), juce::Justification::centredLeft);

    // Chroma bars
    auto c = chromaArea.reduced (16, 10).withTrimmedTop (18);
    auto labels = c.removeFromBottom (16);
    const auto w = c.getWidth() / 12;
    for (int i = 0; i < 12; ++i)
    {
        const auto v = det.chroma[(size_t) i].load();
        const auto bar = juce::Rectangle<int> (c.getX() + i * w, c.getY(), w, c.getHeight()).reduced (4, 0);
        g.setColour (Colours::outline);
        g.fillRoundedRectangle (bar.toFloat(), 3.0f);
        const auto inKey = best >= 0 && suite::music::scales()[(size_t) EmeraldTabletProcessor::scaleIndexFor (best >= 12)].steps.end()
                           != std::find (suite::music::scales()[(size_t) EmeraldTabletProcessor::scaleIndexFor (best >= 12)].steps.begin(),
                                         suite::music::scales()[(size_t) EmeraldTabletProcessor::scaleIndexFor (best >= 12)].steps.end(),
                                         ((i - best % 12) + 12) % 12);
        g.setColour ((inKey ? emerald : Colours::dimText).withAlpha (0.8f));
        g.fillRoundedRectangle (bar.toFloat().withTop ((float) bar.getBottom() - (float) bar.getHeight() * v), 3.0f);
        g.setColour (Colours::dimText);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (suite::music::noteNames()[i], labels.withX (c.getX() + i * w).withWidth (w), juce::Justification::centred);
    }
}

void EmeraldTabletEditor::resized()
{
    auto bounds = getLocalBounds().reduced (16);
    header.setBounds (bounds.removeFromTop (HeaderBar::height).withTrimmedTop (-8));
    bounds.removeFromTop (10);
    broadcastAlone.setVisible (header.advanced());
    resultArea = bounds.removeFromTop (170);
    bounds.removeFromTop (12);
    chromaArea = bounds;

    auto r = resultArea.reduced (16, 10).withTrimmedTop (18);
    auto right = r.removeFromRight (260);
    autoSend.setBounds (right.removeFromTop (34).reduced (2));
    if (header.advanced()) broadcastAlone.setBounds (right.removeFromTop (34).reduced (2));
    right.removeFromTop (6);
    auto buttons = right.removeFromTop (32);
    sendButton.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (2));
    resetButton.setBounds (buttons.reduced (2));
}
