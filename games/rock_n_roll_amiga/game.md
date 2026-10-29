# Rock 'n Roll (Amiga) — how the original works

What was learned while rebuilding Rock 'n Roll (Rainbow Arts, 1989): the disk,
how data is stored and encoded, how the program runs, the rules of play, and
the original's own bugs. The remaining work is in [plan.md](plan.md).

## Sources and evidence

- **Disk image:** `Rock'n Roll (1989)(Rainbow Arts)[cr PNA].adf` (901,120
  bytes).
  - SHA-256 `1d89a8e7b51a3f76a20be9f78229f46c6cac1339f4135d1e5078b1a228615e60`.
  - It is a crack by "PNA" ("Paranoimia", per its intro) that puts the
    two-disk original on one disk. The "INSERT DISK 2" texts remain in the
    program.
- **Method:** everything here comes from this image and from running it in
  FS-UAE (Kickstart 1.3, A500).
  - No outside material on this game was used; RetroReverse has nothing on
    it.
  - Before this work, only the title, publisher and composer were known by
    name, not the internals.
  - TFMX is a general music format. Its player will be rebuilt from the
    copy inside the program.
- **Addresses:** `$xxxxx` is a location in the running program. Main is
  unpacked to $1A578; the flat image from 0 is `work/M.bin`. Game files are
  numbered in hex.
- **Local evidence:** kept outside the repository, in
  `.analysis/rock_n_roll_amiga/`:
  - `harness/`: the FS-UAE reference machine and the recorder;
  - `runs/`: the recordings;
  - `work/`: unpacked files and the Ghidra decompile `work/all.c`;
  - `tools/`: the research unpacker and render scripts.

  The flow of `$6858A` is hard to follow in Ghidra; check the instructions.
- **"Update":** one pass of the in-level loop, which is one PAL frame.
- **Checked vs read:** statements marked *checked* were compared with the
  running original. The rest are read from the code, and **open** marks
  hypotheses.

## Disk and boot

- **File system:** AmigaDOS OFS, volume `Rock&Roll`. `s/startup-sequence`
  runs `intro`. Files:
  - `intro`: the crack intro, a hunk file;
  - `Main`: the game, a hunk file of 156,792 bytes;
  - `devs/System-Configuration` and `Disk.info`;
  - 63 numbered game files, `00`–`41`.
- **Cruncher:**
  - `Main`'s code hunk starts with an unpacking stub (+$0..$E6). It unpacks
    the stream at +$E8 to $1A578 (length $61800, up to $7BD78) and jumps to
    $68166. A failed checksum flashes the background.
  - The numbered files use the same format, with a 12-byte header: packed
    length, unpacked length, XOR checksum. Only file `35` is stored raw.
  - Everything unpacks with good checksums.
  - Code: `import/cruncher.cpp`; research copy `tools/bk.py`.
- **Boot:**
  1. `intro` (also crunched) unpacks to $50000. It calls the demo
     (`$5334C`) and copies the crack's file loader $50200..$51200 to $A000.
     Then it loads `Main` by name to $10000 and jumps into Main's stub.
  2. Main takes over the hardware and makes no OS calls. Its loader glue
     (`$7103A`, `$7108E`) disables interrupts and calls `jsr $A000` with the
     file number in `$70DCC`.
  3. The crack's loader builds the file name from two hex digits of the
     number, except $3D → "33" and $3E → "39".
     - The destination is the long at `$78136` + n×8 + 4. The file is
       unpacked from destination − $500.
     - File $35 is not unpacked.
     - Number $30 first goes through the game's own `$71554` (the
       disk-2 check, original code).
- *Checked:* the program in memory at its entry ($68166) equals the
  unpacked image byte for byte.

### Load table `$78136` (8 bytes per file number)

| Files | Address | Contents |
| --- | --- | --- |
| 00–20 | $737C0 | Level maps (33 levels) |
| 21, 23, …, 2F | $18F46 | World graphics (ILBM and more); 2F is the title |
| 22, 24, …, 2E | $762B8 | 6,144 bytes, equal to the last 6,144 bytes of the world file |
| 30 | $762B8 | The high-score table (see "High scores") |
| 31 | $5AEA6 | 12,288 bytes into display buffer 1 (not decoded) |
| 32, 34, …, 3C | $5740E | TFMX song data |
| 33, 35, …, 3B, 3D, 3E | $4B976 / $53976 | TFMX samples |
| 3F | $1A578 | Main itself |
| 40 | $10360 | Three TFMX song sets: title $10360, $13240, $16120 (copied by `$70E40`) |
| 41 | $18F46 | The ending picture ("YOU MADE IT") |

## Data formats

### Program tables

- **World table** `$70E94`: 8 × 4 longs per world (graphics, extra, songs,
  samples). Worlds 1–7 are the level worlds; world 8 is the title.
  - **Open:** world 3's songs and samples are both file $3C. Does it keep
    the previous samples?
- **Level table** `$7855E`: per level, a long file number and a long world
  (read by `$6C6A8` with the level index `$719AE`). 33 levels.
- **Level names** `$6AEFC`: 16 bytes each, 14 characters plus $FF $00. The
  list ends with "NO ", the hourglass text for no time limit.
- **Texts:**
  - messages from `$676AC`, and warp hints;
  - jukebox titles from `$68208`, high-score texts from `$69044`;
  - the four prompt lines after a fall at `$6816E`, "OUT OF TIME" at
    `$681E8`;
  - the keyboard map at `$70C9C`.

  The text format is a column byte, a row byte (8 lines a row), then
  characters up to $FF.

### Level files (00–20)

- **+$000..+$3C7: the header.**
  - +$1BE: the warp countdown, word ($FFFF = none).
  - +$1C0: the hourglass text, 4 characters ($FFFFFFFF = "NO ").
  - +$1C4: a long; its low word +$1C6 is the time limit in seconds
    ($FFFF = none).
  - +$1C8: the reward countdown, word.
  - +$1CA: 9 bytes, the 3 × 3 cells a bomb leaves.
  - +$1D4: the refill value, a byte. The set-up reads the word and keeps
    its low byte, +$1D5.
  - +$1D8/+$1DC: the crumble countdown and list.
  - +$1DA/+$1FA: the timed list of switches $2D/$2E.
  - +$218, +$254, +$290, +$2CC, +$308, +$344: the cell-change lists of
    switches $29–$2E. Each is 60 bytes of entries: x word, y word, two
    value bytes (the second wins); $FFFF ends.
  - +$380: eight teleport points for $22; +$3C0/+$3C4 is the $23 target.
- **+$3C8 word:** rows. **+$3CA word:** columns (the row stride).
- **+$3CC..+$3E7: settings.**
  - +$3D0/+$3D4: the camera limits.
  - +$3D8/+$3DC: the camera's start.
  - +$3E0/+$3E4: the ball's start place in the view.
- **+$3E8: the cells**, one byte each, row by row. No file stores 0 or
  $F1–$FF; those are run-time markers.
- **Level 28** (file $1B) is 60 rows × 100 columns, which needs 7,000
  bytes; the file has 6,144.
  - The game reads on into memory behind the file. After start-up that
    memory is all $21 (abyss); after a larger level was loaded, it holds
    that level's tail.
  - **Open:** what the view shows at the bottom edge.

### Tiles and the graphics bank

- **The bank** starts at $19000: 200 bytes a row, 5 planes × 40 bytes,
  interleaved.
  - A world file loads at $18F46, so its ILBM BODY lands at $19000. The BODY
    is 320×200×5, uncompressed; its CMAP holds the world's 32 colours.
  - The rest of the file (27,398 bytes) continues the bank: rows 200–336
    hold animation frames and items.
- **From row 344 ($29CC0) on**, the pictures come from inside Main:
  - the panel (row 344) and the name bar ($2CEC0, row 408);
  - the abyss pattern (about rows 425–455) and the ball frames (row 456);
  - item pictures and the balls column (row 584), the font (rows 600–615).

  Rows 616–621 are not pictures. The TFMX player is at $37EE0.
- **Palette:** `$68510` sets the colour registers from file +$30, using the
  high nibbles of the CMAP bytes.
- **Tile table** `$72CBE` (241 longs, offsets into the bank), built by
  `$70954`. Entry v (1–240) is sheet cell v−1, 20 per row, 16×16.
  Offset = row·3200 + column·2.
