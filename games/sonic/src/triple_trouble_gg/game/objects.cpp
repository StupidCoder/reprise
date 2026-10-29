// The object engine: scripts (5A60), sprite frames (5AED) and the player's
// update (3431).

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

namespace {
// Bank 0F, 8000: per object type a table of its frames; a frame's record is
// the sprite count, two bytes for collisions, the address of its sprite
// offsets (bank 0D), then the header the sprite code reads (sprites.cpp).
constexpr Table frame_tables{0x0F, 0x8000};
// 0200: 256 signed sine bytes (64 = 1).
constexpr Table sine_table{0x00, 0x0200};
// 589B: the hit flash colour by zone (5888).
constexpr Table boss_flash_colors{0x01, 0x589B};
// 1C:8156: which objects are seen, by their position relative to the
// camera in 16-pixel cells (32 x 32, from 128 pixels left of and above the
// camera): 0 and 1 seen, 2 not drawn, 3 removed.
constexpr Table visibility{0x1C, 0x8156};
} // namespace

// 4F3F: the other objects (slots 2-23), in order.
Flow Game::update_objects() {
    if (act.objects_frozen) {
        co_return;
    }
    objects[0].touch_sides = 0;
    ++flow.object_updates; // D142
    // 4F75 (4FE6): with the water line armed each object's update runs
    // with D133 set, as the player's does.
    const bool marked = video.water_line_mode;
    if (!act.object_mode && flow.mode == 0x11) {
        throw Unrecovered("objects in mode 11 (4F82)");
    }
    for (unsigned i = 2; i < objects.size(); ++i) {
        co_await update_object(objects[i], marked);
    }
}

// 4FC0: an object's script and routine; then, unless it is in state 0,
// whether it is seen or removed (5456). Types F0 and above are being
// removed (509C).
Flow Game::update_object(Object& o, bool marked) {
    if (!o.type) {
        co_return;
    }
    if (o.type >= 0xF0) {
        remove_object(o);
        co_return;
    }
    if (marked) {
        video.busy = 0xFF;
    }
    co_await animate_object(o); // 5A60
    if (marked) {
        video.busy = 0;
    }
    if (o.type >= 0xF0) {
        remove_object(o);
        co_return;
    }
    if (marked) {
        video.busy = 0xFF;
    }
    co_await run_routine(o, o.routine); // 50D6
    if (marked) {
        video.busy = 0;
    }
    if (o.state) {
        check_visibility(o); // 5456
    }
}

// 509C: type FE becomes FF (54F1); FF is deleted (515D): its placement may
// spawn again, and the whole record is cleared. Other types F0-FD: nothing.
void Game::remove_object(Object& o) {
    if (o.type == 0xFE) {
        o.type = 0xFF;
        o.state = 0;
    } else if (o.type == 0xFF) {
        delete_object(o);
    }
}

// 515D
void Game::delete_object(Object& o) {
    if (o.placement != 0xFF && o.placement) {
        placements.spawned[o.placement - 1u] = 0;
    }
    o = {};
}

// 5456: outside mode 0D, an object far from the camera is no longer drawn
// (display bit 6) and, unless kept (display bit 1) or of type 08 or 13,
// removed: through FE when it came from a placement.
void Game::check_visibility(Object& o) {
    if (flow.mode == 0x0D) {
        return;
    }
    o.display &= u8(~display_flag::offscreen);
    const unsigned dx = (o.x + 0x80u - camera.x) & 0xFFFF;
    const unsigned dy = (o.y + 0x80u - camera.y) & 0xFFFF;
    u8 seen = 3;
    if (dx < 0x200 && dy < 0x200) {
        seen = rom.byte(visibility, (dy >> 1 & 0xF8) * 4u + ((dx >> 1) >> 3 & 0x1F));
    }
    if (in_wide_view(o.x, o.y)) {
        return; // Widescreen: on screen anywhere inside the presented view.
    }
    if (seen != 3) {
        if (seen & 0x02) {
            o.display |= display_flag::offscreen;
        }
        return;
    }
    o.display |= display_flag::offscreen;
    if (in_wide_view(o.x, o.y, 64)) {
        return; // Widescreen: kept in a ring outside the view, as 5456's is.
    }
    if (o.display & 0x02) {
        return;
    }
    if (o.type == 0x08 || o.type == 0x13) {
        return;
    }
    o.type = o.placement ? 0xFE : 0xFF;
    o.state = 0;
}

