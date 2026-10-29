# Adding a game to Reprise

Start here when adding or integrating a game. Read [AGENTS.md](AGENTS.md),
[RECONSTRUCTION.md](RECONSTRUCTION.md), and any instructions in the engine's
folder first. This guide records the user's requirements of 2026-10-29 and
connects them to the current framework. Requirements below describe the target;
existing games are not proof that every requirement is already implemented.

## 1. Ownership, scope and documentation

- Use a stable `games/<game>_<platform>/` folder and game ID. IDs also identify
  imports, settings and saves: do not casually rename them after release.
- Collections are optional launcher groupings, not engine ownership. Shared
  families can share code without duplicating it per catalog entry.
- Single games keep lowercase `plan.md` and `game.md` in their engine root.
  Layton keeps one pair per game under `games/layton/docs/<game-id>/`.
  Sonic uses one collection-wide pair under `games/sonic/docs/`; distinguish
  each game's status within it.
- `plan.md`: current playable scope, remaining tasks, open bugs, missing paths,
  known differences, validation gaps, reproduction steps and acceptance criteria.
  Distinguish original bugs from reconstruction bugs and optional enhancements.
- `game.md`: supported image/revision and hashes, recovered formats and rules,
  script semantics, RNG behavior, original bugs/quirks, evidence references and
  uncertainties. Mark observations, interpretations and hypotheses separately.
  Reference shared research instead of copying it into several game documents.
- Curated notes belong in Git. Keep large traces, dumps and user game images in
  ignored evidence storage. A local absolute `.analysis` symlink is not portable
  documentation; name the evidence and explain how it can be regenerated.
- Check current changes before editing: other agents may be working here.
  Keep changes scoped; never discard their work, fabricate research or create
  empty documents just to satisfy a filename check. Commit/push only when asked.

## 2. Clean, portable reconstruction

Ship idiomatic C++20 with SDL 3 and standard-library services. Reprise targets
multiple desktop operating systems and Android, including touch devices and
handhelds with integrated gamepads. No Cocoa/UIKit/Win32-only game services,
native UI widgets, hardcoded developer paths or platform-specific audio codecs.
Put necessary OS plumbing behind the shared platform boundary with supported
portable implementations; do not hide a mandatory platform dependency behind
an `#ifdef` and call the game portable.

Model gameplay using owned, named types: actors, rooms, routes, cameras, timers,
script state and sound channels. Do not ship a CPU emulator, a RAM image,
guest-address state, an original memory map or a routine-by-routine translation.
Temporary emulation and close translations are research/validation instruments.

Rewrite native logic and interpreters in clean C++. Execute original data-driven
content from the imported files: scripts, layouts, object definitions, animation
sequences, timings and resource tables. Do not hand-code a native replacement
for a sequence that was data in the original. Preserve arithmetic and update
order when they affect meaningful game behavior; document deliberate departures.

Derive game-specific mechanics from the user's own images and execution of those
images. Do not consult existing game disassemblies, decompilations, ports, symbol
maps, asset maps or game-specific reverse-engineering guides. General hardware
and tool documentation is permitted. Editorial release/development anecdotes
are a separate, attributed catalog concern, not reconstruction evidence.

## 3. Randomness: preserve meaning without rebuilding a machine

- Reconstruct the game's generator when its sequence matters: seeded levels,
  puzzles, procedural worlds, reproducible routes or gameplay decisions whose
  distribution/sequence affects the rules. Preserve seed and generator state.
- Use a standard generator for insignificant cosmetic details such as particles,
  visual grain and audio noise. Separate cosmetic and simulation streams so
  changing graphics settings or refresh rate never changes gameplay randomness.
- If recreating the original generator would require extensive system-memory,
  raster-beam or incidental hardware-state reconstruction, use a standard
  algorithm instead. Do not rebuild a machine merely to obtain random bits.
  Preserve meaningful ranges/distributions and document the compatibility limit,
  especially when original seeds no longer generate identical content.
- Prefer explicit engine state (for example a suitable `<random>` engine), not
  global `rand()` or repeated wall-clock seeding. Support fixed seeds in tests;
  save the state needed for continued gameplay. For cross-platform deterministic
  replays, specify bounded sampling too: standard distribution implementations
  are not guaranteed to produce identical sequences across standard libraries.

This is an explicit user-approved exception to strict random-sequence fidelity
in older reconstruction guidance. Record each decision in `game.md`.

## 4. Importing and catalog integration

Inspect [app/game_entry.hpp](app/game_entry.hpp), [app/library.cpp](app/library.cpp)
and [app/catalog.cpp](app/catalog.cpp). A game supplies a `GameEntry` with a
stable ID, title, machine, media description, importer, previews and start factory.

1. Identify supported revisions from the supplied files; reject unrelated data
   with `NotThisGame`, and recognized but damaged/unsupported releases with a
   useful error. Validate sizes, bounds, archive paths and decompression limits.
