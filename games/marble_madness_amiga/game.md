# Marble Madness (Amiga) — how the original works

What was learned while rebuilding Marble Madness (Electronic Arts, 1986, the
Amiga version): the disk, its encrypted program and protection, the data
formats, how the program runs, the rules of all six courses and the front
end, and the original's own quirks. The remaining work is in
[plan.md](plan.md). The lesson chapters are described in
[LESSONS.md](LESSONS.md).

## Sources and evidence

- **Image:** `Marble_Madness.adf` (901,120 bytes),
  MD5 `735dc697d64b3eeaa000778eb0b1153a`.
- **Method:** everything here comes from this image (own ADF and hunk
  readers, capstone listings, a Ghidra decompile) and from runs of it in
  FS-UAE (A500, Kickstart 1.3, GDB stub).
- **Prior exposure:** the user's RetroReverse project has a long writeup of
  this disk, whose README says the model that wrote it recognised game
  names from training data. It was used as a guide to where things are;
  every point taken from it was checked against the image's code, and where
  they disagree the sections below say so.
- **Addresses:** `$1xxxxx` is a location in the research layout: the
  decrypted program laid out flat at $100000 (`work/dat.bin`); `hNN` is
  hunk NN.
- **Paths:** source paths are relative to the Reprise root. Evidence lives
  outside the repository in `.analysis/marble_madness_amiga/`:
  - `work/` (flat image, `all.c`), `ghidra/` (the project and `ghidra.sh`);
  - `harness/`: `mmref.py` (FS-UAE driver and recorder), `regress.sh`,
    `memcheck.py`, recordings in `runs/`;
  - `tools/`: `m68kdis.py`, `libdump.py`, `audiodiff.py`, `picmap.py`,
    `ultimate_maze.py`;
  - `lessons/`: pictures for the lesson chapters.
- **Checking tools:** `build/marble_madness_replay` (modes `lockstep`,
  `pictures`, `trace`, `search`, `flow`) and `build/marble_madness_assets`.
  "Joyport" in the notes is the native re-implementation's earlier project
  name.

## The original's quirks (summary)

Each is described where it belongs below. All are kept in the
re-implementation unless said otherwise.

- **Region interpreter:** "if marble near" skips only half of its target
  when not taken, so the target's low word runs as an unknown opcode (a
  no-op, `$11051C`); its racer loop re-reads the same record after a racer
  that is not racing.
- **Vertex pattern search** never compares the table's first entry
  (`$10DC24`).
- **Course start** leaves some marble fields from the previous race.
- **Mixed arithmetic:** the speed cap multiplies with a 16-bit `muls.w`,
  friction with 32-bit arithmetic.
- **Dust records past the end of the break dust list** are drawn with
  colours from wherever their pointer leads (the port skips such a record).
- **Ultimate** loads `Silly.ilb` (listed twice); `ultima.ilb` is never
  loaded.
- **Drawbridge mask:** a one-pixel line in the bridge's mask (the user
  chose to keep it).
- **Random numbers** mix in the video beam position; by the user's rule
  the port uses a standard generator with the same masks, ranges and draws
  (see "Random numbers ($109136)" under Findings).

## Disk, loader and protection

