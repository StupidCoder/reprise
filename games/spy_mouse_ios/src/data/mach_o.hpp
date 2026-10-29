// Minimal reader for the game's executable: a fat Mach-O with armv6 and armv7 slices. Used
// only to fetch data the original keeps in its program image (file keys), by virtual address
// in the armv7 slice.
#pragma once

#include "data/game_files.hpp"

#include <optional>
#include <string>

namespace doc::data {

class ExecutableImage {
public:
    static std::optional<ExecutableImage> parse(Bytes file, std::string& error);

    // Bytes at an armv7 virtual address; nullopt if the range is not file-backed.
    std::optional<Bytes> read(std::uint32_t vmaddr, std::size_t length) const;
    // NUL-terminated string at an armv7 virtual address, including the terminator.
    std::optional<Bytes> readCString(std::uint32_t vmaddr, std::size_t maxLength) const;

private:
    struct Segment {
        std::uint32_t vmaddr, vmsize, fileoff, filesize;
    };
    Bytes file_;
    std::size_t sliceOffset_ = 0;
    std::vector<Segment> segments_;
};

}  // namespace doc::data
