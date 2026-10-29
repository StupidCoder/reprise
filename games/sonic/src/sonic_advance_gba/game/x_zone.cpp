// Act 12 (X-Zone): its falling platforms and the three bosses the act
// strings together (enemy kinds 28, 29 and 30), each closing the view in on
// its arena, fought, then flying off to open the way to the next.

#include "sonic_advance_gba/game/x_zone.hpp"
#include "sonic_advance_gba/game/boss.hpp"
#include "sonic_advance_gba/game/boss_parts.hpp"
#include "sonic_advance_gba/game/game.hpp"
#include "sonic_advance_gba/game/math.hpp"
#include "sonic_advance_gba/game/object_base.hpp"
#include "sonic_advance_gba/game/object_util.hpp"
#include "sonic_advance_gba/game/player.hpp"
#include "sonic_advance_gba/game/player_effects.hpp"
#include "sonic_advance_gba/game/results.hpp"
#include "sonic_advance_gba/game/solid.hpp"
#include "sonic_advance_gba/game/sound.hpp"
#include "sonic_advance_gba/game/stage.hpp"
#include <algorithm>
#include <array>
#include <memory>

namespace sonic_advance_gba {

namespace {

using namespace boss_parts;

// 080935B0, 080936B8 / 08093890 / 08093A90: a platform that dips under
// the players standing on it; half a second after the first player steps
// on it, it falls (speeding up 2Ah a frame), carrying its riders for 32
// frames, then dropping them.
class FallingPlatform final : public Interactive {
public:
    FallingPlatform(Game& g, const ObjectSpawn& o) : Interactive(o), game_(g) {
        sprite_.start(g.sprite_tiles.allocate(6), 0x23E, 0, x_, y_, 0x480, 0x2000);
        animate(g, sprite_);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 08093C54

    void update(Game& g) override {
        StageState& s = g.stage();
        sprite_.x = s16(x_ - s.camera.x);
        sprite_.y = s16(y_ - s.camera.y);
        switch (state_) {
        case State::resting: {
            u32 r = collide_platform(g, sprite_, x_, y_, s.player[0]);
            if (s.players == 2) {
                r |= collide_platform(g, sprite_, x_, y_, s.player[1]);
            }
            if ((r & solid::on_top) && s.player[0].standing_on == &sprite_) {
                state_ = State::shaking;
                timer_ = 0x1E;
            }
            if (g.game_mode >= 2) {
                timer_ = 0;
                state_ = State::falling;
            }
            if (out_of_range(s, x_, y_, sprite_.x, sprite_.y)) {
                leave(g);
                return;
            }
            break;
        }
        case State::shaking:
            // 08093890
            collide_platform(g, sprite_, x_, y_, s.player[0]);
            if (s.players == 2) {
                collide_platform(g, sprite_, x_, y_, s.player[1]);
            }
            if (g.game_mode >= 2) {
                throw Unrecovered("falling platforms in multiplayer (08093890)");
            }
            if (--timer_ == 0) {
                state_ = State::falling;
            }
            if (out_of_range(s, x_, y_, sprite_.x, sprite_.y)) {
                leave(g);
                return;
            }
            break;
        case State::falling:
            // 08093A90
            speed_y_ = s16(speed_y_ + 0x2A);
            fall_ += speed_y_;
            sprite_.y = s16(sprite_.y + s16(u32(fall_) >> 8));
            ++timer_;
            for (unsigned n = 0; n < s.players; ++n) {
                Player& p = s.player[n];
                if ((p.flags & player_flag::on_object) && p.standing_on == &sprite_) {
                    if (timer_ < 0x21) {
                        p.y += 0x100 + speed_y_;
                    } else {
                        p.flags = (p.flags & ~player_flag::on_object) | player_flag::in_air;
                        p.speed_y = speed_y_;
                    }
                }
                if (timer_ < 0x20) {
                    collide_platform(g, sprite_, x_, s16(y_ + (fall_ >> 8)), p);
                }
            }
            if (!near_view(sprite_.x, sprite_.y)) {
                leave(g);
                return;
            }
            break;
        }
        // The dip under riders (a quarter turn of the sine at most: 4 pixels).
        const auto on = [&](const Player& p) {
            return (p.flags & player_flag::on_object) && p.standing_on == &sprite_;
        };
        if (on(s.player[0]) || (s.players == 2 && on(s.player[1]))) {
            if (dip_ != 0x100) {
                dip_ = u16(dip_ + 0x10);
            }
        } else if (dip_ != 0) {
            dip_ = u16(dip_ - 0x10);
        }
        sprite_.y = s16(sprite_.y + (sine(g.assets(), dip_) >> 12));
        draw(g, sprite_);
    }

private:
    enum class State : u8 { resting, shaking, falling };

    void leave(Game& g) {
        give_back();
        g.scheduler().destroy(*this);
    }

    Game& game_;
    Sprite sprite_; // +0C
    u16 timer_{};   // +3C
    s32 fall_{};    // +44 8.8
    s16 speed_y_{}; // +4A (the speed across, +48, stays 0)
    u16 dip_{};     // +4C
    State state_ = State::resting;
};

// What the X-Zone bosses share: Eggman's machine (+0C) and Eggman (+3C)
// with their own tiles, the hit flash and the contact.
class XZoneBossBase : public Process {
public:
    XZoneBossBase(Game& g, const ObjectSpawn& o, u32 vehicle_tiles, u16 vehicle_animation,
                  u8 vehicle_variant, u16 vehicle_layer, u16 eggman_animation)
        : Process(0x2000), game_(g) {
        base_x_ = s16(o.record[0] * 8 + o.cell_x * 0x100);
        base_y_ = s16(o.record[1] * 8 + o.cell_y * 0x100);
        vehicle_tiles_ = g.sprite_tiles.allocate(vehicle_tiles);
        vehicle_.start(vehicle_tiles_, vehicle_animation, vehicle_variant, base_x_, base_y_,
                       vehicle_layer, 0x2000);
        o.record[0] = 0xFE;
        eggman_tiles_ = g.sprite_tiles.allocate(8);
        eggman_.start(eggman_tiles_, eggman_animation, 0, base_x_, base_y_, 0x540, 0x2000);
    }

    void on_destroy() override {
        game_.sprite_tiles.free(vehicle_tiles_);
        game_.sprite_tiles.free(eggman_tiles_);
    }

    s8 hits() const { return hits_; }

    s16 act_x() const { return act_x_; }

    s16 act_y() const { return act_y_; }

    const Sprite& vehicle() const { return vehicle_; }

    Sprite& eggman() { return eggman_; }

protected:
    // Eggman sits in the machine (3 pixels higher in his escape pod).
    void seat_eggman() {
        eggman_.x = vehicle_.x;
        eggman_.y = vehicle_.y;
        if (eggman_.animation == 0x2B6) {
            eggman_.y = s16(vehicle_.y - 3);
        }
    }

    // Blinking white after a hit (while more than 16 of its 32 frames are
    // left), then both drawn.
    void show(Game& g) {
        if (flash_ != 0) {
            const u8 b = --flash_;
            if (s8(b) > 0x10 && (b & 2) == 0 && !(g.stage().player[0].flags & player_flag::dead)) {
                vehicle_.flags |= 0x100;
                flash_on(g);
            }
        }
        draw(g, vehicle_);
        draw(g, eggman_);
    }

    // The attack box on the machine's hitbox hits it (Eggman hurts); a
    // touch hurts the player (Eggman laughs).
    void contact(Game& g, s16 x, s16 y) {
        StageState& s = g.stage();
        vehicle_.flags &= ~sprite_flag::mode;
        flash_off(g);
        if (flash_ != 0) {
            return;
        }
        const BossContact c0 = boss_contact_single(g, vehicle_, x, y, s.player[0]);
        const BossContact c1 = s.players == 2 ? boss_contact_single(g, vehicle_, x, y, s.player[1])
                                              : BossContact::none;
        if (c0 == BossContact::attacked || c1 == BossContact::attacked) {
            ++hits_;
            flash_ = 0x20;
            eggman_.variant = 2;
            eggman_.previous_variant = 0xFF;
            eggman_.flags &= ~sprite_flag::ended;
            g.sound().start(sound_boss_hit);
        } else if (c0 == BossContact::hurt || c1 == BossContact::hurt) {
            laugh();
        }
    }

    void add_points(Game& g) {
        StageState& s = g.stage();
        const u32 before = s.score;
        s.score += 1000;
        if (divide(s32(s.score), 50000) != divide(s32(before), 50000) && g.game_mode == 0) {
            ++s.lives;
        }
    }

public:
    void laugh() {
        eggman_.variant = 1;
        eggman_.previous_variant = 0xFF;
        eggman_.flags &= ~sprite_flag::ended;
    }

protected:
    Game& game_;
    s16 base_x_, base_y_;
    u32 vehicle_tiles_{}, eggman_tiles_{}; // +10, +40
    Sprite vehicle_;                       // +0C
    Sprite eggman_;                        // +3C
    s16 act_x_{}, act_y_{};                // +80, +82 where it is in the act
    s8 hits_{};                            // +86
    u8 flash_{};                           // +87
};

class WreckingBallBoss;

// 08034FE0's parts: the ball's chain grows out from the machine (four
// links and the ball, then swinging with it) and its mount; ended (in
// smoke) once the boss has taken four hits.
class ChainPart final : public Process {
public:
    enum class Kind : u8 { mount, link, ball, hook };
    ChainPart(Game& g, WreckingBallBoss& boss, Kind kind, u8 index);

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 080357E8

    void update(Game& g) override;

private:
    void mount(Game& g);
    void link(Game& g);
    void ball(Game& g);
    void hook(Game& g);

    Game& game_;
    WreckingBallBoss& boss_;
    Kind kind_;
    Sprite sprite_;    // +00
    u16 timer_ = 0x40; // +30
    u8 index_;         // +40
    bool swinging_{};  // the update is 080353DC / 08035558
};

// 08034448: the first boss (enemy kind 28): the view closes in on a
// screen-wide arena; the machine drops in from above (080346E8), lowers a
// wrecking ball on a chain (08034FE0), then swings to and fro across the
// arena (080348EC), the ball hanging below it. Four hits break it
// (08034B4C): it flies off smoking (08034C70) and the view opens again.
class WreckingBallBoss final : public XZoneBossBase {
public:
    WreckingBallBoss(Game& g, const ObjectSpawn& o)
        : XZoneBossBase(g, o, 0x1E, 0x2AC, 0, 0x580, 0x2AE) {}

    void update(Game& g) override {
        switch (state_) {
        case State::waiting:
            wait(g);
            break;
        case State::arriving:
            arrive(g);
            break;
        case State::swinging:
            swing(g);
            break;
        case State::leaving:
            leave(g);
            break;
        }
    }

    u16 chain_angle() const { return chain_angle_; }

    bool hook_shown() const { return hook_shown_ && speed_x_ != 0; }

    s32 x_offset() const { return x_; }

private:
    enum class State : u8 { waiting, arriving, swinging, leaving };

    void place(const StageState& s) {
        act_x_ = s16(s16(u32(x_) >> 8) + base_x_);
        act_y_ = s16(s16(u32(y_) >> 8) + base_y_);
        vehicle_.x = s16(act_x_ - s.camera.x);
        vehicle_.y = s16(act_y_ - s.camera.y);
    }

    // 08034614: the view's sides close in once it reaches the arena, then
    // its top and bottom; then the music, and the machine comes.
    void wait(Game& g) {
        StageState& s = g.stage();
        auto& c = s.camera;
        if (phase_ == 0) {
            if (c.x >= base_x_ - 0x78) {
                c.min_x = s16(base_x_ - 0x78);
                c.max_x = s16(base_x_ + 0x78);
                phase_ = 1;
            }
        } else if (phase_ == 1) {
            if (c.y >= base_y_ - 0x40) {
                g.scheduler().create<CameraBounds>(g, base_y_, s16(base_y_ + 0xA0));
                phase_ = 2;
            }
        }
        if (c.min_x == base_x_ - 0x78 && c.max_y == base_y_ + 0xA0) {
            phase_ = 0;
            speed_y_ = 0x100;
            state_ = State::arriving;
            s.music_queued = 0x14;
        }
    }

