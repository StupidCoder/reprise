# Spindizzy Worlds (Amiga) — plan

Remaining work towards a fully playable re-implementation, and the known
bugs and gaps. What has been learned about the original is in
[game.md](game.md); evidence (harness, traces, dumps, research tools) is in
`.analysis/spindizzy_worlds_amiga/`.

Goal (owner, 2026-10-28): a modable clean C++ re-implementation in the
Reprise shell, later with widescreen and smooth motion up to 120 Hz. So
levels stay data (built at run time from the world files, which mods can
replace), the playfield is drawn by our own isometric renderer from game
state with the camera in world units, and the rules tick at the original
rate apart from drawing.

## Done

1. **Import and assets** (2026-10-28). Importer (release by the SHA-256 of
   `main`, six ranges and the world block by SHA-256; worlds unscrambled),
   NEOchrome title, world files, level builder (`world/level_builder`);
   launcher previews; `spindizzy_worlds_assets import|export|gridcheck`.
2. **Block graphics and the isometric view** (2026-10-28): tiles, masks,
   pieces, palettes; `view/tile_map`, `view/playfield` (any window size).
3. **Sprites and objects in the view** (2026-10-28): every object kind's
   look and the original's depth order; six screen dumps pixel-exact.
4. **Explorer movement, surfaces, camera** (2026-10-28): `play/explorer`,
   `play/surfaces`, `view/camera`; 17 traces over 10 levels tick for tick.
5. **Front end, game flow, the Tutorial finished** (2026-10-29):
   - `game/front_end`: the attract pages (title, texts, block pictures,
     animated sprites), the menu (from the game's item list; "Start new
     game", "Continue", "Slow Mode?"), the Easy/Hard choice with its
     shrunken clusters, the turning world map with shadows, sizes and the
     chosen planet's name.
   - `view/text`: the game's text printer (its strings and control codes,
     glyph banks, sizes, transparency, boxes).
   - `view/panel`: the panel (SELECT.SWP), labels, world name, compass,
     the pillars (attached sprites) and the fuel gauge.
   - `play/objects`: jewels, lifts, exits (travel, places, gates), push
     buttons, binary switches, heads, secret passages, timers, messages,
     clues; `play/level_play`: level start and travel, jewels per level,
     fuel and its costs, clues, conditions and rebuilds, the dying kind.
   - `game/play_screen`, `game/module`: a world level by level from the
     world map to "Next world".
   - Checks: `playcheck` replays whole-game traces (`sdw.py playtrace`:
     A5+0 … $3800 every rules tick) through the native rules, every
     object slot included: Tutorial 1 → 4 → 1 (2,700 ticks) and the whole
     Tutorial from its start to its end, 12,849 ticks over 11 level visits
     (see game.md, "Evidence"); `drivefile` plays it through the module
     from the title to "Next world". The route (`route` tool) takes the
     four level-1 jewels (entering levels 4-7 and leaving them at once),
     then levels 8 and 15; level 4's jewel is covered by the shorter
     route1 record.

6. **Creatures, controls, game over and world end** (2026-10-29):
   boinks, Pac aliens, pyramids and spark generators (`play/objects`);
   the random generator as game state, including the original's sprite
   blits that clear its state word; turning the level (r/t) with every
   object's rotate handler; the keys F, c and p; the game-over offer;
   the statistics screen; the timer's seconds; the locked-world message.
   Records: 12 runs equal tick for tick (game.md, "Evidence").
