#pragma once
#include "amiga/hunk.hpp"
#include "mindwalker_amiga/screen/canvas.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <vector>

// The landscape's picture sequences, played while A's loop stands still (no
// input is read): the catch picture (h14, $2039B8: a creature's face in the
// sprite colours, turned to cyan, stamped frame after frame; after a lost
// life the playfield colours fade to black) and the reveal that follows it
// and opens every stage (h32, $206728, the paint's mode 2: the old picture
// slides to the four corners while the new view grows from the middle) —
// and the other repaints of the view, which take the A500 about 40 frames
// each: the view change (h8 clears the old picture a plane per row and
// blacks out a colour), the plain one (the compass), and $201602's (the
// background flashing through four colours per row; with a star growing
// around the event's cell for beacons and creatures' places).
namespace mindwalker::landscape {

// The display as these sequences change it: display planes 0-3 (as pens 0-15,
// the bar included), display plane 4 (the tether layer) and the colours.
struct Display {
    screen::Canvas planes;
    std::vector<std::uint8_t> plane4 = std::vector<std::uint8_t>(320 * 200);
    std::array<std::uint16_t, 32> colours{};
    template <class A> void persist(A &ar) { ar(planes, plane4, colours); }
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
    // A repaint of the view from `now` to `next`, the old picture kept
    // until the new one is copied; `star_x/y` the star's middle (display
    // rows).
    // `grid` draws the grid and score onto the display (h8 does so after each
    // plane a repaint clears); passed on each call, not kept, so that an
    // interlude holds no pointer back into its stage (a stage can be copied).
    using Grid = std::function<void(screen::Canvas&)>;
    enum class Repaint { plain, view_change, flash, star };
    void start_repaint(const Display& now, const Display& next, Repaint, int repeats = 1, int star_x = 0, int star_y = 0,
                       const Grid& grid = {});
    // $205EC4, the moment of a catch (before the catch picture, in the same
    // frame): 24 steps of 2 frames, the catchers' tethers (`tethers`, plane
    // 4) reaching the walker, colour 0 flashing green, colours 19-31 and 17
    // cycling. `step()` tells the sound which step it is (-1 when done).
    void start_grab(const Display& now, const std::vector<std::uint8_t>& tethers);
    int grab_step() const { return phase_ == Phase::grab ? step_ : -1; }
    // h14 is showing its picture (the face, the fade).
    bool catching() const { return phase_ == Phase::face || phase_ == Phase::fade || phase_ == Phase::fade_tail; }
    // (While a repaint plays, another one waits its turn.)
    // Whether the sequence leaves the sprites on screen (repaints do).
    bool shows_sprites() const { return phase_ == Phase::repaint || phase_ == Phase::grab; }
    // One display frame on.
    void advance(const Grid& grid = {});
    const Display& display() const { return display_; }

private:
    enum class Phase { idle, face, fade, fade_tail, paint, reveal, settle, repaint, grab };
    void grab_frame();
    void repaint_frame(const Grid& grid);
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
    Repaint repaint_{};
    int repeats_{}, frame_{}, star_x_{}, star_y_{};
    std::array<std::uint16_t, 32> blackened_{}; // h8's h10
    std::array<std::uint16_t, 32> grab_colours_{}; // $2004AC, loaded again at the end
    struct Queued {
        Display next;
        Repaint kind;
        int repeats, star_x, star_y;
        template <class A> void persist(A &ar) { ar(next, kind, repeats, star_x, star_y); }
    };
    std::vector<Queued> queued_; // repaints asked for while one plays (one cell's events)
  public:
    template <class A> void persist(A &ar) {
        ar(display_, next_, phase_, step_, frames_left_, level_, lost_, fading_, repaint_, repeats_, frame_,
           star_x_, star_y_, blackened_, grab_colours_, queued_);
    }
};

} // namespace mindwalker::landscape
