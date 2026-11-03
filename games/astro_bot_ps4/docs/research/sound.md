# Sound and music (2026-10-12)

Engine: "sndx" (source paths sndx/sndx_player*.c, sndx_engine_ngs2_orbis.c: plays through the
platform's NGS2 mixer and Audio3d), banks `.sxd`. Research tools: `tools/sxd.py` (chunk list),
C++ `build/beacon-sound info|check|wav|interleave`.

## Files

72 banks: `data/common/sound/` (library_sfx 81 MB, buss effect banks) and
`data/multi_platformer/sound/`: 22 `*_bgm.sxd` (music per world/boss/menu), `*_sfx.sxd`
(effects, by topic), `*_amb_sfx.sxd` (ambience), `multi_platformer_buss.sxd`. `resident.xml`
lists the banks kept loaded. `lox/bgm_player.lox` etc. place sound objects in levels.

## SXDF container

Header: "SXDF", u16 4, u16, u32 file size, 16 bytes (GUID-like), bank name at 0x20 (64 bytes).
Chunks from 0x60 {tag, u32 size, payload}, 4-byte aligned: TONE, REQD, NAME, WAVE, SEQD, (LVAR,
LVRN, RXFD, RXFN), DATA. Offsets inside chunks are relative to the field holding them.

- WAVE: u32 0, u32 count, self-relative offsets to variable-size entries: u32 1, u8 codec, u8
  channels, u16 0x00ff, u32 rate (all 48000 seen), u32 16384, u32 0, u32 samples per channel,
  u32 loop start, u32 loop end (inclusive; 0xffffffff = none), u32 data bytes, u32 data offset
  (relative to that field, inside DATA), then optional fields; ATRAC9 entries carry the 4-byte
  codec configuration as a u32 with top byte 0xFE at a varying position (byte 44, 64 or 76),
  identified by matching rate and channel layout.
- NAME: u32 0, u32 count, {u32 self-relative string, u32 hash, u32 request index}. Names come in
  pairs "x" / "x_tv" (HMD and TV/social-screen outputs), some "_vox" stems.
- Name -> wave chain (decoded 2026-10-12, sound-name resolution in data/SoundBank.cpp):
  - REQD: self-relative table; entry {u32 0x47, u32 flags, u32 index, u32 ?, u32 0x8/0x60008,
    u32 output (2 = headset, 6 = TV), [u32 0x110/0x210, variable bindings 0x7d0000|id...]}.
    flags low byte = kind: 1 = play sequence `index`, 2 = play transition (TRNS) `index`.
  - SEQD: self-relative table; entry {u32 0, u16 0, u16 1000, u32 0x4000, u32 size, u32 rel offset}
    to an event stream. Opcodes from the executable's sequence player (FUN_00d4d690, debug
    strings): a0 PLAY_BY_ID, a2 PLAY_WAVEFORM(wave,...), a3 PLAY_SEQUENCE(seq,...),
    a4 PLAY_TONE(handle, program, volume, note, pitch 4096, ...), a5 RELEASE, a6 BREAK, a7 STOP,
    a8 SYNC, a9/b8 SET_PARAM, f1 (wait), ff END. Operands (FUN_00d4ba90): type byte, high nibble
    0/1 = +/- BCD number with (low nibble) bytes of two decimal digits; 2x/3x = variable
    references; an opcode byte ends the operand list (remaining operands take defaults).
  - TONE: u32 0, u32 2, two headers {u32, u16, u16, u32 count, u32 rel}: [0] keymaps {u32, u16,
    u16 lo, u16 hi, u16 regions, u32 rel} -> regions {u16, u16 note, u16 wave, u16, u16, u16};
    [1] split groups (values exceed the wave count; variable-driven variants, not needed for
    naming). Evidence for note -> wave: max region wave = wave count - 1 in every bank checked.
  - TRNS (interactive music, sndx_player_trans): self-relative table; entry: 0x1c bytes, u32
    track count, self-relative track offsets; track (64 bytes) {u32 flags, u32 track id, rel
    name ("Action_N"/label), ..., +16/+20 rel conditions, +28 rel actions}. Condition bytecode
    "... f1 0f <type> <len incl. 0> <variable> 0 <value> b2 <len> <track label>". Action =
    instruction list {u8 operand count | 0x80 last, u8 opcode}, operands {u8 length, LE bytes}
    (interpreter FUN_00d56a00); opcode 0x31 = play sequence operand[1] on slot operand[0];
    0x42/0x60/0x61 other (volume/fade, not decoded). A piece can start several layers.
  - LVAR: u32 0, u32 count, inline 32-byte entries {u32 0, u32 id (e.g. 2003), rel name, u32,
    rel value list {u32 0, u32 count, rel first label}, ..., float}; LVRN holds the names.
    Example: battle_state = bgm_battle_start / bgm_battle_win / bgm_battle_lose.
  - Checks: C++ and Python prototype (tools/sxd.py music_names) agree on all 21 music banks.
    Bird boss verified acoustically: waves 0/4 and 3/5 are identical copies (TV routing),
    and the resolved chain gives battle start/win/lose + quiet intro loop consistently.
    Coverage: 150 of 190 music-bank waves named; effect banks 721 of 12,232 (they use other
    play paths: PLAY_BY_ID, variable operands; not needed yet).

