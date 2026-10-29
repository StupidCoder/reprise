# Spy Mouse (iOS) — plan

What is left before DoubleOCheese is a fully playable re-implementation of *Spy Mouse*
(iOS, version 1.1.1), and every known bug. Research findings are in [game.md](game.md); the
evidence it cites lives in the local research archive (`.analysis`, not in the repository).

State on 2026-10-29: worlds 1–3 and world 4 except the secret level 4-S2 play from the world map
through each boss fight, frame-identical to recorded runs of the original. That verification was
done on version **1.0.5**. The port now reads **1.1.1**, whose changed rules have not been
re-verified yet. Music decodes portably (own HE-AAC decoder), but it has only been built on macOS.

## 1. Finish the move to version 1.1.1

The port reads 1.1.1's program tables, sound and particle numbering, text ids and GUI format,
builds, and plays 1-1. Everything else was verified on 1.0.5 only.

- [ ] Finish the Ghidra analysis of the 1.1.1 slice (project `ghidra/SpyMouse111`; the import was
      still running and holding its lock at the last check).
- [ ] Re-derive the struct offsets that the `doc-verify` watches and the touchHLE scripts use
      (Player fields after +0x130 moved by about +0x180, Object by +8 around +0x114, …) and
      re-key the scripts' RAM addresses (globals table in game.md).
- [ ] Re-record the reference runs on 1.1.1 (same input scripts where the maps are unchanged,
      inputs shifted +178 frames: 1.1.1 boots 1-1 at frame 1185, 1.0.5 at 1007) and compare. Each
      mismatch leads to a changed function, which is re-derived from 1.1.1. Delete a run's 1.0.5
      evidence once its 1.1.1 run verifies (disk budget).
- [ ] Check the 303 of 452 cited functions that differ after address masking, beyond struct
      offsets (`.analysis/tools/v111/fdiff.py`).
- [ ] Find out which 54 maps changed in content and re-plan the affected winning runs.
- [ ] Particles: which `crumb_eat` entry (0x15 or the new 0x16) crumbs use; the special (text and
      score) kinds above the table; `Particles::addParticle/update/render` changed logic.
- [ ] The results screen's "Please wait..." with a store tip after a level: in touchHLE the
      original did not reach the map. Find out what it waits for (probably the store/online
      layer) and what the offline game does.
- [ ] Results layout items new in 1.1.1 (NEW_WORLD_UNLOCKED_BOUNCE, FRAME_CHEESE_COUNT,
      IMG_CHEESE_FOUND) are hidden; confirm when the original shows them.
- [ ] Convert code comments and research notes from 1.0.5 addresses to 1.1.1 (by symbol).
- [ ] Copy the `/tmp`-built migration scripts into `.analysis/tools/v111/` if any are missing.

## 2. Remaining content

- [ ] **4-S2** (map 730, uid 4451). Floor 0 is solved in the port
      (`.analysis/tools/plan/4s2/s2y.plan`). Floor 1 has no route under the 1.0.5 cat tracks
      (time-expanded search, 102,831 states); retry on 1.1.1's map 730 or with another
      approach. Then record `l4h1_win`. Type 19 on floors 1 and 2 is not reconstructed.
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
      second snapshot handler (`g_pSnapshotHandlerFloor`), the checkpoint a safe hole takes.
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
- [ ] Kiska bought (FRAME_SKIP, states 1..3 of `SceneLevelSkip::update`). The offer works; the
      purchase does not exist offline. Decide the offline behaviour from the code, do not invent.
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
- [ ] `Object::updatePositions` is applied to the clockwork mouse; whether resting or carried
      cheese is pushed out of walls the same way (`ObjControllerPickups::update` @0x1a0c04, 1.0.5)
      is not checked.

Deliberate differences (not bugs): touchHLE ends sounds early relative to game time (ninja cry
replayed after 1.43 s instead of 1.74 s); the port keeps the files' real lengths. In
reference runs played through a world 1 slot, the world card reads "World 1 Completed" in the
original. The port keeps whole 17 ms ticks on high-refresh displays.

## 6. Documentation

- [ ] Bring the curated research notes (`.analysis/docs/*.md`) into the repository next to
      game.md, keeping their provenance, so a fresh clone has them (Reprise's documentation
      audit recommends this). Evidence (traces, captures, unpacked IPAs) stays outside.
- [ ] Name the files cited by game.md's evidence paths relative to the research archive.
