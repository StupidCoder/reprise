// Carnage mode (not the original game): fire is a rapid paint gun. Its
// shots fly at a pixel's precision, burst the aliens they hit into paint
// (through the game's own hit, $6FBE, so they die, drop or turn to pearls
// as the original's shots make them), and bite into the landscape: a
// crater of carved pixels with a splash of paint round it. A damaged cell
// keeps its own pixels (by its place in the map, so they stay while the
// view scrolls and when the place comes again); one with too little left
// opens to sky for good, and the game's cell tests (the wiz's bounce, the
// aliens', shots) then find nothing there. Paint thrown out of a burst
// alien flies, falls and splashes where it lands. A completed place floods
// with colour from the wiz while it fades.
//
// Landscape cut off from the ground (a pillar shot through at its foot, a
// ledge undercut) comes loose as a body that falls and turns over, crushes
// what it lands on and shatters into rubble. The smart bomb sends a
// shockwave that cracks the landscape it passes. A fast wiz rams aliens and
// bites into what it bumps; a catellite left to follow swings on a tether
// and smashes what it hits. Kills close together make combos (a bonus each),
// combos fill a meter, and a full one gives a while of a rainbow overdrive
// gun; hard hits ask the picture for a moment's stop and a shake.
// Carnage keeps its own random numbers: the game's stay as they were.

#include "wizball_c64/game/play.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace wizball::game {
namespace {
constexpr int gun_period = 3;          // passes between shots while fire is held
constexpr float tracer_speed = 9.0f;   // pixels a pass (the original's shot: a cell, 8)
constexpr int tracer_life = 50;
constexpr float crater = 3.2f, splash_ring = 7.5f;
constexpr int crumble_pairs = 3;       // a cell with fewer solid pairs left opens
constexpr float blob_gravity = 0.12f;
// The paint colours: the three cauldrons' ($B516, $B517, $B519).
constexpr std::uint8_t paint[] = {2, 5, 6};
// The flood's bands, out from the wiz.
constexpr std::uint8_t flood_colours[] = {2, 8, 7, 13, 5, 3, 14, 6, 4, 10};
constexpr int window_left = 24, window_right = 344;
constexpr int ground_rows = 18;        // cells from this row down hold what stands on them
constexpr int body_limit = 160;        // a loose part bigger than this many cells holds
constexpr float body_gravity = 0.22f, body_fall = 7.0f;
constexpr int combo_window = 100;      // passes (two seconds) to add to a combo
constexpr float meter_per_kill = 0.09f;
constexpr int overdrive_passes = 300;
constexpr float flail_length = 30.0f;  // pixels (a line is a pixel high, a pixel 1/1.6 wide)
constexpr std::uint8_t rainbow[] = {2, 8, 7, 13, 5, 3, 14, 6, 4, 10};
constexpr int field_top = Display::text_top + 3 * 8, field_bottom = field_top + Playfield::rows * 8;
} // namespace

std::uint8_t Play::carnage_random() {
    carnage_seed_ ^= carnage_seed_ << 13;
    carnage_seed_ ^= carnage_seed_ >> 17;
    carnage_seed_ ^= carnage_seed_ << 5;
    return std::uint8_t(carnage_seed_ >> 24);
}

int Play::carnage_pair(int x, int y) const {
    if (y < field_top || y >= field_bottom) return -1;
    const int gx = x - 24 - (playfield_.fine_scroll() ^ 7);
    if (gx < 0 || gx >= 8 * Playfield::columns) return -1;
    const int column = gx / 8, row = (y - field_top) / 8, sub = (y - field_top) % 8, pair = (gx % 8) / 2;
    const auto code = playfield_.code(column, row);
    if (!(code & 0x80)) return -1;
    if (const auto it = damage_.find(playfield_.cell_key(column, row)); it != damage_.end())
        return it->second[std::size_t(sub * 4 + pair)];
    return (carnage_glyph(code)[std::size_t(sub)] >> (6 - 2 * pair)) & 3;
}

