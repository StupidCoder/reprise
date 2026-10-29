#include "import/asset_pack.hpp"
#include "import/asset_codecs.hpp"
#include "import/pack_writer.hpp"
#include "import/sha256.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>

namespace picarat::import {
namespace {

const char* const kBackgrounds[] = {"logo/publishnintendo_2",
                                    "logo/publishnintendo_1",
                                    "logo/level5_bottom",
                                    "logo/level5",
                                    "logo/ai_logo_bottom",
                                    "logo/ai_logo_top",
                                    "title/title",
                                    "title/title_sub",
                                    "name/ge/name_sub",
                                    "name/name_bg1",
                                    "name/name_bg2",
                                    "name/name_bg3",
                                    "name/name_bg4",
                                    "map/main0",
                                    "nazo/q1",
                                    "nazo/q1a",
                                    "nazo/system/nazo_text1",
                                    "nazo/system/nazo_title1",
                                    "nazo/system/nazo_seikai1",
                                    "nazo/system/nazo_fail1",
                                    "nazo/system/qend_retry",
                                    "nazo/system/ge/hint_1",
                                    "nazo/system/ge/hint_2",
                                    "nazo/system/ge/hint_3",
                                    "nazo/system/ge/puzzlet3_0",
                                    "nazo/system/ge/puzzlet2_0",
                                    "nazo/hantei/ge/judge_l14_bg",
                                    "nazo/hantei/ge/judge_l114_bg",
                                    "nazo/system/ge/hint_1_2",
                                    "nazo/system/ge/hint_2_2",
                                    "nazo/system/ge/hint_3_2",
                                    "nazo/system/ge/jitenhint_1",
                                    "nazo/system/ge/jitenhint_2",
                                    "nazo/system/ge/jitenhint_3",
                                    "nazo/system/ge/atode_bg",
                                    "nazo/system/ge/picarat_get",
                                    "nazo/system/jiten_seikai"};
const char* const kSprites[] = {"title/ge/start.arc",
                                "title/ge/continue.arc",
                                "title/ge/secret.arc",
                                "title/waku.arc",
                                "title/train.arc",
                                "title/senro.arc",
                                "title/ge/logo.arj",
                                "name/name_btn.arc",
                                "name/ge/name_ok.arc",
                                "name/ge/bad_name.arc",
                                "nazo/drawinput/di_cursor.arc",
                                "lukeletter/ge/op_message.arc",
                                "nazo/tracebutton/arrow_left.arc",
                                "nazo/tracebutton/arrow_right.arc",
                                "nazo/tracebutton/point_trace.arc",
                                "nazo/tracebutton/ge/retry_trace.arc",
                                "nazo/tile/q1_tile.arc",
                                "nazo/system/ge/nazo_text.arj",
                                "nazo/system/picaratGetBig.arc",
                                "nazo/system/picaratGetSmall.arc",
                                "nazo/system/ge/touch.arc",
                                "nazo/system/ge/retry.arc",
                                "nazo/system/ge/seikai_text.arc",
                                "system/btn/ge/hantei.arc",
                                "system/btn/ge/hint_wifi.arc",
                                "system/btn/ge/reset.arc",
                                "system/btn/ge/hint.arc",
                                "system/btn/ge/hint_pica.arc",
                                "system/btn/ge/memo.arc",
                                "system/btn/ge/atode.arc",
                                "system/btn/ge/modoru_memo.arc",
                                "system/btn/ge/yes.arc",
                                "system/btn/ge/no.arc",
                                "nazo/system/hint_num.arc",
                                "nazo/system/ge/hint1.arc",
                                "nazo/system/ge/hint2.arc",
                                "nazo/system/ge/hint3.arc",
                                "nazo/system/ge/hintlock1.arc",
                                "nazo/system/ge/hintlock2.arc",
                                "nazo/system/ge/hintlock3.arc"};

constexpr std::uint32_t kOverlay2Base = 0x02068880;

// fnmatch subset used by the pack rules: '*', '?' and '[set]'.
bool matches(std::string_view pattern, std::string_view name) {
    if (pattern.empty()) {
        return name.empty();
    }
    if (pattern[0] == '*') {
        for (std::size_t skip = 0; skip <= name.size(); ++skip) {
            if (matches(pattern.substr(1), name.substr(skip))) {
                return true;
            }
        }
        return false;
    }
    if (name.empty()) {
        return false;
    }
    if (pattern[0] == '[') {
        const std::size_t close = pattern.find(']');
        if (close == std::string_view::npos ||
            pattern.substr(1, close - 1).find(name[0]) == std::string_view::npos) {
            return false;
        }
        return matches(pattern.substr(close + 1), name.substr(1));
    }
    return (pattern[0] == '?' || pattern[0] == name[0]) &&
           matches(pattern.substr(1), name.substr(1));
}

std::string stripExtension(const std::string& path) {
    const std::size_t dot = path.rfind('.');
    return dot == std::string::npos ? path : path.substr(0, dot);
}

std::string replaceAll(std::string text, std::string_view from, std::string_view to) {
    for (std::size_t at = 0; (at = text.find(from, at)) != std::string::npos; at += to.size()) {
        text.replace(at, from.size(), to);
    }
    return text;
}

// ROM files addressed by their FNT path; archives are expanded on demand.
class RomAssets {
public:
    explicit RomAssets(const NdsRom& rom) : rom_(rom) {}

