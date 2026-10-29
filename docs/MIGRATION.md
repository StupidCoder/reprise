# Foundation and handoff record

## Baseline

Joyport commit `bc99948abfac710199857f194eb2f7149234bfbf` was cloned with full history. The source working tree
was never reset, staged or edited. The inherited remote is now `joyport-source`
and its push URL is disabled to avoid accidentally pushing Reprise to Joyport.
The Reprise remote is https://github.com/StupidCoder/reprise.git. The initial
Reprise migration commit records the combined working tree; pinned source
revisions are retained below.

Source mapping:

| Original Joyport | Reprise |
| --- | --- |
| src/app | app |
| src/platform, src/ui, src/c64, src/amiga | shared/<same name> |
| src/state, src/third_party | shared/<same name> |
| src/<game> | games/<game> |

Header include names and game IDs remain unchanged. The binary save magic
`Joyport quick save` is intentionally retained for compatibility. The new
application data folder is `NativeReconstruction/Reprise`; source images in
`images/` are not the imported content library.

## Completed source handoffs

All four source working trees were verified clean after the final handoff.
Joyport's final commit f511b90 adds design records and plan notes only; its
code matches the baseline above. The root retains that final history through
joyport-source/main. The other exact revisions and file counts are recorded in
[source-handoffs.json](source-handoffs.json).

| Source | Stable engine root | State |
| --- | --- | --- |
| Picarat | games/layton | Source migrated; standalone build retained |
| DoubleOCheese | games/spy_mouse_ios | Connected to the Reprise launcher (2026-10-28); standalone build retained |
| BlueStreak | games/sonic | Source migrated; standalone build retained |
| Joyport games | games/<existing game id> | Connected to the Reprise launcher |

Source files were exported from the committed Git trees, not copied from dirty
working directories. No nested repositories were created. Named `*-source`
remotes retain their full histories; push URLs are disabled. The first Reprise
commit should explicitly record/join the histories as appropriate. The first Reprise commit includes the migration, subsequent shell work and
completed game handoffs. Source remotes remain local history references.

Agents can work on engines at the stable paths above. They should keep their
existing namespaces and internal source trees while adapters are developed.
The shell previews are **not** evidence of live engine integration. Rendering,
ordered pointer input, timing, audio, importer and save/profile adapters remain.
No subprocess launching or screenshot-based rendering is presented as integration.

The original projects are left intact. Local ignored `.analysis` symlinks point
at their research archives, avoiding tens of GB of duplicated evidence. Keep
those original archives available; portable clones require their own evidence.
No research links or images directory is bundled into Reprise.app.

Reprise uses its own data directory; old imports/saves remain untouched in their
original application directories. Automatic library migration is still pending,
except for Spy Mouse (below).

## Shared interfaces added for engine integration (2026-10-28)

Introduced while connecting Spy Mouse; designed so Layton and Sonic can use them too.

- **GPU frames** (`platform::GpuFrame`, `Host::present_game_gpu`, `GameModule::gpu_frame_size`
  and `draw_gpu`): a game draws with the host's SDL renderer into a render-target texture of
  the shown size in device pixels; the host composes it with the note and the pause overlay. No
  per-frame read-back. `GpuSurface::capture` (Host) is a one-off read-back for save previews,
  given to the module with `attach_gpu`.
- **Screen filter stage** (2026-10-29: `platform::ScreenEffect`, `platform::FragmentShader`,
  `Host::present_game_gpu`'s `effect`, `app::screen_effect`, `app::screen_filter_option`,
  `GameOption::presentation`, `ui::vhs_effect`): the VHS look over GPU-drawn games. The game
  draws at its logical size; the host applies the effect as it shows the frame, then the note
  and overlay, unfiltered. A fragment shader on SDL's GPU renderer (the host now prefers it,
  declaring SPIR-V and MSL; the `SDL_RENDER_DRIVER` hint or environment variable overrides),
  else one read-back and the CPU form. Off: the earlier path, unchanged. Picture games keep
  `shown_picture`'s CPU look, so nothing is filtered twice.
- **Ordered pointer events** (`platform::PointerEvent`, `Controls::pointer_events`,
  `GameInput::pointer_events`): every press, move and release over the game frame in order,
  in frame units (floats); events of frames without a tick go to the next tick.
- **Leaving from the game** (`GameModule::finished`): the game's own quit returns to the
  library without an error.
- **Per-game import size** (`GameEntry::largest_image`, default 64 MB): only importers that
  accept a file's size see it; the IPA importer takes up to 512 MB.
- **Takeover of an earlier application's data** (`GameEntry::legacy`, `LegacyCopy`,
  `Library::adopt_legacy`): once per game, its import is installed when Reprise has none and
  files such as progress are copied where Reprise has none; originals are only read, nothing in
  Reprise is overwritten, and a marker in `<data>/migrations/` records the outcome (a failed
  takeover is tried again next start).
- **Scripted checks** (`--events FILE`, `app/event_script.*`; `Host::capture_next_frame`):
  timed mouse, key and file-drop events through SDL's queue and frame captures, for checking
  the real window.

## Spy Mouse

The engine runs inside Reprise's window and loop (games/spy_mouse_ios/REPRISE-HANDOFF.md):
GPU frames, ordered pointer events, the game's fixed 17 ms step (Reprise's own fixed-step loop;
its backlog limit is four steps where the standalone app drops after three), audio mixed by
the engine and resampled into Reprise's stream, Reprise's pause menu (resume, restart, quick
save and load, return to the library) and the game's own map quit. Quick saves go through the
shared persistent save format with a picture for the details page and Continue playing.

Campaign progress is `<data>/progress/spy_mouse_ios/progress.txt`, separate from quick saves.
Loading a quick save restores the saved moment but never lowers progress: the restored
profile takes over every unlock, best total, won goal and completion of the progress on disk.

DoubleOCheese takeover (first start of Reprise): its `progress.txt` is copied when Reprise has
none (level uids are the same in 1.0.5 and 1.1.1). Its import is the 1.0.5 build, which this
port no longer reads; the message says so and asks for the 1.1.1 IPA. DoubleOCheese's files
are never changed.
The save magic and game IDs remain compatible for the connected Joyport games.

Diorama remains in its original project; later share its core between Reprise
and an independent OpenXR target, preserving its graphics abstraction.
