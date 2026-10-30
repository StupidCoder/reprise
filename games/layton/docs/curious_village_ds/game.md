# Curious Village (Nintendo DS): how the game works

What we learned while reverse engineering the first adventure (European
cartridge `A5FP`) for the re-implementation in `games/layton`. It covers the
cartridge and program, the main loop and scene router, the state block and
save format, the data formats and languages, the GDS script machine, the
story, the trunk and its minigames, the bonus section, the puzzles and their
engines, sound and movies, and the original's own bugs. Remaining work is in
[plan.md](plan.md).

All findings come from the user's ROM and from runs of it. Sources were the
Ghidra output of the ARM9 and ARM7 programs (`.analysis/adventure1/tools/ghidra_a5fp.py`),
the cartridge's data, and frame-by-frame reference runs in the research
harness. The harness is an emulator used only as a measuring instrument. No
disassembly, decompilation, asset map or format description of this game was
consulted. Findings from the second and third adventures were used only as
hypotheses and re-checked against this ROM. Addresses are ARM9 RAM addresses
unless marked ARM7. Statements marked **hypothesis** are not confirmed. The
detailed research notes are in `.analysis/adventure1/docs/` (`assets.md`,
`script-machine.md`, `story.md`, `puzzles.md`, `sound.md`). They are local and
not part of the repository.

Contents: 1 Cartridge and program · 2 Program structure · 3 Data formats and
languages · 4 The GDS script machine · 5 Story, rooms and saves · 6 Trunk
pages and minigames · 7 Bonus section · 8 Puzzles · 9 Sound · 10 Movies ·
11 Display and timing rules · 12 Bugs and oddities in the original ·
13 Research tools and checks


## 1 Cartridge and program

| Field | Value |
|---|---|
| File | `... Curious Village (Europe) (En,Fr,De,Es,It).nds` |
| SHA-256 | `d808728d3e930b7a6c952d05e5e39db4a15b685c9c0919b57ece07b5243f493f` |
| Size | 128 MiB |
| Header | game code `A5FP`, maker `01`, revision 0 |
| ARM9 | loaded at 0x02000000, entry 0x02000800, 913,304 bytes |
| Overlays | one: overlay 0 at 0x0221fbe0, 158,592 bytes (the Wi-Fi library) |
| Files | 4,264 under `data/`, plus `dwc/utility.bin` |

There is no German-only release. This European cartridge carries English,
French, German, Spanish and Italian.

The program is nearly monolithic: one 913 KB resident program and a single
overlay. The second adventure has 55 overlays and the third 48. Code
similarity, measured with 32-byte windows (an indication, not proof of
identical behaviour; `evidence/code-overlap.txt`):

- 60 % of this ARM9 appears in the second game's code: the SDK and runtime,
  the Wi-Fi library and the Mobiclip movie decoder.
- The second game's handwriting recogniser (its ov0) and Mobiclip decoder
  (ov1) sit 98.5 % inside this ARM9. Its engine overlay 2 shares only 27 %,
  so the engine was largely restructured between the games.
- Several of the second game's puzzle overlays share half or more with this
  game's engines. Some of them (ov28, ov29, ov32, ov35, ov36, ov40, ov44) are
  shipped in the second game but used by none of its puzzles: leftovers of
  this game's engines.
- The handwriting database `decuma/_databas_le.bin` and `font/font18.NFTR`
  are byte-identical to the second game's.


## 2 Program structure

- **Globals** at `020dec00`: +8 the current puzzle engine, +0xc a pointer to
  the state block.
- **Main loop** `02000cb4`: initialise (`02002f4c`), then forever run the
  router `02000d28`. Scenes 6, 0x10 and 0 go to the boot `0200129c`, 0xb to
  the demo end (`02002558`, "Taiken"), 3 to a room (`02002324`).