    std::span<const std::uint8_t> raw(const std::string& path) const {
        const RomFile* file = rom_.find(path);
        if (!file) {
            throw FormatError("missing ROM file " + path);
        }
        return rom_.contents(*file);
    }

    Bytes expanded(const std::string& path) const { return expandAsset(raw(path), path); }

    bool exists(const std::string& path) const { return rom_.find(path) != nullptr; }

    // Files directly inside `directory` whose name matches, sorted by name.
    std::vector<std::string> list(const std::string& directory, std::string_view pattern) const {
        std::vector<std::string> names;
        const std::string prefix = directory + "/";
        for (const RomFile& file : rom_.files()) {
            if (file.path.compare(0, prefix.size(), prefix) != 0) {
                continue;
            }
            const std::string name = file.path.substr(prefix.size());
            if (name.find('/') == std::string::npos && matches(pattern, name)) {
                names.push_back(name);
            }
        }
        std::sort(names.begin(), names.end());
        return names;
    }

    // Paths below `directory` (any depth, relative to it) whose file name matches.
    std::vector<std::string> listRecursive(const std::string& directory,
                                           std::string_view pattern) const {
        std::vector<std::string> paths;
        const std::string prefix = directory + "/";
        for (const RomFile& file : rom_.files()) {
            if (file.path.compare(0, prefix.size(), prefix) != 0) {
                continue;
            }
            const std::string relative = file.path.substr(prefix.size());
            if (matches(pattern, relative.substr(relative.rfind('/') + 1))) {
                paths.push_back(relative);
            }
        }
        std::sort(paths.begin(), paths.end());
        return paths;
    }

    const std::vector<ArchiveMember>& members(const std::string& archive) const {
        auto found = archives_.find(archive);
        if (found == archives_.end()) {
            found = archives_.emplace(archive, archiveMembers(expanded(archive))).first;
        }
        return found->second;
    }

    const Bytes& member(const std::string& archive, const std::string& name) const {
        for (const ArchiveMember& m : members(archive)) {
            if (m.name == name) {
                return m.data;
            }
        }
        throw FormatError("missing archive member " + archive + "/" + name);
    }

    // Members (sorted by name) of one archive whose name matches.
    std::vector<const ArchiveMember*> members(const std::string& archive,
                                              std::string_view pattern) const {
        std::vector<const ArchiveMember*> found;
        for (const ArchiveMember& m : members(archive)) {
            if (m.name.find('/') == std::string::npos && matches(pattern, m.name)) {
                found.push_back(&m);
            }
        }
        std::sort(found.begin(), found.end(), [](auto* a, auto* b) { return a->name < b->name; });
        return found;
    }

private:
    const NdsRom& rom_;
    mutable std::map<std::string, std::vector<ArchiveMember>> archives_;
};

void put32(Bytes& out, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(std::uint8_t(value >> (8 * i)));
    }
}

void put16(Bytes& out, std::uint16_t value) {
    out.push_back(std::uint8_t(value));
    out.push_back(std::uint8_t(value >> 8));
}

void putString(Bytes& out, std::string_view text) {
    put32(out, std::uint32_t(text.size()));
    out.insert(out.end(), text.begin(), text.end());
}

void putTag(Bytes& out, const char* tag, std::uint32_t version) {
    out.insert(out.end(), tag, tag + 4);
    put32(out, version);
}

// Width, height, then RGB555 words; bit 15 marks an opaque pixel. Index 0
// is transparent where the original layer treats colour 0 as transparent.
void putSurface(Bytes& out, unsigned width, unsigned height,
                const std::vector<std::uint16_t>& palette, std::span<const std::uint8_t> indices,
                bool transparent) {
    put32(out, width);
    put32(out, height);
    for (std::uint8_t i : indices) {
        check(i < palette.size(), "palette index outside palette");
        put16(out, std::uint16_t(palette[i] | ((i || !transparent) ? 0x8000 : 0)));
    }
}

std::string lines(const std::vector<unsigned>& values) {
    std::string text;
    for (unsigned v : values) {
        text += std::to_string(v) + "\n";
    }
    return text;
}

// Overlay 2 data at a RAM address (load address kOverlay2Base).
struct Overlay2 {
    Bytes data;

    std::span<const std::uint8_t> at(std::uint32_t address, std::size_t size) const {
        check(address >= kOverlay2Base && address - kOverlay2Base + size <= data.size(),
              "overlay 2 address outside data");
        return std::span(data).subspan(address - kOverlay2Base, size);
    }

    std::uint32_t word(std::uint32_t address) const { return read32(at(address, 4), 0); }

    std::string cString(std::uint32_t address) const {
        const auto rest = std::span(data).subspan(at(address, 0).data() - data.data());
        return std::string(rest.begin(), std::find(rest.begin(), rest.end(), 0));
    }
};

} // namespace

Bytes escapeTableFile(const std::vector<std::pair<std::string, std::uint32_t>>& escapes, char open,
                      char close) {
    const bool angled = open == '<' && close == '>';
    Bytes file;
    putTag(file, "L2ES", angled ? 1 : 2);
    if (!angled) {
        put32(file, std::uint32_t(std::uint8_t(open)) | std::uint32_t(std::uint8_t(close)) << 8);
    }
    put32(file, std::uint32_t(escapes.size()));
    for (const auto& [key, value] : escapes) {
        putString(file, key);
        put32(file, value);
    }
    return file;
}

