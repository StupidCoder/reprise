#include "sonic_advance_gba/game/item_box.hpp"
#include "sonic_advance_gba/game/collision.hpp"
#include "sonic_advance_gba/game/destruction.hpp"
#include "sonic_advance_gba/game/game.hpp"
#include "sonic_advance_gba/game/math.hpp"
#include "sonic_advance_gba/game/object_util.hpp"
#include "sonic_advance_gba/game/player_effects.hpp"
#include "sonic_advance_gba/game/solid.hpp"
#include "sonic_advance_gba/game/stage.hpp"

namespace sonic_advance_gba {

namespace {

constexpr u32 icon_variants = 0x080B84E0; // by item: the icon (the 1-up adds the character)

void copy_record(Sprite& to, const Sprite& from) {
    to.tiles.from = from.tiles.from;
    to.tiles.to = from.tiles.to;
    to.tiles.bytes = from.tiles.bytes;
    to.animation = from.animation;
    to.frame = from.frame;
    to.flags = from.flags;
    to.position = from.position;
    to.x = from.x;
    to.y = from.y;
    to.layer = from.layer;
    to.timer = from.timer;
    to.previous_animation = from.previous_animation;
    to.variant = from.variant;
    to.previous_variant = from.previous_variant;
    to.speed = from.speed;
    to.first_entry = from.first_entry;
    to.entries = from.entries;
    to.palette = from.palette;
    to.hitboxes[0] = from.hitboxes[0];
}

// 0801E798: a box with its item's icon. Solid; the player breaks it by
// attacking or jumping on it; bumped from below it hops.
class ItemBox final : public Process {
public:
    ItemBox(Game& g, const ObjectSpawn& o)
        : Process(0x2000), game_(g), spawn_(o), x_byte_(o.record[0]), item_(o.record[2]) {
        x_ = s16(x_byte_ * 8 + o.cell_x * 0x100);
        y_ = s16(o.record[1] * 8 + o.cell_y * 0x100);
        o.record[0] = 0xFE;
        box_.start(g.sprite_tiles.allocate(0x10), 0x2C1, 0, x_, y_, 0x480, 0x2000);
        animate(g, box_);
        // The icon starts as a copy of the box's sprite record.
        copy_record(icon_, box_);
        icon_.tiles.to = g.sprite_tiles.allocate(4);
        icon_.tiles.bytes = 0;
        icon_.animation = 0x2C2;
        icon_.variant =
            u8(g.assets().u8(icon_variants + item_) + (item_ == 0 ? g.stage().character : 0));
        icon_.position = 0;
        icon_.timer = 0;
        icon_.previous_variant = 0xFF;
        icon_.palette = 0;
        icon_.layer = 0x4C0;
        icon_.hitboxes[0].index = 0xFFFFFFFF;
        icon_.flags = 0x2000;
        animate(g, icon_);
    }

