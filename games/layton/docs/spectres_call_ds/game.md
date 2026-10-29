# Spectre's Call (Nintendo DS): how the game works

What we learned while reverse engineering the fourth adventure (German
cartridge `BLFD`) for the re-implementation in `games/layton`. It covers the
cartridge and the new `gn` engine library, the archive and data formats, the
GSS1 script machine, the scene engine, the loop stack and the story's
bookkeeping, the puzzles and their special loops, the minigames, the bonus
section, sound, saves, films, and the original's own quirks. Remaining work
is in [plan.md](plan.md).

All findings come from the user's ROM and from runs of it. Sources were Ghidra
decompiles (`.analysis/adventure4/tools/ghidra_blfd.py`), disassembly
(`dis4.py`), dumps of scripts and data lists (`gss_dump.py`, `gsd_dump.py`)
and runs in the research harness, which is used only as a measuring
instrument. Findings from the earlier adventures were used only as hypotheses
and re-checked. No disassembly, decompilation, asset map or format
description of this game was consulted. Addresses are guest addresses quoted
as evidence. Statements marked **hypothesis** are not confirmed. Detailed
notes: `.analysis/adventure4/docs/` (`assets.md`, `script-machine.md`,
`scene.md`, `story.md`, `puzzles.md`, `special-loops.md`, `minigames.md`,
`sound.md`, `bonus.md`), local only.

Contents: 1 Cartridge and program · 2 Data formats · 3 The GSS1 script
machine · 4 Scene engine · 5 Loops and the story · 6 Puzzles · 7 Special
puzzle loops · 8 Minigames · 9 Bonus section · 10 Sound and films · 11 Saves
· 12 Bugs and oddities in the original · 13 Research tools and checks


## 1 Cartridge and program

| Field | Value |
|---|---|
| File | the German cartridge image (`... (Germany).nds`) |
| SHA-256 | `f26b840d79ca67f9d7621c5bfbd30bb7db2f20a773b75360d81537bbdfdf35d2` |
| Size | 256 MiB |
| Header | game code `BLFD`, revision 0 |
| ARM9 | 0x02000000, 225,344 bytes with autoload sections |
| Overlays | 47 (0–46), all compressed; 41–46 are 32-byte stubs |
| Files | 514: 468 streams, 38 movies, 4 wave banks, three archives (`lt4_main_ge.fa`, `lt4_seq.fa`, `lt4_sub.fa`), `dwc/utility.bin` |

Almost everything sits in the three GFSA archives (4,460 files); only streams,
wave banks and movies are loose. The main archive's name carries the language
(`lt4_main_%s.fa`).

**The script machine runs from ITCM.** The ARM9 image's autoload copies file
bytes 0x31A00..0x36FE0 to ITCM `01FF8000`, and the whole script machine lives
there.

