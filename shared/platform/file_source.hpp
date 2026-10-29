#pragma once
#include <cstdint>
#include <filesystem>
#include <span>

// Random access to a file without reading it whole, for importers of large images (cartridge
// images of a gigabyte and more). Opened through SDL's I/O streams, so the paths the platform's
// file picker returns work too (Android's content:// documents). Errors throw
// std::runtime_error with a message for the user.
namespace platform {

class FileSource {
public:
    explicit FileSource(const std::filesystem::path& path);
    ~FileSource();
    FileSource(const FileSource&) = delete;
    FileSource& operator=(const FileSource&) = delete;

    std::uint64_t size() const { return size_; }
    // Fills `out` from `offset`.
    void read(std::uint64_t offset, std::span<std::uint8_t> out);

private:
    void* io_{};
    std::uint64_t size_{};
};

} // namespace platform
