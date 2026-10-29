# Tower of Babel (Amiga) — how the game works

Everything worth keeping from the reverse engineering of Tower of Babel v1.0
(Pete Cooke, Rainbird, 1990): how the program runs, how its data is stored
and encoded, the game's rules, and the bugs in the original. Remaining work
is in [plan.md](plan.md).

The original is hand-written 68000. It uses no operating system after boot
and keeps all its variables at fixed offsets from register A6 = $806.

- **Addresses:** all are the original's memory as the program sees it: the program at $800, the title images at $18200 and the main data at $1C5F0. Offsets into the main data are written as the program writes them, `$8200 + offset` (the program reaches $8200 as `$79FA(a6)` or `lea $8200(pc)`).
- **Variables:** "$11E4" means the variable at absolute address $11E4; "$692(a6)" means A6 + $692.
- **Hypotheses:** marked as such.

## Sources and limits

- All findings come from the user's disk image and from running it on a
  reference emulator (from 2026-10-28). No existing disassembly, port,
  symbol map or guide of this game was used. Editorial facts in the shell's
  catalogue (reviews and so on) are not evidence for this work.
- **Image:** `Tower of Babel v1.0 (1990)(Rainbird)[cr MAD].adf`, 901,120 bytes.
  - SHA-256 `0a8dff1a7c777dfe334ee0fdf8d0d196c87d1ada8f3e3549cb270c8bc8785c71`
  - MD5 `10d9a845a8cfc84fedf9d7b39664fa08`
  - A cracked copy; the boot block says "Cracked by Leaders In Crime (LIC) of M.A.D".
- **Tools:**
  - capstone 5 for disassembly;
  - Ghidra 12 headless, on a 512 KB chip memory dump taken at the main menu (302 functions);
  - Python for research renders.
- **Reference machine:** FS-UAE with a GDB stub, Kickstart 1.3, A500.
- **Evidence:** the harness scripts, recordings and dumps are kept outside the repository in the local research archive (`.analysis/tower_of_babel_amiga/`: `disk.md`, `formats.md` and `game.md` there are the working notes this document was condensed from; `harness/` the scripts).

## Disk and boot

### Layout

The disk has no AmigaDOS file system. The boot block loads raw byte ranges
with trackdisk.device; after that the game reads 512-byte blocks with its own
MFM track loader. That loader uses the standard AmigaDOS sector format, 11
sectors per track, so ADF offset = block × 512.

| Blocks | ADF bytes | Loaded to | Contents |
| --- | --- | --- | --- |
| 0–1 | 0–$3FF | — | boot block: `DOS\0`, credits text, loader code at $0C–$11F |
| — | $2C00–$F5FF | $60000 | stage 1: set-up and the packed program |
| — | $F600–$121FF | $5F800 | the crack's intro ("this game might crash…"), called from the boot block |
| $A9–$CA | $15200–$195FF | $18200 | title images: the logo (320 × 35) at $18200, the globe (160 × 133) at $197E0 |
| $CB–$333 | $19600–$667FF | $1C5F0 | main data: graphics, panel set 1, shapes, sounds, all towers; loaded after the title |
| $334–$39F | $66800–$73FFF | $23BC0 | panel sets 1–3, $4800 bytes each (the "Panel Style" option, $9F20). Set 1 equals the copy in the main data. |
| $3A0–$537 | $74000–$A6FFF | $1C5F0 | title music: one sample, 208,300 bytes used, period 176 |
| $538–$6DF | $A7000–$DBFFF | — | never read; from $540 on it is noise (7.93 bits per byte) |

**Load checks:**
- Every load is followed by a 32-bit additive checksum check and a retry ($7B78 $1EF3C5EE, $7C32 $6E256AEB, $7F98 $28B1E3CB).
- Block $AA, part of the logo, is read alone and summed ($29CF849D) to recognise the game disk: the "Waiting for Disk" check before panel loads.

### Boot chain

