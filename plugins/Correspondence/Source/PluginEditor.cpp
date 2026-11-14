#include "PluginEditor.h"
#include "Audition.h"
#include "Drums.h"
#include "Identity.h"

using namespace suite::ui;

namespace
{
    const juce::Colour accent = Colours::verdigris;

    juce::File dragFolder()
    {
        return juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Correspondence");
    }
}

//==============================================================================
namespace { std::unique_ptr<juce::Component> makeSongGrid (juce::AudioProcessorValueTreeState&, std::function<void (juce::Component&)> writeStronger, std::function<void (juce::Component&, int)> writeForLabel); }

DragTile::DragTile (CorrespondenceProcessor& p, juce::String k, juce::Colour c)
    : processor (p), kind (std::move (k)), colour (c)
{
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

void DragTile::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (colour.withAlpha (dragging ? 0.35f : 0.15f));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (colour);
    g.drawRoundedRectangle (r, 8.0f, 1.5f);

    // Grip dots
    g.setColour (colour.withAlpha (0.7f));
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 2; ++col)
            g.fillEllipse (r.getX() + 12.0f + (float) col * 7.0f, r.getCentreY() - 9.0f + (float) row * 7.0f, 3.5f, 3.5f);

    g.setColour (Colours::text);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawFittedText (kind.toUpperCase(), r.withTrimmedLeft (26.0f).withTrimmedBottom (r.getHeight() * 0.4f).toNearestInt(),
                      juce::Justification::centred, 1, 0.7f);
    g.setColour (unavailable ? Colours::ember : Colours::dimText);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (unavailable ? "needs Ouroboros" : "drag into FL", r.withTrimmedLeft (26.0f).withTrimmedTop (r.getHeight() * 0.5f),
                juce::Justification::centred);
}

void DragTile::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 5)
        return;

    const auto file = processor.exportClip (kind, dragFolder());
    unavailable = ! file.existsAsFile();   // song clips need an Ouroboros map
    if (unavailable)
    {
        repaint();
        return;
    }
    dragging = true;
    repaint();
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this,
                                                                [safe = juce::Component::SafePointer<DragTile> (this)]
                                                                {
                                                                    if (safe != nullptr) { safe->dragging = false; safe->repaint(); }
                                                                });
}

void DragTile::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
    repaint();
}

//==============================================================================
void RollView::setData (std::vector<std::vector<int>> chordsIn, std::vector<int> bassIn, int countIn, int highlightIn,
                        std::vector<chords::Note> melodyIn, double quartersPerChordIn)
{
    auto sameMelody = melodyIn.size() == melodyNotes.size();
    for (size_t i = 0; sameMelody && i < melodyIn.size(); ++i)
        sameMelody = melodyIn[i].pitch == melodyNotes[i].pitch && std::abs (melodyIn[i].start - melodyNotes[i].start) < 1.0e-9;
    if (chordsIn == voiced && bassIn == bass && countIn == count && highlightIn == highlight && sameMelody)
        return;
    voiced = std::move (chordsIn);
    bass = std::move (bassIn);
    count = countIn;
    highlight = highlightIn;
    melodyNotes = std::move (melodyIn);
    quartersPerChord = quartersPerChordIn;
    repaint();
}

void RollView::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (Colours::background);
    g.fillRoundedRectangle (r, 6.0f);

    int lo = 127, hi = 0;
    for (const auto& c : voiced) for (auto p : c) { lo = std::min (lo, p); hi = std::max (hi, p); }
    for (auto p : bass) { lo = std::min (lo, p); hi = std::max (hi, p); }
    for (auto& n : melodyNotes) { lo = std::min (lo, n.pitch); hi = std::max (hi, n.pitch); }
    if (lo > hi) return;
    lo -= 2; hi += 2;

    const auto rowH = r.getHeight() / (float) (hi - lo + 1);
    const auto colW = r.getWidth() / (float) chords::maxSlots; // aligned with the slot columns above

    // Black-key shading for orientation
    for (int p = lo; p <= hi; ++p)
        if (juce::MidiMessage::isMidiNoteBlack (p))
        {
            g.setColour (Colours::panel.withAlpha (0.6f));
            g.fillRect (r.getX(), r.getBottom() - (float) (p - lo + 1) * rowH, r.getWidth(), rowH);
        }

    for (int i = 0; i < count; ++i)
    {
        const auto x = r.getX() + (float) i * colW;
        if (i == highlight)
        {
            g.setColour (accent.withAlpha (0.12f));
            g.fillRect (x, r.getY(), colW, r.getHeight());
        }
        g.setColour (Colours::outline);
        if (i > 0) g.drawVerticalLine ((int) x, r.getY(), r.getBottom());

        if (i < (int) voiced.size())
            for (auto p : voiced[(size_t) i])
            {
                g.setColour (i == highlight ? accent : accent.withAlpha (0.65f));
                g.fillRoundedRectangle (x + 4.0f, r.getBottom() - (float) (p - lo + 1) * rowH + 1.0f, colW - 8.0f, juce::jmax (5.0f, rowH - 1.0f), 2.0f);
            }

        if (i < (int) bass.size())
        {
            const auto p = bass[(size_t) i];
            g.setColour (Colours::brass.withAlpha (0.8f));
            g.fillRoundedRectangle (x + 4.0f, r.getBottom() - (float) (p - lo + 1) * rowH + 1.0f, colW - 8.0f, juce::jmax (5.0f, rowH - 1.0f), 2.0f);
        }
    }

    // Melody (Melodia) drawn over the chords.
    for (const auto& n : melodyNotes)
    {
        const auto x = r.getX() + colW * (float) (n.start / quartersPerChord);
        const auto w = juce::jmax (3.0f, colW * (float) (n.length / quartersPerChord));
        g.setColour (n.velocity == 84 ? Colours::amethyst : Colours::ember);   // counter-melody notes in violet
        g.fillRoundedRectangle (x, r.getBottom() - (float) (n.pitch - lo + 1) * rowH + 1.0f, w, juce::jmax (4.0f, rowH - 1.0f), 2.0f);
    }

    g.setColour (Colours::outline);
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
}

void RollView::mouseDown (const juce::MouseEvent& e)
{
    if (onSlotClicked)
        onSlotClicked (juce::jlimit (0, count - 1, (int) ((float) e.x / juce::jmax (1.0f, (float) getWidth() / (float) chords::maxSlots))));
}

//==============================================================================
CorrespondenceEditor::SlotView::SlotView (juce::AudioProcessorValueTreeState& state, int index)
{
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (Params::slotDegree (index))))
        degree.addItemList (p->choices, 1);
    quality.addItemList (chords::qualityNames(), 1);
    degreeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, Params::slotDegree (index), degree);
    qualityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, Params::slotQuality (index), quality);
    addAndMakeVisible (degree);
    addAndMakeVisible (quality);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void CorrespondenceEditor::SlotView::resized()
{
    auto r = getLocalBounds().reduced (4);
    quality.setBounds (r.removeFromBottom (24));
    r.removeFromBottom (4);
    degree.setBounds (r.removeFromBottom (24));
}

void CorrespondenceEditor::SlotView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().reduced (4).withTrimmedBottom (56).toFloat();
    g.setColour (lit ? accent.withAlpha (0.25f) : Colours::background);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (lit ? accent : Colours::outline);
    g.drawRoundedRectangle (r, 6.0f, 1.2f);

    g.setColour (active ? accent : Colours::dimText.withAlpha (0.4f));
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (roman, r.removeFromTop (r.getHeight() * 0.4f), juce::Justification::centredBottom);
    g.setColour (active ? Colours::text : Colours::dimText.withAlpha (0.4f));
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawFittedText (name, r.toNearestInt().reduced (2, 0), juce::Justification::centred, 1, 0.7f);
}

void CorrespondenceEditor::SlotView::mouseDown (const juce::MouseEvent& e)
{
    if (e.y < getHeight() - 60 && onClick)
        onClick();
}

