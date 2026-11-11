# Stunt Car Racer — remaining work

Status (2026-10-30): importer, the loading picture, the eight circuits
(decoded from the track bank, shown as turnable 3D models), the whole race
screen, and a whole practice race on Little Ramp with the game's own car
physics, crashes and the crane, laps and lap times, damage, dust and smoke
(checked frame by frame against the original) are connected; the opponent
and the league are not yet. Findings
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
- [ ] Interpret the rest of a track definition: the (segment, value) pairs
      and the segment list,
      and the per-segment table built at $1C952 (opponent speeds?); the
      shape description bytes ($1BB7B, $1BBD9, $1BBD4).
- [ ] The drawbridge animation ($5A794) in the race view.
- [x] Race view: sides as one polygon per road piece with their dark
      lines and colours, and the drawing order while the car is beside
      the road (start line no longer shows through the side under the
      crane); checked against the original's frames.
- [ ] Race view, remaining: sides in colour 9 while $1BB66 is set; the
      opponent's place in the order while $57C69 is not 0; thin
      stripes on near sides at glancing angles; the thinned far segments
      (check on the tall tracks); facing backwards ($1BC32) once the car
      can turn round; verify Divisions 1–3 circuits (needs league progress
      or a pose on a loaded track).
- [x] Front wheels: rows from the suspension ($5E778, checked against 71
      saved frames) and turning on the 50 Hz vertical blank ($651B8).
- [ ] Race screen, remaining: when the message box (item $33) shows; the
      opponent's extra row when $63EDE ≥ $FF00; the 3-pixel '.' difference.
- [x] Front end (`data/front_end.*`, `render/front_end_screen.*`,
      `game/front_end.*`): SELECT, NAME?, the divisions screen, the
      division menu, Practise (track lists), the track picture (race view in
      picture mode, `draw_track_picture`), the Hall of Fame; screens compared
      with the original's captures (menus, NAME?, Hall of Fame 100 %;
      fixture, RESULT, standings, CHANGES equal but for names and this run's
      results; track picture 97 %).
- [x] Touch and mouse in the front end: a tap on a menu bar takes that
      entry; on the track picture the outer 64 pixels of the window walk
      round the stadium, the middle starts the race; on every other screen
      (and after PRESS FIRE) a tap is fire. The race's touch buttons show
      only in races. Driven with `stunt_car_racer_assets drive` ("@x.y").
- [x] Single-player league (`league/league.*`, `league/records.*`): season
      rounds, ratings, standings, fixtures, RESULT, standings, CHANGES,
      promotion and relegation, the Super League, track records from the
      player's laps; rounds replayed against the original
      (`harness/leaguecap.py`, `stunt_car_racer_assets league`).
- [x] Car physics (`race/physics.*`, `race/contact.*`), placement
      (`race/placement.*`), the start and race loop (`race/race.*`), checked
      frame by frame against the original; a practice race on Little Ramp
      playable from the launcher (`game/module.*`).
- [x] Race loop as a whole (`race/race.*`, `race/smoke.*`,
      `race/random.hpp`): spawn/settling/driving frames, going over the edge
      and the crane's relaunch, the clock, laps (halfway segment, crossing
      fraction), best lap and lamps, the finish and end countdown, LAPS
      OVER / PRESS FIRE, roll-bar holes, the roof crack and wrecking, the
      boost count, dust and smoke, the game's random generator; checked
      against `work/race_lr1.bin` (467 of 467 frames exact). PRESS FIRE
      restarts the practice in the port.
- [ ] Check laps, the best lap, the finish and wrecking against the
      original: a whole-race capture (`physcheck autodrive` finds inputs
      that finish Little Ramp in the port; `harness/racesample.py` needs
      fixing: the original hung after ~160 fed frames).
- [x] The opponent (`race/opponent.*`): skill and segment speeds at track
      load, ride, suspension, bumps, speed, steering, the gap, placement,
      its laps and lap times, the knocks' share; the dashboard distance;
      checked against `work/league_lr1.bin` (every frame). A "Race" option
      plays race 3 against Jumpin' Jack.
- [x] The cars touching ($63CE4) checked: `work/league_touch.bin`.
- [ ] Front end, remaining: the "New track record" screen after a league
      race ($64664 with $1BBB1); fades between screens ($64470); pause
      ($5D9DC); the Super League's wall colours in the race view (8 and 4,
      $5D29C); the "taken" bar's exact time ($5BA42 is a busy loop); sounds
      (engine $5B56C, knocks, scrape, crane, menus).
- [ ] Captures of the other circuits (jumps, drawbridge, wrecking), of
      boosting and of the league (the checker's map of the original's variables is in
      `tools/stunt_car_racer_physcheck.cpp`).
- [ ] Renderer enhancements: widescreen by widening the view (the angle
      projection extends naturally), smooth motion up to 120 Hz, higher
      resolution.
- [ ] Sound effects (engine, impacts).
- [x] Touch controls (phones and tablets without a gamepad): left and right
      bottom left, the accelerator bottom right (it also gives fire while
      the game waits for it: PRESS FIRE), the boost (fire and the
      accelerator together) above it. Not yet tried on a device.
