# Cannon Fodder (Amiga) — how the original works

What was learned while rebuilding Cannon Fodder (Sensible Software / Virgin,
1993): the three disks and their own file system, the loader and program,
the data formats, the mission loop, the view and camera, every object kind,
the campaign and front end, the RJP1 music player and the effects, and the
original's own bugs. The remaining work is in [plan.md](plan.md).

## Sources and evidence

- **Images:** three ADFs, `Cannon Fodder (1993)(Virgin)(Disk N of 3)[cr
  CSL].adf` (SHA-256 below, in "Disks, crack, loader"), a cracked copy (the
  user agreed to its use). The game files look untouched; the crack adds an
  intro loaded by disk 1's boot block.
- **Method:** everything here comes from these images (own Sensible-disk
  reader and RNC unpacker, capstone, a Ghidra decompile) and from runs in
  FS-UAE (A500, Kickstart 1.3, GDB stub). No outside material on this game
  was used; RetroReverse has none. The model knows the game in general terms
  (a squad, named recruits, a hill of graves); nothing of that is used as a
  finding.
- **Addresses:** `$xxxxx` is a location in the $80000 layout of `FODDERC`
  (`work/F.bin`, equal to the running program at its entry); `+$nn` is an
  offset in an object record.
- **Paths:** source paths are relative to the Reprise root. Evidence lives
  outside the repository in `.analysis/cannon_fodder_amiga/`:
  - `disk/`, `files/` (files as stored and unpacked), `work/` (flat image,
    `all.c`, recordings `*.trace`), `ghidra/`;
  - `harness/`: `run.sh`, `mkboot.py`, `cf.py`, `tomission.sh`,
    `ticktrace3.py` (the tick recorder), `sndtrace.py`, `camtrace.py`,
    `display.py`, grab scripts;
  - `tools/`: `sensi.py`, `rnc.py`, `flat.py`, `m68kdis.py`, `callers.py`,
    `replay_all.py` (replays every recording).
- **Checking tool:** `build/cannon_fodder_assets` (modes `import`, `export`,
  `view`, `camreplay`, `replay`, `sndreplay`, `music`, `play`, `front`,
  `graphics`, `savecheck`, `loadsave`). "Joyport" in the notes is the native
  re-implementation's earlier project name.

## The original's bugs and oddities (summary)

Each is described where it belongs below.

- **Cliff test with swapped coordinates:** while sliding on a cliff or into
  a hole, `$A22C8` gets x as the row and y as the column (kept).
- **Enemy tank search:** its test of `+$4C` branches to the next
  instruction either way, so it searches every tick.
- **Slot latches:** when `$9819A`/`$98104` find no free slot they latch,
  and every later search of that kind fails until an object is removed
  through `+$24`.
- **One-object-a-tick scans** wrap at 43, not at the table's 44 entries.
- **Enemy aim** uses a register left over from before `$9DA04`
  (hypothesis: the enemy itself).
- **The view's ring** leaves columns hidden under the panel (the widescreen
  option draws the map's own pixels there).
- **Disk 2** has one zeroed sector (in `DESBASE.INS`); the same file on
  disk 3 is intact.
- **Named but missing files:** `disk2.ra`, `dir`, `abgnd32.lbm` are named
  by the program but on no disk.

## Disks, crack, loader, program, research setup

Images (in `Joyport/`), 901,120 bytes each:

| Disk | File | SHA-256 | MD5 |
| --- | --- | --- | --- |
| 1 | `Cannon Fodder (1993)(Virgin)(Disk 1 of 3)[cr CSL].adf` | `2b01cbc94561684ca337b98006af7fb9a7354dd0767426eceb113650f1372b40` | `7a61d6a9bd7ac1a3249ef167ae136af7` |
| 2 | `… (Disk 2 of 3)[cr CSL].adf` | `9089815bd1d4f72befcafe48b347ac105b29ab9cf5175237c0ac754b02528db1` | `7b4e5002a472858bbf16bbcbd17e348e` |
| 3 | `… (Disk 3 of 3)[cr CSL].adf` | `87ea9c1d7f90473feaa37e959dcc44b8ab86b45f6d52eb34f039cb67db11db2e` | `b2ca99a818fc3df46f1b151473b4a4f6` |

### Sources and limits

- Everything below comes from these three images (static reading; no run in
  an emulator yet). No disassembly, port, map, editor documentation or guide
  of this game was consulted. RetroReverse has no Cannon Fodder material.
- Disclosure: the model doing this work knows the game in general terms
  (it is well known: squad of soldiers, recruits with names, a hill of
  graves). Nothing of that is used as a finding; every statement here was
  checked against the image (file names, sizes, decoded pictures, code).
- RNC ProPack is a general-purpose packer of the time; its method-1 format
  was implemented from general knowledge of the format and is confirmed by
  both CRC-16s of all 44 packed files (see "Data formats").
- Tools: `tools/sensi.py` (directory + sector chains), `tools/rnc.py`,
  `tools/unpack_all.py`, `tools/flat.py` (relocated program image),
  `tools/m68kdis.py` (capstone), `tools/pics.py`, `tools/blk.py`/`blktry.py`
  (tile layout experiments), `tools/maprender.py`, `tools/filetable.py`
  (the importer's C++ table), `tools/tracks.py` (per-track entropy);
  Ghidra 12.1.4 headless (`ghidra/ghidra.sh import|all F`, flat image at
  $80000, 616 functions found by auto-analysis, `work/all.c`).
- Working copies: `work/d1..3.adf`, `disk/d1..3/` (files as stored),
  `files/d1..3/` (unpacked), `work/png/` (renders).

### No AmigaDOS

None of the disks has an AmigaDOS file system. Disk 1 has a DOS boot block
("DOS\0", valid boot checksum) whose code loads with trackdisk.device; disks
2 and 3 have no boot block (disk 2's sectors 0-1 hold file data, disk 3's are
zero). All three have a Sensible Software directory on track 3.

### Boot (disk 1, boot block, read from the code at +$0C)

1. trackdisk read: offset $400, length $2400 → $40000; `JSR $40000`.
   These are disk 1's sectors 2-19 (track 1 shows high entropy): the
   cracker's intro (CSL), presumably. Not needed by the game.
2. trackdisk read: offset $42000 (track 48), length $A000 → $60000; motor off.
3. Copies a small trap handler from boot+$E0 to $180 and sets the TRAP #0 and
   TRAP #1 vectors ($80, $84). The TRAP #1 handler compares a 4-character
   tag at the caller's pointer with "pstu" and "rank" and only flashes the
   background colour ($DFF180 = $F) for them — probably the crack's hook into
   loads of `pstuff.lbm`/`rankfont.lbm` (hypothesis; not traced).
4. Walks the file chain from track 48 sector 0 inside the $60000 buffer
   ($3EC6 bytes, 510 bytes per sector, link = next (track^1, sector); the
   track byte is turned into a buffer offset by `bchg #0` and `- $30`), copies
   it to $70000 and jumps there. That file is `fload` (the directory lists
   it: start 48/0, size $3EC6).

The loader-from-boot-block may be the crack's replacement of the original
boot; the retail boot is not known (no retail image).

### The loader `fload` ("Nufload", "Version 1 21/5/93")

Runs at $70000 without the OS: own disk routines (MFM sync $4489, an
"SOS6" tag), keyboard table, interrupt vectors $68/$6C. At $73B4C it probes
memory and picks the program:

| Memory found | Program | Loaded to |
| --- | --- | --- |
| fast RAM at $200000 | `FODDERF` | $200000 |
| 1 MB chip ($803FF not mirrored at $3FF) | `FODDERC` | $80000 |
| 512 KB chip + slow RAM at $C00000 | `FODDERS` | $C00000 |
| 512 KB chip only | none (endless loop `$73C76`) | — |

then `JMP` to the load address. On this disk the three directory entries
point at the same sector chain (start 10/10, $32B32 bytes): one
self-relocating file serves all three addresses.

### The program `FODDERC` (207,666 bytes, SHA-256 `5e725f92…e30f76`)

- +$00: `PEA -2(PC)` … `JMP` to a relocation stub at +$2CA2E.
- The stub adds the load address to each u32 at body offset n (body = file
  +$1A), for every n in the list at +$2CA52 (6,199 entries, ends with a
  negative value exactly at the end of the file), then moves the body
  ($2CA18 bytes) down to the load address and returns there.
- Entry after relocation: `JMP base+$5D92`. Startup at $85D92 (base $80000):
  68020+ test (illegal-instruction trap, `MOVEC D0,CACR`), stack at $100000,
  interrupts and DMA set up, level-3 handler $8A2E2, then file loads (`JSR
  $8B3E8`, a chip buffer at $3D34).
- `tools/flat.py FODDERC 0x80000 work/F.bin` builds the image the stub
  builds, for Ghidra and disassembly.
- Data found in it so far: the file name lists (front end $86BEF…,
  "mapm01.map" $86DE4, landscape prefixes "jundesicemorint" $87718, suffixes
  ".blk.pal.spt.hit.bht.swp.ins.sng" $8A64C), the recruits' names ($917E0,
  "JOOLS", "JOPS", "STOO", … 9-byte records: 6-character name, then three
  bytes still open), texts ("TIME TO DIE" $8855A, "PRETENTIOUS1-3",
  credits block near $9D0CA "Sensible Software Game … STARRING …").
- `abgnd32.lbm` is named in the program but on no disk (unused, or a
  leftover; open).

### File system ("Sensi disk")

See "Data formats" for the byte layout. Every file on every disk follows its
chain to its full size, except one: disk 2's `DESBASE.INS` runs into track
24 sector 0, which is zeroed in this image (506 bytes differ from the copy
on disk 3; its link reads 0000). Disk 3 holds the same file intact (the
first 7,650 and the rest of the bytes equal it), so the importer takes it
from disk 3. Everything else (303 files) reads and unpacks with valid CRCs.

### What is on the disks

- Disk 1 (26 entries): `fload`, `FODDERC`/`FODDERS`/`FODDERF`, screens
  `CFTITLE.RAW` (title), `SENSPROD.RAW`, `VIRGPRES.RAW`, `1.RAW`-`8.RAW`
  (black-and-white photographs), `WON.RAW` ("THE END"), `DISK2.RAW` (insert
  disk 2), `FONT.RAW`; pictures `pstuff.lbm`, `rankfont.lbm`; music
  `JON.SNG`/`JON.INS`, `WARX4.SNG`/`WARX1.INS`; `disk2.ra` (2,760 bytes,
  unknown); marker `disk1`.
- Disk 2 (139 good entries): mission maps `mapm1`-`mapm36` (`.map` + `.spt`),
  jungle, ice and desert sets (`*base.blk`, `*sub*.blk`, `.hit`, `.bht`,
  `.swp`, `.pal`, sky/mid/fgnd/fgn2 `.pl8`, `*heli.pal`, `*army.lbm`,
  `*copt.lbm`, `*play.lbm`, `*BASE.SNG`/`.INS`), `hills.lbm`,
  `morphbig.lbm`, `grave32.pal`, `font.pl8`, `dir` (8,772 bytes, unknown,
  looks random), marker `disk2`. `ICEBASE.BLK` and `icebase.blk` are two
  entries with identical contents.
- Disk 3 (137 entries): `mapm37`-`mapm72` (and `mapm1.spt`), moor and interior
  sets, the desert set again (identical except `desbase.bht`: 4 bytes
  differ), `startup.lbm`, marker `disk3`.

The importer installs every entry as `diskN/<lower-case name>` (300 files,
3.7 MB unpacked); `FODDERS`/`FODDERF` are skipped (same bytes as `FODDERC`).

### Reference machine

FS-UAE with 1 MB chip RAM (the program runs at $80000, equal to
`work/F.bin`), GDB stub on port 6863; see "Reference machine" under Program structure.

## Data formats

All multi-byte values big-endian unless said otherwise. "Checked" = every
file of that kind decodes and the result was looked at (renders in
`work/png/`, `cannon_fodder_assets export`).

### Sensible disk file system

- Sectors of 512 bytes, 11 per track, 160 tracks; image offset =
  (track × 11 + sector) × 512.
- Track 3 = directory. Header (32 bytes): "Sensi disk\0Requested \0…"; byte
  $1A differs per disk ($1A, $8B, $89; meaning open). Then 32-byte entries
  until the end of the track; an entry starting with 0 is free.
- Entry: +$00 name (16 bytes, zero-terminated, mixed case), +$10 10 bytes
  not used by the chain (first u32 often equals a size, sometimes stale —
  e.g. every `.spt`/`.map` pair shows the same value; leftovers of the
  mastering tool, hypothesis), +$1A first sector (track ^ 1, sector),
  +$1C size (u32).
- Sector: 510 data bytes, then the next sector (track ^ 1, sector). The
  boot block's chain walker confirms the bit flip (`bchg #0`). Checked: all
  304 entries; one damaged chain (see "Disks, crack, loader").

### RNC method 1 (files starting "RNC\1")

Header 18 bytes: "RNC", 1, u32 unpacked size, u32 packed size, u16 CRC of the
unpacked data, u16 CRC of the packed data, u8 leeway, u8 chunk count.
CRC-16 reflected, polynomial $A001, initial 0. Bit stream after the header:
little-endian 16-bit words, lowest bit first; 2 flag bits; per chunk three
Huffman tables (5-bit symbol count, 4-bit code lengths, canonical codes
stored bit-reversed; symbol n ≥ 2 = (1 << (n-1)) | n-1 more bits): literal
run lengths, distances (+1), match lengths (+2); a 16-bit run count; runs
of [literal bytes copied byte-wise from the stream, bit reader refilled
after them] + match. Checked: 44 packed files, both CRCs each
(`tools/rnc.py`, `games/cannon_fodder_amiga/import/rnc.cpp`).
Not yet located: the game's own unpacker in `FODDERC`/`fload`.

### RAW screens (`*.RAW`, disk 1)

2^planes colour words ($0RGB), then the bitplanes one after another, 40 bytes
per 320-pixel line. 51,464 bytes = 32 colours + 5 × 257 lines (title,
presentation, photos, end, insert disk); 41,152 = 16 + 4 × 257 (`1.RAW`-
`8.RAW`); 40,992 = 16 + 4 × 256 (`FONT.RAW`). Checked by rendering.
Interleaved-plane reading gives garbage, plane-sequential is right.

### IFF ILBM (`*.lbm`)

