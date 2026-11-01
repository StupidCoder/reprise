# Initial Reprise integration validation — 2026-11-01

This is a foundation milestone, not a playable Reprise release. The existing
`captain_toad_3ds` card and artwork retain their identity. No OpenXR application,
headset dependency or standalone SDL host is included in the new build targets.
The original Diorama checkout and its installed data were not modified.

## Inputs and source

- Source: Diorama commit `0816c17a53afe613a0fea728872645c727120699`;
  148 source files pinned individually in `../source-handoff.json`.
- Supplied unencrypted European image: 536,870,912 bytes, program ID
  `00040000001CB200`, SHA-256
  `e72f516e11c38be6fc2252e1e768247916f492740d3fef395dfcfe71e01eb4cb`.
- C++20; original floating-point contraction setting retained. Embedded targets
  use private source include paths and isolated namespaces. The standalone
  import/install API was removed from the embedded copy: Reprise's importer
  writes only into an empty Library-owned staging folder.
- macOS checks used Apple silicon, Apple clang 17 and the installed SDL 3.4
  renderer. Android libraries compiled for ARM64/API 26 with NDK 29.0.14206865
  and the project's pinned SDL 3.4.16.

## Importer checks

`captain_toad_import_check` verifies bounded random-access reads; unrelated image
recognition; recognized encrypted/damaged input errors; malformed IVFC block
shifts; cyclic RomFS file and directory lists; overflowing member offsets;
code decompression limits; out-of-range SARC tables; and staging preservation.
The archive parser now also rejects Windows-style escape paths.

A real import through Reprise's document-aware `FileSource` produced 4,210 files.
A separately compiled importer from the untouched pinned source produced the
same file set and payloads. `import-equivalence.json` records zero differences.
The only permitted metadata difference is the standalone import timestamp;
the rest of that manifest is compared as JSON. The payload manifest digest is
`02a36346d4be2d4cc188ff8dfa68200416797c155e7a3ce94727a93a66ad0694`.

This checks the new reader/staging adapter, not the Android document-picker UI
or installation through the catalog, which is still pending.

### Historical baseline differences — unresolved

The installed standalone package predates the source revision: 155 fragment
shaders differ (the newer source has the `shadowKernel` parameter). This is why
the comparison above uses a fresh source build rather than the installed cache.

The local research exporters were also run into a fresh temporary directory:
`export_opening.py`, `export_layouts.py`, `export_eft.py`, `export_sound.py`.
Their `compare_packages.py` reports **545 common files, 178 problems** against
the current native output: skin/tangent payloads, particle tables and shader
sources. These are inherited source/exporter differences, not introduced by
the Reprise adapter (the fresh native-to-native comparison is exact). They
remain an unresolved research-handoff gate. Do not rewrite sealed evidence,
ignore these categories, or claim original-ROM fidelity from this adapter test.

## Programmable graphics proof

`gpu_host_probe` exercises the actual Reprise host and synthetic shaders:

- Sampled textures, near/far depth occlusion and stencil-restricted drawing.
- Repeated native GPU submissions into the host's own SDL texture.
- Reallocation at 320×240 and 517×291, verified by captured pixel colours.
- One-off raw capture, recovery after a thrown draw callback, and the host's real VHS shader/compositor.
- An unfiltered yellow overlay over the filtered image, visually inspected.

Both **Metal/MSL** and **Vulkan/SPIR-V** passed on the Mac; source/binary and capture hashes are in `gpu-host-proof.json`. Ordinary frames do
not read back pixels; capture checks explicitly do so. Targets now use native
RGBA32 instead of a potentially converted packed RGBA8888 texture, making the
underlying GPU texture available. The shared `GpuCommands` scope borrows the
host's device/target and owns command submission; Reprise retains presentation.

This proves the host-composition route. It does **not** yet prove the game's
material compiler, full `gpu::Device` implementation, original rendering parity,
performance at 120 Hz, Android device rendering or surface-loss recovery.

The existing `screen_effect_check` also passed native-resolution independence,
adaptive aspect rasters, and four filter levels: GPU/CPU worst mean error 0.000;
filter-off before/after mean error 0.000. The new GPU-command header was separately
syntax-checked using the Android NDK and pinned SDL headers.

## Build and regression checks

- `./build.sh`: macOS app built and packaged.
- CTest: all 13 tests passed, including the new importer test.
- `./build-android.sh`: APK built; signature and 16 KB alignment checks passed.
- Explicit NDK build of `captain_toad_session` and `captain_toad_import_adapter`:
  passed. These libraries are not linked into the application yet, so APK
  packaging alone would not have established their portability.
- No physical Android gameplay validation, and no integrated gameplay or save
  behavior is claimed at this milestone.

## Reproduction

From the Reprise root (build dependencies as described in the root guides):

```sh
./build.sh
cmake --build build --target gpu_host_probe captain_toad_import_check
ctest --test-dir build --output-on-failure
./build/gpu_host_probe /tmp/reprise-gpu-check
SDL_GPU_DRIVER=vulkan ./build/gpu_host_probe /tmp/reprise-vulkan-check
./build/captain_toad_import_check "$ROM" /tmp/new-reprise-import
python3 tools/compare_captain_imports.py /tmp/new-reprise-import "$SOURCE_IMPORT"
./build-android.sh
cmake --build build-android/app --target captain_toad_session captain_toad_import_adapter
```

`ROM` is the user's supplied image; `SOURCE_IMPORT` is a fresh output of the
pinned standalone `diorama-import`, not the old installed package. Use new
output directories for evidence runs. The equivalence report stores hashes,
not assets. Regenerate synthetic probe shader headers with
`python3 tools/build_gpu_probe_shaders.py` (glslangValidator and spirv-cross).
