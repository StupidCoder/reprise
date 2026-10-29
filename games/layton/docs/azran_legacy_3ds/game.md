# Azran Legacy (Nintendo 3DS): how the game works

What we learned while reverse engineering the sixth adventure (German
cartridge image `AL6D`) for the re-implementation in `games/layton`. The game
runs on the same engine as *Miracle Mask*. That game's
[game.md](../miracle_mask_3ds/game.md) describes the shared parts: formats,
the XQ32 script machine, the scene and services, 3D rendering, sound and
movies. This document covers what differs and what is this game's own: the
puzzle loops and their rules, the minigames, the renderer findings made
here, and the original's quirks. Remaining work is in [plan.md](plan.md).

All findings come from the user's image and from runs of it. Sources were
Ghidra decompiles and annotated disassembly of the code segment (loaded at
0x00100000), script dumps, and runs in the reference emulator (a libretro
build of Azahar), which is used only as a measuring instrument. Findings
from the fifth adventure counted only once checked against this image: same
class names and service numbers proved to be weak evidence. No disassembly,
decompilation, asset map or format description of this game was consulted.
Statements marked **hypothesis** are not confirmed. Detailed notes:
`.analysis/adventure56/docs/puzzles6.md` and `minigames6.md` (local only).

Contents: 1 Cartridge image · 2 What changed from the fifth adventure ·
3 Puzzles · 4 Puzzle loops · 5 Minigames · 6 Rendering findings · 7 Bugs and
oddities in the original · 8 Research tools and checks


## 1 Cartridge image

| Field | Value |
|---|---|
| File | the German cartridge image (`... (Germany).cci`), decrypted (NoCrypto) |
| SHA-256 | `4ca8db98214909a1111034d0d8183f4238b038885cac3b27aa16a801c61c5910` |
| Product | `CTR-P-AL6D`, program id 00040000000f3100 |
| Size | 1 GiB image, 948 MB RomFS, 610 files |
| Code | 3,059,712 bytes expanded (text 0x299808, ro 0x36b08, data 0x198a0) |

RomFS: `lt6/arc/` (three archives), `snd/` (`product.bcsar` and 566 streams),
`mov/` (34 movies), the electronic manual `manual.cfa`, `version.cfg`.
Build leftovers in the pop-up packs (`Google SketchUp 8.lnk`, `all.bat`);
the archives carry a Windows FILETIME from 2013.


## 2 What changed from the fifth adventure

- **Archives** are ARC0 instead of XFSA. The header is 0x48 bytes and adds a
  FILETIME and check words. Directories are 20 bytes (CRC-32, two links,
  first file, count, offsets); files 16 bytes (CRC-32, name offset, data
  offset, size). XFSP packs are gone. All 5,603 names match their CRC-32.
- **Code base**: recompiled and extended. 31 % of this game's code windows
  occur in the fifth (46–75 % in the library region). New classes include
  render objects, collision data, coloured ruby text commands and the Nut
  Roller.
- **Services**: the library table has 1,029 handlers (1000..6988) and the game
  table 314 (1300..20900). Of the 437 services the puzzle scripts use, 406
  share a number with the fifth adventure; same number usually means same
  service, but each was confirmed by traces. The new game services 20000..20700
  are vector and mesh helpers (20000 cosine between vectors, 20001 cross
  product, 20002 length, 20003 rotate a vector about an axis by a quaternion,
  20004 within distance, 20100 an angle between two pen directions, 20300..
  20302 sprite-batch entries, 20400..20402 a mesh's value and visibility,
  20500..20503 the motion sensor, 20700 a merged copy's position).
- **Game loop**: it also loads `seq/prog_lt` after `prog_common`.
- **Puzzle values**: seven per puzzle; the session writes the row's five,
  then −1 and 0. The help script uses the seventh as the help animation
  number.
- **Text**: 5950's first value is the letter spacing (−1 outside Japanese).
- **Streams**: named in the sound archive's file table (0x220D entries, e.g.
  `stream/JNG_415_Failure.dspadpcm.bcstm`), not by number.
- **Handwriting**: the same recogniser, its tables 0x3A688 lower in the code
  segment.
- **Motion sensor** (20500..20503): reads the raw accelerometer at game
  +0x548 (raw / 512, in G; at rest (0, −1, 0)). The two sensor puzzles count
  shakes, not tilt.
- **Clock** (2900): parts of the local date and time; puzzle 061 seeds its bat
  with the millisecond.


## 3 Puzzles

**Data**: `res/naz/seq/nNNN.xq` (175) plus 20 daily-puzzle scripts; catalogue
`res/ge/lt6_def.xs` 2301 (175 rows); parameters `res/lt6_naz.xb/nNNN.xs`;
texts `txt/ge/50/50_NNNNNN.xs`. Type numbers are not the fifth adventure's.
Puzzles 169..175 have empty scripts. Types from 101 up (daily puzzles) are in
the type switch but not mapped.

**Session**: type switch `0x1D7888`, loop factory `0x28F7BC`. 103 puzzles of
types 1–6, 15, 19, 23, 24 and 33–40 run on the standard loop 3100.

