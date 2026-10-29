# Miracle Mask (Nintendo 3DS): plan

What remains for a complete re-implementation of the fifth adventure
(`AKKD`), and the known bugs and differences. How the game works:
[game.md](game.md). Detailed notes and the full open-item lists:
`.analysis/adventure56/docs/` (`story.md`, `puzzles.md`, `ruins.md`,
`rabbit.md`, `robot.md`, `secrets.md`, `movies.md`, `hwr.md`).

## Where it stands (2026-10-29)

- All 150 puzzles play and solve natively; traced puzzles match the console
  command for command.
- The story plays from the title to the end credits, with saves and
  continue. The autopilot reaches the staff roll in one run.
- Movies with subtitles, music, voices and effects.
- Side games ported: the ruins (all eight floors), the robot game, the shop,
  the horse ride, the train, and the rabbit's hub, notebook, show puzzles and
  care room.
- The secrets: profiles, gallery, music, voices and videos.

## Remaining tasks

1. **Rabbit**: the shows (6003) run with the hub's commands only; the stylus
   trail over the fur in Commu; the fur shader's alpha; the shows' multiple
   lights and spot light; the select and tutorial scripts beyond the first
   stage; service 1204 (repeat pulse) is a hypothesis.
2. **Ruins**: the guide mode is ported but not compared (item 11); the ruins' own draw pool; the block's roll while sliding;
   motion blend frames; leaving the ruins (an extra pop: **hypothesis**).
3. **Robot game**: the goal, key, fuel-out and bonus stages not compared;
   timing drift from the 30 Hz script cadence.
4. **Horse ride**: the console draws ~1.6 random numbers per display frame,
   the port one, so the course forks after the masked rider's first random
   way; 6870 now colours the horse's and the mask's outline (its alpha is
   not drawn); the ride's lighting is warmer and brighter on the console.
5. **Shop**: stages 2..10 and the end (results) not compared; the fixed
   good's blinking frame is out of phase.
6. **Wi-Fi menus** (4000/4100), `scrt_mode` (5000) and `scrt_chlg` (5100):
   mapped, not run. Whether the challenge loops (3500..3690) have data is
   not checked.
7. **Secrets**: pages after the first and the item screens behind each entry
   are not compared. The secret door needs a per-installation console address
   (now all zero).
8. **Puzzle hut**: the list is in place; the hut screen was never compared
   (the pilot solves every puzzle).
