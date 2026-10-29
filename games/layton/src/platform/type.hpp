#pragma once
// Metrics and antialiased coverage for the shell's bundled interface fonts.
#include "platform/image.hpp"
#include <string_view>

namespace picarat::platform {
// Portable bundled-font rasterizer. White glyph coverage, top-left origin.
// No game fonts or game display code use this service.
struct TypesetLine {
    Image coverage;
    int advance;
    int capOffset;
};

int textAdvance(std::string_view text, int pixels, bool bold);
TypesetLine typeset(std::string_view text, int pixels, bool bold);
} // namespace picarat::platform
