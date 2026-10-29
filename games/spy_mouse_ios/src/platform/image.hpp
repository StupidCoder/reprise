// Decoding the game's PNG and JPEG images into RGBA pixels.
#pragma once

#include "data/game_files.hpp"

#include <optional>

namespace doc::platform {

struct Image {
    int width = 0, height = 0;
    std::vector<std::uint8_t> rgba;  // width * height * 4, rows top to bottom
};

std::optional<Image> decodeImage(const data::Bytes& file);

}  // namespace doc::platform
