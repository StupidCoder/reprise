#include "sonic_advance_gba/game/player_effects.hpp"
#include "sonic_advance_gba/game/camera.hpp"
#include "sonic_advance_gba/game/collision.hpp"
#include "sonic_advance_gba/game/game.hpp"
#include "sonic_advance_gba/game/math.hpp"
#include "sonic_advance_gba/game/object_util.hpp"
#include "sonic_advance_gba/game/rings.hpp"
#include "sonic_advance_gba/game/stage.hpp"
#include <algorithm>

namespace sonic_advance_gba {

namespace {

// 08040610: does nothing; while it exists, dust may be made.
class DustBudget : public Process {
public:
    DustBudget() : Process(0x4000) {}

    void update(Game&) override {}

    void on_destroy() override {
        if (stage_) {
            stage_->dust_budget = nullptr; // 08040604
        }
    }

    StageState* stage_{};
};

// 0804053C: a dust cloud drifting by (dx, dy) until its animation ends.
class SkidDust : public Process {
public:
    SkidDust(s16 x, s16 y, s16 drift_x, u32 random) : Process(0x4001), x_(x), y_(y), dx_(drift_x) {
        sprite_.start(0x12100, 0x2EC, 0, 0, 0, 0x200, 0x2000 | (random & 0xC00));
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        if (!s.dust_budget || (sprite_.flags & sprite_flag::ended)) {
            ++s.dust_left;
            g.scheduler().destroy(*this);
            return;
        }
        sprite_.x = s16(x_ - s.camera.x);
        sprite_.y = s16(y_ - s.camera.y);
        animate(g, sprite_);
        draw(g, sprite_);
        x_ = s16(x_ + dx_);
        y_ = s16(y_ + dy_);
    }

private:
    s16 x_, y_, dx_{}, dy_{};
    Sprite sprite_;
};

// 08040698 / 08040788: the spin dash's dust at the player's feet (more of
// it above a charge of 200h), while the player charges. Its tiles are
// freed with it (08040878).
class SpinDashDust : public Process {
public:
    explicit SpinDashDust(Game& g) : Process(0x4001), game_(g) {
        sprite_.start(g.sprite_tiles.allocate(0x14), 0x2EA, 0, 0, 0, 0x200, 0x2000);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); }

    void update(Game& g) override {
        StageState& s = g.stage();
        const Player& p = s.player[0];
        if (!p.process ||
            (p.flags & (player_flag::dead | player_flag::spin_dash)) != player_flag::spin_dash) {
            g.scheduler().destroy(*this);
            return;
        }
        const bool more = p.spin_charge > 0x200;
        if (more != more_) {
            more_ = more;
            sprite_.animation = more ? 0x2EB : 0x2EA;
            sprite_.variant = 0;
            sprite_.previous_variant = 0xFF;
        }
        const bool upside_down = s.flags & stage_flag::upside_down;
        const s16 feet = upside_down ? s16(-p.radius_y) : s16(p.radius_y);
        sprite_.x = s16(s16(u32(p.x) >> 8) - s.camera.x);
        sprite_.y = s16(s16(s16(u32(p.y) >> 8) + feet) - s.camera.y);
        if (p.flags & player_flag::facing_left) {
            sprite_.flags &= ~sprite_flag::flip_x;
        } else {
            sprite_.flags |= sprite_flag::flip_x;
        }
        if (upside_down) {
            sprite_.flags |= sprite_flag::flip_y;
        } else {
            sprite_.flags &= ~sprite_flag::flip_y;
        }
        animate(g, sprite_);
        draw(g, sprite_);
    }

private:
    Game& game_;
    Sprite sprite_;
    bool more_ = false;
};

// 080403A8 + 08040244: a sprite effect until its animation ends, placed by
// its mode: bits 0-1 0: at an act position (bits 4-5: 10 on the water
// line, 20 following the player), 1: relative to the far background, 2: at
// a screen position; bit 6: mirrored as the player faces, bit 7: flipped
// with reversed gravity. Its tiles are freed with it (08040454).
class Effect : public Process {
public:
    Effect(Game& g, s32 x, s32 y, u16 mode, u8 player, u32 tiles, u16 animation, u8 variant,
           u16 layer, u32 flags)
        : Process(0x4001), game_(g), x_(x), y_(y), mode_(u8(mode)), player_(player) {
        sprite_.start(g.sprite_tiles.allocate(tiles), animation, variant, 0, 0, layer, flags);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); }

