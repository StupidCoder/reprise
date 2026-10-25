# Joyport

A growing collection of classic Commodore 64 and Amiga games, rebuilt as
native C++ programs. Joyport contains no game data: you import your own tape
or disk image, and Joyport reads the graphics, levels and sound from it while
running its own re-implementation of the game, with no emulator.

Game names are used only to describe which data Joyport can read. Joyport is
not affiliated with or endorsed by the games' publishers.

## Status (version 0.2.0)

| Game | Machine | State |
| --- | --- | --- |
| Fort Apocalypse | C64 | Playable from the title to the debrief; compared frame by frame with the original in scripted runs |
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
bar. I shows the decoded game data.

Esc (or the gamepad's Back/Guide button) opens the pause menu: resume,
restart, quick save and load (also Cmd+S and Cmd+L), sound on/off (also M),
the game's options and the way back to the launcher. The options you choose
are kept for each game (in `options/` in the data folder) and set again the
next time you start it.

### Fort Apocalypse options

- **Enemy pilot** — *Original* keeps the famous bug: a stray byte of text
  overlaps the first instruction of the enemy helicopter's climb routine, so
  it checks the wrong spot before climbing and often flies into rock.
  *Fixed* makes it check its own path, and it also crashes on rock that
  the original lets it fly through: the C64's collision hardware counts one
  of the four multicolour pixel kinds as background, and most of the rock
  (level 1's and 3's brown) is drawn in it.
- **Particle effects** — debris and fire on the C64's pixel grid and in its 16
  colours: shots, the tanks' shells and the enemy's shots throw debris where
  they hit; destroyed tanks burst and toss their turret, which tumbles and
  bounces off the landscape; shot-down mines drop in an arc, turning, and
  blow up on the ground; a shot-down helicopter, yours or the enemy's, keeps
  its flashing but bursts apart, and its wreck bounces off what it rammed
  with what way it had left, then falls slower than the small debris, as
  the big machine it is. On its next hard impact it bursts into tumbling
  chunks of itself that bounce like a tank's turret (if it comes down too
  gently, it bursts where it settles), so no intact helicopter is left on
  the ground. When yours
  crashes, the playfield flashes only for a short shock (8 frames), then
  stays black so the wreck can be followed.
  Before your next helicopter comes, the game waits for that (about a
  second after the wreck stops, six seconds at most; fire or the space bar
  goes on at once), while tanks, mines, shells, the enemy and the men carry
  on: from the crash's first frame a copy of the game is shown, its camera
  following the wreck (by the game's own scrolling) until it bursts, and is
  dropped at "GET READY". The HUD stays the real game's. Wrecks
  and debris hit only the landscape, not tanks, mines or men, and a wreck
  that starts inside rock drops out of it.
- **Smooth motion** — the helicopter banks smoothly (its level sprite turned
  instead of the stepped tilt shapes, the game's bank steps eased into one
  turn) and leans into the way it moves while it still faces the screen, tanks glide from cell to cell and rock
  over the rough ground, mines and shells glide instead of jumping (mines
  wobbling as they fly), the men run smoothly, the fuel depots and the
  landing pad sink smoothly, and tanks that come back rise out of the ground
  at their home (with particle effects, throwing up the rock).
- **Widescreen** — the picture fills the window to its edges, without the
  C64's border, and the playfield shows more of the map left and right, up
  to 21:9 (as in BlueStreak's Game Gear games). The HUD stays in the middle.
  Shots that fly past the original window's left edge hit what the wider
  view shows them cross; in the original they test cells far to their right
  there (an unsigned subtraction), which no one could see.
- **VHS tape** — the picture as from a worn video tape on an old TV, in four
  steps (its faults are the tape's lines, two to each of the C64's):
  *Light* (soft, bleeding colour, a little line wobble, the tracking
  band and head-switching noise at the bottom, grain, scanlines), *Medium*
  (more of it, dropouts, the odd tear), *Heavy* and *Broken* (tears full of
  static, the colour fading out for moments, the vertical hold slipping).
  A shared filter (`src/ui/vhs.cpp`) for the other games too.

All of these change only the picture (and widescreen the one shot rule
above): the game plays exactly the same.

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
