// Water splash droplets (see water_splash.hpp).

#include "gamegear/water_splash.hpp"
#include "gamegear/water_droplet.hpp"
#include <algorithm>
#include <cmath>

namespace gg {
namespace {
void blend(Screen& image, int x, int y, Color color, unsigned alpha) {
    if (x < 0 || y < 0 || x >= image.width || y >= image.height) {
        return;
    }
    auto& under = image.pixels[y * image.width + x];
    alpha = alpha * color.a / 255;
    auto channel = [&](unsigned top, unsigned bottom) {
        return std::uint8_t((top * alpha + bottom * (255 - alpha) + 127) / 255);
    };
    under = {channel(color.r, under.r), channel(color.g, under.g), channel(color.b, under.b), 255};
}

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
} // namespace

void WaterSplash::step() {
    for (auto& p : particles_) {
        if (++p.age <= p.delay) {
            continue;
        }
        p.x += p.vx;
        p.y += p.vy;
        p.vx *= 0.97f;
        p.vy += 0.13f;
    }
    // Expire, or let falling droplets disappear back into the water surface.
    std::erase_if(particles_, [](const auto& p) {
        return p.age >= p.life || (p.age > p.delay && p.vy > 0 && p.y > p.surface + 1);
    });
}

void WaterSplash::burst(int x, int surface, int drift_pixels, bool wet, std::uint32_t ticks) {
    // Two balanced but irregular fans. Each droplet gets its own hashed
    // launch angle, speed, ejection delay, size and lifetime, so the spray
    // reads as a burst rather than two straight diagonal lines. Horizontal
    // motion through the surface skews the spray.
    if (particles_.size() > 3 * burst_size) {
        particles_.erase(particles_.begin(), particles_.end() - 3 * burst_size);
    }
    const std::uint32_t seed = mix(ticks * 0x9e3779b9u ^ std::uint32_t(x) * 0x85ebca6bu ^
                                   std::uint32_t(surface) * 0xc2b2ae35u ^ unsigned(wet));
    const float drift = std::clamp(float(drift_pixels), -4.f, 4.f) * 0.25f;
    for (unsigned i = 0; i < burst_size; ++i) {
        const std::uint32_t h = mix(seed + i * 0x27d4eb2fu);
        const float side = (i & 1) ? 1.f : -1.f;
        // Angle from vertical: a couple of near-vertical column drops,
        // the rest spread between roughly 14 and 72 degrees.
        const float angle = i < 2 ? 0.05f + 0.2f * unit(h) : 0.25f + 1.0f * unit(h);
        const float speed =
            (wet ? 2.4f : 2.8f) * (0.6f + 0.7f * unit(h >> 10)) * (1.f - 0.25f * (angle - 0.25f));
        // Per side: two full drops, three small drops and three droplets.
        constexpr unsigned sizes[burst_size / 2] = {2, 1, 0, 2, 1, 0, 1, 0};
        particles_.push_back({float(x) + side * (1.f + 4.f * unit(h >> 5)),
                              float(surface) - 2.f * unit(h >> 15),
                              side * std::sin(angle) * speed + drift, -std::cos(angle) * speed, 0,
                              (h >> 20) % 4, 0, sizes[i / 2], float(surface)});
        auto& p = particles_.back();
        p.life = p.delay + 20 + (h >> 23) % 14;
    }
}

void WaterSplash::draw(Screen& image, int cx, int cy) const {
    for (const auto& p : particles_) {
        if (p.age < p.delay) {
            continue;
        }
        const unsigned left = p.life - p.age;
        const unsigned opacity = left > 8 ? 230 : left * 230 / 8;
        if (p.size == 0) {
            blend(image, int(std::lround(p.x)) - cx, int(std::lround(p.y)) - cy,
                  {220, 245, 255, 255}, opacity);
            continue;
        }
        const int w = p.size == 1 ? 2 : 4, h = p.size == 1 ? 3 : 6;
        int x = int(std::lround(p.x)) - cx - w / 2, y = int(std::lround(p.y)) - cy - h / 2;
        for (int dy = 0; dy < h; ++dy) {
            for (int dx = 0; dx < w; ++dx) {
                int sx = dx * 8 / w;
                if (p.vx > 0) {
                    sx = 7 - sx;
                }
                blend(image, x + dx, y + dy, water_droplet[(dy * 12 / h) * 8 + sx], opacity);
            }
        }
    }
}

} // namespace gg
