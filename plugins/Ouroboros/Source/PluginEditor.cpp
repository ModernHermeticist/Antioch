#include "PluginEditor.h"
#include "DirectorPanel.h"
#include "LabelReadyPanel.h"
#include "Delivery.h"
#include "AutopilotBus.h"
#include "TransmuteBus.h"
#include "../../Transmutation/Source/Effects.h"
#include "AutomationBus.h"
#include "Alias.h"
#include "SceneUI.h"
#include "Music.h"

using namespace suite::ui;
using suite::SectionType;
using suite::ui::sectionColour;

namespace
{
    constexpr int numTypes = (int) SectionType::count;
    const juce::Colour accent = Colours::ember;
}

//==============================================================================
Timeline::Timeline (OuroborosProcessor& p) : processor (p) {}

float Timeline::xForBar (double bar) const
{
    const auto total = juce::jmax (1, processor.getMap().totalBars);
    return (float) ((bar - 1.0) / total) * (float) getWidth();
}

double Timeline::barForX (float x) const
{
    const auto total = juce::jmax (1, processor.getMap().totalBars);
    return 1.0 + (double) x / juce::jmax (1, getWidth()) * total;
}

int Timeline::boundaryNear (float x) const
{
    const auto m = processor.getMap();
    for (int i = 1; i < m.count; ++i)
        if (std::abs (xForBar (m.sections[(size_t) i].startBar) - x) < 6.0f)
            return i;
    return -1;
}

void Timeline::paint (juce::Graphics& g)
{
    auto m = preview.has_value() ? *preview : processor.getMap();
    m.finalLift = (int) processor.apvts.getRawParameterValue (OuroborosProcessor::liftId)->load();
    auto r = getLocalBounds().toFloat();
    auto ruler = r.removeFromBottom (18.0f);
    g.setColour (Colours::background);
    g.fillRoundedRectangle (r, 6.0f);

    for (int i = 0; i < m.count; ++i)
    {
        const auto& s = m.sections[(size_t) i];
        const auto x0 = xForBar (s.startBar), x1 = xForBar (m.endBar (i));
        const auto block = juce::Rectangle<float>::leftTopRightBottom (x0, r.getY(), x1, r.getBottom()).reduced (1.0f, 4.0f);
        const auto c = sectionColour (s.type);
        const auto isSel = i == selected && ! preview.has_value();
        g.setColour (c.withAlpha (isSel ? 0.55f : 0.32f));
        g.fillRoundedRectangle (block, 4.0f);
        g.setColour (isSel ? Colours::text : c);
        if (preview.has_value())
        {
            juce::Path outline;
            outline.addRoundedRectangle (block, 4.0f);
            juce::Path dashed;
            const float dashes[] = { 6.0f, 4.0f };
            juce::PathStrokeType (1.5f).createDashedStroke (dashed, outline, dashes, 2);
            g.fillPath (dashed);
        }
        else
        {
            g.drawRoundedRectangle (block, 4.0f, isSel ? 2.0f : 1.0f);
        }

        if (block.getWidth() > 34.0f)
        {
            g.setColour (Colours::text);
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawFittedText (juce::String (suite::sectionName (s.type)).toUpperCase(), block.reduced (4.0f, 6.0f).toNearestInt(),
                              juce::Justification::centredTop, 1, 0.6f);
            g.setColour (Colours::dimText);
            g.setFont (juce::FontOptions (11.0f));
            g.drawFittedText (juce::String (m.endBar (i) - s.startBar) + " bars", block.reduced (4.0f, 6.0f).toNearestInt(),
                              juce::Justification::centredBottom, 1, 0.6f);
            if (const auto shift = m.keyShiftOf (i); shift != 0)
            {
                g.setColour (Colours::brass);
                g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
                g.drawFittedText ("KEY +" + juce::String (shift), block.reduced (4.0f, 6.0f).toNearestInt(), juce::Justification::centred, 1, 0.6f);
            }
        }
    }

    if (preview.has_value())
    {
        g.setColour (Colours::text);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText ("PROPOSAL FROM LISTENING: Apply or Discard", r.reduced (8.0f, 4.0f), juce::Justification::bottomRight);
    }

    // Energy curve: a line per section at its energy, joined with ramps; drag a line to change it.
    {
        juce::Path curve;
        for (int i = 0; i < m.count; ++i)
        {
            const auto x0 = xForBar (m.sections[(size_t) i].startBar) + 4.0f, x1 = xForBar (m.endBar (i)) - 4.0f;
            const auto y = yForEnergy (m.energyOf (i));
            if (i == 0) curve.startNewSubPath (x0, y); else curve.lineTo (x0, y);
            curve.lineTo (x1, y);
            if (m.sections[(size_t) i].energy >= 0)   // drawn by hand: mark it
            {
                g.setColour (Colours::brass);
                g.fillEllipse ((x0 + x1) * 0.5f - 3.0f, y - 3.0f, 6.0f, 6.0f);
            }
        }
        g.setColour (Colours::brass.withAlpha (0.85f));
        g.strokePath (curve, juce::PathStrokeType (2.0f));
    }

    // Ruler: a tick every 8 bars, labels every 32.
    g.setFont (juce::FontOptions (10.0f));
    for (int bar = 1; bar <= m.totalBars + 1; bar += 8)
    {
        const auto x = xForBar (bar);
        g.setColour (Colours::outline.brighter (0.2f));
        g.drawVerticalLine ((int) x, ruler.getY(), ruler.getY() + ((bar - 1) % 32 == 0 ? 8.0f : 4.0f));
        if ((bar - 1) % 32 == 0)
        {
            g.setColour (Colours::dimText);
            g.drawText (juce::String (bar), juce::Rectangle<float> (x + 2.0f, ruler.getY() + 4.0f, 40.0f, 12.0f), juce::Justification::left);
        }
    }

    // Playhead
    const auto bar = processor.ppq.load() / processor.barQuarters.load() + 1.0;
    if (bar >= 1.0 && bar <= m.totalBars + 1)
    {
        const auto x = xForBar (bar);
        g.setColour (Colours::text);
        g.drawLine (x, r.getY(), x, r.getBottom(), 2.0f);
    }
}

float Timeline::yForEnergy (float energy) const
{
    const auto area = getLocalBounds().toFloat().withTrimmedBottom (18.0f).reduced (0.0f, 22.0f);
    return area.getBottom() - area.getHeight() * juce::jlimit (0.0f, 1.0f, energy);
}

int Timeline::energyLineNear (float x, float y) const
{
    if (preview.has_value()) return -1;
    const auto m = processor.getMap();
    const auto index = arrangement::sectionAt (m, (int) std::floor (barForX (x)));
    return index >= 0 && std::abs (y - yForEnergy (m.energyOf (index))) < 6.0f ? index : -1;
}

void Timeline::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (boundaryNear ((float) e.x) >= 0 ? juce::MouseCursor::LeftRightResizeCursor
                    : energyLineNear ((float) e.x, (float) e.y) >= 0 ? juce::MouseCursor::UpDownResizeCursor
                                                                     : juce::MouseCursor::PointingHandCursor);
}

void Timeline::mouseDown (const juce::MouseEvent& e)
{
    if (preview.has_value()) return;   // no editing while a proposal is shown
    const auto m = processor.getMap();
    dragging = boundaryNear ((float) e.x);
    if (dragging >= 0)
        return;
    energyDragging = e.mods.isPopupMenu() ? -1 : energyLineNear ((float) e.x, (float) e.y);
    if (energyDragging >= 0)
        return;

    const auto index = arrangement::sectionAt (m, (int) std::floor (barForX ((float) e.x)));
    if (index < 0)
        return;
    selected = index;
    if (onSelectionChanged) onSelectionChanged();
    repaint();

    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu menu;
        for (int t = 0; t < numTypes; ++t)
            menu.addItem (t + 1, juce::String ("Make it ") + suite::sectionName ((SectionType) t));
        menu.addSeparator();
        menu.addItem (101, "Energy: back to the usual for " + juce::String (suite::sectionName (m.sections[(size_t) index].type)),
                      m.sections[(size_t) index].energy >= 0);
        menu.addItem (100, "Delete section", m.count > 1);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                            [this, index] (int result)
                            {
                                auto map = processor.getMap();
                                if (result == 100) arrangement::remove (map, index);
                                else if (result == 101) map.sections[(size_t) index].energy = -1;
                                else if (result >= 1 && result <= numTypes) map.sections[(size_t) index].type = (SectionType) (result - 1);
                                else return;
                                processor.setMap (map);
                                selected = juce::jmin (selected, map.count - 1);
                                if (onSelectionChanged) onSelectionChanged();
                                repaint();
                            });
    }
}

void Timeline::mouseDrag (const juce::MouseEvent& e)
{
    if (preview.has_value()) return;   // no editing while a proposal is shown
    if (energyDragging >= 0)
    {
        auto m = processor.getMap();
        const auto area = getLocalBounds().toFloat().withTrimmedBottom (18.0f).reduced (0.0f, 22.0f);
        const auto energy = juce::jlimit (0.0f, 1.0f, (area.getBottom() - (float) e.y) / area.getHeight());
        m.sections[(size_t) energyDragging].energy = juce::roundToInt (energy * 20.0f) * 5;   // 5% steps
        processor.setMap (m);
        repaint();
        return;
    }
    if (dragging < 0)
        return;
    auto m = processor.getMap();
    const auto snap = OuroborosProcessor::snapBars ((int) processor.apvts.getRawParameterValue (OuroborosProcessor::snapId)->load());
    arrangement::moveStart (m, dragging, arrangement::snapBar (barForX ((float) e.x), e.mods.isShiftDown() ? 1 : snap));
    processor.setMap (m);
    repaint();
}

void Timeline::mouseUp (const juce::MouseEvent&) { dragging = -1; energyDragging = -1; }

void Timeline::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (preview.has_value()) return;   // no editing while a proposal is shown
    // Split the section here: the new part takes the next type in the usual order.
    auto m = processor.getMap();
    const auto snap = OuroborosProcessor::snapBars ((int) processor.apvts.getRawParameterValue (OuroborosProcessor::snapId)->load());
    const auto bar = arrangement::snapBar (barForX ((float) e.x), snap);
    const auto index = arrangement::sectionAt (m, bar);
    if (index < 0 || bar <= m.sections[(size_t) index].startBar)
        return;
    const auto nextType = (SectionType) (((int) m.sections[(size_t) index].type + 1) % numTypes);
    selected = arrangement::insertAt (m, bar, nextType);
    processor.setMap (m);
    if (onSelectionChanged) onSelectionChanged();
    repaint();
}

//==============================================================================
void SuitePanel::refresh()
{
    entries = suite::Registry::list();
    std::sort (entries.begin(), entries.end(), [] (const auto& a, const auto& b)
               { return a.plugin != b.plugin ? a.plugin < b.plugin : a.track < b.track; });
    suite::SectionMap map;
    const auto haveMap = suite::SectionBus::read (map);
    advice = suitecheck::check (entries, haveMap ? &map : nullptr);
    next = suitecheck::nextStep (entries, haveMap ? &map : nullptr);
    if (const auto idle = suitecheck::unused (entries); ! idle.isEmpty())
        advice.push_back ({ "Doing nothing (remove to keep the project light): " + idle.joinIntoString (", ") + ".", false });
    notes = processor != nullptr ? processor->getNotes() : juce::StringArray();
    fixButton.setVisible (suitecheck::needsSessionFix (entries));
    repaint();
}

