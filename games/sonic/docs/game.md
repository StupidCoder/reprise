# BlueStreak: what the games are and how they work

Everything learned while reconstructing the Sonic games in BlueStreak:
how each cartridge's program is organised, how its data is stored and
encoded, its rules, and the bugs and oddities of the original code.

Status, open work and known bugs of the reconstruction are in
[plan.md](plan.md). Where to find each topic in the source is in the
README's "Where to find the game mechanics".

## Conventions and sources

- **Source of every finding.** Each finding comes from the supplied ROM:
  static reading of its code (our own coverage-driven Z80 disassembler,
  LLVM and Ghidra for ARM/Thumb) and runs of it in our own research tracers
  (`ggprobe` for the Game Gear, `gbaprobe` for the GBA).
  - General hardware documentation (SMS Power for the Game Gear, GBATEK for
    the GBA) and general-purpose emulators were used only for hardware facts.
  - No disassembly, decompilation, port, symbol map or guide for these games
    was consulted. Before that rule was set there was brief exposure to a
    Sonic 1 disassembly; those files were removed and every claim here was
    re-checked against the ROM. The model doing the work may have seen
    public material about these games in training. So this is not a
    clean-room claim.
- **Names.** Names are descriptive and ours, not the developers' symbols.
- **Address notation.**
  - Game Gear: an address is a Z80 CPU address with the bank that the code
    maps. Bank 0 is at 0000-3FFF, slot 1 at 4000-7FFF and slot 2 at
    8000-BFFF; "bank 1E:9BA1" means bank 1E mapped at 8000. `D415`-style
    numbers are work RAM (C000-DFFF).
  - GBA: `08xxxxxx` is cartridge code or data (Thumb unless noted),
    `03xxxxxx` is IWRAM and `02xxxxxx` is EWRAM.
  - Numbers are hexadecimal unless the text says otherwise.
- **Hypotheses.** A statement marked *hypothesis* has not been confirmed by
  the code or by a run.
- **Detail notes.** The detailed per-game research notes are in
  `.analysis/` (not in git):
  - `sonic2_gg/NOTES.md`
  - `triple_trouble_gg/PLAN.md`
  - `sonic_advance_gba/PLAN.md`
  - `gg_engine/PLAN.md`
  - Sonic 1's milestone reports, archived in
    `archive/pre-cleanup-2026-10-10.tar.gz` (`docs/*.md`)

## The four cartridges

| Game | Dump | Size | CPU | Engine family |
| --- | --- | --- | --- | --- |
| Sonic the Hedgehog (GG) | Japan/USA, CRC32 3E31CB8C, SHA-256 dd605c17... | 256 KiB | Z80 | Sonic 1 GG engine |
| Sonic the Hedgehog 2 (GG) | World, CRC32 95A18EC7, SHA-256 2826d679... | 512 KiB | Z80 | Sonic 2 GG / Triple Trouble lineage |
| Sonic Triple Trouble (GG) | USA/Europe/Brazil, CRC32 D23A2A93, header "GG SONIC THE HEDGEHOG Ver 1.00 1994/08/02" | 512 KiB | Z80 | Same lineage as Sonic 2, later revision |
| Sonic Advance (GBA) | USA, CRC32 63F70FD8, code ASOE, maker 78, 64 KiB Flash ("FLASH_V126") | 8 MiB | ARM7TDMI | Unrelated engine (task scheduler) |

- **Shared code.** Sonic 2 and Triple Trouble share their asset formats, the
  tile compression, the sound driver design, the object-script bytecode
  family and the display handling, as revisions of the same code. That is
  why they share `src/gg_engine`.
- **What differs between them.** Their object run loops, palettes, frame
  interrupts, load timing, map streaming, camera and player physics are
  different code revisions, not parameter variants.
- **Sonic 1 and Sonic Advance.** Sonic 1 GG and Sonic Advance share nothing
  with that lineage or with each other beyond hardware.

---

## Game Gear hardware notes (all three GG games)

- **Screen.** The LCD shows VDP pixels 48-207 and lines 24-167, a
  160x144 crop of the 256x192 picture.
- **Palette.** The palette is 32 colours of 12 bits; 4-bit channels expand
  as n x 17.
- **Sprite and display limits.** The VDP draws at most eight sprites per
  line. A sprite Y of D0 in the table ends the sprite list for that frame.
- **Line interrupt.** Water lines, horizon bands and train-ride scroll
  bands come from the line interrupt (VDP register 10). Its timing against
  the program's CPU time is not modelled (see the deviations in plan.md).
- **PSG.** Sound is the SN76489-style PSG with Game Gear stereo routing:
  three tone channels and a noise channel.
- **Refresh register.** `LD A,R` (the Z80 refresh register) is read as a
  random source by some routines. It depends on the exact instruction count,
  so the ports replace it with a small deterministic generator.
- **Banks.** The cartridge maps 16 KiB banks into slots 1 and 2. Code
  addresses always mean "with the bank that code maps".

---

## Sonic the Hedgehog (Game Gear)

### Program structure

- **Frame boundary.** The game's frame-wait return at `032D` (front end) and
  `14DD` (gameplay loop) marks a completed update.
  - A video frame is not always a completed update: the original misses
    frames under load, and the sampled voice runs with interrupts disabled.
  - `14DD` can be hit twice without an update when a pending interrupt
    returns there.
- **Per-frame order** (`281E-28D2`):
  1. HUD.
  2. Camera (`2953-2B0D`).
  3. Active objects (`2C8D`).
  - The player's update runs before integration. The camera sees the
    previous frame's state.
- **Activation.** Objects activate through a four-slot activation cache
  refreshed at `2BD8`, then update in slot order.
- **Act loader** (`1A08`, `175C`):
  - placed object records are reset (`1A80-1AB2`); the unused tail keeps its
    old fields with type FF;
  - the shield bit survives a level load (`1A76`);
  - the loader sets a zero-sized collision box (`185D`, `1AB3`);
  - sixteen neutral updates (`1472-14DA`) settle Sonic before play.
    The original's starting point, for example, is (96,384), settling to
    Y=368.