//==============================================================================
CorrespondenceEditor::CorrespondenceEditor (CorrespondenceProcessor& p)
    : AudioProcessorEditor (p), processor (p),
      root (p.apvts, Params::rootNote, "Key"),
      scale (p.apvts, Params::scale, "Scale"),
      perChord (p.apvts, Params::perChord, "Length per chord"),
      count (p.apvts, Params::count, "Chords", accent),
      broadcast (p.apvts, Params::broadcast, "Broadcast key", accent),
      acceptDetected (p.apvts, Params::acceptDetected, "Accept detected", Colours::moss),
      voicing (p.apvts, Params::voicing, "Voicing"),
      chordPattern (p.apvts, Params::chordPattern, "Chord rhythm"),
      bassPattern (p.apvts, Params::bassPattern, "Bass rhythm"),
      arpPattern (p.apvts, Params::arpPattern, "Arp pattern"),
      voiceLead (p.apvts, Params::voiceLead, "Voice leading", accent),
      addBass (p.apvts, Params::addBass, "Bass in chords", Colours::brass),
      registerOct (p.apvts, Params::registerOct, "Register", accent),
      bassOctave (p.apvts, Params::bassOctave, "Bass octave", Colours::brass),
      arpOctaves (p.apvts, Params::arpOctaves, "Arp octaves", Colours::amethyst),
      previewVolume (p.apvts, Params::preview, "Preview vol", Colours::text),
      humanise (p.apvts, Params::humanise, "Humanise", Colours::text),
      chordTile (p, "Chords", accent),
      bassTile (p, "Bass", Colours::brass),
      arpTile (p, "Arp", Colours::amethyst),
      songChordTile (p, "Song Chords", accent),
      songBassTile (p, "Song Bass", Colours::brass),
      songArpTile (p, "Song Arp", Colours::amethyst),
      melodyTile (p, "Melody", Colours::ember),
      songLeadTile (p, "Song Lead", Colours::ember),
      songHarmonyTile (p, "Song Harmony", Colours::ember),
      songCounterTile (p, "Song Counter", Colours::ember),
      songSubTile (p, "Song Sub", Colours::brass),
      songFillsTile (p, "Song Fills", Colours::text),
      melodyStyle (p.apvts, Params::melodyStyle, "Melody")
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (header);
    header.actions = [this] { return actions(); };
    for (const auto& name : Params::layerNames())
    {
        layerTiles.push_back (std::make_unique<DragTile> (p, "Song " + name, name == "Piano" || name == "Strings" || name == "Brass" || name == "Choir" || name == "Timpani"
                                                                                ? Colours::brass : Colours::amethyst));
        addAndMakeVisible (*layerTiles.back());
    }

    presetCaption.setText ("Progression preset", juce::dontSendNotification);
    presetCaption.setJustificationType (juce::Justification::centred);
    presetCaption.setFont (juce::FontOptions (12.0f));
    presetBox.setTextWhenNothingSelected ("Choose a preset...");
    fillPresets();
    presetBox.onChange = [this]
    {
        const auto id = presetBox.getSelectedId();
        if (id <= 0) return;
        if (id <= (int) chords::presets().size()) { processor.applyPreset (id - 1); return; }
        presetBox.setSelectedId (0, juce::dontSendNotification);
        if (id >= 500 && id < 600 && id - 500 < (int) favourites.size()) processor.applyNumerals (favourites[(size_t) (id - 500)].first);
        else if (id >= 600 && id - 600 < (int) hooks.size()) processor.useHook (hooks[(size_t) (id - 600)].second);
        else if (id == 999)
        {
            nameWindow = std::make_unique<juce::AlertWindow> ("Save a hook", "Name this hook (your motif, or the melody's first two bars):", juce::MessageBoxIconType::NoIcon);
            nameWindow->addTextEditor ("name", "My hook");
            nameWindow->addButton ("Save", 1);
            nameWindow->addButton ("Cancel", 0);
            nameWindow->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int result)
            {
                if (result == 1 && nameWindow != nullptr) processor.saveHook (nameWindow->getTextEditorContents ("name"));
                fillPresets();
            }));
        }
    };

    broadcastStatus.setFont (juce::FontOptions (12.0f));
    broadcastStatus.setJustificationType (juce::Justification::centredLeft);

    for (int i = 0; i < chords::maxSlots; ++i)
    {
        slots[(size_t) i] = std::make_unique<SlotView> (p.apvts, i);
        slots[(size_t) i]->onClick = [this, i] { auditionSlot (i, 1.6); };
        addAndMakeVisible (*slots[(size_t) i]);
    }
    roll.onSlotClicked = [this] (int i) { auditionSlot (i, 1.6); };

    songButton.setButtonText ("Song page");
    songButton.onClick = [this] { showPage (1); };
    colourButton.setTooltip ("Harmony explorer: colour for this progression (a major V, a Dorian IV, sus and add9...), each with what it does. Press Play to hear it");
    colourButton.onClick = [this]
    {
        juce::PopupMenu m;
        const auto ideas = processor.harmonyIdeas();
        m.addSectionHeader ("Colour for this progression");
        for (size_t i = 0; i < ideas.size(); ++i)
            m.addItem ((int) i + 1, ideas[i].name + ":  " + ideas[i].effect);
        m.addSeparator();
        m.addItem (100, "Undo the last colour", processor.beforeIdea.has_value());
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&colourButton), [this] (int id)
        {
            if (id == 100) processor.undoHarmonyIdea();
            else if (id > 0) processor.applyHarmonyIdea (id - 1);
        });
    };
    varyButton.setTooltip ("Variation engine: alternatives for any kind of section (re-rhythmed, thinned out, fuller, re-harmonised) in the song clips");
    varyButton.onClick = [this]
    {
        juce::PopupMenu m;
        for (int t = 0; t < (int) suite::SectionType::count; ++t)
        {
            juce::PopupMenu sub;
            const auto vars = processor.sectionVariations ((suite::SectionType) t);
            for (size_t i = 0; i < vars.size(); ++i)
                sub.addItem (1000 + t * 10 + (int) i, vars[i].name + ":  " + vars[i].effect);
            m.addSubMenu (suite::sectionName ((suite::SectionType) t), sub, ! vars.empty());
        }
        m.addSeparator();
        juce::PopupMenu clashes;
        const auto found = processor.clashes();
        for (size_t i = 0; i < found.size(); ++i)
            clashes.addItem (2000 + (int) i, found[i].text + "  ->  " + found[i].fixText, found[i].paramId.isNotEmpty());
        if (found.empty()) clashes.addItem (1999, "No clashes: every part has its own register", false);
        m.addSubMenu ("Clash check" + (found.empty() ? juce::String() : " (" + juce::String ((int) found.size()) + ")"), clashes);
        m.addItem (1, "Undo the last variation", ! processor.beforeVariation.empty());
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&varyButton), [this] (int id)
        {
            if (id >= 2000)
            {
                const auto found = processor.clashes();
                if (id - 2000 < (int) found.size()) processor.fixClash (found[(size_t) (id - 2000)]);
                return;
            }
            if (id == 1) processor.undoVariation();
            else if (id >= 1000) processor.applyVariation ((suite::SectionType) ((id - 1000) / 10), (id - 1000) % 10);
        });
    };
    newMelodyButton.onClick = [this]
    {
        if (! processor.getMotif().empty() || processor.getDna().valid)
        {
            processor.forgetMotif();
            processor.forgetDna();
            return;
        }
        if (auto* seed = processor.apvts.getParameter (Params::melodySeed))
        {
            const auto next = juce::Random::getSystemRandom().nextInt (999) + 1;
            seed->beginChangeGesture();
            seed->setValueNotifyingHost (seed->convertTo0to1 ((float) next));
            seed->endChangeGesture();
        }
    };
    playButton.setClickingTogglesState (false);
    playButton.onClick = [this] { togglePlay(); };
    saveButton.setTooltip ("Writes every clip into a new song folder, plus one MIDI file with all the song parts on separate "
                           "tracks and the sections as markers: drag it into FL to get everything at once");
    saveButton.onClick = [this]
    {
        juce::PopupMenu m;
        m.addItem (1, "Export all (every clip and the all-parts file)...");
        m.addItem (2, "Song pack for a vocalist (sheet, chords, bass, lead guide, markers)...");
        m.addItem (3, "Spire layers: lead octave, lead pluck, sub (one clip each)...");
        m.addSeparator();
        m.addItem (4, "Send to FL's piano roll (as scripts: Tools > Scripting)");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&saveButton), [this] (int choice)
        {
            if (choice <= 0) return;
            if (choice == 4)
            {
                const auto written = processor.sendToPianoRoll();
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "Sent to FL's piano roll",
                    written.isEmpty() ? juce::String ("Nothing to send.")
                                      : "Wrote " + juce::String (written.size()) + " scripts to " + written[0].getParentDirectory().getFullPathName()
                                            + ".\n\nIn FL's piano roll: Tools > Scripting > 'Antioch - Chords' (or any part), choose where, and OK. "
                                              "If FL was open, it lists new scripts after a restart (or Tools > Scripting > Refresh, if your version has it).");
                return;
            }
            chooser = std::make_unique<juce::FileChooser> ("Choose a folder", juce::File::getSpecialLocation (juce::File::userMusicDirectory));
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                  [this, choice] (const juce::FileChooser& fc)
                                  {
                                      const auto dir = fc.getResult();
                                      if (dir == juce::File()) return;
                                      if (choice == 3)
                                      {
                                          for (auto* kind : { "Song Lead Octave", "Song Lead Pluck", "Song Sub" }) processor.exportClip (kind, dir);
                                          dir.revealToUser();
                                          return;
                                      }
                                      (choice == 1 ? processor.exportEverything (dir) : processor.exportSongPack (dir)).revealToUser();
                                  });
        });
    };

    for (auto* c : std::initializer_list<juce::Component*> { &root, &scale, &perChord, &count, &broadcast, &acceptDetected, &broadcastStatus,
                                                             &presetCaption, &presetBox, &roll, &voicing, &chordPattern,
                                                             &bassPattern, &arpPattern, &voiceLead, &addBass, &registerOct,
                                                             &bassOctave, &arpOctaves, &previewVolume, &humanise, &chordTile, &bassTile,
                                                             &arpTile, &songChordTile, &songBassTile, &songArpTile,
                                                             &melodyTile, &songLeadTile, &melodyStyle, &newMelodyButton,
                                                             &playButton, &saveButton, &songButton, &colourButton, &varyButton })
        addAndMakeVisible (c);
    writeButton.setTooltip ("Write: lock bars and regenerate the rest, step through takes and keep the best bars, hear a part in context, "
                            "the hook doctor, hum a hook, a topline from lyrics, the originality check, the emotional arc, trance harmony moves, "
                            "a reference's feel, the shared rhythm, and why it works");
    writeButton.onClick = [this] { showWriteMenu(); };
    makePrimary (writeButton, accent);

    // Two pages: Chords and Song
    songGrid = makeSongGrid (processor.apvts, [this] (juce::Component& c) { showStrongerHooks (c); }, [this] (juce::Component& c, int goal) { showLabelOptions (c, goal); });
    addChildComponent (*songGrid);
    for (auto* t : { &songHarmonyTile, &songCounterTile, &songSubTile, &songFillsTile }) addChildComponent (*t);
    for (auto* b : { &chordsTab, &songTab })
    {
        b->setClickingTogglesState (false);
        b->setColour (juce::TextButton::buttonOnColourId, accent.withAlpha (0.35f));
    }
    header.addRight (songTab, 90);
    header.addRight (chordsTab, 100);
    header.addRight (writeButton, 110);
    header.enableAdvanced (processor.apvts.state, [this] (bool) { resized(); repaint(); });
    chordsTab.setTooltip ("Key, progression, voicing and rhythm, and the loop clips");
    songTab.setTooltip ("The whole song: patterns per section, the pattern library (mood, style, kit, layers) and every song clip to drag into FL");
    chordsTab.onClick = [this] { showPage (0); };
    songTab.onClick = [this] { showPage (1); };
    songExportButton.setTooltip ("Write every song clip, the markers and a song pack to a folder");
    songExportButton.onClick = [this] { saveButton.triggerClick(); };
    addChildComponent (songExportButton);
    directorStrip.director = [] { return suite::DirectorBus::cue (suite::director::pCorrespondence); };
    addAndMakeVisible (directorStrip);

    // Resizable; the size is kept with the project
    setResizable (true, true);
    setResizeLimits (1160, 740, 2560, 1600);
    const auto w = (int) processor.apvts.state.getProperty ("uiWidth", 1160), hgt = (int) processor.apvts.state.getProperty ("uiHeight", 740);
    page = juce::jlimit (0, 1, (int) processor.apvts.state.getProperty ("uiPage", 0));
    setSize (juce::jlimit (1160, 2560, w), juce::jlimit (740, 1600, hgt));
    showPage (page);
    timerCallback();
    startTimerHz (30);
}

