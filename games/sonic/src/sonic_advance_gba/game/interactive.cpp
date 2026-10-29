#include "sonic_advance_gba/game/interactive.hpp"
#include "sonic_advance_gba/game/angel_island.hpp"
#include "sonic_advance_gba/game/casino_paradise.hpp"
#include "sonic_advance_gba/game/destruction.hpp"
#include "sonic_advance_gba/game/egg_rocket.hpp"
#include "sonic_advance_gba/game/game.hpp"
#include "sonic_advance_gba/game/ice_paradise.hpp"
#include "sonic_advance_gba/game/math.hpp"
#include "sonic_advance_gba/game/object_base.hpp"
#include "sonic_advance_gba/game/object_util.hpp"
#include "sonic_advance_gba/game/player_effects.hpp"
#include "sonic_advance_gba/game/results.hpp"
#include "sonic_advance_gba/game/secret_base.hpp"
#include "sonic_advance_gba/game/solid.hpp"
#include "sonic_advance_gba/game/stage.hpp"
#include "sonic_advance_gba/game/tables.hpp"
#include "sonic_advance_gba/game/x_zone.hpp"
#include <algorithm>
#include <array>

namespace sonic_advance_gba {

namespace {

// 0801FF94 / 08020160: spikes pointing up: solid, and standing on them
// hurts (08020D14).
class Spikes final : public Interactive {
public:
    Spikes(Game& g, const ObjectSpawn& o) : Interactive(o) {
        sprite_.start(0x11B00, 0x1C6, 2, x_, y_, 0x440, 0x2000);
        animate(g, sprite_);
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        sprite_.x = s16(x_ - s.camera.x);
        sprite_.y = s16(y_ - s.camera.y);
        if (g.game_mode == 6) {
            throw Unrecovered("spikes in the chao hunt (08020160)");
        }
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if (s.flags & stage_flag::upside_down) {
                // 08020E68: with gravity reversed, meeting their underside
                // hurts (not while the movement routines are off).
                if (!(p.flags & player_flag::no_movement)) {
                    touching_[p.index & 1] = collide_solid(g, sprite_, x_, y_, p);
                    if ((touching_[p.index & 1] & solid::from_below) && hurt_player(g, p)) {
                        g.sound().start(0xAB);
                    }
                }
                continue;
            }
            // 08020D14: standing on them hurts.
            touching_[p.index & 1] = collide_solid(g, sprite_, x_, y_, p);
            if ((touching_[p.index & 1] & solid::on_top) && hurt_player(g, p)) {
                g.sound().start(0xAB);
            }
        }
        if (near_view(sprite_.x, sprite_.y)) {
            draw(g, sprite_);
        } else {
            give_back();
            g.scheduler().destroy(*this);
        }
    }

private:
    Sprite sprite_;                 // +0C
    std::array<u32, 2> touching_{}; // +3C per player: the collision's result
};

// 08023FC8, 0802414C: a platform (the act's picture, 080B8504) swinging
// to and fro along a sine (record +5: the horizontal swing, +6: the
// vertical, in 8 pixels; the larger one is used; +3/+4 negative: it starts
// at the other end), carrying who stands on it; it dips while ridden.
class SwingingPlatform final : public Interactive {
public:
    SwingingPlatform(Game& g, const ObjectSpawn& o) : Interactive(o, 0x2010), game_(g) {
        if (o.record[6] < o.record[5]) {
            speed_x_ = 4;
            phase_ = s8(o.record[3]) < 0 ? 0x80 : 0;
        } else {
            speed_y_ = 4;
            phase_ = s8(o.record[4]) < 0 ? 0x80 : 0;
        }
        const u16 animation =
            g.game_mode == 6 ? 0x1CF : g.assets().u16(0x080B8504 + u32(s32(g.stage().zone)) * 2);
        sprite_.start(g.sprite_tiles.allocate(0x12), animation, 0, x_, y_, 0x480, 0x2000);
        animate(g, sprite_);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 08024AEC

    void update(Game& g) override {
        StageState& s = g.stage();
        const Assets& a = g.assets();
        s32 move_x = 0, move_y = 0;
        const auto swing = [&](s32& offset, u16 speed, u8 range) {
            const s32 before = offset;
            offset =
                (range * 0x800 * s32(sine(a, (speed * ((s.ticks + phase_) & 0xFF)) & 0x3FF))) >> 14;
            return offset - before;
        };
        if (speed_x_ != 0) {
            move_x = swing(offset_x_, speed_x_, spawn_.record[5]);
        }
        if (speed_y_ != 0) {
            move_y = swing(offset_y_, speed_y_, spawn_.record[6]);
        }
        sprite_.x = s16(x_ - s.camera.x + s16(u32(offset_x_) >> 8));
        sprite_.y = s16(y_ - s.camera.y + s16(u32(offset_y_) >> 8));
        const bool upside_down = s.flags & stage_flag::upside_down;
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if ((p.flags & player_flag::on_object) && p.standing_on == &sprite_) {
                p.x += move_x;
                p.y += (upside_down ? -0x200 : 0x100) + move_y;
            }
            if (!(p.flags & player_flag::no_movement)) {
                collide_platform(g, sprite_, s16(x_ + (offset_x_ >> 8)), s16(y_ + (offset_y_ >> 8)),
                                 p);
            }
        }
        if (out_of_range(s, x_, y_, sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        if (!(s.player[0].flags & player_flag::no_movement)) {
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
            const s16 d = s16(sine(a, dip_) >> 12);
            sprite_.y = s16(upside_down ? sprite_.y - d : sprite_.y + d);
        }
        draw(g, sprite_);
    }

private:
    Game& game_;
    Sprite sprite_;               // +0C
    u16 phase_{};                 // +3C
    s32 offset_x_{}, offset_y_{}; // +40, +44 8.8
    u16 speed_x_{}, speed_y_{};   // +48, +4A
    u16 dip_{};                   // +4C
};

// A trigger box (record +3/+4: its corner, +5/+6: its size, in 8 pixels)
// that lives while near the view.
class TriggerBox : public Process {
public:
    explicit TriggerBox(const ObjectSpawn& o)
        : Process(0x2000), record_(o.record), x_byte_(o.record[0]) {
        x_ = o.cell_x * 0x100 + o.record[0] * 8;
        y_ = o.cell_y * 0x100 + o.record[1] * 8;
        left_ = x_ + s8(o.record[3]) * 8;
        top_ = y_ + s8(o.record[4]) * 8;
        right_ = left_ + o.record[5] * 8;
        bottom_ = top_ + o.record[6] * 8;
        o.record[0] = 0xFE;
    }

protected:
    bool far(const StageState& s) const {
        const auto& c = s.camera;
        return x_ > c.x + 0x170 || x_ < c.x - 0x80 || y_ > c.y + 0x120 || y_ < c.y - 0x80;
    }

    bool inside(const Player& p) const {
        const s32 px = p.x >> 8, py = p.y >> 8;
        return px >= left_ && px < right_ && py >= top_ && py < bottom_;
    }

    void end(Game& g, Player& p) {
        p.flags &= ~u32(0x8000);
        record_[0] = x_byte_;
        g.scheduler().destroy(*this);
    }

    static void give_buttons_back(const StageState& s, Player& p) {
        if (p.flags & player_flag::frozen) {
            p.flags &= ~player_flag::frozen;
            p.held = u16(s.jump_buttons | s.other_buttons | p.held);
        }
    }

    u8* record_;
    u8 x_byte_;
    s32 x_, y_, left_, right_, top_, bottom_;
};

// 0804D82C, 0804D6FC: the start of a tunnel: a player inside rolls on,
// the buttons frozen, unable to stop rolling (flag 200).
class RollStart final : public TriggerBox {
public:
    explicit RollStart(const ObjectSpawn& o) : TriggerBox(o) {}

    void update(Game& g) override {
        StageState& s = g.stage();
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if (far(s)) {
                end(g, p);
                return;
            }
            if (!inside(p)) {
                continue;
            }
            if (p.flags & player_flag::dead) {
                return;
            }
            end_player_moves(g, p);
            p.flags |= player_flag::frozen | player_flag::no_jump_cap | player_flag::rolling;
            p.held = 0;
            p.pressed = 0;
            p.state = state_jumping;
            set_player_radius(g, p, 6, 9);
            p.power |= 0x80;
            s.player[0].flags &= ~player_flag::no_movement;
            if (p.character != 3) {
                g.sound().start_unless_playing(0x6D);
            }
        }
    }

private:
    static constexpr u8 state_jumping = 5;
};

// 0804D9C0, 0804D8B4: the end of a tunnel: the player may stop rolling and
// gets the buttons back.
class RollEnd final : public TriggerBox {
public:
    explicit RollEnd(const ObjectSpawn& o) : TriggerBox(o) {}

    void update(Game& g) override {
        StageState& s = g.stage();
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if (far(s)) {
                end(g, p);
                return;
            }
            if (!inside(p)) {
                continue;
            }
            if (p.flags & player_flag::dead) {
                return;
            }
            p.flags &= ~player_flag::no_jump_cap;
            give_buttons_back(s, p);
            p.power &= 0x7F;
        }
    }
};

// 0804E0DC/0804DF1C, 0804E164/0804DFF4, 0804E2D0/0804E1EC,
// 0804E440/0804E358: boxes that set the speed of a player on the ground
// inside (kinds 36-39: on at 900h, back at 740h, turned round on a
// ceiling, back at 480h).
class SpeedTrigger final : public TriggerBox {
public:
    enum class Kind { forward, back, turn, back_slow };

    SpeedTrigger(const ObjectSpawn& o, Kind kind) : TriggerBox(o), kind_(kind) {}

    void update(Game& g) override {
        StageState& s = g.stage();
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if (far(s)) {
                end(g, p);
                return;
            }
            if (!inside(p)) {
                continue;
            }
            if (kind_ != Kind::forward && (p.flags & player_flag::dead)) {
                return;
            }
            if (kind_ == Kind::turn) {
                p.angle = 0x80;
            }
            if (p.flags & player_flag::in_air) {
                continue;
            }
            switch (kind_) {
            case Kind::forward:
                p.ground_speed = 0x900;
                break;
            case Kind::back:
                p.ground_speed = -0x740;
                break;
            case Kind::turn:
                p.ground_speed = s16(-p.ground_speed);
                break;
            case Kind::back_slow:
                p.ground_speed = -0x480;
                break;
            }
        }
    }

private:
    Kind kind_;
};