Image: `Joyport/Marble_Madness.adf`, 901,120 bytes, MD5
`735dc697d64b3eeaa000778eb0b1153a` (the same file as RetroReverse's copy).

### Sources and disclosure

- Everything below was read from this image with Joyport's own reader
  (`shared/amiga/adf.cpp`, `hunk.cpp`), capstone 5.0.7 (68000 listings,
  `tools/m68kdis.py`) and Ghidra 12.1.4 (`ghidra/`, decompiled program in
  `work/all.c`).
- **Prior work.** RetroReverse (`~/Development/RetroReverse/games/marble-madness-amiga`,
  the user's own project) has a long writeup of this disk. Its README says
  the model that wrote it recognised game names from training data. It was
  used as a guide to where things are; every point taken from it here was
  checked against the image's code, and where the two disagree this file says
  so. Its Kickstart ROM (`tools/platform/amiga/kick13.rom`, MD5
  `192d6d950d0ed3df8040b788502831c2`) and its GDB-enabled FS-UAE build
  (`~/Development/fs-uae`, prb28 fork at 58feb4c with its arm64 patch) are
  the reference machine.

### File system

Plain AmigaDOS OFS, volume `MarbleMadness!`, 50 files in `c/`, `s/`,
`libs/` and the root (`tools/amiga_disk list`). Every checksum is valid
except `c/sigfile` (93 bytes, header block 884): its eleven "data blocks" are
blocks 11-21 (cylinder 0, side 1), which hold no OFS headers, only
`"DOW"` + a counting byte per long (`444F5780 444F5781 …`). On the original
disk that track was presumably a non-standard track; the ADF has a
placeholder there. Nothing in the launcher or game reads `c/sigfile` through
DOS (the launcher only `Lock`s it, `MarbleMadness! $554`), so the reader
skips it.

Hunk files: c/bootscr was linked so that every BSS hunk is followed by two
`HUNK_END`s; AmigaDOS ignores an END with no open hunk, and so does
`amiga::read_load_file`.

### Boot chain (read from the code)

1. `s/startup-sequence`: `LoadWb`, `endcli > nil:`; the player starts the
   `MarbleMadness!` tool icon.
2. Launcher `MarbleMadness!` (23 hunks, symbols kept), `main` at `$3BA`
   (flat image at base 0):
   - builds `"c/" + <program name> + ".dat"` (`$77C`) — so it **does** open
     `c/MarbleMadness!.dat` by name; the string is made at run time, which is
     why it never appears in the file (RetroReverse says the file is only
     read by track position; the code says otherwise);
   - allocates 8-120 bytes of chip memory sized by `VHPOSR & 15` (freed at
     the end; it only moves later allocations);
   - control block on its stack: `+0` 0, `+4` 1, `+8` key count, `+$C` key
     pointer, `+$18` zzz's relocation checksum word;
   - `LoadSeg("c/zzz")`, runs it on `"c/xxx"` with count 0 → xxx's seglist;
   - `C = checksum_seglist(xxx) ^ ctrl+$18` (`$574-$58A`), count = 20;
   - runs xxx with the control block (xxx allocates the 20-long key and
     fills it), `UnLoadSeg(xxx)`, `key[i] ^= C`;
   - `LoadSeg("c/bootscr")`, shows `c/splash` (or `lo-res/paintcan`);
   - runs zzz on `c/MarbleMadness!.dat` with the key → the game's seglist,
     tears bootscr down and starts the game.
3. `c/zzz` (`$C4` main): key table, decrypt-and-load (below).
4. `c/xxx` (decrypted, 22 hunks): `$C8` allocates `count * 4` bytes for the
   key, `$130` opens trackdisk.device, reads tracks through the hardware
   (`$496`: CIA-B `$BFD100`, disk DMA) and measures something (`$348`:
   difference of two positions minus `$440`). At `$240` the result is
   compared with `$5DC`, but the branch is a `NOP` and `$248` stores the
   constant `$5DB` (1499) instead; `key[i] = 1499 / ((i + 1) * 300)` →
   `4, 2, 1, 1, 0, …`. (Two more `NOP`s at `$29E`, `$2CE`.) This looks like a
   patched (cracked) copy; the image's `c/sigfile` placeholder fits that.
   Hypothesis only.

### The cipher (c/zzz)

- `$C2C`: `t[0] = $57319753`, `t[i] = t[i-1] * 31 + i` (55 longs);
  `$D46`: `t[i] ^= key[i]` for `min(count, 55)` entries; `$DEA`: XOR
  `(v >> 16) & $FF` of the vectors at `$08-$20` into `t[10..16]`, `$28-$38`
  into `t[10..14]`, `$80-$BC` into `t[32..47]`, of `FindTask(0)->tc_ExceptCode`
  into `t[30]` and of `tc_TrapCode` shifted right 4 into `t[31]`.
- `$F04`: `t[p] += t[q]; p = (p + 1) % 55; q = (q + 2) % 55` (q from 27);
  returns `t[p]` at the **new** p.
- `$2F0` loader: block types are read as stored; every other long is
  XORed with the next keystream long (`$2C0`); names, debug data, RELOC16/8
  data are skipped as stored. Grammar in `loader_cipher.cpp`.
- `$78C` also keeps `ctrl+$18 ^= hi ^ lo` of each relocated long after
  relocation. `checksum_seglist` (launcher `$DA2`) XORs every word of the
  loaded hunks, so `C` is the XOR of all words not inside a relocated long —
  independent of load addresses. Computed from the decrypted xxx: **$CDDA**
  (RetroReverse found the same value by brute force).
- Machine values (Kickstart 1.x): every vector page and `tc_ExceptCode`
  page `$FC`, `tc_TrapCode` page `$FF`. With them xxx and the program decrypt
  to valid load files (every relocation in range), which is the evidence;
  the values themselves agree with RetroReverse's capture.

### Results

- `c/xxx` → 6116-byte plain load file, SHA-256 `876e16a1…dc11`.
- `c/MarbleMadness!.dat` → 347 hunks (133 CODE, 109 DATA, 105 BSS), SHA-256
  `a03ea01d7e018250b6dd4a7539259ac4c1948652b0afcc8b78deed1642af19a2`
  (`program.hunks`, the import's identification).

### Reference check (2026-10-26)

`harness/mkboot.py` copies the ADF with the startup-sequence replaced by
`MarbleMadness!` (CLI start; the launcher's CLI branch takes the name from
argv). FS-UAE (A500, KS 1.3, `--remote_debugger_port=6860`) boots it;
`harness/memcheck.py program.hunks dump.bin` waits for the game's tasks
(`Painter`, `Framer`, `Scroller`, `SfxTask`), dumps chip and slow memory, and
locates each hunk. Result: all 118 CODE hunks with contents are in memory
byte for byte (116 found by content, 2 all zero), 22 DATA hunks equal, 21
DATA hunks differ (variables the running game has changed), 1,443
relocations point at the located hunks, none wrong. `harness/hunkdiff.py`
gives the per-hunk breakdown. (FS-UAE needs a real window; it is a
verification tool only.)

## Data formats

Addresses are in the decrypted program laid out flat at `$100000`
(`work/dat.bin`, `amiga_disk flat program.hunks 0x100000`, hunks 8 bytes
apart; `work/dat.hunkmap.txt` lists them). Decoders:
`games/marble_madness_amiga/data/assets.cpp`; pictures:
`build/marble_madness_assets export <content> <out>` and the launcher viewer.

### Which file is loaded by what

| Files | Loaded by | Name table |
| --- | --- | --- |
| `<course>.mlb` | `$1080A8` (course start `$103326`) | `$10353E` (6 names) |
| `<course>.ilb` → bank 0 | `$108224(name, 0)` | `$103596`; Ultimate uses `Silly.ilb` (listed twice), `ultima.ilb` is never loaded |
| `marbdat.vlb` → bank 1 | `$108224(name, 1)` | literal `$115454` |
| `slink.vlb`, `ooze.vlb`, `birdink.vlb` → banks 2-4 | creature set-ups | literals |
| `<course>obsc.vlb` → bank 5 | `$108224(name, 5)` | `$11F5BE` |
| `*Track` | `$103216`, LoadSeg (`$108E90`) | `$1035E4` |
| `*snd` | `$108E90` near `$1225E8` (only if `$1225E1` set) | `$122612` |
| `marbdat`, `defcop` | LoadSeg | literals `$11544C`, `$108E46` |

Buffer sizes per course: maps `$108440` (`$88B8-$A410`), bank 0 `$108458`,
bank 5 `$108470`, banks 1-4 `$10848C` (`$5208`, `$1388` ×3). Flags `$1084C8`
= 1 (drop sprites' guard row) and `$1084C9` = 1 (files are packed) are
initialised data; nothing writes them.

### Packing

`.mlb`, `.ilb`, `.vlb` are one ByteRun1 stream each (`$1092D0`, the IFF
UnPackRow rule): `$107FBE` reads the file into the end of its buffer and
unpacks it to the start.

### Tile map (`.mlb`, `$1080A8`, drawn by `$109AF8`/`$109BA8`)

| Offset | Contents |
| --- | --- |
| `+$00` word | rows the scroll reaches (`$10D47A` area: `rows - $19`) |
| `+$02` 4 longs | offsets of tile planes 0-3 (made absolute at load) |
| `+$12` long | offset of the tile map |
| `+$16` 16 words | palette, `$0RGB` |

Tiles are 8 × 8, 4 planes; tile n, row r, plane p is the byte at
`plane_p + (n >> 1) * 16 + (n & 1) + 2r` (`$109BA8`), so a plane holds 8
bytes per tile (odd counts happen: Intermediate has 769). The map is a word
per cell, 36 cells (288 pixels) a row (`$109AF8` steps `$48` bytes), up to the
end of the file. Results: Practice 75 rows/814 tiles, Beginner 145/942,
Intermediate 135/769, Aerial 128/782, Silly 143 (144 stored)/936,
Ultimate 108 (198 stored)/812. The extra stored rows of Silly and Ultimate
are beyond the scroll's reach (what Ultimate's are for: open). Colours 10-15
are changed per screen band by the course's display data (RetroReverse;
not checked yet — open).

### Image libraries (`.ilb`, `.vlb`, `$108224`)

A word with the cell count, then 20 bytes per cell:

| Offset | Contents |
| --- | --- |
| `+0` byte | type: 1 = sprite, 0 = scenery |
| `+1` byte | flags; bit 0 on a type-0 cell: build its mask at load (`$108196` ORs 4 planes) |
| `+2` word | width in 16-pixel words |
| `+4` word | rows (sprites: minus 1 at load, the guard row) |
| `+6` word | bytes per plane = width × 2 × rows |
| `+8` long | offset of the pixels (made absolute) |
| `+$C` long | mask (0: taken from the buffer's free tail) |
| `+$10` long | another offset (made absolute when not 0; meaning open) |

Sprites store each row's plane words together (the hardware sprite layout;
the marble masking at `$110E6x` reads a long per row = both planes);
scenery stores 4 whole planes one after the other. Checked: in all 21
libraries every cell's span to the next cell is exactly `planes × bytes per
plane` with 2 planes for type 1 and 4 for type 0 (`tools/libdump.py`).
Sprite colours come from draw records (not decoded yet; the previews use a
grey ramp).

### Course layout (`*Track`, `$103216`)

A load file whose first hunk starts with ten longs that `$103216` stores in
globals (`$1009BE`, `$162`, `$165`, `$246`, `$132`, `$108B52`, …). Its
"CODE" hunks hold data, not instructions (PrcTrack hunk 3 starts with
offsets and small tables). Contents not decoded yet — next step.

### Sound (`*snd`), `marbdat`, `defcop`

`*snd` (PrcSnd, BegSnd, IntSnd, AerSnd, SilSnd, UltSnd): load files of
data-only modules (empty CODE, DATA in chip memory, empty BSS). The first
filled hunk starts with the sound table: a word count (35 on Practice),
then 8 bytes per entry: kind (0 music, 1 effect, 2 stop music, 3 counter,
4/5 flag), link (an entry that keeps the started sound, or -1), address.

- Effect record (18 bytes): segment chain, volume envelope, pitch envelope
  (longs; 0 = steady), duration (word, ticks), priority (byte), spare,
  pan (word; -2 right, -1 left, 0-$13F screen x).
- Segment (14 bytes): next (long, 0 ends), count (word, -2 = loop), skip
  (long, non-zero passes it by), wave (long). Wave (12 bytes): length in
  words (long), period, volume (words), samples (long).
- Envelope: (rate, target) pairs of 16.16 longs; rate 0 ends (holds).
- Music entry (10 bytes): score (long), instrument (long), tempo (word).
  Score: 8 voices × {pattern list (long), pan (word), priority (byte),
  spare}; pattern list: 6-byte entries {repeats (word), pattern (long)},
  repeats 0 ends; pattern: note bytes 0-$7E, $7F rest, control bytes
  $80+ (low nibble 0 = end of pattern, else duration code; bit 6 = early
  release). Instrument (18 bytes): IFF 8SVX voice header (oneShotHi,
  repeatHi, samplesPerHiCycle, samplesPerSec, ctOctave at +$E, ...),
  samples (octave after octave, each twice as long), volume envelope,
  pitch envelope, volume (byte at +$10).

Practice: one instrument (hunk 4: 4-sample top octave, 7 octaves, at
offset $18), the race music (entry 30, tempo 136), the out-of-time tune
(entry 25, tempo 180), about 20 effects (bounces, landings, breaking,
rolling loops kept by entries 10, 12, 15).

### Title picture (`c/splash`)

IFF ILBM 320 × 200, 4 planes, ByteRun1, CMAP and four CRNG ranges
(`amiga/iff.cpp`). Shown by `c/bootscr` while the program loads.

## Program map

Program: `program.hunks` (decrypted `c/MarbleMadness!.dat`, see "Disk, loader and protection"), a
compiled C program of ~115 object modules (each a CODE/DATA/BSS triple),
no symbols. Research layout: flat at `$100000` (`work/dat.bin`), Ghidra
project `ghidra/` (`./ghidra.sh import`, `./ghidra.sh all ../work/all.c`,
`./ghidra.sh decompile OUT ADDR…`), 548 functions so far.

### Architecture (first reading)

The game runs on AmigaOS: it opens intuition, graphics, input.device and
audio.device and splits its work into Exec tasks, each created through
`$12535C(task, entry, 0)`:

| Task | Set-up | Entry | Role (hypothesis from names) |
| --- | --- | --- | --- |
| `dummer` | `$102E1E` | h14 | ? |
| `Framer` | `$107172` | h33 | per-frame timing |
| `Painter` | `$10969E` | h58 `$109390` | drawing (tile map `$109AF8`) |
| `Scroller` | `$10AD66` | h71 | vertical scroll |
| (unnamed) | `$11FBB4` | `$11FABA` | the music player, woken each vertical blank |
| `SfxTask` | `$121950` | `$121214` | sound effects and the notes of the music ("Sound" below) |

Courses: `$1005EE` is the course number (0-5). `$103326` starts a course:
frees the last one (`$103300`), loads the map (`$1080A8`), the obstacle bank
(`$108224`), the Track (`$103216`), then sets up the object systems (h215,
h221, h230, h245, h131, h257, `$1085FE`), Intermediate's extra (h140,
course 2), Ultimate's (`$11E5EA`, course 5) and `$10D47A(course)`.
`$1033E0` starts a game (two player records of `$F4` bytes at `$10024E`,
player count `$1005F0`, first course from `$102378`).

Texts (the score screens, banners, "GAME OVER", "RED/BLUE PLAYER", "READY
SET GO!") are in DATA hunks around `$1029xx`; they are data to read at run
time.

### Code versus data (first sorting)

- Native code to rewrite: physics, objects, the scroll and drawing, the
  task scheduling (as a single-threaded frame loop with the original's order
  — to be established), sound playback.
- Data to read: maps and libraries (done), Track contents (placements,
  height field, display bands, animation scripts — RetroReverse calls part of
  them "region scripts"; check whether they are interpreted), texts and
  tables in the program's DATA hunks, `*snd` samples and sequences.

## The rebuilt play

Code: `games/marble_madness_amiga/game/` (clean C++; the comments name the
routines each part reproduces, in the research layout of `work/dat.bin`).
Started 2026-10-26.

| File | What |
| --- | --- |
| `terrain` | the height map: band rasteriser, vertex encoder, dictionary, decoder |
| `probe` | the ground under a point: corner cache, height, slope, side/corner analysis |
| `regions` | the 25 region slots, activation by scroll band, the script interpreter, frame lists (pictures, height patches) |
| `marble` | the marble record's fields (with their offsets, for the tools) |
| `race` | course start, world update order, scroll, clocks, the mode machine, rolling update, region contacts |
| `marble_rules` | input, speed cap, friction, slope force, walls, edges, landing, respawn, zones, goal |
| `marble_shine` | the rotating shine dots |
| `marble_covers` | the visibility rule and the wall covers that hide the marble (also for slinkies) |
| `steelies` | the black enemy marbles: spawning by band, patrol/hunt/chase/return/knocked modes, steering; the marble-marble collision |
| `slinkies` | the green worms: spawning, wandering, hopping, knocked, eating; their contact with the players' marbles |
| `view` | playfield ring model, region pictures, colour bands, sprites (as plane words, so covers cut them as the game does), sparkles, drop markers, score bar |
| `module` | the Joyport module (25 updates/s, steering option, simple course order, sound) |
| `../sound/effects` | the effect synthesiser (SfxTask): slots, channel choice by side and priority, segment chains, envelopes, 50 ticks/s |
| `../sound/music` | the music player: 8 voices, patterns, durations, tempo, the 8SVX instrument's octaves |
| `../sound/sound_board` | the course's sound table ($1222E4) and the clock between the engines |
| `shared/amiga/paula` | the four channels as audio.device drives them (writes, cycles, period/volume, flush), A500 output filter |

### Frame structure (read from the code)

- Exec tasks: `Framer` ($106EB0) sends a tick to `Scroller` ($10AC1C) and
  waits at least two vertical blanks ($1071C4 = 2) until the tick is done:
  the world runs at 25 updates/s on PAL; a slower update stretches (not
  modelled: no slowdown). `Painter` ($109390) draws tile rows into the
  playfield ring.
- `Scroller` runs the game mode's handler ($10B48A, table $10B4FA): mode 0
  races ($10B1EE), 1 the demo, 5 the options screen, others between races.
- World update $10B1EE: scroll ($10D618), score and time displays, marbles
  ($113F72 → $114402 per marble), creature lists, regions ($1100A2), colour
  cycling ($108664), clocks ($10B7E6), input read for the next update
  ($119944).
- Course start ($1030B4): load, one region update (lays the start area's
  animated ground), place marbles ($114AA2), time for the course and
  difficulty ($102350 table, added by the intro $106A64), clock starts.

### Findings

- The height map stores, per grid point, the heights of the four cells
  around it; walls are discontinuities. Encoded vertex codes rotate the
  corners by one against the rasteriser's order; the pattern search never
  compares the table's first entry ($10DC24, hand-written assembly).
- Animated vertices (codes $800-$FFF) look up a runtime table ($1007B0)
  that region frames patch.
- The region interpreter skips unknown opcodes (≥ $13); "if marble near"
  skips only half of its target when not taken, so the target's low word
  runs as such a no-op ($11051C). Its racer loop re-reads the same record
  after a racer that is not racing.
- The speed cap multiplies with a 16-bit `muls.w`, friction with 32-bit
  arithmetic; both kept. Rolling scores 10 points per $A0 of the roll-sound
  accumulator.
- Course-start leaves some marble fields from the previous race
  (`fall_start_z`, edge cells, cells, edge sides, saved zone): the replay
  tool takes them over from the recording.
- The goal recolours its copper band to the winner's colours ($108710).
- Joystick input is ±$20 per axis on the course's diagonal grid; the mouse
  (default) turns the screen's axes onto the grid ($119A00). The options
  screen offers both (and a rotation flag).
- The clock: a tenth takes three updates, so a game second is 30 updates
  (1.2 s).
- Sprites count lines from the top of the 9-line score bar (pixel-exact
  against the original's screen). Screen heights count upwards (a sprite's
  y is its top; $108 - y is the picture row).
- Wall covers ($11F486 → $11EC42): per 8-pixel column the marble's sprite
  touches (3, or 5 for the large animation frames), at most one cover from
  the course's obsc library: the Track's hand-placed lists (header +$C,
  5-word entries {last row, rows, kind, height, cell}) or, failing those,
  the first wall-face row at or in front of the marble (descriptor +$2E,
  a bit map per column) and the ground heights there ($10E0A8) against the
  sprite: none, full (cell 0) or a slanted edge (cells by height
  difference, $11F510/$11F532/$11F554). All columns covered (or the middle
  three of five) hides the marble (+$1C = 0); so does a screen height
  (y) outside -$10..$FF. The sprite builder ($1114F8) clears the covers' pixels from
  the sprite (a long shift for 1-7 pixels, word shifts for 8-15). The
  global switch $11F4F8 is never set.
- The respawn sparkles ($1138D4): 8 marble cells ($44-$4B) on a circle of
  radius countdown/2 lifted by the countdown, turning 10 of $192 per update,
  offsets from $1020C4; kept in the shine and cover slots with +$EB set so
  the sprite builder shows them instead of the marble (no covers).
- Practice's drop bonus ($11573A): landing in zone 1, 2 or 3 (once a game
  per player, $10060E) gives bonus 7-13 by the distance past $1E8/$238 (y)
  or $210 (x), and leaves a marker (a splat, marble cell $3C/$3E, $100BBE)
  re-projected after every update ($11DAAC; it leaves its x/y in the probe).
  The award ($10A82A) also restarts the points picture's bob (step -1).
- The scroll check runs when the frame counter $100248 is a multiple of 8;
  the counter is reset at game start only (1 at the first race's first
  update).
- Sound: see "Sound" below.

#### Beginner (2026-10-26)

- The scroll band change ($10D9BC) spawns and removes steelies ($119E5A),
  slinkies ($11ABF8) and acid ($11BE98, Intermediate), then the regions.
- Steelies ($100BD6, 2 × $86 bytes) are marbles rolled by $115470 with a
  push from $11365A: patrol along neighbour 0 of a waypoint graph (6-byte
  waypoints), hunt (neighbour nearest the player), chase within $280,
  return to the nearest waypoint after a chase or a knock. Their landings
  add up impacts in +$60; over $50 or one over $1F breaks them (bonus 3 to
  the players). Falling off or into a funnel removes them (bonus 3).
- The marble collision ($1169C4) covers both players and both steelies:
  back to the update's start, velocities swapped and pushed apart
  ($116EC8 by speed; four times as hard between a player and a steelie);
  coming down on another marble breaks it.
- Slinkies ($101548, 4 × $6A bytes) move a cell per animation round along
  3-byte waypoints; a player of their zone rolling faster than $C000 at
  $180-$280 makes them hop (height curve $11B2AA); within $6F they eat the
  marble (frozen, then respawn), within $C0 they throw it off. A player's
  marble that touches one ($11AF24) is stopped and bumped; a wandering
  slinky is knocked sliding with the marble's push (friction kind 2).
- Region codes on Beginner: 3 the drawbridge (a pictured region whose
  keyframe registers it for the course's own occlusion: the marble is cut
  by the bridge's silhouette while not past its point, $105CB0 →
  $111B10 → $111772); 16 ice (16 × 14 cells without friction); 18-22 the funnels:
  the mouths (18, 19/20) take the marble to an exit (another marble there
  moves the exit), hold it in mode 3 for $12 updates and score 4000 or
  2000; their walls (18, 21, 22) are planes ($1186A4, tables $118BC8)
  that reflect the marble; one falling deep into a tube is lost.
- Keyframes of codes 3, 6, 10-13 and $19 only register the region for
  course rules; the script goes on (the port stopped there before).
- The world-update count $100248 is 0 at a race's first update and then
  goes 2, 3, 4… (every recording); the scroll looks when it is a multiple
  of 8.
- The broom sweeping up the shards is the break's dust animation (+$6C
  while +$71), drawn in its record's own colours; the dizzy stars likewise
  (+$DE while +$E3).
- Masks: $111772 cuts a sprite only through a cell's mask (+$C of the
  20-byte cell record). The loader ($108224) makes masks only for type-0
  cells with flag bit 0 (the planes ORed, $108196). The bridge's frames
  (cells 4-7 of `beginr.ilb`) have none of their own: the course start
  ($10F9E4, course 1) copies the masks of cells $11-$14 into them; those
  are 64 × 64 pictures of the bridge alone, so only the bridge cuts, not
  its 64 × 63 rectangle of course around it (the port cut by the frame's
  own pixels before, which hid the marble in the whole rectangle).
  Checked at run time (`mmref.py --peek "@@1084a0+52:50,!365f8:800"`).
  These masks reach a pixel past the frames' drawn edge in places, so a
  marble cut by the bridge shows a one-pixel line of whatever lies behind
  (wall or floor) between the bridge and the marble. The original does the
  same: `beg_bridge` update 940 is identical pixel for pixel around the
  cut, that line included.
- The display list ($10060F order, $100436 slots of 14 bytes: type,
  index, a box of 6 words; 31 slots): an object enters before the first
  entry that belongs in front of it ($112004), leaves at removal
  ($1120F4). At the end of each world update ($111BB4 from $10B4DC) the
  boxes are refreshed ($110CD4: marbles 6 × 6 × 6 around the point, a
  slinky its frame's box with height $10, regions by code) and the list
  gets three partial bubble passes with gaps 1, 2 and 3 ($10E044); the
  test ($1113DC) compares the corner sums x + y + z, then the boxes axis
  by axis. Drawing follows the list, the nearest last. Types: 1 player,
  2 steelie, 4 slinky, 5 region (goal flags), $2A drop marker. Regions
  add an entry at each keyframe of kind ≠ 1 (duplicates happen) and
  remove one when they stop.

#### Intermediate (2026-10-26)

- Acid ($1016F0, 5 × $42 bytes; Intermediate and Ultimate): Track header
  +$20 holds a spawn list (8-byte entries: first band, last band, path,
  zone; a path of 0 starts a group: zone 5 picks one of two paths at +$14,
  the others one of two sets of six at +$1C/+$34 by random(2)) and four
  creep animations by direction (+4 x+, +8 y+, +$C y−, +$10 x−). A puddle
  starts at its path's first cell corner, creeps a cell (8) per animation
  round towards the next waypoint (3 bytes: x, y, next index; x first
  unless it last moved along x, $11C5BA), 2 updates per step, 1 while a
  player is in its zone if that zone is 6. Spawn and removal by band like
  the slinkies ($11BE98). Display type $E: box from the frame record, z−8,
  height 0 ($110CD4); drawn by its record in its own colours ($10652E).
- A player's marble whose left/back edge points (x−5 or x−1, y−6 or y−3)
  fall in a puddle's frame box dissolves ($11C1C0): mode 9 (the port
  called it "appearing" before), sound $1D, +$62 = the puddle, the puddle
  slows to period $1C for one step. Mode 9 pulls the marble a pixel per
  update to the box's middle (whole pixels, $11C34C) while marbdat list
  +$10 plays (period 4), then the player respawns (delay $65, sound $13).
- The funnel (code $20, $117BB6) works like Beginner's code $12 with
  heights $3F80/$3FB0/$3FA0, exit (620, 596) or (620, 604) when taken,
  push −$40000 along y; its tube walls are code $21 (plane set 4, below
  $3F60).
- The wave on the long ramp: a code-6 region (Track region block entry 2)
  that $1100A2 starts when a racing player is in zone 9 or 10 and
  $1007A5 allows it. Its keyframe pins its bands (0-$500), clears
  $1007A5, counts it in $1007A4 and zeroes +$25; each animation round adds
  to +$25 and round $1E allows the next wave. While one runs, block entry
  3 resets $42 animated vertex codes from a Track table, and each frame
  shown calls block entry 4 with the round: two routines stored as 68000
  code in the Track (rebuilt in `regions.cpp` over the tables beside
  them). In rounds $44-$53 the wave's picture is drawn without the ramp's
  end piece (course cell $F at x $F0, ring line $158 + table $11275C) and
  the piece drawn again over it ($112464).
- The dizzy surprise ($10AAD0): the race start rolls random(256)
  ($10068E); if it is under the course's chance ($10ABDC: 0, 200, 100, 65,
  65, 65) the first player rolling in one of the course's zones ($10ABF4:
  Beginner 9, 10; Intermediate 6, 7, 8; Aerial 7, 8, 10; Silly 20;
  Ultimate 8, 9) with 3-6 seconds left makes the racing marbles dizzy.
- Random numbers ($109136): a lagged additive generator over 13 longs
  ($109288, indices $1092BC/$1092BE: +1 and +2 modulo 13) whose two low
  bits come from graphics.library VBeamPos; the result is masked to the
  next power of two and reduced once. Since 2026-10-27 (the user's rule
  for all Joyport rebuilds) the port draws the raw bits from `std::mt19937`
  instead (`game/random.hpp`; seeded once per program start, the tools'
  races from a fixed seed, `MM_SEED=n` for another) and keeps the mask,
  the single reduction (so random(3) is 0 half the time, as in the
  original), the ranges and which events draw. The original's sequence
  depends on its display timing and is neither replicated nor fed to the
  port any more (`mmref.py --random` and `MM_SEARCH_RANDOM` removed; the
  old `runs/*-random.txt` logs are unused).

#### Aerial (2026-10-26)

- The start chutes (codes $17 and $18, $1175A4): a column (row) of cells
  from the region's cell marks the marble (+$62, timer $FF); the mark takes
  the steering ($1134C4) and the friction away. A tick before the race's
  clock runs already marks it (taken over from the recordings' first
  record).
- The vacuums (codes 11 and 13, $1170E8/$117466): while their animation is
  block entry $18 ($19) they pull a marble in the mouth's reach by a
  quarter of the unit vector to the mouth (division $1248D4 by the
  octagonal length, a word), capture it in the mouth (mode 10, count $20,
  +$62 = the region) and around the body bounce it back or, when it was on
  the body already, eat it (respawn). Code 13 tests code 11's body box and
  the start offsets an earlier test of the same call left on the stack
  (kept by the port). Mode 10 ($113C2C): eight sparkles (cells $44-$4B,
  jitter $1020C4) fly from the marble past a point beside the mouth into
  it; then the vacuum closes (block entry $13 / $14, sound $1020D4 by zone
  − $1E) and the marble reappears where it is (mode 4 without a respawn).
  Their pictures are drawn over a floor cell ($110A46: $110C48 by zone, the
  link kept in the cell record) instead of the playfield.
- Code 10, the launcher ($116FF8): a marble on its cell at its first frame
  is put on its point and thrown up (vz $A0000) and towards −y with random
  x and y parts; mode 3, timer $F, the launcher's script goes on at block
  entry $17. Its animate command plays sound $2F.
- Code 12, a bumper (four, a random group of block entries 3-18 next to the
  goal, bands 40-64): throws a marble out of its frame box. Rebuilt; no run
  reached it (the view does not seem to reach band 40).
- Random groups ($10FC02): a band entry without a script starts block
  entries 3+4k to 6+4k for k = random(4), unless a code-12 region is
  registered.
- Zone changes ($112F20, Aerial only from the region part on): sounds $21
  (zone $14) and $20 (zones 6, 7, then sound 6 after $3C updates, $10B48A),
  the pistons, then regions waiting for the zone resume with sound $1020EC
  by zone − $1E. Entering zone 4 ($1130BE) stops the waiting regions and
  closes the sucking vacuums. On Silly two once-a-race sounds ($20, $21).
  The interpreter's "if the zone is free" plays $1020D4 by zone − $1E;
  showing a region's frame leaves its point in the probe ($106A00), which
  positioned sounds read.
- The pistons ($1018BE, 36 × 16 bytes; three groups at Track region block
  +$23C): live while a player is in zone 4 or 5 ($11DFDE); each group
  raises three of its twelve pistons in one of four random patterns,
  animates over the playfield, then waits random(4) × 16 updates ($11DB4C,
  sounds $102148/$102168 by group); a raised piston throws up a marble on
  its cell at its foot and pushes back one running into it ($11E1E0); they
  cut the marble's sprite like course parts ($11E358). Codes 10-13 register
  in lists for the same occlusion ($110338, $105CB0 with $100 − y).

#### Silly (2026-10-26)

- The course runs bottom to top with the slope pulling uphill. Its bands
  count from descriptor +$10 alone (the port counted from the view offset
  with Silly's +$12 before), and the course start lays the tile rows into
  the playfield's ring for a scroll one line further than the one it
  starts with: tiles show a line higher than pictures and sprites
  (measured: ring line = scroll − $277).
- The funnel up (code $22, $117BF4): on the cell before the region's point
  the marble rises (vz +$9000 an update; at $3F74 lifted by 2) up to $3F95
  and comes out at (684 or 732, 688), picked by random(2), the other one
  when taken, sound $25; codes $23/$24 are its walls (plane set 5).
- Critters ($101AFE, 9 × $28; display types 7-9 by kind, Track header
  +$24 lists): spawned with bands $1D-$38 ($10DAAC, $11C78C) at random free
  places of the pen (16 × 16 cell bit map $11D478), wander in sixteenths of
  a turn (kind 7 turns ±2 at random; 8 and 9 along an axis, 9 stands one
  time in four; $11D002/$11CF30/$11D3D2), flee the nearest racing marble
  while one is on the pen's floor in zone 1 ($11D182/$11D08E); caught by a
  player within 4 (kind 7: 5) in zone 1: three seconds, bonus $E and 500
  points ($11CA44).
- Birds ($101C66, 10 × $3E; display type $F): with bands 3-$1B, every
  eighth update up to two drop in at the top of five columns ($11DA7C,
  sounds $11DA86) and walk towards −y in short slow and long fast runs; a
  wall squashes one (sound $2B on the screen); one meeting a player's
  marble eats it: mode 11, the sparkles gather in ($1138D4 counted the
  other way), then a respawn ($11D8EA). Their covers are $11EC42's kind 3
  in three columns where walls stand.
- Free display slots hold a box of $7FFF; new entries are placed by the
  boxes their slots last held ($112004 does not store the new box).

#### Ultimate (2026-10-26)

- The start: the marble must cross a two-row gap at the start floor's
  level. Up the ramp (rising in +y onto a strip 12 above the floor) with
  fire held, it flies over the gap onto the square round the funnel (code
  $25) and drops in; the funnel puts it out at (240, 320).
- The moving goal area (region code $19, $11E5EA-$11E992, set up by the
  course start): the course's last 30 map rows (78-107) exist in four
  versions, stored as further map rows (78, 108, 138, 168; "Ultimate's 90
  extra rows"). The region's frame list pairs each version's first row
  with a patch of the animated heights. A task renders the next version
  into a spare buffer through a message port ($10B1A4, $1094E2: $1E rows);
  the region's step polls it ($11E848): when done, the heights are patched,
  the cover bit-map set is chosen ($1006A4 = version × $90) and the display
  is told; the display ($1076B6, $11E588) swaps the buffers and asks for
  the next version ($11E7B0, the list wraps at −1). Rendering took 18-25
  updates in the recordings (it runs on what the main loop leaves over);
  the port takes 21 (the most frequent), replays the logged times. Pausing
  and leaving the course ($11E708, $11E6FC) wait for or redo a rendering
  (not modelled: they end or freeze the race in the port).
- The blocks of the goal area stand 36 above the floor: a fall breaks the
  marble. The route waits on blocks that stay up through each change
  (`maze.py`'s plan: components of the high cells per version, chained
  across the changes). Codes $2D-$3B are the course's floor rectangles
  (slippery, sticky, icy) found before.
- The goal line runs along x cell 87, rows 88-90 (zone 12 → −1): the goal
  block is entered from −x.
- Colour cycling ($108664 after the regions, Ultimate only: the course
  start sets $1006A7 to 0): every fourth update the three colours of two
  copper lists of the display block (+$18, +$1C) take the next of three
  triplets (+$20/+$24/+$28).
- A region without frames reads its display box from addresses 3-6 (its
  record pointer is −1): ExecBase's pointer, $00C002xx on the 512K A500;
  this orders Ultimate's two goal flags (box width −64, depth 2).
- Recorder: an interrupt taken at the world update's first instruction
  stops the breakpoint twice (three records in 2500 equal to the one
  before); `mmref.py` now drops such a stop and the tools skip such records
  in older runs. After Ultimate's finish no world update follows (the end
  sequence has none): `mmref.py` stops after 15 s without one.
- Finding a route the original follows: its update count lags now and
  then (a slow main loop counts two vertical blanks), which moves its
  scroll checks and with them the regions, and the moving area's rendering
  times vary. A route found on the port's own timing therefore parts from
  the original in the goal area. The search takes a recording's counts,
  start fields and rendering times (`MM_SEARCH_COUNTS`, `MM_SEARCH_START`,
  `MM_SEARCH_RENDER`) and goes on from the update where that recording
  parts from the plan; two rounds were enough (`ult_maze_fall`, then
  `ult_goal`). (The routes were found while the search still took the
  recording's random bits, since retired; Ultimate's runs part only where
  random choices matter.)
- The waits of the goal area come from `tools/ultimate_maze.py` (the high
  cells of each version's height map from `MM_MAP_PATCH` + `MM_PATH_MAP`;
  with a reach of 4 cells per version, a chain of cells to stand on at each
  change) and the search's `MM_SEARCH_PLAN` (be within `MM_PLAN_SLACK` of
  the plan's point when its version comes; the score looks where the
  marble will be when the next version is due).

- Vacuums and sparkles: $113C2C and $1138D4 set +$EB ("sparkles instead
  of the marble") even when no sparkle is left to draw (all in the mouth,
  or off the screen), so the marble stays hidden; the port keeps that flag
  apart from the sparkle count (`Marble::sparkles_shown`; aer_vacuum
  updates 703-711 showed the whole marble before, fixed 2026-10-27).
- Aerial's bumpers (code 12, $117E86): the box comes from the frame
  record with no check for the end of the frame list; at record -1 the
  original reads addresses 3-6 like the frameless regions' draw box (now
  one helper, `Race::frame_box_byte`). Before, the port threw there and the
  app closed (2026-10-27, found by `marble_madness_replay fuzz`).
- Steelie dust: the break dust list runs past its end while a shatter
  lasts; a record whose colour pointer is not in the game's data is not
  drawn (as for the player's marble; the original reads colours from
  wherever the pointer leads, not modelled).
- Aerial's pistons stop where they are when no player is in zone 4 or 5
  ($11DFDE clears the flag $1007A6 that $11DB4C's group animation needs and
  restores no picture), so a group caught rising stays half raised until a
  player comes back; the port does the same.
- `marble_madness_replay fuzz <content> <course> <run|-> <prefix> <seeds>
  <updates>`: random controls after a recorded prefix, reporting any
  exception (all six courses, 1500 runs of 1000 updates clean).
- Intermediate's goal: a small black block under the goal platform's
  lower right edge is the original's too (capture int_goal-1600); kept.

### The front end (2026-10-26, one player)

The program's main loop runs a handler by mode ($1005EA, table $10B4FA):
0 the race's world update ($10B1EE), 1 the attract demo (its hooks are
empty in this release), 2 back to the options, 3 a finish ($10B2AA), 4 out
of time ($10B368), 5 the options screen ($10B3DC/$10CCDC), 6 the end after
Ultimate ($10B470/$10C728). The between-race code waits in vertical
blanks ($108F90); the race takes two per update. Rebuilt in
`game/front_end.cpp` (`GameFlow`), drawn by `View::draw(race, overlay)`.

- The font (user decision 2026-10-27): the Kickstart's topaz 9, embedded
  like the C64 character ROM's glyphs (`shared/amiga/topaz.cpp`, with a
  notice that Joyport's MIT licence does not cover it). Extracted from
  Kickstart 1.3 (34.5): the graphics library's TextFont at $FC84DE, found
  through a pointer to "topaz.font" (Kickstart 1.x keeps the name pointer
  6 bytes before tf_YSize): 10 x 9, baseline 6, characters $20-$FF, every
  glyph 10 pixels wide; stored as 10-bit rows per glyph plus a character
  table. (A first version read it at import from a Kickstart ROM the user
  added; replaced the same day at the user's decision.)
  Checked: the options screen's text pixels equal the capture
  (prac-options-50, 3133 text pixels, no difference); the score bar's
  digits sit on the capture's pixels (aer_goal 560).
- The score bar ($10B5D6, $10A88A, title $10C980): its own bitmap cleared
  to colour 10 ($888), colours 10-15 from the copper moves at $10BBB8
  (11/12 red player's clock/score $F00, 13/14 blue $00F). The score in
  seven places at the record's x (0; $D8 for blue), the clock in two
  places with zeros at x $80 ($50 with two players), both at y 1 (Move y
  + 6, the baseline).
- Texts: lists of (column, row, text, next) entries ($10A46A) drawn on
  the playfield's 8 × 8 grid from column 3 ($10A2FA: x = (column − 3) × 8,
  baseline at row × 8 + 7) with pen 7 over pen 4 (JAM2), in topaz 9 (10 ×
  9 cells, opened at $109C20); a list of spaces makes a box's grey. Numbers
  ($10A59A): spaces, zeros, or left-aligned. Boxes ($109C82/$109E64/
  $109F8A) save the playfield under them and are filled with pen 4.
- The options screen: the playfield cleared to pen 4, the texts of
  $10D152/$10D186, the values (players, difficulty, the red player's port
  and input device: names at $10D206/$10D236), the bar's title "Marble
  Madness!!" ($10C980: a letter every 14 pixels in colours 11, 14, 12, 13,
  which pulse here through the bar's clock and score shade tables), and a
  yellow marble as pointer (display type $2C: record $106928, ramps
  $106888) that follows the mouse ($119648); a click changes the item of
  the pointer's line ($40-$4F players, $50-$67 difficulty, $68-$77 port,
  $78-$8F input device, $90-$AF the blue player's, from $B0 GO!) and waits
  $14 blanks. The text pen is the last course's pen 7 (Practice's red at
  boot, Beginner's blue after it). In the port the joystick moves the
  pointer and fire clicks; the number of players stays 1; the input
  device sets the port's steering (Joy Stick: joystick; Trackball,
  Mouse: screen directions).
- GO ($10B3DC): sounds 3, 4, 2 (one player), the players' records reset,
  the display off while the course loads.
- The course's introduction ($106A64): "TIME TO FINISH" (Practice,
  Beginner; the clock starts at 0, $114AA2) or "EXTRA TIME FOR", the
  course's line and its seconds (table $102350 by difficulty) counted into
  the clock five at a time: $3C blanks, then $14 a step, then $3C − time;
  Silly adds "EVERYTHING YOU KNOW IS WRONG!". Then the course's music and
  the race.
- The race ends (mode 3, $113F72) once every racer has finished (status
  3) or one has and the others are out — checked against the recordings:
  the port reaches mode 3 in the update after the last record of
  `ult_goal`, mode 4 in that of an out-of-time run.
- A finish ($10B2AA, $10BC28): sounds $F and $10 (not on Intermediate),
  "BONUS FOR / TIME LEFT:" with 100 points a second (up to 99) in a box at
  (10, 40) for $180 blanks, the course's finish sound ($102124), the
  points, the sprites go; the next course loads while the last stays on
  the screen, then a moment of black.
- Out of time ($10BE7C): sound $19, $F, $10, "OUT OF TIME / GAME OVER" in
  pen 9 in a box at ((10 − 3) × 8, $80) for $B4 blanks, then the options.
- The end ($10C728): sound $20, the playfield cleared, sixteen marbles at
  random places and speeds bouncing between x 8-$118 and heights
  $30-$F8 ($11E9A8, $11EB10), the tally ($10C530: 20000, seconds × 1000,
  died × −1000) counted into the score $FA an update ($10C6BE), $B4
  updates, the options. The bar keeps the score it had.
- The bar's clock flashes through $10B9E4's shades from four seconds left
  ($10B7E6, $10B946) and shows at most 99 ($10B654).

Captures of the original (`mmref.py --after`, `--end-after`,
`--options-shot`, `--poke`; the race-end handlers stop the recording) match
the port's screens in layout, colours and timing: the options screen, the
Beginner introduction at the same count (box one line low fixed), the
finish box, the out-of-time box, the end. `marble_madness_replay flow
<content> <script> <dir> <ticks…>` runs the whole game headless (script
lines as elsewhere, `race` waits for a race, `finish` ends one).

### Options and cheats

The module offers steering (joystick grid or screen directions), the
shared VHS look, and two cheats (not the original game; Race::Cheats):
- Stop the timer: the race clock ($10B7E6) does not count down.
- Unbreakable marble: a landing hard enough to break the marble ($114DEC)
  becomes the hardest landing that still bounces (delay $3E - bounce), and
  a drop of more than 128 units onto ground lands instead of being lost;
  falling into nothing or under the course still costs a marble. Tried on
  Beginner (`trace` with `MM_CHEATS=timer,falls`): a break at update 215
  becomes a bounce.

Quick save and load (2026-10-27): the module keeps a copy of the whole
`GameFlow` (screen stage, race, view, sound board with its music and
effects engines, random numbers); `Owned<T>` copies the owned parts, the
sound board shares its read-only sample memory. The options stay as set
when loading. Checked with `MM_SAVE_AT=tick marble_madness_replay flow`
(copies the game at that tick and runs both on, comparing pictures and
sound every tick): saves on the options screen, in a race, on the
finish and introduction screens and in the next course play on
identically, and a run with a copy equals one without.

### The crowd race (2026-10-27, not the original game)

A pause-menu mode ("Crowd race" page, off by default): seven more marbles
race the player, steered by the computer (`game/crowd.{hpp,cpp}`). On
mid-race, they appear on the course's start; off, they go.

- Marbles: `Race::marbles` has 8 slots; 0-1 the players' as before, 2-7
  only while the crowd runs (1 too in a one-player game). They race by the
  players' rules (rolling, collisions, enemies, falls, breaking, respawn by
  zone) but make no sounds, score for nobody, take no drop or first-home
  bonus, get no cheats, and leave the course once home. Enemies hunt only
  the players; the course's moving pieces (regions) react to the players
  only (a crowd near Beginner's drawbridge kept it up). A crowd marble
  never keeps a player waiting to respawn and two never wait for each
  other; crowd marbles a funnel holds, or that overlapped when the update
  began (a funnel's exit spot), do not collide (they pinned each other).
  An empty slot keeps its own number (it zeroed the player's push when
  reset to player 0: the "goal stops my input" bug).
- Colours: marble 1 the original's blue; 2-7 green, purple, cyan, orange,
  white, pink in the players' ramp layout (Joyport's own).
- Starts: three round the first player's start cell, four round the
  second's (descriptor +$14/+$16; Aerial's two launch platforms), on the
  start's platform, middle first, 2 cells apart where there is room.
- Navigation from the course data: the Track's zone lines give each zone
  its way out towards the goal; a flood over the height map's cells
  (rise 3, climb 10, drop up to 48; drops and narrow ways dearer) gives a
  distance to go per zone. Moving ground (height codes $800-$FFF) without
  floor counts as its neighbours' height. Funnels and lifts the floor does
  not show are portals taken from recordings of the original (beg_goal
  700/792, int_goal 333, sil_funnel 196, ult_goal 191 with its run-up lane
  from 181,108), plus Beginner's drawbridge.
- Driving: every 4 updates a marble tries 26 pushes on copies of the race
  (8 directions, with and without fire, held 6 updates then its usual
  steering or held all the way, no push, the usual steering) for 20
  updates (on until it lands when airborne) and keeps the best (nearest the
  goal, a loss anywhere ruling a choice out). Usual steering: a few cells
  down the distance, at its own pace, slower where narrow, braking before
  a step it could not roll. Ultimate's run-up lane: flat out with fire.
  Stalled for 200 updates (afraid of every way on): usual steering for a
  while. About 1.5 ms an update on this Mac.
- Off the screen the course's moving pieces do not run (as in the
  original), so a crowd marble well off the screen within 2 cells of a
  funnel's (or the drawbridge's, or the wave channel's) way in is carried
  to its way out, to a free spot, with the funnel's push out ($40000 by
  course: race.cpp's through_funnel calls).
- Plans are scored by the distance to the goal over the whole course
  (falling back on zones), so a ride the map prefers beats a quick zone
  line; a funnel taking the marble (a jump of over 24 pixels) is a
  success; falling into a funnel's mouth is no loss; next to a mouth is
  no success (marbles hovered there). Dashes (a direction with fire held
  the whole look-ahead) cross Beginner's drawbridge in its short time
  down.
- A funnel's mouth is reached from any floor within 2 cells of its way
  in, whatever the height (Intermediate's funnel lies 124 below the
  plateau beside it: the crowd idled in its corner, then wandered off).
- Vacuums: the crowd obeys their rules (the pull, $1170E8, acts on its
  marbles: ~$3C00 an update towards the mouth), but crosses their reach
  near full speed (~3.7 pixels an update), so it seldom got caught; the
  plan no longer counts a capture as a loss (it had dodged every one), and
  within 56 pixels of a vacuum a crowd marble goes carefully (no fire, at
  most 1.25 pixels an update; user request): with the vacuums on the
  screen 1-3 get caught a race. Off the screen vacuums do not run.
- The dizzy surprise stuns the crowd's racing marbles too; hard landings
  limit their pushes by the same rule as the players' (push(), $1134C4).
- Place and results (2026-10-27): `Race::standings()` (home in order,
  then by the distance to the goal); the bar shows the player's place
  ("3RD/8") at x $D0; the finish screen adds a results box under the
  bonus box (each name in its marble's light shade, View::Text::colour).
- Sounds (2026-10-27): race sounds carry the crowd marble that made them;
  each crowd marble plays on a sound board of its own (four more Paula
  channels each, past the Amiga's four), music left out, mixed at 0.6 on
  the screen fading to nothing 256 lines off it, only while racing, then
  clamped. `flow` with MM_FLOW_WAV writes the game's audio.
- Skill (2026-10-27, "Computer skill": Easy, Normal default, Hard): Hard
  as before (fire, no lapses); Normal: no fire (but Ultimate's run-up)
  and a lapse about once in 600 updates (12-31 updates of a wrong push or
  careless driving); Easy: 60% pace, stops pushing above 2.5 pixels an
  update, lapses once in 250, one run-up in three without fire. Four
  starts per course (banding off), crowd marbles home / losses: Hard
  167/168 / 203, Normal 161/168 / 617, Easy 150/168 / 1032.
- Always-on course pieces (question, not built): every course's regions
  fit the 25 slots (13/11/3/11+one random group/5/20); they would have to
  run in course space without drawing off the screen (their pictures go
  into the playfield's ring of visible lines; vacuums measure from screen
  positions), enemies and pistons follow the players' bands and zones.
- Rubber banding ("Keep the pack close", on by default; user request
  2026-10-27): a crowd marble ahead of the leading player by the map's
  distance to the goal (more than 60 units) gets no fire and a speed
  limit falling from full to 0.75 pixels an update at 260 units ahead
  (braking down to it); not by a funnel's exit or while a course piece
  holds it. Measured with `crowd` MM_MISTAKES=250 (the computer-driven
  player loses its marble every 250 updates): the pack's mean lead
  +62/+5/+138/+87 (Beginner/Intermediate/Aerial/Silly) without, -72/-73/
  -46/0 with; "far ahead" (400 units) from up to 20% of the time to at
  most 4%. MM_NO_BAND turns it off in the tool.
- Off-screen carrying puts a marble on the funnel's exit or the spot the
  funnel itself uses when the exit is taken (race.cpp); a spot beside it
  could lie inside the tube's walls and held the marble there for good.
  A crowd marble past the goal line but not finishing counts as home; one
  off the map altogether respawns.
- Intermediate's wave (2026-10-27, user request): the channel x 81-84,
  y 76-97 is floor 16148 while drawn (x 85 open but for the bridges at
  y 81-82, 93-94; seen with MM_PLAYER_AT=722,697,10 MM_GROUND); its cells
  cost little and the lane beside it (x 86-91, y 74-84) much, so the crowd
  rides the wave (5 of 7 in the test; they roll along over the crests).
- Checked with `marble_madness_replay crowd` (MM_IDLE, MM_SOLO,
  MM_PLAYER_ZONE, MM_LOSS_MAP, MM_LOSS_FRAMES, MM_TRACE_MARBLE, MM_PILE;
  detectors for stuck, held and waiting marbles and the player's push):
  four starts per course, 165 of 168 crowd marbles home; quick-save copies identical with the crowd
  switched on and off (`flow` with "crowd on"/"crowd off" lines and
  MM_SAVE_AT); random play with the crowd on (MM_CROWD fuzz) clean; the 19
  recorded runs unchanged with it off.

### Sound

Each course has a sound file (`*snd`, loaded at course start): a table of
35 entries (kind, link, address) and their data. Kinds: 0 music, 1 effect,
2 stop the music, 3 count down linked effects (rolling sounds), 4/5 a flag
nothing reads. The race asks for entries by number ($1222E4 with a side:
left channels 0/3 or right 1/2; $1224D8 takes the side of the probe's
point). Course start plays $10210C[course] (the race music), a finish
$102124[course] (stop), running out of time $19.

- Effects (SfxTask $121214): an 18-byte record {segment chain, volume
  envelope, pitch envelope, duration, priority, pan}. Segments (14 bytes:
  next, count, skip, wave) play a wave (12 bytes: words, period, volume,
  samples) `count` times per audio.device write (-2: until stopped); two
  writes stay queued per channel. Envelopes are (rate, target) lists of
  16.16 values stepped each tick. A timer asks for 1/60 s on the vertical
  blank unit, so on PAL everything steps 50 times a second. Four slots;
  channels by side (pairs $1217BC/$1217C0, stereo pairs $1217C4 with the
  volume shared by screen x), taken from lower priorities ($120A4E).
  Period and volume go out as ADCMD_PERVOL when they change (periods under
  $72 as $48).
- Music ($11F5E8-$1200C0, task $11FABA): a score of 8 voices (pattern list,
  pan, priority); patterns of note bytes and control bytes (duration code,
  bit 6 = early release and restart); durations $1201EE, semitone periods
  $1200CA, transpose 5, tempo step (tempo << 7 << 9) / 150. All voices use
  one IFF 8SVX instrument (header, samples by octave, volume and pitch
  envelopes); a note plays its octave once, then loops the repeat part.
  The task makes up missed blanks, five at most; its count stands still
  while stopped, so a new score starts with five steps at once.

### Verification

Harness (`harness/`): FS-UAE (prb28 fork with the GDB stub, Kickstart 1.3)
on `mkboot.py`'s disk; `mmref.py <program.hunks> <script> <updates> <out.bin>
[--course N] [--shots N,..] [--copper N,..]` waits for the options screen,
locates the program's hunks in memory, starts a race, stops at every world
update ($10B1EE), records the players' records, globals ($1005E0-$1006FF),
scroll, frame counter and pushes, and writes the scripted joystick pushes.
`showrun.py` prints a recording; `picdiff.py` compares a native picture with
a capture (nearest-colour matching against the capture's colour
management); `winid.swift` finds the FS-UAE window for captures.

`build/marble_madness_replay`:
- `lockstep <content> <run.bin> [course]` runs the recorded pushes from the
  course start and compares 25 marble fields after every update. Three
  races on other seeds run alongside; the first update where any of them
  differs from the main race (player, creatures or pistons) is where a
  random choice first matters ("random: choices matter from update K; N
  of N updates before match"). Up to it the race must equal the
  recording; after it mismatches are counted but only printed with
  `MM_SHOW_ALL=1`, and the original's random choices in its creature log
  are checked by their rules instead ("random rules": a piston group's
  pattern with its first piston and its wait of random(4) × 16, a
  critter's start on the pen's 4-unit grid, a bird's walk count).
- `pictures <content> <run.bin> <out dir> <updates…>` (MM_INDEX_DUMP=1 also
  writes the colour indices).
- `trace <content> <course> <script> [updates]` prints mode changes.
- `search <content> <course> <script out> [updates] [beam width]` finds
  input that reaches the goal (beam search on the rebuilt race).

`mmref.py` also takes `--covers` (stops where $11EC42 returns and writes
both players' cover lists: the Painter consumes them before the next
tick), `--audio <snd file>` (logs every audio.device request with the
vertical blank count, the music starts and the ticks) and `--peek
A:N,...` (`@A+D:N` reads past a pointer stored at A, `@@` past two,
`!A:N` a machine address). Its start-course poke writes one byte
($102379 next to it is the slow scroll speed; an earlier word write zeroed it and stopped scrolls in
all older recordings, which were replaced). Recordings may begin a tick
before the course start; the tools find the first update with the clock
running. `audiodiff.py` compares request logs update by update (as sets:
the original's world update may run into the next blank before its sound
calls), `picmap.py` compares pictures by colour mapping (the capture's
colour management shifts saturated colours too far for `picdiff.py`).
`marble_madness_replay audio <content> <run> <wav> [original log]` replays
a run with sound (each update as long as it was in the original, which
slows down to three blanks now and then; the port does not).

Runs (Practice unless named Beginner, Intermediate, Aerial, Silly or Ultimate, `harness/runs/`;
`harness/regress.sh [prefix]` replays them all; the
`.bin.gz` creature logs are compressed, `gunzip` before use, inputs in `harness/inputs/`).
The results in the table were reached while the port still rebuilt the
original's generator and took the random bits each recording logged.
With the standard generator (2026-10-27) every run equals its recording
up to the update where a random choice first matters; sixteen runs still
match to their end (on Intermediate and Ultimate the acid's path picks
happen to fall the same way on the default seed), while `aer_pistons`
(from 307: the pistons' patterns), `sil_funnel` (from 68) and `sil_goal`
(from 63: the critters' starts) part from it there. The original's
choices checked by their rules: 119/119 piston, 9/9 and 9/9 critter.

| Run | Content | Result |
| --- | --- | --- |
| `roll1` | rest, push, roll, fall off, respawn, settle | 598/598 updates equal; covers 544/544 |
| `goal1` | search's route: start ramp, rails, goal, finish roll; copper list at 214; shots | 238/238 equal; covers 189/189 |
| `drop1` | goal route, then a drop into zone 3 (3000 points); shots | 299/299 equal (score, bonus picture fields); covers 299/299 |
| `edge1` | along the right edge of the screen (x up to 281) | 158/158 equal; covers 129/129 (the marble stays visible); pictures exact at 70 and 110 |
| `beg_shatter` | Beginner: a hard landing shatters the marble, the broom sweeps up | 528/528 equal; covers 369/369; pictures exact through the sweep |
| `beg_steelie` | Beginner: a steelie patrols, hunts, chases and knocks the marble | 298/298 equal; steelies 298/298; covers 232/232 |
| `beg_goal` | Beginner start to goal: steelies, slinkies, both funnels, the ice, the goal | 949/949 equal until the original moves on to Intermediate; creatures and all 25 regions 949/949; covers 877/877; pictures exact where the view stands still (1-2 pixels: the ice's glints) |
| `beg_slinky` | `beginner_goal.txt` for 700 updates, with the display list | 698/698 equal including the display order; pictures of a slinky over or behind the marble exact (540; 516-530 with the capture's 1-line scroll shift) |
| `beg_bridge` | Beginner: waits beside the drawbridge while it goes up and down | 998/998 equal; creatures and display list 998/998; covers 877/877; the raised bridge cuts the marble's edge as in the original (910-940); 890 and 960 differ by the bridge's and marble's in-between frames |
| `int_goal` | Intermediate start to goal (99 s, `--time`): maze, acid, funnel, ramp wave, waves | 1653/1653 equal until the original moves on to Aerial; creatures (acid, regions, display list) 1653/1653; covers 1604/1604; sound calls identical (`int_goal-sounds.txt` from a second recording); pictures exact (200, 600, 1000; the wave 1310-1350, its end at 1600) |
| `int_acid` | Intermediate: puddle 0 dissolves the marble | 599/599 equal; creatures 599/599; covers 575/575; sound calls identical ($1D, $13); pictures exact during the dissolve (336-354) |
| `aer_goal` | Aerial start to goal (99 s): chutes, vacuums, the goal | 899/899 equal; creatures 899/899; covers 604/604; sound calls identical; pictures exact (200-700, the vacuums) |
| `aer_vacuum` | Aerial: a vacuum swallows the marble | 899/899 equal; creatures and regions 899/899; sound calls identical |
| `aer_pistons` | Aerial: the launcher, zone 4 with the pistons | 1099/1099 equal; creatures with pistons and groups 1099/1099; covers 853/853; sound calls identical; pictures exact apart from a capture a playfield update ahead |
| `sil_goal` | Silly start to goal (99 s): the pen, the funnel up, a bird eats the marble, the goal | 1049/1049 equal; creatures (critters, birds) 1049/1049; covers 655/655; sound calls identical; pictures exact (480-900) |
| `sil_funnel` | Silly: up the funnel | 399/399 equal; creatures 399/399; sound calls identical |
| `ult_goal` | Ultimate start to goal (99 s): the run-up over the gap, the funnel, ice, acid, steelies, the moving goal area | 1595/1595 equal; creatures (with the area's pictures and cover set) 1595/1595; covers 1538/1538; sound calls identical up to the finish; pictures exact at the goal (1560, 1590, with the winner's colours and the bonus) |
| `ult_maze_fall` | Ultimate: a block drops from under the marble in the goal area, a respawn on the icy platform | 1699/1699 equal; creatures 1699/1699; covers 1570/1570 |
| `sound1` | goal route with the audio log | requests: 460/460 in total, 203 of 224 updates identical, the rest one-blank shifts |

`beg_goal-audio.txt` (Beginner): the same requests, but with more sounds
at once a sound sometimes takes the other channel of its pair (whether a
channel is free depends on the same one-blank timing); 5059 original
against 5080 native requests.

`mmref.py --creatures` logs the steelies, slinkies, regions, display list
and acid per update (`<out>-creatures.bin`, layout in
`<out>-creatures-layout.txt`); the lockstep tool compares them
(`MM_REGIONS=1` adds the regions). `--sounds` logs the
sound calls ($1222E4; `lockstep` prints the port's with `MM_SOUNDS=1`);
`--time N` gives the players N seconds (a course started from the options
has only its own time). The recorder holds the script while the race's
clock has not started, and the tools start at the last record before the
clock counts, so runs with and without ticks before the race line up.

Native research helpers: `search` with `MM_SEARCH_ZONES=1` (ranks zones
by their distance to the goal in the zone graph), `MM_SEARCH_ZONE_TARGET=
z:x,y`, `MM_SEARCH_PATH=x,y[,rise,fall]` (a flood over the floor cells
with the game's side analysis: no walls higher than `rise`, no drops
deeper than `fall`; `MM_PATH_MAP=file` dumps heights and distances),
`MM_SEARCH_PREFIX=script` (search on from a script's end),
`MM_SEARCH_HOLD=n` (updates per step), `MM_SEARCH_CODE=n` (done when a
region of that code takes the marble), `MM_SEARCH_MODE=n` and
`MM_SEARCH_ACID=n` (reach a mode, head for puddle n − 2), `MM_TIME=n`,
`MM_SEARCH_FIRE=1` (also the eight directions with fire), the recording
inputs above (`MM_SEARCH_COUNTS/START/RENDER=run.bin`),
`MM_SEARCH_PLAN=file` with `MM_PLAN_SLACK=n`, `MM_SEARCH_TRACE=n` (print
the prefix's updates from n on), `MM_MAP_PATCH=address` (apply a height
patch before `MM_PATH_MAP`), `MM_ZONE_DUMP=1` (zone lines and ranks);
`lockstep` with `MM_NO_COUNTS=1` / `MM_NO_RENDER_TIMES=1` (the port's own
timing, research), `MM_DISPLAY_DUMP=n` (display slots); `words <content>
<course> <hex address> <count>` prints Track words;
`trace` with `MM_START=x,y,scroll,z`, `MM_CHEATS`, `MM_TIME`,
`MM_REGION_CODES=1` and `MM_PICTURES=dir,first` (the view after each
update); `ground <content> <course> x y r` prints heights; `cell` prints a
marble cell. Routes are found in stages (Intermediate: funnel, exit,
ramp, plank, waves, goal) with the course picture of
`marble_madness_assets export` as the map.

`drop1-audio.txt`: 735 of 737 requests equal; the difference is one
envelope step at each end of a sound whose start or stop the original's
world update issued a blank earlier or later.

Pictures: the course view, the marble behind walls (5 moments on the
rails), the respawn sparkles, the drop bonus picture and marker are
pixel-identical to captures (the capture shows the picture of the update
before), apart from 1-3 playfield pixels at the view's bottom in `drop1`
(open item).
