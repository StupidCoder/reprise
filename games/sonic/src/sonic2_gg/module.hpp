#pragma once
#include "app/game_module.hpp"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

namespace sonic2_gg {

// Creates the Sonic 2 (Game Gear) module from an installed asset pack. With a
// non-empty `state` (a quick save), play resumes where it was saved;
// otherwise the game starts at the SEGA logo like the cartridge.
std::unique_ptr<shell::GameModule> make_module(const std::filesystem::path& pack,
                                               std::span<const std::uint8_t> state = {});

// Starts directly at a level selector start point (index into levels).
std::unique_ptr<shell::GameModule> make_level_module(const std::filesystem::path& pack,
                                                     unsigned level);

// Debugging and testing aids, not used by the shell: changes applied to a
// running Sonic 2 module before its next update. Negative values leave a
// setting alone; the teleport only acts while an act is being played.
struct Assist {
    int lives{-1};
    int rings{-1};
    int teleport_x{-1}, teleport_y{-1}; // Sonic's new position in level pixels
    int boss_hits{-1};                  // A boss needs at most this many more hits
    bool move_camera{};                 // The teleport also moves the camera along
};

void assist(shell::GameModule& module, const Assist& changes);

// What a test driver may look at: Sonic's position and the objects of the
// act (type numbers as in the placement tables), in level pixels.
struct Observation {
    bool playing{};
    int sonic_x{}, sonic_y{};

    struct Thing {
        int type, x, y;
    };

    std::vector<Thing> objects;
};

Observation observe(const shell::GameModule& module);

} // namespace sonic2_gg
