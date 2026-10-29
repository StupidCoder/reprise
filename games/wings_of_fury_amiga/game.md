# Wings of Fury (Amiga) — how the original works

What was learned while rebuilding Wings of Fury (Broderbund, 1990, version
1.0): the disk and its crack, the programs and data formats, the frame and
logic ticks, the flight model, weapons, islands, enemy planes and ships, the
front end, music and saved games, and the original's own bugs. The
remaining work is in [plan.md](plan.md).

## Sources and evidence

- **Image:** `Wings_of_Fury_v1.0_1990_Broderbund_cr_Hysteria.adf` (901,120
  bytes), SHA-256
  `638d9a9d1b725b62165f4dd4e69ac589b05f84e156223f1568b99baea4620044`, a
  cracked copy (the user agreed to its use).
- **Method:** everything here comes from this image (own ADF and hunk
  readers, capstone, Ghidra with the small-data base set) and from runs of
  it in FS-UAE (A500, Kickstart 1.3, GDB stub). No outside material on this
  game was used; RetroReverse has none. Symbol names such as `_PlaySfx` are
  the game's own (`songplay` and `wofsongs` keep their symbols).
- **Addresses:** `$1xxxxx` is a location in the research layout of
  `wings` (flat at $100000); `−$xxxx` is an A4-relative global
  (A4 = $11AF52 in that layout).
- **Paths:** source paths are relative to the Reprise root. Evidence lives
  outside the repository in `.analysis/wings_of_fury_amiga/`:
  - `work/`, `ghidra/` (with `SetA4.java`);
  - `harness/`: `run.sh`, `wof.py`, `viewref.py`, `flightrec.py`,
    `flightpoke.py`, `dashgrab.py`, `musicrec.py`, `ffpref.py` (the FFP
    oracle), `ready.py`, recordings;
  - `tools/`: `rpck.py`, `ppkc.py`, `iffinfo.py`, `flightcheck.sh` (all 45
    recordings and 15 set-up checks), `viewcheck.sh`, `dashcheck.sh`;
  - `previews/`: rendered checks.
- **Checking tools:** `build/wings_of_fury_replay` (modes `flight`,
  `simulate`, `setup`, `maps`, `frontend`, `briefing`, `ticker`, `music`)
  and `build/wings_of_fury_assets`. "Joyport" in the notes is the native
  re-implementation's earlier project name.

## The original's bugs and oddities (summary)

Each is described where it belongs below.

- **Misplaced crash blast** (`$10B304`): a sliding crash on land passes the
  plane's x:altitude words as an address, so the blast lands at a map x
  that depends on where the map sits in memory (rebuilt with the reference
  machine's value).
- **Carrier step test** (`$101CAE`): `cmpi #1,$14` has its flags overwritten
  by the next `move`, so while the plane is below deck it comes up at every
  step; a lost plane coming up below deck also ends an accomplished
  mission.
- **Battleship's sixth parked plane:** its row-9 count is 6 but its
  template holds 5 planes, so the sixth is read past the template (kept).
- **Map columns:** 8 bytes past a map's columns are read uninitialised (read
  as 0 in the port).
- **Lost audio interrupts:** the music's handler reads INTREQ once and
  clears all four channels' requests, so an interrupt arriving meanwhile is
  lost and a one-shot plays twice (not reproduced).
- **No ending:** after map o the game goes on with m, n, o, each a
  promotion with the fireworks.
- **Dead text:** an easter-egg string is never referenced.
- **The crack** removed the call to the manual-word check; the routine and
  its table are still in `wings`.

## Disk, crack, programs, research setup

Image: `Joyport/Wings_of_Fury_v1.0_1990_Broderbund_cr_Hysteria.adf`, 901,120
bytes, SHA-256 `638d9a9d1b725b62165f4dd4e69ac589b05f84e156223f1568b99baea4620044`
(MD5 `648e7c844d2f59bddfa8eeea49f26d9a`).

### Sources and limits

- Everything here comes from this image (static reading so far; no run yet).
  No disassembly, port, map or guide of this game was used. The user's
  RetroReverse project has no Wings of Fury material (searched 2026-10-27).
- Symbol names quoted below (`_PlaySfx`, `voices1`, `BassDrum3Data`, …) are
  the game's own: `songplay` and `wofsongs` keep their HUNK_SYMBOL blocks.
- Tools: Joyport's `build/amiga_disk` (list, extract, hunks, flat), Ghidra
  12.1.4 headless (`ghidra/ghidra.sh`, with `SetA4.java` for the small-data
  base), capstone (`tools/m68kdis.py`), Python research scripts in `tools/`
  (`rpck.py`, `ppkc.py`, `iffinfo.py`).

### The crack

This is a cracked copy ("Hysteria"). What differs from a retail disk, as far
as this image shows:

- `s/startup-sequence` is `type s/text`, `closewb`, `cd wings_of_fury`,
  `wings`; `s/text` (2,332 bytes) is the cracker's ANSI banner.
  `s/startup-sequence.bak` (`initser`, `closewb`, `cd wings_of_fury`,
  `wings`) is presumably the original.
- The game's manual-word check is still in `wings` but is never called:
  `$10F41A` (draws "You are required to enter a word from the manual … PAGE
  %d LINE %d WORD %d", picks one of 33 entries of the table at `$116622`
  — u16 page, line, word, then the word XOR `$BA` after `& $5F` — and also
  accepts the fixed word at `$10F727`). Nothing calls it directly, through
  its A4 jump-table thunk `$1131E8` or through any pointer in the image.
  Where the original called it is not known (no retail image to compare).
- No other change is visible: the disk is plain AmigaDOS (no custom boot
  block, no protection track), and the game loads its data with dos.library.

Joyport does not rebuild the manual check; the import accepts this release
by the SHA-256 of `wings`. A retail `wings` would differ (the call site), so a
second release entry will be needed when one turns up; the data files should
be identical.

### File system

OFS, volume `Wings of Fury`, all block checksums valid. Workbench 1.x
support files (`c/`, `l/`, `libs/`, `devs/system-configuration`, icons) plus:

| File | Bytes | What it is |
| --- | --- | --- |
| `Wings_of_Fury/wings` | 94,292 | the game (3 hunks: CODE 77,644, DATA 20,464, BSS 4) |
| `Wings_of_Fury/songplay` | 5,148 | music and effect player (symbols kept) |
| `Wings_of_Fury/wofsongs` | 41,328 | songs, voices and instrument samples (symbols kept) |
| `Wings_of_Fury/shapes/*.shp` | | shape libraries (`PPkc`, mostly inside `Rpck`) |
| `Wings_of_Fury/shapes/…` | | IFF ILBM pictures and palettes (see "Data formats") |
| `Wings_of_Fury/maps/a.map`…`o.map` | 1,910–7,138 | the fifteen mission maps |
| `Wings_of_Fury/sounds/*` | | 8-bit signed effect samples (8 files) |
| `Wings_of_Fury/newarmyfont` | 2,476 | the game's own bitmap font (format open) |
| `fonts/army.font`, `fonts/army/12` | | a 12-point disk font ("army") |
| `Wings_of_Fury/highscore` | 360 | ten entries (u32 score, u16 rank, 30-byte name) |

The game writes `highscore`; with a command-line argument it also records
the player's input (below), and it reads `wofdemo` for playback when present
(not on this disk).

### The program `wings`

- Compiled C (Aztec-style: `LINK A5` frames, globals and library calls
  relative to A4) for the front end and file handling, plus a large body of
  hand-written 68000 with register calling (`MOVEM.L D0-D7/A0-A6`) for the
  game itself: objects, map scrolling, blitter drawing.
- A4 = data hunk + `$7FFE` = `$11AF52` in the `$100000` layout. The data hunk
  starts with 185 `JMP abs.l` thunks (`$112F54`…`$1133A4`); `JSR -n(A4)` calls
  go through them. `SetA4.java` gives Ghidra the register value and makes the
  thunks functions, so the decompiler names the targets.
- Custom chips: `$DFF000` kept at `-$46C6(A4)`; the shape blitter is at
  `$110B0C` (see "Data formats").

#### Main flow (`$100006`, read from the code)

1. Set up (`$102570`, `$102910`), argument count > 1 → record mode
   (`$116EDA` = buffer at `$1133AC`).
2. Title sequence `$108022`: song 2; scrolling story `$107E80` (the 53 lines
   at the pointer `$1158E0`, justified to 615 pixels unless a line ends a
   paragraph; any key ends it); song 1; the Broderbund, title and credit
   pictures with timed waits (`$106EEE`, 300/600 ticks).
3. Loop: rank selection `$108262` (eight entries: seven ranks + one more
   option, `$108B96`; ranks 0–6 start at maps a, d, g, i, k, l, m, list
   `$1135A4`). Playback mode reads inputs from `wofdemo` (`$105D3E`), record
   mode stores them (5,000-byte buffer).
4. Map load `$102ADC` (see "Data formats"), then the mission loop (`$10CCF6`,
   `$101386`, `$10174A`, …), end-of-mission / high-score screens
   (`$109856`), and back to rank selection.

### Research layout

| Program | Base | Notes |
| --- | --- | --- |
| `wings` | `$100000` | A4 `$11AF52`; `work/W.bin`, `work/W.c` (Ghidra), `work/W.asm` (capstone) |
| `songplay` | `$180000` | `work/P.bin` |
| `wofsongs` | `$190000` | `work/S.bin` |

`ghidra/ghidra.sh import W` then `ghidra/ghidra.sh all W work/W.c`.

### Reference machine (harness/)

FS-UAE (prb28 fork, `~/Development/fs-uae`), Kickstart 1.3 (RetroReverse
`kick13.rom`), A500, GDB stub on port 6862 (`WOF_PORT`; 6860/6861 belong to
other sessions). Window must stay open (screenshots via `shot.sh`/`winid`).

- `mkboot.py <adf> work/ref/boot.adf --hold`: copy of the image whose `wings`
  starts with `bra.s *`; `run.sh` boots it.
- `wof.py attach`: waits for the spin, restores the JMP, prints the hunks
  (cli_Module of the Initial CLI); `Program.rt/ours` map addresses; A4 at run
  time = hunk 1 + $7FFE.
- No watchpoints in this stub (`Z2` answers E38).
- `wof.inject(d, prog, events)`: input by breakpoints at the reads — fire
  `$11044E`/`$11045E` (D0 1 = pressed), stick `$1104AA` (front end: 1 up,
  3 right, 5 down) and `$10522E` (flight: bits 1 up, 2 down, 4 left, 8
  right). see "Input".
- `viewref.py takeoff`: dismiss the menu, poke full speed, hold left until
  airborne.
- `viewref.py grab follow …` keeps the game's camera; `WOF_POKE="5F86=160,…"`
  writes words at every pinned frame start (states the harness cannot fly
  into). The grab also saves the small-data area at the drawn frame's start
  and the next one (OUT.a4before/after) and resolves shape pointers to
  "library name mirrored" via the loaded libraries' indexes.
- `screen.py out.png`: decodes the BitMap at `-$3806(A4)` (320×162, 5 planes)
  in the day palette. The view is double-buffered (−$3806 / −$38B2, the
  target −$3EB0 alternates; the dashboard is a 640×37×4 BitMap), so use
  `viewref.py` for comparisons.