    // 080346E8: down, across to the arena's middle, the ball lowered.
    void arrive(Game& g) {
        StageState& s = g.stage();
        x_ += speed_x_;
        y_ += speed_y_;
        place(s);
        animate(g, vehicle_);
        animate(g, eggman_);
        seat_eggman();
        show(g);
        switch (phase_) {
        case 0:
            if (y_ < 0x2000) {
                return;
            }
            y_ = 0x2000;
            speed_x_ = -0x100;
            speed_y_ = -0x10;
            break;
        case 1:
            if (x_ > 0) {
                return;
            }
            x_ = 0;
            speed_x_ = speed_y_ = 0;
            timer_ = 4;
            break;
        case 2:
            if (--timer_ != 0) {
                return;
            }
            eggman_.variant = 1;
            eggman_.previous_variant = 0xFF;
            angle_ = 0x100;
            chain_angle_ = 0x100;
            timer_ = 0x40;
            ++phase_;
            create_chain(g);
            return;
        case 3:
            if (timer_ != 0) {
                --timer_;
            }
            if (eggman_.flags & sprite_flag::ended) {
                eggman_.variant = 0;
                eggman_.previous_variant = 0xFF;
            }
            if (eggman_.variant != 0 || timer_ != 0) {
                return;
            }
            speed_x_ = -0x30;
            hook_shown_ = true;
            phase_ = 0;
            state_ = State::swinging;
            g.scheduler().create<CameraBounds>(g, s16(s.camera.min_y - 0x50), s.camera.max_y);
            return;
        default:
            return;
        }
        ++phase_;
    }

    // 08034FE0
    void create_chain(Game& g) {
        g.scheduler().create<ChainPart>(g, *this, ChainPart::Kind::mount, 0);
        for (u8 i = 0; i < 4; ++i) {
            g.scheduler().create<ChainPart>(g, *this, ChainPart::Kind::link, i);
        }
        g.scheduler().create<ChainPart>(g, *this, ChainPart::Kind::ball, 4);
        g.scheduler().create<ChainPart>(g, *this, ChainPart::Kind::hook, 5);
    }

    // 080348EC: the swing: the machine turns at the arena's ends (its
    // picture mirrored at the swing's middle), bobbing two pixels; the
    // chain hangs at 300h +- the swing.
    void swing(Game& g) {
        StageState& s = g.stage();
        const Assets& a = g.assets();
        x_ += speed_x_;
        y_ += speed_y_;
        const u16 turned = u16(angle_ - 4);
        angle_ = u16(turned & 0x3FF);
        if ((turned & 0x1FF) == 0x100) {
            vehicle_.flags ^= sprite_flag::flip_x;
            eggman_.flags ^= sprite_flag::flip_x;
        }
        act_x_ = s16(s16(u32(x_) >> 8) + base_x_);
        act_y_ = s16((sine(a, (((angle_ + 0x100) * 2) & 0x3FF) + 0x100) >> 13) + s16(u32(y_) >> 8) +
                     base_y_);
        const s16 c = sine(a, angle_ + 0x100u);
        u32 reach = u32(c >> 6) & 0xFFFF;
        if ((c >> 6) > 0xFF) {
            reach = 0xFF;
        }
        chain_angle_ = u16(reach + 0x300);
        contact(g, act_x_, act_y_);
        if (hits_ >= 4) {
            broken(g);
            return;
        }
        vehicle_.x = s16(act_x_ - s.camera.x);
        vehicle_.y = s16(act_y_ - s.camera.y);
        if (eggman_.variant != 0 && (eggman_.flags & sprite_flag::ended)) {
            eggman_.variant = 0;
            eggman_.previous_variant = 0xFF;
        }
        animate(g, vehicle_);
        animate(g, eggman_);
        seat_eggman();
        show(g);
        switch (phase_) {
        case 0:
            if (u16(angle_ - 0x101) > 0x1FF) {
                return;
            }
            speed_x_ = 0;
            break;
        case 1:
            if (angle_ > 0x200) {
                return;
            }
            speed_x_ = 0xC0;
            break;
        case 2:
            if (angle_ > 0x100) {
                return;
            }
            speed_x_ = 0;
            break;
        case 3:
            if (angle_ != 0 && angle_ < 0x101) {
                return;
            }
            speed_x_ = -0xC0;
            phase_ = 0;
            return;
        default:
            return;
        }
        ++phase_;
    }

    // 08034B4C: the fourth hit.
    void broken(Game& g) {
        const Assets& a = g.assets();
        phase_ = 0;
        timer_ = 0x3C;
        speed_x_ = speed_y_ = 0;
        y_ += (sine(a, (((angle_ + 0x100) * 2) & 0x3FF) + 0x100) >> 13) * 0x100;
        eggman_.variant = 2;
        eggman_.previous_variant = 0xFF;
        g.sound().start(sound_boss_broken);
        vehicle_.flags &= ~sprite_flag::mode;
        flash_off(g);
        add_points(g);
        state_ = State::leaving;
        leave(g);
    }

    // 08034C70: smoking, Eggman takes his pod, rises, then flies off to
    // the right; past 130h pixels the view's limits are the act's again
    // (music 34h).
    void leave(Game& g) {
        StageState& s = g.stage();
        x_ += speed_x_;
        y_ += speed_y_;
        act_x_ = s16(s16(u32(x_) >> 8) + base_x_);
        act_y_ = s16(s16(u32(y_) >> 8) + base_y_);
        if ((timer_ & 7) == 0) {
            create_explosion_piece(g, act_x_, act_y_, 0, 5, 0x5C0);
        }
        animate(g, vehicle_);
        animate(g, eggman_);
        vehicle_.x = s16(act_x_ - s.camera.x);
        vehicle_.y = s16(act_y_ - s.camera.y);
        seat_eggman();
        draw(g, vehicle_);
        draw(g, eggman_);
        switch (phase_) {
        case 0:
            if (--timer_ == 0) {
                timer_ = 0x5A;
                ++phase_;
                g.sprite_tiles.free(eggman_tiles_);
                eggman_tiles_ = g.sprite_tiles.allocate(0xC);
                eggman_.tiles.to = eggman_tiles_;
                eggman_.animation = 0x2B6;
                eggman_.variant = 4;
                eggman_.previous_variant = 0xFF;
                eggman_.layer = 0x5C0;
            }
            break;
        case 1:
            speed_y_ += 3;
            if (--timer_ == 0) {
                timer_ = 0x1E;
                ++phase_;
            }
            break;
        case 2:
            speed_y_ -= 0x18;
            if (--timer_ == 0) {
                speed_x_ = 0x200;
                speed_y_ = -0x40;
                timer_ = 1;
                ++phase_;
                eggman_.variant = 6;
                eggman_.previous_variant = 0xFF;
                vehicle_.flags |= sprite_flag::flip_x;
                eggman_.flags |= sprite_flag::flip_x;
            }
            break;
        case 3:
            if (x_ > 0x13000) {
                auto& c = s.camera;
                c.min_y = 0;
                c.max_y = s16(s.map.height);
                c.max_x = s16(s.map.width);
                s.music_queued = 0x34;
                g.scheduler().destroy(*this);
            }
            break;
        default:
            break;
        }
    }

    State state_ = State::waiting;
    s32 x_ = 0x4000, y_ = -0x6000; // +6C, +70 8.8 from its place
    s32 speed_x_{}, speed_y_{};    // +74, +78
    u16 timer_ = 0x78;             // +84
    u16 angle_{};                  // +88 the swing
    u16 chain_angle_{};            // +8A
    bool hook_shown_{};            // +8C
    u8 phase_{};                   // +8D
};

ChainPart::ChainPart(Game& g, WreckingBallBoss& boss, Kind kind, u8 index)
    : Process(kind == Kind::mount  ? 0x2100
              : kind == Kind::link ? u16(0x2100 | index)
              : kind == Kind::ball ? 0x2104
                                   : 0x2105),
      game_(g), boss_(boss), kind_(kind), index_(index) {
    switch (kind) {
    case Kind::mount:
        sprite_.start(g.sprite_tiles.allocate(2), 0x2AB, 1, 0, 0, 0x600, 0x2000);
        break;
    case Kind::link:
        sprite_.start(g.sprite_tiles.allocate(4), 0x2AB, 2, 0, 0, 0x640, 0x2000);
        break;
    case Kind::ball:
        sprite_.start(g.sprite_tiles.allocate(0x10), 0x2AD, 0, 0, 0, 0x5C0, 0x2000);
        break;
    case Kind::hook:
        sprite_.start(g.sprite_tiles.allocate(4), 0x2AB, 0, 0, 0, 0x600, 0x2000);
        break;
    }
}

void ChainPart::update(Game& g) {
    switch (kind_) {
    case Kind::mount:
        mount(g);
        break;
    case Kind::link:
        link(g);
        break;
    case Kind::ball:
        ball(g);
        break;
    case Kind::hook:
        hook(g);
        break;
    }
}

// 08035290: the chain's mount rises out of the machine, then stays on it.
void ChainPart::mount(Game& g) {
    const auto& c = g.stage().camera;
    u32 rise = 0;
    if (timer_ != 0) {
        --timer_;
        rise = timer_ >> 3;
    }
    const s16 x = boss_.act_x();
    const s16 y = s16(boss_.act_y() - s16(rise));
    sprite_.x = s16(x - c.x);
    sprite_.y = s16(y - c.y);
    animate(g, sprite_);
    draw(g, sprite_);
    if (boss_.hits() > 3) {
        create_smoke(g, s32(x) << 8, (y + 0x14) * 0x100);
        g.scheduler().destroy(*this);
    }
}

// 08035324, 080353DC: a link: lowered below the machine over 64 frames,
// then hanging at the chain's angle (its place 13 + 14 x its number down
// the chain); ended in smoke one by one after the fourth hit.
void ChainPart::link(Game& g) {
    const auto& c = g.stage().camera;
    const Assets& a = g.assets();
    const s16 x = boss_.act_x(), y = boss_.act_y();
    if (!swinging_) {
        const s32 k = index_ * 0xE;
        s16 drop;
        if (--timer_ == 0) {
            drop = s16(k + 0x23);
            timer_ = u16(index_ * 8 + 8);
            swinging_ = true;
        } else {
            const s32 t = s32(u32(timer_) << 8) >> 6;
            drop = s16(k - (s16(u32((k + 0x15) * t * 0x100) >> 16) - 0x23));
        }
        sprite_.x = s16(x - c.x);
        sprite_.y = s16(drop + y - c.y);
        animate(g, sprite_);
        draw(g, sprite_);
        return;
    }
    const s32 k = s16((u32(index_) * 0xE0000 + 0xD0000) >> 16);
    const u16 angle = boss_.chain_angle();
    const s32 cx = s16(((sine(a, s16(angle) + 0x100) * k * 4) >> 16) + x);
    const s32 cy = s16(y + ((k * sine(a, angle & 0x1FF) * 4) >> 16) + 0x16);
    sprite_.x = s16(cx - c.x);
    sprite_.y = s16(cy - c.y);
    animate(g, sprite_);
    draw(g, sprite_);
    if (boss_.hits() > 3 && --timer_ == 0) {
        create_smoke(g, cx << 8, cy << 8);
        g.scheduler().destroy(*this);
    }
}

// 080354C4, 08035558: the ball: lowered to 99 pixels below the machine
// over 64 frames, then swinging at the chain's end (4Dh along it); it
// hurts players it meets (Eggman laughs). After the fourth hit it smokes
// for a second, then ends.
void ChainPart::ball(Game& g) {
    StageState& s = g.stage();
    const auto& c = s.camera;
    const Assets& a = g.assets();
    const s16 x = boss_.act_x(), y = boss_.act_y();
    if (!swinging_) {
        s16 drop;
        if (--timer_ == 0) {
            drop = 99;
            timer_ = 0x3C;
            swinging_ = true;
        } else {
            drop = s16(99 - s16(u32((s32(u32(timer_) << 8) >> 6) * 0x5D00) >> 16));
        }
        sprite_.x = s16(x - c.x);
        sprite_.y = s16(drop + y - c.y);
        animate(g, sprite_);
        draw(g, sprite_);
        return;
    }
    const u16 angle = boss_.chain_angle();
    const s16 bx = s16(((sine(a, s16(angle) + 0x100) * 0x4D) >> 14) + x);
    const s16 by = s16(y + ((sine(a, angle & 0x1FF) * 0x4D) >> 14) + 0x16);
    if (boss_.hits() < 4) {
        const bool hit0 = hazard_hits(g, sprite_, bx, by, s.player[0]);
        const bool hit1 = s.players == 2 && hazard_hits(g, sprite_, bx, by, s.player[1]);
        if (hit0 || hit1) {
            boss_.laugh();
        }
    }
    sprite_.x = s16(bx - c.x);
    sprite_.y = s16(by - c.y);
    animate(g, sprite_);
    draw(g, sprite_);
    if (boss_.hits() > 3) {
        if ((timer_ & 7) == 0) {
            const u32 r = s.next_random();
            create_smoke(g, (bx + s32(r & 0x1F) - 0xF) * 0x100,
                         (by + s32((r & 0x1F0000) >> 16) - 0xF) * 0x100);
        }
        if (--timer_ == 0) {
            g.scheduler().destroy(*this);
        }
    }
}

// 08035738: the hook under the machine, shown while it moves in the
// swing, ended once it is 12Ch pixels along in its flight.
void ChainPart::hook(Game& g) {
    if (!boss_.hook_shown()) {
        return;
    }
    if (boss_.x_offset() > 0x12C00) {
        g.scheduler().destroy(*this);
        return;
    }
    const auto& c = g.stage().camera;
    sprite_.flags =
        (sprite_.flags & ~sprite_flag::flip_x) | (boss_.vehicle().flags & sprite_flag::flip_x);
    sprite_.x = s16(boss_.act_x() - c.x);
    sprite_.y = s16(boss_.act_y() - c.y);
    animate(g, sprite_);
    draw(g, sprite_);
}

// What the second boss's parts read of it (they may outlive it: then
// they read its last values, as the original reads its freed record).
struct FistBossView {
    s16 act_x{}, act_y{};
    s32 x{}, y{}; // 8.8 from its place
    s16 base_x{}, base_y{};
    s8 hits{};
    bool armed{}; // +8C
    u32 vehicle_flags{};
    s32 speed_x{};
    u8 phase{};
};

class FistBoss;

// 0803657C's parts of the second boss: its fist (08036964), which hurts
// players it meets and from the third hit on is fired off at a player
// behind the machine (08036B64), and four hull pieces (0803670C) that fly
// off and bounce away when it breaks (080367E0).
class FistPart final : public Process {
public:
    FistPart(Game& g, FistBoss& boss, std::shared_ptr<const FistBossView> view, int piece);

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 08036CB0