- **Abyss parallax:** `$6C972` re-points $14 and $21 each update.
  - Offset = $157C0 + ((x>>14)&15)·2 − ((y>>14)&15)·200, with x,y the
    camera.
  - *Checked* against a live table value.
- **Railings** are the tile pixels with colour bit 4. They are thin lines;
  floor and abyss have none.

### Tile animations (`$6C106`, `$6C004`)

Both run every update in the level loop, after the clock and before the
mouse is read; not in the start wait.

- **The records:** 18 slots at `$6C08C`, each pointing at a record:
  - +0: the address of a picture strip in the bank;
  - +4: the period; +6: the counter; +$A: the step;
  - +8: the cell value whose tile-table entry it sets;
  - +$C: one word per 256 of the counter, the frame's byte offset into the
    strip.
- **`$6C106`:** the slot cursor `$6C08A` moves on three slots each update
  (0–17, round), so each slot has its turn every sixth update.
  - On its turn: counter += step, and back to 0 when it reaches the period
    (a signed compare).
  - The cell's entry becomes strip − $19000 + frame[counter >> 8].
  - Slot 17 (cell $51) is set at each level's set-up (`$6C0E0`). It is
    `$7196A`, or the empty `$7197E` in world 6, which is skipped.
- **The animated cells:**

  | Record | Cell | What | Period / step |
  | --- | --- | --- | --- |
  | `$715D4` | $11 coin | coins turning | $C00 / $100 |
  | `$715FA`, `$71620` | $12, $13 | (overwritten by `$6C004`) | |
  | `$71646` | $DA flames | flames | $D00 / $12C |
  | `$7166C` | $22 teleport | pulsing | $2000 / $160 |
  | `$716B8`, `$716FA` | $A9, $AA | gates | $1B00, $1900 / $100 |
  | `$71738`, `$71778` | $A2, $A5 | gates | $1A00, $1900 / $100 |
  | `$717B6`, `$717CE` | $2F, $45 | shops | $600 / $100 |
  | `$717E6` | $D9 mud | | $600 / $100 |
  | `$717FE`, `$7184A`, `$71898`, `$718E4` | $AE, $B1, $B5, $B6 | arcs | about $2000 / $100 |
  | `$7192E` | $B9 | arcs | $1800 / $100 |
  | `$7196A` | $51 | a 2 × 2 picture | $400 / $160 |

- **`$6C004`, after them:**
  - $BC shows $B9's frame + 6 bytes (three tiles right);
  - $52, $65, $66 show $51's + 8, 16, 24 bytes, except in world 6. These
    are the four pieces of a ventilator; each piece's strip of four frames
    is 64 pixels wide. ($65 is also a slope value; level 1 is in world 6,
    so its cells keep their sheet pictures.)
  - $12 and $13 show $11's frame, and while $11's counter is 0 all three
    show their sheet pictures (offsets $20, $22, $24).
- **Counters run on.** They start at 0 with the program, run only in level
  updates, and are never reset. The tile table is rebuilt at every level
  set-up (`$70954`), so a cell shows its sheet picture until its slot's
  next turn.
- **Pictures decide passage.** The contact test reads the drawn view, so
  an animated gate's or arc's frame decides where the ball can pass. A
  gate is a wall while its closed frame shows. *Checked:* gate3 (the ball
  pushed left through the $AA gate in level 1), 399 updates equal.
