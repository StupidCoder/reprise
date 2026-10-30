# Spy Mouse (iOS) — plan

What is left before DoubleOCheese is a fully playable re-implementation of *Spy Mouse*
(iOS, version 1.1.1), and every known bug. Research findings are in [game.md](game.md); the
evidence it cites lives in the local research archive (`.analysis`, not in the repository).

State on 2026-10-30: worlds 1–4 (with both of world 4's secret levels) play from the world map
through each boss fight, every level won in a reference run of the original. The port follows version **1.1.1**; the reference runs were re-recorded
on 1.1.1 and most of them verify again frame for frame (section 1 lists what does not yet).
Music decodes portably (own HE-AAC decoder), but it has only been built on macOS.

## 1. Finish the move to version 1.1.1

Done: program tables, sound/particle/text numbering, GUI format 26, struct offsets for the
verifier and the scripts, 1.1.1 reference runs (`refruns111/`, `evidence/touchhle111/`),
results screen (cheese badge), dossiers, carry popups opened by `SceneGame::update`, boss chase
and fights of worlds 1 and 2, checkpoint lines and safe-hole checkpoints, the new rollback
details (whole-number radius, route trims, view), the cats' 1.1.1 init/postInit (boss cat,
Fluffy, henchman pictures), the droid's render-driven body frame, no automatic Kiska offer.

- [x] 62 runs of worlds 2–4 were recorded with the profile's world poked at 1.0.5's offset
      (+0x18 instead of +0x1dc); re-recorded 2026-10-29 (`port_run.py` maps it now; four were
      first recorded in the wrong slot and redone).
- [x] Checkpoint reloads (2026-10-29): the cats' init runs again over the kept objects in all
      modes (a back-and-forth route walks forward again, `l3h2_win`/`l3h2_caught`); the Spitter
      keeps the whole-number radius (`lbf2_caught`); the mouse's hole tag comes back but not the
      hole (`l24_win`: out at once, no grow).
- [x] `l45_win` 4174: `Object::onFloorChanged` removes twine (state 0x22) and eaten catnip left
      on other floors.
- [x] `l17_win` 3020: one door timer (Player +0x364), reset when the floor shows.
- [x] `l2s_win` 9481: a safe hole keeps the trail's items when the mouse is touched out.
- [ ] `l37_win` 6760: the original snaps onto the safe hole (obj 17, type 0x16a) one tick before
      the port (port's path end 30.07 from it, reach + radius 28.10). The trace has no path
      points to compare the end with; `snapPath` @0x101070 reads the last point like the port.
- [ ] `l110_win` 3338: the 1.0.5 plan is caught in 1.1.1 and the drag goes on across the retry
      (finger/drawing state across `reloadFromSnapshot` not modelled); re-plan the run.
- [ ] `l26_win` 5650: the 1.0.5 script's release at (20, 45) lands on 1.1.1's power-up button;
      the original opens the power-up bar with Digger's introduction (the game pauses from
      5651). Needs section 1a (or a moved release to keep the old plan).
- [ ] `l11_hud`: the HUD watches (`hudA`/`hudB`) still use 1.0.5 offsets, and 1.1.1's HUD
      items sit elsewhere (x 30 for the power-up button); re-derive with section 1a's HUD.
- [ ] World-map runs (`map_walk`, `w2_enter`, `w2_back2`) compare nothing since the move
      (found 2026-10-29): `mapcmp.py` still opens 1.0.5's app and evidence, and the 1.1.1
      recording of `map_walk` never reaches the map (its screenshots show level 1-1). In 1.1.1
      the map's Player is at SceneMenuMap +0x11140 (1.0.5 +0x11124; `SceneMenuMap::update`
      @0x15c0c4 passes it to `Player::update`) and the update first reads +0x158e0 (1.0.5's
      state was +0x152a0; that this is the state is a hypothesis). Re-record the map runs on
      1.1.1 (how its front end reaches the map), then point `mapcmp.py` at them.