Bytes nativeBackground(std::span<const std::uint8_t> expanded, bool transparent, bool withTile) {
    const Background bg = decodeBackground(expanded);
    Bytes data;
    putTag(data, "L2BG", withTile ? 2 : 1);
    putSurface(data, bg.width, bg.height, bg.palette, bg.indices, transparent);
    if (withTile) {
        putSurface(data, 8, 8, bg.palette, expanded.subspan(bg.tileOffset, 64), transparent);
    }
    return data;
}

// ov2 02076998 (second adventure): a sprite's extension may name an attached
// child sprite (X plane, Y plane, child-animation bytes, then a 128-byte path).
std::string spriteChild(const Sprite& sprite) {
    const std::size_t n = sprite.animations.size();
    if (sprite.tail.size() < n * 5 + 128) {
        return {};
    }
    const auto field = std::span(sprite.tail).subspan(n * 5, 128);
    const auto end = std::find(field.begin(), field.end(), 0);
    check(std::all_of(field.begin(), end, [](std::uint8_t c) { return c < 128; }),
          "non-ASCII child sprite name");
    return std::string(field.begin(), end);
}

Bytes nativeSprite(const Sprite& sprite, const std::string& childPath, bool rotating) {
    // Version 4/5 (schema 14) append every frame's texture-chunk records
    // (x, y, width, height as stored, e.g. 8 << ex) for the scaled 3D
    // draw 02076124; version 5 also keeps the rotation chunks of v3.
    Bytes data;
    putTag(data, "L2SP", rotating ? 5 : 4);
    put32(data, std::uint32_t(sprite.frames.size()));
    for (const SpriteFrame& frame : sprite.frames) {
        putSurface(data, frame.width, frame.height, sprite.palette, frame.indices, true);
    }
    put32(data, std::uint32_t(sprite.animations.size()));
    for (const SpriteAnimation& animation : sprite.animations) {
        putString(data, animation.label);
        put32(data, std::uint32_t(animation.images.size()));
        for (std::size_t s = 0; s < animation.images.size(); ++s) {
            put32(data, animation.next[s]);
            put32(data, animation.durations[s]);
            put32(data, animation.images[s]);
        }
    }
    put32(data, std::uint32_t(sprite.variables.size()));
    for (std::size_t i = 0; i < sprite.variables.size(); ++i) {
        putString(data, sprite.variables[i]);
        for (const auto& plane : sprite.planes) {
            put16(data, std::uint16_t(plane[i]));
        }
    }
    // ov2 02076998: X plane, Y plane, child-animation bytes, child path.
    const std::size_t n = sprite.animations.size();
    putString(data, childPath);
    for (std::size_t i = 0; i < n; ++i) {
        const auto& tail = sprite.tail;
        const std::uint16_t x = childPath.empty() ? 0 : read16(tail, 2 * i),
                            y = childPath.empty() ? 0 : read16(tail, 2 * n + 2 * i);
        put16(data, x);
        put16(data, y);
        put32(data, childPath.empty() ? 0 : tail[4 * n + i]);
    }
    auto putChunks = [&] {
        for (const SpriteFrame& frame : sprite.frames) {
            put32(data, std::uint32_t(frame.chunks.size()));
            for (const SpriteChunk& c : frame.chunks) {
                for (int value : {c.x, c.y, c.width, c.height}) {
                    put32(data, std::uint32_t(value));
                }
            }
        }
    };
    if (rotating) {
        putChunks();
    }
    putChunks();
    return data;
}

Bytes nativeFont(const Font& font, std::span<const std::uint8_t> widths) {
    // Version 2 appends the program's width table.
    Bytes data;
    putTag(data, "L2FT", widths.empty() ? 1 : 2);
    put32(data, std::uint32_t(font.mapped.size()));
    for (const FontGlyph& glyph : font.mapped) {
        put32(data, glyph.code);
        for (std::int32_t value : {font.cellWidth, font.cellHeight, glyph.left, glyph.advance}) {
            put32(data, std::uint32_t(value));
        }
        data.insert(data.end(), glyph.alpha.begin(), glyph.alpha.end());
    }
    data.insert(data.end(), widths.begin(), widths.end());
    return data;
}

