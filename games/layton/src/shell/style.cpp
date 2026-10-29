// The shell drawing kit: scalable text, private icons, UTF-8 and shapes.
#include "shell/style.hpp"
#include "platform/type.hpp"
#include "shell/ui_font.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <tuple>

namespace picarat::shell::style {

namespace {

// Typographic characters the font lacks, drawn with a plain one.
constexpr std::pair<char32_t, char> kSubstitutes[] = {
    {U'–', '-'}, {U'—', '-'}, {U'‚', ','}, {U'‹', '<'}, {U'›', '>'},
    {U'Œ', 'O'}, {U'œ', 'o'}, {U'Ÿ', 'Y'}, {U'€', 'E'}, {0x2212, '-'},
};

// Index of a code point's glyph in the font, or -1.
int glyphIndex(char32_t c) {
    const char32_t* end = kFontCodes + kFontCodeCount;
    const char32_t* found = std::lower_bound(kFontCodes, end, c);
    return found != end && *found == c ? int(found - kFontCodes) : -1;
}

// What one code point is drawn as: up to three glyphs (the font has no
// ellipsis, so "…" is three full stops).
struct Shape {
    int glyphs[3]{};
    int count = 0;
};

Shape shapeOf(char32_t c) {
    Shape s;
    if (c == U'…') {
        s.glyphs[0] = s.glyphs[1] = s.glyphs[2] = glyphIndex('.');
        s.count = 3;
        return s;
    }
    int index = glyphIndex(c);
    for (const auto& [code, plain] : kSubstitutes) {
        if (index < 0 && code == c) {
            index = glyphIndex(char32_t(plain));
        }
    }
    s.glyphs[0] = index >= 0 ? index : glyphIndex('?');
    s.count = 1;
    return s;
}

int characterCount(std::string_view utf8) {
    int count = 0;
    for (std::size_t i = 0; i < utf8.size();) {
        count += shapeOf(nextCodePoint(utf8, i)).count;
    }
    return count;
}

} // namespace

// --- Font sizes --------------------------------------------------------------

FontSize fontSize(int pixels) {
    // The strike and whole-number factor closest to the asked em size; on a
    // tie the larger strike (fewer enlarged pixels) wins.
    FontSize best{&kFontStrikes[0], 1};
    int bestError = 1 << 30;
    for (int i = kFontStrikeCount - 1; i >= 0; --i) {
        const FontStrike& strike = kFontStrikes[i];
        const int factor = std::max(1, int(std::lround(double(pixels) / strike.size)));
        const int error = std::abs(strike.size * factor - pixels);
        if (error < bestError) {
            best = {&strike, factor};
            bestError = error;
        }
    }
    return best;
}

// --- UTF-8 -----------------------------------------------------------------

char32_t nextCodePoint(std::string_view s, std::size_t& i) {
    const auto byte = [&](std::size_t k) {
        return std::uint8_t(s[k]);
    };
    const std::uint8_t lead = byte(i);
    int length = lead < 0x80         ? 1
                 : (lead >> 5) == 6  ? 2
                 : (lead >> 4) == 14 ? 3
                 : (lead >> 3) == 30 ? 4
                                     : 0;
    if (length > 1 && i + length <= s.size()) {
        char32_t c = lead & (0x7f >> length);
        bool valid = true;
        for (int k = 1; k < length; ++k) {
            valid = valid && (byte(i + k) & 0xc0) == 0x80;
            c = (c << 6) | (byte(i + k) & 0x3f);
        }
        if (valid) {
            i += length;
            return c;
        }
    }
    ++i;
    return lead; // ASCII, or a stray byte read as Latin-1
}

void appendUtf8(std::string& out, char32_t c) {
    if (c < 0x80) {
        out += char(c);
    } else if (c < 0x800) {
        out += char(0xc0 | (c >> 6));
        out += char(0x80 | (c & 0x3f));
    } else if (c < 0x10000) {
        out += char(0xe0 | (c >> 12));
        out += char(0x80 | ((c >> 6) & 0x3f));
        out += char(0x80 | (c & 0x3f));
    } else {
        out += char(0xf0 | (c >> 18));
        out += char(0x80 | ((c >> 12) & 0x3f));
        out += char(0x80 | ((c >> 6) & 0x3f));
        out += char(0x80 | (c & 0x3f));
    }
}

std::string foldCase(std::string_view utf8) {
    std::string out;
    for (std::size_t i = 0; i < utf8.size();) {
        char32_t c = nextCodePoint(utf8, i);
        if ((c >= 'A' && c <= 'Z') || (c >= 0xc0 && c <= 0xde && c != 0xd7)) {
            c += 0x20;
        }
        appendUtf8(out, c);
    }
    return out;
}

void popCodePoint(std::string& utf8) {
    while (!utf8.empty()) {
        const auto last = std::uint8_t(utf8.back());
        utf8.pop_back();
        if ((last & 0xc0) != 0x80) {
            break; // removed the first byte of the code point
        }
    }
}

// --- Pixel level -----------------------------------------------------------

int textWidth(std::string_view utf8, FontSize font) {
    return characterCount(utf8) * font.strike->advance * font.factor;
}

int text(Image& im, std::string_view utf8, int x, int y, FontSize font, Color color) {
    const FontStrike& s = *font.strike;
    const int k = font.factor;
    const int glyphBytes = s.height * s.rowBytes;
    // y is the top of the capitals; the cell starts higher by the rows above
    // them (accents).
    const int top = y - (s.baseline - s.capHeight) * k;
    for (std::size_t i = 0; i < utf8.size();) {
        const Shape shape = shapeOf(nextCodePoint(utf8, i));
        for (int g = 0; g < shape.count; ++g) {
            const std::uint8_t* bits = s.bits + std::size_t(shape.glyphs[g]) * glyphBytes;
            const int left = x + s.left * k;
            for (int row = 0; row < s.height; ++row, bits += s.rowBytes) {
                // Runs of lit pixels become one rectangle each.
                for (int col = 0; col < s.width;) {
                    if (!(bits[col / 8] & (0x80 >> (col % 8)))) {
                        ++col;
                        continue;
                    }
                    int end = col + 1;
                    while (end < s.width && (bits[end / 8] & (0x80 >> (end % 8)))) {
                        ++end;
                    }
                    rect(im, left + col * k, top + row * k, (end - col) * k, k, color);
                    col = end;
                }
            }
            x += s.advance * k;
        }
    }
    return x;
}

Color mix(Color a, Color b, int t) {
    t = std::clamp(t, 0, 255);
    auto m = [t](int p, int q) {
        return std::uint8_t(p + (q - p) * t / 255);
    };
    return {m(a.r, b.r), m(a.g, b.g), m(a.b, b.b), m(a.a, b.a)};
}

void fill(Image& im, Color c) {
    std::fill(im.pixels.begin(), im.pixels.end(), c);
}

void rect(Image& im, int x, int y, int w, int h, Color c) {
    if (c.a < 255) {
        blendRect(im, x, y, w, h, c);
        return;
    }
    const int x0 = std::max(x, 0), x1 = std::min(x + w, im.width);
    for (int yy = std::max(y, 0); yy < std::min(y + h, im.height); ++yy) {
        std::fill_n(im.pixels.begin() + std::size_t(yy) * im.width + x0, std::max(0, x1 - x0), c);
    }
}

void blendRect(Image& im, int x, int y, int w, int h, Color c) {
    const int a = c.a;
    const int x0 = std::max(x, 0), x1 = std::min(x + w, im.width);
    for (int yy = std::max(y, 0); yy < std::min(y + h, im.height); ++yy) {
        Color* row = im.pixels.data() + std::size_t(yy) * im.width;
        for (int xx = x0; xx < x1; ++xx) {
            auto& p = row[xx];
            if (p.a == 255) {
                // Over an opaque pixel (most pages): the general formula
                // below with keep = 255 - a and out = 255, divided by a
                // constant, which is much faster.
                auto over = [&](int src, int dst) {
                    return std::uint8_t((src * a + dst * (255 - a)) / 255);
                };
                p = {over(c.r, p.r), over(c.g, p.g), over(c.b, p.b), 255};
                continue;
            }
            const int keep = p.a * (255 - a) / 255, out = a + keep;
            if (!out) {
                continue;
            }
            auto channel = [&](int src, int dst) {
                return std::uint8_t((src * a + dst * keep) / out);
            };
            p = {channel(c.r, p.r), channel(c.g, p.g), channel(c.b, p.b), std::uint8_t(out)};
        }
    }
}

void rounded(Image& im, int x, int y, int w, int h, int radius, Color c) {
    radius = std::max(0, std::min({radius, w / 2, h / 2}));
    for (int row = 0; row < h; ++row) {
        const int d = std::min(row, h - 1 - row);
        int inset = 0;
        if (d < radius) {
            const double dy = radius - d - 0.5;
            inset = radius - int(std::lround(std::sqrt(double(radius) * radius - dy * dy)));
        }
        rect(im, x + inset, y + row, w - 2 * inset, 1, c);
    }
}

void picture(Image& im, const Image& source, int x, int y, int w, int h, int opacity) {
    if (w <= 0 || h <= 0 || source.width == 0) {
        return;
    }
    if (source.width == w && source.height == h) {
        // Already the right size (a cached, pre-scaled picture): copy it,
        // row by row when it is not faded (mix at 255 is the source itself).
        const int x0 = std::max(x, 0), x1 = std::min(x + w, im.width);
        if (opacity >= 255) {
            for (int yy = std::max(y, 0); yy < std::min(y + h, im.height) && x0 < x1; ++yy) {
                const Color* from = source.pixels.data() + std::size_t(yy - y) * source.width;
                std::copy(from + (x0 - x), from + (x1 - x),
                          im.pixels.data() + std::size_t(yy) * im.width + x0);
            }
            return;
        }
        for (int yy = std::max(y, 0); yy < std::min(y + h, im.height); ++yy) {
            for (int xx = std::max(x, 0); xx < std::min(x + w, im.width); ++xx) {
                auto& p = im.pixels[std::size_t(yy) * im.width + xx];
                p = mix(p, source.at(xx - x, yy - y), opacity);
            }
        }
        return;
    }
    // Crop the source to the target's aspect ratio, centred.
    double cropW = source.width, cropH = source.height;
    if (cropW / cropH > double(w) / h) {
        cropW = cropH * w / h;
    } else {
        cropH = cropW * h / w;
    }
    const double x0 = (source.width - cropW) / 2, y0 = (source.height - cropH) / 2;
    for (int yy = std::max(y, 0); yy < std::min(y + h, im.height); ++yy) {
        const double sy =
            std::clamp(y0 + (yy - y + 0.5) * cropH / h - 0.5, 0.0, double(source.height - 1));
        const int sy0 = int(sy), sy1 = std::min(sy0 + 1, source.height - 1);
        const double fy = sy - sy0;
        for (int xx = std::max(x, 0); xx < std::min(x + w, im.width); ++xx) {
            const double sx =
                std::clamp(x0 + (xx - x + 0.5) * cropW / w - 0.5, 0.0, double(source.width - 1));
            const int sx0 = int(sx), sx1 = std::min(sx0 + 1, source.width - 1);
            const double fx = sx - sx0;
            auto lerp = [&](auto channel) {
                const double top =
                    channel(source.at(sx0, sy0)) * (1 - fx) + channel(source.at(sx1, sy0)) * fx;
                const double bottom =
                    channel(source.at(sx0, sy1)) * (1 - fx) + channel(source.at(sx1, sy1)) * fx;
                return top * (1 - fy) + bottom * fy;
            };
            const Color s{std::uint8_t(lerp([](Color c) { return c.r; }) + 0.5),
                          std::uint8_t(lerp([](Color c) { return c.g; }) + 0.5),
                          std::uint8_t(lerp([](Color c) { return c.b; }) + 0.5)};
            auto& p = im.pixels[std::size_t(yy) * im.width + xx];
            p = mix(p, s, opacity);
        }
    }
}

Image scaled(const Image& source, int w, int h) {
    Image out(std::max(w, 0), std::max(h, 0));
    if (source.width == 0 || source.height == 0) {
        return out;
    }
    // Each target pixel averages the source area it covers, weighting colour
    // by alpha so transparent pixels do not darken the edges.
    const double fx = double(source.width) / w, fy = double(source.height) / h;
    for (int y = 0; y < h; ++y) {
        const double y0 = y * fy, y1 = (y + 1) * fy;
        for (int x = 0; x < w; ++x) {
            const double x0 = x * fx, x1 = (x + 1) * fx;
            double r = 0, g = 0, b = 0, a = 0, area = 0;
            for (int sy = int(y0); sy < std::min(int(std::ceil(y1)), source.height); ++sy) {
                const double wy = std::min(y1, sy + 1.0) - std::max(y0, double(sy));
                for (int sx = int(x0); sx < std::min(int(std::ceil(x1)), source.width); ++sx) {
                    const double weight = wy * (std::min(x1, sx + 1.0) - std::max(x0, double(sx)));
                    const Color p = source.at(sx, sy);
                    const double alpha = weight * p.a;
                    r += p.r * alpha;
                    g += p.g * alpha;
                    b += p.b * alpha;
                    a += alpha;
                    area += weight;
                }
            }
            if (a > 0) {
                out.pixels[std::size_t(y) * w + x] = {
                    std::uint8_t(std::lround(r / a)), std::uint8_t(std::lround(g / a)),
                    std::uint8_t(std::lround(b / a)), std::uint8_t(std::lround(a / area))};
            } else {
                out.pixels[std::size_t(y) * w + x] = {0, 0, 0, 0};
            }
        }
    }
    return out;
}

void blend(Image& im, const Image& source, int x, int y) {
    for (int yy = std::max(y, 0); yy < std::min(y + source.height, im.height); ++yy) {
        for (int xx = std::max(x, 0); xx < std::min(x + source.width, im.width); ++xx) {
            const Color s = source.at(xx - x, yy - y);
            if (s.a == 0) {
                continue;
            }
            auto& p = im.pixels[std::size_t(yy) * im.width + xx];
            const int keep = p.a * (255 - s.a) / 255, out = s.a + keep;
            auto channel = [&](int src, int dst) {
                return std::uint8_t((src * s.a + dst * keep) / out);
            };
            p = {channel(s.r, p.r), channel(s.g, p.g), channel(s.b, p.b), std::uint8_t(out)};
        }
    }
}

// --- Canvas ----------------------------------------------------------------

int Canvas::px(float v) const {
    return int(std::lround(v * scale_));
}

FontSize Canvas::font(float size) const {
    return fontSize(std::max(1, int(std::lround(size * kEmUnits * scale_))));
}

namespace {
// A shared 24-unit outline alphabet. Rounded strokes are rasterized at the
// actual device density; coverage at their edges keeps small controls smooth.
struct IconPoint {
    float x, y;
};

using IconPath = std::vector<IconPoint>;

std::vector<IconPath> iconPaths(unsigned char code) {
    std::vector<IconPath> paths;
    auto path = [&](std::initializer_list<IconPoint> points) {
        paths.emplace_back(points);
    };
    auto arc = [&](float x, float y, float radius, float start, float end) {
        IconPath points;
        for (int i = 0; i <= 48; ++i) {
            const float angle = (start + (end - start) * i / 48) * 3.14159265f / 180;
            points.push_back({x + radius * std::cos(angle), y + radius * std::sin(angle)});
        }
        paths.push_back(std::move(points));
    };
    switch (code) {
    case 1:
        path({{19, 12}, {5, 12}});
        path({{11, 6}, {5, 12}, {11, 18}});
        break;
    case 2:
        path({{5, 12}, {19, 12}});
        path({{13, 6}, {19, 12}, {13, 18}});
        break;
    case 3:
        path({{6, 15}, {12, 9}, {18, 15}});
        break;
    case 4:
        path({{6, 9}, {12, 15}, {18, 9}});
        break;
    case 5:
        path({{5, 12}, {10, 17}, {19, 7}});
        break;
    case 8:
        arc(10, 10, 6, 0, 360);
        path({{14.5f, 14.5f}, {20, 20}});
        break;
    case 16: // Jigsaw piece with two rounded tabs.
        path({{4, 20}, {4, 13}, {6, 13}});
        arc(6, 10, 3, 90, -90);
        path({{6, 7}, {4, 7}, {4, 4}, {9, 4}});
        arc(12, 4, 3, 180, 360);
        path({{15, 4}, {20, 4}, {20, 9}});
        arc(20, 12, 3, -90, -270);
        path({{20, 15}, {20, 20}, {13, 20}});
        arc(10, 20, 3, 0, -180);
        path({{7, 20}, {4, 20}});
        break;
    case 17: // Gamepad, with a direction cross and two action buttons.
        path({{8, 6},
              {16, 6},
              {19, 8},
              {22, 18},
              {20, 20},
              {17, 16},
              {7, 16},
              {4, 20},
              {2, 18},
              {5, 8},
              {8, 6}});
        path({{6, 11}, {10, 11}});
        path({{8, 9}, {8, 13}});
        arc(16, 10, 0.65f, 0, 360);
        arc(18, 13, 0.65f, 0, 360);
        break;
    case 18:
        path({{4, 3}, {20, 3}, {20, 21}, {4, 21}, {4, 3}});
        path({{8, 3}, {8, 21}});
        path({{16, 3}, {16, 21}});
        for (float y : {7.f, 12.f, 17.f}) {
            path({{4, y}, {8, y}});
            path({{16, y}, {20, y}});
        }
        break;
    case 19:
        path({{9, 17}, {9, 5}, {20, 3}, {20, 15}});
        path({{9, 9}, {20, 7}});
        arc(6, 18, 3, 0, 360);
        arc(17, 16, 3, 0, 360);
        break;
    case 20:
        arc(12, 12, 8, -140, 140);
        path({{5.9f, 6.9f}, {5.9f, 2.5f}});
        path({{5.9f, 6.9f}, {10.3f, 6.9f}});
        break;
    }
    return paths;
}

// Only drawn labels are cached; wrapping measurements do not allocate bitmaps.
bool isIcon(unsigned char c) {
    return c > 0 && c < 32;
}

const platform::TypesetLine& label(std::string_view value, int pixels, bool bold) {
    static std::map<std::tuple<std::string, int, bool>, platform::TypesetLine> cache;
    static std::size_t bytes = 0;
    const auto key = std::tuple{std::string(value), pixels, bold};
    if (auto it = cache.find(key); it != cache.end()) {
        return it->second;
    }
    auto line = platform::typeset(value, pixels, bold);
    const auto cost = line.coverage.pixels.size() * sizeof(Color);
    if (bytes + cost > 16 * 1024 * 1024) {
        cache.clear();
        bytes = 0;
    }
    bytes += cost;
    return cache.emplace(key, std::move(line)).first->second;
}
} // namespace

float Canvas::textWidth(std::string_view utf8, float size) const {
    int width = 0;
    const int pixels = std::max(1, px(size * kEmUnits));
    for (std::size_t i = 0; i < utf8.size();) {
        if (isIcon(static_cast<unsigned char>(utf8[i]))) {
            width += pixels;
            ++i;
        } else {
            auto end = i + 1;
            while (end < utf8.size() && !isIcon(static_cast<unsigned char>(utf8[end]))) {
                ++end;
            }
            width += platform::textAdvance(utf8.substr(i, end - i), pixels, size >= 2.5f);
            i = end;
        }
    }
    return width / scale_;
}

float Canvas::capHeight(float size) const {
    return px(size * kEmUnits * 0.72f) / scale_;
}

float Canvas::text(std::string_view utf8, float x, float y, float size, Color color) {
    int left = px(x);
    const int pixels = std::max(1, px(size * kEmUnits));
    for (std::size_t i = 0; i < utf8.size();) {
        if (isIcon(static_cast<unsigned char>(utf8[i]))) {
            icon(utf8.substr(i++, 1), left / scale_, y + (capHeight(size) - pixels / scale_) / 2,
                 pixels / scale_, color);
            left += pixels;
        } else {
            auto end = i + 1;
            while (end < utf8.size() && !isIcon(static_cast<unsigned char>(utf8[end]))) {
                ++end;
            }
            const auto& run = label(utf8.substr(i, end - i), pixels, size >= 2.5f);
            for (int yy = 0; yy < run.coverage.height; ++yy) {
                for (int xx = 0; xx < run.coverage.width; ++xx) {
                    auto ink = color;
                    ink.a = std::uint8_t(int(ink.a) * run.coverage.at(xx, yy).a / 255);
                    if (ink.a) {
                        blendRect(image_, left + xx, px(y) - run.capOffset + yy, 1, 1, ink);
                    }
                }
            }
            left += run.advance;
            i = end;
        }
    }
    return left / scale_;
}

void Canvas::icon(std::string_view symbol, float x, float y, float side, Color color) {
    if (symbol.empty() || side <= 0) {
        return;
    }
    const auto paths = iconPaths(static_cast<unsigned char>(symbol.front()));
    const float unit = side * scale_ / 24;
    const float originX = x * scale_, originY = y * scale_;
    const float radius = 0.95f * unit;
    for (int py = std::max(0, int(std::floor(originY)));
         py < std::min(image_.height, int(std::ceil(originY + side * scale_))); ++py) {
        for (int px = std::max(0, int(std::floor(originX)));
             px < std::min(image_.width, int(std::ceil(originX + side * scale_))); ++px) {
            float distance = 100000;
            for (const auto& path : paths) {
                for (std::size_t i = 1; i < path.size(); ++i) {
                    const float ax = originX + path[i - 1].x * unit,
                                ay = originY + path[i - 1].y * unit;
                    const float dx = (path[i].x - path[i - 1].x) * unit,
                                dy = (path[i].y - path[i - 1].y) * unit;
                    const float t = std::clamp(((px + .5f - ax) * dx + (py + .5f - ay) * dy) /
                                                   std::max(.00001f, dx * dx + dy * dy),
                                               0.f, 1.f);
                    distance = std::min(distance,
                                        std::hypot(px + .5f - ax - t * dx, py + .5f - ay - t * dy));
                }
            }
            auto ink = color;
            ink.a = std::uint8_t(color.a * std::clamp(radius + .5f - distance, 0.f, 1.f));
            if (ink.a) {
                blendRect(image_, px, py, 1, 1, ink);
            }
        }
    }
}

float Canvas::textRight(std::string_view utf8, float right, float y, float size, Color color) {
    return text(utf8, right - textWidth(utf8, size), y, size, color);
}

float Canvas::textCentred(std::string_view utf8, float centreX, float y, float size, Color color) {
    return text(utf8, centreX - textWidth(utf8, size) / 2, y, size, color);
}

std::vector<std::string_view> Canvas::lines(std::string_view utf8, float width, float size) const {
    std::vector<std::string_view> result;
    while (!utf8.empty()) {
        std::size_t take = utf8.size();
        while (take > 0 && textWidth(utf8.substr(0, take), size) > width) {
            const auto split = utf8.rfind(' ', take - 1);
            take = split == std::string_view::npos || split == 0 ? take - 1 : split;
        }
        take = std::max<std::size_t>(take, 1);
        // Do not cut a UTF-8 sequence in half.
        while (take < utf8.size() && (std::uint8_t(utf8[take]) & 0xc0) == 0x80) {
            ++take;
        }
        result.push_back(utf8.substr(0, take));
        utf8.remove_prefix(take);
        while (!utf8.empty() && utf8.front() == ' ') {
            utf8.remove_prefix(1);
        }
    }
    return result;
}

int Canvas::wrapped(std::string_view utf8, float x, float y, float width, int maxLines, float size,
                    Color color) {
    const auto all = lines(utf8, width, size);
    const int count = std::min(int(all.size()), maxLines);
    for (int i = 0; i < count; ++i) {
        text(all[i], x, y + i * lineHeight(size), size, color);
    }
    return count;
}

float Canvas::lineHeight(float size) const {
    return px(size * kEmUnits * kLineEms) / scale_;
}

std::string Canvas::ellipsize(std::string_view utf8, float width, float size) const {
    if (textWidth(utf8, size) <= width) {
        return std::string(utf8);
    }
    std::string shortened(utf8);
    while (!shortened.empty() && textWidth(shortened + "...", size) > width) {
        popCodePoint(shortened);
    }
    return shortened + "...";
}

void Canvas::fill(Color color) {
    style::fill(image_, color);
}

void Canvas::rect(float x, float y, float w, float h, Color color) {
    const int x0 = px(x), y0 = px(y);
    style::rect(image_, x0, y0, px(x + w) - x0, px(y + h) - y0, color);
}

void Canvas::rounded(float x, float y, float w, float h, float radius, Color color) {
    const int x0 = px(x), y0 = px(y);
    style::rounded(image_, x0, y0, px(x + w) - x0, px(y + h) - y0, px(radius), color);
}

void Canvas::frame(float x, float y, float w, float h, float radius, float thickness, Color edge,
                   Color inside) {
    const int x0 = px(x), y0 = px(y), w0 = px(x + w) - x0, h0 = px(y + h) - y0;
    const int t = std::max(1, px(thickness)), r = px(radius);
    style::rounded(image_, x0, y0, w0, h0, r, edge);
    style::rounded(image_, x0 + t, y0 + t, w0 - 2 * t, h0 - 2 * t, std::max(0, r - t), inside);
}

void Canvas::blend(const Image& source, float x, float y) {
    style::blend(image_, source, px(x), px(y));
}

void Canvas::picture(const Image& source, float x, float y, float w, float h, int opacity) {
    // Sized by its length, not by its rounded edges, so a picture keeps its
    // pixel size wherever it lands: a pre-scaled copy (cached at px(w) x
    // px(h)) is then copied rather than resampled while a page scrolls.
    style::picture(image_, source, px(x), px(y), px(w), px(h), opacity);
}

} // namespace picarat::shell::style
