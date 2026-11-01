# Campaign adapter and checkpoints — 2026-11-01

This extends opening integration commit `493530f` and the pinned source
reconstruction `0816c17a53afe613a0fea728872645c727120699`. It is an integration
of that reconstruction, not new evidence that the entire cartridge is recovered.
The supported ROM and its SHA-256 remain those in game.md.

## Implemented scope

The adapter owns opening → book → course transitions, the source course pause,
clear and miss controllers, and return/restart bookkeeping. It uses imported
StageList entries, layouts, texts, animations and audio. Only PullUpStage,
RevolverRuinsStage, TouchPuzzleMiniStage and FreshRuinsStage may start; choosing
another course produces an explicit reconstruction-limit message. Options →
Cheats also offers the book and these four courses directly. This selection
marks the opening cleared but does not fabricate course clears.

The book and original course pause use the original stacked screens, centred in
the host frame. Gameplay uses the larger single view, optionally widened.
Filters receive separate regions for the two screens. Start opens the original
course menu; Escape remains the Reprise menu. The book uses native pointer/touch
input without the gameplay touch deck. Its 30 Hz input latches the host's 60 Hz
button presses and queues pointer events so short presses are not discarded.

Generated stages, layouts, audio and shaders live under the Library's
`cache/captain_toad_3ds/eu-00040000001cb200-v1/`. Export functions now require both
an immutable import root and an explicit writable cache root. No source ROM is
needed after import. Campaign values live in the module/checkpoint and Reprise
save generations; the standalone progress.json writer is not called.

## Checkpoint contract

Schema 1 stores a scene name and owned campaign values: clears, gems, played and
revealed pages, lives, season, page and return flags. Continue restarts that scene
at its entry. In-course position, timers, effects, camera and audio are rebuilt;
this is deliberately not an arbitrary-moment save. The opening checkpoint is its
beginning. A book checkpoint is its entry, before page navigation/reveal playback.
Saving retains progress as of that checkpoint, not later unfinished course gains.

The module validates schema, release, supported scene, bounds and names before
replacing state. Reprise additionally checks the imported-content fingerprint and
checksum, and publishes the state/preview pair as a durable generation. Display
and control preferences survive loading. Save previews render the restart point
in an independent temporary module and restore the live module's GPU selection.

## Reproduction and observed results

`captain_toad_flow_probe IMPORT OUT SCENE` runs against a real imported cartridge;
SCENE is Book or one of the four stage identifiers above. Each smoke run executes
720 input/audio ticks, samples rendering, opens the course pause, captures a
checkpoint preview, rejects corrupt state, restores the checkpoint and checks
that presentation preferences survive. These passed for the book and four
courses. Audio was finite, bounded and nonzero. The preview did not change the
running picture. This does not exercise every object or solve each course.

The first two course restart captures have small differences near effects:
RevolverRuinsStage mean RGB absolute difference 0.01442101/255, 788 changed pixels
out of 384,000. TouchPuzzleMiniStage and FreshRuinsStage were exact in the recorded
runs. A first-course save/Continue comparison in separate processes differed by
0.01787153/255 across 422 pixels. The cause of these localized visual differences
is unresolved; no pixel-exact restoration claim is made. The probe reports the
difference and fails above mean 0.05/255 or 0.5% changed pixels. Owned checkpoint
values round-trip exactly. This tolerance is a smoke-test limit, not a fidelity
specification or evidence about the original game.

After a stage probe creates OUT/checkpoint.bin, run the probe with `durable-save`
and then `durable-continue` in separate processes. Both passed through the actual
Reprise save wrapper. The save test also blocks the temporary save destination,
checks that failure is reported and the preceding generation remains loadable,
then corrupts the latest generation and checks that a failed load preserves the
running scene. The test restores the original file after the corruption check.
Use a disposable test Library, never the player's save directory, for this probe.

The asset-free `captain_toad_checkpoint` CTest covers round trips, every truncation
boundary, trailing bytes, unsupported schema/release/course and invalid ranges.
All 4,210 imported files still match the pre-play native import; the Library's
release.txt is the only additional file. No generated cache or progress file
appeared inside the import. Generated cache size for the four courses and book
was 276 MiB in this run.

A final 1,800-tick `BookFlow` run reached PullUpStage through the actual book
and title-card transition (audio energy 15082.97270). A scripted real Reprise
shelf → details → Continue run entered that saved course, opened the host pause
menu, resumed and exited; both captured screens were visually inspected.
The final public course-selection option reached FreshRuinsStage and passed its
checkpoint checks. BookFlow also passed on Vulkan (MoltenVK on this Mac).
Capture and binary hashes, plus cross-backend differences, are in
[campaign-proof.json](campaign-proof.json).

## Builds and regression checks

`./build.sh` packaged the macOS app. `ctest --test-dir build --output-on-failure`
passed all 15 checks, including the new checkpoint test and the concurrently
landed host touch-coordinate test. `./build-android.sh --jobs 6` packaged the
ARM64 APK (100.0 MiB), with v2/v3 signing verification. After the final small
course-selection/menu-input correction the desktop app and campaign probe were
rebuilt and Android was packaged again; the graphical checks cover that change.

## Remaining validation and inherited limits

End-to-end opening completion, full course clear/miss/restart playthroughs and
all touch-specific objects still need fixed-input regression coverage. The
adapter wires the inherited controllers, but smoke tests are not proof of those
whole paths. The source's miss timing and gem-retention rule are still marked
hypotheses in its code; full game-over behavior remains unreconstructed.

Android build/package validation does not establish physical-device behavior.
No Android device was attached. Real touchscreen/gamepad, suspend/resume and GPU
reset checks remain pending. The 178 older research-export differences remain
unresolved; the original source and installed data are retained. Automatic
adoption of Diorama progress, mod override validation and exact-moment saves are
not implemented. No OpenXR mode is exposed or linked.
