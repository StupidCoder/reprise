#pragma once
#include "amiga/hunk.hpp"
#include "mindwalker_amiga/landscape/view_cells.hpp"
#include "mindwalker_amiga/screen/canvas.hpp"
#include <array>
#include <cstdint>
#include <deque>
#include <functional>

// The landscape's map (the emblem gadget, id 4; A $20071C opens it, $200D92
// runs each frame, $2005C2 takes clicks, $200D02 closes it): a 16 x 8 grid of
// squares of 4 x 4 world cells each, centred on the view (north up), every
// world cell a 5-pixel block in its surface's colour (the four kinds, named
// Humans, Wizards, Spriggans and Nymphs on the map) plus its feature
// (crystals, tubes, pyramids, the walker's path); beacons in the cycling
// colour 16, the marked block in 29. Opening draws the middle 6 x 4 squares;
// holding the button on the grid draws the square under the pointer; "Fill
// In Entire Map" draws them all; the legend's words light their colours in
// yellow while held. The map has its own 30 colours (h6, $200EA0).
namespace mindwalker::landscape {

class MapView {
public:
    // The view's cells at any position (the view builder, $2086B0).
    using Builder = std::function<ViewCells(std::uint16_t x, std::uint16_t y)>;

    explicit MapView(const amiga::LoadedImage& landscape);

    bool open() const { return open_; }
    // $20071C: the map over the display's planes 0-3 (`display` holds the
    // bar, which stays), around the view at (view_x, view_y).
    void show(const screen::Canvas& display, std::uint16_t view_x, std::uint16_t view_y, const Builder&);
    void close() { open_ = false; fill_all_ = false; }
    // $20071C(0) after a pause: drawn again (all squares when it was filled).
    void redraw(const Builder&);
    // $2005C2: the left button pressed or released at the pointer.
    void button(bool down, int x, int y);
    // $200D92: one turn of the map's loop. Returns the display frames it
    // takes (two WaitTOFs while the colour cycles).
    int turn(int x, int y);
    // Squares are drawn one after another, about 3 frames each on the A500
    // (measured: opening 79 frames, filling 102 squares 304); meanwhile the
    // loop reads no input. One frame of that work.
    // (The builder is passed on each call, not kept: a map view holds no
    // pointer back into its stage, so a stage can be copied.)
    bool busy() const { return wait_ > 0 || !pending_.empty(); }
    void work(const Builder&);

    const screen::Canvas& planes() const { return canvas_; }
    // Colours 0-29 as the map has them (30 and 31 stay the view's).
    const std::array<std::uint16_t, 30>& colours() const { return colours_; }

    // A cell's pen on the map ($200BF0; also the minimap's) and the map's
    // 30 colours (h6).
    static int pen(const ViewCells&, int x, int y);
    static int pen(std::uint16_t terrain, std::uint16_t objects);
    static std::array<std::uint16_t, 30> palette(const amiga::LoadedImage& landscape);

private:
    void draw_square(int column, int row, const Builder&); // $200A36
    void queue_square(int column, int row);
    void draw_box();
    void load_colours(); // LoadRGB4 of h6

    const amiga::LoadedImage& a_;
    screen::Canvas canvas_;
    std::array<std::uint16_t, 30> colours_{};
    std::uint16_t origin_x_{}, origin_y_{}, view_x_{}, view_y_{};
    screen::Canvas display_;
    bool open_{}, held_{}, fill_all_{}; // $204D6E, $20102E, $20101E
    std::uint16_t cycle_{};             // h7: colour 16's value
    std::deque<int> pending_;           // squares (column * 8 + row) to draw; -1: the view's box
    int wait_{};                        // frames until the next
};

} // namespace mindwalker::landscape
