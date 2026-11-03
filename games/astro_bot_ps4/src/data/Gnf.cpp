#include "data/Gnf.hpp"

#include <algorithm>

namespace beacon::data {

namespace {

// GPU texture descriptor fields (8 dwords, hardware layout).
struct Descriptor {
    std::uint32_t dataFormat, numberFormat;
    std::uint32_t width, height, depth, pitch;
    std::uint32_t baseLevel, lastLevel, tileMode, type;
    std::uint32_t lastArray;
    bool pow2Pad;
    std::uint32_t dataSize;   // dword 7: the GNF writer stores the texture's byte size here
};

Descriptor decode(ByteView d) {
    Descriptor t{};
    std::uint32_t w1 = u32(d, 4), w2 = u32(d, 8), w3 = u32(d, 12), w4 = u32(d, 16), w5 = u32(d, 20);
    t.dataFormat = (w1 >> 20) & 0x3f;
    t.numberFormat = (w1 >> 26) & 0xf;
    t.width = (w2 & 0x3fff) + 1;
    t.height = ((w2 >> 14) & 0x3fff) + 1;
    t.baseLevel = (w3 >> 12) & 0xf;
    t.lastLevel = (w3 >> 16) & 0xf;
    t.tileMode = (w3 >> 20) & 0x1f;
    t.pow2Pad = ((w3 >> 25) & 1) != 0;
    t.type = w3 >> 28;
    t.depth = (w4 & 0x1fff) + 1;
    t.pitch = ((w4 >> 13) & 0x3fff) + 1;
    t.lastArray = (w5 >> 13) & 0x1fff;
    t.dataSize = u32(d, 28);
    return t;
}

// Hardware image data formats used by the game's textures.
TextureFormat toFormat(std::uint32_t dataFormat) {
    switch (dataFormat) {
    case 10:
        return TextureFormat::Rgba8;
    case 35:
        return TextureFormat::Bc1;
    case 36:
        return TextureFormat::Bc2;
    case 37:
        return TextureFormat::Bc3;
    case 38:
        return TextureFormat::Bc4;
    case 39:
        return TextureFormat::Bc5;
    case 40:
        return TextureFormat::Bc6h;
    case 41:
        return TextureFormat::Bc7;
    default:
        return TextureFormat::Unknown;
    }
}

constexpr std::uint32_t kTileThin1d = 13;    // 1D tiling: 8x8-element micro tiles in row order
constexpr std::uint32_t kTileThick1d = 19;   // 1D tiling: 8x8x4-element micro tiles
constexpr std::uint32_t kNumberFormatSrgb = 9;
constexpr std::uint32_t kType3D = 10, kTypeCube = 11;

std::uint32_t nextPow2(std::uint32_t v) {
    std::uint32_t p = 1;
    while (p < v) {
        p <<= 1;
    }
    return p;
}

// Index of element (x, y, z) inside a micro tile for the non-displayable orders: bits interleave
// x0 y0 x1 y1 x2 y2 (thin), x0 y0 z0 x1 y1 z1 x2 y2 (thick).
std::uint32_t microIndexThin(std::uint32_t x, std::uint32_t y) {
    return (x & 1) | (y & 1) << 1 | (x & 2) << 1 | (y & 2) << 2 | (x & 4) << 2 | (y & 4) << 3;
}

std::uint32_t microIndexThick(std::uint32_t x, std::uint32_t y, std::uint32_t z) {
    return (x & 1) | (y & 1) << 1 | (z & 1) << 2 | (x & 2) << 2 | (y & 2) << 3 | (z & 2) << 4 | (x & 4) << 4 |
           (y & 4) << 5;
}

}   // namespace

bool isBlockCompressed(TextureFormat format) {
    return format != TextureFormat::Unknown && format != TextureFormat::Rgba8;
}

std::uint32_t bytesPerElement(TextureFormat format) {
    switch (format) {
    case TextureFormat::Bc1:
    case TextureFormat::Bc4:
        return 8;
    case TextureFormat::Rgba8:
        return 4;
    case TextureFormat::Unknown:
        return 0;
    default:
        return 16;
    }
}

const char* formatName(TextureFormat format) {
    switch (format) {
    case TextureFormat::Bc1:
        return "BC1";
    case TextureFormat::Bc2:
        return "BC2";
    case TextureFormat::Bc3:
        return "BC3";
    case TextureFormat::Bc4:
        return "BC4";
    case TextureFormat::Bc5:
        return "BC5";
    case TextureFormat::Bc6h:
        return "BC6H";
    case TextureFormat::Bc7:
        return "BC7";
    case TextureFormat::Rgba8:
        return "RGBA8";
    default:
        return "unknown";
    }
}

std::optional<Texture> loadGnf(ByteView file, std::string* error) {
    auto fail = [&](const char* message) -> std::optional<Texture> {
        if (error) {
            *error = message;
        }
        return std::nullopt;
    };
    if (file.size() < 0x30 || std::memcmp(file.data(), "GNF ", 4) != 0) {
        return fail("not a GNF file");
    }
    std::uint32_t contentsSize = u32(file, 4);
    std::uint8_t count = file[9];
    if (count < 1) {
        return fail("GNF holds no texture");
    }
    Descriptor desc = decode(file.subspan(0x10, 32));
    Texture tex;
    tex.format = toFormat(desc.dataFormat);
    if (tex.format == TextureFormat::Unknown) {
        return fail("unsupported texture format");
    }
    if (desc.tileMode != kTileThin1d && desc.tileMode != kTileThick1d) {
        return fail("unsupported tile mode");
    }
    tex.srgb = desc.numberFormat == kNumberFormatSrgb;
    tex.kind = desc.type == kType3D     ? TextureKind::Tex3D
               : desc.type == kTypeCube ? TextureKind::Cube
                                        : TextureKind::Tex2D;
    tex.width = desc.width;
    tex.height = desc.height;
    tex.depth = tex.kind == TextureKind::Tex3D ? desc.depth : 1;
    tex.layers = tex.kind == TextureKind::Tex3D ? 1 : desc.lastArray + 1;
    std::uint32_t storedSlices = desc.pow2Pad ? nextPow2(tex.layers) : tex.layers;

    const bool blocks = isBlockCompressed(tex.format);
    const std::uint32_t elementBytes = bytesPerElement(tex.format);
    const bool thick = desc.tileMode == kTileThick1d;
    const std::uint32_t tileDepth = thick ? 4 : 1;
    std::size_t offset = 8 + contentsSize;

    for (std::uint32_t level = desc.baseLevel; level <= desc.lastLevel; ++level) {
        TextureLevel out;
        out.width = std::max(1u, tex.width >> level);
        out.height = std::max(1u, tex.height >> level);
        out.depth = std::max(1u, tex.depth >> level);
        std::uint32_t padW = out.width, padH = out.height, padPitch = std::max(1u, desc.pitch >> level);
        if (desc.pow2Pad) {
            padW = nextPow2(padW);
            padH = nextPow2(padH);
            padPitch = nextPow2(padPitch);
        }
        auto elements = [&](std::uint32_t v) {
            return blocks ? (v + 3) / 4 : v;
        };
        std::uint32_t ew = elements(out.width), eh = elements(out.height);
        std::uint32_t pitchElems = (std::max(elements(padPitch), elements(padW)) + 7) / 8 * 8;
        std::uint32_t heightElems = (elements(padH) + 7) / 8 * 8;
        std::uint32_t slices = tex.kind == TextureKind::Tex3D ? out.depth : storedSlices;
        std::uint32_t storedDepth = (slices + tileDepth - 1) / tileDepth * tileDepth;
        std::size_t sliceBytes = std::size_t(pitchElems) * heightElems * elementBytes;
        std::size_t levelBytes = sliceBytes * storedDepth;
        if (offset + levelBytes > file.size()) {
            return fail("texture data truncated");
        }
        std::uint32_t outSlices = tex.kind == TextureKind::Tex3D ? out.depth : tex.layers;
        out.data.resize(std::size_t(ew) * eh * elementBytes * outSlices);
        const std::uint32_t tilesPerRow = pitchElems / 8;
        for (std::uint32_t z = 0; z < outSlices; ++z) {
            for (std::uint32_t y = 0; y < eh; ++y) {
                for (std::uint32_t x = 0; x < ew; ++x) {
                    std::size_t src;
                    if (thick) {
                        std::size_t tile =
                            std::size_t(z / 4) * (tilesPerRow * (heightElems / 8)) + (y / 8) * tilesPerRow + x / 8;
                        src = tile * 256 * elementBytes +
                              std::size_t(microIndexThick(x % 8, y % 8, z % 4)) * elementBytes;
                    } else {
                        std::size_t tile = std::size_t(y / 8) * tilesPerRow + x / 8;
                        src = z * sliceBytes + tile * 64 * elementBytes +
                              std::size_t(microIndexThin(x % 8, y % 8)) * elementBytes;
                    }
                    std::size_t dst = ((std::size_t(z) * eh + y) * ew + x) * elementBytes;
                    std::memcpy(out.data.data() + dst, file.data() + offset + src, elementBytes);
                }
            }
        }
        offset += levelBytes;
        tex.levels.push_back(std::move(out));
    }
    if (desc.dataSize != 0 && offset - (8 + contentsSize) != desc.dataSize) {
        return fail("texture size does not match the descriptor");
    }
    return tex;
}

}   // namespace data
