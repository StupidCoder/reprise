# Bruce Lee (C64) — plan

What is left before the re-implementation is complete, and the known bugs.
How the original works, and where the evidence is: [game.md](game.md).

## Where it stands (2026-10-29)

The whole game is playable from the title through all twenty rooms to the
end and the next round, with sound, the attract demo and both two-player
modes. The scripted game (1,435 passes) and the attract demo (1,612 passes)
run in lockstep with the original (actors, counters, room scripts, lift,
crumbling floors, screen and charsets compared after every pass); the room
tour checks every room's setup; the title music and the effects match the
original's SID writes.

Finished, in order:

1. Reference run (`blref`, with weak bits refilled for the protection).
2. Disk import: G64 → the loader's three encrypted stages → `game.prg`,
   `high.prg`, `loading.prg`, recognised by content.
3. Asset decoding: charsets, room maps, loading picture, text font.
4. Game: terrain probe, actors, combat, lanterns, hazards, exits, scoring,
   lives, every room's setup and script.
5. Front end and sound: title, options, music player and effects, demo,
   player and game-over screens, pause, the two-player modes, idle pause.
6. Random numbers as a view of the game's variables.
7. Enhancements: smooth motion, particles, lights, gore, widescreen, VHS,
   cheats, Carnage mode (two slices).
8. Persistent quick saves.

## Remaining tasks

### Comparisons not run yet

- [ ] The two-player modes against the original (needs a harness run with
  both joysticks).
- [ ] The idle pause (11 minutes without input) and the pause key.
- [ ] Rooms 1-20 against the game's own drawing (force `$29` in the
  harness); today checked through the room tour's lockstep and by eye.

### Not rebuilt

- [ ] The idle pause's colour cycling (`$1675` over the room header bytes at
  `$0153`).
- [ ] The V key's version line: `toggle_version_line` exists, but the
  launcher passes no V key.
- [ ] The title's joystick-hand messages (L / R keys). The hand modes are
  pause-menu options instead; decide whether the messages should show.
- [ ] The S key (music toggle): an option instead of the key.

### Research

- [ ] Why the first note after a tune restarts comes a frame later than the
  listing suggests (kept as observed).
- [ ] Room 15 tour run: after a lost life the original's probe reports no
  ladder although `$7C` is $A0, while the native, probing charset 0, finds
  one. Not understood.
- [ ] Which poses share a collision-mask address in the original (`$117F`,
  `$1251`); the native keeps one mask per pose. List the poses whose masks
  differ and decide whether the shared mask is observable.
- [ ] The band counter `$44` that `$3181` copies into the first extra
  fall's mark (seen 0 only).
- [ ] The loading picture: which bytes form bitmap, screen and colour (the
  loader writes $CE into the screen area and $01 elsewhere).
- [ ] Confirm that the loader's leftovers at $C000-$CA3F never become
  visible.
- [ ] Try other images (only this one has been tried).

### Enhancements

- [ ] Widescreen: check on a real fall or climb (only the faked
  `BL_FALL_TO` one so far) and with two players.
- [ ] Carnage mode: room 10's stone courses and columns count as objects and
  break; more than one Ninja at a time would need more actor slots.
- [ ] Ideas not started: movers and sliders scrolling smoothly, room 18's
  flashing as a glow, room 19's colour wave, softer sky bands, sprite
  outlines.

## Known bugs and inexact spots

- The alternating two-player mode uses the main joystick for both players;
  the original reads port 2 for player 2.
- Random numbers: the native's view of pages 0 and 1 gives temporaries as 0
  (95.5 % of the bytes equal the recorded ones), so the numbers are not
  exactly the original's. The Y the room 18/19 draws use is not measured
  (taken as 0).
- Timing not modelled exactly: which frame's stick a pass reads (native:
  the next frame; the original reads it 13-47k cycles into the pass); passes
  stretched by stretches with interrupts off; the interrupt's
  random-counter steps inside a pass; the SID noise behind the random table.
- A second enemy leaving through an exit in the same pass (a second stack
  slip) is not modelled, nor Bruce's map row for the slip when he is
  skipped (dying).
- The first probe after a lost life assumes no interrupt falls between the
  pass start and Bruce's probe (charset 0).
- `$430E` (lift speed), `$4375`/`$437F` and the room-18 state are not in
  the harness's records; the lockstep starts them from defaults.