    Sprite& sprite() { return sprite_; }

    void update(Game& g) override {
        if (sprite_.flags & sprite_flag::ended) {
            g.scheduler().destroy(*this);
            return;
        }
        StageState& s = g.stage();
        switch (mode_ & 3) {
        case 0:
            if ((mode_ & 0x30) == 0x10) {
                y_ = s.water_level;
            } else if ((mode_ & 0x30) == 0x20) {
                if (g.game_mode >= 2) {
                    throw Unrecovered("player effects in multiplayer (08040244)");
                }
                const Player& p = s.player[player_];
                x_ = p.x >> 8;
                y_ = p.y >> 8;
            }
            sprite_.x = s16(x_ - s.camera.x);
            sprite_.y = s16(y_ - s.camera.y);
            break;
        case 1:
            sprite_.x = s16(x_ - s.camera.parallax_x);
            sprite_.y = s16(y_ - s.camera.parallax_y);
            break;
        case 2:
            sprite_.x = s16(x_);
            sprite_.y = s16(y_);
            break;
        default:
            break;
        }
        if (mode_ & 0x40) {
            if (s.player[0].flags & player_flag::facing_left) {
                sprite_.flags &= ~sprite_flag::flip_x;
            } else {
                sprite_.flags |= sprite_flag::flip_x;
            }
        }
        if (mode_ & 0x80) {
            if (s.flags & stage_flag::upside_down) {
                sprite_.flags |= sprite_flag::flip_y;
            } else {
                sprite_.flags &= ~sprite_flag::flip_y;
            }
        }
        animate(g, sprite_);
        draw(g, sprite_);
    }

private:
    Game& game_;
    s32 x_, y_;     // +00, +04
    u8 mode_;       // +14
    u8 player_;     // +16
    Sprite sprite_; // +18
};

// 0804B838 (shield, power bit 0), 0804B960 (magnetic shield, bit 3),
// 0804BB64 (invincibility, bit 1): drawn round the player every other
// frame pair while its power lasts (a shield is hidden while invincible);
// the invincibility scatters sparkles every eighth frame. Their tiles are
// freed with them (0804BE34).
class PowerEffect : public Process {
public:
    PowerEffect(Game& g, u8 player, u8 power, u16 animation)
        : Process(0x4001), game_(g), player_(player), power_(power) {
        sprite_.start(g.sprite_tiles.allocate(0x24), animation, 0, 0, 0, 0x200, 0x2000);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); }

    void update(Game& g) override {
        StageState& s = g.stage();
        if (g.game_mode >= 2) {
            throw Unrecovered("power effects in multiplayer (0804B838)");
        }
        const Player& p = s.player[player_];
        if (!(p.power & power_)) {
            g.scheduler().destroy(*this);
            return;
        }
        if (power_ != player_power::invincible && (p.power & player_power::invincible)) {
            return;
        }
        const s16 x = s16(s16(u32(p.x) >> 8) + p.unknown_50);
        const s16 y = s16(u32(p.y) >> 8);
        sprite_.x = s16(x - s.camera.x);
        sprite_.y = s16(y - s.camera.y);
        sprite_.flags = (sprite_.flags & ~sprite_flag::priority) |
                        (p.body->sprite.flags & sprite_flag::priority);
        if (power_ == player_power::invincible && (s.ticks & 7) == 0) {
            // A sparkle at a random distance (0-15) and direction.
            const u32 first = s.next_random();
            const s32 distance = (first >> 8) & 0xF;
            const unsigned angle = (s.next_random() >> 8) & 0xFF;
            const Assets& a = g.assets();
            const s16 sx = s16(x + ((distance * (sine(a, angle * 4 + 0x100) >> 6)) >> 8));
            const s16 sy = s16(((distance * (sine(a, angle * 4) >> 6)) >> 8) + y);
            g.scheduler().create<Effect>(g, sx, sy, 0, 0, 9, 0x2F0, 1, 0x200, 0x2000);
        }
        animate(g, sprite_);
        // Shields show on the frames the invincibility does not (and the
        // second player's on the others).
        const bool odd_pair = s.ticks & 2;
        const bool first_player = (player_ & 1) == 0;
        if (power_ == player_power::invincible ? odd_pair != first_player
                                               : odd_pair == first_player) {
            draw(g, sprite_);
        }
    }

