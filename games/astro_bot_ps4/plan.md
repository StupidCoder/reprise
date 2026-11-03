# Beacon integration plan

Stable ID: `astro_bot_ps4`; source project: Beacon. Title-card work is owned by
another agent. Keep the existing catalog identity and do not regenerate artwork.

## Current slice

Portable core copied with per-file provenance, isolated namespaces and explicit
CMake targets: data, animation, physics, gameplay and portable audio decoding.
The catalog now imports CUSA12392.v1.04.pkg directly, plus legacy .beacon
character and music bundles, through Library staging and
opens an animated textured asset viewer. The exporter discovers base-texture
dependencies from decoded models, including textures outside the character folder.
144 models with visible geometry are available; meshless skeletal/effect files
are excluded. See `docs/asset-viewer.md` for usage and validation.
Gameplay remains unavailable. No persistent-save or gameplay feature badges are enabled.
Beacon is retained and is not superseded by this partial handoff.

## Next slices and acceptance criteria

1. Import: package/PFS extraction for the character/music viewer is implemented;
   see `docs/package-import.md`. Expand the selected data as gameplay consumers
   arrive. Other releases and arbitrary modified packages remain unsupported.
2. Session: extract scene orchestration from Beacon's LevelScene into a headless
   session with fixed ticks, owned state and host GameInput. Preserve update order
   for hero, physics, objects, enemies, rescues and moving collision. Replay the
   source ROOFTOPS inputs before changing behavior; include existing hypotheses.
3. Presentation: adapt scene drawing to the shared programmable SDL GPU boundary
   established by Captain Toad, including depth, skinning, materials, shadows and
   post-processing. Avoid continuous readback. Route music through GameModule
   audio and implement portable movie decoding. Test filters and screenshots.
4. User controls: keyboard/gamepad plus touch; document flat-view camera and
   simulated head/controller behavior. VR scope requires a deliberate host design;
   it is not implemented by the flat-screen handoff. Pause, focus loss and device
   recreation must preserve session correctness.
5. Persistence: versioned owned state and content identity, transactional loading,
   documented checkpoint/exact-state scope, RNG state and options separation.
   Validate save/quit/relaunch/Continue before enabling persistent saves.
6. Connect catalog import/start and truthful feature badges only after these
   contracts work. Exercise desktop and Android ROOFTOPS flow, resizing, pause,
   audio and touch; update controls/objectives and the playable-scope description.

## Known limitations

All source-game reconstruction gaps remain: see `docs/research/backlog.md`.
No new gameplay fidelity is claimed. The asset envelope bounds names, file sizes/counts and total input, checks release
identity and per-file hashes, and uses transactional Library installation. Legacy
format readers have not undergone a comprehensive hostile-input audit. Cross-platform RNG sampling is not proven. Full
rendering, audio presentation and persistent state have not been tested in Reprise.
The source project has no selected license for its own code; preserve that status
rather than labeling the copied core MIT. Third-party notices are retained.
