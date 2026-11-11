# Antioch Suite

Twelve VST3 plugins for uplifting trance production in FL Studio. They're built to replace
the setup work you'd otherwise redo in every project. They share one key: set it once
in **Correspondence**, and the others follow it.

| Plugin | What it replaces |
|---|---|
| **Antioch**: harmonic echo, euphoric reverb, throw delay | A delay, a reverb + EQ + sidechain chain, and the send automation for delay throws |
| **Cause & Effect**: build-up macro | The 6+ automation clips (filter, reverb, noise, riser, gate, width) you draw for every build |
| **Correspondence**: key and chord engine | Working out progressions, voicings and offbeat basslines in the piano roll by hand |
| **Kybalion**: trance gate + smart ducking | Drawing gate shapes in Gross Beat or Peak Controller, and sidechain/LFO Tool setups |
| **Vibration**: lead polish strip | The EQ → saturation → multiband → widener chain on every lead |
| **Mentalism**: mix assistant | Loudness meters, correlation meters, reference A/B and guesswork about what's wrong |
| **Ouroboros**: arrangement conductor | Automation clips that switch builds, ducking and throws on and off across the song |
| **Lapis**: master finisher | Riding the master limiter until the drop is loud enough, and checking true peak |
| **Emerald Tablet**: key detector | Working out the key of a track, loop or acapella by ear |
| **Azoth**: automatic transition FX | Placing impacts, crashes, swells, downlifters and snare rolls at every transition |
| **Prima Materia**: section mixer | Muting, filtering and riding individual tracks section by section with automation |
| **Solve et Coagula**: vocal unmasking | Carving EQ space for the vocal by hand, or a static cut that's there even when nobody sings |

## How much runs on its own

With the full suite loaded, here is everything that happens without automation clips:

1. **Emerald Tablet** hears the key and hands it to **Correspondence**, which shares it
   with every plugin.
2. **Ouroboros** listens to one play-through and proposes the song's sections (or drafts a
   map from a length and a few choices with **Arrange**), then tells the suite where it is.
3. **Cause & Effect** builds and drops on cue, **Kybalion** stops ducking in breakdowns,
   and **Antioch** throws at section ends.
4. **Section scenes** move each plugin's key controls per section: more reverb and shimmer
   in breakdowns, more Euphoria and width in drops, and so on.
5. **Azoth** plays the transition FX on cue: impact and crash into drops, a swell before
   them, a downlifter into breakdowns, and a snare roll through builds.
6. **Prima Materia** on individual tracks mutes, filters and rides them per section, so
   one looped groove arranges itself.
7. **Correspondence** exports full-song chord, bass, arp and lead clips that follow the map,
   including a generated melody (Melodia).
8. **Mentalism** measures against its target and shares it, and **Lapis** sets the master
   gain to hit it. Mentalism's **arrangement coach** checks the energy arc section by
   section, and its **Fix** buttons adjust the other plugins in one click.
9. **Every drop after the first goes bigger**: more echo and Euphoria, louder impacts,
   longer swells, the lead up an octave and embellished with a harmony line, and an
   optional **key change** for the last drop that the whole suite follows.
10. **Solve et Coagula** makes room for the vocal in the music, only where and when it sings.
11. **Set-up:** Prima Materia and Kybalion set themselves up from the track names, every
    Kybalion can duck to the real kick with no routing (**Suite kick**), **Rough mix**
    balances the tracks, and Ouroboros' **Suite** overview says what's still missing.
12. **Snapshots** save and compare whole versions of the song across every plugin, and
    **Export all** writes the song's MIDI in one go.

What's left to you is writing and arranging the music, plus choosing taste settings.

## Correspondence: key and chord engine

![Correspondence](docs/correspondence.png)

- **Key and scale** (minor, major, harmonic minor, Dorian, Phrygian, Mixolydian) are
  broadcast to the other plugins. Turn **Broadcast key** off to stop that.
- **Progression:** 2–8 chords, each set by scale degree (I–VII) and type (Auto, Major,
  Minor, Sus2, Sus4, 7th, Add9, Power). Auto stays inside the scale. Ten presets cover the
  classic trance moves (i–VI–III–VII, VI–VII–i, i–iv–VI–V, …).
- **Voicing:** Close, Open (drop-2, pad-like) or Wide (root doubled below). Voice leading
  keeps each chord close to the previous one, so pads move smoothly.
- **Rhythm:** chords can be sustained, offbeat stabs, a 3-3-2 pluck or a 16th gate. Bass
  can be offbeat, rolling, galloping, sustained or walking. Arps go up, down, up-down or bounce.
- **Drag into FL:** drag the **Chords**, **Bass** or **Arp** tile straight onto FL's
  playlist or into a piano roll, and it lands as a MIDI clip. **Save MIDI…** writes all
  three to a folder instead.
- **Audition:** click a chord, or press **Play**, to hear the progression through a
  simple preview tone. The audio of the channel it sits on passes through unchanged.

Put it on any mixer insert, the master for example. It doesn't change the audio passing
through it.

## Antioch: space bundle

![Antioch echo tab](docs/antioch_echo.png)

There's one master strip (Dry, Echo, Reverb, Throw and Output levels, plus the key) and
three tabs:

- **Echo:** four tempo-synced taps. Each repeat goes through a pitch shifter (−24…+24
  semitones) and a resonator tuned to a harmonic of the key, so the echoes climb into
  chords and ring in key. **→ Reverb** sends the echoes into the reverb.
- **Reverb:** an 8-line reverb with decay up to 20 s, pre-delay, modulation, width and
  low/high cut. **Shimmer** feeds back a pitch-shifted copy of the tail (+5th, octave,
  octave+5th, 2 octaves or −octave). **Duck** pushes the reverb down while the dry signal
  plays, so it blooms in the gaps without washing out the lead. **Freeze into breakdowns**
  (with Ouroboros): as each breakdown starts, the last half second of the reverb is held as
  an endless wash under it, fading out over 1, 2 or 4 bars (**Freeze level** sets how loud).
  **Tension** (default 30%, with Correspondence running) widens the reverb on the chords
  that pull away from home (anything but i and its relative major), up to half again as
  wide at 100%, and brings it back on the home chord, so the space breathes with the
  harmony.
- **Throw:** a dub-style delay that only takes in audio while the **THROW** pad is held
  (or latched, or automated). The tail keeps ringing after you let go. **Auto Throw**
  catches the last 1/16th to 1 bar of every 2, 4, 8 or 16 bars while FL is playing, so
  phrase-end throws happen without any automation. It has ping-pong, drive, filters,
  ducking and a send to the reverb.

![Antioch reverb tab](docs/antioch_reverb.png)
![Antioch throw tab](docs/antioch_throw.png)

## Cause & Effect: build-up macro

![Cause & Effect](docs/cause_effect.png)

Put it on the bus you want to build: a group, the music bus, or the master. Then drive it
either way:

1. **Automate the Build knob** with one ramp clip from 0 to 100%. Snapping it back to 0
   is the drop.
2. **Or flip TRIGGER BUILD on** at the start of the build (one automation point) and pick
   a length: 4, 8, 16 or 32 bars. It ramps itself in sync with FL's tempo, snaps to the
   nearest bar line, and snaps back exactly at the drop. Switching Trigger off early drops
   early.

As the build rises, it applies:

- **High-pass sweep** up to a frequency you choose, with resonance.
- **Reverb wash** that swells. **Cut tails at drop** clears it on the drop so the drop
  lands clean.
- **Noise riser:** band-passed white noise sweeping from 400 Hz to 12 kHz.
- **Tonal riser:** detuned saws gliding up 1–3 octaves and landing exactly on the
  song's root note.
- **Stutter:** volume chops that speed up in steps (1/4 → 1/8 → 1/16 → 1/32).
- **Widen:** the stereo image opens up, then snaps back to normal on the drop.

**Curve** sets the shape of the ramp. "Late" (the default) keeps most of the action for
the last few bars.

## Kybalion: trance bus (Rhythm + Polarity)

![Kybalion rhythm tab](docs/kybalion_rhythm.png)

**Rhythm** is a 16-step trance gate.

- Draw step levels by clicking or dragging on the grid (right-drag erases), pick from 9
  patterns (straight 16ths, trance classic, 3-3-2, offbeat, gallop, stutter, accents,
  half-time, ramp up), or hit **Randomise**.
- Step size can be 1/8, 1/16 or 1/32. **Length**, **Attack** and **Release** shape each
  step. **Depth** sets how far closed steps drop.
- **Swing** pushes every second step later.
- **Stereo offset** shifts the right channel's pattern by 1–8 steps, for gates that
  bounce left and right.
- **Filter** makes closed steps darker as well as quieter, with resonance.
- **Fills** (with Ouroboros): the last bar before each drop (and, with *Both*, each
  breakdown) plays a fill instead of the pattern: **Stutter** (1/32 chops), **Half-time**,
  **Gap** (silence on the last beat), **Roll** (1/8 → 1/16 → 1/32), or **Auto**, which
  alternates roll and gap into drops and uses half-time into breakdowns.
- **Your own patterns:** draw a pattern and choose *Save this pattern…* in the pattern
  list. It's saved as a file (in *Antioch Suite/Gate patterns*) and listed as *"Mine: …"*
  in every project.
- **Suite groove:** when Correspondence's *Groove* is a shuffle, the gate swings with it
  (unless its own Swing is set higher), so gated pads sit with the exported MIDI.
- **Chord changes** (with Correspondence running): *Accent* opens the gate fully on the
  first step of every chord change and lifts it a touch, so pads pulse with the harmony;
  *Restart* starts the pattern again at each change (it matters when chords are shorter
  than the pattern). Changes are counted from each section's start, as Correspondence
  writes them.
- **Dynamic gate** (off by default): the gate's depth follows the song: in drops it grows
  from 60% to full over the drop; elsewhere it follows the section's energy, so a quiet
  breakdown gates gently and a hot groove hard.

