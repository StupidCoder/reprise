# Opening integration validation — 2026-11-01

The existing `captain_toad_3ds` shelf entry uses the embedded importer and opening
module. Source reconstruction commit: `0816c17a53afe613a0fea728872645c727120699`.
ROM/import identity remains the one recorded in game.md and import-equivalence.json.
The source project was not modified.

## Rendering

`captain_toad_scene_probe IMPORT OUT` loads `IMPORT/Opening` using the real
Reprise Host and renders at frames 0, 120, 240 and 360 with Session::advance(1/60).
Run normally (Metal) and with `SDL_GPU_DRIVER=vulkan` (MoltenVK on this Mac).
Metal and Vulkan RGB pixels were identical at the three nonzero checkpoints.

A matching harness linked to the untouched source's session/platform/gpu/importer
libraries used the same Session::advance clock and 400x240 offscreen target.
Mean absolute RGB differences (0–255) were respectively:

| Frame | R | G | B |
|---|---:|---:|---:|
|120|0|0|0|
|240|0.0294271|0.0287188|0.0201979|
|360|0.0167396|0.0151667|0.0130833|

The older source `--play` research harness advances its sky/animation clocks
differently from Session::advance; at frame 360 that produced larger sky
changes. It is not the renderer oracle for this comparison. The matching clock
harness and capture hashes are recorded in opening-render-proof.json.
These comparisons establish the sampled integration path, not whole-game fidelity.

## Player-facing module

`captain_toad_module_probe IMPORT OUT` exercises the actual GameModule:
1,200 fixed input/audio ticks, rightward movement after the intro, camera input,
48 kHz output with finite bounded nonzero samples, stable repeated raw captures,
wide view and interpolation, 30 repeated GPU presentations, GPU VHS, touch-layout
availability, destruction and restart on the same host device. Audio energy in
the recorded Metal run: 12817.96902. Screenshots were visually inspected.

The complete macOS build and all 13 CTest checks passed. Android ARM64 API 26
build/packaging passed with v2/v3 signature and 16 KB alignment checks. No Android
device was attached (`adb devices -l` returned an empty device list).
The real Reprise library importer installed the supplied CCI under
`games/captain_toad_3ds`; a scripted shelf → details → Play run showed the opening,
then the Reprise pause menu, resume and resize. Its first capture exposed final
frame alpha from additive effects leaking into SDL composition. The backend now
seals only the final alpha channel to 1 on the GPU before handing off the frame;
internal material alpha remains unchanged. The module probe checks opaque output.
The temporary library's automatic legacy copies were removed after the test;
original installed libraries and source projects were not modified. The opening ends at its title transition with an explicit message;
the book/campaign, quick saves and automatic adoption of Diorama's installed
progress are not implemented in this milestone.

## Remaining checks

Physical Android rendering/input/audio, suspend/resume and GPU device loss remain
untested. macOS Vulkan success and Android compilation do not establish physical
Android correctness. The source importer/exporter drift (178 earlier differences)
remains unresolved. No source retirement or campaign-completion claim is made.

The SDL backend rejects mixed constant-RGB/constant-alpha factors within RGB
blending rather than silently approximating them; that combination was not used
by the tested opening. Later course materials need their own validation.

## Final compositing regression

After sealing final alpha, module captures at ticks 600, 720, 1200 and the wide
view have alpha 255 everywhere. Their RGB bytes are unchanged from the earlier
module captures (mean absolute difference exactly zero for every channel).
A final real-shell run confirmed the star effect has no black rectangle;
play → pause → resume → resize → quit completed successfully. The supplied
4,210 imported files remained byte-identical after play. Final macOS packaging
and Android packaging both include glslang/SPIRV-Cross license notices and no
ROM/import payloads or OpenXR libraries.

The final module probe also passed on Vulkan/SPIR-V, including opaque output,
48 kHz audio, filtering, resize and restart. Its four final captures match the
Metal captures in RGBA bytes. See opening-render-proof.json for binary and
capture hashes. Physical Android remains untested.
