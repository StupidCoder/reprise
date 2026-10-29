# Tower of Babel (Amiga) — how the game works

Everything worth keeping from the reverse engineering of Tower of Babel v1.0
(Pete Cooke, Rainbird, 1990): how the program runs, how its data is stored
and encoded, the game's rules, and the bugs in the original. Remaining work
is in [plan.md](plan.md).

The original is hand-written 68000. It uses no operating system after boot
and keeps all its variables at fixed offsets from register A6 = $806.

- **Addresses:** all are the original's memory as the program sees it: the program at $800, the title images at $18200 and the main data at $1C5F0. Offsets into the main data are written as the program writes them, `$8200 + offset` (the program reaches $8200 as `$79FA(a6)` or `lea $8200(pc)`).
- **Variables:** "$11E4" means the variable at absolute address $11E4; "$692(a6)" means A6 + $692.
- **Hypotheses:** marked as such.

## Sources and limits

- All findings come from the user's disk image and from running it on a
  reference emulator (from 2026-10-28). No existing disassembly, port,
  symbol map or guide of this game was used. Editorial facts in the shell's
  catalogue (reviews and so on) are not evidence for this work.
- **Image:** `Tower of Babel v1.0 (1990)(Rainbird)[cr MAD].adf`, 901,120 bytes.
  - SHA-256 `0a8dff1a7c777dfe334ee0fdf8d0d196c87d1ada8f3e3549cb270c8bc8785c71`
  - MD5 `10d9a845a8cfc84fedf9d7b39664fa08`
  - A cracked copy; the boot block says "Cracked by Leaders In Crime (LIC) of M.A.D".
- **Tools:**
  - capstone 5 for disassembly;
  - Ghidra 12 headless, on a 512 KB chip memory dump taken at the main menu (302 functions);
  - Python for research renders.
- **Reference machine:** FS-UAE with a GDB stub, Kickstart 1.3, A500.
- **Evidence:** the harness scripts, recordings and dumps are kept outside the repository in the local research archive (`.analysis/tower_of_babel_amiga/`: `disk.md`, `formats.md` and `game.md` there are the working notes this document was condensed from; `harness/` the scripts).

## Disk and boot

### Layout

The disk has no AmigaDOS file system. The boot block loads raw byte ranges
with trackdisk.device; after that the game reads 512-byte blocks with its own
MFM track loader. That loader uses the standard AmigaDOS sector format, 11
sectors per track, so ADF offset = block × 512.

| Blocks | ADF bytes | Loaded to | Contents |
| --- | --- | --- | --- |
| 0–1 | 0–$3FF | — | boot block: `DOS\0`, credits text, loader code at $0C–$11F |
| — | $2C00–$F5FF | $60000 | stage 1: set-up and the packed program |
| — | $F600–$121FF | $5F800 | the crack's intro ("this game might crash…"), called from the boot block |
| $A9–$CA | $15200–$195FF | $18200 | title images: the logo (320 × 35) at $18200, the globe (160 × 133) at $197E0 |
| $CB–$333 | $19600–$667FF | $1C5F0 | main data: graphics, panel set 1, shapes, sounds, all towers; loaded after the title |
| $334–$39F | $66800–$73FFF | $23BC0 | panel sets 1–3, $4800 bytes each (the "Panel Style" option, $9F20). Set 1 equals the copy in the main data. |
| $3A0–$537 | $74000–$A6FFF | $1C5F0 | title music: one sample, 208,300 bytes used, period 176 |
| $538–$6DF | $A7000–$DBFFF | — | never read; from $540 on it is noise (7.93 bits per byte) |

**Load checks:**
- Every load is followed by a 32-bit additive checksum check and a retry ($7B78 $1EF3C5EE, $7C32 $6E256AEB, $7F98 $28B1E3CB).
- Block $AA, part of the logo, is read alone and summed ($29CF849D) to recognise the game disk: the "Waiting for Disk" check before panel loads.

### Boot chain