- **Scene router** `02000d28` switches on state +0xf, the scene. Command 0x51
  names some scenes: Question 1, Room 3, Event 5, Movie 7, Event Movie 0x22,
  Reset 0, Taiken 0xb, Challenge Mode 0x1c, Staff Roll 0x26. The others are:
  2 trunk, 4 puzzle then event, 8 painting, 9 hotel, 0xa robot dog, 0xc..0x14
  bonus pages, 0x15 title, 0x16/0x17 intro talk, 0x18 logos, 0x19 title logo,
  0x1a/0x27 puzzle index, 0x1b two-screen movie, 0x1d..0x21 Wi-Fi, 0x23 name
  entry, 0x24 bonus menu, 0x25 passcode, 0x28 naming the robot dog, 0x29
  opening text.
- **Script objects** (their RTTI names): `MasterObject` runs `logo.gds`,
  `room<N>_in.gds` and `story.gds`; `RoomObject` runs `room<N>_param.gds`;
  `EventScreenObject` runs `event/e<N>.gds`; each puzzle engine runs its
  parameter script. The command's effect depends on the object (section 4).

### State block

One 2,000-byte record per save slot. Known fields:

| Offset | Meaning |
|---|---|
| +0 | event (u32, 0..0x1df) |
| +4 / +6 / +7 / +8 | puzzle (s16), solved, retry, quit |
| +0xa / +0xb | reward event after a puzzle; retries |
| +0xe / +0xf / +0x11 | room (u8), scene, return scene |
| +0x1e | story number |
| +0x24 + N | puzzle progress: 1 seen, 2 solved, 4 done, 8/0x10/0x20 hints 1–3 bought, 0x40/0x80 failure count. Puzzles outside 1..254 sit at +0xeb − N |
| +0x12c + N | event marks (1 run, 2 marked by 0x55) |
| +0x328 / +0x418 | slot names, player names |
| +0x3e8 / +0x3f4 | play time |
| +0x468 + kind | puzzle-kind tutorial seen |
| +0x4a8 | place name (`map%i.txt`) |
| +0x4f0 | judgement picture set (commands 0x66/0x67) |
| +0x534 | hint coins (u16; a new game has 10) |
| +0x538 | hide the quit button |
| +0x554 | 1,024 story flags (0x8d/0x8e). Flags 300..339 also update the notes |
| +0x5d4 | "marked" puzzle bits of the index |
| +0x5f4 | chapter |
| +0x600.. | robot dog, hotel and painting state (section 6) |
| +0x640 | picarats |
| +0x684 / +0x6a4 | the two hotel rooms' furniture (20 each) |
| +0x704..+0x708 | "new" marks: memo, dog, hotel, painting, mysteries |
| +0x70c | movie number |
| +0x728 | special puzzle number |
| +0x738 | player name (20 bytes) |
| +0x74c | event after the scene |
| +0x754 | 1 on this European program (selects the ink glyph) |
| +0x758 | language folder index (section 3) |

A new game (`0201974c`) sets event 1, puzzle 0x6f, room 1, return scene 5,
10 hint coins and flag 300.


## 3 Data formats and languages

| Files | Format | Notes |
|---|---|---|
| `bg/**/*.arc` (1,659) | 4-byte codec tag + BIOS stream (LZ10, RL, Huffman); palette, 8 bpp tiles, tile map | same as the second game |
| `bg/staff_top*.bgx` (16) | the same layout, uncompressed | duplicates of `.arc` files |
| `ani/**/*.arc`, `.arj` (1,255) | tag + stream; sprite frames, palette, animations | same as the second game |
| `**/*.pcm` (71) | LPCK archive in one LZ10 stream (texts) | new; the second game uses PCK2 |
| `script/**/*.gds` (930) | GDS token list | more token kinds than the second game |
| `font/*.NFTR` (3) | Nitro font 1.0 | |
| `sound/sound_data.sdat` | SDK sound archive: 21 sequences, 94 sequence archives (effects), 115 banks, 2 wave archives | new; the sequels use SMD/SWD/SED |
| `stream/*.SAD` (59) | `sadl` streams, format 0x72 | |
| `video/*.mods` (28) | Mobiclip `MODS N3` | |
| `decuma/_databas_le.bin` | handwriting database | identical to the second game's |
| `debug/puzzledata.dat`, `debug/storyscript/*.gds` | debug leftovers | 52 `storystart_NN.gds` set flags and jump into the story; usable as test fixtures |

