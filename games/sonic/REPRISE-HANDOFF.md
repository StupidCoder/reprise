# Reprise handoff

Copied from BlueStreak commit `0f644a33a62ef8a5278322e1fda7c67068b3c42d`. On
2026-10-29 the snapshot was compared with the BlueStreak working tree: nothing
was missing (only an untracked copy of docs/ and one blank line in
design/SPEED-ARCHIVE.md differed). The stable engine root is `games/sonic`.

## Connected (2026-10-29)

The four catalog entries `sonic_gg`, `sonic2_gg`, `triple_trouble_gg` and
`sonic_advance_gba` import, start and run inside Reprise. Per-game status is in
[docs/plan.md](docs/plan.md) ("Reprise integration"); validation in
`../../docs/VALIDATION.md`.

- `reprise/entry.cpp`: the Reprise adapter (`app::GameModule`, importers,
  previews, the BlueStreak takeover). It includes only
  `src/embed/bluestreak_embed.hpp` and Reprise's own headers.
- `src/embed/`: the embedding interface over BlueStreak's modules (std types and
  Reprise's shared `platform::Image` only).
- Build boundary: Reprise's root CMakeLists sets `BLUESTREAK_HOST_SHARED` and
  includes this folder. Embedded, `CMakeLists.txt` builds only the engine
  libraries and `bluestreak_embed`, compiled against Reprise's shared
  `platform/` headers (image, SHA-256, host constants; identical to BlueStreak's
  copies apart from Reprise's `persist` members) and linked to Reprise's
  `platform`. BlueStreak's shell (`src/app`), host (`src/platform`) and its
  `platform`/`shell_checks` targets are built only standalone, so Reprise links
  one host implementation and never sees BlueStreak's `app/...` headers.
- Engine changes made for the integration (behaviour-neutral): extraction can
  write to memory (`sonic1_gg::PackWriter`, `Assets::files()`,
  `extract_pack`), `pack_current` per game, and Triple Trouble's pack format
  number 3 → 4 (packs from BlueStreak 0.14/0.15 lack one data byte; see plan.md).
- Standalone: `./build.sh` here still builds the BlueStreak app and its check.
- Check: `SONIC_ROMS=images build/sonic_checks [SHOTS]` (from the repository root)
  (CTest `sonic_module`; skipped without the variable). `SONIC_LEGACY=<a
  standalone BlueStreak data folder>` also checks the takeover (read only).

## Research archive and history (2026-10-29)

The original BlueStreak folder is retired. Everything still needed moved here:

- `.analysis/` (7.8 GB: traces, captures, harnesses, Ghidra projects, per-game
  notes such as `triple_trouble_gg/PLAN.md`) and `.tools/` (the research
  emulator builds and setups). Both are ignored by Git (`.gitignore`); keep
  them on disk, they are the evidence behind the reconstruction.
- ROM dumps are read only from Reprise's `images/` folder. The research
  scripts find them at `<games/sonic>/../../images/` (repointed 2026-10-29;
  historical logs still name the old paths).
- The harnesses in `.analysis/{gg_engine,sonic2_gg,triple_trouble_gg,
  sonic_advance_gba}/harness` build against this folder's `src`
  (`cmake -S <harness> -B <harness>/build -G Ninja`). `.analysis/sonic2_gg/wide`
  was already stale before the move (it names `gamegear/vdp.cpp`, removed in
  0.16); its earlier binaries remain.
- History: the tag `source/bluestreak` in this repository (the five BlueStreak
  commits, `main` at `0f644a3`).

## Keep

Do not commit research dumps, ROMs or extracted packs. The standalone BlueStreak
data folder (`NativeReconstruction/BlueStreak`) is only read (its imports are
copied once).
Follow this project's AGENTS.md (if present), plus ../../AGENTS.md and
../../RECONSTRUCTION.md. Game-specific instructions govern its reconstruction.
