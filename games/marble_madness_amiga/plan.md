# Marble Madness (Amiga) — plan

What is left before the re-implementation is complete, and the known bugs.
How the original works, and where the evidence is: [game.md](game.md).

## Where it stands (2026-10-29)

All six courses play from start to goal and equal the original update for
update in recordings (marbles, creatures, regions and covers; pictures
pixel-identical at the captured moments). The front end runs for one
player: options screen, each course's introduction, finish bonus, out of
time, game over and the end. Music and effects match the original's
audio.device requests. `harness/regress.sh` re-checks all 19 runs.

Finished, in order:

1. Disk import: the cipher of `c/zzz` rebuilt, `program.hunks` and the data
   files, checked against the running original's memory.
2. Asset decoding: tile maps, image libraries, title picture.
3. Reference harness (`mmref.py`, `marble_madness_replay`).
4. Play: height map, probe, regions and their scripts, the marble, scroll,
   shine, view, wall covers, Practice.
5. Sound: effect synthesiser, 8-voice music player, Paula model.
6. Beginner, Intermediate, Aerial, Silly, Ultimate.
7. Front end for one player; the Kickstart's topaz 9 font.
8. Quick save and load (persistent); cheats; VHS look.
9. Crowd race (not the original game).
10. Random numbers from a standard generator (user rule).

## Remaining tasks

### Two players

- [ ] Two players in the race.
- [ ] The front end with two players: the blue player's options, "WON LAST
  RACE" bonuses (`$106A64`), the other player's "READY SET GO!" after one
  runs out (`$10BE7C`), the end's second tally.
- [ ] Two players with the crowd race.

### Input

- [ ] Mouse control (the original's default) needs mouse deltas from the
  host.
- [ ] Trackball input.

### Rules not rebuilt

- [ ] Region contact codes other than 5, 10-13, 16-24, 26-33 (`$116EE0`),
  mode 11, region pictures with linked cells (`$112204`).
- [ ] Display objects other than region code 5 (types 1-15 of `$111C82`);
  display types other than 1, 2, 4, 5 and $2A are not drawn; display list
  boxes of region codes other than 3, 5, 6 and 10-13 and of later courses'
  creatures (`$110CD4`).
- [ ] Covers of creatures and other marbles (`$11EC42` with 2 and 3); with
  no animation frame the original cuts the covers out of the last sprite it
  built (the points picture) — not copied.
- [ ] Other zone effects (`$112F20` on Aerial/Silly).
- [ ] The demo driver (mode 1).
- [ ] Ultimate's moving goal area: pausing and leaving the course during a
  rendering (`$11E708`, `$11E6FC`).
- [ ] The 50 Hz in-between display frames (`$1076B6` / h36: half scroll
  steps); the module shows each world update for two frames.
- [ ] The splash and loading pictures before the options screen (the
  launcher starts the game).

### Recordings still to make

- [ ] The acid dissolving a marble on Ultimate, Aerial's bumpers (code 12),
  catching a critter, slinkies eating a marble (state 5).
- [ ] Check by rule against the recordings: the acid's path pick (one of two
  lists) and the launcher's side. Then delete the unused
  `harness/runs/*-random.txt` logs.
- [ ] Compare Paula's rendering with the emulator's output (only the
  requests are compared).

### Research

- [ ] `c/sigfile`'s track and the measurement in `c/xxx` (patched out in
  this image): what the original protection checked.
- [ ] Silly's extra map row: what uses it.
- [ ] Library descriptor `+$10`: meaning.
- [ ] Sprite colour ramps (draw records) for the previews; colours 10-15 per
  screen band (from the Track's display data).
- [ ] The task roles (`dummer`, `Framer`, `Painter`, `Scroller`) are guesses
  from names.

### Crowd race

- [ ] Crowd marbles off the screen meet the course's moving pieces only as
  last left (except the portals).
- [ ] Ultimate's run-up still fails now and then for a marble that joins the
  lane late.
- [ ] Measure the planner's cost on slower machines (Android).

## Known bugs and inexact spots

- Intermediate's sound requests: the calls are identical, but the requests
  are 12,979 against 13,384 up to the goal — a held sliding sound keeps a
  channel longer in the original (one-blank timing of the channel pairs, as
  on Beginner).
- Beginner: the ice's glinting dots differ in place (probably the colour
  cycling `$108664`).
- 1-3 playfield pixels near the view's bottom differ in `drop1` (ring
  repaint or a region picture).
- In captures under load the goal flags' frames lag the world by up to two
  updates (the display's task); not modelled.
- The count `$100248` at later courses' starts: the port continues it and
  applies the measured +2 of a race's first update.
- Loading between courses: the original shows the last course for about
  10 s while the next loads; the port keeps 2 s and 0.4 s of black.
- The end's mode handler ran about 20 % slower than one update in the
  capture (slowdown with sixteen sprites); not modelled.
- Sound: the output filter is a fixed first-order 4.4 kHz low-pass (A500);
  the LED filter is not modelled. The positioned pan (`$121664`) is unused
  on Practice and unchecked elsewhere.
- Dust records past the end of the break dust list are skipped (the
  original draws them with stray colours).
