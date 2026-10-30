#include "beach_volley_amiga/data/assets.hpp"
#include "platform/files.hpp"
#include <stdexcept>

namespace beach_volley::data {
namespace {

constexpr std::uint32_t court_palettes = 0x29190; // 8 x 16 colour words
constexpr int court_planes = 4;

std::vector<std::uint8_t> read_installed(const std::filesystem::path& content, const std::string& name) {
    try {
        return platform::read_file(content / name);
    } catch (const std::exception&) {
        throw std::runtime_error("The imported file " + name + " is missing; import the disk again.");
    }
}

} // namespace

CodeFile::CodeFile(std::vector<std::uint8_t> bytes, std::uint32_t base) : bytes_(std::move(bytes)), base_(base) {}

std::span<const std::uint8_t> CodeFile::at(std::uint32_t address, std::size_t length) const {
    if (address < base_ || address - base_ + length > bytes_.size()) {
        throw std::runtime_error("A code file is shorter than the game expects.");
    }
    return std::span(bytes_).subspan(address - base_, length);
}

std::uint16_t CodeFile::u16(std::uint32_t address) const {
    const auto b = at(address, 2);
    return std::uint16_t(b[0] << 8 | b[1]);
}

std::uint32_t CodeFile::u32(std::uint32_t address) const {
    return std::uint32_t(u16(address)) << 16 | u16(address + 2);
}

Picture planar_picture(std::span<const std::uint8_t> bytes, int width, int height, int planes) {
    const std::size_t row = std::size_t(width) / 8, plane = row * std::size_t(height);
    if (width % 8 != 0 || bytes.size() < plane * std::size_t(planes)) {
        throw std::runtime_error("A picture is shorter than the game expects.");
    }
    Picture picture{width, height, std::vector<std::uint8_t>(std::size_t(width) * std::size_t(height))};
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int index = 0;
            for (int p = 0; p < planes; ++p) {
                const auto byte = bytes[std::size_t(p) * plane + std::size_t(y) * row + std::size_t(x / 8)];
                index |= (byte >> (7 - x % 8) & 1) << p;
            }
            picture.pixels[std::size_t(y) * std::size_t(width) + std::size_t(x)] = std::uint8_t(index);
        }
    }
    return picture;
}

Assets load_assets(const std::filesystem::path& content) {
    Assets assets{CodeFile(read_installed(content, "volley.code"), volley_base), {}};
    for (int n = 1; n <= court_count; ++n) {
        Court court;
        court.number = n;
        court.picture = planar_picture(read_installed(content, "terrain" + std::to_string(n)), court_width,
                                       court_height, court_planes);
        const std::uint32_t palette = court_palettes + std::uint32_t(n - 1) * 32;
        for (int i = 0; i < 16; ++i) court.palette[std::size_t(i)] = assets.volley.u16(palette + std::uint32_t(i) * 2);
        assets.courts.push_back(std::move(court));
    }
    return assets;
}

IntroFiles load_intro_files(const std::filesystem::path& content) {
    IntroFiles files{CodeFile(read_installed(content, "intro.code"), intro_base), read_installed(content, "introp1"),
                     read_installed(content, "trophe"), read_installed(content, "music13"),
                     read_installed(content, "instruments")};
    const auto second = read_installed(content, "introp2");
    files.block.insert(files.block.end(), second.begin(), second.end());
    return files;
}

} // namespace beach_volley::data