**LPCK** (the expanded `.pcm`): u32 header size (16), u32 total size, u32
member count, `LPCK`. Then per member: u32 record header size (always 32),
u32 record size, u32 0, u32 payload size, a NUL-terminated name padded to the
header size, the payload, padding to the record size. Checked on all 71
archives (20,538 members). Texts are plain 8-bit text with `@` codes (`@p`
wait, `@c` clear, and `@1 @2 @3 @W @C @w @P @#`, not all decoded) and
escapes from a 70-entry table at `020bbf10` (`0200c2d8`).

**GDS tokens.** The reader `02017e7c` knows tags 0..12: 0 command (u16),
1 int32, 2 float32, 3 and 4 strings (length + bytes), 6 and 7 a 4-byte jump
target (offset after the size word), 5 and 8–11 without operand, 12 end.
The scripts use tags 6, 7, 8, 9 and 11; tags 4, 5 and 10 never occur.

**Languages.** Language-specific files sit in two-letter folders (`ani/de/`,
`qtext/it/q000.pcm`). The program writes paths with a `?`
(`/data/qtext/?/q000.pcm`), which `0201bc8c` replaces with the folder of
state +0x758, looked up in `020d2ad0`: jp, en, es, fr, it, de, ko, ch.
`020199b8` sets +0x758 once from the firmware's user language
(`027ffc80` +0x64, bits 0–2): Japanese and English give en, French fr,
German de, Italian it, Spanish es, anything else en. The game has no
language menu of its own. Only en, es, fr, it and de exist on this
cartridge, and every language-specific resource exists in all five.

**Anomalies.** `bg/<82 91>140_bg.arc` has a Shift-JIS full-width "q" in its
name (the importer spells such bytes `%XX`). `ani/clock_numbers.arc` frame 2
does not fit its header and nothing names the file; it is left out.
**Hypothesis:** an unused leftover. `ani/en/mm_btn_memo.arc` puts its second
chunk outside the frame (the other languages do not); it draws nothing.
Event 8 names `ebg_261` without the language folder, which exists only per
language; the game keeps the old picture (**hypothesis** from `020033f0`).
`ebg_302.gbx` is a typo that the loader survives because it replaces the
last three characters of the name.


## 4 The GDS script machine

| Part | Address | Notes |
|---|---|---|
| Token reader | `02017e7c` | each token updates only its own value slot |
| Reader state | `0220d074` | command, tag, float, integer, base, jump target, size; strings in `0220d48c` / `0220dc8c` |
| Main loop | `02018050` | skips to the next command; tag 12 or command 0x4f ends; otherwise `table[cmd](cursor, object, &result)` |
| Handler table | `0220d08c` | 256 entries, filled by `020171b0`: 254 commands 0x01..0xfe, all distinct |

There is one handler table for every script. Each handler calls the running
object's virtual +8, reads a fixed number of arguments and usually passes
them to a virtual function of the object. So the same command does different
things in a room, an event or a puzzle engine.

**Arguments.** A handler reads N tokens and takes each value from the slot of
the kind it expects. A token of another kind therefore leaves an older value
in place. The scripts do this six times (events 49, 42, 126, 233 and 276 give
integers to the float parameter of 0x78/0x79). Commands 0xd1/0xd2 take a
truth value: an integer other than 0, or the string "true" in any case.

**Structured flow control** (the second game has none of this):

