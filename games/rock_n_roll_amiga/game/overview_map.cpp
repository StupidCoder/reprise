#include "rock_n_roll_amiga/game/overview_map.hpp"

namespace rocknroll::game {
namespace {

constexpr std::uint32_t ranges = 0x77E7D;      // pairs: first value, count; $FF ends
constexpr std::uint32_t range_colours = 0x77EF2; // a colour per range, copied when revealed
constexpr std::uint32_t reveal_order = 0x77F1A;  // the range each piece reveals
constexpr std::uint32_t depth_text = 0x68142;    // "CREATING MAP WITH DEPTH:" (1, 0)
constexpr std::uint32_t digits_text = 0x68160;   // two digits (26, 0)

} // namespace

OverviewColours::OverviewColours(const data::Program& program, int depth) {
    // $77ECA: cleared at a new game; piece n sets range order[n − 1].
    std::array<std::uint8_t, 64> shown{};
    for (int n = 1; n < depth && n <= 33; ++n) {
        const int range = program.u8(reveal_order + std::uint32_t(n - 1));
        shown[std::size_t(range)] = program.u8(range_colours + std::uint32_t(range));
    }
    std::uint32_t at = ranges;
    for (std::size_t i = 0; program.u8(at) != 0xFF && i < shown.size(); ++i, at += 2)
        ranges_.push_back({program.u8(at), program.u8(at + 1), shown[i]});
}

std::uint8_t OverviewColours::colour(std::uint8_t value, int column) const {
    std::uint8_t result = 0;
    int looking_for = value;
    for (const auto& r : ranges_) {
        const int offset = (looking_for - r.first) & 0xFF; // byte arithmetic, as the compare loop counts
        if (offset >= r.count) continue;
        result |= r.colour;
        looking_for = 7 - column % 8; // D0 now holds the bit number
    }
    return result;
}

std::vector<ScreenText> overview_texts(const data::Program& program, int depth) {
    auto digits = read_text(program, digits_text);
    digits.text[2] = char('0' + depth / 10 % 10);
    digits.text[3] = char('0' + depth % 10);
    return {read_text(program, depth_text), digits};
}

OverviewOrigin overview_origin(int columns, int rows) {
    const int bytes = ((240 - columns) >> 1) / 8;
    return {32 + bytes * 8, ((112 - rows) >> 1) + 8};
}

} // namespace rocknroll::game