// 50E1: an object of `type` with `param` in the first free slot from 2
// (D580; at most 16 tried); the spawner's byte 31 gets its own slot
// number plus 1.
Object* Game::spawn_object(Object& spawner, u8 type, u8 param) {
    for (unsigned i = 2; i < 18; ++i) {
        auto& n = objects[i];
        if (n.type) {
            continue;
        }
        n.type = type;
        n.param = param;
        spawner.slot_tag = u8(slot_number(spawner) + 1);
        return &n;
    }
    return nullptr;
}

// 510A: the same at a position (X, Y).
Object* Game::spawn_object_at(Object& spawner, u8 type, u8 param, std::uint16_t x,
                              std::uint16_t y) {
    for (unsigned i = 2; i < 18; ++i) {
        auto& n = objects[i];
        if (n.type) {
            continue;
        }
        n.type = type;
        n.param = param;
        n.y = y;
        n.x = x;
        spawner.slot_tag = u8(slot_number(spawner) + 1);
        return &n;
    }
    return nullptr;
}

// A block of the level map changes, and 2CEB draws it into the name table
// where the probed point (D405, D407) falls.
void Game::set_map_block(std::uint16_t cell, u8 block) {
    set_map_block_at(cell, block, probe.x, probe.y);
}

// The same drawn where (x, y) falls (2D17 uses D40D, D40F): its 4x4
// entries at the point's 32-pixel cell (modulo the table's 256 x 224
// pixels).
void Game::set_map_block_at(std::uint16_t cell, u8 block, std::uint16_t x, std::uint16_t y) {
    if (!cell || cell >= level_map.cells.size()) {
        throw Unrecovered("map block write outside the map");
    }
    level_map.cells[cell] = block;
    const unsigned tiles = rom.word(level.block_bank, level.block_table + block * 2u);
    const unsigned row = ((y & 0xFFE0u) >> 3) % Display::map_rows;
    const unsigned column = ((x >> 5) & 7) * 4;
    for (unsigned r = 0; r < 4; ++r) {
        for (unsigned c = 0; c < 4; ++c) {
            display.map[(row + r) * Display::map_columns + column + c] =
                rom.word(level.block_bank, tiles + r * 8 + c * 2);
        }
    }
}

// 33AB: a ring: BCD; at 100 the hundreds count up (332A).
void Game::add_ring() {
    score.rings_low = bcd_add(score.rings_low, 1);
    if (score.rings_low) {
        show_rings(); // 33BA
        return;
    }
    add_hundred_rings();
}

// 332A: a hundred rings; a new hundred gives a life (with its song, unless
// a power-up's song plays).
void Game::add_hundred_rings() {
    score.rings_high = bcd_add(score.rings_high, 1);
    if (player.rings_rewarded >= score.rings_high) {
        show_rings();
        return;
    }
    player.rings_rewarded = score.rings_high; // 333E
    extra_life_now();                         // 3342
}

// 5142: the first free slot from 8 (D700) on.
Object* Game::find_free_slot() {
    for (unsigned i = 8; i < objects.size(); ++i) {
        if (!objects[i].type) {
            return &objects[i];
        }
    }
    return nullptr;
}

// An object's address in the original (D500 + 64 per slot), as objects
// keep each other's.
std::uint16_t Game::object_address(const Object& o) const {
    return std::uint16_t(0xD500 + 0x40 * slot_number(o));
}

Object& Game::object_at(std::uint16_t address) {
    const unsigned slot = (address - 0xD500u) / 0x40;
    if (address < 0xD500 || slot >= objects.size()) {
        throw Unrecovered("object address outside the slots");
    }
    return objects[slot];
}

// 5CA6 / 5EA3: script commands 4 and 16: a new object at an offset (X
// mirrored with the object), facing the same way, with the same tile
// bases; the parameter (command 4) or the angle plus the object's
// (command 16). Without a free slot nothing happens. The slot's other bytes
// keep what its last object left.
void Game::spawn_from_script(Object& o, const ScriptStep& s) {
    Object* n = find_free_slot();
    script_spawned_ = n; // IY afterwards (0 without a slot)
    if (!n) {
        return;
    }
    n->type = s.type;
    if (s.kind == ScriptStep::Kind::spawn) {
        n->param = s.value;
    }
    const std::uint16_t dx = (o.display & display_flag::mirrored) ? std::uint16_t(-s.dx) : s.dx;
    n->x = n->origin_x = std::uint16_t(o.x + dx);
    n->y = n->origin_y = std::uint16_t(o.y + s.dy);
    if (s.kind == ScriptStep::Kind::spawn_turned) {
        n->angle = u8(s.value + o.angle);
    }
    n->display = o.display & display_flag::mirrored;
    n->tile_base = o.tile_base;
    n->tile_base_mirrored = o.tile_base_mirrored;
}

