#include "tower_of_babel_amiga/data/assets.hpp"
#include "platform/files.hpp"
#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace babel::data {
namespace {

void out_of_range(std::uint32_t address) {
    char text[64];
    std::snprintf(text, sizeof text, "The game data has no byte at $%X.", address);
    throw std::runtime_error(text);
}

// Where the installed files sit in the original's memory.
constexpr std::uint32_t program_base = 0x800;
constexpr std::uint32_t title_base = 0x18200;  // $7B4E
constexpr std::uint32_t main_base = 0x1C5F0;   // $7F6E
constexpr std::uint32_t sample_base = 0x8200;  // the sound code adds offsets to $79FA(a6) = $8200

// Title screen ($7EF6-$7F4A and copper mode 3 at $8A6A).
constexpr std::uint32_t title_palette = 0x2C9E; // $2498(a6), faded in by $1252C
constexpr std::uint32_t logo_at = 0x18200;      // $7F1A: 20 words x 35 rows at (0, 10)
constexpr std::uint32_t globe_at = 0x197E0;     // $7F38: 10 words x 133 rows at (80, 61)

// Tower groups: the addresses the game-type code loads ($91C0 standard,
// $8F2C tutorial, $9046 custom default, $158C6 demonstrations).
struct GroupPlace { const char* name; std::uint32_t address; int towers; };
constexpr GroupPlace tutorial{"Tutorial", 0x5FF32, 9};
constexpr GroupPlace standard{"Standard", 0x61372, 13 * 9}; // 13 groups of 9 ($A20 bytes each)
constexpr GroupPlace demos{"Demonstrations", 0x5E0D2, 18};
constexpr GroupPlace custom{"Custom (empty)", 0x5F512, 9};

constexpr std::uint32_t object_names = 0x6F10;   // designer's list, zero-terminated names
constexpr int object_name_count = 46;

constexpr std::uint32_t effect_first = 0x3504;   // first of the effect records
constexpr std::uint32_t effect_size = 0x2E;
constexpr std::uint32_t title_music_list = 0x3B24; // started at $7F06
constexpr std::uint32_t jingle_list = 0x3AF2;      // started at $9A8E

// The tower record's bit reader ($1390A): most significant bit first.
class BitReader {
public:
    explicit BitReader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}
    int get(int bits) {
        int value = 0;
        while (bits-- > 0) {
            if (position_ / 8 >= bytes_.size()) throw std::runtime_error("A tower record is cut short.");
            value = (value << 1) | (bytes_[position_ / 8] >> (7 - position_ % 8) & 1);
            ++position_;
        }
        return value;
    }
    std::string text(int length) { // 7-bit characters; the first non-printable ends it
        std::string result;
        bool ended = false;
        for (int i = 0; i < length; ++i) {
            const int c = get(7);
            if (c < 0x20 || c >= 0x7F) ended = true; // $136FE clears these
            if (!ended) result.push_back(char(c));
        }
        return result;
    }

private:
    std::span<const std::uint8_t> bytes_;
    std::size_t position_{};
};

// The 3-bit cell codes of the tower map, as $133F8 translates them ($1375A).
constexpr std::array<std::uint8_t, 8> cell_codes = {0x00, 0xC0, 0xC8, 0xE0, 0xF0, 0x30, 0xE8, 0x00};

TowerGroup read_group(const Memory& main, const GroupPlace& place) {
    TowerGroup group{place.name, {}};
    for (int i = 0; i < place.towers; ++i) {
        group.towers.push_back(decode_tower(main.bytes(place.address + std::uint32_t(i) * tower_record_size,
                                                       tower_record_size)));
    }
    return group;
}

// A segment list for the player $1570A: volume, period, then (offset.l,
// length.w in words) until a negative offset.
SampledPiece read_piece(const char* name, const Memory& program, std::uint32_t list, const Memory& samples) {
    SampledPiece piece{name, program.u16(list), program.u16(list + 2), {}};
    for (std::uint32_t at = list + 4; std::int32_t(program.u32(at)) >= 0; at += 6) {
        const auto data = samples.bytes(sample_base + program.u32(at), std::uint32_t(program.u16(at + 4)) * 2);
        piece.samples.insert(piece.samples.end(), data.begin(), data.end());
    }
    return piece;
}

} // namespace

std::uint8_t Memory::u8(std::uint32_t address) const {
    if (!contains(address)) out_of_range(address);
    return bytes_[address - base_];
}

std::uint16_t Memory::u16(std::uint32_t address) const {
    return std::uint16_t(u8(address) << 8 | u8(address + 1));
}

std::uint32_t Memory::u32(std::uint32_t address) const {
    return std::uint32_t(u16(address)) << 16 | u16(address + 2);
}

std::span<const std::uint8_t> Memory::bytes(std::uint32_t address, std::uint32_t length) const {
    if (!contains(address, length)) out_of_range(address);
    return {bytes_.data() + (address - base_), length};
}

std::string Memory::text(std::uint32_t address, std::uint32_t limit) const {
    std::string result;
    for (std::uint32_t i = 0; i < limit && u8(address + i) != 0; ++i) result.push_back(char(u8(address + i)));
    return result;
}

