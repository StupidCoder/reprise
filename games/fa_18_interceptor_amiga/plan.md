# F/A-18 Interceptor (Amiga) — plan

What remains for a complete, playable re-implementation, and the known
bugs and differences. How the game works and what was learned: game.md.

## Where it stands (2026-10-29, the F-16)

Playable from the title: the title with its tune, credits, call-sign
entry or welcome back, main menu, flight log, free flight's start map
and aircraft choice, then free flight from the three runways or the
carrier's deck: the original's flight model tick for tick, the head-up
display, the panel's numbers, the message line with its warnings, the
original's sounds, the crash (noise, flash, five aircraft, end text,
back to the menu), the outside views and the view from above, the
carrier's deck and wires, the weapons (gun, missiles, flares, chaff
with their readouts), and either aircraft (F/A-18 or F-16). Options: widescreen, smooth motion up to 120 Hz,
VHS, sound. Checked against the original: `flightcheck`, `soundcheck`,
`weaponcheck`, `hudcheck`, `viewcheck`, front-end screens and the map's markers
(game.md sections 2 and 7). Committed and pushed step by step.

Owner's design notes: widescreen and smooth motion stay (own renderer from
game state, rules at 15 Hz); the High detail option (done 2026-10-30,
off by default) draws the nearest detail level that each pass may draw,
at any distance and height. Fast text (done 2026-10-31, off by default):
every text types at 100 steps a second, twice the menu screens. Later
(owner, 2026-10-30): analog joystick input as an option - the stick's
deflection sets the inputs the original ramps (`ramp_inputs`: pitch and
rudder ±20, roll ±60, looked up in the response tables) and a throttle
axis the throttle target; the digital path stays the default (faithful
play, the demo, the checks). Done for touch (2026-10-30): the shared
touch framework's stick, throttle slider, GEAR and VIEW buttons (game.md
"Input"), and taps on the front end's menus, map and aircraft question.
Left: fire and weapon buttons for touch, a gamepad's analog axes (the
app has none yet).

## Remaining tasks towards a full game

1. **Front end** — done 2026-10-29 for free flight: title with its tune,
   credits, call-sign entry / welcome back, main menu, flight log (with
   write and reset), the start map with its markers, the aircraft choice,
   "READY FOR TAKEOFF", Esc / Shift-Esc, the pilot record (`config`).
   Left: the missions list (6) and the other items'
   screens (with their modes, tasks 10-11), the security check (skipped:
   copy protection), the log disk messages (the folder is always there).
2. **Music** — done 2026-10-29: the title tune is the chain of sample
   slots 13-34 (no sequencer), with the master volume's glide.
3. **Cockpit** — done 2026-10-29 for free flight: the head-up display,
   latitude and longitude, the fuel bar, the compass strip, the gear
   symbol, the radar's scan and own mark, the panel's own colours. Left:
   the GEAR box's lighting while the gear-speed warning blinks, the
   radar contacts and the weapons' marks (tasks 7-8). Done since: the
   threat lamps (`$125D26`, 2026-10-30), the
   zoom (Ctrl with = / -, the original's `[` / `]`, `$13AD32` $80 / $40
   / $20 with its readout).
4. **Warnings and messages** — partly done 2026-10-29: fuel low,
   critical and exhausted in the message line, the 7.8 G beep and the
   blackout counter (engine at half volume); the radar's threats, the
   missile alerts and their beeps (2026-10-30), the blackout's picture
   (2026-10-31). Left: collision proximity (see game.md "Warnings").
5. **Collisions** — done as far as the original has them: it has no
   collisions with scenery (game.md "Warnings"); ground and water
   contact were rebuilt with the flight. Object collisions and collision
   proximity come with task 7. Open: what landing on water does
   (ditching: a crash here, not traced).
6. **Other views** — done 2026-10-29: the outside camera (keypad 0 for
   the original's keypad Enter, starting in front; keypad 4 / 6 round
   the aircraft, 8 above, 2 behind; Ctrl with = / - the distance, the
   original's `[` / `]`) with the original's offsets, chase and level
   look-at, the F/A-18's shape and shadow, the outside sound rules
   (checked against a recording), and the view from above with its grid
   (V for the original's M, which Reprise keeps for mute), the start
   map's descent to the aircraft (`$13AAA6`, checked frame by frame) and
   the camera's turning (`$10454A`), looking up and down (keypad 7 / 1;
   own aircraft only for directions 3-9, 2026-10-31).
7. **Moving objects** — done 2026-10-29 for free flight, whose only
   object is the carrier: spawned from the game's object list, drawn,
   place 4 on the map and the start on its deck ("READY TO LAUNCH"),
   the deck test (six outlines), the wire zone and the hook (key A,
   here B). Left: the hull's faces (the port crashes below the deck
   inside its outline instead), the other ships' decks (`$12EF74`),
   the object step for moving objects (`$11A352` for others, flight
   paths `$38`, `$11DDC0`), other aircraft, missiles and parachutes
   (they come with the modes that spawn them, tasks 10-11). Done since:
   objects touching (`$11B6A2`: near, hit, crash, explode) for the
   aircraft records; explosions, smoke and the burning trail drawn
   (2026-10-30, game.md "Explosions, smoke and trails"); left: scenery
   boxes (a hit on one blows up there and leaves smoke in the air, type
   33), a burning aircraft's fall (it needs the missions' path
   following).
