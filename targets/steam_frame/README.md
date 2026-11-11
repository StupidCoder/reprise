# Steam Frame development target

An opt-in Linux ARM64 OpenXR/Vulkan frontend, separate from game logic. The
stereo diagnostic has run on the actual headset and its scene and both controller
rays/triggers were confirmed by the user. The floor, real catalog ring and offscreen Spy Mouse screen have also been
visually checked. Shared details, documentation, reversible transitions and glowing pointer
feedback are running; Start/Continue gameplay integration is ready for headset testing. See [validation](VALIDATION.md) for exact coverage.

## Mac-driven native build

```sh
./build.sh --steam-frame inspect
./build.sh --steam-frame build
./build.sh --steam-frame launch
./build.sh --steam-frame logs
./build.sh --steam-frame stop
```

The workflow uses SSH with `~/.ssh/degauss_frame_ed25519` to
`steamos@192.168.1.129`; `--host` and `--identity` override these. It uploads an
explicit source allowlist and builds in `~/reprise-dev/build-native` using the
headset's native ARM64 GCC/CMake/Ninja. Packages are installed only in
`~/reprise-dev/bundle`. It does not install OS packages or synchronize user data.
Keep the headset awake during uploads. Failed uploads can be retried.

OpenXR headers are Khronos OpenXR-SDK `release-1.1.53`, downloaded over HTTPS and
verified against `openxr-headers.sha256`. The headset supplies Vulkan development
files and `/opt/steamvr/bin/linuxarm64/libopenxr_loader.so`. That loader has no
SONAME: this development executable intentionally has an absolute dependency on
it. It is not copied or redistributed. The bundle records compiler, OS, source,
header and executable hashes, ELF requirements, runtime version and linkage.
This is a reproducible development procedure on the recorded OS, not a hermetic
release SDK or a distributable Steam package.

This device has **Steam Linux Runtime 4 ARM64**, build
`4.0.20261021.265997` (glibc 2.43), and the launcher explicitly runs its `run`
entry point in the existing user systemd session. The native host also has glibc
2.43. Both linked and executed successfully for the initial stereo test. The
provisional Debian 11/Sniper Docker recipe was removed because it was not tested
against this device. Valve's public deployment page still names Sniper; record
actual installed runtime evidence instead of assuming that example applies.

`--seconds N` requests an orderly OpenXR session exit. SIGINT/SIGTERM also request
exit. `stop` addresses only the Reprise probe service. Each launch has a unique
log in `~/.local/state/reprise/`; `steam-frame-probe.log` points at the latest.
The build is launched via systemd/pressure-vessel, not yet registered as a Steam
Library devkit title. Existing applications and their registrations are untouched.

## Existing game bridge diagnostic

```sh
./build.sh --steam-frame build --bridge
./build.sh --steam-frame launch --content /home/steamos/reprise-dev/test-data/games/spy_mouse_ios
```

The content path must contain an existing Reprise Spy Mouse import. Copy it
separately into isolated test data; never put imports in the executable bundle.
The module retains its normal game logic, data readers and fixed simulation
step. This diagnostic calls the existing `GameModule::draw_gpu` at 960×640 into
an offscreen SDL GPU renderer, reads back RGBA, and uploads it to a 1.5×1 m
world-anchored OpenXR quad two metres ahead. It intentionally has no gameplay
input, audio, pause/save UI or launcher integration yet. Use disposable test
progress: the existing module retains its normal persistence behavior.

The baseline uses separate SDL and XR Vulkan devices. SDL completes readback
before CPU access; a coherent staging buffer feeds the XR device. Images are
acquired/waited before recording, fully overwritten, transitioned from transfer
destination to color-attachment layout, and fenced before release. The previous
upload fence also protects reuse of staging memory. The runtime owns swapchain
images; Reprise never destroys them. Timings separately report draw/readback/
conversion, copy/upload/fence wait, and total bridge cost. XR frame timings
separate rendering from `xrEndFrame`, which may include runtime throttling.
These are CPU wall times, not Vulkan timestamp-query GPU measurements.

The SDL GPU API in the installed headers exposes an `SDL_GPUTexture`, not a
native `VkImage` or an imported Vulkan device. The distinct SDL Vulkan 2D renderer
can take an external Vulkan device and exposes its native texture, but requires
window/surface support and does not satisfy games using `platform::GpuCommands`.
Do not cast opaque SDL GPU handles or assume those two renderers are equivalent.
A zero-copy path remains an investigation; the measured baseline is not a
shipping performance claim.

## Room and shared details development

```sh
./build.sh --steam-frame build --bridge
./build.sh --steam-frame catalog
./build.sh --steam-frame launch --room --catalog --content /home/steamos/reprise-dev/test-data/games/spy_mouse_ios
```