- **Random numbers.** `0688-06A0` is a 16-bit generator. The video interrupt
  also calls it (`00D8`), so the number of interrupts per update affects
  later random choices.

### Acts, rooms and progression

- **Header table.** Level headers come from a table at ROM `15600`.
  - Acts 0-17 are the six zones, three acts each: Green Hill, Bridge,
    Jungle, Labyrinth, Scrap Brain, Sky Base.
  - Act 18 is the automatic flyby.
  - Act 19 is a zero header: it calls the ending and credits controller
    instead of loading a map.
  - Headers 20-25 are Scrap Brain act 2's linked rooms, and 26 is the lower
    room from Sky Base act 2.
  - Header 27 is a duplicate that nothing leads to.
  - Acts 26 and 27 are scene 7. The bonus stages are acts 28-35, scene 6.
- **Linked rooms.**
  - A linked room selects another header (`D2D4`) while keeping the logical
    act (`D238`). Doors, switches, life monitors, score and results use the
    logical act.
  - A room change keeps rings and the clock.
  - A death in a room reloads its own start and starts the clock at 1:30
    (`18D8`, `1B4E`).
  - Transports (`58E0`, five cell-indexed records) lead to a place in this
    map, a linked room, or FF (the next logical act, without tally or map).
- **Header 21's partial map.** Its compressed map writes only 2048 of its
  declared 4096 bytes. The rest is left over from the previous room. A
  direct start into it is therefore refused.
- **Life monitors.** They are remembered in an act bitmap (`D306-D30B`), so
  re-entering rooms cannot farm them.
- **World map between zones.**
  - Route records are five bytes: mapping pointer, X, Y, duration. A zero
    duration jumps to another record.
  - The map runs a 300-update route loop (`0BDD-0E7A`), then fades out and
    waits 60 black frames.
  - The act 3 map entrance (`0D76-0DBE`) moves 256 updates left, then 114
    right.
- **Fades.** `0AB7-0B3D` waits four frames, then takes sixteen palette
  nibble steps of four waits each. Before fading, `0AB7`/`0B1E` reload the
  base background palette whatever the colour cycle's phase.
- **Results** (`449D-460E`):
  - hold 120 updates, then count down the ring and time bonuses together,
    then hold up to 180 more; Start skips only the final hold;
  - each update counts one ring (100 points) and one 100-point time unit,
    so the tally lasts as long as the larger of the two;
  - the tally sound plays every fourth update;
  - the time bonus thresholds are a table of fourteen BCD amounts
    (`4457-449C`); the reward is not computed by a formula.
- **Score.** Points are packed BCD. Extra lives are given at score
  thresholds kept in `D2F8` (units of 10,000; `33AA-33D6`).
- **Sign rewards** (`173D-17E9`). At finish count 136 the sign gives a
  sound, a life, rings or a bonus stage request. With at least 50 rings the
  sign leads to a bonus stage.
- **Final bonus.** The bonus needs six emeralds, eighteen life monitors,
  eight bonus attempts, eight successes and no life lost. Meeting all of
  them raises the combined bonus from 30 to 50 units of 10,000.
  - Emeralds and lives are tallied separately.
  - An ending music field of FF means "keep the current song".

### Death, game over, continue

- **Life loss.** A life is taken only when Sonic falls 192 pixels below the
  camera (`5701`). The game-over test (`1812`) reads the stored lives.
- **Death timer.** It counts on odd frames (`15A2`, `17EB`). A death caused
  by terrain still counts down its newly set timer on that odd frame
  (`1591-15A6`).
- **After the timer** (`17F2-1811`): the linked-area flag is copied, the
  water interrupt is switched off and the act's death count `D300` rises.
  The goal sign uses `D300`, and a map after a continue clears it.
- **Restart** (`13E4`): fade over 68 waits, 60 black waits, stop the sound,
  reload. The checkpoint camera starts three cells before the start cell,
  clamped at zero (`195C-1997`).
- **Game over** (`1819`, `11E1-121C`):
  1. the GAME OVER screen holds for 120 waits;
  2. without continues, it waits up to 180 more (buttons 1 or 2 end it,
     Start does not);
  3. with continues, it scrolls 16 lines, shows CONTINUE ? and a countdown
     from 9 with a tick sound every 60 waits. Only Start continues.
  - A continue clears the act's checkpoint bit and restores three lives.
  - The sequence requests no music.

### Player physics (bank 1)

- **Player record.** Object records are 26 bytes; the player's is at
  `D3FD`.
  - X, Y and the two speeds are 24-bit with 8 fractional bits.
  - Flags at `D415`:

    | Bit | Meaning |
    | --- | --- |
    | 0 | rolling |
    | 1 | facing left |
    | 2 | jumping |
    | 3 | jump armed |
    | 4 | water |
    | 5 | no terrain collision |
    | 6 | wall contact |
    | 7 | grounded |

- **Dry-land constants** (`4B63-4B86`):

  | Rule | Value |
  | --- | --- |
  | Acceleration | 16/256 px per frame² |
  | Braking when reversing | 48/256 |
  | Coasting friction (below running speed) | 8/256 |
  | Running limit | 3 px/frame |
  | Jump launch | -3 px/frame, held for up to 16 frames (`4D60-4E12`) |
  | Gravity outside the hold | 56/256 per frame |
  | Overall speed cap (`4EE0`) | 10 integer px/frame |

  Water constants (`4B37`) also pick the walking animation at the reduced
  top speed. Bonus stages have their own constants (`4B89`).
- **Rolling** (`55C7`) uses a shorter collision box. It handles the roll
  jump and crouching.
  - In the air, a curled Sonic gains +-16/256 per frame from Left or Right
    with no running limit. Only the 10 px cap applies.
  - Ramps (`576B`) keep the roll bit.
  - So a rolling launch with Right held reaches 10.98 px/frame and outruns
    the camera, which moves at most 8 px per frame (verified in the ROM).
