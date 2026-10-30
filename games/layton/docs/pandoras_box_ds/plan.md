# Pandora's Box (Nintendo DS): plan

What remains for a complete re-implementation of the second adventure
(`YLTD`), and the known bugs and differences. How the game works:
[game.md](game.md). The living gap register with evidence:
`.analysis/RECOVERY-GAPS.md` (gap ids G.. below).

## Where it stands (2026-10-29)

The game is complete, from the opening to the ending and into the post-game:

- the story, from the recovered router and state;
- all 187 puzzles;
- the herb tea, camera repair, photo hunt and hamster;
- the bag with its pages;
- the shack and the staff roll;
- the three save slots in the cartridge layout;
- the bonus section;
- movies, voices, music and sound effects.

Conveniences: music player, theatre, puzzle mode, minigame free play,
skipping, checkpoints and touch-area help.

## Remaining tasks

1. **Place 63, Granny Riddleton's hut** (done 2026-10-30, ov7
   `020F3050..020F3C88`): reached by ordinary exits from places 17, 22 and 62.
   Open in it: the bottles' fade during a turn uses the 3D blend
   (**hypothesis**; they are 2D sprites); the exclamation over a tapped bottle
   is not drawn (as for all puzzle spots, item 3); the turn button and smoke
   were not compared with the ROM (the reference run had six bottles).
2. **First-touch overlay** (command 0x92, ov7 `020F0218..020F0268`, G02):
   recover the flag's lifetime, the sprite (`map/%s/firsttouch.spr`), its
   dismissal and input gating.
3. **Room widgets** (G03/G42): map icons, the puzzle counter, place label
   position, the exclamation mark, hint-coin flight and sparkle (`020F1EF8`),
   the move-mode blink, and the tea request that replaces a talk event
   (`+0x434`, `ht_event`).
4. **Background blend of command 0x37** (G08): accepted but has no effect.
5. **Windows** (G41): objective, prize, item, reward, photo piece, torn photo,
   feature, companion, hamster name and save prompt use ROM text and icons with
   a native layout, and their fades are waits. Recover the window objects
   (`prize_window2`, text layer `02072F18`).
6. **Event lifecycle** (G07): event entry, chapter cards and narration set-up
   use native timing. Narration modes 2/3 are not compared (G47).
7. **Dialogue** (G09): one dialogue field is read but unused; mouth and
   bubble timing are not compared.
8. **Puzzle scene lifecycle** (G10, G27): intro, tutorial, hint, retry,
   give-up and solved animations are only partly recovered (some sprites show
   only their first frame); the solved explanation lacks its small header.
9. **Shack memo** (0x76 notice screen, G44): native layout.
10. **Wi-Fi** (G57): the downloaded-puzzle list (scene 22), download (39) and
    settings (38) show a native notice.
11. **Bonus pages not captured from the ROM** (G57): art, characters, music,
    voices, movies, challenges and the challenge notice. The character page's
    blend is not modelled. Unlock code 2 was not entered on the ROM. The quiet
    save's timing is not compared.
12. **Soft reset**: the bag's reset prompt restarts the native game instead of
    `0201FE14` (G48).
13. **Sound call sites** (G14b): menus and minigames, the hint-coin sparkle,
    events outside the recorded route, the narration page duck and the
    movie's pause and resume.
14. **Movies** (G16): pacing (timer, late-frame skip `0205A330`,
    decode-ahead, the five closing frames) and A/V sync are native.
15. **Random numbers** (G12, G45): synchronise the remaining consumers
    (voices, background shake, pancake entrances) with the original's single
    generator and its seed.

## Known bugs and differences

- **Hut prompt**: the German "Rätsel Nr. %.3d lösen?" is given a pointer to
  the digit string, so the console shows a stack address (41825528 in the
  reference run); the port shows the puzzle's number, as the shack menu does.
- **Hut bottles**: their order and colours come from the port's random
  numbers (G12), so they differ from a console's.

- **3D renderer**: a clean-room rewrite from hardware documentation, not
  pixel-identical. Edge pixels and blending can differ: hamster message
  window edges, translucent polygons under brightness (G50), turned pieces at
  intermediate angles (G25).
- **Layer latency** (G35): the puzzle scene presents its layers together;
  mid-stroke trace frames differ by the newest ink segment.
- **Drawing feedback** (G23): eight unexplained pixels in invalid feedback.
- **Hint button phase** (G10): its animation differs by 530 pixels in some
  captures.
- **Peg solitaire** (G33): eight drag frames differ by input and presentation
  phase.
- **Load lag** (G52, G54): the DS skips frames while reading the card (photo
  album, staff roll, bag pages). The port has no load lag, so audio-gated
  waits end a few frames apart.
- **Play time** (G49): counted in native frames.
- **Native checkpoints** (G13): a checkpoint written at an event start resumes
  there, not in the room before it.
- **Lines** (G36): the original's uninitialised snap target is treated as "no
  target".
- **Pancakes** (G27): the entrance bounces use the port's random numbers, so
  bounce counts can differ by one or two.
- **Microphone** (G26): holding B stands in for blowing.
- **Passcodes**: computed from a per-installation stand-in for the console's
  MAC address.
- **Title after the ending** (G55): not compared.
- **Not compared yet**: retry title cards and special puzzle cards (G14c);
  hamster level 6 and slim mode (G53).
- **Puzzle mode**: unknown special catalogue numbers show `W??` (G20).
  Puzzle mode keeps its own progress (G21, intentional).

## Checks to run after changes

- `.analysis/tools/golden.py run PACK OUT` and `compare` against the baseline.
- `.analysis/tools/validate_story_state.py`: 0 mismatches.
- `run_engine_regression.py` and `run_answer_checks.py` after puzzle changes.
- `run_mobiclip_validation.py` after decoder changes.
- `ctest`; `build.sh` runs the story and first-puzzle self-test when the pack
  exists.

## In Reprise (2026-10-29)

The game runs inside Reprise (catalog id `pandoras_box_ds`); the integration is
described in [../reprise.md](../reprise.md).

Connected: import (YLTD NDS, streamed), Start (the title), Continue and quick
save/load (the last checkpoint: a room, or an event's or puzzle's start - it
resumes at that start), the slots in `backup.sav`, the "Hold to blow" button
for puzzle 203 (or fire held in a puzzle), the VHS filter per screen,
touch-area help, background previews, takeover of Picarat's pack and
`backup.sav`. Picarat's checkpoint of 9 October is format 1, which the game
itself refuses (it opens the title); such checkpoints are not taken over.

Not offered in Reprise (Picarat shell features, by decision): music player,
movie theatre, puzzle mode, minigame free play, the hub's "Continue story".
Movies, music, puzzles and minigames play as part of normal play.

Remaining for Reprise:

- typed name entry (the touch keyboard works);
- an accelerometer for the 3DS games on phones (Picarat had one);
- the screen filter's CPU fallback path is untested on a renderer without
  GPU shaders.

Validation:

- Validated on macOS (2026-10-29): `ctest -R layton_module` with
  `LAYTON_PACKS` (the story starts from the title with picture and sound);
  the real window driven by `reprise --events` scripts. Android: the APK
  builds; only Curious Village was run on an emulator (start, touch,
  rotation). No physical device was used.
- Quick save round trip checked with a format-2 checkpoint written by
  `picarat_story_explorer` (continues into event 20020's start).
- Not run on Android.
