# Spy Mouse (iOS) — what we learned

Research findings behind DoubleOCheese, the re-implementation of *Spy Mouse* for iPhone
(Firemint/EA). This is how the game works, how its data is stored and encoded, and the quirks
and bugs of the original code. Remaining work is in [plan.md](plan.md).

## Sources and conventions

- Everything here comes from the user's own IPAs and from runs of them: the executable's
  disassembly and decompilation (Ghidra 12.1.4, Apple `otool`), the data under `res/`, and
  deterministic reference runs in a locally patched touchHLE (verification only, never in the
  port). No outside game-specific material was used. Before the clean-room rule was set, the
  conversation that started this work briefly looked at an existing disassembly of an unrelated
  game (Sonic). Those files were removed, and nothing here comes from them.
- Addresses are armv7 vmaddrs of **version 1.0.5** unless marked 1.1.1. Struct offsets
  (`Player +0x130`) are research mappings only. The port uses its own types and never the
  original's memory layout.
- Detailed notes, evidence and tools are in the local research archive (`.analysis/` in this
  folder, ignored by git; the Ghidra projects are in `ghidra/`): `docs/level-1-1.md`, `level-1-2.md`, `world-map.md`,
  `world1-levels.md` … `world4-levels.md`, `reference-runs.md`, `v111-migration.md`,
  `feasibility.md`, `values-defaults.txt`.
- **HYPOTHESIS** marks what is inferred, not confirmed by code and a run.

## Builds

| | 1.0.5 | 1.1.1 |
|---|---|---|
| IPA | `SPY_mouse_1.0.5_ios_3.0.ipa` (132,320,817 bytes) | `com.ea.spymouse.inc-iOS3.1-(Clutch-2.0.4).ipa` |
| Bundle id | com.ea.spymouse.bv | com.ea.spymouse.inc |
| Executable | fat Mach-O armv6 + armv7, not encrypted (cryptid 0) | armv7 dumped with Clutch 2.0.4 (cryptid 0) |
| armv7 LC_UUID | F530882F-1868-332F-BCB0-3786F4B55E59 | 0B8B1588-9119-36F9-9EA6-EEA7B51316BF |
| Symbols | 19,429 (4,398 mangled C++ names) | 32,698 (full C++ names) |
| Edition | Valentine's Day: cheese crumbs and cats' thought bubbles are hearts | regular pictures, Retina set `res_960x640/`, world carousel, store, dossiers, a seventh world |

The 1.0.5 armv7 slice has SHA-256 `6c274b35…e827d`. Both builds keep full C++ class and method names
and embedded source paths (`…/AgentSqueek/src/Map.cpp`), which made the work far easier than on
stripped console binaries. The port now reads 1.1.1. Its rules were reconstructed and verified on
1.0.5 (see "Version 1.1.1" below).

## Program structure

- A C++ engine (`Engine`, `Scene*`, `Gui*`, `Manager*`, `Obj*`) over a separated platform layer
  (`Platform`, `PlatformIPhone`, `EAGLView`: OpenGL ES 1.x fixed function, touches; OpenAL +
  AudioToolbox; AVFoundation/MediaPlayer for the videos; FreeType 2.4.4 embedded). A large share
  of the 3 MB of code is EA middleware (EBISU social layer, Origin, Game Center, StoreKit, iAd,
  telemetry, GData, TinyXML, SBJSON). None of that is game logic, and the port does not need it.
- ABI: Thumb-2, ARM AAPCS, GCC 4.2 C++ (COW `std::string`), SjLj exceptions.
- Scenes are a stack. The top scene gets input; many scenes update the level underneath
  (results, cutscenes, pause darkening). `App::update` runs the scenes, then the ambience player
  and the iris (pinhole).

### Main loop and timing

- `iPhone::Update` @0xfcfec reads `mach_absolute_time` and runs `Engine::update(dt)` in ticks of
  `min(17, pending)` while at least 8 ms are pending, at most 3 per frame; the rest of the backlog is
  dropped. `Application::s_nTickTime` = 17 ms. `-[EAGLView initWithCoder:]` @0xfb97a uses 33 ms
  only on first-generation devices (iPod1,1, iPhone1,1, iPhone1,2).
- On a 120 Hz screen that scheduler would give mostly 8–9 ms ticks. Rules that act per tick
  rather than per millisecond (a carried item closes 20 % of its gap each tick, two `rand()` per
  tick, counters) would then run twice as often. The original only ever ran on 60 Hz-class
  screens, so the port runs whole 17 ms ticks only.
- `SceneGame::update` @0xea0fc order: two `rand()` (screen-shake noise), footprints,
  scene/HUD/tutorial, touch → path, `updatePure`, `updateGameMode` (objects, mouse),
  finger-slot clean-up, predictive path, `collideSphereSpecific`, `updatePaused`. The game clock
  (Scene +8) moves on early in `Scene::update` @0xea33c, after the shake draws.
- Drawing changes state. `Player::render` stores `fmodf(heading, 2π)` back every frame (this
  changes later movement bit for bit), and `renderCat` folds a cat's aim and steps its facing. The
  port runs this "render-time" step once after every tick (`Session::prepareFrame`), so pictures
  can be taken at any time.

### Randomness

- Game code calls only `rand()`: Darwin's Park–Miller generator (Schrage), seeded once with
  `srand(time())` in `Engine::initEngine`. `arc4random` occurs only in networking code.
  `randWithinRange(a, b)` @0x1892e0 = `((b − a) × float(rand())) × 4.65661287e-10 + a`.
- Every consumer is known and reproduced draw for draw: the level boot (music slot `rand() % 3`,
  each cheese's shine timer, ambience delays, the popup's posed mouse), two draws per tick,
  footsteps, sparkle bursts, type-601 triggers, ambience picks, idle animation choices, cat
  awareness intervals (`(rand & 0x7f) + 17` ms), give-up timers, the droid's watch timer,
  projectile pictures, map clouds.
