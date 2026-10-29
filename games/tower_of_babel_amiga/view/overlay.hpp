#pragma once
#include "tower_of_babel_amiga/data/assets.hpp"
#include "tower_of_babel_amiga/view/canvas.hpp"

// What the game writes over the view: the READY caption before the start
// ($151CC), the compass and the Klondike/object counters ($150BC), the
// timer ($15152) and the sights ($E0F6). Glyphs are 8 x 10 from the main
// data ($238C8, $28 bytes each: per row a keep mask and the pixels, which
// are colour 15).
namespace babel::view {

enum class Corner { off, top, bottom };

struct OverlayState {
    bool ready{};              // before the start ($9968's first loop)
    int blink{};               // $FC0: 6 down to 0, the counters blink when >= 5
    Corner info{Corner::off};  // option $1112 (1 bottom, else top)
    Corner timer{Corner::off}; // option $1114 (2 top, else bottom)
    bool sights{};             // option $111C >= 0
    int spider{-1};            // the active spider (0-2), -1 none
    int heading{};
    int klondikes{}, klondikes_needed{};          // $11D8, $E30
    int destroyed{}, destroy_needed{};            // $11DA, $E32
    bool time_limit{};                            // flag 5 clear
    int time_left{};                              // $121A, BCD seconds
};

void draw_overlay(Canvas&, const OverlayState&, const data::Memory& program, const data::Memory& main, int left = 0);

} // namespace babel::view
