#include "wizball_c64/enhanced/effects.hpp"
#include "c64/palette.hpp"
#include <algorithm>
#include <cmath>
#include <span>

namespace wizball::enhanced {
namespace {

using game::Display;
using game::Play;

constexpr int field_top = 66, field_bottom = 233; // the playfield's raster lines (picture.cpp)
constexpr int explosion_first_frame = 0x5F;

// The C64's colours one step darker, down to black (by brightness).
constexpr std::array<int, 16> darker{0, 15, 9, 12, 2, 8, 0, 13, 11, 0, 8, 6, 11, 5, 4, 12};

bool same(const platform::Color& a, const platform::Color& b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

bool playing(const Play& play) {
    const auto stage = play.stage();
    return (stage == Play::Stage::play || stage == Play::Stage::vanish || stage == Play::Stage::bonus ||
            stage == Play::Stage::game_over) &&
           !play.in_lab() && !play.flying_text();
}

// The window on a raster line, in VIC x (as Display::render cuts it).
std::array<int, 2> window(const Display& d, int raster) {
    const auto& l = d.line(raster);
    if (l.wide && d.extension > 0) return {24 - 8 * d.extension, 344 + 8 * d.extension};
    return l.columns38 ? std::array<int, 2>{31, 335} : std::array<int, 2>{24, 344};
}

} // namespace

std::uint32_t Effects::next() {
    seed_ ^= seed_ << 13;
    seed_ ^= seed_ >> 17;
    seed_ ^= seed_ << 5;
    return seed_;
}

void Effects::burst(float x, float y, const std::vector<int>& colours, int count, float speed, int life, bool upwards,
                    float floor) {
    if (colours.empty()) return;
    for (int i = 0; i < count; ++i) {
        float angle = float(next() % 6283) / 1000.0f;
        if (upwards) angle = 3.1416f + 0.35f + float(next() % 2440) / 1000.0f; // up and to the sides
        const float v = speed * (0.35f + float(next() % 1000) / 1540.0f);
        Particle p;
        p.x = x;
        p.y = y;
        p.dx = std::cos(angle) * v * 1.6f; // x in single pixels, which are narrower than lines are tall
        p.dy = std::sin(angle) * v - 0.4f;
        p.colour = colours[next() % colours.size()];
        p.life = life / 2 + int(next() % unsigned(life));
        p.floor = y + floor;
        particles_.push_back(p);
    }
}

void Effects::swirl(float x, float y, float width, const std::vector<int>& colours) {
    Particle p;
    const float side = float(next() % 1000) / 1000.0f - 0.5f;
    p.x = x + side * width;
    p.y = y;
    p.dx = -side * 1.2f; // in towards the middle as it rises
    p.dy = -0.6f - float(next() % 100) / 160.0f;
    p.colour = colours[next() % colours.size()];
    p.life = 16 + int(next() % 14);
    particles_.push_back(p);
}

void Effects::step(std::vector<Particle>& particles, bool clip) {
    for (auto& p : particles) {
        p.x += p.dx;
        p.y += p.dy;
        p.dy += 0.07f;
        p.dx *= 0.97f;
        ++p.age;
    }
    std::erase_if(particles, [clip](const Particle& p) {
        return p.age >= p.life || (clip && (p.y < field_top || p.y >= field_bottom)) || (p.dy > 0 && p.y >= p.floor);
    });
}

void Effects::update(const Play* play, const Settings& settings) {
    if (play != last_play_) { // another game, or a quick load
        particles_.clear();
        object_state_ = {};
        shots_ = {};
        enemy_shots_ = {};
        last_sprites_ = {};
        lights_.clear();
        waves_.clear();
        bubbles_.clear();
        free_particles_.clear();
        last_weapon_ = play ? play->chosen_weapon() : 0;
        last_painted_ = {};
        if (play)
            for (int p = 0; p < 9; ++p) last_painted_[std::size_t(p)] = play->painted(p);
        last_paint_ = {};
        if (play)
            for (int c = 0; c < 4; ++c) last_paint_[std::size_t(c)] = std::min<std::uint8_t>(play->colour()[std::size_t(c)], 0x14);
        last_flash_ = play ? play->flash_background() : 0;
        last_dying_ = play ? play->dying() : 0;
        last_play_ = play;
    }
    ++frame_;
    // The Wizlab (and after it): the weapon taken, the paint used.
    if (play && settings.lab) {
        const int weapon = play->chosen_weapon();
        if (weapon != 0 && last_weapon_ == 0) {
            const auto pos = play->token_position(weapon - 1);
            const auto before = particles_.size();
            burst(float(pos[0] + 12), float(pos[1] + 11), {1, 7, 13, 3}, 18, 1.4f, 30);
            free_particles_.insert(free_particles_.end(), particles_.begin() + long(before), particles_.end());
            particles_.resize(before);
        }
        for (int p = 0; p < 9; ++p) {
            if (play->painted(p) > last_painted_[std::size_t(p)] && play->painted(p) <= 3) {
                const auto pos = play->cauldron_position(3);
                const int colour = play->colour()[3] ? 7 : 1;
                for (int k = 0; k < 24; ++k) {
                    Particle q;
                    q.x = float(pos[0] + 4 + int(next() % 16));
                    q.y = float(pos[1] + 8 + int(next() % 10));
                    q.dx = (float(next() % 100) - 50.0f) / 150.0f;
                    q.dy = -1.2f - float(next() % 100) / 80.0f;
                    q.colour = k % 3 == 0 ? 1 : colour;
                    q.life = 20 + int(next() % 20);
                    free_particles_.push_back(q);
                }
            }
        }
    }
    if (play) {
        last_weapon_ = play->chosen_weapon();
        for (int p = 0; p < 9; ++p) last_painted_[std::size_t(p)] = play->painted(p);
    }
    step(free_particles_, false);
    if (!play || !playing(*play)) {
        particles_.clear();
        lights_.clear();
        waves_.clear();
        shots_ = {};
        enemy_shots_ = {};
        last_sprites_ = {};
        falling_ = {};
        if (play)
            for (int s = 0; s < 6; ++s) object_state_[std::size_t(s)] = play->objects()[std::size_t(s)].state;
        return;
    }
    // Explosions start where an object turns to its first explosion frame.
    for (int s = 0; s < 6; ++s) {
        const auto& o = play->objects()[std::size_t(s)];
        const bool started = o.active() && o.state == 4 && o.frame == explosion_first_frame && object_state_[std::size_t(s)] != 4;
        const bool splashed = o.active() && o.state == 3 && object_state_[std::size_t(s)] == 2;
        object_state_[std::size_t(s)] = o.active() ? o.state : 0;
        const auto& sp = play->object_sprite_of(s);
        if (splashed && settings.splashes) {
            // The droplet's sprite bottom is on the ground ($A8EC stops it at $CB).
            const int paint = sp.colour & 15;
            burst(float(sp.x + 12), float(sp.y + 19), {paint, paint, darker[std::size_t(paint)], 1}, 16, 2.0f, 40, true,
                  2.0f);
        }
        // Gone from inside the field: taken by the wiz, or caught by the catellite.
        auto& last = last_sprites_[std::size_t(s)];
        if (last.active && !o.active() && settings.pickups && last.x >= 0x30 && last.x < 0x150) {
            const bool taken = last.state >= 5 && last.state <= 7;
            const bool caught = last.state == 2 && last.y < 0xCB;
            if (taken || caught) {
                burst(float(last.x + 12), float(last.y + 11), {last.colour, 1, 7, 1}, 14, 1.1f, 26);
                for (auto& p : std::span(particles_).last(14)) p.dy -= 0.5f; // they rise
            }
        }
        last = {o.active(), sp.x, sp.y, sp.colour & 15, o.state};
        falling_[std::size_t(s)] = o.active() && o.state == 2;
        if (!started) continue;
        if (settings.lights) lights_.push_back({sp.x + 12, sp.y + 11, sp.colour & 15, 0});
        if (!settings.explosions) continue;
        std::vector<int> colours;
        const auto shape = play->sprite(explosion_first_frame);
        bool used[4]{};
        for (auto b : shape)
            for (int k = 0; k < 4; ++k) used[(b >> (2 * k)) & 3] = true;
        if (used[1]) colours.push_back(play->sprite_shared()[0] & 15);
        if (used[2]) colours.push_back(sp.colour & 15);
        if (used[3]) colours.push_back(play->sprite_shared()[1] & 15);
        colours.push_back(1); // a few white-hot pieces
        burst(float(sp.x + 12), float(sp.y + 11), colours, 40, 2.6f, 34);
    }
    // Shots: where each is drawn now; one that went out short of the screen's
    // edge hit something.
    const int xscroll = play->playfield().fine_scroll() ^ 7;
    for (int k = 0; k < 6; ++k) {
        const auto& s = play->shots()[std::size_t(k)];
        auto& t = shots_[std::size_t(k)];
        if (s.code != 0 && s.drawn_column >= 0) {
            const int c = s.colour & 15;
            t = {true, 24 + xscroll + 8 * s.drawn_column, Display::text_top + 8 * (s.drawn_row + 3),
                 s.dx > 0 ? 1 : s.dx < 0 ? -1 : 0, s.dy > 0 ? 1 : s.dy < 0 ? -1 : 0, c & 8 ? c & 7 : c, k >= 3};
            continue;
        }
        if (t.active && settings.shots) {
            const int column = (t.x - 24 - xscroll) / 8;
            const int row = (t.y - Display::text_top) / 8 - 3;
            const bool at_edge = (t.dx > 0 && column >= 0x25) || (t.dx < 0 && column <= 1) || (t.dy > 0 && row >= 0x13) ||
                                 (t.dy < 0 && row <= 0);
            if (!at_edge) burst(float(t.x + 4 + 4 * t.dx), float(t.y + 4), {t.colour, 1, 7}, 7, 1.3f, 12);
        }
        t.active = false;
    }
    // The aliens' shots: their cells while drawn; one that ends inside the
    // field ran into the landscape or the wiz.
    for (int k = 0; k < 16; ++k) {
        const auto& e = play->enemy_shots()[std::size_t(k)];
        auto& t = enemy_shots_[std::size_t(k)];
        if (e.state == 1 && e.drawn_column >= 0) {
            // The dot shows in the cell's colour (its low three bits in a multicolour cell).
            const int cell = play->cell_colour(e.drawn_column, e.drawn_row) & 15;
            t = {true, 24 + xscroll + 8 * e.drawn_column, Display::text_top + 8 * (e.drawn_row + 3), e.dx, e.dy,
                 cell & 8 ? cell & 7 : cell};
            continue;
        }
        if (t.active && e.state == 0 && settings.shots) {
            const int column = (t.x - 24 - xscroll) / 8, row = (t.y - Display::text_top) / 8 - 3;
            if (column > 0 && column < 0x26 && row > 0 && row < 0x13)
                burst(float(t.x + 4), float(t.y + 4), {t.colour, 1}, 5, 1.0f, 10);
        }
        t.active = false;
    }
    // The wiz: its burst, the smart bomb, its rings coming and going.
    const auto& wiz = play->wiz();
    const int wiz_x = wiz.x() + 12, wiz_y = wiz.y() + 11;
    if (settings.lights && play->dying() != 0 && last_dying_ == 0)
        lights_.push_back({wiz_x, wiz_y, play->wiz_colour() & 15, 0, 1.8f});
    last_dying_ = play->dying();
    // (The strobe's counter starts at 1 and has moved on by the end of the
    // frame; a blinking alien's starts at $0A.)
    const auto flash = play->flash_background();
    if (settings.lights && (play->stage() == Play::Stage::play || play->stage() == Play::Stage::bonus) && flash >= 1 && flash < 4 &&
        (last_flash_ == 0 || (last_flash_ & 0x80)))
        waves_.push_back({wiz_x, wiz_y, 0});
    last_flash_ = play->flash_background();
    if (settings.wiz && wiz.appearing()) {
        const auto shape = play->sprite(wiz.frame());
        const int pair_colour[4] = {0, play->sprite_shared()[0] & 15, play->wiz_colour() & 15, play->sprite_shared()[1] & 15};
        for (int tries = 0, made = 0; tries < 40 && made < 2; ++tries) {
            const int row = int(next() % 21), pair = int(next() % 12);
            const int value = shape[std::size_t(row * 3 + pair / 4)] >> (6 - 2 * (pair % 4)) & 3;
            if (value == 0) continue;
            Particle p;
            p.x = float(wiz.x() + 2 * pair);
            p.y = float(wiz.y() + 1 + row);
            p.dx = (float(next() % 100) - 50.0f) / 60.0f;
            p.dy = -0.4f - float(next() % 100) / 120.0f;
            p.colour = next() % 3 == 0 ? 1 : pair_colour[value];
            p.life = 14 + int(next() % 12);
            particles_.push_back(p);
            ++made;
        }
    }
    
    // The blackout: three steps darker after each frame it showed.
    if (settings.blackout) {
        if (!play->field_dark()) blackout_fade_ = 4;
        else if (blackout_fade_ > 0) --blackout_fade_;
    } else {
        blackout_fade_ = 0;
    }
    // Tunnels: sparkles at the mouth while the wiz sinks, at the exit while
    // it rises and the wiz slides out.
    if (settings.tunnels) {
        const auto& wiz = play->wiz();
        const std::vector<int> colours{play->wiz_colour() & 15, 1, 15, 7};
        if (wiz.tunnel() != 0 && play->arrival_phase() == 0)
            for (int k = 0; k < 2; ++k) swirl(float(wiz.x() + 12), float(wiz.y() + 20), 22.0f, colours);
        if (play->arrival_phase() == 1)
            for (int k = 0; k < 2; ++k) swirl(float(0xE0 + 8), float(0x46 + 40), 36.0f, {1, 15, 12, 3});
        if (play->arrival_phase() == 2) swirl(float(play->arrival_x() + 12), float(wiz.y() + 18), 18.0f, colours);
    }
    // The cauldrons: bubbles from the bottom for paint poured in.
    for (int c = 0; c < 4; ++c) {
        const auto now = std::min<std::uint8_t>(play->colour()[std::size_t(c)], 0x14);
        if (settings.cauldrons && now > last_paint_[std::size_t(c)] && !play->in_lab())
            for (int k = 0; k < 3; ++k) bubbles_.push_back({c, 4 + int(next() % 16), 20 - int(next() % 3), int(next() % 4), false});
        last_paint_[std::size_t(c)] = now;
    }
    for (auto& b : bubbles_) {
        if (b.popped) {
            b.clock = 99;
            continue;
        }
        if (++b.clock % 3 == 0) --b.row;
        const int surface = 20 - std::min<int>(play->colour()[std::size_t(b.cauldron)], 0x14);
        if (b.row <= surface) b.popped = true; // shows one frame above the surface
    }
    std::erase_if(bubbles_, [](const Bubble& b) { return b.clock == 99; });
    if (bubbles_.size() > 60) bubbles_.erase(bubbles_.begin(), bubbles_.begin() + long(bubbles_.size() - 60));
    if (const auto tint = play->spray()[2] & 15; tint != 0) spray_tint_ = std::uint8_t(tint);
    // The catellite: sparks per hit taken, a burst when it is lost.
    {
        const auto hits = play->catellite().hits();
        const auto& c = play->catellite().sprite();
        const bool on = play->catellite_on();
        if (settings.catellite && on && last_catellite_on_ && hits == last_catellite_hits_ + 1 && hits < 0x80)
            burst(float(c.x + 12), float(c.y + 11), {c.colour & 15, 1, 7}, 10, 1.6f, 16);
        if (settings.catellite && !on && last_catellite_on_ && last_catellite_hits_ >= 8 && last_catellite_hits_ < 0x80) {
            const auto& l = last_catellite_;
            burst(float(l.x + 12), float(l.y + 11), {l.colour & 15, 1, 7, 15}, 34, 2.4f, 30);
            if (settings.lights) lights_.push_back({l.x + 12, l.y + 11, l.colour & 15, 0, 1.2f});
        }
        last_catellite_hits_ = hits;
        last_catellite_on_ = on;
        last_catellite_ = c;
    }
    for (auto& w : waves_) ++w.age;
    std::erase_if(waves_, [](const Wave& w) { return w.age > 40; });
    for (auto& l : lights_) ++l.age;
    std::erase_if(lights_, [](const Light& l) { return l.age > 4; });
    step(particles_, true);
}

void Effects::draw_flight(platform::Image& image, const Display& d, const Play& play) const {
    // A dithered glow round the text's pixels, a step darker than its colour.
    const int shift = 8 * d.extension;
    const int colour = play.flight_colour() & 15;
    const int glow = darker[std::size_t(colour)] ? darker[std::size_t(colour)] : 11;
    for (int n = 2; n < 8; ++n) {
        const auto shape = play.sprite(0xB2 + n);
        const auto pos = play.flight_text_position(n);
        for (int row = 0; row < 21; ++row)
            for (int px = 0; px < 24; ++px) {
                if (!(shape[std::size_t(row * 3 + px / 8)] >> (7 - px % 8) & 1)) continue;
                for (int gy = -2; gy <= 2; ++gy)
                    for (int gx = -2; gx <= 2; ++gx) {
                        const int vx = pos[0] + px + gx, raster = pos[1] + 1 + row + gy;
                        if (((vx + raster + int(frame_ / 3)) & 1) || raster < field_top || raster >= field_bottom) continue;
                        const int x = vx + shift, y = raster - Display::first_line;
                        if (x < 0 || y < 0 || x >= image.width || y >= image.height) continue;
                        auto& q = image.pixels[std::size_t(y * image.width + x)];
                        if (same(q, c64::colour(d.line(raster).background))) q = c64::colour(glow);
                    }
            }
    }
}

void Effects::draw(platform::Image& image, const Display& d, const Play& play, const Settings& settings) const {
    if (play.flying_text() && settings.flight) draw_flight(image, d, play);
    {
        const int shift = 8 * d.extension;
        for (const auto& p : free_particles_) {
            int c = p.colour;
            if (p.life - p.age < 6) c = darker[std::size_t(c)];
            if (c == 0) continue;
            const int x = (int(std::floor(p.x)) & ~1) + shift, y = int(std::floor(p.y)) - Display::first_line;
            for (int k = 0; k < 2; ++k)
                if (x + k >= 0 && y >= 0 && x + k < image.width && y < image.height)
                    image.pixels[std::size_t(y * image.width + x + k)] = c64::colour(c);
        }
    }
    if (!playing(play)) return;
    const int shift = 8 * d.extension;
    auto at = [&](int vic_x, int raster) -> platform::Color* {
        const int x = vic_x + shift, y = raster - Display::first_line;
        if (x < 0 || y < 0 || x >= image.width || y >= image.height) return nullptr;
        return &image.pixels[std::size_t(y * image.width + x)];
    };
    auto inside = [&](int vic_x, int raster) {
        if (raster < field_top || raster >= field_bottom) return false;
        const auto w = window(d, raster);
        return vic_x >= w[0] && vic_x < w[1];
    };
    auto background = [&](int raster) { return c64::colour(d.line(raster).background); };

    // Star drift: each star's pair of pixels moved to its single-pixel place.
    if (settings.stars) {
        const auto& field = play.playfield();
        const int xscroll = field.fine_scroll() ^ 7;
        std::vector<std::array<std::uint8_t, game::Playfield::rows>> sides;
        const int e = d.line(field_top).wide ? d.extension : 0;
        for (int c = -e; c < 40 + e; ++c)
            if (c < 0 || c >= 40) sides.push_back(field.side_column(c));
        auto code_at = [&](int column, int row) -> int {
            if (column >= 0 && column < 40) return field.code(column, row);
            const int i = column < 0 ? column + e : column - 40 + e;
            return sides[std::size_t(i)][std::size_t(row)];
        };
        for (int band = 0; band < game::Playfield::sky_bands; ++band) {
            const auto& star = field.stars()[std::size_t(band)];
            const int code = 1 + 18 * band + (star.offset + 2) / 8, glyph_row = (star.offset + 2) % 8;
            const int pair = 2 * ((star.position >> 5) & 3), fine = (star.position >> 4) & 7;
            if (fine == pair) continue; // already where it belongs
            for (int row = 0; row < game::Playfield::rows; ++row) {
                const int raster = Display::text_top + 8 * (row + 3) + glyph_row;
                if (raster < field_top || raster >= field_bottom) continue;
                const auto bg = background(raster);
                for (int column = -e; column < 40 + e; ++column) {
                    if (code_at(column, row) != code) continue;
                    const int x = 24 + xscroll + 8 * column;
                    auto* a = at(x + pair, raster);
                    auto* b = at(x + pair + 1, raster);
                    if (!a || !b || !inside(x + pair, raster) || !same(*a, *b) || same(*a, bg)) continue;
                    const auto colour = *a;
                    *a = *b = bg;
                    for (int k = 0; k < 2; ++k) {
                        auto* p = at(x + fine + k, raster);
                        if (p && inside(x + fine + k, raster) && same(*p, bg)) *p = colour;
                    }
                }
            }
        }
    }

    // Shot streaks: the shot's own pixels, twice, fainter, behind it.
    if (settings.shots) {
        for (const auto& t : shots_) {
            if (!t.active) continue;
            const auto colour = c64::colour(t.colour);
            for (int y = 0; y < 8; ++y) {
                const int raster = t.y + y;
                for (int x = 0; x < 8; ++x) {
                    const auto* p = at(t.x + x, raster);
                    if (!p || !same(*p, colour)) continue;
                    for (int i = 1; i <= 2; ++i) {
                        const int tx = t.x + x - t.dx * 5 * i, ty = raster - t.dy * 4 * i;
                        auto* q = at(tx, ty);
                        int c = t.colour;
                        for (int k = 0; k < i; ++k) c = darker[std::size_t(c)];
                        if (q && c != 0 && inside(tx, ty) && same(*q, background(ty))) *q = c64::colour(c);
                    }
                    if (!t.blazer) continue;
                    // The blazer's glow: a dithered fringe round its pixels.
                    const int glow = darker[std::size_t(t.colour)];
                    for (int gy = -2; gy <= 2; ++gy)
                        for (int gx = -3; gx <= 3; ++gx) {
                            const int vx = t.x + x + gx, r = raster + gy;
                            if (((vx + r) & 1) || glow == 0) continue;
                            auto* q = at(vx, r);
                            if (q && inside(vx, r) && same(*q, background(r))) *q = c64::colour(glow);
                        }
                }
            }
        }
    }

    // The aliens' shots: their dot drawn again back along the flight, fainter.
    if (settings.shots) {
        for (const auto& t : enemy_shots_) {
            if (!t.active || (t.dx == 0 && t.dy == 0)) continue;
            const auto colour = c64::colour(t.colour);
            const float length = std::sqrt(float(t.dx * t.dx + t.dy * t.dy));
            const float ux = -float(t.dx) / length, uy = -float(t.dy) / length;
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x) {
                    const auto* p = at(t.x + x, t.y + y);
                    if (!p || !same(*p, colour)) continue;
                    for (int i = 1; i <= 3; ++i) {
                        const int tx = t.x + x + int(std::lround(ux * 2.5f * i)), raster = t.y + y + int(std::lround(uy * 2.0f * i));
                        auto* q = at(tx, raster);
                        int c = t.colour; // its own colour, then darker
                        for (int k = 1; k < i; ++k) c = darker[std::size_t(c)];
                        if (q && c != 0 && inside(tx, raster) && same(*q, background(raster))) *q = c64::colour(c);
                    }
                }
        }
    }