## Codecs

- 0x42 ATRAC9: byte rate 18,000 B/s for 48 kHz stereo = 144 kbit/s; configuration e.g.
  FE 74 0B F0 (48 kHz, stereo, 384-byte superframes of 4 frames). Decoded with LibAtrac9 (MIT,
  pinned commit in third_party/atrac9/VERSION.txt). Pitfall: its decode call handles one frame and
  reports bytes used rounded down; frames are packed back to back within a superframe and each
  superframe is padded to its fixed size. (Advancing by "bytes used" across superframes drifts;
  advancing by fixed frame size fails.)
- 0x21 HE-VAG: byte rate 54,857 B/s stereo = 16-byte frames of 28 samples; predictor index up to
  127 (byte 1 high nibble holds the upper bits), 4-tap coefficients from vgmstream's table
  (third_party/hevag_coefs.h, permission notice kept). Mono decodes smoothly (roughness 0.02).
  Stereo layout unverified: 16-byte interleave assumed; roughness tests on 34 stereo effect waves do
  not discriminate (noisy sounds).

## Checks

`beacon-sound check`: all 12,422 waves of the 72 banks decode, each to exactly its stated sample
count, 31,262 s of audio in 14 s. Title music: RMS 5262, L/R correlation 0.70; loop seam: audio
before loop start and before loop end align with a lag of 11 samples (cross-correlation over
65,536 samples), so loop points are used as stored.

## Level music (bgm_player, 2026-10-16)

Decoded from code (evidence/decomp/bgm/), ported in src/game/LevelMusic.*; output
src/platform/MusicOutput.* (3 slots mixed at 48 kHz), cue playback in src/app/BeaconApp.cpp.

- Class bgm_player: constructor 0x30c290 (vtable 0x1688090), properties Request (+0x1cb0 ..),
  RequestHero, RequestController, TriggerBox link (+0x1f50), FadeInTime (value +0x2120, default 0),
  FadeOutTime (+0x21d0, default 0.5), TriggerType enum Normal=0 / HeroSpawnLanding=1 (+0x2280).
  State +0x1ca4.
