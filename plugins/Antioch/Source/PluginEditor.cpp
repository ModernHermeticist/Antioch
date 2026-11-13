#include "PluginEditor.h"
#include "Music.h"

using namespace suite::ui;

namespace
{
    const juce::Colour tapColours[] = { Colours::brass, Colours::verdigris, Colours::amethyst, Colours::moss };
    const juce::Colour echoColour = Colours::brass, verbColour = Colours::verdigris, throwColour = Colours::ember;
}

//==============================================================================
class EchoPanel : public juce::Component
{
public:
    explicit EchoPanel (AntiochProcessor& p)
        : processor (p),
          resonance (p.apvts, Params::resonance, "Resonance", Colours::verdigris),
          harmonics (p.apvts, Params::resMix, "Harmonics", Colours::verdigris),
          damping (p.apvts, Params::damping, "Damping"),
          lowCut (p.apvts, Params::lowCut, "Low Cut"),
          toVerb (p.apvts, Params::echoToVerb, juce::String::fromUTF8 ("\xe2\x86\x92 Reverb"), verbColour)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &resonance, &harmonics, &damping, &lowCut, &toVerb })
            addAndMakeVisible (c);

        for (int t = 0; t < Params::numTaps; ++t)
        {
            rows[(size_t) t] = std::make_unique<Row> (p.apvts, t, tapColours[t]);
            addAndMakeVisible (*rows[(size_t) t]);
        }
    }

    void refresh (bool synced, float rootHz)
    {
        for (int t = 0; t < Params::numTaps; ++t)
        {
            auto& row = *rows[(size_t) t];
            row.division.setVisible (synced);
            row.time.setVisible (! synced);

            const auto harm = (int) processor.apvts.getRawParameterValue (Params::tapId (t, Params::Tap::harmonic))->load();
            const auto hz = rootHz * (float) harm;
            const auto midi = juce::roundToInt (69.0 + 12.0 * std::log2 (hz / 440.0));
            row.harmonicNote.setText ("H" + juce::String (harm) + juce::String::fromUTF8 ("  \xe2\x89\x88 ")
                                          + juce::MidiMessage::getMidiNoteName (midi, true, true, 4)
                                          + "  " + juce::String (juce::roundToInt (hz)) + " Hz",
                                      juce::dontSendNotification);
        }
    }

    void paint (juce::Graphics& g) override
    {
        drawPanel (g, globalArea, "Echo character", echoColour);
        drawPanel (g, tapArea);

        g.setColour (Colours::outline);
        const auto rowH = tapArea.getHeight() / Params::numTaps;
        for (int i = 1; i < Params::numTaps; ++i)
            g.drawHorizontalLine (tapArea.getY() + i * rowH, (float) tapArea.getX() + 12.0f, (float) tapArea.getRight() - 12.0f);
    }

    void resized() override
    {
        auto bounds = getLocalBounds();
        globalArea = bounds.removeFromTop (112);
        bounds.removeFromTop (8);
        tapArea = bounds;

        auto g = globalArea.reduced (12, 6).withTrimmedTop (12);
        layoutRow (g.withTrimmedLeft (g.getWidth() / 7).withTrimmedRight (g.getWidth() / 7),
                   { &resonance, &harmonics, &damping, &lowCut, &toVerb });

        const auto rowH = tapArea.getHeight() / Params::numTaps;
        for (int t = 0; t < Params::numTaps; ++t)
            rows[(size_t) t]->setBounds (tapArea.getX(), tapArea.getY() + t * rowH, tapArea.getWidth(), rowH);
    }

