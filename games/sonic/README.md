# BlueStreak

An open-source recreation of classic 2D platforming mechanics, compatible with
data extracted from Sonic the Hedgehog (Game Gear, Genesis, Sega CD, GBA, DS).
Requires original ROM or disc dumps.

BlueStreak contains no game data. It reads the levels, graphics and music from
a ROM or disc dump you supply, and runs its own C++ recreation of the game logic
natively, with no emulator.

Sonic the Hedgehog is a trademark of SEGA. BlueStreak is not affiliated with or
endorsed by SEGA; game names are used only to describe which data it can read.

## Status (version 0.17.0)

| Game (dump) | State |
| --- | --- |
| *Sonic the Hedgehog*, Game Gear (Japan/USA, CRC32 `3E31CB8C`) | Complete: all six zones, linked rooms, special stages, the final boss, both endings and credits, death, game over and continue |
| *Sonic the Hedgehog 2*, Game Gear (World, CRC32 `95A18EC7`) | Complete: intro, title and attract demos, all seven zones with their bosses, hang glider, mine carts, pipes, swing poles, water and rain, continue and game over, zone select, both endings with credits |
| *Sonic Triple Trouble*, Game Gear (USA/Europe/Brazil, CRC32 `D23A2A93`) | Playable start to finish with Sonic or Tails: intro, title, attract demos, character select, all six zones with their bosses and zone ends, the train ride, the board and jet board, the special stages (on foot and the flights), the rival's scenes, the final machine, both endings with credits, continue and game over, and the title's sound test. A few rarely reached paths are not yet verified (see *Known open items*) |
| *Sonic Advance*, Game Boy Advance (USA, CRC32 `63F70FD8`) | Playable as Sonic from power-on to the ending (verified act by act and boss by boss in scripted runs, not yet in one continuous playthrough): SEGA and Sonic Team logos, intro, title, attract demos, character select, Neo Green Hill, Secret Base, Casino Paradise, Ice Paradise, Angel Island, Egg Rocket, Cosmic Angel and X-Zone with all their bosses, the special stages, the ending and staff roll, game over. The Moon Zone, Super Sonic and the true ending are reconstructed too, but normal play cannot reach them yet. Tails, Knuckles and Amy, the act select, the continue screen, time over, saves, options and the multiplayer and time attack modes are not reconstructed yet (see *Known open items*) |

