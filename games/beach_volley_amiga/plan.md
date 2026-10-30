# Beach Volley — plan

Status (2026-10-29): the disk imports (52 files, every one checked by
SHA-256) and the launcher shows the eight courts with their palettes. The
game starts: the intro with its music and voices and the players menu, equal
to the original frame for frame; choosing stops with a note (the match is
next). Findings: [game.md](game.md).

## Done

- [x] Release identified (`Beach_Volley_1989_Ocean_cr.adf`, SHA-256 b5c1ce38…).
- [x] Load chain and load addresses of the five code files.
- [x] Cruncher recovered (shared `amiga/backward_cruncher`); all 46 files unpack.
- [x] Import with release check, per-file hashes, rejection of other disks
      (all eleven other ADFs in `images/` give NotThisGame) and of damaged
      files.
- [x] Courts: 576 × 232 pictures and their palettes as previews;
      `beach_volley_assets import|export`.
- [x] Research harness (port 6872) reaches the first match.
- [x] Intro (front/intro.cpp): titles, trophy, logos, credits, copper
      palettes, comic with voices, menu; 1007 frames equal to chip RAM
      (`harness/frames_all.py`).
- [x] Music player (sound/soundtracker.cpp), intro.code's copy: 3,000
      vertical blanks equal to the original's variables.
- [x] Module: 50 Hz, VHS option, quick saves of the real state (screens,
      copper, counters, player, Paula; `beach_volley_assets save-check`).

## Next

1. Match screen from game state: court scroll ($29018), sprites/players
   (animation1/2 at $53E00/$66A62), ball, net, scoreboard digits ($26210).
2. Rules: serve, rally, touches per side, scoring, side changes, match end;
   computer players; read from volley.code with the reference as oracle
   (recording harness like the other Amiga games).
3. Front end after the menu: venue cards, map (carte, animation.code),
   score screen (score.code), ending (final.code, trophe again).
4. Music in the other parts: volley.code's, score.code's, animation.code's
   and final.code's copies of the player (their channels and pause flags);
   the match's sound effects.
5. Randomness: find the generator and decide per ADDING_A_GAME §3.
6. Module: fixed tick, input (keyboard, gamepad, touch), VHS option,
   quick saves of real state, widescreen (the court is 576 wide: show more
   of it), 120 Hz smoothing, cheats.
7. Android build check (no new dependencies so far; not built yet).

## Known gaps

- Map, score and ending picture layouts not yet read.
- The host has no F2 key: two players is 2, the gamepad's second button or
  a click on the choice. (Adding F2 touches the shared host.)
- Intro pace: the port keeps the program's 3 blanks a step; the reference
  needs 3.6 on average (drawing time), so the port's titles are shorter.
- The comic's pauses are a CPU delay loop; the port uses the 147 blanks
  measured from page to page (observed 146-148).
- Smooth motion for the intro's titles (they step 16.7 times a second) is
  not offered yet.
- Rock 'n Roll still has its own copy of the cruncher stream
  (`rock_n_roll_amiga/import/cruncher.cpp`); it could use the shared one.

## Open bugs

None known.
