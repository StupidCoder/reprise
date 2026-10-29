// The object engine: scripts, sprite frames, spawning and removal
// (5DA4-6126, 6539-668F).

#include "sonic2_gg/game/game.hpp"
#include <stdexcept>
#include <string>

namespace sonic2_gg {

// 5DA4: updates slots 1-19 (Sonic, slot 0, is updated by 2F4F).
void Game::update_objects() {
    objects[0].touch_sides = 0; // D521
    for (int i = 1; i < 20; ++i) {
        update_object(objects[i]); // 5DBD
    }
}

// 5DBD: script, handler, then the off-screen check. Types F0 and above are
// slots being emptied (5E20).
void Game::update_object(Object& o) {
    if (!o.type) {
        return;
    }
    if (o.type >= 0xF0) {
        if (o.type == object_type::waiting) {
            check_respawn_window(o); // 60D2
        } else if (o.type == object_type::removed) {
            delete_object(o); // 6108
        }
        return;
    }
    // The bank is chosen by the type read on entry; an object that deleted
    // itself still runs the off-screen check of its old type (a zeroed slot
    // of type 20 and above becomes FF there, with bit 6 set).
    const unsigned bank = object_bank(o.type);
    animate_object(o, bank); // 5F0A
    run_object_handler(o);   // 5FF9
    if (bank == 0x1F && !o.state) {
        return; // 5DE1: types below 20 skip the check in state 0
    }
    check_off_screen(o); // 603C
}

// 5F0A: advances the object's script. A script is a list of 4-byte frames
// (ticks, sprite frame, handler address) and commands (FF, command, args).
// A change of next_state restarts at that state's script; otherwise the
// current frame counts down and then the next entry is read.
void Game::animate_object(Object& o, unsigned bank) {
    if (o.script_type) {
        if (o.next_state == o.state) {
            if (--o.frame_ticks) {
                return;
            }
            run_script(o, bank); // 5F47
            return;
        }
        o.state = o.next_state;
    }
    start_script(o, bank); // 5F27
}

// 5F27: the script of the current state.
void Game::start_script(Object& o, unsigned bank) {
    o.script_type = o.type;
    o.script_state = o.state;
    o.script_step = 0;
    run_script(o, bank);
}

// 5F47: works through the script until a frame is shown or a command stops.
void Game::run_script(Object& o, unsigned bank) {
    for (;;) {
        const auto& steps = scripts_.script(o.script_type, o.script_state, bank);
        const ScriptStep& s = steps.at(o.script_step++);
        switch (s.kind) {
        case ScriptStep::Kind::frame:
            o.frame_ticks = s.ticks;
            o.frame = s.frame;
            o.routine = s.routine;
            load_frame(o); // 5F6C
            return;
        case ScriptStep::Kind::restart: // 6572
            start_script(o, bank);
            return;
        case ScriptStep::Kind::remove: // 6575
            delete_object(o);
            return;
        case ScriptStep::Kind::call: // 6578
            run_routine(o, s.routine);
            if (!o.script_type) {
                // The original would go on reading at address 0: not seen.
                throw std::runtime_error("A script routine restarted the script");
            }
            continue;
        case ScriptStep::Kind::hold: // 658E
            return;
        case ScriptStep::Kind::velocity: { // 658F: X mirrored with the object
            unsigned vx = s.vx;
            if (o.display & display_flag::mirrored) {
                vx = (0x10000 - vx) & 0xFFFF;
            }
            o.vx = std::uint16_t(vx);
            o.vy = s.vy;
            continue;
        }
        case ScriptStep::Kind::request_state: // 65BF
            o.next_state = s.value;
            continue;
        case ScriptStep::Kind::spawn:           // 65D3: at an offset (X mirrored with the object)
            if (Object* n = find_free_slot()) { // 6025
                n->type = s.type;
                unsigned dx = s.dx;
                n->param = s.value;
                if (o.display & display_flag::mirrored) {
                    dx = (0x10000 - dx) & 0xFFFF;
                }
                n->x = std::uint16_t(o.x + dx);
                n->origin_x = n->x;
                n->y = std::uint16_t(o.y + s.dy);
                n->origin_y = n->y;
                n->display = o.display & display_flag::mirrored;
                n->tile_base = o.tile_base;
                n->tile_base_mirrored = o.tile_base_mirrored;
            }
            continue;
        case ScriptStep::Kind::call_and_show: // 6655: then the frame as it is
            o.routine = s.next_routine;
            run_routine(o, s.routine);
            load_frame(o);
            return;
        case ScriptStep::Kind::object_tiles: // 6675 (7743)
            tiles.object_request = s.value;
            continue;
        case ScriptStep::Kind::sound: // 6689: with bit 7 set only when on screen
            if (s.value & 0x80) {
                if (!(o.display & display_flag::offscreen)) {
                    sound_.request(s.value);
                }
            } else {
                sound_.request(u8(s.value | 0x80));
            }
            continue;
        case ScriptStep::Kind::nothing: // 656F
            continue;
        case ScriptStep::Kind::invalid:
            throw std::runtime_error("Object script of type " + std::to_string(o.script_type) +
                                     " ran past its data");
        default: // Triple Trouble's commands
            throw std::logic_error("Sonic 2 script with another revision's step");
        }
    }
}

// 5F6C: the sprite frame record (bank 1F, table 8000 by type): sprite count,
// collision half sizes, layout address, then the tile part.
void Game::load_frame(Object& o) {
    const unsigned frames = rom.word(rom::sprite_frames, o.type * 2u);
    const unsigned record = rom.word(0x1F, frames + o.frame * 2u);
    o.sprite_count = rom.byte(0x1F, record);
    o.width = rom.byte(0x1F, record + 1);
    o.height = rom.byte(0x1F, record + 2);
    o.layout = rom.word(0x1F, record + 3);
    o.tiles = record + 5;
}

// 5FF9: the current frame's routine.
void Game::run_object_handler(Object& o) {
    run_routine(o, o.routine);
}

// 603C: an object more than a screen away gets bit 6 of `display`; one more
// than about 384 pixels away is removed (placed objects wait as type FE until
// their spawn point is out of range too). Bit 1 exempts the object; bit 0
// removes it as soon as it leaves the screen.
void Game::check_off_screen(Object& o) {
    if (o.display & display_flag::keep_on_screen) {
        return;
    }
    o.display &= u8(~display_flag::offscreen);
    const auto near = [](unsigned position, unsigned camera) {
        const unsigned d = ((position - camera) & 0xFFFF) >> 2;
        return (d & 0xFF) < 0x40;
    };
    if (near(o.x, camera.x) && near(o.y, camera.y)) {
        return;
    }
    if (in_wide_view(o.x, o.y)) {
        return; // Widescreen: on screen anywhere inside the presented view.
    }
    if (o.display & 0x01) {
        delete_object(o);
        return;
    }
    o.display |= display_flag::offscreen;
    const auto in_range = [](unsigned position, unsigned camera) {
        return ((position + 0x180 - camera) & 0xFFFF) >> 8 < 4;
    };
    if (in_range(o.x, camera.x) && in_range(o.y, camera.y)) {
        return;
    }
    o.type = o.placement ? 0xFE : 0xFF;
    o.state = 0;
}

// 60D2: a removed placed object is deleted once its spawn point is out of
// range, so that it can be placed again later.
void Game::check_respawn_window(Object& o) {
    const auto in_range = [](unsigned position, unsigned camera) {
        return ((position + 0x180 - camera) & 0xFFFF) >> 8 < 4;
    };
    if (in_range(o.origin_x, camera.x) && in_range(o.origin_y, camera.y)) {
        return;
    }
    o.type = object_type::removed;
    o.state = 0;
}

// 6108: frees the slot (and its placement record).
void Game::delete_object(Object& o) {
    if (o.placement) {
        spawner.placement_spawned[o.placement - 1] = 0;
    }
    o = Object{};
}

// 6004: puts `type` with `param` in the first free slot of 1-16.
void Game::spawn_object(u8 type, u8 param) {
    for (int i = 1; i <= 16; ++i) {
        Object& o = objects[i];
        if (!o.type) {
            o.type = type;
            o.param = param;
            return;
        }
    }
}

// 6025: the first free slot of 8-18, for objects spawned by objects.
// IY is left on the slot found (or, when none is free, one past slot 19:
// DBC0); spawned_slot_ keeps its number for routines that use IY later
// (9D6A).
Object* Game::find_free_slot() {
    for (int i = 8; i < 19; ++i) {
        if (!objects[i].type) {
            spawned_slot_ = u8(i + 1);
            return &objects[i];
        }
    }
    spawned_slot_ = 0x1C;
    return nullptr;
}

// 6376 / 6385: slot number (1-based) and slot address conversions.
int Game::slot_number(const Object& o) const {
    return int(&o - &objects[0]) + 1;
}

} // namespace sonic2_gg