Version 0.16.0 moves the code Sonic 2 and Triple Trouble run the same way
(asset import, the picture model, the sound driver, the object-script format,
the widescreen view) into one shared engine, `src/gg_engine`; both games
behave as before, except that Sonic 2's sound driver now follows the
cartridge's stereo rule when a sound effect ends (10 frames of the attract
demo's audio change). Triple Trouble also gains paths a random-input player
stopped at in 0.15.0 (the title's sound test, the shell ride, Meta
Junglira's barrels, breathing a big bubble, Tails floating up) and keeps the
widescreen arena wall across quick saves. Triple Trouble quick saves from
0.15.0 cannot be loaded; Sonic 2's still can.

The other library entries (Genesis, Sega CD, the other Game Boy Advance games
and DS) are shown as *planned*. Built and tested on macOS (Apple silicon) only
so far; the code uses SDL 3 and standard C++20, but Windows and Linux builds
have not been tried yet.

## Shell design

The speed archive shell uses an original swept-ribbon logo, numbered geometric
covers, generation accents and bundled Lato typography. Library navigation,
launch transitions, level selection, sound test and in-game menus are rendered
entirely through SDL. Font rendering uses bundled stb_truetype; no native UI or
system-font API is needed. Run with `--reduced-motion` to remove decorative
motion and screen sweeps. The gameplay clock is unchanged.

See [design/SPEED-ARCHIVE.md](design/SPEED-ARCHIVE.md) for design notes, logo
provenance and validation. Fonts and their licenses ship in `resources/`;
other-platform packagers should preserve those relative paths in SDL assets.

## Features

- A library with an integrated ROM importer. A dump is identified by size and
  SHA-256, extracted to a staging folder, validated, then installed in one
  step. The dump itself is not kept.
- A pause menu with quick save/load (versioned, explicit little-endian format,
  one slot per game), a level selector (every act, the special stages and the
  endings) and a sound test for each Game Gear game. Sonic Advance starts from
  power-on only and has no quick saves yet.
- Optional enhancements, each switched independently and off by default; each
  game lists only the ones it supports:

  | Enhancement | Sonic 1 | Sonic 2 | Triple Trouble |
  | --- | --- | --- | --- |
  | Widescreen expanded view (10:9 up to 21:9) | yes | yes | yes |
  | Game Gear LCD screen look | yes | yes | yes |
  | Water: refraction, splashes, waterfall mist | yes | | |
  | Water: refraction, translucent water, surface waves, splashes, rain on the camera lens | | yes | |
  | Translucent shield | yes | | |
  | Scattered rings that bounce | yes | | |

  The expanded view never shows more than the Game Gear screen could show
  somewhere in the act. Objects are active and drawn across the whole view, and
  a force field marks a boss arena's invisible walls where they fall inside
  it. With every enhancement off, each game behaves like the original.
  Sonic Advance has no enhancements yet.

## Building

You need CMake 3.24 or later, Ninja, a C++20 compiler and SDL 3.2 (`brew install sdl3 cmake ninja`).

```bash
./build.sh
```

```bash
open build/BlueStreak.app
```

Or double-click `Play BlueStreak.command`, which builds on first use. Imported
data, settings and saves are stored in
`~/Library/Application Support/NativeReconstruction/BlueStreak/`.
To use a separate data folder, or to import a dump while the app starts, run
`bluestreak --data-dir DIRECTORY --import ROM`.

## Playing

Start the app and choose a game in the library (Game Gear: 8-bit, Game Boy
Advance: 32-bit). Selecting it asks for your ROM dump (or drop the file onto
the window). The dump is identified by its checksum, its data is extracted into
the app's data folder, and the ROM itself is not kept. Then choose the full
game or a single act (Game Gear games).

| Action | Keyboard | Gamepad |
| --- | --- | --- |
| Move | Arrow keys / WASD | D-pad / left stick |
| Jump (Game Gear: either button; GBA: A) | Space / Z | South button |
| Game Gear: jump; GBA: B (Sonic's attacks) | X | East button |
| Start (title screens; GBA: also the game's own pause) | Enter | Start |
| Pause menu (save, load, enhancements) | Esc; on Game Gear also Enter while playing | Guide; on Game Gear also Start while playing |
| Quick save / quick load | Cmd+S / Cmd+L (Ctrl+S / Ctrl+L on Windows and Linux) | Pause menu |
| Restart the act (Sonic Advance: the game) | R | |
| Mute; sound test from the library | M | |
| Switch hardware generation in the library | Q / E | Shoulder buttons |

## How the games were reconstructed

All four games were recovered the same way, from the supplied ROM only:

1. **Static analysis of the ROM.** For the Game Gear games, a coverage-driven
   Z80 disassembly (roots from execution coverage, the interrupt vectors and
   the object script tables) located every routine and table the game
   reaches. For Sonic Advance, the ARM/Thumb code was disassembled and
   decompiled with general-purpose tools (LLVM, Ghidra), seeded with the call
   targets seen in execution.
2. **Translation into C++.** Each routine was rewritten by hand, keeping the
   original control flow, update order and arithmetic (8/16-bit wrap-around,
   fixed-point speeds, table lookups). Tables, object scripts, level layouts,
   collision profiles, palettes and music are not retyped: the importer
   extracts them from the ROM into the asset pack, and the code reads them
   from there.
3. **Verification against the running ROM.** The original was executed in a
   tracer and compared with the reconstruction frame by frame: work RAM,
   video memory, palette and sound chip writes in recorded play, demo input,
   generated input (bots and exploration searches) and poked test states.
   For Sonic Advance, the comparison covers video memory, colours, the
   sprite table, the display settings, the sound engine's mix buffer and
   sound registers, and the rendered picture. Its objects were checked by
   placing the player next to the first instance of every object kind in
   every act, and its bosses, endings and special stages by scripted runs.
4. **Rewrite for readability.** For Sonic 2 and Triple Trouble the verified
   translation was then rewritten in a structured style (state grouped by
   subject, named object types, routines and sounds, a typed sound driver, a
   picture made from a display model) and compared with the verified version
   frame for frame. Triple Trouble and Sonic Advance were written in that
   style from the start.

Comments cite the original addresses, for example `4B63` (Z80 code in the ROM)
and `D415` (work RAM), or `0803D6F0` (Thumb code in the GBA cartridge) and
`03005A20` (GBA work RAM), so each routine can be traced back to the code it
came from. Quirks of the original (bugs, out-of-range reads, odd timings) are kept
and labelled as such.

### Ported from the original code

These parts are reconstructions of the cartridge's own routines and data
tables, checked against its execution:

- **Game logic:** Sonic's (and Tails') physics and states, terrain collision
  and special blocks, every placed object, enemy and boss, rings and items,
  the camera, the act flow (title cards, signs, tallies, continue, game over),
  special stages and endings. In Sonic Advance also the game's task scheduler
  (the order its processes run and are created in) and its sprite animation
  scripts, which run from the cartridge's data.
- **Front end:** SEGA logo, intro, title, character and zone select, attract
  demos (driven by the demo input stored in the ROM), credits.
- **Sound:** each game's music and effect sequencer, and the sampled SEGA
  voice, feeding a model of the Game Gear's PSG sound chip. Sonic Advance's
  sound engine (four music players, sampled channels mixed in software, the
  tone generators) is reconstructed from the cartridge's own engine.
- **Picture:** tile, sprite and palette handling, palette fades and cycles,
  the water line's palette change and line-scrolled bands. In Sonic 2,
  Triple Trouble and Sonic Advance the picture is built from a model of the
  video chip's state as the game writes it (for the GBA: tile layers, rotated
  layers, windows, blending and per-line effects).
- **Random numbers:** the games' own generators (for example Sonic 1's `0688`)
  where the game uses one.

