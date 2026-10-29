# Spectre's Call (Nintendo DS): plan

What remains for a complete re-implementation of the fourth adventure
(`BLFD`), and the known bugs and differences. How the game works:
[game.md](game.md). Detailed notes: `.analysis/adventure4/docs/`.

## Where it stands (2026-10-29)

Playable from the title to the end credits:

- the story, with event saves, the bag's save and continuing from the title;
- the lost-puzzle list;
- the bonus section: puzzle index, challenges and the secrets;
- the bag's three minigames and the whack-a-mouse;
- 228 of the 229 catalogue puzzles (the last is a leftover without a
  puzzle);
- films, music and sound effects.

Wi-Fi downloads end in the original's connection error. Everything runs from
the game's GSS1 scripts on the reconstructed machine. Conveniences in place.

## Remaining tasks

1. **Places**: the geometry helper (place loop 2400 1000), map routes
   (`020C28A8`, 3211/3225), area picture variants (2143), lighting records
   (3203, 4330..4332 colour treatment). These are still approximations.
2. **Escape and packing puzzles**: the session's state-2 verdict flags
   ("square packing", "6×6 magic square", "queen placement", "escape
   puzzle") are not reconstructed.
3. **Characters' faces in events**: pose sequences show no faces yet.
4. **Puzzle index inside the story** (2130: 3408, 3410, 3430, 3440..3442,
   6140, 6230) and the bag's other pages: not checked against the ROM.
5. **Memo**: `lt4_memo` undo and redo through the mode-8 recorder; paint
   modes 2, 3, 5 and 9 (they throw).
6. **Saves**: the card error loops (8000.., never entered natively), the
   boot's slot check, AutoSave's callers (name entry, puzzle index, Wi-Fi,
   challenges), and the cartridge's save layout (the port keeps its own files).
7. **Message details**: talk messages (6101, flag 6 from `<K>`), message tags
   other than C, T, W and L, the typing sound's timer.
8. **Hints, coins and totals**: "Verdiente Pikarat" on the verdict shows 0;
   coin and Wi-Fi counts (3456).
9. **Places parented to places** (8002's fourth operand naming a place); the
   places' own update.
10. **Unidentified roles**: 3905 returns 0 (the ROM had 1); 6133's message
    +0x11C, 2922's game +0xAAF8, 9100's flag, 4120/4121; GTXT byte 5 and
    halfword 0x22; GFSP header byte 5 and word 0x10; GFNT metrics nibbles;
    GCSU (`conv_list.gu`) layout.
11. **Random seeding** (`0207B698`): when the game seeds its generator.
12. **Sound**: 4912, 4930 and 4920's per-group readiness answer fixed values;
    the manager's queues are carried out at once. Event voices and film sound
    are not checked against the ROM.
13. **Bulldozer** (type 19, ov23): no puzzle uses it; reconstruct only if
    wanted.
14. **Catalogue leftovers**: decide how the index hides them (23 rows; the
    game's own filter is not checked).

## Known bugs and differences

- **Sound-gated waits**: waits and result windows can come a frame or more
  earlier than on the console, which first loads the sound (railway result
  ~29 frames, fish death sprite, coin spin phase).
- **Lost-puzzle list**: its notice follows the code but was not compared with
  a ROM save holding lost puzzles.
- **Wi-Fi**: the failure message and timing are a hypothesis based on the
  first game's measurement. "Puzzle of the week" never holds puzzles.
- **Index lists**: the game appends while playing; the port sorts.
- **Films**: the port counts the due frame from its own 60 frames a second.
  A flagged film is assumed never to show full screen.
- **3D edges**: the DS 3D engine shifts texels at the top clipping edge
  (railway, whack-a-mouse tree); the port doesn't.
- **Blending**: Ltgram's semi-transparent piece pixels differ by up to 15.
- **Dark mazes** (71, 187): the ROM leaves a one-pixel row at the top and
  bottom of the light circle.
- **Rotated pictures** are sampled at pixel centres, not by the DS
  rasteriser. Picture hit tests with flag bit 1 are taken as misses.
- **Hypotheses in use**: the screen shake (4241/4242), the memo's second
  store, the GFNT 64 KiB wrap, Slide's quarter turns, and the tween random
  curves' arithmetic (an approximation).
- **Recogniser**: rules 1, 2, 5, 6 and 12 are not ROM-checked (unreachable
  with this game's boxes). Only five written-answer puzzles were solved end
  to end; narrow 38-pixel boxes (puzzles 20, 114, …) were not tried.
- **Fish tank**: the top message shows "???" without a fish name; the story's
  value is not checked.
- **Runner luck**: the step-520 candle puzzle has a fixed tap order, so the
  automatic player stalls there now and then. This is not a game bug.

## Checks to run after changes

- `picarat_a4_loop_run PACK OUT FRAMES 0 sweep x`: every puzzle, no missing
  service.
- The solver inputs in `research/evidence/special-loops/` for the special
  loops.
- `picarat_a4_story_run` with `A4_AUTO` from a step snapshot to the staff roll.
- `ctest` (`a4_gss_tests`, `a4_saves_tests`).
