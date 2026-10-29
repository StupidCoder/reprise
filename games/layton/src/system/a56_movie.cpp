// Movies as script resources. See a56_movie.hpp.
#include "system/a56_movie.hpp"
#include <cmath>
#include <utility>

namespace picarat::adventure56 {

MoviePlayer::MoviePlayer(std::vector<std::uint8_t> file, moflex::Tables tables)
    : movie_(std::move(file), std::move(tables)), picture_(std::make_shared<ctr3d::Texture>()) {
    const auto& v = movie_.video();
    picture_->width = int(v.width);
    picture_->height = int(v.height);
    picture_->pixels.assign(std::size_t(v.width) * v.height, platform::Color{0, 0, 0, 255});
    picture_->linear = false;
}

void MoviePlayer::start() {
    started_ = true;
    ended_ = false;
    ticks_ = 0;
    if (shown_ < 0) {
        showNext();
    }
}

bool MoviePlayer::showNext() {
    // A stereo movie's pictures alternate between the eyes (layout 0: the
    // left one first); the left one is shown.
    unsigned eye = 0;
    std::int64_t time = 0;
    for (;;) {
        if (!movie_.next(rgba_, eye, time)) {
            exhausted_ = true;
            return false;
        }
        if (eye == 0) {
            break;
        }
    }
    ++shown_;
    auto& px = picture_->pixels;
    for (std::size_t i = 0; i < px.size() && i * 4 + 3 < rgba_.size(); ++i) {
        px[i] = platform::Color{rgba_[i * 4], rgba_[i * 4 + 1], rgba_[i * 4 + 2], 255};
    }
    return true;
}

void MoviePlayer::tick(float displayFrames) {
    if (!started_ || ended_) {
        return;
    }
    ticks_ += displayFrames;
    // 0x382098: the pair the clock wants; the view catches up picture by
    // picture (0x2dd39c skips ahead when more than two behind; native
    // playback never falls behind).
    long wanted = long(std::floor(double(ticks_) * movie_.rate() / kDisplayRate));
    while (shown_ < wanted && !exhausted_) {
        showNext();
        if (exhausted_ && looping_ && shown_ >= 0) {
            // Played through: from the first picture again, the clock
            // taken back by one pass.
            ticks_ -= float(double(shown_ + 1) * kDisplayRate / movie_.rate());
            wanted -= shown_ + 1;
            movie_.rewind();
            exhausted_ = false;
            shown_ = -1;
        }
    }
    if (exhausted_ && wanted > shown_) {
        ended_ = true;
    }
}

} // namespace picarat::adventure56
