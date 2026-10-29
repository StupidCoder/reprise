// ROM decoding and the asset pack file format for levels.

#include "sonic1_gg/data/level_data.hpp"
#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace sonic1_gg {
Bytes read_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot read " + path.string());
    }
    return Bytes(std::istreambuf_iterator<char>(file), {});
}

void write_file(const std::filesystem::path& path, std::span<const std::uint8_t> data) {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot write " + path.string());
    }
    file.write(reinterpret_cast<const char*>(data.data()),
               static_cast<std::streamsize>(data.size()));
    if (!file) {
        throw std::runtime_error("Write failed: " + path.string());
    }
}

Rom::Rom(const std::filesystem::path& path) : bytes_(read_file(path)) {
    // All discovered offsets refer to this exact supplied cartridge revision.
    // Refuse other images rather than silently decode their bytes as this game.
    std::uint32_t crc = 0xffffffff;
    for (auto value : bytes_) {
        crc ^= value;
        for (int bit = 0; bit != 8; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
        }
    }
    if (bytes_.size() != 262144 || (crc ^ 0xffffffff) != 0x3e31cb8c) {
        throw std::runtime_error("Expected the 256 KiB Sonic 1 Game Gear ROM, CRC32 3E31CB8C");
    }
}

std::uint8_t Rom::byte(std::size_t offset) const {
    return bytes_.at(offset);
}

std::uint16_t Rom::word(std::size_t offset) const {
    return static_cast<std::uint16_t>(byte(offset) | (byte(offset + 1) << 8));
}

Bytes Rom::slice(std::size_t offset, std::size_t count) const {
    if (offset > bytes_.size() || count > bytes_.size() - offset) {
        throw std::runtime_error("ROM range outside image");
    }
    return Bytes(bytes_.begin() + offset, bytes_.begin() + offset + count);
}

// Tile graphics compression (0406-04F9). Every 4-byte tile row is either a
// literal or a reference into a dictionary of previously stored rows.
Bytes decode_pattern_rows(const Rom& rom, std::size_t base) {
    // Header words +2/+4 locate the reference stream and literal dictionary.
    // Word +6 counts four-byte rows. The bitset at +8 chooses each row's source.
    auto references = base + rom.word(base + 2);
    const auto dictionary = base + rom.word(base + 4);
    auto literals = dictionary;
    const auto count = rom.word(base + 6);
    if (count > 4096) {
        throw std::runtime_error("Pattern block exceeds VRAM capacity");
    }
    Bytes result;
    result.reserve(count * 4);
    for (unsigned row = 0; row < count; ++row) {
        std::size_t source;
        if (rom.byte(base + 8 + row / 8) & (1u << (row % 8))) {
            unsigned index = rom.byte(references++);
            // F0-FF introduce a twelve-bit dictionary index; smaller indices
            // occupy one byte. Each dictionary entry is one complete tile row.
            if (index >= 0xf0) {
                index = ((index - 0xf0) << 8) | rom.byte(references++);
            }
            source = dictionary + index * 4;
        } else {
            source = literals;
            literals += 4;
        }
        for (unsigned plane = 0; plane < 4; ++plane) {
            result.push_back(rom.byte(source + plane));
        }
    }
    return result;
}

// Layout compression (0A73-0AA2): a byte repeated twice is followed by a count
// of further repeats.
Bytes decode_layout(const Rom& rom, std::size_t offset, std::size_t compressed_size) {
    Bytes result;
    int previous = -1;
    const auto end = offset + compressed_size;
    while (offset < end) {
        auto value = rom.byte(offset++);
        if (value == previous) {
            // The first occurrence was emitted as a literal. Its repeat marks
            // a count of ADDITIONAL occurrences. Z80 DJNZ treats zero as 256.
            if (offset == end) {
                break;
            }
            unsigned count = rom.byte(offset++);
            result.insert(result.end(), count ? count : 256, value);
            previous = -1;
        } else {
            result.push_back(value);
            previous = value;
        }
        if (result.size() > 8192) {
            throw std::runtime_error("Layout exceeds cartridge RAM");
        }
    }
    return result;
}

std::uint8_t Level::block_at(int x, int y) const {
    // 30D5-31B8 forms a flat RAM address, not a clamped rectangle. The
    // X block coordinate is eight bits; crossing a narrow map's right edge
    // can read the first cell of the following row (bonus act 31 does this).
    unsigned column = ((unsigned(x) & 65535) >> 5) & 255;
    unsigned index = ((((unsigned(y) & 65535) >> 5) * columns) + column) & 65535;
    // Reads beyond the extracted 4 KiB layout remain outside this model.
    return index < layout.size() ? layout[index] : 0;
}

