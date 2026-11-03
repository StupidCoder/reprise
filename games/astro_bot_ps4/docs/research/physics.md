# Physics (research started 2026-10-13)

Goal (user, 2026-10-13): re-implement the original physics code with the same math, since the
feel of the game depends on it; replace the port's approximations (enemy sphere slide, thrown
body as a projectile, enemy separation, hit spheres) with the recovered code.

## Layers found in the executable
1. **Physics library**: Sony Physics Effects (PFX), linked statically: strings ScePfxJobWorker,
   PfxTask, GpfxFindOverlapPairs; .pfb files (`pfx/` folder) are its body files. The world step
   runs as jobs, so a callee walk from the wrappers reaches only ~170 functions; the library's
   extent is still to be mapped (candidate regions 0xc60000-0xd30000, 0xde0000-0xe00000).
2. **Engine module** `app\Module\Physics` (asserts): PhysicsWorld (0xca8c50 scale, 0xca87e0
   reset), PhysicsWorldImpl (14 functions 0xcb2170-0xcbbc30), PhysicsRigidBody, PhysicsImportPfb,
   PhysicsParticleWorld; `app\Game\Extension\IfcPhysics.cpp`; cloth (`app\Game\Physics`).
   - World scale (world +0x58): the library works in scaled units; the module multiplies
     velocities and gravity by it on the way in and divides on the way out (0xcb1210 / 0xcb1250,
     0xca5fa0). The multi-platformer sets 3.0 (MupMain 0xca8c50 with 0x123bddc).
   - Gravity (0, -9.8, 0) at reset (0xca87e0, constant 0x1304080).
   - Body API used by the game: 0xca5f20 position, 0xca5f50 / 0xca5fa0 linear velocity,
     0xca6080 / 0xca6090 angular velocity, 0xca6380 / 0xca6390 filter, 0xca5c70 motion type,
     0xca5e20 / 0xca5f40 transform; queries 0xc22cb0.. (sweep / cast: from +0x20, to +0x30,
     radius +0x40, masks +0x44 / +0x48, run 0xc22e20, results 0xc23100 / 0xc23110 / 0xc23140).
3. **Game controllers**:
   - MupRootDynamics (enemies; 56 source-labelled functions): step 0x7dfe50 = 0x7e0020,
     0x7e0770, 0x7e1600, mode-dependent velocity limit, 0x7e20e0 (ground probe), 0x7e4850;
     about 2,700 decompiled lines. It turns clip root motion (flags +0xb0, SpeedScale +0xa0 of
     the controller) into the body's velocity and handles ground, steps and contacts.
   - Hero body step 0x66ab90 (1,641 lines; ported as the port's own collide-and-slide,
     verified frame-exact on the traced paths except one corner, hero.md).
   - Thrown enemies: DeadCollision body switched to a free rigid body (0x2e2750).
   - Physical objects (MupPhysicalObject / Block / Trigger).

## Plan
0. Inventory: map the PFX functions (job entry points behind PfxTask, the step pipeline:
   integrate, broadphase GpfxFindOverlapPairs, narrowphase per shape pair, contact solver,
   sleeping), the shapes used by the game's .pfb files, and the query functions.
1. Port the library pieces the game uses, in clean C++ (src/physics), with the original's
   math: rigid body integration and damping, shapes and contact generation for the shapes in
   the data, the solver (iterations, friction, restitution, limits such as the angular speed
   cap seen as 60 rad/s), queries (ray, sphere sweep) with their masks.
2. Port the module layer (world scale, body API, pfb import).
3. Port MupRootDynamics and replace the enemy stand-ins (separation, root motion, sphere slide,
   hit sphere), then the thrown body, then physical objects.
4. Re-check the hero's collision against the ported queries (its port currently uses the port's
   own CollisionWorld).
Verification: per-update traces of body position / velocity (harness WATCH), as before.

## Integration into the port (started 2026-10-13, user: "continue until the physics engine is integrated")
Stages and state:
1. Library queries: done - the three cast tables and world casts ("Casts and queries"). The
   broadphase is the port's own: body boxes cached while a fixed / keyframed body's pose is
   unchanged; pairs searched only for bodies that are neither fixed nor keyframed (the motion
   table never pairs those two); queries test every body's cached box (no trees).
2. Module layer `physics::PhysicsWorld`: bodies from .pfb placements (0xbd91a0 scaling), baked
   level meshes, keyframed placements moved with `movePlacement` (body = placement pose x
   record frame; `delta` = the body's change of transform by the last move), the three module
   queries in game units with the module's result rules, `step`, run-time (kind-6) meshes built
   at load ("Run-time meshes" below), bodies at the record's body frame ("Body frames" below).
   Open: contact lists. Placed bodies carry the game's filters from their hit flags.
3. Level: done - LevelScene builds the PhysicsWorld with the triangle overlay, moves the
   bodies of placements under moving joints every update and steps it.
4. Enemies: done - MupRootDynamics on library bodies (game/enemy/BasicEnemy.cpp "Root dynamics",
   rules in "MupRootDynamics (ported)" below); the slide, ground ray and pair-separation stand-ins
   are gone. enemy5: Standby / Launch / Cruising equal to the original (Cruising 0.0000 between
   segment restarts, the launch until its ledge contact, whose first impulse is within 1 %);
   the Active phase where the two enemies push each other averages 12-17 cm (comparable to the
   old separation stand-in; closest approach 0.13 vs 0.11 m). combat5 / combat7: the thrown flight
   (torso without spin, then the dead body) within 3 cm until the tumble, mean 0.10 m after.
5. Hero: done - queries on the PhysicsWorld: wall capsule (0x66dd40) on the capsule query
   (radius CollisionForWallRadius, half CollisionForWallHeight / 2, quaternion (0, 0, sin 45,
   cos 45)), ground probe and drop-edge casts on the sphere query, slope / teeter / edge rays on
   the ray query; ground normal = the module's (facet) normal; moving support = a probe hit on
   a body of motion type 1 or 2. The stand-in ceiling cast while rising was removed: the
   original always runs the ground probe (0x66ab90: one sphere query per update, rising or not); run move2 rises beside a moss
   lip the foot body touches. Replays (herocmp.py): move1 1,113 updates to 6115, move2 1,619 to
   5920, move3 532 to 8183, as before (each ends at a run-time enemy or the known corner).
   The game no longer queries CollisionWorld (the collision overlay and tools keep it).
Open (integration): ceilings while rising - found in the slide 0x66dd40 and ported (hero.md),
closed 2026-10-14: checked against run ceil4 (same contact update, v.y 0, within 3 mm); the hit flags - closed 2026-10-14: art_bg 0x5ecda0 builds the
object's target mask (+0x1ca8): all bits with HitOther, else none; then the bits of the hero
manager's first hero's body (0x3df080: the robot, 1) on / off by HitVrHero and the other heroes'
(0x3df120: 0xf without them) by HitTvHero; self (+0x1ca4) = 0x10 (0x5eb211); applied to all of
the object's bodies by IfcPhysics 0xbd40f0 / 0xbd4200 (virtuals +0x208 / +0x210; 0x5eced0 switches
collision off by clearing the three flags). The port gives placed bodies these filters
(LevelCollision) and maps its surface masks the same way (the robot = HitVrHero; the port had
read HitTvHero for it: in forest_castle the two never differ; where they do, tv-only objects are
far scenery and vr-only ones walkable platforms). Effects: enemies' probes and bodies and the
punch ignore objects without HitOther (punchtest 22 of 22, was 21);
the enemy items listed under "MupRootDynamics (ported)"; the
run-time meshes' items under "Run-time meshes"; radius-0 sphere casts against hulls (the
original's normal there is rounding residue, the port's undefined: the module then drops the
hit; the game casts rays through the ray table instead, physcompare does the same now).

## Reference source (allowed by the user, 2026-10-13)
Bullet 2.80's Extras/PhysicsEffects (Sony Computer Entertainment, BSD 3-clause, 2012), fetched
to `.tools/reference/pfx-src` (not shipped). It has the low-level pipeline (pfxFindPairs,
pfxDetectCollision, pfxRefreshContacts, pfxGenerateIsland, pfxSetupSolverBodies,
pfxSetupContactConstraints, pfxSolveConstraints, pfxUpdateRigidStates; base-level shape and
contact code), but no world-level layer: the game's version adds one (world object, task jobs).
It may be older than the game's version; the executable is authoritative. If ported code follows
its structure closely, keep its copyright notice with that code.

## Map (2026-10-13)
- Library region: 0xd909f0-0xe8053e (1,114 functions, ~0.97 MB; functions returning the PFX
  error codes 0x8088000x lie there), world layer 0xcc0000-0xd90000 (e.g. world 0xcf*,
  job sequence 0xdd0*, de06a0 "ScePhysicsEffects" / "ScePfxJobWorker" threads, task vtable
  0x16f1ba0), engine module 0xc9cc70-0xcbe400 (impl functions 0xcb0000-0xcbd000).
