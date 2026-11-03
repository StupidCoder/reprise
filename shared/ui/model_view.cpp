#include "ui/model_view.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace ui {
namespace {

struct Projected {
    float x, y, z; // screen position and view depth (larger = farther)
};

struct Viewport {
    platform::Image& image;
    int x0, y0, w, h;
    std::vector<float> depth;

    void plot(int x, int y, float z, platform::Color colour) {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        auto& d = depth[std::size_t(y) * std::size_t(w) + std::size_t(x)];
        if (z >= d) return;
        d = z;
        if (colour.a < 255 && x0+x >= 0 && y0+y >= 0 && x0+x < image.width && y0+y < image.height) {
            const auto back = image.pixels[std::size_t(y0+y) * image.width + x0+x];
            const int a = colour.a;
            colour = {std::uint8_t((colour.r*a + back.r*(255-a))/255),
                      std::uint8_t((colour.g*a + back.g*(255-a))/255),
                      std::uint8_t((colour.b*a + back.b*(255-a))/255), 255};
        }
        image.put(x0 + x, y0 + y, colour);
    }

    // Fills a triangle with depth interpolated linearly in screen space
    // (good enough for flat faces viewed from a distance).
    void triangle(Projected a, Projected b, Projected c, platform::Color colour,
                  const platform::Image* texture = nullptr,
                  std::array<std::array<float, 2>, 3> uv = {}, float alpha_threshold = 0) {
        const float area = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
        if (std::abs(area) < 1e-6f) return;
        const int left = std::max(0, int(std::floor(std::min({a.x, b.x, c.x}))));
        const int right = std::min(w - 1, int(std::ceil(std::max({a.x, b.x, c.x}))));
        const int top = std::max(0, int(std::floor(std::min({a.y, b.y, c.y}))));
        const int bottom = std::min(h - 1, int(std::ceil(std::max({a.y, b.y, c.y}))));
        for (int py = top; py <= bottom; ++py) {
            for (int px = left; px <= right; ++px) {
                const float sx = float(px) + 0.5f, sy = float(py) + 0.5f;
                const float wa = ((b.x - sx) * (c.y - sy) - (c.x - sx) * (b.y - sy)) / area;
                const float wb = ((c.x - sx) * (a.y - sy) - (a.x - sx) * (c.y - sy)) / area;
                const float wc = 1 - wa - wb;
                if (wa < 0 || wb < 0 || wc < 0) continue;
                auto pixel = colour;
                const float inverse_depth = wa / a.z + wb / b.z + wc / c.z;
                if (texture && texture->width > 0 && texture->height > 0) {
                    const float u = (wa * uv[0][0] / a.z + wb * uv[1][0] / b.z + wc * uv[2][0] / c.z) / inverse_depth;
                    const float v = (wa * uv[0][1] / a.z + wb * uv[1][1] / b.z + wc * uv[2][1] / c.z) / inverse_depth;
                    if (!std::isfinite(u) || !std::isfinite(v)) continue;
                    const int tx = std::min(texture->width - 1, int((u - std::floor(u)) * texture->width));
                    const int ty = std::min(texture->height - 1, int((v - std::floor(v)) * texture->height));
                    const auto texel = texture->pixels[std::size_t(ty) * texture->width + tx];
                    pixel = {std::uint8_t(int(colour.r) * texel.r / 255), std::uint8_t(int(colour.g) * texel.g / 255),
                             std::uint8_t(int(colour.b) * texel.b / 255), std::uint8_t(int(colour.a) * texel.a / 255)};
                }
                if (pixel.a < std::max(1, int(alpha_threshold * 255))) continue;
                plot(px, py, 1 / inverse_depth, pixel);
            }
        }
    }

    void line(Projected a, Projected b, platform::Color colour) {
        const int steps = int(std::max(std::abs(b.x - a.x), std::abs(b.y - a.y))) + 1;
        for (int i = 0; i <= steps; ++i) {
            const float t = float(i) / float(steps);
            // A small bias so lines on a face win over the face itself.
            plot(int(a.x + (b.x - a.x) * t), int(a.y + (b.y - a.y) * t), a.z + (b.z - a.z) * t - 0.01f, colour);
        }
    }
};

platform::Color shade(platform::Color colour, float light) {
    const auto f = [&](std::uint8_t v) { return std::uint8_t(std::clamp(float(v) * light, 0.0f, 255.0f)); };
    return {f(colour.r), f(colour.g), f(colour.b), colour.a};
}

} // namespace

