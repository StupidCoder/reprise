# Lost Future (Nintendo DS): how the game works

What we learned while reverse engineering the third adventure (German
cartridge `C3JD`) for the re-implementation in `games/layton`. It covers the
cartridge, the all-new file formats, the LSCR script machine and its scene
services, the loop stack that runs the game, the puzzles and their special
engines, the minigames, sound, saves, and the original's own quirks.
Remaining work is in [plan.md](plan.md).

All findings come from the user's ROM and from runs of it. Sources were Ghidra
decompiles of the overlays (`.analysis/adventure3/tools/ghidra_c3jd.py`), a
disassembler with resolved literals (`dis2.py`), dumps of the script and table
files (`lscr_dump.py`), and statement traces from the research harness, which
is used only as a measuring instrument. Findings from the second adventure
were used only as hypotheses and re-checked against this ROM. No disassembly,
decompilation, asset map or format description of this game was consulted.
Addresses are guest addresses quoted as evidence; the port holds no guest
state. Statements marked **hypothesis** are not confirmed. Detailed notes:
`.analysis/adventure3/docs/` (`assets.md`, `script-machine.md`,
`puzzle-engines.md`, `story.md`, `minigames.md`), local only.

Contents: 1 Cartridge and program · 2 Data formats · 3 The LSCR script
machine · 4 Scene services · 5 The loop stack and the story · 6 Puzzles ·
7 Minigames · 8 Sound and streams · 9 Saves · 10 Bugs and oddities in the
original · 11 Research tools and checks


## 1 Cartridge and program

| Field | Value |
|---|---|
| File | the German cartridge image (`... (Germany).nds`) |
| SHA-256 | `e8d72745b7530ba3cf2fa713d1840d0f9aebb20a835f2746f32b6bdcd8ea373b` |
| Size | 256 MiB |
| Header | game code `C3JD`, maker `01`, revision 0 |
| ARM9 | 0x02000000, 185,144 bytes expanded |
| Overlays | 48 (0–47), all compressed; 43–47 are 32-byte stubs |
| Files | 3,945, all game data under `lt3/` |

Build leftovers on the cartridge:

- `lt3/copy_{ge,fr,it,sp,du,uk}.bat`, which copy English art into the
  language folders;
- `rc/dbg/lt3_ver.cfg`: version "0.81", revision 099, dated 2010-08-31;
- debug fonts and a debug table `rc/dbg/ge/lt3_dbg.cpck` (it holds a
  phase-to-place table, useful for automatic play);
- empty `dummy.cbin` event folders.

The language folders are `ge` (1,233 files) and `eu` (605 puzzle pictures
shared by the European languages).

**Code shared with the second game** (32-byte window similarity, not proof of
identical behaviour):

| Module | Shared | Matches the second game's | Role |
|---|---|---|---|
| ARM9 | 96 % | ARM9 | SDK, runtime, base services |
| ov1 | — | sound driver (tables byte for byte the same) | sequencer |
| ov2 | 99.6 % | ov1 | Mobiclip decoder |
| ov3 | 100 % | ov0 | handwriting recogniser |
| ov4, ov5 | 99.8 / 98.5 % | ov4 | Wi-Fi library |
| ov0, ov6 | 4–6 % | — | new engine |
| ov7–ov42 | 3–50 % | — | new loops and engines |

The platform layer carried over. The engine, its scripts and all file formats
were rewritten.

| Overlay | Role |
|---|---|
| 0 | loop scheduler, script machine core |
| 6 | game state, objects, message manager, loop factory (table at `020EC1F8`) |
| 9 | the 492 script command handlers and the condition tree |
| 18 | dictionary |
| 19 | picture book |
| 20..23 | toy car (select, edit, run) |
| 24 / 25 / 26 | parrot event / menu / game |
| 35 | puzzle loop |
| 36..42 | special puzzle engines |


## 2 Data formats

