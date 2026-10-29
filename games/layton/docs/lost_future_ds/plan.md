# Lost Future (Nintendo DS): plan

What remains for a complete re-implementation of the third adventure
(`C3JD`), and the known bugs and differences. How the game works:
[game.md](game.md). Detailed notes: `.analysis/adventure3/docs/`.

## Where it stands (2026-10-29)

Playable from the logos to the end credits:

- all 224 puzzles, every special type, run from the game's scripts on the
  reconstructed LSCR machine;
- name entry, the bag, the three save slots and the mystery menu;
- the parrot, toy car and picture book minigames;
- movies, voices, music and sound effects.

An automatic player walks the story from a new game to the credits without
stand-ins. Conveniences in place: music player, theatre, puzzle mode,
minigame free play, skipping, checkpoints, touch-area help. None of the
missing loops below is needed to finish the story.

## Remaining tasks

1. **Loops not reconstructed yet**: the parrot's story event
   MINI_PARROT_EVENT (ov24), the puzzle index MENU_NAZOBEE / MENU_NAZORINE,
   the dictionary MENU_JITEN (ov18), the secrets SCRT_* and the Wi-Fi menus
   WIFI_*. The research runner can skip them; the app must not need to.
2. **Puzzle index mode**: the hints' free state (bg_hN4) and the "prize" page.
3. **Wi-Fi puzzles** (169–222): decide how to offer them. 19 have no German
   text; six expect kana that the European code table cannot produce.
4. **Message palette colours** (command 4642): not used yet.
5. **Ink**: find where the palette entries 0xF0+ come from (the colours are
   measured). Reconstruct the line rasteriser (`02045A0C`); the port stamps a
   square pen with ~75 % pixel overlap.
6. **Upper-screen dimming**: find the register behind the measured 15/16
   colour of upper-screen objects. Picture objects are drawn ~8 % brighter
   than on the console.
7. **Touch filter**: this game's own filter and release-frame position are
   assumed to be the second game's. Verify them.
8. **Random numbers**: recover the C library's power-on seed, so the random
   result picture and other draws match.
9. **Minigames against the ROM**: the parrot's flight, the toy car and the
   picture book have never been compared with ROM captures. Their open
   details:
   - parrot: the inverse square root's rounding; whether the preview is silent;
   - car: the store at a new piece's +1000; the two unused course numbers;
   - book: the `<CS>` text colour, the line pitch, the page markers, the page
     counter showing 00 on page 1, a book covered while open.
10. **Loader rules**: confirm the stored (type 0) compression and the LPC2
    lookup in the game's loader code. Find out whether the shared wave banks
    link like the second game's banks.
11. **Saves**: the slots use the port's own container (`PCR3SAV1`), not the
    cartridge layout. Recover the backup format if save compatibility is
    wanted.

## Known bugs and differences

- **Upper-screen pictures** are slightly brighter than on the console (item 6).
- **Thick ink**: drawn with a square pen.
- **Random result picture**: uses the port's own random numbers.
- **Stream release**: a failed stream open gives its object back natively
  (**hypothesis**; the ROM's failure path was not traced). Without this, a few
  missing files blocked every later stream.
- **Load timing**: loading pauses differ (the ROM waits for file loads: 38
  frames for the text table, 24 between scenes). Some sound-length waits end
  a few frames apart.
- **Latency**: the DS shows sprites and 3D a frame late; the port doesn't.
- **Slanted lines**: in LINE puzzles, they differ from the DS by a few pixels.
  The scale arm's edges differ.
- **Sound**: one redundant slider stop natively, the jar's release sound in
  SCALE one frame early, and a silent "play −1" the ROM issues in LTGRAM.
  The title voice runs ~90 frames natively against ~85.
- **Errors**: the port stops on an unregistered command or unknown condition;
  the original ignores them.

## Checks to run after changes

- `.analysis/adventure3/tools/a3_regress.py PACK`: every recorded trace.
- `picarat_a3_loop_run PACK OUT 0 224 x 0`: the support survey.
- `picarat_a3_story_run` with `PICARAT_AUTO`: the story to the credits.
- `ctest` (`a3_lscr_tests`).
