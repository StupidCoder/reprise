// Checkpoint files. Layout (little-endian):
//   "L2CP", version (2), ROM SHA-256 as 64 hex characters,
//   name: 10 characters, cursor, keyboard layout,
//   scene, following scene, event ID (16 bits), puzzle (16 bits),
//   flags (bit 0 retry variant, bit 1 revisit variant, bit 2 game cleared),
//   random state, the 0x320-byte progress block, and a CRC-32 of all of it.
// Files are written to a temporary name and then renamed, so a crash never
// leaves a half-written checkpoint.
#include "system/a2_checkpoint.hpp"
#include "system/resources.hpp"
#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <zlib.h>

namespace picarat {
namespace {
constexpr std::uint32_t version = 2;
constexpr std::size_t fileSize =
    4 + 4 + 64 + 10 + 4 + 4 + 1 + 1 + 2 + 2 + 1 + 4 + SaveProgress::size + 4;
const std::string identity = "76ed9184ce07c7d4cb342aff4cc960412f36b3d99558aebdb37a2db41cfc275e";

void word(Bytes& bytes, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) {
        bytes.push_back(std::uint8_t(value >> (8 * i)));
    }
}

void validate(const Checkpoint& c) {
    // Only boundaries the native router can resume from.
    const bool sceneOk = c.scene == scene::Room || c.scene == scene::DramaEvent ||
                         c.scene == scene::Puzzle || c.scene == scene::PuzzleResult;
    if (!sceneOk || c.name.cursor > 9 || c.name.layout < 0 || c.name.layout > 3 ||
        c.progress.place() < 0 || c.progress.place() >= 128) {
        throw std::runtime_error("Unsupported native checkpoint state");
    }
}
} // namespace

void CheckpointStore::save(const Checkpoint& c) const {
    validate(c);
    Bytes bytes{'L', '2', 'C', 'P'};
    word(bytes, version);
    // Identity prevents accidental loading against a different game's build.
    bytes.insert(bytes.end(), identity.begin(), identity.end());
    bytes.insert(bytes.end(), c.name.characters.begin(), c.name.characters.end());
    word(bytes, c.name.cursor);
    word(bytes, unsigned(c.name.layout));
    bytes.push_back(c.scene);
    bytes.push_back(c.following);
    bytes.push_back(std::uint8_t(c.eventId));
    bytes.push_back(std::uint8_t(c.eventId >> 8));
    bytes.push_back(std::uint8_t(c.puzzle));
    bytes.push_back(std::uint8_t(std::uint16_t(c.puzzle) >> 8));
    bytes.push_back(std::uint8_t(unsigned(c.retryVariant) | (unsigned(c.revisitVariant) << 1) |
                                 (unsigned(c.cleared) << 2)));
    word(bytes, c.randomState);
    bytes.insert(bytes.end(), c.progress.bytes.begin(), c.progress.bytes.end());
    word(bytes, std::uint32_t(crc32(0, bytes.data(), uInt(bytes.size()))));
    std::filesystem::create_directories(path_.parent_path());
    const auto temporary = path_.string() + ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out.exceptions(std::ios::badbit | std::ios::failbit);
        out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        out.flush();
        out.close();
    }
    // Same-directory rename is atomic: interrupted writes retain the prior save.
    std::filesystem::rename(temporary, path_);
}

std::optional<Checkpoint> CheckpointStore::load() const {
    if (!fileExists(path_)) {
        return std::nullopt;
    }
    const auto bytes = readFile(path_);
    if (bytes.size() >= 8 && std::string(bytes.begin(), bytes.begin() + 4) == "L2CP" &&
        read32(bytes, 4) == 1) {
        throw std::runtime_error(
            "This checkpoint was written before the story router (format 1) and cannot be "
            "continued; start a new game. It has not been overwritten");
    }
    if (bytes.size() != fileSize || std::string(bytes.begin(), bytes.begin() + 4) != "L2CP" ||
        read32(bytes, 4) != version ||
        std::string(bytes.begin() + 8, bytes.begin() + 72) != identity ||
        read32(bytes, fileSize - 4) != crc32(0, bytes.data(), uInt(fileSize - 4))) {
        throw std::runtime_error(
            "Native checkpoint is damaged or incompatible; it has not been overwritten");
    }
    Checkpoint c;
    std::size_t p = 72;
    std::copy_n(bytes.begin() + p, 10, c.name.characters.begin());
    p += 10;
    c.name.cursor = read32(bytes, p);
    p += 4;
    c.name.layout = int(read32(bytes, p));
    p += 4;
    c.scene = bytes[p++];
    c.following = bytes[p++];
    c.eventId = read16(bytes, p);
    p += 2;
    c.puzzle = std::int16_t(read16(bytes, p));
    p += 2;
    const auto flags = bytes[p++];
    if (flags > 7) {
        throw std::runtime_error("Invalid native checkpoint field");
    }
    c.retryVariant = flags & 1;
    c.revisitVariant = flags & 2;
    c.cleared = flags & 4;
    c.randomState = read32(bytes, p);
    p += 4;
    std::copy_n(bytes.begin() + p, SaveProgress::size, c.progress.bytes.begin());
    validate(c);
    return c;
}
} // namespace picarat