- **The rules that read counters:**
  - $DA (`$6D6E2`): −$C0 while counter ≥ $600. A bomb's flames set the
    counter to $800 (`$6BFE6`), the start of its burning frames.
  - $B9/$BC (`$6D640`, $B9's counter): −$40 while ≤ $100.
  - $AE/$B1/$B5/$B6 (`$6D680`…): −$C0 while ≤ $1100.
  - $A2/$A5/$A9/$AA (`$6D738`…): −$140 when the counter is 0 or $100,
    with sound $C and a sound delay of $28.
  - All of these act on the centre cell only, at most once per drain
    period (`$6C840`), halved with the protective ball.
  - `$6E254` (set by these hits and by purchases) only keeps `$6F466`
    from also playing the energy-loss sound $11.
- **Unreferenced:** `$6E35C` (sink into mud via `$6C378` while the $D9
  counter < $7D0) is never called.

### High scores (file $30 at `$762B8`)

- **Tables:** one per level index (0–31) at `$762B8` + index × 128, and the
  hall of fame at index 32 (`$772B8`). The bonus level ($20) has no
  records.
- **Entries:** 16 bytes each.
  - +0 is the rank character, never written.
  - +1..+$C is the name, 12 bytes, with '(' as a space.
  - +$D..+$F is a 24-bit value; the program reads the long at +$C and
    masks it.
- **Levels** use 5 places of their 8, with times as seconds << 8 |
  hundredths. The hall of fame uses 10 places (160 bytes), with scores.
- **High score:** the first hall-of-fame score (`$772C4` & $FFFFFF), which
  the panel shows.
- **Shipped values:**
  - every level's records are "DYNAMIC" 31:19.9 down to 31:19.5;
  - the hall of fame is "DYNAMIC" 10000 down to 1000;
  - scores show with an extra '0' (so 10000 shows as "100000").
- **Writing back:** a new entry is written to disk at once (`$710D0`).
  The port keeps it in `scores.bin` beside the installed files. The
  imported file stays as it was.

## Machine use and the main loop

- **Interrupts:** none except level 2, the keyboard (`$70C3C` → vector
  $68).
  - The handler writes the key's character to `$70BD8`, and '$' on release.
  - The table is `$70C9E` (German layout): Return is '[', space '(' and
    backspace '''.
- **Pacing:** the game polls the beam. `$70BA2` waits for line $10, `$70BB4`
  for $FF, and `$70BC6` for the blitter.
  - **One update per frame (50 Hz)**. *Checked:* 507 loop passes in 10.1 s
    of real-time FS-UAE.
- **Display:** two buffers `$5AEA6`/`$612A6` (5 planes × 40 bytes,
  interleaved), swapped by `$6FA5E`.
  - Two copper lists:
    - `$73278` shows 320 × 200 from the bank (the title, the continue
      screen, the ending; `$6993A`).
    - Level set-up switches COP1LC to `$73084` (`$69838`), inside the play
      screen's list described under "The screen" (from `$73040`).
- **Start-up** (`$68166` → `$6858A`): bitplane pointers, DMA and copper;
  music (`$70A74`, TFMX at $37EE0); file $40 loaded.
  - `$68B88` is a disk-2 check that does nothing in this version: it
    returns after its first call.
  - Then each game starts at `$6867A`. See "Front end" for the title and
    name entry.
- **Level start:**
  - The level index is `$719AE` (0-based); `$6C69E`/`$6C6A8` pick the file
    and world.
  - It loads the world (`$70F96`, `$70FB8`) and the map (`$70DD0`).
  - The game's own disk routine `$710D0` runs here unless `$7198E` bit 0
    is set. It is the routine that writes the score file back after a new
    record (`$6A6F2`); that it writes here too is read from the call only.
- **Level set-up** (`$686E2`):
  - the camera from +$3D8/+$3DC, the ball's place in the view from
    +$3E0/+$3E4;
  - the tile table;
  - the panel and object set-ups (`$6EE58` … `$6F4FC`).
- **Start wait** (`$688B4`): the mouse updates the velocity, but the camera
  stays still until a speed with bits 12–15 = 1 appears ($1000 = one pixel
  per update). Then the clock starts.
- **The in-level loop** `$68940`, each update in this order:

  `$70AE4` music, `$6B166` bought balls, `$6B13A`, `$6F466` shown energy,
  `$6C84C` clock, `$6C106` tile animation, `$6C004`, **`$7040C` mouse →
  velocity and camera**, `$703B4` halving, `$70BC6`, **`$707AA` keep in
  view**, `$6B4AC` centre cell, `$6C972` abyss, `$6CFC6` cells under the
  ball, `$6C24E` ball state, `$6F710`, **`$6FAA0` tiles**, `$6FCC0`,
  `$6F666`/`$6F60A`/`$6FDEE` contact test, `$700E8` bounce, `$6FCCC`,
  `$6B372` moving objects, `$6F7B6` ball blit and continue point, `$6BE8A`
  bomb, `$6ACE6`, `$6ABF0` speed clamp, `$6F8FC`, `$6F090` panel, `$68AEA`,
  fine scroll, `$70BA2`, `$6FD9A` display start. The items (`$68A02`) are
  handled at the end.
- It leaves through `$68A50` when the ball is lost or the level ends.

## Steering and movement

### Position model

- **Camera** `$71A30`/`$71A34`: the view's top-left in tiles, as 16.16
  (1 tile = $10000 = 16 pixels, so $1000 = 1 pixel).
- **The ball's place in the view** `$71A38`/`$71A3C`, also 16.16 tiles.
- **Velocity** `$71A48` (x) / `$71A4C` (y), per update.
- **Keep in view** (`$707AA`): while the ball is off the view's centre (x
  $90000, y $30000), or the camera is at a limit (0 or `$71A50`/`$71A54`),
  the camera stays and the ball moves in the view instead. So the camera
  follows the ball once the ball reaches the centre.
  - The ball is held within x $20000..$100000 and y 0..$60000; at those
    limits it bounces.
  - For y: if view_y ≥ $30000 and camera_y > 0, then:
    - if view_y ≤ $30000 (unsigned) and camera_y < limit, view_y = $30000;
    - otherwise camera_y = limit and view_y += vy. At ≥ $60000 the ball is
      clamped and bounces; below $30000 it is set to $30000.

    Otherwise camera_y = 0 and view_y += vy. At ≤ 0 it is clamped and
    bounces; above $30000 it is set to $30000.
  - For x it is the same, with $90000, $20000 and $100000.

### Mouse → velocity (`$7040C`)

The mouse is read every update, unless a gate skips the read:

1. If `$70406` ≠ 0: clear it, move the camera by the velocity, done
   (bridges use this).
2. **Grip** `$70404`: when it is clear (some surfaces clear it), the read is
   skipped except every 25th update (counter `$7040A`).
3. JOY0DAT is compared with the previous value `$719E6`. While the ball
   falls (`$6C3C6`) or after the time runs out (`$70408`), the read is taken
   as "no movement".
4. **y**: d = (prev & $FF00) − (cur & $FF00), as 16 bits (mouse up gives
   d > 0).
   - d ≥ $1AE (2 or more counts) gives accel $200; d ≥ $1E (1 count) gives
     accel 100 ($64). On ice (`$719A4` ≠ 0) the accel is $20.
   - vy −= accel, unless vy ≤ the minimum `$71A64` (−$1200 from `$6C246`).
   - The other direction works the same with (cur − prev), adding up to the
     maximum `$71A5C` (+$1200).
   - **No y movement:**
     - if `$70400` is set, x is skipped entirely (go to 6);
     - otherwise, if `$70405` is set and not on ice, friction `$71A76` (400
       at level start) applies.
   - **Friction:** vy < 0 gives vy += f, set to zero only if vy ≥ f;
     vy > 0 gives vy −= f, set to zero if vy ≤ f. So the friction is
     asymmetric (see "Quirks").
5. **x**: the same with the low byte shifted up 8, limits `$71A58`/`$71A60`
   (from `$6C242`), and `$70402` instead of `$70400`.
6. Set `$70405`, clear `$70402`, `$70400` and `$719A4`, store the
   velocities and move the camera.

The last accelerations (`$71A72`/`$71A6E`) are kept for arrows and slopes:

- $200 when the axis did not move;
- 100 for one count;
- $20 on ice, stored only when within the limit;
- unchanged when the read was skipped.

*Checked:* rec2 (2 counts left each frame: −$200 a frame up to −$2600, then
friction +400 a frame) and rec3.

### After steering

- **`$703B4`:** a contact found in the previous update halves the stored
  speed along it (ASR: rows act on y, columns on x).
- **`$707AA`** works with the speeds still in D3/D5 from `$7040C`, which
  are the speeds before the halving. It negates with `muls.w #$ffff` (low
  word only).
- **Speed limits:** ±$2600 at the start. `$6E3BE` narrows them by 200 per
  call while `$71A68` lasts (4 calls at game start). `$6ABF0` clamps both
  speeds to the limits.

## The ball and the cells under it

- **Centre cell** (`$6B4AC`): 4, −1 if the 16-bit sum of the camera_x and
  view_x low words is ≤ $A000, −2 if the y sum is ≤ $9000. This numbers the
  four cells 1 top-left … 4 bottom-right.
- **Cells under the ball** (`$6CFC6`):
  - index = (camera_y + view_y, whole tiles, 16 bits) × columns +
    (camera_x + view_x, whole tiles) − 2;
  - then +1, +columns and +columns+1 for cells 1–4.

  `$6D0FA` runs per cell: for v ≥ $50, `$6D0F8` is set, which allows the
  railing test. It jumps through `$733C4[v−1]`, with 90 handlers. Almost
  all of them act only for the centre cell (`$6B4AA`).
- **Drawing layers:** `$6D08A` swaps the four cells for markers $F1–$F4.
  - The records go to `$6FC60`: tile, cell, value, and a layer: 1 for
    v ≤ $50 or $EC, 2 for v ≥ $DC, else 0.
  - The drawer draws them by layer, then the values are put back.
    `$6FCC0` draws layers 0 and 2 (before the contact test), `$6FCB8`
    layer 1 (before the ball), and `$6FCB0` layer 2 again (after the ball).
  - So layer-1 cells count as empty in the contact test.

### Ball states (`$6C24E` → `$6C23A`)

- **Rolling** (`$6C274`):
  - The phases `$6C232`/`$6C236` += speed; on ice, the ±$1200 of the last
    push is added once.
  - The picture is bank row 456 + 16·((phase_y >> 12) & 7), column
    (phase_x >> 12) & 7.
  - `$6C258`/`$6C266` are variants with other ball pictures (the bought
    balls).
- **Falling** (`$6C3D6`), each update including the first:
  - friction `$6C3C8` ($800); the mouse is ignored (so the ball slows);
    the railing contact is off;
  - the counter `$6C3C4` += 1. It is not reset when a fall starts, only at
    level start and on continue.
  - The picture is `$30D50` + ((counter >> 2) & $FE) while that is ≤ 14.
    After that, friction 400, contact on, and the ball is lost (`$6FDEA`).
- **Fall start** (`$6CA26`): the speed is pushed out of the cell it fell
  from (bit $2000 set on the size, with signs by cell 1–4). The timer
  `$6B4F4` = 10.
- **Rescued** (`$6C488`): masks from `$32650`, one every 8 updates, then
  back to the continue point.
- **Energy out** (`$6C3CC`): the fall, with masks from `$34BD0` (the ball
  dissolves).

### Contact test and bounce

- **The test:**
  - `$6F666` blits the ball disc (`$2F450`, plane 4, shifted by the fine x
    of camera + view) AND the view buffer's plane 4 under the ball, 2 words
    × 16 rows, into `$37A30`. `$6F60A` shifts each row back.
  - `$6FDEE` sets grip := 1 and clears the counts. It only counts if
    `$6FDE8` is set, `$6FDE6` (bridge) is clear and `$6D0F8` is set.
    - Rows 0–3 count +1 vertical, rows 9–12 −1.
    - Rows 1–11 count +1 sideways for the left half, −1 for the right half.
      Rows 0 and 12 count vertical only.
  - The disc is 13 px; the picture is 16.
  - The ball is drawn into the buffer later (`$6F7B6`).
- **Bounce** (`$700E8`): a forced kind `$6FDE0`, or the signs of the counts
  (a vertical, e sideways).
  - a = 0: vx = ±1000 − vx. e = 0: vy = ±1000 − vy.
  - Diagonal: the speeds swap with signs.
    - a>0, e>0: vx = 1000−vy, vy = 1000−vx.
    - a>0, e<0: vx = −1000+vy, vy = 1000+vx.
    - a<0, e>0: vx = 1000+vy, vy = −1000+vx.
    - a<0, e<0: vx = −1000−vy, vy = −1000−vx.
  - Then grip := 0 and sound 0.

### Cell rules

| Cells | Handler | Rule |
| --- | --- | --- |
| floor (110 values) | `$6E39A` | No rule while rolling; bookkeeping of a cell to restore (`$6ECB0`) |
| $8D–$9B railings | — | Only the contact test |
| $21 abyss | `$6C9C0` | A fall (`$6CA26`); with a repair set armed, the cell takes `$6EC00` instead |
| $14 cracked abyss | `$6D510` | Only with a repair set armed: the cell is repaired and the warp countdown counts down (no reward check); otherwise nothing |
| $1D–$20 ice | | Slow (`$719A4` = 1) unless the ice-proof ball (`$719E8`) |
| $09/$0A/$0B/$0C arrows | | Up/right/down/left: 2 × the last acceleration, within the limits |
| $3D–$40 dome quarters | | Two pushes away from its middle |
| $65–$6C / $6F–$76 slopes | | Left / right, once per update (via `$70402`), by the last acceleration. The rightward one compares with the *minimum* speed |
| $01–$04 keys | | `$719EC`, `$719F8`, `$719F0`, `$719F4` +1 (up to 9), sound 3 |
| $05–$08 finds | | `$719FC`, `$71A08`, `$71A00`, `$71A04`; score `$719BA` +30/+400/+150/+70 |
| $11–$13 coins | | Money `$719C2` +100/+300/+500, at most 9,900 |
| $0D/$0E map pieces | `$6EC1C`/`$6EC3E` | Clear the other half, `$6EB22` (one more map range revealed), then themselves |
| $10 bomb spot | `$6D722` | Remembered (`$6BE86`), floor otherwise |
| $D1–$D8 locks | `$6E882`… | See "Locks" |
| $22 / $23 teleports | `$6CBF2` / `$6CB6C` | See "Teleports" |
| $24 / $26 countdowns | `$6D880` / `$6D810` | See "Countdowns and warps" |
| $DD–$E2 bridges | | See "Bridges" |
| $3B exit | `$6E992` | Level done (`$6FDEC`), the time copied (`$719D6`/`$719D2`), then a fall: the ball drops into the exit |
| shops | `$6E1CA` | See "Shops" |
| $29–$2E switches | `$6DD4C`… | See "Switches" |
| $38–$3A crumbling floor | `$6DE4A`… | `$6EC90` := $39/$3A/$21; the cell under the centre becomes the next stage when the ball leaves it (`$6EC92`/`$6EC94`) |
| $9C–$9F, $DB, $DC walls | `$6DBDC`… | Breakable with the protective ball (`$719EA`) when tick bit 0 is set and the ball moves faster than $1000 into the wall (direction by the cell's place). That speed := 0 and `$6EC64` replaces the wall (with $9D, $DB, $15, $21, …) |
| $D9 mud | | Energy −$80, both speeds halved (ASR) |
| $DA flames | `$6D6E2` | Energy −$C0 while its animation counter `$7164C` ≥ $600 |
| $B9/$BC arcs | `$6D640` | Energy −$40 while `$71934` ≤ $100 |
| $AE/$B1/$B5/$B6 arcs | `$6D680`… | Energy −$C0 while their counter ≤ $1100 |
| $A2/$A5/$A9/$AA gates | `$6D738`… | Energy −$140 at counter 0 or $100; passage by the animated picture |
| $C0 pushable block | `$6D984` | See "Pushable blocks" |
| $51/$53/$66/$68 ventilators | `$6FAA0` → `$6ACE6` | Push the ball by their place in the view (see "Ventilators") |

**Energy** is `$6F45A` (full $7FF); the panel shows `$6F458`.

- Losses are shifted right by one with the protective ball.
- A loss is taken at most once until `$6C840` is cleared (every second
  clock second).
- `$6F466` lowers the shown energy one step an update toward the energy.
  When it reaches 0, the ball dissolves.

**Refill:** a collected item leaves `$6EC00` in its cell (`$6EC02`).
`$6EC00` is then refilled from the header byte +$1D4.

### Ventilators (`$6FAA0`, `$6ACE6`)

The hints call them ventilators: 2 × 2 objects ($51 $52 / $65 $66, and
$53 … $68). Their cells have no rule of their own; the push comes from
the drawing.

- **Recording** (`$6FAA0`, the tile drawer): while it draws the 16 × 8
  view from the camera's cell, every $66, $51, $68, $53 it meets goes into
  the list at `$77F40` as two signed bytes:
  - $66: (column, row);
  - $51: (column + 1, row + 1);
  - $68 and $53: the same negated.

  The four cells under the ball are markers then, so they are left out.
- **Pushing** (`$6ACE6`, after the bomb, before the speed clamp `$6ABF0`):
  - Skipped on a bridge (`$6FDE6`, cleared here), in world 6, and unless
    the ball rolls normally or as the protective ball (state `$6C274` or
    `$6C258`; not the ice-proof ball's `$6C266`, not falling).
  - Each entry up to the first 0 is cleared and used. A negative x
    negates both values.
  - The table `$6AC56` (9 rows by view row, 8 pairs by (x − 1) / 2) gives
    signed bytes fx, fy; the speeds gain fx × 8 and fy × 4. A negative
    entry gives half that the other way.
  - A non-zero value clears the steered flag `$70405`.
- **So the push depends on where the ventilator is in the view**, not on
  where the ball is; the table assumes the ball near the view's centre.
- *Checked:* level 3 (world 3), fan1 (near $51/$66) and fan2 (near
  $53/$68), 149 updates each, the ball moved by them alone.

### Locks

- Locks $D1–$D8 take keys 1–4 by (v − $D1) mod 4 (`$6E882` key 1, `$6E94E`
  key 2, `$6E8C6` key 3, `$6E90A` key 4).
- For each of the four cells, `$6E6F0` decides whether the ball is
  "pressing".
  - $D1–$D4 (beside):
    - cell 1 needs centre 2 and an x fraction sum ≤ $CFFF;
    - cell 2 needs centre 1 and ≥ $6000;
    - cell 3 needs centre 4 and ≤ $CFFF;
    - cell 4 needs centre 3 and ≥ $6000.
  - $D5–$D8 (above or below): the same with y and centres 3, 4, 1, 2.
- With a key: the key count − 1, and the lock cell is cleared. Sound 4 is
  delayed by `$68AE6` = 50 updates.

### Shops

- **Purchase** (`$6E1CA`): the price is in the next cell ($34–$37 →
  100/300/500/900; anything else fails).
  - **With enough money** (unsigned compare): money − price, sound 18.
  - **Otherwise:** a wait counter `$6E27C` + 1. It resets to 0 when camera
    x + view x (32-bit) differs from `$6E27E`, which then takes it.
    - Below 50, it fails.
    - Otherwise d6 = (price − money) × 2 (word). More than the energy
      (signed word compare) fails. Else energy − d6 and money := 0.
  - So standing still on a shop for a second pays with energy.
- **Price cells** $34–$37 (`$6E32A`): the handler of the cell to the left
  runs, so rolling onto the price buys too.
- **One-cell shops:**
  - $33: a repair set (`$719C6`);
  - $0F/$31: a bomb (`$71A1C`);
  - $30: the protective ball;
  - $32: the ice-proof ball;
  - $2F: speed, only while `$71A6A` < `$71A6C` ($400). Each purchase widens
    the limits by $200 and adds $100 to `$71A68`.

  A bought shop's cell becomes $43, and the next one $44 via `$6EC00`.
- **Two-cell shops** (on the centre or the cell left of the centre):
  - $45: a hint (`$69DFA`, after `$6B8E6`);
  - $46: an hourglass, `$719B2` + 1 up to 9, centre only (the rescue item's
    count);
  - $47: an extra ball (`$719B6` + 1).
- **Bought balls** last $800 updates (`$6F45C`, the panel's extra bar).
  - `$6B166`, first in each update, counts `$6F4FC` down. When (time − 1)
    reaches 0, the state goes back to normal (the time stays 1).
  - The next update clears `$719EA` (protective: energy losses halved) and
    `$719E8` (ice-proof).

### Teleports

- **$22** (`$6CBF2`): `$6CC26` searches with the camera's whole-tile x,
  then x+1, for the first of eight points at +$380 whose x tile matches.
  - The camera jumps to the partner point (x + 1 tile, or − 1 tile when
    moving left).
  - The speeds become 0, the view is unchanged, and sound 9 plays. The
    sound number returned in D0 also ends the search.
- **$23** (`$6CB6C`): the same jump to +$3C0/+$3C4, with the view centred
  ($90000, $30000).
- Both set `$6F8B8` (a flash).

### Countdowns and warps

- **At set-up:** `$6D80E` from +$1BE (the warp countdown) and `$6D87E` from
  +$1C8 (the reward countdown).
- **$26** (`$6D810`, centre, unless $FFFF): the count − 1. At 0:
  - if `$6D87E` still equals +$1C8, the secret warp (`$69F8A`) happens and
    the count := $1FFF, with the cell kept;
  - otherwise the cell := $25.
- **$24** (`$6D880`): the count − 1. At 0, the reward `$6D8CE` (a per-level
  routine, table `$78008`) and the count := $1FFF. The cell := $25 always.
- **Other counters:** repair sets placed on $14, and blocks that sink into
  $14 (`$6B470`; the hints call blocks "small balls"), also count `$6D80E`
  down.
- **Rewards** (`$6D8CE`, by level index from table `$78008`). Each reward
  plays sound 5 and shows a message in the view (see "Notices"):

  | Routine | Reward | Message | Level indexes |
  | --- | --- | --- | --- |
  | `$6D8F6` | money + 500 | `$69CA6` "A BONUS OF 500 COINS." | 11 |
  | `$6D914` | score + 1,500 (shown ×10) | `$69C8A` "15.000 BONUS POINTS." | 10 |
  | `$6D932` | full energy, after the message (`$6D940`) | `$69C52` "FULL ENERGY." | 1, 8, 12, 24, 30 |
  | `$6D956` | one more ball (`$699F8` + 1) | `$69C6E` "AN EXTRA BALL." | 4, 7, 14, 21, 32 |
  | `$6D8EC` | none: colour 0 := $FFF for a moment | | all others |

  Rewards come from the $24 countdown and from the end of the $2D/$2E
  timed list (`$6ECB0`).
- **Secret warp** `$69F8A`, target by level number (with the warp text's
  digits at `$67836`):

  | Level | 1 | 2 | 3 | 4 | 6 | 7 | 14 | 27 |
  | --- | --- | --- | --- | --- | --- | --- | --- | --- |
  | Target index | $20 | $13 | $17 | $0A | $07 | $1D | $11 | $06 (+1 bomb) |

  Other levels clear `$69F86` and go on as from the exit.
- **The warp's message:** `$69F8A` writes the target's level number at
  `$67836` (inside "A SECRET WARP TO LEVEL nn") and sets the level done
  (`$6FDEC`), the loop's end (`$6FDEA`) and `$69F86`.
  - At the level's end (`$6C5F4`), `$69DC2` shows the list `$69CC2`
    ("CONGRATULATIONS / YOU HAVE ACTIVATED / A SECRET WARP TO LEVEL nn /
    KEEP UP THE GOOD WORK.").
  - Then the target level follows, without results or a time record. The
    energy gains $280 as at the exit, and the bombs are kept.
  - The list's fourth entry points at the digits themselves, so its column
    and row are the digits and it prints nothing.

### Notices (`$69EF6`, `$69F1A`)

- **In the view** (rewards, the warp): the view is cleared (`$6B8E6`) and
  the program waits for the left button's release. Then sound $18, and the
  texts of a 0-ended pointer list.
- **The wait** (`$6AB46`), as on the results: colours 1–7 $EE0, colour 1
  flashing, until a press, then the release.
- **Hints** (`$69DFA`, bought at a $45 shop): only in the levels numbered
  8, 11, 18, 20, 24, 29 and 30 (lists `$69AE6`, `$69B1A`, `$69B4E`,
  `$69B82`, `$69BB6`, `$69BEA`, `$69C1E`). Each tells how to reach that
  level by a secret warp. Elsewhere the purchase shows nothing.
  - A hint clears the whole bank (`$6B910`) and shows it through the
    title's copper list. The texts' origin is `$18FFC` + $644: x 8 ×
    column, line 8 + 8 × row.
  - The colours are whatever the registers hold (the panel's, as the play
    screen's copper list leaves them).
  - Afterwards the world's graphics are loaded again (`$69EBE` → `$70F96`).

### Bridges

- **$DD (along x) / $DE (along y), centre:**
  - `$6FDE6` is set for this update: no railing test and no continue point.
    It is cleared by `$6ACE6`.
  - The cross speed := 0, and the position along the bridge is rounded
    down to 2 pixels (& $FFFFE000, camera and view).
  - The speed := ±$2000 by sign (≥ 0 gives +).
  - slow := 1, and `$70406` := 1, so the next update only moves the camera
    and does not clear slow.
- **Ends $DF/$E0 (x) and $E1/$E2 (y):** the cross position snaps first
  (`$70B7A`). The fraction $2000 is kept; above $8000 it goes to the next
  tile, otherwise to this tile with fraction $2000. Then the same as above.
- **The last axis** (`$6D12E`): every ride along x sets it to 0, along y
  to 1. The program never resets it.
- **$E3** (`$6D326`, centre): on the bridge (`$6FDE6`), slow, and the next
  update only moves the camera (`$6D4FC`). No snap, and the speed is
  kept: a crossing.
- **Corners $E8–$EB** (`$6D36A`, `$6D3CE`, `$6D432`, `$6D496`, centre):
  - The ride goes on along the last axis until the camera's and view's
    fraction words along it add up to exactly $2000.
  - There the ball turns: the cross speed is set to ±$2000 and it rides
    the other axis (by jumping into `$6D18C`/`$6D282`).

  | Corner | Coming along x, turns | Coming along y, turns |
  | --- | --- | --- |
  | $E8 | down | left |
  | $E9 | up | right |
  | $EA | down | right |
  | $EB | up | left |

- **$E4–$E7** (`$6D33C`, centre): both speeds negated, then on into $E8's
  code (no return in between).
- **Sounds:** `$6D130`/`$6D132`/`$6D13A` only choose the bridge sounds
  ($16/$17 at the start of a ride, alternating at the ends).
- *Checked:* level 1:
  - corner1: up from the end at (40,29), the $EA corner turns the ball
    right, 159 updates;
  - corner2: right onto the end at (7,4), $E8 turns it down, $E9 at (8,15)
    turns it right, 219 updates.

### Switches

- **$29–$2E:** `$6DDF2` runs once per latch (`$6DDF0`, cleared each clock
  second).
  - The switch cell is cleared and the switch's cell-change list from the
    header is written into the map. Sound 10.
- **$2D/$2E also start a timed list:** `$6EC9A` := +$1DA, `$6EC9C` :=
  +$1FA.
  - `$6ECB0` counts it down for each abyss cell restored.
  - At 0 it runs `$6DE0E`/`$6D8CE`.

### Clock

- **`$6C84C`, every update,** while `$6C848` = $FA (normal) and the level
  is not done:
  - the hundredths `$719CE` += `$6C842` (2 per update);
  - above 99:
    - the panel time is drawn (`$6F270`, *before* the seconds count on);
    - the switch latch is cleared;
    - the tick bits `$6EC62` change: bit 2 toggles each second, and the
      energy-once flag is cleared every second second;
    - the seconds `$719CC` + 1.
  - When the seconds reach the limit `$6C846` (unsigned), the time is out.
- **Out of time** (`$6C8F8`): the mouse is ignored and the ball coasts,
  with `$6C848` counting down. When the ball stops or `$6C848` reaches 0,
  the ball is lost.
  - "OUT OF TIME" (1, 5) is shown in the cleared view for 15 delays
    (`$6ABB2`, about 1.4 s), and the music stops.
  - Then comes a restart, with no prompt.
- **Set-up** (`$6C778`): step 2, the limit from +$1C6, the hourglass text
  from +$1C0, and `$6EC9C` from +$1DC.

## Bombs and other items

- **Items** (`$68A02`, at the end of each update):
  - The right button (edge) steps the choice `$6B4F4` by 2; past 10 it goes
    back to 2. Nothing else happens that update.
  - Otherwise the latch and the repair flag are cleared, and the left
    button held uses the item (not after the level is done).
- **The items**, in their picture order (bank row 584, 16 px apart, 32-px
  cells):

  | Item | Picture | Use |
  | --- | --- | --- |
  | 0 | Exit | Give up (`$6B5A8`); the '?' key does the same at the end of any update (`$68A36`; the port maps it to the slash key) |
  | 2 | Eye | The overview map (`$6B67E`) |
  | 4 | zz | Pause until the button is let go, pressed and let go again (`$6B612`) |
  | 6 | Bomb | `$6BE9A` |
  | 8 | Pickaxe | Arms a repair set (`$6C9BE`) |
  | 10 | Parachute | Rescue: while 0 < fall count ≤ 40, with an hourglass (`$719B2`), the state goes to `$6C488` |

- **Bomb** (`$6BE9A`): works on the spot `$6BE86` (from a $10 cell under the
  centre); the spot is cleared when no button is held.
  - The timer `$6BE78` = 150, and $0F is written into the cell.
  - `$6BE8A`: at 90, 80, 60 and 30, the 3 × 3 cells become $DA (flames) and
    `$7164C` := $800.
  - At 0, the cells take the header's 9 bytes from +$1CA.

### The overview map (item 2, `$6B67E`)

- **Sequence:**
  1. The panel's time is redrawn.
  2. The view is cleared, and "CREATING MAP WITH DEPTH: nn" is printed
     (1, 0 and 26, 0; nn = `$77E62`).
  3. The program waits while the left button is held, then one delay
     (`$6B66E`).
  4. The map is drawn (`$6B77A`), then the ball's place (`$6B6FA`).
  5. Colour 1 flashes (`$6B9F4`) until a press, then the program waits
     for the release and the game goes on.
- **Display:** the view is shown with fine scroll 0 (`$7313E` := $FF).
- **The map** (origin `$6B884`): the view buffer + 4 + ((240 − columns) /
  2) / 8 bytes + ((112 − rows) / 2) lines, then 8 lines down. One pixel
  per cell, row by row.
- **Colours:**
  - The value ranges are at `$77E7D` (first value, count; 37 of them; $FF
    ends). Each range's colour is at `$77ECA` (0 = not shown).
  - The depth `$77E62` is 1 at a new game (`$6ADCC` clears the colours),
    and the maximum `$77E64` is 33.
  - A map piece (`$6EB22`) copies `$77EF2[i]` to `$77ECA[i]` for range i
    = `$77F1A[depth − 1]`, then raises the depth while it is ≤ 33. So the
    depth runs 1–34, and piece n reveals one range.
  - The depth and colours lie outside `$719C6`–`$71A23`, so they last
    across restarts and levels until the game ends.
  - "MAGIC MAP" calls `$6EB22` 33 times: depth 34, every range shown.
- **Two quirks in the drawing loop:**
  - The depth is compared (`$6B7DA`) but the result is never used, so it
    doesn't limit the drawing.
  - At a cell's first matching range the plot code reuses D0, which held
    the cell value, for the pixel's bit number (7 − column % 8). The scan
    then goes on through the later ranges with that number. So a cell
    normally gets its first range's colour only. Cells of the first range
    (values 1–4) also get the second range's colour (5–8) in columns 0–2
    of every eight.
- **The ball's place:** one pixel in colour 1 (plane 0 set, planes 1–4
  cleared). Its column is the camera's plus the view's whole tiles − 1,
  its row the same sum.
- **Speed:** every cell scans all ranges with a button test each time, so
  drawing takes about the same time per cell. Measured in level 1: nine
  rows of 70 a second, about 12.6 cells a frame. A press during drawing
  stops it; the ball's place is still shown, and the flash ends at once.
- *Checked:* MAGIC MAP in level 1, the original's view buffer read from
  memory: all 4,900 map pixels equal the rule above, including the ball's
  place.

### Pushable blocks (`$6D984`, `$6B372`)

- **The push test** (`$6D984`): the handler of $C0 for any of the four
  cells under the ball, with no centre test. It does nothing while a slide
  runs (`$6DAF4` ≠ $FF or `$6DAF8` ≠ 0).
  - It takes the speeds kept at the previous test (`$6D978`/`$6D980`),
    then keeps the current ones.
  - The axis is the one where the kept speed is larger (x on a tie).
  - It needs the previous update's contact along that axis (`$71A7E`
    sideways, `$71A7A` vertical) and a kept speed of at least $1000.
  - The direction comes from the kept speed's sign: 1 up, 2 right, 3 down,
    4 left.
  - Sound $F plays even when the way turns out to be blocked.
- **The next cell decides:**
  - $24: the block slides; it will stay a block ($6B1AC := $C0).
  - $21: the block sinks, ending as $24.
  - $14: the block sinks and counts toward the secret warp (`$69F88`).
  - anything else: no move.

  On a move, the block's cell becomes $C1 and the next one $C2 (both plain
  floor).
- **The slide** (`$6B372`, each update after the bounce):
  - The count `$6DAF4` goes 0, 2 … to $38, so the slide takes 28 updates.
  - The tile entries of $C1 and $C2 point into a strip: `$6B194` across
    (directions 2, 4) or `$6B198` along (1, 3). The frame is (count >> 1)
    & $FC, with offsets from `$6B1B2` ($C1) and `$6B252` ($C2; +$80 for
    sinking, where $FE means the abyss pattern, the $14 entry).
  - At the end: the left cell := $24, the entered cell := $C0 (or $24),
    and the $C1/$C2 entries are set to the block's and $24's pictures.
  - A block that sank into $14 counts the warp countdown `$6D80E` down
    (unless $FFFF); at 0 comes the secret warp `$69F8A`.
- **Unused effects:**
  - At count $18 the program writes $6B1AC into wherever A1 points. That
    is $37A70, just past the contact test's scratch rows (read at run
    time), so the only effect is sound $E for a sinking block.
  - `$6DBA4` is set by every push, but only the unreachable code at
    `$6C9CE` reads it.
- **State that is never reset:** the block's state sits in fixed places
  the set-up doesn't touch. A slide running when the level ends would go
  on in the next level; the port keeps it per level.
- *Checked:* level 7 (via a level code):
  - block1: a slide left, then a sink into $14 with the warp countdown
    from 2 to 1, 178 updates;
  - block2: the second block sinks and the secret warp follows on the same
    update, 47 updates;
  - block3: a slide with the whole map compared at the end, 89 updates.

## After a lost ball (`$6BB86`)

- **Balls left:** `$699F8`, 2 at game start ("3 BALLS REMAINING").
- **After a fall** (the state routine still `$6C3D6`) there are three
  delays (`$6BB9E`, about 14 frames) with the level still showing.
- **A fall with balls left:** the four prompt lines and a wait for a button.
  - **Left: continue.** `$6C5BC` restores the camera and view from the
    continue point `$6C476`.
    - The point is saved by `$6F7B6` at the end of every update while the
      fall counter is 0, so it is the last update before the fall.
    - The state goes back to rolling with the counters and flags reset and
      the speeds 0.
    - The energy gains $280 (over $800 → $7FF; `$6BDF8`).
    - The loop resumes with the map and pocket as they were, with no start
      wait.
  - **Right: restart.**
- **Any other loss** (no balls left, out of time, energy out): `$6D940`
  refills the energy (shown $800, actual $7FF), then restart.
- **Every way:** balls − 1, and the text becomes "N BALLS REMAINING" /
  "LAST BALL".
- **At −1: game over.** "GAME OVER", then the high scores (`$6993A`). The
  balls are reset to 2, `$719C6`–`$71A23` are cleared, the score and money
  are set to 0, and it goes back to the title (`$6867A`).
- **Restart:** `$719C6`–`$71A23` are cleared (keys, finds, repair sets, shop
  items; the score, money and the map's depth stay). The level is set up again,
  which reloads its file.
- *Checked:* fall4 + cont4.

## Front end (`$6867A`, `$68EA0`)

- **A new game** (`$6867A`):
  - the speed limits are set up (4 × `$6E3BE`);
  - the rescues `$719B2` := 0 and the balls `$699F8` := 2;
  - with a bought extra ball left (`$719B6`), the game continues instead
    (see "End of a game");
  - otherwise the level index := 0, the title runs, and the game starts on
    the chosen level. A code for level 33 (the bonus level) starts level 1
    (`$686C8`).
- **Title set-up** (`$68EA0`):
  - the bank is cleared and world 8 is loaded over it;
  - the score file is reloaded, and file $31 is loaded into display buffer
    1;
  - the palette comes from world 8's CMAP.
- **The title's display:** the copper list `$73278` shows 320 × 200 from
  the bank at `$19000` + `$69850`.
  - Rows 0–199 hold the title picture, with "PRESS LEFT BUTTON TO VIEW
    HIGHSCORES" and "PRESS RIGHT BUTTON TO START GAME".
  - Rows 200–335 (the rest of world 8's file) hold a framed panel: "USE LEFT
    BUTTON TO SELECT", "BY DYNAMIC VISIONS".
- **The title loop** (`$68FCA`):
  - the right button (POTGOR bit 10) goes to name entry;
  - the left button (or a finished game's result, `$68E90` bit 1) goes to
    the records.
- **Scrolling** (`$69854`): the offset moves one row (200 bytes) a frame,
  from 0 to $8FC0 (row 184) or back, so a scroll takes 184 frames.
  - Past row 135 the display's last line rises (`$732A6` = $F4 − (offset −
    $6978) / 200), so at row 184 it shows 151 lines.
- **Records** (`$69144`): first the hall of fame (table 32).
  - **Stepping** (`$69306`): holding the left button steps to the next
    table: 32 → 0 → 1 … → 31 → 32. It steps at once, and then after 17,
    15, 13 … frames, down to every frame (counters `$69041`/`$69040`, a
    period from 20 down by 2 to at least 2). Letting go resets it.
  - **Leaving:** the right button scrolls back up. The program waits for
    the left button's release before taking the title's buttons again.
- **Records texts** (`$6918A`): origin bank row 181, x 32 (`$19000` +
  $8728 + $644), in plane 2.
  - Each line is built by `$6A7BC` into the texts at `$6A110`. Place p is
    at column 4, row 5 + p: "(p" or "10", '(', the 12-byte name, '(',
    then the value.
  - **Times:** MM:SS:T (`$6F5A4`, then the tenths).
  - **Scores:** five digits without blanks, then '0' (`$6F5D6`).
  - **Headers:** "HALL OF FAME" (4, 4), or "TIMERECORDS OF LEVEL nn" (3,
    4) with rows 11–15 blanked.
  - **After a finished game,** "CONGRATULATIONS PLACE nn" (nn = "01"…"10")
    or "SORRY, NO ENTRY" is printed once over the header.
- **Name entry** (`$693D8` → `$695F0`):
  - File $31's first 60 rows (12,000 bytes, the name box) are copied over
    rows 124–183 of the title, covering its button texts.
  - The name (13 characters at `$68416`) is printed at column 7, row 14,
    and the wheel's letter at column 23, row 14 (origin `$1A900` + $644:
    row 40, x 32).
  - Each pass of the loop waits two frames (two `$70BA2` calls), so the
    wheel turns at 25 Hz.
- **Each name-entry pass:**
  1. A key (from `$70BD8`, except '$' released and '!') becomes the letter
     and is taken at once.
  2. Otherwise `$7040C` turns the wheel. From rest, two counts or more of
     x movement in a pass give a velocity of exactly ±$200, which steps one
     letter (right is forward). The y movement does nothing.
  3. The wheel runs ' (delete), ( (space), 0–9, A–Z, [ (end), and round
     again.
  4. The right button sets the letter to '['.
  5. The left button takes the letter when it is let go.
- **Taking a letter:** '[' ends; ''' deletes back. Anything else is stored
  and the cursor moves on. A 13th character is taken back at once, so a
  name holds 12.
- **The records' name** (`$71996`, copied by `$6941C`) is the first 11
  characters and a space, so a 12th character never reaches the records.
- **The name is kept** from game to game; the cursor starts at its first
  character.
- **Names that do more** (`$6941C` compares the first 11 characters):
  - "MAGIC MAP": `$6ADBC` calls the map-piece routine `$6EB22` 33 times,
    then the game starts.
  - "RAINBOW ARTS": a level code (`$694B2`). Colour 0 turns $555, the name
    becomes twelve '0's, and the same entry edits them.
    - Valid when digits 1–2 equal digits 12–11 and the level (digits 1–2,
      1–33) minus 1 equals the sum of digits 5–8 minus 1 (byte
      arithmetic).
    - A valid code sets the level and `$68454`. A bad one gives level 1.
    - Either way the name becomes RAINBOW ARTS and colour 0 $333.
  - "COUNTRY": the jukebox (`$68CD4`: nine songs, "TITLE NR.", button 0
    exits). Afterwards the name is asked again (`$719AC`).

## Results and the end of a game

- **The exit** (`$6BB3C` → `$6BDD4`): the time copied at the exit is put
  back, and the energy gains $280 (`$6BDF8`).
  - A game started from a code (`$68454`) loses its spare balls and bought
    extra balls here.
- **A secret warp** (`$6C5F4`) shows the warp text (`$69DC2`) and goes to
  its target level.
- **Otherwise, except on the bonus level:**
  - one second is taken off the clock (`$6C61E`), and the panel's time is
    redrawn;
  - the time goes into the level's records (`$6A344`), as seconds << 8 |
    hundredths.
- **Results screen** (in the play view, `$6A874`/`$6AA6A`):
  - the five records, the new one printed into plane 0 (`$6AE12` = 0)
    and the rest into plane 2;
  - "PRESS BUTTON TO CONTINUE" (3, 12);
  - with a place: "CONGRATULATIONS" (4, 1), "YOU FINISHED LEVEL nn" (2, 3)
    and "IN nTH PLACE." (9, 4);
  - without one: "SORRY, NO HIGH-SCORE ENTRY" (3, 1).
- **Waiting** (`$6AB46`): the program waits for the left button's release.
  - Colours 1–7 of the view then become $EE0 and colour 1 cycles
    (`$6B9F4`: d = ((d + 2) & $FFF) | $335, with a delay loop up to d's low
    byte, many changes a frame) until the button is pressed.
  - After the release, the colours are restored.
- **Next level** (`$6C644`):
  - index + 1. After the last level ($1F), or with the '0' key held as the
    level ends (`$6C65A`), comes the ending.
  - After the bonus level comes level 2 (index $21 → 1).
  - `$719C6`–`$71A23` are cleared (`$68B16`); bombs are kept only after a
    warp.
  - No "BALLS REMAINING" message is shown, since `$68140` is still set.
- **The ending** (`$6C7F2`): file $41 ("YOU MADE IT") is shown through the
  title's copper list until the left button is pressed. Then the game
  ends as if out of balls.
- **"N BALLS REMAINING"** (`$6882C`, at the next set-up after a lost ball,
  when `$68140` was cleared): shown for 8 × `$6B66E`.
  - `$6B66E` is a delay loop of 65,536 passes, about 92 ms or 4.6 frames.
  - The digit is '1' + balls left; with none left the text is "LAST BALL".
- **Game over** (`$6BCC8`): "GAME OVER" (5, 5) for 15 delays.
  - The score goes into the hall of fame (`$6A300`), with the place kept
    in `$68E8C` for the title.
  - Score and money become 0, and back to `$6867A`.
- **Continue after game over:** with a bought extra ball, `$69996` takes
  one.
  - It shows "CONTINUE GAME", "LEFT BUTTON / RIGHT BUTTTON", "YES / NO" on
    the cleared bank through the title's copper list.
  - It waits for the left button, then sets up the same level, with "3
    BALLS REMAINING".
  - Despite the texts, only the left button is read.

## The screen

- **The play screen's copper list** (`$73040`): 304 pixels wide (DIW
  $3D81–$FDB1, DDF $38–$D0, modulo $A0).
  - **Line $3D:** the name bar (16 lines, `$2CEC0`) with the level name and
    the hourglass text. BPLCON2 is 0, so the playfield is in front of the
    sprites where it is not colour 0.
  - **Line $4D:** the view (112 lines).
    - It shows the buffer + 2 + fine y × 200, with BPLCON1 `$7313E` =
      ~(fine x × $11).
    - BPLCON2 is $20. A single playfield takes its priority from PF2P (4:
      behind all sprites), so **the pillars cover the view's edge
      columns**, which hides the tiles drawn in as the view scrolls.
      *Checked* with an FS-UAE capture.
  - **Line $BD:** the panel (64 lines, `$29CC0`).