![Kybalion polarity tab](docs/kybalion_polarity.png)

**Polarity** is ducking with no sidechain routing.

- In **Tempo** mode it ducks on the beat grid (every 1/2, 1/4 or 1/8). A negative
  **Nudge** ducks slightly before the beat, which works like lookahead with no added
  latency.
- In **Sidechain** mode it follows a kick you route into the plugin's sidechain input,
  with a threshold.
- Presets for each element: Bass (tight), Pads (deep), Leads (gentle), Classic pump,
  Reverb & delays, and Lows only.
- In **Suite kick** mode it ducks to the real kick with no routing at all: turn on
  **Share kick** in a Kybalion on the kick track, and it learns where the kick hits in the
  bar (separately for each section type) and shares that pattern. Every Kybalion set to
  Suite kick then ducks on it, offbeat kicks, gaps and all. Until a kick is shared, it
  ducks on the beat grid. A Kybalion on a track named "Kick" turns Share kick on by
  itself.
- **Fit to kick:** with a kick on the sidechain (or shared), Kybalion measures how long
  it rings and the pitch of its body. The button sets Hold and Release to match, and the
  status line shows the kick's note against the song's root (for example *"55.1 Hz (A1 +4
  ct): in tune with the root"*, or how many semitones to retune it).
- **Phase check:** while a kick plays on the sidechain (or shared), Kybalion compares this
  track's low end (below 120 Hz) with the kick's. If they cancel, the status line says
  *"low end OUT OF PHASE with the kick"*: turn on **Flip polarity** (next to Breakdown),
  which inverts this track.
- **Split** ducks only below a chosen frequency, using a Linkwitz-Riley crossover, so the
  bass pumps and the top stays put.
- **BREAKDOWN** switches ducking off. Automate it for breakdowns where the kick drops
  out.
- The curve display shows the gain shape over two beats, with a live position marker and
  a gain-reduction meter.

Put it on any insert. A typical setup: Rhythm on a pad or pluck bus, and Polarity
(Tempo, *Bass (tight)*) on the bass.

## Vibration: lead polish strip

![Vibration](docs/vibration.png)

Put it on a supersaw lead, pluck, pad or arp. Turn up **EUPHORIA**: one knob adds air,
presence, saturation, multiband glue and width, and cuts mud, all at once. A line under
the knob shows exactly what it's applying. Start from a preset (Supersaw lead, Pluck,
Pad, Vocal chop, Arp, Clean), then fine-tune:

- **Tone:** low cut, mud (320 Hz), presence (3.2 kHz) and air (10 kHz shelf).
- **Saturation:** drive and mix. It runs at 2x oversampling so heavy drive stays clean
  rather than aliasing.
- **Multiband:** OTT-style, splitting into 3 bands at 250 Hz and 3 kHz. Loud parts are
  pushed down and quiet parts lifted, with **Depth** as the mix and **Time** scaling the
  speed.
- **Width:** sides up to 200%, with everything below **Mono below** kept mono so the
  lead's low end doesn't smear.
- **Auto gain:** on by default. It matches the output level to the input, so you judge
  the tone rather than the volume.
- The spectrum shows input (grey) against output (gold).
- **Lead fingerprint…** keeps your lead sound across projects. Play the lead for about 5
  seconds and choose *Save this lead's fingerprint*: it stores the character (Euphoria,
  low cut, drive, saturation, multiband, width) and the tone of the processed lead (its
  octave-band shape from 125 Hz up). In another song, on another synth, choose *Match:
  <name>*: the character comes back, and Mud, Presence and Air move 70% of the way towards
  the stored tone. Play it again and match once more to refine. Fingerprints live in
  *Antioch Suite/Lead fingerprints*.
- **Tension width** (default 30%, with Correspondence running): the lead widens on the
  chords that pull away from home (up to 50% wider at 100%) and narrows back on the home
  chord.
- **Pitch bright** (off by default): Vibration tracks the lead's pitch; notes above its
  usual pitch get more air and presence (up to +4 and +2 dB) and notes below get less, so
  the top of a melody sparkles and low notes stay warm.

## Mentalism: mix assistant

![Mentalism](docs/mentalism.png)

Put it last on the master. It only measures and leaves the audio bit-identical. Play the
track (or its loudest section) and read the numbers:

- **Loudness:** momentary, short-term and gated integrated LUFS (ITU-R BS.1770 / EBU
  R128), plus true peak with 4x oversampling. A gauge shows integrated loudness against
  the target: Club master −8 LUFS, Loud club −6, Streaming −14, or **Match reference
  track**.
- **Stereo:** full-band correlation, correlation below 120 Hz (is the low end mono?),
  and how much level the mix loses when summed to mono.
- **Tonal balance:** 10 octave bands compared with a target. Bars show the whole song,
  the line shows the last 3 seconds, and the shaded zone is ±3 dB. Only the shape is
  compared, not the overall level. The built-in curves for uplifting and progressive /
  tech trance are rough guides. **Load reference…** (WAV/AIFF/FLAC/Ogg/MP3) analyses a
  track you like and uses it as the target instead. The reference is saved with your
  project.
- **Suggestions:** plain-language advice ordered by importance. For example: *"Low-mids
  (250 Hz) is 5.0 dB above the target: classic mud: cut 200-400 Hz on pads, bass and
  reverbs."*

**Masking** (third tab). Each Prima Materia measures its track's spectrum in the drops
and shares it. Mentalism lists the pairs of tracks fighting for the same band (from
250 Hz to 8 kHz: a band that's prominent in both, at similar levels), worst first, e.g.
*"Pad and Lead clash at 2 kHz in the drops: carve Pad"*. **Fix** carves a 3 dB dip at that
band into the less important track (vocal keeps over lead, lead over kick and bass, then
pads, hats, FX); press again to go deeper (up to 9 dB). Prima Materia's status line shows
the carve, and History undoes it.

Mentalism also keeps an eye on masking while you work: the clashes are checked every 30
seconds of playback, and one that's still there after three checks in a row (over a minute)
appears among the Suggestions. It never carves anything by itself: the Fix stays your call.

**Match reference** (Tonal Balance tab, with a reference loaded and at least 10 s of the
song measured) compares your mix's long-term balance with the reference and moves Lapis'
new **Low tone** (100 Hz shelf) and **High tone** (8 kHz shelf) half-way towards it, at
most 4 dB each, and sets the loudness target to match the reference. It's deliberately
gentle, so press it again if you want to go further, and it's in History to undo.

**Reset** starts a new measurement, for example after changing the mix.

**Export checklist** (Suggestions panel) is the last look before rendering: the song has
been measured, loudness is within 1 LU of the target, true peak is under the ceiling, no
samples clipped, the low end holds up in mono, the mix survives a mono sum, no section is
silent by mistake, the coach is happy with the energy arc, and the intro and outro are
DJ-friendly (a kick in the intro, no pads, leads or vocals in either; it checks the Prima
Materia tracks, and its **Fix** button takes the melodic parts out, undoable in History), and
the **DJ phrasing** is right: the intro and outro are whole 16-bar phrases and every
section starts on the 8-bar grid. Its Fix rounds the intro and outro to 16 or 32 bars and
moves the sections onto the grid in Ouroboros (undoable). Each item shows pass,
warning or fail, with *Ready to export* when everything passes. Press Reset and play the
whole song through first.

**My sound.** **Learn my sound…** (Tonal Balance tab) takes a few of your own finished
tracks and averages their loudness, true peak and tonal balance. Choose **My sound** as the
loudness target and the tonal target, in any project, to mix towards your own records
instead of a genre guide. Lapis follows the loudness and ceiling when it follows Mentalism.

**Arc match.** Where you've drawn a section's energy on Ouroboros' curve, the coach holds
the song to it: relative to the first drop, each drawn section should sit 12 LU lower per
100% of energy (a breakdown drawn at 40% belongs 7.2 LU under the drop). More than 3 LU
off and it says so, with a **Fix** that moves that one section up or down on every Prima
Materia track that plays in it (undoable; saved with the project; forgotten if the map
moves the section). For drawn sections the curve replaces the usual breakdown and intro
rules.

**Since the last pass.** Press Reset after playing the song (20 s or more) and that pass is
kept: once the next pass has run 20 s, the Suggestions say how much louder or quieter it
is (and how far to turn the master to compare at equal loudness) and which bands changed.

**Mix report.** *Save mix report* (in the export checklist) writes everything Mentalism
knows about the mix to a text file in *Antioch Suite/Mix reports*: loudness, true peak,
stereo, tone against the target, the comparison with the last pass, the suggestions, each
section's loudness and the coach's notes, the checklist and every fix applied. Finish the
song saves one automatically when it completes.

**Auto-coach** (Suggestions panel) takes the clicking out: after every complete
play-through it applies the coach's fixes by itself, forgets those sections so it hears
them again, and repeats on the next play-through, up to three passes or until the coach
is happy. Everything it does is in History.

**Finish the song** does the rest in one click: gain staging on every Prima Materia track,
the DJ intro/outro fix if the checklist wants it, the reference match if a reference is
loaded, and auto-coach on. Once you've played the drop again (the tracks measure their new
levels), it runs the rough mix; then play the whole song and the auto-coach shapes the
arc. The line under the suggestions says what it's waiting for, and finally whether you're
ready to export.

**Arrangement coach.** With Ouroboros running, Mentalism also measures loudness section
by section, and the two halves of each build. The **Arrangement** tab charts it and checks
the arc:
- Is each breakdown 2–12 LU below the drop that follows?
- Does each build rise by at least 1 LU, and get brighter? (Where the build has real top
  end, its 8 kHz band should rise at least 1 dB against the mids from the first half to the
  second: a build that only gets louder is flagged, with the same Fix.)
