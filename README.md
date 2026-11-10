# Antioch Suite

Three VST3 plugins for uplifting trance production in FL Studio. They're built to replace
the setup work you'd otherwise redo in every project. All three share one key: set it once
in **Correspondence**, and the others follow it.

| Plugin | What it replaces |
|---|---|
| **Antioch**: harmonic echo, euphoric reverb, throw delay | A delay, a reverb + EQ + sidechain chain, and the send automation for delay throws |
| **Cause & Effect**: build-up macro | The 6+ automation clips (filter, reverb, noise, riser, gate, width) you draw for every build |
| **Correspondence**: key and chord engine | Working out progressions, voicings and offbeat basslines in the piano roll by hand |

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
2. Unzip it and copy the three `.vst3` folders to `C:\Program Files\Common Files\VST3\`.
3. In FL Studio, go to **Options → Manage plugins → Find installed plugins**.
4. Load Antioch and Cause & Effect on mixer inserts, and Correspondence on any insert.

On macOS, copy the `.vst3` folders to `~/Library/Audio/Plug-Ins/VST3/`. The CI builds are
not code-signed, so you may need to run
`xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/*.vst3`.

## Building from source

You need CMake 3.22+ and a C++17 compiler (Visual Studio 2022 on Windows, Xcode on
macOS). JUCE 8 is downloaded automatically at configure time.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target Antioch_VST3 CauseAndEffect_VST3 Correspondence_VST3
# Results: build/plugins/<Plugin>/<Plugin>_artefacts/Release/VST3/
```

Each plugin also has a `<Plugin>_Standalone` target for testing without a DAW.

### Tests

```bash
cmake -B build -DSUITE_BUILD_TESTS=ON
cmake --build build --target AntiochTests CauseAndEffectTests CorrespondenceTests
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

They also write example WAVs and UI snapshots to `test-output/`.

## Project layout

```
shared/                  code used by every plugin
  SuiteDSP.h             delay line, pitch shifter, resonator, filters, FDN reverb, oscillators
  KeyBus.*               cross-plugin key broadcast (named shared memory)
  Music.h                scales, note names, tempo divisions
  HostInfo.h             tempo/position/time-signature snapshot from the host
  SuiteUI.*              shared look-and-feel, knobs, toggles, tabs
  TestUtils.h            offline render helpers for the tests
plugins/Antioch/         Engines.h (echo, reverb, throw), processor, tabbed UI, tests
plugins/CauseAndEffect/  BuildClock.h (timed build logic), processor, UI, tests
plugins/Correspondence/  ChordEngine.h (theory, voicing, patterns, MIDI), preview synth, UI, tests
.github/workflows/       Windows/macOS/Linux CI builds of all three
```

## Licence note

JUCE is dual-licensed: AGPLv3, or a commercial JUCE licence. For personal use, as here,
nothing needs to be done. If you ever distribute the plugins, either release the source
under AGPLv3 or get a JUCE licence (there is a free tier for small revenue).
