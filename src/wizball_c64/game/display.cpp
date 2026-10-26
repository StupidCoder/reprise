#include "wizball_c64/game/display.hpp"
#include "c64/palette.hpp"

namespace wizball::game {

Display::Display() = default;

void Display::set_lines_from(int line, const LineSettings& settings) {
    for (int r = line; r < raster_lines; ++r) {
        lines_[std::size_t(r)] = settings;
    }
}

void Display::set_sprite(int index, int line, const SpriteSettings& settings) {
    auto& changes = sprites_[std::size_t(index)];
    if (!changes.empty() && changes.back().line == line) {
        changes.back().settings = settings;
    } else {
        changes.push_back({line, settings});
    }
}

int Display::side_index(int row, int column) const {
    const int k = column < 0 ? column + extension : column - 40 + extension;
    if (row < 0 || row >= 25 || k < 0 || k >= 2 * extension) return -1;
    return row * 2 * extension + k;
}

void Display::set_side(int row, int column, std::uint8_t code, std::uint8_t colour) {
    if (side_codes_.size() != std::size_t(50 * extension)) {
        side_codes_.assign(std::size_t(50 * extension), 0);
        side_colours_.assign(std::size_t(50 * extension), 0);
    }
    const int i = side_index(row, column);
    if (i < 0) return;
    side_codes_[std::size_t(i)] = code;
    side_colours_[std::size_t(i)] = colour;
}

void Display::clear_sprites() {
    for (auto& changes : sprites_) {
        changes.clear();
    }
}

platform::Image Display::render() const {
    const int shift = 8 * extension; // frame x = VIC x + shift
    const int frame_width = width + 2 * shift;
    const int pixels = frame_width * raster_lines;
    std::vector<std::uint8_t> colour(std::size_t(pixels), 0);
    std::vector<bool> foreground(std::size_t(pixels), false);
    // Sprite layer: winning sprite's colour per pixel (-1: none), whether
    // that sprite is behind the foreground.
    std::vector<int> sprite_colour(std::size_t(pixels), -1);
    std::vector<bool> sprite_behind(std::size_t(pixels), false);

    // Characters and background.
    for (int r = first_line; r < first_line + line_count; ++r) {
        const auto& set = lines_[std::size_t(r)];
        const bool wide = set.wide && extension > 0;
        const int left = (wide ? 24 - shift : set.columns38 ? 31 : 24) + shift;
        const int right = (wide ? 344 + shift : set.columns38 ? 335 : 344) + shift;
        int row = -1, sub = 0;
        if (r >= text_top && r < text_top + 200) {
            row = (r - text_top) / 8;
            sub = (r - text_top) % 8;
        }
        const Matrix* matrix = nullptr;
        if (row >= 0) {
            const int screen = lines_[std::size_t(text_top + row * 8)].screen;
            if (screen >= 0 && screen < int(screens.size())) matrix = &screens[std::size_t(screen)];
        }
        const bool closed = !vertical_border_open && (r < text_top || r >= text_top + 200);
        for (int x = 0; x < frame_width; ++x) {
            const std::size_t at = std::size_t(r * frame_width + x);
            if (closed || x < left || x >= right) {
                colour[at] = set.border;
                continue;
            }
            colour[at] = set.background;
            if (!matrix || !set.charset) continue;
            const int gx = x - shift - 24 - set.xscroll;
            if (gx < 0 && !wide) continue;
            const int column = (gx + 8 * (extension + 1)) / 8 - (extension + 1), bit = (gx + 8 * (extension + 1)) % 8;
            std::uint8_t code, cell_colour;
            if (column >= 0 && column < 40) {
                const int cell = row * 40 + column;
                code = matrix->codes[std::size_t(cell)];
                cell_colour = colours[std::size_t(cell)] & 15;
            } else {
                const int i = wide && !side_codes_.empty() ? side_index(row, column) : -1;
                if (i < 0) continue;
                code = side_codes_[std::size_t(i)];
                cell_colour = side_colours_[std::size_t(i)] & 15;
            }
            const auto byte = (*set.charset)[code][std::size_t(sub)];
            if (set.multicolour && (cell_colour & 8)) {
                const int pair = byte >> (6 - (bit & 6)) & 3;
                const std::uint8_t c[4] = {set.background, set.shared1, set.shared2, std::uint8_t(cell_colour & 7)};
                colour[at] = c[pair];
                foreground[at] = pair >= 2;
            } else if (byte >> (7 - bit) & 1) {
                colour[at] = std::uint8_t(cell_colour);
                foreground[at] = true;
            }
        }
    }

    // Sprites: lowest number wins; each starts on the line after its y
    // matches the raster (low eight bits) and runs for 21 lines.
    for (int n = 7; n >= 0; --n) {
        const auto& changes = sprites_[std::size_t(n)];
        if (changes.empty()) continue;
        std::size_t next = 0;
        SpriteSettings st{};
        bool dma = false, flip = false;
        int base = 0;
        for (int r = 0; r < raster_lines; ++r) {
            while (next < changes.size() && changes[next].line <= r) st = changes[next++].settings;
            if (dma) {
                if (r >= first_line && r < first_line + line_count) {
                    const int row = base / 3;
                    for (int px = 0; px < 24; ++px) {
                        const auto byte = st.shape[std::size_t(row * 3 + px / 8)];
                        int c = -1;
                        if (st.multicolour) {
                            const int pair = byte >> (6 - (px & 6)) & 3;
                            if (pair == 1) c = lines_[std::size_t(r)].sprite_shared1;
                            else if (pair == 2) c = st.colour;
                            else if (pair == 3) c = lines_[std::size_t(r)].sprite_shared2;
                        } else if (byte >> (7 - px % 8) & 1) {
                            c = st.colour;
                        }
                        if (c < 0) continue;
                        const int span = st.expand_x ? 2 : 1;
                        for (int k = 0; k < span; ++k) {
                            int vx = (st.x + px * span + k) % 504;
                            // A wide view shows 80 pixels either side of the
                            // window: positions from 424 on are left of it.
                            if (shift > 0 && vx >= 424) vx -= 504;
                            const int x = vx + shift;
                            if (x < 0 || x >= frame_width) continue;
                            const std::size_t at = std::size_t(r * frame_width + x);
                            sprite_colour[at] = c & 15;
                            sprite_behind[at] = st.behind;
                        }
                    }
                }
                if (!st.expand_y || flip) base += 3;
                flip = !flip;
                if (base >= 63) dma = false;
            }
            if (!dma && st.enabled && st.y == (r & 0xFF)) {
                dma = true;
                base = 0;
                flip = false;
            }
        }
    }

    platform::Image image(frame_width, line_count);
    for (int y = 0; y < line_count; ++y) {
        const int r = y + first_line;
        const auto& set = lines_[std::size_t(r)];
        const bool wide = set.wide && extension > 0;
        const int left = (wide ? 24 - shift : set.columns38 ? 31 : 24) + shift;
        const int right = (wide ? 344 + shift : set.columns38 ? 335 : 344) + shift;
        for (int x = 0; x < frame_width; ++x) {
            const std::size_t at = std::size_t(r * frame_width + x);
            int c = colour[at];
            const bool in_border = x < left || x >= right ||
                                   (!vertical_border_open && (r < text_top || r >= text_top + 200));
            if (sprite_colour[at] >= 0 && !in_border && !(sprite_behind[at] && foreground[at])) {
                c = sprite_colour[at];
            }
            image.put(x, y, c64::colour(c));
        }
    }
    return image;
}

} // namespace wizball::game
