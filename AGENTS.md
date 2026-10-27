# Joyport — instructions for agents

Joyport is a collection of clean, native re-implementations of Commodore 64
and Amiga games (C++20 + SDL 3; macOS first, Android later). A launcher lists
the supported games; the user imports their own tape or disk image, and the
game then runs on our own code, with optional enhancements. The collection
grows one game at a time. See [PLAN.md](PLAN.md) for the roadmap and status.
It lives in the Decomp workspace: also follow [../AGENTS.md](../AGENTS.md) and
[../RECONSTRUCTION.md](../RECONSTRUCTION.md); the rules below add to them.

## Evidence and sources

- Derive game-specific findings only from the user's own images (TAP, D64,
  ADF, …) and from running those images. Keep evidence: image hashes, file
  offsets, instruction traces, captures, comparisons. Mark hypotheses as such.
- Do not consult or reuse existing game disassemblies, decompilations, ports,
  symbol or asset maps, or game-specific reverse-engineering guides.
- General-purpose tools, emulators and hardware documentation (VIC-II, SID,
  CIA, Paula, Denise, Agnus, the Copper and Blitter, AmigaDOS, …) are fine; record which
  ones and which versions were used.
- **Prior work.** The user's own RetroReverse project
  (`~/Development/RetroReverse`) holds earlier analyses of some of these games
  (Fort Apocalypse C64, Marble Madness Amiga, …) plus Go tools and a C++ C64
  emulator. It may be reused: port its decoders, check its writeups. Its own
  README says the model that wrote it recognised some game names from its
  training data, so verify every claim taken from it against the image before
  relying on it, and say so in the research notes.
- Disclose any prior exposure to outside game-specific material in the
  game's research notes.

## Target form of the shipped code

- Ship a fresh re-implementation of each game's rules and concepts in
  idiomatic C++ (own types, names and ownership), not a translation of the
  6502/68000 code. No CPU emulation, no RAM images, no guest addresses as
  state, no fixed-size guest records addressed by offset, no VIC-II/custom-chip
  register images as the game's model of the screen. Draw the picture from
  game state.
- Keep the rules exact: positions, speeds, timings, collisions, randomness,
  edge cases and bugs a player can observe. Keep integer widths, wraparound
  and update order wherever they change an outcome, and say so in a comment.
- **Code versus data.** Native game code is rewritten from scratch
  (including any script or music interpreters). Anything the original drives
  from data — maps, charsets, sprites, tables, display scripts, music
  streams, timings — is read from the imported files at run time, never typed
  into C++.
- A close translation or an emulator is allowed as a research and
  verification tool (lockstep comparison against the original), never inside
  the shipped game.
- Hardware failures are not part of the game: no sprite multiplexing
  dropout, no slowdown. Effects the game itself produces stay.
- Enhancements only upgrade what the original shows; they never invent new
  content. Each one is optional and off by default; all off is the original.
- Annotate the clean code with the original routine addresses and evidence it
  reproduces, so it stays traceable.

## Layout

```
src/platform/        host services: window, input, audio, files, dialogs (SDL 3)
src/ui/              the launcher's drawing kit (own pixel font, shapes)
src/app/             launcher: catalog, library/import, screens, main
src/c64/             shared C64 formats and hardware helpers (TAP, KERNAL tape, PRG, …)
src/amiga/           shared Amiga formats and helpers (ADF, hunks, …) — later
src/<game>_<platform>/  one directory per game, e.g. src/fort_apocalypse_c64
tools/               build, packaging and command-line tools
.analysis/<game>/    research notes and evidence per game (not shipped, gitignored)
```

- Each game keeps its importer, data decoders, game code and enhancements in
  its own directory and its own namespace. Shared code moves to `src/c64`,
  `src/amiga` or `src/platform` only when a second game needs it.
- Never commit or bundle game images or extracted game data. Imported content
  lives in the user's data folder
  (`~/Library/Application Support/NativeReconstruction/Joyport/`).
- Exception (owner's decision, 2026-10-25): the 64 upper-case glyphs of the
  C64 character ROM that Bruce Lee copies for its text live in
  `src/c64/character_rom.cpp`, marked as not covered by the MIT licence.
  Likewise (2026-10-27) the Kickstart 1.3 topaz 9 glyphs Marble Madness
  draws with, in `src/amiga/topaz.cpp`, and for Mind Walker the topaz 8
  glyphs (same file) and Intuition's two screen-depth gadget images and
  window close-gadget image (`src/amiga/intuition_imagery.cpp`). Add
  no other firmware or ROM contents without asking.
- Real game titles appear only to name the game a dump belongs to
  (descriptive use). Don't use the original logos or box art in Joyport's own
  interface, and nothing should suggest a publisher's endorsement.

## Working practice

- Work in small, verifiable slices; verify each against the original (the
  RetroReverse C64 emulator or another emulator as the reference), and report
  what was actually checked.
- Build with `./build.sh` (CMake + Ninja, packages `build/Joyport.app`).
  Always rebuild the bundle before handing over; `cmake --build` alone leaves
  a stale app.
- Disk space is shared and tight: keep hashes, small logs and small images,
  not bulk dumps; keep only the latest build.
- Every reported open item also goes into PLAN.md (or the game's notes).
- Commit only when the user asks.
