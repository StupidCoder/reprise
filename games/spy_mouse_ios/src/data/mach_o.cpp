#include "data/mach_o.hpp"

namespace doc::data {
namespace {

std::uint32_t be32(const Bytes& b, std::size_t o) {
    return (std::uint32_t(b[o]) << 24) | (std::uint32_t(b[o + 1]) << 16) | (std::uint32_t(b[o + 2]) << 8) | b[o + 3];
}
std::uint32_t le32(const Bytes& b, std::size_t o) {
    return std::uint32_t(b[o]) | (std::uint32_t(b[o + 1]) << 8) | (std::uint32_t(b[o + 2]) << 16) |
           (std::uint32_t(b[o + 3]) << 24);
}

constexpr std::uint32_t kFatMagic = 0xCAFEBABE;
constexpr std::uint32_t kMachMagic = 0xFEEDFACE;  // 32-bit, little-endian on disk
constexpr std::uint32_t kCpuArm = 12;
constexpr std::uint32_t kCpuSubtypeArmV7 = 9;
constexpr std::uint32_t kLcSegment = 0x1;

}  // namespace

std::optional<ExecutableImage> ExecutableImage::parse(Bytes file, std::string& error) {
    ExecutableImage image;
    image.file_ = std::move(file);
    const Bytes& f = image.file_;
    if (f.size() < 8) {
        error = "executable too small";
        return std::nullopt;
    }
    if (be32(f, 0) == kFatMagic) {
        std::uint32_t count = be32(f, 4);
        bool found = false;
        for (std::uint32_t i = 0; i < count && 8 + i * 20 + 20 <= f.size(); ++i) {
            std::size_t e = 8 + i * 20;
            if (be32(f, e) == kCpuArm && be32(f, e + 4) == kCpuSubtypeArmV7) {
                image.sliceOffset_ = be32(f, e + 8);
                found = true;
            }
        }
        if (!found) {
            error = "executable has no armv7 slice";
            return std::nullopt;
        }
    }
    std::size_t s = image.sliceOffset_;
    if (s + 28 > f.size() || le32(f, s) != kMachMagic) {
        error = "armv7 slice is not a 32-bit Mach-O image";
        return std::nullopt;
    }
    std::uint32_t ncmds = le32(f, s + 16);
    std::size_t lc = s + 28;
    for (std::uint32_t i = 0; i < ncmds && lc + 8 <= f.size(); ++i) {
        std::uint32_t cmd = le32(f, lc), size = le32(f, lc + 4);
        if (cmd == kLcSegment && lc + 56 <= f.size())
            image.segments_.push_back({le32(f, lc + 24), le32(f, lc + 28), le32(f, lc + 32), le32(f, lc + 36)});
        if (size == 0) break;
        lc += size;
    }
    return image;
}

std::optional<Bytes> ExecutableImage::read(std::uint32_t vmaddr, std::size_t length) const {
    for (const Segment& seg : segments_) {
        if (vmaddr < seg.vmaddr || vmaddr + length > seg.vmaddr + seg.filesize) continue;
        std::size_t off = sliceOffset_ + seg.fileoff + (vmaddr - seg.vmaddr);
        if (off + length > file_.size()) return std::nullopt;
        return Bytes(file_.begin() + off, file_.begin() + off + length);
    }
    return std::nullopt;
}

std::optional<Bytes> ExecutableImage::readCString(std::uint32_t vmaddr, std::size_t maxLength) const {
    auto bytes = read(vmaddr, maxLength);
    if (!bytes) return std::nullopt;
    for (std::size_t i = 0; i < bytes->size(); ++i)
        if ((*bytes)[i] == 0) {
            bytes->resize(i + 1);
            return bytes;
        }
    return std::nullopt;
}

}  // namespace doc::data
