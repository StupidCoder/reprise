# Reprise game engines

Follow ../AGENTS.md and ../RECONSTRUCTION.md, plus each engine's own instructions.
Each engine owns its game logic, data interpreter and importer. Collections
are optional presentation metadata, never a source-code ownership boundary.
The initial layton/sonic/spy_mouse_ios imports retain their standalone structure
and build tools; adapters to the shared Reprise host are a separate step.

Naming: single-game directories use `<game>_<platform>` (for example
`spy_mouse_ios`, `bruce_lee_c64`). `layton` and `sonic` are intentionally
shared, multi-game and multi-platform engine families; do not duplicate their
shared code to force them into single-game folders. Collection membership is
independent of this engine grouping.
