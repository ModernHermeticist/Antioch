# Antioch Suite

Six VST3 plugins for uplifting trance production in FL Studio. They're built to replace
the setup work you'd otherwise redo in every project. All three share one key: set it once
in **Correspondence**, and the others follow it.

| Plugin | What it replaces |
|---|---|
| **Antioch**: harmonic echo, euphoric reverb, throw delay | A delay, a reverb + EQ + sidechain chain, and the send automation for delay throws |
| **Cause & Effect**: build-up macro | The 6+ automation clips (filter, reverb, noise, riser, gate, width) you draw for every build |
| **Correspondence**: key and chord engine | Working out progressions, voicings and offbeat basslines in the piano roll by hand |
| **Kybalion**: trance gate + smart ducking | Drawing gate shapes in Gross Beat or Peak Controller, and sidechain/LFO Tool setups |
| **Vibration**: lead polish strip | The EQ → saturation → multiband → widener chain on every lead |
| **Mentalism**: mix assistant | Loudness meters, correlation meters, reference A/B and guesswork about what's wrong |

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
2. Unzip it and copy the six `.vst3` folders to `C:\Program Files\Common Files\VST3\`.
3. In FL Studio, go to **Options → Manage plugins → Find installed plugins**.
4. Load Antioch, Cause & Effect, Kybalion and Vibration on mixer inserts, Correspondence on any insert,
   and Mentalism last on the master.
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
cmake --build build --config Release --target Antioch_VST3 CauseAndEffect_VST3 Correspondence_VST3 Kybalion_VST3 Vibration_VST3 Mentalism_VST3
# Results: build/plugins/<Plugin>/<Plugin>_artefacts/Release/VST3/
```

Each plugin also has a `<Plugin>_Standalone` target for testing without a DAW.

### Tests

```bash
cmake -B build -DSUITE_BUILD_TESTS=ON
cmake --build build --target AntiochTests CauseAndEffectTests CorrespondenceTests KybalionTests VibrationTests MentalismTests
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

They also write example WAVs and UI snapshots to `test-output/`.

## Project layout

```
shared/                  code used by every plugin
  SuiteDSP.h             delay line, pitch shifter, resonator, filters, EQ, crossover, FDN reverb, oscillators
  Spectrum.h             audio-thread tap and real-time spectrum display
  KeyBus.*               cross-plugin key broadcast (named shared memory)
  Music.h                scales, note names, tempo divisions
  HostInfo.h             tempo/position/time-signature snapshot from the host
  SuiteUI.*              shared look-and-feel, knobs, toggles, tabs
  TestUtils.h            offline render helpers for the tests
plugins/Antioch/         Engines.h (echo, reverb, throw), processor, tabbed UI, tests
plugins/CauseAndEffect/  BuildClock.h (timed build logic), processor, UI, tests
plugins/Correspondence/  ChordEngine.h (theory, voicing, patterns, MIDI), preview synth, UI, tests
plugins/Kybalion/        Engines.h (gate, ducker, sidechain trigger), step grid, curve view, tests
plugins/Vibration/       EQ, oversampled saturation, 3-band multiband, width, auto gain, tests
plugins/Mentalism/       Analysis.h (BS.1770 loudness, true peak, stereo, bands), Advisor.h, tests
.github/workflows/       Windows/macOS/Linux CI builds of all six
```

## Licence note

JUCE is dual-licensed: AGPLv3, or a commercial JUCE licence. For personal use, as here,
nothing needs to be done. If you ever distribute the plugins, either release the source
under AGPLv3 or get a JUCE licence (there is a free tier for small revenue).
