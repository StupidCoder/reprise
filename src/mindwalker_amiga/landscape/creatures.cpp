#include "mindwalker_amiga/landscape/creatures.hpp"
#include <algorithm>
#include <cstdlib>

namespace mindwalker::landscape {
namespace {

// A's data (research layout).
constexpr std::uint32_t random_seeds = 0x207F80;      // three shift registers
constexpr std::uint32_t course_dx = 0x207F14, course_dy = 0x207F34; // 8 longs each
constexpr std::uint32_t attach_masks = 0x207F54;      // by creatures attached (words)
constexpr std::uint32_t follow_speeds = 0x207F5C;     // by stage / 7 (longs)
constexpr std::uint32_t depth_limits = 0x207EE4;      // h39: by creature (12 longs)
constexpr std::uint32_t kind_frames = 0x2004EC;       // first frame of each kind in S's +$2D0 set (5 longs)
constexpr int off_screen = -80;                       // $FFFFFFB0
constexpr int idle_band = 0xD2;
constexpr int symbols_set = 4;                        // Assets::figures: the +$2D0 set

bool near(int ax, int ay, int bx, int by, int range) { // $207060
    return std::abs(ax - bx) < range && std::abs(ay - by) < range;
}

std::uint16_t row_word(const data::Bitmap& image, int row, int plane) {
    std::uint16_t word = 0;
    for (int x = 0; x < 16; ++x) word = std::uint16_t(word << 1 | (image.at(x, row) >> plane & 1));
    return word;
}

} // namespace

Creatures::Creatures(const amiga::LoadedImage& a, const data::Assets& assets)
    : a_(a), assets_(assets), tethers_(std::size_t(320 * 200)) {
    random_a_.state = a.u32(random_seeds);
    random_b_.state = a.u32(random_seeds + 4);
    random_c_.state = a.u32(random_seeds + 8);
    // h38 ($206BA8): bands of 30 rows to begin with, nobody present.
    for (int i = 0; i < 12; ++i) creatures_[std::size_t(i)].top = (i % 6) * 30;
    reset();
    forget_views();
}

const data::Bitmap& Creatures::image_a(int kind) const {
    return assets_.figures[symbols_set].frames[std::size_t(a_.s32(kind_frames + std::uint32_t(kind * 4)))];
}

const data::Bitmap& Creatures::image_b(int kind) const {
    return assets_.figures[symbols_set].frames[std::size_t(a_.s32(kind_frames + std::uint32_t(kind * 4)) + 1)];
}

// (The original also moves every sprite to (-80, -20).)
void Creatures::reset() {
    for (auto& c : creatures_) {
        c.kind = -1;
        c.tether_x = -1;
        c.shown_x = off_screen;
        c.shown_y = -20;
    }
    attached_ = 0;
}

void Creatures::hide_all(const Scene& scene) {
    for (int i = 0; i < 12; ++i) place(i, off_screen, creatures_[std::size_t(i)].top, scene);
}

void Creatures::show_present(const Scene& scene) {
    for (int i = 0; i < 12; ++i) {
        const auto& c = creatures_[std::size_t(i)];
        if (c.present) place(i, c.x, c.y, scene);
    }
}

void Creatures::clear_presence(const Scene& scene) {
    for (auto& c : creatures_) c.present = false;
    hide_all(scene);
}

void Creatures::forget_views() {
    current_x_ = 0xFFFF;
    for (auto& m : history_) m.x = 0xFFFF;
}

// $2070BE: the view being left, with the kinds present in it.
void Creatures::record_view() {
    auto& m = history_[std::size_t(history_next_)];
    m.x = current_x_;
    m.y = current_y_;
    m.kinds = {};
    for (const auto& c : creatures_)
        if (c.present && c.kind >= 0) ++m.kinds[std::size_t(c.kind)];
    history_next_ = (history_next_ + 1) & 7;
}

void Creatures::enter_view(std::uint16_t view_x, std::uint16_t view_y, int creature_places, int platform, int distance,
                           bool marked, const Scene& scene) {
    timer_ = 1000;
    record_view();
    current_x_ = view_x;
    current_y_ = view_y;
    reset();
    // $2071AA. A view seen lately brings back its kinds.
    bool known = false;
    std::array<int, 5> kinds{};
    int next_kind = 0;
    if (creature_places == 0) {
        for (int k = 6; k >= 0 && !known; --k) {
            const auto& m = history_[std::size_t((k + history_next_) & 7)];
            known = m.x == view_x && m.y == view_y;
            if (known) {
                kinds = m.kinds;
                next_kind = random_a_.next() & 3;
            }
        }
    }
    score_bonus_ = creature_places * 25;
    quiet_ = marked;
    int bands = std::clamp(6 - distance / 12, 2, 6);
    kind_mask_ = 0x407;
    int start_chance;
    if (creature_places == 0) {
        if (platform == 0) {
            start_chance = 0x40;
            appear_chance_ = 1;
        } else {
            start_chance = std::max(0xC0 - 2 * distance, 7);
            appear_chance_ = std::max(0x3F - distance, 7);
        }
    } else {
        start_chance = 0x100;
        appear_chance_ = 0;
        bands = 6;
        kind_mask_ = 0xFFFF;
    }
    const int band_height = 0xBA / bands;
    attached_ = 0;
    for (int i = 0; i < 12; ++i) {
        auto& c = creatures_[std::size_t(i)];
        c.tether_x = -1;
        const int slot = i < 6 ? i : i - 6;
        c.x = off_screen;
        c.enabled = slot < bands;
        if (!c.enabled || quiet_) {
            c.top = c.y = idle_band;
            c.present = false;
            place(i, off_screen, c.y, scene);
            continue;
        }
        c.top = c.y = slot * band_height;
        c.bottom = band_height - 15 + c.top;
        if (known) {
            if (++next_kind > 4) next_kind = 0;
            c.present = false;
            for (int j = 0; j < 5 && !c.present; ++j) {
                int k = next_kind + j;
                if (k > 4) k -= 4; // (the original's wrap; kind 0 is not reached this way)
                if (kinds[std::size_t(k)] != 0) {
                    --kinds[std::size_t(k)];
                    c.present = true;
                    c.x = (random_b_.next() & 0xFF) + 0x20;
                    place(i, off_screen, c.top, scene);
                    set_kind(i, k);
                    choose_course(i);
                    choose_depth(i);
                }
            }
        } else {
            c.present = (random_a_.next() & 0xFF) < start_chance;
            if (c.present) {
                c.x = (random_b_.next() & 0xFF) + 0x20;
                place(i, off_screen, c.top, scene);
                set_kind(i, choose_kind(scene.stage));
                choose_course(i);
                choose_depth(i);
            }
        }
    }
}

// $206ED8: the kind's images into the sprite, unmasked.
void Creatures::set_kind(int i, int kind) {
    auto& c = creatures_[std::size_t(i)];
    c.kind = kind;
    c.visible.fill(0xFFFF);
    c.visible_row = row_word(image_a(kind), 11, 0);
}

// $2076C0.
int Creatures::choose_kind(int stage) {
    if ((kind_mask_ & random_b_.next()) == 0) return 4;
    int kind = stage / 7;
    if (kind > 3) kind = random_c_.next() & 3;
    return kind;
}

// $207D02: one of eight directions, speeds 3-6 across and 1-4 down.
void Creatures::choose_course(int i) {
    auto& c = creatures_[std::size_t(i)];
    const int r = random_b_.next() & 7;
    c.dx = a_.s32(course_dx + std::uint32_t(r * 4));
    c.dy = a_.s32(course_dy + std::uint32_t(r * 4));
    c.speed_x = (random_c_.next() & 3) + 3;
    c.speed_y = (random_c_.next() >> 6 & 3) + 1;
}

// $207DA6: how deep in the view it flies, at most its limit.
void Creatures::choose_depth(int i) {
    auto& c = creatures_[std::size_t(i)];
    const int limit = a_.s32(depth_limits + std::uint32_t(i * 4));
    if (limit < (random_b_.next() >> 3 & 3)) c.depth = limit;
    else c.depth = random_b_.next() >> 3 & 3;
}

// $206DD4: the sprite moved to (x, y); when that is on screen and the
// creature flies behind some rows, its image is rebuilt masked by what those
// rows cover ($203384, the minterms at $2039A0; a copy of the view's own
// drawing, so close but not pixel-exact).
void Creatures::place(int i, int x, int y, const Scene& scene) {
    auto& c = creatures_[std::size_t(i)];
    c.shown_x = x;
    c.shown_y = y;
    if (x <= 0 || c.depth <= 0 || c.kind < 0 || scene.masks == nullptr) return;
    x = std::clamp(x, 0, 319);
    y = std::clamp(y, 0, 199);
    for (int r = 0; r < 14; ++r) {
        std::uint16_t visible = 0;
        for (int col = 0; col < 16; ++col) {
            bool covered = false;
            const int px = x + col, py = y + r;
            if (px < 320 && py < 200)
                for (int d = 0; d < c.depth; ++d)
                    covered |= (*scene.masks)[std::size_t(d)][std::size_t(py * 320 + px)] != 0;
            visible = std::uint16_t(visible << 1 | (covered ? 0 : 1));
        }
        c.visible[std::size_t(r)] = visible;
    }
    c.visible_row = row_word(image_a(c.kind), 11, 0) & c.visible[11];
}

// $2079BE: a free creature may appear (from either side), or flies on: a new
// course now and then, a chance to attach when visible and at least 60 away
// from the walker,
// bouncing off the sides and its band's top and bottom.
void Creatures::move_free(int i, const Scene& scene) {
    auto& c = creatures_[std::size_t(i)];
    if (!c.present) {
        if (c.enabled && int(random_a_.next() & 0xFFF) < appear_chance_) {
            c.present = true;
            c.y = c.top;
            c.x = (random_c_.next() & 1) == 0 ? 0x140 : -16;
            set_kind(i, choose_kind(scene.stage));
            choose_course(i);
            choose_depth(i);
        }
        return;
    }
    if ((random_a_.next() & 0xF) == 0) choose_course(i);
    c.x += c.dx * c.speed_x;
    c.y += c.dy * c.speed_y;
    if (attached_ < 3) {
        const int r = random_b_.next();
        if ((a_.u16(attach_masks + std::uint32_t(attached_ * 2)) & (r ^ i)) == 0 && c.x > 0 && c.x < 0x12F &&
            (c.visible_row & 0x7E0) != 0 && !near(scene.walker_x, scene.walker_y, c.x, c.y, 0x3C)) {
            c.tether_x = c.x;
            c.tether_y = c.y;
            ++attached_;
        }
    }
    if (c.x < 1) c.dx = 1;
    else if (c.x > 0x13E) c.dx = -1;
    if (c.top < c.y) {
        if (c.bottom <= c.y) {
            c.dy = -1;
            c.speed_y = 1;
            c.y = c.bottom;
        }
    } else {
        c.dy = 1;
        c.speed_y = 1;
        c.y = c.top;
    }
    place(i, c.x, c.y, scene);
}

void Creatures::hasten() {
    timer_ = std::max(timer_, 3);
    tether_clock_ = std::max(tether_clock_, 6);
}

void Creatures::zap(int x, int y, const Scene& scene) {
    const int speed = a_.s32(follow_speeds + std::uint32_t(scene.stage / 7 * 4)); // $2083B2
    for (int i = 0; i < 12; ++i) {
        auto& c = creatures_[std::size_t(i)];
        if (!c.present || c.kind == 4 || !near(x, y, c.x + 7, c.y + 7, 0x14)) continue;
        c.present = false;
        if (c.tether_x == -1) {
            score_bonus_ += speed * 3;
        } else {
            c.tether_x = -1;
            --attached_;
            score_bonus_ += speed * 5;
        }
        place(i, off_screen, c.top, scene);
        return;
    }
}

// $207810: the tether's end creeps towards the walker's head.
bool Creatures::follow(int i, const Scene& scene, int head_y) {
    auto& c = creatures_[std::size_t(i)];
    const int s = a_.s32(follow_speeds + std::uint32_t(scene.stage / 7 * 4));
    c.tether_x = std::clamp(c.tether_x + (scene.walker_x <= c.tether_x ? -s : s), 0, 319);
    c.tether_y = std::clamp(c.tether_y + (head_y <= c.tether_y ? -s : s), 0, 199);
    draw_tether(i);
    if (!near(scene.walker_x, head_y, c.tether_x, c.tether_y, 9)) return false;
    attached_ = 0;
    for (auto& other : creatures_) other.tether_x = -1;
    return true;
}

// $20794E / $2061BA: a filled triangle from the creature to the tether's end
// (8 either side) into the tether layer (display rows: drawing + 10).
void Creatures::draw_tether(int i) {
    const auto& c = creatures_[std::size_t(i)];
    const int x1 = std::clamp(c.x + 7, 0, 319), y1 = std::clamp(std::clamp(c.y + 7, 0, 199) + 10, 0, 199);
    const int y2 = std::clamp(c.tether_y + 10, 0, 199);
    const int left = std::clamp(c.tether_x - 8, 0, 319), right = std::clamp(c.tether_x + 8, 0, 319);
    const int ya = std::min(y1, y2), yb = std::max(y1, y2);
    for (int y = ya; y <= yb; ++y) {
        // Edges from the apex (x1, y1) to (left, y2) and (right, y2).
        const int lx = y2 == y1 ? left : x1 + (left - x1) * (y - y1) / (y2 - y1);
        const int rx = y2 == y1 ? right : x1 + (right - x1) * (y - y1) / (y2 - y1);
        for (int x = std::min(lx, rx); x <= std::max(lx, rx); ++x) tethers_[std::size_t(y * 320 + x)] = 1;
    }
    tethers_drawn_ = true;
}

void Creatures::clear_tethers() {
    std::fill(tethers_.begin(), tethers_.end(), 0);
    tethers_drawn_ = true;
}

void Creatures::tick_tethers() {
    if (!tethers_drawn_) return;
    if (++tether_clock_ > (tether_round_ & 3) + 5) {
        tether_clock_ = 0;
        ++tether_round_;
        tethers_drawn_ = false;
        std::fill(tethers_.begin() + 10 * 320, tethers_.end(), 0); // $200E62
        if (attached_ != 0 && !quiet_)
            for (int i = 0; i < 12; ++i)
                if (creatures_[std::size_t(i)].tether_x != -1) draw_tether(i);
    }
}

bool Creatures::update(const Scene& scene) {
    if (++timer_ <= 4 || quiet_) return false;
    timer_ = -attached_;
    const int head_y = std::clamp(scene.walker_y - scene.head, 0, 199);
    bool caught = false;
    for (int i = 0; i < 12; ++i) {
        if (creatures_[std::size_t(i)].tether_x == -1) move_free(i, scene);
        else caught |= follow(i, scene, head_y);
    }
    return caught;
}

} // namespace mindwalker::landscape
