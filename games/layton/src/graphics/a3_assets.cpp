// The third adventure's pictures, animations, fonts and positions. See
// a3_assets.hpp.
#include "graphics/a3_assets.hpp"
#include "import/asset_codecs.hpp"
#include <cstring>
#include <stdexcept>

namespace picarat::adventure3 {
namespace {
using import::adventure3::Lscr;
using import::adventure3::LscrCommand;

int integer(const LscrCommand& c, size_t i) {
    if (i >= c.operands.size()) {
        throw std::runtime_error("missing animation operand");
    }
    const auto& o = c.operands[i];
    if (o.type == 1) {
        float f;
        std::memcpy(&f, &o.value, 4);
        return int(f);
    }
    return int(std::int32_t(o.value));
}

// The game's spelling of a file name as the pack's: language folder and
// expanded extensions.
std::string packPath(std::string path) {
    for (size_t at; (at = path.find("/?/")) != std::string::npos;) {
        path.replace(at, 3, "/ge/");
    }
    auto swap = [&](const char* from, const char* to) {
        const size_t n = std::strlen(from);
        if (path.size() >= n && path.compare(path.size() - n, n, from) == 0) {
            path.replace(path.size() - n, n, to);
        }
    };
    swap(".cimg", ".limg");
    swap(".cbin", ".lbin");
    swap(".cani", "");
    swap(".cfnt", ".lfnt");
    return path;
}
} // namespace

const Animation::Sequence* Animation::sequence(const std::string& name) const {
    for (const auto& s : sequences) {
        if (s.name == name) {
            return &s;
        }
    }
    return nullptr;
}

void Animation::drawFrame(Surface& image, unsigned frame, int x, int y) const {
    if (frame >= frames.size()) {
        return;
    }
    const Frame& f = frames[frame];
    for (const auto& l : f.layers) {
        const Picture& p = pictures.at(l.picture);
        const Rect& part = p.parts.at(l.part);
        for (int sy = 0; sy < part.height; ++sy) {
            for (int sx = 0; sx < part.width; ++sx) {
                const int px = part.x + sx, py = part.y + sy;
                if (px >= int(p.image.width) || py >= int(p.image.height)) {
                    continue;
                }
                const std::size_t at = size_t(py) * p.image.width + size_t(px);
                std::uint16_t c = p.image.pixels[at];
                const int dx = x + l.x + sx, dy = y + l.y + sy;
                if ((c & 0x8000) && dx >= 0 && dy >= 0 && dx < int(image.width) &&
                    dy < int(image.height)) {
                    std::uint16_t& d = image.pixels[size_t(dy) * image.width + size_t(dx)];
                    // A translucent pixel over a drawn one: the 3D engine's
                    // blend, (c * (a + 1) + d * (31 - a)) / 32 per channel
                    // (the hardware's formula; which engine draws these
                    // pictures in the game is not checked).
                    const unsigned a = p.alpha.empty() ? 31 : p.alpha[at];
                    if (a < 31 && (d & 0x8000)) {
                        std::uint16_t mixed = 0x8000;
                        for (int shift : {0, 5, 10}) {
                            const unsigned cs = (c >> shift) & 31, ds = (d >> shift) & 31;
                            mixed |= std::uint16_t(((cs * (a + 1) + ds * (31 - a)) / 32) << shift);
                        }
                        c = mixed;
                    }
                    d = c;
                }
            }
        }
    }
}

bool Picture::tileUsed(int x, int y) const {
    if (x < 0 || y < 0 || x >= int(image.width) || y >= int(image.height)) {
        return false;
    }
    const int tx = x & ~7, ty = y & ~7;
    for (int sy = ty; sy < std::min(ty + 8, int(image.height)); ++sy) {
        for (int sx = tx; sx < std::min(tx + 8, int(image.width)); ++sx) {
            if (image.pixels[size_t(sy) * image.width + size_t(sx)] & 0x8000) {
                return true;
            }
        }
    }
    return false;
}

bool Animation::touches(unsigned frame, int x, int y) const {
    if (frame >= frames.size()) {
        return false;
    }
    for (const auto& l : frames[frame].layers) {
        const Picture& p = pictures.at(l.picture);
        const Rect& part = p.parts.at(l.part);
        const int sx = x - l.x, sy = y - l.y;
        if (sx >= 0 && sy >= 0 && sx < part.width && sy < part.height &&
            p.tileUsed(part.x + sx, part.y + sy)) {
            return true;
        }
    }
    return false;
}

Picture decodePicture(std::span<const std::uint8_t> data) {
    const auto limg = import::adventure3::parseLimg(data);
    Picture p;
    p.image.width = limg.width;
    p.image.height = limg.height;
    p.image.pixels.assign(size_t(limg.width) * limg.height, 0);
    for (unsigned y = 0; y < limg.height; ++y) {
        for (unsigned x = 0; x < limg.width; ++x) {
            const auto px = limg.pixel(x, y);
            if (px.alpha && px.index < limg.palette.size()) {
                p.image.pixels[size_t(y) * limg.width + x] =
                    std::uint16_t((limg.palette[px.index] & 0x7fff) | 0x8000);
                if (px.alpha < 31) {
                    p.alpha.resize(p.image.pixels.size(), 31);
                }
                if (!p.alpha.empty()) {
                    p.alpha[size_t(y) * limg.width + x] = px.alpha;
                }
            }
        }
    }
    for (const auto& part : limg.parts) {
        p.parts.push_back(
            {int(part.x * 8), int(part.y * 8), int(part.width * 8), int(part.height * 8)});
    }
    return p;
}

Assets::Assets(std::filesystem::path path) : root(std::move(path)) {}

std::filesystem::path Assets::file(const std::string& path) const {
    return root / packPath(path);
}

bool Assets::exists(const std::string& path) const {
    return fileExists(file(path));
}

std::string Assets::codeText(int overlay, std::uint32_t load, std::uint32_t address) const {
    const auto code = readFile(root / "code" / ("overlay" + std::to_string(overlay) + ".bin"));
    std::string out;
    for (std::size_t i = address - load; i < code.size() && code[i]; ++i) {
        out += char(code[i]);
    }
    return out;
}

std::uint32_t Assets::codeWord(int overlay, std::uint32_t load, std::uint32_t address) const {
    const auto code = readFile(root / "code" / ("overlay" + std::to_string(overlay) + ".bin"));
    const std::size_t i = address - load;
    if (i + 4 > code.size()) {
        return 0;
    }
    return std::uint32_t(code[i]) | std::uint32_t(code[i + 1]) << 8 |
           std::uint32_t(code[i + 2]) << 16 | std::uint32_t(code[i + 3]) << 24;
}

Lscr Assets::script(const std::string& path) const {
    return import::adventure3::parseLscr(readFile(file(path)));
}

const Picture& Assets::picture(const std::string& path) {
    const std::string key = packPath(path);
    if (auto it = pictures_.find(key); it != pictures_.end()) {
        return it->second;
    }
    return pictures_.emplace(key, decodePicture(readFile(root / key))).first->second;
}

const Animation& Assets::animation(const std::string& path) {
    const std::string key = packPath(path);
    if (auto it = animations_.find(key); it != animations_.end()) {
        return it->second;
    }
    // ani.lbin: 1010 picture name; 1020 frame (name, x, y, w, h), 1022 layer
    // (picture, part, x, y, flags), 1021 end; 1030 sequence (name, origin x/y,
    // touch box x0, y0, x1, y1), 1032 step (frame, duration, x, y, flags),
    // 1031 end.
    const std::filesystem::path folder = root / key;
    const Lscr s = import::adventure3::parseLscr(readFile(folder / "ani.lbin"));
    Animation a;
    for (const auto& c : s.commands) {
        switch (c.opcode) {
        case 1010:
            a.pictures.push_back(decodePicture(readFile(folder / s.text(c.operands.at(0)))));
            break;
        case 1020:
            a.frames.push_back({s.text(c.operands.at(0)),
                                {integer(c, 1), integer(c, 2), integer(c, 3), integer(c, 4)},
                                {}});
            break;
        case 1022:
            if (a.frames.empty()) {
                throw std::runtime_error("animation layer outside a frame");
            }
            a.frames.back().layers.push_back({unsigned(integer(c, 0)), unsigned(integer(c, 1)),
                                              integer(c, 2), integer(c, 3),
                                              unsigned(integer(c, 4))});
            break;
        case 1030: {
            Animation::Sequence q;
            q.name = s.text(c.operands.at(0));
            q.originX = integer(c, 1);
            q.originY = integer(c, 2);
            q.box = {integer(c, 3), integer(c, 4), integer(c, 5) - integer(c, 3),
                     integer(c, 6) - integer(c, 4)};
            a.sequences.push_back(std::move(q));
            break;
        }
        case 1032:
            if (a.sequences.empty()) {
                throw std::runtime_error("animation step outside a sequence");
            }
            a.sequences.back().steps.push_back({unsigned(integer(c, 0)), unsigned(integer(c, 1)),
                                                integer(c, 2), integer(c, 3),
                                                unsigned(integer(c, 4))});
            break;
        default:
            break;
        }
    }
    for (const auto& f : a.frames) {
        for (const auto& l : f.layers) {
            if (l.picture >= a.pictures.size() || l.part >= a.pictures[l.picture].parts.size()) {
                throw std::runtime_error("animation layer outside its pictures: " + key);
            }
        }
    }
    // Steps may name frames the file does not have (nazo/n020, n129); they
    // show nothing (drawFrame).
    return animations_.emplace(key, std::move(a)).first->second;
}

const Font& Assets::font(const std::string& name) {
    if (auto it = fonts_.find(name); it != fonts_.end()) {
        return it->second;
    }
    // The same glyph records the second adventure's pack stores (.l2ft).
    const auto nftr = import::decodeFont(readFile(root / "fnt" / (name + ".lfnt")));
    Font font;
    for (const auto& g : nftr.mapped) {
        font.glyphs[g.code] = {unsigned(nftr.cellWidth), unsigned(nftr.cellHeight), g.left,
                               g.advance, g.alpha};
    }
    return fonts_.emplace(name, std::move(font)).first->second;
}

std::optional<Position> Assets::position(const std::string& name) {
    if (!positionsLoaded_) {
        // 5601 {name, x, y, layer, [mirror]} (020b4440 uses all five).
        const Lscr s = script("rc/ge/lt3_rc/lt3_pos.lbin");
        for (const auto& c : s.commands) {
            if (c.opcode == 5601 && c.operands.size() >= 4) {
                positions_[s.text(c.operands[0])] = {integer(c, 1), integer(c, 2), integer(c, 3),
                                                     c.operands.size() >= 5 ? integer(c, 4) : 0};
            }
        }
        positionsLoaded_ = true;
    }
    auto it = positions_.find(name);
    return it == positions_.end() ? std::nullopt : std::optional<Position>(it->second);
}
} // namespace picarat::adventure3
