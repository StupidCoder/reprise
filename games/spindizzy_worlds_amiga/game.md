# Spindizzy Worlds (Amiga) — what the original does

Everything learned while reverse engineering the game: how it is built, how
its data is stored and encoded, how its rules work, and its bugs and
quirks. Remaining work is in [plan.md](plan.md).

Addresses are the original's: main is loaded at $10000, and all variables
are A5-relative with A5 = $616E8 (written $xxxx(a5) or just $xxxx in the
explorer sections). Hypotheses are marked as such.

## Sources and limits

- Image: the user's `Spindizzy Worlds (1990)(Electric Dreams)[cr HZ].adf`,
  901,120 bytes, SHA-256
  `d0df762238e5e8454cbdfe7ab99e3a28b65265f5e1b3742afeec5837abf9c055`. A
  cracked copy: the boot block's tag area keeps the game's own
  "SD][MASTER", and the crack adds an intro at $D1000.
- Everything here comes from this image and from running it (started
  2026-10-28). No disassembly, port, map or guide of this game was used.
- Tools: capstone 5.0.7 (`tools/m68kdis.py`), Ghidra 12.1.4 headless on a
  512 KB chip RAM dump taken in Tutorial level 1 (`work/game.bin`; the
  decompile is weak because arguments are passed in registers),
  Python/PIL renders.
- Reference machine (verification only): FS-UAE (prb28 fork), Kickstart
  1.3, A500, GDB stub on port 6870, its own `--state_dir`.
- Research files: `.analysis/spindizzy_worlds_amiga/` (harness, tools,
  Ghidra project, dumps, traces). Paths below are relative to it.

## Overview

Hand-written 68000. After boot there is no OS: the game takes over the
machine, runs its own sector loader and reads byte ranges named by file
records ("A:\title.neo"). The world is 32 "cluster" files of levels; each
level is a command list (with macros) built at run time into a 32 × 32 × 8
grid of blocks, drawn as an isometric scrolling playfield. The explorer is
a spinning top that rolls over the block surfaces under joystick control.

The NEOchrome title picture, its file name and the 3-bit palettes point to
an Atari ST origin of the data (hypothesis).

## Boot chain

1. Boot block ($0C): trackdisk `DoIO` reads $5C00 bytes from byte $D1000 to
   $30000 and `JSR $30000` (the crack intro; the research disk from
   `harness/mkboot.py` NOPs the call at $3A), then $400 bytes from $1600 to
   $40000 and `JMP $4000C`.
2. $4000C copies its own code ($40034…) to $47000 and runs it there. It
   reads disk bytes $400–$DFF (the MFM sector loader, standard AmigaDOS
   sectors, so ADF offset = block × 512) to $47168, kills interrupts, loads
   blocks $1D1+$41 to $68000 (title.neo) and shows it as the loading
   picture (palette +4 doubled, planes to $70400 with stride $1F40), then
   loads blocks $16+$1A4 to $10000 and `JMP $1001C`.
3. Main ($10000): header longs $214F0, $20CC0, $1EBC0, $3400 (+$10000 they
   name the tile, mask and piece tables and the effects module). $1001C
   installs an illegal-instruction trap to enter supervisor mode, stack
   $7FFFE, init $188A0 (A5 = $616E8, clears $4100 bytes of variables), main
   loop $18950. Its own copy of the sector loader sits at $41AF0; $10A68
   loads a file record (A0 → name, the disk byte offset in the long before
   it), $10AA6 writes one.

## Disk map (ADF byte offsets)

