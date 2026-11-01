// Verified access to the ROM files the importer reads. Every file is checked against the SHA-256
// of the supported release (Europe, program 00040000001CB200) before use, so packages are always
// built from exactly the data the reconstruction was validated against. The files come from the
// ROM image itself (the import) or from the verified copy of its file system that the import
// leaves in the data folder (the stages, built when first played).
#pragma once
#include "Archive.hpp"
#include "Bch.hpp"
#include "RomImage.hpp"

#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace captain_toad {

constexpr uint64_t kSupportedProgramId = 0x00040000001cb200ull;

class Inputs {
public:
    explicit Inputs(RomImage& rom);
    // The extracted copy: `extracted`/exefs/code.bin and `extracted`/romfs/PATH.
    explicit Inputs(const std::filesystem::path& extracted);

    // Decompressed code segment (code.bin), file offset = address - 0x100000.
    const Bytes& code();
    // RomFS file, verified.
    const Bytes& romfs(const std::string& path);
    // RomFS file, verified, without keeping it (the import's copy of the file system).
    Bytes copy(const std::string& path);
    // True when the release has this RomFS file.
    static bool has(const std::string& path);
    // Member of a Yaz0/SARC archive in the RomFS.
    const Bytes& file(const std::string& archive, const std::string& member);
    const std::vector<SarcMember>& archive(const std::string& path);
    Bch bch(const std::string& archive, const std::string& member) { return Bch(file(archive, member)); }
    json::Value byml(const std::string& archive, const std::string& member) {
        return captain_toad::byml(file(archive, member));
    }

    // ROM paths of every file read, with their digests (package provenance).
    json::Value sources() const;
    // Called after each verified RomFS file (progress reporting).
    std::function<void(const std::string&)> onFile;

private:
    std::function<Bytes()> readCode_;
    std::function<Bytes(const std::string&)> readFile_;
    Bytes code_;
    std::map<std::string, Bytes> files_;
    std::map<std::string, std::vector<SarcMember>> archives_;
};

// Expected digest of a supported input, or nullptr.
const char* expectedDigest(const std::string& path);
// Every RomFS file of the supported release, sorted.
std::vector<std::string> romfsPaths();

}   // namespace captain_toad
