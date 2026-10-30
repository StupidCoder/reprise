#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

// The imported files, decoded into what the game uses.
namespace stuntcar::data {

// An Amiga colour register value (4 bits per gun).
struct Colour {
    std::uint8_t r{}, g{}, b{};
};

// A low-resolution picture in the Amiga's planar form, decoded to colour
// indices (one byte per pixel).
struct Picture {
    int width{}, height{};
    std::vector<std::uint8_t> pixels;
    std::vector<Colour> palette;
};

// The game program as it runs: the sound samples at $F4B8-$1AA49 converted
// from unsigned to signed (bit 7 flipped), as the routine at $EE64 does at
// start-up. Addresses are the program's
// own load addresses; they locate data for decoding, never game state.
class Program {
  public:
    static constexpr std::uint32_t base = 0xE700;
    explicit Program(std::vector<std::uint8_t> on_disk);
    std::span<const std::uint8_t> at(std::uint32_t address, std::size_t length) const;
    std::uint8_t u8(std::uint32_t address) const;
    std::uint16_t u16(std::uint32_t address) const;
    std::uint32_t u32(std::uint32_t address) const;
    std::size_t size() const { return bytes_.size(); }

  private:
    std::vector<std::uint8_t> bytes_;
};

struct Assets {
    std::vector<std::uint8_t> loader;
    Program program;
    Picture loading_picture; // shown by the loader while the program loads
};

// Reads the installed files; throws std::runtime_error when they are missing
// or do not hold what the decoders expect.
Assets load_assets(const std::filesystem::path& content);

// The loader's picture: 320 x 200, four planes stored word-interleaved
// from loader offset $D9C (one word of each plane in turn), palette of 16
// colour words at $D7C (the loader copies them at $B6).
Picture loading_picture(std::span<const std::uint8_t> loader);

} // namespace stuntcar::data