**Compressed files** (`.c????`): a 4-byte header, low byte the type, upper 24
bits the expanded size. 0x10 is LZ10 (nearly all files), 0x30 run-length and
0x00 stored: 15 puzzle pictures whose bytes follow the header unchanged,
padded to 16. **Hypothesis:** the game's loader treats 0x00 as stored; the
rule fits all 15 files, but the loader was not located.

**LPC2 archives** (`.cani`, `.cpck`, `.cdat`, `.csnd`):

| Offset | Field |
|---|---|
| 0 | `LPC2` |
| 4 | member count |
| 8 / 0x18 | data offset |
| 0xC | total size |
| 0x10 | member table (12 bytes each: name offset, data offset, size) |
| 0x14 | name pool |

A `.cani` holds one `ani.lbin` and its pictures. A `.csnd` holds SMD+SWD or
SED+SWD pairs.

**LSCR command lists** (`.cbin`, `.lbin`; 3,029 files): `LSCR`, u16 command
count, u16 header size (16), u32 operand table, u32 string pool. A command is
8 bytes {u16 opcode, u16 operand count, u32 first operand}; an operand is 5
bytes {u8 type, u32 value}. Operand types: 0 integer, 1 float32, 2 string,
3 a name resolved at compile time, 4 a variable. The same container holds
scripts, text, tables and animations:

- **Animations** (`ani.lbin`): 1010 picture list, 1020/1022/1021 frame
  begin/layer/end, 1030/1032/1031 sequence begin (name, origin, touch
  box)/step/end.
- **Text** (`txt/ge/*.cpck`): 6000 header, 6001 {kind, speaker, text}. Text
  is ASCII with `{xy}` accent escapes (an 82-entry table at ov0 `0206DAA8`
  maps them to Windows-1252) and `<T>`, `<A1/3>`, `<CR>`, `<CS>` markup.
- **Tables**: catalogue, positions (`lt3_pos`: name, x, y, layer on a
  256×384 plane), sound sets (`lt3_snd`), game-state declarations
  (`lt3_gmd`), minigame data (`rc/mode/*`), rooms (`rc/obj/p%04d`).

**LIMG pictures** (`.cimg`, `.limg`; 2,167):

| Offset | Field |
|---|---|
| 0 | `LIMG` |
| 4 | palette offset |
| 8 / 0xA | parts offset / count (8 bytes each) |
| 0xC / 0xE | pieces offset / count (3 bytes each) |
| 0x10 / 0x12 | tile map offset / count (u16 per 8×8 cell, 0xFFFF empty) |
| 0x14 / 0x16 | tiles offset / count |
| 0x18 | format: 0 = 4 bpp, 1 = 8 bpp, 2 = A3I5 |
| 0x1A | palette colours |
| 0x1C / 0x1E | width / height |
| 0x20 / 0x22 | power-of-two area |
| 0x28 / 0x29 | log2 of the area in tiles |

Tiles are de-duplicated and have no flip bits. Pieces use the twelve DS OBJ
shapes. Format 2 (A3I5: 3 bits of alpha, 32 colours) is used for character
portraits, which are drawn as 3D textures with soft edges.

**Unchanged from the second game**: SAD streams, SWD/SMD/SED sound (version
0x0415), MODS movies and NFTR fonts. The handwriting database
`dcm/scrdb_eu.cdcm` is byte-identical to the second game's.


## 3 The LSCR script machine

The machine lives in overlay 0 (dispatch `0204FF54`, frame `02050678`,
thread run `02051898`, call `02051A0C`, tail call `02051B24`). The command
handlers are in overlay 9 (492 registered by its static initialiser,
conditions in one switch at `02103FE0`).

- **Compilation.** Files compile into sections: 1100 {name, argument count}
  … 1101; statements 1110 {opcode, operands}; labels 1111; 1102 names a
  variable or constant for the section or file. All 3,029 files compile
  (4,328 sections, 50,764 statements).
- **Values.** Int, float (IEEE single through the game's software float
  library), text, name, reference. int∘int stays int and wraps; otherwise
  float. float→int truncates toward zero, saturating. Comparisons: numbers as
  doubles, texts by `strcmp`; different kinds are neither equal nor unequal.