- Shape kinds in .pfb = PFX shape types: 0 sphere, 1 box, 2 capsule, 3 cylinder, 4 convex mesh
  (the reference's 5 large mesh; the files use 6 / 7 for meshes: newer format). Motion types:
  0 fixed, 1 active, 2 keyframe (3 one-way, 4 trigger in the reference).
- Convex mesh creation 0xde67e0 (module limit: 192 vertices, 64 faces, 0xcb5880).
- World creation: module 0xcb04e0 fills the world parameters (defaults incl. 1/60 s, 0.8,
  0.1, 180, 500, 3, constants at 0x13042f0..0x1304328) and builds the library world (0x2810
  bytes) with 0xcf3960 (+ 0xcf3710 sizing); facade 0xca7e80.
- **Step**: facade 0xca8600(dt, world) -> module 0xcb0900 (clears the contact cache, world+0x2760
  = dt) -> world step 0xcf66d0:
  1. 0xdd04d0 begin job sequence (+0x360)
  2. 0xcf5be0 parallel task over the bodies (count +0x1a0, task vtable 0x16f0e08), 0xcf5cf0,
     0xcf6390
  3. flag +0x2780 & 2: 0xcf6a10
  4. 0xdd60d0(params: dt +0x2760, +0x2764, constants 1.2 / 1.6, body / pair arrays): pipeline
     0xdd2bb0 (or 0xdda5b0 when +0x11c and flag 4): callees 0xd99710/0xd99730, 0xddcc80,
     0xddce80, 0xdd6190, 0xddd0b0, 0xddd7e0, 0xde0270, 0xe78120, 0xe68f10.., 0xe792a0,
     0xe770e0 (to be matched with the reference stages)
     Pipeline 0xdd2bb0 in call order (stage names: hypotheses by the reference's order, to be
     confirmed function by function; decompiles in `evidence/decomp/pfxworld`, `pfxpipe`):
     a. 0xd99710 / 0xd99730 per body: broadphase proxies (pfxUpdateBroadphaseProxy?)
     b. 0xddcc80 / 0xddce80 / 0xdd6190: sort proxies (pfxParallelSort?)
     c. 0xddd0b0: find overlapping pairs (pfxFindPairs?); 0xddd7e0: new / kept / removed pairs
        (pfxDecomposePairs?); 0xde0270 x6: merge pair arrays
     d. 0xe78120 (1,015 lines): contact detection per pair through shape-pair function tables
        (pfxDetectCollision?)
     e. 0xe68f10 / 0xe6a570 / 0xe6add0: solver bodies and contact constraints set-up
     f. tables 0xe6b080 / 0xe6b0c0 / 0xe6b100 and 0xe69730 / 0xe6aa90 / 0xe6af50: solver
        iterations (contacts, joints)
     g. 0xd99cb0 (414 lines), 0xd99c40: state update / integration (pfxUpdateRigidStates?)
     h. 0xe6b140 / 0xe69e00: a second constraint pass (newer than the reference?)
     i. 0xe792a0 / 0xe770e0: CORRECTION - not islands: pose records of attached bodies (0x290-
        byte records) and propagation of a root body's pose to bodies attached to it
  5. flag & 1: 0xde0390 = sleep pass; flag & 2: per-body lists of the contact reports (+0x27f8)
  6. 0xdd04f0 end
- Contacts of a body for the game: module 0xcb1420 (via 0xcad2a0; used by MupRootDynamics
  0x7e0770), pairs keyed by body ids at contact +0x184 / +0x186.

## Body parameters (.pfb, 2026-10-13)
Body record fields copied by the importer 0xc9cc70 into the body description: +0x24c mass,
+0x254 / +0x258 (0.2 / 0.6: restitution / friction by the reference's members), +0x264 / +0x268
(900 / 60: max linear / angular velocity), +0x26c / +0x274 (1.0 / 0.99: linear / angular
damping; the reference's defaults are 1.0 / 0.99). Pairing to be confirmed in 0xcb5880.
The basic enemy's bodies carry max angular velocity 60: the thrown enemy's exact 60 rad/s spin
(run combat5) is this limit, applied when forces are applied (reference pfxApplyExternalForce:
damping, then length clamps). The spin axis remains open.

## Ported so far (src/physics/rigid)
- ConvexSolver, ContactCache, MeshContacts (all primitives), PrimitiveContacts, SweptContacts:
  see the sections below; verified by executing the original routines (pfxrun).
- RigidBody.{hpp,cpp}: state / body records, applyExternalForce (0xd9a050), integrate
  (0xd99cb0), the library's sine / cosine (max error 1.1e-7 against double precision).
  - 0xd9a050 (game version): skips motion types fixed / keyframe / trigger and sleeping bodies;
    v += F / m dt; w = Iw^-1 (Iw w + T dt), Iw = R I R^T (Euler, no gyroscopic term; the
    published version: Runge-Kutta, gyroscopic term, damping here); then |v| <= state +0x20,
    |w| <= state +0x24 (scaled back).
  - 0xd99cb0: skips fixed / trigger and sleeping; p += v dt; |w| > 1e-6: q = d q with d the
    rotation by |w| dt about w (sign-flipped when its axis opposes w), else q += (w,0) q dt / 2;
    normalise.
  - Damping (state +0x18 / +0x1c): applied after the solver iterations, see "World step".
- World pass 1 (0xcf5be0 -> parallel task 0xcf9280 -> 0xcf9820): for each active body (bit set
  in world +0x1b0): save v / w (state +0x60 / +0x70 -> world +0x1d8 array, 0x30 stride), then
  applyExternalForce(dt = world +0x2760, force = world gravity (+0x180, scaled) x mass).
- Library units: module position / velocity setters multiply by the world scale, getters
  divide (reciprocal estimate + one refinement); orientations unscaled.

## Contact dispatch (2026-10-13)
Table 0x171fb50: [shape type A][shape type B] -> contact function (rows 0x88 bytes, 17 slots;
0xe0db90 = no contact). Types 0 sphere, 1 box, 2 capsule, 3 cylinder, 4 convex, 5-7 meshes:
  row 0 sphere:   e0f080 e0eb70 e0ee00 e101c0 e11ab0 | 5: e12380 6: e13930 7: e127c0
  row 1 box:      e0e0f0 e0dba0 e0de50 e104b0 e11da0 | e12380 e14590 e127c0
  row 2 capsule:  e0e8d0 e0e390 e0e630 e107b0 e12090 | e12380 e151f0 e127c0
  row 3 cylinder: e0f310 e0f600 e0f900 e0fbf0 e0fed0 | e12380 e16590 e127c0
  row 4 convex:   e10db0 e110b0 e113c0 e10aa0 e116d0 | e12380 e16c60 e127c0
  row 5:          e12380 x5 | -      e17320 e12b30
  row 6:          e13300 e13f60 e14bd0 e15830 e15ed0 | e17320 - e177f0
  row 7:          e127c0 x5 | e12d70 e177f0 e12fa0
Type 6 has one function per primitive like the reference's triangle-mesh contacts (sphere,
box, capsule, cylinder, convex); 5 and 7 one for all primitives (dispatchers: open). Which
pfb kinds (6, 7) map to which types: in the importer (open).
Joint constraint tables (by joint type, 9 entries): set-up 0x1720610, warm start 0x1720690,
solve 0x1720710, finish 0x1720790.
Enemy bodies: dead body = sphere r 0.0724 + three cylinders; torso cylinder (r 0.0799, half
height 0.0317), head sphere.

## Contact manifold (game version, ported: src/physics/rigid/ContactManifold)
Manifold: +0x08 duration (u16), +0x0a point count, points of 0xa0 bytes from +0x3c: +0x3c
duration (u8), +0x40..+0x5c sub data (32 bytes), +0x60 distance, +0x64 local A, +0x70 local B,
rows from +0x7c (0x20 each: normal, rhs, jacDiagInv, lower, upper, accumulated impulse +0x1c).
- add 0xe0cee0: same point when both local points are within 1e-4 (squared; the published
  version: A only, 0.01); a same point keeps its solver state, its normal averaged with the new
  one when dot^2 < 0.9 (distance projected); else append, or sort4 and reset the slot (distance
  1e38, impulses 0).
- sort4 0xe0c900 (and the cache's 0xe0bb50), game version: keep the deepest (the new point's
  distance is the starting threshold); for each other candidate the "area" of the four points
  left (the new one with three old ones) is the largest |d1 x d2|^2 over the three ways to pair
  them into two diagonals (the published version: one fixed pairing); the largest wins, ties to
  the lower index. CORRECTION 2026-10-13: first ported with the published pairing; now
  pointToReplace(), verified against both routines on 4,000 random cases (0 differences).
- refresh 0xe0d6e0: every point (the published version skips new ones): separation along the
  normal > 0.01 -> removed (last point moved in); else distance stored; sideways squared > 0.002
  -> removed; else duration + 1 (<= 255). Manifold duration + 1, or 0 when empty.
- merge variant 0xe0d250 (cache -> manifold; 1e-4 / 0.99), other caches 0xe0d9d0 (0x7c-byte
  entries) and 0xe0c630 (0x60-byte entries, flag 0x10): not ported.

## Contact solver (game version, ported: src/physics/rigid/ContactSolver)
- Per pair (0xdd2bb0): restitution = (eA + eB) x 0.5 (body +0x34), only while the manifold
  duration < 2; friction = sqrt(fA fB) (+0x38); rolling friction (+0x3c) x 0.5 summed, see
  below; accumulated impulses scaled by the warm-start factor (step parameters +0x5c = 1.0)
  before set-up; a per-body priority byte scales one side's mass when the two differ (see
  "Solver order and priorities").
- The per-pair data is the manifold record itself (persistent): +0x08 duration, +0x0a point
  count, +0x0c count of a second list (0x7c-byte entries, bodies with flag 0x20: not ported),
  +0x10 rolling limit, +0x14 friction, +0x18 restitution, +0x1c rolling row (direction, rhs,
  jacDiagInv, lower, upper, accumulated impulse), points from +0x3c.
- Rolling friction (found 2026-10-13, ported: RollingRow): after a pair's point set-ups,
  0xe6add0 sets up one angular row against the bodies' relative spin w = wA - wB (state +0x70):
  when |w|^2 > 1e-8, direction d = w / |w|, k = d.(IA^-1 + IB^-1) d from the solver copies
  (priority scaling included, no one-way rule), jacDiagInv = 1 / k (0 when |k| <= 1e-8), rhs =
  -(d.w) jacDiagInv, limits -+ the pair's rolling limit; else d = 0 and the accumulated impulse 0
  (the rest left as it was). Warm start inline in 0xdd2bb0 after the points' (dwA += IA^-1 d
  acc, dwB -= IB^-1 d acc); 0xe6af50 after the pair's points in every iteration (the usual
  clamped row on the delta angular velocities). Both skip unless all three components of d are
  non-zero (exact compares: a spin exactly about a coordinate axis gets no rolling friction).
  Body +0x3c: 0 in every body record (0xcb5880 and the other creators fill the 0x50-byte record
  with 0.2 / 0.6 / 0); set only through 0xca6350 (body wrapper +0x40 -> +0x3c) by the physical
  block (0x778290: 0.03, 0x1244a10) and the soccer enemy's ball (0x8a5c70: its tuning +0x2ac4).
- Set-up 0xe68f10: as the published pfxSetupContactConstraint with: +0.001 on every effective
  mass; restitution only when v.n < -0.5; position bias x |d| / (|d| + 0.05) with slop 0.001;
  all limits [0, 1e38]. Solve 0xe69730: normal row, then friction rows within
  +-friction |normal impulse| (as published). Joint tables 0x1720610.. (not ported).
- Parameters (world creation 0xcb04e0 -> world 0xcf3960): +0x80 dt 1/60 -> world +0x2760;
  +0x84 0.8 -> +0x2764 (step parameters +0x60: separation bias, hypothesis); +0x88 (3, 3, 3, 2)
  -> +0x2768.. (iteration counts, hypothesis); bounds +-250; gravity (0, -9.8, 0); +0xa0 180,
  +0xa4 0.1, +0xb0 3; step constants 1.2, 1.6 (step parameters +0x64 / +0x68): open.

## Who the solver moves (2026-10-13)
- Enemy set-up 0x2daf40: for kinds 0x7d2 / 0x7d3 the DamageJoint body (enemy_head_col; earlier
  noted as RootCollision - the name read is Bind/DamageJoint, tuning +0xcd8) is
  made keyframe (0xca5c70(2), filter 0x1000, target +0x3ac0) and stored at +0x3b48; for other
  kinds bodies become triggers (4, filter 0x20000, target 0xa0f). Hero bodies: trigger (0x667200,
  0x995d00). Keyframe / fixed bodies are static to the library: no contacts with each other, so
  living enemies are not moved by the solver; their motion, walls and pushing come from
  MupRootDynamics' own queries and the contact list it reads (0xcad2a0).
