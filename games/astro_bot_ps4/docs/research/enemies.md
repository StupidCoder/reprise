# Enemies (research started 2026-10-13)

From the user's executable and data only. Decompiles in `evidence/decomp/enemy`, `eproc`, `defeat`.
This file records the structure found, what is ported and how it was checked.

## Data

- Classes in forest_castle: 22 `enemy_standard` (model mup_ch_enemy_basic or _spiky), 16
  `enemy_spiky`, 9 `enemy_ace_spiky`, 25 `enemy_locator_ground` (landing / waypoint locators),
  pole_enemy, centipede_enemy, ring_head_enemy, enemy_spinner, enemy_balloon_spitter.
- Placement attributes: common block (0x606890, every enemy): SpawnTrigger, ChaseTrigger,
  EndTrigger (links), LaunchDelayTime, ChaseDelayTime, LifeTime, NoChase, Rewards (none,
  Coin(small|medium|large|special)). Basic-enemy block (0x2d77f0): LaunchType (Stand, Parachute,
  Jumpup, Panic), Hitpoints, LaunchJumpupVector, LaunchJumpHeight, LaunchJumpGravity,
  LaunchJumpEnd (link to a locator), LaunchJumpThroughWall, LaunchStandingTime, RopeAction,
  InvisibleStandby, ColorType, AttachmentType, ParachuteColor/Type, NoDiscoveryAction,
  CanWalkByWater, BarrelAction, NoPath, RopeJumpDownHeight.
- Tuning: `property/basic_enemies.xml`, one block per kind (EnemyStandard, EnemySpiky, ...):
  default chase / end trigger cylinders (r 3 / 4 m, half height 3), collision bindings,
  SpeedScale (x root motion), AngularVelocity 240, AngleToTurn 135, Goal radius/height/angle,
  Charge/Attack/CoolDown times (spiky 0.6/0.8/0.8), UnbeatableTime, DeadTime*, AttackPunch*,
  sensor geometry (GeometrySensorRadius 0.025, SensorY, SensorUp/DownHeight, ...), Cruising*
  (radius 0.5, time spans), spiky Standby/Chase times.
- Animations: `characters/anim/mup_ch_enemy_basic_anim_*.anim` (idle, walk, chase, discover,
  turn_alerted, attack_start/end, damage_hit, jump_*, landing, spawn*, ...); behaviour processes
  name clips by FNV-1a hash (0xc301cf93 idle, 0x4ba70b3d chase, 0x6aa25568 falling).

## Objects

- MupEnemy base constructor 0x606e90 (vtable 0x16acd20): kind id +0x1cc8 (0x7d2..0x7d8 ..),
  common attributes at +0x1cd0.
- Common trigger layer 0x60a520 (via 0x608510 -> virtual +0x310, every enemy): SpawnTrigger
  (+0x1cd0) fired once -> spawned (+0x24ad); time since spawn +0x24a4; at LaunchDelayTime
  (+0x2120) -> launched (+0x24ae, 0x6f0860 except some kinds); LifeTime (+0x2280) > 0 elapsed ->
  removed (+0x30). Without ChaseTrigger / EndTrigger links the enemy owns default sensor objects
  (+0x2458 chase, +0x2460 end) posed at the enemy (+0x2440) with radius / half height from the
  kind's tuning (DefaultChaseTrigger, DefaultEndTrigger); the end trigger "on" accumulates +0x24a8
  towards +0x24a0. 0x609900 collects the heroes for which virtual +0x2f8 holds (targets).
- Basic enemy (`MupBasicEnemy`, constructor 0x2d9b20, vtable 0x1685908): character /
  animation component +0x2570 (vtable 0x1685cf8, engine 0x7dc9d0), kind tuning pointer +0x2ad0,
  basic attributes +0x2ae0, process tree root +0x3a70, position +0x2670 (MupRootDynamics,
  virtual +0x58 / +0x70).
- Update 0x2dec80: animation component step 0x7dfe50, timers, 0x2df3e0 (collision filter
  bits), virtual +0x358 (0x2e1d80) and +0x3b8 (0x2e2c60, contact classification from the
  character's contact flags), the process tree (0xb8a0 on +0x3a70), a kill plane below the
  placement (global tuning +0xa30), virtual +0x3c0.
- Root process (0x2dbf00, by kind): default vtable 0x1687740, start 0x307230 adds the behaviour
  process (vtable 0x16876c8, all channels) and, with flag +0x3390, a second one (0x1686990).

## Process framework (engine "SlotProcess", 0xb180 / 0xb410 / 0xb8a0 / 0xb970)

Generic behaviour tree used by enemies (and other objects): a process has a name ("Basic"),
a channel mask (+0x8) and an active mask (+0xc), started / finished flags (+0x20 / +0x21) and a
child list (+0x30).
- Add child 0xb410(parent, child, channels): appended, ids counted on the root.
- Run 0xb8a0(p, owner): first run calls start (slot +0x28 -> +0x50); children run by 0xb970 in
  order: a child returning 2 (running) takes its channels, so later children that share one are
  interrupted (their active bits cleared, stopped 0xbd00 when none is left); 3 = finished ->
  stopped; finished children are removed; then update (slot +0x20 -> +0x60, counter +0x78).
- Messages (slot +0x48, base 0x255b30) are forwarded to children of kind -0x28d07b23.
- Basic brain process (vtable 0x16865d0): message 0x2f19d0, start 0x2edfc0 (chooses the launch:
  Stand / Parachute / Jumpup with a ballistic jump from LaunchJumpupVector, LaunchJumpHeight and
  gravity towards the LaunchJumpEnd locator; brain state +0x80 = 1, 4, 5 (waypoints), 7
  falling, 8, 9, 10), stop 0x2f1910, update 0x2f1dd0.
- Animation process (vtable 0x1686738): plays a clip by hash through enemy virtual +0x348
  (-> +0x350); others 0x1686288, 0x1686210, 0x1686120, 0x1686198, 0x1685e50 (falling),
  0x1686030 / 0x1685f40 (moves to a point), 0x167d2a0.

### Behaviour states (labelled processes, debug names written into the enemy's state string)

| state | vtable | start | update | notes |
|---|---|---|---|---|
| Standby | 0x16876c8 | 0x306170 | 0x3069c0 | first process; idle clip or kind-specific; reacts to event bits +0x10 (2: launch -> 0x1687308, kind 0x7d6: 0x1688f40) |
| Launch | 0x16873f8 | 0x300420 | 0x3010c0 | by LaunchType (+0x80): Stand (idle 0xc301cf93), Parachute (falling 0x6aa25568), Jumpup: trajectory solver 0xba9310(height LaunchJumpHeight, gravity, from, LaunchJumpEnd locator or +0x3bc0) -> clip `spawn` if the kind has it else `discover` (0x2e52f0), arc mover 0x2e5a40, then `landing` (0x2e7b00) |
| Active | 0x1686db0 | 0x2fa670 | 0x2faab0 | |
| Active (start detail) | | | | target hero 0x302910 (+0x80); children in priority order on guard channels 0x4000002 (target down: 0x2fe1a0), 0x8000002 (0x2fdb00), 0x20000002 (0x2fcd20 with tuning +0x2920), 0x10000002 (0x1686d38), 0x80000002 (0x16874e8), each followed by 0x2fcb80 on channel 4 and the terminator 0x167d2a0 on 0xf |
| Active (variant) | 0x1687470 | 0x301a30 | 0x302af0 | |
| Cruising | 0x1686e28 / 0x1687650 | 0x303aa0 | 0x2fb060 / 0x3050d0 | stop 0x3050b0, messages 0x3060d0 |
| Reactive | 0x1687380 | 0x2ffaa0 | 0x300020 | |

Process vtable layout: +0x48 message, +0x50 start, +0x58 stop, +0x60 update (returns 2 running,
3 finished); 0xb5a0 replaces a process by another (transition).

## Observed (traces)

- Run move1: the hero was hit at frame 6115 near the start (FC_S00 `enemy_standard_A_01/_02`:
  InvisibleStandby, LaunchType Jumpup from y -0.7 to the locators at z 48.2 / 48.7, Rewards
  Coin(small)); the hero's coin counter later rose twice 4 frames apart while standing (dropped
  reward coins flying to the hero, CoinDropParameters AutoGetSpawnDelay 0.08 s).