private:
    struct Row : public juce::Component
    {
        Row (juce::AudioProcessorValueTreeState& s, int t, juce::Colour colour)
            : on (s, Params::tapId (t, Params::Tap::on), "ON", colour),
              division (s, Params::tapId (t, Params::Tap::division), "Division"),
              time (s, Params::tapId (t, Params::Tap::timeMs), "Time", colour),
              pitch (s, Params::tapId (t, Params::Tap::pitch), "Pitch", colour),
              feedback (s, Params::tapId (t, Params::Tap::feedback), "Feedback", colour),
              harmonic (s, Params::tapId (t, Params::Tap::harmonic), "Harmonic", Colours::verdigris),
              level (s, Params::tapId (t, Params::Tap::level), "Level", colour),
              pan (s, Params::tapId (t, Params::Tap::pan), "Pan", colour)
        {
            title.setText ("TAP " + juce::String (t + 1), juce::dontSendNotification);
            title.setFont (juce::FontOptions (15.0f, juce::Font::bold));
            title.setColour (juce::Label::textColourId, colour);
            harmonic.slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            harmonicNote.setJustificationType (juce::Justification::centred);
            harmonicNote.setFont (juce::FontOptions (13.0f));
            harmonicNote.setColour (juce::Label::textColourId, Colours::text);

            for (auto* c : std::initializer_list<juce::Component*> { &title, &on, &division, &time, &pitch, &feedback,
                                                                     &harmonic, &harmonicNote, &level, &pan })
                addAndMakeVisible (c);
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (12, 4);
            const auto cellW = area.getWidth() / 7;

            auto titleCell = area.removeFromLeft (cellW);
            title.setBounds (titleCell.removeFromTop (titleCell.getHeight() / 2));
            on.setBounds (titleCell.withSizeKeepingCentre (56, 24).withX (titleCell.getX() + 4));

            auto timeCell = area.removeFromLeft (cellW);
            time.setBounds (timeCell);
            division.setBounds (timeCell);

            pitch.setBounds (area.removeFromLeft (cellW));
            feedback.setBounds (area.removeFromLeft (cellW));

            auto harmCell = area.removeFromLeft (cellW);
            harmonicNote.setBounds (harmCell.removeFromBottom (16).expanded (24, 0));
            harmonic.setBounds (harmCell);

            level.setBounds (area.removeFromLeft (cellW));
            pan.setBounds (area.removeFromLeft (cellW));
        }

        juce::Label title, harmonicNote;
        Toggle on;
        Choice division;
        Knob time, pitch, feedback, harmonic, level, pan;
    };

    AntiochProcessor& processor;
    Knob resonance, harmonics, damping, lowCut, toVerb;
    std::array<std::unique_ptr<Row>, Params::numTaps> rows;
    juce::Rectangle<int> globalArea, tapArea;
};

//==============================================================================
class ReverbPanel : public juce::Component
{
public:
    explicit ReverbPanel (AntiochProcessor& p)
        : processor (p),
          decay (p.apvts, Params::Verb::decay, "Decay", verbColour),
          size (p.apvts, Params::Verb::size, "Size", verbColour),
          predelay (p.apvts, Params::Verb::predelay, "Pre-delay", verbColour),
          mod (p.apvts, Params::Verb::mod, "Modulation", verbColour),
          width (p.apvts, Params::Verb::width, "Width", verbColour),
          damping (p.apvts, Params::Verb::damping, "Damping"),
          lowCut (p.apvts, Params::Verb::lowCut, "Low Cut"),
          highCut (p.apvts, Params::Verb::highCut, "High Cut"),
          shimmer (p.apvts, Params::Verb::shimmer, "Shimmer", Colours::amethyst),
          shimInt (p.apvts, Params::Verb::shimInt, "Shimmer Interval"),
          duck (p.apvts, Params::Verb::duck, "Duck", Colours::ember),
          duckRel (p.apvts, Params::Verb::duckRel, "Duck Release", Colours::ember),
          freeze (p.apvts, Params::Verb::freeze, "Freeze into breakdowns"),
          freezeLevel (p.apvts, Params::Verb::freezeLevel, "Freeze level", verbColour),
          tension (p.apvts, Params::Verb::tension, "Tension width", verbColour),
          pump (p.apvts, Params::Verb::pump, "Pump to kick", Colours::ember),
          leadRoom (p.apvts, Params::Verb::leadRoom, "Room for lead", Colours::ember)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &decay, &size, &predelay, &mod, &width, &damping, &lowCut,
                                                                 &highCut, &shimmer, &shimInt, &duck, &duckRel, &freeze, &freezeLevel, &tension, &pump, &leadRoom })
            addAndMakeVisible (c);
    }

    void refresh()
    {
        const auto d = processor.duckAmount.load();
        meter += 0.4f * (d - meter);
        repaint (meterArea);
    }

    void paint (juce::Graphics& g) override
    {
        drawPanel (g, spaceArea, "Space", verbColour);
        drawPanel (g, toneArea, "Tone & shimmer", Colours::amethyst);
        drawPanel (g, duckArea, "Ducking and freeze", Colours::ember);

        // Gain-reduction meter
        const auto r = meterArea.toFloat();
        g.setColour (Colours::background);
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (Colours::ember);
        g.fillRoundedRectangle (r.withWidth (r.getWidth() * juce::jlimit (0.0f, 1.0f, meter)), 3.0f);
        g.setColour (Colours::dimText);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("reverb ducked under dry signal", meterArea.translated (0, 12), juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto bounds = getLocalBounds();
        const auto rowH = (bounds.getHeight() - 16) / 3;
        spaceArea = bounds.removeFromTop (rowH);
        bounds.removeFromTop (8);
        toneArea = bounds.removeFromTop (rowH);
        bounds.removeFromTop (8);
        duckArea = bounds;

        auto inner = [] (juce::Rectangle<int> a) { return a.reduced (12, 6).withTrimmedTop (12); };
        layoutRow (inner (spaceArea), { &decay, &size, &predelay, &mod, &width, &tension });
        layoutRow (inner (toneArea), { &damping, &lowCut, &highCut, &shimmer, &shimInt });

        auto d = inner (duckArea);
        const auto cell = d.getWidth() / 7;
        duck.setBounds (d.removeFromLeft (cell));
        duckRel.setBounds (d.removeFromLeft (cell));
        pump.setBounds (d.removeFromLeft (cell));
        leadRoom.setBounds (d.removeFromLeft (cell));
        freeze.setBounds (d.removeFromLeft (cell));
        freezeLevel.setBounds (d.removeFromLeft (cell));
        meterArea = d.reduced (24, 0).withSizeKeepingCentre (d.getWidth() - 48, 10).translated (0, -6);
    }

private:
    AntiochProcessor& processor;
    Knob decay, size, predelay, mod, width, damping, lowCut, highCut, shimmer;
    Choice shimInt;
    Knob duck, duckRel;
    Choice freeze;
    Knob freezeLevel, tension, pump, leadRoom;
    juce::Rectangle<int> spaceArea, toneArea, duckArea, meterArea;
    float meter = 0.0f;
};

