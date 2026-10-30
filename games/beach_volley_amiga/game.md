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
(musicfinal, channel 3). The other copies differ in their channels and
flags: volley.code's ($30E6C start, $30EB2 tick) and animation.code's
($3FD26, $3FD84) play notes and run effects on channels 0-2; volley.code's
skips channel 1's notes while a spike's sound plays there ($26A8A), ends
the music at a song's end instead of starting again when $3133A is set
(the jingles), and its start keeps the channels' volumes and what their
notes left; animation.code's clears the four volumes. score.code's has one
note channel and final.code's four (read when their parts are rebuilt).
Every copy keeps its own speed in its own code (command F).

Port: `beach_volley_assets music-trace` against `harness/music_trace.py`
(the player's variables after each blank): 3,000 blanks equal.

## Match (recovered from volley.code; rebuilt in match/)

**Loop and timing (observed and measured).** The main loop ($2526A) draws a
frame into the back buffer and calls the flip ($30C14), which waits for
beam line 255 and shows it: one pass every two vertical blanks (25 Hz).
A pass reads the joysticks (decoded each blank by the level-3 interrupt
into 1 up, 2 away from the net, 4 down, 8 toward the net; JOY0 is side B,
mirrored) as they stood a blank before its frame shows. The court blit at
the start of a pass outlasts a blank: the rally logic sees the chance value
before it, the drawing and the ball's flight the value after. A serve's
contact pass runs on into the rally's first pass without a flip (3 blanks).
After a point: the last frame, the court with the net and the two scores
in 8 x 8 blocks (drawn into the shown buffer), a pause of 155-157 blanks
(a CPU loop; the music's load decides), then the next serve.

**Chance (observed).** There is no random generator: each venue has a
table of values 1-5 ($2869A for London, 54 entries) the vertical blank
steps through, one per blank. Shots, B1's jump and B2's block read the
current value. The port keeps the tables (data) and the stepping.

**Objects (recovered).** 20-byte records at $30CC4 (x, y, picture, mask
buffer, screen address, width, height): A1, A2, B1, B2, ball, ball shadow,
four player shadows, net, setter mark, landing mark, cursor, clock ring,
spike mark, clock hand, ball mark, banner. Slot 1 of a side is always the
player under control: $25B72/$25C18 swap the records when the other
player is nearer the landing mark (16-bit wrapped squared distance).
Pictures are 4-plane images in the sprite block (animation1 + animation2 at
$53E00, planes $1F40 apart, rows 40 bytes). The second player of each side
is drawn with planes 0 and 1 swapped (other colours); side A's colours stay
with the person across swaps ($25C16).

**Drawing (recovered, pixel-checked).** The court (576 x 232) is blitted
to the buffer from the camera's byte ($29018) with a 0-15 pixel shift
($28B56), 32 rows above the window; the scene moves down 4 rows a pass,
up to 32, while the ball is above the screen ($2AF44). Sprites ($3030E)
are cookie-cut with a mask made from their four planes; the routine clears
the blitter's last-word mask, so each row's last word is never drawn. The
window shows buffer columns 16-303 and 200 rows. The scoreboard digits are
drawn into the court picture itself ($26210); the match clock's dots are
plotted into the sprite sheet ($31E5C): used minutes colour 9, the rest 15,
and a two-dot hand turning one place a pass.

**Rally (recovered).** The touch phase ($25B6A) steps through 3 4 3 1 2 1
($2A95A): 1/2 side A's first/second touch, 3/4 side B's. Every touch calls
the shot routine ($2950C), a decision tree over who touched (serve, bump,
set, jump, block), the joystick and the chance value; it fills the shot
(travel, pixels a pass, direction, the landing mark's move across) and the
arc's height. The landing mark ($2FC82) and the camera's easing run
($2AA96) follow from it; the ball rises and falls along the arc table
below $2AF38 while its shadow walks to the mark. The ball down at the mark
ends the rally: out beyond x 65 / 484 or y 70 / 176 by the side that
touched it, otherwise to the side whose half it fell in ($26192).
Matches go to 7 points (rally scoring); one-player matches have a clock of
eleven minutes of 1,100 blanks (at the last, side B wins).

**Players (recovered).** The four walking/stance routines are copies
($2B41C, $2B5E6, $2B786, $2B962; B1 judges the ball from 16 pixels
further right). A1's button plays ($2C632): by where the landing mark is
from his feet: dig (just behind), bump (further behind), crouch (just
ahead), low dig (further ahead), dives left and right; up and the button
at the net while attacking: the jump and spike ($2D25E); up and the button
at the net otherwise: the block hop ($2E986). The computer side uses the
same plays ($2F224) plus its setters ($2E58A, $2F01A) and B2's block
($2EE90).

**Original quirks kept.** The sound routines' delay loops leave -1 in d0:
the toss step that plays the bounce moves the ball by -1. The block test
compares a register the joystick routine left. The bounce's step is a
32/16 division of a register whose high half the flip left (overflow:
step 1). A B1 dive clears A1's play flag. The serve's B1 shadow is written
into A1's shadow record. A serve after a point first shows the older
buffer for a frame ($262B2).

**Sound (recovered; match/match_sound.cpp).** The vertical blank runs
volley.code's player while the music is on ($31B88, from each rally's
start) and then sets the three music channels (and the venue song's sample
volumes) to the music's volume ($28698: 10 as loaded, 40 after the first
match; keypad + and - change it): neither a sample's own volume nor command
C is heard. From the clock's ninth minute (dots 18) the hurry song music8
($796C4) replaces the venue's ($7BD1C: music12 London, music3 New York,
music12 Bahamas, music2 Cairo, music12 Sydney, music3 Tokyo, music2 Moscow,
music3 Paris; the names are in $3133C's loads). The effects are routines
that program a channel themselves (game.md's names): the crowd after a
point ($29292: $73B92, repeating, faded out over the pause by $26172 in 65
steps of a CPU loop, 64 down to 0), side A's serve ($292BE: the hit's
sample at a lower pitch, without a restart, so it takes over a hit still
playing), the whistle ($292FA, twice at a match's start and when a rally
ends), the higher whistle of a spike into the net ($2934A), side A's spike
on channel 1 ($29392), the hit ($293DA), the ball in the sand ($29422) and
side B's spike ($294BA: the hit's sample higher). Each plays once: the
routine sets the length to one word after the start.

