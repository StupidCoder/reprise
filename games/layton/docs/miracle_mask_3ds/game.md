# Miracle Mask (Nintendo 3DS): how the game works

What we learned while reverse engineering the fifth adventure (German
cartridge image `AKKD`) for the re-implementation in `games/layton`. This is
the first of the two 3DS games, and its engine is shared with *Azran Legacy*,
so this document also describes the shared 3DS engine: the cartridge image,
the archive and data formats, the XQ32 script machine, the scene and its
services, the 3D renderer's rules, sound and movies. *Azran Legacy*'s
[game.md](../azran_legacy_3ds/game.md) lists only what differs. Remaining
work is in [plan.md](plan.md).

All findings come from the user's image and from runs of it. Sources were
Ghidra decompiles of the code segment (`.analysis/adventure56/tools/ghidra_ctr.py`),
annotated disassembly (float arguments hide from the decompiler and were read
from the instructions), dumps of scripts and data lists (`xq_dump.py`), and
runs in a reference emulator. The emulator (a libretro build of Azahar driven
by `ref_host`) is a measuring instrument only: command traces, memory dumps,
GPU command lists and frame captures. General 3DS container documentation
(NCSD, NCCH, ExeFS, RomFS, code LZ) was used. No disassembly, decompilation,
asset map or format description of this game was consulted. The DS games'
findings were hypotheses until checked here. Code addresses are in the code
segment loaded at 0x00100000. Statements marked **hypothesis** are not
confirmed. Detailed notes: `.analysis/adventure56/docs/` (`assets.md`,
`script-machine.md`, `puzzles.md`, `sweep.md`, `hwr.md`, `story.md`,
`ruins.md`, `rabbit.md`, `robot.md`, `secrets.md`, `movies.md`), local only.

Contents: 1 Cartridge image and engine lineage · 2 Data formats · 3 The XQ32
script machine · 4 Scene, objects and services · 5 3D rendering · 6 Sound ·
7 Movies · 8 Loops and the story · 9 Puzzles · 10 Side games · 11 Saves and
the secrets · 12 Bugs and oddities in the original · 13 Research tools and
checks


## 1 Cartridge image and engine lineage

| Field | Value |
|---|---|
| File | the German cartridge image (`... (Germany).cci`), decrypted (NoCrypto) |
| SHA-256 | `3fae2d2f5c3bc10933a9446904c9dcef5f02b80a311a3c20d71ae857aaec9788` |
| Product | `CTR-P-AKKD`, program id 00040000000a8700 |
| Size | 1 GiB image, 825 MB RomFS, 714 files |
| Code | 3,293,184 bytes expanded (text 0x2d3d3c, ro 0x35ea0, data 0x197e0) |

RomFS: `lt5/arc/` holds three archives (`_a` program data, `_b` shared art and
3D, `_ge` German resources); `snd/` one NintendoWare sound archive
(`product.bcsar`) and 632 `.bcstm` streams; `mov/` 42 `.moflex` movies; `ctr/`
banner and icons. Encrypted images are refused; nothing is decrypted.

**The engine is the fourth DS game's design carried forward** to a 3D
platform, with the middleware replaced:

- The DS library's `gnC*` managers return as `lxpC*` (nine names identical
  but for the prefix), plus new 3D managers (model, chara, light, sky,
  particle, layout, pop-up, OpenGL ES, movie).
- The compression word is the same: method in bits 0–2 (stored, LZ10,
  Huffman 4/8, run-length), size above.
- Archives: GFSA (DS) → XFSA (this game) → ARC0 (the sixth). Packs: GFSP →
  XFSP (this game only) → XPCK. Scripts: GSS1 → XQ32. Data lists: GSD1 →
  XSCR. Hashes grew from CRC-16/X.25 to CRC-32; fields grew to 32 bits;
  tables are compressed.
- **Sound** is now Nintendo's NintendoWare middleware; the DS games' own
  sequencer is gone.
- **Movies** are still Mobiclip, in a new container (moflex).
- **Pictures** are IMGC (true colour and ETC1).


## 2 Data formats