- Run enemy1 (move.txt, FIND every 120 updates, `tools/enemylog.py`): both FC_S00 enemies sit in
  Standby at y -0.7 until ~5500, then stand on their locators (y -0.042) under Cruising
  (0x1687650 -> brain 0x16865d0 -> 0x1686210, small moves around the locator); from ~5640 they
  switch to Active (0x1687470, children 0x1686378 / 0x1687128 / 0x16871a0) and follow the hero
  (frames 5640-6480, small hops to y 0.18 near 6360), then return to Cruising by 6600 and wander
  back towards their locators.

## Recovered details (2026-10-13, second pass)

### Framework semantics (exact, from 0xb970 assembly)
Children run in list order with three masks: `owned` (channels of children that returned 1 this
update), `lent` (channels of children that returned 2) and a per-channel lender table. A child
whose channels meet `owned` is skipped (and re-arms lenders sharing its channels); otherwise any
lender of a channel it uses loses that channel and is stopped when it has none left; then the
child runs: 3 -> stopped (removed in the sweep after the loop), 2 -> lends its channels, 1 -> owns
them. 0xb5a0 "follow" inserts a behaviour after the caller (behind the caller's earlier follows,
creator id +0x1c) with the caller's channels; 0xbd70 broadcasts event bit n to a subtree; 0xbe50
"channels free"; 0xbe90 child count. Port: `game/ai/Behavior.hpp`.

### Leaves
Terminator 0x167d2a0 (done at once), Wait 0x167ceb8 (running while the time before this update
was > 0), Race 0x1686a80 (parts on channels 1<<i, done when the first part ends), PlayOnce
0x16863f0 (hash +0x80, body mode +0x84, flags +0x88..+0x8a, speed +0x8c; mode 1 = ground or air
by the grounded test, kept in step), PlayLoop 0x1686738 (yields), PlayGrounded 0x1686378 (a
PlayOnce child on channel 1), TurnOverTime 0x1685fb8 (slerp eased (1 - cos(pi t)) / 2), the
trigger watcher 0x1687038 (hit -> event 1 to the tree; reaction request +0x3d10 -> event 2).
Clip play (virtual +0x348 -> 0x2df9d0) sets the body mode before looking the clip up; a missing
clip leaves the body mode set and the player stopped.

### Standby / Launch / Arc (ported, verified on run enemy2)
- Standby: with InvisibleStandby (basic attr +0x3430) or LaunchType Parachute no clip and no
  watcher (body mode stays 1, traced); otherwise idle (mode 0x10) + watcher. Update: event 0 ->
  0x16875d8, event 1 -> 0x1687308 then a new Standby, event 2 -> Reactive 0x1687380 then Standby,
  launch ready (virtual +0x2f0(0) = +0x24ae) -> Launch(LaunchType) then Active follow.
- Common trigger layer: SpawnTrigger fires -> launched after LaunchDelayTime (enemy_standard_A_02:
  0.2 s = 12 updates later, traced and reproduced).
- Jumpup solver 0xba9310 (traced exactly): v_up = sqrt(-2 g h), rise = -v_up / g, fall =
  sqrt(max(0, 2 min(dy - h, 0) / g)), horizontal = distance / (rise + fall).
- Arc 0x16860a8: LaunchJumpGravity replaces the body gravity (+0x25d0; the swap's restore never
  happens since the saved "unset" FLT_MAX is not negative); children: spawn_start (mode 0x10) on
  0x800000f, TurnOverTime on 4, Race{spawn_up, Wait(rest)} / spawn_up_loop / spawn_down on
  0x10000001, Wait(rest) on 2, terminators, spawn_landing on 0x20000001; rest = flight -
  len(spawn_down). Velocity (vx, v0y + g (elapsed - dt), vz) while channel 0x8000000 is free and
  0x10000000 held: exactly rise + fall.
- Body integration (traced): v.y += g dt, then p += v dt (velocity printed after gravity).
- Landing (traced): grounded when 0 <= ground distance <= ThresholdOfGround (0.03); body mode 2,
  velocity 0, snapped to the floor one update later.
- Verified: enemy_standard_A_01 equals the original in free flight (positions and velocities to
  1e-6) until it clips a terrain edge at frame 4251; the original's physics response (a push and
  a slow return to the commanded velocity) is not reproduced (port: sweep and slide of a
  RootRadius sphere); it lands 0.2 m from the original. A_02: same launch frame, flight within
  5 cm.

### Clips and locomotion
- Definition (from code, 2026-10-13): the class factory passes a definition name to the
  constructor (0x2da4d0: kind 0x7d2 with "mup_ch_enemy_basic", stored at +0x3a88); the spawn
  0x2da770 builds the model object from it (0x7722f0), and from the placement's Odx only when
  the name is empty. Clips are looked up by name hash in that object's animation map (+0x270,
  0xbb13d0). The placement's Odx (mup_ch_enemy_spiky for FC_S00) is not used. (Earlier the port
  looked up the class definition first and the placement's second, chosen from a capture.)
- Walk speed is root motion: the root joint (hash 0x45279a6e) of walk moves 0.0976 m per loop.
  The traced speed ramps up over ~12 updates at the start of a walk (clip blend: open).
