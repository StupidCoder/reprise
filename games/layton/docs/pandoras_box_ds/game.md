# Pandora's Box (Nintendo DS): how the game works

What we learned while reverse engineering the second adventure (German
cartridge `YLTD`), the first game of the family to be reconstructed. It covers
the cartridge and its 55 overlays, the data formats, the GDS script machine,
the game state and scene router, rooms and events, saves, the puzzles and
their 25 types, the minigames, the bag, the bonus section, sound, movies and
the display rules, and the original's own bugs. Remaining work is in
[plan.md](plan.md).

All findings come from the user's ROM and from runs of it. Sources were
Ghidra 12.1.4 exports of the ARM9 and ARM7 programs and every overlay (57
programs), sealed reference runs in a research build of melonDS (a measuring
instrument only, with observation hooks and no game patches), and a
general-purpose ARMv5TE interpreter that runs the ROM's own routines as an
oracle. No existing decompilation, disassembly, port, symbol map, asset map
or game-specific guide was consulted. Addresses are ARM9. Overlay 2 (the
engine) loads at `02068880`. Scene overlays load at `020efda0` and puzzle
engines at `020f78a0`. Statements marked **hypothesis** are not confirmed.
Detailed research notes, with evidence paths: `.analysis/docs/` and
`.analysis/RECOVERY-GAPS.md` (local, not in the repository).

Contents: 1 Cartridge and program · 2 Data formats · 3 The GDS script machine
· 4 Game state and scene router · 5 Rooms and events · 6 Saves and the title
· 7 Puzzles · 8 Minigames and the bag · 9 Ending, shack and bonus section ·
10 Sound · 11 Movies · 12 Display rules · 13 Bugs and oddities in the
original · 14 Research tools and checks


## 1 Cartridge and program

| Field | Value |
|---|---|
| File | the German cartridge image (`... (Germany).nds`) |
| SHA-256 | `76ed9184ce07c7d4cb342aff4cc960412f36b3d99558aebdb37a2db41cfc275e` |
| Size | 128 MiB |
| Header | game code `YLTD`, revision 0; header CRC 0x6E59, logo CRC 0xCF56 |
| File system | 138 directories, 2,642 named files, 2,697 FAT entries |
| ARM9 | stored compressed at ROM 0x4000; expands to 289,240 bytes at 0x02000000, entry 0x02000800 |
| ARM7 | ROM 0x0BE000, 160,048 bytes, uncompressed, entry 0x02380000 |
| Overlays | 55 ARM9 overlays (54 compressed, 917,568 bytes expanded); no ARM7 overlays |

The ARM9 image carries autoload sections at 0x01FF8000 (1,472 bytes) and
0x027E0000 (96 bytes); the ARM7 copies itself to 0x037F8000 and 0x027E0000.
Many overlays share load addresses (0 and 1 at 0x0205A120; scene overlays at
0x020EFDA0; puzzle engines at 0x020F78A0), so every address needs its overlay
number. SDK markers in the ARM9 include Nintendo backup, Zi SCRLIB,
Actimagine Mobiclip 1.1.1, DWC, Wi-Fi, CPS and SSL.

| Overlay | Role |
|---|---|
| 0 | handwriting recogniser |
| 1 | Mobiclip movie decoder |
| 2 | engine: resources, scripts, sound object, scene services, router, save |
| 6 | movie scene |
| 7 / 8 | room scene / event scene (never resident together) |
| 9 | title |
| 10 | narration |
| 11 | code entry |
| 12 | name entry |
| 13 | camera repair |
| 14 | herb tea |
| 15 | hamster |
| 16 | photo hunt |
| 17 | bag |
| 18 / 19 / 20 / 21 | notes / puzzle index / mysteries / diary |
| 22 | staff roll |
| 23 | bonus section |
| 24 | challenges |
| 25 | shack |
| 27 | puzzle scene (card, intro, hints, memo, result) |
| 30..54 | puzzle engines (section 7) |

