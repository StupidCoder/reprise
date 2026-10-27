#pragma once
#include "amiga/hunk.hpp"
#include "mindwalker_amiga/screen/canvas.hpp"
#include <array>
#include <cstdint>
#include <vector>

// The landscape's picture sequences, played while A's loop stands still (no
// input is read): the catch picture (h14, $2039B8: a creature's face in the
// sprite colours, turned to cyan, stamped frame after frame; after a lost
// life the playfield colours fade to black) and the reveal that follows it
// and opens every stage (h32, $206728, the paint's mode 2: the old picture
// slides to the four corners while the new view grows from the middle).
namespace mindwalker::landscape {

// The display as these sequences change it: display planes 0-3 (as pens 0-15,
// the bar included), display plane 4 (the tether layer) and the colours.
struct Display {
    screen::Canvas planes;
    std::vector<std::uint8_t> plane4 = std::vector<std::uint8_t>(320 * 200);
    std::array<std::uint16_t, 32> colours{};
};

class Interlude {
public:
    explicit Interlude(const amiga::LoadedImage& landscape);

    bool active() const { return phase_ != Phase::idle; }
    // The catch picture from `now`, then the reveal of `next` (the new
    // display: drawing, bar and colours; its plane 4 empty). `damage` is the
    // damage after the catch; `lost` whether it cost a life.
    void start_catch(const Display& now, int damage, bool lost, const Display& next);
    // The reveal of `next` over `now` ($201492 mode 2).
    void start_reveal(const Display& now, const Display& next);
    // One display frame on.
    void advance();
    const Display& display() const { return display_; }

private:
    enum class Phase { idle, face, fade, fade_tail, paint, reveal, settle };
    void start_paint();
    void face_step(int step);
    void fade_step();
    void reveal_step(int step);

    const amiga::LoadedImage& a_;
    Display display_;
    Display next_;
    Phase phase_{Phase::idle};
    int step_{}, frames_left_{}, level_{};
    bool lost_{};
    std::array<std::uint16_t, 16> fading_{}; // h10: colours 0-15 on their way to black
};

} // namespace mindwalker::landscape
