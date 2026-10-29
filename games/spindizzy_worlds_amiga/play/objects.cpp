#include "spindizzy_worlds_amiga/play/objects.hpp"
#include "spindizzy_worlds_amiga/view/sprites.hpp"

namespace spindizzy::play {
namespace {

using world::ObjectKind;

// Per jewel type (the first parameter byte & $E, a word offset): the fuel
// it gives ($14B5C, also kept for the reserve), the explorer's flash ticks
// it adds ($14B6C, $24F4), how far above the jewel it can be taken
// ($14B7C) and its sound ($14B8C).
constexpr std::uint32_t jewel_fuel_table = 0x14B5C;
constexpr std::uint32_t jewel_flash_table = 0x14B6C;
constexpr std::uint32_t jewel_reach_table = 0x14B7C;
// A lift's pause after reaching an end, per 4-bit index ($14DAE).
constexpr std::uint32_t lift_pauses = 0x14DAE;
constexpr int max_slots = 31; // $144BE: slots 1-31 at $25CC

std::uint8_t parameter(const world::PlacedObject& object, std::size_t i) {
    return i < object.parameters.size() ? object.parameters[i] : 0;
}
std::uint16_t parameter_word(const world::PlacedObject& object, std::size_t i) {
    return std::uint16_t(parameter(object, i) << 8 | parameter(object, i + 1));
}

std::uint8_t high(std::uint16_t word) { return std::uint8_t(word >> 8); }
std::uint8_t low(std::uint16_t word) { return std::uint8_t(word); }

// The rounded whole part of a 16.16 speed ($14EB8: + $8000, swap).
std::int16_t rounded(std::int32_t speed) { return std::int16_t(std::uint32_t(speed + 0x8000) >> 16); }

// Whether the explorer is in the object's cell and between `reach`
// below and the object's height plus `extra` ($14B9C): the object's x and
// y with the low byte $FF minus the explorer's must have a zero high
// byte; (z + extra) − explorer z, unsigned, at most `reach`.
bool explorer_on(const LevelObject& object, const Explorer& e, int extra, int reach, std::int16_t* height = nullptr) {
    const std::uint16_t dx = std::uint16_t((object.x | 0xFF) - e.x);
    const std::uint16_t dy = std::uint16_t((object.y | 0xFF) - e.y);
    if ((dx & 0xFF00) || (dy & 0xFF00)) return false;
    const std::uint16_t dz = std::uint16_t(object.z + extra - e.z);
    if (height) *height = std::int16_t(dz);
    return dz <= std::uint16_t(reach);
}

// A solid object's sides ($14EAE): when the explorer came in from
// outside along an axis this tick (`dx` − its move is outside 0 … `size`),
// the move is undone and the speed reversed and halved.
void push_out(Explorer& e, std::uint16_t dx, std::uint16_t dy, std::uint16_t size) {
    const std::int16_t mx = rounded(e.vx);
    if (std::uint16_t(dx - mx) >= size) {
        e.x = std::int16_t(e.x - mx);
        e.vx = -e.vx >> 1;
    }
    const std::int16_t my = rounded(e.vy);
    if (std::uint16_t(dy - my) < size) return;
    e.y = std::int16_t(e.y - my);
    e.vy = -e.vy >> 1;
}

} // namespace

std::uint16_t next_random(std::uint32_t& state) {
    // $112CA: the word × $F1, $29 added to the product's low word only.
    std::uint32_t product = (state & 0xFFFF) * 0xF1;
    product = (product & 0xFFFF0000u) | ((product + 0x29) & 0xFFFF);
    state = product;
    return std::uint16_t((product & 0xFFFF) + (product >> 16));
}

bool condition_holds(std::uint16_t condition, const world::Switches& switches, std::uint32_t jewels) {
    world::Switches now = switches;
    now.jewels_left = jewels != 0;
    return now.holds(condition);
}

LevelObjects::LevelObjects(const world::Level& level, const data::Memory& main, const SurfaceTable& surfaces,
                           ObjectWorld& w, bool first_visit, int& jewels_found)
    : main_(main), surfaces_(&surfaces) {
    int jewel_index = 0; // $256A: counts every jewel the level places
    int slot = 2;
    for (std::size_t i = 0; i < level.objects.size(); ++i) {
        const auto& placed = level.objects[i];
        if (slot > max_slots) break; // no free slot: the object is not made
        LevelObject o;
        o.kind = ObjectKind(placed.kind);
        o.source = i;
        o.slot = slot;
        // $127F8: positions are the cell's origin (x, y × 256), z the level × 256.
        o.x = std::int16_t(placed.x << 8);
        o.y = std::int16_t(placed.y << 8);
        o.z = std::int16_t(placed.z << 8);
        o.count = 1; // $144BE leaves +$1E to the create routine; lists start at 1
        switch (o.kind) {
        case ObjectKind::jewel: { // $14A18
            const int index = jewel_index++;
            if (first_visit) { // $1392A: bit 31 set: every jewel is there
                ++jewels_found;
                w.jewels |= 1u << index;
            } else if (!(w.jewels >> index & 1)) {
                o.alive = false; // taken before
            }
            o.word2e = std::uint16_t(index);
            o.word2c = parameter(placed, 0) & 0x0E;
            o.count = (((31 - slot) & 7) + 1) * 4; // +$1E high byte; the step +$1F is 0
            break;
        }
        case ObjectKind::lift: // $14CBC
            o.word2c = parameter_word(placed, 0);
            o.word2e = parameter_word(placed, 2);
            o.word26 = parameter_word(placed, 4);
            o.word30 = 0;
            o.word32 = parameter(placed, 6);
            o.word34 = std::uint16_t((parameter(placed, 7) & 7) << 8);
            break;
        case ObjectKind::exit_point: { // $14FC2
            o.word2c = std::uint16_t(parameter(placed, 0) << 8 | parameter(placed, 1)); // flags, destination
            o.word26 = parameter_word(placed, 2);
            o.word2e = parameter_word(placed, 4);
            const int flags = parameter(placed, 0), destination = parameter(placed, 1) & 15;
            // Bit 6: gone once the destination level has no jewels left
            // (its long is 0: visited and emptied).
            if ((flags & 0x40) && destination != 0 && w.level_jewels[destination - 1] == 0) o.alive = false;
            o.count = 1;
            break;
        }
        case ObjectKind::push_button: // $16048
            o.word2c = parameter(placed, 0) & 7;
            break;
        case ObjectKind::binary_switch: { // $16154
            const int colour = parameter(placed, 0) & 3;
            o.word2c = std::uint16_t(8 >> colour);
            o.word32 = std::uint16_t(colour + 0x18);
            o.word2e = std::uint16_t(std::uint8_t(parameter(placed, 1) - w.rotation) << 8);
            o.word30 = std::uint16_t(colour * 8);
            break;
        }
        case ObjectKind::heads: // $1594A
            o.word2c = std::uint16_t(parameter(placed, 0) << 8);
            o.word32 = parameter(placed, 1) & 3;
            o.word26 = parameter_word(placed, 2);
            o.word30 = std::uint16_t(o.z);
            o.word2e = condition_holds(o.word26, w.switches, w.jewels) ? 0x200 : 0; // z follows at its first tick
            break;
        case ObjectKind::secret_passage: // $15404
            o.word2c = parameter(placed, 0);
            break;
        case ObjectKind::timer: { // $1530C
            const int seconds = parameter(placed, 0);
            int sum = (w.timer >> 8) + seconds;
            if (seconds >= 0x63) sum = 0x63;
            w.timer = std::uint16_t((sum & 0xFF) << 8 | (w.timer & 0xFF));
            o.word2c = std::uint16_t(parameter(placed, 1) << 8);
            if (!(parameter(placed, 1) & 0x80)) w.timer = std::uint16_t(seconds << 8 | 0xF0); // not "Relative"
            break;
        }
        case ObjectKind::message: // $16002 (the editor's test mode $24BE deletes them)
            o.word2c = std::uint16_t(parameter(placed, 0) << 8);
            o.count = parameter(placed, 1) + 1;
            break;
        case ObjectKind::boink: // $151C4
            o.word32 = 0;
            o.word2e = std::uint16_t(std::int8_t(parameter(placed, 0)));      // gravity
            o.word2c = std::uint16_t(std::int8_t(parameter(placed, 1)) * 4);  // speed
            o.x = std::int16_t(o.x | 0x80);
            o.y = std::int16_t(o.y | 0x80);
            o.word26 = parameter_word(placed, 2);                              // the set (+$2A)
            o.heading = std::int16_t(o.word26 + w.rotation);
            o.count = 1; // +$1E = $100
            break;
        case ObjectKind::pac: // $1544A: the start cell kept; then $15456
            o.parameters = placed.parameters;
            o.word34 = std::uint16_t((o.x & 0xFF00) | (std::uint16_t(o.y) >> 8));
            start_pac(o, w);
            break;
        case ObjectKind::pyramid: { // $157F6: speed p0, set p1 (heading = set + rotation)
            const std::int16_t speed = std::int16_t(parameter(placed, 0));
            o.word30 = std::uint16_t(speed);
            o.word26 = parameter(placed, 1);
            o.heading = std::int16_t((o.word26 + w.rotation) & 3);
            o.x = std::int16_t(o.x | 0x80);
            o.y = std::int16_t(o.y | 0x80);
            o.target_x = o.target_y = std::int16_t(0x8000);
            const std::int16_t sx[] = {speed, 0, std::int16_t(-speed), 0}, sy[] = {0, speed, 0, std::int16_t(-speed)};
            o.step_x = sx[o.heading];
            o.step_y = sy[o.heading];
            o.count = 1;
            break;
        }
        case ObjectKind::spark_generator: // $15C10: condition, period (+$2C), direction (+$28)
            o.word26 = parameter_word(placed, 0);
            o.word2c = parameter(placed, 2);
            o.heading = parameter(placed, 3);
            o.word2e = std::uint16_t((parameter(placed, 3) << 4) & 0xFC);
            o.count = 1;
            break;
        case ObjectKind::clue: // $16340: a clue word for the panel; 0 takes $361C (not set: 0)
            o.word26 = parameter_word(placed, 0);
            break;
        default: break;
        }
        objects_.push_back(o);
        if (o.alive) ++slot;
    }
    reapply(w);
}

void LevelObjects::reapply(ObjectWorld& w) const {
    // $15404 / $15430: a secret passage's column. Bit 7 of its parameter
    // clears count + 1 cells upward; otherwise the bottom cell becomes
    // block 1 and the `count` cells above it are cleared (the dbra jumps
    // into the clearing loop after the first store). The tile map was
    // built before the objects, so the passage still looks like a wall.
    for (const auto& o : objects_) {
        if (!o.alive || o.kind != ObjectKind::secret_passage) continue;
        const int x = o.x >> 8 & 31, y = o.y >> 8 & 31;
        int z = o.z >> 8 & 7;
        const int count = o.word2c & 7;
        for (int i = 0; i <= count && z < world::level_height; ++i, ++z) {
            const bool solid = i == 0 && !(o.word2c & 0x80);
            w.grid.set_type(x, y, z, solid ? 1 : 0);
        }
    }
}

void LevelObjects::rotate(int turns, ObjectWorld& w) {
    auto turn_cell = [&](std::int16_t& x, std::int16_t& y, int mask) { // $12A08
        for (int i = 0; i < (turns & 3); ++i) {
            const std::int16_t old_x = x;
            x = std::int16_t(y ^ mask);
            y = old_x;
        }
    };
    for (auto& o : objects_) {
        if (!o.alive) continue;
        turn_cell(o.x, o.y, 0x1F00); // $12DDE
        switch (o.kind) {
        case ObjectKind::binary_switch: o.word2e = std::uint16_t(o.word2e + (turns << 8)); break; // $161AE
        case ObjectKind::boink: // $1520C
            o.heading = std::int16_t(o.heading + turns);
            o.x = std::int16_t(o.x | 0x80);
            o.y = std::int16_t(o.y | 0x80);
            break;
        case ObjectKind::pac: { // $154E4: target, start cell and step with it
            o.heading = std::int16_t(o.heading + turns);
            turn_cell(o.target_x, o.target_y, 0x1F00);
            std::int16_t sx = std::int16_t(o.word34 >> 8 & 31), sy = std::int16_t(o.word34 & 31);
            turn_cell(sx, sy, 0x1F);
            o.word34 = std::uint16_t(sx << 8 | (sy & 0xFF));
            o.word26 = std::uint16_t(w.grid.at(o.x >> 8 & 31, o.y >> 8 & 31, o.z >> 8 & 7) << 8 | (o.word26 & 0xFF)); // $154D0
            o.heading &= 3; // $154A2
            const std::int16_t speed = std::int16_t(o.word30 >> 8);
            const std::int16_t ax[] = {speed, 0, std::int16_t(-speed), 0}, ay[] = {0, speed, 0, std::int16_t(-speed)};
            o.step_x = ax[o.heading];
            o.step_y = ay[o.heading];
            break;
        }
        case ObjectKind::pyramid: { // $15850
            o.heading = std::int16_t((o.heading + turns) & 3);
            turn_cell(o.target_x, o.target_y, 0x1F00);
            const std::int16_t speed = std::int16_t(o.word30);
            const std::int16_t ax[] = {speed, 0, std::int16_t(-speed), 0}, ay[] = {0, speed, 0, std::int16_t(-speed)};
            o.step_x = ax[o.heading];
            o.step_y = ay[o.heading];
            break;
        }
        default: break;
        }
    }
    reapply(w); // secret passages at their new cells ($15430)
}

void LevelObjects::tick(ObjectWorld& w, ObjectEvents& events) {
    for (auto& o : objects_) {
        if (!o.alive) continue;
        switch (o.kind) {
        case ObjectKind::jewel: tick_jewel(o, w, events); break;
        case ObjectKind::lift: tick_lift(o, w, events); break;
        case ObjectKind::exit_point: tick_exit(o, w, events); break;
        case ObjectKind::push_button: tick_button(o, w, events); break;
        case ObjectKind::binary_switch: tick_switch(o, w, events); break;
        case ObjectKind::heads: tick_heads(o, w, events); break;
        case ObjectKind::timer: tick_timer(o, w, events); break;
        case ObjectKind::message: tick_message(o, events); break;
        case ObjectKind::boink: tick_boink(o, w, events); break;
        case ObjectKind::pac: tick_pac(o, w, events); break;
        case ObjectKind::pyramid: tick_pyramid(o, w, events); break;
        case ObjectKind::spark_generator: tick_spark(o, w, events); break;
        default: break;
        }
    }
}

void LevelObjects::tick_jewel(LevelObject& o, ObjectWorld& w, ObjectEvents& events) { // $14B04
    const auto type = std::uint32_t(o.word2c);
    const int reach = main_.u16(jewel_reach_table + type);
    if (!explorer_on(o, w.explorer, reach, reach + 0x180)) return;
    w.jewels &= ~(1u << (o.word2e & 31));
    const auto fuel = std::int16_t(main_.u16(jewel_fuel_table + type));
    w.fuel_change += std::int32_t(std::uint32_t(fuel) << 16); // add.w to the long's high word
    w.jewel_fuel = std::int16_t(w.jewel_fuel + fuel);
    w.explorer.timer_a = std::int16_t(w.explorer.timer_a + std::int16_t(main_.u16(jewel_flash_table + type)));
    ++w.jewels_taken;
    events.sounds.push_back({0, main_.u16(0x14B8C + type) & 0xFF, 0x40}); // $14B46
    o.alive = false; // $1449E
}

void LevelObjects::tick_lift(LevelObject& o, ObjectWorld& w, ObjectEvents& events) { // $14D46
    o.speed = 0;
    if (condition_holds(o.word26, w.switches, w.jewels) && o.word30-- == 0) {
        o.word30 = 0;
        int pause_index;
        bool turn = false;
        hear(o, events, (o.word2e & 0x8000) ? 0x0C : 0x0B, loudness(o, w.explorer, false)); // $14D6A
        if (!(o.word2e & 0x8000)) { // going up: "UpSpeed", up to the level "Max z"
            o.speed = std::int16_t(low(o.word2e) & 0x7F);
            o.z = std::int16_t(o.z + o.speed);
            pause_index = low(o.word2c);
            turn = std::int8_t(low(o.word2c) & 15) <= std::int8_t(high(std::uint16_t(o.z)));
        } else { // going down: "DnSpeed", down to "Min_z"
            o.speed = std::int16_t(-(high(o.word2e) & 0x7F));
            o.z = std::int16_t(o.z + o.speed);
            pause_index = high(o.word2c);
            if (o.z < 0) {
                o.z = 0;
                turn = true;
            } else {
                const std::int16_t bottom = std::int16_t((high(o.word2c) & 15) << 8);
                if (bottom > o.z) {
                    o.z = bottom;
                    o.speed = 0;
                    turn = true;
                }
            }
        }
        if (turn) {
            o.word30 = main_.u16(lift_pauses + std::uint32_t((pause_index & 0xF0) >> 3));
            o.word2e ^= 0x8000;
        }
    }
    // $14E26: the explorer on the platform or against its sides.
    auto& e = w.explorer;
    const std::uint16_t dx = std::uint16_t(e.x - o.x), dy = std::uint16_t(e.y - o.y);
    if (dx >= 0x100 || dy >= 0x100) return;
    const std::int16_t below = std::int16_t(o.z + 8 - e.z);
    if (below < 0) return;
    if (!(o.word32 & 0x80)) {                        // not a column down to the floor
        if (o.word32 & 0x40) {                       // tall
            if (below >= 0x2FF) return;
        } else if (below >= 0x1FF) {
            return;
        }
    }
    if (below < 0x80) { // on top: carried
        e.z = std::int16_t(o.z + 8);
        e.contact |= contact::on_lift | contact::traction;
        e.friction = 4;
        if (o.speed >= std::int16_t(e.vz >> 16)) e.vz = std::int32_t(std::uint32_t(o.speed) << 16 | (std::uint32_t(e.vz) & 0xFFFF));
        return;
    }
    push_out(e, dx, dy, 0x100);
}

void LevelObjects::tick_exit(LevelObject& o, ObjectWorld& w, ObjectEvents& events) { // $15044
    o.byte20 = 0;
    if (!condition_holds(o.word26, w.switches, w.jewels)) return;
    const int flags = high(o.word2c);
    if (!(flags & 0x80)) o.byte20 = 0xFF; // shown
    if (!w.running) return;
    const auto& e = w.explorer;
    if (!(flags & 0x20)) { // "Ignore xy" clear: the explorer's cell
        if ((std::uint16_t((o.x | 0xFF) - e.x) & 0xFF00) || (std::uint16_t((o.y | 0xFF) - e.y) & 0xFF00)) return;
    }
    if (!(flags & 0x10) && (std::uint16_t(o.z + 0x40 - e.z) & 0xFE00)) return; // "Ignore z" clear
    events.sounds.push_back({2, 7, 0x40}); // $150FA
    if (flags & 0x08) { // back to the respawn point ($12C3E)
        events.back_to_respawn = true;
        return;
    }
    const int destination = low(o.word2c) & 15;
    if (destination == w.level) {
        // $1510C: a place in this level ("Use XYZ pos" forced).
        events.travel = ObjectEvents::Travel{destination, std::uint16_t(o.word2e | 0x8000), false};
        return;
    }
    o.byte21 = (w.level_flags & 0x1000) ? 0xFF : 0;
    events.travel = ObjectEvents::Travel{destination, o.word2e, o.byte21 != 0};
    w.running = false;
}

void LevelObjects::tick_button(LevelObject& o, ObjectWorld& w, ObjectEvents& events) { // $1609C: resting exactly on it
    std::int16_t height = 0;
    if (!explorer_on(o, w.explorer, 0, 0xFFFF, &height) || height != 0) return;
    auto& s = w.switches;
    const int type = o.word2c;
    // A sound only when the value changes ($160C6 …).
    if (type == 7) { // resets: word $3616 = $FFFF
        if (s.primary == 0xFF && s.secondary == 0xFF) return;
        s.primary = s.secondary = 0xFF;
        events.sounds.push_back({1, 0x0E, -1});
    } else if (type & 4) { // the primary value (byte $3616)
        if (s.primary == std::uint8_t((type & 3) + 1)) return;
        s.primary = std::uint8_t((type & 3) + 1);
        events.sounds.push_back({1, type + 0x0E, -1});
    } else { // word $3616 = type: primary 0, secondary type
        if (s.primary == 0 && s.secondary == type) return;
        s.primary = 0;
        s.secondary = std::uint8_t(type);
        events.sounds.push_back({1, type + 0x0E, 0x40});
    }
}

void LevelObjects::tick_switch(LevelObject& o, ObjectWorld& w, ObjectEvents& events) { // $161E0: resting exactly on it
    std::int16_t height = 0;
    if (!explorer_on(o, w.explorer, 0, 0xFFFF, &height) || height != 0) return;
    // The half of the cell along the switch's facing sets or clears its bit.
    std::uint16_t across = std::uint16_t(w.explorer.x), along = std::uint16_t(w.explorer.y);
    const int facing = high(o.word2e);
    if (facing & 1) std::swap(across, along);
    std::uint8_t part = std::uint8_t(across);
    if (facing & 2) part = std::uint8_t(~part);
    const std::uint16_t bit = o.word2c;
    std::uint16_t state = std::uint16_t(w.switches.binary | bit);
    int sound = o.word32; // $18 + colour, or 3 less when it clears
    if (!(part & 0x80)) { state = std::uint16_t(state ^ bit); sound -= 3; }
    if (std::uint8_t(state) == w.switches.binary) return;
    w.switches.binary = std::uint8_t(state);
    events.sounds.push_back({1, sound, 0x40});
}

void LevelObjects::tick_heads(LevelObject& o, ObjectWorld& w, ObjectEvents& events) { // $159DC
    o.speed = 0;
    bool moved;
    if (condition_holds(o.word26, w.switches, w.jewels)) {
        moved = o.word2e + 4 < 0x200;
        o.word2e = std::uint16_t(std::min(o.word2e + 4, 0x200));
    } else {
        moved = o.word2e >= 4;
        o.word2e = std::uint16_t(o.word2e >= 4 ? o.word2e - 4 : 0);
    }
    if (moved) hear(o, events, 0x0A, loudness(o, w.explorer, true)); // $15A12
    o.z = std::int16_t(o.word30 + 0x300 + ((o.word2c & 0x8000) ? o.word2e : -o.word2e));
    // $15A4C: a solid block three levels tall below its top.
    auto& e = w.explorer;
    const std::uint16_t dx = std::uint16_t(e.x - o.x + 0x5A), dy = std::uint16_t(e.y - o.y + 0x5A);
    if (dx >= 0x1B4 || dy >= 0x1B4) return;
    const std::int16_t dz = std::int16_t(e.z - o.z + 0x300 + 0x168);
    if (dz < 0 || dz >= 0x300) return;
    push_out(e, dx, dy, 0x1B4);
}

void LevelObjects::tick_timer(LevelObject& o, ObjectWorld& w, ObjectEvents& events) { // $15346
    if (w.timer_expired) return;
    if (w.timer >= 8) {
        w.timer = std::uint16_t(w.timer - 8);
        if ((w.timer & 0xFF) == 0) { // $1538E: a tick each second, quieter as it runs out
            const int seconds = w.timer >> 8;
            events.sounds.push_back({3, 0x21 + (seconds & 1), std::max(0x40 - 2 * seconds, 0x20)});
        }
        return;
    }
    w.timer = 0;
    const int flags = high(o.word2c);
    if (flags & 0x40) { // "Die if elapse"
        events.clue = 4;
        events.timer_death = true;
        w.timer_expired = true;
        o.alive = false; // its tick and frame become the dying routines (not rebuilt)
        return;
    }
    // "Elapse Dest" ($150D0): the timer travels like an exit at place 0.
    const int destination = flags & 15;
    if (destination == w.level) {
        if (w.running) events.travel = ObjectEvents::Travel{destination, 0x8000, false};
        return;
    }
    o.byte21 = (w.level_flags & 0x1000) ? 0xFF : 0;
    events.travel = ObjectEvents::Travel{destination, 0, o.byte21 != 0};
    w.running = false;
}

int LevelObjects::loudness(const LevelObject& o, const Explorer& e, bool flat) {
    const int dx = std::abs(o.x - e.x), dy = std::abs(o.y - e.y), dz = std::abs(o.z - e.z); // $111B4
    if (flat) { // $111A0: x and y only, / 64
        const int d = (dx + dy) >> 6;
        return d >= 0x3F ? 0 : d ^ 0x3F;
    }
    const int d = ((dx + dy + dz) & 0xFFFF) >> 4; // $1117E
    if (d >= 0x5F) return 0;
    return std::max(d - 0x20, 0) ^ 0x3F;
}

void LevelObjects::hear(const LevelObject& o, ObjectEvents& events, int effect, int volume) const {
    // $111EC: the loudest offer of the tick wins (an equal one does not).
    if (events.heard_volume >= 0 && volume <= events.heard_volume) return;
    events.heard_effect = effect;
    events.heard_volume = volume;
    events.heard_object = std::size_t(&o - objects_.data());
}

bool LevelObjects::touches(const LevelObject& o, const Explorer& e) const {
    // $1481C: the set's half-width r, z offset and range ($146FC + 24 ×
    // set, words +$12 … +$16).
    const std::uint32_t set = 0x146FC + std::uint32_t((o.word26 & 0x1C) / 4) * 24;
    const int r = main_.u16(set + 0x12);
    if (std::uint16_t(o.x - e.x + r) >= std::uint16_t(2 * r)) return false;
    if (std::uint16_t(o.y - e.y + r) >= std::uint16_t(2 * r)) return false;
    return std::uint16_t(o.z - e.z + main_.u16(set + 0x14)) < main_.u16(set + 0x16);
}

void LevelObjects::tick_boink(LevelObject& o, ObjectWorld& w, ObjectEvents& events) { // $15244
    if (o.popping) { // $14810: gone once its list has made +$1E negative
        if (o.expired) o.alive = false;
        return;
    }
    o.word32 = std::uint16_t(o.word32 - 1);
    std::int16_t speed = std::int16_t(o.word2c + o.word2e);
    o.word2c = std::uint16_t(speed);
    o.z = std::int16_t(o.z + speed);
    if (o.z < 0) {
        o.word2c = std::uint16_t(-std::int16_t(o.word2c));
        o.z = 0;
    }
    const int cx = o.x >> 8 & 31, cy = o.y >> 8 & 31;
    auto cell = [&](int level) { return level >= 0 && level < world::level_height ? w.grid.at(cx, cy, level) : 0; };
    bool bounce = false;
    if (std::int16_t(o.word2c) < 0) { // falling: a block in the level z reaches
        if (o.z <= 0x700) bounce = cell((std::uint16_t(o.z) + 0xFF) >> 8 & 0xFF) != 0;
    } else if (o.z <= 0x500) {        // rising: a block three levels up
        bounce = cell((std::uint16_t(o.z) >> 8) + 3) != 0;
    }
    if (bounce) {
        speed = std::int16_t(-speed);
        o.word2c = std::uint16_t(speed);
        o.z = std::int16_t(o.z + speed);
        if (std::int16_t(o.word32) < 0) { // $152AA: a sound when heard, then 8 ticks quiet
            if (const int volume = loudness(o, w.explorer, false)) events.sounds.push_back({1, 0x1F, volume});
            o.word32 = 8;
        }
    }
    if (!touches(o, w.explorer)) return;
    events.clue = 2;                // "Getting crushed destroys craft"
    events.fuel_cost += 0x30;       // $152E2
    events.sounds.push_back({1, 5, 0x40});
    events.sounds.push_back({0, 0x20, 0x40});
    o.popping = true;               // $14800 → $147E8
    o.count = 1;
    o.step = 0;
}

void LevelObjects::start_pac(LevelObject& o, const ObjectWorld& w) {
    // $15456: speed p0, pause p1, turn chance p2 ("Side scan%"), set p3;
    // +$2A's high byte = the block of its cell ($154D0); on the cell's
    // centre, raised by the block's height there ($12F62).
    const auto p = [&](std::size_t i) { return i < o.parameters.size() ? o.parameters[i] : std::uint8_t(0); };
    o.word30 = std::uint16_t(p(0) << 8 | p(2)); // speed (high), chance (low)
    o.pause = p(1);
    o.word2c = p(3);
    o.heading = std::int16_t(p(3) + w.rotation);
    o.count = 1;
    const int cx = o.x >> 8 & 31, cy = o.y >> 8 & 31, level = o.z >> 8 & 7;
    const int block = w.grid.at(cx, cy, level);
    o.word26 = std::uint16_t(block << 8 | p(3));
    o.x = std::int16_t((o.x & 0xFF00) | 0x80);
    o.y = std::int16_t((o.y & 0xFF00) | 0x80);
    o.z = std::int16_t(o.z + (surfaces_ ? surface_height(*surfaces_, block, 0, 0x80, 0x80) : 0));
    o.target_x = o.target_y = std::int16_t(0x8000);
    o.heading &= 3;
    const std::int16_t speed = std::int16_t(p(0));
    const std::int16_t sx[] = {speed, 0, std::int16_t(-speed), 0}, sy[] = {0, speed, 0, std::int16_t(-speed)}; // $129F2
    o.step_x = sx[o.heading];
    o.step_y = sy[o.heading];
}

void LevelObjects::tick_pac(LevelObject& o, ObjectWorld& w, ObjectEvents& events) { // $15526
    if (o.popping) { // $15712: back at its start once the pop list ran out ($157A4)
        if (!o.expired) return;
        o.popping = false;
        o.expired = false;
        o.x = std::int16_t(o.word34 & 0xFF00);
        o.y = std::int16_t(o.word34 << 8);
        o.z = std::int16_t(o.z & 0xFF00);
        start_pac(o, w);
        o.pause = 0x7F;
        return;
    }
    if (o.eaten) { // $15774: rises for 32 ticks, then back at its start
        if (--o.eaten > 0) { o.z = std::int16_t(o.z + 0x20); return; }
        o.z = std::int16_t(o.z - 0x3E0);
        o.x = std::int16_t(o.word34 & 0xFF00);
        o.y = std::int16_t(o.word34 << 8);
        o.z = std::int16_t(o.z & 0xFF00);
        start_pac(o, w);
        o.pause = 0x7F;
        return;
    }
    auto touch = [&] { // $156B6
        if (o.pause) return;
        if (!touches(o, w.explorer)) return;
        if (w.explorer.timer_a != 0) { // $1571E: eaten while the explorer flashes from a jewel
            o.eaten = 0x20;
            w.explorer.timer_b = std::int16_t(std::min(w.explorer.timer_b + 1, 5));
            events.sounds.push_back({1, 0x1D, 0x40}); // $1575E
            return;
        }
        events.clue = 5;
        events.fuel_cost += w.alien_cost;
        events.sounds.push_back({1, 0x1E, 0x40}); // $156F2
        w.explorer.arrival_ticks = std::int16_t(w.explorer.arrival_ticks + 0xF);
        o.popping = true; // $147E8
        o.count = 1;
        o.step = 0;
    };
    if (o.pause) { --o.pause; touch(); return; }
    const std::int16_t nx = std::int16_t(o.x + o.step_x), ny = std::int16_t(o.y + o.step_y);
    if (std::uint16_t(o.target_x - nx + 0x100) < 0x200 && std::uint16_t(o.target_y - ny + 0x100) < 0x200) {
        o.x = nx; o.y = ny; touch(); return;
    }
    // $1556A: at a cell centre: which way next.
    const bool same_type = (o.word26 & 0x80) != 0; // set bit 7: only cells of its start block ($157F0)
    const int level = std::uint16_t(o.z) >> 8;
    o.target_x = std::int16_t((nx & 0xFF00) | 0x80);
    o.target_y = std::int16_t((ny & 0xFF00) | 0x80);
    const int cx = nx >> 8 & 31, cy = ny >> 8 & 31;
    auto cell = [&](int x, int y, int z) { return x >= 0 && x < 32 && y >= 0 && y < 32 && z >= 0 && z < 8 ? w.grid.at(x, y, z) : 0; };
    auto open = [&](int h2) {
        const int h = h2 >> 1;
        const int ox = o.x >> 8 & 31, oy = o.y >> 8 & 31; // $155EC: the edge from its own cell
        if ((h == 0 && ox == 31) || (h == 1 && oy == 31) || (h == 2 && ox == 0) || (h == 3 && oy == 0)) return false;
        const int dx[] = {1, 0, -1, 0}, dy[] = {0, 1, 0, -1};
        const int x = cx + dx[h], y = cy + dy[h];
        auto clear = [&](int t) { return t == 0 || (t & 0xFE) == 0x22; };
        if (o.z < 0x5FF) { if (!clear(cell(x, y, level + 2)) || !clear(cell(x, y, level + 1))) return false; } // $15630
        else if (o.z < 0x6FF) { if (!clear(cell(x, y, level + 1))) return false; }                     // $1561E
        const int t = cell(x, y, level);
        if (same_type) return t == (o.word26 >> 8);
        return !(main_.u8(0x30BC0 + std::uint32_t(t)) & 8); // $135C0: attribute bit 3
    };
    int h2 = (o.heading & 3) * 2;
    if (open(h2)) {
        if ((next_random(*w.random) & 0xFF) >= (o.word30 & 0xFF)) { touch(); return; }
        h2 ^= 4;
    }
    // $15644: a side by the shared toggle, the other side, else back.
    *w.turn_side = std::int16_t(-*w.turn_side);
    int d = (h2 + *w.turn_side) & 6;
    if (!open(d)) {
        d ^= 4;
        if (!open(d)) d = (d - *w.turn_side) & 6;
    }
    o.heading = std::int16_t(d >> 1);
    o.x = std::int16_t((o.x & 0xFF00) | 0x80);
    o.y = std::int16_t((o.y & 0xFF00) | 0x80);
    o.target_x = o.x;
    o.target_y = o.y;
    touch();
    if (o.popping || o.eaten) return;
    const std::int16_t speed = std::int16_t(o.word30 >> 8); // $154A2
    const std::int16_t sx[] = {speed, 0, std::int16_t(-speed), 0}, sy[] = {0, speed, 0, std::int16_t(-speed)};
    o.step_x = sx[o.heading];
    o.step_y = sy[o.heading];
}

void LevelObjects::tick_pyramid(LevelObject& o, ObjectWorld& w, ObjectEvents& events) { // $15876
    if (o.popping) { // $14810
        if (o.expired) o.alive = false;
        return;
    }
    auto touch = [&] { // $15910
        hear(o, events, 0x23, loudness(o, w.explorer, false)); // $15922
        if (!touches(o, w.explorer)) return;
        events.clue = 5;
        events.fuel_cost += w.alien_cost + 0x10; // $1593A
        o.popping = true;
        o.count = 1;
        o.step = 0;
    };
    const std::int16_t nx = std::int16_t(o.x + o.step_x), ny = std::int16_t(o.y + o.step_y);
    if (std::uint16_t(o.target_x - nx + 0x100) < 0x200 && std::uint16_t(o.target_y - ny + 0x100) < 0x200) {
        o.x = nx; o.y = ny; touch(); return;
    }
    // $158B4: at a cell centre: back when the next cell (at its level) is
    // a wall block (attribute bit 3) or the cell above it is filled.
    o.target_x = std::int16_t((nx & 0xFF00) | 0x80);
    o.target_y = std::int16_t((ny & 0xFF00) | 0x80);
    const int h = o.heading & 3, level = std::uint16_t(o.z) >> 8;
    const int dx[] = {1, 0, -1, 0}, dy[] = {0, 1, 0, -1};
    const int x = (nx >> 8 & 31) + dx[h], y = (ny >> 8 & 31) + dy[h];
    auto cell = [&](int z) { return x >= 0 && x < 32 && y >= 0 && y < 32 && z >= 0 && z < 8 ? w.grid.at(x, y, z) : 0; };
    if (cell(level + 1) == 0 && !(main_.u8(0x30BC0 + std::uint32_t(cell(level))) & 8)) { touch(); return; } // keeps going
    o.heading = std::int16_t(h ^ 2);
    o.x = std::int16_t((o.x & 0xFF00) | 0x80);
    o.y = std::int16_t((o.y & 0xFF00) | 0x80);
    o.target_x = o.x;
    o.target_y = o.y;
    touch();
    if (o.popping) return;
    const std::int16_t speed = std::int16_t(o.word30); // $15826
    const std::int16_t sx[] = {speed, 0, std::int16_t(-speed), 0}, sy[] = {0, speed, 0, std::int16_t(-speed)};
    o.step_x = sx[o.heading];
    o.step_y = sy[o.heading];
}

void LevelObjects::tick_spark(LevelObject& o, ObjectWorld& w, ObjectEvents& events) { // $15C70
    // Off (+$20 set: not drawn) while its condition holds; else sparking:
    // the explorer in its cell up to $180 below it loses $20 fuel a tick.
    o.byte20 = 0xFF;
    if (condition_holds(o.word26, w.switches, w.jewels)) return;
    o.byte20 = 0;
    hear(o, events, 0x25, loudness(o, w.explorer, false)); // $15C84
    if (!explorer_on(o, w.explorer, 0, 0x180)) return;
    events.clue = 9; // "You need to disable sparks"
    events.fuel_cost += 0x20;
}

void LevelObjects::tick_message(LevelObject& o, ObjectEvents& events) { // $16028
    if (--o.count < 0) {
        o.alive = false;
        return;
    }
    if (o.count != 0) return;
    events.clue = high(o.word2c) & 0x1F;
    events.clue_always = (o.word2c & 0x8000) != 0;
}

std::vector<view::SpritePlace> LevelObjects::masked_creature_places(const data::Memory& main) const {
    std::vector<view::SpritePlace> places;
    for (const auto& o : objects_) {
        if (!o.alive || o.eaten || (o.kind != ObjectKind::boink && o.kind != ObjectKind::pac && o.kind != ObjectKind::pyramid)) continue;
        const std::uint32_t set = 0x146FC + std::uint32_t((o.word26 & 0x1C) / 4) * 24;
        const std::uint32_t pair = 0x146F8 + std::uint32_t(std::int16_t(main.u16(set + (o.popping ? 0x10 : 2))));
        auto place = view::project({o.x, o.y, o.z});
        place.x += std::int16_t(main.u16(pair));
        place.y += std::int16_t(main.u16(pair + 2));
        place.key = std::int16_t(main.u16(set)) < 0 ? 1 : 0; // 1: the 32-pixel drawer ($10124)
        places.push_back(place);
    }
    return places;
}

void LevelObjects::frame(const data::Memory& main, bool slow, const view::Camera& camera) {
    // $10058 for the animated kinds: jewels ($14BCA, their per-type list)
    // and exits shown ($15128, list $15140). The count is a byte; without
    // slow mode it counts twice a frame. A handler steps only a sprite on
    // the playfield: its corner less the scroll across within −$20 … $120
    // ($14BEA), its rows reaching rows 0 … $A6 ($1072A).
    constexpr std::uint32_t jewel_lists = 0x14C44, jewel_rows = 0x14C24, exit_list = 0x15140;
    for (auto& o : objects_) {
        if (!o.alive) continue;
        std::uint32_t list = 0;
        auto place = view::project({o.x, o.y, o.z});
        place.x -= 16;
        int rows = 0;
        if (o.kind == ObjectKind::jewel) {
            list = jewel_lists + std::uint32_t(std::int16_t(main.u16(jewel_lists + o.word2c)));
            rows = std::int16_t(main.u16(jewel_rows + o.word2c));
            if (o.word2c == 4 || o.word2c == 14) place.y -= 16; // $14A76
            else if (o.word2c == 2) place.y -= 8;              // $14AAC
        } else if (o.kind == ObjectKind::exit_point && o.byte20) {
            list = exit_list;
            rows = 0x0E;
            if (o.word2c == 2) place.y -= 8; // $14AAE's test, meant for jewels
        } else if (o.kind == ObjectKind::spark_generator && !o.byte20) { // $15CAA
            list = 0x15CBC;
            rows = 0x0F;
            if (o.word2c == 2) place.y -= 8; // $14AAE's test, meant for jewels
        } else if (o.kind == ObjectKind::boink || o.kind == ObjectKind::pac || o.kind == ObjectKind::pyramid) {
            // $1467E: the set's (dx, dy) pair at +2 (moving) or +$10
            // (popping, $147E2) from $146F8, its list after it.
            const std::uint32_t set = 0x146FC + std::uint32_t((o.word26 & 0x1C) / 4) * 24;
            const std::uint32_t pair = 0x146F8 + std::uint32_t(std::int16_t(main.u16(set + (o.popping ? 0x10 : 2))));
            place = view::project({o.x, o.y, o.z});
            place.x += std::int16_t(main.u16(pair));
            place.y += std::int16_t(main.u16(pair + 2));
            list = pair + 4;
            const int mode = std::int16_t(main.u16(set));
            rows = mode < 0 ? 23 : mode > 0 ? 15 : 11; // $10124, $100A0 16 rows, 12 rows
        } else {
            continue;
        }
        // Creatures step before their drawer's screen test ($10092,
        // $147CA, $10124); the others only on screen.
        const bool creature = o.kind == ObjectKind::boink || o.kind == ObjectKind::pac || o.kind == ObjectKind::pyramid;
        const int across = place.x - camera.scroll_x + 0x20;
        const int down = place.y - camera.scroll_y;
        if (!creature && (across <= 0 || across >= 0x140 || down >= 0xA7 || down + rows < 0)) continue;
        bool next = false;
        if (!slow) next = (o.count = (o.count - 1) & 0xFF) == 0;
        if (!next) next = (o.count = (o.count - 1) & 0xFF) == 0;
        if (!next) continue;
        ++o.step;
        if (std::int16_t(main.u16(list + std::uint32_t(o.step) * 4)) < 0) o.step = 0;
        o.count = main.u16(list + std::uint32_t(o.step) * 4) & 0xFF;
        if (o.popping && (o.count & 0x80)) o.expired = true; // +$1E negative: the next tick acts
    }
}

} // namespace spindizzy::play