- **Name mismatch:** the code binds "FadeInTime"/"FadeOutTime"; the level files (and the lox schema)
  hold FadeInDuration/FadeOutDuration, so the defaults apply (ROOFTOPS' 2.0 s is not read).
  [decoded; the binder's by-name lookup is assumed as for the hero parameters]
- Start 0x30c7d0 (vtable slot 4): unless owner flag (+0x1b60 -> +0x210) or level name hash
  0x693c55c: a Normal player with an empty link plays and enters state 1. (Exceptions not ported.)
- Update 0x30cb90 (slot 8): state 0: Normal + empty link -> play, 1; Normal + link fired (0x552f00)
  -> play, 2. State 1: link fired -> play (a no-op, see below), 2. State 2: Normal + empty link ->
  stop all slots (0x73a2e0, no fade), 0. 0x552ed0 = link empty.
- Play 0x30c9b0: acts only in state 0, state 1 with an empty link, or state 2. Per slot (0 Request,
  1 RequestHero, 2 RequestController): 0x73a710 marks the slot used; a request starts through
  0x739dc0 (FadeInTime, volume 1 x the music volume at global +0x930, no position; slot 0 also
  gets 0xc445a0(1)), no request stops the slot over FadeOutTime (0x73a460).
- Forced play 0x30cc70 (state 0, play, state 2). Callers: 0x53c400 (every player in a room list
  +0x388) <- 0x551ae0 (every room) <- the hero's spawn state update 0x69ed20 (on landing: clip
  0x98f5c7bb, machine state 5) and 0x6a09f0 (timer variant); also MupVrMenu (world map).
  **[inference]** the room lists hold the HeroSpawnLanding players.
- ROOFTOPS: sound_bgm_player (HeroSpawnLanding, Request mup_city_rooftop_bgm = city_bgm wave 0
  stereo, RequestHero mup_city_rooftop_bgm_vox = wave 1 mono, both 149.49 s looping; its
  TriggerBox UUID names no object, so the link is empty) and sound_bgm_player_no_track (Normal,
  empty: stops every slot at the start). Port run (BEACON_DEBUG_MUSIC=1): three stops at load, then
  slots 0/1 start when the hero first touches the ground.
- Port choice: the spawn drop is not ported, so "spawn landing" is the hero's first grounded
  update after play starts.

### The vocal layer (RequestHero) is interactive (2026-10-16, after the user heard it wrongly)

- The first port mixed mup_city_rooftop_bgm_vox in at full level; the user did not recognise it.
- The _vox requests are REQD kind 2 (TRNS 0), bound to variable 2001 = LVAR `bgm_hero_state`
  (labels idle = 0, move = 1). TRNS 0 tracks: Action_0_0 (start) 0x31 plays sequence 1 (wave 1)
  on piece slot 1; Action_1_0 condition bgm_hero_state == move (quantized "_beat1") -> 0x42
  [1, 0, 0, 0, 60, 0]; Action_2_0 condition == idle ("_bar1") -> 0x42 [1, 0, 1, 0, 30, 0].
- Opcode 0x42 in the action interpreter 0xd56a00: operand 0 slot list, operand 2 = the level
  (passed to 0xd5abf0); operands 3/4 are read and not used for it; operand 5 a further float
  (0 here). So: move -> level 0, idle -> level 1.
- Who sets bgm_hero_state (0xc3cd60(name, state); string built from immediates): Idle enter
  0x4c8870 and exit 0x4c8ae0 and the idle-wait restart 0x4c9370 set "move" (enter/exit only
  with hero +0x78 set); the Idle update 0x4c8c60 sets "idle" only when the random idle animation
  it picks after its wait (0x6888b0, wait from 0xebe680 between 0x11f200c / 0x11f2010) is one of
  three dances (0x9e85ccf4, 0x150c6a8c, 0x04dd9726); it then plays that clip with its speed scaled
  to the music tempo (0x11f2000 / +0x54 x 0x11f2004). So the hero sings along only while dancing.
- Also: music slots 1 and 2 are positional voices (handle +8 = 2D only for slot 0, set by
  0xc445a0; 0xc45f80 -> 0xc44e40 positions the others from an attached node / joint). Not ported
  (the vocal plays centred when audible).
- Port: data::SoundBank::levelRules (TRNS tracks with a condition and 0x42), MusicCatalog::
  levelRules, LevelMusic::setVariable from LevelScene::noteHeroMusicState (Idle enter / exit ->
  "move"), BeaconApp::slotLevel -> MusicOutput::setLevel. The idle dances are not ported, so the
  vocal stays silent (as it is in the original unless the hero dances). Beat / bar quantization
  of the level changes is not applied.

## Viewer (until 2026-10-16; the app no longer has a music panel, `beacon-sound music` lists tracks)

Music panel: 109 tracks (104 named) from the *_bgm banks: waves >= 10 s plus named "bgm" pieces
>= 1.5 s (win/lose stings, jingles). Title = first non-TV use ("name [state]"); waves started
together by one piece (same uses and length, e.g. final boss battle) are one track with layers,
mixed at playback; waves used only by "_tv" names are hidden unless "Show TV mixes" is on.
`beacon-sound music GAMEDIR` prints the catalog. Playback through SDL 3
(platform/AudioOutput), looping between the stored loop points; play/pause, stop, loop, seek,
volume. `--music BANK:WAVE` starts a track. Tested with SDL_AUDIO_DRIVER=dummy (no sound played):
position advances in real time.
