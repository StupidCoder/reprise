# Fort Apocalypse (C64) — how the original works

What was learned while rebuilding Fort Apocalypse (Synapse Software, 1982;
this release: the U.S. Gold cassette): the tape and its loader, where the
data is and how it is encoded, how the program runs, the rules of play, and
the original's own bugs. The remaining work is in [plan.md](plan.md).

## Sources and evidence

- **Image:** `Fort_Apocalypse.tap` (225,817 bytes, TAP version 1),
  SHA-256 `9e444c4576bac52ba691f0ffe2c0a7efb0f62f3fe2be7cbe78dba08672dda00b`.
- **Method:** everything here comes from this image, its disassembly and
  runs of it on the reference machine (RetroReverse's own C64 emulator core).
- **Prior exposure:** the tape layout and the graphics data were first
  analysed in the user's RetroReverse project, whose README says the model
  that wrote it recognised the game's names from training data. Every claim
  taken from there was re-checked against this image (where they disagree,
  the sections below say so).
- **Addresses:** `$xxxx` is a C64 address in the running program
  (`game.prg` loads at $7000-$B8FF, entry $8600).
- **Paths:** source paths are relative to the Reprise root. Evidence lives
  outside the repository in `.analysis/fort_apocalypse_c64/`:
  - `disasm/code.txt`: the disassembly these notes were read from;
  - `harness/`: `fortref` (the reference machine), `record.sh`, `check.sh`,
    `scenario.sh`, the input scripts and recordings;
  - `tools/`: `assetoracle` (runs the game's setup code), `compare_assets.py`,
    `compare_frames.py`, `sheet.py` (contact sheets);
  - `oracle/`: the memory dumps the asset decoders are checked against.
- **"Joyport"** in the notes below is the native re-implementation (the
  project's name before the move to Reprise); the tools kept their names
  (`build/fort_apocalypse_replay`, `build/fort_apocalypse_assets`).
- **Markers:** [obs] read in the code or seen in a run; [hyp] hypothesis;
  [enh] about an enhancement, not the original.

## The original's bugs (summary)

Each is described where it belongs below.

- **Enemy climb bug** ($A000): with the enemy in the cave below the craft it
  climbs through rock, a row per frame. Kept; the pause menu's "Enemy pilot:
  Fixed" removes it.
- **The fort's explosion kills prisoners still inside**: men left drops
  below 0 ($FE), so level 1 can never be left afterwards.
- **Shots left of the window** ($A33F-$A349) compute their column with an
  unsigned subtraction and test cells 53-63 columns to their right.
- **Decompressor overruns**: every terrain and radar stream's last run spills
  past its buffer (the radar's over the mine tables at $3500-$3532).
- **Enemy in a barrier**: level 0's patrol point 1 ($C3,$16) is next to
  breakable rock, so the enemy can explode there and its shot removes the
  rock for good.
- **Fire latch** $5B starts at 0: holding fire from the title into the game
  fires nothing until fire is released.

## The tape's loading screen

The U.S. Gold loading screen is a display script plus a music stream that
also patches memory. It is not rebuilt (an optional task in the plan): the
music player would be native code to rewrite, its stream is data.

## Tape and import

### Image

- `Fort_Apocalypse.tap`, 225,817 bytes, TAP version 1
- MD5 `bec7409816865f3ad160af9984f127cd`
- SHA-256 `9e444c4576bac52ba691f0ffe2c0a7efb0f62f3fe2be7cbe78dba08672dda00b`
- The same file as `RetroReverse/games/fort-apocalypse-c64/Fort_Apocalypse.tap`.

### Provenance and prior exposure

- The tape layout and loader were first analysed in the user's RetroReverse
  project (Go tools, writeup `fort-apocalypse-c64.md`). That project's README
  says the model that wrote it recognised Fort Apocalypse's character and
  level names from its training data. Treat names and claims taken from that
  writeup as unverified until they are checked against this image.
- For the import, the loader rules were re-checked against the loader's own
  code (below) before being re-implemented in C++.

### Tape structure (observed)

| Part | Encoding | Content |
| --- | --- | --- |
| Header block "FORT", recorded twice | KERNAL | type 1 (BASIC), 16-byte name, 171 spare bytes = loader pulse handler, loaded to $0351-$03F5 |
| Data block, recorded twice | KERNAL | 175 bytes at $0801: `SYS 2061` line and the loader setup at $080D |
| Fast loader stream | Novaload | pages $E0-$E6, $EF, $F0, $EE, $F1, then $70-$B8, then end marker $00 |

All block and record checksums on this image are good.

### Loader rules (observed in its code)

Disassembly of the header's spare bytes ($0351-$03F5) and the setup code
($080D-), made with RetroReverse's own `dis6502`:

- Setup ($0860-$0887): `$FD` (offset in page) = 0, `$AA` (checksum) = $55,
  CIA 1 timer A latch = $03F4, `$DC0D` = $90 (FLAG interrupt only), IRQ
  vector $FFFE = $0351. The bit register `$A9` is **not** initialised.
- Bit ($0354-$035F): `LDA $DC05` (timer A high), restart the timer
  (`$DC0E` = $11: start, force load, continuous), `EOR #$02 / LSR / LSR`:
  the carry is bit 1 of (high byte EOR 2), i.e. 1 when the high byte is 0 or 1.
- Byte ($0360-$0364): `ROR $A9`; a carry of 0 out of the register means a
  complete byte. The state is the operand of the `BCC` at $0364 (written
  through `STA $0365`).
- States: pilot $0368 (wait for $80; no register restart on a mismatch),
  sync $0370 ($AA, else back to pilot), checksum $039D (equal to `$AA`, else
  `$AB` = $80 and the FLAG interrupt is switched off), page $03A5 (page
  number; $F0 rewrites the operand at $03A2 so later page states go to
  $03B1), page after $F0 at $03B1 ($00 ends the load: `$AB` = 0; any other
  value counts down the on-screen "blocks to load" digits at $07D8-$07DA and
  loads normally), data $0388 (store at (`$FB`),Y, also written to SID $D401,
  checksum += byte with carry cleared; after 256 bytes back to checksum).
- After the restart of every state except a pilot mismatch: `$A9` = $7F ($037D).

#### Hypotheses

- Starting the bit register at $7F (the code leaves it undefined) only moves
  where the pilot search begins; the long pilot makes the result independent
  of it.
- Interrupt latency (a few cycles added to the measured length) is not
  modelled. This tape's pulses are 296 and 672 cycles, far from the 500 and
  1012 cycle edges.

### C++ import and its verification (2026-10-23)

Code: `shared/c64/tape_image.*`, `shared/c64/kernal_tape.*`,
`games/fort_apocalypse_c64/import/novaload.*`, `.../tape_import.*`.

- `joyport --import Fort_Apocalypse.tap` installs `boot.prg` ($0801),
  `loader-e000.prg`, `loader-ee00.prg`, `game.prg` ($7000-$B8FF). All four
  are byte-identical (`cmp`) to the RetroReverse Go extractor's
  `FORT.prg`, `FORT-fast-E000.prg`, `FORT-fast-EE00.prg`, `FORT-fast-7000.prg`.
- Program SHA-256 (without load address), used to recognise the release:
  boot `c107f06f…`, $E000 `358dc28c…`, $EE00 `f0c2c9ae…`, game `1856e5d8…`
  (full values in `tape_import.cpp`).
- Variants made from this image (scratch, deleted after the run):
  - one fast loader pulse flipped: rejected, "page $95 has a wrong checksum";
  - one pulse of the first KERNAL data recording flipped: imported from the
    repeat recording;
  - every pulse length jittered by ±8 % (another "recording"): imported, same
    content;
  - truncated file: rejected (length mismatch); a text file: rejected (not TAP).
- Not checked: other real dumps of this release, other releases (disk, other
  publishers), TAP version 0 files.

## Graphics data

Input: `game.prg` as installed by the tape import ($7000-$B8FF, SHA-256 of
the bytes `1856e5d8…`). Code: `games/fort_apocalypse_c64/data/`.

Prior exposure: the RetroReverse writeup and its Go decoders (`fortgfx`)
were read first. Every location and rule below was re-checked in the game's
own code (disassembly with RetroReverse's `dis6502`) and by running that code
(below) before it went into C++. Where they disagree, this file says so.

### Where the data is and which code builds it (observed)

| Data | Location | Built by | Notes |
| --- | --- | --- | --- |
| HUD charset | $B298 → $500F-$52FF | init $899C (three overlapping 256-byte strips = one continuous copy) | $9AB3 then clears $52E0-$53FF (chars $5C-$7F, the radar window), so the file's bytes for $5C-$5F are never shown |
| Playfield charset | $B561 → $5908-$5BFF | init $899C | chars $00-$20 left empty (animated at run time) |
| HUD rows 0-5 | $8603, 240 bytes | $9BC6 (`AND #$7F`) | |
| Helicopter shapes | pointers $86F3 (14 words), 36 bytes each | $B044 at game start: 18 left bytes, then 18 right bytes per shape → VIC blocks 1-14 | |
| Tilt → shape | $A320, 18 bytes (block numbers 1-14) | | |
| Bullet dots | $B0C2, 9 bytes | $B0B0 → $4800 (twice), $4840 (once) | |
| Run codes | terrain $8D2B (23, `CPY #$17`), radar $8D42 (4) | RLE $8CDB | run byte compared before `AND #$7F` |
| Terrain sources | $8D46: $7000, $762B, $7000 | level setup $8F8F → $0503, 40 × 256 | |
| Radar sources | $9116: $8000, $81E9, $8000 | $9038 → $2E00, 40 × 40 | |
| Level colour | $9107: $02, $0E, $02 | $8F19 → $D022 | |

### Run-time changes the setup makes (game logic, not in the decoders)

- $8FC2: `$73` → random $62-$64, `$74` → random $65-$67 (SID noise $D41B,
  `AND #$03`, retried on 0).
- $8FF1: columns 0-39 of every terrain row are copied to columns 215-254.
  **Correction to the RetroReverse writeup**, which calls columns 215-254
  "always $00": that is only true of the compressed stream.
- $905D: radar bytes 0-12 of every line are copied to bytes 27-39 (the radar
  shows bytes 0-26 = 108 pixel pairs, one pair per two terrain columns).
- $9017 (level 2 only): 3 rows × 14 cells from column 121, row 19 are
  cleared.
- $90A4 (levels 0 and 1): up to 8 prisoners: a `$48` cell with `$48` to its
  right and rock `$1F` above, taken when the noise value is in [$0A, $32),
  becomes `$01`; the scan repeats until 8 are placed. Their positions go to
  $3608/$3610, state 2 to $3600, $10 to $3618.

### Stream spills (observed; found here, not in the writeup)

The decompressor checks for the end only after a whole run, and every
stream's last run crosses its buffer end:

| Stream | Spill |
| --- | --- |
| Terrain, levels 0 and 2 | 82 × $61 into $2D03-$2D54 |
| Terrain, level 1 | 255 × $00 into $2D03-$2E01 ($2E00/$2E01 then overwritten by the radar) |
| Radar, all levels | 243 × $00 into $3440-$3532, which includes the mine tables at $3500-$3532 |

The decoders record each spill (`Level::terrain_spill`, `radar_spill`). Open
item: whether the radar spill over $3500-$3532 is observable depends on when
the mine tables are initialised relative to $9038; settle it when porting
the mines.

### Colours seen during play (observed in the emulator run)

$D021 = 0, $D022 = level colour, $D023 = 1, $D016 = $18 (multicolour) in
both screen parts. Colour RAM: playfield rows $0D (green, multicolour) except
row 9 = $08; HUD row 0 and 5 $0B, rows 1-4 $0D, with $0F stretches in row 0
(columns 19-30) and row 3 (columns 0-11, 27-38); row 6 $0C. $AFA0 sets the
initial layout with $0C as the fill; which code later sets $0D is not traced
yet (open item; the previews use the observed values).

### Verification (2026-10-23)

Oracle: `.analysis/fort_apocalypse_c64/tools/assetoracle` (Go, RetroReverse
`mos6502` core, minimal C64 bus with a fixed-LCG SID noise) runs the game's
init from $8927 to its idle loop $8A9F, then calls $B044, $8F8F with
$5C = 0, 1, 2, $9BC6 and $B0B0, and dumps memory; it also plays into mode 2
(gameplay) for 3 s for the colour RAM. Comparison:
`compare_assets.py <fort_apocalypse_assets export> <oracle dumps>`.

Result: all match. Charsets 4096/4096 bytes, sprite blocks 1-14, HUD rows,
bullet pattern, radar of all levels (bytes 27-39 as wrap copies), terrain of
all levels with the run-time changes above checked as rules (722/486/722
random-rock cells, 1600 wrap cells, 8/8/0 prisoners, 42 cleared cells on
level 2), and all spills.

Two bugs found by this comparison and fixed: the playfield charset copy runs
to $5BFF, not $5AFF; the HUD radar characters must be left empty.

Reproduce (from the Reprise root; `build/reprise` was `build/joyport`):

```bash
build/reprise --data /tmp/jp --import Fort_Apocalypse.tap
build/fort_apocalypse_assets /tmp/jp/games/fort_apocalypse_c64 /tmp/jp-export
(cd .analysis/fort_apocalypse_c64/tools/assetoracle && GOWORK=off GOFLAGS=-mod=mod go run . /tmp/jp/games/fort_apocalypse_c64/game.prg ../../oracle)
python3 -I .analysis/fort_apocalypse_c64/tools/compare_assets.py /tmp/jp-export .analysis/fort_apocalypse_c64/oracle
```

Not covered: soft-character animations and the barrier/static patterns
($8907, $891F, $A927, $AF80), the title texts drawn by $9B56, the playfield
background colour per level ($9F), sound data. They come with the game code
that uses them.

## Reference harness and front end

### Reference harness (`harness/`)

`fortref` runs the original on RetroReverse's own C64 emulator
(`tools/platform/c64/owned`: CPU, CIAs, VIC-II with a raster framebuffer,
SID noise, tape), used read-only and compiled into the harness.

- `fortref boot`: power on, `LOAD`, play `Fort_Apocalypse.tap` through the
  KERNAL and Novaload, `RUN`, stop at the game's entry ($8600). The loaded
  $7000-$B8FF matches the installed `game.prg` (FNV `19c227f1`). The
  checkpoint `entry.state` (226 KB) is reached at emulated frame 5826.
- `fortref run`: from the checkpoint, replay an input script
  (`inputs/*.txt`: frame, joystick 2, joystick 1, held keys F1/F3/F5/F7/SPACE)
  and record per frame: game mode $9D, frame counter $15, joystick, picture
  hash, RAM hash (`frames.txt`); zero page + VIC registers (`state.bin`); each
  distinct 320x200 display-window picture once (`pictures.bin`).
- `record.sh` regenerates all runs (deterministic); `check.sh <content>`
  replays them on the native module (`build/fort_apocalypse_replay`) and compares
  every frame (`tools/compare_frames.py`). Frame n of the native module is
  aligned with reference frame 20 + n.
- ROMs: RetroReverse's `site/emulators/firmware/c64`.

Frame boundaries are at raster 0; the picture of frame N shows what the
interrupts set up at raster $F9 of frame N-1, so the native module draws a
frame and then runs its tick.

### Front end (observed and recovered)

Init ($8927) ends in an idle loop at $8A9F; the title is driven entirely by
its interrupt at raster $F9 ($8AA2). The main loop ($8BB1) only starts when
the title is left.

**Title interrupt, per frame:** $D016 = $18, $D011 = $1B, $15 += 1, joystick
($B032: $17 = fire, 0 when pressed), keyboard ($AFED: $18 = 6/5/3 for F7/F5/F3
alone, 7 otherwise; skipped while joystick 2 is active; $19 = column with
SPACE on change). Every 4 frames ($15 & 3 = 0) $D022/$D023 swap (the
flashing frame); every 8 frames $29 += 1. A two-voice tone follows $29
(not rebuilt yet). Exits: $29 = $D3 → $F4 = 0 (weak gravity), $50 = $FF →
attract game; fire or F7 → mode 3 (game); F3/F5 → mode 9 (options).

**Title picture:** HUD charset, colours from $AFA0 ($0C fill, rows 0/5 $0B,
parts of rows 0 and 3 $0F) plus rows 9-11 $0F and row 19 col 1 – row 22 col 0
$0B; frame of $3D/$3C characters (rows 6 and 24, then the side loop over
column 39 of rows 6-23 and column 0 of rows 7-24); five texts via $9B56
(double width: code and code+$20; $00 = one empty cell) at (5,9), (6,12),
(2,14), (6,16), (5,19), (4,21); "1984" as 8 raw codes from $8B7D at $4712.

**Options (mode 9):** $8B97 copies the HUD ($9BC6), clears rows 6-24 and the
radar characters ($9AB3, which also sets $12 = 1), installs the gameplay
interrupt $9BD4 and enters the main loop. Because $12 = 1, $9C2E never
installs the playfield part ($AE19): the whole screen uses the HUD part's
setup ($D011 = $1F, scroll 7, 40 columns) and the shared colours stay as the
title left them. The main loop's key handler ($92E2) acts only when $18
differs from $5A: F7 → game; otherwise $9351: F3 → $FA = ($FA+1) mod 3,
F5 → the selected setting +1 mod 3, then redraw (texts at (13,7), (0,9),
(28,9), names at (0,13/15/17), values at (28, same)); rows 13-17 colour $0D,
the selected option's two rows $0C. No key change for 256 decrements of $42
(only in frames with $15 & 4) → title again ($89C7, which also runs $9AB3).
SPACE ($19 = $EF) pauses (mode 8) until SPACE again, a function key or fire.
Defaults (init): gravity WEAK ($F4 = 0), pilot skill NOVICE ($F6 = 0), robo
pilots SEVEN ($F8 = 2).

**HUD counters ($A92F/$A9D3, every frame outside modes 5 and 6):** score
$91-$93 at row 0 col 19 (6 digits), bonus $97/$98 at row 3 col 29, fuel
$9B/$9C at row 3 col 2 (4 digits); each digit two cells ($10+d, then d or
$0A for 0); leading zeros blank (codes $80, $00) except the last digit. In
mode 2 the bonus counts down every 8 frames and the fuel every 16 (not used
yet).

**Timing details reproduced:** the first options frame still has the
title's $D011 (scroll 3) and no counters; after the options time out, the
title redraw keeps interrupts off past raster $F9, so one interrupt is lost
($15 does not advance).

### Verification (2026-10-23)

Five scripted runs (`check.sh`): idle (title → attract at frame 1707), fire
(game at frame 300), options (F3, F5, F3, F5, F3, F5, F7), options timing
out back to the title, pause. Every frame is identical to the original
except:

- frame 20: the original's init is still drawing the title while the beam
  passes (CPU/beam race; the native module starts with the finished title);
- the frame after the options time out (778, 1115): the title redraw races
  the beam the same way.

The native module stops (with a message) where the original starts a game
or the attract game.

## Game code

### Modes ($9D) and main loop ($8BB1)
- 1 title (IRQ only) / debrief (main loop runs, $42 timeout -> $89C7)
- 2 playing; 3 new game; 4 get ready; 5 life lost; 6 game over/debrief;
  7 transition lock; 8 pause; 9 options; $0A cavern teleport.
- Main loop: if mode != 2 wait for $15 change; if mode 2: $94D2 (SPMs),
  $992A (tanks), $9644 (missiles), $AABA (prisoners), $AD2E (radar window),
  $AC2B, $97B0 (fort destruction), $8C08 (level exits); always: $9892
  (teleport, mode $0A), $8D4C (mode dispatch 3:$8D6B 6:$916E 4:$8EDD
  5:$8E11), $92E2 (keys/pause/options); $50<0 -> $50=0, mode 3 (attract);
  mode 1/9 with $15&4: DEC $42, 0 -> $89C7 title.
- Keys in any mode ($92E2, on change of $18 vs $5A): F7 -> mode 3, $50=3
  (new game); mode 9: $9351 options; otherwise F3/F5 -> options screen ($930F: mode 9, $50=9, $9AB3,
  $FA=0, $9351). SPACE edge -> pause (mode 8) until SPACE/F-key/fire.

### Level exits ($8C08, main loop, mode 2)
- Level 0 ($5C=0): player mode $64=6 (landed), row $6A>=$23, col $69 in
  [$7E,$88]: if men left $EB != 0 -> $AB3B (message); else $5C++, $9B81
  (score bonus), $9BBB (clear row 6), SID off, mode 7, sink animation:
  $8C7D shifts a 6-wide x5 block of map rows down (floor opens) 3 times,
  each with 6 steps of {$9B99 wait 5 frames?, $A62F, $66++} -> mode 4.
- Level 1: fort destroyed ($ED=1), row < 2, col [$7E,$87): men left ->
  $AB3B; else $5C++, $9B81, mode 4.
- Level 2: landed, row < $0D, col [$15,$F4): $9B81, $5C++ (=3), mode 6.

### New game ($8D6B, mode 3)
- $9AB3; $5C=0, score $91-93=0, bonus $97/98=0, $3D=0, fuel $9B/9C=0,
  rank $4F=0, rescued $EC=0; $8F=1, $F0=1, $F2=1; $3E=$80; $ED=2, $EE=2;
  $99=9 (engine: out of fuel -> refilled at life start); $6E=1.
- Tables by option: $F5=$8DFC[$F4] (gravity mask 0F/07/03);
  $F9=$8E01[$F8] (lives 3/5/7) but 2 when $50=0 (attract);
  by $F6: $EF=$8E04 (04/08/10), $FC=$8E07 (0C/19/26), $72=$8DFE
  (03/01/00), $F1=$8E0A (04/03/02), $F3=$8E0B (03/02/01), $90=$8E0E
  (3E/2F/25).
- $8DD3 (also per life?): clear chars $59/$5A ($5AC8, $5AD0), fill one with
  $55 by SID noise sign. -> mode 4.

### Get ready ($8EDD, mode 4)
- $9AB3; "ENTERING" ($9134) at (12,6); level name at (2,8): $5C=1 ->
  $9152 "CRYSTALLINE CAVES", else $913D "VAULTS OF DRACONIS". On level>0
  with $72=3 -> $72=1 (enemy faster).
- $9E/$D022 = $9107[$5C] (02,0E,02); camera $56/$58 = $9110/$9111[2*$5C]
  (05,00 / 6D,00 / 6E,18); sprite $65/$66 = $910A/$910B[2*$5C]
  (6A,64 / 77,64 / 74,AA); $57=0; $59 = 7 (0 on level 2); bank $67=8;
  $A176 (save checkpoint); bonus $97/$98 = $99/$99 (9999 BCD).
- Tanks: homes $31,X/$37,X from $911C/$9122 (levels 0,2) or $9128/$912E
  (level 1); $BE,X=7 (respawn); missiles $A0,X=1; $6E=1.
- SPMs: $3700..$3726 = 1, first $FC+1 slots ($3700..$3700+$FC) = 7.
- $8F8F: map + random rock + wrap copy; level 2 clear; radar + wrap copy;
  prisoners ($90A4, not level 2); -> mode 5 ($9101).

### Life start ($8E11, mode 5)
- $AFA0 with $0D (colour RAM all $0D, HUD layout); $9AB3; lives $F9-- (BCD);
  if it was 0 ($99 after) -> mode 6. $D027=7 (yellow). If $99=9 (no fuel):
  $99=8, fuel=0100. "GET READY" ($8EBE) at (4,8), "PILOTS LEFT" ($8ED0)
  at (5,10); lives digit at $469C via $A9D3 (X=1). $AF80->$5BE0 (chars
  $7C-$7F). $9B99 X=$78 (wait 120 frames?). Restore checkpoint $5D-$63 ->
  camera $56/$58/$57/$59, sprite $65/$66, bank $67. $B297=0; $9AA9; $B044
  (sprites); $B0B0 (bullets); $64=7; mode 2; read latches; $B1D8.

### Debrief ($916E, mode 6)
- $9AB3; rank $4F += rescued/4; +3 if fort destroyed ($ED=1); +1 more if
  $5C=3; += score high byte $93; += $916B[$F4] (FE,FF,00); += ~(2-$F8)
  ... (see code); += $916B[$F6]; clamp 0..15.
- High score $94-$96 updated if score >= it.
- Texts: "HIGH SCORE" (2,0) + digits at $4508; "MISSION" (3,5)
  "ABORTED"/"COMPLETED" ($5C=3) at ($15,5); "YOUR RANK IS" (7,8);
  "CLASS" ($15,$0A) + digit; bird name (3,$0A) from $92B4 table.
- $42=$FF, mode 1, $50=1 (main loop keeps running; title after timeout).

### Main loop steps and the comparison schedule (2026-10-23)
- [obs] A play pass ($8BB1) checks the mode once at its start ($8BBD) and
  then runs eight routines ($8BC3-$8BD8) and the rest ($8BDB: $9892,
  $8D4C, $92E2). Routines that wait ($9B99) return into the pass; a mode
  change during a wait resets the stack (the pass starts over). The loop
  restarts at once after a pass, so a crash that changes the mode mid-pass
  lets the whole play pass finish first.
- Joyport runs a pass as steps with a cursor (`run_pass_step`). For
  comparisons, `fort_apocalypse_replay` reads the original's step ends
  (`FORTREF_WATCH=8BC6,8BC9,8BCC,8BCF,8BD2,8BD5,8BD8,8BDB,8C05,9B99`) and
  runs each step in the frame part (before raster $76, before $F9, after)
  the original finished it. A step that waited or ran longer than 1.5
  frames is started where the previous one ended; its end only releases
  the wait. Passes outside play are left to the game (one per frame).
- [obs] Main-loop pacing in play: 1.70-1.81 frames per pass in five played
  runs (1.84 in the attract game). The game's default is 1/1.75.

### Level build duration ($8F8F-$9101, measured)
- Six builds on three levels: decoding takes a base per level (38.75
  frames for levels 0 and 2, 46.37 for level 1) plus ~3.9 lines per
  random-rock draw (draws of 0 repeat); the prisoner scan ~0.29 lines per
  map cell examined (it stops at the eighth prisoner, so a second pass
  costs more). The build starts at raster $89 of the frame after
  "ENTERING ..." is drawn; the pass ends a line after $9101 and the life
  starts after the next HUD interrupt. Builds of the same level vary by
  ~10 lines (the attract run's ends 2 lines before $F9; the model puts it
  ~10 lines earlier, so that run starts its life a frame early).

### Life start wait ($8E11, measured)
- The drawing before the 120-frame wait takes about a frame. From the
  loop's per-frame wait (after "ENTERING ...") it runs at raster ~$102 and
  the wait counts from the next HUD interrupt; after a crash it runs at
  the end of a play pass wherever the beam is, mostly counting from the
  second interrupt after (Joyport: $79 frames then).

### Findings while comparing (2026-10-23)
- [obs] Landing ($A0FC): when the craft only probes solid cells near it
  ($A134 path, no VIC collision), $73 bit 0 is checked again: no touch, no
  landing; it keeps flying (Joyport had landed it).
- [obs] The fire latch $5B is 0 at power-on: holding fire from the title
  into the game fires no shot until it is released (Joyport started at 1).
- [obs] The crash flash counts $D021 down as a full byte; only the low 4
  bits reach the VIC (the rasteriser now masks colour registers).
- [obs] The playfield colours are written mid-raster $7F; the change lands
  at pixel 84-124 or past the line (92 most common, 77 of 259 frames).
- [obs] The fort's explosion kills prisoners still in it: men left goes
  below 0 ($FE), so level 1 cannot be left afterwards. Faithful in Joyport.
- [obs] The enemy climb bug: with the enemy in the cave below the craft
  (poke 76=38/77=18 while the craft sits on the fuel depot), the original
  and Joyport (original variant) climb from row $18 to $16 frame for
  frame; the fixed variant stays at $18 (its own column is blocked).

### Verified scenarios (harness/scenario.sh, inputs/*.txt)
Each runs the original (fortref) and Joyport with the original's noise and
step schedule; pokes set up the scenario in both (`FORTREF_POKE`,
`REPLAY_POKE`: checkpoint $5D-$63, level $5C, fuel $9B/$9C, men $EB, lives
$F9, fort $ED, enemy $6E/$76/$77). Identical frames (rest: the known
sub-frame races, the line-$7F colour jitter, irq-versus-frame-end
sampling in state comparisons):
- fly1 (flight, shots, scoring, enemy collision, crash flash, restart):
  974/1080.
- refuel (landing on the depot, depot animation, fuel to 2001, take-off):
  1452/1480.
- rescue (cave landing, three prisoners board, two crashes): 870/980.
- exit0 (level 0 pad, bonus, sink sequence, level 1 build): 1159/1280.
- fort (fort explosion, level 1 exit, level 2, landing at base, mission
  completed debrief, title): 3018/3180.
- enemy (the climb bug): 1431/1980 (frames until 1366 identical). The
  enemy is not on screen in this run (held at the map edge, column $30);
  from 1366 on the differences are the depot rows shifted by one and the
  craft's sprite, not the enemy.
- check.sh front-end runs: fire 378/380, options 972/980, options_idle
  978/980, pause 1278/1280, idle 1871/3000 (attract life start, above).
- Not compared yet: the cavern teleport ($9892, mode $0A), tanks' and
  missiles' hits on the craft, losing the last life mid-level (debrief
  "ABORTED"), sound output.

### Findings while adding the enhancements (2026-10-24)
- [obs] Shot columns ($A33F-$A349): (x - $30) >> 2 unsigned, plus the
  camera column. Shots left of the window (x 4-$2F, still flying until
  x < 4) test cells 53-63 columns to their right.
- [obs] Fuel depot ($AC87-$ACB4): one stage per main-loop pass with $40=1,
  so stages 4 to 0 take some 7 frames and the 7-frame window copy shows
  only the full depot and the sunk one (reference refuel run, frames
  555-558). The landing pad ($8C57) waits 5 frames per step, so its three
  one-row moves show.
- [obs] Tanks respawn ($9A53-$9A89): a destroyed tank (state 1) comes back
  home (state 7) when the craft's row is at least its home row + 3 and none
  of the 13 cells from its home column - 5 holds a tank body ($6C-$6E).
- [obs] The mine's four phase pairs ($963C) are its art shifted 2 pixels
  each, with its light in a different place in each (an animation).
- [obs] An enemy helicopter that appears inside a breakable barrier is the
  original's own data: level 0 patrol point 1 ($C3,$16) is next to $47 rock
  (map cells 00 00 47), so the enemy overlaps it, gets a VIC background hit
  and explodes, and its shot removes $47 rock for good. The emulator run
  meant to show it was inconclusive (the craft crashed first) [hyp: the
  same sequence happens in the original whenever the enemy reaches that
  point].
- [obs] The enemy explodes only on the VIC's sprite-background register
  (or a shot): $D01F, read into the hits the enemy code tests. In
  multicolour cells pixel pair 01 (shared colour 1, $D022 = the level's
  colour, red/brown on levels 0 and 2) is background for that register
  (VIC-II documentation; the rasteriser models it), so the enemy flies
  through brown rock and explodes on the green and white parts.
  Verified on the original (`scenario.sh enemyrock 600
  "530:6E=03/76=0F/77=0D"`: the enemy put into the brown rock left of the
  level 0 fuel depot, beside the hovering craft): in fortref's own pictures
  it sits deep in the brown rock, visible, for frames 531-543 with $6E = 3,
  and explodes at 544 only when its own shot appears on it (sprite-sprite);
  Joyport matches frame for frame (573/580, the rest HUD races). (An
  earlier note here cited the enemy run's frame 868; there the original's
  enemy is not on screen, so it showed nothing.) The player's craft does
  die on brown rock: it also probes the map cells round it ($A134).
  The emulator core's VIC (RetroReverse tools/platform/c64/owned/vic.cpp)
  sets $D01F only for multicolour pairs 10/11 (`foreground=pair>=2`), as
  documented for the VIC-II. The user remembers the enemy dying on brown
  rock on a real C64 [open: other releases than this U.S. Gold tape, or
  another case, not checked; to check in VICE: start a game, hover over
  the level 1 depot, then in the monitor `> 6e 03`, `> 76 0f`, `> 77 0d`].
- [obs] The enemy keeps out of rock by its own cell tests before each move
  ($9DB9 left, $9DE9 right, $9E19 down, $A000 up), except the climb bug.
  Measured on Joyport (Original pilot, widescreen 21:9, 60,000 frames of
  random flying on level 0): its sprite never covered a rock pixel, neither
  in the original window (1,506 frames shown) nor in the widescreen
  margins (27,066 frames). So a sighting inside rock is the climb bug's
  doing, or a case not reached by random flying [open: not reproduced].
- [enh] Particles: pieces could stay inside rock (up to 39 at once after a
  crash, seen in rendered runs). They now move a pixel per substep, leave a
  hit only into the open (3x3 clear), and a piece the ground closes over is
  removed (at most 8 at once, which then vanish).
