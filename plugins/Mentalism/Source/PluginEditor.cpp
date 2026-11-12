#include "PluginEditor.h"
#include "SceneUI.h"

using namespace suite::ui;

namespace
{
    const juce::Colour accent = Colours::verdigris;

    juce::String lufsText (float v) { return v <= -70.0f ? juce::String::fromUTF8 ("\xe2\x80\x93") : juce::String (v, 1); }
    juce::Colour diffColour (float d)
    {
        const auto a = std::abs (d);
        return a <= 1.5f ? Colours::moss : a <= advisor::tonalTolerance ? Colours::brass : Colours::ember;
    }
}

//==============================================================================
ChecklistPanel::ChecklistPanel (MentalismProcessor& p) : processor (p)
{
    fixButton.onClick = [this] { if (pendingFix != suite::Fix::None) sentSeq = processor.sendFix (pendingFix); refresh(); };
    addChildComponent (fixButton);
    reportButton.setTooltip ("Writes everything Mentalism knows about this mix (loudness, peaks, tone, arrangement, checklist, "
                             "fixes) to a text file in Antioch Suite/Mix reports");
    reportButton.onClick = [this]
    {
        const auto f = processor.saveMixReport();
        reportButton.setButtonText (f.existsAsFile() ? "Report saved" : "Couldn't save");
        if (f.existsAsFile()) f.revealToUser();
    };
    addAndMakeVisible (reportButton);
    refresh();
    setSize (640, 420);
    startTimerHz (2);
}

void ChecklistPanel::refresh()
{
    items = checklist::check (processor.report(), processor.clippedSamples.load(), suite::Registry::list());
    pendingFix = suite::Fix::None;
    for (const auto& i : items) if (i.fix != suite::Fix::None) pendingFix = i.fix;
    fixButton.setButtonText (sentSeq != 0 && suite::FixBus::acknowledgements (sentSeq) > 0
                                 ? "Fixed in " + juce::String (suite::FixBus::acknowledgements (sentSeq)) + " tracks (Undo in History)"
                                 : juce::String ("Fix: ") + coach::describe (pendingFix).fromFirstOccurrenceOf (": ", false, false));
    fixButton.setVisible (pendingFix != suite::Fix::None || sentSeq != 0);
    repaint();
}

void ChecklistPanel::resized()
{
    auto bottom = getLocalBounds().reduced (14, 10).removeFromBottom (30);
    reportButton.setBounds (bottom.removeFromRight (170));
    bottom.removeFromRight (8);
    fixButton.setBounds (bottom);
}

void ChecklistPanel::paint (juce::Graphics& g)
{
    using checklist::State;
    g.fillAll (Colours::panel);
    const auto ready = checklist::readyToExport (items);
    g.setColour (ready ? Colours::moss : Colours::brass);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText (ready ? "READY TO EXPORT" : "BEFORE YOU EXPORT", 14, 8, getWidth() - 28, 22, juce::Justification::centredLeft);
    int y = 40;
    for (const auto& item : items)
    {
        const auto colour = item.state == State::Pass ? Colours::moss : item.state == State::Warn ? Colours::brass
                          : item.state == State::Fail ? Colours::ember : Colours::dimText;
        const auto mark = item.state == State::Pass ? juce::String::fromUTF8 ("\xe2\x9c\x93") : item.state == State::Unknown ? juce::String ("?")
                        : item.state == State::Warn ? juce::String ("!") : juce::String::fromUTF8 ("\xe2\x9c\x97");
        g.setColour (colour);
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText (mark, 14, y, 20, 20, juce::Justification::centred);
        g.setColour (Colours::text);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (item.name, 40, y, 170, 20, juce::Justification::centredLeft);
        g.setColour (Colours::dimText);
        g.setFont (juce::FontOptions (13.0f));
        g.drawFittedText (item.detail, 212, y, getWidth() - 226, 20, juce::Justification::centredLeft, 1, 0.8f);
        y += 34;
    }
}

//==============================================================================
void FixHistoryPanel::refresh()
{
    auto latest = processor.getFixHistory();
    std::reverse (latest.begin(), latest.end());   // newest first
    const auto changed = latest.size() != records.size()
                      || ! std::equal (latest.begin(), latest.end(), records.begin(),
                                       [] (const auto& a, const auto& b) { return a.seq == b.seq && a.undoSeq == b.undoSeq; });
    records = latest;
    if (changed)
    {
        undoButtons.clear();
        for (const auto& r : records)
        {
            auto* b = undoButtons.add (new juce::TextButton ("Undo"));
            b->onClick = [this, seq = r.seq] { processor.undoFix (seq); refresh(); };
            addAndMakeVisible (b);
        }
        resized();
    }
    for (size_t i = 0; i < records.size() && (int) i < undoButtons.size(); ++i)
        undoButtons[(int) i]->setEnabled (records[i].undoSeq == 0 && suite::FixBus::acknowledgements (records[i].seq) > 0);
    repaint();
}

void FixHistoryPanel::resized()
{
    for (int i = 0; i < undoButtons.size(); ++i)
        undoButtons[i]->setBounds (getWidth() - 84, 36 + i * 30, 70, 24);
}

