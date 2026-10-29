#include "game/sprite_anim.hpp"

namespace doc::game {

void SpriteAnim::setSheet(int sheetWidth, int sheetHeight, float frameWidth) {
    frameHeight_ = float(sheetHeight);
    if (frameWidth >= 0.0f) {
        frames_ = int(float(sheetWidth) / frameWidth + 0.5f);
        frameWidth_ = frameWidth;
        mode_ = Hold;
    } else {
        frames_ = 1;
        frameWidth_ = float(sheetWidth);
        mode_ = Still;
    }
    first_ = last_ = 0;
    frameMs_ = 250;
    frame_ = 0;
    timerMs_ = 0;
}

void SpriteAnim::setAnim(int first, int last, int mode, int frameMs) {
    first_ = first;
    last_ = last;
    mode_ = mode;
    frameMs_ = frameMs;
    frame_ = first;
    timerMs_ = 0;
    step_ = first > last ? -1 : 1;
    paused_ = false;
    finished_ = false;
    oneShot_ = false;
    sequenceActive_ = false;
}

void SpriteAnim::playOneShot(int first, int last, int frameMs) {
    saved_ = {first_, last_, mode_, frameMs_, frame_, timerMs_, step_};
    first_ = first;
    frame_ = first;
    last_ = last;
    mode_ = Once;
    frameMs_ = frameMs;
    timerMs_ = 0;
    step_ = first > last ? -1 : 1;
    finished_ = false;
    oneShot_ = true;
    paused_ = false;
}

void SpriteAnim::update(int dtMs) {
    if (paused_) return;
    if (sequenceActive_) {
        updateSequence(dtMs);
        return;
    }
    if (mode_ < Once) return;
    timerMs_ += dtMs;
    if (timerMs_ < frameMs_) return;
    timerMs_ -= frameMs_;
    frame_ += step_;
    const bool pingPong = mode_ == PingPong || mode_ == PingPongOnce;
    // Still inside the range (which runs downward when the step is negative)?
    if (step_ > 0 || pingPong) {
        if (frame_ >= first_ && frame_ <= last_) return;
    } else if (frame_ <= first_ && frame_ >= last_) {
        return;
    }
    if (pingPong) {
        if (mode_ == PingPongOnce && step_ == -1) {
            frame_ = last_;
            mode_ = Hold;
            finished_ = true;
            return;
        }
        frame_ = frame_ - 2 * step_;
        step_ = -step_;
        return;
    }
    if (mode_ == Loop) {
        frame_ = first_;
        return;
    }
    if (oneShot_) {
        // The one-shot ends: back to the animation it interrupted.
        oneShot_ = false;
        finished_ = false;
        first_ = saved_.first;
        last_ = saved_.last;
        mode_ = saved_.mode;
        frameMs_ = saved_.frameMs;
        frame_ = saved_.frame;
        timerMs_ = saved_.timerMs;
        step_ = saved_.step;
        return;
    }
    frame_ = last_;
    mode_ = Hold;
    finished_ = true;
}

int SpriteAnim::addSequence(const data::AnimSequence& sequence) {
    SequenceState state;
    state.data = sequence;
    sequences_.push_back(std::move(state));
    return int(sequences_.size()) - 1;
}

int SpriteAnim::findSequence(const std::string& name) const {
    for (std::size_t i = 0; i < sequences_.size(); ++i)
        if (sequences_[i].data.name == name) return int(i);
    return -1;
}

const data::AnimSequence* SpriteAnim::sequence(int index) const {
    return index >= 0 && index < int(sequences_.size()) ? &sequences_[std::size_t(index)].data : nullptr;
}

int SpriteAnim::sequenceFrame() const { return sequenceActive_ ? sequences_[std::size_t(sequence_)].frame : 0; }
int SpriteAnim::sequenceRow() const { return sequenceActive_ ? sequences_[std::size_t(sequence_)].row : 0; }

void SpriteAnim::playSequence(int index, int mode, int row) {
    if (index < 0 || index >= int(sequences_.size())) return;
    if (sequence_ == index && sequenceActive_) return;
    sequence_ = index;
    SequenceState& s = sequences_[std::size_t(index)];
    s.frame = 0;
    s.row = row;
    s.timerMs = 0;
    s.mode = mode;
    s.finished = false;
    sequenceActive_ = true;
}

void SpriteAnim::updateSequence(int dtMs) {
    SequenceState& s = sequences_[std::size_t(sequence_)];
    s.finished = false;
    s.timerMs += dtMs;
    if (s.data.frames.empty()) return;
    // A frame ends once its delay has been exceeded; the next starts from zero.
    if (s.data.frames[std::size_t(s.frame)].delayMs >= s.timerMs) return;
    if (++s.frame >= int(s.data.frames.size())) {
        if (s.mode == SequenceOnce) {
            if (++s.loopsDone < s.data.loops) {
                s.frame = 0;
            } else {
                // Done: back to the frame animation (the last frame index stays).
                s.loopsDone = 0;
                s.finished = true;
                sequenceActive_ = false;
            }
        } else if (s.mode == SequenceLoop) {
            s.frame = 0;
        }
    }
    s.timerMs = 0;
}

}  // namespace doc::game
