// The bosses and the end of a zone's second act: Eggman flies in, the boss
// fight, his escape, the capsule that frees the animals and the walk off
// to the results.
//
// Neo Green Hill (enemy kinds 21 and 22): Eggman passes by in his ship
// (080259C0), later his machine waits at the arena's right end and swings a
// hammer on a chain; it takes eight hits (six with the save's byte +18 set):
// after the seventh it spins the hammer round, the eighth knocks it apart.

#include "sonic_advance_gba/game/boss.hpp"
#include "sonic_advance_gba/game/boss_parts.hpp"
#include "sonic_advance_gba/game/collision.hpp"
#include "sonic_advance_gba/game/game.hpp"
#include "sonic_advance_gba/game/math.hpp"
#include "sonic_advance_gba/game/object_util.hpp"
#include "sonic_advance_gba/game/player_effects.hpp"
#include "sonic_advance_gba/game/results.hpp"
#include "sonic_advance_gba/game/solid.hpp"
#include "sonic_advance_gba/game/stage.hpp"
#include "sonic_advance_gba/game/water.hpp"
#include <array>
#include <memory>

namespace sonic_advance_gba {

namespace {

using namespace boss_parts;

// 0804CBE0, 0804CAEC: the view shakes.
class Shake final : public Process {
public:
    Shake(Game& g, s32 amplitude, s32 decay, s32 step, s32 frames, u32 mode)
        : Process(0xEFF), game_(g), amplitude_(amplitude), decay_(decay), step_(step),
          frames_(frames), mode_(mode) {}

    // 0804CC34
    void on_destroy() override {
        auto& c = game_.stage().camera;
        if (mode_ & 0x40) {
            c.extra_x = 0;
        }
        if (mode_ & 0x80) {
            c.extra_y = 0;
        }
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        if (frames_ == 0 || amplitude_ < 1) {
            g.scheduler().destroy(*this);
            return;
        }
        s32 v;
        if (mode_ & 0x10) {
            v = s32((s.next_random() & 0x1FFFF) >> 8) - 0xFF;
        } else {
            v = sine(g.assets(), phase_) >> 6;
        }
        v = (v * amplitude_) >> 16;
        if ((mode_ & 3) == 1) {
            v = v < 0 ? -v : v;
        } else if ((mode_ & 3) == 2) {
            v = v < 0 ? v : -v;
        }
        if (!(mode_ & 0x20) || (s.ticks & 1) == 0) {
            if (mode_ & 0x40) {
                s.camera.extra_x = s16(v);
            }
            if (mode_ & 0x80) {
                s.camera.extra_y = s16(v);
            }
        }
        if (frames_ > 0) {
            --frames_;
        }
        if (amplitude_ > 0) {
            amplitude_ -= decay_;
        }
        phase_ = (phase_ + step_) & 0x3FF;
    }

private:
    Game& game_;
    s32 amplitude_; // +00 16.16
    s32 decay_;     // +04
    s32 step_;      // +08
    s32 frames_;    // +0C
    u32 mode_;      // +10
    s32 phase_{};   // +14
};

// 08016D34, 08016E08: an animal from the capsule hopping to and fro
// (within 20h pixels of where it came out).
class HoppingAnimal final : public Process {
public:
    HoppingAnimal(Game& g, s16 x, s16 y, u16 animation) : Process(0x2000), game_(g), x_(x), y_(y) {
        sprite_.start(g.sprite_tiles.allocate(4), animation, 0, 0, 0, 0x3C0, 0x2000);
    }

    // 0801760C
    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); }

    void update(Game& g) override {
        const auto& c = g.stage().camera;
        speed_y = s16(speed_y + 0x28);
        offset_y_ += speed_y;
        offset_x_ += speed_x;
        if (speed_y > 0 && floor_distance(g, x_ + (offset_x_ >> 8), y_ + (offset_y_ >> 8)) < 0) {
            speed_y = bounce;
            if ((offset_x_ >> 8) < -0x20 && speed_x < 0) {
                speed_x = s16(-speed_x);
            } else if ((offset_x_ >> 8) > 0x20 && speed_x > 0) {
                speed_x = s16(-speed_x);
            }
        }
        if (speed_x < 0) {
            sprite_.flags &= ~sprite_flag::flip_x;
        } else {
            sprite_.flags |= sprite_flag::flip_x;
        }
        sprite_.x = s16((offset_x_ >> 8) + x_ - c.x);
        sprite_.y = s16((offset_y_ >> 8) + y_ - c.y);
        animate(g, sprite_);
        draw(g, sprite_);
    }

    s16 speed_y = -0x400; // +3C
    s16 speed_x = 0x100;  // +3E
    s16 bounce = -0x400;  // +40 the speed of each hop

private:
    Game& game_;
    Sprite sprite_;               // +00
    s32 offset_x_{}, offset_y_{}; // +30, +34
    s16 x_, y_;                   // +38, +3A
};

// 08016EF8, 08017008: a bird from the capsule circling (drawn scaled to
// look round in depth) while it drifts to its height.
class CirclingAnimal final : public Process {
public:
    CirclingAnimal(Game& g, s16 x, s16 y, u16 animation) : Process(0x2000), game_(g), x_(x), y_(y) {
        t_.sprite.start(g.sprite_tiles.allocate(4), animation, 0, 0, 0, 0x500, 0x2070);
    }

    // 0801760C
    void on_destroy() override { game_.sprite_tiles.free(t_.sprite.tiles.to); }

    void update(Game& g) override {
        StageState& s = g.stage();
        const Assets& a = g.assets();
        bob_ = (bob_ + 0x10) & 0x3FF;
        phase = u16((phase + speed) & 0x3FF);
        offset_y_ = sine(a, bob_) >> 2;
        offset_x_ = sine(a, phase);
        const s16 scale = s16(s8(u16(sine(a, phase + 0x100u)) >> 8) + 0x100);
        t_.scale_y = scale;
        t_.sprite.layer = scale < 0x101 ? 0x440 : 0x3C0;
        const bool far_side = (u32(s16(phase)) - 0x100u) & 0x200;
        t_.scale_x = (speed < 1) == far_side ? scale : s16(-scale);
        if (s16(y_ - target_y) >= 1) {
            --y_;
        } else if (s16(y_ - target_y) < 0) {
            ++y_;
        }
        t_.x = s16((offset_x_ >> 8) + x_ - s.camera.x);
        t_.y = s16((offset_y_ >> 8) + y_ - s.camera.y);
        Sprite& sp = t_.sprite;
        sp.flags = (sp.flags & ~sprite_flag::rotation_set) | s.next_rotation_set++;
        animate(g, sp);
        transform(g, t_);
        draw(g, sp);
    }

    s16 target_y{}; // +4E
    u16 phase{};    // +50 around the circle
    s16 speed = 1;  // +52

private:
    Game& game_;
    TransformedSprite t_;         // +00 sprite, +30 transform
    s32 offset_x_{}, offset_y_{}; // +3C, +40
    s16 x_, y_;                   // +44, +46
    u16 bob_{};                   // +4C
};

// 08017450, 08016B20: the animals come out of the open capsule at (x, y),
// a few every fourth frame, alternately to the left and right.
class AnimalBurst final : public Process {
public:
    AnimalBurst(s16 x, s16 y) : Process(0x2000), x_(x), y_(y) {}

    void update(Game& g) override {
        StageState& s = g.stage();
        const Assets& a = g.assets();
        const u16 frame = count_++;
        if ((frame & 3) == 0) {
            const s16 y = s16(y_ - 0x20);
            const u32 animals = capsule_animals + u32(s.zone >> 1) * 6;
            const bool right = round_ & 1;
            u32 r = 0;
            const auto hop = [&](u16 animation, bool leftward) {
                HoppingAnimal& h = g.scheduler().create<HoppingAnimal>(g, x_, y, animation);
                r = s.next_random();
                h.speed_x = s16(h.speed_x + (r & 0xFF));
                if (leftward) {
                    h.speed_x = s16(-h.speed_x);
                }
                h.speed_y = s16(h.speed_y + ((r & 0x1F00) >> 4));
                h.bounce = h.speed_y;
            };
            const auto bird = [&](u16 animation, u32 random) {
                CirclingAnimal& c = g.scheduler().create<CirclingAnimal>(g, x_, y, animation);
                c.speed = s16(c.speed + (random & 7));
                if (right) {
                    c.speed = s16(-c.speed);
                    c.phase = 0x200;
                }
                c.target_y = s16((s.player[0].y >> 8) - 0x20);
            };
            if (count_ != 5) {
                hop(a.u16(animals), right);
            }
            hop(a.u16(animals + 2), !right);
            if (count_ != 5) {
                r = s.next_random();
                bird(0x1C0, r);
            }
            bird(0x1BA, r >> 8);
            ++round_;
        }
        if (s16(count_) > 0xC) {
            g.scheduler().destroy(*this);
        }
    }

private:
    s16 x_, y_;   // +00, +02
    u16 count_{}; // +04
    u16 round_{}; // +08
};

// 08017620, 080172E8, 080173B4: after the capsule opened, player `p` walks
// to the side of the view it is on, stops facing left and celebrates; then
// (player 1 only) the results come, 60 frames after the celebration's
// animation ended (or after 240 frames).
class WalkOff final : public Process {
public:
    explicit WalkOff(Player& p) : Process(0x2000), p_(p) {}

    void update(Game& g) override {
        StageState& s = g.stage();
        if (walking_) {
            const s16 sx = s16((p_.x >> 8) - s.camera.x);
            const u32 f = p_.flags;
            p_.flags = f | player_flag::frozen;
            if (sx <= 0x78) {
                p_.held = 0x20; // left
                if (sx >= p_.index * 8 + 0x28) {
                    return;
                }
            } else {
                p_.held = 0x10; // right
                if (sx <= 200 - p_.index * 8) {
                    return;
                }
            }
            p_.ground_speed = 0;
            p_.speed_x = 0;
            p_.held = 0;
            p_.flags = f | player_flag::frozen | player_flag::facing_left;
            p_.state = 0x1D;
            p_.flags |= player_flag::keep_state;
            after_end_ = 0;
            waited_ = 0;
            walking_ = false;
            return;
        }
        if (!(p_.flags & player_flag::in_air)) {
            p_.state = 0x1D;
        }
        if (p_.index != 0) {
            return;
        }
        if ((p_.body->sprite.flags & sprite_flag::ended) && after_end_++ > 0x3C) {
            start_results(g, s.rings, u32(s.clock));
            g.scheduler().destroy(*this);
            return;
        }
        if (after_end_ == 0 && waited_++ > 0xF0) {
            start_results(g, s.rings, u32(s.clock));
            g.scheduler().destroy(*this);
        }
    }

private:
    Player& p_;       // +04
    s16 after_end_{}; // +00
    s16 waited_{};    // +02
    bool walking_ = true;
};

// 08015FB0: the capsule flies up from where the boss broke, falls back
// into the middle of the view, and opens when a player stands on its
// button.
class Capsule final : public Process {
public:
    Capsule(Game& g, s16 x, s16 y) : Process(0x2000), game_(g), x_(x), y_(y) {
        body_.sprite.start(g.sprite_tiles.allocate(0x51), 0x242, 0, x, y, 0x4C0, 0x2030);
        body_.scale_x = body_.scale_y = 0x40;
        button_.start(g.sprite_tiles.allocate(6), 0x240, 0, x, y, 0x500, 0x2000);
    }

    // 0801755C
    void on_destroy() override {
        game_.sprite_tiles.free(body_.sprite.tiles.to);
        game_.sprite_tiles.free(button_.tiles.to);
    }

    void update(Game& g) override {
        switch (state_) {
        case State::rising:
            rise(g);
            break;
        case State::falling:
            fall(g);
            break;
        case State::closed:
            wait_for_button(g);
            break;
        case State::opening:
            open(g);
            break;
        case State::open:
            stay_open(g);
            break;
        }
    }

private:
    enum class State : u8 { rising, falling, closed, opening, open };

    // 08016110: up out of the view, turning and growing.
    void rise(Game& g) {
        const auto& c = g.stage().camera;
        body_.x = s16(x_ - c.x);
        body_.y = s16((y_ - c.y) - lift_);
        if (body_.scale_x < 0x100) {
            body_.scale_x = s16(body_.scale_x + 8);
            body_.scale_y = body_.scale_x;
        }
        body_.angle = u16(body_.angle + 0x10);
        animate(g, body_.sprite);
        transform(g, body_);
        draw(g, body_.sprite);
        speed_ = s16(speed_ - 0x10);
        lift_ = s16(lift_ + (speed_ >> 8));
        if (lift_ > 0xE0) {
            state_ = State::falling;
            body_.sprite.animation = 0x23F;
            body_.sprite.variant = 0;
            body_.sprite.flags &= ~sprite_flag::rotated;
        }
    }

    // 080161F0: down in the middle of the view; it lands with a shake,
    // throwing aside a player below.
    void fall(Game& g) {
        StageState& s = g.stage();
        Sprite& b = body_.sprite;
        b.x = 0x78;
        b.y = s16((y_ - s.camera.y) - lift_);
        button_.x = 0x78;
        button_.y = b.y;
        if (b.y >= 0) {
            animate(g, b);
            animate(g, button_);
            draw(g, b);
            draw(g, button_);
        }
        speed_ = s16(speed_ - 0x10);
        lift_ = s16(lift_ + (speed_ >> 8));
        if (b.y <= 0x5F) {
            return;
        }
        const s32 d = floor_distance(g, s.camera.x + b.x, s.camera.y + b.y + 8);
        if (d >= 1) {
            return;
        }
        y_ = s16(y_ - lift_ + d);
        state_ = State::closed;
        x_ = s16(s.camera.x + 0x78);
        shake_view(g, 0x800, 0x40, 0x100, -1, 0x80);
        g.sound().start(sound_capsule_lands);
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if (!touches_player(b, x_, y_, p) && !touches_player(button_, x_, y_, p)) {
                continue;
            }
            p.hurt_timer = 0x78;
            const s32 px = p.x >> 8;
            if (px < s.camera.x + 0x78) {
                p.speed_x = -0x200;
                const s32 to = x_ + b.hitboxes[0].left - p.radius_x;
                if (to < px) {
                    p.x = to << 8;
                }
            } else {
                p.speed_x = 0x200;
                const s32 to = x_ + b.hitboxes[0].right + p.radius_x;
                if (px < to) {
                    p.x = to << 8;
                }
            }
            p.speed_y = (p.flags & player_flag::underwater) ? -0x2A0 : -0x4E0;
            p.flags = (p.flags & 0xFFFFFAC3) | player_flag::in_air;
            p.state = 0x0F;
            set_player_radius(g, p, 6, 0xE);
            p.radius_x = 6;
            p.radius_y = 0xE;
            p.special = 0;
            p.move = 0;
        }
    }

    // 08016444: solid; standing on the button opens it.
    void wait_for_button(Game& g) {
        StageState& s = g.stage();
        Sprite& b = body_.sprite;
        place(s, 0);
        animate(g, b);
        animate(g, button_);
        draw(g, b);
        draw(g, button_);
        bool pressed = false;
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if (p.state == 0x0F) {
                continue;
            }
            // (The original compares the first player's height before and
            // after, whichever player this is.)
            Player& first = s.player[0];
            const s16 before = s16(u32(first.y) >> 8);
            collide_solid(g, b, x_, y_, p);
            if (!(first.flags & player_flag::dead) &&
                (before < (first.y >> 8) || y_ < (first.y >> 8))) {
                p.x = ((x_ + b.hitboxes[0].left) - p.radius_x) * 0x100;
                p.y = (y_ - p.radius_y) * 0x100;
            }
            if ((collide_solid(g, button_, x_, y_, p) & solid::on_top) ||
                hammer_hits(button_, x_, y_, p)) {
                pressed = true;
            }
        }
        if (!pressed) {
            return;
        }
        state_ = State::opening;
        s.flags |= stage_flag::paused | stage_flag::frozen;
        b.animation = 0x23F;
        b.variant = 1;
        g.sound().start(sound_capsule_opens);
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            p.ground_speed = 0;
            p.flags |= player_flag::frozen;
            p.held = 0;
            g.scheduler().create<WalkOff>(p);
        }
    }

    // 08016604: the button goes down and the capsule opens; then its top
    // flies off and the animals come out.
    void open(Game& g) {
        StageState& s = g.stage();
        Sprite& b = body_.sprite;
        place(s, 4);
        const u32 running = animate(g, b);
        animate(g, button_);
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if (p.state != 0x0F) {
                collide_solid(g, b, x_, y_, p);
                collide_solid(g, button_, x_, s16(y_ + 4), p);
            }
        }
        draw(g, b);
        draw(g, button_);
        if (running != 0) {
            return;
        }
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if ((p.flags & player_flag::on_object) && p.standing_on == &button_) {
                p.flags = (p.flags & ~player_flag::on_object) | player_flag::in_air;
            }
        }
        state_ = State::open;
        b.animation = 0x23F;
        b.variant = 2;
        for (const bool right : {false, true}) {
            Debris& d = g.scheduler().create<Debris>(g, x_, y_, 0x40, 0x241, 0);
            d.speed_x = right ? 0x100 : -0x100;
            d.speed_y = -0x200;
            d.growth = 0;
            d.spin = 4;
            if (right) {
                d.t.scale_x = -0x100;
            }
            d.timer = 0x3C;
        }
        g.scheduler().create<AnimalBurst>(x_, y_);
    }

    // 08016810
    void stay_open(Game& g) {
        StageState& s = g.stage();
        Sprite& b = body_.sprite;
        b.x = s16(x_ - s.camera.x);
        b.y = s16(y_ - s.camera.y);
        animate(g, b);
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if (p.state != 0x0F) {
                collide_solid(g, b, x_, y_, p);
            }
        }
        draw(g, b);
    }

    void place(const StageState& s, s16 button_drop) {
        Sprite& b = body_.sprite;
        b.x = s16(x_ - s.camera.x);
        b.y = s16(y_ - s.camera.y);
        button_.x = b.x;
        button_.y = s16(b.y + button_drop);
    }

    Game& game_;
    TransformedSprite body_; // +00 sprite, +60 transform
    Sprite button_;          // +30
    s16 x_, y_;              // +6C, +6E
    s16 lift_{};             // +72
    s16 speed_ = 0x800;      // +74
    State state_ = State::rising;
};

// What the two Eggman processes share (080259C0 and 08025F0C, 08027898):
// the layout record, the vehicle's sprite (+0C) and Eggman's (+44) with
// their own tiles.
class EggmanBase : public Process {
public:
    EggmanBase(Game& g, const ObjectSpawn& o, u32 vehicle_tiles, u32 eggman_tiles)
        : Process(0x2000), game_(g), record_(o.record) {
        base_x_ = s16(o.record[0] * 8 + o.cell_x * 0x100);
        base_y_ = s16(o.record[1] * 8 + o.cell_y * 0x100);
        o.record[0] = 0xFE;
        vehicle_tiles_ = g.sprite_tiles.allocate(vehicle_tiles);
        eggman_tiles_ = g.sprite_tiles.allocate(eggman_tiles);
    }

    // 08027898
    void on_destroy() override {
        flash_off(game_);
        game_.sprite_tiles.free(vehicle_tiles_);
        game_.sprite_tiles.free(eggman_tiles_);
    }

protected:
    void place(const StageState& s, s16 x, s16 y) {
        vehicle_.x = s16(x - s.camera.x);
        vehicle_.y = s16(y - s.camera.y);
        eggman_.x = vehicle_.x;
        eggman_.y = vehicle_.y;
    }

    Game& game_;
    u8* record_;
    s16 base_x_, base_y_;
    u32 vehicle_tiles_, eggman_tiles_; // +10, +48
    Sprite vehicle_;                   // +0C
    Sprite eggman_;                    // +44
};