- The solver moves active bodies only: thrown enemies (DeadCollision switched to active,
  0x2e2750), physical objects; keyframe bodies push them.
- Priority from this: MupRootDynamics (enemy feel) and the queries it uses; then the world step
  with contacts for thrown bodies and physical objects.
- CORRECTION (run rootdyn1): the body named by Bind/RootCollision (enemy_torso_col, a cylinder)
  is the root dynamics body (+0x2810 = rootdyn +0x2a0) and is ACTIVE (state +1 = 1). The keyframe
  one is the body named by DamageJoint (+0x3b48). While walking the body's position = root
  centre (+0x1a0) x 3 and velocity = root velocity x 3 (library units), and its orientation
  tumbles. So enemies are moved by the library: cylinder vs level mesh contacts and cylinder vs
  cylinder (enemy separation) are on the critical path after all.

## Position correction (game version, found 2026-10-13)
- Velocity set-up runs with separation bias 0 (0xdd2bb0 clears XMM3 before 0xe68f10): no
  position term in the velocity rows.
- After integration a position pass (0xe69e00, per contact point, step parameters +0x60 = world
  +0x2764 = 0.8): d = n.(pA - pB) from the integrated poses; impulse p = -0.8 |d| / (|d| + 0.05)
  min(0, d + 0.001) / (n.K.n); positions += +-n p / m, orientations += 1/2 (I^-1 (r x n) p, 0) q,
  normalised. Iterations: hypothesis the fourth count (2) of world +0x2768.
- Solver iterations: max of step parameters +0x48 / +0x4c / +0x50 (world +0x2768.. = 3, 3, 3);
  each constraint kind gated by its own count.

## Convex solver (ported: src/physics/rigid/ConvexSolver, 2026-10-13)
Not the published GJK/EPA: a Minkowski portal refinement (XenoCollide-style) on A - B with B in
A's frame. Context (set-up 0xe1ebb0): shape A, shape B, support A, support B, centre A, centre B.
- Portal discovery 0xe1ebd0 (returns 0 = found): v0 = centre difference; n = -v0; v1 = support;
  v1.n < 0 -> miss; n = v1 x v0 (|.|^2 < 1e-16: axis x v0 for the first axis with
  1 - |n.axis| > 1e-8); v2; v2.n < 0 -> miss; n = (v1-v0) x (v2-v0); v3; v3.n < 0 -> miss; then
  up to 33 rounds: n1 = (v1-v0) x (v3-v0) with the origin in front -> swap 1,3, new v2 along n1;
  else n2 = (v2-v0) x (v1-v0) -> swap 1,2, new v3; else n3 = (v3-v0) x (v2-v0) -> swap 2,3,
  new v1; else found. No miss test on these replacements.
- Refinement (inlined): n = unit((v2-v1) x (v3-v1)) (not finite -> fail); w = support(n);
  n.(-w) > 0 -> separated; n.(w - v1) < 1e-4 or 33 rounds -> converged; else with
  c1 = ((w-v0) x (v2-v0)).(-v0), c2 = ((w-v0) x (v3-v0)).(-v0), c3 = ((w-v0) x (v1-v0)).(-v0):
  c1 > 0 and c2 <= 0 -> v1 = w; c3 > 0 and c1 <= 0 -> v3 = w; c3 <= 0 and c2 > 0 -> v2 = w;
  else unchanged.
- Converged: c = point of the portal triangle closest to the origin (Voronoi regions).
- Two entry points: 0xe20d50 (primitive pairs) runs up to four searches: the first from the
  centres; then from below the first portal's side faces (v0,v1,v2), (v0,v2,v3), (v0,v3,v1):
  v0' = -max(0.01, v0.n) n (n the face normal, or the x axis when |n|^2 <= 1e-6); stops when
  |c|^2 < 0.0100000007; a later search that fails discovery ends the series, one that fails
  refinement is skipped; the smallest |c|^2 wins (ties: earlier). 0xe1f9a0 (triangle
  contacts): one search. Centres within 1e-8 (squared) -> no result.
- Contact from the portal: flat portal (|e1 x e2| < 1e-8) -> distance 0, normal x, points of
  corner 1; corners on a line (|1 - cos| < 0.01 for an edge pair) -> interpolate along the two
  outer corners (normal from the segment to v0 when |c| < 0.001); else barycentric (determinant^2
  < 1e-16 -> no result); normal -c, or (v3-v1) x (v2-v1) when |c| < 0.001 (0xe20d50) / 1e-5
  (0xe1f9a0); |normal| < 1e-6 -> x axis and distance 0; else distance -|c|. Normal from B to A,
  point A in A's frame, point B in B's frame.
- Supports: cylinder 0xe22a80 (x axis; a cap by the sign of dir.x, 0 -> negative; side point
  r (0, dy, dz)/sqrt(dy^2+dz^2) unless that is < 1e-6 squared), sphere 0xe22a60, box 0xe22970,
  capsule 0xe229f0, hull 0xe22750 (first farthest point), four points 0xe224c0; centres zero
  except hulls / four points (average: 0xe22e50, 0xe22f20). All add margin x dir (callers pass 0).
- Cylinder pairs (0xe0f310 sphere, 0xe0f600 box, 0xe0f900 capsule, 0xe0fbf0 cylinder, 0xe0fed0
  hull): full search with the cylinder as A; result used when distance < the caller's threshold;
  normal rotated to world and normalised, distance recomputed as n.(pA - pB) in world space,
  points moved into the bodies' frames by the shapes' offsets.
- Verification (2026-10-13): .analysis/tools/pfxrun.c maps the executable's image into an
  x86_64 process (Rosetta runs the AVX code) and calls the routines; pfxprobe.py compares.
  Cylinder pairs: 96% equal (largest difference 0.0016 in matched cases). Every mismatch traced
  so far is a support tie decided by rounding: a cylinder queried perpendicular to its axis picks
  a cap by the sign of a component of order 1e-8 (the console's reciprocal-square-root
  estimate decides; not reproducible), after which the search converges elsewhere on the same
  surface (sphere-cylinder side contacts: 43% differ, distances to ~1e-5, normals by ~0.01).

## Mesh contacts (ported: src/physics/rigid/MeshContacts, all five primitives, 2026-10-13)
- Meshes in this game are layout 2 (PELM byte 6 = 2 -> runtime flags 2): traversal 0xe8bc80
  (flags bit 1) with per-primitive island routines from table 0x1329fac (layout 7: 0x1329fc0,
  bit 0 set: 0x1329f98): sphere 0xea6940, box 0xe91dd0, capsule 0xe99ad0, cylinder 0xea1990,
  hull 0xe9d800. CORRECTION: the earlier list (0xe9ffe0 etc., traversal 0xe8b020) is for meshes
  without bit 1 and unused here.
- PELM preamble = the mesh tree: u32 length, u32 count, nodes of 11 bytes {u8 min steps[3],
  u8 max steps[3], u8 child kinds, u16 left, u16 right}. Island tree nodes are 9 bytes with the
  same order and u8 children (the runtime reorders both). Child kinds: 2 bits each (left 0-1,
  right 2-3): 0 inner, 1 leaf, else none. Child box = parent box shrunk by steps x
  (max - min) x 1/255 (the code uses the raw reciprocal estimate of 255).
- Walk (both trees): pop; box apart from the primitive's box -> drop; a left inner child takes
  the popped slot, a right inner child is pushed above it (so right subtrees first); leaves are
  reported left then right as the node is reached. Mesh tree: each leaf island is tested against
  the box and handled at once. Island tree: leaves collected, then handled in order. Stack 100.
- Primitive box in the mesh frame: centre = relative translation, half = |c0| hl + |c1| r +
  |c2| r for a cylinder (sphere r sum|ci|, box sum h_i|ci|, capsule (hl+r)|c0| + r|c1| + r|c2|,
  hull: its half extents). Relative transform = mesh transform^-1 x primitive transform (the
  mesh transform may scale; mirrored when the product of the shape's scale factors is < 0:
  corners 0 and 2 swap, edges 0 and 1 swap).
- Island (0xea1990): facets in the cylinder's frame (vertices by the inverse transform, normals
  by its inverse-transpose, normalised); per facet 0xe9f940 into an island cache; then each
  point: sub data A = facet << 8 | 2, B = user << 32 | t 16 bits | s 16 bits (barycentric
  weights of corners 1, 2, clamped to [0, 1] x 65535 - computed from the point still in the
  cylinder's frame against mesh-frame corners: wrong unless the transform is the identity; only
  bookkeeping), normal to the mesh frame (inverse-transpose), point A to the mesh frame.
- Facet 0xe9f940: skip when n.v0 > n.support(n) + thickness (facet +0xc); apex = centroid -
  n x mean edge length; tetrahedron (v0, v1, v2, apex) around its centre against the cylinder
  (0xe1f9a0); skip unless distance < 0 and normal.n >= 0; contact normal = -solver normal
  (from the cylinder towards the mesh) unless the point (barycentric s = w1, t = w2) lies on an
  edge (weight < 1e-4) or corner (> 0.9999) whose edges are all flat (angle type 0): then -n;
  inside, only when s + t > 0.9999 (double) and edge 1 is flat. Cache: A = mesh (point on the
  facet), B = cylinder.
- Back in 0xe8bc80 per island point: normal to world (inverse-transpose, normalised), distance
  recomputed as n.(pA_w - pB_w), island index into sub data A bits 16-31, into the result cache.
- Caches (0xe0c0e0, ported: ContactCache): four points; same point = both ends within 1e-4
  squared (closest wins); normals averaged when dot^2 < 0.9 (distance x dot(new, average));
  else append or sort4 (0xe0bb50).
- Verification: pfxrun deserializes real PELM blobs with the game's own loader (0xdf3cc0,
  allocation hook 0x171fb38) and runs 0xe8bc80; meshprobe.py places cylinders against random
  facets of 40 game meshes. Same points (count and sub-shape data: facet, island, barycentric) in
  81% of cases, median value difference 6e-6; per facet (0xe9f940 alone, 4,000 random facets):
  1,657 identical, 196 differing in value only (normals ~0.01-0.04), 1 in structure; the cases
  traced are cap ties as above. Scaled and mirrored mesh transforms: same picture.

