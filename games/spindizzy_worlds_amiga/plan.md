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
- [ ] A Pac touch and a Pac eaten while the explorer flashes, traced.
- [x] Spark generators (on/off by condition, touch: clue 9, $20 fuel a
      tick); Spindizzy! 1/2 equal, a touch to game over in `play_w22_1s`.
- [x] Turning the level (r/t; the builder's offset table $125A8 makes
      blocks and objects agree), traced in `play_turn`.
- [x] Keys F (slow mode), c (centre), p (pause, dimmed). Not done: [ (pause
      without dimming) and the overview map m ($1135A/$18DC6).
- [ ] The deadly timer's end: after it runs out (Roller-ama 4, tick 641)
      the original runs no more rules ticks (its frame handler $153EA runs
      the dying routine $12CE8 on the timer's own record); the port keeps
      the explorer's 200-tick dying phase. Read $12CE8 with a4 = timer.
- [x] Game over: the reserve-fuel offer ($18C14) with its three outcomes.
- [ ] The dying kind's frames ($12D14 list, "Game over" text $12D68) and
      the timer's "Die if elapse" frame ($153EA, see the timer's end).
- [ ] The exit's "Reserve fuel collected" message ($15186) (kind −9's
      notice is done).
- [ ] The space-bar brake traced.
- [ ] Palette objects (kind 16, unused by the levels) and $207E fades.

### Front end

- [x] The statistics screen ($16DA4) after a world and for "Check
      performance".
- [ ] The disintegrating planet on the map after a world ($11838, kind −7,
      frames SELECT.SWP + $35C0, list $16B38) and the ending when every
      world is done ($17CBE).
- [x] Saves between worlds as the original keeps them (the cluster, worlds
      done and their records, clues, slow mode) in the shell's format
      (`write_state`/`read_state`); the menu's "Save game state", "Load
      saved game" and "Save game" after a world use a slot in it; a load
      goes back to the world map.
- [ ] Wind back world ($36B0 − 2) and Create Data disk (decide whether to
      keep them).
- [x] The panel's clue boxes ($1635E/$163BE), equal in every record.
- [x] The timer's seconds on screen ($153C4) and the locked-world message
      ($1180A).
- [ ] Menu input: the original takes the joystick byte each pass; the
      port acts on presses. Its pass rate (not frame-locked) is guessed
      as one a frame.

### Sound

- [x] Effects: the player rebuilt (`sound/effects`), the game's requests
      and the distance-heard channel 2; every channel's effects equal in
      order (`play_route1s`).
- [ ] Effects not traced yet: the timer ticks, switch pads, creature
      touches, boink bounces, heads, sparks, pyramids; the sample-level
      output against a recording of the original's audio.
- [x] Music: the player rebuilt (`sound/music`); tune 0 on the title and
      menus, the map's tune on the cluster choice and map; both equal to
      the original over 1,500 ticks (`musiccheck`).
- [ ] Music still to place: tune 3 ($18958, after "Abandon") and tune 1
      (not seen yet); the music after a world ($18A6A); the attract's tune
      kept over a restart ($6F9 of the module); compare the sample output
      with a recording of the original's audio.

### Enhancements (optional, off by default)

- [ ] Widescreen: camera targets for wider views (the original's $88/$50
      are for 288 pixels), stars over the wider view, pillars and panel.
- [ ] 120 Hz smooth motion.
- [ ] Mod loading of world files.

## Known bugs and gaps in the port

- The front end's screens are checked by eye against reference screenshots
  and dumps, not pixel by pixel; the attract sprites' timing and the menu's
  flash rate are not compared.
- The Pacs' eaten frames ($157DC: $25930 + ($24F6 − 1) × $50) are not
  drawn yet (the view keeps their walking frames).
- The clue search takes the objects in slot order; the original walks
  them in drawing order ($358C), which differs only with two conditional
  objects near the explorer at once.
- The random generator starts at 0 at boot (the attract and menus draw
  no numbers in the code read); not checked against the original after
  a long front-end session.
- Sprite colour 24 and the pillars' screen columns (−1 and 303) follow
  the hardware registers; not checked against a window capture.
- Not compared yet: the "lost" tops, the ice timeout, the brake, a lift
  top without its lift, the clue boxes; of the Tutorial, levels 6, 7 and
  14 played through (their buttons, lifts, gates) and level 5's
  switches (only played natively).
- The bonus notice and clue texts are drawn over the live picture; the
  original prints them once into the shown buffer (the same picture
  while the game waits).

## Open research items

- Level header words +2 ($24F0, not read in the code traced) and +6; the
  world "setting" word.
- The 33rd world slot at $C0000 and the unloaded disk ranges $37400 and
  $42400.
- sd_game.dat's layout.
- Bit 15 set on tile-map slots by a later pass.
- $24F5/$24F2 states' frame set (+$C80) in full.
