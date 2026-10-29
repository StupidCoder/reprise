#pragma once
#include "app/game_module.hpp"
#include <cstdint>
#include <filesystem>
#include <memory>

// Playing Rock 'n Roll in the shell: the title with the records and the
// name entry, then each level from its start wait through rolling, the
// panel's items, falls with the continue/restart prompt, to the exit with
// its time records (or a secret warp) and on; game over with the hall of
// fame, and the ending. Steered by the captured mouse's movement. The
// overview map, the jukebox and music are not rebuilt yet
// (games/rock_n_roll_amiga/plan.md).
namespace rocknroll::game {

std::unique_ptr<app::GameModule> start(const std::filesystem::path& content);

// Research and tests only (rock_n_roll_replay play): puts a running game's
// ball at a camera and view position with the given speeds, rolling, as
// the recorder's placement does on the original. False when the game is
// not in a level.
bool place_ball(app::GameModule&, std::int32_t camera_x, std::int32_t camera_y, std::int32_t view_x, std::int32_t view_y,
                std::int32_t speed_x, std::int32_t speed_y);
// Research and tests only: the money in the pocket (the recorder's poke of
// $719C2). False when the game is not in a level.
bool set_money(app::GameModule&, int money);
// Research and tests only: the energy ($6F45A; the panel follows it down).
bool set_energy(app::GameModule&, int energy);
// Research and tests only: the level clock's seconds and hundredths (the
// recorder's pokes of $719CA/$719CE). False when the game is not in a level.
bool set_clock(app::GameModule&, int seconds, int hundredths);

} // namespace rocknroll::game