void SuitePanel::paint (juce::Graphics& g)
{
    g.fillAll (Colours::panel);
    auto r = getLocalBounds().reduced (14, 10);
    g.setColour (accent);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText ("SUITE: " + juce::String ((int) entries.size()) + " PLUGINS LOADED", r.removeFromTop (22), juce::Justification::centredLeft);
    // The next step: the one most useful thing to do now.
    {
        auto row = r.removeFromTop (40).reduced (0, 4);
        g.setColour ((next.problem ? Colours::ember : Colours::moss).withAlpha (0.18f));
        g.fillRoundedRectangle (row.toFloat(), 4.0f);
        g.setColour (Colours::text);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawFittedText ("NEXT:  " + next.text, row.reduced (10, 2), juce::Justification::centredLeft, 2, 0.85f);
        r.removeFromTop (4);
    }

    // Two columns of plugin / track.
    const auto listHeight = juce::jmin (240, ((int) entries.size() + 1) / 2 * 20 + 6);
    auto list = r.removeFromTop (listHeight);
    const auto colW = list.getWidth() / 2;
    g.setFont (juce::FontOptions (13.0f));
    for (size_t i = 0; i < entries.size(); ++i)
    {
        const auto& e = entries[i];
        auto row = juce::Rectangle<int> (list.getX() + (int) (i % 2) * colW, list.getY() + (int) (i / 2) * 20, colW - 8, 20);
        if (row.getBottom() > list.getBottom()) break;
        g.setColour (Colours::text);
        g.drawText (e.plugin, row.removeFromLeft (130), juce::Justification::centredLeft);
        g.setColour (Colours::dimText);
        g.drawText (e.track.isNotEmpty() ? e.track : juce::String ("(track name unknown)"), row, juce::Justification::centredLeft);
    }

    r.removeFromTop (8);
    g.setColour (Colours::outline);
    g.drawHorizontalLine (r.getY(), (float) r.getX(), (float) r.getRight());
    r.removeFromTop (8);
    g.setFont (juce::FontOptions (13.0f));
    auto notesArea = r.removeFromBottom (juce::jmin (150, r.getHeight() / 2));
    for (const auto& a : advice)
    {
        auto row = r.removeFromTop (36);
        if (row.getHeight() < 20) break;
        g.setColour (a.problem ? Colours::ember : Colours::moss);
        g.fillEllipse ((float) row.getX() + 2.0f, (float) row.getY() + 6.0f, 6.0f, 6.0f);
        g.setColour (Colours::text);
        g.drawFittedText (a.text, row.withTrimmedLeft (14), juce::Justification::topLeft, 2, 0.9f);
    }

    // Session notes: the newest automation steps in this project.
    g.setColour (accent);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("SESSION NOTES", notesArea.removeFromTop (20), juce::Justification::centredLeft);
    g.setFont (juce::FontOptions (12.0f));
    g.setColour (Colours::dimText);
    if (notes.isEmpty())
        g.drawText ("Nothing yet: fixes, snapshots, versions, recipes and new songs are noted here.", notesArea.removeFromTop (18), juce::Justification::centredLeft);
    for (int i = notes.size() - 1; i >= 0 && notesArea.getHeight() >= 16; --i)
        g.drawText (notes[i], notesArea.removeFromTop (17), juce::Justification::centredLeft);
}

//==============================================================================
SectionSnapPanel::SectionSnapPanel (OuroborosProcessor& p)
{
    for (int t = 0; t < (int) suite::SectionType::count; ++t)
    {
        choices.push_back (std::make_unique<suite::ui::Choice> (p.apvts, OuroborosProcessor::sectionSnapId (t), suite::sectionName ((suite::SectionType) t)));
        addAndMakeVisible (*choices.back());
    }
    note.setText ("As the song enters a section of a type, the whole suite recalls that snapshot (save them with A-D first). "
                  "Ouroboros keeps playing its map.", juce::dontSendNotification);
    note.setFont (juce::FontOptions (12.0f));
    note.setColour (juce::Label::textColourId, Colours::dimText);
    note.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (note);
    setSize (520, 170);
}

void SectionSnapPanel::resized()
{
    auto r = getLocalBounds().reduced (10);
    auto row = r.removeFromTop (60);
    const auto w = row.getWidth() / (int) choices.size();
    for (auto& c : choices) c->setBounds (row.removeFromLeft (w).reduced (2, 0));
    r.removeFromTop (8);
    note.setBounds (r);
}

//==============================================================================
namespace
{
    /** The kinds of section the map has, in order of first appearance (and "Drop 2+" if it has later drops). */
    std::vector<int> mixerColumns (const suite::SectionMap& m)
    {
        std::vector<int> cols;
        int drops = 0;
        for (int i = 0; i < m.count; ++i)
        {
            const auto t = (int) m.sections[(size_t) i].type;
            if (t == (int) suite::SectionType::Drop) ++drops;
            if (std::find (cols.begin(), cols.end(), t) == cols.end()) cols.push_back (t);
        }
        if (cols.empty()) for (int t = 0; t < (int) suite::SectionType::count; ++t) cols.push_back (t);
        if (drops > 1) cols.push_back (-1);   // drop 2+
        return cols;
    }
    juce::String cellText (const suite::PlanBus::Plan& p, int col)
    {
        if (col < 0) return std::abs (p.laterDropDb) < 0.05f ? juce::String ("same") : (p.laterDropDb > 0.0f ? "+" : "") + juce::String (p.laterDropDb, 1);
        const auto db = p.levelDb[(size_t) col];
        if (db <= suite::PlanBus::offDb + 0.5f) return "mute";
        juce::String t = std::abs (db) < 0.05f ? juce::String ("0") : (db > 0.0f ? "+" : "") + juce::String (db, 1);
        if (p.lowCutHz[(size_t) col] > 0.0f) t << "  HP " << juce::roundToInt (p.lowCutHz[(size_t) col]);
        if (p.highCutHz[(size_t) col] > 0.0f) t << "  LP " << juce::String (p.highCutHz[(size_t) col] / 1000.0f, 1) << "k";
        if (col == (int) suite::SectionType::Build && p.ramp) t << "  rising";
        return t;
    }
    juce::String columnName (int col) { return col < 0 ? juce::String ("Drop 2+") : juce::String (suite::sectionName ((suite::SectionType) col)); }
}

MixerPanel::MixerPanel (std::function<suite::SectionMap()> map, std::function<double()> bpm) : getMap (std::move (map)), getBpm (std::move (bpm))
{
    exportButton.setTooltip ("One MIDI file with every track's fader moves (a MIDI track each, named after the FL track; CC 7: 100 = unchanged, 0 = muted), and the table as text");
    exportButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Folder for the mixer moves", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc)
        {
            const auto dir = fc.getResult();
            if (dir == juce::File()) return;
            const auto f = exportAll (dir, getMap(), getBpm());
            if (f == juce::File())
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "Mixer moves", "Nothing to export yet: put Prima Materia on your tracks (with a role) and make a map.");
            else f.revealToUser();
        });
    };
    addAndMakeVisible (exportButton);
    setSize (900, 520);
    startTimerHz (2);
}

juce::StringArray MixerPanel::tableText (const suite::SectionMap& map)
{
    juce::StringArray out;
    const auto cols = mixerColumns (map);
    juce::StringArray head { "Track", "Gain", "Fader" };
    for (auto c : cols) head.add (columnName (c));
    head.add ("Before drops");
    out.add (head.joinIntoString ("\t"));
    for (const auto& p : suite::PlanBus::read())
    {
        juce::StringArray row { p.track, juce::String (p.gainDb, 1), juce::String (p.trimDb, 1) };
        for (auto c : cols) row.add (cellText (p, c));
        row.add (p.gapQuarters > 0.0 ? "mute " + juce::String (p.gapQuarters, 1) + " beats" : juce::String ("-"));
        out.add (row.joinIntoString ("\t"));
    }
    return out;
}

juce::File MixerPanel::exportAll (const juce::File& folder, const suite::SectionMap& map, double bpm)
{
    const auto plans = suite::PlanBus::read();
    if (plans.empty() || map.count == 0) return {};
    folder.createDirectory();
    auto mid = folder.getChildFile ("Mixer moves.mid");
    mid.deleteFile();
    const auto file = suite::PlanBus::allFaderMoves (plans, map, bpm);
    if (auto stream = mid.createOutputStream()) file.writeTo (*stream);
    folder.getChildFile ("Mixer moves.txt").replaceWithText ("Every track's suggested mixer moves (dB on the fader; HP / LP = high- / low-pass)\n\n"
        + tableText (map).joinIntoString ("\n")
        + "\n\nMixer moves.mid has a MIDI track per FL track with its fader as CC 7 (100 = unchanged, 0 = muted). In FL, link each mixer "
          "track's fader to CC 7 on its own MIDI Out channel, play the clips, and record or convert them to automation clips.\n");
    return mid;
}

//==============================================================================
TransmutePanel::TransmutePanel (std::function<suite::SectionMap()> map) : getMap (std::move (map))
{
    spice.setSliderStyle (juce::Slider::LinearHorizontal);
    spice.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 20);
    spice.setRange (0.0, 200.0, 1.0);
    spice.setTextValueSuffix ("%");
    spice.setValue (suite::TransmuteBus::songSpice() * 100.0, juce::dontSendNotification);
    spice.setTooltip ("How much happens on every track (each track's own Spice on top): 0% = nothing, 100% = as each track is set, 200% = wild");
    spice.onValueChange = [this] { suite::TransmuteBus::setSongSpice ((float) spice.getValue() / 100.0f); };
    reroll.setTooltip ("New dice for the whole song: every track picks new moves (locked moments stay)");
    reroll.onClick = [this]
    {
        suite::TransmuteBus::setSongSeed (suite::TransmuteBus::songSeed() * 1664525u + 1013904223u);
        suite::MoveLog::add ("Ouroboros", {}, "Transmutation: the song re-rolled (every track picks new moves)");
    };
    exportAll.setTooltip ("Every track's planned moves in one MIDI file: a MIDI track per FL track, a note and a marker per move");
    exportAll.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Save every track's moves", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                                                                     .getChildFile ("Transmutation moves.mid"), "*.mid");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            const auto plans = suite::TransmuteBus::read();
            if (f == juce::File() || plans.empty()) return;
            f.deleteFile();
            juce::FileOutputStream out (f);
            if (out.openedOk() && suite::TransmuteBus::planMidi (plans, getBpm ? getBpm() : 138.0, [] (int id) { return juce::String (trm::effect (id).name); }).writeTo (out))
                f.revealToUser();
        });
    };
    addAndMakeVisible (spice);
    addAndMakeVisible (reroll);
    addAndMakeVisible (exportAll);
    setSize (980, 560);
    startTimerHz (4);
}

void TransmutePanel::resized()
{
    auto r = getLocalBounds().reduced (14).removeFromBottom (30);
    reroll.setBounds (r.removeFromRight (170));
    r.removeFromRight (12);
    exportAll.setBounds (r.removeFromLeft (230));
    spice.setBounds (r.removeFromRight (320));
}