void FixHistoryPanel::paint (juce::Graphics& g)
{
    g.fillAll (Colours::panel);
    g.setColour (Colours::verdigris);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText ("FIX HISTORY (NEWEST FIRST)", 14, 8, getWidth() - 28, 20, juce::Justification::centredLeft);
    if (records.empty())
    {
        g.setColour (Colours::dimText);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText ("No fixes yet: use Fix in the Arrangement tab, or Rough mix.", 14, 40, getWidth() - 28, 20, juce::Justification::centredLeft);
        return;
    }
    for (size_t i = 0; i < records.size(); ++i)
    {
        const auto& r = records[i];
        const auto y = 36 + (int) i * 30;
        const auto applied = suite::FixBus::acknowledgements (r.seq);
        const auto undone = r.undoSeq != 0;
        g.setColour (Colours::dimText);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (r.when.formatted ("%H:%M"), 14, y, 44, 24, juce::Justification::centredLeft);
        g.setColour (undone ? Colours::dimText : Colours::text);
        g.setFont (juce::FontOptions (13.0f));
        const auto what = coach::describe (r.fix) + (undone ? juce::String ("  (undone)")
                                                  : applied > 0 ? "  (" + juce::String (applied) + (applied == 1 ? " plugin)" : " plugins)")
                                                                : juce::String ("  (nothing applied it)"));
        g.drawFittedText (what, 62, y, getWidth() - 62 - 96, 24, juce::Justification::centredLeft, 1, 0.8f);
    }
}

