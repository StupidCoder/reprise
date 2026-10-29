#include "tower_of_babel_amiga/import/disk_import.hpp"
#include "amiga/adf.hpp"
#include "platform/sha256.hpp"
#include "tower_of_babel_amiga/import/decruncher.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>
#include <string_view>

namespace babel::import {
namespace {

// The boot block's own words: the game's copyright line at +$10.
constexpr std::string_view boot_text = "Tower Of Babel Amiga v1.0";

// The stage-one loader: the boot block reads $CA00 bytes from byte $2C00.
constexpr std::size_t loader_offset = 0x2C00;
constexpr std::size_t loader_length = 0xCA00;

struct Release {
    std::string_view name;
    std::string_view loader_sha256;
    std::string_view program_sha256; // of the unpacked program
};

// "Tower of Babel v1.0 (1990)(Rainbird)[cr MAD].adf", SHA-256 0a8dff1a…8c8785c71.
// A cracked copy: the boot block calls the crackers' intro and the
// copy-protection hook at $17600 is a stub. The game's data looks untouched.
constexpr std::array releases = {
    Release{"v1.0 (1990), cracked copy",
            "7b107319525c4b8202786fa46f2beec7f0a0447b3ac992f337bdd92b20fd9aa9",
            "5b8a5c6b120ea27dc32ebda8438a63310d652de3317e339eb3706aabb929d09e"},
};

struct BlockRange {
    std::string_view name;
    std::size_t first;  // block number
    std::size_t blocks;
    std::string_view sha256;
};

// The ranges the program reads (its loads at $7B46, $7C00, $7F66, $9F20).
constexpr std::array ranges = {
    BlockRange{"title", 0xA9, 0x22, "ef38cc854e20470842f4feaa79ae46d8da2e7bd0dcc96c3de7997548cf01c9af"},
    BlockRange{"main", 0xCB, 0x269, "1037f77c030ce4afcbb03bc089475e94ec683e8bbb8672b823f8d869e44a5336"},
    BlockRange{"panel1", 0x334, 0x24, "cd64e9c417f6525248d2697de856a2fb73ce3e9983b8292d5cb5d19174541190"},
    BlockRange{"panel2", 0x358, 0x24, "f4a579e907428e86435005190c1c4769d5ae2c9bb7b15124db3fd5079aab61d3"},
    BlockRange{"panel3", 0x37C, 0x24, "a55581377d541002134da3cd6a0b772119576ab9d67b2afc7431d904ab5b18a7"},
    BlockRange{"music", 0x3A0, 0x198, "75eb1107026dcc9567b0b607ec5687e6c0897b442523a0bfc8c0a33bf63c0519"},
};

std::vector<std::uint8_t> slice(std::span<const std::uint8_t> image, std::size_t offset, std::size_t length) {
    return {image.begin() + std::ptrdiff_t(offset), image.begin() + std::ptrdiff_t(offset + length)};
}

} // namespace

app::ImportResult import_disk(std::span<const std::uint8_t> image) {
    if (!amiga::is_adf(image)) {
        throw app::NotThisGame("This is not an Amiga disk image (ADF).");
    }
    const auto boot = image.subspan(0x10, boot_text.size());
    if (!std::equal(boot.begin(), boot.end(), boot_text.begin())) {
        throw app::NotThisGame("This disk is not Tower of Babel.");
    }
    app::ImportResult result;
    const auto loader = slice(image, loader_offset, loader_length);
    const auto loader_sha = platform::sha256(loader);
    const Release* release = nullptr;
    for (const auto& known : releases) {
        if (known.loader_sha256 == loader_sha) release = &known;
    }
    if (release == nullptr) {
        throw std::runtime_error("This disk holds a different release of Tower of Babel, which is not supported yet.");
    }
    result.release = std::string(release->name);
    auto program = unpack_program(loader);
    if (platform::sha256(program) != release->program_sha256) {
        throw std::runtime_error("The game's program does not unpack to the expected bytes; the disk may be damaged.");
    }
    result.files.push_back({"program", std::move(program)});
    for (const auto& range : ranges) {
        auto bytes = slice(image, range.first * amiga::block_size, range.blocks * amiga::block_size);
        if (platform::sha256(bytes) != range.sha256) {
            throw std::runtime_error("The disk's " + std::string(range.name) +
                                     " data is damaged or from a different release.");
        }
        result.files.push_back({std::string(range.name), std::move(bytes)});
    }
    return result;
}

} // namespace babel::import
