# Backlog

Open items of the port. Every item that is guessed or inferred instead of decoded from the
game's code is marked **[guess]**; items decoded but not ported are marked **[decoded]**.
Current milestone: ROOFTOPS (`world0_0_new_intro_1`) playable from start to finish.
Details and evidence live in the topic notes named in brackets.

## Milestone ROOFTOPS (in level order)

- Slide: done (2026-10-15, verified on run roof_slide1). Not ported: Punch -> Slide
  (0x4d0964), Walk's state 0x13 (0x66fa00), machine +0x80 on slide jumps, flag +0x2a5, the exit
  event 0x38e1d2d3, slope objects of class 0x13f6 (acceleration override), bird-boss variants;
  collide-and-slide skip list +0x460 and motion type >= 3; hover clearing a step edge loses an
  update in the original (2.2 cm) [hero.md "Slide"]
- `dynamic_grass`: idle sway and the bend away from the hero done (2026-10-15, weights exact on
  run roof_grass1). Not ported: attack bends (punch / spin attack / weapons, 0x3f26d0 /
  0x3f1740), other actors (bots, enemies: list 0x7397b0), LookAtHero, the rustle sound
  0x178bdcaf, AnimTrigger. **[decoded, not traced]** weights freeze when no hero is within
  1.19 m (the bend is not evaluated). **[guess]** the tuft index (+0x305c) is the document order
  among tufts (staggers the 10-update list refresh) [objects.md "Dynamic grass"]
- Placement clips: every placement whose Animation attribute names a definition clip plays it
  looping from AnimationStartTime (165 in ROOFTOPS: art_bg idle / idle2, dynamic_grass idle).
  **[guess]** start phase 0 where no AnimationStartTime (dynamic_grass); AnimTrigger /
  AnimName / AnimDelayTime (15 grass tufts link a trigger) not modelled; the base class 0x2583e0's
  own playback rules not decoded
- `lost_bot`: waiting / help / kick-ready states, the hero's kick, the rescue flight and the
  landing in the controller done (2026-10-15, run roof_bot5: same updates, 0.3 mm). Not ported:
  spawn triggers, actions (happy, fall, jetpack fall, mine cart), FindTrigger, water, the
  stage-progress bit, effects and sounds, the controller's display of rescued bots, kick hits on
  other objects (enemies). **[guess]** the kicked arc's wall cast is left out. **[guess]** the
  touch and kick tests are geometric (box of mup_arbot_col on AR_Root at its bind height) instead
  of physics contacts. **[decoded, fitted]** the jump into the controller (0xa5a940 not decoded;
  height 0.2 / gravity 15 fit to 0.1 mm). **[guess]** the turn's frame-time exponent taken as one
  60 Hz frame. **[guess]** the flag's and jetpack's attachment (their joint's bind pose inverted
  after the bot's joint; 0x726270 keeps a computed inverse, not compared with a picture)
  [objects.md "Lost bot"]
- First bridge (moj_L_turn, triggered by lost_bot_0 reaching the controller): turns on the same
  update after the arrival as the original (run roof_bridge1: arrival 3514, state 1 at 3515);
  the jolt at its start fixed (Parent-type children wait at their clip's start, run roof_bridge2).
  **[hypothesis]** no joint's clip plays by itself before its playback starts (objects.md)
- Telescoping bridge (moj_bridge): collision follows the segments' animated scale (0xbd4660);
  the seams' panic came from the short collision. Not checked against a capture of a walk across
- Treasure boxes: break on punch, kick and the hover laser; pieces, coins flying to the hero;
  stacked boxes fall (2026-10-15). Not ported: effects, sounds. **[hypothesis]** stack links by
  distance. Hover breaks, pieces and coins checked against runs roof_box1/2 (objects.md): pieces
  and coin timing/start ported; **[fit]** coin launch cone; **[open]** the hover's toe positions
  (original 1.5 cm either side, port 3.4 cm) and whether two stacked boxes can break in one update.
- Port enemies differ from the original on ROOFTOPS' round platform after the bridge (run
  roof_box1, ~4050: a port enemy stops in the hero's path, the original's walk elsewhere).
  **[guess]** the pieces' push (random spread in the cone, the force law
  0x396270 only partly followed), the coins' launch tilt; boxes and coins not compared with a
  capture [objects.md "Treasure boxes"]
- Hover laser (type-4 hits from the toes): done; enemies burn (DeadTimeBurned); beams drawn
  (rendering.md "Hover laser") [hero.md "Hover laser"]
- Hero Kick (state 0x33): done; not ported: its effect/sound (0x674890 / 0x5280d0) [hero.md
  "Kick"]
