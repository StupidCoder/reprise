// See graphics.hpp.
#include "graphics/graphics.hpp"
#include "graphics/ds_pipeline.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace picarat {
namespace {
class Reader {
public:
    explicit Reader(Bytes bytes, const char* magic) : data(std::move(bytes)) {
        if (data.size() < 8 || !std::equal(data.begin(), data.begin() + 4, magic)) {
            throw std::runtime_error("native asset header");
        }
        version = read32(data, 4);
        if (version < 1 || version > 5) {
            throw std::runtime_error("native asset version");
        }
        cursor = 8;
    }

    std::uint32_t word() {
        auto v = read32(data, cursor);
        cursor += 4;
        return v;
    }

    std::int32_t integer() { return std::bit_cast<std::int32_t>(word()); }

    std::int16_t half() {
        auto v = read16(data, cursor);
        cursor += 2;
        return std::bit_cast<std::int16_t>(v);
    }

    Bytes take(std::size_t count) {
        if (cursor > data.size() || count > data.size() - cursor) {
            throw std::runtime_error("native asset truncated");
        }
        Bytes out(data.begin() + cursor, data.begin() + cursor + count);
        cursor += count;
        return out;
    }

    std::string string() {
        auto bytes = take(word());
        return {bytes.begin(), bytes.end()};
    }

    Surface image() {
        Surface out;
        out.width = word();
        out.height = word();
        if (!out.width || !out.height || out.width > 2048 || out.height > 2048) {
            throw std::runtime_error("image dimensions");
        }
        auto bytes = take(out.width * out.height * 2);
        out.pixels.resize(out.width * out.height);
        for (std::size_t i = 0; i < out.pixels.size(); ++i) {
            out.pixels[i] = read16(bytes, i * 2);
        }
        return out;
    }

    void end() {
        if (cursor != data.size()) {
            throw std::runtime_error("unparsed native asset tail");
        }
    }

    unsigned version = 1;

private:
    Bytes data;
    std::size_t cursor = 0;
};
} // namespace

const Surface& GraphicsAssets::background(const std::string& name) {
    auto found = backgrounds_.find(name);
    if (found != backgrounds_.end()) {
        return found->second;
    }
    Reader r(readFile(root / "bg" / (name + ".l2bg")), "L2BG");
    auto image = r.image();
    if (r.version >= 2) {
        auto tile = r.image();
        if (tile.width != 8 || tile.height != 8) {
            throw std::runtime_error("background tile size");
        }
        backgroundTiles_.emplace(name, std::move(tile));
    }
    r.end();
    return backgrounds_.emplace(name, std::move(image)).first->second;
}

const Sprite& GraphicsAssets::sprite(const std::string& name) {
    auto found = sprites_.find(name);
    if (found != sprites_.end()) {
        return found->second;
    }
    Reader r(readFile(root / "ani" / (name + ".l2sp")), "L2SP");
    Sprite sprite;
    auto count = r.word();
    if (count > 4096) {
        throw std::runtime_error("sprite image count");
    }
    for (unsigned i = 0; i < count; ++i) {
        sprite.images.push_back(r.image());
    }
    count = r.word();
    if (count > 4096) {
        throw std::runtime_error("animation count");
    }
    for (unsigned i = 0; i < count; ++i) {
        auto label = r.string();
        auto size = r.word();
        if (size > 65536) {
            throw std::runtime_error("animation length");
        }
        sprite.animationNames[label] = i;
        std::vector<AnimationStep> steps;
        for (unsigned j = 0; j < size; ++j) {
            AnimationStep s{r.word(), r.word(), r.word()};
            if (s.next >= size || s.image >= sprite.images.size()) {
                throw std::runtime_error("animation reference");
            }
            steps.push_back(s);
        }
        sprite.animations.push_back(std::move(steps));
    }
    count = r.word();
    if (count > 16) {
        throw std::runtime_error("sprite variables");
    }
    for (unsigned i = 0; i < count; ++i) {
        auto name = r.string();
        std::array<std::int16_t, 8> values;
        for (auto& value : values) {
            value = r.half();
        }
        sprite.variables[name] = values;
    }
    if (r.version >= 2) {
        sprite.child = r.string();
        for (std::size_t i = 0; i < sprite.animations.size(); ++i) {
            sprite.attachments.push_back({r.half(), r.half(), r.word()});
        }
    }
    if (r.version == 3 || r.version == 5) {
        for (const auto& image : sprite.images) {
            auto n = r.word();
            if (n > 4096) {
                throw std::runtime_error("sprite chunk count");
            }
            std::vector<Rect> chunks;
            for (unsigned i = 0; i < n; ++i) {
                Rect c{r.integer(), r.integer(), r.integer(), r.integer()};
                if (c.x < 0 || c.y < 0 || c.width <= 0 || c.height <= 0 ||
                    c.x + c.width > int(image.width) || c.y + c.height > int(image.height)) {
                    throw std::runtime_error("rotating sprite chunk bounds");
                }
                chunks.push_back(c);
            }
            sprite.chunks.push_back(std::move(chunks));
        }
    }
    if (r.version >= 4) {
        for (std::size_t k = 0; k < sprite.images.size(); ++k) {
            // Texture-chunk records of each frame (02074458 draws one quad per record).
            auto n = r.word();
            if (n > 4096) {
                throw std::runtime_error("sprite quad count");
            }
            std::vector<Rect> quads;
            for (unsigned i = 0; i < n; ++i) {
                Rect c{r.integer(), r.integer(), r.integer(), r.integer()};
                if (c.x < 0 || c.y < 0 || c.width <= 0 || c.height <= 0 || c.width > 1024 ||
                    c.height > 1024) {
                    throw std::runtime_error("sprite quad bounds");
                }
                quads.push_back(c);
            }
            sprite.quads.push_back(std::move(quads));
        }
    }
    r.end();
    return sprites_.emplace(name, std::move(sprite)).first->second;
}