1. The boot block reads $2C00 bytes from $F600 to $5F800 and calls it (the crack's intro), then $CA00 bytes from $2C00 to $60000, and returns A0 = $60000.
2. At $60000 the program:
   - takes over the machine (supervisor mode, DMA and interrupts off, CIA set-up);
   - copies $C7A0 bytes from $600BE to $100 and calls them;
   - clears $18000–$80000;
   - jumps to $7A5E with the stack at $800.
3. At $100 a decruncher unpacks the program in two passes.
   - **Backward LZ:**
     - The bit stream is read from $C89C downwards in 32-bit words, least significant bit first, with a sentinel bit.
     - Output goes downwards from $4FBE + the end offset.
     - **Literal runs:** a 3-bit count; the escape 7 adds 2, 8, 12 or 15 more bits. Each literal is a 3–7 bit index into a 264-byte table at $290.
     - **Match length classes:** 2 and 3 use 9- and 10-bit offsets; then 4, 5–6 and 7–14; and 15–270 with a 10-bit offset.
   - **RLE:** from $4FBE..$19000 to $800, with escape byte $A3. `$A3 0` is a literal $A3; `$A3 n b` is n + 3 copies of b.
   - The string "DEFJAM" in the packed data is probably the packer's name (hypothesis).
4. At $7A5E the program, in order:
   - sets the interrupt vectors;
   - loads the title images;
   - calls the copy-protection hook at $17600 (the crack's stub: `moveq #0,d0; move.l #$67CCA330,$24.w`);
   - loads the title music;
   - asks for a word from the manual (page / paragraph / line / word, in English, French or German; the crack accepts any word);
   - plays the title, loads the main data, asks for the player's name, and goes to the main menu.

**Anti-tamper:** many checksum loops over the program jump around code that
would wipe memory; the crack turned their conditional branches into
`bra`. None of this is game logic.

### Memory map

- $800–$18200: program code and its data. This holds the variables at A6+, texts, menus, tables, the copper list at $3B5C and the sound lists.
- $18200–$1C5EF: the title images.
- $1C5F0–$697EF: the main data.
- $69800: the display. Four bitplanes of 320 × 200, $1F40 apart, with a second buffer $7D00 further on.

## Program structure

### Internal interface

The jump table of `bra.w` at $E1E8–$E314 is the program's internal API.

### Interrupts

- **Level 2 ($80E2), keyboard:**
  - the CIA-A keyboard is translated to ASCII by tables at $69C(a6) (unshifted, shifted +$70, +$E0);
  - the key goes to the key byte $692(a6);
  - repeat counters $693/$694 give the first repeat after $23 frames, then every 4;
  - shift and control are in $695.
- **Level 3 ($7FA8), vertical blank:**
  - mouse counters update the pointer $E/$10(a6), in hires units, clamped to 0–$27F and 0–$18F, scaled by $11D0(a6);
  - the buttons go to $69A(a6) (bit 1 left, bit 0 right);
  - the pointer sprite is positioned;
  - the hook at $12(a6) is called with (x/2, y/2, buttons); menus use it ($113E8).
- **Level 4 ($865A), audio:** segment lists and effect channels.

### Screen and copper

- **Screen:** 320 × 200 in 4 planes, $1F40 apart. Buffers are at $16(a6) and +$7D00; $87EE swaps them.
- **The original's frame rate:** a game frame lasts at least 14 video frames (the counter $28(a6)). So the game updates at most about 3.6 times a second (hypothesis, not yet measured).
- **Copper list modes:** $88EA builds the list at $3B5C in one of four modes, chosen by $6(a6):
  - 0: plain 16 colours;
  - 1: sky bands on colour 7, and the panel palette from line $82;
  - 2: the same with the tower's colours;
  - 3: the title's three colour bands.
- **Display window:** set at $8B30.

### Front end

- **Order of screens:** the crack's intro, the protection text, the manual language (1–3), the word prompt, the title (logo, globe, sampled music), "Please Enter Your Name", then the main menu.
- **Main menu ($8B9C):**
  - Menu items are 10-byte records: flags.w, then x0, y0, x1, y1 in low-res pixels. They are reached through the pointer $E02(a6); the selection loop is $112EA, and the choice comes back in $E00(a6).
  - Choices are dispatched at $8E48: Play Game $8EAE, Save/Load a Position $A6B6, View/Update High Scores $A81E, Tower Designer $E386, Demo Sequence $E2F4, Restart Game.
- **Play Game** offers Standard, Custom or Tutorial; the game type is $11DE(a6): 0 standard (group $1140(a6)), 1 custom, 2 tutorial; demonstrations use types ≥ 4.
- **Choose Tower** shows nine towers A–I in a 3 × 3 grid, the neighbouring groups at the sides, and the percent complete. Its buttons are at $A542.
- **Information on Tower** ($A12A) shows the name, author, the spiders available and the objective text.
- **Starting a tower:** $13278 unpacks the tower, then $E274 and $9F84 start it.

### The game screen

- **The 3D view:** filled polygons, dithered colours, and a copper sky gradient on colour 7.
- **The panel:**
  - spider select (Zapper, Pusher, Grabber), Fire, the turn and move arrows;
  - the cameras N E S W, Pause;
  - Stop/In, Run/Out, Run All, Write, Delete, Clear;
  - a program strip.

### The play loop ($9968)

Each game frame runs, in order:

1. $AB44: inverts object pens 23 and 24 (a blink);
2. $ADFC: panel input, polled once per game frame;
3. $BC7A: object logic (programs and moves);
4. $BEE4: each object's own behaviour and its animation frame;
5. $D432: the 3D view;
6. $87EE: the buffer swap;
7. $15342: sounds.

**Before the start:** a READY loop runs ($151CC draws READY). The first direct move (for example forward) starts the tower: $11E6 = 0, then $11E4 = 0. Programs alone do not start it.

### Randomness

- The seed is $890(a6), a 16-bit LCG, r × $5E5 + $29, at $135CA. One call site adds $2A instead.
- Menus and the protection read the CIA time-of-day counter ($BFD800). The port uses its own random numbers there.

## Data formats

### Pictures ($11862)

- **Format:** 16 colours in four bitplanes interleaved by word. For each row and each 16-pixel column come the words of planes 0 to 3.
- **Drawing:** $11862 takes (x, y, width in words, height) and draws into the current buffer's planes.
- **Title pictures:**
  - the logo: 20 words × 35 rows at (0, 10);
  - the globe: 10 words × 133 rows at (80, 61).
- **Title palette:** 48 colours at $2C9E ($2498(a6)), faded in by $1252C. Copper mode 3 switches the 16-colour bands at screen lines $36 and $71.
- **Menu palettes:** use the same 48-word form ($23F0(a6); hypothesis from the calls at $8C02/$8E0A).

### Towers ($13278)

A tower is a 288-byte record ($120 bytes). It is a bit stream read most
significant bit first through 32-bit words ($1390A). 2,269 of its 2,304
bits are used.

| Bits | Into | Meaning |
| --- | --- | --- |
| 62 × 7 | $D22 | name (7-bit text; a non-printable character ends it) |
| 62 × 7 | $D60 | author |
| 10 × 7 | $D9E | lock password ("lebab", "tivros", …; empty = unlocked) |
| 14 | $E18 | time limit in seconds (shown as 4 digits) |
| 10 × 1 | $E1A.. | flags, stored as 0 / −1 words (see below) |
| 2 | $E2E | time bomb speed (0–3; added + 1 to the count each frame) |
| 3, 3 | $E30, $E32 | Klondikes to collect, objects to destroy (the objective) |
| 2, 3, 2, 3, 3 | $E34–$E3C | $E38 sky type; $E3A the moon's heading; others open |
| 9 | $E3E | the moon's x (16 + 16 × value) |
| 7 | $E40 | the moon's y (16 + value; seen only when ≤ $37) |
| 2 | $E42 | the moon's animation speed |
| 4 × 8 × 8 × 3 | $1CC9.. | cells (below) |
| 20 × (6 + 8) | objects | kind and position (below) |
| 18 × 12 | $2414 | colours, 4-bit R, G, B |
| 2, 1, 1 | $A4–$A8(a6) | open (sound settings, hypothesis) |

**Flags:**
- 0–2: the spider is absent (Zapper, Pusher, Grabber); $1362A selects the first one present.
- 5: no time limit ($A1B4/$A22E).
- 6: no lightning.
- 7: no meteors.
- 8: Proximity Mines leave no hole ($E2A).
- 3, 4 and 9 are open.

**Cells:** floor by floor, then row (z), then column (x). Each 3-bit code
becomes a cell byte by the table $1375A: 00 C0 C8 E0 F0 30 E8 00. The bytes
go into a 10 × 10 grid per floor with a border of non-floor cells.

**Objects:** each has a 6-bit kind and an 8-bit position (bits 0–2 x, 3–5 z,
6–7 floor). Only kinds 4–39 are placed.
- Kinds 4–6 are the player's spiders and go to slots 4–6 ($15DA + kind × 32).
- Others fill slots from $16BA.
- The kind table at $13742 has 10-byte entries: a type word, a long shape offset from $18200, a word, and a flags word.

**Colours:** the first 16 are the view's palette. Colour 16 is "Floor Lit" (the horizon line), 17 "Floor Unlit" (ground shadows). Two more are the sky's top and bottom.

**Object names:** the designer's list at $6F10 holds 46 names:
- Floor Section, Supporting Block, Lift Up, Lift Down;
- Player's Zapper, Pusher and Grabber;
- Zapper and Pusher facing N, E, S and W;
- Rotating Zapper and Pusher;
- Worm, Watcher, Hopper, Proximity Mine, Time Bomb;
- Reflector, Prisms, Lizards, Klondike, Bonus Flag, Convertors, Forcers, Freezer, Exchanger.

The checked kinds match the name order: 4 Player's Zapper, 8 Zapper facing E, 31 Klondike. The full mapping is open.

**Tower groups in the main data:**

| Address | Towers | Group |
| --- | --- | --- |
| $61372 | 13 × 9 | Standard, Groups 00–12 ($A20 bytes a group; $91C0) |
| $5FF32 | 9 | Tutorial ($8F2C) |
| $5E0D2 | 18 | demonstrations and interludes ("Zapper Demo", "Pusher Dies 1", …; $158C6) |
| $5F512 | 9 | the Custom group's empty default (copied to $60952 by $9046) |

All 153 towers decode to plausible names and authors (Pete Cooke, Chris
Newcombe, Tim Roberts, "Legin Drawnek", "Axeman Towers Ltd.").

### Cell bytes

| Bit | Meaning |
| --- | --- |
| 7 | solid: a floor slab or a block, something to stand on |
| 6 | opaque: hides the faces of its neighbours |
| 5 | lift shaft |
| 4 | lift in its up position; light passes down the shaft ($CCCA) |
| 3 | block: full height up to the slab above; stops walks and beams |
| 2 | lit: nothing above it. Lit tops use the lit pen and show shadows; the bit passes down when a floor is removed |
| 1 | drawn in the second square colours (the colour pattern $CD90) |
| 0 | border: outside the 8 × 8 tower |

- **Addressing:** a step in a compass direction is +10, +1, −10 or −1 (table $23E4; heading 0 north, 1 east, 2 south, 3 west), and a floor up is +100.
- **The ground:** floor −1 sits below floor 0 and carries only the lit flag.
- **The object map:** follows the cells at $1E4E, in the same 10 × 10 × 4 layout. It holds the slot standing in each cell (−1 none).

### Objects ($15DA + 32 × slot)

There are 52 slots. The ground's cells follow slot 51, so a lit bit passed
to the ground shows up as "slots" 52–54. Slots 4–6 are the spiders; slots
0–3 hold the four cameras.

| Offset | Meaning |
| --- | --- |
| +$00 | type (kind table word 0); ≥ $1A gone or exploding; negative ends the list |
| +$02 | the shape shown (a main-data address) |
| +$06, +$08, +$0A | position x, y, z (a cell is $200 units) |
| +$0C | heading |
| +$0E | move kind: 0–3 walk, 4–7 pushed, 8–11 lift; higher values per type |
| +$10 | frames left of the move (0 = standing; nonzero = between cells) |
| +$12 | what beams do to it (below) |
| +$14 | the cell it stands in |
| +$16 | the cell it is moving into (0 none) |
| +$18 | its own slot |
| +$1A, +$1C, +$1E | counters, by type |

**Flags (+$12):**
- Low byte, bit by bit:
  - 0: destroyed by a zap;
  - 1: reflects;
  - 2: pushable;
  - 3: grabbable (Klondike);
  - 4: convertor;
  - 5: prism;
  - 6: forcer;
  - 7: Freezer, Wiper or Exchanger.
- High byte:
  - bit 0: Klondike taken;
  - bit 1: the second particle cloud;
  - bit 2: the floor under a cloud goes (a mine's hole);
  - bit 3: a zap reverses its move.
- Seen values: spiders $0005, Klondike $0209, block $0004, enemy Zapper $0001.

**Programs ($156A + 16 × slot):**

| Offset | Meaning |
| --- | --- |
| +0 | the move under way: 0 forward, 1 back, −1 none |
| +4 | mode: 1 running, 2 running all, 3 writing, −1 idle |
| +6 | the next command; while writing, the cursor |
| +8 | eight commands: 1 forward, 2 back, 3 left, 4 right, 5 fire, 0 nothing |

**Panel lights:** $1558 move, $155C turn, $1560 program, $1564 the program shown in the strip.

### Object shapes

- **Location:** objects' shapes are in the main data from about $4C3A0 to $5D4F2, with animated sets of frames.
- **Structure:** a shape has shadow polygons, then parts. Each part has a sort centre and faces; a face has a style, a normal and byte vertices that are doubled.
- **Style bits:**
  - 7 + 6: the object's own pen ($892 + (slot + 9 [+ 7]) × 16);
  - 7 alone: a see-through pen ($B22 + i × 32: a mask word and plane words per row, filler $1447C);
  - 6 alone: the outlined filler $1414C (span ends in colour 0);
  - 5: no unlit adjustment. Without it, faces in unlit cells use pen i − 1.
- **Object pens ($892(a6)):** 0–15 solid colours, 16–22 dithers. 23 and 24 are inverted every game frame.

### Sound

- **Segment player ($1570A, one channel):**
  - A list is volume.w and period.w, then pairs of offset.l (from $8200) and length.w (in words), until a negative long.
  - The audio interrupt ($865A/$86CA) queues the next segment each time.
  - Title music, list $3B24: one sample, 209,452 bytes at period 176 (20,153 Hz).
  - Jingle $3AF2, started at $9A8E (the end of a tower?): 43,108 bytes at period 414, at $393F4.
- **Effects:**
  - **Records:** 46 bytes each, at $3504–$3AF1 (34 effects).
  - **Fields:** +$24 priority; +$18 the waveform's offset from $8200; +$1C/+$20/+$26/+$2A envelope longs; +$2E a second part when bit 1 of +$09 is set. The waveform starts with its length in words. The envelope's meaning is open.
  - **Queueing:** $15316/$1531E queue up to 16 a frame, and $15540 starts each on the channel with the lowest priority.
  - **Waveforms:**
    - $37E00/$37E12/$37E24 (16 bytes);
    - $37E36 (2,048, noise?);
    - $38638 (240);
    - $39312/$39354 (64);
    - $393A6 (128).
  - **Periods and volume:** the period table is at $D2(a6) (432 entries); the volume offset is $CE(a6).
- **Effects the rules start** (records, as `$806 +` offset):
  - blast $2D2C;
  - footstep $2E12 (on a lift $2E40);
  - floor gone $300C;
  - hop $3068;
  - thaw $2FB0;
  - convertor hum $31D8.

### Other main-data ranges

- $1C7D0 + n × $A0: images drawn by $10F64 (designer map icons? open).
- $238C8 + i × $28: 8 × 10 overlay glyphs, as a keep mask with pixels in colour 15. The "/" is at $23A58.
- $23BC0–$283BF: the panel set; the panel's pixels are at $24730–$27038. The layout is open.
- $283C0, $2CDC0, $2D3C0 (+$200 steps): small images (tower chooser buttons $A542/$A5D2, and $D34E).
- $2D3C0 + heading × $180: star tables.
- $2D9C0: moon frames.
- $339C0: lightning bolts.
- $34280: meteor frames.
- $34C80: particle offsets.

## The 3D view ($D432)

**Per frame:**
1. $AAD4: the camera;
2. $CDE6: the projection rows;
3. $1191C: clears 288 × 112 at (16, 16) to colour 7, which the copper turns into the sky and ground bands;
4. $D37A: ground shadows;
5. $D33A: stars;
6. $CF56: the moon;
7. $D120: meteors;
8. $CFFC: lightning;
9. the cells and objects;
10. the overlays.

### Camera ($AAD4)

- **Source:** the active object's record (position and heading & 3). Each quarter turn maps (x, z) to (−z, x).
- **Spiders:** add the option offsets from $9F84: $FBC sideways, $FB8 height (+$58), −$FBA distance.
- **Result:** stored at $FB0/$FB2/$FB4, the heading at $FB6.

### Projection ($CDE6)

- **Rows:** rows of cell edges run from the far side of the tower (depth $800 − z) towards the camera, in steps of $200, clamped to ≥ $100. There are at most 9 rows: min(($880 − z) asr 9, 8) + 1.
- **Per row:**
  - 9 x edges: ((x − cx) << 8) / depth + 160;
  - for levels 0–4, the tops and the slab undersides (y − $20): 72 − ((y − cy) << 8) / depth.
  - The divisions are 68000 DIVS, so they truncate towards zero.

### Traversal

- **Rows:** far to near; the camera's own row is not drawn.
- **Levels in a row:** first those above eye height (min((cy + $20) lsr 9, 3)) from floor 3 down, then floors 0 up to eye level.
- **Columns in a level:** first those right of the camera from column 7 inwards, then those on the left from column 0. The counts are min(2 − (cx asr 9), 7) + 1 and min((cx asr 9) + 4, 7) + 1.
- **Directions:** the original walks the grid with strides from the heading table $23C4; the native code turns the cell coordinates instead.

### Faces

- **Kinds:** sides ($D8E0, $D96A), the front ($D9F6, the rectangle fill $11540), the underside when above the eye ($DA4C), and the top when below it ($DAB2).
- **Hiding:** a neighbour with bit 6 hides a slab's face; one with bit 3 hides a block's.
- **Blocks:** a block reaches from the slab's underside to the next level's underside.
- **Lifts** ($DCD8 heights, $DE28–$DFDC): the platform is drawn at the floor, at the underside of the level above (bit 3), or at the lift rider's y, projected at the centre depth ∓ $100.
- **Pens ($A22(a6)):** 16-byte pens at $1228, as two rows of four plane words (2 × 2 dithers), aligned to the screen. In order:
  - under, side, front / odd side, odd front, top, lit top;
  - the same for the second square colour;
  - a spare;
  - ground shadow (colour 8);
  - lift top, lit lift top.

### Polygon filler

- **Clipper ($147E0):** Sutherland–Hodgman on x, then on y, with 14-bit intersections. Repeated points are removed.
- **Scan-line fill ($13E3C):**
  - **Start:** the top vertex has the least y, then the least x. The right side is walked forwards and the left side backwards.
  - **Edges:** 16.16 fixed point. The slope is DIVS(dx × 256, dy) << 8, including DIVS's overflow case. A vertex's x replaces only the integer part.
  - **Flat right edges** after the first are passed over.
  - **Spans:** inclusive; the last line is strict.

**Mouse picking ($1507C):** the filler compares the pixel under the pointer
before and after filling each top face. That gives the cell under the
pointer ($7EE–$7F4).

### Objects in the view ($E0AC, $13934)

- **When drawn:** each cell's object (from the object map) is drawn at the cell's place in the traversal. Slots ≥ $3F and types > $1A are skipped. Type $1A is a particle cloud.
- **Shadows:**
  - only in lit cells, and only while the eye is above the first shadow point's height;
  - drawn in the floor's unlit top pen (the lift top pen in lift cells).
- **Part order:** far to near by the squared distance of the sort centres (32-bit). A new part goes before equal ones.
- **Back faces:** a face is skipped when its turned normal · (first point − eye) ≥ 0. A point behind the eye drops the face.
- **Moving objects:** clipped in 3D at the boundary of the cell being drawn. The side comes from the object's position against the cell centre ($FA6/$FAA), with the plane ±$100.

### Sky and effects

- **Colours (copper mode 2, $89D2):** registers 0–7 and 9–15 come from the tower's colours, and 8 from colour 17.
- **Sky gradient on colour 7:**
  - it starts at "Sky Colour Top" and changes at the lines in the sky type's list ($8ACA: type 0 has 17 lines, type 1 has 15, type 2 has 13, type 3 none);
  - it runs a 15-step gradient towards "Sky Colour Bottom" ($12392: 8.8 fixed point, start | $80, step DIVS(diff << 8, 15));
  - type 2 alternates bottom and top stripes;
  - longer lists read on into the panel palette;
  - line 72, the horizon, takes colour 16;
  - the panel palette starts at line $82.
- **Stars ($D33A):** a table at $2D3C0 + heading × $180 of 64 × (x, y, pen). 21 × density + 1 stars are plotted, each in the first row of its pen ($14F88).
- **Moon ($CF56):**
  - Shown when the heading equals $E3A and $E40 ≤ $37.
  - 16 pixels wide and up to 16 rows, at (16 + 16 × $E3E, 16 + $E40), with mask rows $CF36.
  - Frames are at $2D9C0 + type × $1000 + (frame & 31) × $80.
  - The frame counter $E44 advances by $E42 − 1, and only when the moon is drawn.
- **Meteors ($D120, when tower flag 7 is clear):**
  - **Start:** after a wait ($FCC), a meteor starts at x from the random number (r & $1FF, halved unless < $190) and y −15.
  - **Movement:** −4, +2 per frame until y ≥ $38.
  - **Drawing:** only in the heading it started in. 8 frames at $34280 + n × $140, a 32-pixel masked image at x + 1 ($D1DE), in the top 56 lines only.
- **Lightning ($CFFC, when tower flag 6 is clear):**
  - **Timing:** every other frame, while $20(a6) = 0. After a wait ($FC4) a bolt appears; then comes a short wait (6–13) or a long one (32–62, with a new heading). The thunder follows after 4–7 more waits ($FCA).
  - **The bolt:** 16 × 56 at $339C0 + n × $230, in column 2 + (r & 15), shown only in the heading $FC6.
  - **Effect:** a crack sound and a palette flash. The flash uses copper list 2 with $2592: the 18 colours, with $FFF ORed in where the mask at $1D448 has a bit.
- **Particle clouds ($14C18, type $1A):**
  - 2n big dots (2 × 2 at even x, $14FEA), then 2n small ones, where n is the cloud's remaining count;
  - offsets from $34C80 (+ $18C0 for the second cloud, +$12 bit 9), plus the table $14D58 by 32 − n;
  - colours 9–15 and 8 in turn.
- **Overlays:**
  - READY ($151CC, only before the start);
  - compass and counters ($150BC);
  - the timer ($15152, BCD at $121A);
  - the sights ($E0F6).
  - They are drawn on line 18 or 118, chosen by the options $1112/$1114.

## The rules

### Program steps and moves ($BC7A)

- **Spiders (slots 4–6, type < $1A):** a standing spider takes its next program command ($CB6C); a moving one advances its move ($C97E).
- **Other objects (slot 7 on, until a negative type):** after the start ($11E4 ≥ 0) they only advance their moves here; their decisions come in $BEE4.
- **Forcers ($BCE4):** run last (below).

**Program steps ($CB6C):** only in mode 1. The position advances by 1; past 7
the mode becomes −1. A command that is not possible is skipped.

**Starting a move ($B510):**
- **Walk:** the cell ahead must be solid (bit 7), not a block (bit 3) and have no object. Then the state is the direction and the progress 32.
- **Lift rides (directions 8 up, 10 down), from a lift cell (bit 5; bit 4 = up position):**
  - going up, the cell above becomes solid and is claimed;
  - going down, the cell's opaque bit and the lower cell's block bit are cleared;
  - $1216 counts the rides under way.
- **Back ($B4B4):**
  - a moving spider reverses ($B5E2). A walk cannot reverse at progress 16, 10 or 22. A reversed lift ride swaps its cells and sets the spider's bit in $1218's high byte;
  - a spider standing on a lift rides it;
  - otherwise a backward step needs the "Backwards allowed" option ($110E).
- **Turns:** instant.

**Advancing a move ($C97E):**
- **Walk:**
  - 16 units a frame;
  - at progress 32 − claim (claim = $CB28[type], 10 for spiders) the cell ahead is claimed. If it is taken, the object bounces: state ^ 2, progress 32 − progress;
  - at 16 the cell and target swap;
  - at the claim value the old cell is released.
- **Pushed (states 4–7):** steps from $CB3C; the swap comes at 8 and the release at 0.
- **Lift (8–11):** y changes by ± $CB4C[progress − 1]. At the end:
  - going up, the cell gains the block bit and the target the opaque bit;
  - going down, the cell loses the solid bit;
  - the old cell is released and $1216 drops by 1.

### Firing ($B66E)

The spider in slot 4 zaps ($B684), slot 5 pushes ($B83C) and slot 6 grabs
($B90E), always in its heading.

- **Zap and push beams:**
  - **Range:** up to 16 cells. They stop at border (bit 0) and block (bit 3) cells and at type $3F.
  - **Passed through:** empty cells, the shooter (by +$18; after a reflection no one is the shooter) and particle clouds.
  - **The zap also** stops at lift riders and passes objects that are only claiming the cell (cell ≠ their +$14). The push does not check that.
- **Zap, by what it hits:**
  - **Destroy:**
    - the object becomes a particle cloud: flags 0, progress 0, +$1A = 32, its target released, type $1A;
    - a spider triggers the lost check ($BC16);
    - other objects count towards $E32;
    - a working Freezer is undone ($11E4 + 1);
    - then the win check ($BBE2).
  - **Reflect:** the direction turns round (^ 2), and no one is the shooter any more.
  - **Prism:**
    - If its heading equals the direction, the beam turns left (direction − 1).
    - If heading + 1 equals the direction, the beam turns right (direction + 1).
    - Otherwise the beam stops.
    - The prism becomes the shooter.
  - **Convertor in line** (heading ^ direction even): the beam goes on as a push.
  - **Reverse a move** (+$12 high bit 3): not at progress 0, 2, 30 or 16.
- **Push:**
  - A pushable, standing object moves one cell when the next cell is solid, not a border or block, and has no object. It gets progress 16 and state direction + 4, and claims the target.
  - A convertor in line turns the push into a zap.
- **Grab beam (no range limit, no shooter skip):**
  - **Klondike:** taken (high bit 0), $11D8 + 1 up to $E30, then the win check.
  - **Forcers** ($11 up $BAAA, $12 down $BB42):
    - they move every free lift that is in the other position, on floors 0–2 (up) or 3–1 (down), at most 20 of them;
    - the lifts are carried by slots 25–27 for 32 frames ($BCE4);
    - the cell flags are rewritten at the end from the list $11E8.
  - **Freezer:** +$1C = 128 and $11E4 − 1. The tower stops until the Freezer's own count reaches 64.
  - **Wiper:** +$1C = 63 when idle.
  - **Exchanger:** the Grabber takes its place and turns round; the Exchanger explodes and counts.

### The end and the freeze counter

- **Won ($BBE2):** when everything is collected and destroyed, $11E0 = $FC0 + $15 and $11E2 = 1.
- **Lost ($BC16):** reason 4 if the Grabber is gone with Klondikes still missing; reason 3 if all three spiders are gone.
- **$11E4, the freeze counter:**
  - it is −1 before the start, and the play loop raises it to 0 at the start;
  - each working Freezer lowers it by 1;
  - objects with decisions do nothing while it is negative.

### Each object's own behaviour ($BEE4)

**The loop:** $BEE4 runs after the moves. It goes from slot 4 to the first
negative type, skips types ≥ $1B, and calls a handler per type ($BF16
table). It also sets each object's shape for the frame (frame tables
$BD9A–$BED4; spider frame sets $BF4E); the frame counter is $FBE.

| Type | Object (kind table flags) | Handler |
| --- | --- | --- |
| 0 | spider | $BF6E |
| 1, 2 | Zapper, Pusher | $C066, $C0B2 |
| 3, 4 | Rotating Zapper, Rotating Pusher | $C0FE, $C14E |
| 5 | Worm ($0804) | $C19A |
| 6 | Watcher ($0204) | $C1FC |
| 7 | Hopper ($0001) | $C382 |
| 8 | Proximity Mine ($0204) | $C466 |
| 9 | Time Bomb ($0204) | $C586 |
| $A, $B, $C | Block, Reflector, Prisms | none |
| $D | Lizard ($0200) | $C66E |
| $E | Klondike ($0209) | $C6F4 |
| $F | Bonus Flag | $C74E |
| $10 | Convertor | $C76C |
| $11, $12 | Forcers | $C7B4, $C7E4 |
| $13 | Freezer | $C814 |
| $14 | Wiper | $C868 |
| $15 | Exchanger | none |
| $1A | particle cloud | $C8F0 |

Handlers 1–9, $D, $11, $12 and $14 do nothing while $11E4 < 0.

- **Spiders:**
  - a footstep sound every 4 frames of a walk ((progress & 3) = 1, when $A6 = 0), the lift sound on a lift cell;
  - a reversed lift ride (its $1218 bit) shows frame (6 − state / 2) & 3 once and clears the bit.
- **Zapper and Pusher:** fire every frame in their heading, with their own slot as the shooter.
- **Rotating Zapper:** +$1E = $BE0A[frame & 63]. It fires when that value is a multiple of 8, in direction value / 8.
- **Rotating Pusher:** pushes when (frame & 15) = 0, in direction frame / 16.
- **Worm:** when standing it walks on in +$0E; if it can't, +$0E ^= 2 (it turns round).
- **Watcher** (+$0E is its direction): keeps a wall on its left. When standing:
  - **Turning:** a turn under way (+$1C) counts down, showing turn frames $BE6A[(±$1C + 4 × state) & 15]; +$1E < 0 means a right turn.
  - **Left free:** if the cell to the left is free ($C34A: solid, no block, no object), it turns left (4 frames).
  - **Otherwise, in order:**
    - after a turn, or when the left is blocked, it walks on;
    - if it can't, it turns right (4 frames, +$1E = −1);
    - else it turns round (8 frames);
    - else it stands, with frames chosen by the frame counter.
- **Hopper:**
  - when standing it shows its base frame and +$0E &= 3. It then tries to move right, ahead, left, then back;
  - boxed in, +$0E |= $10 and progress 16: a hop on the spot;
  - the hop sound plays at (progress & 15) = 1.
- **Proximity Mine:** goes off when a neighbouring object is moving (progress ≠ 0, state < 8).
- **Time Bomb:** +$1C += $E2E + 1 each frame; at ≥ 800 it goes off.
- **A blast** (both):
  - every neighbouring object of any type, except lift riders, becomes a particle cloud, and so does the bomb itself;
  - none of these count towards the objective;
  - a mine's cloud gets +$12 = $400 (leave a hole) unless $E2A < 0 (tower flag 8);
  - then the lost check, and the blast sound.
- **Lizard** (uses +$0C):
  - when standing, if the cell remembered in +$1C is free now, the floor there goes (floor-gone sound);
  - then it walks ahead; if it can't, it turns round and remembers its own cell in +$1C;
  - so it eats the floor where it turned, once it has walked off.
- **Klondike:** once taken (+$12 bit 8), 7 vanishing frames; then type $3F, and its cell and map entry are cleared.
- **Convertor:** a hum at (frame & 31) = 1.
- **Bonus Flag, Forcers:** frames only.
- **Freezer:**
  - +$1C counts down;
  - at $40, $11E4 + 1: the tower runs again, with the thaw sound;
  - so the tower stands still for 64 frames, while the Freezer animates for all 128.
- **Wiper:**
  - +$1C counts down from 63;
  - at 48, 32, 16 and 0 it sweeps west, south, east and north ($23E4[count / 16]), removing each floor cell up to the border or a block;
  - each sweep plays the floor-gone sound.
- **Particle cloud:** +$1A counts down; at 0 it becomes type $3F and its cell and map entry are cleared. With +$12 bit 10, the floor under it goes.

### Removing a floor cell ($C924)

- **Conditions:** only a solid cell that is not a lift, has no object and, on floors ≥ 1, is not above a block.
- **The lit bit:** passes down to the first non-empty cell below, or into the ground cell.

## Bugs in the original

Both are kept in the re-implementation.

- **Meteor plane mix-up ($D2B4):** in a meteor's first partial word, plane 3 takes plane 0's bits.
- **Freezer counter:** zapping a Freezer whose +$1C ≠ 0 always adds 1 to the freeze counter $11E4, even after the Freezer has already thawed and added its own 1. The counter then stays at 1, and the next Freezer only lowers it to 0, so it stops nothing.

## The re-implementation

**Namespace:** `babel`, under `games/tower_of_babel_amiga`.

**Code:**

| Folder | What it holds |
| --- | --- |
| `import/` | the decruncher (byte-equal to the machine's memory at $7A5E); the importer, which identifies the release by the loader's SHA-256 and checks the program and six block ranges (title, main, panel1–3, music) by SHA-256 |
| `data/` | decoders: title pictures, tower records, shapes, sampled pieces, effect waveforms; the launcher previews and the shared 3D model viewer |
| `game/` | `tower_map` (cells with shape flags, lit columns $CCA8, colour pattern $CD90); `tower_state` (the running tower in the rules' bordered layout: object map, objects, programs, panel lights, forcers, objective, freeze counter); `rules` (programs, moves, firing); `behaviour.cpp` (each type's update); `animation` (the frame choice as a pure function of state and frame counter) |
| `view/` | `polygon` (the clipper, the filler, see-through and outlined fills); `tower_view` (camera, projection, traversal, faces, objects, particles); `sky`; `overlay` |

**Assets tool:** `tower_of_babel_assets import | export | viewcheck | animcheck | framecheck | logiccheck | objectcheck`.

**Design choices:**
- **Drawing from state:** the view is drawn from game state by the native renderer, never by copying the original's 320-pixel output. That leaves room for widescreen.
- **Tick rate:** the rules run at the original's tick rate, apart from drawing, so positions can later be interpolated up to 120 Hz.
- **Cell numbering:** native cell index = original index + 100. The native grid includes the ground row as floor −1.

### How it was checked

Each part is compared with the reference machine's memory or screen, frame
by frame:

- **Decruncher and title:** the decruncher equals the machine's memory at $7A5E, and the title picture equals a capture.
- **3D view (`viewcheck`):**
  - Compared with the original's freshly drawn buffer, with stars, moon, effects and overlays off: identical, 0 of 32,256 pixels, in all four headings at several positions and eye heights. The cell flags equal the original's grid.
  - With objects on: identical on towers A–C in 24 grabs.
- **Animation (`animcheck`):** 0 differences in 3,800 demo frames: spiders walking, turning and standing; Zapper, Rotating Zapper, Worm, Hopper, Proximity Mine, Lizard, Klondike and Wiper.
- **Sky, effects and overlays (`framecheck`, the whole view window and the copper list):** identical in all 255 frames on towers A–C (sky types 0–2, with lightning and meteors switched on by poking; 80 meteor frames), in 3 lightning flash frames, and in 60 demo frames (18 with particle clouds).
- **Programs, moves and firing (`logiccheck`):** 0 differences in 15 recordings, 1,795 frames:
  - walks, skipped commands, push, bounce, lift up and down;
  - a Klondike grabbed and the tower won, a spider destroyed;
  - reflection onto the shooter, prism, reversal;
  - both forcers, Freezer, Wiper, Exchanger.
- **Objects' behaviour (`objectcheck`):** cells, map and records, including +$1E and the shape. 0 differences in 13 recordings, 1,825 frames, from twelve cases staged on tower A:
  - Zapper and Pusher, Worm, Watcher, Hopper;
  - a Lizard eating 2 cells, a mine set off by a walking spider;
  - a time bomb (Grabber lost, reason 4), a Rotating Zapper, a Rotating Pusher;
  - a Freezer (63 frames stopped), a Wiper (3 cells);
  - a Klondike vanishing, a Convertor with a Bonus Flag.

**Harness notes for the reference:**
- **Panel clicks:** in play the panel is polled once per game frame, so a click must hold the button for about 20 video frames. A 3-frame hold or an early click leaves the game at READY.
- **Keys:** go in through $692(a6).
- **Mouse:** a breakpoint at $806C in the vertical blank, then pokes to the pointer $814/$816 and $E9C/$E9E and the buttons $EA0.
