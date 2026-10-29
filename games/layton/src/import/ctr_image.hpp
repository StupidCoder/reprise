#pragma once
// Read-only access to an unencrypted Nintendo 3DS cartridge image (CCI,
// "NCSD"): identity, partition 0's code segment and its RomFS file tree.
// Container layouts are generic 3DS metadata (public format documentation).
//
// No decryption: only images whose game partition declares "no encryption"
// (NCCH flag NoCrypto) are read, and nothing is decrypted. The only
// transformation is the code segment's decompression.
#include "import/rom_image.hpp"
#include "import/rom_source.hpp"
#include <array>
#include <functional>
#include <map>

namespace picarat::import {

struct CtrFile {
    std::string path; // '/'-separated, from the RomFS root; non-ASCII as %XX
    std::uint64_t offset = 0, size = 0;
};

class CtrRom {
public:
    // Verifies the SHA-256 (one of kKnownRoms), the container headers, the
    // NoCrypto flag and every table bound. `progress` sees the hashed share.
    explicit CtrRom(RomSource& source, const std::function<void(double)>& progress = {});

    const std::string& sha256() const { return sha256_; }

    const KnownRom& identity() const { return *identity_; }

    std::string productCode() const { return product_; } // "CTR-P-AKKD"

    // The code segment (text, read-only data, data) as loaded at 0x00100000,
    // expanded.
    Bytes code();

    // The segments' load addresses and sizes from the extended header:
    // text, read-only data, data.
    struct Segment {
        std::uint32_t address = 0, size = 0;
    };

    std::array<Segment, 3> segments() const { return segments_; }

    const std::vector<CtrFile>& files() const { return files_; }

    const CtrFile* find(std::string_view path) const;
    Bytes read(const CtrFile& file);

    void read(std::uint64_t offset, std::span<std::uint8_t> out) { source_.read(offset, out); }

private:
    Bytes bytes(std::uint64_t offset, std::size_t size);

    RomSource& source_;
    std::string sha256_, product_;
    const KnownRom* identity_ = nullptr;
    std::uint64_t exefs_ = 0, exefsSize_ = 0;
    bool codeCompressed_ = false;
    std::array<Segment, 3> segments_{};
    std::vector<CtrFile> files_;
    std::map<std::string, std::size_t, std::less<>> byPath_;
};

// The code segment's compression: the DS overlays' backward LZ with
// distances counted from 3 instead of 1 (each flag byte, high bit first,
// selects a literal or a {length, distance} reference to bytes above).
Bytes decompressCtrCode(std::span<const std::uint8_t> data);

} // namespace picarat::import