- Player controller (the bots' destination): **[port choice]** a stand-in mup_controller parked
  at rig + (0.35, -0.3, -0.4), upright, playing bot_impact on arrival **[guess]**; the bots land on
  its origin + 1 cm **[guess]** (the original's pad point 0x5f2870 not decoded); the controller's
  reactions (0x5f1250, 0x5f29e0, 0x5f1860) not ported
- VR player stoppers (`vr_player_stopper`): the rig stops at z = -3.9 in ROOFTOPS (seen in run
  roof_bot5); not ported, so the bots' flight target (relative to the rig) can be further ahead
- Spitter (`inflate_enemy`): ported 2026-10-15 [objects.md "Spitter"]. **[guess]** body placement,
  stone contacts (sweep) and bounce; not ported: look-at (0x6c6a70), push jiggle springs, death effect, large-coin reward, NearestHero target, spawn state 1 (starts active).
- Jump boards (`jump_board`): vertical boards ported 2026-10-15 [objects.md "Jump boards"]; not
  ported: non-vertical (0x676af0), appear trigger.
- MarkerJoint AR_Root triggers use the rig (port) **[hypothesis]**; vr_player_stopper not ported.
- Glass blocks (`miniblock_volume`): ported and captured 2026-10-15 [objects.md "Glass blocks"];
  **[fit]** spin cap, **[guess]** angle composition and cross-wall links; not ported: wobble,
  Crack, Jingle, disappear effect, Rock assets.
- G group / goal bridge (ROOFTOPS): spawn on the spitter's death state, launches verified (run
  roof_g4) [objects.md "The G group"]; **[hypothesis]** object markers drop defeated targets.
- Hero damage: ported 2026-10-15 (hero.md "Damage", enemies.md "Attack on the hero"): LightDamage
  verified (combat5), DeathOnSpike timing verified (roof_g5); open: Damage (11) not seen in a
  capture (the stone hit, the down-time weight of type 0x19 **[hypothesis]**), the headbutt lands
  ~3 updates early, the hero map's bodies **[hypothesis]** (foot only), the contact push of moving
  bodies (0x66e950: 1-3 cm per update in combat5), effects / sounds / blinking, water deaths, the
  other hit types. A stone hit needs the VR player in tb_inflate_F_atk: run roof_spit6 (no rig
  pin) fired no stone, runs roof_spit4 / 5 (rig pinned) warped the hero away before the shot.
- Respawn: ported and verified 2026-10-15 (hero.md "Respawn", objects.md "Respawn markers"; run
  roof_g6 equal to the update); open: vr_player_stopper (also clamps the warp target), the room
  reset flag, Vanish's vanish-effect wait, respawn effects, QuickRespawn / water, death_collision
  triggers (port: a fall below the level calls the death routine), **[fit]** the warp's skipped
  update, **[hypothesis]** the respawn point before the first marker.
- A hero placed inside a block: the port lifts it onto the block, the original pushes it out
  sideways (seen in the glass-block runs; not decoded).
- Test camera: rises with the hero (port aid, 2026-10-15)
- Remaining classes in the level, not yet looked at: `hanging_plant`,
  `bounce_platform`, `breakable_object` (the base is partly decoded with the treasure
  box), `reactive_object`, `death_collision`, `hero_no_respawn_area`, `respawn_marker`,
  `trigger_secret_icon`, `goal_balloon_base`, `vr_player_stopper`,
  `hero_stopper`, `tv_camera_limit_box`, `follow_camera` (rules; see the public screen), `vibration`,
  `sound_source` / `sound_controller` (`bgm_player` ported 2026-10-16, sound.md "Level music")
- Movie prompts: ported 2026-10-16 (HoloBoard device + hologram board + looping movie,
  AVFoundation decoding) [objects.md "Movie prompts"]; open: touch-pad prompts,
  ShowDelayTimeAfterClearing, the LostBot / gadget / headbutt hide conditions, the repeat flag,
  the HoloBoard's close effect and other types, the board's world matrix
- Public screen (TV): ported 2026-10-16, on / off by its links, the screen shows a second view
  [objects.md "Public screen"]; **[guess]** that view's camera (hero + OffsetPosition looking at
  hero + TargetOffset, FovMin); open: the follow-camera rules, the `off` branch
  **[hypothesis]**, picture size / previous frame
- Goal: platform ported 2026-10-16 (stage collision and pump equal to run roof_goal5 / goal1 to
  1e-4, the flag); open: the whole goal sequence (balloon, rings, popping, prizes, bots out of
  the controller, count, finish, result) [objects.md "Goal"]