int Level::data(unsigned address) const {
    auto next = object_tables.upper_bound(address);
    if (next == object_tables.begin()) {
        throw std::runtime_error("Unknown object data address");
    }
    auto entry = std::prev(next);
    return entry->second.at(address - entry->first);
}

// ROM ranges of the data tables read by the object code (Level::data).
static const std::array<std::pair<unsigned, unsigned>, 79> object_ranges = {
    {{0x510a, 0x513a}, // 50CE: invincibility star offsets
     {0xbf28, 0xbf53}, {0xbff8, 0xbfff}, {0xba2d, 0xbb0a}, {0xbc3c, 0xbcf2}, {0xbde5, 0xbe03},
     {0xa97d, 0xa98d}, {0xbb0a, 0xbb95}, {0xaa29, 0xaa30}, {0xaac7, 0xaae7}, {0xacb7, 0xad32},
     {0xadc3, 0xadfb}, {0xae47, 0xae4e}, {0xb1ed, 0xb25d}, {0xb355, 0xb372}, {0xb435, 0xb447},
     {0xb4c0, 0xb4e8}, {0x9bae, 0x9bbd}, {0x9c3e, 0x9c45}, {0x9c5c, 0x9c63}, {0x9d13, 0x9dc3},
     {0x9ef4, 0x9f2b}, {0x9fb7, 0x9fee}, {0xa07a, 0xa0b1}, {0xa130, 0xa173}, {0xa29b, 0xa305},
     {0xa37a, 0xa384}, {0xa4e3, 0xa51a}, {0xa682, 0xa7b6}, {0x58e0, 0x58f9}, {0x216, 0x296},
     {0x52fa, 0x5300}, {0x88c9, 0x8910}, {0x8999, 0x8b08}, {0x8bce, 0x8c28}, {0x8d4a, 0x8d59},
     {0x8e3e, 0x8e6a}, {0x8e93, 0x8ecf}, {0x904a, 0x90c5}, {0x91e8, 0x91f5}, {0x9261, 0x9271},
     {0x947d, 0x94a7}, {0x968a, 0x96aa}, {0x96f7, 0x96fa}, {0xa454, 0xa474}, {0xaff7, 0xb0ba},
     {0x8209, 0x822d}, {0x82d6, 0x82fb}, {0xa384, 0xa3c1}, {0x8389, 0x83d6}, {0x8496, 0x84ab},
     {0x8647, 0x8681}, {0x8845, 0x884c}, {0xb58f, 0xb5a1}, {0x9973, 0x9a84}, {0x550b, 0x551d},
     {0x9b43, 0x9b4a}, {0x5e97, 0x5eb1}, {0x64a8, 0x65f9}, {0x66d0, 0x6747}, {0x682e, 0x693e},
     {0x69bb, 0x69ed}, {0x6bd5, 0x6bd9}, {0x6d3c, 0x6dca}, {0x6eef, 0x6f61}, {0x7b99, 0x7bd1},
     {0x7c53, 0x7c75}, {0x5c1b, 0x5c5b}, {0x61f1, 0x61f8}, {0x7046, 0x7059}, {0x7e0b, 0x7e31},
     {0x7eb6, 0x7ec8}, {0x7eef, 0x7efc}, {0x8031, 0x8070}, {0x729a, 0x736b}, {0x754d, 0x75d3},
     {0x7668, 0x76d8}, {0x7791, 0x77fd}, {0x795e, 0x7a36}}};

