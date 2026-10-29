#include "tower_of_babel_amiga/front/screen.hpp"
#include "tower_of_babel_amiga/data/preview.hpp"
#include "tower_of_babel_amiga/view/sky.hpp"
#include "tower_of_babel_amiga/view/tower_view.hpp"
#include <algorithm>
#include <utility>

namespace babel::front {
namespace {

constexpr std::uint32_t font_at = 0x1C050;      // 256 glyphs of 9 bytes: width, 8 rows (title data +$3E50)
constexpr std::uint32_t messages = var(0x7138); // word offsets of the numbered texts from $670A(a6)
constexpr std::uint32_t message_base = var(0x670A);
constexpr int digit_glyph = 0x8A;               // the font's digits: $8A + digit
constexpr int radio_off = 0x83, radio_on = 0x84;

} // namespace

Screen::Screen(const data::Assets& assets) : assets_(assets), font_(assets.title_memory) {
    pens_ = view::read_pens(assets.program).object;
}

void Screen::invert_blink_pens() {
    // $AB44: the 32 bytes of pens 23 and 24 inverted: every colour index c becomes 15 - c.
    for (int n = 23; n <= 24; ++n)
        for (auto& row : pens_[std::size_t(n)].rows)
            for (auto& c : row) c = std::uint8_t(15 - c);
}

void Screen::clear() {
    std::fill(canvas_.pixels.begin(), canvas_.pixels.end(), std::uint8_t(0));
}

void Screen::fill(int x0, int y0, int x1, int y1, int pen_index) {
    const auto& p = pen(pen_index);
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) put(x, y, p.colour(x, y));
}

void Screen::plot(int x, int y, int pen_index) {
    put(x, y, pen(pen_index).colour(x, 0));
}

void Screen::line(int x0, int y0, int x1, int y1, int pen_index) {
    // $14DE6: from the left end; the longer axis steps every point, the
    // other when the error (starting at half the long side) runs out. Each
    // point takes the pen's first row at its x.
    if (x1 < x0) {
        std::swap(x0, x1);
        std::swap(y0, y1);
    }
    const int dx = x1 - x0, step_y = y1 >= y0 ? 1 : -1, dy = (y1 - y0) * step_y;
    int x = x0, y = y0;
    if (dx >= dy) {
        int error = dx >> 1;
        for (int i = 0; i <= dx; ++i) {
            plot(x, y, pen_index);
            ++x;
            error -= dy;
            if (error < 0) {
                error += dx;
                y += step_y;
            }
        }
    } else {
        int error = dy >> 1;
        for (int i = 0; i <= dy; ++i) {
            plot(x, y, pen_index);
            y += step_y;
            error -= dx;
            if (error < 0) {
                error += dy;
                ++x;
            }
        }
    }
}

void Screen::frame(int x0, int y0, int x1, int y1, int pen_index) {
    line(x0, y0, x0, y1, pen_index);
    line(x1, y0, x1, y1, pen_index);
    line(x0, y0, x1, y0, pen_index);
    line(x0, y1, x1, y1, pen_index);
}

void Screen::bevel_box(int x0, int y0, int x1, int y1, int pen_index) {
    // $11AA6: the inside in the pen, a corner of shading in the two pens
    // after it, the frame in pen 1 and a short top line in pen 2.
    fill(x0 + 2, y0 + 2, x1 - 2, y1 - 2, pen_index);
    plot(x1 - 4, y0 + 2, pen_index + 1);
    plot(x1 - 3, y0 + 2, pen_index + 1);
    plot(x1 - 2, y0 + 3, pen_index + 1);
    plot(x1 - 2, y0 + 2, pen_index + 2);
    frame(x0, y0, x1, y1, 1);
    line(x1 - 2, y0, x1, y0, 2);
}

void Screen::invert(int x0, int y0, int x1, int y1, int pen_index) {
    const auto& p = pen(pen_index);
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            if (x >= 0 && x < width && y >= 0 && y < height) canvas_.set(x, y, std::uint8_t(canvas_.at(x, y) ^ p.colour(x, y)));
}