- **Terrain** (`2CD4-2EDD`):
  - the world is 32x32-pixel blocks of 4x4 tiles; `30D5` looks a block up;
  - collision has four contour tables: left `03A0A`, right `03B0A`, ceiling
    `03BDA` and floor `03E7A`. Block attributes come per scene from
    `0343D`;
  - snapping keeps the position's fractional bytes;
  - `30D5` builds a flat layout address with an 8-bit X block index, so on
    narrow maps a right-edge sensor can read the next row's first cell
    (seen in bonus stage 4).
- **Terrain actions** (`5763-5AC5`): spikes, ramps (launch depending on
  speed), horizontal springs, and vertical springs gated by sensor
  alignment and the newly-grounded flag. Also water entry, the waterfall
  sound, currents, bonus bumpers and springs, and Bridge's collapsing
  planks (actions 24-26, `5AD8-5BA9`).
  - Spike damage cancels the controller input at once, midway through the
    player's update.
- **Rings in the map.** Ring cells (79-7B) hold two presence bits. Taking a
  ring changes the layout cell, and the background is redrawn from it.
- **Tile patches.** Collecting a ring and terrain changes share one pending
  2x2 name-table patch per frame (`3282`). Collecting the ring comes before
  the horizontal camera clamp (`4E3F` before `4F33`).

### Camera

- **Dead zones and limits** (`2953-2B0D`): dead zones and act limits. A
  subtraction that would go below zero is refused before the limits apply
  (`29DF`, `2A9E`).
- **Right boundary.** It snaps the position but leaves a positive
  sub-pixel speed below one pixel in place.
- **Bosses.** Boss arenas move both limits one pixel at a time
  (`2B16-2B51`, `7CC0-7CF4`). Bridge act 2's limits move one pixel on odd
  frames (`1672`), and its header sets horizontal auto-scrolling (cleared by
  the goal).
- **Death.** The camera freezes during death (`28B6`).

### Objects

- **Placement.** Each act has a placement stream: a count including the
  player, then three-byte records (type, block X, block Y). Example: Green
  Hill 1 is at `15B34`, with 25 records.
- **Per-object state.** Each object has seven bytes of per-type state. The
  update table maps types to handlers (`2C8D`).
- **Shared routines.** Monitors (`5DE1-6010`, `6089`, `60CC`), enemy contact,
  damage and death (`2FC1`, `2FD9`, `2FF4`), the goal sign (`61F8`),
  explosions (`693E`, `7A76`) and lost rings (`7BD1`).
- **Shared boss code** (`77FD-795D`): eight hits (twelve for the final boss,
  `B60E-BA2C`), ten explosions, sinking, escape, then the capsule
  (`736B-754C`: twelve animal spawn attempts). Each zone's boss uses its own
  mapping offset.
- **Zone handlers.** Bridge `82FB-8846` and `B4E8`; Jungle `806B-82E1` and
  `6A55`; Labyrinth `884C-9867`, `A3C1`, `AE4E-B131`; Scrap Brain and Sky
  Base `9B4A-B4E7`; final boss `B60E-BE02`; ending `BE03-BFFE`.
- **The bonus stages** (bank 2 `9868-9B49`, bank 1 `5973-5AC5`):
  - pieces: flippers, three-contour launch, a moving bumper that reverses at
    96 frames (192-frame cycle) and a continue monitor;
  - clocks: acts 28-31 start at 2:00 and 32-35 at 1:00, from table
    `1B51-1B68`;
  - time running out loses the rings and any unbanked continue;
  - each success raises the per-ring bonus by 100.

### Water (Labyrinth)

- **Two water heights.** Movement and air use the water's world Y. The
  visible colour change starts lower, at water Y + 13 on screen.
- **Colour band.** The line interrupt (`01D7-0203`) streams 64 colour bytes
  over nine scanlines, so the boundary briefly mixes dry and underwater
  colours. The completed colours per row are 2, 6, 11, 14, 18, 22, 26, 30
  and 32.
- **Underwater palettes.** They are at ROM `0216` (`0256` with `D207`
  bit 4).
- **Air.** The air clock, warning digits and drowning are at `526C-52F9`.
  Act 11 is exempt.

### Data formats

- **Tiles** (`0406-04F9`):
  - tiles are decoded row by row; a bitset picks literal or dictionary rows;
  - dictionary tokens below F0 are one byte, and F0-FF start a 12-bit row
    index; literal rows advance the dictionary;
  - a row is four plane bytes;
  - pointers such as A8B7 or DAD0 look too large, but the decoder adds the
    full offset to the bank base on purpose. Masking them to 14 bits reads
    the wrong data.
- **Layouts** (`0A73-0AA2`). A literal byte is emitted; a repeated byte
  marks a count of extra copies (0 means 256, from DJNZ).
  - Example: Green Hill 1 is 256x16 blocks, decompressed from `17430`
    (0786 bytes into 4096).
- **Player art.** Frames are three-plane, 192 bytes each, at `20000`
  (facing right) and `21800` (facing left). They are expanded to four
  planes on upload (`31BC-320B`).
- **Palettes.** The table is at `23400`. `0586` keeps 16 background
  colours, then background colour 0 again, then sprite colours 1-15.
  - Background colours cycle per act; the header gives the phase lengths
    (`1B1E`).
  - Ring and grass animations have their own counters, reset per act
    (`161E-1667`, `1AF1-1B1D`).
- **SEGA logo voice.** Its packed samples are at file `23D24` (length 175E;
  two 4-bit volumes per byte). They are played by bank 8 `7C00-7D1F`, which
  maps banks 8 and 9.
  - The schedule lasts 5,402,583 master clocks (about 1.51 s).
  - The logo uses the export-region trademark patch. The region bit was
    read from the running cartridge.
- **Demos.** Demo input bytes advance after every 32nd update (`12E9-1355`).
  The two attract demos alternate Green Hill and Jungle (`135B-13E1`).
  The Jungle demo ends by falling into a pit.

### Sound