**The base loop of every sixth-adventure loop** (`0x230E94`) keeps an undo
history: 10000 sets room for N values or records, 10001 pushes a record,
10002 pops the newest into registers, 10003 empties it; a full ring
overwrites its oldest values. Thirty puzzles keep their undo moves there.

**The arrow pad** (base class `0x22FD90`, commands 11000..11010): drawn arrows
and invisible touch areas over each animation's box, moved by the animation's
origin. 11010 answers the direction (1 right, 2 up, 3 left, 4 down, 5 outside,
6 centre) and whether the pad's buttons gave it. Pad hooks (1110) run a block
when a button goes down.


## 4 Puzzle loops

| Type | Loop | Class | Puzzles | Rules |
|---|---|---|---|---|
| 7 | 3130 | Ltgram | 5 | the fifth's place-pieces rules plus overlap and inside-a-group tests (13, 15, 16) |
| 8 | 3110 | Slide | 5 | blocks as in the fifth, but a goal cell's corner must lie exactly where one of the block's collision rectangles starts. "Adventure" boards (112, 122): sliding path panels with one hole; a walker follows the corners from mark to mark |
| 9 | 3120 | Line | 1 | the fifth's pairing lines plus "number of lines" and "line i's points" |
| 10 | 3230 | Cut | 2 | new: a rope loop through every post of an 8 × 7 grid that shuts the thieves in without a jewel |
| 11 | 3240 | Pile | 3 | the fifth's piles, but the drop takes the nearest cell whose footprint (top-left anchored) is free, and the judge marks from the top-left |
| 12 | 3250 | Mosaic | 3 | the fifth's sliding lines plus holes (cells that add nothing to the free distance) and shading of pieces that can move |
| 13 | 3260 | Route | 3 | an airship route over a grid of buttons with fuel; straight and diagonal steps cost differently; refill cells; judged when no step is left and the path is long enough |
| 14 | 3270 | Slide2 | 3 | trains: an engine dragged a cell at a time on a rail map; wagons couple and uncouple; points switch; a drum view on the upper screen |
| 16 | 3290 | Beam | 3 | lanterns of three colours light their streets on a 6 × 6 grid |
| 17 | 3300 | Othello | 3 | the fifth's flipping cats, plus blocked cells, long jumps and cell heights; colours are costume meshes |
| 18 | 3310 | Vanish | 3 | jump-over on a 5 × 5 board from fixed pixel tables; links that reach twice as far across gaps; no kind check on the jumped piece |
| 20 | 3140 | Fit | 15 | pieces on a board with cell values, turned in 45° steps; bookshelves, plants, routes, present boxes, snakes and wagons judged by script |
| 25 | 3320 | GuardRobot | 2 | the player steps past robots whose beams see along rows and columns; stepping onto a robot breaks it |
| 26 | 3170 | Transfer | 2 | boats step in turn and carry the character; landing on the goal from above or below |
| 27, 30, 31, 32, 41 | 3180 | Partition | 12 | Fit plus corner and item grids: sandbags of equal weight round every cell, maze panels, a plate cut into blocks of four, crates stacked by colour, ten buttons, ice floes |
| 28 | 3330 | BridgeLabyrinth | 2 | stepping along links between nodes; the cubes left behind fall unless held; switches |
| 29 | 3340 | TouchPad | 4 | the arrow pad only: statue mazes (statues turn each step; none may face the player) and cat walks |
| 42 | 3160 | ChangeTexture | 1 | a mesh of a model draws a captured screen as its texture |

Details worth knowing:

- **Fit**: command 12 returns the new angle in radians. A clockwise swing on
  the screen adds two steps (confirmed on the console).
- **Fit input**: the pen goes to the script (`Fit_PenTouchBegin / Drive /
  End`), which gets the cell where the pen was lifted.
- **Slide2** (the trains): track bits per cell (1 up-down, 2 left-right, 4
  up-left, 8 down-left, 16 up-right, 32 down-right). On a bend the move tries
  the last way whose dot product with the heading has the pull's sign. The
  upper screen's drum turns 40° per engine move.
- **Cut**: the rope board at +0x1D8 is shared by four more classes. Each step
  flips an edge between the stroke's post and the nearest neighbour.