- `viewref.py start|fly|grab X OUT.png`: start = fresh machine (with
  `WOF_MAP=k.map` rank 0 loads that map instead of a), fly = press fire until
  the flight frames run, grab = pin the camera, read the finished buffer and
  the backdrop state (OUT.raw, OUT.state, OUT.png). Breakpoint stops can
  arrive after a stale packet; `WOF.halt()` stops a machine that may
  already be stopped (the stub does not answer an interrupt then).
- Globals seen: player x/y `-$5F84`/`-$5F86`, camera `-$41A2` (copied at
  `$100F90`), vertical `-$41A8`. Moving the player low over land crashes
  the plane (costs a life).
- Observed: without input the rank screen times out into a mission on the
  carrier (rank 0, map a) with the weapon menu up. After the last plane is
  lost the game returns to rank selection, whose eighth line reads "RETURN
  FROM R&R". Stick code 5 = down (rank menu).

## Data formats

All addresses are `wings` in the `$100000` layout (see "Disk, crack, programs"). Decoders:
`games/wings_of_fury_amiga/data` (C++), `tools/*.py` (research).

### Packed files: "Rpck" (verified)

`$10FF16` opens a file and reads 16 bytes:

- `"Rpck"`, u32 unpacked size: allocate that size, read the rest, unpack with
  `$10FEE0`: control byte n (signed); n < 0 copies −n literal bytes, n ≥ 0
  repeats the next byte n + 1 times; until the end of the file.
- `"Pckd"`: refused (no such file on the disk).
- anything else: read as it is.

Some files decode one byte past the stated size (cruiseship.shp,
japplane.shp); the buffer has the stated size, so the byte is dropped.
Check: all 12 packed files decode to exactly their stated size (+0/+1).

### Shape libraries: "PPkc" (verified against the blitter code)

Library: `"PPkc"`, u16 n, n four-character names, n u32 offsets counted from
`6 + 8n`. Lookup by name `$110560`. The game keeps lists of the names it
wants (data around `$113B60`, e.g. `flg0…` for `japcarrier.shp` at
`$113B7C`, loaded by `$105BC6` with A0 = file name, A1 = name list).

Shape header (20 bytes) and planes, as read by `$1109BC` (clip), `$110B0C`
(draw) and `$110CE2` (mask):

| Offset | Size | Meaning |
| --- | --- | --- |
| 0 | u16 | bytes per row (whole 16-pixel words) |
| 2 | u16 | rows |
| 4, 6 | s16 | offset x, y (hypothesis: applied by the callers; small, often negative) |
| 8, 10 | u16 | position on the artist's sheet (hypothesis: unused by the game; equal x for sequences) |
| 12 | u8 | bitplanes cleared under the mask (pass 1: B = 0) |
| 13 | u8 | bitplanes set under the mask (pass 2: B = $FFFF) |
| 14–19 | u8[] | stored planes: each byte = the bitplanes that plane is copied into; 0 ends |
| 20 | | stored planes, bytes per row × rows each |

Mask (`$110CE2`): OR of the stored planes (up to five), the plane itself when
there is one; no stored planes → A pointer 0 → solid rectangle (BLTCON0
`$3CA`/`$1CA` instead of `$BCA`). Pixels outside the mask are untouched.
A plane mask can name several bitplanes (`0E` = planes 1–3 together), which
is how the game saves memory. Preview sheets (`wings_of_fury_assets export`)
show every library correctly in the day palette.

| Library | Shapes | Contents (from the pictures) |
| --- | --- | --- |
| world.shp | 140 | carrier, islands, huts, guns, soldiers, flags, explosions, smoke, splashes, waves, palms, HUD labels, "THE END" |
| hellcat.shp | 116 | the player's F6F in all attitudes, landing gear, bombs |
| japplane.shp | 27 | enemy planes |
| torpedo.shp | 100 | bombs, rockets, torpedoes in all angles |
| 8thscale.shp | 122 | small (radar-scale?) versions |
| battleship/cruiseship/destroyer/japcarrier.shp | 25/25/28/12 | enemy ships, wrecks, flags |
| dash.shp / nightdash.shp | 223 each | cockpit dashboard parts: 3-D view frames, gauges, digits, radar planes |
| selectrank.shp | 8 | rank-selection bars |

### Pictures and palettes (verified: decode with the shared ILBM reader)

| File | Picture |
| --- | --- |
| wingstitle | 320×200, 5 planes: title |
| broderbund | 320×200, 4 planes: publisher logo |
| creditscreen | 320×200, 5 planes: credits |
| selectrank | 320×200, 5 planes: rank selection |
| rank.iff | 640×200, 3 planes |
| iff-dash, nightdash | 640×37, 4 planes: cockpit dashboard (day/night) |
| hiscore.iff | 320×75, 5 planes |
| hiscoreslab | 640×200, 4 planes |

Palettes: `wingspalette`, `night.p`, `nightocean.p` are a bare `CMAP` chunk
(the length field holds garbage; the game reads 32 colours); `palette`
(packed), `ocean.p`, `ocean.palette` are tiny ILBMs whose CMAP is the point.
The game's list at `$106462`: wingspalette, night.p, iff-dash, nightdash,
ocean.palette, nightocean.p, dash.shp, nightdash.shp (day/night pairs).

### Maps (scenery read; specials open)

Loader `$102B1A` (table of 15 names at `$115460`): `Read(4)` → file size S,
`Read(4)` → C, carrier/start x `-$5C6C(A4)` = C×4 − 8 (verified: a.map gives
7032, the player's x `-$5F84(A4)` and camera `-$41A2(A4)` on the deck in the
reference machine), then the remaining bytes into a buffer.

One word per 8-pixel column (scenery drawer `$1037BC`; it starts at
camera − $120 and steps `-$60C8(A4)` = 8, or 1 for the 1/8-scale view):

| Bits | Meaning |
| --- | --- |
| 15 | draw this column's shape (one column of a shape's span carries it) |
| 11–13 | ×4: drop below the scenery line `-$5C5E(A4)` = $97 (night view: fixed $97) |
| 2–10 | index into the scenery table |
| 0–1 | surface (`$104EE8` reads it under the plane): 2 islands, 1 carrier; 1 makes the shape follow the ship's bob (`$104EAC`) |

Shape drawn at (x − 8 − offset_x, line + drop − offset_y), i.e. the header
offsets are handles (`$10389E`).