    void update(Game& g) override;

private:
    void fist(Game& g);
    void flying_fist(Game& g);
    void hull(Game& g);
    void hull_off(Game& g);

    void copy_look() {
        sprite_.flags = (sprite_.flags & ~u32(0x580)) | (view_->vehicle_flags & 0x580);
    }

    Game& game_;
    FistBoss& boss_;
    std::shared_ptr<const FistBossView> view_;
    int piece_;                 // -1 the fist, else the hull piece (+44)
    Sprite sprite_;             // +00
    u16 timer_ = 0x3C;          // +30
    s32 x_{}, y_{};             // +34, +38 8.8
    s16 speed_x_{}, speed_y_{}; // +3C, +3E
    s16 from_x_{}, from_y_{};   // +40, +42
    bool loose_{};              // the update is 08036B64 / 080367E0
};

// 08035B14: the second boss (enemy kind 29; not met again once a
// checkpoint was passed): the view closes in, the machine comes in from
// the right (08035DA4) and, armed with its fist, drives to and fro across
// the arena (08035F40). Four hits break it (08036120): it falls, smoking,
// to the ground (0803620C); Eggman flies off (08015C10) and the view opens.
class FistBoss final : public XZoneBossBase {
public:
    FistBoss(Game& g, const ObjectSpawn& o)
        : XZoneBossBase(g, o, 0x36, 0x2AA, 0, 0x580, 0x2A8),
          view_(std::make_shared<FistBossView>()) {
        publish();
    }

    void update(Game& g) override {
        switch (state_) {
        case State::waiting:
            wait(g);
            break;
        case State::arriving:
            arrive(g);
            break;
        case State::fighting:
            fight(g);
            break;
        case State::falling:
            fall(g);
            break;
        }
        publish();
    }

private:
    enum class State : u8 { waiting, arriving, fighting, falling };

    void publish() {
        FistBossView& v = *view_;
        v.act_x = act_x_;
        v.act_y = act_y_;
        v.x = x_;
        v.y = y_;
        v.base_x = base_x_;
        v.base_y = base_y_;
        v.hits = hits_;
        v.armed = armed_;
        v.vehicle_flags = vehicle_.flags;
        v.speed_x = speed_x_;
        v.phase = phase_;
    }

    void move() {
        x_ += speed_x_;
        y_ += speed_y_;
        act_x_ = s16(s16(u32(x_) >> 8) + base_x_);
        act_y_ = s16(s16(u32(y_) >> 8) + base_y_);
    }

    void show_both(Game& g) {
        const auto& c = g.stage().camera;
        vehicle_.x = s16(act_x_ - c.x);
        vehicle_.y = s16(act_y_ - c.y);
        animate(g, vehicle_);
        animate(g, eggman_);
        eggman_.x = vehicle_.x;
        eggman_.y = vehicle_.y;
        show(g);
    }

    // 08035CE8
    void wait(Game& g) {
        StageState& s = g.stage();
        auto& c = s.camera;
        if (phase_ == 0) {
            if (c.x >= base_x_ - 0x78) {
                c.min_x = s16(base_x_ - 0x98);
                c.max_x = s16(base_x_ + 0x98);
                phase_ = 1;
            }
        } else if (phase_ == 1) {
            if (c.y >= base_y_ - 0xE0) {
                g.scheduler().create<CameraBounds>(g, s16(base_y_ - 0xA0), base_y_);
                phase_ = 2;
            }
        }
        if (phase_ == 2) {
            phase_ = 0;
            speed_x_ = -0x180;
            act_x_ = s16(s16(u32(x_) >> 8) + base_x_);
            act_y_ = s16(s16(u32(y_) >> 8) + base_y_);
            publish();
            g.scheduler().create<FistPart>(g, *this, view_, -1);
            for (int i = 0; i < 4; ++i) {
                g.scheduler().create<FistPart>(g, *this, view_, i);
            }
            state_ = State::arriving;
            s.music_queued = 0x15;
        }
    }

    // 08035DA4: in from the right; Eggman laughs; then off it goes.
    void arrive(Game& g) {
        move();
        show_both(g);
        switch (phase_) {
        case 0:
            if (x_ > 0) {
                return;
            }
            x_ = 0;
            speed_x_ = speed_y_ = 0;
            timer_ = 0xF;
            break;
        case 1:
            if (--timer_ != 0) {
                return;
            }
            eggman_.variant = 1;
            eggman_.previous_variant = 0xFF;
            break;
        case 2:
            if (!(eggman_.flags & sprite_flag::ended)) {
                return;
            }
            eggman_.variant = 0;
            eggman_.previous_variant = 0xFF;
            timer_ = 0xF;
            break;
        case 3:
            if (--timer_ != 0) {
                return;
            }
            speed_x_ = -0x200;
            armed_ = true;
            phase_ = 0;
            state_ = State::fighting;
            return;
        default:
            return;
        }
        ++phase_;
    }

    // 08035F40: to and fro, 0C0h pixels each way, half a second at each end.
    void fight(Game& g) {
        StageState& s = g.stage();
        move();
        contact(g, act_x_, act_y_);
        if (hits_ >= 4) {
            broken(g);
            return;
        }
        if (eggman_.variant != 0 && (eggman_.flags & sprite_flag::ended)) {
            eggman_.variant = 0;
            eggman_.previous_variant = 0xFF;
        }
        vehicle_.x = s16(act_x_ - s.camera.x);
        vehicle_.y = s16(act_y_ - s.camera.y);
        animate(g, vehicle_);
        animate(g, eggman_);
        eggman_.x = vehicle_.x;
        eggman_.y = vehicle_.y;
        show(g);
        switch (phase_) {
        case 0:
            if (x_ > -0xC000) {
                return;
            }
            speed_x_ = 0;
            timer_ = 0x1E;
            break;
        case 1:
            if (--timer_ != 0) {
                return;
            }
            speed_x_ = 0x200;
            vehicle_.flags |= sprite_flag::flip_x;
            eggman_.flags |= sprite_flag::flip_x;
            break;
        case 2:
            if (x_ < 0xC000) {
                return;
            }
            speed_x_ = 0;
            timer_ = 0x1E;
            break;
        case 3:
            if (--timer_ != 0) {
                return;
            }
            speed_x_ = -0x200;
            vehicle_.flags &= ~sprite_flag::flip_x;
            eggman_.flags &= ~sprite_flag::flip_x;
            phase_ = 0;
            return;
        default:
            return;
        }
        ++phase_;
    }

    // 08036120
    void broken(Game& g) {
        StageState& s = g.stage();
        phase_ = 0;
        speed_x_ = speed_y_ = 0;
        eggman_.variant = 2;
        eggman_.previous_variant = 0xFF;
        s.camera.min_x = s.camera.x;
        s.camera.max_x = s16(s.camera.x + 0xF0);
        g.sound().start(sound_boss_broken);
        vehicle_.flags &= ~sprite_flag::mode;
        flash_off(g);
        add_points(g);
        state_ = State::falling;
        fall(g);
    }

    // 0803620C: falls, smoking, to the ground; four seconds later Eggman
    // flies off in a puff of smoke (music 35h); four more, and the view's
    // limits are the act's again.
    void fall(Game& g) {
        StageState& s = g.stage();
        if (phase_ < 2) {
            if (phase_ == 0) {
                speed_y_ += 0x10;
            }
            move();
            if ((timer_ & 7) == 0) {
                create_explosion_piece(g, act_x_, act_y_, -0x10, 5, 0x5C0, -0x20);
            }
            animate(g, vehicle_);
            animate(g, eggman_);
            vehicle_.x = s16(act_x_ - s.camera.x);
            vehicle_.y = s16(act_y_ - s.camera.y);
            eggman_.x = vehicle_.x;
            eggman_.y = vehicle_.y;
            draw(g, vehicle_);
            draw(g, eggman_);
        }
        switch (phase_) {
        case 0: {
            --timer_;
            const s32 d = floor_distance(g, act_x_, act_y_ + 0x14);
            if (d < 0) {
                y_ += d * 0x100;
                speed_y_ = 0;
                timer_ = 0xF0;
                ++phase_;
            }
            break;
        }
        case 1:
            if (--timer_ == 0) {
                const s32 x = act_x_, y = act_y_;
                g.scheduler().create<EggmanEscape>(act_x_, act_y_);
                s.music_queued = 0x35;
                create_smoke(g, (x + 0x10) * 0x100, (y - 0x10) * 0x100);
                create_smoke(g, (x - 0x10) * 0x100, (y - 0x10) * 0x100);
                create_smoke(g, (x + 0x14) * 0x100, (y + 0x10) * 0x100);
                create_smoke(g, (x - 0x14) * 0x100, (y + 0x10) * 0x100);
                timer_ = 0xF0;
                ++phase_;
            }
            break;
        case 2:
            if (--timer_ == 0) {
                auto& c = s.camera;
                c.max_y = s16(s.map.height);
                c.max_x = s16(s.map.width);
                c.min_y = 0;
                g.scheduler().destroy(*this);
            }
            break;
        default:
            break;
        }
    }