Overlay 26 is all zeros. The first game's engines live on here as shipped but
unused overlays (ov28 Match, ov29 Slide, ov32 Drag, ov35 Connect, ov36 Cup,
ov40, ov44).


## 2 Data formats

**Compression wrapper.** Every `.arc`/`.arj` begins with a 32-bit selector,
then a standard BIOS stream: 1 run-length, 2 LZ10, 3 Huffman 4-bit, 4 Huffman
8-bit (dispatch at `0206C318`). `.plz` and `.dlz` start straight with LZ10.
The three `.arb` backgrounds are uncompressed. The ROM holds 1,807 LZ10, 220
run-length, 64 Huffman-8 and one Huffman-4 assets.

**PCK2 archives** (`.plz`, 77 files, 8,177 members): u32 header size, u32
archive size, `PCK2`. Each member: u32 member header size, u32 record size,
an unknown u32, u32 payload size, a NUL-terminated name at +0x10, the payload
at member-header-size. The game checks the magic at `0206C834` and searches
by name at `0206C8B8`.

**Backgrounds**: u32 colour count, RGB555 colours, u32 tile count, 8×8 8-bit
tiles, u16 map width and height, one halfword per cell (tile in bits 0–9,
flips in bits 10/11).

**Sprites**: u16 image count, u16 depth (3 = 4 bpp, 4 = 8 bpp). `.arj` adds
the palette count here; `.arc` stores it after the images. Each image has a
size and chunks (origin, width and height exponents: `8 << e`). `.arc` chunks
store linear rows and `.arj` chunks 8×8 tiles. After the images come the
palette and named animations (step count; next step, duration and image
index per step). An optional extension (`0x1234`) holds sixteen named
variables (such as `pos`) with eight planes of values. An animation advances
when `elapsed > duration`; the frame is drawn before the update.

**Fonts**: Nitro NFTR 1.0, 1-bit glyphs. `font18` is Shift-JIS; `fontevent`
and `fontq` are CP1252. Text advance in the centring path uses a separate
224-entry width table (`02072608`), while positioned and typed text use the
NFTR glyph width (a `?` is one pixel wider there).

**DLZ tables** (16 files): u16 row count, header size, row size, an unknown
field, then rows. Sorted tables use one binary search (section 4).

**Other files**: 4,756 GDS scripts; 358 SAD streams; 15 SMD sequences, 82 SWD
wave banks and 63 SED effect sets; 23 MODS movies; 20 NSBMD models, 19 NSBCA
and 4 NSBMA animations and 19 `.mtn` motion tables (all for the hamster).
`decuma/_databas_le.bin` is the handwriting database.


## 3 The GDS script machine

Scripts are separate programs (logo, events, puzzle parameters, movie
subtitles). A script is a u32 byte count, then tokens: a u16 type, then 0 a
u16 command, 1 an int32, 2 a float32, 3 a u16 length and string bytes, and 12
end. Dialogue `t*.gds` files are structured records (speaker, animations,
text) read with the same reader, not programs.

- **Executor** `0207CEE0`, token reader `0207CCD8`. Tag 12 ends a script;
  command 1 skips to the end of the body. Non-command tokens update their
  persistent value slots and are skipped.
- **Handlers** sit at `0207C950 + op × 4`. Most call a fixed virtual slot of
  the running object (room, event, puzzle engine), so one command number means
  different things in different scenes.
- **The 0x78 pre-pass.** The event constructor runs `0207CFF0`, which executes
  only commands whose handler is `0207B634`: command 0x78, which stores a
  reward in state +0x8200. The main dispatcher skips 0x78. Command 0x74 later
  shows that reward and adds one more.

There is no structured flow control in this game (the first game has
if/else and loops).


## 4 Game state and scene router

One global state object (pointer `020889F4`, 0x82C4 bytes, constructed by
`020831C8`; new game `02083368`). Its **progress block** (0x320 bytes at
+0xC8) is what the save stores. The ROM's own debug strings name its bit sets:

