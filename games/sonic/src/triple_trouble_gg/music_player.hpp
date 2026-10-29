#pragma once
#include "gg_engine/audio/music_player.hpp"
#include <filesystem>

namespace triple_trouble_gg {
// The sound test's tracks (catalog.hpp) on the game's sound driver.
std::unique_ptr<gg::MusicPlayer> make_music_player(const std::filesystem::path& pack);
} // namespace triple_trouble_gg