//==============================================================================
/** Big THROW pad: momentary (hold) by default, or toggles when Latch is on. */
class ThrowButton : public juce::Component
{
public:
    explicit ThrowButton (AntiochProcessor& p)
        : processor (p),
          param (*p.apvts.getParameter (Params::Throw::on)),
          latch (*p.apvts.getRawParameterValue (Params::Throw::latch))
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (2.0f);
        const auto lit = juce::jmax (processor.throwGate.load(), param.getValue() > 0.5f ? 1.0f : 0.0f);

        g.setColour (Colours::background.interpolatedWith (throwColour.withAlpha (0.6f), lit));
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (throwColour.withAlpha (0.4f + 0.6f * lit));
        g.drawRoundedRectangle (r, 10.0f, 2.0f);

        g.setColour (lit > 0.5f ? Colours::text : throwColour);
        g.setFont (juce::FontOptions (28.0f, juce::Font::bold));
        g.drawText ("THROW", r.withTrimmedBottom (r.getHeight() * 0.25f), juce::Justification::centred);
        g.setFont (juce::FontOptions (12.0f));
        g.setColour (Colours::dimText);
        g.drawText (latch.load() > 0.5f ? "click to toggle" : "hold to throw", r.withTrimmedTop (r.getHeight() * 0.5f),
                    juce::Justification::centred);
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        const auto next = latch.load() > 0.5f ? (param.getValue() < 0.5f) : true;
        set (next);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (latch.load() < 0.5f)
            set (false);
    }

private:
    void set (bool on)
    {
        param.beginChangeGesture();
        param.setValueNotifyingHost (on ? 1.0f : 0.0f);
        param.endChangeGesture();
        repaint();
    }

    AntiochProcessor& processor;
    juce::RangedAudioParameter& param;
    std::atomic<float>& latch;
};