- **Variable slots.** 0–15 frame locals, 100–111 arguments, 200–215 the
  thread's own, 300–315 globals.
- **Frames.** At most 8 per thread; a ninth call is ignored and the caller
  goes on.
- **Execution.** `pc += delta; delta = 1; run`. Results: Next, Called, Wait
  (stop for this frame), Return, Stop. The clock grows by 4096 a frame.
- **Core commands.** 1000 wait a frame; 1010/1011 stop; 1020..1022 return
  (if/unless); 1030..1032 wait forever/while/until; 1040..1043 jumps
  (random included); 1101/1102 call/tail call; 1300..1357 arithmetic, bit
  operations, min/max/clamp, floor/ceil/round, square root.
- **Random.** The C library's `rand` (`0202070C`): 1323 int = `rand % n`,
  float = `n·rand/32767`.
- **Game state.** 5501 declares flags {bit, name}: bits below 0x600 are
  saved, the rest temporary. 5511 declares variables {byte offset, size
  (negative = signed), write-protected, name}: offsets below 0x240 are saved.
  Commands 1510–1513 handle flags and 1520–1527 variables; conditions
  1510–1527 test them. Variable names are Japanese strings read from the
  overlays' code.
- **Unknowns.** The original ignores unregistered commands and treats unknown
  conditions as true. The port stops with an error instead.


## 4 Scene services

- **Fixed point** is 20.12. Script floats are truncated into it, and division
  uses the hardware divider, rounded. The sine table is
  `round(sin(2πi/4096)·4096)`.
- **Tweens** {curve, remaining, total, value, target, start}, with
  `f = remaining / total`. Curves: 1 `f`, 2 `1−(f−1)²`, 3 `f²`, 4 in-out
  quadratic, 5 `sin(180°·f)`, 6 `4f(1−f)`, 7 endless `(1+cos 360°f)/2`.
  Brightness is a linear tween per screen, shown as `floor(level·16)`.
- **Objects** are created by name in the current scope level. A new object
  gets layer 3. The draw list is sorted with high layers first; re-layering
  inserts after equal layers. Each object has four switches (shown, input,
  moving, a fourth) with save stacks: 3010 pushes them and deepens the scope,
  3011 restores them and deletes what the level created.
- **Touchable objects.** Buttons (4018) own a thread: press runs the T
  section, leaving the area N, lifting inside C. Switches (4016) run one
  section on press. Pieces (4021): N released, T carried, C turned. Input
  goes front to back, and the object that took the pen comes to the front of
  its layer. An object without touch areas is hit where its shown frame has
  pixels, by 8×8 tiles.
- **Pen places.** Object handlers read the pen's place from the frame
  *before*. The scene's input record keeps where the pen came down, the
  current place, and a smoothed place `(3·current + previous)/4`.
- **Frame order.** Input to objects, the scene thread, then per object (in
  creation order) its thread and its movement; brightness steps after the
  scene thread.
- **Pieces and points.** Rotation in fixed-point radians (0x3244 = π), snapped
  the short way round with steps. Grids snap to the cell under the pen. Anchor
  points are free or attached to an object; the nearest point is searched with
  Manhattan distance < 512. Objects stack on the object their point is
  attached to.
- **Circling.** The anchor points inside the ink's bounding box are counted;
  exactly one point is the answer.
- **Ink.** Every pixel within half the pen width of a segment (round ends).
  Ink is drawn three frames late, and what is pending at the release is
  dropped.
- **Handwriting boxes** (ov6, vtable `020F6554`):
  - A press inside starts a stroke; a point is added when `|dx|+|dy| > 2`.
  - Lists hold 256 points and 32 strokes.
  - Recognition uses the shared recogniser: categories by mode, four
    candidates of which only the first character is kept, mapped to a byte
    through `dcm/code.lbin`; no candidate gives "?".
- **Messages** reveal two characters a frame. Command 4645 sets the typing
  sound and its interval.