- **Driver.** The driver is bank 3, mapped at 4000 (file offset = address +
  8000).
  - The tick is `423A`, called from the interrupt at `00CB`.
  - Music selection is `46EB`, reached through RST 18, and effects are
    `4171`, through RST 28.
  - It keeps four 45-byte music records and one effect record
    (`DC26-DD06`), with loop counters interleaved after them.
- **Song data.** 21 music pointers are at `4716` and 36 effect records
  (pointer, priority, unused byte) at `4740`. Musical commands 80-8D
  dispatch through `4529`.
- **Effects over music.**
  - An effect takes over one music voice; that voice's notes go on, but its
    envelope and vibrato pause until it is heard again.
  - A lower nonzero priority wins, and an equal priority replaces the
    current effect.
  - Output is mono on both sides.
- **Effect details.** The skid sound has a 60-frame cooldown (`D413`).
  Speed shoes change the tempo.

### Original bugs and oddities (kept)

- **Activation margins** lose six pixels: six `LDI`s count down the `BC`
  register that is then used as the left margin.
- **Crab** (`65F9`): its firing frame overwrites an animation pointer, so
  that frame shows animation entry 0.
- **Rolling reversal**: both directions reach the same left-braking code.
- **Projectiles** keep a byte of the slot's previous state. A new shot from
  the Scrap Brain gun keeps the slot's unused bytes.
- **The goal sign**'s last speed subtraction wraps below zero before its
  spin flag clears.
- **Act 3 map entrance** (`0EA1`): the code increments the byte *at* the
  address stored in `D20F`. With `D20F` zero this writes to ROM (no effect),
  so the entrance's animation never advances past its first pose.
- **Explosions near a capsule.** `696F` cancels the explosion rebound while
  terrain streaming is off, so capsule explosions do not bounce Sonic.
- **Partial map.** Header 21's map is only half written (above).
- **Input sampled mid-update.** An interrupt in the middle of the player's
  update can sample the buttons again: after `537E` masks input while hurt,
  an interrupt's call to `0602` brings it back for the rest of that update.
  The port samples input once per update (not reproduced).
- **Bridge's porcupine** (type 45, `82FB`): its upper spikes are tested
  before its body. A fast roll can skip the narrow band where the body can
  be hit (*hypothesis* for a reported unfair hit; not reproduced).

---

## Sonic the Hedgehog 2 and Triple Trouble (Game Gear): the shared lineage

### Common design

- **Main loop and frame.** Each frame has two parts: the frame interrupt
  (VBlank services: controller, fades, uploads, the water line), then the
  program.
  - Long flows are blocking routines that wait for frames. Sonic 2's wait
    is `05C5`; Triple Trouble's is `084C`, with its breakpoint at `0851`.
  - Triple Trouble's object routines also wait for frames, so the port
    writes them as coroutines.
- **Tile compression** (Sonic 2 `1B00`, Triple Trouble `2E6B`; shared
  decoder `gg_engine::decode_tiles`):
  - the header has a word count of tiles at +2 and a word offset to the
    code bytes at +4; data starts at +6;
  - each tile has a 2-bit code, four codes per byte:

    | Code | Tile |
    | --- | --- |
    | 0 | all zero |
    | 1 | 32 literal bytes |
    | 2 | a 32-bit mask (bytes rotated right one bit per output byte) picks which bytes are literal and which are zero |
    | 3 | as 2, then each plane-byte pair is XORed with the row above (delta rows) |

  - "Mirrored" blocks map each output byte through the bit-reverse table at
    bank 0 `0100`.
- **Object scripts.** A bytecode family drives objects per (type, state):
  frames, durations, routine calls, removal.
  - Sonic 2 has 10 commands, including remove, hold and object tiles
    (`6539`).
  - Triple Trouble has 18, adding jumps, loops, field writes and FE frames
    (`5C19`).
  - One shared decoder takes each cartridge's opcode map
    (`gg_engine/objects/scripts.hpp`).
- **Sound driver.** Each game's revision of one design is described by a
  `DriverProfile`:
  - table addresses, request ranges and slots;
  - the tempo rule and fade step;
  - channel header size;
  - envelopes: ADSR (Sonic 2) or tables (Triple Trouble);
  - modulation tables (unused by Sonic 2);
  - PAL mode.
  - Each game also has a sampled SEGA voice (`VoiceProfile`).
- **Display.** `gg_engine/video/display.hpp` holds tile art, the background
  map, sprites, colours and the line-scroll changes. Triple Trouble's is a
  superset: art past tile 447, line scroll and world position.
  - Tiles 448-511 overlap the background map and sprite table in video
    memory. Sonic 2's intro sky shows those bytes as tiles.
- **Camera to scroll registers** (Sonic 2 `4AE1`). Register 8 = -(camera X
  + 1) and register 9 = camera Y + 17. Name table rows repeat every 28 and
  columns every 32. A screen pixel (X, line) therefore shows world
  (camera X + 1 + X, camera Y + 17 + line).

### Sonic 2 (GG)

- **Zones.** Under Ground, Sky High, Aqua Lake, Green Hills, Gimmick Mt.,
  Scrambled Egg and Crystal Egg, three acts each. Zone 7 act 0 is the
  ending.
  - The good ending runs the same code with object 5D visible. It is not
    yet exercised in lockstep.
  - Acts are started by patching `D295`/`D296` at `0953` in the tracer.
- **Zone mechanics.** Mine carts (Under Ground), rain and the hang glider
  (Sky High), bubbles, air and rising water with the line interrupt (Aqua
  Lake), swing poles and boulders (Gimmick Mt.), pipes (Scrambled Egg).
- **Objects.**
  - There are 20 object records (64 bytes in the original), run by scripts.
  - The window for objects to stay active is VDP 0-255 both ways (`603C`),
    and they are removed at -384..639. Placement reach (bank 1E, 8000) is
    byte-coarse, at least -128..383 horizontally.
