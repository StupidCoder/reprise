// Ice Paradise's objects (zone 4).

#include "sonic_advance_gba/game/ice_paradise.hpp"
#include "sonic_advance_gba/game/collision.hpp"
#include "sonic_advance_gba/game/destruction.hpp"
#include "sonic_advance_gba/game/game.hpp"
#include "sonic_advance_gba/game/interactive.hpp"
#include "sonic_advance_gba/game/math.hpp"
#include "sonic_advance_gba/game/object_base.hpp"
#include "sonic_advance_gba/game/object_util.hpp"
#include "sonic_advance_gba/game/player.hpp"
#include "sonic_advance_gba/game/solid.hpp"
#include "sonic_advance_gba/game/sound.hpp"
#include "sonic_advance_gba/game/stage.hpp"
#include <array>
#include <cstdlib>

namespace sonic_advance_gba {

namespace {

// Whether a short-lived thing drawn at (sx, sy) is still near the view.
bool near_view(s16 sx, s16 sy) {
    return u16(sx + 0x14) < 0x115 && sy + 0x14 >= 0 && sy < 0xF1;
}

// 08071AE4, 08072724 (and 0807266C, 08071B94): an icicle shot, moved
// `step_x` pixels a frame across and falling with `gravity`; it hurts
// players it touches and ends out of view.
class IceShot final : public Process {
public:
    IceShot(Game& g, s16 x, s16 y, s16 step_x, s16 speed_y, s16 gravity)
        : Process(0x3000), step_x_(step_x), speed_y_(speed_y), gravity_(gravity) {
        sprite_.start(0x12580, 0x1AB, 0, x, y, 0x40, 0);
        animate(g, sprite_);
    }

    void update(Game& g) override {
        const auto& c = g.stage().camera;
        sprite_.x = s16(sprite_.x - step_x_);
        speed_y_ = s16(speed_y_ + gravity_);
        sprite_.y = s16(sprite_.y + s8(u16(speed_y_) >> 8));
        const s16 x = sprite_.x, y = sprite_.y;
        projectile_hits(g, sprite_, x, y);
        sprite_.x = s16(x - c.x);
        sprite_.y = s16(y - c.y);
        if (!near_view(sprite_.x, sprite_.y)) {
            g.scheduler().destroy(*this);
            return;
        }
        animate(g, sprite_);
        draw(g, sprite_);
        sprite_.x = x;
        sprite_.y = y;
    }

private:
    Sprite sprite_; // +00 (x, y in the act between frames)
    s16 step_x_;    // +32
    s16 speed_y_;   // +34
    s16 gravity_;
};

// 080714C4, 080716C0: walks to and fro on the ground within its range
// (record +3: the start, +5: the width, in 8 pixels) at 0A0h, drawn as two
// halves (the second mirrored); when the player is above it (within 8
// pixels across) it fires an icicle straight up (08071904: on the 4th of
// 13 frames; then 60 frames before the next).
class UpShooter final : public Enemy {
public:
    UpShooter(Game& g, const ObjectSpawn& o) : Enemy(g, o, 8) {
        // The left half in front of everything (layer 0), as the original sets.
        sprite_.start(tiles_, 0x1A6, 1, s16(base_x_), s16(base_y_), 0, 0x2000);
        animate(g, sprite_);
        right_tiles_ = g.sprite_tiles.allocate(8);
        right_.start(right_tiles_, 0x1A6, 1, s16(base_x_), s16(base_y_), 0x480, 0x2000);
        animate(g, right_);
    }

    void on_destroy() override { // 08071C30
        Enemy::on_destroy();
        game_.sprite_tiles.free(right_tiles_);
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        if (!shooting_) {
            offset_x_ = u16(offset_x_ + speed_);
        }
        const s16 x = s16(base_x_ + s8(offset_x_ >> 8));
        const s16 y = s16(base_y_ - 6 + offset_y_);
        sprite_.x = right_.x = s16(x - s.camera.x);
        sprite_.y = right_.y = s16(y - s.camera.y);
        if (out_of_range(s, base_x_, base_y_, sprite_.x, sprite_.y)) {
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
        if (shooting_) {
            if (++timer_ == 4) {
                g.scheduler().create<IceShot>(g, x, s16(y - 0x12), 0, -0x500, 0x28);
            }
            if (timer_ == 0xD) {
                sprite_.variant = right_.variant = 1;
                shooting_ = false;
            }
        } else {
            walk(s, x, y);
        }
        animate(g, sprite_);
        animate(g, right_);
        draw(g, sprite_);
        right_.flags |= sprite_flag::flip_x;
        draw(g, right_);
        right_.flags &= ~sprite_flag::flip_x;
    }

private:
    void walk(const StageState& s, s16 x, s16 y) {
        const s32 along = s8(offset_x_ >> 8);
        if (along <= param(3) * 8) {
            speed_ = 0xA0;
        } else if (along >= (param(3) + spawn_.record[5]) * 8) {
            speed_ = -0xA0;
        } else if (cooldown_ != 0) {
            --cooldown_;
        } else {
            const Player& p = s.player[0];
            const s32 px = p.x >> 8;
            if ((p.y >> 8) <= y && px <= x + 8 && x - 8 <= px) {
                cooldown_ = 0x3C;
                timer_ = 0;
                // (The previous variant is kept: the change shows when the
                // script next reads it.)
                sprite_.variant = right_.variant = 0;
                shooting_ = true;
            }
        }
    }

    Sprite sprite_;     // +0C the left half
    Sprite right_;      // +48
    u32 right_tiles_{}; // +4C
    s16 speed_ = -0xA0; // +78
    u16 offset_x_{};    // +7A 8.8
    s16 offset_y_{};    // +7C
    s16 timer_{};       // +7E
    s16 cooldown_{};    // +80
    bool shooting_{};   // the update is 08071904
};

// 08071C4C, 08072280 / 08071E90: climbs up and down a wall within its range
// (record +4: the start, +6: the height, in 8 pixels) at 0A0h, drawn as two
// halves (the second upside down). Record +3 picks the side: 0 faces right
// (the wall to its left), else left; when the player is level with it
// (within 8 pixels) on the side it faces, it fires an icicle that way
// (080724BC / 080720C8: on the 4th of 18 frames; then 60 frames).
class WallClimber final : public Enemy {
public:
    WallClimber(Game& g, const ObjectSpawn& o) : Enemy(g, o, 8), facing_right_(o.record[3] == 0) {
        const u32 flags = o.record[3] != 1 ? 0x2400 : 0x2000;
        sprite_.start(tiles_, 0x1A7, 1, s16(base_x_), s16(base_y_), 0, 0x2000);
        animate(g, sprite_);
        sprite_.flags = flags;
        lower_tiles_ = g.sprite_tiles.allocate(8);
        lower_.start(lower_tiles_, 0x1A7, 1, s16(base_x_), s16(base_y_), 0, flags);
        animate(g, lower_);
    }

