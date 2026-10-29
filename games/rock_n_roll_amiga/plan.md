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

## Remaining tasks

### In-level rules

None known: every cell value the 33 levels hold now has its rule (checked
against the handler table `$733C4`). What is rebuilt but not yet
compared with the original is under "Verification".

### Front end

- [ ] **The wheel's mouse read**: it can be gated by the grip flag `$70404`
  left from the last level (a read every 25th update). Check whether this
  shows in the original; the port always reads.
- [ ] **Not yet compared with captures**:
  - the continue screen's colours (the port uses the panel's palette,
    which the registers should hold when the copper list changes);
  - the ending picture;
  - name entry.

### Sound

- [ ] **The keys that toggle sound:** '§' toggles bit 1 of `$719A8` and
  restarts the music (song 2 instead of 0, `$68ACE`); '&' toggles the
  effects (`$68AAE`, with sounds $17/$18).
- [ ] **The sounds the original asks for and stops in the same moment:**
  $18/$17 at the start of the notices' and results' flash (`$6B9F4`),
  followed at once by stopping all channels. Not played by the port.
- [ ] **Sounds during level set-up and the start wait** (the energy bar's
  first step plays $11 at set-up in the original; the port plays it in
  the first update).

### Host and enhancements (off by default unless noted)

- [ ] Touch input: a swipe moves the mouse counters, like captured-mouse
  movement.
- [ ] Widescreen: the view is already a camera in map pixels. Draw wider
  than 16 columns and keep the original's right-edge run into the next row
  only in the original-width view.
- [ ] Smooth motion up to 120 Hz: interpolate the camera and ball between
  the 50 Hz updates (game state is kept apart from drawing).
- [ ] Quick save, like the other Reprise games.

### Verification

- [ ] Compare with captures of the original:
  - the rewards' and the warp's messages;
  - a hint screen (its colours: the port uses the panel's palette);
  - "OUT OF TIME".

  Their texts and order come from the program's lists; the rewards are
  checked against no recording yet.

- [ ] The gate hit (−$140 when a gate's counter is 0 or $100) and the
  $B9/$AE-group drains are rebuilt from the code. No recording has the
  ball on such a cell at the right moment yet (arc1 crossed $B9 without a
  drain).
- [ ] Recordings for the item uses (bomb, repair set, rescue, pause). They
  are rebuilt from the code only, because level 1 has no shops. Needs
  pokes or a later level.
- [ ] Bridge crossings $E3 and the reversing pieces $E4-$E7: rebuilt from
  the code, not recorded (level 18 has many).
- [ ] A vertical block push (directions 1 and 3) and a block sinking into
  $21: rebuilt from the code, not recorded (level 7 has only horizontal
  tracks; levels 21 and 29 have others).
- [ ] Recordings outside levels 1, 3 and 7, among them:
  - the $23 teleport;
  - the $24/$26 countdowns and the warp;
  - speed shops and bought balls;
  - breakable walls with the protective ball.
- [ ] The energy-out dissolve (`$6C3CC`), compared with a capture.
- [ ] Level 28 (file $1B): the map is larger than its file, and the
  original reads on into whatever memory lies behind the file (game.md,
  "Level files"). Check what the view shows at the bottom edge, and decide
  what the port fills in.

## Known bugs and differences

- The panel's clock shows 0:00 at a level's start. The original shows the
  previous level's time until the first second ends. Like the original, it
  then shows the second before.
- A level without a reward turns the view's colour 0 white for one
  picture. The original writes the colour register directly, so the white
  shows only from that moment of the frame until the copper list sets it
  again.
- The results' flashing colour is an estimate of the original's loop
  speed (`records.cpp`, `results_flash_colour`). The original changes it
  many times a frame.
- Effect calls are checked against recordings for bounces, falls, keys,
  locks and bridges; the others are placed from the code's call sites only
  (coins, teleports, switches, countdowns, gates, flames, repairs, blocks,
  crumbling floor, energy, purchases, the bomb).
- The panel's clock digits did not match the FS-UAE capture. Everything
  else in the panel matched. The likely cause is the lag above (the capture
  was taken mid-second); this has not been checked.

The original's own bugs are kept on purpose. They are listed in game.md,
"Quirks and bugs of the original".
