# Reconstruction and source handoff

The engine originates in the local Diorama project at the revision and per-file source hashes in source-handoff.json. Only clean reconstructed C++ and importer code are carried over; user images, imported assets, builds, private tools and sealed evidence are excluded. Retain the original MIT notice in LICENSE.

## Supported input

The source importer supports the unencrypted European CCI with program ID `00040000001CB200`; it checks the exact files it consumes against the digests in src/importer/Inputs.cpp. No decryption is implemented. Package schema is currently 5. The validated supplied image is 536,870,912 bytes; SHA-256 `e72f516e11c38be6fc2252e1e768247916f492740d3fef395dfcfe71e01eb4cb`. See [the integration report](docs/integration-validation.md) for the native-import equivalence check and its limits.

## Preserved contracts

The session consumes 60 Hz input frames and advances gameplay on alternating frames (30 Hz). Recovered arithmetic retains `-ffp-contract=off`. DSP mixing uses 268111856 / 8192 Hz. Gameplay, script/layout/animation data, shader generation, collision and audio remain game-owned. Reprise owns the window, input devices, timing, audio output, presentation and library. Persistent saves remain pending.

## Evidence and uncertainty

Source comments cite original routines and local evidence. The source README reports opening, course-book and first-four-course coverage; later courses are incomplete. The opening has now been checked through Reprise; the course-book and course reports have not yet been rerun there. The original local `.analysis` archive is retained externally, not copied or represented by a checkout-dependent symlink. Curated reproducible evidence references remain an integration deliverable.

## Randomness and enhancements

Preserve current generator state and draw order during the initial handoff. Audit simulation and cosmetic streams before changing either; use fixed seeds in regression checks. Wide view, interpolation, tap-to-walk and smooth camera are optional enhancements, not original rules. Mod override validation and save compatibility remain to implement.

## Reprise opening adapter (2026-11-01)

Native service: SDL GPU compiles the imported material dialect with pinned
static glslang 16.6.0 and SPIRV-Cross vulkan-sdk-1.4.328.1. Each stage's samplers
are remapped to SDL sets 0/2 and uniform data to 1/3. Metal receives converted
MSL; Vulkan receives SPIR-V. Bottom-up imported/render textures remain bottom-up
internally; clip Y is inverted for SDL's common coordinate convention, and one
GPU blit flips the final image into the host target. The host alone composites,
filters and presents it. Normal frames do not read pixels back to the CPU.

The module uses Session::advance(1/60) for input ticks and only updates the
presentation interpolation fraction when drawing. Audio retains a fractional
resampling position between callbacks at 268111856/8192 Hz. The game owns no
second audio device. Imports are immutable; disposable material caches are in
Reprise's cache/captain_toad_3ds/materials-v1 directory. No progress writes or
persistent saves are advertised for this opening milestone.

Controls: arrows/left stick move, Z/south acts, X/east runs, IJKL/right stick
turns the camera, Q/E or shoulders rotate; C/V expose the other face buttons.
Start opens Reprise's pause menu. Automatic or explicit touch controls provide
movement, camera, A/B and L/R controls. Touch preference and display options are
presentation settings. The book and later courses remain unavailable.

Validation: docs/opening-validation.md. These are integration checks against
the pinned source reconstruction, not new claims of whole-ROM equivalence.