### Measured or reconstructed from emulator inspection

Some behaviour depends on hardware timing rather than on game code, and was
modelled from what the running ROM showed:

- **Loading pauses** (Sonic 2, Triple Trouble): a cycle-count model of the
  original's decompression and video memory fills, with costs measured per
  routine in the tracer. It is close but not exact; pauses can be a few frames
  off.
- **Water palette band** (Sonic 1): the line interrupt streams the palette
  over nine scanlines below the surface; the per-line mix was measured and is
  reproduced as a band.
- **Water line timing** (Sonic 2, Triple Trouble): the line interrupt is
  applied after the frame's program (hypothesis; no game state depends on
  its exact timing).
- **The Z80 refresh register** (`LD A,R`), which a few routines read as a
  random source (Sonic 2's rain and some enemies, Triple Trouble's final
  machine): replaced by a small deterministic generator, so these choices
  differ from the original's.
- **Region detection** (Sonic 1): the export/Japan port bit was taken from
  the running cartridge.
- **Fast tally** (Sonic 2): the step rate while a button is held is an
  estimate (3 steps per frame).
- **Terrain reads above the map's first row** (Sonic 2, Triple Trouble): the
  original reads cartridge bytes below the map's address; modelled so,
  partly unverified.
- **GBA hardware** (Sonic Advance): the video renderer, the sound hardware
  (tone generators, the direct sound channels and their mixing) and the BIOS
  services the game calls (decompression, arctangent) follow the public
  hardware documentation (GBATEK). The game's register writes and its sound
  mix are verified; the analog output levels are not compared with real
  hardware.
- **Water timing** (Sonic Advance): when the water line's interrupt and
  colour change take effect within a frame was found by tracing the running
  game, and is modelled so.
