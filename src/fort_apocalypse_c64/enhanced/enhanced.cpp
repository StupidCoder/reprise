#include "fort_apocalypse_c64/enhanced/enhanced.hpp"
#include "fort_apocalypse_c64/enhanced/widescreen.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace fort_apocalypse::enhanced {

namespace {
using game::Game;

constexpr double degrees = std::numbers::pi / 180;

// Fire cools from white through yellow, orange and red to dark grey.
constexpr u8 fire_colours[] = {1, 7, 7, 8, 8, 2, 2, 9, 11};

// The helicopter shapes ($B044 order): level side views facing one way and
// the other, and the front view, each with two rotor frames.
constexpr int level_a_shape = 4, front_shape = 6, level_b_shape = 8;
// Bank steps (the bank >> 1, 0-8): 2 is level facing one way, 3-5 the front
// view, 6 level facing the other way. Each step past level turns the
// sprite this far; the original's medium and steep shapes are the level
// one turned about 5 and 7 degrees, drawn anew.
constexpr double bank_step_degrees = 4;

Bitmap helicopter(const data::SpriteShape& shape, u8 colour) {
    // Expanded sprite pixels: two hires pixels wide.
    Bitmap b(data::SpriteShape::width, data::SpriteShape::height, 2);
    for (int row = 0; row < data::SpriteShape::height; ++row) {
        for (int bit = 0; bit < data::SpriteShape::width; ++bit) {
            if (shape.pixel(bit, row)) {
                b.put(bit, row, colour);
            }
        }
    }
    return b;
}

} // namespace

EnhancedView::EnhancedView(const data::Assets& assets, const data::GameTables& tables)
    : assets_(assets), tables_(tables) {}

void EnhancedView::reset() {
    particles_.clear();
    wrecks_.clear();
    tanks_ = {};
    mines_ = {};
    shells_ = {};
    sink_drop_ = 0;
    depot_drop_ = -1; // takes the depots' height as they are
    window_.reset();
    bank_ = -1;
    craft_shape_ = -1;
    craft_angle_ = 0;
    was_crashing_ = false;
    craft_at_.reset();
    craft_velocity_ = {};
}

double EnhancedView::random(double low, double high) {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return low + (high - low) * double(rng_ % 100000) / 100000.0;
}

bool EnhancedView::solid(const Game& game, double x, double y) const {
    const int wx = int(std::floor(x)), wy = int(std::floor(y));
    const int column = wx >> 3, row = wy >> 3;
    if (column < 0 || column > 255 || row < 0 || row >= 40) {
        return false;
    }
    const u8 code = game.map_code(column, row);
    if (code == 0x20) {
        return false; // the flickering explosion
    }
    const u8 bits = game.playfield_glyph(code)[std::size_t(wy & 7)];
    return (bits >> (6 - (wx & 6)) & 3) != 0;
}

void EnhancedView::follow_window(const Game::MapCell& window) {
    // The map repeats its first 40 columns after column 215; the camera
    // jumps between the copies, and the effects jump with it.
    if (window_) {
        const int jump = window.column - window_->column;
        if (std::abs(jump) > 100) {
            const double shift = (jump > 0 ? 215 : -215) * 8.0;
            for (auto& p : particles_) {
                p.x += shift;
            }
            for (auto& t : wrecks_) {
                t.x += shift;
            }
        }
    }
    window_ = window;
}

void EnhancedView::spawn_impact(const Game& game, const Game::ShotImpact& hit,
                                const c64::TextFrame& frame) {
    // The shot's flight (sprite x units are two pixels).
    double ux = tables_.bullet_dx[hit.direction] * 2.0, uy = tables_.bullet_dy[hit.direction];
    const double length = std::hypot(ux, uy);
    if (length > 0) {
        ux /= length;
        uy /= length;
    } else {
        uy = 1;
    }
    const double cx = hit.cell.column * 8 + 4 - ux * 4, cy = hit.cell.row * 8 + 4 - uy * 4;
    spawn_hit_debris(game, hit.cell, hit.code, cx, cy, ux, uy, frame);
}

