// The fourth adventure's pictures, animation packs, fonts, data lists and
// scripts. See a4_assets.hpp.
#include "graphics/a4_assets.hpp"
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace picarat::adventure4 {
namespace {
using import::adventure4::DataList;
using import::adventure4::Operand;

std::vector<std::uint8_t> readAll(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open " + path.string());
    }
    return {std::istreambuf_iterator<char>(in), {}};
}

bool endsWith(const std::string& s, const char* tail) {
    const std::size_t n = std::strlen(tail);
    return s.size() >= n && s.compare(s.size() - n, n, tail) == 0;
}

// The game's spelling of a path as the pack's (relative to an archive
// folder): the language folder for "@" and the expanded names.
std::string packPath(std::string path, const std::string& language) {
    for (std::size_t at; (at = path.find('@')) != std::string::npos;) {
        path.replace(at, 1, language);
    }
    for (const char* pack : {".ci", ".ca", ".cse"}) {
        if (endsWith(path, pack)) {
            path.resize(path.size() - std::strlen(pack));
            return path;
        }
    }
    const std::size_t dot = path.rfind('.');
    if (dot != std::string::npos && dot + 1 < path.size() && path[dot + 1] == 'c') {
        path[dot + 1] = 'l';
    }
    return path;
}

int integer(const Operand& o) {
    switch (o.type & 15) {
    case 3: {
        float f;
        std::memcpy(&f, &o.value, 4);
        return int(f);
    }
    default:
        return int(std::int32_t(o.value));
    }
}

// RGB555 from a GPLT palette.
std::vector<std::uint16_t> palette(std::span<const std::uint8_t> data) {
    return import::adventure4::parseGplt(data);
}
} // namespace

// ------------------------------------------------------------- pictures

Picture decodePicture(std::span<const std::uint8_t> data, std::span<const std::uint8_t> plt) {
    const auto gtxt = import::adventure4::parseGtxt(data);
    const auto colours = plt.empty() ? std::vector<std::uint16_t>{} : palette(plt);
    Picture p;
    p.width = gtxt.width;
    p.height = gtxt.height;
    p.format = gtxt.format;
    p.pixels.assign(std::size_t(p.width) * p.height, 0);
    const bool translucent = gtxt.format == 4 || gtxt.format == 5;
    if (translucent) {
        p.alpha.assign(p.pixels.size(), 0);
    }
    auto colour = [&](unsigned index) -> std::uint16_t {
        return index < colours.size() ? std::uint16_t(colours[index] & 0x7fff) : 0;
    };
    for (unsigned y = 0; y < p.height; ++y) {
        for (unsigned x = 0; x < p.width; ++x) {
            const auto v = gtxt.pixel(x, y);
            if (!v) {
                continue;
            }
            const std::size_t at = std::size_t(y) * p.width + x;
            switch (gtxt.format) {
            case 0: // direct colour: bit 15 is the pixel's coverage
                if (*v & 0x8000) {
                    p.pixels[at] = std::uint16_t(*v | 0x8000);
                }
                break;
            case 4: { // A5I3: 5 bits alpha, 3 bits colour
                const unsigned a = *v >> 3;
                if (a) {
                    p.pixels[at] = std::uint16_t(colour(*v & 7) | 0x8000);
                    p.alpha[at] = std::uint8_t(a);
                }
                break;
            }
            case 5: { // A3I5: 3 bits alpha (scaled to 0..31), 5 bits colour
                const unsigned a3 = *v >> 5;
                if (a3) {
                    p.pixels[at] = std::uint16_t(colour(*v & 31) | 0x8000);
                    p.alpha[at] = std::uint8_t(a3 * 4 + a3 / 2);
                }
                break;
            }
            default: // 2, 4 or 8 bits per pixel; index 0 is empty
                if (*v) {
                    p.pixels[at] = std::uint16_t(colour(*v) | 0x8000);
                }
                break;
            }
        }
    }
    for (const auto& part : gtxt.parts) {
        p.parts.push_back(
            Rect{int(part.x) * 8, int(part.y) * 8, int(part.width) * 8, int(part.height) * 8});
    }
    return p;
}

// ----------------------------------------------------------------- fonts

