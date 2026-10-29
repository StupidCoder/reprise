#pragma once
#include "fa_18_interceptor_amiga/data/assets.hpp"
#include "fa_18_interceptor_amiga/game/scene.hpp"
#include <vector>

// A screen-text block typed out as the text runner does ($127920, per
// frame; game.md "Screen texts"): a character every `speed` frames,
// each with a click, spaces skipped at once in lines with flag bit 1, a
// pause of `delay` x 4 frames after each line. Drawn in the menu font at
// the block's place, lines 11 rows apart.
namespace interceptor::game {

class TextScreen {
public:
    TextScreen(const data::Assets& assets, int block);

    // One frame; true when a character was typed (the runner clicks).
    bool tick();
    bool finished() const { return line_ >= lines(); }
    void draw(IndexedImage& image, int left) const;

private:
    int lines() const { return block_ ? int(block_->lines.size()) : 0; }
    // Moves past spaces the line skips; false at the line's end.
    bool next_character();

    const data::Assets& assets_;
    const data::TextBlock* block_{};
    int line_ = 0;            // the line being typed
    std::size_t shown_ = 0;   // its characters drawn so far
    int wait_ = 0;            // frames to the next step
    bool pausing_ = false;    // after the line's end
};

} // namespace interceptor::game