8. **Weapons** — done 2026-10-29 for free flight: the stores, Return
   for the next weapon, space to fire, F a flare, C chaff (with their
   messages); the gun's bursts, rounds and sound, missiles launched from
   their stations and flown on (no target in free flight), flares and
   chaff, ground hits with sparks; the head-up display's weapon line, the
   gun's aiming circle, the panel's stores display with ARM / NO and the
   name. Checked frame by frame against the original (`weaponcheck`).
   Left: the hit tests against scenery and objects (`$11AE96`'s
   8 sub-steps, `$11B572` / `$11B4A8`) and what a hit does; missiles'
   guidance with a radar lock (`$13ACB0`, `$1182F2`, task 11); the flare's
   burning drawing (`$12185E`, a dot here); the log's counters (config
   +$3A rounds, +$3E / +$42 missiles); Shift-F (the ejection); key R's
   mode nibble (+$63 low).
9. **The F-16** — done 2026-10-29: the type byte, its head-up display
   (tapes, load factor), eye point, outside camera, end text; flight and
   sound checked against a recording of the original (f16a, all frames),
   the head-up display pixel for pixel (`hudcheck`). The F/A-18's HUD
   positions were corrected at the same time (from the original's tables).
10. **Demo, training and qualification** — done 2026-10-29:
    qualification (5), the maneuvers' demo (3) and their practice (4,
    with the lead, the triggers and re-positioning) are built and checked
    against the original (`trainercheck`); the demo (1) flies its
    recording in mission 3's world from the object list, all 2,046 frames
    equal (`democheck`). Left: a recording of the break turn's and the
    aileron roll's first roles (39/40/2/7, 11-13) to check them too; the
    demo's vector value against the original's screen.
