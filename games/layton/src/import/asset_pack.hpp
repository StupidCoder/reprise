#pragma once
// Builds the versioned native asset pack the game reads (GraphicsAssets,
// GameText, sound, movies) straight from the ROM. No pixels come from
// emulation: every image is decoded from its original resource, and tables
// such as the keyboard escapes come from overlay 2's own data.
#include "import/asset_codecs.hpp"
#include "import/rom_image.hpp"
#include <filesystem>
#include <functional>
#include <map>

namespace picarat::import {

// Called between steps with the fraction of the pack written so far. It may
// throw (for example to cancel); the caller then discards the output.
using PackProgress = std::function<void(double fraction, const char* message)>;

struct PackResult {
    std::map<std::string, std::string> outputs; // pack path -> SHA-256
};

// escapes.bin for GameText: the ROM's {name, character} substitutions in
// table order. Version 1 is the second adventure's "<name>" syntax; version
// 2 records other delimiters (the third adventure's "{xy}").
Bytes escapeTableFile(const std::vector<std::pair<std::string, std::uint32_t>>& escapes, char open,
                      char close);

// The native picture, sprite and font files GraphicsAssets reads, shared by
// the adventures whose resources use these formats (second and first).
// A background expanded from its .arc (or stored as .bgx/.arb): version 2
// appends tile 0, which the 256x256 BG map shows outside the picture;
// `transparent` makes colour 0 see-through (BG layers over sprites).
Bytes nativeBackground(std::span<const std::uint8_t> expanded, bool transparent,
                       bool withTile = true);
// ARM9 sprite extension: the attached child sprite's file name, if any.
std::string spriteChild(const Sprite& sprite);
// A sprite (decodeSprite) with its animations, variables, child attachment
// (`childPath` as the pack names it, or empty) and texture-chunk records;
// `rotating` keeps a second copy of the chunk records (rotating tiles).
Bytes nativeSprite(const Sprite& sprite, const std::string& childPath, bool rotating);
// A font with, optionally, the program's separate width table (224 bytes).
Bytes nativeFont(const Font& font, std::span<const std::uint8_t> widths);

// Writes the pack into `output` (created if needed) and its provenance.json.
// `library` lists the internal puzzle IDs to enable, in collection order.
PackResult buildNativePack(const NdsRom& rom, const std::filesystem::path& output,
                           std::span<const unsigned> library, const PackProgress& progress = {});

} // namespace picarat::import