- [ ] Sound on 1.1.1: `soundall.sh` still compares the 1.0.5 runs (`soundcmp.py` takes
      `REF_VER=111` since 2026-10-30). Spot checks of 1.1.1 runs: every slot run's level start
      lacks the map's stops and a `ui_wobble` at +9 frames; `l4s_win` lacks 1.1.1's new
      `cat_purr_end`, `l48_win` the twine's `cat_purr_loop`/`cat_string` at 3158. Run all
      1.1.1 runs and fix what differs.
- [ ] Check the 303 of 452 cited functions that differ after address masking, beyond struct
      offsets (`.analysis/tools/v111/fdiff.py`).
- [ ] The results screen's "Please wait..." with a store tip after a level (touchHLE never
      reached the map); find what the offline game does.
- [ ] Results: the best badge's rotated caption (string 0x13b, `renderStrRotWrap`) is not drawn.
- [ ] Dossier page: the scroll frame's offset from the paper and the "Level %i" numbering of
      "First appearance" are hypotheses; the first-find note on leaving the page is not shown.
- [ ] Type-0x11d chase branch (Player +0x42c); objects 0x1b8/0x1b9/0x109 (no map uses them);
      `Player::kill` no longer plays 0x1d; patrol differences of cats 0x35/0x37 unchecked.
- [ ] Convert code comments and research notes from 1.0.5 addresses to 1.1.1 (by symbol).
- [x] Research archive and Ghidra projects moved into this game's folder (`.analysis`,
      `ghidra`, both ignored by git; 2026-10-29). The old DoubleOCheese folder is no longer used.

## 1a. Power-ups and the store (1.1.1)

1.1.1 has six power-ups (banana, bomb, roller skates, hamster ball, Kiska, X-ray specs), bought
with cheese in the store (`menu_store`) and used from an in-level bar: the power-up button
(`hud/hud_powerup.png`, top left; the score pills sit to its right), a sliding frame
(`popup_powerup.png`, SceneGame +0x57bf4), the info card, "Digger" the shopkeeper with the
power-up tutorial (`setPowerupTutorialState` @0xf73ec, texts 0x6a8–0x6b1, layout
`powerups_intro_tutorial`, reminder `digger_store_reminder`). The in-level HUD lacks the button
and the shifted pills today. Kiska is now a power-up: the level skip opens only from the bar.

- [ ] HUD: power-up button and the 1.1.1 pill positions; the bar, its buttons and info card.
- [ ] Each power-up's effect in play (`ObjControllerPowerup`, `Player`), from the code.
- [ ] The store scene, prices, the cheese balance; buying cheese with money is online and not
      ported (show what the offline game shows).
- [ ] The power-up tutorial (from world 1's boss chase on, profile +9) and the store reminder.

## 1b. Quick save without re-running

- [x] (2026-10-29) Quick saves keep the scene's whole state (`src/game/state_io.hpp`): each
      class lists its changing fields once in `io()`, used for writing and reading; loading
      builds the scene from the files as at its start and reads the state over it (about 26–50
      KB, under 10 ms). `tools/check_quick_save.py` checks that every field is listed or named
      as not saved. Verified with a save and load at every frame against an uninterrupted twin
      (byte-equal states): all level reference runs (`DOC_QUICKSAVE=1 doc-verify`), the map
      runs (`DOC_QUICKSAVE=1 doubleocheese ... --map`), and the whole flow from a fresh profile
      through 1-1, its results and the map (`--flow-check`). Version 1 saves (scene start and
      touches) still load by running the scene again. Damaged saves (30 cut short, 30 with a
      flipped bit, under UBSan) are refused or load and run without faults.
- [x] Moving a `WorldMap` left its particles holding the old map's random generator (a dangling
      reference since the map was first made). Maps and sessions are now made in place and
      cannot be moved or copied.

## 2. Remaining content

