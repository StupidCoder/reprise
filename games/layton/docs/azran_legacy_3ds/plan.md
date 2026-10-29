# Azran Legacy (Nintendo 3DS): plan

What remains for a complete re-implementation of the sixth adventure
(`AL6D`), and the known bugs and differences. How the game works:
[game.md](game.md) and, for the shared engine,
[Miracle Mask's game.md](../miracle_mask_3ds/game.md). Detailed notes:
`.analysis/adventure56/docs/puzzles6.md` and `minigames6.md`.

## Where it stands (2026-10-29)

- All 168 puzzles with content play and solve natively in puzzle mode, and
  the same inputs end in "RICHTIG" on the console.
- Minigames: the Nut Roller (course 1 verified) and the Flower Garden (garden
  1 verified with yellow and red) run natively.
- The story has not been started.

## Remaining tasks

1. **Story**: the game loop's own preload list (`seq/prog_lt` after
   `prog_common`, `0x1D6F5C`), then the loops, story tables, places, events
   and saves on the fifth adventure's story code, verified as for the fifth.
   Then the conveniences: theatre, music player, checkpoints, touch areas.
2. **Minigames**:
   - Fashion (6100): write the native class from the survey and verify it on
     the console.
   - Airship (6000): survey and port.
   - Nut Roller: courses 2..10 (acorn trees, high curves, bridges along).
   - Garden: blue and purple flowers, poison, walls, gardens 2..10. The
     flowers kept by 2110 8/9 must go into the save (+0x128C4).
3. **Daily puzzles**: types from 101 up in the type switch; 20 `dNN_*.xq`
   scripts.
4. **Puzzle visuals**:
   - 057/096/105: the village lights are lit at the start natively and dark on
     the console (the light meshes' start value is probably in the model file;
     the port assumes 1).
   - 145: the sphere ripple (18000: a latitude/longitude mesh textured with a
     render target; 3431, 5438/5439).
   - 5433/5434: a picture content's value, set and tweened (017's map turn
     fades it; 151).
   - 20600: the byte the material set-up reads (040, 041, 128, 130).
   - Slide2: the train drum's lag over long runs of moves (probably a tween
     restarted each move) and the wagons' slide time.
   - Fit 121/133: the wagon's slide time.
   - BridgeLabyrinth: the player's shadow (50/51) is not drawn; mid-walk more
     cubes fall natively than on the console (check the unsupported-group
     rule).
5. **Renderer** (shared): the draw order of 3D objects with the same priority
   (the Nut Roller's ball shadows still show black); the garden walls'
   shadow decals are broader than the console's; the bloom burst looks
   bigger; `.atr` word 2 may hold depth flags; water is darker; the following
   camera frames a little closer; `#FIX_MDL` alpha-blend and alpha-test rules
   to apply.
6. **Unknowns**: 4405 (stereo value, returns 0); the drawing effect of the view
   switches; XMTM interpolation (linear assumed) and descriptor byte 6; 5950's
   second and third values; 11002's dimmed arrows (not drawn); the pad hooks'
   dispatch and kinds; 3591's reader; Partition 1004/1011 and Fit 38/39
   (unused by the puzzles here).
7. **Motion sensor on phones**: wired, checked on the emulator, not yet in a
   3DS game on a phone.

## Known bugs and differences

- **Pictures** sit one row higher than the console's while text does not
  (shared renderer; mean differences ~8 on the lower screen).
- **Timing**: the console takes input a little later after loads. Solutions
  rest the pen on the target before lifting, because the console's pen can
  end short of a drag's last point.
- **Random idles**: characters' random idles, snowflakes and the "RICHTIG"
  picture differ by random state.
- **Coin counter** in puzzle 013: "05000" natively, "5000" on the console.
- **Puzzle 051**: a press meant for one turned piece took another on the
  console. The console's touch areas or stacking for turned Ltgram pieces may
  differ; the solution now keeps presses clear of other pieces.
- **Hypotheses in use**: ChangeTexture's 512 × 256 texture size; 6838 as
  material alpha; the Vanish direction table order; Slide2's cell offsets per
  side; the GuardRobot list order for left/right; 20300's on-screen turn
  sense.

## Checks to run after changes

- `tools/solve6.sh`: all 168 puzzles reach their verdict natively.
- The Nut Roller course-1 solve and the garden-1 runs (taps in
  `minigames6.md`).
- When shared 3DS code changes, also run the fifth adventure's `solve5.sh`,
  `regress5.sh` and its story frame check.

## In Reprise (2026-10-29)

The game runs inside Reprise (catalog id `azran_legacy_3ds`); the integration is
described in [../reprise.md](../reprise.md).

Connected for import only: the AL6D CCI imports (streamed), Picarat's pack is
taken over, the card says it cannot be started. The story is not
reconstructed; the puzzles and two minigames that run in the engine are not
offered as a substitute (Reprise has no puzzle browser by design).

Not offered in Reprise (Picarat shell features, by decision): music player,
movie theatre, puzzle mode, minigame free play, the hub's "Continue story".
Movies, music, puzzles and minigames play as part of normal play.

Remaining for Reprise:

- typed name entry (the touch keyboard works);
- an accelerometer for the 3DS games on phones (Picarat had one);
- the screen filter's CPU fallback path is untested on a renderer without
  GPU shaders.

Validation:

- Validated on macOS (2026-10-29): `ctest -R layton_module` with
  `LAYTON_PACKS` (the story starts from the title with picture and sound);
  the real window driven by `reprise --events` scripts. Android: the APK
  builds; only Curious Village was run on an emulator (start, touch,
  rotation). No physical device was used.
- Import path shared with Miracle Mask (checked with AKKD); AL6D's pack
  takeover checked.
