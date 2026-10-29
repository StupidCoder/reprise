#include "rock_n_roll_amiga/game/tile_animation.hpp"

namespace rocknroll::game {
namespace {

constexpr std::uint32_t slot_table = 0x6C08C; // 18 pointers to the animations
constexpr int slot_count = 18;
constexpr int slot_cell_51 = 17;              // $6C0D0: $7196A, or the empty $7197E in world 6
constexpr std::uint32_t bank = data::GraphicsBank::address;

} // namespace

TileTable::TileTable() {
    for (int v = 1; v <= 240; ++v) {
        const int index = v - 1;
        entries_[std::size_t(v)] = std::uint32_t(index / 20 * 3200 + index % 20 * 2);
    }
}

SlidePictures::SlidePictures(const data::Program& program) {
    across = program.u32(0x6B194);
    along = program.u32(0x6B198);
    for (std::uint32_t d = 0; d < 5; ++d)
        for (std::uint32_t f = 0; f < 8; ++f) {
            leaving[d][f] = program.u32(0x6B1B2 + d * 32 + f * 4);
            for (std::uint32_t k = 0; k < 2; ++k) entering[k][d][f] = program.u32(0x6B252 + k * 0x80 + d * 32 + f * 4);
        }
}

TileAnimator::TileAnimator(const data::Program& program) : slide_pictures_(program) {
    for (int i = 0; i < slot_count; ++i) {
        const std::uint32_t at = program.u32(slot_table + std::uint32_t(i) * 4);
        TileAnimation a;
        a.strip = program.u32(at);
        a.period = program.u16(at + 4);
        a.counter = program.u16(at + 6);
        a.cell = std::uint8_t(program.u16(at + 8));
        a.step = program.u16(at + 10);
        for (int f = 0; f <= (a.period - 1) >> 8; ++f) a.frames.push_back(program.u16(at + 12 + std::uint32_t(f) * 2));
        slots_.push_back(std::move(a));
    }
}

void TileAnimator::update(TileTable& tiles) {
    if (slots_.empty()) return;
    // $6C106: three slots, each counter + step (0 at the period); the frame
    // counter >> 8 points the cell's entry into the strip.
    for (int k = 0; k < 3; ++k) {
        cursor_ = cursor_ + 1 == slot_count ? 0 : cursor_ + 1;
        if (cursor_ == slot_cell_51 && !slot17_on_) continue;
        auto& a = slots_[std::size_t(cursor_)];
        std::uint16_t c = std::uint16_t(a.counter + a.step);
        if (std::int16_t(c) >= std::int16_t(a.period)) c = 0; // signed compare (BLT)
        a.counter = c;
        show(a, tiles);
    }
    follow(tiles);
}

void TileAnimator::show(const TileAnimation& a, TileTable& tiles) const {
    const std::size_t frame = std::min<std::size_t>(a.counter >> 8, a.frames.size() - 1);
    tiles.set(a.cell, a.strip - bank + a.frames[frame]);
}

void TileAnimator::show_all(TileTable& tiles) const {
    for (int i = 0; i < slot_count && std::size_t(i) < slots_.size(); ++i)
        if (i != slot_cell_51 || slot17_on_) show(slots_[std::size_t(i)], tiles);
    follow(tiles);
}

void TileAnimator::follow(TileTable& tiles) const {
    // $6C004: pictures that follow others.
    const auto entry = [&](std::uint8_t v) { return tiles.offset(v); };
    tiles.set(0xBC, entry(0xB9) + 6); // three tiles to the right of $B9's frame
    if (world_ != 6) {               // $51's 2 x 2 picture: its pieces' strips 64 pixels apart
        tiles.set(0x52, entry(0x51) + 8);
        tiles.set(0x65, entry(0x51) + 16);
        tiles.set(0x66, entry(0x51) + 24);
    }
    // The coins $12/$13 show $11's frame; while $11's counter is 0 the three
    // show their own sheet pictures.
    tiles.set(0x12, entry(0x11));
    tiles.set(0x13, entry(0x11));
    if (counter(0x11) == 0) {
        tiles.set(0x11, 0x20);
        tiles.set(0x12, 0x22);
        tiles.set(0x13, 0x24);
    }
}

std::uint16_t TileAnimator::counter(std::uint8_t cell) const {
    // The last slot with the cell ($11's own animation is slot 16, $715D4).
    for (auto it = slots_.rbegin(); it != slots_.rend(); ++it)
        if (it->cell == cell) return it->counter;
    return 0;
}

void TileAnimator::set_counter(std::uint8_t cell, std::uint16_t value) {
    for (auto it = slots_.rbegin(); it != slots_.rend(); ++it)
        if (it->cell == cell) {
            it->counter = value;
            return;
        }
}

} // namespace rocknroll::game