void EnhancedView::spawn_hit_debris(const Game& game, Game::MapCell cell, u8 code, double cx,
                                    double cy, double ux, double uy, const c64::TextFrame& frame) {
    // Debris in the colours of what was hit.
    const auto window = game.shown_window();
    const int screen_column = cell.column - window.column;
    const int screen_row = cell.row - window.row + Game::window_first_row;
    u8 cell_colour = 0x0D;
    if (screen_column >= 0 && screen_column < 40 && screen_row >= Game::window_first_row &&
        screen_row < 25) {
        cell_colour = frame.cell(screen_column, screen_row).colour;
    }
    const auto& line = frame.line(c64::Picture::height - 1);
    Bitmap look(8, 8); // the colours only
    draw_character(look, 0, 0, game.playfield_glyph(code), cell_colour, line.shared1,
                   line.shared2);
    std::vector<u8> colours;
    for (u8 c : look.pixels) {
        if (c != Bitmap::clear) {
            colours.push_back(c);
        }
    }
    if (colours.empty()) {
        colours.push_back(1);
    }
    const int count = 7 + int(random(0, 4));
    for (int i = 0; i < count; ++i) {
        Particle p;
        p.x = cx + random(-1.5, 1.5);
        p.y = cy + random(-1.5, 1.5);
        const double speed = random(0.6, 1.7), spread = random(-0.9, 0.9);
        p.vx = -ux * speed - uy * spread;
        p.vy = -uy * speed + ux * spread - random(0.2, 0.8);
        p.colour = colours[std::size_t(random(0, double(colours.size()) - 0.01))];
        p.life = int(random(30, 60));
        particles_.push_back(p);
    }
    for (int i = 0; i < 3; ++i) { // sparks
        Particle p;
        p.x = cx;
        p.y = cy;
        p.vx = -ux * random(0.8, 2.0) + random(-0.6, 0.6);
        p.vy = -uy * random(0.8, 2.0) + random(-0.6, 0.6);
        p.fire = true;
        p.life = int(random(10, 20));
        particles_.push_back(p);
    }
}

void EnhancedView::spawn_shell_burst(const Game& game, const Game::ShellBurst& shell,
                                     const c64::TextFrame& frame) {
    // A tank's shell flies flat; it bursts at its front.
    const double ux = shell.direction, uy = 0;
    const double cx = shell.cell.column * 8 + 4 + ux * 3, cy = shell.cell.row * 8 + 4;
    spawn_hit_debris(game, shell.cell, shell.code, cx, cy, ux, uy, frame);
    spawn_fire(cx, cy, 8, 1.8);
}

void EnhancedView::spawn_craft_wreck(const c64::TextFrame& frame, const Viewport& view,
                                     const Game& game) {
    (void)game;
    const auto& s = frame.sprite(0);
    const int shape = craft_shape_ >= 0 ? craft_shape_ : front_shape;
    Wreck w;
    w.craft_shape = shape;
    w.bitmap = helicopter(assets_.helicopter[std::size_t(shape)], s.colour);
    const double x = craft_at_ ? (*craft_at_)[0] : view.world_x(s.x - 24 + frame.extension() + 16);
    const double y = craft_at_ ? (*craft_at_)[1] : view.world_y(s.y + 1 - 51 + 9);
    w.x = x;
    w.y = y;
    w.angle = craft_angle_;
    // It keeps its way, kicked up by the blast, and tumbles the way it goes.
    w.vx = craft_velocity_[0] * 0.8 + random(-0.5, 0.5);
    w.vy = std::min(craft_velocity_[1], 0.0) - random(1.8, 2.6);
    w.spin = (w.vx < 0 ? -1 : 1) * random(0.015, 0.03);
    w.radius = 6;
    wrecks_.push_back(w);
    // The explosion: much of the craft flies apart, with fire and sparks.
    Bitmap pieces(32, data::SpriteShape::height);
    for (int py = 0; py < pieces.height; ++py) {
        for (int px = 0; px < pieces.width; ++px) {
            pieces.put(px, py, w.bitmap.at(px / 2, py));
        }
    }
    spawn_pieces(pieces, x - 16, y - 9, 0.8, x, y, 4.2);
    for (int ring = 0; ring < 2; ++ring) {
        for (int i = 0; i < 28; ++i) {
            Particle p;
            p.x = x + random(-4, 4);
            p.y = y + random(-3, 3);
            const double a = random(0, 2 * std::numbers::pi);
            const double v = ring == 0 ? random(2.0, 4.5) : random(0.6, 2.0);
            p.vx = std::cos(a) * v + w.vx * 0.3;
            p.vy = std::sin(a) * v - 0.6;
            p.fire = true;
            p.life = int(ring == 0 ? random(18, 34) : random(30, 55));
            particles_.push_back(p);
        }
    }
}

