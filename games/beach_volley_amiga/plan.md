# Beach Volley — plan

Status (2026-10-30): the disk imports and the launcher shows the eight
courts. The game plays its intro, music and menu (equal to the original
frame for frame), then a one-player match on the first court (London):
serves, rallies, the computer's side, side A's plays under the joystick,
points and the scoreboard, equal to the original's state at every frame
of two recordings (2,235 frames). The match has no sound yet; it stops
with a note when it ends, and two players stop at the menu. Findings:
[game.md](game.md).

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
- [x] Match (match/*.cpp): one-player match on venue 1 from volley.code's
      rules: both serves, the shot decision tree, the ball's flight and
      camera, the computer's four players' movement and plays, side A1's
      plays (bump, dig, crouch, low dig, dives, block hop, jump spike,
      fall), points, scoreboard digits, big scores, match clock dots.
      Equal to the original's object table at every frame of
      `rec_idle2` (700) and `rec_play1` (1,535, random human input);
      sampled screens pixel-identical (`cmp_screens.py`). Quick saves of the
      match (`beach_volley_assets match-save`).

## Next

1. Match sound: volley.code's music player copy ($30E6C/$30EB2: three
   music channels, the fourth for effects) with the venue song and the
   hurry song; the effects ($292BE-$294BA one-shot samples in the block).
2. Match end ($25CA2): winner banner, jingles, next venue (venues 2-8 and
   their chance tables), the time limit (clock at 22 -> side B wins).
3. Two players: side B under port 0 ($2B32C, $2DA8C, $2D49A, $2EC5A,
   $2E924, the 2P branches in $2950C/$2FC82).
4. Refactor the match code toward the clean form (it follows volley.code's
   routines closely; per-player duplicates are folded, the rest is next).
5. Front end after the menu: venue cards, map (carte, animation.code),
   score screen (score.code), ending (final.code, trophe again).
6. Music in the other parts: score.code's, animation.code's and
   final.code's copies of the player.
7. Module: touch controls for the match, widescreen (the court is 576
   wide: show more of it), 120 Hz smoothing, cheats (the original's
   11-key code + HELP).
8. Android build check (no new dependencies so far; not built yet).

## Known gaps

- The pause after a point is a CPU loop in the original: 155-157 blanks
  depending on the music's load. The port waits 156; a blank's difference
  moves the chance table one step (research tools set the recorded
  lengths, `BV_PAUSES`).
- Some frames draw the ball 2 pixels left of the original (frame 190 of
  `rec_idle2`; the state is equal, the blit differs). Not yet explained.
- B1's reach test reads a register B1's movement leaves when his walk is
  skipped; the port assumes a nonzero value there (`b1_register_`).
- The time-up quirk that plots a clock dot at the sprite sheet's corner
  (index 11) is not reproduced.
- The red flash ($DFF180) when the ball lands out is not shown.

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
