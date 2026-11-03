# Level object runtime (2026-10-13)

From the user's executable and data only (AstroQuest not used for this part). Decompiles in
`evidence/decomp/{objrt,objvt,movobj,seqr,animplayer,level}`; tools `beacon-level objects |
locator | near`, `.analysis/tools/strref.py`. Port: `src/game/LevelRuntime`.

## Creation

- `MupLevelObjectGenerator` (0x714950): looks a class descriptor up by lox name
  (`m_levelObjectDescriptors`), calls its `std::function` factory, registers the object's
  attribute properties (asserting "Attribute %s already exists"). Called through 0x714400 by the
  document loader 0x566250 and by run-time spawning by hashed name (0x54d210).
- Load order (0x566250): placements of class `cutscene_player` first, then all others not flagged
  (+0x2c), with special handling of `crew_bot` (FNV-1a of the class names 0x8ae5efb3 /
  0x882f629e).
- The factory 0x6f2ae0 registers 264 classes, each from a shared prototype `std::function` plus
  a list of 40-byte name entries (meaning open).

## Attributes and links

- Each object keeps a list of attribute properties at +0x1a60 (type at +0x20: 0xc link, 0xd
  sequencer, others values); a class binds its attributes with reflected property objects (value
  at property +0x90, as the hero tuning), e.g. moving joints 0x2538e0 / 0x59da10.
- After creation 0x534750 visits the list: links (0x5528f0 + 0x552990) and sequencers (0x21bbc0 +
  0x21c3a0) get their owner and are resolved against the document (owner id +0x1a90, document
  +0x1b60).
- **Link resolution (0x552a00)**: split at ','; an entry starting with ':' whose next character is
  '1' sets the link's "all" flag (+0x108); every other entry is FNV-hashed and looked up; unknown
  UUIDs are skipped (the 796 dangling links are harmless); targets whose class is on the link's
  exclusion list are skipped; each target gets a back-reference to the linking object (0x5380a0).
- **Link state**: 0x552ed0 "empty" (no targets or first target gone); 0x552f00 "fired": without
  the flag any target "on", with ":1" all targets "on"; "on" = the target's virtual +0xa0;
  optional filter on target position (+0x131/+0x134, not ported).

## Object classes

323 vtables share the base layout (40 slots; base implementations in `evidence/decomp/objvt`).
Slots identified so far by the moving-joint class: +0x20 setup after load, +0x40 update(dt),
+0xa0 "on", +0x70/+0x78/+0x80 set position/rotation/scale(?), +0x118/+0x120 world transform.

## Moving joints (`moving_object_joint`, vtable 0x16a4fa8, constructor 0x59da10)

Locator objects that play their own clip: attribute `Locator` (not in the schema) names
`<level>/anim/<UUID>_locator_anim_play.anim`; joint 1 of the clip is the locator's pose in its
parent (verified: frame 0 equals the placement's transform relative to its placed parent). Objects
parented to a joint follow it.

- **Setup (0x59deb0)**: StartTrigger set -> the sequencer's player is paused and a start callback
  (0x59faa0 -> sequencer play 0x555ae0) is installed; TriggerType None -> delay state 1; no
  StopTrigger and TriggerOff Ignore -> flag +0x5a8. Player loop flag cleared (0xbb8180(p, 0)).
- **Trigger (0x59eb90)**: StopTrigger set and fired -> off; StopTrigger set, not fired, type None
  -> on; otherwise by type: TriggerObject(1) StartTrigger fired; LookAtAndTriggerObject(4) also
  the look-at; RideOn(2) a hero stands on it (see below); LookAt(3) the player looks at it (+0x2625/+0x25bc);
  Parent(5) the parent object is on; None without StopTrigger -> off.
- **RideOn (0x59eb90, type 2)**: for every hero (list 0x672dc0) with a live support node (hero
  +0x1b0, weak reference), the nodes listed by the joint node's extension (+0x8 -> +0x70, copied
  by 0xbd6150 from +0xa0) are compared with it; a match triggers and the hero joins the rider
  list kept at +0x2ce0. Port: the hero's support placement is parented directly to the joint
  (reading +0xa0 as the node's children is a hypothesis; no RideOn joint in forest_castle).
- **"On" seen by links (0x59e6d0)**: rising edge of the trigger; while it stays triggered, the
  update after a completed cycle (unless flag +0x5a8).
- **Start delay (0x252610)**: 0 idle -> triggered -> 1 counting (drops back to 0 when the trigger
  drops, unless TriggerOff Ignore) -> after StartDelayTime: callback, state 2, progress 0.
- **Playback (0x252750, exact from the assembly)**: progress += rate * dt * (Speed, or
  SpeedReturn when off and TriggerOff Return); at the clip end Loop wraps (cycle completed),
  OneShot holds, Replay holds and returns to idle (completed); triggered -> rate 1; off: Stop and
  progress below CompletionThreshold % -> rate falls 2/s to 0; Return -> rate falls 2/s to -1, at
  the start progress/rate 0 and idle (callback); otherwise rate 1. Rate starts at 0.
- **Pose (0x59e6d0)**: progress != 0 -> the clip is sought to it (0xbb82a0); otherwise the
  sequencer's own player runs (paused at the start when a StartTrigger exists). A TriggerType
  Parent joint without a StartTrigger also waits at its clip's start until its parent fires (run
  roof_bridge2: moj_wood_cylinder_rote at y 0.2963 = frame 0 from 2801 until the bridge turns).
  **[hypothesis]** no joint's player runs by itself (the port samples the clip at the playback's
  progress only; the earlier "plays once" was an inference and made ROOFTOPS' first bridge jump).
- **Scale (0xbd4660)**: a placement's bodies follow its node's scale: when the scale differs from
  the stored one (+0x170) by at least FLT_EPSILON, it is stored and the shapes are rebuilt at the
  new scale (the build 0xbd1290 runs once per object setup with the node's scale then). ROOFTOPS'
  telescoping bridge (moj_bridge, segments scaled 0.49-0.57 -> 0.61 by their clips) needs it:
  without it the collision stayed short, with slots and steps at the seams.
- Clip player (engine, 0xbb.... / track advance 0xc639f0): time += rate * speed * dt; loop flag
  +0x19 wraps, else clamps to [0, length] and reports the end.

**Verified** (run joints1, `tools/jointcmp.py`): all 312 joints of forest_castle traced every 120
updates for 94 s (object id +0x1a90 = FNV-1a of the UUID; +0x2c50 delay state, +0x2c54 progress):
the port's states and progress equal the original in all 14,664 samples (progress within 2e-3,
including loop wraps), once visibility-group suspension is in.

## Visibility groups (`visibility_group`, vtable 0x16e5780)

- Constructor 0xaf3170: state +0x1ca4 = 1 (shown); start (+0x28, 0xaf3530): resolves Targets
  (0xaf35a0) and, if TriggerAppear is set, hides the targets (0xaf3e70) and goes to state 2.
- Update 0xaf3f40: shown and TriggerDisappear fired -> hide, state 2; hidden and TriggerAppear
  fired -> each target: 0x772130(target, 1, 2) and suspend bit 0x10 (+0x31) cleared (resume
  virtual +0xf0 when no bit is left), state 1. A `visibility_trigger` link target must also be in
  its state 1 (+0x1cac).
- Hide 0xaf3e70: only targets whose virtual +0xc8 returns true (base: false) get 0x772130(t, 0, 2)
  (model/physics off, flags +0x32) and suspend bit 0x10.
- **Traced (mechanism not found)**: moving joints listed directly in a hidden group's Targets are
  not updated (10 of 10 such None-type joints stuck, all 132 others playing; membership is not
  inherited from parents). Moving joints keep base +0xc8 = false, so another path suspends them,
  probably the `SuspendBySection` optimizer (multi_platformer.xml <Optimizer>). The port
  suspends targets of hidden groups (hypothesis for the mechanism, matches the trace).
