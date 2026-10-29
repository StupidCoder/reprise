#include "test_assets.hpp"
// Mobiclip regression: decodes original MODS files from a native asset pack
// and compares frame digests recorded from the ROM's own decode/convert
// routines (a local research oracle).
#include "system/resources.hpp"
#include "video/mobiclip.hpp"
#include <cstdio>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
using namespace picarat;

namespace {
void require(bool ok, const std::string& message) {
    if (!ok) {
        throw std::runtime_error(message);
    }
}

std::uint64_t fnv(const void* data, std::size_t size) {
    auto* p = static_cast<const std::uint8_t*>(data);
    std::uint64_t h = 1469598103934665603ull;
    while (size--) {
        h ^= *p++;
        h *= 1099511628211ull;
    }
    return h;
}

struct Expected {
    std::uint64_t luma, chroma, rgb;
};

// FNV-1a 64 of luma, chroma and RGB555 bytes, as reported by the ROM oracle.
const std::map<unsigned, std::map<unsigned, Expected>> expected = {
    {1,
     {{0, {0xc3e50f72a509b78d, 0x2916f9645af86b25, 0x64baa03d6aa67e6a}},
      {1, {0x3187db07c1ab8acb, 0x46879cb22a126a8c, 0x5056cd635321a5df}},
      {100, {0xb30143d4714f1d0c, 0x06266b4d01ee5906, 0x55feb1cb6a30c41d}},
      {683, {0x0f5d3d71e95b5486, 0x6782e7cb8dd3e497, 0xaa0e4f9f7171831c}},
      {3016, {0x757e301d709a6d73, 0xead50c6b07dc292d, 0xb31188472f1b80ef}}}},
    {2,
     {{0, {0x1dcd22baef8d5383, 0x7d7d979c70018383, 0x776103f13dd32383}},
      {250, {0xc8713542bc2b1287, 0x8a1a500a9619600c, 0xd8e4f8483bad3702}},
      {484, {0x1d3752a6f88ccc24, 0x4e52d117f97a1ad6, 0xa2f03304dafeb6bb}}}},
    {7,
     {{0, {0xf748fd0b1ebaaf61, 0x89ed12585eb61b40, 0x7f6bedcacd9f3c8e}},
      {250, {0xd9f3dd932698ed7e, 0x5c77a75c43f62535, 0x412bb144b6877d30}},
      {683, {0x6d7c01168c99c6e3, 0x1ab167c4c22a3ac0, 0x4a88b3e37acd914e}}}},
};
} // namespace

int main(int argc, char** argv) try {
    requireTestPack(argc, argv);
    require(argc == 2, "assets argument");
    const auto tables = mobiclip::Tables::fromOverlay(
        readFile(std::filesystem::path(argv[1]) / "code" / "overlay1.bin"));
    unsigned checked = 0;
    for (const auto& [movie, frames] : expected) {
        auto file = readFile(std::filesystem::path(argv[1]) /
                             ("movie/m" + std::to_string(movie) + ".mods"));
        mobiclip::Container container(file);
        const auto& header = container.header();
        require(header.width == 256 && header.height == 192 && header.rate != 0, "MODS header");
        mobiclip::Decoder decoder(header.width, header.height, tables);
        std::vector<std::uint16_t> rgb(header.width * header.height);
        mobiclip::Packet packet;
        unsigned decoded = 0;
        while (container.next(packet)) {
            auto consumed = decoder.decode(packet.data);
            // Keyframes carry four extra bytes after the video in N3 files (0x0205ad08).
            require(consumed + (packet.keyframe ? 4 : 0) <= packet.data.size() &&
                        consumed + (packet.keyframe ? 4 : 0) + 2 >= packet.data.size(),
                    "consumed bytes, movie " + std::to_string(movie) + " frame " +
                        std::to_string(packet.frame));
            auto found = frames.find(packet.frame);
            if (found != frames.end()) {
                decoder.convert(rgb.data(), header.width);
                bool same = fnv(decoder.luma(), 192 * 256) == found->second.luma &&
                            fnv(decoder.chroma(), 96 * 256) == found->second.chroma &&
                            fnv(rgb.data(), rgb.size() * 2) == found->second.rgb;
                require(same, "frame digest, movie " + std::to_string(movie) + " frame " +
                                  std::to_string(packet.frame));
                ++checked;
            }
            ++decoded;
        }
        require(decoded == header.frames, "frame count");
    }
    std::cout << "Mobiclip: " << checked
              << " ROM-oracle frame digests matched across movies 1, 2 and 7\n";
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
