#include "beach_volley_amiga/data/preview.hpp"
#include <algorithm>

namespace beach_volley::data {

platform::Color to_color(Rgb4 colour) {
    return {std::uint8_t((colour >> 8 & 15) * 17), std::uint8_t((colour >> 4 & 15) * 17), std::uint8_t((colour & 15) * 17),
            255};
}

platform::Image to_image(const Picture& picture, const Palette& palette) {
    platform::Image image(picture.width, picture.height);
    for (std::size_t i = 0; i < picture.pixels.size(); ++i) {
        image.pixels[i] = to_color(palette[picture.pixels[i] & 15]);
    }
    return image;
}

platform::Image palette_image(const Palette& palette) {
    constexpr int size = 12;
    platform::Image image(int(palette.size()) * size, size);
    std::fill(image.pixels.begin(), image.pixels.end(), platform::Color{40, 40, 56, 255});
    for (std::size_t i = 0; i < palette.size(); ++i) {
        for (int y = 1; y < size - 1; ++y)
            for (int x = 1; x < size - 1; ++x) image.put(int(i) * size + x, y, to_color(palette[i]));
    }
    return image;
}

platform::Image waveform_image(const app::PreviewSound& sound) {
    constexpr int width = 256, height = 64;
    platform::Image image(width, height);
    std::fill(image.pixels.begin(), image.pixels.end(), platform::Color{24, 24, 40, 255});
    const std::size_t count = sound.samples.size();
    for (int x = 0; x < width && count > 0; ++x) {
        const std::size_t from = count * std::size_t(x) / width, to = std::max(from + 1, count * std::size_t(x + 1) / width);
        float low = 0, high = 0;
        for (std::size_t i = from; i < to && i < count; ++i) {
            low = std::min(low, sound.samples[i]);
            high = std::max(high, sound.samples[i]);
        }
        const int top = int((1.0f - high) * (height - 1) / 2), bottom = int((1.0f - low) * (height - 1) / 2);
        for (int y = top; y <= bottom; ++y) image.put(x, y, platform::Color{120, 200, 255, 255});
    }
    return image;
}

} // namespace beach_volley::data