void Screen::open_window(int x0, int y0, int x1, int y1, int fill_pen) {
    // $11BAC keeps whole 16-pixel columns from x0's to the one before
    // x1 rounded up (a box ending on a column's first pixel leaves it out).
    SavedBox box{x0 & ~15, y0, (x1 + 15) & ~15, y1, {}};
    for (int y = box.y0; y <= box.y1; ++y)
        for (int x = box.x0; x < box.x1; ++x) box.pixels.push_back(y >= 0 && y < height && x < width ? canvas_.at(x, y) : 0);
    saved_.push_back(std::move(box));
    fill(x0, y0, x1, y1, fill_pen);
    frame(x0, y0, x1, y1, 15);
    line(x0, y0 + 11, x1, y0 + 11, 15);
}

void Screen::close_window() {
    if (saved_.empty()) return;
    const auto box = std::move(saved_.back());
    saved_.pop_back();
    std::size_t i = 0;
    for (int y = box.y0; y <= box.y1; ++y)
        for (int x = box.x0; x < box.x1; ++x) put(x, y, box.pixels[i++]);
}

void Screen::title_bar() {
    fill(0, 0, 319, 11, 0);
    frame(0, 0, 319, 11, 15);
}

void Screen::picture(const data::Memory& memory, std::uint32_t address, int x, int y, int width_words, int rows) {
    x &= ~15;
    for (int r = 0; r < rows; ++r)
        for (int w = 0; w < width_words; ++w) {
            std::array<unsigned, 4> planes{};
            for (int p = 0; p < 4; ++p) planes[std::size_t(p)] = memory.u16(address + std::uint32_t(((r * width_words + w) * 4 + p) * 2));
            for (int b = 0; b < 16; ++b) {
                int c = 0;
                for (int p = 0; p < 4; ++p) c |= int(planes[std::size_t(p)] >> (15 - b) & 1) << p;
                put(x + w * 16 + b, y + r, std::uint8_t(c));
            }
        }
}

void Screen::icon16(const data::Memory& memory, std::uint32_t address, int x, int y) {
    for (int r = 0; r < 16 && y + r < height; ++r) {
        const std::uint32_t row = address + std::uint32_t(r) * 10;
        const unsigned keep = memory.u16(row);
        for (int b = 0; b < 16; ++b) {
            if (keep >> (15 - b) & 1) continue;
            int c = 0;
            for (int p = 0; p < 4; ++p) c |= int(memory.u16(row + 2 + std::uint32_t(p) * 2) >> (15 - b) & 1) << p;
            put(x + b, y + r, std::uint8_t(c));
        }
    }
}

void Screen::icon8(const data::Memory& memory, std::uint32_t address, int x, int y) {
    for (int r = 0; r < 8 && y + r < height; ++r) {
        const std::uint32_t row = address + std::uint32_t(r) * 5;
        const unsigned keep = memory.u8(row);
        for (int b = 0; b < 8; ++b) {
            if (keep >> (7 - b) & 1) continue;
            int c = 0;
            for (int p = 0; p < 4; ++p) c |= int(memory.u8(row + 1 + std::uint32_t(p)) >> (7 - b) & 1) << p;
            put(x + b, y + r, std::uint8_t(c));
        }
    }
}

void Screen::dot(int x, int y, int pen_index) {
    for (int r = 0; r < 2; ++r)
        for (int b = 0; b < 2; ++b) put((x & ~1) + b, y + r, pen(pen_index).colour((x & ~1) + b, r));
}

