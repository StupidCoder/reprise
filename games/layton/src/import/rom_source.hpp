#pragma once
// Random access to a ROM image without holding it in memory: the 3DS
// cartridge images are 1 GiB. The app adapts its platform file reader (which
// also opens Android's content:// documents); the command-line tools read
// files directly.
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>

namespace picarat::import {

class RomSource {
public:
    virtual ~RomSource() = default;
    virtual std::uint64_t size() const = 0;
    // Fills `out` from `offset`; throws std::runtime_error if it cannot.
    virtual void read(std::uint64_t offset, std::span<std::uint8_t> out) = 0;
};

class FileRomSource final : public RomSource {
public:
    explicit FileRomSource(const std::filesystem::path& path);

    std::uint64_t size() const override { return size_; }

    void read(std::uint64_t offset, std::span<std::uint8_t> out) override;

private:
    std::ifstream file_;
    std::uint64_t size_ = 0;
};

} // namespace picarat::import
