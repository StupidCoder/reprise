#pragma once
#include "mindwalker_amiga/data/game_files.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

// Mind Walker's pictures, image sets, palettes, sound samples and tables,
// decoded from an installed content folder (.analysis/mindwalker_amiga/
// formats.md has the layouts and the code that reads each one).
namespace mindwalker::data {

using Rgb4 = std::uint16_t; // $0RGB, four bits per gun
using Palette = std::vector<Rgb4>;

// A planar image as colour indices, row by row.
struct Bitmap {
    int width{};
    int height{};
    int planes{};
    std::vector<std::uint8_t> pixels;

    template <class A> void persist(A &ar) { ar(width, height, planes, pixels); }

    std::uint8_t at(int x, int y) const { return pixels[std::size_t(y * width + x)]; }
};

// Reads `planes` planes of width x height stored one after the other, each
// row padded to whole 16-pixel words (the layout of Intuition images and of
// every picture in this game).
Bitmap planar_bitmap(std::span<const std::uint8_t> bytes, int width, int height, int planes);

// A picture for the hold-and-modify screen: each pixel is a 6-bit HAM code
// (bits 0-3 data, bits 4-5 control: 0 palette colour, 1 modify blue,
// 2 modify red, 3 modify green), decoded against a 16-colour palette.
struct HamPicture {
    Bitmap codes;
    Palette palette; // 16 colours
};

// A set of 16-pixel-wide, 2-plane frames that the stages step through in
// `sequence` order (frame numbers, 0-based; S's 1-based index lists).
struct ImageSet {
    std::string name;
    std::vector<Bitmap> frames;
    std::vector<int> sequence;
};

// 8-bit signed samples as the game copies them to chip memory (some stages
// shift each byte left on the way, in 8 bits).
struct Sample {
    std::string name;
    std::vector<std::int8_t> data;
};

struct HighScore {
    std::uint32_t score{};
    std::string initials;
};

struct Assets {
    HamPicture title;                   // MindB on T's title palette
    std::array<Bitmap, 6> portraits;    // MindF1-6
    std::vector<ImageSet> figures;      // S's shared image sets
    std::vector<Bitmap> icons;          // S's small one-plane icons
    std::array<Bitmap, 2> hands;        // the tunnel's hands (B; shown in colours 8 + value)
    std::vector<Bitmap> arena_images;   // C's cells and other 4-plane images
    Palette landscape_palette, tunnel_palette, arena_palette, psyche_palette, title_palette;
    std::vector<std::string> words;     // P's 32 words
    std::vector<Sample> samples;
    std::vector<HighScore> high_scores; // MindHighs, empty entries left out
};

// Reads every decoded file of the content folder; throws std::runtime_error
// when one is missing or does not decode.
Assets load_assets(const std::filesystem::path& content);

} // namespace mindwalker::data
