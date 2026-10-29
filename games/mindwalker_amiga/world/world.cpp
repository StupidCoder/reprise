#include "mindwalker_amiga/world/world.hpp"
#include "mindwalker_amiga/world/random.hpp"
#include <algorithm>

namespace mindwalker::world {

std::array<ChainLevel, 3> Chain::at(std::uint16_t coordinate) const {
    std::array<ChainLevel, 3> levels{};
    ChainLevel seed{key_a_, key_b_};
    for (int level = 2; level >= 0; --level) {
        const int digit = coordinate >> (level * 4) & 15;
        auto mixer = Mixer::seeded(seed.a, seed.b);
        ChainLevel out{};
        for (int i = 0; i <= digit; ++i) {
            out.a = mixer.step();
            out.b = mixer.step();
        }
        levels[std::size_t(level)] = out;
        seed = out;
    }
    return levels;
}

World::World(std::uint32_t seed, const amiga::LoadedImage& landscape) {
    constexpr std::uint32_t salt = 0x209814; // A h45: four words
    ShiftRegister random{seed};
    for (std::uint32_t i = 0; i < 4; ++i) {
        keys_[i] = std::uint16_t(landscape.u16(salt + i * 2) ^ random.next());
    }
    rows_ = Chain(keys_[0], keys_[1]);
    columns_ = Chain(keys_[2], keys_[3]);
    const int r = random.next() & 0x3F;
    floor_ = r + 0x86;
    plain_ = r + 0x85;
    ceiling_ = std::min(r + 0x91, 0xC5);
    alternate_ = (random.next() >> 2 & 0x3F) + 0x28;
    start_x_ = random.next() & 0xFFF;
    start_y_ = random.next() >> 2 & 0xFFF;
}

Platform World::platform(std::uint16_t x, std::uint16_t y) const {
    const int u = (x + y) & 0xFFF, v = (x - y) & 0xFFF;
    const int band_u = u >> 4 & 3, band_v = v >> 4 & 3;
    Platform cell;
    if (band_u == 0 || band_v == 0) {
        return cell;
    }
    // 16-bit shifts of the key words by the block coordinates.
    int h = std::uint16_t(keys_[1] >> (u >> 6 & 15) ^ keys_[0] ^ keys_[3] >> (v >> 6 & 15));
    if (u & 0x400) {
        h ^= v >> 10;
    }
    cell.height = (h & 3) + 1;
    cell.centre = band_u == 2 && band_v == 2;
    cell.beacon = cell.centre && (u & 63) == 40 && (v & 63) == 40;
    cell.marked = (u >> 6) == marked_u && (v >> 6) == marked_v;
    return cell;
}

} // namespace mindwalker::world
