# BlueStreak: plan

What is left to do before each game is a fully playable re-implementation,
plus every known bug and difference.

How the games work is in [game.md](game.md). The detailed per-game status
notes are in `.analysis/` (not in git).

Status as of 0.17.0 (2026-10-29):

| Game | State |
| --- | --- |
| Sonic 1 (GG) | Complete |
| Sonic 2 (GG) | Complete; a few paths not run in lockstep |
| Triple Trouble (GG) | Playable start to finish; a few paths not run in lockstep |
| Sonic Advance (GBA) | Playable as Sonic from power-on to the ending; many modes missing |
| All other library entries | Planned, nothing started |

Each list is ordered by priority. **Bug** marks something that behaves
wrongly for a player. **Gap** marks something not reconstructed yet.
**Unverified** marks something written from the code but never compared
against a run of the original. **Difference** marks a known, accepted
difference from the original.

---

## Sonic Advance (GBA): towards fully playable

### Gaps that stop the game (the module stops with a message)

1. **Act select** (`08061914`, about 6 KiB with its tasks). Once one act
   has been cleared, the character select goes to the act select. So a new
   game after a game over or the ending, in the same run, stops. This is
   the most visible gap; do it first.
2. **Continue screen** (game over mode 1: `08056F20`/`08056CAC`/`08056F6C`).
   It is reached by a game over with continues earned in the special
   stages.
3. **Time over** (`0805B9B4`, `0803D638`): ten minutes in one act. Includes
   the Egg Rocket countdown's time over.
4. **Timed block, cycle 3** (Casino Paradise, `0807E78C`).
5. **`0801758C`**: not yet reached; what it does is unknown.
6. **Sound engine extended commands above 0B**: no song seen so far uses
   them.

### Gaps: modes and characters

7. **Saves.** Keep the cartridge's Flash save: the "PIRO" records and the
   writes between acts and at the endings. These writes do nothing now, so
   every start is a first start with no emeralds.
   - Without saves, normal play cannot reach The Moon Zone, Super Sonic or
     the true ending (they need all seven emeralds and every character to
     have cleared X-Zone).
   - Then verify the true ending's way in X-Zone (`0803A51C`, `08038524`,
     player states 32h-34h) with a real save, and the last picture after a
     true ending's staff roll with save byte +1C 7Fh.
8. **Tails**: partner process, sprite, movement, the following code, the
   chute tails (`08047DE4`, written but never run). Then **Knuckles** and
   **Amy**, including Amy's hammer against objects (`0800A768`) and
   breakable ice.
9. **Main menu entries**: options, time attack (`0806A170`), the language
   select (`0806A700`; the game runs in English now), soft reset.
10. **Tiny Chao Garden** and the **multiplayer** modes (VS, chao hunt,
    acts 14-17, link-cable paths, multiplayer respawn). These need a design
    decision first: local multiplayer in one app, or leave them out.

### Unverified

11. **No continuous playthrough.** The game was never compared in one run
    from power-on to the ending. It was verified act by act and boss by
    boss in scripted runs.
12. **Special stages.** Only Casino Paradise act 1's stage (index 4) was
    run. The other six stages' object lists, and the dash and homing moves,
    springs, bombs and capsules beyond those passed, are written from the
    code.
    - Not run either: going back onto the special spring after the results
      (player +6E makes it a plain spring).
13. **Secret Base boss**: verified through five hits and a death; its
    defeat and the act results are not.
14. **Ice Paradise boss**: hitting it from the icicles in normal play is
    not exercised.
15. **Branches written from the code without a run**:
    - the swinging block's and spiked block's crushes;
    - the vortex's second rider;
    - the warp's death handling;
    - the pipe bumper;
    - the circling platform's chain;
    - the sled's crash and landings;
    - the steam vent's riders;
    - the frozen diver's flight;
    - boss 29's skip after a checkpoint;
    - The Moon Zone boss's debug flag and the camera end's interrupt
      restore.
16. **Objects** were checked around the first instance of each kind in each
    act. The serpent, cart and lift only around their first instances.
    Casino Paradise acts 4/5 and Ice Paradise were never played through.
17. **Capsule rings**: a capsule ring's +6D comes from the previous task's
    record. This is a hypothesis, not checked against a run.

### Shell features for Sonic Advance

18. Quick saves, a level selector, a sound test (the engine already plays
    any song) and enhancements. Widescreen and smooth motion need their
    own design. Enhancements must only upgrade effects the original
    shows.

### Bugs and differences

- **Bug: Ice Paradise act 7 patches.** Small patches in rows 146-159 show
  the neighbouring colour-cycle entry (one red unit off), while the view
  shakes and in bursts during the boss.
  - The sprite table, video memory, colours, registers and the line table
    all agree at the hand-over.
  - *Hypothesis:* the original draws map rows scrolled in at the bottom
    while the picture is being drawn.