- Controller touch pad: opens for kicked bots, closes after the last (run roof_padbot) 2026-10-16;
  open: the glow inside the open pad, the controller's state table [objects.md "Controller touch pad"]
- Controller: the rescued-bot counter on the PS button added 2026-10-16 (digit cell
  **[hypothesis]**); open: mup_controller_transform, the controller's clips
- Hidden visibility-group targets: art_bg models and physics off while hidden. **[guess]** that
  art_bg answers virtual +0xc8 with true (the arms and banana sign of the gorilla moment are
  hidden in the original) [objects.md]

## Hero

- Not ported: water / swimming, damage and knock-back (LightDamage 12 is requested by enemy
  attacks), hard landing, jump punch (9), charge attack, water and stacking punch variants,
  punch effects, carrying and its push, all gadget states, `teetering_collision` edges
  (NoTeetering), the hero's response to touching enemy bodies [hero.md]
- Fall / Hover exits and slopes: only partly read from 0x66ab90 (lines 560-1480) [hero.md]
- Ground probe's extra 0.2 m reach (0x12221f4): ported for moving supports (+0x1b8) and slides
  (+0x14c == 3); the probe's other flag +0x1b8 users not checked
- A wall-probe sample cast misses by 5 mm (perhaps a shape margin) [hero.md]
- **[guess]** Hero animation choice (HeroView): clips picked by state and speed, not the game's
  animation runtime. Also feeds the trigger marker (skeleton joint 2), so its path is
  approximate
- **[guess]** Punch: +0x4a start value unknown (port: off)
- Player rig / camera: AutoScrollX, MoveAlways, tv_camera / follow_camera, vr_player_stopper /
  warp points, the camera basis the stick is mapped with in VR

## Enemies (basic enemy)

- Spiky (0x7d4): launch, charge, attack and guard ported and verified (run roof_g4); spike damage
  ported; the additive attack clips and CoolDownTime / Standby / ChaseTime users not [enemies.md]
- Not ported: other kinds (flying, fish, rope, bosses), launch types Parachute /
  Panic, rope / waypoint / edge brain states, rewards and dropped coins, death model effect,
  survive path 0x2e2f30, hit types 4 / 9 / 0x13 / 0x15, Cruising reaction requests (event 2),
  Cruising water restart (+0x3c6c), grounded flag +0x73 (writer unknown) [enemies.md]
- Not verified: Path state (5) never seen in a capture; Stop during chases
- Standby: the root follows the placement's world frame; **[guess]** the fixed body stays at the
  spawn pose (not traced)
- Removal 42 updates after a defeat in the original, 59 in the port
- **[guess]** Defeat spin: decoded formula spins faster than the original's 60 rad/s about a
  fixed axis
