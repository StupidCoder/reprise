// See Binary.hpp.
#include "Binary.hpp"

#include "platform/Sha256.hpp"

#include <fstream>
#include <iterator>
#include <span>

namespace captain_toad {

std::string sha256Hex(const uint8_t* data, size_t size) {
    return captain_toad::platform::sha256(std::span<const uint8_t>(data, size));
}

static void writeRaw(const std::filesystem::path& path, const char* data, size_t size) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        throw ImportError("Cannot create " + path.string());
    }
    f.write(data, std::streamsize(size));
    if (!f) {
        throw ImportError("Cannot write " + path.string());
    }
}

void writeFile(const std::filesystem::path& path, const Bytes& data) {
    writeRaw(path, reinterpret_cast<const char*>(data.data()), data.size());
}

Bytes readFile(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        throw ImportError("Cannot read " + path.string() + ". Import the ROM again.");
    }
    return Bytes(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

void writeFile(const std::filesystem::path& path, const std::string& text) {
    writeRaw(path, text.data(), text.size());
}

}   // namespace captain_toad
