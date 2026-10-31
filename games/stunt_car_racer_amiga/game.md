# Stunt Car Racer (Amiga) — findings

Everything learned about the game so far. Remaining work and known bugs are in
[plan.md](plan.md). Evidence lives in `.analysis/stunt_car_racer_amiga/`
(not in Git; "Evidence" below says how to regenerate it). Labels follow
[RECONSTRUCTION.md](../../RECONSTRUCTION.md): **Observed** (bytes or
behaviour seen in this image or its execution), **Interpreted**,
**Hypothesis**.

## Editorial context

The Amiga release is dated 1989 by [Lemon Amiga](https://www.lemonamiga.com/game/stunt-car-racer).
Catalog descriptions and credits are editorial material, not reconstruction evidence.
The cover is original AI-generated interpretive artwork, not extracted game data.

## Prior work: RetroReverse

The user's own earlier project RetroReverse
(`~/Development/RetroReverse/games/stunt-car-racer-amiga/`) holds a technical
write-up of this same image. The user confirmed (2026-10-30) that it was
independent clean-room work by an earlier agent, so it may be used as a
source of leads. It was read on 2026-10-29 for orientation (disk layout,
loader, protection, and routine addresses for tracks and physics); its
named address of the track unpacker ($5AE46) led to that routine. Its claims
are still treated as hypotheses until checked against this image or the
running original: the findings below were all re-checked, and its claims
about physics, the opponent car and the horizon remain to be re-derived.
No other game-specific material was consulted.

## Supported image

| file | size | SHA-256 | MD5 |
|---|---|---|---|
| `Stunt Car Racer.adf` | 901,120 | `548fd106cd62f2d80159d48ddd5293d8b22b6b17f80c17a84a61d75f5c8a9e06` | `b6d3751e6aa636f203f3c6a8de81ebfc` |

The import (`import/disk_import.cpp`) installs two regions, each checked by
SHA-256 (full hashes in the source):

| file | sectors | bytes | SHA-256 |
|---|---|---|---|
| `loader` | 22–97 | $9800 | `a04faf46…375989c5` |
| `program` | 110–914 | 805 × 512 | `8b2e0721…653c085d` |

Release name: "1989 MicroStyle release". An unknown loader gets
`NotThisGame`; the known loader with a different program gets "damaged or
from a different release".

## Disk (Observed)

No AmigaDOS file system. The disk holds three things:

- **Boot block** (`DOS\0`): Forbid, AllocMem($9800, chip|clear), trackdisk
  CMD_READ of $9800 bytes from byte $2C00, motor off, Permit, `JMP` into the
  buffer. Text at +$83: "Protection (C) Copyright 1989 Rob Northen
  Computing. All Rights Reserved." (the Copylock vendor's tag).
- **Loader** (sectors 22–97, position-independent): TRAP #0 into
  supervisor mode, interrupts off, its own VBlank handler at $6C; copies
  itself from its offset $78 to $4000 (loop at loader $8ABC) and continues
  there; shows the loading picture; reads **$325 sectors from sector $6E to
  $E700** with its own MFM track reader (entry loader $570: d1 = first
  sector, d2 = count, a0 = destination; rejects first + count > $6E0).
  Retries through a counter at $526, then turns COLOR00 magenta and hangs.
  Sets USP $7FFFC and SSP $3D80 and jumps to $E700.
- **Program** (sectors 110–914), loaded at $E700.

All other sectors hold a disk copier's format filler ("DOS"/"DOW"/"DoW" and
a counter, or zeros); the loader's table at $96CE is the same filler.
Open: whether the program reads more from disk later (league saves?).

### Loading picture (Observed, decoded)

Loader offset $D9C: 320 × 200, four bitplanes stored word-interleaved (one
word of planes 0, 1, 2, 3 in turn; $FA0 groups); the copy loop at loader $A0
spreads them to planes $1F40 bytes apart at $78000. Palette: 16 colour words
at loader $D7C (copied at $B6). Shows the logo, an engine and the credits
"Program and Design Geoff Crammond / Additional Graphics John Cumming" with
the MicroStyle logo. Decoder: `data/assets.cpp loading_picture()`; its export
equals the research render (`work/title_loader.png`) pixel for pixel.

## Program start-up (Observed)

- $E700 sums $63BA8 bytes from $E730 as words, then **overwrites the sum with
  $5834** and branches over the failure path ($E71E): the check can never
  fail (the real sum is $862F). Interpreted: a disabled or defeated check.
- TRAP #0 into supervisor mode, SSP $EAD2, `JSR $ED56` (hardware set-up),
  `JMP $1BA08`.
- $EE64 (inside $ED56) applies **XOR $80 to $F4B8–$1AA49** (46,482 bytes).
  The result equals the running machine's memory over that whole range.
  Interpreted: not encryption but sound samples stored unsigned and turned
  into signed 8-bit (the converted bytes form smooth waveforms around 0).
  `data::Program` applies the same conversion on load.
- The display runs from a Copper list at $E74E (on the menu: four planes at
  $72294/$741D4/$76114/$78054, $1F40 apart).

## Copy protection (Observed)

- A trace-vector encrypted Copylock-style block lies around $5CE40–$5CEA8
  (a repeating encrypted pattern, then MOVEM and RTE). $5CEAA calls $5CEB4
  (waits for the blitter, deselects the drives, DMACON $0450) and continues
  at $5CEFA, which stores **d0 into $64AEC**, mixes $7A21A + $7A416 into
  $5CF2C and sets $64AF0 = $80.
- On disk $64AEC holds $2F76EA80. In the running reference (FS-UAE, this ADF)
  it holds **$9CEDCD02** after start-up; that value also lies in the program
  at $5C962. So this image runs as a genuine disk would. Explanation (Observed): the
  game's track picture screen shows "Broken by QUARTEX" beside "Hit fire to
  continue", so the user's image is a cracked copy of the MicroStyle
  release; the crack supplies the key.
- Hypothesis (prior write-up, unverified): a check compares $64AEC with
  $9CEDCD02 and weakens the handling on mismatch. The port will behave as with
  a genuine disk; protection is not game behaviour.

## Front end (Observed)

The game boots to a SELECT menu: "1. Single Player League", "2. Multiplayer",
"3. Computer Link" (`harness/screen.py`, Copper list $E74E). Texts are plain
ASCII in the program (menu texts around $5A69D, driver names from $1ECAA,
track names from $1EDAA); an earlier note here that the program holds no
readable strings was wrong.

