// See RomImage.hpp. Only unencrypted images are read; nothing is decrypted.
#include "RomImage.hpp"
#include <set>
#include <utility>

namespace captain_toad {

namespace {
constexpr uint64_t kMedia = 0x200;   // NCSD / NCCH media unit
}

RomImage::RomImage(const std::filesystem::path& path) : file_(path, std::ios::binary) {
    if (!file_) {
        throw ImportError("The file cannot be opened.");
    }
    file_.seekg(0, std::ios::end);
    const auto end = file_.tellg();
    if (end < 0) {
        throw ImportError("Cannot determine image size.");
    }
    fileSize_ = uint64_t(end);
    initialize();
}

RomImage::RomImage(uint64_t size, Reader reader) : reader_(std::move(reader)), fileSize_(size) {
    if (!reader_) {
        throw ImportError("No image reader was supplied.");
    }
    initialize();
}

void RomImage::initialize() {
    // NCSD header: the cartridge container, partition 0 is the game.
    const Bytes ncsd = read(0, 0x200);
    if (std::memcmp(ncsd.data() + 0x100, "NCSD", 4) != 0) {
        throw ImportError("This is not a 3DS cartridge image (CCI).");
    }
    ncch_ = uint64_t(u32(ncsd, 0x120)) * kMedia;
    const Bytes ncch = read(ncch_, 0x200);
    if (std::memcmp(ncch.data() + 0x100, "NCCH", 4) != 0) {
        throw ImportError("The game partition is missing.");
    }
    programId_ = u64(ncch, 0x118);

    // NCCH flags byte 7, bit 2: NoCrypto. Only unencrypted images are accepted; Reprise has no
    // keys and does not decrypt anything.
    if (!(ncch[0x188 + 7] & 4)) {
        throw ImportError(
            "This ROM image is encrypted. Reprise only reads unencrypted ROM images and does not decrypt anything.");
    }

    // ExeFS, and whether its code segment is compressed (extended header: system control info
    // flags (+0x0d) bit 0).
    exefs_ = ncch_ + uint64_t(u32(ncch, 0x1a0)) * kMedia;
    exefsSize_ = uint64_t(u32(ncch, 0x1a4)) * kMedia;
    if (u32(ncch, 0x180) >= 0x400) {
        const Bytes exheader = read(ncch_ + 0x200, 0x400);
        codeCompressed_ = exheader[0x0d] & 1;
    }

    // RomFS: IVFC header; level 3 is placed after the header and the master hash, aligned to its
    // block size.
    const uint64_t romfs = ncch_ + uint64_t(u32(ncch, 0x1b0)) * kMedia;
    const Bytes ivfc = read(romfs, 0x60);
    if (std::memcmp(ivfc.data(), "IVFC", 4) != 0 || u32(ivfc, 4) != 0x10000) {
        throw ImportError("The RomFS is invalid.");
    }
    const auto shift = u32(ivfc, 0x4c);
    if (shift > 24) {
        throw ImportError("Invalid RomFS block size.");
    }
    const uint64_t block = uint64_t(1) << shift;
    level3_ = romfs + ((0x60 + u32(ivfc, 8) + block - 1) / block) * block;
    level3Header_ = read(level3_, 0x28);
    if (u32(level3Header_, 0) != 0x28) {
        throw ImportError("The RomFS file table is invalid.");
    }
    dirMeta_ = read(level3_ + u32(level3Header_, 0x0c), u32(level3Header_, 0x10));
    fileMeta_ = read(level3_ + u32(level3Header_, 0x1c), u32(level3Header_, 0x20));
}

Bytes RomImage::read(uint64_t offset, size_t size) {
    if (offset > fileSize_ || size > fileSize_ - offset) {
        throw ImportError("The image is truncated.");
    }
    if (size > 512u * 1024 * 1024) {
        throw ImportError("Unreasonable image member size.");
    }
    Bytes out(size);
    if (reader_) {
        reader_(offset, out);
        return out;
    }
    file_.seekg(std::streamoff(offset));
    file_.read(reinterpret_cast<char*>(out.data()), std::streamsize(size));
    if (!file_) {
        throw ImportError("The image cannot be read.");
    }
    return out;
}

Bytes RomImage::exefsFile(const std::string& name) {
    // ExeFS header: ten entries of (name[8], offset, size).
    const Bytes header = read(exefs_, 0x200);
    for (int i = 0; i < 10; ++i) {
        const size_t e = size_t(i) * 16;
        const char* entryName = reinterpret_cast<const char*>(header.data() + e);
        const std::string entry(entryName, strnlen(entryName, 8));
        if (entry != name) {
            continue;
        }
        const uint32_t offset = u32(header, e + 8);
        const uint32_t size = u32(header, e + 12);
        if (uint64_t(offset) + size + 0x200 > exefsSize_) {
            throw ImportError("The ExeFS is invalid.");
        }
        Bytes data = read(exefs_ + 0x200 + offset, size);
        return name == ".code" && codeCompressed_ ? decompressCode(data) : data;
    }
    throw ImportError("The ExeFS lacks " + name + ".");
}

Bytes RomImage::romfsFile(const std::string& path) {
    // Names are UTF-16LE; the paths used here are ASCII.
    auto name = [](const Bytes& meta, size_t at, size_t length) {
        checkRange(meta, at, length, "RomFS name");
        if (length % 2) {
            throw ImportError("Invalid RomFS name length.");
        }
        std::string s;
        for (size_t k = 0; k < length; k += 2) {
            s += char(u16(meta, at + k));
        }
        return s;
    };

    // Walk the directory tree one path component at a time; the last component is a file.
    uint32_t dir = 0;   // root
    size_t start = 0;
    while (true) {
        const size_t slash = path.find('/', start);
        const std::string part = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        if (slash == std::string::npos) {
            std::set<uint32_t> visited;
            for (uint32_t f = u32(dirMeta_, dir + 12); f != 0xffffffff; f = u32(fileMeta_, f + 4)) {
                if (!visited.insert(f).second) {
                    throw ImportError("Cyclic RomFS file table.");
                }
                if (name(fileMeta_, f + 0x20, u32(fileMeta_, f + 0x1c)) != part) {
                    continue;
                }
                const uint64_t offset = u64(fileMeta_, f + 8);
                const uint64_t size = u64(fileMeta_, f + 16);
                const uint64_t base = level3_ + u32(level3Header_, 0x24);
                if (base > fileSize_ || offset > fileSize_ - base || size > fileSize_ - base - offset) {
                    throw ImportError("RomFS member exceeds the image.");
                }
                return read(base + offset, size_t(size));
            }
            throw ImportError("The RomFS lacks " + path + ".");
        }
        std::set<uint32_t> visited;
        uint32_t child = u32(dirMeta_, dir + 8);
        while (child != 0xffffffff && name(dirMeta_, child + 0x18, u32(dirMeta_, child + 0x14)) != part) {
            if (!visited.insert(child).second) {
                throw ImportError("Cyclic RomFS directory table.");
            }
            child = u32(dirMeta_, child + 4);
        }
        if (child == 0xffffffff) {
            throw ImportError("The RomFS lacks " + path + ".");
        }
        dir = child;
        start = slash + 1;
    }
}

// The code segment's compression: an LZ variant decompressed in place from the end of the buffer
// backwards. The footer holds the compressed size, the footer size and the size increase. Each
// flag byte describes eight items (high bit first): a literal byte, or a back-reference of
// (length, distance) to bytes already written above.
Bytes decompressCode(const Bytes& in) {
    if (in.size() < 8) {
        throw ImportError("The code segment is invalid.");
    }
    const uint32_t bounds = u32(in, in.size() - 8);
    const uint32_t extra = u32(in, in.size() - 4);
    const size_t compressed = bounds & 0xffffff;
    const size_t header = bounds >> 24;
    if (compressed > in.size() || header < 8 || header > compressed || in.size() > 64u * 1024 * 1024 ||
        extra > 64u * 1024 * 1024 - in.size()) {
        throw ImportError("The code segment is invalid.");
    }
    Bytes out(in);
    out.resize(in.size() + extra);
    size_t src = in.size() - header;
    size_t dst = out.size();
    const size_t stop = in.size() - compressed;
    while (src > stop) {
        uint8_t flags = out[--src];
        for (int bit = 0; bit < 8 && src > stop; ++bit, flags <<= 1) {
            if (flags & 0x80) {
                if (src < 2) {
                    throw ImportError("The code segment is invalid.");
                }
                src -= 2;
                const uint32_t v = out[src] | out[src + 1] << 8;
                const size_t length = (v >> 12) + 3;
                const size_t distance = (v & 0xfff) + 3;
                for (size_t k = 0; k < length; ++k) {
                    if (dst == 0 || dst - 1 + distance >= out.size()) {
                        throw ImportError("The code segment is invalid.");
                    }
                    --dst;
                    out[dst] = out[dst + distance];
                }
            } else {
                if (dst == 0) {
                    throw ImportError("The code segment is invalid.");
                }
                out[--dst] = out[--src];
            }
        }
    }
    return out;
}

}   // namespace captain_toad