// 3431: the player's update: its script and routine, then the checks that
// follow every frame (water, damage, falling out of the act, ...).
Flow Game::update_player() {
    if (act.objects_frozen) {
        co_return;
    }
    auto& p = objects[0];
    if (!p.type) {
        co_return;
    }
    if ((p.status & 0x02) && objects[0].status_ticks) {
        if (--p.status_ticks == 0) {
            p.status &= u8(~0x02);
        }
    }
    video.busy = 0xFF;
    co_await animate_object(p);
    video.busy = 0xFF;
    co_await run_routine(p, p.routine); // 50D6
    video.busy = 0;
    if (player.hold_x) {
        p.x = 0x004A;
    }
    if (!player.checks_off) {
        co_await player_frame_checks();
    }
    limit_player_speed();        // 349A
    co_await player_power_ups(); // 0C:B811
}

// 5A60: the object's script runs on when the current step's updates are
// used up, or at once when another state was asked for (unless flag bit 3
// makes such a change wait for the script's restart command).
Flow Game::animate_object(Object& o) {
    if (!o.script.running()) {
        co_await start_script(o);
        co_return;
    }
    if (!(o.flags & 0x08) && o.next_state != o.state) {
        o.state = o.next_state; // 5A80
        co_await start_script(o);
        co_return;
    }
    if (--o.frame_ticks == 0) {
        co_await run_script(o);
    }
}

// 5A86: the script of the object's current state from its start.
Flow Game::start_script(Object& o) {
    o.routine = Routine::none;
    if (o.type >= 0xF0) {
        start_removed_script(o);
        co_return;
    }
    o.script = {o.type, o.state, 0, script_bank_};
    co_await run_script(o);
}

// An object that made itself removed (type FE, FF) in a script call and
// then restarts its script: the original looks its script up as if the
// type were valid and runs a garbage first step into its frame bytes
// before the object loop removes it (4FD0). Such an object is never drawn
// and is wiped the next frame, so nothing of that matters here.
void Game::start_removed_script(Object& o) {
    o.script = {};
}

// 5AAF: steps run until one shows a frame.
Flow Game::run_script(Object& o) {
    for (;;) {
        const auto& steps = scripts_.script(o.script.type, o.script.state, o.script.bank);
        if (o.script.step >= steps.size()) {
            throw Unrecovered("object script ran past its end");
        }
        const ScriptStep& s = steps[o.script.step];
        ++o.script.step;
        auto check = [&](Routine r, std::uint16_t address) {
            if (r == Routine::unknown) {
                throw Unrecovered(
                    "routine " + std::to_string(address) + " (bank " +
                    std::to_string(o.script.bank ? o.script.bank : scripts_.bank(o.script.type)) +
                    ") of object type " + std::to_string(o.type));
            }
        };
        switch (s.kind) {
        case ScriptStep::Kind::frame:
            check(s.routine, s.routine_address);
            o.frame_ticks = s.ticks;
            o.frame = s.frame;
            o.routine = s.routine;
            load_frame(o);
            co_return;
        case ScriptStep::Kind::frame_keep:
            o.frame_ticks = s.ticks;
            o.frame = s.frame;
            load_frame(o);
            co_return;
        case ScriptStep::Kind::restart: // 5C40
            if (o.state != o.next_state) {
                o.state = o.next_state;
            }
            co_await start_script(o);
            co_return;
        case ScriptStep::Kind::call: // 5C4C
            check(s.routine, s.routine_address);
            co_await run_routine(o, s.routine);
            break;
        case ScriptStep::Kind::velocity: // 5C62
            o.vx = (o.display & display_flag::mirrored) ? std::uint16_t(-s.vx) : s.vx;
            o.vy = s.vy;
            break;
        case ScriptStep::Kind::request_state: // 5C92
            o.next_state = s.value;
            break;
        case ScriptStep::Kind::call_and_show: // 5D28
            check(s.routine, s.routine_address);
            check(s.next_routine, s.next_routine_address);
            o.routine = s.next_routine;
            co_await run_routine(o, s.routine);
            load_frame(o);
            co_return;
        case ScriptStep::Kind::sound: // 5D5C
            sound_.request(s.value, 1);
            break;
        case ScriptStep::Kind::jump: // 5D84
            o.script.step = s.target;
            break;
        case ScriptStep::Kind::branch_if: // 5D97
            check(s.routine, s.routine_address);
            co_await run_routine(o, s.routine);
            if (routine_carry_) {
                o.script.step = s.target;
            }
            break;
        case ScriptStep::Kind::show: { // 5E27
            check(s.routine, s.routine_address);
            o.frame_ticks = s.ticks;
            o.frame = (o.angle & 0x10) ? s.frame_mirrored : s.frame;
            o.routine = s.routine;
            load_frame(o);
            co_return;
        }
        case ScriptStep::Kind::set_loop: // 5E53
            o.loop_count = s.value;
            break;
        case ScriptStep::Kind::loop: // 5E67
            if (--o.loop_count) {
                o.script.step = s.target;
            }
            break;
        case ScriptStep::Kind::and_field: // 5DEF
            set_object_byte(o, s.offset, u8(object_byte(o, s.offset) & s.value));
            break;
        case ScriptStep::Kind::or_field: // 5E0B
            set_object_byte(o, s.offset, u8(object_byte(o, s.offset) | s.value));
            break;
        case ScriptStep::Kind::set_field: // 5DBF
            set_object_byte(o, s.offset, s.value);
            break;
        case ScriptStep::Kind::music: // 5D70
            sound_.request(s.value, 2);
            break;
        case ScriptStep::Kind::spawn:
        case ScriptStep::Kind::spawn_turned:
            spawn_from_script(o, s);
            break;
        case ScriptStep::Kind::poke: // 5DD9: a byte of work RAM
            if (s.address == 0xDBD1) {
                act.machine_down = s.value;
                break;
            }
            if (s.address == 0xDBD7) {
                player.in_scene = s.value;
                break;
            }
            throw Unrecovered("script write to " + std::to_string(s.address));
        case ScriptStep::Kind::invalid:
            throw Unrecovered("object script command at " + std::to_string(s.source));
        default: // Sonic 2's commands
            throw std::logic_error("Triple Trouble script with another revision's step");
        }
    }
}