CorrespondenceEditor::~CorrespondenceEditor()
{
    stopTimer();
    processor.preview.stop();
    setLookAndFeel (nullptr);
}

namespace
{
    /** Grid of pattern choices: one row per section type, columns for chords, bass and arp. */
    class SongPatternGrid : public juce::Component
    {
    public:
        explicit SongPatternGrid (juce::AudioProcessorValueTreeState& state)
        {
            for (int t = 0; t < (int) suite::SectionType::count; ++t)
            {
                auto label = std::make_unique<juce::Label> ("", suite::sectionName ((suite::SectionType) t));
                label->setFont (juce::FontOptions (14.0f, juce::Font::bold));
                addAndMakeVisible (*label);
                labels.push_back (std::move (label));
                for (auto part : { chords::SongPart::Chords, chords::SongPart::Bass, chords::SongPart::Arp, chords::SongPart::Lead })
                {
                    auto box = std::make_unique<juce::ComboBox>();
                    box->addItemList (chords::songChoiceNames (part), 1);
                    attachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
                        state, Params::songId (part, t), *box));
                    addAndMakeVisible (*box);
                    boxes.push_back (std::move (box));
                }
            }
            hint.setText ("Each section of the Ouroboros map plays these patterns; the progression restarts at every section.",
                          juce::dontSendNotification);
            lift = std::make_unique<juce::ToggleButton> ("Lead up an octave in drop 2+");
            liftAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, Params::songLift, *lift);
            addAndMakeVisible (*lift);
            counter = std::make_unique<juce::ToggleButton> ("Counter-melody");
            counterAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, Params::songCounter, *counter);
            addAndMakeVisible (*counter);
            for (const auto& [ptr, att, text, pid, tip] : {
                     std::tuple<std::unique_ptr<juce::ToggleButton>*, std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>*, const char*, juce::String, const char*>
                     { &climb, &climbAttachment, "Climbing chords in drop 2+", Params::songClimb, "Later drops: each chord gains its top note an octave up" },
                     { &doubling, &doublingAttachment, "Double the final drop", Params::songDouble,
                       "The last of several drops: the lead doubled an octave up and the bass a sub octave down" },
                     { &ghost, &ghostAttachment, "Ghost notes", Params::songGhost, "Soft in-between 16ths in the bass and arp, more where the energy is higher" },
                     { &fills, &fillsAttachment, "Drum fills", Params::songFills, "A SONG FILLS clip (General MIDI drums) for every section change and phrase end" },
                     { &vocalRoom, &vocalRoomAttachment, "Lead and vocal take turns", Params::songVocalRoom,
                       "Where the vocal sings (heard by Solve et Coagula) the lead keeps only beats 1 and 3 and the harmony steps out; "
                       "where there's no lead, it answers in the bar after each vocal phrase" } })
            {
                *ptr = std::make_unique<juce::ToggleButton> (text);
                (*ptr)->setTooltip (tip);
                *att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, pid, **ptr);
                addAndMakeVisible (**ptr);
            }
            phrase = std::make_unique<juce::ToggleButton> ("Phrase turnarounds");
            phrase->setTooltip ("The last bar of every 8-bar phrase: the bass bounces octaves and the arp lifts an octave");
            phraseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, Params::songPhrase, *phrase);
            addAndMakeVisible (*phrase);
            vary = std::make_unique<juce::ToggleButton> ("Vary the lead + harmony in drop 2+");
            varyAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, Params::songVary, *vary);
            addAndMakeVisible (*vary);
            grooveBox = std::make_unique<juce::ComboBox>();
            grooveBox->addItemList (state.getParameter (Params::groove)->getAllValueStrings(), 1);
            grooveBox->setTooltip ("The feel of the exported MIDI; Kybalion's gate swings with it too");
            grooveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, Params::groove, *grooveBox);
            addAndMakeVisible (*grooveBox);
            leadBox = std::make_unique<juce::ComboBox>();
            leadBox->addItemList (state.getParameter (Params::leadExpression)->getAllValueStrings(), 1);
            leadBox->setTooltip ("Exported leads: glide on leaps (set your synth to legato / portamento) and pitch-bend scoops "
                                 "into each phrase (set its bend range to match)");
            leadAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, Params::leadExpression, *leadBox);
            addAndMakeVisible (*leadBox);
            accent = std::make_unique<juce::ToggleButton> ("Accents as CC 74");
            accent->setTooltip ("Each note's accent is also written as CC 74, so a synth's filter can open on the accents");
            accentAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, Params::accentCC, *accent);
            addAndMakeVisible (*accent);
            for (const auto& [editor, prop, hint] : { std::tuple<juce::TextEditor*, const char*, const char*> { &title, "songTitle", "Song title (for exports)" },
                                               { &artist, "artist", "Artist" } })
            {
                editor->setTextToShowWhenEmpty (hint, Colours::dimText);
                editor->setText (state.state[prop].toString(), juce::dontSendNotification);
                auto* ed = editor;
                const juce::Identifier id (prop);
                ed->onTextChange = [&state, ed, id] { state.state.setProperty (id, ed->getText(), nullptr); };
                addAndMakeVisible (*editor);
            }
            macrosBox = std::make_unique<juce::ComboBox>();
            macrosBox->addItemList (state.getParameter (Params::songMacros)->getAllValueStrings(), 1);
            macrosBox->setTooltip ("Spire macro rides: the song clips carry CC automation for Spire's four macros (1 brightness, 2 drive, "
                                   "3 space, 4 movement) that follows the song. In Spire, MIDI-learn each macro to its CC once, and save the patch");
            macrosAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, Params::songMacros, *macrosBox);
            addAndMakeVisible (*macrosBox);
            planBox = std::make_unique<juce::ComboBox>();
            planBox->addItemList (state.getParameter (Params::macroPlan)->getAllValueStrings(), 1);
            planBox->setTooltip ("How the Spire macros move through the song: Uplifting (full moves), Progressive (gentler), "
                                 "Big breakdowns (space wide open, brightness down, movement rising). Export all also writes them as Spire macros.mid");
            planAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, Params::macroPlan, *planBox);
            addAndMakeVisible (*planBox);
            breakdownBox = std::make_unique<juce::ComboBox>();
            breakdownBox->addItemList (state.getParameter (Params::songBreakdown)->getAllValueStrings(), 1);
            breakdownBox->setTooltip ("Breakdowns can start the progression on VI or IV, so the drop's return to i lands harder, "
                                      "or lift to the relative major's home (III) for their second half");
            breakdownAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, Params::songBreakdown, *breakdownBox);
            addAndMakeVisible (*breakdownBox);
            hint.setFont (juce::FontOptions (12.0f));
            addAndMakeVisible (hint);

            // The pattern library: mood x style, and a roll of the dice for another set of families
            libOn = std::make_unique<juce::ToggleButton> ("Pattern library");
            libOn->setTooltip ("Bass, arps, chord rhythms and lead hooks from the library: one family per part for the whole song, "
                               "quiet in intros and breakdowns, climbing through builds, full in the drops, with a fill at each phrase end");
            libOnAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, Params::libOn, *libOn);
            addAndMakeVisible (*libOn);
            kitsToggle = std::make_unique<juce::ToggleButton> ("Kits");
            kitsToggle->setTooltip ("Kits: a family for every part chosen to go together (a sound, a groove, a character). "
                                    "Off: each part picked on its own, kept in the same feel and interlocking with the bass");
            kitsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, Params::libKits, *kitsToggle);
            kitsToggle->onClick = [this] { showFamilies(); };
            addAndMakeVisible (*kitsToggle);
            libLead = std::make_unique<juce::ToggleButton> ("Lead hooks too");
            libLead->setTooltip ("The lead from the library's hooks (a motif or melody style you taught it always wins)");
            libLeadAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, Params::libLead, *libLead);
            addAndMakeVisible (*libLead);
            for (auto [box, att, pid, tip] : { std::tuple { &moodBox, &moodAttachment, Params::libMood, "The mood: which families colour the song" },
                                              std::tuple { &styleBox, &styleAttachment, Params::libStyle, "The style: the rhythm language of the parts" } })
            {
                *box = std::make_unique<juce::ComboBox>();
                (*box)->addItemList (state.getParameter (pid)->getAllValueStrings(), 1);
                (*box)->setTooltip (tip);
                *att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, pid, **box);
                (*box)->onChange = [this] { showFamilies(); };
                addAndMakeVisible (**box);
            }
            rollButton.setTooltip ("Another set of families for the same mood and style");
            rollButton.onClick = [this, &state]
            {
                if (auto* seed = state.getParameter (Params::libSeed))
                    seed->setValueNotifyingHost (seed->convertTo0to1 ((float) juce::Random::getSystemRandom().nextInt ({ 1, 10000 })));
                showFamilies();
            };
            addAndMakeVisible (rollButton);
            families.setFont (juce::FontOptions (12.0f));
            families.setColour (juce::Label::textColourId, Colours::dimText);
            addAndMakeVisible (families);
            for (int i = 0; i < Params::layerNames().size(); ++i)
            {
                auto t = std::make_unique<juce::ToggleButton> (Params::layerNames()[i]);
                t->setTooltip ("The library's " + Params::layerNames()[i].toLowerCase() + " layer in the song (piano and orchestra in breakdowns, "
                               "intros and the final drop; acid, chops and candy where the style uses them)");
                layerAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, Params::layerId (i), *t));
                addAndMakeVisible (*t);
                layerToggles.push_back (std::move (t));
            }
            writingButton.setTooltip ("Writing: how the parts are shaped beyond the library's patterns (phrases, development, harmony, feel, "
                                      "parts together, arrangement, ornaments, your edits), your favourite and banned kits and hooks, and the library's tools");
            writingButton.onClick = [this] { showWritingMenu(); };
            addAndMakeVisible (writingButton);
            stateRef = &state;
            showFamilies();
            setSize (1200, 492);
        }

        std::function<void (juce::Component&)> writeStronger;   // the editor's: scores the drop leads
        std::function<void (juce::Component&, int)> writeForLabel;   // the editor's: scored options per goal

        /** The Writing menu: the groups on or off, favourites and bans, and the library's tools. */
        void showWritingMenu()
        {
            if (stateRef == nullptr) return;
            auto& state = *stateRef;
            juce::PopupMenu m;
            m.addSectionHeader ("Writing");
            for (int i = 0; i < Params::writingNames().size(); ++i)
            {
                const auto on = state.getRawParameterValue (Params::writingId (i))->load() > 0.5f;
                m.addItem (1 + i, Params::writingNames()[i], true, on);
            }
            const auto sp = Params::readSongPatterns (state);
            const auto mood = (patterns::Mood) juce::jlimit (0, 6, sp.libMood);
            const auto style = (patterns::Style) juce::jlimit (0, 6, sp.libStyle);
            const auto* kit = sp.libKits ? patterns::pickKit (mood, style, sp.libSeed) : nullptr;
            const auto& hook = chords::songFamily (chords::SongPart::Lead, 0, sp, -1);
            m.addSeparator();
            m.addSectionHeader ("Favourites (later songs lean on them; banned ones never come up)");
            if (kit != nullptr)
            {
                const auto f = patterns::favourite (patterns::kitKey (*kit));
                m.addItem (20, "Favourite this kit (" + juce::String (kit->name) + ")", true, f > 0);
                m.addItem (21, "Ban this kit", true, f < 0);
            }
            if (! hook.name.empty())
            {
                const auto f = patterns::favourite (patterns::familyKey (hook));
                m.addItem (22, "Favourite this hook (" + juce::String (hook.name) + ")", true, f > 0);
                m.addItem (23, "Ban this hook", true, f < 0);
            }
            m.addSeparator();
            m.addSectionHeader ("Tools");
            m.addItem (30, "Learn from my edit of a part (a MIDI file)...");
            m.addItem (31, "Suggest new kits for this mood and style (adds to My kits)");
            m.addItem (32, "Library report (coverage, near-duplicates, weakest)...");
            m.addItem (33, "Render this song's drop to a WAV (audition)...");
            m.addItem (34, "Check the library (validator)");
            m.addItem (35, "Sound guide: what each part should sound like...");
            m.addItem (36, "Save this song's hook to the hook bank");
            m.addItem (37, "Write the hook stronger (3 scored new hooks)...", writeStronger != nullptr);
            {
                juce::PopupMenu label;
                label.addItem (40, "The breakdown's emotion...");
                label.addItem (41, "Drop 2 differs from drop 1...");
                label.addItem (42, "The counter-line's space...");
                label.addItem (43, "The drop's bass fits the chords...");
                label.addItem (44, "Tease the hook in the intro...");
                m.addSubMenu ("Write for the label (scored options)", label, writeForLabel != nullptr);
            }
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&writingButton), [this, kitName = kit != nullptr ? kit->name : std::string(), hookKey = patterns::familyKey (hook)] (int id)
            {
                if (id <= 0 || stateRef == nullptr) return;
                auto& st = *stateRef;
                if (id >= 1 && id <= 8)
                {
                    if (auto* prm = st.getParameter (Params::writingId (id - 1))) prm->setValueNotifyingHost (prm->getValue() > 0.5f ? 0.0f : 1.0f);
                    return;
                }
                if (id == 20 || id == 21) { const auto k = "kit:" + kitName; patterns::setFavourite (k, patterns::favourite (k) == (id == 20 ? 1 : -1) ? 0 : (id == 20 ? 1 : -1)); showFamilies(); return; }
                if (id == 22 || id == 23)
                {
                    const auto nowFav = patterns::favourite (hookKey) == (id == 22 ? 1 : -1) ? 0 : (id == 22 ? 1 : -1);
                    patterns::setFavourite (hookKey, nowFav);
                    if (nowFav > 0) saveHookToBank();   // 3: a favourite hook goes in the hook bank too
                    showFamilies();
                    return;
                }
                if (id == 35)
                {
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "Sound guide", chords::drums::soundGuide (Params::readSongPatterns (st)));
                    return;
                }
                if (id == 37) { if (writeStronger) writeStronger (writingButton); return; }
                if (id >= 40 && id <= 44) { if (writeForLabel) writeForLabel (writingButton, id - 40); return; }
                if (id == 36)
                {
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "Hook bank", saveHookToBank());
                    return;
                }
                if (id == 34)
                {
                    const auto problems = patterns::validate();
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "Library check",
                        problems.isEmpty() ? juce::String ("Every family and kit is fine.") : problems.joinIntoString ("\n"));
                    return;
                }
                if (id == 31)
                {
                    const auto spNow = Params::readSongPatterns (st);
                    const auto made = patterns::generateKits ((patterns::Mood) juce::jlimit (0, 6, spNow.libMood), (patterns::Style) juce::jlimit (0, 6, spNow.libStyle), 3,
                                                              (uint32_t) juce::Random::getSystemRandom().nextInt());
                    juce::StringArray lines;
                    for (const auto& k : made) lines.add (patterns::kitLine (k));
                    const auto f = patterns::userKitsFile();
                    f.getParentDirectory().createDirectory();
                    if (! f.existsAsFile()) f.replaceWithText ("# Your kits (Correspondence's kit generator adds here; edit freely, same format as kits.txt)\n");
                    f.appendText (lines.joinIntoString ("\n") + "\n");
                    patterns::reloadKits();
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "New kits",
                        juce::String ((int) made.size()) + " kit(s) added to My kits (" + f.getFullPathName() + "):\n\n" + lines.joinIntoString ("\n\n"));
                    showFamilies();
                    return;
                }
                const auto save = id == 32 || id == 33;
                chooser = std::make_unique<juce::FileChooser> (id == 30 ? "Your edited part (MIDI)" : id == 32 ? "Save the library report" : "Save the audition",
                                                               juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                                                   .getChildFile (id == 32 ? "Library report.txt" : id == 33 ? "Drop audition.wav" : ""),
                                                               id == 30 ? "*.mid" : id == 32 ? "*.txt" : "*.wav");
                chooser->launchAsync (save ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting : juce::FileBrowserComponent::openMode,
                                      [this, id] (const juce::FileChooser& fc)
                {
                    const auto file = fc.getResult();
                    if (file == juce::File() || stateRef == nullptr) return;
                    auto& st2 = *stateRef;
                    if (id == 32) { file.replaceWithText (patterns::coverageReport()); file.revealToUser(); return; }
                    if (id == 33)
                    {
                        const auto ok = chords::audition::renderDrop (file, Params::readSettings (st2), Params::readSongPatterns (st2));
                        if (ok) file.revealToUser();
                        else juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "Audition", "Nothing to render: switch the pattern library on.");
                        return;
                    }
                    // 30: which part, from the file's name (bass, arp, chords, lead, counter, piano...)
                    const auto name = file.getFileNameWithoutExtension().toLowerCase();
                    auto part = chords::SongPart::Lead;
                    for (auto [word, p] : { std::pair { "bass", chords::SongPart::Bass }, std::pair { "arp", chords::SongPart::Arp }, std::pair { "chord", chords::SongPart::Chords },
                                            std::pair { "counter", chords::SongPart::Counter }, std::pair { "piano", chords::SongPart::Piano }, std::pair { "string", chords::SongPart::Strings },
                                            std::pair { "brass", chords::SongPart::Brass }, std::pair { "choir", chords::SongPart::Choir }, std::pair { "acid", chords::SongPart::Acid },
                                            std::pair { "candy", chords::SongPart::Candy }, std::pair { "chop", chords::SongPart::Chops } })
                        if (name.contains (word)) { part = p; break; }
                    std::vector<chords::Note> edited;
                    juce::MidiFile mf;
                    if (auto in = file.createInputStream(); in != nullptr && mf.readFrom (*in))
                    {
                        const auto tpq = juce::jmax (1, (int) mf.getTimeFormat());
                        for (int t = 0; t < mf.getNumTracks(); ++t)
                        {
                            juce::MidiMessageSequence seq (*mf.getTrack (t));
                            seq.updateMatchedPairs();
                            for (int e = 0; e < seq.getNumEvents(); ++e)
                                if (const auto* ev = seq.getEventPointer (e); ev->message.isNoteOn())
                                {
                                    const auto start = ev->message.getTimeStamp() / tpq;
                                    const auto end = ev->noteOffObject != nullptr ? ev->noteOffObject->message.getTimeStamp() / tpq : start + 0.25;
                                    edited.push_back ({ ev->message.getNoteNumber(), start, end - start, ev->message.getVelocity() });
                                }
                        }
                    }
                    suite::SectionMap map;
                    if (! suite::SectionBus::read (map)) { map.count = 1; map.sections[0] = { 1, suite::SectionType::Drop }; map.totalBars = 32; }
                    auto spNoLearn = Params::readSongPatterns (st2);
                    spNoLearn.write.learned = false;
                    const auto original = chords::songClip (Params::readSettings (st2), map, spNoLearn, part);
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "Learned from your edit",
                        chords::writing::learnEdit (chords::lib::libraryPart (part), original, edited));
                });
            });
        }

        /** 3: the song's library hook (8 bars over this progression, from bar 1) into the hook bank,
            where Melodia can recall it as a motif in any song, in any key. */
        juce::String saveHookToBank()
        {
            if (stateRef == nullptr) return {};
            const auto sp = Params::readSongPatterns (*stateRef);
            const auto& hook = chords::songFamily (chords::SongPart::Lead, 0, sp, -1);
            if (hook.name.empty()) return "No library hook in this song.";
            const auto notes = chords::lib::render (hook, 2, false, Params::readSettings (*stateRef), 32.0, sp.melody.octave);
            const auto name = "Library - " + juce::String (hook.name);
            return identity::saveHook (name, notes) ? "Saved \"" + name + "\" to the hook bank (" + identity::hookFolder().getFullPathName() + ")."
                                                    : juce::String ("Couldn't save the hook.");
        }

        /** The families this song's parts play (drop rhythms), under the library row. */
        void showFamilies()
        {
            if (stateRef == nullptr) return;
            const auto sp = Params::readSongPatterns (*stateRef);
            juce::StringArray names;
            if (sp.libKits)
                if (const auto* kit = patterns::pickKit ((patterns::Mood) juce::jlimit (0, 6, sp.libMood), (patterns::Style) juce::jlimit (0, 6, sp.libStyle), sp.libSeed))
                    names.add ("Kit: " + juce::String (kit->name) + (patterns::favourite (patterns::kitKey (*kit)) > 0 ? " (favourite)" : "") + (kit->user ? " (yours)" : ""));
            using T = suite::SectionType;
            const auto drop = (size_t) T::Drop;
            for (auto [part, choice, label] : { std::tuple { chords::SongPart::Bass, sp.bass[drop], "bass" }, std::tuple { chords::SongPart::Arp, sp.arp[drop], "arp" },
                                                std::tuple { chords::SongPart::Chords, sp.chords[drop], "chords" }, std::tuple { chords::SongPart::Lead, 1, "hook" } })
                if (const auto kind = chords::lib::kindFor (part, choice, T::Drop); kind >= 0)
                    names.add (juce::String (label) + ": " + juce::String (chords::songFamily (part, kind, sp, -1).name));
            families.setText (names.joinIntoString ("   "), juce::dontSendNotification);
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (Colours::dimText);
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            const auto w = (getWidth() - 120) / 4;
            int x = 120;
            for (auto* h : { "CHORDS", "BASS", "ARP", "LEAD" }) { g.drawText (h, x, 4, w, 18, juce::Justification::centred); x += w; }
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (6);
            r.removeFromTop (20);
            hint.setBounds (r.removeFromBottom (24));
            {
                auto row = r.removeFromBottom (28);
                row.removeFromLeft (130);
                const auto w = row.getWidth() / juce::jmax (1, (int) layerToggles.size());
                for (auto& t : layerToggles) t->setBounds (row.removeFromLeft (w));
            }
            {
                auto row = r.removeFromBottom (30);
                libOn->setBounds (row.removeFromLeft (130));
                moodBox->setBounds (row.removeFromLeft (130).reduced (2));
                styleBox->setBounds (row.removeFromLeft (140).reduced (2));
                rollButton.setBounds (row.removeFromLeft (120).reduced (2));
                libLead->setBounds (row.removeFromLeft (130));
                kitsToggle->setBounds (row.removeFromLeft (70));
                writingButton.setBounds (row.removeFromLeft (90).reduced (2));
                families.setBounds (row);
            }
            {
                auto extras = r.removeFromBottom (30);
                grooveBox->setBounds (extras.removeFromLeft (160).reduced (2));
                accent->setBounds (extras.removeFromLeft (170));
                phrase->setBounds (extras.removeFromLeft (160));
                leadBox->setBounds (extras.removeFromLeft (200).reduced (2));
                artist.setBounds (extras.removeFromRight (200).reduced (2));
                title.setBounds (extras.removeFromRight (260).reduced (2));
            }
            {
                auto more = r.removeFromBottom (28);
                const auto w = more.getWidth() / 5;
                climb->setBounds (more.removeFromLeft (w));
                doubling->setBounds (more.removeFromLeft (w));
                ghost->setBounds (more.removeFromLeft (w));
                fills->setBounds (more.removeFromLeft (w));
                vocalRoom->setBounds (more);
            }
            {
                auto toggles = r.removeFromBottom (28);
                breakdownBox->setBounds (toggles.removeFromRight (170).reduced (2));
                macrosBox->setBounds (toggles.removeFromRight (130).reduced (2));
                planBox->setBounds (toggles.removeFromRight (140).reduced (2));
                counter->setBounds (toggles.removeFromRight (140));
                lift->setBounds (toggles.removeFromLeft (toggles.getWidth() / 2));
                vary->setBounds (toggles);
            }
            const auto rowH = r.getHeight() / (int) labels.size();
            for (size_t t = 0; t < labels.size(); ++t)
            {
                auto row = r.removeFromTop (rowH);
                labels[t]->setBounds (row.removeFromLeft (114));
                const auto w = row.getWidth() / 4;
                for (size_t k = 0; k < 4; ++k)
                    boxes[t * 4 + k]->setBounds (row.removeFromLeft (w).reduced (3, 4));
            }
        }

    private:
        std::vector<std::unique_ptr<juce::Label>> labels;
        std::vector<std::unique_ptr<juce::ComboBox>> boxes;
        std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> attachments;
        juce::Label hint;
        std::unique_ptr<juce::ToggleButton> lift, vary, counter, phrase, climb, doubling, ghost, fills, vocalRoom;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> climbAttachment, doublingAttachment, ghostAttachment, fillsAttachment, vocalRoomAttachment;
        std::unique_ptr<juce::ComboBox> leadBox;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> leadAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> liftAttachment, varyAttachment, counterAttachment, phraseAttachment;
        std::unique_ptr<juce::ComboBox> breakdownBox, grooveBox, macrosBox;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> macrosAttachment, planAttachment;
        std::unique_ptr<juce::ComboBox> planBox;
        std::unique_ptr<juce::ToggleButton> accent;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> grooveAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> accentAttachment;
        juce::TextEditor title, artist;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> breakdownAttachment;
        std::unique_ptr<juce::ToggleButton> libOn, libLead, kitsToggle;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> kitsAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> libOnAttachment, libLeadAttachment;
        std::unique_ptr<juce::ComboBox> moodBox, styleBox;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> moodAttachment, styleAttachment;
        juce::TextButton rollButton { "New patterns" };
        juce::TextButton writingButton { "Writing..." };
        std::unique_ptr<juce::FileChooser> chooser;
        juce::Label families;
        juce::AudioProcessorValueTreeState* stateRef = nullptr;
        std::vector<std::unique_ptr<juce::ToggleButton>> layerToggles;
        std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> layerAttachments;
    };
}

