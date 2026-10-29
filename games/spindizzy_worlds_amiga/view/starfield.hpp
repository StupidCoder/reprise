#pragma once
#include "spindizzy_worlds_amiga/data/assets.hpp"
#include "spindizzy_worlds_amiga/view/playfield.hpp"
#include "spindizzy_worlds_amiga/world/level_builder.hpp"
#include <cstdint>
#include <vector>

// The starfield object (kind 14): stars fixed to the screen, not the map,
// placed by the game's own random generator from the level's seed.
namespace spindizzy::view {

// The generator $112CA: a 32-bit state; the low word × $F1 + $29 (the add
// only on the low word) becomes the state; the result is its low word plus
// its high word.
class StarRandom {
public:
    explicit StarRandom(std::uint32_t state) : state_(state) {}
    std::uint16_t next();
    std::uint32_t state() const { return state_; }

private:
    std::uint32_t state_;
};

struct Star {
    int x{}, y{};           // screen pixels
    std::uint16_t clear{};  // the word's bits that must be background ($15B96 by x & 15)
};

// $15AFC: `count` stars (the first parameter); a non-zero seed (the second,
// plus the level's rotation) gives state seed:seed and leaves the running
// generator untouched; seed 0 draws from the running generator.
std::vector<Star> make_stars(const data::Memory& main, int count, StarRandom& random);
std::vector<Star> make_stars(const data::Memory& main, const world::PlacedObject&, StarRandom& running);

// $15BD2: each star in colour 3 where its pixel and the neighbours in
// `clear` are colour 0. `origin_x` is where the original's buffer column 0
// is on the canvas.
void draw_stars(Canvas&, const std::vector<Star>&, int origin_x = 0);

} // namespace spindizzy::view