- **Bosses.**

  | Zone | Boss | Parts |
  | --- | --- | --- |
  | Under Ground | 4B/4C/4D | |
  | Sky High | 48 | helpers 36/46/47/49; six hits, eight in an unused variant (`9D98`) |
  | Aqua Lake | 4A/4F | |
  | Green Hills | 42 | |
  | Gimmick Mt. | 44 | boulders 45 |
  | Scrambled Egg | 4E | |
  | Crystal Egg | 55 | fire 53/54 |

  The capsule is 40/41.
- **Camera** (`5C18`, `5CC7`). The aim is screen X 50 (B0 facing left), 78
  when the camera is held and 80 on the title or ending.
  - The dead zone is +-8 horizontally and +-4 vertically. It eases toward
    its aim at 2 (1) px per frame.
  - Only the low byte of Sonic's distance is compared, so the high byte is
    kept: Sonic 256 px or more ahead moves the target that far at once.
- **Screen pin.** `3BF5` always keeps Sonic between screen X 38 and C7 (the
  LCD's edges).
  - Boss arenas hold the camera with a scripted target (`4A21`, `D15F`
    bit 0).
  - The left limit follows the camera during boss approaches, at the
    capsule and at the sign (`4A3F`, `B8E4`).
  - Arena limits are not always equal (Gimmick Mt.: 0221-0430).
- **HUD.** The HUD is twelve fixed sprites at table entries 52-63 (`224A`,
  table `227B`).
  - *Hypothesis:* with many object sprites the table fill runs into them.
    The HUD went missing in a teleported Green Hills boss fight.
- **Water** (Aqua Lake).
  - Acts 1 and 3 draw water as a checkerboard of colour 0 and colour 4 in
    priority tiles (tile 150, 156-16A); surface tiles 1BA-1BC have a
    highlight row.
  - Act 2 has only the line interrupt's colours plus the wave object 0C.
  - When the water line reaches line 192, `05E8` fires only after the next
    frame interrupt. One frame has no split; the next frame is underwater
    over the whole screen (a one-frame flash in the original).
- **Rain** (Sky High 2). The drops are eight 8x16 sprites (tiles F6/F8,
  entries 2C-33). A drop 48 px above the camera gets table Y = D0, the
  end-of-list marker, so that frame shows no later sprites (nor the HUD).
- **Speed shoes.** The after-image object 03 exists, but monitor reward
  bit 2 does nothing in `4820`, so it never appears.
- **Unused object types.** 3D/3E (an enemy that is never placed or
  created), 0D (lava ball), 11-13 (emerald) and 15-17, which are never
  placed or spawned.
- **Unused sound commands.** EF and F1 point into data at `492C`; running
  them would execute data as code. No song uses them.

#### Sonic 2 original oddities (kept where observable)

- `0230`/`6025`: command 6 leaves IY on the free slot it found (or `DBC0`
  when none is free); Sky High's boss later registers that slot (`9D6A`).
- `879D`: the glider object calls `027E` with IX on itself.
- `98C3` (Gimmick Mt. boss): the rebound speed stores the unshifted low
  byte.
- `A2C9` (Aqua Lake boss) leaves HL/DE changed for its caller.
- `5C18`: the camera target keeps the high byte of Sonic's distance
  (above).
- `7743`: at the end of a tile list the finish runs twice.
- `186B`: the sprite table fill writes one entry more than its count.
- The attract flag is never cleared by the original (only the zone select
  does).
- The terrain probe above the map's first row reads the cartridge byte
  below the map's address (bank 1E mapped). This is modelled but no test
  reaches it.
- Sound driver (`4867-488E`): the effect scan leaves IX on a record, and the
  stereo routine takes its old bits from that record when a channel ends
  (fixed to match the cartridge in 0.16.0).
- More than 64 sprites in a frame (never seen) would overwrite the rain
  state.
- Register and scratch leftovers with no visible effect were dropped. For
  example, `1E93`'s RRD leaves digits in its scratch bytes, and `7743`
  selects bank 0.

### Triple Trouble (GG)

- **Program modes.** Boot `0000` → `053A` (init) → `0DFE`, the main dispatch
  on `D143` through table `0E1D`.

  | Mode | Screen |
  | --- | --- |
  | 0 | SEGA logo (`0E53`) |
  | 1 | intro (`0F3E`) |
  | 2 | title (`0FED`) |
  | 4 | title card |
  | 5 | play |
  | 6 | tally |
  | 7 | life lost |
  | 9 | on-foot special stage (zone 6) |
  | 0A | special stage results |
  | 0D | flight stage (zone 8) |
  | 18 | character select |
  | 19 | the title's sound test (`1BFE`) |

  - The frame interrupt is `0038` → `0576` (VBlank) or `085B` (line
    interrupt).
  - `D12A`/`D12B` mirror the slot 1/2 bank registers.
  - The pause (`20FB`) sets `D144` bit 0, which stops all of the interrupt's
    level services.
- **Sprites.** The shadow table is `DB00` (Y) and `DB40` (X, tile). `27A0`
  uploads it in a different order every other frame (`D135` parity).
- **Zones.** Great Turquoise, Sunset Park, Meta Junglira, Robotnik Winter,
  Tidal Plant and Atomic Destroyer, three acts each.
  - Sunset Park 3 is the train ride; Atomic Destroyer 3 ends with the last
    machine.
  - Zone 6 is the on-foot special stage and zone 8 the flight stage.
  - Zone 9 is the rival's (Nack's) scene after a flight; "zone 0A" is the
    short ending.
  - The level select lists entries 3Z+A.
- **Name table streaming.** The name table is streamed exactly as the
  original does (`45E5`/`4644` columns, `46AA`/`46F0` rows, `2CEB` blocks),
  so a stopped act leaves the same stale picture. A live act is drawn from
  the level map.
