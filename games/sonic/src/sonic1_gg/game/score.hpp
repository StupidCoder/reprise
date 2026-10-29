#pragma once
#include "sonic1_gg/audio/sound.hpp"

namespace sonic1_gg {

// Adds points and awards extra lives at score thresholds (33AA-33D6).
//
// The score has eight decimal digits. `threshold` is the original packed-BCD
// byte D2F8 holding the next extra-life score in units of 10,000 points. It
// starts at 0x05 (50,000) and advances by 5 (every 50,000 points). Once the
// byte wraps to zero no further lives are awarded; values above the reachable
// range also block awards.
inline void award_points(int& score, int& lives, unsigned& threshold, int points,
                         std::vector<SoundEvent>& events) {
    score = (score + points) % 100000000;
    const unsigned ten_thousands = (score / 10000) % 100;
    const unsigned packed = (ten_thousands / 10) * 16 + ten_thousands % 10;
    if (!threshold || packed < threshold) {
        return;
    }
    const unsigned next = ((threshold >> 4) * 10 + (threshold & 15) + 5) % 100;
    threshold = (next / 10) * 16 + next % 10;
    lives = (lives + 1) & 255;
    events.push_back({SoundEvent::Kind::Effect, unsigned(Effect::ExtraLife)});
}

} // namespace sonic1_gg