//==============================================================================
MentalismEditor::MentalismEditor (MentalismProcessor& p)
    : AudioProcessorEditor (p), processor (p),
      loudTarget (p.apvts, Params::loudnessTarget, "Loudness target"),
      tonalTarget (p.apvts, Params::tonalTarget, "Tonal target")
{
    setLookAndFeel (&lnf);

    resetButton.onClick = [this] { processor.resetMeasurements(); };
    roughMixButton.setTooltip ("Sets every Prima Materia track's Trim so it sits at the usual trance level for its role, "
                               "relative to the kick (measured in the drops). Play a drop first.");
    roughMixButton.onClick = [this]
    {
        lastFix = suite::Fix::RoughMix;
        lastFixSeq = processor.sendFix (suite::Fix::RoughMix);
        lastUndoSeq = 0;
        fixStatusTicks = 0;
    };
    roughUndoButton.onClick = [this] { undoFixButton.onClick(); };
    historyButton.setTooltip ("Every fix and rough mix so far, with Undo for each");
    historyButton.onClick = [this]
    {
        juce::CallOutBox::launchAsynchronously (std::make_unique<FixHistoryPanel> (processor), historyButton.getScreenBounds(), nullptr);
    };
    addAndMakeVisible (historyButton);
    likeMeButton.setTooltip ("How close this song is to the way you usually make trance: key, tempo, structure, length, loudness and tone");
    likeMeButton.onClick = [this]
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "Sounds like me?", processor.soundsLikeMe());
    };
    addAndMakeVisible (likeMeButton);
    for (int i = 0; i < (int) carveButtons.size(); ++i)
    {
        auto& b = carveButtons[(size_t) i];
        b.setButtonText ("Fix");
        b.setTooltip ("Carves a 3 dB dip in that band on the less important track (again: deeper). Undo in History.");
        b.setColour (juce::TextButton::buttonColourId, Colours::amethyst.withAlpha (0.35f));
        b.onClick = [this, i]
        {
            if (i >= (int) clashes.size()) return;
            lastFix = suite::Fix::Carve;
            lastFixSeq = processor.sendFix (suite::Fix::Carve, suite::packCarve (clashes[(size_t) i].carveId, clashes[(size_t) i].band));
            lastUndoSeq = 0;
            fixStatusTicks = 0;
        };
        addChildComponent (b);
    }
    checklistButton.setTooltip ("Pass/fail list to check before rendering: loudness, true peak, clipping, mono, sections");
    checklistButton.onClick = [this]
    {
        juce::CallOutBox::launchAsynchronously (std::make_unique<ChecklistPanel> (processor), checklistButton.getScreenBounds(), nullptr);
    };
    addAndMakeVisible (checklistButton);
    autoCoach = std::make_unique<suite::ui::Toggle> (p.apvts, Params::autoCoach, "Auto-coach", Colours::ember);
    autoCoach->setTooltip ("After every complete play-through, applies the coach's fixes by itself (up to 3 passes) until it's happy");
    finishButton.setTooltip ("Gain staging, DJ intro/outro, reference tone, then the rough mix once the drop is heard again, "
                             "then the auto-coach until the energy arc is right. Everything can be undone in History.");
    finishButton.setColour (juce::TextButton::buttonColourId, Colours::moss.withAlpha (0.4f));
    finishButton.onClick = [this]
    {
        juce::PopupMenu m;
        m.addItem (1, "Finish the song (gain staging, rough mix, coaching)");
        m.addItem (2, "Mix it: everything automatic (also masking, mud, harshness and translation)");
        m.addItem (3, "Undo Mix it");
        m.addSeparator();
        m.addItem (4, "Translation check (phone, earbuds, mono)...");
        m.addItem (5, "Last Mix it report...", processor.mixItReport().isNotEmpty());
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&finishButton), [this] (int id)
        {
            if (id == 1) processor.finishSong();
            if (id == 2) processor.mixIt();
            if (id == 3) processor.undoMixIt();
            if (id == 4) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "Translation check", processor.translationCheck().joinIntoString ("\n\n"));
            if (id == 5) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "Mix it", processor.mixItReport());
        });
    };
    automationStatus.setFont (juce::FontOptions (12.0f));
    automationStatus.setJustificationType (juce::Justification::centredLeft);
    automationStatus.setColour (juce::Label::textColourId, Colours::moss);
    autoMasking = std::make_unique<suite::ui::Toggle> (p.apvts, Params::autoMasking, "Resolve masking", Colours::ember);
    autoMasking->setTooltip ("Clashes that last over a minute are resolved by themselves: the track that matters less gives way in that band, "
                             "only while the vocal or lead it clashes with plays (undo in History)");
    problemWatch = std::make_unique<suite::ui::Toggle> (p.apvts, Params::problemWatch, "Watch", Colours::ember);
    problemWatch->setTooltip ("The problem watcher: clipping tracks, a buried vocal and lasting masking are fixed by themselves; "
                              "mono, phase and session problems are offered under Problems. Everything shows in What changed (Ouroboros).");
    problemsButton.setTooltip ("Problems the watcher found that need your OK: click one to fix it");
    problemsButton.onClick = [this]
    {
        juce::PopupMenu m;
        const auto list = processor.problems();
        if (list.empty()) m.addItem (1, "No problems right now", false);
        for (size_t i = 0; i < list.size(); ++i)
            m.addItem (100 + (int) i, list[i].text + (list[i].fix != suite::Fix::None ? "  ->  " + list[i].fixText : "  (" + list[i].fixText + ")"),
                       list[i].fix != suite::Fix::None);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&problemsButton), [this, list] (int id)
        {
            if (id >= 100 && id - 100 < (int) list.size()) processor.applyProblemFix (list[(size_t) (id - 100)].key);
        });
    };
    addAndMakeVisible (problemsButton);
    addAndMakeVisible (*problemWatch);
    autoBalance = std::make_unique<suite::ui::Toggle> (p.apvts, Params::autoBalance, "Balance", Colours::ember);
    autoBalance->setTooltip ("Every 4 s of music, each track that's off its rough-mix balance moves 1 dB towards it; the step is kept "
                             "only if the master didn't move away from its tonal target (see What changed in Ouroboros)");
    for (auto* c : std::initializer_list<juce::Component*> { autoCoach.get(), autoMasking.get(), autoBalance.get(), &finishButton, &automationStatus })
        addAndMakeVisible (c);
    matchButton.setTooltip ("Moves Lapis' low and high tone half-way towards the reference (at most 4 dB) and sets the loudness "
                            "target to match it. Undo it in History.");
    matchButton.onClick = [this]
    {
        matchSeq = processor.matchReference();
        const auto l = processor.lastMatchLow.load(), h = processor.lastMatchHigh.load();
        auto fmt = [] (float v) { return (v >= 0.0f ? "+" : "") + juce::String (v, 1); };
        matchButton.setButtonText (matchSeq != 0 ? "Matched: low " + fmt (l) + ", high " + fmt (h) + " dB"
                                   : processor.getReference().valid ? juce::String ("Tone already close; loudness matched")
                                                                    : juce::String ("Load a reference first"));
    };
    roughStatus.setFont (juce::FontOptions (12.0f));
    roughStatus.setJustificationType (juce::Justification::centredRight);
    for (auto* c : std::initializer_list<juce::Component*> { &roughMixButton, &roughUndoButton, &roughStatus })
        addAndMakeVisible (c);
    loadButton.onClick = [this]
    {
        juce::PopupMenu m;
        m.addItem (1, "Choose a reference track...");
        m.addItem (2, "Pick the closest from a folder... (tone, and key and tempo in the file names)");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&loadButton), [this] (int choice)
        {
            if (choice == 0) return;
            chooser = std::make_unique<juce::FileChooser> (choice == 1 ? "Choose a reference track" : "Choose a folder of reference tracks",
                                                           juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                           choice == 1 ? "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3" : "*");
            const auto flags = juce::FileBrowserComponent::openMode
                             | (choice == 1 ? juce::FileBrowserComponent::canSelectFiles : juce::FileBrowserComponent::canSelectDirectories);
            chooser->launchAsync (flags, [this, choice] (const juce::FileChooser& fc)
            {
                if (choice == 1 && fc.getResult().existsAsFile()) processor.loadReference (fc.getResult());
                if (choice == 2 && fc.getResult().isDirectory()) processor.autoReference (fc.getResult());
            });
        });
    };

    mySoundButton.setTooltip ("Choose a few of your finished tracks: their average loudness, ceiling and tonal balance "
                              "become the 'My sound' targets, in every project");
    mySoundButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose your finished tracks", juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                       "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectMultipleItems,
                              [this] (const juce::FileChooser& fc)
                              {
                                  if (fc.getResults().isEmpty()) return;
                                  processor.learnMySound (fc.getResults());
                                  setParameterFromEditor (Params::loudnessTarget, (float) advisor::mySoundIndex);
                                  setParameterFromEditor (Params::tonalTarget, (float) advisor::mySoundTonalIndex);
                              });
    };
    referenceLabel.setFont (juce::FontOptions (12.0f));
    referenceLabel.setJustificationType (juce::Justification::centredLeft);

    compareAB = std::make_unique<suite::ui::Toggle> (p.apvts, Params::compareAB, "A/B", Colours::brass);
    compareAB->setTooltip ("Hear the reference's loudest 20 s at your mix's loudness, to compare sound, not level "
                           "(Mentalism must be last on the master). Automate it or map it to a key to flip quickly.");
    for (auto* c : std::initializer_list<juce::Component*> { &loudTarget, &tonalTarget, &resetButton, &loadButton, &referenceLabel, &balanceTabs, &matchButton, &mySoundButton,
                                                             static_cast<juce::Component*> (compareAB.get()) })
        addAndMakeVisible (c);
    balanceTabs.setComponentID ("tabs");
    balanceTabs.onChange = [this] (int i)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &tonalTarget, &loadButton, &referenceLabel, &matchButton, &mySoundButton,
                                                                 static_cast<juce::Component*> (compareAB.get()) })
            c->setVisible (i == 0);
        undoFixButton.setVisible (i == 1 || i == 2);
        fixStatus.setVisible (i == 1 || i == 2);
        layoutFixButtons();
        updateCarveButtons();
        repaint();
    };

    for (int i = 0; i < (int) fixButtons.size(); ++i)
    {
        auto& b = fixButtons[(size_t) i];
        b.setButtonText ("Fix");
        b.setColour (juce::TextButton::buttonColourId, Colours::ember.withAlpha (0.35f));
        b.onClick = [this, i] { sendFix (i); };
        addChildComponent (b);
    }
    undoFixButton.onClick = [this]
    {
        if (lastFixSeq == 0) return;
        lastUndoSeq = processor.undoFix (lastFixSeq);
        fixStatusTicks = 0;
    };
    undoFixButton.setEnabled (false);
    fixStatus.setFont (juce::FontOptions (12.0f));
    fixStatus.setJustificationType (juce::Justification::centredLeft);
    addChildComponent (undoFixButton);
    addChildComponent (fixStatus);

    setSize (1040, 720);
    timerCallback();
    startTimerHz (10);
}

