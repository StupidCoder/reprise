# Mind Walker (Amiga) — how the original works

What was learned while rebuilding Mind Walker (Commodore, 1986): the disk
and its module loader, the data formats, how each of the seven stage
programs works, the rules of the landscape, tunnel, arena and psyche
screen, the front end and saves, and the original's oddities. The
remaining work is in [plan.md](plan.md); how to play is in
[PLAYER_GUIDE.md](PLAYER_GUIDE.md).

## Sources and evidence

- **Image:** `Mind Walker (1986)(Commodore).adf` (901,120 bytes),
  SHA-256 `2e83a67a30c716bada40ff6415fb73f644817c108a39945a1df59875e9a8969c`.
- **Method:** everything here comes from this image (own ADF and hunk
  readers, capstone, one Ghidra program per module) and from runs of it in
  FS-UAE (A500, Kickstart 1.3, GDB stub). No outside material on this game
  was used; RetroReverse has none.
- **Addresses:** each module has its own research base (`work/bases.txt`):
  A $200000, B $300000, C $400000, P $500000, T $600000, S $700000,
  E $780000, the loader L $7C0000. `hNN` is a hunk of the module in
  question. The shared block lives at `$7C04F4`.
- **Paths:** source paths are relative to the Reprise root. Evidence lives
  outside the repository in `.analysis/mindwalker_amiga/`:
  - `work/` (flat modules, decompiles), `ghidra/`;
  - `harness/`: `run.sh`, `mkboot.py`, `mw.py`, `mwrec.py` (the per-frame
    recorder), `worldref.py`/`viewref.py` (oracles running the game's own
    routines), `regress.sh`, recordings in `runs/`;
  - `tools/`: `m68kdis.py`, `rawpng.py`, timing scripts;
  - `guide/`: material for the player guide.
- **Checking tools:** `build/mindwalker_replay` (modes `title`, `walk`,
  `view`, `tunnel`, `arena`, `psyche`, `title-again`, `flow`, `sim`,
  `quicksave`), `build/mindwalker_world` (`check`, `show`, `scan`, `grid`)
  and `build/mindwalker_assets`. "Joyport" in the notes is the native
  re-implementation's earlier project name.

## Overview

The game is seven compiled-C programs run one at a time by a small loader
(S set-up, T title/menus, A the landscape, B the tunnel, C the arena, P the
psyche screen, E errors). The state that crosses between them lives in a
784-byte block the loader owns; the stage order follows a mode word
(T → A → B → C → P → A …). The worlds are generated from a seed (four preset
seeds and a clock seed), not stored. The port keeps that split as concepts
(a game record handed from stage to stage), not as a byte image, and keeps
the original's random generators (an exception to the standard-generator
rule, by the user's decision).

## The original's oddities (summary)

Each is described where it belongs below.

- **Resuming the tunnel** after its pause sets it up again: the pace goes
  back to 7 and the wander is reseeded.
- **Arena and psyche pauses:** C and P finish the interrupted pass after a
  nested button read; P takes a click after any of its button reads.
- **Frame-less sequences** (the arena's death and wall death, the last
  picture's passes) run as fast as the blitter allows, so their pace on a
  real machine depends on load.
- **Sliders without FREEHORIZ** keep their knob at the left.
- **Slowdown:** the beacon bonus and the music's tempo follow the frame
  rate, which drops under load.

## Disk, boot chain, modules, reference machine

Image: `Joyport/Mind Walker (1986)(Commodore).adf`, 901,120 bytes, SHA-256
`2e83a67a30c716bada40ff6415fb73f644817c108a39945a1df59875e9a8969c`.

### Sources and limits

- Everything here comes from this image and from running it. No existing
  disassembly, port, map or guide of this game was used. The user's
  RetroReverse project has no Mind Walker material (checked 2026-10-27).
- Tools: Joyport's own `build/amiga_disk` (list, extract, hunks, flat),
  Ghidra 12.1.4 headless (`ghidra/ghidra.sh`, one program per module),
  capstone (`tools/m68kdis.py`), Python/PIL for research renders.
- Reference machine: FS-UAE, prb28 fork at 58feb4c with the arm64 patch
  (`~/Development/fs-uae`), Kickstart 1.3 (RetroReverse
  `tools/platform/amiga/kick13.rom`, MD5 `192d6d950d0ed3df8040b788502831c2`),
  A500 model (512 KB chip + 512 KB slow RAM; ExecBase lands in slow RAM).

### File system

Plain AmigaDOS OFS disk, volume `MWDisk`, standard boot block (DOS boot
code only, no custom loader, no protection track found). All block
checksums valid. Files (`amiga_disk list`):

| File | Bytes | What it is |
| --- | --- | --- |
| `Mind Walker` | 3,040 | the loader (12 hunks, library-stub symbols kept) |
| `MindWlkS` | 14,468 | module S: opens the screen, copies the shared images to chip memory |
| `MindWlkT` | 28,112 | module T: title, world menu, command menu (save/load), high scores |
| `MindWlkA` | 67,732 | module A: the physical level (3D map) |
| `MindWlkB` | 20,652 | module B: the tunnel |
| `MindWlkC` | 40,580 | module C: the arena (score, strength, temperature) |
| `MindWlkP` | 38,900 | module P: the psyche screen (portraits, words, bonus) |
| `MindWlkE` | 2,128 | module E: error requester ("Could not load data from the disk." …) |
| `MindB` | 32,000 | title picture cache (5 planes, HAM; "Data formats") |
| `MindF1`…`MindF6` | 8,000 each | the protagonist's six portraits (1 plane) |
| `MindHighs` | 256 | high-score table (32 entries) |
| `Preferences`, `c/`, `l/`, `libs/`, `devs/`, `s/`, `t/`, `*.info` | | Workbench 1.x system files |

`s/Startup-Sequence` echoes a welcome and runs `LoadWb`; the player starts
the `Mind Walker` icon. The game also writes `MindB` (rebuild path, see
"Data formats"), `MindHighs` and saved worlds `MindWorldA`…`MindWorldH`.

The modules are compiled C (Lattice-style start-up: `_exit`, `_SysBase`,
`_errno`, `initialSP`; every function opens with `LINK A6`) linked with
amiga.lib stubs whose symbols survive (`_OpenScreen`, `_LoadRGB4`, …).
Each object module is a CODE/DATA/BSS triple.

### Research layout

One flat image per module (`work/bases.txt`, hunks 8 bytes apart, RELOC32
applied — what `amiga_disk flat` and `amiga::LoadedImage` produce):

| Module | Base | Hunks |
| --- | --- | --- |
| A | `$200000` | 82 |
| B | `$300000` | 43 |
| C | `$400000` | 62 |
| P | `$500000` | 68 |
| T | `$600000` | 46 |
| S | `$700000` | 27 |
| E | `$780000` | 8 |
| loader (L) | `$7C0000` | 12 |

Addresses in these notes are in this layout ("A `$2085D8`"). Ghidra:
`ghidra/ghidra.sh import MOD`, `ghidra/ghidra.sh all MOD OUT.c`
(pseudo-C of every function in `work/MOD.c`).

### Boot chain and module protocol (read from the code)

1. Workbench starts `Mind Walker`. Its own start-up (`$7C0000`) returns 100
   when started from a CLI (`pr_CLI != 0`) and otherwise waits for the
   Workbench message.
2. Loader main `$7C0108`: opens intuition and graphics, `CloseWorkBench`
   (bitplane DMA off if it closed), `AllocRaster(320, 1800)` (the 6 planes
   of the 320×200 game screen, 8,000 bytes each, plus spare) and
   `AllocRaster(336, 200)`, keeps a **shared block** in its BSS at `$7C04F4`
   (784 bytes, see "Shared block"), and runs modules one after another:
   - `run_module(letter)` `$7C03F6`: patches the letter into
     `"MWDisk:MindWlkS"`, `LoadSeg`, `CreateProc("WyndMod", pri 0, seg,
     4000)`, busy-waits on `FindPort("WyndPort")`, sends a message whose
     first data long (+$14) points to the shared block, waits for the reply,
     `UnLoadSeg`.
   - The module sets shared+4 ("status") before replying: 0 = carry on,
     1 = back to T, 2 (set by the loader before the run) = the module never
     replied properly, 3 = error, 5 = running.
   - Order: S, then T, then while status is 0 the module chosen by the mode
     word (shared+$14): 2 → C, 3 → P, 8/9 → B, anything else → A; status 1
     → T again. On quit: `FreeRaster`, `OpenWorkBench`, `CloseScreen`, and
     module E (the requester) unless T asked to quit.
3. Each module: `AddPort("WyndPort")`, `WaitPort`, `GetMsg`, works, sets
   status, `RemPort`, `ReplyMsg`, `_exit`.

The module graph (mode values written by the modules):

```
S --> T --(world chosen: mode 0)--> A --(done: mode 9)--> B
B --(mode was 9: success -> 2, else 1)--> C or A
C --(success: 3, +$5C += 7 up to 42; else 1)--> P or A
P --(1)--> A          any module --(status 1)--> T
```

Seen in the reference machine (2026-10-27): T's title (brain picture),
the world menu (Preset 1-4, New Game), A after Preset 1, B (tunnel),
C (arena) and P (psyche screen) by ending each module's loop from the
debugger (`harness/mw.py finish_module`).

### Reference machine and harness (`harness/`)

- `mkboot.py <adf> <out> [--hold]`: research boot disk. The copy's loader
  accepts a CLI start (`$7C001E` beq→bra, `$7C0050` bsr waitmsg → bra to
  main, final ReplyMsg at `$7C00B8` → NOPs) and `s/Startup-Sequence` runs
  `"Mind Walker"` instead of `LoadWb` (only OFS data blocks change, their
  checksums recomputed). `--hold` also makes the loader's first instruction
  `bra.s *`. Note: a padded last line of spaces made the CLI stall before
  the command; a real command after it (`Echo D`) works.
- `run.sh`: starts FS-UAE on the boot disk with the GDB stub on port
  `$MW_PORT` (default 6861; another session's Marble Madness machine uses
  6860), remembers its pid in `work/ref/fsuae.pid` and stops only that
  instance on the next start. `--remote_debugger=1` enables the stub
  without waiting (0 disables it).
- `shot.sh OUT.png`: captures this instance's window (`winid` prints window
  ids per pid).