// 080259C0: Eggman in his ship waits off the right of the view; when the
// player comes near he stops the player, laughs, turns and flies off to
// the right. The view is held around him meanwhile.
class EggmanArrival final : public EggmanBase {
public:
    EggmanArrival(Game& g, const ObjectSpawn& o) : EggmanBase(g, o, 0x1E, 0xC) {
        vehicle_.start(vehicle_tiles_, 0x2B5, 0, base_x_, 0, 0x4C0, 0x2000);
        eggman_.start(eggman_tiles_, 0x2B6, 0, base_x_, base_y_, 0x480, 0x2000);
        auto& c = g.stage().camera;
        c.min_x = s16(base_x_ - 0xF0);
        if (c.min_x < 0) {
            c.min_x = 0;
        }
        c.max_x = s16(base_x_ + 0x40);
        const s16 max_y = s16(base_y_ + 0x70);
        g.scheduler().create<CameraBounds>(g, s16(base_y_ - 0x30), max_y);
        if (c.y + 0xA0 < max_y) {
            c.max_y = max_y;
        }
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        switch (state_) {
        case State::waiting: { // 08025BB0
            show(g);
            auto& c = s.camera;
            c.min_x = c.max_x - 0xF0 <= c.x ? s16(c.max_x - 0xF0) : c.x;
            if (base_x_ - c.x <= 0xB0) {
                Player& p = s.player[0];
                p.ground_speed = 0;
                p.flags |= player_flag::frozen;
                p.held = 0;
                eggman_.animation = 0x2B6;
                eggman_.variant = 2;
                state_ = State::laughing;
                s.music_queued = 0x11;
            }
            break;
        }
        case State::laughing: { // 08025C94
            place(s, base_x_, base_y_);
            animate(g, vehicle_);
            const u32 running = animate(g, eggman_);
            draw(g, vehicle_);
            draw(g, eggman_);
            if (running == 0) {
                vehicle_.animation = 0x2B5;
                vehicle_.variant = 1;
                eggman_.animation = 0x2B6;
                eggman_.variant = 9;
                state_ = State::turning;
            }
            break;
        }
        case State::turning: { // 08025D50
            place(s, base_x_, base_y_);
            const u32 running = animate(g, vehicle_);
            animate(g, eggman_);
            draw(g, vehicle_);
            draw(g, eggman_);
            if (running == 0) {
                state_ = State::leaving;
                vehicle_.animation = 0x2B5;
                vehicle_.variant = 0;
                vehicle_.flags |= sprite_flag::flip_x;
                eggman_.animation = 0x2B6;
                eggman_.variant = 0;
                eggman_.flags |= sprite_flag::flip_x;
                g.scheduler().create<CameraBounds>(g, s16(base_y_ - 0x30), s16(base_y_ + 0x70));
            }
            break;
        }
        case State::leaving: // 08025E3C
            away_ += 0x200;
            place(s, s16(base_x_ + (away_ >> 8)), base_y_);
            if (vehicle_.x < 0x131) {
                animate(g, vehicle_);
                animate(g, eggman_);
                draw(g, vehicle_);
                draw(g, eggman_);
            } else {
                Player& p = s.player[0];
                p.flags &= ~player_flag::frozen;
                p.held = u16(p.held | s.jump_buttons | s.other_buttons);
                g.scheduler().destroy(*this);
                s.camera.max_x = s16(s.map.width);
            }
            break;
        }
    }

private:
    enum class State : u8 { waiting, laughing, turning, leaving };

    void show(Game& g) {
        place(g.stage(), base_x_, base_y_);
        animate(g, vehicle_);
        animate(g, eggman_);
        draw(g, vehicle_);
        draw(g, eggman_);
    }

    s32 away_{}; // +8C 8.8
    State state_ = State::waiting;
};

class NeoGreenHillBoss;

// 080270D0, 080278E8: a link of the hammer's chain, 10h pixels apart from
// the boss's turning point along the scene's turn; knocked off (080271B4,
// 080272A0) it bounces about for B5h frames, smoking.
class ChainLink final : public Process {
public:
    ChainLink(Game& g, u8 index) : Process(u16(0x2000 | index)), game_(g), index_(index) {
        sprite_.start(g.sprite_tiles.allocate(4), 0x260, 0, 0, 0, 0x440, 0x2030);
        animate(g, sprite_);
    }

    // 08027918
    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); }

    void knock_off() { state_ = State::knocked_off; }

    void update(Game& g) override {
        StageState& s = g.stage();
        if (state_ == State::on_chain) {
            transform_in_view(g, sprite_, 0, 0x100, 0x100, s16(index_ << 4), 0);
            draw(g, sprite_);
            return;
        }
        if (state_ == State::knocked_off) {
            const Assets& a = g.assets();
            const SceneTransform& v = g.scene;
            x_ = s16(((sine(a, v.angle + 0x100u) * 0x10 * index_) >> 14) + s.camera.x + v.focus_x);
            y_ = s16(((sine(a, v.angle) * 0x10 * index_) >> 14) + s.camera.y + v.focus_y);
            speed_x_ = (index_ & 1) ? 0x200 : -0x200;
            speed_y_ = -0x600;
            offset_x_ = offset_y_ = 0;
            timer_ = 0;
            sprite_.flags &= ~sprite_flag::rotated;
            state_ = State::flying;
        }
        // 080272A0
        if (timer_++ >= 0xB5) {
            g.scheduler().destroy(*this);
            return;
        }
        speed_y_ = s16(speed_y_ + 0x28);
        offset_x_ += speed_x_;
        offset_y_ += speed_y_;
        if (speed_y_ > 0) {
            const s32 d = floor_distance(g, x_ + (offset_x_ >> 8), y_ + (offset_y_ >> 8));
            if (d < 0) {
                offset_y_ += d * 0x100;
                speed_y_ = s16((speed_y_ >> 2) - speed_y_);
            }
        }
        const s32 x = x_ + (offset_x_ >> 8);
        if (s.camera.x + 0xF0 < x || x < s.camera.x) {
            speed_x_ = s16(-speed_x_);
        }
        if ((timer_ & 7) == 0) {
            create_smoke(g, x_ * 0x100 + offset_x_, y_ * 0x100 + offset_y_);
        }
        sprite_.x = s16((offset_x_ >> 8) + x_ - s.camera.x);
        sprite_.y = s16((offset_y_ >> 8) + y_ - s.camera.y);
        draw(g, sprite_);
    }

private:
    enum class State : u8 { on_chain, knocked_off, flying };

    Game& game_;
    u8 index_;                    // +50
    Sprite sprite_;               // +00
    s16 x_{}, y_{};               // +3C, +3E where it came off
    u16 timer_{};                 // +40
    s32 offset_x_{}, offset_y_{}; // +44, +48
    s16 speed_x_{}, speed_y_{};   // +4C, +4E
    State state_ = State::on_chain;
};

// 080273B4, 0802747C: the hammer at the chain's end (60h from the turning
// point), kept upright at the ends of the swing; it hurts a player it
// touches (Eggman laughs). Knocked off (080275D0, 080276E4) it flies,
// spinning.
class Hammer final : public Process {
public:
    Hammer(Game& g, NeoGreenHillBoss& boss) : Process(0x2005), game_(g), boss_(boss) {
        t_.sprite.start(g.sprite_tiles.allocate(0x40), 0x261, 0, 0, 0, 0x440, 0x2030);
        t_.x = 0x60;
        animate(g, t_.sprite);
    }

    // 0802792C
    void on_destroy() override { game_.sprite_tiles.free(t_.sprite.tiles.to); }

    void knock_off() { state_ = State::knocked_off; }

    void update(Game& g) override;

private:
    enum class State : u8 { on_chain, knocked_off, flying };

    // The hammer's own turn: none in the middle of the swing.
    u16 own_turn() const;
    void fly(Game& g);

    Game& game_;
    NeoGreenHillBoss& boss_;
    TransformedSprite t_;         // +00 sprite, +30 transform
    s16 x_{}, y_{};               // +3C, +3E where it came off
    u16 timer_{};                 // +40
    s16 spin_{};                  // +42
    s32 offset_x_{}, offset_y_{}; // +44, +48
    s16 speed_x_{}, speed_y_{};   // +4C, +4E
    State state_ = State::on_chain;
};

// 08025F0C: the Neo Green Hill boss. The view stops at its right end
// (40h left of where the boss is placed) and 360 pixels before.
class NeoGreenHillBoss final : public EggmanBase {
public:
    NeoGreenHillBoss(Game& g, const ObjectSpawn& o) : EggmanBase(g, o, 0x46, 8) {
        hits_ = g.save().unknown_5178 != 0 ? 2 : 0;
        vehicle_.start(vehicle_tiles_, 0x25F, 1, base_x_, base_y_, 0x4C0, 0x2000);
        eggman_.start(eggman_tiles_, 0x262, 0, base_x_, base_y_, 0x480, 0x2000);
        StageState& s = g.stage();
        s.camera.shift_y = -0x38;
        s.camera.max_x = s16(base_x_ - 0x40);
        // (The original computes this in floating point.)
        s.camera.min_x = s16(s.camera.max_x - 360);
        s.music_queued = 0x10;
    }

    void update(Game& g) override {
        switch (state_) {
        case State::waiting:
            wait(g);
            break;
        case State::swinging:
            swing(g);
            break;
        case State::spinning:
            spin(g);
            break;
        case State::falling:
            fall(g);
            break;
        case State::smoking:
            smoke(g);
            break;
        }
    }

    s16 angle() const { return angle_; }

    u8 blink() const { return blink_; }

    void laugh() { eggman_.variant = 1; }

private:
    enum class State : u8 { waiting, swinging, spinning, falling, smoking };

    // 08027838: two seconds, then the chain and the hammer.
    void wait(Game& g) {
        if (timer_++ <= 0x78) {
            return;
        }
        for (u8 n = 0; n < 5; ++n) {
            links_[n] = &g.scheduler().create<ChainLink>(g, n);
        }
        hammer_ = &g.scheduler().create<Hammer>(g, *this);
        state_ = State::swinging;
        swing(g);
    }

    // 080260EC: drives to and fro swinging the hammer over its head; the
    // hammer hits the ground at both ends. After the fifth hit it swings
    // faster.
    void swing(Game& g) {
        StageState& s = g.stage();
        angle_ = s16(angle_ + swing_);
        if (u16(angle_ + 0x237) > 0x26E) {
            g.sound().start(sound_swing_lands);
            swing_ = s16(-swing_);
            shake_view(g, 0x800, 0x40, 0x100, -1, 0x80);
        }
        drive();
        vehicle_.flags &= ~sprite_flag::mode;
        flash_off(g);
        const s16 x = s16(base_x_ + (offset_x_ >> 8));
        const s16 y = base_y_;
        if (blink_ == 0) {
            const BossContact c0 = boss_contact(g, vehicle_, x, y, s.player[0]);
            const BossContact c1 =
                s.players == 2 ? boss_contact(g, vehicle_, x, y, s.player[1]) : BossContact::none;
            if (c0 == BossContact::attacked || c1 == BossContact::attacked) {
                ++hits_;
                blink_ = 0x20;
                if (hits_ == 7) {
                    blink_ = 0x40;
                    state_ = State::spinning;
                } else if (hits_ > 4) {
                    swing_ = swing_ < 1 ? s16(~hits_) : s16(hits_ + 1);
                }
                eggman_.variant = 2;
                g.sound().start(sound_boss_hit);
            } else if (c0 == BossContact::hurt || c1 == BossContact::hurt) {
                laugh();
            }
        }
        place(s, x, y);
        turn_at_ends(s, x);
        show(g);
    }

    // 08026498: after the seventh hit it spins the hammer round and round,
    // hopping forward with each turn; the eighth hit breaks it.
    void spin(Game& g) {
        StageState& s = g.stage();
        const Assets& a = g.assets();
        const bool right = vehicle_.flags & sprite_flag::flip_x;
        swing_ = right ? s16(hits_ * 2 - 2) : s16((hits_ - 3) * -2 - 4);
        angle_ = s16(angle_ + swing_);
        if (angle_ < -0x300) {
            angle_ = s16(angle_ + 0x400);
            offset_x_ -= 0xB4C6;
        } else if (angle_ > 0x100) {
            angle_ = s16(angle_ - 0x400);
            offset_x_ += 0xB4C6;
        }
        // Past the ground at either end the hammer lifts the machine.
        s32 dx = 0, dy = 0;
        if (angle_ < -0x237 || angle_ > 0x37) {
            if (!landed_) {
                landed_ = true;
                g.sound().start(sound_swing_lands);
                shake_view(g, 0x800, 0x40, 0x100, -1, 0x80);
            }
            const bool left_end = angle_ < -0x237;
            const unsigned i = left_end ? (0x200u - u16(angle_)) & 0x3FF : u16(angle_) & 0x3FF;
            dx = ((0x3C42 - sine(a, i + 0x100)) * 3) >> 9;
            if (left_end) {
                dx = -dx;
            }
            dy = -(((sine(a, i) - 0x158F) * 3) >> 9);
        } else {
            landed_ = false;
            drive();
        }
        vehicle_.flags &= ~sprite_flag::mode;
        flash_off(g);
        const s16 x = s16(base_x_ + (offset_x_ >> 8) + dx);
        const s16 y = s16(base_y_ + dy);
        if (blink_ == 0) {
            const BossContact c0 = boss_contact(g, vehicle_, x, y, s.player[0]);
            const BossContact c1 =
                s.players == 2 ? boss_contact(g, vehicle_, x, y, s.player[1]) : BossContact::none;
            if (c0 == BossContact::attacked || c1 == BossContact::attacked) {
                broken(g, x, y, dx, dy);
                return;
            }
        }
        place(s, x, y);
        if (dy == 0) {
            turn_at_ends(s, x);
        }
        show(g);
    }

    // The eighth hit: points, the chain and hammer fly off, four pieces
    // of the machine too, and it drops.
    void broken(Game& g, s16 x, s16 y, s32 dx, s32 dy) {
        StageState& s = g.stage();
        ++hits_;
        blink_ = 0x20;
        eggman_.variant = 2;
        speed_y_ = -0x200;
        const u32 before = s.score;
        s.score += 1000;
        if (divide(s32(s.score), 50000) != divide(s32(before), 50000) && g.game_mode == 0) {
            ++s.lives;
        }
        g.sound().start(sound_boss_broken);
        for (ChainLink* link : links_) {
            link->knock_off();
        }
        hammer_->knock_off();
        offset_x_ += dx * 0x100;
        offset_y_ = dy << 8;
        timer_ = 0;
        vehicle_.animation = 0x264;
        vehicle_.variant = 0;
        const bool right = vehicle_.flags & sprite_flag::flip_x;
        for (u16 n = 0; n < 4; ++n) {
            Debris& d = g.scheduler().create<Debris>(g, x, y, 0x10, u16(0x265 + n), 0);
            d.speed_y = -0x200;
            d.growth = 0;
            d.spin = 0;
            const bool backward = n < 2;
            if (right) {
                d.t.scale_x = -0x100;
            }
            d.speed_x = (backward != right) ? -0x400 : 0x400;
            d.timer = 0x3C;
        }
        s.camera.min_x = s.camera.x;
        s.camera.max_x = s16(s.camera.x + 0xF0);
        state_ = State::falling;
        fall(g);
    }

    // 08026C14: falls and bounces to rest, exploding.
    void fall(Game& g) {
        StageState& s = g.stage();
        speed_y_ = s16(speed_y_ + 0x28);
        offset_y_ += speed_y_;
        const s16 x = s16(base_x_ + (offset_x_ >> 8));
        const s16 y = s16(base_y_ + (offset_y_ >> 8));
        const s32 d = floor_distance(g, x, s16(y - 8));
        if (d < 0) {
            offset_y_ += d * 0x100;
            speed_y_ = s16((speed_y_ >> 2) - speed_y_);
            if (speed_y_ > -0x100) {
                speed_y_ = 0;
                state_ = State::smoking;
            }
        }
        vehicle_.flags &= ~sprite_flag::mode;
        flash_off(g);
        if ((++timer_ & 7) == 0) {
            create_explosion_piece(g, x, y);
        }
        place(s, x, y);
        animate(g, vehicle_);
        animate(g, eggman_);
        if (vehicle_.variant == 2) {
            --eggman_.y;
        }
        draw(g, vehicle_);
        draw(g, eggman_);
    }

    // 08026EA0: explodes on the ground for F0h frames; then Eggman flies
    // off and the capsule comes.
    void smoke(Game& g) {
        StageState& s = g.stage();
        const s16 x = s16(base_x_ + (offset_x_ >> 8));
        const s16 y = s16(base_y_ + (offset_y_ >> 8));
        if (timer_++ > 0xF0) {
            g.scheduler().create<EggmanEscape>(x, s16(y - 0x20));
            g.scheduler().create<Capsule>(g, x, y);
            s.music_queued = 0x30;
            g.scheduler().destroy(*this);
            return;
        }
        if ((timer_ & 7) == 0) {
            create_explosion_piece(g, x, y);
        }
        place(s, x, y);
        animate(g, vehicle_);
        animate(g, eggman_);
        if (vehicle_.variant == 2) {
            --eggman_.y;
        }
        draw(g, vehicle_);
        draw(g, eggman_);
    }

    // A pixel a frame the way it faces.
    void drive() { offset_x_ += (vehicle_.flags & sprite_flag::flip_x) ? 0x100 : -0x100; }

    // Past the view's limits (with 40h to spare) it turns round.
    void turn_at_ends(const StageState& s, s16 x) {
        if (x < s.camera.min_x - 0x40) {
            vehicle_.flags |= sprite_flag::flip_x;
            eggman_.flags |= sprite_flag::flip_x;
        } else if (s.camera.max_x + 0x40 < x) {
            vehicle_.flags &= ~sprite_flag::flip_x;
            eggman_.flags &= ~sprite_flag::flip_x;
        }
    }

    // Drawn (blinking after a hit, flashing white at first); the chain
    // turns around a point 24h above the machine.
    void show(Game& g) {
        animate(g, vehicle_);
        animate(g, eggman_);
        if (vehicle_.variant == 2) {
            --eggman_.y;
        }
        bool shown = true;
        if (blink_ != 0) {
            const u8 b = --blink_;
            if (s8(b) < 0x11) {
                shown = (b & 2) != 0;
            } else if ((b & 2) == 0 && !(g.stage().player[0].flags & player_flag::dead)) {
                vehicle_.flags |= 0x100; // the sprite window
                flash_on(g);
            }
        }
        if (shown) {
            draw(g, vehicle_);
            draw(g, eggman_);
        }
        SceneTransform& v = g.scene;
        v.angle = u16(angle_) & 0x3FF;
        v.scale_y = 0x100;
        v.centre_x = v.centre_y = 0;
        v.scale_x = 0x100;
        v.focus_x = vehicle_.x;
        v.focus_y = s16(vehicle_.y - 0x24);
    }

    s32 offset_x_{};                    // +8C 8.8
    s32 offset_y_{};                    // +90 8.8
    s16 angle_{};                       // +94 the chain's angle (0 to the right, -200h to the left)
    s16 swing_ = -4;                    // +96
    u16 timer_{};                       // +98
    s8 hits_{};                         // +9A
    u8 blink_{};                        // +9B
    bool landed_{};                     // +9C
    s16 speed_y_{};                     // +9E
    std::array<ChainLink*, 5> links_{}; // +78
    Hammer* hammer_{};                  // +74
    State state_ = State::waiting;
};

// 0802D930: the Secret Base boss. The view closes in on a 300-pixel
// arena ending at its place. Eggman's machine drops in (0802DB1C,
// 0802DC0C), laughs (0802DD9C), then crouches and hops toward the player
// (0802DECC, 0802E100, 0802E260, 0802E3AC), faster with each hit; from the
// fourth hit every other hop is a leap off the screen that comes down on
// the player (0802E4D0, 0802E6E4) and hurts a player on the ground. Eight
// hits (six with the save's byte +18 set) break it (0802E838, 0802EA5C,
// 0802ECCC).
class SecretBaseBoss final : public EggmanBase {
public:
    SecretBaseBoss(Game& g, const ObjectSpawn& o) : EggmanBase(g, o, 0x36, 8) {
        hits_ = g.save().unknown_5178 != 0 ? 2 : 0;
        vehicle_.start(vehicle_tiles_, 0x26C, 1, base_x_, base_y_, 0x540, 0x2000);
        eggman_.start(eggman_tiles_, 0x26A, 0, base_x_, base_y_, 0x580, 0x2000);
        // (The original computes the left limit in floating point.)
        g.scheduler().create<CameraBoundsXY>(g, s16(base_y_ - 0x90), s16(base_y_ + 0x10),
                                             s16(base_x_ - 300), base_x_);
        g.stage().music_queued = 0x10;
    }