// The object bytes a script command names by their offset in the record
// (the position's bytes included: 11-12 X, 14-15 Y).
u8 Game::object_byte(const Object& o, u8 offset) const {
    switch (offset) {
    case 0x03:
        return o.flags;
    case 0x04:
        return o.display;
    case 0x08:
        return o.tile_base;
    case 0x09:
        return o.tile_base_mirrored;
    case 0x0A:
        return o.angle;
    case 0x0B:
        return o.speed;
    case 0x11:
        return lo(o.x);
    case 0x12:
        return hi(o.x);
    case 0x14:
        return lo(o.y);
    case 0x15:
        return hi(o.y);
    case 0x1E:
        return o.counter;
    case 0x1F:
        return o.timer;
    case 0x26:
        return o.status_ticks;
    case 0x34:
    case 0x35:
    case 0x36:
    case 0x37:
    case 0x38:
    case 0x39:
        return o.scratch[offset - 0x34u];
    default:
        throw Unrecovered("script access to object byte " + std::to_string(offset));
    }
}

void Game::set_object_byte(Object& o, u8 offset, u8 v) {
    switch (offset) {
    case 0x03:
        o.flags = v;
        return;
    case 0x04:
        o.display = v;
        return;
    case 0x08:
        o.tile_base = v;
        return;
    case 0x09:
        o.tile_base_mirrored = v;
        return;
    case 0x0A:
        o.angle = v;
        return;
    case 0x0B:
        o.speed = v;
        return;
    case 0x11:
        o.x = std::uint16_t((o.x & 0xFF00) | v);
        return;
    case 0x12:
        o.x = std::uint16_t((o.x & 0x00FF) | v << 8);
        return;
    case 0x14:
        o.y = std::uint16_t((o.y & 0xFF00) | v);
        return;
    case 0x15:
        o.y = std::uint16_t((o.y & 0x00FF) | v << 8);
        return;
    case 0x1E:
        o.counter = v;
        return;
    case 0x1F:
        o.timer = v;
        return;
    case 0x26:
        o.status_ticks = v;
        return;
    case 0x34:
    case 0x35:
    case 0x36:
    case 0x37:
    case 0x38:
    case 0x39:
        o.scratch[offset - 0x34u] = v;
        return;
    default:
        throw Unrecovered("script access to object byte " + std::to_string(offset));
    }
}