    void on_destroy() override {
        // 0801F134
        game_.sprite_tiles.free(box_.tiles.to);
        game_.sprite_tiles.free(icon_.tiles.to);
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        if (g.game_mode >= 2) {
            throw Unrecovered("item boxes in multiplayer (0801E798)");
        }
        if (state_ != State::whole) {
            broken(g);
            return;
        }
        if (falling_) {
            offset_ = s16(offset_ + s8(u16(speed_y_) >> 8));
            speed_y_ = s16(speed_y_ + 0x28);
            const s32 d =
                probe_short(g.assets(), s.map, y_ + offset_, x_, 1, 8, nullptr, Sensor::floor);
            if (d < 0) {
                offset_ = s16(offset_ + d);
                falling_ = false;
            }
        }
        box_.x = s16(x_ - s.camera.x);
        box_.y = s16(y_ - s.camera.y + offset_);
        const s16 y = s16(y_ + offset_);
        Player& p = s.player[0];
        if (!(p.flags & player_flag::no_movement)) {
            if (s.players > 1) {
                collide_solid(g, box_, x_, y, s.player[1]);
            }
            u32 r;
            if (!((p.flags & player_flag::on_object) && p.standing_on == &box_) &&
                p.body->sprite.hitboxes[1].index != 0xFFFFFFFF) {
                r = collide_breakable(g, box_, x_, y, p);
                if (!(r & solid::from_below) && (r & (solid::on_top | solid::attacked))) {
                    for (unsigned n = 0; n < 2; ++n) {
                        Player& q = s.player[n];
                        if ((q.flags & player_flag::on_object) && q.standing_on == &box_) {
                            q.flags =
                                (q.flags & ~u32(player_flag::on_object | player_flag::pushing)) |
                                player_flag::in_air;
                        }
                    }
                    g.sound().start(0xA7);
                    create_explosion(g, x_, y);
                    state_ = State::rising;
                    clock_ = 0;
                    return;
                }
            } else {
                r = collide_solid(g, box_, x_, y, p);
            }
            if (r & solid::from_below) {
                speed_y_ = -0x100;
                falling_ = true;
            }
        }
        if (out_of_range(s, x_, y_, box_.x, s16(box_.y + offset_))) {
            spawn_.record[0] = x_byte_;
            g.scheduler().destroy(*this);
            return;
        }
        draw(g, box_);
        icon_.x = s16(x_ - s.camera.x);
        icon_.y = s16(y_ - s.camera.y + offset_);
        draw(g, icon_);
    }

private:
    // 0801EBF8: the icon rises for a second, then the item is given;
    // 0801F0B8: it stays half a second more.
    void broken(Game& g) {
        StageState& s = g.stage();
        if (state_ == State::rising) {
            offset_ = s16(offset_ - 1);
            if (++clock_ > 0x3B) {
                give(g);
                state_ = State::shown;
                clock_ = 0;
            }
        } else if (++clock_ >= 0x1F) {
            g.scheduler().destroy(*this);
            return;
        }
        icon_.x = s16(x_ - s.camera.x);
        icon_.y = s16(y_ + offset_ - s.camera.y);
        draw(g, icon_);
    }

    // The ring bonuses: rings added, a life every 100.
    static void add_rings(Game& g, s32 n) {
        StageState& s = g.stage();
        const u16 before = s.rings;
        s.rings = u16(s.rings + n);
        if (s.zone != 0xD && divide(s.rings, 100) != divide(before, 100) && g.game_mode == 0) {
            ++s.lives;
            s.music_one_up = 0x10;
        }
        if (g.game_mode == 6 && s.rings > 0xFF) {
            s.rings = 0xFF;
        }
        g.sound().start(0x75);
    }

    void give(Game& g) {
        StageState& s = g.stage();
        Player& p = s.player[0];
        switch (item_) {
        case 0: // a life
            ++s.lives;
            s.music_one_up = 0x10;
            break;
        case 1: // the shield
            p.power = u8((p.power & ~player_power::magnetic_shield) | player_power::shield);
            create_shield(g, p.index);
            break;
        case 2: // the magnetic shield
            p.power = u8((p.power & ~player_power::shield) | player_power::magnetic_shield);
            create_magnetic_shield(g, p.index);
            break;
        case 3: // invincibility
            if (!(p.power & player_power::invincible)) {
                create_invincibility(g, p.index);
                s.music_invincible = 0x10;
            }
            p.power |= player_power::invincible;
            p.invincible_timer = 0x4B0;
            break;
        case 4: // speed shoes
            p.power |= player_power::fast;
            p.power_timer = 0x4B0;
            g.sound().set_tempo(0, 0x200);
            break;
        case 5: // a random number of rings
            add_rings(g, g.assets().s8(0x080B8310 + s.next_random() % 6));
            break;
        case 6:
            add_rings(g, 5);
            break;
        case 7:
            add_rings(g, 10);
            break;
        default:
            throw Unrecovered("multiplayer items (0801EBF8)");
        }
    }

    enum class State { whole, rising, shown } state_ = State::whole;
    u8 clock_{}; // +70 (while broken)
    Game& game_;
    ObjectSpawn spawn_;
    u8 x_byte_; // +08
    s16 x_, y_;
    Sprite box_;     // +0C
    Sprite icon_;    // +3C
    s16 offset_{};   // +6C how far it has hopped
    s16 speed_y_{};  // +6E
    bool falling_{}; // +70
    u8 item_;        // +71
};

} // namespace

void create_item_box(Game& g, const ObjectSpawn& o) {
    if (g.game_mode == 1 && o.record[2] == 0) {
        o.record[0] = 0xFE; // no extra lives in time attack
        return;
    }
    g.scheduler().create<ItemBox>(g, o);
}

} // namespace sonic_advance_gba
