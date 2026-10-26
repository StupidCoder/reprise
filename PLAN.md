# Joyport plan

Joyport runs classic Commodore 64 and Amiga games as clean native C++
re-implementations. Rules for the work are in [AGENTS.md](AGENTS.md).

## The app

1. **Launcher.** A library of supported games, grouped by machine. Each
   entry is *planned* (not playable yet), *needs import*, or *ready*.
2. **Import.** The user picks or drops their own image (TAP now; later
   T64, D64, G64, PRG, ADF, …). The importer:
   - identifies the image by decoding it, not by its file hash: different
     dumps of the same release (other TAP recordings, other ADF copies) are
     accepted when they decode to the same content;
   - checks every checksum the original loader checks;
   - extracts into a staging folder, validates, then installs in one step;
   - keeps the decoded game files (PRG files for the C64, disk files for the
     Amiga), never the image itself.
3. **Play.** The game module reads its data from the installed files and
   runs on our own code. The pause menu has restart, quick save/load and the
   enhancement switches.
4. **Enhancements** (later, per game, each off by default). Ideas to check
   against what each game actually shows: widescreen view, CRT/monitor look,
   smoother scrolling, high-resolution rendering of vector games, remastered
   SID/Paula output filters.
5. **Platforms.** macOS first (app bundle with SDL), then Android (APK, same
   approach as the Picarat/Diorama Android builds).

## Shared building blocks

| Area | Contents | State |
| --- | --- | --- |
| `src/platform` | SDL 3 host: window, input, audio, file picker, data folder, SHA-256 | started |
| `src/ui` | own pixel font, drawing primitives | started |
| `src/app` | catalog, library and import pipeline, launcher screen | started |
| `src/c64` | TAP reader, KERNAL tape decoder, PRG files, G64 reader (GCR sectors, raw tracks, CBM DOS directory and files) | started |
| `src/c64` | VIC-II-style picture building blocks (charset/sprite decoders, palette), SID sound | planned |
| `src/amiga` | ADF/AmigaDOS reader, hunk loader, IFF, Paula sound | planned |

## Games

| Game | Machine | Image | State |
| --- | --- | --- | --- |
| Fort Apocalypse | C64 | `Fort_Apocalypse.tap` (MD5 `bec7409816865f3ad160af9984f127cd`) | playable from title to debrief, compared with the original in scripted runs; pause menu with the enemy-pilot option |
| Bruce Lee | C64 | `Bruce_Lee_(Original).g64` (MD5 `8cbfd1595cdc18a8e68fe4d43eec6aa8`) | playable from the title to the end and the next round, with sound, the demo and both two-player modes; compared with the original pass by pass (actors, rooms, screen, SID); smooth motion, particle and VHS options |
| Mindwalker | Amiga | — | planned |
| Marble Madness | Amiga | — | planned (RetroReverse has prior analysis) |
| Wizball | C64 | `Wizball.g64` (MD5 `73e1a136da4b6972d39f00817b86983e`) | disk import and asset decoding (levels, tiles, charsets, sprites) with launcher previews; game not rebuilt yet |
| Cannon Fodder | Amiga | — | planned |
| Rock 'n Roll | Amiga | — | planned |
| Wings of Fury | Amiga | — | planned |

## Fort Apocalypse (C64) milestones

1. **Tape import** — done. TAP → KERNAL boot file + Novaload fastloader
   pages (84 page records, all checksums) → four PRG files, identified by
   SHA-256 of the decoded content. Verified byte-identical to the
   RetroReverse Go extractor's output.
2. **Asset decoding** — done. Charsets, HUD rows, helicopter shapes and
   tilt table, bullet dots, terrain and radar of all three level entries
   (with the exact stream spills), from `game.prg`. Verified byte for byte
   against the game's own setup code run on RetroReverse's 6502 core
   (`.analysis/fort_apocalypse_c64/assets.md`). The launcher's details
   screen shows the decoded pictures; `fort_apocalypse_assets` exports them.
3. **Reference harness** — done. `fortref` boots the real tape on the
   RetroReverse C64 emulator to a checkpoint at the game's entry, replays
   input scripts and records per-frame state and pictures;
   `fort_apocalypse_replay` records Joyport the same way; `check.sh`
   compares every frame (`.analysis/fort_apocalypse_c64/front_end.md`).