    void update(Game& g) override {
        switch (state_) {
        case State::waiting:
            wait(g);
            break;
        case State::dropping:
            drop(g);
            break;
        case State::laughing:
            laugh(g);
            break;
        case State::crouching:
            crouch(g);
            break;
        case State::hopping:
            hop(g);
            break;
        case State::landing:
            land(g);
            break;
        case State::resting:
            rest(g);
            break;
        case State::leaping:
            leap(g);
            break;
        case State::stomped:
            stomped(g);
            break;
        case State::falling:
            fall(g);
            break;
        case State::smoking:
            smoke(g);
            break;
        }
    }

private:
    enum class State : u8 {
        waiting,
        dropping,
        laughing,
        crouching,
        hopping,
        landing,
        resting,
        leaping,
        stomped,
        falling,
        smoking
    };

    s16 x() const { return s16(base_x_ + (offset_x_ >> 8)); }

    s16 y() const { return s16(base_y_ + (offset_y_ >> 8)); }

    void place(const StageState& s) {
        vehicle_.x = s16(x() - s.camera.x);
        vehicle_.y = s16(y() - s.camera.y);
    }

    // 0802D8D8: Eggman sits in the machine, his height following its
    // frame (crouching lowers him).
    void seat(Game& g) {
        eggman_.x = vehicle_.x;
        s16 dy = 0x2E;
        if (vehicle_.animation == 0x26C) {
            const s32 k = divide(g.assets().s8(vehicle_.frame + 1), 5);
            dy = s16(-g.assets().s16(seat_heights + u32(s8(k)) * 2));
        }
        eggman_.y = s16(vehicle_.y + dy);
    }

    // 0802D840: blinking white after a hit (from frame 10h of it).
    void show(Game& g) {
        if (blink_ != 0) {
            const u8 b = --blink_;
            if (s8(b) > 0x10 && (b & 2) == 0 && !(g.stage().player[0].flags & player_flag::dead)) {
                vehicle_.flags |= 0x100;
                flash_on(g);
            }
        }
        draw(g, vehicle_);
        draw(g, eggman_);
    }

    // 0802D718: hits and touches.
    void contact(Game& g) {
        StageState& s = g.stage();
        vehicle_.flags &= ~sprite_flag::mode;
        flash_off(g);
        if (blink_ != 0) {
            return;
        }
        const BossContact c0 = boss_contact_from_air(g, vehicle_, x(), y(), s.player[0]);
        const BossContact c1 = s.players == 2
                                   ? boss_contact_from_air(g, vehicle_, x(), y(), s.player[1])
                                   : BossContact::none;
        if (c0 == BossContact::attacked || c1 == BossContact::attacked) {
            ++hits_;
            blink_ = 0x20;
            if (hits_ == 4) {
                leaps_ = 1;
            }
            eggman_.variant = 2;
            eggman_.flags &= ~sprite_flag::ended;
            g.sound().start(sound_boss_hit);
        } else if (c0 == BossContact::hurt || c1 == BossContact::hurt) {
            eggman_.variant = 1;
            eggman_.flags &= ~sprite_flag::ended;
        }
    }

    // The common part of the fighting states: false when it broke instead.
    bool fight(Game& g, bool animated) {
        contact(g);
        if (hits_ >= 8) {
            broken(g);
            return false;
        }
        place(g.stage());
        if (eggman_.variant != 0 && (eggman_.flags & sprite_flag::ended)) {
            eggman_.variant = 0;
            eggman_.previous_variant = 0xFF;
        }
        if (animated) {
            animate(g, vehicle_);
            animate(g, eggman_);
        }
        seat(g);
        show(g);
        return true;
    }

    // 0802DB1C
    void wait(Game& g) {
        if (timer_++ <= 0x78) {
            return;
        }
        offset_x_ = -0x3C00;
        offset_y_ = -0x9000;
        speed_y_ = 0;
        place(g.stage());
        eggman_.x = vehicle_.x;
        eggman_.y = vehicle_.y;
        animate(g, vehicle_);
        animate(g, eggman_);
        draw(g, vehicle_);
        draw(g, eggman_);
        state_ = State::dropping;
        drop(g);
    }

    // 0802DC0C
    void drop(Game& g) {
        if (offset_y_ == 0) {
            animate(g, vehicle_);
            animate(g, eggman_);
            place(g.stage());
            seat(g);
            draw(g, vehicle_);
            draw(g, eggman_);
            if (vehicle_.flags & sprite_flag::ended) {
                state_ = State::laughing;
                vehicle_.animation = 0x26C;
                vehicle_.variant = 0;
                vehicle_.previous_variant = 0xFF;
                timer_ = 0x78;
            }
            return;
        }
        speed_y_ += 0x20;
        offset_y_ += speed_y_;
        if (offset_y_ >= 0) {
            offset_y_ = 0;
            vehicle_.animation = 0x26C;
            vehicle_.variant = 1;
            vehicle_.previous_variant = 0xFF;
            animate(g, vehicle_);
            animate(g, eggman_);
            g.sound().start(sound_swing_lands);
        }
        place(g.stage());
        seat(g);
        draw(g, vehicle_);
        draw(g, eggman_);
    }

    // 0802DD9C
    void laugh(Game& g) {
        if (eggman_.variant == 0) {
            if (--timer_ == 0x3C) {
                eggman_.variant = 1;
                eggman_.previous_variant = 0xFF;
            }
            if (timer_ == 0) {
                state_ = State::crouching;
                speed_y_ = 0;
                vehicle_.animation = 0x26C;
                vehicle_.variant = 1;
                vehicle_.previous_variant = 0xFF;
                eggman_.animation = 0x26A;
                eggman_.variant = 0;
                eggman_.previous_variant = 0xFF;
            }
        } else if (eggman_.flags & sprite_flag::ended) {
            eggman_.variant = 0;
            eggman_.previous_variant = 0xFF;
        }
        animate(g, vehicle_);
        animate(g, eggman_);
        place(g.stage());
        seat(g);
        draw(g, vehicle_);
        draw(g, eggman_);
    }

    // 0802DECC: crouched; when the crouch has played, it hops.
    void crouch(Game& g) {
        if (!fight(g, true) || !(vehicle_.flags & sprite_flag::ended)) {
            return;
        }
        const Assets& a = g.assets();
        if ((leaps_ & 3) == 1) {
            speed_x_ = 0;
            speed_y_ = -0xA00;
            state_ = State::leaping;
        } else {
            const s32 f = a.s16(hop_strengths + u32(hits_) * 2);
            speed_x_ = (g.stage().player[0].x - x() * 0x100) / 0x48;
            speed_y_ = -0x480;
            gravity_ = 0x20;
            speed_x_ = (f * speed_x_) / 10;
            speed_y_ = (f * speed_y_) / 10;
            gravity_ = (f * f * gravity_) / 100;
            if (speed_x_ < 0) {
                vehicle_.flags &= ~sprite_flag::flip_x;
                eggman_.flags &= ~sprite_flag::flip_x;
            } else {
                vehicle_.flags |= sprite_flag::flip_x;
                eggman_.flags |= sprite_flag::flip_x;
            }
            state_ = State::hopping;
        }
        if (leaps_ != 0) {
            ++leaps_;
        }
    }

    // 0802E100
    void hop(Game& g) {
        speed_y_ += gravity_;
        offset_x_ += speed_x_;
        offset_y_ += speed_y_;
        if (offset_y_ >= 0) {
            offset_y_ = 0;
            speed_x_ = 0;
            speed_y_ = 0;
            vehicle_.variant = 1;
            vehicle_.previous_variant = 0xFF;
            animate(g, vehicle_);
            animate(g, eggman_);
            state_ = State::landing;
            g.sound().start(sound_swing_lands);
        }
        fight(g, false);
    }

    // 0802E260
    void land(Game& g) {
        if (fight(g, true) && (vehicle_.flags & sprite_flag::ended)) {
            vehicle_.variant = 0;
            vehicle_.previous_variant = 0xFF;
            timer_ = u16(300 / g.assets().s16(hop_strengths + u32(hits_) * 2));
            state_ = State::resting;
        }
    }

    // 0802E3AC
    void rest(Game& g) {
        if (fight(g, true) && --timer_ == 0) {
            vehicle_.variant = 1;
            vehicle_.previous_variant = 0xFF;
            state_ = State::crouching;
        }
    }

    // 0802E4D0: up off the screen, over the player, and down.
    void leap(Game& g) {
        StageState& s = g.stage();
        speed_y_ += 0x20;
        offset_y_ += speed_y_;
        if (offset_y_ < -0xC800) {
            offset_x_ = s.player[0].x - (base_x_ & 0xFFFF) * 0x100;
        }
        if (offset_y_ >= 0) {
            offset_y_ = 0;
            speed_y_ = 0;
            timer_ = 0x3C;
            vehicle_.variant = 2;
            vehicle_.previous_variant = 0xFF;
            state_ = State::stomped;
            g.sound().start(sound_swing_lands);
            shake_view(g, 0x800, 0x40, 0x100, -1, 0x80);
            Player& p = s.player[0];
            const bool grounded = !(p.flags & player_flag::in_air);
            if (grounded || p.state == 0x41 ||
                ((p.flags & 0x4000000) && (p.animation != 0x176 || p.variant != 0))) {
                hurt_player(g, p);
                eggman_.variant = 1;
                eggman_.flags &= ~sprite_flag::ended;
            }
            if (s.players == 2 && !(s.player[1].flags & player_flag::in_air)) {
                hurt_player(g, p); // (the first player again, as in the original)
                eggman_.variant = 1;
                eggman_.flags &= ~sprite_flag::ended;
            }
        }
        fight(g, false);
    }

    // 0802E6E4
    void stomped(Game& g) {
        if (!fight(g, true)) {
            return;
        }
        if (vehicle_.variant == 2) {
            if (vehicle_.flags & sprite_flag::ended) {
                vehicle_.variant = 0;
                vehicle_.previous_variant = 0xFF;
            }
        } else if (--timer_ == 0) {
            vehicle_.animation = 0x26C;
            vehicle_.variant = 1;
            vehicle_.previous_variant = 0xFF;
            state_ = State::crouching;
        }
    }

    // 0802E838: the last hit: points, two pieces, and it drops.
    void broken(Game& g) {
        StageState& s = g.stage();
        offset_y_ -= 0x3000;
        const s16 bx = x(), by = y();
        eggman_.variant = 2;
        bounce_ = -0x200;
        g.sound().start(sound_boss_broken);
        timer_ = 0;
        vehicle_.animation = 0x26D;
        vehicle_.variant = 0;
        vehicle_.previous_variant = 0xFF;
        const bool right = vehicle_.flags & sprite_flag::flip_x;
        const std::array<std::pair<u16, s16>, 2> pieces{{{0x26B, 0}, {0x269, 0x20}}};
        for (const auto& [animation, dy] : pieces) {
            Debris& d = g.scheduler().create<Debris>(g, bx, s16(by + dy), 8, animation, 0);
            d.speed_y = -0x200;
            d.growth = 0;
            d.spin = animation == 0x26B ? 0x80 : 0x180;
            if (right) {
                d.t.scale_x = -0x100;
                d.speed_x = 0x400;
            } else {
                d.speed_x = -0x400;
            }
            d.timer = 0x3C;
        }
        s.camera.min_x = s.camera.x;
        s.camera.max_x = s16(s.camera.x + 0xF0);
        const u32 before = s.score;
        s.score += 1000;
        if (divide(s32(s.score), 50000) != divide(s32(before), 50000) && g.game_mode == 0) {
            ++s.lives;
        }
        state_ = State::falling;
        fall(g);
    }

    // 0802EA5C: falls and bounces to rest, exploding.
    void fall(Game& g) {
        StageState& s = g.stage();
        bounce_ = s16(bounce_ + 0x28);
        offset_y_ += bounce_;
        const s16 bx = x(), by = y();
        const s32 d = floor_distance(g, bx, s16(by + 0x10));
        if (d < 0) {
            offset_y_ += d * 0x100;
            bounce_ = s16((bounce_ >> 2) - bounce_);
            if (bounce_ > -0x100) {
                bounce_ = 0;
                state_ = State::smoking;
            }
        }
        vehicle_.flags &= ~sprite_flag::mode;
        flash_off(g);
        if ((++timer_ & 7) == 0) {
            create_explosion_piece(g, bx, by, 0);
        }
        vehicle_.x = s16(bx - s.camera.x);
        vehicle_.y = s16(by - s.camera.y);
        seat(g);
        animate(g, vehicle_);
        animate(g, eggman_);
        draw(g, vehicle_);
        draw(g, eggman_);
    }

    // 0802ECCC
    void smoke(Game& g) {
        StageState& s = g.stage();
        const s16 bx = x(), by = y();
        if (timer_++ > 0xF0) {
            g.scheduler().create<EggmanEscape>(bx, s16(by - 4));
            g.scheduler().create<Capsule>(g, bx, by);
            s.music_queued = 0x30;
            g.scheduler().destroy(*this);
            return;
        }
        if ((timer_ & 7) == 0) {
            create_explosion_piece(g, bx, by, 0);
        }
        vehicle_.x = s16(bx - s.camera.x);
        vehicle_.y = s16(by - s.camera.y);
        seat(g);
        animate(g, vehicle_);
        animate(g, eggman_);
        draw(g, vehicle_);
        draw(g, eggman_);
    }

    // 0802EED8 frees the tiles without the flash's reset.
    void on_destroy() override {
        game_.sprite_tiles.free(vehicle_tiles_);
        game_.sprite_tiles.free(eggman_tiles_);
    }

    static constexpr u32 hop_strengths = 0x084A5CAC; // by hits, tenths
    static constexpr u32 seat_heights = 0x084A5CA0;  // by the machine's frame

    s32 offset_x_{}, offset_y_{}; // +94, +98 8.8
    s32 speed_x_{}, speed_y_{};   // +9C, +A0
    s32 gravity_{};               // +A4
    u16 timer_{};                 // +AC
    s8 hits_{};                   // +AE
    u8 blink_{};                  // +AF
    u8 leaps_{};                  // +B0 from the fourth hit: odd counts leap
    s16 bounce_{};                // +B2
    State state_ = State::waiting;
};

u16 Hammer::own_turn() const {
    const s16 a = boss_.angle();
    if (a < 0) {
        const s16 v = s16(-0x200 - a);
        return v > 0 ? u16(v) : 0;
    }
    return a > 0 ? u16(-a) : 0;
}

// The Casino Paradise boss's flight paths (084A5D24: 16 bytes each, the
// start (8.8, from the boss's place) and the speed before the scaling by
// hits), its speed by hits (084A5CC0, in tenths) and the pairs of paths
// for the machine and its claw (084A5DA4).
constexpr u32 casino_paths = 0x084A5D24, casino_speeds = 0x084A5CC0, casino_path_pairs = 0x084A5DA4;
// The arena's pieces (084A5CD2: 8 bytes each: offset x, y, variant,
// flags) and how far they slide in (084A5D12: x, y bytes).
constexpr u32 casino_pieces = 0x084A5CD2, casino_piece_slides = 0x084A5D12;

struct FlightPath {
    s32 x, y, speed_x, speed_y;

    static FlightPath load(const Assets& a, unsigned index, s8 hits) {
        const u32 e = casino_paths + index * 0x10u;
        const s32 speed = a.s16(casino_speeds + u32(s32(hits)) * 2);
        return {s32(a.u32(e)), s32(a.u32(e + 4)), quotient(speed * s32(a.u32(e + 8)), 10),
                quotient(speed * s32(a.u32(e + 12)), 10)};
    }
};

// 0803050C, 08030570: a piece of the Casino Paradise boss's arena (index
// 0-7). After 90 frames it slides in from its offset; once the boss is
// broken it slides out again (in 16 steps) and is gone. The first two are
// solid. Pieces 0 and 2 own the tiles the others share (08030724).
class ArenaPiece final : public Process {
public:
    ArenaPiece(Game& g, s16 base_x, s16 base_y, u8 index, std::shared_ptr<const s8> hits, u32 tiles,
               bool owner)
        : Process(0x2200), game_(g), index_(index), hits_(std::move(hits)), owner_(owner) {
        const Assets& a = g.assets();
        const u32 e = casino_pieces + index * 8u;
        dx_ = a.s16(e);
        dy_ = a.s16(e + 2);
        base_x_ = base_x;
        base_y_ = base_y;
        sprite_.start(tiles, 0x251, a.u8(e + 4), s16(base_x + dx_), s16(base_y + dy_),
                      (index & 2) ? 0x480 : 0x380, u32(s32(s16(a.u16(e + 6) | 0x2000))));
    }

    void on_destroy() override {
        if (owner_) {
            game_.sprite_tiles.free(sprite_.tiles.to);
        }
    }

    u32 tiles() const { return sprite_.tiles.to; }

    void update(Game& g) override {
        const Assets& a = g.assets();
        const s8 tx = a.s8(casino_piece_slides + index_ * 2u),
                 ty = a.s8(casino_piece_slides + index_ * 2u + 1);
        if (waiting_ > 0) {
            if (--waiting_ == 0) {
                slide_x_ = tx;
                slide_y_ = ty;
            }
            return;
        }
        if (*hits_ < 8) {
            const auto settle = [](s16& v) {
                if (v < 0) {
                    ++v;
                }
                if (v > 0) {
                    --v;
                }
            };
            settle(slide_x_);
            settle(slide_y_);
        } else {
            slide_x_ = s16(slide_x_ + (tx >> 4));
            slide_y_ = s16(slide_y_ + (ty >> 4));
            if (slide_x_ == tx && slide_y_ == ty) {
                g.scheduler().destroy(*this);
                return;
            }
        }
        StageState& s = g.stage();
        const s16 x = s16(slide_x_ + base_x_ + dx_), y = s16(slide_y_ + base_y_ + dy_);
        sprite_.x = s16(x - s.camera.x);
        sprite_.y = s16(y - s.camera.y);
        if ((index_ & 6) == 0) {
            collide_solid(g, sprite_, x, y, s.player[0]);
            if (s.players == 2) {
                collide_solid(g, sprite_, x, y, s.player[1]);
            }
        }
        animate(g, sprite_);
        draw(g, sprite_);
    }

private:
    Game& game_;
    Sprite sprite_;                  // +0C
    s16 dx_, dy_;                    // +3C, +3E
    s16 slide_x_{}, slide_y_{};      // +40, +42
    u8 index_;                       // +44
    u8 waiting_ = 0x5A;              // +45
    std::shared_ptr<const s8> hits_; // the boss's hits (through +48)
    s16 base_x_, base_y_;
    bool owner_;
};

class CasinoParadiseBoss;

// 080300F0: the boss's claw: waits off screen until the machine sets off,
// then flies its own path (080301DC; touching it hurts, and Eggman laughs);
// once the boss is broken it drops, bouncing and smoking, for 181 frames
// (08030334, 080303E4).
class BossClaw final : public Process {
public:
    BossClaw(Game& g, CasinoParadiseBoss& boss, s16 base_x, s16 base_y, s32 x, s32 y);

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 08030710

    void set_off() { go_ = true; }

    void drop(Game& g);
    void update(Game& g) override;

private:
    s16 cx() const { return s16(base_x_ + (x_ >> 8)); }

    s16 cy() const { return s16(base_y_ + (y_ >> 8)); }

    void fall(Game& g);

    enum class State { waiting, flying, dropping, falling } state_ = State::waiting;
    Game& game_;
    CasinoParadiseBoss& boss_;
    Sprite sprite_;             // +00
    s32 x_, y_;                 // +30, +34 8.8 from the boss's place
    s32 speed_x_{}, speed_y_{}; // +38, +3C
    u16 timer_{};               // +40
    bool go_{};                 // +42
    s16 base_x_, base_y_;
};