// 0804DDA8, 0804DE84: an animated decoration (animation 1D9) shown while
// near the view.
class Decoration final : public Process {
public:
    Decoration(Game& g, const ObjectSpawn& o)
        : Process(0x2000), game_(g), record_(o.record), x_byte_(o.record[0]) {
        x_ = o.cell_x * 0x100 + o.record[0] * 8;
        y_ = o.cell_y * 0x100 + o.record[1] * 8;
        o.record[0] = 0xFE;
        sprite_.start(g.sprite_tiles.allocate(0x31), 0x1D9, 0, 0, 0, 0x3C0, 0x1000);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 0804DF04

    void update(Game& g) override {
        const auto& c = g.stage().camera;
        if (x_ > c.x + 0x170 || x_ < c.x - 0x80 || y_ > c.y + 0x120 || y_ < c.y - 0x80) {
            record_[0] = x_byte_;
            g.scheduler().destroy(*this);
            return;
        }
        sprite_.x = s16(x_ - c.x);
        sprite_.y = s16(y_ - c.y);
        animate(g, sprite_);
        draw(g, sprite_);
    }

private:
    Game& game_;
    u8* record_;
    u8 x_byte_;     // +3D
    s32 x_, y_;     // +04, +08
    Sprite sprite_; // +0C
};

// 0804DA48, 0804DB44: a curved launch (the box's floor rises along a
// quarter sine over its width): a player on the curve is carried up it,
// frozen and rolled up, a little faster each frame (to 800h), and thrown
// off its end; a whirl (animation 1DA) shows on the player.
class LaunchCurve final : public TriggerBox {
public:
    LaunchCurve(Game& g, const ObjectSpawn& o)
        : TriggerBox(o), game_(g), width_(o.record[5] * 8), height_(o.record[6] * 8) {
        sprite_.start(g.sprite_tiles.allocate(0x12), 0x1DA, 0, 0, 0, 0x3C0, 0x1000);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 0804DD90

    void update(Game& g) override {
        StageState& s = g.stage();
        const Assets& a = g.assets();
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if (far(s)) {
                end(g, p);
                return;
            }
            const s32 px = p.x >> 8;
            const s32 feet = (p.y >> 8) + p.radius_y;
            bool on = px >= left_ && px < right_ && feet >= top_ && feet <= bottom_;
            s32 along = 0;
            if (on) {
                if (p.flags & player_flag::dead) {
                    return;
                }
                along = std::clamp(quotient((px - left_) * 0x200, width_), 0, 0x1FF);
                const s32 surface =
                    (bottom_ * 0x100 - (sine(a, unsigned(along)) >> 6) * height_) >> 8;
                on = feet >= surface && feet <= surface + 0x20;
            }
            if (!on) {
                give_buttons_back(s, p);
                p.flags &= ~u32(0x8000);
                continue;
            }
            const s32 cos = sine(a, unsigned(along) + 0x100) >> 6;
            if (!(p.flags & 0x8000)) {
                g.sound().start_unless_playing(0xB1);
                p.flags |= 0x8000;
            }
            const s32 v =
                std::clamp(((p.flags & player_flag::in_air) ? p.speed_x : p.ground_speed) + 0x40,
                           -0x800, 0x800);
            p.ground_speed = s16(v);
            p.speed_x = s16(v);
            p.speed_y = s16(std::clamp(((v * -height_ * cos) >> 16) - 0x2A, -0x800, 0x800));
            end_player_moves(g, p);
            p.state = 0x0F;
            p.flags &= ~player_flag::rolling;
            set_player_radius(g, p, 6, 9);
            p.flags |= player_flag::frozen;
            p.pressed = 0;
            p.held = 0;
            sprite_.x = s16(px - s.camera.x);
            sprite_.y = s16(feet - p.radius_x - s.camera.y);
            animate(g, sprite_);
            draw(g, sprite_);
        }
    }

private:
    Game& game_;
    s32 width_, height_; // +1C, +20
    Sprite sprite_;      // +24
};

// 0801FE54: once the player runs on the ground faster than 23Fh, the
// goal pose (state 1Ch) begins.
class GoalBrake final : public Process {
public:
    GoalBrake() : Process(0x2000) {}

    void update(Game& g) override {
        Player& p = g.stage().player[0];
        if (!(p.flags & player_flag::in_air) && p.ground_speed > 0x23F) {
            land_player(g, p);
            p.state = 0x1C;
            p.flags |= player_flag::keep_state;
            g.scheduler().destroy(*this);
        }
    }
};

// 0801F150: the goal line (shown when record +3 is set; in one-player
// games only when record +4 is set). Passing it (0801F268) stops the clock
// and plays the goal sound; then the player runs on alone (0801F650) and
// after two seconds, once the player has stopped in the goal pose, the
// results begin.
class Goal final : public Interactive {
public:
    Goal(Game& g, const ObjectSpawn& o) : Interactive(o), game_(g) {
        sprite_.start(g.sprite_tiles.allocate(0x24), 0x1C5, 0, x_, y_, 0x480, 0x2000);
        animate(g, sprite_);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 08009670

    void update(Game& g) override {
        StageState& s = g.stage();
        if (g.game_mode >= 2) {
            throw Unrecovered("the goal in multiplayer (0801F268)");
        }
        sprite_.x = s16(x_ - s.camera.x);
        sprite_.y = s16(y_ - s.camera.y);
        if (!passed_) {
            const Player& p = s.player[0];
            if ((p.x >> 8) <= x_) {
                if (!near_view(sprite_.x, sprite_.y)) {
                    give_back();
                    g.scheduler().destroy(*this);
                } else if (spawn_.record[3] != 0) {
                    draw(g, sprite_);
                }
                return;
            }
            // Act 7's goal counts only for a player less than 50h above
            // it; act 10's only once the player's body box holds its point.
            bool through = true;
            if (s.zone == 7) {
                through = (p.y >> 8) > y_ - 0x50;
            } else if (s.zone == 10) {
                const Hitbox& h = p.body->sprite.hitboxes[0];
                const s32 left = (p.x >> 8) + h.left, top = (p.y >> 8) + h.top;
                through = left <= x_ && x_ <= left + (h.right - h.left) && top <= y_ &&
                          y_ <= top + (h.bottom - h.top);
            }
            if (!through) {
                if (!near_view(sprite_.x, sprite_.y)) {
                    give_back();
                    g.scheduler().destroy(*this);
                } else if (spawn_.record[3] != 0) {
                    draw(g, sprite_);
                }
                return;
            }
            sprite_.variant = 1;
            passed_ = true;
            if (s.player_blinks[0] != 0xFF) {
                // Another goal was passed first: this one only shows.
                shown_only_ = true;
            } else {
                s.flags |= stage_flag::paused | stage_flag::frozen;
                if (s.zone != 10) {
                    g.sound().start(0x87);
                }
                clock_ = 0;
                braking_ = false;
            }
        }
        if (shown_only_) {
            // 0801F9F8
            if (spawn_.record[3] != 0) {
                animate(g, sprite_);
                draw(g, sprite_);
            }
            return;
        }
        run_on(g, s);
    }

private:
    // 0801F650
    void run_on(Game& g, StageState& s) {
        Player& p = s.player[0];
        const bool rocket = s.zone == 10;
        if (rocket) {
            // Act 10: the player stays where the goal was met; the rocket's
            // sound stops.
            p.flags |= player_flag::no_movement;
            g.sound().stop(0x78);
        } else {
            p.flags |= player_flag::frozen;
            p.held = button::right;
            if (s.camera.shift_y > -0x38) {
                --s.camera.shift_y;
            }
            if (p.ground_speed > 0x480) {
                p.ground_speed = 0x480;
            }
        }
        if (!rocket && clock_ > 0x1E && !braking_) {
            braking_ = true;
            g.scheduler().create<GoalBrake>();
        }
        s.player_blinks[0] = 0;
        if (clock_++ < 0x79 || (p.state != 0x1C && !rocket)) {
            if (spawn_.record[3] != 0) {
                animate(g, sprite_);
                draw(g, sprite_);
            }
            return;
        }
        g.scheduler().destroy(*this);
        start_results(g, s.rings, u32(s.clock));
    }

    Game& game_;
    Sprite sprite_;     // +0C
    u16 clock_{};       // +3C
    bool braking_{};    // +3E
    bool passed_{};     // the update is 0801F650 (else 0801F268)
    bool shown_only_{}; // the update is 0801F9F8
};

// 0804D5CC / 0804D664: a hump past the end of a run-up (record +3/+4: the
// run-up box's corner, +5/+6: its size, in 8 pixels; kind 28 to the right,
// 29 to the left): running through the box faster than 240h (on the
// ground, no move) marks the player (flag 8000); past the box's end the
// player is carried over a hump as high as the speed allows (0804D46C).
class Hump final : public Process {
public:
    Hump(const ObjectSpawn& o, bool leftward)
        : Process(0x2000), record_(o.record), x_byte_(o.record[0]), leftward_(leftward) {
        x_ = o.cell_x * 0x100 + o.record[0] * 8;
        y_ = o.cell_y * 0x100 + o.record[1] * 8;
        left_ = x_ + s8(o.record[3]) * 8;
        top_ = y_ + s8(o.record[4]) * 8;
        right_ = left_ + o.record[5] * 8;
        bottom_ = top_ + o.record[6] * 8;
        o.record[0] = 0xFE;
    }

    // 0804D144 / 0804D2D4
    void update(Game& g) override {
        StageState& s = g.stage();
        const auto& c = s.camera;
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            const bool far = leftward_ ? (x_ > c.x + 0x410 || x_ < c.x - 0x80)
                                       : (x_ > c.x + 0x170 || x_ < c.x - 0x320);
            if (far || y_ > c.y + 0x120 || y_ < c.y - 0x80) {
                p.flags &= ~u32(0x8000);
                record_[0] = x_byte_;
                g.scheduler().destroy(*this);
                return;
            }
            const s32 px = p.x >> 8, py = p.y >> 8;
            if (px >= left_ && px < right_ && py >= top_ && py < bottom_) {
                const bool fast = leftward_ ? p.speed_x < -0x240 : p.speed_x > 0x240;
                if (fast && !(p.flags & player_flag::in_air) && p.move == 0) {
                    p.flags |= 0x8000;
                    saved_y_ = p.radius_y * 0x100 + p.y;
                } else {
                    p.flags &= ~u32(0x8000);
                }
                continue;
            }
            if (!(p.flags & 0x8000) || (leftward_ ? px >= left_ : px < right_)) {
                continue;
            }
            if (p.flags & player_flag::dead) {
                return;
            }
            if (u16(p.speed_x + 0x23F) <= 0x47E || (s.jump_buttons & p.pressed)) {
                p.flags &= ~u32(0x8000);
                if (u8(p.state - 0x18) < 3) {
                    p.state = 4;
                }
            } else if (leftward_ ? p.speed_x < -0x23F : p.speed_x > 0x23F) {
                carry(g, p, leftward_ ? u32(px - left_ + 0x2A0) : u32(px - right_));
            }
        }
    }

private:
    // 0804D46C: over the hump, `along` pixels past the run-up (2A0h long).
    void carry(Game& g, Player& p, u32 along) {
        const Assets& a = g.assets();
        s32 height = 0;
        if (along < 0x2A0) {
            const s32 v = p.speed_x < 0 ? -p.speed_x : p.speed_x;
            const s32 k = s32(u32(v - 0x240) * 0x4000) >> 16;
            const u32 i = k < 0 ? 0 : (k > 0x100 ? 0x100 : (u32(v - 0x240) & 0x3FFFF) >> 2);
            height = std::min<s32>(sine(a, i) >> 6, 0x100);
            const s32 t = s32(along) < 0x150 ? s32(along) << 15 : (s32(along) - 0x2A0) * 0x8000;
            const u32 q = u32(quotient(t, 0x150));
            height = (height * (((sine(a, ((q & 0xFFFF) >> 6) + 0x100) * -3) >> 2) + 0x3000)) >> 8;
        }
        if (!(p.flags & player_flag::rolling) && p.move == 0 &&
            (p.state == 4 || u8(p.state - 0x18) < 3)) {
            p.state = height < 0x800 ? 4 : height < 0x1800 ? 0x18 : height < 0x3000 ? 0x19 : 0x1A;
        }
        p.flags &= ~player_flag::in_air;
        p.speed_y = 0;
        const s32 d = std::clamp(saved_y_ - height - p.radius_y * 0x100 - p.y, -0x600, 0x600);
        p.y += d;
    }

    u8* record_;
    u8 x_byte_; // +21
    bool leftward_;
    s32 x_, y_;        // +04, +08
    s32 left_, right_; // +0C, +10
    s32 top_, bottom_; // +14, +18
    s32 saved_y_{};    // +1C the feet's height when the run-up was entered
};

// 08076438, 08076518: a splash (animation 1D3) for 52 frames. Its tiles
// are freed with it (08076634).
class Splash final : public Process {
public:
    Splash(Game& g, s16 x, s16 y) : Process(0x3000), game_(g), x_(x), y_(y) {
        sprite_.start(g.sprite_tiles.allocate(0x10), 0x1D3, 0, x, y, 0, 0);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); }