void EnhancedView::spawn_tank_debris(const Game& game, const Game::TankDestroyed& tank,
                                     const c64::TextFrame& frame) {
    const auto& line = frame.line(c64::Picture::height - 1);
    const u8 colour = 0x0D; // the playfield's colour RAM
    const double wx = tank.body.column * 8, wy = tank.body.row * 8;
    Bitmap body(24, 8); // hires, for the debris' places
    for (int k = 0; k < 3; ++k) {
        draw_character(body, k * 8, 0, game.playfield_glyph(tables_.tank_characters[std::size_t(k)]),
                       colour, line.shared1, line.shared2);
    }
    spawn_pieces(body, wx, wy, 0.45, wx + 11, wy + 10, 2.6);
    spawn_fire(wx + 12, wy + 4, 16, 2.4);
    if (tank.turret == Game::turret_left || tank.turret == Game::turret_right) {
        Wreck t;
        t.bitmap = Bitmap(4, 8, 2);
        draw_character(t.bitmap, 0, 0, game.playfield_glyph(tank.turret), colour, line.shared1,
                       line.shared2);
        t.x = wx + 12;
        t.y = wy - 4;
        t.vx = random(-1.4, 1.4);
        t.vy = random(-3.4, -2.6);
        t.spin = random(0.12, 0.3) * (random(0, 1) < 0.5 ? -1 : 1);
        wrecks_.push_back(t);
    }
}

void EnhancedView::spawn_pieces(const Bitmap& look, double left, double top, double share,
                                double x, double y, double speed) {
    // Every other hires pixel (one per double-wide pixel), some of them.
    for (int py = 0; py < look.height; ++py) {
        for (int px = 0; px < look.width; px += 2) {
            const u8 c = look.at(px, py);
            if (c == Bitmap::clear || random(0, 1) > share) {
                continue;
            }
            Particle p;
            p.x = left + px + 1;
            p.y = top + py + 0.5;
            double dx = p.x - x, dy = p.y - y;
            const double length = std::max(0.5, std::hypot(dx, dy));
            dx /= length;
            dy /= length;
            const double v = random(speed * 0.4, speed);
            p.vx = dx * v + random(-0.4, 0.4);
            p.vy = dy * v - random(0.4, 1.4);
            p.colour = c;
            p.life = int(random(45, 90));
            particles_.push_back(p);
        }
    }
}

void EnhancedView::spawn_fire(double x, double y, int count, double speed) {
    for (int i = 0; i < count; ++i) {
        Particle p;
        p.x = x + random(-6, 6);
        p.y = y + random(-2, 2);
        const double a = random(-std::numbers::pi, 0), v = random(speed * 0.25, speed);
        p.vx = std::cos(a) * v;
        p.vy = std::sin(a) * v;
        p.fire = true;
        p.life = int(random(20, 45));
        particles_.push_back(p);
    }
}

void EnhancedView::spawn_mine_wreck(const Game& game, const Game::MineDestroyed& mine,
                                    const c64::TextFrame& frame) {
    const auto& line = frame.line(c64::Picture::height - 1);
    Wreck w;
    w.bitmap = Bitmap(8, 8, 2); // two multicolour characters
    for (int k = 0; k < 2; ++k) {
        draw_character(w.bitmap, k * 4, 0, game.playfield_glyph(mine.look[std::size_t(k)]), 0x0D,
                       line.shared1, line.shared2);
    }
    w.x = mine.cell.column * 8 + 8;
    w.y = mine.cell.row * 8 + 4;
    // It keeps some of its way and pops up a little as it is hit.
    w.vx = mine.direction * random(0.3, 0.9);
    w.vy = random(-1.6, -0.6);
    w.spin = random(0.03, 0.08) * (random(0, 1) < 0.5 ? -1 : 1);
    w.radius = 4;
    w.explodes = true;
    wrecks_.push_back(w);
    spawn_fire(w.x, w.y, 6, 1.4);
}