void Screen::copy_block(std::span<const std::uint8_t> planes, int x, int y, int width_words, int rows) {
    // Four planes one after the other, each `rows` lines of `width_words` words.
    const std::size_t plane_bytes = std::size_t(width_words) * std::size_t(rows) * 2;
    for (int r = 0; r < rows; ++r)
        for (int w = 0; w < width_words; ++w)
            for (int b = 0; b < 16; ++b) {
                int c = 0;
                for (int p = 0; p < 4; ++p) {
                    const std::size_t at = std::size_t(p) * plane_bytes + (std::size_t(r) * std::size_t(width_words) + std::size_t(w)) * 2 + (b >> 3);
                    if (at < planes.size()) c |= (planes[at] >> (7 - (b & 7)) & 1) << p;
                }
                put((x & ~15) + w * 16 + b, y + r, std::uint8_t(c));
            }
}

void Screen::glyph(int code) {
    // $11F36: a cell as wide as the glyph, 8 lines; set bits in the ink's
    // first pen row, clear ones in the paper's.
    const std::uint32_t at = font_at + std::uint32_t(code & 0xFF) * 9;
    const int w = std::int8_t(font_.u8(at));
    const auto& ink = pen(cursor.ink);
    const auto& paper = pen(cursor.paper);
    for (int r = 0; r < 8; ++r) {
        const unsigned bits = font_.u8(at + 1 + std::uint32_t(r));
        for (int b = 0; b < w && b < 8; ++b) {
            const int x = cursor.x + b, y = cursor.y + r;
            put(x, y, (bits >> (7 - b) & 1) ? ink.colour(x, 0) : paper.colour(x, 0));
        }
    }
    cursor.x += w;
}

void Screen::text(std::string_view s) {
    for (std::size_t i = 0; i < s.size(); ++i) {
        const auto c = std::uint8_t(s[i]);
        const auto byte = [&](std::size_t k) { return i + k < s.size() ? int(std::uint8_t(s[i + k])) : 0; };
        if (c >= 0x20) {
            glyph(c);
        } else if (c == 0x1F) {
            cursor.x = std::int16_t(byte(1) << 8 | byte(2));
            cursor.y = std::int16_t(byte(3) << 8 | byte(4));
            i += 4;
        } else if (c == 0x1E) {
            cursor.ink = byte(1);
            i += 1;
        } else if (c == 0x1D) {
            cursor.paper = byte(1);
            i += 1;
        } else if (c == 0x1C) {
            cursor.paper = 0;
            cursor.ink = 15;
        } else if (c == 0x1B) {
            cursor.x = std::int16_t(byte(1) << 8 | byte(2));
            i += 2;
        } else if (c == 0x0D) {
            cursor.x = 0;
            cursor.y += 12;
        } else if (c == 0x0B) {
            cursor.y -= 8;
        } else if (c == 0x0A) {
            cursor.y += 8;
        } else if (c == 0x09) {
            cursor.x += 8;
        } else if (c == 0x08) {
            cursor.x -= 8;
        }
    }
}

namespace {
// A zero-terminated text with its layout codes (the codes' operands may hold zero bytes).
std::string program_text(const data::Memory& program, std::uint32_t at) {
    std::string s;
    for (;;) {
        const auto c = program.u8(at++);
        if (c == 0) break;
        s += char(c);
        const int operands = c == 0x1F ? 4 : c == 0x1B ? 2 : (c == 0x1E || c == 0x1D) ? 1 : 0;
        for (int k = 0; k < operands; ++k) s += char(program.u8(at++));
    }
    return s;
}
} // namespace

std::string Screen::string_at(std::uint32_t address) const {
    return program_text(assets_.program, address);
}

void Screen::text_at(std::uint32_t address) {
    text(program_text(assets_.program, address));
}

int Screen::text_width(std::string_view s) const {
    int w = 0;
    for (const char c : s) w += font_.u8(font_at + std::uint32_t(std::uint8_t(c)) * 9);
    return w;
}

void Screen::centre(std::string_view s) {
    cursor.x = (unsigned(width - text_width(s)) & 0xFFFF) >> 1;
}

std::string Screen::message_text(int index) const {
    return program_text(assets_.program, message_base + assets_.program.u16(messages + std::uint32_t(index) * 2));
}