| Offset | Content |
|---|---|
| +0x004 | `EventViewed`, 1,024 bits |
| +0x084 | `StoryFlag`, 128 bits |
| +0x094 | 128 byte counters (scripts 0x35/0x36, place conditions) |
| +0x114 | puzzle bytes, by puzzle number − 1: status in bits 0–1, failures 2–3, hints 4–5, passed to the shack bit 6, favourite bit 7 |
| +0x1EC | `HintCoin` bits (place × 4 + i) |
| +0x22C / +0x22E / +0x230 | hint coins, found count, picarats |
| +0x234 / +0x238 / +0x23C | story step, place, place version |
| +0x24C | camera parts; bit 10 = camera solved |
| +0x280 / +0x281 | owned herbs, brewed teas |
| +0x284.. / +0x28E | hamster items, the hamster's 8×6 field |
| +0x2BE | hamster step record |
| +0x2ED | photo taken |
| +0x2F4 | correct teas |
| +0x2F7 | hamster name (default "NO NAME") |
| +0x312 | pending chained event |
| +0x318 / +0x319 | notes page, index selection |
| +0x31A / +0x31B | bonus codes entered |
| +0x31C / +0x31E | goal, companions |

Other names in the ROM: `MemoFlag`, `MemoNewFlag`, mystery flags, story
items, `MenuNewFlag`, `TeaCorrectFlag`, `PhotoPieceFlag`, `TutorialFlag` and
the diary flags. Bit helpers (`020820EC`/`02082110`) read out-of-range bits as
clear and ignore out-of-range writes.

**Tables.** `place/data.plz` holds `placeflag.dat`, `storyflag2.dat` and
`autoevent2.dat` (copied into the state). The rc tables are `ev_inf2`,
`ev_fix`, `nz_lst`, `goal_inf`, `chp_inf`, `snd_fix`, `tm_def` and `sb_pht`.
Every sorted table uses the same search (e.g. `020848F0`): an unsigned check
against the last key, then power-of-two steps that probe `lo + step` and move
`lo` on a positive signed difference. Equal rows do not stop the loop.

**Story step** (`02083AA8`): every `storyflag2` row (34 bytes) for the
current step checks up to eight conditions (1 story flag, 2 puzzle solved).
Any failure ends the pass. A passing row sets the step to the *next* row's
step, and the scan continues.

**Place version** (`020839FC`): versions 1..15 each hold a step range and a
counter condition (equal, not equal, at least); the last match wins.

**Automatic events** (`02083B70`): eight (event, first step, last step)
records per place. An unviewed event in range becomes the current event.

**Scene router.** Script commands 0x06/0x07 write the current and following
scene (state +0x478/+0x479). The main loop `02068CCC` dispatches through
`020688A8`.

