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
| `src/amiga` | ADF/AmigaDOS (OFS/FFS) reader, hunk load files, ByteRun1, IFF ILBM | started; Paula sound planned |

## Games

| Game | Machine | Image | State |
| --- | --- | --- | --- |
| Fort Apocalypse | C64 | `Fort_Apocalypse.tap` (MD5 `bec7409816865f3ad160af9984f127cd`) | playable from title to debrief, compared with the original in scripted runs; pause menu with the enemy-pilot option |
| Bruce Lee | C64 | `Bruce_Lee_(Original).g64` (MD5 `8cbfd1595cdc18a8e68fe4d43eec6aa8`) | playable from the title to the end and the next round, with sound, the demo and both two-player modes; compared with the original pass by pass (actors, rooms, screen, SID); smooth motion, particle and VHS options |
| Mindwalker | Amiga | `Mind Walker (1986)(Commodore).adf` (SHA-256 `2e83a67a…a8969c`) | disk import and decoded assets with launcher viewer; title, world menu and new game playable (mouse), matching the original frame for frame and pen for pen; world generator's base layer verified against the original; the landscape and later stages not rebuilt yet |
| Marble Madness | Amiga | `Marble_Madness.adf` (MD5 `735dc697d64b3eeaa000778eb0b1153a`) | disk import (both encrypted programs decrypted, checked against the original's memory); asset previews; all six courses complete in play (physics, covers, creatures, special regions, music and effects; each compared with the original update for update from start to goal); front end for one player (options, introductions, bonus, game over, the end) |
| Wizball | C64 | `Wizball.g64` (MD5 `73e1a136da4b6972d39f00817b86983e`) | disk import, asset decoding with launcher previews; play rebuilt and checked in lockstep (places, enemies, catellite, tunnels, lives, bonus wave, Wizlab, level flight, round end, sound and music); front end, high scores and keys rebuilt |
| Cannon Fodder | Amiga | — | planned |
| Rock 'n Roll | Amiga | — | planned |
| Wings of Fury | Amiga | `Wings_of_Fury_v1.0_1990_Broderbund_cr_Hysteria.adf` (SHA-256 `638d9a9d…4620044`, a cracked copy) | disk import and decoded assets with launcher viewer (pictures, all 12 shape libraries, palettes, sounds, texts); nothing playable yet |
| Tower of Babel | Amiga | — | planned; original interpretive shell cover added, importer and gameplay not implemented |

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
   page: infinite lives, enemy hits don't count. Carnage page: carnage
   mode, first slice (`game/carnage.cpp`, `World::Carnage`): Bruce's blows
   launch the enemy (kick > chop > low strike, harder out of energy) in a
   per-frame flight that keeps the thrown move (so pose, deadly cells,
   exits and the out-of-energy removal stay the game's own); bounces on
   pixel pair 01, hard hits cost energy; decoration (cells only in pairs
   00/10, never tested by an actor rule) smashes; a flying enemy knocks the
   other over and can be hit again (juggle); combo scores 50 x count from
   the second hit; events feed debris, dust, "xN" popups, hit-stop and
   shake. Off: pictures identical to the previous build in the attract
   demo, three scripted games and a run with upgrades on (180 each).
   Second slice: the Yamo's blows launch Bruce (Bruce's flights score
   nothing, hurt half); objects only break (decoration pieces of 4-80
   cells, bodies faster than 2.2); a small piece (up to 40 cells, nothing
   the game keeps touching it) left with nothing under it falls and bursts,
   knocking over whoever it hits; each enemy beaten shortens both enemy
   timers (halved per wave up to /8, at least 3) with a WAVE popup; room
   18: a blow meeting a fireball sends it to the face (1000 points; the
   game's fireball waits 12 passes; drawn only in shown pictures, never in
   the one the collisions read); combo points capped at 10 hits. Preview
   `BL_CARNAGE=1`, `BL_TRACE_CARNAGE=1`, `BL_HARMFUL=1` (enemy hits land).
   Open: room 10's stone courses and columns count as objects and break;
   more than one Ninja at a time would need more actor slots (not done). Preview `BL_AT=x,y` puts
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
- Images for Cannon Fodder and Rock 'n Roll are not in the project folder yet.
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
memory map, display, levels, code overview, open items; `play.md` the
rebuilt play, `front.md` the front end).

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
4. **Play** — in progress (notes: `play.md`). Rebuilt and compared pass by
   pass with the original (`tools/wizball_replay lockstep`, runs of up to
   9718 passes, pictures pixel-exact): the scrolling places and stars, the
   wiz (bounce, spin, thrust, antigravity, landscape), enemies (templates,
   spawn lists, motion, firing, droppers, pearls), shots and blazers, the
   tokens and powers, the catellite, death and lives, tunnels and the place
   change, sound effects and the tune player.
   Also done: the end of a place (vanish, bonus wave from the wave script,
   kill count), the Wizlab (permanent weapons, painting), the level-complete
   flight, the round end; checked in runs of up to 9118 passes through
   three clears, a level change and a round end.