- **Palettes:** the copper slots at `$73140` hold the view's colours (the
  world's palette). The slots at `$731F0` hold the bar's and panel's
  colours (from `$73344`, copied at `$6854E`).
- **View x:** display x p in the view = buffer x 1 + fine x + p. Buffer x
  32–287 hold the tiles; the rest is black.
- **The view drawer** (`$6FAA0`): 16 columns × 8 tile rows from cell
  (x>>16, y>>16), by a byte pointer. Past the last column it runs into the
  next row.
  - The destination is the display buffer (`$71A86`) + 4 + column·2.
  - The first row starts at fine = (y>>12)&15; the last row draws fine+1
    lines.
  - *Checked:* level 1's start view equals the original's buffer except
    inside the ball (157 of 28,928 pixels).
- **Pillars:** eight sprites at `$71AA0` + $244·n, attached in pairs.
  - The data is at `$37140` (even) / `$3737C` (odd).
  - 143 lines from line $2E, HSTART $80, $90, $190, $1A0.
- **Ball blit:** a cookie-cut (minterm $FCA), A = mask, B = picture, 16 rows
  × 5 planes.
- **Print** (`$6AE16`/`$6AE8A`):
  - Space becomes $28. Glyphs $27–$4E are at bank row 600, $4F on at row
    608.
  - The glyph byte replaces plane 2.
  - The texts go to view buffer + $644 or to the bar (row 4: the name at
    byte 12, the hourglass text at byte 31).
