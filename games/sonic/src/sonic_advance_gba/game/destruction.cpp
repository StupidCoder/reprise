#include "sonic_advance_gba/game/destruction.hpp"
#include "sonic_advance_gba/game/collision.hpp"
#include "sonic_advance_gba/game/game.hpp"
#include "sonic_advance_gba/game/math.hpp"
#include "sonic_advance_gba/game/sprite.hpp"
#include "sonic_advance_gba/game/stage.hpp"

namespace sonic_advance_gba {

namespace {

constexpr u32 animals = 0x08681154;      // per zone 3 x 16 bytes: tile, animation, variant, kind
constexpr u32 enemy_points = 0x080B82BC; // by chain position
constexpr u16 sound_enemy = 0x8A;

// 0801FF40: the explosion stays where it happened.
class Explosion final : public Process {
public:
    Explosion(s16 x, s16 y) : Process(0x2000) {
        sprite_.start(0x11000, 0x197, 0, x, y, 0x3C0, 0x2000);
    }

    void update(Game& g) override {
        const auto& c = g.stage().camera;
        const s16 x = sprite_.x, y = sprite_.y;
        sprite_.x = s16(x - c.x);
        sprite_.y = s16(sprite_.y - c.y);
        if (animate(g, sprite_) == 0) {
            draw(g, sprite_);
            g.scheduler().destroy(*this);
        }
        draw(g, sprite_);
        sprite_.x = x;
        sprite_.y = y;
    }

private:
    Sprite sprite_;
};

// The freed animals (0x3C-0x40 bytes): position and speed in 1/256 pixels.
class Animal : public Process {
public:
    Animal(Game& g, u32 entry, s16 x, s16 y) : Process(0x2000), x_(x << 8), y_(y << 8) {
        const Assets& a = g.assets();
        sprite_.start(0x10000 + a.u32(entry) * 0x20, u16(a.u32(entry + 4)), u8(a.u32(entry + 8)), x,
                      y, 0x440, 0x2000);
    }

protected:
    // Falls onto the ground below (layer 1); true when it landed.
    bool land(Game& g) {
        const StageState& s = g.stage();
        const s32 y = y_ + s32(probe_short(g.assets(), s.map, y_ >> 8, x_ >> 8, 1, 8, nullptr,
                                           Sensor::floor)) *
                               0x100;
        if (y <= y_) {
            y_ = y;
            return true;
        }
        return false;
    }

    // Drawn while near the view, else gone.
    void show(Game& g) {
        const auto& c = g.stage().camera;
        const s32 sx = (x_ >> 8) - c.x;
        const s32 sy = (y_ >> 8) - c.y;
        sprite_.x = s16(sx);
        sprite_.y = s16(sy);
        if (u16(sx + 0x20) <= 0x130 && sprite_.y + 0x20 >= 0 && s16(sy) <= 0xC0) {
            animate(g, sprite_);
            draw(g, sprite_);
        } else {
            g.scheduler().destroy(*this);
        }
    }

    Sprite sprite_; // +00
    s32 x_, y_;     // +30, +34
};

// 08054858: hops up once and stays on the ground.
class SittingAnimal final : public Animal {
public:
    SittingAnimal(Game& g, u32 entry, s16 x, s16 y) : Animal(g, entry, x, y) {}

    void update(Game& g) override {
        speed_y_ = s16(speed_y_ + 0x30);
        y_ += speed_y_;
        x_ += speed_x_;
        if (delay_ == 0) {
            if (land(g)) {
                speed_y_ = 0;
            }
        } else {
            --delay_;
        }
        show(g);
    }

private:
    s16 speed_x_{};        // +38
    s16 speed_y_ = -0x400; // +3A
    u8 delay_ = 0x2A;      // +3C frames before it can land
};

// 080546C4: bounces away to the left.
class BouncingAnimal final : public Animal {
public:
    BouncingAnimal(Game& g, u32 entry, s16 x, s16 y) : Animal(g, entry, x, y) {}

