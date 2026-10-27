// The acid puddles ($11BD34-$11C5BA): green ooze on Intermediate and Ultimate.
//
// The Track lists them (header +$20: a table whose first entry is a list of
// 8-byte entries {first band, last band, path, zone}; a path of 0 starts a
// group picked at random). One appears when the view scrolls into its bands
// and goes when it leaves them. A puddle creeps a cell per round of its
// animation towards its next waypoint (3 bytes: cell x, y, the next one's
// index), x before y or y before x by its last heading, faster while a
// player is in its zone (zone 6 only). A player's marble that touches the
// puddle's frame box dissolves in it: drawn into its middle while the
// dissolve animation plays, then the player starts again; the puddle
// slows down for a while meanwhile.
#include "marble_madness_amiga/game/race.hpp"

namespace marble_madness::game {
namespace {

std::int16_t s16(std::int32_t v) { return std::int16_t(v); }

// The acid table's entries (offsets into header +$20's table).
enum AcidTable : std::uint32_t {
    spawn_list = 0x00,
    creep_x_plus = 0x04,
    creep_y_plus = 0x08,
    creep_y_minus = 0x0C,
    creep_x_minus = 0x10,
    single_paths = 0x14,  // two paths for an entry of zone 5 (one is picked)
    group_paths = 0x1C,   // two groups of six paths for the others
    group_size = 0x18,
};

// A frame's box (record +4: x, y offsets, width, depth), or the default.
struct FrameBox {
    int x, y, w, h;
};

FrameBox frame_box(const amiga::LoadedImage& t, std::uint32_t frame) {
    const std::uint32_t record = t.contains(frame, 4) ? t.u32(frame) : 0xFFFFFFFF;
    if (record == 0xFFFFFFFF || !t.contains(record, 8)) {
        return {-2, -2, 0xC, 0xC};
    }
    return {t.s8(record + 4), t.s8(record + 5), t.s8(record + 6), t.s8(record + 7)};
}

} // namespace

// $11BE98: the bands from `from` to `to`.
void Race::spawn_acid(int from, int to) {
    if (course_ != data::Course::intermediate && course_ != data::Course::ultimate) {
        return;
    }
    const auto& t = files_->track(course_);
    const std::uint32_t table = t.u32(files_->track_header(course_) + 0x20);
    auto free_acid = [this]() -> Acid* {
        for (auto& a : acids) {
            if (a.status == 0) {
                return &a;
            }
        }
        return nullptr;
    };
    bool full = false;
    for (std::uint32_t e = t.u32(table + spawn_list); !full && t.s8(e) != -1; e += 8) {
        const std::uint32_t path = t.u32(e + 2);
        bool running = false;
        for (const auto& a : acids) {
            running |= a.status != 0 && a.path == path;
        }
        const int first = t.s8(e), last = t.s8(e + 1);
        if (running || (first != to && last != to) || (from >= first && from <= last)) {
            continue;
        }
        if (path != 0) {
            Acid* a = free_acid();
            if (a == nullptr) {
                break;
            }
            start_acid(*a, path, e);
            continue;
        }
        // A group: one of two paths (zone 5) or one of two sets of six.
        const bool single = t.s8(e + 6) == 5;
        std::uint32_t list = single ? table + single_paths + (random.next(2) != 0 ? 4u : 0u)
                                    : table + group_paths + (random.next(2) != 0 ? group_size : 0u);
        for (int i = 0; i < (single ? 1 : 6); ++i) {
            Acid* a = free_acid();
            if (a == nullptr) {
                full = true;
                break;
            }
            start_acid(*a, t.u32(list), e);
            list += 4;
        }
    }
    for (auto& a : acids) {
        if (a.status != 0 && ((from == a.band_from && to < a.band_from) || (from == a.band_to && a.band_to < to))) {
            a.status = 0;
            hide(0xE, a.index);
        }
    }
}

void Race::start_acid(Acid& a, std::uint32_t path, std::uint32_t entry) {
    const auto& t = files_->track(course_);
    a.waypoint = a.path = path;
    a.zone = t.s8(entry + 6);
    a.band_from = std::int16_t(t.s8(entry));
    a.band_to = std::int16_t(t.s8(entry + 1));
    a.x = t.s8(path) << 19;
    a.y = t.s8(path + 1) << 19;
    probe_.place(a.x, a.y);
    a.z = probe_.ground_height_here(terrain_);
    a.vx = a.vy = 0;
    a.status = 1;
    a.screen = 0;
    acid_next(a);
    acid_place(a);
    show(0xE, a.index);
}

// $11BD58: one update: the animation, a cell on at the end of each round.
void Race::update_acid(Acid& a) {
    if (a.status == 0) {
        return;
    }
    const auto& t = files_->track(course_);
    if (++a.tick >= a.period) {
        a.tick = 0;
        if (a.period > 2) {
            a.period = 1; // the slow-down after a meal ends
        }
        a.frame += 4;
        if (t.u32(a.frame) == 0xFFFFFFFF) {
            a.frame = a.animation;
            a.x += a.vx;
            a.y += a.vy;
            acid_next(a);
        }
    }
    acid_place(a);
}

// $11C54C: at its waypoint, on to the next one; then the heading.
void Race::acid_next(Acid& a) {
    const auto& t = files_->track(course_);
    if (s16(a.x >> 19) == t.s8(a.waypoint) && s16(a.y >> 19) == t.s8(a.waypoint + 1)) {
        a.waypoint = a.path + std::uint32_t(t.s8(a.waypoint + 2) * 3);
    }
    acid_heading(a);
}

// $11C5BA: a cell towards the waypoint (along x first unless it was moving
// along x), the animation for that direction, two updates a step (one while
// a player is in its zone, which only zone 6 checks).
void Race::acid_heading(Acid& a) {
    const auto& t = files_->track(course_);
    const std::uint32_t table = t.u32(files_->track_header(course_) + 0x20);
    const int wx = t.s8(a.waypoint), wy = t.s8(a.waypoint + 1);
    const int cx = s16(a.x >> 19), cy = s16(a.y >> 19);
    int dx = 0, dy = 0;
    auto along_x = [&] { return wx < cx ? (dx = -8, true) : cx < wx ? (dx = 8, true) : false; };
    auto along_y = [&] { return wy < cy ? (dy = -8, true) : cy < wy ? (dy = 8, true) : false; };
    if (a.vx == 0) {
        along_x() || along_y();
    } else {
        along_y() || along_x();
    }
    const std::uint32_t list = dy > 0 ? creep_y_plus : dy < 0 ? creep_y_minus : dx > 0 ? creep_x_plus : creep_x_minus;
    a.animation = t.u32(table + list);
    a.vx = dx << 16;
    a.vy = dy << 16;
    a.frame = a.animation;
    a.tick = 0;
    a.period = 2;
    for (int i = 0; i < players_; ++i) {
        const Marble& m = marbles[std::size_t(i)];
        if (m.status == 1 && m.feature == a.zone && a.zone == 6) {
            a.period = 1;
            break;
        }
    }
}

// $11C48E: the screen place ($106A00).
void Race::acid_place(Acid& a) {
    probe_.set_point(s16(a.x >> 16), s16(a.y >> 16));
    const auto x = s16(a.x >> 16), y = s16(a.y >> 16), z = s16(a.z >> 16);
    const auto screen_x = s16(y - x + 0x88);
    const auto screen_y = s16(s16(view_) + z + 0x6C - s16((x + y) >> 1));
    a.screen = std::int32_t(std::uint32_t(std::uint16_t(screen_x)) << 16 | std::uint16_t(screen_y));
}

// $11C1C0: a player's marble against the puddles' frame boxes (its left
// and back edges, 5 and 1 before its point along x, 6 and 3 along y).
bool Race::touch_acid(Marble& m) {
    if (course_ != data::Course::intermediate && course_ != data::Course::ultimate) {
        return false;
    }
    const auto& t = files_->track(course_);
    const auto px = probe_.x(), py = probe_.y();
    const bool right = px < py;
    for (auto& a : acids) {
        if (a.status == 0) {
            continue;
        }
        const auto box = frame_box(t, a.frame);
        const auto x0 = s16(box.x + s16(a.x >> 16)), y0 = s16(box.y + s16(a.y >> 16));
        auto within = [](int v, int from, int size) { return from <= v && v <= s16(from + size); };
        const bool x_hit = within(s16(px - 5), x0, box.w) || within(s16(px - 1), x0, box.w);
        const bool y_hit = within(s16(py - 6), y0, box.h) || within(s16(py - 3), y0, box.h);
        if (!x_hit || !y_hit) {
            continue;
        }
        set_mode(m, Marble::dissolving);
        sound(0x1D, right);
        m.region_code = std::int8_t(a.index);
        a.period = 0x1C;
        return true;
    }
    return false;
}

// $11C34C: a dissolving marble moves a pixel a update towards the middle of
// its puddle's frame box (whole pixels: the fractions go).
void Race::pull_into_acid(Marble& m) {
    const Acid& a = acids[std::size_t(std::uint8_t(m.region_code)) % acids.size()];
    const auto box = frame_box(files_->track(course_), a.frame);
    const int cx = s16((box.w >> 1) + box.x + s16(a.x >> 16));
    const int cy = s16((box.h >> 1) + box.y + s16(a.y >> 16));
    const int x = s16(m.x >> 16), y = s16(m.y >> 16);
    m.x = (x + (cx < x ? -1 : x < cx ? 1 : 0)) << 16;
    m.y = (y + (cy < y ? -1 : y < cy ? 1 : 0)) << 16;
}

} // namespace marble_madness::game
