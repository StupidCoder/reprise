// Sound banks (.sxd): the sound engine's bank container (tag "SXDF"). Findings and checks:
// .analysis/docs/sound.md.
//
// Header: "SXDF", u16 version (4), u16, u32 file size, 16 bytes, bank name (64 bytes, zero-padded).
// Chunks from 0x60: tag[4], u32 payload size, payload; each chunk 4-byte aligned. Seen: TONE (tone
// key maps), REQD (requests), NAME (sound names), WAVE (waveforms), SEQD (event sequences), LVAR,
// LVRN, RXFD, RXFN, DATA (sample data).
//
// WAVE: u32 0, u32 count, then count self-relative u32 offsets to variable-size entries:
//   +0 u32 1, +4 u8 codec (0x21 HE-VAG ADPCM, 0x42 ATRAC9), +5 u8 channels, +6 u16 0x00ff,
//   +8 u32 sample rate, +12 u32 (16384), +16 u32 0, +20 u32 samples per channel,
//   +24 u32 loop start, +28 u32 loop end (0xffffffff = no loop), +32 u32 data bytes,
//   +36 u32 data offset relative to this field; ATRAC9 entries carry the codec's 4-byte
//   configuration as a little-endian u32 whose top byte is the 0xFE sync byte.
// NAME: u32 0, u32 count, count x {u32 self-relative string offset, u32 hash, u32 request index}.
//
// Sound names resolve to waves through (see .analysis/docs/sound.md):
//   REQD request {u32 0x47, u32 flags (low byte: 1 = sequence, 2 = transition), u32 index, ...}
//   SEQD sequence: event stream; PLAY_TONE (0xa4) operand 4 = note, PLAY_WAVEFORM (0xa2)
//        operand 1 = wave, PLAY_SEQUENCE (0xa3) operand 1 = sequence. Operands: type byte (high
//        nibble 0/1 = +/- BCD number, low nibble = byte count), opcode bytes >= 0xa0.
//   TONE keymap regions {u16, u16 note, u16 wave, u16, u16, u16}
//   TRNS transition: tracks with a condition (variable == value, variable name inline) and an
//        action whose instruction 0x31 names the sequence to play (0x42 sets a slot's level)
//   LVAR variables (inline 32-byte entries) with value labels, e.g. battle_state = bgm_battle_win.
#pragma once

#include "data/Bytes.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace beacon::data {

enum class WaveCodec : std::uint8_t { HeVag = 0x21, Atrac9 = 0x42 };

struct Wave {
    WaveCodec codec = WaveCodec::HeVag;
    std::uint32_t channels = 1, sampleRate = 48000;
    std::uint32_t samples = 0;                         // per channel
    std::optional<std::uint32_t> loopStart, loopEnd;   // sample positions
    ByteView data;
    std::array<std::uint8_t, 4> atrac9Config{};   // byte order as the decoder expects
};

struct SoundName {
    std::string name;
    std::uint32_t request = 0;
};

// One way a wave is played: by sound name, optionally only in a state of an interactive piece.
struct WaveUse {
    std::string name;    // e.g. "mup_bird_boss_battle_bgm"
    std::string state;   // e.g. "bgm_battle_win", "bgm_state=1", empty for unconditional
};

// An interactive piece's level change: while `variable` holds `state`, the piece plays at `level`
// (TRNS track with that condition and action 0x42). E.g. mup_city_rooftop_bgm_vox: bgm_hero_state
// idle -> 1, move -> 0.
struct LevelRule {
    std::string name;       // sound name of the piece
    std::string variable;   // e.g. "bgm_hero_state"
    std::string state;      // value label, e.g. "idle" (or "variable=n" without labels)
    float level = 1;
};

struct SoundBank {
    std::string name;
    std::vector<Wave> waves;
    std::vector<SoundName> names;
    std::vector<std::vector<WaveUse>> waveUses;   // per wave
    std::vector<LevelRule> levelRules;
};

// The bank keeps views into `file`, which must outlive it.
std::optional<SoundBank> loadSoundBank(ByteView file, std::string* error = nullptr);

}   // namespace data
