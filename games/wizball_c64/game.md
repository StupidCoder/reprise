# Wizball (C64) — how the original works

What was learned while rebuilding Wizball (Sensible Software / Ocean, 1987;
this image: a disk release): the disk, its loader and copy protection, the
memory map and data formats, how the program runs, the rules of play and the
front end, and the original's own bugs. The remaining work is in
[plan.md](plan.md).

## Sources and evidence

- **Image:** `Wizball.g64` (666,804 bytes, G64 version 0),
  SHA-256 `507775fefd1201c5a03351ffa4d35aa8622f0e10e1b07f582b53750a1d1a331a`.
- **Method:** everything here comes from this image, its decoded sectors,
  the disassembly and runs on the reference machine (`harness/wizref`,
  RetroReverse's own C64 + 1541 core). No outside game-specific material
  was used; RetroReverse had nothing on this game.
- **Addresses:** `$xxxx` is a C64 address in the running game
  (`wizball.prg` $0400-$FD00, entry $6389).
- **Paths:** source paths are relative to the Reprise root. Evidence lives
  outside the repository in `.analysis/wizball_c64/`:
  - `disasm/code.txt`: the listing;
  - `evidence/`: RAM images, coverage runs;
  - `harness/`: `wizref`, `lock.sh`, checkpoints and input scripts;
  - `tools/`: G64/CBM DOS scanners, research decoders, contact sheets.
- **Checking tools:** `build/wizball_replay` (modes `lockstep`, `front`,
  `run`, `savetest`, `soak`) and `build/wizball_assets`. "Joyport" in the
  notes is the native re-implementation's earlier project name.
- **Markers:** *hypothesis* marks what the code has not confirmed.

## The original's bugs and oddities (summary)

Each is described where it belongs below.

- **`$8558` reads `LDA $84FF`**, an opcode byte ($AD), so its test always
  passes (harmless).
- **Catellite shield warning** (`$89B9`) is never cleared: it sounds once
  per load.
- **Catellite catches of other colours** set `$B26C` = $80, after which it
  never counts hits again.
- **Kept-weapon list:** full without two blazers, the Wizlab hangs (kept in
  the re-implementation).
- **Random numbers depend on the CPU cycle** of each call (CIA timer), so
  the same input never gives the same game twice.
- **Anti-tamper code** (not game rules) uses unstable opcodes (`LXA` at
  `$B589`) and checks the vectors (`$90F8`).

## Disk, loader and copy protection

### Image

- `Wizball.g64`, 666,804 bytes, G64 version 0, 84 half-track slots (the
  half tracks 1.5-35.5 are copies of their whole tracks)
- MD5 `73e1a136da4b6972d39f00817b86983e`
- SHA-256 `507775fefd1201c5a03351ffa4d35aa8622f0e10e1b07f582b53750a1d1a331a`
- Disk name "WIZBALL", ID "C6", DOS "2A".

### Provenance and prior exposure

- No prior analysis of Wizball exists in RetroReverse (searched 2026-10-26);
  no outside game-specific material was consulted. I know Wizball as a game
  in general terms (a wizard in a bouncing ball, a catellite companion,
  paint droplets that colour grey levels); every technical finding below
  comes from this image, its decoded sectors and runs of it.
- Tools: own Python scripts in `tools/` (`g64scan.py`, `cbmdir.py` and
  `disasm.py` copied from the Bruce Lee notes; `mkram.py`, `levels_research.py`,
  `sprites_research.py`, `sheet.py`), RetroReverse's owned C64 + 1541 core
  for execution (`harness/wizref`, adapted from Bruce Lee's `blref`).
- Firmware: as for Bruce Lee (RetroReverse's pinned C64 ROMs and
  `dos1541-325302-01+901229-05.bin`), from
  `~/Development/RetroReverse/site/emulators/firmware/c64`.

### Disk layout (`tools/g64scan.py`)

| Tracks | Content |
| --- | --- |
| 1-35 | standard GCR sectors (speed zones as normal), all checksums good except 22/10 |
| 22/10 | header good, **data checksum wrong** (the protection, below) |
| 36-41 | unformatted noise, a few stray syncs |

BAM: tracks 5-17 used (the file chain), the rest free. Directory:

| Entry | Start | Load | Size | SHA-256 (with load address) |
| --- | --- | --- | --- | --- |
| PRG "LOADER" | 17/0 | $0314-$03FE | 1 block | `edd810a9…56aa` |
| PRG "F" | 17/1 | $4000-$43FE | 5 blocks | `656ce1f7…9a1e` |
| PRG "WIZBALL" | 17/2 | $0400-$FD00 | 251 blocks (tracks 5-17) | `0a0f0c43…1913` |

SHA-256 of WIZBALL without the load address (63,745 bytes):
`eca9b29e2774c9a57f1780bb2d8102f6938968920f7f74b8a2b679ace323fe23` (the
importer's check).

### Boot chain (disassembled, then confirmed by a run)

1. **LOADER** (`LOAD"*",8,1`) lands on the KERNAL vectors $0314-$0333 with
   CHROUT ($0326) pointing at its own code $0363, so the READY message
   starts it: it restores CHROUT, loads "F" (secondary address set: to
   $4000) and jumps to $4000.
2. **F** ($4000): sets the ILOAD vector $0330 to its own fast loader
   ($41D4), copies its receive code to the stack page ($42B1-$4397 →
   $0100-$01E6), opens the command channel with "I0:", a buffer "#", sends
   `U1:2,0,22,10` (block read of 22/10) and reads the error message:
   - first digit '2' (a read error, as on the original): $018A = $30 (all
     RAM while storing) and `LOAD"WIZBALL"` through the fast loader;
   - otherwise $018A = $37 and a jump into the end of the receive code
     ($0150) without loading the game: a copy crashes.
3. **Fast loader**: M-W's drive code to $0600 ($40D2-$41D1), M-E's it. The
   drive reads each sector of the file's chain itself (GCR decode at $F8E0,
   header and checksum checks) and sends all 256 bytes, two bits at a time
   over CLK/DATA ($0686); the C64 side ($018B) assembles and inverts them
   and stores with $01 = $018A ($01D8). The data are the file's bytes
   unchanged.
4. Last: "I" to the drive, screen on, $01 = $35, CIA interrupts off,
   `JMP $6389` (game start: `LDA #$35 / STA $01 / JMP $EC00`).

Evidence: `harness/wizref boot` then `resume` with `WIZREF_ENTRY=1`; first
fetch of $6389 at cycle 82,381,668 (about 84 s of emulated time).
`evidence/entry-ram.bin` (SHA-256 `1fb5a7f5…889e`) equals the WIZBALL file
at $0400-$FD00 byte for byte. `harness/entry.state` is the checkpoint at
that point; `harness/play1.state` is a game in progress (level place 0,
about 30 s after the entry with `inputs/start1.txt`).

### Copy protection

Only the read error on 22/10. The core reproduces it from the image (the
stored checksum is wrong), so no weak-bit model is needed (the harness
still has Bruce Lee's, harmless here). The game file does not depend on it;
no in-game re-check has been seen so far (open: search the code for drive
access or checks of $018A-like values).

### Emulator note: unstable opcode

The game executes `LXA #$FF` ($AB) at $B58B (`LDA #$00 / LXA #$FF /
DCP $FF97,X / RTS` at $B589, reached by `JMP $B589` from the frame code at
$8BB8). The core stops on the unstable opcodes; `wizref` finishes them as a
6510 with "magic constant" $EE (A = X = (A | $EE) & imm), so X = $EE and
the DCP decrements $0085 (wrapping). The NMI handler ($7C61) clears $85.
On a chip with constant $FF the DCP would hit $0096 instead, so what this
code means for the game depends on the chip (open item; `WIZREF_MAGIC`
selects another constant).

## Memory map, assets and code overview

Everything here was read from the WIZBALL file and from runs of the original
disk on `harness/wizref` (see "Disk, loader and copy protection"). Statements marked *hypothesis* are not
yet backed by the code.

### Memory map of the game file ($0400-$FD00)

| Range | Content | Evidence |
| --- | --- | --- |
| $0400-$045F | variables (change at run time) | entry vs play RAM |
| $0460-$047F | map row pointers, low ($0460) and high ($0470), 16 rows | $78E4, $7586 |
| $0532-$0B31 | level map, 16 rows x 96 tiles | rendered, see below |
| $0B42-$263A | nine 760-byte blocks: characters $A0-$FE per place | $77C6, `$B31C` |
| $2642-$3E92 | 249 tiles, 5 x 5 characters (25 bytes, row-major) | $78E4 (`$B62B` = base) |
| $3E93-$4552 | not decoded yet (read at run time in places) | cover |
| $4553-$55xx | sound driver (writes $D400-$D418) | grep of SID stores |
| $55xx-$B8F2 | game code with tables between (22 KB of code found so far) | `disasm/code.txt` |
| $B0xx-$B8xx | many variables and tables (e.g. $B128 place, $B1F1 view column, $B209-$B20B map position, $B2F0 places, $B31C char blocks, $B1D1 painted colours, $B11F paint progress) | code |
| $C000-$DFFF | 128 sprite frames (multicolour) | sheet, pointers in use |
| $E000-$EBFF | screens: $E000/$E400 the playfield (double buffered by $B1FD bit 4, `$8A25`), $E800 panel; their file contents are leftovers | D018 writes |
| $EC00-$EFFF | the init code (run once from $6389) and the high-score text; later the top split's screen | $EC00 |
| $F000-$F7FF | front-end charset (letters, digits, title logo), partly inverted in the file | $8C96, $8CC7 |
| $F800-$FCFF | playfield charset, codes $00-$9F (13 star characters animated by $74A7/$7506) | play RAM |
| $FD00-$FFF7 | (not in the file) the current place's characters $A0-$FE | $77C6 |

VIC bank 3 ($C000-$FFFF), set by the init ($DD00 = $C0..).

### Display (one play frame, `WIZREF_WRITES=d000-d02e`)

The interrupt at $7C73 runs a **raster list**: ($68),Y gives the next
handler's address (patched into a `JSR` at $7C9D) and the next raster line;
a zero line ends the list ($7CA6). The list is data (to be read from the
file, per the code-versus-data rule); the handlers are code. In play:

| Line | Handler | What it sets |
| --- | --- | --- |
| $30 | $89C3.. | top bar: 8 sprites (the icon tokens), $D018 = $B0 (screen $EC00) |
| $3F | $8A57.. | score row: $D018 = $8C (screen $E000, charset $F000), 2 sprites |
| $71/$73 | $8A25.. | playfield: $D018 = $8E/$9E (screen $E000/$E400, charset $F800), $D016 = $10 + fine scroll (multicolour), $D011 = $1B |
| $87, $E5 | $8B15, $8B39 | sprite re-use further down (multiplexing) |
| $EE | $8BD0.. | sprites off, colours |
| $F7 | $8B4B.. | bottom panel: $D018 = $DC (screen $F400, charset $F000), 8 sprites |
| $FF | $8BC5 | $D011 = $13 (24 rows: the lower border opens) |
| $01 | $7CB2 | back to the top of the list |

NMI ($7C61, CIA 2 timer): clears $85 (see "Anti-tamper / timing checks").

### Levels

- **Map** ($0532, 16 x 96 tiles): four blocks of 4 rows. Each block holds
  two levels side by side, columns 0-44 and 45-89; columns 90-95 hold a
  small extra section.
- **Places** ($B128 = 0-10): `$B2F0 + 4p` = first column minus one ($FF or
  $2C), first map row, two bytes not decoded; `$B31C + 2p` = the address of
  the place's 760-byte character block (copied to $FD00, codes $A0-$FE).
  Places 0-7 are the eight level strips; places 8-10 (column $00/$5B) are
  special (*hypothesis*: places the game uses between levels; to settle).
- **Scrolling** ($7A98-$7B8A): the view's left character column $B1F1 runs
  0-$BA (40 + 187 = 227 characters, a 45-tile strip plus two), with the
  fine scroll in $B1FC (bits 0-2 of $B1F2) and the tile position in
  $B209 (map column) / $B20B (column within the tile, 0-4). The start
  column comes from a random empty column ($7586/$75A8, `JSR $7533` = the
  random number source).
- **Colours**: unpainted levels use $D022 = $0B, $D023 = $0C, characters
  $09 ($778A). `$B1D1 + 4p` holds the painted colours (shared 1, shared 2,
  character, and a byte stored to $B51A); $7799 copies as many of them as
  `$B11F[p]` says (the paint collected so far).
- **Tiles** ($2642): 5 x 5 character codes. $78E4 builds a 5-character
  column of each of the 4 tile rows into the column buffer at $E79A.

Checked: the export's playfield charset for place 0 equals the game's
$F800-$FFF7 in play except 14 bytes of 13 animated star characters; the
rendered strips show the levels as seen in play.

### Sprites

$C000-$DFFF, pointers $00-$7F: icons (0-$0C), the wiz rotating ($10-$2F
region), catellite, shots, droplets, explosions, enemies, the cauldron
pieces, the cat. Byte 63 of each frame is $10 + a colour (the wiz frames
$15 = green): *hypothesis* that the game uses it as the sprite colour.
Pointers seen in 60 s of title and play: $00-$5C on the playfield screen,
$80-$8C and $A0/$B9-$BC (blocks in $E000-$EFFF, panel and digit shapes) in
the bars. Not all frames are named yet.

### Front-end charset

$F000-$F7FF: $F000-$F1FF letters and digits, $F200-$F7FF the title logo.
$8C96 inverts the first part and $8CC7 the second, each when its flag byte
($F100, $F6F0) asks for it; in play the first part is upright and the
second inverted. The file holds the first part inverted.

### Code overview (provisional)

- $6389 entry → $EC00 init (obfuscated) → $8F61.
- $639B: outer loop (title/attract: $6E42, $63CA, $6893, $70EC, $BAAD, then
  the game $63E0, $69F2, $8C44, $6E38, $6B27).
- $63E0: game set-up ($720B, $6E4F, $7D1A colours, $A9C6, $9F5F, $8C84
  font, $7A98 scroll, $9DE9, $9CD5, $8C50, $8040), then the per-frame loop
  $640E-$6465 (about 25 subsystems; $B1CF/$B1D0 end it).
- $7533: random numbers (called from the level start; to be read).
- $77C6/$776D: place set-up (characters, colours, panel picture $770D).
- $81CD-$81DB: $B128 changes (tunnels between places).
- $4553-$55xx: sound (SID); music data still to be located.

### Front end observed

Title: the gothic logo with colour cycling, "TOP SCORES" table (one and two
player columns, names like "JOE", "BAZ NIF"); a menu "ONE PLAYER / TWO
PLAYERS / TWO PLAYER TEAM / THREE PLAYERS / FOUR PLAYERS"; "GET READY!"
with ring sprites before play.

## Code

Read from `disasm/code.txt` (coverage runs `evidence/cover-start1.bin`,
`cover-random1.bin`). Names are mine. *Hypothesis* marks what the code has
not confirmed yet.

### Program flow

| Address | What |
| --- | --- |
| $6389 | entry: $01 = $35, `JMP $EC00` (init, obfuscated: CIA 2 timer A = $4CC7 (one PAL frame minus one cycle) as NMI, timer B free-running from $FFFF; vectors from $B62D; then `JMP $8F61`) |
| $8F61 | power-on: sound init, title tune ($45F1 Y=$1A) |
| $8F75 | **title**: raster list $B450, text $BFFE (high-score table, big letters), logo sprites ($8E32/$8DE4 render text into sprites), timer $9215(150 units of 5 frames); loop: $9829 colour cycling, fire → game ($6392), timeout → menu screen $9017; key 4 (B254) → info pages $944D |
| $9017 | **menu** (players): raster list $B480, $B7E0 text, $91DB stick up/down changes $B272 (0-4: one player, two players, two player team, three, four? see $B2AC), fire → $6392 |
| $944D | three info pages cycled ($944C), raster lists $B86E, texts $4199 |
| $6392 | new game: $70D3 (clear scores), $6830 (per-game set-up: places, players $BFD6-8 from $B2AC + 4*$B272, current player $BFD9 = 1) |
| $639B | **per life loop**: $6E42 restore player state, $63CA level start, $6893 "PLAYER ONE / TEAM ONE" screen ($B71B texts), $70EC panel, $BAAD SID clear, raster list $B414, $63E0 play, $69F2 game-over screen, $6E38 save player, $6B27 lives/next player |
| $63E0 | play set-up, then the **pass loop $640E-$6465** (one pass per frame, waits for raster >= $C8 at $6E83; occasionally two passes fall in one frame when the pass ends late) |
| $646F | the level-cleared sequence (when $B874 is set): fly-through, `$6576` loop with a reduced pass |
| $AB20 | **bonus stage** ($AC4E = 1, $B874 = 6) |
| $B076 | "SELECT PERMANENT WEAPON" / "ALL SYSTEMS GO!!" between levels |
| $6B92 | game end: high-score check $BFD0 (→ $BBA0), entry $6BFB/$6CBC (keyboard name entry, $6D94 key table), back to title |

### The pass ($640E)

`$6E83` wait, `$45BD` sound, pause check `$6EB7` (B254 = 1: run-stop? waits),
`$9AD6` erase enemy shots, `$7090` erase player shots, `$7A98` scroll,
`$7DF4` **wiz**, `$881F` catellite fire, `$72AA` token select/activate,
`$77F7` screen buffer preparation, `$6F0D` draw player shots,
`$99BD` move/draw enemy shots, `$747E` stars, `$7429` keyboard,
`$697D` joystick mapping, `$9E0D` cauldron/panel, `$681D` wait raster
$46-$C6, `$8640` catellite, `$A9DA` spawn from the place's spawn list,
`$A204` objects, `$A14A` level timer / filler spawns; end when $B1CF
(death/exit) and $B1D0 = 1.

### Input

- `$73D4`/`$7429`: CIA 1 ports to $B256/$B257, keyboard matrix scan to
  $B254 (key number, repeat-filtered by $B255).
- `$697D`: per player ($BFD9) and control mode ($BFD5+player): $B23F = wiz
  stick (bits 0-3 up/down/left/right low-active, bit 4 fire), $B23E =
  catellite stick. One-player mode: holding fire for 10 passes ($B242)
  while the catellite is out ($B234) toggles fire to the catellite
  ($B241); $B240 selects which port is whose.
- `$723D`: stick waggle detector (left-right) → $723C counts alternations;
  `$72AA`: 4 alternations (or keys) activate the lit token.

### Random numbers ($7533)

`INC $7532; A = $7300[$7532] XOR $DD06` (CIA 2 timer B low byte, counting
down every cycle since the init). The value depends on the exact cycle of
the call, so the port cannot reproduce it; lockstep runs replay the
original's values (PC log at $753F).

### Scrolling ($7A98, $7BCB, $77F7)

- Speed index $B1F4 (signed, from the wiz's horizontal velocity $B21C by
  `$818E`: (|v| - 4) / 8 + 1). Per pass $7BCB adds the 8.8 step
  $B33D/$B344[index] (right) or subtracts $B34B/$B351[index+6] (left) to
  $B1F3/$B1F2 (fine position; $63 = pixel within the char).
- Crossing a char boundary moves the view column $B1F1 (0-$BA) and the map
  position $B209 (tile column) / $B20B (char within tile); at the ends
  ($B1F1 >= $BB) $B25D stops scrolling and $B1F9 becomes the wiz's offset
  instead.
- Three screen buffers $E000/$E400/$E800 ($B1FD = $00/$10/$20 shown). The
  two hidden ones are rebuilt shifted one char left and right from the
  shown one in four chunks over four passes ($77F7, state $B200-$B208); the
  new edge column comes from the map ($78CF/$78AE → $E79A, 20 chars) with
  star characters filled into empty cells ($79C2: codes $49-$5A, 18 star
  shapes, $B27A rotates).
- `$747E` animates five star columns in the charset ($F80A + n*$90, record
  $B1B4: byte offset, phase, speed...) with random twinkles.

### The wiz ($7DF4 …)

- Position: x $B20D/$B20E (9-bit, = $AC + $B1F9), y $B20F; vertical
  velocity $B211.$B212 with direction $B217 (bounce), gravity $B27C;
  horizontal velocity $B21C (signed, ±$30, steps of 6 per pass from the
  stick; without thrust the stick only spins it); spin $B21A.$B21B
  (16 frames, sprite = $B21A + $10, $B525).
- $7F62 bounce: falling accelerates by $B27C until y >= $C8, then
  $818E/… bounce up; $8097 clamps y to $49-$C8.
- $836E landscape collision through `$7C1D` (screen address of x,y): chars
  $C2-$C7 = tunnel (→ $81C9 place change ±1, wiz shrinks into it
  $8216/$8239), chars >= $98 under the eight probe offsets $B3B6 → bounce
  handlers from $B3BE.
- Fire: $8444 (edge of fire) / $8467 spawns a player shot (6 records of 12
  bytes at $0480: column, row, dx, dy, char, colour, saved char, screen
  address, double flag); $B236 double shots, $B237/$B238 blazers.
- Death: $B1CF set (by collisions), $7DB8 explosion ($7D74: 16 debris shots).

### Objects (enemies, pickups) — six slots of $2A bytes at $0200

Templates of 23 bytes from the table at $3ECF (pointer per kind), copied by
`$9DA1` with random fields. Fields (offset): 0/1 x (8.8), 2/3 y, 4/5 dx,
6/7 dy, 8 movement mode (0/1 linear with acceleration, >=2 table-driven
via $0A/$0B pointer, 3 = with momentum $17-$1A), $0C step period, $0D
landscape bounce mode, $0E fire mode, $0F fire period, $10 kind/flags
($80 droplet?, $40 behind landscape, $20 …), $11/$12/$13/$14 animation,
$15 state (2 = hit, 4 = dying, 5 = …), $16 score, $1D-$20 hit box, $24
sprite frame, $25 hit flag, $28/$29 spawn-list entry.
`$A204` updates all: $A7F2 hits, $A386 motion, $A346 scroll compensation,
$A5B1 animation, $A659 bounds/landscape, $A541 firing ($9BB5 enemy shots),
$A274, $A29C sprite registers, $A744 hit box.

Spawning: `$9CEA` runs the place's **wave script** ($3902 copied to $ED02:
records of three bytes: kind (bit 7: wait until no objects), count, …;
$FD/$FE/$FF commands); `$A9DA` spawns from per-place spawn lists ($B1A6
pointers, random positions built by $9F9B) as the view passes their
columns.

### Enemy shots ($0300, 16 records of 16 bytes)

Char-based, moved by $9A14 (8.5 fixed steps in $0302/$0303 with dx/dy
$0304/$0305), drawn with $B8E1 shapes, colours $B885; hit tests against the
wiz and catellite boxes in $9AD6.

### Catellite ($8640)

$B261/$B262 position; follows the wiz's trail ($B244/$B24C, 8 entries)
when its stick is idle, else moved by its stick ($B263/$B264 + inertia
$B265/$B266); collects droplets ($88B5: object kind $10 = 2/5/6 → colour
counts $B12B-$B12D per $B194[place]). Rebuilt; details in "Play".

### Display

Raster list per screen (data; entries: line, 0, handler): play $B3E2,
title $B416?, menu $B452, info $B86E..., player screen $B4B2. Handlers:
$89BB top token bar (7 sprites $B4C5-$B4DC), $8A52 score row and wiz
sprite, $8A25 playfield ($D018 by $B1FD, fine scroll $B1FC), $8B0F/$8B22
second use of sprite 0, $8BCB/$8B46 bottom panel (cauldron sprites
$B504-$B523, colour cycling), $8BBB open border, $B90C/$B8F3/$B8FC/$B903
sound ticks.

### Sound

$45F1 start tune Y, $4553 start effect Y, $47D0 init, drivers $4CB7-$5513,
tick from the raster handlers ($B90C: $45D0 + three voice routines; $B960
voice LFO/sweep). Tune data $5525-$6388 (command bytes $C0-$EA).

### Anti-tamper / timing checks (not game rules)

- `$B589`: `LXA #$FF / DCP $FF97,X` decrements $0085 (cleared by the NMI
  each frame); `$8B04`/`$9287`: `LAX $86 / DCP $FF86,X`, unbalancing the
  stack when the compare fails.
- `$90F8`: compares $FFE2-$FFFF with $B615 and bumps $D021 if changed.
- `$AFA8`: a seven-key sequence ($AFCE) sets $BB07, the invincibility cheat
  (confirmed: $7EF4 skips the death while it is set; see "Play").

## Play

Status 2026-10-26. The play screen runs on our own code; the front end
("Front end") starts it. Every claim below
was checked by the lockstep tool against the original in the wizref
harness; runs are listed at the end.

### Structure

- `Play` (play.cpp, objects.cpp, combat.cpp, tunnel.cpp, picture.cpp): a
  game. `pass()` is one pass of the main loop ($640E): part A (the raster
  wait $6E83 through the cauldrons $9E0D) and part B (catellite $8640,
  spawns $A9DA, objects $A204, timer $A14A). `frame()` is the raster
  interrupts' once-a-frame work (sound $B90C, token bar $7269, score line
  $67A1, panel $8B9A, the blacked-out landscape $8A02).
- `Playfield` (scroll, screens, stars), `Wiz`, `Catellite`, `Sound` +
  `Music`, `Display` (VIC-like line/sprite settings rendered to pixels).
- Sequences inside part A: the original waits for the raster inside some
  routines (the tunnel's place change waits 88 times at $6E92). Part A
  then stops (`waiting()`), and `resume()` does one frame's step; when the
  sequence ends the rest of part A runs and part B follows in that frame.

### Findings made while rebuilding (this slice)

- **Life end** ($6E38, $6B27): progress saved, a life taken, the other
  player's turn in 2-player modes; game over when no lives remain. Three
  lives (lives 3 → 0 ends the game after the third). The death ($7DB8)
  also cuts off a tune (music mode 0, $D417 = 0).
- **Life start** ($7CDA) clears $B20D up to the length at $B26E ($60), so
  $B20D-$B26C: the wiz, sticks, powers, pearls, tokens and the catellite
  up to its hit count; not $B26D (the catellite's colour clock). Also the
  sprite positions $E7C4-$E7D4.
- **Display lag** (raster log: part B writes $E7C7/$E7C9 at lines 77-100,
  the line-48 handler copies at line 54-55; 316 of 317 object writes come
  after it): the catellite and the objects on screen are those of the pass
  before; the wiz (part A) is current. `Play` latches the playfield
  sprites at the start of part B. A pass that runs late cannot happen in
  the port (no slowdown); the original then shows the same lag.
- **Catellite** ($8640, $881F, $88B5, $895B): see catellite.hpp. It
  trails the wiz's 8-pass-old position (+3 down; x −8 plus an offset that
  moves 0..$15 towards the side the wiz last moved from), the pull limited
  to ±4/±2 by the sign of the subtraction (so $80 counts as large
  positive); the stick takes over for 10 passes after any direction. Hits
  count in $B26C: 9 loses it (token 3 shows again). Catches: droplet
  colour 2/5/6 = paint for the place's colour ($B194: 0,1,2,0,1,2,…) and
  50 points; 1 extra life; 4 wandering (random stick every 16 passes,
  colours $B5DF, one more hit loses it); $0E filler burst with the warning
  tune; 0 blacks out the landscape (shared colours 0 except 3 frames after
  the wiz bumps a solid cell, $83BA); others set $B26C = $80 (never counts
  hits again). Its shield warning ($89B9) is never cleared: it sounds once
  per load. Fire: shot slot 2, direction from the stick when steering,
  else from the wiz; the blazers fire from it when switched over ($B238).
- **$8558** reads `LDA $84FF`, the opcode byte $AD of the instruction at
  $84FF, so the test always passes (an original slip; harmless).
- **Tunnels** ($81C9, $8216, $8239-$8306): the centre probe sees $C2/$C3/
  $C6/$C7; $BE among the next three cells means rightwards. Right leads on
  only if the place is not the furthest reached ($B1CE); otherwise it
  re-enters the same place. 21 passes of sinking (sprite $B4 = a copy of
  the wiz frame shifted down a row a pass), then the place change: objects
  removed (their list entries may come again, $A1C9), $75E0 set-up, the
  aliens counted, enemy shots cleared; then 21 + 5 frames of the exit
  rising (two y-stretched sprites drawn from $CE7F/$CEBF into $ED40/$EDC0,
  the byte offset kept in $BB02, the sound's last-effect byte), the wiz
  sliding left from x $D0 to $AC one pixel a frame, 5 + 21 frames of the
  exit sinking; then the wiz routine runs again ($8306 JMP $7DF4).
  Sequence pictures: frame N shows step N-1 and frame N's interrupts.
- **Cheat**: $BB07 is set by a seven-key sequence ($AFA8 compares $B254
  with $AFCE; border colour 9 when done); quitting from the pause clears
  it ($6ED7). Hits then do nothing until the level is cleared.

- **The end of a place** (clear.cpp). The fourth cauldron full sets
  $B874 ($9E0D, not while dying or in a tunnel); the play loop then runs
  $646F: shots and objects gone, the wave script copied ($3902 → $ED02),
  thrust and antigravity granted ($7379 twice), and the loop $64CF fades the
  wiz out in rings (sticks forced: the catellite flies up-left) while the
  playfield's shared colours count up every frame and the music fades; at
  silence tune $13 starts. That first frame runs in the same frame as the
  play pass that found the colour. While $B874 is set the landscape probe
  is off ($836E), the first screen is drawn empty ($766B), and the 8.8 step
  moves $B1F2, which places the wiz ($7F0B, limits $87-$53).
- **The bonus wave** ($6512, loop $6576): two frame waits (colour RAM 0,
  then $0C); the font copied over the field's characters $A0-$DF ($6743)
  for the messages; the two bonus tunes take turns ($BB0C, starts at 2:
  tune $21 mode 1, then $0C mode 2 speed 1). The wave script ($9CEA):
  2-byte spawn records [kind (bit 7: wait for an empty screen), delay],
  3-byte loops [target+2, the marker $BB04 = $39, count] whose counts are
  used up in the copy, $FF/$FE speed the stars up/down to 6/0 in 1/16 steps
  ($6755), $FD ends it ($B1CF = $36). Bonus enemies use a second template
  table ($39DC) with the normal template copier ($9DA1) but no list entry or
  edge placement. A hit or the script's end shows the count (messages are
  data: [column, row, text, $FF] lines at $B7E8/$B813/$B826; the three
  digits $B841-$B843 live inside the text): every second frame a kill
  counts 40 points; then the message is printed again in black over the
  sky characters and the lab follows.
- **The Wizlab** (lab.cpp, $AB20, loop $AB7B): place 9's screen in 40
  columns; the powers put by ($B228-$B230) and the bar shows the weapons
  ($B074 set: quiet activations, base colour $0A, flash colours from
  $B53D+6, sprite colour 2). The stick (read on change only, $9420) moves
  the light over tokens 1-5; fire keeps that weapon ($B116-$B11E, re-armed
  at every life start by $6E4F). A full list drops two kept blazers; with
  fewer than two the original loops for ever at $B0D2 (kept: the game
  stops). The wiz walks in on a scripted stick ($B17C), the paint pots slide
  to it and pour one unit of the completed colour every 20 frames (a point
  every other frame while any remains), then 100 frames more. If the
  catellite was out it flies a scripted path ($AC43) to its perch. The
  score lines' middle shows "SELECT PERMANENT WEAPON" / "ALL SYSTEMS GO!!"
  in the title's colour run ($98BE). No weapon taken: 1000 points for each
  place still to go. Then the place is one colour more painted.
- **Level complete** (flight.cpp, $AE11, loop $AE9C): "WOT A WIZACE! /
  LEVEL n COMPLETED" drawn into sprites $B4-$B9 ($8DE4, three characters a
  sprite; $B9 is cauldron 0's sprite, so the panel shows the empty cauldron
  $38 meanwhile); 50 frames, the level scrolled at speed 6 to column $BC
  with a point a frame, 50 frames; then the furthest place with fresh spawn
  lists ($B158), $B129 counted. The flight loop often runs two iterations
  in one frame (its wait $6E71 returns at once when the work is short);
  the rebuilt one does one a frame.
- **Round end** ($AEFD, $AF20): places 0-7 all painted: painted counts
  cleared, $B12A counted, place 0 with new spawn lists; place 10's room for
  510 frames with its shared colour random every frame and a random effect
  every second; then a place change into place 0.
- **Life start** also re-arms the kept weapons ($6E4F) and **every wiz
  pass** sets the shot sounds' sustain from $BA81 (0-15, keys 5/6; $A0 by
  default, the file has $F0).
