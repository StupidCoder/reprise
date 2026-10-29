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
    explicit ShotHits(const amiga::LoadedImage& program) : program_(program) {}

    // True when the round hit a record (it then stands where it hit).
    bool test(Shot& shot, std::vector<WorldObject>& objects, int& cannon_hits) const;

private:
    bool inside(const Shot& shot, const WorldObject& o) const;
    const amiga::LoadedImage& program_;
};

} // namespace interceptor::game
