# Tower of Babel (Amiga) — plan

What is still missing for a fully playable re-implementation, and the known
bugs. How the game works and what has been verified so far: [game.md](game.md).

## Where it stands (2026-10-29)

Done and checked against the original, frame by frame, with no differences:

- import and asset decoding;
- the 3D view with objects, sky, effects, particles and overlays;
- spider programs, moves and firing;
- every object's own behaviour.

The launcher shows previews only. Nothing is playable yet.

## Remaining tasks

### 1. Playing a tower (next)

- **Panel input ($ADFC):**
  - spider select (Zapper / Pusher / Grabber), Fire, turn and move arrows;
  - the four cameras N E S W (camera records 0–3);
  - Pause, Stop/In, Run/Out, Run All;
  - program editing: Write, Delete, Clear, the program strip, cursor;
  - the panel lights.
  - Check each against the original with the harness, polling once per game frame.
- **Mouse picking:** the cell under the pointer is found from the top faces drawn ($1507C, $7EE–$7F4). Find out what the game uses it for.
- **Keyboard:** find out whether play has keyboard controls (the key byte $692(a6) is read in play?), and add them.
- **READY state:** the start condition. The tower starts on the first direct move ($11E6, then $11E4 = 0); programs alone do not start it. Also check what the player sees while the original copies buffers around its draws ($87A6).
- **The tower's end:**
  - the time limit and timer ($E18, BCD $121A, no-limit flag 5) and running out of time;
  - the end countdown ($11E0/$11E2/$FC0) and end reasons 1, 3 and 4 (other reasons?);
  - the end screen and the jingle $3AF2 (started at $9A8E);
  - scoring and "percent complete";
  - what happens next (back to the chooser, next tower).
- **Game frame timing:** a game frame lasts at least 14 video frames ($87EE; 50/14 ≈ 3.6 updates per second is a hypothesis). Measure it on the reference, including whether the view's cost changes it. The native rules must tick at that rate, independently of drawing.
- **Playable module in the shell:**
  - `entry.cpp` runs a chosen tower with the native view, panel, input and sounds;
  - the panel graphics and layout from the panel set ($23BC0; panel sets 1–3, the "Panel Style" option);
  - pause menu and quick save in Reprise's usual way.

### 2. Front end

- Title screen with the sampled title music (one segment list, $3B24).
- Name entry, main menu ($8B9C), Restart.
- Game types: Standard (13 groups of 9), Custom, Tutorial.
- Tower chooser ($A542 buttons, 3 × 3 grid, neighbouring groups, percent complete).
- Tower information ($A12A).
- Locked towers and their passwords ($D9E).
- Options:
  - view offsets $FB8–$FBC;
  - "Backwards allowed" $110E;
  - overlay position $1112/$1114;
  - panel style $9F20;
  - the sound settings $A4–$A8(a6) (hypothesis).
- High scores, view and update ($A81E).
- Save and load a position ($A6B6): find the format and map it to Reprise saves.
- Demo sequence ($E2F4): the 18 demonstration towers. Find where the demo spiders' programs come from.
- Leave out the manual-word copy protection and the "Waiting for Disk" check.

### 3. Tower designer ($E386)

- Editing cells, objects and colours, the designer's object list ($6F10), the name, author, password and objective fields.
- Saving custom towers: the original's format and where it writes them; map them to Reprise storage.
- The full object kind → designer name mapping.

### 4. Sound

- Effect records ($3504–$3AF1, 34 of 46 bytes): find the envelope semantics (+$1C/+$20/+$26/+$2A, the second part at +$2E).
- Channel choice by priority ($15540), up to 16 queued a frame ($15316/$1531E).
- Native mixer playback.
- Compare the effects the native rules start (already collected per frame) with the original's queue.
- Find what $A6 = 0 means for footsteps; the "sound setting" reading is a hypothesis.

### 5. Enhancements (after the original is complete)

- Widescreen: the view is drawn from game state, so widen the frustum, then check the sky, stars and overlays at the new width.
- Smooth motion up to 120 Hz: interpolate spider, object and camera positions between game ticks.

## Not yet covered by a check

These run in native code but have not been seen in a recording yet:

- a Proximity Mine leaving a hole (tower flag 8, $E2A ≥ 0);
- a time bomb speed ($E2E) other than tower A's;
- both Forcers' animation frames, the Watcher's idle frames and the spiders' reversed-ride frame ($1218);
- the convertor in either direction (beam to push and push to beam);
- sky type 3 (no line list), the sights overlay, and towers with their own lightning or meteors (only checked by poking tower A's flags);
- every sound: collected, but not compared.

## Known bugs

- Native: none known. Every check so far shows 0 differences.
- Original bugs kept on purpose (see game.md, "Bugs in the original"):
  - the meteor's first partial word takes plane 3 from plane 0;
  - zapping a Freezer after it thawed throws the freeze counter off by one, so the next Freezer stops nothing.

## Open research questions

- Tower record fields:
  - $E2E's full range;
  - $E34–$E42 (only $E38, sky type, and the moon fields are known);
  - flags 3, 4, 6–9 (6 lightning off, 7 meteors off, 8 no mine holes are inferred from the code);
  - the last 4 bits ($A4–$A8(a6)).
- Main data ranges:
  - $1C7D0 (designer map icons?);
  - $283C0/$2CDC0/$2D3C0 small images;
  - the panel set's layout ($23BC0).
- Object pens 23 and 24 are inverted every game frame ($AB44): the blink. Find which objects use them.
- Disk blocks $538–$6DF are never read ($540 onward is noise).
- Ghidra seeds: add the jump table entries $E2CC–$E310.