    // Falling paint: the droplet's own pixels again above it, fainter.
    if (settings.splashes) {
        for (int s = 0; s < 6; ++s) {
            if (!falling_[std::size_t(s)]) continue;
            const auto& l = last_sprites_[std::size_t(s)];
            const auto colour = c64::colour(l.colour);
            for (int y = 0; y < 21; ++y)
                for (int x = 0; x < 24; ++x) {
                    const int raster = l.y + 1 + y;
                    const auto* p = at(l.x + x, raster);
                    if (!p || !same(*p, colour)) continue;
                    for (int i = 1; i <= 2; ++i) {
                        const int r = raster - 3 * i;
                        auto* q = at(l.x + x, r);
                        int c = l.colour;
                        for (int k = 1; k < i; ++k) c = darker[std::size_t(c)];
                        if (q && c != 0 && inside(l.x + x, r) && same(*q, background(r))) *q = c64::colour(c);
                    }
                }
        }
    }

    // Explosion light: a widening ring of dithered light in its colour.
    if (settings.lights) {
        for (const auto& l : lights_) {
            const float inner = (6.0f + 3.5f * float(l.age)) * l.size, outer = inner + 3.0f * l.size;
            int c = l.colour;
            if (l.age >= 3) c = darker[std::size_t(c)];
            if (c == 0) c = 11;
            for (int y = int(-outer); y <= int(outer); ++y)
                for (int x = int(-outer * 1.6f); x <= int(outer * 1.6f); ++x) {
                    const float r = std::sqrt(float(x * x) / 2.56f + float(y * y));
                    if (r < inner || r > outer) continue;
                    const int vx = l.x + x, raster = l.y + y;
                    if (((vx >> 1) + raster) & 1) continue; // a checker of pixel pairs
                    auto* q = at(vx, raster);
                    if (q && inside(vx, raster) && same(*q, background(raster))) *q = c64::colour(c);
                }
        }
    }