PackResult buildNativePack(const NdsRom& rom, const std::filesystem::path& output,
                           std::span<const unsigned> library,
                           const PackProgress& progressCallback) {
    check(rom.identity().game == Game::Adventure2, "not the second adventure's ROM");
    const RomAssets assets(rom);
    PackWriter pack(output);
    auto progress = [&](double fraction, const char* message) {
        if (progressCallback) {
            progressCallback(fraction, message);
        }
    };
    auto copyRaw = [&](const std::string& directory, std::string_view pattern,
                       const std::string& target) {
        for (const std::string& name : assets.list(directory, pattern)) {
            pack.save(target + name, assets.raw(directory + "/" + name));
        }
    };
    auto copyMembers = [&](const std::string& archive, std::string_view pattern,
                           const std::string& target) {
        for (const ArchiveMember* m : assets.members(archive, pattern)) {
            pack.save(target + m->name, m->data);
        }
    };
    // A path string argument (tag 3) following an opcode, as the scripts store it.
    auto stringArgument = [](const std::vector<ScriptToken>& tokens, std::size_t index) {
        check(index < tokens.size() && tokens[index].tag == 3, "GDS argument is not a string");
        return tokens[index].text();
    };

    std::set<std::string> backgrounds(std::begin(kBackgrounds), std::end(kBackgrounds));
    std::set<std::string> sprites(std::begin(kSprites), std::end(kSprites));
    std::set<std::string> fonts{"fontevent.NFTR", "fontq.NFTR"};
    bool decuma = false;
    // Puzzle card (ov27 020efda0): title sprite, picarat digits, lower background.
    sprites.insert({"nazo/system/ge/nazo_title.arc", "nazo/system/picaratBig.arc",
                    "nazo/system/picaratTotal.arc", "nazo/system/picaratSmall.arc"});
    backgrounds.insert({"nazo/system/nazo_title1", "nazo/system/nazo_title2",
                        "nazo/system/nazo_title3", "nazo/system/nazo_title4",
                        "nazo/system/ge/picarat_bg"});
    sprites.insert({"nazo/tilerotate/pict_puzzle_rot.arc",
                    "nazo/tilerotate/pict_puzzle_rot_q203.arc", "nazo/tilerotate/q2_button.arc"});
    backgrounds.insert("nazo/system/ge/puzzlet1_0");

    // Program code with constant tables the engine needs (the sound driver's
    // pitch, envelope and pan tables; the movie decoder's code tables). They
    // are read from the user's pack at run time, so the source code carries
    // only their addresses.
    pack.save("code/arm9.bin", rom.arm9Program());
    const RomFile* movieOverlay = rom.overlay(true, 1);
    check(movieOverlay != nullptr, "missing ARM9 overlay 1");
    pack.save("code/overlay1.bin", decompressCode(rom.contents(*movieOverlay)));
    // The bonus menu (ov23): its music page's track list (020f3b80), used by
    // the app's music player.
    const RomFile* bonusOverlay = rom.overlay(true, 23);
    check(bonusOverlay != nullptr, "missing ARM9 overlay 23");
    pack.save("code/overlay23.bin", decompressCode(rom.contents(*bonusOverlay)));

    const RomFile* overlayFile = rom.overlay(true, 2);
    check(overlayFile != nullptr, "missing ARM9 overlay 2");
    const Overlay2 overlay{decompressCode(rom.contents(*overlayFile))};
    pack.save("puzzle/judgment.tbl", overlay.at(0x2086f18, 114));
    // Lead character names (src/a2_character_names.hpp): string pointers in the
    // literal pools of the name entry (ov12) and the tea request (ov14 020f16dc).
    Bytes names;
    std::vector<std::string> leads;
    putTag(names, "L2NM", 1);
    for (const auto& [id, pool] : {std::pair{12u, 0x020f03ccu}, std::pair{14u, 0x020f1e68u}}) {
        const RomFile* file = rom.overlay(true, id);
        check(file != nullptr, "missing name overlay");
        const Bytes code = decompressCode(rom.contents(*file));
        const std::uint32_t base = file->overlay->loadAddress;
        auto word = [&](std::uint32_t address) {
            check(address >= base && address - base + 4 <= code.size(),
                  "name pointer outside overlay");
            return read32(code, address - base);
        };
        for (std::uint32_t slot = 0; slot < 2; ++slot) {
            const std::uint32_t address = word(pool + 4 * slot);
            check(address >= base && address - base < code.size(), "name string outside overlay");
            const auto begin = code.begin() + std::ptrdiff_t(address - base);
            const auto end = std::find(begin, code.end(), 0);
            check(end != code.end() && end - begin <= 16, "unterminated character name");
            leads.emplace_back(begin, end);
            putString(names, leads.back());
        }
    }
    pack.save("names.bin", names);
    // Two sprites carry the protagonist's name in their ROM file names
    // (bag-menu walker and tea face). The pack stores them with "protagonist"
    // in its place; no script refers to them by name.
    std::string protagonist = leads.front();
    for (char& c : protagonist) {
        c = char(std::tolower(std::uint8_t(c)));
    }
    auto packName = [&](std::string path) {
        return replaceAll(std::move(path), protagonist, "protagonist");
    };
    for (const char character : {'l', 'r'}) {
        for (int index = 1; index < 14; ++index) {
            backgrounds.insert(std::string("nazo/hantei/judge_") + character +
                               std::to_string(index) + "_bg");
        }
        for (int index = 107; index < 114; ++index) {
            backgrounds.insert(std::string("nazo/hantei/judge_") + character +
                               std::to_string(index) + "_bg");
        }
        for (int index : {14, 114}) {
            backgrounds.insert(std::string("nazo/hantei/ge/judge_") + character +
                               std::to_string(index) + "_bg");
        }
    }
    copyRaw("data_lt2/stream/nazo/ge", "ST_*.SAD", "stream/nazo/ge/");
    pack.save("puzzle/library.txt", lines(std::vector<unsigned>(library.begin(), library.end())));

    // Catalogue metadata is available even when the corresponding engine is not.
    // Empty ROM slots are not puzzles; keep every named record, including 206.
    const std::string nazo = "data_lt2/nazo/ge";
    std::vector<unsigned> catalogue;
    std::map<unsigned, std::vector<const Bytes*>> puzzleRecords;
    for (const std::string& archive : assets.list(nazo, "nazo*.plz")) {
        for (const ArchiveMember* m : assets.members(nazo + "/" + archive, "n*.dat")) {
            const unsigned internal = unsigned(std::stoul(stripExtension(m->name).substr(1)));
            puzzleRecords[internal].push_back(&m->data);
            if (m->data.size() <= 4 || m->data[4] == 0) {
                continue;
            }
            catalogue.push_back(internal);
            pack.save("puzzle/n" + std::to_string(internal) + ".dat", m->data);
        }
    }
    std::sort(catalogue.begin(), catalogue.end());
    pack.save("puzzle/catalogue.txt", lines(catalogue));
    progress(0.05, "Preparing puzzles…");

    for (const unsigned internal : library) {
        const auto& candidates = puzzleRecords[internal];
        check(candidates.size() == 1, "puzzle data identity");
        const Bytes& data = *candidates.front();
        check(data.size() > 0x3e, "puzzle record truncated");
        const unsigned type = data[0x3a];
        const bool answer = type == 16 || type == 20 || type == 21 || type == 22 || type == 28 ||
                            type == 32 || type == 35;
        const bool line = type == 9 || type == 15, slide = type == 25, toggle = type == 23,
                   rose = type == 24, lamp = type == 31;
        const bool skate = type == 27, couple = type == 30, bridge = type == 33,
                   trace = type == 6 || type == 34, slot = type == 11;
        const bool engine = answer || line || slide || toggle || rose || lamp || skate || couple ||
                            bridge || trace || slot;
        const bool classic = type == 2 || type == 3 || type == 5 || type == 10 || type == 13 ||
                             type == 17 || type == 18 || type == 26 || type == 29;
        check((classic || engine) &&
                  (!(data[0x38] & 0x40) || type == 2 || type == 3 || type == 26 || engine),
              "puzzle type without a native engine");
        const std::string id = std::to_string(internal);
        pack.save("puzzle/n" + id + ".dat", data);
        const Bytes& params = assets.member("data_lt2/script/puzzle.plz", "q" + id + "_param.gds");
        pack.save("puzzle/q" + id + "_param.gds", params);
        const std::string theme = std::to_string(data[0x3e]),
                          background = std::to_string(data[0x3b]);
        backgrounds.insert(std::string("nazo/") + ((data[0x38] & 0x20) ? "ge/" : "") + "q" +
                           background);
        if (data[0x38] & 0x10) {
            backgrounds.insert(std::string("nazo/") + ((data[0x38] & 0x40) ? "ge/" : "") + "q" +
                               background + "a");
        }
        for (const char* prefix : {"nazo_text", "nazo_title", "nazo_seikai", "nazo_fail"}) {
            backgrounds.insert(std::string("nazo/system/") + prefix + theme);
        }
        if (type == 29) {
            sprites.insert("nazo/pegsolitaire/solitaire_gfx.arc");
        }
        if (type == 17) {
            sprites.insert("nazo/knight/knight_gfx.arc");
        }
        if (type == 13) {
            sprites.insert({"nazo/pancake/pancake_gfx.arc", "nazo/common/counter_number.arc"});
        }
        if (slide) {
            sprites.insert("nazo/common/counter_number.arc");
        }
        if (rose) {
            sprites.insert("nazo/rose/rose_gfx.arc");
        }
        if (lamp) {
            sprites.insert("nazo/lamp/lamp_gfx.arc");
        }
        if (skate) {
            sprites.insert("nazo/iceskate/iceskate_gfx.arc");
        }
        if (bridge) {
            sprites.insert("nazo/bridge/q99_foot.arc");
        }
        if (couple) {
            sprites.insert({"nazo/couple/couple_gfx.arc", "nazo/common/counter_number.arc"});
        }
        // ov37 020f78a0: point/target markers and the line-limit notice.
        if (line) {
            sprites.insert({"nazo/cut/line_point.arc", "nazo/cut/line_here.arc",
                            "nazo/cut/ge/nomorelines.arc"});
        }
        if (answer) {
            // ov42: answer-screen controls, digit previews and failure popup.
            sprites.insert({"nazo/drawinput/ge/di_kaito.arc", "nazo/drawinput/ge/di_hantei.arc",
                            "nazo/drawinput/ge/di_allreset.arc", "nazo/drawinput/ge/di_modoru.arc",
                            "nazo/drawinput/ge/di_erase2.arc", "nazo/drawinput/ge/inputfail.arc",
                            "nazo/drawinput/inputnumbers.arc"});
            fonts.insert("font18.NFTR");
            decuma = true;
            if (internal == 58) {
                sprites.insert("nazo/drawinput/q58_gfx.arc");
                backgrounds.insert("nazo/q" + background + "x");
            }
            if (data[0x34]) {
                backgrounds.insert("nazo/system/ge/puzzlet" + std::to_string(data[0x34]) + "_0");
            }
        }
        const auto tokens = scriptTokens(params);
        const auto pages = std::count_if(tokens.begin(), tokens.end(), [](const ScriptToken& t) {
            return t.tag == 0 && t.opcode == 0x3e;
        });
        for (long page = 1; page <= pages; ++page) {
            backgrounds.insert("nazo/q" + id + "_" + std::to_string(page));
        }
        for (std::size_t n = 0; n < tokens.size(); ++n) {
            if (tokens[n].tag != 0) {
                continue;
            }
            const unsigned opcode = tokens[n].opcode;
            if (opcode == 0x2e) {
                sprites.insert(
                    "nazo/touch/" +
                    stripExtension(replaceAll(stringArgument(tokens, n + 3), "?", "ge")) + ".arc");
            }
            if (opcode == 0x14) {
                sprites.insert(
                    std::string(type == 2 ? "nazo/freebutton/" : "nazo/onoff/") +
                    stripExtension(replaceAll(stringArgument(tokens, n + 3), "?", "ge")) + ".arc");
            }
            if (opcode == 0x43) {
                backgrounds.insert(
                    "nazo/drawinput/" +
                    stripExtension(replaceAll(stringArgument(tokens, n + 1), "?", "ge")));
            }
            if (opcode == 0x39 && slot) {
                sprites.insert("nazo/tile/" + stripExtension(stringArgument(tokens, n + 3)) +
                               ".arc");
            }
            if (opcode == 0x51 && slide) {
                sprites.insert("nazo/slide/" + stripExtension(stringArgument(tokens, n + 1)) +
                               ".arc");
            }
            if (opcode == 0x54) {
                sprites.insert(
                    std::string(type == 18 ? "nazo/tilerotate/" : "nazo/tile/") +
                    stripExtension(replaceAll(stringArgument(tokens, n + 1), "?", "ge")) + ".arc");
            }
        }
    }
    progress(0.10, "Preparing the story…");

    // Story progression. The recovered scene router
    // follows the original data anywhere, so the pack carries every place
    // version, event script/data/dialogue, progression table and text instead
    // of a curated route. Places keep the ROM's n_place%i_%i numbering.
    for (const char* archive : {"plc_data1.plz", "plc_data2.plz"}) {
        for (const ArchiveMember* m :
             assets.members(std::string("data_lt2/place/") + archive, "n_place*_*.dat")) {
            pack.save("place/" + m->name.substr(7), m->data);
        }
    }
    for (const char* name : {"placeflag.dat", "autoevent2.dat", "storyflag2.dat"}) {
        pack.save(std::string("place/") + name, assets.member("data_lt2/place/data.plz", name));
    }
    for (const char* name :
         {"ev_inf2.dlz", "ev_fix.dlz", "nz_lst.dlz", "ev_inf.dlz", "ev_lch.dlz", "ev_str.dlz"}) {
        pack.save(std::string("rc/") + name,
                  assets.expanded(std::string("data_lt2/rc/ge/") + name));
    }
    // Herb tea (overlay 14): recipes, herb descriptions and tasting talk rows
    // (ov2 02085334/02085128/02085758) plus the rc/ge/ht_tlk.plz messages.
    for (const char* name : {"ht_elm.dlz", "ht_rcp.dlz", "ht_tlk.dlz"}) {
        pack.save(std::string("rc/") + name,
                  assets.expanded(std::string("data_lt2/rc/ge/") + name));
    }
    copyMembers("data_lt2/rc/ge/ht_tlk.plz", "*.txt", "text/ht/");
    // Camera repair (overlay 13) message archive rc/ge/cm_txt.plz.
    copyMembers("data_lt2/rc/ge/cm_txt.plz", "*.txt", "text/cm/");
    // Hamster (overlay 15): G3D models/animations (chara/*.plz) and the rc/ge/hm_exp.plz texts.
    for (const std::string& archive : assets.list("data_lt2/chara", "*.plz")) {
        copyMembers("data_lt2/chara/" + archive, "*", "chara/" + stripExtension(archive) + "/");
    }
    copyMembers("data_lt2/rc/ge/hm_exp.plz", "*.txt", "text/hm/");
    for (const char* name :
         {"goal_inf.dlz", "chp_inf.dlz", "sm_inf.dlz", "ht_event.dlz", "sb_pht.dlz"}) {
        pack.save(std::string("rc/") + name, assets.expanded(std::string("data_lt2/rc/") + name));
    }
    for (const std::string& archive : assets.list("data_lt2/event", "ev_d*.plz")) {
        copyMembers("data_lt2/event/" + archive, "*", "event/");
    }
    for (const std::string& archive : assets.list("data_lt2/event/ge", "ev_t*.plz")) {
        copyMembers("data_lt2/event/ge/" + archive, "*", "event/");
    }
    for (const char* archive : {"txt.plz", "txt2.plz"}) {
        copyMembers(std::string("data_lt2/txt/ge/") + archive, "*.txt", "text/");
    }
    copyMembers("data_lt2/place/ge/tobj.plz", "*.txt", "text/");
    copyMembers("data_lt2/nazo/ge/jiten.plz", "*.txt", "text/jiten/");
    // The bonus code entry's key characters (ov11 020efda0).
    copyMembers("data_lt2/txt/input_txt.plz", "secret.txt", "text/input/");
    copyMembers("data_lt2/script/movie/ge/movie.plz", "*.gds", "movie/");
    progress(0.20, "Preparing films and music…");
    copyRaw("data_lt2/stream/movie/ge", "M*.SAD", "stream/movie/ge/");
    copyRaw("data_lt2/stream/bgm", "BG_*.SAD", "stream/bgm/");
    // Every background and sprite: rooms, events and menus reference them by
    // computed names (map/main%i, eventobj/obj_%i, bgani/%s ...).
    for (const std::string& path : assets.listRecursive("data_lt2/bg", "*.arc")) {
        backgrounds.insert(stripExtension(path));
    }
    for (const std::string& path : assets.listRecursive("data_lt2/ani", "*.ar[cj]")) {
        sprites.insert(path);
    }
    copyRaw("data_lt2/stream/event/ge", "*.SAD", "stream/event/ge/");
    backgrounds.insert({"chapter/ge/chapter1", "lukeletter/luke_letter",
                        "lukeletter/luke_letter_sub", "map/map1", "menu/bag/info_mode_bg",
                        "menu/bag/ge/info_mode_sub", "menu/bag/info_mode_sub2"});
    sprites.insert({"lukeletter/ge/message.arc", "event/twindow.arc", "map/movemode.arc",
                    "map/exit_0.arc", "map/exit_3.arc", "menu/bag/ge/info_mode_chr.arc",
                    "menu/bag/ge/fukamaru_ok.arc"});
    for (int i = 1; i <= 3; ++i) {
        sprites.insert({"eventchr/chr" + std::to_string(i) + ".arc",
                        "eventchr/ge/chr" + std::to_string(i) + "_n.arc"});
    }

    // Resolve attachment resources from the ROM's extension, not a name map.
    std::vector<std::string> pending(sprites.begin(), sprites.end());
    for (std::size_t i = 0; i < pending.size(); ++i) {
        const std::string name = pending[i];
        const std::string child = spriteChild(
            decodeSprite(assets.expanded("data_lt2/ani/" + name), name.ends_with(".arj")));
        if (child.empty()) {
            continue;
        }
        const std::string path = "sub/" + stripExtension(child) + ".arc";
        // A few unused sprites name children absent from the ROM.
        if (!sprites.contains(path) && assets.exists("data_lt2/ani/" + path)) {
            sprites.insert(path);
            pending.push_back(path);
        }
    }
    progress(0.30, "Preparing audio…");
    // Streamed effects ST_%03d.SAD (script 0x5d, overlay-2 stream object) and movie audio.
    copyRaw("data_lt2/stream", "ST_*.SAD", "stream/");
    pack.save("stream/movie/ge/M1.SAD", assets.raw("data_lt2/stream/movie/ge/M1.SAD"));
    {
        std::vector<std::string> first{"it_1.txt", "id_1.txt", "item_4.txt"};
        for (int i = 0; i < 18; ++i) {
            if (i != 10) {
                first.push_back("m1_" + std::to_string(i) + ".txt");
            }
        }
        for (const std::string& name : first) {
            pack.save("text/" + name, assets.member("data_lt2/txt/ge/txt.plz", name));
        }
        for (const char* name :
             {"goal_5.txt", "goal_10.txt", "next_5.txt", "tx_1000.txt", "tx_1400.txt", "tx_204.txt",
              "tx_700.txt", "tx_701.txt", "tx_702.txt", "tx_1601.txt"}) {
            pack.save(std::string("text/") + name, assets.member("data_lt2/txt/ge/txt2.plz", name));
        }
    }
    for (int i = 1; i <= 7; ++i) {
        pack.save("text/tobj" + std::to_string(i) + ".txt",
                  assets.member("data_lt2/place/ge/tobj.plz", "tobj" + std::to_string(i) + ".txt"));
    }
    for (int i = 67; i <= 71; ++i) {
        pack.save("text/tobj" + std::to_string(i) + ".txt",
                  assets.member("data_lt2/place/ge/tobj.plz", "tobj" + std::to_string(i) + ".txt"));
    }
    for (const auto& [room, archive] :
         {std::pair{"1_0", 1}, std::pair{"1_1", 1}, std::pair{"91_0", 2}}) {
        pack.save(std::string("place/") + room + ".dat",
                  assets.member("data_lt2/place/plc_data" + std::to_string(archive) + ".plz",
                                std::string("n_place") + room + ".dat"));
    }
    pack.save("time.dlz", assets.expanded("data_lt2/rc/tm_def.dlz"));
    // Sequenced audio (src/sound_object.cpp): every original SWD/SMD/SED bank,
    // sequence and effect set, and the room-sound table rc/snd_fix.dlz.
    copyRaw("data_lt2/sound", "*.S[WME]D", "sound/");
    pack.save("sound/snd_fix.dlz", assets.expanded("data_lt2/rc/snd_fix.dlz"));
    pack.save("movie/m1.gds", assets.member("data_lt2/script/movie/ge/movie.plz", "m1.gds"));
    // Original Mobiclip MODS files, decoded at runtime by src/mobiclip.cpp.
    for (int i = 1; i < 24; ++i) {
        pack.save("movie/m" + std::to_string(i) + ".mods",
                  assets.raw("data_lt2/movie/m" + std::to_string(i) + ".mods"));
    }
    const Bytes& movieScript = assets.member("data_lt2/script/movie/ge/movie.plz", "m2.gds");
    pack.save("movie/m2.gds", movieScript);
    for (const ScriptToken& token : scriptTokens(movieScript)) {
        if (token.tag == 1) {
            const std::string name = "m2_" + std::to_string(token.integer) + ".txt";
            pack.save("text/" + name, assets.member("data_lt2/txt/ge/txt.plz", name));
        }
    }
    pack.save("stream/movie/ge/M2.SAD", assets.raw("data_lt2/stream/movie/ge/M2.SAD"));
    progress(0.40, "Preparing backgrounds…");

    // Staff-roll pictures (ov22) are an upper BG2 blended over sprites, so their
    // colour 0 is transparent; three of them are stored uncompressed (.arb,
    // the expanded background layout) and are packed under the same names.
    const std::regex staffRoll("ending/[0-9][0-9]");
    // The bonus menu's character portraits (ov23 020f1fe8) are an upper BG2
    // over chrmode_sub: colour 0 transparent as well (schema 18).
    const std::regex portrait("menu/secret/chr_sub[0-9]+");
    std::size_t done = 0;
    for (const std::string& name : backgrounds) {
        const bool transparent =
            std::regex_match(name, staffRoll) || std::regex_match(name, portrait);
        // Version 2 (schema 14) appends tile 0 as an 8x8 image: 0206ffec
        // loads the BG into a 256x256 map whose unused rows show tile 0
        // (visible when a shake scrolls past the picture, ov20 020f5208).
        pack.save("bg/" + name + ".l2bg",
                  nativeBackground(assets.expanded("data_lt2/bg/" + name + ".arc"), transparent));
        if (++done % 64 == 0) {
            progress(0.40 + 0.25 * double(done) / double(backgrounds.size()),
                     "Preparing backgrounds…");
        }
    }
    for (const std::string& path : assets.listRecursive("data_lt2/bg", "*.arb")) {
        const std::string name = stripExtension(path);
        pack.save("bg/" + name + ".l2bg",
                  nativeBackground(assets.raw("data_lt2/bg/" + path),
                                   std::regex_match(name, staffRoll), false));
    }
    progress(0.65, "Preparing sprites…");
    done = 0;
    for (const std::string& name : sprites) {
        const Sprite sprite =
            decodeSprite(assets.expanded("data_lt2/ani/" + name), name.ends_with(".arj"));
        const bool rotating = name.starts_with("nazo/tilerotate/");
        const std::string child = spriteChild(sprite);
        const Bytes data = nativeSprite(
            sprite, child.empty() ? std::string() : "sub/" + stripExtension(child) + ".arc",
            rotating);
        pack.save("ani/" + packName(name) + ".l2sp", data);
        if (++done % 64 == 0) {
            progress(0.65 + 0.30 * double(done) / double(sprites.size()), "Preparing sprites…");
        }
    }
    if (decuma) {
        pack.save("decuma/databas_le.bin", assets.raw("data_lt2/decuma/_databas_le.bin"));
    }
    for (const std::string& name : fonts) {
        const Font font = decodeFont(assets.raw("data_lt2/font/" + name));
        // Version 2 appends the ov2 width table; font18 (02072ab0) has none.
        const std::uint32_t literal = name == "fontevent.NFTR" ? 0x02072630
                                      : name == "fontq.NFTR"   ? 0x0207265c
                                                               : 0;
        const auto widths =
            literal ? overlay.at(overlay.word(literal), 224) : std::span<const std::uint8_t>{};
        const Bytes data = nativeFont(font, widths);
        pack.save("font/" + name + ".l2ft", data);
    }
    // The ROM's text escape table (ov2 0206bec8): {name pointer, byte} pairs
    // until a null pointer. A repeated name keeps its first position.
    std::vector<std::pair<std::string, std::uint32_t>> escapes;
    for (std::uint32_t entry = overlay.word(0x0206bec8); overlay.word(entry); entry += 8) {
        const std::string key = overlay.cString(overlay.word(entry));
        const std::uint32_t value = overlay.word(entry + 4);
        auto found = std::find_if(escapes.begin(), escapes.end(),
                                  [&](const auto& e) { return e.first == key; });
        if (found == escapes.end()) {
            escapes.emplace_back(key, value);
        } else {
            found->second = value;
        }
    }
    Bytes keyboard;
    putTag(keyboard, "L2KB", 1);
    for (const char* name : {"komoji.txt", "oomoji.txt", "kigou.txt", "tokushu.txt"}) {
        Bytes raw = assets.member("data_lt2/txt/input_txt.plz", name);
        const bool special = std::string_view(name) == "tokushu.txt";
        if (special) {
            Bytes substituted;
            for (std::size_t i = 0; i < raw.size(); ++i) {
                const auto close = std::find(raw.begin() + std::ptrdiff_t(i) + 1, raw.end(), '>');
                if (raw[i] == '<' && close != raw.end() &&
                    close != raw.begin() + std::ptrdiff_t(i) + 1) {
                    const std::string key(raw.begin() + std::ptrdiff_t(i) + 1, close);
                    const auto found = std::find_if(escapes.begin(), escapes.end(),
                                                    [&](const auto& e) { return e.first == key; });
                    check(found != escapes.end(), "unknown keyboard escape");
                    substituted.push_back(std::uint8_t(found->second));
                    i = std::size_t(close - raw.begin());
                } else {
                    substituted.push_back(raw[i]);
                }
            }
            substituted.resize(std::min<std::size_t>(substituted.size(), 52));
            raw = std::move(substituted);
        }
        check(raw.size() == (special ? 52u : 45u), "unexpected keyboard layout size");
        put32(keyboard, std::uint32_t(raw.size()));
        keyboard.insert(keyboard.end(), raw.begin(), raw.end());
    }
    pack.save("keyboard.bin", keyboard);
    pack.save("logo.gds", assets.raw("data_lt2/script/logo.gds"));
    pack.save("puzzle/q1_param.gds", assets.member("data_lt2/script/puzzle.plz", "q1_param.gds"));
    pack.save("puzzle/n1.dat", assets.member("data_lt2/nazo/ge/nazo1.plz", "n1.dat"));
    // Preserve the ROM's substitution table as data, including non-ASCII
    // character bytes. The native text service consumes it without a locale.
    pack.save("escapes.bin", escapeTableFile(escapes, '<', '>'));
    pack.save("stream/ST_001.SAD", assets.raw("data_lt2/stream/ST_001.SAD"));
    pack.save("stream/bgm/BG_001.SAD", assets.raw("data_lt2/stream/bgm/BG_001.SAD"));

    pack.writeProvenance(rom.sha256());
    progress(0.96, "Verifying the imported game…");
    return {pack.outputs()};
}

} // namespace picarat::import
