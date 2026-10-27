#pragma once
#include "amiga/hunk.hpp"
#include <array>
#include <cstdint>
#include <optional>

// The physical world of a Mind Walker game, generated from the world's seed.
//
// Nothing of the landscape is stored: the landscape stage (A) computes what is
// at a cell from four key words when it needs it. Cells have 12-bit
// coordinates (x, y), wrapping at 4096. The game looks at them along the
// diagonals u = x + y and v = x - y (both mod 4096): every 16 cells of u and
// of v form a band, four bands a block of 64.
namespace mindwalker::world {

// What a coordinate's generator chain yields: each of its three levels (one
// per hex digit of the coordinate, the top digit first) ends with two words.
struct ChainLevel {
    std::uint16_t a{}, b{};
};

// One axis of the coarse landscape (A $209482 for u-like rows with keys 0 and
// 1, $20959A for the other axis with keys 2 and 3). The top level's mixer is
// seeded with the two keys and stepped in pairs (digit + 1) times; each lower
// level is seeded with the outputs of the level above. The game caches each
// level's state between calls; the result does not depend on that.
class Chain {
public:
    Chain() = default;
    Chain(std::uint16_t key_a, std::uint16_t key_b) : key_a_(key_a), key_b_(key_b) {}

    // levels[0] is the finest level (the low digit), levels[2] the top one.
    std::array<ChainLevel, 3> at(std::uint16_t coordinate) const;

private:
    std::uint16_t key_a_{}, key_b_{};
};

// A platform cell (A $209206): bands 0 of u and of v are open space between
// the platforms; the other 3 x 3 cells of a block are a platform 1-4 high.
struct Platform {
    int height{};        // 0: no platform
    bool centre{};       // band 2 of both u and v (the block's middle)
    bool beacon{};       // the block's exact middle (u and v 40 within the block)
    bool marked{};       // the block the game has marked (see World::marked)
};

class World {
public:
    // A new game's world (A $2096B2 with the mode word 0): four key words
    // (the salt words at A $209814 XORed with the seed's shift register),
    // the terrain levels and a random start cell.
    World(std::uint32_t seed, const amiga::LoadedImage& landscape);

    const std::array<std::uint16_t, 4>& keys() const { return keys_; }
    const Chain& rows() const { return rows_; }
    const Chain& columns() const { return columns_; }

    // Terrain level clamps the coarse cells are built with ($209994,
    // $209998, $20999C, $2099A0): floor = r + $86, plain = r + $85, ceiling =
    // min(r + $91, $C5) from one draw r (0-63); alternate = 40-103 from the
    // next draw.
    int floor_level() const { return floor_; }
    int plain_level() const { return plain_; }
    int ceiling_level() const { return ceiling_; }
    int alternate_level() const { return alternate_; }

    // Where a new game starts ($209984, $209986).
    std::uint16_t start_x() const { return start_x_; }
    std::uint16_t start_y() const { return start_y_; }

    // The marked block ($209988/$20998A, block coordinates u / 64, v / 64);
    // a new game has none ($FFFF never matches).
    std::uint16_t marked_u{0xFFFF}, marked_v{0};

    Platform platform(std::uint16_t x, std::uint16_t y) const;

private:
    std::array<std::uint16_t, 4> keys_{};
    Chain rows_, columns_;
    int floor_{}, plain_{}, ceiling_{}, alternate_{};
    std::uint16_t start_x_{}, start_y_{};
};

} // namespace mindwalker::world
