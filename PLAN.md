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
| Fort Apocalypse | C64 | `Fort_Apocalypse.tap` (MD5 `bec7409816865f3ad160af9984f127cd`) | tape import, graphics, reference harness, title and options done; game start next |
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
5. **Gameplay** — Rocket Copter, bullets, enemy helicopter, tanks, homing
   missiles, mines, prisoners, gates/walls/lasers/barriers, collision
   matrix, difficulty variants, level progression, victory.
6. **Sound** — the game's SID effects and music as a clean player.
7. **Optional: loading screen** — the tape's U.S. Gold loading screen is a
   display script plus a music stream that also patches memory. It could
   play during import, read from the imported data (the music player is
   native code to rewrite; its stream is data).

## Open items

- Fort Apocalypse: no sound yet (the title's two-voice tone is the first).
- Fort Apocalypse: game start (modes 3/4: level setup, "ENTERING ..." screen)
  is the next slice; the in-game screen split (HUD part scroll 7, playfield
  part from raster $76) is recovered but not used yet.
- No pause menu or quick save in the player yet; Esc returns to the launcher.

- Fort Apocalypse: is the radar stream's spill over the mine tables
  ($3500-$3532) observable? Settle when porting the mines.
- Fort Apocalypse: which code sets the in-play colour RAM to $0D (previews
  use the observed values).
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
- No enhancements yet.