- `mw.py`: GDB client (`MW`, with the stub's `%Stop` packets handled),
  task/port lists, `module()` locates the running module's hunks through
  the `WyndMod` process's `pr_SegList[3]` and identifies it by hunk count,
  `Module.rt(our address)`; `inject(d, events)` feeds input by stopping
  after each module's reads of `$BFE001` (left button bit 6, fire bit 7)
  and `$DFF00C` (JOY1DAT) and rewriting the register, and pokes the
  screen's MouseX/MouseY (Screen +$12/+$10) for clicks; `shared_block(d)`;
  `finish_module(d, mode)` sets a module's "done" flag and, at its exit
  routine, the next mode.
- `ports.py`, `tasks.py`, `watchtasks.py`, `findbytes.py`: inspection.

Input reads (our layout, instruction after the read):

| Module | Left button / fire (`$BFE001`) | Joystick (`$DFF00C`) |
| --- | --- | --- |
| T | `$600406` d0, `$60166C` d1, `$603224` d0, `$6040F0` d1 | — |
| A | `$203F70` d1, `$2085AE` d0 | `$208562` d0 |
| B | `$300418` d0, `$30271A` d0 | `$3026CE` d0 |
| C | `$4005C0` d1, `$402574` d0 | `$402528` d0 |
| P | `$500430` d1 | — |

Gotcha found the hard way: `pkill` patterns must match how the emulator
was started; stale instances kept port 6861 and the debugger talked to an
old machine. `run.sh` now tracks its own pid.

## Data formats

All multi-byte values big-endian. Addresses in the research layout
("Disk, boot chain"). Renders checked against the reference machine where noted.

### Screens

The modules draw on one 320×200 screen whose six planes come from the
loader's `AllocRaster(320, 1800)` (plane n at raster + n × 8,000; T
`$601082`). T's NewScreen (`$60154A`): 320×200, depth 6, ViewModes
`$4800` = **HAM** + sprites. Rows 0-11 hold the window's gadget bar
(QUIT, pause box `P`, the progress bar, Fame/disk gadgets).

### `MindB` — title picture (32,000 bytes)

Five planes of 320×160, plane after plane (6,400 bytes each). T `$602DD8`
reads them into bitplanes 0, 1, 2, 3 and **5** (table `$603350`) at row 15
(+600 bytes). On the HAM screen that gives data bits 0-3 and control bit 1
only: control 00 = palette colour, 10 = keep green and blue of the pixel to
the left and set red to the data value. Palette: 16 colours at T `$6032B0`
(colour 0 white). Render (`tools`, `mindwalker_assets`) matches the
emulator's title picture.

Rebuild path (developer leftover, T `$602E78`): if run, it reads
`ram:diag1` (a $92-byte header, then 200 rows × 5 planes), remaps colours
through `$6032F0`, shades runs of equal colours with red-modify values
from `$603330` (0,2,4,…,F,…,2), and writes the planes back to `MindB`
from row 40. The shipped flow only reads `MindB` (title `$602C70`).

### `MindF1`…`MindF6` — portraits (8,000 bytes each)

One plane, 320×200, 40 bytes per row. P `$50574C` reads 7,520 bytes (188
rows) into plane memory at row 12. Six drawings of the protagonist with
handwritten lines: (1) "They're listening to us, you know." (2) "7 years
in therapy for this?!" (3) "I want my mother." (4) "Is that you, Mom?"
(5) "Sometimes I feel so drained." (6) "I'd like to go home now."
Which portrait P shows when: open (hypothesis: by shared +$5C, which C
raises by 7 per success up to 42).

### `MindHighs` — high scores (256 bytes)

32 entries of 8 bytes: score (long), initials (4 bytes, zero-padded).
T `$602196` reads it (clears the table when the read fails), `$602138`
writes it. The shipped file has two entries: 3498 "GWM", 9247 "GWM".

### `MindWorldA`…`MindWorldH` — saved worlds (784 bytes)

The whole shared block (see "Shared block"), written by T `$6026AE` and read by
`$602618`. It contains the loader's chip-memory pointers; the port will
use its own save format.

### Shared images (module S, copied to chip memory at start)

S `$7005A8` copies five image sets from its DATA hunks into chip memory
(`AllocRemember` CHIP|CLEAR, sizes at S `$700A6C`) and stores frame
pointer tables in the shared block. Each frame is a **16-pixel-wide,
2-plane image stored plane after plane** (rows of plane 0, then of plane
1; 2 bytes per row per plane). Each set has a 1-based frame index list
before its frames (the order the games step through).

| Shared | Source (S) | Frames | Rows | Index list | Pointers | Seen as |
| --- | --- | --- | --- | --- | --- | --- |
| +$76 | h9 `$700B04` | 27 | 22 | h9 `$700A94` | 28 | walker (player in A?) |
| +$E6 | h12 `$7014D4` | 27 | 19 | h12 `$701464` | 28 | crawling creature |
| +$156 | h15 `$701D60` | 19 | 22 | h15 `$701CF0` | 28 | a second figure |
| +$1C6 | h18 `$702540` + `$702B30` | 19 + 32 | 20, 6 | h18 `$702400` | 60 | a third figure + small frames |
| +$2D0 | h21 `$702E48` | 10 | 14 | (in order) | 10 | ten symbols |

+$2B6 points past the +$1C6 frames. Small one-plane icons (S h6):
+$2F8 `$7009DC` 32×10, two planes (the emblem in A's bar), +$2FC `$700A2C` 16×10 (a figure:
lives), +$300 `$700A40` 16×10 (box), +$304 `$700A54` 16×12 (the ▶
pointer of the progress bar). Which module uses which set as what is
open; the colours come from each module's palette (sprite colour
registers 16-31).

### Module images (Intuition `Image` structures)

- B h18 `$30273C`: two `Image`s, 70×70×3, PlanePick 7, data in B h15
  (`$301494`, chip hunk, 2,100 bytes each, plane after plane, 10 bytes per
  row): the player's left and right hands in the tunnel. B `$30250C` draws
  them at (64,125) and (180,115), then blits plane 3 = planes 0|1|2 over
  their rows (`$3025AA`, minterm $FE) and plane 2 |= ¬(plane 0 | plane 1)
  (`$30263C`, minterm $AB): a hand pixel of value v shows colour 8 + v
  (B's skin tones), the space around them colour 4.
- C h48: eleven `Image` structures at `$4078C4` (20 bytes apart) whose data
  pointers C `$406D28` fills with chip copies: four 32×32×4 cells
  (`$406F34`, `$407134`, `$407334`, `$407534`; each also used by a 32×32×3
  `Image`, PlanePick 7), a 32×10×4 (`$407734`, top edge 18: the green pad
  under the player), a 32×15×4 (`$4077D4`, top edge 8: a pyramid), a
  32×32×0 placeholder, and a 16×14×4 (`$406EC4`, the `Image` at `$4079A0`).
  C changes colour 0 at run time (the arena floor is not palette colour 0).
- T h6, A h18, B/C/P h6: proportional gadget images (the bars in the
  window title) — not game graphics.

### Palettes (LoadRGB4 tables, 12-bit RGB words)

| Module | Address | Colours |
| --- | --- | --- |
| A | `$2004AC` | 32 (also h6 30 colours, h10 BSS fades) |
| B | `$3002E8` (h3) | 32 |
| C | `$40042C` (h3) | 32 |
| P | `$5002F4` (h3) | 32 |
| T | `$600AD8` (h3) | 16 (HAM base); title `$6032B0`, blank `$603290` |

### Sound data

Copied to chip memory and played on Paula directly (audio registers):

| Module | Data | Bytes | Treatment |
| --- | --- | --- | --- |
| B | h33 `$3034DC` | 2,000 | samples × 8 at copy (B `$3032A8`) |
| C | h33 `$4050DC` | 4,000 | copied as is (C `$404DEE`) |
| C | h36 `$406094` | 1,000 | copied as is; played on channel 2 |
| C | shared +$1C6 | 2,600 | image memory played as noise on channel 3 (set-up just before C `$404D2C`) |
| P | h24 `$5044BC`, h27 `$504CD4` | 2,048 each | × 4 at copy |
| P | h30 `$5054EC` | 384 | six 64-byte waveforms |

Periods, lengths and the sound logic are not read yet (open).

### Texts

Intuition texts and strings in DATA hunks: T (title credits, menu,
command screen, save dialogue, end-of-game "Your score was:"), A (the
creature/terrain names "Nymphs, Spriggans, Wizards, Humans, Pyramids,
Tubes, Crystals" and "Fill In Entire Map"), C ("Score", Strength,
Temperature), P (32 words "Love … Satisfaction", pointer table `$501CD2`,
picked by `& 31`; "Your Score:",
"Self-Development Bonus:", "Playing Time:", "Extra Lives:"), E (the error
messages). They are data: the port reads them from the modules.

## Stages, shared block and rules

Seven compiled-C modules run one at a time by the loader (see "Disk, boot chain"). The
game state that survives between modules lives in the loader's **shared
block** (784 bytes at `$7C04F4`); everything else is per module. The port
keeps that split as concepts (a game record handed from stage to stage),
not as a byte image.

Prior exposure: none for this game (no outside material read; RetroReverse
has nothing on it). The game's general premise is public knowledge from
its packaging era; nothing here relies on it — every point below comes
from the code or from the reference machine.

### Stages seen on screen (reference machine, 2026-10-27)

| Module | Seen | Notes |
| --- | --- | --- |
| S | "Loading Mind Walker..." | sets shared +$10 = `$F699A3B2`, mode 0, +$5C = 0 |
| T | title: brain picture (`MindB`), "Mind Walker -by- Bill Williams", "Please press the left mouse button to continue.", © 1986 Commodore/Amiga, © 1986 Synapse | HAM screen |
| T | world menu: Preset 1-4, New Game over colour-cycled HAM pillars | presets = fixed seeds `$600BC6`; New Game = seed from `CurrentTime` |
| A | perspective grid, floating green/tan platforms on blue pillars, player figure, a tree, a water block; bar: lives figures, compass N E S W, emblem | joystick |
| B | octagonal tunnel in first person, two hands (B images) | joystick |
| C | top-down arena with cell blobs, the player firing, panel "Score", gauges Strength and Temperature | joystick |
| P | seven picture tiles, colour bar, creatures in a V, "Your Score", "Self-Development Bonus: 42000", text box | mouse |

Between modules the outgoing module shows "Loading..." with a line-art
arrow (B and C have the text).

### Shared block (first reading; offsets from `$7C04F4`)

| Offset | Meaning | Evidence |
| --- | --- | --- |
| +0 | the 320×1800 raster (six screen planes) | loader `$7C0108` |
| +4 | status (0 go on, 1 to T, 2 unset, 3 error, 5 running) | every module's exit |
| +8 | the screen (S opens it) | S main |
| +$C | the 336×200 raster | loader |
| +$10 | world seed | S default `$F699A3B2`; T: presets `$600BC6` |
| +$14 | mode → next module | loader switch `$7C02AC` |
| +$18…+$58 | A/T only (A's world state; to read) | |
| +$5C | progress 0-42 (C +7 per success, capped at 42; T sets ≤ 35 on a new game) | C main, T `$600040` |
| +$60 | flag set by C's success | C main |
| +$62, +$66, +$6A, +$6E | T: +$62 from table `$600BA6` (multiples of 960), +$66 = 0, +$6A = 0, +$6E = 3 (lives — hypothesis); B adds points to +$66 (score — hypothesis) | T main, B |
| +$72 | Remember key for S's chip allocations | S `$700266` |
| +$76/+$E6/+$156/+$1C6/+$2D0 | frame pointer tables of the shared images | "Data formats" |
| +$2B6 | end of the +$1C6 frames | S |
| +$2BA | stage marker: S 0, T reads (0 = first title), A sets 2, B 3 | |
| +$2BC…+$2CC | B's copy of +$62, +$6A, +$66, +$6E, +$5C on entry (restore point) | B main |
| +$2F8…+$304 | small icons | "Data formats" |
| +$308, +$30C | self-development bonus, 42,000 at a new game (P shows it); B copies +$308 to +$30C | T, B, P |

### Module entry points

- **T** main `$600040`: port, LoadRGB4, seed mixing with `CurrentTime`,
  title `$602C70` (first time only, +$2BA = 0), menus; world menu loop
  (`$6003EE` click → `$60045A` picks one of five rows at y `$600AFC`
  {12, 55, 98, 141, 184}, x 127-191); pillars `$600558`/`$6007FC`, colour
  cycling `$60094E`; new game set-up at the end (+$5C, +$62…+$6E, +$308).
  Command screen (Continue Game / New Game, Same World / Save Game, worlds
  A-H), high scores ("Fame"), end screen ("Your score was:").
- **A** main `$200040` → `$200262`: set-up (`$2096B2`, `$204312`, …,
  `$2086B0` with the start position), loop `$200262` (`$20BEE0` per frame,
  `$209D8A`, `$2003C0`, `$208420`), joystick `$208558` → flags
  `$2085D8` right, `$2085DA` left, `$2085DC` up, `$2085DE` down,
  `$2085E0` fire; done flag `$204D72`, quit `$204D6C`; exit `$20021C`.
- **B** main `$300040`: loop until `$300B64` (quit) / `$300B66` (success) /
  `$300B68`; outcome → mode; exit `$3002A0`.
- **C** main `$400040`: loop until `$401080`/`$40107E` (success)/`$40107C`
  (quit); exit `$4002F8`; joystick `$40251E`.
- **P** main `$500040`; mouse only; portraits `$50574C`; sets mode 1.
- **S** main `$700040`, image copy `$7005A8`, allocations `$700266`.
- **E** shows one of five messages with `AutoRequest`.

### Code versus data (first sorting)

- Native code to rewrite: the world generator (seeded RNG, T `$6049D2` and
  friends — the worlds are generated, not stored), A's 3D map and rules,
  B's tunnel, C's arena, P's psyche screen, the menus, sound playback.
- Data to read from the imported modules at run time: images (S sets,
  B hands, C frames), palettes, texts and word lists, menu positions,
  preset seeds, sound samples and waveforms, `MindB`, `MindF1-6`,
  `MindHighs`. Interpreters: none found so far (to confirm in A and P).

### Title stage (T), as rebuilt (`games/mindwalker_amiga/title`)

Flow of T's main `$600040` for the first title of a session:

1. `$601082`: screen (HAM, 6 planes) and window; the disk and Fame gadgets
   start disabled (ghosted).
2. Title `$602C70` (only when shared +$2BA = 0): playfield cleared from row
   11, MindB into bitplanes 0-3 and 5 at row 15, the credits (`$602D4C`: four
   IntuiText lists; "X" → "©"), the title colours (`$6032B0` via the working
   copy `$6032D0`). Loop `$603202`, one vertical blank per pass: after 80
   frames the **twinkle** (`$602B34`: shift registers `$603494`/`$603498`;
   when `(next & $296) == 0` a colour from the table `$60349C` — re-picked up
   to twice if it equals the last — flashes: level $FFF, minus $111 per frame,
   colour = level XOR its title colour until level < 1); then the left button
   (held or new) ends the title.
3. Menu colours `$600AD8` (whose values double as pens: 0-15 are blue
   levels). The seed `$600AF8` is XORed with `CurrentTime` (secs << 3 ^
   micros; 0 → `$FED45123`); two draws seed the pillar mixer `$600C18`; the
   shift register as it then stands becomes the "New Game" seed `$600BD6`.
   T also sets colour 1 of **S's** screen to green (the loading picture's).
4. `$600A5E`: ten vertical blanks (sound check). `$600558` grid, `$600694`
   item bands, texts in pen $3F (hold-and-modify green 15), pillars
   (`$6007FC`), then the disk and Fame gadgets enabled.
5. Loop `$600276`, one vertical blank per pass: gadget messages (`$600CB4`:
   QUIT $80 ends T, pause $10, disk $30 command screen, Fame $40 high
   scores), one step of the New Game seed, the click `$6003EE` (a new press —
   `$600BDA` holds the last read — with x 127-191 picks the item whose
   16-row band at `$600AFC` holds y), one pillar's red bands moved (`$60094E`:
   pillar = (mixer step >> 5) & 7; ±3 per step; triangle wave of 32).
