#include "amiga/hunk.hpp"
#include "amiga/adf.hpp"
#include <stdexcept>

namespace amiga {
namespace {

constexpr std::uint32_t hunk_code = 0x3E9;
constexpr std::uint32_t hunk_data = 0x3EA;
constexpr std::uint32_t hunk_bss = 0x3EB;
constexpr std::uint32_t hunk_reloc32 = 0x3EC;
constexpr std::uint32_t hunk_symbol = 0x3F0;
constexpr std::uint32_t hunk_debug = 0x3F1;
constexpr std::uint32_t hunk_end = 0x3F2;
constexpr std::uint32_t hunk_header = 0x3F3;

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}
    bool at_end() const { return at_ >= bytes_.size(); }
    std::uint32_t next() {
        if (at_ + 4 > bytes_.size()) {
            throw std::runtime_error("A program file on the disk is cut short.");
        }
        const auto value = read_be32(bytes_, at_);
        at_ += 4;
        return value;
    }
    std::span<const std::uint8_t> take(std::size_t count) {
        if (at_ + count > bytes_.size()) {
            throw std::runtime_error("A program file on the disk is cut short.");
        }
        const auto part = bytes_.subspan(at_, count);
        at_ += count;
        return part;
    }

private:
    std::span<const std::uint8_t> bytes_;
    std::size_t at_{};
};

[[noreturn]] void malformed(const char* what) {
    throw std::runtime_error(std::string("A program file on the disk is malformed: ") + what + ".");
}

} // namespace

bool is_load_file(std::span<const std::uint8_t> bytes) {
    return bytes.size() >= 4 && read_be32(bytes, 0) == hunk_header;
}

LoadFile read_load_file(std::span<const std::uint8_t> bytes) {
    Reader in(bytes);
    if (in.next() != hunk_header) {
        malformed("no HUNK_HEADER");
    }
    while (const auto length = in.next()) { // resident library names
        in.take(std::size_t(length) * 4);
    }
    const auto table_size = in.next();
    const auto first = in.next();
    const auto last = in.next();
    if (last < first || last - first + 1 > table_size || table_size > 65536) {
        malformed("a bad hunk table");
    }
    LoadFile file;
    file.hunks.resize(last - first + 1);
    for (auto& hunk : file.hunks) {
        const auto size = in.next();
        hunk.memory_flags = size & 0xC0000000;
        if (hunk.memory_flags == 0xC0000000) {
            hunk.memory_flags = in.next(); // explicit memory attributes
        }
        hunk.size = (size & 0x3FFFFFFF) * 4;
    }
    std::size_t current = 0;
    bool started = false;
    while (!in.at_end() && current < file.hunks.size()) {
        const auto type = in.next() & 0x3FFFFFFF;
        auto& hunk = file.hunks[current];
        switch (type) {
        case hunk_code:
        case hunk_data:
        case hunk_bss: {
            if (started) {
                malformed("a hunk without HUNK_END");
            }
            started = true;
            hunk.kind = type == hunk_code ? HunkKind::code : type == hunk_data ? HunkKind::data : HunkKind::bss;
            const std::size_t length = std::size_t(in.next() & 0x3FFFFFFF) * 4;
            if (length > hunk.size && type != hunk_bss) {
                malformed("a hunk larger than its header says");
            }
            if (type != hunk_bss) {
                const auto stored = in.take(length);
                hunk.bytes.assign(stored.begin(), stored.end());
                hunk.bytes.resize(hunk.size, 0);
            }
            break;
        }
        case hunk_reloc32:
            while (const auto count = in.next()) {
                Relocation relocation{.target_hunk = int(in.next() - first)};
                if (relocation.target_hunk < 0 || std::size_t(relocation.target_hunk) >= file.hunks.size()) {
                    malformed("a relocation to a missing hunk");
                }
                for (std::uint32_t i = 0; i < count; ++i) {
                    relocation.offsets.push_back(in.next());
                }
                hunk.relocations.push_back(std::move(relocation));
            }
            break;
        case hunk_symbol:
            while (const auto length = in.next()) {
                const auto name = in.take(std::size_t(length) * 4);
                std::string text(name.begin(), name.end());
                text.erase(text.find_last_not_of('\0') + 1);
                hunk.symbols.push_back({std::move(text), in.next()});
            }
            break;
        case hunk_debug:
            in.take(std::size_t(in.next()) * 4);
            break;
        case hunk_end:
            // Some linkers (c/bootscr's on Marble Madness) write a second
            // HUNK_END after a BSS hunk; with no hunk open it ends nothing.
            if (started) {
                started = false;
                ++current;
            }
            break;
        default:
            malformed("an unknown block type");
        }
    }
    if (current != file.hunks.size()) {
        malformed("fewer hunks than the header lists");
    }
    return file;
}

std::vector<std::uint8_t> relocate(const LoadFile& file, std::uint32_t base, std::vector<std::uint32_t>& addresses,
                                   std::uint32_t gap) {
    addresses.clear();
    std::uint32_t at = base;
    for (const auto& hunk : file.hunks) {
        at = (at + 3) & ~3u;
        addresses.push_back(at);
        at += hunk.size + gap;
    }
    std::vector<std::uint8_t> image(at - base, 0);
    for (std::size_t i = 0; i < file.hunks.size(); ++i) {
        const auto& hunk = file.hunks[i];
        auto* out = image.data() + (addresses[i] - base);
        std::copy(hunk.bytes.begin(), hunk.bytes.end(), out);
        for (const auto& relocation : hunk.relocations) {
            for (const auto offset : relocation.offsets) {
                if (offset + 4 > hunk.size) {
                    malformed("a relocation outside its hunk");
                }
                auto* word = out + offset;
                const std::uint32_t value = (std::uint32_t(word[0]) << 24 | std::uint32_t(word[1]) << 16 |
                                             std::uint32_t(word[2]) << 8 | word[3]) +
                                            addresses[std::size_t(relocation.target_hunk)];
                word[0] = std::uint8_t(value >> 24);
                word[1] = std::uint8_t(value >> 16);
                word[2] = std::uint8_t(value >> 8);
                word[3] = std::uint8_t(value);
            }
        }
    }
    return image;
}

} // namespace amiga