**Archives** (`.fa`): magic; offsets of the directory table, a
directory-hash list, the file table, the name list and the data; u16
directory count, u16 hash-list count, u32 file count. Tables are method-word
streams; files are stored as they are, each starting with its own magic.

- XFSA directory (16 bytes): CRC-32 of the name; file count (bits 0–13) and
  file-names offset (14–31); first file and a link; sub-directory count and
  name offset.
- XFSA file (12 bytes): CRC-32; offset / 16 in bits 0–25 with name-offset
  bits 9–14 above; size in bits 0–22 with name-offset bits 0–8 above. The
  split was found from the data: with 9 bits alone only half the names
  matched.

All 5,301 names match their CRC-32.

**Packs.** XPCK: `XPCK`, a 12-bit member count, offsets / 4 of the member
table, name list (a method-word stream) and data. Members are 12 bytes:
CRC-32, name offset, offset / 4, size, and a u16 extending offset and size.
XFSP (13 packs, this game only) is the same with 10-byte members and
CRC-16/X.25 hashes.

**IMGC pictures** (`.xi` and pack members): a 0x48 header (format at 0x0A,
mip levels at 0x0C, bits per pixel at 0x0D, width and height at 0x10, stream
sizes at 0x38/0x3C), then a tile map (one u16 per 8×8 cell, 0xFFFF empty,
all mip levels in sequence) and de-duplicated tiles.

- Formats: 0 RGBA32, 3 RGB24, 1/2/4 sixteen-bit, 12 eight-bit, 27 ETC1,
  28 ETC1 + 4-bit alpha.
- Tile pixels are in Z order with **y on the even bits**; ETC1 blocks are
  column-major, with the alpha block first.

**XQ32 scripts** (1,276): `XQ32`, the same ten halfwords as the DS GSS1
header, then five compressed tables. Blocks 24 bytes, labels 12 {name,
CRC-32, command}, commands 12 {first operand, count, group, opcode},
operands 8 {type, value}. **XSCR data lists** (1,942): `XSCR`, commands of 8
bytes {opcode, count, first}, operands as XQ32. Text uses `{:a}`-style
escapes.

**3D files**:

| Kind | Content |
|---|---|
| `.xc` model packs | `XMPR` primitives (meshes), `MTRC` materials, `ATRC` attributes, `XPVB`/`XPVI` vertex and index buffers, `XCSL` silhouettes, `.mbn` bones (rest transforms), `.txp` texture projections |
| animations | `XMTN` bone motions (T/R/S tracks, int16 quaternions, `MINF` spans), `XIMA` image motions (four track lists; list 0 texture offsets), `XMTM` material motions (per-material alpha tracks) |
| `fix.xr` | PICA200 shader binaries (`DVLB`), light tables (`LUTC`), combiners (`CMBC`), named techniques |
| `.xk`, `.xv` | sky / fog / light; cameras (`XCMA` clips) |
| `.efp3ds` | particle effects |
| `.xa` | 2D animations as textured quads (`ANMC` + buffers) |
| `.xf` fonts | `FNTC` with an RGBA5551 glyph sheet, three glyph planes in R, G, B; glyph box offsets are signed bytes |

**Material blend** (`.atr`, `ATRC01`): word 6 bytes 0..2 are equation,
source and destination factor in the game's enum (0 zero, 1 one, 8 source
alpha, 9 one minus source alpha). (0, 8, 1) is additive; equation 2 reverse
subtract. Word 3 byte 0 = 1 marks a decal with a depth offset in word 8.


## 3 The XQ32 script machine

The fourth DS game's GSS1 machine, widened.

- **Loader** (`0x2810A4`): opcodes below 1000 bind to the core table (78
  entries); higher ones to the service descriptor passed in, which falls back
  to its parent. Descriptors from RAM: the library (995 handlers,
  1000..6988), the game (196 handlers, 2104..16571, parent the library) and
  the data-list commands (47).
