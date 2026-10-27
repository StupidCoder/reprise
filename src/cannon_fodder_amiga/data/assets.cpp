#include "cannon_fodder_amiga/data/assets.hpp"
#include "amiga/adf.hpp"
#include "platform/files.hpp"
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace fodder::data {
namespace fs = std::filesystem;

std::optional<fs::path> Content::find(std::string_view name, int disk) const {
    std::error_code error;
    const auto try_disk = [&](int n) -> std::optional<fs::path> {
        auto path = folder_ / ("disk" + std::to_string(n)) / std::string(name);
        if (fs::exists(path, error)) {
            return path;
        }
        return std::nullopt;
    };
    if (auto path = try_disk(disk)) {
        return path;
    }
    for (int n = 1; n <= 3; ++n) {
        if (n != disk) {
            if (auto path = try_disk(n)) {
                return path;
            }
        }
    }
    return std::nullopt;
}

std::vector<std::uint8_t> Content::read(std::string_view name, int disk) const {
    const auto path = find(name, disk);
    if (!path) {
        throw std::runtime_error("The imported files lack " + std::string(name) + ".");
    }
    return platform::read_file(*path);
}

std::vector<std::string> Content::names(int disk) const {
    std::vector<std::string> result;
    std::error_code error;
    for (const auto& entry : fs::directory_iterator(folder_ / ("disk" + std::to_string(disk)), error)) {
        result.push_back(entry.path().filename().string());
    }
    std::sort(result.begin(), result.end());
    return result;
}

amiga::IlbmPicture read_raw_screen(std::span<const std::uint8_t> file) {
    constexpr int width = 320, row_bytes = width / 8;
    for (const int planes : {5, 4}) {
        const std::size_t colours = std::size_t(1) << planes;
        if (file.size() <= colours * 2 || (file.size() - colours * 2) % std::size_t(row_bytes * planes) != 0) {
            continue;
        }
        const int height = int((file.size() - colours * 2) / std::size_t(row_bytes * planes));
        if (height < 200 || height > 290) {
            continue;
        }
        amiga::IlbmPicture picture;
        picture.width = width;
        picture.height = height;
        picture.planes = planes;
        for (std::size_t i = 0; i < colours; ++i) {
            picture.palette.push_back(amiga::read_be16(file, 2 * i));
        }
        picture.pixels.assign(std::size_t(width * height), 0);
        const auto bitmap = file.subspan(colours * 2);
        for (int plane = 0; plane < planes; ++plane) {
            for (int y = 0; y < height; ++y) {
                const auto row = bitmap.subspan(std::size_t((plane * height + y) * row_bytes), std::size_t(row_bytes));
                for (int x = 0; x < width; ++x) {
                    if (row[std::size_t(x >> 3)] & (0x80 >> (x & 7))) {
                        picture.pixels[std::size_t(y * width + x)] |= std::uint8_t(1 << plane);
                    }
                }
            }
        }
        return picture;
    }
    throw std::runtime_error("A screen file has an unexpected size.");
}

Palette read_palette(std::span<const std::uint8_t> file) {
    Palette palette;
    for (std::size_t at = 0; at + 1 < file.size(); at += 2) {
        palette.push_back(amiga::read_be16(file, at) & 0x0fff);
    }
    return palette;
}

std::vector<Tile> read_tile_set(std::span<const std::uint8_t> file) {
    constexpr std::size_t tile_bytes = 128;
    std::vector<Tile> tiles(file.size() / tile_bytes);
    for (std::size_t t = 0; t < tiles.size(); ++t) {
        const auto data = file.subspan(t * tile_bytes, tile_bytes);
        auto& pixels = tiles[t].pixels;
        for (int plane = 0; plane < 4; ++plane) {
            for (int y = 0; y < 16; ++y) {
                const auto row = amiga::read_be16(data, std::size_t((plane * 16 + y) * 2));
                for (int x = 0; x < 16; ++x) {
                    if (row & (0x8000 >> x)) {
                        pixels[std::size_t(y * 16 + x)] |= std::uint8_t(1 << plane);
                    }
                }
            }
        }
    }
    return tiles;
}

MissionMap read_map(std::span<const std::uint8_t> file) {
    constexpr std::size_t header = 0x60;
    if (file.size() < header || file[0x50] != 'c' || file[0x51] != 'f' || file[0x52] != 'e' || file[0x53] != 'd') {
        throw std::runtime_error("A map file is not in the expected format.");
    }
    const auto name_at = [&](std::size_t at) {
        std::string name;
        for (std::size_t i = at; i < at + 16 && file[i] != 0; ++i) {
            name += char(std::tolower(file[i]));
        }
        return name;
    };
    MissionMap map;
    map.base_set = name_at(0);
    map.sub_set = name_at(0x10);
    map.width = amiga::read_be16(file, 0x54);
    map.height = amiga::read_be16(file, 0x56);
    const auto cells = std::size_t(map.width * map.height);
    if (map.width == 0 || map.height == 0 || file.size() != header + cells * 2) {
        throw std::runtime_error("A map file has an unexpected size.");
    }
    for (std::size_t i = 0; i < cells; ++i) {
        map.cells.push_back(amiga::read_be16(file, header + 2 * i));
    }
    return map;
}

std::vector<Placement> read_placements(std::span<const std::uint8_t> file) {
    std::vector<Placement> result(file.size() / 10);
    for (std::size_t i = 0; i < result.size(); ++i) {
        for (std::size_t w = 0; w < 5; ++w) {
            result[i].words[w] = amiga::read_be16(file, i * 10 + w * 2);
        }
    }
    return result;
}

int map_disk(int number) {
    return number <= 36 ? 2 : 3;
}

} // namespace fodder::data
