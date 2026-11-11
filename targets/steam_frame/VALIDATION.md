# Steam Frame validation — 2026-11-11

## Observed device and launch environment

- Hostname `StupidFrame`, Linux ARM64, SteamOS VR 0.6.0, build
  `20261103.6153902`; kernel `7.2.0-g509b71a1bdcd`.
- GCC `15.2.1+r604+g0b99615a8aef-1`, CMake 4.2.3, Ninja; host glibc 2.43.
- Host SDL 3.4.2; Vulkan headers 1.4.363, loader package 1.4.309.
- GPU Turnip Adreno 750; Mesa 26.3.0-devel `6a15f899c8`.
- Active XR manifest resolves to `/opt/steamvr/steamxr_linuxarm64.json`.
  Runtime reports SteamVR/OpenXR 2.19.1.
- Installed Steam runtime is `SteamLinuxRuntime_4-arm64`, depot
  `4.0.20261021.265997`, pressure-vessel `0.20261007.0`, glibc 2.43.
  Its SDL library filename is `libSDL3.so.0.4.18`.

## Executed and visually checked

The reviewed provisional probe was applied to the checkout after `git apply
--check`. It was compiled and linked with the native headset compiler, installed
under `~/reprise-dev/bundle`, and launched through Runtime 4 using a transient
user systemd service. No container image, root installation, or Steam Library
modification was necessary.

[Initial log](evidence/2026-11-11/stereo.log) records:

- Vulkan initialization via `XR_KHR_vulkan_enable2`, Vulkan API 1.0,
  graphics queue family 0, format 43 (`R8G8B8A8_SRGB`).
- Two 1728×1728 eye swapchains, three images per eye.
- Session IDLE → READY → SYNCHRONIZED → VISIBLE → FOCUSED, followed by
  focus/visibility changes and continued frame submission.
- Both hands use `/interaction_profiles/valve/frame_controller_valve`.
- Both hands report tracked aim and repeated independent trigger presses.
- Predicted period 13.89 ms (72 Hz). Sampled complete submission wall times
  around 9.8–12.6 ms after a 90.5 ms first frame. These sparse samples include
  `xrEndFrame` and serialized waits; they are not GPU cost or percentile data.

The user confirmed the scene works, then specifically confirmed that both
controllers aim correctly and respond to triggers. Stereo image submission and
basic visual/controller validation are established. Detailed comfort, angular
stereo calibration, held-trigger focus recovery, actual tracking loss,
suspend/resume and orderly exit are not established by those confirmations.

The initial run used provisional local OpenXR headers identifying as 1.1.53.
They were subsequently replaced with the checksum-pinned upstream release.
The initial build was not the later instrumented game-bridge build. Do not
attribute its execution evidence to later changes.

## Local regression checks

- `./build.sh`: passed; macOS application packaged.
- CTest: library_validation_cache, shell_navigation, shell_motion,
  persistent_saves, touch_and_tilt_controls, host_touch_coordinates: 6/6 passed.
- Probe plus optional bridge: macOS Clang C++20 syntax checks passed with
  Vulkan/OpenXR/SDL development headers. This does not validate Linux linking
  or headset execution of the bridge.
- Android `./build-android.sh`: packaged and signature verification passed
  after the portability fixes; no Android device run.

## SDL GPU bridge, room and catalog

The native bridge was built, linked and executed through Runtime 4. The existing
Spy Mouse module renders offscreen at 960×640 through SDL GPU. Its RGBA readback
is uploaded to an OpenXR quad using a separate Vulkan device. Runtime-owned
images are acquired/waited, overwritten, transitioned to color-attachment layout,
and fenced before release. Zero-copy sharing is not established.

[Timed room/bridge log](evidence/2026-11-11/room-bridge-timed.log) records changing
pixel hashes, focused rendering, ring aim hits, and orderly STOPPING → IDLE →
EXITING. One focused measurement block reports bridge total mean 2.734 ms,
p95 4.375 ms; draw/readback/conversion mean 1.993 ms, upload mean 0.699 ms,
including fence mean 0.554 ms. These are CPU wall times, not GPU timestamps.
Unfocused diagnostics were substantially slower (about 10.8 ms mean); do not
combine those into a focused performance claim. No shipping performance claim
or cross-game coverage follows from this one module.