| Overlay | Role |
|---|---|
| 0 | sound driver (the same tables as the third game) |
| 1 | Mobiclip decoder |
| 2 | handwriting recogniser (same functions as the second game's, recompiled) |
| 4, 5 | Wi-Fi |
| 6 | the `gn` engine library: files, archives, compression, scripts, pictures, fonts, objects, messages, tweens, loop manager |
| 7 | the game: managers, objects, touch, places, progress, loop requests |
| 8 | boot |
| 10 | film process |
| 12 | the 517 service handlers |
| 13 | game loop |
| 14 | script loop |
| 15 / 16 | place / event loops |
| 17 | puzzle session and standard play loop |
| 18..27 | special puzzle loops |
| 29 | name entry |
| 32..35 | secrets |
| 36..40 | minigames |

Code similarity (32-byte windows; not proof): 27 % of this game's code
resembles the third game's and 24 % the second's. The middleware carries
over; the engine and its script interpreter are new. The third game's LSCR
interpreter has no counterpart here.


## 2 Data formats

**Compression** (ov6 `02080F10`): a 4-byte word, method in bits 0–2 and
expanded size in bits 3–31. Methods: 0 stored, 1 LZ10, 2/3 Huffman with
`1 << method` bits per symbol, 4 run-length. The bodies are the BIOS ones.

**GFSA archives** (`.fa`):

| Offset | Field |
|---|---|
| 0x00 | `GFSA` |
| 0x04 | directory table |
| 0x08 | file table |
| 0x0C | run-time table (expands to zeros; the game fills it) |
| 0x10 | name list |
| 0x14 | file data |
| 0x18 / 0x1C | directory count / file count |
| 0x20 / 0x24 | expanded sizes of the tables |

A directory (8 bytes) holds the hash of its name *with* the trailing slash,
its file count and first file. A file (8 bytes, sorted by hash) packs a hash,
a 28-bit offset and a 20-bit size. Lookup splits the path at the last `/` and
binary-searches both hashes. The hash is **CRC-16/X.25** (reflected 0x1021,
init and xorout 0xFFFF; table at ov6 `020B3F84`), confirmed on all 4,460
names, pack members and script block and label names. The name list is not
used by the lookup.

**Anti-tamper copy.** `lt4_seq.fa` and `lt4_sub.fa` are the same 52 scripts
except `lt4_menu_bag.cq`. The boot (ov8 `020EF440`) hashes part of the program
and mounts `sub` when the hash matches, `seq` otherwise. In the `seq` copy the
bag's "Save" fades out and returns to the title: a trap for modified
cartridges. The port reads `sub`.

**GFSP packs** (`.ca`, `.ci`, `.cse`, `.cb`): `GFSP`, a member count, tables
at offset / 4. Members (8 bytes): hash, offset / 4, size, high bits. Kinds:
`.ci` a picture (`IMG.lp` palette + `IMG.lt` texture), `.ca` an animation
(`AS.lb` + palettes and textures), `.cse` a sound bank, `dcm/dcm_en.cb` the
handwriting database and its conversion list.

**GSS1 scripts** (`.cq`, 1,310): blocks (functions) and labels indexed by name
hash; commands of 12 bytes {first operand, operand count, loader flag,
destination register, opcode, handler}; operands of 5 bytes {kind, value}.
Opcodes ≥ 1000 are bound to native handlers by the loader. There is one
script per puzzle (`res/naz/seq/n001..n229`), every menu, minigame and event.

**GSD1 data lists** (`.cb`, `AS.lb`; 1,540): a compact LSCR with 6-byte
commands. Examples: puzzle texts (3000 header, 3001 question, 3002 correct,
3003 wrong, 3004–3007 hints), `res/ge/lt4_def.cb` (catalogue, flags,
variables, steps, events, items, sound sets, rooms), a debug menu list.

**GPLT palette**: `GPLT`, colour count, RGB555 colours.

**GTXT picture**: format codes 0 direct, 2/4/8 bits, A5I3, A3I5 (character
parts), area shifts and sizes, parts and pieces as in the third game's LIMG
(the twelve DS OBJ shapes), a tile map with 0xFFFF for empty cells, then the
tiles.

**GFNT font** (`.cf`): cell size, a glyph table sorted by code (ASCII /
Shift-JIS) and bitmaps. Fonts: `main` 12×11 (7,127 glyphs), `font_en1`,
`font_en2`, `rubi` (80 kana). In `main` the 16-bit bitmap offsets wrap once.
**Hypothesis:** true offset = stored + 64 KiB per wrap in table order.

**Animation packs** (`AS.lb` run through a 14-entry handler table): images
built from texture parts (1010/1012), composites of images (1020/1022), timed
sequences (1030/1032, with a "next" sequence), palette and texture members.

**Text** uses `{xy}` escapes (89 entries at ov6 `020B4790`, e.g. `{:a}` →
0xE4) and tags: `<C?>` colour (B C G N O R S T W), `[base/ruby]`, `<#n>`
icon, `<W..>` waits, `<L s.f>` timed pause, `<T>..</T>` tap-to-show and more.


## 3 The GSS1 script machine

- **Machine** (ITCM): frame run `01FFB6E4`, core commands `01FFB88C`,
  register lookup `01FFB424`. The loader binds opcodes ≥ 1000 through a sorted
  {opcode, function} table filled by overlay 12.
- **Script cache.** Every loaded script links to the one loaded before it, so
  a block lookup searches the newest script first.
- **Operand kinds**: 1 int, 2 unsigned, 3 float, 4 register, 8 text, 9 name
  (0x10 = the value is a file offset).
- **Registers**: ≥ 4000 the file's values, 3000.. the block's arguments,
  2000.. locals, 1001.. temporaries, 1000 the caller's destination, 0..999 the
  thread's values. A command whose destination register does not exist is
  skipped entirely. A missing register operand reads as its own number, which
  counts as true when not negative.
- **Execution.** The innermost frame runs first, with `pc++` before the
  command. Results: 0 next, 1 wait, 2 return, 3 stop. A waiting block's
  temporaries are cleared. Calls run at once in the same frame.
- **Calls** (20/21 name, args): searched from the thread's script on. A block
  needing more arguments than given is not called. The destination gets 1 or
  0 and is overwritten later by the callee's return value.
- **Core commands.** 10 wait, 11 return, 12 stop, 30 jump back and wait while
  true, 31 jump to one of n labels at random (draws a number even for one
  label), 32/33 jump if/unless, 100.. assignment and logic, 130..135
  comparisons, 150..154 arithmetic (text + anything joins), 160..171 bit
  operations, 501 format (sprintf per segment), 502 full-width digits, 503 a
  Shift-JIS character, 520 random, 600..623 maths (sin, cos, tan and atan2
  via the ARM9 library).
- **Library behaviour.** Int division by zero gives the dividend and
  remainder 0. Float to int truncates toward zero and saturates.
- **Random numbers.** xorshift128 at ov6 `020B66E0`; `w % n` (n = 0 gives w);
  float = `(w & 0xFFFFFF) / 16777215 · x`. When the game seeds it is not
  established.


## 4 Scene engine

The game object (`*(frame + 8)`) holds managers:

| Offset | Manager | Services |
|---|---|---|
| +0x18 | language index (5 = `ge`; ov6 table `020B51EC`) | |
| +0x18C | game data: flags, variables, puzzle records, picarats | 30xx, 34xx, 39xx |
| +0x5BA0 | two screens | 41xx, 42xx |
| +0x8D30 / +0x8E64 | file request queues | 2502/2503 |
| +0x8FA8 | script cache | |
| +0x8FEC | text lists | 48xx |
| +0x906C / +0x90F0 | pictures / animation packs (by CRC-32 name) | 46xx / 47xx |
| +0x9174 | sound | 49xx |
| +0x9440 | objects | 50xx..54xx, 59xx |
| +0x94E4 | messages | 60xx..62xx |
| +0x9584 | paint canvases | 70xx, 71xx |
| +0x9624 / +0x9660 | places | 8002, 89xx, 8100 |
| +0x96C4 | touch input | 1xxx |
| +0xAADC | current scope level | 4020/4021 |

- **Scope levels.** Every named record keeps the level it was made at. 4021
  frees the current level's places, paints, messages, objects, texts,
  animations, pictures and scripts. Each loop opens a level of its own.
- **Switches.** Every object, message, place and paint has four on/off words
  with a stack: master, touch, update, draw. 4014 pushes all, 4015 pops.
- **Loop frame order** (`020E22E4`): touch update, touch dispatch to places and
  objects, both loop threads, post-touch, game data, screens, pictures,
  animations, texts, sound, objects (in the screens' draw orders, so an
  object hung on a place updates after the place), messages, places. Drawing:
  screens, objects, messages, places, pictures, animations, texts.
- **Tweens.** Time in 20.12 frames; curves 0 linear, 1 p², 2 1−(1−p)²,
  3 ease in-out, 4 sin(πp), 5 4p(1−p), 0x21..0x23 periodic random weights.
  `value = start + trunc((target − start)·w/4096)`.
- **Objects.** Created by 5002 {name, kind, values} with a 16-register thread
  in the newest script; priority 13 by default. Each has a picture or
  animation, a position + offset + shake, rotation, scale and alpha, and
  buttons with touch kinds (plain, touch, select/click, slide). A pressed
  object switches to the sequence `<name>*`. The hit test goes through the
  inverse matrix: a pixel test without touch areas, else the areas last to
  first. Rotation steps snap the short way round; grids snap to the nearest
  cell.
- **Messages.** The boot loads `font_en1` and `font_en2` into font slots 0
  and 1. The cursor advances `speed` glyphs a frame. Line height = font
  height + line spacing + ruby room. Glyphs go into a 4-bit text layer with
  the colour's palette index (`fnt/color.lp`).
- **Places.** 8002 makes a place, optionally parented to an object. 8900 finds
  the nearest shown place within a radius (squared distances in 20.12).


## 5 Loops and the story

**The loop stack** (ov6 loop manager, ov7). Each frame: carry out the
requests (pop, restart, push, in that order), begin the top loop once nothing
loads, the top loop's frame, drawing, screen set-up, VBlank. An empty stack
asks for the game loop. Loop numbers:

| Id | Loop | Id | Loop |
|---|---|---|---|
| 1000 | game | 2300 | name entry |
| 1100 / 1200 | place / event | 2400 | lost-puzzle list |
| 2000 / 2010 | title / logos | 2500/2510 | vehicles |
| 2100 | bag | 3000 | puzzle session |
| 2120 | mysteries | 3100..3200 | play loops |
| 2130 | puzzle index | 4000..4260 | bonus section |
| 2150 | staff roll | 5000..5100 | Wi-Fi |
| 2200 / 2210 | puzzle title card / verdict | 6000..6401 | minigames |
| | | 8000..8200 | error loops |

Script loops get their script name as value 6 and end once their Main is done.

- **Event loop.** Finds the event, picks the first branch whose conditions
  hold, loads `evt/GG/GG_NNNNNN.cq`, applies the branch's opening effects,
  runs Main at the branch's label, and applies the closing effects at the end.
- **Place loop.** Loads `res/plc/pNNNN.cq` for the current place, lets the
  story move on and starts PlaceMain.

**The story's bookkeeping** (`lt4_def`):

- **Flags and variables.** A flag is a bit numbered by its name's number plus
  the index. A variable is a field of a byte array at its offset plus the
  index times its size, so names that overlap share bytes.
- **Steps** (2201 {id, name, heading, goal}, 2203 effects, 2204 conditions).
  The story moves to the next step while the current one's conditions hold;
  entering a step applies its effects.
- **Conditions and effects.** Flags, variables, puzzles, items, place, step,
  and the event's note, done flag and counter.
- **Items** (2021): sets give their items; 32..383 are marked, below 32
  counted (as the variable at that offset, e.g. the mysteries and the market
  medals); a notice is queued for each.
- **Puzzles.** Counts per category (main < 155, challenges, Wi-Fi), the index,
  completion items and picarats.

**Lost puzzles** (`020BC0B8`). Entering a step ends with the main puzzles it
leaves behind. Each unsolved puzzle among the first 155 index rows whose
third field names the new step gets its record's bit 8 and joins a list (at
most 170). 50 puzzles name a step. The next item notice lists them, six to a
page, as sent to the cat. The cat's menu (loop 2400) offers the unsolved
puzzles with bit 8.