11. **Missions**: the six selectable missions and the advanced mission
    chain, briefings, outcomes, the log's records. Done: the object lists
    (`$11CF06`, game/world_objects.*), the carrier at any place and
    heading; the other aircraft's object step (speed, distance, pursuit
    point, attack choice: game/object_step.*), the autopilot's mission
    roles and the attitude step's rules for others, checked with
    `objectcheck` (m3a, demo_objs); the radar (list, pass, contacts,
    T lock, mission 3's confirmation, the player's distance); the
    enemies' missiles - launch, flight, decoys, hits on the player - and
    the threat warnings and lamps (2026-10-30, game.md "The enemies'
    missiles"). Left: weapons against objects (built: missiles homing and
    hitting, the explosion, fall and end, the lock dropped, the rounds'
    hit test with the drawer's detail levels, checked against three
    recordings; left: the drawer's list as such and the ships' hull test), the objects' touches and crashes, the trigger areas
    (`$11D17A`: spawn, again while inside, sent back on leaving, the way
    home `$11D60C`; checked with `areacheck` on area1-3; area 3 and a
    respawn checked in mission 8); the missions' flow is built for choosing (6, 7, F1-F6),
    the briefings, every mission's objective and failures, mission 6's
    rescue pod, completion in the log, the player's crash ending a
    mission; missions 4-8 checked against 13 recordings (`missioncheck`,
    game/object_world.*: every frame equal but the pokes'), the camera's
    turns to Air Force One and the cruise missile (the viewed record, the
    texts' delay; m4b, m7a, turn7); the records' explosions, the smoke a
    missile leaves on the ground and the trail behind the last aircraft
    hit (checked with `weaponcheck` on sm2-sm4, `trailcheck` on
    demo_trail, pictures with `effectshot`). Left: the view from a gone
    record's eye (cockpit case of `$10EC3C`), the aircraft question of
    mission 3's runway start, the radio noise; unrecorded:
    −3 (Air Force One shot by the player), the leave rules for records
    heading for a record or a path point; flying a mission again with
    the same variant and offsets (`$13AE0C`, `$13AE08`).
12. **Dusk and night**: palettes 3-9 (the fades).
13. **Saves**: quick save and load of the module's state.

## Open research items

- `$10F14E` (message $4005, fuel and stores refilled): in mission 6 it
  clears `$13AA88` unless the mission succeeded - what calls it (a
  refuelling landing?) is not traced.

- A soft touch-down (and its tyre sound), the boom and the spin warning
  were never seen in a recording of the original; touch-down rules are
  ported from the code only.
- The throttle keys' timing between the original's key events and the port's
  held keys; the timer +$4C (stepped by 2 a tick).
- The restart routine `$10D558` is taken as the free-flight start.
- Which palette each briefing screen uses (previews use palette 0);
  entries 16–19, 25 and 111–116 of the text table are not block starts.
- `pix/frnt5` (288 × 12) use; hunk 62 (chip) sprite data; maths tables
  in hunks 63 and 65.
- Real-second counters $13AB74 and the 10-second flag; $13ABE0 (set to 2
  by the stall check).
- World: the coarse water grid's cell layout; type 58's role (a hill at
  the carrier's start; model flag $10); the
  run-time cell bitmaps; the visible-list entry fields after the position;
  the octant lists (`$136464`).
- Shapes: scenery opcode $18 (types 107, 108); discs (other than the
  smoke's), dots, lights, deck stripes ($80/$50) and shadows; the vertex
  builders' rounding; the polygon lists' distance bands in the renderer;
  the order of flat ground shapes between objects (taken far to near per
  pass: hypothesis); the axis tests on x and z (offsets `$13AE1A` /
  `$13AE1E` taken as the camera about the object).
- The enemies' missiles: `$13AA79` (always 1, no writer found) and
  `$13AAAC` (never set) taken as constants; the threats' beeps and the
  message order checked only for an enemy (`democheck`, bit 2) and a
  Sidewinder on the player (em4b's recorded `$13AB5E`, not stepped by a
  check); `$13AB77`'s reader not traced; AMRAAMs on the player, chaff
  and the ECM's faster pace not recorded.
- Explosions and smoke (game.md "Explosions, smoke and trails"): the
  explosion noise's delay is counted from the frame after the drawing
  (where `$104DD4` runs in the frame is not traced) and its distance is
  taken from the aircraft, not the camera; the trail's record is the one
  whose +2 bit 4 came on last (the original sets `$1450BA` at the hit,
  which is the same unless two are hit in one frame); the triangle fill
  (`$124B48`) and the disc's top row clipping are not pixel-checked;
  the cruise missile's explosion (kinds 3-5) and a missile over water
  (kind 2) were not recorded.
- The message drawer's colour for attribute class 0 (no record uses it).

## Known bugs and differences in the port

- Done 2026-10-31: the ground's specks (`$11C1B4`: grey on the sea,
  black on the land, near a low camera; game.md "The ground's specks").
- Fixed 2026-10-31: looking up or down from the cockpit drew the
  player's own aircraft (the demo's look down at the wreck showed the
  fuselage); the original draws it only for look directions 3-9.
- Fixed 2026-10-31: the screen fades (demo start and end, qualification,
  missions, start map) read their colour steps from the wrong side of
  `$11A04C` and flickered through random colours and orange; they now
  fade from black as on the original. The overload blackout (screen to
  black after more than five seconds at 7.8 G or more, either sign) is
  rebuilt and checked on the original (2026-10-31), and so is the P key's
  pause (dimmed screen, "- PAUSED -", sound off). Open: P during a
  mission's text countdown, which the original also toggles, and the
  pause's time left out of the flight time (the port keeps no flight
  time yet).
  In scripted `fly` runs the port's loops peak at 7.6 G; a real 7.8 G
  pull was not flown, the blackout was checked with a forced latch.

- The start map misses the carrier's marker (4) and shows a dark speck
  west of the coast the original does not; the scenery decoder still
  stops at opcodes `$5C` and `$9C` (some mid-distance levels).
- The title stays 22 s (the original's loading time on the A500) but a key
  skips it; the original takes no key there.
- Free flight does not yet update the pilot record's flight time (the
  crash also adds 1 to config +$10, not decoded).
- The panel's speed does not drop to 0 at a crash (state bit 15 is set
  by the object collision code, task 7).
- A crash adds 1 to the log's crashes (config +$46) in memory; when the
  original writes the record after a crash is not traced (the port
  writes it with the next mission's completion or the front end's).
- The view's window of object cells is a square, not the original's
  octant lists. (The water's cell windows follow the original since
  2026-10-31.)
- The water windows' quarter and heading are taken from the camera; the
  original takes the cockpit's from the aircraft's cell words (+6, +8)
  and its heading word `$13AD7E` (3,302 of 3,335 demo frames give the
  same eighth; the rest lie on a 45-degree boundary). The water
  routine's `-$20` (2 up to height $400, 1 up to $C80, for the camera's
  cell) is passed to the polygon drawer and not followed.
- The runway's markings during the demo's final approach (frames
  1880-1900) differ from the original's (threshold blocks nearer):
  not traced.
- No objects other than the carrier in free flight (as the original's
  free flight; others come with the modes, tasks 10-11).
- A missile taken off when it hits still shows where it hit for some
  frames in the original (demo_trail2, frames 556-564: the drawer's cell
  lists are not rebuilt at once - hypothesis); the port draws only active
  records.
- The smoke slots' timers stop mattering below 0 (the original's word
  wraps after 32,768 frames, about 36 minutes, and would then show a
  column of whatever lies past the shape's lists: not reproduced).
