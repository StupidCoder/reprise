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
bank points at.

### Track definition (unpacker $5AE46, helpers $5AE00 byte read, $5AE0C cell step, $5FE56 segment set-up, $5B2D2/$5B2C8 profile read)

- 4 bytes: segment count, then three bytes kept at $1CA1B–$1CA1D (not yet
  interpreted; bytes 1 and 2 are equal on every track; byte 2 + 1 is stored at
  $1CA32 as a segment number: Hypothesis start/finish line).
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
- After the segments: 4 bytes ($1CA2A–$1CA2D), a count of pairs
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

**Height scale** (Interpreted from code): the view code takes a point's
horizontal angle with an atan2 ($64D66) and its distance with a hypot
approximation ($64DE8); the vertical angle ($624C2) uses (height difference
÷ 4) against that distance, so heights are a quarter of plan units in the
race view. The track-picture mode halves x/z and scales heights by about
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
## Lesson candidates

- Copylock, and how a cracked image still yields the genuine key.
- A C64 (6502) game converted to the 68000 instruction by instruction, data
  and all: how to recognise it (zero-page-like variables, little-endian
  pointers, byte-wise carries) and why the port reads the data, not the
  layout.
- Tracks as a grid of cells with a few plan shapes and height profiles.
- Filled-vector 3D on the Amiga; rigid-body car physics in fixed point (to be
  recovered).

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
- `tools/dis68k`, built from RetroReverse's general 68000 disassembler
  (`go build ./cmd/dis68k` in `RetroReverse/tools`).
