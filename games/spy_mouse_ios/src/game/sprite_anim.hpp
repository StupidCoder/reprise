// Frame animation of a sprite sheet (one row of equal frames), as the original's Sprite class
// runs it (Sprite::setImage @0x1328c4, setAnim @0x131ecc, playOneShotAnim @0x13186c,
// update @0x131bb4), and the data-driven sequences a sprite can play instead
// (playAnimSequence @0x131970, update's sequence part).
#pragma once

#include "data/anim_sequences.hpp"

#include <vector>

namespace doc::game {

struct SheetSize {
    int width = 0, height = 0;
    // Quick saves (state_io.hpp).
    template <class Io>
    void io(Io& io) { io(width, height); }
};

class SpriteAnim {
public:
    enum Mode : int {
        Still = 0,     // no animation (sheet without frame width)
        Hold = 1,      // shows `frame`, finished one-shots end here
        Once = 2,      // plays first..last once, then holds the last frame
        Loop = 3,      // first..last, again and again
        PingPong = 4,  // first..last..first..
        PingPongOnce = 5,  // first..last..first, then holds
    };

    // A sheet cut into frames `frameWidth` wide (negative: one frame, no animation). Resets to
    // frame 0, held, 250 ms per frame.
    void setSheet(int sheetWidth, int sheetHeight, float frameWidth);
    int frameCount() const { return frames_; }
    float frameWidth() const { return frameWidth_; }
    float frameHeight() const { return frameHeight_; }

    // Also ends a running sequence and unpauses.
    void setAnim(int first, int last, int mode, int frameMs);
    // Plays first..last once, then returns to the animation that was running.
    void playOneShot(int first, int last, int frameMs);
    void pause(bool paused) { paused_ = paused; }
    bool paused() const { return paused_; }
    // The current animation's time per frame (Sprite +0x60), changed in place.
    void setFrameMs(int frameMs) { frameMs_ = frameMs; }
    void update(int dtMs);

    int frame() const { return frame_; }
    void setFrame(int frame) { frame_ = frame; }  // (a snapshot's rollback)
    int first() const { return first_; }  // Sprite +0x54: the animation's first frame
    int mode() const { return mode_; }
    bool finished() const { return finished_; }
    bool playingOneShot() const { return oneShot_; }  // Sprite +0x70

    // Sequences: added in order, played by index. While one plays the frame animation waits.
    enum SequenceMode : int { SequenceOnce = 0, SequenceLoop = 1 };  // Once: numLoops times
    int addSequence(const data::AnimSequence& sequence);
    int findSequence(const std::string& name) const;  // -1 if missing
    // Restarts only if another sequence (or none) is playing. `row` is passed along for
    // drawing (the facing row of direction-aware sequences).
    void playSequence(int index, int mode, int row);
    int currentSequence() const { return sequenceActive_ ? sequence_ : -1; }
    void stopSequence() { sequenceActive_ = false; }  // Sprite::stopSequence @0x131b1c
    const data::AnimSequence* sequence(int index) const;
    int sequenceFrame() const;  // frame of the current sequence
    int sequenceRow() const;

private:
    struct State {
        int first = 0, last = 0, mode = Hold, frameMs = 250, frame = 0, timerMs = 0, step = 1;
        // Quick saves (state_io.hpp).
        template <class Io>
        void io(Io& io) { io(first, last, mode, frameMs, frame, timerMs, step); }
    };
    int frames_ = 1;
    float frameWidth_ = 0.0f, frameHeight_ = 0.0f;
    int first_ = 0, last_ = 0, mode_ = Hold, frameMs_ = 250, frame_ = 0, timerMs_ = 0, step_ = 1;
    bool paused_ = false, finished_ = false, oneShot_ = false;
    State saved_;

    struct SequenceState {
        data::AnimSequence data;
        int frame = 0, row = 0, timerMs = 0, loopsDone = 0, mode = SequenceOnce;
        bool finished = false;
        // Quick saves (state_io.hpp).
        template <class Io>
        void io(Io& io) { io(data, frame, row, timerMs, loopsDone, mode, finished); }
    };
    std::vector<SequenceState> sequences_;
    int sequence_ = -1;
    bool sequenceActive_ = false;
    void updateSequence(int dtMs);
public:
    // Quick saves (state_io.hpp).
    template <class Io>
    void io(Io& io) { io(frames_, frameWidth_, frameHeight_, first_, last_, mode_, frameMs_, frame_, timerMs_,
                           step_, paused_, finished_, oneShot_, saved_, sequences_, sequence_,
                           sequenceActive_); }
};

}  // namespace doc::game
