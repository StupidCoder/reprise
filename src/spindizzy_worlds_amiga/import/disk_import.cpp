#include "spindizzy_worlds_amiga/import/disk_import.hpp"
#include "amiga/adf.hpp"
#include "platform/sha256.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <string_view>

namespace spindizzy::import {
namespace {

// The disk tag the program itself checks in the boot block ($19FD4 reads
// block 0 and compares the bytes at +$E with "SD][MASTER").
constexpr std::size_t tag_offset = 0xE;
constexpr std::string_view disk_tag = "SD][MASTER";

struct Range {
    std::string_view name;
    std::size_t offset; // ADF byte offset (block × 512 for block-aligned files)
    std::size_t length;
    std::string_view sha256;
};

// The program's loads: $40000 (main), $177D2 "A:\title.neo", $1124C
// (music), $1894A (effects), $16BC0 "A:\sd_game.dat", $10042
// "A:\SWAP\SELECT.SWP" and $11C8C (worlds, $2000 bytes each).
constexpr Range main_range{"main", 0x16 * amiga::block_size, 0x1A4 * amiga::block_size,
                           "2411c396c94e35a3f997f26e6b58a4851afe5cfae249b1731fa62a6e0f3f3505"};
constexpr std::array ranges = {
    Range{"title.neo", 0x3A200, 0x7D80, "ef13e7f3cbb3c7e10a112946301bd85409a5cb99dfdf3b92cfa8f1006e0953b6"},
    Range{"music", 0x45E00, 0x9C40, "8a91660a7390e7d296aa999c05cf444c6afdf8da8e5fb95f4c04057d98718dd5"},
    Range{"effects", 0x4FE00, 0xC000, "b801bdf169b8a22851782d9cb59fb019568a82bf0fb11ee61e26bd9adb04c865"},
    Range{"sd_game.dat", 0x7A600, 0x83C, "8d1d4518fbc5d4b39d0f4591f39310e12a6423c870327ea229da15169fca7c29"},
    Range{"select.swp", 0x7B000, 0x5140, "e77c2a5675d644912daedf21fe8b6dbc2da6aa9fab5fec4c7b77db8572277cb9"},
};

constexpr std::size_t world_offset = 0x80000;
constexpr std::size_t world_size = 0x2000;
constexpr int world_count = 32; // $1177A: world numbers 0-$1F
constexpr std::string_view worlds_sha256 = "c9cb3e52d09b45ce3a28b87af1e13ef60f905c0312c611f4d6670427e10db01b";

// "Spindizzy Worlds (1990)(Electric Dreams)[cr HZ].adf", SHA-256 d0df7622…bf9c055.
// A cracked copy: the boot block first loads and runs the crackers' intro
// (byte $D1000 → $30000); the game's own files look untouched.
constexpr std::string_view release_name = "1990, cracked copy";

std::vector<std::uint8_t> slice(std::span<const std::uint8_t> image, std::size_t offset, std::size_t length) {
    return {image.begin() + std::ptrdiff_t(offset), image.begin() + std::ptrdiff_t(offset + length)};
}

} // namespace

void unscramble_world(std::vector<std::uint8_t>& bytes) {
    std::uint16_t key = 0xCC0C, turn = 0x62C0; // d2, d3 at $11CFC
    for (std::size_t i = 0; i < 0x7FE && 2 * i + 1 < bytes.size(); ++i) {
        const std::uint16_t word = std::uint16_t(bytes[2 * i] << 8 | bytes[2 * i + 1]);
        const std::uint16_t plain = word ^ key;
        bytes[2 * i] = std::uint8_t(plain >> 8);
        bytes[2 * i + 1] = std::uint8_t(plain);
        const int n = word & 15; // ror.w d1,d3 rotates by the scrambled word mod 16
        turn = std::uint16_t(n == 0 ? turn : (turn >> n | turn << (16 - n)));
        key ^= turn;
    }
}

app::ImportResult import_disk(std::span<const std::uint8_t> image) {
    if (!amiga::is_adf(image)) {
        throw app::NotThisGame("This is not an Amiga disk image (ADF).");
    }
    const auto tag = image.subspan(tag_offset, disk_tag.size());
    if (!std::equal(tag.begin(), tag.end(), disk_tag.begin())) {
        throw app::NotThisGame("This disk is not Spindizzy Worlds.");
    }
    app::ImportResult result;
    auto main = slice(image, main_range.offset, main_range.length);
    if (platform::sha256(main) != main_range.sha256) {
        throw std::runtime_error("This disk holds a different release of Spindizzy Worlds, which is not supported yet.");
    }
    result.release = std::string(release_name);
    result.files.push_back({std::string(main_range.name), std::move(main)});
    for (const auto& range : ranges) {
        auto bytes = slice(image, range.offset, range.length);
        if (platform::sha256(bytes) != range.sha256) {
            throw std::runtime_error("The disk's " + std::string(range.name) +
                                     " data is damaged or from a different release.");
        }
        result.files.push_back({std::string(range.name), std::move(bytes)});
    }
    if (platform::sha256(image.subspan(world_offset, world_count * world_size)) != worlds_sha256) {
        throw std::runtime_error("The disk's world files are damaged or from a different release.");
    }
    for (int n = 0; n < world_count; ++n) {
        auto bytes = slice(image, world_offset + std::size_t(n) * world_size, world_size);
        unscramble_world(bytes);
        char name[16];
        std::snprintf(name, sizeof name, "world%02d", n);
        result.files.push_back({name, std::move(bytes)});
    }
    return result;
}

} // namespace spindizzy::import
