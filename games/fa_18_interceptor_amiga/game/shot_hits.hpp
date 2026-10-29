#pragma once
#include "amiga/hunk.hpp"
#include "fa_18_interceptor_amiga/game/weapons.hpp"
#include "fa_18_interceptor_amiga/game/world_objects.hpp"
#include <vector>

// A round against the records (`$11AE96`'s object part, game.md "Weapons
// against objects"): the records drawn this frame in the round's cell (at
// most six), not its owner; within a box ($140, or $800 for models marked
// $10 at +7) the round goes on in eighths of its move, and at each step
// is tested against the record's hit model - the faces of `$12DC64`'s list
// for the model's byte +1, the record's model points turned as the drawer
// turns them: inside every face, it hit. A hit adds to the log's cannon
// hits and the record's damage (+$3C); the third crashes it.
namespace interceptor::game {

class ShotHits {
public:
    ShotHits(const amiga::LoadedImage& program, const FlightTables& tables) : program_(program), tables_(tables) {}

    // True when the round hit a record (it then stands where it hit).
    // `viewer`: where the scene was drawn from (the detail levels go by its
    // distance).
    bool test(Shot& shot, std::vector<WorldObject>& objects, int& cannon_hits,
              const std::array<std::int32_t, 3>& viewer) const;
    // The model the drawer draws a record with from `viewer` (`$111D8C`:
    // the first level whose threshold, in 16-unit steps, is beyond the
    // distance); 0 when none is drawn.
    std::uint32_t drawn_model(const WorldObject& o, const std::array<std::int32_t, 3>& viewer) const;

private:
    bool inside(const Shot& shot, const WorldObject& o, std::uint32_t model) const;
    const amiga::LoadedImage& program_;
    const FlightTables& tables_;
};

} // namespace interceptor::game
