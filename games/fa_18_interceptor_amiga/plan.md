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
game state, rules at 15 Hz); a planned option, off by default, always
draws the nearest detail level (the renderer keeps the level choice apart
from drawing, and must still respect the passes' model rule).

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
   alert lights' colours (`$125D26`, from the alerts of task 7), the
   radar contacts and the weapons' marks (tasks 7-8). Done since: the
   zoom (Ctrl with = / -, the original's `[` / `]`, `$13AD32` $80 / $40
   / $20 with its readout).
4. **Warnings and messages** — partly done 2026-10-29: fuel low,
   critical and exhausted in the message line, the 7.8 G beep and the
   blackout counter (engine at half volume). Left: the radar and missile
   alerts and collision proximity (they need the other objects, task 7),
   the blackout's picture
   (see game.md "Warnings").
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
   the camera's turning (`$10454A`). Left: looking up and down (keypad 7
   / 1: no keys for them in Reprise yet).
7. **Moving objects** — done 2026-10-29 for free flight, whose only
   object is the carrier: spawned from the game's object list, drawn,
   place 4 on the map and the start on its deck ("READY TO LAUNCH"),
   the deck test (six outlines), the wire zone and the hook (key A,
   here B). Left: the hull's faces (the port crashes below the deck
   inside its outline instead), the other ships' decks (`$12EF74`),
   the object step for moving objects (`$11A352` for others, flight
   paths `$38`, `$11DDC0`), other aircraft, missiles and parachutes
   (they come with the modes that spawn them, tasks 10-11), collisions
   between objects (`$11B6AA`'s first part, collision proximity).
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
10. **Demo, training and qualification** — partly done 2026-10-29:
    qualification (5) is built (briefing, catapult, the qualifying
    landing, the pilot record, the re-launch through the fade-out and the
    chosen line; Esc the same way, also in free flight). The demo (1) has its
    replay, key events, fades and up/down looks built and checked against
    the original's data, but waits for mission 3's world (its sailing
    carrier and the bogey: task 11) - menu item 1 still returns to the
    menu. Left: the maneuvers' demo (3) and practice (4): the trainer in
    record 4 flying the control-byte scripts of `$11756A` (seven
    maneuvers, prompts `$117522`), briefings 9 and 10.
11. **Missions**: the six selectable missions and the advanced mission
    chain, briefings, outcomes, the log's records.
12. **Dusk and night**: palettes 3-9 (the fades).
13. **Saves**: quick save and load of the module's state.

## Open research items

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
  the carrier's start; model flag $10); the white specks on the water; the
  run-time cell bitmaps; the visible-list entry fields after the position;
  the octant lists (`$136464`).
- Shapes: scenery opcodes $18 and $118 (types 33-35, 107, 108); discs,
  dots, lights, deck stripes ($80/$50) and shadows; the vertex builders'
  rounding; the facing tree's drawing order and distance bands in the
  renderer.
- The message drawer's colour for attribute class 0 (no record uses it).

## Known bugs and differences in the port

- The start map misses the carrier's marker (4) and shows a dark speck
  west of the coast the original does not; the scenery decoder still
  stops at opcodes `$5C` and `$9C` (some mid-distance levels).
- The title stays 22 s (the original's loading time on the A500) but a key
  skips it; the original takes no key there.
- Free flight does not yet update the pilot record's numbers (flight
  time, crashes; the crash adds 1 to config +$10, not decoded).
- The panel's speed does not drop to 0 at a crash (state bit 15 is set
  by the object collision code, task 7).
- The crash ends free flight's log update: config +$46 (crashes) is not
  counted yet.
- The view's window of object cells is a square, not the original's
  octant lists; the fine water grid's window below height $9000 (by
  heading) draws all cells.
- No objects other than the carrier in free flight (as the original's
  free flight; others come with the modes, tasks 10-11).