namespace
{
    std::unique_ptr<juce::Component> makeSongGrid (juce::AudioProcessorValueTreeState& s, std::function<void (juce::Component&)> writeStronger, std::function<void (juce::Component&, int)> writeForLabel)
    {
        auto g = std::make_unique<SongPatternGrid> (s);
        g->writeStronger = std::move (writeStronger);
        g->writeForLabel = std::move (writeForLabel);
        return g;
    }
}

/** Write for the label: options for one goal, scored, the best three offered (with Undo). */
void CorrespondenceEditor::showLabelOptions (juce::Component& target, int goal)
{
    static const char* goals[] = { "The breakdown's emotion", "Drop 2 isn't a copy of drop 1", "The counter-line's space", "The drop's bass fits the chords", "The hook teased early" };
    float current = 0.0f;
    const auto g = (CorrespondenceProcessor::LabelGoal) juce::jlimit (0, 4, goal);
    const auto options = processor.labelOptions (g, current);
    juce::PopupMenu m;
    m.addSectionHeader (juce::String (goals[juce::jlimit (0, 4, goal)]) + ": now " + juce::String (juce::roundToInt (current * 100.0f)) + "/100");
    if (options.empty()) m.addItem (-1, "Nothing scores higher than what's there", false);
    for (size_t i = 0; i < options.size(); ++i)
        m.addItem ((int) i + 1, options[i].name + "  (" + juce::String (juce::roundToInt (options[i].score * 100.0f)) + "/100)");
    m.addSeparator();
    m.addItem (99, "Undo the last change", ! processor.beforeVariation.empty());
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target), [this, options] (int id)
    {
        if (id == 99) { processor.undoVariation(); return; }
        if (id <= 0 || id > (int) options.size()) return;
        processor.applyLabelOption (options[(size_t) id - 1]);
    });
}