class ThrowPanel : public juce::Component
{
public:
    explicit ThrowPanel (AntiochProcessor& p)
        : button (p),
          latch (p.apvts, Params::Throw::latch, "Latch", throwColour),
          autoMode (p.apvts, Params::Throw::autoMode, "Auto Throw"),
          autoLen (p.apvts, Params::Throw::autoLen, "Throw Window"),
          division (p.apvts, Params::Throw::division, "Delay Time"),
          feedback (p.apvts, Params::Throw::feedback, "Feedback", throwColour),
          pingPong (p.apvts, Params::Throw::pingPong, "Ping-Pong", throwColour),
          drive (p.apvts, Params::Throw::drive, "Drive", throwColour),
          lowCut (p.apvts, Params::Throw::lowCut, "Low Cut"),
          highCut (p.apvts, Params::Throw::highCut, "High Cut"),
          duck (p.apvts, Params::Throw::duck, "Duck", throwColour),
          toVerb (p.apvts, Params::Throw::toVerb, juce::String::fromUTF8 ("\xe2\x86\x92 Reverb"), verbColour)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &button, &latch, &autoMode, &autoLen, &division, &feedback,
                                                                 &pingPong, &drive, &lowCut, &highCut, &duck, &toVerb })
            addAndMakeVisible (c);

        hint.setText ("Auto Throw catches the end of every phrase while FL is playing; 'At section ends' follows "
                      "Ouroboros; 'At vocal phrase ends' throws the vocal's last words (play the song once with Solve et Coagula "
                      "hearing the vocal). Or automate the Throw switch for one-off throws.",
                      juce::dontSendNotification);
        hint.setFont (juce::FontOptions (12.0f));
        hint.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (hint);
    }

    void refresh() { button.repaint(); }

    void paint (juce::Graphics& g) override
    {
        drawPanel (g, triggerArea, "Trigger", throwColour);
        drawPanel (g, soundArea, "Delay", throwColour);
    }

    void resized() override
    {
        auto bounds = getLocalBounds();
        triggerArea = bounds.removeFromLeft (bounds.getWidth() / 3);
        bounds.removeFromLeft (8);
        soundArea = bounds;

        auto t = triggerArea.reduced (14, 8).withTrimmedTop (14);
        button.setBounds (t.removeFromTop (150));
        t.removeFromTop (10);
        latch.setBounds (t.removeFromTop (28).withSizeKeepingCentre (90, 26));
        t.removeFromTop (10);
        auto choices = t.removeFromTop (54);
        autoMode.setBounds (choices.removeFromLeft (choices.getWidth() / 2));
        autoLen.setBounds (choices);
        t.removeFromTop (10);
        hint.setBounds (t.removeFromTop (60));

        auto s = soundArea.reduced (12, 6).withTrimmedTop (14);
        const auto rowH = s.getHeight() / 2;
        layoutRow (s.removeFromTop (rowH), { &division, &feedback, &pingPong, &drive });
        layoutRow (s, { &lowCut, &highCut, &duck, &toVerb });
    }

private:
    ThrowButton button;
    Toggle latch;
    Choice autoMode, autoLen, division;
    Knob feedback, pingPong, drive, lowCut, highCut, duck, toVerb;
    juce::Label hint;
    juce::Rectangle<int> triggerArea, soundArea;
};

//==============================================================================
AntiochEditor::AntiochEditor (AntiochProcessor& p)
    : AudioProcessorEditor (p), processor (p),
      syncButton (p.apvts, Params::sync, "Tempo Sync"),
      rootChoice (p.apvts, Params::rootNote, "Key"),
      followButton (p.apvts, Params::followKey, "Follow Key", Colours::verdigris),
      octave (p.apvts, Params::rootOct, "Octave"),
      dry (p.apvts, Params::dry, "Dry", Colours::text),
      echoLevel (p.apvts, Params::echoLevel, "Echo", echoColour),
      verbLevel (p.apvts, Params::verbLevel, "Reverb", verbColour),
      throwLevel (p.apvts, Params::throwLevel, "Throw", throwColour),
      output (p.apvts, Params::output, "Output", Colours::text),
      tabs ({ "ECHO", "REVERB", "THROW", "SCENES" }, { echoColour, verbColour, throwColour, Colours::ember })
{
    setLookAndFeel (&lnf);

    echoPanel = std::make_unique<EchoPanel> (p);
    reverbPanel = std::make_unique<ReverbPanel> (p);
    throwPanel = std::make_unique<ThrowPanel> (p);
    scenePanel = std::make_unique<suite::ui::ScenePanel> (p.scenes, p.apvts);

    for (auto* c : std::initializer_list<juce::Component*> { &syncButton, &bpmLabel, &keyStatus, &rootChoice, &followButton,
                                                             &octave, &dry, &echoLevel, &verbLevel, &throwLevel, &output, &tabs,
                                                             echoPanel.get(), reverbPanel.get(), throwPanel.get(), scenePanel.get() })
        addAndMakeVisible (c);

    keyStatus.setJustificationType (juce::Justification::centred);
    keyStatus.setFont (juce::FontOptions (11.0f));

    tabs.onChange = [this] (int index)
    {
        echoPanel->setVisible (index == 0);
        reverbPanel->setVisible (index == 1);
        throwPanel->setVisible (index == 2);
        scenePanel->setVisible (index == 3);
    };
    tabs.onChange (0);
    tabs.setComponentID ("tabs");

    tweakBanner = std::make_unique<suite::ui::TweakBanner> (p.scenes);   // "make it a scene?" after you tweak a knob in a section
    addChildComponent (*tweakBanner);
    autopilotStrip.director = [] { return suite::DirectorBus::cue (suite::director::pAntioch); };
    autopilotStrip.moves = [&p]
    {
        juce::StringArray m;
        if (p.noteRoomNow.load() > 0.3f) m.add ("the tail making room at the lead's note");
        if (p.pumpNow.load() < 0.9f) m.add ("the tail pumping with the kick");
        if (p.leadRoomNow.load() < 0.9f) m.add ("stepping back while the lead or vocal plays");
        if (p.timedNow.load()) m.add ("pre-delay and decay on the tempo");
        if (p.sibNow.load() < -2.0f) m.add ("de-essing what goes into the space");
        if (p.levelHoldDb.load() < -0.5f) m.add ("holding the level to the input's (" + juce::String (p.levelHoldDb.load(), 1) + " dB)");
        return m;
    };
    addAndMakeVisible (autopilotStrip);
    setSize (960, 770);
    timerCallback();
    startTimerHz (20);
}

