// Carnage mode (not the original game; not in the attract game): Bruce's
// blows send the Ninja and the Yamo flying in arcs, and the Yamo's send
// Bruce. A flying enemy keeps the
// game's thrown move, so the game's own rules still apply to it (its pose,
// the deadly cells and exits that terrain_events checks, the removal of an
// enemy out of energy once its thrown move ends), but the flight moves it:
// it bounces off what the actors stand on and hurts on each hard hit,
// smashes the decoration it flies through, knocks over the other enemy, and
// lands when it is slow enough. Decoration that loses what it stood on
// topples and falls as one piece. Blows and hits in quick succession build
// a combo that scores; each enemy beaten brings the next sooner. In room 18
// a blow sends a fireball back up at the wizard's face.
#include "bruce_lee_c64/game/world.hpp"
#include <algorithm>
#include <cmath>

namespace bruce_lee::game {
namespace {

// Bit 7: an 8-bit value below zero (energy run out).
bool negative(std::uint8_t v) {
    return v & 0x80;
}

constexpr double gravity = 0.22, fall_speed = 6;   // lines per frame (squared)
constexpr double bounce_speed = 2.4;               // a landing faster than this bounces
constexpr int combo_window = 150;                  // frames between hits that keep a combo
constexpr std::size_t kept_events = 64;
constexpr std::uint32_t points_combo = 50;         // times the combo, per hit from the second
constexpr int combo_cap = 10;                      // (waves keep a combo going: its points stop growing)
constexpr double smash_speed = 2.2;                // a body slower than this breaks nothing
constexpr int object_cells = 4;                    // decoration smaller than this is a wall's pattern
constexpr std::uint32_t points_carnage_down = 200; // an enemy a hit (not a blow) finished
constexpr std::uint32_t points_face = 1000;        // a fireball sent back into the face
constexpr int chunk_limit = 80;                    // cells joined: part of something bigger, it holds
constexpr int falling_limit = 40;                  // cells: the biggest piece that falls
constexpr double chunk_gravity = 0.3;
// Room 18: where a fireball sent back flies to (the face, printed from
// column $10 of row 2; sprite units) and how fast it climbs.
constexpr double face_x = 0x58, face_y = 0x37, returned_climb = 5;

// A body's place in the picture from the actor's: its sprite is 16 pixels
// wide (x-expanded) from 2x - 24, 21 lines from y - 21 (see Effects).
double body_x(double x) {
    return 2 * x - 16;
}
double feet_y(double y) {
    return y - 1;
}
constexpr double body_height = 20, body_half_width = 7;

// Cells a flight never breaks: exits, lanterns and their flames, the deadly
// ones, crumbling floors, the lift, room 16's extra fall.
bool kept_code(std::uint8_t code) {
    const std::uint8_t c = code & 0x7F;
    return (c >= 2 && c <= 5) || c == 0x13 || c == 0x14 || c == 0x31 || c == 0x06 || c == 0x67 ||
           c == 0x64 || c == 0x0F || c == 0x40 || (c >= 0x54 && c < 0x5B) || (c >= 0x71 && c < 0x7D) ||
           c == 0x66 || c == 0x60 || c == 0x62 || c == 0x2D || c == 0x2E;
}

} // namespace

std::uint8_t World::carnage_random() {
    auto& s = carnage.seed;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return std::uint8_t(s >> 24);
}

void World::carnage_event(Carnage::Event e) {
    e.id = carnage.next_id++;
    carnage.events.push_back(e);
    if (carnage.events.size() > kept_events) {
        carnage.events.erase(carnage.events.begin(), carnage.events.end() - kept_events);
    }
}

// One more hit in the run: from the second on it scores, more each time.
void World::carnage_hit(int count) {
    carnage.combo += count;
    carnage.combo_frames = 0;
    if (carnage.combo >= 2) {
        add_score(points_combo * std::uint32_t(std::min(carnage.combo, combo_cap)));
    }
}

// What the actors stand on (pixel pair 01, the terrain probe's ground), at
// a picture pixel of the playfield.
bool World::carnage_solid(int px, int py, bool) const {
    if (px < 0 || px >= Playfield::columns * 8 || py < 24 || py >= Playfield::rows * 8) {
        return false;
    }
    const auto& cell = field.at(px / 8, py / 8);
    if (!(cell.colour & 8)) {
        return false;
    }
    const std::uint8_t bits = charsets[std::size_t(shown_charset)][cell.code][std::size_t(py & 7)];
    return ((bits >> (6 - 2 * ((px & 7) / 2))) & 3) == 1;
}

// A blow by Bruce that landed ($2B3B has set the move): the victim flies
// away from him, harder for a kick than a chop or a low strike, and harder
// still once its energy is gone.
void World::carnage_launch(int slot, int attacker, std::uint8_t hit) {
    Actor& a = actors.slots[std::size_t(slot)];
    const Actor& by = actors.slots[std::size_t(attacker)];
    const int strength = (hit & 8) ? 3 : (hit & 4) ? 2 : 1;
    constexpr double speeds[] = {0, 1.1, 1.6, 2.2}, lifts[] = {0, 1.6, 2.4, 3.0};
    const double out = negative(a.d1) ? 1.3 : 1.0;
    const double spread = 0.9 + carnage_random() / 255.0 * 0.2;
    auto& f = carnage.flights[std::size_t(slot)];
    const bool juggle = f.active;
    f.active = true;
    f.x = a.x;
    f.y = a.y;
    f.vx = (by.facing == 1 ? 1 : -1) * speeds[strength] * out * spread;
    f.vy = -lifts[strength] * spread;
    f.age = 0;
    a.move = move::thrown;
    a.step = 0x0C; // (a thrown move still running: not taken away yet)
    carnage_event({Carnage::Event::Kind::launch, 0, body_x(a.x), feet_y(a.y) - 10, juggle ? 3 : strength, 0, {}, f.vx});
    if (slot == 0) {
        return; // (Bruce sent flying scores nothing)
    }
    carnage_hit(juggle ? 2 : 1);
    if (carnage.combo >= 2) {
        carnage_event({Carnage::Event::Kind::combo, 0, body_x(a.x), feet_y(a.y) - 24, carnage.combo, 0, {}, 0});
    }
}

void World::carnage_frame() {
    if (++carnage.combo_frames > combo_window) {
        carnage.combo = 0;
    }
    for (int slot = 0; slot < 3; ++slot) {
        auto& f = carnage.flights[std::size_t(slot)];
        if (!f.active) {
            continue;
        }
        const Actor& a = actors.slots[std::size_t(slot)];
        if (!a.present() || a.move != move::thrown) {
            f.active = false; // (taken away: a deadly cell, an exit; Bruce dying)
            continue;
        }
        carnage_fly(slot);
    }
    carnage_chunks();
    carnage_returned();
}

void World::carnage_fly(int slot) {
    Actor& a = actors.slots[std::size_t(slot)];
    auto& f = carnage.flights[std::size_t(slot)];
    ++f.age;
    // A hard hit hurts (its energy can run out: then it is taken away as
    // it lands, as the game takes away a thrown enemy out of energy).
    auto impact = [&](double speed) {
        if (speed < 1.6) {
            return;
        }
        const bool was_out = negative(a.d1);
        a.d1 = std::uint8_t(a.d1 - int(speed * (slot == 0 ? 0.75 : 1.5)));
        if (slot != 0 && !was_out && negative(a.d1)) {
            add_score(points_carnage_down);
            carnage_hit(1);
        }
        const int strength = speed > 4 ? 3 : speed > 2.6 ? 2 : 1;
        carnage_event({Carnage::Event::Kind::impact, 0, body_x(f.x), feet_y(f.y) - 8, strength, 0, {}, f.vx});
    };
    // Sideways: the edges of the playfield and the sides of what the actors
    // stand on turn it back.
    const double nx = f.x + f.vx;
    const double front = body_x(nx) + (f.vx > 0 ? body_half_width : -body_half_width);
    bool wall = nx < 0x0E || nx > 0xA2;
    for (double py = feet_y(f.y) - body_height + 4; !wall && py < feet_y(f.y) - 3; py += 2) {
        wall = carnage_solid(int(front), int(py), true);
    }
    if (wall) {
        impact(std::abs(f.vx) * 1.6);
        f.vx = -f.vx * 0.45;
    } else {
        f.x = nx;
    }
    // Up and down: a ceiling stops the climb; a floor bounces it, or it
    // lands when slow enough.
    f.vy = std::min(f.vy + gravity, fall_speed);
    if (f.vy < 0) {
        const double head = feet_y(f.y) - body_height + f.vy;
        if (carnage_solid(int(body_x(f.x)), int(head), false)) {
            f.vy = 0;
        } else {
            f.y += f.vy;
        }
    } else {
        bool landed = false;
        double line = feet_y(f.y);
        for (double step = 1; step <= std::ceil(f.vy) && !landed; ++step) {
            line = feet_y(f.y) + std::min(step, f.vy);
            for (double dx = -4; dx <= 4 && !landed; dx += 2) {
                landed = carnage_solid(int(body_x(f.x) + dx), int(line), false) || line >= Playfield::rows * 8 - 1;
            }
        }
        if (landed) {
            f.y = std::floor(line); // feet just above the floor
            if (f.vy > bounce_speed) {
                impact(f.vy);
                f.vy = -f.vy * 0.38;
                f.vx *= 0.6;
            } else {
                // Down: the thrown move ends, and the game goes on with it
                // (or takes it away, out of energy).
                f.active = false;
                a.step = 0;
            }
        } else {
            f.y += f.vy;
        }
    }
    a.x = std::uint8_t(std::clamp(std::lround(f.x), 0L, 255L));
    a.y = std::uint8_t(std::clamp(std::lround(f.y), 0L, 255L));
    carnage_smash(slot);
    // Into an enemy (Bruce flying, either of them): it goes flying too.
    for (int other = 1; other < 3; ++other) {
        if (other == slot) {
            continue;
        }
        Actor& o = actors.slots[std::size_t(other)];
        auto& of = carnage.flights[std::size_t(other)];
        if (!f.active || !o.present() || of.active || std::abs(f.vx) + std::abs(f.vy) <= 1.5 ||
            std::abs(2 * (int(a.x) - int(o.x))) >= 16 || std::abs(int(a.y) - int(o.y)) >= 18) {
            continue;
        }
        of = {true, double(o.x), double(o.y), f.vx * 0.8, -2.2, 0};
        o.move = move::thrown;
        o.step = 0x0C;
        o.d1 = std::uint8_t(o.d1 - 4);
        f.vx *= 0.5;
        carnage_hit(2);
        carnage_event({Carnage::Event::Kind::knock, 0, body_x(o.x), feet_y(o.y) - 10, 3, 0, {}, of.vx});
        carnage_event({Carnage::Event::Kind::combo, 0, body_x(o.x), feet_y(o.y) - 24, carnage.combo, 0, {}, 0});
    }
}

// Decoration: a cell drawn only in the background and pixel pair 10,
// which no actor rule tests (the terrain probe's flag for pair 10 is never
// read), so nothing the game plays on.
bool World::carnage_decor(const Cell& cell) const {
    if (!(cell.colour & 8) || kept_code(cell.code)) {
        return false;
    }
    bool decor = false;
    for (auto bits : charsets[std::size_t(shown_charset)][cell.code]) {
        for (int p = 0; p < 4; ++p) {
            const int pair = (bits >> (2 * p)) & 3;
            if (pair == 1 || pair == 3) {
                return false;
            }
            decor |= pair == 2;
        }
    }
    return decor;
}

// An empty glyph of the room's charset, to put where decoration went.
int World::carnage_empty_code() const {
    const auto& charset = charsets[std::size_t(shown_charset)];
    for (int code = 0; code < 256; ++code) {
        if (!kept_code(std::uint8_t(code)) &&
            std::all_of(charset[std::size_t(code)].begin(), charset[std::size_t(code)].end(), [](auto b) { return b == 0; })) {
            return code;
        }
    }
    return -1;
}

// Decoration a fast body flies through breaks off, if it is an object (a
// piece of decoration from 4 to 80 cells: smaller ones are the bits of a
// wall's pattern, bigger ones a wall); what stood on it may fall.
void World::carnage_smash(int slot) {
    const auto& f = carnage.flights[std::size_t(slot)];
    if (std::abs(f.vx) + std::abs(f.vy) < smash_speed) {
        return;
    }
    const int empty = carnage_empty_code();
    if (empty < 0) {
        return;
    }
    const auto& charset = charsets[std::size_t(shown_charset)];
    const double left = body_x(f.x) - body_half_width, right = body_x(f.x) + body_half_width;
    const double top = feet_y(f.y) - body_height, bottom = feet_y(f.y);
    std::vector<std::pair<int, int>> broken;
    // The decoration joined to a cell: at most chunk_limit cells.
    auto small_decor = [&](int column, int row) {
        std::vector<std::pair<int, int>> joined{{column, row}};
        for (std::size_t i = 0; i < joined.size(); ++i) {
            const auto [c, r] = joined[i];
            for (const auto [dc, dr] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
                const int nc = c + dc, nr = r + dr;
                if (nc < 0 || nc >= Playfield::columns || nr < 3 || nr >= Playfield::rows ||
                    std::find(joined.begin(), joined.end(), std::pair{nc, nr}) != joined.end() ||
                    !carnage_decor(field.at(nc, nr))) {
                    continue;
                }
                joined.push_back({nc, nr});
                if (int(joined.size()) > chunk_limit) {
                    return false;
                }
            }
        }
        return int(joined.size()) >= object_cells;
    };
    for (int row = std::max(3, int(top) / 8); row <= std::min(Playfield::rows - 1, int(bottom) / 8); ++row) {
        for (int column = std::max(0, int(left) / 8); column <= std::min(Playfield::columns - 1, int(right) / 8);
             ++column) {
            auto& cell = field.at(column, row);
            if (!carnage_decor(cell) || !small_decor(column, row)) {
                continue;
            }
            carnage_event({Carnage::Event::Kind::smash, 0, column * 8.0 + 4, row * 8.0 + 4, column, row,
                           charset[cell.code], f.vx});
            cell.code = std::uint8_t(empty);
            broken.push_back({column, row});
        }
    }
    for (const auto& [column, row] : broken) {
        carnage_topple(column, row - 1);
    }
}

// What is joined to this cell (side by side or one on the other: drawn
// cells), if it rested on decoration that broke, nothing holds it any more
// (nothing drawn under it), it is small (a statue on its slab, not a floor)
// and nothing the game keeps (a lantern, an exit, a hazard) touches it,
// comes loose as one piece and falls.
void World::carnage_topple(int column, int row) {
    const auto& charset = charsets[std::size_t(shown_charset)];
    auto drawn = [&](const Cell& cell) {
        const auto& g = charset[cell.code];
        return !std::all_of(g.begin(), g.end(), [](auto b) { return b == 0; });
    };
    auto joinable = [&](const Cell& cell) { return drawn(cell) && !kept_code(cell.code) && (cell.colour & 8); };
    if (row < 3 || !joinable(field.at(column, row))) {
        return;
    }
    std::vector<std::pair<int, int>> piece{{column, row}};
    auto in_piece = [&](int c, int r) {
        return std::find(piece.begin(), piece.end(), std::pair{c, r}) != piece.end();
    };
    for (std::size_t i = 0; i < piece.size(); ++i) {
        const auto [c, r] = piece[i];
        for (const auto [dc, dr] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
            const int nc = c + dc, nr = r + dr;
            if (nc < 0 || nc >= Playfield::columns || nr < 3 || nr >= Playfield::rows || in_piece(nc, nr) ||
                !joinable(field.at(nc, nr))) {
                continue;
            }
            piece.push_back({nc, nr});
            if (int(piece.size()) > chunk_limit) {
                return; // part of something big: it holds
            }
        }
    }
    if (int(piece.size()) > falling_limit) {
        return; // too big: it holds
    }
    for (const auto& [c, r] : piece) {
        if (r + 1 >= Playfield::rows) {
            return; // on the bottom row
        }
        const auto& below = field.at(c, r + 1);
        if (!in_piece(c, r + 1) && drawn(below)) {
            return; // standing on something
        }
        for (const auto [dc, dr] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
            const int nc = c + dc, nr = r + dr;
            if (nc >= 0 && nc < Playfield::columns && nr >= 0 && nr < Playfield::rows &&
                kept_code(field.at(nc, nr).code) && drawn(field.at(nc, nr))) {
                return; // something the game keeps holds it
            }
        }
    }
    const int empty = carnage_empty_code();
    Carnage::Chunk chunk;
    chunk.id = carnage.next_chunk++;
    int left = Playfield::columns, top = Playfield::rows, right = 0, bottom = 0;
    for (const auto& [c, r] : piece) {
        left = std::min(left, c);
        right = std::max(right, c);
        top = std::min(top, r);
        bottom = std::max(bottom, r);
    }
    chunk.x = left * 8;
    chunk.y = top * 8;
    chunk.columns = right - left + 1;
    chunk.rows = bottom - top + 1;
    for (const auto& [c, r] : piece) {
        chunk.cells.push_back({c - left, r - top, charset[field.at(c, r).code]});
    }
    carnage.chunks.push_back(chunk);
    // (The event first: its picture is taken while the piece still shows.)
    carnage_event({Carnage::Event::Kind::chunk, 0, chunk.x, chunk.y, int(chunk.id), 0, {}, 0});
    for (const auto& [c, r] : piece) {
        field.at(c, r).code = std::uint8_t(empty);
    }
}

// The falling pieces: down until they reach what the actors stand on,
// knocking over whoever they fall on; there they burst.
void World::carnage_chunks() {
    for (auto it = carnage.chunks.begin(); it != carnage.chunks.end();) {
        auto& c = *it;
        c.vy = std::min(c.vy + chunk_gravity, fall_speed);
        bool landed = false;
        double y = c.y;
        for (double step = 1; step <= std::ceil(c.vy) && !landed; ++step) {
            y = c.y + std::min(step, c.vy);
            for (const auto& cell : c.cells) {
                const double bottom = y + (cell.row + 1) * 8;
                for (int dx = 1; dx < 8 && !landed; dx += 2) {
                    landed = carnage_solid(int(c.x) + cell.column * 8 + dx, int(bottom), false) ||
                             bottom >= Playfield::rows * 8;
                }
            }
        }
        c.y = std::floor(y);
        // Whoever it falls on.
        for (int slot = 0; slot < 3; ++slot) {
            Actor& a = actors.slots[std::size_t(slot)];
            auto& f = carnage.flights[std::size_t(slot)];
            const double ax = body_x(a.x), ay = feet_y(a.y) - body_height / 2;
            if (!a.present() || f.active || a.move == move::dying || ax < c.x - 4 || ax > c.x + c.columns * 8 + 4 ||
                ay < c.y || ay > c.y + c.rows * 8 + 6) {
                continue;
            }
            if (slot != 0) {
                a.d1 = std::uint8_t(a.d1 - 10);
                carnage_hit(2);
            }
            f = {true, double(a.x), double(a.y), ax < c.x + c.columns * 4 ? -1.2 : 1.2, -1.4, 0};
            a.move = move::thrown;
            a.step = 0x0C;
            carnage_event({Carnage::Event::Kind::knock, 0, ax, ay, 3, 0, {}, f.vx});
        }
        if (landed) {
            carnage_event({Carnage::Event::Kind::landed, 0, c.x + c.columns * 4, c.y + c.rows * 8, int(c.id), 0, {}, 0});
            carnage_event({Carnage::Event::Kind::impact, 0, c.x + c.columns * 4, c.y + c.rows * 8 - 4, 3, 0, {}, 0});
            it = carnage.chunks.erase(it);
        } else {
            ++it;
        }
    }
}

// Room 18: a blow that meets a falling fireball sends it back up at the
// face (it is out of the game's hands: the game's own fireball waits).
void World::carnage_fireballs() {
    const Actor& b = actors.slots[0];
    const bool striking = ((b.move == move::kick_left || b.move == move::kick_right) && b.step < 0x0A) ||
                          b.move == move::chop || (b.move == move::low_strike && b.step < 8);
    if (!b.present() || !striking) {
        return;
    }
    for (std::size_t i = 0; i < fireballs.size(); ++i) {
        Fireball& f = fireballs[i];
        const int dx = int(f.x) - int(b.x);
        const bool in_reach = b.facing == 1 ? dx >= 0 && dx <= 16 : dx >= -12 && dx <= 4;
        const int dy = int(f.y) - int(b.y);
        if (!f.active || !in_reach || dy < -6 || dy > 20) {
            continue;
        }
        auto& r = carnage.returned[i];
        const double frames = std::max(1.0, (f.y - face_y) / returned_climb);
        r = {true, double(f.x), double(f.y), (face_x - f.x) / frames, -returned_climb, f.shape, f.colour};
        f.active = false;
        f.wait = 12; // (the game's next one waits while it flies)
        collision_latch &= std::uint8_t(~(0x40 << i)); // it no longer touches Bruce
        carnage_hit(1);
        carnage_event({Carnage::Event::Kind::returned, 0, 2.0 * f.x - 16, f.y + 0x1D - 51 + 2.0, 2, 0, {}, 0});
    }
}

void World::carnage_returned() {
    for (auto& r : carnage.returned) {
        if (!r.active) {
            continue;
        }
        r.x += r.vx;
        r.y += r.vy;
        if (r.y <= face_y) {
            r.active = false;
            add_score(points_face);
            carnage_hit(1);
            carnage_event({Carnage::Event::Kind::face, 0, 2 * r.x - 16, r.y + 0x1D - 51 + 2.0, 3, 0, {}, 0});
        }
    }
}

// An enemy beaten: the next of either comes sooner, more so with each.
void World::carnage_beaten(int) {
    ++carnage.wave;
    const int shift = std::min(carnage.wave, 3);
    ninja_timer = std::uint8_t(std::max(3, ninja_timer >> shift));
    yamo_timer = std::uint8_t(std::max(3, yamo_timer >> shift));
    if (carnage.wave >= 2) {
        carnage_event({Carnage::Event::Kind::wave, 0, 160, 60, carnage.wave, 0, {}, 0});
    }
}

} // namespace bruce_lee::game
