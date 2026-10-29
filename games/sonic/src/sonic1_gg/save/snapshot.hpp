#pragma once
#include "sonic1_gg/audio/voice.hpp"
#include "sonic1_gg/frontend/session.hpp"

namespace sonic1_gg {
// Versioned native snapshots. Never serialize host handles or raw C++ object memory.
Bytes save_snapshot(Session&, SoundDriver&, gg::Psg&, LogoVoice&, std::uint64_t ticks);
void load_snapshot(std::span<const std::uint8_t>, Session&, SoundDriver&, gg::Psg&, LogoVoice&,
                   std::uint64_t& ticks);
} // namespace sonic1_gg
