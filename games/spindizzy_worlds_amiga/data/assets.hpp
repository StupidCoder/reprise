#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <vector>

// Spindizzy Worlds' data, decoded from an installed content folder
// (import/disk_import.hpp). Formats: games/spindizzy_worlds_amiga/game.md.
namespace spindizzy::data {

// A loaded block of the original's memory, read by the addresses its code uses.
class Memory {
public:
    Memory() = default;
    Memory(std::uint32_t base, std::vector<std::uint8_t> bytes) : base_(base), bytes_(std::move(bytes)) {}

    std::uint32_t base() const { return base_; }
    bool contains(std::uint32_t address, std::uint32_t length = 1) const {
        return address >= base_ && address - base_ + length <= bytes_.size();
    }
    std::uint8_t u8(std::uint32_t address) const;
    std::uint16_t u16(std::uint32_t address) const;
    std::uint32_t u32(std::uint32_t address) const { return std::uint32_t(u16(address)) << 16 | u16(address + 2); }
    std::span<const std::uint8_t> bytes(std::uint32_t address, std::uint32_t length) const;
    // A zero-terminated text, at most `limit` bytes.
    std::string text(std::uint32_t address, std::uint32_t limit = 256) const;

private:
    std::uint32_t base_{};
    std::vector<std::uint8_t> bytes_;
};

// A 12-bit Amiga colour ($0RGB).
using Colour = std::uint16_t;

// A NEOchrome picture as the program shows it ($176C2): 16 colours from
// the Atari ST palette doubled into Amiga colours (3 bits → 4 bits per
// gun), 320 × 200 pixels in the ST's interleaved low-resolution layout.
struct Picture {
    std::array<Colour, 16> palette{};
    int width{320}, height{200};
    std::vector<std::uint8_t> pixels; // colour index 0-15
};
Picture read_neo(std::span<const std::uint8_t> file);

// One record of a world file: a level (positive id) or a macro (negative).
struct Record {
    int id{};
    std::span<const std::uint8_t> data; // the record's bytes after its id and length
};

// A level record's 8-byte header ($12052 skips it; the flags word is
// copied to $24B8(a5)). Only the flags word's use is traced so far.
struct LevelHeader {
    // The explorer's start cell: x = bits 8-12, y = bits 3-7, entered at
    // the cell's centre (Tutorial 1 $0B79 → $0B80/$0F80, Gargoyle 1 $1078
    // → $1080/$0F80). The low three bits do not match the start height.
    std::uint16_t first{};
    int start_x() const { return first >> 8 & 31; }
    int start_y() const { return first >> 3 & 31; }
    std::uint16_t second{};
    std::uint16_t flags{};   // $24B8: bit 15 must be set for $12018; bit 12 tested at $11FE4
    std::uint16_t fourth{};
    template <class A> void persist(A& ar) { ar(first, second, flags, fourth); }
};

// A world ("Cluster" file): numbered levels built from command lists, and
// the macros those lists call.
struct World {
    int number{};
    std::string name;       // from the program's list ($119EE, 13 bytes each)
    std::uint16_t setting{}; // the word after "Cluster " (meaning not traced yet)
    std::vector<std::uint8_t> bytes;
    std::map<int, Record> levels; // id 1 … 15
    std::map<int, Record> macros; // id -1 … -255

    LevelHeader header(int level) const;
    // The command list of a level (after its header) or of a macro.
    std::span<const std::uint8_t> commands(int id) const;
};
World read_world(int number, std::string name, std::vector<std::uint8_t> bytes);

constexpr int world_count = 32;

// The playfield's graphics (tables named by main's header longs, + $10000).
// The playfield is a map of 16 × 8-pixel tiles; each block type is drawn
// as up to eight tile pieces, each with a mask that lets the tiles behind
// it show through.
struct TilePiece {
    int mask{};  // 0 = opaque; else a mask number (low byte of the first word)
    int tile{};  // -1 = no piece
};
constexpr int tile_width = 16, tile_height = 8;
using Tile = std::array<std::uint8_t, tile_width * tile_height>; // colour indices 0-15
using TileMask = std::array<std::uint16_t, tile_height>;          // bit set: keep what is behind

struct Graphics {
    std::array<std::array<TilePiece, 8>, 256> pieces{}; // $2EBC0: per block type, 2 columns × 4 rows
    std::vector<TileMask> masks;                        // $30CC0, 16 bytes each
    std::vector<Tile> tiles;                            // $314F0, 64 bytes each (rows of 4 plane words)
    std::array<std::array<Colour, 16>, 4> palettes{};   // $110AE: ST words, doubled ($11096)
};

// A sprite frame in main (drawn by $100A0, $14BE0 and relatives): per
// row the mask (1 = sprite pixel), then plane 0 … 3, each `words` words
// (one word = 16 pixels). Which frame an object shows is its own code's
// choice; the explorer's frames start at $21240 (16 × 16, 160 bytes each).
struct SpriteFrame {
    int width{}, height{};
    std::vector<std::uint8_t> pixels; // colour indices
    std::vector<std::uint8_t> opaque; // 1 where the mask is set
};
SpriteFrame read_sprite_frame(const Memory& main, std::uint32_t address, int words, int rows);

struct Assets {
    Memory main;  // at $10000
    Picture title;
    std::array<std::uint8_t, 256> block_attributes{}; // the table at $30BC0, one byte per block type
    Graphics graphics;
    std::vector<World> worlds;
    // SELECT.SWP ($5140 bytes, loaded to $A(a5) + 4: $115F8 keeps a tag
    // long first, so the code's offsets are the file's + 4): the front
    // end's pictures: file 0 the panel (32 rows, the four plane words of
    // each 16 pixels in turn, $149F0), $1400 the planets ($7D0 each,
    // $169AC), $1928 the shadow, $3340 the "?" planet, $35C0 the
    // disintegrating one ($280 a frame).
    std::vector<std::uint8_t> select_swap;
    std::vector<std::uint8_t> effects; // the effects player and its samples ($3400)
    std::vector<std::uint8_t> music;   // the music player and its tunes ($432A8)
};

Assets load_assets(const std::filesystem::path& content);

} // namespace spindizzy::data
