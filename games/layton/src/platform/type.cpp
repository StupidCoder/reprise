// Rasterize trusted bundled interface fonts without native font or UI APIs.
#include "platform/type.hpp"
#include "platform/host.hpp"
#define STB_TRUETYPE_IMPLEMENTATION
#include "third_party/stb_truetype.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace picarat::platform {
namespace {
struct Font {
    std::vector<std::uint8_t> bytes;
    stbtt_fontinfo info{};

    explicit Font(const char* file)
        : bytes(Host::readFile(Host::resourceDirectory() / file, 4 * 1024 * 1024)) {
        if (!stbtt_InitFont(&info, bytes.data(), 0)) {
            throw std::runtime_error("Invalid bundled interface font.");
        }
    }
};

std::vector<int> codepoints(std::string_view text) {
    std::vector<int> result;
    for (std::size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i++]);
        int n = c < 0x80             ? 0
                : (c & 0xe0) == 0xc0 ? 1
                : (c & 0xf0) == 0xe0 ? 2
                : (c & 0xf8) == 0xf0 ? 3
                                     : -1;
        int code = n == 0 ? c : n == 1 ? c & 31 : n == 2 ? c & 15 : c & 7;
        if (n < 0) {
            result.push_back(0xfffd);
            continue;
        }
        const int count = n;
        bool valid = true;
        while (n-- > 0) {
            if (i >= text.size() || (static_cast<unsigned char>(text[i]) & 0xc0) != 0x80) {
                valid = false;
                break;
            }
            code = (code << 6) | (static_cast<unsigned char>(text[i++]) & 63);
        }
        if ((count == 1 && code < 0x80) || (count == 2 && code < 0x800) ||
            (count == 3 && code < 0x10000) || code > 0x10ffff ||
            (code >= 0xd800 && code <= 0xdfff)) {
            valid = false;
        }
        result.push_back(valid ? code : 0xfffd);
    }
    return result;
}

const stbtt_fontinfo& fontFor(bool bold) {
    static Font regular("fonts/Lato-Regular.ttf"), heavy("fonts/Lato-Bold.ttf");
    return bold ? heavy.info : regular.info;
}
} // namespace

int textAdvance(std::string_view text, int pixels, bool bold) {
    const auto& font = fontFor(bold);
    const float scale = stbtt_ScaleForMappingEmToPixels(&font, float(pixels));
    const auto codes = codepoints(text);
    float width = 0;
    for (std::size_t i = 0; i < codes.size(); ++i) {
        int advance{}, left{};
        stbtt_GetCodepointHMetrics(&font, codes[i], &advance, &left);
        width += advance * scale;
        if (i + 1 < codes.size()) {
            width += stbtt_GetCodepointKernAdvance(&font, codes[i], codes[i + 1]) * scale;
        }
    }
    return int(std::ceil(width));
}

TypesetLine typeset(std::string_view text, int pixels, bool bold) {
    // Only Picarat's trusted, bundled font files go into stb_truetype. Imported
    // game data is never treated as a host font. All pixels are presented by SDL.
    const auto& font = fontFor(bold);
    const float scale = stbtt_ScaleForMappingEmToPixels(&font, float(pixels));
    int ascent{}, descent{}, leading{};
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &leading);
    const int baseline = int(std::ceil(ascent * scale));
    const auto codes = codepoints(text);
    float advance = 0;
    for (std::size_t i = 0; i < codes.size(); ++i) {
        int ax{}, lsb{};
        stbtt_GetCodepointHMetrics(&font, codes[i], &ax, &lsb);
        advance += ax * scale;
        if (i + 1 < codes.size()) {
            advance += stbtt_GetCodepointKernAdvance(&font, codes[i], codes[i + 1]) * scale;
        }
    }
    Image mask(std::max(1, int(std::ceil(advance)) + 4),
               int(std::ceil((ascent - descent) * scale)) + 3);
    std::fill(mask.pixels.begin(), mask.pixels.end(), Color{0, 0, 0, 0});
    float pen = 1;
    for (std::size_t i = 0; i < codes.size(); ++i) {
        int w{}, h{}, xoff{}, yoff{};
        const float subpixel = pen - std::floor(pen);
        auto* bitmap = stbtt_GetCodepointBitmapSubpixel(&font, scale, scale, subpixel, 0, codes[i],
                                                        &w, &h, &xoff, &yoff);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const auto a = bitmap[y * w + x];
                mask.put(int(pen) + xoff + x, baseline + yoff + y, {255, 255, 255, a});
            }
        }
        stbtt_FreeBitmap(bitmap, nullptr);
        int ax{}, lsb{};
        stbtt_GetCodepointHMetrics(&font, codes[i], &ax, &lsb);
        pen += ax * scale;
        if (i + 1 < codes.size()) {
            pen += stbtt_GetCodepointKernAdvance(&font, codes[i], codes[i + 1]) * scale;
        }
    }
    int x0{}, y0{}, x1{}, y1{};
    stbtt_GetCodepointBox(&font, 'H', &x0, &y0, &x1, &y1);
    return {std::move(mask), int(std::ceil(advance)), baseline - int(std::ceil(y1 * scale))};
}
} // namespace picarat::platform
