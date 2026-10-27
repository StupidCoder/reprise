#include "marble_madness_amiga/game/race.hpp"
#include <algorithm>
#include "amiga/byte_run1.hpp"
#include <cstdlib>

namespace marble_madness::game {
namespace {

std::int16_t s16(std::int32_t value) { return std::int16_t(value); }
std::int32_t abs32(std::int32_t value) { return value < 0 ? -value : value; }

// The game's octagonal length of (a, b) in sixteenths: the longer side plus
// three eighths of the shorter (16-bit arithmetic), read as a signed word.
std::int16_t octagonal_length(std::int16_t a, std::int16_t b) {
    const auto ua = std::uint16_t((a < 0 ? -a : a) << 4), ub = std::uint16_t((b < 0 ? -b : b) << 4);
    return std::int16_t(ua > ub ? std::uint16_t((ub >> 3) * 3 + ua) : std::uint16_t((ua >> 3) * 3 + ub));
}

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

Race::Race(const data::GameFiles& files, data::Course course, int players, int difficulty, const Random* seed)
    : files_(&files), course_(course), players_(players), terrain_(files, course), regions_(files, course) {
    const auto& t = files.track(course);
    const std::uint32_t header = files.track_header(course);
    descriptor_ = t.u32(header);
    respawn_points_ = t.u32(header + 4);
    zones_ = t.u32(header + 8);
    marble_data_ = files.marble_data().first_filled_hunk();
    random = seed != nullptr ? *seed : Random();
    for (std::size_t i = 0; i < acids.size(); ++i) {
        acids[i].index = std::uint8_t(i);
    }
    for (std::size_t i = 0; i < critters.size(); ++i) {
        critters[i].index = std::uint8_t(i);
    }
    for (std::size_t i = 0; i < birds.size(); ++i) {
        birds[i].index = std::uint8_t(i);
    }
    // The course start ($102D06) sets Ultimate's colours cycling.
    if (course == data::Course::ultimate) {
        colour_count_ = 0;
    }
    if (course == data::Course::aerial) {
        // The piston groups as the Track holds them (the program changes them
        // in place; the Track is loaded anew for each race).
        const std::uint32_t groups = t.u32(header + 0x14) + 0x23C;
        for (std::uint32_t g = 0; g < piston_groups.size(); ++g) {
            const std::uint32_t at = groups + g * 0x14;
            auto& group = piston_groups[g];
            group.picture = t.u32(at);
            group.frame = t.u32(at + 4);
            group.timer = t.s16(at + 8);
            group.x = t.s16(at + 0xA);
            group.line = t.s16(at + 0xC);
            group.column = t.s16(at + 0xE);
            group.row = t.s16(at + 0x10);
            group.pattern = t.s16(at + 0x12);
        }
    }
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
        bands_changed(band(view_ + 0x19), band(view_));
    } else {
        bands_changed(band(view_ - 0x19), band(view_));
    }

    // $1030B4: one region update before the marbles are placed (it lays
    // down the start area's animated ground).
    regions_.update(terrain_, probe_);
    take_region_display_events();
    take_region_sounds();

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
        show(1, m.player);
        // $106A64: the course's time for the difficulty setting, then the
        // clock starts ($1030B4 sets the tenth counter going).
        m.time = std::int16_t(files.program().u8(files.program().u32(race_times + std::uint32_t(difficulty) * 4) +
                                                 std::uint32_t(course)));
        m.timer_frames = 5;
    }
    // $1030B4: the course's music, the roll for the dizzy surprise; the
    // display list's first sort ($103136).
    sound(int(files.program().u32(course_music + std::uint32_t(course) * 4)));
    surprise_armed_ = true;
    surprise_roll_ = std::uint8_t(random.next(0x100));
    display_.sort([this](std::uint8_t type, std::uint8_t index) { return box_of(type, index); });
}

void Race::show(std::uint8_t type, std::uint8_t index) {
    display_.add(type, index, [this](std::uint8_t t, std::uint8_t i) { return box_of(t, i); });
}

void Race::hide(std::uint8_t type, std::uint8_t index) {
    display_.remove(type, index);
}

void Race::take_region_display_events() {
    for (const auto& e : regions_.take_display_events()) {
        if (e.add) {
            show(e.type, e.index);
        } else {
            hide(e.type, e.index);
        }
    }
}

