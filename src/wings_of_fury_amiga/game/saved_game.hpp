#pragma once
#include "wings_of_fury_amiga/data/assets.hpp"
#include "wings_of_fury_amiga/game/flight.hpp"
#include <optional>
#include <string>
#include <vector>

// A saved game (RETURN FROM R&R; `$108B96` saves with Ctrl-g on deck and
// loads from the rank menu or with Ctrl-l). The original writes its
// variables and lists as they lie in memory to a file `wof.<name>`
// ($105E8A, $105EC2); the port writes the same game - the mission and its
// progress, the map as play changed it, the islands, ships, airfields,
// enemy planes and the plane - as text. What only lasts a moment (weapons
// and blasts in flight, smoke, splashes, sounds) is not kept.
namespace wings::game {

struct SavedGame {
    FlightWorld world;   // the map set (from `maps` by name), its columns as saved
    PlayerFlight plane;
};

std::string save_game_text(const FlightWorld&, const PlayerFlight&);
// Nothing when the text is not a saved game of this kind.
std::optional<SavedGame> load_game_text(const std::string& text, const std::vector<data::MissionMap>& maps);

} // namespace wings::game