9. **Missing services** (looked into 2026-10-29; handlers from the game
   table `research/l5-handler-tables.tsv`, read in the ROM's code):
   - *Now implemented*: 6825 / 6835 (0x15fe98 / 0x1601bc: the face and
     material motion on a layer into a register; 6815 now honours its layer
     operand too), 3404 / 3504 (0x13f3a8 / 0x13cc24: 3402 / 3502 at scope
     level 0), 5111 (0x136738: the colour into three registers), 15401 /
     15402 (0x15e57c / 0x15e3ac: an area's `gmp/gNN.xi` / `.xa`, the latter
     only with the choice's animation flag; a true second operand evaluates
     the conditions afresh), 16502 (0x163220: 現在のガイド), 15701 (0x1622f0:
     a puzzle's place in 2301), 16551 (0x162394: give an item, as
     Game::giveItem), 3727 (0x14e064: a material motion's length, as
     3725 / 3726), 6832 / 6834 (0x14d7ec / 0x160270: 6812 / 6814 for the
     material motion layer; with 3727 the area-map rebuild reaches them). Together these complete the events' area-map rebuild
     (prog_event EventChangeGmpBuild / EventChangeMapBuild / ...Fade), the
     puzzle list's numbers (NazobaListStep, menu_jiten), Place_Item and the
     deepening puzzles' menu (menu_fukamaru).
   - *Implemented in the second pass (2026-10-29)*: 5120 / 6120 (0x156cb8
     / 0x1575f4 -> 0x2becfc: the alpha or the colour still changing; each
     tween's curve byte, +0x98 / +0xbc, stays below 32 while it runs; the
     scripts wait on it after 5103), 5322 / 5323 (0x135d08 -> 0x35114c /
     0x135d88 -> 0x2ca584: the alpha of every 2D object of a screen and
     kind, at once / over frames, as 6322 / 6261 for the 3D ones; 5323's
     sixth operand is passed on but not read), 6262 (0x14d194 -> 0x32b9a8,
     a second copy of 6322's code), 6025 (0x12c060 -> 0x1c48e8: a 3D
     object's kind, +0x76a, below 27), 16505 (0x163280: the current
     puzzle's id; the puzzle's 2301 record is copied to game +0x194a8, so
     no store to +0x194ac shows up in a scan; +0x194ae is 16541's type,
     +0x194c4 16542's title; the ladybug puzzles name motions
     "n%03d_blue_fly_start_00" with it), 6870 (0x14db74 -> 0x18b204: an
     outline's colour by the silhouette's name, the .sil block's word 0;
     the horse ride gives its horse and mask outline "#new_000" (48, 32,
     32); the alpha, 0.8, is kept but not drawn: *not known* whether the
     outline blends; the ride itself was not compared again).
   - *Read, not implemented*: 5043 (0x12bf9c -> 0x2b887c: an animation
     object's +0x150 player, one named value; 12 uses), 5400 (0x12e21c: the
     object's virtual +0x28).
   - Checked after the change: all 150 puzzles (`solve5.sh`), puzzle 1's
     key frames (`regress5.sh`), the sixth adventure's 168 (`solve6.sh`),
     and the auto-player to the staff roll (game frame ~1.03M, step 900).
   - Still missing in the full run before the guide port: only the ruins
     loop commands 139..142, which belong to the **guide mode** (item 11,
     now ported with 131..148 and 2210, 0x130870: a block started on the
     top loop's first thread). After the port the full run reports no
     missing service and still reaches the staff roll.
   - Script blocks called by hash but absent after item pick-ups: open.
10. **Investigation cursor**: the capped step (`0x2D0FE0`) is not modelled.
    Some coins cannot be picked (the pilot skips them).
11. **Ruins guide mode** (ported 2026-10-29; game.md "The ruins' guide").
    Played through with the research pilot (`A5_GUIDE=1`, from a
    checkpoint before the ruins): floor 1's route offered at its start
    cell, the tutorial event (06_068200), the coin prompt, the restart,
    the intro event (06_060150) bringing Layton onto the markers, the three
    phases (walk, dig while the mummy falls into the hole, walk to the 1508
    marker) and Clear_GuideMode. `A5_GUIDE=fail` steps off the markers: the
    failure event (06_068230, ガイド失敗ミイラ) plays, the room resets and the
    retried guide is cleared. The same run takes and clears both routes
    of floor 2 (five pushes; a mummy route of pushes) and floor 3's first
    two routes (pushes; nine phases with three digs), so the push and dig
    judgements hold in play. The full story run (no guide taken) passes
    the rooms' telops; the pilot watches them (DrawGuideTelop holds the
    main thread for about 170 frames). Open:
    - Not compared with the console (no capture of the guide yet): the
      markers' look and timing, the telop, floors 4.. .
    - The ruins' view care (0x1c8270) and draw pool (0x1d0808) are not
      ported: markers are drawn for the whole shown phase, also in rooms the
      unexplored-room shells still cover.
    - The markers' rotation for turned steps (5106's x codes) is the
      quaternion product decomposed into the objects' X, Y, Z angles.
12. **Trial flag** and the console's save creation page at boot: not modelled.
13. **Saves**: the 0xA08-byte side file (copies of progress ranges, the
    volume) is not written; erasing and copying slots not looked at; 11019
    does not store the game volume.
14. **Movies**: the right eye (3D) is decoded but not shown; the skip prompt
    is untried.
15. **Rendering**:
    - toon combiner use of the alpha factor;
    - silhouette open edges;
    - only the first sky light is used;
    - camera kinds 3 and 5 near/far;
    - per-frame near/far offsets;
    - texture filtering of thin dark lines;
    - model alpha factor (6875) not blended;
    - the one-row offset of pictures and cells (vs text) against the console
      on some screens.
16. **Sound hypotheses**: effect envelopes, velocity/volume curves, key ranges;
    voices and 3D-positioned effects not checked on the console.
17. **Unknown format fields**: XFSA word 0x20, XPCK header words, IMGC header
    bytes 0x0B/0x0E/0x0F and the doubled thumbnail maps, XQ32 block fields and
    command group, 502's table, maths 608/620–622/630/631.

## Known bugs and differences

- **Timing**: the port runs Init and Main in one frame and loads at once. The
  console needs frames for both and waits 3..9 frames in loads, so
  comparisons use offsets. Sound start and end latency: 1..2 frames per
  jingle.
- **Random numbers**: the generator matches when the console's state is
  injected, but the boot seed comes from console data and clock. Two puzzle
  solutions (006, 017) depend on the native seed.
- **Movies**: the post filter always runs natively; the console skips it when
  it falls behind. The movie quad's modulate pass (up to one level) is not
  modelled.
- **Place rooms**: edges differ by about a pixel; animation phases differ;
  the in-layer mesh order follows neither file nor material order on the
  console.
- **Text**: the console's line pitch is 18 + the second value of 5950; the
  port uses 17 in places.
- **Hypotheses in use**: XMTM linear interpolation, the canvas brush shape,
  the depth override of scope tags, 4005/4404.

## Checks to run after changes

- `tools/solve5.sh`: all 150 puzzles reach their verdict.
- `tools/regress5.sh`: puzzle 1's key frames.
- `picarat_a5_story_run` with the autopilot: the staff roll is reached; the
  ruins frame at 930000 stays byte-identical.
- `ctest` (`a56_xq_tests`, `moflex_tests`, `mobiclip_tests`).
- When shared 3DS code changes, also run the sixth adventure's `solve6.sh`.

## In Reprise (2026-10-29)

The game runs inside Reprise (catalog id `miracle_mask_3ds`); the integration is
described in [../reprise.md](../reprise.md).

Connected: import (AKKD unencrypted CCI, streamed: 26 s, ~200 MB memory for
the 1 GiB image), Start (the title) with the game's own continue from its
slots (`<data>/progress/miracle_mask_3ds/`), the 400x240 + 320x240 screens in
both arrangements, the VHS filter per screen, Shift+arrows tilt and X/gamepad
east shake, the phone's accelerometer as the console's motion sensor (since
2026-10-29), takeover of Picarat's pack and `akkd-saves`. No imported-data
previews yet.

**Quick save / Continue (2026-10-29)**: the app's checkpoint, as in the DS
games. `Story::checkpointFile` (src/story/a5_story.*): once a place is ready -
the place loop 1300 on top and begun, objects taking touches (2109 off), no
request waiting, two frames in a row - the game's own save record
(`saveRecord`, the slots' format) goes to `checkpoint.sav`, once each time the
place loop comes back on top (entering a place, back from an event or a
puzzle). `Story::continueFrom`: the game loop restores the record and pushes
1300 {0, -1} instead of the title - the title's continue (10100, then 10001 2)
without its recap event; the stack is then `1000 1300` instead of
`1000 2010 1300` (only matters when the place loop itself ends). The save's
picture is the frame of the checkpoint (after a fade, the first steady one).
Checked: an auto-player run (`picarat_a5_story_run` with `A5_CHECKPOINT=FILE`,
`A5_CONTINUE=1` continues) wrote 127 checkpoints up to step 324; continuing at
place 1010 (Celebration Boulevard) and at place 3050 (Zirkus Glitzerstern,
15 solved, objective and goal marker shown) works, and the auto-player plays
on from there (new places, a puzzle, step 320 → 324); through Reprise:
continue, quick save with picture, fresh load, damaged and foreign saves.
Not compared with the console (a host convenience). Ruins floors (loop 6400)
write no checkpoint: Continue returns to the last place before them.
Hypothesis to watch: state the title or the recap sets that a direct place
start lacks (none seen so far).

Not offered in Reprise (Picarat shell features, by decision): music player,
movie theatre, puzzle mode, minigame free play, the hub's "Continue story".
Movies, music, puzzles and minigames play as part of normal play.

Remaining for Reprise:

- typed name entry (the touch keyboard works);
- the accelerometer was built (SDL sensor, as in Picarat) but not yet tried
  on a phone;
- the screen filter's CPU fallback path is untested on a renderer without
  GPU shaders.

Validation:

- Validated on macOS (2026-10-29): `ctest -R layton_module` with
  `LAYTON_PACKS` (the story starts from the title with picture and sound);
  the real window driven by `reprise --events` scripts. Android: the APK
  builds; only Curious Village was run on an emulator (start, touch,
  rotation). No physical device was used.
- Not run on Android.