6. New game `$6002F2`: seed = preset `$600BC6`[choice] (slot 4 is the New Game
   seed), stage = ((slider HorizPot / $618) / 7) × 7 capped at 35 (the
   compiler's helpers h32/h33 divide, h34/h35 multiply), +$62 = `$600BA6`
   [stage / 7], score and +$6A 0, +$6E 3, bonus 42000; the loading picture
   `$604AB2` on S's one-plane screen (threads `$604C9C` for stages s..s+6,
   "Loading..." JAM2) and its title bar (ShowTitle).

Gadget bar (all stages share the layout): rows 0-9 start in pen 15, rows
10-11 pen 0; GRELRIGHT gadgets count from x 319; images follow
PlanePick/PlaneOnOff; the slider's container is cleared inset 2/1 with the
knob 4 pixels in; ghosting is pen 15 on x mod 4 = 3 (odd rows) / 1 (even
rows). Texts: topaz 8 (titles: topaz 9 bold+italic; algorithmic italic
shifts a row by (baseline − row) >> 1, bold ORs one pixel to the right,
advance = width).

### World generator (A `$2096B2`, `$209482`, `$20959A`, `$209206`; `games/mindwalker_amiga/world`)

- New game: the seed's shift register gives four key words (XOR the salt
  `$209814`), one draw r for the levels (floor r+$86, plain r+$85, ceiling
  min(r+$91, $C5)), alternate ((next >> 2) & 63) + 40, start x = next & $FFF,
  start y = (next >> 2) & $FFF. Mode ≠ 0 takes the position from shared
  +$18/+$1C, the marked block from +$20/+$24 and a flag from +$60.
- Chains: per 12-bit coordinate, three mixer levels (top hex digit first);
  each level seeded with the outputs of the level above (top: keys 0/1 for
  rows, 2/3 for columns), stepped in pairs digit + 1 times. The renderer
  (`$2086B0`) adds row and column values over a 6 × 6 window of cells and
  takes level-1 words for the terrain kind (to rebuild with A).
- Platform cells: u = x + y, v = x − y (mod 4096); band 0 of u or v is open;
  height = ((key1 >> (u/64 & 15)) ^ key0 ^ (key3 >> (v/64 & 15)), XOR v >> 10
  when u bit 10 is set) & 3, + 1; flags centre (bands 2/2), beacon (u, v ≡ 40
  mod 64), marked block.

### Landscape (A): first reading

Set-up `$200262`: world `$2096B2`, screen and bitmaps `$204312` (A draws
into a 4-plane bitmap at raster +8000..+39999 and blits to the displayed
5-plane bitmap at +40000..+71999 with plane 4 at raster +0, `$208420`), the
map window `$205A14`/`$205A36` (64 × 64 cells around the start, one-plane
bitmap; `$205B90` marks, `$205B10` reads), `$206944` (new game: view for the
cell left of the start, the walker's form from the view's kind at
`$202F60` matched against `$206B50`; else from shared +$48/+$58), the view
`$2086B0`, `$20BDD4`, colours, `$203EF0`, `$201492(2)`. Loop: `$20BEE0` (sound
service), `$203FD0` (window messages: gadgets, $200 close → quit),
`$209D8A` (movement/height following; to read), `$2003C0`, `$208420` (blit
to display).

View builder `$2086B0` (`landscape/view_cells`): the 6 × 6 window around
x−1..x+4, y−1..y+4 sums the row and column chains (level-0 words; level-1
words XORed and masked to $C000 for the surface kind); for the 4 × 4 cells
x..x+3, y..y+3: a diagonal mean of the level byte where the key bits of row
and column disagree, the surface following a diagonal neighbour when bit 3
is set, the mix word's overrides, levels clamped by the world's floor,
plain, alternate and ceiling, platform kinds (`$20984C`), beacons, finds
from stage 14 (`$209856`, taken flags `$2099BC/BE`), creature places from
stage 7, landmarks ($400), stage markers ($800/$A00, `$20985A`), empty cells
($40, `$20986A`), the marked block ($100); then the colours: 1-15 from the
position and keys with guns rearranged by one of four shift sets
(`$20981C/$20982C/$20983C`), 12/15 for $2000 markers/water, 17/18 kept
(`$209884`, `$209886`), 20-31 a ramp whose step depends on the previous
view's distance from a block's middle.

#### Drawing the view (first reading, 2026-10-27)

`$201492(mode)`: copies the colours to `$209890` (h10), clears the drawing
RastPort (`$204D5C`: 4 planes at raster +8000), `$2015F4` (= `$209AD4`
then `$207E1A`: floor grid and sky, to read), `$20129A` (turns the 4 × 4
tables by the facing direction `$203070` 0-3 into view order at
`$202F70` terrain / `$202FF0` objects / `$202FB0` (from `$202F90`, filled
elsewhere), gives each cell a lattice place `$203030` (column 0-8) /
`$203050` (row 0-8) and a shade `$203010` = column + row of the turned
cell, 6 → 5 with water, 3 → 2 with $2000 markers, 4 → 5 when anything is
marked in view), the bar items while lives remain (`$205218`), the cells
(`$201758(1, mode == 1, $20105C)`), the walker for mode 2 (`$206728`),
LoadRGB4 of the view colours, ClipBlit (minterm $C0) of rows BorderTop+1..199
to the display, then a second pass `$201758(0, 0, $201292)` into the drawing
bitmap cleared to 15 with mask bits per row (an occlusion mask for the
walker sprite — hypothesis).

