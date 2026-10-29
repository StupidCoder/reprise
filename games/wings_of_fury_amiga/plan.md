# Wings of Fury (Amiga) — plan

What is left before the re-implementation is complete, and the known bugs.
How the original works, and where the evidence is: [game.md](game.md).

## Where it stands (2026-10-29)

The whole game is playable and checked against the original: the opening,
rank selection, briefings, all fifteen maps (islands, airfields, the four
enemy ships, night missions, the Japanese carrier), promotions with the
fireworks, game over, the hall of fame, music, saved games (R&R), the
original's keys and debug mode. 45 recordings equal the original tick for
tick and 15 map set-ups equal its first tick (`tools/flightcheck.sh`);
screens match captures.

Finished, in order:

1. Import and assets (Rpck, PPkc shapes, pictures, palettes, maps, samples,
   texts).
2. Reference machine (FS-UAE harness, the game's own input points).
3. Map scenery and its pixel check; the backdrop.
4. Player and carrier deck pictures; take-off and the 1/8-scale view.
5. Flight model (FFP fitted to the ROM) and free flight.
6. Crashes and the carrier; crash effects, carrier animations, wires.
7. Dashboard, weapons, sound effects.
8. Missions 1-3, briefing, mission change, pillboxes.
9. Front end and music (`songplay` rewritten over `wofsongs`).
10. Maps d-j (airfields, ships, night), maps k-o, fireworks, saved games,
    keys, debug mode.
11. Cheats, widescreen, smooth motion, VHS look; persistent quick saves.

## Remaining tasks

### Comparisons not run yet

- [ ] Effect pictures (blasts, splashes, smoke) pixel-compared with grabs
  (so far checked by eye).
- [ ] Briefings of missions 2 and later (same text routine as mission 1's,
  not grabbed).
- [ ] Ship bob other than 0; the night palette split (sampled once, map i).
- [ ] Ship guns as rocket targets on ships other than the destroyer.
- [ ] Song 0 (the hall of fame) against the original's player.
- [ ] Widescreen: what the original shows past a map's ends (the view now
  reaches up to 96 pixels further); the signed-camera scenery fix left of a
  map's start.

### Not rebuilt

- [ ] The demo playback (`wofdemo` is not on the disk) and the game's own
  input recorder.
- [ ] The line editor's timeout.
- [ ] Music: the player's sound effects (commands 7-12, unused by the game).
- [ ] Keys left out: Ctrl-b (an `illegal` trap), the debug mode's Space
  (free memory), Help and F10. Ctrl-s and Ctrl-l stay Reprise's quick save
  and load (the mute and RETURN FROM R&R do their work).

### Research

- [ ] Shape mirroring's first use.
- [ ] The dashboard's sticky −$3902 flag; enemy ships in the forward view.
- [ ] Unrecorded or unknown fields: the skid counter (−$4368), −$5A3D,
  −$5C8E, −$5C94 (drawn as the propeller, 0 throughout a real flight).
- [ ] The player's −$5BE0/−$5BE4 parts, upset and facing right not yet
  seen; stick direction codes.
- [ ] The `army` font's use; the `newarmyfont` format; sample periods; the
  shape offset/sheet fields (hypotheses); the island's sand strip drawer.
- [ ] The R&R editor's starting cursor (at the end: hypothesis).
- [ ] Where the retail game called the manual check; a second (retail)
  release entry for the importer.
- [ ] The video standard (PAL assumed).
- [ ] The FFP oracle crashes after ~1,000 calls; make it robust if more
  FFP checks are needed.

## Known bugs and inexact spots

- The elevator, effects, crew and flags step per drawn frame in the
  original; the port draws once per tick, so their tie to the draw rate is
  not reproduced.
- Values taken as 0: the 8 bytes past a map's columns (uninitialised in the
  original), the atan division's low word, the camera's low word (−$41A0);
  gun posts' standing flag taken as cleared memory.
- The misplaced crash blast's x depends on the map's address; the reference
  machine's $FE04 is used.
- Random values from the beam position (shell splashes and the torpedoes
  they set off, the guns heard in the 1/8 view, rocket ignitions, hit timers
  and new heights, a wreck's blasts) come from a standard generator; the
  replays check them by their rules.
- Music: the CIA timer's first latch is assumed $FFFF; the original's lost
  audio interrupts (a one-shot playing twice) are not reproduced.
- The saved games are listed sorted; the original lists the directory's
  order.
- Widescreen: an island's enemy flag is chosen by the island the camera is
  over, so a wider view can show a neighbour's pole; enemy gun flashes are
  not drawn in the widened margins.
- Smooth motion is up to one tick (80 ms) behind the game, as it draws
  between ticks.