    State state_ = State::waiting;
    std::shared_ptr<FistBossView> view_;
    s32 x_ = 0xC020, y_ = -0x2800; // +6C, +70 8.8 from its place
    s32 speed_x_{}, speed_y_{};    // +74, +78
    u16 timer_ = 0x78;             // +84
    bool armed_{};                 // +8C
    u8 phase_{};                   // +8D
};

FistPart::FistPart(Game& g, FistBoss& boss, std::shared_ptr<const FistBossView> view, int piece)
    : Process(piece < 0 ? 0x2100 : u16(0x2100 | piece)), game_(g), boss_(boss),
      view_(std::move(view)), piece_(piece) {
    from_x_ = view_->act_x;
    from_y_ = view_->act_y;
    if (piece < 0) {
        sprite_.start(g.sprite_tiles.allocate(0x10), 0x2A7, 0, 0, 0, 0x500, 0x2000);
    } else {
        sprite_.start(g.sprite_tiles.allocate(0x10), 0x2A9, u8(piece), 0, 0,
                      (piece & 1) ? 0x5C0 : 0x4C0, 0x2000);
    }
}

void FistPart::update(Game& g) {
    if (piece_ < 0) {
        loose_ ? flying_fist(g) : fist(g);
    } else {
        loose_ ? hull_off(g) : hull(g);
    }
}

// 08036964
void FistPart::fist(Game& g) {
    StageState& s = g.stage();
    const FistBossView& b = *view_;
    copy_look();
    const s16 x = b.act_x, y = b.act_y;
    sprite_.x = s16(x - s.camera.x);
    sprite_.y = s16(y - s.camera.y);
    if (b.armed && b.hits < 4) {
        const bool hit0 = hazard_hits(g, sprite_, x, y, s.player[0]);
        const bool hit1 = s.players == 2 && hazard_hits(g, sprite_, x, y, s.player[1]);
        if (hit0 || hit1) {
            boss_.laugh();
        }
    }
    animate(g, sprite_);
    draw(g, sprite_);
    if (b.hits <= 2) {
        return;
    }
    bool fire = false;
    if (timer_ == 0) {
        const s32 px = s.player[0].x >> 8;
        fire = (sprite_.flags & sprite_flag::flip_x) ? x + 0x10 < px : px < x - 0x10;
    } else {
        --timer_;
    }
    if (b.hits > 3) {
        fire = true;
    }
    if (fire) {
        x_ = b.x;
        y_ = b.y;
        from_x_ = b.base_x;
        from_y_ = b.base_y;
        speed_y_ = 0;
        speed_x_ = (sprite_.flags & sprite_flag::flip_x) ? 0x300 : -0x300;
        loose_ = true;
    }
}

// 08036B64: the fist fired off along the arena; ended 0C0h pixels from
// the arena's middle.
void FistPart::flying_fist(Game& g) {
    StageState& s = g.stage();
    x_ += speed_x_;
    y_ += speed_y_;
    const s16 x = s16((x_ >> 8) + from_x_), y = s16((y_ >> 8) + from_y_);
    sprite_.x = s16(x - s.camera.x);
    sprite_.y = s16(y - s.camera.y);
    if (view_->hits < 4) {
        const bool hit0 = hazard_hits(g, sprite_, x, y, s.player[0]);
        const bool hit1 = s.players == 2 && hazard_hits(g, sprite_, x, y, s.player[1]);
        if (hit0 || hit1) {
            boss_.laugh();
        }
    }
    animate(g, sprite_);
    draw(g, sprite_);
    if (u32(x_ + 0xC000) > 0x18000) {
        g.scheduler().destroy(*this);
    }
}

// 0803670C: a hull piece, on the machine (animated while it moves).
void FistPart::hull(Game& g) {
    StageState& s = g.stage();
    const FistBossView& b = *view_;
    copy_look();
    sprite_.x = s16(b.act_x - s.camera.x);
    sprite_.y = s16(b.act_y - s.camera.y);
    if (b.speed_x != 0) {
        animate(g, sprite_);
    }
    draw(g, sprite_);
    if (b.hits > 3) {
        from_x_ = b.act_x;
        from_y_ = b.act_y;
        x_ = y_ = 0;
        speed_x_ = speed_y_ = 0;
        loose_ = true;
    }
}

// 080367E0: once the machine is on the ground, the piece flies off
// (by its number; mirrored with the machine) and bounces away; ended
// when it bounces out of view.
void FistPart::hull_off(Game& g) {
    StageState& s = g.stage();
    const FistBossView& b = *view_;
    s16 x = from_x_, y = from_y_;
    if (b.phase != 0) {
        if (speed_x_ == 0) {
            speed_y_ = -0x100;
            switch (piece_) {
            case 0:
                speed_x_ = -0xC0;
                speed_y_ = -0x1B0;
                break;
            case 1:
                speed_x_ = -0x100;
                speed_y_ = -0x180;
                break;
            case 2:
                speed_x_ = 0x100;
                speed_y_ = -0x190;
                break;
            case 3:
                speed_x_ = 0xE0;
                speed_y_ = -0x1A0;
                break;
            default:
                break;
            }
            if (b.vehicle_flags & sprite_flag::flip_x) {
                speed_x_ = s16(-speed_x_);
            }
        }
        speed_y_ = s16(speed_y_ + 0x18);
        x_ += speed_x_;
        y_ += speed_y_;
        const s32 px = (x_ >> 8) + from_x_;
        const s32 py = (y_ >> 8) + from_y_;
        x = s16(px);
        y = s16(py);
        const s32 d = floor_distance(g, s16(px), s16(py) + 0x1E);
        if (d < 0) {
            y_ += d * 0x100;
            speed_y_ = s16(4 - speed_y_);
            if (s16(px) < s.camera.x - 0x20 || s.camera.x + 0x110 < s16(px)) {
                g.scheduler().destroy(*this);
                return;
            }
        }
    }
    sprite_.x = s16(x - s.camera.x);
    sprite_.y = s16(y - s.camera.y);
    animate(g, sprite_);
    draw(g, sprite_);
}

// The third boss (enemy kind 30, 08037198) and the parts it shares its
// record with (they may outlive it: then they read its last values, as the
// original reads its freed record).
struct ArmBossView {
    s16 act_x{}, act_y{};   // +8C, +8E
    s32 x{}, y{};           // +74, +78 8.8 from its place
    s16 base_x{}, base_y{}; // +88, +8A
    u16 timer{};            // +92
    s8 hits{};              // +94
    u8 flash{};             // +95
    bool armed{};           // +98
    u8 phase{};             // +99
    u8 attack{};            // +9A
    u32 vehicle_flags{};
    u8 vehicle_palette{}; // the machine sprite's +25 (FDh while flashing)
};

// What the arm reports (its +7A bits).
namespace arm_status {
inline constexpr u8 posed = 1;  // its picture shows the pose asked for
inline constexpr u8 down = 2;   // lowered to its place
inline constexpr u8 raised = 4; // fully raised
} // namespace arm_status

class ArmBoss;

// 08038ED4, 08039234: the machine's arm (two halves). The boss gives it
// commands (+79): lower it, raise it, take a pose for an attack; it turns
// through its poses (08 -> 0A -> 09 -> 08, 0803915C) until it shows the
// one asked for, and hurts players it meets while the fight is on.
class BossArm final : public Process {
public:
    BossArm(Game& g, ArmBoss& boss, std::shared_ptr<const ArmBossView> view);

    void on_destroy() override { // 0803A5A0
        game_.sprite_tiles.free(upper_.tiles.to);
        game_.sprite_tiles.free(lower_.tiles.to);
    }

    void update(Game& g) override;

    u8 command{}; // +79
    u8 status{};  // +7A

private:
    void set_pose(u8 upper, u8 lower) {
        upper_.variant = upper;
        lower_.variant = lower;
        upper_.previous_variant = lower_.previous_variant = 0xFF;
        pose_ = upper;
    }

    // 080390D8: from an attack's pose to its closing one.
    void close_hand() {
        switch (upper_.variant) {
        case 4:
            set_pose(0xE, 0x11);
            break;
        case 2:
            set_pose(0xF, 0x12);
            break;
        case 6:
            set_pose(0x10, 0x13);
            break;
        default:
            break;
        }
    }

    // 08039044: the pose for an attack.
    void attack_pose(u8 attack) {
        switch (attack) {
        case 2:
            set_pose(2, 3);
            break;
        case 1:
            set_pose(4, 5);
            break;
        case 3:
            set_pose(6, 7);
            break;
        default:
            break;
        }
    }

    void turn_to(u8 pose);

    Game& game_;
    ArmBoss& boss_;
    std::shared_ptr<const ArmBossView> view_;
    Sprite upper_;              // +00
    Sprite lower_;              // +30
    u16 timer_{};               // +60
    s32 x_{}, y_ = -0x1700;     // +64, +68 8.8
    s32 speed_x_{}, speed_y_{}; // +6C, +70
    s16 at_x_{}, at_y_{};       // +74, +76
    u8 pose_{};                 // +78
};

// 0803964C, 08039778: a bomb thrown from the arm (at random: high and slow
// or low and fast), bouncing; it hurts players it meets; ended D0h pixels
// from the arena's middle.
class ArmBomb final : public Process {
public:
    ArmBomb(Game& g, ArmBoss& boss, const ArmBossView& b);

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 0803A5BC

    void update(Game& g) override;

private:
    Game& game_;
    ArmBoss& boss_;
    Sprite sprite_;             // +00
    s32 x_, y_;                 // +34, +38 8.8
    s16 speed_x_{}, speed_y_{}; // +3C, +3E
    s16 base_x_, base_y_;       // +40, +42
    s16 gravity_{};             // +44
};

// Something of the hand's thrown off when the boss breaks (0803A43C): it
// falls and bounces for its time, then ends in smoke.
struct FallingRotated {
    s32 x{}, y{};  // +54, +58 8.8
    s16 speed_y{}; // +48
    u16 timer{};   // +3C
};

// What the hand's links read of it.
struct HandView {
    s16 base_x{}, base_y{}; // +50, +52
    u16 previous_angle{};   // +4E
    s32 reach{};            // +40
    u8 state{};             // +61
};

class GrabbingHand;

// 0803A1A8, 0803A2C8: a link of the hand's chain (drawn turned with it),
// one more for each 16 pixels the hand reaches out.
class HandLink final : public Process {
public:
    HandLink(Game& g, GrabbingHand& hand);

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 0803A5D0

    void update(Game& g) override;

private:
    Game& game_;
    std::shared_ptr<const HandView> hand_view_;
    std::shared_ptr<const ArmBossView> boss_;
    Sprite sprite_;                 // +00
    u16 angle_{};                   // +30 the transform
    s16 scale_x_, scale_y_ = 0x100; // +32, +34
    s16 base_x_, base_y_;           // +50, +52
    s32 reach_{};                   // +40
    u8 index_;                      // +60
    bool falling_{};                // the update is 0803A43C
    FallingRotated fall_;
};

// 08039910, 08039A34: the hand on a chain: it waits a while (at random),
// turns toward the player (within limits), then shoots out (sound B3h) to
// the player's distance; touching the player grabs: the player is spun
// round, losing up to five rings each turn, and after five turns thrown
// off (0803A140). Without rings the grab hurts. Then it draws back.
class GrabbingHand final : public Process {
public:
    GrabbingHand(Game& g, ArmBoss& boss, const ArmBossView& b);

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 0803A5D0

    void update(Game& g) override;

    std::shared_ptr<HandView> view = std::make_shared<HandView>();
    std::shared_ptr<const ArmBossView> boss_view; // read through the hand's record

    u32 flags() const { return sprite_.flags; }

    u8 links() const { return links_; }

private:
    // 08036F6C: a touch hurts (state 0-1) or grabs (state 2; without
    // rings it hurts); Eggman laughs at a hurt.
    int contact(Game& g, s16 x, s16 y, bool grabbing);
    void hold_player(Player& p, s32 x, s32 y);
    void throw_player(Game& g, Player& p);

    Game& game_;
    ArmBoss& boss_;
    Sprite sprite_;                 // +00
    u16 angle_shown_{};             // +30 the transform
    s16 scale_x_, scale_y_ = 0x100; // +32, +34
    u16 timer_ = 0x11;              // +3C
    s32 reach_ = 0xE00;             // +40 8.8 (negative: out)
    s32 reach_limit_{};             // +44
    s16 reach_speed_ = -0x200;      // +48
    u16 angle_{};                   // +4A
    s16 spin_{};                    // +4C
    u8 links_{};                    // +60
    u8 state_{};                    // +61
    bool grabbed_{};                // +62
    bool falling_{};                // the update is 0803A43C
    FallingRotated fall_;
};

// 08037198: the third boss. The view closes in; the machine rises into
// view (0803750C) and lowers its arm; then (0803772C) it picks an attack
// at random (080370EC, by the player's distance and whether in the air):
// a bomb thrown from the arm (1), a beam swept along the floor (2), the
// grabbing hand (3), or a charge across the arena and back (4). Its body
// is hit while the arm is not raised; the arm hurts. Eight hits (six with
// the save's byte +18 set) break it (0803800C): it falls to the ground
// (08038124) and the end begins (the true ending's way to act 13 with all
// seven emeralds as Sonic, 0803A51C; else the act's end, 0803A564).
class ArmBoss final : public Process {
public:
    ArmBoss(Game& g, const ObjectSpawn& o)
        : Process(0x2000), game_(g), view_(std::make_shared<ArmBossView>()) {
        StageState& s = g.stage();
        hits_ = g.save().unknown_5178 != 0 ? 2 : 0;
        base_x_ = s16(o.record[0] * 8 + o.cell_x * 0x100);
        base_y_ = s16(o.record[1] * 8 + o.cell_y * 0x100);
        o.record[0] = 0xFE;
        vehicle_tiles_ = g.sprite_tiles.allocate(0x2A);
        vehicle_.start(vehicle_tiles_, 0x2AF, 0, base_x_, base_y_, 0x580, 0x2000);
        eggman_tiles_ = g.sprite_tiles.allocate(0xC);
        eggman_.start(eggman_tiles_, 0x2B3, 0, base_x_, base_y_, 0x540, 0x2000);
        if (!(s.player[0].flags & player_flag::dead)) {
            // The arena is lit (window 0 the whole picture, brightened
            // outside it only where the window masks allow: nowhere yet).
            auto& v = g.video().settings;
            v.windows[0].left = 0;
            v.windows[0].right = 0xF0;
            v.windows[0].top = 0;
            v.windows[0].bottom = 0xA0;
            v.inside[0] = v.inside[1] = 0x1F;
            v.outside = 0x1F;
            v.inside_sprite_window = 0x1F;
            v.blend.set_control(0xBF);
            v.blend.evy = 0xC;
            v.window0 = true;
        }
        // Sprite palette 12 all white: its blinking after a hit (the
        // palette moved by FDh) shows it.
        auto& colors = g.video().sprite_colors;
        std::fill(colors.begin() + 0xC0, colors.begin() + 0xD0, u16(0x7FFF));
        g.video().sprite_colors_changed = true;
        publish();
    }

    void on_destroy() override { // 0803A500
        game_.sprite_tiles.free(vehicle_tiles_);
        game_.sprite_tiles.free(eggman_tiles_);
    }

    void update(Game& g) override {
        switch (state_) {
        case State::waiting:
            wait(g);
            break;
        case State::arriving:
            arrive(g);
            break;
        case State::fighting:
            fight(g);
            break;
        case State::falling:
            fall(g);
            break;
        }
        publish();
    }

