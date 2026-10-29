#pragma once
#include "gg_engine/video/display.hpp"
#include "triple_trouble_gg/game/types.hpp"

namespace triple_trouble_gg {

using gg_engine::Display;

// The world position of the 256x192 picture's top left corner while the
// act's map is shown (set with the scroll, interrupt.cpp; read by
// Game::level_background).
struct PictureOrigin {
    int x{}, y{};
    bool pending{}; // The act was just drawn: the next frame sets x and y
};

} // namespace triple_trouble_gg