    // The shield: a shimmering field, segments of a ring lit in turn.
    auto field_ring = [&](int cx, int cy, float radius) {
        static constexpr int colours[] = {1, 13, 3, 15};
        for (int y = int(-radius) - 2; y <= int(radius) + 2; ++y)
            for (int x = int(-radius * 1.6f) - 3; x <= int(radius * 1.6f) + 3; ++x) {
                const float r = std::sqrt(float(x * x) / 2.56f + float(y * y));
                if (r < radius - 1.0f || r > radius + 1.0f) continue;
                const int segment = int((std::atan2(float(y), float(x) / 1.6f) + 3.1416f) * 12.0f / 6.2832f);
                const int phase = (segment + int(frame_ / 2)) % 6;
                if (phase >= 3) continue;
                const int vx = cx + x, raster = cy + y;
                if ((vx + raster) & 1) continue;
                auto* q = at(vx, raster);
                if (q && inside(vx, raster) && same(*q, background(raster))) *q = c64::colour(colours[(phase + segment) % 4]);
            }
    };
    if (settings.lights && play.stage() != Play::Stage::vanish) {
        const auto& wiz = play.wiz();
        if (play.shield() != 0 && play.dying() == 0) field_ring(wiz.x() + 12, wiz.y() + 11, 14.0f);
        if (play.catellite_on() && play.catellite().shield() != 0) {
            const auto& c = play.catellite().sprite();
            field_ring(c.x + 12, c.y + 11, 10.0f);
        }
        // The wizspray's rings: a dithered glow along their pixels.
        const auto spray = play.spray();
        const int glow = darker[spray_tint_ & 15] ? darker[spray_tint_ & 15] : 11;
        for (int part = 0; part < 2; ++part) {
            if (spray[std::size_t(part)] == 5) continue;
            const auto shape = play.sprite(spray[std::size_t(part)]);
            const int sx = wiz.x(), sy = (wiz.y() + (part == 0 ? -0x17 : 0x17)) & 0xFF;
            for (int row = 0; row < 21; ++row)
                for (int pair = 0; pair < 12; ++pair) {
                    if (!(shape[std::size_t(row * 3 + pair / 4)] >> (6 - 2 * (pair % 4)) & 3)) continue;
                    for (int gy = -1; gy <= 1; ++gy)
                        for (int gx = -2; gx <= 3; ++gx) {
                            const int vx = sx + 2 * pair + gx, raster = sy + 1 + row + gy;
                            if (((vx >> 1) + raster + int(frame_)) & 1) continue;
                            auto* q = at(vx, raster);
                            if (q && inside(vx, raster) && same(*q, background(raster))) *q = c64::colour(glow);
                        }
                }
        }
    }