Path to a circuit, driven with `harness/scr.py` (keys through the game's key
matrix at $EAD6, raw key code + $EAD6 = $B3 while held; the level-2 handler
at $F002 fills it):

1. SELECT: `1` then Return → "NAME?" entry → type a name, Return.
2. The divisions screen: four divisions of three drivers (portraits, names)
   and two tracks each: Division I Drawbridge / Ski Jump, II Roller Coaster /
   High Jump, III Big Ramp / Stepping Stones, IV Hump Back / Little Ramp.
3. Return → "DIVISION 4" menu: Hall of Fame, Practise, Start the Racing
   Season, Load/Save/Replay (so the program does use the disk again).
4. Practise → "Tracks in DIVISION 1–4" → "Practise The LITTLE RAMP" /
   "… HUMP BACK" / Cancel.
5. A picture of the circuit in its stadium with its name ("The LITTLE RAMP")
   and the crack's text; fire starts the practice race (car lowered by a
   crane). Escape in the race returns to the practise menu.

## Program structure: a C64 game converted to the 68000 (Interpreted)

Stunt Car Racer first appeared on the Commodore 64 (6502 processor), and the
Amiga program is not a rewrite: it is the C64 game's code converted to the
68000 almost instruction by instruction, with the C64's data kept in its
6502 form. The evidence, all from this image:

- Single-byte variables in two 256-byte pages at $1BB00–$1BCFF, used the
  way 6502 code uses its zero page (indexed by a byte, pointers kept there).
- `(pointer),Y`-style accesses: a 16-bit pointer is read from that page as
  two bytes in little-endian order (the 6502's byte order, the reverse of
  the 68000's), byte-swapped, offset into the Amiga image and then indexed.
- Data laid out for a 6502 address space: the track bank at $1EF82 is the
  C64's bank at $B100, and its internal pointers are C64 addresses
  ($B100–$BFFF), little-endian (see "Tracks").
- Byte operations dominate the code at $55000–$6A4AD (about 2,660
  `MOVE.b` against 1,870 `MOVE.w`), and 6502 rotate-through-carry idioms
  survive as `ROXR.b` right after an `ADD.b` or `LSR.b` (40 places; for
  example $5DFBE folds three bytes together, halving after each add, with `ADD.b`/`ROXR.b` twice, and the
  unpacker at $5AF7C builds a byte with `LSR.b`/`ROXR.b`). Some 16-bit
  arithmetic, though, was rewritten with word operations (`ADD.w`, `ASL.w`,
  `SWAP`), so the conversion is not purely mechanical.
- The unpacker reverses a heading with `EORI.b #$C0` on the code byte
  ($5AF04), a 6502-style bit trick on packed fields.
- Fragments of the converter's 68000 assembler source survive as filler
  between tables, with 6502-flavoured labels (for example
  "move.b d0,DV / jsr FETCH" around $1EE60).

What this means for the port (interpretation; the C64 program has not been
examined): the game's rules are the C64 game's rules, so the shapes, profiles, physics constants and tables found here are the
original design's own data, not Amiga reworkings. Amiga-only parts sit
around that core (filled-polygon drawing, sound samples, the disk loader,
Copylock). Ghidra decompiles the converted code poorly (byte-wide variables and
carries), so key routines are read as assembly. The port keeps none of
this layout: it decodes the data into its own types (hypothesis to watch:
the same conversion pattern may also explain some odd fixed-point widths in
the physics).

Code lies at $E700–$F4B7, around $1BA08 and in $54000–$6A4AD; the rest is
data (samples, then pictures from about $20000).

## Tracks (Observed, verified)

### The track bank

At program $1EF82 (bank address $B100; bank pointers are little-endian
words counting from $B100):

| bank offset | entries | what |
|---|---|---|
| $000 | 16 | shapes (segment plan shapes); slots 2, 5, 8, 9, 11–15 point outside the bank and are unused |
| $020 | 128 | height profiles; a few slots (81, 120, 121, 123) point outside the bank |
| $120 | 8 | track definitions, in name order: Little Ramp, Stepping Stones, Hump Back, Big Ramp, Ski Jump, Draw Bridge, High Jump, Roller Coaster |

Names: 16 space-padded characters each from $1EDAA, same order.

**Shape**: byte 0 = offset of its description; the description holds the
point count n, five bytes used by the shape code elsewhere ($5FF5E–$5FF92:
a flag in bit 1 of the first, then bytes kept at $1BB7B, $1BBD9, $1BBD4),
then n points (x, z signed little-endian words), left and right road edge in
turn: n/2 cross-sections, n/2 − 1 steps. Frame: a cell is $800 units square,
the shape heads +z. The straight (0) has edges at x $340 and $4C0 and steps
of $100 in z (8 steps); 1 and 3 are eighth-turn curves from an axis to a diagonal, 6 and 7
quarter-turn curves (9 steps; Stepping Stones uses only 0, 6 and 7), 4 and
10 diagonal straights (13 and 11 steps).

**Profile**: one road edge's height offset per step. Narrow form (one byte:
low 4 bits whole units of $100, bits 4–6 eighths: `(b & $F) << 8 | (b << 1
& $E0)`), wide form (two bytes, high byte masked $7F) chosen per segment by
bit 7 of its left profile number. A profile's bytes run to the next thing the
bank points at. Bit 7 of a step's (first) byte is not height but a mark; on
the left profile the race view splits the road surface there (see "Race
view").

### Track definition (unpacker $5AE46, helpers $5AE00 byte read, $5AE0C cell step, $5FE56 segment set-up, $5B2D2/$5B2C8 profile read)

- 4 bytes: segment count, then three bytes kept at $1CA1B–$1CA1D: the
  segment the car is lowered onto (copied to $1BB1D at the race start,
  $5D3C6; observed: the car came down in that segment's cell on Little Ramp
  and Hump Back), the start/finish line's segment (the race view compares
  segments with $1CA1C; the line is drawn across that segment's last
  cross-section, observed on Little Ramp), and the halfway segment a lap
  must pass before the line counts again ($5DCC8, verified on Little
  Ramp). The first two are equal on every track; the second + 1 is stored
  at $1CA32 (where a lap counts).
