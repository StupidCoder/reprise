# Bruce Lee (C64) — how the original works

What was learned while rebuilding Bruce Lee (Datasoft, 1984; this image: the
original disk): the disk, its loader and copy protection, the memory map and
data formats, how the program runs, the rules of play, and the original's
own bugs. The remaining work is in [plan.md](plan.md).

## Sources and evidence

- **Image:** `Bruce_Lee_(Original).g64` (333,744 bytes, G64 version 0),
  SHA-256 `f2ccd35eb3c5e868a70d6bd71903b49ce13b383b95fe675e57ad8873c46c1e53`.
- **Method:** everything here comes from this image, its decoded sectors,
  the disassembly and runs on the reference machine (RetroReverse's own
  C64 + 1541 core with the real 1541 DOS, `harness/blref`). No outside
  game-specific material was used; RetroReverse had nothing on this game.
- **Addresses:** `$xxxx` is a C64 address in the running game (`game.prg`
  $0400-$A3FF, `high.prg` $E000-$FFFF).
- **Paths:** source paths are relative to the Reprise root. Evidence lives
  outside the repository in `.analysis/bruce_lee_c64/`:
  - `disasm/code.txt` and `disasm/labels.txt`: the listing;
  - `evidence/`: RAM at the entry, memory maps, recordings;
  - `harness/`: `blref`, its checkpoint and input scripts;
  - `tools/`: G64 scanner, research decoder, `memmap.py`, `ticks.py`,
    `tour.sh`, `disasm.py`;
  - `chapter/`: material for the lesson chapters.