void EnhancedView::move_particles(const Game& game) {
    for (auto& p : particles_) {
        ++p.age;
        p.vy += p.fire ? 0.05 : 0.12;
        const double nx = p.x + p.vx, ny = p.y + p.vy;
        if (!p.free) {
            p.free = !solid(game, nx, ny);
            p.x = nx;
            p.y = ny;
            continue;
        }
        if (!solid(game, nx, ny)) {
            p.x = nx;
            p.y = ny;
            continue;
        }
        // A bounce off whichever way the ground is.
        const bool wall = solid(game, nx, p.y), floor = solid(game, p.x, ny);
        if (wall || !floor) {
            p.vx = -p.vx * 0.5;
        }
        if (floor || !wall) {
            p.vy = -p.vy * 0.45;
            p.vx *= 0.75;
            if (std::abs(p.vy) < 0.35) {
                p.vy = 0;
            }
        }
        if (!solid(game, p.x + p.vx, p.y + p.vy)) {
            p.x += p.vx;
            p.y += p.vy;
        }
    }
    std::erase_if(particles_, [](const Particle& p) { return p.age >= p.life || p.y > 40 * 8; });
}

void EnhancedView::move_wrecks(const Game& game) {
    std::vector<Wreck> blown;
    for (auto& t : wrecks_) {
        ++t.age;
        t.vy += t.explodes ? 0.12 : 0.15;
        const double r = t.radius, nx = t.x + t.vx, ny = t.y + t.vy;
        const bool floor = solid(game, nx, ny + r), ceiling = solid(game, nx, ny - r);
        const bool left = solid(game, nx - r, ny), right = solid(game, nx + r, ny);
        if (!t.free) {
            t.free = !floor && !ceiling && !left && !right;
        } else if (t.explodes && (floor || ceiling || left || right)) {
            blown.push_back(t); // it hits the ground and blows up
            t.age = 1 << 20;
            continue;
        } else {
            if (floor && t.vy > 0) {
                t.vy = -t.vy * 0.4;
                t.vx *= 0.7;
                t.spin = t.vx * 0.15; // it rolls along the ground
                if (std::abs(t.vy) < 0.5) {
                    t.vy = 0;
                    ++t.rest;
                }
            }
            if (ceiling && t.vy < 0) {
                t.vy = -t.vy * 0.4;
            }
            if ((left && t.vx < 0) || (right && t.vx > 0)) {
                t.vx = -t.vx * 0.5;
                t.spin = -t.spin * 0.6;
            }
        }
        t.x += t.vx;
        t.y += t.vy;
        t.angle += t.spin;
        if (t.rest > 0) {
            // Lying on the ground: it settles flat, either way up.
            t.spin *= 0.8;
            t.vx *= 0.9;
            const double flat = std::round(t.angle / std::numbers::pi) * std::numbers::pi;
            t.angle += (flat - t.angle) * 0.1;
        }
    }
    std::erase_if(wrecks_, [](const Wreck& t) {
        return t.age > (t.explodes ? 400 : 250) || t.y > 40 * 8;
    });
    for (const auto& w : blown) {
        const double half_w = w.bitmap.width * w.bitmap.pixel_width / 2.0;
        const double half_h = w.bitmap.height / 2.0;
        Bitmap look(w.bitmap.width * w.bitmap.pixel_width, w.bitmap.height);
        for (int y = 0; y < w.bitmap.height; ++y) {
            for (int x = 0; x < look.width; ++x) {
                look.put(x, y, w.bitmap.at(x / w.bitmap.pixel_width, y));
            }
        }
        spawn_pieces(look, w.x - half_w, w.y - half_h, 0.7, w.x, w.y + half_h, 2.4);
        spawn_fire(w.x, w.y, 18, 2.6);
    }
}

double EnhancedView::floor_slope(const Game& game, double left, double bottom) const {
    // The ground's height under the tracks' two ends: the first set pixel
    // below the body (the floor's random rock is uneven).
    auto depth = [&](double x) {
        for (int d = 0; d < 6; ++d) {
            if (solid(game, x, bottom + d)) {
                return d;
            }
        }
        return 6;
    };
    constexpr double span = 18; // from the track's left end to its right
    const double rise = depth(left + 3 + span) - depth(left + 3);
    return std::clamp(std::atan2(rise, span) / degrees, -10.0, 10.0);
}

