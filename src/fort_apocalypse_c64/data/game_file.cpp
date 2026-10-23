#include "fort_apocalypse_c64/data/game_file.hpp"
#include <stdexcept>

namespace fort_apocalypse::data {

GameFile::GameFile(c64::Program program) : program_(std::move(program)) {
    if (program_.load_address != load_address || program_.bytes.size() != 0x4900) {
        throw std::runtime_error("The installed game file is not the expected one; please "
                                 "import the tape again.");
    }
}

std::uint8_t GameFile::byte(std::uint16_t address) const {
    return bytes(address, 1)[0];
}

std::uint16_t GameFile::word(std::uint16_t address) const {
    const auto pair = bytes(address, 2);
    return std::uint16_t(pair[0] | pair[1] << 8);
}

std::span<const std::uint8_t> GameFile::bytes(std::uint16_t address, std::size_t count) const {
    if (!program_.contains(address) || address - load_address + count > program_.bytes.size()) {
        throw std::out_of_range("Fort Apocalypse data read outside the game file");
    }
    return std::span(program_.bytes).subspan(address - load_address, count);
}

} // namespace fort_apocalypse::data
