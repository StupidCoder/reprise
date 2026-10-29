#include "import/rom_image.hpp"
#include "import/sha256.hpp"
#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace picarat::import {

void check(bool condition, const char* message) {
    if (!condition) {
        throw FormatError(message);
    }
}

ByteReader::ByteReader(std::span<const std::uint8_t> data, std::size_t offset)
    : data_(data), pos_(offset) {
    check(offset <= data.size(), "reader offset outside data");
}

std::span<const std::uint8_t> ByteReader::take(std::size_t size) {
    check(size <= data_.size() - pos_, "truncated data");
    auto value = data_.subspan(pos_, size);
    pos_ += size;
    return value;
}

std::uint8_t ByteReader::u8() {
    return take(1)[0];
}

std::uint16_t ByteReader::u16() {
    auto b = take(2);
    return std::uint16_t(b[0] | b[1] << 8);
}

std::uint32_t ByteReader::u32() {
    auto b = take(4);
    return std::uint32_t(b[0]) | std::uint32_t(b[1]) << 8 | std::uint32_t(b[2]) << 16 |
           std::uint32_t(b[3]) << 24;
}

const std::string& safeName(const std::string& name) {
    check(!name.empty() && name.front() != '/' && name.find('\\') == std::string::npos &&
              name.find('\0') == std::string::npos,
          "unsafe relative name");
    std::size_t start = 0;
    for (;;) {
        const std::size_t end = std::min(name.find('/', start), name.size());
        const std::string_view part(name.data() + start, end - start);
        check(
            !part.empty() && part != "." && part != ".." &&
                part.find(':') == std::string_view::npos &&
                std::all_of(part.begin(), part.end(), [](char c) { return std::uint8_t(c) >= 32; }),
            "unsafe path component");
        if (end == name.size()) {
            break;
        }
        start = end + 1;
    }
    return name;
}

static std::string folded(std::string text) {
    for (char& c : text) {
        c = char(std::tolower(std::uint8_t(c)));
    }
    return text;
}

static std::uint16_t crc16(std::span<const std::uint8_t> data) {
    std::uint16_t crc = 0xffff;
    for (std::uint8_t byte : data) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            crc = std::uint16_t((crc >> 1) ^ ((crc & 1) ? 0xa001 : 0));
        }
    }
    return crc;
}

static std::span<const std::uint8_t> region(std::span<const std::uint8_t> data,
                                            std::uint32_t offset, std::uint32_t size) {
    check(offset <= data.size() && size <= data.size() - offset, "ROM region out of bounds");
    return data.subspan(offset, size);
}