| Scene | What | Scene | What |
|---|---|---|---|
| 1 | room | 21 | staff roll |
| 3 | event ("drama") | 22 | Wi-Fi puzzle list |
| 6 | movie | 23 | notes |
| 8 | puzzle result | 24 | challenges |
| 10 | puzzle | 25 | tea request (script 0x73) |
| 12 | title | 26/27 | photo hunt |
| 13 | narration | 28..36 | bonus section |
| 14 | camera repair | 37 | name entry (also the hamster's name) |
| 15 | herb tea | 38/39 | Wi-Fi settings, download |
| 16 | hamster | 40..42 | passcode, code entry |
| 17 | bag | 43 | diary |
| 19 | puzzle index | 44 | shack |

A room scene (`02068D18`) first plays an unviewed automatic event (kind 6
needs a place change; skipped when the following scene is already an event).

**Puzzle result** (`02069468`): the result event is the puzzle's event + 3
when solved (+1 more with flag +0x8220), + 4 otherwise, − 1 after a retry
variant, − 2 with +0x821F.


## 5 Rooms and events

**Place files** (`n_place%i_%i.dat`, 910 bytes, per place and version): label
(`p_%u.txt`), map marker, two backgrounds, four hint-coin rectangles (+0x1C),
sixteen touch objects (+0x2C), twelve background animations (+0xCC), sixteen
event objects (+0x24C), twelve exits (+0x2CC) and the room-sound key (+0x38C).

Each frame the room tests, in order: event objects (`020F1C98`), hint coins
(`020F1EF8`), touch objects (`020F1BFC`), the move-mode button and exits
(`020F2948`), and direct exits of type 1/3 (`020F2B5C`). Exits trigger events
(types 2/3) or change place. Event objects hide once viewed (kind 1) or
solved (kind 4).

**Event variants** (`020F2E1C`): a puzzle event adds 1 when the puzzle was
seen (the retry variant) and 2 when solved; events ≥ 30000 add 4 once viewed.
Footsteps (0xE6, 0xE7, 0xEB, 0xE9) depend on the floor type, and the next room
waits for them.

**Event scene** (ov8, constructor `020F0380`, run `020F3B20`): data
`d%i_%.3i.dat` and the `ev_inf2` row (kind, room-sound key, puzzle, story
flag), the script, then the goal row (a nonzero kind shows `next_%i.txt`), the
pending chained event with its fade, the closing fade, and the post-game
special cases (18000, 18480, 18490).

Recovered event commands include:

- 0x05 set place, 0x2C actor fade, 0x30 actor position, 0x35/0x36 counters;
- 0x49/0x5F music volume over frames;
- 0x61 the end (music fade, title);
- 0x6A/0x6B background shake;
- 0x70/0x71/0x7D mysteries;
- 0x73 tea request, 0x74 reward, 0x76 pass a puzzle to the shack;
- 0x77/0x7A story items, 0x79 feature window;
- 0x7B save prompt, 0x7C wait, 0x7E actor shake;
- 0x7F/0x81/0x82 fades, 0x83 counter jump, 0x84 photo piece;
- 0x89 stop ambience, 0x8C timed music, 0x90/0x91 viewed bits;
- 0x94 hamster named, 0x95 torn photo, 0x96/0x97 companions join/leave;
- 0x98..0xA0 effects and streams;
- 0x9B adventure result and rank, 0x9C count-gated jump, 0x9D yes/no,
  0x9E notice, 0xA1 notice unless the game was completed;
- dialogue controls `@W`, `@V` and `@S`.

**Dialogue.** A blip plays every fifth byte when there is no voice and no
fast-forward. Window styles 4–6 are quiet. A speaker talks (its "X" animation
switches to "*X") while its voice stream is loud: max − min of the last refill
> 6000, sampled every 11th frame (`0207E500`). A page ends on a touch once no
tracked sound is busy.

**Narration** (ov10): modes 0–3 by event (10010, 10030, 13190, 17190). Page
groups and voices come from `020F0790..020F099C`. The page alpha goes to the
DS polygon attributes.


## 6 Saves and the title

The backup is 8 KiB (`backup.sav` natively, byte-compatible):

| Offset | Size | Content |
|---|---|---|
| 0x000 | 16 | `ATAMFIREBELLNY`; without it the card is formatted |
| 0x010 | 200 | header: checksum, used-slot mask, three 64-byte summaries |
| 0x110 + 0x370 n | 800 | slot n: the progress block, word 0 its checksum |
| 0xB60 | 144 | shared block (bonus codes, downloads), checksummed |

The summary holds the name, place (place 17 is stored as 100 once event
11220 was seen), play seconds (capped at 99:59:59), puzzles found and solved,
and the finished level (1 finished, 2 with all 138 story puzzles, 3 with all
153). Checksums (`0206B1B4`) are two sums over halfwords starting at 0xFFFF,
folded every 360 words, giving `a | b << 16`. Saving writes the header with a
zero checksum first, then the block, then the summary and the header with its
checksum. At boot a bad header damages all slots; any damage shows
`save_corrupt` and the damaged parts are cleared.

The title (ov9) shows Continue and Secret once a slot is used. Continue
starts the `chp_inf` row of the loaded story step: its event, or the
replacement once the row's seen bit is set. Secret enters the bonus section.


## 7 Puzzles

**Records.** 206 record slots: 187 named puzzles, 19 empty slots (blank title
and texts, no script). Internal id, record number and display number are
different fields: puzzle 002 is internal 116 (type 5), puzzle 005 is
internal 4 (type 26). Special-catalogue records have no catalogue number in
the ROM. Record 206 (number 0) is a gift puzzle: no status, always the result
event 26504. `nz_lst` also has records 154–205 for weekly Wi-Fi puzzles. 33
of them have data on the cartridge and the shared save block has room for
exactly 33 download entries. **Hypothesis:** a download only unlocked
puzzles already on the cartridge.

**Puzzle scene** (ov27):

- **Card.** Title, number, worth, maximum and score; jingle `ST_010`. After a
  failure the worth counts down.
- **Intro.** The question is revealed two characters a frame. The tutorial
  shows once per kind; its seen bits are saved.
- **Hints.** Buying a hint sets the count, then debits one coin. A solved
  puzzle's hints are free.
- **Memo.** The tools slide away, the window grows from the top right, and the
  pen stamps glyph 0x60 of `fontq` inside 11..250 × 6..181. Ink is kept
  between visits.
- **Judgment** (`0206B4FC`, a 19-row table at `02086F18`). Brightness rises by
  7 (2 for the last image), then six-frame holds, 17 fade waits, a 22-frame
  hold, twelve 16-pixel slide steps, a 37-frame hold and a random voice.
- **Reward.** Failures 0 / 1 / 2+ select one of three reward bytes. Solved
  puzzles, record 206 and following scene 22 earn nothing. The count adds two
  per frame for awards ≥ 25 and waits an extra frame below 15. Reaching zero
  does not exit; another touch is needed. Status is committed after the
  result screen.
- **Retry.** After construction the scene calls the engine's restore (virtual
  +0x230) and before deletion its save (+0x22C). One slot (≤ 0x300 bytes) is
  kept only when the result chose retry, so toggles, lamps, tiles, colours and
  selections keep their state on a retry; the others start fresh.
