// Reader for an unencrypted Nintendo 3DS cartridge image (CCI, "NCSD" container): the first
// NCCH partition's ExeFS (including the compressed code segment) and its RomFS. General container
// formats, following public 3DS format documentation.
//
// No decryption: Diorama contains no cryptographic keys and no decryption or circumvention code of
// any kind. It reads only images whose partition header declares "no encryption" (NCCH flag
// NoCrypto) and refuses every other image. The only transformations applied to the data are
// decompression (the code segment's backward LZ here, Yaz0 archives in Archive.cpp).
#pragma once
#include "Binary.hpp"

#include <filesystem>
#include <fstream>
#include <functional>
#include <span>
#include <string>

namespace captain_toad {

class RomImage {
public:
    explicit RomImage(const std::filesystem::path& path);

    // Borrowed random-access source: Reprise supplies FileSource (including Android documents).
    using Reader = std::function<void(uint64_t, std::span<uint8_t>)>;
    RomImage(uint64_t size, Reader read);

    uint64_t programId() const { return programId_; }

    // ExeFS section by name (".code" is returned decompressed when the exheader marks it).
    Bytes exefsFile(const std::string& name);
    // RomFS file by '/'-separated path, e.g. "SoundData/sound_data.bcsar".
    Bytes romfsFile(const std::string& path);

private:
    void initialize();
    Bytes read(uint64_t offset, size_t size);
    Reader reader_;
    std::ifstream file_;
    uint64_t fileSize_ = 0;
    uint64_t ncch_ = 0;   // partition 0 offset
    uint64_t programId_ = 0;
    bool codeCompressed_ = false;
    uint64_t exefs_ = 0;
    uint64_t exefsSize_ = 0;
    uint64_t level3_ = 0;   // RomFS level-3 (file system) start
    Bytes level3Header_;
    Bytes dirMeta_;    // RomFS directory metadata table
    Bytes fileMeta_;   // RomFS file metadata table
};

// Backward LZ used for the ExeFS code segment (in place, decompressing from the end).
Bytes decompressCode(const Bytes& compressed);

}   // namespace captain_toad