5. **Front end and keys** — done (notes: `front.md`). Title with the top
   scores, menu of modes, three wiztips pages, player screen, game-over
   message, high-score entry with names, start-place keys and the cheat
   sequence, pause/quit (RUN/STOP = Tab, Q) and shot volume (↑ = `]`,
   =). Pictures compared with the original (wiztips to 1 px; title and
   score entry apart from raster races and colour phases), set-up times
   measured. The module runs the whole game from the title.
6. **Enhancements** — started (options, all off by default):
   - Widescreen: while a place scrolls (play, vanish, game over), the
     landscape and sky run on to the window's sides up to 21:9, from the
     map as the game lays it in (`Playfield::side_column`); only as far as
     the game's own view reaches at a place's ends (black beyond); cleared
     places stay empty; sprites outside the window show where they are
     (80 pixels either side). Other screens keep their border at the
     sides. Preview: `WIZ_WIDE=<columns> wizball_replay front ...`.
     Checked: side columns move with the window every frame.
   - VHS look (shared).
   - Cheats: the game's own invincibility without the key sequence (the
     option also keeps the catellite from taking hits; the original's own
     cheat does not), infinite lives, all start places on the title's keys.
   - Effects (`src/wizball_c64/enhanced/effects.cpp`, drawn over the play
     picture, reading the game only): explosion debris in the explosion
     sprite's colours ($A891 frames $5F-$5C) with a short dithered ring of
     light, paint drops (a falling droplet's drip trail; drops of its paint
     thrown up where it bursts, $A925, landing again), pickup sparkles (a
     pearl, life or token taken, a droplet the catellite catches), wiz
     effects (a ring of light sweeping out with the smart bomb's strobe;
     sparkles shed by the wiz's rings as it fades out and in; a shimmering
     field for the shield, the catellite's too; a glow along the wizspray's
     flickering rings; a larger light ring on its burst, under Explosions),
     blazer glow (under Shot streaks), catellite effects (sparks per hit, a
     burst when lost), tunnel sparkles (sinking, the exit rising, the slide
     out), cauldron bubbles, the blackout's fade (three steps through
     darker C64 colours after each bump; a `blackout_fade` parameter of
     Play::picture), the flight's text glow, Wizlab sparkles (the weapon's
     token; paint rising from the colour cauldron), menu star trails; the
     bonus wave and the flight now have the effects and widescreen (a
     cleared place's side columns are sky),
     shot streaks and impact sparks (the wiz's and catellite's shot
     characters; the aliens' shot dots and the wiz's death pieces trail
     back along their flight and spark on the landscape), smooth star drift
     (the five drifting stars at single pixels from their sub-character
     position). Preview: `wizball_replay run <content> <script> <frames>
     <dir> <frames>` with `WIZ_OPTIONS=1=1,2=1,3=1` (`WIZ_CANVAS=w,h` for
     widescreen); `front` with `WIZ_LOG_EXPLOSIONS=1` lists object states,
     `WIZ_INVINCIBLE=1` plays on, `WIZ_DROPPERS=1` makes every alien a
     paint dropper (place 1's aliens turn to pearls), `WIZ_LOG_SHOTS=1`
     counts alien shots, `WIZ_PEARLS_AT=<frame>` gives six pearls (the
     light to the smart bomb; fx bomb script waggles it off at ~717;
     `frame:n` for other tokens), `WIZ_PAINT_AT=f1,f2,...` completes the
     place's colour (three give a flight), `WIZ_TUNNEL_AT`,
     `WIZ_BLACKOUT_AT`, `WIZ_CAT_HIT_EVERY` + `WIZ_CAT_OPEN`, logs
     `WIZ_LOG_CAT`, `WIZ_LOG_BLACKOUT`, `WIZ_LOG_STARS`; stage, tunnel and
     Wizlab weapon changes are always printed, `WIZ_EFFECTS=1` draws all effects into the PNGs
     (first explosion of the long test run at frame 3752, a splash at 725
     with droppers, alien shots from 3672).
   - Quick save and load: the whole state (front end, game, sound, the
     random numbers); `wizball_replay savetest <content> <frame> <frames>`
     checks that after a load pictures and sound equal a run that went on
     from the save (0 differing frames at 50/200/1500/3000).
   Open: the bonus wave, the Wizlab and the flight are not widened; the
   sky pattern in the side columns continues the edge column's (the game's
   own pattern has seams where columns were prepared).
8. **Carnage mode** — slice 1 done (an option, not the original game,
   `game/carnage.cpp`): fire is a rapid paint gun (a shot every three
   passes, 9 pixels a pass, angled by down/up, in the cauldrons' colours in
   turn; it replaces the wiz's own shots, the spray and blazers stay).
   Shots hit aliens through the game's own hit ($6FBE), which bursts them
   into flying paint that splashes where it lands; on the landscape they
   carve a crater and splash paint round it. A damaged cell keeps its own
   pixel pairs (Display::set_cell_art: the character's pair values or a
   painted colour) under its map position (Playfield::cell_key), so the
   damage stays while the view scrolls and in the side columns; a cell
   with fewer than three solid pairs opens to sky for good
   (Playfield::open_cell, also in the prepared screens and every later
   map read), and the game's cell tests then find nothing there. A
   completed place floods with colour in bands widening from the wiz
   while it fades. Own random numbers. Preview `WIZ_CARNAGE=1` in
   `wizball_replay front` (option 8 for `run`). The options page shows
   five entries like the other games (widescreen; particle, light and
   smooth-motion effects, each switching a group of the effects above;
   VHS) with the cheats and carnage on pages of their own.
   Slices 2 and 3 done, carnage mode complete:
   - Loose landscape: when a cell opens, the landscape next to it is
     flood-filled over the screen's cells; a part that no longer reaches
     the ground rows (18-19) or the screen's edges (where it may go on),
     and is at most 160 cells, comes loose as a body of its pixels that
     falls, turns away from the side it was hit on (a cut pillar
     topples), crushes aliens and the wiz it falls on, and shatters where
     it lands: rubble that splashes its colours, three craters, points.
   - The smart bomb's shockwave: a ring from the wiz that cracks the
     landscape it passes (small craters, rainbow splashes).
   - The wiz as a wrecking ball: at speed index 3 or more it rams the
     aliens it touches (the game's own hit); bumping the landscape at 2 or
     more bites a crater in front of it.
   - The catellite as a flail: while it only follows (not steered, not
     wandering, not a team's), it swings on a 30-pixel tether from the
     wiz with gravity and drag, smashes aliens it meets without taking a
     hit, and bites into the landscape it strikes.
   - Arcade layer: kills within two seconds make a combo (50 points a kill
     in the row, up to 500), combos fill a meter, a full one gives six
     seconds of overdrive (a shot every pass in a fan of three, rainbow
     paint); banners in the score line's middle ("COMBO X5", "OVERDRIVE");
     hard hits ask the module for a hit-stop (two or four frames) and a
     shake. Checked: a 200,000-frame random soak with carnage (15 clears,
     labs and flights), the module with every option at 21:9; with carnage
     off the lockstep runs and quick save are unchanged. Test logging
     `WIZ_LOG_CARNAGE=1`; `soak` takes `WIZ_CARNAGE=1`.
   - Automatic catellite (a separate option, on the Cheats page,
     `game/catellite_ai.cpp`): a computer team mate on the second stick,
     wired like the team modes ($697D control 2), so holding fire never
     hands the stick to the catellite. It goes for the nearest falling
     droplet worth catching (paint 2/5/6, white for a life), keeps clear
     of aliens unless it cannot be hurt, shoots an alien level with it,
     and otherwise lets the catellite trail the wiz. Only for a side
     played alone. Test: every alien a paint dropper, tapped fire, 8,000
     frames: 6 of 6 droplets caught with it, 2 caught and 3 missed
     without. Preview `WIZ_CAT_AI=1` (front, soak), `WIZ_LOG_PAINT=1`.
   Open: no rocking of pillars before they go (they only fall when cut);
   tunnels are not specially blasted open (their cells can be shot away
   like any other).
7. **Player modes** — done: the two-player team's split control (the
   catellite on the other joystick, $697D) added; modes 1-4 match the
   original pass by pass through six lives and the game-over messages
   between them (play.md, "Player modes"). Bug found and fixed on the
   way: the game-over message blacked out the wrong cells and missed the
   sprite table's colours.

Open items: see `game.md`, `play.md` and `disk.md` (places 8-10,
undecoded tables, sprite colour byte, the LXA routine's chip dependence,
other releases; catellite catches and left tunnels not exercised by a run;
blank frames around set-ups; known picture differences listed in play.md).

## Marble Madness (Amiga) milestones

Notes: `.analysis/marble_madness_amiga/` (`disk.md` boot chain, cipher,
protection and the reference check; `formats.md` file formats; `game.md`
program map, plan and open items). RetroReverse's earlier writeup of the
same disk is used as a guide; claims are checked against the image's code.

1. **Disk import** — done. ADF → AmigaDOS files (`src/amiga`) → c/zzz's
   cipher rebuilt (`import/loader_cipher.cpp`: key table, the Kickstart 1.x
   vector protection, keystream, the launcher's checksum of c/xxx, c/xxx's
   key) → `program.hunks` (347 hunks, recognised by SHA-256) plus 35 data
   files and the title picture. Checked in FS-UAE (KS 1.3): every code hunk
   equals the running original's memory, all 1,443 checked relocations
   right (`harness/memcheck.py`).
2. **Asset decoding** — started. Tile maps (tiles, map, palette), image
   libraries (sprites and scenery), title picture; launcher viewer and
   `marble_madness_assets export`. Next: Track files (height field,
   placements, display bands, scripts), sprite colour ramps, sound.
3. **Reference harness** — done for play: `harness/mmref.py` drives FS-UAE
   (GDB stub, KS 1.3): starts a race from the options screen, stops at
   every world update, records the marble records and globals, injects
   scripted joystick pushes, captures the window and the copper list on
   request. `marble_madness_replay lockstep` compares the rebuilt race.
4. **Play** — in progress (notes: `play.md`). Rebuilt from the code: the
   height map (encoder, dictionary, animated vertices), the ground probe,
   regions (activation, script interpreter, frame pictures and height
   patches, Practice's contact codes), the marble (input, speed cap,
   friction, slope, walls, edges, falling, landing, breaking, respawn,
   zones, goal, clocks, score), scroll, the shine, the view (playfield ring,
   colour bands, goal colours, region sprites, marble, bonus picture, score
   bar), the wall covers that hide the marble ($11EC42 / $1114F8), the
   respawn sparkles, Practice's drop bonus and its marker. Practice matches
   the original update for update in three recordings (roll, fall, respawn,
   rails, goal, finish; a drop bonus), cover lists included; its picture is
   pixel-identical at the captured moments, with the marble behind walls.
   Playable in the app (courses in order, steering option).
5. **Sound** — done for Practice (`src/marble_madness_amiga/sound`, notes in
   `play.md`): the course's sound table, the effect synthesiser (segments,
   envelopes, priorities, channel pairs), the 8-voice music player with the
   8SVX instrument, on a model of the four channels (`src/amiga/paula`).
   The audio.device requests equal the original's in recorded runs (all of
   them; one-blank shifts where the original's world update reaches a sound
   call before or after a vertical blank).
6. **Beginner** — done (2026-10-26): steelies, slinkies, the marble
   collisions, the drawbridge (and the marble behind it), the funnels, the
   ice, the shatter's broom. A recording from start to goal (both funnels,
   the ice, steelies and slinkies) equals the original update for update,
   creatures and regions included. The display list (drawing order of
   marbles, creatures and markers by depth) and the bridge's own masks
   are rebuilt and checked against captures.
