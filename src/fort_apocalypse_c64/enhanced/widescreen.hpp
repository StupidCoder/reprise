#pragma once
#include "c64/text_frame.hpp"
#include "fort_apocalypse_c64/game/game.hpp"
#include <vector>

// The widescreen view: the picture fills the window to its edges, without
// the C64's border, and the playfield shows more of the map left and right
// of the C64's 40 columns, up to 21:9 (the widest BlueStreak's Game Gear
// games use; the narrowest is the original screen with its border). The
// game plays as the original: the extra columns are the map cells copied
// along with the original window, and the enemy helicopter, which the game
// hides when it is outside its own range, is shown in them where it is. The
// HUD stays in the middle.
namespace fort_apocalypse::enhanced {

using u8 = std::uint8_t;

struct WideSize {
    int columns{};  // added on each side (one more than shows, so a column
                    // scrolling in is drawn already)
    int width{};    // of the picture to show (0: not widescreen)
    int margin_y{}; // border above and below, in windows narrower than the
                    // display window's shape
};
// For a canvas (window) of this size, with C64 pixels this wide for their
// height.
WideSize wide_size(int canvas_width, int canvas_height, double pixel_aspect);

// The game's last frame with `columns` more on each side (0: as it is).
c64::TextFrame wide_frame(const game::Game&, int columns);

// The character codes such a frame shows.
struct ScreenCodes {
    int columns{};   // of the frame
    int extension{}; // columns left of the original window
    std::vector<u8> codes;
    u8 at(int column, int row) const { return codes[std::size_t(row * columns + column)]; }
};
ScreenCodes screen_codes(const game::Game&, int columns);

} // namespace fort_apocalypse::enhanced
