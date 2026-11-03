# Hero (the robot): tuning, state machine, movement rules (2026-10-12)

From the user's data and executable only. Evidence: `evidence/hero_params.tsv` (parameter ->
offset), `hero_param_users.tsv`, `hero_states.tsv` (state vtables + animations),
`hero_state_ids.tsv` (id -> name -> class), decompiles in `evidence/decomp/{hero,herostates,
statector,statetype}`; tools `tools/annotate_hero.py`, `FindDisp.java`, `vtables.py`.

## Tuning

`multi_platformer.xml` <Hero> is bound by a reflection binder (0x170150) into a singleton
(global 0x2ed3a38, 0xd4d0 bytes, created lazily at each use: the 135 callers of 0x170150 are
exactly the hero code). Each parameter is a property object (bool 0xa0 / float 0xb0 bytes)
whose value sits at +0x90; `hero_params.tsv` lists 251 names with their value offsets (nested
groups such as Stack/charge are not resolved yet). The game reads the shipped XML values, so the
port loads the same file.

## State machine

0x67f750 registers ~70 states: Register(machine, id, name, std::function factory). State objects:
vtable [dtor, dtor, Enter, Update, ?, Exit], +0x10 name string, +0x30 machine, +0x38 hero,
+0x40 time in state. Requests: machine+0x14 = id, +0x18 = pending (0x6a7350).
Core ids: 1 Idle, 2 Walk, 3 Jump, 4 Fall, 5 Land, 6 Hover, 7 Slide, 8 Punch, 9 JumpPunch,
10 SpinAttack, 11 Damage, 12 LightDamage, 17 DebugMove, 19 Teetering, 20/21 PickUp/Throw,
27 SwimWalk, 48 FloatOnWater, 55-65 deaths (names partly reassembled from inline string
fragments: see hero_state_ids.tsv). Animations are selected by FNV-1a of the names in
`mup_hero.odx` (278 animations, 11 sequences), which named every state class.

## Movement rules recovered (fixed update; dt passed in xmm0)

- **Ground (0x683ed0, Walk)**: target = Speed (x stack/charge/water multipliers, 0x684420) *
  stick (x, z). a = lerp(AccelLimitMin, AccelLimitMax, |stick|). d = target - v.xz. If the
  projection of d on v.xz is longer than a, d is scaled by a / |proj|; v.xz += d (per update,
  not scaled by dt). From rest (v = 0) the projection is undefined and v jumps to target.
  v.y untouched. Tilt = wrapped heading change * TiltScale, clamped +-TiltMaxAngle. Facing
  follows v when |v| > 1.19e-7.
- **Air (0x684580, Jump/Fall/Hover)**: k = lerp(ControlOnJumpMin, ControlOnJumpMax, |stick|);
  v.xz += (Speed * stick - v.xz) * k per update.
- **Jump enter (0x4caf20)**: hMin/hMax = stackScale * JumpHeightMin/Max (water: InWater*/
  WaterSurface* variants); v.y = sqrt(-2 g hMin) (+ upward speed of a moving support, whose
  horizontal velocity is also inherited in some cases).
- **Jump update (0x4cb530)**: while rising, jump held and not in water: v.y += dt * (v0^2 *
  -0.5 / hMax - g), i.e. effective gravity -v0^2/(2 hMax) so the apex reaches hMax; released:
  normal gravity (apex hMin). After 0.1 s a new press -> Hover (6). Grounded -> Land (5).
  v.y < 0.1 (or -1.0 with a flag) -> Fall (4). Air control skipped for 0.3 s when a flag is set.
- **Hover (enter 0x4cc440 / update 0x4ccee0)**: enter: v.y < HoverVelocityOnStart -> v.y =
  HoverVelocityOnStart, else keep rising until below it. Update while (held or t < HoverTimeMin)
  and t <= HoverTime: s = t / HoverTime, v.y += dt * (lerp(HoverGravityStart, HoverGravityEnd, s)
  - g). Air control applies.
  Rising (entered at or above HoverVelocityOnStart): plain gravity; the update that finds v.y below
  it still gets no hover gravity, the next one does (run roof_box1, 4112-4115: 0.5047, 0.3547,
  0.2047, then 0.2076; the port matches to 4 decimals since 2026-10-15 - before, it added hover
  gravity on that update and climbed at ~0.36 m/s; earlier replays entered below 0.4 and never
  took this branch).
- **Coyote time**: Walk keeps running while airborne up to JumpableTimeAfterFalling (0.15 s;
  Walk compares its own airborne timer with hero parameter +0x810).
- **Body step (0x66ab90)**: v.y += g dt (Gravity -9; water: Buoyancy, Viscosity, FallViscosity
  after FallViscosityTime; lava: LavaBuoyancy); horizontal target = pos.xz + v.xz dt solved by
  0x66dd40; vertical/ground and slopes afterwards (SlopeMaxAngle, SlopeFight*, IsSlope).
