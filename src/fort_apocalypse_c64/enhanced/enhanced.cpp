#include "fort_apocalypse_c64/enhanced/enhanced.hpp"
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
    window_.reset();
    bank_ = -1;
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
    // Debris in the colours of what was hit.
    const auto window = game.shown_window();
    const int screen_column = hit.cell.column - window.column;
    const int screen_row = hit.cell.row - window.row + Game::window_first_row;
    u8 cell_colour = 0x0D;
    if (screen_column >= 0 && screen_column < 40 && screen_row >= Game::window_first_row &&
        screen_row < 25) {
        cell_colour = frame.cell(screen_column, screen_row).colour;
    }
    const auto& line = frame.line(c64::Picture::height - 1);
    Bitmap look(8, 8); // the colours only
    draw_character(look, 0, 0, game.playfield_glyph(hit.code), cell_colour, line.shared1,
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

std::vector<EnhancedView::ShownTank> EnhancedView::take_tanks(c64::TextFrame& frame,
                                                               const Game& game) {
    std::vector<ShownTank> shown;
    const auto& screen = game.shown_screen();
    const auto window = game.shown_window();
    const auto views = game.tank_views();
    const auto& body = tables_.tank_characters;
    auto is_turret = [](u8 code) { return code == Game::turret_left || code == Game::turret_right; };
    for (int row = Game::window_first_row; row < 25; ++row) {
        for (int column = 0; column + 2 < 40; ++column) {
            const auto at = std::size_t(row * 40 + column);
            if (screen[at] != body[0] || screen[at + 1] != body[1] || screen[at + 2] != body[2]) {
                continue;
            }
            ShownTank t;
            t.screen_column = column;
            t.screen_row = row;
            t.body = {body[0], body[1], body[2]};
            if (row > Game::window_first_row && is_turret(screen[at - 40 + 1])) {
                t.turret = screen[at - 40 + 1];
            }
            const Game::MapCell cell{window.column + column,
                                     window.row + row - Game::window_first_row};
            std::array<u8, 3> under{};
            for (int i = 0; i < 6; ++i) {
                const auto& v = views[std::size_t(i)];
                if (v.on_map && !v.exploding && v.body.column == cell.column &&
                    v.body.row == cell.row) {
                    t.index = i;
                    under = v.saved;
                }
            }
            // The tank is drawn on its own; what it covers shows instead.
            for (int k = 0; k < 3; ++k) {
                frame.set_glyph(column + k, row, &game.playfield_glyph(under[std::size_t(k)]));
            }
            shown.push_back(t);
            column += 2;
        }
    }
    // Turrets go with their tank's picture; one without a tank under it was
    // blown off and flies as debris.
    for (int at = Game::window_first_row * 40; at < 1000; ++at) {
        if (is_turret(screen[std::size_t(at)])) {
            frame.set_glyph(at % 40, at / 40, nullptr);
        }
    }
    return shown;
}

void EnhancedView::draw_tanks(const std::vector<ShownTank>& shown, const c64::TextFrame& frame,
                              const Game& game, const Viewport& view, const Clip& clip) {
    const auto window = game.shown_window();
    for (const auto& t : shown) {
        const int line_y = std::clamp(view.first_line() + 8 * (t.screen_row - Game::window_first_row),
                                      0, c64::Picture::height - 1);
        const auto& line = frame.line(line_y);
        Bitmap look(12, 16, 2);
        if (t.turret) {
            draw_character(look, 4, 0, game.playfield_glyph(*t.turret),
                           frame.cell(t.screen_column + 1, t.screen_row - 1).colour, line.shared1,
                           line.shared2);
        }
        for (int k = 0; k < 3; ++k) {
            draw_character(look, k * 4, 8, game.playfield_glyph(t.body[std::size_t(k)]),
                           frame.cell(t.screen_column + k, t.screen_row).colour, line.shared1,
                           line.shared2);
        }
        const int column = window.column + t.screen_column;
        const int row = window.row + t.screen_row - Game::window_first_row;
        double pitch = 0;
        if (t.index >= 0) {
            auto& look_state = tanks_[std::size_t(t.index)];
            if (look_state.column != column || look_state.row != row) {
                // A step onto the next cell jolts the body over a bump.
                if (look_state.column >= 0) {
                    look_state.spin += random(-1.6, 1.6);
                }
                look_state.column = column;
                look_state.row = row;
                look_state.target = floor_slope(game, column * 8, row * 8 + 8);
            }
            // A damped spring about the ground's own slope.
            look_state.spin += (look_state.target - look_state.pitch) * 0.08 - look_state.spin * 0.12;
            look_state.pitch = std::clamp(look_state.pitch + look_state.spin, -12.0, 12.0);
            pitch = look_state.pitch;
        }
        // About the middle of its tracks.
        const double wx = column * 8 + 12, wy = row * 8 + 8;
        draw_rotated(picture_, look, 12, 16, view.picture_x(wx), view.picture_y(wy), pitch * degrees,
                     view.grid_parity(), clip);
    }
}

void EnhancedView::draw_craft(const c64::TextFrame& frame, const c64::Layers& layers,
                              const Game& game) {
    const auto& s = frame.sprite(0);
    if (!s.shown || !game.craft_shown()) {
        bank_ = -1;
        return;
    }
    const int sprite_line = std::clamp(s.y - 51, 0, c64::Picture::height - 1);
    if (!(frame.line(sprite_line).sprites & 1)) {
        return; // the VIC would not start it
    }
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
    const int left = s.x - 24, top = s.y + 1 - 51;
    Clip clip{0, c64::Picture::height, 0, c64::Picture::width, &layers};
    for (int y = 0; y < c64::Picture::height; ++y) {
        if (frame.line(y).covered) {
            clip.last_line = std::min(clip.last_line, y);
        }
    }
    if (frame.line(c64::Picture::height / 2).narrow) {
        clip.left = 7;
        clip.right = c64::Picture::width - 9;
    }
    draw_rotated(picture_, look, 16, 9, left + 16, top + 9, angle * degrees, left & 1, clip);
}

void EnhancedView::update(const Game& game) {
    const auto& shown_frame = game.shown_frame();
    using game::Mode;
    const auto mode = game.mode();
    if (mode != Mode::playing && mode != Mode::locked && mode != Mode::teleport &&
        mode != Mode::paused) {
        // Texts and the title: the effects end with the play.
        reset();
        picture_ = game.picture();
        return;
    }
    follow_window(game.shown_window());
    for (const auto& hit : game.happenings().impacts) {
        spawn_impact(game, hit, shown_frame);
    }
    for (const auto& tank : game.happenings().tanks) {
        spawn_tank_debris(game, tank, shown_frame);
    }
    for (const auto& mine : game.happenings().mines) {
        spawn_mine_wreck(game, mine, shown_frame);
    }
    if (mode != Mode::paused) {
        move_particles(game);
        move_wrecks(game);
    }

    // The picture again, without what is drawn anew.
    c64::TextFrame frame = shown_frame;
    const auto tanks = take_tanks(frame, game);
    auto craft = frame.sprite(0);
    const bool own_craft = craft.shown && game.craft_shown();
    if (own_craft) {
        craft.drawn = false;
        frame.set_sprite(0, craft);
    }
    c64::Layers layers;
    picture_ = frame.rasterise(nullptr, &layers);
    const auto window = game.shown_window();
    const Viewport view(layers, window.column, window.row, Game::window_first_row, frame);
    if (!view.valid()) {
        return; // no playfield on screen (title, texts)
    }
    if (own_craft) {
        draw_craft(frame, layers, game);
    }
    const Clip clip{view.first_line(), view.last_line(), view.left(), view.right(), nullptr};
    draw_tanks(tanks, frame, game, view, clip);
    for (const auto& t : wrecks_) {
        if (!t.explodes && t.age > 220 && (t.age & 2)) {
            continue; // blinking out
        }
        draw_rotated(picture_, t.bitmap, t.bitmap.width * t.bitmap.pixel_width / 2.0,
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