// $110CD4: a display object's box in course space (from x, y, z towards
// the viewer): marbles 6 x 6 x 6 from 3 before their point; slinkies, acid and
// region sprites by their frame record's box (+4: x, y offsets, width,
// depth) and a height by kind.
DisplayList::Box Race::box_of(std::uint8_t type, std::uint8_t index) const {
    const auto& t = files_->track(course_);
    auto make = [](int x, int y, int z, int w, int d, int h) {
        return DisplayList::Box{std::int16_t(x), std::int16_t(y), std::int16_t(z),
                                std::int16_t(w + x), std::int16_t(d + y), std::int16_t(h + z)};
    };
    auto hi = [](std::int32_t v) { return int(std::int16_t(v >> 16)); };
    switch (type) {
    case 0:
        return make(0x7FFF, 0x7FFF, 0x7FFF, 0, 0, 0);
    case 1:
    case 2: {
        const Marble& m = type == 1 ? marbles[index % max_marbles] : steelies[index & 1].body;
        return make(hi(m.x) - 3, hi(m.y) - 3, hi(m.z) + 1, 6, 6, 6);
    }
    case 4: {
        const Slinky& s = slinkies[index & 3];
        const std::uint32_t record = t.contains(s.frame, 4) ? t.u32(s.frame) : 0xFFFFFFFF;
        if (record == 0xFFFFFFFF || !t.contains(record, 8)) {
            return make(hi(s.x) - 4, hi(s.y) - 4, hi(s.z), 8, 8, 0x10);
        }
        return make(t.s8(record + 4) + hi(s.x), t.s8(record + 5) + hi(s.y), hi(s.z), t.s8(record + 6),
                    t.s8(record + 7), 0x10);
    }
    case 0x2A: {
        const auto& d = drop_marks_[index & 1];
        return make(d.x, d.y, d.z - 8, 0, 0, 0);
    }
    case 0xE: {
        const Acid& a = acids[index % acids.size()];
        const std::uint32_t record = t.contains(a.frame, 4) ? t.u32(a.frame) : 0xFFFFFFFF;
        if (record == 0xFFFFFFFF || !t.contains(record, 8)) {
            return make(hi(a.x) - 4, hi(a.y) - 4, hi(a.z) - 8, 8, 8, 0);
        }
        return make(t.s8(record + 4) + hi(a.x), t.s8(record + 5) + hi(a.y), hi(a.z) - 8, t.s8(record + 6),
                    t.s8(record + 7), 0);
    }
    case 7: case 8: case 9: {
        // Silly's critters: the frame record's box, flat and lower once caught.
        const Critter& c = critters[index % critters.size()];
        const std::uint32_t record = t.contains(c.animation, 4) ? t.u32(c.animation) : 0xFFFFFFFF;
        if (record == 0xFFFFFFFF || !t.contains(record, 8)) {
            return {};
        }
        const bool caught = c.state == Critter::caught;
        return make(t.s8(record + 4) + hi(c.x), t.s8(record + 5) + hi(c.y), hi(c.z) + (caught ? -8 : 0),
                    t.s8(record + 6), t.s8(record + 7), caught ? 0 : 6);
    }
    case 0xF: {
        const Bird& b = birds[index % birds.size()];
        if (b.state == Bird::gone) {
            return make(hi(b.x), hi(b.y), hi(b.z), 0, 0, 0);
        }
        return make(hi(b.x) - 4, hi(b.y) - 4, hi(b.z), 8, 8, 4);
    }
    case 0x29: case 0x2C:
        return {}; // never shown (Aerial's pistons) / the attract demo's
    default:
        break;
    }
    // Regions by their code.
    if (index >= Regions::slots) {
        return {};
    }
    const Region& r = regions_.all()[index];
    const std::uint32_t record = t.contains(r.frame, 4) ? t.u32(r.frame) : 0xFFFFFFFF;
    const bool none = record == 0xFFFFFFFF || !t.contains(record, 8);
    auto offset = [&](int k) { return frame_box_byte(r.frame, k); };
    int ox = 0, oy = 0, oz = 0, w = 0, d = 0, h = 0;
    switch (type) {
    case 3: ox = offset(4); oy = offset(5); w = offset(6); d = offset(7); oz = -0x10; h = 0x10; break;
    case 5: ox = offset(4); oy = offset(5); w = offset(6); d = offset(7); h = 0x18; break;
    case 6: oy = offset(4); d = offset(6); w = 0x20; h = 0x28; break;
    case 10: ox = offset(4); oy = offset(5); w = offset(6); d = offset(7); oz = -8; break;
    case 11: case 13:
        if (none) { w = d = 8; oz = -8; } else { ox = offset(4); oy = offset(5); w = offset(6); d = offset(7); oz = -8; h = 0x18; }
        break;
    case 12:
        if (none) { w = d = 8; oz = -8; } else { ox = offset(4); oy = offset(5); w = offset(6); d = offset(7); h = 0x10; }
        break;
    default:
        break;
    }
    return make(ox + hi(r.x), oy + hi(r.y), oz + hi(r.z), w, d, h);
}