void draw_model(platform::Image& image, int x, int y, int w, int h, const Model& model, double yaw, double pitch, double zoom, double pan_x, double pan_y) {
    if (model.points.empty() || w <= 0 || h <= 0) return;
    // Fit: centre of the bounding box, radius of the farthest point.
    std::array<float, 3> low{}, high{};
    for (int axis = 0; axis < 3; ++axis) {
        low[std::size_t(axis)] = std::numeric_limits<float>::max();
        high[std::size_t(axis)] = std::numeric_limits<float>::lowest();
    }
    for (const auto& p : model.points) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            low[axis] = std::min(low[axis], p[axis]);
            high[axis] = std::max(high[axis], p[axis]);
        }
    }
    if (model.fit_bounds) for (int i=0;i<3;++i) { low[i]=(*model.fit_bounds)[i]; high[i]=(*model.fit_bounds)[i+3]; }
    const std::array<float, 3> centre{(low[0] + high[0]) / 2, (low[1] + high[1]) / 2, (low[2] + high[2]) / 2};
    float radius = 1e-6f;
    for (const auto& p : model.points) {
        radius = std::max(radius, std::hypot(p[0] - centre[0], p[1] - centre[1], p[2] - centre[2]));
    }

    if (model.fit_bounds) radius = std::max(1e-6f, std::hypot(high[0]-low[0], high[1]-low[1], high[2]-low[2]) * .5f);

    const float cy = float(std::cos(yaw)), sy = float(std::sin(yaw));
    const float cp = float(std::cos(pitch)), sp = float(std::sin(pitch));
    const auto turn = [&](std::array<float, 3> p) {
        p = {(p[0] - centre[0]) / radius, (p[1] - centre[1]) / radius, (p[2] - centre[2]) / radius};
        const float x1 = cy * p[0] + sy * p[2], z1 = -sy * p[0] + cy * p[2];
        const float y2 = cp * p[1] - sp * z1, z2 = sp * p[1] + cp * z1;
        return std::array<float, 3>{x1, y2, z2};
    };
    // The camera sits 3 radii in front; the whole sphere fits the shorter side.
    constexpr float distance = 3.0f;
    const float scale = float(std::min(w, h)) * 0.5f * std::sqrt(distance * distance - 1) * 0.95f * float(zoom);
    std::vector<std::array<float, 3>> view;
    std::vector<Projected> screen;
    for (const auto& p : model.points) {
        const auto v = turn(p);
        const float depth = distance - v[2];
        view.push_back(v);
        screen.push_back({float(w) / 2 + float(pan_x) + v[0] * scale / depth, float(h) / 2 + float(pan_y) - v[1] * scale / depth, depth});
    }

    Viewport port{image, x, y, w, h, std::vector<float>(std::size_t(w) * std::size_t(h), std::numeric_limits<float>::max())};
    const auto valid = [&](int i) { return i >= 0 && std::size_t(i) < model.points.size(); };
    for (const auto& face : model.faces) {
        if (face.corners.size() < 3 || !std::all_of(face.corners.begin(), face.corners.end(), valid)) continue;
        // Newell's normal (robust for slightly non-planar polygons).
        std::array<float, 3> n{};
        for (std::size_t i = 0; i < face.corners.size(); ++i) {
            const auto& a = view[std::size_t(face.corners[i])];
            const auto& b = view[std::size_t(face.corners[(i + 1) % face.corners.size()])];
            n[0] += (a[1] - b[1]) * (a[2] + b[2]);
            n[1] += (a[2] - b[2]) * (a[0] + b[0]);
            n[2] += (a[0] - b[0]) * (a[1] + b[1]);
        }
        const float length = std::hypot(n[0], n[1], n[2]);
        const float facing = length > 0 ? std::abs(n[2]) / length : 1.0f; // light from the viewer, both sides
        const auto colour = shade(face.colour, face.unlit ? 1.0f : 0.55f + 0.45f * facing);
        const auto& first = screen[std::size_t(face.corners[0])];
        for (std::size_t i = 1; i + 1 < face.corners.size(); ++i) {
            const auto b = screen[std::size_t(face.corners[i])], c = screen[std::size_t(face.corners[i + 1])];
            if (first.z <= .05f || b.z <= .05f || c.z <= .05f ||
                !std::isfinite(first.x) || !std::isfinite(first.y) || !std::isfinite(b.x) || !std::isfinite(b.y) || !std::isfinite(c.x) || !std::isfinite(c.y)) continue;
            const auto* texture = face.texture >= 0 && std::size_t(face.texture) < model.textures.size() ? model.textures[face.texture].get() : nullptr;
            std::array<std::array<float, 2>, 3> uv{};
            if (face.uv.size() == face.corners.size()) uv = {face.uv[0], face.uv[i], face.uv[i+1]};
            port.triangle(first, b, c, colour, texture, uv, face.alpha_threshold);
        }
    }
    for (const auto& line : model.lines) {
        if (valid(line.from) && valid(line.to)) {
            port.line(screen[std::size_t(line.from)], screen[std::size_t(line.to)], line.colour);
        }
    }
}

} // namespace ui