MentalismEditor::~MentalismEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void MentalismEditor::setParameterFromEditor (const juce::String& id, float plainValue)
{
    if (auto* p = processor.apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
        p->endChangeGesture();
    }
}

void MentalismEditor::sendFix (int row)
{
    const auto fix = shownFixes[(size_t) row];
    if (fix == suite::Fix::None) return;
    lastFix = fix;
    lastFixSeq = processor.applyCoachFix (fix, shownFixData[(size_t) row], rep.map);   // the fixed sections are heard again before re-judging
    lastUndoSeq = 0;
    fixStatusTicks = 0;
}

void MentalismEditor::updateFixStatus()
{
    ++fixStatusTicks;
    if (lastFixSeq == 0)
    {
        fixStatus.setColour (juce::Label::textColourId, Colours::dimText);
        fixStatus.setText ("Fix applies a small step in the plugins that can help; click again if it needs more.", juce::dontSendNotification);
        undoFixButton.setEnabled (false);
        roughUndoButton.setEnabled (false);
        return;
    }
    if (lastUndoSeq != 0)
    {
        const auto n = suite::FixBus::acknowledgements (lastUndoSeq);
        fixStatus.setColour (juce::Label::textColourId, Colours::text);
        fixStatus.setText (n > 0 ? "Undone in " + juce::String (n) + (n == 1 ? " plugin." : " plugins.") : "Undoing...", juce::dontSendNotification);
        undoFixButton.setEnabled (false);
        roughUndoButton.setEnabled (false);
        if (lastFix == suite::Fix::RoughMix)
            roughStatus.setText (n > 0 ? "Rough mix undone" : "Undoing...", juce::dontSendNotification);
        return;
    }
    const auto n = suite::FixBus::acknowledgements (lastFixSeq);
    undoFixButton.setEnabled (n > 0);
    roughUndoButton.setEnabled (n > 0 && lastFix == suite::Fix::RoughMix);
    if (lastFix == suite::Fix::RoughMix)
    {
        roughStatus.setColour (juce::Label::textColourId, n > 0 ? Colours::moss : fixStatusTicks > 10 ? Colours::ember : Colours::dimText);
        roughStatus.setText (n > 0 ? "Rough mix: " + juce::String (n) + (n == 1 ? " track" : " tracks") + " trimmed"
                             : fixStatusTicks > 10 ? "Rough mix: no changes (needs Prima Materia with roles, including a Kick, and a drop played)"
                                                   : "Rough mix...", juce::dontSendNotification);
    }
    if (n > 0)
    {
        fixStatus.setColour (juce::Label::textColourId, Colours::moss);
        fixStatus.setText ("Applied in " + juce::String (n) + (n == 1 ? " plugin" : " plugins") + " (" + coach::describe (lastFix)
                           + "). Play the section again to re-check.", juce::dontSendNotification);
    }
    else if (fixStatusTicks > 10)
    {
        fixStatus.setColour (juce::Label::textColourId, Colours::ember);
        fixStatus.setText ("Nothing could apply it. It needs " + coach::describe (lastFix).upToFirstOccurrenceOf (":", false, false)
                           + " in the project.", juce::dontSendNotification);
    }
    else
        fixStatus.setText ("Applying...", juce::dontSendNotification);
}

