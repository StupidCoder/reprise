# Stunt Car Racer — remaining work

Status (2026-10-29): importer, the loading picture and the eight circuits
(decoded from the track bank, shown as turnable 3D models) are connected;
nothing is playable yet. Findings are in [game.md](game.md).

Done:

- [x] Identify the user's image reproducibly (ADF and region SHA-256s).
- [x] Disk layout, loader and program start-up (sample conversion, defeated
      checksum) recovered.
- [x] Import into the content library (`loader`, `program`), with rejection of
      other disks and a damaged-program error; loading picture as preview.
- [x] Reference harness (FS-UAE, port 6880) boots to the menu; `scr.py`
      drives it by key matrix to the practice races.
- [x] Ghidra project over the decrypted program (`ghidra/ghidra.sh`).
- [x] Track format re-derived from the unpacker ($5AE46) and verified
      against the original's RAM tables (Little Ramp, Hump Back); circuits
      as 3D model previews (`data/track.*`, `data/preview.*`).

Next:

- [ ] Map the front end's state machine and the main race loop in Ghidra
      (the 6502-style code decompiles poorly; read assembly where needed).
- [ ] Interpret the rest of a track definition: header bytes 1–3 (start
      line?), the four trailer bytes, the (segment, value) pairs and the
      segment list, and the per-segment table built at $1C952 (opponent
      speeds?); the shape description bytes ($1BB7B, $1BBD9, $1BBD4).
- [ ] Confirm the height scale (¼ of plan units, from the view code) once
      the renderer is recovered; the drawbridge animation ($5A794).
- [ ] Font and front-end screens (menu, league tables, track select) from the
      program's data.
- [ ] Car physics, opponent driving, damage, race rules and the league.
- [ ] Renderer: filled-vector 3D view, cockpit/panel, horizon; widescreen
      by widening the view, smooth motion up to 120 Hz.
- [ ] Sound effects (engine, impacts).
- [ ] Keyboard, gamepad and touch controls; quick saves of real state;
      VHS/screen filter; cheats (e.g. no damage, practice any track).
- [ ] Multiplayer / Computer Link: decide scope (serial link has no host).
- [ ] Load/Save/Replay menu: what it reads and writes on disk (league saves,
      replays) and how the port stores it.
- [ ] Desktop and Android builds validated; differences documented.

## Open bugs

No playable implementation exists to test yet.