    // A part's hit on a player: Eggman laughs.
    void laugh() {
        eggman_.variant = 3;
        eggman_.flags &= ~sprite_flag::ended;
        eggman_.previous_variant = 0xFF;
    }

    // The hand's chain is done (+99 advances), or (after a grab) the next
    // attack comes at once (the timer +92 set to 1).
    void hand_done(bool grabbed) {
        if (!grabbed) {
            ++phase_;
        } else {
            timer_ = 1;
        }
        publish();
    }

    void set_phase(u8 phase) {
        phase_ = phase;
        publish();
    }

    s8 hits() const { return hits_; }

    std::shared_ptr<const ArmBossView> view() const { return view_; }

private:
    enum class State : u8 { waiting, arriving, fighting, falling };
    static constexpr u32 attack_odds = 0x084A5E1C; // four tables of four (percent)

    void publish() {
        ArmBossView& v = *view_;
        v.act_x = act_x_;
        v.act_y = act_y_;
        v.x = x_;
        v.y = y_;
        v.base_x = base_x_;
        v.base_y = base_y_;
        v.timer = timer_;
        v.hits = hits_;
        v.flash = flash_;
        v.armed = armed_;
        v.phase = phase_;
        v.attack = attack_;
        v.vehicle_flags = vehicle_.flags;
        v.vehicle_palette = vehicle_.palette;
    }

    // The place this frame, bobbing while not moving up or down.
    void place() {
        act_x_ = s16((x_ >> 8) + base_x_);
        act_y_ = s16((y_ >> 8) + base_y_);
        if (speed_y_ == 0) {
            bob_ = u16((bob_ + 8) & 0x3FF);
            act_y_ = s16(act_y_ + (sine(game_.assets(), bob_ + 0x100u) >> 12));
        } else {
            bob_ = 0;
        }
    }

    // 08037084: blinking (its picture's palette moved) after a hit while
    // more than 16 of its 32 frames are left.
    void show(Game& g) {
        if (flash_ != 0) {
            const u8 b = --flash_;
            if (s8(b) > 0x10 && (b & 2) == 0 && !(g.stage().player[0].flags & player_flag::dead)) {
                vehicle_.palette = 0xFD;
            }
        }
        draw(g, vehicle_);
        draw(g, eggman_);
    }

    void show_both(Game& g) {
        const auto& c = g.stage().camera;
        vehicle_.x = s16(act_x_ - c.x);
        vehicle_.y = s16(act_y_ - c.y);
        if (eggman_.variant != 0 && (eggman_.flags & sprite_flag::ended)) {
            eggman_.variant = 0;
            eggman_.previous_variant = 0xFF;
        }
        animate(g, vehicle_);
        animate(g, eggman_);
        eggman_.x = vehicle_.x;
        eggman_.y = vehicle_.y;
        show(g);
    }

    // 08036DF0: the body is hit while the arm is not raised (the attack box
    // on its first hitbox); its second hitbox hurts.
    void contact(Game& g, s16 x, s16 y) {
        StageState& s = g.stage();
        if (!(s.player[0].flags & player_flag::dead)) {
            vehicle_.palette = 0;
        }
        if (flash_ != 0) {
            return;
        }
        const auto touch = [&](Player& p) {
            int r = 0;
            if (!(arm_->status & arm_status::raised)) {
                const BossContact c = boss_contact_single(g, vehicle_, x, y, p);
                r = c == BossContact::attacked ? 1 : c == BossContact::hurt ? 2 : 0;
            }
            if (r == 0 && hazard_hits_second(g, vehicle_, x, y, p)) {
                r = 2;
            }
            return r;
        };
        const int r0 = touch(s.player[0]);
        const int r1 = s.players == 2 ? touch(s.player[1]) : 0;
        if (r0 == 1 || r1 == 1) {
            ++hits_;
            flash_ = 0x20;
            eggman_.variant = 2;
            eggman_.previous_variant = 0xFF;
            eggman_.flags &= ~sprite_flag::ended;
            g.sound().start(sound_boss_hit);
        } else if (r0 == 2 || r1 == 2) {
            eggman_.variant = 3;
            eggman_.previous_variant = 0xFF;
            eggman_.flags &= ~sprite_flag::ended;
        }
    }

    // 080373CC
    void wait(Game& g) {
        StageState& s = g.stage();
        auto& c = s.camera;
        switch (phase_) {
        case 0:
            if (base_x_ - 0x78 <= c.x) {
                c.min_x = s16(base_x_ - 0x90);
                c.max_x = s16(base_x_ + 0x90);
                phase_ = 1;
            }
            break;
        case 1:
            if (base_y_ - 0x28 <= c.y) {
                g.scheduler().create<CameraBounds>(g, s16(base_y_ - 0x28), s16(base_y_ + 0xA0));
                phase_ = 2;
            }
            break;
        case 2:
            if (c.min_y == base_y_ - 0x28 && c.max_y == base_y_ + 0xA0) {
                phase_ = 3;
            }
            break;
        case 3:
            phase_ = 0;
            speed_y_ = -0x100;
            act_x_ = s16(s16(u32(x_) >> 8) + base_x_);
            act_y_ = s16(s16(u32(y_) >> 8) + base_y_);
            publish();
            arm_ = &g.scheduler().create<BossArm>(g, *this, view_);
            state_ = State::arriving;
            shake_view(g, 0x400, 4, 0x100, 0x7A, 0x80);
            s.music_queued = 0x16;
            break;
        default:
            break;
        }
    }

    // 0803750C: up into view; the arm comes down, Eggman laughs, up again.
    void arrive(Game& g) {
        x_ += speed_x_;
        y_ += speed_y_;
        place();
        const auto& c = g.stage().camera;
        vehicle_.x = s16(act_x_ - c.x);
        vehicle_.y = s16(act_y_ - c.y);
        animate(g, vehicle_);
        animate(g, eggman_);
        eggman_.x = vehicle_.x;
        eggman_.y = vehicle_.y;
        show(g);
        switch (phase_) {
        case 0:
            if (y_ > 0x6E00) {
                return;
            }
            y_ = 0x6E00;
            speed_x_ = speed_y_ = 0;
            timer_ = 0xF;
            break;
        case 1:
            if (--timer_ != 0) {
                return;
            }
            arm_->command = 1;
            break;
        case 2:
            if (arm_->command != 4) {
                return;
            }
            eggman_.variant = 1;
            eggman_.previous_variant = 0xFF;
            break;
        case 3:
            if (!(eggman_.flags & sprite_flag::ended)) {
                return;
            }
            arm_->command = 5;
            eggman_.variant = 0;
            eggman_.previous_variant = 0xFF;
            break;
        case 4:
            if (arm_->command != 7) {
                return;
            }
            timer_ = 0xF;
            break;
        case 5:
            if (--timer_ == 0) {
                armed_ = true;
                phase_ = 0;
                state_ = State::fighting;
            }
            return;
        default:
            return;
        }
        ++phase_;
    }

    // 080370EC: the attack (1-4): a table of four chances (percent) by
    // whether the player is in the air and more than 78h pixels away.
    u8 choose(Game& g, s16 x) {
        StageState& s = g.stage();
        const Assets& a = g.assets();
        const Player& p = s.player[0];
        s16 d = s16((p.x >> 8) - x);
        if (d < 0) {
            d = s16(-d);
        }
        u32 table = attack_odds + ((p.flags & player_flag::in_air) ? 8 : 0);
        if (d > 0x78) {
            table += 0x10;
        }
        s32 roll = s32(s.next_random() % 100u);
        u8 i = 0;
        s16 chance = a.s16(table);
        while ((roll -= chance) > 0) {
            if (++i >= 4) {
                break;
            }
            chance = a.s16(table + i * 2u);
        }
        return u8(i + 1);
    }

    // Snaps to `limit` once past it (in the direction it faces); true then.
    bool reached(s32 limit) {
        if (!(vehicle_.flags & sprite_flag::flip_x)) {
            if (x_ < limit + 1) {
                x_ = limit;
                return true;
            }
            return false;
        }
        if (x_ < -limit) {
            return false;
        }
        x_ = -limit;
        return true;
    }

    // Snaps to the arena's far end (D000h), turning round; true then.
    bool reached_end() {
        s32 at;
        if (!(vehicle_.flags & sprite_flag::flip_x)) {
            if (x_ >= -0xCFFF) {
                return false;
            }
            at = -0xD000;
        } else {
            if (x_ < 0xD000) {
                return false;
            }
            at = 0xD000;
        }
        x_ = at;
        vehicle_.flags ^= sprite_flag::flip_x;
        eggman_.flags ^= sprite_flag::flip_x;
        return true;
    }

    // The beam (attack 2): window 0 shows a band of the picture beside the
    // machine (its brightening outside), and the band hurts.
    void sweep_beam(Game& g) {
        StageState& s = g.stage();
        auto& v = g.video().settings;
        const bool shows = !(s.player[0].flags & player_flag::dead) || s.lives != 0;
        if (--timer_ == 0) {
            phase_ = 4;
            if (shows) {
                v.windows[0] = {0, 0xF0, 0, 0xA0};
                v.inside[0] = v.inside[1] = 0x1F;
            }
            return;
        }
        if (shows) {
            v.inside[0] = v.inside[1] = 0x3F;
        }
        s16 d = s16(timer_ - 0xF);
        if (d < 0) {
            d = s16(-d);
        }
        s16 half = s16(0xF - d);
        u32 h = u32(0xF - d) & 0xFFFF;
        if (half > 0xB) {
            half = 0xC;
            h = 0xB;
        }
        const s32 top = vehicle_.y - half;
        u16 bottom = u16(half + vehicle_.y);
        if (s16(bottom) > 0xA0) {
            bottom = 0xA0;
        }
        u32 box;
        if (!(vehicle_.flags & sprite_flag::flip_x)) {
            if (shows) {
                v.windows[0].left = u8(u16(vehicle_.x - 0x20) >> 8);
                v.windows[0].right = u8(vehicle_.x - 0x20);
                v.windows[0].top = u8(top);
                v.windows[0].bottom = u8(bottom);
            }
            box = (u32((vehicle_.x - 0x98) & 0xFF) << 16) | 0x88 | (u32(u8(-s32(h))) << 8) |
                  (h << 24);
        } else {
            if (shows) {
                v.windows[0].left = u8(vehicle_.x + 0x20);
                v.windows[0].right = 0xF0;
                v.windows[0].top = u8(top);
                v.windows[0].bottom = u8(bottom);
            }
            box = u32((vehicle_.x - 0x58) & 0xFF) | 0x780000 | (u32(u8(-s32(h))) << 8) | (h << 24);
        }
        const s16 bx = s16(s.camera.x + 0x78);
        const bool hit0 = beam_hits(g, box, bx, act_y_, s.player[0]);
        const bool hit1 = s.players == 2 && beam_hits(g, box, bx, act_y_, s.player[1]);
        if (hit0 || hit1) {
            eggman_.variant = 3;
            eggman_.flags &= ~sprite_flag::ended;
            eggman_.previous_variant = 0xFF;
        }
    }

    // 0800C1E8: a box (packed: left, top, right, bottom as signed bytes)
    // at (x, y) meeting the player's body hurts.
    static bool beam_hits(Game& g, u32 box, s16 x, s16 y, Player& p) {
        const Hitbox& body = p.body->sprite.hitboxes[0];
        if ((p.flags & player_flag::dead) || body.index == 0xFFFFFFFF) {
            return false;
        }
        Hitbox h;
        h.index = 0;
        h.left = s8(box);
        h.top = s8(box >> 8);
        h.right = s8(box >> 16);
        h.bottom = s8(box >> 24);
        if (!hitboxes_meet(x, y, h, p.x >> 8, p.y >> 8, body)) {
            return false;
        }
        hurt_player(g, p);
        return true;
    }

