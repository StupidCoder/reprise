#pragma once
// The parts the bosses share (boss.cpp, x_zone.cpp): their effects (smoke,
// explosion pieces, debris), the hit flash, the view's limits moving, and
// Eggman's escape after a zone's second act.
#include "sonic_advance_gba/game/collision.hpp"
#include "sonic_advance_gba/game/game.hpp"
#include "sonic_advance_gba/game/math.hpp"
#include "sonic_advance_gba/game/player.hpp"
#include "sonic_advance_gba/game/stage.hpp"

namespace sonic_advance_gba::boss_parts {

// The explosions' pieces (8 kinds): tile counts, animations, variants.
constexpr u32 piece_tiles = 0x080B843C;      // bytes
constexpr u32 piece_animations = 0x080B8424; // halfwords
constexpr u32 piece_variants = 0x080B8434;   // bytes
// Per zone, the animations of the two animals hopping out of the capsule
// (3 halfwords; the third is not used here).
constexpr u32 capsule_animals = 0x080B8444;

// Fixed sprite tile places: the smoke puffs' tiles (their animation loads
// them) and, after the boss is gone, the place its tiles had for Eggman's
// escape.
constexpr u32 smoke_tiles = 0x11000;
constexpr u32 escape_ship_tiles = 0x12580, escape_eggman_tiles = 0x12E40;

constexpr u16 sound_swing_lands = 0x91;
constexpr u16 sound_boss_hit = 0x8F;
constexpr u16 sound_boss_broken = 0x90;
constexpr u16 sound_capsule_lands = 0x88;
constexpr u16 sound_capsule_opens = 0x89;

// A floor below (x, y) on layer 1 within one tile (080401E8): the distance,
// negative inside it.
inline s32 floor_distance(Game& g, s32 x, s32 y) {
    return probe_short(g.assets(), g.stage().map, y, x, 1, 8, nullptr, Sensor::floor);
}

// The hit flash: the boss's sprites (marked as the sprite window's) show
// white through a brightness effect; off again unless the player is dead.
inline void flash_on(Game& g) {
    gba::Settings& v = g.video().settings;
    v.sprite_window = true;
    v.outside = 0x1F;
    v.inside_sprite_window = 0x3F;
    v.blend.set_control(0xBF);
    v.blend.evy = 0x10;
}

inline void flash_off(Game& g) {
    if (g.stage().player[0].flags & player_flag::dead) {
        return;
    }
    gba::Settings& v = g.video().settings;
    v.sprite_window = false;
    v.outside = 0;
    v.inside_sprite_window = 0;
    v.blend.set_control(0);
    v.blend.evy = 0;
}

// 08017490, 08017280: the camera's top and bottom limits move one pixel a
// frame from around the view to new values.
class CameraBounds final : public Process {
public:
    CameraBounds(Game& g, s16 min_y, s16 max_y) : Process(0x2000), min_y_(min_y), max_y_(max_y) {
        auto& c = g.stage().camera;
        c.min_y = c.y;
        c.max_y = s16(c.y + 0xA0);
        if (c.max_y < max_y_) {
            c.max_y = s16(c.y + 0x140);
        }
    }

    void update(Game& g) override {
        auto& c = g.stage().camera;
        if (c.min_y < min_y_) {
            ++c.min_y;
        } else if (c.min_y > min_y_) {
            --c.min_y;
        }
        if (c.max_y < max_y_) {
            ++c.max_y;
        } else if (c.max_y > max_y_) {
            --c.max_y;
        }
        if (c.min_y == min_y_ && c.max_y == max_y_) {
            g.scheduler().destroy(*this);
        }
    }

private:
    s16 min_y_, max_y_; // +00, +02
};

// 08017650, 08017704: a piece flying with a speed and an acceleration
// (8.8 pixels), gone off screen or once its animation ended and its time
// ran out.
class Particle final : public Process {
public:
    Particle(Game& g, u32 tiles, u16 animation, u8 variant, bool own_tiles)
        : Process(0x2000), game_(g), own_tiles_(own_tiles) {
        sprite.start(tiles, animation, variant, 0, 0, 0, 0);
    }

    // 080177A0
    void on_destroy() override {
        if (own_tiles_) {
            game_.sprite_tiles.free(sprite.tiles.to);
        }
    }