- The game keeps its own generator in the port (it is simple, not hardware-fed).

### Floating point

- The original's ARMv7 arithmetic flushes denormals to zero. A cat's decaying steering offset
  reaches exactly 0 there and would stay a denormal in IEEE (run `l23_win`). The port enables
  flush-to-zero for game code only (`ScopedFlushToZero`).
- The original calls `sinf` and `cosf` separately. Clang merges them into `__sincosf_stret`, one
  ulp off, so game code builds with those builtins disabled.
- NEON float order was taken from the disassembly where it matters for bit-exact movement.

## Data formats

### PFile containers (`*.mbn` maps, GUI layouts, saves)

- `PFile::PFile(key)` @0x13a194: key = a C string, **its NUL included** in the rolling XOR.
- Optional header (17 of 156 maps): magic `0xFFFFA329`, djb2 hash (seed 5381, `h = h × 33 + byte`)
  of the decrypted body, `hash XOR fileSize` (`checkSafeValues` @0x1392d4). Without it the whole
  file is XORed from offset 0 and unchecked.
- Ints are big-endian (`getInt` byte-swaps); **floats are little-endian** (`getFloat` @0x139178 has
  no swap). A running XOR of the plaintext must end at 0 (the last byte balances it).
- Map key: the string after `…/src/Map.cpp` in `__cstring` (1.0.5 vmaddr 0x2CBC03). GUI key: at
  0x2d2198. The port reads both from the user's executable, never ships them.
- 139 other `.mbn` files start `3c cf 98 de` (a variant not decoded). `.uvw` files are not decoded.

### BRD chunk trees (`*.brd`, 175 files: level table, ambience schemes, map events, dialogue, cutscenes, animation sequences, popups, path info)

- `BrdReader::loadFile` @0x1fd020: bytes 0–1 are `7B 02`; the rest is XORed with a 29-byte key at
  0x2E8570; the XOR of all plaintext must be 0.
- Chunk = `u32be size, name\0, u32be nvars, vars, u32be nchildren, children`. Var types: 0 string,
  1 bool, 2 s8, 3 s16be, 4 s32be, **5 f32 little-endian**. Top chunk "rt" wraps the root element.

### Maps (`Map::loadBinVersion` @0x12732c)

- Magic `0x0FFBADFA`, background picture name, vertices `{f32 x, f32 y, u32 flags, s32 type,
  s32 index, s32 d, s32 e, string name}`, lines `{a, b, type, tag}` (tag < 0: disabled), a second
  line list (routes), end byte. Map space is y-up, 480×320, origin bottom-left.
- Vertex flag 2 = entity. Types 500..574 = decorations ("fluff"), 601 = timed trigger whose name
  is `"rand(a, b)"`, 1100–1112 = intro/tutorial markers, 990/993/998/999 = world map pads and
  markers. Route vertices start at type 0x65, 0x82 or 0x84. Line types: 0 wall; 0x6a.. special
  (0x6c, 0x6f–0x73, 0x7a, 0x7b, 0x7d–0x81, 0x87, 0x8d paint, 0x8f beam, 0x90, 0x91 are trigger edges
  skipped by sight tests; 0x71 conveyor; 0x73 ignored by most pushes; 0x81 boss trigger walls;
  125 hidden world-map triggers; 142 = demonstrating finger route).
- Scrolling ("infinite") levels are cut into 64-wide picture strips. They sort the **whole**
  entity container by x, decorations and start point included.
- In 1.0.5, maps 710/751/761/785 end early in the port's reader (not understood).

### GUI layouts (`res/guis/*.gui`)

- PFile with the GUI key; header magic 0x00101c0f, version (1.0.5: 17, 1.1.1: 26), an int from
  version 20. Then a tree: per level a count, per child its kind and version-dependent fields (id,
  name, picture, phone and tablet positions and sizes, colour, blend, relative, …). The phone
  placement is used.
- Kinds (`createChildren` @0x1c2bec): 0 item, 2 label, 3 button, 6 scroll frame, 7/8/11 progress
  frames, 9 checkbox, 10 rect, 13 group; 101.. are the app's own (`App::loadUserItem`): 101
  `GuiBounceFrameCM`, 105 plus frame, 106 posed mouse, 107 level goal button, 108 goal info,
  109 dots. 1.1.1 adds 114 `GuiLabelSM`, 115 `GuiScaleButton`, 118 `GuiFMWidget`, runs the
  table to 121, and changes `GuiScrollFrame` (v25) and `GuiSlideFrame` (v22).

### Text

- `res/text/text_<LANG>.txt`: UTF-16LE with BOM, CRLF, `\n` escapes; 9 languages. 1.0.5 has 1,410
  lines, 1.1.1 has 1,900.
- Code names a string by `getStrID`: djb2 over **signed** bytes modulo 0x106d643d, resolved
  through a compiled decision tree in the executable (1.0.5: 0x1eaadc–0x1f24a6, 1657 pairs). The
  port reads the tree from the user's executable.

### Program tables read from the executable

- Sound table `SoundList` (1.0.5 @0x340680: 162 files, groups @0x340910; 1.1.1: 186).
- Particle sprites `g_pParticleSprites` (1.0.5 @0x33f9c0, 20 bytes × 53; 1.1.1: 50 kinds).
- Decorations `g_pFluffData` (1.0.5 @0x33fe00, 24 bytes × 74: sheet, frame width, flags, frame
  ms, mode, autoplay; 1.1.1 adds ambush_01/02).