7. **Intermediate** — done (2026-10-26): the acid puddles, the funnel,
   the wave down the long ramp (two routines the Track stores as 68000
   code, rebuilt), the dizzy surprise, the random-number generator. A
   recording from start to goal equals the original update for update,
   creatures, regions and covers included; the dissolve in the acid too.
8. **Aerial** — done (2026-10-26): the chutes, vacuums (with the capture),
   launcher, pistons, random region groups, zone sounds and the course's
   occlusions. Recordings from start to goal, of a capture and through the
   pistons equal the original update for update.
9. **Silly** — done (2026-10-26): the funnel up, the critters, the birds
   (eating the marble), the course's bands and playfield line. A recording
   from start to goal equals the original update for update.
10. **Ultimate** — done (2026-10-26): the run-up over the gap into the
   funnel, the moving goal area (four versions of the last 30 map rows
   rendered in turn, with height patches and cover sets), the colour
   cycling, the goal flags' draw order. Recordings from start to goal and
   of a fall in the goal area equal the original update for update,
   creatures, regions and covers included; the pictures at the goal are
   pixel-identical.
11. **Front end, one player** — done (2026-10-26): the options screen
   (pointer, difficulty, input device), each course's introduction
   counting its time in, the finish bonus, out of time and game over, the
   end after Ultimate (tally and bouncing marbles), the bar's clock flash.
   Checked against captures of the original's screens.