- **Bug: Angel Island boulder.** Over an empty tile it uses angle 0. The
  original uses a leftover stack value, so runs where it matters diverge
  (the warp of act 8 kind 87).
- **Difference: lunger.** It starts clean (cooldown 0). The original keeps
  a previous lunger's second hitbox when it is made again in the same heap
  block.
- **Difference: ending streaks.** The fall's streaks start at 0, so none
  are shown. The original shows three at leftover places for five frames.
- **Difference: title logo.** In the last title picture before demo 0
  (harness frame 4182), the rotated logo layer is one pixel off in rows
  8-85 in the original. Registers, colours and memory agree; the cause is
  not found.
- **Difference: overrun frames.** Late hand-overs in the original are not
  reproduced, by design (no hardware failures). Where they show:
  - the act's first frame;
  - the save frame;
  - Casino Paradise restarts: the line scroll one line early;
  - Ice Paradise's first picture after a restart: about 20 lines;
  - the first picture of the true ending;
  - busy Cosmic Angel frames.
- **Difference: per-line sprite limit.** The hardware's sprite limit is
  probably what hides a few sprite pixels in two warps. It is not modelled,
  by design.
- **Unverified hardware model.** The sound hardware output (`src/gba/apu.cpp`)
  follows GBATEK and is not compared with real hardware: tone generator
  levels, mixing ratios, the wave bank switching and bias quantisation.
  Wave memory (`04000090`) is not compared by the harness.
- **Leftover-memory reads.** Where a traced run showed where the value
  comes from, the port reproduces that. Runs that were not traced may meet
  other values:
  - the HUD's flashing zero;
  - the special stage counter's blink;
  - the Cosmic Angel boss shots' freed gun record;
  - sprite tile copies from freed records.
- **Unverified: Eggman's escape** uses fixed tile places. This relies on
  the allocator placing the boss there, which was true in the verified run.
- **Original bug, kept:** the results' continues line shows only the first
  digit, so 10 or more continues show "1".

---

## Triple Trouble (GG)

1. **Gap: unreconstructed routines.** If the game reaches a routine that is
   not reconstructed, it stops with a message. Keep running the random
   player (ggdiff) and the explorers to find any that remain. The five
   found so far are done.
2. **Unverified: Tidal Plant acts 1 and 2.** No route was found in the
   original through the row of engraved blocks (kind 0D) that joins the
   start's side to the sign's side.
   - Act 1: 700-7BF, Y 240-25F. Act 2: 260-2DF, Y 620-63F.
   - Tried without success: a spin dash off the ledge loses the rolling
     bit; a spin dash and jump reach the wall below the row; jumps out of
     a swim are too slow; the jet board.
   - *Hypothesis:* the board ridden along a row from a distance, or another
     mechanism.
   - Both acts are verified only from a test setup past the row.
3. **Unverified: special stages 3 and 5.** They are verified from near
   their tops through the guardian fights (types 5A, 5B), not through a
   full climb. Explorers have not found the climbs.
4. **Unverified: bosses.** Tidal Plant's boss is verified through 11 of 16
   hits. Sunset Park's train boss and Atomic Destroyer's are verified "as
   far as traced". Do full defeat runs.
5. **Unverified: Tails.** States beyond flight and float have not been
   run.
6. **Gap: enhancements.**
   - The water enhancement (as in Sonic 2) is not available yet.
   - The special stages, flights, zone 9 and the train ride are not
     widened.
7. **Difference: refresh register.** `LD A,R` is replaced by a generator,
   so the final machine's and some enemies' random choices differ from the
   original.
8. **Difference: loading pauses** are modelled (`timing.cpp`), not exact.
9. **Difference: water line timing.** The water line's flag bit (`D11E`
   bit 4) is not compared while the line is armed. The line interrupt's
   timing against CPU time is not modelled.
10. **Housekeeping:** the "Not yet" paragraph in
    `.analysis/triple_trouble_gg/PLAN.md` is stale (the flights, the full
    ending and Robotnik Winter 2 are done). Update it.

---

## Sonic 2 (GG)

1. **Unverified objects.** Path platforms (21), the moving swing pole (2A)
   and the falling block (2C) were checked separately but never in recorded
   lockstep play.
2. **Unverified: good ending.** The final scene (act 1, object 5D visible)
   was not compared in lockstep.
3. **Unverified: other paths.** The reference suite does not exercise:
   - the continue screen;
   - parts of the Under Ground, Sky High, Green Hills, Scrambled Egg and
     final boss fights;
   - the game's own pause;
   - rare gimmicks (bubble carry, traps, drop blocks, the second hopper).

   Clang source coverage was 81% of the reference's functions.
4. **Unverified: widescreen arenas.** The Under Ground and Crystal Egg
   boss arenas and the capsule's descent in the expanded view were checked
   by reading the code only.