- **Display**: the aliens counter's colour uses $8309, a copy of $B874
  taken at the play loop's top; screen 0 ($E000) doubles as the score
  lines, so its rows 0-1 show at raster 66 when it is the playfield.

### Verification runs (lock.sh, inputs in harness/inputs)

| run | input | pokes | passes | result |
|---|---|---|---|---|
| still | still.txt | enemies off | 1698 | all match |
| move1 | move1.txt | enemies off | 2951 | all match |
| fight1 | fight1.txt | none | 3453 | all match, 0 sound resyncs |
| die | die.txt | none | 7691, 3 lives + game over | all match |
| cat1 | cat1.txt | 700:b259=03 (pearls) | 2200 | all match; catellite 1769 passes (follow, steer, fire) |
| cat3 | cat1.txt | + 0:bb07=07 (cheat) | 9718 | all match; one catellite hit |
| tun1 | tunnel1.txt | 0:bb07=07 | 1851 | all match; tunnel + 88-frame sequence + place 1 |
| clr | clear1.txt | cheat + 600: paint 20/20/20 | 4084 | all match: vanish, bonus, lab, place change, play on (colour RAM compared) |
| clr3 | clear3.txt | cheat + paint at 600/2300/4000 | 9118 | all match: three clears and labs, level flight, level 2, a fourth clear |
| round | clear3.txt | as clr3 + places 1-7 painted | 6590 | all match: round end (place 10), round 2 |
| die (again) | die.txt | none | 8326 | all match, with the game-over message passes ($6A94) |
| team1 | team1.txt (`WIZ_MODE=2`) | cheat + 800: pearls 3 | 1749 | all match: two-player team, the catellite steered from port 1 (fails at pass 0 with mode 0) |
| mode1 | mode1.txt (`WIZ_MODE=1`) | none | 16652 | all match: two players taking turns, six lives, player 1's game over between lives |
| mode3 | mode3.txt (`WIZ_MODE=3`) | none | 16669 | all match: a team against a player |
| mode4 | mode4.txt (`WIZ_MODE=4`) | none | 16626 | all match: two teams |