void TransmutePanel::paint (juce::Graphics& g)
{
    g.fillAll (Colours::panel);
    auto r = getLocalBounds().reduced (14);
    g.setColour (Colours::text);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText ("Transmutation: every track's creative moves", r.removeFromTop (22), juce::Justification::centredLeft);
    g.setFont (juce::FontOptions (12.0f));
    g.setColour (Colours::dimText);
    g.drawText ("Planned by each track's director from this map. Lock, re-roll or switch moves off in each track's Transmutation.",
                r.removeFromTop (18), juce::Justification::centredLeft);
    {
        auto foot = getLocalBounds().reduced (14).removeFromBottom (30);
        g.drawText ("Song spice", foot.withTrimmedRight (520), juce::Justification::centredRight);
    }
    r.removeFromTop (8);
    r.removeFromBottom (40);
    const auto map = getMap();
    const auto plans = suite::TransmuteBus::read();
    if (plans.empty())
    {
        g.drawFittedText ("No plans yet: put Transmutation on your tracks (it works out each track's role from its name).", r, juce::Justification::topLeft, 3);
        return;
    }
    const auto totalQ = juce::jmax (64.0, map.totalBars * 4.0);
    const auto nameW = 150;
    auto strip = r.removeFromTop (20).withTrimmedLeft (nameW);
    for (int i = 0; i < map.count; ++i)
    {
        const auto s = (map.sections[(size_t) i].startBar - 1) * 4.0, e = (map.endBar (i) - 1) * 4.0;
        const auto box = juce::Rectangle<float> ((float) strip.getX() + (float) strip.getWidth() * (float) (s / totalQ), (float) strip.getY(),
                                                 (float) strip.getWidth() * (float) ((e - s) / totalQ), (float) strip.getHeight()).reduced (1.0f, 0.0f);
        const auto type = map.sections[(size_t) i].type;
        g.setColour ((type == suite::SectionType::Drop ? Colours::ember : type == suite::SectionType::Build ? Colours::brass
                      : type == suite::SectionType::Breakdown ? Colours::amethyst : Colours::outline).withAlpha (0.35f));
        g.fillRoundedRectangle (box, 3.0f);
        g.setColour (Colours::text);
        g.drawText (suite::sectionName (type), box.reduced (3.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft, true);
    }
    r.removeFromTop (4);
    const auto rowH = juce::jlimit (12, 26, r.getHeight() / juce::jmax (1, (int) plans.size()));
    static const juce::Colour cat[] = { Colours::ember, Colours::brass, Colours::verdigris, Colours::amethyst, Colours::moss,
                                        juce::Colour (0xffe85aa8), Colours::dimText, juce::Colour (0xff5a9be8), juce::Colour (0xffd8c27d), juce::Colour (0xff9be85a) };
    for (const auto& p : plans)
    {
        if (r.getHeight() < rowH) break;
        auto row = r.removeFromTop (rowH);
        g.setColour (Colours::text);
        g.drawText (p.track + "  (" + juce::String ((int) p.moves.size()) + ")", row.removeFromLeft (nameW), juce::Justification::centredLeft, true);
        for (const auto& m : p.moves)
        {
            const auto x0 = (float) row.getX() + (float) row.getWidth() * (float) (m.startQ / totalQ);
            const auto w = juce::jmax (2.0f, (float) row.getWidth() * (float) ((m.endQ - m.startQ) / totalQ));
            const auto& e = trm::effect (m.effect);
            g.setColour (cat[juce::jlimit (0, 9, e.category)].withAlpha (0.8f));
            g.fillRect (juce::Rectangle<float> (x0, (float) row.getY() + 2.0f, w, (float) rowH - 4.0f));
        }
    }
}

void MixerPanel::resized()
{
    exportButton.setBounds (getLocalBounds().reduced (14).removeFromBottom (30).removeFromRight (230));
}

void MixerPanel::paint (juce::Graphics& g)
{
    g.fillAll (Colours::panel);
    auto r = getLocalBounds().reduced (14);
    g.setColour (Colours::text);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText ("Mixer: suggested moves", r.removeFromTop (22), juce::Justification::centredLeft);
    g.setFont (juce::FontOptions (12.0f));
    g.setColour (Colours::dimText);
    g.drawText ("From each track's Prima Materia (it changes nothing itself): the fader per section, in dB, and its filters. Make them in FL's mixer, or export them.",
                r.removeFromTop (18), juce::Justification::centredLeft);
    r.removeFromTop (8);
    r.removeFromBottom (40);
    const auto map = getMap();
    const auto cols = mixerColumns (map);
    const auto plans = suite::PlanBus::read();
    if (plans.empty())
    {
        g.drawFittedText ("No suggestions yet: put Prima Materia on your tracks and give each a role (or name the tracks Kick, Bass, Pads...).", r, juce::Justification::topLeft, 3);
        return;
    }
    const auto nameW = 150, gainW = 60, gapW = 90;
    const auto colW = juce::jmax (60, (r.getWidth() - nameW - 2 * gainW - gapW) / juce::jmax (1, (int) cols.size()));
    auto header = r.removeFromTop (22);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.setColour (Colours::dimText);
    g.drawText ("TRACK", header.removeFromLeft (nameW), juce::Justification::centredLeft);
    g.drawText ("GAIN", header.removeFromLeft (gainW), juce::Justification::centred);
    g.drawText ("FADER", header.removeFromLeft (gainW), juce::Justification::centred);
    for (auto c : cols) g.drawText (columnName (c).toUpperCase(), header.removeFromLeft (colW), juce::Justification::centred);
    g.drawText ("BEFORE DROPS", header, juce::Justification::centred);
    g.setFont (juce::FontOptions (12.0f));
    for (const auto& p : plans)
    {
        if (r.getHeight() < 20) break;
        auto row = r.removeFromTop (20);
        g.setColour (Colours::text);
        g.drawText (p.track, row.removeFromLeft (nameW), juce::Justification::centredLeft, true);
        g.setColour (Colours::dimText);
        g.drawText (std::abs (p.gainDb) < 0.05f ? juce::String ("-") : juce::String (p.gainDb, 1), row.removeFromLeft (gainW), juce::Justification::centred);
        g.drawText (std::abs (p.trimDb) < 0.05f ? juce::String ("-") : juce::String (p.trimDb, 1), row.removeFromLeft (gainW), juce::Justification::centred);
        for (auto c : cols)
        {
            const auto t = cellText (p, c);
            g.setColour (t == "mute" ? Colours::ember : t == "0" || t == "same" ? Colours::dimText : Colours::text);
            g.drawText (t, row.removeFromLeft (colW), juce::Justification::centred, true);
        }
        g.setColour (Colours::dimText);
        g.drawText (p.gapQuarters > 0.0 ? "mute " + juce::String (p.gapQuarters, 1) : juce::String ("-"), row, juce::Justification::centred);
    }
}

AutopilotPanel::AutopilotPanel()
{
    for (int i = 0; i < suite::Autopilot::numGroups; ++i)
    {
        const auto g = suite::Autopilot::groupAt (i);
        auto& b = switches[(size_t) i];
        int count = 0;
        for (int f = 0; f < suite::Autopilot::numFeatures; ++f) if (suite::Autopilot::featureGroup (f) == (uint32_t) g) ++count;
        b.setButtonText (juce::String (suite::Autopilot::groupName (g)) + "  (" + juce::String (count) + ")");
        b.setTooltip (juce::String (suite::Autopilot::groupDetail (g)) + ". Each fix in it can be switched on its own from the search box (Ctrl+K).");
        b.setToggleState (suite::Autopilot::on (g), juce::dontSendNotification);
        b.setColour (juce::ToggleButton::tickColourId, Colours::ember);
        b.onClick = [&b, g]
        {
            suite::Autopilot::set (g, b.getToggleState());
            suite::MoveLog::add ("Ouroboros", {}, juce::String ("Autopilot: ") + suite::Autopilot::groupName (g) + (b.getToggleState() ? " on" : " off"));
        };
        addAndMakeVisible (b);
    }
    setSize (1040, 140 + 6 * 40 + suite::Autopilot::numCounters * 18);
    startTimerHz (2);
}

void AutopilotPanel::resized()
{
    auto r = getLocalBounds().reduced (14);
    r.removeFromTop (48);
    auto grid = r.removeFromTop (6 * 40);
    const auto colW = grid.getWidth() / 2;
    for (int i = 0; i < suite::Autopilot::numGroups; ++i)
        switches[(size_t) i].setBounds (grid.getX() + (i % 2) * colW, grid.getY() + (i / 2) * 40, colW - 8, 34);
}

void AutopilotPanel::paint (juce::Graphics& g)
{
    g.fillAll (Colours::panel);
    auto r = getLocalBounds().reduced (14);
    g.setColour (Colours::text);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText ("Autopilot", r.removeFromTop (22), juce::Justification::centredLeft);
    g.setFont (juce::FontOptions (12.0f));
    g.setColour (Colours::dimText);
    g.drawText ("What the suite manages by itself while you choose sounds and effects. Each move is in What changed.", r.removeFromTop (18), juce::Justification::centredLeft);
    r.removeFromTop (8 + 6 * 40 + 10);
    g.setColour (Colours::text);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("Done so far (all instances on this computer)", r.removeFromTop (20), juce::Justification::centredLeft);
    g.setFont (juce::FontOptions (12.0f));
    for (int c = 0; c < suite::Autopilot::numCounters; ++c)
    {
        auto row = r.removeFromTop (18);
        const auto n = suite::Autopilot::countOf ((suite::Autopilot::Counter) c);
        g.setColour (n > 0 ? Colours::text : Colours::dimText);
        g.drawText (suite::Autopilot::counterName (c), row.removeFromLeft (row.getWidth() - 80), juce::Justification::centredLeft);
        g.drawText (juce::String ((int) n), row, juce::Justification::centredRight);
    }
}

void StereoPanel::paint (juce::Graphics& g)
{
    g.fillAll (Colours::panel);
    auto r = getLocalBounds().reduced (14);
    g.setColour (Colours::text);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText ("Stereo field", r.removeFromTop (22), juce::Justification::centredLeft);
    g.setFont (juce::FontOptions (12.0f));
    g.setColour (Colours::dimText);
    g.drawText ("How far each track's side sits under its middle (from its Quintessence): wider to the right.", r.removeFromTop (18), juce::Justification::centredLeft);
    r.removeFromTop (8);
    std::vector<std::pair<float, juce::String>> rows;
    static const char* roles[] = { "", "kick", "bass", "hats", "pads", "lead", "vocal", "FX", "bus" };
    for (const auto& e : suite::Registry::list())
        if (e.plugin == "Quintessence" && e.metrics[3] > -90.0f)
            rows.push_back ({ e.metrics[3], (e.track.isNotEmpty() ? e.track : juce::String ("a track")) + "  (" + roles[juce::jlimit (0, 8, (int) e.metrics[0])] + ")" });
    std::sort (rows.begin(), rows.end(), [] (const auto& a, const auto& b) { return a.first > b.first; });
    if (rows.empty())
    {
        g.drawFittedText ("No widths yet: Quintessence on each track measures it while the song plays.", r.removeFromTop (40), juce::Justification::topLeft, 2);
        return;
    }
    for (const auto& [db, name] : rows)
    {
        if (r.getHeight() < 22) break;
        auto row = r.removeFromTop (22);
        g.setColour (Colours::text);
        g.drawText (name, row.removeFromLeft (170), juce::Justification::centredLeft, true);
        const auto w = juce::jlimit (0.0f, 1.0f, (db + 30.0f) / 30.0f);   // -30 dB (mono) .. 0 dB (as wide as it is deep)
        auto bar = row.reduced (4, 5);
        g.setColour (Colours::outline);
        g.fillRoundedRectangle (bar.toFloat(), 3.0f);
        g.setColour (db > -6.0f ? Colours::ember : db > -14.0f ? Colours::verdigris : Colours::dimText);
        g.fillRoundedRectangle (bar.toFloat().withWidth ((float) bar.getWidth() * w), 3.0f);
        g.setColour (Colours::text);
        g.drawText (juce::String (db, 1) + " dB", bar, juce::Justification::centredRight);
    }
}

ChangesPanel::ChangesPanel()
{
    search.setTextToShowWhenEmpty ("Search: a bar (\"bar 64\") or words (\"pads\", \"undid\", \"Quintessence\")", Colours::dimText);
    search.setFont (juce::FontOptions (14.0f));
    search.onTextChange = [this] { refresh(); };
    addAndMakeVisible (search);
    setSize (720, 520);
    refresh();
    startTimerHz (2);
}

void ChangesPanel::refresh()
{
    const auto all = suite::MoveLog::read();
    total = (int) all.size();
    shown.clear();
    for (const auto& e : all)
        if (suite::MoveLog::matches (e, search.getText())) shown.push_back (e);
    repaint();
}

void ChangesPanel::resized()
{
    search.setBounds (getLocalBounds().reduced (12).removeFromTop (28));
}

void ChangesPanel::paint (juce::Graphics& g)
{
    g.fillAll (Colours::panel);
    auto r = getLocalBounds().reduced (12);
    r.removeFromTop (34);
    g.setFont (juce::FontOptions (12.0f));
    g.setColour (Colours::dimText);
    g.drawText (juce::String ((int) shown.size()) + " of " + juce::String (total) + " moves", r.removeFromTop (18), juce::Justification::centredLeft);
    if (shown.empty())
    {
        g.drawFittedText (total == 0 ? "Nothing yet: automatic moves (fixes, EQ and dynamics moves, balance steps) appear here as the suite makes them."
                                     : "Nothing matches.", r.removeFromTop (40), juce::Justification::topLeft, 2);
        return;
    }
    const auto now = juce::Time::currentTimeMillis();
    for (const auto& e : shown)
    {
        if (r.getHeight() < 34) break;
        auto row = r.removeFromTop (34);
        r.removeFromTop (2);
        const auto colour = e.kind == suite::MoveLog::kept ? Colours::moss : e.kind == suite::MoveLog::undone ? Colours::ember
                          : e.kind == suite::MoveLog::fix ? Colours::brass : Colours::text;
        g.setColour (colour.withAlpha (0.8f));
        g.fillRect (row.removeFromLeft (3));
        row.removeFromLeft (8);
        auto head = row.removeFromTop (15);
        const auto secs = (int) ((now - e.timeMs) / 1000);
        const auto ago = secs < 60 ? juce::String (secs) + " s ago" : secs < 3600 ? juce::String (secs / 60) + " min ago" : juce::String (secs / 3600) + " h ago";
        g.setColour (Colours::dimText);
        g.setFont (juce::FontOptions (11.0f));
        const auto dot = juce::String::fromUTF8 ("  \xc2\xb7  ");
        g.drawText (e.plugin + (e.track.isNotEmpty() ? dot + e.track : juce::String()) + (e.bar > 0 ? dot + "bar " + juce::String (e.bar) : juce::String())
                        + dot + ago, head, juce::Justification::centredLeft);
        g.setColour (colour);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (e.text, row, juce::Justification::centredLeft, true);
    }
}

//==============================================================================
namespace
{
    /** Moments: one per line, "bar what" (impact, stop or throw). */
    class MomentsPanel : public juce::Component
    {
    public:
        explicit MomentsPanel (OuroborosProcessor& p) : processor (p)
        {
            editor.setMultiLine (true);
            editor.setReturnKeyStartsNewLine (true);
            editor.setText (p.momentsText(), false);
            editor.setFont (juce::FontOptions (15.0f));
            note.setText ("One per line: a bar and what happens there.\n"
                          "  impact - Azoth's impact and crash on that bar\n"
                          "  stop - a beat of silence before it, on every track\n"
                          "  throw - every Antioch throws the beat before it\n"
                          "e.g.  64 stop   (then 65 impact)", juce::dontSendNotification);
            note.setFont (juce::FontOptions (12.0f));
            note.setColour (juce::Label::textColourId, Colours::dimText);
            note.setJustificationType (juce::Justification::topLeft);
            apply.onClick = [this]
            {
                const auto n = processor.setMomentsText (editor.getText());
                result.setText (juce::String (n) + (n == 1 ? " moment" : " moments") + " in the song", juce::dontSendNotification);
                editor.setText (processor.momentsText(), false);
            };
            result.setColour (juce::Label::textColourId, Colours::moss);
            for (auto* c : std::initializer_list<juce::Component*> { &editor, &note, &apply, &result }) addAndMakeVisible (c);
            setSize (380, 330);
        }
        void paint (juce::Graphics& g) override { g.fillAll (Colours::panel); }
        void resized() override
        {
            auto r = getLocalBounds().reduced (10);
            note.setBounds (r.removeFromTop (84));
            auto bottom = r.removeFromBottom (30);
            apply.setBounds (bottom.removeFromRight (90));
            result.setBounds (bottom);
            r.removeFromBottom (6);
            editor.setBounds (r);
        }
    private:
        OuroborosProcessor& processor;
        juce::TextEditor editor;
        juce::Label note, result;
        juce::TextButton apply { "Apply" };
    };
}

//==============================================================================
RecipePanel::RecipePanel (OuroborosProcessor& p)
{
    names.setEditableText (true);
    names.setTextWhenNothingSelected ("Recipe name (type a new one to save)");
    const auto existing = suite::Recipes::list();
    for (int i = 0; i < existing.size(); ++i)
        if (! existing[i].startsWith (OuroborosProcessor::autosavePrefix)) names.addItem (existing[i], i + 1);

    // Version history: the autosaves, newest first, to roll the whole suite back to.
    autosave = std::make_unique<suite::ui::Choice> (p.apvts, OuroborosProcessor::autosaveId, "Autosave");
    addAndMakeVisible (*autosave);
    const auto saves = OuroborosProcessor::autosaves();
    history.setTextWhenNothingSelected (saves.isEmpty() ? "No autosaves yet (they start once the song has played)" : "Version history...");
    for (int i = 0; i < saves.size(); ++i) history.addItem (saves[i].fromFirstOccurrenceOf (OuroborosProcessor::autosavePrefix, false, false), i + 1);
    rollBack.setTooltip ("Loads that autosave into every suite plugin (save a recipe first if you want to keep where you are)");
    rollBack.onClick = [this, saves]
    {
        const auto i = history.getSelectedId() - 1;
        if (! juce::isPositiveAndBelow (i, saves.size())) { result.setText ("Choose a version first", juce::dontSendNotification); return; }
        sentSeq = suite::RecipeBus::send (suite::RecipeAction::Load, saves[i]);
        sentWhat = "Rolled back to " + history.getText() + " in ";
        timerCallback();
    };
    addAndMakeVisible (history);
    addAndMakeVisible (rollBack);
    caption.setText ("Every suite plugin saves or loads its own settings, matched by track name. "
                     "Recipes live in " + suite::Recipes::folder().getFullPathName(), juce::dontSendNotification);
    caption.setFont (juce::FontOptions (12.0f));
    caption.setColour (juce::Label::textColourId, Colours::dimText);
    caption.setJustificationType (juce::Justification::topLeft);
    result.setFont (juce::FontOptions (13.0f));
    result.setColour (juce::Label::textColourId, Colours::moss);
    save.onClick = [this] { send (suite::RecipeAction::Save); };
    load.onClick = [this] { send (suite::RecipeAction::Load); };
    saveSignature.setTooltip ("Saves every suite plugin's settings as your signature: New song can start from it");
    saveSignature.onClick = [this] { names.setText (suite::Recipes::signature, juce::dontSendNotification); send (suite::RecipeAction::Save); };
    // Artist aliases: each keeps its own style log, recipes, signature, templates, hooks, presets and fingerprints.
    alias.setEditableText (true);
    alias.addItem ("Main", 1);
    const auto aliases = suite::Alias::list();
    for (int i = 0; i < aliases.size(); ++i) alias.addItem (aliases[i], i + 2);
    alias.setText (suite::Alias::current().isEmpty() ? juce::String ("Main") : suite::Alias::current(), juce::dontSendNotification);
    alias.setTooltip ("Artist alias: every alias keeps its own style log, recipes, signature, templates, hooks, presets and "
                      "fingerprints. Choose one, or type a new name, and Switch");
    useAlias.onClick = [this]
    {
        const auto name = alias.getText().trim();
        suite::Alias::setCurrent (name == "Main" ? juce::String() : name);
        result.setText ("Now working as " + (suite::Alias::current().isEmpty() ? juce::String ("your main name") : suite::Alias::current())
                            + ": every suite plugin uses its recipes, style and presets", juce::dontSendNotification);
    };
    for (auto* c : std::initializer_list<juce::Component*> { &names, &save, &load, &saveSignature, &caption, &result, &alias, &useAlias })
        addAndMakeVisible (c);
    setSize (460, 370);
    startTimerHz (4);
}

void RecipePanel::send (suite::RecipeAction action)
{
    const auto name = names.getText().trim();
    if (name.isEmpty()) { result.setText ("Type a name first", juce::dontSendNotification); return; }
    sentSeq = suite::RecipeBus::send (action, name);
    sentWhat = (action == suite::RecipeAction::Save ? "Saved '" : "Loaded '") + name + "' in ";
    timerCallback();
}

void RecipePanel::timerCallback()
{
    if (sentSeq == 0) return;
    const auto n = suite::RecipeBus::acknowledgements (sentSeq);
    result.setText (sentWhat + juce::String (n) + (n == 1 ? " plugin" : " plugins"), juce::dontSendNotification);
}

void RecipePanel::paint (juce::Graphics& g) { g.fillAll (Colours::panel); }

void RecipePanel::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto row = r.removeFromTop (30);
    load.setBounds (row.removeFromRight (70).reduced (2, 0));
    save.setBounds (row.removeFromRight (70).reduced (2, 0));
    names.setBounds (row.withTrimmedRight (6));
    r.removeFromTop (8);
    {
        auto row = r.removeFromTop (30);
        saveSignature.setBounds (row.removeFromLeft (200));
        row.removeFromLeft (10);
        useAlias.setBounds (row.removeFromRight (70).reduced (2, 0));
        alias.setBounds (row.withTrimmedRight (4));
    }
    r.removeFromTop (8);
    {
        auto h = r.removeFromTop (54);
        autosave->setBounds (h.removeFromLeft (170));
        auto line = h.withTrimmedTop (20).withTrimmedLeft (8);
        rollBack.setBounds (line.removeFromRight (90).reduced (2, 0));
        history.setBounds (line.withTrimmedRight (6));
    }
    r.removeFromTop (8);
    result.setBounds (r.removeFromBottom (22));
    caption.setBounds (r);
}

