# F/A-18 Interceptor (Amiga) — plan

What remains for a complete, playable re-implementation, and the known
bugs and differences. How the game works and what was learned: game.md.

## Where it stands (2026-10-29, second milestone)

Playable from the title: the title with its tune, credits, call-sign
entry or welcome back, main menu, flight log, free flight's start map
and aircraft choice, then free flight from the three runways in the
cockpit view: the original's flight model tick for tick, the head-up
display, the panel's numbers, the message line with the stall and fuel
warnings, the 7.8 G warning, the original's sounds, the crash (noise,
flash, five aircraft, end text, back to the menu); a first outside view.
Options: widescreen, smooth motion up to 120 Hz, VHS, sound. Checked against the
original: `flightcheck`, `soundcheck`, `viewcheck`, front-end screens
and the map's markers (game.md sections 2 and 7). Nothing of this is
committed.

Owner's design notes: widescreen and smooth motion stay (own renderer from
game state, rules at 15 Hz); a planned option, off by default, always
draws the nearest detail level (the renderer keeps the level choice apart
from drawing, and must still respect the passes' model rule).

## Remaining tasks towards a full game

1. **Front end** — done 2026-10-29 for free flight: title with its tune,
   credits, call-sign entry / welcome back, main menu, flight log (with
   write and reset), the start map with its markers, the aircraft choice,
   "READY FOR TAKEOFF", Esc / Shift-Esc, the pilot record (`config`).
   Left: the map's descent of the outside camera to the aircraft (cut
   straight to the cockpit), the missions list (6) and the other items'
   screens (with their modes, tasks 10-11), the security check (skipped:
   copy protection), the log disk messages (the folder is always there).
2. **Music** — done 2026-10-29: the title tune is the chain of sample
   slots 13-34 (no sequencer), with the master volume's glide.
3. **Cockpit** — done 2026-10-29 for free flight: the head-up display,
   latitude and longitude, the fuel bar, the compass strip, the gear
   symbol, the radar's scan and own mark, the panel's own colours. Left:
   the GEAR box's lighting while the gear-speed warning blinks, the
   alert lights' colours (`$125D26`, from the alerts of task 7), the
   radar contacts and the weapons' marks (tasks 7-8), the zoom keys
   (`$13AD32`, keys `[` / `]` in the cockpit).
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
   (V for the original's M, which Reprise keeps for mute). Left: the
   camera's fly-in states (`$13AAA6`, and the start map's descent to the
   aircraft), looking up and down (keypad 7 / 1: no keys for them in
   Reprise yet), which shape the F-16 uses. Done since: the cockpit
   look directions (keypad 4 / 6 / 8 / 2 in the cockpit).
7. **Moving objects**: the carrier (free-flight places 4-5, the deck,
   catapult and its thump, arrestor hook), other aircraft, missiles,
   parachutes; their object records and flight paths.
8. **Weapons**: gun (slot 10's sound), missiles, flares and chaff.
9. **The F-16** (type $10) as the second aircraft.
10. **Demo, training and qualification**: menu items 1, 3, 4 and 5
    (carrier landing), the maneuver prompts.
11. **Missions**: the six selectable missions and the advanced mission
    chain, briefings, outcomes, the log's records.
12. **Dusk and night**: palettes 3-9 (the fades).
13. **Saves**: quick save and load of the module's state.

## Open research items

- A soft touch-down (and its tyre sound), the boom and the spin warning
  were never seen in a recording of the original; touch-down rules are
  ported from the code only.
- The wheel brakes' second bit ($4000 of +2) and key A's role; the
  throttle keys' timing between the original's key events and the port's
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
- The F-16 choice flies the F/A-18 (task 9).
- Free flight does not yet update the pilot record's numbers (flight
  time, crashes; the crash adds 1 to config +$10, not decoded).
- The panel's speed does not drop to 0 at a crash (state bit 15 is set
  by the object collision code, task 7).
- The crash ends free flight's log update: config +$46 (crashes) is not
  counted yet.
- The view's window of object cells is a square, not the original's
  octant lists; the fine water grid's window below height $9000 (by
  heading) draws all cells.
- No look directions in the cockpit, no tower view; F/A-18 only; no
  other objects.
- The map cuts straight to the cockpit (the original flies the outside
  camera down to the aircraft).
