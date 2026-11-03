# Trace harness: AstroQuest's shadPS4 on macOS (2026-10-12/13)

Source: github.com/bigmak94/AstroQuest @8213f28 (GPL-2.0+, consulted under the project's
exception; nothing copied into src/). Clone and build in `.tools/astroquest` (build dir
`build-mac`), runtime in `.tools/harness`.

## Build (x86_64, runs under Rosetta 2)
- `shadps4-arm64-main` tree, CMAKE_OSX_ARCHITECTURES=x86_64, Ninja, Release. Vulkan via Mesa
  KosmicKrisp (submodule, built x86_64 through its cross file).
- Needs: Homebrew llvm, spirv-llvm-translator, libclc, python@3.12; venv `.tools/aq-venv`
  (meson, mako, pyyaml, packaging, ply, pillow); `PKG_CONFIG_PATH=.tools/libclc-compat/pkgconfig`
  (shim .pc + symlinks `spirv{,64}-mesa3d-.spv` -> Homebrew libclc 23 `libclc.spv`). Keep
  Homebrew llvm off PATH (Mesa would pick its clang). Rebuild:
  `PATH=.tools/aq-venv/bin:/opt/homebrew/bin:$PATH cmake --build build-mac -j 12`, then copy
  `build-mac/shadps4` to `.tools/harness/`.
- Nested submodules: mesa (in mesa-kosmickrisp), sirit, zydis, discord-rpc, freetype, toml11,
  rapidjson.