- Tuning values `g_pVal` (`Values::init` @0x12c654 defaults, names in `settingChanged`
  @0x12afc4; full list in the archive's `values-defaults.txt`).
- Eye tables, goal names (`g_LevelGoalNames`), star pictures, score notes. Their 1.1.1 addresses
  are in `src/data/program_layout.hpp`.

### Other files

- `level_data.xml.brd`: the level table. Six worlds (`<world type="0".."5">`), 86 levels with uid,
  map, number ("1-1"), name, theme, music list (comma separated, split into three slots), medal
  goals, trail length, mode (normal, infinite = chase, boss), unlock rules (`by1`/`by2`),
  secret flags, story names.
- `world/world_NN.mbn` + `world/worldNN/*.obj`: world map layout and its landscape (Wavefront OBJ
  from 3ds Max, loaded at 1.2×, drawn flat with map y = z).
- `world/pathinfo/world_pathinfo_NN.xml.brd`: the map's roads (map-editor drawing, y down).
- `anim_sequences/*.xml.brd`: `<Animation image frameWidth frameHeight>`, `<Sequence name
  numLoops>`, `<Frame cx cy delay ox oy>` (delay × 1000, truncated).
- `mapNN/amb_schemes.xml.brd` → `sound/sound/amb/<scheme>/amb.xml.brd`: ambience loops and
  one-shot groups (min/max seconds).
- `mapNN/map_events.xml.brd`, `dialogue/dialogue.xml.brd`: boss cutscene scripts and speech.
- `popup_tutorials/*.xml.brd`: popup pose, title and body ids, picture.
- Effects: 22 kHz mono WAV. Music: `.m4a` (22 in 1.0.5's survey, 24 tracks checked in 1.1.1), HE-AAC (AAC-LC 22,050 Hz core + SBR, output
  44,100 Hz) with an iTunes `iTunSMPB` tag (priming and valid sample counts, used for gapless
  loops). Videos `intro.mp4`/outro (H.264).
- Story cutscenes named by the level table (`cutscene/world01_chase/story.xml.brd`, …) are not
  in either IPA. `SceneStory::showScene` only reports the missing file, so nothing plays.

## Levels and progress

- World 1 and 2: 11 levels, a boss chase, a boss fight and a secret level each. World 3: 8 levels
  plus hidden H1/H2. World 4: 9 plus S1/S2. Worlds 5 and 6: 11 plus H levels. 1.1.1 adds a
  seventh world.
- Game modes: 0 normal, 1 infinite (boss chase: the view scrolls with the boss cat), 2 boss
  fight, 3 (seen in conditions only).
- Profile per level: unlocked, new, won, best total, goals won. `Profile::completeLevel`
  @0x162880 opens every locked level whose `by1`/`by2` is the won level (secret ones only from a
  run that found the secret); others lose "new".
- The profile's current world (`g_pProfile +0x18`, "curworld") picks world-dependent pictures
  (doors, teacup or bucket) and **which boss wakes in a fight** (see bugs).
- Trail capacity = the profile's "strength" (fresh: 1) raised to the level's trail length when
  the level starts. A raise shows the "carry more" popup.

## Core rules

### Drawing a path

- A touch beginning within 48 of the mouse starts a path; the finger offset (mouse − finger) is
  kept and shrinks × 0.95 once the path has more than 3 points. Candidates are appended when
  more than 0.98 away (`|d|² > 0.9604`), then resampled (spacing 8), smoothed (kernels
  [1 2 4 2 1]/10 and [1 2 1]/4), snapped and pushed off walls (radius reach + 0.01). Release or
  a full path (capacity 192): final resample and smooth, every point pushed off walls.
- `clipLine` @0x128ff4 keeps the path out of walls: the two crossed lines nearest the start;
  where they share a vertex the end goes 13 along the corner's bisector, else to the nearest
  line's closest point plus one unit back.
- `snapPath` @0xf1308 ends a path on an object: candidates must be snap targets (or holes when
  enough cheese is predicted, or a bank hole after a predicted cheese), not glowing from an
  earlier snap (the intro's pulse does not block). Nearest within reach + radius. A hole gets its
  centre appended (the drawing ends), another object a point at r − 2 toward the path. A full
  trail refuses.
- Predictive path: cheese snapped while drawing counts as predicted. Home holes' door lines are
  disabled once enough cheese is carried or predicted.
- Touch → map is (x, 320 − y) for unscrolled levels. Scrolled levels add the view offset.

### The mouse

- Start = entity + (0, 18.5), heading 3π/2, reach 10, size 37.
- Speed: run speed 1.2; with trail weight w > 9 × 9/w; water × 0.9; some states 0 or × 0.5. Step
  per tick `s = dt × ((0.2 × speed) × boost + speed) × 0.05`, × acceleration, × 5 while boosted.
- Path following: while `dx² + dy² ≤ reach` (**not** squared reach) the index advances; then
  `heading = atan2f(dy, dx)`.
- Sprite direction: 16 rows, `(int(fmod(a + π/16, 2π) / (π/8)) + 12) mod 16`. Walk cycle 4
  frames at 75 ms. Idle: after 99 ms standing, `idle1` facing the heading's quadrant, then
  alternately `idle1` and `rand() % 2` of {scratch1, sniff1}; after 120 s `rand() % 2` of
  {yoyo1, juggle_loop}. Growing out of a hole counts as standing.
- Footsteps every `|pos − last|² ≥ 101`, alternate feet, one `rand()` for the sound (always
  drawn, even when a teacup replaces the sound).

### Cheese, trail and scoring

- Cheese types 6..10 (big to small): weights 17/14.5/11.5/9/7, points 50/25/20/15/10.
- Trail weight = average weight of the carried items, truncated, plus average × 0.2 per further
  item. A balloon on the trail makes it weightless.
- Pickup (`Player::pickup` @0x10cb10): inserted after items of the same type, else before the
  first item with a higher score. Balloons go first and, like type 15, do not count against
  capacity. A full trail refuses. Each pickup asks `requestExitOpen`.
- Trail following (`Player::updateTrail` @0x10d858): each item follows the one in front at
  spacing 18 by `pos = (target + 4 × pos) / 5`, then is pushed out of walls. A link blocked by a
  wall uses spacing 2 — and **every item behind it inherits the 2** for that tick (see quirks).
  While hiding, items are pulled into the hole by k × distance² × dt.
- Score of a trail = Σ points × poker-hand bonus, truncated, × 2.0 (`exit_bonus_multiplier`) into
  the home hole, truncated again. Hands (`getPokerHand` @0x201eb0, from the top two counts and the
  total): bluff 1, pair 2, threes 3, two pair 4, fours 4, full house 5, straight (five different)
  7, flush (five of a kind) 6. Six or more of one kind is a bluff.
- Crumbs (type 260, hearts in 1.0.5): a run of crumbs eaten within a second of each other is
  worth 1, 2, 3… points, with rising notes.
- Level clock: starts with the first path point, ticks every 200 ms of game time, stands while
  won, while hiding in a safe hole, and after a checkpoint until a new path. The results' time
  bonus is the seconds left.

### Holes and exits

- Home holes (0xf0..0xf3) open once delivered + carried cheese reaches the total. Their wall line
  is a one-way door while closed: it is enabled when `dot(A − mouse, cross(B − A)) ≥ 0`. Entering
  one hides the mouse (slide 200 ms), delivers all carried cheese and wins the level.
  0xf4..0xf7 are secret exits (a win through one sets the secret flag).
- Wall holes 20..23 come in tag pairs: in one, out of the other. A wall hole whose tag is 6
  modulo 8 becomes a **munch (safe) hole** (type + 0x154 = 0x168..0x16b): it opens on any cheese
  pickup, banks the trail like home (scored), stops the clock while inside, and closes when the
  mouse leaves with an empty trail.
- Pipes 200..203 hide the mouse with the whole trail at once; flip-flaps 204..207 push it out
  two radii. Each use counts for the "pipe" goal.
- Trapdoors 36..39 and house doors (0x21), and the sliding door 0x140, are "room doors": opened
  when the shown floor's own cheese is delivered or carried; using one scores and delivers the
  trail, goes down a floor (iris in the mouse's silhouette) and takes a checkpoint.
- Floor doors 30/31 (up/down), 34 (to the floor in its tag), 35 (to the ground floor). A
  floor's first entry through a plain door shows "Hidden Area!" if the level was not won before.

### Results and goals

- Level over → (cheese still carried: wait 500 ms) → results. Caught → "delay results on level
  fail" 1000 ms → the level-lost iris → reload from the snapshot with "Try Again!".
- Results screen: counts at 0.75 × dt, points over 1000 ms, then the time bonus, medals pop,
  improved-score badge only when beating an earlier completion. Continue is pressed by itself
  after 14 s (`results_time_out`) and a pulse.
- Goal kinds (`Profile::isGoalAchieved` @0x161c0c; names `g_LevelGoalNames`): score (total ≥),
  time (whole seconds ≤), paths (paths started at the mouse ≤; extensions do not count),
  combo_pair … combo_flush (number of trails with that hand **equals** the value), spotted (cats
  that saw the mouse, equality), bump (cats pounced into walls), all_crumbs; kinds 16..27 are "at
  most" counts of wall holes, safe holes, switches, furniture, cups, chillies, balloons, TVs,
  pipes, string, fish. Goal groups in parentheses take the first part's text and picture
  (HYPOTHESIS). 1.1.1 has 30 kinds (adds "collapsible", "poisoned").

## Objects

The controller table (`ObjController::createAllControllers` @0x18a5d8) maps type ranges: Cat
50..59 and 285; Switch 65..67, 80, 81, 150..155; Hideable 20..27, 29, 40..49, 200..207, 240..247,
360..363; FloorTransport 30, 31, 33..39, 267, 268, 320; Doors 210..225; Spawner 269..275; Pickups
6..19, 72, 97..99, 149, 209, 250, 252, 260, 330; ForceGenerator 276..281; Boss 300..306, 265, 266.
Others are handled by `Object::init` itself.

Common: radius = factor × frame size, position = vertex + half the height. Objects are drawn by
y descending in passes low (1), normal (2/4) and high (8) by their pass mask; projectiles are
drawn after each pass's objects. `Object::updatePositions` @0x100d8c pushes movers out of walls
(the toy mouse every update, carried or not).

| Type | Object | Notes |
|---|---|---|
| 6..10 | cheese | `cheese0<11−type>.png`; shine every 0–5 s; sheen sweep across loose cheese |
| 11 | clockwork mouse | picked up, thrown within 50 of a trap; disarms it |
| 12 (0xc) | twine | lure: cats chase it, play 15 s, then ignore twine 15 s; rolls when stopped |
| 13 | bonus heart | |
| 14 | chilli | speed: score boost 4, 16 s, red tint |
| 17 | teacup (bucket in world index 2) | worn: cats cannot see the mouse (but still catch by touch); 10 s, flashes from half time |
| 18 | balloon | weightless trail; pops at any hole, by shurikens |
| 19 (0x13) | catnip | lure: eaten by the cat (8.2 s) |
| 20..23 | wall holes | tag pairs; tag ≡ 6 mod 8 → munch hole |
| 24..26 | couch (furniture) | animated; hiding not reconstructed |
| 36..39, 0x21, 0x140 | room doors | trapdoor, house door, sliding door |
| 50 (0x32) | brown cat | |
| 51 (0x33) | orange thief cat | steals cheese (not reconstructed) |
| 52 (0x34) | black ("mean") cat | |
| 53 (0x35) | boss cat (chases) | |
| 54 (0x36) | blind cat | |
| 56 (0x38) | ninja cat | shuriken bursts |
| 59 (0x3b) | Fluffy's henchman | boss scenes |
| 60 | mouse trap | |
| 61, 94, 95 → 0x3d | cheese machine | dispenses held cheese on touch or message 0/3 |
| 65..67, 80, 81, 150..155 | floor buttons | open, close, timed 10/20/30 s; 151..154 pressed by cats only |
| 96 (0x60) | TV | hypnotises cats in sight |
| 97..99 (0x61..0x63) | bonus pickups | 10/15/20 points in chases |
| 149 (0x95) | trap prize | in a chase eaten at once, no points |
| 200..207 | pipes, flip-flaps | |
| 240..243 (0xf0..0xf3), 244..247 | home holes, secret exits | one-way door while closed; picture by side |
| 209 | second speed item | ×7.2 for 8 s |
| 210..225 | barred doors | 210..217 closed, 218..225 open; bars by world |
| 253, 274 | bomb, bomb dropper | world 3 fight |
| 260 | crumb | |
| 300, 301, 302 | vacuum, droid, Spitter | bosses of world index 2, 0, 1 |
| 330 | key | thrown by a beaten boss; opens the boss exit |
| 359, 0x166, 0x167 | exit doors | chase and fight exits |
| 366, 367 | rail sphere, wall device | world 2 fight |
| 0x113, 0x11a, 0x11b, 0x9b | launcher, reflectors, light, fight buttons | world 4 fight |
| 412, 421 | car, butterfly ("roamers") | route end mode from vertex type 0x66/0x67 |
| 500..574 | decorations | from `g_pFluffData`; 572 bush plays on its 601 trigger |
| 601 | timed trigger | `"rand(a, b)"` → `a + rand() % b` ms |
| 1100..1112 | tutorial/intro markers | inert; spotlights in the level intro |

### Messages

`SceneGame::sendMessage` @0xecc74: `{kind, tag, sender, type, extra}`. With "apply to map" the
tag's walls react first (0 toggles, 2 on, others off); then every object on the sender's floor
(the shown floor for the scene) whose tag or type matches receives it, active or not. Kinds:
1 open, 2 close, 3 machines dispense, 4 boss parts, 5 timer warning.

## Cats

- **Route**: from a route-start vertex (0x65, 0x82, 0x84) whose |d| is the cat's tag along the
  second line list, no length limit. End modes: 0 stop, 1 turn back, 2 restart, 3 link another
  route. A point with a pause sits the cat purring with "zZz" for that many seconds.
- **Values**: walk 0.5, run 1.15, turn 1, sit 2000 ms, view 400 units over 145° (× 0.8 wide:
  72.5° either side), close 50, "?" 600 ms, aim 700 ms, pounce 550 ms, daze 2500 ms, lead 0.1.
- **Awareness** every `(rand & 0x7f) + 17` ms: playthings (TVs), then the trail (only twine and
  catnip; cheese only for the thief cat), then the mouse (`canSeePlayer`: cone, distance, clear
  line; trigger edges never block).
- **Chase** steps: "?" stop 600 ms (give-up timer 20–36 s), run along a way-back trail to where it
  saw the mouse, leading a moving mouse (`interceptTarget`), within 50 aim 700 ms, pounce straight
  on at a slowing speed; into a wall it is dazed 2.5 s; at an empty spot it sits, then walks back.
  Stuck 16 ticks without sight of its next point → looks about 1 s, turns back.
- **Steering**: whiskers 23 ahead at ±35°; chasing cats keep clear of each other (flocking).
- **Catch**: on touch, unless in a harmless state; the cat moves to the midpoint, fight cloud,
  the mouse dies, the last trail item drops (then one more every tick, silently). Being caught
  reloads from the checkpoint with "Try Again!". Cats are reset (`resetCats`) except in boss
  fights.
- **Kinds**: black cat — ×1.6 speed, lead ×8, never stops to aim, accelerates to ×1.84 over 1.4 s
  in a chase, makes the mouse jump on spotting; ignores carried cheese. Blind cat — walking
  pictures only, sees nothing (how it finds the mouse is open). Ninja — ×1.45, stops in the open
  to throw bursts of three shurikens at where it last saw the mouse, with no lead. Boss cat —
  in chases, walks out at its route's end; seeing the mouse loses the level at once.
- **TV** (0x60): a cat that sees a playing TV walks back to it, takes a seat and is hypnotised
  for good (the TV plays forever once touched).
- **Lures**: a chasing cat does not catch a mouse whose trail holds twine or catnip; on reaching
  the lure it takes it and plays.
- Spotting cries per kind: brown 0x12, orange 0x17, black 0x13, ghost 0x14, ninja 0x16, lava
  0x15, henchman 0x8e (world 6 only); none for the boss, blind cat or Fluffy.

## Bosses

- **Chases** (mode 1, maps 736/555/565/556): the view scrolls with the boss cat; the HUD has only
  the pause button; the exit door (359) opens for the boss at the view's end and ends the level
  for the mouse; no results — the fight follows.
- **Fights** (mode 2): a cutscene from `map_events` (Fluffy appears, speech from `dialogue`,
  the exit closes), then a checkpoint (`SnapshotHandler::takeSnapshot`: clock, score, walls,
  player and active objects' fields). Being caught reloads it with "Keep Going!". A fight starts
  no music of its own (the map's goes on). Win: `blowUpDroid` (explosion, the key arcs to marker
  333), the key opens the exit, and the first win shows the world card.
  - World 1, droid "Accela" (301): rides a rail; on sight rages 500 ms then charges, rolls on
    until it hits a wall, bounces back. Lured west along the top corridor, it trips two trigger
    walls (type 0x81, tag 120): the first sets it sliding, the second blows it up.
  - World 2, Spitter (302): circles its round, throws fork/spoon/knife (`rand() % 3`) at a mouse
    in sight. Thrown cutlery stuck in both rail spheres lights both wall devices, whose beam
    breaks it.
  - World 3, vacuum (300): walks its route and sucks the mouse in within 160 in sight (three
    stages). Buttons chain doors to a cheese button that drops a bomb on a conveyor; the vacuum
    swallows the bomb.
  - World 4: a launcher on the left wall fires shuriken bursts; coloured buttons flip reflectors;
    a shuriken breaks the light over the orange button; a shuriken routed into the henchman's
    balloon dunks him.

## Scenes and presentation

- **Level intro** (`SceneLevelIntro`): spotlights on the mouse and markers, the level name; fades
  at 4 s (2.5× faster on a touch, and the touch still reaches the level). The cheese pulses as it
  ends.
- **Demonstrating finger** (level 1-1): its route is data — the map's type-142 lines, each vertex's
  `d` the wait time. Removed as soon as the player's path has a point.
- **Popups**: carry-more (`new_cheese_slot0N`), level goals (1-3), safe holes (2-4); the posed
  mouse blinks and looks around on `rand()`. Goal info popup on tapping a medal.
- **Pause menu**: goals as ribbons, darkening; retry and exit close the iris first.
- **Kiska's offer** (`SceneLevelSkip`): first time on maps 758 (2-8) and 521 (3-6), else after 8
  lost levels in a sitting, at most twice, at least 22 hours apart (`time()`). Buying is a store
  purchase; offline only "Later" is meaningful.
- **Iris** (`PinholeManager`): kinds 0 (pattern), 2 (cat silhouette, level lost), 3 (mouse
  silhouette, floors and boss modes), 5 (bonus pattern, results); speed 0.0008 (× 1.5 for kind 0).
  A finished close still reports "animating" for one more update, so whatever waits for it acts a
  tick later.
- **HUD**: trail slots, score with count-up, hand and multiplier; eased in and out.
- **Text**: FreeType 2.4.4, fonts 14/20/25 px from `SPYmouse_efigs.ttf`. Horizontal alignment
  by ink; vertical by the font's 'A' top and 'y' bottom, not the string's ink. Line wrap measures
  ink, not advances.
- **Screen shake**: a delayed, decaying four-corner step with a projection scale, for the level,
  HUD and results card (not the iris).
- **World card** (`SceneWorldCompleteResults`): "World %d Completed", goals of the world. Continue
  moves the profile to the next world.

## World map

- One 480×320 screen per world from `world/world_NN.mbn`: level pads (990, d = uid), house markers
  (993), previous/next world pads (998/999), gates per level (walls tagged with the uid while the
  level is not won), hidden trigger lines (125) that open secret levels.
- Roads from the path-info file; routes are cheapest stop-to-stop by straight-line distance,
  walked by the same `Player::update` as in levels. Tap a pad to walk there; the Play bubble picks
  the free spot of six; Play pressed before arriving teleports the mouse (blur sprites).
- New levels are revealed with a masked road fade (990 ms), grass and house, then the pad.
- Clouds in five lanes on a 30° slope, started on `rand()`.
- Arriving in a new world (state 8): the mouse walks in beside the previous-world pad and on to
  the current level.

## Sound and music

- Effects through `SoundList`: 8 playing voices with stealing; `playSound` restarts,
  `playSoundOnce` starts only if not playing (decided on game time). Ambience loops and random
  one-shots per floor scheme; stopped when the results show; looping object sounds (the shared
  purr) stop on a level win.
- Music: slot `rand() % 3` of the level's list (`CM_Menu1.m4a` if empty); on the win
  `<track>_Win.m4a` once. Boss fights keep the map's music until the mouse is caught.
- Gapless HE-AAC: trim = priming × 2 + 962 (SBR delay) output samples, length = valid × 2. The
  port's own decoder matches AudioToolbox on all 24 tracks (same length and alignment, core about
  61 dB SNR, high band within 0.5 dB).

## Version 1.1.1

- Same container keys. Program tables moved (read at 1.1.1 addresses). The sound table grew from
  162 to 186 entries (new cat and dart sounds, seven firework sounds); every effect was renumbered
  by file name. Particles shrank from 53 to 50 kinds, with changed priorities and flags. Text ids
  were remapped by their English text (e.g. "Busted!" 0x268 → 0x270, "World %d Completed" 0x261 →
  0x269, Play bubble 582/583 → 590/591).
- Struct layouts changed (Player fields after +0x130 moved about +0x180, Object +8 near +0x114).
  Of 452 functions cited in the notes, 303 differ after masking addresses; how many change rules,
  not just offsets, is open. 54 maps changed in content.
- RAM globals 1.0.5 → 1.1.1: Player::s_pGame 0x3475d0 → 0x5fb200, g_pEng 0x346460 → 0x5fa060,
  g_LevelState 0x346690 → 0x5fa290, g_pProfile 0x3482b0 → 0x5fbee0, g_pVal 0x34823c → 0x5fbe6c,
  Object::s_pGame 0x346a80 → 0x5fa6b0.
- 1.1.1 boots level 1-1 through the world carousel at frame 1185 (1.0.5: 1007). After a level's
  results the original shows "Please wait..." with a store tip and did not reach the map in
  touchHLE (open).
- `levelSkip.gui` was renamed `level_skip.gui`; the IPA has an empty deflated file
  (`res/build_notes.txt`).
- Profile: the current world is +0x1dc (1.0.5: +0x18). Several set-ups read it rather than
  the level's world: the teacup/bucket picture (bucket only in profile world 2 and outside
  challenges), Fluffy's facing, the henchman's pictures, the boss of `bossTypeOfWorld`.
- Popups at the start: `SceneGame::initGame` @0x106014 only records the notice (+0x57d8c: 1
  carry more, 2 safe holes on map 26, 3 level goals on map 704, the last two until the level is
  won); `SceneGame::update` @0x103528 pushes `popup_tutorial` on its first run while the
  power-up bar is still and returns at once; the iris then finishes 7 times faster
  (`PinholeManager::finishTransition(false)`). A tap pops the popup at once.
- The menu's closing iris also reaches the new game's `pinholeCallback` (@0xfc358) at boot; it
  acts only with the game scene on top, i.e. in the boss chase (no intro): cheese and mouse traps
  pulse.
- No automatic Kiska offer after the level intro: `level_skip` is pushed only by the power-up
  bar (`SceneGame::guiEvent` @0x103e00). Power-ups (banana, bomb, roller skates, hamster ball,
  Kiska, X-ray specs), the store and Digger's tutorial are new (plan.md 1a).
- Boss chase: the boss cat starts with velocity (0.05, 0.05) (init @0x19fa16); the view's
  scroll is set from the cat's velocity *before* its move (@0x1a14a4), so the first tick scrolls
  0.05. At its last route point `patrol` (@0x1a408a) runs it at 2.5× speed; within its radius of
  the point it stops for good (Object +5 cleared, scroll 0, then set once more from the last
  step). The exit door reopens only for a boss cat still updating; the mouse walking out shuts
  the door behind it (LevelState +0x18, also blocking path drawing) and then stands.
- Checkpoints: a safe hole (`Player::hide` @0x124d70) and a checkpoint line (wall 0x72, which
  switches itself off, `collideObjects` @0x10085c) take a snapshot. `Snapshot<Object>` keeps the
  radius as an int and the route-trim count (+0x208, replayed with `Path::trim`); the view
  (+0x1393c) is kept as an int. A reload runs the cats' init again over the kept objects and
  then rolls the snapshot back: aim, calm timer, way back, steering and chase timer start over,
  and a back-and-forth route walks forward again (run `l3h2_win`). The Spitter's radius comes
  back as an int too (`lbf2_caught`). `Snapshot<Player>` (rollback @0x10bc7c) restores state
  (+0x2b0), floor (+0x2b4), position, heading, the trail's first item (+0x2f4), the scoring
  fields +0x3a0–+0x3ac and the hole tag (+0x36c), but not the hole itself (+0x2e0): a mouse
  rolled back into a safe hole comes out by a touch at once, without the 80 ms grow, the hole's
  sound or the slide (`Player::setState` @0x123fd2 needs +0x2e4; run `l24_win`).
- `Object::updatePositions` @0x115430 never pushes these kinds out of walls: 0xb (toy mouse),
  0x37, 0x109/0x10a, 0x12d, 0x12f, 0x14a, 0x165–0x167, 0x1a4/0x1a5; nor states 0x24/0x40.
- `updateTouchPoints` @0x1017a8: no drawing in player states 5, 9, 0xC, while the door timer is
  not past or once the exit shut. The door timer is Player +0x364 (SceneGame +0x13190): using a
  door sets it 10 s ahead (`startFloorTransition` @0x1b26ca), arriving on the floor
  (`SceneGame::changeFloor` @0xfc054) sets it to now, so doors snap again right after the
  arrival (`snapPath` @0x10138c; run `l17_win`). A shut door or trapdoor (state 0x29) is not
  used at all (`ObjControllerFloorTransport::touchPlayer` @0x1b232c), so standing on one does
  not hold the timer (run `l37_win` 6401).
- Safe (munch) holes 0x168–0x16b: touched out (`Object::getTouched` @0x115e44), the trail's items
  other than 0xb, 0xc and 0x13 stay in the hole (inactive; their carried flag is left set);
  `setState(0)` then closes the bank holes. Leaving a hole sounds 0x33 for wall holes and 0x28
  for types below 0x14, 0x18–0x1d and 0x28–0x30; a munch hole is quiet (@0x124060). A trail
  item of type 0x12 is gone on leaving any hole; 0x13 is not set running again (@0x123f6c).
- `Object::onFloorChanged` @0x11208c: on a floor change, objects left on other floors stop
  their looping sounds (0x1d, 0x34, 0x72); twine a cat has had (state 0x22) and catnip still
  being eaten are gone (run `l45_win`).
- A touch lets a hidden mouse out by the nearest object within 45 whose tag matches the mouse's
  hole tag (+0x36c; `updateTouchPoints` @0x101bfe); `getTouched` then refuses an object that is
  not an exit (+0x870) or has +0x873 set, and needs a tag of 0 or more outside the boss modes
  (LevelState +0x14 = 2).
- The best total of the boss chase is LevelState +0x24 (cheese home), else the total
  (`Profile::completeLevel` @0x1735e8; new-best flag LevelState +0x1d).
- Cats: Fluffy (0x3a) and the henchman (0x3b) start in the cutscene state 0x40; Fluffy faces by
  the profile's world (init @0x19fa92) and shows his pose (part 2, frame 5, `postInit`
  @0x19fbc4; 1.0.5's `ScriptSequence::showFluffy` is gone). The henchman has no walking
  pictures outside world 6, so he is not drawn there. The cutscene part index is Object +0x228.
- Droid (0x12d): `postInit` sets aim = heading; `renderAccela` @0x1acd98 sets the body to the
  heading's sixteenth (getSpriteDirFromAngle) every frame outside steps 1, 2, 5–7, 9.
- Path drawing: a finger held since elsewhere starts a path when it comes within reach of the
  mouse and is more than sqrt(200) from where it touched down (`updateTouchPoints` @0x101cb8);
  that path starts at index 0 (one step along heading 0). The finger keeps its path (even one
  that ended in a hole or filled up) until it lifts.
- Projectile pictures (`ProjectileManager::load`): 0 shuriken, 1 lava fireball, 2 world 4's
  shuriken, 3–5 fork/spoon/knife, 7 poison dart (1.0.5 had a second ball, so its later numbers
  were one higher). Walls do not stop picture 2. The ninja's wait between throws is the burst
  cooldown (600 ms), not the throw cooldown; the Spitter drops a wound-up throw when it loses
  sight of the mouse. A shuriken death (player state 0xB) sets the level-over wait to 1300 ms
  (the boss modes keep it). Cutlery kills like a shuriken (projectile gone, state 0xB) with
  the new "cutlerydeath1" sequence (`player_cutlerydeath.xml.brd`); a worn teacup stops
  shurikens (`Player::touchProjectile` @0x1244a4).
- State numbers that moved in 1.1.1: reflectors 0x3d/0x3e (were 0x39/0x3a), the breakable
  lights 0x2d/0x2e (were 0x29/0x2a). The secret home holes (0xf4–0xf7) run like the others.
- Level over: while the mouse still carries something the wait restarts at 250 ms (1.0.5 500)
  every tick. Results: past the 14 s time-out the ring is cleared every update.
- Boss leftovers: the key is thrown 0.0001 above the boss (`blowUpDroid` script) and has its
  picture (32 x 32, radius 13.58); a boss win shows the world card when the fight was not
  completed before. The rail spheres (366) are 48 wide with 0.7 of the usual radius; world 4's
  henchman has radius 15. Doors and buttons come back from a checkpoint with their state and
  frame.
- Dossiers: 21 pages (names 0x5F6100, types 0x526C80, tips 0x526AD0, last seen 0x526CE0);
  `SceneDossier` eases folder/pictures by 1/8, the paper by 1/16 (@0x286f96); the first-find
  note waits for the paper to settle (the pictures' step is re-armed while it is near).

## Quirks and bugs in the original

Kept in the port where they change behaviour; listed so nobody "fixes" them by accident.

- **Path following compares squared distance with the unsquared reach** (`dx² + dy² ≤ 10`), so the
  mouse advances to the next point within about 3.16 units, not 10.
- **Trail spacing leaks**: once one link's line touches a wall, every item behind it uses spacing
  2 for that tick, even with a clear line (register reuse at 0x10d8e6/0x10d950/0x10d96e).
- **The boss to wake is chosen by the profile's current world, not the level's**
  (`SceneCutscene::hideScene`, `blowUpDroid`, `showFluffy` switch on `g_pProfile +0x18`). A fight
  played under another world's profile wakes the wrong boss or none. The port uses the level's
  world.
- `getPokerHand` walks the trail to check whether a straight is in type order, then never uses
  the result.
- `getValidTrailTarget` compares the lure's distance with the sight distance **squared**, so the
  distance test always passes.
- `clipLine`'s ignore-type argument receives the view position and the view argument 0 (the
  arguments are swapped at the call); kept as found.
- `setReturnPos` with a single point left reads before its array; a droid charge whose rail index
  runs past the end reads beyond the rail. The port uses safe values; no run reaches either.
- The world 4 launcher's heading is set every tick to the float 2π, so its shots' sine is 1.7e-7,
  not 0.
- Pictures stepped twice per tick: TVs (400 ms frames show for 200 ms), floating balloons,
  trapdoors (150 ms frames last 4–5 ticks), wall devices — their own update and the generic object
  update both advance them.
- `Sprite::clone` re-sets the image, so a thrown knife or fork stays on frame 0.
- `ProjectileManager::update` **ends early** after a projectile turns to fading (the slots after it
  wait a tick), and the extra `update(0)` after a rail-sphere hit moves by `s_nTickTime`, not its
  argument.
- `lineSegmentIntersection` with a zero-length line computes NaN everywhere, and the `-2 <= t`
  comparisons make it cross nothing.
- `isFloatEqualToZero` tests only the exponent bits 0x7c000000.
- `Path::smooth` does not count the last non-closing 3-tap point; `Path::trim` marks only decrease
  while positive.
- A finished iris close keeps `isAnimating` true for one more update.
- The scheduler gives variable ticks (8–17 ms) on faster displays, and per-tick rules then run
  faster; harmless on the 60 Hz devices of the time.
- Story cutscenes named in the level table are missing from the shipped IPA.
- 1.0.5's main menu shows Valentine decorations regardless of the date (`time()` does not choose
  it; source unknown).

## Verification method

- **Reference harness**: touchHLE (commit ca1de89c, 2026-10-13) with a local patch (never
  upstreamed: touchHLE refuses AI-generated code). It reports an iPhone 3GS, sets the clock to 1 s
  + frames × 17 ms (exactly one tick per presented frame), pins `time()`, uses Darwin `rand()`,
  replays scripted touches, takes screenshots and RAM watches, traces `rand()`, files, OpenAL
  and AudioQueue calls, and can poke memory (`poke 3482b0*+18 1` sets the profile's world). For
  1.1.1 it also stubs EA's online update and answers unknown classes/selectors with nil.
- **Synthetic levels**: the original reads `Library/Caches/res/<file>` before its bundle, so
  edited maps and level tables (re-encoded with balanced XOR bytes) test single rules. Later
  levels are played through level 1-2's map slot.
- **`doc-verify`** replays a run's touches from the level boot with the run's generator state and
  compares, every frame, the mouse, path, every object, cats, projectiles, the level state, the
  number of `rand()` draws and the generator state, and the top scene. Sound is compared event by
  event; screenshots by mean pixel difference (typically 1–2 of 255, from texture filtering and
  FreeType versions).
- Shot alignment: the shot of frame F shows the state after F − 1007 ticks (1.0.5).