- **Leftover memory** (Sonic Advance): a few routines read values a previous
  task left behind (the HUD's flashing zero, the special stage counter's
  blink, the ending's streaks, a boulder's angle, a capsule ring's flag).
  Where a traced run showed where the value comes from, the port reproduces
  that; otherwise it starts from zero (the boulder, the ending's streaks).
  Runs that were not traced can differ.

### Deliberate differences from the hardware

- **No slowdown.** The original drops frames in busy scenes; BlueStreak runs
  every frame at full speed. Attract demos read their input per frame, so they
  play out slightly differently.
- **No sprite dropout.** The video chip's eight-sprites-per-line limit is
  lifted.
- **No stale background tiles.** The background is drawn from the level map,
  so the original's streaming lag behind a fast camera (Sonic 2's pipes, a
  few scripted pans) does not show.
- **Level starts from the shell** go straight to play, without the title
  card's fade-in or loading pause; a game started normally keeps them.
- **No overrun frames** (Sonic Advance). Where the original's work runs past
  the start of the vertical blank (an act's first frame, the save write, some
  busy frames), the hardware shows one picture that is partly old or a line
  off; BlueStreak shows the finished picture. Video memory the game writes
  at once during its set-up shows from the next picture.

## Known open items

- **Sonic 2:** path platforms (21), the moving swing pole (2A) and the
  falling block (2C) were checked separately but not in recorded lockstep
  play, nor was the good ending's final scene. The unused enemy types 3D/3E
  are not reconstructed (never placed by the game). In the expanded view,
  Aqua Lake 2's surface shimmer covers only the LCD's width, and the Under
  Ground and Crystal Egg boss arenas were checked by reading the code only.
  A few sound driver commands that no song uses stop the game with a message.
- **Triple Trouble:** the third and fifth special stages are verified from
  near their tops through the guardian fights, not through a full climb; in
  Tidal Plant acts 1 and 2 no route through the row of engraved blocks was
  found in the original, so those acts are verified only from past it. Should
  the game reach a routine that is not reconstructed, it stops with a message
  instead of guessing. The water enhancement is not available yet, and the
  special stages, flights, the rival's zone and the train ride are not
  widened.
- **Sonic Advance:**
  - Not reconstructed yet, so the game stops with a message when it gets
    there: the act select (the character select goes there once an act has
    been cleared, so a new game after a game over or the ending in the same
    run stops), the continue screen (a game over with continues from the
    special stages), the time over (ten minutes in one act), Tails, Knuckles
    and Amy, the main menu's other entries (VS and its chao hunt, time
    attack, options, Tiny Chao Garden), and the language select (the game
    runs in English).
  - The cartridge's save is not kept: every start is a first start with no
    emeralds. The Moon Zone, Super Sonic and the true ending are
    reconstructed and verified in scripted runs, but normal play cannot reach
    them: the original requires all seven emeralds and every character to
    have cleared X-Zone.
  - No quick saves, level selector, sound test or enhancements yet.
  - Objects were verified around the first instance of each kind in each act
    and in the recorded runs; branches no run reached (some crushes, riders,
    the partner and multiplayer paths) are written from the code only. Of the
    special stages only the one reached from Casino Paradise act 1 was
    played; the others run on the same code with their own course data.
  - Known single-picture differences in the verified runs: a few pixels at
    the bottom of the view while Ice Paradise's view shakes (cause not
    traced), a few sprite pixels that the hardware's per-line sprite limit
    probably hides, and one title picture where the original shows the logo
    one pixel off.
- **All games:** loading pauses are modelled, not exact; randomness from the
  refresh register differs (see above).
- **Platforms:** Windows and Linux builds have not been tried.
- **Other games:** the Genesis, Sega CD, Sonic Advance 2 and 3 and DS entries
  are planned, not playable.

## Source layout

```
src/
  app/              The shell: main loop, library, menus, quick-save files,
                    the registry of games (titles.cpp)
  platform/         Host services: SDL window/input/audio/GPU, RGBA image, SHA-256
  gamegear/         Game Gear hardware shared by the GG games: VDP tiles and
                    palettes, tile screens, the PSG sound chip, the LCD screen
                    filter, the widescreen view size and arena force field,
                    water splashes and droplets, lens rain, the sound test player
  gg_engine/        The engine Sonic 2 and Triple Trouble share; each game
                    describes its revision of a part with a small profile
    data/             Cartridge identity, asset pack (data bytes, tile
                      decompression), import
    video/            The picture model (tile art, background map, sprites,
                      colours) and its 160x144 render
    audio/            The sound driver (DriverProfile), the SEGA voice
                      (VoiceProfile), the sound test player
    objects/          The object-script bytecode decoder (ScriptFormat)
    enhancements/     Widescreen view geometry and the arena force field
    flow.hpp          Frame-waiting coroutines for the program's flow
  sonic1_gg/        Sonic the Hedgehog (Game Gear)
    module.cpp        The game module (GameModule) for this data
    catalog.hpp       Level selector and sound test entries
    data/             ROM decoding, asset pack files, import
    game/             The act: Sonic, terrain, objects, enemies, bosses
    frontend/         Logo, title, map, results, game over, ending; the Session
    audio/            The music/effect sequencer and the SEGA voice sample
    render/           Drawing a gameplay frame
    enhancements/     Widescreen, water, rings (optional, presentation only)
    save/             Quick-save serialization
  sonic2_gg/        Sonic the Hedgehog 2 (Game Gear)
    module.cpp, catalog.hpp
    data/             ROM check, tile decoding, asset pack files, import
    game/             State, main loop, Sonic, terrain, objects (scripts, one
                      file per zone and boss), act flow, the screen, ending
    frontend/         Boot, SEGA logo, intro, title, attract demos, zone select
    audio/            Its sound driver and SEGA voice profiles
    enhancements/     Widescreen rules (inert at the original view), water
    render/           The expanded and enhanced view
  triple_trouble_gg/ Sonic Triple Trouble (Game Gear)
    module.cpp, catalog.hpp
    data/             ROM check, asset pack files, import
    game/             State, frame services, player and physics, terrain,
                      objects (one file per subject and boss), special stages,
                      credits, quick saves
    frontend/         The program's modes: logo, intro, title, select, play,
                      tally, flights, demos, ending
    audio/            Its sound driver and SEGA voice profiles
    enhancements/     Widescreen rules (inert at the original view)
    render/           The expanded view
  gba/              Game Boy Advance hardware shared by GBA games: the video
                    renderer (tile and rotated layers, sprites, windows,
                    blending, per-line effects), the sound hardware, the BIOS
                    services (decompression, arctangent)
  sonic_advance_gba/ Sonic Advance (Game Boy Advance)
    module.cpp        The game module (GameModule) for this data
    data/             ROM check, asset pack (all cartridge data, no code), import
    game/             The game: task scheduler, video hand-over, backgrounds,
                      sprites, the player, collision, camera, objects (one
                      file per zone and subject), bosses, special stages,
                      results, game over, endings
    frontend/         SEGA and Sonic Team logos, intro, title, character select
    audio/            The sound engine (music players and the software mixer)
tools/              macOS packaging and embedding the enhancement art
art/logo.png        Transparent export of the SDL-rendered BlueStreak logo
art/enhancements/   Original art for the enhancements (water droplet, mist curls)
```