void MentalismEditor::layoutFixButtons()
{
    const auto show = balanceTabs.getSelected() == 1 && rep.arrangementAvailable && rep.map.count > 0;
    auto notes = arrangementNotes;
    for (int i = 0; i < (int) fixButtons.size(); ++i)
    {
        auto row = notes.removeFromTop (21);
        const auto fix = i < rep.arrangement.problems && i < (int) rep.arrangement.fixes.size() ? rep.arrangement.fixes[(size_t) i] : suite::Fix::None;
        shownFixes[(size_t) i] = fix;
        shownFixData[(size_t) i] = i < (int) rep.arrangement.fixData.size() ? rep.arrangement.fixData[(size_t) i] : 0u;
        auto& b = fixButtons[(size_t) i];
        b.setBounds (row.removeFromRight (56).reduced (2, 1));
        b.setTooltip (coach::describe (fix));
        b.setVisible (show && fix != suite::Fix::None);
    }
}

void MentalismEditor::timerCallback()
{
    {
        const auto n = (int) processor.problems().size();
        problemsButton.setButtonText (n > 0 ? "Problems (" + juce::String (n) + ")" : "Problems");
        problemsButton.setColour (juce::TextButton::textColourOffId, n > 0 ? Colours::ember : Colours::text);
    }
    rep = processor.report();
    {
        const auto finish = processor.finishStatus(), coachText = processor.autoCoachStatus();
        automationStatus.setText (processor.finishStage() != MentalismProcessor::FinishStage::Idle ? finish
                                  : processor.apvts.getRawParameterValue (Params::autoCoach)->load() > 0.5f
                                        ? (coachText.isNotEmpty() ? coachText : juce::String ("Auto-coach on: play the whole song through."))
                                        : juce::String(), juce::dontSendNotification);
    }
    clashes = masking::find (suite::Registry::list());
    updateCarveButtons();
    layoutFixButtons();
    updateFixStatus();
    const auto ref = processor.getReference();
    referenceLabel.setColour (juce::Label::textColourId, ref.valid ? accent : Colours::dimText);
    referenceLabel.setText (processor.isLoadingReference() ? "Analysing reference..."
                            : ref.valid ? "Reference: " + ref.name + "  (" + juce::String (ref.lufs, 1) + " LUFS, "
                                          + juce::String (ref.truePeakDb, 1) + " dBTP)"
                                        : "No reference loaded",
                            juce::dontSendNotification);
    {
        const auto mine = processor.getMySound();
        mySoundButton.setButtonText (processor.isLearningMySound() ? "Learning..." : mine.valid ? "My sound: " + juce::String (mine.tracks) + " tracks"
                                                                                               : juce::String ("Learn my sound..."));
    }
    repaint();
}

void MentalismEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);
    drawHeader (g, { 20, 12, 700, 32 }, "MENTALISM", "mix assistant", accent);
    drawPanel (g, loudArea, "Loudness", accent);
    drawPanel (g, stereoArea, "Stereo", Colours::amethyst);
    const auto arrangementTab = balanceTabs.getSelected() == 1, maskingTab = balanceTabs.getSelected() == 2;
    drawPanel (g, balanceArea, arrangementTab ? "Arrangement: loudness per section (Ouroboros)"
                               : maskingTab ? "Masking: tracks fighting for the same frequencies in the drops"
                                            : "Tonal balance vs target  (bars = whole song, line = last 3 s)",
               arrangementTab ? Colours::ember : maskingTab ? Colours::amethyst : Colours::brass);
    drawPanel (g, adviceArea, "Suggestions", Colours::ember);

    paintLoudness (g, loudContent);
    paintStereo (g, stereoContent);
    if (arrangementTab)  paintArrangement (g, arrangementContent);
    else if (maskingTab) paintMasking (g, arrangementContent);
    else                 paintBalance (g, balanceContent);
    paintAdvice (g, adviceContent);
}