- **Collide-and-slide (0x66dd40)**: recursive, at most 6 iterations; shape cast of a capsule
  (radius CollisionForWallRadius 0.06, height CollisionForWallHeight * 0.5, raised by
  CollisionForWallYOffset 0.14) from the current to the target position, filter mask
  0xd8241a1f minus per-hero bits; on hit at fraction < 0.001 with the contact within the
  capsule's vertical span: slide.
- Collision sizes (metres): foot capsule CollisionYOffset 0.12, CollisionHeight 0.05,
  CollisionRadius 0.08; ClimbableStepHeight 0.04. Hero model Scale 0.22.

## Input, state switching, ground (recovered 2026-10-13; each checked against traces)

- **Pad stick (0xc89950, called by the pad reader 0xc87230 with 0.1 / 1.0 from 0x1301ba0)**:
  s = 2 (v/255 - 0.5) per axis; |s| < 0.1 -> 0, else s * (|s| - 0.1) / 0.9 (length may exceed
  1 on diagonals). Port: `game/PadStick`.
- **Hero stick (0x667b20)**: per-axis |s| < MinStickInput (0.07) -> 0; mapped onto the ground
  with the camera's right/forward; length > 1 (0x12221cc) is scaled to 1; "no stick" (+0x11c)
  below FLT_EPSILON. Port: `setHeroStick`.
- **State machine (0x6a77a0)**: time += dt, current Update; if a state was requested (0x6a7350,
  first request wins): Exit, create, Enter in the same update; the new state's Update from the
  next one. Then the body step (0x66ab90). Consequences seen in the traces: from Idle the hero
  first moves one update after the push (Idle 0x4c8c60 only requests Walk/Jump/Fall/Slide);
  Jump.Enter sets v0 and the same update's body step subtracts g/60 (first airborne vy = v0 -
  0.15).
- **Walk (0x4c9b10)**: ground movement, then: jump pressed -> Jump (speed > 0.5 sets the running
  flag +0x80); slope -> Slide; teeter; not grounded beyond JumpableTimeAfterFalling -> Fall;
  speed < FLT_EPSILON -> Idle (so Walk decelerates to a standstill first).
- **Land (enter 0x4cbff0, update 0x4cc150)**: clip jump_01_land (jump_02_land after a running
  jump, stacking_jump_land when stacked; FNV-1a 0xb35989db / 0x95a52444 / 0x2df29ce2); ground
  movement while grounded; jump -> Jump; stick held and t > 0.06 -> Walk; t > 0.1 slope ->
  Slide; clip finished -> Idle. Clip length from the game's .anim (0.75 s): `game/HeroClips`.
- **Ground probe (0x66ab90, ~line 470)**: sphere cast radius 0.01 (0x12221fc) from (new x, old
  y + 0.5 * model scale (+0xc0 = 0.22), new z) to the new position (after v.y += g dt); with flag
  +0x1b8 or mode 3 the end is 0.2 m lower (0x12221f4; not ported, flag 0 while walking).
  Hit: feet on the contact height, v.y = 0. Hence: feet rest exactly on the floor; ledges are
  left falling; steps up to 0.11 m are climbed.
- **Foot body (0x667200)**: capsule radius CollisionRadius, half length 0.5 CollisionHeight,
  offset (0, CollisionYOffset, 0): its bottom is 0.04 above the feet, so trim lower than that
  (moss pieces at the forest start) passes beneath it.
- Foot body as a physics body (hero +0x150, 2026-10-14): created by 0xcaa360 -> 0xcb56a0 as an
  active body (state byte 1 = 1) of mass 100 that never sleeps (0xd99850 leaves the sleep bit of
  byte 0 clear: 0x0c); self = 1 for the robot (0x12241d4 holds 2 / 4 / 8 for heroes 0..2), target
  0x7fffffff (+0x164). The hero update ends with 0x66d5f0 (after the body step 0x66ab90 and
  0x66ce60): move-to velocities to the feet (+0xa0) for one world step (mode +0x168 == 2: set
  there, velocity 0), orientation identity (0xca5f40), no spin. Enemy sensor body (+0x158):
  0xcaa220 cylinder, radius EnemySensorCollisionRadius, half length 0.5 EnemySensorCollisionHeight
  (binder defaults 0.3 / 0.1, not in the XML), centred 0.5 Scale above the feet; trigger, self
  0x800000, target 0x10041000; set to the feet each update. Ported as game::HeroBodies (scene and
  enemytrace); the contacts the hero reads back (0x668240: running into an enemy, move1 at 6115)
  are not ported yet.

