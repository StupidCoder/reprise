#include "fa_18_interceptor_amiga/game/replay.hpp"

namespace interceptor::game {

// The words as the original tests their bits: the high byte's bits 0-7
// are bits 0-7 here, the low byte's 8-15.
std::uint16_t Replay::key_word(std::size_t record, int word) const {
    const std::size_t at = record * 4 + std::size_t(word) * 2;
    if (at + 1 >= keys_.size()) return 0;
    return std::uint16_t(keys_[at] | keys_[at + 1] << 8);
}

Replay::Frame Replay::next() {
    Frame f;
    f.control = control_at(control_++);
    f.game = key_word(key_, 0);
    f.view = key_word(key_, 1);
    // The key records wrap at the end of their buffer (four times the
    // control bytes').
    if (++key_ >= controls_.size()) key_ = 0;
    for (;;) {
        // Three $FF control bytes next: the end.
        if (control_at(control_) == 0xFF && control_at(control_ + 1) == 0xFF && control_at(control_ + 2) == 0xFF) {
            f.ended = true;
            return f;
        }
        if (control_ < controls_.size()) return f;
        // Past the buffer: from the start again ($10E06E).
        control_ = 0;
        key_ = 1;
    }
}

} // namespace interceptor::game
