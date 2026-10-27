#pragma once
#include <array>
#include <cstdint>

// Imagery that Intuition (Kickstart 1.x) draws itself, for games whose
// screens show it (see intuition_imagery.cpp for its source and licence; it
// is not covered by Joyport's MIT licence).
namespace amiga {

// A screen's depth-arrangement gadgets in its title bar: GRELRIGHT gadgets
// (x = screen width - 1 + left), 17 x 10, two planes (PlanePick 3,
// PlaneOnOff 0), rows of 32 bits with the leftmost pixel in bit 31.
struct ScreenDepthGadget {
    int left;
    std::array<std::array<std::uint32_t, 10>, 2> planes;
    static constexpr int width = 17, height = 10;
};

const std::array<ScreenDepthGadget, 2>& screen_depth_gadgets();

} // namespace amiga