- 2 bytes: start height (little-endian) for both road edges.
- Per segment: a code byte. Low nibble $F: repeat the previous code
  (high nibble) times, each in the next cell along its heading (a reversed
  code steps along heading XOR 3, as the unpacker flips both heading bits).
  Otherwise the code is followed by the cell byte (high nibble z, low x).
  Code bits: 7–6 heading (quarter turns: 0 +z, 1 +x, 2 −z, 3 −x), 5 both
  edges share one profile number, 4 shape driven from its far end (edges
  swap), 3–0 shape. Shapes $C and $D are straights whose profile pair comes
  from the program ($5B2B8 + kind / $5B2BA + kind: 3/4 and 4/3). Then one or
  two profile numbers (left, right).
- Heights: each edge carries on from where the previous segment left it:
  base = height − profile(0), next height = base + profile(steps). A kerb
  colour phase flips after every segment with an odd number of steps.
- After the segments: 4 bytes ($1CA2A–$1CA2D: the damage window for the
  standard and the super league's car, then their boosts; Little Ramp 6,
  5, 34, 47), a count of pairs
  (segment, value → $1C8A8/$1C8C8) and a count of single segment numbers
  (→ $1C8E8), then the routine at $63A8E and a pass that fills $1C952 per
  segment. Not yet interpreted.

Placement: a segment's shape point (x, z) in cell (cx, cz) lies at
heading 0 (cx·$800 + x, cz·$800 + z), 1 (cx·$800 + z, cz·$800 + $800 − x),
2 ($800 − x, $800 − z), 3 ($800 − z, x) (from the transform at $5C6C4).
All eight are closed loops on the 16 × 16 grid (Ski Jump closes to within one
unit on a diagonal).

**Verification**: the decoder (prototype `tools/tracks.py`, port
`data/track.cpp`) reproduces the original's per-segment tables (code $1C5EC,
cell $1C588, profiles $1C4C0/$1C524 with the kerb bit, edge bases
$1C650/$1C718) exactly for Little Ramp (44 segments) and Hump Back (53),
read from RAM on their track pictures; each of the seven measurable blocks
is consumed to its last byte; the port's export (`tracks.txt`) equals the
prototype for all 445 segments.