void EnhancedView::hide_objects(c64::TextFrame& frame, const Game& game,
                                const ScreenCodes& codes) const {
    if (settings_.smooth) {
        // The platforms are drawn on their own as they sink.
        auto clear_block = [&](const Game::MapCell& top_left) {
            const int first_column = game.shown_window().column - codes.extension;
            for (int row = 0; row < 5; ++row) {
                const int screen_row =
                    top_left.row + row - game.shown_window().row + Game::window_first_row;
                for (int i = 0; i < 6; ++i) {
                    const int column = top_left.column + i - first_column;
                    if (screen_row >= Game::window_first_row && screen_row < c64::TextFrame::rows &&
                        column >= 0 && column < codes.columns) {
                        frame.set_glyph(column, screen_row, nullptr);
                    }
                }
            }
        };
        for (const auto& cell : game.depot_cells()) {
            clear_block(cell);
        }
        if (game.sink_view().active) {
            clear_block(game.sink_view().top_left);
        }
    }
    const auto window = game.shown_window();
    const int first_column = window.column - codes.extension; // the frame's column 0
    const auto& body = tables_.tank_characters;
    const auto& mine = tables_.mine_characters;
    const auto views = game.tank_views();
    auto is_turret = [](u8 code) { return code == Game::turret_left || code == Game::turret_right; };
    for (int row = Game::window_first_row; row < c64::TextFrame::rows; ++row) {
        for (int column = 0; column < codes.columns; ++column) {
            const u8 code = codes.at(column, row);
            if (settings_.smooth) {
                // Tanks and mines are drawn where they are, moving smoothly;
                // what they cover shows instead.
                const int k = int(std::find(body.begin(), body.end(), code) - body.begin());
                if (k < 3) {
                    u8 under = 0;
                    for (const auto& v : views) {
                        if (v.on_map && v.body.column + k == first_column + column &&
                            v.body.row == window.row + row - Game::window_first_row) {
                            under = v.saved[std::size_t(k)];
                        }
                    }
                    frame.set_glyph(column, row, &game.playfield_glyph(under));
                } else if (is_turret(code) || code == 0x71 || code == 0x72 ||
                           (code != 0 && std::find(mine.begin(), mine.end(), code) != mine.end())) {
                    frame.set_glyph(column, row, nullptr);
                }
            } else if (settings_.particles && is_turret(code)) {
                // A turret without its tank under it was blown off: it flies
                // as debris.
                const bool on_tank = row + 1 < c64::TextFrame::rows && column > 0 &&
                                     codes.at(column - 1, row + 1) == body[0] &&
                                     codes.at(column, row + 1) == body[1];
                if (!on_tank) {
                    frame.set_glyph(column, row, nullptr);
                }
            }
        }
    }
}

void EnhancedView::draw_tanks(const c64::TextFrame& frame, const Game& game, const Viewport& view,
                              const Clip& clip) {
    const auto views = game.tank_views();
    const auto& line = frame.line(std::clamp(view.last_line() - 1, 0, c64::Picture::height - 1));
    for (std::size_t i = 0; i < views.size(); ++i) {
        const auto& v = views[i];
        auto& look_state = tanks_[i];
        if (!v.on_map || v.exploding) {
            look_state.column = -1;
            continue;
        }
        Bitmap look(12, 16, 2);
        const u8 turret = game.map_code(v.body.column + 1, v.body.row - 1);
        if (turret == Game::turret_left || turret == Game::turret_right) {
            draw_character(look, 4, 0, game.playfield_glyph(turret), 0x0D, line.shared1,
                           line.shared2);
        }
        for (int k = 0; k < 3; ++k) {
            draw_character(look, k * 4, 8, game.playfield_glyph(tables_.tank_characters[std::size_t(k)]),
                           0x0D, line.shared1, line.shared2);
        }
        const double target = v.body.column * 8;
        if (look_state.column < 0 || look_state.row != v.body.row ||
            std::abs(target - look_state.x) > 24) {
            look_state.x = target; // (re)appearing
            look_state.pitch = look_state.spin = 0;
        }
        if (look_state.column != v.body.column || look_state.row != v.body.row) {
            // A step onto the next cell jolts the body over a bump.
            if (look_state.column >= 0) {
                look_state.spin += random(-1.6, 1.6);
            }
            look_state.column = v.body.column;
            look_state.row = v.body.row;
            look_state.target = floor_slope(game, target, v.body.row * 8 + 8);
        }
        // It rolls on towards its cell instead of jumping there.
        look_state.x += std::clamp((target - look_state.x) * 0.25, -2.5, 2.5);
        // A damped spring about the ground's own slope.
        look_state.spin += (look_state.target - look_state.pitch) * 0.08 - look_state.spin * 0.12;
        look_state.pitch = std::clamp(look_state.pitch + look_state.spin, -12.0, 12.0);
        // About the middle of its tracks.
        const double px = view.picture_x(look_state.x + 12);
        draw_rotated(picture_, look, 12, 16, px, view.picture_y(v.body.row * 8 + 8),
                     look_state.pitch * degrees, int(std::floor(px)) & 1, clip);
    }
}