12. **The original's font** — done (2026-10-27): the Kickstart's topaz 9
   embedded like the C64 character ROM's glyphs (`amiga/topaz.cpp`, not
   covered by the MIT licence); texts and score bar pixel-checked against
   captures.
13. **Quick save and load** — done (2026-10-27): a copy of the whole game
   flow; checked to play on identically from saves at every stage.
14. **Crowd race** — done (2026-10-27, not the original game): seven
   computer-steered marbles from the course data (zone lines, height map,
   funnel portals from recordings), a look-ahead planner on race copies;
   pause-menu mode, spawn/despawn mid-race. Open items in play.md.
   Next: two players, mouse input.

## Mind Walker (Amiga) milestones

Notes: `.analysis/mindwalker_amiga/` (`disk.md` files, boot chain, the
module protocol, the reference machine and harness; `formats.md` data
formats; `game.md` stages, shared block, code map, plan and open items).
No outside material on this game was used (RetroReverse has none).

The game is seven compiled-C programs run one at a time by a small loader
(S set-up, T title/menus, A the physical level, B the tunnel, C the arena,
P the psyche screen, E errors). The state that crosses between them lives in
a 784-byte block the loader owns; the stage order follows a mode word
(T → A → B → C → P → A …). The worlds are generated from a seed (four preset
seeds and a clock seed), not stored.