private:
    Game& game_;
    u8 player_; // +30
    u8 power_;
    Sprite sprite_;
};

// 08041FAC (08041154; 08040D28 with reversed gravity): the rings the
// player lost: 48 at most, bouncing until their time runs out. Their
// shared sprite animates every frame.
class LostRings : public Process {
public:
    void bounce_off_ceilings() { flags_ |= 1; }

    explicit LostRings(u8 zone) : Process(0x2001), gravity_(zone == 0xD ? 7 : 0x12) {
        sprite_.start(0x11D00, 0x2C3, 0, 0, 0, 0x500, 0x2000);
        sprite_.speed = 0x20;
    }

    void on_destroy() override {
        if (stage_) {
            stage_->lost_rings = nullptr; // 08041FEC
        }
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        // (08040D28 with gravity reversed: the same with the floor and the
        // ceiling swapped and gravity pulling up.)
        const bool upside_down = s.flags & stage_flag::upside_down;
        const Assets& a = g.assets();
        auto& v = g.video();
        bool drawn = false;
        animate(g, sprite_);
        for (Ring& r : rings_) {
            if (r.timer == 0) {
                continue;
            }
            r.x += r.speed_x;
            r.y += r.speed_y;
            const s32 x = r.x >> 8, y = r.y >> 8;
            const s32 sx = x - s.camera.x, sy = y - s.camera.y;
            // In act 13 (08041154) the ground below rolls by: it is met at
            // its own position.
            const s32 gx = s.zone == 0xD && !upside_down ? moon_ground_x(s, x) : x;
            if (collected(g, s.player[0], r, x, y) ||
                (s.players == 2 && collected(g, s.player[1], r, x, y))) {
                create_ring_sparkle(g, s16(x), s16(y));
                const u16 before = s.rings++;
                if (s.zone != 0xD && divide(s.rings, 100) != divide(before, 100) &&
                    g.game_mode == 0) {
                    ++s.lives;
                    s.music_one_up = 0x10;
                }
                if (g.game_mode == 6 && s.rings > 0xFF) {
                    s.rings = 0xFF;
                }
                r.timer = 0;
                continue;
            }
            // Bouncing off the floor (and off ceilings, flag bit 0),
            // losing three quarters of the speed.
            const auto off_floor = [&] {
                if (r.speed_y > 0) {
                    const s32 d =
                        probe_short(a, s.map, y, gx, u8(r.layer), 8, nullptr, Sensor::floor);
                    if (d < 1) {
                        r.y += d * 0x100;
                        r.speed_y = s16((r.speed_y >> 2) - r.speed_y);
                    }
                }
            };
            const auto off_ceiling = [&] {
                if (r.speed_y < 0) {
                    const s32 d = probe_short(a, s.map, y - 0x10, gx, u8(r.layer), -8, nullptr,
                                              Sensor::floor);
                    if (d < 1) {
                        r.y -= d * 0x100;
                        r.speed_y = s16((r.speed_y >> 2) - r.speed_y);
                    }
                }
            };
            if (!upside_down) {
                off_floor();
                if (flags_ & 1) {
                    off_ceiling();
                }
                r.speed_y = s16(r.speed_y + gravity_);
            } else {
                off_ceiling();
                if (flags_ & 1) {
                    off_floor();
                }
                r.speed_y = s16(r.speed_y - gravity_);
            }
            // Drawn in view (blinking in the last half second): the first as
            // a sprite, the others as copies of its sprite table entry.
            if (u32(sx + 7) < 0xFF && sy > -8 && sy < 0xA8 &&
                (r.timer > 0x1F || (s.ticks & 2) == 0)) {
                if (drawn && sprite_.first_entry != 0xFF) {
                    const auto attr = v.queued_entries[sprite_.first_entry].attributes();
                    const u16 a0 =
                        u16((attr[0] & 0xFF00) + ((sy - a.s16(sprite_.frame + 10)) & 0xFF));
                    const u16 a1 =
                        u16((attr[1] & 0xFE00) + ((sx - a.s16(sprite_.frame + 8)) & 0x1FF));
                    gba::Sprite e;
                    e.set_attributes(a0, a1, attr[2]);
                    queue_sprite(v, (sprite_.layer & 0x7C0) >> 6, e);
                } else {
                    sprite_.first_entry = 0xFF;
                    sprite_.x = s16(sx);
                    sprite_.y = s16(sy);
                    draw(g, sprite_);
                    drawn = true;
                }
            }
            --r.timer;
        }
    }