- **Panel numbers** (`$6F090`, dispatched by the bits of `$6F058`):
  - **Digits:** `$6F5D6` gives 10000…1 plus a 0 byte. `$6F578` gives time
    digits (v/6000, /600, /60, /10, units); `$6F570` gives /10 and units.
  - **Drawing:** `$6F33C` copies the glyph bytes ('0' = font byte 9 +
    digit, blank = byte 1) into plane 2, 7 lines high. Leading zeros are
    blank except the last digit.
  - **Places** (panel line, x):

    | Element | Line | x |
    | --- | --- | --- |
    | Record time | 19 | minutes 8, seconds 32, tenths 48 |
    | Time | 28 | as the record |
    | High score (6 digits) | 37 | 8 |
    | Score (drawn once a find changed it) | 45 | 8 |
    | Money (4 digits) | 18 | 112 |
    | Bought speed (`$71A6A` high byte) | 24 | 216 |
    | Keys | 24 / 48 | 248 and 280 |
    | The item's count (items 6/8/10) | 48 | 216 |
    | Level number (in the bar, 3 digits) | bar row 4 | 40 |

- **Bars** (`$6F548`): a long of ones shifted by ~(v >> 6) & 31, 3 lines in
  colour bit 2, at x 112.
  - The energy is at line 48, updated every update (`$6F466`).
  - The bought-ball time is at line 28.
