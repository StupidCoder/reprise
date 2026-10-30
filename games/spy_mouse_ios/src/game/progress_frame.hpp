// A GUI frame that eases in and out (GuiProgressFrame, 1.1.1 @0x20dca4..@0x20dfa4): its
// progress runs from 0 (out) to 1 (in). Slide frames (GuiSlideFrame @0x20e3c4) move by it:
// direction × (1 − progress) × their size.
#pragma once

namespace doc::game {

class ProgressFrame {
public:
    // How progress moves (+0xa4 going in, +0xa8 going out; GuiProgressFrame::updateProgress
    // @0x20de88): Linear at rate × 0.001 per ms; EaseIn speeds up away from where it started,
    // EaseOut slows towards where it goes (both by rate × 0.002 per ms of the distance, at
    // least 0.001).
    enum class Mode { Linear = 0, EaseIn = 1, EaseOut = 2 };

    ProgressFrame() = default;
    ProgressFrame(float rate, Mode in, Mode out) : rate_(rate), modeIn_(in), modeOut_(out) {}

    // GuiProgressFrame::update @0x20dfa4 (the progress part).
    void update(int dtMs);
    // goIn @0x20dd2c / goOut @0x20dd78: false when already there.
    bool goIn();
    bool goOut();
    // setIn @0x20dcfc / setOut @0x20dd18: there at once.
    void setIn() { in_ = true, moving_ = false, progress_ = 1.0f; }
    void setOut() { in_ = false, moving_ = false, progress_ = 0.0f; }

    float progress() const { return progress_; }
    bool goingIn() const { return in_; }
    // isAnimatingIn @0x20de50.
    bool animatingIn() const { return moving_ && in_; }
    // finishedAnim @0x20ddec: still, and all the way in or out.
    bool finished() const {
        if (moving_) return false;
        return in_ ? progress_ >= 0.999899983f : progress_ <= 9.99999975e-05f;
    }
    // SceneGame::getPowerupsVisible @0xf7294: moving in, or not all the way out.
    bool showing() const { return animatingIn() || progress_ > 0.0f; }

private:
    float progress_ = 0.0f;  // +0x9c
    bool in_ = true;         // +0x94
    bool moving_ = true;     // +0x95
    int outPhase_ = 0;       // +0x98: an out-frame reports itself done one update later
    float rate_ = 1.0f;      // +0xa0
    Mode modeIn_ = Mode::Linear, modeOut_ = Mode::Linear;
public:
    // Quick saves (state_io.hpp). Not saved: rate_, modeIn_ and modeOut_ (set where the
    // frame is made).
    template <class Io>
    void io(Io& io) { io(progress_, in_, moving_, outPhase_); }
};

}  // namespace doc::game