**Height scale** (Observed, verified by the race view below): the view code
takes a point's horizontal angle with an atan2 ($64D66) and its distance
with a hypot approximation ($64DE8); the vertical angle ($624C2) uses
(height difference ÷ 4, truncated) against that distance, so heights are a
quarter of plan units in the race view. The track-picture mode halves x/z and scales heights by about
0.59 (factor $4C1B), about 0.3 overall; measured from the original's Hump
Back picture ≈ 0.37. The preview models use ¼. Plan handedness: seen from
+z (z towards the onlooker) the decoded circuits match the original's
pictures (Hump Back's diagonal at the far left, hump straight in front).

**Jumps and the drawbridge**: steep one-step height changes at segment ends
are the gaps (Ski Jump segment 3, Draw Bridge 4 and 38, High Jump 41/43/45).
Draw Bridge's segments 51–55 use profiles 95–98, which rise to $3840 in the
middle step; the routine at $5A794 (runs only on track 5) rewrites profile
95's bytes every frame to animate the bridge. The previews show the stored
state.
## Race view (Observed, verified)

The view through the windscreen is 256 × 128 pixels at screen (32, 16),
drawn by the CPU into four bitplanes ($1F40 bytes apart, 40 bytes a row) by
a span filler ($68544 → $686E0); the colour of a polygon is the low nibble
of $69AEC, one bit a plane (solid colours only). The frame routine is
$64E4C (called from the race loop at $5D496): camera ($60190), the car's
segment and the next two in full ($6521C, $655EA, $65A18, $6702E/$67092),
the far segments ($65EC4), the horizon and hills ($6953E), then the
polygons and lines from a row table at $7B08A ($68F28 and helpers).

**Colours**: the race palette is 16 words at $20184 with three bits a gun;
the game widens each gun c to 2c + 1 (0 stays 0) when it loads them
($1BA82). Sky 7, ground 13, hills 5, road 1/2, sides 15/10, edge lines
3/9/10/11, start line 15, jump face 0.

**Camera** ($60190): car position longs $1BCD8 (x), $1BCE0 (z), $1BCDC (y);
plan = x >> 12, height = y >> 11. Eye height $1BBFA = (y >> 11) + (($1BD38 +
$780 [or 2·$1BD38 + $280 when $1BD38 ≥ $500] − car pitch / 2 when nose up)
>> 4), about 100 units above the road for a car at rest. Yaw $1BCE6 (0 = +z,
$4000 = +x), car pitch $1BCE4 (the view uses $1BC42 = −pitch, $61ACC), roll
$1BCE8 with sine/cosine kept at $1C250/$1C252 ($61368). To keep angles
small the code turns the world in quarter turns ($1BC30) so the view's yaw
stays within ±45°; the port does not need this.

**Projection** ($62454, $624C2, $62518): by angles, not by division. For a
point, across = atan2(dx, dz) − yaw and down = atan2((eye − h) ÷ 4, √(dx² +
dz²)) − view pitch, both in 65536ths of a turn and shifted right 3; then
turned by the roll: x = (cos·across + sin·down) ÷ 4 + 128, y = (cos·down −
sin·across) ÷ 4 + 64. So 2048 pixels a turn: the view is 45° wide, 22.5°
high. Straight edges between projected corners would cut a near road short
(its near corners lie far to the sides at large bearings); the game pushes
points beyond |x − 128| > 160 down by (|x − 128| − 160) ÷ 8 ($617D0) and
extends off-screen edges to the border ($65B80). The port instead cuts long
edges into short pieces before projecting, which gives the true picture.

**What is drawn, in what order**: sky above the horizon, ground below
(turned by the roll); the hills; then the track. The track is walked from
the car's segment onwards (the one behind too) until a segment ends off the
side of the view (the last cross-section's x outside 0–255; $660B0), and
painted from the far end back to the car, each step's road piece and
then its sides over it. Sides (each road edge down to height $200, the
ground level) are drawn only when seen from outside. (Drawing the sides
first left stair steps where a near wall rises above the road, as on
Little Ramp's start under the crane: 96.9 % equal pixels before, 98.0 %
after; the other compared frames are unchanged. The original also draws
thin dark lines between wall pieces and changes the wall colour at other
places there; not yet looked at.)
Segments beyond the first three come from a thinned copy built at track
load ($65BEC into $7ABDA, pointers per segment at $7AA1A): only
cross-sections where the left height bends by $A0 or more from its
neighbours' mean, and each segment's last. The port draws every
cross-section (the difference is within a pixel at that distance;
Hypothesis to check on the tall tracks).

**Road pieces** (loader $65756, colours $68D82): the surface is filled in
pieces between boundaries. A cross-section is a boundary where segments
meet, and inside a segment where the left profile's step has bit 7 set; a
segment's last step with bit 7 set is *not* a boundary (so a curve's colour
can change one step before the segment ends, as the original shows). A
piece takes the colour of the segment at its far boundary: 1 for even, 2
for odd segments. When the left edge rises or falls by $280 or more at the
boundary, the piece is a jump face, colour 0.

**Sides** ($68A..., $68BEC): 15 for even segments, 10 for odd ($691B2);
colour 9 ($691B3) for all sides while $1BB66 is set (not yet understood).

**Edge lines** ($6893A..., colour per row at $673EE): one-pixel lines along
both road edges for the nearest steps: from the row counter $1BB58 ($20
minus the high byte of $1BB10 at the car's segment, then one per row;
Interpreted: $1BB10 is the car's position along its segment in steps) red 9 below $26, 10 below
$2A, 11 below $2C, none beyond; yellow 3 instead on alternate steps (phase
from the segment's kerb bit). The line routine starts one row above the
edge's first row ($69342), so the lines sit a row higher than the outline.

**Start line**: a white (15) line across the start/finish segment's last
cross-section (row flag 1 from $657BC).

**Hills** ($6953E; data at $69A80/$699B8): the first scenery set (the game
always uses set 0): 32 placements of 14 shapes, each a four-cornered hill in
colour 5. A shape's corners are offsets in eighths of the angle unit from
its bearing (256ths of a turn) and above the horizon; a negative coordinate
word stands for the next value of the shape's own value list. Then edges
(point pairs) and polygons (colour, 3 or 4 edges). Drawn when the bearing
lies from $1C left to $10 right of the view's heading (high byte of yaw).

**Facing backwards**: the game walks the track backwards when the car
faces against it ($1BC32 set by the physics). Not yet observed: a pose with
only the yaw turned (the flag unchanged) draws garbage in the original. The
port picks the direction from the camera and the road at the car.

**Verification** (`harness/pose.py`, `harness/view.py`): the original was
stopped before a frame's drawing, given a pose, and its drawn buffer
compared pixel for pixel with the port's view from the same camera. Little
Ramp: 8 frames (start with the start line, straight, the ramp, the jump,
the banked hairpin, a diagonal, pitch + roll, off the road) 98.4–99.8 %
equal; Hump Back: 7 frames (humps, banked turns, a tilted landing, sides
in view) 95.2–98.8 % (the tilted one lowest). The rest are edge pixels: the
original's integer atan2/hypot tables and truncations against the port's
real arithmetic, about a pixel along edges and lines. Known differences:
a near side seen at a glancing angle shows thin stripes in the original
(Hump Back, h4), and the thinned far segments.

## Race screen (Observed, verified)

The whole race screen is 320 × 200 (display window $3C81–$04C1), double
buffered ($6A584 and +$7D00; the frame is finished at the swap, $5DAC0).

**Cockpit picture**: packed at $201A4 (PackBits, unpacked by $593F8: 200
rows, each the four planes' 40 bytes in turn), race colours at $20184.

**Overlay items**: 52 items, records of 16 bytes at $6A16C (source column
and row, columns − 1, rows − 1, screen column and row). They are cut at
start ($69CFC) from the sprite sheet at $6A5B6 (a 320 × 200 picture with
the planes interleaved a word at a time, plain in the program; colour 1 is
see-through), except $0A–$0E, cut from the cockpit picture itself.
$69F42 draws an item at its record's place (masked, CPU), $6A0BE at any
pixel, clipped to the view. Items: $00–$05 front wheels (right and left,
three steering frames), $06–$09/$31/$32 boost flames, $0A engine, $0B/$0C
pieces under the view's lower corners, $0D/$0E the window's top corners,
$0F/$10 and $11/$12 the dashboard lamps (lit/dim), $13–$16 crane chain
links, $17–$1C roll-bar marks (hole, cracking, whole; +1 for odd
positions), $1D–$24 smoke puffs, $25–$30 tyre treads (hardware sprites:
plane A = sheet plane 0 inverted, plane B = sheet plane 2; colours 17–19
black, white, $C88 from $69EB4), $33 the message box.

**Each frame, after the view** ($650E2 on): crane chains ($662B4), the
window corners, the front wheels (frame $1BBDB on the right, 5 − that on
the left, top row from the suspension via $5E778, cut off at row $9F), the
engine, the corner pieces, boost flames while $1BB62 (frame $1BB69, 0–2),
the messages ($5E508). The tyre-tread sprites follow the wheels (item $25 +
frame, + 6 when the wheel is above row $7E; $651B8, moved in the vertical
blank).

**Crane**: while the car hangs ($1BBDF counting down), the chains hang from
the top of the view: links every 8 rows from (bottom − $30) up to row 16,
bottom $1BBEA = $B0. Once the car is down they retract in five frames
(bottom less 8, 16, 24, 32; gone at $60).

**Dashboard** (drawn when it changes, into both buffers): the speed bar
(speed $1BD30 → (speed − $1100) × $B700 >> 23, 0–127, rows 174–175 from x
96, colour 3, dark 0; $5E23E); the ten roll-bar marks ($60988: the first
$1C9CF whole, the rest holes); the chequered-flag lamp ($5E0C4, lit from a
comparison with the opponent's laps and place, $607D0) and the right lamp
($5E098, lit while $1BBB2 is 0); texts in the dashboard font (8 bytes a
character from ' ' at $1FE82, 7 pixels wide, columns of 7 and rows of 8
plus pixel offsets, each character filling its cell with colour 14 and its
glyph 0; $594D6): "L" and the lap ($1BB20) at (37/45, 178), "B" and in
league races ($1CA22 bit 7) the boost left ($1CA20, BCD) at (60, 178) and
(68/76, 178), the distance to the opponent ($1BC38 × 5/16, minus while
$1BBB8 bit 7) at (44…76, 188); lap times ($5E326: M:SS. and hundredths,
BCD in $1C908/$1C920/$1C938) on row 178 (current lap, slot 0) and 188
(slot 2), ':' and '.' drawn without filling their cells.

**Messages** ($5E508, table at $5E65C): groups of a control byte ('!'
starts the second line) and three characters ('<' a 4-pixel gap), named by
their start and group count: WRECK, RACE WON, RACE LOST, DROP / START,
PRESS FIRE, PAUSED, LAPS OVER, DEFINE KEYS, STEER LEFT/RIGHT, AHEAD/BACK +
BOOST, VERIFY KEYS, FAULT FOUND. Printed from column 19 of row 4 (y 32;
y 37 for a one-line two-group message), second line y 42, glyphs only,
black (white while $1BB88); shown while $1BB8E bit 7. PRESS FIRE comes
with the message box (Interpreted: its caller was not found).

**Verification** (`harness/full.py`, `harness/screencheck.py`): whole
frames captured at the swap with the cockpit state read from RAM, compared
pixel for pixel with the port: practice (99.99 % outside the view), the
crane (chains, swinging car: 99.88 %), boost flames, the speed bar (44
pixels), messages, the end of a league race with PRESS FIRE (99.96 %).
The tyre treads are sprites and not in the captured buffer. Known
differences: the timer's '.' (3 pixels).

## Opponent car (Observed, verified)

Drawn by $67AA6 while 10 ≤ $1BC38 < $C80 and $1BBB8 bit 7 is clear, at
its place in the painter's order: after the road piece just nearer than
its rear axle ($1BC12, set while the rows are built, $66FF0; one more row
when $63EDE ≥ $FF00, not understood).

**Placement** ($5A356–$5A51C): its segment ($1BB1D), its rear axle's step
($1BB13 fraction; Interpreted from $1BB0C − 64 over 256 in captures), the
front axle half a step on; across the road $1BBEC (0 at the left edge, 256
at the right), the wheels ± 27 ($1BB43). Road points and heights are
interpolated linearly between the cross-sections and edges ($5C554).
Corner body heights $1BF64/$1BF68/$1BF66/$1BF6A (rear left, rear right,
front left, front right; about 15 above the road at rest); the shadow's
heights are the lower of those and the road + $50.

**Construction** ($599E2–$5A182, in screen space): the four corners are
projected (at body and shadow height). For each axle, U is half the
projected axle vector (each part limited to 255), V = (U.y, −U.x) / 2, the
centre the axle's middle; U/2, U/4, U/8, V/2 truncated toward zero. Rear
cross-section: −U/2 − V/2, V − 3U/8, V + 3U/8, U/2 − V/2; front: −U/2 −
V/2, −U/2, U/2, U/2 − V/2; wheels from the axle's ends (−U to −U/2, U − U/2
to U), V deep; the shadow's rear corners lowered by V − 1, its front by V;
several corners one pixel larger (the game's ADDQ #1s). Faces in order:
shadow (5, while across is $1C–$E4), front wheels, both sides (12), back
(10), top (15), underside (9), rear wheels (0). The filler ($66618) follows
each outline both ways from its top and fills a row only where the left
end lies left of the right: faces turned away vanish, faces seen nearly
edge-on leave slivers.

**Verification**: the construction reproduces every vertex the game
computed in a captured frame (RAM $1BFB0/$1C0F0 slots $100–$11E); posed
behind the opponent (harness/behind.py) on Little Ramp and Hump Back the
car matches to a pixel or two (the corners' projection).

## Car physics and the race loop (Observed, verified)

Prior work: RetroReverse's write-up (Part V) and its Go version of the
physics were used as a map (the user's earlier clean-room work); every
routine here was read in this program and the port is checked frame by
frame against the running original. The Go version's names for $1BCE4 and
$1BCE8 are swapped: the suspension tilts the rear point against the front
pair by $1BCE4 (pitch) and left against right by $1BCE8 (roll).

**Race loop** ($5D47E–$5D608): controls ($5D8A2: steering ±15, fire,
drive force from the per-car acceleration with a latch, reverse $FF10,
boost doubling it while fire is held and boost is left, $608A4), physics
($6185C), engine pitch ($5B56C), the frame routine ($64E4C: camera and
placement, then drawing; in league races the opponent too), the
drawbridge, dashboard updates, the clock and lap timer ($5DB34), the swap.
**Time base**: the lap timer adds $13 (0.13 s, BCD) every frame ($5DF2E),
so a frame is 0.13 s of race time; the original reaches about 7.7 frames
a second on an A500 (measured: frame counter $1BBC9 against the 50 Hz
vertical-blank counter $EA8A). The port steps at 0.13 s.

**Physics frame** ($6185C): orientation matrix from yaw (and yaw against
the piece's heading), pitch and roll (sine via a quarter-wave table at
$1CA42 with interpolation); the three suspension points' places; the road
under them; their heights on the tilted body; velocity into the body frame
(transform recipe at $1EC46); speed and engine note; gravity ($13D) into
the body frame; springs (compression = road − body − rest, limited to
−$300..$1400; force = travel + Δtravel × $114/256, 0..$11FF; hard landing
when a force jumps to $400 from below $200; damage while (force −
tolerance<<8) ≥ $700, for the first `window` frames of an impact, severity
1.5 × (excess − $600) >> 8); net lift, pitch and roll lift (3 × left −
right, ±$1000); airborne nose-up nudge on tracks 4 and 7; road slope from
the springs (half-angle table $1EECA) and the net lift along the body
axes; the spawn servo (below); the other car's knock; a landing thud
(sound 3, at most every fifth frame); then, while armed, drive limited by
grip (2 × up-load, none in the air), sideways grip with sliding, track
following ($61012: heading error against the piece, bias table $6125A,
steering torque from the joystick scaled by speed, a turn towards the road
when not steering; the disk-protection check there passes on the genuine
disk), forces to the world, drag ($6000 firm while gripping, else
speed-proportional above a $A00 dead zone), torques; then velocity and
position (×238/256 at both stages, positions << 6/7/6, height limit $3E8)
and angles with their limits ($61AD4).

**Road contact** ($5C1D0): per point, its offset across the piece (the
across gain from the shape's description) and along it (the along gain),
crossing into the next or previous piece at the strip's ends; the four
surrounding edge heights (profile + base, >> 5) interpolated bilinearly;
past the road's edge ($180) the surface tapers for $30, then falls away
($1000) and the point is marked off track. Slow and level, the sample is
averaged with the last (a low-pass). A lateral fraction of 1 reads the
byte at address $FF (0): the game means #$FF.

**Placement** ($64E4C: $60190, $5FE04/$6001C, $5BE44, $60246, $5C138): the
world is turned in quarter turns so the heading stays within 45° of +z;
the camera's height (net lift and pitch added); the segment from the
16 × 16 cell grid (or across the nearest cell edge); the car's place on
the piece by its kind: straight (offset from the shape's centre line,
progress = z in the cell), diagonal (rotated by $B5/256), arc (atan2/hypot
around the curve's centre from the shape's figures, progress = the swept
angle times the arc gain, the radius refined towards the true distance;
past a curve's end onto a type-4 piece it places on that one); over the
edge: $C0 from the centre line is the side, $100 a crash; progress in
driving direction (reversed pieces count back).

**Start** ($605B6, $5D726): the car's figures from a table of two cars at
$1FE6C (acceleration, boost reload, damage tolerance; the super league's
car is the second); the race's damage window is trailer byte 0 (byte 1 in
the super league); back up to a piece the car may start on (not curves:
table $1F0C2; not the track's listed segments); cell centre, pointing
along the piece (diagonals 45° more); hanging high; one placement and
physics frame; the crane's target height from the rear road height; the
car set $A0/256 along its heading. Recovery clock from $F0: level the
roll (towards ±$10), lower the car to its target height, then $8C frames
of servo and the hand-over (DROP / START).

**Verification** (`harness/physcap.py`, `tools/stunt_car_racer_physcheck`):
captured frames of the original (crane, drop, idle, throttle, steering on
Little Ramp): the physics frame from each captured state matches all 319
frames in every field; the placement all 280. The whole race loop is
checked by `harness/racecap.py` (below).

## The race: crashes, the crane, laps, damage (Observed, verified)

**Frames of the race loop**: a *spawn* frame ($5D402: the spawn $605B6,
the wheels, the smoke cleared, the frame routine, the clock, the frame
down-counter $1BBAC, the buffer swap), a *settling* frame (physics, frame
routine, clock, swap; then the drive forces are armed, a short delay
loop $5BA42, the palette fade $64470 and the engine sound), then
*driving* frames ($5D47E: joystick, physics, engine sound, frame routine,
drawbridge, speed bar, damage $5DFB4, clock $5DB34, distance $5E12A, the
end-of-race test, swap, pause $5D9DC, crash test). One clock tick and one
physics step per frame throughout (checked on the original: frame counter
$1BBC9 against breakpoints on $6185C and $5DB34).

**Over the edge** ($60246, main loop $5D5AC–$5D608): $100 from the centre
line sets the crash state $1BB9C = $80, the side ($1BBE1, the spawn's
direction), a wait of $10 frames ($1BB41) and clears the smoke. While the
car is crashed, on the ground and not held by the crane, each frame after
the swap: a car lying low (height byte $1BCD0 below 2) takes the narrow
angle limits ($1BB75 = $1BB9C), and the wait counts down; one frame after
it reaches 0 (and with no race-end countdown running) the loop jumps to
the spawn frame with the segment where the car was last on the road
($1BB9B, kept by the frame routine while the car is on the grid). The
spawn keeps everything outside $1BCCC–$1BD5E (springs' road contact and
damage, flags, clocks); a car that has been launched before ($1BBC4) is
put just above the road (rear surface << 9 + $180000) with the recovery
clock at $E6: the crane levels it, lowers it and then waits for fire
(PRESS FIRE) instead of DROP / START. In league races the wait after
lowering is $A0 + a random 0–$1F frames instead of $8C. Off the grid
($64E8E) the crash state is $C0 and placement stops.

**Clock and laps** ($5DB34; read from the code, the clock verified, line
crossings and the finish so far only seen in the port): the frame counter, the slow clock ($EE
added per frame, $1BBCD = 0 on carries), the lap-time hold ($1BBCC), the
player's lap time (+$13 hundredths, BCD; seconds wrap at 60, minutes stop
at 9; slots: player 0, opponent 1, best 2 at $1C908 minutes, $1C920
seconds, $1C938 hundredths; all start at 9 minutes), the lap check
($5DCC8), the finish ($5DE1C) and the end countdown ($5DBD6, twice per
frame while the car lies below the road). A car's lap counts when it
reaches the segment after the start line ($1CA32 = start line + 1) with
its halfway flag clear ($1BB1E + car; set at the line, cleared at the
track header's third byte, the halfway segment $1CA1D). The first crossing
starts lap 1 and resets the time. Later crossings add the part of a frame
the car needed to reach the line (($FF − progress low byte) × $D >> 8
hundredths, when on the piece's first step, $6181E), hold the finished
time on the dashboard with hundredths for $1B frames, then show the best
lap (lower line, $5DCA6; the lamp right of the wheel lit when the best
is the player's); an opponent's best lap shows at once. The race is over
after four crossings ($1BB99 = 4, three laps): the end countdown $1BB6C =
$2C slow-clock frames starts (only while the car is down, held, or the
countdown is ≥ 6), the chequered-flag lamp lights when the player is
ahead (laps, then segments past the line, $607D0) and the message is
RACE WON / RACE LOST in league races, LAPS OVER in practice. When the
countdown runs out ($1BB6F), on the frame drawn into the second buffer
with no lap time on hold and the car down or held: PRESS FIRE and the
loop ends. Messages flash white with bit 2 of the countdown; while it is
1–2 or done only PRESS FIRE shows ($5E508).

**Damage** ($5DFB4, $66106, $5E848): a frame with spring damage sets the
crack's target to the springs' average damage ((front left + front
right) / 2 + rear) / 2, $1BBB6) and grows the crack ($1BB55) towards it
a pixel at a time along the roof (screen column $28 + length, rows 2–5
($662AA, starting at 4), wandering up or down at random on every second
pixel: two dark pixels and a highlight above in colour $B, $C where it
rose). Where the cockpit picture's roof has no paint the crack is not
drawn and the target grows instead, all three springs' damage with it;
a crack of $F0, or a target that wraps, wrecks the car: rest length byte
$1BCA2 = 2 (the car sags, no drive), the wheels' rest row $1BBDD = $92,
the end countdown $3C, message WRECK(ED). An impact of $1400 or more
($1BC3A) knocks a hole in the roll bar's marks ($1C9CF, 10 at the start
of a practice), shown as a crack for one frame, and no other hole for
$45 frames ($1BB73). Otherwise the knock only sounds (volume from the
impact). The practice restores the marks afterwards ($5D70A).

**Boost**: the track trailer's bytes 2 and 3 are the boosts allowed
(standard and super league car), BCD at $1CA20 with the allowance at
$1BB95. While boosting, each wrap of the boost timer ($1BB3D, reloaded
from the car table) spends one ($60824: + $99 BCD, capped).

**Random numbers** ($62574): five bytes $625A2–$625A6, each draw shifts
them along and appends (word $625A4 >> 4) xor (word $625A2 >> 1). The
race set-up stores $3B at $625A5. Draws in a practice: once per driving
frame by the joystick routine ($60BAE) and once by the engine sound
($5B56C, also after the settling frame), the recovery's lowering
($5B3B0), the dust and smoke, the crack. (The opponent draws too.) The
port keeps this generator: it is plain software and the crack, smoke
and league waits follow from it.

**Dust and smoke** ($60CF8, $60C92, $60E88): 32 particles (x $1C380, y
$1C3C0, vx $1C400, vy $1C440, view pixels). While the car scrapes the
road's edge ($1BBDA) or is wrecked, moving and not crashed: each particle
is drawn (colour 3 at (x, y), (x + 1, y), (x, y − 1), white at (x + 1,
y − 1)) and moved (vy + 2); fallen ones (y ≥ $80) are thrown up again
from x $40–$BF, y $77–$7E, vy = −(strength / 4 + random 0–7) − 1, vx =
(x − $80) / 8, strength = speed byte capped at $32. Crashed and not yet
held: 16 puffs instead (items $1D + size, size from a table by particle +
frame down-counter, drawn at x − offset + $20, y + $10, clipped to the
view), from anywhere across the view, rising faster (strength capped at
$10, / 2). Off the ground everything is cleared ($60CDE: y = $D4).

**Verification** (`harness/racecap.py`, `stunt_car_racer_physcheck ...
race`): records at every frame start (spawn, settling, driving) with the
state window, the random generator, the crack and the message, driving
with given inputs. `work/race_lr1.bin` (470 frames: drop, idle, throttle,
off the edge of the first curve, the wait, the crane, PRESS FIRE, fire,
on round the reversed banked curve): the race run on its own from the
first driving frame matches all 467 frames in every field (car and race
state). The capture loses the first driving frame (its breakpoint stop
races the interrupt after the long delay loop); the checker starts after
it. Found on the way: on reversed pieces the road sampler uses the
lateral fraction as it is and only the rear wheel's copy ($1BBA1) is
flipped; the boost spend. Not yet captured: a finished lap, the finish,
wrecking. `harness/racesample.py` (an input feeder in free RAM and stops
only every few seconds, for whole races; checked by `physcheck ...
samples`) does not work yet: the original hung in the ROM after about
160 frames, and the start breakpoint was not always reported.

## The opponent's driving (Observed, verified)

League races only ($1CA22 bit 7). The opponent is not a second physics
car: it rides along the track, and only three wheels bounce on a simple
suspension. Its routines run in the frame routine after the player's
placement ($64F68: $63996 ride, $6076C gap, $636C0 steering, $5A186
place; off the grid only the ride and the place). Driver number $1CA29
(Jumpin' Jack = 9 in Division 4), habits byte $1ED8A + number (Jumpin'
Jack: $10, none of the bits below).

**Track load** ($5B1B8, $63A8E, $5B1E0): 256 draws of the generator, then
the skill: top speed $63CE0 = (draw & mask) + base and curve speed $63CE1
the same, from the table at $1FE2C by track number (+$20 super league:
masks at +0 and +$10, bases at +8 and +$18). Then each segment's target
speed $1C952, from the last segment back: a segment named in the track's
(segment, value) pairs takes the value (bit 7 set: it and the next three
found are marked); curves (start table $1F0C2 bit 7) take the curve
speed and restart the run $A below it; other segments $A more than the
one after them while that stays below $80 (from $7C). Bit 7 of a segment's
target: no bumps there and a doubled push. The distance table $1C7E0 is
32 × the steps of all segments before each; the track length $1CA1E.

**Start** ($5D3C6, $63A58): segment $1BB1D = the car's start segment,
progress $1BB0C = $0400 (step 4), lane $1BBED = $4C, placed, body heights
$1BD66.. = road + $68 + a random 0–$7F.

**Ride** ($63996; only once the player's car has been dropped, $1BBC4):
the suspension ($63EE2: per wheel, compression = road − body + rest ($28,
$7C on curves), no lower than −$60, spring = compression + Δ × $114/256,
0..$3FF; on the ground $1BB44 while any spring is loaded; forces (sum +
5 × own) / 8; drivers with habit bit 2 hop now and then (front settled,
1 in 16 draws: wheel speed $A0); wheel speeds and heights integrate with
the 238/256 damping; the rear wheels at most $128 apart, rear and front
$170, the pair's speeds averaged when held; airborne, it noses down; the
drawn corners $1BF64.. = body + $50, the front pair split by half the
rear's tilt), bumps ($5BD1E: on a rough segment, at random against the
driver's number, it bucks for 16 slow-clock frames: rear wheels and lane
wobble from the table at $5BE34), the push ($63C04: the car table's bytes
4/5, less $19 while bucking, none in the air), the target ($63C3C: the
segment's, capped by the top speed unless marked; over it, the push turns
to braking; under it after braking, or on a marked segment, double
push), the speed ($63B08: + push − drag (speed byte × speed>>7, $14 less
in the slipstream: the player's $1BBC7 and $1BBB8 both set) >> 6; on the
ground the push also less the speed byte (twice on curves, and $23); plus
the slope from the road under the rear and front wheels; × 238/256; not
below 0); then progress += speed × the piece's along gain (× 238/256, in
1/256 steps with the fraction's carry $1BB3C), wrapping into the next
segment after the piece's steps.

**Gap** ($6076C): (opponent progress − player progress) / 8 + the two
segments' distance-table difference = $1BC3E; the shorter way round
$1BC38, and $1BBB8 bit 7 set when the opponent is behind that way.

**Steering** ($636C0): the player's drag flag $1BBC7 is cleared, then set
when within $40 and behind it or in its lane ($32); its lane against the
player's ($1BBA1): within $10 and $32 across, with the player on the road,
they touch ($63CE4); within $18 it dodges to $20 or $E0 away from the
player (or, habit bit 3, keeps its lane from $E on when ahead); when
behind, it passes on the far side; within $32 it follows the player's
lane (habit bit 1) or passes; within $C8 with habit bit 5 it passes; else
its own lane ($40, $6E with bit 3; inverted for odd drivers). On a curve
or before one it takes the middle ($80). The lane moves 9 a frame towards
the target (or with the bump wobble), within $20..$E0, only on the ground.

**Touching** ($63CE4): if either car is in the air and they are within
$C0 in height, a vertical knock (($100 − apart) << 4) for up to three
frames; side by side (lane gap < $2D, distance < 8) a sideways knock ±$800;
once per touch, a knock along by half the speed difference ± 3; the
player's springs take (($200 + the knocks) >> 8) damage. The player's
physics applies the knock ($63E2E) and the opponent takes its share: its
speed less the knock along, its wheels' speeds less the vertical / 16.

**Place** ($5A186): its rear axle $40 behind its progress (into the
previous segment if need be); the road under the rear wheels either side
of its lane (± the half track from $1EF4A by its body roll / 16) and
under the front half a step on, interpolated as the player's road
($5C554), / 8. The dashboard distance ($5E12A, every fourth frame):
(distance + distance/4) / 4, minus when it is behind.

**Laps**: as the player's, with its own lap time (slot 1), its progress
for the crossing fraction, and its best laps count for the best lap.

**Verification**: `work/league_lr1.bin` (600 frames of race 3 against
Jumpin' Jack: the drop, the opponent driving away, a lap of its own,
bumps, lane changes, the player crashing and the crane): with the
original's skill and segment speeds loaded, the race matches every frame
in every field, the opponent included (3–231 and 233–599; the capture
lost two frames at 232). `work/league_lr2.bin` (another skill roll, 318
frames) matches too. `work/league_touch.bin` (1345 frames: league_lr1's
opening state written into the original at the first frame, then inputs
the port found to make the player block the opponent as it laps round,
`physcheck ... race` with TRIAL/BLOCK): the opponent laps, bumps, dodges
and touches the player's car at frame 1319; all 1342 frames match.

## Lesson candidates

- Copylock, and how a cracked image still yields the genuine key.
- A C64 (6502) game converted to the 68000 instruction by instruction, data
  and all: how to recognise it (zero-page-like variables, little-endian
  pointers, byte-wise carries) and why the port reads the data, not the
  layout.
- Tracks as a grid of cells with a few plan shapes and height profiles.
- Filled-vector 3D on the Amiga with the CPU: projecting by angles (atan2)
  instead of division, and what that does to straight edges.
- Rigid-body car physics in fixed point (to be recovered).

## Evidence

`.analysis/stunt_car_racer_amiga/`:

- `work/disk.adf` (copy of the user's image), `work/loader.bin`,
  `work/game.bin` (program as on disk), `work/program_dec.bin` (decrypted);
  regenerate with `build/stunt_car_racer_assets import <adf> <dir>` (or `dd`
  of the sectors above).
- `work/title_loader.png` (research render), `work/s1.png` (menu capture).
- `harness/run.sh`: FS-UAE A500, KS 1.3, GDB stub on **port 6880**
  (`SCR_PORT`), own state directory. (6872 belongs to the Beach Volley
  harness.) `harness/dbg.py` (GDB RSP client), `harness/screen.py OUT COPPER
  [lines]` (picture from memory through a Copper list).
- `harness/scr.py STEP...`: key taps (`kXX`), waits, screenshots, RAM dumps
  on the running reference.
- `ghidra/ghidra.sh` (`import`, `decompile`, `all`): headless Ghidra over
  `work/M.bin` (512 KB image, decrypted program at $E700, seeds
  `work/seeds.txt`); `work/all.c` is the full pseudocode (480 functions).
- `tools/tracks.py [TRACK DUMP]`: track-bank prototype decoder and the RAM
  comparison; `work/ram/*_picture.{bin,png}`: RAM and screen on the Little
  Ramp and Hump Back pictures.
- `harness/prof.py N` (PC and return-address sampling), `harness/frame.py
  OUT` (one frame: RAM at the start of drawing, the drawn buffer after),
  `harness/pose.py OUT X Z HEIGHT YAW PITCH ROLL` (the same from a chosen
  pose), `harness/poses.py TRACK SEGMENT STEP [SIDE] [TURN]` (poses on a
  circuit), `harness/view.py DUMP PICTURE TRACK SEGMENT|cell OUT` (port vs
  original, pixel for pixel; uses `stunt_car_racer_assets view`).
  `work/ram/race_*.{bin,png}`: two kept frames (Little Ramp f1, Hump Back
  h1); `work/compare/*.png`: original | port | difference for every frame
  listed under "Race view" and "Race screen".
- `harness/full.py OUT [N] [D]` (whole frames at the buffer swap),
  `harness/screencheck.py CAPTURE TRACK OUT` (whole screen port vs original,
  state read from RAM; uses `stunt_car_racer_assets screen`),
  `harness/items.py` (overlay items drawn in a frame), `harness/text.py N
  [KEY]` (characters printed), `harness/behind.py OUT TRACK BACK STEP|auto`
  (pose behind the opponent). The stub reports breakpoint stops only when
  interrupted and queues replies: drain after every stop.
- `tools/screens.py`: prototype decoders for the cockpit, palettes, sheet
  and items.
- `harness/physcap.py OUT FRAMES INPUTS` (with `PLACE=1` around the
  placement): per frame the state window before and after; `work/phys_lr1.bin`
  (319 frames) and `work/place_lr1.bin` (280); checked by
  `build/stunt_car_racer_physcheck <content> <capture> <track> physics|place|drive`.
- `stunt_car_racer_assets drive <content> <frames> <inputs> <prefix> [every]`:
  runs the game module headless and saves race screens (inputs as hex
  bytes, `xx*count` repeats).
- `harness/racecap.py OUT FRAMES INPUTS` (from the track picture): the whole
  race loop, a record at every frame start; `work/race_lr1.bin` (practice),
  `work/league_lr1.bin`, `league_lr2.bin`, `league_touch.bin` (race 3,
  Division 4: Division menu, 3, Return, Return, then the script; with
  `STATE=capture.bin` it first writes that capture's opening state into the
  original); the checker's `TRIAL=inputs [BLOCK=frames]` plays inputs (and a
  blocking controller) in the port from a capture's opening state;
  checked by `build/stunt_car_racer_physcheck <content> <capture> <track>
  race [first] [count]`. `stunt_car_racer_physcheck <content> autodrive
  <track> <frames> [gain dead flip sign top]` lets a simple controller drive
  the port and prints inputs that replay on the original (driving frames
  only).
- `tools/dis68k`, built from RetroReverse's general 68000 disassembler
  (`go build ./cmd/dis68k` in `RetroReverse/tools`).