- **Frame run** (`0x24DA70`) works as the DS machine. Calls go through block
  lookup (binary search, then the script's link to older scripts); labels are
  searched in the block's range.
- **Values** are 8 bytes {kind, array count, value}. Kinds: 1 int, 2 uint,
  3 float, 4 register, 8 text, 9 name, 10 array. Copies are deep.
- **Hashes.** CRC-32 for names; CRC-16/X.25 for opcode 521.
- **Random numbers.** xorshift128 at `0x2C40B8`. The default state is
  0x6C078966 0xDD5254A5 0xB9523B81 0x03DF95B3. A seed (`0x26484C`) fills the
  first three words with `(x ^ x >> 30) · 1812433253 + i`; the boot seeds from
  a hash of console data and clock. `random(n) = w % n`; random float =
  `(r & 0xFFFFFF) · float(0x33800001) · x`.
- **Differences from the DS machine:**
  - 100 assigns to an element when indices follow; 240..271 are in-place
    operators on elements.
  - 150..154 are numbers only (501 formats text).
  - Integer division by zero gives quotient 0 and remainder the dividend.
  - 500 clears its destination.
  - New commands: 40 (kind of a value), 511..513 conversions, 521/522
    CRC-16/CRC-32, 530/531 arrays.
  - 600..623 maths are single precision.


## 4 Scene, objects and services

**Frame order** (`0x37889C`):

1. The loop's scripts run.
2. Each 2D object in creation order: its handler thread, then its tweens and
   animation (only while shown and switched active).
3. The same for each 3D object.
4. The input is read and the touch dispatched, so a touched object's block
   runs next frame and every script sees the previous frame's input.

**Scopes** (2190/2191). Objects, 3D objects, resources and cameras carry the
depth at which they were created. Closing a scope deletes everything of its
depth from all eleven lists and restores the screens' cameras, skies and
switches saved at the open. A second resource of a name is refused (first
wins). The game keeps one resource list per kind.

**Switches.** 5005 shown, 5006 touch, 5007 active, 5008 drawn, each with a
saved state per open scope. 3010/3011 switch every object of a screen;
3012/3013 the other two.

**Object services.** A property's services come in fives: moving, get, set,
tween, relative tween; the offset is +5..+9. 6xxx are the 5xxx services on
3D objects. While an object has a handler thread, its registers 0..15 are
the object's values.

**Touch.**

- Picking (`0x27233C`) takes the point into the object's space through the
  inverse transform and tests touch areas last to first. Without areas, the
  object is hit by its content: an animation cell's triangles (triangles with
  repeated indices are skipped, zero-area triangles never hit, edges count)
  or a picture's box.
- A finished touch or click returns the handler to its normal block.
- A tap is held under 15 frames and released within 8 pixels (`dx² + dy² <
  64`).
- 3D picking (3022) casts a ray against sphere, capsule and box shapes, or a
  pop-up's layer triangles.

**Screen lists** are sorted by `priority · 256 + byte +0x58E`, a newcomer
after its equals. They decide touch ties and 2D draw order.

**Tweens** run per display frame. A 30 Hz loop (2400 2) advances them two
frames a game frame.

**3D transforms**: model matrices are `T · Rx · Ry · Rz · S`. A motion
replaces only the channels it has tracks for; others keep the bone's rest
values. Children attached by 6013 hang from a bone of the parent's posed
model.

**Cameras.**

- Kinds are built by a factory (`0x202C54`): kinds 0–2 near 10 / far 10250,
  kind 4 (the verdict's) near 1 / far 10241.
- Orbit (4190): `eye = target + (d cos P sin Y, −d sin P, d cos P cos Y)`.
- Each active camera draws two random floats a frame for its shake
  (`0x221944`), which is why the random sequence depends on how many cameras
  are active.

**Canvases** (5500..5534): pen mode, width and colour; mode 16 records strokes
for handwriting.

**Merged models** (6890..6896) draw many instances of one mesh, each with its
own matrix.


## 5 3D rendering

Read from the console's GPU command lists (dumped from emulator RAM and
decoded per draw) and from `fix.xr`'s shaders:

- **Characters** use fragment lighting: the combiner (primary + secondary)
  × texture × vertex colour. The shader passes 0.5 × the colour and a later
  stage doubles it. The material's emission is the global ambient; the sky
  light's specular × the material's; diffuse 0. The D0 table (input N·H,
  signed) is on exactly when the material has its own LUTC.