// 0802F138: the Casino Paradise boss. The view closes in on its arena
// (whose pieces slide in, ArenaPiece); Eggman's machine waits two seconds,
// rises in from the left (0802F53C), laughs and leaves the top (0802F614).
// Then it keeps flying across the arena on random paths (0802F7D4,
// 0802F9C8), faster with each hit, its claw on another; it is hit from the
// front by the player's attack. Eight hits (six with the save's byte +18
// set) break it (0802FAFC): it falls, bounces and smokes (0802FBE4,
// 0802FE58), and Eggman escapes, leaving the capsule.
class CasinoParadiseBoss final : public Process {
public:
    CasinoParadiseBoss(Game& g, const ObjectSpawn& o) : Process(0x2100), game_(g) {
        StageState& s = g.stage();
        s.random = g.frames();
        base_x_ = s16(o.record[0] * 8 + o.cell_x * 0x100);
        base_y_ = s16(o.record[1] * 8 + o.cell_y * 0x100);
        *hits_ = g.save().unknown_5178 != 0 ? 2 : 0;
        vehicle_tiles_ = g.sprite_tiles.allocate(0x24);
        vehicle_.start(vehicle_tiles_, 0x270, 0, base_x_, base_y_, 0x540, 0x2000);
        o.record[0] = 0xFE;
        eggman_tiles_ = g.sprite_tiles.allocate(8);
        eggman_.start(eggman_tiles_, 0x26E, 0, base_x_, base_y_, 0x580, 0x2000);
        std::array<u32, 2> shared{};
        for (u8 i = 0; i < 8; ++i) {
            const bool owner = (i & 0xFD) == 0;
            const u32 tiles = owner ? g.sprite_tiles.allocate(0x1C) : shared[(i & 2) >> 1];
            ArenaPiece& piece =
                g.scheduler().create<ArenaPiece>(g, base_x_, base_y_, i, hits_, tiles, owner);
            if (owner) {
                shared[(i & 2) >> 1] = piece.tiles();
            }
        }
        g.scheduler().create<CameraBoundsXY>(g, s16(base_y_ - 0xB8), s16(base_y_ + 0x10),
                                             s16(base_x_ - 0x118), base_x_);
        s.music_queued = 0x10;
    }

    // 080306F4
    void on_destroy() override {
        game_.sprite_tiles.free(vehicle_tiles_);
        game_.sprite_tiles.free(eggman_tiles_);
    }

    s8 hits() const { return *hits_; }

    u8 pick() const { return pick_; }

    // Eggman laughs (the claw hit the player).
    void laugh() {
        eggman_.variant = 1;
        eggman_.flags &= ~sprite_flag::ended;
        eggman_.previous_variant = 0xFF;
    }

    void update(Game& g) override {
        switch (state_) {
        case State::waiting:
            wait(g);
            break;
        case State::rising:
            rise(g);
            break;
        case State::laughing:
            leave(g);
            break;
        case State::choosing:
            choose(g);
            break;
        case State::swooping:
            swoop(g);
            break;
        case State::falling:
            fall(g);
            break;
        case State::smoking:
            smoke(g);
            break;
        }
    }

private:
    enum class State : u8 { waiting, rising, laughing, choosing, swooping, falling, smoking };

    s16 bx() const { return s16(base_x_ + (x_ >> 8)); }

    s16 by() const { return s16(base_y_ + (y_ >> 8)); }

    void place(const StageState& s, s16 x, s16 y) {
        vehicle_.x = s16(x - s.camera.x);
        vehicle_.y = s16(y - s.camera.y);
        eggman_.x = vehicle_.x;
        eggman_.y = vehicle_.y;
    }

    void show_both(Game& g) {
        animate(g, vehicle_);
        animate(g, eggman_);
        draw(g, vehicle_);
        draw(g, eggman_);
    }

    void stop_laughing() {
        if (eggman_.variant != 0 && (eggman_.flags & sprite_flag::ended)) {
            eggman_.variant = 0;
            eggman_.previous_variant = 0xFF;
        }
    }

    // 0802F478: two seconds unseen, then in from the left.
    void wait(Game& g) {
        if (timer_++ <= 0x78) {
            return;
        }
        StageState& s = g.stage();
        x_ = -0x5500;
        y_ = 0;
        speed_y_ = -0x100;
        place(s, s16(base_x_ - 0x55), base_y_);
        show_both(g);
        timer_ = 0x3C;
        state_ = State::rising;
        rise(g);
    }

    // 0802F53C
    void rise(Game& g) {
        if (y_ < -0x4FFF) {
            if (--timer_ == 0) {
                eggman_.variant = 1;
                timer_ = 0x3C;
                state_ = State::laughing;
            }
        } else {
            y_ -= 0x100;
        }
        place(g.stage(), bx(), by());
        show_both(g);
    }

    // 0802F614: laughs, then up and away; the claw is made.
    void leave(Game& g) {
        if (eggman_.variant == 0) {
            if (timer_ == 0) {
                y_ -= 0x100;
                if (y_ < -0xCFFF) {
                    timer_ = 0x3C;
                    state_ = State::choosing;
                    claw_ = &g.scheduler().create<BossClaw>(g, *this, base_x_, base_y_, x_, y_);
                }
            } else {
                --timer_;
            }
        }
        if (eggman_.variant == 1 && (eggman_.flags & sprite_flag::ended)) {
            timer_ = 0x3C;
            eggman_.variant = 0;
        }
        place(g.stage(), bx(), by());
        show_both(g);
    }

    // 0802F7D4: a second off screen, then a random path.
    void choose(Game& g) {
        StageState& s = g.stage();
        if (--timer_ == 0) {
            pick_ = u8(s.next_random() & 7);
            const FlightPath f =
                FlightPath::load(g.assets(), g.assets().u8(casino_path_pairs + pick_ * 2u), *hits_);
            x_ = f.x;
            y_ = f.y;
            speed_x_ = f.speed_x;
            speed_y_ = f.speed_y;
            if (x_ < -0x8C00) {
                vehicle_.flags |= sprite_flag::flip_x;
                eggman_.flags |= sprite_flag::flip_x;
            } else {
                vehicle_.flags &= ~sprite_flag::flip_x;
                eggman_.flags &= ~sprite_flag::flip_x;
            }
            claw_->set_off();
            state_ = State::swooping;
        }
        flash_ = 0;
        animate(g, vehicle_);
        animate(g, eggman_);
        vehicle_.x = s16(bx() - s.camera.x);
        vehicle_.y = s16(by() - s.camera.y);
        stop_laughing();
        eggman_.x = vehicle_.x;
        eggman_.y = vehicle_.y;
    }

    // 0802F9C8
    void swoop(Game& g) {
        x_ += speed_x_;
        y_ += speed_y_;
        if (u32(x_ + 0x13800) > 0x15800 || y_ < -0xD000 || y_ > 0x2800) {
            timer_ = 0x3C;
            state_ = State::choosing;
        }
        const s16 x = bx(), y = by();
        animate(g, vehicle_);
        animate(g, eggman_);
        contact(g, x, y);
        if (*hits_ >= 8) {
            broken(g);
            return;
        }
        StageState& s = g.stage();
        vehicle_.x = s16(x - s.camera.x);
        vehicle_.y = s16(y - s.camera.y);
        stop_laughing();
        eggman_.x = vehicle_.x;
        eggman_.y = vehicle_.y;
        show(g);
    }

    // 0802EF64: hits (the attack box on its hitbox) and touches.
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
            if (++*hits_ == 4) {
                angry_ = true;
            }
            flash_ = 0x20;
            eggman_.variant = 2;
            eggman_.flags &= ~sprite_flag::ended;
            g.sound().start(sound_boss_hit);
        } else if (c0 == BossContact::hurt || c1 == BossContact::hurt) {
            eggman_.variant = 1;
            eggman_.flags &= ~sprite_flag::ended;
        }
    }

    // 0802F0A0: blinking white after a hit (from frame 10h of it).
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

    // 0802FAFC: the last hit: points, the claw drops, and it falls.
    void broken(Game& g) {
        StageState& s = g.stage();
        eggman_.variant = 2;
        bounce_ = -0x200;
        g.sound().start(sound_boss_broken);
        claw_->drop(g);
        g.scheduler().destroy_range(0x2010, 0x2011);
        s.player[0].flags &= ~player_flag::on_object;
        s.player[1].flags &= ~player_flag::on_object;
        auto& c = s.camera;
        c.min_x = c.x;
        c.max_x = s16(c.x + 0xF0);
        // The bounds only start moving later (the current ones are kept).
        const s16 min_y = c.min_y, max_y = c.max_y;
        g.scheduler().create<CameraBounds>(g, s16(min_y + 0x28), max_y);
        c.min_y = min_y;
        c.max_y = max_y;
        const u32 before = s.score;
        s.score += 1000;
        if (divide(s32(s.score), 50000) != divide(s32(before), 50000) && g.game_mode == 0) {
            ++s.lives;
        }
        state_ = State::falling;
        fall(g);
    }

    // 0802FBE4: falls and bounces to rest, exploding.
    void fall(Game& g) {
        StageState& s = g.stage();
        bounce_ = s16(bounce_ + 0x28);
        y_ += bounce_;
        const s16 x = bx(), y = by();
        const s32 d = probe_short(g.assets(), s.map, y + 0x18, x, 1, 8, nullptr, Sensor::floor);
        if (d < 0) {
            y_ += d * 0x100;
            bounce_ = s16((bounce_ >> 2) - bounce_);
            if (bounce_ > -0x100) {
                bounce_ = 0;
                state_ = State::smoking;
            }
        }
        vehicle_.flags &= ~sprite_flag::mode;
        flash_off(g);
        if ((++timer_ & 7) == 0) {
            create_explosion_piece(g, x, y, -0x20);
        }
        place(s, x, y);
        show_both(g);
    }

    // 0802FE58
    void smoke(Game& g) {
        StageState& s = g.stage();
        const s16 x = bx(), y = by();
        if (timer_++ > 0xF0) {
            g.scheduler().create<EggmanEscape>(x, s16(y - 8));
            g.scheduler().create<Capsule>(g, x, y);
            s.music_queued = 0x30;
            Debris& d = g.scheduler().create<Debris>(g, x, y, 0x10, 0x26F, 0);
            d.speed_y = -0x200;
            d.growth = 0;
            d.spin = 0x80;
            if (vehicle_.flags & sprite_flag::flip_x) {
                d.t.scale_x = -0x100;
                d.speed_x = 0x400;
            } else {
                d.speed_x = -0x400;
            }
            d.timer = 0x3C;
            g.scheduler().destroy(*this);
            return;
        }
        if ((timer_ & 7) == 0) {
            create_explosion_piece(g, x, y, -0x20);
        }
        place(s, x, y);
        show_both(g);
    }

    State state_ = State::waiting;
    Game& game_;
    s16 base_x_, base_y_;
    u32 vehicle_tiles_, eggman_tiles_;                   // +10, +40
    Sprite vehicle_;                                     // +0C
    Sprite eggman_;                                      // +3C
    BossClaw* claw_{};                                   // +6C
    s32 x_{}, y_{};                                      // +70, +74 8.8 from its place
    s32 speed_x_{}, speed_y_{};                          // +78, +7C
    u16 timer_{};                                        // +88
    std::shared_ptr<s8> hits_ = std::make_shared<s8>(0); // +8A
    u8 flash_{};                                         // +8B
    bool angry_{}; // +8C (set at the fourth hit; nothing reads it)
    u8 pick_{};    // +8E the path pair
    s16 bounce_{}; // +90
};

BossClaw::BossClaw(Game& g, CasinoParadiseBoss& boss, s16 base_x, s16 base_y, s32 x, s32 y)
    : Process(0x2800), game_(g), boss_(boss), x_(x), y_(y), base_x_(base_x), base_y_(base_y) {
    sprite_.start(g.sprite_tiles.allocate(0x24), 0x271, 0, 0, 0, 0x5C0, 0x2000);
}

void BossClaw::update(Game& g) {
    StageState& s = g.stage();
    switch (state_) {
    case State::waiting: // 080300F0
        if (go_) {
            const FlightPath f = FlightPath::load(
                g.assets(), g.assets().u8(casino_path_pairs + (boss_.pick() & 7) * 2u + 1),
                boss_.hits());
            x_ = f.x;
            y_ = f.y;
            speed_x_ = f.speed_x;
            speed_y_ = f.speed_y;
            if (x_ < -0x8C00) {
                sprite_.flags |= sprite_flag::flip_x;
            } else {
                sprite_.flags &= ~sprite_flag::flip_x;
            }
            go_ = false;
            state_ = State::flying;
        }
        return;
    case State::flying: { // 080301DC
        x_ += speed_x_;
        y_ += speed_y_;
        if (u32(x_ + 0x13800) > 0x15800 || y_ < -0xD000 || y_ > 0x2800) {
            state_ = State::waiting;
        }
        const s16 x = cx(), y = cy();
        animate(g, sprite_);
        const bool h0 = hazard_hits(g, sprite_, x, y, s.player[0]);
        const bool h1 = s.players == 2 && hazard_hits(g, sprite_, x, y, s.player[1]);
        if (h0 || h1) {
            boss_.laugh();
        }
        sprite_.x = s16(x - s.camera.x);
        sprite_.y = s16(y - s.camera.y);
        draw(g, sprite_);
        return;
    }
    case State::dropping:
    case State::falling:
        fall(g);
        return;
    }
}

// The boss broke: from its next update the claw drops.
void BossClaw::drop(Game&) {
    state_ = State::dropping;
}

// 08030334, 080303E4: thrown toward the view's middle (and up), it falls,
// bounces and smokes for 181 frames.
void BossClaw::fall(Game& g) {
    StageState& s = g.stage();
    if (state_ == State::dropping) {
        sprite_.x = s16(cx() - s.camera.x);
        sprite_.y = s16(cy() - s.camera.y);
        speed_x_ = cx() < s.camera.x + 0x78 ? 0x200 : -0x200;
        speed_y_ = speed_y_ == 0 ? -0x600 : speed_y_ + (speed_y_ < 0 ? -0x300 : 0x300);
        timer_ = 0;
        state_ = State::falling;
    }
    if (timer_++ >= 0xB5) {
        g.scheduler().destroy(*this);
        return;
    }
    speed_y_ += 0x28;
    x_ += speed_x_;
    y_ += speed_y_;
    const s16 x = cx(), y = cy();
    sprite_.x = s16(x - s.camera.x);
    sprite_.y = s16(y - s.camera.y);
    if (speed_y_ > 0) {
        const s32 d = probe_short(g.assets(), s.map, y + 0x18, x, 1, 8, nullptr, Sensor::floor);
        if (d < 0) {
            y_ += d * 0x100;
            speed_y_ = (speed_y_ >> 2) - speed_y_;
        }
    }
    if ((timer_ & 7) == 0) {
        const u32 r = s.next_random();
        create_smoke(g, (x + s32(r & 0x3F) - 0x20) * 0x100,
                     (y - (s32((r & 0x3F0000) >> 16) - 0x20)) * 0x100);
    }
    animate(g, sprite_);
    draw(g, sprite_);
}

void Hammer::update(Game& g) {
    StageState& s = g.stage();
    const Assets& a = g.assets();
    const SceneTransform& v = g.scene;
    if (state_ == State::knocked_off) {
        // 080275D0
        t_.angle = u16(own_turn() + v.angle);
        x_ = s16(((sine(a, v.angle + 0x100u) * 3) >> 9) + s.camera.x + v.focus_x);
        y_ = s16(((sine(a, v.angle) * 3) >> 9) + s.camera.y + v.focus_y);
        speed_x_ = x_ < s.camera.x + 0x78 ? 0x200 : -0x200;
        speed_y_ = -0x600;
        offset_x_ = offset_y_ = 0;
        timer_ = 0;
        spin_ = 0x2A;
        state_ = State::flying;
    }
    if (state_ == State::flying) {
        fly(g);
        return;
    }
    t_.angle = own_turn();
    transform_in_view(g, t_.sprite, t_.angle, 0x100, 0x100, 0x60, 0);
    if (boss_.blink() == 0) {
        const s16 x = s16(((sine(a, v.angle + 0x100u) * 3) >> 9) + s.camera.x + v.focus_x);
        const s16 y = s16(((sine(a, v.angle) * 3) >> 9) + s.camera.y + v.focus_y);
        const bool first = touches_player(t_.sprite, x, y, s.player[0]);
        const bool second = s.players == 2 && touches_player(t_.sprite, x, y, s.player[1]);
        if (first) {
            hurt_player(g, s.player[0]);
            boss_.laugh();
        }
        if (second) {
            hurt_player(g, s.player[1]);
            boss_.laugh();
        }
    }
    draw(g, t_.sprite);
}

// 080276E4
void Hammer::fly(Game& g) {
    StageState& s = g.stage();
    if (timer_++ >= 0xB5) {
        g.scheduler().destroy(*this);
        return;
    }
    speed_y_ = s16(speed_y_ + 0x28);
    offset_x_ += speed_x_;
    offset_y_ += speed_y_;
    if (speed_y_ > 0) {
        const s32 d = floor_distance(g, x_ + (offset_x_ >> 8), y_ + (offset_y_ >> 8));
        if (d < 0) {
            offset_y_ += d * 0x100;
            speed_y_ = s16((speed_y_ >> 2) - speed_y_);
            spin_ = s16(-spin_);
        }
    }
    if ((timer_ & 7) == 0) {
        create_smoke(g, x_ * 0x100 + offset_x_, y_ * 0x100 + offset_y_);
    }
    Sprite& sp = t_.sprite;
    sp.x = s16((offset_x_ >> 8) + x_ - s.camera.x);
    sp.y = s16((offset_y_ >> 8) + y_ - s.camera.y);
    t_.angle = u16((t_.angle + spin_) & 0x3FF);
    t_.x = sp.x;
    t_.y = sp.y;
    transform(g, t_);
    draw(g, sp);
}

class IceParadiseBoss;

// 0804C3D8 as a process of its own (FFFDh, made with the Ice Paradise
// boss): the underwater colours are made again every frame, so that the
// hit flash and fades show under water too.
class UnderwaterColors final : public Process {
public:
    UnderwaterColors() : Process(0xFFFD) {}

    void update(Game& g) override { build_underwater_colors(g); }
};

// 08031968, 08031A84: an icicle the Ice Paradise boss shakes down: from 32
// pixels above the arena's top at a random place up to 310 pixels left of
// its right end, falling with a random weight; in the water it slows to an
// eighth (unless the boss is broken). Until the boss is broken its second
// hitbox hurts; in the water it is a platform, carrying its riders down.
class Icicle final : public Process {
public:
    Icicle(Game& g, s16 x, s16 y, std::shared_ptr<s8> boss_hits)
        : Process(0x2200), x_(x), y_(y), boss_hits_(std::move(boss_hits)) {
        StageState& s = g.stage();
        const u32 r = s.next_random();
        s.next_random();
        offset_x_ = ((s32((r >> 1) & 0xF) * -0x14) - 10) * 0x100;
        weight_ = s32(s.next_random() & 0xF) + 10;
        sprite_.start(0x12720, 0x276, 0, 0, 0, 0x500, 0x2000);
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        const bool boss_broken = *boss_hits_ >= 8;
        bool in_water = false;
        s32 step;
        if (y_ + (offset_y_ >> 8) - 0x11 < s.water_level) {
            speed_ += weight_;
            step = speed_;
            offset_y_ += step;
        } else {
            in_water = true;
            speed_ += boss_broken ? weight_ : quotient(weight_, 8);
            step = quotient(speed_, 8);
            offset_y_ += step;
        }
        if (offset_y_ > 0xD000) {
            g.scheduler().destroy(*this);
            return;
        }
        const s16 x = s16(x_ + (offset_x_ >> 8)), y = s16(y_ + (offset_y_ >> 8));
        animate(g, sprite_);
        sprite_.x = s16(x - s.camera.x);
        sprite_.y = s16(y - s.camera.y);
        if (!boss_broken) {
            hazard_hits_second(g, sprite_, x, y, s.player[0]);
            if (s.players == 2) {
                hazard_hits_second(g, sprite_, x, y, s.player[1]);
            }
        }
        // (The first player only finds the platform in the water; the
        // partner always.)
        if (in_water) {
            collide_platform(g, sprite_, x, y, s.player[0]);
        }
        carry(s.player[0], step);
        if (s.players == 2) {
            collide_platform(g, sprite_, x, y, s.player[1]);
            carry(s.player[1], step);
        }
        draw(g, sprite_);
    }

private:
    void carry(Player& p, s32 step) {
        if (!(p.flags & player_flag::on_object) || p.standing_on != &sprite_) {
            return;
        }
        if (offset_y_ < 0xC401) {
            p.y += 0x100 + step;
        } else {
            p.flags &= ~player_flag::on_object;
        }
    }

