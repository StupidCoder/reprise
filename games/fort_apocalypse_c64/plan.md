# Fort Apocalypse (C64) — plan

What is left before the re-implementation is complete, and the known bugs.
How the original works, and where the evidence is: [game.md](game.md).

## Where it stands (2026-10-29)

The whole game is playable from the title through all three levels to the
debrief, including the attract game, options and pause. Scenario runs that
poke the same situation into the original and the native module, and replay
the original's random numbers and main-loop schedule, match frame for frame
apart from known sub-frame races (flight, refuelling, rescue, both level
exits, the fort, the mission's end, the enemy's climb bug, the front end).

Finished, in order:

1. Tape import: TAP → KERNAL boot file + Novaload pages → four PRG files,
   identified by content.
2. Asset decoding, checked byte for byte against the game's own setup code.
3. Reference harness (`fortref`, `fort_apocalypse_replay`, `check.sh`,
   `scenario.sh`).
4. Title, options, pause and the HUD counters.
5. Gameplay: craft, camera, shots, enemy helicopter (with the "Enemy pilot"
   Original/Fixed option), tanks, homing missiles, mines, prisoners, depots,
   gates/walls/lasers/barriers, fort, three levels, bonus, debrief and rank.
6. Enhancements: particle effects, smooth motion, widescreen (up to 21:9),
   VHS look, Carnage mode (all three slices).
7. Persistent quick saves (Reprise's shared save format).

## Remaining tasks

### Sound

- [ ] Compare the SID output with the original's. The effects (engine,
  shots, explosions, rescue, missiles, refuelling, fuel alarm, title tones)
  drive a register-level SID but have never been checked against a recording.

### Comparisons not run yet

- [ ] The cavern teleport ($9892, mode $0A).
- [ ] Hits on the craft by tanks and missiles.
- [ ] Losing the last life mid-level (debrief "MISSION ABORTED").
- [ ] Mode 1 after a game over: the main loop keeps running until the $42
  timeout returns to the title.
- [ ] Options reached from modes 3/5 ($930F) and pause during play.

### Research

- [ ] Which code sets the in-play colour RAM to $0D ($AFA0 starts it at
  $0C). The previews use the observed values.
- [ ] Enemy explosions on brown rock: this tape (and the VIC-II as
  documented) lets the enemy fly through the level colour's pixels; the user
  remembers it exploding there on a real C64. Check in VICE (game.md,
  "Findings while adding the enhancements") and, if possible, other releases.
- [ ] Try other dumps of this release (recognised by content) and say how
  other releases are rejected.

### Optional

- [ ] The tape's U.S. Gold loading screen during import: its display script
  and music stream are data; the music player is native code to rewrite.
- [ ] Panning and zoom in the details viewer's terrain page (today it shows
  the whole level reduced to fit).

## Known bugs and inexact spots

- The level build's duration is modelled from measured work (±10 raster
  lines). A build ending that close to the HUD interrupt can start the life
  a frame early (the attract run does).
- Sub-frame races are not modelled: main-loop work against the beam (a mine
  half moved, "LOW ON FUEL" flicker), and the playfield colour write on
  raster $7F (fixed at pixel 92; the original lands at 84-124).
- The VIC row sequencing in `shared/c64/text_frame.cpp` is checked only for
  the scroll changes these screens use.
- Smooth motion shows objects where the game has them now, while the craft's
  VIC collisions use the original picture, whose screen copy lags up to 7
  frames: a touch can look slightly off.
- Crash hold (particle effects): the copy of the game shown while the wreck
  settles has not been tried with the enemy or shells hitting things; their
  effects show but their outcome is lost.
- Widescreen: shots are not seen past the original window (the game ends
  them at its edges). Not yet played in the app at full 21:9.
- The enemy helicopter's banking and the prisoners' running stay as in the
  original under smooth motion.
- VHS look: checked on rendered previews, not in the app window.