void MentalismEditor::paintLoudness (juce::Graphics& g, juce::Rectangle<int> area)
{
    auto numbers = area.removeFromTop (78);
    const auto& m = rep.measurement;
    const std::pair<const char*, float> items[] = { { "Momentary", rep.momentary }, { "Short-term", rep.shortTerm },
                                                    { "Integrated", m.integratedLufs } };
    const auto w = numbers.getWidth() / 4;
    for (auto& [label, v] : items)
    {
        auto cell = numbers.removeFromLeft (w);
        g.setColour (Colours::dimText);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (label, cell.removeFromTop (16), juce::Justification::centred);
        g.setColour (juce::String (label) == "Integrated" ? accent : Colours::text);
        g.setFont (juce::FontOptions (30.0f, juce::Font::bold));
        g.drawText (lufsText (v), cell.removeFromTop (36), juce::Justification::centred);
        g.setColour (Colours::dimText);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("LUFS", cell, juce::Justification::centredTop);
    }
    {
        auto cell = numbers;
        const auto over = m.truePeakDb > rep.ceiling;
        g.setColour (Colours::dimText);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("True peak", cell.removeFromTop (16), juce::Justification::centred);
        g.setColour (over ? Colours::ember : Colours::text);
        g.setFont (juce::FontOptions (30.0f, juce::Font::bold));
        g.drawText (m.truePeakDb <= -99.0f ? juce::String::fromUTF8 ("\xe2\x80\x93") : juce::String (m.truePeakDb, 1),
                    cell.removeFromTop (36), juce::Justification::centred);
        g.setColour (Colours::dimText);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("dBTP", cell, juce::Justification::centredTop);
    }

    // Gauge: integrated loudness against the target (+/- 10 LU).
    area.removeFromTop (4);
    auto gauge = area.removeFromTop (22).toFloat().reduced (8.0f, 4.0f);
    g.setColour (Colours::outline);
    g.fillRoundedRectangle (gauge, 4.0f);
    auto xFor = [&] (float lu) { return gauge.getX() + gauge.getWidth() * juce::jlimit (0.0f, 1.0f, (lu + 10.0f) / 20.0f); };
    g.setColour (Colours::moss.withAlpha (0.35f));
    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xFor (-1.0f), gauge.getY(), xFor (1.0f), gauge.getBottom()));
    g.setColour (Colours::text);
    g.drawVerticalLine ((int) xFor (0.0f), gauge.getY() - 3.0f, gauge.getBottom() + 3.0f);
    if (m.integratedLufs > -70.0f)
    {
        const auto x = xFor (m.integratedLufs - rep.targetLufs);
        g.setColour (std::abs (m.integratedLufs - rep.targetLufs) <= 1.0f ? Colours::moss : Colours::brass);
        g.fillEllipse (x - 6.0f, gauge.getCentreY() - 6.0f, 12.0f, 12.0f);
    }
    g.setColour (Colours::dimText);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("-10 LU", area.removeFromTop (14).withTrimmedLeft (8), juce::Justification::left);
    g.drawText ("target " + juce::String (rep.targetLufs, 1) + " LUFS / " + juce::String (rep.ceiling, 1) + " dBTP  +10 LU",
                area.withTrimmedRight (8).withY (area.getY() - 14).withHeight (14), juce::Justification::right);
}

void MentalismEditor::paintStereo (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto& m = rep.measurement;
    auto meter = [&] (juce::Rectangle<int> r, const juce::String& label, float corr, float warnBelow)
    {
        g.setColour (Colours::dimText);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (label + "   " + juce::String (corr, 2), r.removeFromTop (16), juce::Justification::left);
        auto bar = r.removeFromTop (14).toFloat();
        g.setColour (Colours::outline);
        g.fillRoundedRectangle (bar, 3.0f);
        const auto centre = bar.getCentreX();
        const auto x = bar.getX() + bar.getWidth() * (corr + 1.0f) * 0.5f;
        g.setColour (corr < warnBelow ? Colours::ember : Colours::moss);
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (std::min (centre, x), bar.getY(), std::max (centre, x), bar.getBottom()));
        g.setColour (Colours::text.withAlpha (0.5f));
        g.drawVerticalLine ((int) centre, bar.getY(), bar.getBottom());
        g.setColour (Colours::dimText);
        g.setFont (juce::FontOptions (10.0f));
        g.drawText ("-1", r.removeFromTop (12), juce::Justification::left);
        g.drawText ("+1", r.withY (r.getY() - 12).withHeight (12), juce::Justification::right);
    };

    meter (area.removeFromTop (46), "Correlation (full)", m.correlation, 0.2f);
    area.removeFromTop (6);
    meter (area.removeFromTop (46), "Correlation below 120 Hz", m.lowCorrelation, 0.8f);
    area.removeFromTop (6);
    g.setColour (m.monoLossDb > 3.0f ? Colours::ember : Colours::dimText);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("Mono sum: " + juce::String (-m.monoLossDb, 1) + " dB vs stereo", area.removeFromTop (18), juce::Justification::left);
}

void MentalismEditor::paintBalance (juce::Graphics& g, juce::Rectangle<int> area)
{
    auto labels = area.removeFromBottom (18);
    const auto r = area.toFloat();
    g.setColour (Colours::background);
    g.fillRoundedRectangle (r, 6.0f);

    if (! rep.tonalAvailable)
    {
        g.setColour (Colours::dimText);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText ("Load a reference track to compare against it", r, juce::Justification::centred);
        return;
    }

    constexpr float range = 12.0f; // +/- dB shown
    auto yFor = [&] (float d) { return r.getCentreY() - r.getHeight() * 0.5f * juce::jlimit (-1.0f, 1.0f, d / range); };

    // Tolerance zone and grid
    g.setColour (Colours::moss.withAlpha (0.08f));
    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (r.getX(), yFor (advisor::tonalTolerance), r.getRight(), yFor (-advisor::tonalTolerance)));
    g.setColour (Colours::outline);
    for (float d : { -9.0f, -6.0f, -3.0f, 3.0f, 6.0f, 9.0f })
        g.drawHorizontalLine ((int) yFor (d), r.getX(), r.getRight());
    g.setColour (Colours::dimText.withAlpha (0.6f));
    g.drawHorizontalLine ((int) yFor (0.0f), r.getX(), r.getRight());
    g.setFont (juce::FontOptions (10.0f));
    for (float d : { -6.0f, 6.0f })
        g.drawText ((d > 0 ? "+" : "") + juce::String ((int) d) + " dB", juce::Rectangle<float> (r.getX() + 4.0f, yFor (d) - 12.0f, 50.0f, 12.0f),
                    juce::Justification::left);

    const auto colW = r.getWidth() / (float) analysis::numBands;
    const auto haveLong = rep.measuredSeconds >= 2.0f;
    for (int b = 0; b < analysis::numBands; ++b)
    {
        const auto x = r.getX() + colW * (float) b;
        if (haveLong)
        {
            const auto d = rep.longDiff[(size_t) b];
            g.setColour (diffColour (d).withAlpha (0.8f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x + colW * 0.25f, std::min (yFor (0.0f), yFor (d)),
                                                                  x + colW * 0.75f, std::max (yFor (0.0f), yFor (d))));
        }
        const auto s = rep.shortDiff[(size_t) b];
        if (rep.measuredSeconds > 0.0f)
        {
            g.setColour (Colours::text);
            g.drawLine (x + colW * 0.15f, yFor (s), x + colW * 0.85f, yFor (s), 2.0f);
        }

        const auto hz = analysis::bandCentres()[(size_t) b];
        g.setColour (Colours::dimText);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (hz >= 1000.0f ? juce::String ((int) (hz / 1000.0f)) + "k" : juce::String ((int) hz),
                    juce::Rectangle<int> ((int) x, labels.getY(), (int) colW, labels.getHeight()), juce::Justification::centred);
    }

    g.setColour (Colours::dimText);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("measured " + juce::String ((int) rep.measuredSeconds) + " s of audio", r.reduced (8.0f, 4.0f),
                juce::Justification::topRight);
}

