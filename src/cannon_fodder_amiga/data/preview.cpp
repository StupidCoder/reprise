#include "cannon_fodder_amiga/data/preview.hpp"
#include "platform/files.hpp"
#include <algorithm>
#include <array>
#include <string>

namespace fodder::data {
namespace {

constexpr platform::Color backdrop{40, 40, 56, 255};
constexpr platform::Color missing{255, 0, 255, 255};

platform::Color colour_of(const Palette& palette, int index) {
    return std::size_t(index) < palette.size() ? to_color(palette[std::size_t(index)]) : missing;
}

// The five landscapes, by the prefix of their files ("jun" → junbase.blk,
// junsub0.blk, junarmy.lbm, …), in the order the program's name table
// lists them ("jundesicemorint").
struct Landscape {
    const char* prefix;
    const char* name;
    int disk;
    std::array<const char*, 2> sub_sets;
};
constexpr std::array landscapes = {
    Landscape{"jun", "jungle", 2, {"junsub0.blk", "junsub1.blk"}},
    Landscape{"des", "desert", 2, {"dessub0.blk", nullptr}},
    Landscape{"ice", "ice", 2, {"icesub0.blk", nullptr}},
    Landscape{"mor", "moor", 3, {"morsub0.blk", nullptr}},
    Landscape{"int", "interior", 3, {"intsub0.blk", nullptr}},
};

} // namespace

platform::Color to_color(amiga::Rgb4 colour) {
    return {std::uint8_t((colour >> 8 & 15) * 17), std::uint8_t((colour >> 4 & 15) * 17), std::uint8_t((colour & 15) * 17),
            255};
}

platform::Image picture_image(const amiga::IlbmPicture& picture) {
    platform::Image image(picture.width, picture.height);
    for (int y = 0; y < picture.height; ++y) {
        for (int x = 0; x < picture.width; ++x) {
            image.put(x, y, colour_of(picture.palette, picture.pixels[std::size_t(y * picture.width + x)]));
        }
    }
    return image;
}

platform::Image palette_image(const Palette& palette) {
    constexpr int size = 12, per_row = 16;
    const int rows = std::max(1, (int(palette.size()) + per_row - 1) / per_row);
    platform::Image image(per_row * size, rows * size);
    std::fill(image.pixels.begin(), image.pixels.end(), backdrop);
    for (std::size_t i = 0; i < palette.size(); ++i) {
        const int x0 = int(i % per_row) * size, y0 = int(i / per_row) * size;
        for (int y = 1; y < size - 1; ++y) {
            for (int x = 1; x < size - 1; ++x) {
                image.put(x0 + x, y0 + y, to_color(palette[i]));
            }
        }
    }
    return image;
}

platform::Image tile_sheet(const std::vector<Tile>& tiles, const Palette& palette) {
    constexpr int per_row = 20;
    const int rows = std::max(1, (int(tiles.size()) + per_row - 1) / per_row);
    platform::Image image(per_row * 16, rows * 16);
    std::fill(image.pixels.begin(), image.pixels.end(), backdrop);
    for (std::size_t t = 0; t < tiles.size(); ++t) {
        const int x0 = int(t % per_row) * 16, y0 = int(t / per_row) * 16;
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                image.put(x0 + x, y0 + y, colour_of(palette, tiles[t].pixels[std::size_t(y * 16 + x)]));
            }
        }
    }
    return image;
}

platform::Image map_image(const Content& content, int number) {
    const int disk = map_disk(number);
    const auto map = read_map(content.read("mapm" + std::to_string(number) + ".map", disk));
    auto tiles = read_tile_set(content.read(map.base_set, disk));
    const auto sub = read_tile_set(content.read(map.sub_set, disk));
    tiles.insert(tiles.end(), sub.begin(), sub.end());
    auto palette_name = map.base_set;
    palette_name.replace(palette_name.size() - 3, 3, "pal");
    const auto palette = read_palette(content.read(palette_name, disk));
    platform::Image image(map.width * 16, map.height * 16);
    for (int ty = 0; ty < map.height; ++ty) {
        for (int tx = 0; tx < map.width; ++tx) {
            const auto index = std::size_t(map.tile(tx, ty));
            for (int y = 0; y < 16; ++y) {
                for (int x = 0; x < 16; ++x) {
                    image.put(tx * 16 + x, ty * 16 + y,
                              index < tiles.size() ? colour_of(palette, tiles[index].pixels[std::size_t(y * 16 + x)])
                                                   : missing);
                }
            }
        }
    }
    return image;
}

