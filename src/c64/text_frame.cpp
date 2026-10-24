#include "c64/text_frame.hpp"
#include "c64/palette.hpp"
#include <algorithm>

namespace c64 {

platform::Image Picture::to_image(int margin_x, int margin_y) const {
    return to_image(margin_x, margin_y, 0, width);
}

platform::Image Picture::to_image(int margin_x, int margin_y, int left, int shown) const {
    platform::Image image(shown + 2 * margin_x, height + 2 * margin_y);
    std::fill(image.pixels.begin(), image.pixels.end(), colour(border));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < shown; ++x) {
            image.put(margin_x + x, margin_y + y, colour(at(left + x, y)));
        }
    }
    return image;
}

TextFrame::TextFrame(int columns)
    : columns_(columns), cells_(std::size_t(columns * rows)) {}

void TextFrame::set(int column, int row, const Glyph* glyph, std::uint8_t colour) {
    if (column >= 0 && column < columns_ && row >= 0 && row < rows) {
        cells_[std::size_t(row * columns_ + column)] = {glyph ? *glyph : Glyph{}, colour};
    }
}

void TextFrame::set_glyph(int column, int row, const Glyph* glyph) {
    if (column >= 0 && column < columns_ && row >= 0 && row < rows) {
        cells_[std::size_t(row * columns_ + column)].glyph = glyph ? *glyph : Glyph{};
    }
}

void TextFrame::set_colour(int column, int row, std::uint8_t colour) {
    if (column >= 0 && column < columns_ && row >= 0 && row < rows) {
        cells_[std::size_t(row * columns_ + column)].colour = colour;
    }
}

const TextFrame::Cell& TextFrame::cell(int column, int row) const {
    return cells_[std::size_t(row * columns_ + column)];
}

void TextFrame::clear() {
    std::fill(cells_.begin(), cells_.end(), Cell{});
}

namespace {
// The VIC's colour registers have 4 bits; the rest of a written value is lost.
LineSetup with_vic_colours(LineSetup setup) {
    setup.background &= 15;
    setup.shared1 &= 15;
    setup.shared2 &= 15;
    setup.border &= 15;
    setup.background_before &= 15;
    setup.border_before &= 15;
    return setup;
}
} // namespace

void TextFrame::set_lines(const LineSetup& setup) {
    lines_.fill(with_vic_colours(setup));
}

void TextFrame::set_lines(int first, int end, const LineSetup& setup) {
    std::fill(lines_.begin() + std::clamp(first, 0, Picture::height),
              lines_.begin() + std::clamp(end, 0, Picture::height), with_vic_colours(setup));
}