AntiochEditor::~AntiochEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void AntiochEditor::timerCallback()
{
    auto& state = processor.apvts;
    const auto synced = state.getRawParameterValue (Params::sync)->load() > 0.5f;
    bpmLabel.setText (synced ? juce::String (processor.currentBpm.load(), 1) + " BPM" : "Free time", juce::dontSendNotification);

    const auto root = processor.effectiveRoot.load();
    const auto following = processor.followingBus.load();
    rootChoice.box.setEnabled (! following);
    keyStatus.setColour (juce::Label::textColourId, following ? Colours::verdigris : Colours::dimText);
    keyStatus.setText (following ? "Following: " + suite::music::keyName (root, processor.busScale.load())
                                 : (state.getRawParameterValue (Params::followKey)->load() > 0.5f ? "No Correspondence found" : "Manual key"),
                       juce::dontSendNotification);

    const auto oct = (int) state.getRawParameterValue (Params::rootOct)->load();
    echoPanel->refresh (synced, suite::music::midiToHz ((float) (12 * (oct + 1) + root)));
    reverbPanel->refresh();
    throwPanel->refresh();
    suite::ui::updateSceneGhosts (*this, processor.scenes);
}

void AntiochEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);
    drawHeader (g, { 20, 12, 600, 32 }, "ANTIOCH", juce::String::fromUTF8 ("harmonic echo \xc2\xb7 euphoric reverb \xc2\xb7 throw"),
                Colours::brass);
    drawPanel (g, masterArea, "Master");
}

void AntiochEditor::resized()
{
    autopilotStrip.setBounds (getLocalBounds().removeFromBottom (suite::ui::AutopilotStrip::height + 8).reduced (16, 4));
    auto bounds = getLocalBounds().withTrimmedBottom (suite::ui::AutopilotStrip::height + 4).reduced (16);

    auto header = bounds.removeFromTop (40);
    bpmLabel.setBounds (header.removeFromRight (100));
    syncButton.setBounds (header.removeFromRight (120).reduced (4, 6));

    bounds.removeFromTop (8);
    masterArea = bounds.removeFromTop (118);
    bounds.removeFromTop (10);
    tabs.setBounds (bounds.removeFromTop (34));
    bounds.removeFromTop (10);
    panelArea = bounds;

    auto m = masterArea.reduced (12, 6).withTrimmedTop (12);
    const auto cell = m.getWidth() / 9;
    auto keyCell = m.removeFromLeft (cell + 20);
    keyStatus.setBounds (keyCell.removeFromBottom (18).expanded (10, 0));
    rootChoice.setBounds (keyCell);
    auto followCell = m.removeFromLeft (cell);
    followButton.setBounds (followCell.withSizeKeepingCentre (cell - 12, 28).translated (0, 8));
    layoutRow (m, { &octave, &dry, &echoLevel, &verbLevel, &throwLevel, &output });

    for (auto* panel : std::initializer_list<juce::Component*> { echoPanel.get(), reverbPanel.get(), throwPanel.get(), scenePanel.get() })
        panel->setBounds (panelArea);
}