// 5AED: the frame's record (bank 0F, by type and frame number). An object
// that removed itself (type FE, FF) earlier in the same script pass looks
// its frame up in a garbage table in the original; it is wiped before it
// is drawn, so nothing is looked up here.
void Game::load_frame(Object& o) {
    if (o.type >= 0xFE) {
        return;
    }
    const unsigned frames = rom.word(frame_tables, o.type * 2u);
    const unsigned record = cpu_word(frame_tables.bank, frames + o.frame * 2u);
    // A frame number past a type's frames can point below 8000 (a ring
    // before the HUD ring's first spin, D3DD 0, takes frame 3C: 0C0A).
    o.sprite_count = cpu_byte(frame_tables.bank, record);
    o.frame_width = cpu_byte(frame_tables.bank, record + 1);
    o.frame_height = cpu_byte(frame_tables.bank, record + 2);
    o.layout = cpu_word(frame_tables.bank, record + 3);
    o.frame_header = std::uint16_t(record + 5);
}

// 5A3B: terrain contact and (unless the player is being carried) the sides
// touched by objects.
void Game::update_contact(Object& o) {
    u8 b = o.terrain;
    if (player.carried || (!(o.flags & 0x02) && !(objects[0].flags & 0x80))) {
        b |= u8((o.touch_sides >> 4) & 0x0F);
    }
    o.contact = b;
}

// 349A: the player's horizontal speed is at most 6 pixels per frame.
void Game::limit_player_speed() {
    auto& vx = objects[0].vx;
    if (!(vx & 0x8000)) {
        if (vx >= 0x0600) {
            vx = 0x0600;
        }
    } else if (vx < 0xFA00) {
        vx = 0xFA00;
    }
}

// A byte as the CPU reads it with `bank` in slot 2: below 4000 bank 0,
// below 8000 bank 1. The cartridge's frame records are read this way. One
// part of RAM is read so too: frames whose sprite offsets an object writes
// at run time point into the shared work bytes (DBE6-DC45: Meta
// Junglira's arm) or D483 (the sparkle's).
u8 Game::cpu_byte(unsigned bank, unsigned address) const {
    address &= 0xFFFF;
    if (address >= 0xDBE6 && address < 0xDBE6 + act.work.size()) {
        return act.work[address - 0xDBE6];
    }
    if (address >= 0xD483 && address < 0xD483 + animation.sparkle_offsets.size()) {
        return animation.sparkle_offsets[address - 0xD483]; // the sparkle's (type 5)
    }
    if (address >= 0xC000) {
        throw Unrecovered("cartridge data read from RAM at " + std::to_string(address));
    }
    return rom.byte(address < 0x4000 ? 0x00 : address < 0x8000 ? 0x01 : bank, address);
}

std::uint16_t Game::cpu_word(unsigned bank, unsigned address) const {
    return std::uint16_t(cpu_byte(bank, address) | cpu_byte(bank, address + 1) << 8);
}

// A routine moves the object's script to the step at `address` (in its
// script's bank), as the original does by writing IX+0E/0F.
void Game::jump_script(Object& o, unsigned address) {
    o.script.step = scripts_.step_at(o.script.type, o.script.state, address, o.script.bank);
}

// 5888 (050D): the colour a hit boss flashes, by zone.
void Game::set_boss_flash_color() {
    act.flash_color = rom.word(boss_flash_colors, flow.zone * 2u);
}

// 52E8 (032D): the speed from the direction (byte 0A, 256 to the turn)
// and the speed (0B, in 1/16 pixel): each part the sine table's byte
// (0200; X by the angle, Y by the angle + C0) times the speed, over 16.
void Game::velocity_from_angle(Object& o) {
    o.vx = 0;
    o.vy = 0;
    if (!o.speed) {
        return;
    }
    const auto part = [&](u8 angle) {
        const int s = std::int8_t(rom.byte(sine_table, angle));
        return std::uint16_t((s * o.speed) >> 4);
    };
    o.vx = part(o.angle);
    o.vy = part(u8(o.angle + 0xC0));
}

// 5359: the position (24 bits: 16 pixels, 8 fraction) plus the speed.
void Game::move_object(Object& o) {
    auto add = [](std::uint16_t& whole, u8& fraction, std::uint16_t speed) {
        const std::uint32_t position = (std::uint32_t(whole) << 8) | fraction;
        const std::uint32_t delta = (speed & 0x8000) ? (0xFF0000u | speed) : speed;
        const std::uint32_t sum = (position + delta) & 0xFFFFFF;
        whole = std::uint16_t(sum >> 8);
        fraction = u8(sum);
    };
    add(o.x, o.x_sub, o.vx);
    add(o.y, o.y_sub, o.vy);
}

