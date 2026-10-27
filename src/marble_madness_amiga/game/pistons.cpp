// Aerial's pistons ($11DB4C-$11E550).
//
// While a player is in zone 4 or 5 ($11DFDE, on each zone change) the 36
// pistons of three groups are live: the Track's region block lists their
// cells (+$1AC, +$1D0), heights (+$1F4) and first waits (+$6C for the first
// 24, the rest from one of four lists at +$18C picked at random). Each
// update ($11DB4C) a group whose wait has run out picks one of four
// patterns at random and raises three of its pistons (a row, a column, a
// diagonal step or a corner of the group's 3 x 4 cells), plays the rising
// animation over the playfield, then puts its picture back and waits 0,
// 16, 32 or 48 updates. A raised piston throws up a marble that sits on its
// cell at the floor's height and pushes back one that runs into it
// ($11E1E0). (The display type $29 the program removes for raised pistons
// is never added.)
#include "marble_madness_amiga/game/race.hpp"

namespace marble_madness::game {
namespace {

std::int16_t s16(std::int32_t v) { return std::int16_t(v); }

// The Track's region block (header +$14) offsets.
enum PistonTable : std::uint32_t {
    first_waits = 0x6C,     // words, pistons 0-23
    wait_lists = 0x18C,     // 4 longs: words for pistons 24-35
    patterns = 0x19C,       // 4 longs ($1007A8; not read by the program)
    piston_columns = 0x1AC,   // bytes
    piston_rows = 0x1D0,      // bytes
    piston_heights = 0x1F4,   // words
    groups = 0x23C,         // 3 x $14 bytes
    rises = 0x278,          // 4 longs: frame lists by pattern 0-3
};
constexpr std::uint32_t sounds_on_screen = 0x102148, sounds_off_screen = 0x102168; // longs by group * 2 + 0-1

} // namespace

// $11DFDE: the pistons go live while a racing player is in zone 4 or 5, and
// are cleared when none is.
void Race::switch_pistons() {
    if (course_ != data::Course::aerial) {
        return;
    }
    bool wanted = false;
    for (int i = 0; i < players_; ++i) {
        const Marble& m = marbles[std::size_t(i)];
        if (m.status == 1 && (m.feature == 4 || m.feature == 5)) {
            wanted = true;
            break;
        }
    }
    const auto& t = files_->track(course_);
    const std::uint32_t block = t.u32(files_->track_header(course_) + 0x14);
    if (!pistons_on_ && wanted) {
        const std::uint32_t k = random.next(4);
        const std::uint32_t waits = t.u32(block + wait_lists + k * 4);
        for (std::uint32_t i = 0; i < pistons.size(); ++i) {
            Piston& piston = pistons[i];
            piston.index = std::uint8_t(i);
            piston.state = i < 24 ? t.s16(block + first_waits + i * 2) : t.s16(waits + (i - 24) * 2);
            piston.cell_x = t.s8(block + piston_columns + i);
            piston.cell_y = t.s8(block + piston_rows + i);
            probe_.place(std::int32_t(piston.cell_x) << 19, std::int32_t(piston.cell_y) << 19); // $10ECA6
            piston.z = t.s16(block + piston_heights + i * 2);
        }
        piston_pattern_ = t.u32(block + patterns + k * 4);
        pistons_on_ = true;
    } else if (!wanted && pistons_on_) {
        for (auto& piston : pistons) {
            if (piston.state == -1) {
                hide(0x29, piston.index);
            }
            piston.state = 0;
            piston.cell_x = piston.cell_y = 0;
            piston.z = 0;
        }
        pistons_on_ = false;
    }
}

// $11DB4C: one update of the three groups (while the pistons are live).
void Race::update_pistons() {
    if (course_ != data::Course::aerial || !pistons_on_) {
        return;
    }
    const auto& t = files_->track(course_);
    const std::uint32_t block = t.u32(files_->track_header(course_) + 0x14);
    std::array<bool, 3> started{};
    bool any_started = false;
    for (std::size_t g = 0; g < piston_groups.size(); ++g) {
        PistonGroup& group = piston_groups[g];
        const std::size_t first = g * 12;
        if (group.timer < 0) {
            if (group.timer == -1) {
                group.frame += 8;
                if (t.u32(group.frame) == 0xFFFFFFFF) {
                    // Down again: the picture back, a new wait.
                    piston_blits_.push_back({group.picture, group.x, s16(group.line - scroll_)});
                    group.timer = s16(std::int32_t(random.next(4)) << 4);
                    for (std::size_t i = 0; i < 12; ++i) {
                        pistons[first + i].state = group.timer;
                    }
                }
            }
        } else if (--group.timer < 1) {
            group.pattern = s16(random.next(4));
            int step = 1;
            switch (group.pattern) {
            case 2: // a column of three
                group.frame = t.u32(block + rises + 8);
                group.column = 0;
                group.row = s16(random.next(2));
                step = 4;
                break;
            case 1: // a diagonal step
                group.frame = t.u32(block + rises + 4);
                group.column = s16(random.next(3));
                group.row = s16(random.next(2));
                step = 3;
                break;
            case 0: // a row of three
                group.frame = t.u32(block + rises);
                group.column = 0;
                group.row = s16(random.next(4));
                step = 1;
                break;
            default:
                group.frame = t.u32(block + rises + 12);
                group.column = 0;
                group.row = s16(random.next(2));
                step = 2;
                break;
            }
            std::size_t at = first + std::size_t(group.column + group.row * 3);
            for (int i = 0; i < 3; ++i, at += std::size_t(step)) {
                if (at < pistons.size()) {
                    pistons[at].state = -1;
                }
            }
            group.timer = -1;
            any_started = true;
            started[g] = true;
        }
        if (group.timer == -1) {
            // The rising frame over the group's picture: the picture only
            // when something started this update or the list goes down to
            // an earlier picture, the frame only when it changed.
            const std::uint32_t item = t.u32(group.frame);
            bool picture = true, frame = true;
            if (!any_started) {
                const std::uint32_t previous = t.u32(group.frame - 8);
                frame = previous != item;
                picture = previous > item;
            }
            const std::int16_t line = s16(group.line - scroll_);
            if (picture) {
                piston_blits_.push_back({group.picture, group.x, line});
            }
            if (frame) {
                piston_blits_.push_back({item, s16(group.x + (group.row - group.column) * 8),
                                       s16(line + (group.column + group.row) * 4)});
            }
        }
    }
    // A sound for each group that started rising, from the on-screen list
    // when its picture is in view.
    for (std::size_t g = 0; g < piston_groups.size(); ++g) {
        if (!started[g]) {
            continue;
        }
        const std::uint32_t r = random.next(2);
        const std::int16_t line = s16(piston_groups[g].line - scroll_);
        const std::uint32_t list = line < -0x10 || line > 0xD8 ? sounds_off_screen : sounds_on_screen;
        sound(int(files_->program().u32(list + (std::uint32_t(g) * 2 + r) * 4)));
    }
}

// $11E1E0: a marble that moved into a raised piston's cell. Back to where
// it started; already in that cell at the piston's foot it is thrown up
// (and a little sideways at random, sound $B); less than 12 above the foot
// it bounces back.
bool Race::touch_pistons(Marble& m) {
    if (!pistons_on_ || course_ != data::Course::aerial) {
        return false;
    }
    const Piston* raised = nullptr;
    for (const auto& piston : pistons) {
        if (piston.state == -1 && piston.cell_x == std::int8_t(probe_.cell_x()) && piston.cell_y == std::int8_t(probe_.cell_y())) {
            raised = &piston;
            break;
        }
    }
    if (raised == nullptr) {
        return false;
    }
    m.x = start_x_;
    m.y = start_y_;
    if (std::int8_t(start_x_ >> 19) == raised->cell_x && std::int8_t(start_y_ >> 19) == raised->cell_y &&
        s16(m.z >> 16) == raised->z) {
        m.vz = 0x70000;
        m.z += 0xC0000;
        const std::int32_t vx = m.vx;
        m.vx = (random.next(2) == 0 ? -0x6000 : 0x6000) + (vx >> 1);
        const std::int32_t vy = m.vy;
        m.vy = (random.next(2) == 0 ? -0x6000 : 0x6000) + (vy >> 1);
        m.air = 2;
        sound(0xB);
    } else if (s16(m.z >> 16) - raised->z < 0xC) {
        m.vx = -m.vx;
        m.vy = -m.vy;
    }
    return true;
}

void Race::take_region_sounds() {
    for (const auto& e : regions_.take_sounds()) {
        sound(e.id, e.right);
    }
}

} // namespace marble_madness::game
