# Curious Village (Nintendo DS): plan

What remains for a complete re-implementation of the first adventure
(`A5FP`), and the known bugs and differences. How the game works:
[game.md](game.md). Detailed research notes: `.analysis/adventure1/docs/`.

## Where it stands (2026-10-29)

Complete and playable from the title to the end credits in all five
languages:

- the story with its rooms, events and movies;
- all puzzles (every engine the scripts use);
- the trunk with the robot dog, the hotel and the painting;
- the mysteries, the puzzle index and the puzzle shack;
- the three save slots in the cartridge layout;
- the bonus section;
- music, effects, voices and movies.

The Wi-Fi download ends in the original's connection error, because the
service is gone. The conveniences (music player, theatre, puzzle mode,
minigame free play, skipping, checkpoints, touch-area help) are in place.
An auto-player run reaches the staff roll in one go.

## Remaining tasks

1. **Save write error screen** (`0200b590`): not reconstructed.
2. **Compare with the ROM**: the save screen, the trunk's own pages and the
   staff roll were ported from the code only. Capture them from a ROM run and
   compare.
3. **Staff roll**: check whether the two lower pictures sit directly on top
   of each other in the scroll (**hypothesis**). The original loads the upper
   pictures asynchronously; the port loads them at once.
4. **Draw Input2 letter mode** (puzzle 33) and the "?" reading: compare
   with the ROM. Establish which scene 0x28 is (the canvas keeps letters
   unfolded there; the port always folds).
5. **Hotel happiness**: the trunk's "fix" check uses event 257 only. The
   research notes list the rooms' happiness routines (`0206593C` /
   `020659EC`) as not reconstructed; confirm what the hotel scene does
   natively and compare it with the ROM.
6. **Wi-Fi**: the received-puzzles list (0x1e) shows only what an imported
   backup holds and was never compared (no such backup). The passcode notes
   page (0x14) has no way in.
7. **Intro talk** after ROM frame ~4200: compare frame by frame.
8. **Text `@` codes**: decode `@1 @2 @3 @W @C @w @P @#`.
9. **Command meanings**: most GDS commands and conditions are still named
   `CommandXX`. Tag 7 is unexplained.

## Known bugs and differences

- **Title**: the car's animation runs 6 frames off the ROM's phase. Fades show
  each step one frame earlier than the ROM. The shared compositor's
  brightness keeps 5-bit colour (ROM: 6-bit).
- **Loading pauses** are measured constants per call site. Some story and bonus
  pages load faster natively (the bonus menu and music room by ~20 frames, a
  music title by ~7). The load screen reacts 3 frames earlier and its black
  pause is 9 frames shorter.
- **Typing waits** depend on ARM7 sound timing and can fall one frame apart.
  The title card's exit is one frame off. A retried title card counts down one
  frame early.
- **Robot dog in rooms**: the pose and the chosen spot use the port's own
  random numbers. The walk-in animation phase is 2–3 frames off.
- **Sound**: a note that starts near a sample boundary can land one sample
  apart. The order in which a wake-up pass stops freed channels differs.
  Channel allocation ties can differ.
- **Stream decoder**: the first filter value of the opening movie is zero in
  the ROM (fresh memory). The port always carries the last value over.
- **Movies**: the MODS files' own audio tracks are not played. The game
  doesn't use them either; the streams carry the sound.
- **Edge pixels**: 1–2 edge pixels of the turned jug differ in the Cup
  puzzle. A thin line over a point marker differs by 6 pixels in Cut puzzle 86.
- **Recogniser slowdown** (the cartridge skips 1–2 frames after each stroke)
  is deliberately not reproduced.
- **"No more moves" panel**: stays one frame longer on the cartridge.
  Tutorial pages load ±1 frame.
- **Painting**: whether the shadow objects draw centred is not compared.
- **Hotel**: the puff's 3D alpha write is not modelled.
- **Puzzle index**: the scroll bar's sound and drag follow the code
  (not compared). The shack's puffs are drawn unscaled.
- **Not comparable**: coin pieces with kinds (no script uses them) and how the
  game starts the match tutorial (q0).
- **Intentional**: free play hands out the hotel's furniture alternately to
  the two rooms (the story asks). The passcode uses a per-installation stand-in
  for the console's MAC address (shared with *Pandora's Box*). The game
  follows a language choice in the hub instead of the console's system
  language.

## Checks to run after changes

- `.analysis/adventure1/tools/regress.py`: every recorded puzzle comparison.
- `picarat_a1_story_play`: the auto-player to the staff roll.
- `ctest` (`a1_script_tests` runs all 930 scripts, `a1_sound_tests`).
- After changing the shared interpreter, also compare `tools/golden.py`
  against a build without the change.

## In Reprise (2026-10-29)

The game runs inside Reprise (catalog id `curious_village_ds`); the integration is
described in [../reprise.md](../reprise.md).

Connected: import (A5FP NDS, streamed), Start (the title), Continue and
quick save/load (the last room checkpoint, not the exact moment), the three
cartridge slots in `<data>/progress/curious_village_ds/backup.sav`, a Language
option (system default or one of the five; applies at the next start), the
VHS filter per screen, touch-area help, background previews, takeover of
Picarat's pack and `a5fp-backup.sav`.

Not offered in Reprise (Picarat shell features, by decision): music player,
movie theatre, puzzle mode, minigame free play, the hub's "Continue story".
Movies, music, puzzles and minigames play as part of normal play.

Remaining for Reprise:

- typed name entry (the touch keyboard works);
- an accelerometer for the 3DS games on phones (Picarat had one);
- the screen filter's CPU fallback path is untested on a renderer without
  GPU shaders.

Validation:

- Validated on macOS (2026-10-29): `ctest -R layton_module` with
  `LAYTON_PACKS` (the story starts from the title with picture and sound);
  the real window driven by `reprise --events` scripts. Android: the APK
  builds; only Curious Village was run on an emulator (start, touch,
  rotation). No physical device was used.
- Quick save, fresh-process Continue, damaged and foreign saves: checked
  (`LAYTON_CHECKPOINTS` with Picarat's `a5fp-checkpoint.sav`; in the window:
  title → Continue → slot → room → Cmd+S → relaunch → Continue).
- Emulator (Medium Phone API 36.1): title, touch on New Game in landscape, a
  typed letter kept across rotations, stacked/side-by-side switching.