4. **Title and options** — done: title (flashing frame, timer, exits),
   options (F3/F5/F7, idle return, pause), HUD counters. Identical to the
   original in five scripted runs except two CPU/beam race frames. The
   attract game and game start stop the module with a message. Play from
   the launcher (Enter; keys F1-F7 or 1-7, P = C64 space bar).
5. **Gameplay** — done: the whole game from the title on (craft, camera,
   bullets, enemy helicopter with its climb bug, tanks, homing missiles,
   mines, prisoners, depots, gates/walls/lasers/barriers, fort, three
   levels, bonus, debrief and rank, attract game). Compared with the
   original in scenario runs that set up a situation by poking the same
   RAM in both and replay the original's random numbers and main-loop
   schedule: flight, refuelling, rescue, both level exits, the fort, the
   mission's end and the enemy's climb bug match frame for frame apart
   from known sub-frame races (`.analysis/fort_apocalypse_c64/game_code.md`).
6. **Sound** — in progress: the game's SID effects (engine, shots,
   explosions, rescue, missiles, refuelling, fuel alarm, title tones) drive
   a register-level SID; not yet compared with the original's output.
7. **Particle effects and smooth motion** — done, two options: a
   presentation layer (`src/fort_apocalypse_c64/enhanced`) draws the game's
   picture again (the VIC collisions still come from the original picture).
   Smooth motion makes it a tilemap of the terrain with tanks, mines and
   shells taken out and drawn as objects where the game has them now,
   gliding (tanks rocking on the ground's slope, mines with their own
   animation and a wobble, the men running), the fuel depots and the
   landing pad sinking smoothly, tanks that come back rising out of the
   ground, and the craft banking smoothly; the rules keep the original's cells (objects stamp characters
   into the map, collisions read them). Particle effects add the debris,
   fire, tossed turrets and falling wrecks; wrecks bounce off the
   landscape by its surface direction (pushed out of rock they start in),
   and both helicopters leave one. Rotated art is resampled
   RotSprite-style onto the C64's double-wide pixel grid; unrotated art is
   copied as it is. Checked on rendered runs (`REPLAY_ENHANCED=1`, `=p`,
   `=s`); the original mode's comparisons are unchanged.
8. **Widescreen** — done: borderless, frames wider than 40 columns (the
   rasteriser takes any width) filled from a wide copy of the map taken
   with the original's window copy, one hidden column beyond each edge; the
   enemy shown where the game hides it outside its own range. From the
   original screen's shape with its border to 21:9. One rule changes with
   it: a shot past the window's left edge takes its real column (the
   original's $A341 subtracts unsigned, so there it tests cells 53-63
   columns to its right), and sprites past the left edge show there.
9. **VHS tape look** — done: a shared CPU filter (`src/ui/vhs.cpp`, about
   3-4 ms a frame at twice the picture's size) with four levels, an option
   of the pause menu. Previews with `REPLAY_VHS=<1-4>` and `REPLAY_PNG`.
10. **Carnage mode** — slice 1 done: an option (not the original game) with
    a rapid-fire gun whose tracers carve the brown rock ($4B, $61-$6B) pixel
    by pixel (`game/carnage.cpp`): damaged cells keep their own pixels,
    used by the picture (so the VIC collisions), the craft's and enemy's
    cell probes and the effects, both copies of the map's wrap columns;
    debris and sparks from the carved pixels. A tank blocked both ways (its
    track carved away at its home) stays put instead of turning for ever
    (the original would hang there; found by fuzzing the module). Slice 2
    done: falling bodies in the game's rules (`FallingBody`): rock islands
    found by a flood fill around each crater (held by anything fixed it
    touches, a structure in the next cell such as a tower, or by size,
    6,000 pairs; one held only by its size is looked at again every half
    second with no limit), tanks whose ground row is gone (out of the tank
    logic; the original's respawn brings them back, but only where their
    home still has ground), emitters ($4C-$4F)
    broken by six hits or a blast, taking their beam (which then sweeps
    from the falling emitter for two seconds, melting rock, burning
    objects, other emitters and the craft); all burst where they
    land (craters, debris, a tank burst and score) and crush the craft.
    Towers ($54-$58, three rows or more, not depots) rock on their base
    from hits, lean only into open sides, tip over past 9 degrees, pivot,
    drop and come to rest painted into the map (rock cells joined whole for
    the loose-rock test, so cut-off parts drop). The screen copy lags up to
    7 frames: carved and painted cells keep their records so it shows them
    as they are now, and a resting tower is drawn a few frames longer. Fuel
    depots take 60 hits (a blast six) and blow up: both copies cleared, no
    more refuelling or redrawing there, a crater of 22 pixels, the craft
    caught within 44; smoke, fire, pops and the fireball are effects. The
    last level's depot ends the mission: blown, "WELL DONE IDIOT" flashes
    in the message row for three seconds, the craft goes down and the
    debrief says "MISSION ABORTED" (start_life, whatever the lives). Test
    hooks `REPLAY_BLAST`, `REPLAY_OPTIONS`. Slice 3 done:
    combos (kills within two seconds; a bonus of 50 points per kill in the
    row, up to ten), the overdrive meter and six seconds of a double-rate
    gun in a fan of three, hit-stop (two or four frames) and playfield
    shake from the frame's impact, momentum flight (up to the original's
    whole step a frame, the most its camera follows; no sliding off a pad),
    the banners in the game's own HUD letters. Tracers end at the view's
    edges (the original window's, or the widescreen view's), so the carnage
    happens where it is seen. Bodies that come loose while the falling
    ones move (a landing blast, a beam) join the list afterwards (adding
    them during the walk crashed the game). Carnage mode is complete.
