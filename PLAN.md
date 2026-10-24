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
| `src/c64` | TAP reader, KERNAL tape decoder, PRG files | started |
| `src/c64` | VIC-II-style picture building blocks (charset/sprite decoders, palette), SID sound | planned |
| `src/amiga` | ADF/AmigaDOS reader, hunk loader, IFF, Paula sound | planned |

## Games

| Game | Machine | Image | State |
| --- | --- | --- | --- |
| Fort Apocalypse | C64 | `Fort_Apocalypse.tap` (MD5 `bec7409816865f3ad160af9984f127cd`) | playable from title to debrief, compared with the original in scripted runs; pause menu with the enemy-pilot option |
| Bruce Lee | C64 | — | planned |
| Mindwalker | Amiga | — | planned |
| Marble Madness | Amiga | — | planned (RetroReverse has prior analysis) |

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
   animation and a wobble), the fuel depots and the landing pad sinking
   smoothly, and the craft banking smoothly; the rules keep the original's cells (objects stamp characters
   into the map, collisions read them). Particle effects add the debris,
   fire, tossed turrets and falling wrecks. Rotated art is resampled
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
9. **Optional: loading screen** — the tape's U.S. Gold loading screen is a
   display script plus a music stream that also patches memory. It could
   play during import, read from the imported data (the music player is
   native code to rewrite; its stream is data).

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
- Images for Bruce Lee, Mindwalker and Marble Madness are not in the
  project folder yet.
- Only TAP v0/v1 is read; T64/D64/G64/PRG imports come with games that need them.
- Android build not started.
- Enhancements: the effects keep no state in quick saves (they restart on
  load); the enemy helicopter's banking and the prisoners' running stay as
  in the original. With smooth motion objects show where the game has them
  now, while the craft's VIC collisions use the original picture, whose
  copied screen lags up to 7 frames; touching may look slightly off.
- Widescreen: shots are not seen past the original window (the game ends
  them at its edges); not yet played in the app at full 21:9.
