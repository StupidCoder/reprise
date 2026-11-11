# Fort Apocalypse: cylindrical world presentation

Status: initial implementation and headset preview, 2026-11-11. Baseline:
`b7780f7` (Steam Frame launcher, imports and floating-screen gameplay). Portable
renderer checks pass on macOS and ARM64; the two-cylinder prototype is deployed.
The user validated alignment, seam and comfort and reported playable gameplay.
The full milestone gates below remain outstanding. This
is an optional presentation enhancement; the existing flat mode remains available.

Implementation details and current test coverage are recorded in
[Fort's plan](../../games/fort_apocalypse_c64/plan.md#planned-vr-presentation).
The initial slice uses tick-sampled sprites and excludes enhanced effects rather
than claiming completed interpolation/crash-hold support. It uses camera-nearest
map aliases and reports disagreements; canonical mutation semantics remain an
explicit validation gap.

## Intended experience

Show the entire **current level** on the inside of a stationary cylinder around
the player. Horizontal world travel goes around the cylinder; vertical travel
goes up and down its wall. Turning the head reveals the rest of the level.
The helicopter, enemies, terrain changes and effects inhabit that same surface.
This is a curved 2D world with stereo depth from its surface, not newly invented
3D terrain or a helicopter cockpit.

Keep score, fuel, lives and the existing radar readable on a separate floating
HUD. Title/options/debrief and the shared pause menu use the existing flat UI.
Retain current joystick/fire controls. Head movement never steers the craft or
changes the simulation camera. Preserve audio, saves, Continue, imports and
launcher actions through their existing services.

Start with the current craft facing the player's forward direction when entering
the mode. Thereafter the world stays still. Explicit recenter pauses, clears
pending input and reanchors the world with the craft ahead. Add deliberate snap
rotation for seated access to the rear hemisphere, using an otherwise unused
control after auditing bindings; no automatic orbit or continuous camera follow.
Reduced motion disables presentation shake and animated reanchoring. Keep the
floor as an orientation reference, subject to headset comfort checks.

## Findings from the current implementation

| Area | Evidence and consequence |
| --- | --- |
| World size | `data/assets.hpp`: 215 columns × 40 rows of 8-pixel cells, or 1720 × 320 pixels. `Module::pixel_aspect()` is 0.9365. |
| Wrap | `game/player.cpp::copy_window`: the first 40 columns repeat after column 215. Stored column 255 has a special seam cell. Do not expose all 256 storage columns as world geometry. Audit alias mutations and seam behavior before normalizing presentation coordinates. |
| Widescreen | `enhanced/widescreen.cpp` extends a camera snapshot, capped at 21:9 and 24 extra columns per side. It cannot produce the whole world simply by increasing the canvas size. |
| Live terrain | `Game::map_code`, `cell_glyph`, actor views and sink/depot views expose much of the required data. Actors such as tanks, mines, shells and prisoners are also represented in map cells: drawing both blindly would duplicate them. |
| Timing | `shown_frame`/`shown_window` describe a displayed snapshot, while some accessors return current state. The original terrain copy can lag by seven frames. A new renderer needs an explicit sampling policy, not a mixture of these clocks. |
| Effects | `EnhancedView` uses camera-relative clipping and moves particles across wrap copies. `Module` may show a separate `world_` during the crash hold. A panorama must use the same selected display world and avoid advancing effects twice. |
| Rules | The game's documented projectile lifetime and some collision/visibility behavior depend on the original window. Wider presentation must not extend shots, activate new enemies or replace collision input with panorama pixels. |
| Host | `Gameplay::draw()` currently returns one letterboxed 960 × 640 image. `Ring::layers()` supplies quads. Both boundaries need to accept an optional world surface alongside flat UI. |
| Wrappers | `app/quick_save.cpp` forwards module capabilities. Any new presentation capability must also be forwarded there. Save thumbnails should remain normal flat captures. |

These findings come from the repository and its supplied-ROM evidence in
`games/fort_apocalypse_c64/game.md` and `plan.md`. No external game-specific
research is needed. Existing reconstruction gaps remain gaps; VR is not evidence
that they have been resolved.

## Engine and host boundaries

1. **Fort owns a read-only world presentation.** Add named world-space render
   data for terrain, palette, actors, effects, phase and focus position. Resolve
   camera-relative sprite positions inside Fort. Do not export hardware records,
   guest addresses or OpenXR objects through the shared game interface.
2. **Fort renders a portable panorama and HUD.** Begin with CPU RGBA output at
   native resolution, reusing decoded glyphs, palette and art. Draw map-backed
   actors once. Separate terrain/actor composition from the old viewport clipping
   incrementally; do not rewrite the simulation as a prerequisite. Read the
   active display world once per tick. Rendering zero, one or several times must
   not alter rules, RNG, audio or effect progression.
3. **The shared module exposes an optional surface capability.** Proposed
   contract: capability discovery plus an optional periodic image, physical pixel
   aspect, horizontal period, normalized focus position, separate HUD image and
   a discontinuity generation for loads/teleports/level changes. Exact type names
   follow the first working producer. Default modules expose no capability.
   Images have explicit ownership/lifetime through upload. Keep `picture()` and
   `save_picture()` intact. Avoid game-ID checks or Fort casts in the XR host.
4. **The Steam Frame target owns geometry and comfort.** It chooses cylinder
   radius, world pose, HUD placement, input routing and OpenXR resources. Extend
   Gameplay/LauncherPanel/Ring so a world image bypasses 960 × 640 letterboxing.
   Keep layer storage alive through `xrEndFrame`. Pause UI and pointer overlays
   remain ordered above gameplay; suppress hidden launcher cards during play.

For the first panorama, use current live terrain and corresponding world actors
at a defined tick boundary. This deliberately removes the old display-copy lag
from the new view, while collisions and rules retain their existing behavior.
Document and test the resulting visual mismatch near collision edges. Keep the
flat renderer unchanged. Interpolate continuous actor motion across the shortest
wrapped distance; snap on teleports, loads, level changes and crash-world changes.
Do not interpolate terrain mutations or invent offscreen actors.

## Cylinder implementation and cost

The recorded headset runtime advertises `XR_KHR_composition_layer_cylinder`, but
the target does not yet enable or submit it. Prefer testing two 180-degree
cylinder layers sharing one panorama swapchain. A single exactly 360-degree
layer is invalid: the specified central angle is strictly less than 2π. Each
half needs its own pose, image rectangle and aspect ratio. Verify both joins,
orientation, sampling and layer ordering in the headset. See the
[Khronos cylinder reference](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrCompositionLayerCylinderKHR.html).

If compositor joins or support are unsuitable, use an inward-facing textured
cylinder mesh in the existing Vulkan projection pass. Keep the same portable
panorama producer. Flat gameplay remains the safe fallback on unsupported
runtimes or failed resource creation; do not make two renderer implementations
a prerequisite for the first prototype.

Preserving pixel aspect gives a displayed width/height ratio of
`1720 * 0.9365 / 320 ≈ 5.034`. At radius 2 m a full cylinder is about 2.50 m
high. Centering it vertically at seated eye height gives roughly ±32° vertical
coverage from the center. Radius changes perceived distance and physical size,
but does not reduce that angular coverage at fixed aspect. Start here as a
testable comfort assumption; do not stretch the world to fit an arbitrary band.

One 1720 × 320 RGBA upload is about 2.10 MiB, approximately 105 MiB/s at 50 Hz
or 151 MiB/s at 72 Hz, before HUD, copies and synchronization. This is a budget
estimate, not a measurement. Fort is CPU-rendered, so it needs no SDL GPU
readback for this path. Reuse `XrTexture`'s acquire/wait/upload/release discipline;
upload only on changed frames initially. Measure CPU drawing, upload/fence time,
total frame time and missed frames. Add cached terrain or GPU drawing only when
measurements justify it. Test any panoramic VHS treatment separately for seams
and cost; do not silently overwrite the saved flat-mode filter preference.

## Implementation milestones and acceptance gates

### 1. Portable whole-world renderer

Add the world snapshot and panorama renderer inside Fort, with a desktop probe
that exports small test images from user-installed content. Cover each level,
both wrap directions, carved terrain, sinking platforms, prisoners, shots,
explosions and crash holds. Resolve offscreen palette/actor position questions
against existing code and supplied-ROM evidence before marking coverage complete.

Gate: no duplicate actors or padding columns; map changes appear; seam-crossing
actors split correctly across image edges. Identical seeded input runs produce
identical simulation results with panorama disabled, drawn once and drawn several
times. Compare gameplay state and audio independently of presentation caches.
Retain existing flat-frame reference checks and Fort persistence checks.

### 2. Stationary cylinder in the headset

Enable the extension conditionally; display the panorama using the two cylinder
layers, initially frozen for inspecting geometry. Account for the runtime layer
limit including projection, HUD, pause UI and pointer layers. Log negotiated
capability, surface dimensions and layer submission failures.

Gate: native Linux build, successful execution and clean session lifecycle logs;
then user confirmation of upright art, scale, binocular comfort, both joins and
stable positioning while moving the head. Compilation or logs alone do not
validate the visual result.

### 3. Live gameplay, HUD and seated interaction

Connect the optional module capability through quick-save and Gameplay. Add
flat/cylindrical selection through the shared SDL UI with host capability gating;
persist it as presentation preference, not gameplay state. Add recenter and snap
rotation with release-before-press behavior. Keep HUD and pause controls reachable
from the chosen forward direction; pause/reanchor before resuming from tracking
loss. Preserve clear pointer depth/order and button hover feedback.

Gate: play across the seam; die/restart; transition levels; save, quit and Continue;
switch back to flat gameplay without reset; open/close pause; lose/regain focus
and tracking. No stuck controls, audio catch-up or unintended world rotation.
Ask the user to assess following the craft, HUD readability and seated comfort.

### 4. Enhancement compatibility and performance

Complete smooth actors, particles, crash hold and Carnage presentation on the
world surface. Prevent camera-follow shake from moving the entire VR environment.
Keep unsupported combinations explicitly unavailable until validated. Add useful
performance summaries and compare flat/cylinder runs with the same inputs.

Gate: relevant CTest, `./build.sh`, Android build for shared changes, and native
Steam Frame build/deploy through the existing workflow. Test at the negotiated
72 Hz and report measured costs and remaining misses. Update Fort `plan.md` and
Steam Frame `VALIDATION.md` with source versions, commands, compact evidence and
separate compilation/execution/user-visual status. No new catalog badge until the
mode is usable and verified.

## Scope boundaries and remaining decisions

The first mode reveals the whole current level but preserves its original
simulation activity and shot lifetime. Making all enemies continuously active,
extending projectile range or changing collision rules would be a separate,
explicit gameplay enhancement with its own evidence and save implications.

The main unknowns are canonical seam mutations, complete offscreen actor/palette
extraction, compatibility of enhanced effects with world-space rendering, and
comfort when tracking a craft around the rear hemisphere. Milestones 1–3 resolve
these before optimization or a broader engine redesign. Preserve existing save
schemas where possible: transient world snapshots, textures and XR poses are
rebuilt rather than serialized into game progress.
