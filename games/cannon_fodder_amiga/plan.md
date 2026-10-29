# Cannon Fodder (Amiga) — plan

What is left before the re-implementation is complete, and the known bugs.
How the original works, and where the evidence is: [game.md](game.md).

## Where it stands (2026-10-29)

The whole game plays in the shell with the mouse, from the presentation to
the ending: all 72 phases with every object kind the maps place (vehicles,
hostages, power-ups, the last mission), the campaign (roster, recruits,
promotions, heroes, hall of fame) and every front-end screen rebuilt from
the program (intro, hill, parallax, briefing, debrief, load/save, ending).
The rules are checked tick by tick against recordings of the original
(`tools/replay_all.py`); camera, tile drawer, panel, object drawing,
parallax, debrief and the RJP1 music player against the original's state
and screens.

Finished, in order:

1. Import of all three disks (Sensible disk format, RNC) and assets.
2. Reference machine; top-level structure; the view and scrolling.
3. Camera motion, tile drawer.
4. Mission set-up, object drawing, panel.
5. Squad walking, shooting, deaths, enemy soldiers.
6. Explosives, scenery, mission objectives; sound and music (RJP1).
7. Playable from the launcher; maps 1-10; pointer and music sequence.
8. The whole game: every object kind, campaign, front end, squads.
9. Persistent quick saves, widescreen, smooth motion, VHS look, trackpad
   keys, cheats.

## Remaining tasks

### Recordings still to make

- [ ] A damageable map tile actually blown up (the queue agrees; no
  recording has one yet).
- [ ] Crate pick-up, a blasted bush, trap pick-up.
- [ ] Civilians walking home (objective 8, `$9A380`) and the fast
  spear-throwing civilian ($3E, `$9938A`, kind $47).
- [ ] Homing rockets ($2D/$61, `+$75` bit 0) and the screen shake
  (`$817C6`-`$817CE`).
- [ ] Objective 4 (protect civilians) and 6/7 as separate kinds.
- [ ] Parallax recordings of the other four landscapes.
- [ ] Joining squads (not tried headless).
- [ ] Object drawing with hidden lines, negative/positive layers and y ties.
- [ ] The camera look-ahead's right-button branch (`$872A4`).
- [ ] Sound: release and slide, speed modes other than 1; compare the
  channel model with FS-UAE's audio output.

### Not rebuilt

- [ ] The save-disk request (no disk swaps here).
- [ ] Typed save names (skipped by decision, 2026-10-28; saves are listed by
  mission and phase).

### Research

- [ ] The panel's lit-box condition and vehicle icons: confirm against the
  original.
- [ ] Data fields: tile 399 (map 31), `.spt` fields, `.hit`/`.bht` meanings,
  map cell flag bits 13-15 beyond the music points, the directory header
  byte $1A, recruit record bytes 6-8.
- [ ] Sprite-sheet palettes; `*play.lbm` and the `.pl8` colours.
- [ ] Game logic: `$81E02` (stop shots), the bleed marker (`$81DCA`,
  `+$40` = $9876) and the mission-end rewrite of `+$26`, flagged map cells in
  the sight line, the leftover register in the enemy aim (`$9DA04`).
- [ ] Kinds 1, $A and $B (dynamic) were never seen in play.
- [ ] The crack's TRAP #1 hook ("pstu"/"rank"), the retail boot, and a
  second (retail) release entry for the importer.
- [ ] The game's own unpacker in `FODDERC`/`fload` (not located).

## Known bugs and inexact spots

- Random choices use a standard generator (the original's draws read live
  memory and mouse deltas); replays check them by rule and range.
- The tile drawer's snapshot is taken at the tick start; the original's
  jitters with CPU load (a 260-frame recording with a reversal agrees on 258
  of 259 frames).
- The first fire-control frame of a both-button press comes one frame later
  in some presses in the original.
- The vertical blank's parity at the parallax's start: a recording began a
  frame later (same states a frame apart).
- Ring columns 304-319 are left stale.
- Replay artefacts in recordings started from a poked boot (map 10, map 21:
  a recruit's waypoint, a removed placed soldier, one enemy's aim) come from
  the poked roster, not from the rules.
