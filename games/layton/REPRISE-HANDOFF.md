# Reprise handoff

Copied from Picarat commit `3566d8c0c83e2e170b8a6a7d2303485d4e9022ff`. Later
work (the sixth adventure's Nut Roller and Garden) was done here; on
2026-10-29 Picarat's own tree held nothing newer than this folder, so no
changes had to be brought over.

**Connected to the Reprise launcher on 2026-10-29** (uncommitted when
written): all six catalog ids import; five start their stories; three have
checkpoint quick saves. Integration, saves, filters, controls and checks:
[docs/reprise.md](docs/reprise.md); per-game scope and remaining work: each
`docs/<game-id>/plan.md`.

The standalone build (Picarat app, research tools, tests, `build-android.sh`)
is kept and still builds; Reprise includes this folder with
`PICARAT_EMBEDDED=ON` and uses only `src/embed/layton_embed.hpp`. Engine
changes made for the integration: `GameSession::step()` and `ended()`
(lockstep frames), sound through `platform/audio_pull.*` (pulled per frame;
the standalone app still opens its own device), `src/platform/host_embed.cpp`,
and the UTF-8 helpers moved to `src/shell/utf8.cpp`.

Since 2026-10-29 nothing here depends on the original Picarat folder or its
GitHub repository:

- its history is the Reprise tag `source/picarat` (commit `3566d8c`, all 14
  commits);
- the research archive is this folder's own (ignored) `.analysis/`, moved
  here in full (16 GB: notes, tools, reference harness and Ghidra projects,
  evidence, golden baselines, Picarat's release builds in `.analysis/dist`).
  The reference harness (`.analysis/research/build`) was reconfigured here;
- the cartridge images and the Curious Village `.sav` are in Reprise's
  `images/`; ignored links in this folder (and the Pandora's Box link in
  `.analysis/`) point there, where the research tools look for them;
- the test packs in `build-release/` are real folders (APFS clones for the
  first and fourth game's, which came from Picarat's app data).

Still outside the repository: the standalone Picarat app's data,
`~/Library/Application Support/Picarat` (packs and saves). Reprise copies them
once on first start; several research tools (`adventure*/tools/*.py`) still
read packs from there, so keep it while you use those tools.

Do not commit research dumps or imported game data. Follow this project's
AGENTS.md, plus ../../AGENTS.md and ../../RECONSTRUCTION.md.
