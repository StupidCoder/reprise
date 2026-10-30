# DoubleOCheese

An open-source C++ recreation of a mouse-and-cheese path-drawing stealth game for iPhone,
compatible with the data of *Spy Mouse* (iOS, version 1.1.1). It runs natively on macOS, with
no emulator, as part of Reprise (this folder, `games/spy_mouse_ios`, through `src/embed`) and as
its own small app; Android is planned.

Version note (2026-10-28): the rules were reconstructed and checked against version 1.0.5 (the
Valentine's Day edition). The port now reads version 1.1.1, which has the regular pictures; its
program tables, sound and particle numbering, text ids and GUI file format were followed, while
the comparison of 1.1.1's changed rules with recorded runs is still in progress.

DoubleOCheese contains no game data. It reads the levels, pictures, text and sound from the
`.ipa` of your own copy and runs its own reconstruction of the game's rules. Scripted parts of
the original (cutscenes, dialogue, animation timings, menu layouts) are played from the game's
own files.

*Spy Mouse* and its characters belong to their respective owners. DoubleOCheese is not
affiliated with or endorsed by them; the game's name is used only to describe which data it
can read.

## Status (version 0.3.1)

Worlds 1, 2 and 3 play from the world map to the end of each world's boss fight:

- the world map: the mouse walking the roads between level pads, new levels revealed, level
  goals and the Play bubble, the move on to the next world, teleports and world pads;
- all of world 1's and world 2's levels: drawing paths, cheese and the trail, cats (patrols,
  chases, catching the mouse; black cats and blind cats), hypnotising TVs, hiding places, wall
  holes and safe holes, floors and doors, cars and butterflies, clockwork mice and mouse traps,
  teacups, balloons, speed items and bonus pickups, tutorials and pop-ups (including Kiska's
  offer);
- the three boss chases (the scrolling levels) and the three boss fights with their cutscenes:
  the droid of world 1, world 2's Spitter with its thrown cutlery, rail targets and electric
  beam, and world 3's vacuum with its bomb on the conveyor; the world-complete card;
- all of world 3's levels, the hidden and secret ones included: floor buttons (open, close,
  timed and cat-pressed) with their barred doors, pipes and flip-flaps, trapdoors down to a
  second floor, the bucket, paint footprints, cheese machines and secret exits;
- most of world 4: levels 4-1 to 4-9 and the secret level 4-S1 with ninja cats and their
  shurikens, twine and catnip that black cats chase and play with, house and sliding doors to
  further floors; the boss chase past the ninjas and the boss fight with its launcher,
  coloured buttons, light and reflectors;
- the HUD, level results with medals and goals, the pause menu, retries, music and sound.

The music (HE-AAC in MP4) is decoded by the engine itself (`src/media`), so it plays on every
platform; checked against Apple's AudioToolbox on all 24 tracks (same length and timing, the
core within about 60 dB SNR, the high band within 0.5 dB per band).

In Reprise the pause menu's options offer the shared VHS look, drawn over the game's frame by
Reprise's screen filter stage (a GPU shader where the renderer allows, else on the CPU); the
game, its sound and its saves are the same with it on.

The reconstruction is checked against recorded runs of the original: the mouse, the path, every
object, the random-number draws and the sound events match frame for frame in those runs.
World 4's secret level 4-S2, worlds 5 and later,
a few world 2 extras (hiding in the couch, the teacup and balloon goals),
the original's save format and the online features are not reconstructed.

Remaining work and known bugs: [plan.md](plan.md). What was learned about the game and its
data: [game.md](game.md).

## Build and run

```sh
./build.sh
open build/DoubleOCheese.app
```

Requires CMake, Ninja, SDL 3 and zlib (on macOS for example `brew install cmake ninja sdl3`);
the app icon is drawn with Pillow. `build.sh` builds `build/doubleocheese` and, on macOS,
packages `build/DoubleOCheese.app` (SDL bundled, signed ad hoc, no game data).

The launcher shows **Import IPA...** and **Play**. Choose (or drop on the window or the app icon)
the `.ipa` of your own copy, version 1.1.1. DoubleOCheese copies the game executable and the
`res/` folder out of the archive, checks every file against the archive's checksums and the
executable's file keys against the game's own data, and keeps the copy in the per-user data
folder (`~/Library/Application Support/DoubleOCheese/game`, about 90 MB). **Play** opens the
world map; progress is kept in `progress.txt` next to it. Escape returns to the launcher.

Command line:

```sh
build/doubleocheese --import /path/to/game.ipa
build/doubleocheese /path/to/Payload/SpyMouse.app 1-1
build/doubleocheese /path/to/Payload/SpyMouse.app 1-1 --replay script.txt 1390 --shot 1500 out.bmp
build/doubleocheese /path/to/Payload/SpyMouse.app --map progress.txt --replay script.txt 1007 --shot 1200 out.bmp
build/doubleocheese /path/to/Payload/SpyMouse.app --flow-check progress.txt script.txt 1568 4500
build/doubleocheese --preview-launcher out.bmp
```

The second form plays one level from an unpacked app folder, the third and fourth replay a
touch script headless (a level, or the world map from a progress file) and save frames, the
`--flow-check` plays the whole game flow from a progress file (empty: a fresh start) with a
script's touches and loads a quick save every `DOC_QUICKSAVE` ticks (default every tick),
comparing with an uninterrupted twin; the last draws the launcher headless. `build/doc-verify`
compares a replay with a trace recorded from the original. With `DOC_QUICKSAVE=K`, it and the
`--map` replay also save and load every K frames and compare with an uninterrupted twin.

## Layout

- `src/data` — readers for the game's files: obfuscated containers and chunk trees (keys read
  from the user's executable), maps, the level table, animation sequences, GUI layouts,
  strings, cutscene scripts, the world map's landscape mesh.
- `src/game` — the recovered rules: map collision and line queries, the drawn path, the mouse,
  level objects, cats, the boss droid, the session (tick order, pickup, trail, snapping, holes,
  score, clock), results, pause, tutorials, the world map.
- `src/importer` — copies and checks the game's files from the user's IPA.
- `src/media` — the music decoder: MP4 track reader, AAC-LC with SBR (HE-AAC), streaming with
  gapless trimming; `tools/media_checks.cpp` checks its transforms and filterbank and decodes a
  file. The standard's code tables in `aac_tables.cpp` derive from Android's PacketVideo AAC
  decoder, Apache License 2.0 (`src/media/LICENSE-AOSP-AAC-TABLES.txt`).
- `src/platform`, `src/render`, `src/app` — SDL 3 host and audio, drawing from game state, the
  launcher (pixel-font shell, no operating-system controls) and the app flow.
- `src/third_party/stb_image.h` — image loading (public domain / MIT).
- `tools/package_macos.py`, `tools/make_icon.py` — app bundle and its icon.
- `tools/verify_traces.cpp` — compares the reconstruction with traces of the original.
- `tools/check_quick_save.py` — checks that quick saves list every field (`src/game/state_io.hpp`).

## License

MIT, see [LICENSE](LICENSE), except `src/media/aac_tables.cpp` (Apache License 2.0, see
`src/media/LICENSE-AOSP-AAC-TABLES.txt`) and the third-party headers named above.