    void on_destroy() override { // 080727BC
        Enemy::on_destroy();
        game_.sprite_tiles.free(lower_tiles_);
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        if (!shooting_) {
            offset_y_ = u16(offset_y_ + speed_);
        }
        const s16 x = s16(base_x_ + offset_x_);
        const s16 y = s16(base_y_ + s8(offset_y_ >> 8));
        sprite_.x = lower_.x = s16(x - s.camera.x);
        sprite_.y = lower_.y = s16(y - s.camera.y);
        // Facing right, the shot leaves on the 4th frame before anything
        // else is looked at.
        if (shooting_ && facing_right_ && ++timer_ == 4) {
            fire(g, x, y);
        }
        if (out_of_range(s, base_x_, base_y_, sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        if (enemy_touched(g, sprite_, x, y)) {
            g.scheduler().destroy(*this);
            return;
        }
        if (shooting_) {
            if (!facing_right_ && ++timer_ == 4) {
                fire(g, x, y);
            }
            if (timer_ == 0x12) {
                sprite_.variant = lower_.variant = 1;
                shooting_ = false;
            }
        } else {
            climb(g, s, x, y);
        }
        animate(g, sprite_);
        animate(g, lower_);
        draw(g, sprite_);
        lower_.flags |= sprite_flag::flip_y;
        draw(g, lower_);
        lower_.flags &= ~sprite_flag::flip_y;
    }

private:
    void fire(Game& g, s16 x, s16 y) {
        // The shot is placed 8 pixels left of where it was meant to leave.
        if (facing_right_) {
            g.scheduler().create<IceShot>(g, s16(x + 10 - 8), y, -3, 0, 0x17);
        } else {
            g.scheduler().create<IceShot>(g, s16(x - 10 - 8), y, 3, 0, 0x17);
        }
    }

    void climb(Game& g, const StageState& s, s16 x, s16 y) {
        if (facing_right_) {
            offset_x_ =
                s16(offset_x_ - probe_short(g.assets(), s.map, x, y, 1, -8, nullptr, Sensor::wall));
        } else {
            offset_x_ =
                s16(offset_x_ + probe_short(g.assets(), s.map, x, y, 1, 8, nullptr, Sensor::wall));
        }
        const s32 along = s8(offset_y_ >> 8);
        if (along <= param(4) * 8) {
            speed_ = 0xA0;
        } else if (along >= (param(4) + spawn_.record[6]) * 8) {
            speed_ = -0xA0;
        } else if (cooldown_ != 0) {
            --cooldown_;
        } else {
            const Player& p = s.player[0];
            const s32 px = p.x >> 8, py = p.y >> 8;
            const bool ahead = facing_right_ ? x <= px : px <= x;
            if (ahead && py <= y + 8 && y - 8 <= py) {
                cooldown_ = 0x3C;
                timer_ = 0;
                sprite_.variant = lower_.variant = 0;
                shooting_ = true;
            }
        }
    }

    bool facing_right_; // record +3 == 0 (+82)
    Sprite sprite_;     // +0C the upper half
    Sprite lower_;      // +48
    u32 lower_tiles_{}; // +4C
    s16 speed_ = -0xA0; // +78
    s16 offset_x_{};    // +7A
    u16 offset_y_{};    // +7C 8.8
    s16 timer_{};       // +7E
    s16 cooldown_{};    // +80
    bool shooting_{};
};

class FrozenDiver;

// 08072C84, 08072F78: the ice block holding a FrozenDiver. Once the diver
// breaks out it shatters (0807306C): four pieces thrown up and apart for
// 30 frames, moved and drawn only on the frames with bit 1 of the count
// set.
class IceBlock final : public Process {
public:
    IceBlock(Game& g, s16 x, s16 y) : Process(0x3000), game_(g), x_(x), y_(y) {
        const Assets& a = g.assets();
        for (unsigned i = 0; i < pieces_.size(); ++i) {
            Piece& p = pieces_[i];
            const bool even = (i & 1) == 0;
            p.speed_y = i < 2 ? -0x100 : -0x150;
            p.step_x = i < 2 ? (even ? 3 : -2) : (even ? -3 : 2);
            p.x = s16(x + s8(a.u8(table_ + 2 * i)));
            p.x4 = s16(p.x * 4);
            p.y = s16(y + s8(a.u8(table_ + 2 * i + 1)));
        }
        large_.start(g.sprite_tiles.allocate(4), 0x243, 0, x, y, 0x280, 0x1000);
        small_.start(g.sprite_tiles.allocate(1), 0x244, 0, x, y, 0x280, 0x1000);
        block_.start(g.sprite_tiles.allocate(0x1C), 0x23D, 0, x, y, 0x280, 0x1000);
    }

    void on_destroy() override; // 080731C8

    // The diver broke out: shatter from the next frame.
    void shatter() { shattering_ = true; }

    void forget_diver() { diver_ = nullptr; }

    void set_diver(FrozenDiver* d) { diver_ = d; }

    void update(Game& g) override {
        if (shattered_) {
            fly_apart(g);
            return;
        }
        const StageState& s = g.stage();
        block_.x = s16(x_ - s.camera.x);
        block_.y = s16(y_ - s.camera.y);
        if (out_of_range(s, x_, y_, block_.x, block_.y)) {
            g.scheduler().destroy(*this);
            return;
        }
        if (shattering_) {
            shattered_ = true;
        }
        animate(g, block_);
        draw(g, block_);
    }

private:
    static constexpr u32 table_ = 0x086C8C18; // the pieces' offsets (x, y)

    struct Piece {
        s16 x{}, y{};  // +94, +9C act pixels
        s16 x4{};      // +A4 quarter pixels
        s16 step_x{};  // +AC quarter pixels a frame
        s16 speed_y{}; // +B4
    };

    void fly_apart(Game& g) {
        if (s8(++timer_) >= 0x1E) {
            g.scheduler().destroy(*this);
            return;
        }
        const auto& c = g.stage().camera;
        animate(g, large_);
        animate(g, small_);
        if (!(timer_ & 2)) {
            return;
        }
        for (unsigned i = 0; i < pieces_.size(); ++i) {
            Piece& p = pieces_[i];
            p.x4 = s16(p.x4 + p.step_x);
            p.x = s16(quotient(p.x4, 4));
            p.speed_y = s16(p.speed_y + 0x14);
            p.y = s16(p.y + s8(u16(p.speed_y) >> 8));
            Sprite& sprite = (i & 1) == 0 ? small_ : large_;
            sprite.x = s16(p.x - c.x);
            sprite.y = s16(p.y - c.y);
            draw(g, sprite);
        }
    }

    Game& game_;
    s16 x_, y_;    // +90, +92
    Sprite large_; // +00
    Sprite small_; // +30
    Sprite block_; // +60
    std::array<Piece, 4> pieces_{};
    u8 timer_{};        // +BC
    bool shattering_{}; // +BD set by the diver
    bool shattered_{};  // the update is 0807306C
    FrozenDiver* diver_{};
};

// 080727D8, 080728E8: an enemy frozen in an ice block (made with it).
// When the player comes within 80 pixels in front of it (within 45
// degrees of level, either side) it breaks out and flies straight at the
// player (08072B70: at 1/35 of the direction a frame, until out of range).
class FrozenDiver final : public Enemy {
public:
    FrozenDiver(Game& g, const ObjectSpawn& o) : Enemy(g, o, 10) {
        sprite_.start(tiles_, 0x1A8, 0, s16(base_x_), s16(base_y_), 0x2C0, 0x1000);
        animate(g, sprite_);
        block_ = &g.scheduler().create<IceBlock>(g, s16(base_x_), s16(base_y_));
        block_->set_diver(this);
    }

    void on_destroy() override {
        Enemy::on_destroy();
        if (block_ != nullptr) {
            block_->forget_diver();
        }
    }

    void forget_block() { block_ = nullptr; }

    void update(Game& g) override {
        if (flying_) {
            fly(g);
            return;
        }
        StageState& s = g.stage();
        const s16 x = s16(base_x_), y = s16(base_y_);
        last_x_ = x;
        sprite_.x = s16(x - s.camera.x);
        sprite_.y = s16(y - s.camera.y);
        if (out_of_range(s, x, y, sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        const Assets& a = g.assets();
        const Player& p = s.player[0];
        const s32 dx = (p.x >> 8) - x, dy = (p.y >> 8) - y;
        const unsigned angle = unsigned(octant_angle(a, s16(dy), s16(dx))) & 0x3FF;
        const bool near = dx * dx + dy * dy < 0x1900;
        if (u16(angle - 0x181) < 0xFF && near) {
            // To the left (the horizontal speed made negative either way).
            break_out(g);
            sprite_.flags &= ~sprite_flag::flip_x;
            speed_x_ = -std::abs(divide(sine(a, angle + 0x100), 0x23));
            speed_y_ = divide(sine(a, angle), 0x23);
        }
        if (u16(angle - 0x80) > 0x300 && near) {
            break_out(g);
            sprite_.flags |= sprite_flag::flip_x;
            speed_x_ = divide(sine(a, angle + 0x100), 0x23);
            speed_y_ = divide(sine(a, angle), 0x23);
        }
        if (enemy_touched(g, sprite_, x, y)) {
            g.scheduler().destroy(*this);
            return;
        }
        animate(g, sprite_);
        draw(g, sprite_);
    }

private:
    void break_out(Game&) {
        if (block_ != nullptr) {
            block_->shatter();
        }
        offset_x_ = offset_y_ = 0;
        flying_ = true;
    }

    // 08072B70
    void fly(Game& g) {
        StageState& s = g.stage();
        offset_x_ += speed_x_;
        offset_y_ += speed_y_;
        const s16 x = s16(base_x_ + (offset_x_ >> 8)), y = s16(base_y_ + (offset_y_ >> 8));
        sprite_.x = s16(x - s.camera.x);
        sprite_.y = s16(y - s.camera.y);
        // The range test compares the last resting x with both the view's
        // columns and its rows, as the original does.
        if (out_of_range(s, last_x_, last_x_, sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        if (enemy_touched(g, sprite_, x, y)) {
            g.scheduler().destroy(*this);
            return;
        }
        animate(g, sprite_);
        draw(g, sprite_);
    }

    Sprite sprite_;     // +0C
    s32 speed_x_{};     // +3C 8.8
    s32 speed_y_{};     // +40
    s32 offset_x_{};    // +44
    s32 offset_y_{};    // +48
    s32 last_x_{};      // +4C
    IceBlock* block_{}; // +54
    bool flying_{};     // the update is 08072B70
};

void IceBlock::on_destroy() {
    game_.sprite_tiles.free(large_.tiles.to);
    game_.sprite_tiles.free(small_.tiles.to);
    game_.sprite_tiles.free(block_.tiles.to);
    if (diver_ != nullptr) {
        diver_->forget_block();
    }
}

// 080731EC, 08073310: walks to and fro within its range (record +3: the
// start, +5: the width, in 8 pixels) at 40h without following the ground,
// drawn twice (the second mirrored); every 500 frames it flashes for 120
// (080734B0): still, harmful to touch and not to be attacked.
class Flasher final : public Enemy {
public:
    Flasher(Game& g, const ObjectSpawn& o) : Enemy(g, o, 0xF) {
        sprite_.start(tiles_, 0x1A9, 0, s16(base_x_), s16(base_y_), 0x480, 0x2000);
        animate(g, sprite_);
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        if (!flashing_) {
            offset_x_ += speed_;
        }
        const s16 x = s16(base_x_ + (offset_x_ >> 8)), y = s16(base_y_);
        sprite_.x = s16(x - s.camera.x);
        sprite_.y = s16(y - s.camera.y);
        if (out_of_range(s, base_x_, base_y_, sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        if (flashing_) {
            Player& p = s.player[0];
            const Hitbox& h = sprite_.hitboxes[0];
            if (h.index != 0xFFFFFFFF &&
                hitboxes_meet(x, y, h, p.x >> 8, p.y >> 8, p.body->sprite.hitboxes[0])) {
                hurt_player(g, p);
            }
            if (++timer_ > 0x77) {
                sprite_.previous_variant = 0xFF;
                sprite_.variant = 0;
                flashing_ = false;
            }
        } else {
            if (enemy_touched(g, sprite_, x, y)) {
                g.scheduler().destroy(*this);
                return;
            }
            walk();
        }
        animate(g, sprite_);
        draw(g, sprite_);
        sprite_.flags |= sprite_flag::flip_x;
        draw(g, sprite_);
        sprite_.flags &= ~sprite_flag::flip_x;
    }

private:
    void walk() {
        const s32 along = offset_x_ >> 8;
        if (along <= param(3) * 8) {
            speed_ = 0x40;
        } else if (along >= (param(3) + spawn_.record[5]) * 8) {
            speed_ = -0x40;
        } else if (cooldown_ != 0) {
            --cooldown_;
        } else {
            timer_ = 0;
            cooldown_ = 500;
            sprite_.previous_variant = 0xFF;
            sprite_.variant = 1;
            flashing_ = true;
        }
    }

    Sprite sprite_;     // +0C
    s32 offset_x_{};    // +3C 8.8
    s16 speed_ = -0x40; // +40
    s16 timer_{};       // +42
    s16 cooldown_{};    // +46
    bool flashing_{};   // the update is 080734B0
};

// 08073EF0, 08073FE8: a snowball bursting: two halves thrown up and apart
// (100h a frame each way, from 3C0h up, falling at 3Ch), until below the
// view.
class SnowBurst final : public Process {
public:
    SnowBurst(Game& g, s16 x, s16 y)
        : Process(0x3000), left_x_(s32(x) << 8), right_x_(s32(x) << 8), y_(s32(y) << 8) {
        sprite_.start(0x12620, 0x1AD, 0, x, y, 0x240, 0x2000);
        animate(g, sprite_);
    }

    void update(Game& g) override {
        const auto& c = g.stage().camera;
        left_x_ += speed_x_;
        right_x_ -= speed_x_;
        speed_y_ = s16(speed_y_ + 0x3C);
        y_ += speed_y_;
        sprite_.x = s16(s16(left_x_ >> 8) - c.x);
        sprite_.y = s16((y_ >> 8) - c.y);
        if (sprite_.y >= 0xF1) {
            g.scheduler().destroy(*this);
            return;
        }
        animate(g, sprite_);
        draw(g, sprite_);
        sprite_.x = s16(s16(right_x_ >> 8) - c.x);
        draw(g, sprite_);
    }

private:
    Sprite sprite_;        // +00
    s16 speed_x_ = -0x100; // +30
    s16 speed_y_ = -0x3C0; // +32
    s32 left_x_;           // +3C 8.8
    s32 right_x_;          // +40
    s32 y_;                // +44
};

// 08073B9C, 08073C8C: a snowball rolled along the ground at 200h, sped up
// and slowed by the slope; it falls off edges (3Ch a frame). It bursts on
// hitting a player, after 800 frames, landing faster than 400h, meeting a
// change of slope of a quarter turn or more, or rolling into a wall; it
// ends when out of view.
class Snowball final : public Process {
public:
    Snowball(Game& g, s16 x, s16 y, bool right)
        : Process(0x3000), speed_x_(right ? 0x200 : -0x200), x_(x), y_(y) {
        const StageState& s = g.stage();
        // (The ground's distance goes into the 8.8 offset unshifted.)
        offset_y_ += probe_short(g.assets(), s.map, y, x, 1, 8, nullptr, Sensor::floor);
        sprite_.start(0x125A0, 0x1AC, 0, x, y, 0x240, 0x2000);
        animate(g, sprite_);
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        const Assets& a = g.assets();
        if (falling_) {
            speed_y_ = s16(speed_y_ + 0x3C);
        }
        offset_x_ += speed_x_;
        offset_y_ += speed_y_;
        const s16 x = s16(x_ + (offset_x_ >> 8)), y = s16(y_ + (offset_y_ >> 8));
        // Down the slope (the original also guards against a sum above
        // FFFEh, which a 16-bit speed never reaches).
        speed_x_ = s16(speed_x_ + divide(sine(a, angle_ * 4u), 1000));
        offset_y_ += divide(sine(a, angle_ * 4u + 0x100), 1000);
        if (life_ != 0) {
            --life_;
        }
        if (projectile_hits(g, sprite_, x, y) || life_ == 0) {
            burst(g, x, y);
            return;
        }
        sprite_.x = s16(x - s.camera.x);
        sprite_.y = s16(y - s.camera.y);
        u8 ground_angle = 0;
        const s32 d = probe_short(a, s.map, y, x, 1, 8, &ground_angle, Sensor::floor);
        if (d > 0) {
            if (!falling_) {
                speed_y_ = 0;
            }
            falling_ = true;
        } else {
            if (falling_) {
                angle_ = 0;
                if (speed_y_ > 0x400) {
                    burst(g, x, y);
                    return;
                }
            }
            if (d < 0) {
                falling_ = false;
            }
        }
        if (!falling_) {
            if (d < 0) {
                offset_y_ += d << 8;
                speed_y_ = 0;
            } else {
                speed_y_ = s16(speed_y_ + d * 0x40);
            }
            s8 change = s8(angle_ - ground_angle);
            if (change < 0) {
                change = s8(-change);
            }
            if (change > 0x3F) {
                burst(g, x, y);
                return;
            }
            angle_ = ground_angle;
        }
        if (d >= -5 && d <= 0) {
            // On the ground: a steep wall ahead bursts it.
            if (u8(ground_angle + 0x3F) <= 0x1C || u8(ground_angle + 0x5D) <= 0x1C) {
                burst(g, x, y);
                return;
            }
        } else if (!(u16(sprite_.x + 0x3C) < 0x169 && sprite_.y + 0x3C >= 0 &&
                     sprite_.y <= 0x12C)) {
            g.scheduler().destroy(*this);
            return;
        }
        animate(g, sprite_);
        draw(g, sprite_);
    }

private:
    void burst(Game& g, s16 x, s16 y) {
        g.scheduler().create<SnowBurst>(g, x, y);
        g.scheduler().destroy(*this);
    }

    Sprite sprite_;  // +00
    s16 speed_x_;    // +30
    s16 speed_y_{};  // +32
    s32 offset_x_{}; // +34 8.8
    s32 offset_y_{}; // +38
    s32 x_, y_;      // +3C, +44
    u16 life_ = 800; // +48
    u8 angle_{};     // +4A the slope rolled on
    bool falling_{}; // +4B
};

// 080736A0, 080737C8: walks to and fro on the ground within its range
// (record +3: the start, +5: the width, in 8 pixels) at 0A0h; when the
// player is within 80 pixels it stops to roll a snowball (080739CC: on the
// 13th of 17 frames; then 60 frames before the next).
class SnowballThrower final : public Enemy {
public:
    SnowballThrower(Game& g, const ObjectSpawn& o) : Enemy(g, o, 6) {
        sprite_.start(tiles_, 0x1AA, 0, s16(base_x_), s16(base_y_), 0x480, 0x2000);
        animate(g, sprite_);
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        if (!throwing_) {
            offset_x_ += speed_;
        }
        const s16 x = s16(base_x_ + (offset_x_ >> 8)), y = s16(base_y_ + offset_y_);
        sprite_.x = s16(x - s.camera.x);
        sprite_.y = s16(y - s.camera.y);
        if (throwing_) {
            offset_y_ += ground(g, x, y);
            ++timer_;
        }
        if (out_of_range(s, base_x_, base_y_, sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        if (enemy_touched(g, sprite_, x, y)) {
            g.scheduler().destroy(*this);
            return;
        }
        if (throwing_) {
            throw_snowball(g, x, y);
        } else {
            offset_y_ += ground(g, x, y);
            walk(s, x, y);
        }
        animate(g, sprite_);
        draw(g, sprite_);
    }

private:
    s32 ground(Game& g, s16 x, s16 y) {
        return probe_short(g.assets(), g.stage().map, y, x, 1, 8, nullptr, Sensor::floor);
    }

    void walk(const StageState& s, s16 x, s16 y) {
        const Player& p = s.player[0];
        const s32 dx = (p.x >> 8) - x, dy = (p.y >> 8) - y;
        const s32 along = offset_x_ >> 8;
        const s32 start = param(3);
        if (along <= (start + 1) * 8) {
            if (!(sprite_.flags & sprite_flag::flip_x)) {
                sprite_.flags |= sprite_flag::flip_x;
                speed_ = 0xA0;
            }
        } else if (along >= (start + spawn_.record[5]) * 8) {
            if (sprite_.flags & sprite_flag::flip_x) {
                sprite_.flags &= ~sprite_flag::flip_x;
                speed_ = -0xA0;
            }
        } else if (cooldown_ != 0) {
            --cooldown_;
        } else if (dx * dx + dy * dy < 0x1900) {
            timer_ = 0;
            cooldown_ = 0x3C;
            sprite_.variant = 1;
            sprite_.previous_variant = 0xFF;
            throwing_ = true;
        }
    }

    // 080739CC (the ground is probed twice a frame here).
    void throw_snowball(Game& g, s16 x, s16 y) {
        if (timer_ == 0xD) {
            const bool right = sprite_.flags & sprite_flag::flip_x;
            g.scheduler().create<Snowball>(g, s16(right ? x + 4 : x - 6), y, right);
        }
        offset_y_ += ground(g, x, y);
        if (timer_ > 0x10) {
            timer_ = 0;
            sprite_.previous_variant = 0xFF;
            sprite_.variant = 0;
            throwing_ = false;
        }
    }

    Sprite sprite_;     // +0C
    s32 offset_x_{};    // +3C 8.8
    s32 offset_y_{};    // +40
    s16 speed_ = -0xA0; // +44
    s16 timer_{};       // +46
    s16 cooldown_{};    // +48
    bool throwing_{};   // the update is 080739CC
};

// 08091200, 080912F8 (and 08090D3C, 08090E20): a bubble rising from a vent
// at half a pixel a frame, wobbling 16 pixels across; it bursts at the
// water's surface. The small ones (sizes 0-15, picked by the time) only
// rise; the large one (size 4, made by the vent's own clock) from its 80th
// frame gives a player touching it air
// (08091124: then it shows bursting for 16 frames).
class Bubble final : public Process {
public:
    // The large bubble.
    Bubble(Game& g, s16 x, s16 y)
        : Process(0x2000), game_(g), size_(4), large_(true), x_(x), y_(y) {
        sprite_.start(g.sprite_tiles.allocate(9), 0x214, 0, x, y, 0x3C0, 0x2000);
        animate(g, sprite_);
    }

    // A small bubble; `kind` bit 0 picks the picture.
    Bubble(Game& g, s16 x, s16 y, u8 kind)
        : Process(0x2000), game_(g), size_(kind & 0xF), large_(false), x_(s16(x - 8)), y_(y) {
        const bool second = kind & 1;
        sprite_.start(second ? 0x12AA0 : 0x12A20, second ? 0x2F2 : 0x2F1, 0, x, y, 0x480, 0x2000);
    }

    // 080913E4: frees the tiles when the size is 4. A small bubble of size
    // 4 then "frees" its fixed tiles: the map index wraps (FFF0h, FFF1h) and
    // the cleared halfword lands in the sprite table buffer, at 03004A10 or
    // 03004A12: attribute 0 or 1 of entry 124.
    void on_destroy() override {
        if (large_) {
            game_.sprite_tiles.free(sprite_.tiles.to);
        } else if (size_ == 4) {
            game_.video().cleared_attributes.push_back({124, sprite_.tiles.to == 0x12A20 ? 0 : 1});
        }
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        const s16 wobble = s16(sine(g.assets(), (age_ * 3u) & 0x3FF) >> 11);
        sprite_.x = s16(x_ - s.camera.x + wobble + (large_ ? 0 : size_));
        sprite_.y = s16(y_ - s.camera.y - (age_ >> 1));
        const bool shown =
            u16(sprite_.x + 0x80) <= 0x1F0 && sprite_.y + 0x80 >= 0 && sprite_.y <= 0x120;
        const bool below_surface = s.water_level >= 0 && s.water_level <= s16(y_) - (age_ >> 1);
        if (bursting_) {
            // 08091124
            if (!shown || s8(--burst_timer_) < 0 || !below_surface) {
                g.scheduler().destroy(*this);
                return;
            }
            animate(g, sprite_);
            draw(g, sprite_);
            return;
        }
        if (!shown || !below_surface) {
            g.scheduler().destroy(*this);
            return;
        }
        if (!large_) {
            ++age_;
            animate(g, sprite_);
            draw(g, sprite_);
            return;
        }
        // (The original stops animating other sizes after size x 8 frames;
        // only size 4 comes here.)
        ++age_;
        animate(g, sprite_);
        if (age_ >= 0x50) {
            for (unsigned n = 0; n < s.players; ++n) {
                Player& p = s.player[n];
                if (touches_player(sprite_, s16(s.camera.x + sprite_.x),
                                   s16(s.camera.y + sprite_.y), p)) {
                    breathe(g, p);
                    bursting_ = true;
                    burst_timer_ = 0x10;
                }
            }
        }
        draw(g, sprite_);
    }

private:
    static void breathe(Game& g, Player& p) {
        end_player_moves(g, p);
        p.state = 0x1B;
        if (p.flags & player_flag::rolling) {
            p.flags &= ~(player_flag::rolling | player_flag::jump_cancel);
            if (p.flags & player_flag::in_air) {
                p.radius_x = 6;
                p.radius_y = 0xE;
            } else {
                set_player_radius(g, p, 6, 0xE);
            }
        }
        p.flags &= ~player_flag::pushing;
        p.air_ticks = 0x3C;
        p.air = 0x1E;
        p.speed_x = p.speed_y = p.ground_speed = 0;
        if (p.index == 0) {
            g.sound().stop(0x1B); // the drowning music
        }
        g.sound().start(0xBF);
    }

    Game& game_;
    u8 size_; // +3C
    bool large_;
    s16 x_, y_;        // +40, +42 where it rose from
    u16 age_{};        // +3E frames risen
    u8 burst_timer_{}; // +3D
    bool bursting_{};  // the update is 08091124
    Sprite sprite_;    // +0C
};

// 08090B0C, 08090C10: a vent under water letting out bubbles: a large one
// on its own clock (the first after 120 frames, then the gaps from the
// table 086C8D7C in turn), small ones on another (the first after 60
// frames, then half a table gap picked by the stage clock).
class BubbleVent final : public Interactive {
public:
    BubbleVent(Game& g, const ObjectSpawn& o) : Interactive(o), game_(g) {
        sprite_.start(g.sprite_tiles.allocate(4), 0x215, 0, x_, y_, 0x480, 0x2000);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 080913D0

    void update(Game& g) override {
        StageState& s = g.stage();
        const Assets& a = g.assets();
        sprite_.x = s16(x_ - s.camera.x);
        sprite_.y = s16(y_ - s.camera.y);
        if (--large_timer_ == 0) {
            g.scheduler().create<Bubble>(g, x_, s16(y_ - 4));
            large_timer_ = a.u16(gaps + gap_ * 2u);
            gap_ = u16((gap_ + 1) & 7);
        }
        if (small_timer_-- == 1) {
            g.scheduler().create<Bubble>(g, x_, s16(y_ - 4), u8((large_timer_ >> 3) + s.ticks));
            small_timer_ = s16(divide(s16(a.u16(gaps + (s.ticks & 7) * 2u)), 2));
        }
        if (!near_view(sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        animate(g, sprite_);
        draw(g, sprite_);
    }

private:
    static constexpr u32 gaps = 0x086C8D7C; // eight gaps between bubbles (frames)

    Game& game_;
    Sprite sprite_;          // +0C
    s16 small_timer_ = 0x3C; // +3C
    u16 large_timer_ = 0x78; // +3E
    u16 gap_{};              // +40
};

// 0808E0A0, 0808E1FC: a block that rises (0C0h a frame) while a player
// stands on it, carrying the player, and sinks back to its place when
// nobody does; risen past record +6 eight-pixel units it kills its rider
// (crushed). (Record +3-+6 also set a direction the update never reads.)
class RisingBlock final : public Interactive {
public:
    RisingBlock(Game& g, const ObjectSpawn& o) : Interactive(o), game_(g) {
        sprite_.start(g.sprite_tiles.allocate(0x12), 0x20B, 0, x_, y_, 0x480, 0x2000);
        animate(g, sprite_);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 0808E470

    void update(Game& g) override {
        StageState& s = g.stage();
        const s16 y = s16(y_ - (rise_ >> 8));
        sprite_.x = s16(x_ - s.camera.x);
        sprite_.y = s16(y - s.camera.y);
        bool carrying = false, someone_off = false;
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if ((p.flags & player_flag::on_object) && p.standing_on == &sprite_) {
                carrying = true;
                if (!touches_player(sprite_, x_, s16(y - 4), p)) {
                    p.flags &= ~player_flag::on_object;
                } else {
                    p.y -= 0xC0;
                    if ((rise_ >> 8) > spawn_.record[6] * 8) {
                        p.flags = (p.flags & ~player_flag::on_object) | player_flag::dead;
                    }
                }
            } else if (!(collide_platform(g, sprite_, x_, y, p) & solid::on_top)) {
                someone_off = true;
            }
        }
        if (carrying) {
            rise_ += 0xC0;
        } else if (someone_off && rise_ != 0) {
            rise_ -= 0xC0;
        }
        if (out_of_range(s, x_, y_, sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        draw(g, sprite_);
    }

private:
    Game& game_;
    Sprite sprite_; // +0C
    s32 rise_{};    // +44 8.8
};

// 0808DB58, 0808DCE8: a platform that sinks (0C0h a frame) while a player
// stands on it, carrying the player down, until the ground is 22 pixels
// below; when nobody stands on it, it rises back. Its picture comes from
// the act's entry in 086C8CDC (animation, variant, tiles). A player
// landing on it is lifted 2 pixels (unless spin dashing). (Record +3-+6
// set a direction the update never reads.)
class SinkingPlatform final : public Interactive {
public:
    SinkingPlatform(Game& g, const ObjectSpawn& o) : Interactive(o), game_(g) {
        const Assets& a = g.assets();
        const u32 entry = pictures + u8(g.stage().zone) * 6u;
        sprite_.start(g.sprite_tiles.allocate(a.u16(entry + 4)), a.u16(entry), u8(a.u16(entry + 2)),
                      x_, y_, 0x480, 0x2000);
        animate(g, sprite_);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 0808E470

    void update(Game& g) override {
        StageState& s = g.stage();
        const Assets& a = g.assets();
        u8 first_layer = 0;
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            const u8 bit = u8(1u << n);
            const s16 top = s16(y_ + (sink_ >> 8));
            if ((p.flags & player_flag::on_object) && p.standing_on == &sprite_) {
                const s8 room =
                    s8(probe_short(a, s.map, top + 0x16, x_, p.layer, 8, nullptr, Sensor::floor));
                if (!touches_player(sprite_, x_, s16(top - 4), p)) {
                    p.flags &= ~player_flag::on_object;
                    p.standing_on = nullptr;
                    riders_ &= u8(~bit);
                } else {
                    riders_ |= bit;
                    if (room >= 0) {
                        p.y += 0xC0;
                    }
                    const s8 d = s8(probe_short(a, s.map, (p.y >> 8) + 0x10, p.x >> 8, p.layer, 8,
                                                nullptr, Sensor::floor));
                    if (d < 0) {
                        p.y += d * 0x100;
                        p.flags &= ~player_flag::on_object;
                    }
                }
            } else if (!(collide_platform(g, sprite_, x_, top, p) & solid::on_top)) {
                riders_ &= u8(~bit);
            } else {
                if (!(p.flags & player_flag::spin_dash)) {
                    p.y -= 0x200;
                }
                riders_ |= bit;
            }
            if (n == 0) {
                first_layer = p.layer;
            }
        }
        if (riders_ == 0) {
            if (sink_ != 0) {
                sink_ -= 0xC0;
            }
        } else if (s8(probe_short(a, s.map, y_ + (sink_ >> 8) + 0x16, x_, first_layer, 8, nullptr,
                                  Sensor::floor)) >= 0) {
            sink_ += 0xC0;
        }
        sprite_.x = s16(x_ - s.camera.x);
        sprite_.y = s16(y_ - s.camera.y + (sink_ >> 8));
        if (out_of_range(s, x_, y_, sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        draw(g, sprite_);
    }

private:
    static constexpr u32 pictures = 0x086C8CDC; // by act: animation, variant, tiles

    Game& game_;
    Sprite sprite_; // +0C
    s32 sink_{};    // +44 8.8
    u8 riders_{};   // +50 a bit per player standing on it
};

// 0808AF78, 0808B130: a springy platform swinging to and fro (record +5
// across or +6 up and down, the larger; 8 pixels each per unit, 256 frames
// a swing; record +3 / +4 negative starts half a swing on). A player
// standing on it (or hitting it with Amy's hammer) is thrown up at 780h
// (0808B4A8; 0B40h hammering from off it); it shows bent for 13 frames
// (0808B2B4). It is solid, its sides pushing players out.
class SpringPlatform final : public Interactive {
public:
    SpringPlatform(Game& g, const ObjectSpawn& o) : Interactive(o), game_(g) {
        if (o.record[6] < o.record[5]) {
            step_x_ = 4;
            phase_ = s8(o.record[3]) < 0 ? 0x80 : 0;
        } else {
            step_y_ = 4;
            phase_ = s8(o.record[4]) < 0 ? 0x80 : 0;
        }
        const s8 zone = g.stage().zone;
        u16 animation = 0x21C;
        if ((zone >> 1) == 3) {
            animation = 0x20E;
        } else if ((zone >> 1) == 5 || zone == 0x11) {
            animation = 0x246;
        }
        rest_variant_ = animation == 0x21C ? 0 : 1;
        sprite_.start(g.sprite_tiles.allocate(0x1C), animation, rest_variant_, x_, y_, 0x480,
                      0x2000);
        animate(g, sprite_);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 0808B984

    void update(Game& g) override {
        StageState& s = g.stage();
        const Assets& a = g.assets();
        const u32 at = (s.ticks + phase_) & 0xFF;
        if (step_x_ != 0) {
            offset_x_ = (spawn_.record[5] * 0x800 * sine(a, (step_x_ * at) & 0x3FF)) >> 14;
        }
        if (step_y_ != 0) {
            offset_y_ = (spawn_.record[6] * 0x800 * sine(a, (step_y_ * at) & 0x3FF)) >> 14;
        }
        const s16 x = s16(x_ + (offset_x_ >> 8)), y = s16(y_ + (offset_y_ >> 8));
        if (bent_) {
            meet_players(g, s, x, y);
            sprite_.x = s16(x - s.camera.x);
            sprite_.y = s16(y - s.camera.y);
            if (++timer_ > 0xC) {
                sprite_.variant = rest_variant_;
                bent_ = false;
            }
        } else {
            sprite_.x = s16(x - s.camera.x);
            sprite_.y = s16(y - s.camera.y);
            if (meet_players(g, s, x, y)) {
                bent_ = true;
            }
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
    // 0808B4A8: true when a player was thrown.
    bool meet_players(Game& g, StageState& s, s16 x, s16 y) {
        bool thrown = false;
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if (p.flags & player_flag::dead) {
                continue;
            }
            // The player's size widened by 5 pixels.
            Hitbox body;
            body.left = s8(-(p.radius_x + 5));
            body.top = s8(1 - p.radius_y);
            body.right = s8(p.radius_x + 5);
            body.bottom = s8(p.radius_y - 1);
            const bool hammered = hammer_hits(sprite_, x, y, p);
            const bool standing = (p.flags & player_flag::on_object) && p.standing_on == &sprite_;
            if (standing || hammered) {
                end_player_moves(g, p);
                p.flags |= player_flag::in_air;
                p.state = 0x11;
                p.body->sprite.previous_variant = 0xFF;
                p.speed_y = -0x780;
                if (hammered && !(p.flags & player_flag::on_object)) {
                    p.speed_y = s16(p.speed_y + (p.speed_y >> 1));
                }
                timer_ = 0;
                sprite_.variant = u8(rest_variant_ ^ 1);
                g.sound().start(0xAA);
                thrown = true;
            }
            const s32 before_x = p.x;
            push_out(g, s, x, y, body, p);
            const u32 r = collide_solid(g, sprite_, x, y, p);
            if (p.state == 0x11) {
                p.x = before_x;
            }
            // Met from below or a side (bits 10000h-100000h) while Knuckles (the
            // first player) glides or climbs (080549C8).
            if ((r & 0x170000) && s.character == 2 && u8(s.player[0].state - 0x46) <= 5) {
                end_player_moves(g, p);
                p.state = 5;
                p.flags |= player_flag::in_air | player_flag::rolling;
            }
            if (!(r & solid::pushing) && (p.flags & player_flag::pushing)) {
                p.flags &= ~player_flag::pushing;
                end_player_moves(g, p);
                p.state = 4;
            }
        }
        return thrown;
    }

    // 0808B84C: a player whose widened size meets the platform is put
    // beside it (when within 8 pixels of a side), then out of the ground.
    void push_out(Game& g, StageState& s, s16 x, s16 y, const Hitbox& body, Player& p) {
        const Hitbox& h = sprite_.hitboxes[0];
        const s32 px = p.x >> 8, py = p.y >> 8;
        if (!spans_meet(px + body.left, px + body.right, x + h.left, x + h.right) ||
            !spans_meet(py + body.top, py + body.bottom, y + h.top, y + h.bottom)) {
            return;
        }
        s32 to = x + h.left - body.right;
        if (!(px <= to + 8)) {
            to = x + h.right - body.left;
            if (!(to - 8 <= px)) {
                return;
            }
        }
        const Assets& a = g.assets();
        p.x = to * 0x100;
        s32 d = probe(a, s.map, (p.y >> 8) + 9, to, p.layer, 8, nullptr, Sensor::floor);
        if (d < 0) {
            p.y += d * 0x100;
        }
        d = probe(a, s.map, p.y >> 8, p.x >> 8, p.layer, -8, nullptr, Sensor::floor);
        if (d < 0) {
            p.y += d * 0x100;
        }
        p.flags &= ~(player_flag::on_object | player_flag::pushing);
    }

    Game& game_;
    Sprite sprite_;           // +0C
    u16 phase_{};             // +3C
    s32 offset_x_{};          // +40 8.8
    s32 offset_y_{};          // +44
    s16 step_x_{}, step_y_{}; // +48, +4A
    u16 timer_{};             // +4C
    u8 rest_variant_{};
    bool bent_{}; // the update is 0808B2B4
};

// 08091B90: a player attacking with the second hitbox in one of the
// character's attacking states (Sonic 5-7; Tails also 35h-36h; Knuckles
// also 3Eh-45h; Amy 57h-59h, 5Ah, 5Ch) meets the block.
bool attack_meets(const Sprite& s, s16 x, s16 y, const Player& p) {
    const u8 st = p.state;
    bool attacking = false;
    switch (p.character) {
    case 0:
        attacking = u8(st - 5) < 3;
        break;
    case 1:
        attacking = u8(st - 0x35) < 2 || u8(st - 5) < 3;
        break;
    case 2:
        attacking = u8(st - 0x3E) <= 7 || u8(st - 5) < 3;
        break;
    case 3:
        attacking = u8(st - 0x57) < 3 || st == 0x5C || st == 0x5A;
        break;
    default:
        break;
    }
    const Hitbox& attack = p.body->sprite.hitboxes[1];
    return attacking && attack.index != 0xFFFFFFFF &&
           hitboxes_meet(x, y, s.hitboxes[0], p.x >> 8, p.y >> 8, attack);
}

// 08091408, 080914F8: a solid block of ice broken by a player rolling down
// onto it, attacking it (0800C394, 08091B90) or landing on it in a spin
// (state 5; not Amy): an explosion, then its pieces fly apart (08091780).
// It stays broken.
class BreakableIce final : public Interactive {
public:
    BreakableIce(Game& g, const ObjectSpawn& o) : Interactive(o), game_(g) {
        sprite_.start(g.sprite_tiles.allocate(0x19), 0x22B, 0, x_, y_, 0x200, 0x2000);
        animate(g, sprite_);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 08009670

    void update(Game& g) override {
        StageState& s = g.stage();
        sprite_.x = s16(x_ - s.camera.x);
        sprite_.y = s16(y_ - s.camera.y);
        if (g.game_mode >= 2) {
            throw Unrecovered("breakable ice in multiplayer (080914F8)");
        }
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if (!(p.flags & player_flag::dead)) {
                bool landed_spinning = false;
                if (p.state == 5) {
                    const u32 r = collide_solid(g, sprite_, x_, y_, p);
                    landed_spinning = p.character != 3 && (r & solid::on_top);
                }
                if (breaks_block(g, sprite_, x_, y_, p) || attack_meets(sprite_, x_, y_, p) ||
                    landed_spinning) {
                    shatter(g, s);
                    return;
                }
            }
            collide_solid(g, sprite_, x_, y_, p);
        }
        if (!near_view(sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        draw(g, sprite_);
    }

private:
    void shatter(Game& g, StageState& s) {
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if ((p.flags & player_flag::on_object) && p.standing_on == &sprite_) {
                p.flags = (p.flags & ~player_flag::on_object) | player_flag::in_air;
            }
        }
        create_explosion(g, x_, y_);
        g.sound().start(0x8A);
        g.scene.focus_x = sprite_.x;
        g.scene.focus_y = sprite_.y;
        create_block_debris(g, s16(x_ - g.scene.focus_x), s16(y_ - (g.scene.focus_y + 0x10)),
                            {0x22D, 4, 0x22C, 0x40, 0x51, 1, 0x100, 0x70, 0});
        g.sound().start(0xC1);
        g.scheduler().destroy(*this);
    }

    Game& game_;
    Sprite sprite_; // +0C
};

// Whether spans [a_lo, a_hi] and [b_lo, b_hi] overlap (touching counts).
bool overlap(s32 a_lo, s32 a_hi, s32 b_lo, s32 b_hi) {
    return (a_lo <= b_lo && b_lo <= a_hi) || (b_lo <= a_lo && a_lo <= b_hi);
}

// 08092308, 08092468: a steam vent with a lid. Every 102 frames of the
// stage clock it blows (08092A68: the lid thrown up at 500h, falling at
// 25h, bouncing back at 2/5 of its speed twice before it rests; players on
// it ride along). While the steam shows (frames 6-54 of the blow) its
// second hitbox hurts players.
class SteamVent final : public Interactive {
public:
    SteamVent(Game& g, const ObjectSpawn& o) : Interactive(o), game_(g) {
        // (Neither picture is animated before the first update.)
        vent_.start(g.sprite_tiles.allocate(0x10), 0x231, 0, 0, 0, 0x480, 0x2000);
        lid_.start(g.sprite_tiles.allocate(0x12), 0x20D, 0, 0, 0, 0x440, 0x2000);
    }

    void on_destroy() override { // 08092C1C
        game_.sprite_tiles.free(vent_.tiles.to);
        game_.sprite_tiles.free(lid_.tiles.to);
    }

    void update(Game& g) override {
        if (blowing_) {
            blow(g);
        } else {
            rest(g);
        }
    }

private:
    void reset_vent() {
        vent_.previous_variant = 0xFF;
        vent_.animation = 0x231;
        vent_.variant = 0;
    }

    void rest(Game& g) {
        StageState& s = g.stage();
        vent_.x = lid_.x = s16(x_ - s.camera.x);
        vent_.y = s16(y_ - s.camera.y - 3);
        lid_.y = s16(y_ - s.camera.y);
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            const s32 before_x = p.x;
            if (collide_platform(g, lid_, x_, y_, p) & solid::on_top) {
                p.flags &= ~player_flag::in_air;
                p.y = (y_ - 0x1B) * 0x100;
            }
            p.x = before_x;
            collide_solid(g, vent_, x_, y_, p);
        }
        if (out_of_range(s, x_, y_, vent_.x, vent_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        if (remainder(s32(s.ticks), 0x66) == 0) {
            timer_ = 0x66;
            riders_ = 0;
            speed_ = -0x500;
            lift_ = 0;
            bounces_ = 3;
            vent_.animation = 0x230;
            vent_.variant = 0;
            vent_.previous_variant = 0xFF;
            g.sound().start(0xB6);
            blowing_ = true;
        }
        animate(g, vent_);
        draw(g, vent_);
        animate(g, lid_);
        draw(g, lid_);
    }

    // 080926A8
    void blow(Game& g) {
        StageState& s = g.stage();
        speed_ += 0x25;
        lift_ += speed_;
        const s16 top = s16(y_ + (lift_ >> 8));
        vent_.x = lid_.x = s16(x_ - s.camera.x);
        vent_.y = s16(y_ - s.camera.y - 3);
        lid_.y = s16(y_ - s.camera.y + (lift_ >> 8));
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            const u8 bit = u8(1u << n);
            if (!(riders_ & bit)) {
                if (collide_platform(g, lid_, x_, top, p) == 0) {
                    riders_ &= u8(~bit);
                    continue;
                }
                p.y = (top - 0x1B) * 0x100;
                riders_ |= bit;
                if (p.flags & player_flag::in_air) {
                    riders_ &= u8(~bit);
                }
                if (p.speed_y == 0 && p.state != 4) {
                    end_player_moves(g, p);
                    p.state = 4;
                }
            } else if (!touches_player(lid_, x_, top, p)) {
                riders_ &= u8(~bit);
            } else {
                p.y += speed_;
            }
        }
        const s32 before = timer_--;
        if (timer_ == 0x30) {
            reset_vent();
            animate(g, vent_);
        } else if (u32(before - 0x32) <= 0x2E) {
            scald(g, s);
            animate(g, vent_);
        } else if (timer_ < 0x30) {
            if (animate(g, vent_) == 0) {
                reset_vent();
            }
            animate(g, vent_);
        }
        if (lift_ >= 0) {
            if (--bounces_ == 0) {
                reset_vent();
                blowing_ = false;
            } else {
                speed_ = -divide(speed_ * 2, 5);
                riders_ = 0;
                lift_ = 0;
            }
        }
        draw(g, vent_);
        animate(g, lid_);
        draw(g, lid_);
    }

    // The steam (the vent's second hitbox) against the players' bodies.
    void scald(Game& g, StageState& s) {
        const Hitbox& h = vent_.hitboxes[1];
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            const Hitbox& b = p.body->sprite.hitboxes[0];
            if (b.index == 0xFFFFFFFF) {
                continue;
            }
            const s32 px = p.x >> 8, py = p.y >> 8;
            if (overlap(x_ + h.left, x_ + h.right, px + b.left, px + b.right) &&
                overlap(y_ + h.top, y_ + h.bottom, py + b.top, py + b.bottom)) {
                hurt_player(g, p);
            }
        }
    }

    Game& game_;
    Sprite vent_;    // +0C
    Sprite lid_;     // +44
    u8 riders_{};    // +74 a bit per player riding the lid
    s32 speed_{};    // +78 8.8
    s32 lift_{};     // +7C
    s32 bounces_{};  // +80
    s32 timer_{};    // +84 frames of the blow left
    bool blowing_{}; // the update is 080926A8
};

// 0808C4BC, 0808C628: a sled resting on the ground. A player landing on it
// (it is solid at its top and bottom only) sits in it and it slides off
// (0808C970) along record +4's direction (0: right), sped up and slowed by
// the slopes and carrying its riders; it leaves ledges (in Ice Paradise
// thrown down by 2/5 of its speed, elsewhere up by 3/5) and may land record
// +3 times; landing once more or meeting a wall it bursts (0808D6EC),
// throwing its riders up. Knuckles gliding or climbing only bumps it.
class Sled final : public Interactive {
public:
    Sled(Game& g, const ObjectSpawn& o)
        : Interactive(o), game_(g), landings_(o.record[3]), leftward_(o.record[4] != 0) {
        const u16 animation = (g.stage().zone >> 1) == 3 ? 0x211 : 0x223;
        sprite_.start(g.sprite_tiles.allocate(0x18), animation, 0, x_, y_, 0x480, 0x2000);
        animate(g, sprite_);
    }

    void on_destroy() override { // 0808DB20
        game_.sprite_tiles.free(sprite_.tiles.to);
        game_.sound().stop(0xC6);
    }

    void update(Game& g) override {
        if (riding_) {
            ride(g);
        } else {
            wait(g);
        }
    }

private:
    void wait(Game& g) {
        StageState& s = g.stage();
        sprite_.x = s16(x_ - s.camera.x);
        sprite_.y = s16(y_ - s.camera.y);
        bool started = false;
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if (p.flags & player_flag::dead) {
                continue;
            }
            // 080549C8: Knuckles (the first player) gliding or climbing.
            if (s.character == 2 && u8(s.player[0].state - 0x46) <= 5) {
                if (y_ * 0x100 < p.y) {
                    if (p.state != 0x49 &&
                        (collide_top_bottom(g, sprite_, x_, y_, p) & solid::from_below)) {
                        p.y = (y_ + 6) * 0x100;
                    }
                } else if (p.state != 0x48) {
                    collide_top_bottom(g, sprite_, x_, y_, p);
                }
                continue;
            }
            if (!(collide_top_bottom(g, sprite_, x_, y_, p) & solid::on_top)) {
                continue;
            }
            p.flags = (p.flags & 0xFFFFFEE3u) | player_flag::in_air;
            set_player_radius(g, p, 6, 0xE);
            p.y = y_ * 0x100 - 0x2C00;
            riders_ |= u8(1u << n);
            speed_x_ = speed_y_ = offset_x_ = offset_y_ = 0;
            started = true;
            if (g.game_mode >= 2) {
                throw Unrecovered("a sled in multiplayer (0808C628)");
            }
        }
        if (started) {
            riding_ = true;
        }
        if (!near_view(sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        draw(g, sprite_);
    }

    // 0808C970
    void ride(Game& g) {
        StageState& s = g.stage();
        const Assets& a = g.assets();
        offset_y_ += speed_y_;
        // (Across, the picture keeps last frame's place until the end.)
        sprite_.y = s16(y_ + (offset_y_ >> 8) - s.camera.y);
        if (out_of_range(s, x_, y_, sprite_.x, sprite_.y)) {
            g.sound().stop(0xC6);
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        u8 layer = 0;
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if (p.flags & player_flag::dead) {
                continue;
            }
            if (n == 0) {
                layer = p.layer;
            }
            if (riders_ & (1u << n)) {
                const bool curled = p.flags & (player_flag::spin_dash | player_flag::rolling);
                p.y = y_ * 0x100 + offset_y_ - (curled ? 0x2A00 : 0x2E00);
            }
        }
        if (!airborne_) {
            u8 angle = 0;
            const s32 d = probe(a, s.map, y_ + (offset_y_ >> 8), x_ + (offset_x_ >> 8), layer, 8,
                                &angle, Sensor::floor);
            if (d < 6) {
                const s16 slope = sine(a, angle * 4u + 0x100);
                for (unsigned n = 0; n < s.players; ++n) {
                    Player& p = s.player[n];
                    if (!(p.flags & player_flag::dead) && (riders_ & (1u << n))) {
                        const s32 push = divide(slope, 0x17B6);
                        p.x += leftward_ ? push : -push;
                    }
                }
                const s32 accel = divide(slope, 0x5A0);
                speed_x_ += leftward_ ? -accel : accel;
                offset_y_ += (d + 3) * 0x100;
            } else {
                // Off a ledge.
                g.sound().stop(0xC6);
                if ((s.zone >> 1) == 3) {
                    speed_y_ += std::abs(divide(speed_x_ * 2, 5));
                } else {
                    speed_y_ -= std::abs(divide(speed_x_ * 3, 5));
                }
                airborne_ = true;
            }
        } else {
            const s32 d = probe_short(a, s.map, y_ + (offset_y_ >> 8), x_ + (offset_x_ >> 8), layer,
                                      8, nullptr, Sensor::floor);
            if (d >= 0) {
                speed_y_ += 0x20;
            } else {
                speed_y_ = 0;
                airborne_ = false;
                if (landings_ == 0) {
                    throw_riders(g, s);
                    burst(g, s);
                    return;
                }
                g.sound().start(0xC6);
                --landings_;
            }
        }
        const s16 x = s16(x_ + (offset_x_ >> 8)), y = s16(y_ + (offset_y_ >> 8));
        u8 unused = 0;
        if (probe_short(a, s.map, x + 0x12, y - 0x1E, layer, 8, &unused, Sensor::wall) < 0) {
            throw_riders(g, s);
            burst(g, s);
            return;
        }
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if (p.flags & player_flag::dead) {
                continue;
            }
            const u8 bit = u8(1u << n);
            if (!(riders_ & bit)) {
                if (collide_platform(g, sprite_, x, y, p) & solid::on_top) {
                    p.y += 0x100;
                    riders_ |= bit;
                    end_player_moves(g, p);
                    p.flags = (p.flags & 0xFFFFFEE9u) | player_flag::on_object;
                    p.state = 4;
                    set_player_radius(g, p, 6, 0xE);
                }
            } else if (p.flags & player_flag::in_air) {
                riders_ &= u8(~bit);
            } else {
                carry(g, s, p, bit, x, y);
            }
        }
        offset_x_ += speed_x_ * 2;
        sprite_.x = s16(x_ + (offset_x_ >> 8) - s.camera.x);
        animate(g, sprite_);
        draw(g, sprite_);
    }

    // A rider on the ground goes along, unless a wall or leaving the sled
    // takes the player off.
    void carry(Game& g, StageState& s, Player& p, u8 bit, s16 x, s16 y) {
        const Assets& a = g.assets();
        const auto drop = [&] {
            p.flags = (p.flags & ~player_flag::on_object) | player_flag::in_air;
            riders_ &= u8(~bit);
        };
        p.flags &= 0xFFFFFEEDu;
        s32 d = probe_short(a, s.map, p.x >> 8, p.y >> 8, p.layer, 8, nullptr, Sensor::wall);
        if (d < 0) {
            drop();
            p.x += d * 0x100;
        }
        d = probe_short(a, s.map, p.x >> 8, p.y >> 8, p.layer, -8, nullptr, Sensor::wall);
        if (d < 0) {
            drop();
            p.x -= d * 0x100;
        }
        if (!touches_player(sprite_, x, s16(y - 0xC), p)) {
            end_player_moves(g, p);
            drop();
            return;
        }
        p.x += speed_x_ * 2;
        const auto knocked_off = [&](s32 shift) {
            end_player_moves(g, p);
            p.flags = (p.flags & ~player_flag::on_object) | player_flag::in_air;
            p.state = p.character == 3 ? 0x55 : 5;
            p.standing_on = nullptr;
            riders_ &= u8(~bit);
            p.x += shift;
            p.speed_x = 0;
        };
        d = probe(a, s.map, (p.x + speed_x_ * 2) >> 8, p.y >> 8, p.layer, 8, nullptr, Sensor::wall);
        if (d < 0) {
            knocked_off(d * 0x100);
        } else {
            d = probe(a, s.map, (p.x + speed_x_ * 2) >> 8, p.y >> 8, p.layer, -8, nullptr,
                      Sensor::wall);
            if (d < 0) {
                knocked_off(-d * 0x100);
            }
        }
        const bool curled = p.flags & (player_flag::spin_dash | player_flag::rolling);
        sprite_.y = s16((p.y >> 8) - s.camera.y + (curled ? 0x2A : 0x2E));
    }

    void throw_riders(Game& g, StageState& s) {
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            const u8 bit = u8(1u << n);
            if ((p.flags & player_flag::dead) || !(riders_ & bit)) {
                continue;
            }
            p.speed_y = -0x300;
            p.speed_x = p.ground_speed = s16(speed_x_);
            end_player_moves(g, p);
            p.flags = (p.flags & ~player_flag::on_object) | player_flag::in_air;
            p.standing_on = nullptr;
            p.state = p.character == 3 ? 0x55 : 5;
            riders_ &= u8(~bit);
        }
    }

    void burst(Game& g, StageState& s) {
        g.sound().stop(0xC6);
        g.scene.focus_x = sprite_.x;
        g.scene.focus_y = sprite_.y;
        const bool ice = (s.zone >> 1) == 3;
        create_block_debris(g, s16(x_ + (offset_x_ >> 8) - g.scene.focus_x),
                            s16(y_ + (offset_y_ >> 8) - (g.scene.focus_y + 0x14)),
                            {u16(ice ? 0x22A : 0x225), 1, u16(ice ? 0x229 : 0x224),
                             ice ? 0x40u : 0x10u, 0x3D, 8, 0xC0, 0x6C, 0x8F});
        give_back();
        g.scheduler().destroy(*this);
    }

    Game& game_;
    Sprite sprite_;   // +0C
    s32 speed_x_{};   // +A0 8.8 (moved twice a frame)
    s32 speed_y_{};   // +A4
    s32 offset_x_{};  // +A8
    s32 offset_y_{};  // +AC
    bool airborne_{}; // +B0
    u8 landings_;     // +B1 landings left
    u8 riders_{};     // +B2 a bit per player riding
    bool leftward_;   // +B3
    bool riding_{};   // the update is 0808C970
};

} // namespace

bool create_ice_paradise_object(Game& g, u32 creator, const ObjectSpawn& o) {
    switch (creator) {
    case 0x08090B0C:
        g.scheduler().create<BubbleVent>(g, o);
        return true;
    case 0x0808E0A0:
        g.scheduler().create<RisingBlock>(g, o);
        return true;
    case 0x0808DB58:
        g.scheduler().create<SinkingPlatform>(g, o);
        return true;
    case 0x0808AF78:
        g.scheduler().create<SpringPlatform>(g, o);
        return true;
    case 0x08091408:
        g.scheduler().create<BreakableIce>(g, o);
        return true;
    case 0x08092308:
        g.scheduler().create<SteamVent>(g, o);
        return true;
    case 0x0808C4BC:
        g.scheduler().create<Sled>(g, o);
        return true;
    default:
        return false;
    }
}

bool create_ice_paradise_enemy(Game& g, u32 creator, const ObjectSpawn& o) {
    switch (creator) {
    case 0x080714C4:
        g.scheduler().create<UpShooter>(g, o);
        return true;
    case 0x08071C4C:
        g.scheduler().create<WallClimber>(g, o);
        return true;
    case 0x080727D8:
        g.scheduler().create<FrozenDiver>(g, o);
        return true;
    case 0x080731EC:
        g.scheduler().create<Flasher>(g, o);
        return true;
    case 0x080736A0:
        // Not made in Ice Paradise while the save's byte +18 is set.
        if ((g.stage().zone >> 1) != 3 || g.save().unknown_5178 == 0) {
            g.scheduler().create<SnowballThrower>(g, o);
        }
        return true;
    default:
        return false;
    }
}

} // namespace sonic_advance_gba