//==============================================================================
SessionPanel::SessionPanel (OuroborosProcessor& p) : processor (p)
{
    for (const auto& s : suite::sessionStyles()) style.addItem (s.name, style.getNumItems() + 1);
    style.setSelectedId (1, juce::dontSendNotification);
    for (int n = 0; n < 12; ++n) root.addItem (suite::music::noteNames()[n], n + 1);
    root.setSelectedId (10, juce::dontSendNotification);   // A
    for (const auto& sc : suite::music::scales()) scale.addItem (sc.name, scale.getNumItems() + 1);
    scale.setSelectedId (1, juce::dontSendNotification);
    for (const auto& m : suite::sessionMoods()) mood.addItem (m.name, mood.getNumItems() + 1);
    mood.setSelectedId (1, juce::dontSendNotification);
    style.onChange = [this] { updateInfo(); };
    mood.onChange = [this] { updateInfo(); };
    const auto hasSignature = suite::Recipes::list().contains (suite::Recipes::signature);
    signature.setToggleState (hasSignature, juce::dontSendNotification);
    signature.setEnabled (hasSignature);
    signature.setTooltip (hasSignature ? "Loads your signature recipe into every plugin first, then the style and key"
                                       : "Save one first: Recipes..., Save as my signature");
    for (auto* l : { &styleLabel, &keyLabel, &moodLabel, &info, &result })
    {
        l->setFont (juce::FontOptions (13.0f));
        addAndMakeVisible (l);
    }
    styleLabel.setText ("Style", juce::dontSendNotification);
    keyLabel.setText ("Key", juce::dontSendNotification);
    moodLabel.setText ("Mood", juce::dontSendNotification);
    info.setJustificationType (juce::Justification::topLeft);
    info.setColour (juce::Label::textColourId, Colours::dimText);
    result.setColour (juce::Label::textColourId, Colours::moss);
    go.onClick = [this]
    {
        sentSeq = processor.startSession (style.getSelectedId() - 1, root.getSelectedId() - 1, scale.getSelectedId() - 1,
                                           mood.getSelectedId() - 1, signature.getToggleState());
        timerCallback();
    };
    draft.setTooltip ("Sets up the song, then writes the whole song's MIDI (chords, bass, arp, lead, fills...) into the Drafts folder "
                      "and saves the setup as a recipe: a full draft in one click");
    draft.onClick = [this]
    {
        sentSeq = processor.draftSong (style.getSelectedId() - 1, root.getSelectedId() - 1, scale.getSelectedId() - 1,
                                        mood.getSelectedId() - 1, signature.getToggleState());
        timerCallback();
    };
    for (auto* c : std::initializer_list<juce::Component*> { &style, &root, &scale, &mood, &signature, &go, &draft })
        addAndMakeVisible (c);
    updateInfo();
    setSize (460, 290);
    startTimerHz (4);
}