/** Write the hook stronger: the drop's lead variations scored as hooks, the best three offered. */
void CorrespondenceEditor::showStrongerHooks (juce::Component& target)
{
    float current = 0.0f;
    const auto options = processor.strongerHooks (current);
    juce::PopupMenu m;
    m.addSectionHeader ("Your hook now: " + juce::String (juce::roundToInt (current * 100.0f)) + "/100");
    if (options.empty()) m.addItem (-1, "Nothing stronger found (or the drop has no melody lead, or a learned motif writes it)", false);
    for (size_t i = 0; i < options.size(); ++i)
        m.addItem ((int) i + 1, options[i].name + "  (" + juce::String (juce::roundToInt (options[i].score * 100.0f)) + "/100)");
    m.addSeparator();
    m.addItem (99, "Undo the last change", ! processor.beforeVariation.empty());
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target), [this, options] (int id)
    {
        if (id == 99) { processor.undoVariation(); return; }
        if (id <= 0 || id > (int) options.size()) return;
        processor.applyStrongerHook (options[(size_t) id - 1]);
    });
}

void CorrespondenceEditor::showSongPatterns() { showPage (1); }

std::vector<juce::Component*> CorrespondenceEditor::chordsPageParts()
{
    std::vector<juce::Component*> v { &roll, &voicing, &chordPattern, &bassPattern, &arpPattern, &voiceLead, &addBass, &registerOct,
                                      &bassOctave, &arpOctaves, &previewVolume, &humanise, &chordTile, &bassTile, &arpTile, &melodyTile,
                                      &melodyStyle, &newMelodyButton, &playButton, &saveButton, &songButton, &colourButton, &varyButton };
    for (auto& s : slots) v.push_back (s.get());
    return v;
}

std::vector<juce::Component*> CorrespondenceEditor::songPageParts()
{
    std::vector<juce::Component*> v { songGrid.get(), &songChordTile, &songBassTile, &songArpTile, &songLeadTile, &songHarmonyTile,
                                      &songCounterTile, &songSubTile, &songFillsTile, &songExportButton };
    for (auto& t : layerTiles) v.push_back (t.get());
    return v;
}