platform::Image fit(const platform::Image& image, int width, int height) {
    const int factor = std::max((image.width + width - 1) / width, (image.height + height - 1) / height);
    if (factor <= 1) {
        return image;
    }
    platform::Image out(image.width / factor, image.height / factor);
    for (int y = 0; y < out.height; ++y) {
        for (int x = 0; x < out.width; ++x) {
            int r = 0, g = 0, b = 0;
            for (int dy = 0; dy < factor; ++dy) {
                for (int dx = 0; dx < factor; ++dx) {
                    const auto& c = image.pixels[std::size_t((y * factor + dy) * image.width + x * factor + dx)];
                    r += c.r; g += c.g; b += c.b;
                }
            }
            const int n = factor * factor;
            out.put(x, y, {std::uint8_t(r / n), std::uint8_t(g / n), std::uint8_t(b / n), 255});
        }
    }
    return out;
}

std::vector<app::Preview> previews(const std::filesystem::path& folder, bool full_size_maps) {
    const Content content(folder);
    std::vector<app::Preview> result;
    const auto add_picture = [&](const std::string& title, const amiga::IlbmPicture& picture) {
        result.push_back({title, picture_image(picture)});
    };

    // The front end's screens (disk 1), in the order the program names them.
    for (const char* name : {"cftitle.raw", "sensprod.raw", "virgpres.raw", "1.raw", "2.raw", "3.raw", "4.raw",
                             "5.raw", "6.raw", "7.raw", "8.raw", "won.raw", "disk2.raw", "font.raw"}) {
        add_picture(std::string("Screen: ") + name, read_raw_screen(content.read(name, 1)));
    }
    for (const char* name : {"pstuff.lbm", "rankfont.lbm"}) {
        add_picture(std::string("Picture: ") + name, amiga::read_ilbm(content.read(name, 1)));
    }
    for (const char* name : {"hills.lbm", "morphbig.lbm", "startup.lbm"}) {
        add_picture(std::string("Picture: ") + name, amiga::read_ilbm(content.read(name, 2)));
    }

    // Per landscape: sprite sheets, play-area backdrops, tile sets, colours.
    for (const auto& land : landscapes) {
        const std::string p = land.prefix;
        for (const char* kind : {"army", "copt", "play"}) {
            add_picture("Sprites: " + p + kind + ".lbm (" + land.name + ")",
                        amiga::read_ilbm(content.read(p + kind + ".lbm", land.disk)));
        }
        const auto palette = read_palette(content.read(p + "base.pal", land.disk));
        result.push_back({"Tiles: " + p + "base.blk (" + land.name + ")",
                          tile_sheet(read_tile_set(content.read(p + "base.blk", land.disk)), palette)});
        for (const char* sub : land.sub_sets) {
            if (sub != nullptr) {
                result.push_back({std::string("Tiles: ") + sub + " (" + land.name + ")",
                                  tile_sheet(read_tile_set(content.read(sub, land.disk)), palette)});
            }
        }
        Palette colours = palette;
        for (const char* suffix : {"heli.pal", "sky.pl8", "mid.pl8", "fgnd.pl8", "fgn2.pl8"}) {
            const auto more = read_palette(content.read(p + suffix, land.disk));
            colours.insert(colours.end(), more.begin(), more.end());
        }
        result.push_back({"Colours: " + p + "base, heli, sky, mid, fgnd, fgn2 (" + land.name + ")",
                          palette_image(colours)});
    }

    for (int number = 1; number <= 72; ++number) {
        auto image = map_image(content, number);
        const auto title = "Map: mission map " + std::to_string(number) + " (" + std::to_string(image.width / 16) +
                           " x " + std::to_string(image.height / 16) + " tiles)";
        result.push_back({title, full_size_maps ? std::move(image) : fit(image, 604, 358)});
    }
    return result;
}

} // namespace fodder::data