    Sprite sprite_;          // +00
    s16 x_, y_;              // +44, +46 the boss's place
    s32 offset_x_{};         // +4C 8.8
    s32 offset_y_ = -0x2000; // +50
    s32 speed_{};            // +54
    s32 weight_{};           // +58
    std::shared_ptr<s8> boss_hits_;
};

// 08030A30: the Ice Paradise boss. The view closes in on its arena (320
// pixels left of its place, 192 down); the players wait while Eggman's
// machine floats in from the right (08030C74), stops, and he laughs. Then
// it moves to one of nine places (084A5DC6) at a pixel a frame (08030EB4),
// waits, and stomps for two seconds, shaking the view and the icicles down
// (one every 084A5DB4[hits] frames); after a stomp it rests for two
// seconds. It is hit by the player's attack (0800B8B4); eight hits (six
// with the save's byte +18 set) break it (080311A4): the water goes 160
// pixels down, it sinks slowly, exploding (08031450), smokes on the ground
// (080316DC), and Eggman escapes, leaving the capsule.
class IceParadiseBoss final : public Process {
public:
    IceParadiseBoss(Game& g, const ObjectSpawn& o) : Process(0x2000), game_(g) {
        StageState& s = g.stage();
        g.scheduler().create<UnderwaterColors>();
        base_x_ = s16(o.record[0] * 8 + o.cell_x * 0x100);
        base_y_ = s16(o.record[1] * 8 + o.cell_y * 0x100);
        *hits_ = g.save().unknown_5178 != 0 ? 2 : 0;
        vehicle_.start(g.sprite_tiles.allocate(0x40), 0x274, 2, base_x_, base_y_, 0x580, 0x2000);
        o.record[0] = 0xFE;
        eggman_.start(g.sprite_tiles.allocate(8), 0x275, 0, base_x_, base_y_, 0x540, 0x2000);
        g.scheduler().create<CameraBoundsXY>(g, base_y_, s16(base_y_ + 0xC0), s16(base_x_ - 0x140),
                                             base_x_);
        Player& p = s.player[0];
        p.ground_speed = 0;
        p.flags |= player_flag::frozen;
        // Knuckles crouching keeps holding down.
        p.held = (p.character == 2 && (p.animation == 0x10D || p.animation == 0xCF)) ? 0x80 : 0;
        lost_rings_bounce_off_ceilings(g);
    }

    // 08031C84
    void on_destroy() override {
        game_.sprite_tiles.free(vehicle_.tiles.to);
        game_.sprite_tiles.free(eggman_.tiles.to);
    }

    void update(Game& g) override {
        switch (state_) {
        case State::arriving:
            arrive(g);
            break;
        case State::fighting:
            fight(g);
            break;
        case State::sinking:
            sink(g);
            break;
        case State::smoking:
            smoke(g);
            break;
        }
    }

private:
    enum class State : u8 { arriving, fighting, sinking, smoking };
    static constexpr u32 places = 0x084A5DC6;        // nine x offsets (pixels)
    static constexpr u32 icicle_gaps = 0x084A5DB4;   // frames between icicles, by hits
    static constexpr u32 rider_heights = 0x084A5DD8; // Eggman's height by the machine's frame

    s16 bx() const { return s16(base_x_ + (x_ >> 8)); }

    s16 by() const { return s16(base_y_ + (y_ >> 8)); }

    // Eggman sits in the machine, bobbing with its frames.
    void seat_eggman(Game& g) {
        eggman_.x = vehicle_.x;
        eggman_.y = vehicle_.y;
        if (vehicle_.animation == 0x274) {
            const s8 frame = s8(g.assets().u8(vehicle_.frame + 1));
            eggman_.y = s16(vehicle_.y - g.assets().s16(rider_heights + frame * 2));
        }
    }

    // 0803099C: blinking white after a hit (while more than 0 of its 120
    // frames are left), then both drawn.
    void show(Game& g) {
        if (flash_ != 0) {
            const u8 b = --flash_;
            if (s8(b) > 0 && (b & 2) == 0 && !(g.stage().player[0].flags & player_flag::dead)) {
                vehicle_.flags |= 0x100;
                flash_on(g);
            }
        }
        draw(g, vehicle_);
        draw(g, eggman_);
    }

    void move_and_show(Game& g) {
        StageState& s = g.stage();
        vehicle_.x = s16(bx() - s.camera.x);
        vehicle_.y = s16(by() - s.camera.y);
        animate(g, vehicle_);
        animate(g, eggman_);
        seat_eggman(g);
        show(g);
    }

    // 08030C74
    void arrive(Game& g) {
        StageState& s = g.stage();
        x_ += speed_x_;
        y_ += speed_y_;
        move_and_show(g);
        Player& p = s.player[0];
        switch (phase_) {
        case 0:
            if (--timer_ == 0) {
                s.music_queued = 0x10;
                vehicle_.variant = 0;
                speed_x_ = -0x100;
                ++phase_;
            }
            if (p.animation == 0xD1 && p.variant == 2) {
                p.held = 0;
            }
            break;
        case 1:
            if (x_ < -0x77FF) {
                vehicle_.variant = 2;
                x_ = -0x7800;
                speed_x_ = 0;
                timer_ = 0x3C;
                ++phase_;
            }
            if (s.camera.shift_y != -0x60) {
                --s.camera.shift_y;
            }
            break;
        case 2:
            if (--timer_ == 0) {
                eggman_.variant = 1;
                ++phase_;
            }
            break;
        case 3:
            if (eggman_.flags & sprite_flag::ended) {
                eggman_.variant = 0;
                timer_ = 0x3C;
                ++phase_;
            }
            break;
        case 4:
            if (--timer_ == 0) {
                p.flags &= ~player_flag::frozen;
                p.held = u16(p.held | s.jump_buttons | s.other_buttons);
                s.camera.shift_y = 0;
                phase_ = 0;
                timer_ = 0x1E;
                state_ = State::fighting;
            }
            break;
        default:
            break;
        }
    }

    // 08030874: hits (the attack box on its hitbox) and touches.
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
            ++*hits_;
            flash_ = 0x78;
            eggman_.variant = 2;
            eggman_.flags &= ~sprite_flag::ended;
            g.sound().start(sound_boss_hit);
        } else if (c0 == BossContact::hurt || c1 == BossContact::hurt) {
            eggman_.variant = 1;
            eggman_.flags &= ~sprite_flag::ended;
        }
    }

    // 08030EB4
    void fight(Game& g) {
        StageState& s = g.stage();
        const Assets& a = g.assets();
        x_ += speed_x_;
        y_ += speed_y_;
        contact(g, bx(), by());
        if (*hits_ > 7) {
            broken(g);
            return;
        }
        vehicle_.x = s16(bx() - s.camera.x);
        vehicle_.y = s16(by() - s.camera.y);
        if (eggman_.variant != 0 && (eggman_.flags & sprite_flag::ended)) {
            eggman_.variant = 0;
            eggman_.previous_variant = 0xFF;
        }
        animate(g, vehicle_);
        animate(g, eggman_);
        seat_eggman(g);
        show(g);
        if (rest_ != 0) {
            --rest_;
        }
        switch (phase_) {
        case 0: // a new place
            if (--timer_ == 0) {
                vehicle_.variant = 0;
                const u32 i = s.next_random() % 9;
                target_ = a.s16(places + i * 2) * 0x100;
                if (x_ == target_) {
                    target_ = a.s16(places + u32((s32(i) + 5) % 9) * 2) * 0x100;
                }
                speed_x_ = target_ < x_ ? -0x100 : 0x100;
                phase_ = 1;
            }
            break;
        case 1: // going there
            if (speed_x_ < 1 ? x_ < target_ : target_ < x_) {
                x_ = target_;
                speed_x_ = 0;
                timer_ = 0x1E;
                phase_ = 2;
                vehicle_.variant = 2;
            }
            break;
        case 2: // waiting
            if (--timer_ == 0) {
                if (rest_ != 0) {
                    timer_ = 0x1E;
                    phase_ = 0;
                } else {
                    timer_ = 0x78;
                    phase_ = 3;
                    vehicle_.variant = 1;
                    if (!(s.player[0].flags & player_flag::dead)) {
                        shake_view(g, 0x400, 0, 0x100, 0x78, 0x80);
                    }
                }
            }
            break;
        case 3: // stomping
            if (--timer_ == 0) {
                timer_ = 0x1E;
                phase_ = 0;
                rest_ = 0x78;
                vehicle_.variant = 2;
            } else if (s16(timer_) % a.s16(icicle_gaps + u32(*hits_) * 2) == 0 &&
                       !(s.player[0].flags & player_flag::dead)) {
                g.scheduler().create<Icicle>(g, base_x_, base_y_, hits_);
                g.sound().start(sound_swing_lands);
            }
            break;
        default:
            break;
        }
    }

    // 080311A4: the last hit: three pieces fly off, the view stops, the
    // water goes down, points.
    void broken(Game& g) {
        StageState& s = g.stage();
        const s16 x = bx(), y = by();
        eggman_.variant = 2;
        fall_speed_ = 0;
        g.sound().start(sound_boss_broken);
        timer_ = 0;
        vehicle_.animation = 0x272;
        vehicle_.variant = 0;
        vehicle_.previous_variant = 0xFF;
        const bool flipped = vehicle_.flags & sprite_flag::flip_x;
        Debris& top = g.scheduler().create<Debris>(g, x, y, 0x10, 0x273, 0);
        top.speed_y = 0;
        top.growth = 0;
        top.spin = 0x80;
        if (flipped) {
            top.t.scale_x = -0x100;
            top.speed_x = 0x100;
        } else {
            top.speed_x = -0x100;
        }
        top.timer = 0x3C;
        Debris& right =
            g.scheduler().create<Debris>(g, s16(x + 0x10), s16(y + 0x10), 0xC, 0x273, 1);
        right.speed_y = 0;
        right.growth = 0;
        right.spin = 0x80;
        if (flipped) {
            right.t.scale_x = -0x100;
            right.speed_x = 0x400;
        } else {
            right.speed_x = -0x400;
        }
        right.timer = 0x3C;
        Debris& left = g.scheduler().create<Debris>(g, s16(x - 0x10), s16(y + 0x10), 0xC, 0x273, 1);
        left.speed_y = 0;
        left.growth = 0;
        left.spin = 0x380;
        left.t.sprite.flags |= sprite_flag::flip_x;
        if (flipped) {
            left.t.scale_x = -0x100;
            left.speed_x = -0x400;
        } else {
            left.speed_x = 0x400;
        }
        left.timer = 0x3C;
        auto& c = s.camera;
        c.min_x = c.x;
        c.max_x = s16(c.x + 0xF0);
        // The bounds only start moving later (the current ones are kept).
        const s16 min_y = c.min_y, max_y = c.max_y;
        g.scheduler().create<CameraBounds>(g, s16(max_y - 0xA0), max_y);
        c.min_y = min_y;
        c.max_y = max_y;
        s.water.target = s16(s.water.target + 0xA0);
        const u32 before = s.score;
        s.score += 1000;
        if (divide(s32(s.score), 50000) != divide(s32(before), 50000) && g.game_mode == 0) {
            ++s.lives;
        }
        state_ = State::sinking;
        sink(g);
    }

    // 08031450: sinks, exploding, until it rests on the ground (after two
    // seconds at least).
    void sink(Game& g) {
        StageState& s = g.stage();
        fall_speed_ = s16(fall_speed_ + 2);
        y_ += fall_speed_;
        const s16 x = bx(), y = by();
        if (timer_ > 0x78 && floor_distance(g, x, y + 0x38) < 0) {
            fall_speed_ = 0;
            state_ = State::smoking;
        }
        vehicle_.flags &= ~sprite_flag::mode;
        flash_off(g);
        if ((++timer_ & 7) == 0) {
            create_explosion_piece(g, x, y, -0x20, 5);
        }
        vehicle_.x = s16(x - s.camera.x);
        vehicle_.y = s16(y - s.camera.y);
        seat_eggman(g);
        animate(g, vehicle_);
        animate(g, eggman_);
        draw(g, vehicle_);
        draw(g, eggman_);
    }

    // 080316DC: five seconds of smoke, then Eggman flies off.
    void smoke(Game& g) {
        StageState& s = g.stage();
        const s16 x = bx(), y = by();
        if (!(s.player[0].flags & player_flag::dead)) {
            g.video().settings.sprite_window = false;
            vehicle_.flags &= ~sprite_flag::mode;
        }
        if (timer_++ > 0x12C) {
            g.scheduler().create<EggmanEscape>(x, s16(y + 0x28));
            g.scheduler().create<Capsule>(g, x, y);
            s.music_queued = 0x30;
            g.scheduler().destroy(*this);
            return;
        }
        if ((timer_ & 7) == 0) {
            create_explosion_piece(g, x, y, -0x20, 5);
        }
        gba::Settings& v = g.video().settings;
        v.outside = 0;
        v.inside_sprite_window = 0;
        v.blend.set_control(0);
        v.blend.evy = 0;
        vehicle_.x = s16(x - s.camera.x);
        vehicle_.y = s16(y - s.camera.y);
        seat_eggman(g);
        animate(g, vehicle_);
        animate(g, eggman_);
        draw(g, vehicle_);
        draw(g, eggman_);
    }

    State state_ = State::arriving;
    Game& game_;
    s16 base_x_, base_y_;
    Sprite vehicle_;                                     // +0C
    Sprite eggman_;                                      // +3C
    s32 x_ = 0x3000, y_ = 0x1000;                        // +6C, +70 8.8 from its place
    s32 speed_x_{}, speed_y_{};                          // +74, +78
    s32 target_{};                                       // +80
    u16 timer_ = 0x78;                                   // +84
    std::shared_ptr<s8> hits_ = std::make_shared<s8>(0); // +86
    u8 flash_{};                                         // +87
    s16 fall_speed_{};                                   // +88
    u16 rest_{}; // +8A frames before the next stomp may come
    u8 phase_{}; // +8C
};

// 0804F068, 0804F1A0: a homing missile from the Angel Island boss: from
// its hands, slowly at first; after 16 frames it speeds up along its
// heading, after 24 it turns (3/256 of a turn a frame) toward where the
// player will be. It trails dust and bursts into smoke on a player or
// out of view. Its sound (C8h) stops with it (0804FDEC).
class HomingMissile final : public Process {
public:
    HomingMissile(Game& g, s32 x, s32 y, bool rightward) : Process(0x2001), game_(g), x_(x), y_(y) {
        StageState& s = g.stage();
        speed_x_ = rightward ? 0x40 : -0x40;
        accel_ = (s.next_random() & 0x1000) ? 0x20 : 0x40;
        heading_ = rightward ? 0 : 0x80;
        t_.sprite.start(g.sprite_tiles.allocate(0x40), 0x290, 0, 0, 0, 0x540, 0x2000);
        t_.scale_x = rightward ? -0x100 : 0x100;
    }

    void on_destroy() override {
        game_.sprite_tiles.free(t_.sprite.tiles.to);
        game_.sound().stop(0xC8);
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        const Assets& a = g.assets();
        const auto cos = [&](unsigned d) {
            return s32(sine(a, d * 4u + 0x100) >> 6);
        };
        const auto sin = [&](unsigned d) {
            return s32(sine(a, d * 4u) >> 6);
        };
        unsigned d = heading_;
        if (age_ > 0x18) {
            const Player& p = s.player[0];
            const s32 dy = p.y - y_;
            const s32 dx = std::abs(p.x - x_);
            const s32 across = std::abs(speed_x_ + cos(d) * 8);
            const s32 then = quotient(dx * (speed_y_ + sin(d) * 8), across);
            if (then < dy - 0x800) {
                d += speed_x_ < 1 ? -3 : 3;
            } else if (dy < then) {
                d += speed_x_ < 1 ? 3 : -3;
            }
            d &= 0xFF;
            heading_ = u8(d);
        }
        const s32 x = x_ + speed_x_, y = y_ + speed_y_;
        if (age_ > 0x10) {
            speed_x_ = s16(speed_x_ + s16(u32(accel_ * cos(d)) >> 8));
            speed_y_ = s16(speed_y_ + s16(u32(accel_ * sin(d)) >> 8));
        }
        x_ = x;
        y_ = y;
        bool hit = hazard_hits(g, t_.sprite, s16(x >> 8), s16(y >> 8), s.player[0]);
        if (s.players == 2) {
            hit = hazard_hits(g, t_.sprite, s16(x >> 8), s16(y >> 8), s.player[1]) || hit;
        }
        if (hit) {
            create_smoke(g, x + ((cos(d) * 0x1500) >> 8), y + ((sin(d) * 0x1500) >> 8));
            g.scheduler().destroy(*this);
            return;
        }
        const s32 sx = (x >> 8) - s.camera.x, sy = (y >> 8) - s.camera.y;
        const s32 back = s32(s.next_random() & 0x3FF) - 0x1A00;
        create_skid_dust(g, s16((x + ((back * cos(d)) >> 8)) >> 8),
                         s16((y + ((back * sin(d)) >> 8)) >> 8));
        if (u32(sx + 0x20) >= 0x130 || u32(sy + 0x40) >= 0x120) {
            g.scheduler().destroy(*this);
            return;
        }
        ++age_;
        t_.x = s16(sx);
        t_.y = s16(sy);
        if (t_.scale_x < 0) {
            d += 0x80;
        }
        t_.angle = u16(((d + 0x80) * 4) & 0x3FF);
        t_.sprite.flags = 0x2000 | (u32(s.next_rotation_set++) | 0x20);
        animate(g, t_.sprite);
        transform(g, t_);
        draw(g, t_.sprite);
    }

private:
    Game& game_;
    s32 x_, y_;           // +00, +04 (8.8)
    s16 speed_x_{};       // +08
    s16 speed_y_{};       // +0A
    s16 accel_;           // +0C
    s16 age_{};           // +10
    u8 heading_;          // +12 (256 a turn)
    TransformedSprite t_; // sprite +14, transform +44
};

// 0804F488, 0804F5FC: a piece of the broken Angel Island boss (the table
// 084A7444 by index: animation, variant, tiles), thrown up at random,
// falling and spinning (the spin slowing by 8 a frame), until out of view.
class BossPiece final : public Process {
public:
    BossPiece(Game& g, s32 x, s32 y, unsigned index) : Process(0x4001), game_(g), x_(x), y_(y) {
        StageState& s = g.stage();
        const Assets& a = g.assets();
        const u32 entry = 0x084A7444 + index * 4;
        t_.sprite.start(g.sprite_tiles.allocate(a.u8(entry + 3)), a.u16(entry), a.u8(entry + 2), 0,
                        0, 0x4C0, 0x2000);
        speed_x_ = s16(((s.next_random() >> 8) & 0x7FF) - 0x400);
        speed_y_ = s16(-0x200 - ((s.next_random() >> 8) & 0x1FF));
        accel_y_ = s16(0x2A - ((s.next_random() >> 8) & 0x1F));
        angle_ = u16(s.next_random() >> 8);
        spin_ = s16(((s.next_random() >> 8) & 0xFFF) - 0x800);
        t_.scale_x = t_.scale_y = 0x100;
    }

    void on_destroy() override { game_.sprite_tiles.free(t_.sprite.tiles.to); } // 0804FE08