- [x] **4-S2** (map 730, uid 4451; 2026-10-30): won in `l4h1_win`, identical to the original
      (7610 frames, the results, 284 sound events). Floor 1 is won with the catnip: the black
      cat chases, takes it from the trail and eats it while the mouse fetches the last cheese.
- [ ] **World 5** (11 levels plus H levels, chase, fight) — not started.
- [ ] **World 6** (11 levels plus H levels, chase, fight) — not started. The code mentions
      Fluffy's henchman cry (0x8e) in world 6 only, lava trapdoors, `flamedeath` sequences and
      boss type 0x132.
- [ ] **World 7** (new in 1.1.1) — not started, not surveyed.
- [ ] 1.1.1's world carousel (Prologue, Suburbs, Inner City, Ninja Village, Ghost Town,
      Volcano): map its entries to the level table's worlds; survey the Prologue.
- [ ] Cat kinds not yet reconstructed: the orange thief cat (0x33: steals cheese from the trail),
      ghost (cry 0x14), lava (0x15), 0x37, 0x39–0x3b except the henchman's ported states, 0x11d.
- [ ] How the blind cat (0x36) finds the mouse.
- [ ] Route end mode 3 (linking another route).
- [ ] World 2 leftovers: hiding in the couch (`touchPlayer`, `getTouched`) and the "furniture"
      goal; the "teacup" and "balloon" goals; balloons popped and teacups knocked over by cats
      going to a plaything; the pickups' second `touchPlayer` branch (floor's cats to state 12).
- [ ] Floor transport type 30 in 2-9 and type 35 (floor transport, not the floor door).
- [ ] Projectile kinds other than straight (timed 3, lobbed 4, homing 5, the player's 0),
      pictures 1, 2, 7, 9 and the poison dart (8, not drawn), bouncing off walls (0x6d), the user
      trigger, a projectile caught by and stuck to the mouse, objects 0x10a, 0x4b, 0x159..0x15c
      and flagged pickups being hit, and turrets of other tags (aiming at the mouse).
- [ ] Room door type 33 (`loadNextRoomDoorSprite`), going up a floor through a room door, the
      second snapshot handler (`g_pSnapshotHandlerFloor`).
- [ ] Wall spawners 269/270 and the bomb dropper's other facings (271..273).
- [ ] Bank holes (0x168..0x16b) beyond the munch-hole case; water, platforms and cannons (player
      states not yet seen).
- [ ] Goal kinds 10–27 that are not counted yet (darkness, …); "at most" goals for fish and
      string (the port's count of twine/catnip pickups is a hypothesis); the comparison of
      "shuriken_cheese" (hypothesis: at least).
- [ ] Level-start popup kind 3 exception (`SceneGame+0x13828 == 3`) and mode +0x13828 == 1 in
      `changeFloor`.
- [ ] Mouse states and drawing: carry sprites for held objects (type 0xfa), state 10/3 branch,
      wall push, `surprised1`/landing triggers, tints (dark areas, hits, poison, flicker),
      swimming, wet footprints (particle 0x31) on water levels.
- [ ] Iris kinds 1 and 4 (`pin_star`, `pin_tonsills`), the letterbox and the cut-scene picture
      path of `PinholeManager::render`.

## 3. Front end and meta-game

- [ ] Title screen and the original's first-boot path (logos, "touch to continue", the 1.1.1
      world carousel). The port goes from its launcher straight to the map.
- [ ] Pause menu's settings and help scenes (`options`, `menu_help`); volume settings.
- [ ] Kiska as a power-up (1.1.1): the level skip from the bar (`SceneGame::guiEvent`), its
      FRAME_SKIP states 1..3; the 1.0.5 automatic offer after the intro is gone.
- [ ] 1.1.1's store, dossiers and cheese wedges: find what is reachable offline and what the
      game does without the online layer.
- [ ] World map extras: birds/bats (`SceneMenuMapFluff`), cows, the backpack and its "New!"
      badge and menu, options and help buttons, the billboard's idle title (string 0x16d every
      10 s), the boss intro on the map (state 7), hidden roads of secret levels
      (`initHiddenPaths`), the statue that replaces a beaten boss's house, the arriving mouse's
      fade near the previous-world node, the Play bubble's shrink on hide.