juce::String SessionPanel::moodText() const
{
    const auto& m = suite::sessionMood (mood.getSelectedId() - 1);
    if (m.progression < 0) return {};
    return juce::String ("\nMood: ") + m.name + " - intensity " + juce::String (m.intensity) + "%"
         + (m.breakdownEnergy >= 0 ? ", deeper breakdowns" : "")
         + (m.dropEuphoria > 0.0f ? ", brighter drop leads" : m.dropEuphoria < 0.0f ? ", darker drop leads" : "") + ".";
}

void SessionPanel::updateInfo()
{
    const auto& s = suite::sessionStyles()[(size_t) juce::jmax (0, style.getSelectedId() - 1)];
    const auto bars = suite::sessionBars (s);
    const auto seconds = bars * 4.0 * 60.0 / s.bpm;
    info.setText (juce::String (bars) + " bars (" + juce::String ((int) seconds / 60) + ":" + juce::String ((int) seconds % 60).paddedLeft ('0', 2)
                      + "), " + juce::String (s.drops) + (s.drops == 1 ? " drop" : " drops")
                      + (s.finalLift > 0 ? ", key up for the last drop" : "") + ".\nSet FL's tempo to " + juce::String (juce::roundToInt (s.bpm))
                      + " BPM. This replaces the song map and the other plugins' key, progression, loudness target and scenes."
                      + moodText(),
                  juce::dontSendNotification);
}

void SessionPanel::timerCallback()
{
    if (sentSeq == 0) return;
    const auto n = suite::SessionBus::acknowledgements (sentSeq);
    result.setText ("Song map ready; " + juce::String (n) + (n == 1 ? " other plugin" : " other plugins") + " set up.", juce::dontSendNotification);
}

void SessionPanel::paint (juce::Graphics& g) { g.fillAll (Colours::panel); }

void SessionPanel::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto row1 = r.removeFromTop (28);
    styleLabel.setBounds (row1.removeFromLeft (60));
    style.setBounds (row1);
    r.removeFromTop (8);
    auto row2 = r.removeFromTop (28);
    keyLabel.setBounds (row2.removeFromLeft (60));
    root.setBounds (row2.removeFromLeft (90).reduced (0, 0));
    row2.removeFromLeft (8);
    scale.setBounds (row2);
    r.removeFromTop (8);
    auto row3 = r.removeFromTop (28);
    moodLabel.setBounds (row3.removeFromLeft (60));
    mood.setBounds (row3);
    r.removeFromTop (8);
    result.setBounds (r.removeFromBottom (22));
    auto bottom = r.removeFromBottom (32);
    go.setBounds (bottom.removeFromRight (130));
    draft.setBounds (bottom.removeFromRight (150).reduced (4, 0));
    signature.setBounds (bottom.removeFromLeft (170));
    info.setBounds (r);
}

//==============================================================================
ArrangePanel::ArrangePanel (arrangement::GenSettings initial, double tempo, std::function<void (arrangement::GenSettings)> onGenerate,
                            std::function<void (int)> onLikeMine)
    : bpm (tempo), generate (std::move (onGenerate)), likeMine (std::move (onLikeMine))
{
    likeMineButton.setTooltip ("Your usual structure (learned from the maps you saved as templates), at this length");
    likeMineButton.onClick = [this] { if (likeMine) likeMine ((int) bars.getValue()); };
    likeMineButton.setEnabled (likeMine != nullptr && OuroborosProcessor::learnArrangementDna().valid);
    addAndMakeVisible (likeMineButton);
    auto caption = [this] (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::FontOptions (13.0f));
        addAndMakeVisible (l);
    };
    caption (barsLabel, "Length (bars)");
    caption (dropsLabel, "Drops");
    caption (introLabel, "Intro and outro");
    caption (breakdownLabel, "First breakdown");
    duration.setFont (juce::FontOptions (12.0f));
    duration.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (duration);

    bars.setRange (32, 400, 8);
    bars.setValue (initial.totalBars, juce::dontSendNotification);
    bars.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 22);
    bars.onValueChange = [this] { updateDuration(); };
    drops.addItemList ({ "1", "2", "3" }, 1);
    drops.setSelectedId (juce::jlimit (1, 3, initial.drops), juce::dontSendNotification);
    intro.addItemList ({ "DJ-friendly (32 bars)", "Short (16 bars)" }, 1);
    intro.setSelectedId (initial.djIntro ? 1 : 2, juce::dontSendNotification);
    breakdown.addItemList ({ "Long (32 + 16-bar build)", "Short (16 + 8-bar build)" }, 1);
    breakdown.setSelectedId (initial.longBreakdown ? 1 : 2, juce::dontSendNotification);
    go.onClick = [this] { if (generate) generate (current()); };
    for (auto* c : std::initializer_list<juce::Component*> { &bars, &drops, &intro, &breakdown, &go })
        addAndMakeVisible (c);
    updateDuration();
    setSize (420, 230);
}

arrangement::GenSettings ArrangePanel::current() const
{
    arrangement::GenSettings g;
    g.totalBars = (int) bars.getValue();
    g.drops = drops.getSelectedId();
    g.djIntro = intro.getSelectedId() == 1;
    g.longBreakdown = breakdown.getSelectedId() == 1;
    return g;
}

void ArrangePanel::updateDuration()
{
    const auto seconds = bars.getValue() * 4.0 * 60.0 / juce::jmax (40.0, bpm);
    duration.setText (juce::String ((int) seconds / 60) + ":" + juce::String ((int) seconds % 60).paddedLeft ('0', 2)
                          + " at " + juce::String (juce::roundToInt (bpm)) + " BPM", juce::dontSendNotification);
}

void ArrangePanel::paint (juce::Graphics& g)
{
    g.fillAll (suite::ui::Colours::panel);
}

void ArrangePanel::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto row = [&r] { auto x = r.removeFromTop (30); r.removeFromTop (6); return x; };
    auto a = row();
    barsLabel.setBounds (a.removeFromLeft (130));
    bars.setBounds (a);
    duration.setBounds (row().withTrimmedLeft (130).withHeight (18));
    for (auto [label, box] : { std::pair<juce::Label*, juce::ComboBox*> { &dropsLabel, &drops }, { &introLabel, &intro }, { &breakdownLabel, &breakdown } })
    {
        auto x = row();
        label->setBounds (x.removeFromLeft (130));
        box->setBounds (x);
    }
    go.setBounds (r.removeFromRight (120));
    likeMineButton.setBounds (r.removeFromLeft (130));
}

