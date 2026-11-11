# Stunt Car Racer: stereo window and immersive presentation

Investigation: 2026-11-11, following committed/pushed Fort VR baseline `5fe894a`.
An initial mesh extraction/reference renderer and frozen XR stereo study are now
implemented. Live race presentation, direct GPU drawing and immersive rendering
remain unimplemented. No wearer validation has been received for the study yet.

## Recommendation

Build one portable 3D scene representation and a new perspective renderer, then
offer two presentation modes: a stereo floating window and an immersive seated
view. Validate the scene in a stationary stereo window first. It keeps the
existing cockpit art useful and provides a comfortable place to inspect scale,
depth and interpolation before attaching the viewpoint to the moving car.
Keep the faithful original renderer as the default and comparison oracle.

Simply offsetting the existing camera twice is a useful short experiment, but
not the proposed long-term renderer: it preserves the low-resolution angular
projection and screen-space approximations. Do not grow that experiment into a
second enhancement architecture before testing whether its stereo is useful.

## Findings in this checkout

| Existing seam | Implication |
| --- | --- |
| `data/track.hpp`, `data::road()` | All eight circuits already decode into world-space road cross-sections. Use imported definitions directly; no track tracing or new collision meshes for gameplay. |
| `render/race_view.hpp::Camera` | Explicit x/z, eye height, yaw/pitch/roll. Height units are four times plan units; normalize Y by 1/4 before perspective and stereo. Physical meters per plan unit remain a presentation calibration, not a recovered fact. |
| `render/race_view.cpp::Projector` | A 256 × 128 angular projection, approximately 45° horizontally, with floor/angle quantization. It is not a normal pinhole projection that can directly consume OpenXR eye matrices. |
| `draw_race_view()` | Visibility walks nearby track segments and stops when one leaves the original window. Immersive rendering must use the whole circuit with spatial/frustum culling and a depth buffer; rotating this old camera alone would omit geometry. |
| `draw_opponent()` | Axle corners have world positions, but body/wheel faces are partially constructed in screen space. A stable 3D mesh must be newly authored from these proportions and marked as enhancement art. |
| `render/race_screen.*` | Cockpit/dashboard are a separate composition pass. This is an excellent seam for keeping the frame and instruments at window depth while the track has stereo depth. |
| `game/module.cpp::picture()` | Assembles camera, opponent and cockpit from read-only race state. Extract this assembly into a shared presentation snapshot used by both renderers. |
| `Module::step_seconds()` | Race simulation advances by 0.13 s (~7.69 Hz), menus at 50 Hz. Headset refresh must not become the physics rate. |
| `set_frame_fraction()` | Currently advances wheel animation; it does not interpolate car motion. Sample shared animation once per display frame, then render both eyes from the same snapshot. |
| `platform::GpuFrame` | Currently exposes an SDL_Renderer and a 2D target, not a general stereo 3D command interface. Fort's periodic RGBA `WorldSurface` is also unsuitable for arbitrary immersive geometry. |

The game's `plan.md` has a stale top-level summary: current code includes the
opponent and league. Other recorded gaps remain relevant: drawbridge animation,
some reversed/tall track cases, incomplete audio, and no module persistent-state
implementation. Do not claim VR fixes these or silently expand reconstruction
scope. Derive any further game-specific findings only from supplied-ROM evidence.

## Presentation contract and renderer

Keep race rules, imported geometry and presentation separate:

- Stunt Car Racer produces immutable scene geometry/material IDs, dynamic actor
  transforms, car pose and cockpit/UI data. The original renderer continues to
  use the original camera and visibility rules. Enhanced visibility does not
  change opponent AI, collision/contact, damage, boost, laps or RNG.
- Add a small optional 3D presentation capability at the game/host boundary.
  Prefer owned mesh handles plus snapshot transforms over copying the circuit
  each eye/frame. Define lifetime, resource invalidation and discontinuities.
  Forward through the quick-save wrapper even though this game does not yet
  support persistent saves. No OpenXR types in the engine.