- **Stage manager.** Per screen: picture, parts, scroll, and a 3×3 colour
  matrix filter with a level (lerp to white or black). Filters and sprite
  tints are built as `toYiq · diag · fromYiq`. Setting a screen picture
  resets the filter. Messages draw after sprites.
- **Physics** (4023 fallers): walls are the id-4 touch areas of shown
  objects. Fallers are swept vertically, lowest first, with speed, gravity,
  bounce and landings.


## 5 The loop stack and the story

The game runs a **stack of loops** (scheduler `02064580`; push `020640A8`,
overlay `020640CC`, replace `020640F0`, pop `02063D7C`). The loop factory
table names each loop's program and class; its constructor names the overlay
to load. Commands 1280..1299 call the top loop's vtable at `0x58 + 4n`, and
conditions 2210..2217 at `0x98 + 4n`. Lifecycle slots: +0x38 start, +0x3C
end, +0x40 activate, +0x44 deactivate, +0xE4 after the pen dispatch, +0xE8
resources loaded, +0xEC update, +0xF8 / +0x100 draw lower / upper.

| Loop | What |
|---|---|
| logo, title, GAME | boot and the main loop |
| PLACE | rooms (objects from `rc/obj/p%04d`) |
| EVENT | events; closing actions run on a data-only machine |
| NAZO | a puzzle (section 6) |
| MOVIE | movies |
| NAME_RESIST | name entry |
| MENU_BAG | the bag, save and load |
| mysteries menu | the mystery list |
| MINI_PARROT_* | parrot menu, game and event |
| MINI_CAR_* | toy car |
| MINI_BOOK | picture book |
| MENU_NAZOBEE / MENU_NAZORINE | puzzle index |
| MENU_JITEN | dictionary |
| SCRT_* / WIFI_* | secrets, Wi-Fi |
| STAFF_ROLL | a script loop |

**Story conditions** (tree `02103FE0`): 1850..1859 compare the story phase,
1832/1833 hint coin found, 1890/1891 the current puzzle's state, 1900
companion flag, 2201 an object took the pen, 4300/4301 messages loaded.

**Rooms.** Touchable objects are tagged by kind (1 coin, 2 effect, 3 event,
4 way out, 5 text, 6 menu, 7 view). A found coin makes no object.

**Event confrontations.** Some events hold a judged arrangement: cards
dragged onto places, or a grid of toggles. The answer is data in the event's
own `BTN_JDG` section, sometimes with a second answer behind a flag.

**Ending.** Event 56000 plays movie 34 and requests STAFF_ROLL, which waits
until the movie's stream ends (about 7.7 minutes). Then:

- events 57000..60000 (movie 36);
- 60005, the ending song over two pictures (an extra branch when 153 or more
  puzzles are solved);
- 75290;
- 75350, saving in the power room;
- the title.

**Text tables.** Command 3321 binary-searches the id in the table's index. A
fifth operand picks a row within the id's rows; tables keep several rows per
id (the parrot stages' title, text and rules; mystery description and
solution).


## 6 Puzzles

The puzzle scene is not a set of native engines as in the second game. The
loop (ov35, class `lt3CLoopNazo`) runs the game's scripts:

- **Title card** (`lt3_nazo_start` MAIN).
- **Puzzle screen** (`lt3_nazo` NAZO_MAIN, with the hint, quit and input
  programs, and the puzzle's own script `rc/nazo/naz_sc<id/100>/nNNN.lbin`
  with sections NAZO_RES, NAZO_OBJ, NAZO_RST, NAZO_INP and NAZO_JDG).
- **Verdict** (`lt3_nazo_result` MAIN), unless the verdict variable is −1.
  A 2 means "try again".

The puzzle's header (3050) sets the answerer, help page, sound set and
background. Text files hold the statement, correct, wrong, three hints and
the super hint.

**Catalogue** (`lt3_nazo.lbin`): 224 rows {number, developer label, title,
record, type, type label, picarats for 0/1/2+ wrong answers, title picture}.
Puzzles 169–222 are Wi-Fi puzzles. 19 of them have empty German texts.