| Command / tag | Behaviour |
|---|---|
| 0x12 If (`02010f2c`) | reads a condition chain up to tag 6. True: continue after the jump token. False: look at the target; an 0x16 there tests its chain, an 0x17 continues after it, anything else continues at the target |
| 0x16 Else-if, 0x17 Else | reached at the end of a taken branch: skip to the end of the chain |
| 0x15 While | the chain, then the body up to tag 6; tested again after each pass |
| 0x14 Repeat | integer count, body up to tag 6 |
| 0x19 True, 0x1a False | set the result |
| tag 8 | negates every *later* condition of the chain (never reset within it) |
| tags 9 / 10 | combine the following results with and / or; without them a later result replaces the earlier |
| tag 11 | break out of the innermost loop |
| 0x4f Stop | ends the script |

Condition commands are 0x13, 0x18, 0x49–0x4e, 0x54, 0x58, 0x63, 0x77, 0x86,
0x8d, 0xc1, 0xda and 0xeb. Handlers may block for frames (polling loops such
as `while true { wait; if C break }` in event 301).

**The engine command 0x1b** (`02011610`) looks a name up in a table of 24
engine names (`020d2938`, case-insensitive), constructs the engine, stores it
in `020dec08`, sets the puzzle kind byte, and runs the rest of the parameter
script with its own loop: no stop command, no size check, up to tag 12, every
command going to the engine. An unknown name hangs in an empty endless loop.
Name 16 (River Cross 2) builds River Cross and calls its virtual +0x90(1);
name 21 (Slide Puzzle 2) builds Slide Puzzle.


## 5 Story, rooms and saves

**Boot and new game** (`0200129c`, checked against a ROM run): logos
(`logo.gds`), slot headers, title (`0202ce14` / `0202d480`). A new game runs
name entry (`0203a994`), the opening text (`02035d90`), movie 1, intro talk
part 0, puzzle 24 (display number 001) without a quit button, intro talk
part 1, the title logo, `story.gds` (not on the cartridge, so nothing runs)
and room 1. A loaded game runs event 0x12d first.

**Rooms.** Move mode (`0201fce8`): a plain way out is hit where its pulsing
door is drawn; plain ways and arrows of type −1 leave with a fade. Other
arrows walk the map marker 3 pixels every second frame, run
`room<N>_in.gds` and rebuild the room in place, fading the upper screen only
when the map changes. Footsteps are the looped effect 15. The room fades in
in steps of 2. The map marker is drawn centred on its place. Commands
0xd1/0xd2 register spots for the robot dog; 0xe4 and the info button belong
to the puzzle shack (room 88).

**Robot dog in rooms** (`02065a94`): 0xd1/0xd2 register up to eight spots,
each with a 30 % chance. At the room's start one spot is picked at random and
kept by chance: the dog waits 16 pixels to the side it comes from, invisible,
with a timer of 180. At 40 it barks (effect 0x18), counts +0x63c (at most 999)
and the room takes no touches until it has gone. At 0 it walks in (2 pixels
and 5 alpha a frame), holds a random pose (animation 2..4) for 60 frames and
fades out. A coin taken before the bark sends it away. It is mirrored when it
comes from the left.

**Event face parts** (0x6d, `02027b34`) are actors of kind 1, drawn only
while their character speaks, or kind 2 when the sprite is a mouth. The
`*_e_face` sprites have no "*" animation, so their parts are mouths that
show only while that character talks.

