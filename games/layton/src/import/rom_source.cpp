#include "import/rom_source.hpp"
#include <stdexcept>

namespace picarat::import {

FileRomSource::FileRomSource(const std::filesystem::path& path) : file_(path, std::ios::binary) {
    if (!file_) {
        throw std::runtime_error("Could not open the file.");
    }
    file_.seekg(0, std::ios::end);
    size_ = std::uint64_t(file_.tellg());
}

void FileRomSource::read(std::uint64_t offset, std::span<std::uint8_t> out) {
    if (offset > size_ || out.size() > size_ - offset) {
        throw std::runtime_error("The ROM image is truncated.");
    }
    file_.seekg(std::streamoff(offset));
    file_.read(reinterpret_cast<char*>(out.data()), std::streamsize(out.size()));
    if (!file_) {
        throw std::runtime_error("Could not read the file.");
    }
}

} // namespace picarat::import
