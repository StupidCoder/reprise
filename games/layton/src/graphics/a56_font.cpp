// Fonts of the 3DS adventures. See a56_font.hpp.
#include "graphics/a56_font.hpp"
#include "graphics/a56_assets.hpp"
#include "graphics/a56_picture.hpp"
#include "import/asset_codecs.hpp"
#include <cstring>
#include <stdexcept>

namespace picarat::adventure56 {

namespace fs = std::filesystem;

namespace {

void check(bool ok, const char* what) {
    if (!ok) {
        throw std::runtime_error(what);
    }
}

unsigned u16(const Bytes& d, std::size_t at) {
    check(at + 2 <= d.size(), "font field outside");
    return unsigned(d[at] | d[at + 1] << 8);
}

std::uint32_t u32(const Bytes& d, std::size_t at) {
    check(at + 4 <= d.size(), "font field outside");
    return std::uint32_t(d[at] | d[at + 1] << 8 | d[at + 2] << 16) | std::uint32_t(d[at + 3]) << 24;
}

} // namespace

const Font::Glyph* Font::glyph(std::uint32_t code) const {
    const auto it = glyphs.find(code);
    return it == glyphs.end() ? nullptr : &it->second;
}

int Font::advance(std::uint32_t code) const {
    const Glyph* g = glyph(code);
    return g ? g->advance : 0;
}

std::shared_ptr<Font> loadFont(const Pack& pack, const fs::path& folder) {
    const Bytes data = pack.read(folder / "FNT.bin");
    check(data.size() >= 0x28 && std::memcmp(data.data(), "FNTC", 4) == 0, "not an FNTC font");
    auto font = std::make_shared<Font>();
    font->lineHeight = int(u32(data, 0x0c));
    const std::size_t boxesAt = std::size_t(u16(data, 0x1c)) * 4;
    const unsigned boxCount = u16(data, 0x1e);
    const std::size_t charsAt = std::size_t(u16(data, 0x20)) * 4;
    const unsigned charCount = u16(data, 0x22);
    check(boxesAt < data.size() && charsAt < data.size(), "font tables outside");
    const Bytes boxes = import::decompressMethodWord(std::span(data).subspan(boxesAt));
    const Bytes chars = import::decompressMethodWord(std::span(data).subspan(charsAt));
    check(boxes.size() >= boxCount * 4u && chars.size() >= charCount * 8u, "font tables short");
    for (unsigned i = 0; i < charCount; ++i) {
        const unsigned code = u16(chars, i * 8), metrics = u16(chars, i * 8 + 2);
        const std::uint32_t at = u32(chars, i * 8 + 4);
        Font::Glyph g;
        g.advance = int(metrics >> 10);
        const unsigned box = metrics & 0xff;
        if (box < boxCount) {
            // Signed bytes: a "j" starts a pixel left of the pen.
            g.offsetX = std::int8_t(boxes[box * 4]);
            g.offsetY = std::int8_t(boxes[box * 4 + 1]);
            g.width = boxes[box * 4 + 2];
            g.height = boxes[box * 4 + 3];
        }
        g.plane = int(at & 15);
        g.x = int((at >> 4) & 0x3ff);
        g.y = int((at >> 18) & 0x3f);
        font->glyphs[code] = g;
    }
    // The sheet: 16-bit RGBA5551 tiles whose colour channels are three
    // 5-bit glyph planes.
    const auto picture = import::adventure56::parseImgc(pack.read(folder / "000.xi"));
    for (int p = 0; p < 3; ++p) {
        auto t = std::make_shared<ctr3d::Texture>();
        t->width = int(picture.width);
        t->height = int(picture.height);
        t->pixels.resize(std::size_t(t->width) * std::size_t(t->height));
        t->linear = true;
        font->planes[p] = t;
    }
    static const int kShift[3] = {11, 6, 1};
    for (unsigned y = 0; y < picture.height; ++y) {
        for (unsigned x = 0; x < picture.width; ++x) {
            const unsigned v = rawPixel16(picture, x, y);
            for (int p = 0; p < 3; ++p) {
                const unsigned c = (v >> kShift[p]) & 31;
                font->planes[p]->pixels[y * picture.width + x] = {255, 255, 255,
                                                                   std::uint8_t(c << 3 | c >> 2)};
            }
        }
    }
    return font;
}

std::vector<std::uint32_t> decodeText(const std::string& text) {
    // Two-character escapes in braces (the game's table at 0x355e1c lists
    // the same spellings); the rest of the text is Latin-1.
    struct Escape {
        const char* spelling;
        std::uint32_t code;
    };
    static const Escape kEscapes[] = {
        {",,", 0x201e}, {"66", 0x201c}, {"99", 0x201d}, {"''", 0x201d}, {"OE", 0x152},
        {"oe", 0x153},  {"^!", 0xa1},   {"^?", 0xbf},   {"<<", 0xab},   {">>", 0xbb},
        {"po", 0xa3},   {"C=", 0x20ac}, {"o ", 0xb0},   {"a ", 0xaa},   {"0 ", 0xb0},
        {"ss", 0xdf},   {",C", 0xc7},   {",c", 0xe7},   {"-N", 0xd1},   {"-n", 0xf1},
        {"-A", 0xc3},   {"-a", 0xe3},   {"-O", 0xd5},   {"-o", 0xf5},
    };
    static const char kVowels[] = "AEIOUaeiou";
    static const std::uint32_t kGrave[] = {0xc0, 0xc8, 0xcc, 0xd2, 0xd9, 0xe0, 0xe8, 0xec, 0xf2, 0xf9};
    static const std::uint32_t kAcute[] = {0xc1, 0xc9, 0xcd, 0xd3, 0xda, 0xe1, 0xe9, 0xed, 0xf3, 0xfa};
    static const std::uint32_t kCircumflex[] = {0xc2, 0xca, 0xce, 0xd4, 0xdb,
                                                0xe2, 0xea, 0xee, 0xf4, 0xfb};
    static const std::uint32_t kUmlaut[] = {0xc4, 0xcb, 0xcf, 0xd6, 0xdc, 0xe4, 0xeb, 0xef, 0xf6, 0xfc};
    std::vector<std::uint32_t> out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = (unsigned char)text[i];
        if (c == '{' && i + 3 < text.size() && text[i + 3] == '}') {
            const char a = text[i + 1], b = text[i + 2];
            std::uint32_t code = 0;
            for (const auto& e : kEscapes) {
                if (e.spelling[0] == a && e.spelling[1] == b) {
                    code = e.code;
                    break;
                }
            }
            if (!code) {
                if (const char* v = std::strchr(kVowels, b); v && *v) {
                    const std::size_t k = std::size_t(v - kVowels);
                    code = a == '`' ? kGrave[k] : a == '\'' ? kAcute[k] : a == '^' ? kCircumflex[k]
                                                          : a == ':' ? kUmlaut[k] : 0;
                }
            }
            if (code) {
                out.push_back(code);
                i += 3;
                continue;
            }
        }
        out.push_back(c);
    }
    return out;
}

} // namespace picarat::adventure56