**Save backup** (8 KB, `02024364`..`0201a8e8`, checked on the user's file):

- Header (0x150 bytes): a u32 checksum over +4..+0x14f (two 16-bit running
  sums, `0200b7d0`), the magic `ATAMFIREBELLNY`, +0x14 last slot, +0x15[3]
  used, then per slot: place (0x40 bytes), minutes, hours (not clipped),
  puzzles found, solved, player name (0x14 bytes; an empty slot holds a
  program string) and "cleared" (flag 0x200).
- Slots at 1000 + 2000 n, 0x53c bytes with a checksum at +0x538. Writer
  `02019c9c`, reader `0201a1dc`.
- A fresh native image equals the cartridge's empty header byte for byte,
  and the stored checksum of the user's file is reproduced.

**Save screen** (one class for events and the trunk): slots at
(4, 0x24 + 0x30 n, 0xf8, 0x29). Overwriting asks with the slot's name.
Saving fades the music, plays effect 5 and shows `filesaving_bg` for 0x3c
frames before and after the write. Loading shows `fileloading_bg<n>` for 0x1e
frames.

**Play time** (`02019be0`) is real time: OS ticks since the game was started
or loaded. Every frame counts, loading pauses and fades included.

**Staff roll** (scene 0x26): 0xf0 frames of black. On the lower screen,
`staff_roll1..32` scroll one pixel every three frames on one tall layer, the
next picture loaded into its other half every 0xc0 pixels, with a glowing
edge. On the upper screen, `staff_top1..15` fade in and out over a tiled
background. The music starts after 0x78 calls. Then `staff_gfx`, 300 waits,
"the end" until a touch after 0x78 frames, and event 0x156 saves the cleared
game.

**Save write error** (`0200b590`): after a save (`0201abec`) the card's
error flag (+6) is checked; when set, effect 4, the music out over 30 frames,
both screens to black, the text cleared, `?/save_writeerror` on the lower
screen fading in, 30 VBlanks, then the program stops (an endless loop: the
player must switch off). Both callers are the save screen (from an event and
from the trunk). The port shows it when the save file cannot be written.


## 6 Trunk pages and minigames

**Trunk** (scene 2): buttons for the memo, save, puzzle index, close and
mysteries. With flags 502 / 501 / 500 it also offers the hotel, the painting
and the robot dog, with a "fix" picture once each is complete. The upper
screen shows found/solved, coins, picarats, a medal by coins (>30/50/80/100),
a walking figure that stops at 100 hours, and the place. The memo lists six
entries a page and opens at the current chapter's page. Prizes from events:
0xc5 hands out a painting piece, 0xcc a piece of furniture for one of two
hotel rooms (the player picks the room).

- **Mysteries** (scene 0xc): ten portraits, with flags 300+n / 320+n for
  found / solved.
- **Robot dog** (scene 10): waiting parts (five shown); `dogscript.gds` 0xc2
  places each part; a placed part flies up 6 pixels a frame and fades, and
  its piece fades in on the dog. When complete: event 0x100 once, then the
  dog is named (scene 0x28).
- **Painting** (scene 8, a jigsaw): the screens are swapped. Pieces drag by
  the middle and turn by the rim. A piece snaps to the nearest of 20 places
  (swapping with the occupant; outside the frame it goes back to the box) and
  to the nearest quarter turn at 5 degrees a frame. Whole: effect 0x25c and
  event 0x102 once.
- **Hotel** (scene 9): screens swapped. `hotelscript.gds` 0xbc/0xbd place each
  piece (id, x, y, layer, parent, offsets, frame on parent), 0xc6..0xc9 the
  guests' comments, `hintscript` the words per happiness. Happiness is native
  code (`0206593c` / `020659ec` on condition tables `02064ecc` / `02065404`):
  five needs per room. Both rooms content: event 0x101 once.
- **Puzzle index** (scene 0x1a): found and marked puzzles in number order,
  the text clipped to the rows, data from `qtitle.gds`, `qscript.gds` and
  `pscript.gds`.
- **Puzzle shack** (room 88): spots with the next waiting puzzles (round robin
  over flags 800+n not yet solved), a shuffle button with more than eight
  waiting, and "play puzzle N?".


## 7 Bonus section

"Bonus" on the title opens the load screen; a loaded game shows the bonus
menu (scene 0x24) until its back button. Every page fades in and out.

| Scene | Page | Rules |
|---|---|---|
| 0x24 | menu | Wi-Fi, puzzle index, challenges (flag 506), extras (flag 512); after the ending event 342 sets 506, 510 and 512 |
| 0xd | extras | character files, art, music, voices and movies open at 2,500 / 2,700 / 2,900 / 3,100 / 3,500 picarats; passcode |
| 0xe | character files | 40 characters on two pages |
| 0xf | movie theatre | 16 movies, skippable here |
| 0x11 | art | 20 pictures with titles and descriptions |
| 0x12 | music | 19 pieces; piece 1 is the ending's stream, the others sequences |
| 0x13 | voices | 14 lines of the assistant and 27 of the professor |
| 0x25 | passcode | 36 keys; the code is derived from the console's MAC address (`0200b618`, the same routine as the second game's) with seed 20057; the right code runs event 0x103, which shows the code for seed 20043 |
| 0x1c | challenges | five challenges (flags 507, 508, 509, 512, and every story puzzle solved, which also sets 511), three puzzles each |
| 0x1d..0x21 | Wi-Fi | menu, received puzzles (a list in the backup at 0x1b58: number and date per puzzle), download |

