#include "tower_of_babel_amiga/data/model.hpp"
#include "tower_of_babel_amiga/data/preview.hpp"
#include "tower_of_babel_amiga/view/tower_view.hpp"

namespace babel::data {
namespace {

constexpr std::uint32_t kind_table = 0x13742; // $13486
constexpr std::uint32_t shape_base = 0x18200;

// The average colour of a pen's 2 x 2 pattern (its visible pixels only).
platform::Color pen_colour(const view::Pen& pen, const Tower& tower) {
    int r = 0, g = 0, b = 0, n = 0;
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x) {
            if (pen.see_through(x, y)) continue;
            const auto c = to_color(tower.colours[pen.colour(x, y)]);
            r += c.r; g += c.g; b += c.b; ++n;
        }
    if (n == 0) return to_color(tower.colours[0]);
    return {std::uint8_t(r / n), std::uint8_t(g / n), std::uint8_t(b / n)};
}

} // namespace

std::vector<KindShape> kind_shapes(const Memory& program) {
    std::vector<KindShape> result;
    for (int kind = 4; kind < 0x28; ++kind) {
        const std::uint32_t entry = kind_table + std::uint32_t(kind) * 10;
        result.push_back({kind, shape_base + program.u32(entry + 2)});
    }
    return result;
}

std::shared_ptr<const ui::Model> shape_model(const Shape& shape, const Memory& program, const Tower& tower) {
    const auto pens = view::read_pens(program);
    auto model = std::make_shared<ui::Model>();
    for (const auto& part : shape.parts) {
        for (const auto& face : part.faces) {
            if (face.points.size() < 3) continue;
            // $13D12 as in a lit cell; an object's own pen as slot 4's.
            const int s = face.style;
            const view::Pen& pen = (s & 0x80) ? ((s & 0x40) ? pens.object[std::size_t(4 + 9 + ((s & 1) ? 7 : 0))]
                                                             : pens.see_through[std::size_t(s & 0x1F)])
                                              : pens.object[std::size_t(s & 0x1F)];
            ui::Model::Face out{{}, pen_colour(pen, tower)};
            for (const auto& p : face.points) {
                out.corners.push_back(int(model->points.size()));
                // Game: y up, z away from the camera; viewer: z towards the viewer.
                model->points.push_back({float(p.x), float(p.y), float(-p.z)});
            }
            model->faces.push_back(std::move(out));
        }
    }
    return model;
}

} // namespace babel::data