- The XR host supplies eye transforms/projections, render size, depth/MSAA
  targets and device lifetime. Reuse its Vulkan device for direct rendering;
  avoid two full-resolution CPU readbacks/uploads per frame. A portable scene
  contract permits Metal/SDL GPU backends later without requiring them for the
  first opt-in XR slice. Do not infer that the current SDL_Renderer boundary
  already provides this backend.
- Begin with flat original palette colours, road top/sides, markings, ground and
  a simple sky. Add stable material IDs and road-distance/across-road UVs now;
  actual texture/sky artwork, lighting and grass detail come after geometry and
  comfort validation. Visual additions never feed physics.
- Measure recommended eye-size rendering with depth and supported MSAA (try 2×,
  then 4× if affordable); resolve to the XR image. Native-resolution rendering
  means negotiated render-target pixels, not upscaling the 256 × 128 image.

For immersive projection, use the predicted-time per-eye poses and FOV returned
by `xrLocateViews`, and submit matching projection-layer metadata. Query view
configuration dimensions and sample support rather than hardcoding them. See
[Khronos rendering specification](https://github.com/KhronosGroup/OpenXR-Docs/blob/main/specification/sources/chapters/rendering.adoc).

## Mode A: stereo floating window

Render the track into eye-specific textures displayed on coincident left-only
and right-only quads. The cockpit art/instruments remain identical in each eye,
at the plane of the window. Explicitly separate the transparent windscreen mask
from the overlay so wheels, flames, smoke and messages retain deliberate order.
Do not apply stereo separation to the entire finished game image.

Use parallel eye cameras and asymmetric/off-axis projections, with convergence
at the virtual screen plane. Derive the relationship from eye positions, panel
geometry and a documented world-scale mapping; a random lateral camera offset
does not establish comfortable depth. Start with conservative depth behind the
window. Avoid near objects protruding through the cockpit border. Test a frozen
scene first, including head translation and both window edges. If the original
angular-renderer experiment creates distortions, stop it and use the perspective
path rather than compensate with arbitrary eye toe-in.

Menus and shared pause UI remain normal 2D. This mode remains a useful permanent
option even after immersive VR exists.

## Mode B: immersive seated cockpit

Compose interpolated vehicle position/orientation, a calibrated seat transform,
and current tracked head pose. Head tracking updates at display rate even though
physics does not. The driver can look around the entire circuit and lean within
the seat; looking must never steer or change collision geometry.

The old cockpit bitmap is not a complete 3D cockpit. Start with a deliberately
simple stable cockpit/roll-bar reference and a car-local dashboard using existing
instrument artwork. Do not stretch the old border around the head. Road sides,
undersides, rear views, opponent geometry and horizon need inspection from
directions the original never showed.

Offer explicit comfort choices: car-following versus horizon-stabilized
pitch/roll, reduced camera shake, and fade/reanchor for crane/reset transitions.
Retain translation caused by the actual car; label any comfort adjustment as
presentation only. Recenter calibrates the seat and clears input. On tracking
loss pause physics/audio and require released controls before resuming. Menus
remain reachable without turning with the car. Do not assume stunt jumps and
crashes will be comfortable because the stationary Fort cylinder was.

## Small implementation gates

1. **Read-only scene extraction and mono perspective diagnostic.** Produce all
   circuit meshes, a consistent coordinate conversion, player/opponent snapshots
   and cockpit layers. Inspect Little Ramp and a tall/banked circuit from front,
   side, below and behind. Compare cross-sections and original-view landmarks;
   pixel equality is not expected from a new projection. Verify repeated scene
   extraction and eye rendering do not advance race/RNG/audio/animations.
2. **Motion reconstruction for presentation.** Keep previous/current tick poses,
   interpolate translations and wrapped orientations, and snap at spawn, crane
   relocation, track changes and other discontinuities. Standard interpolation
   adds up to one 130 ms tick of presentation delay: measure responsiveness before
   choosing it. Compare bounded prediction as a separately documented option;
   never run extra physics steps to obtain more visual samples. Test opponent,
   wheel and dashboard synchronization as well as the player's camera.
3. **Frozen then moving stereo window.** Wire the optional scene capability to
   XR and direct GPU drawing. Verify eye order, zero-disparity cockpit, road scale,
   occlusion, AA and head motion in-headset. Record per-eye GPU work, whole-frame
   CPU/GPU cost and missed deadlines against the flat baseline.
4. **Immersive view.** Add tracked seat pose and the simple cockpit. Validate a
   stationary start, slow drive, banking, jump, crash and crane recovery in that
   order with the user. Stop at comfort failures and adjust the presentation.
5. **Visual enhancements.** Add road textures, ground and sky incrementally, with
   optional original-colour mode. Validate performance after each change. Keep
   imported game data external; newly authored assets need provenance.

Run existing Stunt Car Racer physics/reference checks for touched engine paths,
relevant host/UI CTests, Mac/Android builds for shared changes, then native ARM64
deployment. Report compile, execution, geometric checks, performance and wearer
validation separately. Save compact input traces, hashes and diagnostic images.
Do not advertise smooth high-refresh or immersive support until those gates pass.

The first implementation should be gate 1, followed by a frozen stereo window.
This answers the scale/projection question cheaply while retaining a direct path
to full immersive rendering and later visual improvements.

## First slice: frozen stereo study

The diagnostic deliberately avoids introducing a pixel-only game API as the
long-term 3D contract. The portable mesh is the reusable result; fixed eye PNGs
are a temporary inspection transport. No per-frame readback/upload pipeline has
been added. No race module, original renderer or physics function was changed.

Reproduce locally after `./build.sh`:

```sh
./build/stunt_scene_check "$CONTENT" build/stunt-stereo-study 0
./build/stunt_scene_check "$CONTENT" build/stunt-stereo-tall 7
ctest --test-dir build -R 'stunt_scene_geometry|fort_world_render|shell_navigation|host_touch_coordinates' --output-on-failure
./build/stunt_car_racer_physcheck "$CONTENT" .analysis/stunt_car_racer_amiga/work/race_lr1.bin 0 race 2
```

`CONTENT` is the user's installed Stunt Car Racer content folder. The last command
requires the existing local reference capture; the documented capture loses its
first driving frame. Starting at frame zero gives a one-frame mismatch and is
not the validated comparison. Starting at frame 2 gives 467/467 exact frames.
The historical checker returns zero even for mismatches: inspect its summary.

The all-track check reported 2,276–3,842 triangles per circuit including the
ground, no missing within-segment road intervals, and unchanged original start
views. Little Ramp and Roller Coaster overview images and the composited left
eye were visually inspected on the Mac. The underside diagnostic hides ground
to expose the two-sided road surfaces; it is not a new undercarriage model.

The stereo export is 1280 × 800, with the 1024 × 512 windscreen rendered at
2048 × 1024 then downsampled. Cockpit masking is derived by comparing the original
composition over two different flat view colours, retaining identical overlay
pixels in both eyes. The panel is 1.5 × 0.9375 m at 2 m. Camera offsets assume
64 mm eye separation and 10 m per normalized scene unit. These are presentation
calibration values, not recovered physical dimensions. No toe-in is used.

Build/deploy with the usual `./build.sh --steam-frame build --launcher`. Copy only
`stereo.txt`, `left.png` and `right.png` into the headset's dedicated
`~/reprise-dev/stunt-stereo-study` folder, then:

```sh
./build.sh --steam-frame stop
./build.sh --steam-frame launch --room --stereo-study /home/steamos/reprise-dev/stunt-stereo-study
```

Thumbstick-click reanchors the window. This standalone study does not contain the
catalog or game controls. Restore them with stop followed by
`./build.sh --steam-frame launch --launcher`. Generated pictures remain in ignored
build/development storage; no imported art or ROM data is bundled in source.

Native ARM64 linking and OpenXR startup succeeded. The log confirmed both eye
swapchains, eye-specific study layers, a runtime separation of 64 mm and the
first submitted frame. The logged runtime separation matches this export's
nominal value. This proves execution, not depth/comfort. Mac and Android packaging
passed; synthetic renderer and relevant host CTests passed. Native headset
rendering-performance claims require the future live GPU path, not these cached
images. The final diagnostic-only below-view camera correction was built locally
after the XR executable; it does not change the deployed Little Ramp eye images.

Compact [execution and check evidence](evidence/2026-11-11/stunt-stereo-study.txt)
includes binary/image hashes. No rendered imported artwork is checked into Git.

## Live stereo practice preview (2026-11-11)

The wearer confirmed the frozen study has visible depth and is not garbled.
This validates that study's stereo presentation, not live driving or comfort.

The next slice exposes an immutable mesh, camera, cockpit overlay and aperture
through a portable SceneSurface contract. The Steam Frame host draws the same
snapshot twice through Vulkan into OpenXR-owned images, using actual eye
positions and off-axis projection relative to the floating window. Depth testing
and up to 4x MSAA replace the CPU study renderer. The windscreen is 1024 x 512
per eye; this is not an immersive full-headset-resolution view. GPU resources
stay behind the host boundary. Each acquired image is rendered, fenced and
released before submission; no scene readback/upload occurs per frame.

Enable **VR view: Stereo practice preview** in the game's options, then choose
Practise. Flat is the default. League races retain the original view until the
opponent has a proper 3D representation. The original cockpit/HUD remains a
shared transparent overlay. Track markings, scenery, profile shading and a
new opponent model remain future work.

**VR smooth motion** optionally interpolates previous/current camera snapshots
with shortest-path wrapped angles, without changing simulation ticks. It is off
by default and introduces up to one 130 ms tick of presentation delay. Phase
and recovery changes snap rather than blending discontinuities. The HUD uses
current state. Camera/overlay alignment, head translation, crane recovery and
smoothing comfort need wearer validation.

Mac and Android packaging and native ARM64 build/install passed. Four relevant
CTests passed; the Little Ramp oracle remains 467/467 exact. A 500-tick module
comparison sampled 1,080 live scene snapshots at three interpolation fractions:
repeated extraction, original/future flat images and simulation timing remained
unchanged. The launcher starts in SteamVR; the live GPU scene subsequently ran for at least
7,200 frames. Both-eye scene wall time averaged 0.979 ms (0.909 ms fence wait).
Whole-frame render-and-fence timing was 13.968 ms mean / 23.105 ms p95 in a
600-frame sample, exceeding the 13.89 ms budget; this is not a claim of steady
72 Hz. Focus loss paused gameplay. See the wearer follow-up below for visual validation.
The log reports both-eye wall/fence averages every 360 live scene frames; these
are CPU wall measurements, not GPU timestamps.

Wearer follow-up: the live track looked mostly correct, but banking appeared
reversed. Comparison with the original angular projector confirmed a roll-sign
error when converting screen-down rotation to the world-up camera basis. The
basis is corrected; a regression test checks both signs against the original
projector's horizon direction. Synthetic checks and the 500-tick module check
passed again. The wearer then confirmed corrected banking and described the
live view as distinctly Stunt Car Racer with cleaner, smoother stereo graphics.
This validates the tested live presentation; all-track coverage, prolonged
comfort, head-translation alignment and the smoothing toggle comparison remain
separate checks.

### Eye-level gameplay windows

Wearer feedback exposed a low anchored window after recentering: the stereo
window's off-axis view then showed mostly road. Non-immersive gameplay windows
now start upright at current eye height, two metres ahead, and follow head
position and horizontal facing after a 0.5 s dwell outside a 20 cm / 25 degree
dead zone. A critically damped spring eases toward the moving target. Pitch
and roll do not tilt the window. Recenter resets placement immediately;
reduced motion disables automatic following. Immersive world surfaces retain
their anchor. Pointer tests and stereo layers share the updated panel pose.

Native build/install and launcher restart passed. Local room-coordinate checks
cover delay, seated height, monotonic settling, yaw, upright vertical-look
fallback and reduced-motion recenter. Relevant host CTests passed. Wearer
comfort and practical aiming during follow remain to be validated.

The wearer confirmed following works but reported excessive sensitivity and a
long tracking tail. The revised follower stops once within 1 cm and 0.5 degrees
of its target, discards spring velocity and starts a fresh dwell for any later
qualifying movement. It freezes without a final snap. The wider threshold lets
the wearer inspect the window edges; spring rate increases from 6 to 8.
Regression checks cover a 20-degree glance, arrival within one second after the
dwell for a 50 cm move, no continued small-motion tracking, and a fresh delay
before restarting. Revised feel awaits wearer confirmation.

## User-prioritized next milestones

Order requested: enhanced materials first, then missing presentation details and
opponent, then whole-frame pacing, then immersive cockpit VR. Materials must
first improve distance perception in the existing stereo window.

The first material preview adds generated asphalt and grass albedo sources
under `resources/stunt_car_racer_amiga/materials` (prompts/provenance alongside).
`VR materials` selects Textured (default) or Flat colours. Road UVs follow arc
length, with the texture phase fitted around the circuit; grass uses world X/Z.
The portable material descriptor supplies a resource-relative image path. The
XR host uploads an sRGB image array once per mesh, builds linear-light mips, and
uses trilinear mirrored repetition to avoid hard source-image border seams.
No per-frame texture upload or CPU scene readback. Original triangle positions
and simulation remain unchanged. Full immersive presentation and league stereo
remain future milestones, not part of this material slice.

Wearer validation: the textures work well and make depth much easier to judge.
The material slice is accepted visually. Native execution logged 11,880 live
frames, both-eye scene wall mean 1.032 ms / fence wait 0.917 ms. Whole-frame
render/fences remain about 14 ms mean with ~23.5 ms p95; pacing is still the
third milestone in the requested order.

## Track details and league stereo

The next slice restores profile-boundary surface colours (including dark jump
faces) using the same decoded-profile rule as `race_view.cpp`, start-line
placement, and the decoded kerb colour phase. Original one-pixel edge lines
become narrow world-space ribbons around the circuit: their width and extended
visibility are VR presentation choices. Horizon polygons retain decoded angular
positions and colours on a distant camera-centred dome; concave polygons are
ear-clipped. They remain effectively distant rather than new physical hills.

The opponent is a new low-poly world-space wedge and four wheels, anchored to
the existing segment, axle spacing, lane and suspension lifts. It follows road
banking and crosses segment boundaries. The original renderer's screen-space
body cannot be reproduced exactly in 3D; vertical proportions are explicitly
new presentation choices. No opponent AI, suspension or collision changes.
The model is no longer culled by the old narrow-view distance/facing rules.
Opponent motion currently retains the original tick cadence even when player
camera smoothing is enabled.

The portable scene now includes a small dynamic mesh sharing static material
indices. The host updates a reusable coherent vertex buffer after prior-frame
fences, then draws it alongside the cached track in both eyes. No static track
or material re-upload per frame. The stereo option now supports league races.

Checks: 500 ticks each of practice and league, 1080/1140 scene samples,
unchanged original pictures and timing. All eight tracks' topology/material
checks, finite scenery geometry and opponent presence across every segment join
passed. The original physics oracle remains 467/467 exact. Three relevant
CTests passed; Mac and Android packaging passed. Native/wearer results follow.

### Wearer corrections: horizon, opponent and drop/start smoothing

The wearer reported a floating horizon, broad unsmoothed opponent, and jerky
movement while drop/start messages were visible. The finite grass arena ended
before the distant scenery; the ground now extends beyond the far plane and
scenery bases use ground height rather than camera height. A CPU diagnostic
visually confirms the landscape joins the ground.

Opponent body proportions now follow the original projected cross-sections:
base +/-U/2, rear roof +/-3U/8, front roof +/-U/2. Wheels sit inside the axle
span instead of protruding past it. World-space depth/vertical placement remains
a 3D presentation interpretation. With VR smooth motion on, the opponent's
along-track position, lane and lifts interpolate between the same ticks as the
player camera; interpolation wraps segment and lap seams.

A definite smoothing bug was found: changing the recovery countdown byte reset
camera history every tick. History now resets on entry to recovery or phase
changes, preserving interpolation throughout the ordinary countdown. The new
module check requires continuous history throughout its drop/start sequence.
Practice/league runs pass, with 352/372 interpolated moving ticks overall and
three later discontinuities each during crash/phase transitions. Original/future
flat pictures and clock stay unchanged. Physics oracle: 467/467 exact.

Mac, Android and native builds pass; three relevant CTests pass. The available
headset timing log contained only idle startup, so a separate frame-time issue
during messages is not yet ruled out. Corrected headset visuals await feedback.

### Opponent tyre contact

The wearer confirmed the horizon/proportion/interpolation/start fixes, then
reported floating opponent tyres. The 3D mesh used original drawing-corner
heights as its tyre-bottom anchor. `OpponentDriver::suspension` adds a $50
drawing lift to a suspension body reference whose rest height is $28 ($7C on
curves). Those offsets are now removed only in the new 3D presentation.
Remaining suspension displacement and airborne height are retained; the flat
renderer and race rules are untouched.

All eight tracks now exercise a flat road interval: tyre bottoms equal road
height at rest, and an added 64 height units raises them by exactly 64/1024
scene units. Practice/league invariance and start continuity checks pass.
Mac/Android/native builds pass, 3/3 relevant CTests pass, physics 467/467 exact.
Corrected tyre contact still needs headset confirmation.

### Correction to the tyre-contact interpretation

The previous paragraph's interpretation of $28/$7C as ride height was wrong.
The wearer saw tyres sink into the surface, especially in corners. These are
spring preload terms: after the spring response subtracts preload, static
compression is road minus body. Equilibrium is body == road. Only the $50
artwork lift must be removed by the 3D tyre-bottom model. The previous tests
used constructed poses with the same incorrect rest-height assumption.

A new check runs the real league simulation for 200 ticks: rear body-road is
0/0 and drawing-reference minus road is 80/80. It asserts this independently
of mesh construction. The eight-track contact/jump checks now use the correct
80-unit drawing offset; practice/league invariance and three CTests pass.
Mac, Android and native builds pass. Revised visual contact awaits the wearer.

## Frame pacing work

Tyre contact was confirmed by the wearer. The next priority is pacing before
immersive mode. Representative focused driving samples before optimization:
whole frame mean13.954ms/p95 22.304ms; room submission/fence mean8.273ms;
content mean5.415ms/p95 13.856ms. These are wall times, not GPU cost.

The first optimization submits the 320x200 cockpit as a separate native-sized
quad instead of CPU scaling it to 960x640. Its physical size remains
1.5x0.9375m, and input retains the original letterboxed logical coordinates.
Unchanged overlay pixels skip uploads. Static/menu panels retain their old
path; stereo-rendering failures still fall back to the full flat panel.

The wearer confirmed alignment and pause menus still work. In a subsequent
driving sample: whole-frame mean13.599ms/p95 15.073ms/max16.939ms; content
mean2.303ms/p95 3.619ms. 4,461 uploads over 15,000 overlay frames (~30%).
New GPU timestamps show room execution mean1.017ms while its queue/fence wall
time averages11.037ms; image acquisition/wait averages0.007ms. This implicates
queue waiting rather than expensive floor shading. Runs are wearer-driven, not
an identical-input benchmark, so comparisons are representative rather than
controlled speedup claims.

The second build defers the room fence wait until after CPU content preparation.
All Vulkan submissions remain on the same ordered queue; each runtime-owned
image is still fenced before release, and all releases precede xrEndFrame.
Room command buffers and query pools are reset only after their prior frame
completes. This overlaps useful CPU work with pending room GPU work without
adding simulation latency or weakening ownership.

The overlap build ran over 10,000 submitted frames, including pause/resume and
focus loss. Representative focused sample: render-and-fences mean12.504ms,
p95 13.424ms, max14.882ms; xrEndFrame mean0.221ms/p95 0.424ms. Final room
fence wait averages0.005ms, room GPU1.015ms. The wearer reported everything
looks good. CPU content/scene wall times now include waiting behind the pending
room submission, so they cannot be compared directly with the old serial
substage timings. Compare total frame timings instead.

This is a validated pacing improvement, not a guarantee that every frame meets
72Hz: occasional render samples still exceed13.89ms, and xrEndFrame adds time.
The before/after sample sets also differ in user inputs. No extra frame queue,
physics step changes or reduced scene resolution were introduced. The next
milestone is the immersive cockpit presentation.
