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
the 16-colour palette at `$11A04C + (15 - step) × 32`, a step every
third vertical blank) and the security check runs (`$1027C8`: blocks
95/96 for missions, block 71 and the input field block 4; on this
cracked disk block 71 reads "CRACKED BY A-HA" and any answer passes;
the check is skipped once passed, `$13AB1B`). Block 7 ("SELECT YOUR FREE
FLIGHT STARTING LOCATION") at the bottom over the strip; the places of
`$137CF6` (16-byte records: the three runways by cell and offset, then
moving objects $800E and $800F, the carrier) are marked with a dot in
colour 2 at their projected position (`$1238AC`) and their digit
(`$127640`, colour 9) from the dot's column rounded down to 4, plus 8
(`$11FB9E`), the chosen one blinking. Checked 2026-10-31 against the
original's map (harness screen capture): the same scenery and water but
for one-pixel differences in the bridges' lines and the coastline. A digit key chooses ($10EBB4, `$13AB38` = place - 1). For a
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

**Touch controls (port, 2026-10-30).** An enhancement, not in the
original: with the "Controls / Scheme" option on Touch stick (or
Automatic on a phone or tablet without a gamepad) free flight and the
missions show a stick bottom left, a throttle slider on the right and a
GEAR button (`touch_controls()`; none in the demo, the maneuvers or the
front end). The stick feeds the same control bits as the joystick
(`read_controls`); its deflection scales the limits the input ramps head
for (`ramp_inputs`: pitch and rudder 20, roll 60, a deflection d gives
`round(d * limit)`, at least 1), and an input past a smaller limit comes
back by the ramp's own steps. The keys and the recorded control bytes
use deflection 1, so every check is unchanged. The slider sets the
throttle target in the F-keys' tenths (0 idle, 10 full power then the
afterburner, as F10); GEAR is the G key. VIEW (above GEAR) is keypad Enter: the
cockpit or the outside camera. Fire and the weapon keys have
no touch buttons yet.

**Taps on the front end (port, 2026-10-30).** A tap on a menu or mission
line chooses it (the line's text: a leading digit, F1-F6, ESC or SHIFT
ESC; `TextScreen::line_at` finds the line under the tap from the text
blocks' rows); on the start map a tap on a marker picks that runway or
the carrier; the aircraft question takes a tap on its line; other
screens (title, credits, briefing, "READY FOR TAKEOFF") take any tap as
a key; the call-sign entry takes a tap as Return (the name typed so far,
possibly empty).

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
stay at the original tick: 15 Hz, see "Main loop and timing". Attitudes
(the player's body and view matrices, for the cockpit camera and the
aircraft drawn from outside or when looking back) are interpolated by
turning about the axis between the two ticks' matrices, so the aircraft
and the camera turn together and a quick roll does not shrink the
matrix. A cut of the outside camera (the view entered, a new place
about the aircraft, a new distance or viewed record) is not eased from
the old place: between the two steps the camera keeps its new place
about the aircraft and moves with it (`cut_camera`), as the original
puts the camera there at once ($11D9AC).

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

**Typing pace.** The runner ($1278EA) steps once a frame whatever the
frame's length, so the start map's texts (block 75's aircraft list, a
character every 4 frames) type at the map's 15 Hz cap: about 3.7
characters a second against 12.5 on the 50 Hz screens. Timed in the
original (2026-10-31): "1 ... F/A-18 HORNE" about 7 s after the place's
key, the port the same. **Fast text (option, enhancement, 2026-10-31,
off by default):** the port's runner then steps by time, 100 steps a
second (twice the 50 Hz screens) on every screen, the map and the flight
texts included; the 50-frame hold after the last block stays a count of
frames. `fly ... 0:!option=6,1` turns it on in the headless runner.

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
**High detail (option, enhancement, 2026-10-30):** every scenery and
ground object takes its nearest level that its pass draws (the pass rule
below stays), at any distance in the builder's window and with the ground
shapes' height limit lifted; vehicles take their nearest level. Off by
default; the polygon lists are decoded at distance 0 either way.

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
It orders the parts back to front. The tests (2026-10-30, from the
code): **facing** (`$112B1C`) takes P0-P2 from the camera-space buffer,
n = (P1 - P0) x (P2 - P0) (the differences shifted by bits 7-9 of the
test word), the first branch when n . P0 < 0, else the second;
**plane** (`$112AE4`) the six words a point (world units about the
object, shifted by `$13ADA8` like the positions) and a normal, the first
branch when (point + object - camera) . normal < 0; **axis**
(`$112B8A`) the model point at offset v0 (>> the scale nibble) on axis
y (bits 10-11 = 1, against `-$13AD68`), x (2) or z (3, with the cell
offsets `$13AE1A` / `$13AE1E`; hypothesis: the camera's place about the
object), the second branch when the camera's coordinate is smaller than
the point's, the other way round with bit 12. Census: vehicles use facing
tests (and the ships two axis tests), the bridge sections 50-57 plane
tests, 48/49 and 102/103 axis tests; most scenery has no test and paints
its lists in order. A one-sided face seen from behind ends its list
(`$113836`).

**The port's renderer** (`game/scene.cpp`, 2026-10-30) paints like this:
each object's lists in the order its tree gives from the camera, parts
in list order, later parts over earlier ones; a depth buffer (depth
linear in 1/z) settles only what is in front between different objects.
Flat things (ground shapes, flag-2 models such as the airfields' paving)
are painted first with no depth test (filled row by row between edge
crossings, so shapes with bays fill right; faces of standing objects
are split into triangles), pass by pass and far to near
within a pass (hypothesis: the original paints its lists in their
order, which the builder's tables mostly give far to near), then
the aircraft's shadows, then everything standing. Before this a depth
buffer per face let coplanar ground shapes fight (the city streaked
with green) and the F/A-18's wings and stabilizers cut jaggedly through
its body in the start's close-up.

**Polygon lists** ($11284E): opcode words `$8000 | offset into $113470`
(bit 14: the last). A positive word is a distance band: `T, $FFFF` (only
nearer than T) or `T, T2` (only between: at distance 0 the list ends),
or $7FFF; then `count, offset` loads more stored points (count > 0) and
an opcode word follows. Opcodes (78 entries):

| Op | Routine | Operands | What |
|---|---|---|---|
| $00 | $1135A8 | – | nothing |
| $04 | $1135AC | point, colour | a dot |
| $08 / $C0 / $128 | $11373C / $113724 / $11372C | p1, p2, colour | a line (two drawers; $128 only with the camera above 192, `$13AD68` < −$C0) |
| $0C | $1137E4 | points up to one with bit 15, colour word | a face |
| $114 | $11378A | three points, colour word | a triangle |
| $10 | $11392E | altitude, records `n, flag, n points`, negative end | the ground shadow, black |
| $34 | $114A34 | colour, point pairs (bit 15 on the last) | lines |
| $DC | $113888 | long pointer, offsets (negative end) | faces kept in a shared table |
| $58 | $1144F0 | colour, point, n, n counts | rows of parallel lines |
| $80 / $50 | $1141C8 / $1141DA | (colour), point, count | stripes across a quad (deck markings, hypothesis); $50 inverts plane 1 (see below) |
| $F8 | $1157C8 | point, colour, radius | a filled disc |
| $C4 | $1136CE | three points, colour | a dot seen from one side (a light, hypothesis) |
| $CC | $1150C4 | byte count | skip unless the object's flag bit 3 |
| $10C | $113692 | three points, test | skip 18 bytes when facing away |
| $110 / $120 | $11367A / $11366C | – | frames of 52 / 18 bytes follow, frame = $13ABCA & 15 |
| $134 | $1150EA | – | 18-byte variants by $13AB37 |
| $118 | $1135F0 | – | variants by the smoke slot's timer (types 33-35, "Explosions, smoke and trails") |
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
| $24 / $78 / $6C | $114C14 / $1145D6 / $1145C8 | colour, point | parallelogram P0, P1, P2, P2 − (P1 − P0) ($24 only with the camera at most 128 high, in the mode the last op left; $78 sets plane 3, $6C inverts plane 1) |
| $28 | $114C84 | colour, point | parallelogram at P3 with edges P1 − P0, P2 − P1 |
| $44 / $48 | $1147E4 | 4-point records, negative end | quads setting plane 3 (colour 4 becomes 12) |
| $4C | $11408C | colour, point, n | 2n parallel lines across a quad |
| $50 / $80 | $1141DA / $1141C8 | (colour), point, n | 2n filled stripes (runway markings; $50 inverts plane 1, $80 in its colour) |
| $54 / $E4 | $1143C0 / $1143AA | (colour), point, n, n counts | rows of filled stripes ($54 inverts plane 1) |
| $5C | $11486A | point, count | from P3 on, `count` parallelograms (Pk, Pk + V, Pk + V + W, Pk + W), V = P0 − P3 + 1.5 (P1 − P3), W = P2 − P3, inverting plane 1: the runways' threshold blocks |
| $9C | $113B58 | two colours (long), top colour | a box (model points 0-3: P1 a foot, P0 above it, P2 and P3 its neighbours) drawn as one diagonal face: P1 to the far corner when the camera's x and z about the object have the same sign, P2 to P3 otherwise; the long's low word while the camera is farther along z than x, else its high word; the top in its colour while the view looks steeply down (`$13AA76`); nothing unless the model's point 1 lies in the view's pyramid |
| $A0 | $113D1A | two colours (long) | the same for a pyramid: triangle P0 P1 P4 (same signs) or P0 P2 P3 |
| $18 | $113E6C | n, A, B, C, D, colour | n − 1 lines from A + k (B − A) / n to C + k (D − C) / n (the stadium's rows, types 107/108; the original in sixteenths, rounded) |
| $2C | $114960 | count << 8 \| colour, point | lines between point pairs (bridge hangers) |
| $30 | $1149AE | count << 8 \| colour, ref, point | the same, moved by P[ref+1] − P[ref] |
| $38 | $114AA0 | colour, ref, pairs | lines moved by P[ref+1] − P[ref] |
| $3C / $40 | $114B22 / $114B96 | colour, ref, 3 / 2 points | quads with one edge moved by the ref difference |
| $1C / $20 | $114D22 / $114E0E | colour, (ref), two model points, apex, point | one of two triangles, by the camera's side of a line |
| $7C | $113FB2 | model point | orientation set-up, draws nothing (hypothesis) |
| $88 $8C $90 $94 $A4 | see shapes.py | point (, shift) | vertex builders (parallelograms; shift scales a difference) |
| $124 | $11365C | – | skips a word for type 87 only |
| $118 | $1135F0 | – | variants by the smoke slot's timer (types 33-35): `read_smoke_shape` |

62 of the 70 scenery types decode (`.analysis/fa_18_interceptor_amiga/work/sc*.png`): runway centre lines
and ends with numbers and threshold stripes (43-45), the Transamerica
Pyramid (62), Sutro Tower (65), suspension-bridge spans, towers and
causeway sections (48-57), buildings. Names are hypotheses from their
look.

The C++ decoder (`read_shapes`) covers vehicles, scenery and ground
shapes and agrees with the research decoder on every type's face and
line counts. Dots (`$04`) are kept; faces record their facing rule
(always, one-sided, two-sided with a back colour). The smoke's discs
and its `$118` variants have their own reader (`read_smoke_shape`). The
facing tree is kept as `DrawNode`s and every part names its list, so the
renderer paints in the original's order. Not decoded yet: `$18`; other
discs, lights and shadows; the original's rounding.

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

**Scene builder** `$11005C` (rebuilt 2026-10-31, `game::visit_map`): when
the camera's cell, quarter or look-down flag changes (`$13AB48`) it
walks lists of cells around the camera's cell and of slots within each
cell, gathers each cell's objects into slots and then the
visible-object lists (`$143C92`, `$144322`, `$1449B2`; 24-byte entries:
word type << 8 | flags with the precision shift in the low nibble, long
type record, x, y, z words relative to the camera's fine cell corner and
shifted, long info, …; the lists start at the offsets in `$13AC9C`,
`$13AC9E`, `$13ACA0`). The camera cell runs 0-30 (coarse) or 0-127
(fine) and wraps to the other end past them (`$1102C0`).

Which list: by the camera's quarter of its cell (`$13AB40`, the fine
cell within the coarse one, for the coarse passes; `$13AB41`, the
quarter of the fine cell, for the near pass: (3 − x & 3) + 4 (3 − z & 3)
of the position >> 22 or >> 20), whose variant `$136464[q]` (0-3)
picks the remap tables: `$136474` (list by the heading's eighth
`$13AB44`), `$1364A4` (slot codes) and `$1364E4` (cell codes → pairs
`$1106B4`). The lists' base is `$1367A4`, `$136542` while the view looks
steeply down (`$13AA76`, set by `$1043F2` for a pitch over 8 in
$259-$C4D), or `$137386` for the first coarse pass with the camera
below 160 (`$13AD56` > −$A000); the word at base + index is the list's
offset from the base, index = `$136494[q]` × 16 + 2 × the eighth's list
(looking down: 2 × `$136494[$136494[q]]`; low: `$136494[$13AB40]` × 32 +
2 × `$1364A4`[variant][`$13AB41`]). A list: cell code (0-20), count,
that many slot codes; a negative byte ends it. The objects are placed
from the camera's cell by `$1106DE` (coarse) / `$110772` (near) × 4:
the coarse table's +2 entries overflow their word (+32768 reads −32768),
so **the coarse passes put the objects of the cells two ahead two cells
behind** (the dark speck west of the coast on the port's start map was
one put where it lies). The near pass also files the moving records and
smoke slots of the visited cells (`$110470`, `$110528`). A pass keeps at
most 70 entries (from the 70th on each overwrites the last) and, with
the camera below 8192, only the last ones (`$11106A` by height / 2048:
30, 30, 32, 40, or 40, 50, 60, 70 looking down; `$111048` moves the
list's start on). Distances (`+$10` of an entry, 16-unit steps >> the
precision) start at 0, are worked out at once, then again every frame
below $100, every fourth frame below $400, and not at all beyond until
the next rebuild (`$10FB46`).

Two type routines filter by height: `$111C90` (types 43-133 of the
low-altitude kind) is skipped above 14336 by the coarse passes
(`$10FB0E`) and above 16384 by the near one (`$10FDCC`); `$111C84`
(types 67 and 87, the Golden Gate's high version) draws only above
10240 (`$111C84`) and the coarse passes leave its distance at 0
(`$10FB36`).

**Gone records still drawn** (2026-10-31): the lists are rebuilt only
on request (`$13AB48`: bits 0, 1 and 3 the three passes): the camera's
cell, quarter, heading eighth (not while looking down), 8192-unit height
band or the 160 line changing (`$10F58E`), the look-down flag
(`$1043F2`), view keys, a record changing fine cells (`$11A6B8`), a
launch (`$1178C6`, $8C) and the removals `$11AA70` (a hit) and `$117E3C`
($0C). The explosion's end (`$11A3F8`: the missiles, the cruise missile,
aircraft on the ground or out of control) clears the record's active
bit and asks for nothing, and neither the near pass's records
(`$110006`) nor the snapshot list `$143C72` (active records not filed
in a visited cell, `$11122C`) look at that bit: the record's model shows
where it stopped until the next rebuild (`FreeFlight::
leftovers_tick`, module.cpp).

**Checked** (2026-10-31): the port's three lists equal the original's
type by type, in order and place, on the start map (60, 27 and 4 + the
carrier's record) and at demo frames 1840 (the first pass capped from
37 to its last 30), 1880 and 1900 (the low list: 16): harness capture
of the lists `+$13AC9C` with the camera words. The port draws the
moving records apart and does not count them in the caps.

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
cell per coarse map cell, 2^24 in 32-bit units) and a coarse one at
`$137F9C` (8 × 8 words, row by row: cells of 2^20 world units, 2^28 in
32-bit units, the world's corner at cell (4, 4); points in 1/4096 of a
cell, reaching past its edges; each cell's long points at its own
polygons). A positive word is the offset of the cell's data from the
grid base; data starting with a negative word means no water. Cell
data: a long (another version of the cell, `$11F6E0`), then polygons
`count, count × (x, z)` in 1/4096 of a cell (`$11F6CE`), until `$FFFF`;
a word with bit 15 before a count is a height threshold (× 4): that
polygon and the rest of the cell only from that height up (`$11F73C`).
The polygons are not all convex (coastlines with bays).

Which cells `$11F27C` draws (2026-10-31, from the code; data::WaterWindows):
the height `h` is `-$13AD68`, the camera's height in **world units**
(rounded up; read from recordings: -646 for a camera y of 165,172/256),
times 128 / zoom when `$13AACD` is clear (zoom 2 or 4; bit 7 is set at
zoom 1) and h ≤ $7FFF0. (Earlier notes here took these thresholds as
32-bit units.) Above h $3F8 the coarse cells of a window around the
camera's coarse cell first (`$11E712`: 16 lists by the camera's quarter
of its cell, indices into the offset pairs `$11E6E0`; four cells from
`$11E812` up to h $5000). Then the fine grid: above $10000 the 5 × 5
cells around the camera (`$11E8A2`), above $9000 3 × 3 (`$11E852`).
Lower, the camera's quarter of its cell (`$13AB40` = (3 - x quarter) +
4 (3 - z quarter), quarters of 2^22 32-bit units, from the record's
cell words +6, +8, `$10F63C`) and the heading's eighth (`$13AB44`,
`$119A4C`: `$13AD7E` or the outside camera's `$13AD86` / $E10) pick a
list: `$11EFBC` (4 bytes a list) up to $3000, `$11ECBC` (6) up to
$4800, else `$11E8BC` (8), 8 lists per quarter. Up to h $3A0 a table of
four bytes per quarter (`$11F1BC`; up to $160 `$11F1FC`, up to $10
`$11F23C`) names the eighths that see only the camera's own cell (a
negative first byte: every eighth; `$11F488` is the list with that cell
alone, `$13AB8D` set). The other version of a cell is drawn: for the
camera's own cell when `$13AB8C` is set (`$113138` on odd frames: the
table `$113418` by cell z, cell x, a height at most or at least, the
quarter and the eighth); up to h $C80 for another cell whose centre lies
farther along x or z than `$11F5D8`[min(h >> 8, 17)] × 2^16 (32-bit
units; the x distance counts as near when its low word is 0, as the
original tests only the word); never in the view from above
(`$13AA9D`). **Checked** against the original's pictures of the demo's
final approach (frames 1840-1960) and climb (590-650): the land around
the runway stays green and the bay shows where it does; the recorded
`$13AB40`, `$13AB44`, `$13AB8C` and `$13AD68` equal the port's inputs.
1024 cells; the near versions draw the bay's coastline
(`.analysis/fa_18_interceptor_amiga/work/terrain.png`, the viewer's world map).

**How the water reaches the screen** (`$11F6E6` ... `$11F7C2`, then the
clipper `$118C04` and the blitter fill `$124B48`; 2026-11-01). The
water is measured from the viewed aircraft's own place (its record,
`$13B474` + `$13ABCE` + $14; `$13AF2E` in the outside view, which is
also `$13AD6C` there), not from the pilot's eye: 20 units ahead and 5 up
is a whole step at these sizes. Everything is in words: the place from
the cell's corner and the height in steps of 2^12 32-bit units (16 world
units; the height + $1000 first, so rounded) in the fine grid, 2^16 in
the coarse one; the corner's own steps added (16-bit); camera space =
(x m00 + z m02) >> 8 plus the height's part (−h m01) >> 8, taken down
apart, for each row of `$13AEC8`; then shifted left (`-$20(a6)`): 1 from
h $C80 down, 2 for the camera's own cell from h $400 down, 0 in the
coarse grid, still in 16 bits. The clipper's input shift `$14127C` is 0.
The clipper (`$118C02`; the water enters at `$118C04`, after its nop)
runs four passes, y ≤ z, −y ≤ z, x ≤ z, −x ≤ z, Sutherland-Hodgman on
words: a crossing is p + (c − p) k / den (k = p.z ∓ p.axis, den =
(p.z − c.z) ± (c.axis − p.axis)), each quotient rounded to the nearest
(a remainder at least |den| / 2 moves it one away from zero, also from
0 for a negative remainder), a `divs` that overflows keeping the product
(its high word then counts as the remainder); the crossing's z = ± its
axis value. No near plane: a polygon reaching behind the eye is cut by
the four sides only and can come out crossing itself; the fill of the
edge lines (exclusive-or lines, inclusive fill) then leaves only parts
of it, such as the wedge of water at the picture's bottom left on the
demo's approach (frames 1860-1880). Columns 319 − (160 + 160 x / z),
rows 179 − (90 + 90 y / z), the quotients cut toward zero and kept in
0-319 / 0-179 (`$119290`). **Checked**: a Python model of the clipper
(`.analysis/fa_18_interceptor_amiga/harness/clipmodel.py`) gives all 34 polygons' screen corners captured at the
clipper around demo frame 1870 (`harness/clipcap.py`: breakpoints `$118C04` in,
`$1192C4` out); the port's words for the recorded states 1869-1871 equal
the captured inputs (6 of 7 polygons; the port draws a second polygon of
cell (16, 16)'s other version that the original does not reach - cut
away in these frames). Against the demo's pictures (each picture shows
the state logged one frame earlier): water pixels that differ dropped in
28 of 29 frames (e.g. frame 1860: 2490 to 171, 1920: 1876 to 295; frame
195, looking back, 1055 to 1115); what is left is mostly the original's
HUD over the water and single pixels along the coast.

