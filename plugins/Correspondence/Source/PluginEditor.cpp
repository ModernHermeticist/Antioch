#include "PluginEditor.h"

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
      melodyStyle (p.apvts, Params::melodyStyle, "Melody")
{
    setLookAndFeel (&lnf);

    presetCaption.setText ("Progression preset", juce::dontSendNotification);
    presetCaption.setJustificationType (juce::Justification::centred);
    presetCaption.setFont (juce::FontOptions (12.0f));
    presetBox.setTextWhenNothingSelected ("Choose a preset...");
    for (size_t i = 0; i < chords::presets().size(); ++i)
        presetBox.addItem (chords::presets()[i].name, (int) i + 1);
    presetBox.onChange = [this]
    {
        if (presetBox.getSelectedId() > 0)
            processor.applyPreset (presetBox.getSelectedId() - 1);
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

    songButton.onClick = [this] { showSongPatterns(); };
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
        chooser = std::make_unique<juce::FileChooser> ("Choose a folder for the MIDI clips",
                                                       juce::File::getSpecialLocation (juce::File::userMusicDirectory));
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this] (const juce::FileChooser& fc)
                              {
                                  const auto dir = fc.getResult();
                                  if (dir == juce::File()) return;
                                  processor.exportEverything (dir).revealToUser();
                              });
    };

    for (auto* c : std::initializer_list<juce::Component*> { &root, &scale, &perChord, &count, &broadcast, &acceptDetected, &broadcastStatus,
                                                             &presetCaption, &presetBox, &roll, &voicing, &chordPattern,
                                                             &bassPattern, &arpPattern, &voiceLead, &addBass, &registerOct,
                                                             &bassOctave, &arpOctaves, &previewVolume, &humanise, &chordTile, &bassTile,
                                                             &arpTile, &songChordTile, &songBassTile, &songArpTile,
                                                             &melodyTile, &songLeadTile, &melodyStyle, &newMelodyButton,
                                                             &playButton, &saveButton, &songButton })
        addAndMakeVisible (c);

    setSize (1160, 740);
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
                     { &ghost, &ghostAttachment, "Ghost notes", Params::songGhost, "Soft in-between 16ths in the bass and arp, more where the energy is higher" },
                     { &fills, &fillsAttachment, "Drum fills", Params::songFills, "A SONG FILLS clip (General MIDI drums) for every section change and phrase end" },
                     { &vocalRoom, &vocalRoomAttachment, "Lead leaves room for the vocal", Params::songVocalRoom,
                       "Where the vocal sings (heard by Solve et Coagula), the lead keeps only beats 1 and 3 and the harmony steps out" } })
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
            breakdownBox = std::make_unique<juce::ComboBox>();
            breakdownBox->addItemList (state.getParameter (Params::songBreakdown)->getAllValueStrings(), 1);
            breakdownBox->setTooltip ("Breakdowns can start the progression on VI or IV, so the drop's return to i lands harder");
            breakdownAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, Params::songBreakdown, *breakdownBox);
            addAndMakeVisible (*breakdownBox);
            hint.setFont (juce::FontOptions (12.0f));
            addAndMakeVisible (hint);
            setSize (1200, 430);
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
                const auto w = more.getWidth() / 4;
                climb->setBounds (more.removeFromLeft (w));
                ghost->setBounds (more.removeFromLeft (w));
                fills->setBounds (more.removeFromLeft (w));
                vocalRoom->setBounds (more);
            }
            {
                auto toggles = r.removeFromBottom (28);
                breakdownBox->setBounds (toggles.removeFromRight (170).reduced (2));
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
        std::unique_ptr<juce::ToggleButton> lift, vary, counter, phrase, climb, ghost, fills, vocalRoom;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> climbAttachment, ghostAttachment, fillsAttachment, vocalRoomAttachment;
        std::unique_ptr<juce::ComboBox> leadBox;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> leadAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> liftAttachment, varyAttachment, counterAttachment, phraseAttachment;
        std::unique_ptr<juce::ComboBox> breakdownBox, grooveBox;
        std::unique_ptr<juce::ToggleButton> accent;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> grooveAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> accentAttachment;
        juce::TextEditor title, artist;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> breakdownAttachment;
    };
}

