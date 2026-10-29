# Reprise implementation plan

## Foundation

- [x] Preserve Joyport Git history and establish app/shared/games boundaries.
- [x] Original Reprise layered-tile mark, three-column collector shelf.
- [x] Optional collection metadata and Sonic/Layton/Commodore palettes.
- [x] Smooth scrolling, cover transitions, controller/pointer navigation.
- [x] Preserve details, documentation, lessons and persistent save format.
- [x] Explicitly labelled preview catalog entries; no fake play/import actions.
- [x] Ignore the user’s images directory and exclude it from packaging.

## Migration and integration

- [x] Resolve final Joyport/Picarat/BlueStreak handoff commits and remaining UI edits.
- [x] Import committed Layton, Spy Mouse and Sonic sources with provenance/history.
- [ ] Connect their actual engines and importers to the Reprise host.
  - [x] Spy Mouse (2026-10-28): importer, play, pause, saves, previews (docs/VALIDATION.md);
        music through the engine's own HE-AAC decoder, no platform decoder needed.
  - [ ] Layton, Sonic.
- [x] Establish GPU presentation and ordered pointer/touch event contracts (GpuFrame,
      PointerEvent; docs/MIGRATION.md).
- [x] Screen filter stage for GPU-drawn games (2026-10-29): the VHS look as a GPU shader
      (SDL GPU renderer, SPIR-V/MSL) or CPU fallback; Spy Mouse first (docs/VALIDATION.md).
- [ ] Preserve game timing, floating-point requirements and replay validation.
  - [x] Spy Mouse: fixed 17 ms ticks, scoped flush-to-zero, frames identical to the
        standalone replay; module check `spy_mouse_checks`.
- [ ] Migrate existing libraries and profiles without overwriting originals.
  - [x] Shared takeover hook (GameEntry::legacy); Spy Mouse uses it for DoubleOCheese.
- [ ] Spy Mouse: finish the move to version 1.1.1 (re-verify the changed rules against
      recorded 1.1.1 runs; the store, dossiers, new-world card and world 7 are not ported).
- [ ] Complete incoming release metadata and the remaining Sonic catalog.
- [x] ARM64 Android APK pipeline with SDLActivity, signed packaging and document URI import bridge.
- [x] Responsive portrait/landscape main collection shelf with portrait swipes.
- [x] Portrait details and documentation (assets, development notes, lesson browser).
- [ ] Portrait lesson experiment canvases and physical-device input/game validation.
- [x] Audit per-game planning and research documents: docs/GAME-DOCUMENTATION-AUDIT.md.
- [ ] Search and separate platform filtering from optional collections.
- [ ] Register lessons through game entries rather than launcher-specific includes.
- [ ] Add Diorama later, retaining the independently built OpenXR application.

The inherited per-game implementation history is in docs/JOYPORT-BASELINE.md.
