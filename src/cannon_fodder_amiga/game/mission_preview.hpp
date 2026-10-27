#pragma once
#include "cannon_fodder_amiga/data/assets.hpp"
#include "platform/image.hpp"
#include <optional>

// Pictures of a mission's start for the launcher's viewer and the asset
// tool, from the rebuilt set-up, drawers and panel. Objects whose first
// update is not rebuilt yet (soldiers, enemies and most other kinds: their
// looks come from the game logic) are shown as outlined markers: white for
// the player's soldiers, red for enemies, yellow for the rest.
namespace fodder::game {

// The graphic a kind takes on its first update, where that is a fixed value
// ($94608 handler table: kinds 13, 14, 15 → $8F, $90, $91 at $97296).
std::optional<int> fixed_first_graphic(int kind);

// The whole map with every placement's object at its start position.
platform::Image map_objects_image(const data::Content& content, int number);

// The play view at the start of a one-screen mission (camera at 0, 0; maps
// of 19 x 15 tiles) with the panel as a new game shows it: the mission's
// soldiers are the first recruits, rank 0, in squad 0. Nothing for other maps
// (their start position comes from the start pan, not rebuilt as a picture yet).
std::optional<platform::Image> mission_start_image(const data::Content& content, int number);

} // namespace fodder::game
