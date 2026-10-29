#include "data/anim_sequences.hpp"

#include "data/brd.hpp"

namespace doc::data {

std::vector<AnimSequence> loadAnimSequences(const GameFiles& files, const std::string& relative) {
    std::vector<AnimSequence> out;
    auto root = loadChunkTree(files, relative);
    if (!root || root->getInt("version").value_or(0) != 1) return out;
    const std::string image = root->getString("image").value_or("");
    const int frameWidth = root->getInt("frameWidth").value_or(0);
    const int frameHeight = root->getInt("frameHeight").value_or(0);
    for (const BrdNode* s : root->childrenNamed("Sequence")) {
        AnimSequence seq;
        seq.name = s->getString("name").value_or("");
        seq.image = image;
        seq.frameWidth = frameWidth;
        seq.frameHeight = frameHeight;
        seq.loops = s->getInt("numLoops").value_or(1);
        for (const BrdNode* f : s->childrenNamed("Frame")) {
            SequenceFrame frame;
            frame.column = f->getInt("cx").value_or(0);
            frame.row = f->getInt("cy").value_or(0);
            frame.delayMs = int(f->getFloat("delay").value_or(0.0f) * 1000.0f);
            frame.offsetX = f->getFloat("ox").value_or(0.0f);
            frame.offsetY = f->getFloat("oy").value_or(0.0f);
            seq.frames.push_back(frame);
        }
        out.push_back(std::move(seq));
    }
    return out;
}

}  // namespace doc::data
