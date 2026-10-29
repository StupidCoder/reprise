#pragma once
#include "spindizzy_worlds_amiga/view/sprites.hpp"
#include <cstdint>

// The playfield's scroll position following the explorer. The original
// keeps it in pixels of the 1024-pixel map ($2570(a5) across, $2572(a5)
// down; the shown window starts 16 pixels left of $2570) and moves it once
// per frame ($14138) toward the explorer's sprite: an eighth of the way,
// at most 8 pixels across and 4 down per frame. A level start or respawn
// puts it there at once ($14202).
namespace spindizzy::view {

struct Camera {
    int scroll_x{}; // $2570
    int scroll_y{}; // $2572
    template <class A> void persist(A& ar) { ar(scroll_x, scroll_y); }

    // The window's left and top in map pixels (as draw_playfield takes them).
    int left() const { return scroll_x - 16; }
    int top() const { return scroll_y; }
};

// $14202: centred on the explorer's sprite corner (explorer_place), in
// 16-pixel steps across. A level header flag bit 15 ($24B8) fixes the
// view across at column $17 (and $14150 then never scrolls it across).
Camera snap_camera(const SpritePlace& explorer, std::uint16_t level_flags);

// $14138: one frame's easing; z is the explorer's height (its sprite is
// followed as if 1/128 of z lower).
void follow(Camera&, const SpritePlace& explorer, int z, std::uint16_t level_flags);

} // namespace spindizzy::view