    void update(Game& g) override {
        StageState& s = g.stage();
        x_ += speed_x_;
        y_ += speed_y_;
        speed_y_ = s16(speed_y_ + accel_y_);
        angle_ = u16(angle_ + spin_);
        if (spin_ > 0) {
            spin_ = s16(std::max(spin_ - 8, 0));
        } else if (spin_ < 0) {
            spin_ = s16(std::min(spin_ + 8, 0));
        }
        const s32 sx = (x_ >> 8) - s.camera.x, sy = (y_ >> 8) - s.camera.y;
        if (u32(sx + 0x20) >= 0x130 || sy >= 0xE0) {
            g.scheduler().destroy(*this);
            return;
        }
        t_.x = s16(sx);
        t_.y = s16(sy);
        t_.angle = u16(angle_ >> 6);
        t_.sprite.flags =
            (t_.sprite.flags & ~sprite_flag::rotation_set) | (u32(s.next_rotation_set++) | 0x20);
        animate(g, t_.sprite);
        transform(g, t_);
        draw(g, t_.sprite);
    }

private:
    Game& game_;
    s32 x_, y_;             // +00, +04
    s16 speed_x_, speed_y_; // +08, +0A (the across acceleration +0C is 0)
    s16 accel_y_;           // +0E
    u16 angle_;             // +12
    s16 spin_;              // +14
    TransformedSprite t_;   // sprite +18, transform +48
};

// 0804E4C8, 0804E560: the Angel Island boss, a fighter run by the state
// table at 084A70C0 (25 records of 24h bytes: a routine each frame and one
// on entry, speeds and accelerations, a timer, flags, its half size and
// its picture in each of its two forms). The records are read from the
// cartridge; the routines are the ones below, chosen by their addresses.
// It closes the view in, waits for the player, jumps, rolls, kicks, blocks
// (the player bounces off), fires homing missiles; hit while not blocking
// (0800C0E0) it reels; at half its strength it takes its second form; the
// last hit throws it out of the arena in pieces, the capsule comes and it
// runs off to the right.
class AngelIslandBoss final : public Process {
public:
    AngelIslandBoss(Game& g, const ObjectSpawn& o) : Process(0x2000), game_(g) {
        base_x_ = o.x();
        base_y_ = o.y();
        o.record[0] = 0xFE;
        const u8 easy = g.save().unknown_5178;
        hits_ = s8(8 - easy * 2);
        threshold_ = s8(4 - easy);
        body_.start(g.sprite_tiles.allocate(0x40), 0, 0, 0, 0, 0x500, 0x2000);
        extra_.start(g.sprite_tiles.allocate(0x1E), 0, 0, 0, 0, 0x500, 0x2000);
        enter(g, 0);
        lost_rings_bounce_off_ceilings(g);
    }

    void on_destroy() override { // 0804FD84
        game_.sprite_tiles.free(body_.tiles.to);
        game_.sprite_tiles.free(extra_.tiles.to);
    }

    void update(Game& g) override;

private:
    static constexpr u32 states = 0x084A70C0;
    // Flags (+88).
    static constexpr u32 on_ground = 1, at_ceiling = 2, wall_left = 4, wall_right = 8,
                         land_inside = 0x10, show_body = 0x20, show_extra = 0x40, keep_x = 0x4000,
                         keep_y = 0x8000, no_ground = 0x2000000, no_walls = 0x4000000;

    s32 bx() const { return base_x_ + (x_ >> 8); }

    s32 by() const { return base_y_ + (y_ >> 8); }

    void enter(Game& g, u8 state);
    bool run(Game& g, u32 routine);
    void entered(Game& g, u32 routine);
    u8 choose(Game& g);
    bool smoke(Game& g);
    void move();
    void keep_inside();
    void ground(Game& g);

    Game& game_;
    s32 base_x_, base_y_;       // +8C, +90
    Sprite body_;               // +0C
    Sprite extra_;              // +44
    s32 x_ = 0xD000;            // +74 8.8 from its place
    s32 y_ = -0x2000;           // +78
    s16 speed_x_{}, speed_y_{}; // +7C, +7E
    s16 accel_x_{}, accel_y_{}; // +80, +82
    s16 timer_{};               // +84
    u32 flags_{};               // +88
    u8 form_{};                 // +94
    s8 hits_;                   // +95 hits left
    s8 threshold_;              // +96 the second form from here
    s8 flash_{};                // +97
    s8 half_w_{}, half_h_{};    // +98, +99
    u8 state_{};                // +9A
    u8 next_{};                 // +9B
};

// 0804E560
void AngelIslandBoss::update(Game& g) {
    StageState& s = g.stage();
    Player& p0 = s.player[0];
    if (!(p0.flags & player_flag::dead)) {
        g.video().settings.sprite_window = false;
        body_.flags &= ~sprite_flag::mode;
    }
    bool stay = run(g, g.assets().u32(states + state_ * 0x24u));
    move(); // 0804EAD0
    if (!(flags_ & no_walls)) {
        keep_inside(); // 0804FDA0
    }
    if (!(flags_ & no_ground)) {
        ground(g); // 0804EB5C
    }
    const s16 x = s16(bx()), y = s16(by());
    bool hurt = hazard_hits_second(g, body_, x, y, p0);
    if (s.players == 2) {
        hurt = hazard_hits_second(g, body_, x, y, s.player[1]) || hurt;
    }
    if (!hurt && flash_ < 1) {
        const bool a0 = attack_bounces(g, body_, x, y, p0);
        const bool a1 = s.players == 2 && attack_bounces(g, body_, x, y, s.player[1]);
        Player* bounced = a0 ? &p0 : a1 ? &s.player[1] : nullptr;
        if (bounced) {
            const bool blocking = state_ == 5 || state_ == 0xA;
            if (blocking && !(bounced->flags & player_flag::in_air)) {
                // Blocked: the bounce is undone (the player keeps going).
                if (state_ != 0xA) {
                    next_ = 0xA;
                    stay = false;
                    bounced->speed_x = s16(-bounced->speed_x);
                    bounced->speed_y = s16(-bounced->speed_y);
                }
            } else {
                stay = false;
                next_ = 0x11;
                if (--hits_ < 1) {
                    next_ = 0x13;
                }
                flash_ = 0x20;
            }
        }
    }
    if (flash_ != 0) {
        if (!(s.ticks & 2) && !(p0.flags & player_flag::dead)) {
            body_.flags |= 0x100;
            auto& v = g.video().settings;
            v.sprite_window = true;
            v.outside = 0x3F;
            v.inside_sprite_window = 0x3F;
        }
        if (u8(next_ - 0x11) > 1) {
            --flash_;
        }
    }
    body_.x = extra_.x = s16(x - s.camera.x);
    body_.y = extra_.y = s16(y - s.camera.y);
    if (flags_ & show_body) {
        animate(g, body_);
        draw(g, body_);
    }
    if (flags_ & show_extra) {
        animate(g, extra_);
        draw(g, extra_);
    }
    if (!stay) {
        enter(g, next_);
    }
}

// 0804EAD0: speeds and accelerations; with the flags 4000h/8000h an
// acceleration stops where the speed would change sign.
void AngelIslandBoss::move() {
    x_ += speed_x_;
    y_ += speed_y_;
    s32 nx = speed_x_ + accel_x_;
    if ((flags_ & keep_x) && ((speed_x_ < 0) != (nx < 0))) {
        nx = 0;
        accel_x_ = 0;
    }
    speed_x_ = s16(nx);
    s32 ny = speed_y_ + accel_y_;
    if ((flags_ & keep_y) && ((speed_y_ < 0) != (ny < 0))) {
        ny = 0;
        accel_y_ = 0;
    }
    speed_y_ = s16(ny);
}

// 0804FDA0: the arena's walls (6 and EAh pixels from its left end).
void AngelIslandBoss::keep_inside() {
    flags_ &= ~(wall_left | wall_right);
    s32 d = (x_ >> 8) - half_w_ - 6;
    if (d < 0) {
        x_ -= d * 0x100;
        flags_ |= wall_left;
    }
    d = (x_ >> 8) + half_w_ - 0xEA;
    if (d > 0) {
        x_ -= d * 0x100;
        flags_ |= wall_right;
    }
}

// 0804EB5C: the floor below (within 10 pixels, or only inside it with
// flag 10h) and the ceiling while rising.
void AngelIslandBoss::ground(Game& g) {
    const StageState& s = g.stage();
    const Assets& a = g.assets();
    const s32 x = bx(), y = by();
    flags_ &= ~on_ground;
    const s32 d = probe(a, s.map, y + half_h_, x, 1, 8, nullptr, Sensor::floor);
    if ((flags_ & land_inside) ? d < 0 : d <= 10) {
        y_ += d * 0x100;
        flags_ |= on_ground;
    }
    flags_ &= ~at_ceiling;
    if (speed_y_ < 0) {
        const s32 c = probe(a, s.map, y - half_h_, x, 1, -8, nullptr, Sensor::floor);
        if (c < 0) {
            y_ -= c * 0x100;
            flags_ |= at_ceiling;
        }
    }
}

// 0804E8A0: a state starts with its record's values.
void AngelIslandBoss::enter(Game& g, u8 state) {
    const Assets& a = g.assets();
    const StageState& s = g.stage();
    state_ = state;
    const u32 r = states + state * 0x24u;
    s16 sx = a.s16(r + 8), ax = a.s16(r + 0xC);
    const u32 rf = a.u32(r + 0x14);
    u32 f = flags_;
    if (!(rf & 0x400)) {
        f = (f & 0xFFFFF8FFu) | (rf & 0x300);
    }
    if (!(rf & 0x2000)) {
        f = (f & 0xFFFFC7FFu) | (rf & 0x1800);
    }
    u32 u = f & 0xF9FF3FEFu;
    if (rf & 0x10) {
        u = f & 0xF9FF3FEEu;
    }
    flags_ = u | (rf & (0x400 | 0x2000 | 0x10 | 0xC000 | 0x2000000 | 0x4000000));
    timer_ = a.s16(r + 0x10);
    half_w_ = s8(a.u8(r + 0x18));
    const s8 h = s8(a.u8(r + 0x19));
    if (h != half_h_) {
        y_ += (h - half_h_) * 0x100;
        half_h_ = h;
    }
    // The direction of the speeds: away from the middle, or at the player.
    const s32 px = s.player[0].x >> 8;
    if ((u & 0x1800) == 0x800) {
        if ((x_ >> 8) > 0x77) {
            sx = s16(-sx);
            ax = s16(-ax);
        }
    } else if ((u & 0x1800) == 0x1000 && px <= (x_ >> 8) + base_x_) {
        sx = s16(-sx);
        ax = s16(-ax);
    }
    speed_x_ = sx;
    speed_y_ = a.s16(r + 0xA);
    accel_x_ = ax;
    accel_y_ = a.s16(r + 0xE);
    // Facing: to the middle, or to the player.
    if ((u & 0x300) == 0x100) {
        if ((x_ >> 8) < 0x78) {
            body_.flags |= sprite_flag::flip_x;
        } else {
            body_.flags &= ~sprite_flag::flip_x;
        }
    } else if ((u & 0x300) == 0x200) {
        if ((x_ >> 8) + base_x_ < px) {
            body_.flags |= sprite_flag::flip_x;
        } else {
            body_.flags &= ~sprite_flag::flip_x;
        }
    }
    const u32 look = r + 0x1C + form_ * 4u;
    const s16 animation = a.s16(look);
    if (animation >= 0 && (body_.animation != u16(animation) || body_.variant != a.u8(look + 2))) {
        body_.animation = u16(animation);
        body_.variant = a.u8(look + 2);
        body_.previous_variant = 0xFF;
        body_.flags &= ~sprite_flag::ended;
        body_.hitboxes[0].index = body_.hitboxes[1].index = 0xFFFFFFFF;
    }
    entered(g, a.u32(r + 4));
}

// 0804ED4C: the next move, by the distance to the player and chance.
u8 AngelIslandBoss::choose(Game& g) {
    StageState& s = g.stage();
    const Player& p = s.player[0];
    const s32 d = std::abs((p.x >> 8) - bx());
    const u32 r = s.next_random();
    const bool in_air = p.flags & player_flag::in_air;
    if (form_ == 0) {
        if (d < 0x50) {
            if (d < 0x20) {
                return in_air ? 6 : 0xD;
            }
            if (r & 0x1000) {
                return 5;
            }
        }
    } else if (d < 0x50) {
        if (in_air) {
            return 6;
        }
    } else if (r & 0x3000) {
        return 0xF;
    }
    return (r & 0x100) ? 0xB : 6;
}

// 0804EDEC: smoke every eighth frame near it; true when it smoked.
bool AngelIslandBoss::smoke(Game& g) {
    StageState& s = g.stage();
    if (s.ticks & 7) {
        return false;
    }
    const u32 r1 = s.next_random(), r2 = s.next_random();
    Particle& p = create_smoke(g, x_ + base_x_ * 0x100 + s32((r1 & 0x1FFF00) >> 8) - 0x1000,
                               y_ + base_y_ * 0x100 + s32((r2 & 0x1FFF00) >> 8) - 0x1000);
    p.sprite.layer = 0x4C0;
    return true;
}

// The states' routines each frame (true: the state goes on).
bool AngelIslandBoss::run(Game& g, u32 routine) {
    StageState& s = g.stage();
    Player& p = s.player[0];
    auto& c = s.camera;
    const bool ended = body_.flags & sprite_flag::ended;
    const auto go = [&](u8 n) {
        next_ = n;
        return false;
    };
    const auto bounds_y = [&] {
        if (base_y_ < c.max_y) {
            --c.max_y;
        } else if (c.max_y < base_y_) {
            ++c.max_y;
        }
        if (base_y_ - 0xA0 < c.min_y) {
            --c.min_y;
        } else if (c.min_y < base_y_ - 0xA0) {
            ++c.min_y;
        }
    };
    const auto creep = [&] { // the view's left end follows to the right
        if (c.min_x < c.max_x - 0xF0) {
            ++c.min_x;
        }
        if (c.min_x < c.x) {
            c.min_x = c.x;
        }
    };
    switch (routine) {
    case 0x0804EE75: // waits until the player is 32 pixels past its place
        c.min_x = c.x;
        bounds_y();
        return (p.x >> 8) < base_x_ + 0x20 || go(1);
    case 0x0804EEE5: // the view closes in
        if (c.min_x < base_x_) {
            ++c.min_x;
        }
        bounds_y();
        return !(base_x_ <= c.min_x && c.max_y <= base_y_) || go(2);
    case 0x0804F951:
        return go(3);
    case 0x0804F95D:
        return !ended || go(4);
    case 0x0804F97D: // the player's turn ends: the fight starts
        if (!(p.body->sprite.flags & sprite_flag::ended)) {
            return true;
        }
        p.flags &= ~player_flag::frozen;
        p.held = u16(s.jump_buttons | s.other_buttons | p.held);
        p.flags &= ~(player_flag::frozen | player_flag::keep_state);
        p.state = 0;
        return go(5);
    case 0x0804F9D5:
        return timer_-- > 0 || go(choose(g));
    case 0x0804FA05:
        return speed_y_ < 0 || go(7);
    case 0x0804EF6D: { // flies at the player, bobbing
        speed_y_ = s16(speed_y_ + (speed_y_ < 0x80 ? 0x18 : -0x18));
        const s32 d = (p.x >> 8) - bx();
        const bool done = (flags_ & (wall_left | wall_right)) || (speed_x_ < 0 && d > 0x20) ||
                          (speed_x_ > 0 && d < -0x20);
        return !done || go(8);
    }
    case 0x0804FA21:
        return !(flags_ & on_ground) || go(9);
    case 0x0804FA3D:
    case 0x0804FA69:
    case 0x0804FAFD:
    case 0x0804FB49:
        return !ended || go(choose(g));
    case 0x0804FA95:
        return timer_-- > 0 || go(0xC);
    case 0x0804FABD:
        return !(flags_ & (wall_left | wall_right)) || go(5);
    case 0x0804FADD:
        return !ended || go(0xE);
    case 0x0804FB29:
        return !ended || go(0x10);
    case 0x0804FB75: // reeling
        if (hits_ == threshold_) {
            smoke(g);
        }
        return !(flags_ & on_ground) || go(0x12);
    case 0x0804FBB1:
        if (hits_ == threshold_) {
            smoke(g);
        }
        return !ended || go(choose(g));
    case 0x0804FBF5: // broken: bounces off the walls
        smoke(g);
        if (flags_ & wall_left) {
            speed_x_ = s16(-speed_x_);
        }
        if (flags_ & wall_right) {
            speed_x_ = s16(-speed_x_);
        }
        return !(flags_ & on_ground) || go(0x14);
    case 0x0804FC45:
        smoke(g);
        return timer_-- > 0 || go(0x15);
    case 0x0804FC71:
        if (!smoke(g)) {
            return true;
        }
        for (unsigned i = 0; i < 5; ++i) {
            g.scheduler().create<BossPiece>(g, base_x_ * 0x100 + x_, base_y_ * 0x100 + y_, i);
        }
        return go(0x16);
    case 0x0804FCB9: // the view opens to the right
        creep();
        return !(bx() + 0x20 <= c.x + 0xF0 && c.max_x - 0xF0 <= c.min_x) || go(0x17);
    case 0x0804EFED: // the capsule
        creep();
        if (!ended) {
            return true;
        }
        g.scheduler().create<Capsule>(g, s16(bx()), s16(by()));
        s.music_queued = 0x32;
        return go(0x18);
    case 0x0804FD21: // runs off to the right
        creep();
        if (bx() >= c.max_x + 0x20) {
            flags_ &= ~(show_body | show_extra);
            g.scheduler().destroy(*this);
        }
        return true;
    default:
        throw Unrecovered("an Angel Island boss state routine");
    }
}

// The states' routines on entry.
void AngelIslandBoss::entered(Game& g, u32 routine) {
    StageState& s = g.stage();
    Player& p = s.player[0];
    auto& c = s.camera;
    switch (routine) {
    case 0x0804F709: // the arena: the view's right end and height
        c.max_x = s16(base_x_ + 0xF0);
        c.min_y = c.y;
        c.max_y = s16(c.y + 0xA0);
        flags_ |= show_body;
        break;
    case 0x0804F72D: // the player waits
        c.min_x = c.x;
        p.ground_speed = p.speed_x = p.speed_y = 0;
        p.flags = (p.flags | player_flag::frozen) & ~player_flag::facing_left;
        p.held = p.pressed = 0;
        break;
    case 0x0804F75D: // the player's turn (state 27h), the music
        p.state = 0x27;
        p.flags |= player_flag::keep_state;
        s.music_queued = 0x11;
        break;
    case 0x0804F781:
        g.sound().start(0x74);
        break;
    case 0x0804F79D:
        g.sound().start(0x6D);
        break;
    case 0x0804F7A9:
        g.sound().start(0x6E);
        break;
    case 0x0804F7C1: // a missile; the view shakes
        g.scheduler().create<HomingMissile>(g, base_x_ * 0x100 + x_, base_y_ * 0x100 + y_ - 0x600,
                                            body_.flags & sprite_flag::flip_x);
        g.sound().start(0xC8);
        shake_view(g, 0x200, 0x10, 0x100, -1, 0x80);
        shake_view(g, 0x400, 0x10, 0x80, -1, 0x40);
        break;
    case 0x0804F801: // reeling: at the threshold half the push
        if (hits_ == threshold_) {
            accel_x_ = s16(accel_x_ >> 1);
            accel_y_ = s16(accel_y_ >> 1);
        }
        break;
    case 0x0804F82D: // the second form
        if (hits_ == threshold_) {
            form_ = 1;
            s.music_queued = 0x12;
        }
        break;
    case 0x0804F855: { // broken: points
        const u32 before = s.score;
        s.score += 1000;
        if (divide(s32(s.score), 50000) != divide(s32(before), 50000) && g.game_mode == 0) {
            ++s.lives;
        }
        break;
    }
    case 0x0804F8A9: // the run off: from the arena's right
        x_ = 0x11000;
        y_ = -0x6400;
        c.max_x = s16(s.map.width);
        body_.flags &= ~sprite_flag::flip_x;
        extra_.flags &= ~sprite_flag::flip_x;
        extra_.animation = 0x2B6;
        extra_.variant = 3;
        extra_.previous_variant = 0xFF;
        flags_ |= show_extra;
        break;
    case 0x0804F901:
        body_.flags &= ~sprite_flag::flip_x;
        extra_.animation = 0x2B6;
        extra_.variant = 9;
        extra_.previous_variant = 0xFF;
        break;
    case 0x0804F929:
        body_.flags |= sprite_flag::flip_x;
        extra_.animation = 0x2B6;
        extra_.variant = 6;
        extra_.previous_variant = 0xFF;
        break;
    case 0x0804F755:
    case 0x0804F759:
    case 0x0804F77D:
    case 0x0804F78D:
    case 0x0804F791:
    case 0x0804F795:
    case 0x0804F799:
    case 0x0804F7B5:
    case 0x0804F7B9:
    case 0x0804F7BD:
    case 0x0804F8A1:
    case 0x0804F8A5:
        break;
    default:
        throw Unrecovered("an Angel Island boss state's start");
    }
}