void CorrespondenceEditor::showPage (int p)
{
    page = juce::jlimit (0, 1, p);
    processor.apvts.state.setProperty ("uiPage", page, nullptr);
    for (auto* c : chordsPageParts()) if (c != nullptr) c->setVisible (page == 0);
    for (auto* c : songPageParts()) if (c != nullptr) c->setVisible (page == 1);
    chordsTab.setToggleState (page == 0, juce::dontSendNotification);
    songTab.setToggleState (page == 1, juce::dontSendNotification);
    resized();
    repaint();
}

void CorrespondenceEditor::auditionSlot (int slot, double seconds)
{
    if (! juce::isPositiveAndBelow (slot, (int) voiced.size()))
        return;
    processor.preview.playChord (voiced[(size_t) slot]);
    playingSlot = slot;
    auditionStopAt = juce::Time::getMillisecondCounterHiRes() + seconds * 1000.0;
}

void CorrespondenceEditor::togglePlay()
{
    playing = ! playing;
    playButton.setButtonText (playing ? "Stop" : "Play");
    playButton.setToggleState (playing, juce::dontSendNotification);
    if (playing)
    {
        playStart = juce::Time::getMillisecondCounterHiRes();
        playingSlot = -1;
        nextMelodyNote = 0;
    }
    else
    {
        processor.preview.stop();
        playingSlot = -1;
    }
}

void CorrespondenceEditor::timerCallback()
{
    settings = processor.currentSettings();
    voiced = chords::voiceProgression (settings);

    const auto now = juce::Time::getMillisecondCounterHiRes();
    if (playing)
    {
        // The melody plays along with the progression.
        const auto quarterMs = 60000.0 / processor.currentBpm.load();
        const auto elapsedQ = (now - playStart) / quarterMs;
        const auto lead = chords::melody (settings, processor.melodySettings());
        while (nextMelodyNote < lead.size() && lead[nextMelodyNote].start <= elapsedQ)
            processor.preview.playLead (lead[nextMelodyNote++].pitch);

        const auto chordMs = settings.quartersPerChord * 60000.0 / processor.currentBpm.load();
        const auto slot = (int) ((now - playStart) / chordMs);
        if (slot >= settings.count)
            togglePlay();
        else if (slot != playingSlot)
            auditionSlot (slot, chordMs / 1000.0);
    }
    else if (playingSlot >= 0 && now > auditionStopAt)
    {
        processor.preview.stop();
        playingSlot = -1;
    }

    std::vector<int> bassNotes;
    for (int i = 0; i < settings.count; ++i)
    {
        auto& view = *slots[(size_t) i];
        const auto& slot = settings.slots[(size_t) i];
        view.roman = chords::romanNumeral (settings, slot);
        view.name = chords::chordName (settings, slot);
        bassNotes.push_back (12 * (settings.bassOctave + 1) + chords::chordRootPitchClass (settings, i));
    }
    for (int i = 0; i < chords::maxSlots; ++i)
    {
        auto& view = *slots[(size_t) i];
        view.active = i < settings.count;
        view.lit = i == playingSlot;
        view.degree.setEnabled (view.active);
        view.quality.setEnabled (view.active);
        view.repaint();
    }
    melodyNotes = chords::melody (settings, processor.melodySettings());
    if (processor.apvts.getRawParameterValue (Params::songCounter)->load() > 0.5f)
        for (const auto& n : chords::counterMelody (melodyNotes, settings)) melodyNotes.push_back (n);
    roll.setData (voiced, bassNotes, settings.count, playingSlot, melodyNotes, settings.quartersPerChord);
    const auto motifSize = (int) processor.getMotif().size();
    newMelodyButton.setButtonText (motifSize == 0 && processor.getDna().valid ? juce::String ("Forget my DNA")
                                   : motifSize > 0 ? "Forget motif (" + juce::String (motifSize) + " notes)"
                                                 : "New melody (#" + juce::String ((int) processor.apvts.getRawParameterValue (Params::melodySeed)->load()) + ")");
    newMelodyButton.setTooltip ("Drop a MIDI clip on the plugin to use your own motif");
    melodyStyle.setEnabled (motifSize == 0);

    const auto broadcasting = processor.apvts.getRawParameterValue (Params::broadcast)->load() > 0.5f;
    broadcastStatus.setColour (juce::Label::textColourId, broadcasting ? accent : Colours::dimText);
    broadcastStatus.setText (broadcasting ? juce::String (processor.adoptedDetectedKey.load() ? "Detected key. " : "") + "Suite follows " + suite::music::keyName (settings.root, settings.scale)
                                          : "Not broadcasting: other plugins use their own key",
                             juce::dontSendNotification);
    header.setStatus (broadcasting ? "Suite follows " + suite::music::keyName (settings.root, settings.scale) : juce::String ("Not broadcasting the key"),
                      broadcasting ? Colours::moss : Colours::faintText);
}

bool CorrespondenceEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (f.endsWithIgnoreCase (".mid") || f.endsWithIgnoreCase (".midi"))
            return true;
    return false;
}

void CorrespondenceEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dropHover = false;
    juce::StringArray midiFiles;
    for (const auto& f : files) if (isInterestedInFileDrag ({ f })) midiFiles.add (f);
    dropMessage = processor.learnFromFiles (midiFiles);
    dropMessageUntil = juce::Time::getMillisecondCounterHiRes() + 6000.0;
    repaint();
}

void CorrespondenceEditor::fillPresets()
{
    presetBox.clear (juce::dontSendNotification);
    for (size_t i = 0; i < chords::presets().size(); ++i)
        presetBox.addItem (chords::presets()[i].name, (int) i + 1);
    favourites = identity::favouriteProgressions (suite::StyleLog::read());
    if (! favourites.empty())
    {
        presetBox.addSectionHeading ("Your progressions");
        for (size_t i = 0; i < std::min<size_t> (5, favourites.size()); ++i)
            presetBox.addItem ("Mine: " + favourites[i].first + " (" + juce::String (favourites[i].second) + (favourites[i].second == 1 ? " song)" : " songs)"),
                               500 + (int) i);
    }
    hooks = identity::loadHooks();
    presetBox.addSectionHeading ("Your hooks");
    for (size_t i = 0; i < hooks.size(); ++i) presetBox.addItem ("Hook: " + hooks[i].first, 600 + (int) i);
    presetBox.addItem ("Save the current hook...", 999);
}

void CorrespondenceEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);
    if (page == 0) drawPanel (g, keyArea, "Key");
    if (page == 1)
    {
        drawPanel (g, songGridArea, "Song patterns  (each section of the Ouroboros map; the pattern library's mood, style, kit and layers)", accent);
        drawPanel (g, songTilesArea, "Drag into FL  (the whole song, following the map)", Colours::amethyst);
    }
    else
    {
    drawPanel (g, progressionArea, "Progression  (click a chord to hear it)", accent);
    const auto showDrop = juce::Time::getMillisecondCounterHiRes() < dropMessageUntil;
    drawPanel (g, voicingArea, showDrop ? "Voicing & melody  (" + dropMessage + ")"
                               : ! processor.getMotif().empty() ? juce::String ("Voicing & melody  (your motif)")
                               : processor.getDna().valid ? juce::String ("Voicing & melody  (your melodic DNA)")
                                                          : juce::String ("Voicing & melody  (drop MIDI: a motif, chords, or several leads)"), accent);
    drawPanel (g, patternArea, "Rhythm", Colours::brass);
    drawPanel (g, exportArea, "Drag into FL  (loops)", Colours::amethyst);
    }
    if (dropHover)
    {
        g.setColour (Colours::ember.withAlpha (0.15f));
        g.fillRect (getLocalBounds());
        g.setColour (Colours::ember);
        g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
        g.drawText ("Drop to use this MIDI clip as the melody motif", getLocalBounds(), juce::Justification::centred);
    }
}