    // Cauldron bubbles: light dots inside the paint, a white speck at the top.
    if (settings.cauldrons) {
        for (const auto& b : bubbles_) {
            const auto pos = play.cauldron_position(b.cauldron);
            const int raster = pos[1] + 1 + b.row - (b.popped ? 1 : 0);
            const int vx = pos[0] + (b.x & ~1);
            for (int k = 0; k < 2; ++k) {
                auto* q = at(vx + k, raster);
                if (!q) continue;
                if (b.popped) *q = c64::colour(1);
                else if (!same(*q, c64::colour(0))) *q = c64::colour(k == 0 ? 15 : 1); // only over the paint
            }
        }
    }

    // The smart bomb's wave: a ring of light out over the whole field.
    for (const auto& w : waves_) {
        const float inner = 6.0f + 10.0f * float(w.age), outer = inner + 4.0f;
        static constexpr int ramp[] = {1, 1, 1, 15, 15, 12, 12, 11};
        const int ramp_colour = ramp[std::min(7, w.age / 5)];
        for (int raster = std::max(field_top, int(w.y - outer)); raster < std::min(field_bottom, int(w.y + outer) + 1); ++raster) {
            // Kept visible on the strobe's own colour.
            const int line_bg = d.line(raster).background & 15;
            const int c = ramp_colour != line_bg ? ramp_colour : line_bg == 1 ? 11 : 1;
            const float dy = float(raster - w.y);
            const float h2o = outer * outer - dy * dy;
            if (h2o < 0) continue;
            const float h2i = inner * inner - dy * dy;
            const int xo = int(std::sqrt(h2o) * 1.6f), xi = h2i > 0 ? int(std::sqrt(h2i) * 1.6f) : 0;
            const auto bg = background(raster);
            for (int side = -1; side <= 1; side += 2)
                for (int k = xi; k <= xo; ++k) {
                    const int vx = w.x + side * k;
                    if (((vx >> 1) + raster) & 1) continue;
                    auto* q = at(vx, raster);
                    if (q && inside(vx, raster) && same(*q, bg)) *q = c64::colour(c);
                }
        }
    }