11. **Optional: loading screen** — the tape's U.S. Gold loading screen is a
   display script plus a music stream that also patches memory. It could
   play during import, read from the imported data (the music player is
   native code to rewrite; its stream is data).

## Bruce Lee (C64) milestones

Notes: `.analysis/bruce_lee_c64/` (`disk.md` loader and protection,
`game.md` memory map and assets).

1. **Reference run** — done. `harness/blref` boots the G64 on RetroReverse's
   C64 + 1541 core with the real 1541 DOS. The disk's protection reads a
   block with weak bits on track 18; the core has no weak bits, so the
   harness refills long zero-bit runs with noise. Boot to the game's entry in
   about 185 s of emulated time; checkpoint `entry.state`.
2. **Disk import** — done. G64 → GCR sectors → the loader's three encrypted
   stages re-implemented (KERNAL pointer trick, XOR chains keyed from the
   disk's code, sector order, page move, the loader's own checks and
   checksum) → `game.prg`, `high.prg`, `loading.prg`, recognised by SHA-256.
   Equal byte for byte to the emulator's RAM at the game's entry.
3. **Asset decoding** — started. Four charsets (equal to the game's
   $A000-$BFFF), 21 room maps with colours (room 0 equal to the game's
   screen), the loading picture, the text font (the C64 character ROM's
   upper-case glyphs, kept in `src/c64/character_rom.cpp` by decision,
   arranged and patched as the game's init does; equal to the game's
   $8800-$8BFF); shown in the launcher's viewer and exported by
   `bruce_lee_assets`. Next: sprites, room headers and links, sound data,
   the title screen.
