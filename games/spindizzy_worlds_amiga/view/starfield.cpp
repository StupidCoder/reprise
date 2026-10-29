#include "spindizzy_worlds_amiga/view/starfield.hpp"

namespace spindizzy::view {
namespace {

constexpr std::uint32_t clear_masks = 0x15B96; // 16 words, by x & 15
constexpr int screen_offset = 16;              // $8(a5) = 2 bytes into the buffer

} // namespace

std::uint16_t StarRandom::next() {
    // mulu.w #$F1 then addi.w #$29: the add does not carry into the high word.
    std::uint32_t product = (state_ & 0xFFFF) * 0xF1;
    product = (product & 0xFFFF0000) | ((product + 0x29) & 0xFFFF);
    state_ = product;
    return std::uint16_t((product & 0xFFFF) + (product >> 16));
}

std::vector<Star> make_stars(const data::Memory& main, int count, StarRandom& random) {
    std::vector<Star> stars;
    for (int i = 0; i < count; ++i) {
        int x, y;
        do x = random.next() & 0x1FF; while (x >= 0x120);
        do y = random.next() & 0xFF; while (y >= 0xA8);
        const int bit = x & 15; // counted from the word's right end
        stars.push_back({screen_offset + (x & 0x1F0) + 15 - bit, y, main.u16(clear_masks + std::uint32_t(bit) * 2)});
    }
    return stars;
}

std::vector<Star> make_stars(const data::Memory& main, const world::PlacedObject& object, StarRandom& running) {
    const int count = object.parameters.empty() ? 0 : object.parameters[0];
    const int seed = object.parameters.size() > 1 ? object.parameters[1] : 0;
    if (count == 0) return {}; // $15AFE: no stars, no object
    if (seed == 0) return make_stars(main, count, running);
    const auto both = std::uint32_t(seed); // plus the level's rotation (0 as built)
    StarRandom seeded(both << 16 | both);
    return make_stars(main, count, seeded);
}

void draw_stars(Canvas& canvas, const std::vector<Star>& stars, int origin_x) {
    for (const auto& star : stars) {
        const int word_left = star.x - (star.x - screen_offset) % 16 + origin_x; // the 16-pixel word it sits in
        bool clear = true;
        for (int b = 0; b < 16 && clear; ++b) {
            if (!(star.clear >> b & 1)) continue;
            const int x = word_left + 15 - b;
            if (x < 0 || x >= canvas.width || star.y >= canvas.height) continue;
            clear = canvas.at(x, star.y) == 0;
        }
        const int x = star.x + origin_x;
        if (clear && x >= 0 && x < canvas.width && star.y < canvas.height) {
            canvas.pixels[std::size_t(star.y * canvas.width + x)] = 3;
        }
    }
}

} // namespace spindizzy::view
