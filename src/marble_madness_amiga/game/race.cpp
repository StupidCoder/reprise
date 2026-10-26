#include "marble_madness_amiga/game/race.hpp"
#include <algorithm>
#include "amiga/byte_run1.hpp"
#include <cstdlib>

namespace marble_madness::game {
namespace {

std::int16_t s16(std::int32_t value) { return std::int16_t(value); }
std::int32_t abs32(std::int32_t value) { return value < 0 ? -value : value; }

// The view's scroll rules ($102379-$102382): speeds and the screen heights
// (from the bottom) between which the marble may be without scrolling.
constexpr std::uint32_t course_music = 0x10210C;      // longs: sound entry per course
constexpr std::uint32_t scroll_speeds = 0x102379;     // bytes: slow, medium, fast
constexpr std::uint32_t scroll_top = 0x10237C;        // word
constexpr std::uint32_t scroll_bottom = 0x10237E;     // word
constexpr std::uint32_t scroll_medium_gap = 0x102380; // word
constexpr std::uint32_t scroll_fast_gap = 0x102382;   // word
constexpr std::uint32_t race_times = 0x102350;        // per difficulty (0-7): seconds per course

} // namespace

Race::Race(const data::GameFiles& files, data::Course course, int players, int difficulty)
    : files_(&files), course_(course), players_(players), terrain_(files, course), regions_(files, course) {
    const auto& t = files.track(course);
    const std::uint32_t header = files.track_header(course);
    descriptor_ = t.u32(header);
    respawn_points_ = t.u32(header + 4);
    zones_ = t.u32(header + 8);
    marble_data_ = files.marble_data().first_filled_hunk();
    view_ = terrain_.view_offset();
    first_view_ = t.s16(descriptor_ + 0x10);
    regions_.view_scroll = std::int16_t(view_ - first_view_);
    regions_.view = view_;

    // $10D47A: the scroll limit from the tile map's rows, then the regions
    // of the first screen.
    scroll_limit_ = s16((files.map_rows(course) - 0x19) * 8);
    scroll_ = 0;
    if (course == data::Course::silly) {
        scroll_ = s16(scroll_limit_ - 1);
        regions_.bands_changed(band(view_ + 0x19), band(view_));
    } else {
        regions_.bands_changed(band(view_ - 0x19), band(view_));
    }

    // $1030B4: one region update before the marbles are placed (it lays
    // down the start area's animated ground).
    regions_.update(terrain_);

    // $114AA2: the marbles on their start cells (descriptor +$14, a word
    // per player: x cell, y cell).
    for (int i = 0; i < players; ++i) {
        Marble& m = marbles[std::size_t(i)];
        m.player = std::uint8_t(i);
        const std::uint16_t start = t.u16(descriptor_ + 0x14 + std::uint32_t(i) * 2);
        m.x = (start >> 8 & 0xFF) * 0x80000 + 0x40000;
        m.y = (start & 0xFF) * 0x80000 + 0x40000;
        probe_.invalidate();
        locate(m);
        m.z = probe_.ground_height();
        m.status = 1;
        m.mode = Marble::rolling;
        m.tenths = 9;
        reset_shine(m);
        project(m, false);
        rotate_shine(m);
        check_visible(m);
        // $106A64: the course's time for the difficulty setting, then the
        // clock starts ($1030B4 sets the tenth counter going).
        m.time = std::int16_t(files.program().u8(files.program().u32(race_times + std::uint32_t(difficulty) * 4) +
                                                 std::uint32_t(course)));
        m.timer_frames = 5;
    }
    // $1030B4: the course's music.
    sound(int(files.program().u32(course_music + std::uint32_t(course) * 4)));
}

int Race::band(std::int32_t view) const {
    // $10D9BC: 16-line bands counted from the course's first view.
    return std::int8_t((view - terrain_.view_offset()) >> 4);
}

void Race::update(const std::array<Push, 2>& pushes) {
    push_ = pushes;
    scroll();
    for (int i = 0; i < players_; ++i) {
        Marble& m = marbles[std::size_t(i)];
        regions_.racers[std::size_t(i)] = {m.status == 1, m.x, m.y, m.feature};
    }
    regions_.racer_count = players_;
    // $113F72: every marble in the race.
    for (int i = 0; i < players_; ++i) {
        Marble& m = marbles[std::size_t(i)];
        if (m.status != 0) {
            update_marble(m);
        }
    }
    if (course_ == data::Course::practice) {
        project_drop_marks();
    }
    regions_.update(terrain_);
    count_down();
    ++frame_;
}

// $10B7E6 / $10B790: the race clocks. A tenth takes three updates, a
// second ten tenths.
void Race::count_down() {
    for (int i = 0; i < players_; ++i) {
        Marble& m = marbles[std::size_t(i)];
        if (m.mode == Marble::dizzy || m.timer_frames == -1 || cheats.stop_timer) {
            continue;
        }
        m.timer_frames = std::int8_t(m.timer_frames - 2);
        if (m.timer_frames >= 0) {
            continue;
        }
        m.timer_frames = 5;
        if (--m.tenths >= 0) {
            continue;
        }
        m.tenths = 9;
        if (--m.time == -1) {
            // Out of time.
            m.status = 2;
            m.tenths = 0;
            m.time = 0;
            m.timer_frames = -1;
            game_mode = 4;
        }
    }
}

// $10D618: the view follows the lowest racing marble (highest on Silly),
// checked every 8th update unless already scrolling.
void Race::scroll() {
    const auto& p = files_->program();
    if ((frame_ & 7) != 0 && !scrolling_) {
        return;
    }
    const bool silly = course_ == data::Course::silly;
    const std::int16_t none = silly ? -0x7000 : 0x7000;
    std::int16_t target = none;
    for (int i = 0; i < players_; ++i) {
        const Marble& m = marbles[std::size_t(i)];
        const std::int16_t screen_y = s16(m.screen[0]);
        const bool counts = m.status == 1 && (m.air != 2 || m.vz > 0) &&
                            (m.mode == 0 || m.mode == 1 || m.mode == 5 || m.mode == 4 || m.mode == 3);
        if (counts) {
            target = silly ? std::max(target, screen_y) : std::min(target, screen_y);
        }
    }
    const std::int16_t top = p.s16(scroll_top), bottom = p.s16(scroll_bottom);
    const std::int16_t medium = p.s16(scroll_medium_gap), fast = p.s16(scroll_fast_gap);
    const std::int8_t slow_speed = p.s8(scroll_speeds), medium_speed = p.s8(scroll_speeds + 1),
                      fast_speed = p.s8(scroll_speeds + 2);
    if (target == none) {
        if (!scrolling_) {
            return;
        }
        if (scroll_goal_ == 0) {
            std::int16_t gap = 0;
            if (scroll_speed_ == fast_speed) {
                gap = fast;
            } else if (scroll_speed_ == medium_speed) {
                gap = medium;
            }
            scroll_goal_ = scroll_direction_ == 1 ? s16(top - gap) : s16(gap + bottom);
        }
        target = scroll_goal_;
    } else {
        scroll_goal_ = 0;
    }
    std::int8_t direction = 0;
    if ((target < top && target != none) || (scrolling_ && scroll_direction_ == 1)) {
        if (target < top - fast) {
            scroll_speed_ = fast_speed;
        } else if (target < top - medium) {
            scroll_speed_ = medium_speed;
        } else if (top < target) {
            scroll_speed_ = slow_speed;
        }
        direction = 1;
        scrolling_ = scroll_down(scroll_speed_);
    } else if ((bottom < target && target != none) || (scrolling_ && scroll_direction_ == -1)) {
        if (fast + bottom < target) {
            scroll_speed_ = fast_speed;
        } else if (medium + bottom < target) {
            scroll_speed_ = medium_speed;
        } else if (target < bottom) {
            scroll_speed_ = slow_speed;
        }
        direction = -1;
        scrolling_ = scroll_up(scroll_speed_);
    }
    if (scrolling_) {
        if (scroll_goal_ != 0) {
            scroll_goal_ = scroll_direction_ == 1 ? s16(scroll_speed_ + scroll_goal_) : s16(scroll_goal_ - scroll_speed_);
        }
        scroll_direction_ = direction;
    }
    if (target != none && ((top + 0x10 < target && scroll_direction_ == 1) ||
                           (target < bottom - 0x10 && scroll_direction_ == -1))) {
        scrolling_ = false;
    }
}

// $10D278: the view moves down the course by `lines` (it may pass the
// limit by up to a step).
bool Race::scroll_down(int lines) {
    if (scroll_limit_ <= scroll_) {
        return false;
    }
    scroll_ = s16(scroll_ + lines);
    const auto before = view_;
    view_ += lines;
    regions_.view_scroll = s16(s16(view_) - first_view_);
    regions_.view = view_;
    regions_.bands_changed(band(before), band(view_));
    return true;
}

// $10D384: and back up.
bool Race::scroll_up(int lines) {
    if (scroll_ < 1) {
        return false;
    }
    scroll_ = s16(scroll_ - lines);
    const auto before = view_;
    view_ -= lines;
    regions_.view_scroll = s16(s16(view_) - first_view_);
    regions_.view = view_;
    regions_.bands_changed(band(before), band(view_));
    return true;
}

// $114402: one update of a marble, by its mode.
void Race::update_marble(Marble& m) {
    const auto mode = m.mode;
    m.sparkle_count = 0; // shown for one update ($105ECC clears +$EB)
    if (m.bonus_on && mode != 2 && mode != 4 && mode != 10 && mode != 11 && mode != 7) {
        // The points picture over the marble bobs, then goes.
        m.bonus_wobble = std::int8_t(m.bonus_wobble_step + m.bonus_wobble);
        if (m.bonus_wobble < 3) {
            if (m.bonus_wobble < -0x16) {
                m.bonus_wobble = std::int8_t(0xEA);
                m.bonus_wobble_step = 1;
            }
        } else {
            m.bonus_wobble = 2;
            m.bonus_wobble_step = -1;
        }
        if ((m.bonus_wobble & 1) != 0 && std::int8_t(++m.bonus_time) > 0x28) {
            m.bonus_time = 0xFF;
            m.bonus_on = 0;
        }
    }
    switch (mode) {
    case Marble::rolling:
        if (m.stunned == 0 || m.air != 0 ||
            (m.region_code != 0 && m.region_code != 0x35 && m.region_code != 0x34)) {
            save_grounded(m);
            push(m);
            roll(m);
        } else {
            m.bounce = 9;
            m.delay = 10;
            m.stunned = 0;
            m.mode = Marble::dizzy;
            m.dizzy_animation = files_->marble_data().u32(marble_data_ + 0x18);
            m.dizzy_tick = 0;
            sound(0x16);
            m.dizzy_shown = 1;
        }
        break;
    case Marble::landing:
        animate(m);
        save_grounded(m);
        push(m);
        roll(m);
        if (m.mode == Marble::landing) {
            if (m.delay != 0) {
                --m.delay;
            }
            if (m.delay < std::int8_t(m.bounce)) {
                if (--m.bounce == 0) {
                    sound_stop();
                    m.mode = Marble::rolling;
                    m.animation = 0;
                    rotate_shine(m);
                }
            } else {
                ++m.bounce;
            }
        }
        break;
    case Marble::breaking:
        animate(m);
        project(m, true);
        if (m.sprite_flag != 0) {
            check_visible(m);
        }
        break;
    case Marble::recoiling:
        project(m, true);
        rotate_shine(m);
        check_visible(m);
        if (m.region_code != 0 && --m.region_timer == 0) {
            // What threw the marble decides the bonus.
            const auto code = m.region_code;
            if (code == 0x12) {
                award(m, 9);
            } else if (code == 0x13 || code == 0x14 || code == 0x20 || code == 0x22 || code == 0x25) {
                award(m, 5);
            } else if (code == 10) {
                sound(0xB, true);
            }
            m.region_code = 0;
            m.screen[1] = m.screen[2] = 0;
            m.mode = Marble::rolling;
        }
        break;
    case Marble::respawning: {
        project(m, true);
        bool done = appear(m);
        if (done || m.delay < 2) {
            if (occupied(&m, s16(m.x >> 16), s16(m.y >> 16), 0x70) != 0) {
                m.delay = 0x65;
                set_mode(m, Marble::respawning);
                done = false;
            }
            if (done) {
                m.vx = m.vy = 0;
                m.mode = Marble::rolling;
            }
        }
        break;
    }
    case Marble::bumped:
        animate(m);
        save_grounded(m);
        push(m);
        roll(m);
        if (m.mode == Marble::bumped) {
            if (m.bounce != 0) {
                --m.bounce;
            }
            if (m.bounce == 0) {
                sound_stop();
                m.mode = Marble::rolling;
                m.animation = 0;
                rotate_shine(m);
            }
        }
        break;
    case Marble::finishing:
        roll(m);
        if (--m.delay == 0 && m.status != 2) {
            m.status = 3;
            m.vx = m.vy = 0;
        }
        break;
    case Marble::frozen:
        save_grounded(m);
        m.sprite_flag = 0;
        break;
    case Marble::dizzy:
        if (m.bounce != 0) {
            --m.bounce;
        }
        if (m.bounce == 0) {
            m.bounce = 9;
            ++m.time;
            --m.delay;
        }
        if (m.delay == 0) {
            m.dizzy_shown = 0;
            m.mode = Marble::rolling;
            award(m, 0x10);
        } else if (++m.dizzy_tick == 2) {
            m.dizzy_tick = 0;
            m.dizzy_animation += 4;
        }
        project(m, true);
        rotate_shine(m);
        check_visible(m);
        break;
    case Marble::appearing:
        // (On later courses $11C34C first draws the marble towards a
        // creature; not rebuilt yet.)
        animate(m);
        project(m, true);
        check_visible(m);
        break;
    default:
        // Modes 10 and 11 (traps on later courses) are not rebuilt yet.
        project(m, true);
        break;
    }
}

// $11573A: on Practice, a landing in zone 1, 2 or 3 (once a game per
// player) marks the spot and gives a bonus by how far down the course it
// is: zones 1 and 2 by y past $1E8 and $238, zone 3 by x past $210, a
// quarter of that plus 7 (at most 13; bonuses 7-13 of marbdat's table).
void Race::drop_bonus(Marble& m) {
    const auto zone = m.feature;
    const auto bit = std::uint8_t(m.player + 1);
    if (zone < 1 || zone > 3 || (drop_bonus_taken_ & bit) != 0) {
        return;
    }
    drop_bonus_taken_ |= bit;
    int distance = zone == 1 ? probe_.y() - 0x1E8 : zone == 2 ? probe_.y() - 0x238 : probe_.x() - 0x210;
    auto& mark = drop_marks_[std::size_t(m.player & 1)];
    mark = {probe_.x(), probe_.y(), probe_.z(), 0, std::int8_t(zone == 3 ? 1 : 0)};
    project(mark);
    distance = s16(distance);
    if (distance >= 0) {
        distance >>= 2;
    }
    distance = std::min(distance + 7, 0xD);
    if (distance > 6) {
        award(m, distance);
    }
}

// $11DAAC: the markers follow the view.
void Race::project_drop_marks() {
    for (int i = 0; i < players_; ++i) {
        if (drop_marks_[std::size_t(i)].kind != -1) {
            project(drop_marks_[std::size_t(i)]);
        }
    }
}

// $106A00: a marker's screen position, as a marble's.
void Race::project(DropMark& mark) {
    probe_.set_point(mark.x, mark.y);
    const auto screen_x = s16(mark.y - mark.x + 0x88);
    const auto screen_y = s16(s16(view_) + mark.z + 0x6C - s16((mark.x + mark.y) >> 1));
    mark.screen = std::int32_t(std::uint32_t(std::uint16_t(screen_x)) << 16 | std::uint16_t(screen_y));
}

// $115470: a rolling marble's step: move, walls, regions, zones, ground,
// falling and landing, friction and the slope.
void Race::roll(Marble& m) {
    start_x_ = m.x;
    start_y_ = m.y;
    start_z_ = m.z;
    start_cell_x_ = s16(start_x_ >> 19);
    start_cell_y_ = s16(start_y_ >> 19);
    const bool player = is_player(m);
    probe_.invalidate();
    locate(m);
    if (probe_.ground_height() - m.z > 0x100000) {
        // Below the ground (a lift or a closing gap pushed it under).
        if (player) {
            sound_stop();
            sound(0xC);
            m.delay = 0x65;
            set_mode(m, Marble::respawning);
        }
        return;
    }
    m.x += m.vx;
    m.y += m.vy;
    m.z += m.vz;
    locate(m);
    probe_.analyse_sides();
    // Keep inside the course's screen width: near the left or right edge
    // the marble may only move along the edge.
    const std::int32_t vx = m.vx, vy = m.vy;
    const std::int16_t screen_x = s16(probe_.y() - probe_.x() + 0x88);
    if ((screen_x < 8 && vy < vx) || (screen_x > 0x118 && vx < vy)) {
        m.vx = vy;
        const std::int32_t mean = (vy + vx) >> 1;
        m.x = mean + start_x_;
        m.y = mean + start_y_;
        locate(m);
    }
    bounce_x_ = bounce_y_ = false;
    bounce_off_edges(m);
    if (bounce_x_ || bounce_y_) {
        locate(m);
    }
    bounce_x_ = bounce_y_ = false;
    touch_regions(m);
    if (m.region_code == 10 || m.mode == Marble::respawning) {
        return;
    }
    if (bounce_x_ || bounce_y_) {
        locate(m);
    }
    bounce_x_ = bounce_y_ = false;
    // Not yet rebuilt: collisions with the other marble and with creatures
    // ($1169C4, $11AF24, $11C1C0, $11E1E0, $11CA44, $11D8EA).
    if (player) {
        if (m.mode == Marble::settling) {
            return;
        }
        track_zones(m);
    }
    const std::int32_t ground = probe_.ground_height();
    keep_on_ground(m, ground);
    const bool positional = probe_.x() < probe_.y();
    if (abs32(m.z - start_z_) > 0x80000 && m.region_code == 0 && m.vz < 1) {
        m.air = 2;
        m.edge_cell_x = probe_.cell_x();
        m.edge_cell_y = probe_.cell_y();
        m.vz = -0x6000;
        sound(0xB, positional);
    }
    if (m.air == 2) {
        if (ground < m.z) {
            // (Cheat: a long drop onto ground lands; into nothing it is
            // still lost.)
            if (m.fall_start_z - m.z > 0x800000 && !(cheats.safe_falls && ground > 0)) {
                // Fell too far: lost.
                if (player) {
                    sound_stop();
                    sound(0xC);
                    m.delay = 100;
                    set_mode(m, Marble::respawning);
                }
                return;
            }
        } else {
            m.air = 0;
            m.vz = 0;
            const std::int32_t drop = abs32(m.fall_start_z - ground);
            m.z = ground;
            land(m, s16(drop >> 15));
            sound(0xC);
            sound(0xD, positional);
            if (course_ == data::Course::aerial && m.feature == 4) {
                sound(0x1F);
            }
            if (course_ == data::Course::practice) {
                drop_bonus(m);
            }
        }
    } else if (m.air == 0) {
        m.z = ground;
    }
    project(m, false);
    if (player && m.mode != Marble::landing && m.mode != Marble::bumped) {
        rotate_shine(m);
    }
    check_visible(m);
    push_off_walls(m);
    if (m.air == 0) {
        const auto code = m.region_code;
        int kind = 0;
        if (code == 0x2D || code == 0x2E || code == 0x3B) {
            kind = 2;
        } else if (code >= 0x38 && code <= 0x3A) {
            kind = 4;
        } else if (code >= 0x2F && code <= 0x31) {
            kind = 3;
        } else if (code == 0x10 || code == 0x17 || code == 0x18 || (code > 0x31 && code < 0x38)) {
            kind = -1;
        }
        if (kind != -1) {
            friction(m, kind);
        }
        if (player && game_mode != 1) {
            slope_force(m);
        }
    }
}

// $116EE0: what the running regions do to the marble, by terrain code.
// Only the codes Practice uses are rebuilt so far (5, 17, the rails 26-31).
void Race::touch_regions(Marble& m) {
    const std::int16_t px = probe_.x(), py = probe_.y();
    const bool positional = px <= py;
    const std::int32_t vx = m.vx, vy = m.vy;
    const auto before = m.region_code;
    m.region_code = 0;
    for (const auto& r : regions_.all()) {
        if (!r.active) {
            continue;
        }
        const std::int16_t dx = s16(s16(r.x >> 16) - px);
        const std::int16_t dy = s16(s16(r.y >> 16) - py);
        bool hit = false;
        switch (r.code) {
        case 5: { // a post: bounce off within 7 units
            std::uint16_t ax = std::uint16_t(std::abs(dx)), ay = std::uint16_t(std::abs(dy));
            const std::uint16_t d = std::uint16_t(ay * 16) < std::uint16_t(ax * 16)
                                        ? std::uint16_t((ay & 0xFFF) * 6 + ax * 16)
                                        : std::uint16_t((ax & 0xFFF) * 6 + ay * 16);
            if (d < 0x38) {
                bounce_x_ = bounce_y_ = true;
                sound(7, positional);
            }
            break;
        }
        // Rails: a bar along one axis from the region's point; crossing it
        // reverses the velocity across the bar, and near its ends (only when
        // moving outwards) along it too.
        case 0x1A:
            if (dy > -8 && dy < 0x20 && dx > -0xC && dx < 0) {
                if ((dy > -8 && dy < 0 && vy < 0) || (dy > 0x18 && dy < 0x20 && vy > 0)) bounce_y_ = true;
                bounce_x_ = true;
                sound(0x22, positional);
            }
            break;
        case 0x1B:
            if (dx > -8 && dx < 0x20 && dy > -0xC && dy < 0) {
                if ((dx > -8 && dx < 0 && vx < 0) || (dx > 0x18 && dx < 0x20 && vx > 0)) bounce_x_ = true;
                bounce_y_ = true;
                sound(0x22, positional);
            }
            break;
        case 0x1C:
            if (dx > -8 && dx < 0x20 && dy > -8 && dy < 4) {
                if ((dx > -8 && dx < 0 && vx < 0) || (dx > 0x18 && dx < 0x20 && vx > 0)) bounce_x_ = true;
                bounce_y_ = true;
                sound(0x22, positional);
            }
            break;
        case 0x1D:
            if (dy > -8 && dy < 0x20 && dx > -8 && dx < 4) {
                if ((dy > -8 && dy < 0 && vy < 0) || (dy > 0x18 && dy < 0x20 && vy > 0)) bounce_y_ = true;
                bounce_x_ = true;
                sound(0x22, positional);
            }
            break;
        case 0x1E:
            if (dx > -0xC && dx < 0x5E && dy > -0xE && dy < 0) {
                if ((dx > -0xC && dx < -4 && vx < 0) || (dx > 0x56 && dx < 0x5E && vx > 0)) bounce_x_ = true;
                bounce_y_ = true;
                sound(0x22, positional);
            }
            break;
        case 0x1F:
            if (dy > -0xC && dy < 0x54 && dx > -0xE && dx < 0) {
                if ((dy > -0xC && dy < -4 && vy < 0) || (dy > 0x4C && dy < 0x54 && vy > 0)) bounce_y_ = true;
                bounce_x_ = true;
                sound(0x22, positional);
            }
            break;
        default:
            // Codes 6-9, 14, 15, 17, 25, 40-44 do nothing here; the others
            // belong to later courses and are not rebuilt yet.
            break;
        }
        if (hit) {
            break;
        }
    }
    if (m.region_code != before && (before == 0x10 || (before >= 0x32 && before <= 0x37))) {
        sound(10);
    }
    if (bounce_x_) {
        m.x = start_x_;
        m.vx = -m.vx;
    }
    if (bounce_y_) {
        m.y = start_y_;
        m.vy = -m.vy;
    }
}

} // namespace marble_madness::game