- **Player states** (selection):

  | State | Meaning |
  | --- | --- |
  | 0C/0D/13 | paths and tubes |
  | 10/0E | rolling |
  | 11, 26 | Sonic's flights (items 9, 0A) |
  | 12 | held in a bubble |
  | 15, 1A | peel-out |
  | 16 | shell ride |
  | 17 | act entry slide |
  | 18 | Tails' flight |
  | 20 | run-off |
  | 23 | board |
  | 24 | air spin |
  | 25 | breathing a big bubble |
  | 2A-2F | the train |
  | 31 | floating |
  | 32 | barrels |
  | 35/36/3D | overhang walls |
  | 3E | Meta Junglira's tunnel |
  | 41 | swimming |
  | 42 | snow blast |
  | 44 | after a boss |
  | 45 | the sound test's player |
  | 46 | end scene |
  | 47 | continue |
  | 4A | held during the rival's scene |
  | 4B | falling through Knuckles' trap |

- **Terrain kinds** (selection):

  | Kind | Meaning |
  | --- | --- |
  | 0C | dropping floor (`639E`) |
  | 0D | engraved blocks |
  | 0F | side bumper |
  | 12 | ramps |
  | 18 | barrels |
  | 1B | sinking blocks |
  | 1C | overhang ceiling |
  | 5 | breaks only under a shell rider |

  Kind 0D breaks from the side only for a rolling player at 3 px per frame
  or more (`6910`/`6937`), from above when rolled onto (`6330`) and from
  below when risen into (`6B70`).
- **Items.**

  | Item | Effect |
  | --- | --- |
  | 1 | special stage entrance (type 0E; needs 50 rings) |
  | 5 | invincibility |
  | 8 | a life |
  | 9 / 0A | flights; 0A is the jet board, which keeps the facing at pickup: Up and Down steer, Left and Right do not turn it |
  | 0C | the snow board |
  | 0F | Tails' flight |

  Each act's item-1 box is the way into the special stage (for example
  Great Turquoise act 2's at 1388,22E).
- **Bosses.**

  | Boss | Type | Notes |
  | --- | --- | --- |
  | Great Turquoise | 50 | |
  | Sunset Park train | 51 | |
  | Meta Junglira | 52 | arm of 24 pieces whose sprite offsets it writes into `DBE6-DC45` for the sprite code to read from RAM |
  | Robotnik Winter | 53 | rolling balls 32 |
  | Tidal Plant | 54 | 16 hits; drowning count 11 |
  | Atomic Destroyer chase | 55 | three rounds |
  | Last machine | 66 | bank 0F |

  The last machine has three forms:
  1. hopping, ten hits;
  2. a core (16 hits), bombs, and a hatch whose opening allows the one
     beating touch;
  3. in room A30-A90, with lightning: eight hits.
- **Zone ends.** Knuckles (type 35) springs a trap: the ground opens
  (animation 9, `82B5`, rows from table `832C`) or, in Robotnik Winter, the
  ice gives way (`8625`). The player falls (state 4B) and the act ends.
- **Robotnik Winter 2's sign.** The sign stands on a plateau walled off by
  a pillar that reaches the map's top. The only way up is the ramp block 35
  (kind 12, `6113`): spin dash left out of the loop's right exit, run the
  loop backwards, and the ramp throws you up the pillar.
- **Tidal Plant.** Every switch releases a big bubble (type 3B) that puts
  the player into swimming (state 41: rising 1-2, Up 1, sideways 1 px per
  frame, no air counted; a jump ends it). Both acts have a row of engraved
  blocks joining the start's side to the sign's side; see plan.md for the
  open question of how to get through.
- **Special stages.**
  - On foot (mode 9): a 1:30 countdown (`2FE8`), side springs (4A) and the
    emerald (4E). The rival's zone-6 scene has its own camera, and the HUD
    slides away (0B).
  - Flights (mode 0D):
    - course at 1C:9864/9A7C;
    - a depth projection (`552C`) and horizon line interrupts (`095A`);
    - rings (0C), obstacles (37), clouds (20) and stars (46/6B);
    - winning needs 80 rings; zone 9 (Nack, types 5C-5E) follows;
    - stages 3 and 5 have guardians (types 5A, 5B).
- **Endings.**
  - With fewer than six emeralds (`17F4`, `1949`): the rival tosses the
    emeralds in "zone 0A", then back to the SEGA logo.
  - With all six (`1824`): the credits' objects 63/65/68-6A, with the text
    at 1C:BE40.
  - The end scene comes first: craft 57, capsule 58, part 59, cage 61 and
    the player's state 46.
- **Train ride** (Sunset Park 3): player states 2A-2F, director type 13
  with speed tables and map redraws, the roof ring 15, frame service
  `1C72`, and line-interrupt scroll bands (`0625`/`0930`).

#### Triple Trouble original oddities (kept)

- **HUD ring frame.** A ring placed before the HUD ring's first spin takes
  frame 3C, whose record lies in bank 0: code bytes are read as sprite data
  for a frame.
- **Map address.** Zone 1 act 2's map decompresses to `D001`.
- **Board.** Its first landing note reads an opcode byte (`A911`).
- **Crawler.** Its trail shift leaves `DC45`.
- **Roller.** A roller broken by the player still moves and picks its state
  that update.
- **Wrong IX on spawn.** Knuckles' falling-ground spawn in the interrupt
  writes its tag into sprite entry 49's Y, because IX still points at
  `DB00`.
- **Sound test spawn.** The title's sound test spawn tags the IX the
  title's map animations left (`228E`: `D47F`), so FE lands in `D4B0`.
- **Unbounded floor check.** The floor check reads the block above
  (`6671`) without bounds. Above the map's first row the address falls
  below C000 and reads bank 0E (paged at `667B`).
- **Code as data.** With no act loaded, code bytes are read as row offsets
  (`000A`).
- **Garbage frame table.** A removed object's same-pass frame lookup reads
  a garbage table in the original (skipped; no visible effect).
- **Refresh register.** The final machine's choices and some enemies read
  `LD A,R`.

---

## Sonic Advance (Game Boy Advance)

### Boot and frame

