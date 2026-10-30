#include "stunt_car_racer_amiga/import/disk_import.hpp"
#include "amiga/adf.hpp"
#include "platform/sha256.hpp"
#include <array>
#include <stdexcept>
#include <string_view>

namespace stuntcar::import {
namespace {

struct Region {
    std::string_view name;
    std::size_t first_sector;
    std::size_t sectors;
};

// The boot block's read ($2C00, $9800 bytes) and the loader's ($6E, $325 sectors).
constexpr Region loader_region{"loader", 22, 76};
constexpr Region program_region{"program", 110, 805};

struct Release {
    std::string_view name;
    std::string_view loader_sha256;
    std::string_view program_sha256;
};

// "Stunt Car Racer.adf", SHA-256 548fd106…5c8a9e06. The program's Copylock
// routine is still present, yet the game runs with the genuine key on an
// emulated drive (game.md, "Copy protection").
constexpr std::array releases = {
    Release{"1989 MicroStyle release",
            "a04faf467c1d155538095bc925023118bff674bf2051bb88e67c9825375989c5",
            "8b2e072197345617504ca328ccc452fcf6dd88137a51061a5584f73b653c085d"},
};

std::vector<std::uint8_t> slice(std::span<const std::uint8_t> image, const Region& region) {
    const auto begin = image.begin() + std::ptrdiff_t(region.first_sector * amiga::block_size);
    return {begin, begin + std::ptrdiff_t(region.sectors * amiga::block_size)};
}

} // namespace

app::ImportResult import_disk(std::span<const std::uint8_t> image) {
    if (!amiga::is_adf(image) || image.size() < amiga::dd_blocks * amiga::block_size) {
        throw app::NotThisGame("This is not an Amiga disk image (ADF).");
    }
    auto loader = slice(image, loader_region);
    const auto loader_sha = platform::sha256(loader);
    const Release* release = nullptr;
    for (const auto& known : releases) {
        if (known.loader_sha256 == loader_sha) release = &known;
    }
    if (release == nullptr) {
        throw app::NotThisGame("This disk is not a known release of Stunt Car Racer.");
    }
    auto program = slice(image, program_region);
    if (platform::sha256(program) != release->program_sha256) {
        throw std::runtime_error("The disk's game program is damaged or from a different release of Stunt Car Racer.");
    }
    app::ImportResult result;
    result.release = std::string(release->name);
    result.files.push_back({std::string(loader_region.name), std::move(loader)});
    result.files.push_back({std::string(program_region.name), std::move(program)});
    return result;
}

} // namespace stuntcar::import