    // 0803772C
    void fight(Game& g) {
        StageState& s = g.stage();
        speed_x_ += accel_x_;
        x_ = (vehicle_.flags & sprite_flag::flip_x) ? x_ - speed_x_ : x_ + speed_x_;
        y_ += speed_y_;
        place();
        contact(g, act_x_, act_y_);
        if (hits_ > 7) {
            broken(g);
            return;
        }
        show_both(g);
        switch (phase_) {
        case 0: {
            attack_ = 0;
            arm_->command = 8;
            phase_ = 1;
            switch ((s.next_random() & 0x30) >> 4) {
            case 1:
                timer_ = 0xB4;
                break;
            case 2:
                timer_ = 0xF0;
                break;
            default:
                timer_ = 0x5A;
                break;
            }
            choose_next(g);
            return;
        }
        case 1:
            choose_next(g);
            return;
        case 2: {
            const u8 st = arm_->status;
            arm_->status = st & arm_status::posed;
            if (!(st & arm_status::posed)) {
                return;
            }
            if (--timer_ == 0) {
                timer_ = 10;
                ++phase_;
                arm_->command = 10;
            }
            return;
        }
        case 3:
            if (!(arm_->status & arm_status::down) || --timer_ != 0) {
                return;
            }
            arm_->command = 0xD;
            phase_ = u8(attack_ << 4);
            if (attack_ == 2) {
                timer_ = 0x1E;
                g.sound().start(0xAC);
            } else if (attack_ == 1) {
                timer_ = 0;
            }
            return;
        case 4:
            arm_->command = 0xF;
            attack_ = 0;
            phase_ = 5;
            return;
        case 5:
            if (arm_->status & arm_status::raised) {
                phase_ = 0;
            }
            return;
        case 0x10: // a bomb
            publish();
            g.scheduler().create<ArmBomb>(g, *this, *view_);
            ++timer_;
            speed_x_ = 0x200;
            accel_x_ = -0x40;
            phase_ = 0x11;
            return;
        case 0x11: // the recoil, back to its place
            if (speed_x_ < 1) {
                accel_x_ -= quotient(accel_x_, 4);
            }
            if (!reached(0x7400)) {
                return;
            }
            speed_x_ = accel_x_ = 0;
            phase_ = timer_ == 0 ? 0x10 : 4;
            return;
        case 0x20:
            sweep_beam(g);
            return;
        case 0x30: // the hand
            publish();
            g.scheduler().create<GrabbingHand>(g, *this, *view_);
            phase_ = 0x31;
            return;
        case 0x32:
            phase_ = 4;
            return;
        case 0x33: // thrown off: off across the arena, turning at its end
            speed_x_ = -0x200;
            timer_ = 0;
            phase_ = 0x34;
            return;
        case 0x34:
        case 0x41:
            if (phase_ == 0x34 && timer_ != 0) {
                arm_->command = 0xF;
                timer_ = 0;
            }
            if (!reached_end()) {
                return;
            }
            if (phase_ == 0x41) {
                speed_x_ = -0x200;
                arm_->command = 0x13;
            }
            ++phase_;
            return;
        case 0x35:
            if (!reached(0x7400)) {
                return;
            }
            phase_ = 0;
            speed_x_ = 0;
            return;
        case 0x40: // the charge
            if (!(arm_->status & arm_status::down) || --timer_ != 0) {
                return;
            }
            speed_x_ = -0x300;
            phase_ = 0x41;
            return;
        case 0x42:
            if (!(vehicle_.flags & sprite_flag::flip_x)) {
                if (x_ >= 0x7401) {
                    return;
                }
                x_ = 0x7400;
                speed_x_ = 0;
            } else {
                if (x_ <= -0x7401) {
                    return;
                }
                x_ = -0x7400;
                speed_x_ = 0;
            }
            if (arm_->status & arm_status::raised) {
                phase_ = 0;
            }
            return;
        default:
            return;
        }
    }

    // Phase 1: when the wait is over, the attack (the charge goes at once).
    void choose_next(Game& g) {
        if (--timer_ != 0) {
            return;
        }
        attack_ = choose(g, act_x_);
        if (attack_ == 4) {
            arm_->command = 10;
            timer_ = 0x14;
            phase_ = 0x40;
        } else {
            timer_ = 10;
            phase_ = 2;
        }
    }

    // 0803800C: the last hit: the view stops, the player is held still
    // (the act is over), points.
    void broken(Game& g) {
        StageState& s = g.stage();
        phase_ = 0;
        speed_x_ = speed_y_ = 0;
        eggman_.variant = 2;
        eggman_.previous_variant = 0xFF;
        s.camera.min_x = s.camera.x;
        s.camera.max_x = s16(s.camera.x + 0xF0);
        g.sound().start(sound_boss_broken);
        vehicle_.flags &= ~sprite_flag::mode;
        Player& p = s.player[0];
        if (!(p.flags & player_flag::dead)) {
            auto& v = g.video().settings;
            v.outside = 0;
            v.inside_sprite_window = 0;
            v.blend.set_control(0);
            v.blend.evy = 0;
            v.window0 = v.window1 = v.sprite_window = false;
        }
        p.ground_speed = 0;
        p.flags |= player_flag::frozen;
        p.held = 0;
        p.pressed = 0;
        s.flags |= stage_flag::paused | stage_flag::frozen;
        const u32 before = s.score;
        s.score += 1000;
        if (divide(s32(s.score), 50000) != divide(s32(before), 50000) && g.game_mode == 0) {
            ++s.lives;
        }
        state_ = State::falling;
        fall(g);
    }

    // Whether the true ending follows (as the results decide it too).
    static bool true_ending(Game& g) {
        const StageState& s = g.stage();
        const SaveData& save = g.save();
        return s.character == 0 && s.zone > 0xB && save.emeralds == 0x7F &&
               std::all_of(save.zones_reached.begin(), save.zones_reached.end(),
                           [](u16 z) { return z > 0xC; }) &&
               (save.zones_reached[0] != 0xF || s.unknown_5124 != 0xC);
    }

    // 08038124: falls, smoking, bouncing once onto the ground; the end
    // begins; four seconds later (the true ending's way) Eggman flies off.
    void fall(Game& g);