**Rooms**: a lower picture, a pack whose first 16 sequences are the room's
parts, and a map script run once. **Area map**: an upper picture with
icons.

**The ending**: the final sequence (events 110100..110160), the staff roll
(about 28,000 frames of illustrated panels and scrolling names) and event
200030, which saves and returns to the title.


## 6 Puzzles

**Catalogue** (`lt4_def` 2301, 229 rows): id, Japanese name, title, type,
type name, picarats for 0/1/2 wrong answers, language flags. 2311 lists the
puzzles in numbering order: a puzzle's index is the row of its Japanese name.
23 rows are leftovers: 14 without a title in this language (ten Japanese kana
puzzles and 92, 101, 131, 135) and 9 "[Moved to ID n]" entries such as 32.

**Puzzle session** (ov17, loop 3000):

- Start: make the puzzle current, load its parameters
  (`res/naz/scr/@/nNNN.cb`: help picture, music, background, texts), set
  "try again" off and the hint count to the hints opened (0 once solved),
  mark it seen, load the frame scripts and `res/naz/seq/nNNN.cq`.
- States: title card (2200) → the play loop by type → verdict −1 goes back to
  the title card; quit ends; otherwise the verdict loop (2210). Verdict 1
  marks the puzzle solved; anything else counts a wrong answer; 2 tries again.