void MentalismEditor::paintArrangement (juce::Graphics& g, juce::Rectangle<int> area)
{
    if (! rep.arrangementAvailable || rep.map.count == 0)
    {
        g.setColour (Colours::dimText);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText ("Load Ouroboros and play the song to see loudness per section", area, juce::Justification::centred);
        return;
    }

    auto notes = area.removeFromBottom (juce::jmin (88, area.getHeight() / 3));
    notes.removeFromRight (60);   // room for the Fix buttons
    const auto chart = area.toFloat();
    g.setColour (Colours::background);
    g.fillRoundedRectangle (chart, 6.0f);

    // Loudest heard section sets the scale: bars show -24 LU .. 0 LU relative to it.
    float top = -100.0f;
    for (int i = 0; i < rep.map.count; ++i) if (rep.sections[(size_t) i].heard()) top = std::max (top, rep.sections[(size_t) i].lufs());
    const auto total = (float) juce::jmax (1, rep.map.totalBars);
    for (int i = 0; i < rep.map.count; ++i)
    {
        const auto& sec = rep.map.sections[(size_t) i];
        const auto x0 = chart.getX() + chart.getWidth() * (float) (sec.startBar - 1) / total;
        const auto x1 = chart.getX() + chart.getWidth() * (float) (rep.map.endBar (i) - 1) / total;
        const auto colour = suite::ui::sectionColour (sec.type);
        const auto& st = rep.sections[(size_t) i];
        auto column = juce::Rectangle<float>::leftTopRightBottom (x0, chart.getY(), x1, chart.getBottom()).reduced (2.0f, 4.0f);

        if (st.heard())
        {
            const auto rel = juce::jlimit (0.0f, 1.0f, (st.lufs() - (top - 24.0f)) / 24.0f);
            g.setColour (colour.withAlpha (0.55f));
            g.fillRoundedRectangle (column.withTop (column.getBottom() - column.getHeight() * 0.85f * rel), 3.0f);
            g.setColour (Colours::text);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (juce::String (st.lufs(), 1), column.withHeight (16.0f).translated (0.0f, 2.0f), juce::Justification::centred);
        }
        else
        {
            g.setColour (colour.withAlpha (0.12f));
            g.fillRoundedRectangle (column, 3.0f);
        }
        g.setColour (colour);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawFittedText (juce::String (suite::sectionName (sec.type)).toUpperCase(), column.removeFromBottom (14.0f).toNearestInt(),
                          juce::Justification::centred, 1, 0.5f);
    }

    g.setFont (juce::FontOptions (12.5f));
    for (int i = 0; i < juce::jmin (4, rep.arrangement.notes.size()); ++i)
    {
        auto row = notes.removeFromTop (21);
        const auto problem = i < rep.arrangement.problems;
        g.setColour (problem ? Colours::ember : Colours::moss);
        g.fillEllipse ((float) row.getX() + 2.0f, (float) row.getCentreY() - 3.0f, 6.0f, 6.0f);
        g.setColour (Colours::text);
        g.drawFittedText (rep.arrangement.notes[i], row.withTrimmedLeft (14), juce::Justification::centredLeft, 1, 0.8f);
    }
}

void MentalismEditor::updateCarveButtons()
{
    auto rows = arrangementContent.reduced (4, 2);
    for (int i = 0; i < (int) carveButtons.size(); ++i)
    {
        auto row = rows.removeFromTop (34);
        auto& b = carveButtons[(size_t) i];
        b.setBounds (row.removeFromRight (80).reduced (2, 4));
        b.setVisible (balanceTabs.getSelected() == 2 && i < (int) clashes.size());
    }
}

