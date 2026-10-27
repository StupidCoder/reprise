#pragma once
#include "cannon_fodder_amiga/game/camera.hpp"
#include "cannon_fodder_amiga/game/world.hpp"
#include "cannon_fodder_amiga/sound/rjp1.hpp"
#include <array>

// The game's sound effects on the player (.analysis/cannon_fodder_amiga/
// sound.md): channels 2 and 3 take the effects in turn; a request of lower
// priority than the channel's last one waits out that one's 12 ticks; an
// object not on screen sounds the quieter the further it is from the view's
// centre. Reproduces $A3A30 and the busy count in the mission loop ($863B0).
namespace fodder::sound {

class SoundBoard {
public:
    // $A3A30 for one request.
    void play(const game::World& world, const game::GeometryTables& geometry, const game::SoundRequest& request,
              Rjp1& player);
    // All of a tick's requests, in order, then cleared.
    void play_all(game::World& world, const game::GeometryTables& geometry, Rjp1& player);
    // $863B0, once a tick: the channels' busy counts run down.
    void tick();

    int last_channel{2};               // $81DF6
    std::array<int, 4> priority{};     // $81DF8
    std::array<int, 4> busy{};         // $81DFC
    bool silenced{};                   // $81DCE (meaning open)
};

} // namespace fodder::sound