- **Balls left** (`$69A10`): 6 slots up from line 51, 7 lines each.
  - The ball and empty pictures are at bank row 584, x 224/256, with the
    mask at x 240.
  - They are blitted 2 pixels right.
- **Item picture** (`$6EE76`): 23 px at x 200, lines 42–55.

## Music

- The songs are TFMX ("TFMX " header) song data files with raw sample
  banks.
- The player is at $37EE0, a jump table called with offsets $24, $2C, $34
  and $60.
- Sound effects are calls to `$70A9C` with a number.
- Not read yet.

## Quirks and bugs of the original

The port keeps these, because they are part of how the game plays:

- **A $D5 lock never opens.** The pressing test returns v − $D5 as a byte,
  which is 0 ("not pressing") for $D5 itself.
- **Friction can flip a negative speed.** A negative speed is set to zero
  only if vy ≥ f, which never holds. So it can cross to positive by up to
  the friction, while a positive speed stops cleanly.
- **The rightward slope compares with the minimum speed** instead of the
  maximum.
- **The keep-in-view step uses the speeds from before the halving**, and
  negates them with `muls.w #$ffff` (low word only).
- **The ball wedges at a lock's cap.**
  - In level 1, rolling right at full speed into the orange lock at column
    57 (cap $A7 row 51, lock $D4 row 52, cap $AB row 53), the ball ends up
    overlapping the top cap and jitters there.
  - The first contact is right, on the cap's outer ring. The bounce sends
    the ball up into the narrow corner between the cap and the pipe end
    above ($9A/$90, row 50).
  - While the mouse keeps pushing right, the ball stays wedged there, drawn
    over the cap. The disc is 13 px against the 16-px picture, and the
    cap's railing outline is dotted. Any other push frees it.
  - *Checked:* runs/lockcap1.txt, 79 updates equal.