`$201758(colour pass, callbacks, callback)`: rows 3 (2 in the mask pass)
down to 0, columns in the order of `$202E88` {3, 2, 0, 1}; cell centre X =
column × 80 − 120, Z = row × 80 + 64, level = terrain & $FF; corners
(X ∓ 40, Z ± 40) projected by `$202E2C`: x = X·256/(Z+256) + 160, y =
level·256/(Z+256) (`$202606` gives the four corners: far left, far right,
near right, near left). Per row from data `$202E9C` (157, 126, 105, 90),
`$202EA4` (180, 140, 114, 96), `$202EA6` (the next row's). Kinds: $4000 a
trapezoid (AreaMove/Draw with outline when marked), $8000 four vertical
lines to the top (pillars), ground a RectFill box under the top face; the
top face an area fill in the pen from the shade (9 + shade; 13 beacons,
15 water at the floor level; 9 for $1000 markers), outlined for landmarks
($400), kind $4000 or marked cells; $800 cells get two triangles (a ramp);
landmarks and empty cells get `$2027F2` decorations (shape from `$202FB0` &
7) and landmarks `$2026D6`; visited cells `$205158`; ground and water get
side faces depending on the column (0 or 3). Area fills use graphics'
AreaEnd (TmpRas `$204DAE`, outline pen at RastPort +$1B, AREAOUTLINE flag
bit 3 of +$20) — a faithful model of its rasterisation is needed for
pen-exact pictures. Callbacks: `$20105C` (objects, to read), `$201258`
(one colour per row from `$202EAE`), `$201244`, `$201122`.

Loading pictures: the port holds them for a fixed 2 seconds (owner's
decision, 2026-10-27: long enough to see the picture; the original's disk
loading took much longer and is not matched).

#### View painter v1 (2026-10-27, `landscape/view_scene`, `landscape/camera`)

Owner's decision: the landscape need not be pixel perfect, and the camera
must be replaceable by a freely rotating one later. So the scene places the
cells in world units and `FixedCamera` is the original's projection; the
direction turn (`$20129A`) stays a step before projection. Drawn: the floor
grid (`$205218`: 18 rows `$2058A8`, 12 converging pairs `$2058CC/$2058E4/
$2058FC`, horizon 31, pen 14), per cell back to front: pillar lines ($8000),
pedestal trapezoid ($4000, half widths 5 − row and twice that at the row's
top line), ground/water box to the row base, the top face (pens as above,
outlines for $400/$4000/marked), outer columns' side faces, visited
markers, pyramids ($1000, pens 4/3/1). Polygons use our own scanline fill.
Opening view of Preset 1 (`mindwalker_replay view`): 929 of 60,160 pens
differ from the dump (direction 0, drawn from display row 10); the rest is
the not-yet-drawn objects (tree, star) and some outlines.

#### A's bar (`landscape/landscape_bar`)

A's window (NewWindow `$204B1E`, 320 × 12, BlockPen 18, WINDOWCLOSE; IDCMP
CLOSEWINDOW quits A): its title bar in pen 18, gadgets: pause box (x 14;
image chain with two depth-0 fills on/off $12 and the box icon on plane 4),
slider (x 30; knob on planes 1/4; HorizPot = stage × $618, `$204312`), life
figures (x 126; five Images from S's +$2FC; `$203E74` sets Depth/PlanePick
so figure i shows while i + 1 < lives), compass letters N E S W (GRELRIGHT
−103/−87/−71/−55, IntuiText pens 0/18, the facing one's FrontPen 31 by
`$203EF0`), the emblem (GRELRIGHT −31, S's +$2F8 as 32 × 10 × 2 on planes
2/3). Equal to the dump pen for pen at x 14-319; x 0-13 is Intuition's
window close gadget (ROM imagery at `$FD5DD0`, 16 × 10 × 2; not embedded:
waiting for the owner's decision).

#### View objects (rebuilt 2026-10-27)

Drawn by `$201758` after a cell's faces, at the far edge's middle
(X, level, Z + 40) unless noted: landmarks ($400) and empty cells ($40) a
line pattern in pen 0 by mix & 7 (`$2027F2`, via `$202970` cross and
`$2029EC` ring over corners or edge middles), landmarks a star in pen 13
twelve levels above the centre (`$2026D6`); trees on $2000 cells (`$205538`,
pen 12: shift register seeded (m ^ m << 4) | ((m ^ m << 4) ^ $F27E) << 16,
trunk height `$205918`[row] & next, three branches `$2053D2` recursing to
depth 2 with halving size); ramp poles on $800 cells (`$202A82`, pens 5-8)
or else grass on cells at floor level (`$205652`, pen 9, seed m | (m ^ m <<
2) << 16, 1-8 tufts of three blades at the far left corner); pyramids
($1000). The mix word per cell is the builder's `$202F90` (Ghidra showed
the store as `*extraout_A0_00 = local_bc`). Not drawn yet: creatures
(`$202B00`, objects & 4), finds (`h20` `$204E08`, & $30), the marked-block
ramp form (`$202CD4` + triangles). Opening view of Preset 1: 675 of 60,160
pens differ (fill edges); the bar equal.

#### Walking (rebuilt 2026-10-27, `landscape/landscape_stage`)

Rebuilt from `$200262` / `$209D8A` / `$20A45C` and their routines; inputs
are taken at the original's read points (`InputReader`: the button every
frame, stick and fire on stepping frames) so replays count reads like the
recorder. Start (`$20AEE4`): facing north, x 120, z 40, level 0, the view's
first column left of the start; form from the surface of the view's cell
[2][0] (`$206944`, kinds `$206B50`: $4000 → 0, $C000 → 1, $8000 → 2, ground
→ 3) with its sprite rows `$206B1C` (21/19/18/21), colours 17/18
(`$206B30`/`$206B38`) and image set (+$76/+$1C6/+$E6/+$156). Height
following, speed, frames (`$20ABB6` 4-frame cycles, `$20AC7C` form 2,
`$20AC24` swimming; idle fidgets from h51 `$20B2B0`), the view shifts by 4
cells (`$20ACFA`/`$20AD78`) and re-anchoring (`$20B18A`, only when the last
marked cell was not in the front row: `$20B318` is set by `$20A12C` at
marking), the no-step-up rule, cell events (`$209F34`: the marked block's
ramp ends A, landmarks re-centre the map, beacons mark their block, cells
of the form's surface next to the map join it for a point), the 300-frame
wait after a click (`$203F58`, ended by gadget messages), compass turning
(`$204278`), the sprite place (`$209B54`/`$2057D0`: view-turned position,
depth + 24, projected, x − 8 clamped `$209D4E`; in water at the floor level
and shortened).

Harness fix: JOY1DAT encodes down/up as bit 1 ^ bit 0 / bit 9 ^ bit 8; the
first walking recording had "right" as right+down.

Verification: `a_walk` (Preset 1, the button released at A's first read,
1,900 reads of stick moves: forward, right, diagonal, left, back, rests):
the walker's record, the view and the direction equal the original for all
808 frames (1,901 reads; score 18) since the flying creatures were rebuilt
(before: 574 frames, until the first creature attached and raised the
speed limit).

#### Flying creatures (rebuilt 2026-10-27, `landscape/creatures`)

A's module at `$206BA8`: twelve creatures shown as attached hardware-sprite
pairs (colours 16-31), slots 0-5 and 6-11 in two columns of up to six
horizontal bands. Data: shift registers `$207F80/84/88` (seeds as stored),
directions `$207F14`/`$207F34`, attach masks `$207F54` (by creatures
attached), follow speed `$207F5C` (by stage / 7), depth limits h39
`$207EE4`, kind images `$2004EC` (frame k and k + 1 of S's +$2D0 set),
damage per catch `$203E2C`, head rows by form `$206B40`.

- New view (`$2070A8` after the step's view shift, after the score gets the
  last view's bonus `$208132`): history record (`$2070BE`, 8 views, kinds
  present), reset, set-up `$2071AA`: the platform block and its distance
  (`$209206`/`$2093B0`) at the view's middle (+2, +2) give bands =
  clamp(6 − distance / 12, 2, 6), a start chance (64, or 192 − 2·distance
  ≥ 7 on platforms) and an appearing chance out of 4096 (1, or 63 −
  distance ≥ 7); views with creature places: all six bands, start 256,
  mask $FFFF, bonus places × 25. The marked block in view (`$2099B4`)
  keeps them all away. A view of the last seven brings back its kinds.
  The stage's first view gets no set-up (nobody enabled).
- `$207722` every frame the walker moves (in `$2003C0`): every 5th frame
  (plus one per creature attached) free ones fly (`$2079BE`: appear from
  either side; new course 1 in 16; attach when visible in sprite row 11
  (bits $7E0) and at least 60 px from the walker, with a mask chance;
  bounce off the sides and the band) and attached ones creep their
  tether's end to the walker's head (`$207810`), catching within 9 px.
- `$206DD4` moves the sprite (which is not its flight position: a new
  creature waits off screen until its first move) and rebuilds the image
  masked by the rows in front (depth 1-3; second paint pass `$201758(0)`,
  rebuilt as `ViewPainter::depth_masks`).
- The frame after a catch (`$209D8A`, flag `$20B356`): sprites hidden,
  history cleared, damage `$20052E` += table; at 1600 a life (+$6E) is
  lost (0 ends the stage, `$204D6C`) and the view empties; h14's picture
  (a growing pattern, colour fades) is not rebuilt yet.
- Tethers: triangles from the creature (+7, +7) to the end ± 8 in display
  plane 4, cleared and redrawn every (round & 3) + 5 frames (`$2003C0`).
  Colour 13 flashes 0/8/F while the view has landmarks, a beacon, creature
  places or a find.
- Attached creatures raise the walker's speed limit (`$2083AE + $206B6C`).
- `$20ADF6` (stage start) draws two numbers from h51 before walking starts.

Verification: `a_catch` (a_walk's route to read 1400, then 2,000 reads
standing still, then forward; recorder peeks `creatures` $208128..+$290 and
`damage` $20052E): 1,517 frames, 4,001 reads, every creature's presence,
kind, flight position, depth, tether end, the number attached, the view
bonus, damage (two catches: 390, 780) and lives equal the original every
frame. A screen dump at read 2898 (`work/ref/a_catchd-creat.raw`; planes
only, no sprites) shows the two tethers where ours are; it also shows the
score text (`$20655A`), since rebuilt. `a_life` (10,601 reads: four catches,
then walking back and forth across views) equal too. Not exercised yet (see Open items):
views with creature places, the history bringing kinds back, the
marked block keeping creatures away.

#### Score text, catch picture and reveal (rebuilt 2026-10-27)

- Score (`$20655A`, called first by the grid painter `$205218`): shared
  +$66 in decimal without leading zeros (0 prints nothing), IntuiText
  `$2066CE` (pen 14 on 0, JAM2, topaz 8) at (7, 11) of the drawing, before
  the grid and the cells. Pen-exact against the original's dump at
  a_catch read 2898. The finds' icons beside it (`$204E7E`/`$204EC6` at
  x $112/$124 when `$2099BC`/`$2099BE`) wait for finds.
- Catch picture h14 (`$2039B8`; `landscape/interlude`): colours h15
  (`$203DF8`: 16 pens 16-31) set to cyan (0, i, i); the tether layer cleared;
  16 steps, each drawing the face (`$203E18`, 16 × 10 bits) into the
  drawing's plane 0 as blocks of size + 1 pixels and blitting it into all
  five display planes as pen h15[step] (minterms $FC / $0C). Survived:
  size damage / 100, shifted one pixel down-right each step. Lost: size
  17 − step about a fixed point, then colours 0-15 fade a nibble step at a
  time (18 steps of 8 frames), damage 0, lives − 1 (0 ends the stage), h17
  refreshes the bar. The bar turns cyan too (its plane-4 pens are 16-31).
- Reveal h32 (`$206728`, the paint's mode 2: after a catch and at every
  stage start): 22 steps; the four quadrants outside the opening scroll
  outwards (6 across, 4 down; ScrollRaster), then the new drawing's middle
  columns (12 a step) and rows (8 a step) are copied in; the screen's
  RastPort has all five planes, so plane 4 (the face) scrolls along
  (hypothesis: the 4-plane copy clears it where it lands) until `$200E62`
  at the end; then LoadRGB4 of `$2004AC` and the full
  copy. With no lives left the paint draws no view.
- Timing (measured, `harness/catchtime.py`: a forced catch, the caught flag
  `$20B356` poked, vertical blanks at the loop heads; `work/ref/
  catchtime_survived.txt`, `catchtime_lost.txt`): face step 6 frames (any
  size), fade step 8 + 4 after, paint 19, reveal 59 over 22 steps (2-3 each),
  11 after. No input is read meanwhile, so the replays still compare read by
  read.
- Checked: the original's dump at h14's end of a_lives' last catch (lost:
  face steps and the fade done) equals ours but for the walker sprite (121
  pixels; dumps hold no sprites).
