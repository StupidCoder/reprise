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
// Smoke thins from white through light and middle grey to dark grey; a
// fire's smoke is dark from the start.
constexpr u8 smoke_colours[] = {1, 15, 15, 12, 12, 11};
constexpr u8 dark_smoke_colours[] = {12, 12, 11, 11, 11, 11};

// The helicopter shapes ($B044 order): level side views facing one way and
// the other, and the front view, each with two rotor frames.
constexpr int level_a_shape = 4, front_shape = 6, level_b_shape = 8;
// Bank steps (the bank >> 1, 0-8): 2 is level facing one way, 3-5 the front
// view, 6 level facing the other way. Each step past level turns the
// sprite this far; the original's medium and steep shapes are the level
// one turned about 5 and 7 degrees, drawn anew.
constexpr double bank_step_degrees = 4;
// In the front view (steps 3-5, while it turns or moves at half speed) it
// leans this far per step into the way it goes.
constexpr double front_lean_degrees = 3;
// A crash's flashing playfield shows this many frames, then turns black
// (the wreck is easier to follow).
constexpr int crash_shock_frames = 8;
// A helicopter is big next to the rest: its wreck falls and turns slower
// (pixels per frame squared).
constexpr double helicopter_gravity = 0.055;
// It falls no faster than this (pixels per frame, some 30 m/s at its scale),
// which the game's camera (a line a frame) can about follow.
constexpr double helicopter_fall_speed = 1.15;
// A helicopter's wreck bursts on a second impact this hard (pixels per frame
// into the ground), or once it has lain still this long in one piece: an
// intact helicopter on the ground would look as if it could fly on.
constexpr double break_speed = 0.9;
constexpr int break_after_rest = 12;
// Its chunks: a grid of its sprite (columns of sprite pixels x rows).
constexpr int chunk_columns = 3, chunk_rows = 2;

// A double-wide bitmap as hires pixels (for debris from its pixels).
Bitmap hires(const Bitmap& b) {
    Bitmap look(b.width * b.pixel_width, b.height);
    for (int y = 0; y < b.height; ++y) {
        for (int x = 0; x < look.width; ++x) {
            look.put(x, y, b.at(x / b.pixel_width, y));
        }
    }
    return look;
}

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
    flash_ = 0;
    particles_.clear();
    wrecks_.clear();
    tanks_ = {};
    mines_ = {};
    shells_ = {};
    prisoners_ = {};
    tanks_known_ = false;
    sink_drop_ = 0;
    depot_drop_ = -1; // takes the depots' height as they are
    window_.reset();
    bank_ = bank_eased_ = -1;
    craft_shape_ = -1;
    craft_angle_ = 0;
    was_crashing_ = false;
    craft_at_.reset();
    craft_velocity_ = {};
    enemy_at_.reset();
    enemy_velocity_ = {};
    enemy_was_exploding_ = false;
}

int EnhancedView::craft_wreck_rest() const {
    // The wreck, or once it has burst, the last of its chunks to settle.
    int rest = 1 << 20;
    for (const auto& w : wrecks_) {
        if (w.sprite == 0) {
            return w.rest;
        }
        if (w.origin == 0) {
            rest = std::min(rest, w.rest);
        }
    }
    return rest;
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
    u8 code = game.map_code(column, row);
    if (code == 0x20) {
        return false; // the flickering explosion
    }
    // Only the landscape: tanks, turrets, mines, shells and the men are in
    // the map too, but debris and wrecks pass them. A man's legs stand in
    // the walkway's characters: there the walkway is (and the rock above).
    auto among = [&code](const auto& codes) {
        return std::find(codes.begin(), codes.end(), code) != codes.end();
    };
    const c64::Glyph* glyph = &game.cell_glyph(column, row); // carved, in carnage mode
    if (const auto& men = tables_.prisoner_characters; among(men)) {
        code = std::find(men.begin() + 4, men.end(), code) != men.end() ? 0x48 : 0x1F;
        glyph = &game.playfield_glyph(code);
    } else if (code == Game::turret_left || code == Game::turret_right || code == 0x71 ||
               code == 0x72 || among(tables_.tank_characters) ||
               (code != 0 && among(tables_.mine_characters))) {
        return false;
    }
    const u8 bits = (*glyph)[std::size_t(wy & 7)];
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
    const double x = craft_at_ ? (*craft_at_)[0] : view.world_x(s.x - 24 + frame.extension() + 16);
    const double y = craft_at_ ? (*craft_at_)[1] : view.world_y(s.y + 1 - 51 + 9);
    spawn_helicopter_wreck(game, 0, assets_.helicopter[std::size_t(shape)], craft_colour_, x, y,
                           craft_angle_, craft_velocity_);
}

std::optional<std::array<double, 2>> EnhancedView::surface_normal(const Game& game, double x,
                                                                  double y, double half_width,
                                                                  double half_height) const {
    double nx = 0, ny = 0;
    int hits = 0;
    // 32 directions: a thin, gappy crust (a single row of rock characters
    // over a cave) still stops a small piece.
    for (int k = 0; k < 32; ++k) {
        const double a = k * std::numbers::pi / 16, c = std::cos(a), s = std::sin(a);
        for (double d : {1.0, 0.75, 0.5}) {
            if (solid(game, x + c * half_width * d, y + s * half_height * d)) {
                nx -= c;
                ny -= s;
                ++hits;
            }
        }
    }
    if (hits == 0) {
        return std::nullopt;
    }
    const double length = std::hypot(nx, ny);
    if (length < 1e-6) {
        return std::array<double, 2>{0, -1}; // all round: up
    }
    return std::array<double, 2>{nx / length, ny / length};
}