// Byte k (4-7: x, y, width, depth) of the frame record a frame pointer
// points to. A region at the end of its frame list (record -1) reads
// addresses 3-6 instead (the sum wraps): the first vector's low byte and
// ExecBase's pointer, $00C002xx on the 512K-expanded A500 the recordings run
// on (Ultimate's second goal flag: the flags' drawing order; Aerial's
// bumpers: a box that holds nothing).
int Race::frame_box_byte(std::uint32_t frame, int k) const {
    constexpr std::array<std::int8_t, 4> low_memory = {0x00, 0x00, std::int8_t(0xC0), 0x02};
    const auto& t = files_->track(course_);
    const std::uint32_t record = t.contains(frame, 4) ? t.u32(frame) : 0xFFFFFFFF;
    if (record == 0xFFFFFFFF) {
        return low_memory[std::size_t(k - 4) & 3];
    }
    return t.contains(record, 8) ? t.s8(record + std::uint32_t(k)) : 0;
}

int Race::band(std::int32_t view) const {
    // $10D9BC: 16-line bands counted from the descriptor's first view (+$10;
    // on Silly without the +$12 the view starts from).
    return std::int8_t((view - first_view_) >> 4);
}

void Race::update(const std::array<Push, 2>& pushes) {
    push_[0] = pushes[0];
    push_[1] = pushes[1];
    // The crowd's drivers decide from what they see before the update.
    for (int i = players_; i < marble_count_; ++i) {
        steer_bot(marbles[std::size_t(i)]);
    }
    if (bot_drives_player && crowd_map_) {
        steer_bot(marbles[0]);
    }
    scroll();
    // The course's moving pieces react to the players' marbles alone (a
    // crowd always near would keep Beginner's drawbridge up for good).
    for (int i = 0; i < players_; ++i) {
        Marble& m = marbles[std::size_t(i)];
        regions_.racers[std::size_t(i)] = {m.status == 1, m.x, m.y, m.feature};
    }
    regions_.racer_count = players_;
    // $113F72: the dizzy surprise, then every marble in the race.
    dizzy_surprise();
    int finished = 0, out = 0;
    for (int i = 0; i < marble_count_; ++i) {
        Marble& m = marbles[std::size_t(i)];
        const bool bot = i >= players_;
        sound_source_ = bot ? i : -1;
        if (m.status != 0) {
            update_marble(m);
            if (bot) {
                carry_off_screen(m);
            }
        }
        sound_source_ = -1;
        if (crowd_ && (m.status == 3 || m.mode == Marble::finishing) &&
            std::find(home_order_.begin(), home_order_.end(), i) == home_order_.end()) {
            home_order_.push_back(i);
        }
        if (bot) {
            // A crowd marble home goes off the course.
            if (m.status == 3) {
                hide(1, m.player);
                m = Marble{};
                m.player = std::uint8_t(i); // (its slot: never another's push)
            }
            continue;
        }
        if (m.status == 0 || m.status == 2) {
            ++out;
        } else if (m.status == 3) {
            ++finished;
        }
    }
    // The race is over once every racer has finished (or one has and the
    // others are out): mode 3, the finish ($10B2AA).
    if ((finished == players_ || (finished != 0 && out == players_ - 1)) && game_mode != 1) {
        game_mode = 3;
    }
    if (course_ == data::Course::practice) {
        project_drop_marks();
    }
    // $119D4E: the steelies.
    for (auto& s : steelies) {
        update_steelie(s);
    }
    // $11AB0A: the slinkies.
    for (auto& s : slinkies) {
        update_slinky(s);
    }
    // $11BDDA: the acid.
    for (auto& a : acids) {
        update_acid(a);
    }
    // $11C8DA, $11D6B0: Silly's critters and birds; $11DB4C: Aerial's pistons.
    update_critters();
    update_birds();
    update_pistons();
    // $1100A2 reads the marbles as they are now.
    for (int i = 0; i < players_; ++i) {
        const Marble& m = marbles[std::size_t(i)];
        regions_.racers[std::size_t(i)] = {m.status == 1, m.x, m.y, m.feature};
    }
    regions_.update(terrain_, probe_);
    cycle_colours();
    take_region_display_events();
    take_region_sounds();
    count_down();
    // $10B48A: a sound some updates after a zone was entered (Aerial).
    if (zone_sound_timer_ >= 0 && --zone_sound_timer_ < 0) {
        sound(6);
        zone_sounds_ |= 4;
    }
    // $111BB4 at the end of the update: the display list's boxes and order.
    display_.sort([this](std::uint8_t type, std::uint8_t index) { return box_of(type, index); });
    // $1076B6: the display shows a picture Ultimate's moving goal area
    // finished rendering.
    regions_.show_rendered_picture();
    frame_ += first_update_ ? 2 : 1;
    first_update_ = false;
}