const Font& GraphicsAssets::font(const std::string& name) {
    auto found = fonts_.find(name);
    if (found != fonts_.end()) {
        return found->second;
    }
    Reader r(readFile(root / "font" / (name + ".l2ft")), "L2FT");
    Font font;
    auto count = r.word();
    if (count > 65536) {
        throw std::runtime_error("font size");
    }
    for (unsigned i = 0; i < count; ++i) {
        auto code = r.word();
        Glyph g;
        g.width = r.word();
        g.height = r.word();
        g.left = r.integer();
        g.advance = r.integer();
        if (g.width > 64 || g.height > 64) {
            throw std::runtime_error("glyph bounds");
        }
        g.alpha = r.take(g.width * g.height);
        font.glyphs[code] = std::move(g);
    }
    if (r.version == 2) {
        font.textWidths = r.take(224);
    }
    r.end();
    return fonts_.emplace(name, std::move(font)).first->second;
}

std::array<Bytes, 4> GraphicsAssets::keyboard() const {
    Reader r(readFile(root / "keyboard.bin"), "L2KB");
    std::array<Bytes, 4> result;
    for (auto& keys : result) {
        keys = r.take(r.word());
    }
    r.end();
    return result;
}

void blit(Surface& dst, const Surface& src, int x, int y, int foregroundWeight,
          int backgroundWeight) {
    for (unsigned sy = 0; sy < src.height; ++sy) {
        for (unsigned sx = 0; sx < src.width; ++sx) {
            int dx = x + int(sx), dy = y + int(sy);
            if (dx < 0 || dy < 0 || dx >= int(dst.width) || dy >= int(dst.height)) {
                continue;
            }
            auto color = src.pixels[sy * src.width + sx];
            if (!(color & 0x8000)) {
                continue;
            }
            if (foregroundWeight != 16 || backgroundWeight != 0) {
                std::uint16_t shaded = 0x8000;
                auto behind = dst.pixels[dy * dst.width + dx];
                // The ROM sets SUB BLDALPHA to (n,15-n), including (15,0).
                // DS blending rounds at 6-bit precision; project to RGB555 only
                // afterwards. This explains the formerly unresolved logo palette.
                for (int shift : {0, 5, 10}) {
                    int front = (color >> shift) & 31, back = (behind >> shift) & 31;
                    int sixBit =
                        (2 * front * foregroundWeight + 2 * back * backgroundWeight + 8) / 16;
                    shaded |= (std::min(63, sixBit) / 2) << shift;
                }
                color = shaded;
            }
            dst.pixels[dy * dst.width + dx] = color;
        }
    }
}

std::uint16_t blend3D(std::uint16_t color, int alpha, std::uint16_t behind) {
    if (alpha >= 31) {
        return std::uint16_t(color | 0x8000);
    }
    if (alpha < 1) {
        throw std::runtime_error("3D alpha outside reviewed range");
    }
    std::uint16_t result = 0x8000;
    for (int shift : {0, 5, 10}) {
        const int front = (color >> shift) & 31, back = (behind >> shift) & 31;
        const int sixBit =
            ((2 * front + (front != 0)) * (alpha + 1) + 2 * back * (31 - alpha) + 16) / 32;
        result |= std::uint16_t((sixBit / 2) << shift);
    }
    return result;
}

void blitNarrationPolygon(Surface& dst, const Surface& src, int x, int y, int alpha) {
    // ov2 02075e50 -> 02074280 -> 0206e824 writes alpha to POLYGON_ATTR.
    // Alpha zero is the hardware wireframe mode, outside the reviewed page.
    if (alpha < 1 || alpha > 31) {
        throw std::runtime_error("narration polygon alpha outside reviewed range");
    }
    for (unsigned sy = 0; sy < src.height; ++sy) {
        for (unsigned sx = 0; sx < src.width; ++sx) {
            int dx = x + int(sx), dy = y + int(sy);
            if (dx < 0 || dy < 0 || dx >= int(dst.width) || dy >= int(dst.height)) {
                continue;
            }
            const auto color = src.pixels[sy * src.width + sx];
            if (!(color & 0x8000)) {
                continue;
            }
            auto& behind = dst.pixels[dy * dst.width + dx];
            std::uint16_t result = 0x8000;
            for (int shift : {0, 5, 10}) {
                const int front = (color >> shift) & 31, back = (behind >> shift) & 31;
                // Palette texture expands to 6-bit as 2*c + (c != 0), while
                // the 2D backdrop expands as 2*c. Project only after blending.
                const int sixBit =
                    ((2 * front + (front != 0)) * (alpha + 1) + 2 * back * (31 - alpha) + 16) / 32;
                result |= (sixBit / 2) << shift;
            }
            behind = result;
        }
    }
}