## Verification (trace harness, docs/harness.md)

- In-place jumps (run world21): every y / vy sample equal to 1e-6 (apex 0.31965, 32 updates).
- Movement run move1 (forest_castle, frames 5001-7304: walks in four directions, partial and
  diagonal sticks, stops, walking jump, held jump + hover, moss steps and ledges): replayed with
  `beacon-level heroreplay`, positions equal within 1e-3 m (in practice 1e-4) for 1,114 updates
  until frame 6115, where the original runs into the enemy spawned at
  enemy_locator_ground_standard_A_01 (1.00, 48.74): run-time objects are not ported yet.
- Second movement run move2 (different line past the moss): equal until the hero lands next to
  a moss edge (frame 5199): the original moves 3.7 mm sideways (see "contact push" below).
- Stick sweep sweep2: the pad conversion equals the game's stick values for all 114 inputs
  (max 5e-7).
- Head yaw test headyaw2: the stick is mapped with the head's orientation (quaternion at
  [[main 0x2f014a8]+0x48]+8 -> +0x100 equals the scripted head yaw; +45 deg head -> forward
  moves at -45 deg). Port: the caller passes the view's right/forward to setHeroStick.

- **Jump (0x4cb530)**: Hover after 0.1 s on a press (unless blocked, +0x100 set by other
  systems), Slide on slopes, Land when grounded, Fall below 0.1 m/s (-1.0 with flag +0x51).
- **Fall (0x4cb940)**: air control; Slide; grounded -> Land (in water Idle; a hard-landing event
  above 1.5 m of fall); a press -> Hover only while +0x81 is set and +0x100 clear.
- **Hover flag +0x81** ("hover available"): set by Jump.Enter and by Idle/Walk when they ask for
  Fall (walking off an edge); cleared by Hover.Enter (0x4cc440): one hover per airborne phase.
- **Hover (0x4ccee0)**: not held and HoverTimeMin over, or HoverTime over -> Fall (always; Fall
  lands next update); grounded -> Land; slope -> Slide.
- **Land** has no Fall exit: airborne it waits for the clip, Idle then asks for Fall.
- **Contact push (0x66e950)**: hero+0x88 is a two-entry list [state machine, carry component
  (vtable 0x16d4000, constructed in 0x6a7b30)]. The push runs only while the carry component's
  flag +0x88 is set, which the pick-up routine 0x995d00 (PickUpAnimationTime, called from three
  hero states) sets together with the held body (+0x80): for each contact of the held body with a
  static or keyframed body, the hero moves 7 mm (0x1221110) horizontally away from the contact.
  Traced (run move3): the flag stayed 0 in a whole movement run. Not ported (no carrying yet).
- **Landing shift beside an edge (closed)**: in run move2 the hero landed beside a moss lip and
  moved 3.7 mm horizontally away from the lip with no horizontal velocity. Cause: 0x66dd40 slides
  the full 3D displacement (including the vertical drop), not only x/z. Port: `Hero::slide` casts
  the wall capsule (upright, radius 0.06, half height 0.025, centre 0.14 above the feet; three
  spheres approximate it) along the 3D step, collects the hit normals and projects the remaining
  step off them; the push-out applies only when touching at the start (fraction < 0.001); the
  vertical part of the slide result is discarded and the ground probe sets y. The move2 landing
  now matches within 0.25 mm.
- **Grounded v.y on slopes (0x66ab90 at 0x66bb75, recovered)**: after a walkable ground hit
  (v.y = 0), k = -(n.x v.x + n.z v.z) / n.y with the probe's hit normal (the face; edges report the
  touched face, port: CollisionWorld sweep tie-break); if k < 0 a ray from 0.5 * scale above the
  next horizontal position (pos + v dt) down to 1.5 k dt (0x1222210) below it must hit, then
  v.y = k. Traced: -0.015319 / -0.016646 on the moss faces, equal in the port for both movement
  runs (velocities equal until the enemy contact / the landing shift).
- **Moving support (0x66ab90 around 0x66bd80, ported)**: a probe hit on a body of motion type 1
  or 2 sets +0x1b8 (the probe then reaches 0.2 m further) and stores the body's linear velocity at
  +0x1a0; with nonzero linear (0xca5f50) or angular (0xca6080) velocity the hero's x/z follow the
  body's rigid motion of this update around its centre (0xca5dd0); y comes from the probe. Port:
  the body's change of transform this update (CollisionWorld::bodyDelta) applied to x/z.
- **Teetering walls (0x6738f0, ported)**: `teetering_wall` placements (type 0x1b81, group
  0x400000) are thin boxes of half extents scale * (0.5, 0.25, 0.05). For each wall the feet
  touch: d = local z of the new position; if -0.05 < d < 0, s = dot(-n, stick direction, or the
  facing when no stick); s > 0 projects the position onto the wall plane; s > 0.9 stops the hero
  (velocity 0, ground state +0x14c = 2).