    // Debris and sparks, two pixels wide like the multicolour pixels.
    for (const auto& p : particles_) {
        int c = p.colour;
        const int fade = p.life - p.age;
        if (fade < 6) c = darker[std::size_t(c)];
        if (fade < 3) c = darker[std::size_t(c)];
        if (c == 0) continue;
        const int x = int(std::floor(p.x)) & ~1, raster = int(std::floor(p.y));
        const int lines = p.age < p.life / 3 ? 2 : 1; // chunks first, then specks
        for (int dy = 0; dy < lines; ++dy)
            for (int k = 0; k < 2; ++k) {
                if (!inside(x + k, raster + dy)) continue;
                if (auto* q = at(x + k, raster + dy)) *q = c64::colour(c);
            }
    }
}

} // namespace wizball::enhanced

namespace wizball::enhanced {

void Effects::update_front(const game::FrontEnd& front, const Settings& settings) {
    const auto stars = front.star_sprites();
    if (!settings.front || stars.sprites.empty()) {
        star_history_ = {};
        return;
    }
    star_history_[2] = std::move(star_history_[1]);
    star_history_[1] = std::move(star_history_[0]);
    star_history_[0] = stars.sprites;
}

void Effects::draw_front(platform::Image& image, const Display& d, const game::FrontEnd& front) const {
    const auto stars = front.star_sprites();
    if (stars.sprites.empty()) return;
    const int shift = 8 * d.extension;
    // Frames 1 and 2 back (0 is the one shown), fainter each.
    for (int back = 1; back <= 2; ++back) {
        const auto& sprites = star_history_[std::size_t(back)];
        int colour = stars.colour & 15;
        for (int k = 0; k < back; ++k) colour = darker[std::size_t(colour)];
        if (colour == 0) colour = 11;
        for (const auto& s : sprites)
            for (int row = 0; row < 21; ++row) {
                const int raster = s[1] + 1 + row;
                if (s[2] ? raster >= 136 : raster < 136) continue; // the half this copy shows in
                if (raster < Display::text_top || raster >= Display::text_top + 200) continue;
                for (int px = 0; px < 24; ++px) {
                    if (!(stars.shape[std::size_t(row * 3 + px / 8)] >> (7 - px % 8) & 1)) continue;
                    const int x = s[0] + px + shift, y = raster - Display::first_line;
                    if (x < 24 + shift || x >= 344 + shift || y < 0 || y >= image.height) continue;
                    auto& q = image.pixels[std::size_t(y * image.width + x)];
                    // (The screens' empty cells are black: the letters are inverted.)
                    if (same(q, c64::colour(0)) || same(q, c64::colour(d.line(raster).background))) q = c64::colour(colour);
                }
            }
    }
}

} // namespace wizball::enhanced
