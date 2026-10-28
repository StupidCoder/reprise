#include "rock_n_roll_amiga/game/level_map.hpp"

namespace rocknroll::game {
namespace {

Fixed header_long(const data::Level& level, std::size_t at) {
    const auto& h = level.header;
    return Fixed(std::uint32_t(h.at(at)) << 24 | std::uint32_t(h.at(at + 1)) << 16 | std::uint32_t(h.at(at + 2)) << 8 |
                 h.at(at + 3));
}

} // namespace

LevelMap LevelMap::from(const data::Level& level) {
    LevelMap map;
    map.columns = level.columns;
    map.rows = level.rows;
    map.cells = level.cells;
    map.camera_limit_x = header_long(level, 0x3D0);
    map.camera_limit_y = header_long(level, 0x3D4);
    map.camera_start_x = header_long(level, 0x3D8);
    map.camera_start_y = header_long(level, 0x3DC);
    map.view_start_x = header_long(level, 0x3E0);
    map.view_start_y = header_long(level, 0x3E4);
    for (std::size_t i = 0; i < 8; ++i) {
        map.pair_x[i] = header_long(level, 0x380 + 8 * i);
        map.pair_y[i] = header_long(level, 0x384 + 8 * i);
    }
    map.beam_x = header_long(level, 0x3C0);
    map.beam_y = header_long(level, 0x3C4);
    const auto word = [&](std::size_t at) { return std::uint16_t(level.header.at(at) << 8 | level.header.at(at + 1)); };
    map.warp_count_start = map.warp_count = word(0x1BE);
    map.reward_count_start = map.reward_count = word(0x1C8);
    map.left_behind = level.header.at(0x1D5); // set-up reads the word at +$1D4 as a byte ($6C6E2)
    map.refill = level.header.at(0x1D4);
    return map;
}

} // namespace rocknroll::game