- Enemies follow the same rule (2026-10-14, run hop1, all 22 enemy_standard of forest_castle):
  the five listed only by groups hidden at the start (vg_after_carpet2, vg_hooks: E_01, E_04,
  E_05 in FC_S07 and two more) never change state, although E_04 / E_05 have no SpawnTrigger and
  LaunchType Stand (updated, they would launch at once, as the port did); every enemy that does
  update is a target of vg_start (shown from the start). The port now skips the update of
  enemies whose placement is suspended (EnemySystem::update). The level's seven `|optimize|vg_*`
  groups (782 / 22 / 396 / 344 / 208 / 329 / 422 targets) look like the level's streaming
  sections; the code path that suspends them (the Optimizer's SuspendBySection, value at
  +0x140 of its block, bound by 0x777010; objects' Suspendable at +0x1950, bound by 0x5ca50)
  is still not found. Enemy +0xc8 = 0x5bae0 returns +0x19c0 (inside the MayaAttributes set at
  +0x1960), so the group's own hide routine does not suspend enemies either.
- Enemies under moving joints (the ten FC_S03 rope-area enemies, parents moj_*): in Standby (body
  mode 1) the root is the placement's world frame, full rotation (ported, BasicEnemy::update).
  Verified on run hopdet1 with FIRST_FRAME 1304: 23,090 rows of the ten enemies within 0.17 mm
  mean, 0.21 mm max. Not modelled: their fixed body stays at the spawn pose in mode 1 (the
  original's is not traced).
- Hero trigger marker (runs heronode1, trigbox5; 2026-10-14): the plain trigger_box objects carry
  the sensor vtable 0x16d1370 (FIND it, id +0x1a90 = FNV-1a of the upper-case UUID). Heros boxes
  walk the marker list +0x1d08..+0x1d10 (filter 0x66ce40: hero +0x88 -> +0x18 -> +0x10 == 0) and
  test each with 0xc57700. The hero's marker is not the default one: vtable 0x16d15f8, radius 0,
  joint index +0x28 = 2, so the point is the world position of skeleton joint 2 of the hero's
  animated model (the body joint under the root-motion joint, rest height 0.388 x 0.22), not the
  feet (its model-0 position, impl +0x38 -> +0x40, equals the traced root). In run hopdet1 the
  hero brakes and stands 4.2 mm outside tb_standard_A_appear by its feet from 3808; the box
  turns on during update 3813 with neither box nor root moving (the body joint swings into it),
  and b959 launches at 3815 (the port, testing the feet: 3825). Ported: RuntimeInputs::heroMarker
  from HeroView::markerPoint in play; the replay tools keep the feet (no hero pose), and the
  port's hero animation choice is reconstructed, so the joint's exact path is approximate.
- Object managers (multi_platformer.xml <ObjectManagers>: BasicObject, Enemy, Trigger, ... with
  ParallelUpdate and per-manager Suspend/SuspendDistance) update the objects: no single update
  loop exists in the document code.

## Sensors / trigger boxes (ported, verified)

- Sensor base class vtable 0x16d1370 (constructors 0x977c20 / 0x977ec0; trigger_box variants
  0x52a000.. vtables 0x169fd60 / 0x169ffe8). Settings block at +0x1df0 (0x977740: SensorType,
  TriggerType BeInside/BeOutside/Enter/Leave, Oneshot (default true), Inversion, Capacity (default
  1), MarkerType Heros/VrPlayer/AllPlayers/Object/Enemies/VrHero/Traps/Breakable/CrewBot,
  MarkerObject, MarkerJoint, MarkerRadius, MarkerOffset, DetachScale, NoArea), copied into a
  sensor component at +0x1cb0 (0x978ad0): +0x8 type, +0xc state, +0x70 capacity, +0x74 oneshot,
  +0x75 inversion, settings +0x1d40 (+0x18 on delay, +0x1c off delay: 0 by default, 0xc542b0).
- "On" (+0xa0, 0x38abe0): +0x2a11 (a "Triggered" message, 0x97b1a0) -> on; else, once started
  (+0x2901), (state 1 or 3) XOR Inversion.
- Detection 0xc57460: inside = component virtual +0x18, total = +0x20; Capacity >= 0: inside >=
  Capacity, negative: inside == total + 1 + Capacity (-1 = all); BeOutside counts the outside;
  Enter/Leave report the rising edge only.
- State machine 0xc57280 (jump table 0x12fe5c4): 0 start -> 2 (or 1 when forced +0x15); 2 off:
  with Oneshot and Inversion stays off, else detected for the on delay -> 1; 1 on: Oneshot without
  Inversion latches, else not detected -> 3 (off delay > 0, not Enter/Leave) or 2; 3: detected ->
  1, off delay over -> 2. State changes call component virtual +0x10.
- Trigger box inside count 0x979680 by MarkerType: heroes (0x9794d0 -> volume test 0xc57700 /
  0xc54660 with the "active" flag, DetachScale presumably), VR player head (main+0x48 -> +0x4b0;
  0xc577c0), both; NoArea counts everything as inside.
- Volume test 0xc54660: SensorType has one value, 0 "Box" (0x977740, default): the point goes
  into the box frame (owner node position/rotation/scale, 0xc55980, inverse matrix) and must lie
  within +-0.5 * size (size x DetachScale while active, settings +0x10 / +0x20) grown by the
  marker radius; case 2 is a radial/height test, case 3 logs "Arc trigger has not supported yet".
  Trigger boxes are authored as "TriggerCube" placements: a unit cube in the placement's frame.
- Markers: every object gets a default marker (base slot +0xb0 -> 0xc53370): radius 0, no offset
  (0x12fdfe0), i.e. its origin; the hero is tested at its feet.
- **Verified** (run boxes1, `beacon-level runtrace` with the original's hero positions,
  comparison script as for the joints): all 178 trigger boxes (trigger_box, _for_trap,
  _for_enemy) and all 312 joints equal in 13,720 samples over 3,358 updates while the hero walks
  through the start area's boxes, including the joints those boxes start. Trigger boxes keep
  updating inside hidden visibility groups (traced), moving joints do not.

## Visibility triggers (`visibility_trigger`, vtable 0x16d4c40, ported, not yet traced)

- Update 0x9a5890 (jump table 0x1297aa8), event +0x1cac reset each update: state 0 idle: with a
  TriggerOn link it fires when the link fires, without one when the VR player's rig has passed the
  trigger's z ([main+0x48]+0x38 >= ... : object z not below the rig z); TriggerOnDelay 0 -> event
  1, state 2, else state 3 until the delay -> event 1, state 2; state 2: back to 0 when a linked
  trigger stops firing; state 1: after TriggerOffDelay event 2, state 0. "On" (0x9a5a50): event 1
  or 2 (one update). Visibility groups need event 1 from a visibility_trigger.

## Moving collision (ported)

- Placements under moving joints are separate collision bodies (`CollisionWorld::beginBody`),
  posed each update with current world x placed world^-1; the hero stands on them through the
  same probe, follows their motion (hero.md "Moving support") and the probe reaches 0.2 m further
  while supported. Check: `beacon-level ride GAME LEVEL [PLACEMENT [SECONDS]]` (294 moving
  bodies in forest_castle; a hero on a swaying branch keeps its place in the branch's frame).



forest_castle: 312 joints (253 with a clip): 141 None/Loop/Ignore, 105 TriggerObject/OneShot with
start link, 52 Parent/OneShot, 10 TriggerObject/Loop, 2 Parent/Loop, 1 None/Loop/Stop with stop
link, 1 TriggerObject/OneShot without link.

## Coins (`coin` / `coin_big`, vtable 0x1690278, ported)

Decompiles in `evidence/decomp/coin`. Constructor 0x3d3930 (attributes SpawnTrigger +0x2720,
SpawnDelay +0x28f0; flag +0x2970 from the factory), dropped coins 0x3d3ad0 (with CoinDropParameters
0xb04c0, motion 0x3da6f0). Rendering goes through a shared instanced model per kind
([MupMain]+0xf8+kind*8, instance index +0x25a8); kind = virtual +0x208 (0 coin, 1 big).
- **Tuning**: `<Coin>` / `<CoinBig>` in multi_platformer.xml, bound by 0x39c890 (CollisionRadius
  +0x1f0: 0.22 / 0.30, AngleSpeed +0x400: 360) and 0x39c660 (`<Coin>` only: CoinNumBig +0x4b0 = 10,
  treasure box / lost bot counts). Getter 0x3da240(kind).
- **State machine** (generic id -> std::function table at +0x25b0, run by 0x3d4820, switch
  0x3d4530; state +0x25b0, time in state +0x25b4), registered in setup 0x3d3c60:
  0 waiting (update 0x3d8db0): unless +0x2970, once SpawnTrigger fires (or is empty) the timer
  +0x2910 (-1 = unset) starts at SpawnDelay and counts down; at <= 0, 0x3d6860: effect
  `ef_coin_appear`, instance shown, state 1. 1 out (update 0x3d9090 -> 0x3d4a60 unless a mover
  +0x26f0 drives it): the stored pose +0x2950 (refreshed from the world transform when the object
  moves, 0x6f0ab0) is applied with rotation q_coin * q_spin, then the pick-up test 0x3d4c30 -> 2.
  2 taken (enter 0x3d91c0: taken flag +0x1c52 with the document's checkpoint counter, hidden,
  `ef_coin_pickup`; update 0x3d4dd0: 0x3da190 adds 1 (big: CoinNumBig) to the kind's total, the
  hero (+0x44c) and the session counter, plays the pick-up sound, removes the object). 3 flying to
  the hero with `ef_coin_trail` (dropped coins; not ported).
- **Setup**: SpawnTrigger empty and +0x2970 clear -> shown, state 1; otherwise hidden, state 0.
- **Pick-up (0x3d4c30)**: for every hero whose mode (+0x88 -> +0x18 -> +0x10) is 0: centre =
  position + (0, 0.5 * scale.x, 0) (hero +0xc0); taken when |centre - coin| < CollisionRadius.
- **Spin (0x3d9b70 / 0x3d9fb0, per kind)**: angle += AngleSpeed * pi / 180 * dt, wrapped to
  [-pi, pi); q_spin = (0, sin(a/2), 0, cos(a/2)).
- **Port**: `LevelRuntime` (coins after the joints in the update; the original's order between
  objects is not known), `visible` / `pose` for drawing. Hypotheses: the spin starts at angle 0;
  the hero mode check is not modelled (always allowed).
- **Verified**: run coins1 (a walk at the spawn): all 127 coins' states equal the original at the
  start (70 waiting for a SpawnTrigger, 57 out; FIND `1690278@1a90,25b0`). Pick-up, run coins2
  (`scripts/coins.txt`): four FC_S00 coins were moved onto the hero's path by POKEFIND on
  +0x2950..+0x2958 (offsets 0.15, 0.20, 0.22 and 0.24 m beside it; `beacon-level runtrace` with
  `BEACON_COIN_MOVE` does the same): the original's counter (hero +0x44c) rose at frames 4626,
  4642 and 4908, the port's on the same three frames, and neither took the 0.22 m coin.

## Open

- Visibility triggers and rig-driven group switching: ported from the code, not traced yet.
- Update order between objects (the port updates parents first; the original's scheduler is not
  found yet: no single loop in the document code calls the update slot).
- RideOn traced on a level that has such joints; look-at triggers (+0x2625/+0x25bc).
- Coins: dropped / auto-collected coins (state 3, CoinDropParameters), coin_spawner; enemies.

## Dynamic grass (`dynamic_grass`, recovered 2026-10-15; decompiles `evidence/decomp/grass.c`, `grass2.c`; run roof_grass1)

- Class: factory functor 0x1691d90 (call 0x3f39a0: new 0x30b0, constructor 0x3ed830), vtable
  0x1691b48 on base 0x2583e0 (vtable 0x167d368). Properties LookAtHero (+0x2cd0), LookAtRange,
  MinAngle (+0x2eb0), MaxAngle (+0x2f60), LookAtLimitAngle. Tuning `property/dynamic_grass.xml`
  (binder 0x3ed0d0, singleton 0x2edb870): GrassDistanceThreshold +0x140, GrassBotDistance
  +0x1f0, Forward / BackwardBlendSpeed +0x2a0 / +0x350, GrassSoundDistance +0x400, punch / spin /
  water / shuriken / machinegun distances.
- Setup 0x3edad0: index +0x305c = a global tuft counter; one 0x100-byte record per joint whose
  name contains "b_joint" (0xa5a860; names from the model, the port matches fnv1a("b_jointN")
  against the skeleton: 40 blades in mup_bg_city_grass_dyn_a). Record: +0x0 joint, +0x20 rest
  rotation, +0x40 rest matrix, +0x80 the matrix this update (row-major, position in the last
  row), +0xc0 weight, +0xc4 attack mode, +0xe0 its timer, +0xe4 rustle played.
- Update 0x3ee130: every 10th update (+0x3060 % 10 == +0x305c % 10) 0x3ee610 lists the heroes
  (0x7389f0 list) within GrassDistanceThreshold + GrassBotDistance of the tuft (more with
  LookAtHero), their positions in the model's space at entry +0x20; +0x3051 = list not empty.
  Other actor lists (0x7397b0, flag +0x31) and the weapons (0x5f11a0..0x5f11e0) also feed it
  (not ported). Then 0x3f26d0 (attacks: hero state 10 spin, 8 punch, weapons -> +0x3052).
- Per update 0x3ee2e0: the records' matrices from the animated joints (0xbc9800), then with
  +0x3051 the bend 0x3f0370(r = GrassBotDistance / x scale, dt): for each blade not in attack mode
  the nearest listed actor (3D, model space) at distance d; r > d > FLT_EPSILON: angle =
  MaxAngle - d (MaxAngle - MinAngle) / r, axis horizontal tipping the blade away from the actor,
  weight += GrassForwardBlendSpeed dt (<= 1), rustle 0x178bdcaf once above 0.1; else weight -=
  GrassBackwardBlendSpeed dt (>= 0). The joint's rotation = slerp(animated, bend * animated,
  weight) in model space (pivot kept), written back with 0xbcb6b0. Attack mode: angle 0.8..1.0 x
  MaxAngle (random, 0xebe680) for a timer (+0xe0).
- **Verified (run roof_grass1)**: the weight rule reproduces the original's weights of 12 blades
  exactly over 320 updates; a fully bent blade's rotation matches the angle formula (30.2 vs
  30.15 degrees with the idle sway) with a horizontal axis perpendicular to the hero direction.
- Ported: `game/DynamicGrass`, applied in LevelScene::bendGrass to the posed joints.
- Open: when the list empties (hero beyond 1.19 m) the bend is not evaluated and weights stay
  (decoded, not traced); attack mode, other actors, LookAtHero, rustle sound, AnimTrigger.

## Lost bot (`lost_bot`, recovered 2026-10-15; decompiles `evidence/decomp/lostbot.c` .. `lostbot7.c`, `kick.c`; runs roof_bot2, roof_bot5)
Class vtable 0x16b9690 (class id 0x13c0), constructor 0x724b40 (size 0x4a00, base 0x99e3e0),
creator 0x724a00, setup 0x726270, update 0x7288f0 (state +0x2150), hit 0x72f0e0 (slot 0x210).
Tuning `<LostBot>` in multi_platformer.xml, singleton 0x38d460 (values: HelpMeRangeIn/Out +0xab0/+0xb60, kickMeRangeIn/Out +0xc10/+0xcc0, KickTime
+0x2200, FlyTargetPosFromVrPlayer +0x760, FlyPath: FlyStartSpeedRatio +0x2820, FlySpeedMin/Max
+0x28d0/+0x2980, FlyHighestPointRatio +0x2ae0, FlyHighestPointHeightToLen +0x2b90, FlyGravity
+0x2c40, FlyBrakeStartOffset +0x2c60). Placement attributes (value = property + 0x90): SpawnType
+0x2390, WaitAnimFar/Near +0x2640/+0x2720, ActionType +0x2ad0, ActionTrigger +0x2b00, FindTrigger
+0x2850, KickXZVelocity +0x30d0, KickYVelocity +0x3180, KickGravity +0x3230, PathPoint1..5
+0x32f0.., FaceToHero +0x3e60, KickAss +0x3f00, CanKickBeforeHappy +0x3fa0, ID +0x2220.
- Clips: WaitAnimFar / WaitAnimNear, or help_01_far / help_01_near unless both are named (+0x4880).
  0x727ad0 plays a clip on the bot and "<clip>_flag" on the flag (mup_co_flag, attached by its
  arbot_hand joint to AR_Forearm_R; AR_Forearm_L with FlagHand Left); no such flag clip hides the
  flag. 0x727840 plays on the jetpack (mup_co_jetpack, joint_root on AR_Chest), layer state
  +0x4848: 1 inactive_idle, 2 kickass, 3 takeoff_in, 4 takeoff_loop (0x72d790 when takeoff_in
  has played), 5 takeoff_out.
- States (ROOFTOPS: SpawnType Default -> 1 at setup): 1 far, -> 2 when the nearest hero is
  within HelpMeRangeIn (0x72a260: Out while in state 2); 2 near, turns to the hero (FaceToHero or
  default clips), -> 1 beyond HelpMeRangeOut, -> 7 within kickMeRangeIn (default clips or
  KickAss; 0x72a310: kickassready, jetpack kickass); 7 turns its back to the hero (target
  2 bot - hero), -> 2 beyond kickMeRangeOut. Turning (0x72a600): slerp to the heading by
  1 - (1 - 3 dt)^(frame time x 60); targets nearer than 0.1 m keep the heading. The bot updates
  after the hero within an update (transitions at 3186 / 3260 of roof_bot5 only fit the hero of
  the same update).
- Kick: a punch request becomes state 0x33 when 0x678330 finds a body of class 0x13c0 / 0x13c6
  (or 0x138f passing 0x779ac0) among the contacts of the hero's enemy sensor (+0x158); hero.md
  "Kick". Its type-1 hit along the hero's facing reaches 0x72b370 (needs ActionTrigger empty,
  CanKickBeforeHappy, or +0x4855): state 8, rescued bit in the stage progress (bit ID), the
  controller told (0x5f1820), launch velocity +0x4870 = KickXZVelocity x hit direction with
  KickYVelocity up, effect 0x19d8ba4a, flight phase 0.