Wi-Fi puzzles are left out of the found and solved counts and of the index.


## 8 Puzzles

**Flow** (`02001044(N)`): title card (`0203c85c`), the puzzle scene
(`02000f54`: run `qscript/q%i_param.gds`, then the engine's own blocking play
loop and destructor), the result (`0203e248`) unless the player quit, all
again while the retry flag is set.

**Data**

| What | Source |
|---|---|
| display number | int table `020daeb0` by internal number (1 → 9, 24 → 1; 0 = no puzzle) |
| picarats | `pcarot/pscript.gds` 0xc3: value at 0, 1 and 2+ failures |
| title | `puzzletitle/?/qtitle.gds` 0xba, or `t_N` (`NO TITLE` if missing) |
| kind and place | `qinfo/?/qscript.gds` 0xdc |
| texts | `qtext/?/q000` (N < 50), `q050`, `q100`: question, correct, wrong, three hints, title |
| progress | state +0x24 + N |
| kind tutorials | `?/puzzlet<kind>_<page>`, once per kind |

There are 164 scripts and 135 numbered puzzles.

**Base object** (`0203fb94`): hint button and tabs, yes/no, back, quit and
its dialog, the number digits, the touch prompt and the question text.

- The intro types the question two characters a frame.
- The hint screen opens on the first hint not bought. Buying spends a coin.
- The quit dialog shows the title.
- Tutorial pages show the first time an engine kind is played; command 0xf6
  suppresses them.

**Result.** The judgement pictures come from tables `020d2488` (right),
`020d2520` (wrong) and `020d24d4` (frames). Then a slide, a random spoken
line, the picarat transfer, the answer screen and the retry menu ("1" again,
"2" hints first, "3" quit). Commands 0x66/0x67 pick the judgement set; a new
game has 1.

**Engines.** The factory knows 24 names; the 168 parameter scripts use 20 of
them. Button List, Draw Input, Shape and Tile Rotate are never used.

| Engine | Puzzles | Rules worth knowing |
|---|---|---|
| Draw Input2 | 65 | written answers: slots of up to 16 characters; the second game's writing box (8 strokes, 256 points, up to three readings); a character is entered after 15 untouched frames; submit needs every slot full; "nothing" reads as −2 |
| Free Button | 20 | multiple choice; loads `q<N>_bg` itself |
| Trace Button | 15 | circle the answer |
| Cut Puzzle | 13 | the second game's Divide rules |
| Slide Puzzle (2) | 13 | sliding blocks |
| Tile | 9 | tiles onto slots |
| Queen | 5 | pieces on a board |
| Match | 5 | a piece costs a move the first time it is taken; one put back exactly (or within 10/16 pixels, it springs back) returns its move; turning in 15-degree steps; the judge tries an exact pass, then each target's tolerances; a match counts either way round |
| On Off, Cup | 3 each | Cup: the jug's tilt ends at 90 degrees |
| Trace, Scale, River Cross (2) | 2 each | Scale: random lighter/heavier weights from the game's generator; "Restart" swaps 50 random pairs; River Cross: wolves outnumbering chicks eat them; River Cross 2: wolf, sheep, cabbage |
| Drag Puzzle | 3 | |
| Connect, Place Target, Coin, Match Tutorial | 1 each | Connect: 19 × 19 cells; a path follows the stylus one cell a frame and may not touch itself, cross others or run beside another colour; four pairs solve without submit |