2. Return `ImportResult` with relative file names, decoded data and release
   identity. No absolute paths or `..` traversal. Declare an appropriate
   `largest_image`; use `part`/`parts` for genuine multi-disk imports.
   Large-image `import_into` receives an `ImportProgress` callback: report a
   readable phase and a measured fraction from 0 to 1 (negative for unknown).
   Imports run on a worker; callbacks must not draw UI or use the host renderer.
   The library reports its own verification phase after extraction.
3. Let `Library` own staging, checksums and installation. Do not write into the
   application bundle or rely on source-image access after import. Android
   document URIs are handled by platform plumbing, not by individual games.
4. Decode previews into `Preview` images, optional `PreviewSound` waveforms and
   optional `ui::Model` viewers. These represent actual imported data, not cover
   artwork. Fail clearly when decoding is incomplete.
5. Register the module in root CMake and the catalog. Supply release year for the
   relevant platform, description and attributed development notes. Collection
   membership is optional. Keep unavailable engines explicitly marked pending;
   a catalog card must not imply that gameplay/import is connected.
6. Review packaging and Android dependencies. Source ROMs/IPAs/disks, extracted
   game assets and user saves never belong in Git or either app package.

Use existing entries as interface examples, not as authorization to copy game
logic. [Spy Mouse's adapter](games/spy_mouse_ios/reprise/entry.cpp) demonstrates
embedding a previously standalone engine without launching another app. Use
`LegacyCopy`/library adoption for prior installs: copy conservatively, preserve
originals and existing Reprise progress.

## 5. Game loop, input, audio and rendering

Implement [GameModule](app/game_module.hpp). The host owns the window, event
loop, audio device, timing, pause menu, notifications and presentation.

- `step_seconds()` and `tick(GameInput)` define the fixed simulation cadence.
  Never speed up physics because the display is faster. Rendering must not
  mutate simulation state, advance audio or consume gameplay RNG.
- Consume `GameInput`, not a private SDL polling loop. Keep press/move/release
  `pointer_events` in order for gestures/path drawing; coordinates are already
  in game units. Respect captured relative motion, pointer visibility, pause,
  cancellation and focus loss. Test touch and gamepad navigation without a
  keyboard. If analog axes, multitouch or another facility is missing, extend
  the shared input contract and adapters instead of bypassing the host.
- `audio()` supplies interleaved stereo at the requested rate. Use portable
  decoding and account for the game's timing; do not open a second audio device.
- CPU-rendered games return `picture()` and `pixel_aspect()`. GPU-rendered games
  expose `gpu_frame_size()`, `draw_gpu()` and `attach_gpu()` using the host's
  `GpuFrame`/`GpuSurface` in [host.hpp](shared/platform/host.hpp). Draw into the
  supplied target; do not create another window or own final presentation.
- The current `GpuFrame` exposes SDL_Renderer, a 2D rendering interface. It is
  not a general programmable 3D API. For 3D, use a portable software pipeline or
  introduce a shared, portable GPU backend with proper depth/render-target
  support. Do not add direct Metal-only code or promise arbitrary shaders on
  the existing SDL_Renderer contract. Preserve projection, depth, clipping,
  texture and transparency semantics that matter to the original picture.
- GPU `picture()` must still support one-off screenshot capture through the
  attached surface. Continuous rendering should not read back to the CPU every
  frame. Recreate targets on resize/orientation/device reset and preserve host
  renderer state. Test pointer mapping after changes in aspect and orientation.

## 6. Filters, persistent saves and lifecycle are required

Every game must support framework screen effects, including VHS, whether it
renders on the CPU or GPU. Filtering is presentation-only: apply it to the game
frame, then compose the pause menu and save/load notifications at app resolution.
Never filter those overlays or bake effects into the game's simulation/image data.

**Screen filter stage (implemented 2026-10-29, first used by Spy Mouse):**
offer `app::screen_filter_option(level)` among the game's options and return the
level from `vhs_level()`; nothing else is game-specific. Picture games get the
look from `shown_picture()` as before. For GPU-drawn games `app::screen_effect()`
hands `Host::present_game_gpu()` a `platform::ScreenEffect`: the game then draws
its frame at its logical size, and the host applies the effect while composing
the window, before the note and pause overlay (which stay sharp). With the
filter off the frame takes the unchanged, unfiltered path. The effect runs as a
fragment shader on SDL's GPU renderer (preferred at start-up: SPIR-V for Vulkan,
MSL for Metal, built from `shared/ui/shaders/*.frag` by `tools/build_shaders.py`);
on any other renderer the frame is read back once and filtered on the CPU. The
filter option is a presentation option (`GameOption::presentation`): it is kept
in the options file and survives loading a save, which restores only game
options. `tools/screen_effect_check` compares the shader with the CPU form on
the machine's renderer. New effects follow the same pattern (`ui::vhs_effect`).

