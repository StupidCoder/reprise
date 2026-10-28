#include "spindizzy_worlds_amiga/data/assets.hpp"
#include "platform/files.hpp"
#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace spindizzy::data {
namespace {

void out_of_range(std::uint32_t address) {
    char text[64];
    std::snprintf(text, sizeof text, "The game data has no byte at $%X.", address);
    throw std::runtime_error(text);
}

constexpr std::uint32_t main_base = 0x10000;        // the loader's JMP $1001C
constexpr std::uint32_t world_names = 0x119EE;      // 32 names of 12 characters + 0
constexpr std::uint32_t world_name_size = 13;
constexpr std::uint32_t block_attributes_at = 0x30BC0; // $125B4, $135C0
constexpr std::uint32_t pieces_at = 0x2EBC0;  // $12236
constexpr std::uint32_t masks_at = 0x30CC0;   // $12176
constexpr std::uint32_t tiles_at = 0x314F0;   // $12128
constexpr int mask_count = 131;               // up to the tiles
constexpr int tile_count = 1030;              // the highest tile the pieces name is 1029
constexpr std::uint32_t palettes_at = 0x110AE; // $110A8: 4 × 16 words

std::uint16_t be16(std::span<const std::uint8_t> b, std::size_t at) {
    if (at + 2 > b.size()) throw std::runtime_error("A world file is cut short.");
    return std::uint16_t(b[at] << 8 | b[at + 1]);
}

} // namespace

std::uint8_t Memory::u8(std::uint32_t address) const {
    if (!contains(address)) out_of_range(address);
    return bytes_[address - base_];
}

std::uint16_t Memory::u16(std::uint32_t address) const {
    return std::uint16_t(u8(address) << 8 | u8(address + 1));
}

std::span<const std::uint8_t> Memory::bytes(std::uint32_t address, std::uint32_t length) const {
    if (!contains(address, length)) out_of_range(address);
    return {bytes_.data() + (address - base_), length};
}

std::string Memory::text(std::uint32_t address, std::uint32_t limit) const {
    std::string result;
    for (std::uint32_t i = 0; i < limit && contains(address + i); ++i) {
        const char c = char(u8(address + i));
        if (c == 0) break;
        result.push_back(c);
    }
    return result;
}

Picture read_neo(std::span<const std::uint8_t> file) {
    if (file.size() < 0x80 + 32000) throw std::runtime_error("The title picture is cut short.");
    Picture picture;
    for (std::size_t i = 0; i < 16; ++i) {
        // $10A4E: add.w d0,d0 turns the ST's $0777 range into $0EEE.
        picture.palette[i] = Colour((be16(file, 4 + 2 * i) * 2) & 0xFFF);
    }
    picture.pixels.assign(std::size_t(picture.width * picture.height), 0);
    for (int y = 0; y < picture.height; ++y) {
        for (int group = 0; group < 20; ++group) {
            const std::size_t at = 0x80 + std::size_t(y * 20 + group) * 8; // four plane words per 16 pixels
            for (int bit = 0; bit < 16; ++bit) {
                std::uint8_t index = 0;
                for (int plane = 0; plane < 4; ++plane) {
                    index |= std::uint8_t((be16(file, at + 2 * std::size_t(plane)) >> (15 - bit) & 1) << plane);
                }
                picture.pixels[std::size_t(y * picture.width + group * 16 + bit)] = index;
            }
        }
    }
    return picture;
}

LevelHeader World::header(int level) const {
    const auto& data = levels.at(level).data;
    return {be16(data, 0), be16(data, 2), be16(data, 4), be16(data, 6)};
}

std::span<const std::uint8_t> World::commands(int id) const {
    if (id > 0) return levels.at(id).data.subspan(8); // $1206C: lea 8(a0),a0
    return macros.at(id).data;
}

World read_world(int number, std::string name, std::vector<std::uint8_t> bytes) {
    World world{number, std::move(name), 0, std::move(bytes), {}, {}};
    const std::span<const std::uint8_t> b(world.bytes);
    static constexpr char magic[] = "Cluster "; // checked by $1203A
    for (std::size_t i = 0; i < 8; ++i) {
        if (b.size() < 10 || b[i] != std::uint8_t(magic[i])) throw std::runtime_error("A world file has no \"Cluster\" tag.");
    }
    world.setting = be16(b, 8);
    // Records from +$A: id word (0 ends the list), length word, data ($12052, $126FA).
    for (std::size_t at = 10;;) {
        const auto id = std::int16_t(be16(b, at));
        if (id == 0) break;
        const std::size_t length = be16(b, at + 2);
        if (at + 4 + length > b.size()) throw std::runtime_error("A world record runs past its file.");
        const Record record{id, b.subspan(at + 4, length)};
        (id > 0 ? world.levels : world.macros)[id] = record;
        at += 4 + length;
    }
    return world;
}