void EnhancedView::draw_shells(const c64::TextFrame& frame, const Game& game, const Viewport& view,
                               const Clip& clip) {
    const auto views = game.shell_views();
    const auto& line = frame.line(std::clamp(view.last_line() - 1, 0, c64::Picture::height - 1));
    for (std::size_t i = 0; i < views.size(); ++i) {
        const auto& v = views[i];
        auto& at = shells_[i];
        if (!v.flying) {
            at.reset();
            continue;
        }
        // A cell a step, sideways and up or down: it glides there.
        const std::array<double, 2> target{v.cell.column * 8.0, v.cell.row * 8.0};
        if (!at || std::abs(target[0] - (*at)[0]) > 24 || std::abs(target[1] - (*at)[1]) > 24) {
            at = target;
        }
        for (int k = 0; k < 2; ++k) {
            (*at)[std::size_t(k)] +=
                std::clamp((target[std::size_t(k)] - (*at)[std::size_t(k)]) * 0.25, -2.5, 2.5);
        }
        Bitmap look(4, 8, 2);
        draw_character(look, 0, 0, game.playfield_glyph(v.look), 0x0D, line.shared1, line.shared2);
        const double px = view.picture_x((*at)[0] + 4);
        draw_rotated(picture_, look, 4, 4, px, view.picture_y((*at)[1] + 4), 0,
                     int(std::floor(px)) & 1, clip);
    }
}

void EnhancedView::draw_mines(const c64::TextFrame& frame, const Game& game, const Viewport& view,
                              const Clip& clip) {
    const auto views = game.mine_views();
    const auto& line = frame.line(std::clamp(view.last_line() - 1, 0, c64::Picture::height - 1));
    for (std::size_t i = 0; i < views.size(); ++i) {
        const auto& v = views[i];
        auto& x = mines_[i];
        if (!v.flying) {
            x.reset();
            continue;
        }
        // Each phase is two pixels on; it glides there instead.
        const double target = v.cell.column * 8 + v.phase * 2;
        if (!x || std::abs(target - x->first) > 16 || x->second != v.cell.row) {
            x = std::pair{target, v.cell.row};
        }
        x->first += std::clamp((target - x->first) * 0.3, -1.2, 1.2);
        // The phase's own art (its light goes round), placed where it glides.
        Bitmap look(8, 8, 2);
        for (int k = 0; k < 2; ++k) {
            const u8 code = tables_.mine_characters[std::size_t(v.phase * 2 + k)];
            draw_character(look, k * 4, 0, game.playfield_glyph(code), 0x0D, line.shared1,
                           line.shared2);
        }
        // It wobbles as it flies, rocking and bobbing, each mine its own way.
        const double t = ticks_ * 0.13 + double(i) * 1.7;
        const double rock = std::sin(t) * 10 * degrees;
        const double bob = std::sin(ticks_ * 0.09 + double(i) * 2.3) * 1.5;
        const double centre_x = 7 + v.phase * 2; // the art's middle in its two cells
        const double px = view.picture_x(v.cell.column * 8 + (x->first - target) + centre_x);
        draw_rotated(picture_, look, centre_x, 4.5, px, view.picture_y(v.cell.row * 8 + 4.5 + bob),
                     rock, int(std::floor(px)) & 1, clip);
    }
}