    void update(Game& g) override {
        const auto& c = g.stage().camera;
        speed_x = s16(speed_x + accel_x);
        speed_y = s16(speed_y + accel_y);
        x += speed_x;
        y += speed_y;
        sprite.x = s16((x >> 8) - c.x);
        sprite.y = s16((y >> 8) - c.y);
        if (timer != 0) {
            --timer;
        }
        if (u16(sprite.x + 0x20) < 0x131 && sprite.y - 0x20 < 0xA1 &&
            (animate(g, sprite) != 0 || timer != 0)) {
            draw(g, sprite);
        } else {
            g.scheduler().destroy(*this);
        }
    }

    Sprite sprite;            // +00
    s32 x{}, y{};             // +30, +34
    s16 speed_x{}, speed_y{}; // +38, +3A
    s16 accel_x{}, accel_y{}; // +3C, +3E
    s16 timer{};              // +40

private:
    Game& game_;
    bool own_tiles_;
};

// 080174F4: a smoke puff at (x, y) (8.8), randomly mirrored.
inline Particle& create_smoke(Game& g, s32 x, s32 y) {
    Particle& p = g.scheduler().create<Particle>(g, smoke_tiles, 0x263, 0, false);
    p.x = x;
    p.y = y;
    p.sprite.layer = 0x500;
    p.sprite.flags = (g.stage().next_random() & 0xC00) | 0x2000;
    return p;
}

// The broken boss's explosions (in 08026C14 and 08026EA0; 0802EA5C and
// 0802ECCC with no lift; 08031450 and 080316DC falling at 5 instead of
// 28h): a random piece thrown up from `lift` above (x, y), and smoke nearby.
inline void create_explosion_piece(Game& g, s16 x, s16 y, s16 lift = 0x20, s16 gravity = 0x28,
                                   u16 layer = 0x440, s16 smoke_lift = 0x7FFF) {
    if (smoke_lift == 0x7FFF) {
        smoke_lift = lift;
    }
    StageState& s = g.stage();
    const Assets& a = g.assets();
    const u32 kind = s.next_random() & 7;
    const u32 tiles = g.sprite_tiles.allocate(a.u8(piece_tiles + kind));
    Particle& p = g.scheduler().create<Particle>(g, tiles, a.u16(piece_animations + kind * 2),
                                                 a.u8(piece_variants + kind), true);
    p.x = s32(x) << 8;
    p.y = (y - lift) * 0x100;
    p.sprite.flags = 0x2000;
    p.sprite.layer = layer;
    p.accel_y = gravity;
    p.timer = 0x20;
    const u32 direction = s.next_random() & 0x1FF;
    p.speed_y = s16((sine(a, direction) * -0x600) >> 14);
    p.speed_x = s16((sine(a, direction + 0x100) * -0x600) >> 14);
    const u32 r = s.next_random();
    create_smoke(g, (x + s32(r & 0x3F) - 0x20) * 0x100,
                 (y - (s32((r & 0x3F0000) >> 16) + smoke_lift)) * 0x100);
}

// 080168A4, 080169C8: a piece thrown off (pixels per frame in the speeds'
// high bytes), turning by `spin` and scaling by `growth` each frame, for
// 3Ch frames.
class Debris final : public Process {
public:
    Debris(Game& g, s16 x, s16 y, u32 tiles, u16 animation, u8 variant)
        : Process(0x2000), game_(g), x_(x), y_(y) {
        t.x = x;
        t.y = y;
        t.sprite.start(g.sprite_tiles.allocate(tiles), animation, variant, 0, 0, 0x500, 0x70);
        animate(g, t.sprite);
    }

    // 08017578
    void on_destroy() override { game_.sprite_tiles.free(t.sprite.tiles.to); }

    void update(Game& g) override {
        StageState& s = g.stage();
        if (--timer == -1) {
            g.scheduler().destroy(*this);
            return;
        }
        speed_y = s16(speed_y + 0x28);
        x_ = s16(x_ + s8(u16(speed_x) >> 8));
        y_ = s16(y_ + s8(u16(speed_y) >> 8));
        t.x = s16(x_ - s.camera.x);
        t.y = s16(y_ - s.camera.y);
        if (speed_x < 1) {
            t.angle = u16((t.angle - spin) & 0x3FF);
            if (t.scale_x < 0x200) {
                t.scale_x = s16(t.scale_x + growth);
            }
        } else {
            t.angle = u16((t.angle + spin) & 0x3FF);
            if (t.scale_x > -0x200) {
                t.scale_x = s16(t.scale_x - growth);
            }
        }
        t.scale_y = t.scale_x < 0 ? s16(-t.scale_x) : t.scale_x;
        Sprite& sp = t.sprite;
        if (sp.flags & sprite_flag::rotated) {
            sp.flags = (sp.flags & ~sprite_flag::rotation_set) | s.next_rotation_set++;
            transform(g, t);
        } else {
            sp.x = t.x;
            sp.y = t.y;
        }
        draw(g, sp);
    }

