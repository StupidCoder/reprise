# Reprise

Follow [RECONSTRUCTION.md](RECONSTRUCTION.md). Preserve game behavior and evidence.
When adding or integrating a game, read [ADDING_A_GAME.md](ADDING_A_GAME.md),
including the current randomness, portability and enhancement requirements.
Pure SDL 3 portable shell; no native OS UI controls. Game engines may use an
explicit graphics backend behind the host boundary. Never embed ROMs or imports.

- `app/`: launcher, catalog, documentation, save orchestration.
- `shared/`: UI, host services and shared platform formats.
- `games/`: one game per directory, stable namespaces and identifiers.
- `collections/`: optional presentation metadata, never game ownership.
- `targets/`: platform/application build guidance.
- `images/`: user-owned source media, gitignored; do not move, delete or bundle.

Build and package with `./build.sh`; run CTest for relevant host/UI changes.
Preserve the source projects until their handoffs are validated. Do not change
other agents' working trees. Do not commit or push unless asked.
See `docs/MIGRATION.md` for the pinned baseline and outstanding handoffs.
