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
    return map;
}

} // namespace rocknroll::game