- **Breakable walls use an unsigned speed compare.**
- **The clock is drawn at each second's end before the seconds count on**,
  so it shows the second before. At a level's start it still shows the
  previous level's time.
- **Level 28's map is larger than its file** (see "Level files").
- **Layer-1 cells under the ball are not drawn yet when the contact test
  runs**, so pickups never count as railings.
- **A level finished in under one second records no time.** The second
  taken off wraps to $FFFF, which beats no record.
- **The records keep only 11 characters of a name.**
- **A level code for level 33 starts level 1.**
- **The panel's record and high score are drawn at set-up only**, so a new
  record shows from the next level on.
- **The continue screen offers "YES / NO" but reads only the left
  button.**
- **The ventilator list is never ended.** The drawer writes it from the
  start each update, and `$6ACE6` clears only what it reads. When it
  doesn't read (bridge, falling, ice-proof ball), the old entries stay and
  are used later.
- **A $66 in the view's first column records x = 0**, which ends the list
  early.
- **There are no ventilators in world 6**, which includes level 1 (and
  levels 2, 17, 19 and 20). Its $51/$66 cells are drawn without animation
  and push nothing.
- **The overview map ignores its depth while drawing**, and colours some
  cells of the first value range with the second range's colour as well
  (see "The overview map").
- **A block push plays its sound even when the way is blocked.**
- **Holding '0' as a level ends jumps to the ending** (a leftover test
  key).