    Game& game_;
    std::shared_ptr<ArmBossView> view_;
    BossArm* arm_{}; // +9C
    State state_ = State::waiting;
    s16 base_x_, base_y_;                  // +88, +8A
    u32 vehicle_tiles_{}, eggman_tiles_{}; // +10, +48
    Sprite vehicle_;                       // +0C
    Sprite eggman_;                        // +44
    s32 x_ = 0x7400, y_ = 0xE800;          // +74, +78 8.8 from its place
    s32 speed_x_{}, speed_y_{};            // +7C, +80
    s32 accel_x_{};                        // +84
    s16 act_x_{}, act_y_{};                // +8C, +8E
    u16 bob_{};                            // +90
    u16 timer_ = 0x78;                     // +92
    s8 hits_{};                            // +94
    u8 flash_{};                           // +95
    bool armed_{};                         // +98
    u8 phase_{};                           // +99
    u8 attack_{};                          // +9A
};

BossArm::BossArm(Game& g, ArmBoss& boss, std::shared_ptr<const ArmBossView> view)
    : Process(0x2001), game_(g), boss_(boss), view_(std::move(view)) {
    at_x_ = view_->act_x;
    at_y_ = view_->act_y;
    upper_.start(g.sprite_tiles.allocate(0xC), 0x2B0, 8, 0, 0, 0x500, 0x2000);
    lower_.start(g.sprite_tiles.allocate(0xC), 0x2B0, 0xB, 0, 0, 0x500, 0x2000);
}

void BossArm::turn_to(u8 pose) {
    if (upper_.flags & sprite_flag::ended) {
        if (upper_.variant == pose) {
            status |= arm_status::posed;
            return;
        }
        switch (upper_.variant) {
        case 8:
        case 0xE:
            upper_.variant = 0xA;
            lower_.variant = 0xD;
            break;
        case 9:
        case 0xF:
            upper_.variant = 8;
            lower_.variant = 0xB;
            break;
        case 0xA:
        case 0x10:
            upper_.variant = 9;
            lower_.variant = 0xC;
            break;
        default:
            break;
        }
        upper_.previous_variant = lower_.previous_variant = 0xFF;
    }
    status &= u8(~arm_status::posed);
}

void BossArm::update(Game& g) {
    StageState& s = g.stage();
    const ArmBossView& b = *view_;
    upper_.flags = (upper_.flags & ~sprite_flag::flip_x) | (b.vehicle_flags & sprite_flag::flip_x);
    lower_.flags = (lower_.flags & ~sprite_flag::flip_x) | (b.vehicle_flags & sprite_flag::flip_x);
    upper_.palette = lower_.palette = b.vehicle_palette != 0 ? 0xFE : 0;
    at_x_ = b.act_x;
    at_y_ = b.act_y;
    if (b.hits > 7) {
        if (command != 0) {
            if (upper_.variant != 6) {
                close_hand();
            }
            command = 0;
        }
        speed_x_ = speed_y_ = 0;
        if (b.phase > 1) {
            g.scheduler().destroy(*this);
            return;
        }
    } else {
        bool next = false;
        switch (command) {
        case 1: // lowered once it shows its first pose
            pose_ = 8;
            if (status & arm_status::posed) {
                timer_ = 0x1E;
                next = true;
            }
            break;
        case 2:
            if (--timer_ == 0) {
                speed_y_ = 0x100;
                next = true;
            }
            break;
        case 0xA:
            speed_y_ = 0x200;
            ++command;
            [[fallthrough]];
        case 3:
        case 0xB:
            if (y_ + speed_y_ >= 0) {
                y_ = 0;
                speed_y_ = 0;
                next = true;
            }
            break;
        case 5:
            speed_y_ = -0x100;
            next = true;
            break;
        case 6:
        case 0x11:
        case 0x15:
            if (y_ + speed_y_ <= -0x1700) {
                y_ = -0x1700;
                speed_y_ = 0;
                next = true;
            }
            break;
        case 8:
            y_ = -0x1700;
            pose_ = 0;
            ++command;
            [[fallthrough]];
        case 9: // turning to the attack's pose
            switch (b.attack) {
            case 2:
                pose_ = 9;
                break;
            case 1:
                pose_ = 8;
                break;
            case 3:
                pose_ = 0xA;
                break;
            default:
                break;
            }
            break;
        case 0xD:
            attack_pose(b.attack);
            next = true;
            break;
        case 0xF:
            close_hand();
            timer_ = 10;
            ++command;
            [[fallthrough]];
        case 0x10:
            if (--timer_ == 0) {
                speed_y_ = -0x200;
                next = true;
            }
            break;
        case 0x13:
            ++command;
            speed_y_ = -0x200;
            next = true;
            break;
        default:
            break;
        }
        if (next) {
            ++command;
        }
    }
    x_ += speed_x_;
    y_ += speed_y_;
    status = y_ < -0x16FF ? u8(status | arm_status::raised) : u8(status & ~arm_status::raised);
    status = y_ < 0 ? u8(status & ~arm_status::down) : u8(status | arm_status::down);
    const s16 x = s16((x_ >> 8) + at_x_), y = s16((y_ >> 8) + at_y_);
    upper_.x = lower_.x = s16(x - s.camera.x);
    upper_.y = lower_.y = s16(y - s.camera.y);
    turn_to(pose_);
    if (b.armed && b.flash == 0 && b.hits < 8) {
        const bool hit0 = hazard_hits(g, upper_, x, y, s.player[0]);
        const bool hit1 = s.players == 2 && hazard_hits(g, upper_, x, y, s.player[1]);
        if (hit0 || hit1) {
            boss_.laugh();
        }
    }
    animate(g, upper_);
    animate(g, lower_);
    draw(g, upper_);
    draw(g, lower_);
}

ArmBomb::ArmBomb(Game& g, ArmBoss& boss, const ArmBossView& b)
    : Process(0x2100), game_(g), boss_(boss) {
    const bool fast = g.stage().next_random() & 0x800;
    base_x_ = b.base_x;
    base_y_ = b.base_y;
    x_ = b.x;
    y_ = b.y;
    s16 speed;
    if (!fast) {
        gravity_ = 0x30;
        speed_y_ = -0x500;
        speed = 0x180;
    } else {
        gravity_ = 0x28;
        speed_y_ = -0x200;
        speed = 0x300;
    }
    if (!(b.vehicle_flags & sprite_flag::flip_x)) {
        x_ -= 0x1400;
        speed_x_ = s16(-speed);
    } else {
        x_ += 0x1400;
        speed_x_ = speed;
    }
    sprite_.start(g.sprite_tiles.allocate(4), 0x2B2, 1, 0, 0, 0x5C0, 0x2000);
    g.sound().start(0x91);
}

void ArmBomb::update(Game& g) {
    StageState& s = g.stage();
    speed_y_ = s16(speed_y_ + gravity_);
    x_ += speed_x_;
    y_ += speed_y_;
    const s16 x = s16((x_ >> 8) + base_x_);
    s16 y = s16((y_ >> 8) + base_y_);
    const s32 d = floor_distance(g, x, s16(y + 8));
    if (d < 0) {
        y_ += d * 0x100;
        speed_y_ = s16(quotient(speed_y_, 4) - speed_y_);
    }
    if (boss_.hits() < 8) {
        const bool hit0 = hazard_hits(g, sprite_, x, y, s.player[0]);
        const bool hit1 = s.players == 2 && hazard_hits(g, sprite_, x, y, s.player[1]);
        if (hit0 || hit1) {
            boss_.laugh();
        }
    }
    sprite_.x = s16(x - s.camera.x);
    sprite_.y = s16(y - s.camera.y);
    animate(g, sprite_);
    draw(g, sprite_);
    if (u32(x_ + 0xD000) > 0x1A000) {
        g.scheduler().destroy(*this);
    }
}

GrabbingHand::GrabbingHand(Game& g, ArmBoss& boss, const ArmBossView& b)
    : Process(0x2100), boss_view(boss.view()), game_(g), boss_(boss) {
    const bool flipped = b.vehicle_flags & sprite_flag::flip_x;
    HandView& v = *view;
    v.base_x = s16(s16(u32(b.x) >> 8) + b.base_x + (flipped ? 0x1A : -0x1A));
    v.base_y = s16(s16(u32(b.y) >> 8) + b.base_y);
    v.reach = reach_;
    sprite_.start(g.sprite_tiles.allocate(0x40), 0x2B1, 0, 0, 0, 0x5C0,
                  (flipped ? sprite_flag::flip_x : 0) | 0x2030);
    scale_x_ = flipped ? -0x100 : 0x100;
}

int GrabbingHand::contact(Game& g, s16 x, s16 y, bool grabbing) {
    StageState& s = g.stage();
    int r0;
    if (!grabbing) {
        r0 = hazard_hits(g, sprite_, x, y, s.player[0]) ? 2 : 0;
    } else {
        // 0800C2B8: a touch only (not while the player blinks).
        const Player& p = s.player[0];
        const Hitbox& body = p.body->sprite.hitboxes[0];
        r0 = sprite_.hitboxes[0].index != 0xFFFFFFFF && !(p.flags & player_flag::dead) &&
                     p.hurt_timer == 0 && body.index != 0xFFFFFFFF &&
                     hitboxes_meet(x, y, sprite_.hitboxes[0], p.x >> 8, p.y >> 8, body)
                 ? 2
                 : 0;
    }
    const int r1 = s.players == 2 && hazard_hits(g, sprite_, x, y, s.player[1]) ? 2 : 0;
    if (r0 == 2 || r1 == 2) {
        boss_.laugh();
    }
    if (grabbing && r0 == 2 && s.rings == 0) {
        hurt_player(g, s.player[0]);
        r0 = 0;
    }
    return r0;
}

void GrabbingHand::hold_player(Player& p, s32 x, s32 y) {
    p.x = x * 0x100;
    p.y = y * 0x100;
    p.angle = u8((sprite_.flags & sprite_flag::flip_x) ? (-s32(s16(angle_)) >> 2) : (angle_ >> 2));
}

// 0803A140: thrown off, hurt-blinking, up and away from the machine.
void GrabbingHand::throw_player(Game& g, Player& p) {
    p.hurt_timer = 0x78;
    p.speed_x = (sprite_.flags & sprite_flag::flip_x) ? -0x200 : 0x200;
    p.speed_y = -0x5E0;
    p.angle = 0;
    p.flags = (p.flags & 0xFF1FFAC3u) | player_flag::in_air;
    p.state = 0xF;
    set_player_radius(g, p, 6, 0xE);
    p.radius_x = 6;
    p.radius_y = 0xE;
    p.special = 0;
    p.move = 0;
}

void GrabbingHand::update(Game& g) {
    StageState& s = g.stage();
    const Assets& a = g.assets();
    HandView& v = *view;
    const bool flipped = sprite_.flags & sprite_flag::flip_x;
    if (falling_) {
        // 0803A43C
        fall_.speed_y = s16(fall_.speed_y + 0x20);
        fall_.y += fall_.speed_y;
        const s16 x = s16(fall_.x >> 8), y = s16(fall_.y >> 8);
        const s32 d = floor_distance(g, x, s16(y + 8));
        if (d < 0) {
            fall_.y += d * 0x100;
            fall_.speed_y = s16((fall_.speed_y >> 2) - fall_.speed_y);
        }
        animate(g, sprite_);
        transform(g, sprite_, angle_shown_, scale_x_, scale_y_, s16(x - s.camera.x),
                  s16(y - s.camera.y));
        draw(g, sprite_);
        if (--fall_.timer == 0) {
            create_smoke(g, s32(x) << 8, s32(y) << 8);
            g.scheduler().destroy(*this);
        }
        return;
    }
    const ArmBossView& b = *boss_view;
    if (b.hits < 8) {
        v.base_x = s16(b.act_x + (flipped ? 0x1A : -0x1A));
        v.base_y = b.act_y;
    }
    reach_ += reach_speed_;
    v.reach = reach_;
    v.previous_angle = angle_;
    u16 turned = angle_;
    angle_shown_ = angle_;
    if (flipped) {
        turned = u16((-s32(angle_) - 0x200) & 0x3FF);
        angle_shown_ = u16(-s32(angle_) & 0x3FF);
    }
    const s32 x = ((reach_ * (sine(a, u32(s16(turned) + 0x100)) >> 6)) >> 16) + v.base_x;
    const s32 y = ((reach_ * (sine(a, s16(turned)) >> 6)) >> 16) + v.base_y;
    animate(g, sprite_);
    transform(g, sprite_, angle_shown_, scale_x_, scale_y_, s16(x - s.camera.x),
              s16(y - s.camera.y));
    draw(g, sprite_);
    if (b.hits > 7) {
        reach_speed_ = 0;
        fall_.x = x * 0x100;
        fall_.y = y * 0x100;
        sprite_.layer = 0x5C0;
        fall_.timer = 0x50;
        falling_ = true;
        return;
    }
    int r = 0;
    if (state_ < 2) {
        r = contact(g, s16(x), s16(y), false);
    } else if (state_ == 2) {
        r = contact(g, s16(x), s16(y), true);
    }
    Player& p = s.player[0];
    switch (state_) {
    case 0: // waiting a while (at random)
        if (--timer_ == 0) {
            const s32 m = s32(s.next_random()) % 100;
            reach_speed_ = 0;
            ++state_;
            timer_ = m <= 0x23 ? 0xB4 : m <= 0x46 ? 0x3C : m <= 0x5A ? 0x12 : 300;
        }
        break;
    case 1: { // turning toward the player
        s32 dx = v.base_x - (p.x >> 8);
        if (dx < 0) {
            dx = -dx;
        }
        u16 target = u16(octant_angle(a, s16(v.base_y - (p.y >> 8)), s16(dx)));
        if (u16(target - 0xE1) <= 0x11E) {
            target = 0xE0;
        }
        if (u16(target - 0x201) <= 0x11E) {
            target = 0x320;
        }
        const s32 cur = s16(u32(angle_) << 6), tgt = s16(u32(target) << 6);
        if (tgt < cur) {
            angle_ = cur < tgt + 0x180 ? target : u16(angle_ - 6);
        } else if (cur < tgt) {
            angle_ = tgt - 0x180 < cur ? target : u16(angle_ + 6);
        }
        angle_ &= 0x3FF;
        if (--timer_ == 0) {
            const s32 ddx = v.base_x - (p.x >> 8), ddy = v.base_y - (p.y >> 8);
            const u16 dist = square_root(u32(ddx * ddx * 0x100 + ddy * ddy * 0x100));
            reach_limit_ = -s32(dist) * 0x10 - 0x1000;
            reach_speed_ = -0x600;
            links_ = 0;
            ++state_;
            sprite_.layer = 0x3C0;
            sprite_.variant = 1;
            sprite_.previous_variant = 0xFF;
            g.sound().start(0xB3);
        }
        break;
    }
    case 2: { // shooting out, its chain growing
        const u8 n = u8(((((-reach_) >> 8) - 0x14) >> 4) + 1);
        if (links_ < n) {
            links_ = n;
            g.scheduler().create<HandLink>(g, *this);
        }
        if (r == 2) {
            grabbed_ = true;
            reach_speed_ = 0;
            state_ = 7;
            sprite_.variant = 2;
            sprite_.previous_variant = 0xFF;
            end_player_moves(g, p);
            hold_player(p, x, y);
            p.speed_x = p.speed_y = p.ground_speed = 0;
            p.flags |= 0xE00100;
            p.state = 0x28;
            p.held = p.pressed = 0;
        } else if (reach_ <= reach_limit_) {
            reach_speed_ = 0;
            ++state_;
            sprite_.variant = 2;
            sprite_.previous_variant = 0xFF;
        }
        break;
    }
    case 3:
        if (sprite_.flags & sprite_flag::ended) {
            reach_speed_ = 0x200;
            ++state_;
        }
        break;
    case 4:
        if (reach_ >= -0x1400) {
            reach_ = -0x1400;
            reach_speed_ = 0;
            sprite_.layer = 0x5C0;
            sprite_.variant = 0;
            sprite_.previous_variant = 0xFF;
            ++state_;
        }
        break;
    case 5: // turned back level, then drawn in
        if (angle_ == 0) {
            sprite_.layer = 0x5C0;
            reach_speed_ = 0x100;
            ++state_;
        } else if (s16(angle_) < 0x201) {
            angle_ = u16((angle_ - 6) & 0x3FF);
            if (angle_ >= 0x201) {
                angle_ = 0;
            }
        } else {
            angle_ = u16((angle_ + 6) & 0x3FF);
            if (angle_ <= 6) {
                angle_ = 0;
            }
        }
        break;
    case 6:
        if (reach_ > 0xDFF) {
            boss_.hand_done(grabbed_);
            g.scheduler().destroy(*this);
        }
        break;
    case 7: // the grabbed player is drawn in
        hold_player(p, x, y);
        if (sprite_.flags & sprite_flag::ended) {
            reach_speed_ = 0x600;
            ++state_;
        }
        break;
    case 8:
        hold_player(p, x, y);
        if (reach_ >= -0x2400) {
            reach_ = -0x2400;
            reach_speed_ = 0;
            spin_ = 0x10;
            timer_ = 0;
            ++state_;
        }
        break;
    case 9: // swung to and fro, losing rings; thrown off after five swings
        hold_player(p, x, y);
        angle_ = u16((spin_ + angle_) & 0x3FF);
        if (angle_ > 0x1FF) {
            if (s.rings != 0 && spin_ < 0) {
                const u16 n = std::min<u16>(s.rings, 5);
                scatter_rings(g, p.x >> 8, p.y >> 8, n);
                s.rings = u16(s.rings - n);
            }
            spin_ = 0x10;
        }
        if (u16(angle_ - 0x100) < 0x100) {
            spin_ = -0x10;
            ++timer_;
            if (timer_ == 4) {
                boss_.set_phase(0x33);
            }
            if (timer_ == 5) {
                throw_player(g, p);
                state_ = 5;
            }
        }
        break;
    default:
        break;
    }
    v.state = state_;
}

HandLink::HandLink(Game& g, GrabbingHand& hand)
    : Process(0x2101), game_(g), hand_view_(hand.view), boss_(hand.boss_view),
      index_(hand.links()) {
    base_x_ = hand.view->base_x;
    base_y_ = hand.view->base_y;
    const bool flipped = hand.flags() & sprite_flag::flip_x;
    sprite_.start(g.sprite_tiles.allocate(4), 0x2B2, 0, 0, 0, 0x600,
                  (flipped ? sprite_flag::flip_x : 0) | 0x2030);
    scale_x_ = flipped ? -0x100 : 0x100;
}

void HandLink::update(Game& g) {
    StageState& s = g.stage();
    const Assets& a = g.assets();
    if (falling_) {
        // 0803A43C
        fall_.speed_y = s16(fall_.speed_y + 0x20);
        fall_.y += fall_.speed_y;
        const s16 x = s16(fall_.x >> 8), y = s16(fall_.y >> 8);
        const s32 d = floor_distance(g, x, s16(y + 8));
        if (d < 0) {
            fall_.y += d * 0x100;
            fall_.speed_y = s16((fall_.speed_y >> 2) - fall_.speed_y);
        }
        animate(g, sprite_);
        transform(g, sprite_, angle_, scale_x_, scale_y_, s16(x - s.camera.x), s16(y - s.camera.y));
        draw(g, sprite_);
        if (--fall_.timer == 0) {
            create_smoke(g, s32(x) << 8, s32(y) << 8);
            g.scheduler().destroy(*this);
        }
        return;
    }
    const HandView& h = *hand_view_;
    base_x_ = h.base_x;
    base_y_ = h.base_y;
    u16 turned = h.previous_angle, shown = h.previous_angle;
    if (sprite_.flags & sprite_flag::flip_x) {
        turned = u16((-s32(shown) - 0x200) & 0x3FF);
        shown = u16(-s32(shown) & 0x3FF);
    }
    reach_ = h.reach + (index_ * 0x10 + 8) * 0x100;
    const s32 x = ((reach_ * (sine(a, u32(s16(turned) + 0x100)) >> 6)) >> 16) + base_x_;
    const s32 y = ((reach_ * (sine(a, s16(turned)) >> 6)) >> 16) + base_y_;
    angle_ = shown;
    animate(g, sprite_);
    transform(g, sprite_, angle_, scale_x_, scale_y_, s16(x - s.camera.x), s16(y - s.camera.y));
    draw(g, sprite_);
    if (reach_ > 0x800 || h.state == 6) {
        g.scheduler().destroy(*this);
    }
    if (boss_->hits > 7) {
        fall_.speed_y = 0;
        fall_.x = x * 0x100;
        fall_.y = y * 0x100;
        fall_.timer = u16(0x50 - index_ * 4);
        falling_ = true;
    }
}

// 08038BF0 / 08038CFC, 08038E04: a picture flying in from a fixed place in
// the act (EF4h, 120h), shrinking (rotated set 16), for ten seconds.
class ShrinkingPicture final : public Process {
public:
    ShrinkingPicture(Game& g, u32 tiles, u16 animation, s16 speed_x, s16 speed_y)
        : Process(0x2100), game_(g), speed_x_(speed_x), speed_y_(speed_y) {
        sprite_.start(g.sprite_tiles.allocate(tiles), animation, 0, 0, 0, 0x5C0, 0x2030);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 0803A5D0

    void update(Game& g) override {
        const auto& c = g.stage().camera;
        x_ += speed_x_;
        y_ += speed_y_;
        const s16 sx = s16(s16((x_ >> 8) + 0xEF4) - c.x), sy = s16(s16((y_ >> 8) + 0x120) - c.y);
        scale_ = s16(scale_ + growth_);
        if (scale_ < 0x801) {
            scale_ = 0x800;
        }
        animate(g, sprite_);
        transform(g, sprite_, 0, s16(scale_ >> 4), s16(scale_ >> 4), sx, sy);
        draw(g, sprite_);
        if (--timer_ == 0) {
            g.scheduler().destroy(*this);
        }
    }

private:
    Game& game_;
    Sprite sprite_;         // +00
    u16 timer_ = 600;       // +3C
    s16 scale_ = 0x1000;    // +4A
    s16 growth_ = -1;       // +4C
    s32 x_{}, y_{};         // +54, +58 8.8
    s16 speed_x_, speed_y_; // +5C, +5E
};

// 08038B08, 08038B98: a sparkle on the player for 15 frames.
class PlayerSparkle final : public Process {
public:
    explicit PlayerSparkle(Game& g) : Process(0x2100), game_(g) {
        sprite_.start(g.sprite_tiles.allocate(0x38), 0x18E, 1, 0, 0, 0x440, 0x2000);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 0803A5BC

    void update(Game& g) override {
        const StageState& s = g.stage();
        sprite_.x = s16(u32(s.player[0].x) >> 8) - s.camera.x;
        sprite_.y = s16(u32(s.player[0].y) >> 8) - s.camera.y;
        animate(g, sprite_);
        draw(g, sprite_);
        if (--timer_ == 0) {
            g.scheduler().destroy(*this);
        }
    }

private:
    Game& game_;
    Sprite sprite_;   // +00
    u16 timer_ = 0xF; // +30
};

// 080383F0: an explosion piece thrown up from 16 pixels below (x, y), and
// smoke above it (the X-Zone bosses' pieces, in front: layer 5C0h).
void x_zone_explosion(Game& g, s16 x, s16 y) {
    create_explosion_piece(g, x, y, -0x10, 5, 0x5C0, -0x20);
}

// 080388EC: the act's end after the last boss: explosions all over the
// view, faster and faster, and at the player; the player walks off right,
// and from 3072 pixels on the picture whitens over two seconds; then the
// results.
class ExplodingEnd final : public Process {
public:
    ExplodingEnd() : Process(0x1FFF) {}