- **Boot.**
  1. The ARM entry `080000C0` sets the stacks and the IRQ dispatcher
     (copied to IWRAM `030004B0`).
  2. Main `08001494` runs the init `080003EC`. If RCNT is not 8000 the
     game takes the multiboot child path.
  3. Game start `0803CB90`: the save check (`08012C5C`), then the language
     select (`0806A700`) on a fresh save, or the SEGA logo.
- **Main loop** (`0800089C`), per frame:
  1. sound engine (`080977D8`, unless flag 4000);
  2. controller (`08001224`): held `03001B50`, new `03001B78` with
     auto-repeat 20/8 frames per button, released `03001FAC`;
     demo record and playback through `030060D8`;
  3. link cable (`08006D54`);
  4. the task runner (`080016AC`);
  5. `VBlankIntrWait`;
  6. the commit (`08000980`): display, layer, palette, window, blend,
     scroll, affine and HBlank effects, OAM, VBlank callbacks and the
     VRAM queue workers.
- **Global flags** (`03001100`):

  | Bit | Meaning |
  | --- | --- |
  | 1/2 | upload palettes |
  | 4 | HBlank DMA effect |
  | 8 | HBlank callbacks |
  | 10 | VBlank callbacks |
  | 100/200 | multiplayer / multiboot |
  | 400/800 | pause modes |
  | 4000 | sound off |
  | 8000 | soft reset pending |

  A+B+Start+Select is the soft reset, read in the VBlank handler.
- **Task scheduler.** The whole game runs as tasks:
  - 128 records of 14h (20) bytes at `030006B0`, in a doubly linked list sorted
    by priority; a new task goes after all others of equal priority;
  - create `08001588`, destroy `08001620`;
  - task data lives in an IWRAM heap at `030023A0`, which compaction moves
    (`08001864`);
  - the runner reads the next task before calling the current one;
  - in pause mode only tasks with `flags & 5 == 4` run;
  - state switches usually `bx r0` (`08099844`), running the new state in
    the same frame;
  - the record pool is LIFO, and the port models which creator owns each
    record (needed below).
- **Water timing.** The water's VCount interrupt and VBlank callback run
  during the *next* frame's work, so colour memory at the hand-over already
  holds the underwater colours for the same frame.
  - Only the act's first frame (loading) and the game-over save frame run
    late.

### Graphics data

- **Sprites.** Sprite animation is a script interpreter (`080034CC`) over
  the table at `080D868C`:
  - [0] animation → variant → command list;
  - [1] 12-byte frame records;
  - [2] OAM attribute lists;
  - negative words are commands (12 kinds, handlers at `0809BDF8`).

  Sprites are queued into 32 priority layers (`08004158`, `080047FC`) and
  built into OAM in layer order (`08004894`). The sprite tile allocator
  hands out 128-byte blocks (map `03004A30`).
- **Backgrounds.** Background assets are indexed by the table at
  `084A6C40`. The map redraw is queued (`030010C0`, worker `08001AA4`).
  - The act layers are a 31x21-tile window redrawn whenever the camera
    crosses a tile. The fine scroll reaches the screen one frame after the
    map.
- **Compression.** Data is decompressed by BIOS services (LZ77 and RL)
  following GBATEK.

### Stage data (per act, `084A6DA4[zone]`)

- **Collision.**
  - [0] tile height maps, 8 bytes per tile: a high nibble gives a column's
    floor height and a low nibble a row's wall width; -8 reads as 8;
  - [1] tile angles;
  - [2] blocks of 12x12 tile map entries (0x120 bytes each);
  - [3]/[4] block layouts of the two collision layers;
  - [5] tile flags (2 bits per tile; bit 0 = passable for probes with
    layer bit 7);
  - layout and pixel width and height follow.
  - Sensors are `0803FF50` (floor/ceiling) and `0803FE10` (walls).
    `08040080` finds a tile's map entry, with a memo cache that Ghidra folds
    into constants (read it in the disassembly).
- **Objects.** Each act has three run-length compressed layouts in
  256-pixel cells:

  | Layout | Table | Record size | Creators |
  | --- | --- | --- | --- |
  | interactive objects | `080B7F94` | 7 bytes | `080B806C` |
  | item boxes | `080B7FDC` | 3 bytes | `0801E5D8` |
  | enemies | `080B8024` | 7 bytes | `080B823C` |

  A spawned object marks its record's X byte FE and restores it when it
  leaves. Rings use their own run-length layout (`080B7F4C`).
- **Act start.** The act start table is `084A6A18`. Each act's start sets
  colour cycles, music and its far-layer routine (`084A6BA8`).
- **Camera** (`030059D0`): an 8 px dead zone, steps 10h/0Ch, an air lead of
  0-18h and a look offset of +22.

### Player

- **Record.** The player record is at `03005A20` (0x90 bytes; the partner's
  is at `03005AB0`). Its field list is in
  `src/sonic_advance_gba/game/player.hpp`.
- **Movement** (`08047378`) is picked by flags & 6: ground, air, rolling,
  rolled in the air.
  - Physics constants are in `0804421C` (jump 4E0/2A0) and `08044A38`
    (water).
  - Walking is `08044F48`, with friction 8.
  - Air collision (`08042E08`) goes by the direction of movement
    (ArcTan2).
- **Input history.** The game keeps an input history (`030060F0`,
  `03006170`) for double taps (`0804D02C`).
- **Death** (`080454D8` with flag 80). Sonic falls until he is below the
  view, then after 3Ch frames:
  - time over if the clock is over 36000 with the limit on;
  - otherwise a life is lost and the act restarts from the checkpoint;
  - at 0 lives, game over (1) or continue (2, with continues at
    `03005018`).
- **Super Sonic** (`08049864`, The Moon Zone only):
  - a 16-position trail and three afterimages;
  - a dash on the other button, with an 8-frame wait;
  - one ring is spent per second until the boss is beaten, starting from
    50;
  - a hit knocks him back without ring loss.

### Zones

