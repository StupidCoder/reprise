// The 3DS cartridge reader. See ctr_image.hpp.
#include "import/ctr_image.hpp"
#include "import/sha256.hpp"
#include <algorithm>

namespace picarat::import {

namespace {

constexpr std::uint64_t kMedia = 0x200; // NCSD/NCCH media unit

std::uint32_t le32(std::span<const std::uint8_t> d, std::size_t at) {
    check(at + 4 <= d.size(), "3DS header field outside the data");
    return std::uint32_t(d[at] | d[at + 1] << 8 | d[at + 2] << 16 | std::uint32_t(d[at + 3]) << 24);
}

std::uint64_t le64(std::span<const std::uint8_t> d, std::size_t at) {
    return le32(d, at) | std::uint64_t(le32(d, at + 4)) << 32;
}

// RomFS names are UTF-16LE; non-ASCII characters are spelled %XX per byte.
std::string romfsName(std::span<const std::uint8_t> meta, std::size_t at, std::size_t length) {
    check(at <= meta.size() && length <= meta.size() - at && length % 2 == 0,
          "RomFS name outside its table");
    std::string name;
    for (std::size_t k = 0; k < length; k += 2) {
        const unsigned c = meta[at + k] | meta[at + k + 1] << 8;
        if (c >= 32 && c < 127 && c != '%') {
            name += char(c);
        } else {
            static constexpr char kHex[] = "0123456789ABCDEF";
            for (const unsigned byte : {c & 0xff, c >> 8}) {
                name += {'%', kHex[byte >> 4], kHex[byte & 15]};
            }
        }
    }
    check(!name.empty() && name.find('/') == std::string::npos, "invalid RomFS name");
    return name;
}

} // namespace

Bytes CtrRom::bytes(std::uint64_t offset, std::size_t size) {
    Bytes out(size);
    source_.read(offset, out);
    return out;
}

CtrRom::CtrRom(RomSource& source, const std::function<void(double)>& progress) : source_(source) {
    // Identity first: hash the whole image as it streams.
    Sha256 hash;
    Bytes chunk(8u << 20);
    for (std::uint64_t at = 0; at < source.size();) {
        const std::size_t n =
            std::size_t(std::min<std::uint64_t>(chunk.size(), source.size() - at));
        source.read(at, std::span(chunk.data(), n));
        hash.update(std::span(chunk.data(), n));
        at += n;
        if (progress) {
            progress(double(at) / double(source.size()));
        }
    }
    sha256_ = hash.finish();
    for (const KnownRom& known : kKnownRoms) {
        if (sha256_ == known.sha256) {
            identity_ = &known;
        }
    }
    check(identity_ != nullptr, "This is not one of the supported ROM images.");

    const Bytes ncsd = bytes(0, 0x200);
    check(std::equal(ncsd.begin() + 0x100, ncsd.begin() + 0x104, "NCSD"),
          "not a 3DS cartridge image");
    const std::uint64_t ncch = le32(ncsd, 0x120) * kMedia;
    const Bytes header = bytes(ncch, 0x200);
    check(std::equal(header.begin() + 0x100, header.begin() + 0x104, "NCCH"),
          "the game partition is missing");
    // Flags byte 7, bit 2: NoCrypto. Encrypted images are refused.
    check(header[0x188 + 7] & 4, "This ROM image is encrypted; only unencrypted images are read.");
    product_.assign(header.begin() + 0x150,
                    std::find(header.begin() + 0x150, header.begin() + 0x160, 0));
    check(product_.size() == 10 && product_.ends_with(identity_->gameCode),
          "ROM product code differs from its identity");

    // Extended header: the code segments and whether the code is compressed
    // (system control info flags, bit 0).
    check(le32(header, 0x180) >= 0x400, "the extended header is missing");
    const Bytes exheader = bytes(ncch + 0x200, 0x400);
    codeCompressed_ = exheader[0x0d] & 1;
    for (int i = 0; i < 3; ++i) {
        segments_[i] = {le32(exheader, 0x10 + 0x10 * i), le32(exheader, 0x18 + 0x10 * i)};
    }
    exefs_ = ncch + le32(header, 0x1a0) * kMedia;
    exefsSize_ = le32(header, 0x1a4) * kMedia;

    // RomFS: the IVFC hash tree's level 3 (the file system) follows the
    // header and master hash, aligned to its block size.
    const std::uint64_t romfs = ncch + le32(header, 0x1b0) * kMedia;
    const Bytes ivfc = bytes(romfs, 0x60);
    check(std::equal(ivfc.begin(), ivfc.begin() + 4, "IVFC") && le32(ivfc, 4) == 0x10000,
          "invalid RomFS");
    const unsigned blockShift = le32(ivfc, 0x4c);
    check(blockShift >= 4 && blockShift < 24, "invalid RomFS block size");
    const std::uint64_t block = 1ull << blockShift;
    const std::uint64_t level3 = romfs + (0x60 + le32(ivfc, 8) + block - 1) / block * block;
    const Bytes l3 = bytes(level3, 0x28);
    check(le32(l3, 0) == 0x28, "invalid RomFS file table");
    const Bytes dirs = bytes(level3 + le32(l3, 0x0c), le32(l3, 0x10));
    const Bytes files = bytes(level3 + le32(l3, 0x1c), le32(l3, 0x20));
    const std::uint64_t data = level3 + le32(l3, 0x24);

    // Walk the tree; every directory and file is visited once.
    std::vector<std::pair<std::uint32_t, std::string>> pending{{0, ""}};
    std::vector<bool> seen(dirs.size(), false);
    while (!pending.empty()) {
        const auto [dir, prefix] = pending.back();
        pending.pop_back();
        check(dir < dirs.size() && !seen[dir], "cyclic or invalid RomFS directory");
        seen[dir] = true;
        for (std::uint32_t f = le32(dirs, dir + 12), steps = 0; f != 0xffffffff;
             f = le32(files, f + 4)) {
            check(++steps <= files.size() / 0x20, "cyclic RomFS file list");
            CtrFile file{prefix + romfsName(files, f + 0x20, le32(files, f + 0x1c)),
                         data + le64(files, f + 8), le64(files, f + 16)};
            check(file.offset <= source.size() && file.size <= source.size() - file.offset,
                  "RomFS file outside the image");
            safeName(file.path);
            check(byPath_.emplace(file.path, files_.size()).second, "repeated RomFS path");
            files_.push_back(std::move(file));
        }
        for (std::uint32_t c = le32(dirs, dir + 8); c != 0xffffffff; c = le32(dirs, c + 4)) {
            pending.emplace_back(c, prefix + romfsName(dirs, c + 0x18, le32(dirs, c + 0x14)) + "/");
        }
    }
}

const CtrFile* CtrRom::find(std::string_view path) const {
    const auto it = byPath_.find(path);
    return it == byPath_.end() ? nullptr : &files_[it->second];
}

Bytes CtrRom::read(const CtrFile& file) {
    return bytes(file.offset, std::size_t(file.size));
}

Bytes CtrRom::code() {
    const Bytes header = bytes(exefs_, 0x200);
    for (int i = 0; i < 10; ++i) {
        const std::size_t e = std::size_t(i) * 16;
        if (std::string_view(reinterpret_cast<const char*>(header.data() + e), 5) !=
                std::string_view(".code", 5) ||
            header[e + 5] != 0) {
            continue;
        }
        const std::uint32_t offset = le32(header, e + 8), size = le32(header, e + 12);
        check(0x200ull + offset + size <= exefsSize_, "invalid ExeFS");
        Bytes code = bytes(exefs_ + 0x200 + offset, size);
        return codeCompressed_ ? decompressCtrCode(code) : code;
    }
    throw FormatError("the ExeFS lacks the code segment");
}

Bytes decompressCtrCode(std::span<const std::uint8_t> in) {
    check(in.size() >= 8, "code segment too short");
    const std::uint32_t bounds = le32(in, in.size() - 8), extra = le32(in, in.size() - 4);
    const std::size_t compressed = bounds & 0xffffff, header = bounds >> 24;
    check(compressed <= in.size() && header <= compressed && extra <= (64u << 20),
          "invalid code segment");
    Bytes out(in.begin(), in.end());
    out.resize(in.size() + extra);
    std::size_t src = in.size() - header, dst = out.size();
    const std::size_t stop = in.size() - compressed;
    while (src > stop) {
        std::uint8_t flags = out[--src];
        for (int bit = 0; bit < 8 && src > stop; ++bit, flags = std::uint8_t(flags << 1)) {
            if (flags & 0x80) {
                check(src >= stop + 2, "invalid code segment");
                src -= 2;
                const unsigned v = out[src] | out[src + 1] << 8;
                const std::size_t length = (v >> 12) + 3, distance = (v & 0xfff) + 3;
                for (std::size_t k = 0; k < length; ++k) {
                    check(dst > 0 && dst - 1 + distance < out.size(), "invalid code segment");
                    --dst;
                    out[dst] = out[dst + distance];
                }
            } else {
                check(dst > 0, "invalid code segment");
                out[--dst] = out[--src];
            }
        }
    }
    return out;
}

} // namespace picarat::import