void EnhancedView::draw_platforms(const c64::TextFrame& frame, const Game& game,
                                  const Viewport& view, const Clip& clip) {
    const auto& line = frame.line(std::clamp(view.last_line() - 1, 0, c64::Picture::height - 1));
    // A 6x5 block of characters drawn `drop` pixels down, cut off at its
    // block's bottom: it sinks into the ground.
    auto draw_block = [&](const Game::MapCell& top_left, const auto& code_at, double drop) {
        Bitmap look(24, 40, 2);
        for (int row = 0; row < 5; ++row) {
            for (int i = 0; i < 6; ++i) {
                draw_character(look, i * 4, row * 8, game.playfield_glyph(code_at(row, i)), 0x0D,
                               line.shared1, line.shared2);
            }
        }
        Clip block = clip;
        block.last_line = std::min(
            clip.last_line, int(std::floor(view.picture_y((top_left.row + 5) * 8.0))));
        const double px = view.picture_x(top_left.column * 8 + 24);
        draw_rotated(picture_, look, 24, 20, px, view.picture_y(top_left.row * 8 + 20 + drop), 0,
                     int(std::floor(px)) & 1, block);
    };
    // The fuel depots: the game sinks them through five stages in a few
    // frames (the screen only ever shows the first and the last); they
    // sink smoothly instead, and rise again when the craft lifts off.
    const auto& depot = tables_.depot;
    const double target = (4 - game.depot_stage_shown()) * 8.0;
    depot_drop_ = depot_drop_ < 0 ? target : depot_drop_ + std::clamp(target - depot_drop_, -1.0, 1.0);
    for (const auto& cell : game.depot_cells()) {
        draw_block(cell, [&](int row, int i) { return depot[std::size_t((4 + row) * 6 + i)]; },
                   depot_drop_);
    }
    // The landing pad: the game moves it down a row at a time, 35 frames
    // apart; it glides down instead.
    const auto& sink = game.sink_view();
    if (sink.active) {
        sink_drop_ += std::clamp(sink.rows * 8.0 - sink_drop_, -8.0 / 35, 8.0 / 35);
        draw_block(sink.top_left, [&](int row, int i) { return sink.look[std::size_t(row * 6 + i)]; },
                   sink_drop_);
    } else {
        sink_drop_ = 0;
    }
}

void EnhancedView::draw_craft(const c64::TextFrame& frame, const c64::Layers& layers,
                              const Game& game) {
    const auto& s = frame.sprite(0);
    const double target = game.craft_bank() >> 1;
    bank_ = bank_ < 0 ? target : bank_ + (target - bank_) * 0.3;
    if (std::abs(target - bank_) < 0.02) {
        bank_ = target;
    }
    const int rotor = game.craft_bank() & 1;
    int shape = front_shape + rotor;
    double angle = 0;
    if (bank_ < 2.5) {
        shape = level_a_shape + rotor;
        angle = -std::max(0.0, 2 - bank_) * bank_step_degrees;
    } else if (bank_ > 5.5) {
        shape = level_b_shape + rotor;
        angle = std::max(0.0, bank_ - 6) * bank_step_degrees;
    }
    const auto look = helicopter(assets_.helicopter[std::size_t(shape)], s.colour);
    // VIC x 24 and y 51 are the window's top left; the sprite starts a line
    // below its y.
    const int left = s.x - 24 + frame.extension(), top = s.y + 1 - 51;
    Clip clip{0, c64::Picture::height, 0, picture_.width, &layers};
    for (int y = 0; y < c64::Picture::height; ++y) {
        if (frame.line(y).covered) {
            clip.last_line = std::min(clip.last_line, y);
        }
    }
    if (frame.line(c64::Picture::height / 2).narrow) {
        clip.left = 7;
        clip.right = picture_.width - 9;
    }
    draw_rotated(picture_, look, 16, 9, left + 16, top + 9, angle * degrees, left & 1, clip);
    craft_shape_ = shape;
    craft_angle_ = angle * degrees;
}

bool EnhancedView::craft_on_screen(const c64::TextFrame& frame, const Game& game) const {
    const auto& s = frame.sprite(0);
    const int sprite_line = std::clamp(s.y - 51, 0, c64::Picture::height - 1);
    // The VIC starts a sprite only if it is on in the line of its y.
    return s.shown && game.craft_shown() && (frame.line(sprite_line).sprites & 1);
}

void EnhancedView::track_craft(const c64::TextFrame& frame, const Game& game, const Viewport& view) {
    // Where it is, where it goes and how it looks, for a wreck.
    const auto& s = frame.sprite(0);
    if (!settings_.smooth) {
        craft_shape_ = assets_.helicopter_tilt[std::min<u8>(game.craft_bank(), 17)];
        craft_angle_ = 0;
    }
    const std::array<double, 2> at{view.world_x(s.x - 24 + frame.extension() + 16),
                                   view.world_y(s.y + 1 - 51 + 9)};
    if (craft_at_) {
        for (int k = 0; k < 2; ++k) {
            const double step = std::clamp(at[std::size_t(k)] - (*craft_at_)[std::size_t(k)], -4.0, 4.0);
            craft_velocity_[std::size_t(k)] += (step - craft_velocity_[std::size_t(k)]) * 0.3;
        }
    }
    craft_at_ = at;
}