Picture TextFrame::rasterise(Collisions* collisions, Layers* layers) const {
    constexpr int first_display_raster = 51; // the 25-row window starts here
    constexpr int window_left = 24;          // VIC x of the first display pixel
    constexpr int line_width = 504;          // VIC x positions per raster line
    auto setup_for = [&](int raster) -> const LineSetup& {
        return lines_[std::size_t(std::clamp(raster - first_display_raster, 0, Picture::height - 1))];
    };

    const int width = this->width(), extension = this->extension();
    Picture picture(width);
    picture.border = lines_[0].border;
    if (layers) {
        layers->foreground.assign(std::size_t(width * Picture::height), false);
    }
    Collisions found;
    // Row sequencing from the first raster where bad lines can occur.
    int vcbase = 0, rc = 0, buffered_row = 0;
    bool display = false;
    for (int raster = 0x30; raster < 0x100 + 0x30; ++raster) {
        const auto& line = setup_for(raster);
        const bool in_range = raster <= 0xF7;
        const bool bad_line = in_range && (raster & 7) == line.yscroll;
        const bool bad_early = in_range && line.yscroll_before >= 0 &&
                               (raster & 7) == line.yscroll_before;
        if (bad_line || bad_early) {
            display = true;
        }
        if (bad_line) { // cycle 14 with the new value: the row buffer is filled
            rc = 0;
            buffered_row = std::min(vcbase / columns_, rows - 1);
        }
        const int y = raster - first_display_raster;
        const bool in_window = y >= 0 && y < Picture::height;

        // Graphics: colour and foreground flag per display pixel.
        std::vector<std::uint8_t> colours(std::size_t(width), line.background);
        std::vector<bool> foreground(colours.size());
        std::fill_n(colours.begin(), std::clamp(line.background_from_x, 0, width),
                    line.background_before);
        if (in_window && !line.covered) {
            for (int gx = 0; gx + line.xscroll < width; ++gx) {
                std::uint8_t bits, cell_colour;
                if (display) {
                    const auto& c = cells_[std::size_t(buffered_row * columns_ + gx / 8)];
                    bits = c.glyph[std::size_t(rc)];
                    cell_colour = c.colour;
                } else {
                    bits = idle_pattern_; // shown with colour 0, single colour
                    cell_colour = 0;
                }
                const int px = gx & 7, x = gx + line.xscroll;
                if (line.multicolour && (cell_colour & 8)) {
                    const int pair = bits >> (6 - 2 * (px / 2)) & 3;
                    const std::uint8_t background =
                        x < line.background_from_x ? line.background_before : line.background;
                    const std::uint8_t by_pair[] = {background, line.shared1, line.shared2,
                                                    std::uint8_t(cell_colour & 7)};
                    colours[std::size_t(x)] = by_pair[pair];
                    foreground[std::size_t(x)] = pair >= 2;
                } else {
                    const bool set = bits >> (7 - px) & 1;
                    colours[std::size_t(x)] = set ? std::uint8_t(cell_colour & 15)
                                              : x < line.background_from_x ? line.background_before
                                                                           : line.background;
                    foreground[std::size_t(x)] = set;
                }
            }
        }

        // Sprites: the lowest-numbered sprite with a pixel wins; collisions
        // are counted over the whole line.
        if (extension == 0) {
            std::array<std::uint8_t, line_width> hits{};
            std::array<std::int8_t, line_width> winner{};
            winner.fill(-1);
            for (int i = 0; i < 8; ++i) {
                const auto& s = sprites_[std::size_t(i)];
                const int row = raster - (s.y + 1);
                if (!s.shown || row < 0 || row >= 21 || !(setup_for(s.y).sprites >> i & 1)) {
                    continue;
                }
                const int scale = s.expand_x ? 2 : 1;
                for (int bit = 0; bit < 24; ++bit) {
                    if (!(s.data[std::size_t(row * 3 + bit / 8)] >> (7 - bit % 8) & 1)) {
                        continue;
                    }
                    for (int k = 0; k < scale; ++k) {
                        const int x = (s.x + bit * scale + k) % line_width;
                        hits[std::size_t(x)] |= std::uint8_t(1 << i);
                        if (winner[std::size_t(x)] < 0) {
                            winner[std::size_t(x)] = std::int8_t(i);
                        }
                    }
                }
            }
            for (int x = 0; x < line_width; ++x) {
                const auto h = hits[std::size_t(x)];
                if (!h) {
                    continue;
                }
                if (h & (h - 1)) {
                    found.sprite_sprite |= h;
                }
                const int dx = x - window_left;
                const bool on_graphics = in_window && dx >= 0 && dx < width;
                const bool fg = on_graphics && foreground[std::size_t(dx)];
                if (fg) {
                    found.sprite_background |= h;
                }
                const auto& s = sprites_[std::size_t(winner[std::size_t(x)])];
                if (on_graphics && s.drawn && (!s.behind_foreground || !fg)) {
                    colours[std::size_t(dx)] = s.colour;
                }
            }
        } else {
            // A widescreen frame: sprites drawn at their place in the wider
            // picture, without the VIC's wrap at the line's end. Collisions
            // come from the game's standard frame.
            for (int i = 7; i >= 0; --i) {
                const auto& s = sprites_[std::size_t(i)];
                const int row = raster - (s.y + 1);
                if (!s.shown || !s.drawn || row < 0 || row >= 21 ||
                    !(setup_for(s.y).sprites >> i & 1) || !in_window) {
                    continue;
                }
                const int scale = s.expand_x ? 2 : 1;
                for (int bit = 0; bit < 24; ++bit) {
                    if (!(s.data[std::size_t(row * 3 + bit / 8)] >> (7 - bit % 8) & 1)) {
                        continue;
                    }
                    for (int k = 0; k < scale; ++k) {
                        const int x = s.x - window_left + extension + bit * scale + k;
                        if (x >= 0 && x < width && (!s.behind_foreground || !foreground[std::size_t(x)])) {
                            colours[std::size_t(x)] = s.colour;
                        }
                    }
                }
            }
        }

        if (in_window && layers) {
            std::copy(foreground.begin(), foreground.end(),
                      layers->foreground.begin() + y * width);
            layers->source_line[std::size_t(y)] =
                display && !line.covered ? std::int16_t(buffered_row * 8 + rc) : std::int16_t(-1);
            layers->xscroll[std::size_t(y)] = line.xscroll;
        }
        if (in_window) {
            auto* out = picture.pixels.data() + y * width;
            std::copy(colours.begin(), colours.end(), out);
            if (line.covered) {
                std::fill_n(out, width, line.border);
            } else if (line.narrow) {
                std::fill_n(out, 7, line.background_from_x > 0 ? line.border_before : line.border);
                std::fill_n(out + width - 9, 9, line.border);
            }
        }
        // Cycle 58: after the eighth line of a row the next row's position is
        // taken and the VIC goes idle until the next bad line.
        if (rc == 7) {
            if (display) {
                vcbase += columns_;
            }
            display = bad_line;
        }
        if (display) {
            rc = (rc + 1) & 7;
        }
    }
    if (collisions) {
        *collisions = found;
    }
    return picture;
}

} // namespace c64