- **[guess]** Fresh brain's choice between Turn and Walk (inferred from traces)
- **[guess]** Vertical root motion blended in from a clip without it counts in full (per-axis
  rule inferred from traces; the anim runtime's 0xbb5e60 / 0xbb5ec0 not decoded)
- **[guess]** Default chase / end trigger shape: cylinder of Radius and HalfHeight
- **[guess]** Walk speed ramp at the start of a walk modelled as the clip blend
- Replay differences: b959 trails 2 cm after Discover -> Cruising (late hop), switch to Fall one
  update early, take-off clips the obstacle; extra hops by enemies kept in Standby; torsos up to
  6.7 cm inside level body 1242; the launch's ledge contact (landing 0.104 m apart)

## Level objects

- Optimizer / streaming sections (SuspendBySection): mechanism not found. **[guess]** the port
  suspends targets of hidden groups (matches traces) [objects.md]
- **[guess]** A hero's support for RideOn joints read from the node extension's children
- Moving joints: optional filter on target position (+0x131 / +0x134) not ported
- Coins: dropped coins (`ef_coin_trail`) not ported; the hero mode check is not modelled
- Trigger marker list filter 0x66ce40 (hero +0x88 -> +0x18 -> +0x10 == 0) not modelled

## Physics

- Not ported: trigger pairs (reported by the library, skipped by the port), the contact report
  ignore set (+0x980), joint tables, the 0x60-byte list with flag 0x10 and the flag-0x20 body
  list, mesh paths for other layouts [physics.md]
- Unknown: the pair array's own order, the use of the 1.2 step constant, the world rebase flag
  and cell size, pair byte 2 and edge byte 2 masking in islands, the swept-contact open items,
  hardware rsqrt / rcp estimates, which pfb kinds 6 / 7 map to which shape types, PELM header
  byte 7
- **[guess]** World parameters +0x2764 (separation bias) and +0x2768 (iteration counts)
- Not verified: a whole world step against the original (only its routines are); pfb capsules /
  cylinders rolling on flat ground

## Rendering [rendering.md]

- Cloud sea: ported 2026-10-15 (rendering.md "Cloud sea"); far clouds within 1-4 %. Open: point /
  spot lights in the cloud shader, 4x samples in the transparent pass (port: single-sample)
- Where IblIntensity (0.87) applies: the sky probe the shaders read has intensity 1 (run
  roof_cloud_fs) **[open]**; recheck the shaded areas after the probe fix
- Static placements without a skeleton draw rigid meshes untransformed: models whose nodes have
  non-identity base poses would need the base pose (animation.md) **[open]**
- Sunlit gravel (mup_bg_city_concrete_block_d) 1.7x darker than the original: the normal-mapped /
  AO variant of the JsPBRSingle shader is not decoded; **[guess]** occlusion0 multiplies the
  vertex occlusion
- **[hypothesis]** vertex colour b = occlusion, a = alpha factor (vertex shader input 2 z / w)
- Detail maps (baseDetail0, normDetail0, roughDetail0, detailScale0, detailMapWorldScaling):
  parsed, not applied
- **[hypothesis]** cascade split and fit (log/uniform split, on-axis bounding sphere, texel
  snap, depth range); **[guess]** PCF tap offset 1 texel, DirShadowOffsetScale as slope bias,
  DirShadowWorldOffset as constant offset
- Bloom: composite pass not identified; **[guess]** level weights 0.25, levels 1/8..1/64, bloom
  added before the final output
- **[hypothesis]** final output: saturation matrix around the bloom luminance weights, exposure
  2^Exposure, exponent 1 / Gamma
- **[hypothesis]** sky cube name rule (mup_bg_ -> mup_bg_tex_ + _ibl_01); sky dome placement
  (port: around the camera, scale 1000; top of the sky up to 2x brighter in R than the original)
- Light Color attributes gamma 2.2 (engine constants); **[guess]** the same for ambient and IblColor
- Hover laser: ported 2026-10-15 (rendering.md "Hover laser"); beam colour (0, 0.41, 1) as
  dumped; open: the code path that sets it, the hover splash on water, player colours
  (idle_2..4)
- Glass: ported 2026-10-15 (rendering.md "Glass"); open: light lists, spot shadows, a
  comparison against an original frame
- Sun / lens flare: ported 2026-10-15 (rendering.md "Sun and lens flare"), small white star as
  in the original; open: the occlusion test (draw 522), the flare vertex shader's sky branch
- Decal shadow: ported 2026-10-15 (rendering.md "Decal shadow"), compared with run roof_hover1;
  open: **[fit]** footprint 0.7285 x 0.3979 (rule not found), box turning with the hero, no
  ground in reach, the blur's unequal steps
- Fog: not located yet (rendering.md "Fog")
- Effects (A_PT_EffectData): generic port done; open: soft depth, zOffset, transparent fog,
  billboard selection flag **[hypothesis]**, UV matrix composition **[hypothesis]**
- Not ported: distortion (+ lava, underwater),
  AlphaToCoverage, ZPrepass, transparent sorting, SSAO (off in ROOFTOPS), other material
  templates (JsPBRLayerData, ...)

## Engine and presentation

- One texture format, 12 clips with bit-packed channels
- Lighting and shadows: ported 2026-10-15 [rendering.md]; open items under "Rendering" below
- Particles (geometry shaders in the original), water: missing
- **[guess]** Animation playback: clips evaluated from the formats; layers, IK and events of the
  game's runtime not ported
- **[guess]** Music panel track grouping (heuristic)
- Sound effects and the audio middleware, save data, menus, hub, VR / OpenXR, Linux build

## Level music (bgm_player, 2026-10-16)

- **[inference]** the rooms' bgm lists (+0x388) hold the HeroSpawnLanding players (filling not decoded)
- The start's exceptions (owner flag +0x210, level hash 0x693c55c) and 0x6a09f0's timed spawn path
- The hero's spawn drop (the port's hero starts on the ground; "landing" = first grounded update)
- Vocal layer: level rules from TRNS ported (2026-10-16); missing: the hero's idle dances
  (0x6888b0 picks; 3 dance clips played to the music tempo) that set bgm_hero_state = idle,
  beat / bar quantization of level changes, 3D positioning of music slots 1 / 2 (0xc44e40)
- Interactive music states (TRNS conditions/variables) not driven by the game yet
- App: the menu's fonts are macOS system fonts (Avenir Next / Helvetica); bundle a font for other platforms
- Levels page (app): HEAVEN & STORM left out (hero placed oddly, no music in 150 frames),
  underwater levels left out (water rendering not ported), bosses left out