void Screen::message(int index) {
    text(message_text(index));
}

void Screen::number(long value, int digits) {
    long divisor = 1;
    for (int i = 1; i < digits; ++i) divisor *= 10;
    for (; divisor > 1; divisor /= 10) {
        int d = 0;
        while (value >= divisor) { // the original counts subtractions without a limit
            value -= divisor;
            ++d;
        }
        glyph(digit_glyph + d);
    }
    glyph(digit_glyph + int(value));
}

void Screen::radio(std::uint32_t list, int chosen) {
    for (int i = 0; std::int16_t(assets_.program.u16(list)) >= 0; ++i, list += 4) {
        cursor.x = assets_.program.u16(list);
        cursor.y = assets_.program.u16(list + 2);
        glyph(i == chosen ? radio_on : radio_off);
    }
}

void Screen::set_palette(const data::Memory& program, std::uint32_t address, int count) {
    for (int i = 0; i < count && i < int(palette.size()); ++i) palette[std::size_t(i)] = program.u16(address + std::uint32_t(i) * 2);
}

platform::Image Screen::image() const {
    platform::Image image(width, height);
    std::array<data::Colour, 16> colours{};
    std::copy_n(palette.begin(), 16, colours.begin());
    view::FramePalette game;
    if (copper == CopperMode::game) {
        game = view::copper_palette(palette, sky_type, flash, assets_.program);
        colours = game.colours;
    }
    std::size_t next7 = 0;
    for (int y = 0; y < height; ++y) {
        if (copper == CopperMode::title) {
            const int band = y >= 0x71 ? 2 : y >= 0x36 ? 1 : 0;
            std::copy_n(palette.begin() + band * 16, 16, colours.begin());
        } else if (copper == CopperMode::split && y == 0x48) {
            colours[7] = palette[16];
            colours[8] = palette[17];
        } else if (copper == CopperMode::game) {
            while (next7 < game.colour7.size() && game.colour7[next7].first <= y) colours[7] = game.colour7[next7++].second;
            if (y == 0x82) // the panel's colours
                for (std::size_t i = 0; i < 15; ++i) colours[i + 1] = palette[33 + i];
        }
        for (int x = 0; x < width; ++x) image.put(x, y, data::to_color(colours[canvas_.at(x, y) & 15]));
    }
    if (marker) {
        const auto [mx, my] = *marker;
        for (int i = 0; i < 16; ++i)
            for (const auto [x, y] : {std::array<int, 2>{mx + i, my}, {mx + i, my + 15}, {mx, my + i}, {mx + 15, my + i}})
                if (x >= 0 && x < width && y >= 0 && y < height) image.put(x, y, platform::Color{255, 255, 255});
    }
    return image;
}

Menu::Menu(const data::Memory& program, std::uint32_t address) {
    for (; std::int8_t(program.u8(address)) >= 0; address += 10) {
        items_.push_back({program.u8(address), program.u8(address + 1), program.u16(address + 2), program.u16(address + 4),
                          program.u16(address + 6), program.u16(address + 8), false});
    }
}

bool Menu::inside(const Item& item, int x, int y) const {
    const bool in = x >= item.x0 && x < item.x1 && y >= item.y0 && y < item.y1;
    return (item.flags & 0x20) ? !in : in;
}

int Menu::at(int x, int y) const {
    int hit = -1;
    for (int i = 0; i < count(); ++i)
        if (inside(items_[std::size_t(i)], x, y)) hit = i;
    return hit;
}

void Menu::hover(Screen& screen, int x, int y) {
    for (auto& item : items_) {
        const bool in = inside(item, x, y);
        if (in == item.lit) continue;
        item.lit = in;
        if (item.flags & 1) {
            screen.invert(item.x0, item.y0, item.x1, item.y1, 15);
        } else if (item.flags & 2) {
            screen.frame(item.x0, item.y0, item.x1, item.y1, in ? item.pen : 0);
        }
    }
}

} // namespace babel::front
