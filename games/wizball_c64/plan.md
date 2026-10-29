# Wizball (C64) — plan

What is left before the re-implementation is complete, and the known bugs.
How the original works, and where the evidence is: [game.md](game.md).

## Where it stands (2026-10-29)

The whole game runs from the title: menu, wiztips, player screen, play
through every place, tunnels, bonus waves, the Wizlab, the level-complete
flight, the round end, game over and the high-score entry, in all four
player modes. Play is compared pass by pass with the original in lockstep
runs of up to 9,718 passes (pictures pixel-exact apart from the known
differences below); the front end's pictures and set-up times were
compared with recordings.

Finished, in order:

1. Reference run (`wizref`, with the LXA magic constant).
2. Disk import (`wizball.prg`, recognised by content).
3. Asset decoding: level map, tiles, places, charsets, sprite frames.
4. Play: places and stars, the wiz, enemies, shots and blazers, tokens and
   powers, catellite, deaths and lives, tunnels, end of a place, bonus wave,
   Wizlab, flight, round end, sound and the tune player.
5. Front end and keys: title, scores, menu, wiztips, player screen,
   game-over message, score entry, start-place keys, cheat sequence,
   pause/quit, shot volume.
6. Player modes 1-4, including the team's split control.
7. Enhancements: widescreen, VHS, effects (particles, lights, smooth
   motion), cheats, automatic catellite, Carnage mode (all three slices).
8. Quick save and load (persistent).

## Remaining tasks

### Comparisons not run yet

- [ ] Catellite catches of droplets (paint, life, wandering, filler burst,
  blackout); the blackout's picture.
- [ ] A tunnel to the left and the "furthest place" rule.

### Research

- [ ] Place 8 (never seen yet) and map columns 90-95 (9 is the Wizlab, 10
  the round-end room).
- [ ] Undecoded data: $3E93-$4552 and the tables between code; `$B2F0`
  bytes 2-3 and the fourth painted byte (`$B51A`); confirm that sprite byte
  63 is the colour.
- [ ] The raster list format in full and which lists exist.
- [ ] The LXA routine at `$B589`: its purpose and chip dependence; the
  obfuscated init at `$EC00` (followed only as far as `JMP $8F61`).
- [ ] Other releases and images (only this G64 tried; no D64 or tape
  import).

### Enhancements

- [ ] Widescreen for the bonus wave's own screens, the Wizlab and the
  flight's text screens (they keep their borders).
- [ ] The side columns' sky continues the edge column's pattern; the game's
  own pattern has seams where columns were prepared.
- [ ] Carnage mode: pillars do not rock before they go (they only fall when
  cut); tunnels are not specially blasted open.

## Known bugs and inexact spots

- Random numbers depend on each call's CPU cycle in the original, so a game
  started through the front end differs from the original from the first
  draw. The lockstep checks replay recorded values.
- Set-up times (screen blanking around `$8C44`/`$8C50`) are measured
  constants, not a CPU model; from the wiztips they are not measured.
- Title: the logo's badline lines (67, 75) and raster-race rows.
- The second player's name entry misses the extra blank frame of `$6BFB`'s
  `$8C50`.
- The HUD clocks are not carried between games; the front end's and play's
  token flash counters are separate (one counter in the original).
- The vanish stage's pause check is not modelled.
- Known picture differences in play: star dots in the bottom rows of bonus
  frames and in sequence frames (the star glyphs change mid-frame), the
  score in flight frames (two iterations a frame), frames after a pass that
  took two frames (a hit), one pixel at raster 66 in the lab.