- **"Joyport"** in the notes below is the native re-implementation (the
  project's name before the move to Reprise). The checking tool is
  `build/bruce_lee_replay` (modes `pass`, `lockstep`, `music`, `front`,
  `zpview`, `run`, `effects`); `build/joyport` is now `build/reprise`.

## The original's bugs and oddities (summary)

Each is described where it belongs below.

- **Random numbers from game variables:** every new game zeroes the high
  byte of the random pointer (`$17`), so the draws read pages 0 and 1 (game
  variables and the loader's stack leftovers) instead of the SID-noise
  table the init built.
- **Exit slip:** an enemy leaving through an exit drops a return address
  (`$2F82`), so the row scan runs once more for Bruce a map row lower, past
  the screen into the sprite pointers and pose 0's collision mask.
- **Y left over from helpers:** `CPY` tests after step helpers see the
  helper's move code (`$1FA4` does `TAY`): jumps always drop two lines at
  the end; the Ninja's long throw only happens without a step down. The
  Yamo's "down" strike tests the slot number (`$1F0C`).
- **Landing-on defeat loop** (`$2C34` → `$2BE6`) repeats until the energy
  byte turns positive, scoring each round.
- **Room entry** clears only one history byte (`$0A1E` starts its count at
  $83, negative).
- **Map overruns:** the last run of rooms 4, 7, 10, 13, 17 and 18 writes 2
  cells past the map; room 20's header pointer lies past the 20-entry table.
- **Shared collision masks:** several poses build their mask at the same
  address (`$117F`/`$1251`, e.g. poses 0 and 42-54 at $9000), so the last
  mask built applies to all of them.
- **Music restart:** the frame that restarts a tune plays no tick
  (observed, not understood).

## Disk, loader and copy protection

### Image

- `Bruce_Lee_(Original).g64`, 333,744 bytes, G64 version 0, 84 half-track slots
- MD5 `8cbfd1595cdc18a8e68fe4d43eec6aa8`
- SHA-256 `f2ccd35eb3c5e868a70d6bd71903b49ce13b383b95fe675e57ad8873c46c1e53`
- Disk name "BRUCE LEE", ID "64", DOS "2A".

### Provenance and prior exposure

- No prior analysis of Bruce Lee exists in RetroReverse (searched 2026-10-25);
  no outside game-specific material was consulted. Everything below comes from
  this image, its decoded sectors and runs of it on the reference emulator.
- Tools: own Python scripts in `tools/` (GCR/CBM DOS decoding written from the
  1541 format description), RetroReverse's `dis6502` (Go) for disassembly,
  RetroReverse's owned C64 + 1541 core for execution (`harness/blref`).
- Firmware: C64 BASIC/KERNAL/CHARGEN as pinned in RetroReverse; 1541 DOS
  `dos1541-325302-01+901229-05.bin` (SHA-256 `d1d45afb…369e`) fetched on
  2026-10-25 with the user's approval from the same VICE svn-mirror revision
  as the C64 ROMs and recorded in RetroReverse's firmware manifest.

### Disk layout (observed, `tools/g64scan.py`)

| Tracks | Content |
| --- | --- |
| 1-4 | no sync marks at all (unformatted) |
| 5-17, 19 | standard GCR sectors, all header and data checksums good |
| 18 | 19 standard sectors plus one non-standard block (raw bytes `77 52 57` after a sync, then 16 data bytes, then ~310 zero bits) |
| 20-42 | no sync marks |

The BAM claims tracks 1-16 free. Most data sectors carry no file chain: the
loader reads them by track and sector.

The directory sector 18/1 links to itself and holds odd entries (names "1",
"2", "", "3", …, several pointing into graphics data). Only two are used:

| Entry | Start | Load | Use |
| --- | --- | --- | --- |
| PRG "2" | 18/4 | $00AC, 130 bytes | what `LOAD"*",8,1` loads (the 1541 DOS skips the SEQ entry before it) |
| PRG "" (name byte $00) | 18/2 | $0400-$0E89, 2698 bytes | second stage, loaded by stage 1 |

### Boot chain (observed in runs of the image)

1. **"2"**: the KERNAL stores each byte at ($AE),Y and increments $AE/$AF. The
   file's first bytes overwrite $AC-$AF themselves, so the pointer is
   redirected and bytes $1C-$7F of the file land at $02B0-$0313. That covers
   the BASIC idle vector $0302 ($02B0): BASIC autostarts the loader after LOAD.
   $0313 = $0A is reused later as a key.
2. **Stage 1** ($02B0, obfuscated with stack tricks and writes that wrap past
   $FFFF) loads PRG "" by the KERNAL. RAM $0400-$0E89 then equals the file.
3. **Stage 2** ($0500):
   - `$054E` (called several times) copies $0545-$054D to $FFFA-$FFFF and,
     by wrapping, $0000-$0002 (CPU port DDR $EF, port $F7, $02 = $32).
   - `$0592`: chained XOR (`a ^= m; m = a`) over $05BA-$09B9, key = XOR of
     $02AF-$0313.
   - Opens `#0` (buffer 0 = drive $0300), M-W's the drive routine from C64
     $0616 to drive $0300, M-E's it and M-R's drive $0300 back to C64 $0616;
     checks an XOR of $0500-$05FF against $0313.
   - `$0901`: swaps the pages $0400 and $0616 (the XOR sequence there is a swap).
   - `$0920`: 4 pages from $0947, `a ^= $0500[y] ^ m; m = a`, key = $02, Y
     running from $0A round to $09 on every page.
   - Shows the credits text (`$0947`), then `$0BA3`: key = $0313 XOR the
     readback's two data halves (equal, so it stays $0A), chain over
     $0BD1-$0CD0.
   - Loads 32 sectors from 17/0 to $6000 (the loading picture) and shows it.
   - Loads 32 sectors from 15/0 to $E000.
   - Second protection test (below); on a genuine disk loads 160 sectors from
     13/0 to $2F00-$CEFF. (A copy skips this load and crashes later.)
   - Decrypts $E000-$FFFF column by column (Y outer, pages $E0-$FF inner),
     key $054D; decrypts the 256 bytes of stage 3 from $0D8A to $02A7-$03A6
     starting at Y=$D0, key = CPU port register $00 (= $EF); jumps to $02A7.
4. **Stage 3** ($02A7-$03A6):
   - One chained XOR over $CE00-$2F00 downwards (offset 0 first, then $FF to
     $01 in each page), key $0A pulled from the stack. After each 32 pages the
     chain value must equal the next page number (the code branches on it).
   - Patches $03A2-$03A6 into $2F01, $4F01, $6F01, $8F01, $AF01 (the store
     operand's high byte is lowered by $20 each step).
   - Moves $2F00-$CEFF down to $0400-$A3FF page by page, in place: per byte
     `new[$0400+i] = old[$2F00+i]`, `new[$2F00+i] = old[$0400+i] ^ old[$2F00+i]`
     (written as a three-way XOR plus a fix-up).
   - Checks a 16-bit sum of $0400-$A3FF (carry out of the high byte fed into
     the next low add) against $0392/$0393 = $9872.
   - `JSR $0400` (game init).

Sector order of every load (`$0D21`/`$0D53`): start at sector 0, add 10
modulo 21 after each sector, one track down after 21 sectors (all loads stay
in the 21-sector zone).

### Copy protection

- **Drive routine** (uploaded at stage 2, runs at drive $0300): waits for a
  sync, matches the raw (not GCR-decoded) bytes `77 52 57`, stores the next 32
  raw bytes reversed into $0300-$031F, waits, does it again into $0320-$033F,
  then reads raw bytes into $0340+ until the C64's next command interrupts it.
  Bytes 1-16 after the signature are data; bytes 17-32 fall into the long
  zero-bit stretch.
- **Weak bits.** A real 1541 reads a long stretch without flux changes as
  noise, so the two passes differ in their second half. The second test
  (`$0C83`) M-W's a page to drive $0300 and M-R's it back to $CEF6, compares
  32 bytes against the next 32 and stores the index of the highest mismatch
  in $CF78 (it is $10 on a genuine disk unless the noise repeats by chance,
  about 1 in 256). Index 0 (no difference) skips the main load. The index is
  also XORed into $CF79-$CF88.
- **In-game checks** compare pairs of bytes in that readback page (e.g.
  `$513C`: `$CF8A` vs `$CFAA`, hang if they differ; reads of $CF7B/$CF9B,
  $CF84/$CFA4, $CF89/$CFA9 at $06E0 and $1FE3). These are anti-tamper code,
  not game rules; a clean port has no reason to keep them.
- The RetroReverse core reads the stored zeros back unchanged (its README:
  no weak bits). `blref` therefore refills every run of more than 3 zero bits
  on the mounted disk with pseudo-random bits every 20,000 cycles (fixed
  seed, `BLREF_WEAK_SEED` to change it). Without it the loader takes the
  "copy" path, skips the main load and crashes at $0322 (an unstable opcode
  in garbage) about 70 s in. With it the game boots in about 185 s of
  emulated time.

### Result and its verification (2026-10-25)

- Game entry: first fetch of $0400 at cycle 185,373,446 (default seed);
  checkpoint `harness/entry.state`, RAM `evidence/entry-ram.bin`
  (SHA-256 `fc3059fe…6a62`).
- Two boots with different weak-bit seeds give the same RAM at the entry
  except $00A2 (jiffy clock) and $B000-$B07F (leftover noise): the game image
  does not depend on the noise.
- `tools/decode_research.py` (a translation of the loader written to learn
  its rules) rebuilds $0400-$A3FF and $E000-$FFFF from the decoded sectors,
  equal byte for byte to the emulator's RAM at the entry, and reproduces the
  loader's own checksum $9872.
- Not reproduced (loader leftovers): $0000-$03FF, $A400-$CFFF. The game reads
  some before writing them: the protection page at $CF00 (anti-tamper only),
  random-looking blocks at $C000-$CA3F that `$513C` ORs shapes into (no
  visible effect in a 50 s attract run when refilled; the refilled run then
  hangs on the anti-tamper checks), and the stack page $0100-$013F, filled by
  stage 3's leaking `JSR $02F5` with `42 03`, read by `$117A`
  (`INC $16; LDA ($16),Y`), the random numbers' source in play (settled
  in "Random numbers" below; the native module models these bytes as
  `42 03` alternating).

## Memory map and assets

Input: the files the import installs (`game.prg` $0400-$A3FF, `high.prg`
$E000-$FFFF, `loading.prg` $6000-$7FFF), equal to the emulator's RAM at the
game's entry (see "Disk, loader and copy protection"). Everything here was found in this image
by disassembly (`dis6502`) and by runs on the reference emulator
(`harness/blref`). No outside material was used.

### Memory map (first pass, from coverage)

`BLREF_COVER=1` records every opcode fetch, read and write; `tools/memmap.py`
turns that into ranges with the reading routines. Run: title, then a scripted
one-player game in room 0 (`harness/inputs/play1.txt`, 95 s). Maps in
`evidence/memmap-0400.txt` and `memmap-e000.txt`. Only code reached in that
run is marked; the rest of the game (other rooms, the end) is still unmapped.

| Range | Content (observed / hypothesis) |
| --- | --- |
| $0400-$0530 | init: SID noise table at $0200, ROM font copy, charset expansion, video setup |
| $0531-$4927 | game code (main loop, IRQ split at $14BC, rooms, objects, input at $19D8) with tables between |
| $4325-$43B6 | screen row address tables (row low/high for screen and colour RAM) |
| $4788-$478B | play colours $D020-$D023 |
| $478D-$47A1 | colour RAM fill per room |
| $4AB5-$4ADC | room header pointers (20 words) |
| $4ADD-$4B40 | room headers (20-byte windows, overlapping) |
| $4B41-$4B6A | room map pointers (21 low bytes, 21 high bytes) |
| $4C00-$4FFF | tables read by $33xx (object/enemy data, hypothesis) |
| $513C-$5735 | more code (sprite shape building at $513C) |
| $5900-$5C8F | tables read by $518A/$2709/$2715/$2721/$272E (shape pointer tables, hypothesis) |
| $5D91-$68F8 | data read by $51C2/$51E5/$51F6/$269A (sprite shapes, hypothesis) |
| $68F9-$8180 | room maps (21 rooms; rooms 19 and 20 share one) |
| $8800-$8BFF | written at init: the text font from the C64 character ROM (see Text font) |
| $9000-$9FFF | four half-height charsets |
| $A000-$BFFF | written at init: the four expanded charsets |
| $8C00-$8FFF | screen during play (VIC bank $8000, `$DD00` = 1) |
| $E000-$EFFF | sound driver (code from $E300, frequency tables at $E200/$E255, hypothesis) |

Zero page and $0100-$01xx hold game variables (e.g. `$29` room, `$0100`
flag used by the screen clear at $1A71). The stack page's leftover contents
from the loader are read at $117A: the random numbers (see "Random numbers").

### Video setup (observed)

- Title: `$D018` = $31 (screen $8C00, charset $8000), multicolour off.
- Play: `$D018` = $38/$3A/$3C by room (charset $A000/$A800/$B000), `$D016` =
  $D8 (multicolour), border/background from $4788. The interrupt at $14BC
  waits for raster $6D/$6E and rewrites `$D022` on later lines (colour bands
  in the room's top part), then sets `$D018` = $30 for the status line.
- Sprite pointers at $8FF8.

### Charsets ($487F)

Four sources of 1 KiB at $9000, $9400, $9800, $9C00. For each 8 stored bytes
k: rows 0-3, each doubled, make character k; rows 4-7, each doubled, make
character k + 128. Results at $A000, $A800, $B000, $B800.

### Rooms ($0A9F)

- Room number in `$29`, 21 rooms (the low-byte table ends where the high-byte
  table starts).
- Map stream from `$4B41,X` / `$4B56,X`: bit 7 set = literal run of
  (byte & $7F) cells, $80 = end; bit 7 clear = the next byte repeated that
  many times (0 = 256). 40 cells per row, 11 rows, from screen row 3.
- A cell is two screen rows: character (cell & $7F) above, (cell & $7F) |
  $80 below. Cells with bit 7 set get colour $F8 (`$0B51`), all others the
  room's colour from `$478D,X` (filled by `$09CE`).
- Charset: rooms 0-9 $A000, room 18 $B000, others $A800 (`$0A8D`).
- A 20-byte header from `$4AB5` (word per room) is copied to $0153-$0166
  (not decoded yet). Room 20's pointer lies past the 20-entry table and reads
  $0004 (zero page); whether room 20 is reached through this path is open.
- The last run of rooms 4, 7, 10, 13, 17 and 18 writes 2 cells past the map
  (the drawing never checks).

### Text font ($0443-$0477)

The init copies ROM $D100-$D1FF (screen codes $20-$3F) to $8800 and
$D000-$D0FF (codes $00-$1F) to $8900, the inverse of each to $8A00/$8B00,
then sets $8900-$8907 to $FF (code $20: a solid block), $8A10-$8A17 to $03
and $8AB0-$8AB7 to $C0 (codes $42, $56: edges of the inverse key boxes). So a
text code is the ROM screen code + $20 (within $00-$3F), +$40 for inverse.
The interrupt selects it with `$D018` = $32 (`$1591`, from raster $82 on the
title: the option lines) and for the status line in play.

The 64 ROM glyphs are kept in the source tree (`shared/c64/character_rom.cpp`,
the owner's decision of 2026-10-25, marked as outside the MIT licence).
Verified: `text.bin` from `bruce_lee_assets` equals the game's $8800-$8BFF on
the title and in play (1024/1024 bytes).

### Loading picture

Multicolour bitmap at $6000 (32 sectors from 17/0, kept as loaded), shown
with screen RAM $5C00 filled with $CE, colour RAM $01, background 0, border
$0E (observed in the emulator at 40 s).

### Verification (2026-10-25)

- `build/reprise --import Bruce_Lee_(Original).g64` installs game.prg and
  high.prg equal byte for byte to the emulator's RAM at the entry ($0400-$A3FF,
  $E000-$FFFF), loading.prg equal to the raw sectors (and to $6000-$7FFF while
  the picture is shown).
- `build/bruce_lee_assets <content> <out>`: `charsets.bin` equals the
  emulator's $A000-$BFFF after the init (8192/8192 bytes).
- `room00.bin` vs the emulator's screen and colour RAM from row 3 during the
  attract run in room 0: colour RAM identical; screen identical except 4
  tiles the game changes while playing (lanterns: $14 -> $1F, $31 -> $12).
- The other 20 rooms are checked only by eye (they render as coherent
  rooms); not yet compared with the game's own drawing.

## Game code

Working notes on the game's code, from the image. Listing: `disasm/code.txt`, made by `tools/disasm.py`
from `evidence/entry-ram.bin` and coverage runs (`--cover`); names go into
`disasm/labels.txt`. Runtime facts come from `harness/blref` runs (tick logs:
`BLREF_TICKS`, `tools/ticks.py`).

### Frame and pass structure (observed)

- PAL frame = 19,656 cycles. The interrupts run every frame; the main loop
  (`$1BDD`) sets `$48` = 3 and waits until the interrupts count it to 0, so
  the game logic runs once every 3 frames (measured: 3-frame spacing, ±300
  cycles of jitter).
- IRQ entry `$10FA` → `JMP ($0408)` (the vector is kept in the init's space).
  NMI `$110D` (CIA 2 timer): if the top split has not run yet (`$4927`), it
  does the top split's writes (a safety net for long SEI stretches).
- Play: `$48AE` at raster $FD (status charset `$D018`=$32, `$D021`=$FC, the
  room's split tables, `JSR $15C0`) → `$4928` at raster $40 (room charset,
  top colours) → `$498F` per split line → back to `$48AE`. Title: `$1544`
  (raster $FD, `JSR $E006` music) and `$158C` (raster $82, text charset).
  Player screen: `$14BC` (sky colour bands).
- `$15C0` per frame: sound channels (`$EB03`, `$EF73`, `$F073`), RNG counter
  `$16`++, sprite placement (`$25CF`), counters, colour cycling (`$1675`),
  `$48`/`$49`/`$4A` countdown, joystick read (`$16A1`), anti-tamper (`$1682`:
  hangs unless `$00` = $EF).

### Main loop order (`$1BDD`)

`$19D3` (attract: fire/key ends the demo) → in play: keys (`$1B44`),
`$1A07`, `$1A23` → `$1AB1` (1UP blink timer) → `$40A9` (flames) → if no
special room (`$7A`=0): `$0D98`, `$0D03`, `$0DA5` (lanterns?) → `$3A5C` →
`$521B` (terrain probe) → `$2E26` (terrain under actors: exits, lanterns,
hazards) → room-0 extra `$29B0` (when `$4CAB,room` = 0) → `$3181` (score) →
`$17B6` (actors) → `$2571` (sprite blocks) → `$2D42`.

### Actors (slot X: 0 Bruce, 1 Yamo, 2 Ninja)

| Var | Meaning (observed / hypothesis) |
| --- | --- |
| `$95,X` | flags: bit 0 active; bits 6/7 player-controlled modes |
| `$98,X` | joystick nibble, active low ($0E up, $0D down, $0B left, $07 right, $0A/$06 up-left/right, $09/$05 down-left/right, $0F none); Bruce's from the stick (`$16BA`), the enemies' from their logic |
| `$9B,X` | fire (0 = pressed) |
| `$9E,X` / `$A1,X` | x (pixel pairs, 13-163) / y (lines, up to $C7) |
| `$A4,X` | facing (1 right, 0 left) |
| `$A7,X` / `$B6,X` | terrain flags at mid height / at the feet (probe, below) |
| `$BF,X` / `$C2,X` | move now / move of the last pass |
| `$CB,X` | repeat counter for the same move |
| `$CE,X` | step counter of timed moves |
| `$0106,X` | delay before the next decision (from `$4A99,move`) |
| `$4F5B,X` | pose (sprite shape) |
| `$4F64..$4F6D,X` | moves this pass up/down/left/right (counts), history to `$4F70+` |

Moves ($BF) seen: 0 stand, 3/4 walk, 5 jump, 6/7 jump left/right, $0C/$0D
flying kick, $0E fall, $0F, $10-$12, $13, $14/$15 enemy decisions, $16,
$17, $18, $19 (death?), $1A, $1B. Step rules in `$1C6D-$24F3`.

### Terrain probe (`$521B`), verified

Per active actor: the pose's collision mask (3 columns × 32 rows of
multicolour pixel pairs, built at `$513C` from the sprite shapes, an odd
pose ORed into its even partner; column 2 always empty: `$51F4` loads the
second byte but `$5208` doubles `$1F`) is placed by the actor's offset in a
column-after-column buffer and ANDed with the glyphs of the 4 × 3 cells
from screen row `(y-29)/8 + 1`, column `(x-12)/4`. Each non-zero overlap
byte records both of its 4-bit patterns. Where a cell's colour has bits
0-2 set, patterns 3, 7, 11-15 move to slots 16-22. Four groups of patterns
make `$B6` bits 0-3 (`$5465-$5598`; group 0 counts the left column's
pattern 17 twice and never the middle column's). `$A7` = the same after
15 + offset mask rows, `$B6` after 21 + offset.

Native: `games/bruce_lee_c64/game/terrain.cpp`. Check:
`build/bruce_lee_replay probe <content> <ticks.bin>` on `BLREF_TICKS=full`
logs: 2786/2786 passes of a scripted game and 6129/6135 of a 400 s attract
run agree; the 6 others are passes where a room change or a death happens
in between (to recheck with the native flow).

### Sprites

210 poses: shape address (`$59BD`/`$5A8F`), rows-1 (`$5B61`), colour
(`$5C33`), wide flag (`$5CBF`). Bruce and the Yamo use two sprites (odd pose
= second colour layer), the Ninja one (colour forced to $F0). Sprite blocks
are rebuilt every pass in double buffers at $8180-$83FF (`$268D`), pointers
flipped (`$2750`); sprites are x-expanded; x on screen = 2·x (−16 for some
wide poses), y on screen = y + $1D.

### Rooms

- Exits: map characters 2-5; per type a table of destination room, x, y
  (type 2: `$4B6B`/`$4C0B`/`$4C1F`; 3: `$4B7F`/`$4C33`/`$4C47`; 4:
  `$4B93`/`$4C5B`/`$4C6F`; 5: `$4BA7`/`$4C83`/`$4C97`). Taken only at a room
  edge while moving out (`$30D1`); room 11 anywhere.
- Lanterns: `$335B` list; collected by `$3349` (state 0, `$2A`+room count
  down).
- Per-room setup routines through the table at `$4EA9` (RTS trick).

### Room setups and scripts (rebuilt: `game/rooms.cpp`)

- Room entry (`$09AE`) ends with `$335B` (taken lanterns drawn, `$4EA4-$4EA8`
  cleared, the room's setup through `$4EA9`) and `JMP $3A5C`: the room's
  script runs once at entry, then once per pass after the enemy spawns.
  `$7B` toggles every pass; most scripts act only when it is 0. Entering
  room 11 with rooms 12, 14 and 16 cleared leads to room 13 instead
  (`$09E8`). The charsets are not reloaded at entry (the flames' and the
  lift's glyph changes stay).
- `$2A + room`: lanterns left per room; lantern records (`$4CEF`) are
  state, taken character, column, row index; state 0 = taken. Scripts
  test record state bytes directly (e.g. `$4D7D` = room 4's first).
- One-shot pattern: `LDA $4EA4+k / BMI skip / (condition) / DEC $4EA4+k`;
  setups that start a counter at 1 make the change happen twice (rooms 12,
  14, 15: the opening sound plays twice).
- Helpers: `$3A77` (sound $8A, strings $13, $13, $14 down a column = a
  wall opens), `$3A8D`/`$3A97` (last string $17/$2D), `$3AA1` (strings
  $63, $63, $64 = a column closes), `$423C` (sound $8A).
- Movers (`$71-$79`, three): frames 0-6 of a text figure (strings
  $1F+f or $26+f); at frame 3 one column on (old place frame 6) until a
  limit, after frame 6 back to the start column with a negative wait.
- Slider (`$6E` place, `$70` last place, `$6F` row; `$403B`: sound $89,
  string $1C over the last place, $1B at the new one; `$4050`: $55/$54).
- Lift (`$2C5D`, rooms 4-9, 11): `$60` passes per step from `$4CBF`
  (bit 7: a pause), `$66` steps per speed, `$430E` speed 7..0, then
  direction `$65` turns; a step scrolls glyphs $61-$63 (and $E1-$E3) by
  one pattern row from `$42DE` (rooms 0-9) or `$42F6`; `move::ride` moves
  the riders 2 lines when `$60` = 1. Room entry sets `$65` = 0, `$430E`
  = 7; the play-screen start (`$0808`, also after a lost life) `$60` =
  `$66` = 1.
- Crumbling floors (`$3068`, `$3215`): an actor in a code $60 cell marks
  it (colour $FD, up to three, row/column of the cell above-left); after
  8 passes it cracks (strings $2F/$30, sound $8D), 12 opens ($31/$32), 16
  cracks, 20 mends (string 0, colour $F8, $33), 24 free.
- Room 13 (`$3E69`, `$1042`): falling through scores 500 at each of
  three heights once per round (`$014B + 4*player`); otherwise nine
  pieces (`$010F` mode, `$0119` place, `$0123` row, `$0137`/`$0141`
  span) move every other pass; spans depend on `$40`.
- Room 16: the extra fall (`$2D`/`$2E` cells, `$2FD3`): five per player
  (`$4EA2`), one per visit (erased by string $56 when taken, and at entry
  when none are left).
- Room 18 (`$3CC9`, `$4120`): while the lantern hangs, two fireballs
  (sprites 6, 7; random 4-row shapes, colour random) fall at 5 lines per
  pass, 3 (2 near the floor) steps towards Bruce's x; Bruce's sprites
  touching them (`$D01E` bits 0-1 and 6-7) is death. The face at (2, $10)
  looks at Bruce every 8 passes (thresholds `$4A96`). Taking the lantern:
  3000 points, fireballs off, $50 passes of flashing colours (sound $8B),
  then the exit at (11, $25) opens.
- Room 19 (`$424C`, `$425F`): 1112 frames (`$49/$4A`) of flashing and
  string 4; then `$40` counts up, room 13's bonuses, lanterns and
  first-visit scores come back, and play goes on in room 0.
- The exit test (`$30D1`) opens the bottom edge only while falling
  (fixed in the native code after the attract run fell into room 5).

### Random numbers

`$117A`: `INC $16`, `LDA ($16),Y`. The init fills a table at $0200 from
SID noise and points $16/$17 at it, but every new game (`$0F69`, which
clears $03-$F5) zeroes `$17`: in play and in the demo the numbers are the
bytes of pages 0 and 1 at `$16` + Y. The callers' Y: the flames $40 ($2F
when `$1AB1` printed in that pass), the Ninja's wait $40 (3 after the
Yamo's arrival), placements the character kind. So draws read $40-$13F:
game variables, page 1's flags and room-13 tables, and the loader's stack
leftovers ($42/$03 alternating). Native: `random_source.cpp` gives the
bytes from the game's state (95.5 % of the bytes as recorded at pass
starts; temporaries as 0).

### Front end (rebuilt: `game/front.cpp`, `game/game.cpp`)

- Title `$0584`: rows from `$4A1A`/`$49CA`/`$49F2`/`$4A42` in the `$8000`
  charset (codes | $80), strings $4F-$51 single height, inverted labels;
  F3 toggles `$42B6` (turns), F5 `$42B7` (Yamo player), F7 or fire starts
  (fire release, 16 frames); L / shift-L / R / shift-R set `$139B`/`$139C`
  and print a message that `$08DE` clears; after $0960 frames (the
  interrupt's countdown skips one value per high-byte borrow: 2391 frames)
  the demo `$0531`. Music every fifth title (`$09AD`), unless `$139D`.
  Interrupts `$1544` (raster $FD: music, upper charset) and `$158C` (raster
  $82: text font, 38 columns, x scroll 6). Sprite 0 is the trademark sign.
- Demo: options off, `$0104` = 0, input from $F300; fire or F1 ends it
  (`$19D3` -> `$050F`), Bruce's death returns to the title.
- Player screen `$0E9B`: $DC fill, "PLAYER n" from `$4A6A`/`$4A7A` with the
  digit from `$4A8A`/`$4A8E`; interrupt `$14BC` shades it with `$D022`
  line by line (lines as the original's pictures show them); $40 frames
  ($80 with turns).
- Game over `$18A6`: `$42BA`/`$42CC`, the player label, the score as big
  digits (`$199F`), $FE frames, then the other player's turn or $40 frames
  and the title.
- Lost life `$1830`: lives down (negative: game over); turns swap `$03` and
  the state blocks (`$187A`: $28-$43 with $438C, lantern states `$0FE4` with
  $0340, marks `$188D`); the player screen; `$0755` and L3204.
- In play `$1B44`: F1 abandons, SPACE pauses (string $0B; fire on the
  player's port ends it once `$0105`'s guard runs out), S toggles `$139D`, V
  the version line. `$1AB1`: "nUP" blinks every 16 passes. `$1606`: an "L"
  for left-handed sticks. Idle (`$82`, 128 x 256 frames without input):
  pause and a colour cycle of `$0153`. The Yamo's player holding fire
  without moving for 7 x 256 frames gives up (`$1774`): `$42B7` off, clock
  $20, both players five lives less.

### Sound (rebuilt: `game/sound.cpp`)

- Effects: `$24F6` takes a free voice slot or one of lower priority
  (`$255F` by the effect's low nibble), sets the volume. Per frame
  (`$15C5`) each voice runs `$EB03`/`$EF73`/`$F073`: streams by `$EBE5`,
  commands through `$EC03` (0 end, 1 note, 2-6 note with waveform,
  envelope `$EC11`, pulse `$EC1B`, 7 waveform only), notes `$E200`/`$E254`.
- Music (title only, "Micropearl Music Player 1983 John A. Fitzpatrick"):
  `$E000` stop, `$E003` start the list `$E870`, `$E006` per frame. Three
  streams per tune (`$E8C0`), voice defaults `$E2A8`, commands $8x/$Fx
  lengths (`$E6F9`, styles), $9x volume, $Ax waveform/style/legato/ring,
  $Bx envelope sets `$E80A`, $Cx pulse width, $Dx filter sets
  (`$E80A`+$40), $Ex tempo, $FF end. A one-byte look-ahead (`$E130`) decides
  when commands follow a note. The frame that restarts a tune plays no
  tick (as observed).

### Native rebuild and its checks (2026-10-25)

Code: `games/bruce_lee_c64/game/` — `playfield` (the character grid the game
reads as terrain), `terrain` (probe), `actor` (moves, decisions, enemy AI,
poses), `world` (pass: flames, enemy timers and arrivals, probe, terrain
events, combat, actors, removals; per-frame work; room entry; printing;
status; lives), `screen` (picture from the world, sprites), `game` (frames,
passes every third frame, flow), `module` (Joyport module).

Checks (`build/bruce_lee_replay`, logs from `blref` with
`BLREF_TICKS=full BLREF_PCLOG=1bdd,29be,117e`):

- `pass`: per recorded pass, world injected from the record, the
  original's random values and collision register replayed: all 4233
  passes of a 400 s attract run and 1432/1434 of a scripted game agree
  (the two others: the stick changed in the frame the original reads it).
- `lockstep`: from one record on, the native game runs on its own state on
  the original's pass schedule; the stick from the input script (the frame
  the original used, from the interrupt's stick reads `$16D5` before the
  actors `$17B6` in the log; else the frame after the pass start),
  collisions from the native pictures, only random values replayed; after
  each pass the actors (with their step histories), counters, room-script
  state, lift, crumbling floors, the screen from row 3 (codes and colours)
  and both room charsets are compared. The whole scripted game (1435
  passes) and the attract demo (1612 passes, until the demo ends) agree.
- Room tour (`tools/tour.sh N [taken]`): harness run with room 0's exits
  poked to lead to room N's start and Bruce poked onto the exit ledge
  (optionally every lantern poked as taken), then lockstep with the same
  exits; results below.
- Pass starts are taken at $1BDF: an interrupt at the fetch of $1BDD makes
  the core show that fetch twice.

Findings from the room tour (kept in the native code): the bottom exit
opens only while falling (`$30F6`); a death earlier in the cell scan keeps
the lift from taking the actor (`$310B`); lost lives go through `$0808`
(lift timers 1); room entry clears only one history byte (`$0A1E` starts
its count at $83, negative); an exit with x = 0 passes on to the next
exit type (`$2F3C`); an enemy leaving through an exit drops a return
address (`$2F82`), so the row scan runs once more for Bruce one map row
lower, past the screen into $8FE8 ($DC from the player screen), the sprite
pointers and pose 0's collision mask at $9000 (the tour's room 0 run
sends Bruce back to the start through an exit code found there).

More from the tour: a lost life re-enters the room through the room-change
path (`$189E` -> L3204 -> `$2F7B`), so enemies on screen follow Bruce back
in at his start place with full energy; `$F2` is the `$D018` copy (room
charset) and `$7C`, the terrain probe's charset page, is copied from it
by every frame's interrupt (`$15E8`): room 18's end switches `$F2` to
`$3E` (charset 3, now and then still `$3C`), and after a redraw the first
pass probes with the old page (after a lost life charset 0, from `$07D0`);
the main loop sets its 3-frame wait at the top and waits at the bottom, so
after a redraw the next pass follows at once; rooms are drawn with
interrupts off, so the per-frame work (clock, countdown, counters) pauses.

Harness log artefacts (handled in `read_pass_log`): during a badline stall
the core logs a fetch on each cycle before the stall and again after it;
an interrupt taken at a fetch logs that fetch again after the RTI (the
handler's first fetch comes 7 cycles after the cut-off one). Tour PCLOG:
`1bdd,29be,117e,16d5,17b6,10fa,110d,15c0,09ae,521b,2571,5228` (pass
starts, collisions, random values, stick reads, actors, interrupt entries,
frame work, room drawing, probe, sprite update, per-actor probe with the
slot in X). With these the lockstep runs exactly the original's frame steps
per pass (split around the room drawing), refreshes the probe's charset
page for an actor whose probe came after an interrupt (`World::before_probe`
hook), and builds the collision latch from pictures before the pass (frames
before the sprite update `$2571`) and after it.

Findings while checking (kept in the native code): the main loop masks the
enemies' energy every pass (`$1BFA`: Yamo `AND #$BF`, Ninja `AND #$87`);
several `CPY` tests after step helpers see the helper's move code in Y
(`$1FA4` does `TAY`): jumps always drop two lines at the end, the Ninja's
long throw only happens without a step down; the Yamo's "down" strike tests
the slot number (`$1F0C`); the landing-on defeat loop (`$2C34` → `$2BE6`)
repeats until the energy byte turns positive, scoring each round.

Not modelled exactly (timing): which frame's stick a pass reads (13-47k
cycles into a pass: same frame 14 %, next 77 %, the one after 9 %; native:
the next); the interrupt's random-counter steps inside a pass (native: the
counter moves only between passes, offsets of the callers taken as 0); the
noise table (native: the SID's noise register from its reset value,
sampled every 495 cycles).
