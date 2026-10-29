// Waterfall and basin enhancement (Green Hill and Jungle).
//
// The original animates its waterfalls and pools by cycling palette colours.
// This enhancement classifies, per 8x8 tile, which pixels of the level art are
// water (64-bit masks per tile, built once per act from the decoded tiles and
// blocks) and redraws those pixels as translucent water with flowing streaks,
// foam, mist and ripples. Dry rims and logs keep their original pixels. It is
// purely an overlay: collision, layout, priorities and game state are unchanged.

#include "sonic1_gg/enhancements/basin_water.hpp"
#include "sonic1_gg/enhancements/mist_spray.hpp"
#include <algorithm>
#include <cmath>

namespace sonic1_gg {
namespace {
int dry_log(unsigned tile) {
    switch (tile) {
    case 233:
        return 107;
    case 234:
        return 108;
    case 235:
        return 109;
    case 249:
        return 123;
    case 250:
        return 124;
    case 251:
        return 125;
    case 242:
        return 220;
    case 243:
        return 140;
    case 161:
        return 141;
    default:
        return -1;
    }
}

unsigned tile_at(const Level& level, int x, int y) {
    if (x < 0 || y < 0 || x >= level.columns * 32 || y >= level.rows * 32) {
        return 0;
    }
    unsigned block = level.block_at(x, y);
    return level.blocks[block * 16 + (y & 31) / 8 * 4 + (x & 31) / 8];
}

void blend(gg::Screen& image, int x, int y, platform::Color color, unsigned alpha) {
    if (x < 0 || y < 0 || x >= image.width || y >= image.height) {
        return;
    }
    auto& under = image.pixels[y * image.width + x];
    auto channel = [&](unsigned a, unsigned b) {
        return std::uint8_t((a * alpha + b * (255 - alpha) + 127) / 255);
    };
    under = {channel(color.r, under.r), channel(color.g, under.g), channel(color.b, under.b), 255};
}

platform::Color mix_color(platform::Color under, platform::Color over, unsigned alpha) {
    auto channel = [&](unsigned a, unsigned b) {
        return std::uint8_t((a * alpha + b * (255 - alpha) + 127) / 255);
    };
    return {channel(over.r, under.r), channel(over.g, under.g), channel(over.b, under.b), 255};
}

// Time-of-flight coordinate of falling water d pixels below its top: speed
// starts at 1 px/tick, accelerates and saturates at 3.2 px/tick. A pattern
// f(flight(d) - ticks) therefore moves down, stretching as it speeds up.
constexpr float fall_v0 = 1.f, fall_g = .06f, fall_vmax = 3.2f;

float flight(float d) {
    const float d1 = (fall_vmax * fall_vmax - fall_v0 * fall_v0) / (2 * fall_g);
    if (d < d1) {
        return (std::sqrt(fall_v0 * fall_v0 + 2 * fall_g * d) - fall_v0) / fall_g;
    }
    return (fall_vmax - fall_v0) / fall_g + (d - d1) / fall_vmax;
}

float fall_speed(float d) {
    return std::min(fall_vmax, std::sqrt(fall_v0 * fall_v0 + 2 * fall_g * std::max(0.f, d)));
}

// Basin body tint. GH: bright blue. Jungle: dark navy close to its original
// water (palette 9 = GG 0x700 over the 0/9 dither with forest colour 0x030).
struct Tint {
    platform::Color color;
    unsigned alpha;
};

Tint basin_tint(unsigned scene) {
    return scene == 2 ? Tint{{6, 31, 121, 255}, 200} : Tint{{51, 170, 255, 255}, 168};
}

// Flow streak "ropes", mostly two pixels wide, wobbling gently. In flow
// coordinates their bright heads and darker channels accelerate and lengthen
// as the water falls. strength scales them for the fade-in from a basin.
std::uint32_t mix(std::uint32_t v);
float unit(std::uint32_t v);
platform::Color flow_streaks(platform::Color c, int wx, float phase, unsigned scene,
                             float strength);

// Jungle basin blocks 129-155 (155 is a solid block of wet rock 250).
// Falling-water patterns of this ROM, identified by inspecting the decoded art.
// GH: 137 overflow, 184 column, 136 impact foam. Jungle: 2/3/4/20 sheets;
// 9/18/19 crests, 10/25/26 thin streams and 12/28 streams over rock, where
// only the palette-cycled water indices 10-12 and white 15 are water.
// Jungle water also runs through art drawn over the falls: the spike log
// (212/213/236/237), hanging platforms (206/207/238 tops, 222/223/239
// alternating columns), plants (147/149/152), log corners (218/219), Jungle 2
// log posts (232/247/248) and the basin-drain step 170. There white 15 is spike/log shine, so only
// the cycled indices 10-12 are water.
bool fall_pixel(unsigned scene, unsigned tile, unsigned index) {
    if (scene == 0) {
        return tile == 184 || tile == 136 || tile == 137;
    }
    switch (tile) {
    case 2:
    case 3:
    case 4:
    case 20:
        return true;
    case 9:
    case 18:
    case 19:
        return (index >= 10 && index <= 12) || index == 15;
    case 147:
    case 149:
    case 152:
    case 206:
    case 207:
    case 212:
    case 213:
    case 218:
    case 219:
    case 232:
    case 236:
    case 237:
    case 238:
    case 247:
    case 248:
        return index >= 10 && index <= 12;
    default:
        return false;
    }
}

platform::Color flow_streaks(platform::Color c, int wx, float phase, unsigned scene,
                             float strength) {
    const int rope = wx + int(std::lround(.7f * std::sin(phase * .22f + wx * .17f)));
    const std::uint32_t column = mix(std::uint32_t(rope >> 1) * 0x9e3779b9u ^ scene);
    const float period = 14.f + float(column % 16), duty = .18f + .22f * unit(column >> 16);
    float stripe = phase / period + unit(column >> 8) + ((rope & 1) ? .04f : 0.f);
    stripe -= std::floor(stripe);
    c = mix_color(c, {120, 210, 255, 255}, unsigned(48 * strength * unit(column >> 22)));
    if (stripe < duty) {
        c = mix_color(c, {205, 242, 255, 255},
                      unsigned(170 * strength * std::pow(1 - stripe / duty, .6f)));
    } else if (stripe > .52f && stripe < .72f) {
        c = mix_color(c, {10, 90, 190, 255}, unsigned(70 * strength));
    }
    return c;
}

// Jungle stripe curtains: alternating water/gap columns that the original
// uses as transparency (10/25/26 over scenery, 12/28 and 222/223/239 over
// rock). The whole tile is rendered as translucent water over its gaps.
// 170 is the basin drain step, whose 0/9 dither rows interleave with the
// streams; as a curtain it fades in from the basin above like any fed spill.
bool curtain_tile(unsigned scene, unsigned tile) {
    return scene == 2 && (tile == 10 || tile == 12 || tile == 25 || tile == 26 || tile == 28 ||
                          tile == 170 || tile == 222 || tile == 223 || tile == 239);
}

// Small integer hash for deterministic, RNG-independent cosmetic variation.
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

// Draws one mist curl (art/enhancements/mist-spray.png, embedded in
// mist_spray.hpp). Quarter-turn frames and the spin direction's mirror make
// each curl visibly tumble as it travels; the soft edge is drawn at 60%.
void draw_spray(gg::Screen& image, int kind, int rotation, bool mirror, int x, int y,
                unsigned alpha) {
    const int size = spray_size[kind], half = size / 2;
    for (int sy = 0; sy < size; ++sy) {
        for (int sx = 0; sx < size; ++sx) {
            int u = mirror ? size - 1 - sx : sx, v = sy;
            for (int r = 0; r < rotation; ++r) {
                int w = u;
                u = size - 1 - v;
                v = w;
            }
            char c = spray_art[kind][v][u];
            if (c == '2') {
                blend(image, x + sx - half, y + sy - half, {236, 250, 255, 255}, alpha);
            } else if (c == '1') {
                blend(image, x + sx - half, y + sy - half, {180, 228, 255, 255}, alpha * 3 / 5);
            }
        }
    }
}

// Faint flickering haze hugging the impact line, densest under the waterfall
// and thinning out over the basin past its edges.
void draw_haze(gg::Screen& image, BasinWater::Outlet outlet, int reach, float now, int cx, int cy) {
    for (int dy = 0; dy < 10; ++dy) {
        for (int px = -reach; px <= reach; ++px) {
            float across = 1 - std::abs(float(px)) / float(reach);
            float base = across * std::sqrt(across) * (1 - dy / 10.f);
            float flicker = .6f + .4f * std::sin(px * .7f + now * .19f) *
                                      std::sin(dy * 1.3f - now * .13f + px * .21f);
            unsigned alpha = unsigned(150 * base * flicker);
            if (alpha > 4) {
                blend(image, outlet.x - cx + px, outlet.y - cy - 1 - dy, {214, 242, 255, 255},
                      alpha);
            }
        }
    }
}
} // namespace

BasinWater::BasinWater(const Level& level) : scene_(level.scene) {
    if (scene_ != 0 && scene_ != 2) {
        return;
    }
    width_ = level.columns * 4;
    height_ = level.rows * 4;
    masks_.resize(std::size_t(width_) * height_);
    keep_.resize(masks_.size());
    // IDs refer to the decoded pattern and block data of this ROM.
    for (int ty = 0; ty < height_; ++ty) {
        for (int tx = 0; tx < width_; ++tx) {
            int x = tx * 8, y = ty * 8;
            unsigned block = level.block_at(x, y), tile = tile_at(level, x, y);
            bool basin = false, surface = false, curved = false;
            if (scene_ == 0 && ((block >= 153 && block <= 164) || block == 167 || block == 168)) {
                // 137 is the overflow where the pool starts to spill; it belongs to the fall.
                basin = tile == 229 || tile == 245 || tile == 134 || tile == 135 || tile == 246 ||
                        tile == 247;
                surface = tile == 229;
                curved = tile == 134 || tile == 135 || tile == 246 || tile == 247;
            } else if (scene_ == 2 && block >= 129 && block <= 155 && block != 132 &&
                       block != 134 && block != 135 && block != 136 && block != 148 &&
                       block != 149) {
                basin = tile == 1 || tile == 5 || tile == 6 || tile == 7 || tile == 8 ||
                        tile == 21 || tile == 22 || tile == 23 || tile == 24 || dry_log(tile) >= 0;
                surface =
                    tile == 5 || tile == 6 || tile == 7 || tile == 8 || tile == 23 || tile == 24;
            }
            if (!basin) {
                continue;
            }
            auto& mask = masks_[ty * width_ + tx];
            // Jungle's surface band keeps its original wave pixels (palette-cycled
            // indices 10-12 and white 15); only the dark water around them changes.
            const bool waves =
                scene_ == 2 && ((tile >= 5 && tile <= 8) || (tile >= 21 && tile <= 24));
            if (waves) {
                for (int py = 0; py < 8; ++py) {
                    for (int px = 0; px < 8; ++px) {
                        unsigned index = gg::tile_pixel(level.background_patterns, tile, px, py);
                        if ((index >= 10 && index <= 12) || index == 15) {
                            keep_[ty * width_ + tx] |= std::uint64_t(1) << (py * 8 + px);
                        }
                    }
                }
            }
            for (int py = 0; py < 8; ++py) {
                for (int px = 0; px < 8; ++px) {
                    unsigned index = gg::tile_pixel(level.background_patterns, tile, px, py);
                    // GH rim pixels 3/13 are gold; only remove checker pixels 0/4.
                    if (!curved || index == 0 || index == 4) {
                        mask |= std::uint64_t(1) << (py * 8 + px);
                    }
                }
            }
            if (surface) {
                if (!surfaces_.empty() && surfaces_.back().y == y &&
                    surfaces_.back().x + surfaces_.back().width == x) {
                    surfaces_.back().width += 8;
                } else {
                    surfaces_.push_back({x, y, 8});
                }
            }
        }
    }
    // Mist sources are geometric contacts between a decoded waterfall and a
    // basin surface, not hard-coded world positions or a timer in game logic.
    for (auto surface : surfaces_) {
        int run_start = -1;
        for (int x = surface.x; x <= surface.x + surface.width; x += 8) {
            auto tile = tile_at(level, x + 4, surface.y - 4);
            bool falling = x < surface.x + surface.width &&
                           (scene_ == 0 ? (tile == 184 || tile == 136)
                                        : (tile == 2 || tile == 3 || tile == 4 || tile == 9 ||
                                           tile == 25 || tile == 10 || tile == 26));
            if (falling && run_start < 0) {
                run_start = x;
            }
            if (!falling && run_start >= 0) {
                outlets_.push_back({(run_start + x) / 2, surface.y, x - run_start});
                run_start = -1;
            }
        }
    }
    // Waterfall coverage per 8x8 tile, with the block's priority attribute.
    fall_index_.assign(std::size_t(width_) * height_, -1);
    for (int ty = 0; ty < height_; ++ty) {
        for (int tx = 0; tx < width_; ++tx) {
            int x = tx * 8, y = ty * 8;
            if (level.tile_overrides.contains(ty * level.columns * 4 + tx)) {
                continue;
            }
            unsigned block = level.block_at(x, y), tile = tile_at(level, x, y);
            FallTile fall;
            if (curtain_tile(scene_, tile)) {
                fall.mask = fall.curtain = ~std::uint64_t(0);
            } else {
                for (int py = 0; py < 8; ++py) {
                    for (int px = 0; px < 8; ++px) {
                        if (fall_pixel(scene_, tile,
                                       gg::tile_pixel(level.background_patterns, tile, px, py))) {
                            fall.mask |= std::uint64_t(1) << (py * 8 + px);
                        }
                    }
                }
            }
            // Hanging-platform crests (206/207/238): their upper rows are the
            // platform tips; rows 4-7 interleave rock and water like the curtain
            // beneath them, so they become curtain too.
            if (scene_ == 2 && (tile == 206 || tile == 207 || tile == 238)) {
                fall.curtain = ~std::uint64_t(0) << 32;
                fall.mask |= fall.curtain;
            }
            if (!fall.mask) {
                continue;
            }
            fall.priority = (level.block_attributes[block] & 0x80) != 0;
            fall_index_[ty * width_ + tx] = std::int32_t(falls_.size());
            falls_.push_back(fall);
        }
    }
    if (falls_.empty()) {
        return;
    }
    // Vertical runs: their top drives the accelerating flow coordinate and
    // their bottom the impact foam. Body edges get highlights and droplets.
    auto wet = [&](int x, int y) {
        return fall_contains(x, y);
    };
    for (int x = 0; x < width_ * 8; ++x) {
        int top = -1;
        int edge_top[2] = {-1, -1};
        for (int y = 0; y <= height_ * 8; ++y) {
            bool here = y < height_ * 8 && wet(x, y);
            if (here && top < 0) {
                top = y;
            }
            if (!here && top >= 0) {
                for (int yy = top; yy < y; ++yy) {
                    auto& tile = falls_[fall_index_[(yy / 8) * width_ + x / 8]];
                    tile.top[(yy & 7) * 8 + (x & 7)] = top;
                    tile.bottom[(yy & 7) * 8 + (x & 7)] = y - 1;
                }
                top = -1;
            }
            // An edge belongs to a body at least four pixels wide, so the
            // curtains of one-pixel Jungle streams do not all light up.
            for (int side = 0; side < 2; ++side) {
                const int d = side ? 1 : -1;
                bool edge = here && !wet(x + d, y) && wet(x - d, y) && wet(x - 2 * d, y) &&
                            wet(x - 3 * d, y);
                if (edge && edge_top[side] < 0) {
                    edge_top[side] = y;
                }
                if (!edge && edge_top[side] >= 0) {
                    if (y - edge_top[side] >= 12) {
                        fall_edges_.push_back({x, edge_top[side], y - 1, d});
                    }
                    edge_top[side] = -1;
                }
            }
        }
    }
    // Falls spilling out of a basin directly above fade in from the basin look.
    // GH2 authors that spill as staggered 137 tiles; align each fed pixel to
    // the highest fed top within 12 pixels so the fade crosses the stagger as
    // one smooth line rather than repeating its notches.
    std::vector<std::tuple<std::size_t, int, int>> spill;
    for (int ty = 0; ty < height_; ++ty) {
        for (int tx = 0; tx < width_; ++tx) {
            int index = fall_index_[ty * width_ + tx];
            if (index < 0) {
                continue;
            }
            for (int bit = 0; bit < 64; ++bit) {
                if (!((falls_[index].mask >> bit) & 1)) {
                    continue;
                }
                int x = tx * 8 + (bit & 7), y = ty * 8 + bit / 8, top = falls_[index].top[bit];
                if (!contains(x, top - 1)) {
                    continue;
                }
                falls_[index].fed |= std::uint64_t(1) << bit;
                int line = top;
                for (int dx = -12; dx <= 12; ++dx) {
                    auto other = fall_tile(x + dx, y);
                    int other_bit = (y & 7) * 8 + ((x + dx) & 7);
                    if (other && ((other->mask >> other_bit) & 1) &&
                        contains(x + dx, other->top[other_bit] - 1)) {
                        line = std::min(line, other->top[other_bit]);
                    }
                }
                spill.emplace_back(std::size_t(index), bit, line);
            }
        }
    }
    for (auto [index, bit, line] : spill) {
        falls_[index].top[bit] = line;
    }
}

const BasinWater::FallTile* BasinWater::fall_tile(int x, int y) const {
    if (x < 0 || y < 0 || x >= width_ * 8 || y >= height_ * 8 || fall_index_.empty()) {
        return nullptr;
    }
    int index = fall_index_[(y / 8) * width_ + x / 8];
    return index < 0 ? nullptr : &falls_[index];
}

bool BasinWater::replaces(int x, int y) const {
    return contains(x, y) && !((keep_[(y / 8) * width_ + x / 8] >> ((y & 7) * 8 + (x & 7))) & 1);
}

bool BasinWater::fall_contains(int x, int y) const {
    auto tile = fall_tile(x, y);
    return tile && ((tile->mask >> ((y & 7) * 8 + (x & 7))) & 1);
}

bool BasinWater::contains(int x, int y) const {
    if (x < 0 || y < 0 || x >= width_ * 8 || y >= height_ * 8) {
        return false;
    }
    return (masks_[(y / 8) * width_ + x / 8] >> ((y & 7) * 8 + (x & 7))) & 1;
}

int BasinWater::surface_at(int x, int y) const {
    // Find the exposed top of the water column near an immersed probe.
    for (auto surface : surfaces_) {
        if (x >= surface.x && x < surface.x + surface.width && y >= surface.y) {
            bool connected = true;
            for (int yy = surface.y; yy <= y; yy += 8) {
                if (!contains(x, yy)) {
                    connected = false;
                    break;
                }
            }
            if (connected) {
                return surface.y;
            }
        }
    }
    return -1;
}

unsigned BasinWater::foreground_index(const Level& level, int x, int y) const {
    int tile = scene_ == 2 ? dry_log(tile_at(level, x, y)) : -1;
    return tile < 0 ? 0 : gg::tile_pixel(level.background_patterns, unsigned(tile), x & 7, y & 7);
}

unsigned BasinWater::background_index(const Level& level, int x, int y) const {
    // Continue the imported red vertical stripes (pattern 201, as in the
    // scenery above GH basins), aligned in world coordinates under refraction.
    if (scene_ == 2) {
        return foreground_index(level, x, y);
    }
    return gg::tile_pixel(level.background_patterns, 201, x & 7, y & 7);
}

void BasinWater::draw_falls(gg::Screen& image, const Game& game, int cx, int cy,
                            bool priority_only) const {
    if (falls_.empty()) {
        return;
    }
    const auto& level = game.level;
    // The live palette carries the ROM's 3-phase cycle of indices 10-12,
    // which is how the original animates its waterfalls.
    const auto palette = gg::decode_palette(level.palette);
    const float now = float(game.ticks);
    auto original_index = [&](int wx, int wy) {
        return gg::tile_pixel(level.background_patterns, tile_at(level, wx, wy), wx & 7, wy & 7);
    };
    auto is_water = [](unsigned index) {
        return (index >= 10 && index <= 12) || index == 15;
    };
    // Scenery behind a Jungle stripe curtain: the mean of the gap pixels
    // within two pixels in the same row. Averaging keeps differently coloured
    // rock gaps (platform undersides) from showing through as new stripes.
    auto scenery = [&](int wx, int wy) {
        unsigned r = 0, g = 0, b = 0, n = 0;
        for (int dx = -2; dx <= 2; ++dx) {
            unsigned index = original_index(wx + dx, wy);
            if (is_water(index)) {
                continue;
            }
            auto c = palette[index];
            r += c.r;
            g += c.g;
            b += c.b;
            ++n;
        }
        if (!n) {
            return palette[0];
        }
        return platform::Color{std::uint8_t(r / n), std::uint8_t(g / n), std::uint8_t(b / n), 255};
    };
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const int wx = x + cx, wy = y + cy;
            auto tile = fall_tile(wx, wy);
            const int bit = (wy & 7) * 8 + (wx & 7);
            if (!tile || !((tile->mask >> bit) & 1) || (priority_only && !tile->priority)) {
                continue;
            }
            const bool curtain = (tile->curtain >> bit) & 1;
            const int top = tile->top[bit], bottom = tile->bottom[bit];
            const float d = float(wy - top), phase = flight(d) - now;
            // Distortion travels down with the flow, unlike the basin's sway.
            const float bend =
                1.4f * std::sin(phase * .55f + wx * .21f) + .6f * std::sin(phase * 1.3f + wx * .5f);
            int sx = wx + int(std::lround(bend));
            if (!fall_contains(sx, wy)) {
                sx = wx;
            }
            auto shade = [&](platform::Color c) {
                // Sheets are aerated and nearly opaque; stripe curtains are the
                // original's transparency, so the scenery stays visible through them.
                c = mix_color(c, {51, 170, 255, 255}, curtain ? 150 : 205);
                // Keep the original's palette-cycled streak pixels (indices 10-12/15)
                // as a faint layer on sheets; its 0/4 checker dither is replaced.
                const unsigned original = original_index(wx, wy);
                if (!curtain && original >= 10) {
                    c = mix_color(c, palette[original], 56);
                }
                c = flow_streaks(c, wx, phase, scene_, curtain ? .75f : 1);
                // Shimmering highlights along the sides of bodies at least two wide.
                for (int side : {-1, 1}) {
                    if (!fall_contains(wx + side, wy) && fall_contains(wx - side, wy)) {
                        c = mix_color(
                            c, {225, 250, 255, 255},
                            unsigned(std::clamp(150.f + 90.f * std::sin(phase * .8f + float(wx)),
                                                40.f, 230.f)));
                    }
                }
                // Bright lip where water tips over a ledge; a fall spilling out of a
                // basin instead fades in from the basin water over 18 pixels.
                const bool fed = (tile->fed >> bit) & 1;
                if (!fed && d < 6) {
                    c = mix_color(c, {235, 250, 255, 255}, unsigned(130 * (1 - d / 6)));
                }
                if (fed && d < 18) {
                    float w = std::max(0.f, d) / 18;
                    w = w * w * (3 - 2 * w);
                    const auto tint = basin_tint(scene_);
                    platform::Color pool = mix_color(palette[background_index(level, wx, wy) & 31],
                                                     tint.color, tint.alpha);
                    // Continue the pool's lead-in streaks (strength 0.8 at the line).
                    pool = flow_streaks(pool, wx, phase, scene_, .8f);
                    c = mix_color(pool, c, unsigned(255 * w));
                }
                // Churning foam where the column lands in a basin.
                const int remaining = bottom - wy;
                if (remaining < 5 && (contains(wx, bottom + 1) || contains(wx, bottom + 2) ||
                                      contains(wx, bottom + 3))) {
                    const std::uint32_t n =
                        mix(std::uint32_t(wx) * 0x27d4eb2fu ^ std::uint32_t(wy) * 0x165667b1u ^
                            (game.ticks / 3) * 0x85ebca6bu);
                    if (unit(n) < .25f + .55f * (1 - remaining / 5.f)) {
                        c = mix_color(c, {240, 252, 255, 255}, 220);
                    }
                }
                return c;
            };
            const platform::Color behind =
                curtain ? scenery(sx, wy) : palette[background_index(level, sx, wy) & 31];
            const platform::Color water = shade(behind);
            auto& pixel = image.pixels[y * image.width + x];
            if (priority_only && curtain) {
                // A sprite drawn after the background pass is seen through the
                // curtain, as through the original's stripes, rather than hidden.
                if (pixel.r != water.r || pixel.g != water.g || pixel.b != water.b) {
                    image.put(x, y, shade(pixel));
                }
                continue;
            }
            image.put(x, y, water);
        }
    }
}

