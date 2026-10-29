# F/A-18 Interceptor (Amiga) — how the game works

Everything learned while reverse engineering F/A-18 Interceptor (Electronic
Arts / Intellisoft, 1988, the user's ADF) for its re-implementation in
Reprise: the disk and program, the main loop and timing, how the data is
stored and encoded, the flight model, the crash, the cockpit messages and
the sound, the original's own bugs, and how each finding was checked.
Findings come only from this disk and runs of it (FS-UAE with a GDB stub,
Ghidra); addresses are in the research layout (the program loaded at
$100000, `.analysis/fa_18_interceptor_amiga/work/F.bin`). Statements
marked **hypothesis** are not confirmed. Remaining work: plan.md.

Contents: 1 Disk and program · 2 Program structure and timing · 3 Data
formats · 4 Free flight and the flight model · 5 Sound · 6 Bugs and
oddities in the original · 7 Research tools and checks


## 1 Disk and program

Source: `FA-18 Interceptor (1988)(Electronic Arts)[cr A-Ha].adf`, SHA-256
`e28e94698d68d0d2d3f009a67597a6bb70083e3ff2e4aea6c79369b76f93bfa2`
(901,120 bytes, the user's image; a cracked copy).

No outside material on this game (disassemblies, guides, maps, ports) was
consulted. Everything below comes from the image, the decompiled program
(Ghidra 11 headless, `ghidra/ghidra.sh`, capstone listing `.analysis/fa_18_interceptor_amiga/work/all.dis`)
and runs of the image on FS-UAE (KS 1.3, A500) through `.analysis/fa_18_interceptor_amiga/harness/`.
Hardware references: Amiga Hardware Reference Manual (copper, blitter,
Paula), AmigaDOS OFS layout, Aztec C start-up conventions.

### Volume

Standard AmigaDOS OFS volume "F-18 Interceptor", DOS boot block. Its
startup-sequence runs `BindDrivers`, `LoadWb`, `endcli`: the game is
started from its Workbench icon. The research boot disk (`.analysis/fa_18_interceptor_amiga/harness/mkboot.py`)
replaces the startup-sequence with `"F-18 Interceptor"` (CLI start works).

| File | Bytes | SHA-256 | Role |
|---|---|---|---|
| `F-18 Interceptor` | 331,232 | `d7301e20…02ffa` | the program (load file, 185 hunks) |
| `pix/splsh` | 23,224 | `d64e541d…e1c70` | title picture, ILBM 320×200×5 |
| `pix/inst5` | 7,230 | `870329e0…8889c` | instrument panel, ILBM 320×55×5 |
| `pix/frnt5` | 988 | `7cf5300d…2d012` | ILBM 288×12×5 (use not traced) |
| `text/text*` | — | see import | raw 8-bit sound samples (14 files) |
| `text/textply` | 3,676 | `a4c8b947…a6745` | pilot log (read/written by the log menu) |
| `text/textctl` | 14,704 | `0b7b6fdc…f3fe8` | log, second file (read/written with it) |
| `config` | 78 | `f268dc8e…3d063` | settings, read and written by $10846C… |

Names are the original's ("text" files are sounds). The `c`, `l`,
`libs`, `devs` files are Workbench 1.x system files, not imported.

### Program

Load file, 185 hunks (`.analysis/fa_18_interceptor_amiga/work/F.hunkmap.txt`); our research layout puts hunk
0 at $100000 and the rest after it, 8 bytes apart (`amiga_disk flat`,
`.analysis/fa_18_interceptor_amiga/work/F.bin`). All addresses in these notes are in that layout. Only
hunk 62 asks for chip memory (456 bytes: sprite data, $132C28).

- Hunk 0 ($100000, 41,804 bytes): Aztec C start-up (`h0`, `_main` $148BD4)
  and the C front end: `main` = $1003CE (opens graphics, shows the title,
  loads sounds, menu loop $107D48 via the screen procedure pointer $10A6CC).
  Functions open with `LINK A6`.
- Hunks 4–40: hand-written 68000 (no LINK): flight model, 3D engine, HUD,
  input, sound player. Absolute addressing throughout; variables in the BSS
  hunks ($10A6C8, $13A920–$145110).
- Hunks 41–73: data: 3D shapes (41–58), cockpit messages (59), fonts (61),
  screen texts (64), maths tables (63, 65), world tables (66–70), work areas
  (71–73, zero filled).
- Hunks 74–184: the sound driver (audio.device, $145110…) and the Aztec C
  library and amiga.lib stubs (symbols kept: `_OpenLibrary`, `_LoadView`…).

Copy protection: the "DEFENSENET SECURITY CHECK" screen ($13428C) after
three failed attempts reboots; this crack shows "CRACKED BY A-HA" in its
place ($1342AC) and the check never stops the game. The title picture is
checksummed (`h20` ($119314), expected $C560) and the intro music only plays
when it matches.

### Harness (research only)

`.analysis/fa_18_interceptor_amiga/harness/run.sh` starts FS-UAE with the GDB stub on port 6868 (6864/6866
belong to other sessions) and its own state directory; `mkboot.py --hold`
makes `.analysis/fa_18_interceptor_amiga/work/ref/boot.adf` with the program's first word changed to
`bra.s *`; `f18.py attach` finds the program through the Initial CLI,
restores the word and prints the hunk addresses. `f18.py key <names>`
feeds key presses through the program's own pending-key slot (flag word
$10A59E, raw code $10D088, read first by $108DA6), so the host keyboard is
not needed. `screen.py out.png` renders the display from graphics'
LOFlist copper list (GfxBase+$32): bitplane pointers, BPLCON0, modulos and
colour changes per line, at 640 pixels per line (low-res doubled).
Captures: `.analysis/fa_18_interceptor_amiga/work/ref/s1.png` title, `s2.png` main menu, `s3-6` demo.

## 2 Program structure and timing

### Start-up ($1003CE)

graphics.library → `h77` sound set-up → title picture `pix/splsh` loaded
and shown ($1001C8, LoadView) → text201 slot ($109660) → music samples if
chip memory allows ($1096BA) → display set-up ($107F04: View, ViewPort,
a 5-plane and a 4-plane 320×200 BitMap) → intuition, CloseWorkBench
($1007AE) → title checksum, then the intro tune ($109D7A or
$109CE6) while the title is up → instrument panel and strip pictures →
$107D48 main loop.

### Screens

The main loop calls the screen procedure in $10A6CC each frame; screens
set it and queue their texts ("Screen texts"). There are about 75 screen
procedures in $101962-$103C1A (C and hand-written assembler mixed); most
wait for the frame countdown `$13ADC6` (decremented every frame,
$101748) to go negative, or for a key flag, then queue texts and set the
next procedure. Seen in the texts: log disk check, call sign entry,
welcome back, main menu, missions with briefings (hunk 64), aircraft
choice, credits (block 15). The front end as rebuilt is described in
"The front end" below.

### The front end (2026-10-29)

Traced on the running original (`harness/proc.py` prints the screen
procedure, the countdown and the text queue) and from the code.

**Title.** `pix/splsh` is shown; after an empty loop of $21000 rounds
the title tune starts (below), then another $43000 rounds; `$107D48`
then loads the flight sounds from the disk and reads `config`, fades the
title out (16 steps, each colour component -1 per step, one vertical
blank and a $A00-round loop each), waits a $100000-round loop and starts
the main loop. The title's time is therefore loading time: on the A500
the tune played over the title for about 22 s (measured from the tune's
start to the main loop's first procedure, `$101962`). The checksum of the
title picture (`h20`, $C560) gates the tune.

**The title tune** is no sequencer: the music loader (`$1096BA`) makes
slots 13-34 from texti0a (13, copy 14), texti0b (15, 16), texti1 (17,
18; copies 21, 22), texti2 (19, 20; copies 23, 24), texti3 (25, 26;
copies 29, 30), texti4 (27, 28; copies 31, 32) and texti5 (33, 34), all
at the new-slot period $166, and chains them through the slot records'
next field: slot n plays slot n+2 after its repeats. Repeat counts:
13/14: 13 (14 plays), 15/16: 1, 17/18, 21/22, 25/26, 29/30: 2, 33/34: 3,
the rest 0 (one play); 33/34 go on with 17/18. `$109CE6` stops all four
channels and starts 13 on channel 0 and 14 on channel 1 at volume 63:
the same samples on both sides, the tune looping from texti1 for ever.
Its volume follows the driver's master volume (`$14520E`), which the
vertical-blank server (`$1092DE` → `$11954C`) moves by $4000 per blank
(16.16) towards the target `$13A958` while the tune plays (`$13AAC7` ≠
0): $3F at the title, $1F in the menu, 0 once a menu choice is made (a
fade of 2.6 s). Without the music samples the tune is text201's two
looping slots at volume 15 ($109D7A). The tune stops ($10201E, channels
0 and 1) when the chosen menu item starts.

**Credits** ($101962, $103596): menu palette (palette 0 at $119FCC),
block 15 typed, then any key ($1035C8; key flag `$13AA93`).

**Pilot.** After the key: palette 2 ($11A04C), the tune's target volume
$1F, the log disk check (`$108434`: `Info` on DF0:; no disk, not a DOS
disk, write protected → texts 68-70 and 94, Return retries), `config`
read (78 bytes, below). Countdown 5, then ($10370A) palette 0 and
either
- launches (config +4) = 0: block 1 ("WELCOME TO YOUR FIRST TOUR OF
  DUTY ... ENTER YOUR CALLSIGN"), then block 3 in the typing mode
  (`$13AAD0` = 2, at most 20 characters, `$13AAE5`): its first line is
  the input field; keys are buffered ($13AAD1, 10 raw codes) and echoed
  through the keymap `$127DCA` with a click each; Backspace (raw $41)
  takes the last one back; Return ends the field (`$10858A` writes
  `config`) and the block goes on ("YOU ARE NOW OFFICIALLY ENLISTED");
- else block 2, "WELCOME <call sign>" (the name copied into the text,
  config +$1E, at most 24 characters up to a zero), until a key or the
  block's end.
Then launches + 1 and the main menu.

**Main menu** ($101D30): palette 0, line spacing 12, block 6 and blocks
100-109. Keys ($10EACC, the key handler `$10DAC8`) set the choice
`$13AB96`: 1 → $7F (demo, only with the demo recording loaded), 2 → 1
(free flight), 3 → 2 (training demo), 4 → $7D (training practice), 5 →
9 (qualification), 6 → -1 (selectable missions list), 7 → the next
advanced mission (config +6 + 1, kept within 3-8; only when qualified,
config +0), 8 → -2 (flight log), F1-F6 → missions 3-8 (each needing its
flag at config +$14-$19; 3 always). A choice clicks (`$127D8A`). Then
($101E04) the screen is cleared and only the chosen item's line is
typed again (blocks 101-105, 107), the tune fades to 0 and after 210
frames (150 without the tune or once the security check was passed)
`$10201E` stops the tune and starts the item; free flight first starts
the wind at volume 8 (`$109F9A(8)`; without the wind sample the
outside engine, `$109E46($300, 8)`).

**Flight log** (8): block 87 with the numbers written into it
(`$1193EE`, right-aligned, no leading zeros): config +$36 missions
started, +$38 completed, +$3A/+$3C cannon rounds fired/hits, +$3E/+$40
AMRAAMs, +$42/+$44 Sidewinders, +$8 (long) flight time in seconds as
hours and minutes, +$46 crashes, +$48 hits from missiles. Keys: Esc
main menu, 1 write `config` (then, for a pilot with at most one launch,
back to the welcome), Shift 2 clears the whole record (`$108556`: call sign and launches
too, so writing it makes a new pilot).

**Free flight** ($102174): the map camera `$10D3D8` (position
$10800000, $3000000, $10C00000 in the world's 16.8 units, looking
straight down: view matrix rows ($A8, 0, 0), (0, 0, $FC), (0, -$80,
0)), five aircraft (`$13AB87`), then the map fades in ($13AB90 → 15:
the 16-colour palette at `$11A04C - (15 - step) × 32`, a step every
third vertical blank) and the security check runs (`$1027C8`: blocks
95/96 for missions, block 71 and the input field block 4; on this
cracked disk block 71 reads "CRACKED BY A-HA" and any answer passes;
the check is skipped once passed, `$13AB1B`). Block 7 ("SELECT YOUR FREE
FLIGHT STARTING LOCATION") at the bottom over the strip; the places of
`$137CF6` (16-byte records: the three runways by cell and offset, then
moving objects $800E and $800F, the carrier) are marked with their
digit at their projected position (`$11FB9E`, colour 9), the chosen one
blinking. A digit key chooses ($10EBB4, `$13AB38` = place - 1). For a
place other than 4 block 75 asks for the aircraft (1 F/A-18, `$13AB39`
= $11; 2 F-16, $10). Then the aircraft is set at the place ($10D51E)
and the outside camera flies from the map down to it (`$11DC90`…, the
camera chase of the outside views, `$13AB9E` = 6) before the cockpit
view starts; on the way block 89 ("READY FOR TAKEOFF", block 90 "READY
TO LAUNCH" on the carrier) is typed.

**Pilot record** (`config`, 78 bytes, big-endian; the disk's copy has
pilot "R" with one launch):

| Offset | Size | Meaning |
|---|---|---|
| +0 | word | qualified (missions open) |
| +4 | word | launches (0: a new pilot) |
| +6 | byte | the last advanced mission |
| +8 | long | flight time, seconds |
| +$14-$19 | bytes | missions 3-8 open (written per mission) |
| +$1E | 24 | call sign, zero-terminated |
| +$36-$48 | words | the flight log's numbers (above) |

`text/textply` (3,676 bytes) and `text/textctl` (14,704 = 4 × 3,676)
are not the log: they are the demo's recording (menu item 1, "The demo"
below), read into `$14508C` and `$145098` (`$108668`; a missing file is
created: the game can record, `$13AB3B` 1).

### Input

Devices: keyboard.device ($108CDC), timer.device ($108E28), gameport.device
($108E9C).

Keys: keyboard.device read requests; $108DA6 returns the raw code (with a
pending-key slot used by the game itself, $108D8A). Joystick read
directly from JOY1DAT ($10906C), fire from gameport.device events.

### Display

Low-res 320×200, 5 bitplanes, graphics.library View/ViewPort with its
own copper list; double-buffered flight view; cockpit picture inst5 at the
bottom in the cockpit view; external views in the demo.

### Main loop and timing (2026-10-28)

`$107D48` sets up, then loops forever: `$124158` (pick the back buffer's
draw lists), OwnBlitter, `$101124` (one frame: input, the flight and
every object, the 3D view, cockpit and HUD), DisownBlitter, `$10827C`
(WaitBOVP, LoadView: show the new buffer). One pass is one frame and one
simulation step; `$13ABCA` counts them (`$1014B0`, unless paused).

- **No time step.** Nothing in the rules scales by elapsed time: the
  elapsed milliseconds (`$119876`) feed only the frame pacer and a
  frame-rate readout (`$1199EE` → `$13ADD6` fps × 100, drawn by
  `$12677A`); there are no beam-position or CIA clock reads. Checked on
  the running game (`.analysis/fa_18_interceptor_amiga/harness/stepcheck.py`): in level flight the aircraft
  moved 170-174 units every frame whether the frame took 120, 140, 160 or
  180 ms.
- **A frame-rate cap.** `$119876` busy-waits until the time since the
  start of the current group of 8 frames reaches (frame in group + 1) ×
  the table entry `$119592[$13ABAE]` (1000/n ms). `$13ABAE` is set to 15
  (`$10D2EE`) and never changed: at most 15 frames (steps) per second.
- **Real seconds.** When timer.device's seconds change, `$119876`
  counts down a few counters (`$13AB74`, `$13AB75`, three via `$1199E6`,
  `$13AB79`) and every 10 s of accumulated time sets bit 6 of `$13ABBE`.
  Flight-log time and mission clocks read the seconds too (`$101790`…).
  Their roles are not traced yet.
- **Measured** (FS-UAE A500, `.analysis/fa_18_interceptor_amiga/harness/fps.py`): about 4 fps on the deck
  in the demo's close view, about 7 fps in flight; the game's own readout
  agrees. On a stock A500 the flight runs in slow motion compared with
  the cap; only faster machines reached 15.

**Port timing (decided 2026-10-28):** the rules tick at a fixed 15 Hz,
the original's own cap and what it does on a machine fast enough to
reach it; real-second counters count 15 ticks. Drawing is separate, at
the display's rate (up to 120 Hz), interpolating aircraft, objects and
camera between the last two ticks. The variable slow-down of a stock A500
is a hardware limit (no-hardware-failures rule) and is not reproduced.

### Smooth motion and widescreen (owner's plan)

The flight view is a filled-polygon 3D renderer driven by the aircraft and
object state, so a native renderer from game state can widen the field of
view and draw at 120 Hz with the simulation ticking at the original's rate
(state interpolation between ticks). The rules (flight model, AI, weapons)
stay at the original tick: 15 Hz, see "Main loop and timing".

## 3 Data formats

Addresses are in the research layout (program from $100000, this document).
Decoders: `src/fa_18_interceptor_amiga/data/assets.cpp`; research listing
`.analysis/fa_18_interceptor_amiga/tools/texts.py`; export `build/fa_18_interceptor_assets export`.

### Pictures (pix/)

IFF ILBM, 5 planes, ByteRun1, 32-colour CMAP, a GRAB chunk. Loaded by
$1001C8 (own ILBM reader, `_UnPackRow` in hunk 3). **Checked:** `splsh`
decoded natively equals the reference capture of the title screen
(`.analysis/fa_18_interceptor_amiga/work/ref/s1.png`, 0 of 128,000 pixels differ).

### Sounds (text/)

Raw signed 8-bit samples, no header; `$1458A6` reads a whole file into
chip memory and makes a sound slot (40 slots, pointers at $145BA4; slot
record 64 bytes: +0 data, +4 length (bit 31: shared copy), +8 period<<16,
+$C volume<<16 ($3F), +$10 repeat count (-1 loops), +$30 the slot's
effect script; full layout in this document). New slots get period $166 ($145784);
$14592C copies a slot. Loaders: $1099CA (flight sounds), $1096BA (music,
only with more than 200,000 bytes of chip memory free), $109660 (text201).

| File | Slot (copies) | Notes |
|---|---|---|
| textcpt | 0 (1) | slot 1 gets length $15CC and loops |
| textegn / textegn2 | 2 (3, 12) | textegn2 only when textegn is missing; slot 3 starts $370 in and loops |
| texttre | 5 | |
| textwnd | 8 (9) | slot 9 starts $792 in and loops |
| textger | 11 | loops |
| texti0a, texti0b, texti1–5 | 13–34 | the title tune: pieces chained slot to slot ("The front end") |
| text201 | 35 (36) | periods $B3 / $B5, loops |

Slot 4 ($20 bytes, a square wave) and 6 ($800 bytes of noise, copy 10)
are made at load ($145E20, $145EFE). Roles, traced (this document): textcpt
the engine heard in the cockpit, textegn with the afterburner, textwnd
the wind at idle, textger the gear, texttre the tyres, slot 6 the crash.
There is no music sequencer: Dave Warhol's title tune is the chain of
slots 13-34 (section 2, "The front end").

### Palettes ($119FCC, hunk 21)

Ten palettes of 32 colour words. Palette 0 is the menu's ($101D30 →
$103C1C(h21)); it equals the menu screen's copper colours.

### Fonts (hunk 61)

Both: a table of 64 word offsets (from the table's start) for characters
$20–$5F, then one byte per glyph row, leftmost pixel in bit 7.
- $1328B8: cockpit font, 5 rows, 3 pixels wide (drawer $127402: rows from
  d7 = $142 >> 6, mask $E0000000).
- $132A24: menu font, 7 rows ($127B58: d7 = $1C2 >> 6).
`$` and `^` hold the two halves of the ™ sign.

### Screen texts ($133FFE, hunk 64)

Run by $127920 (per frame, from the screen procedure). The game queues
block numbers in the list at $13AA3A (words, 0-terminated; bit 15: continue
at the current position, bit 14: speed 2). Block n's address is
$133FFE + word[n-1]. A block:

    row (scanline), column (byte, 8 pixels)
    line*: flags, colour << 4 | speed, text, 0, delay
    end:   a flags byte with bit 7 set ($127BCA)

- Character i of a line is drawn at column×8 + table[$13635A + 4i]: a byte
  offset and a blitter shift in the top nibble (7-pixel advance), four
  planes written with minterm $FA or $0A per colour bit ($127C5C).
- Lines advance by $13AA2E bytes: $1B8 (11 rows) normally, $1E0 (12 rows)
  for the main menu ($101DA4).
- delay × 4 frames pause after a line ($127836); speed counts frames per
  character (**hypothesis**: $13AACB); flags bit 0 changes the typing
  (keyboard echo for the call-sign entry, **hypothesis**).
- 110 table entries before the next table; 95 decode as blocks. Entries
  16–19, 25, 111–116 point at bytes that are not block starts (reached as
  continuations, **hypothesis**) and are skipped by the decoder.

**Checked:** the main menu built from blocks 6, 100–109 (palette 0,
spacing 12) equals the reference capture (`.analysis/fa_18_interceptor_amiga/work/ref/s2.png`, 0 pixels
differ). Which palette each briefing uses is not traced; previews use 0.

### Cockpit messages ($1321C8, hunk 59)

54 records of 28 bytes: an attribute byte, 26 characters, a zero. $127126
copies 27 bytes from record n + 1 into the message line ($13AAFA). The
attribute's meaning (priority or colour) is not traced.

### 3D shapes

Decoder: `src/fa_18_interceptor_amiga/data/shapes.cpp` (vehicle types
0-15); research tools `.analysis/fa_18_interceptor_amiga/tools/shapes.py` (walker, `extract()`),
`.analysis/fa_18_interceptor_amiga/tools/render.py` (painter's render), `.analysis/fa_18_interceptor_amiga/tools/builders.py` (symbolic run of
the vertex builders), harness `modelbp.py`, `opbp.py`, `pointcheck.py`.

**Object types** ($115F4C, 20-byte records, indexed by the object's type):
draw routine, shape base, detail list, alternate list ($13AD2A,
animation), extra. Routines: $1169C4 and $111C94 (vehicles), $116A1E /
$116ABE (parachutes, pick a list by state), $111C90 / $111D5C (scenery,
types 33-35 and 43-59), $112D5C / $112D6A (types 36-42, another format).
The caller ($10FD52, $10FBBC) passes the base in a0; every offset below
is relative to it.

**Detail levels** ($111D8C): entries `threshold | flags, link, model word`
then the draw list; with bit 14 an entry is `threshold, model word,
one draw-list word` and the next entry follows directly. An entry is used
while the distance (>> the scale shift $13ADA8) is below its threshold;
$FFFF or a negative word ends the list (nothing drawn: hidden when far).
Shapes have 1-4 levels; at distance 0 the first. The model word: low 12
bits the point block, bit 14 no clipping test, bit 13 special (scenery).
**Owner's planned enhancement:** an option to always take the nearest
level (and distance 0 in the polygon lists below), off by default.

**Point block** (model): +0 word (low byte an id), +2 and +4 two
distances, +6 scale nibbles, +7 flags (bit 0: rotated with the object,
cached at object+$A4; bit 1: 2-word points x, z; bit 3: generated points;
bit 4 seen on ships), +8 point count, +9 count drawn; points from +$A,
6 bytes each (x, y, z words; y up, z forward). Point i is always at
+$A + 6i, also the "sub-vertex" blocks loaded later by polygon lists, so
list operands (byte offsets into the transformed buffer $13D67C) are
point index × 6. **Checked:** the F/A-18's 21 points rotated by its
object matrix equal the game's own cache, 21 of 21 (`pointcheck.py`).

**Draw list** ($112650): nodes `v0, test, v1, v2, false, true` (a facing
test through three points; bit 13: an explicit plane, six words; bits
10-11: a comparison with the camera on one axis; test $FCxx: a jump),
branch offsets from the base. Leaves are negative words: bit 13 one
polygon list drawn last; otherwise a list of polygon-list references
(bit 15 on the last; bit 12: a long pointer), and bit 14 stops after it.
It orders the parts back to front; a depth-buffered viewer can draw every
branch.

**Polygon lists** ($11284E): opcode words `$8000 | offset into $113470`
(bit 14: the last). A positive word is a distance band: `T, $FFFF` (only
nearer than T) or `T, T2` (only between: at distance 0 the list ends),
or $7FFF; then `count, offset` loads more stored points (count > 0) and
an opcode word follows. Opcodes (78 entries):

| Op | Routine | Operands | What |
|---|---|---|---|
| $00 | $1135A8 | – | nothing |
| $04 | $1135AC | point, colour | a dot |
| $08 / $C0 / $128 | $11373C / $113724 / $11372C | p1, p2, colour | a line (two drawers; $128 near only) |
| $0C | $1137E4 | points up to one with bit 15, colour word | a face |
| $114 | $11378A | three points, colour word | a triangle |
| $10 | $11392E | altitude, records `n, flag, n points`, negative end | the ground shadow, black |
| $34 | $114A34 | colour, point pairs (bit 15 on the last) | lines |
| $DC | $113888 | long pointer, offsets (negative end) | faces kept in a shared table |
| $58 | $1144F0 | colour, point, n, n counts | rows of parallel lines |
| $80 / $50 | $1141C8 / $1141DA | (colour), point, count | lines across a quad (deck markings, hypothesis) |
| $F8 | $1157C8 | point, colour, radius | a filled disc |
| $C4 | $1136CE | three points, colour | a dot seen from one side (a light, hypothesis) |
| $CC | $1150C4 | byte count | skip unless the object's flag bit 3 |
| $10C | $113692 | three points, test | skip 18 bytes when facing away |
| $110 / $120 | $11367A / $11366C | – | frames of 52 / 18 bytes follow, frame = $13ABCA & 15 |
| $134 | $1150EA | – | 18-byte variants by $13AB37 |
| $118 | $1135F0 | – | variants by the object's state (scenery) |
| $98 $A8 $AC $E0 $EC $FC $100 $104 | see shapes.cpp | 0 or 1 word | vertex builders |
| $84 $C8 $D8 $108 $130 | | 0 / 0 / 0 / 0 / 2 | state flags, skip |

Colour word: low 5 bits the palette index (palette 2 in daylight). A
negative colour word is used as is; bit 14 marks a two-sided face (back
colour in this word, front colour in the next); otherwise the face is
one-sided and the word also selects its facing test (bits 7-9 a precision
shift, bits 10-13 the test kind): seen from behind, the rest of the list
is skipped.

**Vertex builders**: per-model routines that complete points from
others into fixed slots, for the object cache and the camera buffer
(operand: the buffer offset). Equations from `.analysis/fa_18_interceptor_amiga/tools/builders.py`, written
out in shapes.cpp. They compute in 16-bit words and round down at each
halving; the decoder does not round (open item for the game's renderer).

**Shapes decoded** (nearest level; faces / lines): 0 missile 9/0,
1 missile 11/0, 2 pilot under a parachute 145/12, 3 single-fin jet light
14/7, 4 F/A-18 25/8 (the demo's aircraft, model $129D4C), 5 four-engined
airliner 30/10, 6 single-fin jet dark 14/5, 8 carrier (hull 65) 28/46,
10 cruise missile 7/4, 11 pod under a parachute 149/8, 12 submarine 20/13.
Types 7, 9, 13-15 repeat earlier shapes. Names other than the F/A-18 are
hypotheses (looks, mission texts). The C++ decoder and the research
decoder agree on every count.

**Scenery shapes** (research decoder `.analysis/fa_18_interceptor_amiga/tools/shapes.py` `extract()`;
not in the C++ decoder yet): the object types drawn by `$111C90` /
`$111D5C` / `$111C84` (33-35, 43-97, 102-108, 133) use the same lists
with more model flags and opcodes:
- flag 2: points are x, z words (4 bytes) on the object's level
  (`$112292`); sub-point blocks then use data offset = buffer offset / 1.5.
- flag 8: generated points (`$1124CC`): chains `n, start point, n ×
  (point, steps)`, each segment subdivided into `steps` points, filling
  every other slot after the main points; a word then starts a second
  chain from the last point, back through the gaps (bit 15: one slot
  back, else three), so neighbouring slots pair two edges; a positive
  word repeats with a new pair. Used by the bridges' cables and decks.
- flag 4 / 0x40 / 0x10: seen, roles not traced (no effect on geometry
  so far). A model word with bit 13 is drawn only away from the ground
  mode ($111DD2).
- Explicit-plane test nodes have their branch offsets at +16 (six words
  after the test word, `$1126B8`).

| Op | Routine | Operands | What |
|---|---|---|---|
| $24 / $78 | $114C14 / $1145EA | colour, point | parallelogram P0, P1, P2, P2 − (P1 − P0) ($24 near only) |
| $28 | $114C84 | colour, point | parallelogram at P3 with edges P1 − P0, P2 − P1 |
| $44 / $48 | $1147E4 | 4-point records, negative end | quads, colour 12 dithered |
| $4C | $11408C | colour, point, n | 2n parallel lines across a quad |
| $50 / $80 | $1141DA / $1141C8 | (colour), point, n | 2n filled stripes (runway markings) |
| $54 / $E4 | $1143C0 / $1143AA | (colour), point, n, n counts | rows of filled stripes |
| $2C | $114960 | count << 8 \| colour, point | lines between point pairs (bridge hangers) |
| $30 | $1149AE | count << 8 \| colour, ref, point | the same, moved by P[ref+1] − P[ref] |
| $38 | $114AA0 | colour, ref, pairs | lines moved by P[ref+1] − P[ref] |
| $3C / $40 | $114B22 / $114B96 | colour, ref, 3 / 2 points | quads with one edge moved by the ref difference |
| $1C / $20 | $114D22 / $114E0E | colour, (ref), two model points, apex, point | one of two triangles, by the camera's side of a line |
| $7C | $113FB2 | model point | orientation set-up, draws nothing (hypothesis) |
| $88 $8C $90 $94 $A4 | see shapes.py | point (, shift) | vertex builders (parallelograms; shift scales a difference) |
| $124 | $11365C | – | skips a word for type 87 only |
| $18 | $113E6C | variable | curves (types 107/108); not decoded |
| $118 | $1135F0 | – | variants by object state (types 33-35); not decoded |

62 of the 70 scenery types decode (`.analysis/fa_18_interceptor_amiga/work/sc*.png`): runway centre lines
and ends with numbers and threshold stripes (43-45), the Transamerica
Pyramid (62), Sutro Tower (65), suspension-bridge spans, towers and
causeway sections (48-57), buildings. Names are hypotheses from their
look.

The C++ decoder (`read_shapes`) covers vehicles, scenery and ground
shapes and agrees with the research decoder on every type's face and
line counts. Dots (`$04`) are kept; faces record their facing rule
(always, one-sided, two-sided with a back colour). Not decoded yet:
`$18`, `$118`; discs, lights and shadows; the drawing order and the
original's rounding.

### World

Decoders: `games/fa_18_interceptor_amiga/data/world.cpp` (maps, water
grid), `data/shapes.cpp` (all shape families); research tools
`.analysis/fa_18_interceptor_amiga/tools/worldmap.py`, `.analysis/fa_18_interceptor_amiga/tools/worldplot.py`, `.analysis/fa_18_interceptor_amiga/tools/terrain.py`; checks
`.analysis/fa_18_interceptor_amiga/harness/listcheck.py`, `.analysis/fa_18_interceptor_amiga/harness/abscheck.py`, `.analysis/fa_18_interceptor_amiga/harness/viewgrab.py` with
`fa_18_interceptor_assets viewcheck`.

**Units and axes.** World units are the scene builder's: a position long
/ 256. A coarse cell is 65536 (32 per side), a fine cell 16384 (128 per
side). y points up (the camera matrix's y row maps +y to the screen's
up); +x points west (the projection puts camera +x on the left); z runs
along the maps' rows.

**Scene builder** `$11005C`: per frame it walks precomputed lists of cell
offsets around the camera (by heading octant, tables `$136464`-`$1367A4`,
pairs `$1106B4`), gathers each cell's objects into slots and then the
visible-object lists (`$143C92`, `$144322`, `$1449B2`; 24-byte entries:
word type << 8 | flags with the precision shift in the low nibble, long
type record, x, y, z words relative to the camera's fine cell corner and
shifted, long info, …; the lists start at the offsets in `$13AC9C`,
`$13AC9E`, `$13ACA0`). The camera cell runs 0-30 (coarse) or 0-127
(fine) and wraps to the other end past them (`$1102C0`).

**Three object maps** (hunk 66): `$137584` and `$137684` (the two coarse
passes `$10FA64` / `$10FA76`, `$13AB55`) and `$137784` (fine, near the
ground, `$13AB54`). 128 word offsets, **one per z row** (the builder
passes the z cell as the row, `$1102C0`); a row holds 2n, n sorted **x**
cells (binary search `$110434`), then n long pointers to object lists:
`[position code << 4 | slot] [type | bit 7]` then x, z words (0-4095)
unless coded (`$110806`); `$FF` ends. **The stored type is the object type − 16** (the visible lists hold map type + 16; 2026-10-28, "The view in the cockpit"): types 16-31 are the moving objects' own records. Position = cell × 16384 + (3 −
slot % 4, 3 − slot / 4) × 4096 + (x, z), all × 4 in the coarse maps.
Contents: map A 36 cells, map B 8 dense cells (types 89-116: city
blocks), map C 158 cells (types 45/74 every three cells, and more).
**Checked:** every map object in a snapshot of the three visible lists
(30 + 19 + 3) at its decoded position relative to the camera's fine
cell corner, exactly (`listcheck.py`, `abscheck.py`).

**Water** (`$11F27C`, colour 6 over land, which is the colour-4 ground):
a fine grid at `$138160` (32 × 32 words, rows z, 64 bytes apart; one
cell per coarse map cell) and a coarse one at `$137F9C` (8 × 8 words,
row by row: cells of 2^20 world units, 2^28 in 32-bit units, the world's
corner at cell (4, 4); points in 1/4096 of a cell, reaching past its
edges; each cell's long points at its own polygons). `$11F27C` draws,
when the camera is above height `$3F8` (32-bit units, the zoom applied
below `$7FFF0`), first the coarse cells of a window around the camera's
coarse cell (`$11E712`: 16 lists by the camera's quarter of its cell,
indices into the offset pairs `$11E6E0`; four cells from `$11E812` below
height `$5000`), then the fine grid's cells: above `$10000` the 5 × 5
around the camera (`$11E8A2`), above `$9000` 3 × 3 (`$11E852`), lower
lists by the heading (`$11F1BC`… with `$13AB40`, not rebuilt). A positive
word is the offset of the cell's data from the grid base; data starting
with a negative word means no water. Cell data: a long (a simpler
version for far cells, `$11F6E0`; negative: none), then polygons `count,
count × (x, z)` in 1/4096 of a cell (`$11F6CE`), until `$FFFF`; a word
with bit 15 before a count is a height threshold (× 4): that polygon and
the rest of the cell only from that height up (`$11F73C`). 1024 cells,
the near versions draw the bay's coastline (`.analysis/fa_18_interceptor_amiga/work/terrain.png`, the
viewer's world map).

**Ground shapes** (`$112D76`, types 36-42 and 98-132): `threshold, next
list (long), point block (long)`, a polygon list; points from +6
(count, 4 bytes, then x, z), sub-points at +6 + offset (index offset /
4); bands as elsewhere but a positive second value skips one more word
(`$112F18`); ops `$60` `$64` `$68` (colour 7). City blocks with parks,
islands. A type's lists are chained by distance: a list's first word is
how near (× 16 world units, `$7FFF` any distance) the object must be,
the long after it the next list for farther away (`$112F04`; negative:
nothing). City blocks: `$4D0`, then `$C00`, then nothing; the downtown
types 39-42, 99 and 100 are `$7FFF`. Their lists use the scenery's opcode
table (`$113470`, indexed by the opcode as a byte offset) beyond `$60`
`$64` `$68`: `$24`, `$78` and `$6C` a parallelogram P0 P1 P2 (colour,
point offset; `$24` only with the camera low, `$13AD68` ≥ −$80; `$6C`
with other clip flags, `$1145C8`), `$34` lines sharing a colour (colour,
then pairs of point offsets, bit 15 on the last), `$12C` the same lines
only with the camera above −$C0 of `$13AD68` (`$114A20`). Partly known:
`$5C` (`$11486A`: a point offset and a count; with P0-P3 there, V =
P0 − P3 + 1.5 (P1 − P3) and W = P2 − P3, then `count` parallelograms
(Pk, Pk + V, Pk + W + V, Pk + W) from P3 on, in the colour left by the
op before: rows of building fronts, not rebuilt), `$9C` (a long and a word, `$113B58`), `$08` and `$128` in
ground lists.

**The airfields' paving** is the runway types' own flat models (runway 1:
type 46, model `$12C062`: scale 2^−2, flags 6, two main points), not a
ground shape. Flag 4 of a flat model (`$1123A6` → `$112C36`) builds
strips after the main points: blocks of a count `n` (negative: the end)
and `n` points, each followed by itself plus the vector between the two
points before the block (so every strip is as wide as the first); after
each block a word (negative: the end) and two points taken as they are.
Runway 1: 2 + 6 + 2 + 6 = 16 points, drawn by one `$44` quads op. (×¼
the points are the runway table's corners, `$137D8A`.) The same flag
makes a flat object skip itself while the camera is low (`$13AD68` ≥
−$20) beyond `$170` (else `$60`), shifted by the zoom (`$1122A4`): not
rebuilt.

**Ground shapes at height:** their draw routines return at once above a
ceiling: `$112D5C` above $380000 (32-bit units, 14,336 world units),
`$112D6A` above $800000 (32,768): no city blocks on the start map.

**The view** (`$123894`): camera space = the matrix `$13AEC8` (rows
x, y, z, 1.0 = 256, rows scaled by the field of view: about 167, 251,
129) × (point − camera); screen x = 159 − 160 x / z, y = 90 − 90 y / z,
a 320 × 180 window clipped at row `$13AC74` (167 in the cockpit view).
The camera is the fine cell corner minus `$13AD52` / 256 (`$13AD52`,
`$13AD56`, `$13AD5A`: minus the offset from the corner × 256). Model
points are scaled by 2^−(low nibble of model byte +6) (`$111E68`).
One-sided faces are drawn only when (v1 − v0) × (v2 − v0) · v0 < 0 in
camera space (`$112B1C`).

**Native view checked** (`viewcheck`, 12 frames of the demo,
`.analysis/fa_18_interceptor_amiga/work/ref/views/sheet.png`): horizon, sky, land and water, the coastline,
the runway markings and towers in the right places while flying.
Differences, open: moving objects (aircraft, carrier) are not drawn by
the check; it ignores detail levels and distance limits (as the planned
"no LOD" option), and draws the white specks on the water nowhere (their
source is not traced); type 58 (a dark-green four-faced hill, model
flag `$10`, like types 86 and 104) sits at the carrier's starting point
and the original does not show it in these frames.

## 4 Free flight and the flight model

Research notes (2026-10-28), from this ROM and runs of it only.

### Free flight from the menu

Main menu key 2 → map of the bay "SELECT YOUR FREE FLIGHT STARTING
LOCATION" with places 1-4 (key; 4 is at sea, the carrier) → "SELECT
1 ... F/A-18 HORNET 2 .... F-16 FALCON" → cockpit view on the runway
(place 1: heading $8C0 = 28°, throttle 9, brakes off, gear down). The
screens in between: "The front end" (section 2).
Captures `.analysis/fa_18_interceptor_amiga/work/ref/ff_select.png`, `ff_aircraft.png`, `ff_runway.png`.

### Objects

16 records of $200 bytes at $13B474; the player's is $13ABCE (byte offset,
0 here), slot index $13ABCC. Fields found so far (word unless noted):

| Off | Meaning |
|---|---|
| +0 | flags: bit 4 of byte +0 = moves; $800 speed brake (key `\` / backspace toggles); $200 crashed; $400 hit; $8000 ... |
| +2 | flags: $80 on the ground; $40 nose wheel down (set by $105BDE, cleared each frame by $10557C); $2000 full military power reached; $08 afterburner; $400 reverse thrust armed; $10 auto-level roll; $8000 the arresting hook down (key A), $4000 in the carrier's wire zone (both = full braking); $02 near another object (`$11AAE0`) |
| +4 (byte) | bit 3 no rudder/roll input; bits 6-7 over a deck (ground height + word +$4E × 256) |
| +6, +8 | coarse cell x, z (position >> 22) |
| +C, +E | fine position x, z ((pos & $3FFFFF) >> 8); +$10 long: y >> 8 |
| +$14/+$18/+$1C (long) | position x, y, z (world units × 256; y up, $708 = on the gear) |
| +$20 (byte) | bit 0 ground handling, bit 1 stalled-out spin, bit 2 stall / half rates |
| +$22, +$24 | sine and cosine of the folded bank ($122526) |
| +$26 | ground drag / stall drop (stall: nose drop per tick) |
| +$28, +$29, +$2A (signed bytes) | pitch, rudder, roll input (ramps ±1 to ±20, ±1 to ±20, ±3 to ±60) |
| +$2B (signed byte) | throttle 0-120 (120 = military power; afterburner = flag +2 $08); negative = reverse |
| +$39 (byte) | low nibble: throttle-step counter |
| +$3E/+$42/+$46 (long) | velocity added to the position each tick (x, y, z) |
| +$50, +$52, +$54 | bank-turn state ($122526) |
| +$56, +$58, +$5A | pitch, yaw, roll rates (1/80 degree per tick) |
| +$62 (byte) | type ($11 F/A-18 here; family = high nibble, $10 aircraft) |
| +$65 (byte) | controls: $10 up, $20 down, $08 left, $04 right, $40/$80 rudder, bits 0-1 throttle up (1) / down (2) |
| +$66, +$68, +$6A | pitch, heading, roll: 0-$707F = 0-360° (80 per degree); nose up = just below $7080 |
| +$6C | speed (engine); +$6E = airspeed = +$6C + +$78 |
| +$72 (long) | fuel (×256 lb; 25000 lb = $61A800) |
| +$76 | sink from the airspeed table $11AB88 |
| +$78 | speed change from the climb angle ($11AC14) |
| +$7C (byte) | high nibble gear (0 down … $60 up, bit 7 = handle up, key G toggles); low nibble another 1-6 animation |
| +$7E | bank-turn limit |
| +$80 | body matrix (9 words, $4000 = 1) from pitch, heading, roll ($122C52) |
| +$92 | view matrix from the negated angles ($122CEC); +$96/+$9C/+$A2 = forward |

### The flight step (per frame, player)

`$101124` → `$10F58E` → `h17` ($116B80) → per object `$11A352`.
For the player:

1. `$10DFD2` input: control byte → input ramps +$28/+$29/+$2A; throttle
   target `$13AB60` (F1-F10 = 12, 24 … 120, F10 → 121) vs +$2B sets the
   throttle bits of +$65 (equal: clear and forget the target; more than
   8 below: down; above by 9 or more, or the step counter < 2: up).
2. `$121BE0` (h32, asm): for family $10: `$10557C` (C) turns the inputs
   into rates +$56/+$58/+$5A through the response table `$1327B8` (player;
   others `$132834`), with ground handling `$105BDE` (nose) and `$105CAA`
   (wings level), smoothing `$105CF0/$105D5A/$105DB4`, decay `$105B7A`, a
   speed boost of fast rates `$105E2E`. Then (when `$13AA7F`) the bank
   turn `$122526` adds pitch and yaw from the bank; `$1226B8` rotates the
   body matrix by the rates (delta matrix × matrix, 2^28 longs) and reads
   pitch, heading, roll back with the arcsine tables `$13308A`, `$133290`,
   `$1336E0` (sine `$1338E0`, 901 words, tenths of a degree); stall model
   (`$13AA74`), near-vertical fix, store, rebuild +$80 and +$92.
3. `$105ED4` (C, via `$11AC14` at its end): throttle steps and the speed
   model (targets by throttle, altitude/attitude drag `$106A32`, gear,
   speed brake, afterburner fuel), fuel burn.
4. `$106B0E` (C): velocity = previous + (airspeed along forward − previous)/4
   (`$119CB8` scales a vector, `$1108C4` its length), sink added, ground
   contact (gear $708, belly $2EE, type $14 $A8C, sea −$200), take-off and
   touch-down (vertical speed < $240), y.
5. `$11A352` tail: x, z += velocity (clamped 0-$1FFFFFFF), cells, gear
   and second animation in +$7C, collisions `$11B6A2`.

Recording: `.analysis/fa_18_interceptor_amiga/harness/flightcap.py` (one JSON line per frame: player object,
globals $13A9E0-$13AF80). `.analysis/fa_18_interceptor_amiga/work/ref/fc1.jsonl`: take-off at full throttle,
pull up, roll left, pull up (160 frames). Free-flight flags: `$13AA74` 1,
`$13AA7F` 1, `$13AA7C` 1, `$13ABBC` $0840, ceiling `$13A9EA` $800000.

### Ground contact and runways ($11F83A)

On touching the ground (and every few ticks rolling) the wheels' point is
tested: first against the water grid's polygons (`$138160`, even-odd
crossings; inside: +4 bit 1, and with the gear down the gear handle goes up
and +$20 bit 0 is set), then against the runway list `$137D8A` (16-byte
records: map type × 20, coarse x, z, slot, x, z offsets in coarse-map
units; `$FFFF` ends): each runway's footprint quads hang off the type
record's alternate list (`$11608C` + map type × 20 + $10, i.e. the record of
type map type + 16); inside a quad (every edge's cross product ≥ 0): +4 bit
2 (hard surface: full ground thrust and reverse limit), else cleared.

### Free-flight start ($10D51E)

Start table `$137CF6`, 16 bytes per place: coarse x, coarse z, slot (pair at
`$110732`), x and z (× $400), fine x and z (× $10), heading in degrees;
places 1-3 the runways, 4-5 `$800E`/`$800F` = on object slot 14/15 (the
carrier). y = $708, throttle 9 (`$13B49F`), fuel 6,400,000 (25,000 lb), state
$11C8, bank-turn limit $1400 (`$10D8D8`), type $11 (F/A-18; $10 F-16).
Checked: place 1 gives exactly the recorded position $10545920/$10A404F0.

### Cockpit readouts

Height (`$126C16`): (y >> 10) × 5 ft. Speed (`$126B48`): |airspeed| / 12
kts. Fuel (`$126D26`): fuel >> 8 lb. Heading (`$126AB2`): (heading >> 3)/10°.
Thrust: throttle × 100 / 120 %. Positions (measured on `ff_runway.png`):
glare shield `pix/frnt5` at (16, 133) (colour 0 transparent), panel
`pix/inst5` at (0, 145); HUD glass top edge row 37 (93-224, colour $A),
bevels colour 8, posts x 85 and 233 rows 45-144 (colour 0); digits of the
5-row font, 4 px apart, right-aligned with the last digit at x 262 (ft, row
158), 256 (kts, 166), 248 (thrust, 175), 243 (zoom, 184), fuel 104-120 row
183 (black on colour 8), radar range 148-152 row 184.

### The panel and head-up display drawers (2026-10-29)

Each frame `$101124` calls the panel drawers, several only every 2nd,
4th, 8th or 16th frame (`$13ABCA` low bits) or for a few frames after a
change (counters `$13AB26`-`$13AB35`): `$125360`, `$1255B2`, `$125E22`
(radar), `$127EB8` (head-up display), `$125D26`, `$125B74` (compass
strip), `$126AB2`, `$126B48`, `$126D74`, `$126C16`, `$126D26`,
`$125514` (fuel bar), `$124C3A`, `$1274A4` (weapon readout: SW / AM /
GUN and ARM / NO from +$5F, +$60, +$63), `$1255FC` (stores display),
`$126DCE` / `$126E5C` (latitude, longitude), `$1266C8` (zoom), `$125930`
(attitude ball), `$126660` (radar range), `$126EEA`, `$125758`. Numbers
go through `$119F6C` (to decimal) and `$1276A0` / `$127390` with glyph
position tables (pairs: byte offset, pixel shift in the top nibble; the
height's `$127E6C` leaves a gap after the thousands).

- Latitude ($126DCE): ((z − $10000000) >> 8) / $7000 + 375 tenths of a
  degree north; longitude ($126E5C): ((x − $0F000000) >> 8) / $5999 +
  1220 tenths west; black digits on the blue boxes (rows 180 and 188,
  cells 292, 295, 299, point 302, tenth 305).
- Thrust ($126D74): throttle × 256 / $133 %. Radar range ($126660): from
  +$63's low nibble, 9 → 2 mi, $B → 10, else 40. Zoom ($1266C8): $13AD32
  $80 → 1.0, $40 → 2.0, else 4.0.
- Fuel bar ($125514): `$13ABE6` & $7FFF >> 10 lit rows of 22, columns
  112 and 114 from row 180 up, colour 4 (unlit 0).
- Head-up display ($127EB8, cockpit view only, readouts once `$13AA91`
  is set as the flight starts; colour 13). Characters go where the
  original's position tables put them (`$1276A0`: pairs of a byte offset
  and a blitter shift, from the field's start column; checked pixel for
  pixel with `hudcheck` against both aircraft's screens, 2026-10-29):
  - F/A-18 ($11): height (y >> 10) × 5 ft in six digits (up to five
    leading zeros blank) from column 192 row 84 (`$127E6C`: +12, 16, 20,
    26, 30, 34), "FT" from 208 row 91 (`$127E88`: +14, 18); speed +$6E /
    12 kt (0 when crashed) in four digits from 80 row 84 (`$127E90`: +12,
    18, 22, 26), "KT" from 96 row 91 (`$127EA4`: +6, 10).
  - F-16 ($10): tapes (`$128016` height in tens of feet on the right,
    `$128240` knots on the left). A label every 50 (500 ft, 50 kt): the
    value's last two digits up to 20 round down, 21-70 to 50, above 70 up
    (`$13AE12` in BCD); the label's row is 91 + (last two digits − its
    own) / 3, the text 2 rows above it; more labels 15 rows apart from
    row 70 to 110 (+5 up, −5 down, not below 0). Heights: three digits
    (zeros kept) from 208 (`$127E54`: +7, 11, 17) with the point a pixel
    at (223, row + 2); knots: three digits (two zeros blank) from 80
    (`$127E60`: +12, 16, 20). Dots every 3 rows on columns 212 / 105
    between rows 72 and 111, two pixels wide (213 / 104-105) at the
    labels' rows; the indexes are five pixels on row 91 from 206 / 107;
    a bracket by the speed's index at (109-110, 86), (108, 87-88),
    (109-110, 89). No "FT" / "KT".
  - Load factor (`$126960`): (pitch rate + pitch rate / 8 + the bank
    turn's share `$13AC36` when beyond ±1) / 8, minus 10 (plus 10 when
    inverted, `$13ABBA` bit 1: `$1043F2` sets it when exactly one of the
    view's pitch and roll is between 90° and 270°); its size in tenths in
    two digits (one leading zero blank) after "-" (from 0 up) or a blank;
    the point a pixel. F/A-18: from 80 row 121 (`$126508`: +12, 16, 22),
    point (100, 125), "G" at 108 (`$126514`); F-16: from 96 row 61
    (`$1264FC`: +10, 14, 21), point (114, 65), no "G".
  - Heading tape (both, `$1283F8`): the heading in tenths of a degree;
    the nearest label (tens of degrees, two digits, zeros kept; from 4.1°
    past a ten the next one) sits −(tenths past it) / 5 pixels from 159;
    labels 20 pixels apart within ±22 on row 53, their characters from
    `$128612` (8 bytes an offset, two characters) from column 128. Ticks
    (`$128C62`) on row 61 at the label's column and every 10 pixels
    between 133 and 185, also on row 60 at the label and every second
    one. The index at 159 rows 63-65.
  - The marks of `$128D42`: the waterline marks on row 91 (x 141-145 and
    173-177, colour $A). (The port had these one row up and the
    F/A-18's speed and height a few pixels off until the tables were
    read.)
- The compass strip ($125B74): chip hunk 62 at `$132D9C`, 7 rows of 12
  bytes (96 px): a blank row, "W N E S W N E S" (a letter every 12 px,
  dots between them on the third glyph row), a blank row: 7.5° a pixel.
  Blitted into the window at x 192-225, rows 150-157 (glyph colour 2 on
  colour 3), scrolled by the heading; the red index (colour 1) at x 208
  is the panel's. Port: strip column = (x − 207 + heading80 / 600 − 36)
  mod 48 (checked against `ff_runway.png` and `cap/009.png`).
- The gear symbol ($125930's second part): one of four 32 × 7 images by
  (+$7C & $7F) >> 5 (table `$12591E`: `$132D0C` down, `$132CF0`,
  `$132CD4`, `$132CB8` up), black, at row 189 of the GEAR box. The
  first part (`$1C70`) lights the box (another blit while the gear-speed
  warning `$13AB36` blinks): not rebuilt.
- The ball ($124C3A) is the panel picture's: the routine only blits the
  ball area back and draws the aircraft mark (204-214, row 172, colour
  2) and scale dots (colour 13) when the zoom (`$13AC76`) allows; the
  horizon does not move with the attitude.
- The radar ($125E22): the scan's edges from `$125E86` ((154,163) to
  (139,148), (162,163) to (177,148), colour 3) every 16th frame, the own
  mark ($125F0E: (158,167), (157-159,168), colour 12), then the contacts
  from `$143A08` (other objects).
- `$1289C4`, `$128734`, `$128ECC`: the weapons' aiming marks and target
  box (task 8); the head-up display has no pitch ladder.
- The panel is a view port of its own (rows 145-199) with `pix/inst5`'s
  colour map: palette 2's first 16 colours twice (9 and 11 swapped is
  the picture's own order). The flight palette's colours 16-31 are a
  darker copy, so the panel's pixels (colours 16-31) show as 0-15.

### Warnings (2026-10-29)

`$103D4C` picks the message for the first warning that is on, in this
order: crash imminent ($13ABB8 bit 15, $9C06), (missile alerts from
$13AB5E bits 4 and 5: $8010, $8012), fuel exhausted ($C004), stall
($C001), fuel critical ($8002, blinking with "LOW FUEL -"), (cruise
missile $8024, enemy near $800E), low fuel ($0C09), (friendly near
$400D). The fuel warnings come from the flight step (`$1055F8`, while the
flight runs): fuel at 0 → exhausted (bit 6 of `$13AE43`, the afterburner
goes out); below $70800 (1,800 lb) critical (bit 1); below $BB800
(3,000 lb) low (bit 0).

Load factor (`$117128`, every frame): |(pitch rate + pitch rate / 8 +
`$13AC36`) / 8| ≥ $4E (7.8 G) beeps once (`$127D6A(8)`, latch
`$13AB78`). Every real second (`$119954`) the overload counter `$13AB79`
goes up (at most 15) while latched, else down to −1; above 5 the
engine's volume is halved (`$1054A8`) and the screen procedure asks for
the palette step `$13AB91` = 2 (`$10317A`, otherwise 15). The palette
steps (`$1092DE`: `$13AB90` towards `$13AB91`, LoadRGB4 of 16 colours
from `$11A04C − (15 − step) × 32`) run through palette 1's orange and
then program bytes; on the running original no change of the flight
view's colours was seen (`screen.py` captures with `$13AB79` poked to
15 while `$13AB90` stepped down): **hypothesis** they reach a view port
not shown in flight. Not rebuilt: the visual blackout. No code posts "AT MAXIMUM ALTITUDE" (record 8): the
ceiling `$13A9EA` only limits the climb (`$106ABE`).

Collision proximity ($D00A with the crash beep, `$11A50E`) is posted when
the collision test (`$11B6A2` → `$11B6AA`) marks motion bit 0: that test
compares the player with the other objects' records (boxes of $5000 to
$10000 around them, the carrier's deck polygons from `$12E298` /
`$12EF74`) only. **No scenery collisions:** besides the scene builder
(`$10F35C`, `$11005C`) no code reads the scenery maps (`$137584`,
`$137684`, `$137784`); the only collisions with the world are the
ground and water contacts of the flight step (rebuilt). The damage
routine (`$11A8xx`-`$11AA6A`) adds 1 to the flight log's crashes (config
+$46) on a crash.

### Outside views (2026-10-29)

Keypad Enter (raw $43, `$10E3B8`) asks for the switch (`$13ABB6` bit 1);
`$1041E8` then turns the outside camera on with `$13AA75` = `$13AB23`
(−1 after the key) and `$13AC76` = $32, or off again. Entering it puts
the camera in front of the aircraft (`$13ABA2` = 4). `$13AA75` is the
distance: −1, 1, 2, 3, 4 (keys `[` / `]`, raw $1A / $1B, `$10E5AE` /
`$10E634` step it, skipping 0); `$13ABA2` the position (keypad keys,
`$10E4D4`…: 0 behind, 1-7 round the aircraft, 8 above, 9 below);
`$13AA97` the cockpit's look direction (0-11) when not outside.

The chase (`$11D8F2`): the position's offset from `$11D734` (`$11D770`
for the F/A-18, `$11D7AC` for type $14; words x, y, z, 4 world units
each), as it is at distance −1, 1.5 times at 1, then 2, 4, 8 times;
turned by the aircraft's view matrix (`$10D486`) for positions 0 and 4,
by a level matrix (`$13AEFE`, `$10D460`) for the others; at least 7
units above the ground or deck (`$11D9AC`: the camera takes that place
at once each frame). Its orientation has angles of its own (`$13AD84`
pitch, looking down from 0 to $1C20; `$13AD86` heading): `$10454A` turns
them each frame a quarter of the way towards the aircraft, at most $7D0
(80ths of a degree), $230 during the descent below, at once (rate −1)
when the view position changed (`$12222C`) or on the first frame.
Checked on the running original (`harness/camstate.py`): position 4 at
distance −1 on runway 1 put the camera (0, 12, −72) from the aircraft
turned by its heading, the view matrix the look-at's (rows ×$A8, $FC,
$80).

**The descent from the start map** (`$102C6E` → `$102D58` with
`$13AB9E` = 6, `$13AAA6` = 0): the camera starts at the map's place,
looking straight down, aimed at a point 23,040 units behind the aircraft
(`$10D44A`: offset (0, 0, $E980) turned by its view matrix, products
over 16). Each frame (`$11D9B8`) the distance d = the largest of |dx|,
|dy|, |dz| (32-bit) to the target picks, by state (table `$11D710`):
- 0: exponent $D above $1C00000, $B above $D00000, else state 1 and a
  way point (`$11DBEE`: m = (c + p)/2 of camera and aircraft, n = ((m +
  c)/2 in x and z, m/2 in y), o = (n + m)/2, q = (o + m)/2, target (q +
  o)/2);
- 1: $B above $200000, $A above $60000, else state 2 and a way point;
- 2: 9 above $60000, 7 above $30000 (a new way point each frame), else
  state 3;
- 3: the aircraft itself as target; 7 above $60000, 5 above $D000, 3
  above $8000, else state 4 and the near offset (6, 4, 3) for positions
  1 and 2, else (−6, 4, −9), turned as above;
- 4: 2 above $2800, else state 5 and the offset (±6, 1, 6);
- 5: 1.
At d ≤ $240 (any state) state 6: five frames (`$13AB95`), then the view
switches to the cockpit (`$13ABB6` bit 1) and `$13AB9E` = 0; `$102D58`
waits five frames more and types "READY FOR TAKEOFF". The move
(`$11DD2C`): the difference shifted down by 4 until below $4800, scaled
to length 512 (`$119CAE`), shifted up by the exponent; the velocity
`$13AF3A` goes half way to it (or takes it when 0); the camera adds it.
No numbers under the view meanwhile. Checked against the running
original (`work/ref/descent/q070-q100.png` against the port at the same
frames: the same path and view); about 80 frames, 5 s.

The aircraft is drawn with its vehicle shape: type 4 for the F/A-18
(the object's +$62 = $11 is not the shape index: `$10D58C` takes the
type record at offset $50 for the F/A-18, $3C — type 3 — for the F-16), at the scenery's scale (half-span 25
units), with its ground shadow (op `$10`, `$11392E`: a height limit,
then polygons of the shape's points laid flat in colour 0). Port
against `work/ref/out1.png` (front view on runway 1): the same picture but 3
rows higher — the original's screen carried a leftover shake offset
(`$13ABC8` = 3; the shakes, `$119AFE`, run only in the cockpit view).

**Sound from outside** (`$104F14`, `$10530E`): only the textegn pair
(`$109E46`), period $320 − 2t − t/2 − t/4 (t the throttle with the
turn's share, as in the cockpit), volume a quarter less; events: start
(period, volume), afterburner on (+$20, $3F), off (+$20, volume),
missile launch (the thump at $3F), boom (+$16, $3F); otherwise the glide
(`$109EFA`, 32 ticks). The volume (`$105354`, while the camera follows
the aircraft, `$13AAA5`): by the position folded to 0-4 (4 − |position −
4|… positions above 4 count as 8 − position): 0 and 1 → v/2 + v/8, 2 →
v − v/4, 3 → v − v/8, 4 (in front) unchanged; by the distance: 1 → v −
v/8, 2 → v − v/4, 3 → v/2 + v/8, 4 → v/2; then a quarter less without
the afterburner, at least 1. Without following (`$13AE32`): v × 32 /
`$13AE32`, at most $54. The gear sound from outside at volume 5 (10 in
the cockpit, `$104B08`). A view key asks for the engine's update at once
(`$13ABA0` = $FF, `$10E52E`), even when the view stays. Checked:
`work/ref/ov1.jsonl` (outside, front, throttle steps and view keys),
148 of 149 frames' calls equal (the miss: a key that left the position
as it was, which the recording cannot show the port).

**The view from above** (key M, `$10ECE0`; `$13AA9D` = 1): the camera
at the aircraft's x and z at the height `$13AF32` (kept in `$13A954`
between uses; `[` / `]` step it by $2000000 between $1000000 and
$8000000 in 32-bit units), looking straight down with the start map's
matrix, the view down to row $B3 = 179. `$11FD40` draws a grid over it:
24 lines along z (x from the 16-bit coordinate $F680 in steps of $1C0,
half a degree of longitude; z from $23E0 to $EF60) and 16 along x (z
from $FEB0 in steps of $230; x from $1440 to $EA40), colour 8, every
other line labelled with the panel's number drawer (colour 2): the
first set 115 on at the line's clipped bottom end (12 left, 5 up), the
second 34 on at its left end. Checked against `work/ref/above.png`
(runway 1): equal but for the carrier and the sea's specks.

Below the view (row $A7 = 167, `$13AC74`) colour 3 with "HDG nnn" (three
digits with zeros, $126AB2: heading (+$68 >> 3) / 10), "nnn KTS", "nnnnn
FT" on row 184. **Look directions in the cockpit** (`$13AA97`, keys `$10E64C`…: keypad
8 ahead (0), 2 back (6), 4 / 6 one step round (−1 / +1, wrapping 0-11),
7 / 1 up / down (12, 13)): the view turned about the aircraft's own up
axis by `$12249A`'s heading for the direction (1-11: 2256, 4800, 7200,
… 24000, 26544 in 80ths of a degree, clockwise), 12 and 13 by pitches
that depend on the aircraft's (`$122392`…). `$10E7B8` sets the view's
bottom row (`$13AC74`: $90 for 0-2 and 10-11, $A7 for 3-4, 8-9, 12-13,
$B3 for 5-7) and the cockpit's sideways shift `$13AC76` from `$10E828`
(0, −7, −14, −21, −50, 50, 50, 50, 50, 21, 14, 7, 50, 50), × 16 pixels
(`$13AC78`) for the head-up display and panel: the cockpit slides
right as the pilot looks left; what it uncovers from row 140 down is
colour 3. From 50 on there is no cockpit and the aircraft's own shape
shows (no shadow: `$113984` draws it only outside). Checked against
`work/ref/look/l1-l6.png` (directions 11 to 6 on runway 1): the
cockpit's place and the view's extent equal.

**Zoom** (keys `[` / `]` in the cockpit, `$10E836` / `$10E856`):
`$13AD32` halves (in) or doubles up to $80; it is the third row's scale
of the view matrix, the readout ($1266C8) shows 1.0, 2.0 or 4.0 (the
port stops at $20); `$13AACD` bit 7 is set at $80.

Not rebuilt: looking up and down (keypad 7 and 1 do not exist in Reprise's key map).

### The view in the cockpit

Camera = position + view matrix × (5 up, 20 ahead) / 64 (`$10F49E`); camera
space = the body matrix (+$80) with its rows × $A8, $FC and the zoom
`$13AD32` ($80) / 256 (`$10D308`, `$122D84`), checked against `$13AEC8` and
`$13AD52` of the recordings.

**Map types are + 16.** The visible lists (`.analysis/fa_18_interceptor_amiga/harness/listsnap.py`,
`.analysis/fa_18_interceptor_amiga/work/ref/runway_lists.txt` at the runway start) hold map type + 16 for
every object of all three maps: types 0-15 are the vehicle shapes, 16-31
the sixteen moving objects' own records (filled at run time: 16 = the
player, from type 3 or 4; 30 = slot 14 = the carrier, from type 8), the
scenery starts at 32.

**Levels of detail** (`$111D9C`): the list entry's distance word (+16) is
the distance in 16 world units (type 46 at 3910 units: 243); thresholds
(`& $3FFF`) count the same units; the first entry whose threshold exceeds
the distance gives the model, past the last limit nothing is drawn.
**Passes:** at zoom $80 a model word with bit 13 is drawn only by the coarse
maps' passes, one without it only by the fine map's (`$13AB54`) — which
is why the hill (58) and the bridge-like 86 that the check drew were
missing from the original.

### Lockstep checks (`fa_18_interceptor_assets flightcheck`)

| Recording | What | Ticks matching |
|---|---|---|
| `fc1.jsonl` | take-off at full power, pull up, roll, pull | 159/159 |
| `fc2.jsonl` | afterburner, ceiling, rolls, rudder, throttle to reverse, speed brake, stall | 258/259 (the G key's tick) |
| `fc3.jsonl` | ground roll, nose rotation and settling, speed brake and wheel brake bit on the ground | 196/198 (key ticks) |
| `fc4.jsonl` | brakes off, lift-off, afterburner climb | 238/239 (key tick) |
| `f16a.jsonl` | the F-16: take-off at full power, pull, rolls, rudder (also `soundcheck`) | 328/328 |

Not recorded yet: a soft touch-down from the air, water (a hard one is
the crash below).

### The crash ($102EFE, $1038D8, $103980, $1039C2)

Recorded: `.analysis/fa_18_interceptor_amiga/work/ref/cr5.jsonl`, `td3.jsonl` (this document), the flash timed
with `.analysis/fa_18_interceptor_amiga/harness/flashcap.py` (`.analysis/fa_18_interceptor_amiga/work/ref/flash_cr4.txt`, screenshots
`crash_flash_01_*.png`).

1. The flight sets the crashed flag ($200; `$106B0E`: sink at contact
   $240 or more).
2. Next frame the screen procedure `$102EFE` sees it: the log's crash
   count +1, the flight stops (`$13ABBC` bit 6), the flag cleared,
   `$13AB89` = 40; every channel stops and the noise starts
   (`$10A0DC(63, 120)`: 2.4 s). At the frame's end (`$10827C`) the screen
   flashes: 40 times palette 1 (every colour $FB2, panel and border too)
   for one vertical blank, then the frame's own palette for one (80
   blanks, 1.6 s; measured: each half is exactly one step of the noise's
   per-blank fade). The game waits meanwhile.
3. `$1038D8` → `$103902`: the aircraft back at the start (`$10D558`,
   whose start routine also restarts the engine sound, clears the message
   and warnings), aircraft left (`$13AB87`, 5 in free flight from
   `$10217A`) − 1.
4. With aircraft left: `$103980` places it once more, then `$1039C2`
   holds 5 more frames (`$13ADC6` counts every frame) and lets the flight
   run again with the engine sound restarted. Crash to flying: 9 frames
   plus the flash.
5. With none: the screen cleared (`$124922`), 6 frames, the menu palette
   and text 99 (98 for the F-16: "HEY ROOKIE, F/A-18'S DON'T GROW ON
   TREES, YA KNOW") typed with a click per character (`$127C50`), then
   the main menu when the text is done.

The port follows this (`game/module.cpp` phases); without a front end it
starts free flight anew after the text. Ditching in water is not traced:
the port ends it like a crash (**hypothesis**).

### Cockpit messages ($103D4C) and the ground warnings ($11A46C)

The message line: `$13ADD0` the code (low byte the record, see this document;
high byte: bit 15 blinks with the next record - bits 15+14 with blank -,
bit 12 at twice the rate, bit 14 alone shows it for a time), `$13ADCE`
what shows. Each frame `$103D4C` keeps a posted message (`$119C68` sets
`$13ABBC` bit 0) or takes the first warning's (`$13AE40` bit 7 stall →
$C001, others for fuel and radar not rebuilt); a new record resets the
count to its attribute's low 6 bits (0: blinks for ever); blinking runs
on the clock `$103C94` (8 frames: ticks $86, 0, 4, 0, 6, 0, 4, 0 - bit 1
every 4th, bit 2 every 2nd frame); when the count runs out the record
stays, plain. A record's last byte asks for the message beep ($04 on, $08
off). Drawn by `$127126`/`$127390`: 26 characters 4 pixels apart from x
108, row 192, colour 1 for attributes $8x, 4 for $4x (read from the
original's bitplanes, `.analysis/fa_18_interceptor_amiga/work/ref/msg7.idx`, `msg8.idx`; the port's line
equals it pixel for pixel).

Ground warnings (player, flying, after the input step): sinking (`+$42`
< 0) in a spin: sink × 64 ≥ height → $D02A (EJECT) and beep $40; sinking
nose down ($1E0-$3840) with sink × 16 ≥ height, or banked more than 15°
(roll $4B0-$6BD0) at height ≤ $1800 → $9C06 (CRASH IMMINENT / - PULL UP,
3 blinks then steady) and beep $80, unless on the ground or already
showing. Checked with the recordings in this document (message line equal in
every frame).

### Weapons (2026-10-29)

Found in the code, checked against recordings of the original firing
from the carrier's deck and in the air (`work/ref/wpm.jsonl`,
`wpg.jsonl`, `wpa.jsonl`: `flightcap.py --objs`; screens
`work/ref/wp/`). `fa_18_interceptor_assets weaponcheck` steps the
missile records and projectile slots from each recorded frame and
compares the next: all frames of the three recordings match (positions,
velocities, speeds, lives).

**Stores** (player record): +$5F holds the missiles, Sidewinders in the
high nibble, AMRAAMs in the low; +$60 the rounds; `$13AB3D` flares,
`$13AB3C` chaff. A new start (`$10F184`) gives 2 Sidewinders, 4
AMRAAMs, 500 rounds, 16 flares, 16 chaff. +$63's high nibble is the
weapon selected: 0 none (the start), $10 the gun, $20 Sidewinders, $30
AMRAAMs. (The low nibble is another mode, 9/B/D, cycled by key R,
`$10DF1C`: not traced.)

**Keys** (`$10DAC8`, raw codes): Return ($44, `$10E90E`) steps the
nibble down by $10 (0 → $30 → $20 → $10 → 0) with the refusing beep
(`$127D82`) every time; space ($40, `$10F0AA`) fires: with the gun it
sets `$13ABB6` bit 3 until the key's release ($C0, `$10F100`), with a
missile it asks for a launch (`$13AAAA`); F ($23, `$10EE34`) a flare,
C ($33, `$10EEC6`) chaff (Shift-F: the ejection, not ported). Flares and
chaff count down first: with 1 left the key shows "NO FLARES
REMAINING" / "NO CHAFF REMAINING" (records 39, 41), else "IR FLARE
FIRED" / "CHAFF DISPENSED" (38, 40), sets `$13AB7B` (2 flare, 1 chaff)
and a 30-frame timer (`$13AB3F`, `$13AB3E`; read by the missiles'
decoy logic `$11A916`).

**Missiles** (`$116C9E`-`$116D5C`, in the object step after the
player): records 1-3 are the player's missiles. A free record with a
launch asked for takes it (`$11764C`; the request goes even when the
weapon has none left), then the record steps the same frame. The launch
(`$1176D8`): the engine event $FB with `$13AA87` 8; the aircraft's
record copied ($A4 bytes); type +$62 = 1 for AMRAAMs (log counter
config +$3E), 0 for Sidewinders (+$42); the count's nibble down by 1;
the station is the count left (Sidewinders 0-1, AMRAAMs 2-5) in a table
of model offsets turned by the view matrix (+$92) / 64: `$117956` for
the F/A-18 (wingtips (∓26, 0, 0), then (∓5, −2, 2), (∓15, −2, 1)),
`$117932` for the F-16; life +$4C 150 (Sidewinder) or 200 (AMRAAM);
+$26 = $14; flags +0 |= $11C2 (bit 8: the object step skips its move on
this first frame), +2 bit 7 (on the ground) cleared (and the height
velocity with it), rates and sink cleared, +$2C = −1 (no target point).
Each frame (`$117E4A`): life − 1, gone at 0; from 50 on the speed rises
by $1E0 to $4200, and with no target (`$13ACB0` = −1; the copied +1 bit
3 leads there) by another $240 toward $4200 (`$117E8E`); below 50 it
moves by $240 toward $3000, below 12 it sets +0 bit 10. +$6C and +$6E
both take it. The object step (`$11A352` → `$106B0E`) then turns the
velocity toward the direction of flight a quarter at a time and moves
the record, as for the player. In free flight (no locks) a missile flies
straight on. Drawn as shape types 0 (Sidewinder) and 1 (AMRAAM), with
their flames.

**Projectiles** (`$1072DC`, after the flight, with the drawing): ten
slots of $40 bytes, $80 apart, at `$13AF62` (+0/+4/+8 position, x and z
within the cell at +$30/+$32, +$C-+$14 a frame's move, +$18-+$20 an
eighth of it for the hit tests, +$24 moved once, +$26 flags: 1 in use,
2 hit, $1000 chaff, $2000 flare; +$28 life). Each frame, for every
slot: life − 1; while the trigger is held or a decoy is asked for, a
free slot gets one (one a frame, a burst of 5 counted in +$7D's high
nibble, only with rounds left - for flares and chaff too); a burst
refills when no slot fired or waited (+$3B, 20 frames after a round).
A round: the gun's sound (`$10A2F0(6, 22)`, 30 outside; the noise
`$10A0DC(28, 48)` without slot 10), rounds − 3 (not below 0), config
+$3A + 3 (also for decoys). A decoy: the noise. Where it starts
(`$1077D8`, model offsets × view matrix / 64): rounds (0, 0, $38)
(F-16 (6, 1, $18)), life 20; flares (0, −8, −$38), life 60; chaff
(0, −8, 0), life 30. Its direction: (0, lift, $400) × view / 16 for
rounds, lift $43, $3D, $45, $40 by the frame counter's low bits; (0,
−$40, 0) for decoys; its length (`$107AFE`, `$119D50`: the length
routine with a first shift of 3, 32 times longer than `$119CB8`'s):
rounds speed / 64 + $C6 + (0, $10, 0, −$10 by the frame), flares 5,
chaff $D; the frame's move is 8 times that; decoys add the aircraft's
velocity. Then (`$10754C`): gone below life 1; else (not yet hit) the
hit tests and the move (`$11AE96`): rounds fall $100 a frame, chaff
slows by 1/64 and falls $100, flares slow by a quarter in all three
axes; the first frame only marks +$24. Below height 0 (`$10754C`) it
is hit, life 2, put back along its move to height 0 (`$11B45A`: per
whole unit of height, ratios in 16ths). Drawing: rounds a pixel of
colour 9 (`$1238AC`); hits and chaff three short lines of colour 9 ±3
units about it (`$121570`, table `$12163A`); flares `$121504`/`$12185E`
(a burning blob: drawn as a dot here). Not ported: the hit tests
against scenery (`$142A7C` lists) and objects (8 sub-steps), and their
effects.

**Readouts.** The head-up display's weapon line (`$12685C`, colour 13,
row 129 from column 92, four pixels a character): "GUN" and the rounds
in three digits (two leading zeros blank), or " SW" / " AM" with the
count in one digit (0 blank). The panel's left display (`$1274A4`,
`$1255FC`, redrawn when `$13AB33` / `$13AB34` count): "ARM" (or " NO"
with none left) on row 176 and the name on row 184 from column 61; the
gun's mark at (68, 163-164) red when selected; the stations
(`$1255DE`: Sidewinders (85, 172), (51, 172); `$1255E8`: AMRAAMs (74,
159), (62, 159), (81, 167), (55, 167)) as a 6-pixel line below the
point with two fins: black when empty, red for the next to go of the
selected weapon, else green.

**The gun's aiming circle** (`$128734`, only at zoom 1, `$13AD32` $80):
`$1288CE` keeps a point (0, $600, $4000) × view / 64 from the aircraft
for two frames (`$13AA06`, `$13AA12`) and projects the older one from
the aircraft's position now (`$123894`); the circle shows there the
next frame (`$13AC2E`): `$12919C` mirrors the 20 points of a quarter
(`$129548`) into the four quarters (the lower two one row up), colour
10, inside columns $63-$DB and rows $39-$84, and a dot of colour 13 at
the centre. With a target (`$13ACB0`) it follows the target instead
(not in free flight). The port's projection now truncates the quotients
before taking them from the centre, as `$123894` does (the circle sits
on row 74 as the original's).

### The demo (menu item 1, 2026-10-29)

The menu's 1 ($7F, only with the recording loaded) starts mission 3's
world from the carrier's catapult (`$10215E`, `$101BF4`: place 4, three
aircraft, the object list for $7F, entry 10 of `$11DFB2`; then
`$13AB96` = 3, so the flight runs as mission 3) and plays the recording
back (`$13AB3B` = 3). Seen on the original (`work/ref/demo.jsonl`, the
menu's 1 to the second start): the screen fades in (`$13AB90` 0 → 15, a
step every third blank, `$1093F2`: colours 0-15 from the table of darker
copies before `$11A04C`), seven frames, blocks 88 ("YOUR VECTOR XXX FOR
BOGEY", the vector filled in) and 90 ("READY TO LAUNCH") typed, then the
flight with the recording from its first byte.

The recording (`$10DFEE`, the player's input step): each frame one
control byte from `textply` (the record's +$65: stick, rudder, throttle
bits; the F-key throttle settings are not used) and one record of two
words from `textctl`, one record ahead of the control bytes. The words are
key events played into the key handlers at the next frame's start
(`$101544` → `$10D97C`), bit by bit in the order the original tests them
(the high byte's bits 0-7 first): the first word gear `$10E966`, hook
`$10E36A`, fire `$10DF70` / its release `$10DF80`, weapon `$10E8CE`,
Shift-E `$10DE7A`, radar range `$10DF1C` (R: 40, 10, 2 miles), target
`$10DEF8` (T), speed brake `$10E330`, flare, chaff, ECM `$10EF46` (J);
the second word the view keys (`$10ECE0` M, `$10E3C0` keypad Enter,
`$10E416` Shift-Enter, `$10E544` nearer, keypad 8 9 6 3 2 1 4 7 5 .
`$10E64C` `$10E4D4` `$10E6B4` `$10E4EE` `$10E668` `$10E700` `$10E684`
`$10E6DE` `$10E50A` `$10E516`, keypad releases `$10E8BA`, farther
`$10E5E4`). Outside, the keypad keys set the camera's positions 0-9; in
the cockpit 8 and 9 look ahead, 6 / 4 step the look round, 2 back, 7 up
(look 12), 1 down (13: `$122396`, the view pitched 90° — 83° within 7°
of level — no cockpit, the view to row $A7). While recording, the
handlers set the same bits (`$13AC8A`, `$13AC8C`) and the input step
writes them out. Three $FF control bytes end it (2,046 frames): the
flight stops (`$10EF08`, `$13AB1A` 1), the screen fades out (a step every
second blank) and the demo starts again.

The port has the replay (`game/replay.*`), the key events, the fades and
looks 12/13; `fa_18_interceptor_assets democheck` flies the recording
from the catapult against `demo.jsonl`. It does not match yet: the
demo's carrier is not free flight's — path $40 from cells (68, 68)
towards (70, 65), so it sails (the object step for moving objects), and
the list has mission 3's other objects (the bogey; type 6 entries).
Menu item 1 therefore waits for the missions' objects (task 11).

### Qualification (menu item 5, 2026-10-29)

Mode 9 (`$102252`): the carrier's catapult (free flight's object list,
the carrier at rest), three aircraft, the screen fading in (`$10234C`),
then the security check (`$1027C8`; skipped when `$13AB1B` is set, and
by the port always: copy protection). A passed check (`$1197CA`:
`$13AB9C` 1) leads mode 9 to `$101CC0`: with `$13AB1B` clear - chosen
from the menu - block 73, the briefing, is typed (the sound update off,
`$13AA85` 0) until done or a key (`$101D06`); then, or at once when
`$13AB1B` is set, "READY TO LAUNCH" (`$102D58`) and the flight. Nothing
on the way clears the screen: the briefing is typed over the view, like
the other texts.

Each frame (`$118AA8`, while the flight runs) the player's aircraft
standing (+$6E 0) on the deck with the hook down and a wire caught (+2 &
$C080 = $C080) qualifies: `$13AA88` = $FF, the flight stops, and the end
procedure (`$1031F4`, `$1032C2`) sets config +0 (qualified), writes
`config` (`$10858A`), sets `$13AB1B`, and types blocks 74 ("LANDING
SUCCESSFUL ... PREPARE TO BE RE-LAUNCHED") and 83 ("(SHIFT ESC) TO RETURN
TO MAIN MENU") over the stopped view (`$103AA8`). Then 100 passes
(`$13ADC6` = $64, `$103B24`), the screen fades out (`$13AB91` 0) and
the selection starts again.

Starting again (`$101854` for Esc, `$103B24`): the flight stops, the
screen fades out (a step every second blank), `$101A96` stops the
sounds and returns to the menu's chosen-item screen (`$101E04`: palette
0, the item's line, 150 frames since the check counts as passed, 210
only from the menu with the tune), then the item starts afresh
(`$10201E`) - for free flight its start map, for the qualification the
catapult without the briefing ("RE-POSITION ON CATAPULT"); after the
maneuvers' demo (2) it starts as their practice ($7D, `$101AF4`), the
demo (`$13AB3B` 3) starts itself without the menu. Shift-Esc (`$101838`)
clears `$13AB1B` and goes to the main menu.

Timing: while `$13AA85` is clear a main-loop pass neither draws nor
waits for the frame cap (`$101140` skips both), so the 100 passes after
the landing texts last as long as the machine takes - on the A500 about
6 passes a blank (the demo's recording: 18 passes a fade step of 3
blanks). Port decision: 17 blanks.

Checked on the original (pokes of the motion bits on the deck): the
landing's texts over the view, the fade, the chosen line and the new
launch.

### The F-16 (2026-10-29)

The aircraft choice ($10E9DA: key 1 → $11, 2 → $10, `$13AB39`) goes
into the record's type +$62 at the start (`$10D5A8`). The flight model
has no per-type data: everything that differs is a test of +$62 (the
type's high nibble $10 is "aircraft" for both). A recording of the
original flying the F-16 (`work/ref/f16a.jsonl`: take-off, pull, rolls,
rudder) matches the port's flight and sound in all 328 frames. What
differs: the shape (type 3, `$10D58C`'s offset $3C), the head-up
display's tapes and load factor (above), the pilot's eye (`$10F49E`:
(0, 4, 18) × view matrix / 64 instead of (0, 5, 20)), the outside
camera's offsets (`$11D734` instead of `$11D770`), the missile stations
(`$117932`) and the gun's muzzle ((6, 1, $18)), no arresting hook
(`$10E372`), the end text 98 instead of 99 (`$103A06`). The carrier
(place 4) always takes the F/A-18.

### The carrier (2026-10-29)

Each mode has an object list (`$11DFB2`: per mode 4-byte entries
(offset, `$13AB9A`) for three variants, then per progress level the
list's start, `$11CF06`); free flight's (`$11E61C`) has one entry of 10
bytes: the type record's offset ($A0: shape type 8, the carrier), the
type byte +$62 ($20), the record (14, bit 15 set), a path ($42 into
`$11DDC0`) and flags ($8000: no flight path). `$11D318` spawns it: the
path's five words are the coarse cells x and z (<< 22), the offsets
(<< 8) and the height (<< 8): x $11980000, z $10580000 — at rest,
heading 0. Every object, the carrier too, goes through the same object
step (`$116C4E`…, `$11A352`). The type's data block (record +16,
`$12E340`) holds the deck height in its long at +2 (bit 31 a flag): 112.

Start on the carrier (place 4, `$10D750`): the carrier's angles, its
place plus (11, 0, 104) turned by its view matrix (products over 16),
y = deck × 256 + $708, byte +4 |= $C8 (on the deck, no rudder or roll).
No aircraft choice; "READY TO LAUNCH" (block 90). There is no
catapult: the aircraft rolls along the deck at its own thrust
(recording `work/ref/cv2.jsonl`, full power from the deck: the port's
flight step equal in 198 of 199 frames; it leaves the bow after about
60 frames and sinks to about 110 ft at 447 kt).

The deck test (`$11B6AA`, the carrier part; each frame before the
move, for the player and the other aircraft): byte +4's deck bits are
cleared (`$11B6C0`), then for the aircraft's model points 0 (nose: bit
$40) and 3 (under the tail: bit $80), turned by its body: when at or
above the deck height and inside one of the deck's outlines, the bit
is set and +$4E = the deck height. The outlines (from the first
pointer of `$12E298`; `$12EF74` for the other ships): the carrier's
model points by offset (6 bytes a point, the model's scale 2^−3), six
convex outlines each ending with bit 15, $FFFF after them, reaching
from z −688 to 960 and x −216 to 224. In the wires' zone
(`$11BA3E`, only for the tail point and on the ground): between the
lines through the points 54-56 (C, B and D = A + (A − C)/2 beyond A,
parallel to A − C and to B − C) — x −76…96, z −580…−352, the deck's
stern — and heading along the deck (the products of the two view
matrices' third column ≥ $E000000), +2 bit 14 is set; with the hook
down (bit 15) that is the full brake. Outside it bit 14 is cleared
unless both are set. Below the deck height the hull's faces
(`$12E314`, plane tests `$11BC3A`) make a collision (`$11C0B4`, d0 =
$20; the damage routine). Key A (`$10E36A`, the F/A-18 only) turns
the hook (+2 bit 15) with a click and "ARRESTOR HOOK DOWN" ($4023; up:
**hypothesis** $4022). The place-4 marker on the map is the carrier's
own position (`$11FC44`).

## 5 Sound

Research notes (2026-10-29), from this ROM and runs of it only.
Code: `games/fa_18_interceptor_amiga/game/sound_driver.cpp` (the driver),
`sounds.cpp` (the game's rules), checked with
`fa_18_interceptor_assets soundcheck`.

### The driver (hunks 74-86)

Forty slots (pointers at `$145BA4`, 64-byte records, `AllocMem`) and four
channels (records at `$145110`: registers, a cell holding the slot the
channel plays, interrupt words). Slot record:

| Off | Meaning |
|---|---|
| +0, +4 | samples, length in bytes (bit 31: a copy sharing another slot's samples) |
| +8, +$C | period and volume, 16.16 (the high words go to the chip) |
| +$10, +$14 | repeats left (-1: for ever), the value it is reloaded with |
| +$18, +$1C | period and volume steps added every tick |
| +$20 | the slot that plays after the repeats (0: the channel stops) |
| +$24, +$28 | the effect script's two loop counters |
| +$2C | ticks to the script's next entries (0: no script running) |
| +$30, +$34 | the effect script, the next entry's offset in it |
| +$38, +$3C | the steps stop after this many ticks (0: never) |

(this document said +$30 was a generator routine: it is the script pointer.)

- **Audio interrupt** `$1453BC` (also forced through INTREQ by the start
  and stop routines `$145294`/`$145298`): with a slot in the cell: its
  samples and length (`length >> 1` words), DMA on, period (at least $7C)
  and volume (`& $3F`, at most the master volume `$14520E` = $3F), one
  repeat used: below 0 the cell takes +$20 and the count +$14. Without:
  volume 0, period $7C, DMA off. A slot with repeat count r plays its
  block r times (the forced interrupt and the DMA start each count one).
- **Vertical blank** `$145438` (interrupt server `$1451F8`, `AddIntServer`
  level 5): per channel with a slot: the script step `$1454F2`, the
  registers `$1454C0`, then period += step, volume += step, and the step
  counters.
- **Scripts** (`$145F50`-`$1460A7`, data): 8-byte entries `field, value`.
  A field below $2C writes the slot field (8 period, $C volume, $18 / $1C
  steps, $24 / $28 loop counters); $2C waits that many ticks (0: the
  sound ends - cell cleared, interrupt forced); $40 / $44 count loop
  counter $24 / $28 down: at 0 go on, else jump to the value (a byte
  offset from the script's start). Scripts: `$145F50` beeps (slot 4),
  `$145FB0` noise (6), `$145FD8` gear (11), `$146008` slot 12, `$146048`
  slot 10. The routines that start an effect write their parameters into
  the script's entries first.
- **Shift register** `$145E7C` (word at `$145F3C`, first value
  $47091247): bit = bit 3 xor bit 0; shift right, the bit into bit 31.
  `$145ECA(n)` takes n bits. Slot 6's 2048 noise bytes come from it at
  load (`$145EFE`); slot 4 is 16 × $7F then 16 × $82 (`$145E20`).

The port keeps this as `SoundDriver` over the shared register-level Paula
model (`amiga::PaulaChannels`, its block-start callback is the audio
interrupt; one raised inside another waits for it, as on the machine);
ticks at 50 Hz between the samples.

### Loading ($1099CA, $109660)

| Slot | From | Set-up |
|---|---|---|
| 0, 1 | textcpt + copy | both loop; slot 1 length $15CC |
| 2, 3 | textegn (textegn2 if missing) + copy | both loop; slot 3 starts $370 in |
| 4 | square wave | loops, beep script |
| 5 | texttre | plays once |
| 6 | noise | loops, noise script |
| 8, 9 | textwnd + copy | both loop; slot 9 starts $792 in, its length taken from slot 3 (the loader's slip, kept) |
| 10 | copy of 6 | script `$146048`, period $96 |
| 11 | textger | loops |
| 12 | copy of 2 | script `$146008` |
| 35, 36 | text201 + copy | loop, periods $B3 / $B5 |

Flags of what loaded: `$13AE4B` bits 0 cpt, 1 egn, 2 tre, 4 wnd, 5 slot
10, 6 ger, 7 music; `$13AE4A` bits 0 noise, 1 slot 12, 2 text201.
The title tune's slots 13-34: section 2, "The front end".

### The game's sounds

Routines (the names are the roles the code shows):

| Routine | What |
|---|---|
| `$109C58(ch)` | stop the channel |
| `$109C7C(slot, ch, vol)` | play the slot from its start |
| `$109DB2(per, vol)` | textcpt pair on channels 0/1, periods per and per+2 (textegn at vol/4 without it) |
| `$109E46(per, vol)` | the same with textegn |
| `$109EBE` → `$109EFA(per, vol, ticks)` | both engine channels slide to per / vol over ticks (step from channel 0's slot) |
| `$109F9A(vol)` | wind: slots 8/9 at $168 + 6 random bits, restarted |
| `$10A042(p1..p9)` | beep on channel 3: p8 × (p7 × (tone p1/p2 for p3 ticks, tone p4/p5 for p6), pause p9) |
| `$10A0DC(vol, ticks)` | noise on channel 2: vol falling to 0 over ticks, period $231E + 4 × 11 random bits |
| `$10A18C(vol)` | tyres (texttre once, period $136 falling 2 a tick) |
| `$10A1E6(vol)` / `$10A24C` | gear sound on channel 2 / stop it |
| `$10A258(vol)` | slot 12's thump: a missile's launch (event $FB, set only by `$1176F0`) |
| `$10A2F0(a, b)` | a round of the gun: slot 10 on channel 2, its script's values a and b << 16 (6 and 22 in the cockpit, 30 outside) |
| `$127D66`, `$127D6A`, `$127D82` | clicks: a typed character (300,2,…,1,1), a warning's (…,4,4), a refused key (…,2,2; locked by `$13AB7A`) |

**The frame's update** `$104AA0` (after the flight and the messages):
1. Gear sound: `$13AB75` bit 7 (set to $83 by the G key when textger
   loaded) starts it at volume 10; the real-second counter takes it down;
   at 1 it stops (about two seconds).
2. One beep for the warnings `$13AE44` (unless `$13AB7A` > 0), first of:
   $40 spin, then (only with the gear's low nibble 0) $80 crash imminent,
   $01 (locks 3 s), $04 message, $100, $10, $02, $20 stall, $800; $08
   stops channel 3. The bits are then cleared.
3. The engine, on an event (`$13ABA0`) or every fourth frame
   (`$13AAB1 & 3`): t = |throttle|, plus the larger of |pitch rate| and
   |roll rate| (/16; at full power minus); volume `$10530E` (0 → the wind
   at `$105502` = |airspeed| >> 10, at least 1; with `$13AA87` running 63;
   else 63 − (120−t)/4, below 9: 60 − (120−t)/2; at least $28); period
   $310 − 2t − t/4. Events: $FE start ($109DB2, textegn with the
   afterburner), $FD afterburner lit (textegn, period+$23, 30), $FC out
   ($109DB2, period+$20), $FA boom (textegn, period+$16, 40), $FB
   a missile's launch (the thump; the engine restarts 8 frames on), else a glide (`$109EBE`, 32 ticks; with the afterburner
   3/8 volume). `$13AA87` counts frames to a restart event.

**Events from the flight** (`$105ED4`, `$106B0E`): a throttle step from 0
→ $FE; afterburner lit → $FD and 6 frames; afterburner off by the
throttle → $FC; passing the speed of sound (speed ≥ $20D0, latched in
`$13ABC2` bit 14) → $FA and 2 frames; a soft touch-down → $FE and the
tyres (volume $1E below sink $181, else $28); the restart routine → $FE.
Warnings: the stall latch (`$13AE40` bit 7, end of `$10557C`) → $20;
`$1183E2` gear down above speed $1200 (latch `$13AB36`) → `$127D6A(4)`;
`$11A46C` ground warnings → $80 / $40 (with the messages, this document).
Not rebuilt: G-load warning `$117128` (blackout `$13AB79` halves the
engine), radar and missile warnings ($01, $02, $10, $100, $800), the
gun (slot 10), views other than the cockpit.

### Checked

`.analysis/fa_18_interceptor_amiga/harness/flightcap.py --sound --calls` logs every call of the routines
above per frame; `fa_18_interceptor_assets soundcheck` runs the flight
and the sound rules from each record and compares the calls and the
message line (`$13ADD0`, `$13ADCE`) with the next record:

| Recording | What | Frames matching |
|---|---|---|
| `sd2.jsonl` | take-off, gear up and down, afterburner on and off, throttle to 0 (wind), engine restart, stall beeps | 619/619 |
| `cr5.jsonl` | crash imminent, crash into the bay with the last aircraft, end text clicks | 119/119 |
| `td1.jsonl` | climb with a posted message | 259/259 |
| `td2.jsonl` | a pushed-down height (no touch-down) | 59/59 |
| `td3.jsonl` | dive, crash imminent, crash, both restarts, the hold | 89/89 |

Not seen in the original yet: the tyres (a soft touch-down), the boom,
the spin warning.

## 6 Bugs and oddities in the original

- **Wind loop length** ($1099CA): the copy of textwnd in slot 9 takes its
  length from slot 3 (the engine's copy), not from slot 8: slot 9 plays
  $2AF8 bytes from $792 in. Harmless (inside the file); the port keeps it.
- **"F/A-16"**: text block 98, the F-16 pilot's end text, reads "F/A-16'S
  DON'T GROW ON TREES".
- **Glide with an empty channel** ($109EFA): checks only that channel 1
  plays; with channel 0 empty it would read a slot through a null
  pointer. Never seen; the port skips the glide then.
- **The cracked disk**: this copy is a crack; text block 71 reads
  "CRACKED BY A-HA". What else the crack changed is not traced.
- **Slow machines**: nothing scales by elapsed time, so on a stock A500
  (4-7 frames a second) the flight runs in slow motion; the 15 Hz cap is
  only reached on faster machines (section 2).
- **Unused message class**: the message drawer handles attribute class 0
  (bits 7-6 clear), but no message record uses it.
- **Flash with no noise slot**: without the noise slot `$10A0DC` raises the
  engine's boom event instead (a fallback for low chip memory).


## 7 Research tools and checks

Research files live in `.analysis/fa_18_interceptor_amiga/` (`harness/`,
`tools/`, `work/`, `ghidra/`); nothing there ships.

- Reference machine: `harness/run.sh` (FS-UAE, GDB stub port 6868),
  `mkboot.py`, `f18.py attach|key`, `screen.py` (renders the copper list;
  `F18_INDEX=file` writes colour numbers), `shot.sh` (window capture).
- `flightcap.py` records the player object and globals each frame
  (`--sound` slots and channels, `--calls` the sound routines' calls,
  `--objs` the missile records 1-4 and the projectile slots, `--shots`; steps for controls, keys, dives, control byte, pokes);
  `flashcap.py` times the crash flash; `listsnap.py`, `viewgrab.py`,
  `modelbp.py`, `opbp.py`, `pointcheck.py`, `stepcheck.py`, `fps.py`.
- `fa_18_interceptor_assets` (tools/): `import`, `export`, `viewcheck`,
  `flightcheck` (flight model against recordings: fc1-fc4 equal except
  key ticks), `soundcheck` (sound calls and message line: sd2, cr5,
  td1-td3 equal in every frame), `weaponcheck` (missiles and projectiles
  against `--objs` recordings: wpm, wpg, wpa equal in every frame),
  `hudcheck` (the head-up display for given readings against a
  `screen.py` colour dump: `wp/gun.idx` F/A-18, `f16b.idx` F-16, no pixel
  differs), `fly` (headless module run from the title,
  pictures and `!wav`; keys: digits, `*` Return, `~` Tab (Esc), `^`
  Ctrl+Tab (Shift-Esc), `_` space, capitals the letter keys, `@ [ ] { }`
  the keypad's 0 4 6 8 2).
- `harness/proc.py`: the running original's screen procedure, frame
  countdown and text queue (the front end's traces).
- Captures in `work/ref/`: `fc*.jsonl`, `sd2`, `cr5`, `td1-3`, `wpm`, `wpg`, `wpa`, flash
  timing `flash_cr4.txt`, message bitplanes `msg7.idx`/`msg8.idx`,
  reference screenshots.