- Body: MupRootDynamics is ported onto the physics library (physics.md, "MupRootDynamics
  (ported)"); the enemies' slide, ground ray and pair separation stand-ins are gone.
- Body controller 0x7e20e0 (MupRootDynamics): ground probe = cast from above the feet to 1 m
  (+0x44) below them, radius +0x48, ground distance +0x4c = feet - hit (-1 none, -2 in modes
  1/0x10/8); moving support body +0x4b8. Physics-engine body API: 0xca5f20 position, 0xca5f50
  linear velocity, 0xca6080 angular, 0xca6380/0xca6390 filter, 0xca5c70 motion type; casts:
  query 0xc22cb0 (from +0x20, to +0x30, radius +0x40, masks +0x44/+0x48) run by 0xc22e20.

### Tuning layout
Kind binder 0x2d5a90 (`evidence/basic_enemy_params.tsv`): <Enemy> group at +0xb0 (0x606bf0),
<Bind> at +0x630 (0x2d51c0: RootRadius +0xa10 default 0.05, StepRadius +0xac0, ThresholdOfGround
+0xc20, DiscoverLocomotion +0xfd0, DamageHitLocomotion +0x1070, JumpLocomotion +0x10b0,
TurnLocomotion +0x1150, FreeTransition +0x11f0); float/bool values at property +0x90 (default
+0xa0). All kinds: 0x5097c0 (EnemyStandard, EnemyBigStandard, EnemySpiky 0x92b480, ...).

### Brain ("Basic", 0x16865d0)
States 1 NoGoal, 2 MoveOnRope, 3 MoveToRope, 4 TurnTo, 5 MoveToSurface, 6 Walk, 7 Fall, 8 Jump,
9 Turn180, 10 Turn, 11 Stop, 12 Edge; a state change queues a new brain (0x2ea8c0 copies the goal)
and ends the old one. Root brain 0x2ea710 (from Cruising): +0xd0 walk clip, +0xd4/+0xd8 turn
right/left clips (TurnLocomotion false: plain "turn"), +0xdc/+0xe0 turn_180 / turn_180_R, +0xe8
goal object, +0xf4 0.075 / 0.15, +0xf8 -1, +0xfc pi, +0x100 AngleToTurn (rad), +0x104
AngleToBigTurn. Turn (10): clip turn_l / turn_r by the sign of the angle; TurnLocomotion false ->
0x1686210 (code turns at AngularVelocity, traced 240 deg/s), true -> 0x1686120 (clip rotation).
Traced on enemy_standard_A_01: Cruising uses Turn (10), Walk (6), MoveToSurface (5), Stop (11).

### Cruising / brain / Active (ported; verified on runs enemy4/enemy5)
- Cruising start 0x303aa0: home = +0x2440 (the landing point); away from home (> 0.15 m,
  0x2ead50) -> brain to home (reach 0.075, any facing), else a random point within CruisingRadius
  (reach 0.15, facing within 4 deg; drawn from the engine's global generator 0xebe680, so the port
  draws its own); then IdleGround (0x1686300) and Wait(rand(CrusingTimeSpan, CruisingTimeSpan)),
  watchers, SensorPause (0x1687ad0, ch 8). Update 0x3050d0: a target hero (0x305b40: chase trigger
  fired incl. ChaseDelayTime, nearest hero inside) -> Active(discover = !NoDiscoveryAction); all
  children done -> a new Cruising.
- Brain: reach (0x2eaee0) = horizontal distance <= +0xf4 (and |dy| <= +0xf8 / 2 when set) and
  facing within +0xfc (0x2e7510: angle between facing and goal on the ground plane; limits < 0 or
  >= pi always hold). Turn (10) re-plans once facing within AngleToTurn x 0.05 (FreeTransition,
  else pi/4); Walk (6) re-plans when the goal leaves AngleToTurn; reaching ends the brain (update
  returns 3). The start's choice between Turn and Walk (facing within AngleToTurn) is inferred,
  not decoded line by line.
- Steering 0x1686210 (start 0x2e6e70, update 0x2e6ea0 -> 0x2e6ef0): loop clip with root motion,
  turn toward the goal by at most AngularVelocity x dt (exact: 4 deg per update traced).
- Root motion: body velocity = root joint delta of the clips played with root motion, blended
  over the definition's <Transition> JointsBlendDuration (turn -> walk 0.2 s: exact linear ramp),
  times the placement scale and, after a chase clip has played, SpeedScale (+0x2610, kept).
- Active 0x1687470 for enemy_standard (start 0x301a30): Discover (0x1687218: discover clip, with
  root motion when DiscoverLocomotion; turn to the hero over DiscoverRotStart..EndFrame eased; the
  falling process 0x1685e50 on ch 1|4 then ends the turn), Chase (0x1687128: brain to the hero with
  chase / turn_alerted clips, reach GoalRadius x mean scale, GoalHeight x scale, GoalAngle; re-chase
  when the brain ends out of reach + 0.2), attack_start, Attack (0x16871a0: attack clip, attack
  flag +0x3aaf; damage shapes are switched by 0x2dfef0 only for other kinds), attack_end, each
  behind a terminator on 0xf. Update 0x302af0: all done -> a new Active (no discover); target in
  the chase or end trigger -> keep; else back to Cruising unless an attack follows the chase, a
  discover runs, the enemy is off the ground or a one-shot clip plays (player loop flag 0xbb8170).
- Default sensors (no ChaseTrigger / EndTrigger links): cylinders of the kind's
  DefaultChaseTrigger / DefaultEndTrigger Radius / HalfHeight at +0x2440 (shape: hypothesis).
- Verification (tools/enemycmp.py; the port replays the original's cruising goals and starts each
  top-level segment from the original's pose): enemy_standard_A_01 and _A_02 equal the original
  through Cruising (Turn, Walk, stop) to 0.1 mm over 170 updates, Active starts on the same update,
  Discover with its hop within 1 mm (facing 0.4-0.6 deg off at the end), the chase's first
  50 updates within 2 mm. The two enemies then push against each other (rigid bodies of the
  physics engine, 0.14-0.18 m apart): with the port's pairwise separation (contact radius = widest
  shape of the model's root physics body x scale; the engine's response is approximated) the whole
  Active sequence follows: three chase / attack cycles with attacks starting within 4-11 updates
  of the original's, positions within 2-12 cm, back to Cruising on the same update (4296).
- Alignment: watch rows hold the state before an update; the port's update F compares with row
  F + 1, and the hero position the original's objects see during update F is row F + 1.

## Port (src/game/enemy, src/game/ai/Behavior.hpp, src/app/EnemyView)
- `ai::Behavior<Owner>`: the slot-process runner (channels, Running / Waiting / Done, follow,
  broadcast, channel queries).
- `BasicEnemy`: body (modes, gravity override, ground probe, sweep-and-slide of a RootRadius
  sphere), clips (lookup by name hash, class definition first; root motion with definition
  blends; SpeedScale after chase), common triggers (spawn / launch delay, chase incl.
  ChaseDelayTime, end; default cylinders), behaviours: Root, Standby, TriggerWatch, Launch (Jumpup;
  Stand ends at once), LaunchArc, Cruising, Brain (NoGoal / Turn / Walk), SteerTo, IdleGround,
  SensorPause, Active, Discover, Chase, Attack, FallUntilGrounded and the leaves.
- `EnemySystem`: enemy_standard placements (kind 0x7d2, tuning EnemyStandard), enemy-enemy
  separation. `EnemyView`: models posed by the playing clips (retargeted by joint name hash).
- Not ported: hits / death / rewards (events 0-2 hold), hero damage, the other launch types,
  rope / waypoint / jump / fall / edge brain states, other enemy kinds.

## Plan

1. (done) Process framework.
2. (done for enemy_standard) Spawn, Jumpup launch, cruising, chase, attacks.
3. Hero contact: damage to the hero, stomp / punch defeat, Rewards (dropped coins, state 3 of
   the coin machine).
4. Trace: FIND `1685908@1a90,2670,2678` (position) plus the brain state through the process tree
   (+0x3a70 -> children -> +0x80) on runs that walk into FC_S00's two enemies.

## Hit reaction and defeat (decoded 2026-10-13; decompiles `evidence/decomp/eproc`, `defeat`)

- Starting hit points: the placement attribute Hitpoints (binder 0x2d77f0; default 1, value at
  property +0xa0 -> enemy +0x2c30), copied to the current HP +0x3aa8 on spawn (0x2da770);
  virtual +0x298 (0x255db0) returns it. The same binder gives LaunchJumpHeight default 0.1 and
  LaunchJumpGravity -9.8.
- Hit reaction 0x1687308 (start 0x2fe910, update 0x2ff970):
  - virtual +0x368 (0x2e2dc0) with 0: clears +0x3ab4 and switches off the six bodies listed at
    +0x3be4..+0x3bf8 (0x7e9dc0).
  - damage 1 (hit type 5: +0x3b20); HP = max(0, HP - damage), stored at +0x3aa8.
  - throw: 0x60a8d0(AttackPunchAngularVelocity, hit direction (info +0x20), AttackPunchVector,
    rotation) -> velocity, angular velocity:
    - forward = hit direction with y = 0, normalised; when shorter than 1.19e-7 the enemy's
      facing (rotation applied to a constant axis).
    - P = AttackPunchVector (EnemyStandard: 0, 2.5, 2.0): v0 = sqrt(-2 g P.y) up,
      T = -2 v0 / g, horizontal speed |P.xz| / T; velocity = forward * that + up * v0
      (g = physics world gravity from 0xca8d70). Direction shorter than 0.1: both zero.
    - angular: axis = normalised (d.z, 0, -d.x) of the hit direction (the facing when
      |d.y| > 0.9); quaternion (axis, w = pi/4, not normalised) -> 0x32c70 (quaternion to
      Euler, rotation order 2, clamps, asin / atan2 polynomials) x speed / 45.
      Hypothesis: the port uses standard Euler formulas for order 2 until checked against a
      capture.
  - HP left (not types 4, 9, 0x13, 0x15): damage_hit (0xb1c9ef61) on channel 1,
    Wait(UnbeatableTime) on 4, invulnerability 0x1687860 (update 0x307930: clears the hit
    flags, Done) on 4, TriggerWatch on 4, then 0x2e2f30.
  - HP gone: death time = DeadTimePunch + rand x DeadTimeRandomPunch.
    - tuning +0x1010 false (EnemyStandard): set velocity and angular velocity (virtuals +0x330,
      +0x338); death clip 0x71ba3322 mode 8 on channel 1; 0x2f7fc0(0xc464e66d) (vtable
      0x1686828) on 1.
    - true: velocity only; death clip on 1|2 unless kind 0x7d3; 0x16877e8 (start 0x3078c0: sets
      the angular velocity) on 2; 0x2f7fc0 on 1. +0x1010 is Bind/DamageHitLocomotion (true for
      the basic kinds); ported 2026-10-14 and checked on runs combat5 / combat7 (physics.md,
      "Thrown flight").
    - then Wait(death time) on 4 and Terminator on all channels.
    - Rewards flag (+0x3c38): an effect from 0x60a2a0 (ef_enemy_trajectory) placed at the
      enemy, lasting 2 x death time.
    - virtual +0x2e0 (0x609c30) with the velocity and the attacker handle, virtual +0x2e8
      (0x2e3160) with the hit type.
  - update 0x2ff970: while channel 1 is busy -> Running (Done on events 0 or 1); when free:
    clear the hit flags (0x2e2300: +0x3aac -> +0x3aad), Done; HP < 1 -> event 0 to the root.
    An attached effect handle (+0x80) follows the enemy position.

### Port and verification (2026-10-13)
- Ported: hit intake (0x2e20a0: pending, types 6 / 0xd), Hitpoints, HitReaction (survive:
  damage_hit_light + UnbeatableTime + clear; defeat: throw velocity, damage_hit (mode 8),
  damage_hit_loop (0x1686828), Wait(death time), Terminator, event 0), Dead (0x16875d8: hidden,
  removed), the event 0 / 1 branches of Standby, Active, Cruising. Clip names: 0xb1c9ef61
  damage_hit_light, 0x71ba3322 damage_hit, 0xc464e66d damage_hit_loop, 0xdd3a5119 burned.
- Run combat5 (hero punches two enemy_standard): hit 7ec7 in update 3576, b959 in 3676; both
  defeated with one punch (HP 1 -> 0). Throw velocity exactly as the formula with g = -9.8:
  (1.018, 7.000, -0.961) for direction (0.727, -0.686) and AttackPunchVector (0, 2.5, 2.0).
  `enemytrace` with the hit scripted (`ID hit FRAME dx dz`): the reaction starts in the same
  update; the flight stays within 0.1-0.3 m (the original's root circles the rigid body's
  centre while spinning), apex 2.40 vs 2.45 m.
- Open: the spin. The original's body turns at exactly 60 rad/s about the world axis
  (-0.815, -0.579, 0) for that hit (likely the physics engine's limit of 1 rad per step); none
  of the six Euler orders of 0x32c70 for (axis, pi/4) gives that axis. The port keeps the
  decoded formula (hypothesis, spins faster).
- Open: the original's watch rows end 42 updates after the hit, before DeadTimePunch (0.8 s);
  the port removes the enemy after the death time (59 updates in the run). The rewards
  (+0x3c38 trajectory effect, virtuals +0x2e0 / +0x2e8), the death model effect (0x2e2a50),
  the survive path's 0x2e2f30 and the hit types 0x13/0x15 are not ported.
- Types 4 (the hero's hover laser) and 9 (fire), 0x2fea3c: the damage as usual, then `burned`
  (0xdd3a5119) looping in mode 4 (PlayLoop 0x1686738) on channel 1, Wait(DeadTimeBurned, tuning
  +0x2ea0 = 0.3) on 4, Terminator on all; no throw. Ported 2026-10-15; enemytrace with a scripted
  type-4 hit on combat5's 7ec7: Hit at 3576, Dead at 3594, in place.
- Enemy attack on the hero (combat5): b959's Attack (0x16871a0) started at 3592; the hero was
  requested LightDamage (12) at 3596, pushed 0.25 m away over ~15 updates (speed up to 1.2 m/s,
  then decaying), and returned to Idle after 56 updates. Hero hit handler: 0x66faf0 (not yet
  decoded).

### Capture-derived rules replaced by code (2026-10-13)
- Clip lookup / definition: see "Definition" above.
- Standby after the launch queues Active (discover only after a Stand or Panic launch without
  NoDiscoveryAction, 0x3069c0). Active's start (0x301a30) takes as target the nearest hero in the
  list from virtual +0x300 (0x609900: heroes for which +0x2f8(hero, chase) holds, i.e. the chase
  trigger fired and the hero is in it, 0x6092c0); without a target it adds no children, and its
  update (0x302af0) follows Cruising. Run enemy4: Launch -> Cruising on the same update as before.
- World gravity: physics world reset 0xca87e0 sets (0, -9.8, 0) from 0x1304080; SetGravity
  0xca8d30 has no callers. The main object sets the world scale 3.0 (0xca8c50, 0x123bddc).
- Still capture-derived (stand-ins until MupRootDynamics is ported, docs/physics.md): enemy-enemy
  separation, root motion along the facing.

## Sensors (decoded 2026-10-14, static; not ported)
MupRootDynamics keeps a vector of sensors (+0x338 / +0x340, entries of 0xa0; the asserts name it
m_sensors). Add 0x7de8a0(rootdyn, esi, &local position, ecx, r8, r9, xmm0, xmm1) appends
{+0x0 body = 0, +0x8 esi (target filter: 2 -> rootdyn +0x294, else +0x298), +0x10 local
position, +0x20 identity rotation, +0x30 xmm0, +0x34 xmm1, +0x38 r8 (type), +0x80 enabled = 1,
+0x81 ecx (scale by the root's scale +0x120), +0x82 hit = 0, +0x84 hit bits = 0, +0x88 r9 (mask
of body modes it runs in)} and returns its index. Accessors 0x7e9ca0 (body when hit),
0x7e9dc0 / 0x7e9e60 (set / get enabled), 0x7e9ef0, 0x7e9f80 (+0x20), 0x7ea020 (+0x30),
0x7ea0b0 (+0x34); 0x7e9bd0(rootdyn, index, mask): index < 0 -> the body's contact bits (+0x440)
& mask, else the sensor's hit bits & mask while its hit flag is set.
- Bodies (0x7deb20): type 1 -> 0xcaa220 (dims from +0x30 / +0x34 times constants, height 200),
  type 0 -> 0xcaa360; keyframed (0xca5c70 type 4), self filter 0, target from +0x8; contacts
  with the owner's own bodies switched off (0x7df980 -> 0xca9930, undone by 0x7dfcb0 / 0x7dfdc0).
- Update (0x7e0020, start of the step, unless +0x65): every enabled sensor whose +0x88 has a bit
  of the body mode +0x38 clears its hit, then type 2: a sphere sweep (0xc22e20; radius +0x34,
  self filter +0x290, target as above) from the sensor's world position along its turned offset;
  a hit with a valid body sets +0x82 and ORs the hit body's self bits (0xca6380) into +0x84.
  Other types: the trigger body's contact list (0xca6900); each contact ORs the partner's bits.
- Basic enemy (0x2dba50, from the spawn; nothing when +0x2330): four sensors at +0x3be4 A
  (type 0, target +0x294), +0x3be8 B (type 0, +0x298), +0x3bec C (type 0, +0x294), +0x3bf0 D
  (type 2 sweep, +0x298, ground mode only); +0x3bf4 / +0x3bf8 stay -1 for this class. Their
  geometry comes from the kind: SensorY (+0x3b00, times the placement's y scale), SensorUpHeight
  (+0x3bb0; a global +0x1110 / +0x11c0 pair instead when +0x3390), SensorDownHeight (+0x3c60),
  SensorDownDistance (+0x3d10), SensorDistance (+0x3dc0), SensorJumpUpDistance (+0x3e70),
  SensorEdgeDistance (+0x3f20). Read from run sens1 (every enemy_standard of forest_castle,
  scale 1.1; h = the RootCollision body frame's height 0.0963318 x scale = 0.105965, from virtual
  +0x3a0's matrix):
  - A: capsule from (0, s SensorY, 0) forward (+z) over h + SensorDistance (0.156), radius 0.01.
  - B: capsule from (0, s SensorY, SensorEdgeDistance) down over s SensorY + SensorDownHeight
    (0.555).
  - C: capsule from (0, s SensorY + SensorUpHeight, 0) forward over h + SensorJumpUpDistance.
  - D: sphere (r 0.01) swept from (0, s SensorY + Up, h + SensorDownDistance) down over
    s SensorY + Up + DownHeight (0.655).
  Entry +0x40..+0x7f is the shape's matrix (column 2 the axis, +0x70 the centre = half the length
  along it); the capsules are the library's (0xcaa360 -> 0xcb56a0, shape type 2, radius +0x34,
  half length +0x30 / 2, mass 200), motion type 4 (trigger), groups 0xffff. Self mask 0 while the
  mode is 1, 0x40 otherwise (0x7e82f0 / 0x7e7bb0; run sens2), target 0xd7a63fdf (+0x294, A and C)
  or 0x88040810 (+0x298, B). 0x7e7bb0 (end of the apply, after 0x7e71b0) puts them at the new
  root, turned by its yaw alone (0xca5e20 / 0xca5f40 set the pose); the next update reads their
  contacts of the world step in between. Hit bits seen in sens1: 1 (the hero), 0x10 (level art),
  0x1000 (another enemy).
- Virtual +0x368 (0x2e2dc0) switches all six on / off and stores the switch at +0x3ab4.
- 0x2e2c60 condenses them into +0x3bfc each update (0 unless the switch is on):
  A & 0xff7dff9f set: 1 when the contact bits (& 0xff7dff9f) are set and A & 0x1000 (an enemy),
  else 2 - (C & ~0xf ? 1 : 0). A clear: 3 when the body is not grounded (virtual +0x40),
  C clear, D set and B set; else 4 when neither C nor D is set; else (C or D set) 2 - C when
  the contact bits are set, 0 otherwise.
- Readers of +0x3bfc: the update 0x2dec80, the cruising / walk brain 0x2e8620 (which also moves
  sweep D along its planned path: +0xe4 step counter, D's offset / radius through the
  accessors, enabled only while (+0x78 & 7) == 7), 0x2edfc0, 0x2f52a0, the spiky process
  0x2fdb40, 0x303aa0, 0x308a80, 0x256840, 0x2e4090 (compares with 1..4). The fish reads D with its
  own index +0x30d0 (mask 0xff7dff9f).
- Switch (virtual +0x368): off at the Launch start (0x300420), in the hit reaction (0x2fe910) and
  on some Standby paths (0x306170); on at the locomotion brain's start (0x2edfc0) and in
  0x2f52a0, 0x2fa670 and the reaction requests (0x2ffaa0). The classification is virtual +0x3b8,
  called by the enemy update 0x2dec80 right after the root dynamics' step.
- Port (2026-10-14, BasicEnemy "Sensors"): the four sensors as above (trigger capsules in the
  physics world, the sweep through castSphere), read at the start of the step, classified after
  it, placed at the end of the apply; switched at Launch / Brain start and in the hit reaction.
  The enemy5 replay is byte-identical with and without them (triggers take no part in the
  solver). Contacts with the owner's own bodies are dropped (0x7df980; without that every enemy
  sensed its own torso as "another enemy"). Checked on run sens7 (enemycmp script from the
  capture, goal positions read through the brain's handle, `beacon-level enemytrace` now prints
  the state as a last column): 70,717 of 75,221 enemy updates agree. The differences are
  (a) the hero touching A / C (bit 1): the hero is not a physics body in the port, and (b) the
  0 / 2 alternation from the root body's own contacts falling out of step once the poses drift
  (chase / attack, 2-25 cm apart). With the hero's bodies (hero.md, HeroBodies): 70,747 agree;
  the largest remaining group (4 vs 0, 2,831 updates) is the original back in mode 1 while the
  port's enemy, 4 m away by then, still cruises - a behaviour difference, not the sensors. Not
  yet: the consumers of +0x3bfc (the planner 0x2e8620 and the processes listed above).
- The classification rule itself, on the original's own bits (run sens1): 41,911 of 41,915
  updates; the four others are four enemies on one update (4216).

## Brain decisions (decoding, 2026-10-14; brain start 0x2edfc0, update 0x2f1dd0)
- Stress (brain +0x108, every update): f = 0x2e2f40 = 1 - (m^ . (m + d)) / |m| with m = the root's
  last horizontal move (rootdyn +0x1b0 = root - last root at the apply) and d = the push of the
  world step (rootdyn +0x1d0 = read-back root - +0x160); 0 when |m| <= 1.19e-7 (the share of the
  intended move that was lost). +0x108 = clamp(p +0x108 + (1 - p) f, 0, 1), p = (1 -
  StressInterpRatio)^(60 dt) (dt from the system timer). Brain +0x114 sums |root move|.
  Verified on run stress1 (watch of brain +0x108, enemy +0x2720 / +0x2740): the rule predicts the
  original's next value within 8e-7 for 99 % of 4,536 brain updates (max 0.002, a timer dt). A new
  brain starts at 0 (0x2ea8c0). Ported (BasicEnemy::stressSample, Brain::stress_); in the replay
  of stress1 the port's value equals the original's (median 0) while the poses agree; it rises
  where the port's two enemies push into each other and the original's do not (paths diverged).
- Helpers (ground enemy, brain +0x84 == 0; +0x84 == 1 is the rope case, RopeArea = common +0xc40
  = singleton +0xcf0; 0x2e1ea0(enemy, 0) is always false - it tests contacts against mask 0):
  - 0x2eb6b0 hop: JumpUpHeight > 0 and (sensor state 3, or state 2 with stress >
    StressThreshold or enemy +0x25e5).
  - 0x2eb8d0(flag) search: enemy +0x3c6c <= const and (state 4, or flag and state 1 with stress
    > StressThreshold).
  - 0x2ebaf0: false.
  - 0x2e4c00: airborne (rootdyn virtual +0x40 false), touching hero bits (& 0xf), and a hero of
    that index above (its y - ours < its scale x const).
- Start, fresh brain: hop (0x2eb6b0) with the goal inside AngleToTurn, or 0x2e4c00 -> Jump (8);
  grounded with +0x84 == 0 and 0x7e9bc0 -> rope branch (enemy +0x29d0); ... ; airborne -> Fall
  (7: falling process 0x1685e50, "falling", handle +0x110) or Turn180 (9).
- Jump (8) start by brain +0x90: 0 jump up: v0 = sqrt(-2 g JumpUpHeight), apex time t = v0 / -g,
  forward speed JumpDistance / (2 t); velocity (0, v0, that) turned by the rotation (+0x130);
  JumpLocomotion (Bind +0x10b0, true for EnemyStandard) -> process 0x1685f40 (start 0x2e5710,
  update 0x2e5940): jump_up (0x16863f0, mode 0x10, ch 0x80000001), jump_down (0x1686a08: mode 0x10
  until grounded), falling jump_down_loop (0x1685e50, ch 0x40000001), jump_landing (mode 2, ch
  0x20000001); while jump_up / jump_down play the root moves by the horizontal velocity. Types
  1 / 2: 0x1686030 (to a point). JumpLocomotion false: the brain's update integrates the velocity
  under gravity (case 8). Update case 8: all channels done -> a new brain (state 0).
- Walk (6) update, goal not reached: hop -> Jump (8, if virtual +0x398); goal inside AngleToTurn:
  enemy +0x25e5 -> Edge (0xc); search(1) -> Stop (0xb, planner); grounded -> keep walking.
- Stop (0xb) update: when its channels end (or enemy +0x39b0): the goal outside the angle (pi x
  const, or AngleToTurn x const with FreeTransition) -> Turn (10); else search(0) and grounded
  -> planner level +0x194 (1 -> 2), else a new brain.
- Observed (run enemy5, b959 at 5210): Walk -> Jump (8, mode 0x10) -> Fall (7, airborne) -> Stop
  (0xb) on landing -> Walk: an obstacle hop.
- Hop port (2026-10-14, first version): Walk -> Jump (8) on canHop (0x2eb6b0), fresh brain ->
  Jump / Fall (7); ClipJump = 0x1685f40 (four children on channel 1, root moved by the horizontal
  velocity through virtual +0x70 = 0x255c20, which writes the root position alone; the port's
  BasicEnemy::moveRoot); PlayUntilGrounded = 0x1686a08. Clip play axis flags: 0x2df9d0 stores
  enemy +0x2620 = (h, v, h) from its 4th / 5th arguments (horizontal / vertical root motion);
  root motion (0x7e8450) runs in body modes 2 and 0x10 only (mask 0x112 skips 1, 4, 8); the
  read-back 0x7e0770 runs in every mode but 1 (0x7e61b0 then follows a holder, +0x450, when there
  is one). Status values of the slot runner equal the port's (1 running, 2 waiting, 3 done);
  0xbe50(-1) = no children (+0x38), 0xbf00(mask) = channels of started children.
- Hop resolved (2026-10-14, runs enemy5 and animnode1; earlier "clip clock" suspicion refuted):
  - Two kinds of original hops in run enemy5. 7ec7 at 4737 (under Chase): jump_up held for all
    20 updates (root y follows the clip x scale 1.1), jump_down until grounded at 4766, landing:
    what the port's ClipJump does. b959 at 5210 (under Cruising): 13 held updates, then a new
    brain (new goal object at the same point) in Fall (7) flying ballistically 0.5 m over the
    obstacle.
  - The cut comes from Cruising's update 0x3050d0 (virtual +0x398 is `return true` for this
    class, 0x255eb0): with children running, sensor state 4 or 1, body mode != 4 and the Cruising
    process older than 1.0 s (+0x78 updates x dt > [0x11a5300]) it follows itself with a fresh
    Cruising (new brain). A hop rising over an edge in held mode (0x10) therefore turns into a
    fall. Chase has no such rule. Also in that update: with no target found, ground mode and
    grounded, enemy +0x3c6c > 0.9 (water kinds) restarts too (not ported, 0 here).
  - Animation layer (enemy +8 -> +0x78 -> map +0x288, key 0 -> layer; nodes +0x70..+0x78, node
    +0x40 -> +0x1c finished): the "clip playing" test 0xbb2880 is false when every node is
    finished; run animnode1 shows the flag set in the update the clip runs out (spawn_start 12
    updates = its 0.2 s), so PlayOnce (0x2e7e00, mode != 1, +0x88 = 0) lasts the clip.
  - Vertical root motion: the original's jump_up rise uses the clip's own root speed from the
    first update (velocity column = clip delta x scale x 60 at full weight, although walk ->
    jump_up blends 0.2 s), while horizontal root motion ramps over turn -> walk. Port rule: a clip
    with vertical root motion blended in from one without it counts in full on y (hypothesis: the
    anim runtime's rule is per axis; root delta source 0xbb5e60 / 0xbb5ec0 in 0x7e8450, not
    decoded). Port rise now 0.212 m over 17 updates (original 0.214).
  - Grounded test 0x7e9ad0 in full: flag +0x73, or ground distance +0x4c within [0, +0x58], or in
    body modes 0x10 / 8 a contact of sensor index +0x350 whose hit bits meet the target mask
    +0x298. Sensor +0x350 = index 4, the root dynamics' own (0x7de3fe; run sens5th): type 1, a
    trigger cylinder (0xcaa220 -> 0xcb54c0, radius +0x34 = GeometrySensorRadius 0.025, half
    length +0x30 x 0.5, mass 200) from (0, body height 0.106, 0) down over height +
    ThresholdOfGround (0.136 = 0.106 + 0.03), ground mask, all modes (+0x88 = -1), not switched by
    0x2e2dc0. Ported (sensor 5, BasicEnemy::grounded); flag +0x73 not (unknown writer). enemy5
    replay: 7ec7 0.124, b959 0.087; hop1 0.032 / 0.033.
  - Hop start: b959's walk to the obstacle trails the original by ~2 cm from the Discover ->
    Cruising handover at 5032 (the port's turn starts later), so it reaches the obstacle 9 updates
    late: not a hop rule. The switch to Fall comes one update early in the port (sensor state 4
    one update sooner; the original's sensors during a hop are not recorded), leaving the body
    2.5 cm lower so it clips the obstacle top on take-off.
  - Run edgehop1 (deterministic, enemy5 inputs from fc_landed to 6500, raw enemies + sensor
    vectors): 7ec7 hops at the same obstacle (x 0.93, z 47.42) at 4970; the sensor state is 4 at
    the very next update (front A and upper C clear, sweep D off in held mode) and Cruising cuts
    the hop after one held update; fresh brain stands 14 updates (4971-4984), walks from 4985.
    The port replaying the same inputs reaches the same spot by its own goals (23 updates
    earlier) and does the same: one held update, state 4, Stop 14 updates, walk. The 13-update
    hop of enemy5 b959 differs only by A staying blocked while it rose. Sensor rules: in sens7,
    where the port's position is within 1 cm of the original's, the state agrees on 43,749 of
    43,754 rows; the other disagreements all come after the positions drift apart.
  - Port after these (enemy5 replay): 7ec7 0.128 m mean (was 0.134); b959 hops 7 updates late
    (5217 vs 5210), switches to Fall one update early (sensor state 4 one update sooner), then
    its body clips the obstacle top (speed 1.21 -> 0.91 m/s; the original keeps 1.16), lands 8 cm
    short near the edge and the planner sends it on a detour: b959 0.114 (was 0.072 by luck of
    the old landing); hop1 unchanged / 0.033. Open: hop trigger timing, the switch update, the
    take-off speed (original 1.161 vs 2 x 0.6056).

- Planner (0x1686558: start 0x2e8320 = origin +0x90 and the horizontal direction to the goal
  (facing when too short); update 0x2e8620): level +0xd8 (1 / 2), depth +0xdc, order +0xe0
  (0 alternating, -1 / 1 one way), cursor +0xe4 / +0xe8; 12 x level directions per turn, up to
  8 x level + 1 samples at depth 0 and 6 x level deeper; each sample moves sweep D (enabled only
  while sampling) to a candidate point; results +0xec.. (per sample flags), +0x124.. (16-byte
  records); spawns deeper planners ("level == 1 || level == 2"); output waypoints at brain
  +0x198 (16-byte points), followed by states 4 (TurnTo) / 5 (MoveToSurface, waypoint goals
  0x167d0e0). Seen in run hop1: 7ec7 after its spawn landing at 5835, sensor state 4, Stop (0xb)
  with idle for 14 updates while D swept, then Turn (10). Not decoded line by line yet.

- Planner decoded and ported (2026-10-14, assembly 0x2e8620, 0x2e8320; port: Planner and Brain
  states Stop 0xb / Path 5 in BasicEnemy.cpp):
  - Start: +0x90 = the enemy's position; depth 0: origin +0xb0 = that position, base direction
    +0xc0 = horizontal unit vector to the goal (facing when shorter than 1.19e-7), depth and order
    reset; children get origin / base / order from their parent.
  - Per update, only while sweep D is off (the planner switches it on when (update counter +0x78
    & 7) == 7, else off): with +0xe4 == +0xe8, skip samples (depth > 0) whose point is not nearer
    the goal than +0x90, then probe 1 (sphere r = D radius from (origin.x, P.y, origin.z) to P,
    self 0x1000, target 0xd7242f9f) -> +0x104[i], +0xe8++ (+0xe4++ too when blocked); else probe 2
    (P down D's length, target 0x88040810) -> +0xec[i], hit point +0x120 + 16 i, +0xe4++.
    P = origin + turn_y(2 pi i / 12 level) (base x D.z + (0, D.y, 0)); i from the cursor by the
    order (0: 0, n-1, 1, n-2, ...; -1: (n - k) % n; 1: k).
  - Gap (after a finished sample, cursor >= 2): c = the last index, side = -1 if c > 6 level else
    1, neighbour (c - side) % n; both ground, neither blocked -> waypoint at angle 2 pi / n
    (c - side / 2), D.z out, height = mean of the two ground hits; one sample in all: sample 0.
    The path (+0x88 -> brain +0x198) is cut to the depth; depth 0 appends and starts a child
    (depth + 1, same level, order = side, origin = waypoint, base = to the goal) on all channels;
    depths 1 / 2 only when the waypoint is nearer the goal than +0x90; depth 2 ends.
  - Ends: samples spent (depth 0 switches D on), or the sensor state is neither 4 nor 1 (D on).
    Children run first (the parent returns 1 while it has any).
  - Brain: fresh start with the goal inside AngleToTurn and search(1) -> Stop; Walk the same.
    Stop: idle (ground mode) on channel 1, planner level +0x194 on channel 2, path cleared; when
    the planner ends: goal outside (FreeTransition ? AngleToTurn x 0.3 : pi / 4) -> Turn, level 1;
    no longer search(0) or airborne -> new brain, level 1; more than two waypoints -> Path (5),
    level 1; else the other level (1 <-> 2) and Stop again. Path: a nested brain to the first
    waypoint (reach 0.05, facing 5 deg, +0x84 = 2; the original's turn process 0x1686288 before it
    is stood in for by the nested brain's Turn); its end pops the waypoint (5 again or a new
    brain); 5 s without reaching it on the ground drops the path.
  - Search test detail: enemy +0x3c6c is only raised for kinds with +0x3910 (walking on water);
    0 otherwise, so the test's "+0x3c6c <= 0.9" holds for enemy_standard.
- Ground probe order (0x7e20e0): the air-mode lift onto the ground comes before the distance
  (root y - ground y), so a lifted root counts as grounded. The port computed it before the lift;
  with the Fall brain, enemies stayed falling while lifted every update (hop1: 7ec7 went 159 m off).
- Results (hop1 replay, scripted goals): 7ec7 mean 0.030 m (was 4.98), b959 0.035 (was 0.053);
  enemy5: 7ec7 0.134 (0.106 before hops, 0.180 with hops alone), b959 0.072 (0.225 / 0.158).
  The port enters Stop in chases when the two enemies block each other (state 1); the original's
  brain during a chase is not reached by the watch chain, so unverified. Path (5) has not occurred
  in any replay (no three-waypoint path found).

## Spiky enemy (`enemy_spiky`, kind 0x7d4; decoded, captured and ported 2026-10-15)

Decompiles `evidence/decomp/spiky` (and `eproc/00301a30.c`, `00300420.c`, `002fd560.c`); run
roof_g4 (ROOFTOPS: the spitter killed by three hovers, then the G group).
- Object: descriptor 0x2f06dd0, creator 0x92b280 -> 0x92b6b0(kind 0x7d4, "mup_ch_enemy_spiky") on the
  basic enemy 0x2d9b20; vtable 0x16cbe78; root 0x92c230 (the default root of every kind but 0x7d5);
  +0x358 0x92c4d0: the attack on the hero with type 0x53 ("Attack on the hero" below). Tuning <EnemySpiky>: ChargeTime 0.6 (+0x2920), AttackTime 0.8 (+0x29d0), CoolDownTime
  0.8 (+0x2a80), Standby 3-5, ChaseTime 1.5; model / bodies spiky_*.
- Clips (mup_ch_enemy_spiky): shake_start 0.467 s, shake_idle 0.267 s, attack_start 0.533 s,
  attack_idle 0.117 s (loop), attack_end 0.433 s, spawn 0.617 s, landing 0.233 s; no "attack",
  jump_up_loop or spawn_* clips.
- Launch Jumpup (0x300420): only enemy_standard (0x7d2) takes the spawn arc (0x2e6540); 0x7d6 its
  own; every other kind a plain ballistic jump 0x2e52f0 (process 0x1685ec8: start 0x2e53b0, update
  0x2e5550) on channel 2 with the same trajectory (0xba9310), playing "spawn" (or "discover" without
  it) once in mode 4 and then "falling"; it ends grounded with v.y < -FLT_EPSILON (or after the
  flight time when +0x3240 is set; else -1). Meanwhile TurnOverTime (0x2e5a40) over rise + fall on 1,
  the trigger watch on 4, then "landing" (0x3b55d35c) on 3 and the terminator.
- Active (0x301a30, case 0x7d4): chase as usual, then the charge 0x16870b0 (start 0x2fcd70, update
  0x2fce90; +0x80 = ChargeTime): shake_start (0x2e7b00) then shake_idle looping (0x2f7800 mode 1) on
  channel 1, Wait(ChargeTime) on 2, terminator on all; then Attack (0x16871a0) with clip 0 and
  +0x84 = 1. No attack_end process in Active (enemy_standard: attack_start, Attack("attack"),
  attack_end).
- Attack with clip 0 (0x2fd560): attack_start on 0x80000001, terminator; attack_idle (0x2e7820 with
  follow = 1: process 0x1686300, loops on the ground and follows it) on 0x80000001 with
  Wait(AttackTime) on 0x80000002, terminator; attack_end on 1 + terminator 0xf when the definition
  has it (0x2df770). The guard (+0x3aaf via 0x2dfef0) is up from the start until the 0x80000000
  channels are free (update 0x2fd810): attack_start + AttackTime = 80 updates. While it is up,
  kinds 0x7d4-0x7d7 take no punch or kick (hit types 1-3, 16; 0x2e20a0 stores the attacker at
  +0x3b30).
- Captured (roof_g4; harness "launched" flag, mode +0x25a8, guard +0x3aaf, top process from the
  process chain): spitter state 9 at 3247, the G enemies spawned at 3248, launched fa6a426c 3429,
  d5c0905c 3453, spiky 0cb19db3 3465 (no held spawn_start: mode 4 at once); the spiky grounded at
  3513 (-0.314, 0.011, -27.785), Active from 3527, guard 3602-3683 (81 updates); purple d5c0905c's
  guard 3552-3565, fa6a426c's 3601-3614 (13 updates: the "attack" clip). d5c0905c's attack hit the
  hero at 3552 (pushed, then lifted; it died and the level restarted at 3766).
- Port (`scriptwalk` with BEACON_SCRIPT_SPITHIT=2971,3106,3247 and the hero placed as in the
  capture): launches 3429 / 3453 / 3465, spiky Active 3527 at (-0.313, 0.005, -27.783), purple
  guard from 3553, spiky charge 37 updates then guard 81 updates. After 3552 the runs part: the port
  does not damage the hero (the original's hero is pushed away, so the chase ends elsewhere).
- Ported 2026-10-15: the attack on the hero (spikes 0x53, "Attack on the hero"). Not ported: the additive clips
  (attack_*_add, BlendMode 1 on idle; no user found yet); shake; CoolDownTime / Standby / ChaseTime
  users for this kind (not reached in roof_g4).

## Attack on the hero (decoded and ported 2026-10-15; decompiles `evidence/decomp/herodmg2`; runs combat5, roof_g5)
- Virtual +0x358, every enemy update after the root dynamics' step (0x2dec80, before the
  classification +0x3b8): basic 0x2e1d80, spiky 0x92c4d0. Clears +0x3ab0 (hit this update); skips
  everything with +0x3aae. Type: kinds 0x7d4..0x7d7 from the table at 0x11a2630 (0x53, 0x53, 0x53,
  0x16), else 0x2e for enemy_standard (+0x3ab3 = kind not 0x7d2 / 0x7d3 clear) and 0x19 for the
  others.
- 0x2e0130(body, type): with the attack flag +0x3aaf up, each contact of the body (its contact
  report, 0xcad2a0) whose partner is a hero (the heroes' body map, 0x6675e0 +0xd0) that this attack
  has not hit yet (list +0x3cf8, emptied by 0x2dfef0 whenever the flag changes) gets a hit
  {type, the body's position, (hero position - body position) normalised (the hero's +0x68 when
  shorter than FLT_EPSILON)}, queued (0x6cee0) and delivered to 0x66faf0; the hero joins the list;
  +0x3ab0 set; for enemy_standard also an effect 0xc934f198 at the hero. The body: +0x3b38, the
  DamageJoint body (0x2daf40 matches the definition's bodies by the Bind/DamageJoint name: tuning
  +0x2ad0 +0xcd8 = the Bind group at +0x630, DamageJoint +0x6a8; enemy_head_col on joint_head,
  self 0x1000, target +0x3ac0 = 0xd7a63fdf read from run roof_g4). The spiky also checks its root
  body +0x2810.
- 0x2e12a0(type), kinds with +0x3ab3 only, when the flag is up and nothing was hit this update:
  a hero who punched or kicked the guard (+0x3b30, kept by the hit intake 0x2e20a0), else each hero
  standing on one of the enemy's bodies (hero +0x1b0, the model's bodies 0xbd6150) and not yet in
  the list, gets the hit from the damage body; +0x3b30 is then cleared.
- Port: BasicEnemy::attackHero; hits go through EnemyWorld::heroHits to Hero::takeHit (hero.md
  "Damage"). **[hypothesis]** the heroes' body map holds the foot body only (with the enemy sensor
  (r 0.3) the port's headbutt landed on the guard's first update; with the foot body three updates
  before the original's, see below); the hero position the direction uses is its feet.
- Run roof_g5: d5c0905c's guard up at 3552, the hero's 0x2e request at 3556, LightDamage 3557;
  the spiky's guard at 3602, its 0x53 at 3604 (DeathOnSpike 3605). Port (`scriptwalk`, the hero
  placed as in the capture): guard 3553, request 3554 (the head sphere meets the foot capsule on the
  attack clip's second update, 3.4 cm deep): **open** - the original's head reaches the hero ~3
  updates later (clip blend or contact report timing not followed).
- Fixed 2026-10-15: the port looked up Bind/DeadCollision and Bind/DamageJoint in a moved-from
  physics file, so both used record 0 (the dead body): the damage body was enemy_dead_col at the
  enemy's base frame instead of enemy_head_col on joint_head (`beacon-level punchtest` still 38 of
  38).
