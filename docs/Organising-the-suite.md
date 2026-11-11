# Organising the suite in FL Studio

One layout that lets every automatic feature work: each part on its own mixer track with a
name the suite recognises, the music going through one bus, and the master chain last.
Set it up once, save it as an FL template (end of this page), and every new song starts
here.

## Name the mixer tracks

Prima Materia, Quintessence and Kybalion set themselves up from the **mixer track name**.
A name only needs to contain one of these words:

| Role | Words that work |
|---|---|
| Kick | kick, bd, bassdrum |
| Bass | bass, sub, reese |
| Hats and percussion | hat, hh, perc, clap, snare, ride, shaker, top, cymbal, tamb |
| Pads and chords | pad, chord, string, keys, piano, pluck, stab |
| Lead | lead, saw, melody, arp, hook, synth |
| Vocal | vox, vocal, voice, acap, chop |
| FX | fx, atmos, riser, sweep, noise, impact, crash, downlift, texture |

"Vox" wins over "bass" (so "Vox bass chop" is a vocal), and "kick" wins over everything.

## The layout

```
Mixer track          Plugins, in order                                    Routed to
-------------------  ---------------------------------------------------  -----------
 1 Kick              Prima Materia, Quintessence                          Master
 2 Bass              Prima Materia, Quintessence, Kybalion (Suite kick)   Master
 3 Hats              Prima Materia, Quintessence                          Music bus
 4 Pads (Spire)      Prima Materia, Quintessence, Kybalion (gate)         Music bus
 5 Lead (Spire)      Prima Materia, Quintessence, Vibration, Antioch      Music bus
 6 Arp / Pluck       Prima Materia, Quintessence, Antioch                 Music bus
 7 Vocal             Prima Materia, Quintessence, Antioch                 Master + sidechain to 10
 8 FX                Azoth, Prima Materia                                 Music bus
 9 Chords (MIDI)     Correspondence                                       (silent)
10 Music bus         Solve et Coagula (vocal on its sidechain), Cause & Effect   Master
   Master            Ouroboros, Emerald Tablet, Mentalism, Lapis (last)
```

Why this order:

- **Prima Materia first** on each track: it mixes the track per section, measures it for
  the rough mix and the balance loop, and tells the suite its role and tone.
- **Quintessence second:** it EQs and compresses what Prima Materia lets through, and
  takes Mentalism's masking, mud and harshness fixes for that track.
- **Kybalion** on the bass and pads: with *Suite kick* on it ducks to the real kick with no
  routing. Its gate belongs on pads, plucks or a pad bus.
- **Vibration** on the lead only, before Antioch, so the space follows the polished sound.
- **Antioch** as an insert on the lead, arps and vocal: its throws follow the sections and
  the vocal's phrase ends.
- **Solve et Coagula** on the music bus, with the vocal track sent to its sidechain (in
  FL: on the vocal track, right-click the music bus's send knob, *Sidechain to this track*).
  It makes room for the vocal in everything at once, only while the vocal sings.
- **Cause & Effect** on the music bus, so its builds (filter, reverb, noise, riser, gate,
  width) lift all the music at once, without filtering the kick and bass.
- **Master:** Ouroboros (it listens and conducts; one per project), Emerald Tablet (only
  while you want the key detected), Mentalism (measures and fixes), Lapis **last**.
- **Correspondence** on an empty track: it holds the key and the chords and writes the MIDI.
  Use one per project.

One of each per project: Ouroboros, Correspondence, Mentalism, Lapis, Emerald Tablet.
Use as many as you like of the others (one per track).

## Make it your template

FL Studio can't load a template file made outside it, so save this layout from FL once:

1. Set up the mixer tracks, names, plugins and routing above in an empty project. Spire
   instances with your init patches can go in the Channel Rack too.
2. *File > Save as...* into `Documents\Image-Line\FL Studio\Projects\Templates\Antioch\`
   (make the `Antioch` folder), for example as `Antioch trance.flp`.
3. From then on, *File > New from template > Antioch > Antioch trance* starts a song with
   the suite in place. Ouroboros' *New song* then sets the key, tempo and style.

Saving with every plugin untouched matters: plugins on a track whose name gives a role set
themselves up for that role the first time they see it, but only while nobody has touched
them. A template saved straight after loading keeps that.

## Then, in a new song

1. Ouroboros: *New song* (style, key) or *Draft the whole song*.
2. Write and play. Quintessence settles each track within the first minute.
3. Ouroboros: *Learning pass*. Play the song once; the suite sets itself up.
4. Mentalism: *Mix it*. Rough mix, arc, masking, mud and harshness, and the balance loop.
5. Ouroboros: *What changed?* lists every move, and why.
