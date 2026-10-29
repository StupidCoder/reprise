#include "tower_of_babel_amiga/game/panel.hpp"
#include <algorithm>

namespace babel::game {
namespace {

constexpr int camera_step = 0x80;    // a camera rises, sinks or slides this much a press
constexpr int camera_zoom = 0x100;   // and moves in or out this much
constexpr int camera_top = 0x1000, camera_bottom = 0x80;
constexpr int camera_side = 0x1000;  // +$14 stays within -$1000..$1000
constexpr int camera_near = -0x640, camera_far = -0x3A40; // +$16

} // namespace

void Panel::poll(TowerState& t, const Rules& rules, const Settings& settings, const Input& input, int picked) {
    fire_failed_ = false;
    // A click in the view last frame: the view's pick names an object; a
    // spider there is selected as if its button were pressed.
    if (pick_) {
        pick_ = false;
        if (picked >= 4 && picked < 7) {
            select(t, picked);
            return;
        }
    }
    if (select_wait_ > 0) --select_wait_;
    if (turn_wait_ > 0 && --turn_wait_ == 0) t.panel.turn = -1;
    if (move_wait_ > 0) --move_wait_;
    if (program_wait_ > 0 && --program_wait_ == 0) {
        auto& p = t.programs[std::size_t(t.active)];
        if (p.mode != 3 && p.mode != 1) {
            t.panel.program = -1;
            p.mode = -1;
        }
    }
    if (t.objects[std::size_t(t.active)].progress == 0) {
        t.programs[std::size_t(t.active)].command = -1;
        t.panel.move = -1;
    }
    if (input.view_click) {
        pick_ = true;
        return;
    }
    const int b = input.button;
    if (b < 0) return;
    if (b < forward) {
        select(t, b);
        return;
    }
    if (b == pause) {
        if (t.end_countdown == 0) {
            t.panel.turn = 3;
            pause_ = true;
        }
        return;
    }
    if (t.active < 4) {
        if (settings.cameras_forbidden) return;
    } else if (t.objects[std::size_t(t.active)].type >= 0x1A) {
        return; // the spider is gone
    }
    if (b < stop_in) {
        command(t, rules, settings, b - forward);
    } else if (b < strip_first) {
        program_button(t, settings, b - stop_in);
    } else {
        t.programs[std::size_t(t.active)].position = b - strip_first;
        t.panel.program_record = t.active;
    }
}

void Panel::press(TowerState& t, const Rules& rules, const Settings& settings, int b) {
    if (t.active >= 4 && t.objects[std::size_t(t.active)].type >= 0x1A) return;
    if (b >= forward && b < stop_in && b != pause) command(t, rules, settings, b - forward);
}

void Panel::select(TowerState& t, int slot) {
    if (slot == t.active || select_wait_ != 0) return;
    auto& current = t.programs[std::size_t(t.active)];
    if (current.mode == 3) {
        // Writing a program: choosing a spider while a camera's program is
        // written hands the program over (and turns the spider the camera's
        // way); a spider's program can be stored on a camera. Choosing another
        // spider just selects it.
        const bool hand_over = t.active < 4 ? slot >= 4 : slot < 4;
        if (hand_over) {
            if (t.active < 4) t.objects[std::size_t(slot)].heading = t.objects[std::size_t(t.active)].heading;
            t.programs[std::size_t(slot)].commands = current.commands;
            if (t.active < 4) {
                current.commands = {};
                current.position = 0;
                t.panel.program_record = t.active;
            }
            current.mode = -1;
            t.panel.program = -1;
            select_wait_ = 4;
            return;
        }
    }
    t.active = slot;
    selected_ = slot;
    const auto& p = t.programs[std::size_t(slot)];
    t.panel.turn = -1;
    t.panel.move = p.command;
    t.panel.program = p.mode;
    t.panel.program_record = slot;
    select_wait_ = 4;
}

void Panel::command(TowerState& t, const Rules& rules, const Settings& settings, int k) {
    const int slot = t.active;
    auto& p = t.programs[std::size_t(slot)];
    auto& o = t.objects[std::size_t(slot)];
    if (p.mode == 3) {
        // Writing: the command goes in at the cursor, the rest move up one.
        if (program_wait_ != 0) return;
        const int at = std::clamp(p.position, 0, 7);
        for (int i = 7; i > at; --i) p.commands[std::size_t(i)] = p.commands[std::size_t(i - 1)];
        p.commands[std::size_t(at)] = std::uint8_t(k + 1);
        p.position = at < 7 ? at + 1 : at;
        t.panel.program_record = slot;
        program_wait_ = 4;
        return;
    }
    if (slot >= 4) {
        if (k <= 1) {
            if (move_wait_ != 0) return;
            t.started = true;
            const bool done = k == 0 ? rules.walk(t, slot, o.heading) : rules.back(t, slot);
            t.panel.move = k;
            if (done) {
                p.command = k;
                move_wait_ = 8;
            } else {
                move_wait_ = 1;
            }
            return;
        }
        if (turn_wait_ != 0) return;
        if (k == 2 || k == 3) {
            if (k == 2) rules.turn_left(t, slot);
            else rules.turn_right(t, slot);
            t.panel.turn = k - 2;
            turn_wait_ = 4;
            return;
        }
        t.started = true;
        const bool done = rules.fire(t, slot);
        t.panel.turn = 2;
        turn_wait_ = done ? 8 : 1;
        fire_failed_ = !done;
        return;
    }
    // A camera: forward and back raise and lower it, left and right slide it.
    switch (k) {
    case 0:
    case 1: {
        const int y = o.y + (k == 0 ? camera_step : -camera_step);
        if (k == 0 ? unsigned(y) >= unsigned(camera_top) : unsigned(y) < unsigned(camera_bottom)) return;
        o.y = y;
        t.panel.move = k;
        move_wait_ = 1;
        cameras_follow(t, settings, slot);
        return;
    }
    case 2:
    case 3: {
        const int side = std::int16_t(o.cell + (k == 2 ? -camera_step : camera_step));
        if (k == 2 ? side < -camera_side : side > camera_side) return;
        o.cell = side;
        camera_shift(t, settings, slot);
        t.panel.turn = k - 2;
        turn_wait_ = 1;
        return;
    }
    default:
        return; // cameras do not fire
    }
}

void Panel::program_button(TowerState& t, const Settings& settings, int k) {
    const int slot = t.active;
    auto& p = t.programs[std::size_t(slot)];
    auto& o = t.objects[std::size_t(slot)];
    switch (k) {
    case 0: // Stop / In
        if (slot < 4) {
            const int depth = std::int16_t(o.target + camera_zoom);
            if (depth > camera_near) return;
            o.target = depth;
            camera_shift(t, settings, slot);
        }
        t.panel.program = 0;
        p.mode = -1;
        program_wait_ = 8;
        return;
    case 1: // Run / Out
        if (slot < 4) {
            const int depth = std::int16_t(o.target - camera_zoom);
            if (depth < camera_far) return;
            o.target = depth;
            camera_shift(t, settings, slot);
            t.panel.program = 1;
            p.mode = -1;
            program_wait_ = 8;
            return;
        }
        if (program_wait_ != 0) return;
        t.started = true;
        p.mode = 1;
        t.panel.program = 1;
        p.position = -1;
        program_wait_ = 8;
        return;
    case 2: // Run All: every spider's program starts
        if (program_wait_ != 0) return;
        t.started = true;
        p.mode = 2;
        t.panel.program = 2;
        for (int s = 4; s < 7; ++s) {
            t.programs[std::size_t(s)].mode = 1;
            t.programs[std::size_t(s)].position = -1;
        }
        program_wait_ = 8;
        return;
    case 3: // Write
        if (p.mode != 3) {
            p.position = 0;
            t.panel.program_record = slot;
            p.mode = 3;
        }
        t.panel.program = 3;
        return;
    case 4: { // Delete: the command before the cursor goes
        if (program_wait_ != 0) return;
        int at = p.position - 1;
        if (at < 0) return;
        at = std::min(at, 7);
        for (int i = at; i < 7; ++i) p.commands[std::size_t(i)] = p.commands[std::size_t(i + 1)];
        p.commands[7] = 0;
        p.position = at;
        t.panel.program_record = slot;
        t.panel.program = 4;
        p.mode = -1;
        program_wait_ = 8;
        return;
    }
    default: // Clear
        p.commands = {};
        p.position = 0;
        t.panel.program_record = slot;
        t.panel.program = 5;
        p.mode = -1;
        program_wait_ = 8;
        return;
    }
}

// $B44C: a camera's sideways (+$14) and depth (+$16) offsets, seen from its
// own heading, place it; linked cameras all move (each from its own side).
void Panel::camera_shift(TowerState& t, const Settings& settings, int slot) const {
    if (settings.link_cameras) {
        cameras_follow(t, settings, slot);
        return;
    }
    auto& o = t.objects[std::size_t(slot)];
    int x = o.cell, z = o.target;
    for (int i = 0; i < o.heading; ++i) {
        const int old_x = x;
        x = z;
        z = -old_x;
    }
    o.x = std::int16_t(x);
    o.z = std::int16_t(z);
}

// $B478: the four cameras take this camera's offsets and height, each
// turned a quarter more ($B470 does it only for linked cameras).
void Panel::cameras_follow(TowerState& t, const Settings& settings, int slot) const {
    if (!settings.link_cameras) return;
    const auto& from = t.objects[std::size_t(slot)];
    int x = from.cell, z = from.target;
    const int side = from.cell, depth = from.target, y = from.y;
    for (int n = 0; n < 4; ++n) {
        auto& c = t.objects[std::size_t(n)];
        c.cell = side;
        c.target = depth;
        c.x = std::int16_t(x);
        c.y = y;
        c.z = std::int16_t(z);
        const int old_x = x;
        x = z;
        z = -old_x;
    }
}

} // namespace babel::game
