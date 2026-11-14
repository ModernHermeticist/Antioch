#include "PluginEditor.h"

using namespace suite::ui;

namespace
{
    const juce::Colour accent = Colours::amethyst.interpolatedWith (Colours::verdigris, 0.3f);
    using P = SolveEtCoagulaProcessor;
}

SolveEtCoagulaEditor::SolveEtCoagulaEditor (SolveEtCoagulaProcessor& p)
    : AudioProcessorEditor (p), processor (p),
      header ("SOLVE ET COAGULA", "vocal unmasking", accent),
      depth (p.apvts, P::depthId, "Depth", Colours::ember),
      sensitivity (p.apvts, P::sensitivityId, "Sensitivity", accent),
      focus (p.apvts, P::focusId, "Focus", accent),
      mix (p.apvts, P::mixId, "Mix", Colours::brass),
      speed (p.apvts, P::speedId, "Speed"),
      viewTabs ({ "BANDS", "SCENES" }, { accent, Colours::ember }),
      chopsOn (p.apvts, P::chopsOnId, "Vocal chops", Colours::ember),
      chopsOctave (p.apvts, P::chopsOctaveId, "Octave up", accent),
      chopsWhere (p.apvts, P::chopsWhereId, "Chops in"),
      chopsPattern (p.apvts, P::chopsPatternId, "Pattern"),
      chopsLevel (p.apvts, P::chopsLevelId, "Level", Colours::ember)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (header);
    header.addRight (saveMidi, 150);
    header.addRight (checkVocal, 180);
    header.actions = [this] { return actions(); };
    header.enableAdvanced (p.apvts.state, [this] (bool) { resized(); repaint(); });
    status.setFont (juce::FontOptions (13.0f));
    status.setJustificationType (juce::Justification::centredLeft);
    for (auto* c : std::initializer_list<juce::Component*> { &depth, &sensitivity, &focus, &speed, &mix, &status,
                                                             &chopsOn, &chopsOctave, &chopsWhere, &chopsPattern, &chopsLevel, &chopsStatus })
        addAndMakeVisible (c);
    keyShown.fill (-100.0f);
    scenePanel = std::make_unique<suite::ui::ScenePanel> (p.scenes, p.apvts);
    addChildComponent (*scenePanel);
    addAndMakeVisible (viewTabs);
    viewTabs.setComponentID ("tabs");
    viewTabs.onChange = [this] (int i) { scenePanel->setVisible (i == 1); repaint(); };
    saveMidi.setTooltip ("Writes what the sidechain vocal sang as MIDI (on the 1/16 grid, in key), and a harmony a third above, into the Vocal MIDI folder");
    saveMidi.onClick = [this]
    {
        const auto result = processor.saveVocalMidi (SolveEtCoagulaProcessor::vocalMidiFolder());
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "Vocal MIDI", result);
    };
    checkVocal.setTooltip ("Lists the vocal's notes that sit more than 25 cents off the key or 30 ms off the grid (a check: nothing is moved)");
    checkVocal.onClick = [this] { juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "Vocal tuning and timing", processor.checkVocal()); };
    chopsStatus.setFont (juce::FontOptions (12.0f));
    chopsStatus.setJustificationType (juce::Justification::centredLeft);
    chopsStatus.setColour (juce::Label::textColourId, Colours::dimText);
    tweakBanner = std::make_unique<suite::ui::TweakBanner> (p.scenes);   // "make it a scene?" after you tweak a knob in a section
    addChildComponent (*tweakBanner);
    setSize (860, 780);
    timerCallback();
    startTimerHz (30);
}

SolveEtCoagulaEditor::~SolveEtCoagulaEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SolveEtCoagulaEditor::timerCallback()
{
    suite::Watchdog::Scope watch (processor.getName(), "editor refresh");
    for (int b = 0; b < Unmasker::numBands; ++b)
    {
        cutShown[(size_t) b] = processor.unmasker.getCutDb (b);
        keyShown[(size_t) b] = std::max (processor.unmasker.getKeyDb (b), keyShown[(size_t) b] - 1.5f);
    }

    juce::String text;
    if (! processor.sidechainConnected.load())
        text = "No vocal on the sidechain: in FL's mixer, route the vocal track to this one (sidechain only). Passing through untouched.";
    else if (processor.vocalPresent.load())
        text = "Vocal present: the music dips where the vocal lives.";
    else
        text = "Sidechain connected; waiting for the vocal.";
    status.setColour (juce::Label::textColourId, processor.sidechainConnected.load() ? Colours::text : Colours::dimText);
    status.setText (text, juce::dontSendNotification);
    header.setStatus (! processor.sidechainConnected.load() ? "No vocal on the sidechain"
                      : processor.vocalPresent.load() ? "Vocal present: unmasking" : "Waiting for the vocal",
                      ! processor.sidechainConnected.load() ? Colours::ember : processor.vocalPresent.load() ? Colours::moss : Colours::faintText);
    chopsStatus.setText (processor.chopsPlaying.load() ? "Chopping the vocal's last bar."
                         : processor.chopsReady.load() ? "A bar of vocal is ready to chop."
                                                       : "Listens to the sidechain vocal outside the chop sections and keeps its last whole bar.",
                         juce::dontSendNotification);
    if (! processor.melody.notes().empty())
        chopsStatus.setText (chopsStatus.getText() + "  " + juce::String ((int) processor.melody.notes().size()) + " sung notes heard.", juce::dontSendNotification);
    suite::ui::updateSceneGhosts (*this, processor.scenes);
    repaint (bandArea);
}

void SolveEtCoagulaEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);
    drawPanel (g, controlArea, "Controls", accent);
    drawPanel (g, chopsArea, "Vocal chops (on top of the music)", Colours::ember);
    if (viewTabs.getSelected() == 1)
        return;   // the scene panel draws itself
    drawPanel (g, bandArea, "Bands  (vocal level up, music cut down)");

    auto r = bandArea.reduced (16, 10).withTrimmedTop (18).toFloat();
    auto labels = r.removeFromBottom (18);
    const auto mid = r.getY() + r.getHeight() * 0.5f;
    g.setColour (Colours::outline);
    g.drawHorizontalLine ((int) mid, r.getX(), r.getRight());

    const auto w = r.getWidth() / (float) Unmasker::numBands;
    const auto depthDb = std::max (1.0f, processor.apvts.getRawParameterValue (P::depthId)->load());
    for (int b = 0; b < Unmasker::numBands; ++b)
    {
        const auto x = r.getX() + w * (float) b;
        auto col = juce::Rectangle<float> (x, r.getY(), w, r.getHeight()).reduced (w * 0.2f, 0.0f);

        // Vocal level: -60 .. -10 dB fills the upper half.
        const auto key = juce::jlimit (0.0f, 1.0f, (keyShown[(size_t) b] + 60.0f) / 50.0f);
        const auto up = (mid - r.getY() - 4.0f) * key;
        g.setColour (accent.withAlpha (0.55f));
        g.fillRoundedRectangle (col.getX(), mid - 2.0f - up, col.getWidth(), up, 3.0f);

        // Cut: 0 .. depth fills the lower half.
        const auto cut = juce::jlimit (0.0f, 1.0f, -cutShown[(size_t) b] / depthDb);
        const auto down = (r.getBottom() - mid - 22.0f) * cut;
        g.setColour (Colours::ember);
        g.fillRoundedRectangle (col.getX(), mid + 2.0f, col.getWidth(), down, 3.0f);
        if (cutShown[(size_t) b] < -0.1f)
        {
            g.setColour (Colours::text);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (juce::String (cutShown[(size_t) b], 1), juce::Rectangle<float> (x, mid + 4.0f + down, w, 14.0f), juce::Justification::centred);
        }

        const auto hz = Unmasker::centres[(size_t) b];
        g.setColour (Colours::dimText);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (hz >= 1000.0f ? juce::String (hz / 1000.0f, hz < 2000.0f ? 1 : 1) + "k" : juce::String ((int) hz),
                    juce::Rectangle<float> (x, labels.getY(), w, labels.getHeight()), juce::Justification::centred);
    }
}

void SolveEtCoagulaEditor::resized()
{
    auto bounds = getLocalBounds().reduced (16);
    header.setBounds (bounds.removeFromTop (HeaderBar::height).withTrimmedTop (-8));
    bounds.removeFromTop (10);
    const auto advanced = header.advanced();
    setShown ({ &sensitivity, &focus, &speed, &chopsWhere, &chopsPattern, &chopsOctave }, advanced);
    viewTabs.setBounds (bounds.removeFromTop (30).removeFromRight (300).withTrimmedBottom (4));
    bandArea = bounds.removeFromTop (300);
    scenePanel->setBounds (bandArea);
    bounds.removeFromTop (12);
    chopsArea = bounds.removeFromBottom (140);
    bounds.removeFromBottom (12);
    controlArea = bounds;

    auto ch = chopsArea.reduced (12, 8).withTrimmedTop (14);
    chopsStatus.setBounds (ch.removeFromBottom (24));
    chopsOn.setBounds (ch.removeFromLeft (150).withSizeKeepingCentre (140, 32));
    chopsLevel.setBounds (ch.removeFromLeft (110));
    if (advanced)
    {
        chopsWhere.setBounds (ch.removeFromLeft (170));
        chopsPattern.setBounds (ch.removeFromLeft (150));
        chopsOctave.setBounds (ch.removeFromLeft (150).withSizeKeepingCentre (130, 32));
    }

    auto c = controlArea.reduced (12, 8).withTrimmedTop (14);
    status.setBounds (c.removeFromBottom (36));
    if (advanced) layoutRow (c, { &depth, &sensitivity, &focus, &speed, &mix });
    else          layoutRow (c.withSizeKeepingCentre (juce::jmin (c.getWidth(), 360), c.getHeight()), { &depth, &mix });
}

std::vector<suite::ui::Action> SolveEtCoagulaEditor::actions()
{
    std::vector<suite::ui::Action> a;
    a.push_back ({ "Save vocal MIDI", "what the vocal sang, on the grid and in key, plus a harmony a third above", "Vocal", [this] { saveMidi.triggerClick(); } });
    a.push_back ({ "Check tuning & timing", "lists notes off the key or off the grid (nothing is moved)", "Vocal", [this] { checkVocal.triggerClick(); } });
    a.push_back ({ chopsOn.getToggleState() ? "Turn vocal chops off" : "Turn vocal chops on", "chops of the vocal's last bar, on top of the music", "Chops",
                   [this] { chopsOn.setToggleState (! chopsOn.getToggleState(), juce::sendNotification); } });
    a.push_back ({ "Show the bands", "vocal level up, music cut down", "View", [this] { viewTabs.setSelected (0); } });
    a.push_back ({ "Show the section scenes", "per-section offsets for the knobs", "View", [this] { viewTabs.setSelected (1); } });
    return a;
}
