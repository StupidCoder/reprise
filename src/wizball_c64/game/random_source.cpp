#include "wizball_c64/game/random_source.hpp"

namespace wizball::game {

GameRandom::GameRandom(const data::GameImage& image) {
    const auto bytes = image.bytes(0x7300, 256);
    for (std::size_t i = 0; i < 256; ++i) table_[i] = bytes[i];
    counter_ = image.byte(0x7532);
}

std::uint8_t GameRandom::next() {
    ++counter_;
    // A call costs a few hundred cycles of other work in between; vary it.
    drift_ = drift_ * 1103515245u + 12345u;
    timer_ = (timer_ - 120 - (drift_ >> 16) % 900) & 0xFFFF;
    return std::uint8_t(table_[counter_] ^ (timer_ & 0xFF));
}

} // namespace wizball::game