7. **Sound, music, world end and the endings** (2026-10-29): the effects
   and music players (`sound/`), each tune started where the original
   starts it; the done planet breaking up on the map, the Easy and Hard
   endings; the deadly timer's end; the dying explorer's frames and
   "Game  Over"; "Reserve fuel collected"; the overview map (m) and the
   plain pause ([). 13 runs equal tick for tick, both tunes over 1,500
   ticks.
8. **Quick saves and the small differences** (2026-10-29): a quick save is
   the whole state (`state::Archive`, field by field: the screen and its
   own state, the level as it stands with its tile map, objects, explorer,
   camera, stars and timers, both sound players and their channels;
   about 330 KB in a level), never a replay. `drivefile`'s `check` loads
   it into a second module and runs both: equal in state, picture and
   sound in every stage tried. Then the stick byte $10A63 (changes only,
   fire taken by the waits, cleared by the menus and the dying kind), keys
   ending the waits, texts held over the picture on screen, +$18 and the
   clue search in drawing order (equal in all 13 records), the eaten
   Pac's score picture, palette objects (kind 16), the deadly timer after
   "Use reserve fuel", "Wind back world". Fixed after a report: buttons,
   switches, exits and sparks no longer take the jewels' "type 2: 8 up"
   placement (the blue type-2 button sat 8 pixels too high and covered
   an explorer behind it); Tutorial 4 and 6 dumps 0 pixels different.

## Remaining tasks

### Rules

- [x] Boinks (2026-10-29): bouncing, animation, touch (clue 2, $30 fuel,
      popping); traced without a touch (MisDirection 4, Roller-ama 4).
- [ ] A boink touch traced (its popping frames are drawn now).
- [x] Pac aliens rebuilt (walking, turns, the shared toggle, touch, eaten,
      popping, back at the start); the generator $112CA ($2094) kept in
      the game state, starfields draw from it at a level's start.
- [x] The generator overwritten by sprite drawing ($102F6/$101FE), kept;
      Tri Path 8 equal over 800 ticks.
- [x] Pyramids: walking, reversing, touch; Pak Attack 2/3 equal (touches
      in `play_w2_2t`).
- [ ] A Pac touch and a Pac eaten while the explorer flashes, traced (the
      eaten score picture is drawn now, from $157DC; not seen yet).
- [x] Spark generators (on/off by condition, touch: clue 9, $20 fuel a
      tick); Spindizzy! 1/2 equal, a touch to game over in `play_w22_1s`.
- [x] Turning the level (r/t; the builder's offset table $125A8 makes
      blocks and objects agree), traced in `play_turn`.
- [x] Keys F (slow mode), c (centre), p (pause, dimmed), [ (pause without
      dimming), m (the overview map, $1135A/$18DC6, with its 16 passes of
      the frame handlers).
- [ ] The overview map traced (a record with K6d and fire on the same
      tick: its 16 frame-handler passes change the animations and can
      clear the generator).
- [x] The deadly timer's end ($153EA runs $12CE8 on the timer's record):
      Roller-ama 4 ends in game over right after tick 641 as the original
      does (`playcheck` now says what the port does after a record).
- [x] Game over: the reserve-fuel offer ($18C14) with its three outcomes.
- [x] The dying kind's frames (list $12D56) and "Game  Over" ($12D68),
      "TIME OUT" ($153F6).
- [x] "Reserve fuel collected" with the per cent added ($15186); seen at
      the end of the Tutorial (its level 15 is a bonus level).
- [ ] The space-bar brake traced.
- [x] Palette objects (kind 16, $11124): the level palette ($207E) by a
      condition; there are no fades. No level uses one, so not seen.
- [x] The deadly timer after "Use reserve fuel" ($12B58): the timer's
      record becomes a dying explorer a pass ahead of the real one (not
      traced).

### Front end

- [x] The statistics screen ($16DA4) after a world and for "Check
      performance".
- [x] The done planet breaking up on the map after a world ($11838, kind
      −7, list $16B38) and the endings when every world is done ($17CBE:
      Easy's message, Hard's blown-up screen); the map's angles kept
      between visits ($166EC is never reset).
- [ ] The endings and the break-up compared with the original (needs a
      record that finishes a cluster, or $24AE poked to one world left).
- [x] Saves between worlds as the original keeps them (the cluster, worlds
      done and their records, clues, slow mode) in the shell's format
      (`write_state`/`read_state`); the menu's "Save game state", "Load
      saved game" and "Save game" after a world use a slot in it. A menu
      load shows the statistics, then the menu ($18AD0); "Save game"
      after a world shows its menu again ($18CC6); the shell's load goes
      to the world map.
- [x] Wind back world ($18B10: the last world done undone, $16D44, the
      map). Create Data disk ($10AB0) is left out: the shell's saves do
      its work.
- [x] The panel's clue boxes ($1635E/$163BE), equal in every record.
- [x] The timer's seconds on screen ($153C4) and the locked-world message
      ($1180A).
- [x] Menu input: each pass takes the stick byte and clears it ($18086),
      so a stick change acts once (fire first); the menus clear it when
      they open ($17E5A). Only the timeout depends on the pass rate (not
      frame-locked), guessed as one a frame.
- [ ] The menus' keys (cursor up/down, Return, digits for number items,
      $180B0) and the map's and Easy/Hard choice's reading of the stick
      byte (they still take the joystick as held).