    TransformedSprite t;  // +00 sprite, +30 transform
    s16 timer = 0x3C;     // +40
    s16 spin{};           // +42
    s16 speed_x = 0x100;  // +44
    s16 speed_y = -0x200; // +46
    s16 growth = 8;       // +48

private:
    Game& game_;
    s16 x_, y_; // +3C, +3E
};

// 08015C10: Eggman flies off in his ship from (x, y): a pause, up, a look
// round, then away to the right. Drawn with the tiles the boss left.
class EggmanEscape final : public Process {
public:
    EggmanEscape(s16 x, s16 y) : Process(0x2000), x_(x), y_(y) {
        ship_.start(escape_ship_tiles, 0x2B5, 0, x, y, 0x540, 0x2000);
        eggman_.start(escape_eggman_tiles, 0x2B6, 4, x, y, 0x4C0, 0x2000);
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        switch (state_) {
        case State::rising: // 08015D3C
            place(s, s16(x_), s8(u16(rise_) >> 8));
            show(g);
            if (timer_ < 0x2E) {
                ++timer_;
            } else {
                rise_ = s16(rise_ - 0x100);
                if (rise_ < -0x4FFF) {
                    state_ = State::looking;
                    ship_.variant = 1;
                    eggman_.animation = 0x2B6;
                    eggman_.variant = 9;
                    rise_ = s8(u16(rise_) >> 8);
                }
            }
            break;
        case State::looking: { // 08015E20
            place(s, s16(x_), rise_);
            const u32 running = animate(g, ship_);
            draw(g, ship_);
            animate(g, eggman_);
            draw(g, eggman_);
            if (running == 0) {
                state_ = State::leaving;
                ship_.animation = 0x2B5;
                ship_.variant = 0;
                ship_.flags |= sprite_flag::flip_x;
                eggman_.flags |= sprite_flag::flip_x;
                eggman_.animation = 0x2B6;
                eggman_.variant = 0;
                if (s.zone == 12 && x_ > 0x7FF) {
                    eggman_.variant = 8;
                }
            }
            break;
        }
        case State::leaving: // 08015F14
            away_ = s16(away_ + 4);
            place(s, s16(x_ + away_), rise_);
            if (ship_.x > 360) {
                g.scheduler().destroy(*this);
                return;
            }
            show(g);
            break;
        }
    }

private:
    enum class State : u8 { rising, looking, leaving };

    void place(const StageState& s, s16 x, s16 rise) {
        ship_.x = s16(x - s.camera.x);
        ship_.y = s16((rise + y_) - s.camera.y);
        eggman_.x = ship_.x;
        eggman_.y = ship_.y;
    }

    void show(Game& g) {
        animate(g, ship_);
        draw(g, ship_);
        animate(g, eggman_);
        draw(g, eggman_);
    }

    Sprite ship_;   // +00
    Sprite eggman_; // +30
    s16 x_, y_;     // +60, +62
    s16 away_{};    // +64
    s16 rise_{};    // +66 8.8, then pixels
    u16 timer_{};   // +68
    State state_ = State::rising;
};

// 08017170, 080171F8: as CameraBounds, also moving the left limit one
// pixel a frame (the right one is set at once).
class CameraBoundsXY final : public Process {
public:
    CameraBoundsXY(Game& g, s16 min_y, s16 max_y, s16 min_x, s16 max_x)
        : Process(0x2000), min_y_(min_y), max_y_(max_y), min_x_(min_x), max_x_(max_x) {
        auto& c = g.stage().camera;
        c.min_y = c.y;
        c.max_y = s16(c.y + 0xA0);
        c.min_x = c.x;
        c.max_x = max_x;
        if (c.max_y < max_y_) {
            c.max_y = s16(c.y + 0x140);
        }
    }

    void update(Game& g) override {
        auto& c = g.stage().camera;
        const auto step = [](s16& v, s16 to) {
            if (v < to) {
                ++v;
            } else if (v > to) {
                --v;
            }
        };
        step(c.min_y, min_y_);
        step(c.max_y, max_y_);
        step(c.min_x, min_x_);
        if (c.min_y == min_y_ && c.max_y == max_y_ && c.min_x == min_x_ && c.max_x == max_x_) {
            g.scheduler().destroy(*this);
        }
    }

private:
    s16 min_y_, max_y_, min_x_, max_x_; // +00, +02, +04, +06
};

} // namespace sonic_advance_gba::boss_parts