The engines' logic lives in `0204ee00..02062600`. Several match the second
game's unused overlays (Match = ov28, Slide = ov29, Drag = ov32, Connect =
ov35, Cup = ov36, Cut = ov37).

**Turned and scaled sprites** (`02005e78`, `02005bc0`) and the cup's water
go through the DS 3D pipeline: the fixed-point camera of `020040d0`, clipping,
the rasterizer's edge walk and anti-aliasing. An edge pixel over no 3D pixel
keeps its colour. The second game shares this camera and chunk code.


## 9 Sound

Music and effects are SDK sound-archive data played by the SDK libraries:
an archive player on the ARM9 (`02096458..02098f14`) and a sequencer on the
ARM7 (WRAM `037fe804..03801f1c`). The sequels use a different, resident
sequencer.

- **ARM7 pass** (`037ff008`, on every alarm of 0xaa8 × 64 clocks): write the
  channel changes of the previous pass, take the game's commands, step the
  players (240 tempo units a step), update the channels (envelope, sweep,
  LFO, volume and pitch through the BIOS curves), report busy channels and
  advance the random generator.
- **Game's sound object** (`02004718..02005670`, one object at `02104618`):
  per scene kind, room, event and puzzle it loads a sequence, effect archives
  and a group from tables in the program (`020cea86` and neighbours). Fade
  lengths are 100, 10, 30 and 100 frames.
- **Typing tick.** At every fifth character the text box starts effect 0x29.
  If the previous tick still sounds, it loops until it can, so the game
  stands still: this is why some frames of typing are held (`0200e158`).
- **Streams** (`.SAD` format 0x72): 32 nibbles of IMA-style ADPCM per 16-byte
  block. Each nibble gives two samples, the midpoint with the previous value
  and the value, through the cutoff filter shared with the second game,
  quantised to multiples of 64. The decoder's last filter value carries into
  the next stream because the game reuses the place in memory. Voices use
  channels 10/11 and the opening movie 15/14. A refill comes on an alarm every
  1,024 samples.
- **Result pause.** The result's black screen waits until no sound plays:
  135 frames after the "submit" effect, none after engines that solve
  themselves.
- The BIOS pitch and volume curves are not on the cartridge. They are
  computed, and checked against the reference emulator's BIOS table.


## 10 Movies

`02018fa4` plays a one-screen movie (`video/m%i.mods`) and `020191d8` a
two-screen one (`emt%i` / `emb%i`). Each has its stream `M%i.SAD`. Music cues
and captions come from `script/event/m%i.gds` (0xfd caption: index, start and
length in seconds; 0xef/0xf0 music cues). In the story only movie 0 can be
skipped (a touch after 300 frames); in the theatre every movie can. The
decoder's tables lie at the same offsets from `020baab8` as in the second
game's movie overlay. Some MODS files also carry an audio track, which the
game does not use.


## 11 Display and timing rules

- **Fades** step a counter from −16 by `step` while it is ≤ 0: 16 / step + 1
  frames. Only `0200f7b4` (both screens in) stops early.
- **Frame counter.** `021ebb30` counts main-loop frames: presents and fade
  steps. Loading, the typing waits and the last 10 touch-only frames of the
  intro do not count. Sprite animations and button clicks run on it.
- **Touch.** A press counts one update after contact; the DS reports touches
  about 3 frames after the input.
- **Drawing order** decides the 3D depth: equal depths show the one drawn
  first.
