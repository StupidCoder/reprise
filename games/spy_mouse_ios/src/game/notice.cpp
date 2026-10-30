#include "game/notice.hpp"

namespace doc::game {

void PosedMouse::setPose(int pose, bool flip, GameRandom& random) {
    pose_ = pose < 0 ? 0 : pose;
    flip_ = flip;
    timerMs_ = 0;
    nextMs_ = 120;
    blinkMs_ = 0;
    looks_ = 0;
    looksBeforeChange_ = int(random.next() % 5);
    eyes_ = 1;
}

void PosedMouse::update(int dtMs, GameRandom& random, const EyeMapping& eyes) {
    timerMs_ += dtMs;
    if (timerMs_ <= nextMs_) return;
    // Time for a blink: the next one comes 0.25..2.85 s later and lasts 50..169 ms.
    timerMs_ = 0;
    nextMs_ = int(random.next() % 2600) + 250;
    blinkMs_ = int(random.next() % 120) + 50;
    eyes_ = 1;
    // After a few blinks the mouse may look somewhere else (eye pictures the pose has).
    if (++looks_ <= looksBeforeChange_) return;
    looks_ = 0;
    looksBeforeChange_ = int(random.next() % 2);
    const int look = int(random.next() % 4);
    const std::size_t at = std::size_t(look + pose_ * 5);
    eyes_ = at < eyes.size() && eyes[at] != 0 ? look : 1;
}

void NoticePopup::open(const LevelNotice& notice) {
    notice_ = notice;
    card_ = BounceFrame{};
    updates_ = 0;
    active_ = true;
}

void NoticePopup::update(int dtMs) {
    if (!active_) return;
    ++updates_;
    card_.update(dtMs);
}

}  // namespace doc::game