Font decodeFont(std::span<const std::uint8_t> data) {
    const auto gfnt = import::adventure4::parseGfnt(data);
    Font f;
    f.cellWidth = gfnt.cellWidth;
    f.cellHeight = gfnt.cellHeight;
    for (const auto& g : gfnt.glyphs) {
        FontGlyph glyph;
        glyph.code = g.code;
        // ov6 0209afb8: x offset, advance, bitmap width (the top nibble is
        // part of the offset).
        glyph.left = g.metrics & 15;
        glyph.advance = (g.metrics >> 4) & 15;
        glyph.width = (g.metrics >> 8) & 15;
        // One bit per pixel, row after row of `width` pixels, each byte
        // filled from its low bit (checked on the ROM's glyphs).
        glyph.mask.assign(std::size_t(glyph.width) * f.cellHeight, 0);
        for (std::size_t i = 0; i < glyph.mask.size(); ++i) {
            const std::size_t byte = g.offset + i / 8;
            if (byte < gfnt.bitmaps.size()) {
                glyph.mask[i] = (gfnt.bitmaps[byte] >> (i % 8)) & 1;
            }
        }
        f.glyphs.emplace(g.code, std::move(glyph));
    }
    return f;
}

const FontGlyph* Font::glyph(unsigned code) const {
    const auto it = glyphs.find(code);
    return it == glyphs.end() ? nullptr : &it->second;
}

// ------------------------------------------------------------ animations

const Animation::Image* Animation::image(const std::string& name) const {
    for (const auto& i : images) {
        if (i.name == name) {
            return &i;
        }
    }
    return nullptr;
}

const Animation::Composite* Animation::composite(const std::string& name) const {
    for (const auto& c : composites) {
        if (c.name == name) {
            return &c;
        }
    }
    return nullptr;
}