//==============================================================================
OuroborosEditor::OuroborosEditor (OuroborosProcessor& p)
    : AudioProcessorEditor (p), processor (p),
      header ("OUROBOROS", "arrangement conductor", accent),
      broadcast (p.apvts, OuroborosProcessor::broadcastId, "Broadcast to suite", accent),
      snap (p.apvts, OuroborosProcessor::snapId, "Snap"),
      lift (p.apvts, OuroborosProcessor::liftId, "Final drop key"),
      finalSnap (p.apvts, OuroborosProcessor::finalSnapId, "Final drop snapshot"),
      dropGap (p.apvts, OuroborosProcessor::dropGapId, "Before every drop"),
      intensity (p.apvts, OuroborosProcessor::intensityId, "Intensity", Colours::brass),
      live (p.apvts, OuroborosProcessor::liveId, "LIVE MODE", accent),
      entrance (p.apvts, OuroborosProcessor::entranceId, "Drop entrance"),
      ride (p.apvts, OuroborosProcessor::rideId, "Intensity ride"),
      timeline (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (header);
    header.actions = [this] { return actions(); };

    templateBox.setTextWhenNothingSelected ("Start from a template...");
    fillTemplates();
    templateBox.onChange = [this]
    {
        const auto id = templateBox.getSelectedId();
        if (id <= 0) return;
        const auto builtIn = (int) arrangement::templates().size();
        if (id == 998) { templateBox.setSelectedId (0, juce::dontSendNotification); exportMarkers(); return; }
        if (id == 989)
        {
            templateBox.setSelectedId (0, juce::dontSendNotification);
            processor.startLearningPass();
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "Learning pass",
                "Play the whole song once, start to end. Every suite plugin is measuring: at the end the suite fits the ducking to "
                "your kick, fixes any low end that cancels, and Mentalism runs Finish the song (gain staging, rough mix, coaching).");
            return;
        }
        if (id == 988)
        {
            templateBox.setSelectedId (0, juce::dontSendNotification);
            processor.undoLearningPass();
            return;
        }
        if (id == 979)
        {
            templateBox.setSelectedId (0, juce::dontSendNotification);
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "Finish everything", processor.finishEverything());
            return;
        }
        if (id == 978)
        {
            templateBox.setSelectedId (0, juce::dontSendNotification);
            juce::CallOutBox::launchAsynchronously (std::make_unique<MixerPanel> ([this] { return processor.getMap(); }, [this] { return (double) processor.bpm.load(); }),
                                                    templateBox.getScreenBounds(), nullptr);
            return;
        }
        if (id == 975)
        {
            templateBox.setSelectedId (0, juce::dontSendNotification);
            juce::PopupMenu m;
            for (int i = 0; i < (int) suite::delivery::profiles().size(); ++i)
                m.addItem (1 + i, suite::delivery::profiles()[(size_t) i].name, true, processor.deliveryProfile() == i);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&templateBox), [this] (int r)
            {
                if (r <= 0) return;
                processor.setDeliveryProfile (r - 1);
                markerChooser = std::make_unique<juce::FileChooser> ("Where to put the delivery package", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));
                markerChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc)
                {
                    const auto f = fc.getResult();
                    if (f.isDirectory()) processor.writeDeliveryPackage (f, processor.deliveryProfile()).revealToUser();
                });
            });
            return;
        }
        if (id == 976)
        {
            templateBox.setSelectedId (0, juce::dontSendNotification);
            directorButton.triggerClick();
            return;
        }
        if (id == 977)
        {
            templateBox.setSelectedId (0, juce::dontSendNotification);
            auto panel = std::make_unique<TransmutePanel> ([this] { return processor.getMap(); });
            panel->getBpm = [this] { return (double) processor.bpm.load(); };
            juce::CallOutBox::launchAsynchronously (std::move (panel), templateBox.getScreenBounds(), nullptr);
            return;
        }
        if (id == 984)
        {
            templateBox.setSelectedId (0, juce::dontSendNotification);
            juce::CallOutBox::launchAsynchronously (std::make_unique<AutopilotPanel>(), templateBox.getScreenBounds(), nullptr);
            return;
        }
        if (id == 986)
        {
            templateBox.setSelectedId (0, juce::dontSendNotification);
            juce::CallOutBox::launchAsynchronously (std::make_unique<StereoPanel>(), templateBox.getScreenBounds(), nullptr);
            return;
        }
        if (id == 985)
        {
            templateBox.setSelectedId (0, juce::dontSendNotification);
            suite::AutomationBus::setBypassed (! suite::AutomationBus::bypassed());
            suite::MoveLog::add ("Ouroboros", {}, suite::AutomationBus::bypassed() ? "Hearing the mix without the suite's automatic moves (A/B)"
                                                                                   : "The suite's automatic moves are back in the sound");
            fillTemplates();
            return;
        }
        if (id >= 980 && id <= 983)
        {
            templateBox.setSelectedId (0, juce::dontSendNotification);
            const suite::Fix moods[] = { suite::Fix::Darker, suite::Fix::Brighter, suite::Fix::Wider, suite::Fix::MorePump };
            suite::FixBus::send (moods[id - 980]);
            return;
        }
        if (id == 987)
        {
            templateBox.setSelectedId (0, juce::dontSendNotification);
            juce::CallOutBox::launchAsynchronously (std::make_unique<ChangesPanel>(), templateBox.getScreenBounds(), nullptr);
            return;
        }
        if (id == 990)
        {
            templateBox.setSelectedId (0, juce::dontSendNotification);
            juce::CallOutBox::launchAsynchronously (std::make_unique<MomentsPanel> (processor), templateBox.getScreenBounds(), nullptr);
            return;
        }
        if (id == 991)
        {
            templateBox.setSelectedId (0, juce::dontSendNotification);
            processor.arrangeFromLoop();
            timeline.selected = 0;
            refreshSelection();
            return;
        }
        if (id == 992)
        {
            templateBox.setSelectedId (0, juce::dontSendNotification);
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "Project tidy", suitecheck::tidyPlan (suite::Registry::list()));
            return;
        }
        if (id == 993)
        {
            templateBox.setSelectedId (0, juce::dontSendNotification);
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "Stem plan", suitecheck::stemPlan (suite::Registry::list(), {}));
            return;
        }
        if (id == 995)
        {
            templateBox.setSelectedId (0, juce::dontSendNotification);
            processor.logVersionLengths();   // the versions as you kept them: your lengths, next time
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "Render plan", processor.renderPlan());
            return;
        }
        if (id == 994)
        {
            templateBox.setSelectedId (0, juce::dontSendNotification);
            auto panel = std::make_unique<SectionSnapPanel> (processor);
            juce::CallOutBox::launchAsynchronously (std::move (panel), templateBox.getScreenBounds(), nullptr);
            return;
        }
        if (id == 996)
        {
            templateBox.setSelectedId (0, juce::dontSendNotification);
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "Your style",
                                                    OuroborosProcessor::styleReport() + "\n(Saved as " + OuroborosProcessor::styleReportFile().getFullPathName() + ")");
            return;
        }
        if (id == 997)   // your usual structure, learned from your templates
        {
            templateBox.setSelectedId (0, juce::dontSendNotification);
            const auto dna = OuroborosProcessor::learnArrangementDna();
            if (dna.valid)
            {
                processor.setMap (arrangement::generateLike (dna));
                processor.addNote ("Laid out your usual structure (from " + juce::String (dna.songs) + " of your songs)");
            }
            timeline.selected = 0;
            refreshSelection();
            return;
        }
        if (id == 999)   // save the current map as one of your templates
        {
            templateBox.setSelectedId (0, juce::dontSendNotification);
            nameWindow = std::make_unique<juce::AlertWindow> ("Save as template", "Name for this song map (with its energy curve):", juce::MessageBoxIconType::NoIcon);
            nameWindow->addTextEditor ("name", "My structure");
            nameWindow->addButton ("Save", 1);
            nameWindow->addButton ("Cancel", 0);
            nameWindow->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int result)
            {
                if (result == 1 && nameWindow != nullptr)
                {
                    const auto name = nameWindow->getTextEditorContents ("name").trim();
                    if (name.isNotEmpty() && arrangement::saveUserTemplate (arrangement::userTemplateFolder(), name, processor.getMap()))
                        processor.addNote ("Saved the song map as template '" + name + "'");
                    fillTemplates();
                }
                nameWindow.reset();
            }));
            return;
        }
        if (id <= builtIn)
            processor.setMap (arrangement::fromTemplate (arrangement::templates()[(size_t) id - 1]));
        else if (id - builtIn - 1 < (int) userTemplates.size())
            processor.setMap (userTemplates[(size_t) (id - builtIn - 1)].second);
        timeline.selected = 0;
        refreshSelection();
    };

    liveHelp.setFont (juce::FontOptions (12.0f));
    liveHelp.setColour (juce::Label::textColourId, Colours::dimText);
    liveHelp.setText ("Live: send MIDI to Ouroboros and play the sections: C intro, D groove, E breakdown, F build, G drop, "
                      "A outro. Each starts on the next bar and the whole suite follows.", juce::dontSendNotification);
    for (auto* c : std::initializer_list<juce::Component*> { &intensity, &entrance, &ride, &live, &liveHelp })
        addAndMakeVisible (c);

    lengthCaption.setText ("Bars", juce::dontSendNotification);
    lengthCaption.setFont (juce::FontOptions (12.0f));
    lengthCaption.setJustificationType (juce::Justification::centredRight);
    lengthSlider.setRange (8, 1024, 1);
    lengthSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 60, 24);
    lengthSlider.onValueChange = [this]
    {
        auto m = processor.getMap();
        if ((int) lengthSlider.getValue() == m.totalBars) return;
        arrangement::setTotalBars (m, (int) lengthSlider.getValue());
        processor.setMap (m);
    };

    for (auto* l : { &nowLabel, &nextLabel, &selectionLabel, &legend })
    {
        l->setJustificationType (juce::Justification::centredLeft);
        l->setFont (juce::FontOptions (13.0f));
    }
    nowLabel.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    legend.setFont (juce::FontOptions (12.0f));
    legend.setJustificationType (juce::Justification::topLeft);
    legend.setText (juce::String::fromUTF8 ("What the suite does: Transmutation plans creative moves on every track for each build, drop and breakdown \xc2\xb7 "
                                            "Kybalion stops ducking in Breakdowns and Builds \xc2\xb7 Antioch throws at the end of every section "
                                            "(each plugin has a Follow Ouroboros switch)."),
                    juce::dontSendNotification);

    for (int t = 0; t < numTypes; ++t)
    {
        const auto type = (SectionType) t;
        auto mark = std::make_unique<juce::TextButton> (juce::String (suite::sectionName (type)).toUpperCase());
        mark->setColour (juce::TextButton::buttonColourId, sectionColour (type).withAlpha (0.35f));
        mark->onClick = [this, type] { processor.markAtPlayhead (type); };
        addAndMakeVisible (*mark);
        markButtons.push_back (std::move (mark));

        auto set = std::make_unique<juce::TextButton> (suite::sectionName (type));
        set->setColour (juce::TextButton::buttonColourId, sectionColour (type).withAlpha (0.25f));
        set->setColour (juce::TextButton::buttonOnColourId, sectionColour (type));
        set->onClick = [this, type]
        {
            auto m = processor.getMap();
            if (juce::isPositiveAndBelow (timeline.selected, m.count))
            {
                m.sections[(size_t) timeline.selected].type = type;
                processor.setMap (m);
            }
        };
        addAndMakeVisible (*set);
        typeButtons.push_back (std::move (set));
    }
    deleteButton.onClick = [this]
    {
        auto m = processor.getMap();
        arrangement::remove (m, timeline.selected);
        processor.setMap (m);
        timeline.selected = juce::jmin (timeline.selected, m.count - 1);
    };
    timeline.onSelectionChanged = [this] { refreshSelection(); };

    listenButton.setClickingTogglesState (true);
    listenButton.setColour (juce::TextButton::buttonOnColourId, accent);
    listenButton.setToggleState (processor.listening.load(), juce::dontSendNotification);
    listenButton.onClick = [this]
    {
        const auto on = listenButton.getToggleState();
        if (on && ! processor.listening.load())
            processor.clearListening();   // a fresh listen each time
        processor.listening.store (on);
    };
    proposeButton.onClick = [this] { propose(); };

    for (int s = 0; s < (int) slotButtons.size(); ++s)
    {
        auto& b = slotButtons[(size_t) s];
        b.setButtonText (suite::SuiteProcessor::slotName (s));
        b.setTooltip ("Snapshot " + suite::SuiteProcessor::slotName (s) + ": click to recall every suite plugin's settings "
                      "(or to choose this empty slot), right-click to clear");
        b.setColour (juce::TextButton::buttonOnColourId, accent);
        b.onClick = [this, s]
        {
            if (slotButtons[(size_t) s].rightClick)
            {
                snapshotCommand (suite::SnapshotAction::Clear, s);
                return;
            }
            currentSlot = s;
            if (processor.hasSnapshot (s))
                snapshotCommand (suite::SnapshotAction::Recall, s);
            refreshSnapshots();
        };
        addAndMakeVisible (b);
    }
    saveSnapshotButton.setTooltip ("Save the current settings of every suite plugin into the chosen slot");
    saveSnapshotButton.onClick = [this] { snapshotCommand (suite::SnapshotAction::Capture, currentSlot); };
    snapshotStatus.setFont (juce::FontOptions (12.0f));
    snapshotStatus.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (saveSnapshotButton);
    addAndMakeVisible (snapshotStatus);
    arrangeButton.onClick = [this] { showArrange(); };
    referenceButton.setTooltip ("Copy the structure of a reference track: choose an audio file (and its tempo); "
                                "its sections are proposed as a map to Apply or Discard");
    referenceButton.onClick = [this] { chooseReference(); };
    addAndMakeVisible (referenceButton);
    sessionButton.setTooltip ("Start a new song: a style and a key set up the map, key, progression, loudness target and scenes across the suite");
    sessionButton.onClick = [this]
    {
        juce::CallOutBox::launchAsynchronously (std::make_unique<SessionPanel> (processor), sessionButton.getScreenBounds(), nullptr);
    };
    addAndMakeVisible (sessionButton);
    recipesButton.setTooltip ("Save the whole suite's settings as a named recipe, or load one, in any project");
    recipesButton.onClick = [this]
    {
        juce::CallOutBox::launchAsynchronously (std::make_unique<RecipePanel> (processor), recipesButton.getScreenBounds(), nullptr);
    };
    versionsButton.setTooltip ("Keeps this song in snapshot A and makes a radio edit in B, an extended mix in C and a club edit "
                               "in D (32-bar beat-only intro and outro); then click the slots to compare");
    versionsButton.onClick = [this] { versionsSeq = processor.makeVersions(); lastSnapshotSeq = versionsSeq; lastSnapshotVerb = "Made A/B/C/D (back on A)"; refreshSnapshots(); };
    addAndMakeVisible (recipesButton);
    addAndMakeVisible (versionsButton);
    suiteButton.setTooltip ("Every suite plugin loaded, on which track, and what still needs setting up");
    suiteButton.onClick = [this]
    {
        juce::CallOutBox::launchAsynchronously (std::make_unique<SuitePanel> (&processor), suiteButton.getScreenBounds(), nullptr);
    };
    directorButton.setTooltip ("The director: one plan for every transition in the song, every plugin's part in it");
    directorButton.onClick = [this]
    {
        juce::CallOutBox::launchAsynchronously (std::make_unique<ouro::DirectorPanel> (processor), directorButton.getScreenBounds(), nullptr);
    };
    labelButton.setTooltip ("Label-ready: is this a track a label signs? Every check against the label's references, the gate, milestones");
    labelButton.onClick = [this]
    {
        juce::CallOutBox::launchAsynchronously (std::make_unique<lab::LabelReadyPanel> (processor), labelButton.getScreenBounds(), nullptr);
    };
    makePrimary (labelButton, accent);
    header.addRight (broadcast, 170);
    header.addRight (suiteButton, 100);
    header.addRight (directorButton, 100);
    header.addRight (labelButton, 120);
    header.enableAdvanced (processor.apvts.state, [this] (bool) { resized(); repaint(); });
    arrangeButton.setTooltip ("Generate a song map from a length and a few choices");
    applyButton.onClick = [this]
    {
        if (timeline.preview.has_value()) processor.setMap (*timeline.preview);
        timeline.preview.reset();
        timeline.selected = 0;
        refreshSelection();
    };
    discardButton.onClick = [this] { timeline.preview.reset(); };
    listenStatus.setFont (juce::FontOptions (12.0f));
    listenStatus.setJustificationType (juce::Justification::centredLeft);
    for (auto* c : std::initializer_list<juce::Component*> { &listenButton, &proposeButton, &applyButton, &discardButton, &listenStatus, &arrangeButton })
        addAndMakeVisible (c);

    for (auto* c : std::initializer_list<juce::Component*> { &templateBox, &lengthSlider, &lengthCaption, &nowLabel, &nextLabel,
                                                             &selectionLabel, &legend, &snap, &lift, &finalSnap, &dropGap, &timeline, &deleteButton })
        addAndMakeVisible (c);

    setSize (1060, 640);
    refreshSelection();
    timerCallback();
    startTimerHz (20);
}

