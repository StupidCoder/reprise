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
   Tutorial 1 grid: 8189 of 8192 cells equal (the rest is a column an
   object changed).
2. **Block graphics and the isometric view** (2026-10-28). Tiles, masks,
   block pieces, palettes; `view/tile_map` (equal in all 641 cells of
   Tutorial 1), `view/playfield` (any window size).
3. **Sprites and objects in the view** (2026-10-28). Sprite frames,
   projection, occlusion; every object kind's look, the creature table,
   the original's placement and mask quirks; depth order by the +$18 sort
   values. Six screen dumps pixel-exact over the visible 288 columns
   (EasyDizzy 1: only the Timer's text).
4. **Explorer movement, surfaces, camera** (2026-10-28). `play/explorer`,
   `play/surfaces`, `play/level_play`, `view/camera`. 17 traces over 10
   levels replay tick for tick (`physcheck`). The shell runs a test drive
   (`game/module`: pick any level in the pause menu); `drive` runs it
   headless.

## Remaining tasks

### Milestone 3 (rules), continued

- [ ] Objects' behaviour, one kind at a time, each traced on the reference
      machine and compared tick by tick:
  - [ ] Heads: block the explorer, move up when their condition holds
        ($1594A/$15A98). Tutorial 1's trace t1_a stops matching at tick 343
        because of this.
  - [ ] Pac aliens, pyramids, boinks: movement, surface settling ($12F62),
        touching the explorer ($156D8: clue 5, energy $2062, flash +15).
  - [ ] Lifts: motion (Power, Max/Min z, Pause, speeds), carrying the
        explorer (contact $81, $14E26), the lift-top surface without its
        lift.
  - [ ] Jewels: collecting, the jewels-left count ($2566).
  - [ ] Push buttons, binary switches, switch pads: switch values ($3616,
        $3617, $3618), condition re-tests and level rebuilds ($162FE).
  - [ ] Exits: level-to-level travel (Dest level, Keep pos&vel, Use XYZ
        pos, Restore blk, Invisible, gated exits), world completion bits
        ($24AE).
  - [ ] Timer (Time, Relative, Die if elapse, Elapse Dest), spark
        generators (Period, Proximity), secret passages, clues, messages,
        palette changes (palettes 1–3).
- [ ] Energy (fuel): $206C, costs $2060/$2062, $2064 (lives? hypothesis),
      "NO fuel use", "Add reserve fuel", game over.
- [ ] The space-bar brake in a trace (code is ported, not yet compared).
- [ ] Gravity / Fall Velocity objects (editor form; kind not identified).

### Milestone 4 (front end and sound)

- [ ] Attract: title, hint panels.
- [ ] Menu: Continue, Start new game, Load/Save game state (sd_game.dat),
      Check performance, Wind back world, Slow Mode, Create Data disk
      (decide what to keep).
- [ ] Easy/Hard cluster maps, world maps (SELECT.SWP), world gating.
- [ ] Panel: clue box, the four small boxes, world name, compass arrow,
      fuel gauge on the pillar; fonts ($1B000–$1D000) and Timer digits.
- [ ] Sound effects (player module at $3400 and its samples) and music
      (player module at $432A8): decode and rewrite natively.
- [ ] Saved games.

### Milestone 5 (enhancements, optional and off by default)

- [ ] Widescreen: camera targets for wider views (the original's $88/$50
      are for 288 pixels), stars over the wider view (the original places
      them in 288 × 168 only), pillars/panel layout.
- [ ] 120 Hz smooth motion: interpolate the explorer, objects and camera
      between rules ticks.
- [ ] Mod loading of world files.

## Known bugs and gaps in the port

- Objects are drawn in their start state and do not act; no energy, panel,
  sound or front end yet (test drive only).
- The explorer's start height is taken from header word 0 bits 0–2 as the
  level, z = level·256 + $80 (hypothesis; not traced).
- The star generator's running state at a level start is not traced; the
  test drive seeds it with 0, so starfields without their own seed may be
  placed differently.
- The Timer's "25" text is not drawn (fonts not rebuilt).
- Tall lifts with a column are not checked in the view.
- Not compared with the original yet: the "lost" tops (02 family; always
  under another top in the levels), the hot top's 250-tick limit, the
  brake, a lift top without its lift.
- Level 15 of a world cannot be reached through the harness redirect, so
  no level 15 has been checked.
- Grid check only on levels without conditional macros changing (Tutorial
  1, Gargoyle 1): check a conditional level.

## Open research items

- Level header words +2 and +6; the world "setting" word.
- The 33rd world slot at $C0000 (never loaded) and the unloaded disk ranges
  $37400 and $42400.
- sd_game.dat and SELECT.SWP contents.
- Bit 15 set on tile-map slots by a later pass (not needed for the picture).
- $24F5/$24F2 states' frame set (+$C80) in full.
