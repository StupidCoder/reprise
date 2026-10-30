# Lost Future (Nintendo DS): plan

What remains for a complete re-implementation of the third adventure
(`C3JD`), and the known bugs and differences. How the game works:
[game.md](game.md). Detailed notes: `.analysis/adventure3/docs/`.

## Where it stands (2026-10-30)

Playable from the logos to the end credits:

- all 224 puzzles, every special type, run from the game's scripts on the
  reconstructed LSCR machine;
- name entry, the bag, the three save slots and the mystery menu;
- the parrot, toy car and picture book minigames;
- the bag's puzzle index, Granny Riddleton's shack and every bonus page;
- movies, voices, music and sound effects.

An automatic player walks the story from a new game to the credits without
stand-ins. Conveniences in place: music player, theatre, puzzle mode,
minigame free play, skipping, checkpoints, touch-area help.

## Remaining tasks

1. **Menu loops** (done 2026-10-30): the bag's puzzle index MENU_JITEN,
   Granny Riddleton's shack MENU_NAZOBEE / MENU_NAZORINE and the bonus
   pages SCRT_CHARA / VOICE / MOVIE / IMAGE / SOUND / ROOM run on their
   programs with the classes' commands (`story/a3_menus.*`). Open in them:
   - the screen swap (2031) ends when the page's loop does (**hypothesis**:
     the ROM's way back to 0 was not found);
   - "any object moving" (`020B0BF8`, used by the music page) is read as a
     position or offset tween (**hypothesis**);
   - none of these pages was compared with a ROM capture.
   MINI_PARROT_EVENT stays unbuilt on purpose: nothing in this version
   reaches it and its data is not on the cartridge (game.md, section 5). The
   Wi-Fi menus WIFI_* are out of scope.
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

- **Secret room password**: made from a fixed Wi-Fi address of the port's
  own (the console's differs), so it shows `6A4C5F3D` on every machine.

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

## In Reprise (2026-10-29)

The game runs inside Reprise (catalog id `lost_future_ds`); the integration is
described in [../reprise.md](../reprise.md).

Connected: import (C3JD NDS, streamed), Start (the title), Continue and quick
save/load (the last room checkpoint, `checkpoint.sav` record), the slots
`slot0..2.sav` in `<data>/progress/lost_future_ds/`, the VHS filter per screen,
touch-area help, takeover of Picarat's pack and `c3jd-saves` slots. No
imported-data previews yet.

Not offered in Reprise (Picarat shell features, by decision): music player,
movie theatre, puzzle mode, minigame free play, the hub's "Continue story".
Movies, music, puzzles and minigames play as part of normal play.

Remaining for Reprise:

- typed name entry (the touch keyboard works);
- an accelerometer for the 3DS games on phones (Picarat had one);
- the screen filter's CPU fallback path is untested on a renderer without
  GPU shaders.

Validation:

- Validated on macOS (2026-10-29): `ctest -R layton_module` with
  `LAYTON_PACKS` (the story starts from the title with picture and sound);
  the real window driven by `reprise --events` scripts. Android: the APK
  builds; only Curious Village was run on an emulator (start, touch,
  rotation). No physical device was used.
- Quick save round trip checked with a checkpoint written by
  `picarat_a3_story_run` (`PICARAT_AUTO=0`, 15 000 frames): continues in a room
  (Vor dem Uhrenladen).
- Not run on Android.