    void update(Game& g) override {
        StageState& s = g.stage();
        auto& c = s.camera;
        Player& p = s.player[0];
        if ((++ticks_ & 3) == 0) {
            const s32 dx = s32(s.next_random()) % 0xF0;
            const s32 dy = s32(s.next_random()) % 0xA0;
            x_zone_explosion(g, s16(c.x + dx), s16(c.y + dy));
        }
        if (interval_ <= ++counter_) {
            counter_ = 0;
            interval_ = u16(interval_ - 2);
            if (interval_ < 4) {
                interval_ = 4;
            }
            x_zone_explosion(g, s16((p.x >> 8) - 0x30), s16(p.y >> 8));
        }
        auto& v = g.video().settings;
        switch (phase_) {
        case 0:
            if (timer_ != 0) {
                --timer_;
                return;
            }
            if (p.flags & player_flag::in_air) {
                return;
            }
            p.held = button::right;
            c.max_x = s16(s.map.width);
            break;
        case 1:
            if (p.x < 0xC0000) {
                return;
            }
            v.inside[0] = v.inside[1] = 0x3F;
            v.windows[0] = {0, 0xF0, 0, 0xA0};
            v.blend.set_control(0xBF);
            v.blend.evy = 0;
            v.window1 = false;
            v.window0 = true;
            timer_ = 0;
            break;
        case 2:
            if (++timer_ > 0x80) {
                p.held = 0;
                p.speed_x = p.speed_y = p.ground_speed = 0;
                p.flags |= player_flag::hidden;
                g.scheduler().destroy_range(0x2000, 0x2FFF);
                start_results(g, s.rings, u32(s.clock));
                g.scheduler().destroy(*this);
                v.blend.set_control(u16(v.blend.control() & 0xFFEF));
                return;
            }
            v.blend.evy = u8(timer_ >> 3);
            c.shift_x = s16(-(timer_ >> 1));
            return;
        default:
            return;
        }
        ++phase_;
    }

private:
    u16 interval_ = 0x20; // +00 frames between the explosions at the player
    u16 counter_{};       // +02
    u16 ticks_{};         // +04
    u16 timer_ = 0x78;    // +06
    u8 phase_{};          // +08
};

// 08038524: the true ending's way out of the act (Sonic with all the
// emeralds): Sonic walks right to 0E60h, turns; the emeralds fly in
// (08038BF0, 08038CFC), the results; then he turns super (states 34h,
// 32h, 33h), rises and flies off, the picture whitens and act 13 begins.
class TrueEndingStart final : public Process {
public:
    TrueEndingStart() : Process(0x1FFF) {}

    void update(Game& g) override {
        StageState& s = g.stage();
        auto& c = s.camera;
        Player& p = s.player[0];
        auto& v = g.video().settings;
        if (g.game_mode >= 2) {
            throw Unrecovered("the true ending's start in multiplayer (08038524)");
        }
        switch (phase_) {
        case 0:
            if (timer_ != 0) {
                --timer_;
                return;
            }
            if (p.flags & player_flag::in_air) {
                return;
            }
            p.held = button::right;
            c.max_x = s16(s.map.width);
            break;
        case 1:
            if (p.x > 0xE5FFF) {
                p.held = button::left;
                timer_ = 0xC;
                g.scheduler().create<ShrinkingPicture>(g, 4, 0x2DB, -8, 4);
                ++phase_;
                g.scheduler().create<CameraBoundsXY>(g, c.min_y, c.max_y, s16((p.x >> 8) - 0x60),
                                                     c.max_x);
            }
            return;
        case 2:
            if (--timer_ != 0) {
                return;
            }
            p.state = 0x34;
            p.flags |= player_flag::no_movement;
            p.held = 0;
            timer_ = 0x3C;
            break;
        case 3:
            if (--timer_ != 0) {
                return;
            }
            timer_ = u16(start_results(g, s.rings, u32(s.clock)));
            break;
        case 4:
            p.idle_timer = 0x3C;
            if (--timer_ != 0) {
                return;
            }
            p.state = 0x32;
            p.flags |= player_flag::no_movement;
            p.y -= 0x1000;
            wave_ = 0;
            speed_ = 0;
            timer_ = 0;
            break;
        case 5:
            if (++timer_ == 0x22) {
                g.scheduler().create<PlayerSparkle>(g);
            }
            if (timer_ < 0x9A) {
                if (timer_ < 0x22) {
                    if (timer_ < 0x20) {
                        if (timer_ > 0x19) {
                            speed_ = s16(speed_ - 0x180);
                            p.y += speed_;
                        }
                    } else {
                        speed_ = s16(speed_ + 0x400);
                        p.y += speed_;
                        float_y_ = s16(u32(p.y) >> 8);
                    }
                } else {
                    wave_ = u16((wave_ + 0x10) & 0x3FF);
                    p.y = (float_y_ + (sine(g.assets(), wave_) >> 11)) * 0x100;
                }
                return;
            }
            p.state = 0x33;
            speed_ = 0x100;
            timer_ = 0x78;
            break;
        case 6:
            if (p.x < 0xF2001) {
                speed_ = s16(speed_ + 0x80);
                p.x += speed_;
            } else {
                p.flags |= player_flag::hidden;
            }
            if (--timer_ != 0) {
                return;
            }
            g.scheduler().create<ShrinkingPicture>(g, 0x10, 0x2DC, -0xC, 6);
            timer_ = 0xB4;
            break;
        case 7:
            if (--timer_ != 0) {
                return;
            }
            v.inside[0] = v.inside[1] = 0x3F;
            v.windows[0] = {0, 0xF0, 0, 0xA0};
            v.blend.set_control(0xBF);
            v.window1 = false;
            v.window0 = true;
            v.blend.evy = 0;
            timer_ = 0;
            break;
        case 8:
            if (++timer_ < 0x81) {
                v.blend.evy = u8(timer_ >> 3);
            } else {
                g.scheduler().destroy_range(0x2000, 0x2FFF);
                g.scheduler().destroy(*this);
                next_act(g);
            }
            return;
        default:
            return;
        }
        ++phase_;
    }

private:
    u16 timer_ = 0x1A4; // +06
    u8 phase_{};        // +08
    s16 float_y_{};     // +0A
    s16 speed_{};       // +0C
    u16 wave_{};        // +0E
};

void ArmBoss::fall(Game& g) {
    StageState& s = g.stage();
    const bool true_way = true_ending(g);
    if (phase_ < 2) {
        if (phase_ == 0) {
            speed_y_ += 0x10;
        }
        x_ += speed_x_;
        y_ += speed_y_;
        act_x_ = s16((x_ >> 8) + base_x_);
        act_y_ = s16((y_ >> 8) + base_y_);
        if ((timer_ & 7) == 0) {
            x_zone_explosion(g, act_x_, act_y_);
        }
        animate(g, vehicle_);
        animate(g, eggman_);
        vehicle_.x = s16(act_x_ - s.camera.x);
        vehicle_.y = s16(act_y_ - s.camera.y);
        eggman_.x = vehicle_.x;
        eggman_.y = vehicle_.y;
        draw(g, vehicle_);
        draw(g, eggman_);
    }
    switch (phase_) {
    case 0: {
        --timer_;
        const s32 d = floor_distance(g, act_x_, s16(act_y_ + 0x14));
        if (d < 0) {
            y_ += d * 0x100;
            speed_y_ = (speed_y_ >> 2) - speed_y_;
            if (speed_y_ > -0x100) {
                speed_y_ = 0;
                timer_ = 0xF0;
                ++phase_;
                if (true_way) {
                    // 0803A51C: act 13 is reached.
                    SaveData& save = g.save();
                    if (save.zones_reached[0] < 0xE) {
                        save.zones_reached[0] = 0xE;
                    }
                    g.scheduler().create<TrueEndingStart>();
                } else {
                    g.scheduler().create<ExplodingEnd>(); // 0803A564
                }
            }
        }
        break;
    }
    case 1:
        if (--timer_ == 0) {
            const s32 x = act_x_, y = act_y_;
            if (true_way) {
                g.scheduler().create<EggmanEscape>(act_x_, s16(act_y_ - 8));
            }
            s.music_queued = 0x36;
            create_smoke(g, x << 8, (y - 0x18) * 0x100);
            create_smoke(g, (x - 0x10) * 0x100, y * 0x100);
            create_smoke(g, (x + 0x10) * 0x100, y * 0x100);
            create_smoke(g, x << 8, (y + 0x18) * 0x100);
            ++phase_;
        }
        break;
    case 2:
        g.scheduler().destroy(*this);
        break;
    default:
        break;
    }
}

} // namespace

bool create_x_zone_object(Game& g, u32 creator, const ObjectSpawn& o) {
    switch (creator) {
    case 0x080935B0:
        g.scheduler().create<FallingPlatform>(g, o);
        return true;
    default:
        return false;
    }
}

bool create_x_zone_enemy(Game& g, u32 creator, const ObjectSpawn& o) {
    switch (creator) {
    case 0x08034448:
        if (g.game_mode >= 2) {
            o.record[0] = 0xFE;
        } else {
            g.scheduler().create<WreckingBallBoss>(g, o);
        }
        return true;
    case 0x08037198:
        if (g.game_mode >= 2) {
            o.record[0] = 0xFE;
        } else {
            g.scheduler().create<ArmBoss>(g, o);
        }
        return true;
    case 0x08035B14:
        // Not met again once a checkpoint was passed.
        if (g.stage().checkpoint == 0) {
            if (g.game_mode >= 2) {
                o.record[0] = 0xFE;
            } else {
                g.scheduler().create<FistBoss>(g, o);
            }
        }
        return true;
    default:
        return false;
    }
}

} // namespace sonic_advance_gba
