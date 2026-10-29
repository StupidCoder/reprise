#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <utility>
#include <vector>

// Reading the ROM and the level data extracted from it.
//
// The importer decodes everything the game needs from the ROM once and stores
// it as small binary files in the asset pack (see asset_pack.hpp). At run time
// the game only loads those files; it never reads the ROM again. Data tables the
// object code reads by address (animation scripts, paths, sprite layouts) are
// kept under their original ROM addresses in Level::object_tables, so the code
// can refer to them by the same numbers as the original routines.
namespace sonic1_gg {

using Bytes = std::vector<std::uint8_t>;
Bytes read_file(const std::filesystem::path& path);
void write_file(const std::filesystem::path& path, std::span<const std::uint8_t> bytes);

// Where an extraction's files go: a folder (BlueStreak's own importer) or memory (an
// application that installs the files itself, such as Reprise's library). Names are
// relative to the pack, with '/' between folders.
class PackWriter {
public:
    virtual ~PackWriter() = default;
    virtual void write(const std::string& name, std::span<const std::uint8_t> bytes) = 0;
};

class FolderWriter final : public PackWriter {
public:
    explicit FolderWriter(std::filesystem::path root) : root_(std::move(root)) {}
    void write(const std::string& name, std::span<const std::uint8_t> bytes) override;

private:
    std::filesystem::path root_;
};

class MemoryWriter final : public PackWriter {
public:
    void write(const std::string& name, std::span<const std::uint8_t> bytes) override {
        files.emplace_back(name, Bytes(bytes.begin(), bytes.end()));
    }
    std::vector<std::pair<std::string, Bytes>> files;
};

// The supported cartridge image (checked by size and CRC32).
class Rom {
public:
    explicit Rom(const std::filesystem::path& path);
    explicit Rom(Bytes bytes);
    std::uint8_t byte(std::size_t offset) const;
    std::uint16_t word(std::size_t offset) const;
    Bytes slice(std::size_t offset, std::size_t count) const;

private:
    Bytes bytes_;
};

// Original routine 0406-04F9: dictionary coding of four-byte pattern rows.
Bytes decode_pattern_rows(const Rom& rom, std::size_t offset);
// Original routine 0A73-0AA2: duplicate-byte run-length coding.
Bytes decode_layout(const Rom& rom, std::size_t offset, std::size_t compressed_size);

// One act (or linked room, or special stage) as the game uses it.
//
// The level is a grid of 32x32-pixel blocks (`layout`); each block is a 4x4 grid
// of 8x8 tiles (`blocks`) and has an attribute byte selecting its collision
// profile (`block_attributes`, low 6 bits; bit 7 = foreground priority).
struct Level {
    unsigned act{}, scene{}, music{};
    bool linked_area{}; // Header 20–27, independent of the logical act number.
    std::array<std::uint8_t, 4> initial_flags{}; // Header bytes 32..35, original D205..D208.
    std::uint16_t columns{}, rows{};             // One layout cell is 32 x 32 world pixels.
    std::uint16_t camera_left{}, camera_right{}, camera_top{}, camera_bottom{};
    std::uint16_t spawn_x{}, spawn_y{};
    Bytes layout, blocks, block_attributes;
    Bytes electric_palettes; // Three four-frame palette sets selected by 16E4.
    Bytes palette_cycle;     // duration, frame count, then 32 bytes per background palette
    Bytes background_patterns, object_patterns, palette, player_patterns;
    Bytes shield_patterns; // Two immutable 16x32 frames, ROM 23240/23300.
    // Four directions, each with 32 contour samples per block type; 0x80 is no surface.
    Bytes left_contours, right_contours, ceiling_contours, floor_contours;
    Bytes floor_push, wall_push, vertical_tolerance;
    Bytes animation_data; // Original pointer table and bytecode, base 0x5C5B.
    Bytes placements, active_bounds, terrain_actions;
    Bytes boss_patterns, boss_palette, capsule_patterns;
    std::map<unsigned, unsigned> tile_overrides; // Persistent world 8x8 nametable patches.
    Bytes common_patterns, monitor_icons, goal_patterns, goal_palette, ambient_patterns,
        lost_ring_patterns, emerald_patterns;
    // Individually identified animation, path and sprite-layout tables. The
    // extracted pack contains data only; no Z80 instructions are executed.
    std::map<unsigned, Bytes> object_tables;

    // A byte of one of the original data tables, by ROM/CPU address.
    // Level header flags (initial_flags, RAM D205-D208).
    bool animated_tiles() const { return initial_flags[0] & 0x04; } // Waterfall tiles

    bool auto_scrolls() const { return initial_flags[0] & 0x08; } // Bridge 2

    bool sways() const { return initial_flags[0] & 0x40; } // Sky Base

    bool has_water() const { return initial_flags[1] & 0x80; } // Labyrinth

    bool electric_cycle() const { return initial_flags[2] & 0x02; } // Scrap Brain floors

    bool underwater_boss() const { return initial_flags[2] & 0x10; }

    bool final_barrier() const { return initial_flags[3] & 0x10; }

    int data(unsigned address) const;

    int word(unsigned address) const { return data(address) | (data(address + 1) << 8); }

    // The layout block at a world pixel position (see the .cpp for wraparound).
    std::uint8_t block_at(int x, int y) const;
};

// Decodes an act from the ROM; save/load write and read the pack files.
Level extract_level(const Rom& rom, unsigned act);
// `folder`: the level's folder in the pack ("" for the first act).
void save_level(const Level& level, PackWriter& out, const std::string& folder);
Level load_level(const std::filesystem::path& directory);
} // namespace sonic1_gg
