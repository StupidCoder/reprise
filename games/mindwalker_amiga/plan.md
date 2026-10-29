# Mind Walker (Amiga) — plan

What is left before the re-implementation is complete, and the known bugs.
How the original works, and where the evidence is: [game.md](game.md).

## Where it stands (2026-10-29)

Nothing of the game is left unbuilt. It plays from the title and world menu
through the landscape, tunnel, arena and psyche screen to the last picture,
its finale and the Hall of Fame, with saved worlds, the pause boxes and
sound. Every stage has recordings that equal the original read for read in
everything logged (`harness/regress.sh`); screens match pen for pen except
the polygon-edge items below.

Finished, in order:

1. Disk import and assets (seven stage programs, pictures, image sets,
   palettes, samples, high scores).
2. Reference harness (`mwrec.py` recorder, world and view oracles).
3. Title stage (T): title, world menu, new game; topaz 8 and Intuition's
   gadget imagery embedded.
4. World generator.
5. Landscape (A): view builder and painter, top bar, walking, flying
   creatures, score and catch pictures, walker mask, zapping, map, finds,
   beacons, the way on, pyramids, pause, sound.
6. Tunnel (B), arena (C), psyche screen (P).
7. T after a game: Hall of Fame, Fame screen, command screen, saved worlds.
8. Cheats (god mode, minimap, auto transform); quick save (persistent) and
   VHS look.
9. The whole game start to end, T's pause box, a natural tunnel catch.

## Remaining tasks

### Accuracy

- [ ] AreaEnd line rounding: the kaleidoscope's thin-edge texture (also
  30-1,800 pixels inside the last picture's dumps) and the tunnel rings'
  shallow edges (~400 of 57,600 pixels). An AreaEnd oracle on single
  quadrilaterals would settle it.
- [ ] The landscape view's polygon fill still differs by one-pixel edges
  (2 of 60,160 view pens in the recordings).

### Recordings still to make

- [ ] Landscape: a revisited view bringing its creatures back (needs a
  revisit within seven views with creatures), zapping an attached creature
  (+5), the hidden marker's exact first frame, losing a life while both
  finds are held, the marked block keeping creatures away, the edge-push
  counter `$20B382` (h26 every 8 frames, tether clock reset).
- [ ] Arena: a creature's touch and the lightning (only lightly covered).
- [ ] T: the pause box over the Hall of Fame, command screen and initials
  board (recorded over the menu only).
- [ ] The slider drag (Intuition): modelled, not compared; the knob position
  for HorizPot ≠ 0 is a hypothesis.

### Research

- [ ] What each of S's shared image sets and icons is used for, and its
  colours.
- [ ] Sample playback periods and triggers not yet tied to a rule.
- [ ] The shared block's fields still marked as hypotheses in game.md
  (+$18…+$58 and the markers' meaning).
- [ ] The release's video standard (the port steps at 50 Hz like the PAL
  reference machine).
- [ ] Arena: the window top reads 0 at `$401086` in the log although the
  room starts at row 10.

### Enhancements (later, off by default)

- [ ] Dual analog stick controls and a freely rotatable camera in the
  landscape instead of the four compass views (the view painter already
  takes the projection as a replaceable camera).

## Known bugs and inexact spots

- The paces of the frame-less sequences are hypotheses: the arena's death
  (44 scroll steps) and wall death at one step per frame; the last picture's
  passes at one per 60 ms timer tick (as the code waits for it).
- The beacon bonus and the music's tempo follow the port's steady 50 fps;
  the original slows down under load. The sound's timer phase cannot be
  matched exactly.
- The initials board's loop reads continuously in the original (modelled
  at one read a frame).
- Loading pictures are held a fixed 2 seconds (owner's decision; the
  original's disk loading is longer).
- The landscape renderer is close, not pixel-exact (owner's decision).