    void update(Game& g) override {
        const auto& c = g.stage().camera;
        sprite_.x = s16(x_ - c.x);
        sprite_.y = s16(y_ - c.y);
        if (u16(sprite_.x + 0x80) > 0x1F0 || sprite_.y + 0x80 < 0 || sprite_.y > 0x120 ||
            --timer_ == 0) {
            g.scheduler().destroy(*this);
            return;
        }
        animate(g, sprite_);
        draw(g, sprite_);
    }

private:
    Game& game_;
    s32 x_, y_;       // +4C, +50
    u8 timer_ = 0x34; // +3C
    Sprite sprite_;   // +0C
};

// 080765A4, 0807622C: a splash zone (record +5/+6: its size, in 8
// pixels): a player entering it splashes (sound B2h).
class SplashZone final : public Interactive {
public:
    explicit SplashZone(const ObjectSpawn& o) : Interactive(o) {}

    void update(Game& g) override {
        StageState& s = g.stage();
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            const s32 px = p.x >> 8, py = p.y >> 8;
            const u8 bit = u8(1 << n);
            if (px < x_ || px > x_ + spawn_.record[5] * 8 || py < y_ ||
                py > y_ + spawn_.record[6] * 8) {
                inside_ &= u8(~bit);
                continue;
            }
            if (p.flags & player_flag::dead) {
                continue;
            }
            if (!(inside_ & bit)) {
                const u32 first = s.next_random();
                s.next_random();
                g.scheduler().create<Splash>(g, s16(px + ((first & 0x1F00) >> 8) - 0x10),
                                             s16(py + ((s.random & 0xF00) >> 8) - 8));
                g.sound().start(0xB2);
            }
            inside_ |= bit;
        }
        if (u32(x_ - s.camera.x + 0x80) > 0x1F0 || y_ - s.camera.y + 0x80 < 0 ||
            y_ - s.camera.y > 0x120) {
            give_back();
            g.scheduler().destroy(*this);
        }
    }

private:
    u8 inside_{}; // +3C a bit per player
};