// Decodes one act. The level header (37 bytes, located through the table at
// bank 5 / 5600) holds: scene (zone graphics set), layout width and height,
// camera limits, spawn cell, layout and tile-block offsets, pattern and palette
// selectors, palette cycle timing, the placement list offset, four level flags
// (water, auto-scroll, swaying camera, ...) and the music number.
Level extract_level(const Rom& rom, unsigned act) {
    Level level;
    // Discovered from 1414-1439: bank 5, table at CPU address 5600. Entries
    // are offsets relative to that table, not absolute CPU addresses.
    const std::size_t header = 0x15600 + rom.word(0x15600 + act * 2);
    level.act = act;
    level.linked_area = act >= 20 && act < 28;
    level.scene = rom.byte(header);
    level.music = rom.byte(header + 36);
    for (unsigned i = 0; i < 4; ++i) {
        level.initial_flags[i] = rom.byte(header + 32 + i);
    }
    level.columns = rom.word(header + 1);
    level.rows = rom.word(header + 3);
    level.camera_left = rom.word(header + 5);
    level.camera_right = rom.word(header + 7);
    level.camera_top = rom.word(header + 9);
    level.camera_bottom = rom.word(header + 11);
    level.spawn_x = rom.byte(header + 13) * 32;
    level.spawn_y = rom.byte(header + 14) * 32;
    level.layout = decode_layout(rom, 0x14000 + rom.word(header + 15), rom.word(header + 17));
    if (level.layout.size() != std::size_t(level.columns) * level.rows &&
        !(act == 21 && level.layout.size() == 2048)) {
        throw std::runtime_error("Decoded layout dimensions disagree with header");
    }
    level.blocks = rom.slice(0x10000 + rom.word(header + 19), 256 * 16);
    level.background_patterns = decode_pattern_rows(rom, 0x30000 + rom.word(header + 21));
    level.object_patterns =
        decode_pattern_rows(rom, rom.byte(header + 23) * 0x4000 + rom.word(header + 24));

    // 0586-05EF obtains palette offsets from bank 8's table at CPU 7400.
    const auto background_palette = 0x23400 + rom.word(0x23400 + 2 * rom.byte(header + 29));
    const auto object_palette = 0x23400 + rom.word(0x23400 + 2 * rom.byte(header + 26));
    for (unsigned index : {8u, 19u, 20u}) {
        auto colors = rom.slice(0x23400 + rom.word(0x23400 + index * 2), 128);
        level.electric_palettes.insert(level.electric_palettes.end(), colors.begin(), colors.end());
    }
    level.palette = rom.slice(background_palette, 32);
    level.palette.push_back(level.palette[0]);
    level.palette.push_back(level.palette[1]);
    level.palette_cycle = {rom.byte(header + 27), rom.byte(header + 28)};
    auto cycles = rom.slice(background_palette, 32 * level.palette_cycle[1]);
    level.palette_cycle.insert(level.palette_cycle.end(), cycles.begin(), cycles.end());
    auto object_colors = rom.slice(object_palette + 2, 30);
    level.palette.insert(level.palette.end(), object_colors.begin(), object_colors.end());

    // Sonic's graphics are stored as three bitplanes. 31BC-320B inserts an
    // empty fourth plane during VRAM upload. 4EB5-4EC4 selects 192-byte frames.
    for (unsigned frame = 0; frame < 64; ++frame) {
        for (unsigned row = 0; row < 64; ++row) {
            const auto source = 0x20000 + frame * 192 + row * 3;
            for (unsigned plane = 0; plane < 3; ++plane) {
                level.player_patterns.push_back(rom.byte(source + plane));
            }
            level.player_patterns.push_back(0);
        }
    }
    // 5487 selects bank-8 CPU 5800 + (35 or 36)*192 on even ticks.
    for (unsigned row = 0; row < 2 * 64; ++row) {
        for (unsigned plane = 0; plane < 3; ++plane) {
            level.shield_patterns.push_back(rom.byte(0x23240 + row * 3 + plane));
        }
        level.shield_patterns.push_back(0);
    }
    level.animation_data = rom.slice(0x5c5b, 0x5de1 - 0x5c5b);
    const auto placement = 0x15600 + rom.word(header + 30);
    level.placements = rom.slice(placement + 1, (rom.byte(placement) - 1) * 3);
    level.active_bounds = rom.slice(0x2560, 0x57 * 8);
    level.terrain_actions = rom.slice(0x16200 + rom.word(0x16200 + level.scene * 2), 256);
    level.common_patterns = decode_pattern_rows(rom, 0x2f354);
    level.monitor_icons = rom.slice(0x15200, 0x400);
    level.emerald_patterns = rom.slice(0x15480, 128);
    level.goal_patterns = decode_pattern_rows(rom, 0x27ab8);
    // 7083 and 7952 pass bank + an unrestricted 16-bit offset to 0406.
    level.boss_patterns = decode_pattern_rows(rom, (act == 14 || act == 17)  ? 12 * 0x4000 + 0xf00c
                                                   : (act == 5 || act == 11) ? 12 * 0x4000 + 0xe5de
                                                                             : 9 * 0x4000 + 0xa8b7);
    level.capsule_patterns = decode_pattern_rows(rom, 12 * 0x4000 + 0xdad0);
    level.boss_palette = rom.slice(0x23400 + rom.word(0x23400 + 0x11 * 2), 32);
    level.ambient_patterns = rom.slice(0x2f73d, 0x400);
    for (unsigned row = 0; row < 3 * 64; ++row) {
        for (unsigned plane = 0; plane < 3; ++plane) {
            level.lost_ring_patterns.push_back(rom.byte(0x23000 + row * 3 + plane));
        }
        level.lost_ring_patterns.push_back(0);
    }
    level.goal_palette = level.palette;
    auto goal_colors = rom.slice(0x23400 + rom.word(0x23400 + 0x0e * 2) + 2, 30);
    std::copy(goal_colors.begin(), goal_colors.end(), level.goal_palette.begin() + 34);
    for (auto [start, end] : object_ranges) {
        level.object_tables[start] = rom.slice(start, end - start);
    }

    const auto attributes = rom.word(0x343d + rom.byte(header) * 2);
    level.block_attributes = rom.slice(attributes, 256);
    level.floor_push.resize(256);
    level.wall_push.resize(256);
    level.vertical_tolerance.resize(256);
    for (auto member : {&Level::left_contours, &Level::right_contours, &Level::ceiling_contours,
                        &Level::floor_contours}) {
        (level.*member).assign(256 * 32, 0x80);
    }
    // Collision: each block's attribute selects one of 48 profiles; a profile
    // has four 32-entry contours (2D15-2E4D) and floor/wall pushes. Only blocks
    // that can appear in this act get their contours filled in.
    std::array<bool, 256> used{};
    for (auto block : level.layout) {
        used[block] = true;
        // 5000-5034 clears either ring-presence bit independently.
        if ((block & 127) >= 0x79) {
            for (unsigned mask = 0; mask < 4; ++mask) {
                used[block & ~mask] = true;
            }
        }
    }
    // 5B10/5B4D replace a Bridge plank by its surviving left/right half.
    // These profiles do not occur in the initial map but remain collidable.
    if (level.scene == 1) {
        used[0x89] = used[0x8a] = used[0] = true;
    }
    if (act == 21) {
        used.fill(true); // The unwritten lower half retains the previous area layout (0A73).
    }
    for (unsigned block = 0; block < 256; ++block) {
        if (!used[block]) {
            continue;
        }
        auto type = level.block_attributes[block] & 0x3f;
        if (!type) {
            continue;
        }
        if (type >= 48) {
            throw std::runtime_error("Undocumented collision profile");
        }
        const std::array<std::pair<Bytes*, unsigned>, 4> tables = {
            {{&level.left_contours, 0x3a0a},
             {&level.right_contours, 0x3b0a},
             {&level.ceiling_contours, 0x3bda},
             {&level.floor_contours, 0x3e7a}}};
        for (auto [destination, table] : tables) {
            auto values = rom.slice(rom.word(table + type * 2), 32);
            std::copy(values.begin(), values.end(), destination->begin() + block * 32);
        }
        level.floor_push[block] = rom.byte(0x3978 + type);
        level.wall_push[block] = rom.byte(0x39a8 + type);
        level.vertical_tolerance[block] = rom.byte(0x39da + type);
    }
    return level;
}