The user explicitly confirmed Spy Mouse is upright on the floating screen,
the floor fade looks good, all title cards are visible, and cards react to aim.
The catalog atlas contains the real 26 catalog entries and existing artwork;
no ROM/import data is bundled. Cards surround the user, face inward, hover
slightly and highlight/scale when targeted. The floor uses charcoal tiles,
amber seams and a smooth fade. Its height remains a local-space fallback.

## Current unvalidated slice and remaining work

The shared Library/Launcher landscape details renderer and reversible spatial
cover transition are being integrated. They are not covered by the preceding
visual confirmation. The full native launcher compiled, linked, and initialized through Runtime 4. GCC found missing
standard includes across existing game sources. Explicit headers were added;
Clang-only FP pragmas were guarded while GCC retains `-ffp-contract=off`.
Four short formatted-string buffers were enlarged to fit their argument
types, and an invalid scene enum now throws instead of falling through.
Valid game rules and arithmetic were not changed. GCC 15 also reports potential
out-of-bounds handwriting permutation indexing at -O3. For this XR development
target only, that source retains the warning without treating it as an error;
this path remains unaudited and is not validated by the Spy Mouse run. Details actions, Back, scrolling, recenter and reduced motion require
new execution and wearer checks. Game launch/import dispatch remains an explicit
diagnostic boundary in this slice.

Gameplay input/audio, pause/save return, import UI, Steam Library registration,
tracking-loss and held-trigger recovery, suspend/resume, seated floor calibration
and sustained frame-cost/comfort checks remain open. The baseline game screen
has no gameplay input/audio or save UI. Native room coordinate tests passed on
macOS; successful compilation is distinct from headset execution and visual QA.

Unrelated Stunt Car Racer article/plan edits are preserved. No commits or pushes.

The external-details surface regression passes: cover separation changes only
the cover rectangle, subsequent drawing restores it, and the shared Back action
returns to the library. Relevant macOS CTests pass 6/6 after this addition.

The initial full-launcher run rejected the native Frame Back binding because
left Y is a Touch path, not a native Frame path. The corrected native binding
uses left View/right B, per Valve’s custom-engine input reference. Touch fallback
retains left Y. Do not attribute the earlier controller confirmation to these
new navigation actions.

## Live details deployment

The corrected build is recorded in
[launcher-navigation-build.txt](evidence/2026-11-11/launcher-navigation-build.txt).
Its [initialization log](evidence/2026-11-11/launcher-navigation-initialization.log)
shows both eye swapchains, floor, shared Library/Launcher, 26-card atlas and
session startup without the rejected Frame binding. This establishes successful
execution and accepted suggested bindings, not physical Back/recenter/scroll
validation. The untimed details-only session is left running for the wearer to
check selection, continuous cover movement, readable details/documentation and
reverse Back transition. The previously confirmed Spy Mouse screen is disabled
for this check so it does not overlap the details window.

Both macOS and Android packaged successfully after all portability edits. No
new Android hardware validation is claimed. Nothing was committed or pushed.

## Details feedback and glowing pointers

The wearer confirmed the details flow runs as expected and smoothly, including
the requested selection/documentation/Back check. The caveat was that the old
marker laser was obscured by the details window and offered no visible button
target. [Session log](evidence/2026-11-11/details-wearer-confirmed.log).

The next build replaces the room's marker trail with a continuous soft-glow
ribbon and a glowing contact dot at the nearest visible quad intersection.
The ribbon follows the ray and faces the wearer; the contact dot follows the
surface orientation. Both are alpha-blended compositor layers after the UI,
so the details window cannot hide its targeting feedback. Each hand keeps its
color; pressed triggers use white. Four layers are reserved within the runtime's
16-layer budget. The cached 1024×256 glow atlas uploads once, not per frame.
Tracking/focus loss removes the corresponding feedback with the aim pose.

[Build identity](evidence/2026-11-11/laser-build.txt): native ARM64 build/link and
Runtime 4 dependency checks passed. Local ray/quad and beam-frame checks pass,
including parallel eye/ray fallback and vertical/horizontal rays. Headset visual
validation of the glow size, perceived depth and targeting clarity is pending.
This adds a contact indicator rather than button-specific hover styling.

## Import implementation

The wearer confirmed the glowing pointers are substantially better. The next
slice adds a Downloads picker and background import progress to the floating
window, reusing `Library::import` and the shared import screen. The worker owns
its Library instance; UI access resumes through a refreshed library after the
future completes. The worker finishes before its progress state is destroyed.
A batch CLI uses the same import service without constructing an XR session.

