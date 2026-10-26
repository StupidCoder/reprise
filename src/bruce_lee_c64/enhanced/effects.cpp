#include "bruce_lee_c64/enhanced/effects.hpp"
#include "bruce_lee_c64/game/screen.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>

namespace bruce_lee::enhanced {
namespace {

// The game's frames between two steps of its main loop.
constexpr int frames_per_step = 3;

// Places further apart than this are a jump (a new room, a restart, an
// actor coming in), not a step: shown at once.
constexpr int snap_x = 12, snap_y = 24;

// Picture coordinates of an actor's sprite: VIC x 24 and line 51 are the
// display window's corner; the sprite's x is 2 * x, its top line y + $1D + 1.
double sprite_left(double x) {
    return 2 * x - 24;
}
double sprite_top(double y) {
    return y + 0x1D + 1 - 51;
}

// C64 colours the particles fade through.
constexpr std::uint8_t spark_colours[] = {1, 7, 7, 15, 12};
constexpr std::uint8_t lantern_colours[] = {1, 7, 7, 8, 8, 2};
constexpr std::uint8_t ember_colours[] = {7, 8, 8, 2, 9, 9};
constexpr std::uint8_t gib_colours[] = {2, 2, 2, 2, 2, 2, 2, 2, 9, 9};
constexpr std::uint8_t light_gib_colours[] = {10, 10, 10, 10, 2, 2, 2, 2, 9, 9}; // seen on brown rooms too
constexpr std::uint8_t spray_colours[] = {10, 2, 2, 2, 9};

// The flame glyphs $40A9 changes: rows 2-3 of the lower halves of the
// lantern characters $13, $31 and $14 (and of $06 and $67 in rooms 19 and
// 13), which flame drives each and the bits of its pattern.
struct FlameGlyph {
    std::uint8_t code;
    int flame;
    std::uint8_t bits;
};
constexpr FlameGlyph flame_glyphs[] = {{0x93, 0, 0x30}, {0xB1, 1, 0x30}, {0x94, 2, 0x0C},
                                       {0x86, 0, 0x30}, {0xE7, 2, 0x0C}};

int flame_of(std::uint8_t code) {
    for (const auto& g : flame_glyphs) {
        if ((code | 0x80) == g.code) {
            return g.flame;
        }
    }
    return -1;
}

// Moves that start when a blow lands (World::combat).
bool struck(std::uint8_t move) {
    return move == game::move::pushed_back || move == game::move::thrown || move == 0x1B;
}

} // namespace

double Effects::random(double low, double high) {
    return std::uniform_real_distribution<double>(low, high)(rng_);
}

void Effects::glide(Glide& g, bool present, std::uint8_t x, std::uint8_t y, bool snap) {
    if (!present) {
        g.valid = false;
        return;
    }
    const bool jump = !g.valid || snap || std::abs(int(x) - int(g.target_x)) > snap_x ||
                      std::abs(int(y) - int(g.target_y)) > snap_y;
    if (jump) {
        g = {true, double(x), double(y), 0, 0, 0, x, y};
        return;
    }
    if (x != g.target_x || y != g.target_y) {
        // A new step: spread it over the frames to the next one.
        g.target_x = x;
        g.target_y = y;
        g.vx = (x - g.x) / frames_per_step;
        g.vy = (y - g.y) / frames_per_step;
        g.frames_left = frames_per_step;
    }
    if (g.frames_left > 0) {
        g.x += g.vx;
        g.y += g.vy;
        if (--g.frames_left == 0) {
            g.x = g.target_x;
            g.y = g.target_y;
        }
    }
}

void Effects::spark(double x, double y, int count) {
    for (int i = 0; i < count; ++i) {
        const double angle = random(0, 6.2832), speed = random(0.6, 2.2);
        particles_.push_back({x, y, std::cos(angle) * speed * 1.4, std::sin(angle) * speed - 0.6, 0.12, 0,
                              int(random(8, 16)), spark_colours, int(std::size(spark_colours))});
    }
}

void Effects::burst(double x, double y) {
    for (int i = 0; i < 18; ++i) {
        const double angle = random(3.4, 6.0), speed = random(0.8, 2.6);
        particles_.push_back({x + random(-3, 3), y + random(-4, 4), std::cos(angle) * speed * 1.2,
                              std::sin(angle) * speed, 0.09, 0, int(random(18, 34)), lantern_colours,
                              int(std::size(lantern_colours))});
    }
}

void Effects::fragments(double x, double y, std::uint8_t colour) {
    // One colour for the falling pieces: kept in a member so the table
    // pointer stays valid.
    fragment_colour_ = colour;
    for (int i = 0; i < 10; ++i) {
        particles_.push_back({x + random(-10, 10), y + random(-3, 3), random(-0.5, 0.5), random(-0.8, 0.2), 0.18, 0,
                              int(random(20, 40)), &fragment_colour_, 1});
    }
}

// Bloody pieces: chunks that fly, bounce on the room's floors and lie there
// darkening, and a fine spray that fades in the air.
void Effects::burst_gibs(double x, double y) {
    for (int i = 0; i < 28; ++i) {
        // A few big chunks, more small bits; the last ones barely lift: the
        // pool where the body lay.
        const bool pool = i >= 22;
        const bool light = i % 3 == 1;
        Particle p{x + random(-10, 10), y + random(-5, 1), random(-1.3, 1.3) * (pool ? 0.3 : 1),
                   pool ? random(-0.6, 0) : random(-3.2, -0.8), 0.16, 0, int(random(200, 300)),
                   light ? light_gib_colours : gib_colours,
                   int(light ? std::size(light_gib_colours) : std::size(gib_colours))};
        p.bounces = true;
        p.size = i < 8 || pool ? 2 : 1;
        particles_.push_back(p);
    }
    for (int i = 0; i < 14; ++i) {
        const double angle = random(3.3, 6.1), speed = random(0.5, 2.0);
        particles_.push_back({x + random(-4, 4), y + random(-6, 2), std::cos(angle) * speed * 1.3,
                              std::sin(angle) * speed, 0.1, 0, int(random(10, 22)), spray_colours,
                              int(std::size(spray_colours))});
    }
}

void Effects::ember(double x, double y) {
    particles_.push_back({x + random(-3, 3), y + random(-2, 2), random(-0.25, 0.25), random(-0.5, -0.1), 0.0, 0,
                          int(random(8, 14)), ember_colours, int(std::size(ember_colours))});
}

// The game's events, from the change since the last frame.
void Effects::events(const game::World& world, Settings settings) {
    const bool same_room = known_ && world.room == room_;
    if (!same_room) {
        particles_.clear();
    }
    if (settings.gibs && same_room) {
        watch_defeats(world);
    }
    for (std::size_t slot = 0; slot < 3; ++slot) {
        const auto& a = world.actors.slots[slot];
        was_present_[slot] = a.present();
        was_defeated_[slot] = a.present() && (a.d1 & 0x80);
        if (a.present()) {
            last_place_[slot] = body_middle(world, a);
        }
    }
    if (!settings.particles) {
        room_ = world.room;
        known_ = true;
        return;
    }
    // A lantern taken.
    const auto& hanging = world.progress[std::size_t(world.room)].lanterns;
    const auto& list = world.assets.rooms[std::size_t(world.room)].lanterns;
    if (same_room && lanterns_.size() == hanging.size()) {
        for (std::size_t i = 0; i < hanging.size() && i < list.size(); ++i) {
            if (lanterns_[i] && !hanging[i]) {
                burst(8 * list[i].column + 4, 8 * list[i].screen_row + 8);
            }
        }
    }
    lanterns_ = hanging;
    // Blows that land.
    for (std::size_t slot = 0; slot < 3; ++slot) {
        const auto& a = world.actors.slots[slot];
        if (same_room && a.present() && struck(a.move) && !struck(moves_[slot])) {
            spark(sprite_left(a.x) + 12, sprite_top(a.y) + 9, 9);
        }
        moves_[slot] = a.present() ? a.move : 0;
    }
    // A crumbling floor opening (its timer at 12, $3251).
    for (std::size_t i = 0; i < world.crumbles.size(); ++i) {
        const auto& c = world.crumbles[i];
        if (same_room && c.timer == 13 && crumble_timers_[i] == 12) {
            const int column = c.column + 1, row = 2 * (c.row + 1) - 1;
            std::uint8_t colour = 12;
            if (column >= 0 && column < game::Playfield::columns && row >= 0 && row < game::Playfield::rows) {
                colour = world.field.at(column, row).colour & 7;
                if (colour == 0) {
                    colour = 12;
                }
            }
            fragments(8 * column + 4, 8 * row + 12, colour);
        }
        crumble_timers_[i] = c.timer;
    }
    room_ = world.room;
    known_ = true;
}

// The body's middle as drawn (sprites 16 pixels wide, x-expanded; wide
// poses facing left 8 units further left, $25CF).
std::array<double, 2> Effects::body_middle(const game::World& world, const game::Actor& a) {
    const bool wide = a.pose < world.assets.poses.size() && world.assets.poses[a.pose].wide;
    const int x = wide && a.facing != 1 ? a.x - 8 : a.x;
    return {sprite_left(x) + 16, sprite_top(a.y) + 13};
}

// Deaths by spikes, lasers and other deadly cells, and by room 18's
// fireballs: Bruce bursts and stays hidden while his dying move runs; an
// enemy (taken away at once) bursts where it was last drawn.
void Effects::watch_hazards(const game::World& world) {
    const bool same_room = known_ && world.room == room_;
    for (std::size_t slot = 0; slot < 3; ++slot) {
        if (world.hazard_deaths[slot] != hazard_deaths_[slot] && same_room) {
            const auto& a = world.actors.slots[slot];
            if (slot == 0 && a.present()) {
                burst_gibs(body_middle(world, a)[0], body_middle(world, a)[1]);
                bruce_burst_ = true;
            } else if (slot != 0 && was_present_[slot]) {
                burst_gibs(last_place_[slot][0], last_place_[slot][1]);
            }
        }
        hazard_deaths_[slot] = world.hazard_deaths[slot];
    }
    const auto& bruce = world.actors.slots[0];
    if (!bruce.present() || bruce.move != game::move::dying) {
        bruce_burst_ = false;
    }
}

// The Ninja or the Yamo out (energy gone, $2FFE takes it away): it bursts
// where it lay.
void Effects::watch_defeats(const game::World& world) {
    for (std::size_t slot = 1; slot < 3; ++slot) {
        if (was_present_[slot] && was_defeated_[slot] && !world.actors.slots[slot].present()) {
            burst_gibs(last_place_[slot][0], last_place_[slot][1]);
        }
    }
}

// The multicolour pixel pair of the room's graphics at a picture pixel
// (0-3), or -1 where there is none (a hi-res cell, outside the rows).
int Effects::pixel_pair(const game::World& world, const c64::Layers& layers, int x, int y) const {
    if (y < 0 || y >= c64::Picture::height || x < 0 || x >= picture_.width) {
        return -1;
    }
    const int source = layers.source_line[std::size_t(y)];
    const int sx = x - layers.xscroll[std::size_t(y)];
    if (source < 0 || sx < 0) {
        return -1;
    }
    const int row = source / 8, column = sx / 8;
    if (row >= game::Playfield::rows || column >= game::Playfield::columns) {
        return -1;
    }
    const auto& cell = world.field.at(column, row);
    if (!(cell.colour & 8)) {
        return -1;
    }
    const std::uint8_t bits = charset_[cell.code][std::size_t(source & 7)];
    return (bits >> (6 - 2 * ((sx & 7) / 2))) & 3;
}

void Effects::step_particles(const game::World& world, const c64::Layers& layers) {
    const int height = c64::Picture::height;
    // What a piece lands on: what the actors stand on (pixel pair 01, the
    // terrain probe's ground). Pair 11 is no wall here: the dithered rock
    // Bruce walks in front of is drawn with it.
    auto solid = [&](double x, double y) {
        return y >= 24 && pixel_pair(world, layers, int(std::floor(x)), int(std::floor(y))) == 1;
    };
    for (auto& p : particles_) {
        ++p.age;
        if (!p.bounces) {
            p.x += p.vx;
            p.y += p.vy;
            p.vy += p.gravity;
            continue;
        }
        // A piece lying on a floor stays there until the floor goes.
        const double bottom = p.size - 1;
        if (p.resting) {
            if (solid(p.x, p.y + bottom + 1)) {
                continue;
            }
            p.resting = false;
        }
        if (solid(p.x + p.vx, p.y)) {
            p.vx = -p.vx * 0.4; // the side of a ledge
        } else {
            p.x += p.vx;
        }
        p.vy += p.gravity;
        // Every line on the way down, so a fast piece can't skip a thin floor.
        bool lands = false;
        for (double dy = 1; p.vy > 0 && dy < p.vy + 1 && !lands; ++dy) {
            lands = solid(p.x, p.y + bottom + std::min(dy, p.vy));
        }
        if (lands) {
            // Down onto the graphics' top: bounce, or settle.
            while (!solid(p.x, p.y + bottom + 1) && p.y < height) {
                p.y += 1;
            }
            p.vy = -p.vy * 0.35;
            p.vx *= 0.55;
            if (std::abs(p.vy) < 0.6) {
                p.vx = p.vy = 0;
                p.resting = true;
            }
        } else if (p.vy < 0 && solid(p.x, p.y + p.vy)) {
            p.vy = 0; // a ceiling
        } else {
            p.y += p.vy;
        }
    }
    std::erase_if(particles_, [](const Particle& p) {
        return p.age >= p.life || p.y < -8 || p.y > c64::Picture::height + 8 || p.x < -8 || p.x > 330;
    });
}

// Each particle is one double-wide pixel, like the game's multicolour ones.
void Effects::draw_particles() {
    for (const auto& p : particles_) {
        const int x = int(std::floor(p.x / 2)) * 2, y = int(std::floor(p.y));
        if (y < 24 || y >= c64::Picture::height || x < 0 || x + 1 >= picture_.width) {
            continue; // not over the status rows
        }
        const int index = std::min(p.colour_count - 1, p.age * p.colour_count / std::max(1, p.life));
        const std::uint8_t colour = p.colours[index];
        for (int dy = 0; dy < p.size && y + dy < c64::Picture::height; ++dy) {
            for (int dx = 0; dx < 2 * p.size && x + dx < picture_.width; ++dx) {
                picture_.pixels[std::size_t((y + dy) * picture_.width + x + dx)] = colour;
            }
        }
    }
}

// The lift's glyphs ($2C5D) are a strip of 16 rows, two per pattern byte,
// shown from row 2 x phase; a step moves them two rows. Halfway to the next
// step the strip is shown one row on, in the lift's direction.
bool Effects::smooth_lift(const game::World& world, data::Charset& charset) {
    const int room = world.room;
    const bool lift_room = (room >= 4 && room <= 9 && room != 7) || room == 11; // the scripts that step it
    if (world.lift.phase != lift_phase_) {
        lift_phase_ = world.lift.phase;
        lift_changed_ = frame_;
    }
    if (!lift_room || (world.lift.timer & 0x80)) {
        return false; // no lift, or its pause
    }
    const int frames_per_step = 3 * (world.assets.lift_speeds[world.lift.speed] & 0x7F);
    if (frames_per_step <= 0 || 2 * (frame_ - lift_changed_) < frames_per_step) {
        return false;
    }
    const std::size_t set = room < 10 ? 0 : 1;
    const auto& patterns = world.assets.lift_patterns[set];
    const int start = 2 * world.lift.phase + (world.lift.down ? -1 : 1);
    for (std::size_t g = 0; g < 3; ++g) {
        for (int r = 0; r < 16; ++r) {
            const int strip_row = ((start + r) % 16 + 16) % 16;
            const auto value = patterns[g * 8 + std::size_t(strip_row / 2)];
            charset[0x61 + g + (r < 8 ? 0 : 0x80)][std::size_t(r & 7)] = value;
        }
    }
    return true;
}

// A flame's new pattern ($40A9 sets rows 2 and 3 at once) first shows in
// its upper row only, the old one still below, for two frames: the flame
// licks up instead of blinking.
bool Effects::flame_inbetween(const game::World& world, data::Charset& charset) {
    const auto& shown = world.charsets[std::size_t(world.charset_index())];
    bool changed = false;
    for (std::size_t i = 0; i < std::size(flame_glyphs); ++i) {
        const std::uint8_t now = shown[flame_glyphs[i].code][2];
        if (now != flame_rows_[i]) {
            flame_old_[i] = flame_rows_[i];
            flame_rows_[i] = now;
            flame_inbetween_[i] = 2;
        }
        if (flame_inbetween_[i] > 0) {
            charset[flame_glyphs[i].code][3] = flame_old_[i];
            changed = true;
        }
    }
    return changed;
}

// A soft light around each hanging lantern: double-wide pixels in yellow and
// light grey, thinning out with distance by an ordered dither, slowly
// pulsing, only over background pixels.
void Effects::glow(const game::World& world, const c64::Layers& layers) {
    static constexpr int bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    const auto& list = world.assets.rooms[std::size_t(world.room)].lanterns;
    const auto& hanging = world.progress[std::size_t(world.room)].lanterns;
    const auto& shown = world.charsets[std::size_t(world.charset_index())];
    constexpr double radius = 12;
    glow_level_.resize(list.size(), 1.0);
    for (std::size_t i = 0; i < list.size() && i < hanging.size(); ++i) {
        if (!hanging[i]) {
            continue;
        }
        // The light follows the lantern's flame: its pattern's lit pixels
        // (none, one or two of the pair), eased.
        double flame = 0;
        int seen = 0;
        for (int row = list[i].screen_row; row <= list[i].screen_row + 2 && row < game::Playfield::rows; ++row) {
            for (int column = std::max(0, list[i].column - 1);
                 column <= std::min(game::Playfield::columns - 1, list[i].column + 1); ++column) {
                const std::uint8_t code = world.field.at(column, row).code;
                const int f = flame_of(code);
                if (f < 0) {
                    continue;
                }
                for (const auto& g : flame_glyphs) {
                    if (g.code == (code | 0x80)) {
                        flame += std::popcount(unsigned(shown[g.code][2] & g.bits)) / 2.0;
                        ++seen;
                    }
                }
            }
        }
        const double target = seen ? 0.72 + 0.36 * flame / seen : 0.9;
        glow_level_[i] += (target - glow_level_[i]) * 0.3;
        const double cx = 8 * list[i].column + 4, cy = 8 * list[i].screen_row + 8;
        const double pulse = glow_level_[i] + 0.04 * std::sin(frame_ * 0.21 + double(i) * 1.7);
        for (int y = int(cy - radius); y <= int(cy + radius); ++y) {
            if (y < 24 || y >= c64::Picture::height) {
                continue;
            }
            for (int x = (int(cx - radius) / 2) * 2; x <= int(cx + radius); x += 2) {
                if (x < 0 || x + 1 >= picture_.width) {
                    continue;
                }
                const double d = std::hypot(x + 1 - cx, (y - cy) * 1.15);
                if (d < 2.5 || d > radius) {
                    continue;
                }
                const double light = pulse * (1 - d / radius);
                if (light * 16 <= bayer[y & 3][(x / 2) & 3] + 0.5) {
                    continue;
                }
                if (layers.foreground_at(x, y, picture_.width) || layers.foreground_at(x + 1, y, picture_.width)) {
                    continue;
                }
                const std::uint8_t colour = light > 0.45 ? 7 : 15;
                picture_.pixels[std::size_t(y * picture_.width + x)] = colour;
                picture_.pixels[std::size_t(y * picture_.width + x + 1)] = colour;
            }
        }
    }
}

// A crumbling floor ($3215) cracks: from the cell stepped on a jagged dark
// line runs out both ways over the three cells while the timer counts to
// the game's own crack glyphs (8), stays until the floor opens (12) and
// closes again as it mends (16 to 20). Only over the floor's own pixels.
void Effects::cracks(const game::World& world, const c64::Layers& layers) {
    const int width = picture_.width;
    for (const auto& c : world.crumbles) {
        if (c.timer == 0 || c.timer >= 20 || (c.timer >= 12 && c.timer < 16)) {
            continue;
        }
        const double reach = c.timer < 8 ? c.timer / 8.0 : c.timer < 12 ? 1.0 : (20 - c.timer) / 4.0;
        const int top = 8 * (2 * (c.row + 1) - 1), middle = 8 * (c.column + 1) + 4;
        // The same jags every time for this floor: a small hash of its place.
        auto jag = [&](int step) {
            unsigned h = unsigned(c.row * 131 + c.column * 31 + step * 2654435761u);
            h ^= h >> 13;
            h *= 0x5BD1E995u;
            h ^= h >> 15;
            return h;
        };
        auto mark = [&](int x, int y, std::uint8_t colour) {
            x &= ~1;
            if (x < 0 || x + 1 >= width || y < 24 || y >= c64::Picture::height) {
                return;
            }
            if (pixel_pair(world, layers, x, y) != 1) {
                return; // only over the floor itself (what actors stand on)
            }
            auto& p0 = picture_.pixels[std::size_t(y * width + x)];
            auto& p1 = picture_.pixels[std::size_t(y * width + x + 1)];
            const std::uint8_t shade = p0 == colour ? 11 : colour;
            p0 = p1 = shade;
        };
        // The floor's band at a column: its first ground line from the top
        // of the cells and how many follow.
        auto band = [&](int x, int& first, int& lines) {
            first = -1;
            lines = 0;
            for (int y = top; y < top + 16; ++y) {
                if (pixel_pair(world, layers, x, y) == 1) {
                    if (first < 0) first = y;
                    ++lines;
                } else if (first >= 0) {
                    break;
                }
            }
        };
        const int length = int(std::lround(reach * 6)); // in pixel pairs each way: the three cells
        for (int side = -1; side <= 1; side += 2) {
            int depth = 1; // lines below the band's top
            for (int s = 0; s <= length; ++s) {
                const unsigned h = jag(side * 16 + s);
                if (s > 0 && (h & 3) == 0) {
                    --depth;
                } else if (s > 0 && (h & 3) == 1) {
                    ++depth;
                }
                const int x = middle + side * 2 * s;
                int first, lines;
                band(x, first, lines);
                if (first < 0) {
                    continue;
                }
                depth = std::clamp(depth, 0, std::max(0, lines - 1));
                mark(x, first + depth, 0);
                if (s > 1 && (h >> 4) % 5 == 0) {
                    mark(x, first + depth + 1, 0); // a short branch down
                }
            }
        }
    }
}

void Effects::update(const game::Game& game, Settings settings) {
    if (!game.in_play()) {
        known_ = false;
        particles_.clear();
        for (auto& g : actors_) g.valid = false;
        for (auto& g : fireballs_) g.valid = false;
        picture_ = game.picture();
        return;
    }
    ++frame_;
    const auto& world = game.world;
    const bool new_room = known_ && world.room != room_;
    game::SpritePlaces places;
    if (settings.smooth) {
        for (std::size_t slot = 0; slot < 3; ++slot) {
            const auto& a = world.actors.slots[slot];
            glide(actors_[slot], a.present(), a.x, a.y, new_room);
            if (actors_[slot].valid) {
                places.actors[slot] = std::array<double, 2>{actors_[slot].x, actors_[slot].y};
            }
        }
        for (std::size_t i = 0; i < 2; ++i) {
            const auto& f = world.fireballs[i];
            glide(fireballs_[i], world.room == 18 && f.active, f.x, f.y, new_room);
            if (fireballs_[i].valid) {
                places.fireballs[i] = std::array<double, 2>{fireballs_[i].x, fireballs_[i].y};
            }
        }
    }
    // The room charset as shown: the lift a row on, flames in between.
    charset_ = world.charsets[std::size_t(world.charset_index())];
    bool own_charset = false;
    if (settings.smooth && !game.paused()) {
        own_charset = smooth_lift(world, charset_);
    } else if (world.lift.phase != lift_phase_) {
        lift_phase_ = world.lift.phase;
        lift_changed_ = frame_;
    }
    if (settings.lights) {
        own_charset = flame_inbetween(world, charset_) || own_charset;
        if (!game.paused()) {
            for (auto& f : flame_inbetween_) {
                f = std::max(0, f - 1);
            }
        }
    }
    if (own_charset) {
        places.charset = &charset_;
    }
    if (settings.lights) {
        // The flash of a blow that lands: the struck actor white for two frames.
        for (std::size_t slot = 0; slot < 3; ++slot) {
            const auto& a = world.actors.slots[slot];
            const std::uint8_t m = a.present() ? a.move : 0;
            if (struck(m) && !struck(flash_moves_[slot])) {
                flash_[slot] = 2;
            }
            flash_moves_[slot] = m;
            if (flash_[slot] > 0) {
                places.actor_colour[slot] = 1;
                if (!game.paused()) {
                    --flash_[slot];
                }
            }
        }
    }
    if (settings.gibs) {
        watch_hazards(world);
        places.hidden[0] = bruce_burst_;
    } else {
        hazard_deaths_ = world.hazard_deaths;
        bruce_burst_ = false;
    }
    c64::Layers layers;
    picture_ = game::world_frame(world, &places).rasterise(nullptr, &layers);
    if (settings.lights) {
        glow(world, layers);
    }
    if (settings.particles) {
        cracks(world, layers);
    }
    if (settings.particles || settings.gibs) {
        events(world, settings);
        if (!game.paused()) {
            for (std::size_t i = 0; i < 2 && settings.particles; ++i) {
                const auto& f = world.fireballs[i];
                if (world.room == 18 && f.active) {
                    const double fx = places.fireballs[i] ? (*places.fireballs[i])[0] : f.x;
                    const double fy = places.fireballs[i] ? (*places.fireballs[i])[1] : f.y;
                    ember(sprite_left(fx) + 8, sprite_top(fy) + 2);
                }
            }
            step_particles(world, layers);
        }
        draw_particles();
    } else {
        known_ = false;
        particles_.clear();
    }
}

} // namespace bruce_lee::enhanced
