# Rock 'n Roll (Amiga) — plan

What is left before the re-implementation is fully playable, and the known
bugs. How the original works, and where the evidence is: [game.md](game.md).

## Where it stands (2026-10-29)

Every level can be played from its start wait to its exit, with the panel,
items, falls, the continue/restart prompt and game over. The ball's rules
match the original update for update in 20 recorded runs (2,576 updates, all
in level 1; `.analysis/rock_n_roll_amiga/harness/regress.sh`). There is no
front end, music or sound yet. Steering uses the captured mouse.

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

## Remaining tasks

### In-level rules

- [ ] **Overview map** (the eye, item 2): `$6B67E`. Includes map pieces
  $0D/$0E (`$6EB22`) and the one-pixel-per-cell drawing (`$6B77A`,
  colour ranges `$77E7D` → `$77ECB`). Using the eye is a no-op today.
- [ ] **Animated-cell objects** `$6C004`/`$6C106`: tile animation and the
  timers they run.
  - `$7164C`: the bomb flames $DA drain $C0 energy while the timer is
    ≥ $600. Today the flames appear but never hurt.
  - `$71934`: $B9/$BC drain $40 while the timer is ≤ $100.
  - `$71804`, `$71850`, `$7189E`, `$718EA`: compared with ≤ $1100.
- [ ] **Pushable blocks** $C0 (`$6D984`, the moving-object code
  `$6B372`). A block can move into $21, $14 or $24; one in the abyss counts
  toward the secret warp (`$69F88` → `$6D80E`).
- [ ] **Cells $51/$53/$66/$68**: the drawer records them at `$77F40` and
  `$6ACE6` acts on them (fans or magnets?). Not read yet.
- [ ] **Small balls** (`$6B470`): these also count the warp countdown down.
  Not read yet.
- [ ] **Level rewards** `$6D8CE` (per-level routines, table `$78008`).
  `reward_due` is raised but nothing happens.
- [ ] **Hint shop $45**: the sale is recorded (`hint_bought`), but the
  hint text (`$69DFA`, after `$6B8E6`) is not shown.
- [ ] **Extra-ball shop $47**: `Pocket::continues` counts purchases, but
  the balls left are not raised. Read how `$719B6` feeds `$699F8`.
- [ ] **Messages**:
  - "OUT OF TIME" (`$681E8`);
  - the secret warp's text with its digits (`$67836`);
  - "N BALLS REMAINING" / "LAST BALL" / "GAME OVER".
- [ ] **Give-up key '?'**: today the only way to give up is item 0.
- [ ] **Level end**: results, a new record time written into the
  high-score table, and the table saved back (game file $30, kept in the
  user's save folder rather than on the disk image).
- [ ] **Last level**: after it, the ending picture (file $41, "YOU MADE
  IT").

### Front end

- [ ] Title (`$68B88`, loop `$68FCA`): the right button goes to name entry,
  the left button to the high scores.
- [ ] Name entry (`$695F0`): a letter wheel driven by the mouse velocity
  routine (±$200 steps a letter), or typed keys.
- [ ] High-score table (`$69044` texts, `$6993A` after game over).
- [ ] Jukebox (titles at `$68208`), if it is reachable in this version.
- [ ] Disk-2 prompts are dropped (the crack's single disk; the port loads
  everything at once).

### Sound

- [ ] TFMX player, rebuilt from the copy in the program at `$37EE0`
  (entries +$24, +$2C, +$34, +$60). Song data comes from files
  $32-$3C, samples from $33-$3E, the title songs from file $40.
  - Open: world 3 uses file $3C for both songs and samples. Does it keep
    the previous samples?
- [ ] Sound effects: the calls to `$70A9C` with a number (0 bounce, 3 key,
  4 lock, 9 teleport, 10 switch, 18 purchase, …).

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

- [ ] Recordings for the item uses (bomb, repair set, rescue, pause). They
  are rebuilt from the code only, because level 1 has no shops. Needs
  pokes or a later level.
- [ ] Recordings outside level 1, among them:
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
- The flames' energy drain is missing (see animated-cell objects).
- The rewards, hint text, extra balls and messages above are missing.
- No sound or music.
- The panel's clock digits did not match the FS-UAE capture. Everything
  else in the panel matched. The likely cause is the lag above (the capture
  was taken mid-second); this has not been checked.

The original's own bugs are kept on purpose. They are listed in game.md,
"Quirks and bugs of the original".