**The roads** (`$1197E0`, run once by `$101628` as the game starts,
2026-10-30): the program on the disk holds zeros for some of the ground
shapes' points; the game builds them. A list of word offsets from
`$139B74` (negative: the end) leads to runs of 20-byte records inside the
point blocks: a road's ends A and B (x, z), then three points to fill (x
`$FFFF` ends a run). v = B - A made 46 long (`$119CAE`, the length
estimate `$1108C4`), then A + ((-88 vx + 241 vz) >> 8, (-241 vx - 88 vz)
>> 8), A + ((-88 vx - 241 vz) >> 8, (241 vx - 88 vz) >> 8) and B minus
the first offset: the corners of the road's strip, drawn by the city
blocks' `$78` parallelograms and `$12C` lines. **Checked:** all 120 built
points equal the running original's memory (the port's `road_points`);
the city from the original's camera (`$13AD6C`, `$13AEC8`, read at a
frame start) matches the port's picture (`sceneshot` with
`F18_CAMERA`). Without them the city blocks lost their roads.

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
point offset; `$24` only with the camera low, `$13AD68` ≥ −$80; `$78`
sets plane 3, `$6C` inverts plane 1), `$34` lines sharing a colour
(colour, then pairs of point offsets, bit 15 on the last), `$12C` the
same lines only with the camera above −$C0 of `$13AD68` (`$114A20`),
`$08` and `$128` single lines (p1, p2, colour; `$128` only above
−$C0, `$11372C`). (`$68`'s colour 7 is the planes 0 and 1 set, `$1130E0`:
the same over land and water.) All decode since 2026-10-31.

