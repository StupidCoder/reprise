#include "stunt_car_racer_amiga/data/assets.hpp"
#include "platform/files.hpp"
#include <stdexcept>

namespace stuntcar::data {
namespace {

// 8-bit sound samples, stored unsigned; the start-up routine at $EE64 turns
// them into the signed form Paula plays by flipping bit 7 of every byte.
constexpr std::uint32_t unsigned_samples_begin = 0xF4B8;
constexpr std::uint32_t unsigned_samples_end = 0x1AA4A;

constexpr std::size_t loader_size = 0x9800;
constexpr std::size_t program_size = 805 * 512;

constexpr std::size_t picture_palette = 0xD7C;
constexpr std::size_t picture_planes = 0xD9C;

Colour colour(std::uint16_t word) {
    const auto gun = [](int v) { return std::uint8_t(v * 17); };
    return {gun(word >> 8 & 15), gun(word >> 4 & 15), gun(word & 15)};
}

std::uint16_t be16(std::span<const std::uint8_t> bytes, std::size_t at) {
    return std::uint16_t(bytes[at] << 8 | bytes[at + 1]);
}

} // namespace

Program::Program(std::vector<std::uint8_t> on_disk) : bytes_(std::move(on_disk)) {
    if (bytes_.size() != program_size) throw std::runtime_error("The installed game program has the wrong size.");
    for (auto a = unsigned_samples_begin; a < unsigned_samples_end; ++a) bytes_[a - base] ^= 0x80;
}

std::span<const std::uint8_t> Program::at(std::uint32_t address, std::size_t length) const {
    if (address < base || address - base + length > bytes_.size()) {
        throw std::runtime_error("Read outside the game program.");
    }
    return std::span(bytes_).subspan(address - base, length);
}

std::uint8_t Program::u8(std::uint32_t address) const { return at(address, 1)[0]; }
std::uint16_t Program::u16(std::uint32_t address) const { return be16(at(address, 2), 0); }
std::uint32_t Program::u32(std::uint32_t address) const {
    return std::uint32_t(u16(address)) << 16 | u16(address + 2);
}

Picture loading_picture(std::span<const std::uint8_t> loader) {
    if (loader.size() < picture_planes + 4 * 8000) throw std::runtime_error("The loader is too short.");
    Picture picture{320, 200, std::vector<std::uint8_t>(320 * 200), {}};
    for (int c = 0; c < 16; ++c) picture.palette.push_back(colour(be16(loader, picture_palette + std::size_t(c) * 2)));
    // The loader's copy loop ($A0): $FA0 groups of four words, one per plane.
    for (std::size_t group = 0; group < 0xFA0; ++group) {
        std::uint16_t plane[4];
        for (std::size_t p = 0; p < 4; ++p) plane[p] = be16(loader, picture_planes + group * 8 + p * 2);
        for (int bit = 0; bit < 16; ++bit) {
            std::uint8_t index = 0;
            for (int p = 0; p < 4; ++p) index |= std::uint8_t((plane[p] >> (15 - bit) & 1) << p);
            picture.pixels[group * 16 + std::size_t(bit)] = index;
        }
    }
    return picture;
}

Assets load_assets(const std::filesystem::path& content) {
    auto loader = platform::read_file(content / "loader");
    if (loader.size() != loader_size) throw std::runtime_error("The installed loader has the wrong size.");
    Program program(platform::read_file(content / "program"));
    auto picture = loading_picture(loader);
    return {std::move(loader), std::move(program), std::move(picture)};
}

} // namespace stuntcar::data
