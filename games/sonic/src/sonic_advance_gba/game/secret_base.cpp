// Secret Base's objects (zone 2).

#include "sonic_advance_gba/game/secret_base.hpp"
#include "sonic_advance_gba/game/collision.hpp"
#include "sonic_advance_gba/game/game.hpp"
#include "sonic_advance_gba/game/math.hpp"
#include "sonic_advance_gba/game/object_base.hpp"
#include "sonic_advance_gba/game/object_util.hpp"
#include "sonic_advance_gba/game/solid.hpp"
#include "sonic_advance_gba/game/stage.hpp"
#include <algorithm>
#include <array>

namespace sonic_advance_gba {

namespace {

constexpr u16 sound_pulley = 0xB4;

// The player stops standing on `s` (if it was).
void step_off(Player& p, const Sprite& s) {
    if ((p.flags & player_flag::on_object) && p.standing_on == &s) {
        p.standing_on = nullptr;
        p.flags &= ~player_flag::on_object;
    }
}

// 080862CC: a pulley handle on a cable (record +3 the route): the player
// touching it hangs on (no movement of its own, state 26h) and is carried
// along, speeding up; at the end the player drops off with the handle's
// speed. Only the first player rides it.
//  0 (08086854, 080869C8): down to the left for F4h pixels, then back up
//    to the right on a lower cable (F0h below).
//  1 (08086418, 0808658C): down to the right for 1E0h pixels.
//  2 (08086EAC, 08087014): down to the left for 1E0h pixels.
class Pulley final : public Interactive {
public:
    Pulley(Game& g, const ObjectSpawn& o) : Interactive(o), game_(g), route_(o.record[3]) {
        const u16 animation = (g.stage().zone >> 1) == 3 ? 0x239 : 0x1F8;
        sprite_.start(g.sprite_tiles.allocate(6), animation, 0, x_, y_, 0x480, 0x2000);
        animate(g, sprite_);
    }

    // 08087380
    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); }

    void update(Game& g) override {
        switch (state_) {
        case State::waiting:
            wait(g);
            break;
        case State::riding:
            ride(g);
            break;
        case State::done:
            stay(g);
            break;
        }
    }

private:
    enum class State : u8 { waiting, riding, done };

    // 08086854 / 08086418 / 08086EAC: waits for the player to grab it.
    void wait(Game& g) {
        StageState& s = g.stage();
        Player& p = s.player[0];
        sprite_.x = s16(x_ - s.camera.x);
        sprite_.y = s16(y_ - s.camera.y);
        if (!(p.flags & player_flag::dead) &&
            (!(p.flags & player_flag::on_object) || p.standing_on == &sprite_) &&
            touches_player(sprite_, x_, y_, p)) {
            grab(g, p);
        }
        show(g, sprite_.x, sprite_.y);
    }

    void grab(Game& g, Player& p) {
        const s32 speed = p.ground_speed < 0 ? -p.ground_speed : p.ground_speed;
        switch (route_) {
        case 1:
            end_player_moves(g, p);
            p.flags = player_flag::no_movement;
            p.speed_y = 0;
            p.state = 0x26;
            p.y = (y_ + 0x24) * 0x100;
            p.x = (x_ - 6) * 0x100;
            speed_ = speed < 0x100 ? 0x100 : speed;
            break;
        case 2:
            speed_ = speed;
            p.y = (y_ + 0x26) * 0x100;
            p.x = x_ << 8;
            p.angle = 0;
            end_player_moves(g, p);
            p.flags = player_flag::no_movement | player_flag::facing_left;
            p.speed_y = 0;
            p.state = 0x26;
            break;
        default:
            speed_ = -speed;
            p.y = (y_ + 0x24) * 0x100;
            p.x = x_ << 8;
            end_player_moves(g, p);
            p.flags = player_flag::no_movement;
            p.angle = 0;
            p.speed_y = 0;
            p.state = 0x26;
            break;
        }
        if (route_ == 1) {
            p.angle = 0;
        }
        start_x_ = p.x;
        distance_ = 0;
        set_player_radius(g, p, 6, 0xE);
        p.radius_x = 6;
        p.radius_y = 0xE;
        g.sound().start(sound_pulley);
        state_ = State::riding;
        p.flags |= player_flag::on_object;
        p.power |= 0x80;
        p.standing_on = &sprite_;
    }

    // 080869C8 / 0808658C / 08087014
    void ride(Game& g) {
        StageState& s = g.stage();
        Player& p = s.player[0];
        if (p.flags & player_flag::dead) {
            ride_alone(g);
            return;
        }
        if (p.state != 0x26) {
            end_player_moves(g, p);
            p.state = 0x26;
        }
        p.speed_y = 0;
        bool off = false;
        switch (route_) {
        case 1:
            if ((p.x - start_x_) >> 8 < 0x1E1) {
                carry_along(g, p);
            } else {
                off = true;
            }
            break;
        case 2:
            if ((start_x_ - p.x) >> 8 < 0x1E1) {
                carry_along(g, p);
            } else {
                off = true;
            }
            break;
        default:
            if (!lower_) {
                speed_ = std::max(speed_ - 9, s32(-0x600));
                distance_ += speed_;
                p.x = x_ * 0x100 - 0x600 + distance_;
                p.y = (y_ + 0x26) * 0x100 - (distance_ >> 1);
                if ((p.x - start_x_) >> 8 < -0xF4) {
                    lower_ = true;
                }
            } else {
                speed_ += 0x14;
                distance_ += speed_;
                p.x = x_ * 0x100 - 0x600 + distance_;
                p.y = (y_ + 0x114) * 0x100 + (distance_ >> 1);
                off = distance_ >> 8 > 2;
            }
            break;
        }
        if (off) {
            let_go(g, p);
        }
        const s16 dx = route_ == 2 ? -4 : 5;
        sprite_.x = s16((p.x >> 8) - s.camera.x + dx);
        sprite_.y = s16((p.y >> 8) - s.camera.y - 0x24);
        show(g, sprite_.x, sprite_.y);
    }

    // Speeding up along the cable, the player hanging below.
    void carry_along(Game& g, Player& p) {
        speed_ += 0x10;
        distance_ += speed_;
        if (route_ == 1) {
            p.x = x_ * 0x100 - 0x600 + distance_;
            p.y = y_ * 0x100 + 0x2400 + (distance_ >> 1);
        } else {
            p.x = (x_ + 6) * 0x100 - distance_;
            p.y = y_ * 0x100 + 0x2600 + (distance_ >> 1);
        }
        end_player_moves(g, p);
        p.state = 0x26;
    }

    // The end of the cable: the player drops with the handle's speed.
    void let_go(Game& g, Player& p) {
        g.sound().stop(sound_pulley);
        end_player_moves(g, p);
        p.flags &= ~player_flag::no_movement;
        if (route_ == 2) {
            p.flags |= player_flag::facing_left;
        }
        p.state = 4;
        set_player_radius(g, p, 6, 0xE);
        p.radius_x = 6;
        p.radius_y = 0xE;
        state_ = State::done;
        p.ground_speed = s16(route_ == 2 ? -speed_ : speed_);
        p.y -= route_ == 2 ? 0x200 : 0xA00;
        if (route_ == 1) {
            p.angle = 0;
        }
        p.power &= 0x7F;
        step_off(p, sprite_);
    }

    // The player died on the way: the handle runs on alone.
    void ride_alone(Game& g) {
        StageState& s = g.stage();
        Player& p = s.player[0];
        bool end = false;
        switch (route_) {
        case 1:
            step_off(p, sprite_);
            speed_ += 0x10;
            distance_ += speed_;
            sprite_.x = s16(x_ + (distance_ >> 8) - s.camera.x);
            sprite_.y = s16(y_ + (distance_ >> 9) - s.camera.y);
            end = distance_ >> 8 > 0x1E0;
            break;
        case 2:
            speed_ += 0x10;
            distance_ += speed_;
            sprite_.x = s16(x_ - (distance_ >> 8) - s.camera.x);
            sprite_.y = s16(y_ + (distance_ >> 9) - s.camera.y);
            end = (start_x_ - (x_ * 0x100 - distance_)) >> 8 > 0x1E0;
            break;
        default:
            if (!lower_) {
                speed_ = std::max(speed_ - 9, s32(-0x600));
                distance_ += speed_;
                if ((x_ * 0x100 + distance_ - start_x_) >> 8 < -0xF4) {
                    lower_ = true;
                }
                sprite_.y = s16(y_ - (distance_ >> 9) - s.camera.y);
            } else {
                speed_ += 0x14;
                distance_ += speed_;
                sprite_.y = s16(y_ + (distance_ >> 9) - s.camera.y + 0xF0);
                end = distance_ >> 8 > 2;
            }
            sprite_.x = s16(x_ + (distance_ >> 8) - s.camera.x);
            break;
        }
        if (end) {
            g.sound().stop(sound_pulley);
            state_ = State::done;
            step_off(p, sprite_);
        }
        show(g, sprite_.x, sprite_.y);
    }

    // 08086E0C / 08086D5C / 080872CC: at the cable's other end.
    void stay(Game& g) {
        const auto& c = g.stage().camera;
        const s16 dx = route_ == 1 ? 0x1E0 : route_ == 2 ? -0x1E0 : 0;
        sprite_.x = s16(x_ - c.x + dx);
        sprite_.y = s16(y_ - c.y + 0xF0);
        show(g, sprite_.x, sprite_.y);
    }

    // Drawn near the view; otherwise gone (the layout gets it back).
    void show(Game& g, s16 sx, s16 sy) {
        if (near_view(sx, sy)) {
            draw(g, sprite_);
            return;
        }
        step_off(g.stage().player[0], sprite_);
        give_back();
        g.scheduler().destroy(*this);
    }

    Game& game_;
    u8 route_;       // record +3
    Sprite sprite_;  // +0C
    s32 speed_{};    // +40 8.8
    s32 start_x_{};  // +48 where the player grabbed it (24.8)
    s32 distance_{}; // +4C 8.8
    bool lower_{};   // +50 route 0: on the lower cable
    State state_ = State::waiting;
};

