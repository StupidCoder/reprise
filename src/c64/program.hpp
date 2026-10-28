#pragma once
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

// A C64 program file (PRG): the 16-bit little-endian address the bytes load
// to, followed by the bytes. Imported C64 games are installed as PRG files,
// the machine's standard file format.
namespace c64 {

struct Program {
    std::uint16_t load_address{};
    std::vector<std::uint8_t> bytes;

    std::vector<std::uint8_t> to_prg() const {
        std::vector<std::uint8_t> file{std::uint8_t(load_address), std::uint8_t(load_address >> 8)};
        file.insert(file.end(), bytes.begin(), bytes.end());
        return file;
    }

    static Program from_prg(std::span<const std::uint8_t> file) {
        if (file.size() < 2) {
            throw std::runtime_error("A program file is too short.");
        }
        return {std::uint16_t(file[0] | file[1] << 8), {file.begin() + 2, file.end()}};
    }

    // True when the address lies inside the loaded bytes.
    bool contains(std::uint16_t address) const {
        return address >= load_address && address - load_address < bytes.size();
    }

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) { ar(load_address, bytes); }
};

} // namespace c64