- **Drop-edge guard (0x66eb30, ported)**: three samples at 0.5 * scale height: 0.1 ahead and the
  same rotated 90 degrees left and right; each: a ray check, then a sphere (r 0.02) cast 5 m down;
  the first sample without ground is the edge sample. A sphere (r 0.01) cast 0.02 below the feet
  from the edge sample back to the start finds the edge face; s = hit - u * (0.04 / dot(u, n)).
  A new position past s is projected along the edge tangent; the hero stops (ground state 2) when
  standing still or when dot(dir, n) >= 0.9. Not done in states {12, 37, 11, 57}.
  `teetering_collision` (type 0x1b80, NoTeetering) also marks edges: not ported.
- **Teeter state (0x13, 0x4d2fd0, ported)**: Walk and Idle enter it when ground state == 2. A
  press jumps; after 0.2 s, if dot(teeter direction, stick) < 0.7, a ray 0.1 ahead with ground
  within 5 m lets the hero leave: ground movement, then Walk.
- **Verified (run move3)**: frames 7650-8183 equal, including the teetering-wall stop at 7662,
  Teeter from 7663, the hold, the exit at 8162 and the slide along the wall. Residual at 8183:
  at a corner where two teetering walls meet, the port's drop-edge guard fires because one wall
  misses the sample cast by 5 mm (perhaps a box margin in the original's shapes; open).

## Physics queries (2026-10-13)
The hero's queries run on the ported physics library (physics.md, "Casts and queries"): wall
capsule = module capsule query, ground probe / drop-edge samples / edge face = sphere query,
slope / teeter / edge way checks = ray query. The port's earlier ceiling cast while rising (a
foot sphere moving up) was a stand-in and is gone: the original runs the ground probe every
update. Ceilings while rising (2026-10-14, from code): the wall capsule's slide 0x66dd40 meets a
ceiling as a hit whose normal y < -0.7 (0x122223c): it sets hero +0x488 and cuts v.y to the
ceiling's own vertical speed when faster (0 for the level; a keyframed / active body's velocity
y, motion 1 / 2); the position stops through the slide itself (the capsule's move includes the
vertical part). Verified 2026-10-14 (run ceil4, script ceiling.txt: walk left under the overhang
moss_cuboid_a40 beside the forest_castle start, ceiling ~0.44 m above the ground, and jump twice):
`heroreplay` from frame 4210 / 4300 meets the ceiling on the same update as the original (4234,
4577), v.y = 0 there, the same sideways push-out (-0.0026 / -0.0055 m in x), contact heights
within 0.7 / 2.9 mm, then the normal fall; the whole 290-update replay stays within 6.8 mm. (A
replay started from the snapshot at 4555 differs only because the snapshot drops the walk's
remaining speed.) Replays unchanged (move1 to 6115, move2 to 5920, move3 to
8183; `.analysis/tools/herocmp.py ORIGINAL PORT` lists the first difference).

## Port status

`src/game/Hero*`: Idle/Walk/Jump/Fall/Land/Hover with the rules above at 60 Hz, on Beacon's
collision world, with the 3D collide-and-slide, slope v.y, moving support (0.2 m probe mode),
teetering walls, the drop-edge guard and Teeter. Not ported: the Slide state, water, damage, the
carry push (no carrying yet), teetering_collision edges, hard landing.

## Slide (recovered 2026-10-15; decompiles `evidence/decomp/slide.c`, `slide2.c`; run roof_slide1)

- **Slide surfaces**: the ground probe's hit on a body whose surface is flagged IsSlope (0xa5a930
  on the body's user data) with normal n: walkable needs n.y > cos(SlopeMaxAngle 80); a slide
  needs n.y < cos(SlopeMinAngle 15) (cos(SlopeMinAngleOnSliding 3) while +0x14c is already 3;
  the BirdBoss variants in that fight) and |d| > 0.01 with d = (0,-1,0) - n (n . (0,-1,0)): then
  +0x14c = 3, +0x320 = n, +0x330 = d / |d| (0x66ab90 at 0x66b98f..0x66bae3). The angle
  conversion is `_FSin(x, 1)`, a cosine. A slope object of class 0x13f6 overrides the
  acceleration (+0x69, +0x34c): not ported (none in ROOFTOPS).
- **Ground probe**: reaches 0.2 m further when +0x14c == 3 (as on a moving support, flag
  +0x1b8); a hit counts only on a body of motion type < 3 and while v.y <= FLT_EPSILON or v.y <=
  the body's own v.y (0x66b87e): a jump rises through a step's height without landing (run
  roof_slide1 update 2920: the port had landed while rising). The downhill rule (v.y = k) applies
  on slides too (traced v.y -0.140 entering).