**A match's start and end (recovered, measured).** $25078: the court with
the net and both scores, the venue's song started (still off), the referee
whistles twice (19 and 24 blanks after the song's start), and 55 blanks
after it the first rally starts with side A serving. A score of 7 (or the
clock's last minute, or the cheat) ends the match ($25CA2): the music's
volume steps down one every two blanks to 0, every channel stops, the court
shows with the net and the winner's picture (80 x 80, $77114 side A,
$77D94 side B; rows of 10 bytes), the winner's jingle plays once at volume
40 (the jingles are songs in the sprite block, $78A14 and $7906C), and 357
blanks later the next match follows: volley.code stays loaded, so the
next match goes on from everything the last one left (objects, counters,
the chance table's place, which moves on 5 places more before the song
starts) except what $25078 clears (scores, clock, screens). The loads
between take, by wall clock: animation.code 1.5 s (the winner's picture
still up), the card's own files 7.2-8.4 s (black), after the card the
venue's files 5.5 s and a pause of 2.3 s ($3194C, display off). A win takes side A to the next venue
($25B70 + 1) and its card; after the eighth venue final.code is loaded (the
ending). A loss starts again at the first venue, without its card. Three
places choose the screen buffer before a flip ($2868E = 0: the court at a
point, the serve after one, the end): that flip shows buffer B and draws
into A, whatever was shown (the older frame half the time).

**Cheat (observed in the code).** An 11-key sequence typed during a match
($28360, keyboard interrupt) sets $2835C; then HELP after a rally's frame
gives side A the match 7-0 with a whistle ($255A8). The port offers it as
an option, F7 standing in for HELP.

**Not yet rebuilt.** Two players, the score and ending programs.

## Venue card (recovered from animation.code; rebuilt in front/venue_card.cpp)

Before each match of a one-player tour volley.code loads animation.code
to $3BC50 and calls it with the venue in $80. It loads the map (carte, 4
planes of 400 x 200 to $402E0), the venue's picture file (animN to $49F20:
the venue's picture first, then at +$1F40 the strip's background, its
sprites and the frame's tiles), the venue's card song (music6, 7, 13, 11,
12, 10, 9, 5) and voice (dnv0, 1, 3, 2, 4, 5, 6, 7: the third and fourth
swapped in the table), about 7.4 s of loading with the screen black.

It draws straight onto one shown screen ($32000, 4 planes of 400 x 200,
rows of 50 bytes; the display fetches a word early and shows 320 from the
second word, scrolled by the fine scroll and the bitplane pointers),
waiting for line $FA between frames; the copper splits the palette at row
130 for the strip. The steps:

1. The routes flown so far are drawn into the map ($3C00C: 4 x 4 dots,
   colour 15 round, 14 inside; tables $3C6D6-$3C7CA), then the map fades
   in ($3C034: 16 steps, each copying the map through a mask that shows one
   more pixel column of every 16).
2. Sydney, Tokyo and Moscow: the screen scrolls 51 or 61 pixels left, a
   pixel a frame ($3C162). The new route is drawn a dot every 13 blanks
   ($3BFDA), and the scroll goes back ($3C1B6).
3. The venue's scene in the strip (none for London): a hand-written loop
   per venue that restores the strip's background ($3ECCC) and draws
   sprites ($3F3B4: cookie-cut, clipped to rows 130-199, not clipped in x:
   a row's overflow is the next row's start, and past the last row it is
   the next plane's first bytes) along motion tables in the code, some
   read on past their end ($3DD0A, $3E374). New York: a plane lands by the
   Statue of Liberty; Bahamas: windsurfers; Cairo: a camel by the pyramids;
   and so on.
4. A pause ($3CB72, about 158 blanks), the map fades out ($3C0CA), the
   palette goes dark blue ($3F61A), a frame of 16 x 16 tiles is drawn
   ($3ED2C: per venue a list of moves at $3EDE0) and the venue's picture,
   whose palette fades in over 16 blanks ($3C836).
5. Fire ($3BF4A, the button as it stands): the song stops, the voice plays
   once on channel 3 ($3C7DC, period $1AC), 206 blanks, the end.

The fade steps take one blank or two (their blits about fill a frame); the
port takes each venue's step times and pauses as measured
(`tools/card_timing.py`). The card's music player is its own copy (see
Music).

## Attract and flow (observed in the emulator)

After F1 the venue card for London appears; after fire the match starts on
the London court, two players a side, serving from the left. The tour
goes London, New York, Bahamas, Cairo, Sydney, Tokyo, Moscow, Paris.

## Randomness

The intro draws no random numbers. The match uses the venue's chance
table stepped by the vertical blank (see Match); the port keeps it.

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
  load addresses; `tools/rdis.py` (recursive descent from the entries:
  `work/dis/volley_code.s`), `tools/show.py` (address ranges);
  `tools/planes.py` for bitplane layout trials. Working notes:
  `work/match_notes.md`.
- Match harness: `harness/inject.py` patches volley.code's input reads
  ($BFE001, $DFF00A/C) to a mailbox at $25700 the harness writes each blank
  (the stub cannot write SR, and a PC write keeps stale prefetch);
  `harness/record.py` records every frame's state blocks
  (`work/state_blocks.txt`) from a per-blank input script; `grab.py` the
  drawn screens. Recordings: `work/rec_idle2.bin` (idle human),
  `work/rec_play1.bin` (random human input, seed 7); `screens_idle.npz`.
  Comparison: `beach_volley_assets match-trace` then `tools/cmp_match.py`
  and `tools/cmp_screens.py` (`tools/pauses.py` for the point pauses).
- Match sound: `harness/events.py` logs every effect routine, song start
  and the end's steps per blank (`work/ev_idle.txt`: an idle match through
  its end and the next match's start; `work/ev_play1.txt`). The port's log:
  `match-trace` with `BV_SOUND=<file>`; `tools/cmp_music.py` (the player's
  state per recorded frame), `tools/cmp_sound_events.py` (the calls).
- Venue cards: `harness/card.py` passes each card (fire at its wait) and
  wins each match with the original's cheat, recording every frame (screen,
  copper words, blank), the palette fade's steps and the routines' blanks:
  `work/cards/cardN.npz` and `.txt` for the eight venues. Port:
  `beach_volley_assets card-trace`; `tools/cmp_card.py`; the measured step
  times: `tools/card_timing.py` (`tools/card_table.sh` writes them into
  venue_card.cpp); `tools/card_png.py` shows a recorded frame. The tour
  as a whole: `beach_volley_assets tour` (pictures) and `tour-save` (quick
  saves at given blanks, equal for 1,500 blanks after).
- The next match: `harness/rec2.py` wins match 1 with the cheat, passes
  the New York card and records match 2 (`work/rec_m2.bin`, idle; the
  steps between with wall-clock times and the chance table's place in
  `work/rec_m2_steps.txt`). `match-trace` with `BV_AFTER_CHEAT=<place>`
  plays the first match the same way first: 740 passes equal.

## Prior exposure

None for this game.