- **The first update after leaving a bridge accelerates like ice.**
  `$70406` skips the read without clearing slow.

## Verification

- **The reference machine:** FS-UAE on port 6864 (`harness/run.sh`, with its
  own state directory).
  - `rnr.py boot` stops at $50000, makes the demo call return, stops at
    Main's entry and checks memory.
  - `play.py tolevel NAME` goes from the title through name entry.
  - `play.py tocode CODE` starts the level a code gives. It enters the
    name RAINBOW ARTS, then the twelve digits; for level 7 the code is
    070000070070.
- **The input patch** (research only): the game's absolute reads of JOY0DAT,
  POTGOR and CIA-A PRA (37 instructions) are pointed at `$7C100`/`$7C102`/
  `$7C105`. Keys are poked into `$70BD8`.
- **Recordings:** `play.py record N 'a-b:dx,dy[,l|r] …' OUT [COL,ROW]
  [N:ADDR=HEX]` stops at each loop top, moves the mouse counters and logs
  the state.
  - COL,ROW places the ball first. A poke can follow, such as money.
  - Newer recordings also save the original's map (`<run>.txt.map`). Since
    block1 they also save the map at the last row (`<run>.txt.endmap`),
    which the replay compares at the end.
  - The block's state and the warp flag are logged too (`blkcount`,
    `blkdir`, `blkvx`, `blkvy`, `blkfrom`, `blkto`, `blkvalue`, `blksink`,
    `warpflag`).
- **Replay:** `tools/rock_n_roll_replay ball <run>` replays a recording
  natively and compares every logged field. `harness/regress.sh` runs all
  of them.
  - 32 runs: rail1, ice1, fall2, arrow3, slope1, fall3, finds2, lock2,
    fall4+cont4, bridge1, beam2, shop3, shop4, clock1, mud1, glass1,
    switch1, exit1, crumble1, lockcap1, gate1–3, flame1, arc1, corner1–2
    (level 1), block1–3 (level 7), fan1–2 (level 3, code 030000030030).
  - 5,111 updates, all equal.
  - Since gate1, recordings also log the animation cursor and counters
    (`acur`, `ac0`–`ac17`). The replay starts from them, shows every
    slot's current frame (`TileAnimator::show_all`), and compares them
    each update. Older runs start from 0; none of them reaches an
    animated cell.
- **Headless runs of the whole game:** `tools/rock_n_roll_replay play`
  takes:
  - mouse steps `a-b:dx,dy[,l|r]`;
  - held keys `a-b:key=N` (a `GameInput::c64_keys` index);
  - pictures `@N=file.png`;
  - a ball placement `@N:place=cx,cy,vx,vy,sx,sy` (hex, as a recording's
    first row);
  - the fire button (`f` among the buttons), which types a space.
- **Front end checks against FS-UAE captures (2026-10-29):**
  - the title;
  - the hall of fame and level 1's time records;
  - the results after finishing level 1 (placed at 7,19, pushed up-left),
    whose texts and layout match. The original wrote the new record to
    its disk.
- **Gotchas:**
  - Window grabs are black until the stub has been talked to (run any
    `rnr.py regs` first).
  - Disk loads at the title overwrite the harness's input words at
    `$7C100`. Set them to idle after reaching `$68FCA`, or the right
    button reads as held and name entry starts.
  - A run that writes records changes the disk. FS-UAE keeps the writes in
    an overlay, `work/ref/fsuae-state/rnr.sdf`. Delete it and
    `work/ref/rnr.adf` afterwards so the next start has a clean disk.
  - In zsh, lists of steps kept in a variable must be split (`${=VAR}`).
    Also, the same key in back-to-back frames counts as one press.
  - The start wait needs a big enough mouse move (`play.py start`).
  - After a fall, the game waits in its prompt.
