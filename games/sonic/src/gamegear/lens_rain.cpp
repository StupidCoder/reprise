// Rain on the camera lens (see lens_rain.hpp).

#include "gamegear/lens_rain.hpp"
#include <algorithm>
#include <cmath>

namespace gg {
namespace {

// Integer hash for deterministic cosmetic variation; never a game's RNG.
std::uint32_t mix(std::uint32_t v) {
    v ^= v >> 16;
    v *= 0x7feb352du;
    v ^= v >> 15;
    v *= 0x846ca68bu;
    v ^= v >> 16;
    return v;
}

float unit(std::uint32_t v) {
    return float(v & 1023) / 1023.f;
}

Color blend(Color under, Color over, unsigned alpha) {
    auto channel = [&](unsigned top, unsigned bottom) {
        return std::uint8_t((top * alpha + bottom * (255 - alpha) + 127) / 255);
    };
    return {channel(over.r, under.r), channel(over.g, under.g), channel(over.b, under.b), 255};
}

constexpr Color white{255, 255, 255, 255}, black{0, 0, 0, 255};

// Drops landing per update on a 160x144 view in full rain, and the most
// that cling to it at once.
constexpr float landing_rate = 0.2f;
constexpr std::size_t max_drops = 140, max_beads = 4000;
// Drops at least this large slide once they have clung for a while;
// sliding ones that shrink below `stop_radius` stop and dry.
constexpr float slide_radius = 1.7f, stop_radius = 1.4f, max_radius = 4.f;

} // namespace

void LensRain::clear() {
    drops_.clear();
    beads_.clear();
}

void LensRain::step(float rain, int width, int height, std::uint64_t time) {
    width = std::max(width, 1);
    height = std::max(height, 1);
    if (width != width_ || height != height_) {
        // The view changed size: the drops stay where they were on it.
        const float sx = float(width) / float(width_), sy = float(height) / float(height_);
        for (auto& d : drops_) {
            d.x *= sx;
            d.y *= sy;
        }
        for (auto& b : beads_) {
            b.x *= sx;
            b.y *= sy;
        }
        width_ = width;
        height_ = height;
    }

    // New drops: mostly small, a few heavy ones.
    const std::uint32_t h = mix(std::uint32_t(time) * 0x9e3779b9u ^ 0x5bd1e995u);
    const float area = float(width * height) / (160.f * 144.f);
    const float rate = landing_rate * std::clamp(rain, 0.f, 1.f) * area;
    const int landing = int(rate) + (unit(h) < rate - std::floor(rate) ? 1 : 0);
    for (int i = 0; i < landing && float(drops_.size()) < float(max_drops) * area; ++i) {
        const std::uint32_t s = mix(h + std::uint32_t(i + 1) * 0x27d4eb2fu);
        const float size = unit(s >> 10);
        drops_.push_back({unit(s) * float(width), unit(s >> 20) * float(height),
                          0.8f + 2.8f * size * std::sqrt(size), 0.f, 30 + (s >> 4) % 150, 0, s});
    }

    // Clinging, sliding and drying.
    for (auto& d : drops_) {
        ++d.age;
        const bool sliding = d.r >= slide_radius && d.age > d.cling;
        if (!sliding) {
            d.vy = 0;
            d.r -= 0.0012f; // dries in ten to twenty seconds
            continue;
        }
        // Stop and go: a quarter of the 12-update windows hold it back.
        const bool hold = (mix(d.seed + d.age / 12) & 3) == 0;
        d.vy = hold ? d.vy * 0.5f : std::min(d.vy + 0.015f + 0.008f * d.r, 0.12f + 0.16f * d.r);
        const int row = int(std::floor(d.y));
        d.y += d.vy;
        // Every pixel row crossed: a slight sideways wander, a bead of
        // residue and a little less water in the drop.
        for (int y = row + 1; y <= int(std::floor(d.y)); ++y) {
            d.x += (unit(mix(d.seed ^ std::uint32_t(y) * 0x85ebca6bu)) - 0.5f) * 0.5f;
            beads_.push_back({d.x, float(y) - d.r * 0.6f, 0.15f + 0.2f * d.r,
                              120 + (mix(d.seed + std::uint32_t(y)) % 90)});
            d.r -= 0.01f;
        }
        if (d.r < stop_radius) {
            d.cling = ~0u; // too light to move on
        }
    }
    // A sliding drop swallows the drops it runs into.
    for (std::size_t i = 0; i < drops_.size(); ++i) {
        auto& a = drops_[i];
        if (a.vy <= 0 || a.r <= 0) {
            continue;
        }
        for (std::size_t j = 0; j < drops_.size(); ++j) {
            auto& b = drops_[j];
            if (i == j || b.r <= 0) {
                continue;
            }
            const float dx = a.x - b.x, dy = a.y - b.y, reach = (a.r + b.r) * 0.8f;
            if (dx * dx + dy * dy < reach * reach) {
                a.r = std::min(max_radius, std::sqrt(a.r * a.r + b.r * b.r));
                b.r = 0;
            }
        }
    }
    std::erase_if(drops_, [&](const Drop& d) { return d.r < 0.5f || d.y - d.r > float(height); });

    for (auto& b : beads_) {
        --b.life;
    }
    std::erase_if(beads_, [](const Bead& b) { return b.life == 0; });
    if (beads_.size() > max_beads) {
        beads_.erase(beads_.begin(), beads_.end() - std::ptrdiff_t(max_beads));
    }
}

void LensRain::draw(Screen& image) const {
    if (empty()) {
        return;
    }
    const auto picture = image.pixels;
    const float sx = float(image.width) / float(width_), sy = float(image.height) / float(height_);
    auto source = [&](int x, int y) {
        x = std::clamp(x, 0, image.width - 1);
        y = std::clamp(y, 0, image.height - 1);
        return picture[std::size_t(y * image.width + x)];
    };
    auto put = [&](int x, int y, Color c) {
        if (x >= 0 && y >= 0 && x < image.width && y < image.height) {
            image.pixels[std::size_t(y * image.width + x)] = c;
        }
    };

    // The trail: a thin film that shifts the picture by a pixel and catches
    // a little light, fading as it dries.
    for (const auto& b : beads_) {
        const unsigned strength = std::min(255u, b.life * 255 / 60);
        const int x = int(std::lround(b.x * sx)), y = int(std::lround(b.y * sy));
        const int half = int(b.w);
        for (int u = x - half; u <= x + half; ++u) {
            const auto film = blend(source(u + 1, y), white, 56);
            put(u, y, blend(source(u, y), film, strength * 220 / 255));
        }
    }

    // The drops: each a small lens showing the picture around it upside
    // down and smaller and brighter, its upper rim in shadow, the light it
    // gathers as a bright crescent along the lower rim, and one glint at
    // the upper left.
    for (const auto& d : drops_) {
        const float cx = d.x * sx, cy = d.y * sy, r = d.r;
        if (r < 1.f) {
            const int x = int(cx), y = int(cy);
            put(x, y, blend(source(x, y - 1), white, 110));
            continue;
        }
        const int x0 = int(std::floor(cx - r)), x1 = int(std::ceil(cx + r));
        const int y0 = int(std::floor(cy - r)), y1 = int(std::ceil(cy + r));
        const int glint_x = int(std::floor(cx - 0.4f * r)),
                  glint_y = int(std::floor(cy - 0.45f * r));
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const float dx = float(x) + 0.5f - cx, dy = float(y) + 0.5f - cy;
                const float dist = std::sqrt(dx * dx + dy * dy) / r;
                if (dist >= 1.f) {
                    continue;
                }
                auto c = source(int(std::lround(cx - dx * 1.6f)),
                                int(std::lround(cy - dy * 1.6f - r * 0.5f)));
                c = blend(c, white, 60);
                if (dist > 0.62f) {
                    c = dy < 0 ? blend(c, black, 110) : blend(c, white, 120);
                }
                if (x == glint_x && y == glint_y) {
                    c = blend(c, white, 170);
                }
                put(x, y, c);
            }
        }
    }
}

} // namespace gg