    // 08040ADC: up to 48 rings (32 in multiplayer) fly out in pairs,
    // mirrored, at angles stepping round from 488h.
    void scatter(Game& g, s32 x, s32 y, u16 count) {
        if (count == 0) {
            return;
        }
        count = std::min<u16>(count, 0x30);
        if (g.game_mode >= 2 && count > 0x20) {
            count = 0x20;
        }
        g.sound().start(0x76);
        const Assets& a = g.assets();
        const StageState& s = g.stage();
        s32 angle = 0x488, vx = 0, vy = 0;
        unsigned made = 0;
        for (Ring& r : rings_) {
            if (r.timer != 0) {
                continue;
            }
            r.timer = 0x100;
            r.layer = s.player[0].layer;
            r.x = x << 8;
            r.y = y << 8;
            if (angle >= 0) {
                s32 shift = angle >> 8;
                if (shift > 5) {
                    shift = 9 - shift;
                }
                vx = sine(a, unsigned(angle & 0xFF) * 4) >> shift;
                vy = sine(a, unsigned(angle & 0xFF) * 4 + 0x100) >> shift;
                vx -= vx >> 2;
                vy -= vy >> 2;
                angle = (angle + 0x10) | 0x80;
            }
            r.speed_x = s16(vx);
            r.speed_y = (s.flags & stage_flag::upside_down) ? s16(-vy) : s16(vy);
            angle = -angle;
            vx = -vx;
            if (++made >= count) {
                return;
            }
        }
    }

    // 08040BE8: rings thrown out in random directions (no sound; the last
    // boss's broken claw).
    void spread(Game& g, s32 x, s32 y, s32 count) {
        if (count == 0) {
            return;
        }
        count = std::min<s32>(count, 0x30);
        if (g.game_mode >= 2 && count > 0x20) {
            count = 0x20;
        }
        const Assets& a = g.assets();
        StageState& s = g.stage();
        u32 direction = ((s.next_random() & 0xFF000) >> 12) | 0x10;
        s32 made = 0;
        for (Ring& r : rings_) {
            if (r.timer != 0) {
                continue;
            }
            r.timer = 0x100;
            r.layer = s.player[0].layer;
            r.x = x << 8;
            r.y = y << 8;
            const s32 shift = s32((s.next_random() & 0x1000) >> 12) + 5;
            const s32 vx = sine(a, (direction & 0xFF) * 4) >> shift;
            const s32 vy = sine(a, (direction & 0xFF) * 4 + 0x100) >> shift;
            r.speed_x = s16(vx - (vx >> 2));
            const s16 down = s16(vy - (vy >> 2));
            r.speed_y = (s.flags & stage_flag::upside_down) ? s16(-down) : down;
            direction += 0x10 + ((s.next_random() & 0x3F000) >> 12);
            if (++made >= count) {
                return;
            }
        }
    }

    StageState* stage_{};

private:
    struct Ring {
        s32 x{}, y{};
        s16 speed_x{}, speed_y{};
        u16 timer{}; // frames left
        u16 layer{}; // the collision layer
    };

