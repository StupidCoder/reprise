#include "stunt_car_racer_amiga/data/cockpit.hpp"
#include <stdexcept>

namespace stuntcar::data {
namespace {

constexpr int screen_width = 320, screen_height = 200, row_bytes = 40, planes = 4;

// The cockpit picture, packed (unpacked by $593F8), and its colours (16
// words, three bits a gun, widened to 2c + 1 as the game loads them).
constexpr std::uint32_t cockpit_picture = 0x201A4;
constexpr std::uint32_t race_colours = 0x20184;
// The sprite sheet: a 320 x 200 picture with the four planes interleaved a
// word at a time (160 bytes a row), stored plainly in the program.
constexpr std::uint32_t sprite_sheet = 0x6A5B6;
// Item records, 16 bytes each: source x (16-pixel columns), source y,
// columns - 1, rows - 1, screen x (columns), screen y.
constexpr std::uint32_t item_records = 0x6A16C;
constexpr int item_count = 0x34;
// Items 0x0A-0x0E are cut from the cockpit picture itself; 0x25-0x30 are
// made into hardware sprites (the tyre treads).
constexpr int first_cockpit_item = 0x0A, last_cockpit_item = 0x0E;
constexpr int first_sprite_item = 0x25, last_sprite_item = 0x30;
// Sheet colour 1 is see-through when items are cut.
constexpr std::uint8_t sheet_clear = 1;
// The tread sprites' colours (17-19), from the table at $69EB4.
constexpr std::uint32_t sprite_colours = 0x69EB4;
// The dashboard font: 8 bytes a character from ' ', the top 7 bits used.
constexpr std::uint32_t font = 0x1FE82;
// Messages ($5E508): groups of a control byte ('!' starts the second line)
// and three characters ('<' is a half space), printed from column 19 of row
// 4 (one pixel further right with each character), the second line on row
// 5; a two-group message is centred on the two rows.
constexpr std::uint32_t messages = 0x5E65C;
constexpr int message_column = 19, message_x_offset = 3, message_first_y = 32, message_second_y = 42;
constexpr int two_group_y = 37, half_space = 4, message_groups_max = 8;

Colour widen3(std::uint16_t word) {
    const auto gun = [](int c) { return std::uint8_t(c ? (c * 2 + 1) * 17 : 0); };
    return {gun(word >> 8 & 7), gun(word >> 4 & 7), gun(word & 7)};
}

Colour colour12(std::uint16_t word) {
    return {std::uint8_t((word >> 8 & 15) * 17), std::uint8_t((word >> 4 & 15) * 17), std::uint8_t((word & 15) * 17)};
}

// PackBits rows: each screen row holds the four planes' 40 bytes in turn.
std::vector<std::uint8_t> unpack_picture(const Program& program, std::uint32_t at) {
    std::vector<std::uint8_t> pixels(std::size_t(screen_width * screen_height));
    std::uint8_t row[row_bytes];
    for (int y = 0; y < screen_height; ++y) {
        for (int p = 0; p < planes; ++p) {
            int filled = 0;
            while (filled < row_bytes) {
                const int code = program.u8(at++);
                if (code < 128) {
                    for (int k = 0; k <= code; ++k, ++at)
                        if (filled < row_bytes) row[filled++] = program.u8(at);
                } else if (code > 128) {
                    const auto value = program.u8(at++);
                    for (int k = 0; k < 257 - code; ++k)
                        if (filled < row_bytes) row[filled++] = value;
                }
            }
            for (int x = 0; x < screen_width; ++x)
                if (row[x >> 3] & (0x80 >> (x & 7))) pixels[std::size_t(y * screen_width + x)] |= std::uint8_t(1 << p);
        }
    }
    return pixels;
}

std::vector<std::uint8_t> read_sheet(const Program& program) {
    std::vector<std::uint8_t> pixels(std::size_t(screen_width * screen_height));
    for (int y = 0; y < screen_height; ++y)
        for (int column = 0; column < screen_width / 16; ++column) {
            const auto at = sprite_sheet + std::uint32_t(y * 160 + column * 8);
            for (int p = 0; p < planes; ++p) {
                const auto word = program.u16(at + std::uint32_t(p) * 2);
                for (int b = 0; b < 16; ++b)
                    if (word & (0x8000 >> b)) pixels[std::size_t(y * screen_width + column * 16 + b)] |= std::uint8_t(1 << p);
            }
        }
    return pixels;
}

} // namespace

Message decode_message(const Program& program, int start, int groups) {
    if (start < 0 || groups < 1 || groups > message_groups_max) throw std::runtime_error("A message is out of range.");
    Message message;
    int column = message_column, offset = message_x_offset;
    int y = groups == 2 ? two_group_y : message_first_y;
    auto at = messages + std::uint32_t(start);
    for (int g = 0; g < groups; ++g) {
        if (program.u8(at) == '!') {
            column = message_column, offset = message_x_offset;
            y = message_second_y;
        }
        ++at;
        for (int k = 0; k < 3; ++k, ++at) {
            const char c = char(program.u8(at));
            if (c == '<') {
                offset += half_space;
                continue;
            }
            message.glyphs.push_back({c, column * 7 + offset, y});
            ++column, ++offset;
        }
    }
    return message;
}

CockpitArt load_cockpit_art(const Program& program) {
    CockpitArt art;
    art.width = screen_width;
    art.height = screen_height;
    art.picture = unpack_picture(program, cockpit_picture);
    for (int i = 0; i < 16; ++i) art.palette[std::size_t(i)] = widen3(program.u16(race_colours + std::uint32_t(i) * 2));
    for (int i = 0; i < 8; ++i)
        art.palette[std::size_t(16 + i)] = colour12(program.u16(sprite_colours + std::uint32_t(i) * 2));

    const auto sheet = read_sheet(program);
    for (int i = 0; i < item_count; ++i) {
        const auto record = item_records + std::uint32_t(i) * 16;
        const auto field = [&](int k) { return int(program.u16(record + std::uint32_t(k) * 2)); };
        const int source_x = field(0) * 16, source_y = field(1);
        Overlay item;
        item.width = (field(2) + 1) * 16;
        item.height = field(3) + 1;
        item.x = field(4) * 16;
        item.y = field(5);
        const bool from_cockpit = i >= first_cockpit_item && i <= last_cockpit_item;
        const bool sprite = i >= first_sprite_item && i <= last_sprite_item;
        const auto& source = from_cockpit ? art.picture : sheet;
        if (source_x + item.width > screen_width || source_y + item.height > screen_height)
            throw std::runtime_error("A race screen item lies outside its picture.");
        item.pixels.resize(std::size_t(item.width * item.height), Overlay::clear);
        for (int y = 0; y < item.height; ++y)
            for (int x = 0; x < item.width; ++x) {
                const auto c = source[std::size_t((source_y + y) * screen_width + source_x + x)];
                auto& out = item.pixels[std::size_t(y * item.width + x)];
                if (sprite) {
                    // Sprite planes: plane 0 inverted, then plane 2.
                    const int value = ((c & 1) ? 0 : 1) | ((c & 4) ? 2 : 0);
                    if (value) out = std::uint8_t(16 + value);
                } else if (c != sheet_clear) {
                    out = c;
                }
            }
        art.items.push_back(std::move(item));
    }

    for (std::size_t c = 0; c < art.font.size(); ++c)
        for (std::size_t row = 0; row < 8; ++row) art.font[c][row] = program.u8(font + std::uint32_t(c * 8 + row));
    return art;
}

} // namespace stuntcar::data