- Does every later drop hit at least as hard as the one before?
- Does the intro leave headroom for the drop?
- On songs of 64 bars or more: is any build shorter than 8 bars, or any breakdown shorter
  than 16? The Fix makes it 8 bars longer in Ouroboros's map (later sections move along;
  undoable like any fix).

**Your own numbers.** Once you've finished songs (with *Finish the song*), the coach learns
your usual breakdown depth and build rise from them, and its messages add *"(your usual:
X LU)"*, so you can tell a real problem from your style.

Problems appear there and at the top of the suggestions, for example *"Breakdown (bars
65-96) is only 1.0 LU below the drop: the drop won't feel like a lift."*

**Fix buttons.** Each problem has a **Fix** button. One click asks the plugins that can help
to take a small step on their own knobs, so you can see what changed:

| Problem | What Fix does |
|---|---|
| Breakdown too close to the drop | Prima Materia: breakdowns 2 dB quieter (on every track whose breakdown isn't muted) |
| Breakdown far too quiet | Prima Materia: breakdowns 2 dB louder |
| Build doesn't rise | Cause & Effect: +10% riser, noise and wash; Azoth: +10% swell and roll |
| Intro too loud | Prima Materia: intro 2 dB quieter |
| Later drop weaker | Prima Materia: Drop 2+ column on, 1.5 dB above the drop; Azoth: *Bigger later drops* +25% |

The line above the chart says how many plugins applied it. **Undo last fix** puts their
knobs back, and **History** (in the header) lists every fix and rough mix so far with an
Undo for each. Undoing an older fix only puts back parameters that nothing has changed
since, so a later fix or your own tweak is never overwritten. The fixed sections are measured again the next time they play, so click Fix
again if the coach still isn't happy.

![Mentalism arrangement](docs/mentalism_arrangement.png)

## Ouroboros: arrangement conductor

![Ouroboros](docs/ouroboros.png)

Map out your song once, and the rest of the suite follows it with no automation:

| Section | Cause & Effect | Kybalion | Antioch |
|---|---|---|---|
| Build | Ramps the whole build across the section and drops exactly on its last bar | Stops ducking | Throws at its end (with *At section ends*) |
| Breakdown | Nothing | Stops ducking (no kick) | Throws at its end |
| Intro, Groove, Drop, Outro | Nothing | Ducks as usual | Throws at its end |

- **New song…** sets up the whole suite for a fresh track in one go. Pick a style
  (Uplifting anthem, Uplifting radio edit, Tech trance, Progressive / melodic, Emotional /
  vocal) and a key. Ouroboros builds the style's map (e.g. 240 bars, two drops, key up
  for the last drop), Correspondence takes the key and a matching progression and
  broadcasts it, Mentalism sets the loudness target, and Antioch, Kybalion, Vibration and
  Solve et Coagula turn on their suggested scenes. FL's tempo can't be set from a plugin,
  so the panel tells you which tempo to set.
  **Mood** (As the style, Euphoric, Hopeful, Melancholic, Dark) colours the new song:
  Correspondence takes the mood's progression (Euphoria, Ascension, Heartbreak or
  Driving) instead of the style's, Ouroboros sets the intensity (Euphoric 120%) and draws
  deeper breakdowns for Melancholic and Dark, and Vibration's drop scene gets brighter or
  darker.
- **Arrange…** builds a map for you: choose the length in bars (it shows the duration at
  the current tempo), 1–3 drops, a DJ-friendly 32-bar or short 16-bar intro and outro,
  and a long or short first breakdown. Every section is a whole number of 8-bar phrases
  and the song comes out exactly the length you asked for (when it's too short for
  everything, the groove and later breakdowns shrink first and drops keep their 32 bars).
  The result shows as a preview to **Apply** or **Discard**.
- **Start from a template:** classic uplifting (208 bars), two breakdowns (232), radio
  edit (88), or a short 16-bar loop for testing. Then edit it: drag section edges,
  double-click to split, and right-click to change a section's type or delete it.
- **Mark at playhead:** play the song and click INTRO, BREAKDOWN, BUILD, DROP and so on
  as each section arrives. Each new section snaps to the 1, 4, 8 or 16-bar grid.
- **Now playing** shows the current section, the bar within it, and what comes next
  ("Next: Drop in 8 bars").
- Each follower has a **Follow Ouroboros** switch. For Antioch, it's the
  **Auto Throw → At section ends** option.
- The arrangement is saved with the project. Put Ouroboros on any insert (the master is
  fine): it doesn't touch the audio.

**Suite snapshots (A–D).** Click **Save A** to store the current settings of every suite
plugin in the project, change things, choose B and save again, then click A and B to
compare whole versions of the song (mix, scenes, map and all). Each plugin keeps its own
copies inside its own saved state, so snapshots travel with the FL project. Right-click a
slot to clear it.

**Perform.** **Intensity** (0–200%) scales every section move in the suite at once: the
scenes, Prima Materia's levels and the energy curve, and Azoth's transitions. Automate it
or ride it live. **Live mode** turns Ouroboros into an instrument: route a MIDI controller
or a piano-roll clip to it and play the sections (C intro, D groove, E breakdown, F build,
G drop, A outro). Each starts on the next bar line (a note just before a bar line waits for
the one after), and the whole suite follows as if it were the song map: builds ramp over
8 bars, transitions fire, scenes move.

**Intensity ride** puts Intensity on a controller: the mod wheel (CC 1), breath (CC 2),
expression (CC 11) or CC 16, routed to Ouroboros. The bottom of the wheel is 0%, the middle
100% and the top 200%. With FL's automation recording on, your ride is recorded as an
Intensity automation clip.

**Your own templates.** "Save the current map as a template…" at the bottom of the
template list keeps the song map, energy curve included, as one of your templates
("Mine: …") in every project.

**Like my songs** (template list, and a button in Arrange…) uses your **arrangement DNA**:
once you've saved two or more maps as templates, Ouroboros takes your most common order of
sections, the typical length of each and any energy you usually draw, and lays that out
at your usual length (or at the length in Arrange…), in whole 8-bar phrases.

**Export section markers for FL** (template list) saves a MIDI file with a marker at every
section ("Intro", "Breakdown", "Drop 2"...) plus a .txt cue sheet with bars and times.
Import the MIDI file into FL to bring the markers in. If your FL version doesn't import
markers from MIDI, the cue sheet lists every bar number and time for adding markers by hand.

**Session notes.** The Suite panel also lists what the automation did in this project
(fixes, undos, snapshots, versions, recipes, new songs), with times, saved with the
project. It also warns about **collisions**: a key change the vocal can't follow, a
kick-only bar on top of Kybalion fills, a drop drawn below its breakdown, Intensity
nearly off.

**Energy curve.** The brass line across the song map is the song's energy: each section
sits at its type's usual level (drop 100%, build 60%, groove 55%, intro/outro 35%,
breakdown 25%). Drag a section's line up or down (in 5% steps) and the suite follows:
section scenes go further (or less far) than usual, Prima Materia moves each track's level
(up to 12 dB at the extremes; a drop drawn at 75% plays 3 dB lower), and Azoth's
transitions into that section, and its build risers and roll, get bigger or smaller.
Right-click a section to put its energy back to the usual. Hand-drawn sections get a dot.
At the usual levels nothing changes.

**Recipes…** saves the whole suite's settings under a name (each plugin writes its own,
matched by track name, or by plugin when it's used once) and loads them into any other
project: your go-to anthem setup in one click. Recipes live in your user application
data folder under *Antioch Suite/Recipes*.

**Your signature.** *Save as my signature* (in Recipes…) stores the whole suite as you like
it: your reverb, gate, lead chain, loudness habits. **New song…** then offers *Start from
my signature*: every plugin loads your signature first, and the style, key and mood go on
top. Ouroboros keeps the new song's map.

**Make versions** keeps the current song in snapshot A, then makes a **radio edit** in B
(about 3:30, short intro and breakdown, at most two drops, streaming loudness target) and
an **extended mix** in C (64 bars longer, DJ intro and outro, club target) and a **club
edit** in D (the same song with a 32-bar intro and outro; Prima Materia takes the pads,
leads and vocals out of them and keeps the kick in, so DJs get beat-only ends; club
target), and returns to A. Click the slots to switch between versions.

**Autosave and version history.** Every 10 minutes (or 5 or 20, or off: *Autosave* in
Recipes…), once the song has played since the last one, Ouroboros saves the whole suite as
a recipe named *Autosave* with the date and time. The 12 newest are kept. *Version history*
in Recipes… lists them, newest first; **Roll back** loads one into every plugin.

**Style report** (template list) tells you how you make trance, from the style log the
suite keeps as you work: Correspondence logs every song it exports with *Export all* (key,
tempo, progression, length, structure) and Mentalism every master that *Finish the song*
completes (loudness and peak). The report lists your usual keys (and how often minor),
tempo, progressions, length, structures and master loudness, plus the structure arrangement
DNA learned from your templates. It's also saved as *My style.txt*.

**Final drop snapshot.** Pick a slot under *Final drop snapshot* and the whole suite morphs
into it for the last drop: every plugin's knobs glide from your settings to the
snapshot's across the build before it, and stay there to the end (switches and choices
change at the drop). Move the playhead back before the build and your own settings
return. So: set up the song, save the bigger final-drop version into B, go back to A, and
choose B here. While a morph is under way the plugins drive their own knobs, so tweak
them with the playhead before that build; a project saved mid-morph still reloads with
your settings.

