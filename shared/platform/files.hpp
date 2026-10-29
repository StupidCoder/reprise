#pragma once
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

// Whole-file reads and writes; errors throw std::runtime_error.
namespace platform {

inline std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Cannot open " + path.filename().string() + ".");
    }
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

inline void write_file(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    if (!out) {
        throw std::runtime_error("Cannot write " + path.filename().string() + ".");
    }
}

inline void write_text(const std::filesystem::path& path, const std::string& text) {
    write_file(path, {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
}

} // namespace platform
