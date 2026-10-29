#include "platform/image.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include "third_party/stb_image.h"

namespace doc::platform {

std::optional<Image> decodeImage(const data::Bytes& file) {
    int w = 0, h = 0, channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(file.data(), int(file.size()), &w, &h, &channels, 4);
    if (!pixels) return std::nullopt;
    Image image;
    image.width = w;
    image.height = h;
    image.rgba.assign(pixels, pixels + std::size_t(w) * h * 4);
    stbi_image_free(pixels);
    return image;
}

}  // namespace doc::platform