Future games go in their own folders next to the existing ones. Game Gear
titles reuse `gamegear/` (and, from the same lineage as Sonic 2 and Triple
Trouble, `gg_engine/`) and GBA titles `gba/`; Genesis, Sega CD and DS titles
get their own hardware layers. The shell sees game modules only through
`app/game_module.hpp`.

### Where to find the game mechanics

Sonic 1 (`src/sonic1_gg/`):

| Topic | File |
| --- | --- |
| Update order, camera, palette effects | `game/game.cpp` (`Game::tick`, `update_camera`) |
| Running, rolling, jumping, gravity, speed limits | `game/sonic.cpp` (`Sonic::update`, `roll`) |
| Rings, damage, death, animation | `game/sonic.cpp` |
| Terrain collision (block contours, sensors) | `game/terrain.cpp` (`Game::integrate_body`) |
| Springs, spikes, ramps, conveyors, currents | `game/terrain.cpp` (`Sonic::terrain_actions`) |
| Object activation and the type -> handler table | `game/objects.cpp` |
| Monitors, goal sign, common enemies and platforms | `game/common_objects.cpp` |
| Boss hits and defeat, capsule, random numbers | `game/boss.cpp` |
| Zone objects and bosses (one function per type) | `game/bridge.cpp`, `jungle.cpp`, `labyrinth.cpp`, `scrap_brain_sky_base.cpp`, `final_boss.cpp` |
| Special stage bumpers, flippers, clock | `game/special_stage.cpp` |
| State: Sonic, objects, the act | `game/sonic.hpp`, `game/body.hpp`, `game/game.hpp`, `game/object_types.hpp` |
| Names of the ROM tables (scripts, paths, sprite layouts) | `game/rom_tables.hpp` |

Sonic 2 (`src/sonic2_gg/game/`): the state is grouped by subject
(`state.hpp`), objects run scripts decoded per type and state (`scripts.hpp`)
whose routines are named (`routines.hpp`, `dispatch.cpp`), and the picture is
made from the display state, the act's background from the level map
(`display.hpp`, `map.cpp`):

| Topic | File |
| --- | --- |
| Main loop, per-frame update, pause, act start | `main_loop.cpp` |
| Frame interrupt, controller, fades, water line | `interrupt.cpp` |
| Sonic's states and physics | `player.cpp`, `player_support.cpp`, `glider.cpp`, `pipes.cpp` |
| Terrain collision and special blocks | `terrain.cpp` |
| Object scripts, spawning, off-screen removal | `objects.cpp`, `spawner.cpp`, `object_api.cpp` |
| Common objects (monitors, sign, emeralds, platforms) | `objects_common.cpp`, `platforms.cpp` |
| Zone objects | `underground.cpp`, `sky_high.cpp`, `aqua_lake.cpp`, `gimmick.cpp`, `crystal_egg.cpp` |
| Bosses, capsule | `*_boss.cpp`, `final_boss.cpp`, `capsule.cpp` |
| Tally, continue, game over, ending | `act_end.cpp`, `game_over.cpp`, `ending.cpp` |
| The screen (tile art, background map, sprites, colours) | `display.hpp`, `video.cpp`, `sprites.cpp`, `palettes.cpp` |
| Names: object types, sounds, ROM tables | `object_types.hpp`, `../audio/sounds.hpp`, `rom_tables.hpp` |