void CorrespondenceEditor::resized()
{
    processor.apvts.state.setProperty ("uiWidth", getWidth(), nullptr);
    processor.apvts.state.setProperty ("uiHeight", getHeight(), nullptr);
    directorStrip.setBounds (getLocalBounds().removeFromBottom (suite::ui::AutopilotStrip::height + 8).reduced (16, 4));
    auto bounds = getLocalBounds().withTrimmedBottom (suite::ui::AutopilotStrip::height + 4).reduced (16);
    header.setBounds (bounds.removeFromTop (HeaderBar::height).withTrimmedTop (-8));
    bounds.removeFromTop (10);
    const auto advanced = header.advanced();
    if (page == 0) setShown ({ &registerOct, &voiceLead, &addBass, &bassOctave, &arpOctaves, &humanise, &previewVolume }, advanced);

    // The key row on the Chords page; the Song page gives its room to the section patterns
    for (auto* c : std::initializer_list<juce::Component*> { &root, &scale, &perChord, &count, &broadcast, &acceptDetected, &broadcastStatus, &presetCaption, &presetBox })
        c->setVisible (page == 0);
    keyArea = page == 0 ? bounds.removeFromTop (112) : juce::Rectangle<int>();
    if (page == 0) { bounds.removeFromTop (10); layoutKeyRow(); }

    auto inner = [] (juce::Rectangle<int> a) { return a.reduced (10, 6).withTrimmedTop (12); };
    auto placeTiles = [] (juce::Rectangle<int> row, std::initializer_list<DragTile*> tiles)
    {
        const auto tileW = row.getWidth() / (int) tiles.size();
        for (auto* t : tiles) t->setBounds (row.removeFromLeft (tileW).reduced (3, 3));
    };

    if (page == 1)
    {
        // ---- Song page: the pattern grid, then every song clip ----
        songTilesArea = bounds.removeFromBottom (juce::jlimit (190, 320, bounds.getHeight() / 3));
        bounds.removeFromBottom (10);
        songGridArea = bounds;
        if (songGrid != nullptr) songGrid->setBounds (songGridArea.reduced (8, 6).withTrimmedTop (16));
        auto st = inner (songTilesArea);
        auto buttons = st.removeFromBottom (34);
        songExportButton.setBounds (buttons.removeFromRight (180).reduced (3, 2));
        const auto rowH = st.getHeight() / 2;
        placeTiles (st.removeFromTop (rowH), { &songChordTile, &songBassTile, &songArpTile, &songLeadTile, &songHarmonyTile, &songCounterTile, &songSubTile, &songFillsTile });
        {
            auto row = st;
            const auto w = row.getWidth() / juce::jmax (1, (int) layerTiles.size());
            for (auto& t : layerTiles) t->setBounds (row.removeFromLeft (w).reduced (3, 3));
        }
        return;
    }

    auto bottom = bounds.removeFromBottom (190);
    bounds.removeFromBottom (10);
    progressionArea = bounds;

    // ---- Progression: slots on top, piano roll below ----
    auto pr = progressionArea.reduced (12, 6).withTrimmedTop (16);
    auto slotRow = pr.removeFromTop (130);
    const auto slotW = slotRow.getWidth() / chords::maxSlots;
    for (auto& s : slots)
        s->setBounds (slotRow.removeFromLeft (slotW));
    pr.removeFromTop (8);
    roll.setBounds (pr.reduced (4, 0));

    // ---- Bottom panels ----
    const auto side = juce::jmax (330, bottom.getWidth() * 29 / 100);
    voicingArea = bottom.removeFromLeft (side);
    bottom.removeFromLeft (10);
    exportArea = bottom.removeFromRight (juce::jmax (440, bottom.getWidth() * 45 / 100));
    bottom.removeFromRight (10);
    patternArea = bottom;

    auto v = inner (voicingArea);
    auto vTop = v.removeFromTop (v.getHeight() / 2);
    const auto third = v.getWidth() / 3;
    if (advanced)
    {
        layoutRow (vTop, { &voicing, &registerOct, &melodyStyle });
        voiceLead.setBounds (v.removeFromLeft (third).withSizeKeepingCentre (third - 6, 28));
        addBass.setBounds (v.removeFromLeft (third).withSizeKeepingCentre (third - 6, 28));
        newMelodyButton.setBounds (v.withSizeKeepingCentre (third - 6, 28));
    }
    else
    {
        layoutRow (vTop, { &voicing, &melodyStyle });
        newMelodyButton.setBounds (v.withSizeKeepingCentre (juce::jmin (v.getWidth() - 12, 200), 30));
    }

    auto pa = inner (patternArea);
    auto paTop = advanced ? pa.removeFromTop (pa.getHeight() / 2) : pa.withSizeKeepingCentre (pa.getWidth(), juce::jmin (pa.getHeight(), 80));
    layoutRow (paTop, { &chordPattern, &bassPattern, &arpPattern });
    if (advanced) {
    layoutRow (pa, { &bassOctave, &arpOctaves, &humanise, &previewVolume });
    for (auto* knob : { &bassOctave, &arpOctaves, &humanise, &previewVolume })
        knob->setBounds (knob->getBounds().withSizeKeepingCentre (knob->getWidth(), knob->getHeight() + 10));
    }

    auto ex = inner (exportArea);
    auto buttons = ex.removeFromBottom (34);
    placeTiles (ex.withTrimmedBottom (6), { &chordTile, &bassTile, &arpTile, &melodyTile });
    const auto bw = buttons.getWidth() / 5;
    playButton.setBounds (buttons.removeFromLeft (bw).reduced (3, 0));
    colourButton.setBounds (buttons.removeFromLeft (bw).reduced (3, 0));
    varyButton.setBounds (buttons.removeFromLeft (bw).reduced (3, 0));
    saveButton.setBounds (buttons.removeFromLeft (bw).reduced (3, 0));
    songButton.setBounds (buttons.reduced (3, 0));
}

void CorrespondenceEditor::layoutKeyRow()
{
    // ---- Key row ----
    auto k = keyArea.reduced (12, 6).withTrimmedTop (12);
    const auto cell = k.getWidth() / 8;
    root.setBounds (k.removeFromLeft (cell));
    scale.setBounds (k.removeFromLeft (cell + 30));
    auto presetCell = k.removeFromLeft (cell * 2 + 20);
    presetCaption.setBounds (presetCell.removeFromTop (16));
    presetBox.setBounds (presetCell.withSizeKeepingCentre (presetCell.getWidth() - 16, 26));
    count.setBounds (k.removeFromLeft (cell - 20));
    perChord.setBounds (k.removeFromLeft (cell));
    auto bc = k;
    auto toggles = bc.removeFromTop (bc.getHeight() / 2).translated (0, 8);
    broadcast.setBounds (toggles.removeFromLeft (toggles.getWidth() / 2).reduced (3, 4));
    acceptDetected.setBounds (toggles.reduced (3, 4));
    broadcastStatus.setBounds (bc.reduced (4, 0));
}