// 5379 (045F): Y only.
void Game::move_vertically_only(Object& o) {
    const std::uint32_t position = (std::uint32_t(o.y) << 8) | o.y_sub;
    const std::uint32_t delta = (o.vy & 0x8000) ? (0xFF0000u | o.vy) : o.vy;
    const std::uint32_t sum = (position + delta) & 0xFFFFFF;
    o.y = std::uint16_t(sum >> 8);
    o.y_sub = u8(sum);
}

// 539A (0462): X only.
void Game::move_horizontally_only(Object& o) {
    const std::uint32_t position = (std::uint32_t(o.x) << 8) | o.x_sub;
    const std::uint32_t delta = (o.vx & 0x8000) ? (0xFF0000u | o.vx) : o.vx;
    const std::uint32_t sum = (position + delta) & 0xFFFFFF;
    o.x = std::uint16_t(sum >> 8);
    o.x_sub = u8(sum);
}

} // namespace triple_trouble_gg

namespace triple_trouble_gg {

namespace {
// 1C:8556: per zone and act the object placements: 9 bytes each (type, X
// and Y with their high bytes plus 1, display bits, parameter, tile
// bases); FF ends.
constexpr Table placement_tables{0x1C, 0x8556};
} // namespace

// Bank 1C, 8000: placements near the camera get their object (once, until
// it is removed): those just outside the screen (visibility 2), and on the
// first scan also those on it (0, 1). Not while D3E4 is set (except F8).
void Game::spawn_placements() {
    if (act.variant != 0xF8 && act.variant) {
        return;
    }
    const Table list = act_entry(placement_tables);
    const unsigned left = (camera.x + 0x80u) & 0xFFFF;
    const unsigned top = (camera.y + 0x80u) & 0xFFFF;
    for (unsigned i = 0;; ++i) {
        const Table r = list + i * 9u;
        if (rom.byte(r) == 0xFF) {
            break;
        }
        if (i >= placements.spawned.size()) {
            throw Unrecovered("more than 64 placements");
        }
        if (placements.spawned[i]) {
            continue;
        }
        // 8052: the stored positions are 256 too high, so this is the
        // distance from 128 pixels left of / above the camera.
        bool eligible = false;
        const unsigned dx = (rom.word(r, 1) - left) & 0xFFFF;
        if (!(dx & 0x8000) && dx < 0x200) {
            probe.attribute = u8(dx >> 1); // D100 holds it meanwhile
            const unsigned dy = (rom.word(r, 3) - top) & 0xFFFF;
            if (!(dy & 0x8000) && dy < 0x200) {
                const u8 seen =
                    rom.byte(visibility, (dy >> 1 & 0xF8) * 4u + ((dx >> 1) >> 3 & 0x1F));
                eligible = seen < 3 && (seen == 2 || !placements.scanned);
            }
        }
        if (widescreen()) {
            // Widescreen: the ring the original spawns in lies partly inside
            // a larger view; placements appear in the same 64-pixel ring
            // around the presented view instead (on the first scan also
            // inside it).
            const unsigned x = rom.word(r, 1) - 0x0100u, y = rom.word(r, 3) - 0x0100u;
            eligible = in_wide_view(x, y, 64) && (!placements.scanned || !in_wide_view(x, y));
        }
        if (!eligible) {
            continue;
        }
        const u8 type = rom.byte(r, 0);
        if (type == 0xFE) {
            throw Unrecovered("special placement (1C:80BC)");
        }
        Object* o = find_free_slot(); // 0360 (5142)
        if (!o) {
            continue; // tried again at the next scan
        }
        o->type = type;
        placements.spawned[i] = type;
        o->x = o->origin_x = std::uint16_t(rom.word(r, 1) - 0x0100);
        o->y = o->origin_y = std::uint16_t(rom.word(r, 3) - 0x0100);
        o->display = rom.byte(r, 5);
        o->param = rom.byte(r, 6);
        o->tile_base = rom.byte(r, 7);
        o->tile_base_mirrored = rom.byte(r, 8);
        o->placement = u8(i + 1);
        o->slot_tag = u8(slot_number(*o) + 1); // 035A
    }
    placements.scanned = 1;
}

// 52CA: an object's slot number (0 = D500).
unsigned Game::slot_number(const Object& o) const {
    return unsigned(&o - objects.data());
}

} // namespace triple_trouble_gg