void BasinWater::draw_fall_particles(gg::Screen& image, const Game& game, int cx, int cy) const {
    // Stateless like the mist: deterministic in ticks and geometry, pause-safe,
    // no gameplay RNG. Some lifetimes stay empty so emission looks irregular.
    for (auto e : fall_edges_) {
        if (e.x < cx - 16 || e.x > cx + image.width + 16 || e.bottom < cy - 8 ||
            e.top > cy + image.height + 8) {
            continue;
        }
        const std::uint32_t seed = mix(std::uint32_t(e.x) * 73856093u ^
                                       std::uint32_t(e.top) * 19349663u ^ unsigned(e.side + 1));
        const int span = e.bottom - e.top - 6;
        if (span < 4) {
            continue;
        }
        // Droplets peel off the sides of the body and fall faster than it.
        for (int slot = 0; slot < 2 + span / 14; ++slot) {
            const std::uint32_t base = mix(seed + unsigned(slot) * 0x27d4eb2fu);
            const unsigned life = 18 + base % 18, local = game.ticks + (base >> 8) % life,
                           age = local % life;
            const std::uint32_t h = mix(base ^ (local / life) * 0x85ebca6bu);
            if (unit(h >> 20) < .4f) {
                continue;
            }
            const float y0 = float(e.top) + unit(h) * float(span),
                        v = fall_speed(y0 - float(e.top));
            const float a = float(age);
            const float x = float(e.x) + float(e.side) * (1 + (.12f + .3f * unit(h >> 4)) * a);
            const float y = y0 + v * a + .06f * a * a;
            if (y > float(e.bottom) + 1) {
                continue;
            }
            const unsigned alpha = unsigned(230 * std::min(1.f, float(life - age) / 6.f));
            const int px = int(std::lround(x)) - cx, py = int(std::lround(y)) - cy;
            blend(image, px, py, {225, 250, 255, 255}, alpha);
            if (v > 1.8f) {
                blend(image, px, py - 1, {180, 232, 255, 255}, alpha / 2);
            }
        }
    }
    // Spray thrown up from each impact, falling back into the basin.
    for (auto o : outlets_) {
        if (o.x + o.width < cx - 8 || o.x - o.width > cx + image.width + 8 || o.y < cy - 4 ||
            o.y - 40 > cy + image.height) {
            continue;
        }
        const std::uint32_t seed =
            mix(std::uint32_t(o.x) * 0x9e3779b9u ^ std::uint32_t(o.y) * 0x51ed27u);
        for (int slot = 0; slot < 6 + o.width / 6; ++slot) {
            const std::uint32_t base = mix(seed + unsigned(slot) * 0x165667b1u);
            const unsigned life = 22 + base % 16, local = game.ticks + (base >> 8) % life,
                           age = local % life;
            const std::uint32_t h = mix(base ^ (local / life) * 0xc2b2ae35u);
            if (unit(h >> 20) < .3f) {
                continue;
            }
            const float a = float(age);
            const float x =
                float(o.x) + (unit(h) - .5f) * float(o.width) + (unit(h >> 5) - .5f) * 1.8f * a;
            const float y = float(o.y) - 2 - (.9f + 1.5f * unit(h >> 10)) * a + .065f * a * a;
            if (y > float(o.y)) {
                continue;
            }
            const unsigned alpha = unsigned(220 * std::min(1.f, float(life - age) / 6.f));
            const int px = int(std::lround(x)) - cx, py = int(std::lround(y)) - cy;
            blend(image, px, py, {236, 250, 255, 255}, alpha);
            if (unit(h >> 15) < .35f) {
                blend(image, px + 1, py, {180, 228, 255, 255}, alpha * 3 / 5);
                blend(image, px, py + 1, {180, 228, 255, 255}, alpha * 3 / 5);
            }
        }
    }
}

