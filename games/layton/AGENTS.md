# Picarat — notes for contributors and coding agents

- Research notes, the full handoff (`.analysis/README-research.md`,
  `.analysis/AGENTS.md`, `.analysis/RECOVERY-GAPS.md`, `.analysis/docs/`), the
  research tools, emulator harness and evidence live in `.analysis/`, which is
  local and not part of the repository. Read those files first when present.
- Derive game behaviour only from the supplied ROM and its execution. Do not use
  existing disassemblies, decompilations, ports, symbol or asset maps of the game.
- Keep reconstructed control flow faithful; comments name the original routines
  (ARM9 overlay and address) and data they come from. Refactor only after
  comparing against the original.
- Naming: refer to the main character as "the protagonist". Do not put the
  series title or character names into code, comments or user-facing text;
  names the game needs at runtime are read from the ROM (`names.bin`). The
  exception is the library cards and messages, which name each game by its
  English subtitle (`src/shell/library_screen.cpp`, `kKnownRoms`).
- The repository must never contain ROM dumps or data extracted from them.
  Constant tables that are data in the game's code are read at run time from
  the pack's `code/` folder (the importer saves the expanded ARM9 program and
  overlays there); the source keeps only their addresses. Short constants that
  are part of a routine's logic may stay in the code with their address.
- Code style: run clang-format (`.clang-format`) after editing. Every file
  starts with what it does in plain words (for puzzles and minigames: how
  they play); comments explain the game's rules and quirks, and cite the
  original routine addresses that back them. Do not add game facts that are
  not shown by the code or the ROM.
- Layout: `src/story`, `src/puzzles`, `src/minigames`, `src/menus`,
  `src/system`, `src/graphics`, `src/video`, `src/sound`, `src/import`; the
  app in `src/shell` and the SDL 3 platform layer in `src/platform`.
- The shell is portable C++ and draws its own controls (`src/shell/style.hpp`,
  `widgets.hpp`); do not add native UI toolkit code. Only `src/platform`
  talks to SDL.
- Build and test: `./build.sh`, or `cmake --build build-release` and
  `ctest --test-dir build-release`. Data tests need
  `build-release/picarat_import --pack ROM build-release/native-assets`.
- Android: `./build-android.sh [--install]` builds the APK without Gradle
  (SDL from its pinned release source, `android/` holds the manifest, icon
  and the activity subclass). Notes and open items:
  `.analysis/docs/android-port.md`.
- Changing the native pack: edit `src/import/asset_pack.cpp`, bump
  `kPackSchema` (`src/import/rom_import.hpp`) and `kPackFolder`
  (`src/shell/library.hpp`) together, and validate a fresh import.
- The sequel (library card "Lost Future", game code C3JD; `Game::Adventure3`
  in code) is imported by
  `src/import/adventure3_*` into its own pack (`kAdventure3PackSchema`,
  `kAdventure3PackFolder`). Its research lives in `.analysis/adventure3/`.
  Findings from one game are not evidence for the other: verify against the
  sequel's ROM before reusing a subsystem.
- The first game (library card "Curious Village", game code A5FP, European
  multi-language cartridge; `Game::Adventure1`) is imported by
  `src/import/adventure1_*` into its own pack (`kAdventure1PackSchema`,
  `kAdventure1PackFolder`); its language rule is `src/system/a1_language.*`.
  Its scripts are GDS like the second adventure's but with their own command
  numbers and structured flow control (if/else, loops): they run on the shared
  interpreter with the dialect `adventure1::scripts()`
  (`src/system/a1_script.*`). Its pictures and fonts use the shared
  native pack files, so `GraphicsAssets` reads them. Its research lives in
  `.analysis/adventure1/`.
- The fourth adventure (library card "Spectre's Call", game code BLFD;
  `Game::Adventure4`) is imported by `src/import/adventure4_*` into its own
  pack (`kAdventure4PackSchema`, `kAdventure4PackFolder`); import only so
  far. It is built on a new engine library (overlay 6) with new containers
  (GFSA archives, GFSP packs, GSS1 scripts, GSD1 data, GTXT/GPLT pictures,
  GFNT fonts). Its research lives in `.analysis/adventure4/`.
- The 3DS adventures (cards "Miracle Mask", AKKD, and "Azran Legacy", AL6D;
  `Game::Adventure5/6`) are unencrypted cartridge images (CCI) read by
  `src/import/ctr_image.*` (no keys, no decryption; encrypted images are
  refused) through a streaming `RomSource`, and unpacked by
  `src/import/adventure56_*` into `akkd-v1` / `al6d-v1`; import only. They
  share one engine and format set (XFSA/ARC0 archives, XPCK packs, IMGC
  pictures, XQ32 scripts, XSCR data, NintendoWare sound, moflex movies),
  descended from the fourth adventure's. Research: `.analysis/adventure56/`.
- Every game in the collection gets the same conveniences, listed in the
  README under "Conveniences in every game" (music player, movie theatre,
  puzzle mode, minigame free play, skipping, start at the title,
  checkpoints, touch-area help, pinch zoom); a new game is not complete
  without them. Unsupported ones show as greyed-out placeholders.
- Skipping is the same in every game, present and future
  (`src/system/game_types.hpp`): a game reports `GameSnapshot::skippable`
  while a movie, a timed scripted wait (logos, still pictures) or a wait for
  a voice plays, and ends it when `HostInput::skip` is set (the session holds
  the request until the game stops reporting skippable, so one request ends
  a whole sequence). The trigger is the shell's (`App::requestSkip`,
  `kSkipTriggers`: the in-game menu's Skip for now); games never decide it.
- Touch-area help is the same in every game (`TouchArea` in
  `src/system/game_types.hpp`): while a room waits for a tap, the game reports
  `GameSnapshot::touchAreas` - each area's lower-screen rectangle and kind
  (text, dialog, puzzle not solved yet, hint coin not found yet), read from
  the room's own data by the rules of the game's own tap handlers. The shell
  draws them when the in-game menu's switch is on (per game,
  `touch-areas.txt`). Details: `.analysis/docs/touch-areas.md`.
- App checkpoints are the same in every game: one automatic checkpoint per
  game in the game's own save record, written whenever a room is ready (so
  rooms, puzzles and minigames are covered), continued through the game's
  own load path straight into that room ("Continue story" in the hub).
  Details: `.analysis/docs/checkpoints.md`.
- Share code between the games wherever feasible: extend existing functions
  (decompression, ROM access, fonts, sound, movies, puzzle engines) to cover
  both rather than duplicating them.
- Code versus data (see the workspace RECONSTRUCTION.md): native game code is
  rewritten from scratch; anything the game drives from data runs from that
  data. The third adventure's scenes and puzzles (and very likely the fourth's)
  are LSCR scripts: they must run on a clean re-implementation of its script
  interpreter (overlay 9), not on hand-written C++ copies of the scripts.
- Engines shared between games are for native code: the second adventure's
  puzzle types are native, so their rules live in shared engines (first:
  `src/puzzles/written_answer.*`). A game that scripts a puzzle shares only the
  native building blocks its script commands drive (e.g. the handwriting box's
  stroke capture and recognition).
