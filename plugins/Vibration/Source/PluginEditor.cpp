#include "PluginEditor.h"

using namespace suite::ui;

namespace
{
    const juce::Colour accent = Colours::brass;

    juce::String signedDb (float v) { return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB"; }
}

VibrationEditor::VibrationEditor (VibrationProcessor& p)
    : AudioProcessorEditor (p), processor (p),
      euphoria (p.apvts, Params::euphoria, "EUPHORIA", accent),
      autoGain (p.apvts, Params::autoGain, "Auto gain", Colours::verdigris),
      output (p.apvts, Params::output, "Output", Colours::text),
      spectrum ({ { &p.inputTap, Colours::dimText, false }, { &p.outputTap, accent, true } }),
      viewTabs ({ "SPECTRUM", "SCENES" }, { accent, Colours::ember }),
      lowCut (p.apvts, Params::lowCut, "Low cut", Colours::verdigris),
      mud (p.apvts, Params::mud, "Mud 320 Hz", Colours::verdigris),
      presence (p.apvts, Params::presence, "Presence 3.2k", Colours::verdigris),
      air (p.apvts, Params::air, "Air 10k", Colours::verdigris),
      drive (p.apvts, Params::drive, "Drive", Colours::ember),
      satMix (p.apvts, Params::satMix, "Mix", Colours::ember),
      mbDepth (p.apvts, Params::mbDepth, "Depth", Colours::amethyst),
      mbTime (p.apvts, Params::mbTime, "Time", Colours::amethyst),
      width (p.apvts, Params::width, "Width", Colours::moss),
      monoBelow (p.apvts, Params::monoBelow, "Mono below", Colours::moss),
      pitchBright (p.apvts, Params::pitchBright, "Pitch bright", Colours::verdigris),
      tension (p.apvts, Params::tensionWidth, "Tension width", Colours::moss)
{
    setLookAndFeel (&lnf);

    euphoria.maxHeight = 200;
    euphoria.caption.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    euphoria.caption.setColour (juce::Label::textColourId, accent);

    presetBox.setTextWhenNothingSelected ("Preset...");
    for (size_t i = 0; i < Params::presets().size(); ++i)
        presetBox.addItem (Params::presets()[i].name, (int) i + 1);
    presetBox.onChange = [this] { if (presetBox.getSelectedId() > 0) processor.applyPreset (presetBox.getSelectedId() - 1); };

    for (auto* l : { &macroInfo, &autoGainInfo })
    {
        l->setFont (juce::FontOptions (12.0f));
        l->setJustificationType (juce::Justification::centredLeft);
    }

    for (auto* c : std::initializer_list<juce::Component*> { &euphoria, &presetBox, &macroInfo, &autoGainInfo, &autoGain, &output,
                                                             &spectrum, &lowCut, &mud, &presence, &air, &drive, &satMix,
                                                             &mbDepth, &mbTime, &width, &monoBelow, &pitchBright, &tension })
        addAndMakeVisible (c);

    scenePanel = std::make_unique<suite::ui::ScenePanel> (p.scenes, p.apvts);
    addAndMakeVisible (*scenePanel);
    addAndMakeVisible (viewTabs);
    viewTabs.setComponentID ("tabs");
    viewTabs.onChange = [this] (int i) { spectrum.setVisible (i == 0); scenePanel->setVisible (i == 1); repaint(); };
    viewTabs.onChange (0);

    fingerprintButton.setTooltip ("Save this lead's sound as your fingerprint, or match a new lead to one you saved");
    fingerprintButton.onClick = [this] { showFingerprintMenu(); };
    fingerprintStatus.setFont (juce::FontOptions (12.0f));
    fingerprintStatus.setJustificationType (juce::Justification::centredRight);
    fingerprintStatus.setColour (juce::Label::textColourId, Colours::dimText);
    addAndMakeVisible (fingerprintButton);
    addAndMakeVisible (fingerprintStatus);
    setSize (1200, 660);
    timerCallback();
    startTimerHz (30);
}

VibrationEditor::~VibrationEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void VibrationEditor::timerCallback()
{
    spectrum.setSampleRate (processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0);
    spectrum.update();

    // Show what the macro is actually applying on top of the manual knobs.
    const auto e = processor.effective();
    macroInfo.setText ("Applied: air " + signedDb (e.air) + juce::String::fromUTF8 (" \xc2\xb7 presence ") + signedDb (e.presence)
                           + juce::String::fromUTF8 (" \xc2\xb7 mud ") + signedDb (e.mud) + "\n"
                           + "drive " + signedDb (e.drive) + juce::String::fromUTF8 (" \xc2\xb7 multiband ")
                           + juce::String (juce::roundToInt (e.mbDepth * 100.0f)) + "%"
                           + juce::String::fromUTF8 (" \xc2\xb7 width ") + juce::String (juce::roundToInt (e.width * 100.0f)) + "%",
                       juce::dontSendNotification);
    autoGainInfo.setText ("Level matched: " + signedDb (processor.autoGainDb.load()), juce::dontSendNotification);
    suite::ui::updateSceneGhosts (*this, processor.scenes);
}

void VibrationEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);
    drawHeader (g, { 20, 12, 700, 32 }, "VIBRATION", "lead polish strip", accent);
    drawPanel (g, macroArea, "Macro", accent);
    if (spectrum.isVisible())
        drawPanel (g, spectrumArea, "Spectrum  (grey = in, gold = out)");
    drawPanel (g, eqArea, "Tone", Colours::verdigris);
    drawPanel (g, satArea, "Saturation", Colours::ember);
    drawPanel (g, mbArea, "Multiband", Colours::amethyst);
    drawPanel (g, widthArea, "Width", Colours::moss);
}