5. **Bug: Aqua Lake 2 shimmer.** In the expanded view, the surface shimmer
   sprite (object 0C) covers only the LCD's width.
6. **Bug: monitor pop at 21:9.** One monitor appeared 19 px inside the
   right edge while the camera scrolled 4 px per frame. Not investigated.
7. **Gap: unused enemy.** Types 3D/3E are not reconstructed; the game never
   places or creates them.
8. **Gap: unused sound commands.** A song using commands EF/F1 stops the
   game with a message. No song does.
9. **Differences:**
   - Loading pauses are modelled: the attract sequence matches within 0-8
     frames outside the demos. The interrupt handler's own length is not
     counted.
   - `LD A,R` (the rain drops, a few objects) uses a generator.
   - The fast tally step rate (3 per frame while a button is held) is an
     estimate.
   - The terrain probe above the map's first row is modelled but
     unverified.
   - The water line is applied after the frame's program (*hypothesis*).
   - The HUD running out of sprite entries with many objects is a
     *hypothesis*, not checked against the original.
   - Slowdown is not reproduced (about 15% of attract demo frames in the
     original), so the demos end 50-200 px off.
   - Stale background tiles behind a fast camera (Scrambled Egg's pipes)
     are not reproduced.

---

## Sonic 1 (GG)

Complete. Remaining differences, all accepted:

- **Loader waits.** The act loader's display-off waits (`186C`, `1A14`)
  and the map's loading wait (`0BDD`) are not modelled. Map entry and the
  16 settling updates come one or two frames early.
- **Emerald jingle.** The extra wait inside the emerald jingle's music
  restore (`5535`) is not reproduced.
- **Input sampling.** Input is sampled once per update. The original can
  sample it again in an interrupt in the middle of an update (game.md).
- **Display latch.** Screen changes show at once. The hardware latches
  scroll, sprites and colours at the next VBlank, so the first GAME OVER
  frame shows one frame early.
- **Menu screens.** Their four off-screen rows of leftover gameplay video
  memory are not reconstructed.
- **Water colours.** Labyrinth's water colour band is measured (nine lines,
  per-line mix), not computed from the interrupt's timing.
- **Region bit.** The export/Japan bit is fixed to the dump's observed
  value; there is no region option.
- **Linked rooms.** A direct start into the half-written linked room
  (header 21) is refused, as there is no previous room to supply its other
  half.
- **Unverified: game over in a linked room.** No capture of the original
  was taken.

Research infrastructure: the Sonic 1 regression harness lives only in the
scratchpad (`check.sh`, `regress.cpp`, `import.cpp`, `data/`), and the OS
temp cleaner has deleted it before. Move it into
`.analysis/sonic1_gg/regress` so behaviour-sensitive refactors can still be
checked. Its known differing widescreen runs are X5, X28, X30 and X32;
runs L100/L101/X100/X101 exist only in the newer output.

---

## The collection and shell

1. **Platforms.** Windows and Linux builds have never been tried. The code
   is SDL 3 and C++20, so first steps are a CI build and a check of the
   file dialogs and the data folder location.
2. **Sonic Advance in the shell.** It needs per-game feature flags for
   quick saves, the level selector and the sound test (see above).
3. **Engine sharing.** Sharing stays within a lineage. A future Game Gear
   game of the Sonic 2 / Triple Trouble lineage (for example Sonic Chaos)
   should start on `src/gg_engine` with profiles. Sonic 1 and Sonic Advance
   stay separate.
4. **Shell polish.**
   - `design/logo-exploration.png` (392 KiB) is committed only as the
     logo's provenance; drop it if repository size matters.
   - Harmless linker warning: "ignoring duplicate libraries:
     libplatform.a".

## Planned games (not started)

Library entries shown as *planned*:

| Entry | Dump on hand | First step |
| --- | --- | --- |
| Sonic Advance 2 (GBA) | yes | Reuse `src/gba`; same developer as Advance 1, so its scheduler and sound engine are hypotheses to check against this ROM, not facts |
| Sonic Advance 3 (GBA) | yes | As Advance 2 |
| Sonic Rush (DS) | yes (Europe) | Needs a DS hardware layer: two 2D engines and the 3D engine for its bosses and special stages; ARM9/ARM7 research tracer |
| Sonic 1, Sonic 2, Sonic 3, Sonic & Knuckles, Sonic 3 & Knuckles (Mega Drive) | no | Needs dumps; a Mega Drive hardware layer (VDP planes, sprites, H-interrupts; YM2612 + PSG sound) and a 68000 research tracer |
| Sonic CD (Sega CD) | no | Needs a disc dump; Mega Drive layer plus CD audio, PCM and the sub-CPU's graphics |

Every new game follows the same method:
- static analysis and tracing of the user's dump only;
- a clean C++ re-implementation, with no CPU emulation or RAM images;
- data run from the extracted pack;
- lockstep verification against the original;
- every open item recorded here.
