#include "sonic_advance_gba/game/enemies.hpp"
#include "sonic_advance_gba/game/angel_island.hpp"
#include "sonic_advance_gba/game/boss.hpp"
#include "sonic_advance_gba/game/casino_paradise.hpp"
#include "sonic_advance_gba/game/collision.hpp"
#include "sonic_advance_gba/game/egg_rocket.hpp"
#include "sonic_advance_gba/game/game.hpp"
#include "sonic_advance_gba/game/ice_paradise.hpp"
#include "sonic_advance_gba/game/math.hpp"
#include "sonic_advance_gba/game/moon_zone.hpp"
#include "sonic_advance_gba/game/object_base.hpp"
#include "sonic_advance_gba/game/object_util.hpp"
#include "sonic_advance_gba/game/secret_base.hpp"
#include "sonic_advance_gba/game/stage.hpp"
#include "sonic_advance_gba/game/x_zone.hpp"

namespace sonic_advance_gba {

namespace {

// 0806BC80: walks to and fro within its range (record +3: start, +5:
// width, in 8 pixels), keeping to the ground; at each end it stops, turns
// and fires two shots in an arc.
class ShootingWalker final : public Enemy {
public:
    ShootingWalker(Game& g, const ObjectSpawn& o)
        : Enemy(g, o, 0x1E), layer_(o.record[6] != 0 ? 1 : 0) {
        sprite_.start(tiles_, 0x194, 0, s16(base_x_), s16(base_y_), 0x480, 0x2000);
        animate(g, sprite_);
        if (base_x_ <= (g.stage().player[0].x >> 8)) {
            step_ = 0xA0;
            facing_right_ = true;
        }
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        if (turning_) {
            turn(g, s);
            return;
        }
        // 0806BDC8
        offset_x_ += step_;
        const s32 x = base_x_ + divide(offset_x_, 0x100);
        const s32 y = base_y_ - 6 + offset_y_;
        sprite_.x = s16(x - s.camera.x);
        sprite_.y = s16(y - s.camera.y);
        if (out_of_range(s, base_x_, base_y_, sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        if (enemy_touched(g, sprite_, s16(x), s16(y))) {
            g.scheduler().destroy(*this);
            return;
        }
        keep_to_ground(g, x, y);
        const s32 at = divide(offset_x_, 0x100);
        bool turn_now = false;
        if (at > param(3) * 8) {
            if (at >= (param(3) + spawn_.record[5]) * 8 && facing_right_) {
                step_ = -0xA0;
                facing_right_ = false;
                turn_now = true;
            }
        } else if (!facing_right_) {
            step_ = 0xA0;
            facing_right_ = true;
            turn_now = true;
        }
        if (turn_now) {
            timer_ = 0;
            sprite_.variant = 1;
            turning_ = true;
        }
        animate(g, sprite_);
        draw(g, sprite_);
    }

private:
    // 0806BFAC: standing, turning; the shots on the 15th frame.
    void turn(Game& g, StageState& s) {
        const s32 x = base_x_ + divide(offset_x_, 0x100);
        const s32 y = base_y_ - 6 + offset_y_;
        sprite_.x = s16(x - s.camera.x);
        sprite_.y = s16(y - s.camera.y);
        if (++timer_ == 0xF) {
            fire(g, s16(x), s16(y - 0x14));
        }
        if (out_of_range(s, base_x_, base_y_, sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        if (enemy_touched(g, sprite_, s16(x), s16(y))) {
            g.scheduler().destroy(*this);
            return;
        }
        keep_to_ground(g, x, y);
        if (timer_ > 0x12) {
            sprite_.variant = 0;
            turning_ = false;
        }
        animate(g, sprite_);
        draw(g, sprite_);
    }

    void keep_to_ground(Game& g, s32 x, s32 y) {
        offset_y_ = s16(offset_y_ + probe_short(g.assets(), g.stage().map, y, x, layer_, 8, nullptr,
                                                Sensor::floor));
    }

    // 0806C144
    void fire(Game& g, s16 x, s16 y);

    Sprite sprite_;       // +0C
    s32 step_ = -0xA0;    // +3C 8.8 pixels a frame
    s32 offset_x_{};      // +40 8.8
    s16 offset_y_{};      // +44
    s16 timer_{};         // +46
    bool facing_right_{}; // +48 bit 10
    u8 layer_;            // +4C
    bool turning_{};      // the update routine (0806BDC8 or 0806BFAC)
};

// 0806C2B0: walks to and fro within its range (record +3: start, +5:
// width, in 8 pixels) on the ground; when the player is ahead (up to 50h
// pixels, from 20h above to 8 below) it winds up (0806C8D8) and charges at
// 300h until the end of its range, where it slows and turns (0806C6A8).
class Charger final : public Enemy {
public:
    Charger(Game& g, const ObjectSpawn& o) : Enemy(g, o, 0x14) {
        sprite_.start(tiles_, 0x195, 0, s16(base_x_), s16(base_y_), 0x480, 0x2000);
        animate(g, sprite_);
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        const bool facing_right = sprite_.flags & sprite_flag::flip_x;
        if (state_ == State::walking) {
            offset_x_ += speed_;
            if (slowing_ != 0) {
                --slowing_;
                offset_x_ += facing_right ? slowing_ * 0xD : -slowing_ * 0xD;
            }
        }
        const s16 x =
            s16(base_x_ + (state_ == State::walking ? divide(offset_x_, 0x100) : offset_x_ >> 8));
        const s16 y = s16(base_y_ + offset_y_);
        sprite_.x = s16(x - s.camera.x);
        sprite_.y = s16(y - s.camera.y);
        if (out_of_range(s, s16(base_x_), s16(base_y_), sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        if (enemy_touched(g, sprite_, x, y)) {
            g.scheduler().destroy(*this);
            return;
        }
        offset_y_ =
            s16(offset_y_ + probe_short(g.assets(), s.map, y, x, 1, 8, nullptr, Sensor::floor));
        switch (state_) {
        case State::walking:
            walk(s, x, y, facing_right);
            break;
        case State::turning:
            turn(g, facing_right);
            break;
        case State::winding_up:
            if (++timer_ == 0x19) {
                sprite_.variant = 0;
                state_ = State::walking;
            }
            break;
        }
        animate(g, sprite_);
        draw(g, sprite_);
    }

private:
    // 0806C3D8
    void walk(const StageState& s, s16 x, s16 y, bool facing_right) {
        const s32 at = offset_x_ >> 8;
        if ((param(3) + 1) * 8 < at) {
            if (at < (param(3) + spawn_.record[5] - 1) * 8) {
                const Player& p = s.player[0];
                const s32 py = p.y >> 8, px = p.x >> 8;
                if (charging_ || py > y + 8 || py < y - 0x20) {
                    return;
                }
                if (!facing_right ? (px > x || px < x - 0x50) : (px < x || px > x + 0x50)) {
                    return;
                }
                charging_ = true;
                timer_ = 0;
                speed_ = facing_right ? 0x300 : -0x300;
                sprite_.variant = 1;
                state_ = State::winding_up;
                return;
            }
            if (!facing_right) {
                return;
            }
            if (charging_) {
                offset_x_ -= 0x300;
            }
            timer_ = 0;
            speed_ = -0xA0;
        } else {
            if (facing_right) {
                return;
            }
            if (charging_) {
                offset_x_ += 0x300;
            }
            timer_ = 0;
            speed_ = 0xA0;
        }
        sprite_.variant = 2;
        state_ = State::turning;
    }

    // 0806C6A8: slowing over 24 frames (a quarter of a sine), then off the
    // other way.
    void turn(Game& g, bool facing_right) {
        const s32 v =
            divide(sine(g.assets(), unsigned(timer_ * 0x15 + 0x100)), charging_ ? 0x20 : 0x40);
        offset_x_ += facing_right ? v : -v;
        if (++timer_ != 0x18) {
            return;
        }
        sprite_.variant = 0;
        offset_x_ += speed_;
        if (!facing_right) {
            speed_ = 0xA0;
            sprite_.flags |= sprite_flag::flip_x;
        } else {
            speed_ = -0xA0;
            sprite_.flags &= ~sprite_flag::flip_x;
        }
        if (charging_) {
            slowing_ = 0;
        }
        charging_ = false;
        state_ = State::walking;
    }

    enum class State { walking, turning, winding_up } state_ = State::walking;
    Sprite sprite_;     // +0C
    s16 offset_y_{};    // +3C
    s32 offset_x_{};    // +40 8.8
    s16 timer_{};       // +44
    s16 speed_ = -0xA0; // +46
    bool charging_{};   // +48
    u8 slowing_{};      // +49
};

// 0806CA34: sits within its range (record +3: start, +5: width, in 8
// pixels; +6: the collision layer), facing the player; when the player is
// ahead within 50h pixels it hops that way (0806CFC0; the waits between
// hops from 086C8C10); now and then it croaks for a second (0806CE2C).
class Hopper final : public Enemy {
public:
    Hopper(Game& g, const ObjectSpawn& o) : Enemy(g, o, 0x14), layer_(o.record[6]) {
        sprite_.start(tiles_, 0x196, 0, s16(base_x_), s16(base_y_), 0x480, 0x2000);
        animate(g, sprite_);
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        if (state_ == State::hopping) {
            speed_y_ = s16(speed_y_ + 0x40);
            offset_y_ = s16(offset_y_ + divide(speed_y_, 0x100));
            offset_x_ += (sprite_.flags & sprite_flag::flip_x) ? 0x100 : -0x100;
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
        if (state_ == State::sitting) {
            ++timer_;
        }
        if (enemy_touched(g, sprite_, x, y)) {
            g.scheduler().destroy(*this);
            return;
        }
        const s32 d =
            probe_short(g.assets(), s.map, y, x, layer_ != 0 ? 1 : 0, 8, nullptr, Sensor::floor);
        switch (state_) {
        case State::sitting:
            offset_y_ = s16(offset_y_ + d);
            face_player(s, x);
            sit(g, s, x);
            draw(g, sprite_);
            return;
        case State::croaking:
            offset_y_ = s16(offset_y_ + d);
            if (++cooldown_ > 0x3C) {
                cooldown_ = 0;
                sprite_.variant = 0;
                state_ = State::sitting;
            }
            break;
        case State::hopping:
            if (d < 1) {
                cooldown_ = 5;
                offset_y_ = s16(offset_y_ + d);
                sprite_.variant = 1;
                animate(g, sprite_);
                sprite_.variant = 0;
                state_ = State::sitting;
                face_player(s, x);
            }
            break;
        }
        animate(g, sprite_);
        draw(g, sprite_);
    }

private:
    void face_player(const StageState& s, s16 x) {
        if (x < (s.player[0].x >> 8)) {
            sprite_.flags |= sprite_flag::flip_x;
        } else {
            sprite_.flags &= ~sprite_flag::flip_x;
        }
    }

    // 0806CB70
    void sit(Game& g, const StageState& s, s16 x) {
        if (timer_ >= 0xFB) {
            cooldown_ = 0;
            timer_ = s16(timer_ - 0xFA);
            sprite_.variant = 1;
            state_ = State::croaking;
            return;
        }
        if (cooldown_ != 0) {
            --cooldown_;
            return;
        }
        const s32 px = s.player[0].x >> 8;
        const s32 at = offset_x_ >> 8;
        if (!(sprite_.flags & sprite_flag::flip_x)) {
            if (at < (param(3) + 8) * 8 || x < px || px < x - 0x50) {
                return;
            }
        } else if ((param(3) + spawn_.record[5] - 8) * 8 < at || px < x || x + 0x50 < px) {
            return;
        }
        speed_y_ = -0x500;
        sprite_.variant = 0;
        state_ = State::hopping;
        timer_ = s16(timer_ + g.assets().u8(0x086C8C10 + hops_));
        hops_ = u8((hops_ + 1) & 7);
    }

    enum class State { sitting, croaking, hopping } state_ = State::sitting;
    Sprite sprite_;  // +0C
    s32 offset_x_{}; // +3C 8.8
    s16 offset_y_{}; // +42
    s16 speed_y_{};  // +44
    u8 cooldown_{};  // +46
    u8 hops_{};      // +47
    s16 timer_{};    // +48
    u16 layer_;      // +4A
};

// 0806C218: a shot flying in an arc (x in quarter pixels), hurting the
// player it touches, until it leaves the view.
class WalkerShot final : public Process {
public:
    WalkerShot(Game& g, s16 x, s16 y, bool right) : Process(0x3000) {
        x4_ = (x + (right ? 0xE : -0xE)) * 4;
        dx4_ = right ? 6 : -6;
        sprite_.start(0x12620, 0x19A, 0, s16(x + (right ? 8 : -8)), y, 0x240, 0x2000);
        animate(g, sprite_);
    }

    void update(Game& g) override {
        const StageState& s = g.stage();
        x4_ += dx4_;
        sprite_.x = s16(x4_ >> 2);
        speed_y_ = s16(speed_y_ + 0x28);
        sprite_.y = s16(sprite_.y + s8(u16(speed_y_) >> 8));
        const s16 x = sprite_.x, y = sprite_.y;
        projectile_hits(g, sprite_, x, y);
        sprite_.x = s16(x - s.camera.x);
        sprite_.y = s16(y - s.camera.y);
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
    Sprite sprite_;        // +00
    s32 x4_;               // +30
    s32 dx4_;              // +34
    s16 speed_y_ = -0x200; // +38 8.8
};

void ShootingWalker::fire(Game& g, s16 x, s16 y) {
    g.scheduler().create<WalkerShot>(g, x, y, true);
    g.scheduler().create<WalkerShot>(g, x, y, false);
}

// 0801DC54: flies to and fro within its range (record +3: start, +5:
// width, in 8 pixels); when the player is below it within a cone it stops
// and fires a shot aimed at the player, once per pass.
class AimingFlyer final : public Enemy {
public:
    AimingFlyer(Game& g, const ObjectSpawn& o) : Enemy(g, o, 0x14) {
        sprite_.start(tiles_, 0x193, 0, s16(base_x_), s16(base_y_), 0x480, 0x2000);
        if (base_x_ <= (g.stage().player[0].x >> 8)) {
            step_ = 0x100;
            sprite_.flags |= sprite_flag::flip_x;
        }
        animate(g, sprite_);
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        switch (state_) {
        case State::flying:
            fly(g, s);
            break;
        case State::firing:
            fire(g, s);
            break;
        case State::turning:
            turn(g, s);
            break;
        }
    }

private:
    enum class State { flying, firing, turning } state_ = State::flying;

    s32 x() const { return s16(base_x_) + (offset_x_ >> 8); }

    // The usual start of each state: the screen position, leaving range,
    // being destroyed. True if it goes on.
    bool alive(Game& g, StageState& s, s32 x, s32 y) {
        sprite_.x = s16(x - s.camera.x);
        sprite_.y = s16(y - s.camera.y);
        if (out_of_range(s, x, y, sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return false;
        }
        if (enemy_touched(g, sprite_, s16(x), s16(y))) {
            g.scheduler().destroy(*this);
            return false;
        }
        return true;
    }

    // 0801DDBC
    void fly(Game& g, StageState& s) {
        offset_x_ += step_;
        const s32 x = this->x(), y = s16(base_y_);
        if (!alive(g, s, x, y)) {
            return;
        }
        animate(g, sprite_);
        draw(g, sprite_);
        const s32 at = offset_x_ >> 8;
        if (at > param(3) * 8) {
            if (at < (param(3) + spawn_.record[5]) * 8) {
                const Player& p = s.player[0];
                const s32 py = p.y >> 8;
                if (!fired_ && y <= py && py <= y + 0x60) {
                    // The player within the cone below it.
                    const s32 dx = x - (p.x >> 8);
                    s32 near = s16(quotient(dx * 0x11, 10));
                    s32 far = s16(quotient(dx * 10, 0x11));
                    sprite_.flags &= sprite_flag::flip_x; // (the other bits go too)
                    if (sprite_.flags & sprite_flag::flip_x) {
                        far = s16(-far);
                        near = s16(-near);
                    }
                    const s32 dy = py - y;
                    if (dy <= near && far <= dy) {
                        sprite_.previous_variant = 0xFF;
                        sprite_.variant = 1;
                        timer_ = 0;
                        state_ = State::firing;
                    }
                }
            } else {
                start_turn(-0x100, false);
            }
        } else {
            start_turn(0x100, true);
        }
    }

    void start_turn(s16 step, bool right) {
        step_ = step;
        sprite_.previous_variant = 0xFF;
        sprite_.variant = 2;
        state_ = State::turning;
        if (right) {
            sprite_.flags |= sprite_flag::flip_x;
        } else {
            sprite_.flags &= ~sprite_flag::flip_x;
        }
    }

    // 0801E004: the shot on the tenth frame, then flying on.
    void fire(Game& g, StageState& s) {
        const s32 x = this->x(), y = s16(base_y_);
        sprite_.x = s16(x - s.camera.x);
        sprite_.y = s16(y - s.camera.y);
        if (++timer_ == 10) {
            shoot(g, s16(x), s16(y | 6));
        }
        if (out_of_range(s, x, y, sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        if (enemy_touched(g, sprite_, s16(x), s16(y))) {
            g.scheduler().destroy(*this);
            return;
        }
        if (timer_ > 0x1D) {
            sprite_.previous_variant = 0xFF;
            sprite_.variant = 0;
            state_ = State::flying;
            fired_ = true;
        }
        animate(g, sprite_);
        draw(g, sprite_);
    }

    // 0801E160: turning until its animation ends.
    void turn(Game& g, StageState& s) {
        if (!alive(g, s, x(), s16(base_y_))) {
            return;
        }
        fired_ = false;
        unknown_40_ = 0;
        if (animate(g, sprite_) == 0) {
            sprite_.variant = 0;
            state_ = State::flying;
        }
        draw(g, sprite_);
    }

    // 0801E290
    void shoot(Game& g, s16 x, s16 y);

    Sprite sprite_;     // +0C
    s32 offset_x_{};    // +3C 8.8
    u8 unknown_40_{};   // +40
    bool fired_{};      // +41 fired on this pass
    s16 step_ = -0x100; // +42 8.8 pixels a frame
    s16 timer_{};       // +44
};

// 0801E3AC: a shot flying straight at where the player was.
class AimedShot final : public Process {
public:
    AimedShot(Game& g, s16 x, s16 y) : Process(0x9000) {
        const Assets& a = g.assets();
        const Player& p = g.stage().player[0];
        const s16 angle = octant_angle(a, s16((p.y >> 8) - y), s16((p.x >> 8) - x));
        speed_x_ = s16(divide(sine(a, u32(angle + 0x100)), 0x26));
        speed_y_ = s16(divide(sine(a, u32(angle)), 0x26));
        x8_ = x << 8;
        y8_ = y << 8;
        sprite_.start(0x12600, 0x199, 0, x, y, 0x480, 0x2000);
        animate(g, sprite_);
    }

    void update(Game& g) override {
        const StageState& s = g.stage();
        x8_ += speed_x_;
        y8_ += speed_y_;
        const s16 x = s16(u32(x8_) >> 8), y = s16(u32(y8_) >> 8);
        projectile_hits(g, sprite_, x, y);
        sprite_.x = s16(x - s.camera.x);
        sprite_.y = s16(y - s.camera.y);
        if (u16(sprite_.x + 0x28) < 0x141 && sprite_.y + 0x28 >= 0 && sprite_.y < 0xC9) {
            animate(g, sprite_);
            draw(g, sprite_);
        } else {
            g.scheduler().destroy(*this);
        }
    }

private:
    Sprite sprite_;         // +00
    s32 x8_, y8_;           // +30, +34
    s16 speed_x_, speed_y_; // +38, +3A
};

void AimingFlyer::shoot(Game& g, s16 x, s16 y) {
    g.scheduler().create<AimedShot>(g, x, y);
}

// 0801D3AC / 0801D4CC: rises and sinks a pixel a frame between record
// +4 and +4 + +6 (in 8 pixels below its place), facing the player; every
// second time at the top it attacks (0801D650).
class Bobber final : public Enemy {
public:
    Bobber(Game& g, const ObjectSpawn& o) : Enemy(g, o, 0x14) {
        sprite_.start(tiles_, 0x191, 1, s16(base_x_), s16(base_y_), 0x480, 0x2000);
        animate(g, sprite_);
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        const bool attacking = attacking_;
        if (!attacking) {
            offset_ = s8(offset_ + step_);
        }
        const s16 x = s16(base_x_);
        const s32 y = s16(base_y_) + offset_;
        sprite_.x = s16(x - s.camera.x);
        sprite_.y = s16(y - s.camera.y);
        if (!attacking) {
            if (offset_ > param(4) * 8) {
                if (offset_ >= (param(4) + spawn_.record[6]) * 8) {
                    step_ = -1;
                }
            } else {
                ++turns_;
                step_ = 1;
                if ((turns_ & 1) == 0) {
                    attack_timer_ = 0;
                    sprite_.variant = 2;
                    attacking_ = true;
                }
            }
        }
        if (enemy_touched(g, sprite_, x, s16(y))) {
            g.scheduler().destroy(*this);
            return;
        }
        if (u16(sprite_.x + 0x80) < 0x1F1 && sprite_.y + 0x80 >= 0 && sprite_.y < 0x121) {
            if ((s.player[0].x >> 8) < x) {
                sprite_.flags &= ~sprite_flag::flip_x;
            } else {
                sprite_.flags |= sprite_flag::flip_x;
                sprite_.x = s16(sprite_.x - 8);
            }
            if (attacking && ++attack_timer_ > 0xE) {
                // 0801D650: after a moment, a bomb thrown the way it faces.
                attacking_ = false;
                sprite_.variant = 1;
                const bool right = sprite_.flags & sprite_flag::flip_x;
                g.scheduler().create<BobberBomb>(g, s16(right ? x - 0x12 : x + 8), s16(y - 8));
            }
            animate(g, sprite_);
            draw(g, sprite_);
            return;
        }
        give_back();
        g.scheduler().destroy(*this);
    }

private:
    // 0801D7C8, 0801D8B4, 0801D9C0: thrown towards the player (at most 3Ch
    // 1/40 pixels a frame), it bounces once, then lies for 96 frames; it
    // blows up when it touches the player or lies long enough.
    class BobberBomb final : public Process {
    public:
        BobberBomb(Game& g, s16 x, s16 y) : Process(0x2000), base_x_(x) {
            const s32 px = g.stage().player[0].x >> 8;
            if (px < x) {
                speed_x_ = s16(-divide((x - px) * 400, 800));
                if (speed_x_ <= -0x3D) {
                    speed_x_ = -0x3C;
                }
            } else {
                speed_x_ = s16(divide((px - x) * 400, 800));
                if (speed_x_ >= 0x3D) {
                    speed_x_ = 0x3C;
                }
            }
            sprite_.start(0x12580, 0x198, 0, x, y, 0x480, 0x2000);
            animate(g, sprite_);
        }

        void update(Game& g) override {
            StageState& s = g.stage();
            if (!lying_) {
                speed_y_ = s16(speed_y_ + 0x28);
                offset_x_ = s16(offset_x_ + speed_x_);
                sprite_.x = s16(base_x_ + divide(offset_x_, 0x28));
                sprite_.y = s16(sprite_.y + s8(u16(speed_y_) >> 8));
                if (speed_y_ > 0) {
                    const s32 d = probe_short(g.assets(), s.map, sprite_.y, sprite_.x, 1, 8,
                                              nullptr, Sensor::floor);
                    if (d < 0) {
                        sprite_.y = s16(sprite_.y + d);
                        if (clock_ == 0) {
                            speed_y_ = s16(-(speed_y_ >> 2));
                            clock_ = 1;
                        } else {
                            lying_ = true;
                            clock_ = 0;
                            speed_y_ = 0;
                        }
                    }
                }
            } else if (clock_++ >= 0x60) {
                blow_up(g);
                return;
            }
            const s16 x = sprite_.x, y = sprite_.y;
            if (projectile_hits(g, sprite_, x, y)) {
                blow_up(g);
                return;
            }
            sprite_.x = s16(x - s.camera.x);
            sprite_.y = s16(y - s.camera.y);
            animate(g, sprite_);
            draw(g, sprite_);
            sprite_.x = x;
            sprite_.y = y;
        }

    private:
        void blow_up(Game& g) {
            g.scheduler().create<BombBlast>(g, sprite_.x, sprite_.y);
            g.scheduler().destroy(*this);
        }

        Sprite sprite_;        // +00
        u16 clock_{};          // +30 bounced (flying) / frames lying
        s16 speed_y_ = -0x200; // +32
        s16 speed_x_{};        // +34 in 1/40 pixels
        s16 offset_x_{};       // +36
        s16 base_x_;           // +38
        bool lying_ = false;   // the update is 0801D9C0
    };

    // 0801DA58, 0801DB10: the blast hurts the player while its hitbox
    // touches; gone when its animation ends. Its tiles are freed with it
    // (0801DC40).
    class BombBlast final : public Process {
    public:
        BombBlast(Game& g, s16 x, s16 y) : Process(0x2000), game_(g) {
            sprite_.start(g.sprite_tiles.allocate(0x10), 0x192, 0, x, y, 0x480, 0x2000);
            animate(g, sprite_);
        }

        void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); }

        void update(Game& g) override {
            StageState& s = g.stage();
            Player& p = s.player[0];
            const s16 x = sprite_.x, y = sprite_.y;
            const Hitbox& body = p.body->sprite.hitboxes[0];
            if (sprite_.hitboxes[0].index != 0xFFFFFFFF && body.index != 0xFFFFFFFF &&
                hitboxes_meet(x, y, sprite_.hitboxes[0], p.x >> 8, p.y >> 8, body)) {
                hurt_player(g, p);
            }
            sprite_.x = s16(x - s.camera.x);
            sprite_.y = s16(y - s.camera.y);
            if (animate(g, sprite_) == 0) {
                g.scheduler().destroy(*this);
                return;
            }
            draw(g, sprite_);
            sprite_.x = x;
            sprite_.y = y;
        }

    private:
        Game& game_;
        Sprite sprite_;
    };

    Sprite sprite_;     // +0C
    s8 step_ = 1;       // +3C
    s8 offset_{};       // +3D
    u8 turns_{};        // +3E
    u8 attack_timer_{}; // +3F
    bool attacking_{};  // the update is 0801D650 (else 0801D4CC)
};

} // namespace

bool create_enemy(Game& g, u32 creator, const ObjectSpawn& o) {
    switch (creator) {
    case 0x0806BC80:
        g.scheduler().create<ShootingWalker>(g, o);
        return true;
    case 0x0806C2B0:
        g.scheduler().create<Charger>(g, o);
        return true;
    case 0x0806CA34:
        g.scheduler().create<Hopper>(g, o);
        return true;
    case 0x0801D3AC:
        g.scheduler().create<Bobber>(g, o);
        return true;
    case 0x0801DC54:
        // Not made in the first act while the save's byte +18 is set.
        if ((g.stage().zone >> 1) != 0 || g.save().unknown_5178 == 0) {
            g.scheduler().create<AimingFlyer>(g, o);
        }
        return true;
    default:
        return create_boss(g, creator, o) || create_secret_base_enemy(g, creator, o) ||
               create_casino_paradise_enemy(g, creator, o) ||
               create_ice_paradise_enemy(g, creator, o) ||
               create_angel_island_enemy(g, creator, o) || create_egg_rocket_enemy(g, creator, o) ||
               create_x_zone_enemy(g, creator, o) || create_moon_enemy(g, creator, o);
    }
}

} // namespace sonic_advance_gba
