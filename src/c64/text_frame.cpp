#include "c64/text_frame.hpp"
#include "c64/palette.hpp"
#include <algorithm>

namespace c64 {

platform::Image Picture::to_image(int margin_x, int margin_y) const {
    platform::Image image(width + 2 * margin_x, height + 2 * margin_y);
    std::fill(image.pixels.begin(), image.pixels.end(), colour(border));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            image.put(margin_x + x, margin_y + y, colour(at(x, y)));
        }
    }
    return image;
}

void TextFrame::set(int column, int row, const Glyph* glyph, std::uint8_t colour) {
    if (column >= 0 && column < columns && row >= 0 && row < rows) {
        cells_[std::size_t(row * columns + column)] = {glyph, colour};
    }
}

void TextFrame::set_glyph(int column, int row, const Glyph* glyph) {
    if (column >= 0 && column < columns && row >= 0 && row < rows) {
        cells_[std::size_t(row * columns + column)].glyph = glyph;
    }
}

void TextFrame::set_colour(int column, int row, std::uint8_t colour) {
    if (column >= 0 && column < columns && row >= 0 && row < rows) {
        cells_[std::size_t(row * columns + column)].colour = colour;
    }
}

const TextFrame::Cell& TextFrame::cell(int column, int row) const {
    return cells_[std::size_t(row * columns + column)];
}

void TextFrame::clear() {
    cells_ = {};
}

void TextFrame::set_lines(const LineSetup& setup) {
    lines_.fill(setup);
}

void TextFrame::set_lines(int first, int end, const LineSetup& setup) {
    std::fill(lines_.begin() + std::clamp(first, 0, Picture::height),
              lines_.begin() + std::clamp(end, 0, Picture::height), setup);
}

Picture TextFrame::rasterise() const {
    constexpr int first_display_raster = 51; // the 25-row window starts here
    auto setup_for = [&](int raster) -> const LineSetup& {
        return lines_[std::size_t(std::clamp(raster - first_display_raster, 0, Picture::height - 1))];
    };

    Picture picture;
    picture.border = lines_[0].border;
    // Row sequencing from the first raster where bad lines can occur.
    int vcbase = 0, rc = 0;
    bool display = false;
    for (int raster = 0x30; raster < first_display_raster + Picture::height; ++raster) {
        const auto& line = setup_for(raster);
        const bool bad_line = raster <= 0xF7 && (raster & 7) == line.yscroll;
        if (bad_line) {
            display = true;
            rc = 0;
        }
        const int y = raster - first_display_raster;
        if (y >= 0) {
            std::array<std::uint8_t, Picture::width + 8> pixels{};
            for (int column = 0; column < columns; ++column) {
                std::uint8_t bits, cell_colour;
                const Cell* c = nullptr;
                if (display) {
                    c = &cells_[std::size_t(std::min(vcbase / columns, rows - 1) * columns + column)];
                    bits = c->glyph ? (*c->glyph)[std::size_t(rc)] : 0;
                    cell_colour = c->colour;
                } else {
                    bits = idle_pattern_; // shown with colour 0, single colour
                    cell_colour = 0;
                }
                for (int px = 0; px < 8; ++px) {
                    std::uint8_t colour;
                    if (line.multicolour && (cell_colour & 8)) {
                        const int pair = bits >> (6 - 2 * (px / 2)) & 3;
                        const std::uint8_t by_pair[] = {line.background, line.shared1,
                                                        line.shared2,
                                                        std::uint8_t(cell_colour & 7)};
                        colour = by_pair[pair];
                    } else {
                        colour = bits >> (7 - px) & 1 ? std::uint8_t(cell_colour & 15)
                                                      : line.background;
                    }
                    pixels[std::size_t(column * 8 + px)] = colour;
                }
            }
            auto* out = picture.pixels.data() + y * Picture::width;
            std::fill_n(out, line.xscroll, line.background);
            std::copy_n(pixels.begin(), Picture::width - line.xscroll, out + line.xscroll);
            if (line.covered) {
                std::fill_n(out, Picture::width, line.border);
            } else if (line.narrow) {
                std::fill_n(out, 7, line.border);
                std::fill_n(out + Picture::width - 9, 9, line.border);
            }
        }
        // Cycle 58: after the eighth line of a row the next row's position is
        // taken and the VIC goes idle until the next bad line.
        if (rc == 7) {
            if (display) {
                vcbase += columns;
            }
            display = bad_line;
        }
        if (display) {
            rc = (rc + 1) & 7;
        }
    }
    return picture;
}

} // namespace c64