Pictures (WIZREF_PICS vs the tool's PNGs): fight frames 1500/2200/2900/
3300, catellite frames 900/1050/1500, tunnel frames 1000/1020 (inside the
sequence) and 1050: 0 differing pixels (frame N ↔ pass N−307).

The tool replays the recorded interrupts where the rebuilt game passes the
same logged addresses (Play::set_marker_hook: $6E82, $6E91, $7DF4, $8640,
$682F, $6E92, $654E, $6573, $AB58, $AB5B, $7DBE), and treats a pass as the
stretch between loop tops ($640E, $64CF, $6576, $AB7B, $AE9C). Differences
only in effect slots or a tune voice's duration are interrupt-timing races
(an interrupt inside a tune's start, say) and are taken over.

Tool switches: `WIZ_ADOPT_PAINT=auto|pass`, `WIZ_ADOPT_PAINTED`,
`WIZ_PNG_FROM`, `WIZ_DUMP_SCREENS`, `WIZ_ADOPT_PEARLS` (take poked pearls over),
`WIZ_INVINCIBLE` (the cheat poked in), `WIZ_SEARCH=pass` (try stick holds
in the rebuilt game to script recordings, e.g. tunnel1.txt), and
`WIZ_SOUND_DIFFS`.

### Player modes ($B2AC, $697D, $6B27)

The menu's mode picks three control bytes ($B2AC + 4 * mode, copied to
$BFD6-$BFD8): player 1's and player 2's control (1: one player, 2: a team
of two on that side) and whether there is one side only (1). Modes: one
player 01 01 01, two players 01 01 02, two-player team 02 01 01, three
players 02 01 02, four players 02 02 02. A team ($697D, control 2) steers
the wiz with the port whose fire started the life and the catellite with
the other port (no ten-pass fire hold). Sides take turns per life
($6B27); a side's last life ends in the game-over message ($69F2/$6A94),
also while the other side plays on. The set-up starts its tune only at
its end ($6A91), after about two frames of drawing; it copies all eight
sprites from $B4E5 (positions, colours from $B4F5, pointers) and blacks
the cells behind the text in rows 9-11, columns 11-28. The start limit
for the next game is the shared furthest place ($B1CE, not in the
per-player progress blocks $E3C0/$E3D8) less two.

The lockstep tool takes `WIZ_MODE`; lock.sh now records the game-over
loop ($6A94) as a pass, and the tool runs the rebuilt message there.

## Front end

Status 2026-10-26. The module now runs the whole game from the title:
title, menu, wiztips, the player screen, play ("Play"), the game-over
message and the high-score entry, with the keyboard. Findings come from the
image and wizref runs; pictures were compared with WIZREF_PICS.

### Structure

- `FrontEnd` (front_end.cpp, front_picture.cpp): the screens as a state
  machine, one `tick()` per frame: the line-1 handler work (title keys
  $AF85, the tips' animations), the screen's main-loop pass, then the
  interrupts' sound work (`Play::frame()` while a game's list runs, else
  `Sound::frame()`, plus the extra music ticks $B903/$B8FC/$B8F3).
- `ColourRuns` (colour_runs.cpp): the colour table $EBC1-$EBF2 and its
  runs ($9829 title, $9436 all, $9884 logo, $98BE/$98E9/$9916 runs,
  $994A-$999D pulses).
- `Scores` (scores.cpp): the 18 score records (two tables of 9), sort
  $BC23, best table $BC05, ranks $BC90, names $BD32, the title's block
  $BD52-$BD96 and the top name $B6AA.
- `keyboard.hpp`: the two matrix scans ($73D4 all keys, $7429 nine keys)
  and the edge latch $B254/$B255. The host maps the C64 matrix
  (`Controls::c64_keys`): letters, digits, punctuation by character,
  Return, Backspace = DEL, Tab = RUN/STOP, `]` = the up arrow.

### Flow (all measured in wizref runs)

- Entry $8F61 (title tune $1A) → $8F75 title. Title timer $96 steps of 5
  frames → menu; any stick change → menu, fire → game; SPACE → wiztips.
- Menu $906C: timer $82; up/down choose the mode $B272 (0-4, timer reset
  to $5A); fire → game; timeout → title.
- Wiztips $944D: three pages, timer 0 (= 256 steps; $921E reports "out"
  only on the step that reaches 0). Stick change → menu, fire → game,
  SPACE → next page, timeout → title. Printing a page takes 4 frames
  before the loop runs (5 for page one, whose first frame then has two
  passes: the bottom-of-screen wait $6EA8 is still satisfied).
- Game $6392 → per life $639B: set-up with the screen blanked, then the
  player screen $6893 (tune $28, fire on the port that plays), play.
  Set-up time ($6392 → $6893): 20-22 frames from the title, 12 from the
  menu; 11 frames ($6B27 → $6893) for later lives. The difference is the
  raster list left running during $75E0: the title's logo splits take CPU
  time. Modelled as those constants (tips: assumed as the menu).
- Last life: the game-over message (in Play), then $6B5B: start limit =
  furthest place − 2, start place and cheat off; $6B92 enters the scores.
  No rank → title with music ($8F61). Otherwise tune $2F, the screen
  blanked 6 frames ($6B92 → first $6CC5), then name entry per player
  ($6BFB/$6CBC): 3 letters, a rank-1 player also the wizard and feline
  names and "a few words" (10, $B6AA). Return code $3F; DEL $14.
- Keys in play: RUN/STOP pauses ($641F), Q in the pause quits to the
  title (start place and cheat off), ↑/= set the shot volume $BA81
  (0-15, starts at 10, kept between games). Title/menu: keys 0-5 pick the
  start place up to $AF7D (border flashes), the 7-key sequence at $AFCE
  sets the cheat $BB07.

### Pictures

Raster lists are data ($B416 title, $B452 menu, $B482 scores, $B4B2
player, per-page lists at $954A/$957A/$95A6); their handlers are
transcribed into per-line `Display` settings.
- Sprite colours written during line Y take effect from line Y+1.
- The tips' sprite records ($94EA) are written about two lines apart
  (measured $94F6 at 191, 193, 196, 198, 200, 201.5): a sprite still being
  shown keeps its old record (page one's thrust token).
- Fonts: the letters ($F100) and the big font ($F6F0) are inverted in
  memory per screen; inverted letters show the background colour.

### Verification

- Title colour runs equal the original's ticks (frame f ↔ tick f+1);
  title pictures differ only in the logo's badline lines (67, 75) and
  raster-race rows (700-2500 px).
- Wiztips pages 1-3: 1 differing pixel each (frames 200/400/600).
- Menu: 647 px (random stars, colour phase).
- Score entry: 45 px and ~200 px (frames 9540/9745), all in colour-run
  phases carried over from play (life lengths differ in scripted runs,
  see below).
- Pause/quit: quit lands on the same frame as the original; volume keys
  step $BA81 as the original (0A → 0B → 0C → 0B).
- Player modes: modes 1-4 compared pass by pass in play ("Play"); the
  player screens' texts per mode equal the original's pictures (player
  one/two, wizard, team one wizard / player two, team one/two wizard).
- Play lockstep runs after these changes: die 7690/7690, fight1
  3703/3703, clr3 9118/9118, round 6590/6590.
