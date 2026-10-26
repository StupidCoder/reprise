// Exports Bruce Lee's decoded data from an installed content folder: PNG
// pictures for people, and raw dumps laid out like the memory the game
// builds, for comparison with the game's own code running in an emulator
// (.analysis/bruce_lee_c64).
//
// Usage: bruce_lee_assets <content folder> <output folder>

#include "bruce_lee_c64/data/assets.hpp"
#include "bruce_lee_c64/data/preview.hpp"
#include "platform/files.hpp"
#include "platform/host.hpp"
#include <cstdio>
#include <exception>
#include <string>
#include <vector>

namespace {
using namespace bruce_lee::data;

// The four charsets as at $A000-$BFFF after the init ($487F).
std::vector<std::uint8_t> charset_dump(const Assets& a) {
    std::vector<std::uint8_t> out;
    for (const auto& set : a.charsets) {
        for (const auto& glyph : set) {
            out.insert(out.end(), glyph.begin(), glyph.end());
        }
    }
    return out;
}

// A room as the screen holds it from row 3: 22 rows of 40 character codes
// (cell & $7F above, | $80 below), then 22 rows of colour RAM values.
std::vector<std::uint8_t> room_dump(const Room& room) {
    std::vector<std::uint8_t> screen, colour;
    for (int row = 0; row < Room::rows; ++row) {
        for (int half = 0; half < 2; ++half) {
            for (int column = 0; column < Room::columns; ++column) {
                const auto cell = room.at(column, row);
                screen.push_back(std::uint8_t((cell & 0x7F) | (half ? 0x80 : 0)));
                colour.push_back(cell & 0x80 ? 0xF8 : room.colour);
            }
        }
    }
    screen.insert(screen.end(), colour.begin(), colour.end());
    return screen;
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: bruce_lee_assets <content folder> <output folder>\n");
        return 2;
    }
    try {
        const auto assets = load_assets(platform::utf8_path(argv[1]));
        const auto out = platform::utf8_path(argv[2]);
        std::filesystem::create_directories(out);
        platform::write_file(out / "charsets.bin", charset_dump(assets));
        platform::save_png(loading_image(assets), out / "loading.png");
        std::vector<std::uint8_t> text; // as at $8800-$8BFF
        for (const auto& glyph : assets.text) text.insert(text.end(), glyph.begin(), glyph.end());
        platform::write_file(out / "text.bin", text);
        platform::save_png(text_sheet(assets), out / "text.png");
        for (int set = 0; set < int(assets.charsets.size()); ++set) {
            platform::save_png(charset_sheet(assets, set), out / ("charset" + std::to_string(set) + ".png"));
        }
        for (int n = 0; n < int(assets.rooms.size()); ++n) {
            char name[32];
            std::snprintf(name, sizeof name, "room%02d", n);
            platform::write_file(out / (std::string(name) + ".bin"), room_dump(assets.rooms[std::size_t(n)]));
            platform::save_png(room_image(assets, n), out / (std::string(name) + ".png"));
            const auto& r = assets.rooms[std::size_t(n)];
            std::printf("room %2d: map $%04X, header $%04X, charset %d, colour $%02X, spill %d\n", n, r.map_address,
                        r.header_address, r.charset, r.colour, r.spill);
        }
        std::printf("Exported to %s\n", out.string().c_str());
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "bruce_lee_assets: %s\n", e.what());
        return 1;
    }
}
