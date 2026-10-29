#pragma once
// Read-only access to the supplied Nintendo DS ROM: identity, the cartridge
// filesystem (FNT/FAT) and the overlay tables. Header and table layouts are
// generic DS hardware metadata (GBATEK); only the confirmed images listed in
// kKnownRoms are accepted.
#include "system/resources.hpp"
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace picarat::import {

// The games the library can import, named like their library cards.
enum class Game { Adventure1, Adventure2, Adventure3, Adventure4, Adventure5, Adventure6 };

struct KnownRom {
    Game game;
    std::string_view sha256, gameCode;
    const char* title; // the library card's title
};

// The confirmed ROM images, revision 0: the German editions of the second
// to sixth adventures and the European edition of the first, which holds
// English, French, German, Spanish and Italian (there is no German-only
// one). The fifth and sixth are unencrypted Nintendo 3DS cartridge images
// (CCI); their game code is the product code's last four letters.
inline constexpr KnownRom kKnownRoms[] = {
    {Game::Adventure1, "d808728d3e930b7a6c952d05e5e39db4a15b685c9c0919b57ece07b5243f493f", "A5FP",
     "Curious Village"},
    {Game::Adventure2, "76ed9184ce07c7d4cb342aff4cc960412f36b3d99558aebdb37a2db41cfc275e", "YLTD",
     "Pandora's Box"},
    {Game::Adventure3, "e8d72745b7530ba3cf2fa713d1840d0f9aebb20a835f2746f32b6bdcd8ea373b", "C3JD",
     "Lost Future"},
    {Game::Adventure4, "f26b840d79ca67f9d7621c5bfbd30bb7db2f20a773b75360d81537bbdfdf35d2", "BLFD",
     "Spectre's Call"},
    {Game::Adventure5, "3fae2d2f5c3bc10933a9446904c9dcef5f02b80a311a3c20d71ae857aaec9788", "AKKD",
     "Miracle Mask"},
    {Game::Adventure6, "4ca8db98214909a1111034d0d8183f4238b038885cac3b27aa16a801c61c5910", "AL6D",
     "Azran Legacy"},
};

const KnownRom& knownRom(Game game);

// Malformed or unexpected input. Import reports the message to the user.
struct FormatError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

void check(bool condition, const char* message);

// Bounds-checked little-endian cursor over a byte span.
class ByteReader {
public:
    explicit ByteReader(std::span<const std::uint8_t> data, std::size_t offset = 0);
    std::span<const std::uint8_t> take(std::size_t size);
    std::uint8_t u8();
    std::uint16_t u16();
    std::uint32_t u32();

    std::int16_t i16() { return std::int16_t(u16()); }

    std::int32_t i32() { return std::int32_t(u32()); }

    std::size_t position() const { return pos_; }

    std::size_t remaining() const { return data_.size() - pos_; }

    std::span<const std::uint8_t> data() const { return data_; }

private:
    std::span<const std::uint8_t> data_;
    std::size_t pos_;
};

// Relative, slash-separated names only: rejects absolute paths, '..', ':' and
// control characters before anything reaches the host filesystem.
const std::string& safeName(const std::string& name);

struct RomOverlay {
    bool arm9 = true;
    std::uint32_t id = 0, loadAddress = 0, ramSize = 0, bssSize = 0;
    std::uint32_t initStart = 0, initEnd = 0, flags = 0;
};

struct RomFile {
    std::uint32_t id = 0;
    std::string path; // empty for unnamed FAT entries (overlays); non-ASCII bytes as %XX
    std::uint32_t offset = 0, size = 0;
    std::optional<RomOverlay> overlay;
};

class NdsRom {
public:
    // Verifies SHA-256 (one of kKnownRoms), header/logo CRC16 and every
    // table bound.
    explicit NdsRom(Bytes image);

    const std::string& sha256() const { return sha256_; }

    const KnownRom& identity() const { return *identity_; }

    std::string gameCode() const;

    const std::vector<RomFile>& files() const { return files_; }

    std::span<const std::uint8_t> contents(const RomFile& file) const;
    // The resident ARM9 program as it is in memory (loaded at 0x02000000),
    // expanded from its compressed form.
    Bytes arm9Program() const;
    // The ARM7 program as the header loads it (stored plainly; its start-up
    // code copies the autoload segments it carries).
    Bytes arm7Program() const;
    const RomFile* find(std::string_view path) const;
    const RomFile* overlay(bool arm9, std::uint32_t id) const;

private:
    Bytes image_;
    std::string sha256_;
    const KnownRom* identity_ = nullptr;
    std::vector<RomFile> files_;
    std::unordered_map<std::string, std::size_t> byPath_;
};

// Overlay "backward LZ" used by the DS SDK for code compression (GBATEK).
// The footer's last word is the size increase; the word before holds the
// footer length (top byte) and the compressed span (low 24 bits). Decoding
// runs from the end towards the start; bytes below the span stay as stored.
Bytes decompressCode(std::span<const std::uint8_t> data);

} // namespace picarat::import
