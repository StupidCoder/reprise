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

The ball's rules match the original update for update in 28 recorded runs
(4,435 updates, in levels 1 and 7; `.analysis/rock_n_roll_amiga/harness/regress.sh`).
There is no music or sound yet. Steering uses the captured mouse.

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

## Remaining tasks

### In-level rules

- [ ] **Cells $51/$53/$66/$68**: the drawer records them at `$77F40` and
  `$6ACE6` acts on them (fans or magnets?). Not read yet. Their pictures
  ($51, and $52/$65/$66 following it) already animate.
- [ ] **Small balls** (`$6B470`): these also count the warp countdown down.
  Not read yet.
- [ ] **Level rewards** `$6D8CE` (per-level routines, table `$78008`).
  `reward_due` is raised but nothing happens.
- [ ] **Hint shop $45**: the sale is recorded (`hint_bought`), but the
  hint text (`$69DFA`, after `$6B8E6`) is not shown.
- [ ] **Messages**: the secret warp's text with its digits (`$69DC2`,
  `$67836`); "OUT OF TIME" (`$681E8`).
- [ ] **The pause before the prompt**: three delays (`$6BB9E`, about 14
  frames) between a fall and the continue/restart texts.
- [ ] **Give-up key '?'**: today the only way to give up is item 0, which
  the right button never selects.

### Front end

- [ ] **Jukebox** (the name COUNTRY, `$68CD4`): needs the music player.
  Today that name just asks for the name again.
- [ ] **The wheel's mouse read**: it can be gated by the grip flag `$70404`
  left from the last level (a read every 25th update). Check whether this
  shows in the original; the port always reads.
- [ ] **Not yet compared with captures**:
  - the continue screen's colours (the port uses the panel's palette,
    which the registers should hold when the copper list changes);
  - the ending picture;
  - name entry.

### Sound

- [ ] TFMX player, rebuilt from the copy in the program at `$37EE0`
  (entries +$24, +$2C, +$34, +$60). Song data comes from files
  $32-$3C, samples from $33-$3E, the title songs from file $40.
  - Open: world 3 uses file $3C for both songs and samples. Does it keep
    the previous samples?
- [ ] Sound effects: the calls to `$70A9C` with a number (0 bounce, 3 key,
  4 lock, 9 teleport, 10 switch, 18 purchase, $17/$18 on the results, …).

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

- [ ] The gate hit (−$140 when a gate's counter is 0 or $100) and the
  $B9/$AE-group drains are rebuilt from the code. No recording has the
  ball on such a cell at the right moment yet (arc1 crossed $B9 without a
  drain).
- [ ] Recordings for the item uses (bomb, repair set, rescue, pause). They
  are rebuilt from the code only, because level 1 has no shops. Needs
  pokes or a later level.
- [ ] A vertical block push (directions 1 and 3) and a block sinking into
  $21: rebuilt from the code, not recorded (level 7 has only horizontal
  tracks; levels 21 and 29 have others).
- [ ] Recordings outside levels 1 and 7, among them:
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
- The rewards, hint text, and the warp and out-of-time messages are
  missing.
- The results' flashing colour is an estimate of the original's loop
  speed (`records.cpp`, `results_flash_colour`). The original changes it
  many times a frame.
- No sound or music (title songs, level songs, effects).
- The panel's clock digits did not match the FS-UAE capture. Everything
  else in the panel matched. The likely cause is the lag above (the capture
  was taken mid-second); this has not been checked.

The original's own bugs are kept on purpose. They are listed in game.md,
"Quirks and bugs of the original".