- **Judging.** Engines whose virtual +0x220 returns 0 are judged every frame
  (slides, roses, skating, pairs, stones); the others wait for the answer
  button.

**Random numbers** (`02034DAC`): `state = state × 0x41C64E6D + 0x3039`,
returning `(state >> 16) & 0x7FFF`.

**Types** (25 used; numbers are the script's type ids):

| Type | Overlay | Puzzles | Rules |
|---|---|---|---|
| 26 | 48 | 12 | tiles onto slots with swaps |
| 18 | 48 | 5 | rotating tiles: drag the middle to move, the rim to turn (quarter turns); keys insert; record 203 adds the sun and the microphone (amplitude > 192 for 31 updates in a row) |
| 5 | 33 | 20 | circle the answer: a stroke selects region centres inside its bounding rectangle; too small or several centres is rejected |
| 3 | 31 | 9 | select several, then answer |
| 2 | 30 | 25 | multiple choice; a click submits after the button delay |
| 10 | 38 | 6 | cycle each object's picture; −1 is a wildcard in the answer |
| 13 | 41 | 4 | pancake towers (3, 4, 5, 7 pancakes); a larger cannot go on a smaller; automatic judgment after 45 updates; moves capped at 9999 |
| 17 | 43 | 4 | knight's tours on 3×4, 5×6, 6×6 and 8×8; automatic after every square |
| 29 | 50 | 6 | peg solitaire on a fixed 7×7 cross; automatic with one ball left |
| 16, 22, 28, 35 | 42 + 0 | 50 | written answers with the handwriting recogniser |
| 9, 15 | 37 | 15 | lines: type 9 draws orthogonal paths cell by cell; type 15 places straight lines, merges overlaps, limits count and total length (1300.0) |
| 25 | 47 | 14 | sliding blocks of several rectangles; release snaps to the nearest cell; solved waits 45 updates |
| 23 | 45 | 3 | toggles |
| 24 | 46 | 2 | roses: each cell's 5×5 reach is a two-step flood that respects walls; every cell lit exactly once |
| 31 | 52 | 3 | street lamps on a 7×7 grid light the streets through them |
| 27 | 49 | 3 | ice skating: arrows beside the protagonist; slides stop at walls, the edge or the exit |
| 30 | 51 | 2 | pairs: drag two neighbours onto two empty slots |
| 33 | 53 | 1 | stepping stones on an 18 × 2 board; steps of one or three rows |
| 6 / 34 | 34 / 54 | 1 / 1 | trace a ring (13 ≤ r ≤ 53 around (95, 95), all four quadrants) / enclose (flood fill from a seed) |
| 11 | 39 | 1 | tiles on slots, nearest slot by binary32 distance < 1000 |

**Handwriting recogniser** (ov0, entry `0205A12C`, database
`_databas_le.bin`): resampling, analysis, full and coarse matching, candidate
generation, rescoring and guide penalties. The answer screen folds a..z
through `02043218` in puzzles but keeps raw letters for names. The
disambiguation rules exist but are unreachable (ov42 never sets the setting).


## 8 Minigames and the bag

All side screens use the same overlay-2 services (frame, fades, sprites,
buttons, text layers); see section 12 for the display rules.

- **Herb tea** (ov14, scenes 15/25): twelve teas, eight herbs, three pot
  slots. The recipe code is the sorted herb triple, looked up in
  `ht_rcp.dlz`; an unknown blend gets a remark from the code digits. Serving a
  request moves the event by +1 (right) or +2, −4 on a revisit; quitting by
  +3. Opening the bag rolls a tea request (21 %, story step > 169).
- **Camera repair** (ov13, scene 14): ten parts from a five-slot tray, moved
  on 16-pixel cells or turned to the nearest quarter. Each placement is stored
  as `placed | rotation << 12 | y << 6 | x`. Solved: stream 5 and event 18430.
- **Photo hunt** (ov16, scenes 26/27): after the repair, a room with a row in
  `sb_pht.dlz` shows the camera icon. Taking the photo flashes the screen.
  The hunt circles the differences; nine photos complete give event 18500.
- **Hamster** (ov15, scene 16): an item screen (drag owned items onto the 8×6
  field) and a 3D walk.
  - Five hamster models, the map, ten objects and four effects.
  - An orthographic follow camera.
  - A depth-first search over four cells that prefers item kind 2. With
    "smell", the nearest food is chosen by turn angle plus squared distance.
  - Item reactions: eat, play, jump, search back, trampoline, remove.
  - Reaching the goal raises the level; level 6 plays event 18510.
- **Bag** (ov17, scene 17): status on the upper screen (medal by coin count,
  play time, place). Buttons for memo, mysteries, index, save, camera,
  hamster, tea and diary, with "new" marks. The reset prompt restarts the
  game.
- **Mysteries** (ov20): ten entries. The reveal flashes, scales the picture
  from 1.2 and shakes; the stamp pulses for 90 frames.
- **Notes** (ov18): 57 notes, six per page.
- **Diary** (ov21): twelve pages.
- **Puzzle index** (ov19/27, scene 19): all or favourite puzzles, a scroll bar,
  prize, picture and picarats; solved puzzles can be replayed.


## 9 Ending, shack and bonus section

**Staff roll** (ov22, scene 21): three frames a step; it ends at 3 × steps
≥ 20,280.

- Lower screen: 36 credit pages in a 192-step cycle.
- Upper screen: 19 pictures in a 362-step cycle, blended over a paper sprite
  grid.
- Each loader step reads, then expands, one file.

The game then shows the adventure result (solved count and rank), saves the
completed game (resumed in the stored room, where the ending can be replayed)
and returns to the title.

**Shack** (ov25, scene 44): puzzles passed on by script 0x76 and unsolved,
six per page. After a solved shack puzzle event 18000 plays; with a completed
game and all 138 solved, 18480.

**Bonus section** (ov23, ov11, ov24):

- Scene 28 menu: Wi-Fi, index, challenges, extras.
- Extras: five rooms open at 2,500..4,500 picarats on a finished slot.
- Art (10 pictures), character files (5 pages, 6 with all 153 solved), music
  (19 tracks), voices (30), movies (23).
- Passcode page and code entry. The challenges hold three puzzles per
  category.

The passcode generator (`0206BA48`) uses the console's MAC address `m0..m5`
and a seed `k` to make eight bytes: `(m0+m3)k`, `(m1+m3−m4)+k`,
`(m2+m3+m4+m5)k`, `m3−k`, `m4·k`, `m5+k`, `(r0+r3+r9)k`, `(m3−m4−m5)−k`. Even
positions become `'0' + b % 9`, odd ones `'A' + b % 7`. Seeds: 0x4E59 for the
own code, 0x4E4B and 0x4B55 for the unlock codes (events 26500, 18440).


## 10 Sound

Music and effects use a resident ARM9 sequencer for chunk files
(`smdl`/`swdl`/`sedl`; the magics were recognised from general knowledge,
every structure was read from the code). Streams are SAD files.

- **Driver** (`02000D64..0200BA7C`). The sound thread runs every 0x1474 × 64
  clocks: players and effects, external MIDI input, stream fades, envelopes,
  voices, output and status. External MIDI input never runs: its source
  pointer `020494D0` is only ever cleared.
  - Division helpers return the numerator for a zero divisor. Reciprocal
    multiplies divide by 127, 16,129, 8,032 and 127³.
  - The heap allocator zero-fills every block, so the track's note-mute byte
    starts at 0.
- **Sound object** (ov2 `0207F384`):
  - Effect groups are chosen by id: < 50 SY, < 200 GE, < 1000 SI.
  - The room sound table (`snd_fix.dlz`, rows {id, bgm, ge, si}); durations
    from `tm_def.dlz`.
  - The music controller fades over time id 1000 (1010 in movies).
  - The stream controller is a flag machine polled once per frame. A new
    stream opens only after the previous one has stopped, so it can start one
    or more frames late.
  - Stream routes: 0 `ST_%03d`, 1 `bgm/BG_%03d`, 2 `event/ge/%03d_%d`,
    3 `movie/ge/M%d`.
  - A lid close pauses everything and resumes it afterwards.
- **SAD** (header `sadl`, version 0x0415, nominal rate 32,728 Hz). 357 files
  are codec 0xB2 and one (`BG_002`) is 0xB4.
  - Each 16-byte channel block holds 30 predictor nibbles and a header byte,
    XORed with 0x80. Coefficients (`0203E300`): (0,0), (60,0), (115,−52),
    (98,−55), (122,−60).
  - 0xB2 inserts a midpoint before each sample and runs a two-step filter
    whose coefficients come from the cutoff: `float(2π·cutoff / 32728)`, the
    two weights truncated separately.
  - Output is quantised to 64 units.
  - The channel reset clears only 16 of 24 bytes, so the filtered tail carries
    over into the next stream (the first narration voice starts at −3196).


## 11 Movies

Movies are Mobiclip `MODS N3` files (23, all 256×192, no embedded audio; the
sound is a SAD stream). The decoder (ov1, frame routine `02061870`,
hand-written ARM) works like this:

- **Bit reader.** An MSB-first cache over 16-bit words.
- **Intra frames.** QP and table choice; per macroblock type A (one mode for
  four 8×8 blocks) or type B (a mode per block, predicted from a cell cache
  with an inverted most-probable-mode flag).
- **Predictors.** Vertical, horizontal, a bilinear plane with a corrected
  corner, DC, and five diagonals.
- **Predicted frames.** Median motion vectors; a 16-node partition tree from
  16×16 down to 2×2. Luma moves in half pixels and chroma in quarter pixels,
  every interpolation truncated.
- **Coefficients.** 12-bit lookups of run/level/last codes with three escapes;
  H.264-style 8×8 and 4×4 integer transforms.
- **Colour** (`0205B178`). `R = Y+Co−Cg`, `G = Y+Cg`, `B = Y−Co−Cg`, clamped
  and shifted. Within each 2×2 block, two luma samples are lowered by 4 (an
  ordered dither).

The same frame routine is used by the 3DS games (see the shared decoder
notes). Subtitles come from the movie scripts.


## 12 Display rules

- **Present** (`0206CFB8`) swaps 3D and OAM, then waits one frame. Plain waits
  never swap, so the last sprites stay on screen.
- **Swap latency.** Sprites and BG scroll appear one frame after register
  writes such as brightness and windows.
- **Master brightness** on 6-bit colour: darken `c·(16−L)>>4`, brighten
  `c + ((63−c)·L>>4)`.
- **Blending** runs on the 6-bit pipeline: `(2a·EVA + 2b·EVB + 8) >> 4`.
- **Untextured quads** blend as `(f·(a+1) + b·(31−a) [+16]) / 32`.
- **Fades** (`02071790` / `02071E84`) take 17 iterations.
- **Display reset** (`0206DDEC`) puts the text layer above the 3D sprites.
- **Windows.** WIN0/WIN1 hide the text layer while a card slides.
- **Screen shake.** Two random draws per frame (`r % 3 − 1`), shown one frame
  later on a wrapping 256×256 map.
- **Scaled sprites** (`02076124`) are drawn per texture chunk in 20.12 fixed
  point.
- **Touch debounce** (`0206E2C0`) is used by every puzzle scene.


## 13 Bugs and oddities in the original

- **Lines (ov37)**: with no point in the drag direction, the snap reads two
  uninitialised stack words as its target.
- **Toggles (ov45)**: disabled rectangles clamp with swapped axes (`x2 > cols`
  clamps `y2`, `y2 > rows` clamps `x2`).
- **Choice and selection engines**: a button's age is uninitialised until the
  first click. The add handler accepts up to 24 choices, but the object holds
  16 pointers before its answer bytes.
- **Puzzle result (internal 135)**: the following-scene test reads +0x479
  right after setting it to 1, so it always takes +3/+4 from the last judgment
  flag.
- **Zero-frame fades** (0x72/0x80): 16.0 / 0.0 is infinity in binary32, so
  the fade takes one frame and lands at the end level.
- **Mystery stamp**: plays a third animation index on a two-animation sprite;
  it keeps animating as before.
- **Bonus extras**: a 3D button partly off screen draws garbled.
- **Mono streams**: all 293 metered voices are mono. A stereo one would write
  its second channel into the music object.
- **Load lag**: card reads make the DS skip frames (photo album, staff roll,
  bag pages). Waits gated on audio therefore end a few frames earlier on the
  console.


## 14 Research tools and checks

Research tools live in `.analysis/tools/` (local):

| Tool | Purpose |
|---|---|
| `run_reference.py` | sealed reference runs, with `--state-fixture` / `--restore` |
| `validate_story_state.py` | 227 RAM snapshots against the native story state: 220 exact, 7 mid-event, 0 mismatches |
| `golden.py` | 128 native scenarios against a baseline |
| `sequenced_audio/` | the oracle for the sound driver: 78/78 scenarios and 9/9 stream scenarios with 0 differing ticks; the mixer is sample-identical by register replay |
| `run_mobiclip_validation.py` | all 27,994 movie frames against the ROM routines |
| `run_engine_regression.py`, `run_answer_checks.py` | 87 engine runs and 50 written answers |

Native probes: `picarat_story_explorer` (plays the story to the staff roll
and post-game), `picarat_tea_probe --scene tea|bag|camera|photo|room|hamster`,
`picarat_g3d_probe`, `picarat_continue_probe` and `picarat_bonus_probe`.
