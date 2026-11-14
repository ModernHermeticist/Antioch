#include "PrimaMateriaEditor.h"
#include "FixBus.h"

using namespace suite::ui;

namespace { const juce::Colour accent { 0xffb98b5e }; }

PrimaMateriaEditor::PrimaMateriaEditor (PrimaMateriaProcessor& p)
    : AudioProcessorEditor (p), processor (p),
      header ("PRIMA MATERIA", "section mixer (suggestions only)", accent),
      follow (p.apvts, pm::followId, "Follow Ouroboros", Colours::ember),
      ramp (p.apvts, pm::rampId, "Ramp through builds", Colours::brass),
      laterOn (p.apvts, pm::laterOnId, "Separate Drop 2+", Colours::ember),
      lowManager (p.apvts, pm::lowManagerId, "Low-end manager", Colours::verdigris),
      morph (p.apvts, pm::morphId, "Morph"),
      gap (p.apvts, pm::gapId, "Gap before drop"),
      monoLows (p.apvts, pm::monoLowsId, "Mono lows")
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (header);
    header.addRight (follow, 170);
    header.addRight (previewButton, 100);
    header.addRight (suggestionsButton, 220);
    makePrimary (suggestionsButton, accent);
    header.actions = [this] { return actions(); };
    header.enableAdvanced (p.apvts.state, [this] (bool) { resized(); repaint(); });

    roleBox.setTextWhenNothingSelected ("Track role preset...");
    for (size_t i = 0; i < pm::roles().size(); ++i)
        roleBox.addItem (pm::roles()[i].name, (int) i + 1);
    roleBox.onChange = [this] { if (roleBox.getSelectedId() > 0) processor.applyRole (roleBox.getSelectedId() - 1); };
    saveRole.setTooltip ("Keeps this track's section settings as yours for its role: every track given this role "
                         "(by hand or from its name) starts from them, in every project");
    saveRole.onClick = [this]
    {
        const auto role = processor.getRole();
        if (role >= 0 && processor.saveMyRole())
            saveRole.setButtonText (juce::String ("Saved as my ") + pm::roles()[(size_t) role].name);
    };

    auto columnColour = [] (int t) { return t == pm::laterDrop ? sectionColour (suite::SectionType::Drop).brighter (0.35f) : sectionColour ((suite::SectionType) t); };
    for (int t = 0; t < pm::numColumns; ++t)
    {
        const auto c = columnColour (t);
        knobs.push_back (std::make_unique<Knob> (p.apvts, pm::levelId (t), "Level", c));
        knobs.push_back (std::make_unique<Knob> (p.apvts, pm::lowCutId (t), "Low cut", c));
        knobs.push_back (std::make_unique<Knob> (p.apvts, pm::highCutId (t), "High cut", c));
    }
    for (auto& k : knobs)
    {
        k->maxHeight = 110;
        k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 16);
        addAndMakeVisible (*k);
    }

    trim = std::make_unique<Knob> (p.apvts, pm::trimId, "Trim", accent);
    trim->maxHeight = 56;
    trim->slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 16);
    trim->slider.setTooltip ("Track level after everything else. Mentalism's Rough mix sets it for the track's role.");
    addAndMakeVisible (*trim);

    breath = std::make_unique<Knob> (p.apvts, pm::breathId, "Breath", accent);
    pump = std::make_unique<Knob> (p.apvts, pm::pumpId, "Pump", Colours::ember);
    sweep = std::make_unique<Knob> (p.apvts, pm::sweepId, "BD sweep", accent);
    vocalRoom = std::make_unique<Knob> (p.apvts, pm::vocalRoomId, "Vox room", accent);
    rideVocal = std::make_unique<Knob> (p.apvts, pm::rideVocalId, "Ride vox", accent);
    rideVocal->slider.setTooltip ("Vocal rider: rides this vocal's level (within 6 dB) so it sits this far above the music in the 1-4 kHz range, "
                                  "as Solve et Coagula hears it (send the vocal to Solve et Coagula post-fader). 0 = off");
    inputGain = std::make_unique<Knob> (p.apvts, pm::inputGainId, "Input gain", Colours::text);
    answer = std::make_unique<Choice> (p.apvts, pm::answerId, "Call / response");
    for (auto* k : { breath.get(), pump.get(), sweep.get(), vocalRoom.get(), rideVocal.get(), inputGain.get() })
    {
        k->maxHeight = 46;
        addAndMakeVisible (*k);
    }
    addAndMakeVisible (*answer);
    breath->slider.setTooltip ("A slow 8-bar swell in breakdowns and drops, so long chords feel alive");
    vocalRoom->slider.setTooltip ("Pads, leads and FX step back this much while the vocal sings (heard by Solve et Coagula), and come back in its gaps");
    sweep->slider.setTooltip ("Breakdown sweep: a low-pass that opens slowly across each breakdown, fully open just before the build");
    pump->slider.setTooltip ("Ducks this track to the kick pattern a Kybalion shares (Share kick), no routing needed");
    gainStageButton.setTooltip ("Works out every track's channel gain so it arrives at -18 dBFS RMS (play the song first); each Prima Materia lists it in its suggestions");
    gainStageButton.onClick = [] { suite::FixBus::send (suite::Fix::GainStage); };
    addAndMakeVisible (gainStageButton);
    suggestionsButton.setTooltip ("Prima Materia never moves this track's level or filters it: here are the moves it suggests, section by section, "
                                  "to make in FL's mixer, and an export of the fader moves as automation");
    suggestionsButton.onClick = [this] { showSuggestions(); };
    previewButton.setTooltip ("Hear the suggested moves while this is on (not saved: the track always loads untouched)");
    previewButton.setClickingTogglesState (true);
    previewButton.setToggleState (processor.previewPlan.load(), juce::dontSendNotification);
    previewButton.onClick = [this] { processor.previewPlan.store (previewButton.getToggleState()); };
    autoStage = std::make_unique<suite::ui::Toggle> (p.apvts, pm::autoStageId, "Keep at -18", Colours::moss);
    autoStage->setTooltip ("Continuous gain staging: every 10 s, if this track's input has drifted more than 1.5 dB from -18 dBFS RMS, Input gain follows");
    addAndMakeVisible (*autoStage);
    inputLabel.setFont (juce::FontOptions (12.0f));
    inputLabel.setColour (juce::Label::textColourId, Colours::dimText);
    addAndMakeVisible (inputLabel);

    status.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    lowManager.setTooltip ("Where a kick or bass track is playing, this track leaves the low end to them (an automatic low cut for its role)");
    for (auto* c : std::initializer_list<juce::Component*> { &roleBox, &saveRole, &ramp, &laterOn, &lowManager, &morph, &gap, &monoLows, &status })
        addAndMakeVisible (c);

    autopilotStrip.director = [] { return suite::DirectorBus::mixCue (suite::DirectorBus::cueSilence); };
    autopilotStrip.moves = [&p]
    {
        juce::StringArray m;
        const auto n = p.planLines().size();
        m.add ("suggesting " + juce::String (n) + (n == 1 ? " mixer move" : " mixer moves") + " (it changes nothing itself)");
        if (p.previewPlan.load()) m.add ("previewing them now");
        return m;
    };
    addAndMakeVisible (autopilotStrip);
    setSize (1290, 700);
    timerCallback();
    startTimerHz (15);
}