## Primitive pairs (ported: src/physics/rigid/PrimitiveContacts, 2026-10-13)
- Contact table by implementation: every pair with a cylinder or a hull runs the full convex
  search (0xe20d50); six pairs have dedicated math, each verified against the original routine
  with pfxrun `pair` / pairprobe.py (random poses incl. 15% axis-aligned):
  - sphere-sphere 0xe8cbe0 (3,000 cases, identical): normal along the centre difference, z axis
    when the squared length <= 1e-30;
  - box-sphere 0xe870d0 (3,000, <= 1.8e-7): face = axis of the largest gap |p| - h - r, other
    two coordinates clamped; normal from the clamped point, the face normal when the centre is
    inside along the face axis or within 1e-30; distance +-|v| - r (as the published version);
  - capsule-sphere 0xe878c0, capsule-capsule 0xe875a0 (3,000 each, <= 1.8e-7): clamped segment
    parameters (capsule-capsule: s, then t, then s again, the published order), normal x axis
    when the squared length <= 1e-6;
  - box-capsule 0xe865f0 (3,000, identical): the published separating-axis / closest-feature
    routine;
  - box-box 0xe84080 (15,000, <= 2e-6): the published routine, including its edge-test value
    (tA subtracted after the sign multiply, so not a squared distance when the sign is
    negative) for choosing features; but when apart the result is the distance between the two
    returned points (overlapping: the largest gap). Using the corrected edge value changes the
    chosen features in ~1 of 1,500 parallel cases, so the game keeps the published one.
- Wrappers: the math returns the normal from its first shape to the second; when that first
  shape is the pair's A the wrapper negates it (contact normals point from B to A), otherwise it
  swaps the points. Shapes carry a scale at +0x14 (dimensions multiplied, transforms divided);
  the world bakes scale into dimensions. Thresholds passed to the math are 1e38.
- Meshes: sphere 0xea6940 + facet 0xea44f0 (ported: per facet 1,893 identical of 1,893;
  through the mesh traversal 513 of 522 identical, the rest barycentric sub-data off by one unit
  or equal-depth ordering), hull 0xe9d800 + facet 0xe9c130 (ported: 354 of 404 identical, the
  rest sub-data rounding or polytope ties; the hull island works in the mesh frame without the
  mirrored corner order, the facet test runs the convex search with the tetrahedron as A and the
  hull at its pose; hull box half extents = largest |coordinate| per axis, 0xde67e0).
  Sphere facet: rejected when the centre is above the plane by >= r or below by >= thickness + r
  or beyond an edge plane by >= r (edge planes tilted down by tilt x pi/2 / 255 via sin / cos when
  any marked edge has a tilt); contact towards the closest point (-n inside the face or when
  within 1e-5 squared), the flat-edge rule as for cylinders; distance -|r dir - c|.