Standard (DPaint) ILBMs, ByteRun1. 320×256 pictures (`pstuff` 4 planes,
`rankfont` 5, `hills` 4, `startup` 4), sprite sheets 320×336/337 4 planes
(`*army.lbm` soldiers, `*copt.lbm` vehicles, explosions, "MISSION PHASE
COMPLETE"/"GAME OVER"/"TRY AGAIN" lettering), 320×300 3-plane backdrops
(`*play.lbm`: horizontal bands, probably for the helicopter scenes —
hypothesis), `morphbig.lbm` 5 planes. The sheets' CMAPs do not look like
the in-game colours (e.g. blue background on `desarmy.lbm`); which palette
the game uses for sprites is open.

### Colours

`*.pal`: 16 colour words ($0RGB). `*.pl8`: 8 colour words. `*base.pal`
colours the tile sets (checked by rendering all maps). `*heli.pal` and
`sky/mid/fgnd/fgn2.pl8`: use open (helicopter/briefing scenes, hypothesis).

### Tile sets (`*.blk`)

16×16 tiles, 4 bitplanes, 128 bytes per tile: plane 0's 16 rows (one word
each), then plane 1's, 2's, 3's. Base sets 30,720 bytes = 240 tiles, sub sets
20,352 bytes = 159 tiles. Checked: every map renders as a coherent landscape.

### Mission maps (`mapm<N>.map`, N = 1-72)

$60-byte header, then width × height cell words row by row:

| Offset | Contents |
| --- | --- |
| $00 | base tile set name ("junbase.blk"), 16 bytes |
| $10 | sub tile set name ("junsub0.blk"), 16 bytes |
| $20-$4F | not understood (looks like editor leftovers: bits of tile data) |
| $50 | "cfed" |
| $54, $56 | width, height in tiles |
| $58-$5F | unknown |

Cell bits 0-8: tile, 0-239 base set, 240-398 sub set. Map 31 uses tile 399,
one past the sub set (open: what the game draws there). Bits 13-15: flags
($2000, $4000, $6000, $8000, $A000 seen; meaning open). Sizes range from
19×15 to 120×15 / 76×78 tiles. Missions 1-36 on disk 2 (jungle, ice,
desert), 37-72 on disk 3 (moor, interior, desert). Map 72 shows "THE END"
spelled with tiles. Checked: all 72 sizes equal $60 + w×h×2.

### Sprite placements (`mapm<N>.spt`)

Records of 10 bytes (5 words), 8-30 per map. First word often $007C, then
0, then two values in map-pixel range (x, y?) and a small type number
(hypothesis). Not decoded yet.

### Tile attributes (`*.hit`, `*.bht`, `*.swp`)

Per tile set: `.hit` 2 bytes per tile (240 base / 160 sub), `.bht` 8 bytes
per tile, `.swp` 2 bytes per tile (mostly $FFFF). Hypotheses: terrain type
per tile (`.hit`), 8×8 sub-cell bitmask of blocking terrain (`.bht`), tile
swap for destroyed scenery (`.swp`). Open.

### Music (`*.SNG` + `*.INS`)

Both start "RJP1"; `.SNG` continues "SMOD", `.INS` holds samples. One pair
per landscape plus `JON` and `WARX` on disk 1. The player routine in
`FODDERC` is not located yet. Open.

### Other

`disk1`/`disk2`/`disk3`: one byte ($0C) each; the program names them
(disk check). `disk2.ra` (disk 1, 2,760 bytes unpacked, mostly zero at the
start), `dir` (disk 2): unknown.

## Program structure and rules

Addresses are in the $80000 layout (`work/F.bin`, equal to the running
program on the reference machine at its entry). Read from the code
(capstone, Ghidra `work/all.c`) unless marked otherwise.

### Start-up ($85D92)

CPU test (illegal instruction trap; on 68020+ `MOVEC D0,CACR` clears the
cache control), SR $2000, stack $100000, `$8CF94`, DMA/interrupts off, chip
setup (`$87016`, `$8702C`, copper list at $3134), level-3 interrupt
`$8A2E2` (vertical blank → `$864AA`; ports at `$8A2F8` handle the CIA-A
keyboard, last key in `$814E5`), then loads (`$8B3E8` with a chip buffer at
$3D34), `$8CAC6`, `$8CAF0`, `$8CF2A`.

### Top-level loop ($85E62 … $8628E)

- `$80626`: mission phase number, 0-based. After phase $47 (the 72nd map)
  the game jumps to `$9CAB2` (the end, hypothesis: `WON.RAW`).
- `$8000A` = $12345678 once the presentation has run: the first pass shows
  the intro list at `$86B52` (`$86B24` walks it: each entry points to a file
  record, see below) and `$A25C6`.
- `$8000E`: $3333 / $3498 state words choosing between two paths into a
  phase (`$86E22` first time, `$86DF0` + `$8A53C` otherwise; hypothesis:
  briefing vs. retry).
- Per phase: `$94B24`, `$982B8`, map loading (map header at `$F9D5C`:
  width `$F9DB0`, height `$F9DB2`, multiplied by 16 into `$81FDA/$81FDC` =
  map size in pixels), tile sets/palettes (`$9E19A`, `$89E7E`, `$89F12`,
  `$9E21C`), sprite sheets at $75F60 and $6BEC0 (`$9DE6A` twice), then
  dozens of initialisers, then the mission loop `$86292`.
- A 19×15 map (maps 1 and 6: 304×240 pixels, one screen) sets `$82144`
  ("map fits the screen"). Read in two places only:
  - `$867BE` (every frame): with the flag set, the scroll speed limit
    `$81E1C` and the scroll velocities `$814C4/$814C8/$814CC/$814D0` are
    forced to 0 — the view never scrolls.
  - `$9C1FE` (mission loop, when `$81F62 >= 0`; set to 1 at phase start and
    by `$9F940`): without the flag, the view position (`$814AE`, `$814B2`) is
    compared with the target (`$815EE`/`$815F0` = start position from
    `$FDB2C`/`$FDB30`, minus $98/$70, clamped to the map) by the distance
    estimate `$A1062` (|dx|,|dy| halved until < 32, table `$A128C`, result
    shifted back; 1000 when either is ≥ 320). Beyond 139 the game pans: it
    redraws in a loop (`$9C33A` twice per pass, scroll limit $80000) until
    `$81F66` is 0, with the playfield update off. With the flag set the pan is
    skipped. Checked on the machine: map 1 sets `$82144` = $FFFF, the camera
    stays at (0, 0) while the squad walks to the far corner; map 2 leaves it
    0 and the camera starts away from 0 after the pan (below).
- Result of `$86292`: Z flag → `$8725E`, else `$A8AF6` (hypothesis: mission
  won / lost paths, and the graves hill).

### Mission loop ($86292)

```
loop:
    wait for vertical blank ($8649A) while $81DEE != 0
    until vertical-blank count $81488 >= 3        ; game tick every 3rd frame
    $81488 = 0; tick counter $8061A += 1
    ... ~25 subsystems: $88D4C/$88380, $87102, $A3AD0, $9A6CE, $9DD64,
        $A1248, $9DC18, $A38D6, $9E9A2, $9E92A, $9A8FC, $9A730, $87774,
        $A3AE4, $984B6, $9837A
    toggles $80612 ^= 1, $80614 ^= 2, $80616 ^= 4  (alternating-tick phases)
    leave when $817D8 != 0
```

So game logic runs at 50/3 ≈ 16.7 updates per second (PAL). Checked
2026-10-27 in mission 1: 39 consecutive ticks, each exactly 3 frames apart
(frame counter `$80618` at `$862AE`); the same on map 2 while scrolling was
possible. (The code allows more than 3 frames when a tick runs long.)

### Vertical blank ($864AA, every frame)

`$81488 += 1`, `$80618 += 1`, `$8CAF0`; in a mission (`$80610` set) also
`$89446`, `$867A0`, `$867BE`, `$8682A`, `$869CE`, `$86956`, `$86624`,
`$865C0`, `$873FC`, `$9C67E`, `$8727C`, `$9C78A`, `$9C0D4` (per-frame work:
probably mouse, scrolling, copper/sprites — open); always `$A3D22`,
`$A3DBC`, `$9D850` (music, hypothesis), `$8A27A`.

### Objects