void EnhancedView::spawn_helicopter_wreck(const Game& game, int sprite,
                                          const data::SpriteShape& shape, u8 colour, double x,
                                          double y, double angle,
                                          const std::array<double, 2>& velocity) {
    Wreck w;
    w.sprite = sprite;
    w.shape = shape;
    w.colour = colour;
    w.bitmap = helicopter(shape, colour);
    w.x = x;
    w.y = y;
    w.angle = angle;
    w.half_width = 11;
    w.half_height = 6;
    w.gravity = helicopter_gravity;
    w.vx = velocity[0] * 0.8 + random(-0.5, 0.5);
    w.vy = velocity[1] * 0.8;
    if (const auto n = surface_normal(game, x, y, w.half_width, w.half_height)) {
        // It rammed the landscape: it bounces off with what way it had,
        // weaker, and drops (no blast kicks it away from the rock).
        const double vn = w.vx * (*n)[0] + w.vy * (*n)[1];
        if (vn < 0) {
            w.vx -= 1.25 * vn * (*n)[0];
            w.vy -= 1.25 * vn * (*n)[1];
        }
        w.vx += (*n)[0] * 0.1;
        w.vy += (*n)[1] * 0.1;
    } else {
        w.vy = std::min(w.vy, 0.0) - random(0.5, 0.8); // lifted a little by the blast
    }
    w.spin = (w.vx < 0 ? -1 : 1) * random(0.006, 0.012);
    wrecks_.push_back(w);
    // The explosion: much of it flies apart, with fire and sparks.
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

void EnhancedView::draw_falling(const c64::TextFrame& frame, const Game& game, const Viewport& view,
                                const Clip& clip) {
    const auto& line = frame.line(std::clamp(view.last_line() - 1, 0, c64::Picture::height - 1));
    using Kind = Game::FallingBody::Kind;
    for (const auto& b : game.falling()) {
        Bitmap look;
        if (b.kind == Kind::rock) {
            const int w = b.width / 2;
            look = Bitmap(w, b.height, 2);
            for (int r = 0; r < b.height; ++r) {
                for (int c = 0; c < w; ++c) {
                    const u8 pair = b.pixels[std::size_t(r * w + c)];
                    if (pair) {
                        look.put(c, r, pair == 1 ? line.shared1 : pair == 2 ? line.shared2 : u8(0x0D & 7));
                    }
                }
            }
        } else if (b.kind == Kind::tank) {
            look = Bitmap(12, 16, 2);
            draw_character(look, 4, 0, game.playfield_glyph(b.codes[3]), 0x0D, line.shared1, line.shared2);
            for (int k = 0; k < 3; ++k) {
                draw_character(look, k * 4, 8, game.playfield_glyph(b.codes[std::size_t(k)]), 0x0D,
                               line.shared1, line.shared2);
            }
        } else if (b.kind == Kind::emitter) {
            look = Bitmap(4, 8, 2);
            draw_character(look, 0, 0, game.playfield_glyph(b.codes[0]), 0x0D, line.shared1, line.shared2);
            if (b.beam_on) {
                // Its beam, still firing as it tumbles, flickering; sparks
                // where it burns.
                constexpr u8 colours[] = {1, 7, 1, 10, 2, 7};
                const double sx = near_view(view, b.x + 4), shift = sx - (b.x + 4);
                const double x0 = view.picture_x(sx), y0 = view.picture_y(b.y + 4);
                const double x1 = view.picture_x(b.beam_ex + shift), y1 = view.picture_y(b.beam_ey);
                const int steps = int(std::max(std::abs(x1 - x0), std::abs(y1 - y0)));
                const int parity = view.grid_parity();
                for (int k = 0; k <= steps; ++k) {
                    const double px = x0 + (x1 - x0) * k / std::max(1, steps);
                    const int x = int(std::floor((px - parity) / 2)) * 2 + parity;
                    plot(picture_, x, int(std::floor(y0 + (y1 - y0) * k / std::max(1, steps))),
                         colours[(ticks_ + k / 6) % 6], clip);
                }
                if (b.age % 2 == 0) {
                    spawn_fire(b.beam_ex + shift, b.beam_ey, 2, 1.4);
                }
            }
        } else {
            // A tower going over: turned on its base corner, then dropping.
            const int w = b.width / 2;
            look = Bitmap(w, b.height, 2);
            for (int r = 0; r < b.height; ++r) {
                for (int c = 0; c < w; ++c) {
                    const u8 pair = b.pixels[std::size_t(r * w + c)];
                    if (pair) {
                        look.put(c, r, pair == 1 ? line.shared1 : pair == 2 ? line.shared2 : u8(0x0D & 7));
                    }
                }
            }
            const double px = near_view(view, b.pivot_x + b.ox);
            draw_rotated(picture_, look, b.pivot_x - b.x, b.pivot_y - b.y, view.picture_x(px),
                         view.picture_y(b.pivot_y + b.oy), b.angle, view.grid_parity(), clip);
            continue;
        }
        const double cx = near_view(view, b.x + b.width / 2.0), cy = b.y + b.height / 2.0;
        draw_rotated(picture_, look, b.width / 2.0, b.height / 2.0, view.picture_x(cx), view.picture_y(cy),
                     b.spin * b.age, view.grid_parity(), clip);
    }
    // Towers rocking from the gun's hits (their cells are hidden).
    for (const auto& t : game.rocking_towers()) {
        Bitmap look(t.width * 4, t.height * 8, 2);
        for (int r = 0; r < t.height; ++r) {
            for (int c = 0; c < t.width; ++c) {
                const u8 code = game.map_code(t.column + c, t.row + r);
                if (code >= 0x54 && code <= 0x58) {
                    draw_character(look, c * 4, r * 8, game.cell_glyph(t.column + c, t.row + r), 0x0D,
                                   line.shared1, line.shared2);
                }
            }
        }
        const double base_x = t.lean > 0 ? (t.column + t.width) * 8.0 : t.column * 8.0;
        const double base_y = (t.row + t.height) * 8.0;
        draw_rotated(picture_, look, base_x - t.column * 8.0, t.height * 8.0,
                     view.picture_x(near_view(view, base_x)), view.picture_y(base_y), t.lean,
                     view.grid_parity(), clip);
    }
}

double EnhancedView::near_view(const Viewport& view, double x) const {
    // The map's first 40 columns repeat from column 215: of the copies of a
    // place, the one nearest the view.
    constexpr double copy = 215 * 8;
    const double middle = picture_.width / 2.0;
    double best = x;
    for (double k : {-1.0, 1.0}) {
        if (std::abs(view.picture_x(x + k * copy) - middle) < std::abs(view.picture_x(best) - middle)) {
            best = x + k * copy;
        }
    }
    return best;
}

void EnhancedView::draw_text(const std::string& text, int centre_x, int y, u8 colour, const Clip& clip) {
    // The game's letters are two HUD characters wide: A is $21 and its right
    // half $41; a digit d is $10 + d and d (a nought $10 and $0A).
    int x = centre_x - int(text.size()) * 8;
    for (const char ch : text) {
        int left = -1, right = -1;
        if (ch >= 'A' && ch <= 'Z') {
            left = 0x21 + (ch - 'A');
            right = left + 0x20;
        } else if (ch >= '0' && ch <= '9') {
            left = 0x10 + (ch - '0');
            right = ch == '0' ? 0x0A : ch - '0';
        }
        if (left >= 0) {
            for (int half = 0; half < 2; ++half) {
                const auto& glyph = assets_.hud_charset[std::size_t(half == 0 ? left : right)];
                for (int row = 0; row < 8; ++row) {
                    for (int bit = 0; bit < 8; ++bit) {
                        const int px = x + half * 8 + bit, py = y + row;
                        if ((glyph[std::size_t(row)] >> (7 - bit) & 1) && py >= clip.first_line &&
                            py < clip.last_line && px >= clip.left && px < clip.right) {
                            picture_.pixels[std::size_t(py * picture_.width + px)] = colour;
                        }
                    }
                }
            }
        }
        x += 16;
    }
}

void EnhancedView::draw_carnage_banner(const Game& game, const Clip& clip) {
    const auto& c = game.carnage_state();
    // A fixed line below the HUD (the playfield's first line moves with its
    // fine scroll).
    const int centre = (clip.left + clip.right) / 2, top = Game::hud_lines + 12;
    if (c.overdrive > 0) {
        constexpr u8 colours[] = {1, 7, 8, 2, 8, 7};
        draw_text("OVERDRIVE", centre, top, colours[(ticks_ / 3) % 6], clip);
        // What is left of it.
        const int width = c.overdrive * 96 / 300;
        for (int x = centre - width / 2; x < centre + width / 2; ++x) {
            if (x >= clip.left && x < clip.right) {
                picture_.pixels[std::size_t((top + 10) * picture_.width + x)] = 7;
            }
        }
    } else if (c.combo >= 2) {
        // Brighter as the combo grows; it blinks out when it is about to end.
        if (c.combo_timer > 20 || (ticks_ & 2)) {
            const u8 colour = c.combo >= 8 ? 1 : c.combo >= 5 ? 7 : 15;
            draw_text("CARNAGE X" + std::to_string(c.combo), centre, top, colour, clip);
        }
    }
    if (c.overdrive == 0 && c.meter > 0) {
        // The overdrive meter fills.
        const int width = int(c.meter * 96);
        for (int x = centre - 48; x < centre - 48 + width; ++x) {
            if (x >= clip.left && x < clip.right) {
                picture_.pixels[std::size_t((top + 10) * picture_.width + x)] = c.meter > 0.8 ? 7 : 12;
            }
        }
    }
}

void EnhancedView::spawn_depot_fire(const Game& game) {
    for (const auto& d : game.depot_damage()) {
        const double left = d.cell.column * 8.0, top = d.cell.row * 8.0, level = d.damage;
        // Smoke from the first hits, thicker as it burns.
        if (random(0, 1) < 0.12 + 0.7 * level) {
            Particle p;
            p.x = left + random(4, 44);
            p.y = top + random(0, 10);
            p.vx = random(-0.25, 0.25);
            p.vy = -random(0.3, 0.8);
            p.smoke = p.dark = p.free = true;
            p.life = int(random(50, 110));
            particles_.push_back(p);
        }
        // Then fires, more and more of them, licking up from its top.
        if (level > 0.2) {
            const int fires = int(std::ceil((level - 0.2) * 26));
            for (int k = 0; k < fires; ++k) {
                Particle p;
                p.x = left + random(2, 46);
                p.y = top + random(-4, 22);
                p.vx = random(-0.25, 0.25);
                p.vy = -random(0.4, 1.2);
                p.fire = p.free = true;
                p.life = int(random(12, 30));
                particles_.push_back(p);
            }
        }
        // Little explosions here and there.
        if (level > 0.5 && random(0, 1) < 0.05 + 0.2 * (level - 0.5)) {
            spawn_fire(left + random(4, 44), top + random(4, 36), 10, 2.0);
        }
    }
}

void EnhancedView::spawn_fireball(double x, double y) {
    for (int i = 0; i < 260; ++i) {
        // The fireball: white hot at its heart, rolling out and up.
        Particle p;
        const double a = random(0, 2 * std::numbers::pi), v = random(0.6, 5.8);
        p.x = x + random(-6, 6);
        p.y = y + random(-5, 5);
        p.vx = std::cos(a) * v;
        p.vy = std::sin(a) * v * 0.8 - random(0.4, 1.8);
        p.fire = p.free = true;
        p.life = int(random(25, 75));
        particles_.push_back(p);
    }
    for (int i = 0; i < 90; ++i) {
        // The depot's pieces: white frame, green mesh, burnt brown.
        Particle p;
        const double a = random(0, 2 * std::numbers::pi), v = random(1.5, 5.5);
        p.x = x + random(-20, 20);
        p.y = y + random(-16, 16);
        p.vx = std::cos(a) * v;
        p.vy = std::sin(a) * v - random(0.8, 2.4);
        constexpr u8 colours[] = {1, 5, 2, 11};
        p.colour = colours[i % 4];
        p.free = true;
        p.life = int(random(50, 110));
        particles_.push_back(p);
    }
    for (int i = 0; i < 80; ++i) {
        // And a column of smoke after it.
        Particle p;
        p.x = x + random(-18, 18);
        p.y = y + random(-12, 8);
        p.vx = random(-1.2, 1.2);
        p.vy = -random(0.4, 2.2);
        p.smoke = p.free = true;
        p.life = int(random(70, 150));
        particles_.push_back(p);
    }
    flash_ = 8;
}

void EnhancedView::spawn_exhaust(const Game& game) {
    const auto views = game.shell_views();
    for (std::size_t i = 0; i < views.size(); ++i) {
        const auto& v = views[i];
        if (!v.flying) {
            continue;
        }
        // Where it is drawn (gliding with smooth motion), its tail behind it:
        // $71 flies left, $72 right.
        const bool left = v.look == 0x71;
        const double x = shells_[i] ? (*shells_[i])[0] : v.cell.column * 8.0;
        const double y = shells_[i] ? (*shells_[i])[1] : v.cell.row * 8.0;
        const double tail_x = x + (left ? 7 : 1), tail_y = y + 4;
        const double back = left ? 1 : -1;
        Particle smoke;
        smoke.x = tail_x + random(-0.5, 0.5);
        smoke.y = tail_y + random(-1, 1);
        smoke.vx = back * random(0.2, 0.7);
        smoke.vy = random(-0.25, 0.15);
        smoke.smoke = true;
        smoke.free = true;
        smoke.life = int(random(18, 34));
        particles_.push_back(smoke);
        if (ticks_ & 1) {
            Particle spark;
            spark.x = tail_x;
            spark.y = tail_y;
            spark.vx = back * random(0.6, 1.4);
            spark.vy = random(-0.4, 0.4);
            spark.fire = true;
            spark.free = true;
            spark.life = int(random(5, 10));
            particles_.push_back(spark);
        }
    }
}

void EnhancedView::spawn_carved(const std::vector<Game::CarvedPixel>& carved,
                                const c64::TextFrame& frame) {
    // Carnage mode: the rock the gun bites out sprays back the way the
    // tracer came, in its own colours, with a spark or two at the bite.
    const auto& line = frame.line(c64::Picture::height - 1);
    for (const auto& c : carved) {
        const double speed = std::max(1.0, std::hypot(c.dx, c.dy));
        const double bx = -c.dx / speed, by = -c.dy / speed; // back towards the gun
        if (c.bite) {
            spawn_fire(c.x + 1 + bx * 3, c.y + by * 3, 3, 2.0); // the flash of the hit
        }
        for (int n = random(0, 1) < 0.3 ? 2 : 1; n > 0; --n) {
            Particle p;
            p.x = c.x + 1;
            p.y = c.y + 0.5;
            const double v = random(0.8, 3.6);
            p.vx = bx * v + random(-1.4, 1.4);
            p.vy = by * v + random(-1.4, 0.6) - random(0.4, 1.6);
            p.colour = c.pair == 1 ? line.shared1 : c.pair == 2 ? line.shared2 : u8(0x0D & 7);
            p.life = int(random(35, 80));
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
    w.half_width = 5;
    w.half_height = 3;
    w.gravity = 0.12;
    w.explodes = true;
    wrecks_.push_back(w);
    spawn_fire(w.x, w.y, 6, 1.4);
}

void EnhancedView::move_particles(const Game& game) {
    // Clear of the ground on all sides (a rock character has holes).
    auto in_the_open = [&](double x, double y) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (solid(game, x + dx, y + dy)) {
                    return false;
                }
            }
        }
        return true;
    };
    for (auto& p : particles_) {
        ++p.age;
        if (p.free && solid(game, p.x, p.y)) {
            p.age = p.life; // the ground closed over it (a barrier lit, rock restored)
            continue;
        }
        if (p.smoke) {
            p.vx *= 0.95; // it hangs in the air and rises a little
            p.vy = p.vy * 0.95 - 0.01;
        } else {
            p.vy += p.fire ? 0.05 : 0.12;
        }
        // A pixel at a time, so no piece passes through thin ground.
        const int steps = std::max(1, int(std::ceil(std::max(std::abs(p.vx), std::abs(p.vy)))));
        for (int step = 0; step < steps; ++step) {
            const double nx = p.x + p.vx / steps, ny = p.y + p.vy / steps;
            if (!p.free) {
                // Thrown out of the rock it started in: it goes through until
                // it is out in the open.
                p.x = nx;
                p.y = ny;
                p.free = in_the_open(nx, ny);
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
            break; // the rest of this frame's way is spent on the bounce
        }
    }
    std::erase_if(particles_, [](const Particle& p) { return p.age >= p.life || p.y > 40 * 8; });
}

std::optional<std::array<double, 2>> EnhancedView::way_out(const Game& game,
                                                          const Wreck& t) const {
    // The nearest offset where its core is clear, searched in rings.
    for (int d = 1; d <= 32; ++d) {
        for (int k = 0; k < 16; ++k) {
            // Down first, then the sides, up last: a wreck in a ceiling drops.
            const int order[] = {4, 3, 5, 2, 6, 1, 7, 0, 8, 15, 9, 14, 10, 13, 11, 12};
            const double a = order[k] * std::numbers::pi / 8;
            const double x = std::cos(a) * d, y = std::sin(a) * d;
            if (!surface_normal(game, t.x + x, t.y + y, t.half_width * 0.6, t.half_height * 0.6)) {
                return std::array<double, 2>{x, y};
            }
        }
    }
    return std::nullopt; // deep in rock: it falls on through
}

void EnhancedView::move_wrecks(const Game& game) {
    std::vector<Wreck> blown, broken;
    for (auto& t : wrecks_) {
        ++t.age;
        const bool heavy = t.sprite >= 0;
        t.vy += t.gravity;
        if (heavy) {
            t.vy = std::min(t.vy, helicopter_fall_speed);
        }
        // Stuck in the ground (it started there, or sank in): it works its
        // way out to the nearest open place, without bouncing about there.
        if (surface_normal(game, t.x, t.y, t.half_width * 0.6, t.half_height * 0.6)) {
            if (const auto out = way_out(game, t)) {
                const double d = std::hypot((*out)[0], (*out)[1]);
                const double step = std::min(d, 1.5);
                t.x += (*out)[0] / d * step;
                t.y += (*out)[1] / d * step;
                t.vx *= 0.5;
                t.vy = 0;
                t.spin *= 0.7;
                continue;
            }
        }
        // In steps of at most a pixel, so it cannot pass through thin ground
        // (a walkway, a pad, a crust over a cave) between two frames.
        const int steps = std::max(1, int(std::ceil(std::max(std::abs(t.vx), std::abs(t.vy)))));
        bool touched = false, rests = false;
        for (int step = 0; step < steps && !touched; ++step) {
            const double sx = t.vx / steps, sy = t.vy / steps;
            const auto n = surface_normal(game, t.x + sx, t.y + sy, t.half_width, t.half_height);
            if (!n) {
                t.x += sx;
                t.y += sy;
                continue;
            }
            const double vn = t.vx * (*n)[0] + t.vy * (*n)[1];
            if (vn >= 0 && !t.explodes) {
                // Touching, but not moving into it: on it goes, unless that
                // would sink it in (falling past a wall's corner): then it
                // is wedged there.
                if (!surface_normal(game, t.x + sx, t.y + sy, t.half_width * 0.6,
                                    t.half_height * 0.6)) {
                    t.x += sx;
                    t.y += sy;
                    continue;
                }
                t.vx = t.vy = 0;
                touched = true;
                break;
            }
            touched = true;
            if (t.explodes) {
                break;
            }
            if (vn < 0) {
                // A bounce: the part into the surface comes back weaker, the
                // part along it scrapes and turns the body.
                const double tx = -(*n)[1], ty = (*n)[0];
                const double vt = t.vx * tx + t.vy * ty;
                const double out = -vn * (heavy ? 0.35 : 0.45), along = vt * (heavy ? 0.8 : 0.75);
                t.vx = (*n)[0] * out + tx * along;
                t.vy = (*n)[1] * out + ty * along;
                const double turn = heavy ? 0.025 : 0.05, knock = heavy ? 0.03 : 0.08;
                t.spin = t.spin * 0.4 + along * turn + random(-1, 1) * std::min(knock, -vn * 0.03);
                if (heavy && t.age > 8 && -vn > break_speed) {
                    broken.push_back(t); // a hard second impact: it bursts
                    t.age = 1 << 20;
                    break;
                }
                if (heavy && -vn > 0.8) {
                    // A hard knock throws a few sparks.
                    for (int i = 0; i < 4; ++i) {
                        Particle p;
                        p.x = t.x - (*n)[0] * t.half_height;
                        p.y = t.y - (*n)[1] * t.half_height;
                        p.vx = (*n)[0] * random(0.5, 1.8) + random(-1, 1);
                        p.vy = (*n)[1] * random(0.5, 1.8) + random(-1, 1);
                        p.fire = true;
                        p.life = int(random(8, 16));
                        particles_.push_back(p);
                    }
                }
            }
            // Lying on the ground and nearly still: it comes to rest.
            rests = (*n)[1] < -0.6 && std::hypot(t.vx, t.vy) < (heavy ? 0.3 : 0.7);
            if (!surface_normal(game, t.x + t.vx / steps, t.y + t.vy / steps, t.half_width,
                                t.half_height)) {
                t.x += t.vx / steps; // the rest of the step, off the surface
                t.y += t.vy / steps;
            }
        }
        if (touched && t.explodes) {
            blown.push_back(t); // a mine blows up where it hits the ground
            t.age = 1 << 20;
            continue;
        }
        if (t.age == 1 << 20) {
            continue;
        }
        // Resting too: wedged (touching and all but still), or settled and
        // only trembling.
        const double speed = std::hypot(t.vx, t.vy);
        const bool wedged = touched && speed < 0.15;
        t.rest = rests || wedged || (t.rest > 0 && speed < 0.3) ? t.rest + 1 : 0;
        if (heavy && t.rest > break_after_rest) {
            broken.push_back(t); // it settled in one piece: it bursts there
            t.age = 1 << 20;
            continue;
        }
        t.angle += t.spin;
        if (t.rest > 0) {
            // It settles flat, either way up.
            t.spin *= 0.8;
            t.vx *= heavy ? 0.9 : 0.85;
            const double flat = std::round(t.angle / std::numbers::pi) * std::numbers::pi;
            t.angle += (flat - t.angle) * 0.1;
        }
    }
    std::erase_if(wrecks_, [](const Wreck& t) {
        return t.age > (t.explodes || t.sprite >= 0 ? 400 : 250) || t.y > 40 * 8;
    });
    for (const auto& w : blown) {
        const double half_w = w.bitmap.width * w.bitmap.pixel_width / 2.0;
        const double half_h = w.bitmap.height / 2.0;
        spawn_pieces(hires(w.bitmap), w.x - half_w, w.y - half_h, 0.7, w.x, w.y + half_h, 2.4);
        spawn_fire(w.x, w.y, 18, 2.6);
    }
    for (const auto& w : broken) {
        break_up(game, w);
    }
}

void EnhancedView::break_up(const Game& game, const Wreck& w) {
    const Bitmap& b = w.bitmap; // sprite pixels, double wide
    const int piece_width = (b.width + chunk_columns - 1) / chunk_columns;
    const int piece_height = (b.height + chunk_rows - 1) / chunk_rows;
    const double c = std::cos(w.angle), s = std::sin(w.angle);
    for (int row = 0; row < chunk_rows; ++row) {
        for (int column = 0; column < chunk_columns; ++column) {
            Wreck f;
            f.bitmap = Bitmap(piece_width, piece_height, b.pixel_width);
            int pixels = 0;
            for (int y = 0; y < piece_height; ++y) {
                for (int x = 0; x < piece_width; ++x) {
                    const u8 colour = b.at(column * piece_width + x, row * piece_height + y);
                    f.bitmap.put(x, y, colour);
                    pixels += colour != Bitmap::clear;
                }
            }
            if (pixels < 4) {
                continue; // (almost) nothing of it there
            }
            // Where the chunk's middle is, turned as the wreck lies.
            const double lx = ((column + 0.5) * piece_width - b.width / 2.0) * b.pixel_width;
            const double ly = (row + 0.5) * piece_height - b.height / 2.0;
            const double ox = lx * c - ly * s, oy = lx * s + ly * c;
            const double length = std::max(1.0, std::hypot(ox, oy));
            f.x = w.x + ox;
            f.y = w.y + oy;
            f.angle = w.angle;
            f.half_width = piece_width * b.pixel_width / 2.0 - 1;
            f.half_height = std::max(2.0, piece_height / 2.0 - 1);
            // A chunk below the wreck's outline may start in the rock it
            // lies on: it starts nearer the middle, above it, instead.
            for (int k = 0; k < 24 && surface_normal(game, f.x, f.y, f.half_width * 0.6,
                                                      f.half_height * 0.6); ++k) {
                f.x += (w.x - f.x) * 0.15;
                f.y = std::min(f.y, w.y) - 1;
            }
            // Thrown apart from the middle and up, keeping some of the way
            // the wreck had.
            const double v = random(0.6, 1.5);
            f.vx = w.vx * 0.6 + ox / length * v + random(-0.3, 0.3);
            f.vy = std::min(0.0, w.vy) * 0.3 + oy / length * v * 0.6 - random(1.2, 2.3);
            f.spin = random(0.08, 0.22) * (random(0, 1) < 0.5 ? -1 : 1);
            f.gravity = 0.1;
            f.origin = w.sprite;
            wrecks_.push_back(f);
        }
    }
    const double half_w = b.width * b.pixel_width / 2.0, half_h = b.height / 2.0;
    spawn_pieces(hires(b), w.x - half_w, w.y - half_h, 0.25, w.x, w.y + half_h, 2.2);
    spawn_fire(w.x, w.y, 22, 2.2);
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
    const auto rocking = game.rocking_towers();
    for (int row = Game::window_first_row; row < c64::TextFrame::rows; ++row) {
        for (int column = 0; column < codes.columns; ++column) {
            const u8 code = codes.at(column, row);
            if (settings_.particles && ((code >= 0x54 && code <= 0x58) || (code >= 0x21 && code <= 0x35))) {
                // A tower or depot carnage mode toppled or blew up: gone from
                // the map, though the screen copy (it lags up to 7 frames)
                // still has it.
                const int map_row = window.row + row - Game::window_first_row;
                int map_column = first_column + column;
                if (map_column < 0 || map_column > 255) {
                    map_column = (map_column % 215 + 215) % 215; // as the wide copy repeats
                }
                if (map_row >= 0 && map_row < 40 && game.map_code(map_column, map_row) == 0) {
                    frame.set_glyph(column, row, nullptr);
                    continue;
                }
            }
            if (code >= 0x54 && code <= 0x58 && !rocking.empty()) {
                // A rocking tower is drawn turned (draw_falling).
                const int map_column = ((first_column + column) % 215 + 215) % 215;
                const int map_row = window.row + row - Game::window_first_row;
                bool hidden = false;
                for (const auto& t : rocking) {
                    for (int c = 0; c < t.width && !hidden; ++c) {
                        hidden = (t.column + c) % 215 == map_column && map_row >= t.row &&
                                 map_row < t.row + t.height;
                    }
                }
                if (hidden) {
                    frame.set_glyph(column, row, nullptr);
                    continue;
                }
            }
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
                } else if (const auto* men = &tables_.prisoner_characters;
                           std::find(men->begin(), men->end(), code) != men->end()) {
                    // A running man: his walkway and the rock above it show.
                    const bool feet = std::find(men->begin() + 4, men->end(), code) != men->end();
                    frame.set_glyph(column, row, &game.playfield_glyph(feet ? 0x48 : 0x1F));
                } else if (is_turret(code) || code == 0x71 || code == 0x72 ||
                           (code != 0 && std::find(mine.begin(), mine.end(), code) != mine.end())) {
                    frame.set_glyph(column, row, nullptr);
                }
            } else if (settings_.particles && std::find(body.begin(), body.end(), code) != body.end()) {
                // A tank's body where no tank is (it fell, in carnage mode;
                // the screen copy lags behind): hidden.
                const int k = int(std::find(body.begin(), body.end(), code) - body.begin());
                bool there = false;
                for (const auto& v : views) {
                    there |= v.on_map && v.body.column + k == first_column + column &&
                             v.body.row == window.row + row - Game::window_first_row;
                }
                if (!there) {
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
            // A tank that comes back rises out of the ground at its home,
            // throwing up the rock it breaks through.
            look_state.rise = tanks_known_ ? 16 : 0;
            if (tanks_known_ && settings_.particles) {
                for (int side = 0; side < 2; ++side) {
                    const Game::MapCell under{v.body.column + side * 2, v.body.row + 1};
                    spawn_hit_debris(game, under, game.map_code(under.column, under.row),
                                     target + 4 + side * 16, v.body.row * 8 + 8, side ? 0.5 : -0.5,
                                     1, frame);
                }
            }
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
        look_state.rise = std::max(0.0, look_state.rise - 0.4);
        // About the middle of its tracks; rising, it is cut off at its floor.
        Clip rising = clip;
        if (look_state.rise > 0) {
            rising.last_line = std::min(clip.last_line,
                                        int(std::floor(view.picture_y(v.body.row * 8 + 8))));
        }
        const double px = view.picture_x(look_state.x + 12);
        draw_rotated(picture_, look, 12, 16, px,
                     view.picture_y(v.body.row * 8 + 8 + look_state.rise),
                     look_state.pitch * degrees, int(std::floor(px)) & 1, rising);
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

void EnhancedView::draw_prisoners(const c64::TextFrame& frame, const Game& game,
                                  const Viewport& view, const Clip& clip) {
    const auto views = game.prisoner_views();
    const auto& line = frame.line(std::clamp(view.last_line() - 1, 0, c64::Picture::height - 1));
    const auto& men = tables_.prisoner_characters; // torsos 0-3, legs 4-7 ($AC12)
    for (std::size_t i = 0; i < views.size(); ++i) {
        const auto& v = views[i];
        auto& x = prisoners_[i];
        if (!v.running) {
            x.reset();
            continue;
        }
        // A character a step: he runs there instead, and his legs keep time
        // with the ground he covers (a stride each four pixels).
        const double target = v.feet.column * 8;
        if (!x || std::abs(target - x->first) > 16 || x->second != v.feet.row) {
            x = std::pair{target, v.feet.row};
        }
        x->first += std::clamp((target - x->first) * 0.25, -1.0, 1.0);
        const int leg = int(std::floor(x->first / 4)) & 1;
        const int torso = (v.left ? 0 : 2) + leg, legs = (v.left ? 4 : 6) + leg;
        Bitmap look(4, 16, 2);
        draw_character(look, 0, 0, game.playfield_glyph(men[std::size_t(torso)]), 0x0D,
                       line.shared1, line.shared2);
        draw_character(look, 0, 8, game.playfield_glyph(men[std::size_t(legs)]), 0x0D,
                       line.shared1, line.shared2);
        const double px = view.picture_x(x->first + 4);
        draw_rotated(picture_, look, 4, 16, px, view.picture_y(v.feet.row * 8 + 8), 0,
                     int(std::floor(px)) & 1, clip);
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
    // The game steps the bank every 4 frames while the stick is held (every
    // 8 back to level); eased twice, the turn runs on without steps.
    const double target = game.craft_bank() >> 1;
    if (bank_ < 0) {
        bank_ = bank_eased_ = target;
    }
    bank_eased_ += (target - bank_eased_) * 0.22;
    bank_ += (bank_eased_ - bank_) * 0.22;
    if (std::abs(target - bank_) < 0.01 && std::abs(target - bank_eased_) < 0.01) {
        bank_ = bank_eased_ = target;
    }
    const int rotor = game.craft_bank() & 1;
    int shape = front_shape + rotor;
    // Facing the screen, it leans into the way it moves.
    double angle = std::clamp(bank_ - 4, -1.0, 1.0) * front_lean_degrees;
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
    craft_colour_ = s.colour;
}

void EnhancedView::update(const Game& game, const Settings& settings) {
    settings_ = settings;
    craft_wreck_sprite_.reset();
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
        spawn_carved(game.happenings().carved, shown_frame);
        if (mode != Mode::paused) {
            spawn_exhaust(game);
            spawn_depot_fire(game);
        }
        for (const auto& b : game.happenings().depot_blasts) {
            spawn_fireball(b.column, b.row);
        }
    } else if (!settings.particles) {
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
    // The enemy's explosion becomes its wreck.
    const bool enemy_exploding = game.enemy_exploding();
    const auto enemy = frame.sprite(1);
    const bool enemy_shown = enemy.shown && !game.enemy_hidden();
    if (settings.particles && enemy_exploding && enemy_shown) {
        auto hidden = enemy;
        hidden.drawn = false;
        frame.set_sprite(1, hidden);
    }
    if (settings.particles && crashing && game.crash_frames_left() <= Game::crash_frames - crash_shock_frames) {
        // After a short shock the playfield's flashing stops: black, as in
        // play (the part below the split, from the line its colours change).
        bool below = false;
        for (int y = 0; y < c64::Picture::height; ++y) {
            auto line = frame.line(y);
            below = below || line.background_from_x > 0;
            if (below) {
                line.background = line.border = 0;
                frame.set_lines(y, y + 1, line);
            }
        }
    }
    if (flash_ > 0) {
        // A fireball lights the sky: white, yellow, orange, red, gone.
        constexpr u8 flash_colours[] = {2, 2, 8, 8, 7, 7, 1, 1};
        const u8 colour = flash_colours[std::min(7, flash_ - 1)];
        bool below = false;
        for (int y = 0; y < c64::Picture::height; ++y) {
            auto line = frame.line(y);
            below = below || line.background_from_x > 0;
            if (below) {
                line.background = colour;
                frame.set_lines(y, y + 1, line);
            }
        }
        if (mode != Mode::paused) {
            --flash_;
        }
    }
    c64::Layers layers;
    picture_ = frame.rasterise(nullptr, &layers);
    const auto window = game.shown_window();
    const Viewport view(layers, window.column - columns, window.row, Game::window_first_row, frame);
    if (!view.valid()) {
        return; // no playfield on screen (title, texts)
    }
    if (!craft_shown || crashing) {
        bank_ = bank_eased_ = -1;
    }
    if (settings.particles && crashing && !was_crashing_) {
        spawn_craft_wreck(frame, view, game);
    }
    was_crashing_ = crashing;
    if (enemy_shown && !enemy_exploding) {
        // Where it is and where it goes, for a wreck.
        const std::array<double, 2> at{view.world_x(enemy.x - 24 + frame.extension() + 16),
                                       view.world_y(enemy.y + 1 - 51 + 9)};
        if (enemy_at_) {
            for (int k = 0; k < 2; ++k) {
                const double step =
                    std::clamp(at[std::size_t(k)] - (*enemy_at_)[std::size_t(k)], -4.0, 4.0);
                enemy_velocity_[std::size_t(k)] += (step - enemy_velocity_[std::size_t(k)]) * 0.3;
            }
        }
        enemy_at_ = at;
    } else if (!enemy_shown) {
        enemy_at_.reset();
        enemy_velocity_ = {};
    }
    if (settings.particles && enemy_exploding && !enemy_was_exploding_ && enemy_shown) {
        data::SpriteShape shape;
        for (int row = 0; row < data::SpriteShape::height; ++row) {
            shape.rows[std::size_t(row)] = std::uint16_t(enemy.data[std::size_t(row * 3)] << 8 |
                                                         enemy.data[std::size_t(row * 3 + 1)]);
        }
        const double x = enemy_at_ ? (*enemy_at_)[0] : view.world_x(enemy.x - 24 + frame.extension() + 16);
        const double y = enemy_at_ ? (*enemy_at_)[1] : view.world_y(enemy.y + 1 - 51 + 9);
        spawn_helicopter_wreck(game, 1, shape, enemy.colour, x, y, 0, enemy_velocity_);
    }
    enemy_was_exploding_ = enemy_exploding;
    if (craft_shown && !crashing) {
        if (settings.smooth) {
            draw_craft(frame, layers, game);
        }
        track_craft(frame, game, view);
    }
    const Clip clip{view.first_line(), view.last_line(), view.left(), view.right(), nullptr};
    if (settings.smooth) {
        draw_platforms(frame, game, view, clip);
        draw_prisoners(frame, game, view, clip);
        draw_tanks(frame, game, view, clip);
        tanks_known_ = true;
        draw_mines(frame, game, view, clip);
        draw_shells(frame, game, view, clip);
    }
    for (const auto& t : wrecks_) {
        if (t.sprite == 0) {
            // As the game places the sprite: its middle 16 pixels in and 9
            // down, the window's top left at VIC 24, 51, a line below its y.
            craft_wreck_sprite_ = std::array<int, 2>{
                int(std::lround(view.picture_x(t.x))) - 16 + 24 - frame.extension(),
                int(std::lround(view.picture_y(t.y))) - 9 - 1 + 51};
        }
        if (!t.explodes && t.sprite != 0 && t.age > (t.sprite == 1 ? 370 : 220) && (t.age & 2)) {
            continue; // blinking out
        }
        // A helicopter's wreck flashes as long as the game flashes it.
        const bool flashing = (t.sprite == 0 && game.craft_crashing() &&
                               game.crash_frames_left() > Game::crash_frames - crash_shock_frames) ||
                              (t.sprite == 1 && game.enemy_exploding());
        const u8 flash = frame.sprite(std::max(0, t.sprite)).colour;
        const Bitmap look = t.shape ? helicopter(*t.shape, flashing ? flash : t.colour) : t.bitmap;
        draw_rotated(picture_, look, t.bitmap.width * t.bitmap.pixel_width / 2.0,
                     t.bitmap.height / 2.0, view.picture_x(t.x), view.picture_y(t.y), t.angle,
                     view.grid_parity(), clip);
    }
    const int parity = view.grid_parity();
    draw_falling(frame, game, view, clip);
    draw_carnage_banner(game, clip);
    // Carnage mode's tracers: a bright head and a yellow streak behind it.
    for (const auto& t : game.tracers()) {
        const double length = std::hypot(t.dx, t.dy);
        const int dots = 7;
        for (int k = dots - 1; k >= 0; --k) {
            const double back = k * 1.6 / std::max(1.0, length);
            const double px = view.picture_x(t.x - t.dx * back);
            const int x = int(std::floor((px - parity) / 2)) * 2 + parity;
            plot(picture_, x, int(std::floor(view.picture_y(t.y - t.dy * back))), k < 2 ? 1 : 7, clip);
        }
    }
    for (const auto& p : particles_) {
        if (p.life - p.age < 8 && (p.age & 1)) {
            continue;
        }
        u8 colour = p.colour;
        if (p.fire) {
            const auto n = std::size(fire_colours);
            colour = fire_colours[std::min(n - 1, std::size_t(p.age * int(n) / std::max(1, p.life)))];
        } else if (p.smoke) {
            const auto& colours = p.dark ? dark_smoke_colours : smoke_colours;
            const auto n = std::size(colours);
            colour = colours[std::min(n - 1, std::size_t(p.age * int(n) / std::max(1, p.life)))];
        }
        const double px = view.picture_x(p.x);
        const int x = int(std::floor((px - parity) / 2)) * 2 + parity;
        plot(picture_, x, int(std::floor(view.picture_y(p.y))), colour, clip);
    }
}

} // namespace fort_apocalypse::enhanced
