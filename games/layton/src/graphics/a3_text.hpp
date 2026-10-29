#pragma once
// The third adventure's text boxes (the game's message object, ov0 0206ad94
// and neighbours): how a message lays out its lines and draws them.
#include "graphics/a3_assets.hpp"
#include <string>

namespace picarat::adventure3 {

// A text box (the game's message object, ov0 0206ad94 and neighbours):
// lines laid out with the font's advances plus a character spacing, a line
// height of the font height plus a line spacing, optionally centred around
// its position, revealed all at once or a few characters at a time.
struct TextBox {
    const Font* font = nullptr;
    int fontHeight = 10;
    int characterSpacing = 1, lineSpacing = 2;
    bool centerX = false, centerY = false;
    std::uint16_t color = 0x0000;    // RGB555
    std::uint16_t emphasis = 0x001f; // <CR> ... </C>
    std::string text;
    int shown = 0;      // characters revealed (counting visible ones)
    int revealRate = 0; // frames per character step (0: all at once)
    int revealTick = 0;
    bool complete() const;
    int visibleCount() const;
};

// Draws text laid out like the game's message object into `image` (one
// screen, 256x192) at the box's position.
void drawTextBox(Surface& image, const TextBox& box, int x, int y, int alpha = 31);

} // namespace picarat::adventure3