void drawGlyph(Surface& dst, const Font& font, unsigned character, int x, int y) {
    if (character == 0 || character == 32) {
        return;
    }
    auto it = font.glyphs.find(character);
    if (it == font.glyphs.end()) {
        return;
    }
    const auto& g = it->second;
    // ov2:02073728 centers the advance within a 9-pixel character cell.
    x += (9 - g.advance) / 2 + g.left;
    for (unsigned gy = 0; gy < g.height; ++gy) {
        for (unsigned gx = 0; gx < g.width; ++gx) {
            int px = x + int(gx), py = y + int(gy);
            if (px >= 0 && py >= 0 && px < int(dst.width) && py < int(dst.height) &&
                g.alpha[gy * g.width + gx]) {
                dst.pixels[py * dst.width + px] = 0x8000;
            }
        }
    }
}

void applyBrightness(Surface& image, int top, int bottom) {
    for (unsigned y = 0; y < image.height; ++y) {
        for (unsigned x = 0; x < image.width; ++x) {
            int level = std::clamp(y < 192 ? top : bottom, -16, 16);
            if (!level) {
                continue;
            }
            auto& color = image.pixels[y * image.width + x];
            std::uint16_t result = 0x8000;
            // Master brightness on the 6-bit colour c6 = 2c (2D layers): darken
            // c6*(16-L)>>4 (measured on all 16 levels, ref-tea-b), brighten
            // c6+((63-c6)*L>>4) (the ov7 photo flash, ref-room-a).
            for (int shift : {0, 5, 10}) {
                const int c6 = ((color >> shift) & 31) * 2;
                const int r =
                    level < 0 ? (c6 * (16 + level)) >> 4 : c6 + (((63 - c6) * level) >> 4);
                result |= std::uint16_t((r >> 1) << shift);
            }
            color = result;
        }
    }
}

Bytes rgba(const Surface& image) {
    Bytes out(image.pixels.size() * 4);
    std::size_t pos = 0;
    for (auto color : image.pixels) {
        for (int shift : {0, 5, 10}) {
            unsigned value = (color >> shift) & 31;
            out[pos++] = (value << 3) | (value >> 2);
        }
        out[pos++] = 255;
    }
    return out;
}

void writePPM(const Surface& image, const std::filesystem::path& path) {
    std::ofstream out(path, std::ios::binary);
    out << "P6\n" << image.width << ' ' << image.height << "\n255\n";
    auto pixels = rgba(image);
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        out.write(reinterpret_cast<const char*>(pixels.data() + i), 3);
    }
    if (!out) {
        throw std::runtime_error("cannot write capture");
    }
}
} // namespace picarat

namespace picarat {
void blitRotated(Surface& dst, const Surface& src, int cx, int cy, float degrees, int clipTop,
                 std::span<const Rect> chunks) {
    // Original angle is truncated to u16 turns, then the sine table drops 4 bits.
    float positive = degrees < 0 ? degrees + 360 : degrees;
    blitRotatedTurns(dst, src, cx, cy, unsigned(positive * 65536.0f / 360.0f) & 65535, clipTop,
                     chunks);
}

void blitRotatedTurns(Surface& dst, const Surface& src, int cx, int cy, unsigned turns, int clipTop,
                      std::span<const Rect> chunks) {
    // ov2 0206ecfc: each chunk is a textured square through the 3D pipeline.
    static const dspipe::SpriteCamera camera = dspipe::spriteCamera();
    const int W = int(src.width), H = int(src.height);
    const Rect whole{0, 0, W, H};
    if (chunks.empty()) {
        chunks = std::span<const Rect>(&whole, 1);
    }
    for (const auto& q : chunks) {
        dspipe::drawTurnedChunk(camera, cx, cy - clipTop, turns, W, H, q.x, q.y, q.width, q.height,
                                [&](const dspipe::Fragment& f, int u, int v) {
                                    const int px = q.x + u, py = q.y + v, y = f.y + clipTop;
                                    if (px >= W || py >= H || f.x < 0 || f.x >= int(dst.width) ||
                                        y < clipTop || y >= int(dst.height) || y >= clipTop + 192) {
                                        return; // texture outside the frame is transparent
                                    }
                                    const auto pixel =
                                        src.pixels[std::size_t(py) * src.width + std::size_t(px)];
                                    if (pixel & 0x8000) {
                                        dst.pixels[std::size_t(y) * dst.width + std::size_t(f.x)] =
                                            pixel;
                                    }
                                });
    }
}
} // namespace picarat
