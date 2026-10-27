#pragma once
#include "amiga/hunk.hpp"
#include "wings_of_fury_amiga/data/assets.hpp"
#include "wings_of_fury_amiga/game/scenery.hpp"
#include "wings_of_fury_amiga/game/ticker.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

// The mission briefing (`$108590`, before each mission): on a hires screen
// of 640 x 147 with three bitplanes (`$106B04`), the world.shp shape `rank`
// (a panel with the labels RANK, MISSION, MISSION OBJECTIVES, ISLANDS,
// SHIPS) centred at ($140, $65), and over it in `newarmyfont` the rank's
// name, the mission number, the islands to take (-$5C7C) and the ships to
// sink (-$5C8E). It stays up for 240 frames or until fire is pressed (the
// key `r` instead goes back to the title).
namespace wings::game {

struct BriefingTables {
    std::array<std::uint16_t, 8> palette{};  // -$56D2: the screen's colours
    std::vector<std::string> rank_names;     // -$56EE: seven pointers
};
BriefingTables read_briefing_tables(const amiga::LoadedImage& wings);

struct BriefingScreen {
    static constexpr int width = 640, height = 147;
    static constexpr int frames = 240; // $108746: the wait, in frames
};

IndexedCanvas draw_briefing(const data::Assets&, const TickerFont&, const BriefingTables&, int rank, int mission,
                            int islands, int ships);

// $105956: a text in `newarmyfont` with its top-left corner at (x, y), each
// character the width + 1 further (11 for one without a picture), ORed in
// `colour`. Returns the width.
int draw_text(IndexedCanvas&, const TickerFont&, const std::string& text, int x, int y, std::uint8_t colour);

} // namespace wings::game