1. **Disk import and assets** — done (2026-10-27). `import/disk_import`
   keeps the seven stage programs (recognised by SHA-256) and `MindB`,
   `MindF1-6`, `MindHighs`. `data/assets` decodes the title picture (HAM:
   data planes 0-3 and the modify-red plane, on T's palette; equal to the
   emulator's title), the six portraits, S's five shared image sets (16-pixel,
   2-plane frames with their frame orders) and icons, B's hands (colours
   8 + value, as B's blits make them), C's cells and images, the stage
   palettes, P's 32 words, the sound samples and waveforms of B, C and P,
   and the high-score table. Launcher viewer (31 pages) and
   `mindwalker_assets import|export` (PNG, WAV, summary).
2. **Reference harness** — done (2026-10-27): research boot disk
   (`mkboot.py`), FS-UAE on port 6861 with pid tracking (`run.sh`), module
   location and input injection (`mw.py`), the per-frame recorder
   (`mwrec.py`: every input read, state fields, screen dumps, hooks, module
   switches), dumps to pens and palettes (`rawpng.py`), and an oracle that
   runs A's own world routines in batches (`worldref.py`).
3. **Title stage** — done (2026-10-27; `title/`, `screen/`, `game/`): the
   title picture with its twinkle, the world menu (grid, bands, pillars and
   their animation, items, the edge-triggered click), the gadget bar as
   Intuition draws it, the new-game record (preset or New Game seed, stage
   from the slider, lives, bonus) and the loading picture on S's screen.
   Topaz 8 embedded in `amiga/topaz.cpp` (owner's decision, same terms as
   topaz 9). Checked with `mindwalker_replay title` on three recordings:
   every frame's state, the title, menu and loading screens pen for pen and
   colour for colour (S's title bar with Intuition's screen-depth gadgets,
   embedded in `amiga/intuition_imagery.cpp` on the owner's decision).
   The launcher plays it with the mouse (new `GameInput` pointer fields) and
   Intuition's pointer from the disk's preferences, then stops at the
   loading picture naming the generated world.
4. **World generator** — base layer done (2026-10-27; `world/`): seed →
   keys, terrain levels, start cell; both coordinate chains; platform cells.
   Equal to the original's routines for every chain coordinate in two
   worlds and 2,460 cells in one (`mindwalker_world check`;
   `.analysis/mindwalker_amiga/harness/regress.sh` replays all runs).
5. **Landscape (A)** — started (2026-10-27). The view builder
   (`landscape/view_cells`: the 4 × 4 cells in front of the walker, their
   terrain and object words and the view's 32 colours) equals the
   original's routine in 1,396 oracle calls across stages 0-35. The view's
   drawing is read (projection, cell order, kinds; notes "Drawing the
   view"). The view painter (`landscape/view_scene`, with the original's
   projection as a replaceable camera for a later free camera) draws the
   grid and the cells' bodies, faces and markers close to the original (1.5%
   of pens differ in the opening view; owner's decision: close is fine), and
   the game shows the opening view after the loading picture (held a fixed
   2 seconds: owner's decision). A's top bar (pause box, slider, lives,
   compass, emblem, Intuition's close gadget) equals the original's pens;
   the view's trees, grass, patterns, landmark stars and ramp poles are
   drawn (0.9% of pens differ). Walking is rebuilt (fall, steps, speed,
   frames, view shifts, height, visited map and points, beacons, compass
   turning; the walker sprite). The flying creatures are rebuilt
   (`landscape/creatures`: bands per view, appearing, flight, masking
   behind the scenery, tethers, catches, damage and lives); the walking
   recording now equals the original for all its 808 frames. Next:
   zapping, view creatures and finds, the walker's mask, the catch
   picture, the status bar; then B, C, P, sound, saves, high scores.

Open: what each shared image set and icon is used for and its colours;
sample playback periods and triggers; A's world model beyond the generator;
P's rules; T's high scores, command screen (save/load), pause box and end
screens; the slider drag (modelled, not compared); the release's video
standard (50 Hz assumed); the shared fields
marked as hypotheses in `game.md`.

## Wings of Fury (Amiga) milestones

Notes: `.analysis/wings_of_fury_amiga/` (`disk.md` disk, crack, program
structure, research layout; `formats.md` data formats). No outside material
on this game was used (RetroReverse has none).

The image is a cracked copy: the cracker's banner in the startup sequence,
and `wings` no longer calls the manual-word check (the routine and its
33-entry table are still there, `$10F41A`). Everything else is the game's
own data, loaded by name with dos.library: `wings` (compiled C front end +
hand-written 68000 game, A4-relative globals), the music player `songplay`
and songs `wofsongs` (both with their symbols), IFF pictures, "Rpck"-packed
"PPkc" shape libraries, fifteen maps and eight samples.

1. **Done 2026-10-27: import and assets.** `src/wings_of_fury_amiga`
   (namespace `wings`): import (release by `wings` SHA-256, every data file by
   SHA-256), Rpck unpacker, PPkc shape decoder (blitter rules of `$110B0C`:
   clear/set plane masks, stored planes with target masks, OR mask), pictures,
   palettes, maps (raw words), samples, rank names, story text, high scores;
   launcher previews; `wings_of_fury_assets import|export`. Checked: all
   files decode; shape sheets and pictures look right in the day palette.
2. **Done 2026-10-27: reference machine.** FS-UAE harness on port 6862
   (`harness/`: mkboot hold, attach, input injection at the game's reads,
   play-view BitMap grab); disk.md "Reference machine". Later: drive runs
   through the game's own recorder (argument → record, `wofdemo` → playback).
3. **Done 2026-10-27: map scenery.** Column words (draw bit, drop, scenery
   index, surface), the scenery table from `wings`' name lists, carrier
   position; map previews in the launcher and export.
4. **Done 2026-10-27: scenery pixel check.** Native `scenery_view` equals the
   original's play view at 19 camera positions of map a (camera pinned at
   the frame start, `harness/viewref.py`, `tools/viewcheck.sh`), apart from
   what other drawers put on top (plane, flags, deck animation); ship columns
   follow the ship's bob + vertical scroll. formats.md has the details.
5. **Done 2026-10-27: backdrop.** `game/scenery` draws sky, scenery columns
   with the map specials (US flag, deck crew, enemy flags), ships' bob, waves
   and island sand from game state; equal to the original as colour indices
   over the whole view on maps a, d and k except for objects (plane, parked
   planes, elevator, splashes) and the weapon-menu box. Harness reads the
   finished buffer of the double-buffered view.
6. **Done 2026-10-27: player and carrier deck pictures.** `game/player_view`
   (plane, parts, propeller XOR, mirrored shapes, clip at the deck; parked
   planes, elevator); exact against grabs on the elevator, on deck, taxiing,
   in flight (poked states), with the ground line moved. game.md has the
   frame order, player picture and input.
7. Next: take-off/engine control (open), the 1/8-scale view, enemy planes
   and other objects, index-5 special, night, copper palette split; then the
   front end (story scroller, pictures, rank selection, high scores),
   music (`songplay` rewrite), then flight, objects and missions.

Open: taking off in the reference machine (the engine start path, game.md);
the player's -$5BE0/-$5BE4 parts, upset and facing right not yet seen; the
debug key mode; index-5 special ($104E18), the `$114/$115` pairs, the deck phase
tables behind the crew poses; ship bob other than 0 not compared; night maps,
the 1/8-scale view and the copper palette split (rows 151+); maps other than
a, d and k not compared; stick direction codes;
`newarmyfont` and how the two fonts are used; sample periods; the shape
offset/sheet fields (hypotheses); eighth rank-menu option (`$108B96`);
where the retail game called the manual check; a second (retail) release
entry for the importer; video standard (PAL assumed).

## Launcher visual exploration (2026-10-26)

Two AI-generated design proposals and their exact prompts are saved in
`design/launcher-concepts/NOTES.md`. These are concepts only, not implemented
UI or recovered game artwork. The user selected the collector's shelf direction;
the planned-game artwork pass is documented in `design/planned-covers/NOTES.md`.

Implemented: two-column scrolling/filterable SDL library, all eight standalone
screenprinted covers, redesigned details and pause/options pages, bundled portable
Lato typography, selection-aware scrolling and full option-description view.
The former clipped fifth Amiga card is reachable by keyboard, pointer paging,
mouse wheel and the shared gamepad navigation events. No macOS UI/font APIs.
Source licences and artwork provenance ship in `resources/`.

Validation: build/package, synthetic navigation tests and visual/live checks are
recorded in `.analysis/shell-design/validation/REPORT.md`. Open: Android packaging
and device validation, and a physical-gamepad interaction check on the new UI.
The shell currently retains the existing 960x640 letterboxed logical canvas.

## Interactive field guide: Bruce Lee enemy decisions (2026-10-26)

First pure-SDL prototype is available from Bruce Lee Details -> Lessons ->
Behind the enemy decisions, after importing the user's disk. It runs a separate World using the
imported assets, with resettable approach, close and airborne-target scenarios.
Harmless-enemy practice settings, no save writes, silent simulation; movement
works with the usual controls, while player attacks are disabled. Pause, one
logic-pass step, quarter speed, enemy selection and clickable decision gates
expose the mechanics without displaying implementation code. Current move
labels and directional range bands accompany a last-decision snapshot recorded
inside the actor rule. Bands indicate attack choice, not collision hitboxes.

Evidence boundary: the explanations describe the current reconstruction in
Actors::think/wake_enemies and the existing ROM-derived recovery notes. They
are not a new equivalence claim about the original. Original-game validation
was not rerun for this presentation feature; the added telemetry only records
branch outcomes and does not change decisions.

Validation: shell navigation checks; imported-data chapter checks exercise the
Ninja and Yamo attack boundaries, facing and height gates, pause/step behavior,
and render the running chapter. Captures were visually reviewed.

Open follow-ups: broader interactive chapters, original-ROM comparison of
the prepared scenarios, narration/audio, touch/gamepad-only chapter controls,
and more detailed combat hit detection visualizations. This prototype uses a
960x640 letterboxed SDL canvas and keyboard/mouse chapter controls.

## Lesson browser and room-effects chapter (2026-10-26)

Bruce Lee Details now opens a selectable lesson list (Up/Down, wheel, page
keys, Enter/click). Extracted assets remain a separate view. I no longer
launches a lesson from Details. Back from a lesson preserves the list selection.
The list scrolls when entries exceed its three visible rows; lesson rendering
uses a common interface that does not require a game simulation.

Second chapter: A floor opens. The room-2 experiment changes practice progress
for rooms 1/2/3/7 and runs the same room-effect update as normal play, without
actors, combat or saves. Required: rooms 1-3 cleared and room 7 not cleared.
Four updates on alternate passes apply the imported character patterns to the
playfield. Actual changed floor cells are highlighted. Changing prerequisites
resets the demonstration. The How it works tab is a scrollable illustrated
article with comparison images and a rule/data table; no source listings.

Evidence: current reconstructed setup_room/script_room_1, their existing
ROM-recovery annotations, and the imported patterns. No new original-ROM
equivalence claim. Checks cover missing prerequisites, room-7 blocking, the
four-stage completion and changed playfield, lesson entry/exit, article
scrolling, and existing enemy-decision checks. Screenshots reviewed.

Open: compare these fabricated room-effect scenarios against original-ROM
execution; expand the reusable article authoring format beyond this prototype.

## Third lesson: Building a background (2026-10-26)

A read-only illustrated article in Bruce Lee's lesson list. All room figures,
tile crops and repeat/literal examples come from the imported data. The existing
map decoder now records run boundaries and encoded byte lengths as observation
metadata; decoding and gameplay are unchanged. Room 1 measures 272 bytes versus
the 440-byte expanded map in the tested import. The table includes all 20
decoded rooms and explicitly records extra cells produced by final runs.
Counts include control bytes, payload and the end marker, excluding shared
character sets, room headers, colors, lanterns and exits. The article shows
character-row doubling and paired halves, actual repeat/literal runs, palette
versus room-color rendering, and a proportional storage chart. No simulation
or progress writes; only scrolling and returning to Lessons.

Validation: each map's recorded run sizes sum to its measured byte count; each
repeat matches the expanded map; output lengths include spill cells; the final
count points to the imported end marker. Article captures reviewed throughout
the scroll range, and third-lesson entry checked through the launcher. Existing
shell and imported-data lesson checks pass. Original execution comparison is
not newly claimed; explanations describe the existing reconstructed decoder.

## Marble Madness lessons (2026-10-27)

Added three pure-SDL chapters to the game-specific lesson browser: the height
field/physics/occlusion, black-marble decisions, and Silly Race's changed rules.
Illustrations read imported terrain, waypoint links, sprite masks and the pen
bitmap. Separate resettable labs use the real Race/View, paused initially with
the clock frozen. Silly's catch sample demonstrates +500 points and +3 clock
seconds; its slope comparison calls both force branches on identical terrain.

Evidence, scenario construction, validation and limits are documented in
`src/marble_madness_amiga/LESSONS.md`. Open: original-execution comparisons for
these fabricated setups; a verified edge-lure replay; dedicated funnel/bird and
two-player experiments. No original-equivalence claim is made for the new seeds.