- **Order.**

  | Zone | Acts |
  | --- | --- |
  | Neo Green Hill | 0, 1 |
  | Secret Base | 2, 3 |
  | Casino Paradise | 4, 5 |
  | Ice Paradise | 6, 7 |
  | Angel Island | 8, 9 |
  | Egg Rocket | 10 |
  | Cosmic Angel | 11 |
  | X-Zone | 12 |
  | The Moon Zone | 13 |

  Acts 14-17 are multiplayer stages.
- **Act highlights.**
  - Egg Rocket: past a checkpoint the rocket is already up and the clock
    counts down from five minutes. Its checkpoints are invisible triggers
    for lift-off and stage separation.
  - Cosmic Angel: gravity reversal (kind 109, with reversed spikes,
    springs and lost rings).
  - The Moon Zone runs in display mode 1, with a rotated layer for the far
    picture. The boss's body is drawn into a rotated layer's map.
- **Bosses.**

  | Boss | Kind | Notes |
  | --- | --- | --- |
  | Neo Green Hill | 22 | chain and hammer; spins after hit 7 (6 with save byte +18) |
  | Secret Base | 23 | |
  | Casino Paradise | 24 | |
  | Ice Paradise | 25 | |
  | Angel Island | 26 | run from a cartridge table of 25 state records at `084A70C0` |
  | Cosmic Angel | 27 | pendulum pod under a nine-plank bridge |
  | X-Zone | 28-30 | |
  | The Moon Zone | 31 | 18 parts; head hits 8 - 2 x save byte +18 |

  Eggman passing by is kind 21.
- **Endings.**
  - The ending (`08065850`, after act 12), then the staff roll
    (`0805E3CC`).
  - The true ending (`080670C4`, after act 13) sets save +08 = 0F and
    +424 = 1.
- **Special stages** (`080298D8`; stage = `08480034[act]`, ring targets
  `08480040`):
  - the hero is a 3D point with depth projection quotient(k⁴, 198h);
  - failing the first gate or ending below the ring target goes to the
    results without an emerald;
  - the results add 10000 for an emerald plus rings x 100, and a life per
    50000 in story mode;
  - continues grow by the capsule items, plus one if the emerald was
    already held.

### Save

- **Records.** "PIRO" records of 0x430 bytes in 10 Flash slots; the valid
  one is picked by its checksum (`08012F78`).
- **Known fields.**

  | Field | Meaning |
  | --- | --- |
  | +08 | zones reached |
  | +18 | a difficulty-like byte (changes boss hit counts and which enemies are made) |
  | +1C | emeralds (bit per stage) |
  | +424 | true ending seen |

- **Zones reached.** `zones_reached` is acts cleared: the results raise it
  to act + 1, act 13 sets 0E and the true ending 0F. Once it is nonzero,
  the character select goes to the act select.

### Sound engine

- **Init** (`08097760`).
  - The mixer (ARM) is copied to IWRAM `03000000`; SoundInfo is at
    `02001400`.
  - Five sampled channels, master volume 15, 10512 Hz (176 samples per
    frame), in a ring of 9 frames.
  - Four music players (`086D767C`), 16 tracks each.
  - Tone generator records are at `02002440`.
- **Per frame.** Players run in opening order 0-3, then the tone
  generators, then the mixer at its ring position.
- **Sequencer.**
  - tempo 150 ticks;
  - commands B1-CF (table `086D54F0`), including memory access and
    extended commands;
  - voices by key split or drum kit;
  - channels allocated by priority, then by later track.
- **Mixer.** ADSR and echo per channel; 8-bit wrap-around accumulation with
  no clipping; fixed-rate or linearly interpolated sampling (23-bit
  fraction).
  - Reverb (+05) is never used, and the player priority byte (+0B) is
    always 0, so starting a song never refuses.
- **LFO quirk.** It computes 0x80 minus the unwrapped sum.
- **Song table.** `086D76AC`. The music manager is `0803CB18`/`0803C944`:
  - zone songs `084A69E4` and jingles `084A6A08`;
  - the restore after a jingle (reset, volume 4, fade in 4, tempo 200h with
    speed shoes);
  - the ring sound alternates players 1 and 2, panned +-40h.

### Original bugs and oddities (leftover memory)

- **HUD.** Its flashing zero starts from leftover task memory (the title
  card's "shown" flag).
- **Special stage counter.** Its blink starts from the welcome streaks'
  record +48, read after the record was freed.
- **Capsule rings.** A capsule ring's +6D is read from the previous task's
  record (*hypothesis*, not checked against a run).
- **Ending streaks.** The ending fall's streaks start wherever their task
  record left them, so the original shows three streaks at leftover places
  for five frames.
- **Boulder** (Angel Island). Over an empty tile it rolls by the stack's
  leftover angle, because `08040164` does not write it.
- **Lunger.** Its creator never sets the attack cooldown (+4E) and second
  hitbox. A lunger made again in the same heap block keeps the old one's
  hitbox and hurts from its first lunge frame.
- **Cosmic Angel boss shots.** They reach the boss through the gun's task
  record. Once the gun has ended, a shot reads the freed record until a new
  task takes it.
- **Freed sprites.** A queued tile copy of a sprite whose task ended copies
  from freed memory.
- **Swinging handle.** It transforms with a frame that was never set.
- **Colour loads.** Animation colour loads beyond the palette (palette FE)
  are written to `03003AA0`, which is never read.
- **Small bubbles.** A size-4 small bubble writes a stray sprite table entry
  (124) through the tile free's wrapped index.
- **The Moon Zone's rotated map.** The sprite-to-rotated-map writes keep the
  even entry in a stack word across rows. A row ending on an even place
  zeroes the next entry (a misaligned halfword read).
- **Eggman's escape** draws at fixed tile places where the boss's freed
  tiles were. It relies on the allocator placing the boss there.
- **Results.** The results' continues line shows only the first digit, so
  10 or more continues show "1".
- **Late frames.** Frames whose work runs past the vertical blank's start
  hand over late; the picture differs in a few lines (band edges, single
  colours). After a long restart frame the HBlank DMA makes one extra
  transfer before line 0, so the line scroll is one line early (Casino
  Paradise restarts).
