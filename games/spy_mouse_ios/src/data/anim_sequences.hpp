// Animation sequences (res/anim_sequences/*.xml.brd), read as Sprite::addAnimationSequence
// @0x1320d0 does: <Animation image frameWidth frameHeight> with named <Sequence numLoops> of
// <Frame cx cy delay ox oy> (delay in seconds, kept as whole milliseconds).
#pragma once

#include "data/game_files.hpp"

#include <string>
#include <vector>

namespace doc::data {

struct SequenceFrame {
    int column = 0, row = 0;  // cell in the sheet (cx, cy)
    int delayMs = 0;
    float offsetX = 0, offsetY = 0;
    // Quick saves (state_io.hpp).
    template <class Io>
    void io(Io& io) { io(column, row, delayMs, offsetX, offsetY); }
};

struct AnimSequence {
    std::string name;
    std::string image;
    int frameWidth = 0, frameHeight = 0;
    int loops = 1;  // numLoops
    std::vector<SequenceFrame> frames;
    // Quick saves (state_io.hpp).
    template <class Io>
    void io(Io& io) { io(name, image, frameWidth, frameHeight, loops, frames); }
};

// All sequences of one file, in file order; empty if the file is missing or not version 1.
std::vector<AnimSequence> loadAnimSequences(const GameFiles& files, const std::string& relative);

}  // namespace doc::data