Picture read_picture(std::span<const std::uint8_t> bytes, int width_words, int height) {
    Picture picture{width_words * 16, height, {}};
    picture.pixels.resize(std::size_t(picture.width) * std::size_t(height));
    if (bytes.size() < std::size_t(width_words) * std::size_t(height) * 8) {
        throw std::runtime_error("A picture is cut short.");
    }
    std::size_t at = 0;
    for (int y = 0; y < height; ++y) {
        for (int column = 0; column < width_words; ++column, at += 8) {
            for (int bit = 0; bit < 16; ++bit) {
                std::uint8_t index = 0;
                for (int plane = 0; plane < 4; ++plane) {
                    const unsigned word = unsigned(bytes[at + 2 * std::size_t(plane)]) << 8 | bytes[at + 2 * std::size_t(plane) + 1];
                    index |= std::uint8_t((word >> (15 - bit) & 1) << plane);
                }
                picture.pixels[std::size_t(y) * std::size_t(picture.width) + std::size_t(column * 16 + bit)] = index;
            }
        }
    }
    return picture;
}

Tower decode_tower(std::span<const std::uint8_t> record) {
    // Field order of $13278; widths are the D1 values + 1 it passes to $1390A.
    BitReader in(record);
    Tower tower;
    tower.name = in.text(62);     // $D22
    tower.author = in.text(62);   // $D60
    tower.password = in.text(10); // $D9E
    tower.time_limit = in.get(14);
    for (auto& flag : tower.flags) flag = in.get(1) != 0;
    tower.settings[0] = std::min(in.get(2), 3);  // $E2E (clamped by $136F4)
    tower.klondikes = in.get(3);                  // $E30
    tower.objects_to_destroy = in.get(3);         // $E32
    tower.settings[1] = in.get(2);                // $E34
    tower.settings[2] = in.get(3);                // $E36
    tower.settings[3] = in.get(2);                // $E38
    tower.settings[4] = in.get(3);                // $E3A
    tower.settings[5] = in.get(3);                // $E3C
    tower.settings[6] = std::min(in.get(9), 0x11); // $E3E
    tower.settings[7] = std::min(in.get(7), 0x37); // $E40
    tower.settings[8] = in.get(2);                // $E42
    for (auto& floor : tower.cells) {
        for (auto& row : floor) {
            for (auto& cell : row) cell = cell_codes[std::size_t(in.get(3))];
        }
    }
    for (int slot = 0; slot < 20; ++slot) {
        const int kind = in.get(6);
        const int place = in.get(8);
        if (kind < 4 || kind >= 0x28) continue; // $13452: only kinds 4-39 are placed
        tower.objects.push_back({kind, place & 7, place >> 3 & 7, place >> 6 & 3});
    }
    for (auto& colour : tower.colours) {
        const int r = in.get(4), g = in.get(4), b = in.get(4);
        colour = Colour(r << 8 | g << 4 | b);
    }
    tower.settings[9] = in.get(2);  // $A4(a6)
    tower.settings[10] = in.get(1); // $A6(a6)
    tower.settings[11] = in.get(1); // $A8(a6)
    return tower;
}

std::string object_kind_name(const Memory& program, int kind) {
    std::uint32_t at = object_names;
    for (int i = 0; i < object_name_count; ++i) {
        const auto name = program.text(at, 64);
        if (i == kind) return name;
        at += std::uint32_t(name.size()) + 1;
    }
    return "Kind " + std::to_string(kind);
}

Assets load_assets(const std::filesystem::path& content) {
    Assets assets;
    assets.program = Memory(program_base, platform::read_file(content / "program"));
    assets.main = Memory(main_base, platform::read_file(content / "main"));
    assets.music = Memory(main_base, platform::read_file(content / "music"));
    for (std::size_t i = 0; i < assets.panels.size(); ++i) {
        assets.panels[i] = platform::read_file(content / ("panel" + std::to_string(i + 1)));
    }
    assets.title_memory = Memory(title_base, platform::read_file(content / "title"));
    const Memory& title = assets.title_memory;

    auto& screen = assets.title;
    screen.logo = read_picture(title.bytes(logo_at, 20 * 35 * 8), 20, 35);
    screen.globe = read_picture(title.bytes(globe_at, 10 * 133 * 8), 10, 133);
    for (std::size_t i = 0; i < screen.palette.size(); ++i) {
        screen.palette[i] = assets.program.u16(title_palette + 2 * std::uint32_t(i));
    }
    screen.band2_line = 0x36; // $8A80
    screen.band3_line = 0x71; // $8A9A

    assets.groups.push_back(read_group(assets.main, tutorial));
    const auto all = read_group(assets.main, standard);
    for (int g = 0; g < 13; ++g) {
        TowerGroup group{"Group " + std::to_string(g), {}};
        group.towers.assign(all.towers.begin() + g * 9, all.towers.begin() + g * 9 + 9);
        assets.groups.push_back(std::move(group));
    }
    assets.groups.push_back(read_group(assets.main, demos));
    assets.groups.push_back(read_group(assets.main, custom));

    // Effect records run until one names no waveform inside `main`.
    for (std::uint32_t record = effect_first;; record += effect_size) {
        const std::uint32_t waveform = sample_base + assets.program.u32(record + 0x18);
        if (!assets.main.contains(waveform, 2)) break;
        const std::uint32_t length = std::uint32_t(assets.main.u16(waveform)) * 2;
        assets.effects.push_back({record, waveform, assets.main.bytes(waveform + 2, length)});
    }
    assets.pieces.push_back(read_piece("title music", assets.program, title_music_list, assets.music));
    assets.pieces.push_back(read_piece("jingle", assets.program, jingle_list, assets.main));
    return assets;
}

} // namespace babel::data
