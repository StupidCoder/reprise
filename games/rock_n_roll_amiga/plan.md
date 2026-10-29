# Rock 'n Roll (Amiga) — plan

What is left before the re-implementation is fully playable, and the known
bugs. How the original works, and where the evidence is: [game.md](game.md).

## Where it stands (2026-10-29)

A game runs from the title to the end:
- the title, with the hall of fame and every level's time records;
- name entry with the letter wheel or typed keys, and level codes;
- each level from its start wait to its exit, with the panel, items, falls
  and the continue/restart prompt;
- the results with the time records, saved to `scores.bin`;
- "N BALLS REMAINING", game over with the hall of fame, the continue after
  game over, and the ending.

The ball's rules match the original update for update in 36 recorded runs
(5,911 updates, in levels 1, 3 and 7; `.analysis/rock_n_roll_amiga/harness/regress.sh`).
Music and sound effects play as in the original. Steering uses the captured
mouse.

Finished, in order:

1. Import and assets: the cruncher, importer, decoders, launcher previews,
   `rock_n_roll_assets`.
2. Top level and input: the update order and mouse → velocity.
3. The rolling ball: steering, contact test, bounce, the surface cells.
4. Falls, pickups and locks.
5. Continue after a fall.
6. Shops, teleports, countdown cells, bridges.
7. Clock, exit, two-cell shops, the energy cell $D9, switches.
8. Breakable walls, crumbling floor, the play screen, a playable level.
9. Panel, items, energy-out, secret-warp targets, the captured mouse.
10. Front end and the game's course: title, records browser, name entry,
    level codes, results with time records (written back), ball messages,
    game over with the hall of fame, continue after game over, the ending.
    Checked against FS-UAE captures of the title, the hall of fame, level
    1's records and the results after finishing level 1.
11. Tile animations ($6C106/$6C004): gates, arcs, flames, coins and
    teleports animate, and their frames decide passage. The hazards drain
    energy by their counters. Checked in gate3, flame1, arc1 (25 runs,
    4,121 updates, all equal).
12. The eye's overview map (with its drawing pace, the map pieces' depth
    and MAGIC MAP; pixel-exact against the original) and pushable blocks
    (push test, slide, sinking, the warp count; block1–3 in level 7).
13. Ventilators ($6FAA0/$6ACE6, fan1–2 in level 3), the level rewards,
    hints, the secret warp's and "OUT OF TIME" messages, the pause before
    the prompt and the '?' key. Bridge corners and crossings $E3-$EB
    (corner1–2 in level 1).
14. Music and sound effects: the TFMX player rebuilt from the program
    ($37F00-$39128), the sample memory and song sets, music on the title,
    in the levels and in the jukebox, the effects wired to the rules. The
    player's state is equal at every frame for 1,500 frames of the title
    song and of level 1's; the effect calls match in snd1-4 (36 runs,
    5,911 updates, all equal).
15. Verification in later levels: the items (repair, rescue, bomb), the
    shops (protective, ice-proof, speed, a continue, a hint), a reward with
    its notice, bridge crossings and reversal, blocks pushed vertically
    (blocked, sinking, sliding) and the time running out, each against a
    recording (52 runs, 8,382 updates, all equal). Found on the way: a fall
    selects the rescue item, sound 2 is the rescue, the flames reset the
    animation cursor, and notices play $18 (and $18/$17 when the wait
    starts) with the rest of the update's sounds after the notice.
16. The open verification points closed: a wall broken by the protective
    ball, the $23 teleport, the $26 countdown's warp, a block sliding down,
    the pause item, the gate hit and the arc drains, the energy-out
    dissolve, switches and a crumbling cell with their sounds (63 runs,
    10,059 updates, all equal), and captures of "OUT OF
    TIME", the dissolve, name entry, the ending and the continue screen.
    Fixed on the way: energy-out restarts without the prompt and repeats
    its start every update; level 28's missing rows come from the last
    larger level file; the keys Esc/F1/F2 ('%', '§', '&'); the set-up's
    energy step; the mouse read's gate carried into name entry and the
    start wait; the panel clock kept across a level start; the crumbling
    floor's sound only when a cell falls in.
17. Host enhancements: touch input (swipes move the mouse counters, taps
    click), widescreen up to 21:9, smooth motion between updates and
    quick save, each checked headless (pictures after a load equal the
    running game's).
18. The VHS look, cheats (infinite energy and money), and quick saves kept
    on disk by the app.

## Remaining tasks

### In-level rules

None known: every cell value the 33 levels hold now has its rule (checked
against the handler table `$733C4`). What is rebuilt but not yet
compared with the original is under "Verification".

### Front end

None known: the wheel's read gate is carried over from the levels, and
name entry, the continue screen and the ending match captures.

### Sound

None known: the sound keys ('§' on F1, '&' on F3), the flash's $18/$17
and the set-up's energy step are in.

### Host and enhancements (off by default unless noted)

Done: touch input, widescreen, smooth motion and quick save (game.md, "The
port's additions"). Left:
- [ ] Try them in the app on a touch device and at 120 Hz: they are checked
  headless only (touch through the runner's finger steps).
- [ ] Quick saves anywhere: outside a level (title, results, notices,
  prompts) saving reports "not available", as the state is kept for levels
  only.

### Verification

Done: every item listed before is recorded or captured (game.md,
"Verification" and the *Checked* notes). Left:
- [ ] The reward's and hint's sounds are matched by hand from the log, as
  the replay stops at the notice.

## Known bugs and differences

- The original reloads a level's files from disk on a restart (a black
  view for several seconds); the port goes on at once.
- The original's Esc and F2 keys are Tab and F3 in the port (the host
  keeps Esc and F2); Del ('?') is the slash key.
- A level without a reward turns the view's colour 0 white for one
  picture. The original writes the colour register directly, so the white
  shows only from that moment of the frame until the copper list sets it
  again.
- The results' flashing colour is an estimate of the original's loop
  speed (`records.cpp`, `results_flash_colour`). The original changes it
  many times a frame.
- Effect calls are checked against recordings for bounces, falls, keys,
  locks, bridges, coins, purchases, energy, flames, repairs, the rescue,
  the bomb, blocks, teleports ($23), countdowns, gates, wall breaks, the
  dissolve, switches and crumbling floor. The reward's and the hint's
  sounds were read off the original's log and matched by hand (the replay
  stops at the notice).
- Sounds asked for during a notice's wait are cut by the channel stop
  that follows at once. The port makes the same calls in the same order
  but has not compared the player's state across a notice.
- The panel's clock digits did not match an early FS-UAE capture. The
  port now keeps the drawn seconds across a level start like the
  original; not re-checked against a capture.

The original's own bugs are kept on purpose. They are listed in game.md,
"Quirks and bugs of the original".
