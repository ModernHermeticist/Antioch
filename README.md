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
   longer swells and the lead up an octave (see *Bigger later drops*).
10. **Solve et Coagula** makes room for the vocal in the music, only where and when it sings.

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
  can be offbeat, rolling, galloping or sustained. Arps go up, down, up-down or bounce.
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
  plays, so it blooms in the gaps without washing out the lead.
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

![Kybalion polarity tab](docs/kybalion_polarity.png)

**Polarity** is ducking with no sidechain routing.

- In **Tempo** mode it ducks on the beat grid (every 1/2, 1/4 or 1/8). A negative
  **Nudge** ducks slightly before the beat, which works like lookahead with no added
  latency.
- In **Sidechain** mode it follows a kick you route into the plugin's sidechain input,
  with a threshold.
- Presets for each element: Bass (tight), Pads (deep), Leads (gentle), Classic pump,
  Reverb & delays, and Lows only.
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

**Reset** starts a new measurement, for example after changing the mix.

**Arrangement coach.** With Ouroboros running, Mentalism also measures loudness section
by section, and the two halves of each build. The **Arrangement** tab charts it and checks
the arc:
- Is each breakdown 2–12 LU below the drop that follows?
- Does each build rise by at least 1 LU?
- Does every later drop hit at least as hard as the one before?
- Does the intro leave headroom for the drop?

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
knobs back. The fixed sections are measured again the next time they play, so click Fix
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
  in Song patterns).

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

- **Gap before drop** silences the roll and swell for the last half or whole beat.
- **Roll starts** sets whether the roll covers the whole build, its second half, or just
  the last 4 bars. **Tone** sets how bright the crash is.
- **Play** buttons audition each sound.
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
- **Gap before drop** mutes the track for the last half or whole beat before each Drop.
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

The progression restarts at every section, and notes never spill into the next one. Song
clips need Ouroboros to be running; without it the tile says so.

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

**Your own motif.** Drag a MIDI clip (from FL's piano roll or a file) onto the *Voicing &
Melody* panel. Melodia keeps its first two bars, one note per onset (the top note of any
chord). The motif becomes the call of every phrase as you played it, fitted to the key and
register. The answers keep its rhythm and shape, start on a chord tone and follow the
chords. It's saved with the project and goes into the MELODY and SONG LEAD exports.
**Forget motif** goes back to generated melodies.

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
   sidechain, then select that input in the plugin's wrapper settings.

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
plugins/Mentalism/       Advisor.h (suggestions), Coach.h (arrangement arc), tests
plugins/Ouroboros/       Arrangement.h (section editing), Listen.h (section detection), timeline editor, tests
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