- Box 0xe91dd0 + facet 0xe8dcd0 and capsule 0xe99ad0 + facet 0xe94940 (ported 2026-10-13):
  the islands are the cylinder / sphere island (facets in the primitive's frame, mirrored corner
  order, same sub-shape data); the edge flags take edge byte 2 unmasked (the port masks with 3,
  as for the other islands: hypothesis that byte 2 never exceeds 3); tilts = byte 3 x
  0.0061599859 (pi/2/255 as one constant, not the sphere island's /255 x pi/2), passed as one
  vector. Both facet routines are the game's version of the published triangle-box /
  triangle-capsule separating-axis code:
  - the facet as a solid: top = the triangle; side planes = unit((v_i+1 - v_i) x n), tilted to
    m cos t - n sin t when any marked edge has a tilt (the library's inline sine / cosine:
    quadrant by x 2/pi + copysign(0.5), reduction by two parts of pi/2, odd / even polynomials,
    constants 0x132a1f0..0x132a2b0; sin 0 = 0 exactly); with tilts the bottom is where the three
    side planes meet, cut at the thickness (corners moved towards that point by thickness /
    depth) or that point when shallower; without tilts the triangle moved down by the thickness;
  - axes, deepest wins (strictly greater; start -1e38): the facet normal first (box: from v0,
    radius (|n.x hx| + |n.y hy|) + |n.z hz|; capsule: from the triangle's centre, ends at
    (+hl, 0, 0) then (-hl, 0, 0)); rejected when above by more than 0 or below by more than the
    thickness; the other axes only when the primitive reaches above the plane or the thickness
    is <= 0.05. Box: x, y, z, then each edge x each box axis when |e.a| <= 0.9999. Capsule: x x
    each unit edge when |e.x| <= 0.9999; ((end - v_i) x e_i) x e_i for each end; ((v_i - A) x
    axis) x axis; v_i - end for each end (these four kinds only when the squared length is >=
    1e-5). Each axis gives the two overlaps (facet range minimum minus primitive maximum, and
    primitive minimum minus facet maximum, capsule ranges widened by the radius); the larger
    beyond the best becomes the axis (pointing from the facet to the primitive) and records
    whether it faces against the facet's back (axis . -n < 0, or > -0 for the reversed one);
    any positive overlap = separated, no contact;
  - thickness > 0.05 also: each side plane (box: m.(-v_i) - sum |m_k| h_k > 0; capsule: the
    nearer end beyond the plane by more than the radius), the capsule also the corner normals
    unit(m_1 + m_0) at v1, unit(m_1 + m_2) at v2, unit(m_2 + m_0) at v0; then each edge
    unit(m_k+1 x m_k) of the side planes against the box axes / the capsule axis (top corners
    only) as separation-only tests;
  - no contact when the winning axis faces the facet's back;
  - closest features after moving the triangle by |depth| x 1.1 along the axis: box with the
    face most along the axis (first largest of +x +y +z -x -y -z): facet normal -> the face's
    four corners against the triangle (Voronoi regions); box axis -> the triangle's corners
    clamped into the box; edge axis -> each triangle edge against each face edge (segment
    pair: line parameter unless the squared denominator <= 1e-5, clamped, then the other, then
    back). Capsule, whatever the axis: its axis against each triangle edge, then each end
    against the triangle; the capsule point on its surface towards the facet point; kept by
    squared distance between the segment / end and the facet point;
  - up to four pairs kept: taken when no farther than the last taken + 1e-5, dropped when an
    existing pair has both points within 1e-5 squared and is nearer, else appended or replacing
    the farthest; output for pairs within the last taken + 1e-5 (so 1e-5..2e-5 of it): facet
    point moved back, normal = the axis except where the point is on an edge / corner whose
    edges are all flat (the cylinder rule; once switched to -n it stays for later points),
    distance = -|primitive point - facet point| (-0 below 1.2e-38 squared). The second
    sub-shape word passed with the points is a stale register half (the island overwrites it).
  - Unit vectors: the library's 1/sqrt estimate + two Newton steps; the port starts from the
    exact value (libraryUnit), which settles more axis ties the original's way than 1/sqrt.
  - Verification (pfxrun boxfacet / capfacet, facetprobe.py box|cap, 40% of facets face-on to
    the primitive's axes): capsule 6,645 of 6,646 contact cases identical (one value), 6,400 of
    6,403 without exact ties; box 5,871 of 5,871 without exact ties; with exact axis-aligned
    facets 6,083 identical, 9 point order, 72 differing where two axes give the same depth to
    the last bit (normalisation rounding decides; hardware-dependent in the original too).
    Through the mesh traversal (meshprobe.py ... box|capsule, 600 placements each): box 589
    identical, 4 point order / weight rounding, 7 other; capsule 577 / 14 / 9; scaled and
    mirrored meshes box 565 / 21 / 14, capsule 563 / 20 / 17 (sphere for comparison 596 / 2 / 2).
    The "other" cases traced are equal points from neighbouring facets at a shared corner or
    edge, where the 4-point cache keeps a different one of equal candidates.
- World (RigidWorld): boxes, capsules and hulls against meshes now run these routines (hulls
  were ported earlier but not wired). Meshes against meshes: not handled (no entry in the
  library's tables). The data has movable bodies with boxes (323 shapes), capsules (267), hulls
  (182), spheres (44), cylinders (22) and meshes (109).

## World step (assembled: src/physics/rigid/RigidWorld, 2026-10-13)
- Per pair detection (0xdbe190 / 0xdbfcc0): for each shape pair (bounds pre-test), the contact
  table function fills a 4-point cache (points moved into the bodies' frames by the shape
  offsets); then every cached point goes into the pair's manifold with 0xe0cee0 (ported add);
  then 0xe0db70 and the swept list (0xe0d9d0, 0x7c-byte entries; see "Swept contacts").
  Manifold refresh 0xe0d6e0 runs earlier, from 0xdb4b80 (called by the world step at
  0xcf6307). 0xe0d250 (merge with 1e-4 / 0.99: keeps the solver state when normals agree)
  is only used by 0xe0d660, not by detection.
- Solver order and priorities (0xdd2bb0, found 2026-10-13, ported in RigidWorld::solve):
  - 0xddcc80 builds three constraint lists from the pair array (16-byte records: byte 0/1 motion
    types, byte 2 > 15, byte 3 = points, byte 8 = iteration class, +0xc/+0xe body ids): class 2
    pairs go to lists 0, 1, 2, class 1 to lists 1, 2, class 0 to list 2; entries {A, B, pair}.
    Iterations = max of the step counts; each iteration visits the list of its phase (a list
    index that advances when the remaining count reaches the middle and the smallest count), so
    with the game's (3, 3, 3) all pairs are solved in every iteration.
  - Levels (0xddd0b0 -> 0xddd460): per body 1 if movable (active, one-way: 10 >> type & 1) else
    0; then a breadth-first walk from the non-moving bodies over the contact list (neighbours in
    list order): level 0 for them, +1 per contact step, unreached 255.
  - 0xde0270: each list counting-sorted (stable) by level[A] + level[B].
  - Per pair, when the levels differ, the body with the lower level (nearer the ground) gets its
    solver copy's inverse mass and inverse inertia rows scaled by 1 / max(step +0x64, 1) =
    1 / 1.6 - in set-up, iterations and the position pass.
  - Also per pair: restitution (eA+eB)/2 (only while duration < 2), friction sqrt(fA fB),
    (cA + cB)/2 of body +0x3c stored at constraint +0x10 (the rolling-friction limit).
  - Step parameters (0xcf66d0): +0x48 counts (world +0x2768: 3, 3, 3, 2), +0x58 dt, +0x5c 1.0
    (warm start), +0x60 world +0x2764 (0.8), +0x64 1.6 (priority), +0x68 1.2 (use open),
    +0x6c 0x10000. A default block before (cf6a10 call): counts (5, 5, 5, 2), dt 0.016.
  - Position pass: 0xe69e00 per point, +0x54 (= 2) iterations.
- RigidWorld (hypotheses marked): gravity -> pairs by box overlap (lower body id = A; pairs in
  id order: hypothesis) -> refresh -> detection -> set-up + warm start -> 3 iterations ->
  velocities -> integrate -> 2 position passes; solver order and priority scaling as found. Contacts ported: cylinder against every
  primitive and against meshes; other pairs skipped for now.
- Velocities after the iterations (0xdd2bb0, every body): v = state +0x18 x (v + dv),
  w = state +0x1c x (w + dw): the linear / angular damping (pfb 1.0 / 0.99). Ported.
- Solver pairs (0xddcc80): only motion-type combinations of mask 0xaf2b80 at bit
  24 - typeA - 5 typeB (fixed-active, fixed-one-way, active with fixed / active / keyframe /
  one-way, keyframe with active / one-way, one-way with fixed / active / keyframe), not two
  sleeping bodies, not a sleeping body against a fixed one. Ported. Solver bodies get mass by
  motion type alone (the sleep bit is carried along).
- Islands (0xcf6390 before the pipeline: 0xdc9140 reset, 0xe75750 per pair array): every body
  of a pair (pair byte 2 > 15) is entered; the two are joined when both are active, keyframe or
  one-way (mask 0xe). Bodies without pairs are in no island.
- Sleep (0xde0390, world flags bit 0; the module creates the world with flags 3, count 180,
  velocity 0.1 in library units): per active / keyframe / one-way awake body, |v|^2 and |w|^2
  below 0.1^2 -> counter + 1 (to 0xffff), else 0; per island with asleep / awake / awake-and-
  past-180 counts: all awake and past -> 0xd998a0 on each (not fixed / trigger, state bit 0
  "may sleep" set: sleeping bit, counter 0; velocities untouched); mixed asleep and awake ->
  all woken (counter 0). Sleeping bodies skip forces and integration. Ported.
- Contact reports (0xcf6a10, flags bit 1; before the pipeline, after detection): per pair with
  points a 400-byte record (manifold points as world points A / B, normal, distance; body ids at
  +0x184 / +0x186); per-body lists by pushing each record in front (0xcf69..). The engine's
  query 0xcb1420 (via 0xcad2a0, used by MupRootDynamics) walks a body's list (newest first),
  skips bodies in an ignore set (+0x980, not ported) and returns per contact the other body and
  per point: the point on the queried body, the normal from the other body towards it, the
  distance (positions and distances divided by the world scale). Ported as reports() /
  contactsOf().
- Positions: 0xd99c40 rebases (cell + offset, cell size global 0x30deef0) when world flag
  +0x23 bit 0 is set; the port keeps plain floats.
- Check (beacon-physics drop): a cylinder dropped on a game floor mesh lands on its side, rolls,
  tips onto a cap and rests with four contacts at ~0.5 mm penetration. dropbox: a box
  (0.15 x 0.1 x 0.1) dropped on the flat canyon pool floor (mup_bg_canyon_waterpool_a_col) rests
  flat within 2 s with four contacts at ~0.3 mm. dropcap / drop with the same sizes on that floor,
  axis horizontal, even from 1 cm: the body picks up roll about its axis at landing (~1.5 rad/s)
  and keeps speeding up while rolling (~3.3 rad/s after 5 s, rolling without slipping) on a
  floor that is flat to 1e-16 - energy is gained. Investigated 2026-10-13:
  - Cause: the landing points come from triangle edges of the floor grid beside the axis (the
    facet routine's segment pairs; identical to the original's), so the support is off-centre
    and starts the roll. While rolling, the manifold keeps points for ~3 steps (refresh: sideways
    <= sqrt(0.002)); their B ends turn with the body to behind the centre, still carry part of
    the load (gravity makes them approach faster than the spin lifts them) and push the roll on.
    Dropping kept points every step (experiment) still leaves a slower roll (~0.8 rad/s).
  - Everything on this path matches the original routines run natively (solverprobe.py): set-up
    0xe68f10 + warm start + three iterations of 0xe69730, the position pass 0xe69e00,
    integration 0xd99cb0, manifold add 0xe0cee0 / refresh 0xe0d6e0 sequences, rolling friction
    0xe6add0 / 0xe6af50 (3,000 random cases each, relative differences <= 2e-5); the contacts
    as in "Primitive pairs". So the original behaves the same for the same inputs; what stops it
    in the game is rolling friction, which bodies from .pfb files do not have (0): with the
    physical block's 0.03 the same capsule settles within 1 s (BEACON_DROP_ROLLING=0.03).
  - Not verified natively: the whole step as one (order and glue in 0xdd2bb0, including the
    inline warm starts, which the harness reproduces in C).
- Open: the pair array's own order (assumed by body ids; it feeds the stable sort), the use of
  +0x68 (1.2), trigger pairs (detected and reported by the library, skipped by the port), the
  report ignore set, the cell + offset positions (whether the game sets the rebase flag; cell
  size), pair byte 2, edge byte 2 masking in the islands (see "Primitive pairs"), the swept
  list's open items (see "Swept contacts"), the whole step
  verified as one against the original (only its routines are), a game-level check that pfb
  capsules / cylinders roll on flat ground as the port's do.

## Swept contacts (ported: src/physics/rigid/SweptContacts, ConvexSolver separation, 2026-10-13)
The second per-pair list is the library's continuous collision for fast bodies.
- Shape type 6 = swept sphere. The pfb importer gives a sphere a type-6 copy when the body
  record byte +0x201 is set; in the game data only mup_inflate_enemy_eye_col.pfb has it
  (r 0.0922, active body). Collidable +0xf8 marks a body that has one (0xd97910 / 0xd97a60).
  The broadphase box of such a body is the union of its current box and the box at the
  predicted pose (0xdb6020).
- Detection (0xdbe190): type-6 shapes take part only when one of the two bodies has one and
  the pair moves fast: |wA - wB|^2 > 0.05 or |vA - vB|^2 > 0.25 (library units). Predicted
  pose = position + dt v, orientation q + dt/2 (w,0) q normalised. Otherwise the swept sphere
  is skipped (the body's ordinary sphere does the normal contacts).
- Pair functions (table row / column 6): with the swept sphere at its current pose and the
  partner apart (distance > 0; touching is left to the ordinary shapes), the closest pair
  becomes a candidate. The sphere's sweep from its current to its predicted centre, as a capsule
  (centre midway, half length |d|/2, x axis turned onto d; identity below 1e-5), decides the hit
  flag 0x10 on the partner's sub-shape word (word = index << 32 | 1, or 0x11).
  - Sphere 0xe13300 / 0xe13930, box 0xe13f60 / 0xe14590, capsule 0xe14bd0 / 0xe151f0: the
    primitive pair math of "Primitive pairs" for both tests.
  - Cylinder 0xe15830 (sphere A) / 0xe16590 (cylinder A), hull 0xe15ed0 / 0xe16c60: the
    distance search below. Gate: distance < 1e38, status 0 (apart), distance > 0. Normal =
    the search's normal turned by the first shape's world rotation and normalised (negated in
    0xe16c60, which searches sphere-first with its outputs swapped); distance recomputed as
    n . (world point A - world point B). Against hulls the sphere's point is replaced by
    -r n (sphere frame). Hit: sweep search status 1; in 0xe16590 also |sweep distance| < 1e-5.
    Search order: e15830 (sphere, cylinder) then (sweep, cylinder); e15ed0 (sphere, hull) then
    (sweep, hull); e16590 (cylinder, sphere) then (cylinder, sweep); e16c60 (sphere, hull)
    then (hull, sweep).
  - Meshes 0xe17320 -> 0xe7b5a0 -> island 0xe7f990: sphere-facet separation 0xe7c260 and the
    centre path against the triangle 0xe7d9b0; sub word 0x02 / 0x12 | facet << 8 | island << 16.
  - Two swept spheres: no function.
- Candidate store 0xe0c630 (cache +0xc count, +0x18 entries of 0x60 bytes, at most 128): a
  candidate whose two points are within 1e-4 (squared 1e-8) of a kept one replaces it when
  (subA | subB) & 0x10, else it is dropped; otherwise appended.
- Pair list (reset 0xe0db70, add 0xe0d9d0): 0x7c-byte entries at pair +0x2c0, count +0x0c,
  capacity +0x0e; deduplicated on the local points (1e-4); rebuilt by every detection. When both
  bodies are active / keyframe / one-way and have swept shapes, a candidate with subA & 0x10
  flags body B and subB & 0x10 flags body A (state flag 0x20); integration clears the flag.
  Pairs with only swept points are still solved.
- Solver: set-up 0xe6a570 only when either body is flagged (else the list is cleared): one row
  per point, rhs = -(d / dt + n . vAB) jdi, jdi = 1 / (n K n) (no 0.001 term), limits
  [0, 1e38], no warm start; solve 0xe6aa90 after the ordinary points of an iteration, before
  the rolling row. The row keeps the gap from closing within the step.
- Detection threshold (bug fixed while porting this): the pair functions are called with
  threshold 0, so only results with distance < 0 become ordinary contact points. The port had
  kept positive distances (which stopped fast spheres early).
- Distance search 0xe1d110 (context 0xe17c50 / 0xe17fe0; supports sphere 0xe22a60, capsule
  0xe229f0, cylinder 0xe22a80, hull 0xe22750): in A's frame; first direction from B's origin
  to A's, unit, with |t|^2 as its squared length (a quirk: the first support query of A is not
  unit length); per step A's support at -v/|v|, B's at the unit direction in B's frame; stop
  when the new point repeats one (within 1e-8 squared) or, after the first step, |v|^2 - v.w
  < 1e-6; status 1 (overlap) at four points or |v|^2 < 1e-12; at most 11 steps. Result: points
  = weighted simplex points, normal = the direction of the last support query (after the step
  limit that is the closest point before the last simplex step), distance = n . (pA - pB).
- Simplex step 0xe1b870 (signed volumes): barycentric coordinates of the origin; a triangle
  is projected onto the plane of its largest area (ties: the first; areas and sub-areas in the
  original's operation order); a sub-simplex is searched where a coordinate's sign is not
  clearly (beyond 1e-6) that of the whole. The best candidate is kept by squared distance
  (start 1e38), but segment interiors are compared without lowering it, and a triangle's
  inside is only taken when none of its edges was searched. Two points: when the newer point is
  the closest, the step returns before its compaction (count 1, slot 0 still the older point,
  whose A and B points end up in the result). Kept points are compacted in index order.
- Verified natively (pfxrun):
  - simplex step (simplexprobe.py, random and near-degenerate simplices): 69,999 / 70,000;
  - distance search (gjkprobe.py, spheres / capsules / cylinders / hulls): 9,433 / 9,500
    within 1e-3 (seeds 2, 3); the rest stop one step apart (the stop test 1e-6 against |v|^2
    ~ 0.6 at float noise; the original's reciprocal estimates are not reproduced bit for bit);
  - pair functions (sweptprobe.py, 6,000 cases): the six primitive ones identical; cylinder /
    hull 95-99 % within 1e-4, all but 0.3-0.8 % within 1e-2 (same ties, and borderline sweep
    touches flipping the hit flag);
  - mesh (sweptprobe.py mesh): 783 / 800, scaled 780 / 800 (facet-feature ties, full store);
    0xe7c260 alone 5,000 / 5,000;
  - set-up / solve: 3,000 / 3,000 (solverprobe.py swept).
  - World check (beacon-physics sweepdemo): a sphere (r 0.3) at 120 units/s against a 0.1 thick
    box, hull or cylinder wall tunnels as a plain sphere and stops at the face (x = -0.35)
    with a swept sphere. Closed 2026-10-14: the plain sphere passes whenever a step carries its
    centre to or past the wall's middle (from x = -3 at 1/60 s: 59 units/s lands at -0.05 and is
    pushed back, 60 lands exactly on the middle and 61 at +0.05, both pushed out the far side) -
    discrete detection with the verified pair routines, not a 60 units/s anomaly; the earlier
    note "other speeds stop" was wrong.
- Not ported on purpose: the type-7 container (0xe177f0, row 6 column 7): the game never
  creates type-7 shapes (see "Shape types"). Open: the mesh paths for other layouts
  (0xe80540, 0xe7e9b0; 0xe7aae0 beyond layout 2) not ported; candidates beyond the 128-entry
  store; the per-shape user word (shape +0x48) copied into the entries' extra data is not
  carried; the eye body's swept sphere not yet exercised in a game-level comparison.

## Casts and queries (ported: src/physics/rigid/ShapeCasts, RigidWorld, physics/PhysicsWorld, 2026-10-13)
The module has three query classes; each runs a world cast over the eight broadphase trees and,
per body and shape, an entry of its own per-shape table. All three keep the nearest t over the
trees; filters as in "World query" (body +0x8 / +0xc with the query's masks, shape +0x3c / +0x40,
groups). Results go back to game units (point / world scale via a refined reciprocal).

| query (game class)                | module   | world    | per body | table      |
|-----------------------------------|----------|----------|----------|------------|
| ray (0xc226a0, run 0xc22810)      | 0xcb8910 | 0xcf7900 | 0xdc2d80 | 0x1720580  |
| sphere (0xc22cb0, run 0xc22e20)   | 0xcb8d50 | 0xcf8080 | 0xdc9980 | 0x17204f0  |
| capsule (0xc23180, run 0xc23300)  | 0xcb9190 | 0xcf87d0 | 0xdbc4c0 | 0x1720460  |

Game query layouts: ray +0x20 from, +0x30 to, masks +0x40 / +0x44; sphere +0x20 from, +0x30 to,
+0x40 radius, masks +0x44 / +0x48; capsule +0x20 from, +0x30 to, +0x40 radius, +0x44 half length,
+0x50 orientation quaternion (the capsule lies along its x axis), +0x60 self mask, +0x64 target
mask. Results: hit flag, +0x10 point, +0x20 normal, +0x30 body, +0x34 fraction.

Module rules (all three): a hit whose point or normal is not a number counts as no hit
(cmpps ORD checks); for a mesh hit (shape type 5) the normal is replaced by the facet's stored
normal (0xd97eb0: layout 2 facet record +0..+8) turned by body rotation x shape rotation (shape
+0x2c quaternion; the mesh scale is not applied).

- **Sphere table** (0x17204f0): sphere 0xe4caa0, box 0xea9590, capsule 0xea9950, cylinder
  0xeaa9d0, hull 0xe3d3c0, mesh 0xe3e620 -> 0xe46180 (islands 0xe43f70). A start within reach is
  a hit at t = 0. Verified (castprobe.py): sphere 100 %, box ~99.5 % (radius-0 face hits flaky by
  rounding in the original), capsule ~100 %, cylinder 4,000 / 4,000, hull all but radius-0 normal
  noise, mesh 1,000 / 1,000, scaled 997 / 1,000.
- **Ray table** (0x1720580; wrappers 0xe58cc0.. pass radius x scale and the transform / scale):
  sphere 0xe67210 (quadratic; a start inside misses; normal from the centre, -d / |d|^2 within
  1e-5^2), box 0xeaacd0 (slabs in the box frame, a start inside misses, parallel within 1e-5
  outside the slab misses, entry axis y when x <= y and z < y, x when y < x and z < x, else z;
  t > 0, no t <= 1 test of its own), capsule 0xeaafc0 (side quadratic, |x| <= half; else the end
  spheres, the nearer valid), cylinder 0xeabec0 (side as the capsule, normal from the axis or
  -d normalized on it; else the nearer cap plane inside the radius), hull 0xeab470 (in the hull
  frame by the inverse of the pose, so no mirrored swap; front facets, Moller-Trumbore with
  barycentric tolerance 1e-5, t in [0, 1]; normal through the inverse transpose), mesh 0xe5bf00
  (layout 2: Moller-Trumbore per facet, front facets only in face mode 0 - the ray +0x38 byte:
  0 front, 1 back, 2 both; strictly nearest). Verified (castprobe.py with RAY=1, pfxrun rcast /
  rmeshcast): primitives 20,000 / 20,000 identical, meshes 3,000 / 3,000 and scaled 2,000 / 2,000.
- **Capsule table** (0x1720460): sphere 0xe28dc0, box 0xe29410, capsule 0xe29cb0, cylinder
  0xe2a4c0, hull 0xe2abb0, mesh 0xe2baf0 -> 0xe2f560 (float islands 0xe2d330, facets 0xe2bdc0).
  Primitives: the convex cast (0xe1dc90 / 0xe1dcb0, as castConvex) of the capsule (support
  0xe229f0) against the shape (supports 0xe22a60 sphere, 0xe22970 box, 0xe22a80 cylinder,
  0xe22750 hull). Moving hit (status 0, t below the best): p = the capsule's point at its start
  in its frame; normal = -(R n) with n = (0, p.y, p.z) when |p.x| <= half, else p - (+-half, 0, 0),
  normalized; when |p.y| and |p.z| are both below 1e-5 the normal is the constant (0, 0, -1),
  not turned; point = start + t d + R p. Overlap (status 1, t = 0): the shape's point q nearest
  the capsule's centre (sphere: centre + r n towards the start, (0,0,1) when coincident; box:
  clamped, or from inside out through the nearest face; capsule: from its axis, its z axis when
  on it; cylinder: 0xea8a80; hull: nearest point of the facets the start is in front of), then
  c = the capsule axis point nearest q: normal from q towards c, point q, or c - radius n when
  q lies outside the capsule (box / capsule / cylinder inline with the clamped parameter and
  fallback (0,0,1); sphere / hull through 0xe23c40 by regions, fallback the turned z axis,
  negated by the caller). Mesh: tree boxes grown by (max(|axis part across d|, |part along d|) +
  radius) / smallest scale with the half axis R (half, 0, 0); island tree nodes entered at or
  before the best t; per facet the start must be in front; 0xe2bdc0: the convex cast against the
  triangle about its centroid (support 0xe22420, ties to the later corner); overlap: the
  triangle point nearest the capsule centre, distance^2 from it (limit (radius + half)^2); ties as
  the sphere mesh cast (1e-5, most head-on); result point = the facet point nearest the centre
  at t, normal from it to the centre (not a number when the centre lies on the facet - the module
  then reports no hit; mesh normals are replaced by the module anyway). Verified (castprobe.py
  with CAPSULE=1, pfxrun ccast / cmeshcast): primitives 2,951 / 3,000 within 1e-4 (the rest are
  the convex cast's convergence noise, t within 1e-4, box edge normals up to 0.07; no hit / miss
  disagreements), meshes 983 / 1,000 and scaled 974 / 1,000 (the same noise, one tie).
- Level check (beacon-level physcompare, BEACON_DUMP_MESHCAST): of the down-casts where the
  old triangle-soup CollisionWorld and the PhysicsWorld disagree on ninja_stage, all 258 mesh
  cases replayed on 0xe3e620 give the PhysicsWorld's answer (the old world hit back faces and
  facets the start is behind).
- The library ray of the game is not the sphere cast with radius 0: rays starting inside a
  primitive miss, and the box test is exact.

## Shape types (corrected)
Types 0 sphere, 1 box, 2 capsule, 3 cylinder, 4 convex, 5 large triangle mesh (all primitives
-> 0xe12380 -> traversals 0xe8b020 / 0xe8bc80, mesh flag bit 0 selects the per-primitive
island tables 0x1329f70 (sphere 0xea4f10, box 0xe903d0, capsule 0xe980a0, cylinder 0xe9ffe0,
convex 0xe9c850) or 0x1329f5c (0xea3540, 0xe8cd30, 0xe93980, 0xe9e9b0, 0xe9b6c0)), 6 a sphere
variant: the swept sphere (see "Swept contacts"; the earlier guess "character core sphere"
was wrong), 7 a
container traversed by 0xe87b00 / 0xe68540 with callback 0xe87ec0 that dispatches each element
through the table again (swept sphere: 0xe177f0 -> 0xe7a370, the sweep's box in the container's
frame, callback 0xe7a790 looking up each element's function with 0xe17c00).
Who creates type 7 (checked 2026-10-13): every creator of shapes through the add routine
0xd978b0 sets types 0-6 (body builder 0xcb5880: boxes 1, spheres 0, cylinders 3, capsules 2,
hulls 4, swept spheres 6; serialized meshes 0xcb2a10 and 0xcb2170 / 0xcb2d10: 5; the other
module creators 0xcb3080 ... 0xcb8120, 0xcbf030: 0-6 or copies). Type 7 is written only by
the library's world-snapshot reader 0xe06d20 (re-linking shape objects by id for types 4, 5
and 7) and its shape builder 0xdfb1f0 (vtable 0x16f1f40, used by 0xe06d20 alone). The game's
only route there is a debug dump (0xc22540 -> 0xcade30 "Serialize >> %s" -> 0xcfb910, which
opens /data/data/physics_snapshot.txt with mode "w" -> 0xe0a1f0 mode 0 -> 0xe06d20). So no
type-7 shape exists in play: the row and column 7 functions (0xe127c0, 0xe12b30, 0xe12d70,
0xe12fa0, 0xe177f0) are unreachable and not ported. Decompiles: evidence/decomp/pfxtype7. The PELM header byte 7: open whether type or version.
Triangle-sphere test 0xea44f0 (550 instructions; constants 1e-5, 1e-4, 0.9999, 255, pi/2; six
calls to a math routine, edge angles): ported (see "Primitive pairs", "Meshes").

## MupRootDynamics (enemy +0x2570; code 0x7dc000-0x7eb000, 118 functions; decompiles rootdyn/)
Fields: +0x38 mode (1 initial, 2 ground, 4 air, 8 thrown, 0x10 held), +0x3c previous mode,
+0x44 probe length, +0x48 probe radius, +0x4c ground distance, +0x58 ground threshold, +0x5c
fall-speed limit (< 0: none), +0x60 gravity, +0x64 enabled, +0x100 m_pos, +0x110 rotation,
+0x120 m_scl, +0x130 velocity, +0x150 body angular velocity, +0x160 / +0x1c0 / +0x1d0 position
offset smoothing, +0x170 rotation (model), +0x190 saved velocity, +0x1a0 body centre, +0x210 /
+0x220 last position / rotation, +0x240.. output transform, +0x290 / +0x294 / +0x298 query
filters, +0x2a0 body, +0x2b0.. body-local frame (flag +0x6c), +0x338 m_sensors (sensor kind 2:
sphere sweeps; others: trigger bodies), +0x420.. contact list (0xcad2a0) with accumulated
filter bits +0x440, +0x444 time in mode.
- Step 0x7dfe50 (in the enemy update, before the behaviours): 0x7e0020 (sensors), 0x7e0770
  (read back the body: position / velocity / angular velocity; contact push direction), 0x7e1600;
  modes other than 1: +0x190 = v; other than 1 and 8: v = clamp(achieved (pos - last) / dt,
  -|v|, |v|) per axis; ground probe 0x7e20e0; offset = pos - +0x160, smoothed +0x1c0 = 0.8^frames
  s + (1 - 0.8^frames) offset unless all within 0.001 (then = offset); output 0x7e4850; last
  position / rotation saved.
- Apply 0x7e5c60 (enemy late update 0x2df6a0, after the behaviours): +0x444 time in mode;
  virtuals +0x50 / +0x48; 0x7e61b0; air mode: v.y += dt gravity, |v| limited by +0x5c (>= 0);
  then 0x7e71b0 writes velocity, angular velocity, position, rotation, motion type and filters
  to the body. The physics world step then moves the body.

## MupRootDynamics (ported 2026-10-14)
- Body: the definition's .pfb body named by Bind/RootCollision (enemy_torso_col, a cylinder) at
  the root pose x the Bind/BodyJoint's model frame (0x7e83b0, refreshed every update from the
  animated joint; offset +0x280 = 0) x a set-up offset +0xc0 ((root x frame)^-1 x the body's
  pose as created). The body is created at the placement x the record's body frame (0xbd91a0;
  "Body frames"), so the offset is the joint's base frame^-1 x that frame: enemy_torso_col's body
  frame (0, 0.09633, 0) turned Rz(+90 deg) against joint_torso's base (0, 0.09384, 0) gives a
  quarter turn about z (the cylinder upright: the former hypothesis) 0.0025 above the joint.
  The frame is scaled by m_scl (+0x120). Set-up 0x7dd0c0: mass 200, inertia
  0xcacc40 -> 0xcbb400 -> 0xde60d0 (the library's cylinder rule: m r^2 / 2 about the axis,
  m (3 r^2 + 4 h^2) / 12 across, h the half length) called with twice the shape's radius and
  half length (0xca9340 / 0xcb9a60 fill the description: +0x20 radius, +0x24 half length; a
  capsule's half length with its caps) - four times the solid cylinder's (2026-10-14, checked on
  the combat7 ledge contact: spin after it (15.5, 0.9, 15.9) vs the original's (15.0, 2.4, 13.9),
  with the plain cylinder (24.7, -2.5, 29.1)), friction 0.01 (+0x3f4); fixed with self filter 0 until the
  mode leaves 1 (0x7e71b0: active, self 0x1000 = +0x290 set by 0x2da770, target 0xffffffff).
- Step 0x7dfe50 (start of the enemy update): read-back 0x7e0770 - thrown (8): pose and velocity
  from the body; else the position only (the frame undone with the root's own rotation), and
  only when it moved more than sqrt(1.19e-7); held (0x10) keeps the root (it follows its holder,
  0x7e61b0; run rootdyn1: root fixed, body following the frame). Modes != 1, 8: velocity =
  per-axis clamp of the achieved velocity to the commanded speed. Ground probe 0x7e20e0 (when
  +0x66, set by the behaviours' collision switch 0x7e9db0): sphere query radius
  GeometrySensorRadius (+0x48 from kind +0x3a50: 0.025) from the body centre's height down to
  1 m (+0x44) below the root, query masks self 0x1000 / target 0x88040810 (+0x298); ground =
  hit height; walkable when n.y >= sin 60 deg (+0x88), kept as the last walkable ground; on a
  steeper hit within StepRadius (+0x90, kind +0xac0: 0.1) of it the ground stays at its height
  minus tan(StepSlopeAngle (+0x94)) x distance; ground distance +0x4c = root - ground (-1 no
  hit, -2 in modes 1 / 0x10 / 8); ground mode snaps to it, air mode is lifted when below; a
  moving ground body carries the root (+0x1f0). Simplifications: of the probe's cached results
  only the last walkable ground (+0x500: body +0x508, point / normal in its frame +0x530 / +0x540,
  placed again each update, dropped when the body's filters stop matching or its normal turns
  below walkable) is ported (2026-10-14); the last ground (+0x4b0.., same layout, dropped when
  its normal turns downward) and +0x470.. are not, the snap condition (port: always in ground mode) and the output
  smoothing (+0x1c0, +0x250: drawing only) are not reproduced.
- Offset smoothing, closed 2026-10-14: +0x160 / +0x170 are the root position / rotation the apply
  0x7e5c60 leaves in modes 1, 8 and 0x10 (0x7e5f34; the same copy as the small helper 0x7e7180,
  which has no direct caller). The step's 0x7e4760 (inlined at 0x7dff13) keeps d = read-back
  body position - +0x160 at +0x1d0 (how far the world step pushed the body off the root) and eases
  +0x1c0 towards it: kept equal when every axis is within the threshold, else
  +0x1c0 = 0.8^(60 dt) +0x1c0 + (1 - 0.8^(60 dt)) d (dt from the system timer). Nothing reads
  +0x1c0: a scan of every enemy / object function for vector accesses at the embedded copy
  (enemy +0x2570 + 0x1c0 = +0x2730) finds only other classes' own fields there, and the output
  0x7e4850 does not touch it. The only reader of the pair is MupFishEnemy (0x453f83 adds +0x1b0,
  the last apply's position change, and +0x1d0 for its swim speed along its facing). The port
  keeps neither; a fish port needs +0x1b0 / +0x1d0 (static scan: a read through a pointer kept
  elsewhere would not show).
- Apply 0x7e5c60 (late update): frame refresh (virtual +0x50 = 0x7e83b0) first, so the last
  pose's body and the target's share the frame (run rootdyn1: held, body velocity 0); root
  motion (virtual +0x48 = 0x7e8450: the clip's root delta x SpeedScale x scale x axis flags
  turned by the new rotation, plus +0x140 local velocity x dt, pivot kept); air: v.y += g dt
  (+0x60, world -9.8 when unset), fall limit +0x5c, root += v dt; + carry; yaw only (modes not
  1 / 8 / 0x10); velocity = (root - last) / dt; write 0x7e71b0: the body put at the last pose,
  move-to velocities 0xca5e50 / 0xca5ef0 to the target, gravity cancelled 0xca65a0 (which wakes
  the body, as does the force step 0xca6510 and set-velocity 0xca5fa0 - move-to alone does not,
  so without them the body would sleep while standing); max angular velocity 0 on mode changes,
  FLT_MAX entering mode 8. Then the world step moves the body with its contacts.
- Thrown: ThrownLoop (0x1686828, start 0x2f8010) switches the DeadCollision body on (virtual
  +0x380 = 0x2e2750: at the root x the matrix +0x3b70, active, self 0x1000, the enemy's velocity
  and spin; the other bodies' filters 0) and plays damage_hit_loop in mode 1 (mode 8 only without
  a dead body). +0x3b70 is set at creation (0x2daf40: the root's transform^-1 x the body's pose
  then, 0xca5f20 / 0xca5dd0), i.e. the record's body frame: enemy_dead_col 0.07 above the root.
  Its update 0x2f81c0 sets the root from the model's transform every update (the body sync: the
  body x its frame^-1 x the node frame, identity here);
  the stop 0x2f80d0 switches it off. Spin limit 60 rad/s = the dead body record's.
- Module body creation (0xcb5880): every body gets the record's mass, surface (restitution
  0.2 / friction 0.6 for these files) and limits; inertia of the box of its shapes about the body
  (0xde5fe0: full extents = 2 max(|min|, |max|) per axis, I = m (b^2 + c^2) / 12). With the
  level's restitution 0.2 the launch's ledge contact gives the original's impulse to 1 %
  (body velocity after it 1.957 / 3.646 / 0.898 vs 1.938 / 3.662 / 0.911).
- Damage body and punch: the Bind/DamageJoint body (enemy_head_col, hung on the joint its
  record names, joint_head, at its body frame seen from the joint's base frame: 0.0215 below
  it) is keyframed with self 0x1000 (0x2daf40; its target +0x3ac0 is
  open, port all bits) and follows the animated joint every update; the enemy's bodies share a
  group (state +0x10) so they never pair with each other. The hero's punch (0x674c80) is the
  module's sphere query with self 0x20000 / target 0x91041a0f minus the hero's bits: its nearest
  body decides (a wall takes the punch); an enemy's body gets the hit (`beacon-level punchtest`:
  21 of 22 forest_castle enemies hit from 0.3 m beside them, one behind level geometry).
- Thrown flight (run combat7: torso and dead body states watched through enemy +0x2810 / +0x3b40
  -> +0x48 state: position +0x40, orientation +0x50, velocity +0x60, spin +0x70): with
  DamageHitLocomotion (tuning +0x1010 = Bind value at name + 0x90; true for the basic kinds) the
  hit sets the velocity only; the torso keeps its resting pose on entering mode 8 (the pose and
  zero velocities are written only with flag +0x79) and flies without spin; the root follows it
  through the animated joint_torso of damage_hit (the joint rises 0.06 -> 0.44 in the clip, so
  the root lags the body 0.14 -> 0.26 -> 0.08); once damage_hit ends, 0x16877e8 (channel 2)
  sets the spin and ThrownLoop (channel 1) hands velocity and spin to the dead body (the spin
  scaled to its 60 rad/s limit); the root follows the model's transform, synced from the body
  before the step, so it holds on the switch update and trails the body by one update.
  Port: combat5 flight (3577-3640) mean 0.101 m, max 0.206 (was 0.23 / 0.46); combat7 relative
  to the pre-hit position mean 0.103, max 0.165; within 3 cm until the dead body tumbles (its
  0.077 offset turning). The spin is the original's exactly ((-64.5, 42.0, -93.6) vs
  (-64.70, 42.03, -93.50)).
