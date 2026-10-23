#pragma once
#include "c64/program.hpp"
#include <cstdint>
#include <span>

// The installed game program (game.prg, $7000-$B8FF). The game keeps its
// data (maps, charsets, sprite shapes, tables) at fixed places inside this
// file; the decoders read them through this class by the address the data
// loads to, which is how the original code refers to it and how the
// evidence in the notes names it.
namespace fort_apocalypse::data {

class GameFile {
public:
    static constexpr std::uint16_t load_address = 0x7000;

    // Throws std::runtime_error when the program is not the game file.
    explicit GameFile(c64::Program program);

    std::uint8_t byte(std::uint16_t address) const;
    // A little-endian 16-bit value.
    std::uint16_t word(std::uint16_t address) const;
    std::span<const std::uint8_t> bytes(std::uint16_t address, std::size_t count) const;

private:
    c64::Program program_;
};

} // namespace fort_apocalypse::data