- Flight (+0x4560, 0x72bdc0: phase id, time, a map of three phase objects with std::function
  enter / update / exit, objects at +0x1e20 / +0x1f00 / +0x2020, vtables 0x16b9a08 / a30 / a58):
  - 0 (0x730720 / 0x7308a0): the bot's kickass clip stretched to KickTime (blend 0.1);
    v.y += KickGravity dt, p += v dt; a sphere cast (r 0.04 at +0.11) keeps x/z on a hit with
    normal.y >= -0.7, else y; after KickTime -> 1 (the turn still runs that update); turns
    towards the path's start tangent (rate 3).
  - 1 (0x730cd0 / 0x731540): jetpack takeoff_in, bot backpack_flying_0N (N = ID % 3 + 1), the
    entering_controller_0N clip's length kept (+0xf0); path rebuilt every update (0x72d9a0):
    start, path points, highest point (lerp(last, target, FlyHighestPointRatio), y = max of the
    two + horizontal distance x FlyHighestPointHeightToLen), target = VR player (+0x30) +
    FlyTargetPosFromVrPlayer + FlyBrakeStartOffset; start tangent = first chord, end tangent =
    -normalize(FlyBrakeStartOffset). Spline 0x55ca90: phantom points p0 - 1 x t0 and pn + 5 x t1,
    Hermite segments with Catmull-Rom tangents (0.5), chord parametrisation, length +0x80, highest
    y +0x84; evaluation 0x55d040 / tangent 0x55d110. Start speed min(FlySpeedMax,
    sqrt(2 FlyGravity (highest - start.y)) x FlyStartSpeedRatio). Each update: d = at(u + 0.1/L) -
    at(u); up to u = max((L - 0.6)/L, 0.9): v^2 -= 2 FlyGravity d.y v dt / |d| (clamped to
    FlySpeedMin..Max), facing the tangent (heading then pitch, +y up); after that brake to
    2 |b| / entering length (v -= (v^2 - e^2) / 2 / |p - at(1)| dt) and turn a tenth of the way
    to identity (facing +z) per update. u += v_old dt / |d| x 0.1 / L; position = at(u_old). Past
    u = 1: entering_controller_0N (blend 0.2) and 0.5 s along -b by |b| (2 tau - tau^2), tau = t /
    entering length, turning to identity by (1 - cos(min(1, 2t) pi)) / 2; then phase 2.
  - 2 (0x72f4c0 / 0x72f9f0): entering_controller_0N_in; sub-state 0 a jump (0xa5a940, height 0.2,
    gravity 15) from VR player + FlyTargetPosFromVrPlayer into the controller (0x5f2870 pad
    point) while the scale goes to 0.3 x; arrival: jetpack takeoff_out, controller calls
    0x5f1250 / 0x38cd80 / 0x5f29e0. 1: scale 0.3 -> 0.1 x in 0.1 s, then bot and jetpack hidden.
    2: after 0.7 s shown at scale 0.091 playing in_controller_landing. 3: when it has played,
    in_controller_landing_out (blend 0.1). 4: scale 0.091 -> 0.04 and down 0.02 (cosine ease)
    with the clip's progress, controller 0x5f1860 after 0.4167 s; at the clip's end hidden,
    0x730650 (the controller's display of rescued bots), phase 3 -> state 9.
- Verified (port `beacon-level botreplay` against run roof_bot5, frames 2801-3800, rig at the
  stopper (0, 1.15, -3.9)): states 1 -> 2 -> 7 -> 8 -> 9 on the same updates, position within 0.3 mm
  over the whole flight, scale identical, rotation within the recording's 4-decimal precision.
  The jump law in phase 2 (0xa5a940) is fitted, not decoded (0.1 mm).
- As a trigger (slot +0xa0, 0x733820): with TriggerOnGet (+0x3c80) "on" from reaching the
  controller (+0x4919, set at the jump's arrival), otherwise from the kick (+0x2180); with +0x4846
  the attachment's state (0x55c670). ROOFTOPS: the first bridge (`turn_path_root|turn_path_root|
  moj_L_turn`, OneShot, StartTrigger -> lost_bot_0) turns once the first bot is in the controller;
  its children follow with TriggerType Parent.
  Verified 2026-10-15 (run roof_bridge1, joint id ee33f426, state +0x2c50, progress +0x2c54,
  against `beacon-level bottest`): bot reaches the controller (+0x4919) at 3514; joint state 1 at
  3515, state 2 at 3516, progress 1/60 at 3518, 160/60 (the clip end) at 3677, held from then;
  bot state 9 at 3634. The port has the same offsets (state 1 at 251, 2 at 252, first step 254,
  end 413, bot state 9 at 370 = state 1 + 119).
- Port: game/LostBot (logic), app/LostBotView (bot, flag, jetpack, stand-in controller). The
  touch and kick tests are geometric (sensor cylinder / kick sphere against the box of
  mup_arbot_col: half extents 0.25 / 0.35 / 0.35 x scale on AR_Root, bind height 0.388 x scale).
- Not ported: SpawnType Trigger (state 0, SpawnTrigger / SpawnDelay), ActionType actions (happy
  animation, fall to locator, jetpack fall, mine cart; states 3-6, 0xa-0x12), FindTrigger, in-water
  variants, AttachmentLink, flag in the left hand, the stage-progress bit (already rescued bots),
  rescue effects and sounds, the controller's display of rescued bots (0x730650,
  BotsInController*), hits other than the kick (type 5 bazooka, 0xb-0xd kick reactions).

## Treasure boxes (`treasure_box`, recovered 2026-10-15; decompiles `tbox.c`, `tbox2-4.c`, `breakable*.c`, `coin*.c`)
Class vtable 0x16d0de0, creator 0x970cc0 (descriptor 0x2f09a00, initializer 0x976800), constructor
0x970d00 on the breakable object base 0x391ea0 (vtable 0x168d5b8; properties 0x399700). Setup
0x9716d0, update 0x974010, hit slot 0x210 = 0x394a10, damage +0x368 = 0x9726b0, break 0x395600.
- Properties (value = property + 0x90): BreakableObjectType +0x1eb0, BrokenModel +0x2130,
  BrokenEffect +0x2210, ParameterSet +0x22f0 (forced to 1 TreasureBox), NumCoins +0x2490,
  TvHeroAttackable +0x2540, VrHeroAttackable +0x25e0, HoverAttackable +0x2680, Syuriken*
  +0x2720 / +0x27c0, EnemyAttackable +0x2900, ThrowTarget +0x29a0, Bazooka +0x2b20, BigTreasureBox
  +0x2c60, BreakTrigger* , ReactionToWater +0x3390. Treasure box: Color (Green, Red, Yellow, Cyan ->
  models mup_co_treasure_box_green / _pink / _yellow / _blue and _<c>_break, strings in that order at
  0x128ff7c), BigTreasureBox, Bazooka.
- Body: self 0xd1041a1f (virtual +0x220 true), target all (0x392570); the hero stands on it.
- Idle: the idle clip from a random time, looping (RandomAnimation, 0x971d30).
- Hit (0x394a10): the type's flag (1-3 TvHeroAttackable, 4 HoverAttackable, 5 Bazooka, 6 shuriken,
  0x10 VrHeroAttackable, 0x11, 0x12, 0x19/0x22/0x2e/0x53/0x57) -> stored, damage via the parameter
  table (0x3940f0) and +0x368. 0x9726b0: cooldown +0x3908 (UnbreakableTime) and the stack links
  (upper / lower boxes, HorizontalGroupDistance / VerticalGroupDistance), then 0x395600.
- Break (0x395600): once (+0x35c8, long index 0x6b9); model and physics off (0x6f0a20 / 0x6f0860); BrokenModel pieces
  (0x396270 each: restitution, velocity 0, active, filters self 0x40000 / target 0x4040810, a
  random push in ConeAngle with HorizontalForce / VerticalForce x 0.1 / (0.1 d + 0.1) for one
  1/60 s step, a random torque x RotationForce); BrokenEffect; coins: treasure boxes always,
  CoinNumTreasureBoxSmall 5 / CoinNumTresureBoxBig 10 (coin tuning +0x9a / +0xb0 x 8).
- Coins (0x3d5560 -> coin 0x3d3ad0 + mover 0x3d5930 at coin +0x26f0, vtable 0x16904a0, update
  0x3d60f0): start at the hit point (coin +0x2950 = mover +0x30; runs roof_box1/2, both boxes);
  spawn AutoGetSpawnDelay apart; the first step comes the update after the break (t = mover +0x48,
  += AutoGetSpeed (+0x4c, 1.5) / 60: 40 updates; coins start 0, 4, 9, 14, 19 updates apart); a
  quadratic curve start -> start + AutoGetImplusePower (+0x50, 2) x launch -> the hero's centre
  (feet + 0.5 scale up); past 1 the coin is taken (0x3d5070 -> counted). Launch (mover +0x20, unit
  x AutoGetVelocityBase): 0x395600 takes the hit direction's horizontal part, else box - attacker
  with y 0, else (0, 0, -1), and turns straight up towards it by the angle at parameter set
  +0xdb0 (deg -> rad, quaternion); 0x3d5930 then adds random angles (heading 0..2 pi and a noise
  angle). **[fit]** the 20 coins of the two runs lie within 13.5 degrees of an axis 14 degrees from
  up towards the box (away from the hero): ported as a 15-degree lean plus up to 15 degrees of
  spread. BreakableTreasure has AngleBase 45 / AngleNoise 15; which entries 0x395600 / 0x3d5930
  read is not mapped (the parameter set's C++ order differs from the XML order: Speed is read at
  +0x610, ImplusePower at +0x140, SpawnDelay at +0x350 of the coin block at +0x850).
- Pieces (0x396270), decoded 2026-10-15: push = HorizontalForce x hit direction + VerticalForce x
  up (the parameter set's first virtual, 0x39b1c0: +0x150 / +0x200); per piece turned by random
  angles up to ConeAngle (none for the first piece), x 0.1 / (0.1 d + 0.1) (d: piece to hit point),
  applied at the hit point for 1/60 s (0xca6680: torque r x F, force x world scale), then a random
  unit torque x RotationForce (0xca6510); restitution 1. The hover's push is straight down: the
  pieces hit the box or floor below in the same step and bounce up (run roof_box2, piece states
  through +0x35d8 > piece > +0x158 > handle > +0x48: about 3 m/s up, 0.3-3.7 m/s across, 3.0-4.9
  m/s in all, 6-73 rad/s; library units / 3). The port does the same since 2026-10-15 (2.9-4.9 m/s,
  2-4 up); before, it pushed radially across at 5.5-6 m/s. **[guess]** the cone's two random turns
  (axes across the push).
- Port: game/TreasureBox, app/TreasureBoxView, LevelScene (bodies, hits, pieces, coins). Checked
  with `beacon-level walktest` (punch at box6: broken, 5 coins; hover over the box3-stack: all
  three broken, 15 coins). Not compared with a capture.
- Stacks (0x9726b0 / phase machine +0x37b0, phase 2 update 0x973eb0): when a box breaks, the boxes
  above are relinked and fall by the gap between it and the box right above it (each box the
  same drop); the box right above starts at once, each higher one 0.05 s (0x128ff58) later; each
  is unbreakable for UnbreakableTime (+0x3908, 0.2 s; set on each relinked box in 0x9726b0, hits
  skipped while > 0 at its start). The hover laser's rays stop at the first body, so under the
  hover a stack breaks top to bottom and nothing falls; the fall needs a lower box broken from the
  side (punch, kick, bazooka). **[guess]** the 0.2 s keeps the same attack from also breaking the
  box that drops into it.
  Hover breaks, captured 2026-10-15 (runs roof_box1 / roof_box2, `scripts/roof_box1.txt`: after
  the bot, over the bridge, hover over the cyan stack box25/box26 at (-0.733, -10.963)): top box
  broken (+0x35c8) at 4125, the lower one at 4126 in both runs - one update apart, top first; no
  timer, the laser meets the lower box once the top one is gone. Whether a second toe ray in the
  same update already passes a box broken by the first is not settled (0x6f0a20 stamps the
  manager's frame at +0x1c54 - a deferred request - but its effect on the body is not followed);
  the port turns the body off at once (walktest over box16/17/34: 16 and 17 in one update).
  Toe positions: the hit points (coin starts, below) sit 1.5 cm either side of the hero's centre
  and 1.2 / 0.3 cm ahead; the port's hover toes (hover_01 clip, toe_left / toe_right) sit 3.4 cm
  either side. With the original's hero track (`scriptwalk`, BEACON_SCRIPT_PLACE) the port's
  right toe starts 6 mm inside the top box's rounded edge, misses it (a ray starting inside a hull
  misses, as in the library) and breaks the lower box first (4125), the top one falling and
  unbreakable until 4138. **[open]** where the original's toes come from (0x69a890: joint +0x30 x
  the model's world matrix - so the pose or blend differs, not the rule). The fall is scripted, not physics: the
  speed grows by 9.8 / s; reaching or passing the target it bounces at -0.2 x the speed, the
  third time it snaps (phase 1). Ported 2026-10-15 (walktest: the stack at x -2.585 drops 0.2016,
  the upper box 3 updates later, settled in ~18 updates). **[hypothesis]** the stack links:
  the nearest box above within HorizontalGroupDistance across and VerticalGroupDistance up (the
  link-building code not followed).
- Not ported: effects (ef_treasure_box_hit / _break),
  sounds, shuriken / water / VR-hero hits, BreakTrigger.

## Spitter (`inflate_enemy`, model mup_ch_enemy_spitter; decoded and ported 2026-10-15)
Decompiles `infl.c`, `infl2.c`, `infl3.c`, `inflctor.c`, `inflstone.c`, `infltune.c`.
- Class: creator 0x6b7450 (descriptor 0x2ef0b40, initializer 0x6cc7b0), constructor 0x6bf440 (size
  0x3cc0, kind 0x7e5) on the enemy base 0x606e90; vtable 0x16b5048: setup +0x20 0x6bff00, update
  +0x40 0x6c1e50 (base 0x608510, +0x378, +0x388, 0x6c2040, +0x330, +0x338, springs 0x6bd760), hit
  +0x210 0x6c9120, progress +0x348 0x6c9dd0, can attack +0x358 0x6c9e20, lost target +0x360
  0x6c9e90, state step +0x378 0x6c3350, set state +0x380 0x6c4ed0, attack step +0x388 0x6c5ce0,
  set attack +0x390 0x6c6840, shoot +0x398 0x6c6170, look-at +0x3a0 0x6c6a70 (not followed),
  shot delay +0x3a8 0x6c8620, shot source +0x3b8 0x6c8aa0.
- Attributes (binder 0x6ca290, block +0x2570): Hitpoints +0x2600, StartScale +0x26b0, EndScale
  +0x2760, StartChargeTime +0x2810, EndStartTime +0x28c0, Start/EndStoneScale +0x2970 / +0x2a20,
  Start/EndStoneSpeed +0x2ad0 / +0x2b80, Start/EndCooldown +0x2c30 / +0x2ce0, Start/EndJumpHeight,
  AttackTarget +0x2ef0 (VrPlayer 0, NearestHero 1), VrTargetAimOffset, SpawnType +0x3270,
  LaunchJump*, SpawnWarpSpeed +0x35d0, InvisibleStandby, AttackLimit +0x3720, ColorType +0x37d0.
  Tuning: property/inflate_enemy.xml (singleton 0x6bdac0; value offsets in game/enemy/Spitter.hpp).
- Progress p = 1 - (hp - 1) / (max - 1) (0x6c9dd0): every Start..End pair is lerped by it.
- Clips (0x6c0cf0, play 0x6c4d10): 1 idle, 2 charging, 3 attack, 4 shaking, 5 explode, spawn
  (run roof_spit3: id 15 while spawning), 6 / 7 damage_hit_light (+ _mat layer), 8 / 9 water_hit.
- States (+0x3c04, timer +0x3c08 counting down): 1 spawn (Warp: the spawn clip, 79 updates in run
  roof_spit3, the attack timer set to ~StartCooldown), 2 active, 3 squash (from A = the old size to
  A x (RetractXz, RetractY, RetractXz) over RetractDuration), 4 grow (to B x (ExcessXz, ExcessY) over
  GrowDuration; B = lerp(StartScale, EndScale, p)), 5 settle (to B over ShrinkDuration) -> 2, 6 shake
  (EndScale x FinalScaleExcess, ShakeDuration) -> 9, 9 explode (clip 5; when it ends +0x3802: gone).
  The drawn size (+0x3810) follows the state's (+0x3880) at Inflation Smoothing per 1/60 s
  (0x6c23a0).
- Attack (+0x3c0c, timer +0x3c10; 0x6c5ce0 / entries 0x6c6840): 0 waits for a target (chase trigger
  on and attacks below AttackLimit); 1 aims, cooldown running; 2 charging (clip 2, lerp(StartCharge,
  EndStart, p)); 3 attack (clip 3, count +0x3cb4, ShotDelay 0.53 s, then the shot); at the clip's end
  the cooldown lerp(StartCooldown, EndCooldown, p), idle (blend 0.1), back to 1 (0 without target).
- Hit (0x6c9120): punch / kick (1-3, once per attack id 0x6c8dc0), hover laser (4, with
  CanBeDamagedByHeroHover), 5 by damage; invincible InvincibilityDurationOnPunch after each; hp -1,
  then state 3 (9 at 0 hp). The push spring (+0x3a.., PunchYPush*) jiggles the model (not ported).
- Shot (0x6c6170): a spawn request (vtable 0x16b5678) with start = ShotSourceOffsetFromFace in
  face_root_joint's frame (0x6c8aa0), target = the VR player's head + HmdTargetOffset (+
  VrTargetAimOffset), speed and scale lerped by p. The stone (start 0x6b9c60, update 0x6bbc10): a
  rigid body (mup_ch_enemy_spitted_stone_col: sphere r 0.2 x scale; mass scale x MassAtScaleOne;
  filters 0x20000 / 0x241b1f; no contact with its spitter), velocity (d.x / T, d.y / T - 0.5 g T,
  d.z / T) with T = |d| / speed and g = Projectile Gravity (-3); in flight gravity then the move each
  update, contacts read after the world step (0x6ba690): a hero (hit type 0x19 from the stone along
  hero - stone, normalised, to 0x66faf0 = Damage, hero.md; then the bounce as below; ported
  2026-10-15, not yet captured),
  the VR player (the gum, vtable 0x16b5548: ef_vr_screen_spitter_gum on the view per AttachOnHmd,
  clips attach / slide), else a bounce (0x6bb7d0, BounceAngle / BounceVel multipliers, ForceBounceUp)
  after which it falls as a physics body; life DefaultLifeTime, AfterHitLifeTime after a hit.
- Triggers: ROOFTOPS' spitter (2.12, 0, -27.73) chases while trigger_box tb_inflate_F_atk (0, 0.47,
  -27.9; MarkerType VrPlayer, MarkerJoint AR_Root) holds the VR player. **[hypothesis]** AR_Root is
  the avatar's root at the rig: the box reaches 1.48 m only. The rig reaches it at vr_player_stopper_03
  (z -26.34), after stopper_02 (z -3.9) is released by tb_stopper_02_off, which the rescued bot's
  joint (moj_stopper_02_off_appear, StartTrigger lost_bot_0) lowers onto the path.
- Port: game/enemy/Spitter (logic, stones, gum), app/SpitterView, LevelScene (capsule body: self
  0x1000 like the base enemies, CollisionRadius / HalfLength at the size - **[guess]** its placement;
  hits; stones; gum on the view). **[guess]** in the port: the stone's contacts by a sphere sweep along
  its move, the bounce mirrored about the surface (0x6bb7d0 not followed); look-at (0x6c6a70) not
  ported (the model keeps its placed heading); death effect and the large coin reward not ported.
  Checked against captures: attack timings and the stone launch - see "Spitter captures" below.
- Spitter captures (2026-10-15; harness det9 / det10: SHADPS4_BEACON_TELEPORT moves the hero,
  SHADPS4_BEACON_RIGZ the VR player's rig z; script scripts/roof_spit2.txt = the bot rescue, then a
  teleport into tb_stopper_02_off at 3700 and next to the spitter at 3760). roof_spit1/2: the rig
  stays at stopper_02, no attack. roof_spit3: the rig follows the hero to -30.37 (outside the box),
  no attack; at 3866 the level resets (state 1: the spawn clip, id 15, for 79 updates, the attack
  timer from ~2.0). roof_spit4 (rig pinned at -26.4, hero at (1.0, -27.6)): attack 1 at 3761, 2 at
  3762 (timer 1.000 = StartChargeTime), 3 at 3823 (61 updates; timer 0.530); the stone's first record
  3856 with its life 4.967 (made in update 3853: 31 updates after the attack began - the port shoots
  after 32, **[open]** one update), target (0, 1.147, -26.339) (the head at stopper_03), speed 3.5,
  gravity -3, velocity (-2.711, 2.232, 1.773) dropping 0.05 per update (the ballistic law within a few
  cm/s; the start point not recorded exactly); it met the hero at ~3862 and the hero died (one hit)
  -> level reset at 3866. The port: same charge (61), ShotDelay (32), launch law; the stone's start
  differs by up to 6 cm (no look-at turn). Not captured: bounce, gum, hits on the spitter.


## Jump boards (`jump_board`, MupJumpBoard vtable 0x16b5bc8; decoded and ported 2026-10-15)
Decompiles `jboard.c`. Descriptor 0x2ef0c40 ("mup_jump_board", initializer 0x6ce3a0), creator
0x6cd290, constructor 0x6cd2d0 (size 0x2300) on the physical object base 0x99e3e0: VerticalJump
+0x1eb0, JumpSpeed +0x1f50, AppearTrigger +0x1f70, AppearDelay +0x2140, AppearAnim,
FunnyHeroAnimation +0x22d0. Setup 0x6cd610: the bodies (0x99e8c0), self filter 0x10, target all;
without an AppearTrigger shown at once (+0x22e0 0), else appearing later (update 0x6cd730 states).
Every update 0x6cd890: each hero alive and on the ground (+0x14c == 1) whose ground body (+0x1b0)
is the board's gets the board's up axis x JumpSpeed; VerticalJump: 0x673650 -> 0x687c90 (hero
state machine): +0x82 = 1, +0x84 = FunnyHeroAnimation, +0x90 = (0, speed, 0), request Jump (3); the
board plays "bounce". Jump enter (0x4caf20) with +0x82: velocity (0, speed, 0) - the horizontal
speed dropped - no held-jump lift (+0x44 = 0), the clip 0x688aa0: jump_bumper_01, or with the funny
flag (previous + random 1..n) mod n of jump_bumper_02..04 (02..07 when 0x73b050 holds); +0x51:
Jump -> Fall below -1.0 m/s instead of 0.1. ROOFTOPS: the square board at (-1.79, 0.40, -27.49)
(JumpSpeed 7.4) and a round one below (-2.56, -2.12, -25.37; 7.8). Port: LevelScene (boards, bodies,
the bounce clip), Hero::boardLaunch. Checked in the port only (`scriptwalk`: on the board at 2821,
jump at 2822 with 7.25 m/s after one gravity step, apex ~3 m, Fall at -1.15 m/s). Not ported:
non-vertical boards (0x676af0), AppearTrigger / AppearAnim.

## Glass blocks (`miniblock_volume`, MupMiniBlock vtable 0x16a35d0; decoded, captured and ported 2026-10-15)
Decompiles `miniblock.c`. Descriptor 0x2eed3e0 (initializer 0x585c30), creator 0x57b490, constructor
0x57b4d0 (size 0x3100): Asset, VerticalDivision +0x1f70, Seed, DivisionNumberFilter (Default / Round),
Attackable +0x2190, BreakSpeed +0x2230, Spread (link), Jingle +0x2420, Crack +0x24c0, CrackPosition,
NumCrackBlocks +0x2620. Setup 0x57e460 (one body for the block, self 0x800), builder 0x57ca00, update
0x580690, per-cube update 0x580960, cube pose 0x57f8d0, hit 0x582840, break 0x582aa0. Tuning
<MiniBlock> in multi_platformer.xml (singleton 0x583ff0): Impluse 120, Horizontal / VerticalAngle
-30..30 / -60..0, Restitution 0.8, BreakRotation 35, DisappearEffectDelay 0.6; Default / Rock asset
scale and spacing. Cube models mup_bg_city_miniblock (unit cube; _brk not seen in use).
- Grid (0x57ca00; run roof_g3 cube objects +0x190, all 80 of ROOFTOPS' three walls): edge = box
  height / VerticalDivision (+0x2640: 0.1054); along each axis floor(size / edge) cubes, at least
  one; centres (i + 0.5) edges from the box's corner (the placement's origin) along its axes; volume4
  (0.683 wide) has 6 per row, volumes 3 / 5 have 5; rows across, then layers up.
- Hit (0x582840; any hit type while Attackable): the cube nearest to the hit point; 0x582aa0 stores
  the hit (position +0xc0, direction +0xd0) on the cube (+0x1ec count, first time only), marks the
  block broken (+0x30f0) and sets the cube's delay (+0x134; FLT_EPSILON for the hit cube).
- Per cube (0x580960): when the delay ends the cube's own body becomes active (friction 1,
  restitution 0.8, target 0x4000810), a force step pushes it (Impluse along the stored direction turned
  by random angles in the ranges) with a random torque, and its six neighbours and its link into the
  next wall (+0x1d0 / +0x1d8) get the same hit with delay 0.05 s / BreakSpeed; DisappearEffectDelay
  later it goes (effect). Touched neighbours wobble (0.5 s, sin(2 pi 20 t) x 0.01; not ported).
- Captured (run roof_g3, punch at 2930 from (-1.58, -26.92) towards volume5): volume5 breaks from 2935,
  the wave reaches volume4 at 2944 and volume3 at 2957 - one punch brings down all three walls; each
  cube flies 36 updates; every launch adds 2.000 m/s and spins at 116.54 rad/s. The launch directions
  (measured after a world step, contacts with standing neighbours included) lean along the punch.
  Port (`scriptwalk`, the same punch): volume5 first, volume4 6 and volume3 21 updates later; launches
  2.000 m/s.
- **[fit]** the spin: the port caps the cube bodies at 116.54 rad/s (the original's torque goes
  through a reciprocal not followed). **[guess]** the composition of the two random angles; the
  cross-wall links (nearest cube of another wall within 1.5 edges). Not ported: wobble, Crack,
  Jingle sound, the disappear effect, Rock assets.
- Hover over the walls: they stand under an overhanging concrete block (y 0.89..2.02), so they are
  meant to be punched from the walkway (run roof_g2's hover from inside that block did not reach them).

## The G group and the goal bridge (ROOFTOPS; captured run roof_g4, port 2026-10-15)

- Spawn: tb_inflate_F_death_chk (the spitter's death check) spawns enemy_standard_G_01 / G_02
  (fa6a426c / d5c0905c, LaunchDelayTime 3.0 / 3.4) and enemy_spiky_G_01 (0cb19db3, 3.6;
  InvisibleStandby, Jumpup to their locators, Hitpoints 1). The check fires one update after the
  spitter reaches its death state (state 9 at 3247, spawned at 3248), not after the explosion; the
  port now counts the spitter as gone from state 9. Launches 3429 / 3453 / 3465 (port: equal).
- Chase / end trigger tb_enemy_G_atk (Heros marker, centre (0, -0.05, -27.68)). Enemy behaviour:
  enemies.md "Spiky enemy".
- Bridge: tb_enemy_G_death_chk (trigger_box_for_enemy; MarkerType Object over the three enemies
  with ":1", NoArea, Inversion, Oneshot) -> StartTrigger of moving_object_joint_bridge1 (StartDelay
  0.5). **[hypothesis]** an object marker drops defeated targets (the port: removed enemies); in
  the port the bridge extends towards z -28.8 once the three are removed. Not captured: the
  original's run-up to the bridge (the hero died to the enemies in roof_g4).

## Respawn markers (`respawn_marker`, vtable 0x16c0238; decoded and ported 2026-10-15; decompiles `evidence/decomp/respawn`)
- Class: descriptor 0x2f02d90 (initializer 0x7ca7e0), creator 0x7ca720 -> constructor 0x7c9070
  (size 0x2160; +0x2154 = 1 for respawn_marker, 0 for respawn_marker_for_boss), registered with
  manager 8 of the main object. Attributes SpawnTrigger (+0x1cb0), ActivateTrigger (+0x1df0),
  DelayTime, SpawnDelayTime, HeroLife (hearts given on activation, 0 here). Model
  mup_co_respawn_marker (clips spawn 0.583 s, idle, activate 1.317 s, activate_idle, herorespawn,
  after_herorespawn 1.117 s, remove 0.5 s), physics mup_co_respawn_marker_col (a cylinder on
  checkpoint_base; the hero lands on its top).
- Setup 0x7c9530: without a SpawnTrigger "idle", state 3; else hidden (0). Update 0x7c9650
  (state +0x2150): 0 -> 1 when the SpawnTrigger fires; 1 -> 2 after SpawnDelayTime (shown,
  physics on, "spawn"); 2 -> 3 when played ("idle"); 3: activation - with an ActivateTrigger: it
  fires (+0x2142), DelayTime later and the VR player not warping; without one (and
  RespawnMarkerVisible): the hero alive (0x66ce40), the VR player not warping and the hero's z -
  0.15 below the marker's z (passed or within 0.15 m). Activation 0x7c9d80: every hero's respawn
  point = the marker's position (0x673890), the VR player's respawn (0xa5afa0: its position +0x520,
  the marker +0x538; the previous marker gets "remove", state 8, 0x7ca170), +0x2140 on (its links
  fire from the next update, +0x2141), "activate", 4 -> 5 "activate_idle". 6 ("herorespawn",
  set by the warp's black end 0x7ca390) -> 7 "after_herorespawn" after 3 s -> 5.
- ROOFTOPS: respawn_marker (0, 0.054, -3.43) stands from the start; respawn_marker1 (2.063, 0.382,
  -13.58) spawns on it, respawn_marker2 (-1.736, 0.391, -18.59, ActivateTrigger: three trigger
  boxes) on marker1, respawn_marker3 (1.976, 0.011, -24.51) on marker2. Runs roof_g5 / g6 (hero
  teleported to z -27.35): marker and marker1 activated by position, marker2 waits for its
  boxes -> the respawn at marker1. The port's markers follow (`scriptwalk` BEACON_SCRIPT_RESPAWN).
- **[hypothesis]** the collision filters (a level object's: self 0x10, target all); the marker
  clips' effects not ported.

## Movie prompts (`movie_prompt`, vtable 0x16a4608; decoded and ported 2026-10-16; decompiles `evidence/decomp/prompt`, `holo`)

Tutorial boards: a HoloBoard device holding a hologram board that plays a movie.
- Constructor 0x58b560 (attributes PromptType, MovieName, ShowTrigger, HideTrigger, ShowDelayTime,
  ShowDelayTimeAfterClearing, Location Level / TouchPad, HookRingLink, DisplayOut, LostBotLink);
  setup 0x58bb10 loads `prompt_board` (the lox resource) hidden and creates a HoloBoard
  (0x2009e0, type 1, size 0x110); 0x58bd20 resolves the links.
- Update 0x58c3e0 (state +0x2754, timer +0x2750): 0 wait for ShowTrigger (empty = fired, 0x58cd60)
  -> 1 delay (ShowDelayTime +0x2160; ShowDelayTimeAfterClearing +0x2210 on a level cleared before,
  0x73b100) -> the device placed at the prompt (getters +0x58 / +0x68 / +0x60) and opened -> 2
  until the device's state is 2 -> the movie starts (0x58ce00: `data/common/video/` + MovieName,
  `.mp4` added), the board shows (0x772130(p, 1, 1)), clip `show` -> 3 `show` ends -> `idle` -> 4
  -> hide request -> 5 timer -> after 0.5 s (0x120c014) `hide` -> 7 `hide` ends -> 0x58c250 (the
  movie stops), board hidden, device closed (0x2035f0) -> 6 the device's state is 0 -> 8 (done;
  back to 0 with the flag +0x4e8). A hide request during the delay -> 0 (shown again once the
  player stops for the delay).
- Hide request (+0x27ba, outside 0 and 8): HideTrigger fired, or 0x58c9a0 by PromptType (one
  hero state each, no fall-through in the assembly): HeroMove Walk (2), HeroJump Jump (3),
  HeroHover Hover (6), HeroPunch Punch (8), HeroThrow Throw (0x15); LostBot and the others test
  other objects (not ported); the movie `prompt/mup_prompt_hero_from_ds4` also hides with +0x27bb.
- Movie on the board (0x58cf50): a video player named `video_board` bound to the board's texture
  node `/file1` (0x789ee24b, TEXI row 0 "A_PT_Effect_color", the colour of video_board_mat),
  set to loop (0xbf8f90(player, 1)); with a DisplayOut link to a public screen (type 0x1b6b) the
  movie goes onto that screen too (not used in ROOFTOPS).
- HoloBoard (0x2009e0, MupHoloBoard.cpp; tuning property/holoboard.xml): model mup_co_holoboard
  (0x201280), type 1 one sub-board `mup_holoboard_board_noborder_temp` (locators only; types 2 / 3
  more, with orbs mup_holoboard_orb_temp). Open 0x203300: device `open_to_idle` (open then
  open_idle), sub-boards `open`, state 1; update 0x2019d0: state 1 -> 2 when a sub-board's open
  clip has ended (0.233 s). Close 0x2035f0: device and sub-boards `close`, state 3, timer from the
  tuning (+0x1e20); state 3 -> 0 when the clips have ended (the device's close 0.65 s): hidden.
- Movies: H.264 640x360 at 60 frames/s, no sound (ffprobe). The harness's emulator does not decode
  them: its boards show the empty blue screen.
- Captures: run roof_tv1 (roof_box1 route, shots every 100 updates, fast mode): boards at the start
  (f2800-2900) and the hover board at the bridge (f3100-3350; port 3138-3337 on the same route).
- Port: game/MoviePrompt (states), app/MoviePromptView (device, board, movie), video/MovieDecoder
  (AVFoundation on macOS; none elsewhere), render/MovieTexture.
- **[open]** prompts on the controller's touch pad (Location TouchPad; 0x31760 binds `video_board`
  to a node of the controller's prompt model); ShowDelayTimeAfterClearing (needs save data); the
  LostBot / gadget / headbutt hide conditions; the repeat flag +0x4e8; the HoloBoard's close effect
  (timer +0xe8) and its other types; the board's world matrix (vtable +0x120; port: the placement).

## Public screen (`public_screen`, MupPublicScreen vtable 0x16bf3a0; decoded and ported 2026-10-16; decompiles `evidence/decomp/ctrl/007b*`)

A TV (Odx mup_co_public_screen_tv; clips on / off / idle_on / idle_off) with a screen quad
(mup_public_screen) at its joint `screen` (0x55c54c11, placed every update by 0x7be0f0).
- Setup 0x7bd500: the quad's texture node `/file1` (0x789ee24b, mup_tex_tile) gets the
  public-screen picture (0xf946b0; without ExternalDisplay also 0xf94b70 with 0xc2c390); the quad's
  x scale (0xbca9b0) is 1 with ExternalDisplay, else -1 / 1 by Flip (0x124c840). An empty
  TurnOnTrigger starts it on unless ExternalDisplay.
- Update 0x7bdd00 (state +0x2150): TurnOnTrigger fired -> request (+0x214c) 1, TurnOffTrigger
  fired -> 0. 0 off (idle_off): request -> quad shown, `on`, the picture on (0xc4fc70(main, 1)) ->
  1 `on` ends -> `idle_on` -> 2 -> **[hypothesis: the decompiled branch is empty]** request
  cleared -> `off` -> 3 `off` ends -> quad hidden, `idle_off`, picture off -> 0.
- ROOFTOPS: one TV (Flip, VrOnly, ExternalDisplay) on the bridge section; on when the three
  standard enemies above it have left trigger_box_tv_on (BeInside, inverted, NoArea: defeated),
  off at trigger_box_tv_off further on. Run roof_tv1 (no fight) shows it off.
- The picture: the level's follow_camera (`|public_screen_camera|follow_camera`, OffsetPosition
  (-1, 0.34, -1.75), TargetOffset (0, 0.3, -0.75), Fov 30, limits, LimitBox tv_camera_limit_box).
  Its property set is registered by 0x1901a0 (many presets, singleton 0x18ea30); the code that
  moves the camera is not decoded.
- Port: game/PublicScreen, app/PublicScreenView; LevelScene renders the camera's view (640x360,
  own FrameLighting) into the screen's texture before the main view while a screen is on.
  `BEACON_DEBUG_PUBLIC_SCREEN=1` turns the screens on.
- **[guess]** the camera: hero + OffsetPosition looking at hero + TargetOffset, vertical FOV
  FovMin; **[open]** the camera rules (Follow axes, limits, limit box, FOV margins, SwitchTime,
  VrPlayerTransparency), the picture's resolution, whether the original shows the previous frame.

## Controller stand-in and bot counter (2026-10-16; decompile `evidence/decomp/ctrl/005f8220.c`)

- 0x5f8220 (the controller's set-up) loads mup_controller, mup_controller_transform and the
  counter mup_ui_num_bots_ds4 (controller +0x30), and finds button shapes by name.
- The counter: two additive effects in the controller's model space at the PS button: a bot icon
  (mup_ui_ds4_counter_bot) and a digit quad covering one cell (u 0..0.2, v 0..0.5) of
  mup_ui_padlock_counter_num (0..4 over 5..9). Run roof_pad5 (pad=0,-0.25,-0.15, head pitch -40)
  shows the original's controller with the icon and 0 on the PS button; the port lacked it.
- Port: LostBotView draws the counter with the controller; the digit by the rescued count through
  the texture node `file1`'s UV offset **[hypothesis: the code setting the digit is not found]**.
- **[open]** mup_controller_transform and the controller's clips (idle / idle_opaque, touchpad
  open for prompts / lost bots); the port's stand-in plays no clip at rest (matches the capture).

## Controller touch pad during a rescue (2026-10-16; run roof_padbot; decompiles `evidence/decomp/ctrl/005f*`)

- The controller keeps a count of bots on their way (+0x198): +1 when a bot is kicked (0x5f1820,
  also from the goal 0x48b870 / state 0x36), -1 0.4167 s into the bot's
  in_controller_landing_out (0x5f1860). Its state machine (requests through 0x5f0310): 0x5f18e0 /
  0x5f6440 request 6 (10 with +0x19d) while bots are counted, 0x5f2380 / 0x5f7580 request 8 when
  the count is back at 0 (9 with +0x19c). 0x5fbcf0 (a state's entry) builds
  touchpad_open_for_lost_bot (touchpad_open, *_underwater variants by +0x58 / the water flag).
- Run roof_padbot (roof_bot5 with pad=0,-0.25,-0.15, head pitch -40, shots every 20 updates): the
  touch pad is open with a bright glowing inside while the bot flies in (3400-3500); bot_impact
  as it arrives (3520) and the counter goes 0 -> 1 at that moment; the bot stands in the pad
  (3580-3600), shrinks away (3620); the lid closes (3640) and is shut by 3660.
- Clip channels of the controller: alphaScale on touchpad_matSG (open: 1 -> 0.45) and glass_matSG,
  emissiveScale0 on glow / square / cross / circle / triangle (bot_impact: up to 4).
- Port: LostBot reports the kick and the leave (takeKicked / leftController), LevelScene counts,
  LostBotView plays touchpad_open_for_lost_bot -> open_idle, bot_impact -> open_idle on arrival,
  touchpad_close at 0 (held); the counter counts arrivals; regular materials now take clip alpha /
  emissive channels (MaterialChannels). **[hypothesis]** the clip per controller state (the
  states' table is not decoded); touchpad_close held instead of the definition's
  touchpad_close_for_lost_bot sequence (its gadget step ends in a pose the capture does not show).
- **[open]** the glow inside the open pad (no clip channel lights it; the touch pad's emissive
  colour is 0: set by code?); bots in flight more than one at a time.

## Goal (`goal_balloon_base`, vtable 0x1697b58; platform ported 2026-10-16; decompiles `evidence/decomp/goal`)

- Constructor 0x486e10 (FlagAppearTrigger, FlagAppearDelay, StartDelay, MoveMode Spiral / UpDown /
  Side / Cross, Height Low / Mid / High, BossType, NoPrize, TriggerOnBreakCapsule,
  NoPrizeHeroWaitTime, WhaleSplashStartTrigger); setup 0x487490 (joint_stage 0xf3a18a66 at +0x26e8);
  update 0x487dc0; sequence 0x488f30 (state +0x266c: 1..6, 0x32..0x39; next state 0x48b870);
  tuning <GoalBalloon> (singleton 0xae2e0 at 0x2ed2c68; values at property + 0x90: PumpSpeed
  +0x960, PumpSpring +0xa10, PumpDamper +0xac0, PumpDepth +0xb70, VrPlayerZPos +0x2220).
- Bodies (mup_co_goal_balloon_base_col): a hull at joint_root (top 0.388) and a cylinder at
  joint_stage (y 0.257, r 0.478, half length 0.118: top 0.375). The hero stands on the cylinder:
  run roof_goal1 (teleported to (0, 0.6, -32.17) at 2900) rests at 0.347854 = 0.375 + the
  placement's y -0.0267; the hull would hold it at 0.361. CollisionRadius / CollisionMaxHeight /
  CollisionMinHeight are not read by any code found.
- On the stage (0x488330: grounded, support = the goal's body) the pump (0x4883f0 -> 0x487dc0):
  v = PumpSpeed; v += (-d PumpSpring - v PumpDamper) dt; d += v dt; d in 0..1 (rising past 1:
  v = 0; falling below 0: off); joint_stage lowered by d PumpDepth (joint override 0xbcb6b0); the
  clip hero_on. Further landings spawn balloon objects (vtable 0x1697a28, count +0x265c).
- Port (game/GoalBalloon, LevelScene::loadGoal / stepGoal): the bodies keyframed, the stage body
  lowered with the pump, the hull kept out of the robot's queries **[hypothesis: its filters]**.
  Equal to run roof_goal1 on every update of the landing (0.6 -> 0.1979 -> 0.347854, to 1e-4).
- Harness: the hero teleported far ahead is flown back to the VR player after ~4 s (the rig is
  held by vr_player_stopper_01..03 until the hero enters tb_stopper_0N_off; SHADPS4_BEACON_RIGZ
  does not move the player's view). Run roof_goal4: roof_box1's route, then hops through
  tb_stopper_03_off onto the goal.
- Flag (0x48ec30, vtable 0x16980c8, model mup_co_goal_balloon_flag: appear / idle / disappear /
  pole_disappear; state +0x1e20; a sensor body self 0x40000 target 0x100; a pole spring +0x1e70 /
  +0x1e80 with tuning +0x5030 / +0x50e0): with an empty FlagAppearTrigger it shows at once
  (idle, state 2) and the base plays checker_rotate (0x487490). The landing check needs the flag's
  state > 1. Port: drawn at the goal's frame **[hypothesis]**, idle.
- Run roof_goal5 (roof_box1's route; teleports 4200 (0, 0.3, -22), 4300 (1.55, 0.3, -27.35),
  4340-4342 (0, 0.3, -29.2) through tb_stopper_03_off, 4344 (0, 0.6, -32.17); no input after the
  route; shots every 30 updates 4320-5580) - what the goal does without the player:
  - 4350-4380: the hero lands (0.347854); the flag lowers and goes, the pole stays; the stage lights
    cyan; the base's face shows eyes.
  - 4440-4530: a bubble balloon with a face (mup_co_goal_balloon_balloon) inflates above the pole
    and rises (BalloonInflateTime 0.2, BalloonFloatTime 1.5, BalloonGoalPositionLow (0, 2, -3)).
  - 4560: the base shows a counter, 0; 4620 the stage dips once more (0.2239), the counter 1.
  - 4650 on: yellow rings (mup_co_goal_balloon_ring) rise from the stage and circle up around the
    balloon (Spiral: SpiralRadiusMin/Max, SpiralRadiusCycle, SpiralSpeedAngle); the balloon drifts.
  - 4890: a movie board opens beside the balloon (a prompt: **[guess]** how to pop it).
  - ~4770: the trace's hero moves to (0, 0.99, -29.88) in front of the player while a hero figure
    stays on the stage **[open]** (which object the trace follows then).
  - Nothing more by 5580: the balloon waits for the player.
- **[open]** the rest of the goal: what pops the balloon (headbutt / rings), prizes (CoinNumPerRing,
  coin drop), the bots flying out of the controller (states 0x32-0x39, LostBotFly*), the count
  (count_start / count_idle / count_end), dance / bots_on, FinishTime 9, the result and the music
  (mup_goal_balloon_result_bgm); the base face's eyes and counter (texture or clip channels);
  the sequence state machine 0x488f30 (states 1-6 and 0x32-0x39) is only outlined.
