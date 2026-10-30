# Beach Volley — plan

Status (2026-10-30): the disk imports and the launcher shows the eight
courts. The game plays its intro, music and menu (equal to the original
frame for frame), then the one-player tour: each venue's card (map,
route, scene, picture, song, voice; equal to the original's screen at
every recorded frame), the match on that venue's court with its music
and effects, the match's end with the winner's picture and jingle, and
the next venue (or the first again after a loss) through all eight, then
the ending with the trophy, after which the game starts again from the
intro. The match is equal to the original's state at every frame of three
recordings (two on London, one on New York after a first match).
Widescreen (up to 21:9, more of the court) and smooth motion (the match
between its 25 frames a second) are options. Two players stop at the menu.
Findings: [game.md](game.md).

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

- [x] Match sound (match/match_sound.cpp): volley.code's player copy with
      the venue's song, the hurry song, the jingles, the volume the vertical
      blank holds the music at, channel 1 held during a spike; the eight
      effects and the crowd's fade. The player's state equal at all 1,538
      frames of `rec_play1`; the effects equal in kind and order to the
      original's calls (47 in `rec_play1`, 30 in an idle match through its
      end; `harness/events.py`, `tools/cmp_sound_events.py`).
- [x] A match's opening (court, whistles) and end ($25CA2: music fade,
      winner's picture, jingle, pause), the time limit, the original's win
      cheat (option, F7 for HELP), the tour through the eight venues (a loss
      back to the first), the flips that force buffer B.
- [x] Venue cards (front/venue_card.cpp): animation.code's map, routes,
      scroll, eight scenes, tiles, picture, palette fade, song and voice;
      equal to the original's screen, palette and scroll at every frame of
      `harness/card.py`'s recordings (`tools/cmp_card.py`).

- [x] The ending (front/ending.cpp): final.code's trophy, scroll, figures
      and their turning and colour cycle, musicfinal, back to the intro on
      fire; equal to the original's loop (figures, pictures, colours: 300
      frames; five whole screens) and its measured timeline.
- [x] Widescreen up to 21:9 (the match shows more of the court, kept on
      the court at its ends; the other screens centred) and smooth motion
      for the match (its court, players and ball between its frames at
      the display's rate; jumps are not smoothed).

## Next

1. Two players: side B under port 0 ($2B32C, $2DA8C, $2D49A, $2EC5A,
   $2E924, the 2P branches in $2950C/$2FC82).
2. Refactor the match code toward the clean form (it follows volley.code's
   routines closely; per-player duplicates are folded, the rest is next).
3. score.code (two players' score screen) and its copy of the player.
4. Smooth motion for the intro's titles (16.7 steps a second).
5. Module: touch controls for the match.
6. The music's volume keys (keypad + and -, 2-40, $28540) are not mapped.
7. Android build check (no new dependencies so far; not built yet).

## Known gaps

- The pause after a point is a CPU loop in the original: 155-157 blanks
  depending on the music's load. The port waits 156; a blank's difference
  moves the chance table one step (research tools set the recorded
  lengths, `BV_PAUSES`).
- Some frames draw the ball 2 pixels left of the original (frame 190 of
  `rec_idle2`; the state is equal, the blit differs). Not yet explained.
- B1's reach test reads a register B1's movement leaves when his walk is
  skipped; the port assumes a nonzero value there (`b1_register_`).
- The pause after a point and the crowd's fade: the crowd's start can come
  a blank later in the original (drawing the big scores takes longer for
  some digits).
- The ending's first 1,200 loop frames and the scroll were not recorded
  frame by frame (the probe missed the loop's direct fire read); its loop
  was checked around frame 49,271 and its timeline from the step log.
- The disk loads between the parts of a tour (1-8 s on the original) are
  brief black gaps of 10 blanks (by request).
- The chance table's place carries over from match to match by 5 places
  (measured once); the New York recording is equal for several nearby
  places, so the exact place is not pinned down by it.
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