// $10AAD0: once a race, if the race's roll is under the course's chance
// ($10ABDC: 0, 200, 100, 65, 65, 65 of 256), the first time a player rolls
// in one of the course's listed zones ($10ABF4) with 3 to 6 seconds left,
// the racing marbles go dizzy at their next grounded update.
void Race::dizzy_surprise() {
    constexpr std::uint32_t chances = 0x10ABDC, zone_lists = 0x10ABF4;
    const auto& p = files_->program();
    if (!surprise_armed_ || surprise_roll_ >= p.u8(chances + std::uint32_t(course_))) {
        return;
    }
    const std::uint32_t zones = p.u32(zone_lists + std::uint32_t(course_) * 4);
    if (p.s8(zones) == -1) {
        return;
    }
    for (int i = 0; i < players_; ++i) {
        const Marble& m = marbles[std::size_t(i)];
        for (std::uint32_t z = zones;; ++z) {
            if (p.s8(z) == m.feature) {
                if (m.status == 1 && m.mode == Marble::rolling && m.time < 7 && m.time > 2) {
                    surprise_armed_ = false;
                    if (marbles[0].status == 1) {
                        marbles[0].stunned = 1;
                    }
                    if (players_ == 2 && marbles[1].status == 1) {
                        marbles[1].stunned = 1;
                    }
                    // (The crowd race's marbles race too: dizzy as well.)
                    for (int b = players_; b < marble_count_; ++b) {
                        if (marbles[std::size_t(b)].status == 1) {
                            marbles[std::size_t(b)].stunned = 1;
                        }
                    }
                    return;
                }
                break;
            }
            if (p.s8(z + 1) == -1) {
                break;
            }
        }
    }
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
            m.clock_flash = -1;
            game_mode = 4;
        } else if (m.time == 4) {
            m.clock_flash = 0; // $10B7E6: the clock flashes from four seconds on
        } else if (m.time > 5) {
            m.clock_flash = -1;
        }
    }
    // $10B946, every vertical blank: the flash's shade (32 blanks a round).
    for (int i = 0; i < players_; ++i) {
        Marble& m = marbles[std::size_t(i)];
        if (m.clock_flash >= 0) {
            m.clock_flash = std::int8_t((m.clock_flash + 2) & 0x1F);
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
    bands_changed(band(before), band(view_));
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
    bands_changed(band(before), band(view_));
    return true;
}

// $114402: one update of a marble, by its mode.
void Race::update_marble(Marble& m) {
    const auto mode = m.mode;
    m.sparkle_count = 0; // shown for one update ($105ECC clears +$EB)
    m.sparkles_shown = false;
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
    case Marble::dissolving:
        pull_into_acid(m);
        animate(m);
        project(m, true);
        check_visible(m);
        break;
    case Marble::captured:
        // In a vacuum: sparkles to its mouth; then the vacuum closes (block
        // entry $13 for code 11, $14 for 13, with the zone's sound) and the
        // marble reappears where it is.
        project(m, true);
        if (into_vacuum(m)) {
            Region& r = regions_.all()[std::size_t(std::uint8_t(m.region_code)) % Regions::slots];
            r.pc = regions_.block_entry(r.code == 11 ? 0x13 : 0x14);
            constexpr std::uint32_t closing_sounds = 0x1020D4; // longs by zone - $1E
            sound(int(files_->program().u32(closing_sounds + std::uint32_t(r.feature - 0x1E) * 4)));
            regions_.resume(r, terrain_, probe_);
            take_region_sounds();
            sound(0x13);
            m.animation = 0;
            m.mode = Marble::respawning;
            ++m.respawns;
            m.delay = 0x65;
        }
        break;
    case Marble::eaten:
        // In a bird: the sparkles gather in ($1138D4, counting the other
        // way), then the player starts again.
        project(m, true);
        if (appear(m)) {
            m.delay = 0x65;
            set_mode(m, Marble::respawning);
        }
        break;
    default:
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
    show(0x2A, m.player);
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
        } else if (auto* s = steelie_of(m)) {
            remove(*s, true, true);
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
    if (collide(m)) {
        locate(m);
    }
    // Not yet rebuilt: the creatures of later courses ($11C1C0, $11E1E0,
    // $11CA44, $11D8EA).
    if (player && touch_slinkies(m)) {
        locate(m);
    }
    if (player && touch_acid(m)) {
        locate(m);
    }
    if (player && touch_pistons(m)) {
        locate(m);
    }
    if (player) {
        touch_critters(m);
        touch_birds(m);
    }
    if (player) {
        if (m.mode == Marble::eaten) {
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
            if (m.fall_start_z - m.z > 0x800000 && !(cheats.safe_falls && !is_bot(m) && ground > 0)) {
                // Fell too far: lost.
                if (player) {
                    sound_stop();
                    sound(0xC);
                    m.delay = 100;
                    set_mode(m, Marble::respawning);
                } else if (auto* s = steelie_of(m)) {
                    remove(*s, true, true);
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
                if (!is_bot(m)) {
                    drop_bonus(m); // the players' alone
                }
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

// $1186A4: a funnel's walls, a list of planes ($118BC8 by part: normal
// (x, y, z; a first word over $800 marks the last plane), a box and a point
// relative to the region's point + 8). A marble that crosses a plane goes
// back to where it started; touching one already, it bounces straight back
// (or, falling deep into the tube, is lost); otherwise its velocity is
// reflected. True when the marble was lost.
bool Race::funnel_walls(Marble& m, const Region& r, int part) {
    const auto& p = files_->program();
    constexpr std::uint32_t wall_lists = 0x118BC8;
    const std::int32_t vx = m.vx, vy = m.vy, vz = m.vz;
    const auto rx = s16(s16(r.x >> 16) + 8), ry = s16(s16(r.y >> 16) + 8), rz = s16(r.z >> 16);
    const auto nx_ = s16(rx - probe_.x()), ny_ = s16(ry - probe_.y()), nz_ = s16(rz - probe_.z());
    const auto sx_ = s16(rx - s16(start_x_ >> 16)), sy_ = s16(ry - s16(start_y_ >> 16)), sz_ = s16(rz - s16(start_z_ >> 16));
    for (std::uint32_t w = p.u32(wall_lists + std::uint32_t(part) * 4);; w += 16) {
        std::int16_t a = p.s16(w);
        const bool last = a > 0x800;
        if (last) {
            a = s16(a - 0x1000);
        }
        const std::int16_t b = p.s16(w + 2), c = p.s16(w + 4);
        auto inside = [&](std::int16_t x, std::int16_t y, std::int16_t z) {
            return p.s8(w + 6) <= x && x <= p.s8(w + 7) && p.s8(w + 8) <= y && y <= p.s8(w + 9) && p.s8(w + 10) <= z &&
                   z <= p.s8(w + 11);
        };
        if (inside(nx_, ny_, nz_) || inside(sx_, sy_, sz_)) {
            auto side = [&](std::int16_t x, std::int16_t y, std::int16_t z) {
                return std::int32_t(s16(z - p.s8(w + 14))) * c + std::int32_t(s16(y - p.s8(w + 13))) * b +
                       std::int32_t(s16(x - p.s8(w + 12))) * a;
            };
            const std::int32_t now = side(nx_, ny_, nz_), before = side(sx_, sy_, sz_);
            if (abs32(now) < 0x400 || abs32(before) < 0x400 || ((now ^ before) & 0x80000000) != 0) {
                if (abs32(before) < 0x400) {
                    if (m.air == 2 && abs32(m.z - m.fall_start_z) > 0x100000) {
                        sound_stop();
                        sound(0x18);
                        if (is_player(m)) {
                            m.delay = 100;
                            set_mode(m, Marble::respawning);
                        } else if (auto* s = steelie_of(m)) {
                            remove(*s, true, true);
                        }
                        return true;
                    }
                    m.vx = -vx;
                    m.vy = -vy;
                    m.vz = -vz;
                    m.x = start_x_;
                    m.y = start_y_;
                    m.z = start_z_;
                    return false;
                }
                m.x = start_x_;
                m.y = start_y_;
                m.z = start_z_;
                const auto k = s16((std::int32_t(s16(m.vz >> 7)) * c + std::int32_t(s16(m.vy >> 7)) * b +
                                    std::int32_t(s16(m.vx >> 7)) * a) >> 8);
                m.vx -= std::int32_t(a) * k;
                m.vy -= std::int32_t(b) * k;
                m.vz -= std::int32_t(k) * c;
            }
        }
        if (last) {
            return false;
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
        // The region's cell against the probe's.
        const std::int16_t cx = s16((s16(r.x >> 16) >> 3) - probe_.cell_x());
        const std::int16_t cy = s16((s16(r.y >> 16) >> 3) - probe_.cell_y());
        const std::int16_t pz = probe_.z();
        bool hit = false;
        // A funnel: down the tube to its exit (x, y in pixels; another
        // marble there moves the exit 8 back), held in mode 3 for $12
        // updates with a push out of it.
        auto through_funnel = [&](std::int32_t vx_out, std::int32_t vy_out, int ex, int ey, int ex_busy, int ey_busy) {
            const bool busy = occupied(&m, ex, ey, 0x70) != 0;
            m.region_timer = 0x12;
            m.vx = vx_out;
            m.vy = vy_out;
            m.x = (busy ? ex_busy : ex) << 16;
            m.y = (busy ? ey_busy : ey) << 16;
            probe_.locate(terrain_, m.x, m.y, m.z);
            m.z = probe_.ground_height();
            m.mode = Marble::recoiling;
            sound_stop();
            sound(0x1C, positional);
        };
        switch (r.code) {
        // Ice ($117508): within 16 x 14 cells from the region's cell the
        // ground has no friction.
        case 0x10:
            if (cx >= 0 && cx < 0x10 && cy >= 0 && cy < 0xE) {
                if (before != r.code) {
                    sound(9, positional);
                }
                m.region_code = r.code;
                m.region_timer = 0xFF;
            }
            break;
        // The funnels (Beginner): their mouths ($117772, $1178C6) and walls.
        case 0x12:
            if (cx > -3 && cx < 3 && cy > -2 && cy < 5 && pz > 0x3F40) {
                if (cx >= 0 && cx < 3 && cy >= 0 && cy < 4) {
                    m.region_code = r.code;
                    hit = true;
                    m.region_timer = 0xFF;
                }
                if (pz < 0x3F78) {
                    if (cx >= 0) {
                        if (pz < 0x3F60) {
                            if (is_player(m)) {
                                m.air = 0;
                                through_funnel(0x40000, 0, 0x20C, 0x264, 0x204, 0x264);
                            } else if (auto* s = steelie_of(m)) {
                                remove(*s, true, true);
                            }
                        } else {
                            m.vx = m.vy = 0;
                        }
                    }
                } else if (funnel_walls(m, r, 0)) {
                    return;
                }
            }
            break;
        case 0x13:
        case 0x14:
            if (dx > -0x19 && dx < 0x20 && dy > -0x11 && dy < 0x18) {
                if ((s16(r.y >> 16) >> 3) == probe_.cell_y() && cx >= 0 && cx < 3) {
                    through_funnel(0, 0x40000, 0x29C, 0x2E4, 0x29C, 0x2DC);
                    m.region_code = r.code;
                    hit = true;
                } else if (funnel_walls(m, r, 2)) {
                    return;
                }
            }
            break;
        case 0x15:
            if (dx > -0x19 && dx < 0x28 && dy > -0x19 && dy < 0x28 && pz < 0x3F40 && funnel_walls(m, r, 1)) {
                return;
            }
            break;
        case 0x16:
            if (dx > -0x11 && dx < 0x20 && dy > -0x11 && dy < 0x18 && funnel_walls(m, r, 3)) {
                return;
            }
            break;
        // Intermediate's funnel ($117BB6): its mouth takes the marble up the
        // course to (620, 596) with a push towards -y (8 further when that
        // spot is taken); its walls are plane set 0 above the mouth and set
        // 4 down the tube (code $21).
        case 0x20:
            if (cx > -3 && cx < 3 && cy > -2 && cy < 5 && pz > 0x3F80) {
                if (cx >= 0 && cx < 3 && cy >= 0 && cy < 4) {
                    m.region_code = r.code;
                    hit = true;
                    m.region_timer = 0xFF;
                }
                if (pz < 0x3FB0) {
                    if (cx >= 0) {
                        if (pz < 0x3FA0) {
                            m.air = 0;
                            through_funnel(0, -0x40000, 0x26C, 0x254, 0x26C, 0x25C);
                            m.region_code = r.code;
                        } else {
                            m.vx = m.vy = 0;
                        }
                    }
                } else if (funnel_walls(m, r, 0)) {
                    return;
                }
            }
            break;
        case 0x21:
            if (cx > -2 && cx < 4 && cy > -2 && cy < 3 && pz < 0x3F60 && funnel_walls(m, r, 4)) {
                return;
            }
            break;
        // Code 10 ($116FF8): a launcher. A marble on its cell (not moving
        // up or down) when the launcher is at its first frame and not
        // counting is put on its point and thrown up and away (towards -y,
        // a little sideways at random); the launcher's script goes on at
        // block entry $17. Otherwise the marble is held back.
        case 10:
            if (dx > -8 && dx < 8 && dy > -8 && dy < 8 && m.vz == 0) {
                if (r.anim_frames == r.frame && r.count == 0) {
                    const Marble& other = marbles[std::size_t(1 - (m.player & 1))];
                    if (players_ == 2 && other.status == 1 && other.region_code == 10) {
                        // The launcher is taken: back to the update's start ($11541A).
                        m.x = start_x_;
                        m.y = start_y_;
                        m.z = start_z_;
                        m.vx = m.vy = 0;
                    } else {
                        m.x = r.x;
                        m.y = r.y;
                        m.z -= 0x30000;
                        m.vz = 0xA0000;
                        m.vx = std::int32_t(random.next(0x2000)) - 0x1000;
                        m.vy = -0x25000 - std::int32_t(random.next(0xA000) & 0xFFFF);
                        m.air = 2;
                        sound_stop();
                        m.mode = Marble::recoiling;
                        m.region_code = r.code;
                        hit = true;
                        m.region_timer = 0xF;
                        Region& launcher = regions_.all()[std::size_t(&r - regions_.all().data())];
                        launcher.pc = regions_.block_entry(0x17);
                        regions_.resume(launcher, terrain_, probe_);
                        take_region_sounds();
                    }
                } else {
                    m.x = start_x_;
                    m.y = start_y_;
                }
            }
            break;
        // Aerial's vacuums (codes 11 and 13, $1170E8 / $117466): while
        // sucking (their animation is block entry $18 or $19) they draw a
        // marble in their mouth's reach towards the mouth, take it in the
        // mouth (captured, mode 10) and, around their body, bounce it back
        // or (when it was on the body already) eat it. Code 13 is code 11
        // turned a quarter.
        case 0xB:
        case 0xD: {
            const bool turned = r.code == 0xD;
            if (r.state == 0 || r.anim_frames != regions_.block_entry(turned ? 0x19 : 0x18)) {
                break;
            }
            // Along and across the mouth, as for code 11.
            const std::int16_t a = turned ? dy : dx, b = turned ? dx : dy;
            if (a < -0xC || a > 0x1B || b < -0x20 || b > -9) {
                // The body: the box as code 11's, with the code-11 axes
                // (code 13 tests the same box unturned, and the start
                // offsets an earlier test left; $118284).
                if (dx > -0xD && dx < 0x1C && dy > -9 && dy < 0x10 && pz < 0x3FC4) {
                    if (!turned) {
                        vacuum_start_dx_ = s16(s16(r.x >> 16) - s16(start_x_ >> 16));
                        vacuum_start_dy_ = s16(s16(r.y >> 16) - s16(start_y_ >> 16));
                    }
                    const auto sdx = vacuum_start_dx_, sdy = vacuum_start_dy_;
                    if (sdx < -0xC || sdx > 0x1B || sdy < -8 || sdy > 0xF || s16(start_z_ >> 16) > 0x3FC3) {
                        bounce_x_ = bounce_y_ = true;
                    } else {
                        m.delay = 0x65;
                        set_mode(m, Marble::respawning);
                    }
                }
            } else if (a < 0 || a > 0xF || b < -0xF || b > -10) {
                // The draw: a quarter of the unit vector to the mouth
                // ($1248D4 division by the octagonal length).
                const std::int16_t tx = s16(turned ? dx + 0xC : dx - 8), ty = s16(turned ? dy - 8 : dy + 0xC);
                const std::int16_t d = octagonal_length(tx, ty);
                std::int16_t qx = 0, qy = 0;
                if (d != 0) {
                    qx = s16((std::int32_t(tx) << 16) / std::int32_t(d));
                    qy = s16((std::int32_t(ty) << 16) / std::int32_t(d));
                }
                m.vx += std::int32_t(qx) * 4;
                m.vy += std::int32_t(qy) * 4;
            } else {
                m.mode = Marble::captured;
                m.delay = 0x20;
                m.region_code = std::int8_t(&r - regions_.all().data());
                hit = true;
            }
            break;
        }
        // Code 12 ($117E86): a bumper that throws a marble out of its frame's
        // box, from the box's middle, and sets it bouncing.
        case 0xC: {
            if (r.state == 0) {
                break;
            }
            // (At the end of its frame list the box comes from low memory.)
            const int bx = frame_box_byte(r.frame, 4), by = frame_box_byte(r.frame, 5);
            const int bw = frame_box_byte(r.frame, 6), bh = frame_box_byte(r.frame, 7);
            const int x1 = 3 - bx, y1 = 3 - by, x0 = -(bx + bw + 3), y0 = -(by + bh + 3);
            if (!(x0 <= dx && dx < x1 && y0 <= dy && dy < y1)) {
                break;
            }
            vacuum_start_dx_ = s16(s16(r.x >> 16) - s16(start_x_ >> 16));
            vacuum_start_dy_ = s16(s16(r.y >> 16) - s16(start_y_ >> 16));
            const auto sdx = vacuum_start_dx_, sdy = vacuum_start_dy_;
            if (sdx < x0 || x1 <= sdx || sdy < y0 || y1 <= sdy) {
                bounce_x_ = bounce_y_ = true;
                break;
            }
            const std::int16_t tx = s16(dx + 4), ty = s16(dy + 4);
            const std::int16_t d = octagonal_length(tx, ty);
            std::int16_t qx = 0x1000, qy = 0;
            if (d != 0) {
                qx = s16((-(std::int32_t(tx) << 16)) / std::int32_t(d));
                qy = s16((-(std::int32_t(ty) << 16)) / std::int32_t(d));
            }
            m.vx = std::int32_t(qx) << 6;
            m.vy = std::int32_t(qy) << 6;
            if (m.mode != Marble::landing) {
                m.animation_tick = 0;
                m.animation_period = 2;
                m.animation = files_->marble_data().u32(marble_data_);
                sound(0xE, positional);
            }
            m.mode = Marble::landing;
            land(m, 0);
            m.delay = 0x3C;
            m.bounce = 0;
            break;
        }
        // Silly's funnel up (code $22, $117BF4): on the cell before the
        // region's point the marble rises (vz +$9000 an update; from $3F74
        // lifted 2 at once) until it is at $3F95, then comes out at the top
        // at x 684 or 732 (random, the other one when taken) and y 688,
        // pushed towards -y; its walls are codes $23 and $24 (plane set 5).
        case 0x22:
            if (cy == 1 && cx == 1) {
                if (pz < 0x3F95) {
                    if (pz == 0x3F74) {
                        m.z += 0x20000;
                    }
                    m.vz += 0x9000;
                    m.vx = m.vy = 0;
                    m.air = 2;
                } else {
                    m.air = 0;
                    m.region_timer = 0x12;
                    m.vx = 0;
                    m.vy = -0x40000;
                    const bool second = random.next(2) != 0;
                    const int first_x = second ? 0x2DC : 0x2AC, other_x = second ? 0x2AC : 0x2DC;
                    m.x = (occupied(&m, first_x, 0x2B0, 0x70) != 0 ? other_x : first_x) << 16;
                    m.y = 0x2B0 << 16;
                    probe_.locate(terrain_, m.x, m.y, m.z);
                    m.z = probe_.ground_height();
                    start_z_ = m.z;
                    m.region_code = r.code;
                    hit = true;
                    m.mode = Marble::recoiling;
                    sound_stop();
                    sound(0x25, positional);
                }
            }
            break;
        case 0x23:
        case 0x24:
            if (dx > -0x11 && dx < 0x20 && dy > -0x19 && dy < 0x28 && funnel_walls(m, r, 5)) {
                return;
            }
            break;
        // Ultimate's funnel (code $25, $117D3A): on its 2 x 2 cells below
        // $3FE0 the marble goes to (240, 320) or (240, 256) (random, the
        // other one when taken) with a push towards +x; its walls are codes
        // $26 and $27 (plane set 6).
        case 0x25:
            if (cx >= 0 && cx < 2 && cy >= 0 && cy < 2 && pz < 0x3FE0) {
                m.air = 0;
                sound(0xC);
                m.region_timer = 0x12;
                m.vx = 0x40000;
                m.vy = 0;
                const bool second = random.next(2) != 0;
                const int first_y = second ? 0x100 : 0x140, other_y = second ? 0x140 : 0x100;
                m.y = (occupied(&m, 0xF0, first_y, 0x70) != 0 ? other_y : first_y) << 16;
                m.x = 0xF0 << 16;
                probe_.locate(terrain_, m.x, m.y, m.z);
                m.z = probe_.ground_height();
                m.region_code = r.code;
                hit = true;
                m.mode = Marble::recoiling;
                sound_stop();
                sound(0x1C, positional);
            }
            break;
        case 0x26:
        case 0x27:
            if (dx > -0x19 && dx < 0x28 && dy > -0x19 && dy < 0x28 && funnel_walls(m, r, 6)) {
                return;
            }
            break;
        // Ultimate's floors ($117F2A-$118600): a rectangle of cells from the
        // region's cell back (towards smaller x and y) marks the marble; the
        // slippery and sticky floors (codes $2D-$31, $38-$3B) only at the
        // region's height, the icy ones ($32-$37, sound 9 on the way in)
        // at any.
        case 0x2D: case 0x2E: case 0x2F: case 0x30: case 0x31: case 0x32: case 0x33: case 0x34: case 0x35:
        case 0x36: case 0x37: case 0x38: case 0x39: case 0x3A: case 0x3B: {
            struct Floor {
                std::int8_t columns, rows;
            };
            static constexpr std::array<Floor, 15> floors{{{0x14, 0xE}, {2, 3}, {10, 0x14}, {0x13, 9}, {2, 6}, // $2D-$31
                                                           {4, 2}, {2, 4}, {8, 0xB}, {3, 8}, {6, 3}, {2, 3},    // $32-$37
                                                           {0x14, 0xE}, {0xC, 0xE}, {4, 2}, {2, 3}}};          // $38-$3B
            const Floor f = floors[std::size_t(r.code - 0x2D)];
            const bool icy = r.code >= 0x32 && r.code <= 0x37;
            if (cx >= 0 && cx < f.columns && cy >= 0 && cy < f.rows && (icy || m.z == r.z)) {
                if (icy && before != r.code) {
                    sound(9, positional);
                }
                m.region_code = r.code;
                m.region_timer = 0xFF;
            }
            break;
        }
        // Aerial's chutes ($1175A4): along a column of cells from the
        // region's cell (code $17: the same x cell, up to $11 cells before
        // it in y; $18 the same along x). The marble is marked; the mark
        // takes the steering and the friction away ($1134C4, $115470).
        case 0x17:
            if ((s16(r.x >> 16) >> 3) == probe_.cell_x() && cy < 1 && cy > -0x12) {
                m.region_code = r.code;
                m.region_timer = 0xFF;
            }
            break;
        case 0x18:
            if (cx < 1 && cx > -0x12 && (s16(r.y >> 16) >> 3) == probe_.cell_y()) {
                m.region_code = r.code;
                m.region_timer = 0xFF;
            }
            break;
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