// FNT: a directory table (offset of the name list, first file ID, parent)
// followed by name lists of length-prefixed entries; bit 7 marks a
// subdirectory whose ID (0xf000-based) follows the name.
static std::vector<RomFile> filesystem(std::span<const std::uint8_t> fnt,
                                       std::span<const std::uint8_t> fat, std::size_t romSize) {
    check(fat.size() % 8 == 0, "unaligned FAT");
    std::vector<RomFile> entries;
    ByteReader table(fat);
    for (std::uint32_t id = 0; table.remaining(); ++id) {
        const std::uint32_t begin = table.u32(), end = table.u32();
        check(begin <= end && end <= romSize, "FAT entry outside ROM");
        entries.push_back({id, {}, begin, end - begin, std::nullopt});
    }
    ByteReader root(fnt);
    root.u32();
    root.u16();
    const std::uint16_t count = root.u16();
    check(count >= 1 && count <= 4096 && count * 8u <= fnt.size(), "invalid FNT directory count");

    struct Pending {
        std::uint32_t index;
        std::string prefix;
        std::optional<std::uint32_t> parent;
    };

    std::vector<Pending> pending{{0, "", std::nullopt}};
    std::unordered_set<std::uint32_t> visited;
    std::unordered_set<std::string> paths;
    while (!pending.empty()) {
        const Pending directory = pending.back();
        pending.pop_back();
        check(directory.index < count && visited.insert(directory.index).second,
              "cyclic or repeated FNT directory");
        ByteReader head(fnt, directory.index * 8);
        const std::uint32_t start = head.u32();
        std::uint32_t id = head.u16();
        const std::uint16_t storedParent = head.u16();
        check(start >= count * 8u, "FNT name list overlaps directory table");
        check(!directory.parent || storedParent == *directory.parent, "FNT parent mismatch");
        ByteReader names(fnt, start);
        for (;;) {
            const std::uint8_t flag = names.u8();
            if (!flag) {
                break;
            }
            const auto raw = names.take(flag & 127);
            // Names are ASCII, except one Shift-JIS slip in the first
            // adventure (bg/<82 91>140_bg.arc, a full-width 'q'). Bytes
            // outside ASCII are spelled %XX so that every path stays valid
            // on the host's filesystem.
            std::string component;
            for (const std::uint8_t c : raw) {
                check(c != '/' && c != '%', "FNT component is not a plain name");
                if (c < 128) {
                    component += char(c);
                } else {
                    static constexpr char kHex[] = "0123456789ABCDEF";
                    component += {'%', kHex[c >> 4], kHex[c & 15]};
                }
            }
            const std::string path = safeName(directory.prefix + component);
            check(paths.insert(folded(path)).second, "duplicate/case-colliding FNT path");
            if (flag & 128) {
                const std::uint16_t child = names.u16();
                check(child >= 0xf000, "invalid FNT subdirectory ID");
                pending.push_back({child - 0xf000u, path + "/", directory.index + 0xf000u});
            } else {
                check(id < entries.size() && entries[id].path.empty(),
                      "invalid/repeated FNT file ID");
                entries[id].path = path;
                ++id;
            }
        }
    }
    check(visited.size() == count, "unreachable FNT directories");
    return entries;
}

NdsRom::NdsRom(Bytes image) : image_(std::move(image)) {
    const std::span<const std::uint8_t> data(image_);
    sha256_ = import::sha256(data);
    for (const KnownRom& known : kKnownRoms) {
        if (sha256_ == known.sha256) {
            identity_ = &known;
        }
    }
    check(identity_ != nullptr, "This is not one of the supported ROM images.");
    check(data.size() >= 0x200, "ROM header truncated");
    check(crc16(data.subspan(0, 0x15e)) == read16(data, 0x15e), "header CRC mismatch");
    check(crc16(data.subspan(0xc0, 0x9c)) == read16(data, 0x15c), "logo CRC mismatch");
    auto table = [&](std::size_t field) {
        return region(data, read32(data, field), read32(data, field + 4));
    };
    check(gameCode() == identity_->gameCode, "ROM game code differs from its identity");
    files_ = filesystem(table(0x40), table(0x48), data.size());
    for (const bool arm9 : {true, false}) {
        const auto overlays = table(arm9 ? 0x50 : 0x58);
        check(overlays.size() % 32 == 0, "unaligned overlay table");
        std::unordered_set<std::uint32_t> ids;
        for (ByteReader r(overlays); r.remaining();) {
            RomOverlay o;
            o.arm9 = arm9;
            o.id = r.u32();
            o.loadAddress = r.u32();
            o.ramSize = r.u32();
            o.bssSize = r.u32();
            o.initStart = r.u32();
            o.initEnd = r.u32();
            const std::uint32_t file = r.u32();
            o.flags = r.u32();
            check(file < files_.size() && ids.insert(o.id).second && !files_[file].overlay,
                  "invalid overlay reference");
            files_[file].overlay = o;
        }
    }
    for (std::size_t i = 0; i < files_.size(); ++i) {
        if (!files_[i].path.empty()) {
            byPath_.emplace(files_[i].path, i);
        }
    }
}

std::string NdsRom::gameCode() const {
    return std::string(image_.begin() + 12, image_.begin() + 16);
}