Glyph Play::carnage_glyph(std::uint8_t code) const {
    // As play_charset() has it: the place's characters from $A0, the rest
    // the playfield charset's.
    const auto& chars = assets_.places[std::size_t(charset_place_)].characters;
    if (code >= 0xA0 && std::size_t(code - 0xA0) < chars.size()) return chars[std::size_t(code - 0xA0)];
    return assets_.play_charset[code];
}

Display::CellArt& Play::carnage_art(int column, int row) {
    const auto key = playfield_.cell_key(column, row);
    if (const auto it = damage_.find(key); it != damage_.end()) return it->second;
    Display::CellArt art{};
    const auto glyph = carnage_glyph(playfield_.code(column, row));
    for (int y = 0; y < 8; ++y)
        for (int p = 0; p < 4; ++p) art[std::size_t(y * 4 + p)] = (glyph[std::size_t(y)] >> (6 - 2 * p)) & 3;
    return damage_[key] = art;
}

void Play::crumble(int column, int row) {
    const auto key = playfield_.cell_key(column, row);
    const auto it = damage_.find(key);
    if (it == damage_.end()) return;
    int solid = 0;
    for (auto v : it->second) solid += v != 0;
    if (solid >= crumble_pairs) return;
    damage_.erase(it);
    playfield_.open_cell(column, row);
    detach_from(column, row);
}

void Play::carve(float x, float y, float radius) {
    last_hit_x_ = x;
    // Every pair within the radius, in every landscape cell it reaches.
    const int xscroll = playfield_.fine_scroll() ^ 7;
    for (int py = int(y - radius); py <= int(y + radius); ++py)
        for (int px = int(x - radius * 1.6f) & ~1; px <= int(x + radius * 1.6f); px += 2) {
            const float dx = (float(px) + 1.0f - x) / 1.6f, dy = float(py) - y;
            if (dx * dx + dy * dy > radius * radius) continue;
            if (carnage_pair(px, py) <= 0) continue;
            const int gx = px - 24 - xscroll;
            const int column = gx / 8, row = (py - field_top) / 8;
            carnage_art(column, row)[std::size_t(((py - field_top) % 8) * 4 + (gx % 8) / 2)] = 0;
        }
    // Cells left with too little crumble away.
    for (int py = int(y - radius) - 8; py <= int(y + radius) + 8; py += 8)
        for (int px = int(x - radius * 1.6f) - 8; px <= int(x + radius * 1.6f) + 8; px += 8) {
            const int gx = px - 24 - xscroll;
            if (gx < 0 || py < field_top || py >= field_bottom || gx >= 8 * Playfield::columns) continue;
            crumble(gx / 8, (py - field_top) / 8);
        }
}

void Play::splash(float x, float y, float radius, std::uint8_t colour, int chance) {
    // Solid pairs within the radius take the paint (`chance` of 256 each).
    const int xscroll = playfield_.fine_scroll() ^ 7;
    for (int py = int(y - radius); py <= int(y + radius); ++py)
        for (int px = int(x - radius * 1.6f) & ~1; px <= int(x + radius * 1.6f); px += 2) {
            const float dx = (float(px) + 1.0f - x) / 1.6f, dy = float(py) - y;
            if (dx * dx + dy * dy > radius * radius || carnage_random() >= chance) continue;
            if (carnage_pair(px, py) <= 0) continue;
            const int gx = px - 24 - xscroll;
            carnage_art(gx / 8, (py - field_top) / 8)[std::size_t(((py - field_top) % 8) * 4 + (gx % 8) / 2)] =
                std::uint8_t(0x10 | colour);
        }
}

