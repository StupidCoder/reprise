# Game documents

Each game of this engine family has two documents:

- `plan.md`: what remains for a complete, playable re-implementation, and
  the known bugs and differences;
- `game.md`: what reverse engineering taught us: how the game works, how its
  data is stored and encoded, bugs in the original, and how findings were
  checked.

| Game | Platform | Cartridge | Documents |
|---|---|---|---|
| Curious Village | Nintendo DS | `A5FP` (Europe) | [plan](curious_village_ds/plan.md) · [game](curious_village_ds/game.md) |
| Pandora's Box | Nintendo DS | `YLTD` (Germany) | [plan](pandoras_box_ds/plan.md) · [game](pandoras_box_ds/game.md) |
| Lost Future | Nintendo DS | `C3JD` (Germany) | [plan](lost_future_ds/plan.md) · [game](lost_future_ds/game.md) |
| Spectre's Call | Nintendo DS | `BLFD` (Germany) | [plan](spectres_call_ds/plan.md) · [game](spectres_call_ds/game.md) |
| Miracle Mask | Nintendo 3DS | `AKKD` (Germany) | [plan](miracle_mask_3ds/plan.md) · [game](miracle_mask_3ds/game.md) |
| Azran Legacy | Nintendo 3DS | `AL6D` (Germany) | [plan](azran_legacy_3ds/plan.md) · [game](azran_legacy_3ds/game.md) |

The two 3DS games share one engine. *Miracle Mask*'s `game.md` describes it,
and *Azran Legacy*'s covers what differs.

## How the engines relate

| Game | Scripts | Interpreter | Sound | Movies |
|---|---|---|---|---|
| Curious Village | GDS with structured flow control | shared GDS interpreter, own dialect | SDK sound archive (SDAT), ARM7 sequencer | Mobiclip MODS |
| Pandora's Box | GDS | shared GDS interpreter | own resident sequencer (SMD/SWD/SED) | Mobiclip MODS |
| Lost Future | LSCR (scenes, puzzles, menus) | LSCR machine | the same sequencer | Mobiclip MODS |
| Spectre's Call | GSS1 (everything) | GSS1 machine | the same sequencer | Mobiclip MODS |
| Miracle Mask, Azran Legacy | XQ32 | XQ32 machine (GSS1 widened) | NintendoWare (CSAR, CSTM) | moflex (the DS frame routine, extended) |

Shared native code: the cartridge readers and decompression, the
handwriting recogniser (one database for every game), the Mobiclip frame
routine, the DS 2D/3D pipeline and the sound drivers. Each game's own rules
live in its own files (`a1_*` .. `a6_*` in `src/`).

## Conveniences in every game

These are additions, not original behaviour. The rules are in the research
notes (`.analysis/docs/skip.md`, `checkpoints.md`, `touch-areas.md`):

- music player;
- movie theatre;
- puzzle mode;
- minigame free play;
- skipping (movies, timed waits, voices);
- start at the title;
- one automatic checkpoint per game, written whenever a room is ready;
- touch-area help;
- pinch zoom.

A game is not complete without them.

## Sources and rules

All findings come from the user's own cartridge images and from running
them. The reference emulators are measuring instruments, never part of the
port. No existing disassembly, decompilation, port, symbol map or
game-specific guide was used. The research notes, tools and evidence live
in `.analysis/` (local, not in the repository). These documents name them
where they hold more detail.
