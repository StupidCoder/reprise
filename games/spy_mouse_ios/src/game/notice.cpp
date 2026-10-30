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

void NoticePopup::open(const LevelNotice& notice, GameRandom& random) {
    notice_ = notice;
    mouse_ = PosedMouse{};
    mouse_.setPose(1, false, random);  // GuiPosedMouse constructor
    mouse_.setPose(notice.pose, notice.flip, random);  // ScenePopupTutorial::load
    active_ = true;
    closing_ = false;
    alpha_ = 1.0f;
}

void NoticePopup::update(int dtMs, GameRandom& random, const PosedMouse::EyeMapping& eyes) {
    if (!active_) return;
    ++updates_;  // Scene::update: the items, the scroll frame among them
    mouse_.update(dtMs, random, eyes);
    if (!closing_) return;
    alpha_ += float(dtMs) * -0.00700000022f;
    if (alpha_ <= 0.0f) active_ = false;  // Engine::popScene
}

}  // namespace doc::game