- Play loop 3100 commands 1000/1001 save and restore every piece (at most 64).

**Paint canvases** (`gnCPaint`): modes 1 dot, 2 line, 3 box, 4 pen, 5 fill,
8 pen with undo, 9 replay, 0x10.. handwriting. The pen is a thick line from
the previous point to the current one. Canvases take the pen before objects.

**Written answers** (type 1): one canvas per character box. On release the
strokes go to the recogniser in overlay 2, using the second game's database
and the conversion list (68 UTF-16 → byte pairs). Settings: categories by
mode, secondary by language (de 4), slant 0x400, four results, the first
character kept, "?" for none. With slant set, the recogniser reaches its
**pairwise disambiguation**:

- The database's pair list names one of 13 rules per pair of symbols. Rules
  0..3 and 9..12 follow the first stroke's turns and compare widths and areas;
  6 and 8 refit the ends; 5 keeps the order; 4 and 7 never act.
- On a change only the records swap; the scores stay with the ranks.
- This game's boxes are digits and capitals, which reach rules 0, 3 and 8.

Kana boxes recognise nothing: they belong to the ten Japanese leftovers.

**Circling** (type 2): counts the shown places inside the drawn box.


## 7 Special puzzle loops

Fifty puzzles play in loops of their own, each a class derived from the
standard loop's. They override the loop commands (script command 2400 with
a sub-command) and some frame hooks: after the pen (+0x54), after the updates
(+0x5C), end of frame (+0x60) and drawing (+0x64).