    // A player may take a ring once it is a little older (and not while
    // blinking from a hit, being dead or in state 28h).
    bool collected(Game&, const Player& p, const Ring& r, s32 x, s32 y) const {
        if (r.timer > lifetime_ || (p.state == 0x0F && p.hurt_timer != 0) || p.state == 0x28 ||
            (p.flags & player_flag::dead)) {
            return false;
        }
        const Hitbox& h = p.body->sprite.hitboxes[0];
        const s32 px = p.x >> 8, py = p.y >> 8;
        return spans_meet(px + h.left, px + h.right, x - 8, x + 8) &&
               spans_meet(py + h.top, py + h.bottom, y - 0x10, y);
    }

    Sprite sprite_;
    std::array<Ring, 48> rings_{};
    s32 gravity_;         // +330
    u16 lifetime_ = 0xE0; // +334 rings younger than this cannot be taken yet (timer above it)
    u16 flags_{};         // +336 bit 0: they bounce off ceilings
};

// 08040910
class RideHandle final : public Process {
public:
    explicit RideHandle(Game& g) : Process(0x2001), game_(g) {
        sprite_.start(g.sprite_tiles.allocate(0x10), 0x2ED, 0, 0, 0, 0x200, 0x2000);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 080409B0

    void update(Game& g) override {
        const StageState& s = g.stage();
        const Player& p = s.player[0];
        if (!(p.flags & player_flag::riding)) {
            g.scheduler().destroy(*this);
            return;
        }
        if (p.animation == 0x2C && p.variant == 1) {
            sprite_.x = s16((p.x >> 8) - s.camera.x);
            sprite_.y = s16(p.radius_y + (p.y >> 8) - s.camera.y);
            if (!(p.flags & player_flag::facing_left)) {
                sprite_.flags |= sprite_flag::flip_x;
            } else {
                sprite_.flags &= ~sprite_flag::flip_x;
            }
            animate(g, sprite_);
            draw(g, sprite_);
        }
    }

private:
    Game& game_;
    Sprite sprite_;
};

// 08047DE4, 08047CD0: Tails' tails while he spins down a chute (state
// 20h, in a pipe): drawn turned by the tails' angle (+81) and scaled as he
// is. It ends at once for anyone else, or when he is out (its tiles freed,
// 08040454).
class ChuteTails final : public Process {
public:
    ChuteTails(Game& g, u8 player) : Process(0x4001), game_(g), player_(player) {
        sprite_.start(g.sprite_tiles.allocate(0x40), 0x8A, 1, 0, 0, 0x400, 0x2000);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); }

    void update(Game& g) override {
        StageState& s = g.stage();
        const Player& p = s.player[player_];
        if (g.game_mode > 1 || p.character != 1 || p.state != 0x20 || p.tube_stage == 0x7F) {
            g.scheduler().destroy(*this);
            return;
        }
        const s16 x = s16((p.x >> 8) - s.camera.x + u16(p.unknown_50));
        const s16 y = s16((p.y >> 8) - s.camera.y);
        sprite_.flags = (sprite_.flags & ~sprite_flag::priority) |
                        (p.extra->sprite.flags & sprite_flag::priority);
        sprite_.flags &= ~0x1Fu;
        sprite_.flags |= u8(s.next_rotation_set++ | 0x20);
        animate(g, sprite_);
        transform(g, sprite_, u16(p.extra_81 << 2), p.scale_x, p.scale_y, x, y);
        draw(g, sprite_);
    }

private:
    Game& game_;
    u8 player_;     // +16
    Sprite sprite_; // +18
};

// 0804BE48: a number of the air countdown (from 0804BF3C): it rises from
// the player's head (in view coordinates) for 64 frames, growing, and lasts
// 129 frames.
class AirCountdown final : public Process {
public:
    AirCountdown(Game& g, const Player& p, u8 n) : Process(0x4001), game_(g) {
        const auto& c = g.stage().camera;
        x_ = p.x - c.x * 0x100;
        y_ = p.y - c.y * 0x100;
        t_.sprite.start(g.sprite_tiles.allocate(4), 0x30D, u8(5 - n), 0, 0, 0x240, 0x2000);
        t_.scale_x = t_.scale_y = 0;
    }

    void on_destroy() override { game_.sprite_tiles.free(t_.sprite.tiles.to); } // 08040454

