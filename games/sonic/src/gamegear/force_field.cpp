// Widescreen arena force field; see force_field.hpp.

#include "gamegear/force_field.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace gg {

namespace {

float smooth(float edge0, float edge1, float x) {
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.f, 1.f);
    return t * t * (3 - 2 * t);
}

} // namespace

void ForceField::advance() {
    ++ticks_;
    for (auto& w : walls_) {
        if (w.release >= 0) {
            ++w.release;
        }
        if (w.flare) {
            --w.flare;
        }
    }
    std::erase_if(walls_, [](const Wall& w) { return w.release >= release_ticks; });
}

ForceField::Wall* ForceField::side(bool right, bool lock, int x) {
    auto held = std::find_if(walls_.begin(), walls_.end(),
                             [&](const Wall& w) { return w.right == right && w.release < 0; });
    Wall* wall = held == walls_.end() ? nullptr : &*held;
    if (lock) {
        if (!wall) {
            walls_.push_back({x, right});
            wall = &walls_.back();
        }
        wall->x = x;
    } else if (wall && wall->x != x) {
        // The limit moved away (boss defeated, capsule or goal reached):
        // release the field with an outward ripple.
        wall->release = 0;
        wall = nullptr;
    }
    if (wall && wall->strength < fade_ticks) {
        ++wall->strength;
    }
    return wall;
}

void ForceField::touch(Wall& wall, int y) {
    if (wall.flare < flare_ticks - 8) {
        wall.flare = flare_ticks;
        wall.flare_y = y;
    }
}

// The force field: a shimmering line with drifting lattice nodes, a tint over
// the area outside the arena, ripples where Sonic or a boss touches it, and an
// outward shockwave when the arena is released.
void ForceField::draw(Screen& image, int view_x, int view_y) const {
    if (walls_.empty()) {
        return;
    }
    const auto source = image.pixels;
    const float t = float(ticks_);
    const int width = image.width, height = image.height;
    auto sample = [&](float x, int y) {
        x = std::clamp(x, 0.f, float(width - 1));
        const int x0 = int(x), x1 = std::min(x0 + 1, width - 1);
        const float f = x - x0;
        const auto a = source[std::size_t(y) * width + x0], b = source[std::size_t(y) * width + x1];
        return std::array{a.r * (1 - f) + b.r * f, a.g * (1 - f) + b.g * f,
                          a.b * (1 - f) + b.b * f};
    };
    for (const auto& w : walls_) {
        const int sx = w.x - view_x;
        const float outward = w.right ? 1.f : -1.f;
        const bool released = w.release >= 0;
        const float p = released ? float(w.release) / release_ticks : 0.f;
        // A held wall on the view's edge shows nothing; a release may ripple
        // into view from just outside it.
        if (!released && (sx <= 0 || sx >= width)) {
            continue;
        }
        if (released && (sx < -160 || sx > width + 160)) {
            continue;
        }
        const float strength = released ? std::pow(1 - p, 2.f) : float(w.strength) / fade_ticks;
        const float front = 6 + p * 150; // Release wavefront distance.
        const float flare_p = 1 - float(w.flare) / flare_ticks;
        const float flare_radius = 3 + flare_p * 40, flare_y = float(w.flare_y - view_y);
        const int reach = released ? int(front) + 12 : std::max(width, 1);
        const int x_begin = std::max(0, released ? sx - reach : 0);
        const int x_end = std::min(width, released ? sx + reach + 1 : width);
        for (int y = 0; y < height; ++y) {
            const float wy = float(y + view_y);
            const float shimmer = std::sin(wy * 0.21f + t * 0.13f);
            const float flare_core =
                w.flare
                    ? std::exp(-std::pow((y - flare_y) / (5 + flare_p * 26), 2.f)) * (1 - flare_p)
                    : 0.f;
            for (int x = x_begin; x < x_end; ++x) {
                const float d = (x - sx) * outward; // Positive outside the arena.
                const float ad = std::abs(d);
                float shift = 0, glow = 0, tint = 0;
                if (!released && ad > 24 && (!w.flare || ad > flare_radius + 8)) {
                    // Far from the field: only the outside tint applies.
                    if (d < 0) {
                        continue;
                    }
                    tint = 0.32f * strength;
                } else if (!released) {
                    tint = smooth(0, 12, d) * 0.32f * strength;
                    if (ad < 8) {
                        shift = shimmer * 2.2f * strength * (1 - ad / 8);
                    }
                    glow = strength *
                           (0.75f * std::exp(-d * d / 4.5f) + 0.16f * std::exp(-d * d / 60.f)) *
                           (0.72f + 0.28f * std::sin(wy * 0.37f - t * 0.21f));
                    // Lattice nodes drifting upward along the field.
                    glow += strength * 0.55f * std::exp(-d * d / 2.f) *
                            std::pow(std::max(0.f, std::cos((wy + t * 0.6f) * 0.4488f)), 14.f);
                    if (w.flare) {
                        const float dist = std::hypot(d, y - flare_y);
                        const float ring =
                            std::exp(-std::pow(dist - flare_radius, 2.f) / 5.f) * (1 - flare_p);
                        glow += ring * 0.9f + flare_core * std::exp(-d * d / 10.f) * 1.1f;
                        shift += ring * 2.5f * (d >= 0 ? 1 : -1);
                    }
                } else {
                    // Outward shockwave on both sides, then the field fades.
                    const float wave =
                        std::exp(-std::pow(ad - front, 2.f) / 20.f) * std::pow(1 - p, 1.5f);
                    shift = wave * 3.5f * (d >= 0 ? outward : -outward);
                    glow = wave * 0.45f + strength * 0.9f * std::exp(-d * d / 4.5f);
                    if (p < 0.12f) {
                        glow += (1 - p / 0.12f) * 1.2f * std::exp(-d * d / 12.f);
                    }
                    tint = smooth(0, 12, d) * 0.32f * strength;
                }
                if (shift == 0 && glow < 0.004f && tint < 0.004f) {
                    continue;
                }
                auto c = shift != 0 ? sample(x + shift, y) : sample(float(x), y);
                if (tint > 0) {
                    c[0] = c[0] * (1 - tint) + (c[0] * 0.55f + 10) * tint;
                    c[1] = c[1] * (1 - tint) + (c[1] * 0.62f + 26) * tint;
                    c[2] = c[2] * (1 - tint) + (c[2] * 0.72f + 64) * tint;
                }
                c[0] += glow * 105;
                c[1] += glow * 205;
                c[2] += glow * 255;
                image.put(x, y,
                          {std::uint8_t(std::clamp(c[0], 0.f, 255.f)),
                           std::uint8_t(std::clamp(c[1], 0.f, 255.f)),
                           std::uint8_t(std::clamp(c[2], 0.f, 255.f)), 255});
            }
        }
    }
}
} // namespace gg
