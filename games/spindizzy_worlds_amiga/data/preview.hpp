#pragma once
#include "platform/image.hpp"
#include "spindizzy_worlds_amiga/data/assets.hpp"

// Pictures of the decoded data, for checking it (launcher asset viewer,
// export tool). The title picture and the level pictures are drawn as the
// game draws them; the maps from above are not the game's.
namespace spindizzy::data {

platform::Color to_color(Colour);

// The title picture as the game shows it (320 × 200).
platform::Image title_image(const Picture&);

// A level as the game draws its playfield (the whole level, cropped to
// what it covers; palette 0, the one a level starts with at $11FA4), with
// the explorer at its start cell and the objects, in the original's depth
// order (exits are hidden at the start; stars belong to the screen).
platform::Image level_image(const World&, int level, const Assets&);

// Every level of a world seen from above: each column shaded by the height
// of its top block, objects marked, level numbers above.
platform::Image world_image(const World&, const std::array<std::uint8_t, 256>& block_attributes);

} // namespace spindizzy::data