- **Transitions to Slide (7)**: Idle after the airborne check; Walk and Teeter unless a jump was
  pressed; Jump, Fall and Hover before landing; Land after 0.1 s (0x11f207c) unless the stick
  went to Walk after 0.06 s. (Punch 0x4d0964 / 0x4d0990 and Walk's 0x13 transition: not ported.)
- **Enter 0x4d8190**: facing . horizontal d < 0: backwards (+0x44), clip slide_belly, else
  waterslide (mode 4: always slide_belly); the state's velocity +0x50 = v - n (v . n), minus its
  part up along d.
- **Update 0x4d8530**: on the slope the movement 0x688d60: face along the horizontal d (reversed
  when backwards); v -= n (v . n); s = v - d (v . d); v += dt (-SlopeSideFriction s +
  SlopeAccel (-d.y) d + SlopeSideControl (stick - d (stick . d))) (stick = hero +0x110 / +0x114);
  the hero's velocity = v; a jump press (unless +0x2a5) -> Jump (machine +0x80 = 1). Airborne:
  time +0x60 += dt; > 0.4 s: clip jump_01_fall, Fall; a press within JumpableTimeAfterFalling
  (tuning +0x810) jumps. On flat ground: v = 0.9 (v.x, 0, v.z) each update; after 0.2 s
  waterslide -> landing_after_slide_front, slide_belly -> landing_after_slide_back; a press ->
  Jump; stick after 0.4 s -> Walk (0x6882a0); else after 0.3 s, once the clip has played
  (0x694fd0, the current clip), Idle. Exit 0x4d8510 sends event 0x38e1d2d3 to the hero (not
  ported).
- **Verified (run roof_slide1)**: the velocity law reproduces the original's hero velocity to 4
  decimals on every update without a facet change; the whole slide matches within 2-7 cm, the
  stop 1.1 cm (the 2 cm come from the hover before it, see below).
- **Collide-and-slide, contact at the start on the lower cap** (0x66dd40): a hit at fraction
  < 0.001 below the capsule's straight part nudges the capsule 1 mm (0x12210e0) horizontally away
  from the contact (from the cap's centre). Without it the port stuck on the slide's rail joints.
  The routine also skips hits on bodies listed at hero +0x460..+0x468 and on bodies of motion
  type >= 3 (not ported).
- Open: in run roof_slide1 the hover against the fourth step's wall loses one update of x
  movement in the original at 2947 (capsule clearing the edge), not in the port (2.2 cm).

## Player rig / camera (0xa61950, VrPlayer MoveModeZ)

The VR player's head sits on a rig (start: the level's vr_player_locator) that follows the hero
along world Z only. Free area ahead of the rig: far = FreeAreaFar + x^2 * FreeAreaCurveParamFar,
near = FreeAreaNear - x^2 * FreeAreaCurveParamNear (x = hero's sideways offset). Without
CanGoBack the hero's z is clamped to rig.z - near first. Outside the area: v = SmoothParam *
((hero.z - rig.z) + edge) / dt with edge = far (hero too far) or near, clamped to MaxSpeed and
MaxAccel (forward) or the Back variants, then rig.z += v dt; inside: no motion, speed kept.
VrPlayer parameter offsets: `evidence/vrplayer_params.tsv` (binder 0x2d16a0, users in
`decomp/vrplayer`). Port: `src/game/PlayerRig`. Other modes (AutoScrollX ScrollSpeed, the
MoveAlways flag, tv_camera/follow_camera objects, vr_player_stopper/warp points): open.

## Open

Slopes in 0x66ab90 (lines 560-1480), Fall/Hover exits, punch/spin attack, damage, water, all
gadget states; nested parameter groups; the camera basis the stick is mapped with in VR.

## Punch (recovered 2026-10-13; decompiles `evidence/decomp/combat`, `punchreq`)

- Input flags (0x667b20): +0x11d cross pressed, +0x11e cross held, +0x11f square pressed,
  +0x120 square held, +0x11c no stick.
- **Start**: the main state machine's post-update hook (0x684950, the machine object from
  0x66dd30): time since the last punch +0xb4 += dt; a kept press (+0xbd) is dropped outside
  states 8, 9, 51. On square pressed (or kept), when +0xb4 > NonCancelableTime (0.25), the state
  is in the table at 0x1227044 {1, 2, 27, 3, 4, 8, 5, 6, 9, 48, 51}, the hero is grounded and not
  in Jump (in the air only state 15) and not in water: +0xb4 = 0, hand +0xb0 = (+0xb0 + 1) mod 2,
  +0xbe = +0xbd, request 8 (0x33 / 0x20 in other modes) through 0x6a7370; it switches in the
  next update (requests: first wins, 0x6a7350).
- **Enter (0x4cf690)**: clip by hand and stance: right (hand 1) attack_01 / punch_static_right
  (no stick), left attack_02 / punch_static_left (water: underwater_attackR/L; stacking:
  stacking_stand_attack_R/L); +0x48 = airborne and not at an edge, +0x49 = at an edge; attack
  window 0x674a50(CollisionStartTime, + CollisionTime, CollisionRadius, right, left); +0xbd = 0.
- **Update (0x4cfcf0)**: ground movement (0x683ed0) or air movement (0x684580, not at an edge);
  a press before NonCancelableTime keeps the next punch (+0xbd); jump after 0.07 s (or with +0xbe)
  -> Jump; after AnimCancelableTime (0.3): edge -> Teetering, landed from an air start -> Land,
  stick -> Walk (0x6882a0), +0x4a and grounded -> Idle (+0x4a is cleared on release and never
  set by the enter; start value unknown, port: off); clip played -> Idle / Fall / Teetering.
  Exit 0x4cfce0 closes the attack window.
- **Attack window (0x66a730, after the machine)**: time += dt; until the end: hand positions
  (0x69a410 hand_R joint at model +0x24, 0x69a360 hand_L at +0x20, bound by name in 0x695790);
  before the start only recorded; from the start each active hand sweeps a sphere of the radius
  from its last place to the new one (0x674c80, which also records it). Hits call the target's
  virtual +0x210 with type 1 (2 with +0x298), position, direction = target - hero (horizontal,
  normalised), damage 1 (+0x50). Swim attacks use offsets from the hero instead (0xa410/0xa6d0).
- **Ported**: Punch state, controller, window, hand tracks from the clips (`beacon-level punch`
  prints them); the sweep is the module's sphere query (self 0x20000, target 0x91041a0f minus the
  hero's bits) against the enemies' physics bodies and the level (physics.md, MupRootDynamics).
  Not ported: jump punch (9), charge attack, water and stacking variants, punch effects
  (HitStopTime is bound but never read: no hit stop, physics.md).
- **Verified (run combat5, `heroreplay` from 3200)**: Idle -> Punch on the same update (3483)
  and the punch chain from repeated presses ends on the same update (4324); the original was
  hit (LightDamage 3597-3653) in between, which the replay without enemies does not see.

## Kick (state 0x33, vtable 0x169c250, recovered 2026-10-15; decompiles `evidence/decomp/kick.c`, `punchreq`; run roof_bot5)
- Asked for instead of a punch (8) by the attack controller (0x684950) when 0x678330 finds a
  lost bot touching the enemy sensor (objects.md "Lost bot"); the request applies next update
  like the punch's.
- Enter 0x4dc480: clip "kick" (0xc61c131f), the kept press dropped, 0x674890 / 0x5280d0 (effect).
- Update 0x4dc4e0: moves like the punch (ground 0x683ed0 / air 0x684580); a press within
  NonCancelableTime is kept; after 0.4 s a grounded hero with the stick walks off (0x6882a0),
  otherwise the played clip ends it (Idle). From 0.1 s, every update until it meets something
  (+0x44): a sphere query of 0.1 m from feet + (0, 0.5 scale, 0) + 0.05 facing to + 0.15 facing
  (self 0x20000, target 0x91041a0f minus the heroes' bits); the met object's hit slot 0x210
  gets type 1 along the facing.
- Verified on roof_bot5: square at 3320 -> Kick at 3321, the bot hit at 3327 (the port: 7 updates
  too).

## Hover laser (0x4cd6b0, Hover state vtable 0x169b590 slot +0x20; recovered 2026-10-15)
Every update in Hover, for each toe (toe_left / toe_right, bound in 0x695790 at +0x30 / +0x34,
positions 0x69a890 / 0x69a940): a ray from the toe 30 m straight down (0x11f24e0), target mask
0xd804191f; the object owning the body it meets (two object maps, 0x6084f0 and 0x99ef30) gets a
hit through +0x210: type 4, position = the hit point, direction (0, -1, 0), damage 1. The enter
(0x4cc440) also creates two `mup_hero_hover_laser` objects (the beams' looks; not ported).
Targets limit themselves (treasure box HoverAttackable; enemies: burned, enemies.md).

## Damage (recovered 2026-10-15; decompiles `evidence/decomp/herodmg`, `herodmg2`, `herodmg3`; runs combat5, roof_g5)
- **Hit intake 0x66faf0** (the hero's hit slot; hit record {+0x4 type, +0x10 source position,
  +0x20 direction source -> hero}): nothing while invincible (hero +0x316, set with a time by
  0x676b10: +0x318 elapsed, +0x31c duration) except types 0xf / 0x15; machine flags +0x108
  (damageable), +0x109 (light damage allowed), +0x100 (blocks; not ported). By type: 0x14 / 0x37 /
  0x47 / 0x53 -> DeathOnSpike (0x3b) when +0x108; 0x12 / 0x2e -> LightDamage (0xc) when +0x108 and
  +0x109; any type not in the switch -> 0x672270: Damage (0xb) when +0x108 (in water +0x2b0:
  DeathInWater 0x37, not ported); the record is copied to hero +0x198; the state is requested
  with force (0x6a7370(m, id, 1)), so it wins over the current state's own requests and switches at
  the end of the next update. Other cases (bosses, traps) not ported.
- Sources: enemy_standard's attack 0x2e, other basic kinds 0x19, the spiky kinds 0x53
  (enemies.md "Attack on the hero"); the spitter's stone 0x19 (objects.md "Spitter").
- **LightDamage (vtable 0x169b660: enter 0x4d0a50, update 0x4d0de0, exit 0x4d0d80)**: enter clears
  +0x109 and the horizontal velocity; the clip by where the hit came from: a = atan2(local z,
  local x) of -direction (horizontal, unit) in the hero's frame, + pi/4, taken to [0, 2 pi) after
  adding pi: < pi/2 damage_left_light, < pi forward, < 3 pi/2 right, else damage_backward_light (a
  hit from the front plays backward). Update: Idle when the clip has played; grounded after 0.3 s
  +0x109 again; grounded after 0.4 s with the stick Walk (0x6882a0); the horizontal velocity = the
  clip's root travel over the update (0x695020) / dt, the vertical kept. Exit: +0x109 on, velocity
  zero. The facing stays.
  **Verified (run combat5, `heroreplay` with BEACON_REPLAY_HIT=3597:0x2e)**: the velocity equals
  the original's on every update to 1e-6 (e.g. 3600 (-0.738155, 0, -0.003716)), LightDamage
  3597-3652, Idle 3653, the next punch 3655 - all as the original. The positions differ by a
  constant 7.2 cm gathered in 3596-3598: the original moves 1.2 / 2.3 / 2.7 cm more, away from the
  enemy (the contact push of moving bodies, 0x66e950; not ported).
- **Damage (vtable 0x169b620: enter 0x4cff50, update 0x4d0780, exit 0x4d0740)**: enter clears
  +0x108; weight +0x5c (types 12, 31, 32, 34, 38, 39, 43, 47, 49, 50 heavy, 59, 62-64 light, 9
  middle, other types keep the last weight; the state object's start value taken as light,
  **[hypothesis]**); dies +0x58 unless the type is one of 1, 2, 3, 5, 6, 0x11, 0x28-0x2a, 0x32-0x34,
  0x3b, 0x3e, 0x40, 0x42, 0x4d (dying also sets +0x1b8 and sound 0x3318d318, else 0x8173c031);
  d = -direction (horizontal, unit; a random direction when zero, not ported); with d in front of
  the hero (local z >= 0) damage_backward_in / _loop / _out and the hero faces d, else
  damage_forward_* facing -d; velocity = -d DamageVelocityXZ + (0, DamageVelocityY, 0) (other
  type-specific pairs not ported; 0x53 no vertical). Update: on the ground the velocity is zeroed;
  in -> loop; after the down time (Light / Middle / HeavyDamageDownTime by weight) the death routine
  (dies) or the out clip; out: +0x108 again after InvincibleTimeAfterKnockDown + 0.5 s, Idle when
  played (or Walk with the stick after 0.5 s), invincible until that time when leaving; every
  update the clip's root travel moves the hero directly. Exit: +0x108 on (collision back when it
  did not die). Not yet seen in a capture (no stone hit captured, see objects.md).
- **DeathOnSpike (0x3b, vtable 0x169c160: enter 0x4db660, update 0x4db720, exit 0x4db6f0)**: enter
  0x686550 (+0x108 off, +0x1b8 on, the carried object dropped), the foot body meets nothing
  (0x66fa60 mode 2: self / target 0), death_spikes (0xf511719), velocity zero; update: the clip
  with its root travel; when played the model is hidden and ef_hero_death_spikes (0xc2cbd863)
  appears at the hero (effect not ported); 0.4 s later the death routine. Run roof_g5: the spiky's
  0x53 request at 3604 during LightDamage, DeathOnSpike 3605-3697, the death routine's request on
  the second machine (state 0x51) visible at 3697; the port (`heroreplay` with
  BEACON_REPLAY_HIT=3605:0x53) dies in update 3696.
- **Death routine 0x671f80**: telemetry ("enemey" / "fall"), the carried objects dropped, sound,
  0x997360, the second machine (+0x88 block +0x18) asked for 0x51 when idle (0x69d6e0), the main
  machine reset (0x688730), +0x2f2 cleared. Run roof_g5: main machine state 0 from 3698, second
  machine 0x5c; at 3780 the hero stands (hidden) 0.436 m above (2.063, 0.386, -13.582), falls from
  ~3850 (v.y -1.65) and lands at 3863 (Land), Idle 3909. The same 0x51 -> 0x5c sequence brings
  back a hero teleported away from the VR player (runs roof_spit4 / roof_spit5: no stone hit; the
  hero was warped back at 3764 / 3828 -> 3956). See "Respawn".
- The punch request (0x684950) only comes from the states of the table at 0x1227044 (fixed in the
  port 2026-10-15: it let damaged, teetering and sliding heroes punch).

## Respawn (decoded and ported 2026-10-15; decompiles `evidence/decomp/respawn`, `vrwarp`; runs roof_g5, roof_g6)
- Second machine (hero +0x88 block +0x18; states registered by 0x69b870): 0x51 "Vanish" (state
  vtable 0x16b23e0: enter 0x69da60 keeps the position at +0x100 and starts the model's vanish
  0x699870; update 0x69dab0: once the model is gone (+0x18 == 3) hidden, collision off, the main
  machine reset (0x676de0); then 0x5c DefaultRespawn when 0x69db90 allows, 0x54 when hero +0x78
  is clear), 0x5c "DefaultRespawn" (vtable 0x16b2660), and 0x53..0x6b others (not followed). The
  hit intake ignores every hit while this machine has a state.
- **DefaultRespawn** enter 0x6a1170: 0x69d700 (the VR warp, below); hidden, collision off (0x66fa60
  mode 2), the main machine asked for 0 (no state); target = the hero's respawn point (0x674800)
  + (0, FlySpawnSomerSaultHeight x Scale, 0) (2.0 x 0.22 = 0.44). Update 0x6a1260: phase 0 waits
  while the VR player warps (+0x498, 0xa641d0), then: position = target, velocity 0, facing
  (0, 0, -1) (0x66db40, constant 0x1229f00), the model shown, clip "respawn" (0x9f77283f),
  0x678c70, 0xa64220(vr, 1); phase 1: after RespawnEffectTime (0.8 s) respawn_somersault_out;
  phase 2: when it has played respawn_somersault_out_loop, collision on, invincible
  InvincibleTimeAfterRespawn (1.0); phase 3: on the ground the main machine Land (5; Idle 1 in
  water), invincible again, the second machine idle. The hero is held (no gravity) until phase 3.
- **The VR warp** 0x69d700: no warp when the respawn point lies in the respawn area in front of
  the VR player (d = point - VR position: d.z <= RespawnAreaZ 0.5, horizontal |d| >=
  RespawnAreaRadius 0.3 and |atan2(d.x, d.z)| >= RespawnAreaAngle 60 deg) or the second machine's
  +0xa0; else (VR move mode 0) 0xa63d00(WarpWaitTimeForRespawn 0.3, WarpFadeOutTime 0.3,
  WarpFadeBlackTime 0.2, WarpFadeInTime 0.3, target = the VR position with z = point.z +
  FreeAreaFar - 0.1; a vr_player_stopper ahead clamps the target - stoppers are not ported).
  0xa60560 (VR player +0x498, timer +0x480): 1 wait, 2 fade out (then the rig moves to the target;
  with the room flag, unused here, the room resets), 3 black (then the marker's 0x7ca390), 4 fade
  in, 0. Each step ends when its time is exceeded; **[fit]** the update after the rig moved does
  not count (run roof_g6: +0x480 0 at 3733 and 3734). The VR player updates after the hero.
- **Verified (run roof_g6, hero state, second machine, VR +0x498 / +0x480 traced; port
  `scriptwalk` with BEACON_SCRIPT_HIT=3604:0x53)**: DeathOnSpike 3605, death 3696/3697, Vanish
  3697, DefaultRespawn 3698, warp 1/2/3/4/0 at 3698/3715/3733/3747/3765, the hero placed at
  (2.063, 0.822, -13.582) in 3766, falling from 3845, on respawn_marker1's top (y 0.386) at 3862,
  Land 3863, Idle 3909 - all equal in the port. In that run the rig stayed at
  vr_player_stopper_01 (z 2.945) through the warp (the stopper clamp); the port's rig, without
  stoppers, moves to -12.58.
- Respawn point: set by the respawn markers (objects.md "Respawn markers"); **[hypothesis]** the
  hero's start until one is activated. Not ported: Vanish's wait for the model's vanish effect (one
  update in the port), the room reset, the effects (respawn ghost, marker effects), the other
  respawn kinds (QuickRespawn, water), the death_collision triggers (a fall below the level calls
  the death routine in the port).