**Copy a reference's structure.** *Reference…* asks for the reference track's tempo (the
project's or a preset) and an audio file. Ouroboros measures it bar by bar in the
background, finds the bar grid from the first kick, and proposes its sections as a preview
to Apply or Discard, just like Listen.

**Suite overview.** The **Suite** button lists every suite plugin that's loaded and on
which track, and checks the setup. It flags a Kybalion set to sidechain with nothing
routed, a Prima Materia without a role, Lapis or Mentalism off the master, Suite kick with
no kick shared, no key source, no song map, and two Ouroboros at once. It shows **!**
when something needs attention.

At the top, **NEXT:** says the one thing to do now, in order: draw a map, add a key source,
give Prima Materia a role, fix a **key clash** (Emerald Tablet hears a different key from
the one Correspondence shares), add Mentalism, play more of the song, answer the coach,
clear the checklist, and finally *ready to export*. It also warns about plugins adding a
lot of latency, and lists plugins that are loaded but doing nothing (a Kybalion with gate
and duck off, a Solve et Coagula with no vocal), so you can remove them.

**Snapshots per section…** (template list) gives each section type a snapshot slot (or
none): as the song plays into a section, the whole suite recalls that slot. For example,
put the breakdown sound in C and the drop sound in B.

**Render plan…** (template list) lists the versions you made with *Make versions*, with
what to do for each (which slot, reset Mentalism, play through so Lapis settles on its
loudness target, check the checklist, export), its file name and the export settings.

![Suite overview](docs/ouroboros_suite.png)

How it works: Ouroboros shares the section map, not the current section, through the same
kind of shared memory as the key. Each plugin then works out the section from its own copy
of FL's transport, so every reaction lands exactly on the bar line, whatever order FL
processes the tracks in.

## Section scenes (Antioch, Kybalion, Vibration, Solve et Coagula)

![Antioch scenes](docs/antioch_scenes.png)

Each of these three plugins has a **Scenes** tab. For each section type (Intro, Groove,
Breakdown, Build, Drop, Outro) you set how far some of its controls move, as an offset on
top of your own settings. Your knobs never move. Instead, a **ghost ring** on each knob
shows the value actually in use. Changes glide in over the **Morph** time, from instant
up to 1 bar.

**Suggested** fills in sensible starting points:

| Plugin | Breakdown | Build | Drop |
|---|---|---|---|
| Antioch | more reverb, decay, shimmer, echo and harmonics | a little more reverb and echo, brighter | less reverb and shimmer |
| Kybalion | gate much shallower, darker | shorter, more filtered gate | deeper gate |
| Vibration | less Euphoria, wider, more air | more drive and air | more Euphoria and width |
| Solve et Coagula | shallower dips (the music is thin) | slightly deeper | deeper and wider dips |

Scenes follow Ouroboros and are saved with the project. The section is checked once per
audio block, so changes land within about 10 ms of the bar line.

**Print to FL automation** turns the invisible scene moves into automation clips you can
see and edit. Turn on FL's automation recording, click **Print to FL automation** on the
Scenes tab, and play the song: as it plays, the plugin moves its own knobs to the values the
scenes give, and FL records them. Stop playback and the print finishes: scenes switch off,
and FL's clips now do the moving. Clicking the button again before you play cancels it and
puts the knobs back.

### Bigger later drops

The second, third and later drops build on the first, so the last drop isn't a repeat:

- Section scenes have a **Drop 2+** column, added on top of the Drop scene. Suggested:
  a little more echo and shimmer (Antioch), deeper gate and duck (Kybalion), more Euphoria
  and width (Vibration).
- **Azoth's** *Bigger later drops* knob makes later impacts and crashes louder and the
  swell into them longer.
- **Prima Materia** has a **Separate Drop 2+** column, e.g. the lead 2 dB up, hats up,
  atmos down.
- **Correspondence's** full-song lead jumps up an octave in later drops (*Lift later drops*
  in Song patterns). It's also **varied** there (passing notes across leaps, an octave
  climax in the last phrase) with a **harmony line** a diatonic third below, written as
  the SONG HARMONY clip and track in Export all (*Vary the lead + harmony* in Song
  patterns).

### Final-drop key change

Set **Final drop key** in Ouroboros to +1 or +2 semitones, and the song moves up from the
last drop to the end (the timeline marks it **KEY +n**). The whole suite follows:

- Correspondence's song clips (and Export all) are transposed from there.
- Azoth's impact into that drop lands on the new root, and the build risers before it aim
  for the new key.
- Antioch's harmonic echo moves its harmonies up.
- Cause & Effect's tonal riser in the build rises to the new root.

Everything keeps working from the same song key; only the last drop onwards is shifted.

## Ouroboros Listen

Turn on **LISTEN** and play the song through. While it plays, Ouroboros measures each bar's
kick activity (low-end attacks), top-end energy and overall level. When you stop, it
proposes a map, shown as a dashed preview: kick runs become Intro, Groove or Drop, and
kickless stretches become Breakdowns ending in a Build where the top end rises. Where the
top end falls away at the end, it proposes the Outro. Press **Apply** to use the map, or
**Discard** it. Everything snaps to 4-bar phrases. Treat the proposal as a starting point:
check it and nudge the edges if needed.

## Lapis: master finisher

![Lapis](docs/lapis.png)

Lapis goes on the master, just before Mentalism. In **Auto** mode, play the loudest part
(the drop): Lapis learns the gain that puts steady loud sections at the target. By default
it uses Mentalism's target, and its own setting otherwise. It aims 1 LU above the
integrated target, because breakdowns pull the song average down by about that much.

- **Breakdowns are ignored.** It learns only from steady material close to the loudest it
  has heard, so quiet sections and the slide into them never push the gain up.
- **It won't crush the mix.** If reaching the target would need more than 8 dB of
  limiting, it stops and says so.
- **Hold** locks the learned gain, and **Manual** lets you set it yourself. The learned
  gain is saved with the project.
- **Loudness per version:** the gain learned for each target is remembered. Switch from
  the club master to streaming (with *Make versions*, a session or by hand) and Lapis goes
  straight to the gain it learned for that target, or, the first time, starts from the
  current gain moved by the difference in targets (6 dB down from -8 to -14 LUFS).
- **Master fingerprint…** keeps your master sound: after 10 s of a finished master, save
  its chain (target, ceiling, clip, release, tone) and its tonal shape. On a new song,
  *Match* recalls the chain and moves the low and high tone half-way towards the stored
  shape (at most 3 dB a time; match again to refine).
- **Clip** shaves the sharpest peaks with a soft clipper before the limiter.
- The **limiter** is a lookahead true-peak design that detects inter-sample peaks, adds
  77 samples of latency (FL compensates for it automatically), and holds the ceiling.

## Emerald Tablet: key detector

![Emerald Tablet](docs/emerald_tablet.png)

Put it on the master, or on a loop or acapella channel, and play about 20 seconds. It
builds a picture of which pitch classes are present (scoring each note by its whole
harmonic series, so overtones don't skew it) and matches it against all 24 major and
minor key profiles. It shows the best key, its confidence, and the two runners-up.

When the result has been confident and stable for 8 seconds, it **sends the key**:
- If Correspondence is running, it adopts the key, unless its **Accept detected** switch
  is off. Opening a saved project never changes its key.
- If Correspondence isn't running, Emerald Tablet broadcasts the key to the rest of the
  suite itself. It never overrides Correspondence.

It detects major and minor; modes such as Dorian are reported as their nearest major or
minor key.

## Azoth: automatic transition FX

![Azoth](docs/azoth.png)

Put Azoth on its own mixer track, or on any track: the track's audio passes through and the
FX are added on top. It reads the Ouroboros map and plays, timed to the sample:

| Moment | What plays |
|---|---|
| Into a **Drop** | an **impact** (a sub boom tuned to the song's root, from Correspondence or Emerald Tablet) plus a **crash**, with a **swell** of rising, opening noise leading in |
| Into a **Breakdown** | a **downlifter** (falling filtered noise) with a sub drop from the root |
| Across a **Build** | a **snare roll** that speeds up in steps (1/4 → 1/8 → 1/16 → 1/32), gets louder and rises up to 12 semitones |
| Into a **Groove** after the intro | a lighter crash (optional) |

- **Gap before drop** silences the roll, risers and swell for the last half or whole beat.
- **Roll & riser start** sets whether the roll and risers cover the whole build, its
  second half, or just the last 4 bars. **Tone** sets how bright the crash is.
- **Build risers:** the **tonal riser** is detuned saws on the song's root and fifth,
  sweeping up one or two octaves while a filter opens. The **track riser** pitches this
  track's own audio up to an octave across the build and fades it in, the classic
  "everything lifts" effect.
- **Key-change pre-lift** (on by default): over the beat before a drop that changes key,
  this track's own audio glides up to the new key, announcing the lift. Nothing happens
  without a key change.
- **Into the drop** plays with this track's own audio just before each drop: **Tape stop**
  slows it to a halt over the last ½, 1 or 2 beats (pitch falling with it, and silence
  through any gap), or **Stutter** loops the moment before in 1/8s, then 1/16s, then 1/32s.
  Everything is back at full speed on the downbeat.
- **Build shape:** *Smooth* rises evenly; *Stepped* moves the risers and build filter up in
  four distinct stages; *Late surge* holds back and saves most of the rise (and the roll's
  speed-ups) for the end.
- **Build filter** high-passes this track's audio across the build (up to about 1 kHz at
  100%), so it thins out as the tension rises and opens fully on the drop.
- **Your samples…** swaps the synthesised impact, crash or downlifter for your own WAV,
  AIFF or FLAC (mixed to mono, up to 20 s). The knobs still set their level, bigger later
  drops and the energy curve still apply, and the files are remembered with the project.
  Choose *Back to the synthesised…* to return to the in-key sounds. Your samples are
  **level-matched** (their loudest half second is brought to the level of the built-in
  sounds) and, if they have a clear low pitch, **tuned to the song's key** (to the nearest
  root, at most 6 semitones, following the final-drop key change too; switch it off in the
  menu). A **producer tag** slot plays your tag on the first drop, on every drop, or never.
  *Save everything here as a preset…* (in the same *Samples and presets…* menu) keeps every
  Azoth knob and sample under a name, listed in every project.
- **Riser follows the chords** (on by default, with Correspondence running): the tonal riser
  moves with the progression across the build rather than staying on the root.