void EnhancedView::update(const Game& game, const Settings& settings) {
    settings_ = settings;
    const int columns = std::max(0, settings.wide_columns);
    const auto& shown_frame = game.shown_frame();
    using game::Mode;
    const auto mode = game.mode();
    // The play's modes; a crash's last frame already has the next life's
    // mode, with the crash still on screen.
    const bool in_play = mode == Mode::playing || mode == Mode::locked ||
                         mode == Mode::teleport || mode == Mode::paused || game.craft_crashing();
    if ((!settings.particles && !settings.smooth) || !in_play) {
        // Texts and the title: the effects end with the play.
        reset();
        picture_ = columns ? wide_frame(game, columns).rasterise() : game.picture();
        return;
    }
    follow_window(game.shown_window());
    if (settings.particles) {
        for (const auto& hit : game.happenings().impacts) {
            spawn_impact(game, hit, shown_frame);
        }
        for (const auto& tank : game.happenings().tanks) {
            spawn_tank_debris(game, tank, shown_frame);
        }
        for (const auto& mine : game.happenings().mines) {
            spawn_mine_wreck(game, mine, shown_frame);
        }
        for (const auto& shell : game.happenings().shells) {
            spawn_shell_burst(game, shell, shown_frame);
        }
    } else {
        particles_.clear();
        wrecks_.clear();
    }
    if (mode != Mode::paused) {
        move_particles(game);
        move_wrecks(game);
        ++ticks_;
    }

    // The picture again, without what is drawn anew.
    c64::TextFrame frame = wide_frame(game, columns);
    hide_objects(frame, game, screen_codes(game, columns));
    const bool crashing = game.craft_crashing();
    const bool craft_shown = craft_on_screen(frame, game);
    // Smooth motion draws the flying craft; particle effects its wreck.
    const bool own_craft =
        craft_shown && ((settings.smooth && !crashing) || (settings.particles && crashing));
    if (craft_shown) {
        auto craft = frame.sprite(0);
        craft.drawn = !own_craft;
        frame.set_sprite(0, craft);
    }
    c64::Layers layers;
    picture_ = frame.rasterise(nullptr, &layers);
    const auto window = game.shown_window();
    const Viewport view(layers, window.column - columns, window.row, Game::window_first_row, frame);
    if (!view.valid()) {
        return; // no playfield on screen (title, texts)
    }
    if (!craft_shown || crashing) {
        bank_ = -1;
    }
    if (settings.particles && crashing && !was_crashing_) {
        spawn_craft_wreck(frame, view, game);
    }
    was_crashing_ = crashing;
    if (craft_shown && !crashing) {
        if (settings.smooth) {
            draw_craft(frame, layers, game);
        }
        track_craft(frame, game, view);
    }
    const Clip clip{view.first_line(), view.last_line(), view.left(), view.right(), nullptr};
    if (settings.smooth) {
        draw_platforms(frame, game, view, clip);
        draw_tanks(frame, game, view, clip);
        draw_mines(frame, game, view, clip);
        draw_shells(frame, game, view, clip);
    }
    for (const auto& t : wrecks_) {
        if (!t.explodes && t.age > 220 && (t.age & 2)) {
            continue; // blinking out
        }
        const Bitmap look = t.craft_shape >= 0
                                ? helicopter(assets_.helicopter[std::size_t(t.craft_shape)],
                                             frame.sprite(0).colour) // it keeps flashing
                                : t.bitmap;
        draw_rotated(picture_, look, t.bitmap.width * t.bitmap.pixel_width / 2.0,
                     t.bitmap.height / 2.0, view.picture_x(t.x), view.picture_y(t.y), t.angle,
                     view.grid_parity(), clip);
    }
    const int parity = view.grid_parity();
    for (const auto& p : particles_) {
        if (p.life - p.age < 8 && (p.age & 1)) {
            continue;
        }
        u8 colour = p.colour;
        if (p.fire) {
            const auto n = std::size(fire_colours);
            colour = fire_colours[std::min(n - 1, std::size_t(p.age * int(n) / std::max(1, p.life)))];
        }
        const double px = view.picture_x(p.x);
        const int x = int(std::floor((px - parity) / 2)) * 2 + parity;
        plot(picture_, x, int(std::floor(view.picture_y(p.y))), colour, clip);
    }
}

} // namespace fort_apocalypse::enhanced
