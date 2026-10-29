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
2. **Ruins**: the guide's demonstrations (DX challenge records 133, 139..144)
   are not ported; the ruins' own draw pool; the block's roll while sliding;
   motion blend frames; leaving the ruins (an extra pop: **hypothesis**).
3. **Robot game**: the goal, key, fuel-out and bonus stages not compared;
   timing drift from the 30 Hz script cadence.
4. **Horse ride**: the console draws ~1.6 random numbers per display frame,
   the port one, so the course forks after the masked rider's first random
   way; material constant colours (6870) not modelled; the ride's lighting
   is warmer and brighter on the console.
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
9. **Missing services** seen in the pilot run: 6025, 16551, 5043, 5111, 5120,
   5400, 6825/6835, 15401/15402, 15701, 16502, 16505, 3404, 3510; script
   blocks called by hash but absent after item pick-ups.
10. **Investigation cursor**: the capped step (`0x2D0FE0`) is not modelled.
    Some coins cannot be picked (the pilot skips them).
11. **Trial flag** and the console's save creation page at boot: not modelled.
12. **Saves**: the 0xA08-byte side file (copies of progress ranges, the
    volume) is not written; erasing and copying slots not looked at; 11019
    does not store the game volume.
13. **Movies**: the right eye (3D) is decoded but not shown; the skip prompt
    is untried.
14. **Rendering**:
    - toon combiner use of the alpha factor;
    - silhouette open edges;
    - only the first sky light is used;
    - camera kinds 3 and 5 near/far;
    - per-frame near/far offsets;
    - texture filtering of thin dark lines;
    - model alpha factor (6875) not blended;
    - the one-row offset of pictures and cells (vs text) against the console
      on some screens.
15. **Sound hypotheses**: effect envelopes, velocity/volume curves, key ranges;
    voices and 3D-positioned effects not checked on the console.
16. **Unknown format fields**: XFSA word 0x20, XPCK header words, IMGC header
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
