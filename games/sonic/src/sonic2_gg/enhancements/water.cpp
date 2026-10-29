// Water enhancement (see water.hpp).

#include "sonic2_gg/enhancements/water.hpp"
#include "sonic2_gg/game/game.hpp"
#include "sonic2_gg/render/wide_view.hpp"
#include <algorithm>
#include <cmath>

namespace sonic2_gg {

namespace {

constexpr u8 aqua_lake = 2;
constexpr u8 splash_type = object_type::splash, wave_type = object_type::rising_water;
// rain.cpp: the drops are sprite table entries 2C-33.
constexpr unsigned first_rain_entry = 0x2C, rain_drops = 8;
// Object 0C's wave: the lower halves of its two 8x16 sprites (C8, CA).
constexpr unsigned wave_tiles[2] = {0xC9, 0xCB};

// One pixel of a 4-plane tile row: four bytes, bit 7 is the leftmost pixel.
unsigned row_pixel(const std::uint8_t* row, int x) {
    const int bit = 7 - x;
    return ((row[0] >> bit) & 1) | (((row[1] >> bit) & 1) << 1) | (((row[2] >> bit) & 1) << 2) |
           (((row[3] >> bit) & 1) << 3);
}

gg::Color mix(gg::Color under, gg::Color over, unsigned alpha) {
    auto channel = [&](unsigned top, unsigned bottom) {
        return std::uint8_t((top * alpha + bottom * (255 - alpha) + 127) / 255);
    };
    return {channel(over.r, under.r), channel(over.g, under.g), channel(over.b, under.b), 255};
}

// Bilinear sample of `image` (clamped to it and to rows >= `top`).
gg::Color sample(const std::vector<gg::Color>& image, int width, int height, float x, float y,
                 int top) {
    x = std::clamp(x, 0.f, float(width - 1));
    y = std::clamp(y, float(std::max(top, 0)), float(height - 1));
    const int x0 = int(x), y0 = int(y), x1 = std::min(x0 + 1, width - 1),
              y1 = std::min(y0 + 1, height - 1);
    const float fx = x - float(x0), fy = y - float(y0);
    const auto& a = image[std::size_t(y0 * width + x0)];
    const auto& b = image[std::size_t(y0 * width + x1)];
    const auto& c = image[std::size_t(y1 * width + x0)];
    const auto& d = image[std::size_t(y1 * width + x1)];
    auto channel = [&](auto member) {
        return std::uint8_t(std::lround((a.*member * (1 - fx) + b.*member * fx) * (1 - fy) +
                                        (c.*member * (1 - fx) + d.*member * fx) * fy));
    };
    return {channel(&gg::Color::r), channel(&gg::Color::g), channel(&gg::Color::b), 255};
}

// The picture under the water bends: the same waves as Sonic 1's
// Labyrinth (refract_water), anchored to the map, fading in over the first
// five rows under each surface. `depth` counts the water rows from the
// surface down to each pixel (0 above the water).
void refract(gg::Screen& image, const WaterLayers& layers, const std::vector<int>& depth,
             double phase) {
    const auto original = image.pixels;
    std::vector<float> vertical(std::size_t(image.width));
    for (int x = 0; x < image.width; ++x) {
        vertical[std::size_t(x)] =
            0.7f * float(std::sin((x + layers.view_x) * 0.085 + phase * 0.8));
    }
    for (int y = 0; y < image.height; ++y) {
        const float wave = 1.25f * float(std::sin((y + layers.view_y) * 0.13 + phase));
        for (int x = 0; x < image.width; ++x) {
            const int d = depth[std::size_t(y * image.width + x)];
            if (d <= 0) {
                continue;
            }
            const float fade = std::clamp(float(d - 1) / 5.0f, 0.0f, 1.0f);
            image.put(x, y,
                      sample(original, image.width, image.height, float(x) + wave * fade,
                             float(y) + vertical[std::size_t(x)] * fade, y - d + 1));
        }
    }
}

// Object 0C's wave sprite, made translucent and repeated every 16 pixels
// along a surface, drifting like Sonic 1's surface band. `surface` is the
// first row under the water; the wave's lowest row overlaps it by one, as
// the original sprite does.
void draw_wave(gg::Screen& image, const Game& g, const WaterLayers& layers, int x, int surface,
               std::uint64_t time) {
    const int column = int(((layers.view_x + x) - int(time / 6 % 16)) & 15);
    const unsigned tile = wave_tiles[column / 8];
    for (int r = 0; r < 8; ++r) {
        const int y = surface - 6 + r;
        if (y < 0 || y >= image.height) {
            continue;
        }
        const auto bytes = g.display.tile_row(tile, unsigned(r));
        const unsigned index = row_pixel(bytes.data(), column & 7);
        if (index) {
            auto& pixel = image.pixels[std::size_t(y * image.width + x)];
            pixel = mix(pixel, layers.palettes[std::size_t(y)][16 + index], 128);
        }
    }
}

} // namespace

WaterArt::WaterArt(const Display& display) {
    for (unsigned tile = 0; tile < 512; ++tile) {
        for (int row = 0; row < 8; ++row) {
            const auto row_bytes = display.tile_row(tile, unsigned(row));
            const std::uint8_t* bytes = row_bytes.data();
            unsigned even = 0, odd = 0;
            for (int x = 0; x < 8; ++x) {
                if (row_pixel(bytes, x) == 4) {
                    (x & 1 ? odd : even) |= 1u << x;
                }
            }
            // Exactly every other pixel: 0x55 or 0xAA, nothing else.
            const bool water = (even == 0x55 && !odd) || (odd == 0xAA && !even);
            rows_[tile * 8 + unsigned(row)] = water;
        }
    }
}

bool enhanced_water_act(const Game& g) {
    return wide_play(g) && (g.flow.zone == aqua_lake || rain_act(g));
}

bool replaced_by_water(const Game& g, std::uint8_t owner) {
    return g.flow.zone == aqua_lake && (owner == splash_type || owner == wave_type);
}

bool rain_act(const Game& g) {
    return g.flow.zone == 1 && g.flow.act == 1;
}

bool rain_entry(const Game& g, unsigned entry) {
    return rain_act(g) && entry - first_rain_entry < rain_drops;
}

int water_art_surface(const Game& g, const WaterArt& art, int x, int top, int bottom) {
    auto water = [&](int y) {
        const unsigned entry = map_entry(g, x, y);
        int ty = y & 7;
        if (entry & 0x400) {
            ty = 7 - ty;
        }
        return art.water_row(entry, ty);
    };
    bool above = water(top - 1);
    for (int y = top; y <= bottom; ++y) {
        const bool here = water(y);
        if (here && !above) {
            return y;
        }
        above = here;
    }
    return WaterLayers::none;
}

// --- Splashes --------------------------------------------------------------

void WaterEffects::reset() {
    reset_splash();
    lens_.clear();
}

void WaterEffects::reset_splash() {
    splash_.clear();
    primed_ = false;
    previous_dy_ = 0;
}

void WaterEffects::tick(const Game& g, bool enabled, int view_width, int view_height) {
    ++time_;
    // Rain on the lens while the game's rain (rain.cpp) falls: full while
    // it starts or rains, tapering over the two seconds it stops.
    if (enabled && wide_play(g) && rain_act(g)) {
        const auto& rain = g.rain;
        const float amount = (rain.mode & 3) == 3 ? float(rain.stop_ticks) / 120.f
                             : (rain.mode & 3)    ? 1.f
                                                  : 0.f;
        lens_.step(amount, view_width, view_height, time_);
    } else {
        lens_.clear();
    }
    tick_splash(g, enabled);
}

void WaterEffects::tick_splash(const Game& g, bool enabled) {
    const auto& r = g;
    if (!enabled || !wide_play(g) || r.flow.zone != aqua_lake) {
        reset_splash();
        return;
    }
    // Sonic's X and the line of his feet, in map coordinates (sprites are
    // drawn at their position less the camera; the map at the camera + 17).
    const Object& sonic = r.objects[0];
    const int x = int(sonic.x), y = int(sonic.y) + 17;
    // D442, set by 4948 (Sonic below the act's water line), is the game's
    // own idea of being in the water; 4948 makes its splash when it is set.
    const bool wet = r.water.underwater != 0;
    // Act starts, restarts and teleports must never look like a crossing.
    if (primed_ && (zone_ != r.flow.zone || act_ != r.flow.act || std::abs(x - x_) > 32 ||
                    std::abs(y - y_) > 32)) {
        reset();
    }
    splash_.step();
    const int dy = primed_ ? y - y_ : 0;
    // Moving through the surface the way the flag changed (this update or
    // the last: a landing can stop him on the update that reports it).
    // Rising water that swallows him standing still makes no splash.
    const bool moving_through = wet ? (dy > 0 || previous_dy_ > 0) : (dy < 0 || previous_dy_ < 0);
    if (primed_ && wet != wet_ && moving_through) {
        // Act 2: the line interrupt changes the colours from the line
        // after D47F - camera Y, i.e. map Y D47F + 18. Otherwise the
        // dithered water's surface in Sonic's column.
        const int surface = r.flow.act == 1
                                ? int(r.water.rise_y) + 18
                                : water_art_surface(g, WaterArt(g.display), x, y - 48, y + 16);
        if (surface != WaterLayers::none && std::abs(y - surface) < 48) {
            splash_.burst(x, surface, x - x_, wet, std::uint32_t(time_));
        }
    }
    previous_dy_ = dy;
    primed_ = true;
    wet_ = wet;
    zone_ = r.flow.zone;
    act_ = r.flow.act;
    x_ = x;
    y_ = y;
}

// --- The layer over the picture ----------------------------------------------

void draw_water(gg::Screen& image, const Game& g, const WaterLayers& layers,
                const WaterEffects& effects) {
    const int width = image.width, height = image.height;
    const std::uint64_t time = effects.time();
    if (g.flow.zone == aqua_lake) {
        // Depth under the surface for every pixel.
        std::vector<int> depth(std::size_t(width * height));
        if (layers.surface_row != WaterLayers::none) {
            for (int y = 0; y < height; ++y) {
                const int d = std::max(0, y - layers.surface_row + 1);
                std::fill_n(depth.begin() + y * width, width, d);
            }
        } else {
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const auto i = std::size_t(y * width + x);
                    if (layers.flags[i] & WaterLayers::water_pixel) {
                        // Water continuing above the image counts as deep.
                        depth[i] = y ? depth[i - std::size_t(width)] + 1 : 8;
                    }
                }
            }
        }

        // Act 2's wave (object 0C) along the whole rising surface, then the
        // bending (which carries its lowest rows along, as with Sonic 1's
        // band). The dithered water of acts 1 and 3 has no wave sprite, so
        // none is added there.
        if (layers.surface_row != WaterLayers::none) {
            for (int x = 0; x < width; ++x) {
                draw_wave(image, g, layers, x, layers.surface_row, time);
            }
        }
        refract(image, layers, depth, double(time) * 0.045);
        effects.splash().draw(image, layers.view_x, layers.view_y);
    }
}

} // namespace sonic2_gg