**Bitplane modes** (`$13A9D6`, four words, `$124B48` → `$125066`): the
first word picks the planes the filler writes; with the second word
negative each gets its bit of the colour, else per plane the third
word's bit inverts it (blitter minterm $3C), or the second word's bit
sets it ($FC) or clears it ($0C) (a nonzero fourth word takes another
path, `$12500C`: the ground's specks). An object starts with plain
colours (`$112640`, `$112D7A`); ops $50, $54, $5C and $6C invert plane 1
(2, 0, 2, 0: the runways' white threshold blocks and stripes are the
paving inverted), $44, $48 and $78 set plane 3 (8, 8, 0, 0), $4C, $58,
$80, $E4, $34 and $08 go back to plain colours; faces and the other ops
paint in the mode the last op left. The port marks such faces'
colours (`data::invert_planes`, `data::set_planes`) and changes the
pixel's bits; it carries the mode in decoding order (the original's runs
in drawing order, which can differ when a facing tree reorders lists).

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
Rows run from 1 (`$1238AE` clamps a point's row to 1-180): row 0 of
every view picture is colour 0 (all the original's captures), as the
port draws it since 2026-10-31.
The camera is the fine cell corner minus `$13AD52` / 256 (`$13AD52`,
`$13AD56`, `$13AD5A`: minus the offset from the corner × 256). Model
points are scaled by 2^−(low nibble of model byte +6) (`$111E68`).
One-sided faces are drawn only when (v1 − v0) × (v2 − v0) · v0 < 0 in
camera space (`$112B1C`).
Polygons are clipped against the four sides of the view (`$118C02`:
|x| ≤ z and |y| ≤ z in camera space, after the precision shift
`$14127C`), not against a near plane: parts of the aircraft's own shape
a unit or two from the eye still show (the fuselage behind the cockpit
when looking back, demo frames 140-185, checked against the original's
pictures). The port clips at a depth of 1/16 and lets the picture's edges
cut the rest.

**Native view checked** (`viewcheck`, 12 frames of the demo,
`.analysis/fa_18_interceptor_amiga/work/ref/views/sheet.png`): horizon, sky, land and water, the coastline,
the runway markings and towers in the right places while flying.
Differences, open: moving objects (aircraft, carrier) are not drawn by
the check; it ignores detail levels and distance limits (as the planned
"no LOD" option) and leaves out the ground's specks (below); type 58 (a dark-green four-faced hill, model
flag `$10`, like types 86 and 104) sits at the carrier's starting point
and the original does not show it in these frames.

**The ground's specks** (`$11C1B4`, every frame from `$101212`, after
the land, the water and the coarse maps' passes `$10FA64` / `$10FA76`,
before the near map and the moving objects): a fixed pattern of marks
repeated every $800 world units over the ground plane, so it stays put
on the ground as the camera moves; only near a low camera.
- How high: `$13AB5B` (`$1043F2`, every frame) from the view's pitch
  over 8 (`$13AD78` the cockpit's view, `$13AD84` the outside camera's;
  down positive, 80ths of a degree): $259-$C4D (75-315 degrees) 3, from
  $C80 (looking up) 0, above $12C 2, above $64 1, else 0. The camera
  (`$13AD68`, world units below the ground, negated) may be up to $800
  (0), $D80 (1) or $1200 (2, 3) high.
- Which pattern: by the height's word, $80 or lower `$11C908` (96
  marks, worked eight times larger: positions, height and the
  triangles' table), $E0 or lower `$11CB4C` (80), else `$11CD30` (64).
  Each starts with its count and a distance limit (2, 3, 4), then x, z,
  kind per mark. The camera's place in its $800 tile
  (`$13AD62`/`$13AD66`: $400 − (($400 − x) & $7FF)) is added; a mark
  whose entry in the 32 × 32 byte table `$11C508` (by |z| / 256 and
  |x| / 256) is above the limit is left out.
- Kinds: 0-2 a dot, two pixels (the one to the left too, `$12420A`) for
  2; −12, −16, −20 a small triangle (corners at `$11CEB4`, `$11CECC`,
  `$11CEE4`, the eightfold pattern's at `$11CEC0`, `$11CED8`, `$11CEF0`;
  filled by `$124B48`) while the camera is below $200 and the mark's
  table entry is 1 or less, else a dot (one pixel for −12, two for the
  others).
- Placed as `$1238AE` places a point but without its extra row
  (column 159 − 160 x / z, row 89 − 90 y / z), kept only with |x|, |y|
  ≤ z and the row inside the view.
- Colour: `$13A9D6` = 4 (only bitplane 2 written) and colour 3, so the
  pixel's plane 2 is cleared: the sea (6) shows grey (2), the land (4)
  black (0) - the grey specks on the water and the black ones on the
  fields in the original's pictures (look-down after the demo's hit,
  `work/ref` capture 2026-10-31, pictures 615-669: 25-31 grey pixels a
  picture, the port about 30; demopics2 1840/1860: black specks on the
  land, as the port's).
- The port draws them after all flat ground (the near map's too) and
  before the shadows; it takes the pitch from the view matrix (asin of
  the view axis, 0-90 degrees: the original's 90-315 range for views
  past the vertical is not reached) and keeps the marks still between
  whole world units. Widescreen extends the column test to the wider
  picture.

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
`$13AB78`). The absolute value: pulling and pushing count alike, and
there is no separate negative-G effect (no red-out). Every real second
(`$119954`) the overload counter `$13AB79` goes up (at most 15) while
latched, else down to −1; above 5 the engine's volume is halved
(`$1054A8`) and the flight's screen procedure (`$102EFE` → `$103166`)
asks for the palette step `$13AB91` = 2 when the view is the player's
(`$13ABCE` 0) from the cockpit (`$13AA75` 0), otherwise 15 (`$103190`;
`$103922` resets the counter and the target at a restart). The
vertical-blank server (`$1092DE` → `$109440`) moves `$13AB90` one step
towards the target, a step down every second blank, up every third
(`$13AB93`), loading 16 colours from `$11A04C + (15 − step) × 32`
(`$109480`): 16 darker copies of the flight palette, each step one less
in every component, step 0 black; the display is one 16-colour playfield,
so the panel darkens with the view. At the target it stores the colours
as the view's own (`*$13A950`) and reloads them for three more blanks.
Step 2 is black in every colour: the blackout is a black screen.
Checked on the original (2026-10-31): free flight in the cockpit,
`$13AB79` poked to 15: `$13AB90` went 15 → 2 within half a second and
both copper lists carried the table's rows (12, 10, 3, then all zero).
Correction: this section once read the table *before* `$11A04C` and
reported no change of the flight view's colours; the captures then were
taken after the counter had run down. The port (2026-10-31): the target
set in the flying phase, the server's steps over 10/3 blanks a tick.
Also 10 is a target: the P key's pause (`$10DD84`, raw key $19 →
`$10EDC2`; ignored while `$13AB9E` is negative) stops every sound
(`$1015F6`), notes the system time (`$108E54`: the pause is left out of
the flight time, `$13ABF4`-`$13ABFC`) and sets `$13AA9E` = $FF: the
flight stands as for the missions' texts, the frame counter stands, the
sound update is skipped (`$104AE6`). `$102FE6` (or `$102D92` in the
descent from the map) dims the screen to step 10; `$103B76` waits for
the fade and queues block 38 ("- PAUSED -", row $2B, column $10, colour
9, a character every 6 frames, clicking); `$103BA0` waits while
`$13AA9E` is negative. P again (`$10EDD4` → `$10EF68`: the flag
toggled to 0, `$13ABA0` = $FE) clears the texts (`$103462`), turns the
sound update on (`$13AA85`), sets step 15 and goes back to `$102EFE`, or
`$102D58` when `$13AB9E` is 6. Checked on the original (2026-10-31):
`$13AA9E` $FF, fade 10, the frame counter standing, the screen dimmed
with "- PAUSED -"; the audio DMA bits off while paused and the engine's
two channels on again after. Quirk not rebuilt: P during a mission's
positive `$13AA9E` countdown toggles it to 0 as well. The port: phase
`paused` (P in flight or in the descent), the picture at the last
step's end while it stands (no smooth-motion interpolation, also for the
missions' texts).

Smooth motion and the fade (port, 2026-10-31): the flight's fade steps
over the 10/3 blanks of a 15 Hz tick are kept, and with smooth motion
each shows at its own moment within the tick; without it the tick's last.
No code posts "AT MAXIMUM ALTITUDE" (record 8): the
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
(runway 1): equal but for the carrier and the sea's specks (the specks drawn since 2026-10-31).

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
colour 3. From 50 on there is no cockpit. The aircraft's own shape is
drawn from the cockpit only for directions 3-9 (`$111CB8`: the player's
record with `$13AA75` 0 and `$13AA97` 3-9 is drawn with flag bit 0;
for any other direction the drawer takes the shape's second detail
pointer `$13AD2A`, which for the F/A-18 ends at once - nothing drawn)
and without a shadow (`$113984` draws it only outside). Looking up or
down (12, 13) shows no part of it: the demo's look down after its hit
(log frames 675-719) shows the sea with the falling wreck below
(`work/ref` capture 2026-10-31, pictures 615-669); the port drew the
fuselage and wings there until then. Checked against
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
step every third blank, `$109480`: colours 0-15 from the table of darker
copies after `$11A04C`), seven frames, blocks 88 ("YOUR VECTOR XXX FOR
BOGEY", the vector filled in) and 90 ("READY TO LAUNCH") typed, then the
flight with the recording from its first byte.

The seven frames (2026-10-30, recording demo_start: the bogey from the
menu's 1): `$13AA9E`, set at the demo's start (`$10215E`), is cleared at
`$101BDA`; then `$102D58` sets a five-frame countdown and `$102DB8` waits
it out before queuing the texts - seven whole frames the frame counter
counts (1-7, then it stands while the texts are typed and starts from 0
with the flight, `$102EC0`). The flight flag (`$13ABBC` bit 6) is clear,
so the others' object step runs its start (`$11797E`: distance, pursuit
point, pilot, timer) but the record step goes from `$11A440` to
`$11A572`: no autopilot (`$120B6E`), controls (`$10DFD2`) or speed step
(`$105ED4`), only the attitude step (`$121BE0`) and the first-step mark
cleared (+0 bit 8, `$11A5AC`), nothing moved. So the bogey moves in the
flight's first frame. The port skipped these frames, so its bogey lost a
move and a turn and the demo's shot came 65 frames late, the explosion
seen looking down; now the module runs them in `to_ready` (every start
through `$102D58`). Checked: the module's demo against demo, demo_objs,
demo_trail and demo_start - player and bogey equal in every frame;
`democheck` on demo_start (from the launch) 228 of 228.

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
looks 12/13, and the demo's world from its object list ("The missions'
objects"): the carrier at cells (68, 68) turned towards path entry $1A
(cells 70, 112: heading 28,600) and two type-$12 aircraft (records 10).
The carrier does not sail (ships get no "moves" bit; its speed is 0).
`fa_18_interceptor_assets democheck` flies the recording from the deck
against `demo.jsonl`: all 2,046 frames equal (2026-10-29), once the deck
test ran after the move with the place before it ("The carrier").

Block 88's "XXX" ($102DB8 → `$1195D4`, missions 3-8 and the demo): the
first aircraft of the list's entries that is active, of class $10 and not
type $15: the point 500 units ahead of it (`$10D498`, its view matrix,
products over 16) less the player's place, its heading ($10454A) / 80
in degrees, rounded to tens (5 up: the BCD add of 10), 360 → 000, typed
into the text at `$136162` + $11. Without such an aircraft the line is
left out. The port types 350 for the demo; **unchecked** against the
original's screen.

### The maneuvers (menu items 3 and 4, 2026-10-29)

**The demo (item 3, mode 2)** (`$102190`): the object list for mode 2
(`$11E032`) has the carrier and an F-16 (type 3 shape, type byte $10) in
record 4 on path $2E (cells 73, 78, height 12,288 << 8); the spawner
(`$11D318`) gives it the gear up, speed and airspeed $2000, level
angles, +$64 = $10. The view and the panel follow record 4 (`$13ABCC` 4,
`$13ABCE` $800): outside, position 3, distance 1; record 4's +2 bit 0
is set (`$1021DA`). The screen fades in (`$1023C2`), the check
(`$1027C8`) and briefing 9 (`$102452`, until typed or a key), then the
flight starts without a "ready" text (`$102DB8`: `$13AAD0` $FF;
`$102E4E` sets `$13AA83`), three frames, the beep.

Record 4's training step (`$1172AA`, in the object step before its
autopilot): the maneuver number `$13AA89` runs 1-7, each with 8 bytes
at `$11756A`: a long whose low byte is the camera's position and next
byte its distance (only set from outside), and a long $800000nn: the
role nn written to +5 (`$11744C`, `$14508A` marking it started). When
the role is back at 0 the control byte's stick bits go and the next
maneuver follows in the same frame (`$11739C`, `$117478`: its name from
`$117522` + 8n + 2 in the message line - $17 AILERON ROLL, $1A, $19,
$1B, $2B, $18 BARREL ROLL, $1C - or $35 "TOO LOW FOR SPLIT-S" for the
split-S below height $100000); after the seventh the objects are spawned
again. The first frame (`$117354`) sets the throttle to $60 and the
engine's start. Return asks for the next maneuver (`$10E8EA` sets the
player record's +2 bit 3; `$1172FE` respawns the objects and moves on).
Esc starts the selection again as the practice (`$101AF4`: mode 2
becomes $7D).

