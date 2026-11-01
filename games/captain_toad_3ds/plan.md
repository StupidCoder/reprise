# Reprise integration

Stable game ID: `captain_toad_3ds`. Diorama is the superseded source project, not a new shelf entry. Scope: macOS and Android flat-screen play only. No OpenXR or stereo mode.

## Current state

The shelf and artwork already exist. The core/importer/session source is embedded from the pinned source in source-handoff.json. The source project remains untouched. The catalog now connects the streamed importer and an opening-scene module. The course book and campaign are still pending.

## Ordered acceptance gates

1. Isolated embedded libraries compile; no standalone host or XR dependencies.
2. SDL GPU proof: texture/depth/stencil rendering composed by Reprise without continuous CPU readback; shader conversion and synchronization verified on macOS and Android.
3. Random-access importer through FileSource, bounded parsing, Library staging and progress; separate immutable content, generated caches and player progress.
4. Opening/book/course/clear/miss flow under Reprise, 60 Hz input frames and 30 Hz gameplay, resampled audio, full controller and multi-touch controls.
5. Versioned checkpoint saves with honest Continue messaging, fresh-process restoration, corrupt-load and failed-write checks. Exact-moment saves remain a separately scoped follow-up.
6. Filters, options, widescreen/interpolation, course selection, mod identity, previews, portable research notes and license packaging.
7. Desktop/Android builds and physical-device checks; importer equivalence and fixed-input comparisons; regressions for other Reprise games.

## Known limits

The source README reports the opening, course book and first four courses playable. Later courses contain unimplemented objects. These are inherited reconstruction limits, not completed integration tests. Incomplete courses must remain clearly marked. Older local research handoffs describe earlier milestones and are not current coverage.

No integration gate is complete merely because code was copied or a card is visible. Record commands, source versions and observed results here as each gate is tested.

## Foundation milestone (2026-11-01)

Completed: isolated core/importer/session libraries compile on macOS and Android;
streamed FileSource import and malformed-input checks; exact output comparison
with the pinned standalone source (4,210 files); shared programmable host-target
proof on Metal and Vulkan; macOS/Android packaging and all 13 CTest checks.
Details and evidence limits: [integration-validation.md](docs/integration-validation.md).

## Opening milestone (2026-11-01)

The opening is connected to the existing shelf ID. It runs inside Reprise through
SDL GPU (Metal on macOS, Vulkan on Android), with generated material shaders,
textures, shadows, depth/stencil, particles and original imported layouts.
The host owns the window, event loop, audio and presentation. No XR dependency.

The module advances input at 60 Hz and retains the source's 30 Hz gameplay.
Optional interpolation adds display fraction without advancing simulation.
Audio is resampled from the DSP's rational rate to the host rate. Keyboard,
gamepad and named touch controls use shared input APIs; Start requests the host
pause menu. Shader caches live outside immutable imports. The opening does not
write campaign progress. At the title transition the module explicitly ends;
the course book and course selection are not connected.

Validated so far: opening render captures on Metal and Vulkan, source-clock
comparisons, 1,200 input/audio ticks, repeated capture stability, resize, filter
and restart, Android compilation/packaging. See the opening validation report
for exact coverage and remaining physical-device checks.

Next: book/course/clear/miss orchestration, separate generated course/UI/audio
cache and player-progress roots, checkpoint persistence and fresh-process
Continue. Also validate Android suspend/resume and real touchscreen/gamepad
behavior on physical hardware. Exact-moment saves remain out of scope.
No source project retirement or full research-handoff completion is claimed:
the older research exporters have 178 existing differences to resolve.