Native ARM64 compilation/link and Runtime 4 dependency checks passed. Local
`library_validation_cache` and `import_progress` CTests passed (2/2). A native
batch is importing the five user-provided headset Downloads ZIPs into the
existing isolated development library. The source archives are untouched.
Interactive picker, progress readability and focus-loss behavior still need
headset visual validation; successful CLI imports do not establish those.

### Completed Downloads batch

[Full results](evidence/2026-11-11/downloads-import-results.log),
[build identity](evidence/2026-11-11/import-build.txt).
The native Runtime 4 batch exited successfully in 2 min 38.852 s, with a
reported peak process memory of 4.1 GB. Results:

- Commodore: 13 games, including Cannon Fodder assembled from disks 3, 2, 1.
- Sonic: 4 games. Sonic Rush, Sonic Advance 2 and Sonic Advance 3 were rejected;
  those titles are absent from the current Reprise import catalog.
- Layton: all 6 games.
- Spy Mouse and Captain Toad: 1 each.

The final catalog inventory contains 25 installed games and only Astro Bot
uninstalled, matching the supplied current-catalog coverage. Source ZIPs remain
in Downloads; the transient numbered-image extraction folders were cleaned up.
Imports were verified by the shared importer before installation. This is
import/content validation, not gameplay validation of all these engines.
The VR launcher was restarted with the populated library. Start/Continue in
this XR build still reports the explicit gameplay integration boundary.

## Gameplay integration — in progress

The wearer confirmed removing and reimporting games works well. Start/Continue
now dispatch through the real catalog factories and `with_quick_save`, with
content verification and construction on a worker. Options restore before
Continue. The XR adapter owns an offscreen SDL GPU renderer/GpuSurface, stereo
audio stream, fixed-tick scheduling and aspect-correct letterboxing into the
existing world-anchored window. CPU-picture and SDL GPU modules share this host.
The existing PauseMenu handles save/load, options, restart and return to details.
Focus/tracking loss pauses and clears queued input/audio; launch/resume consumes
held action buttons until released. Source game logic is unchanged.

Native smoke checks have passed for Spy Mouse (SDL GPU), Fort Apocalypse (CPU)
and Sonic (console): load, draw, fixed ticks, audio submission, pause and return.
Fort Apocalypse also saved successfully and Continued in a fresh module instance.
Smoke saves/options use a separate gameplay-smoke-data root; imports are linked
read-only by convention from the development library. These checks do not prove
headset controller usability, audible sound or visual comfort. Shared CTests
shell_navigation, persistent_saves, touch_and_tilt_controls and
host_touch_coordinates passed 4/4.

### Gameplay build deployed

[Build identity](evidence/2026-11-11/gameplay-build.txt). The deployed XR session
initializes both eyes and the real catalog without rejecting the new native
Frame bindings. Native gameplay smoke logs:

- [Spy Mouse](evidence/2026-11-11/smoke-spy.log)
- [Fort Apocalypse](evidence/2026-11-11/smoke-fort.log)
- [Persistent save / Continue](evidence/2026-11-11/smoke-fort-save.log)
- [Sonic](evidence/2026-11-11/smoke-sonic.log)
- [Pandora's Box](evidence/2026-11-11/smoke-pandora.log)
- [Captain Toad](evidence/2026-11-11/smoke-captain.log)

Each representative engine passed rendering, fixed ticks, audio submission,
pause and return. The final deployment also improves menu-toggle behavior,
consumes interrupted pointer drags with a release, and pauses on recenter.
These refinements and physical controller/audio interaction await wearer checks.
A headset check has been requested for Spy Mouse pointer dragging/audio and
pause/return, plus console controls. The gameplay adapter is target-local;
macOS/Android game logic and host implementations were not changed in this slice.

Limits: arbitrary keyboard commands, second-player input, multitouch and tilt
are not exposed yet. GPU games still use the measured readback/upload bridge;
VHS is the existing CPU filter in this first gameplay host. Imported or
successfully launched does not mean every game's reconstructed routes are complete.

## Wearer gameplay confirmation

The user subsequently confirmed that gameplay seems to work fine. This is
positive wearer validation of the deployed gameplay slice following the requested
controls/audio/pause check; it does not establish complete playthrough coverage,
all controller combinations or comfort across every imported title.

The next requested feature is an opt-in Fort Apocalypse VR presentation that
wraps its horizontally repeating landscape around the player. No game-specific
VR mode is included in this baseline.