**Records** keep 3-bit failure and hint counts. **Hints**: tabs 1–3 and a
super hint; tab N exists once hint N−1 is open; a hint costs one coin, the
super hint two. **Memo**: a paint object (`lt3CPaintMemo`) runs
`evt/lt3_memo` MEMO_BEGIN, MAIN and END; ink is palette entry 0xF0 + n. The
eight ink colours were measured on the ROM's screen; their source was not
found. Commands 1294/1295 remember and restore movable pieces across leaving
the puzzle.

**Types** (number, count):

| Type | Count | Engine |
|---|---|---|
| 1 written answer | 59 | script + handwriting boxes |
| 2 circling | 25 | script + circling canvas |
| 3 multiple selection, 4/20/21 selection | 24 / 34 / 8 / 3 | script |
| 5, 7 arrangement | 17 / 2 | script, pieces and points |
| 8 matchsticks | 3 | script |
| 9 lines | 9 | CUT (ov36): a grid of points; the pen toggles edges as it steps to neighbours |
| 10 lines | 3 | LINE (ov37): lines between anchor points; drawing an existing line or tapping it within 8 frames removes it |
| 11 sliding blocks | 17 | SLIDE (ov38): blocks move a pixel at a time toward the pen, at most 8 steps a frame, longer axis first; solved when every goal is covered |
| 12 tangram, 18 and 22 arrangement | 4 / 1 / 2 | LTGRAM (ov39): pieces with four turn steps and a mirror button; solved when every target cell's corner lies in an id-2 area |
| 13 bread rolls, 15 frogs, 16 tumbling blocks, 17 bombs | 1 / 2 / 3 / 1 | script and physics |
| 14 bananas | 3 | BANANA (ov40): 12 × 10 cells of 16 px (wall, goal, hole, ice); slides over ice to a wall |
| 19 weighing | 3 | SCALE (ov41): weighs what is linked to the dish points; counts new weighings (at most 999); tilts 0.3 rad |
| 21 grid | 3 | GRID (ov42): pieces with four turn steps on a grid |

Engine hooks: command 1280 + k goes to scene vtable `0x58 + 4k`. The
engines' "before" step (0xC0) runs before the pen dispatch, and their "after"
step (0xE4) after the objects. Engine drawing is 3D: above pictures, beneath
sprites.


## 7 Minigames

**Parrot** (ov25 menu, ov26 game; stages in `rc/mode/lt3_mini_parrot`). The
player stretches ropes between pegs (a rope may not cross another or enclose
a peg); START sends the parrot.

- One flight step (`021245CC`): a hop parabola or free flight.
- The body and head are tested against the ropes, then the walls.
- Collisions back out along the velocity, halving each time.
- The bird bounces, perches or gets stuck (more than 10 frames touching, or
  resting on a slope).
- Flight codes: 0 nothing, 1 perched, 2/3 bounced, 4 off the screens, 5 stuck,
  6 delivered.

**Toy car** (ov20..23, data `rc/mode/lt3_mini_car`): pieces (four arrows and
a jump) placed on course cells. Cell kinds have a height, sprites and an
arrival section. Each course owns slots of a game variable (a piece's cell
+ 1, then the car's direction + 1). The run is drawn isometrically:
`x = 22X − 17Y`, `y = 11X + 17Y − 24Z`, depth `−11X − 17Y − 12Z` around
(128, 96). Leaving the course restarts the run at GAME_OVER_EMPTY.

**Picture book** (ov19, data `rc/mode/?/lt3_mini_book`): books, pages, lines
with gaps and spots, and stickers that fill a gap in one of twenty word
forms. It talks to its script through work variables 0..10. When every
sticker of a book is on its own gap, the book's flag is set.


## 8 Sound and streams

The game keeps the second game's sound driver (overlay 1 at `02072660`, the
same tables) and sound object design. Files are
`snd/<G>_<n>/<G>_<n>.{SED,SWD,SMD}` with large shared wave banks
`snd/WAV_<G>.SWD`.