void Play::carnage_pass() {
    flood_radius_ = -1; // (a flood starts afresh at the next place's end)
    // The gun: a shot every few passes while fire is held, the way the wiz
    // last moved, in the cauldrons' colours in turn.
    const bool held = !(wiz_stick_.bits & 0x10) && dying_ == 0;
    const bool overdrive = carnage_score_.overdrive > 0;
    if (held && ++gun_clock_ >= (overdrive ? 1 : gun_period)) {
        gun_clock_ = 0;
        const float direction = wiz_.last_horizontal() >= 0 ? 1.0f : -1.0f;
        // Down or up on the stick angles the shot that way; the overdrive
        // fires a fan of three in the rainbow's colours.
        const float slope = !(wiz_stick_.bits & 2) ? 0.45f : !(wiz_stick_.bits & 1) ? -0.45f : 0.0f;
        for (int k = overdrive ? -1 : 0; k <= (overdrive ? 1 : 0); ++k) {
            const auto colour = overdrive ? rainbow[std::size_t(gun_colour_++ % 10)] : paint[std::size_t(gun_colour_++ % 3)];
            const float s = slope + 0.22f * float(k);
            tracers_.push_back({float(wiz_.x() + 12) + direction * 10.0f,
                                float(wiz_.y() + 11 + int(carnage_random() % 3) - 1), direction * tracer_speed,
                                s * tracer_speed / 1.6f, colour, tracer_life});
        }
        sound_->start_effect(0);
    }
    if (!held) gun_clock_ = gun_period - 1; // the first shot comes at once
    // The view's scroll carries the shots and blobs along with the landscape.
    const float scroll = -float(playfield_.pixel_delta());
    for (auto& t : tracers_) {
        t.x += scroll;
        bool hit = false;
        const float steps = std::abs(t.dx) / 2.0f;
        for (float travelled = 0; travelled < steps && !hit; travelled += 1.0f) {
            t.x += t.dx / steps;
            t.y += t.dy / steps;
            if (t.x < window_left || t.x >= window_right || t.y < field_top || t.y >= field_bottom) {
                t.life = 0;
                break;
            }
            // An alien: the game's own hit ($6FBE marks it).
            if (const int s = object_at(int(t.x) >> 1, int(t.y)); s >= 0) {
                const auto& sp = object_sprites_[std::size_t(s)];
                for (int k = 0; k < 14; ++k) {
                    const float angle = float(carnage_random()) / 256.0f * 6.2832f;
                    const float v = 1.0f + float(carnage_random() % 100) / 50.0f;
                    blobs_.push_back({float(sp.x + 12), float(sp.y + 11), std::cos(angle) * v * 1.6f,
                                      std::sin(angle) * v - 1.2f, k % 2 ? t.colour : std::uint8_t(sp.colour & 15), 60});
                }
                hit = true;
                break;
            }
            // The landscape: a crater and a splash of the shot's paint.
            if (carnage_pair(int(t.x), int(t.y)) > 0) {
                carve(t.x, t.y, crater);
                splash(t.x, t.y, splash_ring, t.colour, 170);
                hit = true;
            }
        }
        if (hit || --t.life <= 0) t.life = 0;
    }
    std::erase_if(tracers_, [](const Tracer& t) { return t.life <= 0; });
    // Paint thrown out of the aliens: falls and splashes where it lands.
    for (auto& b : blobs_) {
        b.x += scroll + b.dx;
        b.y += b.dy;
        b.dy += blob_gravity;
        b.dx *= 0.98f;
        if (b.y >= field_bottom || b.x < window_left || b.x >= window_right || --b.life <= 0) {
            b.life = 0;
            continue;
        }
        if (carnage_pair(int(b.x), int(b.y)) > 0) {
            splash(b.x, b.y, 2.5f, b.colour, 230);
            b.life = 0;
        }
    }
    std::erase_if(blobs_, [](const PaintBlob& b) { return b.life <= 0; });
    move_bodies();
    carnage_waves();
    wreck();
    carnage_kills();
}

// --- loose landscape -------------------------------------------------------------------

std::uint8_t Play::pair_colour(std::uint8_t value) const {
    if (value & 0x10) return value & 15;
    const std::uint8_t c[4] = {0, shared1_, shared2_, std::uint8_t(field_colour() & 7)};
    return c[value & 3] & 15;
}