// 080706A8: walks to and fro within its range (record +3: start, +5:
// width, in 8 pixels); a player ahead (up to 50h pixels) makes it stop and
// strike for 60 frames (080709BC), its body then hurting on touch; it walks
// on for at least 120 frames before striking again. Not made in Secret
// Base while the save's byte +18 is set.
class Striker final : public Enemy {
public:
    Striker(Game& g, const ObjectSpawn& o) : Enemy(g, o, 0x10) {
        sprite_.start(tiles_, 0x19B, 0, s16(base_x_), s16(base_y_), 0x480, 0x2000);
        animate(g, sprite_);
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        if (!striking_) {
            offset_x_ += speed_;
        }
        const s16 x = s16(base_x_ + (offset_x_ >> 8));
        const s16 y = s16(base_y_ + offset_y_);
        sprite_.x = s16(x - s.camera.x);
        sprite_.y = s16(y - s.camera.y);
        if (out_of_range(s, s16(base_x_), s16(base_y_), sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        if (striking_) {
            strike(g, x, y);
        } else {
            if (enemy_touched(g, sprite_, x, y)) {
                g.scheduler().destroy(*this);
                return;
            }
            walk(s, x);
        }
        animate(g, sprite_);
        draw(g, sprite_);
    }

private:
    // 080707D4
    void walk(const StageState& s, s16 x) {
        const s32 at = offset_x_ >> 8;
        if ((param(3) + 1) * 8 >= at) {
            speed_ = 0xA0;
            sprite_.flags |= sprite_flag::flip_x;
            return;
        }
        if (at >= (param(3) + s32(spawn_.record[5]) - 1) * 8) {
            speed_ = -0xA0;
            sprite_.flags &= ~sprite_flag::flip_x;
            return;
        }
        if (rest_ != 0) {
            --rest_;
            return;
        }
        const s32 px = s.player[0].x >> 8;
        const bool ahead = (sprite_.flags & sprite_flag::flip_x) ? (px >= x && px <= x + 0x50)
                                                                 : (px <= x && px >= x - 0x50);
        if (ahead) {
            timer_ = 0;
            sprite_.variant = 1;
            striking_ = true;
        }
    }

    // 080709BC
    void strike(Game& g, s16 x, s16 y) {
        Player& p = g.stage().player[0];
        if (sprite_.hitboxes[0].index != 0xFFFFFFFF &&
            hitboxes_meet(x, y, sprite_.hitboxes[0], p.x >> 8, p.y >> 8,
                          p.body->sprite.hitboxes[0])) {
            hurt_player(g, p);
        }
        if (++timer_ > 0x3B) {
            rest_ = 0x78;
            sprite_.variant = 0;
            striking_ = false;
        }
    }

    Sprite sprite_;     // +0C
    u16 offset_y_{};    // +3C
    s32 offset_x_{};    // +40 8.8
    s16 timer_{};       // +44
    s16 rest_{};        // +46
    s16 speed_ = -0xA0; // +48
    bool striking_{};   // the update is 080709BC (else 080707D4)
};

// 08070F90, 0807104C: two sparks thrown apart (in quarter pixels across)
// that fall and hurt a player they touch; drawn with fixed tiles.
class Spark final : public Process {
public:
    Spark(Game& g, s16 x, s16 y, s16 speed) : Process(0x3000), x4_(s16(x << 2)), speed_x_(speed) {
        sprite_.start(0x12580, 0x19E, 0, s16(speed > 0 ? x + 8 : x - 8), y, 0x240, 0x2000);
        animate(g, sprite_);
    }

    void update(Game& g) override {
        const auto& c = g.stage().camera;
        x4_ = s16(x4_ + speed_x_);
        s32 q = x4_;
        if (q < 0) {
            q += 3;
        }
        sprite_.x = s16(q >> 2);
        speed_y_ = s16(speed_y_ + 0x28);
        sprite_.y = s16(sprite_.y + s8(u16(speed_y_) >> 8));
        const s16 x = sprite_.x, y = sprite_.y;
        projectile_hits(g, sprite_, x, y);
        sprite_.x = s16(x - c.x);
        sprite_.y = s16(y - c.y);
        if (u16(sprite_.x + 0x14) < 0x115 && sprite_.y + 0x14 >= 0 && sprite_.y < 0xF1) {
            animate(g, sprite_);
            draw(g, sprite_);
            sprite_.x = x;
            sprite_.y = y;
        } else {
            g.scheduler().destroy(*this);
        }
    }

private:
    Sprite sprite_;        // +00 (x, y in the act between frames)
    s16 x4_;               // +30
    s16 speed_x_;          // +32
    s16 speed_y_ = -0x200; // +34
};

// 08070D24, 08070E68: a ball thrown up from a launcher (drawn twice, the
// second mirrored); at the top it bursts into two sparks and falls away.
// It hurts a player it touches.
class LauncherBall final : public Enemy {
public:
    LauncherBall(Game& g, const ObjectSpawn& o, u8 x_byte) : Enemy(g, o, 8) {
        x_byte_ = x_byte;
        x_ = s16(base_x_);
        y_ = s16(base_y_ + 0x18);
        start_y_ = y_;
        sprite_.start(tiles_, 0x19D, 0, s16(base_x_), s16(base_y_), 0x480, 0x2000);
        animate(g, sprite_);
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        sprite_.x = s16(x_ - s.camera.x);
        speed_y_ = s16(speed_y_ + 0x40);
        y_ = s16(y_ + s8(u16(speed_y_) >> 8));
        sprite_.y = s16(y_ - s.camera.y);
        const auto& c = s.camera;
        const bool near = x_ <= c.x + 0x170 && c.x - 0x80 <= x_ && start_y_ <= c.y + 0x120 &&
                          c.y - 0x80 <= start_y_;
        if (!near && (u16(sprite_.x + 0x80) > 0x1F0 || sprite_.y + 0x80 < 0 || sprite_.y > 0x120)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        Player& p = s.player[0];
        if (!(p.flags & player_flag::dead) && touches_player(sprite_, x_, y_, p)) {
            hurt_player(g, p);
        }
        if (!falling_) {
            if (speed_y_ >= 0) {
                g.scheduler().create<Spark>(g, x_, s16(y_ - 0x10), s16(6));
                g.scheduler().create<Spark>(g, x_, s16(y_ - 0x10), s16(-6));
                sprite_.previous_variant = 0xFF;
                sprite_.variant = 1;
                falling_ = true;
            }
        } else if (speed_y_ >= 0x200) {
            g.scheduler().destroy(*this);
            return;
        }
        animate(g, sprite_);
        draw(g, sprite_);
        sprite_.flags |= sprite_flag::flip_x;
        draw(g, sprite_);
        sprite_.flags &= ~sprite_flag::flip_x;
    }

private:
    Sprite sprite_;        // +0C
    s16 speed_y_ = -0x600; // +3C
    s16 x_, y_;            // +3E, +40
    s32 start_y_;          // +44
    bool falling_{};       // the update is 08070E68 (else 08070D24)
};

// 080710F0, 08070B9C: a launcher (not drawn) throwing a ball every 240
// frames while its place is near the view.
class Launcher final : public Process {
public:
    explicit Launcher(const ObjectSpawn& o) : Process(0x2000), spawn_(o), x_byte_(o.record[0]) {
        o.record[0] = 0xFE;
    }

    void update(Game& g) override {
        const StageState& s = g.stage();
        const s16 x = s16(x_byte_ * 8 + spawn_.cell_x * 0x100);
        const s16 y = s16(spawn_.record[1] * 8 + spawn_.cell_y * 0x100);
        const auto& c = s.camera;
        if (x <= c.x + 0x170 && c.x - 0x80 <= x && y <= c.y + 0x120 && c.y - 0x80 <= y) {
            if (remainder(s32(s.ticks + counter_), 0xF0) == 0) {
                spawn_.record[0] = x_byte_;
                g.scheduler().create<LauncherBall>(g, spawn_, x_byte_);
            }
            return;
        }
        spawn_.record[0] = x_byte_;
        g.scheduler().destroy(*this);
    }

private:
    ObjectSpawn spawn_; // +00, +08, +0A
    u8 x_byte_;         // +06
    u16 counter_{};     // +04
};

// 08071148: hides (record +4 zero: stays harmless, 0807143C); otherwise
// every 60 frames it comes out facing the player for 60 frames (08071260,
// 08071348). Attacked while out it is beaten (the points and an animal) and
// hides for good (its record's +4 cleared).
class Popper final : public Enemy {
public:
    Popper(Game& g, const ObjectSpawn& o) : Enemy(g, o, 0x10), active_(o.record[4] != 0) {
        sprite_.start(tiles_, 0x1B2, 0, s16(base_x_), s16(base_y_), 0x480, 0x2000);
        animate(g, sprite_);
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        const s16 x = s16(base_x_), y = s16(base_y_);
        sprite_.x = s16(x - s.camera.x);
        sprite_.y = s16(y - s.camera.y);
        if (!(u16(sprite_.x + 0x80) < 0x1F1 && sprite_.y + 0x80 >= 0 && sprite_.y < 0x121)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        if (active_) {
            if (!out_) {
                if (++timer_ > 0x3B) {
                    timer_ = 0;
                    sprite_.variant = 1;
                    if ((s.player[0].x >> 8) < x) {
                        sprite_.flags &= ~sprite_flag::flip_x;
                    } else {
                        sprite_.flags |= sprite_flag::flip_x;
                    }
                    out_ = true;
                }
            } else {
                if (++timer_ > 0x3B) {
                    timer_ = 0;
                    sprite_.variant = 0;
                    out_ = false;
                }
                if (enemy_touched(g, sprite_, x, y)) {
                    sprite_.variant = 0;
                    spawn_.record[4] = 0;
                    active_ = false;
                }
            }
        }
        animate(g, sprite_);
        draw(g, sprite_);
    }

private:
    Sprite sprite_; // +0C
    s16 timer_{};   // +3C
    bool active_;   // the update is 08071260/08071348 (else 0807143C)
    bool out_{};    // 08071348
};

// 08093F3C, 08094010: a waypoint of a tube (not drawn; record +6 its
// stage). A player within 10 pixels whose last stage is lower (or any
// player at stage 0) is pulled in, rolled up and hidden from the movement
// routines, and carried in record +5 steps by (+3, +4) x 8 pixels; at the
// end the next waypoint takes over, or the player flies out.
class TubeWaypoint final : public Interactive {
public:
    explicit TubeWaypoint(const ObjectSpawn& o)
        : Interactive(o, u16(0x2000 | o.record[6])), stage_(o.record[6]), steps_(o.record[5]),
          dx_(s8(o.record[3]) * 8), dy_(s8(o.record[4]) * 8) {}

    void update(Game& g) override {
        StageState& s = g.stage();
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            Carry& c = carry_[n];
            const u8 bit = u8(1 << n);
            if (!(carried_ & bit)) {
                if (p.flags & player_flag::dead) {
                    p.flags &= ~player_flag::hidden;
                    continue;
                }
                if ((p.tube_stage < s32(stage_) || stage_ == 0) && x_ - 10 <= (p.x >> 8) &&
                    (p.x >> 8) <= x_ + 10 && y_ - 10 <= (p.y >> 8) && (p.y >> 8) <= y_ + 10) {
                    pull_in(g, p, c, bit);
                }
                continue;
            }
            if (p.flags & player_flag::dead) {
                p.flags &= ~(player_flag::hidden | player_flag::no_movement);
                carried_ &= u8(~bit);
                continue;
            }
            if (p.tube_stage != s32(stage_)) {
                carried_ &= u8(~bit);
                continue;
            }
            carry(g, p, c, bit);
        }
        if ((carried_ & 3) == 0 && !near_view(s16(x_ - s.camera.x), s16(y_ - s.camera.y))) {
            give_back();
            g.scheduler().destroy(*this);
        }
    }

private:
    struct Carry {
        u8 step{};                // +3F
        s32 start_x{}, start_y{}; // +44, +4C
        s32 last_x{}, last_y{};   // +5C, +64 (24.8)
    };

    void pull_in(Game& g, Player& p, Carry& c, u8 bit) {
        c.step = 0;
        end_player_moves(g, p);
        p.flags |= player_flag::rolling;
        p.flags |= player_flag::no_movement;
        p.x = x_ << 8;
        p.y = y_ << 8;
        c.start_x = x_;
        c.start_y = y_;
        p.speed_x = p.speed_y = p.ground_speed = 0;
        p.state = 5;
        roll_up(g, p);
        g.sound().start(0xCA);
        carried_ |= bit;
        p.tube_stage = s8(stage_);
        p.power |= 0x80;
    }

    void carry(Game& g, Player& p, Carry& c, u8 bit) {
        p.x = (c.start_x + divide(c.step * dx_, steps_)) * 0x100;
        p.y = (c.start_y + divide(c.step * dy_, steps_)) * 0x100;
        p.speed_y = s16(p.y - c.last_y);
        p.ground_speed = s16(p.x - c.last_x);
        p.speed_x = p.ground_speed;
        ++c.step;
        if ((p.x >> 8) == c.start_x + dx_ && (p.y >> 8) == c.start_y + dy_) {
            // The end of this stretch: the next waypoint takes the player
            // on, or (none there) it is let go into the air.
            if (p.tube_stage == s32(stage_)) {
                p.flags = (p.flags & ~player_flag::no_movement) | player_flag::in_air;
                p.power &= 0x7F;
            }
            carried_ &= u8(~bit);
        } else {
            end_player_moves(g, p);
            p.flags = (p.flags & ~player_flag::facing_left) | player_flag::no_movement;
            p.state = 5;
            roll_up(g, p);
            carried_ |= bit;
        }
        c.last_x = p.x;
        c.last_y = p.y;
    }

    static void roll_up(Game& g, Player& p) {
        set_player_radius(g, p, 6, 0xE);
        p.radius_x = 6;
        p.radius_y = 0xE;
    }

    u8 stage_;    // +3C (per player, both the record's +6)
    u8 steps_;    // +3E
    s32 dx_, dy_; // +54, +58
    std::array<Carry, 2> carry_{};
    u8 carried_{}; // +6C bit per player
};

// 08093E9C, 08093C68: an invisible hazard (record +5 x +6 eight-pixel
// units from its place): a player inside is hurt (unless invincible), or
// with record +3 set dies at once. Players carried by an object (no
// movement of their own) are left alone.
class HazardBox final : public Interactive {
public:
    explicit HazardBox(const ObjectSpawn& o) : Interactive(o) {}

    void update(Game& g) override {
        StageState& s = g.stage();
        const u8* r = spawn_.record;
        if (!near_view(s16(x_ - s.camera.x), s16(y_ - s.camera.y))) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if (p.flags & player_flag::no_movement) {
                continue;
            }
            const s32 px = p.x >> 8, py = p.y >> 8;
            if (px < x_ || px > x_ + r[5] * 8 || py < y_ || py > y_ + r[6] * 8 ||
                (p.flags & player_flag::dead)) {
                continue;
            }
            if (r[3] != 0) {
                p.flags |= player_flag::dead;
            } else if (!(p.power & player_power::invincible)) {
                hurt_player(g, p);
            }
        }
    }
};

// 080920F0, 08091F24: a player passing within 8 pixels starts rolling.
class RollPoint final : public Interactive {
public:
    explicit RollPoint(const ObjectSpawn& o) : Interactive(o) {}

    void update(Game& g) override {
        StageState& s = g.stage();
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            const s32 px = p.x >> 8, py = p.y >> 8;
            if ((p.flags & player_flag::dead) || px < x_ - 8 || px > x_ + 8 || py < y_ - 8 ||
                py > y_ + 8) {
                continue;
            }
            end_player_moves(g, p);
            p.flags |= player_flag::rolling;
            p.state = 5;
            set_player_radius(g, p, 6, 9);
            p.radius_x = 6;
            p.radius_y = 9;
        }
        if (!near_view(s16(x_ - s.camera.x), s16(y_ - s.camera.y))) {
            give_back();
            g.scheduler().destroy(*this);
        }
    }
};

// Inside the box record +5 x +6 eight-pixel units from (x, y).
bool inside_box(const Player& p, s16 x, s16 y, const u8* record) {
    const s32 px = p.x >> 8, py = p.y >> 8;
    return x <= px && px <= x + record[5] * 8 && y <= py && py <= y + record[6] * 8;
}

// 0808425C, 080840C0: a box hiding the players inside it (record +3 set)
// or showing them again (+3 zero).
class HidingBox final : public Interactive {
public:
    explicit HidingBox(const ObjectSpawn& o)
        : Interactive(o, u16(0x2000 | o.record[6])), hides_(o.record[3] != 0) {}

