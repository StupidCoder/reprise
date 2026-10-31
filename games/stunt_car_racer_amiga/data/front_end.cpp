#include "stunt_car_racer_amiga/data/front_end.hpp"
#include "stunt_car_racer_amiga/data/planar.hpp"
#include <stdexcept>

namespace stuntcar::data {
namespace {

// Pictures: a flag word (bit 15: packed), 16 colour words, the planes.
constexpr std::uint32_t menu_picture = 0x23C30;
// These two are 16 colour words followed by packed planes ($5D228, $5FC4E).
constexpr std::uint32_t stadium_picture = 0x2B974;
constexpr std::uint32_t hall_picture = 0x30BD0;
// The portrait sheet: its colours, then its planes ($58888).
constexpr std::uint32_t portrait_colours = 0x35A76, portrait_sheet = 0x35A96;
// The picture table: five pointers (0 is the loader's screen).
constexpr std::uint32_t picture_table = 0x56FF8;
// The name font ($591C6): character c - ' ' is six pixels of the menu
// picture's first plane, 32 to a row of glyphs from x 32, y 87, rows of
// glyphs 24 pixels apart, eight pixels high.
constexpr int name_font_x = 32, name_font_y = 87, name_font_width = 6, name_font_rows_apart = 24;
constexpr std::uint32_t driver_names = 0x1ECAA, track_names = 0x1EDAA;
constexpr std::uint32_t portrait_numbers = 0x58B20, portrait_offsets = 0x58AA4;
constexpr std::uint32_t strip_offsets = 0x64C1A, bar_row_table = 0x64AF4;
// The strips are found from the menu picture's planes less $500 bytes
// ($64B54); the planes start $22 bytes into the picture.
constexpr std::uint32_t strip_base = menu_picture + 0x22 - 0x500;
constexpr int name_length = 12, track_name_length = 15;
constexpr std::uint8_t end_of_text = 0xFF;

Picture picture(const Program& program, std::uint32_t at) {
    Picture p;
    p.width = screen_width;
    p.height = screen_height;
    p.palette = read_palette(program, at + 2);
    p.pixels = (program.u16(at) & 0x8000) ? unpack_rows(program, at + 0x22) : read_interleaved(program, at + 0x22);
    return p;
}

Picture packed_picture(const Program& program, std::uint32_t at) {
    Picture p;
    p.width = screen_width;
    p.height = screen_height;
    p.palette = read_palette(program, at);
    p.pixels = unpack_rows(program, at + 0x20);
    return p;
}

} // namespace

FrontEndArt load_front_end_art(const Program& program) {
    FrontEndArt art;
    art.menu = picture(program, menu_picture);
    art.stadium = packed_picture(program, stadium_picture);
    art.hall_of_fame = packed_picture(program, hall_picture);
    art.portraits.width = screen_width;
    art.portraits.height = screen_height;
    art.portraits.pixels = read_interleaved(program, portrait_sheet);
    art.portraits.palette = read_palette(program, portrait_colours);
    for (std::size_t k = 0; k < art.scenes.size(); ++k)
        art.scenes[k] = picture(program, program.u32(picture_table + std::uint32_t(k + 1) * 4));
    art.font = read_font(program);
    for (std::size_t c = 0; c < art.name_font.size(); ++c) {
        const int x0 = name_font_x + int(c % 32) * name_font_width;
        const int y0 = name_font_y + int(c / 32) * name_font_rows_apart;
        for (int row = 0; row < 8; ++row) {
            std::uint8_t bits = 0;
            for (int x = 0; x < name_font_width; ++x)
                if (art.menu.pixels[std::size_t((y0 + row) * screen_width + x0 + x)] & 1) bits |= std::uint8_t(0x80 >> x);
            art.name_font[c][std::size_t(row)] = bits;
        }
    }
    for (std::size_t d = 0; d < art.portrait_of_driver.size(); ++d)
        art.portrait_of_driver[d] = program.u8(portrait_numbers + std::uint32_t(d));
    // Sheet offsets are in the interleaved picture (160 bytes a row, eight
    // bytes per 16 pixels); screen offsets in one plane (40 bytes a row).
    for (std::size_t k = 0; k < art.portrait_in_sheet.size(); ++k) {
        const auto v = int(program.u32(portrait_offsets + std::uint32_t(k) * 4));
        art.portrait_in_sheet[k] = {v % 160 / 8 * 16, v / 160};
    }
    for (std::size_t k = 0; k < art.portrait_places.size(); ++k) {
        const auto v = int(program.u32(portrait_offsets + std::uint32_t(12 + k) * 4));
        art.portrait_places[k] = {v % 40 * 8, v / 40};
    }
    for (std::size_t k = 0; k < art.strip_rows.size(); ++k)
        art.strip_rows[k] = int(strip_base + program.u32(strip_offsets + std::uint32_t(k) * 4) - (menu_picture + 0x22)) / 160;
    for (std::size_t k = 0; k < art.bar_rows.size(); ++k) art.bar_rows[k] = program.u8(bar_row_table + std::uint32_t(k));
    return art;
}

Text text(const Program& program, Texts table, int index) {
    Text t;
    for (auto at = std::uint32_t(table) + std::uint32_t(index);; ++at) {
        const auto c = program.u8(at);
        if (c == end_of_text) return t;
        t.push_back(char(c));
        if (t.size() > 200) throw std::runtime_error("A front-end text has no end.");
    }
}

std::string driver_name(const Program& program, int driver) {
    std::string s;
    for (int k = 0; k < name_length; ++k) s.push_back(char(program.u8(driver_names + std::uint32_t(driver * 16 + 1 + k))));
    return s;
}

std::string track_name(const Program& program, int track) {
    std::string s;
    for (int k = 0; k < track_name_length; ++k) s.push_back(char(program.u8(track_names + std::uint32_t(track * 16 + k))));
    return s;
}

} // namespace stuntcar::data