| Type | Loop | Overlay | Puzzles | Rules |
|---|---|---|---|---|
| 10 | Cut | 18 | 4 | a grid of dots; stepping along an existing line cuts or mends it; path mode (75, 133) with start dots and buckets |
| 11 | Line | 19 | 9 | lines between dots (places); drawing the same line again rubs it out; options: longest line, keep x/y, not along the edge |
| 14 | Ltgram | 20 | 11 | pieces eased toward the pen, snapped on release, turned by circling; judged when every region cell's corner lies in a piece box |
| 12, 30 | Slide | 21 | 13 | pieces move a pixel at a time toward the target, at most 8 steps a frame, sliding along walls; mazes with a ball (4 steps), colour gates and dark masks |
| 13 | Slide2 | 22 | 2 | sliding with pushing: a piece pushes the pieces it runs into, recursively |
| 19 | Bulldozer | 23 | 0 | no puzzle uses it |
| 24 | Jump | 24 | 2 | a frog jumps by the size of the pad it stands on; the pad it leaves sinks |
| 26 | Lead | 25 | 2 | fish placed in line with the cat; the cat walks there at 11 frames a cell |
| 27 | Vanish | 26 | 3 | peg solitaire on a board with holes and an optional goal cell |
| 28 | Trampoline | 27 | 3 | draw trampolines on a grid; the object falls (`vy += 0x333` in 20.12), bounces off them by reflection times the bounciness, and must land in the catcher |

Loop shapes (lines, dots, tints) are drawn through the 3D engine under the
objects. The fill follows the DS rasteriser's scanline rules (18-bit slopes,
x-major runs, side- and slope-dependent edge fill), which reproduces every
line board pixel for pixel.


## 8 Minigames

The bag's three minigames are script loops with native commands. Courses,
scenes and tanks come from the game's data files (`res/sub/lt4_sub_*.cb`).

- **Model railway** (6200, ov39): a course editor and an isometric run, 32
  steps a cell, with accidents, stations and fuel.
- **Puppet show** (6110 menu, 6100 ov38): scenes and branches from data; the
  plays unlock one after another.
- **Fish tank** (6401 menu, 6400 ov37): ten tanks of 14 × 9 cells of 18 px.
  - Kinds: fish starts, coins, blocks, bubbles, hyper bubbles, cookies, bubble
    spots.
  - Whenever a bubble is placed or removed, the fish's path is swum to its
    63rd bounce and recorded. The run replays the recorded steps: on a cell,
    all coins eaten wins and running out of time loses.
  - A hyper bubble starts a rush that breaks bubbles; the third bounce ends
    it.
  - The water is drawn as textured quad strips shifted by the ARM9 sine table.
- **Whack-a-mouse** (6300, ov40), the story's optional game.
  - Spots, patterns and schedules come from `lt4_sub_mouse.cb`. The loop only
    decides who comes out where; the script plays the game.
  - Each step puts the mice in random order (each inserted at a random place,
    then 256 random swaps) on free allowed spots.
  - Titles depend on the score.
- Loop 6000 (ov36) is never requested. **Hypothesis:** unused.


## 9 Bonus section

"Bonus" on the title loads a slot and opens the bonus menu (loop 4000). Its
entries are the weekly Wi-Fi puzzles (5000, always empty), the puzzle index
(2130), the challenges (4100; after the game is finished and the three
minigame modes are cleared) and the secrets (4200, unlocked by picarats):
profiles, gallery (20 pictures, 21 once the game is complete), music, voices,
videos and the secret door.

- **The secret door** is the name-entry loop in mode 4; its password comes
  from the next game.
- **Videos.** Film 50 plays looped with a flag and is drawn only as 40 × 24
  cells inside each thumbnail button.
- **Command 2600** reboots the DS into another program (0 trial end, 8 the
  Wi-Fi settings utility, 4 an extra program); the port starts over at the
  title.


## 10 Sound and films

The middleware driver of the earlier games, in overlay 0 with the third
game's tables.

- **Banks.** `snd/<G>_%03d.cse` and music `snd/BG_%03d.cse` (GFSP packs), main
  wave banks `wav/BG|SY|GE|SI.SWD`.
