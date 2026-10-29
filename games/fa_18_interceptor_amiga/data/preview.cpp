#include "fa_18_interceptor_amiga/data/preview.hpp"
#include "ui/draw.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <tuple>

namespace interceptor::data {
namespace {

// Draws one character; returns false when the font has no glyph for it.
void draw_glyph(platform::Image& image, const Font& font, char c, int x, int y, platform::Color colour, int scale = 1) {
    const auto* glyph = font.glyph(c);
    if (glyph == nullptr) return;
    for (int r = 0; r < font.rows; ++r) {
        for (int b = 0; b < 8; ++b) {
            if (((*glyph)[std::size_t(r)] >> (7 - b) & 1) == 0) continue;
            for (int sy = 0; sy < scale; ++sy)
                for (int sx = 0; sx < scale; ++sx) image.put(x + b * scale + sx, y + r * scale + sy, colour);
        }
    }
}

constexpr int small_advance = 4; // the cockpit drawer's 3-pixel glyphs plus a gap (hypothesis)

} // namespace

platform::Color to_color(Colour colour) {
    return {std::uint8_t((colour >> 8 & 15) * 17), std::uint8_t((colour >> 4 & 15) * 17), std::uint8_t((colour & 15) * 17)};
}

platform::Image picture_image(const amiga::IlbmPicture& picture) {
    platform::Image image(picture.width, picture.height);
    for (int y = 0; y < picture.height; ++y) {
        for (int x = 0; x < picture.width; ++x) {
            const auto index = picture.pixels[std::size_t(y) * std::size_t(picture.width) + std::size_t(x)];
            image.put(x, y, index < picture.palette.size() ? to_color(picture.palette[index]) : platform::Color{255, 0, 255});
        }
    }
    return image;
}

platform::Image text_screen(const Assets& assets, const std::vector<int>& blocks, int palette, int spacing) {
    platform::Image image(320, 200);
    const auto& colours = assets.palettes.at(std::size_t(palette));
    ui::fill(image, to_color(colours[0]));
    for (int number : blocks) {
        const auto block = std::find_if(assets.texts.begin(), assets.texts.end(),
                                        [&](const TextBlock& b) { return b.number == number; });
        if (block == assets.texts.end()) continue;
        int y = block->row;
        for (const auto& line : block->lines) {
            for (std::size_t i = 0; i < line.text.size() && i < assets.text_columns.size(); ++i) {
                draw_glyph(image, assets.text_font, line.text[i], block->column * 8 + assets.text_columns[i], y,
                           to_color(colours[std::size_t(line.colour)]));
            }
            y += spacing;
        }
    }
    return image;
}

platform::Image font_sheet(const Assets& assets) {
    constexpr int scale = 3;
    platform::Image image(32 * 9 * scale + 8, 2 * (8 + 9) * scale + 8);
    ui::fill(image, {0, 0, 0});
    int y = 4;
    for (const Font* font : {&assets.small_font, &assets.text_font}) {
        for (int half = 0; half < 2; ++half) {
            for (int i = 0; i < 32; ++i) {
                draw_glyph(image, *font, char(0x20 + half * 32 + i), 4 + i * 9 * scale, y, {230, 230, 230}, scale);
            }
            y += (font->rows + 2) * scale;
        }
        y += 4 * scale;
    }
    return image;
}

platform::Image palette_sheet(const Assets& assets) {
    constexpr int cell = 12;
    platform::Image image(32 * cell, int(assets.palettes.size()) * cell);
    for (std::size_t p = 0; p < assets.palettes.size(); ++p) {
        for (int c = 0; c < 32; ++c) {
            ui::rect(image, c * cell, int(p) * cell, cell - 1, cell - 1, to_color(assets.palettes[p][std::size_t(c)]));
        }
    }
    return image;
}

platform::Image message_sheet(const Assets& assets) {
    constexpr int scale = 2;
    const int rows = (int(assets.messages.size()) + 1) / 2;
    platform::Image image(2 * 28 * small_advance * scale + 16, rows * 8 * scale + 8);
    ui::fill(image, {0, 0, 0});
    for (std::size_t i = 0; i < assets.messages.size(); ++i) {
        const int x0 = 4 + int(i % 2) * (28 * small_advance * scale + 8);
        const int y0 = 4 + int(i / 2) * 8 * scale;
        int x = x0;
        for (char c : assets.messages[i]) {
            draw_glyph(image, assets.small_font, c, x, y0, {120, 230, 120}, scale);
            x += small_advance * scale;
        }
    }
    return image;
}

std::shared_ptr<const ui::Model> shape_model(const Shape& shape, const Palette& palette) {
    auto model = std::make_shared<ui::Model>();
    std::map<std::tuple<double, double, double>, int> index;
    const auto point = [&](const ShapePoint& p) {
        const auto key = std::tuple{p.x, p.y, p.z};
        const auto found = index.find(key);
        if (found != index.end()) return found->second;
        model->points.push_back({float(p.x), float(p.y), float(-p.z)}); // nose away from the viewer at rest
        return index[key] = int(model->points.size()) - 1;
    };
    for (const auto& face : shape.faces) {
        ui::Model::Face out{{}, to_color(palette[std::size_t(face.colour)])};
        for (const auto& corner : face.corners) out.corners.push_back(point(corner));
        model->faces.push_back(std::move(out));
    }
    for (const auto& line : shape.lines) {
        model->lines.push_back({point(line.from), point(line.to), to_color(palette[std::size_t(line.colour)])});
    }
    return model;
}

std::string shape_title(const Shape& shape) {
    // Identified from how they look and from the mission texts (hypotheses
    // except type 4, the aircraft the demo flies).
    static const std::map<int, std::string> names = {
        {0, "missile"}, {1, "missile, second kind"}, {2, "pilot under a parachute"},
        {3, "single-fin jet, light"}, {4, "twin-fin jet, the player's F/A-18"}, {5, "four-engined airliner"},
        {6, "single-fin jet, dark"}, {8, "aircraft carrier"}, {10, "cruise missile"},
        {11, "pod under a parachute"}, {12, "submarine"},
        {43, "runway centre line"}, {45, "runway end"}, {48, "suspension bridge"}, {50, "suspension bridge span"},
        {62, "pyramid tower"}, {65, "lattice radio tower"},
    };
    const auto found = names.find(shape.type);
    return "type " + std::to_string(shape.type) + (found == names.end() ? std::string() : ": " + found->second);
}

platform::Image world_map_image(const World& world, const amiga::LoadedImage& program, const Palette& palette) {
    constexpr int size = 640;
    const double span = world.water.cells * world.water.cell_size; // the whole grid
    const auto to_pixel = [&](double x, double z) {
        // +x is west: the projection ($123894) puts camera +x on the left
        return std::array<double, 2>{size - x / span * size, size - z / span * size};
    };
    platform::Image image(size, size);
    ui::fill(image, to_color(palette[4]));
    const auto fill_polygon = [&](const std::vector<std::array<double, 2>>& points, platform::Color colour) {
        if (points.size() < 3) return;
        double top = 1e9, bottom = -1e9;
        for (const auto& p : points) { top = std::min(top, p[1]); bottom = std::max(bottom, p[1]); }
        for (int y = std::max(0, int(top)); y <= std::min(size - 1, int(bottom)); ++y) {
            std::vector<double> xs;
            const double sy = y + 0.5;
            for (std::size_t i = 0; i < points.size(); ++i) {
                const auto& a = points[i];
                const auto& b = points[(i + 1) % points.size()];
                if ((a[1] <= sy) != (b[1] <= sy)) xs.push_back(a[0] + (sy - a[1]) / (b[1] - a[1]) * (b[0] - a[0]));
            }
            std::sort(xs.begin(), xs.end());
            for (std::size_t i = 0; i + 1 < xs.size(); i += 2)
                for (int x = std::max(0, int(std::ceil(xs[i] - 0.5))); x <= std::min(size - 1, int(xs[i + 1] - 0.5)); ++x)
                    image.put(x, y, colour);
        }
    };
    for (const auto& water : world.water.polygons) {
        if (water.min_height) continue; // drawn only from high up
        std::vector<std::array<double, 2>> points;
        for (const auto& p : water.points) points.push_back(to_pixel(p[0], p[1]));
        fill_polygon(points, to_color(palette[6]));
    }
    for (const auto* map : {&world.pass_a, &world.pass_b, &world.near}) {
        for (const auto& object : map->objects) {
            const auto family = shape_family(program, object.type);
            if (family == ShapeFamily::ground) {
                for (const auto& face : read_ground_shape(program, object.type).faces) {
                    std::vector<std::array<double, 2>> points;
                    for (const auto& c : face.corners) points.push_back(to_pixel(object.x + c.x, object.z + c.z));
                    fill_polygon(points, to_color(palette[std::size_t(face.colour)]));
                }
            } else if (family != ShapeFamily::none) {
                const auto p = to_pixel(object.x, object.z);
                image.put(int(p[0]), int(p[1]), {255, 255, 255});
            }
        }
    }
    return image;
}

platform::Image waveform_image(const Sound& sound) {
    constexpr int width = 640, height = 160;
    platform::Image image(width, height);
    ui::fill(image, {16, 20, 28});
    ui::rect(image, 0, height / 2, width, 1, {60, 70, 90});
    if (sound.samples.empty()) return image;
    for (int x = 0; x < width; ++x) {
        const auto from = sound.samples.size() * std::size_t(x) / width;
        const auto to = std::max(from + 1, sound.samples.size() * std::size_t(x + 1) / width);
        int low = 127, high = -128;
        for (auto i = from; i < to && i < sound.samples.size(); ++i) {
            low = std::min<int>(low, sound.samples[i]);
            high = std::max<int>(high, sound.samples[i]);
        }
        const int top = height / 2 - high * (height / 2 - 4) / 128;
        const int bottom = height / 2 - low * (height / 2 - 4) / 128;
        ui::rect(image, x, top, 1, std::max(1, bottom - top + 1), {110, 200, 255});
    }
    return image;
}

} // namespace interceptor::data