PrimaMateriaEditor::~PrimaMateriaEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void PrimaMateriaEditor::showSuggestions()
{
    const auto lines = processor.planLines();
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::NoIcon, "Suggested mixer moves",
        lines.joinIntoString ("\n") + "\n\nPrima Materia changes nothing itself. Export writes the fader moves as a MIDI clip (CC 7) and this list as text.",
        "Export...", "Close", this,
        juce::ModalCallbackFunction::create ([this] (int result)
        {
            if (result != 1) return;
            chooser = std::make_unique<juce::FileChooser> ("Folder for the mixer moves", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc)
            {
                const auto dir = fc.getResult();
                if (dir == juce::File()) return;
                const auto file = processor.exportPlan (dir);
                if (file == juce::File())
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "Mixer moves", "The export follows the Ouroboros map: load Ouroboros with a map (and set this track's role) first.");
                else file.revealToUser();
            });
        }));
}

void PrimaMateriaEditor::timerCallback()
{
    suite::Watchdog::Scope watch (processor.getName(), "editor refresh");
    {
        const auto isVocal = processor.getRole() == 6;
        rideVocal->setVisible (isVocal && header.advanced());
        vocalRoom->setVisible (! isVocal && header.advanced());
    }
    const auto t = processor.currentType.load();
    if (t != shownType)
    {
        shownType = t;
        repaint (gridArea);
    }

    auto hz = [] (float v) { return v >= 1000.0f ? juce::String (v / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; };
    const auto lvl = processor.nowLevelDb.load();
    juce::String text;
    if (t >= 0)
    {
        text << "Now: " << (t == pm::laterDrop ? juce::String ("Drop 2+") : juce::String (suite::sectionName ((suite::SectionType) t)))
             << (processor.ramping.load() ? " (ramping)" : "") << (processor.gapping.load() ? " (gap)" : "")
             << juce::String::fromUTF8 ("  \xc2\xb7  level ") << (lvl <= pm::offDb + 0.05f ? juce::String ("off") : juce::String (lvl, 1) + " dB");
        if (processor.nowLowCut.load() > pm::lowCutOff + 1.0f) text << ", low cut " << hz (processor.nowLowCut.load());
        if (processor.nowHighCut.load() < pm::highCutOff - 100.0f) text << ", high cut " << hz (processor.nowHighCut.load());
    }
    else
    {
        text = processor.mapAvailable.load() ? "Ouroboros found: waiting for playback (passing audio unchanged)"
                                             : "No Ouroboros: passing audio unchanged";
    }
    status.setColour (juce::Label::textColourId, t >= 0 ? sectionColour ((suite::SectionType) juce::jmin (t, pm::numTypes - 1)) : Colours::dimText);
    if (const auto cut = processor.managerCutNow.load(); cut > 0.0f)
        text << "   |   Low end left to the kick/bass (cut " << juce::String (juce::roundToInt (cut)) << " Hz)";
    if (const auto carveBand = (int) processor.apvts.getRawParameterValue (pm::carveBandId)->load() - 1; carveBand >= 0)
        text << "   |   Carved " << juce::String (processor.apvts.getRawParameterValue (pm::carveDepthId)->load(), 1) << " dB at "
             << processor.apvts.getParameter (pm::carveBandId)->getCurrentValueAsText();
    const auto lufs = processor.measuredLufs();
    if (lufs > -70.0f)
        text << "   |   Drop loudness " << juce::String (lufs, 1) << " LUFS";
    if (processor.roleGuessed.load() && processor.getRole() >= 0)
        text << "   |   Role guessed from the sound: " << pm::roles()[(size_t) processor.getRole()].name << " (choose another if it's wrong)";
    status.setText (text, juce::dontSendNotification);
    {
        const auto n = processor.planLines().size();
        header.setStatus (juce::String (n) + (n == 1 ? " suggested move" : " suggested moves") + (processor.previewPlan.load() ? " (previewing)" : ""),
                          processor.previewPlan.load() ? Colours::ember : n > 0 ? Colours::brass : Colours::moss);
    }

    const auto in = processor.inputLevelDb();
    inputLabel.setText (in > -80.0f ? "Input " + juce::String (in, 1) + " dBFS RMS" : juce::String ("Input: play to measure"), juce::dontSendNotification);

    // The role (also set automatically from the track's name).
    const auto role = processor.getRole();
    if (role >= 0 && roleBox.getSelectedId() != role + 1)
        roleBox.setSelectedId (role + 1, juce::dontSendNotification);
}

void PrimaMateriaEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);
    drawPanel (g, gridArea, {});

    for (int t = 0; t < pm::numColumns; ++t)
    {
        const auto col = columns[(size_t) t];
        const auto c = t == pm::laterDrop ? sectionColour (suite::SectionType::Drop).brighter (0.35f) : sectionColour ((suite::SectionType) t);
        const auto live = t == shownType;
        g.setColour (c.withAlpha (live ? 0.22f : 0.08f));
        g.fillRoundedRectangle (col.toFloat().reduced (3.0f), 6.0f);
        if (live)
        {
            g.setColour (c);
            g.drawRoundedRectangle (col.toFloat().reduced (3.0f), 6.0f, 2.0f);
        }
        g.setColour (c.brighter (0.3f));
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText (t == pm::laterDrop ? juce::String ("DROP 2+") : juce::String (suite::sectionName ((suite::SectionType) t)).toUpperCase(),
                    col.withHeight (30).translated (0, 6),
                    juce::Justification::centred);
    }
}