The catalog command exports Reprise's real IDs and artwork from the Mac build.
For the in-progress shared details frontend, build with `build --launcher` and
launch with `launch --launcher`. It uses a separate full native build directory
and the existing Library/Launcher. The default data root is isolated development
data; override with `--data`. Trigger selects; left View/right B goes Back;
thumbsticks scroll and clicking a stick recenters. These newer controls and the
details transition still require validation. `--reduced-motion` removes card
hover; the shared launcher's preference is also respected.

## Headset checks

- Blue/orange markers should remain fixed at different depths while leaning.
- Room left/right beams are green/purple with a soft glow, and each turns white
  on its own trigger. A glowing dot marks the nearest card/window intersection
  and remains visible above the details UI.
- Dashboard focus loss must clear input; held triggers must be released before
  another press is accepted. Test tracking loss separately from focus loss.
- Verify suspend/wake, orderly exit, relaunch and the floating game screen.
- Visual clarity, comfort, colors and screen orientation require a wearer;
  successful frame submission alone does not validate them.

## References

- [Valve custom engines](https://partner.steamgames.com/doc/steamhardware/steamframe/engines/custom)
- [Valve deployment](https://partner.steamgames.com/doc/steamhardware/steamframe/loadgames)
- [SDL offscreen GPU renderer](https://wiki.libsdl.org/SDL3/SDL_CreateGPURenderer)
- [SDL renderer device properties](https://wiki.libsdl.org/SDL3/SDL_CreateRendererWithProperties)
- [SDL Vulkan options](https://wiki.libsdl.org/SDL3/SDL_GPUVulkanOptions)

## Imports

The VR details page's Import action opens a portable Downloads picker inside the
floating window. Select a supported image or ZIP with either trigger; thumbstick
scrolls and Back cancels file selection. The shared import progress screen stays
in the VR session while a worker uses `Library::import`. Once installation has
started it finishes safely; Back does not cancel an active installation. The
source file is preserved. The library refreshes after completion. Per-file ZIP
results are logged and saved by the library in `last-import.txt`.

For an automated batch from this Mac (paths refer to the headset):

```sh
./build.sh --steam-frame stop
./build.sh --steam-frame import --image '/home/steamos/Downloads/Commodore Collection.zip'
./build.sh --steam-frame launch --launcher
```

Repeat `--image` for multiple archives. The import service survives SSH loss and
writes `~/reprise-dev/import-results.log`. Use the same `--data` directory for
imports and launch. A ZIP may partially succeed: inspect the per-file report,
not just the process status. No images or extracted game content enter the
build bundle or repository. Start/Continue now use the shared game factories and save wrapper.

## Gameplay in the room

Choose Start or Continue in a title's details window. Loading verifies imported
files and prepares the module while the room continues rendering. The floating
screen keeps the game's pixel aspect and letterboxes as needed. The shared
pause menu provides Resume, Restart, Quick save/load, sound, options and Quit
to details. Continue loads the existing persistent save format.

- Left stick or D-pad: movement. Right stick: camera where the module supports it.
- Right A/B: primary/secondary actions. X/Y: extra actions; Y also maps Start/F1.
- Bumpers and triggers are forwarded to the corresponding game inputs.
- Point at the screen and hold either trigger to click/drag; the left bumper
  also supplies right mouse. The laser coordinates map through letterboxing
  to the game's logical frame dimensions.
- Left View or right Menu: pause/resume. Within gameplay, B is a game action.
- Thumbstick click: recenter the window. Focus/tracking loss pauses gameplay;
  release held controls before resuming.

This first host does not expose a full C64/Amiga keyboard, two-player input,
multitouch gestures, tilt sensors or every game-specific shortcut. It reuses
existing game logic; all-game playability is not established. VHS filtering
currently uses the existing CPU filter after readback. Native tests cover load,
render/tick/audio submission and pause/return for representative engines; wearer
validation of physical controls, audible sound and performance is separate.

## Fort Apocalypse cylinder preview

In Fort Apocalypse's shared pause menu, select **VR world → Cylinder (preview)**.
For this first preview, turn **Particle effects**, **Smooth motion** and **Carnage
mode** off. Otherwise the game uses its existing flat presentation. Title and
pause screens remain flat; gameplay shows the full current level around the
wearer with a separate HUD. The world image is unfiltered; flat VHS preferences
are preserved. Right-stick horizontal deflection snap-rotates the world by 30°;
release to center before rotating again. Thumbstick-click pauses and recenters,
bringing the craft ahead on resume. View/Menu retains the normal pause controls.

The user has validated alignment, seam and comfort in this development preview.
Full gameplay coverage and consistent native-refresh performance remain open.
See [the implementation plan](FORT_APOCALYPSE_VR_PLAN.md)
and [Fort's current limitations](../../games/fort_apocalypse_c64/plan.md#planned-vr-presentation).
