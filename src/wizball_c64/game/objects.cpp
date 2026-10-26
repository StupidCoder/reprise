#include "wizball_c64/game/play.hpp"

// The object engine ($A204 and what it calls), spawning ($A9DA, $A14A,
// $9DA1) and the enemies' shots ($9BB5, $99BD, $9AD6).
namespace wizball::game {
namespace {
constexpr int slots = 6;
constexpr std::uint8_t solid_for_objects = 0xA0; // $A6A5
constexpr std::uint8_t filler_kind = 17;         // $A19E: template $3ECF + $22

// The 8.8 helpers keep the 6502's byte arithmetic.
void add88(std::uint8_t& whole, std::uint8_t& fraction, std::uint8_t dw, std::uint8_t df) {
    const unsigned f = fraction + df;
    fraction = std::uint8_t(f);
    if (f > 0xFF) ++whole;
    whole = std::uint8_t(whole + dw);
}
void sub88(std::uint8_t& whole, std::uint8_t& fraction, std::uint8_t dw, std::uint8_t df) {
    const int f = fraction - df;
    fraction = std::uint8_t(f);
    if (f < 0) --whole;
    whole = std::uint8_t(whole - dw);
}
} // namespace

std::uint8_t& Object::field(int offset) {
    static std::uint8_t sink;
    switch (offset) {
    case 0x00: return x;
    case 0x01: return x_fraction;
    case 0x02: return y;
    case 0x03: return y_fraction;
    case 0x04: return dx;
    case 0x05: return dx_fraction;
    case 0x06: return dy;
    case 0x07: return dy_fraction;
    case 0x08: return motion;
    case 0x09: return acceleration;
    case 0x0A: return path_lo;
    case 0x0B: return path_hi;
    case 0x0C: return path_period;
    case 0x0D: return landscape;
    case 0x0E: return fire;
    case 0x0F: return fire_period;
    case 0x10: return kind;
    case 0x11: return animation;
    case 0x12: return anim_a;
    case 0x13: return anim_b;
    case 0x14: return anim_c;
    case 0x15: return state;
    case 0x16: return score;
    case 0x17: return mx;
    case 0x18: return mx_fraction;
    case 0x19: return my;
    case 0x1A: return my_fraction;
    case 0x1B: return path_index;
    case 0x1C: return path_clock;
    default: sink = 0; return sink;
    }
}

// A table byte an object reads by address. Some templates point their path
// at $0000 (kind 1, with no speed, so the steps change nothing); bytes
// outside the game file read as 0.
std::uint8_t Play::table_byte(std::uint16_t address) const {
    if (address < 0x0400 || address > 0xFD00) return 0;
    return image_.byte(address);
}

// --- spawning -----------------------------------------------------------------

void Play::spawn_template(int slot, const Template& t) {
    // $9DA1: the template's 23 bytes, the rest cleared, then its random fields.
    auto& o = objects_[std::size_t(slot)];
    o = Object{};
    for (int i = 0; i < 0x17; ++i) o.field(i) = t.bytes[std::size_t(i)];
    for (const auto& r : t.random) o.field(r[2]) = std::uint8_t((random_.next() & r[0]) + r[1]);
}

void Play::spawn(int slot, int kind, int list_slot, int entry) {
    // $AA68
    spawn_template(slot, templates_[std::size_t(kind)]);
    auto& o = objects_[std::size_t(slot)];
    // $AA66
    o.entry = entry;
    o.entry_slot = list_slot;
    if (o.state != 0) o.kind = image_.byte(std::uint16_t(0xB18B + place_));
    if (playfield_.pixel_delta() < 0) {
        o.x = 2; // coming in on the left
    } else {
        o.x = 0xAA; // on the right, heading left
        o.dx = std::uint8_t(~o.dx);
        o.dx_fraction = std::uint8_t(~o.dx_fraction);
    }
}

int Play::free_slot() const {
    for (int s = 0; s < slots; ++s)
        if (!objects_[std::size_t(s)].active()) return s;
    return -1;
}

void Play::spawn_from_list() {
    // $A9DA: as the view scrolls, entries at the edge column come in.
    const int list_slot = slot_of_place_[std::size_t(place_)];
    if (list_slot == 0) return;
    const auto& list = spawn_lists_[std::size_t(list_slot)];
    int a = playfield_.view_column();
    if (playfield_.pixel_delta() > 0) a = (a + 0x28) & 0xFF;
    else if (playfield_.pixel_delta() == 0) return;
    // The list as the game walks it: entry e at bytes 2e+2/2e+3, a header of
    // zeros before it and $FF after. The forward search moves its base two
    // bytes on before stepping, so the cursor it leaves is one entry behind.
    const auto column_at = [&](int e) -> int {
        if (e < 0) return 0;
        if (e >= int(list.size())) return 0xFF;
        return list[std::size_t(e)].column;
    };
    int e = cursor_ - 1; // cursor_: $B1AE as an entry number plus one (0 = the header)
    int base = 0;        // 1 once the base has moved
    bool found = false;
    for (;;) {
        const int v = column_at(e + base);
        if (a == v) {
            found = true;
            break;
        }
        if (a > v) break;
        e -= 1;
    }
    if (!found) {
        base = 1;
        for (;;) {
            const int v = column_at(e + base);
            if (a == v) {
                found = true;
                break;
            }
            if (a < v) {
                cursor_ = e + 1;
                return;
            }
            e += 1;
        }
    }
    cursor_ = e + 1;
    if (a == 0) return;
    const int index = e + base;
    if (index < 0 || index >= int(list.size())) return;
    auto& entry = spawn_lists_[std::size_t(list_slot)][std::size_t(index)];
    if (entry.kind & 0x80) return;
    const int slot = free_slot();
    if (slot < 0) return;
    entry.kind |= 0x80;
    spawn(slot, entry.kind & 0x7F, list_slot, index);
}

void Play::spawn_filler() {
    // $A181: the timer's enemy (kind 17, its fire period by place).
    timer_ticks_ = timer_seconds_ = 0;
    const int slot = free_slot();
    if (slot < 0) return;
    templates_[filler_kind].bytes[0x0F] = image_.byte(std::uint16_t(0xA1C1 + place_));
    spawn(slot, filler_kind, 0, -1);
}

void Play::level_timer() {
    // $A14A
    if (filler_burst_ != 0) {
        if ((++filler_burst_clock_ & 0x0F) == 0) {
            const int before = free_slot();
            spawn_filler();
            if (before >= 0) --filler_burst_;
        }
        return;
    }
    if (++timer_ticks_ == 0x32) {
        timer_ticks_ = 0;
        ++timer_seconds_;
    }
    const int limit = image_.byte(std::uint16_t(0xA1B9 + place_));
    const int left = timer_seconds_ - limit;
    if (left >= 0) {
        spawn_filler();
    } else if (std::uint8_t(left) == 0xFF && timer_ticks_ == 0) {
        sound_->start_tune(5, 1); // a second before: the warning on the third voice
    }
}

// --- the object pass ------------------------------------------------------------

void Play::update_objects() {
    // $A204
    landscape_slot_ = (landscape_slot_ + 1) % slots;
    for (int s = 0; s < slots; ++s) {
        if (objects_[std::size_t(s)].active()) update_object(s);
        else sprite_mask_ &= std::uint8_t(~(4 << s));
    }
    // $A974: a beeping enemy beeps once a second.
    if (beeping_ != beeping_last_) {
        beeping_last_ = beeping_;
        beep_clock_ = 0x3B;
    }
    if (beeping_) {
        if (++beep_clock_ == 0x3C) {
            beep_clock_ = 0;
            beep_effect_ ^= 7;
            sound_->start_effect(beep_effect_);
        }
    }
    if ((sprite_mask_ & 0xFC) == 0) count_aliens();
}

void Play::update_object(int s) {
    auto& o = objects_[std::size_t(s)];
    if (o.hit && !resolve_hit(s)) return; // $A7F2: dying, falling or collected (a reduced pass ran)
    move_object(o);                       // $A386
    scroll_object(o);                     // $A346
    animate_object(o);                    // $A5B1
    bounds_object(s);                     // $A659
    object_fire(s);                       // $A541
    if (left_screen(s)) return;           // $A274
    object_sprite(s);                     // $A29C
    object_box(o);                        // $A744
}

void Play::move_object(Object& o) {
    if (o.motion < 2) {
        if (o.motion == 0) {
            const unsigned f = o.dy_fraction + o.acceleration;
            o.dy_fraction = std::uint8_t(f);
            if (f > 0xFF) ++o.dy;
        } else {
            const int f = o.dy_fraction - o.acceleration;
            o.dy_fraction = std::uint8_t(f);
            if (f < 0) --o.dy;
        }
        add88(o.y, o.y_fraction, o.dy, o.dy_fraction);
        add88(o.x, o.x_fraction, o.dx, o.dx_fraction);
        return;
    }
    if (++o.path_clock == o.path_period) {
        o.path_clock = 0;
        ++o.path_index;
    }
    std::uint8_t step = table_byte(std::uint16_t(o.path() + o.path_index));
    if (step == 0xFF) {
        o.path_index = 0;
        step = table_byte(o.path());
    }
    if (o.motion == 3) { // $A492: the steps push a momentum
        if (step & 1) add88(o.mx, o.mx_fraction, o.dx, o.dx_fraction);
        if (step & 2) sub88(o.mx, o.mx_fraction, o.dx, o.dx_fraction);
        if (step & 4) add88(o.my, o.my_fraction, o.dy, o.dy_fraction);
        if (step & 8) sub88(o.my, o.my_fraction, o.dy, o.dy_fraction);
        add88(o.x, o.x_fraction, o.mx, o.mx_fraction);
        add88(o.y, o.y_fraction, o.my, o.my_fraction);
        return;
    }
    if (step & 1) add88(o.x, o.x_fraction, o.dx, o.dx_fraction);
    if (step & 2) sub88(o.x, o.x_fraction, o.dx, o.dx_fraction);
    if (step & 4) add88(o.y, o.y_fraction, o.dy, o.dy_fraction);
    if (step & 8) sub88(o.y, o.y_fraction, o.dy, o.dy_fraction);
}

void Play::scroll_object(Object& o) {
    // $A346: keep the object where it is on the map while the view moves.
    if (playfield_.at_end()) return;
    const int d = playfield_.pixel_delta();
    if (d >= 0) {
        sub88(o.x, o.x_fraction, image_.byte(std::uint16_t(0xB285 + d)), image_.byte(std::uint16_t(0xB282 + d)));
    } else {
        const int y = d + 2;
        add88(o.x, o.x_fraction, image_.byte(std::uint16_t(0xB28A + y)), image_.byte(std::uint16_t(0xB288 + y)));
    }
}

void Play::animate_object(Object& o) {
    // $A5B1
    if (o.kind & 0x80) beeping_ = 1;
    if (o.animation == 0) {
        o.frame = o.anim_a;
        return;
    }
    if (o.animation == 1) { // $A60C: frames along a table, turning at its end
        const int period = (o.kind & 0x80) ? 5 : 3;
        if (++o.anim_clock == period) o.anim_clock = 0;
        if (o.anim_a != 0x0A && o.anim_clock == 0) {
            const auto old = o.anim_a++;
            if (std::int8_t(std::uint8_t(old - 0x0A)) >= 0) o.anim_a = std::uint8_t(o.anim_a - 2);
        }
        o.frame = table_byte(std::uint16_t((o.anim_b | o.anim_c << 8) + o.anim_a));
        return;
    }
    if (++o.anim_clock == o.anim_a) { // a frame table, $FF n: back n entries
        o.anim_clock = 0;
        const auto p = std::uint16_t((o.anim_b | o.anim_c << 8) + 1);
        o.anim_b = std::uint8_t(p);
        o.anim_c = std::uint8_t(p >> 8);
    }
    for (;;) {
        const auto p = std::uint16_t(o.anim_b | o.anim_c << 8);
        o.frame = table_byte(p);
        if (o.frame != 0xFF) return;
        const auto back = std::uint16_t(p - table_byte(std::uint16_t(p + 1)));
        o.anim_b = std::uint8_t(back);
        o.anim_c = std::uint8_t(back >> 8);
    }
}

namespace {
void reverse_x(Object& o) {
    o.dx = std::uint8_t(~o.dx);
    o.dx_fraction = std::uint8_t(~o.dx_fraction);
    if (o.animation == 1) o.anim_a = 0;
}
void reverse_y(Object& o) {
    o.dy = std::uint8_t(~o.dy);
    o.dy_fraction = std::uint8_t(~o.dy_fraction);
    if (o.animation == 1) o.anim_a = 0x14;
}
enum class Bounce { none, floor, ceiling, wall_left, wall_right };
Bounce bounce_of(std::uint16_t address) {
    switch (address) {
    case 0xA70D: return Bounce::floor;
    case 0xA707: return Bounce::ceiling;
    case 0xA6DF: return Bounce::wall_left;
    case 0xA6E5: return Bounce::wall_right;
    default: return Bounce::none;
    }
}
void apply(Object& o, Bounce b) {
    switch (b) {
    case Bounce::floor:
        if (std::int8_t(o.dy) >= 0) reverse_y(o);
        break;
    case Bounce::ceiling:
        if (std::int8_t(o.dy) < 0) reverse_y(o);
        break;
    case Bounce::wall_left:
        if (std::int8_t(o.dx) < 0) reverse_x(o);
        break;
    case Bounce::wall_right:
        if (std::int8_t(o.dx) >= 0) reverse_x(o);
        break;
    case Bounce::none: break;
    }
}
} // namespace

void Play::bounds_object(int s) {
    // $A659
    auto& o = objects_[std::size_t(s)];
    if (o.y >= 0xC8) {
        o.y = 0xC7;
        if (o.landscape < 2) apply(o, Bounce::floor);
        return;
    }
    if (o.y < 0x46) {
        o.y = 0x47;
        if (o.landscape < 2) apply(o, Bounce::ceiling);
        return;
    }
    if (level_cleared_ || o.landscape == 0 || s != landscape_slot_) return;
    const int column = std::uint8_t(o.x - 0x0C + (playfield_.fine_scroll() >> 1)) >> 2;
    const int row = std::uint8_t(o.y - 0x33) >> 3;
    for (int i = 0; i < 8; ++i) {
        const int at = row * 40 + column + image_.byte(std::uint16_t(0xB3B6 + i));
        const int r = at / 40 - 3, c = at % 40;
        const std::uint8_t code = (r >= 0 && r < Playfield::rows) ? playfield_.code(c, r) : 0;
        if (code < solid_for_objects) continue;
        if (objects_[0].landscape >= 2) { // $A6B1 reads slot 0's field, whichever object it is
            o.acceleration = o.dy = o.dy_fraction = o.dx = o.dx_fraction = o.motion = 0;
            return;
        }
        apply(o, bounce_of(image_.word(std::uint16_t(0xB28C + 4 * i))));
        apply(o, bounce_of(image_.word(std::uint16_t(0xB28E + 4 * i))));
        return;
    }
}

void Play::object_fire(int s) {
    // $A541
    auto& o = objects_[std::size_t(s)];
    if (++o.fire_clock != o.fire_period) return;
    o.fire_clock = 0;
    switch (o.fire) {
    case 0: return;
    case 2:
        o.fire_angle = std::uint8_t(o.fire_angle + 4);
        enemy_fire(s, o.fire_angle & 0x3F);
        break;
    case 3:
        for (int a : {0x00, 0x10, 0x20, 0x30, 0x08, 0x18, 0x28, 0x38}) enemy_fire(s, a);
        break;
    default:
        enemy_fire(s, 0xFF);
        break;
    }
    if (clear_sequence_ == 0) sound_->start_effect(6); // $A5A6
}

bool Play::left_screen(int s) {
    // $A274: past the right edge (x $B6-$F9) it has gone; its entry may come again.
    auto& o = objects_[std::size_t(s)];
    if (o.x < 0xB6 || o.x >= 0xFA) return false;
    if (o.hit == 0 && o.entry >= 0) spawn_lists_[std::size_t(o.entry_slot)][std::size_t(o.entry)].kind &= 0x7F;
    remove_object(s);
    return true;
}

void Play::remove_object(int s) {
    // $A9AF
    auto& o = objects_[std::size_t(s)];
    o.y = 0;
    o.box_left = o.box_right = o.box_top = o.box_bottom = 0;
    beeping_ = 0;
}

void Play::object_sprite(int s) {
    // $A29C: the sprite (hardware sprite 2 + slot) from the object.
    auto& o = objects_[std::size_t(s)];
    auto& sp = object_sprites_[std::size_t(s)];
    sp.x = (o.x << 1 | o.x_fraction >> 7) & 0x1FF;
    sp.y = o.y;
    std::uint8_t colour = o.kind & 0x1F;
    if (colour == 0x10) { // cycling colours
        if (++o.acceleration == 5) {
            o.acceleration = 0;
            ++o.anim_b;
        }
        colour = image_.byte(std::uint16_t(0xB87D + (o.anim_b & 3)));
    }
    sp.colour = colour;
    sp.pointer = o.frame;
    sp.multicolour = true;
    sprite_mask_ |= std::uint8_t(4 << s);
    if ((o.kind & 0x40) && o.hit == 0) sp.multicolour = false;
    if (o.anim_c == 1) { // blinking after a bounce
        ++o.anim_b;
        if ((o.anim_b >> 2) == 4) {
            o.anim_c = 0;
        } else {
            sp.colour = image_.byte(std::uint16_t(0xB875 + ((o.anim_b >> 2) & 3)));
            sp.multicolour = false;
        }
    }
}

void Play::object_box(Object& o) {
    // $A744
    o.box_left = std::uint8_t(o.x - 3);
    o.box_right = std::uint8_t(o.box_left + 0x0A);
    o.box_top = std::uint8_t(o.y - 3);
    o.box_bottom = std::uint8_t(o.box_top + 0x16);
}

int Play::object_at(int x, int y) {
    // $6FBE: the first object whose box holds the point; it is marked hit.
    for (int s = 0; s < slots; ++s) {
        auto& o = objects_[std::size_t(s)];
        if (x >= o.box_left && x < o.box_right && y >= o.box_top && y < o.box_bottom) {
            o.hit = 1;
            return s;
        }
    }
    return -1;
}

bool Play::resolve_hit(int s) {
    // $A7F2. Returns true when the object's normal pass goes on.
    auto& o = objects_[std::size_t(s)];
    if (o.state == 0xFF) { // $A802: bounces off and blinks
        o.hit = 0;
        flash_background_ = 0x0A;
        if (std::int8_t(o.dx) < 0) {
            o.dx = std::uint8_t(~o.dx);
            o.dx_fraction = std::uint8_t(~o.dx_fraction);
        }
        sound_->start_effect(0x0D);
        return true;
    }
    if (o.state < 2 || o.state >= 6) { // $A77F: one fewer to come
        if (o.kind != 0x86) {
            const int limit = image_.byte(std::uint16_t(0xA1B9 + place_));
            if (std::uint8_t(timer_seconds_ - limit) != 0xFF) timer_ticks_ = timer_seconds_ = 0;
            --aliens_left_;
            open_next_wave();
        }
    }
    if (o.state == 1) { // $A7A9: a paint dropper may drop something special
        pearl_count_ = std::uint8_t(pearl_count_ + image_.byte(std::uint16_t(0xA765 + place_)));
        const auto key = std::uint8_t(pearl_count_ & 0xF8);
        for (int y = 5; y >= 1; --y) {
            if (special_at_[std::size_t(y)] == key) {
                o.kind = image_.byte(std::uint16_t(0xA771 + y));
                pearl_count_ = std::uint8_t(pearl_count_ + 8);
                break;
            }
        }
    }
    switch (o.state) {
    case 6: // an extra life
        sound_->start_effect(0x11);
        ++lives_[std::size_t(player_)];
        explode(s);
        return false;
    case 7: // the shield token
        token_selected_ = 7;
        sound_->start_effect(0x14);
        killed(s);
        return false;
    case 4: dying(s); return false;
    case 1: start_dropping(s); return false;
    case 2: dropping(s); return false;
    case 3: splashing(s); return false;
    case 5: pearl(s); return false;
    default: killed(s); return false;
    }
}

void Play::reduced_pass(int s) {
    // $A8C6: what a dying or falling object still does.
    if (left_screen(s)) return;
    scroll_object(objects_[std::size_t(s)]);
    object_sprite(s);
}

void Play::killed(int s) {
    // $A85B
    auto& o = objects_[std::size_t(s)];
    if (kills_ != 0xFF) ++kills_;
    if (o.kind & 0x80) {
        sound_->start_effect(0x0E);
        beeping_ = 0;
    }
    if (++pearl_kills_ == pearl_kill_limit_ || (o.kind & 0x20)) {
        drop_pearl(s);
        return;
    }
    sound_->start_effect(5);
    explode(s);
}

void Play::explode(int s) {
    // $A891
    auto& o = objects_[std::size_t(s)];
    add_points(o.score);
    o.state = 4;
    o.anim_a = 0;
    o.box_bottom = o.box_top = 0;
    o.frame = 0x5F;
    dying(s);
}

void Play::dying(int s) {
    // $A8AC: the explosion's frames run down to $5B.
    auto& o = objects_[std::size_t(s)];
    if ((++o.anim_a & 3) == 0) {
        if (--o.frame == 0x5B) {
            remove_object(s);
            return;
        }
    }
    reduced_pass(s);
}

void Play::start_dropping(int s) {
    // $A8D2: paint falls out.
    auto& o = objects_[std::size_t(s)];
    o.state = 2;
    sound_->start_effect(1);
    add_points(o.score);
    o.anim_a = 0;
    o.animation = 0;
    dropping(s);
}

void Play::dropping(int s) {
    // $A8EC
    auto& o = objects_[std::size_t(s)];
    if ((++o.anim_a & 3) == 0) {
        if (++o.animation == 4) o.animation = 0;
    }
    o.frame = image_.byte(std::uint16_t(0xA9AB + o.animation));
    if (++o.y < 0xCB) {
        object_box(o);
        reduced_pass(s);
        return;
    }
    ++o.state;
    o.frame = 0x53;
    o.anim_a = 0;
    splashing(s);
}

void Play::splashing(int s) {
    // $A925
    auto& o = objects_[std::size_t(s)];
    if ((++o.anim_a & 3) == 0) {
        if (++o.frame == 0x5C) {
            remove_object(s);
            return;
        }
    }
    reduced_pass(s);
}

void Play::drop_pearl(int s) {
    // $A93C
    auto& o = objects_[std::size_t(s)];
    sound_->start_effect(2);
    add_points(o.score);
    pearl_kills_ = 0;
    o.state = 5;
    o.frame = 0x78;
    o.kind = 5;
    pearl(s);
}

void Play::pearl(int s) {
    // $A95B
    auto& o = objects_[std::size_t(s)];
    if (level_cleared_) sub88(o.x, o.x_fraction, 0, 0x40);
    object_box(o);
    reduced_pass(s);
}

// --- enemy shots ------------------------------------------------------------------

void Play::enemy_fire(int s, int direction) {
    // $9BB5
    if (dying_ != 0 || shots_full_) return;
    const auto& o = objects_[std::size_t(s)];
    if (o.y < 0x40) return;
    const int row = (o.y - 0x40) >> 3;
    const int ox = o.x - 8;
    if (ox < 0 || ox >= 0xA0) return;
    const int column = ox >> 2;
    int free = -1;
    for (int k = 0; k < 16; ++k)
        if (enemy_shots_[std::size_t(k)].state == 0) {
            free = k;
            break;
        }
    if (free < 0) {
        shots_full_ = true;
        return;
    }
    auto& e = enemy_shots_[std::size_t(free)];
    e.column = std::uint8_t(column);
    e.row = std::uint8_t(row);
    if (direction == 0xFF) { // at the wiz
        int a = random_.next() & 0x0F;
        int b = 0x0F - a;
        if (((wiz_.x() & 0xFF) >> 1) < o.x) a = -a; // $9C29: the low byte of the wiz's x only
        if (wiz_.y() < o.y) b = -b;
        e.dx = std::int8_t(a);
        e.dy = std::int8_t(b);
    } else if (direction < 0x10) {
        e.dx = std::int8_t(direction);
        e.dy = std::int8_t(direction - 0x10);
    } else if (direction < 0x20) {
        e.dy = std::int8_t(direction - 0x10);
        e.dx = std::int8_t(0x10 - e.dy);
    } else if (direction < 0x30) {
        const int k = direction - 0x20;
        e.dx = std::int8_t(-k);
        e.dy = std::int8_t(0x10 - k);
    } else {
        const int k = direction - 0x30;
        e.dx = std::int8_t(k - 0x10);
        e.dy = std::int8_t(-k);
    }
    e.state = 2;
    random_.next(); // $9C57: drawn and not used
    e.clock = 0;
}

void Play::erase_enemy_shots() {
    // $9AD6: the half drawn last pass comes off; a shot in the wiz's or the
    // catellite's cells hits it.
    const int wx = std::uint8_t((wiz_.x() >> 1) - 0x0B) >> 2;
    const int wy = std::uint8_t(wiz_.y() - 0x43) >> 3;
    const int cx = std::uint8_t(catellite_.x() - 0x0C) >> 2;
    const int cy = std::uint8_t(catellite_.y() - 0x43) >> 3;
    shots_full_ = false;
    const int first = shot_half_ ? 7 : 15;
    for (int k = first; k > first - 8; --k) {
        auto& e = enemy_shots_[std::size_t(k)];
        if (e.state != 1) continue;
        restore_cell(e.drawn_screen, e.drawn_column, e.drawn_row, e.saved);
        const int c = e.column, r = e.row;
        if (c >= wx && c < ((wx + 2) & 0xFF) && r >= wy && r < ((wy + 2) & 0xFF)) {
            e.state = 0;
            ++wiz_hit_;
        } else if (c >= cx && c < ((cx + 1) & 0xFF) && r >= cy && r < ((cy + 1) & 0xFF)) {
            e.state = 0;
            ++catellite_hit_;
        }
    }
}

void Play::move_enemy_shots() {
    // $99BD
    const int step = playfield_.step();
    for (auto& e : enemy_shots_) {
        if (step > 0) --e.column;
        else if (step < 0) ++e.column;
    }
    shot_half_ ^= 1;
    const int first = shot_half_ ? 0 : 8;
    for (int k = first; k < first + 8; ++k) {
        auto& e = enemy_shots_[std::size_t(k)];
        if (e.state == 0) continue;
        if (e.state == 2) --e.state;
        int v = e.sub_x + e.dx;
        e.sub_x = std::uint8_t(v & 0x1F);
        if (std::int8_t(std::uint8_t(v)) < 0) --e.column;
        else if (std::uint8_t(v) >= 0x20 && std::int8_t(std::uint8_t(v)) >= 0) ++e.column;
        v = e.sub_y + e.dy;
        e.sub_y = std::uint8_t(v & 0x1F);
        if (std::int8_t(std::uint8_t(v)) < 0) --e.row;
        else if (std::uint8_t(v) >= 0x20 && std::int8_t(std::uint8_t(v)) >= 0) ++e.row;
        if (e.row >= 0x14 || e.column >= 0x27) {
            e.state = 0;
            continue;
        }
        const int screen = playfield_.shown_screen();
        const auto under = playfield_.code_on(screen, e.column, e.row);
        e.saved = under;
        if (under & 0x80) { // into the landscape
            if (dying_ == 0) e.state = 0;
            continue;
        }
        const int shape = (e.sub_x >> 3) | ((e.sub_y >> 1) & 0x0C);
        playfield_.put(screen, e.column, e.row, image_.byte(std::uint16_t(0xB8E1 + shape)));
        set_cell_colour(e.column, e.row, image_.byte(std::uint16_t(0xB885 + (++e.clock & 7))));
        e.drawn_screen = screen;
        e.drawn_column = e.column;
        e.drawn_row = e.row;
    }
}

} // namespace wizball::game
