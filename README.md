# Joyport

A growing collection of classic Commodore 64 and Amiga games, rebuilt as
native C++ programs. Joyport contains no game data: you import your own tape
or disk image, and Joyport reads the graphics, levels and sound from it while
running its own re-implementation of the game, with no emulator.

Game names are used only to describe which data Joyport can read. Joyport is
not affiliated with or endorsed by the games' publishers.

## Status (version 0.1.0)

| Game | Machine | State |
| --- | --- | --- |
| Fort Apocalypse | C64 | Tape import (TAP), graphics decoding, title and options screens (identical to the original frame by frame); the game itself is next |
| Bruce Lee | C64 | Planned |
| Mindwalker | Amiga | Planned |
| Marble Madness | Amiga | Planned |

See [PLAN.md](PLAN.md) for the roadmap.

## Building

You need CMake 3.24 or later, Ninja, a C++20 compiler and SDL 3.2 or later
(`brew install sdl3 cmake ninja`).

```bash
./build.sh
```

```bash
open build/Joyport.app
```

Or double-click `Play Joyport.command`, which builds on first use. Imported
content is stored in
`~/Library/Application Support/NativeReconstruction/Joyport/`.

## Playing

Select a game and press Enter. Joystick: arrow keys or WASD, fire with Space
or Z, or a gamepad. C64 keys: F1/F3/F5/F7 (or 1/3/5/7), P for the C64 space
bar. Esc returns to the launcher. I shows the decoded game data.

## Importing

Select a game and press Enter to choose its image, or drop the image onto the
window. Joyport decodes the image the way the original loader does, checks
every checksum, recognises the release from the decoded content and installs
only the game's files, not the image.

Command line, for testing:

```bash
build/joyport --import Fort_Apocalypse.tap
```

## License

Joyport's source code is released under the [MIT License](LICENSE): use, change
and reuse any part of it. It contains no game data; the games stay the
property of their rights holders.