- **Streams.** Route 0 jingles `str/jng` (per language for 100..138 and from
  600), 1 music `str/bgm`, 2 film sound `str/mov/@`, 3 event voices
  `str/evt/@/%02d_%06d_%04d.SAD`.
- **Sound sets** (`lt4_def` 2111 {set, music, GE bank, SI bank}, 123 rows):
  4900 selects one.
- **Services.** 4900..4962: effects, music, streams and their status. Music
  and stream requests are queued and carried out in order once what they
  wait for is done.

**Films** (ov10). A hardware alarm at the film's rate advances the due frame;
command 9122 returns the film's time in 60ths of a second, and the movie
script shows each subtitle when that time reaches its id. The decoder is the
shared Mobiclip decoder with this game's table layout (all 37 films decode).


## 11 Saves

The saves are script-driven (`lt4_prog_saveload`: SaveMain, CheckRewrite,
LoadMain, AutoSave), with services 2801 load, 2802 save, 2803 busy, 3050..3052
the slot table and headers.

- **Header** (`020B8698`): play time capped at 99:59, found and solved (story
  plus challenge puzzles), finished 0 / 1 / 2 (all 155 story puzzles) / 3
  (and the 15 challenges), name and place title.
- **Table flag 0** is set once the game is cleared or any slot is finished.
- **Continue** opens with the event named after the step's heading (a recap).

The port keeps its own files (`slot0..2.sav`, `table.sav`), not the
cartridge layout.


## 12 Bugs and oddities in the original

- **Anti-tamper.** The `seq` archive copy makes the bag's save button return
  to the title on a modified cartridge.
- **Line, "keep y".** The option writes the dot's y into the end's x
  (`021075A0`). The port keeps this.
- **Lead.** A fish tapped out of line is still shown (alpha 1) though it is
  not used.
- **Handwriting rule 12** can read one point past its arrays (`y[32]` is
  `x[0]`; `x[32]` the caller's saved step weight). The port reproduces it.
- **Railway undo.** The undo test reads the path before its first cell when
  the track is empty (`path[-1]`).
- **Fish tank.**
  - The plot keeps 512 steps; a longer run would read into the hits list.
  - A fish shut in on every side would loop forever.
  - Several fields are written but never read: the bounce point list, the
    midpoint, the bounce counter and five "dot" players.
- **Random jumps.** Script command 31 (random label jump, also inside
  WaitFrame) draws one random number every frame, so the random sequence
  depends on frame counts.
- **Post-credits event.** Event 200030 names a dummy talker; whether its name
  tag shows on the console is not checked.
- **Leftovers.** The catalogue keeps 23 leftover rows (moved or untranslated
  puzzles); loop 6000 is never requested.


## 13 Research tools and checks

Research tools live in `.analysis/adventure4/tools/` (local):

| Tool | Purpose |
|---|---|
| `gfsa.py`, `render.py` | prototype readers for every format; picture renders |
| `ghidra_blfd.py`, `dis4.py`, `handler_tables.py`, `service_map.py`, `loop_table.py`, `vtables.py` | code maps |
| `gss_dump.py`, `gsd_dump.py` | script and data dumps |
| `*_solve*.py` | solvers for slide, ltgram, slide2, jump, lead, vanish, line, cut and trampoline puzzles |
| `decuma_battery.py`, `decuma_variants.py`, `decuma_compare.py`, `decuma_solve.py` | handwriting |

Native runners: `picarat_a4_loop_run` (one puzzle session, or `sweep` over
all puzzles) and `picarat_a4_story_run` (`A4_AUTO` plays the story
unattended; snapshots per step).

What was checked:

- Puzzle 1's upper screen is pixel-identical to the ROM.
- Every special loop's opening boards and a solution, played in the harness,
  match the ROM.
- The recogniser's candidates are equal in all 412 recorded boxes, rule
  exchanges included.
- The railway, puppet show and fish tank are frame-identical in the compared
  stretches.
- The whack-a-mouse's waves are identical for 30 seconds, once the random
  state is copied.
- The story's loop requests from boot through the first events match the
  ROM. The runner reaches the staff roll and the post-credits event.

## Embedding notes (Reprise, 2026-10-29)

Observations from running the reconstruction inside Reprise, not new
findings about the original: the game thread is stepped one frame at a time
by the host, and the sound is rendered a fixed amount per frame (32 728 Hz
resampled to 48 kHz), so every sound-gated wait follows the frame count
instead of the device's real-time progress. Reprise keeps no checkpoint for this game yet: the engine's own save slots are its progress. Details:
[../reprise.md](../reprise.md).
