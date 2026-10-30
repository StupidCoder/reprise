#include "game/progress_frame.hpp"

#include <algorithm>
#include <cmath>

namespace doc::game {

void ProgressFrame::update(int dtMs) {
    if (moving_) {
        // updateProgress @0x20de88.
        const float scaled = float(dtMs) * rate_;
        const float k = scaled * 0.00200000009f;
        const float target = in_ ? 1.0f : 0.0f;
        const Mode mode = in_ ? modeIn_ : modeOut_;
        float step;
        if (mode == Mode::EaseIn) {
            const float from = in_ ? 0.0f : 1.0f;
            step = std::max(0.00100000005f, std::fabs(progress_ - from) * k);
        } else if (mode == Mode::EaseOut) {
            step = std::max(0.00100000005f, std::fabs(target - progress_) * k);
        } else {
            step = scaled * 0.00100000005f;
        }
        if (target <= progress_) progress_ = std::max(progress_ - step, target);
        else progress_ = std::min(progress_ + step, target);

        if (!in_) {
            if (progress_ <= 0.0f) {
                progress_ = 0.0f;
                if (outPhase_ == 1) {
                    outPhase_ = 0;
                    moving_ = false;
                } else {
                    outPhase_ = 1;
                }
            }
        } else if (progress_ >= 1.0f) {
            progress_ = 1.0f;
            moving_ = false;
        }
    }
    progress_ = std::clamp(progress_, 0.0f, 1.0f);
}

bool ProgressFrame::goIn() {
    if (progress_ >= 0.999899983f && !moving_) return false;
    outPhase_ = 0;
    moving_ = true;
    in_ = true;
    return true;
}

bool ProgressFrame::goOut() {
    if (!(progress_ > 9.99999975e-05f || moving_)) return false;
    moving_ = true;
    in_ = false;
    return true;
}

}  // namespace doc::game