SpriteFrame read_sprite_frame(const Memory& main, std::uint32_t address, int words, int rows) {
    SpriteFrame frame;
    frame.width = words * 16;
    frame.height = rows;
    frame.pixels.assign(std::size_t(frame.width * rows), 0);
    frame.opaque.assign(std::size_t(frame.width * rows), 0);
    const auto row_bytes = std::uint32_t(words) * 10;
    for (int r = 0; r < rows; ++r) {
        const std::uint32_t row = address + std::uint32_t(r) * row_bytes;
        for (int x = 0; x < frame.width; ++x) {
            const auto word = std::uint32_t(x / 16) * 2;
            const int bit = 15 - x % 16;
            const auto at = std::size_t(r * frame.width + x);
            frame.opaque[at] = std::uint8_t(main.u16(row + word) >> bit & 1);
            for (std::uint32_t plane = 0; plane < 4; ++plane) {
                const std::uint32_t plane_word = row + std::uint32_t(words) * 2 * (plane + 1) + word;
                frame.pixels[at] |= std::uint8_t((main.u16(plane_word) >> bit & 1) << plane);
            }
        }
    }
    return frame;
}

Graphics read_graphics(const Memory& main) {
    Graphics graphics;
    for (std::uint32_t type = 0; type < 256; ++type) {
        for (std::uint32_t i = 0; i < 8; ++i) {
            const std::uint32_t at = pieces_at + type * 32 + i * 4;
            const std::uint16_t tile = main.u16(at + 2);
            // $12328: a negative tile word means no piece; the mask is the first word's low byte.
            graphics.pieces[type][i] = {main.u8(at + 1), (tile & 0x8000) ? -1 : int(tile)};
        }
    }
    for (int m = 0; m < mask_count; ++m) {
        TileMask mask{};
        for (std::uint32_t r = 0; r < tile_height; ++r) mask[r] = main.u16(masks_at + std::uint32_t(m) * 16 + 2 * r);
        graphics.masks.push_back(mask);
    }
    for (int t = 0; t < tile_count; ++t) {
        Tile tile{};
        for (std::uint32_t r = 0; r < tile_height; ++r) {
            const std::uint32_t row = tiles_at + std::uint32_t(t) * 64 + r * 8;
            for (int x = 0; x < tile_width; ++x) {
                std::uint8_t index = 0;
                for (std::uint32_t plane = 0; plane < 4; ++plane) {
                    index |= std::uint8_t((main.u16(row + 2 * plane) >> (15 - x) & 1) << plane);
                }
                tile[r * tile_width + std::size_t(x)] = index;
            }
        }
        graphics.tiles.push_back(tile);
    }
    for (std::uint32_t p = 0; p < 4; ++p) {
        for (std::uint32_t i = 0; i < 16; ++i) {
            graphics.palettes[p][i] = Colour((main.u16(palettes_at + p * 32 + 2 * i) * 2) & 0xFFF);
        }
    }
    return graphics;
}

Assets load_assets(const std::filesystem::path& content) {
    Assets assets;
    assets.main = Memory(main_base, platform::read_file(content / "main"));
    assets.title = read_neo(platform::read_file(content / "title.neo"));
    const auto attributes = assets.main.bytes(block_attributes_at, 256);
    std::copy(attributes.begin(), attributes.end(), assets.block_attributes.begin());
    assets.graphics = read_graphics(assets.main);
    for (int n = 0; n < world_count; ++n) {
        char file[16];
        std::snprintf(file, sizeof file, "world%02d", n);
        auto name = assets.main.text(world_names + std::uint32_t(n) * world_name_size, 12);
        // The names are centred with spaces for the panel; trim them for lists.
        const auto first = name.find_first_not_of(' ');
        name = first == std::string::npos ? std::string() : name.substr(first, name.find_last_not_of(' ') - first + 1);
        assets.worlds.push_back(read_world(n, std::move(name), platform::read_file(content / file)));
    }
    return assets;
}

} // namespace spindizzy::data