| Bytes | Loaded to | What |
| --- | --- | --- |
| $0–$3FF | — | boot block; block 0 is also read by $19FD4 (disk tag "SD][MASTER" at +$E; data disks get another tag) |
| $400–$DFF | $47168 | sector loader (also inside main at $41AF0) |
| $1600–$19FF | $40000 | stage 2 |
| $2C00–$373FF | $10000 | **main** ($1A4 blocks): code, texts, tables, graphics |
| $37400–$3A1FF | — | not loaded by anything found (graphics-like leftovers) |
| $3A200–$41F7F | $4EEE8 ($37B6(a5)) | **title.neo** ($177D2 "A:\title.neo", $7D80 bytes); also the loading picture |
| $42400–$45DFF | — | not loaded: leftover data, incl. the developer's level editor text at $43600–$44800 |
| $45E00–$4FA3F | $432A8 ($37BA(a5)) | **music** player + data ($9C40 bytes, record $1124C) |
| $4FE00–$5BDFF | $3400 | **effects** player + samples ($C000, record $1894A; `JMP $3400` resets it) |
| $5BE00–$7A5FF | — | format filler ("DoW…") |
| $7A600–$7AE3B | $68920 ($2576(a5)) | **sd_game.dat** ($83C, "A:\sd_game.dat": the saved-game slot; Load/Save game state) |
| $7B000–$80140 | $56CE8 ($A(a5)) | **SELECT.SWP** ($5140, "A:\SWAP\SELECT.SWP": the cluster screen's swap image; written back too) |
| $80000–$BFFFF | $4CEE8 | **32 world files** ("A:\CLUSTERS\CLUSTER.0nn", $11C8C: byte $80000 + n × $2000, $2000 bytes) |
| $C0000–$C1FFF | — | a 33rd, unscrambled "Cluster" (number word 7): never loaded ($1177A allows 0–$1F) |
| $C2000–$D0FFF | — | filler |
| $D1000–$D6BFF | $30000 | crack intro |

### World file scramble

World files 0–31 are scrambled in their first $FFC bytes ($11CF2): XOR with
a running key $24B4(a5) = $CC0C62C0 (set by $11D12 → `addi.l
#$CC0C62C0,(a4)` at $422FA). Per word: plain = w ^ d2, d3 = ror(d3, w &
15), d2 ^= d3. The rest of each slot is plain; worlds 21 and 22 use it
(records end at $101E and $16D2). `tools/clusters.py` writes the 33 plain
slots to work/clusters/; the importer unscrambles natively.

## Memory and hardware use (in game)

- Display buffers $0(a5) = $70400 and $4(a5) = $78100, 320 × 200, 4 planes
  of $1F40 bytes each; bitplane pointers written by the level-3 interrupt
  $10DAE from $63740.
- Copper: $420 (sprites, then COPJMP2); $10F3E / $10F4A small lists.
- Level grid A5+$E ($2000 bytes); auxiliary grid and object table at
  $2576(a5) (+$2000: objects, 8 bytes each). World file buffer $4CEE8.
- Joystick reader $104C0 → direction byte $10A63/$10A64 (1 up, 2 down, 4
  left, 8 right, $80 fire); keyboard $107F4 (CIA-A serial port, raw codes).

## Screens (work/ref/s*.png)

1. Title picture (title.neo), also the loading picture.
2. Attract: hint panels over the title ("Some hazards to avoid:", "These
   kill", "Lava burns", "Avoid all aliens").
3. Fire → menu box: Continue this game, Start new game, Load saved game,
   Save game state, Check performance, Wind back world, Slow Mode? Yes,
   Create Data disk.
4. Start new game → "Easy" / "Hard" cluster maps (planets on a blue field),
   "Select a cluster with joystick": hold a direction, press and release
   fire ($11EBC; $36B4(a5) = 1 easy, 2 hard).
5. A world map ("Tutorial" highlighted among planets) → fire.
6. In game: isometric scrolling playfield between two pillars (the fuel
   gauge runs up the left pillar, then the right: "Panel and pillars"),
   panel below: Clue, four small boxes (the clue boxes, $1635E), world
   name, "Spindizzy", the compass (the level's rotation, $149C6).

## Code map

| Address | What |
| --- | --- |
| $1001C | entry; $188A0 init; $18950 main loop |
| $10058 | animation stepper (lists of (ticks, frame offset)) |
| $104C0 | joystick → $10A63 |
| $107F4 | keyboard (CIA-A serial), raw codes |
| $10A68 / $10AA6 | read / write a file record |
| $10DAE | level-3 interrupt: bitplane pointers, joystick |
| $110AE / $11096 | palettes / palette select |
| $112CA | star random generator |
| $11510 | one rules tick: objects $14390, sound $11208, `jmp $24A2(a5)` |
| $11C8C | load world file n, $11CF2 unscramble |
| $11E30–$11F36 | Easy/Hard choice |
| $11F72 | start a level: $12008 header, $125DE build, objects $12362 … |
| $12052 / $126FA | find level / macro record |
| $1220A / $12328 / $12372 | tile map build |
| $125DE | level builder |
| $128CE | projection (world → map pixels, sort value) |
| $12B7C / $12BFC | explorer tick, first and second half |
| $12F7A / $12FE6 | surface contact / routine table |
| $135D2 | walls and corners |
| $14138 / $14202 | camera follow / snap |
| $14390, $143B6, $1440E | objects tick, draw, sort |
| $144BE / $148C2 | object create by kind / kind table |
| $10E1E / $10E96 | fuel costs per difficulty / fuel and gauge each frame |
| $10FC6 | pillars (sprites) and gauge |
| $112F6 | level loop (keys, ticks, camera, fuel, drawing, rebuilds) |
| $116F0 / $11E52 | world map / Easy-Hard choice |
| $12714 / $12724 | explorer placement from the header or a place word |
| $138F6 / $13910 | level jewel long / a jewel's index and presence |
| $14980 | panel, labels, world name, pillars, compass |
| $14A18 … $16350 | object kinds (see "Objects' rules") |
| $15FC0 / $15F94 | show a clue once / its text (list $15CDA) |
| $16266 / $162D8 / $162FE | conditions: test, record, re-test and rebuild |
| $16704 / $16756 | world map: angles, projection |
| $16D44 | a world's start values |
| $17694 / $1771C | title picture / attract sequence |
| $17E46 | menu |
| $18518 / $18582 | text printer / boxes |
| $18950 | main loop |
| $3400 / $432A8 | effects player module / music player module |

## Data formats

### title.neo — NEOchrome picture

128-byte header (flags 0, resolution 0, 16 palette words at +4, name text
"        .   " at +$24), then 32,000 bytes of Atari ST low resolution: per
row, per 16 pixels, four plane words. The program doubles each palette
word ($10A4E `add.w d0,d0`): ST 3-bit guns → Amiga 4-bit ($777 → $EEE).
The loader ($40078) and $176C2 de-interleave into planes of $1F40 bytes.
Decoded natively (`data::read_neo`); equal to the reference screen.

### World files ("Cluster")

$2000 bytes each (after unscrambling). "Cluster " (8 bytes, checked by
$1203A), a word ("setting": 3, 7, $B, $20 … — meaning not traced), then
records from +$A:

    id (signed word; 0 ends the list) · length (word) · length bytes

Negative ids are **macros** (found by $126FA with `$FF00 | n`), positive
ids **levels** (found by $12052). World n's name is entry n of the 13-byte
list at $119EE (" Beginners! " … "  Tutorial  " = 23 … " EasyDizzy  " =
31; Tutorial loads world file 23, $251C(a5) = 23). Worlds 29 and 30 ("Easy
World 7/8") have no records. Level ids run 1–15 with gaps.

World 22 ("Spindizzy!") is gated ($1177A: world $16 needs bits 0–21 of
$24AE(a5) = the other worlds done; world $1F tests bits 23–28).

### Level record

8-byte header, then a command list ($1206A: `lea 8(a0),a0`):

| Offset | Size | Meaning |
| --- | --- | --- |
| +0 | word | the explorer's start cell: x bits 8–12, y bits 3–7, at the cell centre (Tutorial 1 $0B79 → $0B80/$0F80, Gargoyle 1 $1078 → $1080/$0F80); bits 0–2 the level, z = level × 256 + $80 ($12724; Tutorial 1 starts at z $180 in the traces) |
| +2 | word | ? |
| +4 | word | flags → $24B8(a5); bit 15 required by $12018 and fixes the camera across; bit 12 tested at $11FE4; bit 11 turns the ice timeout off ("Ice timeout off"); bit 12 "Add reserve fuel"; bit 13 "NO fuel use"; bit 14 "Disable Mapper" (the m key) |
| +6 | word | ? |

### Commands ($125DE)

Pairs of words (first, second); first = 0 ends the list. The first word:
bits 13–15 kind, bits 8–12 x, bits 3–7 y, bits 0–2 z (cells). Positions
turn with the running rotation ($12624: per quarter turn x' = y, y' = −x
mod 32), then the running offset is added ($2488/$2489/$248A).

| Kind | Handler | Meaning |
| --- | --- | --- |
| 0, 3, 6 | rts | nothing |
| 1 ($2000) | $12542 | box: second = block word; two more bytes (across, along): low 5 bits count−1, high 3 bits signed z slope per step; swapped on odd turns; steps from $1253A/$1253E by rotation |
| 2 ($4000) | $12472 | one block, second = block word |
| 4 ($8000) | $126B0 | macro: second word = (id << 2) \| turns; bits 14–15 accumulate into the variant ($248C, `add.w` then `and #$C000`) |
| 5 ($A000) | $1269E | conditional macro: a third word, the condition ($162D8 records it, $16266 tests it) |
| 7 ($E000) | $1278C | object: second = kind << 8 \| parameter count; the parameters follow (table at $2576(a5)+$2000, 8 bytes each, at most 48) |

Block word ($12472): low byte = block type; bits 8–10 = column depth.
Types whose attribute ($30BC0 table, 256 bytes) has bit 7 clear turn with
the frame (low two bits + turns). The variant's high byte is added to the
type (`add.b $248C(a5),d0`). The cell gets the type; unless the type's low
6 bits are 0 or its attribute has bit 0, up to `depth` cells below (never
below z = 0) get `(type & $C0) + 2` (0 for empty types). Auxiliary grid:
cleared at the cell; attribute bit 2 → aux bit 5; bit 0 & bit 1 → aux bit
6; otherwise aux bit 7 at the column's bottom.

Grid: A5+$E, index x·256 + y·8 + z (32 × 32 × 8). The native builder
(`world/level_builder`) equals the original's grid in work/game.bin
(Tutorial level 1) except x 17, y 15, z 0–2 (original 01 00 00, built 42
42 7E): two objects (kinds 8 and 3) sit there and change it at run time.

### Conditions ($16266; the editor's "COND:" fields)

Passes when $361A(a5) = 0. Otherwise all tests that are switched on must
pass:

- bit 15 "test jewels": jewels left ($2566(a5) ≠ 0) fails; bit 11 "jew
  NOTgone" inverts.
- bit 13 "use push": value = bits 8–10; with bit 10: (value & 3) + 1
  compared with $3616(a5), else value with $3617(a5); unequal fails; bit 12
  "NOT pushed" inverts.
- bit 14 "use binary": ($3618 & bits 0–3) must equal bits 4–7 ("Val /
  Mask").

Level start values seen (Tutorial 1): $3616 = $3617 = $FF, $3618 = 0,
jewels 15, $361A = $FFFF. $162FE re-tests the recorded conditions and
rebuilds the level ($11FF0) when one changes: switches reshape levels.

### The developer's level editor (leftover text on the disk)

Bytes $43600–$44800 (never loaded) hold the menus of the original level
editor. Useful as names (the image's own words):

- keys: rR rotate block, +- move in list, () move 8 steps, q quit to game,
  m insert macro, e insert object, E edit object, W write level, w select
  level, S/L save/load cluster_buf, TAB header, p set player.
- level header fields: Lock x scroll, Disable Mapper, NO fuel use, Add
  reserve fuel, Ice timeout off, NO player flash, Macro ID.
- switch values: push primary/second, Red/Green/Blue/Yellow, prime.
- ramps: X/Y ramp val, X/Y Width.
- object forms: a lift (Power, Max z, Pause, Min_z, UpSpeed, DnSpeed,
  Start up, Block, Column?, Base z), Gravity/Fall Velocity, a timer (Time,
  Relative, Die if elapse, Elapse Dest), Pac Alien (Init Pause, Side
  scan%), Head/Statue, Level Sensitive Switch (Levels reqd), Give Clue,
  Starfield (#Stars, Seed), Spark Generator (Period, Proximity), Give
  message (Message #, First time only, Delay), New palette (Palette T/F).
  Exit fields: Invisible, Ignore xy/z, Restore blk, Dest level, Check
  Dest?, Keep pos&vel, Use XYZ pos.

Object kinds = the editor's "Select Object" menu ($443B6): 1 Jewel, 2 Lift,
3 Exit point, 4 Push button, 5 Binary switch, 6 Boink, 7 Timer, 8 Secret
passage, 9 Pac (alien), 10 Pyramid, 11 Heads, 12 Spark generator, 13 Clue,
14 Starfield, 15 Message, 16 Palette. All objects of all levels fall in
1–15 (Palette is unused). Native: `world/object_kind`.

## Playfield graphics

Tables named by main's header longs (+$10000): $2EBC0 pieces, $30CC0
masks, $314F0 tiles; the block attribute table $30BC0 sits between.

- **Tiles** $314F0: 16 × 8 pixels, 64 bytes: per row four plane words.
  1,030 used (highest piece reference 1029, ends $41670).
- **Masks** $30CC0: 131 × 8 words (one per row). A set bit keeps what is
  behind (blit $12170: A = mask, B = tile, C = D = destination, minterm
  $AC: D = A ? C : B).
- **Pieces** $2EBC0: 32 bytes per block type = 8 × (mask word, tile word),
  two columns × four rows. Mask = the first word's low byte (0 = opaque); a
  negative tile word = no piece.
- **Palettes** $110AE: 4 × 16 ST words, doubled; $207E(a5) picks one
  ($11096), cleared at a level's start ($11FA4); palette 0 = the title's.

### Tile map

Built by $12372 → $1220A → $12328 at $200E(a5) = $4EEE8: 64 columns × 128
rows ($400 bytes per row), 16 bytes per cell = 4 slots of (tile+1, key
word). The grid is walked x, y from 31 down, z from 7 down (front to
back); cell (x, y, z) has its first piece row at column y − x + 31, row
x + y − z + 7. Key = $C100 − $200·(62 − x − y) − $100·(7 − z) (+$100 for
aux bit 5, +$100 more for a block's top row); its low byte is replaced by
the mask number. A slot is taken unless the cell is full (an opaque piece
or 4 slots; full = bit 7 of the first byte) or already has a slot with the
same mask.

Per grid cell ($1220A), with the aux byte from the builder:

- type & $3F = 2 (column cell): one row (row +3) of type ((type & $C0) +
  2), pieces (aux & 3) × 2.
- aux bit 6: rows +1 … +4 of the type's 8 pieces, nothing more.
- otherwise: row +0 (key +$100) and rows +1 … +3.
- then, aux bit 7: a base row (+4) from type (type & $C0), pieces (aux &
  3) × 2.

Composition ($120CC): the farthest slot is copied whole, the nearer ones
blitted through their masks. A later pass sets bit 15 on the first slot of
many cells (not needed for the picture; meaning not traced).

Screen: the displayed buffer shows the map with its left edge at
$2570(a5) − 16 and top at $2572(a5). Its first (−$2570) mod 16 columns are
not drawn. The visible playfield is buffer columns 16–303 (288 pixels;
the stars' range), 150 rows above the panel; the pillars cover the rest.

## Sprites and objects

Sprite frames (main $1E000–$2D000): per row the mask (1 = sprite pixel),
then planes 0–3, each `w` words wide (w = 1: 16 px, 10 bytes per row; w =
2: 32 px, 20 bytes).

Projection $128CE (positions in 1/256 cell; z 1/256 block): x = $200 +
(y − x) >> 4, y = $48 + (x + y − z) >> 5, key = 2·((x & $FF00) + (y &
$FF00)) + (z & $FF80) + $4000. The sprite's top-left corner is that point
in map pixels after the drawer's own offset: explorer −8, −16 ($12DF4 →
$257A(a5)); objects −16, −16 or −8 (by kind).

Occlusion $1281E: for each covered map cell, layers nearer than the sprite
(layer key word (depth << 8 | mask) > sprite key) must let it through
their masks; mask 0 is all zero (hides); low byte $FF hides all. The
explorer computes this every frame; objects once at creation (mask buffer
at +$E).

Runtime objects: $25CC(a5), 31 records of $40 bytes: +0 code, +4 frame
handler, +8/+$A/+$C world x/y/z, +$E occlusion mask buffer, +$12/+$14
place, +$16 key, +$18 sort value (d7 of $128CE), +$1A frame base, +$1E
animation counter. Created by kind ($144BE, kind table $148C2: per kind a
descriptor of create routine, frame handler, then a third routine).

Draw order: each frame $1440E sorts the 32 slots by +$18 (ascending,
insertion sort on (+$18 << 16 | slot offset)) and $143B6 calls the frame
handlers in that order (records from $258C(a5); slot 1 = $25CC = the
explorer). $143B6 leaves early through $13358 at the first empty slot.

+$18 (checked in every record, `playcheck`): $7FFF from the create
($144F2) until something projects the object; a deleted slot $FFFF
($144A6). The kinds that never move project themselves once when made
($14A68 / $14A96: jewels, buttons, switches, spark generators, exits
unless invisible, flags bit 7) and again in their rotate handlers;
lifts every tick after moving ($14E22), heads every tick ($15A48);
creatures in their drawers every frame ($152CE, $156C2 + $10, $1591C +
$C), an eaten Pac in its tick ($15792, + $C); the explorer its own. The
value is the projection's ($128CE): (x & $FF00) + (y & $FF00) + (((z &
$FF80) + $4000) >> 4). Clues, messages, timers, passages and palettes
keep $7FFF. The clue search ($1635E) walks this sorted list, so "the
first" candidate is the first in drawing order.

Animation $10058: entries (ticks, frame offset) at the kind's list; +$1E
counts down (twice per frame when $24A6 = 0), +$1F = step; negative ticks
end the list. Lists start with the count at 1, so their first entry is
skipped.

### Per kind

- **The explorer** (slot 1): frames $21240, 16 × 16, four spin steps of
  $A0 (step = bits 2–3 of its counter +$1E, $1490A); flashing ($2510 set)
  alternates with +$280 (+$21 bit 0 toggled by bchg); states $24F5/$24F2 >
  0 use +$C80. Respawning after a loss it runs $12B04 (kind −3) with the
  list $12AF2 (frames +$A00/$AA0/$B40, then +$BE0 for $B frames); a level
  start makes the playing kind −4 at once. Sort offset
  +$C ($12DFE).
- **1 Jewel** ($14A18/$14BCA): type = param0 & $E; frames $25AC0 + list
  offset ($14C44 + word[type]); rows $14C24[type] + 1 (16, 16, 24, 10, 10,
  10, 10, 24); place = projection − (16, 16) for types 4/$E, − (16, 8) type
  2, − (16, 0) else; first countdown ((slot) & 7 + 1) × 4. The "type 2:
  8 up" test ($14AA4) is the jewels' own: buttons ($1608C), switches,
  sparks (both through $1607E) and exits ($15026) branch to $14AAE after
  it, so they are placed at projection − (16, 0) whatever their +$2C.
  (The port first applied the test to them too, which put the type-2 blue
  button 8 pixels too high and over an explorer standing behind it;
  checked against the original's picture in Tutorial 4 and 6.)
- **2 Lift** ($14CBC/$14EF6): params words +$2C, +$2E, +$26, byte flags
  (+$32: type & 7, bit 6 tall, bit 7 column), byte base z (& 7) << 8;
  frames $285E0 + type × $140 (16 rows) or $2A7A0 + type × $1E0 (24 rows);
  shaft rows from $280E0 below the platform: max(0, (z − base)/32 − 15);
  place projection − (16, 0). Its platform is the block family 3C/7C/BC/FC
  (the "lift top" surface); while it carries the explorer it sets contact
  $81 ($14E26).
- **3 Exit** ($14FC2/$15128): flags byte (bit 7 invisible, bit 6 gated by
  the table $2524(a5)), byte, words +$26, +$2E; frames $2B6A0, list $15140
  (8 × 4 ticks, 15 rows); drawn only once +$20 is set.
- **4 Push button** ($16048/$16112): type = param0 & 7; frames $2C000 +
  type × $208, pressed +$104 (13 rows); pressed: type 7 when word $3616 =
  $FFFF, bit 2 when (type & 3) + 1 = $3616, else type = $3617.
- **5 Binary switch** ($16154/$16240): colour = param0 & 3 (switch bit 8 >>
  colour), facing = param1 − level rotation; frames $2D040 + (colour × 8 +
  facing & 3) × $DC (11 rows), off +$370.
- **6 Boink** ($151C4), **9 Pac alien** ($1544A), **10 Pyramid** ($157F6):
  moving creatures, snapped to the cell centre (+$80); graphic set and
  heading from a parameter byte (Pac the 4th, Pyramid the 2nd, Boink the
  word in bytes 2–3; heading = byte + level rotation). Pacs also settle on
  the block surface ($12F62, a handler per block type: the explorer's
  surface code). All draw through $1467E with the creature table $146FC (24
  bytes per set = +$2A & $1C: draw mode, offset of a (dx, dy) pair from
  $146F8 that the animation list follows, frame base, offsets per
  heading): mode 1 16 × 16 and mode 0 16 × 12 (occlusion each frame like
  the explorer's), mode −1 32 × 24 ($10124; the first map column keyed
  $200 nearer for odd headings, the third for even). Sort offsets: Pac
  +$10 ($156BE), Pyramid +$C ($15918), Boink 0 ($152CE). Touching the
  explorer ($156D8/$156EC): clue 5, fuel cost $2062, flash ticks +15, leaves
  $2504 = $FFFF. Tutorial/EasyDizzy: Pacs use sets 7 and 3, Pyramids set 3,
  Boinks set 4.
- **7 Timer** ($1530C/$153C4): seconds in $360E(a5), printed as text at
  ($37A0, $37A2) = (20, 6) by $18826 (font).
- **11 Heads** ($1594A/$15A98): facing = param0 − rotation, type = param1 &
  3, condition word; frames $28EA0 + word[$159A4 + 2 (type × 4 + facing)],
  40 rows; z starts at the object's height and moves up when the condition
  holds ("Move UP"); occlusion like the lift's. Its rules (a solid block
  the explorer bounces off) are under "Objects' rules"; traced in Tutorial 1.
- **12 Spark generator** ($15C10/$15CAA): condition word, period,
  direction; frames $276E0 (+$500 when direction bit 0 is clear), list
  $15CBC, 16 rows; hidden once fired (+$20).
- **14 Starfield** ($15AFC/$15BD2): count, seed; screen-fixed stars from
  the generator $112CA (state × $F1 + $29 on the low word; result low +
  high word), x < $120, y < $A8 by rejection, seed:seed state when the seed
  is non-zero (running state restored afterwards); drawn in colour 3 where
  the star's word bits from $15B96 are background, 16 pixels in from the
  buffer edge.
- **8 Secret passage, 13 Clue, 15 Message, 16 Palette** draw nothing.

## Input: the stick byte and keys

The joystick reader $104C0 (from the CIA-B interrupt, $10DD6) builds the
direction bits and fire and writes them to $10A63 only when they differ
from the last read ($10A64). Readers:

- the explorer's steering and the dying kind's fire test (`tst.b`);
- the waits $109CC (until a key or fire) and $109E2 (the same, at most
  d7 + 1 frames): each pass the key ($109B2: $107A8, cleared when read)
  and then `bclr #7` of $10A63, so fire counts once and again only
  after the stick changes;
- the menus ($17E46): $10A63 cleared on opening ($17E5A); each pass
  $18086 takes the byte and clears it: fire takes the item, else up,
  down, left (value + 1) or right (value − 1), one action per stick
  change; then the keys (cursor up $4C / down $4D, Return, Del, +, −,
  ".", digits for number items);
- the dying kind clears it ($12CCC).

A key pressed in a level is read by the level loop once a frame
($112F6); one pressed while a wait holds stays in $107A8 and ends the
wait at its next poll (the clue's first 26 frames do not poll).

## Rules tick

The main loop runs $11510 twice per frame, or once when "Slow Mode" is on
($24A6(a5) ≠ 0; the menu's default is Yes). $11510 ticks every object
($14390), the sound ($11208), then jumps through the after-objects hook
$24A2(a5), which is the second half of the explorer's tick ($12BFC). After
the ticks, once per frame, the camera follows ($114DE → $14138).

## Explorer movement

The explorer is object slot 1 ($25CC(a5)); its tick is $12B7C, and $12BFC
finishes it once every object has ticked. Native: `play/explorer`.

Units: positions are words, 256 per cell (x and y 0 … $1FFF, z 256 per
level; a block in level n has its top at n·256). Velocities are 16.16
longs ($24D4 x, $24D8 y, $24DC z).

One tick, in order:

1. Timers: $24F4 counts down (at ≤ 0 the long $24F4 is cleared); $24F2
   (arrival flash, $13 at arrival) counts down to 0.
2. Steering $12A1E, only while the previous contact had traction or any
   low-byte flag, or the ice flag: push 1.0 ($24E4), 2.0 with fire ($24E8,
   both set at $11FB8); pushing against the motion adds the plain push once
   more, clamped at 0. Without traction both are a sixteenth (lsr.l #4).
   Stick up = −x, down = +x, left = −y, right = +y. The space bar ($11314,
   only with $24EC = 0 and a low-byte contact flag) stops dead and locks
   for $32 & $1F ticks ($24EC, counted as (lock & $1F) − 1).
3. Friction $129B6 with traction: v −= (v >> 8) × f (muls.w, so the product
   of the low word); f = the per-block byte at $12E0A of the block touched
   last tick ($24E2).
4. Gravity: vz −= 8.0 ($12BB0).
5. Move $12910: z, y, x += the rounded velocity ((v + $8000) >> 16). z +
   $1000 ≥ $2800 is a death ($131F4).
6. Floor $12958: from level min((z + $FF) >> 8, 7) down, the first cell ≠ 0
   that is not $22/$23 (those set $2510 bit 14 and are passed). $2504 = its
   level << 8 ($FF00 none), $250E = the block, $24FE = the column (x & $1F00
   | (y >> 5) & $F8; kept when x/y/z are out of range). The position inside
   the cell is turned by the block's orientation (block & 3, per quarter
   turn (u, v) → (255 − v, u), $129D2); velocities turn as (a, b) → (−b,
   a).
7. Contact $12F7A: $2512 = $2510, $2510 = 0, friction from the block, then
   the block's routine from the table $12FE6 (offset from $12F7A; each
   routine's height entry is 4 bytes before it). $24D2 keeps vz's high word
   from before contact for the landing test. With no flag set afterwards
   the wall test uses the explorer's own z ($2516 = $13222).
8. Walls $135D2 (below).
9. After the objects ($12BFC): landing $12F0A (unless bouncy, when a
   grounded/traction/lift flag is set): impact = min(vz, 0) − $24D2; impact
   − $30 > $3C kills ($131F2, clue 6), > 0 plays effect 6 at volume
   min(impact/2 + $20, $40). Grounded + traction and not on a lift saves
   the respawn point $24CC. z + $1000 < 0 is a death (clue 3, straight to
   kind −3). Then the sprite place ($12DEE) and sort value.

Contact flags ($2510): $8000 grounded, $4000 looked through a $22/$23
cell, $0200 ice, $0100 bouncy, $0080 traction, $0002 on an unheld lift
top, $0001 on a lift (set by the lift object, with $80).

### Surfaces

Routine → kind (native `play/surfaces`, read from the game's dispatch,
friction ($12E0A) and conveyor ($13570) tables at run time; blocks per
routine from the table, every $40 a copy):

| Routine | Blocks | Height (u, v inside the cell) | Contact |
| --- | --- | --- | --- |
| $12FE4 | 00 22 23 40 80 C0 | base | none |
| $1320E | 02 42 82 C2 | base | always (no height test): vz += 16, the explorer is lost (kind −2, no clue) |
| $13298 (+$132B8 … $13302) | most | base (+$40/$80/$C0/$100/$180/$200) | land: vz high word = 0 (the fraction stays), flags $8080, z = top |
| $1327E | 38-3B 64 65 94-97 | base | slippery (the port's name): land with $8000 only (no steering, no friction) |
| $13228 | 2C-33 … | base | land; if less than $20 below the top, the pad sets $3616: pad 0 → word $FFFF, 1-4 → word 0-3, 5-7 → byte (n & 3) + 1; effect $15 + n on change |
| $13312 | 04-07 … | 255 − u | slope: v′ = v turned to the block, v′x += 1.0; follow = −v′x; if follow − vz < 0 only z = top, else vz = follow, v = v′ turned back, flags $8080 |
| $1335E | 08-0B … | 2 (255 − u) | the same with 2.0 and follow × 2 |
| $13394 | 0C-0F … | u + v < 256: base + $100, else 511 − u − v | flat / diagonal |
| $133E2 | 28-2B … | 511 − u − v | diagonal: v′x, v′y += $16DB6, follow = −(v′x + v′y) / 2 |
| $133F8 | 10-13 … | u + v < 256: 255 − u − v, else base | diagonal / flat |
| $13464 | 18-1B … | 255 − min(u, v) | slope along u, or (u > v) along v with the orientation − 1 |
| $13486 | 1C-1F … | 255 − max(u, v) | the same, faces swapped |
| $1349A | 20 21 E1 | base | ice (clues 7 and 10: "trapped on Ice", "On Ice for too long"): not after a lift tick; 250 ticks ($2514) unless level flag bit 11 (clue 10); flags $8200; standing still (both rounded speeds 0) kills (clue 7) |
| $13504 | 03 43 83 C3 | base | bouncy: flags $0180; falling faster than 8 reverses vz (effect $24) |
| $1354C | 24-27 | base | conveyor: vx, vy high words += table $13570 (2 per tick by orientation), land |
| $13584 | 34-37 … | base | deadly: flags 0, clue 8, kind −2, no fuel cost |
| $1359C | 3C 7C BC FC | base | lift top (the lift object's platform block): not after a lift tick; flag $0002, no landing; the second tick in a row kills (clue 6) |

### Walls ($135D2)

The explorer's footprint is ±$5A around its position. x: an inside part <
$5A tests the cell at x − 1, > $A6 the cell at x + 1; y: < $5A tests y − 1,
≥ $A6 tests y + 1. A neighbour is a wall when its top (looked up from level
min((z + $268) >> 8, 7) down, height at the far edge point) less $3C (and
less $1E more while any contact flag is set) is above the current top at
the near edge point ($13808). A wall puts the inside part at $5A/$A6 and,
when moving toward it, reverses and halves the speed ((−v) >> 1). When
both axes are near an edge with no wall, the diagonal cell is tested; a
corner pushes out along one axis (chosen by the inside parts) to $5A/$A5
with −(v >> 1).

### Deaths and respawn

$131F4 takes the death cost $2060(a5) from the fuel change $206C(a5), clears the flags, shows
the clue once ($15FC0, bit n of $3610) and turns the explorer into kind −2.
The tick goes on after a death (the routines return into it).

- The dying kind (out of fuel $12C9A: clue 1, effect 4 on channel 1; or
  $2492 from a timer, $12CB0): tick $12CE8, frame $12D14; +$2C = $C8,
  +$1E = $100, the fire latch cleared. Each tick +$2C − 1: negative →
  game over; below $A0 fire ends it. Frame: the explorer's place
  ($257A), frames from the list $12D56 ($C ticks each, the last $FF),
  and unless $249E "Game  Over" ($12D68: column 15, row $50).
- Kind −2, lost ($12D96): effect 2, $18 ticks + 1, frames from list
  $12DB8.
- Kind −3, arriving ($12AC6; create $12B0C): back at the respawn point at
  the cell's centre ($80, $80, z fraction $40), velocities 0, $10 fuel
  taken ($12B18 `subi.w #$10,$206C`), effect 3; 12 ticks with list $12AF2.
- Then the explorer kind again ($12AD2) with $24F2 = $13 (the special
  frame set). Both other kinds reset $24F4 and $24F2 ($12B2E).

Clues seen: 3 fell out of the world, 5 alien, 6 hard landing / unheld lift
top, 7 standing still on ice, 8 deadly top, 10 too long on ice. Their
texts are in the list at $15CDA (see "Clues and messages").

## Camera ($14138, $14202)

Once per frame after the ticks the scroll position ($2570 across, $2572
down, in map pixels; the 320-pixel buffer starts at $2570 − 16) eases
toward the explorer's sprite corner ($257A/$257C from $12DEE):

- across: target $2570 = corner x − $88; down: target $2572 = corner y +
  (z asr 7) − $50.
- step = distance asr 3, clamped to ±8 across and ±4 down; a negative step
  is one smaller (+1).
- ranges 0–$2E0 and 0–$1A7. Level flags bit 15 ($24B8, tested as `btst #7`
  on the byte) fixes it across.

At a level start ($11FB4) $14202 places it at once: across in 16-pixel
steps ((x − $88) asr 4, 0–$2E, or $17 with bit 15), down corner y − $58.
$1428C then redraws the strips the scroll uncovered (the port draws any
window). Native: `view/camera`.

## Game flow (the main loop $18950)

1. Boot, then the attract sequence ($17694 / $1771C) until fire or a
   direction; then the menu ($18A78). The menu times out back to the
   attract after 800 passes (d7 = $64 << 3).
2. Menu result ≥ 0 → the world map ($116F0). With no cluster chosen yet
   ($24A8 = 0) the Easy/Hard choice comes first ($11E52), then $11E30
   sets the cluster list and its world count ($36B2: Easy $12 = 9 worlds,
   Hard $2E = 23).
3. The world map loop ends on fire; $1177A refuses the gated worlds
   (world $16 needs bits 0-21 of $24AE clear, world $1F bits 23-28);
   $11C8C loads the world file. $24AE starts as the cluster's mask
   ($11E10: Hard $007FFFFF, Easy $9F800000): a set bit = a world still to do.
4. $14980 draws the panel and builds the pillars; $189CA sets sprite
   colours 16-31 to palette 0 and colour 24 to black; $11F72 starts
   level 1; the level loop $112F6 runs until $2498 is cleared.
5. After the loop ($18A18): game over ($2490) → $18C14 (the reserve fuel
   offer: "Use reserve fuel" / "Retry this world" / "Abandon this world",
   asked only with at least $20 reserve and fuel 0); level 0 → the world
   is done: $11838 shows the map with the done planet breaking up (see
   "After a world and at a game over"), $16B8C records the world
   (number, fuel, reserve, jewels found and taken in lists at $36B6,
   $36E4, $3712, $3740), $16D44 recomputes $24AE from those records; all done → the
   ending ($17CBE); else tune 2 ($18A66) and $18C90 (statistics, then
   "Next world" / "Save game") and the world map again. Any other level
   → $11F72 for it.

The level loop's order ($112F6 … $1150E): one key ($109B2) acted on,
then the rules ticks ($11510, twice without slow mode), the sort
($1440E), the camera ($14138), $1428C, the fuel ($10E96), the clue
search ($1635E), the map draw ($13AE2), the frame handlers ($143B6), the
buffer swap ($112E0) and frame wait, the condition re-test ($162FE),
and only then the test of $2498: a frame handler run on the frame an
exit fired (the reserve message, the timer's end) still runs.

Menu results ($18AA2 …): 0 "Continue" and 6 "Slow Mode?" → the map; 1
"Start new game" ($18AB4: no cluster, clues $FFFFFFFF, no worlds done);
2 "Load saved game" ($16D28): on success the statistics ($18AF6), else
the message $16BCF; then fire and the menu again; 3 "Save game state"
($16CBE); 4 "Check performance" (statistics, fire, menu); 5 "Wind back
world" ($18B10: $36B0 − 2 when worlds are done, then $16D44, which
recomputes the worlds left from the records kept, and the map); the
last, "Create Data disk" ($10AB0), writes a data disk (left out: the
shell's saves do that). After a world, "Save game" ($18CC6)
saves and shows its menu again; only "Next world" goes on.

$16D44 (a world's start): switches $3616 = $FFFF, $3618 = 0, rotation 0,
$24C0 = 0, the per-level jewel longs all $80000000 ($138E0), $2564 = 0,
fuel and reserve from the last done world's record (a new game: fuel $80,
reserve 0), the fuel costs ($10E1E), level 1. Clues ($3610) are reset
only by "Start new game".

Keys in the level loop ($112F6, through $109B2): space the brake; "["
($1133C: until fire) or "p" ($11338: the palette dimmed first, $11074,
restored after, $1106E) pause; "m" the overview map ($1135A, not with
header bit 14; see "Overview map"); "r"/"t"
turn the level a quarter (not with header bit 15: rotation $248B,
rebuild, objects' rotate handlers $143DE, compass, camera); "F" toggles
slow mode; "c" centres the camera; with the cheat flag $249C, "l" goes to
the next level; "B" toggles $24A0; any other key ends the level loop with
the key in $2494 ($65 = 'e' → $18D36).

### Overview map ($1135A)

$249E is set (the dying kind's "Game  Over" and the timer's seconds are
not printed meanwhile) and the box $1134A (style $30 at column 4, row 8,
$20 columns, $96 rows) printed over the shown screen. The camera words
$256C … $2572 are saved, then for the rows d5 = $34, $22, $10, 0 (top
in 8-row steps, so scroll y $1A0, $110, $80, 0) and the columns d4 = 3 …
0 (scroll x d4 × $100): the camera set, $1428C, the map drawn ($13AE2)
and the frame handlers run ($143B6, so every object's frame steps 16
times and the sprite blits may clear the generator), then $18DC6
squeezes the hidden buffer into the shown one: per output byte four
source bytes through the table $3FC4 twice ($18D9A: bits 7, 5, 3, 1 into
both nibbles), so every fourth pixel from the buffer's pixel 16, every
fourth row ($A0 bytes), 64 × 42 pixels at x 32 + 64 × d4, y 8 + 2 × d5.
Each tile overlaps the one below it by 6 rows (drawn bottom first).
Then the camera back, $249E cleared, fire ($109CC), and the loop goes on
with the next key test ($11300).

## Turning the level

r (+1) and t (−1) in the level loop ($11402), not with header bit 15:
$248B += direction; the level is built again ($11FF0, the explorer kept
where it is) from the rotation: $125B0 starts each build turned by $248B
and moved by the table $125A8 (none, (0, −1), (−1, −1), (−1, 0) cells),
so a cell (x, y) lands on (y, 31 − x) per turn; then $143DE calls every
object's rotate handler (desc + 8) with (−direction) & 3 turns of (x, y)
→ (y ^ $1F00, x) ($12DDE): binary switches turn their facing, boinks,
Pacs and pyramids their heading (Pacs also their target, start cell and
step, pyramids target and step), secret passages open their cells again
at the new place; the explorer ($12C64) turns its position and respawn
point with $1FFF and its speed ((vx, vy) → (−vy, vx)); the compass is
redrawn and the camera snaps. Traced (`play_turn`: Tutorial 1, r, t, r,
r; grid, objects and explorer equal).

Other keys: F toggles slow mode, c snaps the camera ($14202), p dims the
palette (each ST word less its ror by 1 & $777, $11074) until fire, [
waits for fire without dimming, m shows the overview map ($1135A: the
level drawn in strips through $18DC6; not rebuilt).

## Fuel

Fuel is a 16.16 long ($2064), whole part 0-$100 (both pillars full);
changes are added to $206C (word adds to its high word) and taken in once
a frame ($10E96): fuel = fuel + $206C − drain ($2068); below 0 → 0 and
game over ($2490); above $FFFFFF → $1000000. With header bit 12 ("Add
reserve fuel") the jewels' fuel of the frame ($2070) also goes an eighth
into the reserve $2074. The gauge ($2072) steps one row a frame toward
the fuel's whole part.

Per difficulty step $207C (= $36B0, worlds done × 2) $10E1E sets the drain
$207C × $11 + $280 per frame (0 at step 0, and 0 with header bit 13 "NO
fuel use") and four words from $10DFE + 8 × (0 / <6 / <16 / more): the
first-visit bonus $205C, $205E (not read), the death cost $2060 and the
alien's $2062. At step 0: $20, 2, 2, $20, no drain: the Tutorial costs
nothing to play. A death costs $2060 ($131F4) and the respawn $10 more.
The explorer's tick ($12B7C) turns into the dying kind ($12C9A: clue 1,
200 ticks, fire ends it after $60) when the fuel's whole part is ≤ 0, or
at once when the timer flag $2492 is set.

## Level start and travel

$11F72: the object slots are cleared; the explorer is created first
(kind −4, slot 1: $12B2E clears $24F4/$24F6, sets $24F2 = $13, spin
counter $100: it starts playing at once; kind −3, the arrival with its
$10 cost, is only the respawn); $138F6 takes the level's jewel long from
$2524 + 4 × (level − 1) into $2566; bit 31 set (never visited) adds the
first-visit bonus; palette 0; the header ($12008: flags $24B8; a level
with bit 15 clears the rotation); the build ($125DE), which first places
the explorer ($12714); the tile map, then the objects ($127CA, in the
level's order, slots 2-31: more are not made), bit 31 of $2566 cleared;
camera snap; pushes $24E4 = 1.0, $24E8 = 2.0; the panel's compass; fuel
costs; $2078 = reserve; with header bit 12 the kind −9 object, whose
frame handler prints the bonus-level notice ($13888: "Bonus Level / Collect
jewels before the time runs out / Do not fall off") and waits for fire
($109CC) on the level's first frame, then deletes itself.

Place words ($24C0, exits' +$2E): bit 15 "Use XYZ pos" (else the header's
first word, the start cell), bit 14 keep the explorer's velocity (and
from $11FF0, a rebuild: keep its place), bit 13 z on the level itself
(else + $80); x bits 8-12, y 3-7, z 0-2, turned with the rotation like
commands. The respawn point $24CC is not reset by a level change.

The level loop ends when an exit or timer sets the next level; the level
left keeps its jewel long ($150E8). A level whose long is 0 (visited, no
jewels left) removes the exits with flag bit 6 that lead to it. Rebuilds:
$162FE re-tests the conditions recorded while building each frame and
rebuilds the level where the explorer is when one changed (objects stay;
their rotate handlers run again, which re-opens secret passages).

## Objects' rules

Descriptors at $148C2 + 4 × kind (negative kinds before it): create,
frame handler, rotate (desc + 8), tick (desc + 12). Each rules tick
$14390 runs slots $258C upward (slot 0 unused, the explorer slot 1);
frame handlers run while drawing ($143B6, after the camera), and an
object's animation ($10058) steps only while its sprite is on the
playfield: its corner less $2570 within −$20 … $120 ($14BEA), its rows
reaching rows 0-$A6 ($1072A). Positions are the cell's origin (x, y ×
256), z the level × 256. "Same cell" ($14B9C): the object's x/y with low
byte $FF minus the explorer's must have a zero high byte; the height test
is (object z + extra − explorer z) unsigned ≤ reach.

- **Jewel** ($14A18, tick $14B04): the level's jewel index counts every
  jewel placed ($256A); on a first visit each sets its bit in $2566 and
  counts into $2564, otherwise a jewel whose bit is clear is not made.
  Taken from its z − $180 to z + reach (reach per type, $14B7C: $20,
  $100, $180, $80 …): its bit cleared, fuel + $14B5C[type] ($C, $10,
  $18, 6 …, also into $2070), $24F4 + $14B6C[type], $2565 + 1, sound.
- **Lift** ($14CBC, tick $14D46): words +$2C (bytes: bottom level | down
  pause index << 4, top level | up pause index << 4), +$2E (bit 15 going
  down; byte $2E & $7F down speed, byte $2F & $7F up speed), condition
  +$26, flags +$32 (type, bit 6 tall, bit 7 column), base. Moves while
  its condition holds and its pause (+$30) has run out: up by the up
  speed until its z's high byte reaches the top level (no clamp), down
  to the bottom level (clamped; below 0 → 0); then the pause from
  $14DAE (0, 1, 2, 3, 4, 8, $C, $10, $20, $40, $60, $80, $100, $140,
  $200, $400) and the direction turns. The explorer in its cell: on
  top within $80 below the platform (z + 8): held there (contact $81,
  friction 4, at least the lift's speed upward); lower but within $1FF
  (tall: $2FF; column: any depth) it is a solid side ($14EAE: the move
  that crossed into it undone, speed −v/2).
- **Exit** ($14FC2, tick $15044): flags byte (bit 7 invisible, 6 gone
  with its destination emptied, 5 ignore xy, 4 ignore z, 3 back to the
  respawn point), destination & 15, condition +$26, place +$2E. Shown
  while its condition holds; taken in the explorer's cell with (z + $40
  − explorer z) & $FE00 = 0. To its own level: the place with bit 15
  forced, at once (camera snap). Elsewhere: the next level, place in
  $24C0; with header bit 12 +$21 is set ($150DE) and its frame ($15186,
  also the timer's through $153D0) shows, unless in the editor's test
  mode ($24BE): the box and " Reserve fuel collected   " ($15162), then
  the reserve this level added (($2074 − $2078) × 100 / 256, $16EF8)
  with "%"; with $37B4 clear (as seen) no 26-frame hold, then $109E2:
  until fire, a key or d7 + 1 frames, d7 being what the printer left
  ($37A6 + the row: 15 + the text's row); $112E0, and the object goes.
  Seen at the end of the Tutorial (level 15 is a bonus level).
- **Push button** ($16048, tick $1609C): the explorer resting exactly at
  its z in its cell: type 7 → word $3616 = $FFFF; types 4-6 → byte $3616
  = (type & 3) + 1; types 0-3 → word $3616 = type.
- **Binary switch** ($16154, tick $161E0): colour → bit 8 >> colour of
  $3618; the explorer exactly at its z in its cell: the half of the cell
  along its facing (bit 0 swaps x/y, bit 1 inverts) sets the bit (≥ $80)
  or clears it.
- **Heads** ($1594A, tick $159DC): facing byte (bit 7 rises, else sinks),
  type, condition. Each tick its offset +$2E moves 4 toward $200 while
  the condition holds, toward 0 otherwise; z = base + $300 ± offset. A
  solid block one cell wide ($1B4 with the explorer's $5A each side) from
  $468 to $168 below its z (push as the lift's sides). In Tutorial 1 it
  blocks the way to the level-8 temple until the level's jewels are gone.
- **Secret passage** ($15404; rotate $15430): sets the grid after the tile
  map was built (so the wall still shows): parameter bit 7 → count + 1
  cells cleared upward; else the bottom cell becomes block 1 and `count`
  cells above it are cleared.
- **Timer** ($1530C, tick $15346): seconds × 256 + fraction in $360E;
  −8 a tick; at zero: "Die if elapse" (bit 6) → clue 4 and $2492 (the
  explorer dies); else "Elapse Dest" (bits 0-3) like an exit with place
  0. Creating it: absolute (fraction $F0) or, bit 7, added. Its frame
  prints the seconds right-aligned at column 20, row 6 (not with $249E,
  the overview map). "Die if elapse" also makes its frame handler
  $153EA ($1537C): the dying kind's countdown $12CE8 on the timer's own
  record, each frame its word +$2C (flags << 8 | seconds) less 1;
  negative → game over ($2490 set, $2498 cleared); below $A0 fire
  (latched) ends it too; then "TIME OUT" ($153F6, column 16, row 6, the
  small size). With "Relative" (bit 7 of the flags) the word is negative
  at once: Roller-ama 4's timer ($C306) ends the level loop on the frame
  it runs out (the record stops at tick 641); without it the explorer's
  200-tick countdown comes first. With $24FC set (the reserve used, and
  the explorer not dead since), $12CE8 goes to $12B58: $12B2E makes the
  timer's record the explorer's (its timers cleared, $24F2 = $13, +$1A
  the explorer's frames, +$18 = $2580), the table $12B70 gives it the
  explorer's frame $1490A and tick $12B7C, which runs at once, sees
  $2492 and makes it the dying kind ($12CB0: +$2C = $C8, the stick byte
  and $24FC cleared, then one count). The real explorer dies on its next
  tick; the remade record, a pass ahead, ends the game first, and no
  "TIME OUT" is printed.
- **Palette** (kind 16; create $1110E, tick $11124): +$26 a condition,
  +$1A/+$1B two palette numbers; each tick the first while the condition
  holds, else (or with no condition) the second; when it differs from
  $207E it is set and loaded at once ($11096: colours 0-15 from $110AE +
  (n & 3) × 32; the sprites' 16-31 stay palette 0). No fades. $207E is 0
  at a level's start ($11FA4). No level has one.
- **Message** ($16002, tick $16028): after `delay` + 1 ticks shows message
  p0 & $1F (once a game, or every time with bit 7); not made in the
  editor's test mode ($24BE).
- **Clue** ($16340): a word for the panel (0 → $361C); no tick.
- **Boink** ($151C4, tick $15244): a ball bouncing in its column. Create:
  +$2E = gravity (p0, signed), +$2C = speed (p1 signed × 4), +$30 = its
  column's grid index, x/y the cell's centre, +$2A = set (word p2 p3),
  heading +$28 = set + rotation. Each tick: cooldown +$32 − 1; speed +=
  gravity; z += speed (below 0: z = 0, speed reversed); falling and z ≤
  $700: a block in the column's level (z + $FF) >> 8 bounces it; rising
  and z ≤ $500: a block three levels above z >> 8 bounces it (the speed
  reversed and applied again; a sound when the cooldown has run out,
  which then restarts at 8). Touch ($1481C): clue 2 ("crushed"), $30
  fuel, sounds; it turns into a popping animation ($14800: tick $14810
  deletes it once +$1E goes negative: the pop list's first entry has a
  tick byte ≥ $80, read when the list wraps). Creatures' animations step
  before their drawer's screen test ($10092, $147CA, $10124), so they
  animate off screen too. Rebuilt; traced in MisDirection 4 (600 ticks,
  three boinks) and Roller-ama 4; a touch not traced yet.
- **Touch test** ($1481C, all creatures): per set (+$2A & $1C) in the
  table at $146FC (24 bytes): +$12 half-width r, +$14 z offset, +$16
  z range: touched when (x − ex + r) and (y − ey + r) are below 2r
  (unsigned) and (z − ez + offset) below the range.
- **Random generator** ($112CA): word $2096 × $F1 (mulu), $29 added to
  the low word only, the long kept at $2094; returns low + high word.
  Starfields with seed 0 draw from it at a level's start ($15B48); with a
  seed they use (seed + rotation) twice and restore it. Its state is
  **overwritten by sprite drawing** (an original bug, rebuilt): sprites
  are drawn through an occlusion mask built at $2098 (the explorer and
  16-pixel creatures, $100E0: two columns, stride 4; 32-pixel creatures
  likewise). The drawing blit ($102F6, $101FE) writes D = frame mask AND
  buffer with the first word of each row forced to 0, starting one word
  early when the sprite needs no shift (its corner x on a 16-pixel
  boundary): with its y on an 8-row band and its top not clipped that
  word is $2096, the generator's state word, which becomes 0. So each
  frame the state's low word is cleared when any drawn sprite (on screen:
  corner − scroll + $10 within 0 … $130, + $20 within 0 … $140 for 32
  pixels; rows 0 … $A6) has (x & 15) = 0 and (y & 7) = 0. It decides the
  Pacs' random turns.
- **Pac alien** ($1544A, tick $15526; rebuilt): start cell kept
  (+$34/+$35), speed (p0, +$30), turn chance ("Side scan%", p2, +$31),
  initial pause (p1, +$32/+$33), set (p3); heading +$28; it settles on
  the block surface below ($12F62, the explorer's surface code) and keeps
  a step vector (speed turned by the heading, +$2C/+$2E). Moving cell to
  cell along its heading (offsets $15610: +$100, +8, −$100, −8); while
  its next position stays within $100 of the target (+$36/+$38) it moves,
  else at the cell centre ($1556A) it tests, from its own cell, the level's
  edge ($155EC), headroom over the next cell (z < $5FF: levels +1 and +2
  empty or $22/$23; < $6FF: +1; higher: none), and the next cell at its
  level: attribute bit 3 blocks ($135C0), or with the set's bit 7 any
  other block type than its start cell's ($157F0). Open: it draws a random
  number and keeps its heading unless the low byte is below its chance
  (+$31), then it turns back first. A turn ($15644) negates the shared
  toggle at $15618 (in the code; +2 at boot), tries heading + toggle, then
  the opposite side, else the reverse of that, snaps to its cell centre
  and takes a new step vector. On the keep path it does not move that
  tick. Touch: unless the explorer flashes from a
  jewel ($24F4 ≠ 0, then it is eaten: $1571E: $24F6 + 1 up to 5, its
  score frame from $25930, it rises $20 a tick for 32 ticks, then is
  made again at its start with a pause of $7F; its frame $157DC skips
  the first frame (+$20 = $FFFF, bclr #7) and then draws the 16 × 8
  picture $25930 + (n − 1) × $50 through the 16-pixel masked drawer
  $100A2 at the projection less 8 across and $10 up, with no animation
  step), clue 5, fuel $2062,
  $24F2 + 15; it pops and is made again at its start ($157A4, pause $7F).
- **Pyramid** ($157F6, tick $15876): speed p0, set p1, heading = set +
  rotation; it walks like a Pac without turning at random: at a cell
  centre it reverses (heading ^ 2, snapped to its centre) when the next
  cell's block at its level has attribute bit 3 or the cell above it is
  filled; else it goes on (not moving that tick). Touch: clue 5, fuel
  $2062 + $10, it pops and is gone ($14800).
- **Spark generator** ($15C10, tick $15C70): condition +$26 (p0 p1),
  period p2 (+$2C, only the placement quirk reads it), direction p3
  (+$28: frames $276E0, + $500 when bit 0 is clear). Each tick it is off
  (+$20 set, not drawn) while its condition holds, the switches' way to
  disable it; otherwise it sparks (a sound, its list $15CBC stepping on
  screen) and the explorer in its cell up to $180 below it gets clue 9
  ("You need to disable sparks") and loses $20 fuel every tick, which
  empties the tank within a few ticks (then clue 1 and the dying kind).

## Clues and messages

$15FC0 shows clue n once a game (bit n of $3610), $15FEE every time; the
text comes from the list at $15CDA (number byte, string) and is printed
over the playfield; the game waits 26 frames, then up to 126 more or
until fire ($109E2). Texts (abridged): 1 the game ends if your fuel runs
out; 2 getting crushed; 3 falling off loses fuel; 4 complete this level
before the time runs out; 5 avoid aliens; 6 landing too hard; 7 trapped
on ice, self destructing; 8 avoid these blocks; 9 disable sparks; 10 on
ice for too long; 16/17 the timer level's notes; 24 "This is the switch
state you need" (Tutorial 8); 26 falling off is not fatal; 31 "Well Done!
The planet is disintegrating".

$1635E (every 16 frames, counter $36A0) looks, in drawing order, for the
first object with a word +$26 (lifts, exits, heads, sparks, clues) within
$380 of the explorer that is not the word shown ($36A2) and shows it;
finding none it clears the boxes, so a clue blinks while the explorer
stays near. $163BE: the big box (column 4, row 180, 13 rows of a 32-pixel
frame) shows a push button for bit 13 ($2C000 + value × $208, + $104
unless "NOT pushed"), a jewel ($25AD4) for bit 15, else nothing; the four
small boxes (from $1B12, offsets and pictures in the table at $164AE,
pictures at $418F0, + $40 lit) show the binary mask bits 0-3, lit when
the value bit (+4) is set. Rebuilt; equal in every record.

## After a world and at a game over

$18C90: the screen cleared, palette 0, the statistics ($16DA4, also the
menu's "Check performance"): "You have not started" ($16DB4), or "You
have completed n World(s)." ($16DD3 …), a box ($16E28: x 2, y $20, width
$24, style $3B, height count × 9 + 2) listing up to 16 worlds done: the
name at column 2, the fuel then as a percentage (× 100 / 256, $18818:
"%" then the digits leftward) ending at column $12, "taken of found
jewels." from column $14; then "Reserve fuel level" and its percentage.
Fire ($109CC), then the menu "Next world" / "Save game" ($18CD2 box,
$18CDC items) over it.

$18C14 at a game over: with at least $20 reserve and the fuel at 0 the
menu at $18C2E ("Use reserve fuel", "Retry this world", "Abandon this
world"), else the one at $18C4A without the first item. Use reserve: the
fuel becomes the reserve, which is emptied, $24FC set, and the level
loop goes on ($189E0): the dying explorer takes up its kind again where
it is ($12CE8 → $12B58). Retry: $16D44 and level 1. Abandon: the title.

A locked world ($1177A) prints "You cannot enter this world yet"
($1180A) over the map and waits for fire.

**The planet breaking up** ($11838, before $16B8C records the world):
$24AC = the world just done, the map made again ($1169A: the speeds
$166F8/$166FC cleared, the angles $166EC … $166F4 never reset, so the
map keeps its turn from visit to visit and also turns the Easy/Hard
thumbnails); $119B4 makes that world's planet kind −7: its create
($16A86) asks for effect 0 on channel 0 at volume $40 (the effects
player is still hooked from the level), +$1E = $0400 (4 ticks, entry 0).
Then $40 − $36B2 + 1 frames (Easy 47, Hard 19) of $11686 (box, angles,
draw), the frame handlers and the joystick ($168BE), no planet chosen
(no name, no picture cycling). Its draw ($16ADE): with +$1F (entry) < 4
the planet's own picture ($16AC8, its size from d6 as the drawing loop
leaves it), then $10058 over the list $16B38 once, twice more for $36B2
≥ $10 and ≥ $20 (Easy 2, Hard 3 steps a frame), each step counting +$1E
down once (twice without slow mode); the list: nine pictures 4 ticks
each at SELECT.SWP + $35C0 + n × $280 (32 rows, drawn at the planet's x,
y − 4), then ($FF, the last again), then $FFFF (back to the first). Its
frame ($16AA6) removes it once +$1E is negative, so after the ninth.

**The endings** ($17CBE, every world of the cluster done): the text over
the last map frame ($18518 into the shown buffer): Easy $17D74 (a box,
"Well done!" / "Now try the Hard game."), Hard $17DA7 ("Well done!" at
column 15, row 30); effect 1 on channels 0-2 at $40 ($17CA0); 151
frames ($10A1E with $96). Easy: 51 frames more, then fire ($109CC).
Hard: $17DB8 builds a pixel-doubling table at $40FC, the three effects
again, then 48 frames: from $17C62 a byte per frame (bit 7 clear: effect
1 on that channel; frames 24, 31, 36), $16B62 draws picture d0 + 4 of
the break-up series (d0 = 0-7 in turn; SELECT.SWP + $3840 + (d0 + 3) ×
$280) at x 144, y 84 of the shown buffer, $17DFC doubles that buffer's
centre (bytes 10-29 of rows 50-149, each byte through the table, each
row twice) into the hidden one, which is then shown ($112E0): the
picture zooms in on itself frame after frame; then 9 frames of the
doubling alone. The eighth picture lies past the file's end ($5140
bytes): it comes from whatever the level left in that RAM. Then
$36B0, $36B4 and $24A8 cleared (the clues kept), tune 1 ($11250), and
the main loop's title ($18962), which keeps it playing.

## Text printer ($18518)

All texts are byte strings in main with control codes, printed by one
routine: the cursor is a byte column (8 pixels, wrapping at $28) and a
pixel row. Glyphs: 64 bytes each (16 rows × 4 plane bytes); codes $01-$07
pick the bank $1B400 + (code × 4 − 4) × $680 as a signed word (the code
is still × 4 from the jump table): 1 the usual font, 2 the orange one at
$1CE00, higher codes point past the fonts. Normal size ($00/$10): 16
rows, lines of 16; small ($11, the boot default): 8 of the rows (every
other one) plus the last, lines of 9. Transparent ($12, default) keeps
what is behind where a mask made at boot from the usual bank ($18282,
$677E8) says colour 0, whatever bank is chosen; opaque ($13) draws the
glyph's colour 0 too. Other codes: $08 back, $09/$0C skip, $0A down, $0B
up, $0D new line, $0E n right, $0F n down, $14 n line height, $1B n
column offset until the string ends, $1C/$1D the shown/hidden buffer,
$1E s x y w h a box, $1F x y cursor. After a box the cursor is the box's
corner (d4/d5 are not restored). Numbers: $187F4 left to right, $18826
right-aligned (digits leftward from the cursor).

Boxes ($18582): fill colour = style & 15 in words from the even column
at or after x for h rows (none if h is 1, none if fewer than two words
of width); border colour = style >> 4: lines above and below, a pixel
column each side (not when x or y is 0 or the box reaches the edge).

## Panel and pillars

$14980: SELECT.SWP's first $1400 bytes are the panel (32 rows from row
168, plane words of each 16 pixels in turn), then the string at $149A8
(small size: " Spindizzy" at column 18, row 190 in bank 1; "Clue" at 4,
170; then bank 2, normal size, cursor 18, 172) and the world's name
($119D8, 13 bytes each at $119EE). The compass ($149C6) is 32 × 10 at byte
column 32, row 179, frames $41670 + rotation × $A0.

The pillars ($10FC6) are two pairs of attached hardware sprites at
HSTART $80 and $1B0 (against DIWSTRT $81: screen x −1 and 303), 168 rows
from $1E800: 8 top rows, 128 gauge rows ($1E8E0 empty, $1E8F0 full,
copied raw), 20 rows from $1E840, 12 from $1E880. Converted rows ($10F96)
turn colour 0 into colour 8 (sprite colour 24, set black). The gauge
fills from row $87 upward on the left pillar (fuel 0-$7F), then the right.

## Front end

- **Attract** ($1771C): the title (title.neo, its own palette) with
  "Copyright Paul Shirley 1990" ($17889) for 500 frames, then five pages
  of 500 frames each over the title: text, block pictures ($1741C: pairs
  of buffer offset and block word, drawn by $17242 with the tiles OR'ed),
  and sprites ($179DC: 12-byte entries; kinds −10 … −13 copy a frame
  opaquely into the shown buffer every period ticks, frames from offset
  lists at $17A5E). Pages: $17904 GERALD ($17942, sprites $17B10),
  $1777C hazards ($17312, blocks $172DE, sprites $17ABA), $178AE credits
  ($177DF), $1779C switches ($173DC, $17300, $17B98), $177B6 ($1736E,
  $17B5A). Fire or a direction ends it.
- **Menu** ($17E46): a list of records (length word, string with its own
  cursor position; the two words before the next record name a variable
  and its kind: bit 14 a bit, bit 13 a bit field, else a byte/word/long
  value). Up/down choose, fire takes it, left/right change the value
  ("Slow Mode?" is bit 0 of $24A6, shown "Yes"/"No "). The chosen item
  flashes orange (counter $37B2 bit 6; the counter moves with every item
  drawn). The loop is not tied to the frame rate.
- **Easy/Hard** ($11E52): the labels and boxes first, then each cluster
  drawn into the hidden buffer and copied at 2/3 size ($11D2E/$11D88:
  two of every three pixels, two of every three rows) into its box;
  holding left/right inverts planes 2-3 of a box; fire pressed and
  released with the direction still held chooses ($36B4 = 1 / 2).
- **World map** ($116F0): the cluster list (count − 1, then x, y, z,
  picture, world) as objects (kind −1; done worlds not made; the world
  just done is kind −7, disintegrating). Each frame the box ($1693C,
  colour 11), three rotations from angles over the sine table $164EC
  ($16704/$16756: each (a·sin ± b·cos) × 4 >> 16), shadows ($1688A: plane
  0 cleared under SELECT.SWP + $1928 at x + $98 − (z + $80) / 4, y + $5C),
  then the planets far to near in one of five sizes by depth ($16846,
  table $16A2C), the nearest one cycling its pictures every other 8
  frames; its name at column 14, row 8. The joystick pushes two angle
  speeds by 1.0 a frame, losing an eighth a frame ($168BE). Fire enters
  the nearest planet's world.
- SELECT.SWP is loaded to $A(a5) + 4 ($115E0 keeps a tag long first), so
  the code's offsets into it are the file's + 4.

## Sound

**Drivers.** The CIA-B timer B interrupt ($10D84; timer $3782 at the
709,379 Hz E clock: 49.92 Hz, $10D5E) calls the routine whose address is
in $2054(a5), when not 0: the music player (module + $94, set by $112A2)
on the title and menus, the effects player ($344A, set by $1116C) in the
levels; one at a time. $11250 (d0 & 3) starts a tune (module entry at
offset 0 with d1 = tune, $1126C), $112B0 stops the music, $11152 resets
the effects ($3400: requests $FF, channel volumes and DMA off). A level
leaves the music's state alone: its playing flag (module + $6F9 =
$439A1) stays set.

**Where the tunes start:** the title ($1772C) starts tune 0 only when
$6F9 is clear (at boot), so it keeps whatever plays; entering the map
($1170A) starts tune 2 from its beginning (the cluster choice after it
does not); after a world tune 2 again ($18A66), heard over the
statistics and "Next world"; "Abandon this world" tune 3 ($18956); the
ending tune 1 ($17D6C). Both 3 and 1 go on over the title and menus.

**Effects player** (the file "effects" at $3400). Requests at $36D2:
bytes +0…+3 the effect asked for per channel ($FF none), words +4…+$A
($36D6, $36D8, $36DA, $36DC) the channel volumes ($40 full). Its own
per-channel state at $36D2+$C (busy count), +$10 (duration), +$14
(sample), +$18 (effect playing) and at $366E (sample start, length,
repeat start, repeat length, envelope volume, DMA bit, envelope offset,
position, countdown and repeat count, period). Tables: effects at $3850
(16 bytes: sample, envelope, period word, duration byte, $FF = no end),
envelopes at $3800 (16 bytes: triples of ticks, volume change, repeats;
a ticks byte ≥ $C0 jumps to position (byte & $3F); position 15 ends),
sample offsets at $36EE (longs from $3AB0), sample loops at $375E
(6 bytes: length in words, repeat offset in bytes, repeat length).

Each call, channels 3 to 0 ($3462): a request is taken when it is below 8,
or 8-$D while the channel plays 8 or more, or $E and up while it plays
$E and up, or whenever the channel's busy count is 0; else it is dropped.
Taking it: the effect's sample, period, duration and envelope (volume 0,
countdown 1), DMA off for the channel, the sample's start and length
written, busy $32. Playing: busy − 1; the duration − 1 unless $FF, at 0
the channel's volume 0 and it stops; the envelope: when its countdown
runs out the volume changes by the entry's step (kept in 0 … $40: above
$40 → $40, a byte ≥ $80 → 0) and its repeats − 1; at 0 the next triple.
Then DMA on for all four ($820F, after a busy-wait) and for every
channel still playing: its repeat start and length (taken by Paula after
the sample's first pass), its period, and the volume × the channel's
request volume / $40.

**Rebuilt** (`sound/effects`, from the effects file's own tables, on
PaulaChannels, run at 49.92 Hz while a level plays). Checked with
`play_route1s` (Tutorial 1 → 4 → 1 with the player's state at $3600
recorded, `SDW_SOUND=1`): every channel takes the same effects in the
same order as the original (channel 2: the lift's up and down sounds and
the exit). The ticks can differ by a frame: the original's player runs
from its own interrupt.

Distance ($111B4: |dx|, |dy|, |dz|): $1117E: (sum >> 4) ≥ $5F silent,
else (max(that − $20, 0)) ^ $3F; $111A0 (heads): ((|dx| + |dy|) >> 6) ^
$3F unless ≥ $3F. Offers are taken only when louder than the tick's best
so far; $11208 then writes channel 2's volume every tick (0 with none)
and the effect when the object or effect changed ($2080/$2084).

**What asks for sounds**: jewels (channel 0, effect from
$14B8C), exits (channel 2, 7), push buttons and binary switches
(channel 1, $E + type / $18 + colour), landings (6, volume by impact),
bounces ($24), switch pads ($15 + n), deaths and respawns (2, 3), boinks
($1F), creature touches ($1D, $1E, $20, 5), the timer's ticks (channel 3,
$21/$22 with volume $40 − 2 × seconds, at least $20). Objects heard by
distance ($111EC with the volume from $1117E / $111A0: lifts $B/$C,
heads $A, sparks $25, pyramids $23) compete each tick for channel 2: the
loudest one ($11208) sets $36DA and, when it changed, $36D4.

**Music player** (the file "music" at $432A8; rebuilt as `sound/music`).
Entry 0 starts tune d1: from the tune's 16-byte header at $4429A + 16 ×
tune (four sequence offsets from $442DA, eight tempo counts) it resets
only each channel's off flag, block flag, row, row countdown (1),
instrument ($FF) and sequence position (from $43A1C + n, then one step),
the fade ($FF: none), the counters $28/$29 and the master volume ($40);
everything else in its state blocks ($4390E for Paula's side, $439A0 for
the sequencer) carries over from the file or the tune before. Entry +$94
($4333C) is one tick:

- With a fade set (sequence command $FD n), the master volume drops one
  step every n + 1 ticks; at 0 the tune stops.
- Rows advance only on ticks whose tempo count ($29) is 0; the count is
  then reloaded from the eight tempo counts in turn (backwards), a swing.
- Sequences ($43876): $FD n the fade, $FE stop, $FF n jump to n, a byte
  with bit 6 opens a block played (byte & $3F) times, else (pattern
  repeats, pattern).
- Pattern rows ($43D5C offsets into $43DEE; $FF ends a pattern): a byte
  (bits 0-5 the length in rows, bit 6 the envelope taken up at its
  position 9, bit 7 an instrument (< $80) or a slide byte follows), then a
  note (bits 0-6 an index into the periods at $43A20, bit 7 tied). A new
  instrument or an untied note starts the instrument's sample.
- Instruments ($43C7C, 16 bytes): +0 sample, +1 envelope, +2 flags (bit 1
  a second note every other tick from +$D, bit 2 a second sample and a
  bend +4, bit 3 four notes in turn from +9, bit 4 the sample from +8
  while +7 runs, bit 5 notes from +9 in turn while +7 runs), +7 a phase
  length, +$E vibrato (depth $4397A, step shift $4398D: the gap to the
  next note's period halved that many times), +$F a delay before it.
  Slides: a byte ≥ $C0 raises the period by its low six bits a tick,
  else lowers it.
- Envelopes ($43BDC) as the effects player's, but a sum of $FF gives 0
  (not every byte ≥ $80).
- Then DMA on, and each channel's loop, period and volume × master / $40.

Checked with `musiccheck` against `sdw.py musictrace` (the state blocks at
every tick): tune 0 (title) and the map's tune, 1,500 ticks each, every
channel's period, volume, note, row, pattern, countdown, sequence
position, instrument and envelope position and the tempo and master
volume equal.

## Bugs and quirks in the original

Kept in the port's original mode unless noted.

- **Wall edges are asymmetric:** x tests the next cell only above $A6, y
  from $A6 itself, so a position exactly at the far edge touches the next
  cell along y but not along x.
- **Rebound rounding differs:** edge walls rebound with (−v) >> 1, corners
  with −(v >> 1), and corners push out to $A5 where edges use $A6.
- **Camera never closes the last pixels left/up:** a negative step is
  rounded one smaller, so up to 7 pixels left or up remain.
- **32-pixel creature masks are misaligned:** they AND their per-column
  mask with the frame before shifting it ($101FE), so occlusion lands x &
  15 pixels left of its pixels (EasyDizzy 6: 16 pixels wrong without the
  quirk, 0 with).
- **Object masks are band-aligned:** objects AND a mask made at creation in
  8-row bands (row 0 at a band boundary) into their frame rows without
  correcting for y & 7 (only the explorer's path adds it, $101DA). Lifts
  and heads read their 96-row mask (made at height $7FF, key + $100, −$100
  per band) from row (($900 − z) >> 4 & $FE) / 2.
- **Full object table:** with 48 objects the builder stops storing but does
  NOT skip the parameters, so the following words are read as commands.
- **Friction uses muls.w:** only the low word of v >> 8 is multiplied, so
  large speeds would wrap (inferred from the instruction; not seen in a
  trace).
- **The tick continues after a death:** the death routines return into the
  tick, so the rest of it still runs on the dying explorer.
- **The first entry of every animation list is skipped** (the stepper
  starts with the count at 1).

- **A box at an odd column scribbles** ($185BC): its first byte column is
  written with `movep.l`, which writes every other byte: right for the
  Atari ST's word-interleaved planes, on the Amiga the four plane bytes
  land in plane 0 at four columns two apart, one row after another
  (kept). Most of the game's boxes start at even columns.
- **The binary switch compares with a register it never set** ($161E6):
  d0 still holds its own tick address; harmless, since the height must
  then be exactly equal anyway.
- **Bank codes are multiplied by 4 twice over** ($18348): the jump table's
  index is reused, so banks are $1A00 apart, not $680; codes 3-7 point
  past the fonts.
- **Transparent text masks with the usual font** ($18440): the mask
  table comes from bank 1 whatever bank is drawn.
- **The timer's clamp tests the parameter, not the sum** ($1531A).
- **The Hard ending reads past SELECT.SWP** ($16B62 with d0 = 10): its
  eighth picture starts at the file's end, so whatever the level left in
  that RAM is blitted (the port leaves it out).
- **A deadly timer after "Use reserve fuel"** ($12CE8 with $24FC set)
  remakes the timer's own record as the explorer ($12B58).
- **The reserve message's wait comes from a leftover register** ($151B4:
  $109E2 counts d7, which the text printer left as height + row).
- **The secret passage's loop jumps into the clearing loop** ($15422):
  only the bottom cell becomes block 1, the cells above are cleared.
- **The lift has no clamp at its top**: it can overshoot the top level by
  up to its speed.
- **The menu loop is not frame-locked**: its timeout and flashing depend
  on how long drawing the items takes.

## Unused code and data

- $1341C (a variant of $13394 with $FFFD9365) and $13454/$1347A
  (height-only forms) are not in the surface table.
- $24E0 is written ($16, $5E, $B8) but never read.
- The Palette object kind (16) appears in no level (rebuilt anyway);
  worlds 29 and 30 have no records.
- A 33rd world slot at $C0000 (unscrambled, never loaded).
- Disk ranges $37400–$3A1FF and $42400–$45DFF are never loaded; the latter
  holds the level editor's text.

## Evidence and checks

Harness (research only, `harness/`): `run.sh` (FS-UAE, port 6870),
`mkboot.py` (research boot disk), `shot.sh` (window capture), `dbg.py` (GDB
RSP), `sdw.py` (`go`, `regs`, `mem`, `dump`, `joy BITS [FRAMES]`, `seq
BITS:FRAMES …`, `world N [LEVEL]` enters a locked world by redirecting
$117B0, `trace`, `visit ROUTINE`, `playtrace OUT TICKS INPUTS`: A5+0 …
$3800 at the next level start (SDW_LEVEL=n enters level n), then the changed bytes before every rules
tick $11510, with the joystick byte set per tick), `drawn.py` (dump after a finished draw
pass: break at $143B6 and run to its return address), `route.sh`,
`level.sh`, `batch_traces.sh`, `visit_traces.sh`. `patch_joy` rewrites
$104C2 to read the direction byte from RAM $F800 (free after the effects
module), so input is exact.

Route to a level: boot ~40 s → attract → fire → menu → down, fire →
Easy/Hard: hold left (Easy) or right (Hard), press fire, release fire
first (`seq 04:10 84:10 04:10`) → world map → fire → Tutorial level 1.

Native checks and tools (`spindizzy_worlds_assets`):

- `playcheck`: replays a `playtrace` record through the native rules from
  the level start's state (world, level, fuel, jewel longs, switches,
  timer, clues, explorer) across level changes, comparing every tick:
  the explorer's kind, position, speeds and contact, the level, fuel,
  reserve, jewels, switches, timer and every object slot (made or not,
  position, the kinds' own words, animation counters). A record without
  any change is a second harness stop with no tick (seen once in 13,000
  ticks) and is folded into the tick before. Records in work/traces:
  `play_t1_b` (Tutorial 1, 400 ticks), `play_route1` (levels 1 → 4 → 1,
  2,700 ticks: two jewels, two exits), `play_tutorial_full` (the whole
  Tutorial, see below).
- `route WORLD SCRIPT OUT`: steers the native game through waypoints
  (`go X Y`, `fgo` with fire, `waitz SLOT Z` for lifts, `level L`, `rest`)
  and writes the joystick byte of every tick for `playtrace`. The
  Tutorial route (work/traces/route_tutorial_full.txt, 12,850 ticks):
  the four level-1 jewels by entering each temple (levels 4, 6, 7, 5, each
  left again at once), the Heads rise, the temple to level 8, its button,
  the lift to the top, the exit to level 15, whose timer ends the world.
- `drivefile`: runs the whole module (front end and play) headless on
  frame inputs; the Tutorial played from the title to "Next world".
- `map WORLD LEVEL`, `objects WORLD LEVEL`: route-planning views.

- `gridcheck`: Tutorial 1 grid, 8189 of 8192 cells equal (the rest an
  object's column).
- tile map vs `work/charmap.bin`: equal in all 641 cells.
- `viewcheck` (everything native, run-time state from the dump's records,
  visible columns 16–303): Tutorial 1 (three dumps), Gargoyle 1,
  EasyDizzy 5 (arrival) and 6 (Pacs, Boinks, lifts): 0 / 43,200 pixels
  differ. EasyDizzy 1: 185, the Timer's "25" text only. Pyramids checked
  at record level in EasyDizzy 5.
- `physcheck`: replays a trace (state before each tick, joystick set per
  tick at $12B7C) natively from its first state and compares every tick,
  re-synchronising after deaths and after object effects (an alien's
  touch). Traces in work/traces, all equal:

| Trace | Ticks | Exercised |
| --- | --- | --- |
| t1_a (Tutorial 1) | 343 | flat, 18 wall pushes; then the Heads object blocks the explorer (physcheck stops there; playcheck, which runs the objects, matches it) |
| w31_5, w6_4 | 333, 749 | slopes, 16 falls out of the world (clue 3) |
| w22_2, w7_1_1320e | 1199, 499 | flat, 43 wall/corner pushes, an alien's touch |
| w22_2_1327e | 499 | slippery 497 ticks |
| w18_3_13504 | 499 | bouncy 21, ice 17 |
| w10_1_1349a | 39 | ice: standing still kills (clue 7) ×10 |
| w18_2_13228 | 499 | switch pads 497 (sets $3616 to 2) |
| w19_1_13584 | 39 | deadly (clue 8) ×10 |
| w6_4_13394/13486/13464/133F8/133E2 | 499/499/306/499/499 | corner ramps 273, ridges 494, valleys 126, corner slopes 527 |
| w22_4_133e2 | 448 | diagonal ramps 116 |
| w16_1_1335e | 499 | steep 69, slope 86 |
| w11_1_1354c | 499 | conveyors 497 |
| hard landing | — | w22_2_1327e first take (dropped from 3 levels): clue 6 ×9 |

- `ending EASY|HARD LAST`: the ending over a cluster's map, a picture
  every 8 frames and the effects asked (not compared with the original).
- `drivefile` items `state:FILE` (a saved state) and `kNN` (a matrix key
  for one frame): the break-up after the Tutorial, the Easy ending from
  a state with one world left, the overview map. `check` makes a quick
  save, loads it into a second module and runs both on the same inputs,
  comparing the saved state, the picture and the sound every frame:
  equal through the whole Tutorial (title, menu, cluster choice, map,
  levels, clues, the reserve message, break-up, statistics), the
  overview map, both pauses and the Easy ending; a load saved again
  gives the same bytes.

The port's quick save is the whole state through `state::Archive`
(schema 2, "SWZS"): the stage, the stick byte and keys, the world's
progress, every screen's own state (pictures included), the level as it
stands (header, grid with looks, the tile map as built, objects, the
explorer, camera, stars, timers, the held picture), both sound players
and their Paula channels. Version 1 saves (between worlds) still load.
- `playcheck` says after a record what the port does next: Roller-ama 4
  (`play_w9_4`, 641 ticks, recorded until the original stopped ticking)
  ends in game over at the next tick, as the original does.

## Not yet traced

sd_game.dat; level header words +2/+6; the world "setting" word; the
overview map's 16 passes; the break-up and the endings against the
original; tunes 1 and 3.