- [ ] Keyboard and gamepad controls; quick saves of real state;
      VHS/screen filter; cheats (e.g. no damage, practice any track).
- [ ] Multiplayer (up to seven players taking turns, "Enter another
      driver"/"Continue", $5E8AC, $5EE68): the menu entry is there but does
      nothing yet. Computer Link: no host for the serial link; the entry
      does nothing.
- [ ] Load/Save/Replay menu: what it reads and writes on disk (league saves,
      the HALL file $5F72C, replays) and how the port stores it; the menu is
      there, every entry returns to the division menu. Quick saves of the
      league.
- [ ] Desktop and Android builds validated; differences documented.

## Open bugs

- A league race is left with Tab (the original's Esc, which Reprise keeps
  for its pause menu); on a touch screen there is no way to leave a race.
- On a touch screen with no keyboard the name cannot be typed; fire gives
  the name "Player".
- The track picture leaves out the crack's "Broken by QUARTEX" (the
  genuine release's text there is unknown).

- The opponent is drawn at its progress step on its segment without the
  game's handling of reversed pieces ($5A5B6); not yet checked there.
- The wheel turning has not been compared frame by frame with the
  original (the rule is read from the code; the vertical blanks the port
  runs between frames are 6.5 to a frame, the original's vary a little).
- No sounds.
- The crack's paint test reads the cockpit picture; anything else the
  original has drawn on the roof rows is not considered (nothing observed).

## Technical lessons (2026-10-30)

- [x] Illustrated angular-camera and three-support suspension articles, with imported road diagrams and portable SDL reading layouts.
- [ ] After the playable reconstruction, add the opponent-driving lesson; resolve state ownership and validate decisions before presenting AI behaviour. See [lesson evidence](../../docs/articles/stunt-car-racer-technical.md).

## VR presentation investigation (2026-11-11)

[The Steam Frame VR plan](../../targets/steam_frame/STUNT_CAR_RACER_VR_PLAN.md)
compares a stereo floating window with immersive seated rendering and recommends
a shared 3D scene/perspective renderer, validating the window first. Important
constraints are the original angular projection, 130 ms race ticks, restricted
track visibility and partly screen-space opponent geometry. Currently,
the frozen stereo study is followed by an opt-in live GPU practice preview.

- `render/scene.*` extracts all eight circuits into portable indexed triangles
  with material IDs and road UV coordinates. Heights retain the 1/4 plan scale.
  Road sides use the original $200 base height. No physics or track data changes.
- `platform/scene3d.*` supplies camera math and a deterministic depth-buffered
  CPU reference renderer. This is a diagnostic backend, not the intended
  real-time GPU implementation. Ground and track surfaces are two-sided for now;
  no newly modelled road thickness is claimed.
- `stunt_scene_check` checks depth order, near clipping, stereo convergence and
  height normalization. Against installed content it checks every circuit's
  interval/triangle coverage and unchanged original rendered start view, and
  exports overview/underside and cockpit-composited eye pairs. Original palette
  material alternation is provisional: profile-group shading, edge/start marks,
  scenery and the opponent are not integrated.
- The frozen Little Ramp pair is deployed through the Steam Frame stereo study
  command. Both eyes show the same cockpit art; only the windscreen view differs.
  It uses a documented provisional scale, 64 mm eye separation, off-axis
  convergence at the panel and supersampling. It does not simulate driving or
  respond to head translation. The wearer confirmed visible depth and no garbling; live comfort remains unvalidated.
- macOS and Android builds and four relevant CTests passed. The existing Little
  Ramp reference check still matches 467/467 frames when started at frame 2,
  after the capture's documented missing first driving frame.

The live practice preview now exports immutable scene snapshots and a cockpit
overlay to the host Vulkan renderer, with optional camera interpolation. Physics
timing stays unchanged; league races use the original flat view. Mac/Android and
native ARM64 builds passed, as did a 500-tick scene-sampling invariance check.
The wearer confirmed live driving presentation and corrected banking, retaining
the game's feel with cleaner, smoother stereo graphics. Next: check broader track
coverage, head translation and smoothing before immersive seating. Details,
limitations and commands are in the Steam Frame plan.

VR follow-up: the wearer validated textured road/grass as substantially improving
depth perception. Source art and built-in image-generation prompts are recorded
in `resources/stunt_car_racer_amiga/materials/README.md`. Track-details and league
stereo now compile and are deployed for visual validation: profile shading,
edge/start markings, decoded horizon polygons, and a new world-space opponent
anchored to original race data. Practice and league 500-tick comparisons pass.
Next priorities remain frame pacing, then immersive cockpit VR; detailed status
and limitations are in the Steam Frame plan. No game logic changes claimed.

Steam Frame pacing pass validated: native cockpit overlay with unchanged-image
reuse plus CPU preparation overlapping the room submission reduced representative
render-and-fence p95 from22–24ms to13.4ms. Wearer verified alignment, pause menus
and the overlap build. Occasional over-budget frames remain; measurements exclude
xrEndFrame and are not a controlled replay benchmark. See `stunt-pacing.txt` in
the Steam Frame evidence. Immersive cockpit is the next requested milestone.