static const std::array<std::pair<const char*, Bytes Level::*>, 31> files = {
    {{"electric_palettes.bin", &Level::electric_palettes},
     {"boss.bin", &Level::boss_patterns},
     {"boss_palette.bin", &Level::boss_palette},
     {"capsule.bin", &Level::capsule_patterns},
     {"layout.bin", &Level::layout},
     {"blocks.bin", &Level::blocks},
     {"block_attributes.bin", &Level::block_attributes},
     {"background.bin", &Level::background_patterns},
     {"objects.bin", &Level::object_patterns},
     {"shield.bin", &Level::shield_patterns},
     {"palette.bin", &Level::palette},
     {"player.bin", &Level::player_patterns},
     {"collision_left.bin", &Level::left_contours},
     {"collision_right.bin", &Level::right_contours},
     {"collision_ceiling.bin", &Level::ceiling_contours},
     {"collision_floor.bin", &Level::floor_contours},
     {"floor_push.bin", &Level::floor_push},
     {"wall_push.bin", &Level::wall_push},
     {"vertical_tolerance.bin", &Level::vertical_tolerance},
     {"animation.bin", &Level::animation_data},
     {"placements.bin", &Level::placements},
     {"active_bounds.bin", &Level::active_bounds},
     {"terrain_actions.bin", &Level::terrain_actions},
     {"common.bin", &Level::common_patterns},
     {"monitor_icons.bin", &Level::monitor_icons},
     {"goal.bin", &Level::goal_patterns},
     {"goal_palette.bin", &Level::goal_palette},
     {"ambient.bin", &Level::ambient_patterns},
     {"lost_rings.bin", &Level::lost_ring_patterns},
     {"emerald.bin", &Level::emerald_patterns},
     {"palette_cycle.bin", &Level::palette_cycle}}};