// Act 11's boss (enemy kind 27, 08031F98): Eggman's pod swings like a
// pendulum under a bridge of nine planks (08033848), riding along the
// bridge's sag. Its gun (080332DC) fires at the player. Standing on a plank
// bends the bridge; landing on it hard from a jump springs it, and a
// springing plank near the pod knocks the pod up (it opens: Eggman shows and
// can be hit from the air). Eight hits (six with the save's byte +18 set)
// break it; it falls, then Eggman escapes in a capsule pod and the results
// follow (no animal capsule: the zone's end).

// The bridge's sag: the planks' heights below their rest (03005870, nine
// halfwords) and what the bridge keeps (the bridge process's record).
struct CosmicBridge {
    std::array<s16, 9> heights{};  // 03005870
    std::array<s16, 9> previous{}; // 03005890 the heights before this frame
    Sprite sprite;                 // +00 one plank's picture (all planks draw it)
    u32 tiles{};                   // +04
    u8 state{};                    // +54 0 resting, 1 bent under a player, 2 springing
    u8 sprung{};                   // +55 the springing plank
    u8 stood_on{};                 // +56 the plank a player last stood on
    bool kicking{};                // +57 the spring's first swing (it can knock the pod)
    bool gone{};                   // +58 the pod has landed: the bridge flickers and goes
    s16 limit{};                   // +5A the spring's reach
    s16 velocity{};                // +5C
    u16 ticks{};                   // +5E
    // 03005860-0300586C: the first player as last seen.
    s32 last_x{}, last_y{};
    s16 last_speed_x{}, last_speed_y{};
    u32 last_flags{};
};

class CosmicAngelBoss;

// 0803398C, 08034068: a plank (one process each, priority 2001h + its
// number): drawn with the bridge's picture (plank 0 queues it, the others
// copy its sprite table entry to their places); flickering once the bridge
// is gone; ended once more than F0h above the view.
class BridgePlank final : public Process {
public:
    BridgePlank(std::shared_ptr<CosmicBridge> bridge, u8 index, s16 base_x, s16 base_y)
        : Process(u16(0x2001 + index)), bridge_(std::move(bridge)), index_(index), base_x_(base_x),
          base_y_(base_y), offset_x_(index * 0x2000 - 0x7000) {
        sprite.hitboxes[0] = bridge_->sprite.hitboxes[0];
    }

    s16 x() const { return s16(s16(u32(offset_x_) >> 8) + base_x_); }

    s16 y() const { return s16(bridge_->heights[index_] + (offset_y_ >> 8) + base_y_); }

    void update(Game& g) override {
        StageState& s = g.stage();
        CosmicBridge& b = *bridge_;
        const s16 x = this->x(), y = this->y();
        if (y - s.camera.y < -240) {
            g.scheduler().destroy(*this);
            return;
        }
        if (b.gone && (b.ticks & 1) != 0) {
            return;
        }
        Sprite& picture = b.sprite;
        if (index_ == 0 || picture.first_entry == 0xFF) {
            picture.first_entry = 0xFF;
            picture.x = s16(x - s.camera.x);
            picture.y = s16(y - s.camera.y);
            draw(g, picture);
            return;
        }
        auto& v = g.video();
        const Assets& a = g.assets();
        const auto attr = v.queued_entries[picture.first_entry].attributes();
        const u16 a0 =
            u16((attr[0] & 0xFF00) + ((y - s.camera.y - a.s16(picture.frame + 10)) & 0xFF));
        const u16 a1 =
            u16((attr[1] & 0xFE00) + ((x - s.camera.x - a.s16(picture.frame + 8)) & 0x1FF));
        gba::Sprite e;
        e.set_attributes(a0, a1, attr[2]);
        queue_sprite(v, (picture.layer & 0x7C0) >> 6, e);
    }

    Sprite sprite; // +00 only its hitbox (the bridge's) is used, to stand on
private:
    std::shared_ptr<CosmicBridge> bridge_;
    u8 index_;            // +52
    s16 base_x_, base_y_; // +30, +32
    s32 offset_x_;        // +38 8.8
    s32 offset_y_{};      // +3C
};

// 08033848, 080338F4, 08033A70: the bridge: each frame finds the plank the
// first player stands on and bends the bridge under it; a hard landing (a
// fall faster than 400h) springs the plank (twice as far in the middle),
// which swings and settles again. The player rides the plank's height.
class CosmicBridgeProcess final : public Process {
public:
    CosmicBridgeProcess(Game& g, std::shared_ptr<CosmicBridge> bridge, s16 base_x, s16 base_y)
        : Process(0x2000), game_(g), bridge_(std::move(bridge)), base_x_(base_x), base_y_(base_y) {
        CosmicBridge& b = *bridge_;
        b.tiles = g.sprite_tiles.allocate(0x10);
        b.sprite.start(b.tiles, 0x293, 0, 0, 0, 0x5C0, 0x2000);
    }

    void on_destroy() override { game_.sprite_tiles.free(bridge_->tiles); } // 08034208

    void update(Game& g) override {
        if (!started_) {
            start(g);
            return;
        }
        bend(g);
    }

private:
    // 080338F4
    void start(Game& g) {
        CosmicBridge& b = *bridge_;
        b.state = 0;
        b.kicking = false;
        b.sprung = 0;
        b.stood_on = 0;
        b.gone = false;
        remember_player(g.stage().player[0]);
        b.heights.fill(0);
        animate(g, b.sprite);
        started_ = true;
        for (u8 i = 0; i < 9; ++i) {
            planks_[i] = &g.scheduler().create<BridgePlank>(bridge_, i, base_x_, base_y_);
        }
    }

    void remember_player(const Player& p) {
        CosmicBridge& b = *bridge_;
        b.last_x = p.x;
        b.last_y = p.y;
        b.last_speed_x = p.speed_x;
        b.last_speed_y = p.speed_y;
        b.last_flags = p.flags;
    }

    static void clamp_ends(CosmicBridge& b) {
        b.heights[0] = std::min<s16>(b.heights[0], 8);
        b.heights[8] = std::min<s16>(b.heights[8], 8);
    }

    // The planks near a springing one follow it (two on each side, by
    // thirds).
    static void spring_neighbours(CosmicBridge& b, u8 k) {
        for (u8 j = 0; j < 9; ++j) {
            if (j == k) {
                continue;
            }
            const s8 dist = s8(std::abs(s32(k) - s32(j)));
            if (dist < 3) {
                b.heights[j] = s16(b.heights[k] - s16(quotient(dist * b.heights[k], 3)));
            }
        }
        clamp_ends(b);
    }

    // The others ease a step a frame toward a straight line from plank k
    // to the bridge's ends (the end planks kept within 8 after each one,
    // or only after all).
    static void ease_neighbours(CosmicBridge& b, u8 k, bool clamp_each) {
        for (u8 j = 0; j < 9; ++j) {
            if (j == k) {
                continue;
            }
            s8 dist = s8(k - j);
            u8 span = k;
            if (dist < 0) {
                dist = s8(-dist);
                span = u8(8 - k);
            }
            const s16 target =
                s16(u16(b.heights[k]) - u32(quotient(b.heights[k] * dist, s8(span + 1))));
            if (b.heights[j] < target) {
                ++b.heights[j];
            }
            if (target < b.heights[j]) {
                --b.heights[j];
            }
            if (clamp_each) {
                clamp_ends(b);
            }
        }
        clamp_ends(b);
    }

    // The springing plank k swings; true when it has settled.
    static bool swing(CosmicBridge& b, u8 k) {
        s16& h = b.heights[k];
        h = s16(h + b.velocity);
        if (b.limit <= h) {
            h = b.limit;
            b.velocity = s16(1 - b.velocity);
            b.kicking = false;
        }
        const s32 half = b.limit / 2;
        if (h + half <= 0) {
            h = s16(-half);
            return true;
        }
        return false;
    }

    // 08033A70
    void bend(Game& g) {
        StageState& s = g.stage();
        CosmicBridge& b = *bridge_;
        Player& p = s.player[0];
        b.previous = b.heights;
        ++b.ticks;
        bool on = false;
        u8 plank = 0;
        if (!b.gone) {
            for (u8 i = 0; i < 9; ++i) {
                BridgePlank& pl = *planks_[i];
                const s16 x = pl.x(), y = pl.y();
                pl.sprite.x = s16(x - s.camera.x);
                pl.sprite.y = s16(y - s.camera.y);
                if (collide_platform(g, pl.sprite, x, y, p) != 0) {
                    on = true;
                    plank = i;
                    break;
                }
            }
        } else if (!(p.flags & (player_flag::in_air | player_flag::on_object))) {
            g.scheduler().destroy(*this); // (the rest of the frame still runs)
        }
        if (!on) {
            if (b.state == 2) {
                const u8 k = b.sprung;
                if (swing(b, k)) {
                    b.velocity = 0;
                    b.state = 0;
                }
                spring_neighbours(b, k);
            } else {
                b.state = 0;
                const u8 k = b.stood_on;
                s16& h = b.heights[k];
                if (h > 0) {
                    --h;
                }
                if (h < 0) {
                    h = s16(h + 2);
                    if (h > 0) {
                        h = 0;
                    }
                }
                ease_neighbours(b, k, true);
            }
        } else {
            b.stood_on = plank;
            s16 reach = (plank == 0 || plank == 8) ? 8 : 0x10;
            if (!(b.last_flags & player_flag::in_air) || b.last_speed_y < 0x401) {
                if (b.state == 2) {
                    if (swing(b, plank)) {
                        b.state = 1;
                    }
                    spring_neighbours(b, plank);
                } else {
                    b.state = 1;
                    s16& h = b.heights[plank];
                    h = s16(h + (h < 0 ? 2 : 1));
                    if (h > reach) {
                        h = reach;
                    }
                    ease_neighbours(b, plank, false);
                }
            } else {
                // A hard landing springs it.
                b.state = 2;
                b.sprung = plank;
                b.kicking = true;
                if (reach != 8) {
                    reach = s16(reach << 1);
                }
                b.limit = reach;
                for (u8 j = 0; j < 9; ++j) {
                    if (j == plank) {
                        b.velocity = s8(u16(b.last_speed_y) >> 8);
                        b.heights[j] = s16(b.heights[j] + (b.last_speed_y >> 8));
                        if (b.heights[j] > reach) {
                            b.heights[j] = reach;
                        }
                    } else {
                        b.heights[j] = 0;
                    }
                }
            }
            p.y += 0x100 + (b.heights[plank] - b.previous[plank]) * 0x100;
        }
        remember_player(p);
        if (s.players == 2 && !b.gone) {
            for (u8 i = 0; i < 9; ++i) {
                BridgePlank& pl = *planks_[i];
                if (collide_platform(g, pl.sprite, pl.x(), pl.y(), s.player[1]) != 0) {
                    return;
                }
            }
        }
    }

    Game& game_;
    std::shared_ptr<CosmicBridge> bridge_;
    s16 base_x_, base_y_;
    std::array<BridgePlank*, 9> planks_{}; // +30
    bool started_{};                       // the update is 08033A70
};

// 08033590, 08033700: the gun's shot: from the pod toward the player (half
// the distance's direction), a pixel a frame; it hurts a player it meets
// (Eggman laughs). Ended out of view; once the boss is broken it turns to
// smoke.
class CosmicShot final : public Process {
public:
    CosmicShot(Game& g, CosmicAngelBoss& boss);

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 080341F4

    void update(Game& g) override;

private:
    Game& game_;
    CosmicAngelBoss& boss_;
    // The shot reaches the boss through its gun's task record (the record's
    // creator). Once the gun has ended, the record is freed (it then leads
    // to memory that reads as fewer than 8 hits and not the pod: the shot
    // flies on and makes nobody laugh) or handed to a new task, whose
    // creator then counts (the boss again, if it made that task).
    Sprite sprite_;         // +00
    s32 x_, y_;             // +64, +68 8.8 from the boss's place
    s16 speed_x_, speed_y_; // +6C, +6E
    s16 base_x_, base_y_;   // +70, +72
};

// 080332DC, 08033450: the pod's gun: once the fight is on, every 60 frames
// (after three shots a longer pause, by hits: 084A5DF6) it fires while the
// pod is open or closed (not opening), its muzzle flash (two halves) shown
// over the pod (080333EC).
class CosmicGun final : public Process {
public:
    CosmicGun(Game& g, CosmicAngelBoss& boss) : Process(0x2200), game_(g), boss_(boss) {
        const u32 tiles = g.sprite_tiles.allocate(6);
        flash_.start(tiles, 0x297, 0, 0, 0, 0x540, 0x2000);
        mirror_.start(tiles, 0x297, 0, 0, 0, 0x540,
                      0x2000 | sprite_flag::keep_colors | sprite_flag::keep_tiles);
    }

    void on_destroy() override { game_.sprite_tiles.free(flash_.tiles.to); } // 080341F4

    void update(Game& g) override;

private:
    Game& game_;
    CosmicAngelBoss& boss_;
    Sprite flash_;    // +00
    Sprite mirror_;   // +30
    s16 timer_ = 600; // +60
    s16 shots_{};     // +62
};

class CosmicAngelBoss final : public Process {
public:
    CosmicAngelBoss(Game& g, const ObjectSpawn& o) : Process(0x2100), game_(g) {
        base_x_ = s16(o.record[0] * 8 + o.cell_x * 0x100);
        base_y_ = s16(o.record[1] * 8 + o.cell_y * 0x100);
        hits_ = g.save().unknown_5178 != 0 ? 2 : 0;
        vehicle_tiles_ = g.sprite_tiles.allocate(0x30);
        vehicle_.start(vehicle_tiles_, 0x296, 3, base_x_, base_y_, 0x580, 0x2000);
        o.record[0] = 0xFE;
        eggman_tiles_ = g.sprite_tiles.allocate(8);
        eggman_.start(eggman_tiles_, 0x294, 3, base_x_, base_y_, 0x540, 0x2000);
        g.scheduler().create<CameraBoundsXY>(g, s16(base_y_ - 0x80), s16(base_y_ + 0x20),
                                             s16(base_x_ - 0x90), s16(base_x_ + 0xB0));
    }

    // 080341D8
    void on_destroy() override {
        game_.sprite_tiles.free(vehicle_tiles_);
        game_.sprite_tiles.free(eggman_tiles_);
    }

    void update(Game& g) override {
        switch (state_) {
        case State::arriving:
            arrive(g);
            break;
        case State::fighting:
            fight(g);
            break;
        case State::falling:
            fall(g);
            break;
        case State::landed:
            land(g);
            break;
        case State::escaping:
            escape(g);
            break;
        }
    }

    // What the gun and its shots look at.
    s8 hits() const { return hits_; }

    bool armed() const { return armed_; }

    const Sprite& vehicle() const { return vehicle_; }

    Sprite& eggman() { return eggman_; }

    s16 base_x() const { return base_x_; }

    s16 base_y() const { return base_y_; }

    s16 swing_x() const { return swing_x_; }

    s16 swing_y() const { return swing_y_; }

private:
    enum class State : u8 { arriving, fighting, falling, landed, escaping };
    static constexpr u32 open_times = 0x084A5E08;   // frames the pod stays open (4, at random)
    static constexpr u32 swing_speeds = 0x084A5DE4; // the swing's top speed after a hit, by hits

    // The swing's offset across (±30 pixels) for an angle.
    s32 swing_of(u16 angle) const { return (sine(game_.assets(), angle) * 0xF) >> 11; }

    s16 x_offset() const { return s16(u32(x_) >> 8); }

    // 08031EA0: blinking white after a hit (while more than 16 of its 32
    // frames are left); Eggman is drawn while shown.
    void show(Game& g) {
        if (flash_ != 0) {
            const u8 b = --flash_;
            if (s8(b) > 0x10 && (b & 2) == 0 && !(g.stage().player[0].flags & player_flag::dead)) {
                vehicle_.flags |= 0x100;
                flash_on(g);
            }
        }
        draw(g, vehicle_);
        if (eggman_shown_) {
            draw(g, eggman_);
        }
    }

    void place_both(const StageState& s, s16 x, s16 y) {
        vehicle_.x = s16(x - s.camera.x);
        vehicle_.y = s16(y - s.camera.y);
    }

    // 08032190: it swings in place a little; after 2 seconds the music,
    // a second later Eggman taunts, then the fight.
    void arrive(Game& g) {
        StageState& s = g.stage();
        if (timer_ == 0x78 && phase_ == 0) {
            auto bridge = std::make_shared<CosmicBridge>();
            bridge_ = bridge;
            g.scheduler().create<CosmicBridgeProcess>(g, bridge, base_x_, base_y_);
            g.scheduler().create<CosmicGun>(g, *this);
        }
        const s32 sw = swing_of(angle_);
        s32 at = s16(sw + 0x90);
        if (at < 0) {
            at += 0x1F;
        }
        const s16 sag = bridge_->heights[std::size_t(at >> 5)];
        place_both(s, s16(base_x_ + x_offset() + s16(sw)), s16(sag + base_y_ + (y_ >> 8)));
        animate(g, vehicle_);
        animate(g, eggman_);
        eggman_.x = vehicle_.x;
        eggman_.y = vehicle_.y;
        show(g);
        switch (phase_) {
        case 0:
            if (--timer_ != 0) {
                return;
            }
            s.music_queued = 0x13;
            timer_ = 0x3C;
            break;
        case 1:
            if (--timer_ != 0) {
                return;
            }
            eggman_.variant = 4;
            break;
        case 2:
            if (!(eggman_.flags & sprite_flag::ended)) {
                return;
            }
            eggman_.variant = 3;
            timer_ = 0x3C;
            break;
        case 3:
            if (--timer_ != 0) {
                return;
            }
            armed_ = true;
            phase_ = 0;
            state_ = State::fighting;
            return;
        default:
            return;
        }
        ++phase_;
    }

    // 08031D58: a hit from the air (the first hitbox), or a touch.
    void contact(Game& g, s16 x, s16 y) {
        StageState& s = g.stage();
        vehicle_.flags &= ~sprite_flag::mode;
        flash_off(g);
        if (flash_ != 0) {
            return;
        }
        const BossContact c0 = boss_contact_from_air(g, vehicle_, x, y, s.player[0]);
        const BossContact c1 = s.players == 2
                                   ? boss_contact_from_air(g, vehicle_, x, y, s.player[1])
                                   : BossContact::none;
        if (c0 == BossContact::attacked || c1 == BossContact::attacked) {
            ++hits_;
            flash_ = 0x20;
            if (vehicle_.variant == 2) {
                eggman_.variant = 2;
            }
            if (vehicle_.variant == 3) {
                eggman_.variant = 5;
            }
            eggman_.flags &= ~sprite_flag::ended;
            g.sound().start(sound_boss_hit);
        } else if (c0 == BossContact::hurt || c1 == BossContact::hurt) {
            if (vehicle_.variant == 2) {
                eggman_.variant = 1;
            }
            if (vehicle_.variant == 3) {
                eggman_.variant = 4;
            }
            eggman_.flags &= ~sprite_flag::ended;
        }
    }

    // 08031F44: Eggman's reaction over, back to his pose for the pod.
    void settle_eggman() {
        if (eggman_.variant != 0 && eggman_.variant != 3 && (eggman_.flags & sprite_flag::ended)) {
            if (vehicle_.variant == 2) {
                eggman_.variant = 0;
            }
            if (vehicle_.variant == 3) {
                eggman_.variant = 3;
            }
            eggman_.previous_variant = 0xFF;
        }
    }