const KnownRom& knownRom(Game game) {
    for (const KnownRom& known : kKnownRoms) {
        if (known.game == game) {
            return known;
        }
    }
    throw std::logic_error("unknown game");
}

Bytes NdsRom::arm9Program() const {
    // Header 0x20/0x28/0x2c: the program's ROM offset, load address and size.
    const std::uint32_t offset = read32(image_, 0x20), load = read32(image_, 0x28);
    const std::uint32_t size = read32(image_, 0x2c);
    const auto program = region(image_, offset, size);
    // A 12-byte footer follows the program: the SDK's 0xdec00621 marker and
    // the offset of its module parameters, whose word +0x14 is the address
    // where the compressed part ends (0 when the program is stored plainly).
    const auto footer = region(image_, std::size_t(offset) + size, 12);
    check(read32(footer, 0) == 0xdec00621, "ARM9 program footer missing");
    const auto parameters = region(program, read32(footer, 4), 0x20);
    const std::uint32_t compressedEnd = read32(parameters, 0x14);
    if (compressedEnd == 0) {
        return Bytes(program.begin(), program.end());
    }
    check(compressedEnd > load && compressedEnd - load <= size, "ARM9 compressed end");
    Bytes code = decompressCode(program.subspan(0, compressedEnd - load));
    code.insert(code.end(), program.begin() + (compressedEnd - load), program.end());
    return code;
}

Bytes NdsRom::arm7Program() const {
    // Header 0x30/0x3c: the program's ROM offset and size.
    const auto program = region(image_, read32(image_, 0x30), read32(image_, 0x3c));
    return Bytes(program.begin(), program.end());
}

std::span<const std::uint8_t> NdsRom::contents(const RomFile& file) const {
    return region(image_, file.offset, file.size);
}

const RomFile* NdsRom::find(std::string_view path) const {
    const auto found = byPath_.find(std::string(path));
    return found == byPath_.end() ? nullptr : &files_[found->second];
}

const RomFile* NdsRom::overlay(bool arm9, std::uint32_t id) const {
    for (const RomFile& file : files_) {
        if (file.overlay && file.overlay->arm9 == arm9 && file.overlay->id == id) {
            return &file;
        }
    }
    return nullptr;
}

Bytes decompressCode(std::span<const std::uint8_t> data) {
    check(data.size() >= 8, "compressed code too short");
    const std::uint32_t increase = read32(data, data.size() - 4);
    Bytes out(data.begin(), data.end());
    if (increase == 0) {
        return out;
    }
    const std::uint32_t footer = read32(data, data.size() - 8);
    const std::size_t headerSize = footer >> 24, packedSize = footer & 0xffffff;
    check(headerSize >= 8 && headerSize <= packedSize && packedSize <= data.size(),
          "invalid code compression footer");
    out.resize(data.size() + increase);
    std::size_t source = data.size() - headerSize, target = out.size();
    const std::size_t stop = data.size() - packedSize;
    while (source > stop) {
        std::uint8_t flags = data[--source];
        for (int bit = 0; bit < 8 && source > stop; ++bit, flags = std::uint8_t(flags << 1)) {
            if (flags & 0x80) {
                check(source - stop >= 2, "truncated code back-reference");
                const std::uint8_t high = data[--source], low = data[--source];
                const std::size_t distance = ((high & 0x0f) << 8 | low) + 3,
                                  length = (high >> 4) + 3;
                check(length <= target - source, "code back-reference overruns input");
                for (std::size_t i = 0; i < length; ++i) {
                    --target;
                    check(target + distance < out.size(), "code back-reference outside output");
                    out[target] = out[target + distance];
                }
            } else {
                // Equal positions occur when the span starts at the file's
                // first byte: the last literals are then already in place.
                check(target >= source, "code literal overruns input");
                out[--target] = data[--source];
            }
        }
    }
    check(target == source, "code compression did not meet its stored prefix");
    return out;
}

} // namespace picarat::import
