#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

// Tower of Babel's data, decoded from an installed content folder
// (import/disk_import.hpp). Formats: .analysis/tower_of_babel_amiga/formats.md.
namespace babel::data {

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
    std::uint32_t u32(std::uint32_t address) const;
    std::span<const std::uint8_t> bytes(std::uint32_t address, std::uint32_t length) const;
    // A zero-terminated text, at most `limit` bytes.
    std::string text(std::uint32_t address, std::uint32_t limit = 256) const;

private:
    std::uint32_t base_{};
    std::vector<std::uint8_t> bytes_;
};

// A 12-bit Amiga colour ($0RGB).
using Colour = std::uint16_t;

// A picture in the program's 16-colour format (drawn by $11862): per row,
// per 16-pixel column, one word for each of the four bitplanes.
struct Picture {
    int width{}, height{};
    std::vector<std::uint8_t> pixels; // colour index 0-15
};
Picture read_picture(std::span<const std::uint8_t> bytes, int width_words, int height);

// The title screen: the logo and the globe, and the 48-colour palette in
// three bands of 16 the copper switches between (copper mode 3, $8A6A).
struct TitleScreen {
    Picture logo;   // 320 x 35 at (0, 10)
    Picture globe;  // 160 x 133 at (80, 61)
    std::array<Colour, 48> palette{};
    int band2_line{}, band3_line{}; // first screen lines of palette bands 2 and 3
};

// The parts of a tower record (288 bytes, bit-packed; unpacked by $13278).
struct TowerObject {
    int kind{};          // 4-39 (the designer's names: object_kind_name)
    int x{}, z{}, floor{};
};

struct Tower {
    std::string name, author, password;
    // Floors 0-3, each 8 x 8 cells; values from the unpack table $1375A
    // (0 = empty; $C0 floor, $C8 / $E0 / $E8 / $F0 / $30 other cell kinds).
    std::array<std::array<std::array<std::uint8_t, 8>, 8>, 4> cells{};
    std::vector<TowerObject> objects; // the record's 20 slots, empty ones left out
    std::array<Colour, 18> colours{}; // 16 for the view, then 2 more (sky band ends, hypothesis)
    std::array<bool, 10> flags{};     // $E1A-$E2C: 0-2 spider absent (Zapper, Pusher, Grabber), 5 no time limit
    int klondikes{};                  // to collect ($E30)
    int objects_to_destroy{};         // ($E32)
    int time_limit{};                 // seconds ($E18), when flag 5 is clear
    // Settings not identified yet, in record order ($E2E, $E34-$E3C, $E3E,
    // $E40, $E42, then $A4(a6), $A6(a6), $A8(a6)); see formats.md.
    std::array<int, 12> settings{};

    bool spider_available(int spider) const { return !flags[std::size_t(spider)]; }
    bool has_time_limit() const { return !flags[5]; }
};

constexpr std::size_t tower_record_size = 0x120;
Tower decode_tower(std::span<const std::uint8_t> record);

// The designer's name for an object kind ("Klondike"), from the program's list at $6F10.
std::string object_kind_name(const Memory& program, int kind);

struct TowerGroup {
    std::string name;            // "Standard 1", "Tutorial", …
    std::vector<Tower> towers;
};

// One of the effect sounds (46-byte records at $3504-$3AF1, started by $15316):
// a waveform from `main` played with pitch and volume envelopes.
struct EffectSound {
    std::uint32_t record{};   // its address in the program
    std::uint32_t waveform{}; // address of the waveform (a length word, then the samples)
    std::span<const std::uint8_t> samples;
};

// A sampled piece played by the segment player ($1570A): a volume, a period
// and (offset, length) segments in `main` or `music`.
struct SampledPiece {
    std::string name;
    int volume{}, period{};
    std::vector<std::int8_t> samples; // the segments joined
};

struct Assets {
    Memory program; // $800 onwards
    Memory main;    // at $1C5F0
    Memory music;   // at $1C5F0 (title time)
    std::array<std::vector<std::uint8_t>, 3> panels;
    TitleScreen title;
    std::vector<TowerGroup> groups;
    std::vector<EffectSound> effects;
    std::vector<SampledPiece> pieces;
};

Assets load_assets(const std::filesystem::path& content);

} // namespace babel::data
