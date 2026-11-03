// GNF textures: the platform's texture container. A 0x10-byte file header, one 32-byte GPU texture
// descriptor per texture, user data, then the pixel data at offset 8 + contentsSize. The pixel data
// is stored in the GPU's tiled layout; loadGnf returns every mip level linearised, with compressed
// formats kept as 4x4 blocks so the renderer can upload them unchanged.
//
// Layout facts (verified against the stored data size of all 7,197 game textures, see
// .analysis/docs/formats.md): levels follow each other; each level holds all array slices; a
// level's row pitch and height are padded to whole 8x8-element micro tiles; with the descriptor's
// pow2-pad bit, width, height, pitch and the slice count are rounded up to powers of two
// (cube maps therefore occupy 8 slices).
#pragma once

#include "data/Bytes.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace beacon::data {

enum class TextureFormat { Unknown, Bc1, Bc2, Bc3, Bc4, Bc5, Bc6h, Bc7, Rgba8 };
enum class TextureKind { Tex2D, Tex3D, Cube };

struct TextureLevel {
    std::uint32_t width = 0, height = 0, depth = 1;
    // Linear data: slices (or depth layers) one after another, each a tightly packed grid of
    // elements (4x4 blocks for compressed formats, pixels otherwise), rows top to bottom.
    std::vector<std::uint8_t> data;
};

struct Texture {
    TextureFormat format = TextureFormat::Unknown;
    TextureKind kind = TextureKind::Tex2D;
    bool srgb = false;
    std::uint32_t width = 0, height = 0, depth = 1, layers = 1;
    std::vector<TextureLevel> levels;
};

bool isBlockCompressed(TextureFormat format);
std::uint32_t bytesPerElement(TextureFormat format);
const char* formatName(TextureFormat format);

std::optional<Texture> loadGnf(ByteView file, std::string* error = nullptr);

}   // namespace data
