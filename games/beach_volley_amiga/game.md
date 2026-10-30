# Beach Volley (Amiga) — findings

Research started 2026-10-29. Everything here comes from the user's disk image
and from running it in the research emulator. Status words follow
[RECONSTRUCTION.md](../../RECONSTRUCTION.md): **observed**, **interpreted**,
**hypothesis**. No outside game-specific material (disassemblies, ports,
maps, guides) has been consulted for this game. Catalog text and cover art
are editorial, not evidence: the release is dated 1989 by
[Lemon Amiga](https://www.lemonamiga.com/game/beach-volley), and the cover
is original interpretive artwork.

Working notes, scripts and captures live in `.analysis/beach_volley_amiga/`
(gitignored evidence store; "Evidence" below says how to regenerate it).

## Release

| | |
|---|---|
| Image | `Beach_Volley_1989_Ocean_cr.adf`, 901,120 bytes |
| SHA-256 | `b5c1ce38704942886ce39025b6d88067eb6dd0b35a9f3897db3af41156a6f27a` |
| SHA-1 | `9f0816aeeb2d10a5bf3dc2544949cb556130b065` |
| Recognised by | SHA-256 of `volley.code`, `e85f65b79d796a14d261b3597740d0fc697fe2864521e81c28da98bff9cc3e31` |
| Kind | cracked copy: the cracker's `Icon&Install.lha` sits beside the game's files. Nothing in the load chain refers to it, and the game files show no sign of change (**interpreted**) |

## Disk (observed)

AmigaDOS OFS volume "Beach Volley" with a standard DOS boot block. The game
loads files by name through dos.library (`df0:…`; volley.code $31980:
`Open`, `Read` of up to $10FFF bytes to the address in D4, `Close`).

Load chain:

1. `s/startup-sequence` runs `volley`, a 280-byte hunk program: it reads
   `df0:intro.code` to $23000 and jumps there.
2. intro.code shows the Ocean logo, the scrolling credits and the comic-strip
   pages, then the 1/2-player menu (F1/F2), and loads `volley.code` to
   $25000.
3. volley.code is the match program. It loads `score.code` to $23000 (over
   the intro), `animation.code` to $3BC50 and `final.code` to $49800
   ($31380-$31946), and each venue's files.

| File | Runs at | Loaded by |
|---|---|---|
| intro.code | $23000 | volley |
| volley.code | $25000 | intro.code |
| score.code | $23000 | volley.code $313E6 |
| animation.code | $3BC50 | volley.code $31384 |
| final.code | $49800 | volley.code $31922 (jumped to) |

The code files are flat images (no hunks). Files each part names:

- intro.code: trophe, introp1, introp2, music13, instruments, volley.code
- volley.code: terrain1-8, animation1-2, music2/3/8/12, instruments, the
  other code files
- animation.code: carte, anim0-7, music5-7/9-13, instruments, dnv0-7
- score.code: score, music13
- final.code: trophe, final, musicfinal, intro.code

## Crunched files (observed, recovered)

Every file except the code files and `instruments` is crunched. volley.code
reads a file into the buffer at $324C0 and calls the decruncher at $31B90
with A0 = end of the file and A1 = destination. The file ends with two
longs, checksum and unpacked length; the bit stream before them is read
backwards from its last long. Commands: see
[shared/amiga/backward_cruncher.hpp](../../shared/amiga/backward_cruncher.hpp).
All 46 crunched files unpack exactly to their stated length with a zero
checksum.

**Original quirk:** the decruncher computes the checksum (D5) but ignores
it (both branches at $31C42 return), so a damaged file would unpack silently
there. The import rejects such a file instead.

## Display (observed)

Match screen, from volley.code's copper list at $290DA as it runs: four
bitplanes at $324C0, $34730, $369A0, $38C10 ($2270 bytes apart), BPLCON0
$4000, DDFSTRT/STOP $38/$D0, both modulos -2 (rows 38 bytes apart),
DIWSTRT/STOP $3791/$FFB0. The buffer is 304 × 232 pixels; the window shows
288 × 200 of it. The 16 colours are written into the copper list at $2911E
by $2814A.

The screen buffer doubles as the file load buffer ($324C0); loading happens
only between matches (**interpreted**).

## Courts (observed; decoded for previews)

The venue number (1-8) is the word at $25B70. `terrainN` unpacks to $43900:
576 × 232 pixels, four planes one after the other ($4140 bytes each, rows of
$48 bytes). $28FCC sets up four blits (BLTSIZE $3A13: 232 rows × 19 words)
from $43900 plus the scroll offset in $29018 to the screen, so the court
scrolls sideways under a 304-pixel window. Its palette is entry N-1 of the
table at $29190 (8 × 16 words; pointers at $28180, chosen at $28150 from
$25B70). Decoder: [data/assets.cpp](data/assets.cpp).

Scenery of the eight pictures (**interpreted** from the artwork, not from
game text): 1 London, 2 New York, 3 a sandy beach, 4 Egypt, 5 a red-rock
desert at night, 6 Japan, 7 Moscow, 8 Paris.

$26210 copies two score digits into the court picture itself (digit
pictures via the table at $26286, 10 rows × 1 word × 4 planes, to
$43900+$1340/$1344): the scoreboard is drawn into the background. The
imported pictures therefore show blank boards.

## Other files (sizes observed; formats unresolved)

| File | Unpacked | Notes |
|---|---|---|
| introp1, introp2 | 120,000 + 67,164 | one block at $4AE90: the intro's pictures and voices ("Intro") |
| trophe | 44,688 | the trophy, 224 x 399, four planes; used by the intro and the ending |
| carte | 40,000 | the map ("carte"); layout not yet read |
| animation1/2 | 76,898 each | unpacked to $53E00 / $66A62; player frames (**hypothesis**) |
| anim0-7, dnv0-7 | 8-26 KB | loaded by animation.code |
| score, final | 53,112 / 16,384 | score screen and ending (**hypothesis** from the names) |
| music1-13, musicfinal | 4-13 KB | songs without samples ("Music") |
| instruments | 66,974 | stored as is: the sample bank of all songs ("Music") |

## Intro (observed and recovered; rebuilt in front/intro.cpp)

intro.code loads its files to fixed places ($24616): each crunched file is
read to $12A50 and unpacked from there, trophe to $40000, introp1 to $4AE90
with introp2 straight after it (one block of pictures and voices up to
$789AC), music13 to $789AC, and last `instruments` to $12A50 itself.

**Pictures.** Four planes each, one after the other, colour 0 transparent:

| Where | Size | What |
|---|---|---|
| block +0 | 304 x 83 (rows of 38 bytes) | the Ocean logo |
| $40000 (trophe) | 224 x 399 | the gold trophy |
| table at $23B5C, records of $14 bytes: x, y, picture, mask buffer ($7D000), screen, width, height | | small "ocean" 192 x 39, "Beach" 208 x 93, "Volley" 320 x 122, "PRESENT" 80 x 16, credits 128 x 149, seven comic pages, "F1 1PLAYER" and "F2 2PLAYERS" (112 x 12) |
| block $25150-$2DB1C | | six voices for channel 3 |

$23942 draws a table picture: a mask from the OR of its planes, then a
cookie-cut per plane. Clipping: nothing at y >= 255, rows cut at row 255,
rows above the screen skipped. There is no clip at the right: a picture
running past x = 320 continues at the left of the next row ("Volley" at
x 1 by one pixel, the last comic page by 12).

**Screen.** 320 x 256, four planes $2800 apart, double-buffered at $25000
and $2F000; the display starts at line $26 ($23EDE: DIWSTRT $2681). The
copper list ($23E66) sets 16 colours at the top, waits for a line ($23F2A)
and sets 16 more, waits for a second line ($23F6E) and sets 16 more: three
palettes ($23C74 fills them). The palettes are $24008 (gold and blue),
$24028 (gold and red), $24048 (gold and white) and the comic's $23FE8.

**The titles' loop** ($23186): at its top F1 or F2 (keyboard data $5F,
$5D) leave for the match; then it clears rows 0-254 of the back screen,
draws, and flips ($24068: waits until three vertical blanks have passed
since the last flip, then for line $FF). Counting steps from 1:

- from step 120: the trophy, copied (not cut out) to x 64 from row 0: first
  one row (its last), one more row and one row further up every second
  step; at 255 rows it moves up a row every step until step 772 ($23776,
  $237CE). It ends one row above the picture: plane 0 from the zeros
  before $40000, planes 1-3 from the last row of the plane before (seen
  in chip RAM; the port keeps the row).
- steps 20-410: the Ocean logo rises out of row 150 a row a step, its rows
  copied whole (7 pixels in, black around: the trophy is hidden behind
  them); "PRESENT" at (130, 160) once it is complete ($23812).
- step 412: palettes gold/red, gold/white, gold/white.
- from step 406: "ocean" at x 7 (from y -116 down to 46), "Volley" at x 1
  (from y 255 up to 94), from step 569 "Beach" at (102, 25), moving until
  step 599 ($23650).
- step 601: palettes gold/red, gold/white, gold/blue.
- steps 600-989: all three titles and the credits (from (96, 265)) move up
  a row a step; the first copper line rises to $19, the second from $FF
  (wrapping past 0); at the 220th such step all palettes are gold/blue.
- after step 1000 the comic.

On the reference a step took 3 or 4 vertical blanks (3.6 on average,
regularly 4-3-4-4-3) and up to 5 while the credits scroll: the drawing
outlasts the three-blank wait. The port keeps the program's pace of 3
(the collection's rule: no slowdown), so its titles take about a sixth
less time than on the reference (60 s instead of about 72 s).

**The comic** ($23332): clears the back screen, sets the comic palette
everywhere (lines $12 and $05), flips once and then draws straight onto the
shown screen: seven pages (at (48,0), (0,20), (160,10), (30,80), (120,20),
(80,50), (156,130)), each followed by a delay loop ($23634, 101 x 15,001
dbra) that took 146 vertical blanks on the reference; pages 3-7 start a
voice on channel 3 (DMA off, location, length, period, volume 64, DMA on,
length 1). Last a sixth voice and the two choices at y 200 (x 10 and 198).
Measured page to page: 148, 146, 148, 146, 146, 148, 146 blanks; the port
uses 147. The keys are not read during the comic.

**The menu** ($232DC): F1 or F2 is polled without end (no demo, no
time-out). The choice is stored at $84 (1 or 2); the program turns DMA off
($10F: screen and sound), copies a loader to $70000 and loads volley.code.

Port: verified frame for frame against chip RAM (`harness/frames_all.py`,
the displayed screen and copper state at every titles step 1-999, after
each comic page and at the menu, against `beach_volley_assets intro`): all
1007 equal.

## Music (recovered; rebuilt in sound/soundtracker.cpp)

The songs are in the 15-sample SoundTracker layout without samples: a
20-byte title, 15 records of 30 bytes (22-byte name such as "cu26",
length in words, volume, repeat start in bytes, repeat length in words),
the number of positions, an unused byte ($78), 128 positions, then 64-row
patterns of 4 x 4 bytes. All play the one bank `instruments`, loaded at
$12A50: the player takes sample n's start from a table of 15 addresses
after itself ($2457E in intro.code; the same values in every code file).

Every code file has its own copy of the player (intro $24114, volley
$30E6C, score $23FE6, animation $3FD26, final $4A0F0). intro.code's: called
in the vertical blank; a row every `speed` blanks (6; command F changes the
compare value in the program itself); notes on channels 0-2, the effects
between rows on all four for a non-zero parameter: 0 arpeggio (blanks 1-5:
+x, +y, the note, +x, +y via the 36-period table), 1/2 slides (to $71 and
$358), C volume, F speed (low four bits), E and the rest nothing. A note
with a sample sets the volume at once (or C's), and a repeating sample
plays only its repeat; with a period the channel's DMA goes off, its
registers are set, after all three a short wait and DMA on, and a sample
without repeat gets length 1 (it then repeats its silent first word). The
intro halves every sample's volume in its copy of music13 ("bahamas")
before playing it ($230E4).

The songs use only F (once each, on channel 0), slide down (music2) and C
(musicfinal, channel 3). The other copies differ in their channels (score
has one note channel, volley.code skips channel 0's effects, final.code
plays four) and in pause flags; read when their parts are rebuilt.

Port: `beach_volley_assets music-trace` against `harness/music_trace.py`
(the player's variables after each blank): 3,000 blanks equal.

## Attract and flow (observed in the emulator)

After F1 a venue card (London, Big Ben) appears; after fire the match
starts on the London court, two players a side, serving from the left.

## Randomness

The intro draws no random numbers. The match is not examined yet.

## Lesson candidates

- The court as one wide picture that the blitter windows and scrolls, with
  the scoreboard drawn into the picture.
- The same backward cruncher as Rock 'n Roll in another frame (trailer
  instead of header, checksum ignored).
- A copper list as a running document: the intro rewrites its palettes and
  wait lines while the picture is double-buffered, so palette changes show
  three blanks before the frame they were made for.
- Self-modifying speed: the SoundTracker player's speed is the immediate
  operand of its own compare instruction.

## Evidence

In `.analysis/beach_volley_amiga/`:

- `disk/`: the files as stored (`build/amiga_disk extract <adf> <dir>`).
- `files/`: the unpacked files (`tools/cru.py`, or
  `build/beach_volley_assets import <adf> <dir>`).
- `work/intro/`: intro pictures (`tools/intro_pics.py`), timings
  (`timing.txt` from `harness/intro_timing.py`, `comic.txt` from
  `harness/comic_timing.py`), reference frames (`ref_all.npz` from
  `harness/frames_all.py`) and music traces (`music_ref.txt`).
- `work/ref/`: harness captures (sheet1-4.png: attract, menu, venue card,
  first serve) and `game1.ram` (chip RAM at the first serve).
- Harness: `harness/run.sh` (FS-UAE, KS 1.3, A500, GDB stub on port 6872),
  `key.sh` (macOS key codes: F1 = 122, right Ctrl = fire), `dump.py`
  (memory through the stub), `shot.sh`; `clock.py` (the music player's
  position as a vertical-blank clock). The stub answers a stop with
  `$%Stop:T05…`; `dbg.py` strips the `%`.
- Disassembly: `tools/m68kdis.py` (Capstone) on the flat code files at their
  load addresses; `tools/planes.py` for bitplane layout trials.

## Prior exposure

None for this game.