**The autopilot** (`$120B6E`, also the missions' aircraft): the record's
role byte +5 selects a handler (jump table `$1202D4`, 40 roles; Ghidra
reads the table as code, so this part was read from the disassembly).
Each frame: the timer +$4C counts down in the object step (`$1179A4`),
then the handler sets the control byte +$65 and may move on to the next
role in the same frame. The maneuvers' roles:

| Maneuver | Roles | What they do |
|---|---|---|
| aileron roll ($0B) | 11, 12, 13 | clear the progress bits (+$20 bits 3, 4), 15 frames wings level (+3 bit 4), a full roll at rate 800 (`$121432`[8]), let go within 22.5° of level |
| inside loop ($0E) | 14, 15, 16 | the same start, pull at -400 (`$12141C`[6]) round the loop until the nose is up again |
| split-S ($11) | 17, 18, 19, 15, 16 | only above height $100000; roll inverted, then the loop's pull (2 frames' pause) |
| vertical half loop ($14) | 20, 21, 22, 13 | pull up past the vertical, then the roll to level |
| barrel roll ($17) | 23, 24, 25 | roll at 720 and pull at -480 together (`$12135E` keeps the roll) |
| inverted flight ($1A) | 26, 27, 28, 30, 31, 33, 34, 22, 13 | roll inverted, nose up to the horizon and down again, then the half loop's end |
| break turn ($27) | 39, 40, 2, 7 | to the nearest knife edge (90° / 270°), then pull at -320 for 32 frames |
| (practice's start, $23) | 35-38 | throttle to $60, at airspeed $840 a minute's wait, pull to 32° up, level at height $240000 |

A maneuver ends at `$120A74`: a record with +2 bit 0 (the trainer)
waits in role 10 ($24 frames with `$13AA83` set, else $3C) and lets go
(role 0, the stick released, `$120B58`); in practice, while waiting for
the trigger (`$13AA8A` negative), it flies on in role 6. The helpers:
`$12126E` rolls (bit 3 for a positive rate, bit 2 for a negative one,
the pitch and rudder bits released), `$121362` pulls or pushes while the
pitch rate +$56 is short of the wanted one (`$12135E` keeping the roll
bits); every handler that steers sets +$64 bit 4 (`$1213FC`). In the
attitude step (`$121D4C`) roles 6 and 10 roll the wings level by the
frame's starting roll (side bits 4 / 8, none within 3.5°), for the next
frame. The record then flies by the player's own input, attitude and
velocity steps with two differences for records other than 0: the
response table is `$132834` unless +2 bit 0 is set (`$10557C`), and the
pitch rate eases off by `$105B7A(…, 1)` instead of 2 when the stick is
let go; the flight step's +$4C count of 2 a frame is the player's only.

Checked: `fa_18_interceptor_assets trainercheck` steps record 4 from
each frame of the original's maneuvers' demo (`work/ref/man.jsonl`,
maneuvers 2-7, `flightcap.py --objs`) - all 699 frames equal (role,
timer, control byte, +$64, +$20, position, angles, speed).

**The practice (item 4, $7D)** (`$102202`): free flight's start at
runway 3 (`$102CE0` → `$10D51E`) with one aircraft (`$13AB87` 1), the
F/A-18, briefing 10 (`$102496`) after the fade-in, then the camera's
descent from the aircraft itself (`$11DC7E`, descent state 3: only its
last frames) and "READY FOR TAKEOFF". The lead, record 4 (`$11D0AA`): the
player's record copied, type $10 (the F-16's shape), 72 units ahead
((view column 2 × 72) >> 4, `$10D460`), group +$5E 4, +$2C −1, role 0.
Until the player moves it sits (role 0, whose timer sends it to role 6
for a frame); with the player's airspeed not 0 (`$1172AA`, after the
player's step) its +2 bit 0 is set and it flies entry 0's roles 35-38:
throttle bits up to 96%, at airspeed $840 sixty frames' roll, a pull at
−40 until the nose is more than 25° up, then on until height $240000 and
the maneuver's end (role 10, $3C frames: `$13AA83` is clear here). Then
(`$11739C`) it waits: `$13AA8A` −1 and "TRIGGER FOR: <next maneuver>"
(`$117522` + 8n, first word); meanwhile a maneuver's end leaves it in
role 6. The fire key (`$10F0B6`, only while it waits: the gun is not
armed in the practice) sets its +2 bit 3: the next maneuver (its name in
the message line), flown at once; after the seventh the lead is copied
from the player again. Return while it waits (`$10E8D8`, the refusing
beep) puts the player beside it (`$117170`: its record copied with type
$11, +2 bit 0 and the role cleared, at (9, 1, −97) by its view matrix /
16) and shows the next prompt; otherwise "NOT YET RDY TO RE-POSITION"
($1F). Losing the lead (`$118AEC`) ends the practice with block 72 over
the view, 100 passes (`$103A4C`) and the main menu; crashing the one
aircraft ends it as in free flight. Esc starts it again.

Checked against the original's practice (`work/ref/prac.jsonl`, the
player rolling into the lead on the runway at full afterburner):
`trainercheck` steps the lead - 138 of 141 frames equal; the other
three are the hit lead's last frames, whose stick bits come from the
missions' path following (not built). The player's touch (below) agrees
on all 141 frames, the hit included.

### Objects touching ($11B6A2, 2026-10-29)

After each record's move the object step (`$11A7EA`) calls `$11B6A2`
with the record's position before the move. For every other active record
(not crashed or exploding, state & $600; not of the same group +$5E - an
aircraft and its missiles share one; one of the two with state bit 3)
the two types give two half sizes in 32-bit units: type $15 $5000 /
$1000; ships ($2x) the deck and hull tests (the carrier, "The carrier");
parachutes ($3x) none; a missile, or two of different classes, $10000 /
$5000; two aircraft $7000 / $1000, or $6000 / $800 when one is flown as
the training's (+2 bit 0). Within the first box (all three axes) the
other is near (its +2 bit 0, `$13AB8F`); within the second, now or half
its move back, it is hit: a moving one (state bit 12) is crashed (state
bit 9) and, an aircraft, spins (+$20 bit 1) with +2 bit 12 and role 1;
both get +$20 bit 7 when the mover has state bit 3 (`$11C0BA`, which also
counts missile hits in the log). The result is $40.

What the mover does with it (`$11A88E`): the player's aircraft gets state
bit 15 (no turning while it lasts) and, faster than $3C0 (with `$13AA7C`
set), by the frame counter's bit 0 either "WARNING: TURBINE FAILURE"
($9014, blinking) and a spin - unless flares or chaff are out - or "MINOR
HYDRAULIC DAMAGE" / "MINOR STRUCTURE DAMAGE" ($D00B / $D00C by bit 1).
Another record spins, takes +2 bit 12 and role 1, and is taken off at
once (`$11AA70`). Without a touch (`$11AA80`): a spinning aircraft on the
ground blows up (`$11AA4A`: state bit 10, 15 frames) - for the player the
crash; else, for every class but the pod's ($30), state bit 15 is
cleared (in control again), a crashed record (state bit 9) blows up here
already (bit 9 cleared, 15 frames), or near sets the mover's +2 bit 1
and its absence clears it (`$11AAE0`). A record crashed outside its own
step becomes exploding at the frame's end (`$1169D0`: 15 frames; it
makes one more move, then stops), and spins its speed toward $1200 by
$90 (`$1179C0`); when the 15 frames are out (`$11A398`) the cruise
missile (type $15) and class-0 records are always taken off; an aircraft
(class $10) not touchable (state bit 3) and not on the ground counts as
destroyed (`$13AB9B`); it is taken off if it is on the ground or has
state bit 15, else it falls on spinning - so a record crashed in the air
is in control again by its next step and flies on (recordings m4a, m8b).
The frame counter `$13ABCA` starts at 0 with each flight (`$102EC0`).

