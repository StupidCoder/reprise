# Reprise handoff

Copied without changes from DoubleOCheese commit `dd0df5170a52d0d5fcdb8021c369cef12a33dad2`.
The stable engine root is `games/spy_mouse_ios`. The original DoubleOCheese project is left
untouched; all commits now belong to the Reprise repository.

## Connected to Reprise (2026-10-28)

- `reprise/entry.*`: the catalog entry (id `spy_mouse_ios`, no collection) and its GameModule.
  It talks to the engine only through `src/embed/spy_mouse_embed.hpp`, a header that needs none
  of the engine's include paths (the engine's `platform/...` would otherwise meet Reprise's).
- `src/embed`: the game inside an embedder: fixed 17 ms ticks on the embedder's thread
  (denormals flushed for the game's code only, `ScopedFlushToZero`), drawing with the
  embedder's SDL renderer into a texture it owns (`platform::Host::attach`, `beginFrameInto`),
  sound pulled by the embedder (`platform::Audio::openPull`, resampled 44.1 to 48 kHz), the IPA
  read in memory (`importer::readIpa`), quick-save state, preview pictures, and the standalone
  app's old import folder for a takeover.
- `src/media`: the music decoder (MP4, AAC-LC with SBR), portable C++ with no platform codec;
  `SoundOutput` reads each track's bytes through `GameFiles` (so an in-memory import plays
  too) and the mixer pulls the decoded stream, looping gaplessly. AudioToolbox is no longer
  used; it served only as the reference the decoder was checked against.
- `src/app/game_flow.*`: the map, levels and the way between them as a frame-driven machine,
  used by Reprise and by the standalone app alike. Touches are queued in order and handled at
  the next tick; `Session::prepareFrame` (the original's render-time state step) runs once after
  every tick, so drawing changes nothing and a picture can be taken at any time.
- Quick saves keep the current scene's start (scene, profile text, seed, clock) and the touches
  since, by tick; loading runs the scene again silently and resumes the music and loops.
  Progress made since a save (unlocks, best totals, goals) is never lowered by loading it.
- Campaign progress: `<data>/progress/spy_mouse_ios/progress.txt`, outside the imported files
  and the saves. DoubleOCheese's `progress.txt` is copied once when Reprise has none; its 1.0.5
  import cannot be used (the port reads 1.1.1) and is reported, never changed.

The standalone `doubleocheese` app, `doc-verify` and the replay options keep working
(`./build.sh` in this folder). The local `.analysis` link points into the original project;
keep that project available. Do not commit research dumps or imported game data. Follow this
project's AGENTS.md (if present), plus ../../AGENTS.md and ../../RECONSTRUCTION.md.