void PrimaMateriaEditor::resized()
{
    autopilotStrip.setBounds (getLocalBounds().removeFromBottom (suite::ui::AutopilotStrip::height + 8).reduced (16, 4));
    auto bounds = getLocalBounds().withTrimmedBottom (suite::ui::AutopilotStrip::height + 4).reduced (16);
    header.setBounds (bounds.removeFromTop (HeaderBar::height).withTrimmedTop (-8));
    bounds.removeFromTop (6);
    const auto advanced = header.advanced();
    setShown ({ &morph, &gap, &monoLows, breath.get(), pump.get(), sweep.get(), answer.get(), inputGain.get(), &inputLabel, &gainStageButton, autoStage.get() }, advanced);
    if (! advanced) { vocalRoom->setVisible (false); rideVocal->setVisible (false); }
    for (int t = 0; t < pm::numColumns; ++t)
        for (int r = 1; r < 3; ++r) knobs[(size_t) (t * 3 + r)]->setVisible (advanced);

    auto controls = bounds.removeFromTop (60);
    trim->setBounds (controls.removeFromRight (150).withTrimmedTop (2));
    trim->slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 16);
    roleBox.setBounds (controls.removeFromLeft (220).withSizeKeepingCentre (220, 28).translated (0, 8));
    saveRole.setBounds (controls.removeFromLeft (170).withSizeKeepingCentre (160, 28).translated (0, 8));
    controls.removeFromLeft (12);
    ramp.setBounds (controls.removeFromLeft (180).withSizeKeepingCentre (172, 30).translated (0, 8));
    laterOn.setBounds (controls.removeFromLeft (160).withSizeKeepingCentre (152, 30).translated (0, 8));
    lowManager.setBounds (controls.removeFromLeft (160).withSizeKeepingCentre (152, 30).translated (0, 8));
    morph.setBounds (controls.removeFromLeft (150));
    gap.setBounds (controls.removeFromLeft (150));
    monoLows.setBounds (controls.removeFromLeft (150));


    if (advanced)
    {
        auto row = bounds.removeFromTop (62);
        breath->setBounds (row.removeFromLeft (150));
        pump->setBounds (row.removeFromLeft (150));
        sweep->setBounds (row.removeFromLeft (125));
        vocalRoom->setBounds (row.removeFromLeft (125));
        rideVocal->setBounds (vocalRoom->getBounds());   // the vocal track shows the rider here instead
        answer->setBounds (row.removeFromLeft (150));
        row.removeFromLeft (20);
        inputGain->setBounds (row.removeFromLeft (150));
        inputLabel.setBounds (row.removeFromLeft (100));
        gainStageButton.setBounds (row.removeFromLeft (190).withSizeKeepingCentre (180, 30));
        autoStage->setBounds (row.removeFromLeft (140).withSizeKeepingCentre (130, 30));
        for (auto* k : { breath.get(), pump.get(), sweep.get(), vocalRoom.get(), rideVocal.get(), inputGain.get() })
            k->slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 16);
    }
    status.setBounds (bounds.removeFromBottom (30));
    bounds.removeFromBottom (6);
    gridArea = bounds;

    auto grid = gridArea.reduced (8);
    const auto w = grid.getWidth() / pm::numColumns;
    for (int t = 0; t < pm::numColumns; ++t)
    {
        columns[(size_t) t] = juce::Rectangle<int> (grid.getX() + t * w, grid.getY(), w, grid.getHeight());
        auto col = columns[(size_t) t].reduced (6).withTrimmedTop (34);
        if (! advanced)
        {
            knobs[(size_t) (t * 3)]->maxHeight = 150;
            knobs[(size_t) (t * 3)]->setBounds (col.withSizeKeepingCentre (col.getWidth(), juce::jmin (col.getHeight(), 170)));
            continue;
        }
        knobs[(size_t) (t * 3)]->maxHeight = 110;
        const auto rowH = col.getHeight() / 3;
        for (int r = 0; r < 3; ++r)
            knobs[(size_t) (t * 3 + r)]->setBounds (col.removeFromTop (rowH));
    }
}

std::vector<suite::ui::Action> PrimaMateriaEditor::actions()
{
    std::vector<suite::ui::Action> a;
    a.push_back ({ "Suggested mixer moves", "section by section, to make in FL's mixer; export as automation", "Suggestions", [this] { showSuggestions(); } });
    a.push_back ({ previewButton.getToggleState() ? "Stop previewing the moves" : "Preview the suggested moves", "hear them (never saved)", "Suggestions",
                   [this] { previewButton.triggerClick(); } });
    a.push_back ({ "Gain-stage all tracks", "every track's channel gain for -18 dBFS RMS", "Levels", [this] { gainStageButton.triggerClick(); } });
    a.push_back ({ "Save as my role", "this track's section settings, for every track with this role", "Role", [this] { saveRole.triggerClick(); } });
    for (size_t i = 0; i < pm::roles().size(); ++i)
        a.push_back ({ "Role: " + juce::String (pm::roles()[i].name), "start from this role's section settings", "Role", [this, i] { roleBox.setSelectedId ((int) i + 1); } });
    return a;
}