- **ChangeTexture**: the captured lower screen's rectangle is given in a
  512 × 256 texture (**hypothesis**: the console's power-of-two render target).
- **Screen capture** (3410/3414, 5440/5441): a render-target picture filled
  with the screen drawn so far, at the object's place in the draw order.
- **Line sets** (5970..5988): quads along segments, as long as the segment
  plus the width; the texture rect is in the picture's pixels.
- **Sprite batches** (3570..3573): many cells drawn as one object; 3573 offsets
  an entry. The entry's angle does not turn the drawing on the console.


## 5 Minigames

| Loop | Class | Script and data |
|---|---|---|
| 6000 | lt6CLoopMiniAirShip | `seq/mini_airship` and course data |
| 6100 | lt6CLoopMiniFashion | `seq/mini_fashion` and 32 `mini_fe_*` files |
| 6200 | lt6CLoopMiniGarden | `seq/mini_garden`, `res/sub/garden_stage_NN.xs` |
| 6300 | lt6CLoopMiniRisu (Nut Roller) | `seq/mini_risu`, `res/sub/risu_stage_NN.xs` |

**Nut Roller** (6300):

- **Stage files**: size, cell kinds with blocked sides, cells (kind, object,
  height), two views and a following camera.
- **Kinds**: water, ground, hole, low and high curves, bridges across and
  along, the squirrel's track.
- **Objects**: squirrel, tent, walnut, acorn, rock, bridge, tree, hedge,
  bamboo.
- **Squirrel**: steps along its track (turning is a step), picks up a ball
  ahead, puts it down or throws it lower.
- **Balls** roll cell by cell. Curves turn them, low and high ramps lift them
  by less than 3 or 5, and a ball pushes a ball ahead at the same height.
  Leaving a bridge breaks it.
- **Holes and water**: a rock fills a hole; an acorn grows a tree there. The
  walnut fails in water, in a hole, or crushed by a ball landing on it.
- **Goal**: the walnut on the tent.
- **Timing** (2110 30): squirrel step 12 frames, turn 6, ball step 10; a
  ball spins 0.628 rad a frame while it moves.
- **Undo**: a ring of 30 states.

**Flower Garden** (6200): flowers of four colours planted on a garden's cells
bloom outward in waves and wither trees; the verdict counts the trees left.

- Commands 1..37, waves `0x21BE5C..0x21C510`, step `0x2FDE70`, camera blend
  `0x300728`.
- The verdict waits until its jingle ends: `JNG_415`, 98 display frames.
- Trees run "charge" → "006" → "open" → "003" by motion links.

**Fashion** (6100), surveyed so far:

- Eight characters with five slots each.
- 40 items of seven bytes (five stats, a tag byte, a flag).
- Per character, requirements of six kinds: equal, at least, at most, tags
  worn, tags not worn, the third slot's flag.
- The worn items and five flag bytes are kept in the save.
- Data: `res/sub/lt6_sub_fashion.xs` (40 × 8000 items, then per character
  8001..8003).

**Airship** (6000) is not surveyed yet.


## 6 Rendering findings

Made with this game's console GPU lists (also valid for the fifth):

- **Blend factors** (`.atr` word 6): (0, 8, 1) is additive (417 of 535 "-add"
  materials), (0, 1, 0) opaque with an alpha test, equation 2 reverse
  subtract ("-sub" materials).
- **Draw classes**: the console draws all ordinary meshes, then the decals
  with a depth offset (`.atr` word 3 byte 0 = 1, amount in word 8; offset =
  amount × 1000 / 2²⁴), then the additive meshes. The additive ones are drawn
  without the depth test in the garden, but puzzle 126's glass stays behind
  its rim, so the port keeps the test.
- **Material motions** (XMTM): per-material alpha tracks (one float per key),
  driven by 6830.
- **Outlines**: every `.sil` here has key 0, so no model has an outline.
- **Models on 2D objects** are drawn with the 2D object's matrix, y and z
  negated, plus the screen centre (200, 120 upper; 160, 120 lower),
  orthographic in pixels.


## 7 Bugs and oddities in the original

- **Missing blocks.** Puzzles 010, 147 and 110 call script blocks that the
  loaded scripts do not have (110's are defined in other puzzles).
  **Hypothesis:** the original ignores them as well.
- **Off-board reads.** A Slide panel board read off the board goes through a
  null entry (commands 17, 18, 20). The port treats it as rock.
- **Pile drops.** The drop's footprint test does not check indexes past the
  last cell (a footprint may wrap into the next row) and never takes the last
  cell.
- **Puzzle 145** has no verdict screen; its reply skips it.
- **Service 20600** sets a byte read by touch and material code; its meaning
  is unknown.
- **Unknown fields.** The sprite batch's entry angle is stored but not drawn.
  Several class fields are written and never read.


## 8 Research tools and checks

Research tools live in `.analysis/adventure56/tools/` (local):

| Tool | Purpose |
|---|---|
| `svc_tables.py`, `svc_match.py`, `loop_ids.py`, `loopcmds.py` | service tables and loop classes from RAM and code |
| `solves6.txt`, `solve6.sh` | every puzzle solved natively |
| `refsolve6.sh`, `verify6.sh`, `auto6.py`, `promote6.sh` | the same solutions played on the console and checked for the "RICHTIG" screen |
| `solvers/` | about forty solvers, one per puzzle kind |
| `cmp6.py`, `cmpf6.py` | frame comparisons |
| `risutrace.py` | Nut Roller traces |

A patched emulator core (`ref/core6.dylib`) fixes a data race in Azahar that
crashed every puzzle. Native runner: `picarat_a5_run` with the sixth pack
(`A6_MINI=1` for minigames).

What was checked:

- All 168 puzzles with content are solved natively, and the same inputs end
  in "RICHTIG" on the console.
- Start screens of 69 puzzles were compared with console frames.
- Nut Roller course 1 matches the console step for step through a full
  solve.
- Flower Garden 1 matches the console with the yellow and red flowers.