- Table at `$FDB2C`: 44 slots of 118 ($76) bytes, a 45th = $FFFF end mark;
  free slots start with $8000 (`$A3976` clears all). Fields seen so far:
  +$00 x, +$04 y (16.16), +$08 graphic (frame group), +$0A frame, +$18
  kind (the placement's type), +$1E height above ground (16.16, hypothesis),
  +$22 side (0 player, 1 enemy; set by the first update, by the spawner for
  kind $24), +$24 remove (freed by the next draw list), +$26/+$28 home
  (the placement), +$2C layer, +$32 squad (soldiers before / 8), +$3C
  facing (0-$E, 8 directions), +$4A start delay, +$52 lines hidden at the
  bottom, +$5C visible this tick.
- Spawner `$8A594` (after the map loads, `.spt` at $68810): per 10-byte
  placement (word 0 = $7C and word 1 = 0 in every file, not read), in order:
  graphic $7C, squad = soldiers so far / 8, x = placement x + 16, y, home,
  kind; kinds $6A/$48 count into `$81F60`; kind 0 (soldier) counts and gets
  delay 0; kinds 5 and $24 count into `$81F5E` (enemies; $24 also side 1);
  every non-soldier gets delay 20, 30, 40 … in order (a running +10 from 10,
  reset when the fresh slot's +$62 > 4, never the case). Checked: native
  `spawn_objects` equals the original's table right after the spawner on
  maps 1 and 2 in every field it writes (`harness/atspawn.py`,
  `cannon_fodder_assets spawn`); enemy counts 3 and 17.
- First update (per-kind handler table `$94608`, 4 bytes per kind, 111
  entries): kinds 13/14/15 set graphic $8F/$90/$91 (`$97296`…) then share
  `$972C8` (hit by explosions: random direction via `$9D850`, the random
  number routine, hypothesis); kind 0 soldiers `$96730`, kind 5 enemies
  `$9DADE`, kind 21 `$954F6` (created by the game in slot 42 at the start),
  kind 66 `$99808` (moves; graphic $D3). Soldier/enemy pictures come from
  `$96358`: per side a table (`$8DBB2` + side × 4) of 8 facings × states
  (+0 walk, +$30, +$40, +$50, +$60, +$70), indexed by the facing +$3C.

### Drawing objects

- Each tick `$A38D6` (after the tile drawer): `$AB974` restores the
  backgrounds saved in this buffer two ticks ago, `$AB828` builds the draw
  list, `$AB9B6` draws (saving backgrounds with `$9C852`, blitting with
  `$A378C`).
- Draw list `$AB828` (slots 0-42): removed objects (+$24) are freed;
  visible = x in [snapshot x − 30, snapshot x + 314] (moving left: [−36, …]),
  y in [max(snapshot y − 4, 0), snapshot y + 331], both windows shifted by the
  direction; layer < 0 first in slot order, then layer 0 by an exchange sort
  on y (`for j from last: for i from last, i ≠ j: swap if y[i] < y[j]`),
  then layer > 0 in slot order.
- Frame table `$8DCDA` (232 group pointers, unrelocated offsets in the file;
  groups of 16-byte entries): u32 sheet address, u32 mask pointer (0 in the
  file, filled at run time), u16 width in words, u16 height, u16 sheet plane
  size ($3480 army sheet at $57F60, $34A8 vehicle sheet "copt" at $3294A:
  fixed load addresses from the file records), s8 dx, s8 dy. Masks = OR of
  the four planes over the frame's rectangle (checked live).
- Blit `$A378C`: cookie-cut (BLTCON0 shift | $FCA: A mask, B graphics, C/D
  screen), 4 planes (5 with `$81E08`), last-word mask 0 so a w-word frame
  shows (w − 1) × 16 pixels. Position: left = x + dx, top = y + dy −
  (height − hidden) − height above ground; rows from the frame's top.
- Sheets in memory equal the decoded `*army.lbm`/`*copt.lbm` (checked); the
  play view's colours 0-15 are the landscape's `base.pal`.
- Checked: native `draw_list` + `draw_objects` over the map view equal the
  buffer the original just drew (ring index = (y × 320 + x) mod 81,920) on
  all 60,928 play-area pixels in five grabs on map 2 (start, walking,
  firing; live camera ≠ snapshot in one) (`harness/objgrab.py`,
  `cannon_fodder_assets objects`). Not yet exercised: hidden lines, layers
  ≠ 0, overlapping order ties, shots.

### The panel

- Three attached sprite pairs (sprites 2-7; data at $398/$764, $B30/$EFC,
  $12C8/$1694, 4 bytes per line), HSTART $80/$90/$A0 (one pixel left of the
  play window), 224 lines from the window's top; colours 16-31 =
  `pstuff.lbm`'s own colour map. Sprites 0/1 = the pointer.
- The pointer (native `Module::draw_pointer`): sprites 0/1 show the first 16
  columns of a `pstuff` piece (`$895BC`), the piece number in `$81E16` (a
  request the frame's drawing takes, `$9C6A6`), its offset in
  `$81E12`/`$81E14`; column = pointer x + offset + 15, line = pointer y +
  offset − 4. Per tick (`$9EA10`): over the panel (pointer x < $20,
  `$9EC80`) the arrow $23; with the right button and a leader the
  cross-hair $24 at (−8, −8) (`$9EAD2`); a walk order with the left button
  back to $23 (`$9EB22`). Vehicles' pieces $25-$27 (`$9ED66`…) and $28/$CC
  (front end) not rebuilt. The host's own cursor still shows over the
  picture (open).
- Pieces of `pstuff.lbm` (loaded to $C73AA) from the table `$89658` (x, y,
  width, height − 1): opaque on 16-pixel columns (`$895BC` → `$894F0` →
  `$9C65A`: whole words, planes 0/1 to the even sprite, 2/3 to the odd one);
  masked at any x (`$8A0E0` → `$8A1D0`: each source word shifted by x & 15,
  OR'd where the source colour is not 0).
- Mission start (`$88566`): fill all lines with colour 15 (`$8936C`), then
  per squad with soldiers (`$88C6A`, block line 0, later squads 8 lines
  below the previous block's last bar): bar piece from `$89020`[squad]
  (opaque, 0/1/2), grenade box $C4 ($C3 lit) at (0, +14), rocket box $C6
  ($C5) at (32, +14), squad icon (`$891B0`/`$8919A` selected)[icon] at
  (12, +14) (icon `$88E30`: 0 one soldier, 1 more; vehicles via `$88E0A`),
  per soldier an opaque bar $AB ($A9 selected) at line +34 + 12k, then per
  roster entry (`$80664`, 12 bytes: recruit, rank, object …) of the squad:
  chevrons piece rank + 9 at x 0 and 35, line +35 + 12k, and the name
  (`$8A050`: recruit record `$917E0` + 9 × recruit, up to `$85B0E` + 1
  letters, centred at (48 − 4 × length) / 2, 4 px per letter, piece = font
  base (`$8A0DC`: $29/$4D for the selected squad by roster byte 9 bit 0, $71
  otherwise) + letter − 'A', line +37 + 12k). The globe: opaque piece $D0
  at (0, 213) (`$88472`, not in the last mission). Before the fill,
  `$870DE` lays piece $AC on lines 73 … 229 (overwritten).
- Checked: native `compose_panel` equals the original's sprite memory at
  the start of mission 1 on all 48 × 224 pixels (`harness/sprgrab.py`,
  sprite dump, `cannon_fodder_assets panel`). Open: ammunition counts
  (`$892FC` → `$A99B4`/`$A9A06`), lit boxes' exact condition (`$821CC`
  weapon state), vehicles' icons, the mini-map overlay (`$9E32C`), the
  clickable areas list (`$82480`), `$9C60A` (200-line panel from pstuff
  line 78, another mode).

### Game logic (steps 4-6, research in progress)

- Per tick `$953B4` (from `$A38D6`): every used slot in order, handler from
  the kind table `$94608` (`jsr`). Ghidra: `ghidra.sh seed F 0x94608 111`
  makes the handlers functions (100 new); `tools/callgraph.py` sizes what
  hangs below a routine: soldier `$96730` 55 routines / 11.7 KB, enemy
  `$9DADE` 50 / 10.5 KB, 47 shared.
- Random numbers `$9D850`: pointer `$9D84C` steps 4 bytes through program
  memory `$85D92`…`$ACA14` (wraps), state `$9D848` = rotate((long at the
  pointer) + (long 4000 bytes on) + state + mouse x delta `$8CAC2` − mouse y
  delta `$8CAC4`) (asl.l #2, swap). The window holds live data (3,783 of
  158,854 bytes differed from the program in mission 1: mask pointers,
  variables), so the original's sequence cannot be reproduced. Decision
  (user, 2026-10-27): the rebuild uses a standard C++ random generator;
  the original's draws are neither replicated nor captured. Rules keep
  their use of the value (masks, ranges); random-dependent behaviour is
  checked by its rules, not by identical sequences.
- Walking (soldiers, from `$96730` and a 70-tick recording `work/walk1.trace`,
  click at tick 10):
  - A click on the play area appends a waypoint camera + pointer − (22, 3)
    to the squad's route (`$85C16`[squad] → x/y word pairs, −1 ended); each
    soldier's +$40 indexes its next waypoint, +$26/+$28 hold the current one;
    `$ABE34` takes the next on arrival (+$42 set), or ends the walk.
  - Speed +$36 (`$94BD6`): 16; halved when the squad mode `$85BF8`[squad] is
    0, 24 when it is not 1 (2 in mission 1 → 3 px per tick); 6 when +$4F
    (in water, hypothesis) or while hidden lines +$52 are non-zero (they move
    1 up / 2 down towards +$50); enemies 12 + their +$62, at most 26.
  - Heading +$10 (`$A0AAC`): direction to the waypoint by the angle table
    $A1890 (|dx|,|dy| reduced to 5 bits by a 63/127/255… threshold ladder),
    in sine units (0-$1FE).
  - Facing +$3C (`$A0C12`): +$3E counts down; when it underflows (every
    second tick) the facing turns by the table `$955C0`[(facing − ((heading
    >> 5) − 1 ^ $F) & $E) & $F] × 2 into `$81CEE` (applied by the
    animation step `$96358`).
  - Step (`$94CEA`): within 3 px (speed > 8) snap onto the waypoint
    (`$94D40`); terrain speed changes by the kind under the soldier +$60
    (`$94C54`); move x/y by sine/4 × speed (`$A0C9A`, 16.16, stop at 0);
    squad merging (`$ABF46`); map limits (`$94F72`: y ≥ 14 and < map height,
    x ≥ 14 and x + 26 < map width, else back to the old position); terrain
    (`$950C2`); follower waits behind the soldier ahead when within 8/2 px
    (`$94D9C`).
  - Terrain (`$A22CE`, point + offset): tile attribute word `.hit` at
    `$F9A3C`[tile] (2 bytes per tile; bit 15 = two terrains: `.bht` at
    `$F8DBC` + tile × 8 + ((y >> 1) & 7), bit 7 − ((x >> 1) & 7) chooses the
    high nibble) → kind = `$A2340`[attr & 15]. `$950C2` acts on the kind
    (4/5 speed classes, 6 water (+$4F), 1/2 sinking (+$1E), 8 random fall
    1/64, 9/10 blocked or squeeze, 11 enter (+$5B)); blocked: back to the
    old x or y, else stop with the waypoint = here.
  - Animation `$96358` (8 facings per kind table `$8DBB2`[side], states at
    +0/+$30/+$40/+$50/+$60/+$70, frames from `$81578`/`$8157C`/`$81580`).
  - The squad marker (kind 21, slot 42, `$954F6`): graphic $95 above the
    leader, $7C (not drawn) for a few ticks after an order.
- Native walking (`game/walking`: World, Squad routes and members,
  AnimationClock `$94B54`, Terrain `$A22CE`, SoldierTables `$955C0`/
  `$8DBB2`, `update_soldier` = the selected squad's walking path of
  `$96730` with `$94BD6`, `$A0AAC` (`GeometryTables::heading_toward`),
  `$A0C12`, `$94CEA` → `$94D40`/`$94C54`/`$A0C9A`/`$94F72`/`$950C2`/
  `$94D9C`, `$ABE34`, `$96BC6` (idle soldiers face the pointer: camera +
  pointer − (24, 0)), `$96358`). Checked with `cannon_fodder_assets
  walkreplay` (each tick from the original's state before the object
  update, every soldier field compared after it) on three recordings
  (`harness/ticktrace2.py`): walk2 (map 1, open ground, 80 ticks), walk3
  (map 1, into the jungle block: blocking, sliding, stops, 90 ticks), walk4
  (map 2, river: wading, slow terrain with hidden lines, sinking heights,
  90 ticks): all fields equal, except in walk4 from tick 76, where an enemy
  shot hits a soldier during the same pass (state 1; shooting is next). A
  deliberate error (speed 25) is caught (434 fields).
- Shooting (read; `work/fire1.trace`: right button on mission 1):
  - Fire flag `$81DEC` → `$968B2`: weapon flags `$81592` (grenade)
    `$81594` (bullet) `$81596` (rocket) chosen by the squad's weapon state
    `$821CC` (3/1) for the leader when both buttons (`$8147E`), else bullets
    and rockets cleared … then `$96230`, then the walking branch.
  - `$96230`: cool-down +$4A (counts down; −1 = ready); not while wading;
    kind $24 enemies get all weapons (rockets only when `$81FD8` ≤ 24);
    kind 5 enemies after phase 4 throw a grenade 1 in 32 (random & $1F);
    bullet `$9766C` → +$4A −1 on success / 0 on failure, +$45 = 15 unless
    mode 2; grenade `$97D88` (+$45 = 12); rocket `$9B4FA`.
  - `$9766C` (bullet): limited by `$81E02`, `$81DCE`, a target (+$2E) and
    a free bullet slot (`$98104`, pool slots 36-41 seen), at most 12 alive
    (`$81DF2`); shooter: +$54 = 2 (firing), +$5A, +$57 = 8. Bullet: copy of
    the shooter's position, kind 6, graphic $9D (muzzle) frame = facing / 2,
    lifetime +$12 = `$82148` + `$82150` (soldiers; owner +$5E = roster
    offset, +$5D) or min(8 + (+$62 >> 3), 16) (enemies), height +$1E =
    shooter's + 6, side and squad copied, speed +$4A = `$82146` + `$82152`
    and +$36 = (random & $F) × 8 (soldiers) or 60 + (+$62) and 24
    (enemies), target +$2E/+$30 = the shooter's target + (7, 0) (enemies) or
    (0, 15) (soldiers), owner pointer +$1A, muzzle ticks +$2A = 2, offset
    +$14/+$16 = (0, 2) (−1 for facing 4), +$34 = −1, spread +$50 = ±(random
    & mask): mask 7, or `$8214C` for the leader every 4th shot (no spread
    on the other three), or by rank (`$9E97E`) for other soldiers; sound
    `$A3A30` (effect $10 or $11 at random).
  - Flight `$97878` (kind 6): +$64 age; muzzle phase follows the owner
    (+$14/+$16) for +$2A ticks, then graphic $7F, frame 0 (3 for enemies)
    at owner + (7, 5); then per tick while lifetime +$12 lasts: +$3A += 1;
    heading toward the target, + spread once (+$59); +$34 first heading;
    move by `$A0C9A` with speed +$36 (then +$36 = +$4A); within speed/8 of
    the target: snap, +$44 set (flies on with +$2C = −1); from age 3 a
    blocking tile at (x, y − 9) ends it; hit tests: player bullets
    `$9D69A` box (x − 6 … x + 10, y − 10 … y + 6) against kinds flagged in
    `$801C2` (sizes `$8029E`/`$8037A`/`$80456`), an enemy (kind 5) hit gets
    the owner (+$5E/+$5D); enemy bullets `$A098A` box (x … x + 6, y − 9 …
    y − 4) against the roster's soldiers (not in vehicles, height < 11):
    always while the bullet is young (+$3A ≤ 4), otherwise 1 in 8 when the
    soldier is sunk ≥ 5, wading or his squad is in mode 0, else always; hit
    soldier: state 1, +$64 = $FF, heading (+$10/+$12) = the bullet's.
  - End: impact graphic $96 frames 0-3 (hit) / 3-7 (spent), then
    `$96D2C`: kind 4, graphic $7C, removed at the next draw list; `$81DF2`
    −1.
- Native shooting (2026-10-27; `game/combat.cpp`, `game/world.hpp`,
  `game/tables.cpp`): bullet flight `$97878` with the hit tests `$9D69A`
  (objects, by `$801C2`… tables) and `$A098A` (the recruits in roster
  order `$80664`; past 4 ticks of flight a soldier in cover is hit 1 in 8
  with the rebuild's own generator); corrections to the reading above:
  an enemy bullet's hit counts as "stopped" (`$81F4A` = 1 → spent impact
  frames 3-7), the `+$59` "from behind" comparisons in `$9D69A` can never
  hold (always cleared), `+$2C` is the drawing layer (−1 past the target,
  1 after a hit), `$9766C` success sets `+$4A` = 0 and failure −1. Fire
  control: `$9F454` (every frame while the right button is held, from
  the vertical blank `$9EABA`): `$81DEC` set (never cleared except
  `$9E654`), first press with `$81DD6` < 0 → `$81DD6` = $21 (ticks,
  `$86338`) and the leader's `+$4A` = 0; aim = pointer + camera − (16, 8)
  (x 0 → 1) for every member; `$9E65C`: on the first press frame or when
  `$82140` runs below 0: `$82140` = interval, the next member in the
  firing order `$9E748` (by squad size, member indexes, −1 ends; 7 and 8
  run on into the next list) gets `+$4A` = −1 unless pausing; a lone
  soldier above rank 20 gets interval − 1, lifetime −3, speed +10
  (`$82150`/`$82152`). Weapon `$9E92A` (each tick): row min(rank + 8, 15)
  of `$9E826` (speed, lifetime, interval, spread mask, 0) for the first
  recruit in `$82A2C` (list built by `$9DC18`). Slot allocation `$98104`:
  the highest free slot 42…0 (a failure sets `$826EA`, cleared when the
  draw list frees a slot). Firing pose `+$54` = 2 for `+$57` = 8 ticks:
  walking picture, frame still (mode 2: fast clock); mode 2 squads walk on
  while firing (`$969B8`, speed `$9C4F8` by facing steps from the
  heading), others aim (`$96AAA`) or face the pointer. `$817B8`/`$81DE4`
  carry over between soldiers (kept in `World`). Throwing (`+$54` = 1,
  frames 0-2 of picture +$50, 2 ticks each) and state 3 (+$70, 7 ticks)
  are rebuilt in the animation but not yet exercised.
  Checked with `cannon_fodder_assets replay` (was walkreplay; soldiers and
  bullets in slot order, slots not rebuilt take the original's result,
  new bullets compared except their random first speed and spread, which
  are checked by range; between ticks the fire control's frames and the
  hold timer against the next tick): walk2, walk3, walk4 (enemy bullet
  hitting slot 2 at tick 76 exact), fire1 (old recorder, skips ticks),
  fire3 (`harness/ticktrace3.py`, right button held for 16 ticks twice:
  16 shots, rhythm one shot per 2 ticks alternating): all 0 differing
  fields. Mutations caught (impact frame 2: 12 fields; pose 9 ticks and
  order reset 1: 24 fields). `ticktrace3.py` holds buttons across ticks
  without skipping any (cf.press alone re-pressed every frame, since the
  button routine clears its latch while the real button is up).
  Open: `$81DCE`/`$81E02` (stop shots, meaning unknown), shot sounds
  (`$A3A30` $10/$11), grenades `$97D88`, rockets `$9B4FA`, vehicles
  (`$82798`).
- Soldier death (read, `$95BB2` state 1 with `+$64` set): picture
  +$20 by facing (+$10 when "from behind"), vertical speed `+$1A` (16.16)
  += 2…9 when below 11, `+$12` = 2, sound, speed +15 when below 30, `+$3E`
  = heading, `+$3A` = −1 or the heading (spin), blood `$94AB2` (kind $17,
  graphic $96, at +3, −8, layer 1), waypoint x −1; then each tick: spin,
  move, terrain `$950C2` (height < 12 keeps the new position), height +=
  vertical speed (bounce: −speed/4, minus $18000 per tick), speed −2, frame
  1 after 2 ticks (`+$12` = 6 after), after 20 ticks on the ground: state 6
  (or 1 in 128: state $A, `+$3A` = 500); state 6: 15 ticks, picture $7C from
  tick 7, then `$96CAA` (casualty `$80D1C`, rank into `$80A0E`, recruit
  into `$80CE8`, roster entry cleared, slot freed).
- Native death (2026-10-27, `game/walking.cpp` states, `game/combat.cpp`
  blood/shadow): start `$95C42`, fall `$95DA0`, writhing `$95D28` (state
  $A), lying `$95FCC`/`$96060` (states 6/2: blinks from tick 7: picture
  $7C every other tick), end `$96CAA` (casualty, fallen list, roster
  place cleared, slot freed); blood kind $17 (`$94AFE`: height runs down,
  frames 0-3 two ticks each), shadow kind $16 (`$97AFC` only for bodies in
  the last draw list, `+$5C` set by `$AB828`; follows the body at + height
  / 4 + (5, −3), graphic $97, frame 0 above height 8; ends with the body's
  picture $7C or hidden lines, clearing the body's `+$58`). The throw
  (2-9 up) and spin (half the time) use the rebuild's generator.
  Checked with `replay` on walk4 (start by range, flight exact) and
  death4 (map 2, hit injected on soldier 0 at tick 1526 by poking `+$38` =
  1, `+$64` = $FF, `+$10`: flight, bounce, landing, blood each tick,
  lying, blinking, freed at 1561: 0 differing fields; mutations caught,
  21 fields). In death4 `$81DCA` was −1 (all recruits down?) and `+$26`
  is rewritten outside the object update each tick (the body bleeds every
  tick): mission-end logic, open. Open: the hall of fame `$96DF6`, the
  cries `$A399C`, `$81DCA` bleed marker `+$40` = $9876, states 3 (falling
  into a hole, `$960E8`), $32-$34/$5A/$5B, squeeze, entering.
- Enemy soldiers (read; kind 5 handler `$9DADE`): states and death via
  the shared `$95BB2` (end `$96D48` for side 1, open); `$81FCE` = 0; while
  `+$44` counts down they walk on; otherwise `$9D894` (target), `$96230`
  (fire), `$817B8` = frame; no waypoint or no heading → `$8213E` = 0,
  aim `$96B98`; else save, turn `$A0C12`, speed `$94BD6` (kind 5:
  min(12 + `+$62`, 26), then the hidden-lines rule), pause `+$45`
  (returns while it runs), step `$94CEA`, `$8213E` = −1; then side = 1,
  animate `$96358` (side 1 with a building link `+$70` at its waypoint:
  graphic $DC; with `$81FCE` set: picture +$60 by facing, frame 0,
  unless wading), frame 1 when `$8213E` and `$81FCE` are both 0.
  `$9D894`: bullets only; `$81CEE` = 0; `$81FCE` = −1; `+$5E` < 0 counts
  up to 0 and follows the leader (`$81554`); else `+$5E` indexes the
  recruits' lists from `$85B26` as one array (30 entries, −1 holes): a
  live soldier (kind 0, state 0 or ≥ $32, height < 13) within 200
  (`$A1062`) is the target, 1 in 64 ignored; within 40 or in sight
  (`$A1260` = 0) → `$81FCE` = 0, `+$70` cleared; blocked sight: only
  within 64; target within 135 and register a4 (left over from an
  earlier routine!) at its waypoint → aim at the target (x, y − 14) and
  reload the fire delay; then waypoint = target + (sine, cosine)(slot
  offset & $1FE) >> 10 (±31, a per-enemy offset), aim = target. No
  target: with a building link (kind $48) waypoint = building + (12, 0),
  `+$5E` steps (wraps at 30); else 1 in 16 the aim moves by random
  bytes, waypoint = own position, `+$4A` = 0, `+$5E` steps. Fire delay
  reload `$9DAA6` (when 0): (random & ($1F >> (skill >> 2))) + max(50 −
  4 × skill, 0), at least 5. `$96230` side 1: `+$4A` 0 no shot, −1
  shot, n counts down to a shot (then −1); kind 5 in phase > 4 throws a
  grenade 1 in 32. Enemy bullets: lifetime min(8 + skill/8, 16), speed
  60 + skill, first step 24, aim + (7, 0). Enemy terrain `$950C2`: holes
  (10) and squeezes (9) block kind 5 (side 2: water too); blocked enemies
  (`$95268` side ≠ 0): x back; heading $80/$180 or 0/$100 by which way is
  open, + ((frame counter >> 2) & 63) − 32, y back, `+$44` = (frame
  counter & 31) + 12. `$94D9C` (wait behind) and `$94E38` are side 0
  only. `$81F64` (squad mode) is left over from the last soldier update.
- Native enemies (2026-10-27, `update_enemy` in `game/walking.cpp`, the
  shared routines' side branches, `Terrain::sight_blocked` = `$A1260`
  (tile line `$A0EC0`, walk `$A0F22`: two steps per look, the first two
  places skipped, mixed tiles (bit 15) never block; flagged cells are
  taken as their tile, open), enemy death end `$96D48` (kills `$80D1E`,
  `$81F5E` − 1, credit to the shooter's roster entry `+$A` (max 999) or
  the selected squad's first member)). `$81F64`, `$81FCE`, `$8213E` kept
  in `World` (they carry over between updates). Checked with `replay`
  on all seven recordings (enemies native in all of them): 0 differing
  fields; enemy random choices (fire delay reloads, also after a look-
  around, which clears the delay; the 1-in-16 look-around; the 1-in-64
  lapse) are checked by range (the original's reloads lie in the rule's
  range) and the affected update skipped (5-10% of enemy updates in
  walk4/death4, fewer elsewhere). enemy1 (map 2, hit injected on enemy
  slot 3 at tick 16 with `+$5D` = $FF, `+$5E` = 0): fall, lying, kill
  credited to recruit 0 (`$8066E` 0 → 1), `$81F5E` 17 → 16, exact.
  Mutations caught (enemy speed 13 + skill and range 190: 91 fields).
  Hypothesis: the register a4 left over before `$9DA04` taken as the
  enemy itself; no recorded case distinguishes it (the chest aim is
  overwritten by the approach in the same tick except behind cover).
  Recordings now: walk2-4, fire1, fire3, death4, enemy1 (fire2 had the
  re-press artefact, removed).
- Grenades, rockets and explosions (native 2026-10-27, `game/explosives`):
  throw `$97D88` (no `$81DCE`; at most 2 per side in the air `$81DF0`; aim
  set; enemies only within 130; a free slot pair `$9814E` (lowest i with
  i, i+1 free; a negative x counts as free everywhere); squad grenades
  `$81F4C` − 1; thrower pose `+$54` = 1, `+$55` = 1, `+$5A`; grenade kind 2
  (graphic $7D) + shadow kind 3 ($7E, layer −1, `$980F0` copies the
  slot before's whole x/y and hidden lines, so a shadow left behind by a
  freed slot sits at x = $8000 and counts as free); target aim + (0, 6);
  fuse `+$12` = min(max(d, 20) / 5, 100) (+28 for enemies), up speed
  min(fuse / 2, 14), speed 50, height 7, at thrower + (3, 1), 4 ticks in
  the hand `+$56` (`+$46` owner)); flight `$97F62` (speed 0 within 1,
  halved within 4; −1 a tick; `$95028` probe (2, −3): holes set the fuse
  to 1 when low, water/grass (4-6) hide it (6 lines) on the ground, walls
  below height 8 turn it ($100 − h or $200 − h) and put it back; falling
  at most 8, gravity 1.5, bounce at half; frame = height / 16 (≤ 3), the
  shadow's too); goes off at fuse 0 or any state: hidden < 8 → kind $C,
  else sunk (freed). Rocket `$9B4FA` (`$81DF4`, `$81F52`, pose 3, target
  aim + (0, 16), speed 100, height 10, 6 ticks on the shoulder, graphic
  $A3 with 16 directions, explodes within 7 or at a wall (2, −9); homing
  kinds $2D/$61 for `+$75` bit 0 not rebuilt). Leader's both-button fire
  (`$968B2`): squad weapon `$821CC` 1 grenade, 3 rocket, `+$4A` = 1; after
  a grenade pause 12, after a rocket pause += 5 (if < 10) and wait = pause.
  Explosion kind $C `$973A6`: first tick `$974E6` (on screen: blast points
  `$AAEE0` into the ring `$82498` (128, `$826A8` write, `$826AC` read; 25
  cumulative offsets `$AAF48`), a splash `$9897C` in water (kinds $56/$57)
  or two debris `$9AA26` (kind $2F + shadow, speed 12, opposite random
  directions, up 10.5) when fewer than 20 objects are drawn (`$82736`),
  `$817C0` = 9 (and shake variables, open)); then x − 6, y + 4, graphic
  $8E, side/squad −1; each tick the box (x + 8 … x + 36, y − 32 … y − 6,
  shrunk by `$974AE` pairs per frame) hurts the recruits (`$A098A`
  without the first-hit stop: all in the box, state 1, heading = the
  explosion's) and the objects (`$9D69A`: every hittable kind, state 5,
  heading = the explosion's) on alternate ticks (`+$62` toggled); every 2
  ticks a frame and a drift from the same table; 7 frames, or burst
  graphic $C0 (4 frames) once in the air. Tile explosion $27 `$9B152`
  (pairs read differently, objects box (x … x + 30, y − 30 … y), 3
  frames). Map damage `$AAFCE` (tile drawer, once a tick): the next point
  whose tile has a `.swp` replacement (`$F8A9A`, base then sub) and is not
  in the landscape's list `$A4B8C` (unless big, negative y from kind $59)
  changes the cell; off screen at once, on screen with an explosion $27
  `$9B25A` (tile grid, + 16) and one tick of drawing (`$82494`).
  Debris `$9AAB2` (gravity 2.125, bounce half; smoke kind $30 `$9AB56`
  behind it; frames to 3), splashes `$98A1A`/`$98A30`.
- Scenery (native, `game/scenery`): hut kind $14 `$96ED6` (door shut while
  `+$43` counts down, enemies out at 20 and 10, then open for
  max(20 − skill max, 0) × 8 + 0-15 ticks, then one more and shut (6-21);
  door stays open at 10 enemies; hit (state 5/7) → thrown (heading $1C0 ±
  0-30, speed 120, 5-8 ticks) → its slot becomes explosion $27 at x − 8,
  y + 8 (no tile rounding)); enemy spawn `$949F0` (fails at 10 enemies or
  after the win; slots 29…0 `$9819A`; at hut + (−6, 4), side 1, follows the
  leader for 120-375 ticks (`+$5E` negative), walks out heading $1C0 ± 0-30
  for 8-23 ticks, skill `$9E150` (bounces between `$8062A` and `$8062C`,
  whose max rises every 16 spawns to 30)); crates $25/$26 `$9B3B0` (picked
  up by the leader within 6 of (leader + 2): +4 grenades/rockets; shot:
  explosion); bushes $D-$11 `$97296` (graphics $8F-$93; blast → thrown
  with a shadow $23 `$953E2`, explode on landing or in 5/6/9/10 terrain,
  box + 8); animal $1B `$9720C` (stays in terrain 4-6).
- Mission (native, `game/mission`): objectives per phase `$A4D8E` (codes
  1-8: KILL ALL ENEMY, DESTROY ENEMY BUILDINGS, RESCUE HOSTAGES, PROTECT
  ALL CIVILIANS, KIDNAP ENEMY LEADER, DESTROY FACTORY, DESTROY COMPUTER,
  GET CIVILIAN HOME; names `$A4CDA`) into `$82A08…`; skill range `$A4F70`
  per phase; lost `$81DCA` when `$9DC18` finds no recruit alive; won
  `$81DCC` by `$9DD64` (buildings: kinds $6C-$6E/$58/$64 unless state 7,
  $14/$19 unless 5/7; enemies: list `$9DE20`; objective 1 all enemies,
  2 all buildings, 3/5 `$81F60` = 0, 8 `$82A1C`).
  Checked: grenade2 (map 2: two grenades, water), rocket1 (rockets,
  explosions, debris, smoke, blood), hut1 (map 3, 220 ticks, enemy count
  poked to 5: an enemy sent out, skill and counters), hut2 (hut hit
  injected: thrown, explosion $27); the squad lists, lost/won and buildings
  left between ticks on all recordings: 0 differing fields, apart from
  the both-button first frame (the fire rhythm's first pick is one frame
  later in some presses; injection phase, open). Random choices (debris
  directions, splash, hut door time and throw) checked by range.
- Birds $42/$43 `$99808`/`$998D8` (native: flap frames 1-4, 1.5 a tick,
  2 on the down-stroke; off screen they wait 63 ticks and come in again
  at random by the snapshot camera; cries one tick in 128 by landscape)
  and the leader's rank marker $15 `$954F6` (18 above the leader, frame =
  rank; the bobbing step is overwritten; `$9F9C6` new leader not rebuilt).
- Sound (read, not rebuilt): effects go through the RJP1 player: `$A3A30`
  (d1 effect, d2 priority) takes channel 2 or 3 in turn (`$81DF6`),
  replaces a lower priority or a channel idle for 12 ticks (`$81DFC`),
  volume $40 or by distance from the camera centre (camera + ($98, $70):
  40 − distance / 16, at least 4) unless the object is drawn, then
  `$A3DC4` starts it; `$A3B60` the leader's own sounds; the player code
  is `$A3AD0`…`$A4B5E` (`work/rjp1_a.asm`, `rjp1_b.asm`), data the
  landscape's `.sng`/`.ins` (and `jon`, `warx` on disk 1).
- Game module (2026-10-27, `game/module`, `game/dispatch`): the launcher
  plays the phases from map 1 on. Per frame (50 Hz): pointer (`$8CABA`
  −16…301 / `$8CABC` 4…227, taken as picture column − 16 / line + 4,
  hypothesis; the target check below agrees within 4), right button held →
  `hold_fire`, left press on the map → walk order, both → the leader's
  grenade/rocket, camera (look-ahead `$8727C` from the leader towards the
  pointer, `step_camera`), the RJP1 frame. Every third frame the tick in the
  mission loop's order: hold timer and weapon stats, `$9DD64` objectives,
  `$A1248` click windows, `$9DC18` squad lists, then `$A38D6`: walk clock,
  objects `$953B4`, draw list `$AB828`, snapshot every second tick, map
  damage `$AAFCE`; the sound board and its busy counts. Walk order
  `$9EB36`: target = camera + pointer − (22, 3) (x ≥ 0, y ≥ 3), the
  selected squad's members get wait 0 and their x/y fractions cleared,
  then `$A1136` (route: the members' places, the last click's target
  `$826EE` if any, the target; each member's waypoint the place of the one
  ahead, route index after his slot; a click within 8 ticks (`$81DDE`,
  run down by `$A1248`) appends a waypoint at `$81DD8` instead; the rank
  marker hides while the window runs); merges into the squad cleared.
  New leader `$9F9C6`: the first member on his feet. Ammunition `$9E0B4`:
  grenades 2 × soldiers from phase 5, rockets 1 × soldiers from phase 10
  (`$80D4C` = soldiers in the phase). Checked: order2 (map 3, three clicks:
  fresh, appended, fresh with the last target): routes, route ends, the
  members' waypoints and route indexes exact (`replay` between ticks); the
  module headless (`cannon_fodder_assets play`): 3 × 30,000 frames of
  random clicks and fire without an unhandled update, map 1 won, map 2
  played, lost and restarted; started from the launcher's own path
  (`joyport --preview … --select 5 --pause-preview 0`). The recorder
  `ticktrace3.py` set the press latches `$8147A`/`$8147C` to −1 on every
  frame of a hold (each frame a new press: three walk orders a tick, and
  likely the both-button first-frame difference in grenade2/rocket1); it
  now writes 1 after the first frame.
  Not yet in the module: the game's own pointer sprite (the host's cursor
  shows), panel clicks (squad and weapon choice, so rockets cannot be
  chosen), squads other than the first, the music control (landscape song
  2 at channel volume 64 throughout; the original's fades and mission-end
  songs not run), recruits (a lost phase restarts with recruits 0, 1, …
  again; the original's replacements, promotions and graves), the front
  end, briefing and debriefing, quick save.
- Maps 1-10 (2026-10-27): placed kinds beyond the earlier ones: $18
  `$97018` hidden spawn point (while buildings are left; enemies come out
  at x + 6, y − 5 with 12 lines hidden; count like the hut's, timer 10-265
  at random), $19 `$970AC` second hut (graphic $9B, thrown at 110 towards
  $1C0 only in the ice, its soldiers 4 higher, a failed sending ends its
  count), $24 `$9B48E` kneeling bazooka soldier (enemy scan, all weapons
  off, rockets at targets beyond 24 (`$81FD8`), pose state 3 frame 2), $37
  `$9B35C` trap (the leader within 4 of (trap + (−2, −2)) is held at
  trap + (−3, 1), state 9, height 7; at 5 it only shuts), $3C `$9A5F2`
  house with smoke in the next slot (a placed kind 4), $3D `$9A2A6`
  civilian (side 2, speed 6, wanders to open ground (`$99FE4`, table
  `$A22A8`, 4 tiles in), wobbles ±0-14 a tick, water puts it back, grass
  hides lines; killed: graphic $D6 frames 0-8, `$82860` hostile, a
  "protect"/"home" mission lost); no vehicles are placed on maps 1-10.
  Soldier states now rebuilt: hole (state 3), cliff (squeeze, terrain 9:
  falls faster each tick, waypoint and route sliding while `$A22C8` sees a
  cliff or hole; the original passes x as the row and y as the column
  there, a bug kept), deep water (entering 1, `$95F48`: drowns over 9 × 10
  ticks), trips $32/$34 → slide $33 (speed $24/$2E, −5 a tick, sit up, new
  leader `$9F9C6`), $5A without a vehicle. `$3E` holds both the turn
  timer and a falling body's heading (a soldier landing from a cliff walks
  on with the heading as his timer). Once the mission is lost a falling
  body bleeds every tick (`$40` = $9876, a cry every 8). The panel's weapon
  icons (`$886AE`/`$8879A`, regions `$821E0`: grenade x 0-12, rocket x
  36-48, y 14-34 for the first squad) set `$821CC`. Checked: map7 (250
  ticks: house, smoke, $19, $18, civilians, by range for the civilians'
  wobble and the spawn timer) 0 fields; map10 (300 ticks, four walk
  orders, bazooka rockets, explosions, a cliff fall, a map tile change) 17
  fields, all from the recording's poked boot: the roster's object links
  are all 0 there, so the original tests only slot 0 for hits (replaying
  with every link 0 leaves 0 object fields). Headless: maps 1-10 with
  25,000 frames of random play each, no unhandled update; map 6 won.
- Not yet rebuilt in walking: other squads (enemy-scan AI with random
  draws), the firing branch (`$968B2`, `+$54` states), vehicles (`+$66`,
  `+$6E`), squads joining at a route's end (`$ABF46`, `$ABE34` tail),
  falls/trips (states 3, $32), the kind-21 squad marker.

### Input

Mouse, every frame (`$8CAF0`): pointer x `$8CABA` (−16…$12D), y `$8CABC`
(4…$E3), from JOY0DAT deltas; buttons `$8CBA0`: left (`$BFE001` bit 6) →
`$81476` held, `$81484`/`$8147A` first frame; right (POTGOR bit 10) →
`$81478` held, `$81486`/`$8147C` first frame; both → `$8147E`, `$81FD0`.
Some wait loops call `$8CBA0` themselves (intro photos `$9CFD2`: a press
skips). The disk check `$87504` tries drives 0-3 for the marker file
(`disk1/2/3`, wanted disk in `$8289C`, drive in `$8061C`) and only asks
"PLEASE INSERT … IN ANY DRIVE" when none has it.

Port additions (options, not the original's; 2026-10-28):
- Trackpad keys: Command or Control held = right button held (fire); a
  left press while only that key is held issues the walk order ($9EB36
  path) and does not set both-buttons, so it never throws; Option = both
  buttons held (throw). Checked with `play` holds `c`/`C`/`o` on phase 6:
  Option throws (ammo 10 → 9), Command + click sets the same 6-point route
  as a plain click, no throw.
- Cheats: Top rank = JOOLS ($AA3A6 sets roster[0]'s rank byte $80666 to
  15), applied at each phase start and when a new first recruit joins;
  Bullet proof = `hit_soldiers` stops enemy bullets harmlessly and
  `blast_soldiers` hurts no recruit (all explosions: mines, trip mines and
  booby traps become explosions; snap traps $9B35C and water kill
  directly). First version spared only the enemy's explosions (save
  version 6 kept a per-slot record; version 7 drops it, reads and
  discards it from 6) — owner asked for all explosions.
  Infinite ammo = squad grenade/rocket counts restored after each tick.
  Checked with `play` (toprank/bulletproof/infiniteammo): idle squads lost
  on phases 1, 2, 3, 5, 8, 10 survive with Bullet proof, and their own
  grenade (phase 6, 5 casualties without) kills nobody; with all cheats off the replays (replay_all) are
  unchanged.

### View and scrolling

Read from the code, with the machine checks marked.

- Display: copper list at `$3134` (COP1LC, set once at start-up; `fload`
  points it at its own list `$4AD34` while loading). The game's frame
  interrupt is level 1 (`$64` = `$8A2E2`), raised by the copper's `INTREQ
  $8004` at line $24; `fload`'s level-3 handler `$72046` runs disk timing.
  Mission settings: 4 planes, DDF $38-$D0 (320 pixels, 40 bytes, modulo 0),
  window $3081-$10C1 (320 × 224). The left panel is not in the bitplanes
  (sprites, hypothesis).
- Two screen buffers, $75F60 and $6BEC0 (table `$8148A`, 4 planes
  $2828 bytes apart = 257 lines of 40 bytes), swapped by `$80616`
  (toggled each tick); `$81496` shown, `$81492` drawn.
- Each buffer is a ring of 320 × 256 pixels: map pixel (camera.x + dx,
  camera.y + dy) is at ring index (by + dy) × 320 + bx + dx, wrapping into
  the next line. bx = `$814A6` (16.16, = camera x mod 320 while scrolling
  sideways; crossing 0/320 steps `$81516` by ±$28 and `$81518` by ±1), by =
  `$814AA`. Checked on map 2: both buffers equal the map rendered this way
  except where objects are drawn (562 of 73,200 pixels).
- `$86956` (each frame) turns that into the copper list: byte offset `$814A2`
  = ((bx >> 3) − 2), +40 when negative, made even; BPLCON1 = (15 − (bx & 15))
  for both playfields; split line `$3254` = $130 − by (when > $FF: the PAL
  wrap wait at `$3250` and $30 − by), where the second pointer block
  restarts at the ring's start. `$86624` writes the pointers (buffer +
  `$8149A` + `$814A2`, then buffer + `$814A2` after the split).
- Result on screen: line l shows map row camera.y + l; column c shows map
  column camera.x + c − 31 (columns 0-30: ring's previous line; columns
  15-30 show ring columns 304-319, which are not kept current; the panel
  covers columns 0-47). Checked: native `cannon_fodder_assets view` equals
  the original's display (copper pointers, split, BPLCON1 delay, window,
  `harness/display.py`) on map 2 at cameras (431, 255) delay 0 and (423,
  250) delay 8 with the ring wrap at line 5: in columns 48-319 only object
  pixels differ (583 and 562).
- Visible map area therefore camera.x + 17 … camera.x + 288 and camera.y …
  camera.y + 223. With the camera limits below, the last 16 map columns and
  the last map row can never be seen; on map 1 (camera fixed at 0, 0) map
  columns 0-16 and 289-303 and rows 224-239 are never visible.
- Camera ($814AE x, $814B2 y, 16.16). Order within each frame's vertical
  blank (mission): `$867A0` (drop the chase velocity on axes the hard
  follow drives), `$867BE` (speed limit `$81E1C`: 2 px/frame; 0 when the
  tick is overdue: `$81E1A` set and `$81488` ≥ 3; on a one-screen map or in
  states `$81DD2`/`$81DCA`/`$81DCC` the follow steps, velocities and limit
  are zeroed, the follow flags stay), `$8682A` (velocity += follow step;
  reversal hold; limits camera ≤ (map width − 305, map height − 225) as
  unsigned words; ±limit), `$869CE` (x += vx, stop at 0; ring x bx wraps
  mod 320 and then steps `$81516` ±$28, `$81518` ±1 and `$814D8` ±1 line),
  `$86A8C` (y += vy, stop at 0; ring line by += vy + `$814D8`, masked to
  0-255; `$8149A` = by × 40), velocities cleared, then `$873FC` twice
  (chase), later `$8727C` (look-ahead target) and `$9C0D4` (hard follow).
- Chase (`$8741A`, called twice per frame through `$873FC`): aim = target
  (`$815EA`, `$815EC`) − (144, 112), each ≥ 0; distance estimate `$A1062`
  from the camera; ≤ 16 → speed = distance; else speed += ½ up to `$814BC`
  (20, set by `$8727C`); no direction (aim = camera) → speed −= ½ (≥ 0);
  velocity = (sine[a] >> 2, sine[a + $80] >> 2) × whole speed, i.e. ⅛ of the
  speed in pixels per frame, before the 2 px cap.
- Look-ahead target (`$8727C`, every frame): pointer x > 30 (over the play
  area): origin = leader (x + 24, y) (leader = record `$FDB2C + $81554`,
  the first living member of the selected squad, `$9F940`); point = camera
  + pointer; reach = `$A1062` estimate capped at 140 (100 while `$82798`,
  hypothesis: vehicle); angle `$A0B8E`; target = origin + (sine × reach) >>
  16 per axis (sine peaks at 32767, so half the reach), x − 24, x ≥ 0.
  Pointer over the panel: on first entry target = camera + (144, 112), then
  it moves with the leader. Right button held: other branch ($872A4, open).
  A click sets it too (`$9EA10`: camera + pointer − (22, 3), y ≥ 3).
  Checked: three pointer positions on map 2 give exactly the original's
  targets (native `look_ahead_target`, `cannon_fodder_assets lookahead`).
- Hard follow (`$9C0D4`, every frame, after the step): leader − camera =
  (dx, dy); x: active and 145 ≤ dx ≤ 161 → stop; dx < −15 → −2; dx > 281
  → +2; y: active and 101 ≤ dy ≤ 142 → stop; dy < −11 → −2; dy > 254 →
  +2 (steps are 16.16 high words, written to `$814CC`/`$814D0`, flags
  `$814D4`/`$814D6`). Stop band centre = screen column 183, the middle of the
  play area.
- Reversal hold (`$8682A`): when an axis's velocity changes sign (0 counts
  as positive) its counter (`$81540`/`$81542`) is set to 9 and that axis
  does not move while it is non-zero. It is never counted down: the tile
  drawer's bookkeeping clears it (next item). Seen: 3 to 8 frames.
- Tile drawer. Each game tick (`$A38D6` in the mission loop): on ticks with
  `$80612` = 0 (every second tick) a snapshot: camera into `$81506`/
  `$8150A` (change since the last snapshot into `$81522`/`$81524`), ring
  position into `$8150E`/`$81512`, direction signs `$8153C`/`$8153E` into
  `$81538`/`$8153A`. Then `$9DE42`: `$A34B4` (y) and `$A3526` (x), on the
  same ticks only: direction = sign of the snapshot; equal to the drawer's
  (`$81528`/`$81526`) → clear the hold, drawer origin = snapshot (camera into
  `$81530`/`$8152E`, ring into `$81534`/`$81532`), fill offset `$8152C` =
  240 or −16, `$8152A` = 304 or −16 by direction; different → store it and do
  the same only if the ring position >> 4 equals the drawer's ring >> 4
  (still the same 16-pixel column/line), otherwise the hold waits for the
  next snapshot tick. Then, every tick: `$AB63A` queues a tile column (16
  tiles) at map x = drawer camera x + offset, map rows from the drawer camera
  y's tile row, ring destination from the drawer's ring column − 16 and ring
  line (camera y & $F0) + (column x / 320 & 15), split where it crosses ring
  line 256 (`$AB76A`); `$AB46A` queues a tile row (20 tiles) at map y =
  drawer camera y + offset from the drawer camera's tile column (one left
  when moving left), wrapping the ring column at 40 bytes; `$AB39E` blits the
  queue (`$81E22`: source tile, destination; tile n at `$3FBEA + n × $80`,
  4 planes $2828 apart, 16 lines × 1 word, BLTCON0 $05CC = B to D,
  destination modulo 38). The drawer is invisible as such: the ring is
  always current within the shown area (the display checks above), only the
  hold timing shows.
- Ring position follows from the camera: x mod 320 and (y + x div 320) mod
  256 lines, fractions as the camera's. Checked on all 260 frames of cam3.
- Snapshot timing: the tick starts right after the vertical blank that
  brings `$81488` to 3, but `$A38D6` comes late in the tick, so it saw the
  camera of the tick's first frame in 11 snapshot ticks and the camera one
  vertical blank later in 17 (cam3; heavier ticks run past the blank). The
  native game takes the snapshot at the start of the tick (deterministic,
  no CPU timing); `tools/holdmodel.py [--late]` shows either rule matches
  the recorded holds on all but 1-2 of 259 frames, and `camreplay --holds`
  (native drawer tick) matches the camera on 258 of 259 frames: the first
  reversal starts one frame earlier than the original's late snapshot.
- Geometry tables in the program (no relocations): distance `$A128C` (32 ×
  32 bytes), sine `$A168E` (256 words, ±32767), angle `$A1890` (32 × 32
  words, octant 0-64, −1 for (0,0)). Direction (`$9E28C` from bit 14,
  `$A0B8E` from bit 9): scale |dx|, |dy| to 5 bits, a = 2 × table; dx ≥ 0:
  $80 − a (dy ≥ 0) / $80 + a; dx < 0: $180 + a / $180 − a; x = sine[a], y =
  sine[a + $80].
- Checked: `harness/camtrace.py` recorded 2 × 200 frames on map 2 (soldier
  teleported 320 px left: follow, reversal hold, 2 px steps, stop, easing;
  pointer at (60, 210): both axes easing). Native `step_camera`
  (`cannon_fodder_assets camreplay`, inputs per frame: follow flags/steps,
  target, holds, tick flags) equals the original in position, velocity and
  chase speed on all 398 frames, free-running; the follow decisions
  computed natively from the leader's position equal the recorded ones on
  all frames (`tools/cammodel.py` is the Python research model).
- Start pan (`$9C1FE`): runs until the distance estimate to the target is
  ≤ 139, so the camera stops short of the target. Checked: map 2 target
  (654 − $98, 366 − $70) = (502, 254), camera after the pan (431, 255).
- Open: the panel sprites, objects drawn into the
  buffers, the right-button branch of the look-ahead, `$9C67E` (between the
  two chase calls).

### File records (around $86BE6)

u32, u32, u8 flag, zero-terminated name, padded to even:

| Record | Name | flag |
| --- | --- | --- |
| $86BE6 | hills.lbm | $FF |
| $86BFA | grave32.pal | 0 |
| $86C10 | juncopt.lbm | $FF |
| $86C26 | abgnd32.lbm | $FF |
| $86C3C… | SENSPROD.RAW, VIRGPRES.RAW, CFTITLE.RAW, WON.RAW | 0 |
| $86C90… | PRETENTIOUS1-3 | $FF |
| $86CD2 | disk2.raw | 0 |

Hypothesis: flag $FF = IFF picture, 0 = raw; the two longs are load and
work buffers ($736F8/$73738, $C484A). `PRETENTIOUS1-3` are not files on
any disk (to be traced).

### Reference machine

`harness/run.sh` (FS-UAE A500, KS 1.3, 1 MB chip, slow 0, port 6863, the
three disks in DF0-DF2: `--floppy_drive_count=3` is needed),
`harness/mkboot.py` (disk-1 copy whose boot block skips the crack intro:
`JSR $40000` → NOPs, checksum fixed; the game then boots straight into
`fload`), `harness/cf.py entry|go|regs|mem|frames|click|rclick` (clicks:
pointer poked at `$8CABA`, buttons by writing the button variables at the
returns of `$8CBA0`, since the GDB stub ignores SR and PC writes),
`harness/tomission.sh [clicks] [phase]` (boot, optional phase poke after
`$85EBA`, click into the mission), `harness/display.py [--enter] [--ticks N]
out.png out.raw` (the display as the copper builds it), `harness/grab.py`,
`harness/coplist.py ADDR`, `tools/viewcmp.py`, `harness/camtrace.py out N
[DX] [PX PY] [AT DX2]` (camera state per frame, optional soldier teleport and
pointer), `tools/camtrace2txt.py`, `tools/cammodel.py`, `tools/holdmodel.py`,
`harness/atspawn.py OUT [phase]` (object table right after the spawner and
at tick 1), `harness/objgrab.py OUT [ticks] [--enter]` (table + freshly
drawn buffer + camera), `harness/sprgrab.py OUT.png` (hardware sprites as
shown). The GDB stub has no write watchpoints (Z2 → E38): bisect with
breakpoints at successive call sites instead. Soldiers die within seconds
on map 2 if left alone: grab right after entering.
Checked 2026-10-27: the machine reaches `$85D92` and the 182,808 program
bytes in memory equal `work/F.bin`; the title screen follows.

Next: the soldiers' and enemies' first update and movement (step 4).

### Front end (reading notes, 2026-10-28)

Top level `$85E62`: once `$87082`, `$A28B0`, `$89E48`, `$96DE2`, roster
`$8073C`, `$80658` = 1, mission `$80632` = 0, `$871AE` (next mission), then
per phase: `$87056`, `$8706A`, `$9DB7E`, `$8CC38`; once `$9CBB0` (intro) and
`$9D366` (the "insert disk 2" prompt: not needed natively, skipped);
`$94B24`, `$982B8`; `$8A6A2`, `$A766C` (the recruits' hill, jon song 7),
`$A6614`; the mission's first phase `$86E22`, later phases `$86DF0`, then
`$8A53C`; the map loads; the mission loop `$86292`. Won: `$8725E`, else
lost (`$81DCA`) or failed (`$81DD2`); no recruit left (`$80638` = 0) or the
mission's last phase won: `$A8AF6`. After phase $47: `$9CAB2` (the end).

Screens seen on the reference (2026-10-28 captures): a text page ("not in
any way endorsed …"), the title (CFTITLE.RAW, poppy, fading in), the disk
prompt (DISK2.RAW), the hill (HOME n : n AWAY score, HEROES, SQUAD lists,
LOAD/SAVE icons, the hill picture), a parallax scene (sky, mountains,
jungle: `*play.lbm` + `*sky/mid/fgnd/fgn2.pl8`, `*heli.pal`) with the
mission's name scrolling, the briefing (MISSION n, PHASE n OF m, the phase
name, BRIEFING box with the objectives).

- `$9CBB0` intro (once, `$8065C` = $8421): jon song 1; `$9CE08` text pages
  (`$9D03C` table, `$9D018` draws a centred line, 150 frames); `$9CD36`
  pictures by list (SENSPROD, VIRGPRES, CFTITLE; fade in `$9D4B6` = 16 steps
  of 3 frames, wait for a click or the count `$829E8`, fade out `$9D50A`);
  a click skips (`$82A94`).
- `$9CEF6`: text pages over 1.RAW…8.RAW (`$9D08A` table, 13 pages, 300 or
  150 frames each; a click ends): the ending (hypothesis, from `$9CAB2`).
- File records `$86BE6…`: hills.lbm, grave32.pal, rankfont.lbm,
  morphbig.lbm, desplay.lbm + dessky/desmid/desfgnd/desfgn2.pl8 + desheli.pal,
  font.pl8, pstuff.lbm.

Campaign state (block `$80626`…`$80D4E`, 1832 bytes; `$8A026` copies it to
`$80D4E` and `$8A040` back):

- Roster `$80664`: 8 entries of 12 bytes: +0 recruit name (index into the
  360 names at `$917E0`, -1 empty), +2 rank, +3 phases served this mission
  (`$8725E` +1 for every linked entry when a phase is won; cleared at a new
  mission), +4 link (object offset, -1 none), +6 weapon word (3), +8, +9
  bit 0 marked on the panel, +$A kills.
- `$871AE` (before every phase): phases left `$80658` −1; at 0 a new
  mission: +3 cleared, `$80658`/`$8065A` = phases of the mission (`$A4C1E`),
  mission `$80632` +1, `$80652` = pool, `$80654` = -1 (new recruits on the
  hill), pool `$80638` += 15, the heroes list pointer `$80CE8` = `$80A16`.
- `$9E19A`: placed soldiers − roster entries = new recruits wanted
  (`$80D48`), at most the pool (taken from it); `$80D4A` = roster entries;
  `$80D4C` = soldiers this phase. `$89E7E` compacts the roster (empty
  entries out) and sorts it (same rank: more kills first). `$89F12` adds
  the new ones (`$89FAE`: name = `$8065E` (+1, < 360), rank = (mission − 1)
  / 3, weapon 3) and unlinks entries beyond `$80D4C`. `$9E21C` links the
  entries to the placed soldiers in order (heading $40); placed soldiers
  beyond are removed.
- `$96DF6`: the hall of fame (top 5 by kills, then rank) at `$80CF8`;
  HOME = recruits killed (`$80D1C`), AWAY = enemies killed (`$80D1E`).
- Screens per phase: `$A56B0` the hill (LOAD at x < 16, y < 27; SAVE at x ≥
  $11E, y < 27 unless the mission's entry in `$A9E26` forbids it; the hill
  below line 27 goes on), `$A9E56` load/save, `$8A6A2` disk check, `$A766C`
  the mission's parallax intro (jon song 7), `$A6614` the briefing. A lost
  phase is played again; no recruit left (`$80638` = 0) or a mission's
  last phase won: `$A8AF6` (the debrief with the fallen `$80A16` and the
  roster; jon song 0).

Native front end (`game/front_end`, `game/campaign`, 2026-10-28):

- `FrontFrames` reads any of the program's frame tables (group pointers
  unrelocated, frame addresses absolute); `FrontEnd::draw_frame` finds the
  picture loaded below the address (cookie-cut, colour 0 clear); text by
  `$A9A06`'s glyph order and a width table; fades as `$8CE34` (each gun −
  (15 − level)) in 16 steps of 3 frames.
- Font sources: the text pages use FONT.RAW at $57F60 (table `$8CFCE`,
  widths `$A9DA6`, colours 1-15 a ramp from white); the briefing's fonts
  are pstuff.lbm rows 160-207, which `$A66FE` copies into the $3294A
  buffer (table `$A6DC8`; widths `$A9C86` big, `$A9CE6` small; colours
  font.pl8); the hill uses hills.lbm at $3294A (table `$8D3F6`, header font
  group $D, widths `$A9D46`).
- Done and compared with the reference captures: the intro text page; the
  briefing (MISSION n, PHASE n OF m, the phase name at line $30
  (`$815A0`), BRIEFING with its box `$A0D70`, the objectives from `$A4CDA`);
  the hill's header (LOAD, SAVE by `$A9E26`, HOME, n : n, AWAY: kills left
  of the colon, casualties right). The flow new game → intro → hill →
  briefing → phase → hill … runs in the shell (`play … phase=new`).
- Open: the hill's left column (HEROES = hall of fame, SQUAD), graves
  (`$A5972`, `$8073C` ages), the recruits walking down the road (`$92FC8`
  path, `$A5DC4`/`$A5EEE`), the parallax intro `$A766C`, the debrief
  `$A8AF6`, the ending `$9CAB2`, load/save `$A9E56`, the music of the
  screens.

Front end, continued (2026-10-28):

- Done: the hill's column (`compose_hill_column`: `$AD` HEROES, bars `$A9`/
  `$AB` lit for the hall of fame / the recruits going, `$AE` SQUAD; ranks
  `rank+9`, names, kills by digits `$43…` 4 apart right to 48), from line
  26; the briefing's lines after loading (`$A7FA4`: WITH n SOLDIER(S) YOU
  MUST at $76, n RECRUIT(S) REMAINING / THIS IS YOUR LAST CHANCE at $BA,
  GO FOR IT at $CA); the debrief (`$A8AF6`: blocks of `$A918A` — 9 HEROES IN
  VICTORY / 8 LOST IN SERVICE at ($30, 0), medals 3/7 or poppies 0/4 at
  (0, $4A)/($F0, $4A); "Mission n" `$A9180` group 4 widths `$A9B66`; per name
  the stone bar block 10 with rank chevrons (group 2) at $64/$CC and the
  name (group 3, widths `$A9B06`), one per 44 rounds of 2 frames, $B4 rounds
  after the last) — the original scrolls its screen up while it draws;
  here the names stand where they come to rest (compared with a capture);
  the ending (`$9CAB2`: WON.RAW, warx song 1; the original stops there);
  the panel's ammunition counts (`$892FC`); LOAD/SAVE (one saved game,
  `saved_game.txt` in the game's folder; the original asks for a name and
  lists saves: open); squads: select, pick, split (`$889FC`, `$885CE`),
  join (`$9F0E8`, `$ABE6E`).
- Music: the intro and the ending play warx4.sng/warx1.ins song 1 (`$86FDA`
  loads that pair into jon's buffers), the intro's end jon song 0
  (`$86FA2`), the debrief jon 0, the parallax intro jon 7.
- `$A766C` parallax (rebuilt 2026-10-28, see "Parallax" below; the next
  lines are the first reading): dual playfield copper bands at lines 0/44/152/204
  (`$A7544`), layers of `<land>play.lbm` scrolling 1/2, 1, 2, 4 a frame
  (`$A7A5C`) with `<land>sky/mid/fgnd/fgn2.pl8`, the helicopter = pointer
  image $CC flying the path `$A7A20` (`$A7924`), the mission's name sliding
  in from $138 to $C by 4 a frame; a click fades out.

The hill, rebuilt in full (2026-10-28, after 0bc3151):

- `$A5A7E`: the hill (hills.lbm rows 29-212, from x 48) goes to the screen 4
  lines higher and is cleared in the picture's buffer, which becomes a sheet:
  `$A5C3A` draws there each entry of `$90D3E` (the 9 graves, group 0, and 24
  walking recruits, groups 1-12 × 2 frames) scaled to its list's
  percentages (33/50/66/85/100, or 50/82/100), centred, 16 apart; `$A5B46`
  copies the standing pieces of `$A5BC0`. The table `$90F10` reads that
  sheet (graves groups 0-8 × 5 sizes; recruits groups 10-33 × 3 sizes;
  standing group 9).
- Graves (`$A5CC4`): per fallen recruit, the newest at place 360 − n of
  `$A5110`, the older after it; group min(rank / 2, 8), frame min((y − 20)
  / 32, 4). (Centring of a scaled piece on x + 8 is my reading.)
- Recruits on the road (`$A5D6E`, `$A5DC4`, `$A5EEE`, `$A5FBC`): the path
  `$92FC8` (293 points: x, y, state); 15 walkers from $B4 bytes in, spaced by
  `$A5DA4`; while walking, all recruits are placed afresh each frame (those
  waiting from before, `$80652`, every 4th point from 272 back, looks from
  `$A5ECE` starting at the graves count), the walkers one point a frame,
  their step toggling every 2 frames; within 5 free points of the end or of
  someone standing they close up for good (`$8277C`): then each frame the
  front one leaves (a new recruit's name comes up in SQUAD, `$A60A4`) while
  any are due, and the rest close up a point. Picture: group 6 + state (+2
  below line 124, +4 below 183), size by the point (≤ 70, ≤ 122, beyond).
- Saves: SAVE writes the campaign as "MISSION n PHASE m" into `saves/` in
  the game's folder; LOAD lists them (text font) and resumes at that hill.
  The original types a name and resumes through its "phase won" path:
  the naming is not rebuilt (no text entry in the shell).

### Parallax (`$A766C`), rebuilt 2026-10-28

Evidence: the program (copper data `$A7514`/`$A7544`, tables `$A7CEC`,
`$A8AD8`, `$A7A20`, the file list at `$86CC0`), the reference's main copper
list read from chip memory ($3134 COLOR00-15, $31FC BPLCON0, $3204 BPLCON2
$64), and `work/para1.txt`: 329 loop rounds recorded by
`harness/paratrace.py` (break at `$A77C2`; words `$826B0`-`$826EA`,
`$85BE4/6`) with screenshots at rounds 1, 40, 100, 200.

- Display: dual playfield, 3 planes each (BPLCON0 $6400), modulos 40: each
  playfield a 640 x 256 bitmap, PF1 at $71000, PF2 at $62000; window
  $2C91-$2CB1 (columns 16-303).
- Content (`$A8A3C`): both cleared; `$A7DF6` lets the header routine
  `$A812C` (line `$815A0` = $14: MISSION n at 0, the mission's name from
  `$A83E8` at 20; with any other line it draws the phase's name) draw into a
  320-wide buffer; rows 0-19 go to PF1 rows 0-19 (left half), rows 20-39 to
  PF1 row 210 (right half). PF1 rows 20-209 and all of PF2 cleared again.
  Table `$A8AD8` (dest, picture row, rows, dest row): play.lbm rows 0-175 ->
  PF1 24-199, 176-250 -> PF2 52-126, 252-299 -> PF2 152-199; each row twice
  side by side (3 planes).
- Colours (`$86CC0` list): sky.pl8 $80102, mid $80112, fgnd $80122, fgn2
  $80132, heli.pal $80142 (-> $C482A, the sprites' 16-31), font.pl8 $80162.
- Bands (copper waits at v 68, 136, 196, 248 = lines 24, 92, 152, 204):
  A 0-23: PF1 rows L, scroll $826B0 (the name x), font colours; PF2 rows L,
  coarse $826B8, fine 0, mid colours; BPLCON2 $64 (PF2 in front).
  B 24-91: PF1 row L-4, $826B4, sky; PF2 as A with fine $826B8.
  C 92-151: PF1 row L-4, $826BC, fgnd; PF2 mid; BPLCON2 $24 (PF1 in front).
  D 152-203: PF1 continues ($826BC); PF2 row L-4, $826C0, fgn2; BPLCON2 $44.
  E 204-255: PF1 row L-4, scroll $826C4 = $158 - name x, font colours.
  Scroll: pointer + (value >> 4) words, BPLCON1 = (value & 15) ^ 15
  (`$A7CEC`/`$A7DDE`, `$A7C66`); colours faded by `$826E4` (`$A7B5A`).
- Loop `$A77C2` per frame: copper values, colours, `$A7AE6` (fade up on
  every second vertical blank while the sprite x `$85BE4` > $30 and no
  click `$81484`; the helicopter flies once the fade is 16; else fade down,
  the screen ends at 0), `$A7A5C` (name x -4 to $C; layers -1/2, 1, 2, 4,
  wrapping at 320), the rotor piece $CD + `$826E6` (0-2) into the sprites.
- `$A7924`: the sprite words get the position first (`$8CABA` = x,
  `$85BE4` = x - $20, y), then a new leg when the count runs out (a
  negative count: no move), then the move (sine(heading) >> 2 x speed),
  the turn (`$955C0`), the speed by 1 towards the leg's. The set-up calls it
  once with heading 0, then sets heading and aim to $180.
- The helicopter: body $CB (48 x 23) on attached sprites 2-7 at x - $20
  (`$9C78A`, 16 apart), the nose $CC on 0/1 at x + 16 (`$9C67E`); `$9C65A`
  copies height + 1 lines (dbra); the rotor replaces the body's top lines.
- The variables are zero at each parallax's start (read in a running
  mission; nothing in the program resets them: hypothesis, a bulk clear).
- Verified: the native state equals the recording in all 328 rounds; the
  pictures equal the screenshots (taken two rounds later than the halt) but
  for 0, 2 and 10 helicopter pixels at rounds 40, 100, 200. Those were
  found with the sprites' memory (`work/para2-*.sprites`): the port copied
  the rotor one line too far (PanelArt::piece's `lines` already counts the
  dbra's extra line); now equal to the reference's sprite data in all
  three dumps. para2 began one vertical blank later than para1 (the fade
  steps on even `$80618`): its round n equals the port's n - 1.

### Debrief (`$A8AF6`), rebuilt 2026-10-28

Evidence: `harness/debriefrec.py` (mission 1 won by setting `$81DCC`; with
`fallen NAME RANK` one fallen recruit injected into `$80A16`/the graves at
`$80A12` at `$A8AF6`): `work/deb1` (heroes only, 301 rounds), `work/deb2`
(a fallen recruit, 525 rounds), the screen's five planes at listed rounds.
`cannon_fodder_assets debrief` writes the native screen's indices: equal to
both recordings at every dumped round (0 pixels), colours equal to the
copper's.

- Screen $73738, 5 planes, 320 x 256. Blocks of `$A918A` (`$A8EF8`) and
  frames of `$A91E2`. LOST IN SERVICE (8) + poppies (0, 4) at y $40 when
  this mission has fallen (`$80A16`, reset per mission at `$87240`), else
  HEROES IN VICTORY (9) + medals (3, 7); "Mission n" at $DC (group 4).
- Rounds of two frames (`$A8D1E`): the column x 96-223, lines 46-255 up a
  line (`$A2750`); `$A8F3C` (heroes) every 44 rounds the next new rank
  (rank + served, max 15, `$A8E10`) at y $80/$83 where the lines pass (first
  after $95 rounds, $79 after the switch; `$821A2` is not reset when the
  fallen come first); `$82166` 44 rounds a line (`$A8FC4`): stone bar block
  10 at ($60, $E9), the name + 2 at $EF (group 3), ranks at $64/$CC, $EC.
  After the fallen: $A4 rounds, fade out (`$A33A8`, 3 frames a level),
  banner/medals (`$A8EDE`), fade in, then the heroes; after the last line
  $AA rounds; the left button held ends it. A chevron drawn at ($60, $74)
  before the second clear is not on the screen.

### Load screen (`$A9E56`) and the overview (`$A68CE`), 2026-10-28

- `$A9E56`: the hill's header font (table `$8D3F6` group $D, widths
  `$A9D46`) in the hill's colours: SELECT FILE ($C), UP ($24), DOWN ($AE),
  CONTINUE GAME ($C8), five names from $3E 21 apart; boxes `$A0D22` (x-2,
  y-2 colour $C / x-3, y-3 colour $F; names 3/2) of width + 4, tallest
  glyph + 5; areas `$AAB56` (x-3, w+4, y, h+5) tested by `$A0662` (pointer x
  + 16 against x, pointer y against y); fades a level a frame. Before it,
  `$AA1CE` asks for the save disk (INSERT GAME SAVE DISK, OK, EXIT): not in
  the port. `work/load1`: the native drawing of that request equals the
  reference's displayed screen ($75F60, 4 planes) on every pixel, areas
  equal.
- The pointer (`$81FDE`, the vertical blank's `$9C67E`): shown on the hill
  and the load screen; hidden (x $140) on the intro, briefing, debrief.
- The overview: the globe's area ($88482: x 0-$2F, y $D5-$E0, offset by the
  panel sprites' place $85BE4/$85BE6 = 0, 4 in `$9EA10`) sets `$82A7C`; the
  mission loop calls `$A68CE` before its next tick. Built at the phase's
  start (`$A6B14` from `$8608A`): 280 wide by the map's shape, at most 208
  high (`$A5090`), the map's pixels stepped in exact 16.16 (`$A2092`), at
  ($B0 - w/2 - $20, $68 - h/2) (`$A6D28`). Shown from byte $26 with BPLCON1
  $FF (column x + 31, line y - 1), the panel stays; the leader's marker is
  the pointer's picture $28 at (x w/W + left + 16, y h/H + top), 24 frames
  of 32; a new press leaves. `work/ov1`: size, place and marker equal; the
  picture equal (but the marker's blink and the emulator's border).
  `$A6C86` (a zoom) and `$9E32C` (a sprite overlay) have no callers.

## Object kinds

Kind handler table `$94608` (4 bytes per kind, 111 kinds). Placed counts
over all 72 `.spt` files in brackets. Status: **done** = rebuilt natively (code reading; see "Verification"),
**open** = not yet. Names are hypotheses from the handlers and pictures
(`cannon_fodder_assets graphics <content> <map> <first> <last> <png>`).

| kind | handler | what | status |
|---|---|---|---|
| 0 | $96730 | recruit soldier [254] | done |
| 1 | $96C3A | (dynamic) | done |
| 2 | $97F62 | grenade | done |
| 3 | $980F0 | shadow/follower | done |
| 4 | $953E0 | nothing (helper slots of vehicles: rotor, shadow) [167] | done |
| 5 | $9DADE | enemy soldier [215] | done |
| 6 | $97878 | bullet | done |
| 7 | $955E0 | helicopter core (also called by kinds $28-$2C, $31-$34, $6B) | done |
| 8, 9 | $959D2 | rts | done |
| $A | $97C9C | (dynamic) | done |
| $B | $975AA | (dynamic) | done |
| $C | $973A6 | explosion | done |
| $D-$11 | $97296… | bushes/trees | done |
| $12 | $96E7E | flicker decoration, graphic $94 [14] | done |
| $13 | $96EAE | drifter, graphic $98, x −2/tick, wraps to 2000 | done |
| $14, $19 | $96ED6, $970AC | enemy huts | done |
| $15 | $954F6 | leader marker | done |
| $16, $17 | | shadow, blood | done |
| $18 | $97018 | hidden spawn point | done |
| $1A | $971F4 | animated decoration, graphic $9C | done |
| $1B | $9720C | animal | done |
| $1C-$1E, $22, $3A, $3B | $948E4… | end texts (sprite objects scrolling up to a fixed line; `$9487C`) | done |
| $1F | $9BBAA | ground vehicle core | done |
| $20 | $9BFE6 | rts | done |
| $21 | $9B6B6 | bazooka rocket | done |
| $23 | $953E2 | bush shadow | done |
| $24 | $9B48E | bazooka soldier | done |
| $25, $26 | $9B3B0, $9B3FC | crates (grenades, rockets) | done |
| $27 | $9B152 | tile explosion | done |
| $28, $2A-$2C, $6B | $9ADC0 | enemy helicopters (types 6, 5, 7, 8; $6B last mission) [5,4,8,2,1] | done |
| $29 | $9ABFC | vehicle piece (placed after vehicles) [188] | done |
| $2D | $9B886 | missile | done |
| $2E, $61 | $9BA52 | homing missile (player / enemy) | done |
| $2F, $30 | | debris, smoke | done |
| $31-$34 | $9A93E… | player helicopters types 5-8 [5,11,4,8] | done |
| $35 | $9A694 | falling object (height −0.5/tick, then explosion) | done |
| $36 | $9B2D6 | mine → big explosion $59 [42] | done |
| $37 | $9B2FA | trip mine (leader state $32, explodes after 10 ticks) [4] | done |
| $38 | $9B35C | bear trap [28] | done |
| $39 | $9ABE6 | puff (frames down every 2nd tick) | done |
| $3C | $9A5F2 | house with smoke | done |
| $3D, $3E | $9A2A6, $9A2DA | civilians ($3E: speed 10, attacks when angry, `$9A490`) [14, 9] | done |
| $3F, $40 | $99F92 | ground vehicles types 2, 3 (side 0) [4, 9] | done |
| $41, $45 | $999AA, $99668 | ground vehicles ($999E6) [15, 17] | done |
| $42, $43 | | birds | done |
| $44 | $9A16C | civilian walker, graphic $D5 (`$9A092`) [37] | done |
| $46 | $9A31A | hostage-like civilian (calls out, `$87906`) [9] | done |
| $47, $4D | $994BC, $99E5C | projectiles (dynamic) | done |
| $48, $6A | $98F12 | building with prisoners, graphic $D9 [13, 1] | done |
| $49 | $98E48 | target object, graphic $DD; big blast → $59 [8] | done |
| $4A-$4C | $98D68… | buildings releasing civilians $3D/$3E/$46 [7, 9, 19] | done |
| $4E, $4F | $98B4A, $98B5A | player gun turrets / tanks (side 0, types 0, 1) [31, 12] | done |
| $50-$52 | $98A40… | enemy vehicles types 2, 3, 10 ($9BBAA) [4, 6, 1] | done |
| $53 | $9A290 | standing civilian [1] | done |
| $54, $55, $69 | $98B14… | enemy gun turrets types 0, 1, 9 [91, 39, 8] | done |
| $56, $57 | | splashes | done |
| $58, $64 | $98836, $9881E | enemy holes ($64: takes 105 big blasts) [97, 1] | done |
| $59 | → $973A6 | big explosion | done |
| $5A | $987B6 | civilians' home (sets `$82A18`) [2] | done |
| $5B | $9A04C | graphic $E1 (with $9A16C) [8] | done |
| $5C | $98714 | graphic $E2, becomes explosion near the leader [19] | done |
| $5D-$60, $62 | $98608… | power-ups (graphics $95, $E4, $E3, $E5, $E6) | done |
| $63 | $984F4 | graphic $E7, counted (`$82A28`) [9] | done |
| $65-$68 | $9840C… | player helicopters waiting for `$82A6C` | done |
| $6C-$6E | $982D2… | sturdy targets (70, 105, 175 big blasts) | done |

Reading notes:

- `$9D69A` (hit test of explosions and bullets): an object hit by kind $59
  gets state 7, by $27/$C state 5, by a bullet state 1 (killable) or stops
  it. Fixed in `blast_objects` (big blasts mark 7).
- `$9B448` touch test: Z also when the distance is exactly 6 (its last
  `cmp #6` / `bge`), so crates, mines and power-ups take ≤ 6.
- Dispatch had kind $37 on the trap routine; the trap is $38 ($9B35C).

### Vehicles (reading notes, 2026-10-28)

- Placement groups: helicopter = body + 4 (rotor, graphic $8C) + 4 (shadow
  $8D) + $29; ground vehicle = body + 4 (shadow) [+ $29]; turret = body +
  $29 (at +5, −15 − height). The body's code drives its helper slots
  (slot+1, +2, +3).
- Fields: +$6F type (0/1/9 turrets, 2/3/10 ground vehicles, 5-8
  helicopters), +$65 "is a vehicle", +$26/$28 drive target, +$36 speed,
  +$10 heading, +$1E height (16.16 long), +$1A vertical speed, +$38 state
  (5/7 hit by blast → explosion $C, helpers freed; $32/$33 bump/slide
  back), +$52 sinking count, +$56 bump, +$57/$54 fire latch/request, +$5B
  barrel toggle, +$5E target scan index, +$62 patrol timer, +$46/$48 rest
  point, +$4C wait, +$74/$75.
- Soldier fields: +$66 the vehicle he is walking to, +$6A the vehicle he
  sits in, +$6E carried. `$96730`: carried → `$9A82C` (hidden, at the
  vehicle; vehicle gone → hit, random heading). `$9A87E` (each tick, on
  foot): +$66 set and within 13 of the boarding point (vehicle +16 (−12 for
  types 0/1), −9) → +$6A, carried, `$9F9C6`; the squad's first member
  starts `$82A02[squad]` = 400. `$9A8FC` (per tick): that count runs out →
  all members state $5A (`$95A28`: puff $CC frame 4 → state $5B counts the
  frame down → `$95A88` placed at the boarding point, graphic $7C).
- `$9A730` (per tick): every member of the selected squad carried →
  `$82798` = their vehicle, leader slot `$81554` = the vehicle (the camera
  and the touch tests follow it); else `$82798` = 0.
- Driving (`$9EDDC`, left button held, in a vehicle): target = pointer
  map position − ($1C, −6) (x ≥ 0, y ≥ $14; helicopters +$20 y) into the
  vehicle's +$26/+$28. A click on the own vehicle (`$9EF96`): flying
  helicopter → +$6E = −1 (land); standing on open ground → everybody gets
  out at the vehicle (+15, −10 except turrets), walking to −6, +16.
- Ground vehicle core `$9BBAA` (types 2, 3, 10): engine sound $38 + speed
  /16 (`$A3B60`), destroyed `$9C058`, bounce `$9C03A`, steering `$9BCF4`
  (towards +$26/$28, speed $48), slows within 30, speeds up by 3 to 50 (24
  for kind $52), terrain `$9BDC2`, shadow `$9BF9C`, crush box `$98220`
  (objects in it get state 5). Type 3 adds `$99C7C` (gun: right button
  alternates two barrels, `$99D74`).
- Helicopter core `$955E0` (graphic $8B, rotor $8C at height+14 animated,
  shadow $8D). Destroyed: spins down, falls, → explosion, helpers freed;
  `$9A630` landing height by terrain. Player helicopters `$9A954` (types 5-8,
  `$9A7B6` weapons: 6 grenades `$9AC76`, 7 missiles `$9B760`, 8 homing
  `$9B93E`). Enemy helicopters `$9ADC0` (patrol from the rest point,
  random targets on open ground, attack the recruits within 250
  (`$9B042`), drop troops when landed).
- Missiles: $2D (`$9B886`, graphic $A3, accelerating, explode at the
  target or in a wall → $59), homing $2E/$61 (`$9BA52`, follow +$1A's
  object).

### Native updates for every kind (2026-10-28)

All kinds placed on the 72 maps (and the ones the game makes) have native
updates: `game/vehicles` (jeeps, tanks, turrets, helicopters, missiles,
the jeep's gun shot, spears, boarding/riding/driving), `game/scenery`
(mines, trap, holes, civilian buildings, decorations, tent, house, booby
trap, power-ups, the last mission's markers/trees/time limit),
`game/walking` (civilian variants, bystanders, hostages, end poses),
`game/mission` (end texts and count). Every map runs 900 frames headless
without an unhandled update. Kinds 1, $A, $B (dynamic) were not seen.

Verification so far: the earlier recordings still replay unchanged (map10
15 artefact fields instead of 17: the trap/mine dispatch fix); the music
and end-text timings against the reference (forced loss on mission 1).
(Vehicles, hostages and power-ups: see the recordings below.)

Corrections found while reading: dispatch had the trap on kind $37 (it is
$38); big blasts ($59) mark state 7 (not 5); `$9B448` takes distance 6 as a
touch; hits, blasts and crushing stop once the phase has ended (`$82A92`).

Recording map21.trace (2026-10-28; phase 20 reached by poking the phase,
`tomission.sh 200 20` and clicks, 136 ticks idle; turrets $54, a jeep $51,
the recruits' jeep $40, civilian building $4A, spawn points): the replay
loads the vehicle fields too. It found that `$9A20A` takes the highest free
slot below 30 (`$9819A`) for a released civilian (fixed: 332 → 199 fields).
The 199 left are a recruit's waypoint, a removed placed soldier and one
enemy's aim, the poked-boot roster artefact seen in map10; the vehicles'
fields agree in every tick. `CF_ALL=1` prints every difference.

Hostage, power-up and turret recordings (2026-10-28; `tools/replay_all.py`
replays every `work/*.trace` against its map, read from the phase):

- power1/power2 (phase 18, map 19): $5D and $5E poked at the leader with
  his rank and rockets cleared; both taken in the first tick: rank 15 on
  the roster, 50 rockets, slots freed; no differences. The replay now
  compares the roster ranks (first three places) too.
- host1 (phase 34, map 35): the hostage held by its guard (graphic $D8,
  frame 1). The replay loads the hostage's +$5E as a byte offset and its
  standing picture +$32.
- host3: guard and a turret removed through +$24, the squad poked next to
  the hostage: it walks to the leader (+4, −6) and stands; host4: hostage
  and squad poked by the tent: rescued at tick 44 (`$81F60` → 0), the
  phase won at tick 45, end poses. host1/host3/turret1 agree; host4 keeps
  the original enemy tank's random 1-in-32 shot in tick 32.
- Random parts checked by range in the replay: a civilian's wobble (all
  four kinds), the hostage's heading and place (2 pixels), its standing
  picture, the enemy tank turret's ±1 wander without a target, the end
  pose picked in the first tick; missiles the native side fires by chance
  count as chance shots (the replay's random numbers are not the
  original's).

Rules these recordings corrected:

- `$9819A` (slots 0-29) and `$98104` (all 43) set a latch when they find no
  free slot (`$826EC`, `$826EA`); every later search of that kind fails at
  once until an object is removed through +$24 (`$AB8B4` clears both).
  Without it a hut spawned into a slot freed by other means.
- `$A0B8E` returns the direction from the target to the gun (`d0 − d2`):
  turrets and tank turrets aimed the opposite way.
- The enemy tank (`$9969C`) searches every tick (its test of +$4C branches
  to the next instruction either way) and drops its aim (+$2E = −1) only
  when the scan index wraps past 29.
- The hostage (`$98F66`): the picture $D8 when it moved this tick, and the
  piece next to it, belong to kind $6A only (the port had the position
  test inverted and applied it to $48).
- The one-object-a-tick scans (`$98E64`, `$98EFE`, `$967B8`) wrap at 43
  (`$13D2`), not 44.
- End pose (`$96658`): after a failed objective (`$81DD2`) every recruit
  raises his arms ($38); +$36 toggles bit 15 of a word.

## Sound and music

Read from the program (`work/rjp1_a.asm`, `work/rjp1_b.asm`, `$A3AD0`…
`$A4B5E`) and checked against the original's player state; the code's
signature reads "CODED BY ANDI SMITHERS, VECTORDEAN (C) MILLENNIUM 1992".
No outside description of the RJP1 format or player was used.

### Files (`*.sng` + `*.ins`)

`.ins`: "RJP1" then sample data (signed bytes); instruments point into it
from after the 4-byte header. `.sng`: "RJP1SMOD", then seven sections, each a
long length and the data:

0. instruments, 32 bytes: +0 sample, +4 period table, +8 volume table
   (longs, offsets into the `.ins` data, 0 none), +$C envelope (offset into
   section 1), +$E volume scale, +$10 attack start (words; the player
   doubles it in place once, guarded by `$801B6`), +$12 attack length
   (words), +$14/+$16 loop start/length (words), +$18/+$1A period table
   loop/length, +$1C/+$1E volume table loop/length.
1. envelopes, 6 bytes: start, attack target, attack length, decay target,
   decay length, release length.
2. songs: 4 bytes, a sequence number per channel (0 none).
3. sequence offsets (longs into section 5).
4. pattern offsets (longs into section 6).
5. sequences: pattern numbers; 0 then 0 ends, 0 then a positive n jumps
   back n bytes, 0 then a negative byte and m continues with sequence m.
6. patterns: bytes below $80 are notes (byte offsets into the program's
   period table `$A4856`); $80 end of pattern (next from the sequence, rows
   1), $81 release, $82 n note length, $83 n rows, $84 n instrument, $85 n
   volume scale, $86 c l l l l slide (count, long step), $87 rest.

One pair per landscape (`junbase`, `desbase`, `icebase`, `intbase`,
`morbase`) and `jon` (front end, mission end) and `warx` on disk 1.

### Player (native `games/cannon_fodder_amiga/sound/rjp1`)

Entry points `$A3DBC` frame, `$A3DC0` init (a0 song, a1 instruments),
`$A3DC4` effect (d0 channel, d1 pattern), `$A3DC8` song, `$A3DCC` sequence,
`$A3DD0` channel volume, `$A3DD4` stop, `$A3DD8` mode. Globals at `$A4B5E`
(+0 instrument data, +4…+$1C sections, +$20/+$21 the multiplier word (the
low byte is the channel's volume during its update), +$22 mode (bits 0-1
steps a frame, 0 = every second frame by `$25`; bit 3 music, bit 2
effects), +$28-$2B channel volumes, +$2C busy bits (0-3 music, 4-7
effects)). Channel records at `$A489E`, $B0 bytes: the music voice
($00-$5B) and the effect voice (fields +$58 up to $4A, +$54 from $52).
Each frame, per channel: with an effect playing the effect voice sounds
(the music's sequence goes on counting underneath, its notes are not
started); otherwise the music voice if the channel is active. A voice: loop
pointer set the frame after the key ($39), DMA on the frame after the note
($38); note length/rows counting; envelope (attack → decay → hold, release
on $81; value = target − delta × count / length); volume table (vol +=
table × vol / 128); volume = vol × scale / 64 × channel volume / 64
(16-bit steps, clamped 0-64); period table (vibrato: base − table × base
/ 128, upward swings halved); slide (count, long step added to a long, its
high word added to the period). Note on: envelope start, DMA off, the
attack's first word cleared in the sample data, location/length to the
attack. The hardware's latching (a new location/length takes effect at the
block's end or a DMA restart) is modelled by `shared/amiga/paula_channels`.

### Game side (native `sound/board`, requests from the rules)

`$A3A30` (d1 effect pattern, d2 priority, a0 object): channels 2 and 3 in
turn (`$81DF6`); a lower priority than the channel's (`$81DF8`) waits while
it is busy (`$81DFC`, 12 ticks, run down in the mission loop `$863B0`);
channel volume 64, or for an object not in the draw list 40 − distance /
16 (at least 4) from the view's centre (camera + ($98, $70), `$A10AA`
distance capped at 640). Effects: shots $10/$11 (prio 0), hit cries from
`$A399C` (8: $B $C $D $14 $15 $16 $D $14; prio $14, writhing 0),
explosions 5-8 by the frame counter (prio $1E), grenade throw $F, rocket
launch $2E, splash $2B (each tick of a splash-down), hut door $10 (prio 1),
bird cries $1A jungle / $1F ice one tick in 128 (`$87906`). Not rebuilt:
the engine sound `$A3B60`/`$A3B14` (vehicles: a pattern repeated on
channel 1 at a rate), vehicles' and other kinds' sounds ($29, $2A, $2C,
$2D, $E entering, $16 hole).

Music control (native `sound/music_director` and `game/module`):

- Phase start (`$A39A4`): the landscape's pair, song 2, mode $D, all
  channel volumes 0, effect $F primed on channels 2 and 3. The director's
  variables (`$82852`-`$828BA`: current song, change in progress, target
  volume, nearest distance and song) are 0 at a phase's start: the
  reference cleared them when a forced loss ended mission 1 (frame 347
  after the poke). Rebuilt as `MusicDirector::start_phase`.
- Music points (`$8772C`): the map's cells in map order (x inner); a cell
  whose word has bits 13-15 set marks a point with that song (1-7), up to
  ten points (table `$827FA`: x, y, song, 0; −1 ends it). This is the
  meaning of the map cell flags. Maps 1-10 hold 1, 7, 8, 5, 5, 4, 6, 4,
  9, 9 points.
- Once a tick (`$87774`, from `$863D4`; not while `$81DCE` (end countdown),
  `$817D8` (open), the end songs (`$A3C5C`) or without a leader): the
  leader's tile (+0, +4 >> 4); the nearest point of songs 1-4 by the
  distance estimate `$A10F2` (either axis ≥ 15 tiles: out of reach;
  start value 16, a later point only when strictly nearer). Distance ≤ 4:
  that song at 64; 5-9: that song at 56 − (d − 5)·8; ≥ 10 or none: song 1
  at min((d − 8)·2, 48). The request `$87860`: ignored while a change is in
  progress; a different song starts a change (target volume = the
  volume); the same song sets channels 0 and 1 (1 not while the engine
  sound `$829F6` is on) at once.
- Every fourth frame (`$878A2`, from `$865AA` when frame & 3 = 0, while the
  mission loop runs and no end flag is set): a change to another song takes
  channel 0's volume down by 1; below 8 it is 8 and the new song starts
  (`$A3DC8`); for the same song it rises by 1 up to the target, which ends
  the change. Written to channels 0 and 1 as above.
- End (`$8CC72`, first tick of the end): a won phase (`$81DCC` and not
  `$81DCA`) shows its text and plays `jon` song 6 (the mission's last
  phase, `$80658` = 1, `$9480A`; phases per mission from table `$A4C1E`,
  24 missions, 72 phases) or 12 (`$9483A`) at once; a lost one plays song
  $F 10 ticks later (`$A3C58`/`$A3C5A`, also for `$81DD2`) or, with no
  recruit left in the pool (`$80638` = 0), song 8. `$A3C60` loads
  `jon.sng`/`jon.ins`, starts the song with all volumes 0 and `$82176` set,
  `$A3CEC` then sets all to 64. In the forced loss the `jon` song started
  28 frames (10 ticks) after the poke with all volumes 64.
- `$A3D22` fades (+1 every 4 frames with `$82176`, −1 each frame with
  `$82178`, −1 every 64 frames with `$8217A`): only the front end's use
  them audibly; not rebuilt. The front end plays `jon` 1 and 0.
- Not rebuilt: the recruit pool (a loss always plays song $F), the engine
  sound (no vehicles yet), `$817D8`.

Checked: map 1 headless settles on song 3 at volume 56 (the leader 5 tiles
from its one point), the state the reference showed in mission 1
(`$82854` = 3, `$82852` = 5, channels 0/1 at 56); the forced loss above.
Not compared frame by frame (no recording of a change of song).

### Checks

`cannon_fodder_assets sndreplay` (each frame from the original's player
state, every field compared after the native frame) on `harness/sndtrace.py`
recordings: snd1 (`jon`, 400 frames, between missions), snd2 (map 3 jungle
song 2, 600 frames, the squad firing: effects on channels 2 and 3, 65
notes, vibrato tables), snd3 (`jon` after a lost mission, 1500 frames, 63
notes, all four channels): 0 differing fields. Mutations caught (vibrato
halving 750 fields, decay length +1 3, volume table shift 1140). The
board in `cannon_fodder_assets replay` (channel, priorities, busy counts
after each tick and the next tick's): 0 differing fields on all eleven
recordings apart from ticks with a random bird cry (counted). Not covered by
any recording: release ($81) and slide ($86) (code reading only), speed
modes other than 1 step a frame. `cannon_fodder_assets music` renders a
song or effect through the native player and the channel model to WAV;
the channel model itself is not compared with FS-UAE's audio output.