OuroborosEditor::~OuroborosEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void OuroborosEditor::refreshSelection()
{
    const auto m = processor.getMap();
    timeline.selected = juce::jlimit (0, juce::jmax (0, m.count - 1), timeline.selected);
    if (m.count == 0) return;
    const auto& s = m.sections[(size_t) timeline.selected];
    selectionLabel.setText (juce::String (suite::sectionName (s.type)) + ": bars " + juce::String (s.startBar) + "-"
                                + juce::String (m.endBar (timeline.selected) - 1) + "  (" + juce::String (m.endBar (timeline.selected) - s.startBar)
                                + " bars)",
                            juce::dontSendNotification);
    for (int t = 0; t < numTypes; ++t)
        typeButtons[(size_t) t]->setToggleState ((int) s.type == t, juce::dontSendNotification);
    deleteButton.setEnabled (m.count > 1);
}

void OuroborosEditor::showArrange()
{
    {
        const auto m = processor.getMap();
        if (m.totalBars >= 32) lastArrange.totalBars = m.totalBars;
    }
    juce::Component::SafePointer<OuroborosEditor> safe (this);
    auto panel = std::make_unique<ArrangePanel> (lastArrange, processor.bpm.load(), [safe] (arrangement::GenSettings g)
    {
        if (safe == nullptr) return;
        safe->lastArrange = g;
        safe->timeline.preview = arrangement::generate (g);
        safe->timeline.repaint();
    }, [safe] (int totalBars)
    {
        if (safe == nullptr) return;
        safe->timeline.preview = arrangement::generateLike (OuroborosProcessor::learnArrangementDna(), totalBars);
        safe->timeline.repaint();
    });
    juce::CallOutBox::launchAsynchronously (std::move (panel), arrangeButton.getScreenBounds(), nullptr);
}

void OuroborosEditor::snapshotCommand (suite::SnapshotAction action, int slot)
{
    lastSnapshotSeq = suite::SnapshotBus::send (action, slot);
    lastSnapshotVerb = action == suite::SnapshotAction::Capture ? "Saved" : action == suite::SnapshotAction::Recall ? "Recalled" : "Cleared";
    lastSnapshotVerb << " " << suite::SuiteProcessor::slotName (slot);
    processor.pollSnapshots();   // this plugin answers straight away
    refreshSnapshots();
}

void OuroborosEditor::refreshSnapshots()
{
    for (int s = 0; s < (int) slotButtons.size(); ++s)
    {
        auto& b = slotButtons[(size_t) s];
        const auto filled = processor.hasSnapshot (s);
        b.setToggleState (s == currentSlot, juce::dontSendNotification);
        b.setColour (juce::TextButton::buttonColourId, filled ? accent.withAlpha (0.35f) : Colours::panel);
        b.setColour (juce::TextButton::textColourOffId, filled ? Colours::text : Colours::dimText);
    }
    saveSnapshotButton.setButtonText ("Save " + suite::SuiteProcessor::slotName (currentSlot));
    if (lastSnapshotSeq != 0)
    {
        const auto n = suite::SnapshotBus::acknowledgements (lastSnapshotSeq);
        snapshotStatus.setText (lastSnapshotVerb + " in " + juce::String (n) + (n == 1 ? " plugin" : " plugins"), juce::dontSendNotification);
    }
    else
        snapshotStatus.setText ("Suite snapshots", juce::dontSendNotification);
}

void OuroborosEditor::chooseReference()
{
    // Tempo first (defaults to the project's), then the file.
    juce::PopupMenu menu;
    const auto projectBpm = processor.bpm.load();
    menu.addSectionHeader ("Reference tempo");
    menu.addItem (1, "Same as the project (" + juce::String (projectBpm, 1) + " BPM)");
    const int presets[] = { 128, 130, 132, 134, 136, 138, 140, 142 };
    for (int i = 0; i < 8; ++i)
        menu.addItem (10 + i, juce::String (presets[i]) + " BPM");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (referenceButton), [this, projectBpm, presets] (int result)
    {
        if (result == 0) return;
        referenceBpm = result == 1 ? projectBpm : (double) presets[result - 10];
        referenceChooser = std::make_unique<juce::FileChooser> ("Choose a reference track",
                                                                juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                                "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        referenceChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                       [this] (const juce::FileChooser& fc)
                                       {
                                           if (fc.getResult().existsAsFile())
                                               processor.analyseReference (fc.getResult(), referenceBpm);
                                       });
    });
}

void OuroborosEditor::propose()
{
    const auto proposal = processor.proposeFromListening();
    if (proposal.count > 0)
    {
        timeline.preview = proposal;
        previewNote = {};
    }
    else
        listenStatus.setText ("Not enough heard yet: play at least 16 bars with Listen on", juce::dontSendNotification);
}

void OuroborosEditor::timerCallback()
{
    suite::Watchdog::Scope watch (processor.getName(), "editor refresh");
    refreshSnapshots();
    if (++suiteTicks % 10 == 0)
    {
        const auto entries = suite::Registry::list();
        suite::SectionMap shared;
        const auto haveShared = suite::SectionBus::read (shared);
        const auto advice = suitecheck::check (entries, haveShared ? &shared : nullptr);
        const auto problems = (int) std::count_if (advice.begin(), advice.end(), [] (const auto& a) { return a.problem; });
        suiteButton.setButtonText ("Suite (" + juce::String ((int) entries.size()) + ")" + (problems > 0 ? juce::String (" !") : juce::String()));
        suiteButton.setColour (juce::TextButton::textColourOffId, problems > 0 ? Colours::ember : Colours::text);
    }
    // Listen: when playback stops after a decent listen, propose a map automatically.
    const auto isPlaying = processor.playing.load();
    const auto heard = processor.barsHeard();
    if (wasPlaying && ! isPlaying && processor.listening.load() && heard >= 16 && ! timeline.preview.has_value())
        propose();
    wasPlaying = isPlaying;
    applyButton.setVisible (timeline.preview.has_value());
    listenStatus.setBounds (timeline.preview.has_value() ? statusNarrow : statusWide);
    discardButton.setVisible (timeline.preview.has_value());
    proposeButton.setEnabled (heard >= 16);
    if (! timeline.preview.has_value())
        listenStatus.setText (processor.listening.load() ? "Listening: heard " + juce::String (heard) + " bars (stop playback to get a proposal)"
                                                         : heard > 0 ? "Heard " + juce::String (heard) + " bars" : "Turn on Listen and play the song",
                              juce::dontSendNotification);
    else
        listenStatus.setText (previewNote, juce::dontSendNotification);
    if (processor.analysingReference.load())
        listenStatus.setText ("Analysing the reference...", juce::dontSendNotification);
    if (auto proposal = processor.takeReferenceProposal())
    {
        if (proposal->count > 0)
        {
            timeline.preview = *proposal;
            previewNote = "From '" + processor.referenceName() + "' at " + juce::String (juce::roundToInt (referenceBpm)) + " BPM";
        }
        else
            listenStatus.setText ("Couldn't find a structure in that file (is the tempo right?)", juce::dontSendNotification);
    }

    const auto m = processor.getMap();
    if ((int) lengthSlider.getValue() != m.totalBars)
        lengthSlider.setValue (m.totalBars, juce::dontSendNotification);

    const auto barQ = processor.barQuarters.load();
    const auto barPos = processor.ppq.load() / barQ + 1.0;
    const auto bar = (int) std::floor (barPos);
    const auto index = arrangement::sectionAt (m, bar);
    if (index >= 0)
    {
        const auto& s = m.sections[(size_t) index];
        nowLabel.setColour (juce::Label::textColourId, sectionColour (s.type));
        nowLabel.setText (juce::String (suite::sectionName (s.type)).toUpperCase() + "   bar " + juce::String (bar)
                              + " (" + juce::String (bar - s.startBar + 1) + " of " + juce::String (m.endBar (index) - s.startBar) + ")",
                          juce::dontSendNotification);
        const auto barsLeft = m.endBar (index) - bar;
        nextLabel.setText ((index + 1 < m.count ? juce::String ("Next: ") + suite::sectionName (m.sections[(size_t) index + 1].type)
                                                      + " in " + juce::String (barsLeft) + (barsLeft == 1 ? " bar" : " bars")
                                                : "Last section: song ends in " + juce::String (barsLeft) + " bars")
                               + "\n" + processor.director.ticker (processor.ppq.load(), barQ),   // the director's "what's next" ticker
                           juce::dontSendNotification);
    }
    else
    {
        nowLabel.setColour (juce::Label::textColourId, Colours::dimText);
        nowLabel.setText (bar > m.totalBars ? "After the end" : "Before the start", juce::dontSendNotification);
        nextLabel.setText ({}, juce::dontSendNotification);
    }
    refreshSelection();
    timeline.repaint();
    {
        const auto lr = suite::LabelReadyBus::overallLine();
        header.setStatus (lr.isNotEmpty() ? lr : nowLabel.getText().isNotEmpty() ? nowLabel.getText() : juce::String ("No map yet"),
                          lr.isNotEmpty() ? accent : index >= 0 ? sectionColour (m.sections[(size_t) index].type) : Colours::faintText);
    }
}

void OuroborosEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);
    drawPanel (g, nowArea, "Now playing");
    drawPanel (g, songArea, "Song map  (drag edges or the energy line, double-click to split, right-click for options)", accent);
    drawPanel (g, performArea, "Perform", Colours::brass);
    drawPanel (g, markArea, "Mark at playhead  (click while the song plays)", Colours::brass);
    drawPanel (g, selectArea, "Selected section", Colours::verdigris);
}

