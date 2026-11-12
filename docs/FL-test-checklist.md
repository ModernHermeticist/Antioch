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
- [ ] **6b.9** A wide supersaw lead: Quintessence narrows its *Width* a step at a time; a mono bass is
  never widened. *Spire tips* lists what to change in Spire.
- [ ] **6b.10** Antioch on the lead with lots of echo and reverb: *Space trim* comes down until the tail
  sits about 12 dB under the dry lead; each step shows in *What changed?*.
- [ ] **6b.11** Kybalion on the bass with the suite kick shared: *What changed?* says where the kick's
  body sits against the key; with *Duck focus* on Auto it reports which kind of ducking it chose.
- [ ] **6b.12** Correspondence: *Vary... > Breakdown > Breakdown writer*, then Export all. The breakdown has
  held chords and a slow hint of the hook; drops 2, 3 and 4 each play the hook differently.
- [ ] **6b.13** Quintessence on the kick: *Punch* rises on a soft kick. On the music bus (role *Music bus*)
  the glue settles around 1.5 dB of reduction. On the vocal the de-esser switches on at the 's'
  sounds only.
- [ ] **6b.14** Vibration on a clean lead: *Drive trim* rises until it cuts through. Lapis with *Auto
  tone*: the *Tilt* moves towards your masters' balance.
- [ ] **6b.15** Push a track into clipping: within a few seconds Mentalism's watcher gain-stages it, and
  *What changed?* says so. Turn a Prima Materia's *Mono lows* off on a wide bass: *Problems (1)* offers
  to put it back.
- [ ] **6b.16** With several Quintessences playing, *What changed?* shows their moves one after another,
  never two at once. Put Antioch on the music bus: it becomes a short room. Through a build the pads
  widen and the bus glue tightens; the drop resets both.
- [ ] **6b.17** Solve et Coagula with the vocal on its sidechain: play the song, *Save vocal MIDI*, and drop
  *Vocal melody.mid* and *Vocal harmony.mid* on a Spire: they follow the vocal, in key. Chops on *Words*
  play the sung words in order.
- [ ] **6b.18** Correspondence *Export all*: *SONG VOCAL ANSWER* plays in the vocal's gaps; *Spire macros.mid*
  has four CC lanes. *Vary... > Clash check* lists parts sharing a register. Load a reference in Mentalism:
  Quintessence says "+ your reference".
- [ ] **6b.19** Ouroboros: *Hear without the automation*, then switch back: the difference is everything the suite did.
  Try *Mood: darker* and *wider*. Draw a drop's energy down: the gate softens and Euphoria drops there.
- [ ] **6b.20** While the vocal sings, an arp track steps back more than the pads; while the lead plays, other
  tracks' throws wait. *Stereo field* lists each track's width.
- [ ] **6b.21** Ouroboros *Finish everything*: play the song once; Mix it runs, the parts are exported, and the render
  plan is shown. Export again after changing the lead: only the lead's Quintessence re-learns.
- [ ] **6b.22** Turn up the bass's 250 Hz by 4 dB with Quintessence on bass and pads: within a minute Mentalism
  shares a cut out, most on the bass (a *Master balance* band), and *What changed* says kept or undone.
- [ ] **6b.23** Put a stereo widener at 200% on the FX track: within a minute its Quintessence narrows it (*What
  changed*: "wider than it is solid"). Flip one channel of the bass: the polarity flip comes by itself, judged 20 s later.
- [ ] **6b.24** Squash the drops (Quintessence multiband hard on the bus): the dynamics ease off by themselves and
  are kept only if the drops' peaks stand further out. Mentalism's mono reading shows a loss as "-x dB vs stereo".
- [ ] **6b.25** Turn Cause & Effect's build down to nothing: after the next drop, it builds deeper by itself; after
  two drops that still don't land, Prima Materia adds half a beat of silence before the drop.

## 6c. Pattern library (new)

- [ ] **6c.1** Ouroboros *New song* with Tech trance: Correspondence's Song patterns shows Tech trance and Driving, and
  the families line names a tech bass, arp, gate and hook. *Draft the whole song*: the intro's bass is sparse, the build's
  gets busier bar by bar, the drop's is the busiest, and bar 8 of each phrase turns around.
- [ ] **6c.2** Switch the mood to Melancholic and the style to Vocal, then *New patterns* a few times: each set sounds like
  one song, and the lead hook sits around the vocal's range without big leaps.
- [ ] **6c.4** Uplifting song, drag SONG PIANO, SONG STRINGS, SONG BRASS, SONG CHOIR and SONG TIMPANI onto instruments: the breakdown
  has piano, strings and choir; the build has timpani rolling and strings climbing; the last drop gets brass and choir.
- [ ] **6c.5** Switch the style to Tech trance: SONG ACID has a line in the grooves and drops (a 303 with slide on), and the
  last drop's choir chants instead of holding. Switch to Vocal: the acid tile is empty.
- [ ] **6c.6** Song patterns shows a kit name (e.g. *Shuffled tech*). Drag SONG BASS, ARP, CHORDS and LEAD: they share one feel
  (all shuffled, or all straight), the arp sits clear of the hook, and New patterns gives another kit that also hangs together.
- [ ] **6c.3** Drop a MIDI motif on Correspondence: the lead is your motif again while the bass, arp and chords stay
  from the library.

## 6d. Autopilot (new)

- [ ] **6d.1** Ouroboros menu, *Autopilot*: three switches, all on, and a list of counts. Save the project with Arrangement
  off, reload: it's still off.
- [ ] **6d.2** Push a lead's drive hard with a bright patch and play the drop: Mentalism's log names the track in a "Harsh
  moment" line, the lead's Quintessence dips there, and *harsh moments dipped* counts up. Turn Harshness off: no dips.
- [ ] **6d.3** Turn Antioch's echo feedback right up and stop the input: within a second it backs off ("runaway" in What
  changed). Same with Cause & Effect's filter resonance at maximum.
- [ ] **6d.4** Orchestral song with every layer on: *Export all* — the final drop doesn't have every layer at once, the
  breakdown keeps its piano, and *parts thinned* counts up. In a style with chops, they never land on the hook's notes.

- [ ] **6d.5** Lead (with Vibration and Quintessence) and a supersaw pad (with Quintessence) playing together: the pad's
  Quintessence shows small dips that move with the lead's notes; *masking dips* counts up. Mute the lead: the dips go.
- [ ] **6d.6** A pad with lots of sub, and a bass playing: the pad's lows under 70 Hz go while the bass plays.
- [ ] **6d.7** A sine sub bass alone on a laptop speaker: after a few seconds you can hear its notes (harmonics added).
- [ ] **6d.8** A track named "Reverb" with Quintessence: its low cut goes to 180 Hz and high cut to 12 kHz on the first pass.

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
