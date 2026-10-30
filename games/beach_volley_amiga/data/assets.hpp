#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

// The imported files, decoded into what the game uses (game.md "Formats").
namespace beach_volley::data {

// An Amiga colour register value ($0RGB, 4 bits per gun).
using Rgb4 = std::uint16_t;
using Palette = std::array<Rgb4, 16>;

// A code file as it runs from its load address. Addresses locate data for
// decoding; they are never game state.
class CodeFile {
public:
    CodeFile(std::vector<std::uint8_t> bytes, std::uint32_t base);
    std::span<const std::uint8_t> at(std::uint32_t address, std::size_t length) const;
    std::uint16_t u16(std::uint32_t address) const;
    std::uint32_t u32(std::uint32_t address) const;

private:
    std::vector<std::uint8_t> bytes_;
    std::uint32_t base_;
};

// Where the code files run (game.md "Disk"): `volley` loads intro.code;
// intro.code loads volley.code; volley.code loads score.code over the
// intro, animation.code and final.code.
constexpr std::uint32_t intro_base = 0x23000;
constexpr std::uint32_t volley_base = 0x25000;
constexpr std::uint32_t score_base = 0x23000;
constexpr std::uint32_t animation_base = 0x3BC50;
constexpr std::uint32_t final_base = 0x49800;

// A picture in planar form decoded to colour indices, one byte per pixel.
struct Picture {
    int width{}, height{};
    std::vector<std::uint8_t> pixels;
};

// One of the eight courts: the scrolling background the match is played
// on. terrainN is 576 x 232 in four planes, one after the other ($4140
// bytes each, rows of $48 bytes); volley.code blits a 304-pixel window of
// it to the screen ($28FCC). Its colours are palette N-1 of the table at
// $29190 ($2814A copies the venue's 16 words into the copper list).
struct Court {
    int number{}; // 1-8, the venue number the program keeps at $25B70
    Picture picture;
    Palette palette{};
};

struct Assets {
    CodeFile volley;
    std::vector<Court> courts;
};

// The intro's files as intro.code loads them ($24616): introp1 and introp2
// unpack one after the other into one block of pictures and voices at
// $4AE90, trophe to $40000, the song music13 to $789AC and the sample bank
// `instruments` (stored as is) to $12A50.
struct IntroFiles {
    CodeFile code;                    // intro.code
    std::vector<std::uint8_t> block;  // introp1 then introp2
    std::vector<std::uint8_t> trophy; // trophe
    std::vector<std::uint8_t> song;   // music13
    std::vector<std::uint8_t> instruments;
};

constexpr std::uint32_t intro_block_address = 0x4AE90;
constexpr std::uint32_t intro_trophy_address = 0x40000;
constexpr std::uint32_t instruments_address = 0x12A50; // every code file loads the bank here

IntroFiles load_intro_files(const std::filesystem::path& content);

constexpr int court_count = 8;
constexpr int court_width = 576;
constexpr int court_height = 232;

// Reads the installed files; throws std::runtime_error when they are
// missing or do not hold what the decoders expect.
Assets load_assets(const std::filesystem::path& content);

// Decodes `planes` bitplanes stored one after the other.
Picture planar_picture(std::span<const std::uint8_t> bytes, int width, int height, int planes);

} // namespace beach_volley::data