void OuroborosEditor::resized()
{
    auto bounds = getLocalBounds().reduced (16);
    header.setBounds (bounds.removeFromTop (HeaderBar::height).withTrimmedTop (-8));
    bounds.removeFromTop (10);
    const auto advanced = header.advanced();
    setShown ({ &dropGap, &sessionButton, &versionsButton, &recipesButton, &referenceButton, &lift, &finalSnap, &snap, &entrance, &ride }, advanced);

    nowArea = bounds.removeFromTop (84);
    auto n = nowArea.reduced (14, 6).withTrimmedTop (14);
    {
        // the suite snapshots: A-D and Save
        auto snaps = n.removeFromRight (4 * 38 + 80).withSizeKeepingCentre (4 * 38 + 80, 30);
        saveSnapshotButton.setBounds (snaps.removeFromRight (80).reduced (2, 0));
        for (int s = (int) slotButtons.size() - 1; s >= 0; --s)
            slotButtons[(size_t) s].setBounds (snaps.removeFromRight (38).reduced (2, 0));
        snapshotStatus.setBounds (n.removeFromRight (170).withSizeKeepingCentre (170, 30));
    }
    if (advanced)
    {
        dropGap.setBounds (n.removeFromRight (190).withTrimmedTop (-10));
        sessionButton.setBounds (n.removeFromRight (122).withSizeKeepingCentre (116, 30));
        versionsButton.setBounds (n.removeFromRight (132).withSizeKeepingCentre (126, 30));
        recipesButton.setBounds (n.removeFromRight (106).withSizeKeepingCentre (100, 30));
    }
    nowLabel.setBounds (n.removeFromLeft (n.getWidth() / 2));
    nextLabel.setBounds (n);
    bounds.removeFromTop (10);

    songArea = bounds.removeFromTop (210);
    auto s = songArea.reduced (12, 6).withTrimmedTop (16);
    auto bar = s.removeFromTop (30);
    templateBox.setBounds (bar.removeFromLeft (210).reduced (0, 2));
    arrangeButton.setBounds (bar.removeFromLeft (100).reduced (4, 2));
    lengthSlider.setBounds (bar.removeFromRight (120).reduced (0, 2));
    lengthCaption.setBounds (bar.removeFromRight (50));
    bar.removeFromLeft (12);
    if (advanced) referenceButton.setBounds (bar.removeFromLeft (100).reduced (4, 2));
    listenButton.setBounds (bar.removeFromLeft (80).reduced (0, 2));
    proposeButton.setBounds (bar.removeFromLeft (110).reduced (4, 2));
    statusWide = bar.reduced (6, 0);
    auto actions = bar.removeFromRight (170);
    applyButton.setBounds (actions.removeFromLeft (80).reduced (2));
    discardButton.setBounds (actions.reduced (2));
    statusNarrow = bar.reduced (6, 0);
    listenStatus.setBounds (timeline.preview.has_value() ? statusNarrow : statusWide);
    s.removeFromTop (8);
    timeline.setBounds (s);
    bounds.removeFromTop (10);

    auto bottom = bounds.removeFromTop (130);
    markArea = bottom.removeFromLeft (bottom.getWidth() * 58 / 100);
    bottom.removeFromLeft (10);
    selectArea = bottom;

    auto ma = markArea.reduced (12, 6).withTrimmedTop (16);
    if (advanced)
    {
        auto column = ma.removeFromRight (150);
        lift.setBounds (column.removeFromTop (column.getHeight() / 2));
        finalSnap.setBounds (column);
        auto snapCell = ma.removeFromRight (110);
        snap.setBounds (snapCell);
    }
    const auto bw = ma.getWidth() / 3;
    for (int t = 0; t < numTypes; ++t)
    {
        const auto row = t / 3, col = t % 3;
        markButtons[(size_t) t]->setBounds (juce::Rectangle<int> (ma.getX() + col * bw, ma.getY() + row * (ma.getHeight() / 2), bw, ma.getHeight() / 2).reduced (3));
    }

    auto sa = selectArea.reduced (12, 6).withTrimmedTop (16);
    auto info = sa.removeFromTop (24);
    deleteButton.setBounds (info.removeFromRight (130));
    selectionLabel.setBounds (info);
    sa.removeFromTop (6);
    const auto tw = sa.getWidth() / 3, th = juce::jmin (34, sa.getHeight() / 2);
    for (int t = 0; t < numTypes; ++t)
        typeButtons[(size_t) t]->setBounds (juce::Rectangle<int> (sa.getX() + (t % 3) * tw, sa.getY() + (t / 3) * th, tw, th).reduced (2));

    bounds.removeFromTop (8);
    performArea = bounds.removeFromTop (72);
    {
        auto pa = performArea.reduced (12, 4).withTrimmedTop (10);
        intensity.setBounds (pa.removeFromLeft (180));
        intensity.slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 16);
        intensity.maxHeight = 52;
        if (advanced)
        {
            entrance.setBounds (pa.removeFromLeft (170).withTrimmedTop (-10));
            ride.setBounds (pa.removeFromLeft (170).withTrimmedTop (-10));
        }
        live.setBounds (pa.removeFromLeft (150).withSizeKeepingCentre (140, 30));
        liveHelp.setBounds (pa.reduced (10, 0));
    }
    legend.setBounds (bounds);
}

void OuroborosEditor::fillTemplates()
{
    templateBox.clear (juce::dontSendNotification);
    for (size_t i = 0; i < arrangement::templates().size(); ++i)
        templateBox.addItem (arrangement::templates()[i].name, (int) i + 1);
    userTemplates = arrangement::loadUserTemplates (arrangement::userTemplateFolder());
    if (! userTemplates.empty())
    {
        templateBox.addSeparator();
        for (size_t i = 0; i < userTemplates.size(); ++i)
            templateBox.addItem ("Mine: " + userTemplates[i].first, (int) (arrangement::templates().size() + i + 1));
    }
    templateBox.addSeparator();
    const auto dna = OuroborosProcessor::learnArrangementDna();
    templateBox.addItem (dna.valid ? "Like my songs (your usual structure, from " + juce::String (dna.songs) + " songs)"
                                   : juce::String ("Like my songs (save 2+ maps as templates first)"), 997);
    templateBox.setItemEnabled (997, dna.valid);
    templateBox.addItem ("Save the current map as a template...", 999);
    templateBox.addItem ("Export section markers for FL (MIDI + cue sheet)...", 998);
    templateBox.addItem ("Style report: how you make trance...", 996);
    templateBox.addItem ("Render plan...", 995);
    templateBox.addItem ("Stem plan for a remix pack...", 993);
    templateBox.addItem ("Project tidy...", 992);
    templateBox.addItem ("Arrange from my loop (Prima Materia roles decide who plays where)", 991);
    templateBox.addItem ("Finish everything: learning pass, Mix it, Export all, render plan...", 979);
    templateBox.addItem ("What changed? Every automatic move, and why...", 987);
    templateBox.addItem (suite::AutomationBus::bypassed() ? "Hear WITH the automation again (A/B)" : "Hear without the automation (A/B)", 985);
    templateBox.addItem ("Stereo field: how wide each track sits...", 986);
    templateBox.addItem ("Autopilot: what the suite manages by itself...", 984);
    templateBox.addItem ("Mixer: every track's suggested moves (and export)...", 978);
    templateBox.addItem ("The director: every transition, one plan...", 976);
    templateBox.addItem ("Delivery package for the label (premasters, brief, report, markers)...", 975);
    templateBox.addItem ("Transmutation: every track's creative moves, song spice...", 977);
    templateBox.addItem ("Mood: darker (the master's tilt)", 980);
    templateBox.addItem ("Mood: brighter (the master's tilt)", 981);
    templateBox.addItem ("Mood: wider (pads, leads, FX)", 982);
    templateBox.addItem ("Mood: more pump (Kybalion's ducking)", 983);
    templateBox.addItem ("Moments (an impact, a stop or a throw on any bar)...", 990);
    templateBox.addItem ("Learning pass: play the song once and the suite sets itself up", 989);
    templateBox.addItem ("Undo the learning pass", 988);
    templateBox.addItem ("Snapshots per section...", 994);
}

void OuroborosEditor::exportMarkers()
{
    markerChooser = std::make_unique<juce::FileChooser> ("Save section markers",
                                                         juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Song sections.mid"),
                                                         "*.mid");
    markerChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                                [this] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f == juce::File()) return;
        processor.addNote (processor.exportMarkers (f) ? "Exported section markers to " + f.withFileExtension ("mid").getFileName() + " (and a .txt cue sheet)"
                                                       : juce::String ("Couldn't export the markers"));
    });
}

std::vector<suite::ui::Action> OuroborosEditor::actions()
{
    std::vector<suite::ui::Action> a;
    a.push_back ({ "Label-ready", "every check against the label's references, the gate, milestones, the mix fixes", "Label", [this] { labelButton.triggerClick(); } });
    a.push_back ({ "The director", "one plan for every transition", "Director", [this] { directorButton.triggerClick(); } });
    a.push_back ({ "Suite overview", "every suite plugin loaded and what still needs setting up", "Suite", [this] { suiteButton.triggerClick(); } });
    a.push_back ({ "Arrange...", "generate a song map", "Map", [this] { showArrange(); } });
    a.push_back ({ processor.listening.load() ? "Stop listening" : "Listen", "detect the song's sections while it plays", "Map", [this] { listenButton.triggerClick(); } });
    a.push_back ({ "Propose a map", "from what Listen heard", "Map", [this] { proposeButton.triggerClick(); }, proposeButton.isEnabled() });
    a.push_back ({ "Copy a reference's structure...", "", "Map", [this] { chooseReference(); } });
    a.push_back ({ "New song...", "a style and a key set up the whole suite", "Song", [this] { sessionButton.triggerClick(); } });
    a.push_back ({ "Recipes...", "save or load the whole suite's settings", "Song", [this] { recipesButton.triggerClick(); } });
    for (int s = 0; s < (int) slotButtons.size(); ++s)
        a.push_back ({ "Recall snapshot " + suite::SuiteProcessor::slotName (s), "every suite plugin's settings", "Snapshots",
                       [this, s] { currentSlot = s; snapshotCommand (suite::SnapshotAction::Recall, s); refreshSnapshots(); }, processor.hasSnapshot (s) });
    a.push_back ({ "Save snapshot " + suite::SuiteProcessor::slotName (currentSlot), "the chosen slot", "Snapshots", [this] { saveSnapshotButton.triggerClick(); } });
    for (int t = 0; t < (int) markButtons.size(); ++t)
        a.push_back ({ "Mark " + markButtons[(size_t) t]->getButtonText().toLowerCase() + " at the playhead", "", "Mark", [this, t] { markButtons[(size_t) t]->triggerClick(); } });
    // the autopilot: each group, and each fix on its own (the Autopilot page shows only the groups)
    for (int i = 0; i < suite::Autopilot::numGroups; ++i)
    {
        const auto g = suite::Autopilot::groupAt (i);
        const auto on = suite::Autopilot::on (g);
        a.push_back ({ juce::String (on ? "Autopilot off: " : "Autopilot on: ") + suite::Autopilot::groupName (g), suite::Autopilot::groupDetail (g), "Autopilot",
                       [g, on] { suite::Autopilot::set (g, ! on); } });
    }
    for (int f = 0; f < suite::Autopilot::numFeatures; ++f)
    {
        const auto feature = (suite::Autopilot::Feature) f;
        const auto on = suite::Autopilot::switchedOn (feature);
        const juce::String name = juce::String (suite::Autopilot::featureName (f)).fromFirstOccurrenceOf (": ", false, false);
        a.push_back ({ (on ? "Fix off: " : "Fix on: ") + (name.isNotEmpty() ? name : juce::String (suite::Autopilot::featureName (f))),
                       suite::Autopilot::groupName ((suite::Autopilot::Group) suite::Autopilot::featureGroup (f)), "Autopilot fixes",
                       [feature, on] { suite::Autopilot::set (feature, ! on); } });
    }
    for (int i = 0; i < templateBox.getNumItems(); ++i)
        if (const auto id = templateBox.getItemId (i); id > 0)
            a.push_back ({ templateBox.getItemText (i).upToFirstOccurrenceOf ("...", false, false), "", id < 900 ? "Templates" : "Tools",
                           [this, id] { templateBox.setSelectedId (id); }, templateBox.isItemEnabled (id) });
    return a;
}