1. The boot block reads $2C00 bytes from $F600 to $5F800 and calls it (the crack's intro), then $CA00 bytes from $2C00 to $60000, and returns A0 = $60000.
2. At $60000 the program:
   - takes over the machine (supervisor mode, DMA and interrupts off, CIA set-up);
   - copies $C7A0 bytes from $600BE to $100 and calls them;
   - clears $18000–$80000;
   - jumps to $7A5E with the stack at $800.
3. At $100 a decruncher unpacks the program in two passes.
   - **Backward LZ:**
     - The bit stream is read from $C89C downwards in 32-bit words, least significant bit first, with a sentinel bit.
     - Output goes downwards from $4FBE + the end offset.
     - **Literal runs:** a 3-bit count; the escape 7 adds 2, 8, 12 or 15 more bits. Each literal is a 3–7 bit index into a 264-byte table at $290.
     - **Match length classes:** 2 and 3 use 9- and 10-bit offsets; then 4, 5–6 and 7–14; and 15–270 with a 10-bit offset.
   - **RLE:** from $4FBE..$19000 to $800, with escape byte $A3. `$A3 0` is a literal $A3; `$A3 n b` is n + 3 copies of b.
   - The string "DEFJAM" in the packed data is probably the packer's name (hypothesis).
4. At $7A5E the program, in order:
   - sets the interrupt vectors;
   - loads the title images;
   - calls the copy-protection hook at $17600 (the crack's stub: `moveq #0,d0; move.l #$67CCA330,$24.w`);
   - loads the title music;
   - asks for a word from the manual (page / paragraph / line / word, in English, French or German; the crack accepts any word);
   - plays the title, loads the main data, asks for the player's name, and goes to the main menu.

**Anti-tamper:** many checksum loops over the program jump around code that
would wipe memory; the crack turned their conditional branches into
`bra`. None of this is game logic.

### Memory map

- $800–$18200: program code and its data. This holds the variables at A6+, texts, menus, tables, the copper list at $3B5C and the sound lists.
- $18200–$1C5EF: the title images.
- $1C5F0–$697EF: the main data.
- $69800: the display. Four bitplanes of 320 × 200, $1F40 apart, with a second buffer $7D00 further on.

## Program structure

### Internal interface

The jump table of `bra.w` at $E1E8–$E314 is the program's internal API.

### Interrupts

- **Level 2 ($80E2), keyboard:**
  - the CIA-A keyboard is translated to ASCII by tables at $69C(a6) (unshifted, shifted +$70, +$E0);
  - the key goes to the key byte $692(a6);
  - repeat counters $693/$694 give the first repeat after $23 frames, then every 4;
  - shift and control are in $695.
- **Level 3 ($7FA8), vertical blank:**
  - mouse counters update the pointer $E/$10(a6), in hires units, clamped to 0–$27F and 0–$18F, scaled by $11D0(a6);
  - the buttons go to $69A(a6) (bit 1 left, bit 0 right);
  - the pointer sprite is positioned;
  - the hook at $12(a6) is called with (x/2, y/2, buttons); menus use it ($113E8).
- **Level 4 ($865A), audio:** segment lists and effect channels.

### Timing

- **The game's clock:** CIA-A timer B runs continuously with $1BF8 E-clock ticks (set by stage 1 at $6002A), so it interrupts about 99.07 times a second. Each interrupt adds 1 to $28(a6) ($8634). Measured on the reference: about 2 per video frame.
- **A game frame:** $87EE waits until $28(a6) ≥ 14, clears it and swaps the buffers. So a game frame lasts at least 14 ticks, 0.1413 s (about 7.1 a second). If drawing takes longer the frame is longer; the port runs at the nominal rate (no slowdown).
- **A timer second:** the play loop counts $FC0 down from 6 each frame, and the timer ticks when it wraps. So a "second" is 7 game frames (0.989 s).
- **Menus and fades** run at the video frame rate: a fade is 16 vertical blanks; the hover highlight is updated in the vertical blank.

### Screen and copper

- **Screen:** 320 × 200 in 4 planes, $1F40 apart. Buffers are at $16(a6) and +$7D00; $87EE swaps them.
- **Display:** it starts at line $2(a6) = $29 (the Setup page's "Screen Y position" moves it in steps of $18; $9BE8). Copper WAITs are relative to it.
- **Copper list modes:** $88EA builds the list at $3B5C in one of four modes, chosen by $6(a6):
  - 0: plain 16 colours (the menus);
  - 1: 16 colours, colours 7 and 8 changed at line $48;
  - 2: the game: colours 0–7 and 9–15 from the palette, colour 8 from colour 17, colour 7 changed down the sky (below), then the panel's colours from line $82;
  - 3: the title's three colour bands, changed at lines $36 and $71.
- **The palette:** the copper reads the current 48 colours at $622(a6). In the game they are the tower's 18 colours (0–17), the sky gradient (18–32, $646(a6)) and the panel's 15 (33–47, $664(a6)). A fade ($1252C) moves all 48.
- **Fades ($125B2, $1255C):** per component, start × 16 + 7, then 16 steps of (target − start), one a vertical blank; shown is the value / 16. Fade out is a fade to the black palette $25B6(a6).

### Drawing kit

- **Pens ($892(a6) + 16 × n):** two rows of four plane words. Fills ($11532, $11590) use the row of the screen line's parity; lines ($14DE6), points ($14F7A) and text use the first row only.
- **Rectangles:** fill $11532 (inclusive x0..x1, y0..y1, masks $13DF8 / $13E1C); frame $11A6C (four lines); XOR with a pen $116C8 (the menus' highlight).
- **Lines ($14DE6):** from the left end; the longer axis steps each point, the shorter when the error (starting at half the longer side) runs out.
- **Bevelled box ($11AA6):** the inside (2 pixels in) in the pen, a corner of shading in the next two pens, the frame in pen 1 and a short top-right line in pen 2.
- **Windows ($11B40 / $11B7E):** a stack of boxes that keep what was under them: whole 16-pixel columns from x0's to the one before x1 rounded up (a box ending on a column's first pixel leaves that column out), then filled in the given pen, framed in pen 15, with a line 11 pixels below the top.
- **Pictures ($11862):** 16-colour pictures, per row and per 16-pixel column the four plane words, drawn at x & ~15.
- **The panel's images ($AD50):** blitted from the panel set. The image table $303E has 8 bytes an image (offset in the set, width in words, modulo, rows); the four planes are stored one after the other. The table $3008 gives each button's place (a byte offset in a plane).

### Text

- **The font:** 256 glyphs of 9 bytes in the title data at $1C050 (width, then 8 rows). Set bits are drawn in the ink pen ($4BF(a6)), clear ones in the paper pen ($4BE(a6)), so a glyph paints its whole cell. Digits are glyphs $8A–$93; $80 is a blank, $83 / $84 an empty and a filled radio button, $7C the edit cursor, $7F ©.
- **Layout codes ($11D20):**

  | Code | Effect |
  | --- | --- |
  | $1F x.w y.w | move to (x, y) |
  | $1B x.w | move to x |
  | $1E n / $1D n | ink pen / paper pen |
  | $1C | ink 15 on paper 0 |
  | $0D | new line: x 0, y + 12 |
  | $0B / $0A | up / down 8 |
  | $09 / $08 | right / left 8 |
  | other < $20 | ignored |

- **Centring ($12058):** x = (320 − width) / 2, where the width counts every byte as a glyph, layout codes too.
- **Numbers ($11E7E–$11EEA):** 8 down to 2 digits with leading zeros, by repeated subtraction; $11C64 writes one without them.
- **Numbered texts ($11D04):** word offsets at $7138(a6) from $670A(a6). $66 + the end reason are the end screens' messages, $6C + the bomb speed the speed's name.
- **A6-relative texts** are used directly by their address (for example $4AB7(a6), the main menu).

### Menus

- **Click areas:** 10-byte records (pen, flags, x0, y0, x1, y1 in low-res pixels), ended by a negative first byte. Flags: bit 0 highlight by XOR with pen 15, bit 1 highlight by a frame in the record's pen (unlit: pen 0), bit 5 the area is everything outside the box, bit 6 lit now.
- **Choosing ($112EA):** the choice is set to −1, the vertical-blank hook ($113E8 → $11404) lights the area under the pointer and stores its number (the last one that matches); the loop waits for the left button held while a choice is set; then the highlights are cleared ($113F2: as if the pointer were far outside). Most screens first wait for the buttons to be let go ($11318).
- **Quirk:** bit 6 lives in the program's own copy of the record. Clearing highlights with the pointer "far away" leaves an outside-area lit, and it stays marked lit until the next use (nothing is drawn for those).
- **Yes/No ($110D2):** a window (76, 72)-(243, 125) with the title text, "Are You Sure?" and Yes; a click on Yes (144, 108)-(176, 119) is yes, anywhere outside the window no.
- **Radio buttons ($113C2):** a list of (x, y) words ended by a negative x; the chosen one is glyph $84.
- **The name editor ($12096):** a record (x, y, most characters, rightmost x, length, cursor, digits only, click box, text). Keys: printable characters insert at the cursor while the line ends left of the rightmost x; Delete (8) and Del ($7F); Escape clears; codes 1–4 move the cursor; Return ends. A click in the box moves the cursor, a click outside ends. The hook redraws the line with the cursor glyph in ink 3.

### Front end

**Order:** the crack's intro, the protection text, the manual's language (1–3), the word prompt, the title, the credits, the player's name, then the main menu. The port leaves out the crack, the protection and the disk loads.

- **Title ($7EF6):** copper mode 3; the title music starts; the logo (20 words × 35 at (0, 10)) and the globe (10 × 133 at (80, 61)); fade in the title palette $2498(a6). It waits until the music ends, then loads the main data.
- **Credits ($15A2E):** bands 2–3 fade to black. Then, alternating: the credits text $490D(a6) and the globe, each faded in to $24D8(a6) / $2498(a6) and shown for 100 waits of 11 ticks (about 11 s), until Space. Then everything fades to black.
- **Name ($8BD6):** a framed box (54, 76)-(265, 128) under "Please Enter Your Name:", the editor at (82, 110), at most 28 characters. No name: one of eight names from $52D1(a6) ("Fred Bloggs", …) by the CIA clock. Then all progress, the group and the score are cleared.
- **Main menu ($8D84):** title bar, boxes in rows 1–6 ($11AF6 with $7E), texts $4AB7(a6), the header (name and score in a bevelled bar, $A0DA), menu $3878(a6). Restart Game asks Yes/No and goes back to the name.
- **Game types ($8EAE):** menu $38C2(a6): Standard (not once every tower is solved, $1142(a6) = 0), Custom (menu $3818: use the current group, use the designer's contents, load a group, back), Tutorial, Main Menu. After a game type the main menu comes back.
- **Chooser ($A2DA, $A542, menu $38EC):**
  - The towers are 32 × 32 pictures at $283C0 + tower × $800 + state × $200: 0 solved, 1 closed, 2 open, 3 open and chosen. They sit at (112 + 32 × column, 50 + 32 × row).
  - **Standard:** a tower is open when a neighbour is solved (the masks at $8E52: the 3 × 3 grid's neighbours), and A, B, C are open while nothing in the group is solved. The arrows at (48, 82) and (240, 82) use one picture set ($2CDC0, states 0 off, 1 on, 2 chosen): left for any group but 0, right once 7 of the group are solved and the group is below 12. OK on an arrow moves to the next or last group.
  - **Tutorial:** all nine open. **Custom:** all not yet solved.
  - **The bar** below shows the chosen tower's name, or where an arrow leads ($4C9E / $4CB8). The group number and the percent of the 117 solved (rounded up, except 99) are shown for Standard.
  - Progress is a byte per tower ($1144(a6) standard, $11B9 custom, $11C2 tutorial): $FF solved, otherwise the count of tries (a lost tower +2 up to $63, a restart +1 up to $64).
- **Information ($A396):** four bevelled boxes: the name ("Not Named"), the author ("Anonymous"), the spiders ("Zapper, Pusher and Grabber", "No Spiders Present!") and the task ($A12A). OK plays.
- **The task ($A12A):** "Collect n Klondike(s)", " and" (more follows) or " within" (time limit), "Destroy n object(s)", "a timelimit of n second(s)", centred one under the other; with nothing to do, three lines telling the designer to fix it.

### The game screen ($969A)

- **Drawing:** fade out; copper mode 2; clear; the panel ($AC44: the panel style's 15 colours into the palette, every button); the view's frame (15, 15)-(304, 128) in pen 15; the title bar with the tower's name centred; one view; fade in the tower's 48 colours.
- **The panel's buttons:** the click areas at $31DE (27 × x0, y0, x1, y1, inclusive-exclusive), only below line $84:

  | Buttons | Meaning |
  | --- | --- |
  | 0–3 | cameras N E S W |
  | 4–6 | Zapper, Pusher, Grabber |
  | 7, 8 | forward, back |
  | 9, 10 | turn left, right |
  | 11 | Fire |
  | 12 | Pause |
  | 13–18 | Stop/In, Run/Out, Run All, Write, Delete, Clear |
  | 19–26 | the program strip |

- **Lit buttons:** each button has an unlit and a lit image (image 2 × button + lit). Lit are: the chosen camera or spider ($1554); the move under way ($1558); the turn, Fire or Pause ($155C); the program button ($1560). The strip shows the program $1564 points to: command c in place i is image (c + 19) × 2, lit at the cursor. The vertical-blank hook $AB5A redraws a light when it changes.

### The panel ($ADFC)

Polled once a game frame. It sees no clicks, only the left button held at that moment, so a held button repeats once its lockout runs out.

- **Lockouts** (game frames), counted down each poll: choosing $154C; turns and Fire $154E (at 0 the light goes off); moves $1550; programs $1552 (at 0, unless the program is being written or running alone, its light goes off and its mode becomes −1). A spider standing still clears its move light.
- **A click in the view** (inside the view's window) asks the next view for a pick ($7EC = 0): each top face drawn that changes the pixel under the pointer makes its cell's object the one picked ($DB28 / $E040). The next poll selects that object if it is a spider.
- **Choosing a camera or spider** (buttons 0–6; not the one chosen, not during the lockout): it becomes the active one; its program's lights show, the strip shows its program; lockout 4.
- **While writing a program** ($156A + 16n mode 3), choosing hands it over:
  - from a camera to a spider: the spider gets the commands and the camera's heading, and the camera's program is cleared;
  - from a spider to a camera: the camera stores a copy.
  - The writing stops; lockout 4. Choosing another spider from a spider just selects it.
- **Pause** (button 12): unless the tower is ending, its light goes on and the loop leaves for the pause menu ($14EA = 0).
- **Nothing else** works for a camera when the tower forbids cameras (flag 4), or for a spider that is gone.
- **Commands while writing:** they go in at the cursor (the rest move up, the eighth falls off); the cursor moves on; lockout 4.
- **A spider's direct commands:**
  - Forward / back (lockout $1550): start the tower; forward walks in the heading ($B510), back reverses or rides ($B4B4); the light shows; done: lockout 8 and the move is shown as under way, else lockout 1.
  - Turns (lockout $154E): left / right, lockout 4.
  - Fire: starts the tower; the spider's beam ($B66E); lockout 8, or 1 and the sound $31AA(a6) when nothing happened.
- **A camera's commands:**
  - forward / back raise and lower it by $80 (within $80..$FFF);
  - left / right move its sideways offset (+$14) by $80 (within −$1000..$1000);
  - Stop/In and Run/Out move its depth offset (+$16) by $100 (within −$3A40..−$640).
  - Its place is its offsets turned by its heading ($B44C). With linked cameras (option $1110 = 0) all four follow, each turned a quarter more, at the same height ($B478). Cameras do not fire.
- **Program buttons for a spider:**
  - Stop: the mode −1.
  - Run: starts the tower, mode 1 from the start (position −1).
  - Run All: starts the tower and all three spiders' programs.
  - Write: mode 3, the cursor at 0.
  - Delete: removes the command before the cursor and stops the writing.
  - Clear: empties the program.
  - Lockout 8 for Stop, Run, Run All, Delete and Clear; Run, Run All and Delete wait for it.
- **The strip** (buttons 19–26) puts the cursor at that place.
- **Starting:** any move, Fire or Run starts the tower ($11E6 = 0); turning, choosing and writing do not.

### The play loop ($9968)

- **Before the start:** each frame: $FC0 counts down; pens 23–24 invert ($AB44); panel; logic ($BC7A); behaviour ($BEE4); view ($D432, overlays included); READY ($151CC); swap; sounds. When the panel starts the tower, the freeze counter $11E4 goes from −1 to 0.
- **Running:** each frame: the frame counter $FBE + 1; $FC0 counts down. When it wraps (every 7 frames) and the tower has a time limit and is not ending:
  - under 30 seconds, a warning sound ($314E(a6), under 10 $317C(a6));
  - the BCD seconds $121A drop by 1;
  - at 0 the end begins with reason 2 (out of time).

  Then the same steps as before the start, without READY.
- **The end:** a win or loss sets the countdown $11E0 = $FC0 + $15 (21–27 frames) and the reason $11E2. When the countdown runs out the loop ends:
  - a won tower plays the jingle $3AF2 and waits for it;
  - otherwise the sound $3120(a6) plays.
- **No view:** when the active spider is gone, or a camera is active where cameras are forbidden, the view shows white noise instead ($D804). The window is cleared, and (lockout + 2) × $360 dots are drawn from three counters ($88A–$88E) that move on each frame.
- **Pause:** the loop leaves; the menu bar $4FD6(a6) replaces the title line (menu $3986: Setup, View, Info, Exit, and the Pause button to go on).
  - **Setup ($9AC4):** Backwards Allowed, Link Cameras, Show Compass/Info and Show Timer (Top, Base, No), and the screen's place on the monitor (ignored by the port).
  - **View ($9C68):** View Offset (left, centre, right: $FBC = (n − 1) × $30), View Height ($FB8 = n × $30 + $58), View Distance ($FBA from $9F7C: −16, $1B0, $228), Sights, Panel Style (loads panel set n from disk, $334 + $24 n).
    - Sights need the centre, normal view; choosing them sets it; the farthest distance forces the centre.
    - Closing applies the view ($9F84) and redraws the panel in its style.
  - **Info ($9FB6):** whether cameras are allowed, whether mines destroy the floor, the time bomb speed (Slow, Medium, Fast), and Show Task (the task in a window).
  - **Exit ($A060):** Abandon Tower (reason 5) or Restart Tower (6: one more try counted, the tower unpacked again), each after Yes/No.
  - Back in the tower, the title line shows the name again and the Pause light goes off.

### The data disk, saved positions and high scores

- **The data disk:** positions, score tables and tower groups live on a disk of the game's own format.
  - **Directory:** 5 blocks at block 0: a signature ("Tower Of Babel Copyright (C) Pete Cooke 1989/90", $12DE2, $3E bytes), a checksum, then from +$5C 200 entries of 12 bytes: kind (2 group, 8 position, $20 score table; bit 7 deleted), an 8-character name, block count, first block.
  - **Errors** ($80 + n): "Not a Babel Format Disk", "File Not Found", "Disk Full", …
  - **The port:** a folder beside the installed content (`saves/<kind>.<name>`), always ready, with no disk errors.
- **The requester ($126C0):** a window (64, 24)-(255, 194). The caller's title text and $56C0(a6) give "n File(s). Room for m", "Page p of q", Page ↑ / ↓, the action button and Change Disk.
  - The list shows the files of the kind, sorted by their 8 bytes, ten a page.
  - The name field is record $F7E: at (172, 110), 8 characters, '.' and space refused. It keeps its text between uses.
  - The click areas $3DA8(a6): outside cancels, the action button or Return acts, a listed name is copied into the field, Change Disk reads the directory again.
  - An error shows "Disk Error (n)" and its message ($12CFE). Saving over a file asks "File Exists / Replace File?" ($12D6C).
- **Save/Load a Position ($A6B6):** a menu of Load Game, Save Game and Main Menu. A position is $110E–$11D3: the options, the screen position, the name, the score, the group, "all solved" and every tower's progress byte. The port writes these as text lines (`tower of babel position 1`). After a load, the main menu shows the new name and score.
- **High scores ($A81E):** ten entries at $122E(a6), each a 30-byte name and a long score. The program starts with "Microprose 1" 100 down to "Microprose 10" 10. They are shown in a bevelled box, the score at x 268.
  - **$AA8C,** at the main menu and after each standard tower: the player's entry (same name) takes the current score; otherwise a score above the last entry replaces it; then a bubble sort ($AA24).
  - **Upgrade High Score Table** merges the table with a file: for a name in both, the higher score; new names are added (room for 21); sorted; the best ten are written back. The port writes them as "score name" lines.
- **Load a Group (custom game, $9144):** nine tower records ($A20 bytes) from a group file become the custom group, with its progress cleared. The port's group file is the nine 288-byte records.

### The tower designer ($E386)

**What it edits:** the designer is game type 3. It shares the game's tower variables: settings, flags $E1A, colours $2414, Klondikes and objects to destroy. So on entering it shows the values of the tower last unpacked (a game, a demonstration, or at the start the program image's). It keeps its own:
- the floor grid $195A(a6) and the object grid $1A5A(a6), 4 floors × 64 bytes (index floor × 64 + z × 8 + x). The floor grid holds −1 empty, 0 floor section, 1 supporting block, 2 lift up, 3 lift down, $28 the top of a lift up, $29 the bottom of a lift down; the object grid −1 or kinds 4–39;
- the name, author, password and time-limit text in edit records ($E7A, $ECE, $F22, $F42), whose texts are the tower's own 62/62/10-character buffers. The author starts as the player's name ($8CD4), the time text as "120";
- a group of nine tower records (the custom default group $5F512; "Use Designer Contents" in the custom game plays them);
- an undo copy (a packed record, $16480).

**Records:** $13024 unpacks a record into this form and $12E4A packs it again:
- non-printable characters become 0 ($136FE), and the characters after a text's end are kept;
- cells go through $13712 / $1371A (codes 0–6);
- objects are written in cell order, unused entries all ones;
- more than 20 objects is an error.

Unpacked and repacked, all 153 records come back byte for byte.

**The screen:**
- **Top:** a menu bar (File, Options, Toolbox, Extras), the password ("--------" when unlocked), Spiders Placed (Z P G radio marks), the kind chosen and the last message.
- **The palette:** 24 slots of 16 × 16 icons ($1C7D0 + kind × $A0). Clicking the chosen slot again steps it through its kinds ($DB8: kind, +$18 first, +$30 last).
- **The view window** (0, 12)-(122, 134): the floors up to the chosen one, each 3 pixels further up and left, 16 × 16 icons every 14 pixels. With depth cue, the lower floors use darker icons (+$2A a floor down). A cursor sprite marks the cell under the pointer.
- **Four plans of 8 × 8 icons** ($230D0 + kind × 40), row 0 at the bottom; clicking one chooses the floor.
- **Copper mode 1:** colours 7 and 8 come from colours 16 and 17 below line $48.
- **Fades are instant** ($500).

**Editing ($1098E),** in the view window, with either button held: the left button puts the chosen kind, the right takes what is there.
- **Placing a floor kind** over a lift first clears that lift's other end. Over a lift end it fails ("Can't remove lift").
  - A block can't go on floor 3 ("Can't place support"); it clears the cell's object and puts a floor above if there is none.
  - A lift up can't go on floor 3, nor a lift down on floor 0 ("Can't place lift here"). Each sets its other end and clears the object there.
- **An object** needs a floor section or a lift ("No floor for object"). There may be at most 17 besides the spiders; the one being replaced counts ("Too many objects placed"). Each spider only once ("Spider already placed").
- **Taking:** the object first, then the floor: not a lift's end, not a floor carried by a block ("Supported block, can't remove"); a lift takes its other end.
- **Painting:** with Auto Repeat on, painting floor sections continues while the button stays down.
- The messages are numbered texts $2D + n.

**Repair ($FFF2),** after scrolling, rotating, copying and swapping:
- shafts without their lift, and lifts without their shaft, become floor;
- blocks with nothing above (or a lift's end above) become floor, and a block carrying something loses its own object;
- a second copy of a spider goes, and objects past the 17th.

**The File menu:**
- **Get / Put / Swap Tower** use a 3 × 3 letter grid, the tower's name, and a Yes/No question with the letter put in.
- **Locked towers** (flag 3 clear) ask for their password before Get, Swap or Load: "Correct Password", or "Incorrect Password / Access denied". The password only protects designs; the game never asks for it.
- **Lock Tower** takes a password; Unlock clears the lock.
- **Load and Save** a group (nine records) or a tower (one record), and **Delete** them, on the data disk.
- **Prepare Disk** erases the data disk (OK, then Yes/No).
- **Exit Designer** asks first.

**Options:**
- **Effects:** star density, lightning, meteors, sky type, the tower's colour pattern (five swatches), the Sound Effects page (background sound None / Breath / Drone A / Drone B, walking sounds, other sounds) and the Moon Setup (compass heading N–W or none, the place by clicking a small sky, type 1–6, rotation).
- **Behaviour:** cameras, mines destroy the floor, time bomb speed.
- **Finishing Conditions:** time limit yes/no and its seconds (a 4-digit field), Klondikes and robots to destroy (0–6), and the current status: OK, No Task!, Not Enough Klondikes, Not Enough Objects, No Grabber, No Zapper (Not Fatal).
- **Name Tower, Tower's Author.**
- **Play/View Tower:** packs the tower and plays it as game type 3 (Exit leaves without asking). With Enemy Movement off, the freeze counter starts at −2, so after the start nothing but the spiders moves.

**Toolbox:**
- **Colours:** a page of its own:
  - the 18 colours in two rows, each colour's name (numbered texts $45 + n), its R/G/B, three rows of 16 boxes to set them;
  - 12 presets ($263A, $24 bytes each) and Restore Previous;
  - Step/Swap: swap two components of every colour; raise or lower one or all, for colours 1–12, 16 and 17 only;
  - a test of the tower.

  Colours 0, 13, 14 and 15 are fixed (mask $80073FFF).
- **Scroll Tower/Level:** left, right, up, down, wrapping round.
- **Rotate Tower/Level:** clockwise is three quarter turns. Objects that face a way turn with it (ranges at $FEAE: Zappers, Pushers, Worms, Prisms, Lizards, Convertors).
- **Copy Level, Swap Level** with another floor.
- **Clear Level:** objects go; floors go unless a block carries them; lifts go with their other ends. **Clear All:** also the texts and the lock.

**Extras:**
- **Designer Setup:** Depth Cue, Enemy Movement, Auto Repeat, Confirm Replace (when off, saving over a file doesn't ask), Bright Palette (designer colour 10: $777 or $555).
- **Statistics:** percent of cells used; Zappers, Pushers, Klondikes, others, total.
- **Support Check:** from every ground-floor cell over neighbouring cells, and up through blocks. Each cell reached is marked on the plans in turn; Passed if every cell was reached.
- **Spider Path Check:** for each spider, from its cell over floors (not blocks), up and down lifts, with marks coloured by the spiders that reach them. "Not Found" for a spider not placed. Passed if every walkable cell was reached.
- **Undo:** the tower and the undo copy change places. The copy is taken by Get, Load, and every Toolbox change.
- **Mouse Scaling and Mouse Style:** kept, without effect on the host's pointer.

**Menus:** each page's window closes when clicked outside. The menu under it stays open unless the command finished something ($E0E bit 3): File's Get, Put, Swap, Lock, Unlock and Exit; a done Toolbox command; Setup; a done Undo.

### After a tower

- **Solved (standard game, $92EC):**
  - **The bonuses:** the tower bonus 100, the retry bonus 100 − 4 × tries (at least 0), and the survivor bonus 10 for each spider the tower has that is still there. The tower's byte becomes $FF.
  - **The screen:** a demonstration runs above (interlude n mod 6 with a counter $FD6 that rises each time), with a bevelled box in pen 6, "Tower Completed" and the three bonuses.
  - **Counting:** after 4 frames each bonus is counted into the score in chunks of 20 (halved when the rest is smaller), a chunk a game frame, with 3 frames between bonuses.
  - Then OK. In group 0 the program checks the disk once 7 towers are solved (left out).
- **All 117 solved ($95A0):** "Unbelievable! Amazing! …" and the final score; Standard is closed from then on.
- **Not solved, or another game type ($97AA):** a demonstration (won: 0–5, lost: 6–11) with a box in pen 6 or 3 and the message $66 + reason ("Tower Completed / Congratulations", "Out Of Time", "All Spiders Destroyed", "Grabber Destroyed", "Attempt Abandoned"); then OK.
- **The OK button** lights (XOR over (136, 184)-(183, 195)) while the pointer is on it ($98B6); the demonstration runs on until it is clicked.

### Demonstrations ($158AA, $15922)

- **Setup:** the 18 towers of the demonstration group. Each is unpacked as game type 5 and starts at once (the clock running); the camera is fixed facing north.
- **Each game frame:**
  - the turn and move lockouts are cleared;
  - when the active spider stands still, its script's next byte ($FE4(a6) + 16 × demo) is read;
  - low 3 bits: 0 nothing, 1–5 the buttons forward, back, left, right, Fire (through the panel's command code, $AF3C), 6 a jump on the spot (progress 16, state 12). A 0 byte ends the script.
- **The camera:**
  - x = spider x × $CD / 256 and y = spider y × $140 / 256 + h, where h starts at $120;
  - z = spider z − 8 h;
  - h changes each frame by 2 × (the last byte as signed >> 6).
- **No overlays** are drawn (game type ≥ 4). The panel band under a demonstration takes the menu colours $23F2(a6).
- **Demo Sequence ($15780):** demonstrations 12–17 in turn, with the logo at (0, $90) below the view.
  - A click in the view's left half goes back one, in the right half on one; a click below line $80 or Space ends.
  - When a script ends, the next demonstration starts.

### Randomness

- The seed is $890(a6), a 16-bit LCG, r × $5E5 + $29, at $135CA. One call site adds $2A instead.
- Menus and the protection read the CIA time-of-day counter ($BFD800). The port uses its own random numbers there.

## Data formats

### Pictures ($11862)

- **Format:** 16 colours in four bitplanes interleaved by word. For each row and each 16-pixel column come the words of planes 0 to 3.
- **Drawing:** $11862 takes (x, y, width in words, height) and draws into the current buffer's planes.
- **Title pictures:**
  - the logo: 20 words × 35 rows at (0, 10);
  - the globe: 10 words × 133 rows at (80, 61).
- **Title palette:** 48 colours at $2C9E ($2498(a6)), faded in by $1252C. Copper mode 3 switches the 16-colour bands at screen lines $36 and $71.
- **Menu palettes:** use the same 48-word form ($23F0(a6); hypothesis from the calls at $8C02/$8E0A).

### Towers ($13278)

A tower is a 288-byte record ($120 bytes). It is a bit stream read most
significant bit first through 32-bit words ($1390A). 2,269 of its 2,304
bits are used.

| Bits | Into | Meaning |
| --- | --- | --- |
| 62 × 7 | $D22 | name (7-bit text; a non-printable character ends it) |
| 62 × 7 | $D60 | author |
| 10 × 7 | $D9E | lock password ("lebab", "tivros", …; empty = unlocked) |
| 14 | $E18 | time limit in seconds (shown as 4 digits) |
| 10 × 1 | $E1A.. | flags, stored as 0 / −1 words (see below) |
| 2 | $E2E | time bomb speed (Slow, Med, Fast; added + 1 to the count each frame) |
| 3, 3 | $E30, $E32 | Klondikes to collect, objects to destroy (the objective) |
| 2, 3, 2, 3, 3 | $E34–$E3C | star density (none, low, med, high), the colour pattern (0–4), sky type (Fade A, Fade B, Bars), the moon's heading (N, E, S, W, none), its type (1–6) |
| 9 | $E3E | the moon's x (16 + 16 × value) |
| 7 | $E40 | the moon's y (16 + value; seen only when ≤ $37) |
| 2 | $E42 | the moon's rotation (clockwise, none, anticlockwise) |
| 4 × 8 × 8 × 3 | $1CC9.. | cells (below) |
| 20 × (6 + 8) | objects | kind and position (below) |
| 18 × 12 | $2414 | colours, 4-bit R, G, B |
| 2, 1, 1 | $A4–$A8(a6) | the background sound (none, Breath, Drone A, Drone B), no walking sounds, no other sounds |

**Flags:**
- 0–2: the spider is absent (Zapper, Pusher, Grabber); $1362A selects the first one present.
- 5: no time limit ($A1B4/$A22E).
- 6: no lightning.
- 7: no meteors.
- 3: unlocked (clear: the designer asks for the password);
- 4: cameras may not be used ($E22: the panel ignores them, the view shows noise; Info says "Cameras not allowed").
- 8: Proximity Mines leave no hole ($E2A; Info: "Mines don't destroy floor").
- 9 is open.

**Cells:** floor by floor, then row (z), then column (x). Each 3-bit code
becomes a cell byte by the table $1375A: 00 C0 C8 E0 F0 30 E8 00. The bytes
go into a 10 × 10 grid per floor with a border of non-floor cells.

**Objects:** each has a 6-bit kind and an 8-bit position (bits 0–2 x, 3–5 z,
6–7 floor). Only kinds 4–39 are placed.
- Kinds 4–6 are the player's spiders and go to slots 4–6 ($15DA + kind × 32).
- Others fill slots from $16BA.
- The kind table at $13742 has 10-byte entries: a type word, a long shape offset from $18200, a word, and a flags word.

**Colours:** the first 16 are the view's palette. Colour 16 is "Floor Lit" (the horizon line), 17 "Floor Unlit" (ground shadows). Two more are the sky's top and bottom.

**Object names:** the designer's list at $6F10 holds 46 names:
- Floor Section, Supporting Block, Lift Up, Lift Down;
- Player's Zapper, Pusher and Grabber;
- Zapper and Pusher facing N, E, S and W;
- Rotating Zapper and Pusher;
- Worm, Watcher, Hopper, Proximity Mine, Time Bomb;
- Reflector, Prisms, Lizards, Klondike, Bonus Flag, Convertors, Forcers, Freezer, Exchanger.

The numbered texts $00–$27 are the kinds' names in kind order (4 Player's Zapper … $27 Exchanger), the designer's message n for kind n.

**Tower groups in the main data:**

| Address | Towers | Group |
| --- | --- | --- |
| $61372 | 13 × 9 | Standard, Groups 00–12 ($A20 bytes a group; $91C0) |
| $5FF32 | 9 | Tutorial ($8F2C) |
| $5E0D2 | 18 | demonstrations and interludes ("Zapper Demo", "Pusher Dies 1", …; $158C6) |
| $5F512 | 9 | the Custom group's empty default (copied to $60952 by $9046) |

All 153 towers decode to plausible names and authors (Pete Cooke, Chris
Newcombe, Tim Roberts, "Legin Drawnek", "Axeman Towers Ltd.").

### Cell bytes

| Bit | Meaning |
| --- | --- |
| 7 | solid: a floor slab or a block, something to stand on |
| 6 | opaque: hides the faces of its neighbours |
| 5 | lift shaft |
| 4 | lift in its up position; light passes down the shaft ($CCCA) |
| 3 | block: full height up to the slab above; stops walks and beams |
| 2 | lit: nothing above it. Lit tops use the lit pen and show shadows; the bit passes down when a floor is removed |
| 1 | drawn in the second square colours (the colour pattern $CD90) |
| 0 | border: outside the 8 × 8 tower |

- **Addressing:** a step in a compass direction is +10, +1, −10 or −1 (table $23E4; heading 0 north, 1 east, 2 south, 3 west), and a floor up is +100.
- **The ground:** floor −1 sits below floor 0 and carries only the lit flag.
- **The object map:** follows the cells at $1E4E, in the same 10 × 10 × 4 layout. It holds the slot standing in each cell (−1 none).

### Objects ($15DA + 32 × slot)

There are 52 slots. The ground's cells follow slot 51, so a lit bit passed
to the ground shows up as "slots" 52–54. Slots 4–6 are the spiders; slots
0–3 hold the four cameras.

| Offset | Meaning |
| --- | --- |
| +$00 | type (kind table word 0); ≥ $1A gone or exploding; negative ends the list |
| +$02 | the shape shown (a main-data address) |
| +$06, +$08, +$0A | position x, y, z (a cell is $200 units) |
| +$0C | heading |
| +$0E | move kind: 0–3 walk, 4–7 pushed, 8–11 lift; higher values per type |
| +$10 | frames left of the move (0 = standing; nonzero = between cells) |
| +$12 | what beams do to it (below) |
| +$14 | the cell it stands in |
| +$16 | the cell it is moving into (0 none) |
| +$18 | its own slot |
| +$1A, +$1C, +$1E | counters, by type |

**Flags (+$12):**
- Low byte, bit by bit:
  - 0: destroyed by a zap;
  - 1: reflects;
  - 2: pushable;
  - 3: grabbable (Klondike);
  - 4: convertor;
  - 5: prism;
  - 6: forcer;
  - 7: Freezer, Wiper or Exchanger.
- High byte:
  - bit 0: Klondike taken;
  - bit 1: the second particle cloud;
  - bit 2: the floor under a cloud goes (a mine's hole);
  - bit 3: a zap reverses its move.
- Seen values: spiders $0005, Klondike $0209, block $0004, enemy Zapper $0001.

**Programs ($156A + 16 × slot):**

| Offset | Meaning |
| --- | --- |
| +0 | the move under way: 0 forward, 1 back, −1 none |
| +4 | mode: 1 running, 2 running all, 3 writing, −1 idle |
| +6 | the next command; while writing, the cursor |
| +8 | eight commands: 1 forward, 2 back, 3 left, 4 right, 5 fire, 0 nothing |

**Panel lights:** $1558 move, $155C turn, $1560 program, $1564 the program shown in the strip.

### Object shapes

- **Location:** objects' shapes are in the main data from about $4C3A0 to $5D4F2, with animated sets of frames.
- **Structure:** a shape has shadow polygons, then parts. Each part has a sort centre and faces; a face has a style, a normal and byte vertices that are doubled.
- **Style bits:**
  - 7 + 6: the object's own pen ($892 + (slot + 9 [+ 7]) × 16);
  - 7 alone: a see-through pen ($B22 + i × 32: a mask word and plane words per row, filler $1447C);
  - 6 alone: the outlined filler $1414C (span ends in colour 0);
  - 5: no unlit adjustment. Without it, faces in unlit cells use pen i − 1.
- **Object pens ($892(a6)):** 0–15 solid colours, 16–22 dithers. 23 and 24 are inverted every game frame.

### Sound

- **Segment player ($1570A, one channel):**
  - A list is volume.w and period.w, then pairs of offset.l (from $8200) and length.w (in words), until a negative long.
  - The audio interrupt ($865A/$86CA) queues the next segment each time.
  - Title music, list $3B24: one sample, 209,452 bytes at period 176 (20,153 Hz).
  - Jingle $3AF2, started at $9A8E (the end of a tower?): 43,108 bytes at period 414, at $393F4.
- **Effect records:** 46 bytes each, at $3504–$3AF1 (34 effects):

  | Offset | Meaning |
  | --- | --- |
  | +$00, +$0C, +$18 | three segments of 12 bytes: waveform (offset from $8200, or −1 to keep the one playing), pitch step, its length, volume step, its length |
  | +$24 | priority: bit 0 the envelopes run, bit 1 a second record follows at +$2E |
  | +$26, +$28 | how many more times the pitch and the volume segments repeat |
  | +$2A, +$2C | starting pitch and volume, in 64ths |

  A waveform starts with its length in words; the samples follow ($37E00–$393A6 in `main`: short tones, 2,048 bytes of noise at $37E36, …). All 34 priorities are odd.
- **Starting ($15540):** the channel with the lowest priority is chosen (the first of equals, compared unsigned); the sound plays only if its own priority is higher. The channel record ($432(a6) + $22 n) takes:
  - the record, its priority, the repeats, the pitch and the volume;
  - segment 2's steps and lengths, with both segment counters at $18.

  The channel's DMA goes off; it gets the location, length, period and volume; then DMA goes on. A record with bit 1 starts its second part on another channel the same way.
- **The envelopes ($81FA),** each tick of the CIA timer (about 99 a second; not while $AA(a6) ≠ 0), for every channel whose priority has bit 0:
  - **Pitch:** its length counts down; below 0 the segment counter drops by 12, and below 0 the repeats drop and the counter returns to $18. Repeats below 0 end the sound (priority 0, volume 0, DMA off). The new segment's waveform (if not −1) is written while the old one plays, so it follows at the end of the current block; its step and length are taken. Then pitch += step, and the period is table[clamp(pitch / 64 + $D0(a6), 0, $1AF)] from $D2(a6) (432 periods).
  - **Volume:** the same with its own counter and repeats; volume += step, shown as clamp(volume / 64 + $CE(a6), 0, 63).
- **Once a game frame ($15342):**
  - the queued sounds start (up to 16 a frame; $15316 queues unless setting $A8 is set, $1531E always, used for footsteps);
  - unless $A8 is set:
    - **the lifts' hum:** while forcers ($1210) or lift rides ($1216) are under way, $2D5A(a6) (two channels) if no channel has a priority ≥ $141; when they stop, those channels are silenced and the clunk $2DB6(a6) starts;
    - **the tower's background sound:** setting $A4 (1–3) picks two records from $AC(a6), started in turn whenever no channel has priority $29 (4 towers use 1, 9 use 2, 3 use 3).
- **Stop, pause, go on:**
  - $15494 frees every channel and clears $CE (at a tower's unpacking and its end);
  - $154C6 freezes the envelopes and silences all four (the pause menu, the end of a demonstration);
  - $154F2 switches DMA back on for the channels still in use (each time the play loop starts).
- **The volume offset $CE(a6):** 0 for a spider's view; for a camera, (z + $640) / $377 − 2 with z its turned z ($AB22), about −3 at its start; 0 while the view shows noise.
- **The tower's sound settings:** $A4 the background sound (2 bits), $A6 no footsteps (6 towers), $A8 no effects (no tower uses it).
- **Effects the rules start** (records, as `$806 +` offset):
  - blast $2D2C;
  - footstep $2E12 (on a lift $2E40);
  - floor gone $300C;
  - hop $3068;
  - thaw $2FB0;
  - convertor hum $31D8;
  - a shot that does nothing $31AA;
  - the timer under 30 seconds $314E, under 10 $317C;
  - a tower lost or abandoned $3120;
  - lightning's crack $3096 and thunder $30F2.

### Other main-data ranges

- $1C7D0 + n × $A0: images drawn by $10F64 (designer map icons? open).
- $238C8 + i × $28: 8 × 10 overlay glyphs, as a keep mask with pixels in colour 15. The "/" is at $23A58.
- $23BC0–$283BF: the panel set; the panel's pixels are at $24730–$27038. The layout is open.
- $283C0, $2CDC0, $2D3C0 (+$200 steps): small images (tower chooser buttons $A542/$A5D2, and $D34E).
- $2D3C0 + heading × $180: star tables.
- $2D9C0: moon frames.
- $339C0: lightning bolts.
- $34280: meteor frames.
- $34C80: particle offsets.

## The 3D view ($D432)

**Per frame:**
1. $AAD4: the camera;
2. $CDE6: the projection rows;
3. $1191C: clears 288 × 112 at (16, 16) to colour 7, which the copper turns into the sky and ground bands;
4. $D37A: ground shadows;
5. $D33A: stars;
6. $CF56: the moon;
7. $D120: meteors;
8. $CFFC: lightning;
9. the cells and objects;
10. the overlays.

### Camera ($AAD4)

- **Source:** the active object's record (position and heading & 3). Each quarter turn maps (x, z) to (−z, x).
- **Spiders:** add the option offsets from $9F84: $FBC sideways, $FB8 height (+$58), −$FBA distance.
- **Result:** stored at $FB0/$FB2/$FB4, the heading at $FB6.

### Projection ($CDE6)

- **Rows:** rows of cell edges run from the far side of the tower (depth $800 − z) towards the camera, in steps of $200, clamped to ≥ $100. There are at most 9 rows: min(($880 − z) asr 9, 8) + 1.
- **Per row:**
  - 9 x edges: ((x − cx) << 8) / depth + 160;
  - for levels 0–4, the tops and the slab undersides (y − $20): 72 − ((y − cy) << 8) / depth.
  - The divisions are 68000 DIVS, so they truncate towards zero.

### Traversal

- **Rows:** far to near; the camera's own row is not drawn.
- **Levels in a row:** first those above eye height (min((cy + $20) lsr 9, 3)) from floor 3 down, then floors 0 up to eye level.
- **Columns in a level:** first those right of the camera from column 7 inwards, then those on the left from column 0. The counts are min(2 − (cx asr 9), 7) + 1 and min((cx asr 9) + 4, 7) + 1.
- **Directions:** the original walks the grid with strides from the heading table $23C4; the native code turns the cell coordinates instead.

### Faces

- **Kinds:** sides ($D8E0, $D96A), the front ($D9F6, the rectangle fill $11540), the underside when above the eye ($DA4C), and the top when below it ($DAB2).
- **Hiding:** a neighbour with bit 6 hides a slab's face; one with bit 3 hides a block's.
- **Blocks:** a block reaches from the slab's underside to the next level's underside.
- **Lifts** ($DCD8 heights, $DE28–$DFDC): the platform is drawn at the floor, at the underside of the level above (bit 3), or at the lift rider's y, projected at the centre depth ∓ $100.
- **Pens ($A22(a6)):** 16-byte pens at $1228, as two rows of four plane words (2 × 2 dithers), aligned to the screen. In order:
  - under, side, front / odd side, odd front, top, lit top;
  - the same for the second square colour;
  - a spare;
  - ground shadow (colour 8);
  - lift top, lit lift top.

### Polygon filler

- **Clipper ($147E0):** Sutherland–Hodgman on x, then on y, with 14-bit intersections. Repeated points are removed.
- **Scan-line fill ($13E3C):**
  - **Start:** the top vertex has the least y, then the least x. The right side is walked forwards and the left side backwards.
  - **Edges:** 16.16 fixed point. The slope is DIVS(dx × 256, dy) << 8, including DIVS's overflow case. A vertex's x replaces only the integer part.
  - **Flat right edges** after the first are passed over.
  - **Spans:** inclusive; the last line is strict.

**Mouse picking ($1507C):** the filler compares the pixel under the pointer
before and after filling each top face. That gives the cell under the
pointer ($7EE–$7F4).

### Objects in the view ($E0AC, $13934)

- **When drawn:** each cell's object (from the object map) is drawn at the cell's place in the traversal. Slots ≥ $3F and types > $1A are skipped. Type $1A is a particle cloud.
- **Shadows:**
  - only in lit cells, and only while the eye is above the first shadow point's height;
  - drawn in the floor's unlit top pen (the lift top pen in lift cells).
- **Part order:** far to near by the squared distance of the sort centres (32-bit). A new part goes before equal ones.
- **Back faces:** a face is skipped when its turned normal · (first point − eye) ≥ 0. A point behind the eye drops the face.
- **Moving objects:** clipped in 3D at the boundary of the cell being drawn. The side comes from the object's position against the cell centre ($FA6/$FAA), with the plane ±$100.

### Sky and effects

- **Colours (copper mode 2, $89D2):** registers 0–7 and 9–15 come from the tower's colours, and 8 from colour 17.
- **Sky gradient on colour 7:**
  - it starts at "Sky Colour Top" and changes at the lines in the sky type's list ($8ACA: type 0 has 17 lines, type 1 has 15, type 2 has 13, type 3 none);
  - it runs a 15-step gradient towards "Sky Colour Bottom" ($12392: 8.8 fixed point, start | $80, step DIVS(diff << 8, 15));
  - type 2 alternates bottom and top stripes;
  - longer lists read on into the panel palette;
  - line 72, the horizon, takes colour 16;
  - the panel palette starts at line $82.
- **Stars ($D33A):** a table at $2D3C0 + heading × $180 of 64 × (x, y, pen). 21 × density + 1 stars are plotted, each in the first row of its pen ($14F88).
- **Moon ($CF56):**
  - Shown when the heading equals $E3A and $E40 ≤ $37.
  - 16 pixels wide and up to 16 rows, at (16 + 16 × $E3E, 16 + $E40), with mask rows $CF36.
  - Frames are at $2D9C0 + type × $1000 + (frame & 31) × $80.
  - The frame counter $E44 advances by $E42 − 1, and only when the moon is drawn.
- **Meteors ($D120, when tower flag 7 is clear):**
  - **Start:** after a wait ($FCC), a meteor starts at x from the random number (r & $1FF, halved unless < $190) and y −15.
  - **Movement:** −4, +2 per frame until y ≥ $38.
  - **Drawing:** only in the heading it started in. 8 frames at $34280 + n × $140, a 32-pixel masked image at x + 1 ($D1DE), in the top 56 lines only.
- **Lightning ($CFFC, when tower flag 6 is clear):**
  - **Timing:** every other frame, while $20(a6) = 0. After a wait ($FC4) a bolt appears; then comes a short wait (6–13) or a long one (32–62, with a new heading). The thunder follows after 4–7 more waits ($FCA).
  - **The bolt:** 16 × 56 at $339C0 + n × $230, in column 2 + (r & 15), shown only in the heading $FC6.
  - **Effect:** a crack sound and a palette flash. The flash uses copper list 2 with $2592: the 18 colours, with $FFF ORed in where the mask at $1D448 has a bit.
- **Particle clouds ($14C18, type $1A):**
  - 2n big dots (2 × 2 at even x, $14FEA), then 2n small ones, where n is the cloud's remaining count;
  - offsets from $34C80 (+ $18C0 for the second cloud, +$12 bit 9), plus the table $14D58 by 32 − n;
  - colours 9–15 and 8 in turn.
- **Overlays:**
  - READY ($151CC, only before the start);
  - compass and counters ($150BC);
  - the timer ($15152, BCD at $121A);
  - the sights ($E0F6).
  - They are drawn on line 18 or 118, chosen by the options $1112/$1114.

## The rules

### Program steps and moves ($BC7A)

- **Spiders (slots 4–6, type < $1A):** a standing spider takes its next program command ($CB6C); a moving one advances its move ($C97E).
- **Other objects (slot 7 on, until a negative type):** after the start ($11E4 ≥ 0) they only advance their moves here; their decisions come in $BEE4.
- **Forcers ($BCE4):** run last (below).

**Program steps ($CB6C):** only in mode 1. The position advances by 1; past 7
the mode becomes −1. A command that is not possible is skipped.

**Starting a move ($B510):**
- **Walk:** the cell ahead must be solid (bit 7), not a block (bit 3) and have no object. Then the state is the direction and the progress 32.
- **Lift rides (directions 8 up, 10 down), from a lift cell (bit 5; bit 4 = up position):**
  - going up, the cell above becomes solid and is claimed;
  - going down, the cell's opaque bit and the lower cell's block bit are cleared;
  - $1216 counts the rides under way.
- **Back ($B4B4):**
  - a moving spider reverses ($B5E2). A walk cannot reverse at progress 16, 10 or 22. A reversed lift ride swaps its cells and sets the spider's bit in $1218's high byte;
  - a spider standing on a lift rides it;
  - otherwise a backward step needs the "Backwards allowed" option ($110E).
- **Turns:** instant.

**Advancing a move ($C97E):**
- **Walk:**
  - 16 units a frame;
  - at progress 32 − claim (claim = $CB28[type], 10 for spiders) the cell ahead is claimed. If it is taken, the object bounces: state ^ 2, progress 32 − progress;
  - at 16 the cell and target swap;
  - at the claim value the old cell is released.
- **Pushed (states 4–7):** steps from $CB3C; the swap comes at 8 and the release at 0.
- **Lift (8–11):** y changes by ± $CB4C[progress − 1]. At the end:
  - going up, the cell gains the block bit and the target the opaque bit;
  - going down, the cell loses the solid bit;
  - the old cell is released and $1216 drops by 1.

### Firing ($B66E)

The spider in slot 4 zaps ($B684), slot 5 pushes ($B83C) and slot 6 grabs
($B90E), always in its heading.

- **Zap and push beams:**
  - **Range:** up to 16 cells. They stop at border (bit 0) and block (bit 3) cells and at type $3F.
  - **Passed through:** empty cells, the shooter (by +$18; after a reflection no one is the shooter) and particle clouds.
  - **The zap also** stops at lift riders and passes objects that are only claiming the cell (cell ≠ their +$14). The push does not check that.
- **Zap, by what it hits:**
  - **Destroy:**
    - the object becomes a particle cloud: flags 0, progress 0, +$1A = 32, its target released, type $1A;
    - a spider triggers the lost check ($BC16);
    - other objects count towards $E32;
    - a working Freezer is undone ($11E4 + 1);
    - then the win check ($BBE2).
  - **Reflect:** the direction turns round (^ 2), and no one is the shooter any more.
  - **Prism:**
    - If its heading equals the direction, the beam turns left (direction − 1).
    - If heading + 1 equals the direction, the beam turns right (direction + 1).
    - Otherwise the beam stops.
    - The prism becomes the shooter.
  - **Convertor in line** (heading ^ direction even): the beam goes on as a push.
  - **Reverse a move** (+$12 high bit 3): not at progress 0, 2, 30 or 16.
- **Push:**
  - A pushable, standing object moves one cell when the next cell is solid, not a border or block, and has no object. It gets progress 16 and state direction + 4, and claims the target.
  - A convertor in line turns the push into a zap.
- **Grab beam (no range limit, no shooter skip):**
  - **Klondike:** taken (high bit 0), $11D8 + 1 up to $E30, then the win check.
  - **Forcers** ($11 up $BAAA, $12 down $BB42):
    - they move every free lift that is in the other position, on floors 0–2 (up) or 3–1 (down), at most 20 of them;
    - the lifts are carried by slots 25–27 for 32 frames ($BCE4);
    - the cell flags are rewritten at the end from the list $11E8.
  - **Freezer:** +$1C = 128 and $11E4 − 1. The tower stops until the Freezer's own count reaches 64.
  - **Wiper:** +$1C = 63 when idle.
  - **Exchanger:** the Grabber takes its place and turns round; the Exchanger explodes and counts.

### The end and the freeze counter

- **Won ($BBE2):** when everything is collected and destroyed, $11E0 = $FC0 + $15 and $11E2 = 1.
- **Lost ($BC16):** reason 4 if the Grabber is gone with Klondikes still missing; reason 3 if all three spiders are gone.
- **$11E4, the freeze counter:**
  - it is −1 before the start, and the play loop raises it to 0 at the start;
  - each working Freezer lowers it by 1;
  - objects with decisions do nothing while it is negative.

### Each object's own behaviour ($BEE4)

**The loop:** $BEE4 runs after the moves. It goes from slot 4 to the first
negative type, skips types ≥ $1B, and calls a handler per type ($BF16
table). It also sets each object's shape for the frame (frame tables
$BD9A–$BED4; spider frame sets $BF4E); the frame counter is $FBE.

| Type | Object (kind table flags) | Handler |
| --- | --- | --- |
| 0 | spider | $BF6E |
| 1, 2 | Zapper, Pusher | $C066, $C0B2 |
| 3, 4 | Rotating Zapper, Rotating Pusher | $C0FE, $C14E |
| 5 | Worm ($0804) | $C19A |
| 6 | Watcher ($0204) | $C1FC |
| 7 | Hopper ($0001) | $C382 |
| 8 | Proximity Mine ($0204) | $C466 |
| 9 | Time Bomb ($0204) | $C586 |
| $A, $B, $C | Block, Reflector, Prisms | none |
| $D | Lizard ($0200) | $C66E |
| $E | Klondike ($0209) | $C6F4 |
| $F | Bonus Flag | $C74E |
| $10 | Convertor | $C76C |
| $11, $12 | Forcers | $C7B4, $C7E4 |
| $13 | Freezer | $C814 |
| $14 | Wiper | $C868 |
| $15 | Exchanger | none |
| $1A | particle cloud | $C8F0 |

Handlers 1–9, $D, $11, $12 and $14 do nothing while $11E4 < 0.

- **Spiders:**
  - a footstep sound every 4 frames of a walk ((progress & 3) = 1, when $A6 = 0), the lift sound on a lift cell;
  - a reversed lift ride (its $1218 bit) shows frame (6 − state / 2) & 3 once and clears the bit.
- **Zapper and Pusher:** fire every frame in their heading, with their own slot as the shooter.
- **Rotating Zapper:** +$1E = $BE0A[frame & 63]. It fires when that value is a multiple of 8, in direction value / 8.
- **Rotating Pusher:** pushes when (frame & 15) = 0, in direction frame / 16.
- **Worm:** when standing it walks on in +$0E; if it can't, +$0E ^= 2 (it turns round).
- **Watcher** (+$0E is its direction): keeps a wall on its left. When standing:
  - **Turning:** a turn under way (+$1C) counts down, showing turn frames $BE6A[(±$1C + 4 × state) & 15]; +$1E < 0 means a right turn.
  - **Left free:** if the cell to the left is free ($C34A: solid, no block, no object), it turns left (4 frames).
  - **Otherwise, in order:**
    - after a turn, or when the left is blocked, it walks on;
    - if it can't, it turns right (4 frames, +$1E = −1);
    - else it turns round (8 frames);
    - else it stands, with frames chosen by the frame counter.
- **Hopper:**
  - when standing it shows its base frame and +$0E &= 3. It then tries to move right, ahead, left, then back;
  - boxed in, +$0E |= $10 and progress 16: a hop on the spot;
  - the hop sound plays at (progress & 15) = 1.
- **Proximity Mine:** goes off when a neighbouring object is moving (progress ≠ 0, state < 8).
- **Time Bomb:** +$1C += $E2E + 1 each frame; at ≥ 800 it goes off.
- **A blast** (both):
  - every neighbouring object of any type, except lift riders, becomes a particle cloud, and so does the bomb itself;
  - none of these count towards the objective;
  - a mine's cloud gets +$12 = $400 (leave a hole) unless $E2A < 0 (tower flag 8);
  - then the lost check, and the blast sound.
- **Lizard** (uses +$0C):
  - when standing, if the cell remembered in +$1C is free now, the floor there goes (floor-gone sound);
  - then it walks ahead; if it can't, it turns round and remembers its own cell in +$1C;
  - so it eats the floor where it turned, once it has walked off.
- **Klondike:** once taken (+$12 bit 8), 7 vanishing frames; then type $3F, and its cell and map entry are cleared.
- **Convertor:** a hum at (frame & 31) = 1.
- **Bonus Flag, Forcers:** frames only.
- **Freezer:**
  - +$1C counts down;
  - at $40, $11E4 + 1: the tower runs again, with the thaw sound;
  - so the tower stands still for 64 frames, while the Freezer animates for all 128.
- **Wiper:**
  - +$1C counts down from 63;
  - at 48, 32, 16 and 0 it sweeps west, south, east and north ($23E4[count / 16]), removing each floor cell up to the border or a block;
  - each sweep plays the floor-gone sound.
- **Particle cloud:** +$1A counts down; at 0 it becomes type $3F and its cell and map entry are cleared. With +$12 bit 10, the floor under it goes.

### Removing a floor cell ($C924)

- **Conditions:** only a solid cell that is not a lift, has no object and, on floors ≥ 1, is not above a block.
- **The lit bit:** passes down to the first non-empty cell below, or into the ground cell.

## Bugs in the original

Both are kept in the re-implementation.

- **Meteor plane mix-up ($D2B4):** in a meteor's first partial word, plane 3 takes plane 0's bits.
- **Freezer counter:** zapping a Freezer whose +$1C ≠ 0 always adds 1 to the freeze counter $11E4, even after the Freezer has already thawed and added its own 1. The counter then stays at 1, and the next Freezer only lowers it to 0, so it stops nothing.

## Copy protection (left out)

- **At start:** the program asks for a word from the manual (page, paragraph, line, word; the language chosen first). The cracked copy accepts any word.
- **Delayed check (hypothesis):** the View page ($9CF0) hashes the text at $4550(a6). Two sums, × $1D and × $15B over the characters after leading spaces, are added and XORed with $1288. It compares the result with a table at $6E38(a6) indexed by $2C(a6), and on a mismatch clears memory in a stride and returns into it. On the reference, with a made-up word, the machine crashed right after the View page (once; the program counter was in the Kickstart ROM).
- **Disk checks** before panel loads, and once group 0 has 7 solved towers: the sum of block $AA must be $29CF849D.
- **Anti-tamper:** checksum loops in many routines (main menu, text drawer, interludes) jump over code that clears memory. All of this is left out.

## The re-implementation

**Namespace:** `babel`, under `games/tower_of_babel_amiga`.

**Code:**

| Folder | What it holds |
| --- | --- |
| `import/` | the decruncher (byte-equal to the machine's memory at $7A5E); the importer, which identifies the release by the loader's SHA-256 and checks the program and six block ranges (title, main, panel1–3, music) by SHA-256 |
| `data/` | decoders: title pictures, tower records, shapes, sampled pieces, effect waveforms; the launcher previews and the shared 3D model viewer |
| `game/` | `tower_map` (cells with shape flags, lit columns $CCA8, colour pattern $CD90); `tower_state` (the running tower in the rules' bordered layout: object map, objects, programs, panel lights, forcers, objective, freeze counter); `rules` (programs, moves, firing); `behaviour.cpp` (each type's update); `animation` (the frame choice as a pure function of state and frame counter) |
| `game/panel` | the panel's rules ($ADFC): choosing, direct commands, cameras, program editing, lockouts |
| `view/` | `polygon` (the clipper, the filler, see-through and outlined fills); `tower_view` (camera, projection, traversal, faces, objects, particles, the pointer's pick); `sky` (incl. the 48-colour game palette and its copper view); `overlay` |
| `design/` | `tower_design`: the designer's tower, its packing to and from records, every editing rule, repair, scroll, rotate, copy, swap, clear, statistics, support and path checks |
| `front/` | `data_disk` (the data disk as a folder of files, its answers kept for quick saves); `designer.cpp` (the designer's screen, menus and pages); `screen` (the 320 × 200 picture, pens, rectangles, lines, bevelled boxes, windows, pictures, the font and layout codes, menus' click areas, the copper modes); `play` (the game screen and the play loop, demonstrations); `front_end` (every screen from the title to the end screens, as coroutines that wait for vertical blanks, `flow.hpp`); `effects` (the sound channels, their envelopes and the per-frame logic, on the shared Paula model); `module` (the shell's game module: mouse, keys with Shift, picture, the sampled pieces and effects, the effects' timer ticks between rendered stretches, quick saves) |

**Assets tool:** `tower_of_babel_assets import | export | viewcheck | animcheck | framecheck | logiccheck | objectcheck | panelcheck | fxcheck | drive`. `drive` runs the front end headless from a script (frames, move, click, key, shot, and save / load for quick saves) and writes the effects it played to effects.wav.

**Design choices:**
- **Drawing from state:** the view is drawn from game state by the native renderer, never by copying the original's 320-pixel output. That leaves room for widescreen.
- **Tick rate:** the rules run at the original's tick rate, apart from drawing, so positions can later be interpolated up to 120 Hz.
- **Cell numbering:** native cell index = original index + 100. The native grid includes the ground row as floor −1. The cameras' +$14 / +$16 are kept as raw offsets.
- **Frames:** the module ticks at the PAL frame rate (50 Hz); a game frame runs whenever 14 timer ticks (709379 / 7160 Hz) have built up, the rest carried over. The front end's waits (fades, menus, the name editor) are in vertical blanks as in the original.
- **Clicks:** a press between two game frames counts as held at the next poll, at the place it was made. The original only sees the button when it polls (a quick click can be missed).
- **Keys:** the shell's keys are mapped to the unshifted table's characters; with Shift held, a key's place in that table ($69C(a6)) is read from the shifted one $70 further on, as $80E2 does, so names and texts take capitals.
- **Quick saves (the shell's save and load):** the front end is written as coroutines, whose place in the code can't be saved as values. So a quick save is a checkpoint and a journal:
  - **Marks:** the flow marks the points it can be taken up from: the start, each time the main menu is drawn, the designer opened, and every game frame of a tower in a game (not the designer's test). A mark keeps the whole state: the player, progress, tables, custom group, the designer's work, the screen (picture, colours, copper mode, windows' saved boxes, the blink's phase), the fades' colours, the timer's ticks, the last inputs, the random number, and in a tower the `Play` (tower state, panel, sky, view offsets, the frame's start positions for smooth motion).
  - **Journal:** after a mark, each vertical blank's input (as the game saw it, the widescreen margin taken off; runs of equal inputs counted) and every answer the data disk gives (file lists, files read, writes' results).
  - **A save** is the last mark, the journal and the module's own sound state (the effects' channels and Paula, the sampled piece's voice, the keys held).
  - **A load** takes the state up at the mark: the flow goes back in through `resumed()` (the main menu, then the game type's loop or the designer, past the chooser and the fades) to the line after the mark, finishes that blank, and plays the journal's blanks again. The disk gives its recorded answers and writes nothing. The sounds made meanwhile are dropped. The load fails if the replay doesn't end at the same mark and journal.

  So a load goes on exactly as the game would have from the moment of the save, anywhere (the title, a requester, the pause menu, the jingle, the score screen). A load replays at most the time since the last mark: in a tower a game frame, in the designer or the menus the time since they were opened, which is fast (an hour in the designer in well under a second, ten minutes of a demonstration in about one second). Saves are about 75–85 KB, with the music's samples while a piece plays about 280 KB.
- **Widescreen (option):** the picture is 320 + 2m columns, m chosen so the window shows up to 21:9. Only a running tower or demonstration widens:
  - its view is drawn again across the wider window (the projection's centre m further right). The traversal already draws every column, so the sides need nothing new;
  - the title line and the view's frame stretch with it;
  - the star field repeats beside the original's 288 columns (no moon, meteor or bolt there); the overlays stay where they were relative to the middle;
  - the panel, the menus and every other screen stay 320 wide in the middle, with black beside them;
  - in the pause menu the wide picture is drawn once more at the paused frame, and what the menus draw shows over it;
  - the pointer is taken m columns in from the left. A click in the view's widened sides selects nothing (the pick is the game frame's, in the original 288 columns).
- **Smooth motion (option):** each game frame keeps the positions it starts from. The display draws the view again with every object, lift and the camera that far between them (the timer's ticks since the frame, out of 14). A newly chosen or turned camera, and any jump of more than $300, are not glided. The view is shown one game frame late; the rules don't change.
- **Both options** draw from the game's state without changing it: the sky as the frame drew it (its moon animation would otherwise move on), the noise picture as it is.
- **The designer's cell cursor** is drawn from sprite 1's image ($2AFE(a6)) in the mouse style's colours 17–19 ($2B4E(a6)).
- **Left out:** the crack, the copy protection, the disk loads and disk checks, the screen-position settings (kept in saved positions), the data disk's own format and its errors. The credits also end on a click (the original: Space only).

### How it was checked

Each part is compared with the reference machine's memory or screen, frame
by frame:

- **Decruncher and title:** the decruncher equals the machine's memory at $7A5E, and the title picture equals a capture.
- **3D view (`viewcheck`):**
  - Compared with the original's freshly drawn buffer, with stars, moon, effects and overlays off: identical, 0 of 32,256 pixels, in all four headings at several positions and eye heights. The cell flags equal the original's grid.
  - With objects on: identical on towers A–C in 24 grabs.
- **Animation (`animcheck`):** 0 differences in 3,800 demo frames: spiders walking, turning and standing; Zapper, Rotating Zapper, Worm, Hopper, Proximity Mine, Lizard, Klondike and Wiper.
- **Sky, effects and overlays (`framecheck`, the whole view window and the copper list):** identical in all 255 frames on towers A–C (sky types 0–2, with lightning and meteors switched on by poking; 80 meteor frames), in 3 lightning flash frames, and in 60 demo frames (18 with particle clouds).
- **Programs, moves and firing (`logiccheck`):** 0 differences in 15 recordings, 1,795 frames:
  - walks, skipped commands, push, bounce, lift up and down;
  - a Klondike grabbed and the tower won, a spider destroyed;
  - reflection onto the shooter, prism, reversal;
  - both forcers, Freezer, Wiper, Exchanger.
- **Objects' behaviour (`objectcheck`):** cells, map and records, including +$1E and the shape. 0 differences in 13 recordings, 1,825 frames, from twelve cases staged on tower A:
  - Zapper and Pusher, Worm, Watcher, Hopper;
  - a Lizard eating 2 cells, a mine set off by a walking spider;
  - a time bomb (Grabber lost, reason 4), a Rotating Zapper, a Rotating Pusher;
  - a Freezer (63 frames stopped), a Wiper (3 cells);
  - a Klondike vanishing, a Convertor with a Bonus Flag.

- **The panel (`panelcheck`, harness/panelrec.py):** the poll $ADFC from the recorded variables, with the mouse set at each poll. 0 differences in 304 frames covering choosing spiders and cameras, camera moves and zooms, writing and deleting, the strip, clearing, a camera's program handed to a spider, turns, moves, Fire and Run All.
- **Front end (`drive`, harness/fe/*.txt, compared by harness/fe/cmp.py with screen.py captures):**
  - pixel-identical: the credits, the name prompt, the main menu, the game types, the chooser (with a tower chosen, and after a win with A solved and B, D open), the information pages of towers A and B, the Setup page, the game panel with a written program, the win screen's box and score (230 on tower A);
  - the demonstration after a win: the view identical to the reference's frame at the same moment;
  - the title differed only by the reference renderer's 3-line offset (since fixed: the display starts at $29, not $2C).
- **Sound effects (`fxcheck`, harness/fxrec.py):** the four channel records at every envelope tick, every start and every stop of the reference, with the panel driven as in panelrec.py. From each event's channels the native tick or start must give the next event's. 0 differences in two recordings on tower A: 35 starts and about 6,000 ticks (plus 65,000 while paused):
  - footsteps on the floor and on a lift, the zap, the grab, a push, a shot that did nothing;
  - the lift's hum and clunk (both with their second parts).

  The hum is silenced inside $15342 just before the clunk starts, with no call to record; the check applies that step as `Effects::frame` does.
- **Saved positions and high scores (harness/fe/saves.txt):** the Save/Load menu and the High Scores table pixel-identical with the reference. The requester as the original's, apart from the counts the reference cannot show without a data disk. A position saved, listed, picked and loaded; a table written by Upgrade.
- **The designer:**
  - `designcheck`: all 153 tower records unpacked and packed again byte for byte.
  - The designer screen, its four menus and 14 pages pixel-identical with the reference: Options, Effects, Sound, Moon, Behaviour, Finishing Conditions, Scroll, Rotate, Copy Level, Designer Setup, Statistics, the Support and Spider Path checks with their animation (harness/fe/designer.txt).
  - Then placing and taking objects with their messages ("Spider already placed"), a change of floor, Get Tower with the tower's name and its question, and the Colours page (harness/fe/designer2.txt). In all 27 shots the only difference is the cell cursor, a sprite the reference renderer doesn't draw.
  - harness/fe/refrun.py runs the same scripts on the reference, so both machines take the same steps.
- **Widescreen and smooth motion (harness/fe/wide_smooth.txt):** with both off, 275 pictures (game, win screen, 260 demonstration frames) unchanged by the view's refactoring. With them on: a walk changes the view every display frame, and the pause menu composes without seams.
- **Quick saves:**
  - With `drive`'s save and load put after every few lines of each harness/fe script (win_a, pause_b, designer, designer2, menus, saves, abandon, wide_smooth), every later picture and the effects' sound equal the uninterrupted run's.
  - The shell's persistence checks (tests/persistence.cpp) run alone against the module: at the test's frames (120, 360, 600, 1800, idle) and every 53 frames through tower A and its score screen, with the options off and on: the state written again at once equals the save, and the picture right after the load, then 120 more frames of state, sound and picture, equal the original's.
- **The timer (harness/fe/timecheck.cpp):** "The Lift Is Guarded" counts 160 BCD seconds down in 1,120 frames and ends with reason 2.
- **Played through:** tower A solved by the program Fire Fire Fire for the Grabber (the score screen as the original's); Restart and Abandon from the Exit page.

**Harness notes for the reference:**
- **Display:** screen.py now takes the display's first line from $2(a6) ($29); earlier captures were drawn 3 lines high.
- **Avoid the View page** on the reference: with a made-up manual word the machine crashed after it (above).
- **Panel clicks:** in play the panel is polled once per game frame, so a click must hold the button for about 20 video frames. A 3-frame hold or an early click leaves the game at READY.
- **Keys:** go in through $692(a6).
- **Mouse:** a breakpoint at $806C in the vertical blank, then pokes to the pointer $814/$816 and $E9C/$E9E and the buttons $EA0.