    void update(Game& g) override {
        StageState& s = g.stage();
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if (p.flags & player_flag::dead) {
                p.flags &= ~player_flag::hidden;
            } else if (inside_box(p, x_, y_, spawn_.record)) {
                if (hides_) {
                    p.flags |= player_flag::hidden;
                } else {
                    p.flags &= ~player_flag::hidden;
                }
            }
        }
        if (!near_view(s16(x_ - s.camera.x), s16(y_ - s.camera.y))) {
            give_back();
            g.scheduler().destroy(*this);
        }
    }

private:
    bool hides_; // +3C
};

// 08084038, 08083B7C: a conveyor (not drawn): players on the ground in
// the box are moved A0h (8.8) a frame, to the left (record +3 zero) or to
// the right, kept out of walls.
class Conveyor final : public Interactive {
public:
    explicit Conveyor(const ObjectSpawn& o) : Interactive(o), rightward_(o.record[3] != 0) {}

    void update(Game& g) override {
        StageState& s = g.stage();
        const Assets& a = g.assets();
        const auto wall = [&](const Player& p, s32 dx, s32 step) {
            return probe_short(a, s.map, (p.x >> 8) + dx, p.y >> 8, p.layer, step, nullptr,
                               Sensor::wall);
        };
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if ((p.flags & (player_flag::dead | player_flag::in_air)) ||
                !inside_box(p, x_, y_, spawn_.record)) {
                continue;
            }
            if (!rightward_) {
                p.x -= 0xA0;
            } else {
                p.x += 0xA0;
                if (wall(p, 8, 8) < 0) {
                    p.x += wall(p, 8, 8) * 0x100;
                }
            }
            if (wall(p, -8, -8) < 0) {
                p.x -= wall(p, -8, -8) * 0x100;
            }
        }
        if (!near_view(s16(x_ - s.camera.x), s16(y_ - s.camera.y))) {
            give_back();
            g.scheduler().destroy(*this);
        }
    }

private:
    bool rightward_; // +3C
};

// 08084038 (in Ice Paradise), 08083E8C: ground that drags (not drawn; the
// box record +5 x +6 eight-pixel units): players on the ground inside it
// keep 95/100 of their speed each frame.
class DraggingGround final : public Interactive {
public:
    explicit DraggingGround(const ObjectSpawn& o) : Interactive(o) {}

    void update(Game& g) override {
        StageState& s = g.stage();
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if ((p.flags & (player_flag::dead | player_flag::in_air)) ||
                !inside_box(p, x_, y_, spawn_.record)) {
                continue;
            }
            p.ground_speed = s16(divide(p.ground_speed * 0x5F, 100));
        }
        if (!near_view(s16(x_ - s.camera.x), s16(y_ - s.camera.y))) {
            give_back();
            g.scheduler().destroy(*this);
        }
    }
};

// 08082588, 08082650: a chute (not drawn; record +4 its stage, as the
// tube waypoints'): a player within 12 pixels (in stage 0, or carried
// from an earlier stage) is rolled up and falls straight down, drawn to
// the chute's middle in Secret Base, for record +5 x 3 frames or record
// +3 x 8 pixels.
class Chute final : public Interactive {
public:
    explicit Chute(const ObjectSpawn& o)
        : Interactive(o, u16(0x2000 + s8(o.record[4]))), frames_(u16(o.record[5] * 3)),
          depth_(s16(s8(o.record[3]) << 3)), stage_(o.record[4]) {}

    void update(Game& g) override {
        StageState& s = g.stage();
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            const u8 bit = u8(1 << n);
            if (p.flags & player_flag::dead) {
                if (carried_ & bit) {
                    carried_ &= u8(~bit);
                    p.flags &= ~player_flag::no_movement;
                }
                continue;
            }
            if (!(carried_ & bit)) {
                const bool may_enter =
                    ((p.flags & player_flag::no_movement) && p.tube_stage < s32(stage_)) ||
                    stage_ == 0;
                const s32 px = p.x >> 8, py = p.y >> 8;
                if (may_enter && x_ - 0xC <= px && px <= x_ + 0xC && y_ - 0xC <= py &&
                    py <= y_ + 0xC) {
                    catch_player(g, p, n);
                }
                continue;
            }
            fall(g, s, p, n);
        }
        if ((carried_ & 3) == 0 && !near_view(s16(x_ - s.camera.x), s16(y_ - s.camera.y))) {
            give_back();
            g.scheduler().destroy(*this);
        }
    }