- [ ] Map random stream: 5 `randWithinRange` draws at load and the ambience draws, so clouds and
      sparkles could match the original.
- [ ] The original's save format (`FMUserData`/`FMCryptFile`). The port keeps its own
      `progress.txt`; import of an original save would need it.
- [ ] `attemptsBeforeShowingTutorial`: the original's count across visits (LevelState +0x30).
- [ ] Achievements (`achievements.cfg`) — offline display only, if the original has one.
- [ ] Intro/outro videos (`intro.mp4`/outro, H.264): no portable decoder yet.
- [ ] Story cutscenes named by the level table (`world01_chase`, …) are absent from the IPA;
      confirm nothing else should play.

## 4. Platforms

- [x] VHS look through Reprise's shared screen filter stage (option "VHS look", 2026-10-29).

- [ ] Linux build (the code is portable C++ and SDL 3; never built).
- [ ] Android build inside Reprise's APK, including the music decoder and the document picker
      import; check on a device (touch drawing, 120 Hz displays use whole 17 ms ticks).
- [ ] Listen to the decoded music on a device (checked so far only as samples against
      AudioToolbox).
- [ ] FreeType: the game embeds 2.4.4, the port links 2.14.3; remaining 1-px text differences.

## 5. Known bugs and differences

Open differences between the port and the original, from the reference runs:

- [ ] The first ~6 frames after a retry from the results: the original's mouse spot starts darker
      and its mouse shows another sprite (`l11_retry` 2306–2312). Not explained.
- [ ] The last "zZz" of a cat's pause stays a few frames longer than in the original
      (`l12_pause` 2900).
- [ ] Sound, `l17_win` 2619: the port's 8-entry playing list is full one entry earlier and gives
      up an `eat_food` entry where the original stops two others at 2635/2636.
- [ ] Sound, `lb4_win` 6701 and 10301: the port plays the wall hole's exit sound (0x31), the
      original does not (Player +0x164 points at another object there; its setter is not traced).
- [ ] H2 caught (`l3h2_caught`): the cheese dropped at the catch takes the next trail item's x in
      the original; the code that moves it is not found (field skipped in the verifier).
- [ ] World card: its first two frames differ slightly (the card's bounce, mean 3.4).
- [ ] World pad iris: drawn larger than the original's in its first frames.
- [ ] Kiska offer and results labels: drawn slightly differently while a bounce frame is squashed
      (shot 2360 of `l28_later`, 5.2).
- [ ] Trapdoor arrow: fade-out over the last second is a hypothesis; its size differs by a few
      pixels.
- [ ] 4-8's first recording: a drag at 2120, under the level intro, never moved the original's
      mouse, while earlier levels take drags under their intro. The port passes the touch on
      (gating it broke 26 runs). Cause unknown.
- [ ] Catnip being eaten (state 0x21 fade) is not drawn.
- [ ] Munch holes are drawn only approximately (low pass, frame, pulse).
- [ ] Maps 710, 751, 761 and 785 parse short in the map reader (1.0.5; check on 1.1.1).

Deliberate differences (not bugs): touchHLE ends sounds early relative to game time (ninja cry
replayed after 1.43 s instead of 1.74 s); the port keeps the files' real lengths. In
reference runs played through a world 1 slot, the world card reads "World 1 Completed" in the
original. The port keeps whole 17 ms ticks on high-refresh displays.

## 6. Documentation

- [ ] Bring the curated research notes (`.analysis/docs/*.md`) into the repository next to
      game.md, keeping their provenance, so a fresh clone has them (Reprise's documentation
      audit recommends this). Evidence (traces, captures, unpacked IPAs) stays outside.
- [ ] Name the files cited by game.md's evidence paths relative to the research archive.
