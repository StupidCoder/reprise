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
| Picarat | games/layton | Connected to the Reprise launcher (2026-10-29); standalone build retained |
| DoubleOCheese | games/spy_mouse_ios | Connected to the Reprise launcher (2026-10-28); standalone build retained |
| BlueStreak | games/sonic | Connected to the Reprise launcher (2026-10-29: all four games); standalone build retained; source folder retired (below) |
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

**BlueStreak retired (2026-10-29).** Its research archive and tools now live in
`games/sonic/.analysis` and `games/sonic/.tools` (real folders, ignored by Git;
no longer a link). Its ROM dumps are read only from `images/`. Its history (five
commits, `main` at `0f644a3`) is kept as the tag `source/bluestreak`; the
`sonic-source` remote was removed. The folder `Decomp/BlueStreak` and the GitHub
repository `StupidCoder/BlueStreak` are no longer needed once that tag is pushed.

Picarat was retired on 2026-10-29: its history is the tag `source/picarat`, its research archive
moved into `games/layton/.analysis` (a real ignored folder, no longer a link), and the
`layton-source` remote was removed. The other original projects are left intact. Local ignored `.analysis` symlinks point
at their research archives, avoiding tens of GB of duplicated evidence. Keep
those original archives available; portable clones require their own evidence.
No research links or images directory is bundled into Reprise.app.

Reprise uses its own data directory; old imports/saves remain untouched in their
original application directories. Automatic library migration is still pending,
except for Spy Mouse (below) and BlueStreak (its current imports are copied once;
quick saves stay in BlueStreak; games/sonic/REPRISE-HANDOFF.md).

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
- **Console buttons and the pause request** (2026-10-29, for the Sonic games:
  `Controls::button2`/`start`, `GameInput::button2`/`start`, `GameModule::take_menu_request`):
  a second action button (X, gamepad east) and Start (Return, gamepad Start), held; a game
  whose Start opens the launcher's pause menu during play asks for it once per press.
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

Diorama is superseded by the `captain_toad_3ds` engine in Reprise. The pinned core,
importer and session have been copied with isolated namespaces; the original project is
retained until the handoff is validated. Scope is macOS and Android flat-screen play only;
no OpenXR target is being integrated. The existing shelf card and artwork keep their ID.
The catalog imports and plays the opening, book and first four reconstructed courses through
SDL GPU. Scene-entry checkpoints use Reprise Continue; see the campaign validation report
for smoke-test coverage, restart-render differences and untested full playthroughs.
See `games/captain_toad_3ds/plan.md` and its validation report for completed checks and limits.

## Shared interfaces added for the puzzle adventures (2026-10-29)

Introduced while connecting games/layton (details: games/layton/docs/reprise.md).

- **Streamed imports** (`GameEntry::import_into`, `platform::FileSource`): large images are read
  with random access through SDL's I/O (also Android documents) and the importer writes its
  files into the library's staging folder; `Library::install_folder` hashes them for the
  manifest. Such manifests end in `size` lines and `check sizes`: at start-up these installs are
  checked by file sizes (hashing gigabytes at every start was too slow). `app::importable()`
  replaces the launcher's checks of `GameEntry::import`.
- **Folder takeover** (`LegacyCopy::folder`, `folder_release`): an earlier app's import copied
  file by file, never moved.
- **Several screens in one frame** (`ScreenEffect::regions`, `GameModule::filter_regions`,
  `app::add_filter_regions`): each region is filtered as a picture of its own (own raster,
  uniforms and CPU form, the frame number offset per region) and composed at its place; the
  rest of the frame stays as drawn. Overlays and notes stay sharp as before.
- **Touch** (`PointerEvent::touch`, `platform::Finger`, `Controls::fingers`,
  `GameInput::fingers`): which pointer events come from a finger, and every finger's position,
  for multi-finger gestures (the adventures' pinch zoom).
- **Save wording** (`GameModule::saved_message`, `GameEntry::continue_note`, `saves_note`): a
  game whose quick save keeps a checkpoint says so instead of "Saved state" / "restores this
  moment"; a game with only its own slots says how it keeps progress.
- **Scripted checks**: `--events` scripts gained `resize W H`.
- **Motion and save pictures** (2026-10-29, later): `GameModule::wants_motion`, `Host::set_motion_wanted`,
  `Controls::motion` / `GameInput::motion` (the accelerometer in the window's axes, opened only while
  such a game runs; Android), and `GameModule::save_picture` (the quick save's picture; default
  `picture()`).