### Sound

- [x] Effects: the player rebuilt (`sound/effects`), the game's requests
      and the distance-heard channel 2; every channel's effects equal in
      order (`play_route1s`).
- [ ] Effects not traced yet: the timer ticks, switch pads, creature
      touches, boink bounces, heads, sparks, pyramids; the sample-level
      output against a recording of the original's audio.
- [x] Music: the player rebuilt (`sound/music`); tunes 0 and 2 equal to
      the original over 1,500 ticks (`musiccheck`). Started where the
      original starts them: 0 by the title only when none plays (the
      module's flag $6F9), 2 on entering the map ($1170A) and after a
      world ($18A6A), 3 after "Abandon this world" ($18956), 1 after the
      ending ($17D6C); the title keeps 3 or 1.
- [ ] Tunes 1 and 3 compared with the original (`sdw.py musictrace`), and
      the sample output with a recording of the original's audio.

### Saves

- [x] Quick save and load of the whole state (schema 2); version 1 saves
      (between worlds) still load onto the map.
- [ ] Save size on handhelds: pictures held by the front-end screens
      (64,000 bytes each) could be rebuilt instead of stored.

### Enhancements (optional, off by default)

- [x] Widescreen up to 21:9 (2026-10-29): the level's view wider, stars
      repeated into it, pillars at the edges with the panel's pipes run
      on; the other screens in the middle. The camera keeps the
      original's targets (the explorer in the middle).
- [x] Smooth motion (2026-10-29): explorer, objects and view drawn
      between frames.
- [x] The VHS look (the framework's screen filter).
- [ ] Widescreen on the front-end screens (title, menus, maps): they are
      centred on black.
- [ ] Mod loading of world files.

## Known bugs and gaps in the port

- The front end's screens are checked by eye against reference screenshots
  and dumps, not pixel by pixel; the attract sprites' timing and the menu's
  flash rate are not compared.
- The random generator starts at 0 at boot (the attract and menus draw
  no numbers in the code read); not checked against the original after
  a long front-end session.
- Sprite colour 24 follows the hardware registers; not checked against a
  window capture (the pillars' columns 0 and 304 are).
- Not compared yet: the "lost" tops, the ice timeout, the brake, a lift
  top without its lift, the clue boxes; of the Tutorial, levels 6, 7 and
  14 played through (their buttons, lifts, gates) and level 5's
  switches (only played natively; levels 4 and 6 compared as single
  pictures with the explorer placed by hand).
- The texts held over the picture on screen use the last picture the
  shell asked for; a host that skips drawing a frame would hold an older
  one (the original's shown buffer is always the last frame).
- The Hard ending's eighth picture ($16B62 with d0 = 10, SELECT.SWP +
  $5140) lies past the file: the original blits whatever the level left
  in that RAM; the port leaves it out.
- The breaking planet's picture under its first four frames is drawn at
  the size of its own depth; $16AC8 uses d6 as the drawing loop leaves
  it (not traced).
- The timer's record remade as the dying explorer is counted after all
  objects' ticks, not in its own slot's turn (only the game-over tick
  could move).
- The [ key: the shell's key matrix has no [ (the module listens on '@',
  matrix 46, which the host does not map yet; a shared change).
- The stick byte is read once a tick; the original's reader runs in the
  CIA-B interrupt at 49.92 Hz, a little slower than the frames.
- The locked-world message on the map still ends on a fresh fire press,
  not through the stick byte.

## Open research items

- Level header words +2 ($24F0, not read in the code traced) and +6; the
  world "setting" word.
- The 33rd world slot at $C0000 and the unloaded disk ranges $37400 and
  $42400.
- sd_game.dat's layout.
- Bit 15 set on tile-map slots by a later pass.
- $24F5/$24F2 states' frame set (+$C80) in full.