Scenery table (`$10535A`, 1,112-byte buffers `-$40AA`/`-$407C` for normal
and 1/8 scale): 184 world.shp names (`$113E38`), 24 cruiseship (`$113C24`),
27 destroyer (`$113BB4`), 13 japcarrier (`$113B7C`), 25 battleship
(`$113B14`); name lists end with a zero long. Missing names (barf, bumb,
LIVE, lpn*, expl, fgyx, zpn*) give a null pointer and the column draws
nothing; `bumb` (index 9) is the most common island column (ground with no
shape; the island's sand strip is drawn elsewhere — open).

Ship columns (`$104EAC` → `$104A4E`): a surface-1 column c is moved only
when a ship covers byte offset (c + 1) × 2; ships are searched in the order
−$5B9E, −$5B80, −$5B62, −$5B44 (each only while its flag −$5C84, −$5C87,
−$5C83, −$5C86 is set), then the US carrier −$5B26; each holds its range
(byte offsets) at +0/+2 and its bob at +$1A. The column is drawn bob +
vertical scroll (−$41A8) lower. Other columns ignore the vertical scroll.

Map specials (`$103B1C`; value = word >> 2 without bit 13, so the drop bits
must be 0; checked after the column's own shape, draw bit or not):
- $22 (`towr`, carrier tower) → `$103D10`: the waving US flag. When the frame
  counter −$5C36 is odd, the step −$5C71 goes 3, 2, 1, 0, 3 …; the frame is
  entry $B4 + steps[step] (`flg3`–`flg6`; steps = 03 02 01 00 at `$114BA1`),
  drawn at the column's x − 8, ground line + vertical scroll + carrier bob
  (D6 at the call is the vertical scroll, loaded at `$1037FE`). The radar dish
  is part of these frames.
- $9F (`fgyx`, two columns on the carrier) → `$103BCE`: the deck crew. Once
  per frame (flag −$3BA6) the poses −$59A8/−$59A4 step by one toward targets
  from tables −$59A2/−$598A chosen by the deck phase −$5C9F; every $9F column
  draws entries $8F + pose a and $8F + 8 + pose b at the same place as the
  flag.
- $113 → `$103B52`: an island's enemy flag. The island is the one the camera
  is over (count of right edges ≤ camera / 4 in −$5BC6); its two words in
  −$5BAE: both zero → `POST` (entry $B3), else entry $B0 + a counter that
  each $113 column steps 2, 1, 0 (−$5C7E).
- 5 (`hutb`, drawn) → `$104E18`: not read yet (no such column on maps a, d, k).

Islands (`$1040E8`, after the waves): each runs from a drawn `bchl` (index 1)
column to the next drawn `bchr` (index 2) column; the load scan `$102DE8`
counts them at the right ends (−$5C7A) and keeps the columns' byte offsets
(−$5BCE left, −$5BC6 right; map a: 8 and 986). Per island: `bchl` and
`bchr` through `$105174` (world x = offset × 4, skipped outside screen x
−$80…$1C0, at the ground line), then the sand: a colour-17 rectangle from
the left to the right edge, rows $97–$9B, through the clipped fill `$111010`.

Waves (`$103E6C`, after the objects): entry $4E + frame (`wava`…`wavl`), five
copies $60 apart from x = $3F − (camera & $3F) − $50, at $97 + $A minus the
handle. The frame −$5C70 steps 11 → 0 every second frame (`$10105E`). The
copper shows rows 151 and below in another palette (ocean colours; not
traced yet).

Sky (`$103784`): colour 1 from row 0 to min(ground line + 1, $A1).

Every drawer places a world x at screen x = world x − camera + 160 (− the
handle); the scenery loop's own arithmetic gives the same.

#### Backdrop check against the reference machine (2026-10-27)

`game/scenery.cpp` (`draw_backdrop`) draws sky, scenery columns with their
specials, waves and island sand from a `BackdropState` (camera, wave frame,
vertical scroll, active ships, flag step, crew poses, enemy-flag counter,
island flags).

The play view is double-buffered (BitMaps −$3806 and −$38B2; −$3EB0 is the
one being drawn and alternates every frame). `harness/viewref.py grab X`
pins the camera at the frame start (`$100F96`) for a few frames, then reads
the buffer that is not the current target (the frame just finished; reading
the target gave state one frame off: 3 of 3 flag steps matched with
"other", 0 of 3 with "target") and writes the state file.
`tools/viewcheck.sh` draws the native backdrop from that state
(`wings_of_fury_assets view`) and compares colour indices over the whole
320 × 162 view (`tools/viewcmp.py`).

Results (pixels differing out of 51,840):
- Map a, 22 positions 300–7400: 314 everywhere (the player's plane), plus
  the parked planes, a bow splash and the elevator platform near the carrier
  middle (6700–7032; objects). Enemy flag (column 301), US flag, radar and
  deck crew equal.
- Map d (rank 1), 22 positions 300–14100: 314 everywhere except parked enemy
  planes on an island (3500) and the carrier objects (7300, 7456). Both enemy
  flags (columns 263, 1505) equal.
- Map k (loaded in rank 0's slot by changing the name string, research only),
  20 positions 300–20000 with the weapon menu still up (its box, 6,624 px,
  is in every grab): beyond the box, only parked Japanese planes on the
  battleship and carrier (6200–6700) differ; the ships' scenery matches.
Not covered: a ship bob other than 0 (all grabs had bob 0; the vertical
scroll was 2–4), index 5, night maps, the 1/8-scale view, the copper
palette split.

### Sounds (raw)

`sounds/*`: 8-bit signed samples, no header (boom, screech, scream, splash,
machinegun, metal.clang.1, grind.1, engine). Loaded on demand (`$1033E0`…);
played through `songplay` `_PlaySfx`. Periods/rates: open.

### Music: songplay + wofsongs (symbols only so far)

`songplay` exports `_PlaySong`, `_StopSong`, `_FadeSong`, `_PlaySfx`,
`_StopSfx`, `_SfxStat`, `_PauseMusic`, `_RestartMusic`, `_AdjustSfx`,
`_GetSongStat`, `_ReadInstruments`, `_OpenTimerInt`/`_CloseTimerInt`
(CIA timer interrupt `SongInt`), with per-track state `Track0Info`…
`Track3Info`. `wofsongs` holds `songs`, `song1`…`song4`, `voices1`,
`voice0`…`voice6` and the instrument samples (`BassDrum3Data`,
`SnareDrum1Data`, `RoomBrassData`, `SyntheBassData`, `OmleadData`,
`Mech1Data`). Format: open.

### Texts in `wings`

- Rank names: pointer list `$115864` (Midshipman … Captain, seven).
- Story: `$1158E0` → 53 zero-terminated lines.
- Manual check: see "The crack".
- Error strings ("Crashed in the main task!!! …", "Problem reading file …").

### Fonts (open)

`newarmyfont`: starts u16 12 (height), u8 ' ', u8 '~', then per-character
widths; layout of the glyph data not read yet. `fonts/army/12`: a standard
diskfont load file (TextFont).

### FFP (mathffp.library) — verified against the ROM

The flight code calls mathffp.library through wrappers ($111C9C SPAdd,
$111CB0 SPNeg, $111CC4 SPFix, $111CD8 SPDiv, $111CE2 SPFlt, $111CEC SPMul;
LVOs −$42, −$3C, −$1E, −$54, −$24, −$4E; D0 op D1, SPDiv = D0 / D1).
Format: mantissa bits 31–8 (normalized), sign bit 7, exponent bits 6–0
excess 64. Fitted model (game/ffp.cpp, tools/ffp.py):
- SPMul, SPDiv: exact result rounded to nearest (half up) at 24 bits.
- SPAdd: smaller operand aligned with 8 guard bits (ignored if 24+ places
  smaller); add/subtract; round at the larger operand's scale (+$80, clear
  the guard byte); then normalize (carry: shift right, dropping the bit;
  cancellation: shift left).
- SPFlt rounds to nearest; SPFix truncates toward zero.
Oracle: harness/ffpref.py runs batches in the game's vblank server (patched
first instruction $101758 → JSR to a loop written into the never-called
manual check $10F41A, data in its table $116622). Checked with
`wings_of_fury_replay ffp`: add/mul 1,169/1,169, div 658/658 (exponents
$30–$50), flt 1,905/2,000 (the 95 misses are one batch corrupted while the
code still sat in a blitted buffer), fix 800/800 (Python model; that file was
lost to a crash). Overflow, underflow and exponent-0 operands are not
modelled (outside the game's range). The machine still crashes after ~1,000
oracle calls (cause unknown).

## Game structure and rules

Addresses: `wings` in the $100000 layout (see "Disk, crack, programs"); globals as −$n(A4)
(A4 = $11AF52). Read from the code and checked in the reference machine
where noted.

### Frame (main loop $100228…)

1. `$100F88` frame start: WaitTOF, camera −$41A2 = player x −$5F84, then
   the snapshot copies: altitude −$419E ← −$5F86, facing −$4084 ← −$5F72,
   −$4086 ← −$5BF0, −$4082 ← −$419C, vertical scroll −$41A8 ← −$5C50,
   scale −$60C8 ← −$5C4E (8 normal, 1 = 1/8-scale view).
2. `$10024E`: −$60CA = 3 in the 1/8 view; origin −$60CE = camera − 160
   (scaled); ground line −$60CC = $97, + altitude − $83 above $83 ($4B8 in
   the 1/8 view); −$5C5E the same.
3. `$103772` the whole play-view draw ("Maps" under Data formats for the backdrop):
   sky, scenery columns, then `$103ABC` parked planes, `$10409C` elevator,
   `$1003A6` the player, `$100DA6`, `$103D78`, `$103DE8`, `$104C3E`,
   `$103A18`, `$10391E` (objects), `$103E6C` waves, `$1040E8` island sand.
4. Game logic ($100EE0, $103EEE, $1052F8, $1006BE, $100344, $10557C,
   $1010C2 …), then the dashboard ($10417E) and the buffer swap.
The view is double-buffered (−$3806/−$38B2, target −$3EB0).
Sea motion: every −$5C16 frames −$6894 steps 0..7 and the vertical scroll
−$5C50 = table[−$63BA + step] + 1 ($1013D0).

### Player picture ($1003A6, normal view)

- Not drawn while the elevator state −$5C6A is 1.
- Position: x 160, y = ground line − altitude; clip bottom $A1, or at the
  deck while the elevator moves (−$5C6A ≠ 0: $A2 − carrier +$E + bob +
  scroll + 3; `$10526E`, clip set by `$11129C`, bottom/right exclusive,
  left/right rounded to 16).
- Order: −$5BE0 part (deck state −$5C5A = 2 and −$5C91 set, unless upset),
  −$5BE4 part at y + −$5CB4 − 5 (shown in modes 0/7 or with −$5562;
  −$5CB4 steps 1 toward −$5BF4 per frame), the plane −$5F82, the −$5BE0
  part again when upset (−$4086 ≠ 0), clip reset (`$10524A`), then the
  propeller.
- Propeller (`$10063A`): engine −$5C94 on, not upset: counter −$5C76 steps
  0..3, step = `$114B5C`[counter] (00 01 00 02); 1 → Hellcat entry attitude
  (−$5A6C) − 5 + $43 (fa*), 2 → + $57 (fb*), +10 facing left; XOR-drawn
  (`$110E24`, minterm $6A, A = $FFFF).
- Shapes are mirrored in place for the other facing (`$105B58`: rows
  reversed through the bit-reverse table `$11560A`, handle x → width − 1 −
  x); the shape's +8 word records the facing it is in ($10ABDE).
- Carrier deck (drawn before the player): `$103ABC` parked planes (entry $26
  `Cpln`, min(−$5CA2, 9) − 1 of them from carrier x − $C8 every −$1E, at
  ground + scroll + bob; only while the carrier is afloat), `$10409C` the
  elevator (entry $25 `elev` at the carrier x, y = ground − deck + bob +
  lift −$5C68 + scroll, clipped like the plane; not while −$5C6A = 1).

Checked (game/player_view.cpp vs grabs, colour indices over the whole view,
0 pixels differing): on the elevator (states 1 and 2, rising), on deck,
taxiing, falling from altitude 130/70/30 (attitudes hc05–hc09 mirrored),
propeller frames, ground line moved (altitude 160), and after a real
take-off 16 consecutive flight grabs (climbing and descending, attitudes
hc04–hc07, pitch 1..−2). Not yet: the −$5BE0/−$5BE4 parts actually shown,
upset, facing right, the game-over objects.

### Input

- Fire `$11046A` (via −$7D40/`$11044C`): D0 = 1 pressed.
- Flight stick `$10520E`: JOY1DAT → table −$5958: bits 1 up, 2 down, 4 left,
  8 right; up/down swapped (EOR 3) when −$5B08 is set (inverted plane).
- `$10C9E0`: a fire press shorter than 10 ticks (−$436C) sets bit $20, a
  longer one bit $10; `$10CA32` builds the command word (stick bits 0–3,
  fire bits) every 4th vblank in the interrupt `$101754` (or from the
  recording in playback mode) and queues it (−$3CA8, up to 6); the game pops
  one per frame into −$42BC (`$101714`).
- Keys (`$10CCF6`): r, s (sound), f, g (save, on deck), l (load), Esc
  (pause), b, c; a typed sequence o, l, i … steps −$115E6C to 5, a debug mode
  with extra keys (i/k, f, p, space, q, m, r, digits 4/6/8 …, d) — not read
  further.
- Command word bits (`$10CA32`): stick up → 1, down → 2, left → 8,
  right → 4 (stick bits 4 and 8 swapped), fire $10/$20.
- −$5C94: drawn as "propeller" ($10063A); set by a long press when −$5BF0 = 0
  and fuel −$50EA > 0 ($10B5B4), cleared otherwise. It stayed 0 during the
  whole take-off and flight in the machine, so it is not simply "engine
  running" (meaning open). A long press on deck sends the plane back down
  the elevator (mode $B, `$101374`).

### Take-off (the flight model is compiled C on the player record)

The player record is DAT $117D40 → $114FCC (−$5F86 = A4 − $5F86): +0 y,
+2 x, +4 shape, +8 shape name, +$C mode, +$14 facing (… read so far).
- `$10C4E8` on deck: no stick → acceleration step DAT $117D3E = 0, speed
  DAT $115368 − 8; stick toward the facing (command bit 8 = left gives −1)
  → step + 1 (at most 8) per update and speed + step; the other way → slow
  down and, once stopped, turn round (DAT $115362 counts to 6). Speed is
  kept within 0..$578; above 600 DAT $1159F0 = 1 (lift), unless command bit
  1 (up) is held.
- `$10C5F4`: when x leaves the deck (DAT $115350 = carrier first × 4 + $10,
  DAT $115352 = last × 4 − $10; map a: 6608..7344) the mode becomes 0
  (airborne); otherwise the plane rides the deck height.
- In the machine: injected input builds speed too slowly for the deck (508
  at the bow → ditched, mode 4); with the speed poked to $578 the plane
  leaves the bow and flies (harness `viewref.py takeoff`).
- In flight, holding up+left climbed; pushing right slowed and dropped the
  nose. Turning round in the air not tried successfully yet.

### 1/8-scale view

Switched on high up (scale word −$5C4E → −$60C8 = 1; seen at altitude
~188). −$60CA = 3, ground −$60CC = $4B8, origin −$60CE = camera − $500.
- Backdrop: sky to row $A1; scenery columns one pixel each from (camera &
  ~7 − $900) / 8, 8thscale.shp entries of the same names on the line $97,
  ship drop >> 3, no specials (`$103B2C`); sea band colour 10 rows $97–$A2
  (`$103E7A`); per island the small `bchl`/`bchr` at x = (world − origin) >> 3
  and one sand row at $97.
- Player (`$100468`): 8thscale entry from the frame-start copies: upset
  −$4086, pitch −$4082, facing −$4084 (rules in game/player_view.hpp), at
  x 160, y = (ground − altitude) >> 3; no parts, no propeller.
- Parked planes: 8thscale `Cpln`, x and y scaled; no elevator.
Checked: 4 grabs in the 1/8 view (altitude 198, flying left beyond the map
start): equal except a formation of tiny enemy planes (47–54 px, drawn by a
later object drawer). Not seen: the upset entries, islands/ships under the
1/8 view.


### Logic ticks

`$1014F0` runs the tick `$101386` once per queued command word (the vblank
server `$101754` queues one every 4th vblank, at most 6), so the logic runs at
12.5 Hz on PAL regardless of the draw rate. A tick: flight timers (fuel
every −$3E96 (=$1C) ticks and, once damaged, oil every $50 — only while
flying), sea motion (every −$5C16 (=20)+1 ticks the step −$6894 advances,
−$5C50 = table $114B98[step] + 1), pop the command (−$42BC), every
−$42C0 ticks the deck menu `$1012B0` and the elevator `$101460` (which moves
only when the draw counter −$5C36 changed: tied to the draw rate), then the
flight `$10C660`, then other objects ($10E7D6, $102132, $10B682, …).

### Flight model (game/flight.cpp)

Rebuilt from `$10C660` and its callees (compiled C on the player record
$114FCC, DAT $117D40; the carrier record $1152E6, DAT $117D48: x, elevator
state, lift). Modes: 0 flying, 1 on deck, 4 falling, 6 sinking, 7 caught by
a wire, 8 burning, $B going below. Per mode:
- 1: `$10C4E8` throttle/brake/turn on deck, `$10BDBA` roll (speed+50)/100,
  `$10C5F4` off the deck end → mode 0, `$10B92E` wires (4, every $38 from
  carrier + $46; hook x ± $18; speed ≥ 600 without lift → mode 7),
  `$10BCCE` officer signal.
- 0: oil < $60 or fuel < 0 → falling; altitude < −6 → sinking; else
  `$10BFF4` stick (climb command −4500..3000 in 1/100 degrees, pitch rate
  600, throttle step ≤ 8, speed ≤ $578, turning round via `$10AB80`, a step
  every second call, facing flips at step $E), `$10BDFA` motion in FFP
  (smoothed climb, sine table $115AC8, turn factors $115A60; vx =
  fix((f·speed·cos + 50)/100), vy = fix(speed·sin/100); below speed 1000 the
  nose drops unless up is held; ceiling $44C bounces), `$10BA80` touching
  down (on the deck facing left while pulling up → landed; else bounce or
  crash), signal by altitude.
- 7: roll-out, speed −$6E per tick, then mode 1.
- After every mode: `$10B45A` wheels down within 1280 of the carrier,
  `$10C378`/`$10ABDE` shapes (pitch step (climb+5000)/500; turn tables while
  turning; deck table; entry 9 with lift; wheels `wh*`), scale = altitude >
  $BA ? 1/8 : full.
Height helpers: `$10AAEA` clearance (by attitude, + gear), `$105710`
obstacle heights (scenery classes, ship parts: ship +$E − +$14 − sea, carrier
also − lift).

Lockstep (`harness/flightrec.py` records every tick, replacing the popped
command at `$1013FE` with a script; `wings_of_fury_replay flight`; regression
`tools/flightcheck.sh`): recA (take-off, climb into the 1/8 view, level: 309
ticks), recB (the full planned flight: take-off, 1/8 view, turn, back past
the carrier, second turn, glide, wire catch, stop: 911 ticks), rec_west (high
flight at the ceiling far west, two turns: 911 ticks) — all equal, strict.
rec1 (no "up" after take-off → ditching) is equal until the crash.

Crash and carrier (rebuilt 2026-10-27):
- `$10AFBA` crash (mode 4): the surface under the plane (land / water /
  ship deck by the map's surface bits) decides; settling at wheel height
  (land, water) or on the deck (`$105710`), or, when the column five back is
  open water, pushed back and stopped; a turn unwinds to its nearer end (±2
  per tick, facing flips at $E); sliding (speed/100 per tick, −$55); falling
  (vertical speed −1 per tick down to −10, climb −600 per tick down to
  −3100); at rest: sinking (mode 6) over water or low (< $14) on a ship, else
  burning (mode 8); oil and +$1A cleared. Calling `$10ABDE` with step 0 sets
  only the attitude (the name it returns is dropped).
- Mode 6: down 1 every 3rd tick, climb −250 per tick; mode 8: shape `hcr5` /
  `hcrf` (immediates at $10C88C), at wheel height or on the ship; both run
  the wreck timer `$10AF7C` (150 ticks, or fire after 30) → `$1035CE`.
- `$1035CE`/`$1035D8`: plane lost: black pause (−$3BAC = 20 waits), one spare
  fewer, x = carrier, turn 0, facing left; no spares (or carrier not ready)
  → game over (the mission loop ends: the recorder sees no more ticks).
- `$103684` + `$10B7EC`: a new plane below deck: input delay 15 ticks, weapon
  bombs, elevator 1 at lift $20, menu up, fresh flag, record reset (fuel $C0,
  oil $80, +$10 = beam-position random 6..9).
- `$1012B0` weapon menu: fire → mode $B, elevator 2 (rising); up/down step
  the weapon with a 2-tick repeat; loads $114B4D (15, 30, 1).
- `$101460` elevator: one pixel per tick with a draw: rising to lift 0 →
  up; lowering to $20 → new plane (rearm). `$10B5B0` on deck: fire while
  stopped on the elevator spot (`$10B4DE`) lowers it (mode $B, elevator 3).
- `$10C676`: while the menu is up the flight only zeroes throttle and speed.
- Drawing: `$100344` menu box (entry $4D `selt` + $4A + weapon at (250, 60),
  opaque through `$110B0C` with no mask, −$7CE0).
- +$10 goes down with hits from enemy planes (seen at ticks 342–358 of
  recD with no fire command): not compared until enemies exist.

Lockstep for these: recC (ditch, give up, plane lost, new plane, weapon
menu, elevator up, rearm below deck, up again: 451 ticks), recD (island
crash, burning, next plane: 512), recE (deck bounce facing right: 465), recF
(crash onto the carrier while turning, burning on deck, wreck timeout: 941),
recG (two more planes lost → mission over: 600) — all equal, strict; the
earlier four still equal (`tools/flightcheck.sh`: 5,374 ticks then; see "Effects" for the current set).

The original draws about once per tick (draw counter steps per tick in
recB: 1 × 745, 2 × 127, 0 × 39), so the module's one draw per tick keeps the
elevator close to the original's pace.

Not rebuilt: guns/bombs/rockets/torpedoes (`$10B5B0` air part), the enemy
timer `$10BC02`, the formation check `$10AA6E` (turns assume no enemies),
damage, the menu's keyboard keys, the debriefing. Shape mirroring: the game mirrors pitch
and deck shapes in place on first use per facing; the port treats them as
mirrored when facing left (true when a shape is first shown facing left, as
the plane always starts so; open).

### Game module (game/module.cpp)

Free flight on map a: plane on the deck, commands every 4th vblank from the
stick, fire timed as `$10C9E0`, one draw per tick (the original draws as
fast as it can, about once per tick: draw-rate-tied parts — waves, gear
lowering — run at the tick rate here), dashboard picture below. Starts
below deck with the weapon menu; crashes, the wreck, the black pause, the
next plane and game over (the module stops) as the original.

### Effects, carrier animations, wires (rebuilt 2026-10-27, game/effects)

Frame order after the view ($100228 step 4): `$100EE0` smoke, `$1052F8`
splashes, `$1006BE` blasts — on top of everything. All three step as they
draw (once per drawn frame); `$105174` places an entry: screen x = world x −
(camera − 160), y = ground line −$60CC + $B − y (both ÷ 8 in the 1/8 view,
from the 1/8 table), skipped outside x −$80..$1C0.

- Blasts: the object table −$6350 (15 × $2A; also the player's bombs,
  rockets, torpedoes: +$22 kind, not rebuilt). `$100820`(map address,
  height, kind): first slot with +$20 = 0; x = column × 8, y = $C + height,
  +$20 = 8, frame +$21 = 1, +$1F = 2 for kind 0 (fire; then the sample
  `$102324`) else 0 (water). `$100FBA` copies +$20 → +$C at the frame
  start; the drawer (+$C = 8) steps +$21; at 8 the slot is freed; drawn
  world.shp entry $59 + frame (`exp0`..`exp5`) or $65 + frame
  (`spl0`..`spl5`).
- Splashes: list at *−$40CE, 20 × 4 bytes (the loops run 20 times; the 21st
  word belongs to something else): `$1052AC`(x): first entry with +2 = 0;
  +3 = surface under x (`$1050C8`: 0 off the map); +2 = 4 over an island
  (dust: `ric0` at y 9) else 6 (water: entry $67 + count at y $D: `spl6` is
  missing, so a splash shows `spl5`..`spl1`); drawn while counting down.
- Smoke: list at *−$40A6, 40 × $14 (`$105460`): 16.16 x, y (y at least 16),
  drift +8/+$C = 1.0 + beam-random fraction each way (right and up), size
  +$10 (6 = `smk5` .. 1 = `smk0`, entry $60 + size; 1/8 view: $61 or $62),
  shrinks every 7 frames. `$10CAE0`(x, y): x + beam random & 7, y + 6.
  Clipped at the deck line ($84 + scroll + bob) while the elevator moves.
- Random sources: `$111E24` is the C library's rand (seed −$45E8 = 1 at
  load, LCG ×$41C64E6D + $3039, bits 16–30) and only the wreck blasts call
  it; `$1103BE` (others, smoke) mixes in the beam position (hardware).
  The rebuild draws both from `std::mt19937` (only the uses' masks and
  ranges follow the original); the replay checks random-dependent values
  by those rules and takes the recorded draws.

Where the flight code starts them:
- `$10BA80` touch-down crash: blast two columns ahead (x + facing × 16) at
  the altitude, unless over water or low (< $14) on a ship (then `$1046C6`
  island damage).
- `$10AFBA` crash: water, at wheel height → splash at x + facing × 8;
  land, down → blast in the column at altitude − 1; ship, pushed back →
  blast at the altitude (and sound `$10CAB4`); while sliding: water →
  splash again, land/ship → `$10B304` a blast whose address argument is the
  plane's x:altitude words (a bug: it lands at a fixed map x that depends
  on where the map sits in memory, 2040 in the reference machine; rebuilt
  later, see below), land → `$1046C6`, `$101A84` (island damage, soldiers; not rebuilt);
  falling on land and reaching the ground → blast at altitude − 1.
- `$10AED8` (modes 6 and 8, before the wreck timer): while wreck time <
  $4B and ≥ the next time −$5556: next = time + (rand & $C), blast in the
  column of x + (rand & $F) − 8, height altitude − clearance (sinking: 0,
  water). −$5556 and −$5558 are cleared when the wreck comes to rest.
- `$10C870` burning (mode 8): smoke at (x + facing × 8, altitude + $B).

Carrier specials (drawn with the scenery columns, only when on screen, not
in the 1/8 view; see "Map specials" under Data formats):
- Deck crew `$103BCE`, once per frame: the phase is the officer's signal
  −$5C9F; on a change the targets −$59AA/−$59A6 take the second words of
  tables $1155B0/$1155C8 (per signal: two words, the swing's ends); each
  pose steps 1 toward its target; at the target it turns round to the
  entry's other end (signal 2: pose a waits until pose b is level). Pose a
  → `fgy0`+, pose b → `fgy8`+. `$1036A8` (new plane) clears the phase;
  `$103684` sets the signal to 3.
- Tower flag `$103D10`: odd frames step −$5C71 3, 2, 1, 0.

Arresting wires: `$10B92E` (on deck, speed ≥ 600, no lift −$5562): the
hook (x + $18 facing left, x − $18 facing right) within ±8 of a wire
(carrier + $46 + i × $38, i = 0..3) → mode 7, signal 4, −$5560 = −1,
−$42C4 = the wire's x. Lift on deck is speed > 600 without "up", so
rolling right with "up" held keeps the tail down and the hook catches
(recK: taxi left, turn round, right + up: caught on wire 3 at speed 636,
stopped in 6 ticks). Mode 7: speed − $6E per tick to 0, then on deck.
`$1005C0` draws the cable while in mode 7: line in colour 3 from (wire −
camera + $A0, y + 6) to (160 ± 16 behind, y + 5) with `$111318` (Cohen–
Sutherland onto the clip window, integer proportion, then the blitter's
line mode).

Checks: `wings_of_fury_replay flight` compares, each tick, the blasts and
splashes the tick starts (from the recorded tables; WOFREC3 recordings add
the seed $11696A and the splash list), and steps the crew and tower flag
over the frames between ticks (draw-counter difference) when their columns
are on screen: recH (ditch), recI (island crash), recK (wire brake) and the
nine earlier recordings equal (12 recordings, 6,594 ticks); off screen
nothing moves. The cable equals grabs (poked mode 7, cable lengths 2, 60
and 30 px, both facings: 0 pixels differ). Blast, splash and smoke
pictures compared by eye with grabs during an island crash.

Deck bounces (checked 2026-10-27, recL): the touch-down `$10BA80` has no
angle or impact limit. On the deck (x within the deck, no turn, altitude at
most 4 below deck level after the tick, carrier afloat) anything but facing
left with up held bounces: vertical speed and pitch negated, 6 px up. It
crashes only when the test fails: sinking more than 4 px below the deck in
one tick, touching during a turn, or past the deck's ends. Facing right, "up"
alone pushes the nose DOWN (`$10BFF4`: only facing left does up pull up), so
holding it after a low pass facing right bounces the plane along the deck
every ~8 ticks (recL: 7 bounces, then off the deck's end into the sea);
the original does exactly the same, tick for tick.

Map end: the map buffer is the file's size (`$102BB0`: end −$69D2 = start
+ size − 2); `$10C982` turns an address at or past it into end − 2, two
columns past the column data (the extra 8 bytes are uninitialised memory;
the port reads them as 0). Seen in recL: the wreck sinking beyond the map's
end places its blasts at x 7624 (column 953 of map a's 951).

### Dashboard (rebuilt 2026-10-27, game/dashboard)

640 × 37 hires, 4 planes: `iff-dash` with dash.shp parts by the game's
117-name list (−$6E36, `$106556`: `misl bomb torp olof olon … ndl1.. gaug
zero`). The display is double-buffered as frame structs −$3606/−$35F8
(+0: offset of the buffer's dashboard cache −$30CE + 0/$14; **(+6) + $2C
its dashboard RastPort, (+6) + $2C the play view's). `$10EE16` (each
frame) redraws a part only when its cached value changed:
- oil gauge: target (oil − $60) × 2 & ~3 (+$18 with the engine flag on
  deck/flying), needle −$30A6 ±4 a frame; face `gaug` at ($D0,$12),
  light ($CA,$A) `olof`/`olon` blinking (counter −$30A0: 9 frames lit, 2
  dark) while oil < $74 and flying/on deck/caught; needle `ndl1` + n/4.
- fuel gauge: fuel/2 (≤ $58) & ~3, flickering 0/4 by the beam position
  when empty; light below $41 (counter −$309E, period 8); at ($1A8/$1AF).
- weapon icon (entry = weapon) opaque at (16, 11).
- load digits: two rolling strips (`bnum`, entry 6) at x $2A/$42, y $1C +
  roll, clip rows $13..$1B; units roll +1 a frame (wrap $50 → 1), tens
  step while the units pass 1..8 (odometer); targets $50 − digit × 8,
  $FF: $64 (spins on). `$10EDBC` jumps them (weapon menu, new plane).
- spare planes: one strip at x $7D, ±1 a frame toward $59 − spares × 8;
  `$10EDEA` jumps it at mission start only (a lost plane rolls down).
- score "%07ld" (−$4936) digits `wnum` (entry 7) opaque at x $200 + 14i,
  shifted by −$6402[digit], clip rows $B..$11; kills (−$5C7F, ≤ 99) two
  digits at $1FC/$20A y $15 and up to two rows of seven bars (`zero`).
- (`$10F21A`: an arrow toward an attacking enemy plane: not rebuilt.)
Forward view `$10417E`: window x $102..$17C, rows 7..$1F (clip x $100..
$18F); sky colour 2, sea 1 from the horizon (altitude asr 4 + $18); not in
the 1/8 view beyond the fills. `$104206`: eleven bands (columns ahead
−$6948 backwards: 160,130,105,…,6), farthest first one row lower each; per
band the trees (6..8) and the other scenery found (the first tower $22/$F6
sticks), an island line (colour 5) when island columns are in it, frames
by `$1045A6` (band tables −$6946/−$693A), ships via `$104A52`; the
surface under the plane is skipped when it is a ship with +$12 0/$1770.
`$104430`: over the carrier, the deck ahead from the cockpit (tables
−$692F/−$68B9/−$68C9/−$68A9). `$1041B4`: the sight `targ` lower the lower
the plane. Checked (tools/dashcheck.sh, harness/dashgrab.py, camera
pinned, pokes): 21 grabs exact (camera positions, both facings,
altitudes 37..250, warning lights lit and dark, needles moving) except the
enemy-plane marker (42 px). The stepping (needles, blink counters, rolling
strips) equal per drawn frame in all recordings.

### Weapons (rebuilt 2026-10-27, game/flight + game/effects)

Fire in the air (`$10B5B0`): a tap releases the load (`$10107C` →
`$1007F2`; not mid-turn 6..$10), a long press fires the guns (−$5C94 while
not turning; −$50EA = $600 never counts down). Release (first free slot of
−$6350, load − 1 unless $FF): from the last tick's end (`$101274`: speed_y,
speed_x, x as 16.16, altitude copy + $B): bombs; torpedoes (+$1F = facing
byte); rockets: angle −$5BFA/2, thrust sin/cos × 2 (16.16), ignition 4/8/12
ticks (beam random), picture by pitch. Per tick (`$100A72`): rockets slip
back/down while unlit, then thrust (and aim, `$10099A`: toward a gun post
`pila` the line of fire passes over: atan2 `$105CA6`, speed/100; map a has
none); bombs/torpedoes: drag (a tenth of dx's whole part) and gravity
−$5CAE ($6000); the next height is written only as a whole part (the
fraction of y stays). Lost $500 from the camera ($1680 in 1/8). Ground:
water/island $C, ship obstacle + $B: rockets and bombs become blasts (+$1F
= surface; water: splash frames), sound boom/splash; torpedoes run if they
enter water at dy ≥ −5 (dx ±4.3, $C8 ticks, a wake splash each tick,
blowing up on what is not water), else blow up. Bombs tumble a step every
second drawn frame. Blasts set off torpedoes within 16 (`$101AE2`).
Strafing (`$1019BC`): guns + diving + below $A0 + not pulling up: one
splash/dust where the line of fire meets the ground (`$101A46`, slope
table −$6892, the frame's camera). Pictures: torpedo.shp by the 138-name
list $1142F4 (bom0.. $40, rc/rk $4C, ro $74 unlit, tor2/tor7); drawn from
the frame-start copies (+$C kind, +8/+$A place). In the 1/8 view every
weapon is entry 9 of the 1/8 table: 8thscale.shp's `bumb`, a dot (world.shp
has no `bumb`; 8thscale.shp has); blasts, splashes and smoke use that table
too. Checked against a grab of a high bomb run: the same trail of dots. The muzzle flash (what
these notes earlier called the propeller): hellcat `fa*`/`fb*` XOR while the
guns fire.
Not rebuilt: hits on islands (`$1046DC`: dugouts, huts, gun posts, score,
island capture), soldiers (`$101A84`), barrage balloons (−$5D04,
`$101126`), the enemy ships' guns as rocket targets (`$1011A6`).
Lockstep: recM (bombs, strafing; an anti-aircraft hit taken as recorded),
recN (rockets: ignition delays taken as recorded), recO (torpedoes): all
fields of every weapon in flight equal each tick.

### Sound (rebuilt 2026-10-27)

Samples `sounds/*` (`$103368`): boom, splash, machinegun, Engine, Grind.1,
metal.clang.1, screech, scream. The game drives Paula directly (channel
records −$3190 + $1E n; `$10EA28` start: pointer, length, period, volume,
repeats; `$10EB4C` period/volume, negative values ignored; `$10EAC0`
stop). Requests (−$3C96 + $30 per channel, two of $18 each; `$101F76`):
ch0 guns (machinegun $C8 loop) / engine; ch1 enemy guns / engines (not
rebuilt); ch2 boom ($1F4 once) / splash ($15E once; below deck: $320 vol
$22 loop); ch3 enemy ship guns ($140) / deck sounds (clang `$102354`
$1646 bytes $1C2; screech `$102380` $1A5A $15E; grind while the elevator
moves, $1C2 loop). Driver `$102066` each tick: first active request with a
sample, else the second; restart when the sample differs from the last
started, retune when period:volume changed, stop when none. Events clear
the "last started" so a sound starts again. `$102132`: engine level eases
toward the target (+1 / −2, unsigned: from 1 it runs negative), pitch
toward climb/128 + base + altitude/16 (+$14 / −$A). Targets: flight
$40/$14F loud or $31/$181; deck pushing forward $40/$14F, else idle
$28/$328 (also caught); falling $19/$3C0; sinking, wreck, going below 0;
elevator top idle (level too), new plane off. `$101F4E` silences all at
both elevator ends. Distance volume `$102306`. The misplaced crash blast
(`$10B304`) is rebuilt (x = (altitude − $FE04) × 4: the map's address low
word in the reference machine) because its quiet boom cuts the real one.
Checked: the engine values and every channel record (sample by length,
start period, volume) equal each tick in all 16 recordings (enemy sounds
excluded).

Sound in the app (2026-10-27): the host passes the last frame's buffer to
`audio()`; the module clears it before Paula adds the channels (the first
build did not: the sound piled up into a constant drone).

### Islands and mission rules (rebuilt 2026-10-27, game/islands)

Map scan `$102DE8` (drawn columns): index 4 `huta` → hut (−$5C77,
list −$5B06, $10 bytes, `$103216`: +0 byte offset (long), +4/+6 x = offset
× 4, +8 = 5 (soldiers to release), +9 island); index 3 `dugo` → dugout
(−$5C78, list −$5B02, `$1031C8`: +4 x − $2C, +6 x + $10, dugout range
per island −$5BBE first/last offset); index $F `pila` → pillbox (−$5C79,
list −$5AFA, $E bytes, `$103236`: +0 offset, +6 island, +4 0); each hut
and dugout adds 5 to its island's soldiers (−$5BAE + 4 i, +0), each
pillbox 1 to +2; index 2 `bchr` (island's right end) → −$5C7A islands;
an island with targets adds to −$5C7C/−$5C7B (islands to destroy). Enemy
ship columns ($10D, $F2, $E4, $CC) activate ship slots (−$5C87, −$5C86,
−$5C84, −$5C83) and −$5C8D (ships to sink); $21 the carrier. Soldiers:
list −$5AFE, 8 bytes, (huts + dugouts) × 5 slots (−$5C3A).
Messages: "Island has %d soldiers and %d pillboxes." ($115E6E), "All enemy
forces on island have been destroyed . . . Bonus %ld points", "Congratulations!
You have completed your mission!! … return to carrier", "%s sunk . . .
BONUS". Mission loop `$100066..`: frames until −$5C3C (over); with the
weapon menu up (−$5C9A, below deck) and −$5C42 set (mission accomplished,
`$105694`) it clears both, silences the sound (`$101F4E`), clears the
ticker (−$5848 = 0, `$106BBC`: BltClear of the strip's $444 bytes), adds a
spare plane when promoted (−$5CA1) and goes on to the next mission's
briefing (`$108590`) and its first plane (`$1035A8`, `$103684`). After
game over (`$1001C6`) the high-score table and the title follow.
Tick routines: `$101DE4` releases soldiers from dugouts and huts (+$B
timer, +$A count; timer = (−$5C34 >> 4) & $1F, 0 → 3) through `$101E82`
(soldier +0 x, +2 direction byte, +3 step, +5 island, +6 state 1);
`$101BFC` smoke from a damaged plane (every second tick, beam random);
`$101CAE` ships sinking (carrier too: sunk with the plane on deck → game
over); `$101C5E` celebration balloons (−$5CA1).
Dugouts (`$103D78`, each frame): an empty one (+8 = 0) refills after 200
frames (`$104FEE`); a manned one aims its gun at the plane (`$104D50`:
`gun*` by the angle table −$63E2, `gnf*` firing at random; in the 1/8 view
a random `expl`) and `$104F5C` may burst flak near the plane: a smoke puff
and +$10 − 1; at 0 the oil − 1, fuel − (0..3), +$10 = 6..13 (beam random).
Rebuilt (game/islands): shelters (huts, dugouts), soldiers, per-island
counts, the dugouts' guns and flak, hits (`$1046DC` land part, pillboxes'
damage stages too), soldier kills (`$101A8C`), island taken (bonus table,
messages), mission accomplished (`$105694`), wrecked huts' smoke (`$104E18`,
when drawn, both views: +$C puffs, +$E frames, puff at x + $10, y 17), the
hit flash (`$10030C`: colour 0 on odd counts), the damaged plane's smoke.
The draw counter −$5C36 counts 0..99 (`$1037A8`). Recorder WOFREC5 adds
the huts, dugouts, pillboxes, soldiers and map words each tick; recQ (bomb
pass: dugout 3224 then both huts, soldiers running out; 528 ticks) equal
strict and resynced in every island field, the score and the map; a
dugout soldier's way (beam random) and flak hits are taken as recorded.
The guns' sound: `$104F5C` notes each gun within $1C0 of the camera that
may fire (−$3BAA set, −$3BA8 the nearest distance, reset to $2710 at the
view's start `$103772`); the view's end (`$1038EA`) turns that into
channel 3's first request (−$3E9A on, −$3E98 = $40 − min(distance / 8,
$40)), which the tick's sound state (`$1022B4`) copies and which takes
over from the deck's request.
recU (fresh mission; poked at tick 0: island 0's last soldier already
dying, its count 1): the soldier dies (+25), island taken (+450, rank 0
mission 1's row), no islands or ships left → `$105694` appends ". . . Good
work ! …" to the buffer over the first message's last character and
forces the ticker onto it; fire on the elevator spot, the elevator goes
down, the machine's ticks stop (the next briefing). Equal tick for tick
with the message text.

### Enemy planes (rebuilt 2026-10-27, game/enemies)

Compiled C module `$10D18C..$10E8B8`; four records at −$5DD4 ($34 bytes,
fields in enemies.hpp). Each tick after the flight code: `$10E7D6` (per
plane: side `$10D3B4`, order `$10D476`, by state: flying `$10E728`
(smoke when damaged, then by mode: 1 `$10D9C6`, 2 `$10DCCC`, 4 `$10DEA4`,
$10 `$10E17A`), falling `$10E244`, burning `$10E3E8`; speed easing
`$10E64E`; movement `$10D796` except when burning (FFP: x += SPFix(SPMul(
turn_speed[turn], SPFlt(speed / 100 × facing))), height toward the target
by `$11614C`[gap / 20] + rand(2); the turn timer; `$10D562` turning round)
and the picture `$10D35A`). The launch timer is the player record's +$1C
(`$10BC02` at the start of `$10C660`): $546 for each new plane
(`$10B89A`), 500 after a drop; firing raises it to $2EE; counts only while
the carrier is afloat with torpedo hits left; at 0 (and |$7FFF − x| >
$1A00) `$10E4D0`(formation, x ∓ $1800, 0, ±1): from the side away from the
carrier, over the deck by beam bit 15. A formation plane is a torpedo
bomber (mode 4, height $32, one at a time); lone fighters (mode 1, −$5E28
counts them) come only from `$101510`/`$101622` (enemy carriers and
airfields). Speeds $384..$A28 (`$115EA6`).
Torpedo run: within 1000 short of the deck the target height is $14; at
$14 and level it drops the torpedo into the object slot −$5A6A (outside
the 15-slot table, flown, set off and drawn by the same code: dx = speed ×
$28F, y = (height + $F) << 16, its facing byte), then climbs away (mode
$10, target speed max / 2, height $3C) until $A28 from the player. A
running torpedo in a ship's columns (`$104986`, weapon 2 and +$1E = $A)
flashes the screen red and takes one of the ship's hits (+$C; the
carrier 4, set at `$103562`); the last sets +$18 = $14 and `$101CAE` sinks
it: a step (+$14) each time +$16 runs out, +$16 = +$18, which shrinks by
one a step. The carrier: while the plane is below deck it comes up (the
step test `cmpi #1,$14` has its flags overwritten by the next `move`: at
every step); at step $21 with the plane on deck it sinks with the ship
(mode 6, spare planes 0, −$5A3C = $64 frames to game over, `$101110`); at
$78 the ship's columns are cleared from the map. A splash after a boom
(`$100D92`) takes the boom's volume (D0) as its x: silent.
The player's guns (`$10B682`, after the sound state): a plane ahead the
same way (side 3) within $A0 and $14 while the player's climb is 0 is hit
(+6); every rand(4) + 6 hits a puff and oil − 8; below $60 it is shot down
(+350, state 4, target height −3). Falling: height += vy / 100, vy − 10
(+$22, never reset); at the target it skids: speed − $23 (+ $18 on water
below 300, a height step every second tick), `$101A84`(x, $14), above 500
`$1046C6` (the land/ship hit at its column), a splash ahead; below 100
kills + 1 and on an island it burns (state $10: smoke, then a wreck in
−$5E24, x × facing; the fighter count drops even for a bomber: mode is 0
by then), else it is gone. Sound: channel 1 plays the guns of any firing
plane and the engine of the nearest (|dx| + |dy|, `$1022CE` loudness).
Dashboard: `$1042A0` draws each plane in its forward-view band (the band
range of map offsets, its frame-start column +$2E), centred, at the band's
row − (height / 4 + 4), from −$3BA2 (`$10D1EA`: three sizes of 56,
`zr1*`/`zr2*`/`zr3*`), picture (+$1C swapped facing right) + −$696C[band];
`$10F21A` the alarm `ltar`/`rtar` at ($140, $A) for the first bomber on
its run. Drawing `$100DA6` (after the player): wrecks (`jc1a`/`jc10` at
the ground − 4), planes at the ground − 7 − height, gun flash XORed every
second frame (`fc10`…, japplane.shp list $114520 + $40).
Recordings (WOFREC5 already holds all of it but the skid counter −$4368):
recR (parked; a bomber's whole run, the carrier 4 → 3, the next launch),
recS (island route, at tick 290 a poked bomber ahead with one damage step
left: shot down, skid onto the island, burning, wreck), recT (below deck,
the carrier's last hit poked: the plane comes up, sinks with the ship,
game over), and rec_west (an older recording with a bomber in it). Beam
draws taken as recorded: launch side and hit timer, rand(2) height steps
(±1), rand(7) × 10 + $23/$19 heights, rand(4) + 6 hit timers.

### Briefing and the mission change (rebuilt 2026-10-27, game/briefing)

`$108590` (before each mission, after the map load `$102ADC`): `$106B04`
opens a hires 640 × $93 screen with 3 planes, palette −$56D2 (8 colours);
the world.shp shape `rank` drawn without its handle at ($140 − bytes per
row × 4, $65 − rows / 2); then Move + `$108570` (strlen, `$105A98`: the
text in `newarmyfont` through `$105956`, each character width + 1, 10 + 1
without a picture, ORed; BltBitMapRastPort to the pen position) at
($128, $3D) the rank name (−$56EE pointers), ($154, $49) the mission
("%d"), ($154, $77) islands −$5C7C, ($154, $83) ships −$5C8E; text in
colour 7 ($AAA). Fade in, then up to $F0 frames or fire (`$11044C`); the
key `r` returns 1 (the main loop goes back to the title). Exact against a
screen of the original (rank 0, mission 1; every colour one to one).
Mission change: `$10012A` (menu up and −$5C42) → `$101234`, a spare plane
if promoted (−$5CA1), `$1011FC` (night: bonus row > 6 picks it at random),
`$10EDAA`, `$102ADC` (counts cleared; map index = Σ missions per rank
below (−$5AB6: 3 3 2 2 1 1 3) + mission − 1, names −$5AF2 a..o; the scan
sets the carrier: afloat, deck $21, 4 hits, +$12 0, +$16 $14), briefing,
`$1035A8` (enemy planes, fighters, wrecks, celebration cleared) and
`$103684` (the plane). Score, spare planes and kills stay ($103562 sets
them only for a new game). Maps by features: a, b islands with huts and
dugouts only; c adds pillboxes, d on index $B and $114 places, f/g $CC
ships, h on $E4.. ships, j/k.. $10C.. (enemy carriers/airfields).
Mission 2 (b.map): islands at 1176..3944 and 11608..13936, the carrier at
6312; recV (recQ's bomb pass), recW/recX (each island's last soldier
poked dying: bonuses 500 and 700, then mission accomplished, mission 3)
equal tick for tick. In the original a lost plane coming up below deck
ends the accomplished mission too (seen: spares 3 → 2, c.map loaded).

### Message ticker (rebuilt 2026-10-27, game/ticker)

−$5848 points at the next character; the vertical-blank server
(`$10184E`) shifts the 13-row × $54-byte strip one pixel left while −$5A36
runs ($2A0 after each character) and, once −$5A1E has run out (width + 1
blanks; 10 for a zero-width character), copies the next `newarmyfont`
glyph (word height, first/last character, widths, glyph words; `$102794`)
to byte $50; at the NUL the pointer is cleared (a new message may start:
`$10555A`; `$105694` forces one). The strip is a one-plane viewport at
line $C9, one line below the dashboard; the copper list (`$1087BA`) sets
colour 1 per line from $1158E8 ($777 $999 $BBB $DDD $FFF $DDD $BBB $999
$777 $555). Exact against a grab of the original 208 blanks into ". . .
Good work ! …" (`wings_of_fury_replay ticker`).


### Pillboxes (rebuilt 2026-10-27, game/islands)

`$103DE8` (each frame, after the dugouts' guns): a standing pillbox (list
−$5AFA, $E bytes: +0 offset, +6 island, +8 destroyed, +$A puffs left, +$C
frames to the next) aims and fires its gun as a dugout does (`$104D50`,
drawn $16 up) with the same flak (`$104F5C`); a destroyed one puffs smoke
at x + 8, y 17 (size 5), 50 puffs, each `$32 − left` frames after the last.
Rockets alone hit them (`$104834`: the part hit breaks, the columns are
rewritten; the first hit destroys it: +200, the island's count − 1). The
rockets' homing (`$10115C`) searches this same list (standing ones). recY
(c.map, rockets in a shallow dive: both pillboxes of island 0 down)
matches tick for tick including the smoke counts.

### Front end (rebuilt 2026-10-27, game/frontend)

Fades: `$107084` sixteen frames, step k (0..15) of each colour from the
current to the target by `$106FF6` (per gun (Δ × k / 15) masked to the gun,
so falling guns borrow; k = 15: the target); `$1073B0` to black; `$1071F2`
two viewports at once.
Opening `$108022`: song 2; the story `$107E80` (640 × 200, one plane; the
53 lines from `$1158E0` in `newarmyfont`, a line spread to 615 pixels
(`$105956`: the extra over the gaps, the first ones a pixel more) unless
the next line is empty; a 210-row ring buffer scrolled a row every 4
frames, a new line drawn every 14 rows at view row $C4; the copper list
(`$107D4E`) ramps colour 1 from black over 16 lines at the top (from line
5) and bottom (to line $C9), each change visible a line later; ends 210
rows after the last line or on fire, then the ramp shrinks 16 → 1, two
frames a step). Song 1; Broderbund (palette −$56B2 first: the logo alone,
60 frames; then its own: "presents", 120), the title (300), the credits
(600), each faded in and out; fire during a wait skips to the last fade.
Rank selection `$108262`: song 4; `selectrank` with the bar `rnk0`..`rnk7`
(selectrank.shp) XORed at its sheet position (64, 91 + 10 i); up/down
(keys $4C/$4D or the stick) move it with wrap-around, a held stick repeats
after 9 frames (`$108228`); fire or Return chooses; after 1,800 frames the
game starts (the demo file `wofdemo` it would play is not on the disk).
The eighth entry, RETURN FROM R&R, opens the saved-game screen `$108B96`
(six slots; not rebuilt). Then the music fades out (`$102470`) and the game
starts at the rank's first mission (`$103562`: score 0, 3 spare planes,
carrier 4 hits).
Game over: the last plane lost shows `gmov` (world.shp, THE END) at ($A0,
$51) each frame (−$5C9C, also from the wreck timer `$10AF80` while the
spare planes are 1 or 0); `$1001C6` stops the sound and fades the view out;
`$109856`: song 0; if the score beats the tenth (`$109472`) the name screen
(320 × 200, four planes, palette −$55A2: topaz 8 texts in colour 8 at
($34, $53) and ($5B, $5C), a box in colour 2, the line editor `$106086`: 16
characters at ($52, $66) in colour 6 on 0, the cursor a complemented 8 × 8
cell; Return, up, down or fire end it), the entry replaces the tenth, the
table is bubble-sorted and written to `highscore` ($168 bytes: 10 × score,
rank, name); the table (`$106D7A`: `hiscore.iff` 320 × 75 lowres over the
slab 640 × 145 hires from line $4C; `$10967E` draws each row three times:
x − 1 and x + 1 in colour 0, then colour 15: number, `%-6ld` score at +$28,
`%-12s` rank at +$96, the name at +300, rows $C apart from y 6), 1,800
frames or fire, then rank selection. The key `r` in a mission ends the game
(−$5A3E): the fade, then rank selection directly.
Each screen matches a capture of the original (story at one scroll
position exactly, including the copper's greys; the pictures, rank
selection, name screen and table colour for colour one to one).

### Music (rebuilt 2026-10-27, game/music)

`songplay` (CODE $A88 + DATA $3DC, symbols kept) entry `segentry` takes a
command in D0: 0 `_OpenTimerInt` (CIA-B timer A, `SongInt`; the audio
interrupts `SongIntHandler`), 1 `_ReadInstruments` (D1 song, D2 `songs`:
the song's voice table at +$10, entries from +$14 to the first empty one:
their 8SVX file (+$A) scanned for VHDR (+0) and BODY (+4), +8 = $53 /
octaves), 2 `_PlaySong` (when idle), 3 stop, 4 close, 5 state, 6 fade (D1
speed), 7.. sound effects (unused by the game). A song: four track lists
(6-byte entries: pattern, transpose) and the voice table. `SongInt`
(`$1802A6`): state 1 sets the tracks up (volume $20) and plays; 2 plays; 4
fades (every speed + 1 ticks each track's volume − 1; none left: stop); per
track (`$1804F4`): while a note lasts its arpeggio (voice +$22, offsets
+$24..) or slide (+$1E: the period swings by +$16 between bounds +$12/+$14
every +$18 ticks) and its release (the channel off unless held (+$38) or
tied); then bytes: a note (< $D9; bit 7 tie; + transpose; duration index
into `$180CE4` pairs (length, release before the end)), $D9 next entry,
$DA end, $DB restart, $DC voice, $DD/$DE the CIA timer's high and low byte
(tempo), $DF volume, $E0 hold. The period (`$1807EA`): the octave from the
note / (+8) (the highest octave's samples are the shortest), the table
`$180BC4` (by note − $1B; lower notes read the longs in front of it) over
samples per cycle << octave; the sample past the higher octaves' parts.
The audio interrupt switches a playing one-shot to its loop (or stops the
channel after it). The game (`$1023DC`): a playing song is faded (speed 2)
and waited for, then the next read and played; `$102470` fades and stops.
Songs: 2 the story, 1 the pictures, 4 rank selection, 0 the hall of fame.
`harness/musicrec.py` records the four track structs at each `SongInt`;
`wings_of_fury_replay music <content> 2,1,4 <rec>` runs the rebuild with the
time between interrupts played on the channels and the game's calls (fade,
next song) where the records show them, and compares every field: songs 2,
1 and 4 with the fades between them (from a boot to the game: 7,483 ticks)
equal; 5 track ticks differ only in a note's stage (the audio interrupt's
timing: the original's handler reads INTREQ once and clears all four
channels' requests, so an interrupt arriving while it serves another
channel is lost and a one-shot plays twice; seen at tick 5209). Song 0 (the
hall of fame) not recorded.

### Maps d–j: airfields, enemy ships, night (rebuilt 2026-10-27, game/enemies, game/ships)

Which map holds what (`wings_of_fury_replay maps`): d and e an airfield;
f and g a cruise ship; h a destroyer and an airfield (night possible); i a
destroyer, a cruise ship and an airfield; j no islands, a destroyer and a
battleship. Row = −$7B9F[rank·4 + mission] (0..14 for a..o); night only for
rows > 6 (`$1011FC`, the fourth of four beam draws, bit 15): night.p over
the view above row 151, nightocean.p below, nightdash.shp and the nightdash
picture.

Airfields (`cama` columns between a $114 and a $115 marker, `$102C84`; list
−$5D04, 4 × $14; parked planes and limit per row from −$7B0A): while the
player is within $1E0 of the field and fewer lone fighters than the limit
are up, one plane taxis out every $64 ticks (cooldown −$3CB6), speeds up to
$38 and leaves the strip as a lone fighter (`$10143E`/`$101622`); drawn by
`$103A18` (jp21/jp22, 1/8 view 160/163).

Enemy ships (the first marker of each kind, `$102DE8`; slots destroyer,
battleship, cruise ship, Japanese carrier): deck, value, torpedo hits, guns
(`$10350E`: x from the gun tables −$59FC/−$59EC/−$59B2/−$59D0 + first × 4;
the cruise ship's 2nd and 3rd lifted 5) and parked planes (`$10252C`:
count and limit by row, template low/high/items). Each ship found adds to
−$5C8D. The battleship's row-9 count is 6 but its template holds 5 planes:
the sixth is read from the next table (x first×4 + 320, altitude −32768,
facing 29) and, launched like the others, climbs from −32768 (seen in the
original, recAE; kept).

Ship guns (`$104C3E`, each frame while flying, afloat ships with hits):
first a shell splash (`$104EFC`: distance < beam & $1FF, plane at most $C8
high, (beam & $F) − 6 < 0: a splash within $20 of the camera, by the beam;
it sets off torpedoes within $A, `$101AE2`); then the angle picture
(`$104DB8`), +7 by beam bit 15, the flak (`$104F5C`). Knocked-out guns
smoke. Hits (`$104986`): bombs nothing; a running torpedo flashes red and
takes a hit (the last sets the sink delay $14); anything else flashes white
and knocks out the first standing gun within $10 (+200, red). Sinking
(`$101CAE`): at step $A the value is scored and `$105640` formats ". . . %s
sunk . . . BONUS %ld points . . ." (name by value from −$7456); the last
target calls `$105694`, which appends the next-mission text over the
message's last character (the islands' path does the same). At step $78
the ship's columns are cleared. Lone fighters from ships (`$101510`): within
[low, high] and under the limit, one plane every $64 ticks.

The new plane's set-up `$103684` first calls `$103756`: the kind byte (+$20)
of all 15 objects and of the enemy torpedo (−$5A6A) is cleared — a torpedo
or blast in flight vanishes when the next plane comes up (recAG).

The debugging switch −$408C (word): in the key handler's debug mode
(−$50E6 = 5) the key code $64 (`$10D168`; ASCII `d` is a hypothesis; $34
and $36 there move −$5CAE by ∓$100) sets the oil to $80, the mode to 0 and
toggles it (`not.w` at `$10D184`). With it on the fighters' hits
(`tst.w` at `$10DE36`) and the flak (`tst.w` at `$104F96`) do not hurt. Recordings made with it on are marked `!` in `tools/flightcheck.sh`
(`WOF_INVULNERABLE`).

Verification: `wings_of_fury_replay setup` compares the set-up's scans
(islands, huts, dugouts, pillboxes, soldiers, ships, guns, parked planes,
airfields, counts) with each map's first recorded tick: a–j all equal
(43–173 values each). Lockstep recordings (all equal): recAA/recAB (f: a
torpedo set off by a shell splash; the cruise ship sunk), recAC–recAH (j,
night: destroyer sunk, the battleship's first hit and its sixth plane, the
battleship sunk with its hits poked to 1, a destroyer gun knocked out by
rockets, the last ship sunk and the mission accomplished), recAI–recAL (e,
g, h night, i night: take-off and a flight over the islands). Shell splashes
and the torpedoes they set off are checked by rule (within $20 of the
camera; the splash's frame is not recorded) and taken as recorded; a wreck
blast's boom volume (its x is random) likewise. The night dashboard is
exact against a grab (dashcheck on i); the night view's colours, sampled
from the original's screen against its displayed bitplanes, are night.p
(13 indices) above row 151 and nightocean.p below.

Reaching a map in the reference machine: a new game, then `flightpoke.py`
sets rank/mission to the mission before and takes island 0 of map a
(soldier slot 19 dying, −$5BAE = 1); spare planes set to 1 (byte −$5CA2) and
a ditch end a game. Losing the plane after a mission is accomplished does
not skip the ceremony's map change by itself (not followed further).

### Weapons while turning; the input (checked 2026-10-28)

A tap releases the load during a turn round too, except while the plane is
upside down: `$10B5B0` skips `$10107C` when the turn step −$5BF0 is 6..$10.
In ticks (the step after the tick) the bombs leave at steps 1–6 and 18–25,
none at 7–17 (recAM, map a: a turn with fire tapped every second tick,
equal tick for tick). The command word comes from `$10C9CA` (each vertical
blank: a press released within 10 blanks sets the tap flag −$3202, one held
10 or longer the held flag −$3204) and `$10CA32` (every fourth blank: tap →
bit 5, clearing both flags; held → bit 4; stick bits 0, 1, with 2 and 3
swapped); the port's sampling is the same. So circling over a target drops
bombs at the start and end of each turn round in the original as in the
port.

### Cheats (pause menu, 2026-10-28)

Two are the original's own debug mode keys (debug mode −$50E6 = 5, see
below):
- Bullet proof: the switch −$408C (key code $64, `$10D184`): the fighters'
  hits (`$10DE36`) and the flak (`$104F96`) do no harm.
- Infinite ammo: the load $FF (key code $6D, `$10D08A`; off restores the
  weapon's load −$6405 and calls `$10EDBC`): no load is used and the
  weapon menu keeps it; the dashboard's load strips roll on (strip target
  $64), exact against a grab of the original with the load poked to $FF.
- Enemy duds (not the original's): the enemy bombers' torpedo vanishes
  without a blast or a hit where it reaches the carrier or land.
- Infinite lives (not the original's; first in the menu): a lost plane
  uses no spare (`$1035CE`), and the last plane's wreck shows no THE END
  (`$10AF80`). Checked with `simulate` and `WOF_LIVES=1`.
- Infinite fuel (not the original's; second): the fuel is kept from
  dropping over a tick (burning `$101386`, flak, fighters' hits); a new
  plane still fills up. Checked with `play` (`WOF_OPTIONS=2=1`: the gauge
  full after 1,500 ticks in the air).
The module applies them to the world before each tick. Checked natively:
`wings_of_fury_replay play` with `WOF_OPTIONS=1=1,2=1,3=1` (bombs keep
falling with the strips rolling), `simulate` with `WOF_DUDS=1` (the same
flight: carrier hits 4 → 2 without, 4 with).

Harness note: `run.sh` passes its own `--state_dir` (work/ref/fsuae-state):
FS-UAE keeps floppy write overlays by the disk's file name, and another
session's harness also boots a `boot.adf`; with the shared overlay the
machine no longer booted `wings`.

### The key handler and the debug mode (`$10CCF6`, read 2026-10-28)

Keys are ASCII (raw codes in −$409F; qualifiers −$40A2, bit 3 Ctrl).
With Ctrl: `r` ends the game (fade `$1073E6`, rank selection), `s` sound
on/off (−$5B07), `f` toggles −$5B08 (`$10520E`, the stick read: with it up
and down change places, `eori #3` when either is held), `g` save
(on deck only) and `l` load (`$108B96`, not rebuilt), `b` an `illegal`
instruction (a crash/debugger trap), `c` deletes the file `highscore`
(DOS DeleteFile), `v` shows "Version %d.%d" (−$321C/−$321A) in the ticker.
Without Ctrl: Esc pauses (−$5AA8). Typing `c o l i n` steps −$50E6 0→5
(any wrong key of the sequence resets it): the debug mode. Its keys:
- `d`: oil $80, mode 0 (a falling plane flies again), toggles −$408C
  (fighters' hits and flak harmless).
- `m`: the load $FF (unlimited) or back to the weapon's load.
- `c`: the next weapon (rockets → bombs → torpedo) in the air.
- `f`: fuel $80. `p`: one more spare plane.
- `i` / `k`: the pitch rate −$50E8 ($258, how fast the climb changes;
  FlightTables::pitch_rate) ± 50.
- `8` / `2`: the falling weapons' gravity −$5CAE ± $1000; `4` / `6`: ∓ $100.
- `r`: every object in the table and the enemy torpedo gone (`$103756`).
- `q`: −$407E and −$5C3C set (the main loop `$1001EA` then leaves:
  quitting the game — hypothesis).
- Space: "Chip memory is %ld, Fast memory is %ld, largest is %ld, Total is
  %ld." (exec AvailMem) in the ticker; Help (raw $5F): "Island has %d
  soldiers and %d pillboxes." for the island under the camera; F10 (raw
  $59): the ticker cleared.
All of it is rebuilt (2026-10-28, below), except Ctrl-b (the trap), Space
(free memory), Help and F10.

### Maps k–o, the ending, saved games and the keys (2026-10-28)

Maps k–o hold nothing new but the Japanese carrier (scenery $F1–$F6; m and
o). Its parked planes take off along the deck (`$101510`, first half): when
its turn comes (in range, under the limit, cooldown 0) the last parked
plane gets flag 1, x = first·4 + $17C, altitude $21, the cooldown $64; each
tick −$3CB4 eases toward 128 (d1 += 8 − d1/16) and the plane moves d1/16
left; past the ship's start it becomes a lone fighter. recAP (m) equal tick
for tick incl. the rolling plane and −$3CB4; recAS: the carrier sunk
(". . . Carrier sunk . . . BONUS 6000 points . . ."). k–o set-ups equal
(171–306 values each). In the 1/8 view a gun fires on a beam-random bit, so
the guns' sound request is taken as recorded there (replay). The dashboard
load strips wrap with unsigned compares (`$10F07C`): a $FF load's tens
strip starts below 0 and wraps to 1 at once (seen with the load $FF left in
the reference machine; recAN–recAS run with it).

The ending: there is none. After rank 6's third mission (o) `$105694`
gives mission 1, rank 7 → 6 (and a promotion: −$5CA1, a spare plane), so
the game goes on with m, n, o (checked in the original: rank 6 mission 3
accomplished → rank 6 mission 1, promotion set). The "victory ceremony" the
message promises is the promotion's fireworks (`$10557C` drawing, `$101C5E`
tick, list *−$4098, 20 × $12): in the normal view, a spark that is done
starts $74 left of the carrier (−$5C6C) at height $38, colour 1..3 → 0..2
(scenery entries 12–14), and rises 1..3 pixels a tick up and to the right
until height $AA. dx and dy come from two beam reads a few instructions
apart, nearly equal: every spark climbs at 45° (one stream on the screen;
the port draws one speed for both). Compared with a screen of the original
after a promotion (the island trick with rank 6 mission 3 poked): the same
stream from behind the island, the same colours (`wings_of_fury_replay
sparks`). The text ". . . How about a nice game of thermonuclear war? . .
." (A4 − $74FB, after the sunk message) is never shown: nothing in the
loaded program refers to it or into it (absolute or A4-relative).

Saved games (`$108B96`, 1 save / 0 load): a 320 × 200 screen, all in pen 15
($28F masked) on black: "Save"/"Load" at ($55, $13) and the texts of
−$5656 ("Game" (127, 19), "Exit Game" (63, 193), "Cancel" (222, 194)), the
boxes of −$5636 (six slots (43, 59 + 16i)–(282, 69 + 16i), Exit Game
(43, 183)–(147, 199), Cancel (205, 183)–(285, 199)), palette −$55EE, faded
in and out over 16 frames. `$108A06` finds the files `wof.<name>` (first
six; each name ≤ 27 characters) and `$108958` draws them at ($2D, $3D +
16i + baseline). Loading: the choice is a complemented box ($2C, y − 1)–
($2C + $ED, y + 7), up/down (stick or cursor keys) skip empty slots and
wrap over the eight places, Return/Enter/fire chooses; nothing to load
starts on Cancel. Saving: a slot under the choice is the line editor
(`$106086`, 28 characters at $2C: a pixel left of the found names), whose
up/down move on and Return chooses. ':' and '/' become spaces (`$106592`);
"Saving game..."/"Loading game..." in pen 1 at (10, 10); a renamed slot's
old file is deleted (the first one, `$109180`). Exit Game ends the program
(`$106E96`). The original writes its variables as they lie (`$105EC2`); the
port's file is `game/saved_game` text (the mission, map columns, islands,
ships, airfields, enemy planes, the plane), beside `highscore`. Load Game
and Save Game screens equal to the original's screens pixel for pixel
(after Ctrl-g / Ctrl-l there, with a slot named alike). Ctrl-g only with
the plane on deck (−$5F7A = 1); RETURN FROM R&R loads.

The keys in the port (`$10CCF6`; the host passes Ctrl+letters as
GameInput::ctrl_keys, without its own Ctrl-Q/S/L): Ctrl-r ends the game
(at a briefing, `$108712`: back to rank selection), Ctrl-g saves, Ctrl-v
says "Version 1.0" (−$321C/−$321A), Ctrl-c deletes `highscore`, Ctrl-f
swaps up and down; the weapon menu also takes Return/Enter as fire
(`$1012C8`); typing `colin` opens the debug mode with d, m, c, f, p, i, k,
8, 2, 4, 6, r, q. Esc (pause, −$5AA8) is Joyport's pause menu; Ctrl-s
(sound, −$5B07) its mute (M); Ctrl-l its quick load (a saved game loads
from RETURN FROM R&R).

The port's FlightWorld points at its own map copy: after a copy (quick
load, a saved game) the pointer is set again (it pointed at the snapshot's
map, so map changes made after a quick load were drawn from the stale
copy).

### Widescreen, smooth motion and the scenery's camera (2026-10-28)

**The scenery's camera is signed.** `$1037BC` takes the first column as
(camera − $120) / 8 by an arithmetic shift; the port had read the camera
(the plane's x, a word) as unsigned, so left of a map's start (x below 0,
e.g. $FF58) the first column came out past the map's end and no column was
drawn: the 1/8 view showed an island's sand without its trees until the
camera crossed 0 (found in a quick save at x −168). Now a signed word, also
for the enemy flag's island lookup (camera / 4). Not compared with the
original left of a map's start (hypothesis: the shift on the word).

**Widescreen** (option). The view is 320 + 2 × margin columns; the margin
follows the window's shape from the original's 320 up to 21:9 of the
picture's 221 lines (at most 96 a side). The game keeps drawing in its own x
(0 .. 319): the canvas's pixels start `margin` to the left
(IndexedCanvas::origin_x) and its clip window reaches the margins. Widened
alike: the scenery columns (column c at x = 8c − camera + $A8; in the 1/8
view c − camera/8 + $A8), the wave copies, the sky and 1/8 sea fills, and
the object drawers' limit −$80 .. $1C0 (now −$80 .. +$A0 beyond the clip
window). What the game decides by distance is wider than any margin: guns
aim within $200 of the plane, flak within $1C0, weapons are dropped at $500
from the camera, fighters start $1800 away. The one per-frame rule tied to
the view, the enemy gun flash step (`$100DA6` counts it as it draws a
firing plane), stays on the original's limits (step_gun_flashes); flashes
are not drawn in the margins. The dashboard, ticker, briefing and the
front-end screens stay their width in the middle, black at the sides.

**Smooth motion** (option). The original moves everything once a tick (every
4th vertical blank) and draws about once a tick, so motion steps at 12.5 Hz
(measured in the port's pictures: the landscape stands, then moves 12+
pixels). With the option the module keeps the state the last tick started
from, and each picture shown is drawn from copies placed between the two
at the moment shown: (vertical blanks since the tick + the shell's fraction
of the next) / 4. Moved: the camera, the plane's height and the sea's motion
(so the scroll and the high plane's vertical scroll), the elevator, the
ships' bob and their decks' planes, enemy planes, airfield take-offs, the
soldiers (when the sprite list kept its length), weapons' frame-start
places, smoke puffs and sparks. Not moved: anything further than 64 pixels
in a tick (a new object in a slot, a reset); a smoke puff unless it shrank
or kept its size and drifted 0..2 pixels up and right (a puff's motion,
$100EE0), a spark unless it rose 0..3 (else a new one in a freed slot: seen
as puffs rushing towards a burning wreck on the deck); blasts, splashes, and every
picture change (attitude, waves, crew). A switch of the 1/8 view draws the
present state. Measured: 1-2 pixels a half vertical blank instead of a
12-pixel jump every fourth. The shell passes the time since the last tick
(GameModule::set_frame_fraction), so at 120 Hz each display frame is placed.