void VibrationEditor::showFingerprintMenu()
{
    const auto saved = fingerprint::loadAll (fingerprint::folder());
    juce::PopupMenu m;
    m.addItem (1, processor.fingerprintReady() ? "Save this lead's fingerprint..." : "Save this lead's fingerprint (play it for 5 s first)",
               processor.fingerprintReady());
    if (! saved.empty()) m.addSeparator();
    for (size_t i = 0; i < saved.size(); ++i) m.addItem (100 + (int) i, "Match: " + saved[i].name);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&fingerprintButton), [this, saved] (int id)
    {
        if (id >= 100 && id - 100 < (int) saved.size())
            fingerprintStatus.setText (processor.matchFingerprint (saved[(size_t) (id - 100)]), juce::dontSendNotification);
        else if (id == 1)
        {
            nameWindow = std::make_unique<juce::AlertWindow> ("Lead fingerprint", "Name this lead sound:", juce::MessageBoxIconType::NoIcon);
            nameWindow->addTextEditor ("name", "My lead");
            nameWindow->addButton ("Save", 1);
            nameWindow->addButton ("Cancel", 0);
            nameWindow->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int result)
            {
                if (result == 1 && nameWindow != nullptr)
                {
                    const auto name = nameWindow->getTextEditorContents ("name").trim();
                    fingerprintStatus.setText (processor.saveFingerprint (fingerprint::folder(), name) ? "Saved '" + name + "'"
                                                                                                    : juce::String ("Couldn't save it"),
                                               juce::dontSendNotification);
                }
            }));
        }
    });
}

void VibrationEditor::resized()
{
    auto bounds = getLocalBounds().reduced (16);
    {
        auto header = bounds.removeFromTop (48);
        fingerprintButton.setBounds (header.removeFromRight (170).reduced (4, 8));
        fingerprintStatus.setBounds (header.removeFromRight (420).reduced (4, 8));
    }

    auto bottom = bounds.removeFromBottom (190);
    bounds.removeFromBottom (12);

    macroArea = bounds.removeFromLeft (330);
    bounds.removeFromLeft (12);
    spectrumArea = bounds;

    auto m = macroArea.reduced (14, 8).withTrimmedTop (14);
    presetBox.setBounds (m.removeFromTop (28));
    m.removeFromTop (6);
    euphoria.setBounds (m.removeFromTop (180));
    macroInfo.setBounds (m.removeFromTop (40));
    auto row = m;
    output.setBounds (row.removeFromRight (110));
    autoGain.setBounds (row.removeFromTop (30).reduced (4, 2));
    autoGainInfo.setBounds (row);

    viewTabs.setBounds (spectrumArea.removeFromTop (30).withTrimmedLeft (200));
    spectrumArea.removeFromTop (10);
    spectrum.setBounds (spectrumArea.reduced (12, 8).withTrimmedTop (16));
    if (scenePanel != nullptr) scenePanel->setBounds (spectrumArea);

    eqArea = bottom.removeFromLeft (bottom.getWidth() * 4 / 10);
    bottom.removeFromLeft (10);
    const auto third = (bottom.getWidth() - 20) / 3;
    satArea = bottom.removeFromLeft (third);
    bottom.removeFromLeft (10);
    mbArea = bottom.removeFromLeft (third);
    bottom.removeFromLeft (10);
    widthArea = bottom;

    auto inner = [] (juce::Rectangle<int> a) { return a.reduced (8, 6).withTrimmedTop (14); };
    layoutRow (inner (eqArea), { &lowCut, &mud, &presence, &air, &pitchBright });
    layoutRow (inner (satArea), { &drive, &satMix });
    layoutRow (inner (mbArea), { &mbDepth, &mbTime });
    layoutRow (inner (widthArea), { &width, &monoBelow, &tension });
}
