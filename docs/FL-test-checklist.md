# Antioch Suite: FL Studio test pass

The tests run every plugin offline, without FL. This is the list of things only FL can show.
Work through it in order (each part builds on the one before). For anything that doesn't do
what's written, note the number, what happened instead, and your FL version (Help > About),
and send me the list: I'll fix them in one go.

Allow about an hour. Use a fresh project at 138 BPM.

## 0. Install

- [ ] **0.1** Copy all thirteen `.vst3` files into `C:\Program Files\Common Files\VST3`.
- [ ] **0.2** FL: *Options > Manage plugins > Find plugins*. All thirteen appear (Antioch, Azoth,
  Cause & Effect, Correspondence, Emerald Tablet, Kybalion, Lapis, Mentalism, Ouroboros,
  Prima Materia, Quintessence, Solve et Coagula, Vibration) under the *ModernHermeticist* vendor.
- [ ] **0.3** Each one opens its window without errors.

## 1. The plugins find each other

This is the most important part: FL may run plugins in separate processes (*Wrapper
settings > Processing > Make bridged*, or FL's default sandboxing), and the suite talks
through shared memory.

- [ ] **1.1** Put Ouroboros on any mixer track and Prima Materia on another. Prima Materia's
  status line shows the section that's playing as you play the song.
- [ ] **1.2** Put Correspondence on a track and choose *A minor*. Antioch on another track shows
  *following Correspondence (A minor)*.
- [ ] **1.3** Ouroboros: open *Suite*. Every suite plugin you've loaded is listed, on the right
  track.
- [ ] **1.4** Repeat 1.1-1.3 with one plugin set to *Make bridged* in its wrapper settings. Note
  if anything stops working.

## 2. Track names (auto-roles, signature chains)

- [ ] **2.1** Name a mixer track *Kick*, put Prima Materia on it: the role becomes *Kick* by
  itself.
- [ ] **2.2** Ouroboros's Suite panel shows real track names, not *(track name unknown)*.
- [ ] **2.3** Rename the track: the Suite panel follows within a couple of seconds.

If 2.1-2.3 fail, FL isn't passing track names to VST3 plugins in your version: roles still
work by hand.

## 3. Sidechain

- [ ] **3.1** Route the kick track to Kybalion's track as a sidechain (mixer: right-click the
  arrow, *Sidechain to this track*), and in Kybalion's wrapper *Processing* tab select the
  sidechain input. Set Kybalion's trigger to *Sidechain*: it ducks on the kick.
- [ ] **3.2** Same for Solve et Coagula with a vocal: the display shows the vocal bands.
- [ ] **3.3** *Share kick* on a Kybalion on the kick track, *Suite kick* on another: the second
  ducks to the kick with no routing.

## 4. Save and reload

- [ ] **4.1** Set things up (a map in Ouroboros, a progression in Correspondence, a role in
  Prima Materia, snapshot A saved). Save the project, close FL, reopen: everything is as you
  left it, and snapshot A is still there.
- [ ] **4.2** Ouroboros's Suite panel doesn't report *saved by an older version* for this
  project.

## 5. FL's piano roll scripts (new)

- [ ] **5.1** Correspondence: *Export… > Send to FL's piano roll*. It says where it wrote the
  scripts. That folder should be `Documents\Image-Line\FL Studio\Settings\Piano roll scripts`.
  If your FL keeps scripts elsewhere, tell me where.
- [ ] **5.2** Open a pattern's piano roll: *Tools > Scripting* lists *Antioch - Chords* (and the
  rest). If it doesn't, restart FL and look again.
- [ ] **5.3** Run *Antioch - Chords*: a dialog with *Where* and *Existing notes*; OK pastes the
  chords into the pattern, in key, at the right length.
- [ ] **5.4** *After the last note* appends them; *Replace them* clears the pattern first.

If 5.2-5.4 fail, send me FL's script error message: FL's scripting functions differ a little
between versions.

## 6. Spire (new)

- [ ] **6.1** Correspondence: *Export all*. Drag *Song Lead* onto a Spire channel's piano roll
  (or the playlist). The clip carries CC 20-23 events (the event editor shows them).
- [ ] **6.2** In Spire, right-click macro 1 > *MIDI learn* and move CC 20 (or use FL's *Link to
  controller*); the same for macros 2-4 with CC 21-23. Save the patch. Play the song clip:
  macro 1 opens through the build and is full in the drop.
- [ ] **6.3** Load *Song Lead Octave*, *Song Lead Pluck* and *Song Sub* on three more Spire
  channels: the octave plays only in the drops, the pluck from the build, the sub everywhere
  but the breakdowns.

## 6b. Quintessence (new)

- [ ] **6b.1** Put Quintessence on a pad track named "Pads" and play a loop for 30 s. *What it
  did* fills in: a low cut, then band moves marked *Role tone*, each followed by *Kept* or *Undid*.
- [ ] **6b.2** Turn a band's Gain knob by hand. It shows *Locked* and *Your setting*, and the
  automation doesn't move it again. Unlock it and it's free for the automation again.
- [ ] **6b.3** On a vocal track, the Mid compressor band comes in within a few passes and the
  vocal's level holds steadier, with no jump in loudness.
- [ ] **6b.4** With Prima Materia on the same track, Mentalism's *Mix it* puts its masking and mud
  dips in Quintessence (purple/brass bands), not in Prima Materia. Undo Mix it removes them.
- [ ] **6b.5** Save the project, reopen it: same bands, same locks, the log is still there.
- [ ] **6b.6** With Ouroboros' map playing, the breakdown gets its own moves (*Breakdowns +x dB* under
  a band) and the drop keeps its own.
- [ ] **6b.7** Ouroboros, *Start from a template... > What changed?*: every move above is listed with
  its track and bar; typing a bar number shows what changed just before it.
- [ ] **6b.8** Mentalism: turn on *Balance* (or *Mix it*), play the drop. Over a minute the pad/lead
  trims in Prima Materia move 1 dB at a time, each one kept or undone in *What changed?*.

## 7. A song end to end

- [ ] **7.1** Ouroboros: *New song… > Draft the whole song*. Within a few seconds the suite is
  set up, and `%APPDATA%\Antioch Suite\Drafts` has a new folder with the whole song's MIDI.
- [ ] **7.2** Play the song: Azoth's risers, impact and crash land on the section changes,
  Prima Materia moves the levels, and Kybalion gates.
- [ ] **7.3** Mentalism on the master, last in the chain after Lapis. *Finish the song*, play
  it through: the coach and the checklist fill in.
- [ ] **7.4** Load a reference and press *A/B*: you hear the reference at your mix's loudness;
  press again to come back.
- [ ] **7.5** Export the song from FL (*File > Export > WAV*): Lapis writes a note in
  `%APPDATA%\Antioch Suite\Export notes` afterwards.

## 8. Load and CPU

- [ ] **8.1** With everything loaded and playing, FL's CPU meter stays reasonable. Note the
  value, and which plugin is heaviest (*Options > General > Show plugin CPU usage*).
- [ ] **8.2** Play for five minutes: no crackles, no growing CPU.
- [ ] **8.3** Azoth's *Reverse swells* on: FL compensates the 3 s latency and the track stays in
  time.
