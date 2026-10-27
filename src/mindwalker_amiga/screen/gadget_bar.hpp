#pragma once
#include "amiga/hunk.hpp"
#include "mindwalker_amiga/screen/canvas.hpp"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// The strip of Intuition gadgets along the top of every stage's window (QUIT,
// the pause box, the progress slider, the disk and "Fame"), read from the
// stage program's NewWindow gadget list and drawn as Intuition 1.3 draws it.
//
// Rules taken from the original's screen (T's planes read from the reference
// machine, .analysis/mindwalker_amiga/game.md "Gadget bar"): rows 0-9 start
// in the bar's background pen (T 15, A 18) and rows 10-11 in pen 0; gadget
// images follow PlanePick and PlaneOnOff; a proportional gadget's container is cleared to pen 0 inset by
// 2 pixels left and right and 1 top and bottom, with its knob image 4 pixels
// in; a disabled gadget is ghosted with pen 15 in Intuition's screen-aligned
// pattern ($4444 on even rows, $1111 on odd ones).
namespace mindwalker::screen {

struct GadgetImage {
    int left{}, top{}, width{}, height{}, depth{};
    std::uint32_t data{};               // address in the program (0: none)
    int plane_pick{}, plane_on_off{};
};

struct GadgetText {
    int front{}, back{}, mode{}, left{}, top{};
    std::string text;
};

struct Gadget {
    int id{};
    int left{}, top{}, width{}, height{};
    bool relative_right{};              // GRELRIGHT: left is from the window's right edge
    bool proportional{};
    std::vector<GadgetImage> images;    // GadgetRender's chain (the knob for a slider)
    std::vector<GadgetText> texts;
    bool enabled{true};
    std::uint16_t horizontal_pot{};     // sliders: 0-$FFFF

    // GRELRIGHT counts from the window's last column (Width - 1).
    int x() const { return relative_right ? Canvas::width - 1 + left : left; }
};

class GadgetBar {
public:
    GadgetBar() = default;
    // The gadgets of the NewWindow at `new_window` in `program`. Image data
    // the stage fills in at run time is passed in `image_data` (keyed by the
    // address of the Image structure's data pointer).
    GadgetBar(const amiga::LoadedImage& program, std::uint32_t new_window,
              std::map<std::uint32_t, std::vector<std::uint8_t>> image_data);

    Gadget* find(int id);
    const std::vector<Gadget>& gadgets() const { return gadgets_; }
    // Rows 0-9 start in `background` (T's screen bar: pen 15; A's window
    // title bar: its BlockPen, 18), rows 10-11 in pen 0.
    void draw(Canvas&, int background = 15) const;

private:
    void draw_image(Canvas&, const GadgetImage&, int x, int y) const;
    std::vector<Gadget> gadgets_;
    std::map<std::uint32_t, std::vector<std::uint8_t>> data_; // image data by its address
};

} // namespace mindwalker::screen
