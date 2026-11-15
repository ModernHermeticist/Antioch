# Handoff prompt (paste into a new session)

You're picking up work on the **Antioch Suite**: JUCE 8 VST3 trance plugins for FL Studio, in this
repository. It's a personal project for the owner only (no release, installer or crash-reporting
work). Read this whole prompt before doing anything.

## Ground rules
- Branch: `ccr-ed573315-1xtg4u`. Commit there and push with `git push -u origin ccr-ed573315-1xtg4u`
  (retry on network errors). Never open a pull request. No AI model names in commits or code.
- The owner likes clarifying questions before detailed answers, and alternatives you think are better.
- Standing decisions (don't relitigate): Prima Materia only suggests (no level/mute/filter moves);
  the premaster is label-ready unmastered at -6 dBFS peak with no master limiting; Lapis is a
  reference only; the autopilot has group switches only (12 groups); topics the owner dropped stay dropped.
- Never edit sources while a build is running (wait for it to finish first).

## Layout
- 9 plugins in `plugins/`: Antioch, Correspondence, Kybalion, Mentalism, Ouroboros, Quintessence,
  SolveEtCoagula, Transmutation, Vibration. Four older plugins are now **modules hosted inside
  them**: Emerald Tablet in Correspondence, Lapis in Mentalism, Prima Materia in Quintessence,
  Azoth in Transmutation (`suite_add_module` in the root `CMakeLists.txt`; `SuiteProcessor`
  `addModule` / `modulesAfter`; the modules' own sources stay in `plugins/<Module>/`).
- Shared code in `shared/`: `SuiteProcessor.h` (base class: modules, timers, `blockGuard`),
  `SuiteUI.*` (header, search palette, module pages), `Worker.h` (one background thread),
  `Watchdog.h` ("Freeze log.txt": slow message-thread jobs, interface blocks, heavy audio blocks),
  `RoleCatalog.h` (37 track roles in 9 families), `AutopilotBus.h`, and the various buses.
- Correspondence writing: `ChordEngine.h` (`songClip`), `PatternLibrary.h` / `PatternRender.h` and
  the hand-written families in `plugins/Correspondence/Patterns/*.txt` (plus `kits.txt`,
  `characters.txt`), `Writing.h`, and **`WritingCritic.h`** (the writing critic, see below).

## Build and test
```
cmake --build build -j 4          # (configure first if build/ is missing)
```
Test runners: `build/plugins/<P>/<P>Tests_artefacts/Release/<P>Tests` (modules:
`build/plugins/Correspondence/EmeraldTabletTests...`, `Mentalism/LapisTests`,
`Quintessence/PrimaMateriaTests`, `Transmutation/AzothTests`). Run them under `xvfb-run -a`, with a
folder for screenshots as the argument. A full run of all 13 takes about 35 minutes, Correspondence
alone about 7 minutes. Each prints PASS/FAIL lines and ends with "All tests passed" or "N test(s) FAILED".

A full-run script that has worked well (write the logs somewhere outside the repo):
```bash
#!/bin/bash
cd /path/to/repo; N=$1; S=/path/to/logs
cmake --build build -j 4 > $S/fullbuild$N.log 2>&1; echo "BUILD EXIT $?" >> $S/fullbuild$N.log
: > $S/fulltests$N.log
for p in Antioch Azoth Correspondence EmeraldTablet Kybalion Lapis Mentalism Ouroboros PrimaMateria Quintessence SolveEtCoagula Transmutation Vibration; do
  b=build/plugins/$p/${p}Tests_artefacts/Release/${p}Tests
  case $p in Lapis) b=build/plugins/Mentalism/LapisTests_artefacts/Release/LapisTests;; EmeraldTablet) b=build/plugins/Correspondence/EmeraldTabletTests_artefacts/Release/EmeraldTabletTests;;
    PrimaMateria) b=build/plugins/Quintessence/PrimaMateriaTests_artefacts/Release/PrimaMateriaTests;; Azoth) b=build/plugins/Transmutation/AzothTests_artefacts/Release/AzothTests;; esac
  timeout 1500 xvfb-run -a $b $S/shots > $S/t_$p.log 2>&1; echo "$p EXIT $? $(grep -c FAIL $S/t_$p.log)" >> $S/fulltests$N.log
done; echo FINISHED >> $S/fulltests$N.log
```
Fast modes in the test runners:
- `CPU_ONLY=1`: any runner prints just its CPU line (audio thread playing, stopped and silent, plus timer cost).
- `CRITIC_ONLY=1` (Correspondence): the writing-critic survey over all 49 mood × style songs, in
  about 18 s. `CRITIC_TRIES=6` uses best-of-six. `CRITIC_RUBS=1`, `CRITIC_DUMP=1` and `CRITIC_BD=1`
  print diagnostics (which parts rub, a hook against its chords, a breakdown's parts).

## Where things stand
Recent commits, newest first. The top three were pushed **before their full test run finished**:
- `480ac59` Effects bus role for Quintessence; taste from A/B picks; idle gate wakes on transport changes.
- `f26a9d0` The writing critic plus the fixes it found; kits for the new roles; best-of-six song draft; the A/B compare export.
- `1126788` CPU diet (idle on silence, CPU lines, heavy-block logging) and bells/organ/guitar layers.

**First job: run the full suite and fix what fails.** Known from the last complete run (76):
1. Correspondence "two versions to compare by ear…" failed. The test now prints the clip counts
   and the folder (Tests.cpp, the writing-critic section). Look at `finishDraftPick` (the compare
   branch) and `exportEverything` in `PluginProcessor.cpp`.
2. Vibration "stopping finishes it…" failed because of the idle gate. Fixed since (a full block
   whenever the transport starts or stops); it passed when run alone.

Everything else passed in run 76, including the critic survey checks.

### What was built this session (for orientation)
- **Freeze and CPU:**
  - Correspondence's label-ready song rendering moved to the worker thread (it had frozen FL for 11 s).
  - Mentalism's masking scan runs once a second.
  - `blockGuard`: every plugin times its audio blocks; a block using over 25% of its time is
    logged to the freeze log. Quintessence, Vibration and Antioch sleep through silence after
    0.5 s, still running one block in eight.
  - Vibration no longer reads a parameter by name every sample.
- **Roles:**
  - 37 roles; the newest is "Effects bus (returns)".
  - Bells/mallets, Organ/drones and Guitar/ethnic/woodwind layers in Correspondence (families in
    `bells.txt`, `organ.txt`, `guitar.txt`; in every kit). They play only when a track in the
    project has that role (`roleLayersFrom` reads the registry).
- **The writing critic** (`WritingCritic.h`): rules in four groups (hook, parts together, journey,
  harmony), with `judge`, `bestOf` and a `taste` namespace.
  - The survey went from 0.86 to 0.967 average (worst song 0.89; 0.979 with best-of-six).
  - Bugs it found and that were fixed:
    - "o" root-and-fifth voicing used an inverted voicing's lowest note;
    - the hook's climb, the slowed breakdown hook and the phrase-end cadences ignored the chord under them;
    - the hook didn't follow the chords' fast turnaround or the passing dominant;
    - layers voiced the same chord with different colours;
    - added 7ths and 9ths formed semitone clusters;
    - chord layers crowded the low register.
- **Song draft:** writes six candidates on the worker and keeps the critic's best (`Critic.txt` beside it).
- **Search actions:** "Write two versions to compare" (A/B folders), "I prefer A" / "I prefer B"
  (recorded to `Critic taste.txt`; from three picks on, the rule weights follow them), and "Your taste".

## What's next (the owner's choices)
1. Verify and fix the full run (above). Commit and push.
2. The owner asked for a 100-item list of writing + mixing improvements: **`docs/next-100.md`**.
   They were given a condensed "top 20 + rest" version and **haven't chosen yet**. Ask which to build
   (all, the top 20, or a selection), and ask clarifying questions before starting.
   The top 20 were #43, 1, 6, 5, 15, 16, 20, 32, 29, 31, 44, 45, 55, 64+65, 69, 71, 73, 75, 74, 90.
3. Still pending from earlier:
   - "Tidy after merges": thin pass-through stubs for the four merged plugins, so old projects load.
   - The songwriting rounds the owner picked: hook identity, groove and rhythm, harmony depth.
     They largely overlap the 100-item list; use the critic to prove each change.

## How to work on the writing
- Change rules or patterns, then run `CRITIC_ONLY=1` to see the averages and the weakest rules.
- Use the diagnostic env vars to find the real cause before changing anything: several "low scores"
  this session were measurement errors (corrected in the critic) and several were real engine bugs.
- Keep the critic's checks in the Correspondence tests passing: average ≥ 0.94, worst ≥ 0.85, and
  the per-group and per-rule floors in the "writing critic" section.
