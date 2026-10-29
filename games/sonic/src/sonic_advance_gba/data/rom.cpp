// The cartridge image.

#include "sonic_advance_gba/data/rom.hpp"
#include "platform/sha256.hpp"
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace sonic_advance_gba {

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

Rom::Rom(Bytes bytes) : bytes_(std::move(bytes)) {
    // Every address in this module refers to this exact revision.
    if (bytes_.size() != rom_size || platform::sha256(bytes_) != rom_sha256) {
        throw std::runtime_error("Expected the 8 MiB Sonic Advance Game Boy Advance ROM "
                                 "(USA, En/Ja), CRC32 63F70FD8");
    }
}

Rom Rom::load(const std::filesystem::path& path) {
    return Rom(read_file(path));
}

} // namespace sonic_advance_gba