- **Outlines**: a silhouette geometry shader draws quads along edges between
  front and back faces, with width `n.xy / clamp(w, 15, 45)`. A `.sil` record
  whose key word is 0 is skipped (all of the sixth game's are).
- **Rooms** use the `#FIX_IMG` technique: vertex colour × material colour ×
  texture, unlit.
- **Mesh passes.** A mesh's pass mask (`.prm` word 13): scenes draw pass 0;
  collision meshes (`_col`) are mask 2 and never drawn.
- **Draw classes**: all ordinary meshes, then the decals with a depth offset,
  then the additive meshes.
- **Projective textures** on pop-ups (s, t, q) are divided per pixel.


## 6 Sound

A NintendoWare sound archive (`CSAR`) with sequences (`CSEQ`), banks
(`CBNK`) and wave archives (`CWAR`/`CWAV`), and `CSTM` streams (DSP-ADPCM).

- **Effects.** The 469 effects use only nine sequence commands and run in
  real time with their bank instruments. Pan follows a square-root law
  (centre 1/√2 per side).
- **Effect names** by player: `SY_000_%03d` below 200, `SI_<set>_%03d` from
  200. Service 11021 asks whether anything sounds on an effect player.
- **Streams.** The scripts' six players map onto three (table `0x3E69B8`).
  A play only prepares unless auto-started; 11049 starts or resumes, 11050
  pauses, 11043/11044 stop.
- **Event voices**: `EVT_%02d_%06d_%04d` (group, event, line) on stream
  player 1, with the music lowered under them.

Checked against a console capture: streams correlate 0.985 and effect
waveforms match.


## 7 Movies

`.moflex` files: a run of 4,096-byte blocks.

- **Sync headers** every 3 s: `L2`, a checksum (`0xAAAA ^` the time's
  halfwords), a 64-bit time in µs.
- **Stream descriptors** with varint type and size: 1/3 video, 2 audio,
  4 timeline.
- **Packets** carry a bit-packed stream number, an optional time stamp (which
  ends a frame; tag 0 = key frame) and a 13-bit size.

Video is 400×240 stereo (left eye first, both eyes through one decoder)
at 48000/1001 pictures a second. No movie has an embedded audio stream; each
has its own `MOV_NNN.bcstm`.

**The codec is the DS frame routine** with new partition tables, the
quantizer clamped to 12..52, a P frame before any quantizer taking 12, a
32-bit dequantization multiply and runtime strides (three copies of the
routine, one per stride).

- **Post filter** (`0x165F88`): a deblocking filter with alpha/beta by
  quantizer. Its chroma compare is signed, so a difference ≥ 128 counts as
  small. The console filters only while six or more decoded frames wait.
- **Colour.** The Y2R hardware converter with its standard coefficient set 2.
- **Clock.** The wanted pair = ticks × rate / 59.831; the console skips
  ahead when more than two pairs behind.
- **Subtitles** come from `txt/ge/15/15_000NNN.xs` (`1001 tick speaker
  text`), on the same clock.

Two audio codecs (FastAudio and an IMA-style ADPCM) are in the code but
unused by the movies.


## 8 Loops and the story

**Loop factory** (`0x2C592C`):

| Id | Loop | Id | Loop |
|---|---|---|---|
| 1000 | game | 3000/3001 | puzzle session |
| 1200 / 1300 | event / place | 3010 / 3020 | puzzle title card / verdict |
| 2000..2050 | logo, title, trial, name, finish | 3100..3280 | play loops |
| 2100..2170 | bag, investigation, mysteries, index, episodes, staff roll, collection, rabbit select | 3500..3690 | challenge loops |
| 2200 / 2210 | profile edit / view | 4000 / 4100 | Wi-Fi |
| 2300 / 2500 | coin shop / train | 5000..5260 | secrets |
| 6000..6005 | rabbit | 6100 / 6200 / 6300 / 6400 | robot / shop / horse ride / ruins |
| 7000..7020 | save errors | | |

**Loop lifecycle.** start → Init (once in the loop's life, in its own scope)
→ resume (which waits for Init) → Main. Returning from an event reloads the
place script and restarts Main, but not Init. Requests (pop, restart, push)
are queued.

**Game loop**: one display frame per game frame; resident scripts
`prog_common`, `prog_saveload`, `prog_memo`, `prog_event` and the sky; two
fonts. The place loop runs at 30 Hz (two display frames per game frame).

**Story tables** (`lt5_def`):

- **Steps** (2201..2204): the story advances while the current step's
  conditions hold.
- **Event kinds and branches** (2211..2216).
- **Events** (2321 {group, number, name, kind, sound set, puzzle, note,
  episode, place, step}).
- **Places** (2142..2146: conditions, rooms, exits); **rooms** (2121);
  **areas** (2132..2134).
- **Named conditions** (2221/2223): a bit set recomputed when a place resumes
  or an event ends.
- **Items** (2021).
- **Conditions** cover flags, variables, puzzle states, found and solved
  totals, events, steps; **effects** set flags, variables, puzzle states,
  items, place, step and event marks. Flags 16200..16203 take an index: the
  bit is the named flag's first bit plus the index.

**The guide.** Every place's way to the current step's goal place is found
by a depth-first relaxation over the place exits. Area maps draw routes as
polylines.

**Investigation.** A pad cursor moves a magnifier by drag × speed; 3022 picks
3D objects by ray.

**Rooms.** A room's mesh layers are split between `MAP_%04d` and
`MAP_%04d_NEAR`. Place changes build the new room on another screen and swap
it in.

**The "DX" flag** is set only for language index 0 (Japanese), so the "DX"
events stay off in this version.

**Puzzles left behind.** Entering a step marks every unsolved puzzle whose
record names it (56 of 150) and adds it to the passed-by list for the
puzzle hut.

**Ending**: step 900 plays the ending, then the staff roll, "Ende", the save
of the completed game and the title.


## 9 Puzzles

**Data**: one script per puzzle (`res/naz/seq/nNNN.xq`, 167, plus 20 special
scripts), the catalogue `lt5_def` 2301 (150 rows: id, Japanese name, title,
type, picarats), parameters `lt5_naz.xb/nNNN.xs`. A puzzle's number is not
its id.

**Session** (3000): unsolved → state 1 and the hints it had; after the
verdict, 1 marks it solved (counts, items, picarats by failures), 3 counts a
"later". The new-game state (`lt5_def` 2201 0: 10 hint coins) is applied at
every puzzle start.

98 puzzles run on the standard loop. Special loops (picked by type; the
script talks to them with 2110; input hook after the touch dispatch, update
before the scripts):

| Type | Loop | Puzzles | Rules |
|---|---|---|---|
| 9 | 3200 | 3 | sliding lines on a grid; undo 30 moves; solved when each piece's target cell holds it |
| 10 | 3120 | 6 | edges between grid dots, drawn and erased by sliding |
| 11 | 3130 | 4 | pairing lines between points of a group |
| 12 | 3170 | 3 | sliding blocks, one pixel per step along the major axis, at most 8 a frame |
| 13 | 3230 | 2 | gliding along lines over a tall map, jumping at crossings |
| 14 | 3110 | 8 | placing pieces made of rectangles, with a flip button |
| 15 | 3220 | 5 | piles dropped onto a grid |
| 16 | 3240 | 2 | floating blocks moved by the D-pad or by tilting the console |
| 20 | standard | 2 | mummies on sliding tracks, in real time |
| 27 | 3140 | 3 | jump-over game with cats (vanishing pieces) |
| 33 | 3150 | 3 | the corn cob: an insect walks a 24 × 24 map that wraps round the cob, with warps |
| 34 | 3160 | 4 | bricks knocked by a hammer slide until they meet something; bricks over a hole fall |
| 35 | 3180 | 3 | sliding penguins leave through the border ring |
| 36 | 3190 | 3 | flipping cats: a jump over a line of cats changes their colour |
| 46 | 3250 | 1 | stroke lines between dots on square or staggered grids |
| 47 | 3270 | 2 | balloon strings: buttons swap knots between columns |

**Written answers** (16 puzzles): a canvas per box, recorded in mode 16 and
recognised at pen-up.

- The recogniser library (`0x120D94..0x121620`, `0x23DD84..`, tables in the
  code segment) segments strokes, pre-selects classes and matches by DTW.
  Rule programs, composites and reranking follow.
- Up to 32 candidates are converted to Shift-JIS; the first is kept, "?" for
  none.
- Databases `res/hwr/alnum_u.bin`, `hira_u.bin`, `kata_u.bin`.


## 10 Side games

- **The ruins** (chapter 6, loop 6400): a grid of 70 × 50 cells from floor
  files (`mini/$/ruin_floor_0N.xs`: room parts, models, cells, events,
  objects, rooms).
  - The protagonist can dig (a hole; treasures appear), bury, push blocks,
    pull levers and turn in place.
  - Blocks slide a cell per 12 frames, drop, press plates and crush mummies.
  - Mummies notice the protagonist along rows and columns, narrowed by walls,
    and chase the protagonist by a best-first search.
  - Paired switches need both down.
  - Guide files hold the official demonstrations (DX only).
- **The rabbit** (6000..6005): a hub with a motion machine, care (Commu), a
  notebook, show puzzles and shows.
- **The robot game** (6100): pieces on a board move by their types' script
  blocks, run on the pieces' handler threads; stages from
  `lt5_sub_robot*.xs`.
- **The shop** (6200): goods of cell masks placed on a 15 × 7 shelf grid;
  customers walk to neighbouring goods that share a colour or kind.
- **The horse ride** (6300): a drum course of Bezier bends with barrels,
  coins and carrots in three lanes, and a masked rider choosing random ways.
  Steering is the script's: a slide of more than 8 pixels changes lanes.
- **The train** (2500) is a script loop.


## 11 Saves and the secrets

**Saves.** 10101 saves the progress block (0x866C bytes) and a slot record:
name, in use, finished (1/2/3 by solved story and challenge puzzles),
seconds, picarats, step, place, puzzle counts, coins. 10100 loads.
Continuing opens with the step heading's recap event. The port keeps its own
slot files.

**Secrets** (5000..5260): profiles (native), gallery, music, voices, videos
(script loops) and the secret door. The door's password (1300) is the CRC-32
of a key and the console's six-byte wireless address, spelled through an
alphabet. With an all-zero address the password is "HUSEWYCT".


## 12 Bugs and oddities in the original

- **Missing blocks.** Some scripts call blocks that no script has (a NazoObj
  block in type 27, blocks after item pick-ups); the console's call fails too.
- **Shop anchors.** The anchor search reads past a row's end into the next
  row and past the grid into the stage number; a good may overhang the right
  edge.
- **Robot game.** Command 30 keeps a value only when it is larger than the
  list's count; equal sort keys trade places.
- **Ruins end.** Leaving the ruins queues one more pop.
- **DX content.** It exists in the code but is enabled only for Japanese.
- **Silhouettes.** A `.sil` record with key 0 draws no outline.
- **Post filter.** The chroma's signed compare filters large differences.
- **Strings.** The code already contains the string `lt6_plc_chr_env_eff`
  of the next game.


## 13 Research tools and checks

Research tools live in `.analysis/adventure56/tools/` (local):

| Tool | Purpose |
|---|---|
| `ctr.py`, `xarc.py`, `imgc.py` | image survey, archives, pictures |
| `ghidra_ctr.py`, `dis5.py`, `xq_dump.py`, `blk5.py` | code and scripts |
| `pica_cmds.py`, `pica_draws.py`, `pica_shader.py` | GPU state per draw from console dumps |
| `xqtrace5.py`, `lockstep5.py`, `trace5.sh` | console script-command traces aligned with native runs |
| `solves5.txt`, `solve5.sh`, `regress5.sh` | every puzzle solved natively; frame hashes |
| `solvers/` | per-type solvers |
| `a5_pilot.hpp` | the story autopilot |
| `moflex_oracle.cpp` | the ROM's movie routines in an ARM interpreter |

Native runners: `picarat_a5_run` (puzzles) and `picarat_a5_story_run` (story,
with `A5_PILOT`).

What was checked:

- All 150 puzzles reach their verdict natively.
- Traced puzzles match the console command for command.
- All 214,541 movie frames of both 3DS games match the ROM's own decoder and
  post filter.
- The autopilot plays from the title to the end credits.
- The handwriting recogniser is byte-exact against console dumps.
- Side games and secrets match console runs in the stretches compared.