//==============================================================================
// The Write menu: locks and takes, hearing a part in context, the hook doctor, humming, toplines,
// originality, the emotional arc, harmony moves, a reference's feel, the shared rhythm, why it works
void CorrespondenceEditor::showWriteMenu()
{
    auto& p = processor;
    static const juce::StringArray kinds { "Song Lead", "Song Bass", "Song Arp", "Song Chords", "Melody" };
    suite::SectionMap map;
    const auto haveMap = suite::SectionBus::read (map) && map.count > 0;
    juce::PopupMenu m;
    for (int k = 0; k < kinds.size(); ++k)
    {
        const auto& kind = kinds[k];
        if (kind.startsWith ("Song ") && ! haveMap) continue;
        const auto base = 10000 + k * 1000;
        juce::PopupMenu part, lock, keep;
        const auto& h = p.takes[kind];
        part.addItem (base + 1, "Regenerate (locked bars kept)");
        if (kind.startsWith ("Song ")) part.addItem (base + 2, "Regenerate only this part (the others locked as they are)");
        part.addItem (base + 3, "Take back" + (h.current > 0 ? " (to take " + juce::String (h.current) + ")" : juce::String()), h.current > 0);
        part.addItem (base + 4, "Take forward", h.current + 1 < (int) h.takes.size());
        part.addItem (base + 5, "I like this take", true, h.now() != nullptr && h.now()->rating > 0);
        part.addItem (base + 6, "Not this one", true, h.now() != nullptr && h.now()->rating < 0);
        // locks: by section (song parts) or the whole loop
        const auto lk = p.lockOf (kind);
        if (haveMap && kind.startsWith ("Song "))
            for (int i = 0; i < map.count && i < 40; ++i)
            {
                const auto a = map.sections[(size_t) i].startBar - 1, b = map.endBar (i) - 2;
                const auto locked = lk.bars.count (a) > 0;
                lock.addItem (base + 100 + i, (locked ? "Unlock " : "Lock ") + juce::String (suite::sectionName (map.sections[(size_t) i].type)) + " (bars " + juce::String (a + 1) + "-" + juce::String (b + 1) + ")", true, locked);
            }
        else for (int b = 0; b < 4; ++b) lock.addItem (base + 100 + b, (lk.bars.count (b) ? "Unlock bar " : "Lock bar ") + juce::String (b + 1), true, lk.bars.count (b) > 0);
        lock.addItem (base + 99, "Unlock everything", ! lk.empty());
        part.addSubMenu ("Lock bars (" + juce::String ((int) lk.bars.size()) + " locked)", lock);
        for (int t = 0; t < (int) h.takes.size() && t < 12; ++t)
        {
            juce::PopupMenu which;
            if (haveMap && kind.startsWith ("Song "))
                for (int i = 0; i < map.count && i < 40; ++i)
                    which.addItem (base + 200 + t * 50 + i, juce::String (suite::sectionName (map.sections[(size_t) i].type)) + " at bar " + juce::String (map.sections[(size_t) i].startBar));
            else which.addItem (base + 200 + t * 50, "Bars 1-2");
            keep.addSubMenu ("Take " + juce::String (t + 1) + (h.takes[(size_t) t].rating > 0 ? " (liked)" : h.takes[(size_t) t].rating < 0 ? " (not this)" : ""), which);
        }
        part.addSubMenu ("Keep bars from a take", keep, ! h.takes.empty());
        part.addItem (base + 7, "Hear it in context (in time with FL, under the song)", true, p.contextKind == kind);
        part.addItem (base + 8, "Why it works...");
        m.addSubMenu (kind, part);
    }
    m.addItem (2, "Stop hearing in context", p.contextKind.isNotEmpty());
    m.addSeparator();
    juce::PopupMenu doctor;
    const auto report = p.hookReport();
    doctor.addItem (20, "The hook doctor's report (" + juce::String (juce::roundToInt (report.score * 100.0f)) + "/100)...");
    for (size_t i = 0; i < report.edits.size(); ++i)
        doctor.addItem (21 + (int) i, "Change note " + juce::String (report.edits[i].index + 1) + " to " + suite::music::noteNames()[(report.edits[i].to % 12 + 12) % 12]
                        + ": +" + juce::String (juce::roundToInt (report.edits[i].gain * 100.0f)) + " (" + report.edits[i].why + ")");
    m.addSubMenu ("Hook doctor", doctor);
    m.addItem (30, p.hum.armed.load() ? "Hum a hook: stop and use it" : "Hum a hook (into this track's input)...");
    m.addItem (31, "Topline from lyrics...");
    m.addItem (32, "Originality check (your hooks and the library)...");
    juce::PopupMenu arc;
    const auto plans = chords::tools::arcPlans (p.currentSettings());
    for (size_t i = 0; i < plans.size(); ++i) arc.addItem (40 + (int) i, plans[i].name + ": " + plans[i].story);
    arc.addItem (49, "Undo the arc", ! p.beforeVariation.empty() || p.beforeIdea.has_value());
    m.addSubMenu ("Emotional arc", arc);
    juce::PopupMenu moves;
    const auto mv = chords::tools::harmonyMoves (p.currentSettings());
    for (size_t i = 0; i < mv.size() && i < 20; ++i) moves.addItem (50 + (int) i, mv[i].name);
    moves.addItem (79, "Undo the last harmony change", p.beforeIdea.has_value());
    m.addSubMenu ("Trance harmony moves", moves);
    m.addSeparator();
    m.addItem (80, "One motif, the whole song (every section's lead from the hook)", true, p.option ("motifPlan"));
    m.addItem (81, "Shared rhythm (the bass and arp lean on the hook's rhythm)", true, p.option ("rhythmCell"));
    m.addItem (82, "Feel from a reference (swing and accents from an audio file)...");
    m.addItem (83, "The reference's feel on the bass, arp and drums", p.feel().valid, p.option ("feelOn"));

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&writeButton), [this, map, haveMap, report, plans, mv] (int id)
    {
        auto& p = processor;
        auto note = [this] (const juce::String& text) { juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "Write", text); };
        if (id >= 10000)
        {
            const auto k = (id - 10000) / 1000, sub = (id - 10000) % 1000;
            if (k < 0 || k >= kinds.size()) return;
            const auto& kind = kinds[k];
            if (sub == 1 || sub == 2) { const auto t = p.regenerate (kind, sub == 2); setTooltipNote (t); }
            else if (sub == 3) p.takeBack (kind);
            else if (sub == 4) p.takeForward (kind);
            else if (sub == 5 || sub == 6) p.rateTake (kind, sub == 5);
            else if (sub == 7) p.playInContext (p.contextKind == kind ? juce::String() : kind);
            else if (sub == 8)
            {
                const auto notes = kind.endsWith ("Lead") || kind == "Melody" ? p.currentHook() : chords::tools::window (p.clipNow (kind), 0.0, 32.0);
                const auto why = chords::tools::explain (notes, p.currentSettings());
                note (why.isEmpty() ? juce::String ("Not enough notes to say.") : "Why it works:\n\n- " + why.joinIntoString ("\n- "));
            }
            else if (sub == 99) p.setLock (kind, {});
            else if (sub >= 100 && sub < 200)
            {
                const auto i = sub - 100;
                int a = i, b = i;
                if (haveMap && kind.startsWith ("Song ") && i < map.count) { a = map.sections[(size_t) i].startBar - 1; b = map.endBar (i) - 2; }
                if (p.lockOf (kind).bars.count (a)) p.unlockBars (kind, a, b); else p.lockBars (kind, a, b);
            }
            else if (sub >= 200)
            {
                const auto t = (sub - 200) / 50, i = (sub - 200) % 50;
                int a = 0, b = 1;
                if (haveMap && kind.startsWith ("Song ") && i < map.count) { a = map.sections[(size_t) i].startBar - 1; b = map.endBar (i) - 2; }
                p.keepBarsFrom (kind, t, a, b);
            }
            return;
        }
        if (id == 2) p.playInContext ({});
        else if (id == 20)
        {
            juce::String text = "Hook score: " + juce::String (juce::roundToInt (report.score * 100.0f)) + "/100\n\n- " + report.notes.joinIntoString ("\n- ");
            if (! report.edits.empty())
            {
                text << "\n\nOne-note changes that would help (Write > Hook doctor to apply):";
                for (const auto& e : report.edits) text << "\n- note " << (e.index + 1) << " to " << suite::music::noteNames()[(e.to % 12 + 12) % 12] << ": " << e.why;
            }
            note (text);
        }
        else if (id >= 21 && id < 21 + (int) report.edits.size()) p.applyHookEdit (report.edits[(size_t) (id - 21)]);
        else if (id == 30)
        {
            if (p.hum.armed.load()) note (p.finishHum());
            else { p.startHum(); note ("Listening to this track's input. Play the song (or not) and hum or sing the hook clearly, one note at a time. "
                                      "Then Write > Hum a hook: stop and use it (it stops by itself after 30 seconds)."); }
        }
        else if (id == 31) showToplineDialog();
        else if (id == 32)
        {
            const auto near = p.originality();
            if (near.empty()) note ("The hook doesn't come close to any of your saved hooks or the library's leads: it's yours.");
            else
            {
                juce::StringArray l;
                for (size_t i = 0; i < near.size() && i < 6; ++i) l.add (near[i].name + ": " + juce::String (juce::roundToInt (near[i].similarity * 100.0f)) + "% alike");
                note ("Close to:\n\n- " + l.joinIntoString ("\n- ") + "\n\nChange the rhythm or a few intervals (the hook doctor can suggest one-note changes) to make it its own.");
            }
        }
        else if (id >= 40 && id < 40 + (int) plans.size())
        {
            p.applyArc (plans[(size_t) (id - 40)]);
            note (plans[(size_t) (id - 40)].name + "\n\n" + chords::tools::arcChords (p.currentSettings(), plans[(size_t) (id - 40)]) + "\n\nWhy:\n- " + plans[(size_t) (id - 40)].why.joinIntoString ("\n- "));
        }
        else if (id == 49) { p.undoVariation(); p.undoHarmonyIdea(); }
        else if (id >= 50 && id < 50 + (int) mv.size())
        {
            p.applyMove (mv[(size_t) (id - 50)]);
            note (mv[(size_t) (id - 50)].name + "\n\n" + mv[(size_t) (id - 50)].why + "\n\n(Play to hear it; Write > Trance harmony moves > Undo to put it back.)");
        }
        else if (id == 79) p.undoHarmonyIdea();
        else if (id == 80) p.setOption ("motifPlan", ! p.option ("motifPlan"));
        else if (id == 81) p.setOption ("rhythmCell", ! p.option ("rhythmCell"));
        else if (id == 82)
        {
            chooser = std::make_unique<juce::FileChooser> ("A reference track (audio)", juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac;*.mp3");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
            {
                if (fc.getResult().existsAsFile())
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "Feel from a reference", processor.loadFeel (fc.getResult()));
            });
        }
        else if (id == 83) p.setOption ("feelOn", ! p.option ("feelOn"));
    });
}

void CorrespondenceEditor::setTooltipNote (const juce::String& text)
{
    writeButton.setTooltip (text);
    writeButton.setButtonText ("Write...");
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "Write", text + "\n\nWrite > the part > Hear it in context, to hear it in the song.");
}

void CorrespondenceEditor::showToplineDialog()
{
    auto* aw = new juce::AlertWindow ("Topline from lyrics", "Type a line or two of lyrics. Three melodies come back, each fitted to the syllables and stresses over your chords.", juce::MessageBoxIconType::NoIcon);
    aw->addTextEditor ("lyric", "", "Lyrics:");
    aw->addComboBox ("bars", { "2 bars", "4 bars", "8 bars" }, "Length:");
    if (auto* c = aw->getComboBoxComponent ("bars")) c->setSelectedItemIndex (1);
    aw->addButton ("Write", 1); aw->addButton ("Cancel", 0);
    aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, aw] (int r)
    {
        if (r != 1) return;
        const auto lyric = aw->getTextEditorContents ("lyric");
        const auto barsIdx = aw->getComboBoxComponent ("bars") != nullptr ? aw->getComboBoxComponent ("bars")->getSelectedItemIndex() : 1;
        const auto bars = barsIdx == 0 ? 2 : barsIdx == 2 ? 8 : 4;
        lastToplines = processor.toplines (lyric, bars);
        if (lastToplines.empty()) { juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "Topline", "No syllables found in that."); return; }
        juce::PopupMenu pick;
        for (size_t i = 0; i < lastToplines.size(); ++i)
        {
            const auto& t = lastToplines[i];
            pick.addItem (1 + (int) i, "Topline " + juce::String ((int) i + 1) + ": " + t.shape + ", singable " + juce::String (juce::roundToInt (t.singable * 100.0f)) + "% - save as MIDI (with the lyrics)...");
            pick.addItem (11 + (int) i, "Topline " + juce::String ((int) i + 1) + ": use it as Melodia's hook");
        }
        pick.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&writeButton), [this] (int k)
        {
            if (k >= 11 && k - 11 < (int) lastToplines.size()) { processor.useHook (lastToplines[(size_t) (k - 11)].notes); return; }
            if (k < 1 || k > (int) lastToplines.size()) return;
            const auto t = lastToplines[(size_t) (k - 1)];
            chooser = std::make_unique<juce::FileChooser> ("Where to save the topline", juce::File::getSpecialLocation (juce::File::userMusicDirectory));
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this, t] (const juce::FileChooser& fc)
            {
                if (! fc.getResult().isDirectory()) return;
                const auto f = processor.exportTopline (t, fc.getResult(), "Topline - " + t.lyric.joinIntoString ("").replace ("-", "").substring (0, 40));
                f.revealToUser();
            });
        });
    }), true);
}

std::vector<suite::ui::Action> CorrespondenceEditor::actions()
{
    std::vector<suite::ui::Action> a;
    a.push_back ({ "Write...", "locks and takes, hear it in context, the hook doctor, hum a hook, topline from lyrics, originality, the arc, harmony moves, feel",
                   "Write", [this] { showWriteMenu(); } });
    a.push_back ({ "Chords page", "key, progression, voicing and rhythm, the loop clips", "View", [this] { showPage (0); } });
    a.push_back ({ "Song page", "patterns per section and every song clip", "View", [this] { showPage (1); } });
    a.push_back ({ playing ? "Stop" : "Play the progression", "", "Listen", [this] { playButton.triggerClick(); } });
    a.push_back ({ "Colour...", "re-harmonise and add colour to the chords", "Chords", [this] { colourButton.triggerClick(); } });
    a.push_back ({ "Vary...", "variations of the progression and the melody", "Chords", [this] { varyButton.triggerClick(); } });
    a.push_back ({ "New melody", "", "Melody", [this] { newMelodyButton.triggerClick(); }, newMelodyButton.isEnabled() });
    a.push_back ({ "Export all...", "every clip, the markers and a song pack", "Export", [this] { saveButton.triggerClick(); } });
    a.push_back ({ "Song patterns...", "", "Song", [this] { songButton.triggerClick(); } });
    for (int i = 0; i < presetBox.getNumItems(); ++i)
        if (const auto id = presetBox.getItemId (i); id > 0)
            a.push_back ({ presetBox.getItemText (i), "", "Presets", [this, id] { presetBox.setSelectedId (id); } });
    return a;
}
