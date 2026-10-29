#pragma once
#include "fa_18_interceptor_amiga/data/assets.hpp"
#include "fa_18_interceptor_amiga/game/scene.hpp"
#include <deque>
#include <string>
#include <vector>

// The screen-text runner ($1278EA, every frame; game.md "Screen texts",
// "The front end"): the queued blocks run one after another and stay on the
// screen. A line with flag bit 0 is typed, a character every `speed` frames
// with a click (spaces skipped at once with flag bit 1); without it the
// whole line appears in one frame. After each line a pause of `delay` x 4
// frames. Lines are 11 rows apart (12 in the main menu, $13AA2E). When the
// queue has run out the runner is idle ($13AAD0 = 1) and 50 frames later
// done (bit 7).
//
// The typing mode ($13AAD0 = 2) turns the first line of the next block into
// an input field: keys are echoed there (with a click) until Return, then
// the block goes on.
namespace interceptor::game {

class TextRunner {
public:
    explicit TextRunner(const data::Assets& assets) : assets_(assets) {}

    // A block by number, or a copy of one with its text changed.
    void queue(int block);
    void queue(data::TextBlock block);
    // $103462 with the screen cleared: nothing queued or shown.
    void clear();
    void set_line_spacing(int rows) { spacing_ = rows; }

    // One frame; true when a character was typed (the runner clicks).
    bool tick();
    bool idle() const { return current_ >= int(blocks_.size()) && queued_.empty(); }
    bool done() const { return idle() && idle_frames_ >= 50; }

    // The input field of the next block (at most `length` characters).
    void begin_input(int length);
    bool inputting() const { return input_ != Input::off; }
    // A typed character (letters, digits, space, punctuation of the font)
    // or Backspace; Return ends the field. True when it was taken.
    bool type(char c);
    void backspace();
    void enter();
    const std::string& input() const { return text_; }

    void draw(IndexedImage& image, int left) const;

private:
    struct Running {
        data::TextBlock block;
        int line = 0;
        std::size_t shown = 0;
        int wait = 0;
        bool pausing = false;
        bool finished() const { return line >= int(block.lines.size()); }
    };
    bool step(Running& r);

    enum class Input { off, armed, typing, ended };

    const data::Assets& assets_;
    std::vector<Running> blocks_;          // started, on the screen
    std::deque<data::TextBlock> queued_;   // not started yet
    int current_ = 0;
    int spacing_ = 11;
    int idle_frames_ = 0;
    Input input_ = Input::off;
    int input_length_ = 0;
    std::string text_;
};

} // namespace interceptor::game