Triple Trouble (`src/triple_trouble_gg/`) follows Sonic 2's structure. The
program runs as a set of modes (`frontend/program.cpp`), each frame starts
with the frame services (`game/interrupt.cpp`):

| Topic | File |
| --- | --- |
| Player states and physics (Sonic and Tails) | `game/player.cpp`, `game/player_states.cpp`, `game/physics.cpp` |
| Terrain and object collision, damage | `game/terrain.cpp`, `game/collision.cpp`, `game/damage.cpp` |
| Object scripts and the routine table | `game/scripts.cpp`, `game/routine_table.cpp`, `game/dispatch.cpp`, `game/objects.cpp` |
| Rings, item boxes, power-ups, springs, signs | `game/rings.cpp`, `game/item_box.cpp`, `game/power_ups.cpp`, `game/springs.cpp`, `game/signpost.cpp` |
| Enemies, platforms, paths, barrels, boards, the train | `game/enemies.cpp`, `game/platforms.cpp`, `game/paths.cpp`, `game/barrel.cpp`, `game/board.cpp`, `game/jet.cpp`, `game/train.cpp` |
| Bosses and the rival | `game/boss_*.cpp`, `game/rival.cpp`, `game/destroyer_end.cpp` |
| Special stages and flights | `game/special.cpp`, `game/special_guardians.cpp`, `frontend/flight.cpp`, `game/flight_objects.cpp` |
| Loading time model | `game/timing.cpp` |

Sonic Advance (`src/sonic_advance_gba/game/`): the game runs as processes on
a task scheduler, like the cartridge, and hands each frame's picture to the
display in the vertical blank:

| Topic | File |
| --- | --- |
| Frame order, processes and their creation order | `game.cpp`, `scheduler.cpp` |
| The picture's hand-over, backgrounds, sprites and animation scripts | `video.cpp`, `background.cpp`, `sprite.cpp`, `graphics.cpp` |
| The player: running, rolling, jumping, spin dash, attacks, hurt and death | `player.cpp`, `player_effects.cpp` |
| Terrain collision (height maps, angles) and solid objects | `collision.cpp`, `solid.cpp` |
| Camera and the act's layers | `camera.cpp` |
| Act start and restart, clock, death | `stage.cpp`, `title_card.cpp`, `hud.cpp` |
| Object layouts and the spawner | `objects.cpp` |
| Rings, item boxes, springs, common objects, enemies | `rings.cpp`, `item_box.cpp`, `interactive.cpp`, `enemies.cpp`, `destruction.cpp` |
| Zone objects | `secret_base.cpp`, `casino_paradise.cpp`, `ice_paradise.cpp`, `water.cpp`, `angel_island.cpp`, `egg_rocket.cpp`, `x_zone.cpp`, `moon_zone.cpp` |
| Bosses, capsule, results | `boss.cpp`, `boss_parts.hpp`, `results.cpp` |
| Special stages, game over, endings | `special_stage.cpp`, `game_over.cpp`, `ending.cpp` |
| Sound requests and the sound engine | `sound.cpp`, `../audio/sound_engine.cpp` |

The code style is defined by `.clang-format`.

## Source constraint

Game-specific findings were derived only from the supplied ROMs and their
execution, using general-purpose tools and hardware documentation. Before that
restriction was set, there was brief exposure to an existing Sonic 1
disassembly. Those files were removed, and findings were re-checked against
the ROM. The model that did the work may also have seen public material about
these games during its training; no such material was consulted, and every
claim was taken from the ROM's code and data and checked in execution. This
is therefore not a claim of clean-room independence.

## License

BlueStreak's source code and its original art (`art/`, `design/`) are
released under the [MIT License](LICENSE): use, change and reuse any part of it.

Bundled third-party files keep their own licenses: the Lato fonts
(`resources/fonts/`) are under the SIL Open Font License
(`resources/fonts/OFL.txt`), and `src/third_party/stb_truetype.h` is public
domain or MIT (`resources/licenses/stb_truetype.txt`). Their sources are listed
in `resources/licenses/SOURCES.md`.

The license does not cover game data. ROM and disc dumps, and anything extracted
from them, remain the property of their owners; do not add them to this
repository. The macOS app bundle includes SDL 3, which has its own zlib license
(copied into the bundle by `tools/package_macos.py`).