private:
    void catch_player(Game& g, Player& p, unsigned n) {
        timer_[n] = 0;
        fallen_[n] = 0;
        p.flags |= player_flag::no_movement | player_flag::rolling;
        p.speed_y = 0;
        p.speed_x = 0;
        p.ground_speed = 0;
        hold(g, p);
        g.sound().start(0xAA);
        carried_ |= u8(1 << n);
        p.tube_stage = s8(stage_);
        p.power |= 0x80;
    }

    void fall(Game& g, const StageState& s, Player& p, unsigned n) {
        p.speed_y = s16(p.speed_y + 0x2A);
        p.y += p.speed_y;
        fallen_[n] += p.speed_y;
        if ((s.zone >> 1) == 1 || s.zone == 15) {
            const s32 middle = x_ * 0x100;
            if (p.x >= middle) {
                p.x = std::max(p.x - 0x80, middle);
            } else {
                p.x = std::min(p.x + 0x80, middle);
            }
        }
        if (s32(stage_) < p.tube_stage || ++timer_[n] >= frames_ || depth_ < (fallen_[n] >> 8)) {
            if (p.tube_stage == s32(stage_)) {
                p.tube_stage = 0x7F;
                p.flags &= ~player_flag::no_movement;
                p.power &= 0x7F;
            }
            carried_ &= u8(~(1 << n));
            return;
        }
        p.flags |= player_flag::no_movement;
        p.speed_x = 0;
        p.ground_speed = 0;
        hold(g, p);
        carried_ |= u8(1 << n);
    }

    static void hold(Game& g, Player& p) {
        set_player_radius(g, p, 6, 0xE);
        p.radius_x = 6;
        p.radius_y = 0xE;
    }

    std::array<s32, 2> fallen_{}; // +3C 8.8
    std::array<u16, 2> timer_{};  // +44
    u16 frames_;                  // +48
    s16 depth_;                   // +4A
    u8 carried_{};                // +4C bit per player
    u8 stage_;                    // +4D
};

// 08088094: a gate that drops shut (30h pixels in 8 frames) once the
// first player enters the box (record +5 x +6 eight-pixel units, from +5 x
// 8 - 10 pixels left of it, or 10 with record +3 set); shut, its sides are
// solid.
class DroppingGate final : public Interactive {
public:
    DroppingGate(Game& g, const ObjectSpawn& o) : Interactive(o), game_(g) {
        reach_ = o.record[3] == 0 ? s16(o.record[5] * 8 - 10) : s16(-10);
        sprite_.start(g.sprite_tiles.allocate(0xC), 0x216, 0, x_, y_, 0x480, 0x2000);
    }

    // 08088518
    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); }

    void update(Game& g) override {
        StageState& s = g.stage();
        sprite_.x = s16(x_ - s.camera.x);
        sprite_.y = s16(y_ - s.camera.y + drop_);
        switch (state_) {
        case State::open: { // 08088170
            const Player& p = s.player[0];
            const s32 left = x_ - reach_, px = p.x >> 8, py = p.y >> 8;
            if (!(p.flags & player_flag::dead) && left <= px && px <= left + spawn_.record[5] * 8 &&
                y_ <= py && py <= y_ + spawn_.record[6] * 8) {
                drop_ = 0;
                frames_ = 8;
                state_ = State::dropping;
            }
            break;
        }
        case State::dropping: // 080882C4
            if (out_of_range(s, x_, y_, sprite_.x, sprite_.y)) {
                break;
            }
            if (--frames_ == 0) {
                drop_ = 0x30;
                state_ = State::shut;
            } else {
                drop_ = s8(drop_ + 6);
            }
            animate(g, sprite_);
            draw(g, sprite_);
            return;
        case State::shut: // 080883CC
            for (unsigned n = 0; n < s.players; ++n) {
                Player& p = s.player[n];
                if (!(p.flags & player_flag::dead)) {
                    collide_wall(g, sprite_, x_, s16(y_ + drop_), p, false);
                }
            }
            break;
        }
        if (out_of_range(s, x_, y_, sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        animate(g, sprite_);
        draw(g, sprite_);
    }

private:
    enum class State : u8 { open, dropping, shut };

    Game& game_;
    Sprite sprite_; // +0C
    s8 drop_{};     // +3C
    u8 frames_{};   // +3D
    s16 reach_;     // +3E
    State state_ = State::open;
};

// 08074888, 08074958: one of two pellets thrown apart (x in quarter
// pixels), falling; it hurts a player it touches. Its tiles are its own
// (080749F0).
class Pellet final : public Process {
public:
    Pellet(Game& g, s16 x, s16 y, bool right) : Process(0x3000), game_(g) {
        x4_ = right ? (x + 6) * 4 : (x - 6) * 4;
        speed_x_ = right ? 6 : -6;
        sprite_.start(g.sprite_tiles.allocate(4), 0x19A, 0, s16(right ? x + 8 : x - 8), y, 0x240,
                      0x2000);
        animate(g, sprite_);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); }

    void update(Game& g) override {
        const auto& c = g.stage().camera;
        x4_ += speed_x_;
        sprite_.x = s16(x4_ >> 2);
        speed_y_ = s16(speed_y_ + 0x28);
        sprite_.y = s16(sprite_.y + s8(u16(speed_y_) >> 8));
        const s16 x = sprite_.x, y = sprite_.y;
        projectile_hits(g, sprite_, x, y);
        sprite_.x = s16(x - c.x);
        sprite_.y = s16(y - c.y);
        if (u16(sprite_.x + 0x14) < 0x115 && sprite_.y + 0x14 >= 0 && sprite_.y < 0xF1) {
            animate(g, sprite_);
            draw(g, sprite_);
            sprite_.x = x;
            sprite_.y = y;
        } else {
            g.scheduler().destroy(*this);
        }
    }

private:
    Game& game_;
    Sprite sprite_; // +00 (x, y in the act between frames)
    s32 x4_;        // +30
    s32 speed_x_;   // +34
    s16 speed_y_{}; // +38
};

// 0807406C: hovers about its place on a looping path (record +5 and +6
// its size; drawn twice, the second mirrored). Every 64 steps it pauses
// (08074630); a player within 80 pixels then makes it fire two pellets
// (08074444).
//
// The path's offsets are applied as the difference from the previous
// frame's (and, across, sometimes as the previous value itself), so the
// enemy only wobbles about its place; vertically the difference is taken
// modulo 64 in the moving phase. This is what the original computes.
class Hoverer final : public Enemy {
public:
    Hoverer(Game& g, const ObjectSpawn& o) : Enemy(g, o, 6) {
        sprite_.start(tiles_, 0x19C, 0, s16(base_x_), s16(base_y_), 0x480, 0x2000);
        animate(g, sprite_);
    }

    void update(Game& g) override {
        switch (state_) {
        case State::moving:
            move(g);
            break;
        case State::pausing:
            pause(g);
            break;
        case State::firing:
            fire(g);
            break;
        }
    }

private:
    enum class State : u8 { moving, pausing, firing };

    // The path's next point, from `step`.
    void path(const Assets& a, u16 step) {
        const u8* r = spawn_.record;
        across_ = (r[5] * 0x800 * s32(sine(a, (8 * (step & 0x7F)) & 0x3FF))) >> 14;
        down_ = (r[6] * 0x800 * s32(sine(a, ((8 * (step & 0x3F)) & 0x3FF) + 0x100))) >> 14;
    }

    void show(Game& g) {
        animate(g, sprite_);
        draw(g, sprite_);
        sprite_.flags |= sprite_flag::flip_x;
        draw(g, sprite_);
        sprite_.flags &= ~sprite_flag::flip_x;
    }

    bool near(const StageState& s) const {
        const auto& c = s.camera;
        return (base_x_ <= c.x + 0x170 && c.x - 0x80 <= base_x_ && base_y_ <= c.y + 0x120 &&
                c.y - 0x80 <= base_y_) ||
               (u16(sprite_.x + 0x80) < 0x1F1 && sprite_.y + 0x80 >= 0 && sprite_.y < 0x121);
    }

    // 08074218
    void move(Game& g) {
        StageState& s = g.stage();
        const s32 old_across = across_, old_down = down_;
        path(g.assets(), step_);
        if (++step_ == 0x40 || step_ == 0x80) {
            timer_ = 0;
            sprite_.variant = 0;
            state_ = State::pausing;
        }
        step_ &= 0x7F;
        const s32 dx = (across_ - old_across) >> 5;
        sprite_.x = s16(base_x_ - s.camera.x + dx);
        const s32 dy = (down_ - old_down) >> 4;
        const s32 wrapped = remainder(dy, 0x40);
        bool beaten;
        if (s16(wrapped) == 0x3F) {
            sprite_.y = s16(wrapped);
            beaten = enemy_touched(g, sprite_, s16(base_x_ + dx), s16(base_y_));
            if (!beaten) {
                sprite_.y = s16(base_y_ - s.camera.y);
            }
        } else {
            sprite_.y = s16(base_y_ - s.camera.y - wrapped);
            beaten = enemy_touched(g, sprite_, s16(base_x_ + dx), s16(base_y_ - dy));
        }
        if (beaten) {
            g.scheduler().destroy(*this);
            return;
        }
        if (near(s)) {
            show(g);
            return;
        }
        give_back();
        g.scheduler().destroy(*this);
    }

    // The resting phases' place: the path one step back.
    void hold_place(const StageState& s, const Assets& a, s32& dx, s32& dy) {
        const s32 old_across = across_, old_down = down_;
        path(a, u16(step_ - 1));
        dx = -old_across >> 5;
        dy = (down_ - old_down) >> 4;
        sprite_.x = s16(base_x_ - s.camera.x + dx);
        sprite_.y = s16(base_y_ - s.camera.y - dy);
    }

