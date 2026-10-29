#pragma once
#include "platform/image.hpp"
#include <string_view>

namespace platform {
// Portable bundled-font rasterizer. White glyph coverage, top-left origin.
// No game fonts or game display code use this service.
struct TypesetLine {
    Image coverage;
    int advance;
};

TypesetLine typeset(std::string_view text, int pixels, bool bold);
} // namespace platform