// 08020928, 08020A1C (kind 5, from the ground) and 08020B1C, 08020C14
// (kind 6, from the ceiling; drawn flipped): spikes that come out for half
// of every 128 frames (coming and going over 4 frames); out, they hurt
// like other spikes, and coming out into a player they push it aside and
// hurt it. 08020EFC handles spikes pointing up, 080211D8 spikes pointing
// down (reversed gravity swaps the two).
class PoppingSpikes final : public Interactive {
public:
    PoppingSpikes(Game& g, const ObjectSpawn& o, bool hanging)
        : Interactive(o), game_(g), hanging_(hanging) {
        sprite_.start(g.sprite_tiles.allocate(0x10), 0xFFFF, 0xFF, x_, y_, 0x440,
                      hanging ? 0x2800 : 0x2000);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 080214A8

    void update(Game& g) override {
        StageState& s = g.stage();
        sprite_.x = s16(x_ - s.camera.x);
        sprite_.y = s16(y_ - s.camera.y);
        if (!near_view(sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        const bool down = hanging_ != bool(s.flags & stage_flag::upside_down);
        bool shared = false, shown = false;
        for (unsigned n = 0; n < s.players; ++n) {
            shown = step(g, s.player[n], down, shared);
        }
        if (shown) {
            draw(g, sprite_);
        }
    }

private:
    // 08020EFC / 080211D8 (`down`): true when they show.
    bool step(Game& g, Player& p, bool down, bool& shared) {
        const u32 t = g.stage().ticks & 0x7F;
        u32& touching = touching_[p.index & 1];
        const auto drop = [&] {
            if ((p.flags & player_flag::on_object) && p.standing_on == &sprite_) {
                p.flags = (p.flags & ~player_flag::on_object) | player_flag::in_air;
            }
        };
        const auto unpush = [&] {
            if (touching & solid::pushing) {
                p.flags &= ~player_flag::pushing;
                touching = 0;
            }
        };
        const auto pose = [&](u8 variant) {
            sprite_.animation = 0x1C6;
            sprite_.variant = variant;
            animate(g, sprite_);
        };
        if (t < 0x3C) {
            drop();
            unpush();
            return false;
        }
        if (t < 0x40 || (t >= 0x7C && t < 0x7E)) {
            drop();
            unpush();
            pose(t < 0x3E ? 0 : 1);
            return true;
        }
        if (t >= 0x7E) {
            drop();
            unpush();
            pose(0);
            return true;
        }
        // Out.
        // The points face the player's top (down) or feet.
        const u32 points = down ? solid::from_below : solid::on_top;
        bool hurt = false;
        if (sprite_.variant == 2 && (p.index == 0 || !shared)) {
            touching = collide_solid(g, sprite_, x_, y_, p);
            hurt = touching & points;
        } else {
            if (p.index == 0) {
                shared = true;
            }
            pose(2);
            const Hitbox body{0, s8(-p.radius_x), s8(-p.radius_y), p.radius_x, p.radius_y};
            const bool inside =
                sprite_.hitboxes[0].index != 0xFFFFFFFF && !(p.flags & player_flag::dead) &&
                hitboxes_meet(x_, y_, sprite_.hitboxes[0], p.x >> 8, p.y >> 8, body);
            if (!inside) {
                // 080211D8 does not collide here.
                hurt = !down && (collide_solid(g, sprite_, x_, y_, p) & points);
            } else {
                // Placed against the spikes' tips (the edge facing the
                // player under normal gravity).
                const bool below = down != bool(g.stage().flags & stage_flag::upside_down);
                p.y = (below ? sprite_.hitboxes[0].bottom + y_ + (p.radius_y - 1)
                             : sprite_.hitboxes[0].top + y_ - (p.radius_y - 1))
                      << 8;
                hurt = true;
            }
        }
        if (hurt && hurt_player(g, p)) {
            g.sound().start(0xAB);
        }
        return true;
    }

    Game& game_;
    bool hanging_;
    Sprite sprite_;                 // +0C
    std::array<u32, 2> touching_{}; // +3C per player
};

// 080204F8, 08020614: spikes pointing sideways (kind 3: right, kind 4:
// left): solid; touching their points hurts; the other side holds the
// player at its edge.
class SideSpikes final : public Interactive {
public:
    SideSpikes(Game& g, const ObjectSpawn& o) : Interactive(o), game_(g), kind_(o.record[2]) {
        if (g.game_mode == 6) {
            throw Unrecovered("spikes in the chao hunt (08020614)");
        }
        sprite_.start(g.sprite_tiles.allocate(0x10), 0x1C6, 3, x_, y_, 0x440,
                      kind_ == 3 ? 0x2400 : 0x2000);
        animate(g, sprite_);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 080214A8

    void update(Game& g) override {
        StageState& s = g.stage();
        sprite_.x = s16(x_ - s.camera.x);
        sprite_.y = s16(y_ - s.camera.y);
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            const u32 r = collide_solid(g, sprite_, x_, y_, p);
            if (r & (solid::from_below | solid::on_top)) {
                continue;
            }
            const Hitbox& h = sprite_.hitboxes[0];
            if (r & solid::from_left) {
                if (kind_ == 3) {
                    hurt(g, p);
                } else {
                    p.x = (x_ + h.right + p.radius_x + 6) << 8;
                }
            } else if (r & solid::from_right) {
                if (kind_ == 4) {
                    hurt(g, p);
                } else {
                    p.x = (x_ + h.left - (p.radius_x + 5) - 1) << 8;
                }
            }
        }
        if (near_view(sprite_.x, sprite_.y)) {
            draw(g, sprite_);
        } else {
            give_back();
            g.scheduler().destroy(*this);
        }
    }

private:
    static void hurt(Game& g, Player& p) {
        if (hurt_player(g, p)) {
            g.sound().start(0xAB);
        }
    }

    Game& game_;
    u8 kind_;
    Sprite sprite_; // +0C
};

} // namespace

// 080234A0, 08023668 (and Ice Paradise's 08091780, 08091948): a broken
// block's four pieces fly up and apart, turning and growing (drawn through
// the scene transform, from where the block was on screen), for about a
// second. Their tiles are freed with them (080238B0, 08091F08).
class BlockDebris final : public Process {
public:
    BlockDebris(Game& g, s16 x, s16 y, const DebrisLook& look)
        : Process(0x2000), game_(g), look_(look) {
        pieces_[0].sprite.start(g.sprite_tiles.allocate(look.small_tiles), look.small_animation, 0,
                                0, 0, 0x200, look.first_set);
        pieces_[0].transform = {0, look.start_scale, look.start_scale, x, y};
        animate(g, pieces_[0].sprite);
        copy_sprite(pieces_[1].sprite, pieces_[0].sprite);
        pieces_[1].sprite.flags = look.first_set + 1u;
        pieces_[1].transform = pieces_[0].transform;
        pieces_[1].transform.y = s16(y - 0x10);
        pieces_[2].sprite.start(g.sprite_tiles.allocate(look.large_tiles), look.large_animation, 0,
                                0, 0, 0x200, look.first_set + 2u);
        pieces_[2].transform = pieces_[1].transform;
        pieces_[2].transform.y = y;
        animate(g, pieces_[2].sprite);
        copy_sprite(pieces_[3].sprite, pieces_[2].sprite);
        pieces_[3].sprite.flags = look.first_set + 3u;
        pieces_[3].transform = pieces_[2].transform;
        pieces_[3].transform.y = s16(y - 0x10);
        if (look.sound != 0) {
            g.sound().start(look.sound);
        }
    }

    void on_destroy() override {
        game_.sprite_tiles.free(pieces_[0].sprite.tiles.to);
        game_.sprite_tiles.free(pieces_[2].sprite.tiles.to);
    }

    void update(Game& g) override {
        if (clock_++ >= look_.frames) {
            g.scheduler().destroy(*this);
            return;
        }
        StageState& s = g.stage();
        speed_y_ = s16(speed_y_ + 0x28);
        const s16 fall = s8(u16(speed_y_) >> 8);
        s16 scale = 0;
        // Across by -2, 1, 2, -1 per frame; turning by -2A, 2A, E, -E.
        static constexpr s16 spread[4] = {-2, 1, 2, -1};
        static constexpr s16 turn[4] = {-0x2A, 0x2A, 0xE, -0xE};
        for (std::size_t i = 0; i < 4; ++i) {
            Piece& p = pieces_[i];
            p.transform.y = s16(p.transform.y + fall);
            if (i == 0) {
                scale = s16(p.transform.scale_x + look_.growth);
                if (scale > 0x200) {
                    scale = 0x200;
                }
            }
            p.transform.scale_x = p.transform.scale_y = scale;
            p.transform.angle = u16(p.transform.angle + turn[i]);
            p.sprite.flags = (p.sprite.flags & ~u32(0x1F)) | s.next_rotation_set++;
            const s16 x = s16(p.transform.x - s.camera.x + clock_ * spread[i]);
            const s16 y = s16(p.transform.y - s.camera.y);
            transform_in_scene(g, p.sprite, p.transform.angle, scale, scale, x, y);
            draw(g, p.sprite);
        }
    }

private:
    struct Piece {
        Sprite sprite;

        struct {
            u16 angle;
            s16 scale_x, scale_y, x, y;
        } transform{};
    };

    // The original copies the whole sprite record.
    static void copy_sprite(Sprite& to, const Sprite& from) {
        to.tiles.to = from.tiles.to;
        to.tiles.from = from.tiles.from;
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
        to.hitboxes = from.hitboxes;
    }

    Game& game_;
    DebrisLook look_;
    std::array<Piece, 4> pieces_; // sprites +00, +30, +60, +90; transforms +C0, +CC, +D8, +E4
    u16 clock_{};                 // +F0
    s16 speed_y_ = -0x200;        // +F2
};

void create_block_debris(Game& g, s16 x, s16 y, const DebrisLook& look) {
    g.scheduler().create<BlockDebris>(g, x, y, look);
}

namespace {

// 08023150 / 0802323C: a block that is solid until a player breaks it
// (rolling down onto it or attacking it).
class BreakableBlock final : public Interactive {
public:
    BreakableBlock(Game& g, const ObjectSpawn& o) : Interactive(o), game_(g) {
        sprite_.start(g.sprite_tiles.allocate(0x18), 0x1CA, 0, x_, y_, 0x480, 0x2000);
        animate(g, sprite_);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); }

    void update(Game& g) override {
        StageState& s = g.stage();
        sprite_.x = s16(x_ - s.camera.x);
        sprite_.y = s16(y_ - s.camera.y);
        if (g.game_mode >= 2) {
            throw Unrecovered("breakable blocks in multiplayer (0802323C)");
        }
        if (s.player[0].character == 3) {
            throw Unrecovered("Amy's hammer against a block (0800A768)");
        }
        if (breaks_block(g, sprite_, x_, y_, s.player[0]) ||
            (s.players == 2 && breaks_block(g, sprite_, x_, y_, s.player[1]))) {
            create_explosion(g, x_, y_);
            for (unsigned n = 0; n < s.players; ++n) {
                Player& p = s.player[n];
                if ((p.flags & player_flag::on_object) && p.standing_on == &sprite_) {
                    p.flags = (p.flags & ~player_flag::on_object) | player_flag::in_air;
                }
            }
            g.sound().start(0x8A);
            // The pieces fly from the block's place on screen. (It stays
            // broken: its layout record is not given back.)
            g.scene.focus_x = sprite_.x;
            g.scene.focus_y = sprite_.y;
            create_block_debris(g, s16(x_ - g.scene.focus_x), s16(y_ - g.scene.focus_y - 0x10),
                                {0x1CB, 1, 0x1CC, 4, 0x3D, 8, 0x100, 0x70, 0});
            g.scheduler().destroy(*this);
            return;
        }
        collide_solid(g, sprite_, x_, y_, s.player[0]);
        if (s.players == 2) {
            collide_solid(g, sprite_, x_, y_, s.player[1]);
        }
        if (near_view(sprite_.x, sprite_.y)) {
            draw(g, sprite_);
            return;
        }
        give_back();
        g.scheduler().destroy(*this);
    }

private:
    Game& game_;
    Sprite sprite_; // +0C
};

// 080223D0 / 080224D0: a spring on its side (kind 14 points left, 15
// right; picture variant 6), and 08022178 / 08022290: a diagonal spring
// (kind 12 points up left, 13 up right; variant 4): touching its face
// launches the player (08022DE4, 08022A84); it is solid otherwise.
class SideSpring final : public Interactive {
public:
    SideSpring(Game& g, const ObjectSpawn& o, u8 variant, u8 left_kind)
        : Interactive(o), game_(g), points_left_(o.record[2] == left_kind), idle_variant_(variant) {
        sprite_.start(g.sprite_tiles.allocate(0x10), 0x1C7, variant, x_, y_, 0x480,
                      points_left_ ? 0x2400 : 0x2000);
        sprite_.hitboxes[1].index = 0xFFFFFFFF;
        animate(g, sprite_);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 0802313C

    void update(Game& g) override {
        StageState& s = g.stage();
        const bool bouncing = bouncing_;
        for (unsigned n = 0; n < s.players; ++n) {
            if (touch(g, s.player[n])) {
                bouncing_ = true;
            }
        }
        if (!near_view(sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        if (bouncing && animate(g, sprite_) == 0) {
            // 08022564 / 08022324: the bounce has played.
            sprite_.variant = idle_variant_;
            animate(g, sprite_);
            bouncing_ = false;
        }
        draw(g, sprite_);
    }

private:
    // 08022DE4: true if the player is launched.
    bool touch(Game& g, Player& p) {
        const StageState& s = g.stage();
        sprite_.x = s16(x_ - s.camera.x);
        sprite_.y = s16(y_ - s.camera.y);
        if (p.flags & player_flag::dead) {
            return false;
        }
        const bool hammer = hammer_hits(sprite_, x_, y_, p);
        bool on_face = hammer;
        const Hitbox& face = sprite_.hitboxes[1];
        if (!on_face) {
            const s32 bl = -(p.radius_x + 6), bt = -(p.radius_y + 1), br = p.radius_x + 6,
                      bb = p.radius_y + 1;
            const s32 px = (p.x >> 8) + bl, py = (p.y >> 8) + bt;
            on_face = spans_meet(px, px + (br - bl), x_ + face.left,
                                 x_ + face.left + (face.right - face.left)) &&
                      spans_meet(py, py + (bb - bt), y_ + face.top,
                                 y_ + face.top + (face.bottom - face.top));
        }
        if (!on_face) {
            collide_solid(g, sprite_, x_, y_, p);
            return false;
        }
        p.flags &= ~(player_flag::rolling | player_flag::jump_cancel);
        const bool from_left = points_left_ && (hammer || x_ - ((p.x >> 8) + 0x14) > 0);
        if (!from_left && !hammer && x_ - (p.x >> 8) + 0x14 >= 0) {
            collide_solid(g, sprite_, x_, y_, p);
            return false;
        }
        // Launched: the record's +3 picks one of four strengths (080B84FC)
        // or, 4 and up, +4 x 16 (8.8); a hammer blow adds half.
        if (g.game_mode == 3 || g.game_mode == 5) {
            p.hurt_timer = 2;
        }
        p.flags = (p.flags & ~(player_flag::jumped | player_flag::on_object)) | player_flag::in_air;
        p.special = 0;
        const u8* r = spawn_.record;
        s16 strength;
        if (s8(r[3]) < 4) {
            strength = g.assets().s16(0x080B84FC + (r[3] & 3) * 2u);
        } else {
            strength = s16(r[4] << 4);
        }
        p.speed_y = s16(-strength);
        p.speed_x = from_left ? s16(-strength) : strength;
        if (hammer) {
            p.speed_x = s16(p.speed_x + (p.speed_x >> 1));
            p.speed_y = s16(p.speed_y + (p.speed_y >> 1));
        }
        if (s.flags & stage_flag::upside_down) {
            p.speed_y = s16(-p.speed_y);
        }
        end_player_moves(g, p);
        p.state = (r[3] & 1) ? 0x15 : 0x11;
        p.body->sprite.previous_variant = 0xFF;
        sprite_.variant = u8(idle_variant_ + 1);
        set_player_radius(g, p, 6, 0xE);
        p.radius_x = 6;
        p.radius_y = 0xE;
        g.sound().start(0xAA);
        return true;
    }

    Game& game_;
    Sprite sprite_; // +0C (with a second hitbox: the spring's face)
    bool points_left_;
    u8 idle_variant_;
    bool bouncing_{}; // the update is 08022564 / 08022324
};

} // namespace

// 080227D4: with gravity reversed: met from below (the player's
// "down") it launches the player that way by the record's strength;
// otherwise standing or pushing on it only lands the player.
bool spring_throws_down(Game& g, Sprite& sprite, s16 x, s16 y, const u8* record, Player& p) {
    const StageState& s = g.stage();
    sprite.x = s16(x - s.camera.x);
    sprite.y = s16(y - s.camera.y);
    const u32 r = collide_solid(g, sprite, x, y, p);
    if (!(r & solid::from_below)) {
        if (r & (solid::on_top | solid::pushing)) {
            land_player(g, p);
        }
        return false;
    }
    if (g.game_mode == 3 || g.game_mode == 5) {
        p.hurt_timer = 2;
    }
    p.flags = (p.flags & ~player_flag::jumped) | player_flag::in_air;
    p.special = 0;
    p.speed_y =
        s8(record[3]) < 4 ? g.assets().s16(0x080B84FC + (record[3] & 3) * 2u) : s16(record[4] << 4);
    sprite.variant = 1;
    sprite.previous_variant = 0xFF;
    g.sound().start(0xAA);
    return true;
}

// 08022610: true if the player is launched.
bool spring_throws_up(Game& g, Sprite& sprite, s16 x, s16 y, const u8* record, Player& p) {
    const StageState& s = g.stage();
    sprite.x = s16(x - s.camera.x);
    sprite.y = s16(y - s.camera.y);
    if (g.game_mode == 3 || g.game_mode == 5) {
        throw Unrecovered("springs in multiplayer (08022610)");
    }
    if (p.flags & player_flag::no_movement) {
        return false;
    }
    const u32 r = collide_solid(g, sprite, x, y, p);
    bool hammer = false;
    if (!(r & solid::on_top) && !(hammer = hammer_hits(sprite, x, y, p))) {
        return false;
    }
    // Launched upwards: by the record's strength (+3 below 4: from the
    // table 080B84FC; else +4 times 16), half as much again with Amy's
    // hammer; odd strengths use the other animation.
    land_player(g, p);
    p.flags = (p.flags & 0xFFFFFEF7) | player_flag::in_air;
    p.special = 0;
    const s16 strength =
        s8(record[3]) < 4 ? g.assets().s16(0x080B84FC + (record[3] & 3) * 2u) : s16(record[4] << 4);
    p.speed_y = s16(-strength);
    if (hammer) {
        p.speed_y = s16((p.speed_y >> 1) + p.speed_y);
    }
    end_player_moves(g, p);
    p.state = (record[3] & 1) ? 0x15 : 0x11;
    p.body->sprite.previous_variant = 0xFF;
    sprite.variant = 1;
    sprite.previous_variant = 0xFF;
    set_player_radius(g, p, 6, 0xE);
    g.sound().start(0xAA);
    return true;
}

namespace {

// 08021970 / 08021AB4: a spring pointing up: standing on it launches the
// player (08022610). Unless the record's +3 bit 1 is set it takes its own
// colours (sprite colours 80-8F, then shown with palette 1).
class UpSpring final : public Interactive {
public:
    UpSpring(Game& g, const ObjectSpawn& o) : Interactive(o), game_(g) {
        u32 flags = 0x2000;
        u8 palette = 0;
        if (!(o.record[3] & 2)) {
            const Assets& a = g.assets();
            const u32 colors = a.u32(table::animations + 0x0C) + 0x480;
            auto& v = g.video();
            for (unsigned i = 0; i < 16; ++i) {
                v.sprite_colors[0x80 + i] = a.u16(colors + 2 * i);
            }
            v.sprite_colors_changed = true;
            flags = 0x42000;
            palette = 1;
        }
        sprite_.start(g.sprite_tiles.allocate(0x10), 0x1C7, 0, x_, y_, 0x480, flags);
        sprite_.palette = palette;
        animate(g, sprite_);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 0802313C

    void update(Game& g) override {
        StageState& s = g.stage();
        for (unsigned n = 0; n < s.players; ++n) {
            const bool launched =
                (s.flags & stage_flag::upside_down)
                    ? spring_throws_down(g, sprite_, x_, y_, spawn_.record, s.player[n])
                    : spring_throws_up(g, sprite_, x_, y_, spawn_.record, s.player[n]);
            if (launched) {
                bouncing_ = true;
            }
        }
        if (near_view(sprite_.x, sprite_.y)) {
            // 08021B90: the spring's bounce plays once (from the frame
            // after the launch).
            if (bouncing_now_ && animate(g, sprite_) == 0) {
                sprite_.variant = 0;
                animate(g, sprite_);
                bouncing_ = false;
            }
            bouncing_now_ = bouncing_;
            draw(g, sprite_);
        } else {
            give_back();
            g.scheduler().destroy(*this);
        }
    }

private:
    Game& game_;
    Sprite sprite_;             // +0C
    bool bouncing_ = false;     // the update is 08021B90 (else 08021AB4)
    bool bouncing_now_ = false; // ... as of this frame's start
};

// 0807782C: a catapult (facing right unless mirrored): it catches a
// player touching it (08077EEC), holds them at its tip while it bends down
// and back (08077A24), then throws them up at 780h and swings back
// (08077BA4).
class Catapult final : public Interactive {
public:
    Catapult(Game& g, const ObjectSpawn& o)
        : Interactive(o), game_(g), animation_(animation_for(g)) {
        sprite_.start(g.sprite_tiles.allocate(0xC), animation_, 0, x_, y_, 0x480, 0x2400);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 08078040

    void update(Game& g) override {
        StageState& s = g.stage();
        sprite_.x = s16(x_ - s.camera.x);
        sprite_.y = s16(y_ - s.camera.y);
        const bool player_dead = s.player[0].flags & player_flag::dead;
        if (state_ == State::waiting && !player_dead && catch_player(g)) {
            state_ = State::holding;
        } else if (!near_view(sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        } else if (state_ == State::holding) {
            hold(g, player_dead);
        } else if (state_ == State::swinging) {
            if (animate(g, sprite_) == 0) {
                step_ = 0;
                sprite_.previous_variant = 0xFF;
                sprite_.animation = animation_;
                sprite_.variant = 0;
                state_ = State::waiting;
            }
            draw(g, sprite_);
            return;
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
    static u16 animation_for(Game& g) {
        const s8 zone = g.stage().zone;
        return ((zone >> 1) & 0xFF) == 0 || zone == 0xE ? 0x1D4 : 0x1D6;
    }

    // 08077EEC: the first player touching the catapult's hitbox (with the
    // player's own size), unless Tails or Knuckles in the middle of an air
    // move.
    bool catch_player(Game& g) {
        StageState& s = g.stage();
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            const Hitbox body{0, s8(-p.radius_x), s8(-p.radius_y), p.radius_x, p.radius_y};
            if (sprite_.hitboxes[0].index == 0xFFFFFFFF || (p.flags & player_flag::dead) ||
                !hitboxes_meet(x_, y_, sprite_.hitboxes[0], p.x >> 8, p.y >> 8, body)) {
                continue;
            }
            if (u8(p.character - 1) <= 1 && p.special != 0) {
                continue;
            }
            end_player_moves(g, p);
            g.sound().start(0xAF);
            p.flags |= player_flag::no_movement;
            s.player[0].power |= 0x80;
            p.y = (y_ + 0x10) << 8;
            if (!(sprite_.flags & sprite_flag::flip_x)) {
                p.x = (x_ + 8) << 8;
                p.flags |= player_flag::facing_left;
            } else {
                p.x = (x_ - 8) << 8;
                p.flags &= ~player_flag::facing_left;
            }
            sprite_.previous_variant = 0xFF;
            sprite_.animation = animation_;
            sprite_.variant = 1;
            held_ = p.index;
            held_state_ = p.state;
            p.state = 0x1F;
            return true;
        }
        return false;
    }

    // 08077A24: down a pixel a frame for 7 frames, then up (by the table
    // 086C8C68) and the throw.
    void hold(Game& g, bool player_dead) {
        StageState& s = g.stage();
        Player& p = s.player[held_];
        if (player_dead) {
            p.flags &= ~player_flag::no_movement;
            s.player[0].power &= 0x7F;
            return;
        }
        const s32 d = g.assets().s16(0x086C8C68 + step_ * 2u);
        p.y += step_ < 7 ? d * 0x100 : -d * 0x100;
        if (++step_ > 0xD) {
            p.flags = (p.flags & 0xFFFFFEEB) | player_flag::in_air;
            set_player_radius(g, p, 6, 0xE);
            p.state = 0x15;
            p.speed_y = -0x780;
            p.flags &= ~player_flag::no_movement;
            s.player[0].power &= 0x7F;
            state_ = State::swinging;
        }
    }

    enum class State { waiting, holding, swinging } state_ = State::waiting;
    Game& game_;
    Sprite sprite_; // +0C
    u16 animation_;
    u8 step_{};                        // +3D
    [[maybe_unused]] u8 held_state_{}; // +3E
    u8 held_{};                        // +3F the player held
};

// 08024D5C: a platform (the act's picture, 080B8528) that crumbles: a
// player landing on it (08024E70) starts half a second of waiting
// (08024FD8), then its 32 tiles fall one after another, each at 42t^2/256
// pixels (08025160; once the first has fallen a pixel the platform no
// longer holds anyone, 080253D0). It comes back (its record given back)
// only after it has fallen apart or left the view.
class CrumblingPlatform final : public Interactive {
public:
    CrumblingPlatform(Game& g, const ObjectSpawn& o) : Interactive(o), game_(g) {
        const u16 animation = g.assets().u16(0x080B8528 + u32(s32(g.stage().zone)) * 2);
        sprite_.start(g.sprite_tiles.allocate(0x20), animation, 0, s16(0xFE * 8 + o.cell_x * 0x100),
                      y_, 0x480, o.record[3] != 0 ? 0x2400 : 0x2000);
        animate(g, sprite_);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 080255AC

    void update(Game& g) override {
        StageState& s = g.stage();
        sprite_.x = s16(x_ - s.camera.x);
        sprite_.y = s16(y_ - s.camera.y);
        switch (state_) {
        case State::whole:
            if ((collide_platform(g, sprite_, x_, y_, s.player[0]) & solid::on_top) ||
                (s.players == 2 &&
                 (collide_platform(g, sprite_, x_, y_, s.player[1]) & solid::on_top))) {
                state_ = State::shaking;
                clock_ = 0;
            }
            break;
        case State::shaking:
            collide_platform(g, sprite_, x_, y_, s.player[0]);
            collide_platform(g, sprite_, x_, y_, s.player[1]);
            if (clock_++ > 0x1E) {
                clock_ = 0;
                state_ = State::crumbling;
                g.sound().start(0xAD);
            }
            break;
        default:
            crumble(g);
            return;
        }
        if (out_of_range(s, x_, y_, sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        draw(g, sprite_);
    }

private:
    void crumble(Game& g) {
        StageState& s = g.stage();
        ++clock_;
        const bool holding = state_ == State::crumbling;
        if (holding) {
            collide_platform(g, sprite_, x_, y_, s.player[0]);
            collide_platform(g, sprite_, x_, y_, s.player[1]);
        }
        if ((x_ > s.camera.x + 0x170 || x_ < s.camera.x - 0x80) && u16(sprite_.x + 0x80) > 0x1F0) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        const Assets& a = g.assets();
        auto& v = g.video();
        const u32 entries = a.u32(a.u32(table::animations + 8) + 4u * sprite_.animation);
        const u16 first_a2 = a.u16(entries + a.u8(sprite_.frame + 1) * 6u + 4);
        const s16 bottom = s16(s.camera.y - y_ + 0xA0);
        unsigned k = 0;
        for (unsigned row = 0; row < 4; ++row) {
            for (unsigned column = 0; column < 8; ++column, ++k) {
                const s32 t = s16(k + 0xFFE1 + clock_);
                s16 fall = 0;
                if (!holding || t >= 1) {
                    if (holding && k == 0 && t == 1) {
                        // The first tile goes: whoever stood on it falls.
                        for (unsigned n = 0; n < s.players; ++n) {
                            Player& p = s.player[n];
                            if ((p.flags & player_flag::on_object) && p.standing_on == &sprite_) {
                                p.flags = (p.flags & ~player_flag::on_object) | player_flag::in_air;
                            }
                        }
                        state_ = State::fallen;
                    }
                    const s32 d = t * 0x2A * t;
                    fall = s16(d >> 8);
                    if (bottom < s16((d * 0x100) >> 16)) {
                        if (k == 0) {
                            g.scheduler().destroy(*this);
                            if (!holding) {
                                give_back();
                            }
                        }
                        return;
                    }
                }
                const bool mirrored = sprite_.flags & sprite_flag::flip_x;
                const u16 a0 = u16((fall + row * 8 + sprite_.y) & 0xFF);
                const u16 a1 = mirrored ? u16(((sprite_.x - column * 8 - 8) & 0x1FF) | 0x1000)
                                        : u16((column * 8 + sprite_.x) & 0x1FF);
                const u16 a2 =
                    u16(((first_a2 + sprite_.palette) & 0xF000) | ((sprite_.flags & 0x3000) >> 2) |
                        u16(((sprite_.tiles.to - 0x10000) >> 5) + k));
                gba::Sprite e;
                e.set_attributes(a0, a1, a2);
                if (!queue_sprite(v, (sprite_.layer & 0x7C0) >> 6, e)) {
                    return;
                }
            }
        }
    }

    enum class State { whole, shaking, crumbling, fallen } state_ = State::whole;
    Game& game_;
    Sprite sprite_; // +0C
    u16 clock_{};   // +3C
};

// 08027940, 08027A54: a wall (the act's picture, 080B854C; 4 by 8 tiles)
// that a rolling or attacking player breaks (sound ADh): its tiles fly
// away from the blow at half the player's speed, rising and falling at
// t(42t - 512)/256 pixels, the column nearest the blow first and lower
// rows before higher ones (08027D58). It comes back once its last tile
// has fallen out of view (or the wall left it).
class BreakableWall final : public Interactive {
public:
    BreakableWall(Game& g, const ObjectSpawn& o) : Interactive(o), game_(g) {
        const u16 animation = g.assets().u16(0x080B854C + u32(s32(g.stage().zone)) * 2);
        sprite_.start(g.sprite_tiles.allocate(0x20), animation, 0, s16(0xFE * 8 + o.cell_x * 0x100),
                      y_, 0x3C0, o.record[3] != 0 ? 0x2400 : 0x2000);
        animate(g, sprite_);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 08027FF0

    void update(Game& g) override {
        if (breaking_) {
            fly_apart(g);
            return;
        }
        StageState& s = g.stage();
        if (g.game_mode >= 2) {
            throw Unrecovered("a breakable wall in a linked game (08027A54)");
        }
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if (!(collide_wall(g, sprite_, x_, y_, p, true) & solid::struck)) {
                continue;
            }
            g.sound().start(0xAD);
            breaking_ = true;
            clock_ = 0;
            push_ = s16(p.speed_x >> 1);
            if (push_ == 0) {
                push_ = x_ < (p.x >> 8) ? -0x100 : 0x100;
            }
            const auto drop = [&](Player& q) {
                if ((q.flags & player_flag::on_object) && q.standing_on == &sprite_) {
                    q.flags = (q.flags & ~player_flag::on_object) | player_flag::in_air;
                }
            };
            drop(p);
            if (s.players == 2) {
                drop(s.player[1]);
            }
            break;
        }
        sprite_.x = s16(x_ - s.camera.x);
        sprite_.y = s16(y_ - s.camera.y);
        if (out_of_range(s, x_, y_, sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        draw(g, sprite_);
    }

private:
    void fly_apart(Game& g) {
        StageState& s = g.stage();
        sprite_.x = s16(x_ - s.camera.x);
        sprite_.y = s16(y_ - s.camera.y);
        ++clock_;
        if ((x_ > s.camera.x + 0x170 || x_ < s.camera.x - 0x80) && u16(sprite_.x + 0x80) > 0x1F0) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        const Assets& a = g.assets();
        auto& v = g.video();
        const u32 entries = a.u32(a.u32(table::animations + 8) + 4u * sprite_.animation);
        const u16 first_a2 = a.u16(entries + a.u8(sprite_.frame + 1) * 6u + 4);
        const bool mirrored = sprite_.flags & sprite_flag::flip_x;
        // The tile whose fall out of view ends the wall.
        const unsigned last = (push_ < 0) != mirrored ? 3 : 0;
        const s16 bottom = s16(s.camera.y - y_ + 0xA0);
        for (unsigned row = 0; row < 8; ++row) {
            for (unsigned column = 0; column < 4; ++column) {
                const unsigned c = mirrored ? 3 - column : column;      // the picture's column
                const unsigned order = push_ < 0 ? 3 - column : column; // from the blow
                const unsigned tile = row * 4 + c;
                const s32 t = s16(clock_ - order * 10 - (7 - row) * 2);
                s16 dx = 0, dy = 0;
                if (t >= 1) {
                    dy = s16((t * (t * 0x2A - 0x200)) >> 8);
                    dx = s16((t * push_) >> 8);
                    if (dy > bottom) {
                        if (tile == last) {
                            give_back();
                            g.scheduler().destroy(*this);
                            return;
                        }
                        continue;
                    }
                }
                const u16 a0 = u16((dy + row * 8 + sprite_.y) & 0xFF);
                const u16 a1 = mirrored ? u16(((sprite_.x - c * 8 + dx - 8) & 0x1FF) | 0x1000)
                                        : u16((dx + c * 8 + sprite_.x) & 0x1FF);
                const u16 a2 =
                    u16(((first_a2 + sprite_.palette) & 0xF000) | ((sprite_.flags & 0x3000) >> 2) |
                        u16(((sprite_.tiles.to - 0x10000) >> 5) + tile));
                gba::Sprite e;
                e.set_attributes(a0, a1, a2);
                if (!queue_sprite(v, (sprite_.layer & 0x7C0) >> 6, e)) {
                    return;
                }
            }
        }
    }

    Game& game_;
    Sprite sprite_; // +0C
    bool breaking_{};
    u16 clock_{}; // +3C
    s16 push_{};  // +3E sideways speed of the tiles
};

// 08074DC8: a springy pad (the act's picture, 086C8C20): landing on it
// throws the player up at 500h, facing the way left or right is held
// (080750D8); then it wobbles for half a second (08074FAC).
class SpringyPad final : public Interactive {
public:
    SpringyPad(Game& g, const ObjectSpawn& o) : Interactive(o), game_(g) {
        const u16 animation = g.assets().u16(0x086C8C20 + u32(s32(g.stage().zone)) * 2);
        sprite_.start(g.sprite_tiles.allocate(0xC), animation, 0, x_, y_, 0x480, 0x2000);
        animate(g, sprite_);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 08075310

    void update(Game& g) override {
        StageState& s = g.stage();
        sprite_.x = s16(x_ - s.camera.x);
        sprite_.y = s16(y_ - s.camera.y);
        if (wobble_left_ != 0) {
            sprite_.y = s16(sprite_.y + divide(sine(g.assets(), wobble_ & 0x3FF), 0x400));
        }
        const bool wobbling = wobble_left_ != 0;
        if (launch(g)) {
            wobble_left_ = 0x1E;
            wobble_ = 0;
            sprite_.variant = 0;
            g.sound().start(0xAA);
        }
        if (!near_view(sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        if (wobbling) {
            wobble_ += 0x5A;
            --wobble_left_;
            animate(g, sprite_);
        }
        draw(g, sprite_);
    }

private:
    // 080750D8
    bool launch(Game& g) {
        StageState& s = g.stage();
        bool launched = false;
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if ((p.flags & player_flag::dead) ||
                !(collide_solid(g, sprite_, x_, y_, p) & solid::on_top)) {
                continue;
            }
            if (g.game_mode == 3 || g.game_mode == 5) {
                throw Unrecovered("springy pads in multiplayer (080750D8)");
            }
            end_player_moves(g, p);
            p.flags = (p.flags & 0xFFFFFEE3) | player_flag::in_air;
            if (s.character == 3) {
                p.flags |= 0x2000000;
            }
            if (p.held & button::left) {
                p.flags |= player_flag::facing_left;
            } else if (p.held & button::right) {
                p.flags &= ~player_flag::facing_left;
            }
            p.speed_y = -0x500;
            p.state = 0x17;
            set_player_radius(g, p, 6, 0xE);
            launched = true;
        }
        return launched;
    }

    Game& game_;
    Sprite sprite_;    // +0C
    u32 wobble_{};     // +3C its phase
    u8 wobble_left_{}; // +40 frames (0: still, the update is 08074EE0)
};

// 080757F8, 08075910: a waterfall (record +4: its top, +6: its height,
// both in 8 pixels below its place; +5: its width): a player inside is
// pushed down (10h a frame; not Knuckles while climbing) and splashed (the
// act's animation, 086C8C44, drawn on the player).
class Waterfall final : public Interactive {
public:
    Waterfall(Game& g, const ObjectSpawn& o) : Interactive(o), game_(g) {
        const u16 animation = g.assets().u16(0x086C8C44 + u32(s32(g.stage().zone)) * 2);
        sprite_.start(g.sprite_tiles.allocate(0x12), animation, 0, 0, 0, 0x140, 0x2000);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 08076214

    void update(Game& g) override {
        StageState& s = g.stage();
        const s16 sx = s16(x_ - s.camera.x);
        const s16 sy = s16(y_ - s.camera.y);
        if (u32(s32(sx) + 0x80) > 0x1F0 || sy + 0x80 < 0 || sy > 0x120) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        const u8* record = spawn_.record;
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if (p.flags & player_flag::dead) {
                continue;
            }
            const s32 px = p.x >> 8, py = p.y >> 8;
            const s32 top = y_ + s8(record[4]) * 8;
            if (px < x_ || px > x_ + record[5] * 8 || py < top || py > top + record[6] * 8) {
                continue;
            }
            // 080549C8: Knuckles climbing (states 46h-4Bh) is not pushed.
            const bool climbing = s.character == 2 && u8(p.state - 0x46) < 6;
            if (!climbing) {
                p.speed_y = s16(p.speed_y + 0x10);
            }
            sprite_.x = s16(px - s.camera.x);
            sprite_.y = s16(py - s.camera.y + 10);
            animate(g, sprite_);
            draw(g, sprite_);
        }
    }

private:
    Game& game_;
    Sprite sprite_; // +04
};

// 08078054, 08078184: a current (record +5: width, +6: height, in 8
// pixels; +3: to the left; +4: its priority after 1000h): a player on the
// ground inside is carried along at 400h, the buttons frozen, with the
// act's effect (086C8C88) drawn on the player; leaving it, the player gets
// the buttons back. (It ends only when every player is outside it while it
// is out of view; with one player its mask never clears, so it stays.)
class Current final : public Interactive {
public:
    Current(Game& g, const ObjectSpawn& o)
        : Interactive(o, u16(s8(o.record[4]) + 0x1000)), game_(g) {
        const u16 animation = g.assets().u16(0x086C8C88 + u32(s32(g.stage().zone)) * 2);
        sprite_.start(g.sprite_tiles.allocate(0x12), animation, 0, x_, y_, 0xC0,
                      o.record[3] != 0 ? 0x2400 : 0x2000);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 080786B8

    void update(Game& g) override {
        StageState& s = g.stage();
        const u8* record = spawn_.record;
        unsigned outside = 3;
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if (p.flags & player_flag::dead) {
                continue;
            }
            const s32 px = p.x >> 8, py = p.y >> 8;
            u8& carried = carried_[n];
            if (px >= x_ && px <= x_ + record[5] * 8 && py >= y_ && py <= y_ + record[6] * 8) {
                outside &= ~(1u << n);
                if (carried == 0 && probe_short(g.assets(), s.map, py, px, p.layer, 8, nullptr,
                                                Sensor::floor) >= 4) {
                    continue;
                }
                if (p.flags & player_flag::on_object) {
                    continue;
                }
                if (p.flags & player_flag::in_air) {
                    if (carried != 0 && --carried == 0) {
                        release(s, p);
                    }
                    continue;
                }
                if (carried == 0 && (s.zone >> 1) == 4) {
                    g.sound().start(0xC9);
                }
                carried = 1;
                end_player_moves(g, p);
                p.flags = (p.flags & 0xFFFFFEE3) | player_flag::frozen;
                p.held = 0;
                p.pressed = 0;
                if (record[3] == 0) {
                    p.flags &= ~player_flag::facing_left;
                    p.ground_speed = 0x400;
                    p.speed_x = 0x400;
                } else {
                    p.flags |= player_flag::facing_left;
                    p.ground_speed = -0x400;
                    p.speed_x = -0x400;
                }
                p.state = 0x0F;
                p.speed_y = 0;
                set_player_radius(g, p, 6, 0xE);
                sprite_.x = s16((p.x >> 8) - s.camera.x);
                sprite_.y = s16((p.y >> 8) - s.camera.y);
                animate(g, sprite_);
                draw(g, sprite_);
                continue;
            }
            if (carried == 1) {
                g.sound().fade_out(2, 4);
                carried = 0;
                release(s, p);
            } else if (carried != 0) {
                --carried;
            }
        }
        const s32 sx = x_ - s.camera.x, sy = s16(y_ - s.camera.y);
        if (outside == 0 && (u32(s16(sx) + 0x80) > 0x1F0 || sy + 0x80 < 0 || sy > 0x120)) {
            for (unsigned n = 0; n < s.players; ++n) {
                if (carried_[n] != 0) {
                    g.sound().stop(0xC9);
                    carried_[n] = 0;
                    release(s, s.player[n]);
                }
            }
            give_back();
            g.scheduler().destroy(*this);
        }
    }

private:
    static void release(StageState& s, Player& p) {
        p.flags &= ~player_flag::frozen;
        p.held = u16(s.jump_buttons | s.other_buttons | p.held);
    }

    Game& game_;
    Sprite sprite_;               // +0C
    std::array<u8, 2> carried_{}; // +3D per player
};

// 08074A04: a bounce pad: landing on it throws the player back up at a
// quarter more than the landing speed (at least 3C0h, at most AFEh); it
// wobbles while bouncing (08074BD4).
class BouncePad final : public Interactive {
public:
    BouncePad(Game& g, const ObjectSpawn& o) : Interactive(o), game_(g) {
        const s8 zone = g.stage().zone;
        const bool own = (zone >> 1) == 1 || zone == 0xF || (zone >> 1) == 5 || zone == 0x11;
        sprite_.start(g.sprite_tiles.allocate(own ? 0x1E : 0x18), own ? 0x1F9 : 0x1CD, 0, x_, y_,
                      0x480, 0x2000);
        animate(g, sprite_);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 08074DB4

    void update(Game& g) override {
        StageState& s = g.stage();
        sprite_.x = s16(x_ - s.camera.x);
        sprite_.y = s16(y_ - s.camera.y);
        if (bounce(g)) {
            g.sound().start(0xB0);
            sprite_.variant = 1;
            if (bouncing_) {
                sprite_.previous_variant = 0xFF;
            }
            bouncing_next_ = true;
        }
        if (!near_view(sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        if (bouncing_ && animate(g, sprite_) == 0) {
            sprite_.variant = 0;
            animate(g, sprite_);
            bouncing_next_ = false;
        }
        bouncing_ = bouncing_next_;
        draw(g, sprite_);
    }

private:
    // 08074CB8
    bool bounce(Game& g) {
        StageState& s = g.stage();
        bool bounced = false;
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            const s32 v = p.speed_y;
            if (!(collide_solid(g, sprite_, x_, y_, p) & solid::on_top)) {
                continue;
            }
            if (g.game_mode == 3 || g.game_mode == 5) {
                throw Unrecovered("bounce pads in multiplayer (08074CB8)");
            }
            end_player_moves(g, p);
            p.flags = (p.flags & 0xFFFFFEE3) | player_flag::in_air;
            p.radius_x = 6;
            p.radius_y = 0xE;
            if (v == 0) {
                p.y -= 0x800;
            }
            s32 up = (v >> 5) - (v + (v >> 2));
            if (s16(up) < -0xAFE) {
                up = -0xAFE;
            }
            p.speed_y = s16(up);
            if (p.speed_y > -0x3C0) {
                p.speed_y = -0x3C0;
            }
            p.state = 0x15;
            bounced = true;
        }
        return bounced;
    }

    Game& game_;
    Sprite sprite_;         // +0C
    bool bouncing_ = false; // the update is 08074BD4 (else 08074B18)
    bool bouncing_next_ = false;
};

// 08024418 / 08024544: a platform (the act's picture, 080B8504) that dips
// a little while a player stands on it. Landed on, it waits 30 frames
// (0802470C), then falls (080248FC: at 2Ah a frame more), carrying its
// riders for 32 frames before they drop off; it ends out of view.
class DippingPlatform final : public Interactive {
public:
    DippingPlatform(Game& g, const ObjectSpawn& o) : Interactive(o), game_(g) {
        if (g.game_mode == 6) {
            throw Unrecovered("platforms in the chao hunt (08024418)");
        }
        const u16 animation = g.assets().u16(0x080B8504 + u32(s32(g.stage().zone)) * 2);
        sprite_.start(g.sprite_tiles.allocate(0x12), animation, 0, x_, y_, 0x480, 0x2000);
        animate(g, sprite_);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 08024AEC

    void update(Game& g) override {
        StageState& s = g.stage();
        if (g.game_mode >= 2) {
            throw Unrecovered("platforms in multiplayer (08024544)");
        }
        if (state_ == State::falling) {
            fall(g, s);
            return;
        }
        sprite_.x = s16(x_ - s.camera.x);
        sprite_.y = s16(y_ - s.camera.y);
        // (Player 1's record is tested even when there is no partner.)
        if (state_ == State::resting) {
            if ((collide_platform(g, sprite_, x_, y_, s.player[0]) & solid::on_top) ||
                (collide_platform(g, sprite_, x_, y_, s.player[1]) & solid::on_top)) {
                timer_ = 0x1E;
                state_ = State::landed;
            }
        } else {
            // 0802470C
            collide_platform(g, sprite_, x_, y_, s.player[0]);
            collide_platform(g, sprite_, x_, y_, s.player[1]);
            if (--timer_ == 0) {
                state_ = State::falling;
            }
        }
        if (out_of_range(s, x_, y_, sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        dip_and_draw(g, s);
    }

private:
    enum class State { resting, landed, falling };

    // 080248FC
    void fall(Game& g, StageState& s) {
        speed_y_ = s16(speed_y_ + 0x2A);
        fall_ += speed_y_;
        const s16 y = s16(y_ + (fall_ >> 8));
        sprite_.x = s16(x_ - s.camera.x);
        sprite_.y = s16(y - s.camera.y);
        ++timer_;
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if ((p.flags & player_flag::on_object) && p.standing_on == &sprite_) {
                // (The first player's state is looked at for either rider.)
                if (s.player[0].state == 0xE) {
                    end_player_moves(g, s.player[0]);
                    s.player[0].state = 4;
                }
                if (timer_ < 0x21) {
                    p.y += 0x100 + speed_y_;
                } else {
                    p.flags = (p.flags & ~player_flag::on_object) | player_flag::in_air;
                    p.speed_y = speed_y_;
                }
            }
            if (timer_ < 0x20) {
                collide_platform(g, sprite_, x_, y, p);
            }
        }
        if (!near_view(sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        dip_and_draw(g, s);
    }

    void dip_and_draw(Game& g, StageState& s) {
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

    Game& game_;
    Sprite sprite_; // +0C
    u16 timer_{};   // +3C
    s32 fall_{};    // +44 8.8
    s16 speed_y_{}; // +4A (the speed across, +48, stays 0)
    u16 dip_{};     // +4C a quarter turn of the sine at most: 4 pixels
    State state_ = State::resting;
};

// 080214BC: a checkpoint (record +3: its number). Passing it (its first
// hitbox around the player's centre) makes it the restart point: it spins
// (08021808), then stays lit (080218D8).
class Checkpoint final : public Interactive {
public:
    Checkpoint(Game& g, const ObjectSpawn& o) : Interactive(o), game_(g) {
        state_ = g.stage().checkpoint < s8(o.record[3]) ? State::waiting : State::passed;
        sprite_.start(g.sprite_tiles.allocate(0x12), 0x1C4, state_ == State::waiting ? 0 : 2, x_,
                      y_, 0x480, 0x2000);
        animate(g, sprite_);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 08009670

    void update(Game& g) override {
        switch (state_) {
        case State::waiting:
            wait(g);
            break;
        case State::spinning:
            spin(g);
            break;
        case State::passed:
            shine(g);
            break;
        }
    }

private:
    enum class State { waiting, spinning, passed } state_;

    bool visible(const StageState& s) {
        sprite_.x = s16(x_ - s.camera.x);
        sprite_.y = s16(y_ - s.camera.y);
        return u32((sprite_.x + 0x80) << 16) < 0x1F00001 && sprite_.y + 0x80 >= 0 &&
               (sprite_.y << 16) < 0x1200001;
    }

    void leave(Game& g) {
        give_back();
        g.scheduler().destroy(*this);
    }

    // 080215D8
    void wait(Game& g) {
        StageState& s = g.stage();
        sprite_.x = s16(x_ - s.camera.x);
        sprite_.y = s16(y_ - s.camera.y);
        Player& p = s.player[0];
        const s32 px = p.x >> 8, py = p.y >> 8;
        const Hitbox& h = sprite_.hitboxes[0];
        bool inside;
        if (rocket(s)) {
            // Act 10's are not shown: the first is passed going right of
            // it, the others going above it.
            inside = spawn_.record[3] == 1 ? px > x_ : py + p.radius_y < y_ + 4;
        } else {
            inside = x_ + h.left <= px && px <= x_ + h.left + (h.right - h.left) &&
                     y_ + h.top <= py && py <= y_ + h.top + (h.bottom - h.top);
        }
        if (inside && !(p.flags & player_flag::no_movement)) {
            s.checkpoint = spawn_.record[3];
            p.start_y = s16(y_ - p.radius_y);
            p.checkpoint_time = s.frames;
            p.start_x = x_;
            sprite_.variant = 1;
            state_ = State::spinning;
            spin(g); // called at once (08099844)
            if (rocket(s)) {
                rocket_checkpoint(g, s.checkpoint);
            }
            if (!alive()) {
                return;
            }
        }
        if (rocket(s)) {
            if (!near_view(sprite_.x, sprite_.y)) {
                leave(g);
            }
        } else if (near_view(sprite_.x, sprite_.y)) {
            animate(g, sprite_);
            draw(g, sprite_);
        } else {
            leave(g);
        }
    }

    // 08021808
    void spin(Game& g) {
        if (!visible(g.stage())) {
            leave(g);
            return;
        }
        if (rocket(g.stage())) {
            state_ = State::passed;
            sprite_.variant = 2;
            return;
        }
        if (animate(g, sprite_) == 0) {
            sprite_.variant = 2;
            state_ = State::passed;
        }
        draw(g, sprite_);
    }

    // 080218D8
    void shine(Game& g) {
        if (!visible(g.stage())) {
            leave(g);
            return;
        }
        if (!rocket(g.stage())) {
            animate(g, sprite_);
            draw(g, sprite_);
        }
    }

    static bool rocket(const StageState& s) { return s.zone == 10; }

    Game& game_;
    Sprite sprite_; // +0C
};

// Whether an invisible object at (x, y) has left the view's surroundings
// (128 pixels left and above, to 0x170 right, 0x120 below the top).
bool left_view(const StageState& s, s32 x, s32 y) {
    return u32(((x - s.camera.x) << 16) + 0x800000) > 0x1F00000 || s16(y - s.camera.y) + 0x80 < 0 ||
           s16(y - s.camera.y) > 0x120;
}

// 08023A74 / 080238CC: an invisible area (record +5, +6: width and height
// in 8 pixels) that puts the players in it on collision layer 0 (kind 17)
// or 1 (kind 18).
class LayerSwitch final : public Interactive {
public:
    LayerSwitch(Game& g, const ObjectSpawn& o)
        : Interactive(o), to_front_(o.record[2] == (g.game_mode == 6 ? 0xC : 0x11)) {}

    void update(Game& g) override {
        StageState& s = g.stage();
        const u8* r = spawn_.record;
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            const s32 px = p.x >> 8, py = p.y >> 8;
            if (x_ <= px && px <= x_ + r[5] * 8 && y_ <= py && py <= y_ + r[6] * 8) {
                p.layer = to_front_ ? u8(p.layer & 0xFE) : u8(p.layer | 1);
            }
        }
        if (left_view(s, x_, y_)) {
            give_back();
            g.scheduler().destroy(*this);
        }
    }

private:
    bool to_front_;
};

// 08076C90: a dash panel (record +4 set: pointing left). A player on the
// ground meeting it (the player's size widened by 5 pixels) is set running
// at 0C00h that way, unless already faster than 900h. Its creators differ
// only in the picture: 08076648 (kind 31), 08076754 (kind 32, variant 2)
// and 08076860 (kind 33, variant 1) in Neo Green Hill, 0807696C and
// 08076A78 in Angel Island.
class DashPanel final : public Interactive {
public:
    DashPanel(Game& g, const ObjectSpawn& o, u32 tiles, u16 animation, u8 variant)
        : Interactive(o), game_(g) {
        sprite_.start(g.sprite_tiles.allocate(tiles), animation, variant, x_, y_, 0x480,
                      o.record[4] != 0 ? 0x2400 : 0x2000);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 08077244

    void update(Game& g) override {
        StageState& s = g.stage();
        sprite_.x = s16(x_ - s.camera.x);
        sprite_.y = s16(y_ - s.camera.y);
        const Hitbox& h = sprite_.hitboxes[0];
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if (p.flags & player_flag::dead) {
                continue;
            }
            const s32 px = p.x >> 8, py = p.y >> 8;
            const s32 rx = p.radius_x, ry = p.radius_y;
            if (!spans_meet(px - (rx + 5), px + rx + 5, x_ + h.left, x_ + h.right) ||
                !spans_meet(py + 1 - ry, py + ry - 1, y_ + h.top, y_ + h.bottom) ||
                (p.flags & player_flag::in_air)) {
                continue;
            }
            end_player_moves(g, p);
            g.sound().start(0xAC);
            if (p.state != 5 && p.state != 6 && p.state != 7) {
                p.state = 4; // walking (from skidding too)
            }
            if (!(sprite_.flags & sprite_flag::flip_x)) {
                p.flags &= ~player_flag::facing_left;
                if (p.ground_speed < 0x900) {
                    p.ground_speed = 0xC00;
                    p.speed_x = 0xC00;
                }
            } else {
                p.flags |= player_flag::facing_left;
                if (p.ground_speed > -0x900) {
                    p.ground_speed = -0xC00;
                    p.speed_x = -0xC00;
                }
            }
            p.move = 0;
        }
        if (near_view(sprite_.x, sprite_.y)) {
            animate(g, sprite_);
            draw(g, sprite_);
        } else {
            give_back();
            g.scheduler().destroy(*this);
        }
    }

private:
    Game& game_;
    Sprite sprite_; // +0C
};

} // namespace

void create_dash_panel(Game& g, const ObjectSpawn& o, u32 tiles, u16 animation, u8 variant) {
    g.scheduler().create<DashPanel>(g, o, tiles, animation, variant);
}

namespace {

// The speed a spring gives (record +3 below 4: a table entry, else +4 x 10h).
s16 spring_speed(Game& g, const u8* r) {
    return s8(r[3]) < 4 ? g.assets().s16(0x080B84FC + (r[3] & 3) * 2) : s16(r[4] << 4);
}

// 08021F20 / 08022038: a spring lying on the ground facing sideways (kind
// 10 left, 11 right; variant 2): walking into its face sends the player
// off along the ground (080228A0); its back is solid.
class GroundSpring final : public Interactive {
public:
    GroundSpring(Game& g, const ObjectSpawn& o) : Interactive(o), game_(g) {
        left_kind_ = g.game_mode == 6 ? 8 : 10;
        points_left_ = o.record[2] == left_kind_;
        sprite_.start(g.sprite_tiles.allocate(0x10), 0x1C7, 2, x_, y_, 0x480,
                      points_left_ ? 0x2400 : 0x2000);
        animate(g, sprite_);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 0802313C

    void update(Game& g) override {
        StageState& s = g.stage();
        const bool was_sprung = sprung_;
        for (unsigned n = 0; n < s.players; ++n) {
            if (touch(g, s.player[n])) {
                sprung_ = true;
            }
        }
        if (!near_view(sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        if (was_sprung) {
            // 080220CC: the spring's animation, then idle again.
            if (animate(g, sprite_) == 0) {
                sprite_.variant = 2;
                animate(g, sprite_);
                sprung_ = false;
            }
        }
        draw(g, sprite_);
    }

private:
    // 080228A0: true when the player is sent off.
    bool touch(Game& g, Player& p) {
        const StageState& s = g.stage();
        sprite_.x = s16(x_ - s.camera.x);
        sprite_.y = s16(y_ - s.camera.y);
        const u32 r = collide_spring_box(g, sprite_, x_, y_, p);
        bool launch_left = false;
        if (r & solid::from_left) {
            if (!points_left_) {
                return blocked(g, p, r);
            }
            launch_left = true;
        } else if (r & solid::from_right) {
            if (spawn_.record[2] != left_kind_ + 1) {
                return blocked(g, p, r);
            }
        } else {
            return false;
        }
        if (g.game_mode == 3 || g.game_mode == 5) {
            throw Unrecovered("ground springs in multiplayer (080228A0)");
        }
        const s16 v = spring_speed(g, spawn_.record);
        p.speed_x = launch_left ? s16(-v) : v;
        p.ground_speed = p.speed_x;
        end_player_moves(g, p);
        p.state = (p.flags & player_flag::rolling) ? 5 : 4;
        p.angle = 0;
        if (launch_left) {
            p.flags |= player_flag::facing_left;
        } else {
            p.flags &= ~player_flag::facing_left;
        }
        sprite_.variant = 3;
        sprite_.previous_variant = 0xFF;
        g.sound().start(0xAA);
        return true;
    }

    // Touching its back: it is a wall.
    static bool blocked(Game& g, Player& p, u32 r) {
        land_player(g, p);
        if (r & solid::pushing) {
            p.flags |= player_flag::pushing;
            p.speed_x = 0;
            p.ground_speed = 0;
            set_player_radius(g, p, 6, 0xE);
        }
        return false;
    }

    Game& game_;
    Sprite sprite_; // +0C
    u8 left_kind_{};
    bool points_left_{};
    bool sprung_{}; // the update routine 080220CC
};

// Whether the player is in the invisible area of record `r` at (x, y)
// (+5, +6: width and height in 8 pixels).
bool in_area(const Player& p, const u8* r, s32 x, s32 y) {
    const s32 px = p.x >> 8, py = p.y >> 8;
    return x <= px && px <= x + r[5] * 8 && y <= py && py <= y + r[6] * 8;
}

// Only Sonic and Amy ride (Tails and Knuckles fly over).
bool rides(const Player& p) {
    return u8(p.character - 1) > 1;
}

// 08023F00 / 08023AD8: the start of a ride: a player moving right into the
// area grabs the handle and is carried along (on the other collision
// layer, in front of the level).
class RideStart final : public Interactive {
public:
    explicit RideStart(const ObjectSpawn& o) : Interactive(o) {}

    void update(Game& g) override {
        StageState& s = g.stage();
        Player& p = s.player[0];
        if (!rides(p) || (p.flags & player_flag::dead)) {
            return;
        }
        if (in_area(p, spawn_.record, x_, y_) && !(p.flags & player_flag::riding) &&
            p.speed_y >= 0 && p.speed_x > 0) {
            if (p.speed_x < 0x80) {
                p.speed_x = 0x80;
            }
            p.flags |= player_flag::riding;
            p.layer ^= 1;
            p.body->sprite.flags = (p.body->sprite.flags & ~sprite_flag::priority) | 0x1000;
            p.power |= 0x80;
            g.sound().start(p.character == 3 ? 0x86 : 0x72);
            create_ride_handle(g);
        }
        if (left_view(s, x_, y_)) {
            give_back();
            g.scheduler().destroy(*this);
        }
    }
};

// 08023F64 / 08023C58 (before the player, 0900): the end of a ride: past
// the area's middle the player is let go upwards; before it, jump lets
// go with a higher jump.
class RideEnd final : public Interactive {
public:
    explicit RideEnd(const ObjectSpawn& o) : Interactive(o, 0x900) {}

    void update(Game& g) override {
        StageState& s = g.stage();
        Player& p = s.player[0];
        if (!rides(p) || (p.flags & player_flag::dead)) {
            return;
        }
        if (in_area(p, spawn_.record, x_, y_) && (p.flags & player_flag::riding)) {
            if ((p.x >> 8) > x_ + ((spawn_.record[5] * 8) >> 1)) {
                let_go(g, p, -0x4E0, 0x11);
            } else if (s.jump_buttons & g.controller().held) {
                let_go(g, p, -0x700, 0x14);
                p.body->sprite.variant = 0;
            }
        }
        if (left_view(s, x_, y_)) {
            give_back();
            g.scheduler().destroy(*this);
        }
    }

private:
    static void let_go(Game& g, Player& p, s16 speed_y, u8 state) {
        p.speed_y = speed_y;
        g.sound().stop(p.character == 3 ? 0x86 : 0x72);
        g.sound().start(0x74);
        end_player_moves(g, p);
        p.state = state;
        p.flags = (p.flags & ~player_flag::riding) | player_flag::in_air;
        p.layer ^= 1;
        p.power &= 0x7F;
        p.body->sprite.flags = (p.body->sprite.flags & ~sprite_flag::priority) | 0x2000;
    }
};

} // namespace

bool create_interactive(Game& g, u32 creator, const ObjectSpawn& o) {
    switch (creator) {
    case 0x0801FF94:
        g.scheduler().create<Spikes>(g, o);
        return true;
    case 0x08023150:
        g.scheduler().create<BreakableBlock>(g, o);
        return true;
    case 0x080223D0:
        g.scheduler().create<SideSpring>(g, o, 6, 0xE);
        return true;
    case 0x08022178:
        g.scheduler().create<SideSpring>(g, o, 4, g.game_mode == 6 ? 0xA : 0xC);
        return true;
    case 0x08023A74:
        g.scheduler().create<LayerSwitch>(g, o);
        return true;
    case 0x08021970:
        g.scheduler().create<UpSpring>(g, o);
        return true;
    case 0x08024418:
        g.scheduler().create<DippingPlatform>(g, o);
        return true;
    case 0x080214BC:
        g.scheduler().create<Checkpoint>(g, o);
        return true;
    case 0x08023F00:
        g.scheduler().create<RideStart>(o);
        return true;
    case 0x08023F64:
        g.scheduler().create<RideEnd>(o);
        return true;
    case 0x08076648:
        create_dash_panel(g, o, 8, 0x1D7, 0);
        return true;
    case 0x08076754:
        create_dash_panel(g, o, 0xC, 0x1D7, 2);
        return true;
    case 0x08076860:
        create_dash_panel(g, o, 0xC, 0x1D7, 1);
        return true;
    case 0x08021F20:
        g.scheduler().create<GroundSpring>(g, o);
        return true;
    case 0x08074A04:
        g.scheduler().create<BouncePad>(g, o);
        return true;
    case 0x0807782C:
        g.scheduler().create<Catapult>(g, o);
        return true;
    case 0x08024D5C:
        g.scheduler().create<CrumblingPlatform>(g, o);
        return true;
    case 0x08074DC8:
        g.scheduler().create<SpringyPad>(g, o);
        return true;
    case 0x080757F8:
        if ((g.stage().zone >> 1) == 4) {
            create_holding_waterfall(g, o);
        } else {
            g.scheduler().create<Waterfall>(g, o);
        }
        return true;
    case 0x08078054:
        g.scheduler().create<Current>(g, o);
        return true;
    case 0x080204F8:
        g.scheduler().create<SideSpikes>(g, o);
        return true;
    case 0x0801F150:
        // Only goals with record +4 set count in one-player games.
        if (o.record[4] != 0 && !(g.stage().zone == 10 && g.game_mode >= 2)) {
            g.scheduler().create<Goal>(g, o);
        } else if (o.record[4] != 0 || g.game_mode >= 2) {
            throw Unrecovered("multiplayer goals (0801F150)");
        }
        return true;
    case 0x0804D5CC:
        g.scheduler().create<Hump>(o, false);
        return true;
    case 0x08023FC8:
        g.scheduler().create<SwingingPlatform>(g, o);
        return true;
    case 0x08020928:
        g.scheduler().create<PoppingSpikes>(g, o, false);
        return true;
    case 0x08020B1C:
        g.scheduler().create<PoppingSpikes>(g, o, true);
        return true;
    case 0x08027940:
        g.scheduler().create<BreakableWall>(g, o);
        return true;
    case 0x080765A4:
        g.scheduler().create<SplashZone>(o);
        return true;
    case 0x0804D82C:
        g.scheduler().create<RollStart>(o);
        return true;
    case 0x0804D9C0:
        g.scheduler().create<RollEnd>(o);
        return true;
    case 0x0804DA48:
        g.scheduler().create<LaunchCurve>(g, o);
        return true;
    case 0x0804DDA8:
        g.scheduler().create<Decoration>(g, o);
        return true;
    case 0x0804E0DC:
        g.scheduler().create<SpeedTrigger>(o, SpeedTrigger::Kind::forward);
        return true;
    case 0x0804E164:
        g.scheduler().create<SpeedTrigger>(o, SpeedTrigger::Kind::back);
        return true;
    case 0x0804E2D0:
        g.scheduler().create<SpeedTrigger>(o, SpeedTrigger::Kind::turn);
        return true;
    case 0x0804E440:
        g.scheduler().create<SpeedTrigger>(o, SpeedTrigger::Kind::back_slow);
        return true;
    case 0x0804D664:
        g.scheduler().create<Hump>(o, true);
        return true;
    default:
        return create_secret_base_object(g, creator, o) ||
               create_casino_paradise_object(g, creator, o) ||
               create_ice_paradise_object(g, creator, o) ||
               create_angel_island_object(g, creator, o) ||
               create_egg_rocket_object(g, creator, o) || create_x_zone_object(g, creator, o);
    }
}

} // namespace sonic_advance_gba