- Local patches (all marked "Beacon harness"): src/dist/MacOSBundleInfo.plist.in (missing),
  crash_reporter.cpp (macOS branch), vr_host_link.cpp (SOCK_CLOEXEC/MSG_CMSG_CLOEXEC),
  page_manager.cpp `SHADPS4_NO_PAGE_PROTECT` (Rosetta aborts on a faulting AVX store into a
  write-protected page: "EmulateForward on a synchronous exception"), memory.h `ReadableAreas()`,
  pad.cpp (`NotePad` after scePadRead/scePadReadState), known_title.cpp `BeaconTrace`:
  - `SHADPS4_BEACON_TRACE=file`: one CSV row per controller read (= per game update):
    `frame,hero,px,py,pz,vx,vy,vz,buttons,lx,ly` (buttons/sticks = the input handed to the
    title by the previous read, i.e. the input that produced this row's state) and the first
    0x800 bytes of the hero object to `file.bin` (u64 frame + 0x800 bytes per row).
  - `SHADPS4_BEACON_POKE="GLOBAL:OFF:TYPE:VAL;..."` write through a global pointer.
  - `SHADPS4_BEACON_FIND="VTABLE[@field,a>b>c,...];..."` scan readable memory every 120 frames
    for objects whose first word is image+VTABLE and log fields (pointer chains a>b>c).
  - `SHADPS4_BEACON_POKEFIND="VTABLE:CONDOFF=CONDVAL:CHAIN:TYPE:VALUE;..."` every frame on the
    found objects whose u32 at CONDOFF is CONDVAL; TYPE u8/u32/u64, `TYPE@OLD` writes only while
    the target still holds OLD (one-shot state change).
  - `SHADPS4_BEACON_PATCH="OFF:OLDHEX:NEWHEX;..."` code bytes at load, checked before writing.
  - `SHADPS4_BEACON_WATCH="VTABLE:IDOFF=ID@CHAIN:TYPE,...;..."`: every frame, for every found
    object of VTABLE whose u32 at IDOFF is ID ("*" = all), a row
    `frame,vtable,id,address,values...` in `trace.csv.watch` (TYPE f32 / u32 / u64; chains as
    FIND). Objects are found by the FIND scan (every 120 frames), so short-lived objects are best
    reached by a chain from a long-lived one.

## Running
`.tools/harness/run.sh NAME SECONDS [DUMP_AT...]` (headless, VR demo motion off, game's own
1/60 s step via SHADPS4_TITLE_TIMESTEP=0, no page protection; game folder `$GAME`, default
`.tools/harness/game/CUSA12392` = symlinks + own args.txt `-package master -sequence product`).
Inputs: SHADPS4_INPUT_SCRIPT (lines `start hold buttons|lx=..|ly=..|pad=x,y,z,...`, wall-clock
seconds). Pictures: render-target dumps (`dump_<t>/`); `sheet.py RUN OUT.png` contact sheet.
The game runs at ~67% speed (25 ms per frame); the trace counts game updates.
For repeatable, fast captures (frame-indexed input, deterministic scheduling, fast mode,
pictures and stops by update, savestates) see "Deterministic, fast and checkpointed runs" below.

### Into a level (forest_castle, verified 2026-10-13, run world21)
```
H=.tools/harness
P="$($H/select_level.sh forest_castle);16eb488:2460=5:25d8>cb0:u8:1;16eb488:2460=5:25d8>e60:u32@0:7"
SHADPS4_BEACON_FIND=16eb488 SHADPS4_BEACON_POKEFIND="$P" SHADPS4_BEACON_PATCH="3a795a:765a:9090;b9be14:7528:eb28" \
SHADPS4_INPUT_SCRIPT=$H/scripts/move.txt $H/run.sh NAME 420
```
The product sequence (vtable 0x16eb488, case variable +0x2460, coroutine 0xb99720) goes
1 title -> 2 calibration -> 4 world map -> 5 enter room -> 7 in level (~frame 2900):
- SHADPS4_BEACON_FIND must list 16eb488 (plus any vtables you watch): POKEFIND only acts on
  objects the FIND scan located; without it nothing is poked and the run stays in calibration.
- 0x3a795a patch: lets the calibration finish with the scripted controller pose.
- 0xb9be14 patch: skips the wait for the world map's own selection flow.
- POKEFIND acts on the objects the FIND scan found: a run that sets its own
  SHADPS4_BEACON_FIND must keep the sequence in it (`16eb488@2460;...`), or the level is never
  entered (runs ceil1/ceil2, 2026-10-14). SHADPS4_INPUT_SCRIPT needs an absolute path (run.sh
  changes into the harness folder; a relative one logs "No usable steps").
- Runs sens3 / sens4 / sens6 (2026-10-14) froze the hero at its spawn height; sens7, with the
  same watch as sens6 (goal positions read through the brain's handle `$B>e8>8>70`,
  B = `3a70>30>0>10>30>0>10`), played normally once the shared harness had been rebuilt with the
  picture patch, so the freezes were most likely the emulator binary changing mid-session, not
  the watch. With that build, `SHADPS4_BEACON_FLAT=1` shows the scene in `shots/` (both eyes).
- `select_level.sh LEVEL`: writes the level folder name into the world map's selection string
  ([[seq+0x2570]+0x2230]) while the sequence is in case 4.
- Case 5 waits for the calibration object (seq+0x25d8) to fade (+0xaec > 1.5) and for its room
  transition machine (+0xe60, requested through +0xd90/+0xd98) to reach state 13. Both run in
  its update 0x3a3870 only while +0xcb0 is set (set by the calibration start 0x3a4da0, cleared
  by 0x3a8b20); the bypassed calibration leaves it clear and the machine in state 0. Poking
  +0xcb0 = 1 and moving the machine once to state 7 (the calibrated idle state whose handler
  takes the transition request, 0x3a6a90) lets it run 8..13 by itself.
- The `.bin` snapshot and trace then follow the level's hero from its spawn (frame ~2630).

### Objects
`SHADPS4_BEACON_FIND="16a4fa8@1a90,2c50,2d40"` logs every moving joint every 120 updates (id
+0x1a90 = FNV-1a of the UUID, delay state / progress +0x2c50); `tools/jointcmp.py` compares with
`BEACON_JOINT_DUMP=out.csv beacon-level objects GAME LEVEL SECONDS`. Extra trace columns may start
at an image global: `SHADPS4_BEACON_TRACE_EXTRA="g2f014a8>48>8>100:f32"` (the head orientation).

### Short-lived objects (SHADPS4_BEACON_FIND_EVERY, 2026-10-15)
`SHADPS4_BEACON_FIND_EVERY="N@FROM-TO"` scans every N updates while the last trace row is in
FROM..TO (else every 120), so objects made by an event (coins of a box, pieces) are found at once
(harness build det8, `bin/20261015-120851-det8`; run.sh needs an absolute SHADPS4_BIN). Runs
roof_box1 / roof_box2: `scripts/roof_box1.txt` from roof_landed (bot, bridge, hover over the
box25/26 stack), record `16d0de0:1a90=ID:3a00,...,35d8>{8i}>158>0>48>0:80` (each piece's body
state: position +0x40, orientation +0x50, velocity +0x60, spin +0x70, library units = game x 3)
and `1690278:1a90=*:2980,26f0>0:60,near=3@2950` (coins and their movers). The two runs differ from
~4100 on (enemy movement), the breaks do not.
Port side: `beacon-level scriptwalk GAME LEVEL SCRIPT` plays the same frame script through the
scene (BEACON_SCRIPT_PLACE=F,x,y,z,yaw puts the hero on the original's track before update F;
BEACON_SCRIPT_DEBRIS prints pieces and coins; BEACON_SCRIPT_PROBE a laser-ray grid;
BEACON_SCRIPT_ENEMIES nearby enemies; BEACON_SCRIPT_ENEMYLOG enemy state changes within 6 m;
BEACON_SCRIPT_KILL=F:i,j removes enemies; BEACON_SCRIPT_SPITHIT=F,F,.. hover hits on spitter 0;
BEACON_SCRIPT_BLOCKS glass-block cubes; BEACON_SCRIPT_STONES stones in flight;
BEACON_SCRIPT_HIT=F:TYPE:dx,dy,dz a hit on the hero; BEACON_SCRIPT_RESPAWN markers, VR warp, rig; BEACON_SCRIPT_SHOT a picture at the end).

### Teleport and rig pin (2026-10-15, harness det9 / det10)
`SHADPS4_BEACON_TELEPORT="F:x,y,z[;...]"` writes the hero's position (+0xa0) and clears its
velocity at trace row F; `SHADPS4_BEACON_RIGZ="F:z[;...]"` writes the VR player's rig z
([[main]+0x48]+0x38) at row F (list every row to pin it). A teleported hero far from the rig makes
the rig jump after it (run roof_spit3); with the rig pinned the hero is warped back (second
machine 0x51 -> 0x5c, runs roof_spit4 / roof_spit5 - no stone hit, an earlier note said so). Extra trace
columns: `SHADPS4_BEACON_TRACE_EXTRA="CHAIN:TYPE,..."` (comma-separated; types f32, u32, u8; u32 hex in the CSV for f32).

### Coins
`SHADPS4_BEACON_FIND="1690278@1a90,25b0"` logs every coin (state +0x25b0, time in state +0x25b4).
Coins can be moved for a test by writing float bits to their stored position:
`1690278:1a90=FNV:2950:u32:BITS` (and +0x2954, +0x2958) in SHADPS4_BEACON_POKEFIND; the hero's
coin counter is +0x44c in the `.bin` snapshot. Port side: `BEACON_COIN_MOVE="UUID:x,y,z;..."
beacon-level runtrace ...` prints the counter changes.

### Enemies
Per-frame enemy state (run enemy5): WATCH on the basic enemy (vtable 1685908, id +0x1a90) with
position +0x2670, rotation +0x2680, body mode +0x25a8, velocity +0x26a0, the behaviour path
(`3a70>30>0>10>0` top, `...>30>0>10>0` its first child, brain state `...>80`, goal handle
`...>e8>8`) plus the goal points (vtable 167d0e0, position +0x70). `tools/enemycmp.py script`
turns it into cruising goals, waits and segment-start poses for `BEACON_ENEMY_SCRIPT=...
beacon-level enemytrace`; `enemycmp.py compare` lists port against original per update.

## Cast commands (pfxrun and beacon-physics, same line formats)
`cast` / `meshcast` run the sphere table; prefix `c` (`ccast QX QY QZ QW HALF ...`,
`cmeshcast QX QY QZ QW HALF ...`) runs the capsule table with that orientation and half length
(the radius field is the capsule's); prefix `r` (`rcast`, `rmeshcast`) runs the ray table (radius
ignored; result point = start + t direction). `castprobe.py` compares them: CAPSULE=1 or RAY=1.
`beacon-level floor GAME LEVEL X,Y,Z` also prints the PhysicsWorld ground probe; with
BEACON_CAPSULE=x,y,z,dx,dy,dz,r,half it casts the hero's upright wall capsule.
`BEACON_ENEMY_TRACE=PLACEMENT beacon-level enemytrace ...` prints that enemy's root dynamics per
update (root, body centre and velocity, ground distance, contact partners; frame and target).
`beacon-level punchtest GAME LEVEL` swings a punch at every enemy (physics bodies).
`beacon-anim model SKEL ANIM JOINT` prints a joint's model-space frame over a clip.
`beacon-level physcompare ...` with BEACON_DUMP_MESHCAST=FILE writes the differing mesh cases as
meshcast lines (hull shapes as `cast 4 ...` lines); radius 0 probes the module's ray query;
BEACON_SHOW_ONLY_OLD=1 lists the first places only the old world hits.
Run-time meshes: `beacon-physics soup FILE.pfb OUT.tri [N [SX SY SZ]]` writes a kind-6 mesh's
triangles (scaled as the module scales them); `meshdump FILE OFF SIZE` (SIZE 0: a .tri file
built - by the game's builder 0xde8550 in pfxrun, by buildLargeMesh in beacon-physics; a
`.dump` file: pfxrun's dump read back by beacon-physics) prints islands, facets, edges and
trees in one format for both; every mesh command takes the same SIZE-0 files.
`castprobe.py mesh` / `meshprobe.py` with BUILT=DIR use all run-time meshes (PORT_GAME_MESH=1:
the port casts against the game's build).

## Verification against the port
`beacon-level heroreplay GAME LEVEL TRACE.csv OUT.csv [FROM]` replays the recorded input
frame by frame from the original's position (BEACON_REPLAY_HIT=F:TYPE:dx,dy,dz hits the hero
before update F and reports the death routine); `.analysis/tools/tracecmp.py ORIG.csv PORT.csv`
aligns two traces on a takeoff and lists the differences; `.analysis/tools/herocmp.py ORIG.csv
PORT.csv [TOL]` compares positions frame by frame (first frame beyond TOL). Results:
`docs/hero.md`.

## Facts (AstroQuest findings, recorded as such)
- The game advances 1/60 s per drawn frame (confirmed by the traces: per-row velocity changes
  are exactly g/60). Physics = Sony PhysicsEffects. Main object pointer 1.04 at image
  0x2ecfe38; bodies: world +0x10, +0x8, library +0x20, count +0x1a0, array +0x1a8, stride 0xa0,
  position +0x40, velocity +0x60, motion +0x1 (2 = keyframe).

## Own findings
- Hero path (1.04): [0x2f014a8] main -> +0x88 -> +0x28 table -> [3] hero manager -> list
  +0x50..+0x58, first with byte +0x78 -> hero; position +0xa0, velocity +0xd0, model scale
  vector +0xc0 (0.22), ground contact +0x14c (1 grounded, 0 airborne).
- Boot options: `-level` (0xb82a50, also `-camera`), `-sequence` (0xc65e50; packages
  default/master/future, `-usertest`, `-debugInput`). The demo sequence asserts
  (GfxDataManager), the drs sequence needs the network: both abandoned for the product path.

### Combat captures (2026-10-13)
`scripts/combat.txt` (written by the run command, see the session notes): the boot part of
move.txt, `160 9.0 ly=0` (walk ~3 m to the first enemies), `170 1.5 lx=255 ly=128`, then square
every 0.6 s. Under this harness the game runs at about 31% speed in the level, so stick holds
give roughly a third of their wall time in game updates (5 s -> 93 updates). Watch fields used:
hit pending 3aac, hit type 3ad4, reaction 3d10, +0x30, HP 3aa8. Run combat5: both enemies
punched and defeated, and one enemy attack on the hero.

## Native routine calls (pfxrun, 2026-10-13)
For pure library routines no emulator is needed: `.analysis/tools/pfxrun.c` (built with
`clang -arch x86_64 -O1 -o .tools/bin/pfxrun .analysis/tools/pfxrun.c`) maps
`.tools/ghidra-input/image.bin` at an arbitrary base (the physics code addresses its constants
and callees relative to itself), patches `__isfinitef` and the stack-guard failure, points the
guard slot at itself and the allocation hook (0x171fb38) at posix_memalign, then calls routines
on inputs from stdin in the same line format as `build/beacon-physics`. Rosetta executes the AVX
code (unicorn 2.1 cannot). Commands: `convex`, `sort4`, per-facet `cylfacet` / `sphfacet` /
`boxfacet` / `capfacet` (box and capsule take the tilts in radians, passed as one vector as the
islands do), mesh traversals `cylmesh` / `sphmesh` / `hullmesh` / `boxmesh` / `capmesh`
(deserialize a PELM blob with the game's 0xdf3cc0), `pair` (dedicated primitive pairs). Drivers:
pfxprobe.py, facetprobe.py (cyl|sph|box|cap; TIES=0 leaves out exactly axis-aligned facets),
meshprobe.py (last argument sphere|hull|box|capsule), pairprobe.py. Solver: `solve` (set-up
0xe68f10, warm start in C as the inline loop of 0xdd2bb0, iterations of 0xe69730), `posfix`
(0xe69e00), `integrate` (0xd99cb0), `manifold` (sequences of add 0xe0cee0 / refresh 0xe0d6e0),
`rolling` (0xe6add0, warm start, 0xe6af50) with 0x70-byte solver bodies and 0xa0-byte rigid
states built from the line (layouts in pfxrun.c); driver solverprobe.py (also `swept`: set-up
0xe6a570 and solve 0xe6aa90 of the swept list). Swept contacts: `ccd FN ...` (one of the ten
primitive pair functions 0xe13300 ... 0xe16c60; hull shapes append N X Y Z ...), `ccdmesh`
(0xe17320 on a game mesh), `sfsep` (0xe7c260); driver sweptprobe.py (`mesh N seed [scaled]` for
meshes). Distance search: `gjk` (0xe1d110 on spheres / capsules / cylinders / hulls; with
PFXRUN_GJKLOG set every support query is logged to stderr), `simplex` (one step of 0xe1b870);
drivers gjkprobe.py, simplexprobe.py. Its results are the
original's logic under Rosetta's arithmetic: reciprocal / reciprocal-square-root estimates differ
from the console's, so exact-tie decisions can differ (see physics.md, "Convex solver").

## Viewer test camera (port aid, 2026-10-14; moved 2026-10-15)
Play mode starts with the test camera on (checkbox "Test camera (T)", key T): at the game's resting
head position - the rig plus 0.09 m back (run roof_g6: the VR player's head +0x4b0 (0, 1.15,
3.035), the rig (0, 1.15, 2.945)) - pitched so the hero sits a quarter of the field of view below
the centre (until 2026-10-15: 1.6 m behind and 1.2 m above, which made everything look small and
far below); it follows the hero's yaw and height, and the rig also follows the hero backwards at
its forward speed and acceleration (the game's MoveModeZ only advances, CanGoBack false). Off
restores the game's head position and rule. The game draws with the headset's field of view
(the harness reports tan 1.263 up and down: about 103 degrees vertically); the viewer keeps 52
degrees for a flat screen. `beacon-level render ... --play S --back S` walks forward then back
for checks.

## Why the harness shows no pictures (diagnosed 2026-10-14)
Symptom: `shots/*_hud_*.png` are black apart from the presenter's frame-id marker (white blocks
top left) and ImGui notices; the render-target dumps (`dump_<t>/`) do show the scene.
Cause: the title renders each eye into its own targets (e.g. 1440x1536 R16G16B16A16 at
0x51b620000 -> tonemapped R8G8B8A8Srgb 0x51a420000; 960x1080 / 1200x1280 sets at other moments)
and then copies the result into the six eye buffers it hands to `sceHmdReprojectionStart`
(0x522ae0000 ... 0x5257e0000, 1440x1536 Srgb, pitch 1536) with a 4-vertex triangle-strip draw
that uses the **ES+GS** stages. The Vulkan driver (Mesa KosmicKrisp 26.1.99, commit a7575d7e)
does not offer `geometryShader` (it does offer tessellation), so shadPS4's
`RefreshGraphicsStages` logs "Geometry shader stage unsupported, skipping" and the draw is
dropped (`TRACE draw ... pipeline=false vs=0x0 ps=0x0 cb0=<eye buffer>`). The eye buffers keep
their initial black contents (texture-cache flags 0x40: registered, never written), and the
presenter samples exactly those (`PrepareHmdFrame`). MoltenVK has no geometry shaders either.
Evidence: runs/world13 (7,630 warnings; every `pipeline=false` draw follows one), the HMD frame log
lines, the 3000-draw trace after a dump. Not related to SHADPS4_NO_PAGE_PROTECT.
Second loss from the same cause: instanced point-list draws (`prim=1`, e.g. n=1 inst=72) into
the scene target, i.e. GS-expanded sprites/particles, are missing even inside the dumps.
Open item - ways to get pictures:
1. Emulator fallback for the eye copy: when the GS is unavailable, compile the ES as the vertex
   stage if the GS only passes the triangle through (needs a look at the game's GS; GS/ES code
   is not compiled or dumped today because the stage bail-out comes first).
2. Present the pre-copy target instead (Srgb 0x51a420000 etc.); simple, but which eye and which
   moment it holds needs checking.
3. A driver with geometry shaders (KosmicKrisp lists GS/tessellation work on its roadmap; or a
   Linux/AMD machine). Needed anyway for the particle draws.
Done 2026-10-14 - flat pictures (`SHADPS4_BEACON_FLAT=1`, binary installed in .tools/harness;
the previous one is `shadps4.prev`). Local patch "Beacon harness": src/video_core/beacon_flat.h,
vk_rasterizer.cpp (`BlitDroppedCopy`), vk_presenter.cpp (notes the eye-buffer addresses),
vk_pipeline_cache.h (`LastFragmentInfo`), texture_cache.h (`GetBlitHelper`), blit_helper.*
(`SmoothInto(..., plain, src_msaa, swap_rb)`), host_shaders/beacon_copy.frag. Every dropped draw
logs its pixel shader's textures in the draw trace (`TRACE   dropped draw cb0=... reads image N
at ...`). A dropped draw into an HMD eye buffer is replaced, at that point of the command stream,
by a copy of the texture it reads: the engine's own picture of that eye (both eyes, true stereo),
without the title's copy shader. Lessons on the way:
- the eye copy reads 0x51b620000 (1440x1536 R16G16B16A16Sfloat; 0x5145f0000 816x870 during
  calibration) for both eyes: the eyes are rendered one after the other into the same target,
  so reading it at present time catches the next frame half drawn (sky only) - hence the copy
  in-stream;
- vkCmdBlitImage on KosmicKrisp compiles a Metal pipeline from source on every call (7 fps); the
  copy uses BlitHelper's cached fullscreen pass instead (full speed, ~43 fps in calibration);
- the host holds that target with 8 samples (`backing->num_samples`): sampled as a plain 2D
  texture it gives one constant colour; beacon_copy.frag averages the samples (SRC_MSAA);
- the eye buffers are written through a colour target with red/blue swapped (CB swizzle), so
  the copy swaps them too;
- look up existing images by address (FindImageFromRange): a texture-style FindImage made a
  second image of the target that the cache swapped and re-uploaded every frame.
`SHADPS4_VR_DESKTOP_VIEW=spectator` shows one eye across the whole picture.
Run flatlvl7 (forest_castle, scripts/combat_long.txt): calibration and level pictures correct
in colour; in the level only the shot taken while the hero walks (160-169 s) is fully lit, the
others show the starting view with near geometry black and sky/emissives lit. Open item:
cause not known; hypothesis: the dropped GS point-list draws into the scene (n=1, inst=3/72/
9660, prim=1, ~150 per frame) are lights or similar rather than only particles; or a game-side
fade/state. Still missing in any case: the copy shader's own work and all GS point draws.
Superseded first step (option 2): local patch "Beacon harness" (src/video_core/beacon_flat.h,
vk_rasterizer.cpp, vk_presenter.cpp, vk_pipeline_cache.h). For every dropped draw the pixel
shader's textures are logged in the draw trace (`TRACE   dropped draw cb0=... reads image N at
...`), and the first one is remembered per colour target; `SHADPS4_BEACON_FLAT=1` makes the
presenter show that texture instead of the eye buffer. Result (run scratch flat1, title screen):
the eye copy reads 0x51b620000, 1440x1536 R16G16B16A16Sfloat, for **both** eyes - the engine
renders the eyes one after the other into the same target, so the shown picture is whichever
eye was left there (both halves equal). `SHADPS4_VR_DESKTOP_VIEW=spectator` shows one eye full
frame. The copy shader itself (possibly a final colour step) is not run, and GS point draws
(particles) are still missing.

## Ceiling capture (2026-10-14, run ceil4)
`scripts/ceiling.txt`: the boot part of move.txt, then walk left (`lx=0`, 10 s wall time) under the
overhang beside the forest_castle start and jump (held 1.5 s) twice. Found with
`BEACON_CEILSCAN="x0,x1,z0,z1,y,step,maxgap" beacon-level floor GAME forest_castle 0,0,0` (ground
below y on a grid, then a ceiling with normal y < -0.7 within maxgap above it) and
`BEACON_CEILING=1` (the ceiling straight above each floor point). Compared with
`beacon-level heroreplay ... ceil4/trace.csv OUT 4300`.


## Deterministic, fast and checkpointed runs (2026-10-14)
Everything here is an opt-in local patch ("Beacon harness": src/core/beacon_det.{h,cpp} plus small
hooks); without the variables below the emulator behaves as before.

### Binaries and run.sh
- `.tools/harness/install.sh [--no-switch] [--force] [BINARY] [LABEL]` copies a build (default
  `../astroquest/build-mac/shadps4`) to `bin/<stamp>[-label]/` (with symlinks to the Vulkan loader,
  driver and ICD, which the loader finds next to the executable) and points `current` at it.
  Without `--force` it refuses while an emulator runs; `--no-switch` only installs (use it with
  `SHADPS4_BIN=bin/<ver>/shadps4`). `run.sh` resolves `current` once when a run starts, so a new
  build never touches a running capture (the old `shadps4` file is the fallback while `current`
  does not exist). `runs/NAME/binary.txt` records which build ran.
- `run.sh NAME SECONDS [DUMP_AT_SECONDS...]`: SECONDS `0` runs until the emulator stops itself
  (`SHADPS4_BEACON_STOP_AT`). Relative `SHADPS4_INPUT_SCRIPT`, `SHADPS4_LOADSTATE` and the path in
  `SHADPS4_SAVESTATE_AT` are taken from the caller's directory. Deterministic runs (and `ISOLATE=1`)
  get their own copy of `user_seed/` (fixed save data and settings) in `runs/NAME/work/`, so they
  start from the same state and can run side by side; dumps by update land in `dump_f<N>/`.
  `SHOTS` defaults to 0 in fast mode. The summary line gives frames and wall time.
- `det_env.sh` (source it): `SHADPS4_DETERMINISTIC=1` and the forest_castle recipe
  (FIND/POKEFIND/PATCH) plus an enemy watch (every basic enemy: position, rotation, body mode,
  velocity, behaviour root).

### Frame-indexed input
A script whose first non-empty line starts with `# frames` (or any script with
`SHADPS4_INPUT_FRAMES=1`) counts in updates, the trace's frame column: a line `S H ...` is the input
that trace rows S .. S+H-1 show (the controller read before each row hands it over). It is applied
inside the controller read, so it lands on exactly that update whatever the wall clock does; the
other tokens are as before (buttons, lx/ly/rx/ry, pad=, head=, mic=, ...). Seconds scripts work as
before. Scripts: `scripts/boot_frames.txt` (into forest_castle: pad pose, cross every 120 updates
from 600 to 3000 - the level needs a press after it is set up, about 400 updates after the hero
appears, or the hero waits at its spawn point for ever), `scripts/combat_frames.txt` (the same,
then walk 540 updates from 3300, turn, square every 36 updates).

### Determinism (SHADPS4_DETERMINISTIC=1)
What is pinned:
- Time: every guest time source (process time and its counter, TSC and its frequency - fixed at
  1.6 GHz instead of measured -, clock_gettime for all clocks, gettimeofday, RTC through them, pad
  timestamps) reads a virtual clock: update N starts at 1 s + N/60 s; within an update a thread's
  own readings advance 1 us per reading (monotonic per thread). Wall clock epoch 2026-01-01.
  The suspected real-time step (0xecc560 / [0x132e608]) is not one: 0xecc560 returns [0x17208c8],
  which the frame-rate setter 0xecc4f0 sets to 1e6/fps, and [0x132e608] is the constant 1e6, so
  it is the fixed frame duration.
- Events: timers and HR timers of event queues, sleeps (nanosleep/usleep), waits with a time
  limit (condition variables, mutex timed locks, semaphores, event flags, event queues), the
  display (vblanks at 120 Hz, flips, HMD reprojection ticks), audio output pacing (a port's buffers
  due at their sample times, rounded up to update starts), the virtual camera (a frame every two
  updates) and the microphone (no host microphone: silence or the script's `mic=` level, a buffer
  per buffer length) are events on virtual time. They fire in order only when the main thread is
  blocked and everything else has settled, and at each update for what falls before its start.
- Scheduling (SHADPS4_DET_SCHED, on): the title's own threads (start routine in guest memory; the
  emulator's helpers started as kernel threads are not counted) are tracked at every kernel wait
  (condition variables, mutexes, semaphores, event flags, event queues, sleep, join, once, yield,
  GPU-idle wait, zlib, AJM, audio output, libc guard, vblank wait). At each update the main thread
  waits until every other title thread is blocked, parked, or only polling; while the main thread
  runs, the others wait at their next call into the emulator (the gate at every HLE call); when
  the main thread wakes from a wait it lets the others settle first. "Polling" = only idle calls
  (yield, polls, trylock, time reads, sleeps) for SHADPS4_DET_QUIET_US (1000); "spinning" = the
  title's 45 `pause` instructions (job workers' and locks' spin waits) are patched to `ud2` and the
  illegal-instruction handler marks the thread and steps over them (only in deterministic runs);
  no call at all for SHADPS4_DET_SPIN_MS (50) also counts as spinning. Waits give up after
  SHADPS4_DET_TIMEOUT_MS (30000) with a log line naming the thread (none seen in forest_castle).
  The GPU must be idle too, and every thread the books call blocked must really be waiting in
  the host kernel (Mach thread_info; a thread woken but not yet run is runnable): this closed
  the races that host load caused. While the main thread runs, the other title threads that are
  in their own code are suspended (Mach thread_suspend; SHADPS4_DET_SUSPEND=0 turns it off);
  those inside a call into the emulator are never suspended (they may hold its locks) and wait
  when they come out of it; a main thread that spins (patched pause) counts as blocking, so the
  others run. SHADPS4_DET_BATON=1 also runs the other threads one at a time (no gain seen).
  SHADPS4_DET_HOLD=name[:keep] keeps threads of a name from starting (the job workers: the game
  then hangs in boot - not usable). Random sources (the /dev/*random and rng devices, which
  seeded rand() from the wall clock, and libSceRandom) use a fixed-seed generator. SHADPS4_DET_LOG_CALLS=file[:updates] logs every HLE call of every title thread for
  the first updates (`.analysis/tools/calldiff.py A B` compares per-thread sequences to find where two runs part).
- Save data: a fixed copy per run (run.sh).
What stays nondeterministic: where the heap puts objects. The TBB job workers' work stealing and
the polling loops of several threads (their poll counts follow host timing inside pure game code)
decide which thread allocates first. Positions, velocities, states and every other value were
identical in all comparisons (6000 updates); addresses were not. Deterministic runs therefore
write addresses as handles: the hero column, the watch address column and watch u64 fields that
point into mapped memory become `c0000001`, `c0000002`, ... numbered in the order first written,
and the watch rows of an update are sorted by (vtable, id) (they followed the address order of the
memory scan). SHADPS4_BEACON_RAW_ADDR=1 keeps raw addresses. The hero snapshot (`trace.csv.bin`)
holds raw memory, pointers included. The log (stdout.log) is not deterministic.

### Fast mode and pictures by update
- `SHADPS4_BEACON_FAST=1`: no draws, dispatches, eye-frame composition or presenting
  (`SHADPS4_BEACON_FAST_SKIP=draw,dispatch,present` chooses; game logic was the same without
  draws/dispatches); labels, end-of-pipe events and flips still complete. Without determinism the
  display refreshes at SHADPS4_BEACON_VBLANK_HZ (1000).
- Pictures by update: `SHADPS4_DUMP_AT=N[,N..]` (render targets -> `dump_f<N>/`),
  `SHADPS4_SHOT_AT=N[,N..]` (screenshots named `..._f<N>_...`), `SHADPS4_SHOT_EVERY=N`; drawing
  comes back for SHADPS4_PICTURE_WARMUP (60) updates before each. Works with FLAT.
- `SHADPS4_BEACON_STOP_AT=N`: the emulator writes trace row N, flushes trace, watch and log, and
  exits.
- Speed (2026-10-14, one run alone, M3 Pro, boot to update 2900 = forest_castle a little after
  the hero's landing): fast mode without determinism 83 s (35 updates/s; the old harness took
  ~150 s to reach the level and ran it at ~20 updates/s); fast + deterministic 433 s
  (6.7 updates/s). The cost is the scheduler's settling (the main thread waits until the others
  are quiet, 1 ms; the pump 2 ms) - shorter quiet times (300/500 us) were only 13% faster and
  broke determinism. Without fast mode, deterministic runs are slower again (GPU work).

### Raw object recording (SHADPS4_BEACON_RECORD)
So that one more field never needs one more capture: every update, the raw memory of chosen
objects goes to `trace.csv.rec.gz` (gzip, flushed every 300 updates, readable after a kill), each
block with its guest address, so pointers can be followed offline.
- Spec (hex like WATCH, `;` between items): `VTABLE[:IDOFF=ID]:SIZE[,CHAIN:SIZE...][,near=R@POS]`
  - `VTABLE:SIZE` the first SIZE bytes of every found object of that vtable (FIND must list it);
    `:IDOFF=ID` only those whose u32 at IDOFF is ID (`*` all).
  - `CHAIN:SIZE` SIZE bytes where a pointer chain ends (`3a70>30>0` = add 3a70, read a pointer, add
    30, read, add 0: the block starts there); one block per chain, numbered 1, 2, ... after the
    object (0).
  - `near=R@POS` only objects whose position (3 floats at +POS) is within R m (decimal) of the
    hero.
- Example (every basic enemy within 15 m: the whole object, root dynamics, the behaviour root and
  its first child): `SHADPS4_BEACON_RECORD="1685908:1a90=*:4000,2570:400,3a70>30>0:200,3a70>30>0>10>0:200,near=15@2670"`.
- `.analysis/tools/recread.py FILE summary | field ID BLOCK OFFSET TYPE [FROM [TO]] | follow ID
  FRAME CHAIN TYPE` (TYPE f32/u32/u64/u8/vec3); as a module: `records(path)` and `Memory(recs)`
  to read any address that some block of that frame holds. Records hold raw addresses (they are
  meant for following pointers), so recordings are not byte-identical between runs; the values
  are.
- Size: ~16 KB per enemy per update before gzip.
- An object's drawn transform (its model 0 in the scene's rendering extension, 0xbc7650): chain
  `8>80>38>40:80` gives position +0, rotation +0x10 (x y z w), scale +0x20 (run roof_bot5:
  `16b9690:1a90=85393422:4a00,8>80>38>40:80`, the lost bot; FIND must list 16b9690).

### Savestates (checkpoint by replay)
Choice: a full snapshot (guest memory + thread contexts + kernel objects + GPU) is not workable
here: the title's threads are host threads (native x86 under Rosetta) stopped inside emulator code
with host locks and Vulkan objects; process checkpointing does not exist on macOS. With goals
1-3 a run is a function of build, settings and script, so a state is the recipe plus a check:
- `SHADPS4_SAVESTATE_AT=N:path` after trace row N writes a text file: build path, the settings
  that decide what the title does (determinism, scheduler, PATCH/POKE/POKEFIND/FIND, TITLE_*, fast
  mode, ...), the input script, and an FNV-1a digest of trace rows 1..N (hero column left out).
- `SHADPS4_LOADSTATE=path` applies the state's settings before anything reads them (a different
  value of yours is reported on stderr; FIND = the state's plus yours; WATCH / TRACE_EXTRA /
  pictures are yours), replays rows 1..N with the state's script in fast mode (unless pictures are
  near), writes no trace/watch/bin rows up to N, checks the digest at N ("trace digest matches" or
  an error in the log), and from N+1 on applies `SHADPS4_INPUT_SCRIPT` (absolute rows, or counted
  from the state with a first line `# frames relative`; without a script the state's own goes on).
  Inputs, WATCH, POKE and FIND then behave exactly as in a straight run; the frame column goes on
  from N.
- Cost: a load takes the replay time: 442 s to update 2900 (as long as the straight run). Limits: same build and game data; a state saved
  with fast mode off replays in fast mode (identical, see above).

### Results (2026-10-14, build det5/det7)
- Same frame-indexed script twice from boot to update 6000 in forest_castle (combat_frames.txt,
  det_env.sh enemy watch; runs C1/C2): trace.csv byte-identical (6000 rows). trace.csv.watch
  (84458 rows) identical except at one enemy (7ec7251b) during the punching, at its near-180-degree
  turn around update 4764 (quaternion y near +-1, w near 0): small float differences there flip
  the sign and grow. Other run pairs differ there or at 4799, or not at all (R1..R4). Hypothesis:
  reads of uninitialised memory (heap contents follow the allocation order, which is not
  deterministic) feed that computation; the heap layout differs in every run.
- Savestate at 2900 (C1), loaded and run to 3500 (S5): digest at 2900 matches; trace rows
  2901-3500 and watch rows 2901-3499 byte-identical to C1. Some loads report a digest mismatch:
  the boot still has one race (world map -> level: the hero column's object changes at update
  1315 in some runs, 1319 in others; every value the same); the rows after the level start were
  identical in all of them.
- Existing recipes: the old variables and formats are unchanged when SHADPS4_DETERMINISTIC is
  off; FLAT pictures and render-target dumps by seconds work as before; frame-indexed shots and
  dumps work with fast mode.
Open items: the remaining boot race (hero handle at 1315/1319); the chaotic enemy turn
(uninitialised-memory hypothesis); determinism costs 5x speed against plain fast mode.

Port replays of runs started from states/fc_landed.state (boot_frames.txt boot): the level's
objects start after frame 1304 (`beacon-level enemytrace|runtrace ... 1304`; found from the
standby enemies under moving joints, 0.17 mm mean). The default det_env watch lacks the brain
chain, so `enemycmp.py script` needs the enemy5-style WATCH for random goals.

### Worked example: one state, two scenarios
Save a state in forest_castle 30 updates after the hero lands from its intro drop (landing at
update 2760), then try two 300-update input scripts from it (runs E0, E1, E2, 2026-10-14):
```
cd .tools/harness
source ./det_env.sh                      # determinism + forest_castle recipe + enemy watch
SHADPS4_BEACON_FAST=1 SHADPS4_INPUT_SCRIPT=scripts/boot_frames.txt \
  SHADPS4_SAVESTATE_AT=2790:states/fc_landed.state SHADPS4_BEACON_STOP_AT=2790 ./run.sh E0 0
# scripts/example_left.txt:  "# frames relative" / "1 300 lx=0"
# scripts/example_jump.txt:  "# frames relative" / "1 300 ly=0" / "40 20 cross" / "160 20 cross"
SHADPS4_LOADSTATE=states/fc_landed.state SHADPS4_INPUT_SCRIPT=scripts/example_left.txt \
  SHADPS4_BEACON_STOP_AT=3090 ./run.sh E1 0 &
SHADPS4_LOADSTATE=states/fc_landed.state SHADPS4_INPUT_SCRIPT=scripts/example_jump.txt \
  SHADPS4_BEACON_STOP_AT=3090 ./run.sh E2 0 &
```
Both traces start at row 2791 with the same hero (0.004635, -0.042248, 51.202053); E1 walks left to
x = -2.44 (into a wall at 2970), E2 walks forward to z = 46.86 with two jumps. Each load took ~520 s
(two side by side); E0 426 s. Add SHADPS4_BEACON_RECORD to keep the raw enemies of such a scenario.

## Rendering comparisons (2026-10-15)

- Render-target dumps (`dump_fN/*.png`) of float targets are the linear values clamped to 0..1
  in 8 bits (the sky matches the port's linear HDR), at 640x720; the flat screenshot is the
  sRGB-encoded final output.
- Port side: `beacon-level render GAME LEVEL OUT.png --eye X,Y,Z --yaw 0 --pitch 0 --fov 90
  --play 0 [--exposure E]`; `--exposure E` turns off bloom and the colour grading and scales by
  2^-E, so HDR values can be read back (linearise, x 2^E). Frame 2900 of roof_light3: head at
  (0, 1.15, 3.035), level view.
- `SHADPS4_DBG_DUMP_BUFFERS=<pgm hash>[:<count>]` (build det12): logs the buffers bound to a
  shader stage with that hash (first 256 dwords, hex and float, `BUFDUMP` lines) for <count>
  draws. Used for the cloud shader's constants (runs roof_cloud_fs / roof_cloud_vs, dump at frame
  2900, STOP_AT 2910). Several hashes since det14: `=<hash>,<hash>,...[:<count>]` (count per
  hash); since det15 `SHADPS4_DBG_DUMP_FROM=<update>` skips dumps before that update and each
  `BUFDUMP ... base=` line carries `update=<n>` (runs roof_decal2 / roof_decal3).
- Screenshots by update (`SHADPS4_SHOT_AT` / `SHOT_EVERY`) are only written in fast mode
  (`SHADPS4_BEACON_FAST=1`, `FAST_SKIP=draw,dispatch`): run roof_tv1 without it saved none.
  With a loadstate the replay to the state's frame counts too: SHOT_EVERY=100 also shoots the boot.
- Reaching the goal: the VR player is held by vr_player_stopper_01..03 until the hero enters
  tb_stopper_0N_off; a hero far from it is flown back after ~4 s, and SHADPS4_BEACON_RIGZ moves no
  view. Run roof_goal5 (scripts/roof_goal4.txt + teleports, see objects.md "Goal") gets there.
- Seeing the controller: `pad=0,-0.25,-0.15` with `head=0,0,0,0,-40,0` puts it in the lower half of
  the view (run roof_pad5); pad z -0.4 is ~1.2 m ahead (beside the hero), z 0.15 out of view.
- det13: `TRACE   pgm vs= fs= blend= src= func= dst=` lines (each draw's programs and blend
  state) next to the draw trace; `SHADPS4_TRACE_DRAWS=<n>` sets how many draws the trace keeps
  (default 3000).
- Vulkan validation: `DYLD_LIBRARY_PATH=/opt/homebrew/lib VK_LAYER_PATH=/opt/homebrew/share/vulkan/explicit_layer.d BEACON_VK_VALIDATION=1`.