void MentalismEditor::paintMasking (juce::Graphics& g, juce::Rectangle<int> area)
{
    if (clashes.empty())
    {
        g.setColour (Colours::dimText);
        g.setFont (juce::FontOptions (14.0f));
        g.drawFittedText ("No clashes found. Put Prima Materia (with roles) on your tracks and play a drop: "
                          "each track shares its spectrum, and tracks fighting for the same band show up here.",
                          area.reduced (20), juce::Justification::centred, 3);
        return;
    }
    g.setFont (juce::FontOptions (13.5f));
    auto rows = area.reduced (4, 2);
    for (size_t i = 0; i < clashes.size(); ++i)
    {
        auto row = rows.removeFromTop (34);
        g.setColour (Colours::amethyst);
        g.fillEllipse ((float) row.getX() + 2.0f, (float) row.getCentreY() - 3.0f, 6.0f, 6.0f);
        g.setColour (Colours::text);
        g.drawFittedText (clashes[i].text, row.withTrimmedLeft (16).withTrimmedRight (90), juce::Justification::centredLeft, 1, 0.8f);
    }
}

void MentalismEditor::paintAdvice (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setFont (juce::FontOptions (13.0f));
    for (const auto& line : rep.advice)
    {
        if (area.getHeight() < 22) break;   // only whole lines
        auto row = area.removeFromTop (22);
        g.setColour (Colours::ember);
        g.fillEllipse ((float) row.getX() + 2.0f, (float) row.getCentreY() - 3.0f, 6.0f, 6.0f);
        g.setColour (Colours::text);
        g.drawFittedText (line, row.withTrimmedLeft (16), juce::Justification::centredLeft, 1, 0.85f);
    }
}

void MentalismEditor::resized()
{
    auto bounds = getLocalBounds().reduced (16);
    auto header = bounds.removeFromTop (48);
    resetButton.setBounds (header.removeFromRight (110).reduced (4, 8));
    roughUndoButton.setBounds (header.removeFromRight (70).reduced (4, 8));
    roughMixButton.setBounds (header.removeFromRight (110).reduced (4, 8));
    historyButton.setBounds (header.removeFromRight (90).reduced (4, 8));
    likeMeButton.setBounds (header.removeFromRight (90).reduced (4, 8));
    roughStatus.setBounds (header.removeFromRight (190).reduced (4, 8));

    auto top = bounds.removeFromTop (190);
    loudArea = top.removeFromLeft (top.getWidth() * 62 / 100);
    top.removeFromLeft (12);
    stereoArea = top;
    bounds.removeFromTop (12);

    adviceArea = bounds.removeFromBottom (170);
    bounds.removeFromBottom (12);
    balanceArea = bounds;

    auto l = loudArea.reduced (12, 6).withTrimmedTop (14);
    loudTarget.setBounds (l.removeFromRight (180).withHeight (60));
    loudContent = l;

    stereoContent = stereoArea.reduced (14, 8).withTrimmedTop (18);

    auto b = balanceArea.reduced (12, 6).withTrimmedTop (14);
    auto controls = b.removeFromTop (54);
    tonalTarget.setBounds (controls.removeFromLeft (220));
    loadButton.setBounds (controls.removeFromLeft (150).withTrimmedTop (18).reduced (4, 2));
    matchButton.setBounds (controls.removeFromLeft (200).withTrimmedTop (18).reduced (4, 2));
    mySoundButton.setBounds (controls.removeFromLeft (150).withTrimmedTop (18).reduced (4, 2));
    compareAB->setBounds (controls.removeFromLeft (70).withTrimmedTop (18).reduced (4, 2));
    referenceLabel.setBounds (controls.withTrimmedTop (18).withTrimmedLeft (8));
    b.removeFromTop (6);
    balanceContent = b;
    arrangementContent = balanceArea.reduced (12, 6).withTrimmedTop (60);
    arrangementNotes = arrangementContent.withTop (arrangementContent.getBottom() - juce::jmin (88, arrangementContent.getHeight() / 3));
    {
        auto c = balanceArea.reduced (12, 6).withTrimmedTop (14).removeFromTop (54).withTrimmedTop (18);
        undoFixButton.setBounds (c.removeFromLeft (130).reduced (0, 4));
        fixStatus.setBounds (c.withTrimmedLeft (8));
    }
    layoutFixButtons();
    balanceTabs.setBounds (balanceArea.getRight() - 522, balanceArea.getY() + 6, 510, 28);

    adviceContent = adviceArea.reduced (14, 8).withTrimmedTop (18);
    checklistButton.setBounds (adviceArea.getRight() - 162, adviceArea.getY() + 6, 150, 26);
    finishButton.setBounds (adviceArea.getRight() - 324, adviceArea.getY() + 6, 150, 26);
    autoCoach->setBounds (adviceArea.getRight() - 456, adviceArea.getY() + 6, 124, 26);
    autoMasking->setBounds (adviceArea.getRight() - 608, adviceArea.getY() + 6, 146, 26);
    autoBalance->setBounds (adviceArea.getRight() - 704, adviceArea.getY() + 6, 90, 26);
    problemWatch->setBounds (adviceArea.getRight() - 788, adviceArea.getY() + 6, 78, 26);
    problemsButton.setBounds (adviceArea.getRight() - 900, adviceArea.getY() + 6, 106, 26);
    automationStatus.setBounds (adviceContent.removeFromBottom (20));
}