- Replays: `a_life` (four catches, walking across views), `a_stage21`
  (stage 21; no creature appeared: no platform blocks near), `a_lives`
  (damage poked to 1500 before catches: three lives lost, the original
  then returns to T and ours ends the stage) all equal read for read;
  the replay takes stage, lives and score from the log's shared block and
  applies damage pokes.
- Vertical-blank counter for the harness: the stub refuses reads of CIA
  and custom registers, and library calls from an interrupt stop crashed
  the machine; `mw.install_vbl_counter` links a server (code, node and
  counter in the unassigned vectors $C0-$FF) into exec's VERTB list
  (SysBase + $54 + 5 × 12) while the machine is stopped. `mwrec.py` logs
  `vbl=` at every read.

#### The walker's mask (rebuilt 2026-10-27, h11 `$20309C` / `$2035D2`)

The walker is a hardware sprite; each placing (`$209B54`) masks its image
by blits. `$2057D0` sets the walker's row and column facing the view
(`$20B318`/`$20B31C`, from its position; also written at marking by
`$20A12C`). h11 first builds plane 0 of the mask bitmap (the drawing,
reused after the second paint pass `$201758(0)`, where row r's cells clear
plane r + 1): the sprite's box set (x..x+16, top..top+height), then cleared
by the marked block's ramp pole in the walker's cell (terrain $800, and
`$2099B6` or not $100) when the walker is behind its middle (row × 80 +
$40 < depth + 24): x ± 9 from the top down to the pole's projected top; and
at the edge columns 0 / 3 by the side wall of the turned view's column 1 /
2 cell in the same row when that is higher than the floor and more than 8
above the walker (a quad at X ∓ 80, z row × 80 + $18..$68, widened 16 at
the front, down to the walker's feet for kind 0). Then sprite = image AND
plane 0 AND the depth minterm of its row (`$2039A0`: planes 1..row). Both
sprite planes zero (blitter BZERO) counts a hidden placing (`$20B386`);
from the 9th the marker `$20B2B4` (8 rows) replaces the image's top. Rows
above the screen top are left as they were.

Verification: the recorder logs the sprite data (behind `$20B2E8`).
Since the fill follows AreaEnd (`screen/area_fill`, see the tunnel
section), the masked rows equal the original's in all recordings.
The marker has not occurred in a recording yet.

#### Zapping (rebuilt 2026-10-27, `$20A45C` with fire, `$2061BA`, `$206F40`; `landscape/zap_bolt`)

Fire held: the walker stands. At the start (`$20B376` set, a long) the
cursor `$20B37A/$20B37E` is the walker's sprite place and colours 19-31 are
set to (F, i − 16, F); any later full repaint (`$201492`/`$201602`, a
catch's reveal, a view change) loads `$2004AC` back, so they stay lit only
until then (a cell's marker, `$204F48`, does not repaint). Each frame: the
cursor moves 15 pixels with the raw stick (up = up), clamped to the screen;
the walker faces it (|dy| < |dx|: frame 0 / 4 by dx's sign, else 8 / $C by
dy's; form 2: frame 5); the sprite is placed (h11 runs twice that frame:
the hidden count too); the walker's point again without the −8
(`$20578A`); the creatures' timer is raised to 3 and the tether clock to 6
(so the layer is cleared soon, often within the frame); the bolt from 10
rows above the walker's point to the cursor, drawn as lines into the
tether layer: `$2060BC` splits a segment wider than 8 at its middle and
moves the middle by (mixer % spread), signed by bit 6 of the next draw,
clamped to rows 10-199, the spread halving (0 gives 0, the helper's /0);
spread $1F; every 16th frame or when the mixer & $142 is 0, four branches
from the cursor towards points between it and the corners (0,11), (319,11),
(319,199), (0,199) (`$206388`: ¾ of the way, then once or twice more
halfway by chance), spread $F. The mixer `$20648E` is seeded at the stage's
start by h47 from two h51 draws (`$20ADF6`). Then `$206F40`: the first
present creature not of kind 4 within 20 of the cursor (around its middle,
+7) is gone (untethered and one fewer attached if attached); the view's
bonus grows by 3 (free) or 5 (attached) × the follow speed.

Verification: `a_zap` (a_walk's route, two zapping stretches; recorder logs
the mixer): 3,001 reads equal: cursor, aiming, frames, mixer every frame,
one creature zapped (bonus 3), a catch during the zap. Dumps at two stick
reads mid-zap: the bolt's path pixel for pixel like the original's, and
the colours; one dump falls on a frame where both had already cleared the
layer. The attached-creature zap (× 5) has not occurred yet.

#### The map (rebuilt 2026-10-27, `landscape/map_view`)

The emblem is gadget 4 (`$2049F0`: GRELRIGHT −31, 32 × 10, TOGGLESELECT,
GADGHCOMP); the P box is gadget $10 (`$204AAE`, pause, `$204106`: S's
screen with the loading picture's threads, not rebuilt). `$2042A0`:
selected → `$20071C(1)` (h5 keeps the view's objects and creature places;
h50 and `$207E1A` hide the walker and creatures; the view area cleared
(RectFill on the screen's RastPort: all five planes, so the tethers go
too, though the tether timing is not told); LoadRGB4 of h6 `$200EA0`, 30 colours (the bar's 16-29 change too); the
IntuiText chain `$20100A` at (0, top + 2): "Fill In Entire Map" and the
legend Crystals / Tubes / Pyramids / Path, Humans / Wizards / Spriggans /
Nymphs; grid lines pen $1D every 20 pixels from row top + $1D; squares
columns 5-10 × rows 2-5; the view's square outlined in pen 16 at
150-170 × top + 99..119). While `$204D6E` is set the main loop runs
`$200D92` instead of the walker: colour 16 = h7 (counting up, two WaitTOFs
a turn); with the button held and the pointer at window row ≥ $27 the
square under it is drawn instead. `$200A36`: square (c, r) is the view
built at (view − 30 + 4c, view − 14 + 4r) (`$209980/82` restored after),
skipped when its middle block is not pen 0; its 4 × 4 cells as 5-pixel
blocks at x 20c + 5x, y top + $B8 − 20r − 5y. `$200BF0`: beacon 16; marked
block ($100 without $800) 29; else the kind's base (0 → 1, $4000 → 6,
$8000 → 11, $C000 → 20) + 4 for visited, else + 1 ($400), + 2 ($800), + 3
(other $1C00 bits). `$2005C2` (the button's edges via `$204250`): release
reloads h6; press at screen rows 10-18, x $59-$E7: draw every square
(`$20101E`); rows 20-29: colours base + feature (x / 80 → +1..+4) for the
five bases (1, 6, 11, 20, 24) yellow; rows 30-38: the kind's five colours
yellow. `$200D02` (deselected): the view built again (from the palette
the map's builds left) with the kept objects and creature places, the
tether layer cleared, `$201492(0)`, creatures shown. Compass gadgets are
ignored while the map is open; a click does not start the 300-frame wait.

Timing (vertical-blank counter): opening 79 frames, a cycling turn 2, a
new square about 3 (filling 102: 304 frames), closing 31; squares appear
one by one at that pace in the port.

Harness: `mw.post_gadget` posts a GADGETDOWN IntuiMessage (dummy
PA_IGNORE reply port, both in vectors $100-$163) to A's window port and
toggles the gadget's SELECTED flag, since Intuition cannot be clicked by
poking; `mouse` events also set the windows' MouseX/Y. `a_map`: 801 reads
equal; six dumps (open, a feature and a kind highlighted, squares drawn
by holding, filled, closed) equal pixel for pixel outside the emblem.
The emblem's complement while selected follows Intuition's GADGHCOMP
(hypothesis: the posted message bypasses Intuition's drawing, so the dumps
cannot show it).

#### Finds, creatures' places, beacons, the way on (rebuilt 2026-10-27)

- `$209F34` in full: the marked block's ramp ($100+$800) with the way on
  open (`$2099B6` = 0: set to 1 in a new game, 0 when a beacon marks its
  block; loaded from shared +$60 when continuing) ends A and writes the next
  world's seed to shared +$4C = (cell_y ^ terrain) << 16 | (cell_x ^ mix),
  0 → $5AFE32CC. Finds (objects $10 "E=", $20 "mc²"; from stage 14):
  `$20A1B0` sets `$2099BC/BE`, both → `$20052A` = $200 (the creatures move
  every (n/64 + 1)th frame while it counts down, `$200370`; at 0 both finds
  are back), the view rebuilt without the find (`$20A204` 2 keeps objects
  bit 8 and the places' count). Creatures' places (objects 4, from stage 7):
  drawn by `$202B00` (a kite on a line up, a column of black triangles
  outlined in pen 13, pen 1 once ticked off with bit 8), stepping on one
  ticks it off and counts `$2099AC` down; while it is not 0 the walker
  cannot leave the view (`$20ACC4`), pushing at its edge resets the tether
  clock and counts `$20B382` (a sound every 8). Beacon (`$20A204` 0, needs
  a neighbour on the map, so a trail on the form's own surface):
  `$2099B4`/`$208130` set (creatures stay away), presence cleared,
  tethers cleared, home and marked block, `$206508` time bonus 2000 −
  min(seconds, 1900) (CurrentTime, shared +$6A, the pause left out), damage
  0, `$2099B6` = 0, every view cell | $100, four repaints with stars.
- Drawing: finds' signs in pen 13 (h20) and beside the score (pen 14,
  `$204E7E`/`$204EC6`); the open ramp: the top as a slope plus a short
  capped post (`$202CD4`); pyramids outlined in pen 0 (AREAOUTLINE stays on).
- Repaints as sequences (`interlude`), measured with `harness/viewtime.py`
  on a view change: h8 at 4/9/15/22 frames (one display plane cleared below
  the bar per row, colours $202EB6[row] blacked out in the new palette, the
  grid drawn again), colours + copy at 30, done at 41; plain (compass, map
  close) 31; `$201602` with `$201244` (colour 0 through $202EAE per row and
  in the mask pass) or `$201122` (also a star of 9 rays, $202EBE/$202EE2 ×
  8·(4 − row), around the event cell 12 levels up, colour 13 white).
- Pyramids (`$2069E8`): more than 30 step frames on $1000 cells (the count
  carries over) → `$206A2E(form + 1)` (height, head, colours 17/18, frames
  reset, the form's sound h36 $206B18).
- Pause (P box, gadget $10, `$204106`/`$204196`): the screen's title bar
  (NewScreen `$204AFE`: DetailPen 0, BlockPen 18, title at (2, 7)), the
  window down 10 rows, the emblem OffGadget-ghosted in the bar's background
  pen, no reads (Wait); resuming: view cleared, 30 frames, `$201492(0)`.
  It also draws the next loading picture on S's screen (behind; not shown).
- Verification (recordings with `mindwalker_replay sim` routes, `world
  scan`/`grid`): `a_finds` (stage 14, both finds), `a_places`, `a_wayon`
  (a planned trail to the beacon at a28 b40 and its ramp a29 b41: seed
  $40D8B950 equal; bonus within one point: the replay drives the clock from
  the original's frame counter, the CurrentTime phase is unknown),
  `a_pyramid` (four form changes), `a_pause` (hook-posted resume click):
  all equal read for read; dumps equal but for sprites. In play the bonus
  follows our own pace (no slowdown: about 4 points more on that trail).

#### Sound (rebuilt 2026-10-27, `landscape/sound`, `amiga/paula_voices`)

timer.device-paced (`$20BEE0` polls it; `$20BF26`): period 80 000 − stage ×
$618 µs (word; at least 25 000) + `$20052A` × 200. Sequencer `$20BFA6`: parts
`$20C710` (end word, pattern indices A/B/C; parts ≤ $10 music, above effects;
26 → 27 chain), patterns `$20C648` (low nibble ticks, high nibble volume
>> 2 or 3), `$20C67C` (note, bit 7 slide +10/tick and decay 1 instead of 3),
`$20C6B4` (periods, << loop bit 0); music periods × `$20C80A`/`$20C812`[key
& 7]; the key steps every 4th loop; effects `$20C7D0`/`$20C7DD` save and
return; ring of 16 at delays (13, 1, 15, 0) and volume shifts (1, 1, 1, 0) on
the wave h60 (byte 0 = 0 or $81 by loop bit 1); the blip (`$20C898`) on
channel 0 with the square $81/$7F. Aim synth (`$20B858`/`$20B902`, while
`$20BBE0` ≠ 0, music parts only): a 34-sample pulse wave with wandering
width, pitch 0x9C4 − `$20BBCC` ± vibrato, volumes panned by the cursor
((319 − x)/5 + 4, x/5 + 4); noise = image memory (S's +$1C6 frames from
frame seq[0]) on channels 2/3 for bolts (`$20BBD8`/`$20BBDC`, h57) and
hits (`$20BBE4`). h56 rumble during `$205EC4` (the timer not served).
Effects: 1 find, 2 landmark, 3 creature's place, 4 edge push, 6/12 catch,
7 after it, 11 beacon, h36 per form. Verification: the recorder logs
`music`/`aimsnd`; `a_zap`: the original's sampled sequencer states are a
subsequence of our ticks for 154 states, 196 of 267 by LCS; the rest fall
in stretches where the original runs slower than 50 fps (its timer runs on
real time) or has no reads (the catch picture).

#### Walking (first reading)

`$20A45C` per frame: `$208558` joystick, turned by the direction
`$203070`; without fire: speed `$20B372` grows by one every `$206B70`
frames of the same direction up to `$2083AE + $206B6C` (both form-dependent),
animation frame `$20B366` from `$20ABB6` / `$20AC24` / `$20AC7C` by form
`$206B64` (idle: $18-$1B fidgets from shift register h51 `$209??`), position
`$20B324/$20B328` (pixels across the 4 × 80 view) += speed × direction;
leaving 0-319 scrolls the view (`$20B18A`), `$20ACFA`/`$20AD78` wrap and
flag a new view (→ `$2086B0` + `$201492(1)`), cell under the walker =
position / 80 → its terrain, objects, mix (`$20B32C/50/54`), height
following `$20AB4E` / `$209D8A` (`$20B2F8` the walker's level). With fire: a
target cursor `$20B37A/$20B37E` moved by the stick, colours 19-31 flash,
the walker turns to face it (frames 0/4/8/C), `$2061BA`, `$206F40`
(zapping; to read). Sprites: `$209B54` places the walker (hardware sprite,
frames from S's sets by form), `$206DD4`/`$2084C6` write sprite control
words; the second `$201758` pass builds the mask that hides it.

### Tunnel (B, rebuilt 2026-10-27, `games/mindwalker_amiga/tunnel`)

Hand-over: A's way on (`$20B056`) saves the landscape to shared +$18..+$60
(home, marked, direction, cell, x, level, z, slow, finds, form, view,
damage, the ramp flag) and sets mode 9; the loader runs B. B coming from
mode 9 ends with mode 2 (caught: the arena, C) or mode 1 (ran out: A
again, which restores the walker from the block: `$2096B2`/h35/`$20AEE4`,
terrain $1BE, next terrain level | $100, the form without detection).

- Picture (h8 `$300EDE`): 17 octagon rings (radii h9 `$3012EC`) drawn once
  into a 480 x 300 two-plane picture, outermost first; each ring's 8
  triangles from the centre (240,150), pen (ring + segment + 2) & 3; the
  outermost ring's even segments take a corner from `$301330`/`$301340`.
  Filled by AreaEnd (see below).
- Window: screen (X, Y) shows tunnel (X + 159 - pan x, Y + 100 - pan y)
  (`$300D86`, descending blit); pan clamped to x 0..157, y 0..99, starting
  $50/$32. Screen rows 10..199 (dumps tunnel1-3); the rectangles clamp at
  row 11 (`$300B6C`).
- Display pens: tunnel 4-7 (plane 2 set wherever there is no hand); the
  hands (B images, at (64,125) and (180,115)) are pen 8 | p0 | p1 << 1 |
  minterm $AB of (p0, p1, p2) << 2; rectangles in plane 4 (pen | 16). The
  bar is drawn on pen 2 (dump: exact).
- Pace (`$301360`, from 7): ticks h10 (+1 per frame and per active
  rectangle); when pace < ticks the colours 4-7 rotate (and 0-7 load), the
  pan wanders by the wander mixer (`$301370`: wobble(3) / wobble(0)), and
  the stick steers. Laps (`$301364`) > (8 - pace) × 16 + 8 → pace - 1;
  pace 0 ends the tunnel (ran out).
- Steering (`$301186`): pan += stick; each rectangle moves (size / ($A0 -
  pot / $1D4) + 2) × direction (pot = stage × $618).
- Rectangles (`$302842`, four slots; one served per frame, ++count & 3):
  spawn when mixer[k] & $5D == 9 at x = mixer[4+k] & 63 + pan x + $30,
  y = mixer[8+k] & 31 + pan y + $22; grow by (size / (22 - stage / 3) +
  1)²; move outward by max(grow / 5, 1); off screen frees the slot; box
  w = size / 3, h = w × 3 / 2 (`$302C84`). Caught when the old box spans
  x0 < 81 and x1 > 239: colours 4-7 << 4, score += `$30326C`[pace] (0, 5,
  10, 25, 50, 75, 100, 150).
- Seeds: the shift register (`$301350`) from shared +$4C (the way on's
  seed); the wander mixer from two draws; rectangle mixer i from (draw,
  draw × i).
- End (`$300CFE`): the rings collapse, 17 × 4 frames.
- Sound (`$303380`): the h33 sample on four channels at periods h30 (1500,
  1400, 1500, 1400), each as loud as its rectangle, (min(size + 8, 63) +
  previous) / 2.
- Inputs: the left button each frame (`$30040C`; held: 300 frames still),
  the stick and fire on moving frames (`$3026C4`). Gadget $10 pauses; the
  close gadget quits.

AreaEnd (`screen/area_fill`, landscape and tunnel): each edge into a
scratch plane, one dot per row (the first as Bresenham steps from start to
end), XOR; flat edges none; an edge whose next non-flat edge carries on
its direction leaves out its last row (so a vertex passed straight
through keeps the next edge's first dot); fill right to left
inclusively. The tunnel's centre row needed a vertex rule (it also
brought the landscape's opening view from 12 to 2 pens off); first as
"the outgoing edge leaves out its first row", corrected to this form by
an arena kill at a shallow vertex (tunnel dumps 384/394 pixels, from
411/422). Rejected: edges normalised downwards, skipping first/last rows
or first/last pixels globally.

Verification: `runs/b_tunnel.txt` (Preset 1, a_wayon's route, joystick in
the tunnel, the resumed landscape): A1 equal; B 1,604 frames, 4,812 reads,
0 differences, outcome ran out (score 2001); A2 (MW_SESSION=2, resumed)
610 reads equal. Dumps at B reads 40/400/700: bar exact; view 411/422
pixels differ at 400/700 (one-pixel shallow edges); 1,146 at 40 (the same
picture a pixel apart: the shot falls a frame from the dump while the pan
moves). Harness: mwrec PEEKS for B; read counting across a module's runs;
`mindwalker_replay tunnel` (MW_SESSION, MW_SHOTS; T and A inputs carried;
one duplicated button read skipped, a stub artefact).

### Arena (C, rebuilt 2026-10-27, `games/mindwalker_amiga/arena`)

Entry: B's catch sets mode 2. C copies the restore point (+$2BC..), takes
the seed +$4C (h9), the form +$48 and the stage +$5C (pot = stage × $618).
Exits: the exit with the target: +$5C += 7 (at most 42), +$60 = 1 (A's
ramp flag: the way on closes), mode 3 (P); the exit without it, or a death
with lives left: mode 1 (A resumed); the last life or the close gadget:
status 1 (T).

- Screen: 320 × 200, 5 planes; the window is the gadget bar (BlockPen 18;
  S's icons as in A and B; lives figure i while i + 1 < lives). Room and
  panel from row 10 (dumps; `$401086` read 0 in the log, unexplained).
- Palette (`$4003AA`): colours 1-13 = i × step[seed & 7] (`$400494`),
  colour 0 / 15 from `$4004A4` / `$4004B4`; 29/30 the form's (`$4025AC`,
  `$4025B4`); the rest h3 `$40042C`.
- Maze (h8 `$401108`): a 64 × 48 two-plane map, 8 × 8 rooms of 8 × 6
  cells (pens: 0 open, 1 wall, 2 exit, 3 target). All wall; then 4 -
  (difficulty & 3) passes, each room: a run from a random cell (x + 2..5,
  y + 1..4) along its map row or down its map column (a draw & $40) to a
  random cell of another room; one run through the whole map at row $1B or
  column $24; cells $23-$25 × $1A-$1C cleared, the exit at ($24, $1B).
  Target (`$401480`): a random open cell of a room off row 4 / column 4
  (4 → 3 / 5). Creatures per room (`$4015A0`): `$401C56`[draw & 7]
  (1, 2, 3, 4, 5, 2, 3, 4), the target's room 6, home 0.
- Random numbers (owner's decision 2026-10-27: Mind Walker keeps the
  game's own generators, the arena too, since they make the levels): all
  from shared +$4C. h8: `$401CBC` = the seed; three shift registers
  `$401CC4`/`$401CC8`/`$401CCC` = ((key ^ draw) sign-extended ^ $59DD) << 16
  | (key ^ draw) with keys `$401C50`; the lightning's mixer `$4046A0` from
  draws of the first and third. Each room's look `$401CC0` = 6·ry ^ ry<<10
  ^ rx<<6 ^ ry<<2 ^ rx ^ seed. Play: `$4025FC` (fidgets), `$402EBC`/`$402EC0`
  (the wind's eight mixers), `$402ED4` (pulse skips), h21 `$4045EC`,
  `$4045F0`, `$4045F4` (creatures), each C's data XOR the seed (a zero
  replaced by a constant). Our maze equals the original's cell for cell.
- Room drawing (`$40168C`): each cell a 32 × 32 tile into a back bitmap:
  open: image 10 (clears); wall: image (draw & 15), above 7 the room's own
  (draw & 3), images 4-7 = 0-3 on three planes (C h48 data, `$406F34`..);
  exit / target: images 8 (32 × 10 at +18) / 9 (32 × 15 at +8) over a
  blank. The picks come from the room's generator, so a room looks the
  same each visit. Then the
  scroll-in (`$4018D2`): 7 (across) or 5 (up/down) strips, a frame each,
  the screen scrolled 32 pixels per strip (plus one before), then the
  whole room.
- Player (`$401D24`): 4 px across, 3 up/down a frame; off an edge: the
  next room (map wraps), the room's live creatures kept (`$403C0C`).
  Walls do not stop the player. Fire held: the stick turns the aim every
  other frame (left +1, right -1; 16 directions) instead. Map cell under
  the player: target: taken (tile cleared on screen, +500, a life up to
  6, the home block walled in around the exit: `$401D24`); exit within
  x $8B-$95, y $6B-$72: win (with the target) or leave (without);
  elsewhere on the exit cell with the target: creatures stop coming.
- Frame (`$402058`): aiming: `$4025BC`[aim] (form 2: 5); walking: base by
  the stick (down 8, up $C, right 0, left 4, up+across $10/$14) + counter
  & 3 (form 2: 0/8/$10 + counter & 7); standing: $18, now and then
  $18-$1B. Sprite 7 (colours 29-31), rows `$40259C`[form], at (x - 8,
  y - rows / 2).
- Beam (`$403130`/`$403382`): d = aim + 8 - wind; in line (|d| < 4 or
  12 < |d| < 20): root 15 px out along the aim, else 13; a fan of three
  points in direction (d + aim) - 1..+1 at wind strength × 5 (÷ 7 unless
  fire), filled (AreaEnd) in a one-plane overlay that becomes display
  plane 4 (`$400338`, rows 12-199). A creature with the overlay set at
  (±6, ±6) dies (`$403C72`; +stage / 2 + 1). The wind line: from the root
  against the wind, dotted (pattern rotating a bit a frame), to the
  screen's edge; the aim marker: 4 points at radius 15 round the player.
- Wind (`$402C34`): direction turns ±1 when a draw & $6E is 0; strength
  steps towards a random 0-15, wrapping below 0 to 15 (only slot 0 of
  four is used).
- Creatures (`$4036CE`, sprites 1-6, h27 frames): a free sprite comes in
  at a random edge point if its cell is open and (draw & `$404630`
  [difficulty >> 2 & 3]) == 0, not after the player stood on the exit
  cell with the target. Each frame: new behaviour now and then (draw &
  $250 == 0); touching (|dx| < 8, |dy| < 10): strength -= pot / $666 + 1,
  lightning, noise; bit 0/1: velocity ±2 towards the player (±20), else
  decaying; moves v / 4; a non-open cell bounces it. Dying: 15 frames of
  random erosion.
- Pulse (`$4028C0`/`$402930`): a counter of 32 steps (a step every 1 or 3
  frames by the round count & 2); steps 1-13 light colour k yellow (the
  walls and the label backgrounds); 3 rounds in 4 skipped at step 0. On a
  wall while lit: the wall death (`$402A2C`: 44 frames, the pulse racing),
  then the death (`$401A90`: colours 14/15 dimmed, a life less, the death
  picture on every cell, 44 frames of the four quarters scrolling in).
- Temperature (`$404E46`): every 21 frames, period = min(distance × 2 +
  250, 900) to the target (afterwards distance / 4 + 150 to the exit),
  distance = (|dx| × 3 + |dy| × 4) × 2 in cells, averaged with the last
  three; drone (channels 0/1, C h33) at that period; the gauge from it.
  Channel 1's volume follows the pulse; 2: the kill's squeal (h36);
  3: the hit's noise (S image memory).
- Win (`$40653A`): +7000, 158 frames: colours 1-15 rotating, 16-31
  random, colour 0 throbbing; a chord from a 64-byte wave rebuilt each
  frame (`$406642`).
- Inputs: the button each pass (`$4005A8`, a press freezes 300 frames),
  stick and fire (`$40251E`). P box ($10) pauses: window down 10 under
  the screen title "Click on Pause Box to Resume Game."; resuming: 30
  frames, the room scrolled in again, the panel.

Verification: three recordings after a forced catch in B. `c_leave` (one
cell short of the target, firing, back to the exit without it): 1,389
reads; `c_win` (the target, the walled-in exit crossed with the pulse's
counter poked off, the exit): 1,401 reads, won, score 9,662 as the
original's shared block at P's start (mode 3, +$5C 7, +$60 1, lives 4);
`c_die` (onto a wall in the first pulse): 27 reads, the wall death, mode
1 with lives 2. In all three, at every read, everything logged equals:
the maze, every generator's state, position, room, aim, frames, strength,
score, the creatures (place, speed, behaviour, dying, phase), the rooms'
counts, the wind's four slots and mixers, the pulse, the lightning, the
sound's counters and the drone's history. Dumps arena0-4 of both runs
match our picture to within 8 pixels (most 0-1), at their own frame or
the one before: C starts the overlay's blit and goes on, so a dump may
catch display plane 4 before the blit lands. The beam's kills fixed
AreaEnd's vertex rule (see the tunnel: the incoming edge leaves out its
last row). Harness: C peeks split per hunk (a peek across a hunk
boundary reads past it: the hunks lie apart at run time); pointer peeks
logged as `gone` once the module has freed the memory; pokes are applied
before the read is logged (the replay does the same).

### Psyche screen (P, rebuilt 2026-10-27, `games/mindwalker_amiga/psyche`)

Entry: C's win (mode 3). P copies the restore point, never changes +$5C,
and ends with mode 1 (the landscape, its way on closed by C's +$60) or,
closed, status 1. P reads the world's seed +$10 (not +$4C).

- Screen: the gadget bar (S's icons; the slider's PropInfo has no
  FREEHORIZ, so its knob stays left whatever the pot: dump psy0 — fixed
  in `screen/gadget_bar` for all stages); panel from row 10 (`$5010AC`):
  frame pen 20/16, the picture's grid (`$AC`,`$48`, 7 × 6 cells of 20),
  the left panel: seven slots (x 9 + 22i, y `$14`-`$27`), the colour bar
  (pens 25-31 under the slots, y `$33`-`$3B`), the hint buttons in a V
  (Image `$50213C` at `$501D52`/`$501D6E`), the big creature (Image
  `$502128` at (`$45`,`$9C`)); "Your Score:" and "Self-Development Bonus:"
  (IntuiTexts `$5068C6`, `$506910`, numbers `$5068A6`).
- Tiles (h11 `$5021B8`, `$5022EA`): a shuffle of the 42 tiles by a shift
  register seeded with +$10 (after 50 tries per place mixed with a second
  one, `$502400`; a draw & 63, above 41 less 30); the first (progress - 7)
  go home, the next seven into the slots. Tile t's home: column t / 6,
  row t % 6 (`$502404`/`$50242E`). Progress below 7 counts as 7.
- Kaleidoscope (`$501424`, `$5016A6`): four points in 140 × 120, all moving
  with point 0's speed (each its own every 8th pass), bouncing (a new
  speed 1-8 off an edge); a draw & 15 == 0 gives one a new speed (point 0
  from `$502108`) across or down; their quad filled (AreaEnd) four ways
  mirrored into a hidden 4-plane picture in a pen counting 1-15 (0 for 16
  passes in 64); every 64 passes a word of P's 32 (`$501CD2`) in pen 7 or
  15 (`$500E3C`). Generators h9/`$501BEC`/`$501BF0` = data ^ seed>>4 /
  seed>>6 / seed<<5.
- Pass (`$50198E`): before tiles 0, 4, 8, ... a vertical blank; each
  present tile's piece copied from its home in the picture to where it
  lies, the button read (`$500418`) before each; colours 1-15 turn at the
  end of the pass; tiles home ≥ progress: done (or at 42 the last
  picture). Every 6 tiles (`$505970`) the hint lines climb a step; every
  (pace + 1) steps (pace = 7 - progress / 10) colours 25-31 turn.
- Mouse (`$505852`): slot (x from 9, 22 apart, y `$14`-`$27`) or cell:
  pick up a tile or put the held one into an empty place; elsewhere with
  nothing held the hint buttons (x+4..x+18, y-3..y+8). The hand (sprites
  2/3, `$506514`/`$506574`) at (pointer - 10, pointer - 20) while holding.
- Hints (`$5059B2`): the line climbs 10 px a step from its button; at
  the bar (y `$32`-`$3C`) the slot's colour must be `$F00` (red), else the
  line is wiped; above `$28` the slot's tile flies home (`$505E98`/
  `$506332`: the hand a pixel a step on each axis, a frame every few
  steps, faster; music fast), a tile already there goes to the slot, the
  bonus loses 1,000 (`$506796`).
- Music (h14, `$5026BC`-`$502B8C`, timer.device 60 ms): four voices on
  P h24, patterns `$502E14`, periods h18 `$503490`, envelope `$502E2C`;
  hint hum (`$502A3E`); fast figure `$502E04` on wave `$502E24`.
- Last picture (progress 42, h41): 20,000 per life, 24 per second under
  5,400 of +$62, + the self-development bonus; "WELCOME BACK TO THE REAL
  WORLD!" (`$506A74` chain); the finale (h17, `$503178`); closing the
  window shows MindF6 and returns to T.
- End (done): sound off, screen cleared, colour 0 black; h32: the loading
  picture on S's screen and MindF(progress / 7) over rows 12-199 in a
  colour of its own (`$5057D0`), 50 frames.

Verification: `runs/p_idle.txt` (c_win's route, then P untouched): 1,501
reads, every logged state equal (tiles, slots, grid, kaleidoscope points
and speeds, pen, generators, colours, hand, score); dumps psy0 exact,
psy1-3 differ by 58-335 pixels inside the kaleidoscope's tiles (the
original shows dots and dashes along thin parts of its shapes that our
AreaEnd cancels; drawing edges downwards or rightwards made it no better).
`runs/p_play.txt` (a tile put into a wrong cell, picked up again and
carried home; five more carried home; slot 6's hint pressed: its line
passed the bar on red, the tile flew home, the puzzle done, A next):
364 reads, every logged state equal, outcome done in both; dumps differ
only inside the kaleidoscope's tiles (58-227 pixels).

### T after a game: Hall of Fame, command screen, saves (rebuilt 2026-10-27)

T runs again whenever a stage ends with status 1 (the last life lost, or a
window closed), the game still in the shared block (+$2BA: where it was).
`title/title_stage`, the module's `to_title`, `session`, `save_world`,
`restart`.

- h17: a score (+$66 ≠ 0, +$2BA ≠ 0) at least the lowest of the 32
  MindHighs entries (the entry ranked 32nd, `$6035D4`'s ranking: highest
  first, ties to the later entry) takes its place; the initials board
  (`$6043D2`: frame `$604526`; 34 texts `$604652`.. with x/y/pens; the
  score and its rank with commas `$603AFC`; letters A-Z, (Space), Forget
  It, Start Over; each letter then 10 frames' wait); three letters write
  MindHighs (h11) and show the Hall of Fame with the disk gadget off; Forget
  It returns to the menu (the table not written).
- Hall of Fame (Fame toggle, `$603714`): base colours down column 0 (rows
  `$603E72`, pens `$603EB6`, reaching into the gadget bar), pen $20 across
  (hold-and-modify), "Hall of Fame" at ($70, $F) its colours cycling every
  2 frames (`$603814`, `$603C18`), 32 places in two columns ("n)", score
  padded to 7 with commas, initials), three creatures (S's +$2D0 frames as
  4-plane Images with PlaneOnOff $10/$20/$30, `$603E36`).
- Command screen (disk toggle, `$601AB6`): colour bands (`$601AE2`: h9
  steps, $20/$30 + step across), the message area pen 7, items drawn one a
  frame (`$6019A2`: pen $3F on its back pen, the column after it restored
  to the band's base colour); Command: Continue Game / New Game, Same World
  / Save Game ('>' and back pen 0 on the chosen one), Saved Worlds A-H;
  Save Game asks ("Saving to World A: Are you sure?", NO, BELAY THAT! /
  Yes, do it.) and writes the shared block (0x310 bytes, MindWorldA-H);
  Continue (`$602248`) restores it and restarts where it was (+$2BA: 2 the
  landscape resumed, 3/4/5 the tunnel/arena/psyche from the restore point
  +$2BC.., below 2 the landscape from its start; no lives: a new game in
  the world); New Game, Same World: the world from progress 0.
- Gadgets: Fame and the disk are toggles (GADGHCOMP: complemented when
  selected); each disables the other; leaving redraws the menu (`$6004F0`).
  Only the toggled gadgets' boxes are redrawn (so the Fame bands' column 0
  stays in the bar, as dumped).
- T's playfield top is row 10 (the command screen's dump; the earlier
  title dumps unchanged).
- The port keeps its own files beside the imported data: "Hall of Fame"
  (MindHighs' layout; read instead of MindHighs once written) and
  "World A.save".. (our game record as text: the record, where, the
  stage's restore point, the landscape's resume fields).

Verification: `runs/t_end.txt` (a_lives' route to the game's end, then
the initials "JOY", the Hall of Fame (hooked dump and click), the command
screen: Save Game to A, confirmed, Continue from A): `mindwalker_replay
title-again` drives our T read by read; dumps entry, initials, fame,
command, confirm and saved are pixel-exact except the disk gadget's
highlight (the recorder's posted click toggles SELECTED without Intuition
drawing it; the Fame gadget, refreshed by T, matches). The menu's pillars
follow the clock-mixed seed (not compared). `mindwalker_replay flow` runs
the module through the landscape's close gadget, T, Save and Continue.

Enhancements (2026-10-27): quick save/load and the shared VHS look. Quick
save copies the running stage and the game's record (the stages keep no
pointers into themselves: the arena's and psyche screen's multi-frame
sequences are plain action records, the landscape's view builder, map
builder and repaint grid are passed per call). Checked: `MW_COPY_AT=n`
replaces the stage by a copy at frame n in the walk, tunnel, arena and
psyche replays (0 differences at every point tried, mid-scroll, mid-death,
mid-flight); `mindwalker_replay quicksave` (identical pictures after a
load; save and load under 0.1 ms).

### The finished game, T's pause box, a natural tunnel catch (2026-10-28)

- The ending (P at progress 42, `runs/p_end.txt`): c_win's route with
  shared +$5C poked to 35 late in the arena (C's exit adds 7), the seven
  slotted tiles carried home (layout from `mindwalker_replay psyche-layout`,
  which matched the original's slots), the whole picture with "WELCOME BACK
  TO THE REAL WORLD!", the bonuses (lives 4 × 20,000 = 80,000; time
  128,400; self-development 42,000) and the finale, then the close gadget
  (recorder event `close`: a CLOSEWINDOW IntuiMessage, `mw.post_close`).
  P's end (`$500040` after its loop): no portrait unless the picture was
  finished (`$500CEA`) or it is whole and the window closed; h32
  (`$50567C`) shows MindF(progress / 7, 1-6) with colour 1 from
  `$5057D0`.., 50 frames; closed → status 1 (T), else mode 1 (A). The
  original then shows T's initials board. Compared: 761 of 761 reads
  equal (tiles, slots, grid, kaleidoscope, generators, colours, hand,
  score 260,062 after the bonuses); the whole-picture dumps equal outside
  the kaleidoscope; the MindF6 portrait on S's screen equal pixel for pixel
  (`mindwalker_replay portrait`).
- T's pause box (`$600DF4`, toggle id $10 at `$6014CE`; h5 hands it the
  SELECTED flag): `$600E20` turns the disk and Fame off (ghosted), shrinks
  the window to its bar (the playfield shows pen 0), waits 15 frames,
  moves the window down 10 (Intuition does it a moment later: the screen's
  title bar appears above it, BlockPen 15, "Click on Pause Box to Resume
  Game." at `$601526` in pen 0, the depth gadgets), then Waits on the
  window's port. `$600EE6`: back up and to size, 30 frames, then the
  screen drawn again by what was showing: the command screen (`$601ADA`,
  its choices kept), the Hall of Fame (`$603728`), the menu (`$6004F0`,
  both gadgets on) or the initials board (`$6043B6`). Read by the menu,
  Hall of Fame, command screen and initials loops. `runs/t_pause.txt`
  (over the menu): shrunk, paused, resuming and resumed dumps equal pen for
  pen; the menu's state per read equal after the resume.
- A natural catch in the tunnel (`runs/b_catch.txt`): random joystick
  holds searched in our rebuild (`mindwalker_replay tunnel-search
  40d8b950 0`) until one catches; the original catches at the same read
  (1,103 reads equal), scores 100 for it (2,101) and starts the arena.
- The pauses of B, C and P (`runs/pauses.txt`, p_idle's route with the
  P box clicked in each): every stage takes a message right after a button
  read (B `$30044C`, C `$400618` at each pass's start; P `$500488` at the
  pass's start and after every tile), pauses (sound off, sprites away,
  B's rectangles cleared), MoveWindow down 10, and Waits; woken, the same
  routine reads the button again, then resumes: back up, 30 frames, the
  screen cleared and drawn again, and the interrupted pass goes on (B: the
  stick and a rectangle; C: the stick and the rest; P: the kaleidoscope
  and tiles, or that tile's follow-up). B's resume runs `$300272`: h23
  shows h3's colours again (B turns h3 in place: the current ones) and h8
  sets the tunnel up again: the wander mixer from two more draws, pan
  ($50, $32), laps, ticks, pace 7: a pause gives the tunnel its full time
  back. Paused pictures (dumps 50 vertical blanks into the pause): the
  screen's title bar (B: BlockPen 2; C, P: 18; DetailPen 0) above the
  picture's rows 0-11 moved down 10 (B: its bar's 10 rows), the rest left
  as it was. All three replays equal read for read through the pause;
  the paused pictures equal but for B's ring edges, two aim dots the
  original leaves in C, and P's kaleidoscope texture in the slot tiles.
- Recorder additions: `poke` into the shared block (addresses from
  `$7C04F4`), `close`, `gadget` for every module (`GADGETS`: windows and
  P boxes of A, B, C, P, T), hook `dumplater` (50 vertical blanks run on,
  stopping in the counter's interrupt server, so Intuition's deferred
  window moves show; `mw.wake` puts a task asleep in Wait back on exec's
  ready list when a message is posted after it slept).

### Cheats (2026-10-28; not the original; pause menu, "Cheats" page)

- God mode: landscape: no creature attaches a tether (`Creatures`, the
  attach draw still taken, so their flight is unchanged); arena: a touch
  costs no strength (the bolt and noise stay), the walls' pulse kills no
  one.
- Auto transform: on entering a cell whose surface kind (terrain & $C000)
  differs from the form's (`$206B50`), the walker takes the matching form
  at once (as the pyramids do: height, colours 17/18, the form's sound),
  before the cell's events, so the cell joins the trail.
- Minimap: landscape: 64 x 64 pixels bottom right, north up, the map's
  four surface colours and its path shade, the walker red; zoomed (1-4
  pixels a cell, or several cells a pixel) to the nearest target within 48
  cells: an unlit beacon, or with the way open the marked block's ramps,
  flashing (other beacons dim yellow); cells come from the view builder,
  cached by block. Arena: the 64 x 48 maze a pixel a cell bottom right of
  the room, the current room outlined, the exit flashing, the target cyan.
- Checked: `sim` with `MW_CHEATS` (a_catch's route: no tethers, damage 0;
  a straight route from the start to the beacon and its ramp: every cell
  on the trail, the way on taken, the next seed as the original's); the
  arena replay with god mode (no strength lost; c_die survives the pulse).
  All off: every recording unchanged.

### Verification (2026-10-27)

Harness additions (`harness/`): `mwrec.py` per-frame recorder (stops at
every input read, logs module state and shared +$10..+$7F, scripted input,
screen dumps of the stage's screen or of S's, hooks; follows module
switches through the loader's PutMsg `$7C046A`); `rawpng.py` (dump → PNG,
pens, palette); `worldref.py` oracle (runs A's own routines in batches from
a small 68k loop in the raster's spare 8 KB and records every chain value
and sampled cells). Stub quirks: continuing after a breakpoint stop with a
changed PC does not run; long `M` packets are refused (pokes are chunked);
SR writes are ignored.

Runs (`harness/runs/*.txt`, logs in `work/ref/`):
- `t_preset1` (seed $12345678, Preset 1): 182 frames, every state field
  equal; title and menu screens equal pen for pen and colour for colour;
  new game equal (seed $456D1A9B).
- `t_newgame` (seed $9ABCDEF0, New Game): 135 frames equal; menu screen
  equal; New Game seed $729ED505 equal; loading picture equal (after the
  depth gadgets were embedded).
- `t_twinkle` (seed $0BADF00D, 700 title frames, slider pot poked to
  $3000, Preset 2): 772 frames equal including 12 twinkles; title screen
  equal; stage 7 and +$62 = 960 equal to the original's shared block; the
  stage-7 loading picture equal.
- World oracle, Preset 1 and the $729ED505 world: keys, levels, start, all
  4,096 × 2 chain coordinates × 3 levels (both worlds), 2,460 cells (the
  $729ED505 world): no difference
  (`build/mindwalker_world check`).

- View builder oracle (`harness/viewref.py`, calls `$2086B0` in unrolled
  batches): 4 × 349 views (49 around the start, 300 random, random previous
  cells; stages 0, 7, 21, 35; empty and random visited maps): terrain,
  objects, 32 colours and flags equal (`mindwalker_world views`). Only the
  marked-block bit ($100) has not occurred.
- The screen-depth gadgets (Intuition ROM imagery, embedded on the owner's
  decision in `amiga/intuition_imagery.cpp`): the loading pictures are now
  equal pen for pen.
