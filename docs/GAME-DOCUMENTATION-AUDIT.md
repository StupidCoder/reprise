# Game documentation audit — rechecked 2026-10-29

This supersedes the earlier audit. Checked the current filesystem using the user’s conventions: standalone games in their own folders, each Layton game under `layton/docs`, and one collection-level pair for Sonic. No game research was edited.

## Result

- All **12 standalone games** have non-empty lowercase `game.md` and `plan.md` files.
- All **6 Layton games** have non-empty pairs under `games/layton/docs/<game-id>/`, linked by their docs README.
- **Sonic now has its collection-wide pair** in `games/sonic/docs/`: substantive research notes and a plan covering the individual games.
- Total: **19 of 19 required documentation sets present** (38 of 38 files). This verifies presence and substantive text, not every research claim or plan’s completeness.

## Inventory

| Documentation set | game.md | plan.md |
| --- | --- | --- |
| bruce_lee_c64 | [game.md](../games/bruce_lee_c64/game.md) — 5,449 words | [plan.md](../games/bruce_lee_c64/plan.md) — 779 words |
| cannon_fodder_amiga | [game.md](../games/cannon_fodder_amiga/game.md) — 17,002 words | [plan.md](../games/cannon_fodder_amiga/plan.md) — 652 words |
| fa_18_interceptor_amiga | [game.md](../games/fa_18_interceptor_amiga/game.md) — 8,211 words | [plan.md](../games/fa_18_interceptor_amiga/plan.md) — 786 words |
| fort_apocalypse_c64 | [game.md](../games/fort_apocalypse_c64/game.md) — 4,898 words | [plan.md](../games/fort_apocalypse_c64/plan.md) — 673 words |
| marble_madness_amiga | [game.md](../games/marble_madness_amiga/game.md) — 10,938 words | [plan.md](../games/marble_madness_amiga/plan.md) — 876 words |
| mindwalker_amiga | [game.md](../games/mindwalker_amiga/game.md) — 13,684 words | [plan.md](../games/mindwalker_amiga/plan.md) — 647 words |
| rock_n_roll_amiga | [game.md](../games/rock_n_roll_amiga/game.md) — 6,287 words | [plan.md](../games/rock_n_roll_amiga/plan.md) — 988 words |
| spindizzy_worlds_amiga | [game.md](../games/spindizzy_worlds_amiga/game.md) — 6,220 words | [plan.md](../games/spindizzy_worlds_amiga/plan.md) — 892 words |
| spy_mouse_ios | [game.md](../games/spy_mouse_ios/game.md) — 5,741 words | [plan.md](../games/spy_mouse_ios/plan.md) — 1,720 words |
| tower_of_babel_amiga | [game.md](../games/tower_of_babel_amiga/game.md) — 6,964 words | [plan.md](../games/tower_of_babel_amiga/plan.md) — 966 words |
| wings_of_fury_amiga | [game.md](../games/wings_of_fury_amiga/game.md) — 14,063 words | [plan.md](../games/wings_of_fury_amiga/plan.md) — 768 words |
| wizball_c64 | [game.md](../games/wizball_c64/game.md) — 6,265 words | [plan.md](../games/wizball_c64/plan.md) — 601 words |
| Layton / azran_legacy_3ds | [game.md](../games/layton/docs/azran_legacy_3ds/game.md) — 2,446 words | [plan.md](../games/layton/docs/azran_legacy_3ds/plan.md) — 671 words |
| Layton / curious_village_ds | [game.md](../games/layton/docs/curious_village_ds/game.md) — 4,872 words | [plan.md](../games/layton/docs/curious_village_ds/plan.md) — 791 words |
| Layton / lost_future_ds | [game.md](../games/layton/docs/lost_future_ds/game.md) — 3,555 words | [plan.md](../games/layton/docs/lost_future_ds/plan.md) — 658 words |
| Layton / miracle_mask_3ds | [game.md](../games/layton/docs/miracle_mask_3ds/game.md) — 3,877 words | [plan.md](../games/layton/docs/miracle_mask_3ds/plan.md) — 806 words |
| Layton / pandoras_box_ds | [game.md](../games/layton/docs/pandoras_box_ds/game.md) — 4,862 words | [plan.md](../games/layton/docs/pandoras_box_ds/plan.md) — 778 words |
| Layton / spectres_call_ds | [game.md](../games/layton/docs/spectres_call_ds/game.md) — 4,190 words | [plan.md](../games/layton/docs/spectres_call_ds/plan.md) — 770 words |
| Sonic collection | [game.md](../games/sonic/docs/game.md) — 7,718 words | [plan.md](../games/sonic/docs/plan.md) — 2,482 words |

## Current status

All requested documentation pairs are present. This is a presence/content check,
not independent validation of every reconstruction claim. The shared Sonic docs
live under `games/sonic/docs/`; individual Layton docs remain under their game
subdirectories in `games/layton/docs/`.

The shared-filter integration is complete. The first Reprise commit includes
all documentation pairs; this audit does not independently certify gameplay.

New-game onboarding: [ADDING_A_GAME.md](../ADDING_A_GAME.md).