    // 08074630
    void pause(Game& g) {
        StageState& s = g.stage();
        s32 dx, dy;
        hold_place(s, g.assets(), dx, dy);
        ++timer_;
        if (out_of_range(s, s16(base_x_), s16(base_y_), sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        if (enemy_touched(g, sprite_, s16(base_x_ + dx), s16(base_y_ - dy))) {
            g.scheduler().destroy(*this);
            return;
        }
        const Player& p = s.player[0];
        const s32 ddx = ((p.x >> 8) - s.camera.x) - sprite_.x;
        const s32 ddy = ((p.y >> 8) - s.camera.y) - sprite_.y;
        if (ddy * ddy + ddx * ddx < 0x1900) {
            timer_ = 0;
            sprite_.variant = 2;
            state_ = State::firing;
        } else if (timer_ >= 0x3C) {
            timer_ = 0;
            sprite_.variant = 1;
            state_ = State::moving;
        }
        show(g);
    }

    // 08074444
    void fire(Game& g) {
        StageState& s = g.stage();
        s32 dx, dy;
        hold_place(s, g.assets(), dx, dy);
        if (++timer_ == 0xF) {
            g.scheduler().create<Pellet>(g, s16(base_x_ + dx), s16(base_y_ - dy - 5), true);
            g.scheduler().create<Pellet>(g, s16(base_x_ + dx), s16(base_y_ - dy - 5), false);
        }
        if (out_of_range(s, s16(base_x_), s16(base_y_), sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        if (enemy_touched(g, sprite_, s16(base_x_ + dx), s16(base_y_ - dy))) {
            g.scheduler().destroy(*this);
            return;
        }
        if (timer_ > 0x20) {
            sprite_.variant = 1;
            state_ = State::moving;
        }
        show(g);
    }

    Sprite sprite_; // +0C
    s16 timer_{};   // +46
    u16 step_{};    // +4E
    s32 across_{};  // +50
    s32 down_{};    // +54
    State state_ = State::moving;
};

// 08088CF0, 0808852C: a rail to hang from (not drawn; drawn in the map):
// a player entering its box (from record +3/+4 x 8, size +5/+6 x 8)
// hangs on (state 22h) and slides along it, speeding up (holding right
// faster, left slower); jumping lets go, upward or downward with up or
// down held (otherwise alternating every 15 frames), and for 20 frames
// the rail cannot be caught again.
class HangRail final : public Interactive {
public:
    explicit HangRail(const ObjectSpawn& o) : Interactive(o) {}

    void update(Game& g) override {
        StageState& s = g.stage();
        const u8* r = spawn_.record;
        const s32 left = x_ + s8(r[3]) * 8;
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            const u8 bit = u8(1 << n);
            if (p.flags & player_flag::dead) {
                if (hanging_ & bit) {
                    p.flags &= ~player_flag::no_movement;
                    hanging_ &= u8(~bit);
                }
                continue;
            }
            if (!(hanging_ & bit)) {
                if (cooldown_[n] != 0) {
                    --cooldown_[n];
                    continue;
                }
                const s32 px = p.x >> 8, py = p.y >> 8, top = y_ + s8(r[4]) * 8;
                if (left <= px && px <= left + r[5] * 8 && top <= py && py <= top + r[6] * 8) {
                    catch_player(g, p, n);
                }
                continue;
            }
            if (!slide(g, s, p, n, left)) {
                return;
            }
        }
        if ((hanging_ & 3) == 0 && !near_view(s16(x_ - s.camera.x), s16(y_ - s.camera.y))) {
            give_back();
            g.scheduler().destroy(*this);
        }
    }

private:
    void catch_player(Game& g, Player& p, unsigned n) {
        speed_[n] = std::max(p.ground_speed >> 1, 0);
        distance_[n] = x_ * 0x100 < p.x ? p.x - x_ * 0x100 : x_ * 0x100 - p.x;
        end_player_moves(g, p);
        p.flags =
            (p.flags & ~(player_flag::jumped | player_flag::in_air)) | player_flag::no_movement;
        hanging_ |= u8(1 << n);
        p.state = 0x22;
        set_player_radius(g, p, 6, 0xE);
        p.radius_x = 6;
        p.radius_y = 0xE;
        timer_[n] = 0;
        p.power |= 0x80;
    }

    // False when the update ends here (the original returns at once).
    bool slide(Game& g, const StageState& s, Player& p, unsigned n, s32 left) {
        s32& speed = speed_[n];
        if (speed < 0xE8) {
            speed += 0x10;
        }
        if (speed > 0x900) {
            speed = 0x900;
        }
        distance_[n] += speed;
        p.y = y_ * 0x100 + 0x100;
        const s32 px = p.x >> 8;
        if (left <= px && px <= left + spawn_.record[5] * 8) {
            p.x += speed;
            if (p.held & 0x10) {
                speed += 0x10;
            } else if ((p.held & 0x20) && speed > 0xE8) {
                speed -= 0x10;
            }
            const s32 d = probe_short(g.assets(), s.map, (p.x >> 8) - 8, p.y >> 8, p.layer, -8,
                                      nullptr, Sensor::wall);
            if (d < 0) {
                p.x -= d * 0x100;
            }
        }
        p.speed_y = 0;
        if (p.pressed & s.jump_buttons) {
            if (speed < 0) {
                p.x = (left + 2) * 0x100;
                speed = 0;
                return false;
            }
            let_go(g, s, p, n);
        }
        timer_[n] = remainder(timer_[n] + 1, 0x1E);
        return true;
    }

    void let_go(Game& g, const StageState& s, Player& p, unsigned n) {
        end_player_moves(g, p);
        p.flags &= ~player_flag::frozen;
        p.held = u16(p.held | s.jump_buttons | s.other_buttons);
        p.flags = (p.flags & 0xFFBFFEEF) | player_flag::in_air | player_flag::rolling;
        p.power &= 0x7F;
        if (p.held & 0xC0) {
            p.speed_y = (p.held & 0x40) ? -0x6C0 : 0x6C0;
        } else {
            p.speed_y = divide(timer_[n], 0xF) == 0 ? -0x6C0 : 0x6C0;
        }
        p.state = 5;
        set_player_radius(g, p, 6, 9);
        p.radius_x = 6;
        p.radius_y = 9;
        p.y = y_ * 0x100 + 0x900;
        p.speed_x = 0;
        p.ground_speed = 0;
        hanging_ &= u8(~(1 << n));
        timer_[n] = 0;
        cooldown_[n] = 0x14;
        Sprite& body = p.body->sprite;
        body.flags = (body.flags & ~sprite_flag::priority) | 0x2000;
    }

    u8 hanging_{};                  // +3C bit per player
    std::array<u8, 2> cooldown_{};  // +3D
    std::array<s32, 2> speed_{};    // +40
    std::array<s32, 2> distance_{}; // +48
    std::array<s32, 2> timer_{};    // +50
};

// The tube mouths' and exits' pictures: tile count, animation, variant
// (by the opening: 0-1 up or down, 2-3 across). Zone 6 has its own, one
// look in each act (act 17 shares act 11's).
struct TubePicture {
    u32 tiles;
    u16 animation;
    u8 variant;
    bool upside_down_first = false; // opening 0 (not 1) is shown flipped
};

TubePicture tube_picture(Game& g, u8 opening, bool exit) {
    const s8 act = g.stage().zone;
    const bool upright = opening < 2;
    if ((act >> 1) == 5 || act == 0x11) {
        if (act & 1) {
            // (The mouths' picture here is drawn the other way up.)
            return upright ? TubePicture{0x4D, 0x24F, u8(exit), !exit}
                           : TubePicture{0x5B, 0x24E, u8(exit)};
        }
        // (Act 10's exits show the first variant too.)
        return upright ? TubePicture{0x46, u16(exit ? 0x249 : 0x248), 0}
                       : TubePicture{0x46, u16(exit ? 0x24B : 0x24A), 0};
    }
    return TubePicture{0x46, u16(upright ? 0x24D : 0x24C), u8(exit)};
}

// 08095414: a tube mouth (record +5: 0 open at the bottom, 1 at the
// top, 2 to the right, 3 to the left; +6 its stage), one process per
// player (the first draws it). A player meeting the open side is pulled in
// (080956B4), drawn to its middle over 90 frames (08095A30), then carried
// by (+3, +4) x 8 pixels in 20 steps into the tubes (08095B98).
class TubeMouth final : public Interactive {
public:
    TubeMouth(Game& g, const ObjectSpawn& o, u8 index)
        : Interactive(o, u16(0x2000 | o.record[6])), game_(g), index_(index), stage_(o.record[6]),
          opening_(o.record[5]), dx_(s8(o.record[3]) * 8), dy_(s8(o.record[4]) * 8) {
        const TubePicture look = tube_picture(g, opening_, false);
        const u32 tiles = index == 0 ? g.sprite_tiles.allocate(look.tiles) : 0;
        u32 flags = 0;
        if (opening_ < 2) {
            if ((opening_ != 0) != look.upside_down_first) {
                flags |= sprite_flag::flip_y;
            }
        } else if (opening_ & 1) {
            flags |= sprite_flag::flip_x;
        }
        if (index != 0) {
            flags |= sprite_flag::keep_colors | sprite_flag::keep_tiles;
        }
        sprite_.start(tiles, look.animation, look.variant, x_, y_, 0x140, flags);
        animate(g, sprite_);
    }

    // 08095E0C
    void on_destroy() override {
        if (index_ == 0) {
            game_.sprite_tiles.free(sprite_.tiles.to);
        }
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        Player& p = s.player[index_];
        sprite_.x = s16(x_ - s.camera.x);
        sprite_.y = s16(y_ - s.camera.y);
        switch (state_) {
        case State::open:
            if (!open(g, s, p)) {
                return;
            }
            break;
        case State::pulling:
            pull(g, p);
            break;
        case State::carrying:
            if (!carry(g, s, p)) {
                return;
            }
            break;
        }
        animate(g, sprite_);
        if (index_ == 0) {
            draw(g, sprite_);
        }
    }

private:
    enum class State : u8 { open, pulling, carrying };

    // 080956B4. False when the mouth went away.
    bool open(Game& g, const StageState& s, Player& p) {
        if (p.flags & player_flag::dead) {
            end_player_moves(g, p);
            p.flags |= player_flag::dead;
            p.state = 1;
        } else {
            const s32 px = p.x, py = p.y;
            const u32 r = collide_solid(g, sprite_, x_, y_, p);
            const bool upside_down = s.flags & stage_flag::upside_down;
            bool caught = false;
            u32 keep_air = 0;
            if (opening_ < 2) {
                const bool bottom = (opening_ == 0) != (s.zone == 11);
                const u32 mask =
                    bottom != upside_down ? solid::from_below : u32(0x100000 | solid::on_top);
                caught = r & mask;
            } else if (r & (solid::from_left | solid::from_right)) {
                caught = ((r & solid::from_right) && opening_ == 2) ||
                         ((r & solid::from_left) && opening_ == 3);
                keep_air = player_flag::in_air;
            }
            if (caught) {
                end_player_moves(g, p);
                timer_ = 0x5A;
                p.flags = (p.flags & (keep_air ? 0xFFFFFEC7u : 0xFFFFFEC5u)) | keep_air |
                          player_flag::rolling | player_flag::no_movement;
                p.x = px;
                p.y = py;
                p.state = 5;
                p.speed_y = p.speed_x = p.ground_speed = 0;
                p.tube_stage = 0;
                roll_up(g, p);
                state_ = State::pulling;
                p.power |= 0x80;
            }
        }
        if (near_view(sprite_.x, sprite_.y)) {
            return true;
        }
        if (carrying_) {
            p.flags &= ~player_flag::no_movement;
        }
        leave(g);
        return false;
    }

    // 08095A30
    void pull(Game& g, Player& p) {
        if (p.flags & player_flag::dead) {
            p.flags &= ~player_flag::no_movement;
            return;
        }
        if (timer_ == 0) {
            start_x_ = x_;
            start_y_ = y_;
            carrying_ = true;
            p.flags &= ~player_flag::on_object;
            state_ = State::carrying;
        } else {
            const auto approach = [](s32& v, s32 to) {
                if (v < to) {
                    v = std::min(v + 0x180, to);
                } else if (v > to) {
                    v = std::max(v - 0x180, to);
                }
            };
            approach(p.y, y_ * 0x100);
            approach(p.x, x_ * 0x100);
            if (--timer_ == 0x2D) {
                g.sound().start(0x6D);
            }
        }
        end_player_moves(g, p);
        p.state = 5;
    }

    // 08095B98. False when the mouth went away.
    bool carry(Game& g, StageState& s, Player& p) {
        if (!carrying_) {
            state_ = State::open;
            if (!near_view(sprite_.x, sprite_.y)) {
                leave(g);
                return false;
            }
            return true;
        }
        if (p.flags & player_flag::dead) {
            carrying_ = false;
            p.flags &= ~player_flag::no_movement;
            return true;
        }
        if (p.tube_stage != s32(stage_)) {
            carrying_ = false;
            return true;
        }
        p.x = (start_x_ + divide(timer_ * dx_, 0x14)) * 0x100;
        p.y = (start_y_ + divide(timer_ * dy_, 0x14)) * 0x100;
        p.speed_y = s16(p.y - last_y_);
        p.ground_speed = s16(p.x - last_x_);
        p.speed_x = p.ground_speed;
        ++timer_;
        if ((p.x >> 8) == start_x_ + dx_ && (p.y >> 8) == start_y_ + dy_) {
            if (p.tube_stage == s32(stage_)) {
                p.flags &= ~player_flag::no_movement;
                s.player[0].power &= 0x7F;
            }
            carrying_ = false;
            p.power |= 0x80;
        } else {
            end_player_moves(g, p);
            p.flags |= player_flag::no_movement;
            p.state = 5;
            roll_up(g, p);
            carrying_ = true;
        }
        last_x_ = p.x;
        last_y_ = p.y;
        return true;
    }

    void leave(Game& g) {
        if (index_ == 0) {
            give_back();
        }
        g.scheduler().destroy(*this);
    }

    static void roll_up(Game& g, Player& p) {
        set_player_radius(g, p, 6, 0xE);
        p.radius_x = 6;
        p.radius_y = 0xE;
    }

    Game& game_;
    u8 index_;                  // +3C the player
    u8 stage_;                  // +3D
    u8 opening_;                // +59
    s32 dx_, dy_;               // +48, +4C
    Sprite sprite_;             // +0C
    u8 timer_{};                // +3F
    s32 start_x_{}, start_y_{}; // +40, +44
    s32 last_x_{}, last_y_{};   // +50, +54
    bool carrying_{};           // +58
    State state_ = State::open;
};

// 08095E30: a tube's exit (record +5 the side it opens to, as the
// mouths'; +4 x 64 the speed it fires out at; +6 its stage), one process
// per player (the first draws it). Solid to players outside the tubes; a
// player carried through the tubes from a lower stage that touches it is
// caught (080960A8) and fired out (08096274), flying 15 frames on its own
// before control returns (0809642C).
class TubeExit final : public Interactive {
public:
    TubeExit(Game& g, const ObjectSpawn& o, u8 index)
        : Interactive(o, u16(0x2000 | o.record[6])), game_(g), index_(index), stage_(o.record[6]),
          opening_(o.record[5]), strength_(s16(s8(o.record[4]) << 6)) {
        const TubePicture look = tube_picture(g, opening_, true);
        const u32 tiles = index == 0 ? g.sprite_tiles.allocate(look.tiles) : 0;
        u32 flags = 0;
        if (opening_ < 2) {
            if ((opening_ != 0) != look.upside_down_first) {
                flags |= sprite_flag::flip_y;
            }
        } else if (opening_ & 1) {
            flags |= sprite_flag::flip_x;
        }
        if (index != 0) {
            flags |= sprite_flag::keep_colors | sprite_flag::keep_tiles;
        }
        sprite_.start(tiles, look.animation, look.variant, x_, y_, 0x140, flags);
        animate(g, sprite_);
    }

    // 08096598
    void on_destroy() override {
        if (index_ == 0) {
            game_.sprite_tiles.free(sprite_.tiles.to);
        }
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        Player& p = s.player[index_];
        sprite_.x = s16(x_ - s.camera.x);
        sprite_.y = s16(y_ - s.camera.y);
        switch (state_) {
        case State::waiting:
            wait(g, p);
            if (!near_view(sprite_.x, sprite_.y)) {
                leave(g);
                return;
            }
            break;
        case State::aiming:
            aim(g, p);
            break;
        case State::firing:
            if (timer_ == 0) {
                state_ = State::waiting;
                if (!near_view(sprite_.x, sprite_.y)) {
                    leave(g);
                    return;
                }
            } else if (!(p.flags & player_flag::dead)) {
                --timer_;
                p.x += p.speed_x;
                p.y += p.speed_y;
                if (timer_ == 0) {
                    p.flags &= ~player_flag::frozen;
                    p.held = u16(p.held | s.jump_buttons | s.other_buttons);
                    p.tube_stage = 0x7F;
                    p.flags &= ~player_flag::no_movement;
                    p.power &= 0x7F;
                }
            }
            break;
        }
        animate(g, sprite_);
        if (index_ == 0) {
            draw(g, sprite_);
        }
    }

private:
    enum class State : u8 { waiting, aiming, firing };

    // 080960A8
    void wait(Game& g, Player& p) {
        if (p.flags & player_flag::dead) {
            end_player_moves(g, p);
            p.flags |= player_flag::dead;
            p.state = 1;
            return;
        }
        if (!(p.flags & player_flag::no_movement)) {
            collide_solid(g, sprite_, x_, y_, p);
            return;
        }
        if (p.tube_stage >= s32(stage_) || !touches_player(sprite_, x_, y_, p)) {
            return;
        }
        timer_ = 0;
        end_player_moves(g, p);
        p.flags = (p.flags & 0xFFFFFEC5u) | player_flag::rolling | player_flag::no_movement;
        p.state = 5;
        p.y = y_ << 8;
        p.x = x_ << 8;
        p.speed_y = p.speed_x = p.ground_speed = 0;
        p.held = 0;
        p.pressed = 0;
        p.tube_stage = 0x7F;
        set_player_radius(g, p, 6, 0xE);
        p.radius_x = 6;
        p.radius_y = 0xE;
        state_ = State::aiming;
        p.power |= 0x80;
    }

    // 08096274
    void aim(Game& g, Player& p) {
        if (p.flags & player_flag::dead) {
            return;
        }
        if (timer_ == 0) {
            if (opening_ < 2) {
                p.speed_y = (opening_ & 1) ? s16(-strength_) : strength_;
                p.speed_x = 0;
                p.ground_speed = 0;
            } else {
                const bool left = opening_ & 1;
                if (left) {
                    p.flags |= player_flag::facing_left;
                } else {
                    p.flags &= ~player_flag::facing_left;
                }
                p.speed_y = 0;
                p.speed_x = left ? s16(-strength_) : strength_;
                p.ground_speed = p.speed_x;
            }
            timer_ = 0xF;
            state_ = State::firing;
        } else {
            const auto approach = [](s32& v, s32 to) {
                if (v < to) {
                    v = std::min(v + 0x180, to);
                } else if (v > to) {
                    v = std::max(v - 0x180, to);
                }
            };
            approach(p.y, y_ * 0x100);
            approach(p.x, x_ * 0x100);
            --timer_;
        }
        end_player_moves(g, p);
        p.state = 5;
        p.flags |= player_flag::in_air;
    }

    void leave(Game& g) {
        if (index_ == 0) {
            give_back();
        }
        g.scheduler().destroy(*this);
    }

    Game& game_;
    u8 index_;      // +3C
    u8 stage_;      // +3D
    u8 opening_;    // +3F
    s16 strength_;  // +40
    Sprite sprite_; // +0C
    u8 timer_{};    // +3E
    State state_ = State::waiting;
};

// 08087394, 08087640: a handle swinging on a chain of three links below
// its pivot (record +3 set: half a swing later); a player within 16
// pixels of the handle grabs it (state 26h) and swings with it; jumping
// lets go (upward, steered by left or right). For 20 frames after, the
// handle cannot be grabbed again. The handle is drawn twice, the second
// mirrored.
class SwingingHandle final : public Interactive {
public:
    SwingingHandle(Game& g, const ObjectSpawn& o)
        : Interactive(o), game_(g), phase_(o.record[3] == 0 ? 0 : 0x100) {
        StageState& s = g.stage();
        const s16 sx = s16(x_ - s.camera.x), sy = s16(y_ - s.camera.y);
        handle_.start(g.sprite_tiles.allocate(0x10), 0x1F8, 1, sx, sy, 0x480, handle_flags(s));
        top_.start(g.sprite_tiles.allocate(1), 0x1F7, 2, sx, s16(sy - 8), 0x480, 0x2000);
        link_.start(g.sprite_tiles.allocate(1), 0x1F7, 1, sx, sy, 0x480, 0x2000);
        animate(g, top_);
        animate(g, link_);
    }

    // 08088070
    void on_destroy() override {
        game_.sprite_tiles.free(handle_.tiles.to);
        game_.sprite_tiles.free(link_.tiles.to);
        game_.sprite_tiles.free(top_.tiles.to);
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        const Assets& a = g.assets();
        // The swing: an eased angle and a sideways lean from the clock.
        u32 t = u32(remainder(divide(s32(s.ticks << 3), 3) + phase_, 0x200));
        s8 lean;
        if (t < 0x101) {
            lean = s8(-s8(u16(sine(a, t * 2)) >> 8));
        } else {
            t = 0x200 - t;
            lean = s8(u16(sine(a, t * 2)) >> 8);
        }
        u32 angle = u32(s32(sine(a, ((t - 0x7E) & 0x1FF) * 2)) >> 7) & 0x1FF;
        angle = angle < 0x100 ? angle >> 1 : 0x200 - ((0x200 - angle) >> 1);
        const s32 sin = sine(a, (angle << 1) & 0x3FF),
                  cos = sine(a, ((angle << 1) & 0x3FF) + 0x100);
        const s16 sx = s16(x_ - s.camera.x), sy = s16(y_ - s.camera.y);
        handle_.x = sx;
        handle_.y = sy;
        top_.x = sx;
        top_.y = s16(sy - 8);
        const auto& c = s.camera;
        if (!(x_ <= c.x + 0x170 && c.x - 0x80 <= x_ && y_ <= c.y + 0x120 && c.y - 0x80 <= y_) &&
            !near_view(handle_.x, handle_.y)) {
            for (unsigned n = 0; n < s.players; ++n) {
                Player& p = s.player[n];
                if ((p.flags & player_flag::on_object) && p.standing_on == &handle_) {
                    end_player_moves(g, p);
                    p.flags = (p.flags & ~(player_flag::no_movement | player_flag::on_object)) |
                              player_flag::in_air;
                    p.standing_on = nullptr;
                    p.speed_x = p.speed_y = p.ground_speed = 0;
                    drop_pose(g, p);
                }
            }
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        draw(g, top_);
        s32 along = 0, down = 0;
        for (int k = 0; k < 3; ++k) {
            along += divide(sin * 0x5A, 100);
            down += divide((cos + lean) * 0x5A, 100);
            link_.x = s16(sx + (along >> 10));
            link_.y = s16(sy + (down >> 10));
            link_.layer = 0x480;
            draw(g, link_);
        }
        along += divide(sin * 0x6E, 100);
        down += divide((cos + lean) * 0x6E, 100);
        for (unsigned n = 0; n < s.players; ++n) {
            hold(g, s, s.player[n], n, along, down);
        }
        handle_.flags = handle_flags(s);
        if (handle_.frame == 0xFFFFFFFF) {
            // The first update, before the handle's first animation step:
            // the original's creator leaves the frame pointer as the heap
            // held it, so the transform runs and sets the parameters.
            transform_matrix(g, handle_.flags & sprite_flag::rotation_set,
                             u16(~(angle << 1) & 0x3FF), 0x100, 0x100);
        }
        transform(g, handle_, u16(~(angle << 1) & 0x3FF), 0x100, 0x100, s16(sx + (along >> 10)),
                  s16(sy + (down >> 10)));
        animate(g, handle_);
        draw(g, handle_);
        handle_.flags |= sprite_flag::flip_x;
        draw(g, handle_);
        handle_.flags &= ~sprite_flag::flip_x;
    }

private:
    // The handle's flags: its own rotation set, and the next free set
    // number shifted into the upper bits (as the original does).
    u32 handle_flags(StageState& s) const {
        return (u32(s.next_rotation_set++) << 12) | (phase_ == 0 ? 0x7Fu : 0x7Eu);
    }

    static void drop_pose(Game& g, Player& p) {
        if (p.character == 3) {
            p.flags &= ~player_flag::rolling;
            p.state = 0x55;
            set_player_radius(g, p, 6, 0xE);
            p.radius_x = 6;
            p.radius_y = 0xE;
        } else {
            p.flags |= player_flag::rolling;
            p.state = 5;
            set_player_radius(g, p, 6, 9);
            p.radius_x = 6;
            p.radius_y = 9;
        }
    }

    void hold(Game& g, const StageState& s, Player& p, unsigned n, s32 along, s32 down) {
        const u8 bit = u8(1 << n);
        if (p.flags & player_flag::dead) {
            if (holding_ & bit) {
                p.flags &= ~player_flag::no_movement;
                cooldown_[n] = 0x14;
                holding_ &= u8(~bit);
                p.power &= 0x7F;
            }
            return;
        }
        if (!(holding_ & bit)) {
            if (cooldown_[n] != 0) {
                --cooldown_[n];
                return;
            }
            const s32 hx = x_ + divide(along, 0x3B6), hy = y_ + divide(down, 0x3B6);
            const s32 px = p.x >> 8, py = p.y >> 8;
            if (hx - 0x10 <= px && px <= hx + 0x10 && hy - 0x10 <= py && py <= hy + 0x10) {
                holding_ |= bit;
                end_player_moves(g, p);
                p.flags |= player_flag::no_movement | player_flag::on_object;
                p.speed_x = p.speed_y = p.ground_speed = 0;
                p.standing_on = &handle_;
                p.state = 0x26;
                p.power |= 0x80;
            }
            return;
        }
        p.x = (x_ + divide(along, 0x30C) - 4) * 0x100;
        if (p.flags & player_flag::facing_left) {
            p.x += 0xA00;
        }
        p.y = (y_ + divide(down, 0x2A8) + 2) * 0x100;
        p.angle = 0;
        if (!(p.pressed & s.jump_buttons)) {
            p.speed_y = 0;
            return;
        }
        end_player_moves(g, p);
        p.speed_y = -0x500;
        p.flags = (p.flags & ~(player_flag::no_movement | player_flag::on_object)) |
                  player_flag::in_air | player_flag::jumped;
        p.standing_on = nullptr;
        p.power &= 0x7F;
        if (p.held & 0x30) {
            if (p.held & 0x20) {
                p.flags |= player_flag::facing_left;
                p.speed_x = p.ground_speed = -0x200;
            } else {
                p.flags &= ~player_flag::facing_left;
                p.speed_x = p.ground_speed = 0x200;
            }
        } else {
            p.speed_x = p.ground_speed = 0;
        }
        drop_pose(g, p);
        cooldown_[n] = 0x14;
        holding_ &= u8(~bit);
    }

    Game& game_;
    u16 phase_;                    // +B2
    Sprite handle_;                // +0C (transform +9C)
    Sprite link_;                  // +3C
    Sprite top_;                   // +6C
    std::array<u8, 2> cooldown_{}; // +B0
    u8 holding_{};                 // +B4 bit per player
};

// 08089738: a cage (on a two-link hanger, its top piece above) that
// takes a player touching it (080898F8): it holds the player for 4 frames
// (08089AE8), lowers 30h pixels over 30 frames (08089CBC), carries the
// player record +5 x 8 pixels to the right (08089EBC) and lets go; after 30
// frames (0808A09C) it returns (0808A1BC), rises (0808A2F8) and waits again
// (0808A494).
class CageLift final : public Interactive {
public:
    CageLift(Game& g, const ObjectSpawn& o) : Interactive(o), game_(g) {
        cage_.start(g.sprite_tiles.allocate(0x1E), 0x1F2, 0, 0, 0, 0x3C0, 0x2000);
        link_.start(g.sprite_tiles.allocate(3), 0x1F2, 3, 0, 0, 0x440, 0x2000);
        top_.start(g.sprite_tiles.allocate(1), 0x1F7, 0, 0, 0, 0x440, 0x2000);
    }

    // 0808AF38
    void on_destroy() override {
        game_.sprite_tiles.free(cage_.tiles.to);
        game_.sprite_tiles.free(link_.tiles.to);
        game_.sprite_tiles.free(top_.tiles.to);
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        const s16 sx = s16(x_ - s.camera.x), sy = s16(y_ - s.camera.y);
        Player& p = s.player[rider_];
        const bool alive = !(p.flags & player_flag::dead);
        switch (state_) {
        case State::waiting: { // 080898F8
            place(sx, sy, 0, 0x30);
            for (unsigned n = 0; n < s.players; ++n) {
                Player& q = s.player[n];
                if (touches_player(cage_, x_, s16(y_ + 0x30), q)) {
                    timer_ = 0;
                    rider_ = q.index;
                    cage_.variant = 1;
                    q.flags |= player_flag::no_movement;
                    s.player[0].power |= 0x80;
                    state_ = State::holding;
                    g.sound().start(0xB5);
                    break;
                }
            }
            if (gone(g, s)) {
                return;
            }
            show_hanging(g, sy, 0x18);
            return;
        }
        case State::holding: // 08089AE8
            place(sx, sy, 0, 0x30);
            if (alive) {
                p.x = x_ << 8;
                p.y = (y_ + 0x68) * 0x100;
            }
            if (++timer_ > 3) {
                timer_ = 0;
                stop(g, p, alive, 0x26);
                state_ = State::lowering;
            }
            if (gone(g, s)) {
                return;
            }
            show_hanging(g, sy, 0x18);
            return;
        case State::lowering: { // 08089CBC
            const s16 drop = s16(divide((0x1E - timer_) * 0x30, 0x1E));
            place(sx, sy, 0, drop);
            if (alive) {
                p.x = x_ << 8;
                p.y = (y_ + drop + 0x38) * 0x100;
            }
            if (++timer_ > 0x1E) {
                timer_ = 0;
                stop(g, p, alive, 0x26);
                state_ = State::carrying;
            }
            if (gone(g, s)) {
                return;
            }
            show_hanging(g, sy, s16(drop >> 1));
            return;
        }
        case State::carrying: { // 08089EBC
            const s16 dx = s16(timer_ << 1);
            place(sx, sy, dx, 0);
            if (alive) {
                p.x = (x_ + dx) * 0x100;
                p.y = (y_ + 0x38) * 0x100;
            }
            if (dx > spawn_.record[5] * 8) {
                if (alive) {
                    p.flags &= ~player_flag::no_movement;
                    stop(g, p, true, 0x12);
                }
                pause_ = 0;
                cage_.variant = 2;
                g.sound().stop(0xB5);
                state_ = State::resting;
                p.power &= 0x7F;
            } else {
                ++timer_;
            }
            break;
        }
        case State::resting: // 0808A09C
            place(sx, sy, s16(timer_ << 1), 0);
            if (gone(g, s)) {
                return;
            }
            if (++pause_ > 0x1E) {
                state_ = State::returning;
            }
            show_cage(g);
            return;
        case State::returning: // 0808A1BC
            place(sx, sy, s16(timer_ << 1), 0);
            if (timer_ == 0) {
                state_ = State::rising;
            } else {
                --timer_;
            }
            break;
        case State::rising: { // 0808A2F8
            const s16 drop = s16(divide((0x1E - timer_) * 0x30, 0x1E));
            place(sx, sy, 0, s16(0x30 - drop));
            if (++timer_ > 0x1E) {
                timer_ = 0;
                state_ = State::settling;
            }
            if (gone(g, s)) {
                return;
            }
            show_hanging(g, sy, s16(s16(0x30 - drop) >> 1));
            return;
        }
        case State::settling: // 0808A494
            place(sx, sy, 0, 0x30);
            if (++timer_ > 3) {
                timer_ = 0;
                rider_ = 0;
                state_ = State::waiting;
            }
            if (gone(g, s)) {
                return;
            }
            show_hanging(g, sy, 0x18);
            return;
        }
        if (gone(g, s)) {
            return;
        }
        show_cage(g);
    }

private:
    enum class State : u8 {
        waiting,
        holding,
        lowering,
        carrying,
        resting,
        returning,
        rising,
        settling
    };

    // The cage `dx` along and `dy` below its place; the hanger and top at
    // the place (the cage's top piece follows it along).
    void place(s16 sx, s16 sy, s16 dx, s16 dy) {
        cage_.x = s16(sx + dx);
        cage_.y = s16(sy + dy);
        link_.x = sx;
        link_.y = sy;
        top_.x = s16(sx + dx);
        top_.y = sy;
    }

    void stop(Game& g, Player& p, bool alive, u8 state) {
        if (!alive) {
            return;
        }
        p.speed_x = p.ground_speed = p.speed_y = 0;
        end_player_moves(g, p);
        p.state = state;
    }

    bool gone(Game& g, const StageState& s) {
        if (!out_of_range(s, x_, y_, cage_.x, cage_.y)) {
            return false;
        }
        give_back();
        g.scheduler().destroy(*this);
        return true;
    }

    // The cage, the hanger's two links (the second `second_link` lower)
    // and the top piece.
    void show_hanging(Game& g, s16 sy, s16 second_link) {
        animate(g, cage_);
        draw(g, cage_);
        animate(g, link_);
        draw(g, link_);
        link_.y = s16(sy + second_link);
        animate(g, link_);
        draw(g, link_);
        animate(g, top_);
        draw(g, top_);
    }

    void show_cage(Game& g) {
        animate(g, cage_);
        draw(g, cage_);
        animate(g, top_);
        draw(g, top_);
    }

    Game& game_;
    Sprite cage_; // +0C
    Sprite link_; // +3C
    Sprite top_;  // +6C
    u16 timer_{}; // +9C
    u16 pause_{}; // +9E
    u8 rider_{};  // +A0
    State state_ = State::waiting;
};

// 08088D70: a launcher pad: a platform resting on a base, both solid
// (08088ED0). Every 160 frames of play the platform shoots up and bounces
// back three times, each bounce lower, carrying who stands on it
// (0808920C); meanwhile the base's second hitbox hurts (frames 32h-60h of
// its 66h-frame timer).
class LauncherPad final : public Interactive {
public:
    LauncherPad(Game& g, const ObjectSpawn& o) : Interactive(o), game_(g) {
        base_.start(g.sprite_tiles.allocate(0x18), 0x1F5, 0, 0, 0, 0x480, 0x2000);
        pad_.start(g.sprite_tiles.allocate(8), 0x1F5, 2, 0, 0, 0x440, 0x2000);
    }

    // 0808971C
    void on_destroy() override {
        game_.sprite_tiles.free(base_.tiles.to);
        game_.sprite_tiles.free(pad_.tiles.to);
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        if (launched_) {
            fly(g, s);
            return;
        }
        // 08088ED0
        const s16 sx = s16(x_ - s.camera.x), sy = s16(y_ - s.camera.y);
        base_.x = pad_.x = sx;
        base_.y = pad_.y = sy;
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            const u32 r = collide_solid(g, pad_, x_, y_, p);
            if (r & solid::on_top) {
                p.y += 0x100;
                continue;
            }
            if (p.standing_on == &base_ && (p.flags & player_flag::on_object)) {
                if (p.pressed & s.jump_buttons) {
                    p.flags &= ~player_flag::on_object;
                    p.standing_on = nullptr;
                }
                keep_out_of_ceiling(g, s, p);
            } else if (collide_solid(g, base_, x_, y_, p) != 0) {
                keep_out_of_ceiling(g, s, p);
            }
        }
        if (out_of_range(s, x_, y_, base_.x, base_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        if (remainder(s32(s.ticks), 0xA0) == 0) {
            timer_ = 0x66;
            riders_ = 0;
            speed_ = -0x500;
            offset_ = 0;
            bounces_ = 3;
            base_.animation = 0x237;
            base_.variant = 0;
            base_.previous_variant = 0xFF;
            g.sound().start(0xB6);
            launched_ = true;
        }
        animate(g, base_);
        draw(g, base_);
        animate(g, pad_);
        draw(g, pad_);
    }

private:
    // A player pressed up into a ceiling is pushed back down.
    static void keep_out_of_ceiling(Game& g, const StageState& s, Player& p) {
        const s32 d = probe(g.assets(), s.map, (p.y >> 8) - 0x13, p.x >> 8, p.layer, -8, nullptr,
                            Sensor::floor);
        if (d < 0) {
            p.y += d * 0x100;
        }
    }

    // 0808920C
    void fly(Game& g, StageState& s) {
        speed_ += 0x25;
        offset_ += speed_;
        const s16 sx = s16(x_ - s.camera.x), sy = s16(y_ - s.camera.y);
        base_.x = sx;
        base_.y = sy;
        pad_.x = sx;
        pad_.y = s16(sy + s16(offset_ >> 8));
        const s16 pad_y = s16(y_ + (offset_ >> 8));
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            const u8 bit = u8(1 << n);
            if (riders_ & bit) {
                p.y += speed_;
                if (p.flags & player_flag::in_air) {
                    riders_ &= u8(~bit);
                }
                if (!touches_player(pad_, x_, pad_y, p)) {
                    riders_ &= u8(~bit);
                }
            }
            if (collide_platform(g, pad_, x_, pad_y, p) != 0) {
                riders_ |= bit;
                p.y += 0x100;
            }
            collide_solid(g, base_, x_, y_, p);
        }
        if (offset_ >= 0) {
            if (--bounces_ == 0) {
                base_.previous_variant = 0xFF;
                base_.animation = 0x1F5;
                base_.variant = 0;
                launched_ = false;
            } else {
                speed_ = -divide(speed_ << 1, 5);
                riders_ = 0;
                offset_ = 0;
            }
        }
        const s32 before = timer_--;
        bool animated = false;
        if (timer_ == 0x30) {
            base_.previous_variant = 0xFF;
            base_.animation = 0x237;
            base_.variant = 1;
        } else if (u32(before - 0x32) <= 0x2E) {
            const Hitbox& h = base_.hitboxes[1];
            for (unsigned n = 0; n < s.players; ++n) {
                Player& p = s.player[n];
                const Hitbox& body = p.body->sprite.hitboxes[0];
                if (body.index != 0xFFFFFFFF &&
                    hitboxes_meet(x_, y_, h, p.x >> 8, p.y >> 8, body) &&
                    !(p.flags & player_flag::dead)) {
                    hurt_player(g, p);
                }
            }
        } else {
            if (timer_ < 0x30) {
                if (animate(g, base_) == 0) {
                    base_.previous_variant = 0xFF;
                    base_.animation = 0x1F5;
                    base_.variant = 0;
                }
                animate(g, base_);
            }
            animated = true;
        }
        if (!animated) {
            animate(g, base_);
        }
        draw(g, base_);
        animate(g, pad_);
        draw(g, pad_);
    }

    Game& game_;
    Sprite base_;     // +0C
    Sprite pad_;      // +44
    u8 riders_{};     // +74 bit per player
    s32 speed_{};     // +78 8.8
    s32 offset_{};    // +7C 8.8
    s32 bounces_{};   // +80
    s32 timer_{};     // +84
    bool launched_{}; // the update is 0808920C (else 08088ED0)
};

} // namespace

bool create_secret_base_object(Game& g, u32 creator, const ObjectSpawn& o) {
    switch (creator) {
    case 0x080862CC:
        g.scheduler().create<Pulley>(g, o);
        return true;
    case 0x08093F3C:
        g.scheduler().create<TubeWaypoint>(o);
        return true;
    case 0x08093E9C:
        g.scheduler().create<HazardBox>(o);
        return true;
    case 0x080920F0:
        g.scheduler().create<RollPoint>(o);
        return true;
    case 0x0808425C:
        g.scheduler().create<HidingBox>(o);
        return true;
    case 0x08082588:
        g.scheduler().create<Chute>(o);
        return true;
    case 0x08088094:
        g.scheduler().create<DroppingGate>(g, o);
        return true;
    case 0x08088CF0:
        g.scheduler().create<HangRail>(o);
        return true;
    case 0x08087394:
        g.scheduler().create<SwingingHandle>(g, o);
        return true;
    case 0x08089738:
        g.scheduler().create<CageLift>(g, o);
        return true;
    case 0x08088D70:
        g.scheduler().create<LauncherPad>(g, o);
        return true;
    case 0x08095E30: {
        const u8 x_byte = o.record[0];
        for (u8 n = 0; n < g.stage().players; ++n) {
            o.record[0] = x_byte;
            g.scheduler().create<TubeExit>(g, o, n);
        }
        return true;
    }
    case 0x08095414: {
        // Each takes the record's x (it is marked taken after the last).
        const u8 x_byte = o.record[0];
        for (u8 n = 0; n < g.stage().players; ++n) {
            o.record[0] = x_byte;
            g.scheduler().create<TubeMouth>(g, o, n);
        }
        return true;
    }
    case 0x08084038:
        if ((g.stage().zone >> 1) == 3) {
            g.scheduler().create<DraggingGround>(o);
        } else {
            g.scheduler().create<Conveyor>(o);
        }
        return true;
    default:
        return false;
    }
}

} // namespace sonic_advance_gba

namespace sonic_advance_gba {

bool create_secret_base_enemy(Game& g, u32 creator, const ObjectSpawn& o) {
    switch (creator) {
    case 0x080706A8:
        if ((g.stage().zone >> 1) != 1 || g.save().unknown_5178 == 0) {
            g.scheduler().create<Striker>(g, o);
        }
        return true;
    case 0x080710F0:
        g.scheduler().create<Launcher>(o);
        return true;
    case 0x08071148:
        g.scheduler().create<Popper>(g, o);
        return true;
    case 0x0807406C:
        g.scheduler().create<Hoverer>(g, o);
        return true;
    default:
        return false;
    }
}

} // namespace sonic_advance_gba