void save_level(const Level& level, const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);
    Bytes header{'S', 'G', 'G', '2'};
    for (auto value : {level.columns, level.rows, level.camera_left, level.camera_right,
                       level.camera_top, level.camera_bottom, level.spawn_x, level.spawn_y}) {
        header.push_back(value & 255);
        header.push_back(value >> 8);
    }
    write_file(directory / "level.bin", header);
    write_file(
        directory / "level_kind.bin",
        Bytes{std::uint8_t(level.act), std::uint8_t(level.scene), std::uint8_t(level.music)});
    write_file(directory / "initial_flags.bin", level.initial_flags);
    for (auto [name, member] : files) {
        write_file(directory / name, level.*member);
    }
    for (auto& [address, data] : level.object_tables) {
        write_file(directory / ("table_" + std::to_string(address) + ".bin"), data);
    }
    std::ofstream manifest(directory / "manifest.json");
    manifest << "{\n  \"format\": \"SGG2\",\n  \"rom_crc32\": \"3e31cb8c\",\n"
             << "  \"level\": " << level.act << ",\n  \"layout_columns\": " << level.columns
             << ",\n  \"layout_rows\": " << level.rows << ",\n  \"block_pixels\": 32\n}\n";
}

Level load_level(const std::filesystem::path& directory) {
    Level level;
    auto kind = read_file(directory / "level_kind.bin");
    if (kind.size() != 3) {
        throw std::runtime_error("Invalid level kind");
    }
    level.act = kind[0];
    level.linked_area = level.act >= 20 && level.act < 28;
    level.scene = kind[1];
    level.music = kind[2];
    auto flags = read_file(directory / "initial_flags.bin");
    if (flags.size() != 4) {
        throw std::runtime_error("Invalid initial flags");
    }
    std::copy_n(flags.begin(), 4, level.initial_flags.begin());
    auto h = read_file(directory / "level.bin");
    if (h.size() != 20 || std::string(h.begin(), h.begin() + 4) != "SGG2") {
        throw std::runtime_error("Unsupported level asset format");
    }
    auto get = [&](int i) {
        return static_cast<std::uint16_t>(h[i] | (h[i + 1] << 8));
    };
    level.columns = get(4);
    level.rows = get(6);
    level.camera_left = get(8);
    level.camera_right = get(10);
    level.camera_top = get(12);
    level.camera_bottom = get(14);
    level.spawn_x = get(16);
    level.spawn_y = get(18);
    for (auto [name, member] : files) {
        level.*member = read_file(directory / name);
    }
    for (auto [start, end] : object_ranges) {
        auto data = read_file(directory / ("table_" + std::to_string(start) + ".bin"));
        if (data.size() != end - start) {
            throw std::runtime_error("Truncated object data table");
        }
        level.object_tables[start] = std::move(data);
    }
    if (std::size_t(level.columns) * level.rows != 4096 ||
        (level.layout.size() != std::size_t(level.columns) * level.rows &&
         !(level.act == 21 && level.layout.size() == 2048)) ||
        level.palette.size() != 64 || level.blocks.size() != 4096 ||
        level.background_patterns.size() != 8192 || level.shield_patterns.size() != 512 ||
        level.player_patterns.size() != 16384 || level.object_patterns.size() != 4096 ||
        level.block_attributes.size() != 256 || level.floor_push.size() != 256 ||
        level.wall_push.size() != 256 || level.vertical_tolerance.size() != 256 ||
        level.left_contours.size() != 8192 || level.right_contours.size() != 8192 ||
        level.floor_contours.size() != 8192 || level.ceiling_contours.size() != 8192 ||
        level.animation_data.size() != 0x186 || level.electric_palettes.size() != 384 ||
        level.placements.size() % 3 != 0 || level.placements.size() > 93 ||
        level.active_bounds.size() != 0x2b8 || level.terrain_actions.size() != 256 ||
        level.monitor_icons.size() != 0x400 || level.goal_palette.size() != 64 ||
        level.goal_patterns.size() != 4096 || level.common_patterns.size() != 2048 ||
        level.ambient_patterns.size() != 1024 || level.lost_ring_patterns.size() != 768 ||
        level.emerald_patterns.size() != 128 || level.palette_cycle.size() < 34 ||
        level.palette_cycle.size() != 2 + 32 * std::size_t(level.palette_cycle[1]) ||
        level.camera_left > level.camera_right || level.camera_top > level.camera_bottom) {
        throw std::runtime_error("Incomplete or incompatible extracted assets");
    }
    return level;
}
} // namespace sonic1_gg
