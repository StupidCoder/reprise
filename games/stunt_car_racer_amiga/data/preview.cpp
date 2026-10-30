#include "stunt_car_racer_amiga/data/preview.hpp"
#include <algorithm>
#include <limits>

namespace stuntcar::data {

platform::Image to_image(const Picture& picture) {
    platform::Image image(picture.width, picture.height);
    for (std::size_t i = 0; i < picture.pixels.size(); ++i) {
        const auto& c = picture.palette.at(picture.pixels[i]);
        image.pixels[i] = {c.r, c.g, c.b, 255};
    }
    return image;
}

std::shared_ptr<const ui::Model> circuit_model(const Circuits& circuits, const Track& track) {
    constexpr platform::Color surface{88, 88, 96, 255}, red{136, 34, 34, 255}, white{230, 230, 230, 255},
        ground{122, 106, 74, 255};
    const auto sections = road(circuits, track);
    auto model = std::make_shared<ui::Model>();
    const auto point = [&](PlanPoint p, int height) {
        model->points.push_back({float(p.x) / 256, float(height) / 1024, float(p.z) / 256});
        return int(model->points.size()) - 1;
    };
    int lo_x = std::numeric_limits<int>::max(), hi_x = std::numeric_limits<int>::min();
    int lo_z = lo_x, hi_z = hi_x;
    for (const auto& s : sections) {
        for (const auto p : {s.left, s.right}) {
            lo_x = std::min(lo_x, p.x), hi_x = std::max(hi_x, p.x);
            lo_z = std::min(lo_z, p.z), hi_z = std::max(hi_z, p.z);
        }
    }
    int stripe = 0;
    for (std::size_t i = 1; i < sections.size(); ++i) {
        const auto& a = sections[i - 1];
        const auto& b = sections[i];
        if (a.segment != b.segment) {
            // A new segment starts on the previous one's last cross-section.
            stripe = track.segments[std::size_t(b.segment)].kerb_phase;
            continue;
        }
        const int al = point(a.left, a.left_height), ar = point(a.right, a.right_height);
        const int bl = point(b.left, b.left_height), br = point(b.right, b.right_height);
        model->faces.push_back({{al, ar, br, bl}, surface});
        const auto wall = stripe ? white : red;
        model->faces.push_back({{al, bl, point(b.left, 0), point(a.left, 0)}, wall});
        model->faces.push_back({{ar, br, point(b.right, 0), point(a.right, 0)}, wall});
        stripe ^= 1;
    }
    const int margin = cell_size / 2;
    const int g0 = point({lo_x - margin, lo_z - margin}, 0), g1 = point({hi_x + margin, lo_z - margin}, 0);
    const int g2 = point({hi_x + margin, hi_z + margin}, 0), g3 = point({lo_x - margin, hi_z + margin}, 0);
    model->faces.push_back({{g0, g1, g2, g3}, ground});
    return model;
}

platform::Image circuit_image(const ui::Model& model) {
    platform::Image image(320, 200);
    std::fill(image.pixels.begin(), image.pixels.end(), platform::Color{40, 56, 88, 255});
    ui::draw_model(image, 4, 4, 312, 192, model, 0.0, 0.45);
    return image;
}

} // namespace stuntcar::data
