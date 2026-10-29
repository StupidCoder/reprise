# F/A-18 Interceptor (Amiga) — plan

What remains for a complete, playable re-implementation, and the known
bugs and differences. How the game works and what was learned: game.md.

## Where it stands (2026-10-29)

Playable: free flight from the three runways in the cockpit view, the
original's flight model tick for tick, the outside view with scenery and
detail levels, the panel's numbers, the message line with the ground and
stall warnings, the original's sounds (engine, wind, gear, tyres, crash,
beeps), the crash (noise, orange flash, restart, five aircraft, end text).
Options: starting runway, widescreen, smooth motion up to 120 Hz, VHS,
sound. Checked against the original: `flightcheck`, `soundcheck`,
`viewcheck` (game.md section 7). Nothing of this milestone is committed.

Owner's design notes: widescreen and smooth motion stay (own renderer from
game state, rules at 15 Hz); a planned option, off by default, always
draws the nearest detail level (the renderer keeps the level choice apart
from drawing, and must still respect the passes' model rule).

## Remaining tasks towards a full game

1. **Front end**: title picture with the intro tune, credits, flight-log
   disk check and call-sign entry, main menu, free-flight start map
   (places 1-5) and aircraft choice (F/A-18, F-16), Esc / Shift-Esc,
   pilot log read and written (`textply`, `textctl`, `config` layouts);
   the screen-text runner's keyboard mode.
2. **Music**: the title tune's sequencer and instruments (slots 13-34),
   text201.
3. **Cockpit**: attitude ball, compass tape, radar display and range,
   G meter and HUD symbols (heading, pitch ladder, speed and height), the
   eject/brake/gear/ECM lights, latitude and longitude, zoom keys; the
   original's glyph position tables (`$126524`, `$126544`).
4. **Warnings and messages**: fuel (low, critical, exhausted), G load and
   blackout (`$117128`, `$13AB79`, which also halves the engine sound),
   collision proximity (`$11A50E`), radar and missile alerts with their
   beeps ($01, $02, $10, $100, $800).
5. **Collisions**: scenery and the ground at speed (`$11B6A2`, state bit
   15, the impact noise `$10A0DC(28, 48)`); ditching in water.
6. **Other views**: the external and tower views the view keys select
   (`$10E4xx`), with their sound rules (volumes for outside views).
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

- The apron right of runway 1 shows grass where the original paves it.
- Ditching in water ends like a crash (the original's rule is not
  traced).
- After the last aircraft the original returns to its main menu; the port
  starts free flight again (no menu yet).
- Ground collisions at speed and scenery collisions do not crash the
  aircraft yet; the panel's speed does not drop to 0 at a crash (state bit
  15 is set only by the collision code).
- The view's window of cells is a square, not the original's octant
  lists; the coarse water grid is not drawn.
- Only the cockpit view exists; F/A-18 only; no other objects.