- **Blends.** The save screen and the mysteries page set a blend that the ROM
  does not show (the raw colours are measured). **Hypothesis:** the 3D layer's
  own alpha wins.
- **Lower backdrop.** The trunk sets colour 0, but its picture's palette
  overwrites it at the next VBlank.
- **Loading pauses** are real cartridge read times. They grow with file size
  but no size model fitted; they are kept as measured constants.


## 12 Bugs and oddities in the original

- **Stale arguments.** A token of the wrong kind leaves the previous value in
  a handler's argument (six uses in the events). `qscript/q114_param.gds`
  ends after three of five arguments of its last 0x2b, so the original reads
  past the script; the floats keep the previous value.
- **Repeat 0.** Repeat with count 0 runs its body once as ordinary commands
  (unused by the scripts).
- **Tag 7** follows many commands but no reader uses it.
- **Draw Input2.** The digit loop has no upper bound: a fourth distinct
  reading overwrites a counter, and further ones run past the object.
- **Recogniser slowdown.** The recogniser overruns its frame, so the
  cartridge skips one or two main-loop frames after each stroke. The port
  does not reproduce this.
- **Scale.** The sprite asks for "Plate" and "Block" where the file says
  "plate" and "block"; the lookup ignores case.
- **Connect.** It computes the distance of a negative difference as −(a + b),
  and compares cell numbers with pixel positions when it searches nearby
  dots. Three buttons are created but never updated.
- **River Cross.** Seats are numbered by the count aboard, so two passengers
  can share a seat after one leaves.
- **Cup.** It stores the water's corners as truncated 4.12 fractions, which
  lifts offsets below the centre by a line.
- **Map anomalies.** `mm_btn_memo` (English) puts a chunk outside its frame.
  `clock_numbers` is broken and unused.
- **Missing picture.** Event 8 names a picture that exists only per language.


## 13 Research tools and checks

Research tools live in `.analysis/adventure1/tools/` (local):

| Tool | Purpose |
|---|---|
| `ghidra_a5fp.py` | the Ghidra workspace (ARM9 and ARM7) |
| `fn.py` | decompile one routine, with its literal-pool words resolved |
| `gds_dump.py`, `handler_summary.py` | script dumps and the handler map |
| `a1_run.py` | runs a puzzle in the harness from a saved state |
| `story_ref.py` | story reference runs |
| `regress.py` | replays every recorded ROM comparison and lists the frames that differ |
| `loadgap.py` | measures loading pauses |
| `sound_replay.cpp` | replays a run's sound calls and compares register writes and the mix |

Native runners: `picarat_a1_puzzle_run`, `picarat_a1_story_run`,
`picarat_a1_story_play` (the auto-player) and `picarat_a1_sound_replay`.

What was checked:

- Every engine and puzzle variant against frame-by-frame ROM runs:
  identical except turned-sprite edge pixels.
- The title card, hint screen, quit dialog and tutorial pages: pixel-identical.
- The trunk, rooms, robot dog, the bonus section, the challenges and the
  Wi-Fi error: compared from patched saves.
- Sound: 14,816 of 14,816 register writes on the puzzle path, 13 of them
  differing; the voices sample-identical.
- The auto-player plays from the logos to the staff roll in one run.

Not compared with the ROM: the save screen, the trunk's own pages and the
staff roll.

## Embedding notes (Reprise, 2026-10-29)

Observations from running the reconstruction inside Reprise, not new
findings about the original: the game thread is stepped one frame at a time
by the host, and the sound is rendered a fixed amount per frame (32 728 Hz
resampled to 48 kHz), so every sound-gated wait follows the frame count
instead of the device's real-time progress. Reprise's quick save stores the game's automatic checkpoint - the A5FP cartridge backup image (slot 0) that `RoomScene::run` writes once the room takes touches - and continues through the game's own load path. It is a checkpoint, not a snapshot of the running machine. Details:
[../reprise.md](../reprise.md).
