#pragma once
#include "platform/file_source.hpp"
#include <functional>
#include <string>
#include <vector>

namespace platform {
// Bounded central-directory reader; payloads are streamed, including ZIP64.
class ZipArchive {
public:
    struct Entry {
        std::string name;
        std::uint64_t compressed{}, size{}, offset{};
        std::uint32_t crc{};
        std::uint16_t method{}, flags{};
    };
    explicit ZipArchive(FileSource& source);
    const std::vector<Entry>& entries() const { return entries_; }
    void extract(const Entry&, const std::filesystem::path&, std::uint64_t limit,
                 const std::function<void(double)>& progress = {});
    static bool signature(FileSource&);
private:
    FileSource& source_;
    std::vector<Entry> entries_;
};
}
