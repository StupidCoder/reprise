// The sixth adventure's on-screen arrow pad, kept by the base class of
// every special play loop (0x22fd90: commands 11000..11003 and 11010 go to
// the loop's vtable +0x9c..+0xac, 0x32cbbc .. 0x32c6e8). The puzzles'
// library (prog_nazo Nazo_ArwBtn_Setup_Ex) builds it from two objects: one
// draws the four arrows (an animation per direction, swapped for the
// pressed look), an invisible one takes the touch over four areas made from cells
// of the same set; __Nazo_ArwTouch then asks 11010 which way is pressed.
// Directions: 1 right, 2 up, 3 left, 4 down (the scripts' order), 6 the
// centre button.
#pragma once

#include "system/a5_game.hpp"
#include <array>

namespace picarat::adventure5 {

class ArrowPad {
public:
    // A base-loop command (11000..11010); false when the number is not one.
    bool command(Game& game, int number, xq::Call& c, std::size_t first);

private:
    void rebuild(Game& game);

    std::uint32_t object_ = 0;             // +0x190 record +0x18: the arrows' object
    std::uint32_t set_ = 0;                // +0x1c: their animation set
    int count_ = 0;                        // +0x20
    std::array<std::uint32_t, 8> cells_{}; // the animation shown per slot (11001)
    std::array<float, 8> levels_{};        // 11002 (0.4 dims a blocked arrow)
};

} // namespace picarat::adventure5
