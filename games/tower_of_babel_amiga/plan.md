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
- saved positions, the high score table and loading a custom group, on a folder that stands for the data disk;
- the tower designer with all its menus, pages, checks and the test of a tower;
- the title music, the jingle and every sound effect (envelopes, the lifts' hum, the towers' background sounds);
- widescreen (up to 21:9) and smooth motion, as options;
- capital letters with Shift, and the shell's quick save and load anywhere, going on exactly (game.md, "Quick saves").

Checked against the original, frame by frame or pixel by pixel:

- import and assets, the 3D view, sky and overlays;
- the rules and every object's behaviour;
- the panel's poll;
- the sound channels' starts and envelope ticks;
- the front end's screens and tower A played to its score;
- the designer's screens, and its packing of every tower record.

## Remaining tasks

The original is complete, and both planned enhancements are in. What is left are limits of the host:

- **Keys:** Escape (the Setup page's reset of the screen position, the editors' clear) is Reprise's own menu key, and the host reports A and D as left and right, so the editors' cursor keys (codes 1-4) aren't offered.
- **Mouse Scaling and Mouse Style** are kept without effect, as the host draws the pointer. Mouse Style still colours the designer's cursor.
- **Widescreen:** clicks in the view's widened sides select nothing.

## Not yet covered by a check

These run in native code but have not been compared with the original yet:

- **Objects and animation:** a Proximity Mine leaving a hole (tower flag 8, $E2A ≥ 0); a time bomb speed ($E2E) other than tower A's; both Forcers' animation frames, the Watcher's idle frames and the spiders' reversed-ride frame ($1218); the convertor in either direction.
- **The sky:** sky type 3 (no line list), the sights overlay, and towers with their own lightning or meteors (only checked by poking tower A's flags).
- **The panel:** a click in the view selecting a spider (the pick); the noise view ($D804, a spider gone or cameras forbidden).
- **The designer:**
  - Put / Swap, the password question, Lock;
  - the Colours page's presets and steps;
  - Play/View Tower;
  - the lift and block messages, Clear Level / Clear All, Undo, and the disk commands.

  They ran headless, but weren't compared side by side with the original. The menus, 14 pages, Get Tower, the Colours page and the spider messages were.
- **The data disk:** the requester with files in it, the error box and the Replace question, compared with the original's (the reference has no data disk).
- **Screens not compared:** the Info and Exit pages, the Yes/No requester, the lost-tower screens, the final "all towers solved" screen and the demo sequence. They were drawn from the same code paths, but never side by side with the original's; the View page only up to the reference's crash (game.md, "Copy protection").
- **The timer:** its warning sounds and the time-out compared with the original.
- **Sounds:**
  - the towers' background sounds and the timer's warnings, not yet seen in a recording;
  - the camera's volume offset;
  - the title music and the jingle, not compared with the original's output;
  - the effects' sound itself: only the channel state was compared; the shared Paula model renders it.

## Known bugs


- **Widescreen:** the stars beyond the original's columns repeat its field (the original has no stars there to show).

- **Native:**
  - The moon's animation phase after the menus can differ from the original's. It moves on only when drawn ($E44), and the port's front end draws the view a different number of times before a tower. Not yet confirmed by a check.
- **Original bugs kept on purpose** (see game.md, "Bugs in the original"):
  - the meteor's first partial word takes plane 3 from plane 0;
  - zapping a Freezer after it thawed throws the freeze counter off by one, so the next Freezer stops nothing.

## Open research questions

- **Tower record fields:** $E2E's full range (the designer offers 0–2); flag 9.
- **Main data ranges:**
  - $1C7D0: the designer's 16 x 16 object icons ($A0 bytes each, +$2A a darker floor);
  - $283C0 / $2CDC0 / $2D3C0: the chooser's pictures are known; the rest are open;
  - the panel set's full layout ($23BC0): the button images are known.
- **Object pens 23 and 24** are inverted every game frame ($AB44): the blink. Find which objects use them.
- **Copy protection:** confirm that the View page's hash is the delayed manual-word check (hypothesis), with the right word on the reference.
- **Disk blocks** $538–$6DF are never read ($540 onward is noise).
- **Ghidra seeds:** add the jump table entries $E2CC–$E310.