    // 08032340: the pendulum. Riding the bridge's sag (between the planks
    // under it); a springing plank under it (within two of the one hit)
    // knocks it up: the pod opens for a while (084A5E08, at random), shuts
    // and swings faster after a hit (084A5DE4) for 2 seconds.
    void fight(Game& g) {
        StageState& s = g.stage();
        const Assets& a = g.assets();
        swing_speed_ = s16(swing_speed_ + swing_accel_);
        angle_ = u16((angle_ - s8(u16(swing_speed_) >> 8)) & 0x3FF);
        const s32 sw = swing_of(angle_);
        const s16 x = s16(base_x_ + x_offset() + s16(sw));
        const s32 along = s16(sw + 0x80);
        s32 q = along < 0 ? along + 0x1F : along;
        q >>= 5;
        const u8 i = u8(q);
        s32 d = s16(along - q * 0x20) * (bridge_->heights[i + 1u] - bridge_->heights[i]);
        if (d < 0) {
            d += 0x1F;
        }
        const s32 sag = bridge_->heights[i] + (d >> 5);
        swing_x_ = s16(sw);
        swing_y_ = s16(sag);
        const s16 y = s16(s16(sag) + base_y_ + (y_ >> 8));
        contact(g, x, y);
        if (hits_ > 7) {
            broken(g);
            return;
        }
        place_both(s, x, y);
        settle_eggman();
        animate(g, vehicle_);
        if (eggman_shown_) {
            animate(g, eggman_);
        }
        eggman_.x = vehicle_.x;
        eggman_.y = vehicle_.y;
        show(g);
        CosmicBridge& b = *bridge_;
        switch (phase_) {
        case 0: {
            if (swing_speed_ < 0x200) {
                swing_speed_ = 0x200;
                swing_accel_ = 0;
            }
            s32 under = sw + 0x90;
            if (under < 0) {
                under = sw + 0xAF;
            }
            const s8 plank = s8(under >> 5);
            if (u8(plank) == 0 || u8(plank) == 8 || b.sprung == 0 || b.sprung == 8) {
                return;
            }
            if (plank < s8(b.sprung - 2) || s8(b.sprung + 2) < plank || !b.kicking) {
                return;
            }
            swing_speed_ = 0x200;
            swing_accel_ = 0;
            break;
        }
        case 1: // knocked up: opening
            if (vehicle_.variant != 1) {
                if (b.velocity >= 0) {
                    return;
                }
                vehicle_.variant = 1;
                vehicle_.previous_variant = 0xFF;
                eggman_shown_ = false;
                return;
            }
            if (!(vehicle_.flags & sprite_flag::ended)) {
                return;
            }
            vehicle_.variant = 2;
            eggman_.variant = 0;
            vehicle_.previous_variant = 0xFF;
            eggman_shown_ = true;
            timer_ = a.u16(open_times + ((s.next_random() + s.ticks) & 3) * 2);
            hits_before_ = hits_;
            break;
        case 2: // open: until the time is up or a hit's blinking is half over
            if (flash_ == 0) {
                if (--timer_ != 0) {
                    return;
                }
            } else if (s8(flash_) > 0x10) {
                return;
            }
            vehicle_.variant = 0;
            vehicle_.previous_variant = 0xFF;
            eggman_shown_ = false;
            break;
        case 3: // shutting
            if (!(vehicle_.flags & sprite_flag::ended)) {
                return;
            }
            vehicle_.variant = 3;
            eggman_.variant = 3;
            vehicle_.previous_variant = 0xFF;
            eggman_shown_ = true;
            swing_accel_ = 0x40;
            top_speed_ = a.s16(swing_speeds + u32(hits_) * 2);
            timer_ = 0x78;
            if (hits_ == hits_before_) {
                phase_ = u8(phase_ + 2);
                return;
            }
            break;
        case 4: // hit: swinging faster for 2 seconds
            if (top_speed_ <= swing_speed_) {
                swing_speed_ = top_speed_;
                swing_accel_ = 0;
            }
            if (--timer_ != 0) {
                return;
            }
            swing_accel_ = -0x40;
            phase_ = 0;
            return;
        case 5:
            if (swing_speed_ > 0x1FF) {
                swing_speed_ = 0x200;
                swing_accel_ = 0;
                phase_ = 0;
            }
            return;
        default:
            return;
        }
        ++phase_;
    }

    // 08032794: the last hit: four pieces fly off, the view stops, points.
    void broken(Game& g) {
        StageState& s = g.stage();
        x_ += swing_x_ * 0x100;
        y_ += swing_y_ * 0x100;
        const s32 x = base_x_ + (x_ >> 8), y = base_y_ + (y_ >> 8);
        eggman_.variant = 2;
        fall_speed_ = -0x80;
        g.sound().start(sound_boss_broken);
        const bool flipped = vehicle_.flags & sprite_flag::flip_x;
        const auto piece = [&](s32 px, s32 py, s16 spin, bool mirrored, s16 speed) {
            Debris& d = g.scheduler().create<Debris>(g, s16(px), s16(py), 4, 0x295, 0);
            d.speed_y = -0x200;
            d.growth = 0;
            d.spin = spin;
            if (mirrored) {
                d.t.sprite.flags |= sprite_flag::flip_x;
            }
            if (flipped) {
                d.t.scale_x = -0x100;
                d.speed_x = s16(-speed);
            } else {
                d.speed_x = speed;
            }
            d.timer = 0x3C;
        };
        piece(x, y, 0x100, false, -0x100);
        piece(s16(x) + 0x10, s16(y) + 0x10, 0x200, false, -0x400);
        piece(s16(x) - 0x10, s16(y) + 0x10, 0x300, true, 0x200);
        piece(s16(x) - 0x10, s16(y) + 0x10, 0x380, true, 0x80);
        auto& c = s.camera;
        c.min_x = c.x;
        c.max_x = s16(c.x + 0xF0);
        g.scheduler().create<CameraBounds>(g, s16(c.max_y - 0xA0), s16(c.max_y + 0xA0));
        s.flags |= stage_flag::paused | stage_flag::frozen;
        const u32 before = s.score;
        s.score += 1000;
        if (divide(s32(s.score), 50000) != divide(s32(before), 50000) && g.game_mode == 0) {
            ++s.lives;
        }
        state_ = State::falling;
        fall(g);
    }

    // 08032AC8: falls, exploding, onto the ground below.
    void fall(Game& g) {
        StageState& s = g.stage();
        fall_speed_ = s16(fall_speed_ + 5);
        y_ += fall_speed_;
        const s16 y = s16(base_y_ + (y_ >> 8));
        const s16 x = s16(base_x_ + (x_ >> 8));
        if (floor_distance(g, x, y) < 0) {
            timer_ = 0x3C;
            state_ = State::landed;
            return;
        }
        vehicle_.flags &= ~sprite_flag::mode;
        flash_off(g);
        if ((++timer_ & 7) == 0) {
            create_explosion_piece(g, x, y, -0x20, 5);
        }
        place_both(s, x, y);
        eggman_.x = vehicle_.x;
        eggman_.y = vehicle_.y;
        animate(g, vehicle_);
        animate(g, eggman_);
        draw(g, vehicle_);
        draw(g, eggman_);
    }

    // 08032D14: a second on the ground; then the bridge goes, the view
    // opens downward, the escape music, and Eggman takes his capsule pod.
    void land(Game& g) {
        StageState& s = g.stage();
        if (--timer_ == 0) {
            bridge_->gone = true;
            s.player[0].flags &= ~player_flag::on_object;
            s.camera.max_y = 0x940;
            s.music_queued = 0x33;
            from_x_ = s16(base_x_ + x_offset());
            from_y_ = s16(base_y_ + s16(u32(y_) >> 8));
            x_ = y_ = 0;
            speed_x_ = 0;
            speed_y_ = -0x80;
            phase_ = 0;
            g.sprite_tiles.free(vehicle_tiles_);
            g.sprite_tiles.free(eggman_tiles_);
            vehicle_tiles_ = g.sprite_tiles.allocate(0x1E);
            vehicle_.start(vehicle_tiles_, 0x2B5, 0, vehicle_.x, vehicle_.y, 0x4C0, 0x2000);
            eggman_tiles_ = g.sprite_tiles.allocate(0xC);
            eggman_.start(eggman_tiles_, 0x2B6, 4, eggman_.x, eggman_.y, 0x480, 0x2000);
            state_ = State::escaping;
            return;
        }
        place_both(s, s16(base_x_ + (x_ >> 8)), s16(base_y_ + (y_ >> 8)));
        eggman_.x = vehicle_.x;
        eggman_.y = vehicle_.y;
        animate(g, vehicle_);
        animate(g, eggman_);
        draw(g, vehicle_);
        draw(g, eggman_);
    }

    // 08032F28: the pod rises; the player is held still (walking toward
    // it) once on the ground; Eggman turns to face the player's side,
    // then flies off to the right; the player walks on, and the results
    // begin once the pod is 360 pixels past the view.
    void escape(Game& g) {
        StageState& s = g.stage();
        x_ += speed_x_;
        y_ += speed_y_;
        const s16 x = s16(s16(u32(x_) >> 8) + from_x_);
        if (phase_ < 5) {
            place_both(s, x, s16(s16(u32(y_) >> 8) + from_y_));
            animate(g, vehicle_);
            animate(g, eggman_);
            eggman_.x = vehicle_.x;
            eggman_.y = vehicle_.y;
            draw(g, vehicle_);
            draw(g, eggman_);
        }
        Player& p = s.player[0];
        const s32 px = p.x >> 8;
        const auto turn = [&] {
            ++phase_;
            vehicle_.variant = 1;
            eggman_.variant = 9;
            vehicle_.previous_variant = 0xFF;
            eggman_.previous_variant = 0xFF;
        };
        const auto walk_off = [&] {
            if (px < x - 0x60) {
                p.held = button::right;
            }
            if (float(px) > float(s.camera.x) + 360.0f) {
                p.speed_x = p.speed_y = p.ground_speed = 0;
                p.held = 0;
            }
        };
        switch (phase_) {
        case 0:
            if (!(p.flags & (player_flag::in_air | player_flag::on_object))) {
                if (!(p.flags & player_flag::frozen)) {
                    p.speed_x = p.speed_y = p.ground_speed = 0;
                    p.flags |= player_flag::frozen;
                    p.held = px < x ? button::right : button::left;
                    s.camera.max_step_x = 0;
                    s.camera.max_x = s16(s.camera.x + 0x1E0);
                } else {
                    p.held = 0;
                }
            }
            if (y_ < -0x5A00) {
                y_ = -0x5A00;
                speed_y_ = 0;
            }
            if (speed_y_ != 0 || !(p.flags & player_flag::frozen)) {
                return;
            }
            if (px < x) {
                phase_ = u8(phase_ + 2);
                eggman_.variant = 7;
                eggman_.previous_variant = 0xFF;
                return;
            }
            turn();
            return;
        case 1:
            if (!(eggman_.flags & sprite_flag::ended)) {
                return;
            }
            ++phase_;
            vehicle_.variant = 0;
            eggman_.variant = 7;
            vehicle_.flags |= sprite_flag::flip_x;
            eggman_.flags |= sprite_flag::flip_x;
            vehicle_.previous_variant = 0xFF;
            eggman_.previous_variant = 0xFF;
            return;
        case 2:
            if (!(eggman_.flags & sprite_flag::ended)) {
                return;
            }
            if (eggman_.flags & sprite_flag::flip_x) {
                phase_ = u8(phase_ + 2);
                eggman_.variant = 6;
                eggman_.previous_variant = 0xFF;
                speed_x_ = 0x280;
                return;
            }
            turn();
            return;
        case 3:
            if (!(eggman_.flags & sprite_flag::ended)) {
                return;
            }
            ++phase_;
            vehicle_.variant = 0;
            eggman_.variant = 6;
            vehicle_.flags |= sprite_flag::flip_x;
            eggman_.flags |= sprite_flag::flip_x;
            vehicle_.previous_variant = 0xFF;
            eggman_.previous_variant = 0xFF;
            speed_x_ = 0x280;
            return;
        case 4:
            walk_off();
            if (float(x - s.camera.x) > 360.0f) {
                ++phase_;
                timer_ = 0xB4;
                start_results(g, s.rings, u32(s.clock));
            }
            return;
        case 5:
            walk_off();
            if (--timer_ == 0) {
                g.scheduler().destroy(*this);
            }
            return;
        default:
            return;
        }
    }

    Game& game_;
    State state_ = State::arriving;
    s16 base_x_, base_y_;
    u32 vehicle_tiles_{}, eggman_tiles_{}; // +10, +48
    Sprite vehicle_;                       // +0C
    Sprite eggman_;                        // +44
    std::shared_ptr<CosmicBridge> bridge_;
    s32 x_ = 0x1000, y_{};      // +78, +7C 8.8 from its place
    s32 speed_x_{}, speed_y_{}; // +80, +84 (escaping)
    s16 swing_x_{}, swing_y_{}; // +90, +92 the swing's offset this frame
    s16 from_x_{}, from_y_{};   // +94, +96 where the escape starts
    u16 timer_ = 0x78;          // +98
    s8 hits_{};                 // +9A
    s8 hits_before_{};          // +9B
    u8 flash_{};                // +9C
    s16 fall_speed_{};          // +9E
    u16 angle_{};               // +A0
    s16 swing_speed_ = 0x200;   // +A2 8.8 (its high byte turns the angle)
    s16 swing_accel_{};         // +A4
    s16 top_speed_{};           // +A6
    bool armed_{};              // +A8 the gun may fire
    u8 phase_{};                // +A9
    bool eggman_shown_ = true;  // +AA
};

CosmicShot::CosmicShot(Game& g, CosmicAngelBoss& boss) : Process(0x2300), game_(g), boss_(boss) {
    const Assets& a = g.assets();
    base_x_ = boss.base_x();
    base_y_ = boss.base_y();
    x_ = (boss.swing_x() + 0x10) * 0x100;
    y_ = s32(boss.swing_y()) << 8;
    const Player& p = g.stage().player[0];
    const s16 dy = s16((p.y >> 8) - ((y_ >> 8) + base_y_));
    const s16 dx = s16((p.x >> 8) - ((x_ >> 8) + base_x_));
    const s16 angle = octant_angle(a, s16(dy / 2), s16(dx / 2));
    // (A null vector, -1, reads the entry before the table.)
    speed_x_ = s16(a.s16(table::sine + 2 * (s32(angle) + 0x100)) >> 6);
    speed_y_ = s16(a.s16(u32(s32(table::sine) + 2 * s32(angle))) >> 6);
    sprite_.start(g.sprite_tiles.allocate(4), 0x298, 0, 0, 0, 0x3C0, 0x2000);
}

void CosmicShot::update(Game& g) {
    StageState& s = g.stage();
    x_ += speed_x_;
    y_ += speed_y_;
    const s16 x = s16((x_ >> 8) + base_x_), y = s16((y_ >> 8) + base_y_);
    sprite_.x = s16(x - s.camera.x);
    sprite_.y = s16(y - s.camera.y);
    const bool in_view =
        u16(sprite_.y + 0x20) < 0xD1 || (speed_x_ < 0 ? sprite_.x > -0x21 : sprite_.x <= 0x100);
    if (!in_view) {
        g.scheduler().destroy(*this);
        return;
    }
    const bool reaches_boss = g.scheduler().creator_in_slot(creator_slot()) == &boss_;
    if (reaches_boss && boss_.hits() >= 8) {
        create_smoke(g, s32(x) << 8, s32(y) << 8);
        g.scheduler().destroy(*this);
        return;
    }
    if (projectile_hits(g, sprite_, x, y) && reaches_boss && boss_.vehicle().animation == 0x296) {
        // Eggman laughs.
        if (boss_.vehicle().variant == 2) {
            boss_.eggman().variant = 1;
        }
        if (boss_.vehicle().variant == 3) {
            boss_.eggman().variant = 4;
        }
    }
    animate(g, sprite_);
    draw(g, sprite_);
}

void CosmicGun::update(Game& g) {
    static constexpr u32 pauses = 0x084A5DF6; // after three shots, by hits
    if (boss_.hits() >= 8) {
        g.scheduler().destroy(*this);
        return;
    }
    if (!boss_.armed()) {
        return;
    }
    const Sprite& pod = boss_.vehicle();
    if (timer_ == 0) {
        if (!(flash_.flags & sprite_flag::ended) && u8(pod.variant - 2) < 2) {
            // 080333EC: the muzzle flash over the pod, two halves.
            flash_.x = mirror_.x = pod.x;
            flash_.y = mirror_.y = pod.y;
            animate(g, flash_);
            draw(g, flash_);
            mirror_.flags |= sprite_flag::flip_x;
            animate(g, mirror_);
            draw(g, mirror_);
        } else if (shots_ == 3) {
            timer_ = g.assets().s16(pauses + u32(boss_.hits()) * 2);
            shots_ = 0;
        } else {
            timer_ = 0x3C;
        }
        return;
    }
    if (--timer_ != 0) {
        return;
    }
    flash_.flags &= ~sprite_flag::flip_y;
    mirror_.flags &= ~sprite_flag::flip_y;
    if (pod.variant != 2) {
        if (pod.variant != 3) {
            timer_ = 1;
            return;
        }
        flash_.flags |= sprite_flag::flip_y;
        mirror_.flags |= sprite_flag::flip_y;
    }
    flash_.previous_variant = mirror_.previous_variant = 0xFF;
    flash_.flags &= ~sprite_flag::ended;
    mirror_.flags &= ~sprite_flag::ended;
    ++shots_;
    g.scheduler().create<CosmicShot>(g, boss_);
}

} // namespace

void shake_view(Game& g, s32 amplitude, s32 decay, s32 step, s32 frames, u32 mode) {
    g.scheduler().create<Shake>(g, amplitude, decay, step, frames, mode);
}

void rain_debris(Game& g) {
    StageState& s = g.stage();
    const Assets& a = g.assets();
    const auto& c = s.camera;
    // Three large pieces in front, then three smaller behind (8.8 speeds:
    // the large ones up to 1, the small ones up to 2 pixels a frame).
    for (unsigned n = 0; n < 6; ++n) {
        const bool front = n < 3;
        const u32 kind = s.next_random() & 7;
        const u32 tiles = g.sprite_tiles.allocate(a.u8(piece_tiles + kind));
        Particle& p = g.scheduler().create<Particle>(g, tiles, a.u16(piece_animations + kind * 2),
                                                     a.u8(piece_variants + kind), true);
        p.x = (c.x + s32(s.next_random() & 0xFF)) * 0x100;
        p.y = s16(c.y - 0x20) * 0x100;
        p.sprite.flags = front ? 0x3000 : 0;
        p.sprite.layer = front ? 0x7C0 : 0x400;
        p.accel_y = 0x28;
        p.timer = 0x100;
        const u32 direction = s.next_random() & 0x1FF;
        const s16 down = s16(sine(a, direction) >> (front ? 6 : 5));
        p.speed_y = s16(down < 0 ? -down : down);
        p.speed_x = s16((sine(a, direction + 0x100) * (front ? -0x100 : -0x200)) >> 14);
    }
}

bool create_boss(Game& g, u32 creator, const ObjectSpawn& o) {
    switch (creator) {
    case 0x0802F138:
        if (g.game_mode >= 2) {
            o.record[0] = 0xFE;
        } else {
            g.scheduler().create<CasinoParadiseBoss>(g, o);
        }
        return true;
    case 0x0804E4C8:
        if (g.game_mode >= 2) {
            o.record[0] = 0xFE;
        } else {
            g.scheduler().create<AngelIslandBoss>(g, o);
        }
        return true;
    case 0x08031F98:
        if (g.game_mode >= 2) {
            o.record[0] = 0xFE;
        } else {
            g.scheduler().create<CosmicAngelBoss>(g, o);
        }
        return true;
    case 0x08030A30:
        if (g.game_mode >= 2) {
            o.record[0] = 0xFE;
        } else {
            g.scheduler().create<IceParadiseBoss>(g, o);
        }
        return true;
    case 0x0802D930:
        if (g.game_mode >= 2) {
            o.record[0] = 0xFE;
        } else {
            g.scheduler().create<SecretBaseBoss>(g, o);
        }
        return true;
    case 0x080259C0:
    case 0x08025F0C:
        if (g.game_mode >= 2) {
            o.record[0] = 0xFE;
            return true;
        }
        if (creator == 0x080259C0) {
            g.scheduler().create<EggmanArrival>(g, o);
        } else {
            g.scheduler().create<NeoGreenHillBoss>(g, o);
        }
        return true;
    default:
        return false;
    }
}

} // namespace sonic_advance_gba
