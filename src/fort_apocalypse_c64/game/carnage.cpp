// Carnage mode (not the original game): fire is a rapid-fire gun whose
// tracers carve the brown rock pixel by pixel. A damaged cell keeps its own
// copy of its character's pixels, which everything that draws or tests the
// cell uses (the picture and so the VIC collisions, the craft's and the
// enemy's cell probes, the effects), so a carved tunnel is really open.
// Other things the tracers hit go as the original's shots make them go.

#include "fort_apocalypse_c64/game/game.hpp"
#include <cmath>
#include <initializer_list>

namespace fort_apocalypse::game {

namespace {

// The gun: a tracer every few frames while fire is held, faster than the
// original's shot (4 game units, 8 pixels a frame).
constexpr int gun_period = 3;
constexpr double tracer_speed = 14, tracer_drop_speed = 11;
constexpr int tracer_life = 45;   // frames
constexpr double crater = 5;       // pixels, the bite of a tracer
// A carved cell with this few pixels left crumbles away (a speck of rock
// would block the craft, which tests whole cells).
constexpr int crumble_pixels = 6;
// Loose rock: a piece of carvable rock around a crater that reaches nothing
// fixed within this many pixel pairs comes loose; a bigger mass holds.
constexpr int island_limit = 6000;
// Falling things: pixels per frame squared, and at most per frame.
constexpr double fall_gravity = 0.14, fall_speed = 6;
// Hits that shake a laser emitter off its frame.
constexpr int emitter_strength = 6;
constexpr int pair_columns = 256 * 4, map_lines = 40 * 8;
// Towers: a hit at its top pushes this hard (radians per frame); gravity
// rights it, the air damps it; past `tip_angle` it goes over.
constexpr double tower_push = 0.005, tower_spring = 0.006, tower_damping = 0.05;
constexpr double tip_angle = 0.16;
constexpr double half_turn = 3.14159265358979 / 2;
bool tower_code(u8 code) { return code >= 0x54 && code <= 0x58; }
// The map's first 40 columns repeat from column 215 (the wrap copy): what
// happens to one copy happens to the other.
constexpr int wrap_columns = 40, wrap_offset = 215;

} // namespace

u8 Game::carnage_random() {
    carnage_seed_ ^= carnage_seed_ << 13;
    carnage_seed_ ^= carnage_seed_ >> 17;
    carnage_seed_ ^= carnage_seed_ << 5;
    return u8(carnage_seed_ >> 24);
}

const c64::Glyph& Game::cell_glyph_at(int index, u8 code) const {
    if (!carved_.empty()) {
        const auto it = carved_.find(index);
        // (Also a fallen tower painted where the screen copy, which lags,
        // still has open air, or the tower where it stood.)
        if (it != carved_.end() &&
            (it->second.code == code || (it->second.painted && (code == 0 || (code >= 0x54 && code <= 0x58))))) {
            return it->second.glyph;
        }
    }
    return glyphs_[code & 0x7F];
}

bool Game::solid_cell(int index) const {
    for (auto b : cell_glyph_at(index, map_at(index))) {
        if (b) {
            return true;
        }
    }
    return false;
}

const c64::Glyph& Game::shown_wide_glyph(int screen_column, int screen_row) const {
    int column = shown_window_.column + screen_column;
    if (column < 0 || column > 255) {
        column = (column % wrap_offset + wrap_offset) % wrap_offset; // as copy_window
    }
    const int index = (shown_window_.row + screen_row - window_first_row) * 256 + column;
    return cell_glyph_at(index, shown_wide_code(screen_column, screen_row));
}

std::array<double, 2> Game::world_from_screen(int x, int y) const {
    // As bullets() finds a shot's cell ($A33F-$A35F), in pixels.
    const int a = std::int8_t(camera_row_) < 0 ? 0 : (~fine_y_ & 7);
    const int top = std::int8_t(camera_row_) < 0 ? 0 : camera_row_;
    return {double((x - 0x30) * 2 + (camera_column_ - 1) * 8), double(top * 8 + a + y - 0x66)};
}

void Game::carnage_fire() {
    if (gun_cooldown_ > 0) {
        --gun_cooldown_;
    }
    if (!fire_) {
        fire_latch_ = 1;
        return;
    }
    if (fire_latch_) {
        fire_latch_ = 0;
        sweep_direction_ ^= 0xFE; // a press turns the sweeping walls, as a shot does
    }
    if (gun_cooldown_ > 0) {
        return;
    }
    const bool overdrive = carnage_state_.overdrive > 0;
    gun_cooldown_ = overdrive ? 1 : gun_period;
    // The directions of the original's shot (bullet_dx/dy): left, right,
    // the two downward diagonals at its slope, straight down.
    Tracer t;
    t.direction = bank_direction(bank_);
    const double sideways = tables_.bullet_dx[t.direction] < 0 ? -1 : tables_.bullet_dx[t.direction] > 0 ? 1 : 0;
    if (sideways == 0) {
        t.dy = tracer_drop_speed;
    } else {
        t.dx = sideways * tracer_speed;
        t.dy = tables_.bullet_dy[t.direction] != 0 ? tracer_speed / 4 : 0;
    }
    const auto at = world_from_screen((x_ & 3) + x_ + 6, y_ + 8);
    t.x = at[0];
    t.y = at[1];
    // A little spread; in overdrive, a fan of three.
    for (double fan : overdrive ? std::initializer_list<double>{-0.14, 0, 0.14} : std::initializer_list<double>{0}) {
        const double turn = fan + (carnage_random() / 255.0 - 0.5) * 0.09;
        const double c = std::cos(turn), s = std::sin(turn);
        Tracer one = t;
        one.dx = t.dx * c - t.dy * s;
        one.dy = t.dx * s + t.dy * c;
        tracers_.push_back(one);
    }
    shot_sound_ = 0x20;
    sound_shot(0x81);
}

void Game::move_tracers() {
    // The view's edges, as bullets() ends a shot at the window's, and
    // wider with the widescreen view: what carnage the gun does is seen.
    const auto top_left = world_from_screen(4, 0x64), bottom_right = world_from_screen(0xFB, 0xE6);
    const double left = top_left[0] - options_.wide_view * 8, right = bottom_right[0] + options_.wide_view * 8;
    for (auto& t : tracers_) {
        ++t.age;
        if (t.x < left || t.x >= right || t.y < top_left[1] || t.y >= bottom_right[1]) {
            t.age = tracer_life; // out of sight
            continue;
        }
        // In steps of at most two pixels: nothing thin is skipped.
        const int steps = std::max(1, int(std::ceil(std::max(std::abs(t.dx), std::abs(t.dy)) / 2)));
        for (int k = 0; k < steps; ++k) {
            t.x += t.dx / steps;
            t.y += t.dy / steps;
            if (tracer_hit(t, int(std::floor(t.x)), int(std::floor(t.y)))) {
                t.age = tracer_life; // spent
                break;
            }
        }
    }
    std::erase_if(tracers_, [](const Tracer& t) { return t.age >= tracer_life; });
}

bool Game::tracer_hit(Tracer& t, int x, int y) {
    if (x < 0 || x >= 256 * 8 || y < 0 || y >= 40 * 8) {
        return true;
    }
    // The enemy helicopter, where its sprite is.
    if (enemy_.sprite == 2 && enemy_.state == 3) {
        const auto centre = world_from_screen(enemy_.x + 12, enemy_.y + 10);
        if (std::abs(x - centre[0]) < 22 && std::abs(y - centre[1]) < 9) {
            enemy_shot_down_ = true;
            return true;
        }
    }
    const int column = x >> 3, row = y >> 3, index = row * 256 + column;
    const u8 code = map_at(index);
    if (code == 0) {
        return false;
    }
    const auto& glyph = cell_glyph_at(index, code);
    if (((glyph[std::size_t(y & 7)] >> (6 - (x & 6))) & 3) == 0) {
        return false; // between the character's pixels
    }
    const auto& men = tables_.prisoner_characters;
    if (std::find(men.begin(), men.end(), code) != men.end() || code == 0x20) {
        return false; // the men are spared; explosions are not solid
    }
    if (carvable(code)) {
        carve(x, y, crater, t);
        return true;
    }
    // Anything else, as the original's shot hits it ($A36B): the fort's
    // core, the breakable rock ($47), and the objects, which die of the
    // explosion character in their cells; other solids just stop it.
    happenings_.impacts.push_back({{column, row}, t.direction, code, true});
    if (const int depot = depot_at(column, row); depot >= 0 && carnage()) {
        damage_depot(depot, 1); // it burns, and at last it blows
    } else if (tower_code(code) && carnage()) {
        // A tower: the hit rocks it, the higher the harder.
        if (Tower* tower = tower_at(column, row)) {
            push_tower(*tower, x, y, t.dx);
        }
    } else if (code >= 0x4C && code <= 0x4F) {
        // A laser barrier's emitter: enough hits shake it loose.
        if (++emitter_hits_[index] >= emitter_strength) {
            break_emitter(index);
        }
    } else if (code == 0x3F && level_ == 1) {
        fort_ = 5;
    } else if (code == 0x47) {
        add_score(0x20, 0x00);
        map_set(index, 0);
        if (column < wrap_columns) {
            map_set(index + wrap_offset, 0);
        } else if (column >= wrap_offset && column < wrap_offset + wrap_columns) {
            map_set(index - wrap_offset, 0);
        }
        sound_explosion();
    } else {
        for (int k = 0x12; k >= 0; --k) {
            if (code == tables_.obstacles[std::size_t(k)]) {
                map_set(index, 0x20);
                sound_explosion();
                break;
            }
        }
    }
    return true;
}

void Game::carve(int x, int y, double radius, const Tracer& t) {
    craters_.push_back({x, y});
    const int reach = int(std::ceil(radius));
    const std::size_t first = happenings_.carved.size();
    for (int row = (y - reach) >> 3; row <= (y + reach) >> 3; ++row) {
        for (int column = (x - reach) >> 3; column <= (x + reach) >> 3; ++column) {
            if (row < 0 || row >= 40 || column < 0 || column > 255) {
                continue;
            }
            const int index = row * 256 + column;
            carve_cell(index, x, y, radius, t, true);
            if (first < happenings_.carved.size()) {
                happenings_.carved[first].bite = true;
            }
            if (column < wrap_columns) {
                carve_cell(index + wrap_offset, x + wrap_offset * 8, y, radius, t, false);
            } else if (column >= wrap_offset && column < wrap_offset + wrap_columns) {
                carve_cell(index - wrap_offset, x - wrap_offset * 8, y, radius, t, false);
            }
        }
    }
}

void Game::carve_cell(int index, int x, int y, double radius, const Tracer& t, bool report) {
    const u8 code = map_at(index);
    if (!carvable(code)) {
        return;
    }
    auto it = carved_.find(index);
    if (it == carved_.end() || it->second.code != code) {
        it = carved_.insert_or_assign(index, CarvedCell{code, glyphs_[code & 0x7F]}).first;
    }
    auto& glyph = it->second.glyph;
    const int left = (index % 256) * 8, top = (index / 256) * 8;
    int left_over = 0;
    auto remove = [&](int line, int p, bool always) {
        const int shift = 6 - 2 * p;
        const u8 pair = u8((glyph[std::size_t(line)] >> shift) & 3);
        if (!pair) {
            return;
        }
        const double px = left + p * 2 + 1 - x, py = top + line + 0.5 - y;
        if (always || px * px + py * py <= radius * radius) {
            glyph[std::size_t(line)] = u8(glyph[std::size_t(line)] & ~(3 << shift));
            if (report) {
                happenings_.carved.push_back({left + p * 2, top + line, pair, t.dx, t.dy});
            }
        } else {
            ++left_over;
        }
    };
    for (int line = 0; line < 8; ++line) {
        for (int p = 0; p < 4; ++p) {
            remove(line, p, false);
        }
    }
    if (left_over > 0 && left_over <= crumble_pixels) {
        for (int line = 0; line < 8; ++line) {
            for (int p = 0; p < 4; ++p) {
                remove(line, p, true); // it crumbles
            }
        }
        left_over = 0;
    }
    if (left_over == 0) {
        // All gone: open air, as the game sees it too. (Its record stays,
        // empty, so the screen copy, which lags, shows it gone as well.)
        map_set(index, 0);
    }
}

// --- things that come loose and fall ------------------------------------------

namespace {

// Pixel pair `pair_x` (0-3 within the cell) of a glyph line.
int pair_of(const c64::Glyph& glyph, int y, int pair_x) {
    return (glyph[std::size_t(y & 7)] >> (6 - 2 * (pair_x & 3))) & 3;
}

} // namespace

Game::Ground Game::ground_at(int pair_x, int y) const {
    if (pair_x < 0 || pair_x >= pair_columns || y < 0 || y >= map_lines) {
        return Ground::fixed; // the map's edges hold
    }
    const int index = (y >> 3) * 256 + (pair_x >> 2);
    const u8 code = map_at(index);
    if (code == 0 || pair_of(cell_glyph_at(index, code), y, pair_x) == 0) {
        return Ground::open;
    }
    if (carvable(code)) {
        return Ground::rock;
    }
    // Beams, lasers and walls of light, explosions and the objects (mines,
    // the men, tanks, turrets, missiles) are not part of the place.
    if (code <= 0x0D || code == 0x20 || code == 0x72) {
        return Ground::open;
    }
    for (int k = 0x12; k >= 0; --k) {
        if (code == tables_.obstacles[std::size_t(k)]) {
            return Ground::open;
        }
    }
    return Ground::fixed;
}

bool Game::structural_cell(int index) const {
    const u8 code = map_at(index);
    if (code == 0 || carvable(code) || code <= 0x0D || code == 0x20 || code == 0x72) {
        return false;
    }
    for (int k = 0x12; k >= 0; --k) {
        if (code == tables_.obstacles[std::size_t(k)]) {
            return false;
        }
    }
    for (auto b : cell_glyph_at(index, code)) {
        if (b) {
            return true;
        }
    }
    return false;
}

bool Game::home_has_ground(int home_column, int home_row) const {
    // The top lines of the row under the three cells it appears in.
    const int column = home_column - 5; // object_index's column
    for (int px = column * 4; px < column * 4 + 12; ++px) {
        for (int y = (home_row + 1) * 8; y < (home_row + 1) * 8 + 3; ++y) {
            if (ground_at(px, y) != Ground::open) {
                return true;
            }
        }
    }
    return false;
}

void Game::set_cell_both(int index, u8 code) {
    auto set = [&](int at) {
        map_set(at, code);
        if (const auto it = carved_.find(at); it != carved_.end() && code == 0) {
            it->second.glyph = {}; // (an empty cell never shows or holds old pixels)
        }
    };
    set(index);
    const int column = index % 256;
    if (column < wrap_columns) {
        set(index + wrap_offset);
    } else if (column >= wrap_offset && column < wrap_offset + wrap_columns) {
        set(index - wrap_offset);
    }
}

void Game::clear_pair(int pair_x, int y) {
    auto clear = [&](int index) {
        const u8 code = map_at(index);
        if (!carvable(code)) {
            return;
        }
        auto it = carved_.find(index);
        if (it == carved_.end() || it->second.code != code) {
            it = carved_.insert_or_assign(index, CarvedCell{code, glyphs_[code & 0x7F]}).first;
        }
        auto& line = it->second.glyph[std::size_t(y & 7)];
        line = u8(line & ~(3 << (6 - 2 * (pair_x & 3))));
        for (auto b : it->second.glyph) {
            if (b) {
                return;
            }
        }
        map_set(index, 0); // (its record stays, empty: see carve_cell)
    };
    const int column = pair_x >> 2, index = (y >> 3) * 256 + column;
    clear(index);
    if (column < wrap_columns) {
        clear(index + wrap_offset);
    } else if (column >= wrap_offset && column < wrap_offset + wrap_columns) {
        clear(index - wrap_offset);
    }
}

void Game::hit_object(int index) {
    const u8 code = map_at(index);
    const auto& men = tables_.prisoner_characters;
    if (std::find(men.begin(), men.end(), code) != men.end()) {
        return; // the men are spared
    }
    if (code == 0x47) {
        add_score(0x20, 0x00);
        set_cell_both(index, 0);
        return;
    }
    if (code >= 0x4C && code <= 0x4F) {
        emitter_hits_[index] += emitter_strength / 2; // a blast shakes it hard
        if (emitter_hits_[index] >= emitter_strength) {
            break_emitter(index);
        }
        return;
    }
    for (int k = 0x12; k >= 0; --k) {
        if (code == tables_.obstacles[std::size_t(k)]) {
            map_set(index, 0x20); // as a shot: the object dies of it
            return;
        }
    }
}

void Game::blast(int x, int y, double radius) {
    Tracer t; // no direction: the debris flies every way
    carve(x, y, radius, t);
    const int reach = int(std::ceil(radius));
    std::vector<int> depots; // a blast shakes a depot once, hard
    for (int row = (y - reach) >> 3; row <= (y + reach) >> 3; ++row) {
        for (int column = (x - reach) >> 3; column <= (x + reach) >> 3; ++column) {
            if (row >= 0 && row < 40 && column >= 0 && column < 256) {
                hit_object(row * 256 + column);
                if (const int d = depot_at(column, row);
                    d >= 0 && std::find(depots.begin(), depots.end(), d) == depots.end()) {
                    depots.push_back(d);
                }
            }
        }
    }
    for (int d : depots) {
        damage_depot(d, 6);
    }
    sound_explosion();
}

void Game::break_emitter(int index) {
    // The beam it holds goes with it: diagonally from its corner of the
    // barrier ($4F top left, $4E top right, $4D bottom left, $4C bottom
    // right) to the opposite one.
    const u8 code = map_at(index);
    const int dx = (code == 0x4F || code == 0x4D) ? 1 : -1;
    const int dy = (code == 0x4F || code == 0x4E) ? 1 : -1;
    const int column = index % 256, row = index / 256;
    for (int k = 1; k <= 8; ++k) {
        const int at = (row + dy * k) * 256 + column + dx * k;
        const u8 beam = map_at(at);
        if (beam < 0x01 || beam > 0x08) {
            break;
        }
        set_cell_both(at, 0);
    }
    set_cell_both(index, 0);
    emitter_hits_.erase(index);
    ++frame_kills_;
    carnage_impact(1);
    FallingBody b;
    b.kind = FallingBody::Kind::emitter;
    b.x = column * 8;
    b.y = row * 8;
    b.width = b.height = 8;
    b.codes[0] = code;
    b.vx = dx * (0.4 + carnage_random() / 255.0 * 0.6); // off its frame, into the barrier
    b.vy = -0.8;
    // It tumbles fast, its beam still firing and sweeping all round.
    b.spin = (carnage_random() < 128 ? -1 : 1) * (0.06 + carnage_random() / 255.0 * 0.08);
    b.beam_angle = std::atan2(double(dy), double(dx));
    b.beam_on = true;
    add_falling(std::move(b));
    sound_explosion();
}

void Game::settle_rock() {
    // Slabs held only by their size are looked at again now and then, with
    // no limit: if they hold on to nothing, they come down after all.
    const bool recheck = !size_held_.empty() && frame_ % 25 == 0;
    if (craters_.empty() && !recheck) {
        return;
    }
    int limit = island_limit;
    // 0 not looked at, 1 done.
    std::vector<u8> marks(std::size_t(pair_columns * map_lines));
    std::vector<int> island, queue;
    std::vector<int> joined; // painted cells already joined in this search
    auto at = [](int px, int y) { return y * pair_columns + px; };
    auto painted = [&](int index) {
        const auto it = carved_.find(index);
        return it != carved_.end() && it->second.code == map_at(index) && it->second.painted;
    };
    auto try_island = [&](int px, int y) {
        if (px < 0 || px >= pair_columns || y < 0 || y >= map_lines || marks[std::size_t(at(px, y))] ||
            ground_at(px, y) != Ground::rock) {
            return;
        }
        island.clear();
        queue.assign(1, at(px, y));
        marks[std::size_t(at(px, y))] = 1;
        bool held = false;
        joined.clear();
        while (!queue.empty() && !held) {
            const int p = queue.back();
            queue.pop_back();
            island.push_back(p);
            const int qx = p % pair_columns, qy = p / pair_columns;
            // A fallen tower's ladders and mesh do not touch pixel to pixel:
            // its cells join whole, to each other and to the cells beside.
            if (const int cell = (qy >> 3) * 256 + (qx >> 2);
                painted(cell) && std::find(joined.begin(), joined.end(), cell) == joined.end()) {
                joined.push_back(cell);
                const int cx = qx >> 2, cy = qy >> 3;
                const int cells[5][2] = {{cx, cy}, {cx - 1, cy}, {cx + 1, cy}, {cx, cy - 1}, {cx, cy + 1}};
                for (const auto& n : cells) {
                    if (n[0] < 0 || n[0] > 255 || n[1] < 0 || n[1] >= 40) {
                        held = true; // the map's edge
                        continue;
                    }
                    if (structural_cell(n[1] * 256 + n[0])) {
                        held = true;
                        continue;
                    }
                    for (int y = n[1] * 8; y < n[1] * 8 + 8; ++y) {
                        for (int x = n[0] * 4; x < n[0] * 4 + 4; ++x) {
                            if (ground_at(x, y) == Ground::rock && !marks[std::size_t(at(x, y))]) {
                                marks[std::size_t(at(x, y))] = 1;
                                queue.push_back(at(x, y));
                            }
                        }
                    }
                }
            }
            const int next[4][2] = {{qx - 1, qy}, {qx + 1, qy}, {qx, qy - 1}, {qx, qy + 1}};
            for (const auto& n : next) {
                const auto g = ground_at(n[0], n[1]);
                // Held by anything fixed it touches, or by a structure in the
                // next cell (a tower's ladder has gaps at its sides, but the
                // rock beside it leans on it all the same).
                const int cell = (n[1] >> 3) * 256 + (n[0] >> 2);
                if (g == Ground::fixed ||
                    (cell != (qy >> 3) * 256 + (qx >> 2) && n[0] >= 0 && n[0] < pair_columns &&
                     n[1] >= 0 && n[1] < map_lines && structural_cell(cell))) {
                    held = true;
                } else if (g == Ground::rock && !marks[std::size_t(at(n[0], n[1]))]) {
                    marks[std::size_t(at(n[0], n[1]))] = 1;
                    queue.push_back(at(n[0], n[1]));
                }
            }
            if (!held && int(island.size() + queue.size()) > limit) {
                held = true;
                if (limit == island_limit) {
                    size_held_.push_back({px, y}); // only by its size: see recheck
                }
            }
        }
        if (held) {
            return; // (what is still queued stays marked: it is held too)
        }
        // It comes loose: out of the map, into a falling chunk.
        int left = pair_columns, right = 0, top = map_lines, bottom = 0;
        for (int p : island) {
            left = std::min(left, p % pair_columns);
            right = std::max(right, p % pair_columns);
            top = std::min(top, p / pair_columns);
            bottom = std::max(bottom, p / pair_columns);
        }
        FallingBody b;
        b.kind = FallingBody::Kind::rock;
        b.x = left * 2;
        b.y = top;
        b.width = (right - left + 1) * 2;
        b.height = bottom - top + 1;
        b.pixels.assign(std::size_t((right - left + 1) * b.height), 0);
        for (int p : island) {
            const int qx = p % pair_columns, qy = p / pair_columns;
            const int index = (qy >> 3) * 256 + (qx >> 2);
            b.pixels[std::size_t((qy - top) * (right - left + 1) + qx - left)] =
                u8(pair_of(cell_glyph_at(index, map_at(index)), qy, qx));
        }
        for (int p : island) {
            clear_pair(p % pair_columns, p / pair_columns);
        }
        // (A big slab falls flat: heavy, and drawn without turning it.)
        b.spin = island.size() > 1500 ? 0 : (carnage_random() / 255.0 - 0.5) * 0.04;
        if (island.size() > 40) {
            ++frame_kills_; // a chunk of the place broke away
        }
        add_falling(std::move(b));
    };
    if (recheck) {
        limit = pair_columns * map_lines; // all of it
        const auto suspects = std::move(size_held_);
        size_held_.clear();
        for (const auto& p : suspects) {
            try_island(p[0], p[1]);
        }
        limit = island_limit;
    }
    for (const auto& c : craters_) {
        // Around the crater's rim.
        for (int k = 0; k < 16; ++k) {
            const double a = k * 3.14159265 / 8;
            const int x = c[0] + int(std::lround(std::cos(a) * (crater + 2)));
            const int y = c[1] + int(std::lround(std::sin(a) * (crater + 2)));
            try_island(x >> 1, y);
        }
    }
    craters_.clear();
}

void Game::drop_tanks() {
    for (int i = 0; i < int(tanks_.size()); ++i) {
        auto& t = tanks_[std::size_t(i)];
        if (t.state != 2) {
            continue;
        }
        // Its ground: the top lines of the row under its three cells.
        const int column = t.column - 5; // object_index's column
        bool held = false;
        for (int px = column * 4; px < column * 4 + 12 && !held; ++px) {
            for (int y = (t.row + 1) * 8; y < (t.row + 1) * 8 + 3 && !held; ++y) {
                held = ground_at(px, y) != Ground::open;
            }
        }
        if (held) {
            continue;
        }
        // It falls, with the bit of ground it stood in.
        const int at = object_index(t.column, t.row);
        FallingBody b;
        b.kind = FallingBody::Kind::tank;
        b.x = column * 8;
        b.y = (t.row - 1) * 8;
        b.width = 24;
        b.height = 16;
        b.codes = {tables_.tank_characters[0], tables_.tank_characters[1], tables_.tank_characters[2],
                   map_at(at - 256 + 1)};
        b.vx = (t.direction & 0x80) ? -0.4 : 0.4;
        b.spin = ((t.direction & 0x80) ? -1 : 1) * (0.02 + carnage_random() / 255.0 * 0.03);
        add_falling(std::move(b));
        for (int k = 0; k < 3; ++k) {
            set_cell_both(at + k, 0);
        }
        map_set(at - 256 + 1, 0); // the turret
        t.state = 1;
        tank_blip(i); // off the radar
    }
}

void Game::add_falling(FallingBody b) {
    // Not into the list while move_falling walks it (a landing's blast or a
    // beam can break more loose): those wait until it is done.
    (moving_falling_ ? born_ : falling_).push_back(std::move(b));
}

void Game::move_falling() {
    moving_falling_ = true;
    const auto craft = world_from_screen(x_ + 12, y_ + 10);
    const bool craft_there = craft_ != Craft::inactive && craft_ != Craft::crashing;
    for (auto& b : falling_) {
        ++b.age;
        if (b.kind == FallingBody::Kind::tower) {
            if (b.resting > 0) {
                // At rest, painted in: still shown a few frames (the picture
                // has its pixels a frame or so later).
                if (--b.resting == 0) {
                    b.age = -1;
                }
            } else if (move_tower(b)) {
                b.resting = 3;
            } else if (craft_there) {
                // What is near the craft (its own copy of the map's seam).
                for (int r = 0; r < b.height; r += 4) {
                    for (int c = 0; c < b.width / 2; c += 2) {
                        const auto p = tower_pixel(b, c, r, 1);
                        double dx = p[0] - craft[0];
                        dx -= std::round(dx / (wrap_offset * 8)) * wrap_offset * 8;
                        if (std::abs(dx) < 20 && std::abs(p[1] - craft[1]) < 8) {
                            crushed_ = true;
                        }
                    }
                }
            }
            continue;
        }
        b.vy = std::min(b.vy + fall_gravity, fall_speed);
        if (b.kind == FallingBody::Kind::emitter && b.beam_on) {
            sweep_beam(b);
        }
        // Its lowest pixel in each pair column, against the ground.
        std::vector<int> bottom(std::size_t(b.width / 2), b.height - 1);
        if (b.kind == FallingBody::Kind::rock) {
            const int w = b.width / 2;
            for (int c = 0; c < w; ++c) {
                bottom[std::size_t(c)] = -1;
                for (int r = b.height - 1; r >= 0; --r) {
                    if (b.pixels[std::size_t(r * w + c)]) {
                        bottom[std::size_t(c)] = r;
                        break;
                    }
                }
            }
        }
        const int steps = std::max(1, int(std::ceil(std::max(std::abs(b.vx), std::abs(b.vy)))));
        for (int step = 0; step < steps && b.age > 0; ++step) {
            const double nx = b.x + b.vx / steps, ny = b.y + b.vy / steps;
            bool hit = false;
            for (int c = 0; c < int(bottom.size()) && !hit; ++c) {
                if (bottom[std::size_t(c)] >= 0) {
                    hit = ground_at(int(std::floor(nx / 2)) + c, int(std::floor(ny)) + bottom[std::size_t(c)]) !=
                          Ground::open;
                }
            }
            if (hit) {
                land(b);
                b.age = -1; // gone
                break;
            }
            b.x = nx;
            b.y = ny;
        }
        if (b.age > 0 && craft_there && std::abs(b.x + b.width / 2.0 - craft[0]) < 22 + b.width / 2.0 &&
            std::abs(b.y + b.height / 2.0 - craft[1]) < 9 + b.height / 2.0) {
            crushed_ = true; // it fell on the craft
        }
        if (b.y > map_lines) {
            b.age = -1;
        }
    }
    std::erase_if(falling_, [](const FallingBody& b) { return b.age < 0; });
    moving_falling_ = false;
    // What came loose while these moved (a landing blast, a beam) joins now.
    for (auto& b : born_) {
        falling_.push_back(std::move(b));
    }
    born_.clear();
}

void Game::land(FallingBody& b) {
    const int cx = int(b.x + b.width / 2.0), by = int(b.y + b.height);
    switch (b.kind) {
    case FallingBody::Kind::rock: {
        // It shatters: its pixels fly as debris, and it bites the ground
        // (and whatever it lands on).
        const int w = b.width / 2;
        // (Of a big slab, some of its pixels: enough debris for the eye.)
        int count = 0;
        for (const u8 p : b.pixels) {
            count += p != 0;
        }
        const int stride = std::max(1, count / 1200);
        int n = 0;
        for (int r = 0; r < b.height; ++r) {
            for (int c = 0; c < w; ++c) {
                if (const u8 pair = b.pixels[std::size_t(r * w + c)]; pair && n++ % stride == 0) {
                    happenings_.carved.push_back(
                        {int(b.x) + c * 2, int(b.y) + r, pair, 0, b.vy, c == w / 2 && r == b.height - 1});
                }
            }
        }
        const double size = std::sqrt(double(b.pixels.size()));
        blast(cx, by, std::min(8.0, 2.5 + size / 6));
        break;
    }
    case FallingBody::Kind::tank:
        // It blows up where it lands, as a destroyed tank (and scores as one).
        happenings_.tanks.push_back({{int(b.x) / 8, (int(b.y) + 8) / 8}, b.codes[3]});
        add_score(0x50, 0x02);
        blast(cx, by - 2, 10);
        break;
    case FallingBody::Kind::emitter:
        happenings_.impacts.push_back({{cx / 8, by / 8}, 3, b.codes[0], true});
        blast(cx, by - 1, 4);
        break;
    case FallingBody::Kind::tower:
        break; // (towers come to rest in move_tower)
    }
}

void Game::carnage_frame() {
    move_tracers();
    rock_towers();
    settle_rock();
    if (carnage()) {
        drop_tanks();
    }
    move_falling();
    settle_rock(); // (the craters of what landed)
}

// --- towers that rock and tip over --------------------------------------------

Game::Tower* Game::tower_at(int column, int row) {
    column %= wrap_offset; // the map's own column (both copies alike)
    for (auto& t : towers_) {
        for (int c = 0; c < t.width; ++c) {
            if ((t.column + c) % wrap_offset == column && row >= t.row && row < t.row + t.height) {
                return &t;
            }
        }
    }
    // Its cells: ladders and mesh joined to the one hit, across the seam
    // where the map's first columns repeat.
    std::vector<std::array<int, 2>> cells{{column, row}}, queue{{column, row}};
    auto known = [&](int c, int r) {
        return std::find(cells.begin(), cells.end(), std::array<int, 2>{c, r}) != cells.end();
    };
    while (!queue.empty() && cells.size() < 200) {
        const auto [c, r] = queue.back();
        queue.pop_back();
        const int next[4][2] = {{(c + wrap_offset - 1) % wrap_offset, r}, {(c + 1) % wrap_offset, r},
                                {c, r - 1}, {c, r + 1}};
        for (const auto& n : next) {
            if (n[1] >= 0 && n[1] < 40 && !known(n[0], n[1]) && tower_code(map_code(n[0], n[1]))) {
                cells.push_back({n[0], n[1]});
                queue.push_back({n[0], n[1]});
            }
        }
    }
    // In the copy that holds it whole: across the seam, the high one.
    bool low = false, high = false;
    for (const auto& c : cells) {
        low |= c[0] < wrap_columns;
        high |= c[0] >= wrap_offset - wrap_columns;
    }
    int left = 1 << 20, right = -1, top = 40, bottom = -1;
    for (auto& c : cells) {
        if (low && high && c[0] < wrap_columns) {
            c[0] += wrap_offset;
        }
        left = std::min(left, c[0]);
        right = std::max(right, c[0]);
        top = std::min(top, c[1]);
        bottom = std::max(bottom, c[1]);
    }
    const int width = right - left + 1, height = bottom - top + 1;
    if (height < 3 || width > 12 || height > 14 || right > 255) {
        return nullptr; // not a standing tower
    }
    for (const auto& d : depot_cells()) {
        // A fuel depot (six by five cells from its corner) is not one.
        for (const auto& c : cells) {
            if ((c[0] - d.column + 2 * wrap_offset) % wrap_offset < 6 && c[1] >= d.row && c[1] < d.row + 5) {
                return nullptr;
            }
        }
    }
    towers_.push_back({left, top, width, height, 0, 0});
    return &towers_.back();
}

void Game::push_tower(Tower& t, double x, double y, double dx) {
    (void)x;
    if (dx == 0) {
        return; // straight down: no push sideways
    }
    const double base = (t.row + t.height) * 8.0;
    const double h = std::clamp((base - y) / (t.height * 8.0), 0.0, 1.0);
    t.spin += (dx > 0 ? 1 : -1) * tower_push * h * h;
}

bool Game::tower_side_open(const Tower& t, int side) const {
    // The column of cells beside it, over its upper part.
    int column = side > 0 ? t.column + t.width : t.column - 1;
    if (column < 0) {
        column += wrap_offset;
    }
    const int lines = (t.height * 8 * 6) / 10;
    for (int y = t.row * 8; y < t.row * 8 + lines; ++y) {
        for (int px = column * 4; px < column * 4 + 4; ++px) {
            if (ground_at(px, y) != Ground::open) {
                return false;
            }
        }
    }
    return true;
}

void Game::rock_towers() {
    for (auto& t : towers_) {
        t.spin += -tower_spring * t.lean - tower_damping * t.spin;
        t.lean += t.spin;
        // It leans only where there is room.
        if ((t.lean > 0 && !tower_side_open(t, 1)) || (t.lean < 0 && !tower_side_open(t, -1))) {
            t.lean = 0;
            t.spin = -t.spin * 0.3;
        }
        if (std::abs(t.lean) > tip_angle) {
            topple(t, t.lean > 0 ? 1 : -1);
            t.width = 0; // gone
        }
    }
    std::erase_if(towers_, [](const Tower& t) {
        return t.width == 0 || (std::abs(t.lean) < 0.0005 && std::abs(t.spin) < 0.0002);
    });
}

std::vector<Game::TowerView> Game::rocking_towers() const {
    std::vector<TowerView> views;
    for (const auto& t : towers_) {
        if (std::abs(t.lean) > 0.002) {
            views.push_back({t.column, t.row, t.width, t.height, t.lean});
        }
    }
    return views;
}

void Game::topple(const Tower& t, int side) {
    // Out of the map, into a body turning on its base corner.
    FallingBody b;
    b.kind = FallingBody::Kind::tower;
    b.x = t.column * 8;
    b.y = t.row * 8;
    b.width = t.width * 8;
    b.height = t.height * 8;
    const int w = t.width * 4;
    b.pixels.assign(std::size_t(w * b.height), 0);
    for (int r = 0; r < t.height; ++r) {
        for (int c = 0; c < t.width; ++c) {
            const int index = (t.row + r) * 256 + t.column + c;
            const u8 code = map_at(index);
            if (!tower_code(code)) {
                continue;
            }
            const auto& glyph = cell_glyph_at(index, code);
            for (int line = 0; line < 8; ++line) {
                for (int p = 0; p < 4; ++p) {
                    b.pixels[std::size_t((r * 8 + line) * w + c * 4 + p)] = u8(pair_of(glyph, line, p));
                }
            }
            set_cell_both(index, 0);
        }
    }
    b.pivot_x = side > 0 ? b.x + b.width : b.x;
    b.pivot_y = b.y + b.height;
    b.angle = t.lean;
    b.omega = t.spin;
    b.pivoting = true;
    add_falling(std::move(b));
    sound_explosion();
    frame_kills_ += 2; // a tower!
    carnage_impact(1);
}

std::array<double, 2> Game::tower_pixel(const FallingBody& b, int c, int r, double sub) const {
    const double dx = b.x + c * 2 + sub - b.pivot_x, dy = b.y + r + 0.5 - b.pivot_y;
    const double cs = std::cos(b.angle), sn = std::sin(b.angle);
    return {b.pivot_x + cs * dx - sn * dy + b.ox, b.pivot_y + sn * dx + cs * dy + b.oy};
}

bool Game::move_tower(FallingBody& b) {
    const int w = b.width / 2;
    auto hits = [&](const FallingBody& at) {
        for (int r = 0; r < at.height; ++r) {
            for (int c = 0; c < w; ++c) {
                if (!at.pixels[std::size_t(r * w + c)]) {
                    continue;
                }
                const auto p = tower_pixel(at, c, r, 1);
                if (at.pivoting && std::hypot(p[0] - at.pivot_x, p[1] - at.pivot_y) < 3) {
                    continue; // where it stands on its corner
                }
                if (ground_at(int(std::floor(p[0] / 2)), int(std::floor(p[1]))) != Ground::open) {
                    return true;
                }
            }
        }
        return false;
    };
    const int side = b.pivot_x > b.x + 1 ? 1 : -1;
    if (b.pivoting) {
        // Over it goes, faster and faster.
        b.omega += side * 0.0012 + 0.008 * std::sin(b.angle);
        FallingBody next = b;
        next.angle = std::clamp(b.angle + b.omega, -half_turn, half_turn);
        if (hits(next)) {
            rest_tower(b); // it leans on what it met
            return true;
        }
        b.angle = next.angle;
        if (std::abs(b.angle) >= half_turn) {
            b.pivoting = false; // flat: it drops from its corner
            b.ovx = side * 0.6;
            b.ovy = 0;
        }
        return false;
    }
    b.ovy = std::min(b.ovy + fall_gravity, fall_speed);
    b.ovx *= 0.97;
    const int steps = std::max(1, int(std::ceil(std::max(std::abs(b.ovx), std::abs(b.ovy)))));
    for (int k = 0; k < steps; ++k) {
        FallingBody next = b;
        next.ox += b.ovx / steps;
        next.oy += b.ovy / steps;
        if (hits(next)) {
            rest_tower(b);
            return true;
        }
        b.ox = next.ox;
        b.oy = next.oy;
    }
    return b.pivot_y + b.oy > map_lines + 64; // lost below the map
}

void Game::paint_pair(int pair_x, int y, u8 pair) {
    if (pair_x < 0 || pair_x >= pair_columns || y < 0 || y >= map_lines ||
        ground_at(pair_x, y) != Ground::open) {
        return;
    }
    auto paint = [&](int index) {
        u8 code = map_at(index);
        if (code == 0) {
            code = 0x61; // painted rock: carvable, as the rest
            map_set(index, code);
            carved_.insert_or_assign(index, CarvedCell{code, {}});
        } else if (!carvable(code)) {
            return;
        }
        auto it = carved_.find(index);
        if (it == carved_.end() || it->second.code != code) {
            it = carved_.insert_or_assign(index, CarvedCell{code, glyphs_[code & 0x7F]}).first;
        }
        it->second.painted = true;
        auto& line = it->second.glyph[std::size_t(y & 7)];
        const int shift = 6 - 2 * (pair_x & 3);
        line = u8((line & ~(3 << shift)) | (pair << shift));
    };
    const int column = pair_x >> 2, index = (y >> 3) * 256 + column;
    paint(index);
    if (column < wrap_columns) {
        paint(index + wrap_offset);
    } else if (column >= wrap_offset && column < wrap_offset + wrap_columns) {
        paint(index - wrap_offset);
    }
}

void Game::rest_tower(const FallingBody& b) {
    // It comes to rest as part of the place: its pixels painted into the
    // map (as rock cells, so they hold and can be shot apart). Every map
    // pixel it covers takes the tower pixel turned onto it (so it has no
    // gaps; a gappy tower would come apart in loose pieces).
    const int w = b.width / 2;
    double left = 1e9, right = -1e9, top = 1e9, bottom = -1e9;
    for (const auto& corner : {tower_pixel(b, 0, 0, 0), tower_pixel(b, w, 0, 0), tower_pixel(b, 0, b.height, 0),
                               tower_pixel(b, w, b.height, 0)}) {
        left = std::min(left, corner[0]);
        right = std::max(right, corner[0]);
        top = std::min(top, corner[1]);
        bottom = std::max(bottom, corner[1]);
    }
    const double cs = std::cos(b.angle), sn = std::sin(b.angle);
    for (int y = int(std::floor(top)) - 1; y <= int(std::ceil(bottom)) + 1; ++y) {
        for (int px = int(std::floor(left / 2)) - 1; px <= int(std::ceil(right / 2)) + 1; ++px) {
            // This pixel's middle, turned back into the tower.
            const double dx = px * 2 + 1 - b.ox - b.pivot_x, dy = y + 0.5 - b.oy - b.pivot_y;
            const double sx = cs * dx + sn * dy + b.pivot_x - b.x, sy = -sn * dx + cs * dy + b.pivot_y - b.y;
            const int c = int(std::floor(sx / 2)), r = int(std::floor(sy));
            if (c >= 0 && c < w && r >= 0 && r < b.height) {
                if (const u8 pair = b.pixels[std::size_t(r * w + c)]) {
                    paint_pair(px, y, pair);
                }
            }
        }
    }
    const auto low = tower_pixel(b, w / 2, b.height - 1, 1);
    happenings_.impacts.push_back({{int(low[0]) / 8, int(low[1]) / 8}, 3, 0x58, true});
    sound_explosion();
    carnage_impact(2); // the crash of it
}

// --- fuel depots that burn and blow up -------------------------------------------

namespace {
constexpr int depot_strength = 60; // hits; a blast counts six
constexpr int depot_width = 6, depot_height = 5;
} // namespace

int Game::depot_at(int column, int row) const {
    for (const auto& d : depot_cells()) {
        if ((column - d.column + 2 * wrap_offset) % wrap_offset < depot_width && row >= d.row &&
            row < d.row + depot_height) {
            return d.column % wrap_offset;
        }
    }
    return -1;
}

int Game::depot_near(int column) const {
    for (const auto& d : depot_cells()) {
        const int dx = (column - d.column + 2 * wrap_offset) % wrap_offset;
        if (dx < depot_width + 2 || dx > wrap_offset - 3) {
            return d.column % wrap_offset;
        }
    }
    return -1;
}

bool Game::depot_gone(int depot) const {
    if (depot < 0) {
        return false;
    }
    const auto it = depot_hits_.find(depot);
    return it != depot_hits_.end() && it->second >= depot_strength;
}

std::vector<Game::DepotView> Game::depot_damage() const {
    std::vector<DepotView> views;
    for (const auto& d : depot_cells()) {
        const auto it = depot_hits_.find(d.column % wrap_offset);
        if (it != depot_hits_.end() && it->second < depot_strength) {
            views.push_back({d, double(it->second) / depot_strength});
        }
    }
    return views;
}

void Game::damage_depot(int depot, int hits) {
    if (depot < 0 || depot_gone(depot)) {
        return;
    }
    const auto depots = depot_cells(); // (before it is gone from the list)
    if ((depot_hits_[depot] += hits) < depot_strength) {
        return;
    }
    // It blows up: gone from the map (each copy), a crater all round, and
    // what is near goes with it; the craft too, if it is close.
    MapCell where{};
    double nearest = 1e9;
    const auto craft = world_from_screen(x_ + 12, y_ + 10);
    for (const auto& d : depots) {
        if (d.column % wrap_offset != depot) {
            continue;
        }
        for (int r = 0; r < depot_height; ++r) {
            for (int c = 0; c < depot_width; ++c) {
                set_cell_both((d.row + r) * 256 + d.column + c, 0);
            }
        }
        const double x = (d.column + depot_width / 2.0) * 8;
        if (std::abs(x - craft[0]) < nearest) {
            nearest = std::abs(x - craft[0]);
            where = {int(x), (d.row + depot_height / 2) * 8 + 4};
        }
    }
    if (engine_ == 0x0A) {
        engine_ = 8; // no more fuel from it
    }
    if (level_ == 2) {
        // The last level's depot is where the mission ends: now it cannot.
        doomed_ = true;
        doomed_timer_ = 160;
    }
    blast(where.column, where.row, 22);
    happenings_.depot_blasts.push_back(where);
    if (craft_ != Craft::inactive && craft_ != Craft::crashing &&
        std::hypot(craft[0] - where.column, craft[1] - where.row) < 44) {
        crushed_ = true; // caught in the fireball
    }
}

// --- combos and overdrive --------------------------------------------------------

namespace {
constexpr int combo_time = 100;      // frames to the next kill for the combo
constexpr double meter_per_kill = 0.06;
constexpr int overdrive_time = 300;  // frames
} // namespace

void Game::carnage_score() {
    auto& c = carnage_state_;
    if (doomed_timer_ > 0 && mode_ == Mode::playing) {
        // The message flashes in the message row (kept there: the game
        // clears the row when its own message time is out).
        static const data::Text well_done = {0x37, 0x25, 0x2C, 0x2C, 0x00, 0x00, 0x24, 0x2F, 0x2E, 0x25,
                                             0x00, 0x00, 0x29, 0x24, 0x29, 0x2F, 0x34}; // WELL DONE IDIOT
        message_timer_ = 2;
        if (frame_ & 8) {
            draw_text(well_done, 5, 0);
        } else {
            clear_message_row();
        }
        --doomed_timer_;
    }
    if (doomed_ && doomed_timer_ == 0 && craft_ != Craft::crashing && craft_ != Craft::inactive) {
        crushed_ = true; // and that is the end of it (again, past a grace)
    }
    const bool enemy_down = enemy_.state == 4;
    int kills = frame_kills_ + int(happenings_.tanks.size() + happenings_.mines.size() +
                                   happenings_.depot_blasts.size());
    if (enemy_down && !enemy_was_down_) {
        ++kills;
        carnage_impact(2);
    }
    enemy_was_down_ = enemy_down;
    if (!happenings_.tanks.empty() || !happenings_.mines.empty()) {
        carnage_impact(happenings_.tanks.empty() ? 1 : 2);
    }
    if (!happenings_.depot_blasts.empty()) {
        carnage_impact(3);
    }
    c.impact = frame_impact_;
    frame_kills_ = frame_impact_ = 0;
    if (kills > 0 && carnage()) {
        for (int k = 0; k < kills; ++k) {
            ++c.combo;
            // The bonus: 50 points for each kill in the row so far (up to ten).
            for (int n = std::min(c.combo, 10); n > 0; --n) {
                add_score(0x50, 0x00);
            }
            if (c.overdrive == 0 && (c.meter += meter_per_kill * std::min(c.combo, 5)) >= 1) {
                c.meter = 0;
                c.overdrive = overdrive_time;
            }
        }
        c.combo_timer = combo_time;
    } else if (c.combo_timer > 0 && --c.combo_timer == 0) {
        c.combo = 0;
    }
    if (c.overdrive > 0) {
        --c.overdrive;
    }
}

// --- a falling emitter's beam ------------------------------------------------------

bool Game::object_pixel(int x, int y) const {
    // A pixel of a tank, turret, mine or missile (not the men: they are
    // spared).
    if (x < 0 || x >= pair_columns * 2 || y < 0 || y >= map_lines) {
        return false;
    }
    const int index = (y >> 3) * 256 + (x >> 3);
    const u8 code = map_at(index);
    const auto& men = tables_.prisoner_characters;
    if (code == 0 || std::find(men.begin(), men.end(), code) != men.end()) {
        return false;
    }
    bool object = code == 0x72;
    for (int k = 0x12; k >= 0 && !object; --k) {
        object = code == tables_.obstacles[std::size_t(k)];
    }
    return object && pair_of(cell_glyph_at(index, code), y, x >> 1) != 0;
}

void Game::sweep_beam(FallingBody& b) {
    constexpr int beam_frames = 110, beam_reach = 200;
    if (b.age > beam_frames) {
        b.beam_on = false;
        return;
    }
    // From its middle, along its beam's diagonal turned as it turns, to the
    // first solid thing: that melts, what lives there dies.
    const double a = b.beam_angle + b.spin * b.age;
    const double dx = std::cos(a), dy = std::sin(a);
    const double x0 = b.x + 4, y0 = b.y + 4;
    const auto craft = world_from_screen(x_ + 12, y_ + 10);
    const bool craft_there = craft_ != Craft::inactive && craft_ != Craft::crashing;
    double reach = beam_reach;
    for (double d = 6; d < beam_reach; d += 1.5) {
        const int x = int(std::floor(x0 + dx * d)), y = int(std::floor(y0 + dy * d));
        if (craft_there && std::abs(x - craft[0]) < 18 && std::abs(y - craft[1]) < 7) {
            crushed_ = true; // it cuts through the craft
        }
        const auto g = ground_at(x >> 1, y);
        const int cell = (y >> 3) * 256 + (x >> 3);
        if (g != Ground::open || object_pixel(x, y)) {
            reach = d;
            if (g == Ground::rock && (b.age & 1)) {
                Tracer t;
                t.dx = dx * 4;
                t.dy = dy * 4;
                carve(x, y, 2.5, t); // a molten line where it sweeps
            }
            if (x >= 0 && x < pair_columns * 2 && y >= 0 && y < map_lines) {
                hit_object(cell); // (another emitter it burns may come loose too)
                damage_depot(depot_at(x >> 3, y >> 3), 1);
            }
            break;
        }
    }
    b.beam_ex = x0 + dx * reach;
    b.beam_ey = y0 + dy * reach;
}

} // namespace fort_apocalypse::game