    void update(Game& g) override {
        t_.x = s16(x_ >> 8);
        t_.y = s16(y_ >> 8);
        const s16 scale = s16(std::min((timer_ + 1) * 8, 0x100));
        t_.scale_x = t_.scale_y = scale;
        if (u16(t_.x + 0x20) >= 0x131 || u16(t_.y + 0x20) >= 0xE1 || timer_ > 0x80) {
            g.scheduler().destroy(*this);
            return;
        }
        if (timer_ <= 0x40) {
            y_ += (-(sine(g.assets(), timer_ * 4u + 0x100) >> 6) * 0x120) >> 8;
        }
        ++timer_;
        StageState& s = g.stage();
        t_.sprite.flags = 0x2000 | u8(s.next_rotation_set++ | 0x20);
        animate(g, t_.sprite);
        transform(g, t_);
        draw(g, t_.sprite);
    }

private:
    Game& game_;
    s32 x_, y_;           // +00, +04 view position (8.8)
    s16 timer_{};         // +10
    TransformedSprite t_; // +18 sprite, +48 transform
};

// 0804C18C: an air bubble (small, or big: randomly mirrored and flipped)
// rising at 80h a frame and wobbling, growing as it starts; it bursts at
// the water's surface or after 480 frames. At most 12 (0804BFF8, 0804C3C8).
class Bubble final : public Process {
public:
    Bubble(Game& g, s32 x, s32 y, s16 speed_x, bool big)
        : Process(0x4001), x_(x), y_(y), speed_x_(speed_x) {
        ++g.stage().bubbles;
        if (big) {
            t_.sprite.start(0x12AA0, 0x2F2, 0, 0, 0, 0x240, 0x2000);
            flips_ = u16((g.stage().next_random() >> 16) & 3);
        } else {
            t_.sprite.start(0x12A20, 0x2F1, 0, 0, 0, 0x240, 0x2000);
        }
        t_.scale_x = t_.scale_y = 0;
        game_ = &g;
    }

    void on_destroy() override { --game_->stage().bubbles; }

    void update(Game& g) override {
        StageState& s = g.stage();
        const Assets& a = g.assets();
        t_.x = s16(s16(u32(x_ + (sine(a, (timer_ & 0xFFu) * 4) >> 4)) >> 8) - s.camera.x);
        t_.y = s16(s16(y_ >> 8) - s.camera.y);
        const s16 scale = s16(std::min((timer_ + 1) * 0x10, 0x100));
        t_.scale_x = (flips_ & 1) ? s16(-scale) : scale;
        t_.scale_y = (flips_ & 2) ? s16(-scale) : scale;
        if (u16(t_.x + 0x20) >= 0x131 || u16(t_.y + 0x20) >= 0xE1 || s.water_level < 0 ||
            s.water_level > (y_ >> 8) - 3 || timer_ >= 0x1E1) {
            g.scheduler().destroy(*this);
            return;
        }
        x_ += speed_x_;
        y_ += speed_y_;
        speed_x_ = s16(speed_x_ - (speed_x_ >> 3));
        ++timer_;
        t_.sprite.flags = 0x2000 | u8(s.next_rotation_set++ | 0x20);
        animate(g, t_.sprite);
        transform(g, t_);
        draw(g, t_.sprite);
    }

private:
    Game* game_{};
    s32 x_, y_;           // +00, +04 (8.8)
    s16 speed_x_;         // +08
    s16 speed_y_ = -0x80; // +0A
    s16 timer_{};         // +10
    u16 flips_{};         // +14
    TransformedSprite t_; // +18 sprite, +48 transform
};

// 0804C2B4: while the drowned player lies dead, bubbles rise from it now
// and then (from 0804C398).
class DrowningBubbles final : public Process {
public:
    explicit DrowningBubbles(Player& p) : Process(0x4001), player_(p) {}

