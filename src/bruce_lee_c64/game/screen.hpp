#pragma once
#include "bruce_lee_c64/data/assets.hpp"
#include "c64/text_frame.hpp"
#include <array>
#include <cstdint>
#include <vector>

// The play screen as the C64 shows it: the status rows at the top in the
// text font, the room below in its own charset, and the colour changes the
// raster interrupts make down the screen. Built from game state every frame.
namespace bruce_lee::game {

struct PlayScreen {
    int room{};
    // Per lantern of the room's list: taken (the room shows its "taken"
    // character there).
    std::vector<bool> lanterns_taken;
    // Row 1, the status line, as text codes (40 cells).
    std::array<std::uint8_t, 40> status{};
};

c64::TextFrame play_frame(const data::Assets&, const PlayScreen&);

} // namespace bruce_lee::game

#include <optional>
namespace bruce_lee::game {
class World;
// Where the presentation layer shows the moving things instead of the
// game's own places (actors: x in pixel pairs, y in lines; fireballs the
// same), for the smooth-motion option.
struct SpritePlaces {
    std::array<std::optional<std::array<double, 2>>, 3> actors;
    std::array<std::optional<std::array<double, 2>>, 2> fireballs;
};
// The play screen drawn from a game in progress: the playfield, the colour
// bands and the actors' sprites ($25CF).
c64::TextFrame world_frame(const World&, const SpritePlaces* places = nullptr);
} // namespace bruce_lee::game