- **Roll designer:** **Roll from** (1/4 or 1/8) and **Roll to** (1/16, 1/32 or 1/64) set
  the snare roll's first and last speed, **Curve** how it speeds up (steps, smooth or late),
  and the start velocity how quietly it begins before growing into the drop.
- **Play** buttons audition each sound (your sample if you've loaded one).
- Jumping the playhead into the middle of a section never fires its transition. Everything
  is synthesised (no samples), so it stays in key.

**Reverse swells from this track.** Put Azoth on a vocal or lead track and turn this on.
It captures the first beat of each Drop (or of every section) from that track's own
audio, reverses its reverb tail, and plays it so it swells up into the downbeat: the
classic trance reverse-vocal swell, made automatically. To do that it has to hear the
future, so it adds **3 seconds of latency**. FL compensates automatically, keeping the
track in time with everything else. The synth transitions move with it, so they stay
aligned.

## Prima Materia: section mixer

![Prima Materia](docs/prima_materia.png)

Put Prima Materia on individual tracks. For each section type you set a **level** (or Off),
a **low cut** and a **high cut**. The column for the section that's playing lights up, and a
readout shows the values in use.

- **Ramp through builds:** a Build glides from the previous section's settings to its own
  across the build, for example a pad's low cut sweeping up, or a bass fading back in thin.
- **Gap before drop** mutes the track for the last half beat, beat or bar before each Drop.
- **Before every drop** in Ouroboros does it for every track at once: half a beat or a
  beat of silence, or a **kick-only bar** where everything except the Kick role drops out
  for the bar before each drop.
- **Morph** sets how quickly it moves from section to section.
- **Track role presets** set up the usual trance moves in one click:

| Role | What it does |
|---|---|
| Kick | out in breakdowns and builds |
| Bass | out in the breakdown; fades back in thin (low cut) through the build; gap before the drop |
| Hats & percussion | out in breakdowns, quieter in intro and build |
| Pads & chords | up in the breakdown; low cut sweeping up through the build; tucked under the drop |
| Lead | teased (filtered) in the breakdown, rises through the build, full in the drop |
| Vocal | in for groove, breakdown and build; a little under the drop |
| Atmos & FX | up in the breakdown, down in the drop |

With these on each track, you can loop one full 8-bar groove across the whole song and the
arrangement happens by itself.

**Auto-role.** A new Prima Materia picks its role from the track's name: Kick, Bass/Sub,
Hats/Perc/Clap, Pad/Chords/Pluck/Keys, Lead/Saw/Arp, Vox/Vocal and FX/Atmos/Riser. It only
does this while no role has been chosen, so renaming a track later never overrides your
choice. This needs FL to pass the mixer track's name to the plugin. If it doesn't, the
Suite overview shows "(track name unknown)" and you pick roles by hand.

When the name doesn't say, Prima Materia **guesses the role from the sound** after 8 s of
playback: low and punchy is a kick, low and sustained a bass, a bright top hats, a forward
2–4 kHz a lead, warm mids pads. When it's unsure it leaves the role alone; when it guesses,
the status line says so, and choosing another role is always yours.

**Save as my role** keeps this track's section levels, cuts, ramp, gap, breath and pump as
your settings for its role. From then on, every track given that role (by hand, from its
name or from its sound), in any project, starts from your settings.

**Expression** (second row):
- **Breath:** a slow 8-bar swell (up to 3 dB peak to trough) in breakdowns and drops, so
  long chords feel alive.
- **Call / response:** set one track to Call and another to Response; in breakdowns they
  take turns leading every 2 bars (the other sits 5 dB back), like two instruments
  talking.
- **Pump:** ducks this track to the kick pattern a Kybalion shares (Share kick), up to
  about 8 dB, recovering over ~110 ms, with no routing and no Kybalion on the track.
- **Input gain** and **Gain-stage all tracks:** every Prima Materia measures what arrives
  at its input; the button sets each one's Input gain so every track hits -18 dBFS RMS
  before processing. Undo it in Mentalism's History. **Keep at -18** keeps doing
  it while the song plays: every 10 s, if the input has drifted more than 1.5 dB from
  -18 dBFS, the input gain follows.

**Drop entrance** (Ouroboros, Perform row) decides how every drop arrives: **Slam** (all
at once), **Staggered** (kick on beat 1, bass on beat 2, everything else on beat 3), or
**Filter opening** (everything but kick and bass opens from dark over the first bar).

**Low-end manager** (on by default). Below about 120 Hz, the kick and bass own the mix.
Each Prima Materia shares which sections its track plays in, and wherever a Kick or Bass
track is playing, every other role gets an automatic low cut: pads and FX at 150 Hz, lead
200, hats 250, vocal 120. Where no kick or bass plays (typically breakdowns), the pads
keep their low end. Without a kick or bass track in the project nothing is cut, and your
own low-cut knob still wins when it's set higher. The status line shows when it's
cutting.

**Trim and rough mix.** Each Prima Materia measures its track's loudness in the drops and
has a **Trim**. Mentalism's **Rough mix** button sets every Trim so each track sits at the
usual trance level for its role, relative to the kick: bass 2 LU under, vocal 1, lead 4,
pads 7, hats 10, FX 12. It's a starting balance to refine by ear, and **Undo** puts the
trims back. Play a drop first so there's something to measure. Once two or more of your
finished songs are in the style log, the rough mix uses **your** balance for each role (from
those songs) instead of the built-in one.

**Signature chain per role.** *Save as my role* also saves the suite plugins on that track
(Vibration, Antioch, Kybalion...) as a chain for the role. When the role is applied to
another track, in any project, the plugins on it load that chain. This needs FL to pass
track names, as for auto-roles.

## Correspondence: full-song MIDI

The second row of drag tiles (**SONG CHORDS**, **SONG BASS**, **SONG ARP**) exports clips the
length of the whole song that follow the Ouroboros map. **Song patterns…** sets what each
section plays. The defaults:

| Section | Chords | Bass | Arp |
|---|---|---|---|
| Intro | none | offbeat | none |
| Groove | none | rolling | up |
| Breakdown | sustained pads | none | none |
| Build | 16th gate | none | up |
| Drop | 3-3-2 pluck stabs | rolling | up |
| Outro | none | offbeat | none |

**Breakdown progression** (Song patterns) makes breakdowns start the progression on VI
(the default) or IV, for every part, so the drop's return to i lands harder: Am F C G
becomes F C G Am in the breakdown. The **Walking** bass pattern rolls on each root and
steps through the scale to the next chord's root in the last two 16ths before each
change (C to G walks C, B, A, G). **Lift to III halfway** plays the breakdown from i
for its first half and from III for its second, so it lifts before the build.

**Breakdown piano:** Export all also writes a SONG PIANO clip: in every breakdown, each
chord as a low root and its chord tones broken upwards in 8ths and held, with the sustain
pedal (CC 64) down for each chord.

The progression restarts at every section, and notes never spill into the next one. Song
clips need Ouroboros to be running; without it the tile says so.

**Humanise** (Rhythm panel, default 30%) gives every exported clip beat accents, lighter
off-beat accents and small repeatable velocity and timing variation (up to about 5 ms).
At 0% you get the plain pattern; the preview isn't affected.

**Export all…** writes a new song folder (named with the key, tempo and time) holding every
clip, plus *"<key> - all parts.mid"*: one MIDI file with the song's chords, bass, arp and
lead on separate tracks and the sections as markers. Drag it into FL to get the whole song
in one go.

**Song pack for a vocalist** (the Export button's menu) writes a folder for a singer or
topliner: the song's chords, bass and lead guide as MIDI, the sections as markers, and
*For the vocalist.txt* with the key, tempo, chords, the lead guide's range and every section
with its bar and time.

**Your song's name.** Type a title and artist in Song patterns. Export all then names the
folder *"Artist - Title (Am 138 BPM)"* and adds **Song sheet.txt**: title, artist, key,
tempo, the progression, the length and every section with its start time and the key
change. Both are saved with the project.

**Groove** (Song patterns): *Straight*, *Trance shuffle* or *Hard shuffle* delays the off
16ths of every exported clip (by 15% or 30% of a 16th). **Accents as CC 74** (on by
default) also writes each note's accent as a CC 74 value just before it, so a synth's
filter cutoff can follow the groove: map CC 74 to cutoff in your synth.

**Lead expression** (Song patterns): exported leads (MELODY and SONG LEAD) can carry
**glide** and **scoops**. Glide makes a note run into the next on leaps of a minor third or
more, so a mono synth in legato or portamento mode slides between them. Scoops bend the
first note of each phrase up into its pitch from 2 semitones below, over a 1/16, as pitch
bend. Choose the bend range your synth uses (2 or 12 semitones) so the scoop is the right
size, or *Plain lead* for neither. The lead track in the all-parts file gets the glides.

**Climbing chords in drop 2+** (on by default): each chord in later drops gains its top
note an octave higher, so the second drop is bigger and brighter with the same harmony.

**Ghost notes** (on by default): soft in-between 16ths in the bass and arp exports, just
after a note and never on the beat, more of them where the energy curve is higher.

**Drum fills** (on by default): a SONG FILLS clip, General MIDI drums for FPC or any kit:
a snare roll speeding up into every drop, a tom run into breakdowns, a short snare fill
into other sections, a crash on every change and a small fill at each 8-bar phrase end in
drops and grooves. It's a track of its own in the all-parts file too.

**Lead leaves room for the vocal** (on by default): Solve et Coagula marks the bars the
vocal sings in while the song plays; there, the exported lead keeps only its beat 1 and 3
notes (softer) and the harmony and counter-melody step out. Play the song through once
with the vocal on Solve et Coagula's sidechain before exporting. The same switch (*Lead and
vocal take turns*) also has the lead **answer the vocal**: in a section with no lead, the
bar after each vocal phrase gets a short lead reply.

**Phrase turnarounds** (Song patterns, on by default): in the last bar of every 8-bar
phrase inside a section, the bass bounces octaves on beats 3 and 4 and the arp lifts an
octave, so long sections keep moving. The section's own last bar is left to the
transition.

### Melodia: lead melodies

Correspondence also writes a lead melody over the progression, shown in orange in the
piano roll and played along when you press **Play**:

- **Style** sets the rhythm: driving 8ths, trance 3-3-2, anthem or syncopated.
- Melodies come in **call and answer**: a 2-bar call, then the same rhythm and contour
  re-fitted to the next chords.
- Notes on the beat are always chord tones, with scale steps in between, and the melody
  resolves to the last chord's root.
- **New melody** rolls a new seed. The same seed always gives the same melody.
- Drag the **MELODY** tile for one pass, or use **SONG LEAD**. In the song patterns, the
  Lead column defaults to a sparser **Teaser** in breakdowns and the full **Melody** in
  drops.

**Counter-melody** (shown in violet). Wherever the lead holds a note or rests for a beat
or more, a second line answers it a fifth or so below: it moves against the lead's last
direction, puts chord tones on the beat and steps towards the lead's next note, a little
softer. It's written as the **Counter** clip and, wherever the full lead plays in the song,
as SONG COUNTER, its own track in Export all. Switch it off under *Counter-melody* in Song
patterns.

**Your own motif.** Drag a MIDI clip (from FL's piano roll or a file) onto the *Voicing &
Melody* panel. Melodia keeps its first two bars, one note per onset (the top note of any
chord). The motif becomes the call of every phrase as you played it, fitted to the key and
register. The answers keep its rhythm and shape, start on a chord tone and follow the
chords. It's saved with the project and goes into the MELODY and SONG LEAD exports.
**Forget motif** goes back to generated melodies.

**Your hook bank.** *Save the current hook…* (bottom of the preset list) keeps the motif (or
the melody's first two bars) under a name; *Your hooks* in the preset list brings it back
in any project, fitted to the new key.

**Your progressions.** Correspondence reads the style log: progressions you've used in two
or more exported songs are listed under *Your progressions* in the preset list, and a new
song (from Ouroboros's New song…, with the mood left at *As the style*) starts on your
favourite.

**Drop anything MIDI.** The same panel works out what you dropped:

- **A chord clip** (most onsets have three or more notes) sets the key, the number of
  chords, their degrees (with major or minor where they're not the key's own) and how
  long each lasts. Drop your favourite progression and the whole engine follows it.
- **Two or more melody clips** teach Melodia your **melodic DNA**: where in two bars you
  like to put notes, and how far you like to move between them. New melodies use your
  rhythm and your kind of steps and leaps, over any chords. It's saved with the project;
  **Forget my DNA** clears it.
- **A chord clip** also sets **your voicing**: close, open or wide from how far your chords
  spread, the register they sit in, and whether you put the root underneath.
- **Bass clips** (low, one note at a time) teach your **bassline DNA**: which 16ths you play
  and where you jump up an octave. The Bass pattern switches to **Mine (learned)**, which
  plays your rhythm over any progression. Saved with the project; Forget clears it.
- **One melody clip** becomes the motif, as above.

## Solve et Coagula: vocal unmasking

![Solve et Coagula](docs/solveetcoagula.png)

Put Solve et Coagula on the music (a pad, lead or music bus) and feed the vocal into its
**sidechain**. It follows the vocal in 8 bands from 250 Hz to 6.3 kHz. Where the vocal is
loud enough and prominent, it opens a matching EQ dip in the music; when the vocal stops,
the dips close. The music keeps its full sound between phrases, and the vocal sits on top
without being turned up.

- **Depth:** the deepest dip per band (default 6 dB).
- **Sensitivity:** how quiet a vocal still triggers dips.
- **Focus:** narrow cuts only the vocal's strongest bands; wide follows more of it.
- **Speed:** how fast dips open and close (fast, medium or slow).
- **Mix:** scales all dips (0% = off).

**Scenes** (second tab) move Depth, Sensitivity, Focus and Mix per section, like the
other scene plugins: by default the dips are deeper in drops, where the music is dense,
and shallower in breakdowns.

With no sidechain, or a silent one, the music passes through bit-for-bit. The display shows
the vocal's level per band (up) and the dip in the music (down).

**Vocal chops.** Since Solve et Coagula already hears the vocal, it can chop it too. Turn
on **Vocal chops** and choose where they play (builds, drops, both, or breakdowns). Outside
those sections it keeps the last whole bar of vocal it heard; inside them it cuts that bar
into sixteen 1/16 slices and plays them on the beat grid, on top of the music bus:
**Stutter** (each beat's first slice four times), **Off-beats**, **3-3-2** or **Shuffled**,
with a crisp gate on every chop, **Level**, and **Octave up**.

In FL Studio, select the vocal's mixer track, right-click the arrow below the music's
track and choose **Sidechain to this track**. Then select that input in the plugin
wrapper's settings, as for Kybalion's sidechain.

## How the key sharing works

Correspondence writes the key into a tiny block of shared memory several times a second.
Antioch and Cause & Effect read it each audio block, and their **Follow Key** switch
(on by default) decides whether they use it.

- If no Correspondence is running, or you turn Follow Key off, each plugin uses its own
  Key setting.
- If you load several Correspondence instances, the most recently changed one wins.

## Installing in FL Studio 26 (Windows)

1. Open the repository's **Actions** tab on GitHub, pick the latest successful **Build**
   run, and download **AntiochSuite-Windows-VST3**. Version tags (`v0.2.0`, …) also attach
   the zip to a GitHub Release.
2. Unzip it and copy the twelve `.vst3` folders to `C:\Program Files\Common Files\VST3\`.
3. In FL Studio, go to **Options → Manage plugins → Find installed plugins**.
4. Load Antioch, Cause & Effect, Kybalion and Vibration on mixer inserts, Correspondence on any insert,
   and Mentalism last on the master,
   Ouroboros on any insert, Lapis on the master before Mentalism, Emerald Tablet wherever
   you want the key detected, Azoth on its own track, and Prima Materia on individual tracks, and Solve et Coagula on the music under a vocal.
   To use Kybalion's sidechain mode, route the kick track to Kybalion's mixer track as a
   sidechain, then select that input in the plugin's wrapper settings. Or skip the routing:
   put a Kybalion on the kick track with **Share kick** on, and choose **Suite kick**
   everywhere else.
5. Name your mixer tracks (Kick, Bass, Pad, Lead, Vox...) before loading Prima Materia and
   Kybalion, so they can set themselves up.

On macOS, copy the `.vst3` folders to `~/Library/Audio/Plug-Ins/VST3/`. The CI builds are
not code-signed, so you may need to run
`xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/*.vst3`.

## Building from source

You need CMake 3.22+ and a C++17 compiler (Visual Studio 2022 on Windows, Xcode on
macOS). JUCE 8 is downloaded automatically at configure time.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target Antioch_VST3 CauseAndEffect_VST3 Correspondence_VST3 Kybalion_VST3 Vibration_VST3 Mentalism_VST3 Ouroboros_VST3 Lapis_VST3 EmeraldTablet_VST3 Azoth_VST3 PrimaMateria_VST3 SolveEtCoagula_VST3
# Results: build/plugins/<Plugin>/<Plugin>_artefacts/Release/VST3/
```

Each plugin also has a `<Plugin>_Standalone` target for testing without a DAW.

### Tests

```bash
cmake -B build -DSUITE_BUILD_TESTS=ON
cmake --build build --target AntiochTests CauseAndEffectTests CorrespondenceTests KybalionTests VibrationTests MentalismTests OuroborosTests LapisTests EmeraldTabletTests AzothTests PrimaMateriaTests SolveEtCoagulaTests
./build/plugins/Antioch/AntiochTests_artefacts/Release/AntiochTests test-output   # xvfb-run on headless Linux
```

The tests render audio offline and check behaviour, not just that nothing crashes:

- **Antioch:** echo timing, reverb decay time, how much ducking reduces the reverb, that
  shimmer can't self-oscillate, that nothing enters the throw delay until thrown,
  auto-throw only at phrase ends, and key following.
- **Cause & Effect:** bar-accurate timed builds, exactly one drop, the bass removed late in
  the build and back after the drop, the tonal riser landing on the key's root, and that
  it's bit-transparent at 0.
- **Correspondence:** chord spelling, that Auto chords stay in the scale for every scale,
  that voice leading reduces movement, every rhythm pattern, MIDI files read back
  correctly, and key broadcast.
- **Kybalion:** gate steps on the 16th grid, swing, stereo offset, filter, beat-grid
  ducking depth and recovery, nudge, Breakdown, the low-band split (lows ducked, highs
  untouched), and sidechain kick triggering.
- **Vibration:** flat response at neutral settings, correctly reported latency, each EQ
  band, the macro amounts, mono-below and width, multiband squeezing (34 dB down to
  about 13 dB), and auto gain holding level within 1 dB under heavy drive.
- **Mentalism:** an EBU-style −23 dBFS sine reads −23.0 LUFS, the relative gate works,
  the true peak catches an inter-sample peak that sample-peak metering misses by 3 dB,
  correlation, band detection, the advice rules, reference-track analysis and its
  save/restore, and bit-identical pass-through.
- **Ouroboros:** section editing (insert, retype, move, delete, resize, snap), the map's
  trip through shared memory, section lookup in other time signatures, marking at the
  playhead, and save/restore. The followers' own tests check that builds ramp and drop on
  the Build section, that ducking pauses in breakdowns, and that throws land on section
  ends. **Listen** is tested on a synthetic 104-bar arrangement, and its proposal matches
  the real structure exactly.
- **Scenes:** a Breakdown offset moves the value in use but not the knob, switching
  Scenes off restores the knob value, and scenes survive save/restore.
- **Lapis:** latency matches what it reports; sample peaks stay at the ceiling and true
  peak stays within 0.3 dB under 18 dB of drive; it lands loud material within 1 LU of
  target; the gain doesn't move through a 10 dB quieter breakdown; Hold works, and it
  follows Mentalism's target.
- **Emerald Tablet:** A minor, C major, E minor and F major cadences are detected
  correctly; it sends only when confident; it broadcasts alone without Correspondence and
  never overrides it. Correspondence's tests check that it adopts suggestions and that
  projects keep their key.
- **Azoth:** nothing plays at the song start; a downlifter at the breakdown; exactly 30 roll
  hits across a 2-bar build (2 + 4 + 8 + 16), or 26 with a 1-beat gap; the swell rises;
  the impact lands on the root (A1 when the key is A minor); no FX when the playhead
  jumps into a section; the track's audio passes through.
- **Prima Materia:** the kick role is silent in the breakdown and build and back for the
  drop; the bass role fades in thin through the build with a gap; the pads' breakdown sits
  4 dB above the drop; instant vs gliding Morph; unchanged without a map.
- **Full-song MIDI:** each section plays only its own parts, nothing spills over, the drop
  starts on chord 1, and export is refused without a map.
- **Melodia:** every note is in key (or in the chord), notes on the beat are chord tones,
  the answer repeats the call's rhythm, it stays within a singable range, it ends on the
  last chord's root, and seeds are reproducible. The breakdown teaser is sparser than the
  drop melody. A dropped MIDI motif is rebased to bar 1 (top note per onset, 2 bars), the
  call plays it as written, the answer keeps its rhythm and starts on a chord tone,
  out-of-key notes are fitted to the key, and the motif survives a save and load.
- **Bigger later drops:** the Drop 2+ scene adds on top of the Drop scene only from the
  second drop; Azoth's later impacts are louder and swells longer; Prima Materia uses its
  Drop 2+ column; the song lead goes up an octave.
- **Arrange:** generated maps are exact in length and built from 8-bar phrases, every
  drop follows a build and keeps 32 bars, three drops fit in 288 bars, a 96-bar radio
  edit works, and a song too short for three drops gets fewer.
- **Fix buttons:** each coach problem carries its fix; Prima Materia lowers an unmuted
  breakdown by 2 dB, leaves a muted one alone, applies each fix once, and undoes it; the
  Drop 2+ column starts from the drop's settings; Cause & Effect and Azoth step their
  build and later-drop controls; a plugin loaded later doesn't replay old fixes; the
  fixed sections are re-measured.
- **Snapshots:** Save stores each plugin's own settings, Recall restores them, an empty
  slot changes nothing, slots survive a save and load, old projects load as before, and a
  plugin loaded later doesn't replay old commands.
- **Suite overview:** every plugin is listed with its track; unrouted sidechains, missing
  roles, Lapis off the master and Suite kick with nothing shared are flagged; a tidy
  session gets "Everything is set up".
- **Suite kick:** the kick track learns an offbeat pattern exactly; another track ducks
  right after each shared kick and not on the beat grid; with nothing shared it falls back
  to the grid; a Kybalion on "Kick 1" shares automatically.
- **Fit to kick:** a synthetic kick measures 196 ms and 55.1 Hz; hold and release follow
  it; the tuning text recognises in tune, the fifth and 2 semitones sharp.
- **Auto-role and rough mix:** track names map to roles; a new Prima Materia on "Sub Bass"
  becomes Bass and a later rename doesn't override it; the rough mix trims a pad to 7 LU
  under the kick (re-measured afterwards), leaves the kick alone, and Undo restores it.
- **Export all:** 8 clips plus the all-parts file, with 4 tracks of notes and the
  sections as markers (Groove, Drop, Breakdown, Drop 2).
- **Final-drop key change:** Ouroboros shares the lift, which starts at the last drop and
  runs to the end; song chords move a semitone up there and nowhere else; Azoth's impact
  lands on B1 instead of A1 with +2; Antioch's harmonies go from A to B; Cause & Effect's
  build riser aims for the new root.
- **Gate fills:** Gap silences the last beat before a drop; Stutter chops at 1/32 only in
  the fill bar; Auto plays half-time into a breakdown and a roll into the first drop;
  Off plays the pattern through.
- **Varied later drops:** the second drop's lead gains passing notes (all in key) and a
  climax note, and the harmony is a diatonic third under it, only in later drops.
- **Into the drop:** a 440 Hz track plays normally, slows to about 110 Hz in the last half
  beat with tape stop and is back at 440 Hz on the drop; with a 1-beat gap the stop comes
  first, then silence; stutter repeats the same 1/8 slice exactly.
- **Build shape and filter:** the stepped riser holds its first stage, late surge gives 21
  roll hits instead of 30, and the build filter takes a 100 Hz track from 0.3 to 0.006 at the
  end of the build and back to 0.3 on the drop.
- **Your samples:** a 44.1 kHz WAV plays at its own pitch on the drop instead of the sub
  boom, stops when it ends, is remembered with the project and can be cleared; a missing
  file is refused.
- **Build risers:** silent before the build, rising through it, starting on the song's
  root, sweeping away from it, and stopping at the drop; the track riser pitches the
  track's own audio up an octave.
- **Final drop snapshot:** the morph amount is 0 in the first drop and breakdown, 0.5
  half-way through the build into the last drop and 1 from that drop to the end; a plugin
  reaches half-way, then exactly the snapshot, switches wait for the drop, the user's
  values come back, and a project saved mid-morph reloads with them.
- **Reference structure:** a synthetic 104-bar song written to a file with 0.37 s of
  leading silence gives exactly the same map as Listen hearing it play.
- **Fix history:** two fixes undone in either order; an older fix doesn't overwrite a
  parameter changed since; each fix can only be undone once.
- **Export checklist:** a finished master passes; over the ceiling, clipped, wide bass and
  too quiet all fail; a silent breakdown is named; nothing measured means not ready.
- **Walking bass and breakdowns:** A to F walks A, G, F; C to G walks B, A into G; every
  note is in key; a breakdown starts on F (VI) and the drop on Am; "Start on IV" puts Dm
  first.
- **Before every drop:** a kick-only bar silences the pads for the bar while the kick
  plays on; a beat of silence stops everything; a per-track 1-bar gap works.
- **Low-end manager:** with a bass track that plays in drops, a pad's 60 Hz drops 20 dB
  in the drop (16 dB more than its 1 kHz) but not in the breakdown; nothing changes with
  the manager off or no kick/bass loaded; a Bass track reports playing in drops only.
- **New song:** Uplifting anthem builds a 240-bar map with two drops and the key change,
  and sends D minor to the suite; Correspondence takes the key and the style's
  progression, Mentalism the loudness target, Vibration its suggested scenes; a plugin
  loaded later doesn't replay it.
- **Counter-melody:** answers the lead between its notes (never on or across one), below
  it, in key, with chord tones on the beat; in the song it plays with the full lead in the
  drop, not under the breakdown teaser, and can be switched off; Export all gains the
  Counter clip and track.
- **Climbing chords, ghosts, fills, vocal room:** drop 2's chords gain a top note an octave
  up; ghost notes fill gaps (more in the drop than the groove, never overlapping, none when
  off); fills give a 16-hit roll into the drop, toms into the breakdown, crashes and phrase
  fills; where the vocal sings (bars 9-12) the lead keeps only beats 1 and 3.
- **Bassline and voicing DNA:** an off-beat bass clip with octave jumps teaches the Mine
  pattern, which plays those steps over any chords and is saved; a wide chord clip with the
  root underneath sets the voicing and register.
- **Vocal bars:** Solve et Coagula marks bars 1-2 (vocal) and not 3-4 (silence).
- **Gate on chord changes:** accent opens a closed downbeat at each of 4 changes; restart
  with half-bar chords never reaches the pattern's second half.
- **Samples in key:** a 100 Hz boom is found, tuned to 110 Hz (A) and level-matched from
  -26 dBFS; the tag plays on the first drop only, every drop, or never.
- **Role settings and auto-role:** Save as my Pads applies to new Pads tracks only and can
  be forgotten; spectra map to kick, bass, hats, lead and pads (nothing when unsure); a
  kick with no role becomes the Kick after 8 s.
- **Master fingerprint:** needs 10 s; matching a dark master to a bright one recalls the
  chain and moves the tone up top and down low.
- **Build brightness, pass compare, mix report:** a build that rises 4 LU but only 0.5 dB
  in top end is flagged, +6 dB passes, no top end isn't judged; a pass 1.2 LU louder with
  2 kHz up is described; the mix report is saved as text.
- **Club edit:** version D has a 32-bar intro and outro around the same body; Prima
  Materia takes pads out of them and puts the kick in; other versions leave the mixer alone.
- **DJ phrasing:** a 20-bar intro, a 24-bar outro and the sections they push off the grid are
  found; the fix gives whole 16-bar phrases and undo restores the map; the checklist warns,
  then passes.
- **Autosave:** saves the suite as a recipe, keeps the 12 newest and waits for playback.
- **Style report:** keys, tempo, progressions, length and loudness from a style log; Export
  all logs the song.
- **Key-change pre-lift:** a 440 Hz track reaches 485 Hz by the end of the last beat (B is
  494) and is 440 again in the drop; off, or without a key change, it's untouched.
- **Reverb freeze:** fires once entering the breakdown, holds the drop's reverb long after
  it would have died, and fades out over 4 bars.
- **Masking watch:** a clash joins the suggestions after three checks in a row and leaves
  when it's gone.
- **Intensity ride:** off by default; on the mod wheel, 0 / 64 / 127 give 0 / 100 / 200%;
  other controllers are ignored.
- **Arrangement DNA:** from three maps (two sharing intro-breakdown-build-drop-outro), it
  learns that structure, lays it out at 160 bars in whole phrases with your usual drop
  energy, or at your usual length; one map isn't enough.
- **Section markers:** a marker at every section (Intro@1, Breakdown@25, ...), exported with
  a cue sheet.
- **Lead glide and scoops:** scoops only on phrase starts; the leap glides, the step doesn't;
  the bend goes from 2 semitones down back to centre within a 1/16 (smaller with a 12-semitone
  range); MELODY exports carry it by default, never chords.
- **Print scenes to FL automation:** armed until playback; the groove leaves the knob alone;
  in the drop the knob itself moves and is what plays; stopping switches scenes off; cancel
  restores the knobs.
- **My sound:** two tracks at -10 and -14 LUFS give a -12 LUFS target, kept as a file; before
  learning, the club target; after, your loudness, ceiling and tonal balance.
- **Phrase turnarounds:** in a 16-bar drop only bar 8 changes: 3 bass notes and 16 arp
  notes an octave up; bar 16 is left alone.
- **Signature:** *Save as my signature* stores a recipe; New song from it sends the
  signature load just before the session, and Ouroboros keeps the new 240-bar map.
- **Lead fingerprint:** needs 5 s of the lead; saves and loads back with its character;
  matching a dark lead to a bright fingerprint recalls drive and opens presence and air.
- **Vocal chops:** silent while listening; in the build it plays the breakdown vocal's last
  bar, 300 Hz on beat 1 and 400 Hz on beat 2 (Stutter), each chop gated before the next 1/16.
- **Learning from MIDI:** an Am F C G block-chord clip sets A minor, 4 chords, i VI III
  VII; two melody clips teach DNA (16 notes) that keeps their dotted rhythm, new melodies
  land on it, it's saved and Forget clears it; Trance shuffle moves only the off 16ths;
  exports carry one CC 74 per note (none when off); a titled song exports to
  *"Me - Sunrise (Am 138 BPM)"* with a song sheet, and the title is saved.
- **Mood:** a Dark song draws 20% breakdowns and sends the mood; Euphoric sets 120%
  intensity; Melancholic gives Correspondence Heartbreak over the style's progression;
  Vibration's drop euphoria goes up for Euphoric and down for Dark.
- **Gate patterns:** a saved pattern is a file that loads back with its steps and applies
  in another project.
- **Humanise:** off-beat bass notes are accented, timing moves by at most about 5 ms,
  the same clip always humanises the same way, and 0% gives the plain pattern.
- **DJ check:** a pad in the intro is flagged with a fix; beat-led passes; no kick in the
  intro is flagged; the fix takes the pads out of the intro and outro and leaves the kick.
- **Recipes and versions:** a recipe saved in one project loads into another; an unknown
  recipe changes nothing; versions give a 120-bar radio edit in B, a map 64 bars longer
  in C and the original in A; Mentalism switches to streaming / club accordingly.
- **Arc match:** a breakdown drawn at 40% that plays 5.2 LU too loud gets a -5 dB fix for
  that section (and no contrast complaint); within 3 LU, none. In Prima Materia the fix
  takes the drop down 3 dB, survives a save, is ignored once the map moves the section and
  undoes cleanly.
- **Auto-coach and Finish:** off, nothing is sent; on, one pass sends the coach's fixes
  and waits to hear those sections again. Finish sends gain staging first and turns the
  auto-coach on; after the drop is played again it sends the rough mix and starts coaching
  from a fresh listen.
- **Loudness per version (Lapis):** a new -14 target starts 6 dB below the club gain; each
  target's gain is recalled when you switch back, and saved with the project.
- **Match reference:** with a reference that has 2 dB less low end and 4 dB more top, it
  asks for -1 dB low and +2 dB high; Lapis' shelves move 40 Hz and 14 kHz by that much,
  leave 1 kHz alone, and undo flattens them.
- **Masking map:** a pad and a lead both strong at 2 kHz are found and the pad is carved;
  tracks whose energy is elsewhere don't clash; the carve reaches only the addressed
  track, deepens on a second press, and measures about 4.5 dB down at 2 kHz.
- **Energy curve:** untouched sections have their type's energy; a drawn energy is
  shared and saved; followers see 70% as 0.7 of a drop's usual; a neutral Prima Materia
  plays a drop drawn at 75% 3 dB lower; Azoth gives a half-energy drop half the impact.
- **Perform, templates, notes:** Intensity is shared; a G played mid-bar queues the drop
  for the next bar line, the followers switch exactly there as a new section, black keys do
  nothing, and switching Live off returns to the map; a template keeps its energy curve;
  a rough mix is noted and the notes are saved; the four collision warnings fire.
- **Expression:** a staggered drop brings in kick, bass and the rest on beats 1, 2 and 3;
  filter opening starts the drop dark; breath swells 3 dB over 8 bars; call and response
  take turns by 5 dB; pump ducks 5+ dB after each shared kick; gain staging measures a
  -9 dBFS input and sets -9 dB of input gain.
- **Solve et Coagula:** bit-exact pass-through with no or a silent sidechain; a 1 kHz
  vocal cuts 1 kHz music by about 7 dB but leaves 250 Hz and 4 kHz within 0.4 dB; more
  depth cuts more; the dips open while the vocal sings and close after it stops; a vocal
  below the threshold is ignored; narrow focus leaves the vocal's weaker bands alone. With
  scenes, a drop dips about 7 dB deeper than a breakdown.
- **Arrangement coach:** a flat arrangement gets flagged (breakdown only 1.0 LU below the
  drop, build not rising); a good arc gets no complaints; a weaker second drop is
  flagged.
- **Reverse swells:** 3 s of latency is reported; the track's audio comes out exactly 3 s
  later; the swell rises into the downbeat and plays in full; the impact stays aligned
  with the delayed audio.
- **Round 10, expression:** the breakdown piano holds chord tones with the pedal down;
  *Lift to III* changes the breakdown's second half; the lead answers after a vocal phrase;
  the riser follows the chords; the roll designer's speeds; the dynamic gate is shallower
  early in a drop and in a quiet breakdown; the reverb and lead widen on the tension chord;
  pitch-tracking brightness finds the lead's octave and adds air to high notes.
- **Round 10, identity:** progressions used in two songs are offered and applied to a new
  song; hooks are saved and recalled in another key; Azoth presets round-trip; the coach
  learns your thresholds; the rough mix uses your balance; a role's chain is saved and
  loaded on another track of that role.
- **Round 10, automation:** the next step follows the order (no map, no key source, a key
  clash, ..., ready); idle plugins and high latency are listed; sections too short for
  their job are flagged and extended (and undone); the kick/bass phase check spots an
  inverted bass; continuous gain staging moves a drifting input back to -18 dBFS; the song
  pack has the clips, the markers and a note with key, chords, range and structure; a
  snapshot is recalled as its section starts; the render plan lists the versions.

They also write example WAVs and UI snapshots to `test-output/`.

## Project layout

```
shared/                  code used by every plugin
  SuiteDSP.h             delay line, pitch shifter, resonator, filters, EQ, crossover, FDN reverb, oscillators
  Spectrum.h             audio-thread tap and real-time spectrum display
  SharedMemory.*         named shared memory block (Windows/macOS/Linux)
  KeyBus.*               cross-plugin key broadcast
  SectionBus.*           cross-plugin arrangement broadcast + SectionFollower helper
  TargetBus.*            Mentalism's loudness target, for Lapis
  FixBus.*               Mentalism's fix-it messages and the receiver the other plugins use
  CommandRing.*          lock-free shared-memory command ring (fixes, snapshots)
  SuiteProcessor.h       base class of every plugin: snapshots, registry, track name
  Registry.*             which suite plugins are loaded, on which track, with status
  KickBus.h/.cpp         the shared kick pattern (KickLearner) for Suite kick ducking
  MorphBus.*             Ouroboros' final-drop snapshot morph amount
  Session.h, SessionBus  New song styles and the message that sets up the suite
  Scenes.h, SceneUI.h    section scenes engine, ghost rings and scene editor panel
  Analysis.h             BS.1770 loudness, true peak, stereo and band meters
  Music.h                scales, note names, tempo divisions
  HostInfo.h             tempo/position/time-signature snapshot from the host
  SuiteUI.*              shared look-and-feel, knobs, toggles, tabs
  TestUtils.h            offline render helpers for the tests
plugins/Antioch/         Engines.h (echo, reverb, throw), processor, tabbed UI, tests
plugins/CauseAndEffect/  BuildClock.h (timed build logic), processor, UI, tests
plugins/Correspondence/  ChordEngine.h (theory, voicing, patterns, MIDI), preview synth, UI, tests
plugins/Kybalion/        Engines.h (gate, ducker, sidechain trigger), step grid, curve view, tests
plugins/Vibration/       EQ, oversampled saturation, 3-band multiband, width, auto gain, tests
plugins/Mentalism/       Advisor.h (suggestions), Coach.h (arrangement arc), Checklist.h (pre-export), tests
plugins/Ouroboros/       Arrangement.h (editing, Arrange), Listen.h (section detection), SuiteCheck.h, timeline editor, tests
plugins/Lapis/           Limiter.h (lookahead true-peak limiter), auto-gain loop, tests
plugins/EmeraldTablet/   KeyDetector.h (salience chromagram + key profiles), tests
plugins/Azoth/           Voices.h (impact, crash, downlifter, snare), ReverseSwell.h, section-driven triggering, tests
plugins/PrimaMateria/    Params.h (per-section settings, role presets), section mixer, tests
plugins/SolveEtCoagula/  Unmasker.h (sidechain-keyed dynamic EQ), band display, tests
.github/workflows/       Windows/macOS/Linux CI builds of all twelve
```

## Licence note

JUCE is dual-licensed: AGPLv3, or a commercial JUCE licence. For personal use, as here,
nothing needs to be done. If you ever distribute the plugins, either release the source
under AGPLv3 or get a JUCE licence (there is a free tier for small revenue).