const Animation::Sequence* Animation::sequence(const std::string& name) const {
    for (const auto& s : sequences) {
        if (s.name == name) {
            return &s;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------- assets

Assets::Assets(std::filesystem::path path) : root(std::move(path)) {}

std::filesystem::path Assets::file(const std::string& path) const {
    const std::string relative = packPath(path, language);
    // Program scripts come from lt4_sub.fa: the boot (ov8 020ef440) mounts
    // it when its integrity check of the program passes, lt4_seq.fa only
    // when the check fails. The two differ only in the bag script, whose
    // "save" in lt4_seq.fa returns to the title instead of saving.
    // Everything else comes from the main archive.
    for (const char* archive : {"main", "sub"}) {
        const auto candidate = root / archive / relative;
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

std::vector<std::uint8_t> Assets::packMember(const std::string& pack,
                                             const std::string& member) const {
    std::string folder = pack;
    for (std::size_t at; (at = folder.find('@')) != std::string::npos;) {
        folder.replace(at, 1, language);
    }
    if (const std::size_t dot = folder.rfind('.'); dot != std::string::npos) {
        folder.resize(dot);
    }
    for (const char* archive : {"main", "seq"}) {
        const auto candidate = root / archive / folder / member;
        if (std::filesystem::exists(candidate)) {
            return readAll(candidate);
        }
    }
    throw std::runtime_error("missing " + pack + " member " + member);
}

bool Assets::exists(const std::string& path) const {
    return !path.empty() && !file(path).empty();
}

const Picture& Assets::picture(const std::string& path) {
    auto& slot = pictures_[path];
    if (!slot) {
        const auto folder = file(path);
        if (folder.empty()) {
            throw std::runtime_error("missing picture " + path);
        }
        const auto lt = readAll(folder / "IMG.lt");
        const auto lp = readAll(folder / "IMG.lp");
        slot = std::make_unique<Picture>(decodePicture(lt, lp));
    }
    return *slot;
}

const Animation& Assets::animation(const std::string& path) {
    auto& slot = animations_[path];
    if (slot) {
        return *slot;
    }
    const auto folder = file(path);
    if (folder.empty()) {
        throw std::runtime_error("missing animation " + path);
    }
    auto a = std::make_unique<Animation>();
    const auto description = readAll(folder / "AS.lb");
    const DataList list = import::adventure4::parseGsd1(description);
    auto operand = [&](const import::adventure4::DataCommand& c, unsigned i) -> const Operand& {
        if (i >= c.count) {
            throw std::runtime_error("missing animation operand in " + path);
        }
        return list.operands[c.first + i];
    };
    auto num = [&](const import::adventure4::DataCommand& c, unsigned i) {
        return integer(operand(c, i));
    };
    auto text = [&](const import::adventure4::DataCommand& c, unsigned i) {
        return list.text(operand(c, i));
    };
    std::map<std::string, std::vector<std::uint8_t>> palettes;
    for (const auto& c : list.commands) {
        switch (c.opcode) {
        case 1000:
            for (unsigned i = 0; i < c.count; ++i) {
                a->header.push_back(num(c, i));
            }
            break;
        case 1010:
            a->images.push_back(
                {text(c, 0), num(c, 1), {num(c, 2), num(c, 3), num(c, 4), num(c, 5)}, {}});
            break;
        case 1012:
            if (a->images.empty()) {
                throw std::runtime_error("cell outside an image in " + path);
            }
            a->images.back().cells.push_back({num(c, 1), num(c, 2), unsigned(num(c, 0))});
            break;
        case 1020:
            a->composites.push_back({text(c, 0), {num(c, 1), num(c, 2), num(c, 3), num(c, 4)}, {}});
            break;
        case 1022:
            if (a->composites.empty()) {
                throw std::runtime_error("cell outside a composite in " + path);
            }
            a->composites.back().cells.push_back({num(c, 1), num(c, 2), unsigned(num(c, 0))});
            break;
        case 1030: {
            Animation::Sequence s;
            s.name = text(c, 0);
            s.box = {num(c, 1), num(c, 2), num(c, 3), num(c, 4)};
            s.x = num(c, 5);
            s.y = num(c, 6);
            s.next = c.count > 7 ? num(c, 7) : -1;
            a->sequences.push_back(std::move(s));
            break;
        }
        case 1032: {
            if (a->sequences.empty()) {
                throw std::runtime_error("step outside a sequence in " + path);
            }
            auto& s = a->sequences.back();
            Animation::Step step;
            step.index = unsigned(num(c, 0));
            step.duration = unsigned(num(c, 1)) & 0xffff;
            step.dx = num(c, 2);
            step.dy = num(c, 3);
            step.start = s.length;
            s.length += step.duration;
            s.steps.push_back(step);
            break;
        }
        case 1040: {
            Animation::Combination combination;
            combination.name = text(c, 0);
            for (unsigned i = 1; i < c.count; ++i) {
                combination.values.push_back(std::uint8_t(num(c, i)));
            }
            a->combinations.push_back(std::move(combination));
            break;
        }
        case 1050:
            palettes[text(c, 0)] = readAll(folder / text(c, 0));
            break;
        case 1051: {
            const std::string picture = text(c, 0);
            const std::string plt = c.count > 1 ? text(c, 1) : std::string();
            const auto lt = readAll(folder / picture);
            const auto it = palettes.find(plt);
            a->pictures.push_back(decodePicture(
                lt, it == palettes.end() ? std::span<const std::uint8_t>() : it->second));
            break;
        }
        default: // 1001, 1011, 1021, 1031: ends of lists
            break;
        }
    }
    slot = std::move(a);
    return *slot;
}

const Font& Assets::font(const std::string& path) {
    auto& slot = fonts_[path];
    if (!slot) {
        const auto f = file(path);
        if (f.empty()) {
            throw std::runtime_error("missing font " + path);
        }
        slot = std::make_unique<Font>(decodeFont(readAll(f)));
    }
    return *slot;
}

DataList Assets::data(const std::string& path) {
    // The list keeps a view of its file: the bytes stay with the assets.
    const auto f = file(path);
    if (f.empty()) {
        throw std::runtime_error("missing data list " + path);
    }
    auto& bytes = dataFiles_[f.string()];
    if (bytes.empty()) {
        bytes = readAll(f);
    }
    return import::adventure4::parseGsd1(bytes);
}

std::vector<std::uint8_t> Assets::scriptBytes(const std::string& path) const {
    const auto f = file(path);
    if (f.empty()) {
        throw std::runtime_error("missing script " + path);
    }
    return readAll(f);
}

const std::vector<std::uint8_t>& Assets::code(int overlay) const {
    auto& bytes = code_[overlay];
    if (bytes.empty()) {
        bytes = readAll(
            root / "code" /
            (overlay < 0 ? std::string("arm9.bin") : "overlay" + std::to_string(overlay) + ".bin"));
    }
    return bytes;
}

std::uint32_t Assets::codeWord(int overlay, std::uint32_t load, std::uint32_t address) const {
    const auto& bytes = code(overlay);
    const std::size_t at = address - load;
    if (at + 4 > bytes.size()) {
        throw std::runtime_error("code word outside the program");
    }
    return std::uint32_t(bytes[at] | bytes[at + 1] << 8 | bytes[at + 2] << 16 |
                         std::uint32_t(bytes[at + 3]) << 24);
}

int Assets::languageIndex() const {
    // ov6 0207b4c0: ten names (pointers at 020b51ec); overlay 6 loads at
    // 0207b4c0.
    constexpr std::uint32_t kOverlay6 = 0x0207b4c0, kNames = 0x020b51ec;
    for (int i = 0; i < 10; ++i) {
        if (codeText(6, kOverlay6, codeWord(6, kOverlay6, kNames + 4u * unsigned(i))) == language) {
            return i;
        }
    }
    throw std::runtime_error("unknown language " + language);
}

std::string Assets::codeText(int overlay, std::uint32_t load, std::uint32_t address) const {
    const auto& bytes = code(overlay);
    std::string s;
    for (std::size_t at = address - load; at < bytes.size() && bytes[at]; ++at) {
        s += char(bytes[at]);
    }
    return s;
}

} // namespace picarat::adventure4
