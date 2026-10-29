#pragma once
// The ink drawing service of overlay 2 (02071470 dot, 020714dc line), used by
// the puzzles where the player draws. The brush is character 0x60 of the
// font fontq.NFTR, stamped along the line. The ink goes into a transparent
// 256x192 layer kept by the native scene. The first adventure has the same
// service in its ARM9 (02010378 dot, 0200fff4 line, the brush stamped by
// 0200d310 with the European program's single-byte text) and draws into its
// lower text layer (stamp()).
#include "graphics/graphics.hpp"
#include "puzzles/circle_input.hpp"

namespace picarat {

class CircleInk {
public:
    Surface surface{256, 192, std::vector<std::uint16_t>(256 * 192, 0)};

    // Erases all ink.
    void clear();

    // Draws a dot or line (after clearing, if the command asks for it).
    void apply(const CircleInkCommand&, const Font&, std::uint16_t color);

    // Draws a dot or line into any layer (no clearing).
    static void stamp(Surface& layer, const CircleInkCommand&, const Font&, std::uint16_t color);
};

} // namespace picarat
