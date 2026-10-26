#include "bruce_lee_c64/enhanced/effects.hpp"
#include "bruce_lee_c64/game/screen.hpp"
#include <algorithm>
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

void Effects::ember(double x, double y) {
    particles_.push_back({x + random(-3, 3), y + random(-2, 2), random(-0.25, 0.25), random(-0.5, -0.1), 0.0, 0,
                          int(random(8, 14)), ember_colours, int(std::size(ember_colours))});
}

// The game's events, from the change since the last frame.
void Effects::events(const game::World& world) {
    const bool same_room = known_ && world.room == room_;
    if (!same_room) {
        particles_.clear();
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

void Effects::step_particles() {
    for (auto& p : particles_) {
        p.x += p.vx;
        p.y += p.vy;
        p.vy += p.gravity;
        ++p.age;
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
        picture_.pixels[std::size_t(y * picture_.width + x)] = colour;
        picture_.pixels[std::size_t(y * picture_.width + x + 1)] = colour;
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
        picture_ = game::world_frame(world, &places).rasterise();
    } else {
        picture_ = game.picture();
    }
    if (settings.particles) {
        events(world);
        if (!game.paused()) {
            for (std::size_t i = 0; i < 2; ++i) {
                const auto& f = world.fireballs[i];
                if (world.room == 18 && f.active) {
                    const double fx = places.fireballs[i] ? (*places.fireballs[i])[0] : f.x;
                    const double fy = places.fireballs[i] ? (*places.fireballs[i])[1] : f.y;
                    ember(sprite_left(fx) + 8, sprite_top(fy) + 2);
                }
            }
            step_particles();
        }
        draw_particles();
    } else {
        known_ = false;
        particles_.clear();
    }
}

} // namespace bruce_lee::enhanced