- With the body frames (2026-10-14): enemy5 Cruising unchanged (max 0.0015 / 0.0054), Active
  means 0.153 / 0.157 m (0.186 / 0.142 with the node frames); combat5 before the hit 0.006 m off
  (0.076 with the node frames); punchtest 21 of 22.
- Open: the launch's ledge contact (run combat7, b959b1a7, update 3466, wall normal (-0.98, 0.13,
  0.13); run combat8 recorded the original's contact reports: three points, normals (0.984,
  -0.126, -0.127) / (0.608, -0.740, -0.288) / (0.845, -0.530, 0.067), depths -0.0238 / -0.0074 /
  -0.0154 game units - the port's manifold is the same three points, the second normal a few
  degrees off; with the inertia fixed the first response matches closely (v (1.467, 0.943,
  -0.254) vs (1.632, 1.148, -0.224)); what still differs is the next update, where the original's
  torso meets the wall again and the port's leaves it (landing 0.104 m apart, was 0.117)): the original torso leaves the contact with v (1.632, 1.148, -0.224) and spin (14.98, 2.41,
  13.93) (the limit 0 applies only in the force step, so the contact's spin lasts one step and turnTo
  undoes it), then keeps rubbing the wall for ~14 updates (body velocity y 4.03 vs the commanded
  4.79, x -0.55..-1.05 vs -1.10), the root falling 0.117 m behind the free arc; the port's body leaves
  with (2.04, 2.09, 0.29), no spin, touches once more and flies free (root 0.117 m ahead from 3479
  to the landing). Needs the original's contact points (manifold) at 3466. Earlier (enemy5) the
  port carried a penetration one more step (0.028 library units after the launch's ledge contact; the original clears it). Not the pass count: the step's
  counts (3, 3, 3, 2) are the port's, and the pass formula removes only 0.8 |d| / (|d| + 0.05)
  = 29 % of such a depth per pass, so the original avoids the depth some other way (needs its
  per-step body state from a run); MupRootDynamics sensors (+0x338) and contact list (0xcad2a0: the body's contact report, entries
  of 0xe0 bytes - up to four points of 0x30 {world point, normal, distance} (game units; 0xcb1420
  with points: the read-back passes 1), the count at +0xc0, the partner body at +0xc8, its
  material at +0xd0 (the watch path enemy +0x2998 reads them; the list is empty again by the end
  of the update but the buffer keeps the last report); the enemy's list at
  +0x2990 is filled again by 0x2e0130, which, while enemy +0x3aaf is set, maps each partner body
  to its object - its reactions are not decoded); type 2 hits (the punch routine 0x674c80 on a hit calls the enemy's virtual +0x370 and spawns
  the effect "ef_hero_hit_01"). Closed 2026-10-14: no punch hit stop - <Punch><HitStopTime>
  (0.034) is bound (binder 0x170150, the hero parameter singleton's property at +0xa620, punch
  group +0x7380) but nothing reads it: only the singleton's set-up touches +0xa620 / its value,
  the name string only the binder, its hash nowhere.

## Hero query masks (2026-10-14)
A query's self mask defaults to all ones (0xc226a0: +0x40 / +0x44 = 0xffffffff). The hero's:
wall capsule 0xd8241a1f and ground sphere 0xc804091f (self = hero +0x160, target without it);
the downhill ray (0x66ab90 at 0x66bcd1) 0xc804191f with the hero's bits; the drop edge's look
ahead (0x673f50 at 0x674039) 0xd8241a1f minus the hero's and its carried bodies' bits (list
+0x230), self all ones; the drop samples' sphere 0xc844191f (set by 0x66eb30); the edge face
cast (0x66eb30 at 0x66f2ea, r 0.01) 0xc804191f with the hero's bits; the teetering exit's wall
and ground rays (0x4cf230 at 0x4d31b1 / 0x4d3215) 0xd8241a1f / 0xc804191f as written, self all
ones. 0xc804191f differs from the ground probe's 0xc804091f by 0x1000: those rays see enemy
bodies. Replays unchanged (move1 6115, move2 5920, move3 8183).

## Body frames (2026-10-14)
A .pfb body record has two frames. The importer (0xc9cc70) copies +0x230 / +0x23c into the body
description at +0x90 / +0xa0 and +0x208 / +0x214 / +0x224 into +0xb0 / +0xc0 / +0xd0 (0x4e8 -
0x458 = 0x90 on its stack). Both body paths place the rigid body at the placement x (placement
scale x desc +0x90, desc +0xa0) - 0xbd91a0 (shapes) lines 1462-1548, 0xbdd350 (run-time meshes)
the same - and scale shapes by the placement's scale only; +0xb0..+0xd0 only enter a second
matrix (the model's sync). So +0x230 / +0x23c is the body frame and +0x208 / +0x214 / +0x224 the
node frame. Evidence in the data: mup_bg_beanstalk_cork's cylinder (shape at identity) stands
only with its body frame's quarter turn; mup_bg_bird_boss_platform_a1v's column of cylinders
(node scale 15, half length 0.79, spaced 1.5) fits only unscaled and turned upright by its body
frame; the basic enemy's torso and head (see MupRootDynamics). 2,741 of 4,257 shape bodies have
frames that differ; the port used the node frame until now. Replays unchanged (move1 to 6115,
move2 to 5920, move3 to 8183).

## Run-time meshes (kind 6; ported: src/physics/rigid/MeshBuilder, 2026-10-14)
Path: IfcPhysics 0xbd1290 -> 0xbdd350 (per large-mesh record, "Create No Serialized LargeMesh")
-> 0xcaa750 -> PhysicsWorldImpl 0xcb2170 -> builder 0xde8550 (0xdedc50 facets / vertices / edges,
0xdf07b0 + 0xdf0e50 islands, sort 0xdb0110). The vertices are multiplied by the placement's scale
(each part at least 0.1, 0x12f5b00; skipped when the scale is 1, 1, 1); the mesh shape sits at the
body's origin (shape offset ignored: all records have identity) with scale (k, k, k) x the world
scale (k = IfcPhysics impl +0x1d8: 1.0 from the constructor 0xbef220; its setter 0xbd11d0 has no
references; kind-7 shapes take k x the placement scale). Parameters (0xcb2170): flags 0x2c (0x20 layout 2,
0x8 elimination, 0x4 32-bit indices), strides 12, facet limit 64, epsilon 1e-5, thickness 0.025,
no user data (0x20 off and a user array: the u16 / index variant, unused here). Errors
0x80880001 (empty / too small), 0x80880009 (index range), 0x8088000d (no triangle left).
Rules (the facet stage differs from the open reference's pfx_mesh_creator):
- Facets: a triangle is dropped when |(p1-p0) x (p2-p0)|^2 < 1e-10 (and < eps^2 with 0x8);
  normal = normalize((p2-p1) x (p0-p1)) (rsqrt + two Newton steps); thickness 0.025; box of the
  corners and the corners minus normal x thickness. Unused vertices are marked merged; a later
  vertex closer than eps (squared < eps^2) to an earlier kept one is merged into it (its facets'
  first matching corner replaced).
- Edges: per facet and corner, the vertex pair hashed (0xd8163841 max + 0x8da6b343 min) mod
  (3 facets); an edge takes a second facet only while it has one (a third makes a new edge).
- Edge type (two facets): both centroids moved along the edge onto the plane through its first
  point (c - d (d . (c - p))); m their mean; dot(n0, m - c0) and dot(n1, m - c1) both < -1e-5:
  convex (1), both > 1e-5: concave (2), else flat (0); open edges convex. Edge angle
  acos(clamp(n0 . n1)) / 2; above 1.57 it becomes 0 and both facets' thickness 0. The tilt byte
  stays 0 (written only with AUTO_THICKNESS 0x10, off).
- Facet box, again: with a convex edge of angle > 0 the solid below the facet with side planes
  (edge x n) cos(a) - n sin(a) through the corners: the bottom corners move towards the planes'
  apex down to the thickness (the apex when nearer); else the first box.
- Islands: all facets (pushed at a list head: reversed) halved at the median (stable radix sort
  of the box centres' sortable bits) along the axis of the largest centre variance (x < y -> y;
  z wins only when strictly larger), each half's list reversed again; the smaller half recursed
  first (equal: the upper); halves of at most 64 facets become islands in that order, their
  facets in list order, vertices and edges numbered by first use.
- Island tree: facets as leaves, repeatedly the pair (first in list order) whose joint box has
  the smallest x + y + z extent joined (the pair's places refilled from the list end, the new
  node appended); written depth first from the root, the right child first, each node's box in
  floored 1/255 steps of its parent's (minus one step when not zero) and replaced by the
  dequantized box; one facet: a single node, kinds 9.
- Mesh tree: the island division itself (lower half left), written the same way but without the
  extra step; one island: a single node, kinds 9.
Verification (pfxrun `meshdump`: the game's builder run natively on `beacon-physics soup` files;
castprobe.py / meshprobe.py with BUILT=DIR, PORT_GAME_MESH=1 to cast against the game's own
build): challenge_start equal in islands, island bounds, mesh tree, facet order, vertex and edge
numbering where the island content agrees; normals within 1.2e-7 (the builder's rsqrt estimate
is hardware-specific); near-ties (mirror-symmetric meshes) make the island trees' child order and
some facets at the median split differ. Casts on all 19 run-time meshes: rays 5000/5000, spheres
4992/5000 (also scaled), capsules as against the game's own build (the capsule cast's known
precision). Contacts (port on its own build vs on the game's): 88-92 % identical; the rest keep
a different four of the same candidate points (cache order follows the tree order). Levels:
volcano 29 meshes, challenge_caterpillar 3, giant_1 1, city_funfair_remake 1 (placement check
0.0000 m); the boss arena meshes belong to objects spawned at run time.
Open: the exact rsqrt / rcp estimates (hardware).

## Body interface used by MupRootDynamics (module 0xca5xxx-0xca6xxx)
- 0xca5c70(body, type) motion type (state +1, wakes); 0xca5c50 reads it.
- 0xca60f0(v, body): state +0x24 = v (max angular velocity): FLT_MAX (0x1251c18) when entering
  mode 8, 0 on other mode changes (0x7e71b0) -> a walking enemy's body does not spin from forces.
- 0xca6020: max linear velocity / world scale.
- 0xca5e20 / 0xca5f40: set position (x scale) / orientation.
- 0xca5e50(dt, body, target): dt <= 0 -> world dt; library 0xd998c0: if dt > 0 and the body is
  not fixed: linear velocity = (target - position) / dt (positions are an integer cell at state
  +0x30 plus a float offset at +0x40; cell difference converted by 0xd97890).
- 0xca5ef0(dt, body, target rotation): library 0xd99970: angular velocity that turns the body to
  the target orientation in dt (shortest way: target negated when dot < 0).
- 0xca65a0(body): applies -mass x world gravity through the force step: cancels the gravity the
  world pass adds (the root dynamics body is weightless in the library; the controller adds its
  own gravity in air mode, 0x7e5c60).
- 0xca6510(dt, body, force, torque): force step (0xd9a050) with the force scaled.
- 0x7e71b0 (write to body), modes 2 / 4 / 0x10: target pose from the root (0x7de4f0 with the
  body-local frame +0x2b0), set position and orientation, move-to velocities (0xca5e50 /
  0xca5ef0 with dt -1 = world dt), gravity cancel, ... Mode 8: velocity / angular velocity copied
  once (flags +0x6e / +0x6f), gravity +0x60 applied as a force. Mode 1: fixed, filter 0.