void Play::detach_from(int column, int row) {
    // The landscape next to an opened cell: a part that no longer reaches
    // the ground rows or the screen's edges (where it may go on) falls.
    auto solid = [&](int c, int r) {
        return c >= 0 && c < Playfield::columns && r >= 0 && r < Playfield::rows && (playfield_.code(c, r) & 0x80);
    };
    std::vector<char> seen(std::size_t(Playfield::columns * Playfield::rows), 0);
    const int next[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (const auto& n : next) {
        const int c0 = column + n[0], r0 = row + n[1];
        if (!solid(c0, r0) || seen[std::size_t(r0 * Playfield::columns + c0)]) continue;
        std::vector<int> group{r0 * Playfield::columns + c0};
        seen[std::size_t(group[0])] = 1;
        bool held = false;
        for (std::size_t i = 0; i < group.size() && !held; ++i) {
            const int c = group[i] % Playfield::columns, r = group[i] / Playfield::columns;
            if (r >= ground_rows || c == 0 || c == Playfield::columns - 1 || int(group.size()) > body_limit) {
                held = true;
                break;
            }
            for (const auto& m : next) {
                const int cc = c + m[0], rr = r + m[1];
                if (!solid(cc, rr) || seen[std::size_t(rr * Playfield::columns + cc)]) continue;
                seen[std::size_t(rr * Playfield::columns + cc)] = 1;
                group.push_back(rr * Playfield::columns + cc);
            }
        }
        if (!held) make_body(group);
    }
}

void Play::make_body(const std::vector<int>& cells) {
    const int xscroll = playfield_.fine_scroll() ^ 7;
    Body b;
    float sx = 0, sy = 0;
    std::vector<std::array<int, 3>> pixels;
    for (int i : cells) {
        const int c = i % Playfield::columns, r = i / Playfield::columns;
        Display::CellArt art{};
        if (const auto it = damage_.find(playfield_.cell_key(c, r)); it != damage_.end()) {
            art = it->second;
        } else {
            const auto glyph = carnage_glyph(playfield_.code(c, r));
            for (int y = 0; y < 8; ++y)
                for (int p = 0; p < 4; ++p) art[std::size_t(y * 4 + p)] = (glyph[std::size_t(y)] >> (6 - 2 * p)) & 3;
        }
        for (int y = 0; y < 8; ++y)
            for (int p = 0; p < 4; ++p) {
                const auto v = art[std::size_t(y * 4 + p)];
                if (v == 0) continue;
                const int x = 24 + xscroll + 8 * c + 2 * p, yy = field_top + 8 * r + y;
                pixels.push_back({x, yy, v});
                sx += float(x);
                sy += float(yy);
            }
        damage_.erase(playfield_.cell_key(c, r));
        playfield_.open_cell(c, r);
    }
    if (pixels.empty()) return;
    b.x = sx / float(pixels.size());
    b.y = sy / float(pixels.size());
    for (const auto& p : pixels) b.pixels.push_back({std::int16_t(p[0] - int(b.x)), std::int16_t(p[1] - int(b.y)), std::uint8_t(p[2])});
    // It tips away from the side it was hit on, a tall part more slowly.
    const float side = last_hit_x_ < b.x ? 1.0f : -1.0f;
    b.spin = side * (0.02f + float(carnage_random() % 20) / 1000.0f);
    b.vx = side * 0.3f;
    new_bodies_.push_back(std::move(b));
    sound_->start_effect(3);
}

void Play::move_bodies() {
    const float scroll = -float(playfield_.pixel_delta());
    for (auto& b : bodies_) {
        b.x += scroll + b.vx;
        b.vy = std::min(body_fall, b.vy + body_gravity);
        b.y += b.vy;
        b.angle += b.spin;
        ++b.age;
        const float cs = std::cos(b.angle), sn = std::sin(b.angle);
        bool landed = b.y > float(field_bottom + 40);
        for (std::size_t i = 0; i < b.pixels.size() && !landed; i += 3) {
            const float dx = float(b.pixels[i].dx) / 1.6f, dy = float(b.pixels[i].dy);
            const int px = int(b.x + (dx * cs - dy * sn) * 1.6f), py = int(b.y + dx * sn + dy * cs);
            if (py >= field_bottom) landed = true;
            else if (carnage_pair(px, py) > 0) landed = true;
            // What it falls on: aliens are crushed, the wiz too.
            if (i % 12 == 0) object_at(px >> 1, py);
            if (std::abs(px - (wiz_.x() + 12)) < 9 && std::abs(py - (wiz_.y() + 11)) < 9 && dying_ == 0) ++wiz_hit_;
        }
        if (landed) {
            land(b);
            b.age = -1;
        }
    }
    std::erase_if(bodies_, [](const Body& b) { return b.age < 0; });
    for (auto& b : new_bodies_) bodies_.push_back(std::move(b));
    new_bodies_.clear();
}

void Play::land(const Body& b) {
    // It shatters: rubble flying (splashing its colours where it lands),
    // craters where it struck.
    const float cs = std::cos(b.angle), sn = std::sin(b.angle);
    for (std::size_t i = 0; i < b.pixels.size(); i += 5) {
        const float dx = float(b.pixels[i].dx) / 1.6f, dy = float(b.pixels[i].dy);
        const float px = b.x + (dx * cs - dy * sn) * 1.6f, py = b.y + dx * sn + dy * cs;
        const float angle = float(carnage_random()) / 256.0f * 3.1416f + 3.1416f; // upwards
        const float v = 0.8f + float(carnage_random() % 100) / 45.0f;
        blobs_.push_back({px, std::min(py, float(field_bottom - 1)), std::cos(angle) * v * 1.6f, std::sin(angle) * v,
                          pair_colour(b.pixels[i].value), 50});
    }
    for (int k = 0; k < 3; ++k) {
        const auto& p = b.pixels[std::size_t(carnage_random()) % b.pixels.size()];
        const float dx = float(p.dx) / 1.6f, dy = float(p.dy);
        carve(b.x + (dx * cs - dy * sn) * 1.6f, b.y + dx * sn + dy * cs + 3.0f, 4.0f);
    }
    impact(b.pixels.size() > 300 ? 3 : 2);
    add_points(int(b.pixels.size() / 40) + 1);
    sound_->start_effect(5);
}

// --- the smart bomb's shockwave, the wiz and the catellite ------------------------------

void Play::carnage_smart_bomb() {
    waves_.push_back({float(wiz_.x() + 12), float(wiz_.y() + 11), 0});
    impact(3);
}

void Play::carnage_waves() {
    const float scroll = -float(playfield_.pixel_delta());
    for (auto& w : waves_) {
        w.x += scroll;
        w.radius += 9.0f;
        // Points round the ring a few pixels apart: what is landscape there
        // cracks (a small crater and a splash).
        const int points = std::max(12, int(w.radius * 6.2832f / 7.0f));
        for (int k = 0; k < points; ++k) {
            const float a = 6.2832f * float(k) / float(points);
            const float px = w.x + std::cos(a) * w.radius * 1.6f, py = w.y + std::sin(a) * w.radius;
            if (carnage_pair(int(px), int(py)) > 0 && carnage_random() < 120) {
                carve(px, py, 2.6f);
                splash(px, py, 5.0f, rainbow[std::size_t(int(w.radius / 20.0f) % 10)], 140);
            }
        }
    }
    std::erase_if(waves_, [](const Wave& w) { return w.radius > 260.0f; });
}

void Play::wreck() {
    // The wiz at speed rams the aliens it touches and bites into what it
    // bumps.
    const int speed = std::abs(int(wiz_.speed_index()));
    const float wx = float(wiz_.x() + 12), wy = float(wiz_.y() + 11);
    if (dying_ == 0 && speed >= 3)
        if (object_at(int(wx) >> 1, int(wy)) >= 0) impact(1);
    if (dying_ == 0 && wiz_.bumped() && speed >= 2) {
        const float front = wiz_.last_horizontal() >= 0 ? 1.0f : -1.0f;
        carve(wx + front * 11.0f, wy, 3.0f + 0.4f * float(speed));
        splash(wx + front * 11.0f, wy, 6.0f, std::uint8_t(wiz_colour_ & 15), 120);
        impact(speed >= 5 ? 2 : 1);
    }
    // The flail: whatever the catellite's ball strikes in the landscape.
    if (flail_on_ && carnage_pair(int(flail_x_), int(flail_y_)) > 0) {
        const float v = std::sqrt(flail_vx_ * flail_vx_ / 2.56f + flail_vy_ * flail_vy_);
        if (v > 2.0f) {
            carve(flail_x_, flail_y_, 3.5f);
            impact(1);
        }
        flail_vx_ *= -0.4f;
        flail_vy_ *= -0.4f;
    }
}

void Play::carnage_flail() {
    // A catellite that only follows swings on a tether from the wiz instead
    // (gravity, a little drag, the rope's length), smashing aliens it meets
    // (catch_with_catellite: unhurt) and the landscape (wreck()).
    flail_on_ = stage_ == Stage::play && powers_.catellite && catellite_.following() && !catellite_.wandering() &&
                controls_[std::size_t(player_ - 1)] == 1;
    if (!flail_on_) {
        flail_x_ = float(catellite_.x() * 2 + 12);
        flail_y_ = float(catellite_.y() + 11);
        flail_vx_ = flail_vy_ = 0;
        return;
    }
    const float ax = float(wiz_.x() + 12), ay = float(wiz_.y() + 11);
    flail_vy_ += 0.35f;
    flail_vx_ *= 0.985f;
    flail_vy_ *= 0.985f;
    float x = flail_x_ + flail_vx_, y = flail_y_ + flail_vy_;
    // Keep it on the rope (distances in square pixels: x counts 1/1.6).
    float dx = (x - ax) / 1.6f, dy = y - ay;
    const float d = std::sqrt(dx * dx + dy * dy);
    if (d > flail_length) {
        dx *= flail_length / d;
        dy *= flail_length / d;
        x = ax + dx * 1.6f;
        y = ay + dy;
    }
    flail_vx_ = x - flail_x_;
    flail_vy_ = y - flail_y_;
    flail_x_ = x;
    flail_y_ = y;
    catellite_.place_at(std::uint8_t(std::clamp(int(x - 12) / 2, 0, 255)), std::uint8_t(std::clamp(int(y - 11), 0, 255)));
}

// --- the arcade layer ---------------------------------------------------------------------

void Play::banner(const char* top, const char* bottom) {
    // In the score lines' middle, where the Wizlab puts its words.
    if (in_lab_) return;
    auto put = [&](int row, const char* text) {
        auto& line = hud_text_[std::size_t(row)];
        line.fill(0x20);
        const int n = int(std::strlen(text)), at = (16 - n) / 2;
        for (int i = 0; i < n && at + i < 16; ++i) {
            const char c = text[i];
            line[std::size_t(at + i)] = c >= 'A' && c <= 'Z' ? std::uint8_t(c - 'A' + 1) : std::uint8_t(c & 0x3F);
        }
    };
    put(0, top);
    put(1, bottom);
    hud_text_shown_ = true;
    banner_timer_ = 75;
}

void Play::carnage_kills() {
    auto& sc = carnage_score_;
    const int kills = kills_ >= kills_seen_ ? kills_ - kills_seen_ : 0; // (the count starts over at a place's end)
    kills_seen_ = kills_;
    for (int k = 0; k < kills; ++k) {
        sc.combo = sc.combo_timer > 0 ? sc.combo + 1 : 1;
        sc.combo_timer = combo_window;
        if (sc.combo >= 2) {
            add_points(5 * std::min(sc.combo, 10)); // 50 points a kill in the row, up to 500
            char text[16];
            std::snprintf(text, sizeof text, "COMBO X%d", sc.combo);
            banner(text, sc.overdrive ? "OVERDRIVE" : "");
        }
        if (sc.overdrive == 0) {
            sc.meter += meter_per_kill * float(std::min(sc.combo, 4));
            if (sc.meter >= 1.0f) {
                sc.meter = 0;
                sc.overdrive = overdrive_passes;
                banner("OVERDRIVE", "RAINBOW GUN");
                impact(2);
            }
        }
        impact(1);
    }
    if (sc.combo_timer > 0 && --sc.combo_timer == 0) sc.combo = 0;
    if (sc.overdrive > 0) --sc.overdrive;
    if (banner_timer_ > 0 && --banner_timer_ == 0 && !in_lab_) hud_text_shown_ = false;
}

void Play::carnage_flood() {
    // A ring widening from where the wiz was, painting all landscape pairs
    // it passes in bands of colour.
    if (flood_radius_ < 0) {
        flood_radius_ = 0;
        flood_x_ = float(wiz_.x() + 12);
        flood_y_ = float(wiz_.y() + 11);
        tracers_.clear();
        blobs_.clear();
    }
    const float inner = flood_radius_, outer = flood_radius_ + 7.0f;
    flood_radius_ = outer;
    if (inner > 420.0f) return;
    const int xscroll = playfield_.fine_scroll() ^ 7;
    for (int row = 0; row < Playfield::rows; ++row)
        for (int column = 0; column < Playfield::columns; ++column) {
            if (!(playfield_.code(column, row) & 0x80)) continue;
            const float cx = float(24 + xscroll + 8 * column + 4), cy = float(field_top + 8 * row + 4);
            const float d = std::sqrt((cx - flood_x_) * (cx - flood_x_) / 2.56f + (cy - flood_y_) * (cy - flood_y_));
            if (d < inner - 8.0f || d > outer + 8.0f) continue;
            auto& art = carnage_art(column, row);
            for (int y = 0; y < 8; ++y)
                for (int p = 0; p < 4; ++p) {
                    auto& v = art[std::size_t(y * 4 + p)];
                    if (v == 0) continue;
                    const float px = cx - 4.0f + 2.0f * float(p) + 1.0f, py = cy - 4.0f + float(y);
                    const float dp = std::sqrt((px - flood_x_) * (px - flood_x_) / 2.56f + (py - flood_y_) * (py - flood_y_));
                    if (dp < inner || dp >= outer) continue;
                    v = std::uint8_t(0x10 | flood_colours[std::size_t(int(dp / 14.0f) % 10)]);
                }
        }
}

void Play::put_carnage(Display& d, int extension) const {
    // The damaged cells on the screen (and in the side columns), the shots
    // and the paint in flight.
    for (int row = 0; row < Playfield::rows; ++row)
        for (int column = -extension; column < Playfield::columns + extension; ++column) {
            const auto it = damage_.find(playfield_.cell_key(column, row));
            if (it == damage_.end()) continue;
            if (column >= 0 && column < Playfield::columns && !(playfield_.code(column, row) & 0x80)) continue;
            d.set_cell_art(row + 3, column, it->second);
        }
    for (const auto& t : tracers_)
        for (int k = 0; k < 4; ++k) // a short streak back along its flight
            d.dots.push_back({int(t.x - t.dx * float(k) / 9.0f), int(t.y - t.dy * float(k) / 9.0f),
                              std::uint8_t(k == 0 ? 1 : t.colour)});
    for (const auto& b : blobs_) {
        d.dots.push_back({int(b.x), int(b.y), b.colour});
        d.dots.push_back({int(b.x) + 1, int(b.y), b.colour});
    }
    for (const auto& b : bodies_) {
        const float cs = std::cos(b.angle), sn = std::sin(b.angle);
        for (const auto& p : b.pixels) {
            const float dx = float(p.dx) / 1.6f, dy = float(p.dy);
            const int px = int(b.x + (dx * cs - dy * sn) * 1.6f), py = int(b.y + dx * sn + dy * cs);
            const auto c = pair_colour(p.value);
            d.dots.push_back({px, py, c});
            d.dots.push_back({px + 1, py, c});
        }
    }
    for (const auto& w : waves_) {
        const int points = std::max(12, int(w.radius * 6.2832f / 3.0f));
        for (int k = 0; k < points; ++k) {
            const float a = 6.2832f * float(k) / float(points);
            if ((k + int(w.radius)) % 3 == 0) continue;
            d.dots.push_back({int(w.x + std::cos(a) * w.radius * 1.6f), int(w.y + std::sin(a) * w.radius),
                              std::uint8_t(w.radius < 80 ? 1 : w.radius < 160 ? 15 : 12)});
        }
    }
}

} // namespace wizball::game
