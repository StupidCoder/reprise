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

- [x] Captures of the original: the full-energy reward's and the warp's
  messages, and level 11's hint screen, match the port's texts and layout.
- [ ] A capture of "OUT OF TIME": time4 checks the rule, but the message
  (1.4 s) was gone before the harness took its picture.

- [ ] The gate hit (−$140 when a gate's counter is 0 or $100) and the
  $B9/$AE-group drains are rebuilt from the code. No recording has the
  ball on such a cell at the right moment yet (arc1 crossed $B9 without a
  drain).
- [x] Item uses: repair1 (a repair set and the warp), rescue1, bomb1. The
  pause item is not recorded.
- [x] Bridge crossings $E3 and the reversing pieces $E4-$E7 (cross1, rev1).
- [x] Blocks: a vertical push that is blocked (block5), one that slides up
  (block6, level 28) and a block sinking into $21 (block4). A downward
  slide (direction 3) is not recorded.
- [x] Shops and bought balls: protect1, ice2, speed3 (four purchases up to
  the limit), xball8 ($47), hint11; a reward from the $24 countdown
  (reward1).
- [ ] Still unrecorded outside level 1:
  - the $23 teleport;
  - the $26 countdown and its warp in a recording that reaches 0;
  - a breakable wall actually breaking: in wall1 the protective ball
    bounces before its centre reaches the wall cell (not yet understood);
  - the pause item.
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
  locks, bridges, coins, purchases, energy, flames, repairs, the rescue,
  the bomb and blocks. The reward's and the hint's sounds were read off
  the original's log and matched by hand (the replay stops at the
  notice). The others are placed from the code's call sites only
  (countdowns, teleports, switches, gates, crumbling floor).
- Sounds asked for during a notice's wait are cut by the channel stop
  that follows at once. The port makes the same calls in the same order
  but has not compared the player's state across a notice.
- The panel's clock digits did not match the FS-UAE capture. Everything
  else in the panel matched. The likely cause is the lag above (the capture
  was taken mid-second); this has not been checked.

The original's own bugs are kept on purpose. They are listed in game.md,
"Quirks and bugs of the original".
