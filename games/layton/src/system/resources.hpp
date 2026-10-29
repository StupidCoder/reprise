#pragma once
// Basic file and data access: little-endian reads, whole-file reads, shared
// byte buffers, language-specific resource paths and the game's PCK2
// archives.
//
// The game keeps most small files bundled in PCK2 archives (".plz"), which
// are a header followed by named members. AssetStore reads files from an
// extracted copy of the cartridge filesystem (used by research tools); the
// game itself reads the native pack through GraphicsAssets (graphics.hpp).
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace picarat {
using Bytes = std::vector<std::uint8_t>;

// Little-endian 16- and 32-bit reads from a pointer, for data whose size was
// already checked (table rows, chunk walks).
inline std::uint16_t le16(const std::uint8_t* p) {
    return std::uint16_t(p[0] | p[1] << 8);
}

inline std::uint32_t le32(const std::uint8_t* p) {
    return p[0] | p[1] << 8 | p[2] << 16 | std::uint32_t(p[3]) << 24;
}

// Little-endian 16- and 32-bit reads, bounds-checked.
std::uint16_t read16(std::span<const std::uint8_t> bytes, std::size_t offset);
std::uint32_t read32(std::span<const std::uint8_t> bytes, std::size_t offset);

// Reads a whole file (at most 128 MiB).
Bytes readFile(const std::filesystem::path& path);

// The cartridge's file system ignores letter case: the games ask for
// "bg/i_car4" where the ROM's file is "I_car4", and packs keep the ROM's
// names. On a file system that tells case apart (Android, Linux) a path that
// does not exist as written is matched name by name ignoring ASCII case
// (directory listings are cached and reread when a directory changes).
// Returns the path as written when nothing matches.
std::filesystem::path resolvePath(const std::filesystem::path& path);
// std::filesystem::exists through resolvePath.
bool fileExists(const std::filesystem::path& path);

// A view of shared bytes. The game reads files into shared scratch buffers;
// here every resource owns (a share of) its data, so an archive member stays
// valid even after its archive is gone.
class Resource {
public:
    explicit Resource(Bytes bytes);
    std::span<const std::uint8_t> bytes() const;
    Resource slice(std::size_t offset, std::size_t length) const;

private:
    std::shared_ptr<const Bytes> storage_;
    std::size_t offset_ = 0, length_ = 0;
};

// Replaces the '?' in a resource path by the two-letter language folder,
// e.g. "nazo/?/q1" -> "nazo/ge/q1" (overlay 2, 02085a9c). The language comes
// from the caller, never from the computer's settings.
std::string resolveLanguagePath(std::string_view path, std::string_view language);
// A background's file in the ROM: /data_lt2/bg/<name> with the last three
// characters replaced by "arc" (0206ffec / 0207064c).
std::string backgroundPath(std::string_view logicalName);

// Files of an extracted cartridge filesystem (research layout: "raw/fs" as
// stored, "expanded" decompressed). Paths may not leave the store.
class AssetStore {
public:
    explicit AssetStore(std::filesystem::path extractedRoot);
    Resource raw(std::string_view romPath) const;
    Resource expanded(std::string_view romPath) const;

private:
    std::filesystem::path root_;
    Resource load(std::string_view directory, std::string_view romPath) const;
};

// A PCK2 archive; members are found by exact name, first match (0206c834 /
// 0206c8b8). Unlike the original, every length is checked.
class PckArchive {
public:
    explicit PckArchive(Resource resource);
    std::optional<Resource> find(std::string_view name) const;

private:
    Resource resource_;

    struct Member {
        std::string name;
        std::size_t offset, length;
    };

    std::vector<Member> members_;
};
} // namespace picarat
