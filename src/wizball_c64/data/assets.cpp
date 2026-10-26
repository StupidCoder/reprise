#include "wizball_c64/data/assets.hpp"
#include "platform/files.hpp"
#include <algorithm>
#include <stdexcept>

namespace wizball::data {
namespace {

constexpr std::uint16_t file_start = 0x0400;
constexpr std::size_t file_size = 0xF901;

Glyph glyph_at(const GameImage& image, std::uint16_t address, bool inverted = false) {
    Glyph glyph{};
    const auto bytes = image.bytes(address, 8);
    for (std::size_t row = 0; row < 8; ++row) {
        glyph[row] = inverted ? std::uint8_t(~bytes[row]) : bytes[row];
    }
    return glyph;
}

} // namespace

GameImage::GameImage(c64::Program game) : game_(std::move(game)) {}

std::uint8_t GameImage::byte(std::uint16_t address) const {
    return bytes(address, 1)[0];
}

std::span<const std::uint8_t> GameImage::bytes(std::uint16_t address, std::size_t count) const {
    if (address < game_.load_address || address - game_.load_address + count > game_.bytes.size()) {
        throw std::runtime_error("Wizball data read outside the game file.");
    }
    return std::span(game_.bytes).subspan(address - game_.load_address, count);
}

Assets decode_assets(const GameImage& image) {
    Assets a;

    // The map rows through the row pointer tables ($78E4).
    int highest_tile = 0;
    for (int row = 0; row < LevelMap::rows; ++row) {
        const auto start = std::uint16_t(image.byte(std::uint16_t(0x0460 + row)) |
                                         image.byte(std::uint16_t(0x0470 + row)) << 8);
        const auto bytes = image.bytes(start, LevelMap::columns);
        std::copy(bytes.begin(), bytes.end(), a.map.tiles[std::size_t(row)].begin());
        highest_tile = std::max(highest_tile, int(*std::max_element(bytes.begin(), bytes.end())));
    }
    // The tile table's base is the word at $B62B ($78F6).
    const auto tile_base = image.word(0xB62B);
    for (int t = 0; t <= highest_tile; ++t) {
        Tile tile{};
        const auto bytes = image.bytes(std::uint16_t(tile_base + 25 * t), 25);
        std::copy(bytes.begin(), bytes.end(), tile.begin());
        a.tiles.push_back(tile);
    }

    // $8C96 flips $F000-$F1FF and $8CC7 $F200-$F7FF; each part's flag byte
    // ($F100, $F6F0) is non-zero while that part is inverted. The file
    // holds the first part inverted and the second as it is shown.
    const bool letters_inverted = image.byte(0xF100) != 0, logo_inverted = image.byte(0xF6F0) != 0;
    for (int c = 0; c < 256; ++c) {
        if (c < 0xA0) {
            a.play_charset[std::size_t(c)] = glyph_at(image, std::uint16_t(0xF800 + 8 * c));
        }
        a.front_charset[std::size_t(c)] =
            glyph_at(image, std::uint16_t(0xF000 + 8 * c), c < 0x40 ? letters_inverted : logo_inverted);
    }

    // $7630 (placement), $77C6 (characters), $7799 (painted colours).
    constexpr int place_count = 11;
    for (int p = 0; p < place_count; ++p) {
        Place place;
        const auto entry = image.bytes(std::uint16_t(0xB2F0 + 4 * p), 4);
        place.first_column = (entry[0] + 1) & 0xFF;
        place.first_row = entry[1];
        place.extra = {entry[2], entry[3]};
        place.characters_address = image.word(std::uint16_t(0xB31C + 2 * p));
        for (int c = 0; c < 95; ++c) {
            place.characters[std::size_t(c)] = glyph_at(image, std::uint16_t(place.characters_address + 8 * c));
        }
        if (p < Assets::level_count) {
            const auto colours = image.bytes(std::uint16_t(0xB1D1 + 4 * p), 4);
            place.painted = {colours[0], colours[1], std::uint8_t(colours[2] | 8)};
            place.painted_extra = colours[3];
        }
        a.places.push_back(place);
    }
    // $778A: LDA #$0B / #$0C / #$09; the multicolour bit is ORed in at $77BB.
    a.grey = {0x0B, 0x0C, 0x09};

    for (int s = 0; s < 128; ++s) {
        Sprite sprite;
        const auto bytes = image.bytes(std::uint16_t(0xC000 + 64 * s), 64);
        std::copy(bytes.begin(), bytes.begin() + 63, sprite.rows.begin());
        sprite.extra = bytes[63];
        a.sprites.push_back(sprite);
    }
    return a;
}

GameImage load_game_image(const std::filesystem::path& content) {
    const auto file = content / "wizball.prg";
    auto program = c64::Program::from_prg(platform::read_file(file));
    if (program.load_address != file_start || program.bytes.size() != file_size) {
        throw std::runtime_error("The installed file wizball.prg is not the expected one; please import the disk again.");
    }
    return GameImage(std::move(program));
}

Assets load_assets(const std::filesystem::path& content) {
    return decode_assets(load_game_image(content));
}

} // namespace wizball::data