    void update(Game& g) override {
        speed_y_ = s16(speed_y_ + 0x30);
        y_ += speed_y_;
        x_ += speed_x_;
        if (delay_ == 0) {
            if (land(g)) {
                speed_y_ = -0x400;
                if (!landed_) {
                    speed_x_ = -0x200;
                }
                landed_ = true;
            }
        } else {
            --delay_;
        }
        show(g);
    }

private:
    s16 speed_x_ = 1;      // +38
    s16 speed_y_ = -0x400; // +3A
    bool landed_ = false;  // +3C
    u8 delay_ = 0x2A;      // +3D
};

// 0805450C: flies along a swinging arc (its direction turning between
// 200h and 280h).
class FlyingAnimal final : public Animal {
public:
    FlyingAnimal(Game& g, u32 entry, s16 x, s16 y) : Animal(g, entry, x, y) {}

    void update(Game& g) override {
        const Assets& a = g.assets();
        if (!turning_back_) {
            direction_ = u16(direction_ - 6);
            if (s16(direction_) <= 0x1FF) {
                turning_back_ = true;
            }
        } else {
            direction_ = u16(direction_ + 6);
            if (s16(direction_) > 0x280) {
                turning_back_ = false;
            }
        }
        x_ += divide(sine(a, (direction_ & 0x3FF) + 0x100), 0x14);
        y_ += divide(sine(a, direction_ & 0x3FF), 0x14);
        show(g);
    }

private:
    u16 direction_ = 0x240;    // +38
    bool turning_back_ = true; // +3A
};

// 08012790, 080127FC: the points rise for a second, then stay half a
// second. Its tiles are freed with it (08012860).
class PointsPopup final : public Process {
public:
    PointsPopup(Game& g, s16 x, s16 y, u8 chain) : Process(0x2000), game_(g) {
        sprite_.start(g.sprite_tiles.allocate(2), 0x2C5, chain, x, y, 0, 0x2000);
        animate(g, sprite_);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); }

    void update(Game& g) override {
        const auto& c = g.stage().camera;
        const s16 x = sprite_.x, y = sprite_.y;
        if (!resting_) {
            const s16 rise = ++clock_;
            sprite_.x = s16(x - c.x);
            sprite_.y = s16(y - (c.y + rise));
            draw(g, sprite_);
            sprite_.x = x;
            sprite_.y = y;
            if (rise >= 0x3C) {
                resting_ = true;
                sprite_.y = s16(y - clock_);
                clock_ = 0;
            }
            return;
        }
        if (++clock_ < 0x1F) {
            sprite_.x = s16(x - c.x);
            sprite_.y = s16(y - c.y);
            draw(g, sprite_);
            sprite_.x = x;
            sprite_.y = y;
        } else {
            g.scheduler().destroy(*this);
        }
    }

private:
    Game& game_;
    Sprite sprite_; // +00
    s16 clock_{};   // +30
    bool resting_{};
};

} // namespace

void create_explosion(Game& g, s16 x, s16 y) {
    g.scheduler().create<Explosion>(x, y);
}

void free_animal(Game& g, s16 x, s16 y) {
    StageState& s = g.stage();
    const u32 entry = animals + u32((s.zone >> 1) * 0x30) + s.unknown_501c * 0x10u;
    s.unknown_501c = u8(s.unknown_501c + 1);
    if (s.unknown_501c > 2) {
        s.unknown_501c = 0;
    }
    const u8 kind = g.assets().u8(entry + 12);
    if (kind == 0) {
        g.scheduler().create<SittingAnimal>(g, entry, x, y);
    } else if ((kind & 1) == 0) {
        g.scheduler().create<FlyingAnimal>(g, entry, x, y);
    } else {
        g.scheduler().create<BouncingAnimal>(g, entry, x, y);
    }
}

void award_enemy_points(Game& g, s16 x, s16 y) {
    StageState& s = g.stage();
    Player& p = s.player[0];
    g.sound().start(sound_enemy);
    const u32 before = s.score;
    s.score += g.assets().u16(enemy_points + p.chain * 2u);
    if (divide(s32(s.score), 50000) != divide(s32(before), 50000) && g.game_mode == 0) {
        ++s.lives;
        s.music_one_up = 0x10;
    }
    g.scheduler().create<PointsPopup>(g, x, y, p.chain);
    if (s8(p.chain) < 4) {
        ++p.chain;
    }
}

} // namespace sonic_advance_gba