4. **Game** — in progress. Rebuilt in `src/bruce_lee_c64/game/`: terrain
   probe, actors (Bruce, Ninja, Yamo: moves, decisions, poses), combat,
   lanterns, hazards, exits, scoring and extra falls, lives, every room's
   setup and script (`rooms.cpp`: openings, movers, sliders, the lift,
   crumbling floors, room 13's pieces, room 18's fireballs and face, room
   19's end and the next round). Checked with `tools/bruce_lee_replay
   lockstep` against harness runs: the attract demo (to its end), a
   scripted game (1435 passes, a death), and the room tour
   (`.analysis/bruce_lee_c64/tools/tour.sh`: room 0's exits poked to lead
   to each room, lanterns intact or all poked as taken). Next: title and
   attract cycle, player screen, game over, two-player modes, sound.
5. **Front end and sound** — done. Title with its options and music
   (`front.cpp`, `sound.cpp`: the "Micropearl" music player and the
   effect player rewritten, streams and tables read from the high block),
   attract demo, player and game-over screens, pause, F1, the two-player
   modes (turns with state swaps; the Yamo on a second joystick, its
   give-up rule), the "1UP" blink, the idle pause. The title, player and
   game-over pictures are pixel-identical to the original's; the title's
   music matches the original's SID writes over the whole title, the
   effects over a whole scripted game (`bruce_lee_replay music`,
   `lockstep` with `BL_SID`).
6. **Random numbers** — the original reads them from its zero page and
   page 1 (a new game clears the pointer's high byte), so the native draws
   from a view of the game's variables (`random_source.cpp`): 95.5 % of the
   bytes the draws read equal the recorded ones at pass starts; the native
   demo follows the original's.
7. **Enhancements** — smooth motion (actors, fireballs, the lift a row at
   a time), particle effects (blows, lanterns, crumbling floors with
   crawling cracks, fireball embers), light effects (lantern glow following
   its flame, in-between flame frames, hit flash), gore (defeated enemies
   and deaths by deadly cells or fireballs burst, Bruce hidden while he
   dies; `World::hazard_deaths` counts those for the presentation layer;
   pieces land on pixel pair 01, the terrain probe's ground), widescreen
   (`enhanced/wide_view.cpp`: each side shows the room behind a door on
   that edge, or one whose facing door leads here, from a copy of the world
   entered empty and run 8 passes, dimmed one palette step; each door pair
   is an anchor (Bruce's y here, dy; exact from the return exit's place
   where the other room has a door back, else door row x 8 + 26) and the height follows Bruce's y
   between anchors, eased, so it meets whichever door he takes (room 8's
   two west doors are 128 lines apart, room 7's 64); exits scroll 20 frames, to the glimpsed
   place when the new room was beside, else a playfield height for falls),
   VHS look; all off by default
   (`src/bruce_lee_c64/enhanced`). Preview: `bruce_lee_replay effects
   <content> <script> <frames> <out> <room> <first saved frame>` (all
   upgrades on, Bruce put into the room; `BL_WIDE=<width>` for the
   widescreen view, `BL_FALL_TO=<room>` fakes a fall 60 frames in,
   `BL_TRACE_BRUCE=1`, `BL_TRACE_CRUMBLES=1`,
   `BL_TRACE_DEATHS=1`; deaths walking right in rooms 8, 9, 12, 16, left
   in 5, 13, 17; standing in room 18 for a fireball). Cheats
   page: infinite lives, enemy hits don't count. Preview `BL_AT=x,y` puts
   Bruce at a place on the jump (seams of rooms 7/8 checked to the pixel
   at their upper doors). Widescreen not yet
   checked on a real fall or climb (only the faked one), nor with two
   players. `run` takes `BL_CANVAS=w,h`. Still only ideas: movers
   and sliders scrolling smoothly, room 18's flashing as a glow, room 19's
   colour wave, softer sky bands, sprite outlines.

## Open items

- Fort Apocalypse: not compared yet: the cavern teleport (mode $0A), hits
  by tanks and missiles, losing the last life mid-level, sound output.
- Fort Apocalypse: the level build's duration is modelled from measured
  work (±10 raster lines); a build ending that close to the HUD interrupt
  can start the life a frame early (the attract run does).
- Fort Apocalypse: sub-frame races are not modelled: the main loop's work
  against the beam (a mine half moved, "LOW ON FUEL" flicker), the
  playfield colour write's position on raster $7F (fixed at pixel 92).
- Fort Apocalypse: which code sets the in-play colour RAM to $0D (previews
  use the observed values).
- Pause menu: quick save keeps one slot in memory while the app runs.
- The terrain page in the details viewer shows the whole level reduced to
  fit; no panning or zoom yet.

- Import through the window (file picker, drag and drop) is wired up but has
  only been exercised from the command line (`joyport --import`) so far.
- Only one dump of Fort Apocalypse has been tried; other dumps of the same
  release should work (recognised by content), other releases are rejected.
- Images for Mindwalker, Marble Madness, Wizball, Cannon Fodder, Rock 'n Roll and Wings of Fury are not in the project folder yet.
- Bruce Lee: not compared with the original yet: the two-player modes
  (needs a harness run with both joysticks), the idle pause (11 minutes),
  the pause key. Not rebuilt: the idle pause's colour cycling ($1675,
  `$0153` room header bytes), the V key's version line (`toggle_version_line`
  exists, the launcher passes no V key), the title's joystick-hand messages
  (the hand modes are pause-menu options instead), the S key (an option).
- Bruce Lee: the random numbers' view of pages 0 and 1 gives temporaries
  as 0 (95.5 % of bytes as recorded): the native's numbers are not the
  original's exactly; the Y the room 18/19 draws use is not measured (0).
- Bruce Lee: the music player's first note after a tune restarts comes a
  frame later than the listing suggests (kept as observed, not understood).
- Bruce Lee: the alternating two-player mode uses the main joystick for
  both players (the original reads port 2 for player 2).
- Bruce Lee: not modelled exactly: which frame's stick a pass reads
  (native: the next frame; the original 13-47k cycles into the pass; the
  lockstep takes it from the log), passes stretched by stretches with
  interrupts off (the lockstep counts the logged frame work), the
  interrupt's random-counter steps inside a pass, the SID noise behind the
  random table; the band counter `$44` that `$3181` copies into the first
  extra fall's mark (seen 0 only); a second enemy leaving through an exit
  in the same pass (a second stack slip, not modelled); Bruce's map row
  for the slip when he is skipped (dying).
- Bruce Lee: whether the first probe after a lost life uses charset 0
  (`$07D0` → `$7C` = $A0) depends on whether an interrupt falls between
  the pass start and Bruce's probe; the app assumes it does not fall
  (charset 0). In the room 15 tour run the original's probe after the
  restart reports no ladder although `$7C` is $A0 and the native, probing
  charset 0 there, finds one: not understood yet.
- Bruce Lee: several poses share one collision-mask address in the
  original (`$117F`/`$1251`, e.g. poses 0 and 42-54 at $9000), so the last
  mask built there applies to all; the native keeps one mask per pose (to
  check which poses differ).
- Bruce Lee: `$430E` (lift speed), `$4375`/`$437F` and the room-18 state
  are not in the harness's tick records; the lockstep starts them from
  defaults. Only one disk image tried.
- Only TAP v0/v1 and G64 v0 are read; T64/D64/PRG imports come with games that need them.
- Android build not started.
- Enhancements: the effects keep no state in quick saves (they restart on
  load); the enemy helicopter's banking and the prisoners' running stay as
  in the original. With smooth motion objects show where the game has them
  now, while the craft's VIC collisions use the original picture, whose
  copied screen lags up to 7 frames; touching may look slightly off.
- Crash hold (particle effects): the game itself waits on the crash's last
  frame while the wreck settles (its SID voices gated off); from the
  crash's first frame a copy of the game is shown (the crashed craft put
  where the wreck is, so its camera follows it; the HUD from the real game)
  and is dropped. Not yet tried with the
  enemy or shells hitting things in that copy (their effects show, their
  outcome is lost).
- VHS tape: not tried in the app's window yet (rendered previews only).
- Widescreen: shots are not seen past the original window (the game ends
  them at its edges); not yet played in the app at full 21:9.

## Wizball (C64) milestones

Notes: `.analysis/wizball_c64/` (`disk.md` loader and protection, `game.md`
memory map, display, levels, code overview, open items).

1. **Reference run** — done. `harness/wizref` (Bruce Lee's harness adapted:
   pass marker, several write ranges, tick ranges) boots the G64 to the
   game's entry $6389 in about 84 s; checkpoints `entry.state` and
   `play1.state`. The game runs an unstable opcode (LXA at $B58B) that the
   core lacks; the harness finishes it with magic constant $EE.
2. **Disk import** — done. The disk is plain CBM DOS: LOADER → "F" (checks
   that sector 22/10 fails to read, the protection) → fast loader of the
   file "WIZBALL" unchanged. The import keeps `wizball.prg` ($0400-$FD00),
   recognised by SHA-256; equal to the emulator's RAM at the entry.
3. **Asset decoding** — started. Level map (16 x 96 tiles, two 45-tile
   levels per 4-row block), 249 tiles of 5 x 5 characters, the eleven
   places (map position, characters $A0-$FE, painted colours), the
   playfield and front-end charsets, 128 sprite frames; launcher viewer and
   `wizball_assets` export. Checked against the game's memory in play.
   Next: raster lists, sound data, front-end screens, sprite naming.
4. **Game** — not started (front end first, then play).

Open items: see `game.md` and `disk.md` (places 8-10, undecoded tables,
sprite colour byte, the LXA routine's chip dependence, other releases).