Built for the aircraft records the port has (the player's and the
practice's lead), with the explosions, smoke and trail ("Explosions,
smoke and trails"); scenery boxes (`$142A7C`'s lists) and the falling,
burning aircraft's steering are not.

### Explosions, smoke and trails (2026-10-30)

What a record blowing up shows, from the drawer (`$111D5C` for the
records, `$11278C` after their model) and the object step. Built in
game/scene.cpp (drawing), game/object_world.* (smoke slots, the trail's
ring), game/weapons.cpp (a missile on the ground); the tables are read
from the program (data/shapes.cpp `read_explosions`, `read_smoke_shape`).

**The explosion** (`$1127C2` → `$12185E`): a record with state bit 10
($400) and its timer (+$4C) from 15 down to 0 shows its explosion,
15 - timer frames old: after its model while the timer is 13-15, instead
of it from 12 on (`$111FBC`). Checked with a breakpoint on `$12185E`
(scratchpad trace of a missile hitting the ground): called from the frame
of the hit (timer 15, age 0) to timer 1, one older each frame. The
pieces ($121948): 12 per burst (9 for the cruise missile's discs), each
thrown out along a direction of bytes times the age (world units: tables
`$12174A`, `$121771`, `$121798`, `$1217BF`) and drawn as a triangle of
three corners from eight shapes (`$12169A`, 24 units across) chosen by
piece number + age (`$12181A`), in the burst's colour (`$1217DD` ...).
Kinds by the record: 1 over land (colour 9, orange), 2 over water (+4
bit 1: colour 2), the cruise missile (type $15) 3 - nine grey discs
(colours 13 and 10 by turns) of radius ($2800 + $2800 x age / 6) over
the distance, at most 127 - then 4 a frame younger and 5 two frames
younger and moved by (20, 10, 20), both orange triangles. A piece shows
only when every corner lies in the view's pyramid (|x|, |y| within the
depth); its rows are 89 - 90 y / z (one above `$123894`'s), filled
row by row over every row it spans (`$124B48`: a sliver keeps a pixel a
row). Not drawn more than $8000 units above or below the camera.
Pictures (`flightcap.py --pics`) against `effectshot`: sm4 (a missile
on the runway seen from the cockpit, frames 26-42) and demo_trail2 (the
demo's bogey, frames 552-564) - the bursts' timing, spread, size and
colour agree. Far away (a coordinate
past $4000 or $40000 units) the cruise missile's explosion is worked out
16 or 256 times smaller, which makes its discs that much larger and its
triangles too (their corners are shifted twice) - kept.
At timer 14 the drawing also sets the explosion's noise: `$13AB59`
$10000 over half the drawer's distance (16-unit steps), at most $78,
`$13AB5A` half that, `$13AAA8` 4 - `$13AB5A` / 16 frames to wait; then
`$104DD4` plays `$10A0DC` with that volume (half in the cockpit).

**Missiles blow up too**: the missiles' "fading" bit is state bit 10, so
over its last 12 frames a missile is drawn as its explosion in the air
and touches nothing more (`$11A7DA`). Below the ground (`$11B6A2`'s
result $10: the fine height +$10 below 0, after the records' tests; $20
is a scenery box, $40 a record) a missile goes back along its move onto
height 0 (`$11AB0E`: x and z less the move's ratio to its height part,
in 16ths through a word division, times the depth), `$11F83A` marks
water (+4 bit 1), and it blows up for 15 frames ($11AA4A, +0 bit 12
cleared: it stops). On land it leaves smoke. Checked: sm2, sm3, sm4
(`weaponcheck`: the hit, the put-back place, the explosion's frames, the
smoke slot's number, type, place and timer, every frame but the launch
frame the height hold pokes). A cruise missile that touches a record
blows up instead of being taken off; an aircraft that does spins, burns
(+2 bit 4) and is the trail's before it goes ($11AA04).

**Smoke** (`$11AB3E`, slots `$13D474`, 16 of 32 bytes): the first free
slot gets a copy of the record's first 32 bytes - its cells and fine
place as the move left it (before the put-back) - the shape $11 (in the
air: a scenery box's hit) or on the ground $13 / $12 by the slot's
number (even / odd), `$13AB48` $FF and the timer $7F. The slots' timers
run down a frame at the object step's start (`$116B82`, not while an
outcome is awaited) and past 0; a slot is never freed during a flight
(cleared at its start, `$10D248`), so a flight shows at most 16 smokes.
The shapes are object types 33-35 (the draw table's entries 17-19 past
the records': `$11608C` + 20 x type): near (< $B0 x 16 = 2,816 units) 34
and 35 a black patch with cracks on the ground (mirror images), all three
a column of grey discs (points 12-17, up to 106 units high, colours 8
and $A) chosen by op $118 (`$1135F0`) from the timer: one of four sets
$86 bytes apart by (timer & 7) in pairs, the whole column; above $74 the
single discs from the top down, fewer the younger (it rises over 11
frames); below 12 from the bottom up (it goes); a negative timer draws
nothing. A disc ($F8, $11C's list: `$11582A`) has radius R over its
point's camera-space length times 2^2 (the model's scale shift), at most
127; `$123DC4` fills 2r rows from y - (r - 1), half widths from the
midpoint circle down to the middle and back. Far (to $380 x 16 = 14,336
units) 34 and 35 show a two-pixel dot in colour 3 ($E8, `$12426E`).
Pictures: sm5 (the column from outside) against `effectshot`: the same
puffs, sizes and shades.

**The trail** (`$11587C`, `$11AD0A`): the last aircraft hit (`$1450BA`,
set with +2 bit 4 by a touch `$11B87C`, the third cannon hit `$11B364`
or a hit mover `$11AA04`) leaves a smoke trail. After its step every
frame (not while an outcome is awaited) its place goes into a ring of
six (`$1450BC`, next slot `$1450B9`, count `$1450B8`; while fewer than
five into the next slot too); burning and moving (+$6E) its length +$3D
is the count less one, at most 4, else it runs down by one and at 0 the
ring starts over. Drawn with the record: with the ring full the places
from the oldest, length + 1 of them, else the first `length`; discs of
$9C0 over the distance in radius, odd slots colour 8 and even $A (3 and
$C while it spins), with a length over 1 three more between each two (a
quarter, a half, three quarters, the later one's colour); painted oldest
first when the aircraft comes toward the camera, else newest first, the
last one an eighth smaller. Checked: `trailcheck` on demo_trail (the
demo's bogey hit by the player's missiles).

### The enemies' missiles and the threat warnings (2026-10-30)

**The missile records**: in the object step (`$116C4E` ...) each enemy
aircraft of records 8, 10 and 12 has the record after it (9, 11, 13) for
its missile (record 4's, 5, is the practice's; record 7 is never
stepped). A free one is offered a launch before its own step:
- `$1170A2`, the decision: the aircraft in control (state & $8700 clear),
  not spinning, active; its target, when a record (+$38 bit 7), active,
  not spinning and without state bit 0; its pilot byte in the attack
  (+$64 & $60 = $60) and its state bit 0 set (it follows a record);
  a missile ready (+$63 high nibble $20 Sidewinder or $30 AMRAAM, chosen
  by `$118468` from the distance to its target: within $300 the gun -
  never fired by the enemies - within $C00 a Sidewinder, within $1800 an
  AMRAAM, else none). `$13AAAC` would force it; nothing sets it.
- `$11767E`, the pace: `$13AA79` (1 in every recording, no writer found:
  taken as set) and the frame counter's low bits clear - every 8th frame,
  every 6th from level 2 (mask 5), every 4th from level 3; with the ECM on
  (`$13ABA5`, key J) the mask is halved (more often).
- `$1176B8`, the stores (+$5F: $44 at the spawn, `$11D58C`; Sidewinders
  high nibble, AMRAAMs low): none left, no launch.
- `$1176D8`, the launch: the player's own - the aircraft's record copied,
  type 0 / 1, the station by the count left, life 150 / 200, +$26 $14,
  state |= $11C2 - with the shooter's target (+$38) and no place yet
  (+$2C -1), no pilot byte. The new record is stepped in the same frame.

An enemy takes the player as its target when the player comes within a
box (`$117F7E`: $180000 / $240000 / $300000 by the level; port
`ObjectStep::follow`).

**Their flight** (`$117E4A`): the player's missiles' step; a missile with
state bit 3 (the player's, touchable) homes on the radar's lock, any
other on `$1182F2`: state bit 0 set, the place of its target record
(+$38 & $7F); on the player the latest flare (`$13AB7C`, Sidewinders) or
chaff (`$13AB7D`, AMRAAMs) instead - a projectile slot in 64-byte units
plus 1, set when it is fired (`$107438`), cleared when it ends
(`$1077A0`) and at a new start (`$10F1BA`) - whose nearness (within
$4800 on each axis) sets +2 bit 0 and whose loss +$20 bit 5. Their
touches as the other records' (a hit takes the missile off, `$11AA70`;
a missile reaching the ground blows up there, with smoke on land); the
player hits them in its own touch (before the records' step): the
player's damage ("TURBINE FAILURE" ...) and the missile crashed, blowing
up at the frame's end. Nothing touches while an outcome is awaited
(`$11A364` skips the record step, the player's too).

**The threats** (`$126114` in the radar's panel pass, `$13AB5D`, copied
to `$13AB5E` at the pass's end `$12631E`): for each contact within
$10000 on both axes, not spinning, crashed or blowing up, not on the
ground, not a ship or a pod: an enemy missile (class 0, not touchable)
on the viewed record (`$13ABCC`) sets bit 4 (Sidewinder) or 5 (AMRAAM);
a touchable aircraft bit 1, the cruise missile bit 3, any other aircraft
bit 2. New bits ask for warning beeps (`$13AE44`: 1 for a missile, 2 for
the cruise missile or an enemy - and `$13AB77` $18 - $800 for a friendly
but in mission 5). The message picker (`$103D4C`) shows them: $8010 /
$8012 (missiles) before the fuel and stall warnings, $8024 (cruise
missile) and $800E (enemy) after them, $400D (friendly) last. The
panel's lamps (`$125D26`, every frame with the panel): two two-pixel dots
each in colour 3, lit by the threats - an enemy at (293, 156) colour 1, a
friendly at (298, 156) colour 4, a Sidewinder at (298, 159) and an
AMRAAM at (293, 159) colour 2 - and the stall (+$20 bit 2) at (303, 159)
colour 1 on frames with bit 1.

Checked: em4a (an enemy's AMRAAM in flight to Air Force One: 43 of 43
frames), em4b (record 8 near the player takes it as its target, launches
a Sidewinder at frame 321 - stores $44 → $34 - which hits the player at
345: 419 of 419 frames, `missioncheck` with the missile records and the
player's touch); the threats against the demo (`democheck`, bit 2).
em4c (flares every 18 frames: of three Sidewinders the first two follow
the flares and end in the ground, the third, after the last flare,
hits the player: 599 of 599 frames). A missile diving below -$200 is
marked on the ground by its flight step (+2 bit 7) with its height and
climb kept; its ground test then blows it up (state bit 15, the spin
bit, `$11A996` / `$11A9DE`).

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

The deck test (`$11B6AA`, the carrier part; each frame after the move
but with the position from before it, for the player and the other
aircraft; corrected 2026-10-29 — the demo's take-off showed it: the
aircraft still rolls one frame after its tail point left the deck): byte +4's deck bits are
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

The test's arithmetic (2026-10-29, `$11B8C6`-`$11BB98`): everything in
words of fine position (the position within its cell / 256; cells are
ignored). The points are the records' turned model points at +$A4 (6
bytes each), which the drawer writes (`$112006`: each model point times
the record's view matrix / 64, products over 256) and the test shifts by
the record's scale nibble (+$7D). The aircraft's are its points 0 (+$A4)
and 3 (+$B6). A point at or above the first deck point's height is on
the deck when, for every edge p0 → p1 of an outline, −(p1.z − p0.z) ×
(P.x − p0.x) + (p1.x − p0.x) × (P.z − p0.z) is below 0 (the edge
unshifted, p0 shifted and placed); +$4E is that point's height. The
wire zone's tests have the same form; the heading test is the dot of the
two records' view matrices' third columns (x and z). The port turns the
carrier's points from its own record (`Carrier::place`), so a carrier
at any heading works.

### Weapons against objects (2026-10-29)

**Missiles home** (`$117EC2` → `$117EF8` / `$117F06`): a launched missile
copies the player's touchable bit (+1 bit 3) and has +1 bit 1, so until
its last 50 frames each frame its point is the radar's locked record's
place (+$38 = record | $80); with no lock it glides toward height 0 and
$4200 (`$117E8E`). Its autopilot steers it like any record (role 0,
`$120C66`): it counts +$26 down from 20 and steers from 9 on (others than
aircraft: the rate tables `$121406`, the rudder bits and the turn words
+$50 pitch, +$52 yaw, +$54 roll). Its attitude step (`$121C6E` / `$121E12`
for others than aircraft): on course by the pilot's bits it rolls level
at ∓$3C0 and turns no other way; else its turn words over +$26 (at most
15; 10 with +$20 bit 5, 5 against the player when `$13ABA5`), each rate
eased halfway twice (`$121E74`), then the rotation and the velocity step.
The autopilot runs on the launch frame too; only the move waits. Checked:
`weaponcheck` on `work/ref/demo_w.jsonl` (the demo with `--objs --recs
10,14`): the missile in all 37 frames from launch to impact, including
its touch.

**Touches** (`$11B6A2`): a record looks only at the records after its own
(`$11B6E4`); after its move a missile touches the bogey ("near" one frame,
then the hit: the target crashed, spinning, role 1). The mover that hit
goes at once (`$11A996` → `$11AA70`: state bit 6 cleared, `$13AB48` $C) —
missiles and aircraft alike. `$11C0BA`: +$20 bit 7 on both when the other
is touchable; the log counts Sidewinder (+$44) and AMRAAM (+$40) hits of
the player's group on something not touchable, and missile hits on the
player (+$48). The others' "near" bit (+3 bit 0) lasts one frame: it is
cleared for records 0-14 at the object step's start (`$116BE4`, after
the trigger areas, before the player's step).

**Crash, explosion, fall** (`$1169D0` with the drawing; `$11A398` at the
start of a record's flight step): a crashed record ($200) explodes for
15 frames ($400, timer 15); if it was the locked record the lock goes
(`$118588`: message $4016). When the timer runs out the explosion ends:
an aircraft neither touchable nor on the ground counts as destroyed
(`$13AB9B`, which the default outcome `$118B1C` compares with
`$13AB9A`); on the ground or out of control (+0 bit 15) the record goes
(state bits 6, 14, 13 cleared, timer 20); else it falls on, spinning: in
role 6, speed toward $1200, its stick pitching it down. A spinning
aircraft that reaches the ground stops and explodes (`$11AA80`). The
spin's own nose drop of the stall model is the player's only
(`$121FAC`); others pass through the stall-speed test. Checked:
`democheck` on `demo_objs.jsonl` with the player's weapons from the
recording's keys: player, bogey and radar equal in all 2,018 frames —
the lock, the launch, the homing, the hit, the lock dropped, the
explosion, the fall, the crash on the ground and the record's end (the
stopped record's vertical velocity is the original's last one; the
port's ground step zeroes it, without effect).

**Rounds** (`$11AE96`'s object part, before the round's move): the
drawer's list of the frame (`$1449B2`, $18 bytes an entry, walked back
from `$13ACB6`), at most six records drawn in the round's cell, not its
owner (+$2E), active; a box round the record ($140, `$13AC96`; $800 for
models marked $10 at +7, the ships); inside it the round goes on in
eighths of its first move (+$18) and at each is tested (`$11B29A`):
ships by their hull (`$11B4A8`), others by their hit model (`$11B572`:
+$7B, the drawn model's byte +1 — high nibble taken off the scale
nibble, low the entry of `$12DC64`, bit 7 none — a list of faces, three
point offsets into the record's turned points (+$A4, the drawn model's)
each: bit 14 the normal as the cross product of the two edges over 16,
else as the difference of the second and third point; inside when every
face's dot with the round's way from the first point is at least 0).
None hit after eight: the whole move (+$C) is taken off again
(`$11B2EA`) — since the rounds' fall shortens +$C, the round ends $100
higher per frame of fall than it started. A hit stops the round there
(sparks, life 2, `$1075A6`), adds a cannon hit to the log (+$3C) and one
to the record's damage (+$3C low nibble); the third crashes it (an
aircraft spins, role 1) and leaves the count at 2.

The drawn model is the detail level the drawer takes (`$111D8C`): the
first level whose threshold (16-unit steps, >> `$13ADA8`) is beyond the
distance `$13AE30` (by the zoom `$13AD32` / $80). The F-16 (type 3), for
instance: level 0 (17 points, hit byte $00) below 23, level 1 (10
points, $21: hit model 1 at one shift less) below 40, then 104 and 384.

Checked (2026-10-29) with the practice's lead (record 4, the F-16 shape)
standing at a poked place ahead of the player and `$13AB96` poked to 1
so Return selects the gun (`work/ref/gun1-3.jsonl`, `flightcap.py
--objs`; scratchpad scripts place the record and fire): 254 units ahead
(level 0: hits, damage 1, 2, the crash, the explosion, the record gone),
452 ahead (level 1: hits at its nose, which level 0's model would miss)
and 254 ahead 30 up (a near miss: no hits). `weaponcheck`: every round,
hit, damage count and crash, and the port's level against the recorded
+$7B, equal in all 47, 42 and 44 frames. **Hypotheses left:** the
drawer's distance taken as the length estimate of the place less the
player's in 16-unit steps (x and z the flat pair), `$13ADA8` 0; the
records in record order for the drawer's list (records not drawn — out
of view — are tested too); the ships' hull test as their box. A record
the port spawns but the original never links into its map is not drawn
there and cannot be hit (a record poked in by hand was neither drawn nor
hit).

### The missions' flow (2026-10-29)

**Choosing** (`$10EACC`): menu keys 1-5 and 8 always; 7 and F1-F6 only
once qualified (config +0). 7: the last advanced mission (config +6) +
1, from 3 again outside 3-8, `$13AA82` 1 (its line 107 shown). F1-F6:
missions 3-8; 3 always, mission m when config +$12 + (m − 1) is not 0.
Item 6 (`$1022CE`): block 64 ("SELECTABLE MISSIONS ... F1 - VISUAL
CONFIRMATION MISSION"), then for each mission 3-8 whose config +$12 + m
is not 0 the next of hunk 64's first words (blocks 77-81, the F2-F6
lines), block 110; the menu's keys go on. So config +$12 + m is mission
m's stage (0-3) and also opens mission m + 1. The port takes F1, F3, F5
as the C64 has them, F2, F4, F6 with Shift, and Ctrl+1 - Ctrl+6.

**Start** (`$10201E` for 3-8): the radio noise (`$109D7A(40)`, not
built), three aircraft, the objects (`$11CF06` with the stage and the
level `$13AB97` = config +2, `$10D170`), place 4 (the carrier) — mission 3
place 1 (runway 1, `$1021F8`) — with the F/A-18. `$102534`: the screen
fades in; chosen from the menu (`$13AB1B` clear): block 91 (NORAD
STRATEGIC COMMAND) and the mission's first briefing (its stage 0) or the
later one (`$119508`: per mission five blocks from `$133FFE` +
`$1340DA`: mission 3: 26, 27, 95, 29, 30 — first briefing, later
briefing, the security check, the orders, success; 4: 31, 32, 95, 34, 35;
5: 39, 40, 95, 42, 43; 6: 46, 47, 95, 49, 50; 7: 52, 53, 95, 55, 56; 8:
59, 60, 95, 62, 63), the security check (`$1027C8`: passed — the disk is
cracked; the port leaves it out), block 93 (CLEARANCE CONFIRMED) and the
orders, missions started + 1 (config +$36, `$1028DA`); then the camera's
descent, block 88 (the vector) and "READY ..." as free flight's. The
aircraft question for mission 3's runway start (`$102C36`, block 75) is
not built (the port flies the F/A-18).

**Outcome** (`$1185BE` per frame of a mission, by mode). Success sets
`$13AA88` $FF, `$13AB1A` 3, `$13AB1C` 4 and `$13ABB0` (`$118B5E`: 4 for
Air Force One's success, 7 for the cruise missile's failure, else 0 —
mission 8's path leaves a stale register there, negative in the
recordings); the flight goes on until `$13AB1C` runs out (`$101922`),
then `$13ABB0` frames more (`$1031C8`), then `$13AA9E` stops the objects
and `$1031F4` types the success text (index 4) and block 85 ("RETURN TO
BASE", mission 3) or 86 ("RETURN TO CARRIER"): 5 frames flown after the
outcome's frame, 9 and 12 with the camera's turn (all 13 recordings);
the flight then goes on (`$102EFE`, `$13AA88` 1). Failures alike.
Several objectives first count `$13AA9E` down (5 or 2 frames), the
objects standing meanwhile. While `$13AA9E` is set, the frame counter
`$13ABCA` stands (`$1014A6`: such a frame keeps its number), the
flight step's climb is held (`$106D30`: the vertical move 0), and the
outside camera stands (`$11D83C`).

**The camera's turn** (missions 4 and 7): the frame `$13AA9E` reaches 1
(`$1186E2`, `$1189E2`), once a flight (`$13AAAE` bit 7), the view's
position goes to `$13AAAE` and its distance (`$13AA75`) to `$13AB24`;
with record 4 still in use the camera takes position 7, the view from
above ends, and from the cockpit the outside view is asked for
(`$13ABB6` bit 1, switched on next frame by `$1041E8`) at `$13AB23` = −1
(Air Force One) or 3 (the missile), `$13AC1A` its heading. With the
outcome (Air Force One's success, the missile's failure) `$10EC3C`
makes record 4 the viewed record (`$13ABCC` 4, `$13ABCE` $800): outside
the distance becomes `$13AB23`; the chase (`$11D8F2`) follows it with
the offset table by its type ($11 `$11D770`, $14 `$11D7AC`, else
`$11D734`), and the outside view's numbers (`$126AB2`) are its own
(heading, knots, feet). From the cockpit (the record gone the frame
before) the view would take the record's eye — not built. After the
texts (`$1030A2`) the view is the player's again: `$13AA75` from
`$13AB24`, position from `$13AAAE`, look ahead. Checked: m4b, m7a,
turn7 (`missioncheck` compares the turn, `$13AB23`, the view and
`$13ABB0`); the camera's place in m7a's last frames is record 4's plus
the F-16 table's position 7 at distance 3 turned by its heading, to the
unit; the camera takes that place at once, but its angles (`$13AD84`,
`$13AD86`) turn from their old values a quarter a frame (m7a: pitch
1149 → 1062 → 997 … towards 817). The original's picture at the turn
(`screen.py`) shows the cruise missile's explosion - grey discs and
orange triangles, kinds 3-5 of "Explosions, smoke and trails" by their
look (not compared pixel for pixel). The chase also flips a side view (positions 1-3, 5-7) to the other
side when the viewed aircraft's heading jumped by 90-270 degrees since
last frame (`$11D8AC`, against `$13AC1A`).

- 3, visual confirmation (`$118650`): the radar's `$13AB5F` < 0.
- 4, emergency defense (`$11867C`): once Air Force One (record 4, type
  $14) has a place (cell x or fine x not 0), it on the ground below speed $320 with records 8 and 10 gone — or
  destroyed: −3 when it bore +$20 bit 7 (touched by something touchable:
  block 76 "YOU HAVE DESTROYED AIR FORCE ONE", the flight ends), else −2
  (block 37 or 36 by the frame counter's bit 0). Meanwhile the view turns
  to it (below).
- 5, stolen F-16s (`$1187BA`): records 4 and 6, touchable while stolen;
  the first in use: east of cell $70 → −2 (block 44, the flight ends);
  within a box round it (by the level $30000, $24000, $18000) the counter
  `$13AA72` (set to 200 whenever outside) runs down, then both head for
  their path entries +$3A (home); then — or with neither in use — the
  aircraft destroyed (`$13AB9B`) must reach `$13AB9A` (which grows by
  one for every stolen F-16 that turns attacker, `$1181BE`).
- 6, rescue (`$118914`): the rescue pod (`$13ABB2`) down (+0 bit 15) and
  its timer out: within $10000 of record 11 (the downed pilot's raft) in x
  and z, else −2 (block 51). The pod: Shift-F (`$10EE76`, mission 6 only,
  none dropped yet, a missile record free) sets the player's +$7C low
  nibble to $F; the missile pass (`$116FE8` → `$117616`) makes the free
  record a copy of the aircraft, type $31, shape 11 ($DC / 20), 3 units
  below (`$117926`), pushed down at $3C0 (`$11788C`), timer −20. Its step
  (`$117C82`, class $30): drag on the drift (by 1/64 while the timer is
  below 0, then 1/4, 1/8 from its third frame, while the drift is above
  $F0 less the timer), the fall toward −$300 (−$3C a frame, −$3C0 once
  rising after its third frame); down, it drifts ($C a frame up to $C0 in
  x) and its timer runs out (clamped to 4). Its move (`$11A5C6`): y +
  the fall; at 0 or below it is down (+0 bit 15) at height 0, where it
  was before that frame's move, and moves no more (recordings m6a, m6b).
- 7, cruise missile (`$118998`): once placed (cell x or fine x not 0),
  record 4 (type $15) gone or its
  autopilot let go (the end of its path, state bit 7): shot down
  (spinning) → success, else −2 (block 57, then 92 or 58 by the frame
  counter's bit 0; with 58 the flight ends). The view turns to it
  (below).
- 8 and others (`$118B1C`): `$13AB9B` reaches `$13AB9A`. The cruise
  missile area 3 spawns (mission 8 only) counts towards `$13AB99` but
  never as destroyed and is not spawned again, so at level 0 (limit 4,
  the list's bandit, the missile) the fourth kill must be area 2's bogey
  coming back while the player stays in its box (recording m8c).
- A crash of the player's aircraft (`$11A982` in the air, `$106FC2` on
  the ground) adds 1 to the log's crashes (config +$46) and sets
  `$13AA88` 4: no outcome and no landing after it in that mission; the
  restart (`$10D950`) keeps any outcome at 4. (**Hypothesis left:** the
  touch path skips this when the touch result has bit 6, not traced.)

After success, landed (`$118BA2`: +$21 bit 0 — set in the
take-off/landing step `$1071AE`, **hypothesis**: the aircraft has flown —
on the ground, on a runway (+4 bit 2) or, started on the carrier, on its
deck (+4 & $C0), airspeed 0): `$13AA88` $FC → blocks 84 ("YOUR LOG NOW
REFLECTS THIS COMPLETED MISSION") and 83, `$1034A0`: config +6 the
mission, +7 its stage, completed + 1 (+$38), the level and the mission's
stage + 1 (at most 3), the record written. A failure that does not end
the flight adds 86 and 83 and the flight goes on (`$13AA88` 2: no
landing check). The trigger areas: "The trigger areas".

**Checked against the original** (2026-10-30; `missioncheck`, below):
each recording, made with `flightcap.py --recs` from `mstart.py`'s
start, is run frame by frame from the recorded records and counters
through the port's `ObjectWorld` (trigger areas, near bits, the records'
step after the player's recorded move, the objective) and compared with
the next frame: counters `$13AB99`-`$13AB9B`, `$13AA72`, `$13AA9E`,
`$13AA8D`, the outcome `$13AA88`, every recorded record.

| recording | what | frames equal |
|---|---|---|
| m4a | mission 4 from its start: Air Force One's flight and landing (record 4), the bandit (8); the bandit crashed in the air flies on | 2,896 / 2,897 |
| m4b, m4c | mission 4 success (bandit taken off: 5-frame wait), failure (Air Force One taken off: −2) | 11 / 12, 25 / 26 |
| m5a | mission 5: the stolen F-16s (4, 6) and the bandit | 1,499 / 1,499 |
| m5b | the player beside record 4: `$13AA72` 200 → −1, both turn home; bandits crashed → success | 270 / 271 |
| m5c | record 4 east of cell $70 → −2, the flight ends | 24 / 26 |
| m6a, m6b | the pod dropped above the descending pilot (record 11): too far → −2; near → success | 77 / 77, 377 / 377 |
| m7a | mission 7: the cruise missile's whole path to its end → −2 (flight ends) | 1,854 / 1,854 |
| m7b | the missile hit (spin, out of control, exploding) → success | 15 / 16 |
| m8a | mission 8, the player in area 3: the cruise missile spawned into record 4 | 299 / 299 |
| m8b | missile and bandit crashed: the missile goes uncounted | 197 / 199 |
| m8c | the player in area 2: the bandit and area 2's bogey (record 12, back twice) destroyed → success | 153 / 161 |

Every frame that differs is a poke's frame (the poke writes a position
long or a state the original's step had not produced; cell words +6/+8
not updated) — recipes in "Research tools". Found and fixed by them: the
cruise missile's fixed height (`$11A662`: a moving type $15 is set to y
$B000 after its move), the explosion's end and the touch step's no-hit
path ("Objects touching"), the pod's landing, the player's crash ending
the objectives. Not recorded: −3 (Air Force One destroyed by the
player), the leave rules for records heading for a record or a path
point.

### The missions' objects (`$11CF06`, 2026-10-29)

`$11CF06` (each flight's start, after the choice; maneuvers' $7D has its
own set-up `$11D0AA`): the list for `$13AB96` ($7E and $7F: the tenth),
entry `$11DFB2` + (list − 1) × 12 + variant × 4: (offset from
`$11DFB2`, the most aircraft `$13AB9A`). The variant: flown again
(`$13AB1B`) the one kept in `$13AE0C`; else `$13ADE8` & 3, 0 and 1 both
the first (the draw minus 1 is kept). The list starts with a word offset
per stage (`$11CFBE`: the configuration's byte +$12 + mode; 0 for $7E /
$7F); `$13AA2A` keeps the stage's first entry. Every entry whose level
(flags & $7F) is at most `$13AB97` is spawned (the first always), then
turned towards where it heads for (`$11CFE4`, `$10454A`'s heading, pitch
and roll 0).

An entry is five words: the type record's offset into `$115F4C` (20
bytes; copied to `$11608C` + record × 20; the model's scale nibble goes to
+$7D), the type byte (+$62), "heads for" (bits 0-6 the record; bit 15: bits
8-14 × 2 a path entry to head for, 0 none; else bits 8-14 a record whose
place it heads for, +$38 = record | $80; bits 8-14 also go to +$3A), the
path entry of its place (`$11DDC0`: cells x and z, offsets, height) and
flags: bits 8-11 only in that mode; $8000 touchable (+1 bit 3; else an
aircraft of class $10 counts towards `$13AB9A`, and once the count
reaches it no further entry is spawned); $4000 role 8; $2000 +1 bit 0;
$1000 scattered by the random: cells x ± ((r >> 1) & 3) (sign bit 4), z ±
((r >> 2) & 3) (sign bit 5), halved with $0080, kept in `$13AE08` /
`$13AE0A` for flying again. The record is cleared and gets: group +$5E =
record, +$5D the trigger area + 1 (−1 from the list), +$7A 3, state |=
$1080 unless a ship (class $20; so ships never move), position (cell << 22
+ offset << 8), in the air (height ≠ 0) the gear up (+$7C | $E0) and speed
$2000, state |= $140 & ~$8000, +$5F $44, +$60 $1F4, fuel 25,000 lb, +$63
$3D, +$71 $FF, +$64 $10, angles 0 (`$12212C`).

The lists (mode: entries): 1 free flight and 9 the qualification: the
carrier at (70, 65). 2 the maneuvers: the carrier and the trainer. 3
visual confirmation, stage 0: the carrier (scattered by up to one cell
each way), a type-$12 (shape 6) heading for the carrier, from level 1 a
type $13 (shape 7). 4-8 likewise with their mission's object (4: type $14
heading for a path place, role 8; 5: two type $10; 6: a type $32 (rescue
raft?); 7: type $15; 8: type $21, the submarine **hypothesis**). The
trigger areas spawn more entries when the player enters a box ("The
trigger areas"). An entry is skipped too when its record last held a
type $15 whose cell x is not 0 (`$11D3B4`: the cruise missile's record
is not used again).

### The trigger areas (`$11D17A`, 2026-10-29)

`$11DF00`: longs to the areas, up to a negative word. An area: a box of
cells (x from, x to, z from, z to; inclusive), a count, that many object
list entries (10 bytes, as above). In this program four:

| area | box x | box z | count | entry |
|---|---|---|---|---|
| 0 | 65-67 | 65-67 | 0 | (entries follow, not counted) |
| 1 | 62-64 | 69-73 | 0 | (likewise) |
| 2 | 50-76 | 67-76 | 1 | type $12 (shape 6) into record 12 at path $6E (76, 76, height 4096), heading for record 14 (the carrier), any mode (its level bits 1 are not tested here) |
| 3 | 75-77 | 75-77 | 1 | type $15 into record 4, mode 8 only, role 8, +1 bit 0, heading for path entry 9 |

The walk (`$116BCE`: when the frame counter `$13ABCA` & 15 is 3, in the
object step's start before the player's step — also while an outcome
text holds the objects — and not in the demo, `$13AA80`): for each area,
with a bit number d7 from 0 in `$13AA8D`: the player's cell (record 0's
+6 / +8) in the box and the bit clear → the bit set and the area's
entries spawned (`$11D2FA`); in the box and the bit set → spawned again
(a record still active is skipped, so records destroyed come back, up to
`$13AB9A`); out of the box and the bit set → the bit cleared and the
area's records sent back: those whose +$38 is negative with a record in
bits 0-6 (following another record, or $FF a path point) are left; else
by the entry's "heads for": the player (0) → its own entry's place,
+$38 $80, behaviour 3/4 → 5; a path point → that point, +$38 $FF,
behaviour 3; a record → that record's place if active (+$38 record |
$80, behaviour 3), else its own place as for the player. +1 bit 0 is
cleared in each case. The walk ends after bit number 7 or at the
negative word.

`$11D2FA` spawns only in modes above 2 other than the practice's $7D —
so also in the qualification (mode 9, aircraft limit 2), where area 2's
bogey appears (recording `area1.jsonl`). It runs the list spawner
(`$11D318`) over the area's entries without the level test and without
turning them (heading 0); scattered entries use the kept offsets when
flown again, else `$13ADE8`'s fresh bits (the port: a fresh draw; flying
again with the same variant is not kept yet, for the lists either). Each record gets +$5D = d7 + 1.

**The original's slip** (kept): the spawner returns its result in d7
(0: the last entry spawned, −1: skipped, and −1 for a count of 0), the
same register as the walk's bit number. After an area is spawned the
walk goes on with bit number result + 1 for the next area, and the next
entry of the same area gets +$5D = result + 1. Entering area 0 (count 0)
sets bit 0 and then tests area 1 against bit 0 too, which clears it when
the player is not in area 1: `$13AA8D` stays 0 in area 0 (recording
`area3.jsonl`). With these tables only area 2's +$5D (3) is affected
when area 0 or 1 was entered first; the port reproduces the register.

The way home (`$11D60C`, in a record's flight step `$11A380` when
frame & $1F is its record number, before the explosion's end): an
aircraft of class $10, not in role 8, with +$5D ≥ 1 and outside its
area's box (+$5D − 1) heads for its entry's place: behaviour 3/4 → 5,
+1 bit 0 cleared, +$38 $FF (+$5D 0: a debug message `$10F20E`, now an
empty routine). When the record an aircraft follows is gone (`$1181E6`),
its entry is searched in the stage's list and then from its area's
entries on (to a negative word; not found, the original would loop).

A spawned record's first flight step does not move it (`$11A5AC`: state
$0100 is cleared and the move and velocity step `$106B0E` skipped; speed
and turn still run) — the same for the lists' records at the start.

Checked (`fa_18_interceptor_assets areacheck <content> <recording>`: the
walk from each recorded frame, spawned records stepped, `$13AA8D`,
`$13AB99` and the records compared): `area1.jsonl` (qualification,
player poked into area 2: the spawn) 24 of 24 frames; `area2.jsonl`
(mission 6, area 2 entered and left, 16 walks) 259 of 259; `area3.jsonl`
(area 0) 24 of 24. `objectcheck … area2.jsonl 12`: the spawned bogey
143 of 143 frames, including the way home at frames 396, 428, 460 and
492 (it then follows the player again by the level's box). Not
recorded: area 3 (mission 8's type $15), a respawn while inside, the
leave rules for a record following the player or a path point.

### The missions' aircraft (`$11797E`, `$120C66`, 2026-10-29)

The object step runs the records in order after the player's (`$116C4E`
...: record 0 first). For an aircraft (class $10) `$11797E` returns 1 (it
always flies on: `$11797A` is `moveq #1,d0; rts`): the timer −1; with
"moves" (state $1000) and `$13AA7E`: spinning, it heads for the spot under
it and its speed goes toward $1200 by $90; otherwise, unless flown as the
trainer (+2 bit 0) or without a point, the speed it aims for (`$117A16`),
then where its point is (`$117BA6`). Then `$11A352`: the autopilot
(`$11A456` → `$120B6E`), the attitude step's rules for others (`$121C6E`),
the input step and the flight as the player's.

**Distance** (`$118468`, first thing of `$117A16`): +$39's high nibble
counts frames; at $480 or more away it measures only when the count
passes $20 (below $900) or $50 (from $900). The way to the point in fine
units (a cell $4000); beyond $7F00 on an axis +$4A = $7FFF; else
`$1108C4` with x and the height as the flat pair and z third (the order
matters: `objectcheck` found it), +4 bit 0 set; at most $1E00 behaviour
3 → 4, beyond 4 → 3; beyond $1800 +$4A = $7FFF; the weapon nibble of
+$63 by distance ($10 up to $300, $20 to $C00, $30, $F0 far).

**Speed** (`$117A16`): a point ($38 = $FF): cruise ($1D40; type $15
$1EC0; touchable $15C0; type $14 lands: half its speed on the ground, $600
below height $6C0 with the gear lowered, else $1680). A record: its speed,
matched by distance and role (+4 bit 5: ≤ $240 match, ≥ $600 slower;
roles other than 0/1: ≤ $360 keep, role 6 match, else slower; roles 0/1:
< $180 slower, the pilot's $60 bits match, else slower within $300):
"match" is twice its speed up to $1BC0 within $300, else cruise; "slower"
less an eighth (at most $420); at least $900. The aim is averaged with
the speed, the step at most $90, and the control byte's throttle bits
(1 up, 2 down) push towards it.

**The point** (`$117BA6`): role 8 near its point ($480) steps along its
path (`$11DDC0` from +$3A: the entry equal to the point, then the next;
after the last the autopilot is let go, state bit 7; a type $15 crashes).
Role 8 then goes to the attack part (its a3 there is left over from the
caller; the port takes the player — **hypothesis**). A touchable record
(+1 bit 3) keeps its point (with +1 bit 1 the radar's locked record:
missiles). Any other aircraft follows a record: the player when its +$38
is $FF (a point is overwritten: `$117C60`), else that record — gone, it
takes its own list entry's place again (`$1181DE`); within a box round
the player ($180000, $240000 from level 2, $300000 from 3) it switches to
the player (+$38 = $80). The point is 36 units behind the record (its
view matrix, `$10D498`); near something (+2 bit 0) 48 behind and beside
or above by the pilot's bits 0-1.

**Attacks** (`$1180A6`): when the +$39 count is 1 and the record's
distance is at most $3000: is it ahead of the record it follows, heading
the same way (dot of the way with the record's forward ≤ −$2C0000, the two
forwards' dot ≥ $D000000: +4 bit 5, `$11826A`). While bit 5 is set, in
any role but 6 (8 only in mission 5 on request `$13AAA7`), every frame a
(timer, role) from `$120374` (16 bytes a case, 4 levels): by the record's
distance (≤ $300 / ≤ $C00 / ≤ $1E00 / beyond: +$60 each) and, except
beyond $1E00, the speeds ($C0 apart: +$20, own faster +$20 more) and
(past $300) the player's lock on it just now (+$10). The roles are the
banked turns 9 (8°) and 5 (14°) and the knife-edge pull 2.

**Heading for the point** (roles 0, 1, 3, 4, 8: `$120C66`; before it
`$120B6E` turns roles other than 0/1/8 back to role 1 for 30 frames when
the height leaves the band speed × 64 .. $800000 − speed × 64, and in
mission 5 sends a touchable record whose timer ran out along its path,
role 8): spinning with the nose 15-45° down it waits (role 6); role 8
upside down and not steep waits 37 frames; roles 0/1 whose timer ran out
end (`$120A74`: role 6 for 10 frames, or chasing a record by level 75 /
37 / 18 / 4). The way to the point (the height held between a floor and
a ceiling that grow with the speed — compared, as the original does, with
the 32-bit height) shortened below $4800 and scaled to $C0
(`$119CAE`); its dots with the view matrix's columns 0 (sideways) and 1
(up) pick the axis (+$64 bit 3); within limits (by class: aircraft
$30000-$50000, role 8 $C000-$18000) it is on course and the stick stays;
else up/down: pull or push to a rate from `$12141C` (type $14 `$121448`,
others `$121406`) by the dot's high word; sideways: a roll rate from
`$121432` (type $14 and role 8 `$12145E`), halved from speed $1800; banked
75-285° or pitched past 20° the rudder alone; behaviour 3/4 hold the roll
near level; the stick rolls toward the rate (`$121202`; with the point
ahead, rudder and roll together) and the bank limit +$7E grows with how
far off the point is ($1000-$3000, behind: more; role 8 half).

**The attitude step's rules for others** (`$121C6E`, before the input
step; the port runs them after the flight step by the roll the frame
started with, which equals it — `trainercheck`, `objectcheck`): without
the pilot's bit 4 the stick bits are cleared (rudder and throttle kept);
roles 6 and 10, or on course by the pilot's bits (vertical: bits 2 or 6,
then 5; sideways: bits 1 or 5, then 6) in roles 0/1 — which become role 6
for 30 frames — and 8: the stick rolls the wings toward level (within
3.5°), state $2000; with pilot bits 1 and 2 +3 bit 4.

Checked (`fa_18_interceptor_assets objectcheck <content> <recording>
10`: the record stepped from each recorded frame, the player as moved in
that frame): `work/ref/m3a.jsonl` 314 of 314 frames; `work/ref/
demo_objs.jsonl` (the demo with records 10 and 14) every frame until the
player's shots pass the bogey (frame 636: "near", then the hit — weapons
against objects are not built). `democheck` on `demo_objs.jsonl` flies
the bogey freely beside the player, with the radar ("The radar"): equal
for all 608 frames up to the player's shots reaching it (frame 636).

### The radar (`$10FF26`, `$125E22`, 2026-10-29)

**The list** (`$10FF26`, at the end of the scene's drawing; nothing in
the outside view): `$13AB28` (a pass is due) is set when the frame
counter's bit 1 is clear; the list is rebuilt every 4th frame (counter &
3 = 0) — every frame, and the pass due, while the panel is being drawn
anew (`$13AB27` > 1). Every other active record (not the viewed one,
records in order): its place less the viewer's, shifted right by 2 until
the largest is at most $7FFFFF and then by 8 more, turned by the heading
alone (`$13AEEC` from `$122B1E`: cos and sin / 64; products over 256,
shifted back up: `$119DDA`), into 16-byte entries at `$1435A8` (x, 0, z,
record × 256 | $10; an entry of zeros ends it). The pointer is reset
every frame (`$119DC8`); entries not rewritten stay.

**The pass** (`$125E22` in the panel, when the radar is on screen: the
look shift `$13AC76` within ±11, `$125CDE`; once a frame, twice while the
panel is drawn anew — `$13AB27` 3 from `$10F024`: a view or look change,
and every 32 frames when counter & $1F = $10, `$101492`; it counts down a
frame to −1 at `$1013DC`): per entry, rounded shifts (an arithmetic shift
plus the last bit out): by 13, outside the window (x within ±$1B, z from
−$10 to $15 ahead — the 40-mile picture) or a record not active, on the
ground or the viewer's: state $4000 cleared. Else, except in the
maneuvers, within $10000 on both axes it is "seen" (+$20 bit 6) — and
then for an aircraft while anything is locked mission 3's confirmation
counts (`$13AB5F`: 0 → 30, then −1 a pass, 1 → −1: confirmed), and the
threat lamps' bits (`$13AB5D`: aircraft 4, type $15 8, touchable 2,
missiles at the player $10, others $20 — not built) — beyond it +$20 bit
6 is cleared; state $4000 set (on the radar). By the range (the player's
+$63 low nibble: 9, B, D; key R) the same window says whether it is on
the scope (the entry's bit 5) and where its blip goes: (−x + $9E, −z +
$A7) in the panel, colour 5 ships, 8 not seen or spinning, 4 touchable,
2 missiles, 1 aircraft; two pixels wide (`$12420A`) at or above the
viewer's height; the locked one only every other pass (`$13AB73`).

**The lock** (key T, `$10DEF8`: `$13AAA9`; taken at the end of the next
pass, `$126408`): from the entry after the locked record's (not found:
message $30 and the first) the first on the scope that is an aircraft
(not class 0, $20, $30) and not spinning; none: unlocked (`$13ACB0` −1).
The player's step (`$1183E2`) measures its distance to the locked record
(+$4A, the others' counter and estimate; kept while unlocked).

Checked with `democheck` on `demo_objs.jsonl` (the views from the
recording): the lock, the confirmation and the distance equal in every
frame up to the hit at 636 (the confirmation reaches −1 at frame 314).
Not built: the threat lamps, the lock dropped when its record is
destroyed (`$11858A`, message $4016).

The angle routine `$10454A` (heading part used here; the port's
`direction_of`): signs apart, the three lengths scaled so the largest
fits ($10000000: / 8, shifts 14 / 0; $2000000: 14 / 0; $800000: 12 / 2;
$200000: 10 / 4; else 8 / 6), the smaller of x and z over the larger
(`$119EE4`: a word division rounded half away from zero) / 64 into the
arctangent table `$132DF8` (257 tenths of a degree) × 8, folded into the
quadrant (x < 0 and z < 0: $3840 − h; z < 0: h + $3840; neither: $7080 −
h). The pitch likewise from the height over the flat length
(`$119BB2`: Newton's square root from n / 200 + 2).

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

### Quick saves (2026-10-31)

`write_state` / `read_state` keep the module's whole running state as
values (`state::Writer`, "FA18" version 1, about 20 KB): the front end
and its texts, the flight, the player's and the others' records, the
weapons' slots, the radar, the messages, the sound driver's slots,
scripts and Paula channels, the pilot record as it is in memory and the
random generator. The options stay as the player has them; gone records
still drawn are dropped (the lists are built anew). Checked with `fly
... N:!twin`: from the title, the front end, the demo (start, the shot,
the landing), a free flight (rolling, crash, weapons), a mission from
the carrier, the maneuvers, the practice (a collision) and the
qualification, the loaded module's pictures, sound and saved state stay
equal to the original module's every tick.

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
- **The trigger walk's register** (`$11D17A`): the bit number of the
  areas' walk is overwritten by the spawner's result ("The trigger
  areas"); kept.
- **Mission 8's count**: the cruise missile counts towards the aircraft
  limit `$13AB9A` when spawned, but its destruction never counts in
  `$13AB9B` and it is never spawned again ("The missions' flow").
- **x below 0** (`$11A640`): a record moved past x = 0 has its z set
  to the move instead of x clamped (only at the world's edge; the port
  clamps x).
- **Smoke never freed**: the 16 smoke slots are only cleared at a
  flight's start, so after 16 explosions on the ground a flight shows no
  more smoke; their timers are words that would wrap after 32,768 frames
  (about 36 minutes) and show a column again (the port lets them run out).
- **Far cruise-missile explosions**: past $4000 units on an axis the
  discs' size jumps 16 times (the distance is taken shifted), past
  $40000 again; kept.
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
  `--objs` the missile records 1-4 and the projectile slots, `--recs`
  chosen records, `--smoke` the smoke slots, `--mem ADDR:LEN` any bytes,
  `--pics DIR:F1,...` `screen.py` pictures at those frames from the same
  halted frame (the picture of the frame before the logged state),
  `--shots`; steps for controls, keys, dives, control byte, pokes,
  `hold` (a poke every frame), `orw`/`andw` word bits,
  `follow:REC,DY,DZ,DX` the player kept at a record's place every frame,
  logged before the poke as `moved`; each line logs where the object
  lists are loaded, `lists`); `screen.render(d, out)` for scripts that
  hold the machine (one debugger client at a time; never connect while
  the game loads from disk - FS-UAE quits);
  `mstart.py M [LEVEL]` starts mission M from the main menu (config +6,
  key 7, space through the briefing), `recs.py` lists the records;
  `flashcap.py` times the crash flash; `listsnap.py`, `viewgrab.py`,
  `modelbp.py`, `opbp.py`, `pointcheck.py`, `stepcheck.py`, `fps.py`.
- `fa_18_interceptor_assets` (tools/): `import`, `export`, `viewcheck`,
  `flightcheck` (flight model against recordings: fc1-fc4 equal except
  key ticks), `soundcheck` (sound calls and message line: sd2, cr5,
  td1-td3 equal in every frame), `weaponcheck` (missiles and projectiles
  against `--objs` recordings: wpm, wpg, wpa equal in every frame; with
  `--smoke` the ground hit, explosion and smoke slots: sm2-sm4),
  `trailcheck` (the trail's ring against `--mem 1450B8:50`), `effectshot`
  (the port's view of a recorded frame: camera `$13AD6C` / `$13AEC8`,
  missiles, explosions, smoke, the aircraft itself outside or looking
  past the cockpit; recordings work/ref/demo_pics2.jsonl with the
  original's pictures demopics2/ for the demo's look back and final
  approach),
  `objectcheck` (missions' records stepped from each recorded frame),
  `areacheck` (the trigger areas: area1-3 equal in every frame),
  `missioncheck` (a mission's world in lockstep: m4a-m8c, "The missions'
  flow"; with the missile records 9, 11, 13 recorded, the enemies'
  launches and missiles, the decoys and the player's touch: em4a-em4c),
  `hudcheck` (the head-up display for given readings against a
  `screen.py` colour dump: `wp/gun.idx` F/A-18, `f16b.idx` F-16, no pixel
  differs), `fly` (headless module run from the title,
  pictures, `!sub` four pictures between the step before and that one
  (smooth motion), `!wav`, `!reload` (a quick save loaded into a fresh
  module at that tick) and `!twin` (the same, run beside the module:
  prints whether pictures, sound and saved state stay equal to the end;
  not with `!wav`); keys: digits, `*` Return, `~` Tab (Esc), `^`
  Ctrl+Tab (Shift-Esc), `_` space, capitals the letter keys, `@ [ ] { }`
  the keypad's 0 4 6 8 2; touch: `?stick=X,Y` held until the next stick
  step, `?throttle=Y`, `?gear`, `?view`, `?tap=X,Y` in picture pixels - quote
  them in zsh), `stickcheck` (the pitch input's ramp for a half
  deflection). `sceneshot <content> <out.png> x y z heading pitch normal|high`
  (the port's outside view from any place; with `F18_CAMERA=file` the
  original's camera: `$13AD6C` place and `$13AEC8` matrix / 256, 12
  numbers).
- `harness/proc.py`: the running original's screen procedure, frame
  countdown and text queue (the front end's traces).
- Captures in `work/ref/`: `em4a`-`em4c` (mission 4: the enemies'
  missiles, records 4-14 with `--objs`), `sm1`-`sm5` (missiles into the ground: smoke,
  pictures in `sm3`-`sm5/`), `demo_trail` (the demo with the trail's
  ring), `demo_start` (the demo from the menu's 1 with the bogey), `m4a`-`m8c` (missions 4-8), `fc*.jsonl`, `sd2`, `cr5`, `td1-3`, `wpm`, `wpg`, `wpa`, flash
  timing `flash_cr4.txt`, message bitplanes `msg7.idx`/`msg8.idx`,
  reference screenshots.

## 8 Reprise technical lessons (2026-10-31)

The Documentation / Lessons browser now explains the Hornet's draw ordering,
startup-built roads and player flight model. Detailed section-to-evidence links
and laboratory limits: [technical lesson evidence](../../docs/articles/interceptor-technical.md).
These are illustrated articles with small isolated experiments, not new claims
of whole-game equivalence. The plane shares the game renderer's draw-order
interpreter; road coordinates use `road_points`; flight comparisons call `fly`
on fresh states. No save or pilot data is changed. The road-offset comment's
“160 degrees” was corrected to about 110 degrees from atan2(241,-88), without
changing the original integer arithmetic.