- **Sound sets.** Commands 4800/4801 select a set from `lt3_snd` rows {set,
  music, GE bank, SI bank}. Flags 0x200/0x400/0x800 skip SY/GE/SI, and 0x100
  skips the music.
- **Effects.** 4820 plays and 4821 stops. Groups are SY < 50, GE < 200,
  SI < 1000.
- **Music** (BGM object `0207217C`). The same music already loaded plays
  again with a 60-frame fade; otherwise it is loaded and played. Commands
  4830..4835.
- **Streams** (object `020721C8`): 4851 music route, 4852 ordinary
  (`str/nrm`), 4853 film, 4854 event voices
  (`str/evt/?/%02d_%06d_%04d.SAD`: group, event, line). 4860 starts, 4861 and
  4850 stop. Status: 4810–4814 write it into variables; conditions 4810..4813
  test it.
- **Refills.** A stream asks for half the ring per refill in the format's
  blocks: 512 bytes for 0xB2 and 1024 for 0xB4. This game's voices are 0xB4.
- **Engine sounds.** The slider's move sound stops after a still frame. CUT's
  tracing sound follows the marked point. LINE plays start and end sounds,
  and 0x90 on refusal.


## 9 Saves

Header `020DDCC8`: holds-data bit, play time (capped 99:59), puzzles found and
solved, clear status (0..3), player name and place title. Commands: 1690
fields, 1691 load {slot, out}, 1692 save. The port keeps the three slots as
`c3jd-saves/slot0..2.sav` (its own container, magic `PCR3SAV1`, not the
cartridge layout).


## 10 Bugs and oddities in the original

- Unregistered commands are ignored and unknown conditions count as true.
- A ninth call frame is silently ignored; the caller goes on.
- Numbers below 1000 used as conditions are true unless 0.
- Animation steps that name missing frames show nothing (puzzles 20 and 129).
- A wait on condition 3000 (loading finished) always lasts into the next
  frame.
- Upper-screen objects show at 15/16 of their colour (blend weights 15, 0),
  while the screen picture does not. Measured; the register is not identified.
- Puzzles 189, 192, 195, 200, 206 and 217 expect kana, which the European
  code table cannot produce, so they cannot be solved in this version.
- A refused name showed only its window (whose text is part of the picture);
  the port briefly drew another message over it.


## 11 Research tools and checks

Research tools live in `.analysis/adventure3/tools/` (local):

| Tool | Purpose |
|---|---|
| `ghidra_c3jd.py` | the Ghidra workspace |
| `dis2.py`, `litdump.py` | disassembly and string literals |
| `lscr_dump.py` | script and table dumps |
| `opcode_table.py` | the command table |
| `rom_trace.py`, `compare_traces.py` | statement traces against native runs |
| `compare_scene.py` | frame comparisons |
| `a3_regress.py PACK` | replays every recorded trace |
| `slide_solve.py`, `banana_solve.py` | solvers |

Native runners: `picarat_a3_loop_run` (puzzles), `picarat_a3_story_run` (the
story, with the automatic player `PICARAT_AUTO`) and `picarat_a3_lscr_probe`.

What was checked:

- Puzzle 6 runs statement for statement like the ROM through the whole
  correct-answer run; pictures are within ~1.5 %.
- Puzzles 1, 2, 11, 18, 19, 32, 67, 106, 118 and 182: statement traces equal
  to the ROM through the verdict.
- All 224 puzzles pass the static support check and a fuzz timeline.
- The automatic player walks from the start to the credits without stand-ins.

## Embedding notes (Reprise, 2026-10-29)

Observations from running the reconstruction inside Reprise, not new
findings about the original: the game thread is stepped one frame at a time
by the host, and the sound is rendered a fixed amount per frame (32 728 Hz
resampled to 48 kHz), so every sound-gated wait follows the frame count
instead of the device's real-time progress. Reprise's quick save stores the game's automatic checkpoint - the slots' own record format, as `checkpoint.sav` written when PLACE_LOOP is entered - and continues through the game's own load path. It is a checkpoint, not a snapshot of the running machine. Details:
[../reprise.md](../reprise.md).
