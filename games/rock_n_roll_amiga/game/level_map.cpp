#include "rock_n_roll_amiga/game/level_map.hpp"

namespace rocknroll::game {
namespace {

Fixed header_long(const data::Level& level, std::size_t at) {
    const auto& h = level.header;
    return Fixed(std::uint32_t(h.at(at)) << 24 | std::uint32_t(h.at(at + 1)) << 16 | std::uint32_t(h.at(at + 2)) << 8 |
                 h.at(at + 3));
}

} // namespace

// A cell-change list ($6DE0E): x word, y word, two value bytes (the second
// is the one that stays), until x = $FFFF; at the map at the latest.
std::vector<LevelMap::CellChange> LevelMap::read_changes(const data::Level& level, std::size_t at) {
    std::vector<CellChange> changes;
    const auto word = [&](std::size_t a) { return std::uint16_t(level.header.at(a) << 8 | level.header.at(a + 1)); };
    for (; at + 6 <= 0x3E8; at += 6) {
        const std::uint16_t x = word(at);
        if (x == 0xFFFF) break;
        changes.push_back({x, word(at + 2), level.header.at(at + 5)});
    }
    return changes;
}

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
    map.time_limit = word(0x1C6);
    for (std::size_t list = 0; list < 6; ++list) map.switch_lists[list] = read_changes(level, 0x218 + 0x3C * list);
    map.crumble_count = word(0x1D8);
    map.crumble_count_switched = word(0x1DA);
    map.crumble_list = read_changes(level, 0x1DC);
    map.crumble_list_switched = read_changes(level, 0x1FA);
    map.left_behind = level.header.at(0x1D5); // set-up reads the word at +$1D4 as a byte ($6C6E2)
    map.refill = level.header.at(0x1D4);
    for (std::size_t i = 0; i < 9; ++i) map.after_bomb[i] = level.header.at(0x1CA + i);
    return map;
}

} // namespace rocknroll::game
