// See text.hpp.
#include "graphics/text.hpp"
#include <algorithm>
#include <stdexcept>

namespace picarat {
GameText::GameText(const std::filesystem::path& root) {
    auto b = readFile(root / "escapes.bin");
    const unsigned version = b.size() >= 12 ? read32(b, 4) : 0;
    if (b.size() < 12 || std::string(b.begin(), b.begin() + 4) != "L2ES" || version < 1 ||
        version > 2 || (version == 2 && b.size() < 16)) {
        throw std::runtime_error("escape table header");
    }
    std::size_t pos = 8;
    if (version == 2) {
        open_ = char(b[pos]);
        close_ = char(b[pos + 1]);
        pos += 4;
    }
    auto count = read32(b, pos);
    pos += 4;
    if (count > 256) {
        throw std::runtime_error("escape table count");
    }
    for (unsigned i = 0; i < count; ++i) {
        auto n = read32(b, pos);
        pos += 4;
        if (n > b.size() - pos) {
            throw std::runtime_error("escape table bounds");
        }
        std::string key(b.begin() + pos, b.begin() + pos + n);
        pos += n;
        escapes_[key] = read32(b, pos);
        pos += 4;
    }
    if (pos != b.size()) {
        throw std::runtime_error("escape table tail");
    }
}

std::string GameText::expand(std::string_view raw) const {
    // Second adventure: ov2 0206bdd4 resolves strings through the table at
    // 0206bec8. Third: ov0 0206aacc finds the '}' and matches the two
    // characters against the table at 0206daa8.
    std::string out;
    for (std::size_t i = 0; i < raw.size() && raw[i]; ++i) {
        if (raw[i] == open_) {
            auto end = raw.find(close_, i + 1);
            if (end == raw.npos) {
                throw std::runtime_error("unterminated text escape");
            }
            auto key = std::string(raw.substr(i + 1, end - i - 1));
            auto it = escapes_.find(key);
            if (it == escapes_.end()) {
                throw std::runtime_error("unknown ROM text escape: " + key);
            }
            out += char(it->second);
            i = end;
        } else {
            out += raw[i];
        }
    }
    return out;
}

int textAdvance(const Font& font, unsigned c) {
    // ov2 02072608 / 02072634 use separate 224-byte tables, not NFTR advances.
    if (!font.textWidths.empty()) {
        return c >= 32 && c <= 255 ? font.textWidths.at(c - 32) : 0;
    }
    auto it = font.glyphs.find(c);
    return it == font.glyphs.end() ? 0 : it->second.advance;
}

void textGlyph(Surface& dst, const Font& font, unsigned c, int x, int y, std::uint16_t color) {
    auto it = font.glyphs.find(c);
    if (it == font.glyphs.end()) {
        return;
    }
    const auto& g = it->second;
    x += g.left;
    for (unsigned yy = 0; yy < g.height; ++yy) {
        for (unsigned xx = 0; xx < g.width; ++xx) {
            int dx = x + int(xx), dy = y + int(yy);
            if (dx >= 0 && dy >= 0 && dx < int(dst.width) && dy < int(dst.height) &&
                g.alpha[yy * g.width + xx]) {
                dst.pixels[dy * dst.width + dx] = color;
            }
        }
    }
}

void textLines(Surface& image, const Font& font, std::string_view text, int x, int y,
               int lineHeight, std::uint16_t color, bool colorControls) {
    int start = x;
    for (std::size_t i = 0; i < text.size(); ++i) {
        unsigned c = std::uint8_t(text[i]);
        if (colorControls && c == '#' && i + 1 < text.size()) {
            // ov2 02073088 consumes the marker; 020726cc selects font palette 12/15.
            // RGB555 values verified against special-167-b's red/default glyphs.
            switch (text[++i]) {
            case 'r':
            case 'R':
                color = 0x801f;
                break; // Red - Selects the red font color.
            case 'x':
            case 'X':
                color = 0x8000;
                break; // Default - Selects the black font color.
            case 'g':
            case 'G':
            case 'w':
            case 'W':
                throw std::runtime_error(
                    "unrecovered puzzle font color"); // OtherColor - Requires its font palette
                                                      // mapping.
            default:
                break; // UnknownColor - Original leaves the current palette index unchanged.
            }
            continue;
        }
        if (c == '\r') {
            continue;
        }
        if (c == '\n' || (c == '@' && i + 1 < text.size() && text[i + 1] == 'B')) {
            if (c == '@') {
                ++i;
            }
            x = start;
            y += lineHeight;
            continue;
        }
        textGlyph(image, font, c, x, y, color);
        x += textAdvance(font, c) + 1;
    }
}

PuzzleData::PuzzleData(std::span<const std::uint8_t> b, const GameText& text) {
    // ov27 020f593c: pointers +58..6c are base+70 plus offsets +40..54.
    if (b.size() < 0x70 || read16(b, 2) != 0x70) {
        throw std::runtime_error("puzzle data header");
    }
    auto string = [&](std::size_t p) {
        if (p >= b.size()) {
            throw std::runtime_error("puzzle text offset");
        }
        auto end = std::find(b.begin() + p, b.end(), 0);
        if (end == b.end()) {
            throw std::runtime_error("puzzle text terminator");
        }
        return text.expand(std::string(b.begin() + p, end));
    };
    number = read16(b, 0);
    title = string(4);
    tutorial = b[0x34];
    for (unsigned i = 0; i < 3; ++i) {
        picarats[i] = b[0x35 + i];
    }
    flags = b[0x38];
    place = b[0x39];
    type = b[0x3a];
    background = b[0x3b];
    soundId = read16(b, 0x3c);
    theme = b[0x3e];
    completionValue = b[0x3f] < 128 ? b[0x3f] : int(b[0x3f]) - 256;
    prompt = string(0x70 + read32(b, 0x40));
    correct = string(0x70 + read32(b, 0x44));
    incorrect = string(0x70 + read32(b, 0x48));
    for (unsigned i = 0; i < 3; ++i) {
        hints[i] = string(0x70 + read32(b, 0x4c + 4 * i));
    }
}
} // namespace picarat