void BasinWater::draw(gg::Screen& image, const Game& game, int cx, int cy) const {
    if (empty()) {
        return;
    }
    const auto original = image.pixels;
    const double time = game.ticks * 0.045;
    const float now = float(game.ticks);
    std::vector<Outlet> near;
    for (auto o : outlets_) {
        if (o.x + o.width / 2 + 72 >= cx && o.x - o.width / 2 - 72 <= cx + image.width &&
            o.y + 24 >= cy && o.y - 4 <= cy + image.height) {
            near.push_back(o);
        }
    }
    // Sample at logical resolution only. Never distort the GPU-sized output.
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            int wx = x + cx, wy = y + cy;
            if (!replaces(wx, wy)) {
                continue;
            }
            // Turbulence: refraction grows and quickens just below an impact.
            float churn = 0;
            for (auto o : near) {
                if (wy >= o.y && wy < o.y + 24) {
                    float dist = std::max(0.f, std::abs(float(wx - o.x)) - o.width * .5f);
                    churn += 1.4f * std::exp(-dist / 16.f) * (1 - float(wy - o.y) / 24.f);
                }
            }
            churn = std::min(churn, 1.4f);
            float sx = std::clamp(float(x + 1.25 * (1 + churn) * std::sin(wy * .13 + time) +
                                        churn * .9f * std::sin(wy * .55f + now * .35f + wx * .3f)),
                                  0.f, float(image.width - 1));
            float sy = std::clamp(float(y + .7 * (1 + churn) * std::sin(wx * .085 + time * .8)),
                                  0.f, float(image.height - 1));
            int x0 = int(sx), y0 = int(sy), x1 = std::min(x0 + 1, image.width - 1),
                y1 = std::min(y0 + 1, image.height - 1);
            auto sample = [&](int xx, int yy) {
                return replaces(xx + cx, yy + cy) ? original[yy * image.width + xx]
                                                  : original[y * image.width + x];
            };
            auto a = sample(x0, y0), b = sample(x1, y0), c = sample(x0, y1), d = sample(x1, y1);
            float fx = sx - x0, fy = sy - y0;
            auto channel = [&](auto member) {
                return std::uint8_t(std::lround((a.*member * (1 - fx) + b.*member * fx) * (1 - fy) +
                                                (c.*member * (1 - fx) + d.*member * fx) * fy));
            };
            image.put(x, y,
                      {channel(&platform::Color::r), channel(&platform::Color::g),
                       channel(&platform::Color::b), 255});
            // GH: bright blue from its basin/waterfall palette family; Jungle: its
            // darker navy (rather than Labyrinth's game-defined green palette).
            const auto tint = basin_tint(scene_);
            blend(image, x, y, tint.color, tint.alpha);
            // Lead-in: pooled water above a spill starts to drift towards it, its
            // streaks accelerating from rest into the fall's flow coordinate.
            for (int k = 1; k <= 20 && has_falls(); ++k) {
                auto below = fall_tile(wx, wy + k);
                const int bit = ((wy + k) & 7) * 8 + (wx & 7);
                if (!below || !((below->fed >> bit) & 1) || !((below->mask >> bit) & 1)) {
                    continue;
                }
                const int line = below->top[bit];
                const float above = float(line - wy);
                if (above > 20) {
                    break;
                }
                // Pool pixels left inside the staggered spill row continue at
                // full lead-in strength so the line has no seam.
                const float phase = above > 0 ? -above / .6f - now : flight(-above) - now;
                image.put(x, y,
                          flow_streaks(image.pixels[y * image.width + x], wx, phase, scene_,
                                       .8f * std::min(1.f, 1 - above / 20)));
                break;
            }
        }
    }
    // Jungle keeps its original animated wave band instead of GH's calm line.
    if (scene_ == 0) {
        for (auto s : surfaces_) {
            if (s.y - cy < -3 || s.y - cy >= image.height) {
                continue;
            }
            for (int x = std::max(s.x, cx); x < std::min(s.x + s.width, cx + image.width); ++x) {
                int y = s.y + int(std::lround(std::sin(x * .32 + time * 1.5) * .65));
                if (contains(x, y)) {
                    blend(image, x - cx, y - cy, {170, 238, 255, 255}, 160);
                }
                if (contains(x, y + 1)) {
                    blend(image, x - cx, y + 1 - cy, {85, 204, 255, 255}, 100);
                }
                // Rings of ripples run outward from each waterfall impact.
                for (auto o : near) {
                    if (std::abs(o.y - s.y) > 1) {
                        continue;
                    }
                    float dist = std::abs(float(x - o.x)) - o.width * .5f;
                    if (dist < 0 || dist >= 72) {
                        continue;
                    }
                    // Bright crest just under the surface line, darker trough behind.
                    for (int ring = 0; ring < 3; ++ring) {
                        float r = std::fmod(now * .45f + ring * 24.f, 72.f), fade = 1 - r / 72;
                        float at = dist - r;
                        if (at > -2.5f && at < .5f) {
                            for (int row = 1; row <= 2; ++row) {
                                if (contains(x, s.y + row)) {
                                    blend(image, x - cx, s.y + row - cy, {225, 250, 255, 255},
                                          unsigned((150 + 90 * fade) / row));
                                }
                            }
                            blend(image, x - cx, s.y - 1 - cy, {215, 248, 255, 255},
                                  unsigned(110 * fade));
                        } else if (at >= .5f && at < 3.5f) {
                            for (int row = 1; row <= 2; ++row) {
                                if (contains(x, s.y + row)) {
                                    blend(image, x - cx, s.y + row - cy, {10, 80, 170, 255},
                                          unsigned(110 * fade));
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    draw_fall_particles(image, game, cx, cy);
    // Waterfall-impact mist: a dense band of small rotating spray curls that
    // spread sideways along the waterfall/basin interface and drift a little
    // past the foam on both sides, over a faint flickering haze. Every value is
    // a deterministic function of game ticks and the detected outlet, so it is
    // pause-safe, repeatable and never touches gameplay RNG.
    for (auto outlet : outlets_) {
        const int reach = outlet.width / 2 + 18;
        if (outlet.x + reach < cx - 8 || outlet.x - reach > cx + image.width + 8 ||
            outlet.y < cy - 4 || outlet.y > cy + image.height + 16) {
            continue;
        }
        const std::uint32_t seed =
            mix(std::uint32_t(outlet.x) * 73856093u ^ std::uint32_t(outlet.y) * 19349663u);
        draw_haze(image, outlet, reach, now, cx, cy);
        const unsigned count = 40 + unsigned(outlet.width) * 5 / 4;
        for (unsigned n = 0; n < count; ++n) {
            const std::uint32_t slot = mix(seed ^ (n * 0x9e3779b9u));
            const unsigned life = 48 + slot % 64;
            const unsigned local = game.ticks + (slot >> 8) % life;
            const unsigned age = local % life;
            // A fresh path per lifetime; slots stay staggered so the cloud is
            // continuously populated rather than pulsing.
            const std::uint32_t h = mix(slot ^ ((local / life) * 0x85ebca6bu));
            const float t = float(age) / float(life);
            const float x0 = (unit(h) - .5f) * float(outlet.width);
            float side = x0 < 0 ? -1.f : 1.f;
            if (((h >> 10) & 3) == 0) {
                side = -side;
            }
            const float target = float(reach) * (.45f + .7f * unit(h >> 3));
            const float ease = 1 - (1 - t) * (1 - t);
            float x = x0 + side * std::max(0.f, target - std::abs(x0)) * ease;
            float y = -1 - (2.f + 10.f * unit(h >> 6)) * std::sqrt(t);
            // Each particle orbits a moving centre; radius grows as it spreads.
            const float spin = (.10f + .16f * unit(h >> 12)) * (((h >> 9) & 1) ? 1.f : -1.f);
            const float angle = 6.2831853f * unit(h >> 15) + spin * float(age);
            const float radius = (1.5f + 4 * t) * (.6f + .6f * unit(h >> 18));
            x += radius * std::cos(angle) + 1.2f * std::sin(now * .07f + y * .4f + float(n));
            y += .6f * radius * std::sin(angle) +
                 .8f * std::sin(now * .09f + x * .3f + float(n) * .7f);
            y = std::min(y, 1.f);
            const float fade = std::min(1.f, t / .12f) * std::min(1.f, (1 - t) / .45f);
            const unsigned alpha = unsigned((190 + 65 * unit(h >> 21)) * fade);
            if (alpha < 8) {
                continue;
            }
            const unsigned roll = (h >> 24) % 10;
            const int kind = roll < 2 ? 0 : roll < 5 ? 1 : roll < 8 ? 2 : 3;
            const int rotation = int(std::floor(angle / 1.5707963f)) & 3;
            draw_spray(image, kind, rotation, spin < 0, outlet.x - cx + int(std::lround(x)),
                       outlet.y - cy + int(std::lround(y)), alpha);
        }
    }
}
} // namespace sonic1_gg