Implement portable `write_state()`/`read_state()` and integrate
[app/quick_save.cpp](app/quick_save.cpp). The current `persistent_saves()` allowlist
must include the new ID after its save contract is validated. The wrapper handles
on-disk generations, content checks, screenshots, dates and Continue availability.
A successful “Saved” message means the durable write actually succeeded.

Save owned gameplay values, relevant RNG/interpreter state, timers and pending
logic. Rebuild pointers, imported resource bindings, SDL/GPU objects and transient
caches; never serialize addresses or raw native structs. Version schemas and
validate sizes/content identity before replacing the live game. A failed load
must preserve the running state. Include enhancement/cheat options consistently.

Test save → quit → relaunch → Continue, load after progress has changed, corrupt
or incompatible saves, missing screenshots, failed writes and Android suspend/
resume. Keep original-slot saves separate where needed. Saves live outside
imported files so removing and reimporting content does not discard progress.
Document whether screenshots are raw or filtered; retain the current raw gameplay
capture convention unless the framework deliberately changes it.

## 7. Modding, enhancements and cheats

Make the engine modable and enhanceable. Separate rules, imported definitions,
rendering and platform services. Provide explicit, validated override seams for
reasonable asset/rule mods rather than hardwiring every choice. Keep original
imports immutable; mods need their own storage and version/identity. Define how
active mods affect save compatibility; the current import fingerprint must not
silently treat different effective content as identical. Do not execute arbitrary
native code from a mod or promise a mod API that has not been implemented.

Where reasonable, offer optional aspect ratios from the original through 21:9,
and smooth motion up to 120 Hz. Use `set_canvas()` and
`set_frame_fraction()` for adaptive presentation and interpolation. Extend the
view/camera rather than stretching pixels. Keep gameplay activation/collision
bounds faithful unless explicitly enhanced. Snap interpolation across teleports,
loads and scene changes; avoid input latency and double interpolation. State
which scenes cannot widen safely and use an intentional letterboxed fallback.

Expose options through `GameOption`/`set_option()` and the shared pause menu;
choice zero represents original behavior. Preserve a faithful mode. Useful
cheats can exceed infinite lives: navigation maps (as in Mind Walker), hints,
route overlays, forgiving timing, checkpoints, level selection, invulnerability,
resource assistance or puzzle help. Implement a thoughtful set appropriate to
the game, with clear descriptions and consistent persistence. Cheats must not
silently replace the normal rules. Document fixes for original bugs as optional
fixes when appropriate, with the original behavior and evidence retained.

## 8. Explain the game and create its artwork

Provide meaningful rules/objectives and controls, especially multi-stage games.
Identify lesson candidates in `game.md`: algorithms, AI, data storage, physics,
room logic and original quirks. Use imported-data illustrations, tables and
interactive demonstrations when they clarify a finding. Explain reconstructed
mechanics honestly; do not imply that our C++ is the original implementation.
Keep readable portrait layouts in mind when adding documentation pages.

Catalog artwork is original editorial illustration, never a substitute for
research evidence or an extracted screenshot. Use the image-generation skill
for new raster art; inspect the output and record provenance in
[resources/covers/PROVENANCE.md](resources/covers/PROVENANCE.md). Store the final
asset as `resources/covers/<game-id>.png`. Keep typography/status badges in SDL.

Suggested prompt template (replace brackets with known, supported themes):

> Original full-bleed landscape catalog illustration for [title/platform],
> approximately 2.2:1. Match Reprise's collector's screenprint style: crisp
> geometric forms, restrained dry-ink grain and halftone, [collection palette].
> Depict [recognizable, evidence-supported theme/setting] with [distinct focal
> composition], readable at small card size. No text, lettering, logo, UI or
> border. Do not copy publisher box art. This is interpretive artwork, not a
> screenshot or a claim about an unverified level. [Reference images] are style
> references only; preserve the game's own identity.

## 9. Validate and hand off honestly

Use focused checks for importer rejection/corruption, fixed-tick behavior,
seeded generation, save round trips, options, input mapping and rendering.
Compare against the user's original image and recorded execution, documenting
both covered and untested branches. A matching screenshot or successful launch
is not proof that the whole game is reconstructed.

Build `./build.sh`, run relevant CTest checks, and build `./build-android.sh` for
portable changes. See [Android instructions](targets/android/README.md). Exercise
resize, portrait/landscape, pause/resume, audio, touch/gamepad, effect off/on,
120 Hz presentation where supported, and fresh-process save restoration. Do not
claim physical-device validation from emulator startup. Report untested targets.

Before handoff, update plan.md/game.md, record source/build versions and concrete
reproduction commands, inspect packaged assets/licenses, and report what plays,
what remains and any compatibility changes. Keep external source projects and
other agents' working changes intact. Do not mark a requirement complete because
it has a menu label, empty method, placeholder card or an untested fallback.
