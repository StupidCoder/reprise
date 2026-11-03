# Initial core handoff validation — 2026-11-02

This verifies a partial source integration, not a playable Reprise module or
original-ROM equivalence. Source: clean Beacon commit
`89f52f6c105a2a57a56a9786cb27ca5768d5f4d1`. Source files and assets were preserved.

## Reproduction

From Reprise:

```sh
python3 games/astro_bot_ps4/tools/verify_handoff.py /path/to/Beacon
./build.sh
ctest --test-dir build --output-on-failure
cmake --build build --target beacon_hero_probe beacon_model_check beacon_anim_check beacon_sound_check beacon_physics_check
build/games/astro_bot_ps4/beacon_hero_probe /path/to/Beacon/.tools/game/CUSA12392 world0_0_new_intro_1
./build-android.sh
```

For a source-side comparison, build Beacon's `game` and `audio` targets, then
compile `src/tools/HeroProbe.cpp` without `REPRISE_BEACON_CORE`, with Beacon's
`src` include directory and its `libgame.a`, `libphysics.a`, `libanim.a`,
`libdata.a`. Run both probes against the same folder. Beacon's existing
`beacon-level hero GAME_DIR LEVEL` provides the same sequence. The probe covers
522 fixed updates: settling, tap/held jumps, walking, release, jump and hover.
It prints state transition frames and rounded positions/apices; equality of this
output is narrower than full internal-state or frame-exact equivalence.

The optional model/animation/sound tools retain source behavior, including
returning zero for some reported decode failures. Inspect their output; process
exit success alone is not a pass. Do not use these research tools as importers.

## Scope and remaining validation

Core check: centered/directional pad mapping, authored XML/entity decoding,
malformed XML rejection and UUID link resolution including missing targets and
all-target semantics. No game assets are needed by CTest.

No UI playthrough, game import, frame/render comparison, audio-device output,
movie playback, save restoration or physical Android-device test is claimed.
The new static core targets compile on Android but are not wired into libmain's
gameplay entry point. Source native reconstruction gaps remain unchanged.

## Results

- Provenance verifier: 160 original hashes match. All whole-file copies match
  after reversing namespace isolation; the extracted hero probe is explicitly
  identified in the manifest. Beacon's source working tree remains clean.
- macOS: `./build.sh` passed and packaged `build/Reprise.app`. All 16 CTest
  checks passed, including `beacon_core`. Verification tools built successfully.
- Android: `./build-android.sh` passed, including compilation of all six core
  libraries (explicit prerequisites of the app target), APK alignment checks and v2/v3 signature verification. APK: 103.6 MiB.
  Both packages were checked for accidental Beacon game payloads; none found.
- Movement: the Reprise probe, the same probe linked against Beacon's libraries,
  and Beacon's existing `beacon-level hero` produce identical 522-update summaries
  on `world0_0_new_intro_1`. Source core build was current (Ninja: no work to do).
- Models: identical source/Reprise output, 5,478 meshes and 1,940 meshless files;
  no reported model parsing failures.
- Animation: identical source/Reprise output, 1,465 skeletons and 11,787 clips;
  **12 unsupported bit-packed clips remain**, plus 2 paired joint-count mismatches
  and 6,020 clips without a skeleton matched by filename. These are inherited
  limitations, not successful coverage of those cases.
- Audio: `hero_sfx.sxd` waves 0 (ATRAC9 mono), 2 (HE-VAG mono) and 7 (HE-VAG stereo)
  decode to byte-identical WAVs in source and Reprise. Hashes retained; temporary
  decoded audio was deleted. This checks decoder preservation, not playback.

Small outputs, binary/source version identity and hashes are in `handoff/`.
No bulk traces or extracted game payloads were retained in Reprise. To repeat
asset checks, run source and copied model/animation tools with `check GAME_DIR`;
for audio, run each sound tool with `wav GAME_DIR/data/multi_platformer/sound/hero_sfx.sxd INDEX OUT.wav`
and compare outputs for indices 0, 2 and 7. Delete decoded WAVs after comparison.