void CorrespondenceEditor::showSongPatterns()
{
    auto grid = std::make_unique<SongPatternGrid> (processor.apvts);
    juce::CallOutBox::launchAsynchronously (std::move (grid), songButton.getScreenBounds(), nullptr);
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

void CorrespondenceEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);
    drawHeader (g, { 20, 12, 700, 32 }, "CORRESPONDENCE", "key & chord engine", accent);
    drawPanel (g, keyArea, "Key");
    drawPanel (g, progressionArea, "Progression  (click a chord to hear it)", accent);
    const auto showDrop = juce::Time::getMillisecondCounterHiRes() < dropMessageUntil;
    drawPanel (g, voicingArea, showDrop ? "Voicing & melody  (" + dropMessage + ")"
                               : ! processor.getMotif().empty() ? juce::String ("Voicing & melody  (your motif)")
                               : processor.getDna().valid ? juce::String ("Voicing & melody  (your melodic DNA)")
                                                          : juce::String ("Voicing & melody  (drop MIDI: a motif, chords, or several leads)"), accent);
    drawPanel (g, patternArea, "Rhythm", Colours::brass);
    drawPanel (g, exportArea, "Drag into FL  (loop / whole song)", Colours::amethyst);
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
    auto bounds = getLocalBounds().reduced (16);
    bounds.removeFromTop (48);

    keyArea = bounds.removeFromTop (112);
    bounds.removeFromTop (10);
    auto bottom = bounds.removeFromBottom (190);
    bounds.removeFromBottom (10);
    progressionArea = bounds;

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

    // ---- Progression: slots on top, piano roll below ----
    auto pr = progressionArea.reduced (12, 6).withTrimmedTop (16);
    auto slotRow = pr.removeFromTop (130);
    const auto slotW = slotRow.getWidth() / chords::maxSlots;
    for (auto& s : slots)
        s->setBounds (slotRow.removeFromLeft (slotW));
    pr.removeFromTop (8);
    roll.setBounds (pr.reduced (4, 0));

    // ---- Bottom panels ----
    voicingArea = bottom.removeFromLeft (330);
    bottom.removeFromLeft (10);
    exportArea = bottom.removeFromRight (440);
    bottom.removeFromRight (10);
    patternArea = bottom;

    auto inner = [] (juce::Rectangle<int> a) { return a.reduced (10, 6).withTrimmedTop (12); };

    auto v = inner (voicingArea);
    auto vTop = v.removeFromTop (v.getHeight() / 2);
    layoutRow (vTop, { &voicing, &registerOct, &melodyStyle });
    const auto third = v.getWidth() / 3;
    voiceLead.setBounds (v.removeFromLeft (third).withSizeKeepingCentre (third - 6, 28));
    addBass.setBounds (v.removeFromLeft (third).withSizeKeepingCentre (third - 6, 28));
    newMelodyButton.setBounds (v.withSizeKeepingCentre (third - 6, 28));

    auto pa = inner (patternArea);
    auto paTop = pa.removeFromTop (pa.getHeight() / 2);
    layoutRow (paTop, { &chordPattern, &bassPattern, &arpPattern });
    layoutRow (pa, { &bassOctave, &arpOctaves, &humanise, &previewVolume });
    for (auto* knob : { &bassOctave, &arpOctaves, &humanise, &previewVolume })
        knob->setBounds (knob->getBounds().withSizeKeepingCentre (knob->getWidth(), knob->getHeight() + 10));

    auto ex = inner (exportArea);
    const auto tileRow = (ex.getHeight() - 36) / 2;
    auto placeTiles = [&] (juce::Rectangle<int> row, std::initializer_list<DragTile*> tiles)
    {
        const auto tileW = row.getWidth() / (int) tiles.size();
        for (auto* t : tiles) t->setBounds (row.removeFromLeft (tileW).reduced (2, 2));
    };
    placeTiles (ex.removeFromTop (tileRow), { &chordTile, &bassTile, &arpTile, &melodyTile });
    placeTiles (ex.removeFromTop (tileRow), { &songChordTile, &songBassTile, &songArpTile, &songLeadTile });
    auto buttons = ex.withTrimmedTop (4);
    const auto bw = buttons.getWidth() / 3;
    playButton.setBounds (buttons.removeFromLeft (bw).reduced (3, 0));
    saveButton.setBounds (buttons.removeFromLeft (bw).reduced (3, 0));
    songButton.setBounds (buttons.reduced (3, 0));
}
