#pragma once
#include "bruce_lee_c64/data/assets.hpp"
#include "bruce_lee_c64/game/playfield.hpp"
#include <cstdint>

// What an actor touches, found by testing its collision mask against the
// glyphs under it pixel by pixel ($521B). Multicolour pixel pairs in the
// room's glyphs stand for kinds of terrain; four groups of them give four
// flags.
namespace bruce_lee::game {

struct TerrainContact {
    std::uint8_t feet{}; // the flags over the mask's first 21 rows below its top offset ($B6)
    std::uint8_t body{}; // the same over the first 15 rows ($A7)

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) { ar(feet, body); }
};

// x and y are the actor's coordinates (x in pixel pairs, y in lines).
TerrainContact probe_terrain(const Playfield&, const data::Charset&, const data::CollisionMask&, int x, int y);

} // namespace bruce_lee::game