    void update(Game& g) override {
        StageState& s = g.stage();
        if (!(player_.flags & player_flag::dead)) {
            g.scheduler().destroy(*this);
            return;
        }
        if ((s.ticks & 3) != 0 || (s.next_random() & 0x300) != 0) {
            return;
        }
        u32 more = (s.next_random() & 0x100) >> 8;
        do {
            const u32 r1 = s.next_random(), r2 = s.next_random(), r3 = s.next_random();
            create_bubble(g, player_.x + s32(((r1 & 0x7FF00) >> 8) - 0x400),
                          player_.y + s32((r2 & 0x7FF00) >> 8) - 0x1000, 0, (r3 & 0x100) != 0);
        } while (more-- != 0);
    }

private:
    Player& player_; // +00
};

} // namespace

void create_ride_handle(Game& g) {
    g.scheduler().create<RideHandle>(g);
}

void create_water_splash(Game& g, s16 x, s16 y) {
    g.scheduler().create<Effect>(g, x, y, 0x10, 0, 0xC, 0x245, 0, 0x1C0, 0x2000);
}

void create_air_countdown(Game& g, const Player& p, u8 n) {
    g.scheduler().create<AirCountdown>(g, p, n);
}

bool create_bubble(Game& g, s32 x, s32 y, s16 speed_x, bool big) {
    if (g.stage().bubbles >= 12) {
        return false;
    }
    g.scheduler().create<Bubble>(g, x, y, speed_x, big);
    return true;
}

void start_drowning_bubbles(Game& g, Player& p) {
    g.scheduler().create<DrowningBubbles>(p);
}

void create_chute_tails(Game& g, const Player& p) {
    g.scheduler().create<ChuteTails>(g, p.index);
}

void start_player_effects(Game& g) {
    StageState& s = g.stage();
    if (!s.dust_budget) {
        auto& b = g.scheduler().create<DustBudget>();
        b.stage_ = &s;
        s.dust_budget = &b;
        s.dust_left = 0x14;
    }
}

void create_skid_dust(Game& g, s16 x, s16 y, s16 drift_x) {
    StageState& s = g.stage();
    if (!s.dust_budget || s.dust_left == 0) {
        return;
    }
    --s.dust_left;
    g.scheduler().create<SkidDust>(x, y, drift_x, s.next_random());
}

void create_air_attack_effect(Game& g, const Player& p) {
    auto& e = g.scheduler().create<Effect>(g, 0, 0, 0xA0, p.index, 0x10, 0x32, 1, 0x200, 0x2000);
    if (!(p.flags & player_flag::facing_left)) {
        e.sprite().flags |= sprite_flag::flip_x;
    } else {
        e.sprite().flags &= ~sprite_flag::flip_x;
    }
}

void start_lost_rings(Game& g) {
    if (g.game_mode >= 2) {
        throw Unrecovered("lost rings in multiplayer (08041FCC)");
    }
    StageState& s = g.stage();
    auto& r = g.scheduler().create<LostRings>(u8(s.zone));
    r.stage_ = &s;
    s.lost_rings = &r;
}

void lost_rings_bounce_off_ceilings(Game& g) {
    if (Process* r = g.stage().lost_rings) {
        static_cast<LostRings*>(r)->bounce_off_ceilings();
    }
}

void start_spin_dash_dust(Game& g) {
    g.scheduler().create<SpinDashDust>(g);
}

void scatter_rings(Game& g, s32 x, s32 y, u16 count) {
    StageState& s = g.stage();
    if (s.lost_rings) {
        static_cast<LostRings*>(s.lost_rings)->scatter(g, x, y, count);
    }
}

void spread_rings(Game& g, s32 x, s32 y, u16 count) {
    StageState& s = g.stage();
    if (s.lost_rings) {
        static_cast<LostRings*>(s.lost_rings)->spread(g, x, y, count);
    }
}

void create_shield(Game& g, u8 player) {
    g.scheduler().create<PowerEffect>(g, player, player_power::shield, 0x2EE);
    g.sound().start(0xA8);
}

void create_magnetic_shield(Game& g, u8 player) {
    g.scheduler().create<PowerEffect>(g, player, player_power::magnetic_shield, 0x2EF);
    g.sound().start(0xA8);
}

void create_invincibility(Game& g, u8 player) {
    g.scheduler().create<PowerEffect>(g, player, player_power::invincible, 0x2F0);
}

} // namespace sonic_advance_gba
