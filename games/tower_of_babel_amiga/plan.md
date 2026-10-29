# Tower of Babel (Amiga) — plan

What is still missing for a fully playable re-implementation, and the known
bugs. How the game works and what has been verified so far: [game.md](game.md).

## Where it stands (2026-10-29)

Playable in the shell from the title:

- the credits, the player's name, the main menu;
- the Standard, Tutorial and Custom games with the tower chooser and the tower's information;
- every tower with the panel, programs, cameras and every object;
- the pause menus (Setup, View, Info, Exit with Abandon and Restart);
- the score screen and the end screens with their demonstrations, and the demo sequence;
- the title music, the jingle and every sound effect (envelopes, the lifts' hum, the towers' background sounds).

Checked against the original, frame by frame or pixel by pixel:

- import and assets, the 3D view, sky and overlays;
- the rules and every object's behaviour;
- the panel's poll;
- the sound channels' starts and envelope ticks;
- the front end's screens and tower A played to its score.

## Remaining tasks

### 1. The rest of the front end (next)

- **High scores, view and update ($A81E):** the table, the entry of a new score, where they are kept. Map them to Reprise storage. The menu item does nothing yet.
- **Save and load a position ($A6B6):** find the format, then the shell's saves. The menu item does nothing yet. Quick save (`write_state`) is not offered yet either.
- **Custom game:** "Load a Group" from a disk does nothing yet.
- **Locked towers and their passwords ($D9E):** find where the password is asked. The chooser opens towers by neighbours only.
- **Keys in the menus:** Escape in Setup (resets the screen position), the editor's cursor keys (codes 1–4).
- **Capital letters in the name:** the shell reports keys without Shift, so names are lower case.

### 2. Tower designer ($E386)

- Editing cells, objects and colours, the designer's object list ($6F10), the name, author, password and task fields; its menus (the numbered texts $3A–$56: colour names, checks such as "No Grabber").
- Saving custom towers: the original's format and where it writes them; map them to Reprise storage. "Use Designer Contents" then uses them.
- The full object kind → designer name mapping.

### 3. Enhancements (after the original is complete)

- **Widescreen:** the view is drawn from game state, so widen the frustum, then check the sky, stars and overlays at the new width.
- **Smooth motion up to 120 Hz:** interpolate spider, object and camera positions between game ticks (`GameModule::set_frame_fraction`).

## Not yet covered by a check

These run in native code but have not been compared with the original yet:

- **Objects and animation:** a Proximity Mine leaving a hole (tower flag 8, $E2A ≥ 0); a time bomb speed ($E2E) other than tower A's; both Forcers' animation frames, the Watcher's idle frames and the spiders' reversed-ride frame ($1218); the convertor in either direction.
- **The sky:** sky type 3 (no line list), the sights overlay, and towers with their own lightning or meteors (only checked by poking tower A's flags).
- **The panel:** a click in the view selecting a spider (the pick); the noise view ($D804, a spider gone or cameras forbidden).
- **Screens not compared:** the Info and Exit pages, the Yes/No requester, the lost-tower screens, the final "all towers solved" screen and the demo sequence. They were drawn from the same code paths, but never side by side with the original's; the View page only up to the reference's crash (game.md, "Copy protection").
- **The timer:** its warning sounds and the time-out compared with the original.
- **Sounds:**
  - the towers' background sounds and the timer's warnings, not yet seen in a recording;
  - the camera's volume offset;
  - the title music and the jingle, not compared with the original's output;
  - the effects' sound itself: only the channel state was compared; the shared Paula model renders it.

## Known bugs

- **Native:**
  - The moon's animation phase after the menus can differ from the original's. It moves on only when drawn ($E44), and the port's front end draws the view a different number of times before a tower. Not yet confirmed by a check.
- **Original bugs kept on purpose** (see game.md, "Bugs in the original"):
  - the meteor's first partial word takes plane 3 from plane 0;
  - zapping a Freezer after it thawed throws the freeze counter off by one, so the next Freezer stops nothing.

## Open research questions

- **Tower record fields:**
  - $E2E's full range;
  - $E34–$E42 (only $E38, sky type, and the moon fields are known);
  - flags 3 and 9;
  - the last 4 bits ($A4–$A8(a6)).
- **Main data ranges:**
  - $1C7D0 (designer map icons?);
  - $283C0 / $2CDC0 / $2D3C0: the chooser's pictures are known; the rest are open;
  - the panel set's full layout ($23BC0): the button images are known.
- **Object pens 23 and 24** are inverted every game frame ($AB44): the blink. Find which objects use them.
- **Footsteps:** find what $A6 = 0 means exactly; the "no footsteps" reading is a hypothesis (6 towers set it).
- **Copy protection:** confirm that the View page's hash is the delayed manual-word check (hypothesis), with the right word on the reference.
- **Disk blocks** $538–$6DF are never read ($540 onward is noise).
- **Ghidra seeds:** add the jump table entries $E2CC–$E310.
