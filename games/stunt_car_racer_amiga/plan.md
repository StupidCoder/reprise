# Stunt Car Racer — remaining work

Status (2026-10-30): importer, the loading picture, the eight circuits
(decoded from the track bank, shown as turnable 3D models), the whole race
screen, and a practice race on Little Ramp with the game's own car physics
(checked frame by frame against the original) are connected; crashes, laps,
damage and the opponent are not yet. Findings
are in [game.md](game.md).

Done:

- [x] Identify the user's image reproducibly (ADF and region SHA-256s).
- [x] Disk layout, loader and program start-up (sample conversion, defeated
      checksum) recovered.
- [x] Import into the content library (`loader`, `program`), with rejection of
      other disks and a damaged-program error; loading picture as preview.
- [x] Reference harness (FS-UAE, port 6880) boots to the menu; `scr.py`
      drives it by key matrix to the practice races.
- [x] Ghidra project over the decrypted program (`ghidra/ghidra.sh`).
- [x] Track format re-derived from the unpacker ($5AE46) and verified
      against the original's RAM tables (Little Ramp, Hump Back); circuits
      as 3D model previews (`data/track.*`, `data/preview.*`).
- [x] Race screen (`data/cockpit.*`, `render/race_screen.*`): cockpit
      picture, overlay items from the sprite sheet, wheels, engine, crane
      chains, flames, tyre treads, dashboard (speed bar, marks, lamps, lap,
      boost, distance, timers), messages; the opponent car (in
      `render/race_view.*`). Whole frames compared with the original.
- [x] Race view renderer (`render/race_view.*`, hills `data/scenery.*`):
      angle projection, palette, sky/ground/hills, road pieces with the
      profile marks, sides, edge lines, start line; 15 frames of Little Ramp
      and Hump Back compared with the original (95–99.8 % equal pixels);
      start views as previews. Height scale ¼ confirmed.

Next:

- [ ] Map the front end's state machine and the main race loop in Ghidra
      (the 6502-style code decompiles poorly; read assembly where needed).
- [ ] Interpret the rest of a track definition: the third header byte, the
      four trailer bytes, the (segment, value) pairs and the segment list,
      and the per-segment table built at $1C952 (opponent speeds?); the
      shape description bytes ($1BB7B, $1BBD9, $1BBD4).
- [ ] The drawbridge animation ($5A794) in the race view.
- [ ] Race view, remaining: sides in colour 9 while $1BB66 is set; thin
      stripes on near sides at glancing angles; the thinned far segments
      (check on the tall tracks); facing backwards ($1BC32) once the car
      can turn round; verify Divisions 1–3 circuits (needs league progress
      or a pose on a loaded track).
- [ ] Race screen, remaining: smoke puffs' placement (table at $60F5A, items
      $1D–$24 via $6A0BE); when the timer lines and the message box show
      (callers of $5E326 and of item $33); the cracking roll-bar mark's
      timing; the right lamp's meaning ($1BBB2); the wheel curve from the
      suspension ($5E778, sine table $1CA42) once physics exists; the
      opponent's extra row when $63EDE ≥ $FF00; the 3-pixel '.' difference.
- [ ] Font and front-end screens (menu, league tables, track select) from the
      program's data.
- [x] Car physics (`race/physics.*`, `race/contact.*`), placement
      (`race/placement.*`), the start and race loop (`race/race.*`), checked
      frame by frame against the original; a practice race on Little Ramp
      playable from the launcher (`game/module.*`).
- [ ] Race, next: crash handling and the crane's recovery (main loop
      $5D4E8–$5D608, $5B32E relaunch, $60CDE), the off-grid path; laps and
      the lap timer ($5DB34, $5DCA6, $5DED0); damage to the roll bar
      ($5DFB4, $66106) and wrecking; the wheels' suspension curve ($5E778);
      smoke; the boost count ($60824); then the opponent: its placement
      ($5A186), driving ($63996, $6076C, $636C0) and the knocks between the
      cars ($1BB46); then the league and the menus.
- [ ] Captures beyond the first straight: curves, jumps, crashes, boost, the
      other circuits (the checker's map of the original's variables is in
      `tools/stunt_car_racer_physcheck.cpp`).
- [ ] Renderer enhancements: widescreen by widening the view (the angle
      projection extends naturally), smooth motion up to 120 Hz, higher
      resolution.
- [ ] Sound effects (engine, impacts).
- [ ] Keyboard, gamepad and touch controls; quick saves of real state;
      VHS/screen filter; cheats (e.g. no damage, practice any track).
- [ ] Multiplayer / Computer Link: decide scope (serial link has no host).
- [ ] Load/Save/Replay menu: what it reads and writes on disk (league saves,
      replays) and how the port stores it.
- [ ] Desktop and Android builds validated; differences documented.

## Open bugs

- After going over the road's edge the car stays where it lands: crash
  handling and the crane's recovery are not ported yet.
- No lap counting, timer, damage display or opponent in the practice race.

