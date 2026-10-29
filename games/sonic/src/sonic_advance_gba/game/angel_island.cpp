// Angel Island's objects (zone 5, acts 8 and 9).

#include "sonic_advance_gba/game/angel_island.hpp"
#include "sonic_advance_gba/game/collision.hpp"
#include "sonic_advance_gba/game/game.hpp"
#include "sonic_advance_gba/game/interactive.hpp"
#include "sonic_advance_gba/game/math.hpp"
#include "sonic_advance_gba/game/object_base.hpp"
#include "sonic_advance_gba/game/object_util.hpp"
#include "sonic_advance_gba/game/player.hpp"
#include "sonic_advance_gba/game/solid.hpp"
#include "sonic_advance_gba/game/sound.hpp"
#include "sonic_advance_gba/game/stage.hpp"
#include <algorithm>
#include <array>
#include <cstdlib>

namespace sonic_advance_gba {

namespace {

// Hitbox `h` of something at (x, y) meeting the first player's body: hurt.
// (The body box's index is not looked at.)
void hurts_first_player(Game& g, const Hitbox& h, s16 x, s16 y) {
    Player& p = g.stage().player[0];
    if (h.index != 0xFFFFFFFF &&
        hitboxes_meet(x, y, h, p.x >> 8, p.y >> 8, p.body->sprite.hitboxes[0])) {
        hurt_player(g, p);
    }
}

// 0806E698, 0806E7C0: walks to and fro on the ground within its range
// (record +3: the start, +5: the width, in 8 pixels) at 0A0h, turning at
// the ends (0806EA44: 14 frames). When the player is ahead of it (up to 80
// pixels, from 32 above to 8 below) it lunges (0806EBAC: 31 frames, its
// second hitbox hurting; then 60 frames before the next). Not made in
// Angel Island while the save's byte +18 is set.
class Lunger final : public Enemy {
public:
    Lunger(Game& g, const ObjectSpawn& o) : Enemy(g, o, 0x18) {
        sprite_.start(tiles_, 0x1AE, 0, s16(base_x_), s16(base_y_), 0x480, 0x2000);
        animate(g, sprite_);
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        if (state_ == State::walking) {
            offset_x_ += speed_;
        }
        const s16 x = s16(base_x_ + (offset_x_ >> 8)), y = s16(base_y_ + offset_y_);
        sprite_.x = s16(x - s.camera.x);
        sprite_.y = s16(y - s.camera.y);
        if (out_of_range(s, base_x_, base_y_, sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        if (state_ == State::lunging) {
            hurts_first_player(g, sprite_.hitboxes[1], x, y);
        }
        if (enemy_touched(g, sprite_, x, y)) {
            g.scheduler().destroy(*this);
            return;
        }
        u8 angle = 0;
        offset_y_ =
            u16(offset_y_ + probe_short(g.assets(), s.map, y + 3, x, 1, 8, &angle, Sensor::floor));
        switch (state_) {
        case State::walking:
            walk(s, x, y);
            break;
        case State::turning: // 0806EA44
            if (++timer_ == 0xE) {
                sprite_.variant = 0;
                state_ = State::walking;
            }
            break;
        case State::lunging: // 0806EBAC
            if (++timer_ == 0x1F) {
                sprite_.variant = 0;
                state_ = State::walking;
            }
            break;
        }
        animate(g, sprite_);
        draw(g, sprite_);
    }

private:
    enum class State { walking, turning, lunging };

    void turn(s16 speed) {
        speed_ = speed;
        timer_ = 0;
        sprite_.variant = 2;
        sprite_.flags ^= sprite_flag::flip_x;
        state_ = State::turning;
    }

    void walk(const StageState& s, s16 x, s16 y) {
        const s32 along = offset_x_ >> 8;
        const bool right = sprite_.flags & sprite_flag::flip_x;
        if (along <= param(3) * 8) {
            if (!right) {
                turn(0xA0);
            }
        } else if (along >= (param(3) + spawn_.record[5]) * 8) {
            if (right) {
                turn(-0xA0);
            }
        } else if (cooldown_ != 0) {
            --cooldown_;
        } else {
            const Player& p = s.player[0];
            const s32 px = p.x >> 8, py = p.y >> 8;
            const bool ahead = right ? (x <= px && px <= x + 0x50) : (px <= x && x - 0x50 <= px);
            if (py <= y + 8 && y - 0x20 <= py && ahead) {
                cooldown_ = 0x3C;
                timer_ = 0;
                sprite_.variant = 1;
                state_ = State::lunging;
            }
        }
    }

    Sprite sprite_;     // +0C
    s32 offset_x_{};    // +44 8.8
    s16 speed_ = -0xA0; // +48
    s16 timer_{};       // +4A
    u16 offset_y_{};    // +4C
    // +4E: the creator leaves it as the heap had it (0 wherever seen).
    s16 cooldown_{};
    State state_ = State::walking;
};

// 0806EDE8, 0806EFF8: flies to and fro within its range (record +3: the
// start, +5: the width, in 8 pixels) on a swinging course (its heading
// swings up and down; across 1/(80 - record +6 x 4) of it a frame), a head
// followed by four body parts on its path of the last 64 frames (the
// parts trail by 7-24 frames across, 10-40 down; each hurts players it
// touches). It turns at the ends when its swing turns.
class Serpent final : public Enemy {
public:
    Serpent(Game& g, const ObjectSpawn& o) : Enemy(g, o, 4) {
        xs_.fill(s16(base_x_));
        ys_.fill(s16(base_y_));
        head_.start(tiles_, 0x1AF, 0, s16(base_x_), s16(base_y_), 0x480, 0x2000);
        animate(g, head_);
        small_tiles_ = g.sprite_tiles.allocate(4);
        small_.start(small_tiles_, 0x1AF, 1, s16(base_x_), s16(base_y_), 0x480, 0x2000);
        animate(g, small_);
        large_tiles_ = g.sprite_tiles.allocate(4);
        large_.start(large_tiles_, 0x1AF, 2, s16(base_x_), s16(base_y_), 0x480, 0x2000);
        animate(g, large_);
    }

    void on_destroy() override { // 0806F78C
        Enemy::on_destroy();
        game_.sprite_tiles.free(small_tiles_);
        game_.sprite_tiles.free(large_tiles_);
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        const Assets& a = g.assets();
        offset_x_ += divide(sine(a, (heading_ & 0x3FF) + 0x100u), 0x50 - spawn_.record[6] * 4);
        const s16 x = s16(base_x_ + (offset_x_ >> 8));
        offset_y_ += divide(sine(a, heading_ & 0x3FF), 0x50);
        const s16 y = s16(base_y_ + (offset_y_ >> 8));
        head_.x = s16(x - s.camera.x);
        head_.y = s16(y - s.camera.y);
        const bool turned = swing();
        ++step_;
        xs_[step_ & 0x3F] = x;
        ys_[step_ & 0x3F] = y;
        if (out_of_range(s, base_x_, base_y_, head_.x, head_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        if (enemy_touched(g, head_, x, y)) {
            g.scheduler().destroy(*this);
            return;
        }
        const s32 along = offset_x_ >> 8;
        const bool right = head_.flags & sprite_flag::flip_x;
        if (along <= param(3) * 8) {
            if (!right && turned) {
                face(true);
            }
        } else if (along >= (param(3) + spawn_.record[5]) * 8 && right && turned) {
            face(false);
        }
        animate(g, head_);
        draw(g, head_);
        // The body parts, from the tail: two large, two small.
        part(g, large_, 0x18, 0x28, true);
        part(g, large_, 0x13, 0x1E, false);
        part(g, small_, 0xE, 0x14, true);
        part(g, small_, 7, 10, false);
    }

private:
    // The heading swings between its limits; true when it turned back.
    bool swing() {
        const bool right = head_.flags & sprite_flag::flip_x;
        if (!right) {
            if (!rising_) {
                heading_ = u16(heading_ - 8);
                if ((heading_ & 0x3FF) < 0x19C) {
                    heading_ = 0x19C;
                    rising_ = true;
                    return true;
                }
            } else {
                heading_ = u16(heading_ + 5);
                if ((heading_ & 0x3FF) > 0x264) {
                    heading_ = 0x264;
                    rising_ = false;
                    return true;
                }
            }
        } else if (!rising_) {
            heading_ = u16(heading_ - 5);
            if (u16((heading_ & 0x3FF) - 0x65) < 0x337) {
                heading_ = 0x39C;
                rising_ = true;
                return true;
            }
        } else {
            heading_ = u16(heading_ + 5);
            if (u16((heading_ & 0x3FF) - 0x65) < 0x337) {
                heading_ = 100;
                rising_ = false;
                return true;
            }
        }
        return false;
    }

    void face(bool right) {
        speed_ = right ? 0xA0 : -0xA0;
        for (Sprite* sp : {&head_, &small_, &large_}) {
            if (right) {
                sp->flags |= sprite_flag::flip_x;
            } else {
                sp->flags &= ~sprite_flag::flip_x;
            }
        }
        heading_ ^= 0x100;
    }

    // A body part: across half-way between the head and where the head was
    // `back_x` frames ago, at the height it had `back_y` frames ago.
    void part(Game& g, Sprite& sp, unsigned back_x, unsigned back_y, bool animate_it) {
        const auto& c = g.stage().camera;
        const s16 then = xs_[(step_ - back_x) & 0x3F];
        const s16 gap = s16(head_.x - (then - c.x));
        const s16 x = s16(then - (gap >> 1));
        const s16 y = ys_[(step_ - back_y) & 0x3F];
        sp.x = s16(x - c.x);
        sp.y = s16(y - c.y);
        if (animate_it) {
            animate(g, sp);
        }
        draw(g, sp);
        hurts_first_player(g, sp.hitboxes[0], x, y);
    }

    Sprite head_;  // +0C
    Sprite small_; // +48
    Sprite large_; // +84
    u32 small_tiles_{}, large_tiles_{};
    s32 offset_x_{};           // +B4 8.8
    s32 offset_y_{};           // +B8
    u16 heading_ = 100;        // +BC
    s16 speed_ = -0xA0;        // +BE (set at the turns; not read)
    std::array<s16, 64> xs_{}; // +C0 the head's path
    std::array<s16, 64> ys_{}; // +140
    bool rising_ = true;       // +1C0
    u8 step_{};                // +1C1
};

// A player's position within the box at (x, y), record +5 x +6 eight-pixel
// units (edges included).
bool inside_box(const Player& p, s32 x, s32 y, const u8* record) {
    const s32 px = p.x >> 8, py = p.y >> 8;
    return x <= px && px <= x + record[5] * 8 && y <= py && py <= y + record[6] * 8;
}

// 0807578C, 08075324: an invisible launcher (the box record +5 x +6 eight-
// pixel units; record +3: 0 to the left, else to the right): a player
// inside running that way faster than 400h is thrown up (800h) and on
// (400h faster, up to 1100h).
class Launcher final : public Interactive {
public:
    explicit Launcher(const ObjectSpawn& o) : Interactive(o), rightward_(o.record[3] != 0) {}

    void update(Game& g) override {
        StageState& s = g.stage();
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if ((p.flags & player_flag::dead) || !inside_box(p, x_, y_, spawn_.record)) {
                continue;
            }
            if (rightward_ ? p.speed_x <= 0x400 : p.speed_x >= -0x400) {
                continue;
            }
            p.flags = (p.flags & 0xFFFFFEF3u) | player_flag::in_air;
            set_player_radius(g, p, 6, 0xE);
            p.speed_y = -0x800;
            if (rightward_) {
                p.speed_x = s16(std::min(p.speed_x + 0x400, 0x1100));
            } else {
                p.speed_x = s16(std::max(p.speed_x - 0x400, -0x1100));
            }
            end_player_moves(g, p);
            p.state = 0x11;
            g.sound().start(0xAA);
        }
        if (!near_view(s16(x_ - s.camera.x), s16(y_ - s.camera.y))) {
            give_back();
            g.scheduler().destroy(*this);
        }
    }

private:
    bool rightward_; // +09
};

// 0808B998, 0808BB10: a swinging hazard (record +5 across or +6 up and
// down, the larger; 8 pixels each per unit, 256 frames a swing; record
// +3 / +4 negative starts half a swing on) that hurts players it touches.
class SwingingHazard final : public Interactive {
public:
    SwingingHazard(Game& g, const ObjectSpawn& o) : Interactive(o), game_(g) {
        if (o.record[6] < o.record[5]) {
            step_x_ = 4;
            phase_ = s8(o.record[3]) < 0 ? 0x80 : 0;
        } else {
            step_y_ = 4;
            phase_ = s8(o.record[4]) < 0 ? 0x80 : 0;
        }
        const u16 animation = (g.stage().zone >> 1) == 4 ? 0x21D : 0x1FE;
        sprite_.start(g.sprite_tiles.allocate(0x24), animation, 0, x_, y_, 0x480, 0x2000);
        animate(g, sprite_);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 0808BD2C

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
        sprite_.x = s16(x - s.camera.x);
        sprite_.y = s16(y - s.camera.y);
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if (!(p.flags & player_flag::dead) && touches_player(sprite_, x, y, p)) {
                hurt_player(g, p);
            }
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
    Sprite sprite_;           // +0C
    u16 phase_{};             // +3C
    s32 offset_x_{};          // +40 8.8
    s32 offset_y_{};          // +44
    s16 step_x_{}, step_y_{}; // +48, +4A
};

// 0808C450, 0808BD40: a bar to hang from (not drawn; the box from record
// +3, +4 eight-pixel units off its place, record +5 x +6 units). A player
// falling into the box grabs it (18 pixels below its top) and moves along
// it hand over hand with left and right (2 pixels a frame); jumping or
// going past its ends lets go (the player drops, spinning, 22 pixels
// below; with left or right held moving 200h that way).
class HangingBar final : public Interactive {
public:
    explicit HangingBar(const ObjectSpawn& o) : Interactive(o, 0x8000) {}

    void update(Game& g) override {
        StageState& s = g.stage();
        const u8* r = spawn_.record;
        const s32 left = x_ + s8(r[3]) * 8, right = left + r[5] * 8;
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            const u8 bit = u8(1u << n);
            if (!(hanging_ & bit)) {
                const s32 px = p.x >> 8, py = p.y >> 8, top = y_ + s8(r[4]) * 8;
                if ((p.flags & player_flag::dead) || p.speed_y < 1 || px < left || px > right ||
                    py < top || py > top + r[6] * 8) {
                    continue;
                }
                grab(g, p, n);
                hanging_ |= bit;
                continue;
            }
            if (p.flags & player_flag::dead) {
                hanging_ &= u8(~bit);
                continue;
            }
            hang(g, s, p, n, left, right);
        }
        if (hanging_ == 0 && !near_view(s16(x_ - s.camera.x), s16(y_ - s.camera.y))) {
            give_back();
            g.scheduler().destroy(*this);
        }
    }

private:
    // Facing and the hanging state by the direction held.
    static s32 face(Player& p) {
        if (p.held & 0x10) {
            p.state = 0x24;
            p.flags &= ~player_flag::facing_left;
            return 1;
        }
        if (p.held & 0x20) {
            p.state = 0x24;
            p.flags |= player_flag::facing_left;
            return -1;
        }
        p.state = 0x23;
        return 0;
    }

    void grab(Game& g, Player& p, unsigned n) {
        p.angle = 0;
        end_player_moves(g, p);
        p.flags = (p.flags | player_flag::no_movement) &
                  ~(player_flag::in_air | player_flag::jump_cancel);
        p.power |= 0x80;
        face(p);
        p.y = y_ * 0x100 + 0x1200;
        hold_x_[n] = p.x;
        set_player_radius(g, p, 6, 0xE);
    }

    void hang(Game& g, StageState& s, Player& p, unsigned n, s32 left, s32 right) {
        const s32 way = face(p);
        if (way != 0) {
            p.x = hold_x_[n] + way * 0x200;
        }
        p.y = (y_ + s8(g.assets().u8(hang_heights + p.character))) * 0x100;
        hold_x_[n] = p.x;
        const s32 px = p.x >> 8;
        if (!(p.pressed & s.jump_buttons) && left <= px && px <= right) {
            return;
        }
        // Lets go.
        end_player_moves(g, p);
        p.flags = (p.flags & ~(player_flag::no_movement | player_flag::jump_cancel)) |
                  player_flag::rolling;
        p.state = 5;
        p.speed_y = 0;
        p.flags |= player_flag::in_air;
        p.state = 0x12;
        p.power &= 0x7F;
        p.y = y_ * 0x100 + 0x1600;
        if (p.held & 0x10) {
            p.speed_x = p.ground_speed = 0x200;
        } else if (p.held & 0x20) {
            p.flags |= player_flag::facing_left;
            p.speed_x = p.ground_speed = -0x200;
        } else {
            p.speed_x = p.ground_speed = 0;
        }
        hanging_ &= u8(~(1u << n));
    }

    static constexpr u32 hang_heights = 0x086C8CD8; // by character: the hands' height below the bar

    u8 hanging_{};                // +3C a bit per player hanging
    std::array<s32, 2> hold_x_{}; // +40 where each player hangs
};

// 0808F418, 0808EF90: an updraft (not drawn; the box record +5 x +6
// eight-pixel units): players inside rise (68h a frame faster, up to 240h)
// floating (state 25h; with record +3 set, as off a spring, 11h). Its sound
// (C8h) starts when a player begins floating; any player not floating
// fades it (music player 2). (The per-player flags +38/+3A are never set.)
class Updraft final : public Interactive {
public:
    explicit Updraft(const ObjectSpawn& o) : Interactive(o) {}

    void update(Game& g) override {
        StageState& s = g.stage();
        const s16 sx = s16(x_ - s.camera.x), sy = s16(y_ - s.camera.y);
        if (u32(s32(sx) + 0x80) > 0x1F0 || sy + 0x80 < 0 || sy > 0x120) {
            if (sounding_ == 1) {
                g.sound().stop(0xC8);
            }
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        const bool spring = spawn_.record[3] != 0;
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if (p.state != 0x25 && sounding_ != 0) {
                g.sound().fade_out(2, 4);
                sounding_ = 0;
            }
            if ((p.flags & player_flag::dead) || !inside_box(p, x_, y_, spawn_.record)) {
                continue;
            }
            if (sounding_ == 0 && p.state != 0x25 && !spring) {
                sounding_ = 1;
                g.sound().start(0xC8);
            }
            p.speed_y = s16(std::max(p.speed_y - 0x68, -0x240));
            p.flags |= player_flag::in_air;
            end_player_moves(g, p);
            p.state = spring ? 0x11 : 0x25;
            if (p.character == 3) {
                p.flags |= 0x2000000;
            }
            if (probe_short(g.assets(), s.map, p.y >> 8, p.x >> 8, p.layer, 8, nullptr,
                            Sensor::floor) > 4) {
                p.flags = (p.flags & 0xFFFFFEEBu) | player_flag::in_air;
            }
        }
    }

private:
    u8 sounding_{}; // +3D
};

// 0808FD30: a boulder from a Boulder spawner: it rolls on the ground
// (from 40h, sped up and slowed by the slopes), leaves ledges (thrown down
// by 2/5 of its speed), may land record +3 times and bursts on the next
// landing (0809003C; with a sound when on screen). It hurts players it
// touches.
class Boulder final : public Process {
public:
    Boulder(Game& g, s16 x, s16 y, u8 landings)
        : Process(0x2000), game_(g), x_(x), y_(y), landings_(landings) {
        sprite_.start(g.sprite_tiles.allocate(0x10), 0x1C9, 0, x, y, 0x480, 0x2000);
        animate(g, sprite_);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 080904B4

    void update(Game& g) override {
        StageState& s = g.stage();
        const Assets& a = g.assets();
        sprite_.x = s16(x_ + (offset_x_ >> 8) - s.camera.x);
        sprite_.y = s16(y_ + (offset_y_ >> 8) - s.camera.y);
        // (The first test compares its place in the act with the view's
        // size, as the original does.)
        const bool near =
            (u32(s32(x_) + 0x80) <= 0x1F0 && y_ + 0x80 >= 0 && y_ <= 0x120) ||
            (u16(sprite_.x + 0x80) <= 0x1F0 && sprite_.y + 0x80 >= 0 && sprite_.y <= 0x120);
        if (!near) {
            g.scheduler().destroy(*this);
            return;
        }
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if (!(p.flags & player_flag::dead) &&
                touches_player(sprite_, s16(x_ + (offset_x_ >> 8)), s16(y_ + (offset_y_ >> 8)),
                               p)) {
                hurt_player(g, p);
            }
        }
        const s16 x = s16(x_ + (offset_x_ >> 8)), y = s16(y_ + (offset_y_ >> 8));
        if (!airborne_) {
            // (Over an empty tile the probe leaves the angle alone: the
            // original then rolls by whatever its stack held; flat here.)
            u8 angle = 0;
            const s32 d = probe_short(a, s.map, y, x, 1, 8, &angle, Sensor::floor);
            if (d < 6) {
                speed_x_ += divide(sine(a, angle * 4u + 0x100), 0x4E2);
                offset_y_ += (d + 2) * 0x100;
            } else {
                speed_y_ += std::abs(divide(speed_x_ * 2, 5));
                airborne_ = true;
            }
        } else if (probe_short(a, s.map, y, x, 1, 8, nullptr, Sensor::floor) < 0) {
            speed_y_ = 0;
            airborne_ = false;
            if (landings_ == 0) {
                burst(g, x, y);
                return;
            }
            --landings_;
        } else {
            speed_y_ += 0x20;
        }
        offset_x_ += speed_x_;
        offset_y_ += speed_y_;
        animate(g, sprite_);
        draw(g, sprite_);
    }

private:
    void burst(Game& g, s16 x, s16 y) {
        g.scene.focus_x = sprite_.x;
        g.scene.focus_y = sprite_.y;
        create_block_debris(g, s16(x - g.scene.focus_x), s16(y - g.scene.focus_y),
                            {0x1CB, 4, 0x1CC, 1, 0x3D, 8, 0x100, 0x70, 0});
        if (u16(sprite_.x + 0x80) < 0x1F1 && sprite_.y + 0x80 >= 0 && sprite_.y < 0x121) {
            g.sound().start(0x90);
        }
        g.scheduler().destroy(*this);
    }

    Game& game_;
    Sprite sprite_;      // +0C
    s16 x_, y_;          // +B2, +B4 where it came out
    s32 speed_x_ = 0x40; // +A0 8.8
    s32 speed_y_{};      // +A4
    s32 offset_x_{};     // +A8
    s32 offset_y_{};     // +AC
    bool airborne_{};    // +B0
    u8 landings_;        // +B1 landings left
};

// 08090448, 0808FB78: lets a boulder out every 120 frames of the stage
// clock (record +3: how often it may land before bursting). Not drawn.
class BoulderSpawner final : public Interactive {
public:
    explicit BoulderSpawner(const ObjectSpawn& o) : Interactive(o) {}

    void update(Game& g) override {
        StageState& s = g.stage();
        if (remainder(s32(s.ticks), 0x78) == 0) {
            g.scheduler().create<Boulder>(g, x_, y_, spawn_.record[3]);
        }
        const auto& c = s.camera;
        if (c.x + 0x170 < x_ || x_ < c.x - 0x80 || c.y + 0x120 < y_ || y_ < c.y - 0x80) {
            give_back();
            g.scheduler().destroy(*this);
        }
    }
};

// 0809215C, 0809226C and 0808EDD0, 0808EEF4: decoration, animated, given
// back out of view.
class Decoration final : public Interactive {
public:
    Decoration(Game& g, const ObjectSpawn& o, u16 animation, u32 tiles, u16 layer)
        : Interactive(o), game_(g) {
        sprite_.start(g.sprite_tiles.allocate(tiles), animation, 0, x_, y_, layer, 0x2000);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 080922F4, 0808EF7C

    void update(Game& g) override {
        StageState& s = g.stage();
        sprite_.x = s16(x_ - s.camera.x);
        sprite_.y = s16(y_ - s.camera.y);
        if (!near_view(sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        animate(g, sprite_);
        draw(g, sprite_);
    }

private:
    Game& game_;
    Sprite sprite_; // +0C
};

// 0808E484, 0808E5A8: a cart on a straight track (record +3: the
// direction, eighths of a turn; record +5: its length). A player standing
// on it drives it by running (0808E98C: the cart takes the player's speed,
// doubled forward, up to 0B20h; the player stays on it); with nobody on it
// it rolls back (0808E788: 0Ch a frame slower). Its wheels turn with its
// speed and rumble (sounds D4h-D6h, more often the faster). Knuckles
// gliding or climbing only meets it as a solid block.
class Cart final : public Interactive {
public:
    Cart(Game& g, const ObjectSpawn& o)
        : Interactive(o), game_(g), length_(o.record[5] * 0x10E0), direction_(o.record[3]) {
        sprite_.start(g.sprite_tiles.allocate(0x10), 0x21E, 1, x_, y_, 0x480, 0x2000);
        animate(g, sprite_);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 0808EDBC

    void update(Game& g) override {
        StageState& s = g.stage();
        sprite_.x = s16(x_ + track_x(g) - s.camera.x);
        sprite_.y = s16(y_ + track_y(g) - s.camera.y);
        if (out_of_range(s, x_, y_, sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if ((p.flags & player_flag::on_object) && p.standing_on == &sprite_) {
                ride(g, s, p);
            } else {
                meet(g, s, p);
            }
        }
        if (speed_ != 0) {
            animate(g, sprite_);
        }
        draw(g, sprite_);
    }

private:
    // The place along the track: its eighth-turn direction (sine / 6000:
    // -2 to 2) times the distance in units of 0C80h.
    s16 track_x(Game& g) const {
        return s16(s16(divide(sine(g.assets(), (direction_ & 7) * 0x80u + 0x100), 6000)) *
                   s16(divide(position_, 0xC80)));
    }

    s16 track_y(Game& g) const {
        return s16(s16(divide(sine(g.assets(), (direction_ & 7) * 0x80u), 6000)) *
                   s16(divide(position_, 0xC80)));
    }

    static bool knuckles_busy(const StageState& s) {
        return s.character == 2 && u8(s.player[0].state - 0x46) <= 5;
    }

    void wheels() { sprite_.speed = u8(std::abs(divide(speed_, 0x1C))); }

    void rumble(Game& g) {
        ++timer_;
        if (u32(speed_ + 0x4FF) < 0x9FF) {
            if (speed_ != 0 && remainder(s16(timer_), 0x19) == 0) {
                g.sound().start(0xD4);
            }
        } else if (u32(speed_ + 0x81F) < 0x103F) {
            if (remainder(s16(timer_), 0x10) == 0) {
                g.sound().start(0xD5);
            }
        } else if (remainder(s16(timer_), 8) == 0) {
            g.sound().start(0xD6);
        }
    }

    // 0808E788: a player not on it may land on it; the first player's
    // turn moves the empty cart.
    void meet(Game& g, StageState& s, Player& p) {
        if (!(p.flags & player_flag::dead)) {
            if (knuckles_busy(s)) {
                collide_solid(g, sprite_, s16(x_ + track_x(g)), s16(y_ + track_y(g)), p);
            } else if (collide_solid(g, sprite_, s16(sprite_.x + s.camera.x),
                                     s16(sprite_.y + s.camera.y), p) &
                       solid::on_top) {
                p.flags |= player_flag::on_object;
                p.standing_on = &sprite_;
                last_speed_ = 0;
                timer_ = 0;
                p.ground_speed = s16(speed_);
                p.power |= 0x80;
                p.flags &= ~player_flag::no_movement;
            }
        }
        if (p.index != 0) {
            return;
        }
        wheels();
        speed_ -= 0xC;
        position_ += speed_;
        if (position_ > length_) {
            position_ = length_;
        }
        if (position_ < 0) {
            position_ = 0;
            speed_ = 0;
        }
        const u8 variant = speed_ >= 0 ? 1 : 0;
        if (sprite_.variant != variant) {
            sprite_.variant = variant;
            sprite_.previous_variant = 0xFF;
        }
        rumble(g);
    }

    // 0808E98C: a player on it.
    void ride(Game& g, StageState& s, Player& p) {
        if (p.state == 0xF || (p.flags & player_flag::dead)) {
            if ((p.flags & player_flag::on_object) && p.standing_on == &sprite_) {
                p.standing_on = nullptr;
                p.flags &= ~player_flag::on_object;
            }
            return;
        }
        if (knuckles_busy(s)) {
            collide_solid(g, sprite_, s16(x_ + track_x(g)), s16(y_ + track_y(g)), p);
            return;
        }
        const u32 flags = p.flags;
        p.y = (sprite_.y + s.camera.y + sprite_.hitboxes[0].top - p.radius_y) * 0x100;
        if (!(flags & player_flag::in_air) && p.move == 0) {
            p.x = (x_ + track_x(g)) * 0x100 - p.speed_x;
        } else {
            timer_ = 0;
            if (!(flags & player_flag::in_air)) {
                if (collide_solid(g, sprite_, s16(sprite_.x + s.camera.x),
                                  s16(sprite_.y + s.camera.y), p) &
                    solid::on_top) {
                    p.flags |= player_flag::on_object;
                    p.standing_on = &sprite_;
                    p.ground_speed = s16(std::clamp<s32>(p.ground_speed, -0x240, 0x240));
                    p.power |= 0x80;
                }
            } else {
                p.standing_on = nullptr;
                p.flags = flags & ~player_flag::on_object;
                if (p.character == 3 && (flags & 0x4000000)) {
                    speed_ = 0;
                } else {
                    p.speed_x = s16(std::clamp<s32>(p.speed_x, -0x120, 0x120));
                }
                p.power &= 0x7F;
            }
        }
        if (p.index != 0) {
            // The partner only takes the cart's speed (half of it forward).
            p.ground_speed = s16(speed_ > 0 ? speed_ >> 1 : speed_);
            return;
        }
        wheels();
        speed_ = (p.character == 3 && p.state == 2) ? p.ground_speed : p.ground_speed - 0x10;
        if (speed_ > 0) {
            speed_ <<= 1;
        }
        speed_ = std::min<s32>(speed_, 0xB20);
        position_ += speed_;
        bool slowed = true;
        if (position_ > length_) {
            position_ = length_;
            if (speed_ < 0) {
                speed_ = 0;
            } else {
                slowed = false;
            }
        }
        if (slowed && speed_ < 0 && p.ground_speed < 1 && (p.character != 0 || p.move != 3)) {
            p.ground_speed = s16(p.ground_speed - 0x40);
        }
        p.ground_speed = s16(std::clamp<s32>(p.ground_speed, -0x900, 0x480));
        if (position_ < 0) {
            position_ = 0;
            speed_ = 0;
            if (p.move == 0 && p.ground_speed < 0) {
                p.ground_speed = 0;
            }
            sprite_.previous_variant = 0xFF;
            sprite_.variant = 1;
        }
        if (speed_ < 0) {
            if (last_speed_ >= 0) {
                sprite_.previous_variant = 0xFF;
                sprite_.variant = 0;
            }
        } else if (last_speed_ < 1) {
            sprite_.previous_variant = 0xFF;
            sprite_.variant = 1;
        }
        rumble(g);
        last_speed_ = speed_;
    }

    Game& game_;
    Sprite sprite_;    // +0C
    s32 length_;       // +3C
    s32 position_{};   // +40 along the track
    s32 speed_{};      // +44
    s32 last_speed_{}; // +48
    u16 timer_{};      // +4C
    u8 direction_;     // +4E
};

// 0808A608, 0808A768: a lift: a handle (picture 221h, variant 1) on a pole
// (variant 0, drawn four times above it). A player touching the handle
// takes it (state 26h) and is carried 128 pixels up in 64 frames
// (0808A950); at the top (0808AB68) a jump lets go (400h up, spinning);
// then the handle waits a second and comes down in 30 frames (0808AD6C).
class Lift final : public Interactive {
public:
    Lift(Game& g, const ObjectSpawn& o) : Interactive(o), game_(g) {
        handle_.start(g.sprite_tiles.allocate(9), 0x221, 1, 0, 0, 0x3C0, 0x2000);
        pole_.start(g.sprite_tiles.allocate(4), 0x221, 0, 0, 0, 0x440, 0x2000);
        animate(g, handle_);
        animate(g, pole_);
    }

    void on_destroy() override { // 0808AF5C
        game_.sprite_tiles.free(handle_.tiles.to);
        game_.sprite_tiles.free(pole_.tiles.to);
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        switch (state_) {
        case State::waiting:
            wait(g, s);
            break;
        case State::rising:
            rise(g, s);
            break;
        case State::top:
            top(g, s);
            break;
        case State::lowering:
            lower(g, s);
            break;
        }
    }

private:
    enum class State { waiting, rising, top, lowering };

    Player& rider(StageState& s) { return s.player[rider_ != 0 ? 1 : 0]; }

    // The handle `lift` pixels up; the pole's four parts `first` and then
    // `step` pixels apart above it. False when it left the view.
    bool show(Game& g, StageState& s, s16 lift, s16 first, s16 step) {
        handle_.x = pole_.x = s16(x_ - s.camera.x);
        handle_.y = s16(y_ - s.camera.y - lift);
        if (out_of_range(s, x_, y_, handle_.x, handle_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return false;
        }
        draw(g, handle_);
        for (int i = 0; i < 4; ++i) {
            pole_.y = s16(y_ - s.camera.y - lift - first - i * step);
            draw(g, pole_);
        }
        return true;
    }

    void wait(Game& g, StageState& s) {
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if (!touches_player(handle_, x_, y_, p)) {
                continue;
            }
            timer_ = 0;
            rider_ = p.index;
            p.flags |= player_flag::no_movement;
            p.speed_x = p.ground_speed = p.speed_y = 0;
            end_player_moves(g, p);
            p.state = 0x26;
            g.sound().start(0xB5);
            p.power |= 0x80;
            state_ = State::rising;
            break;
        }
        show(g, s, 0, 0x10, 0x20);
    }

    // 0808A950
    void rise(Game& g, StageState& s) {
        const s16 lift = s16(0x80 - divide((0x40 - timer_) * 0x80, 0x40));
        Player& p = rider(s);
        if (!(p.flags & player_flag::dead)) {
            p.x = (x_ + ((p.flags & player_flag::facing_left) ? 8 : -3)) * 0x100;
            p.y = (y_ - lift + 0x1A) * 0x100;
        } else {
            p.flags &= ~player_flag::no_movement;
            s.player[0].power &= 0x7F;
        }
        if (++timer_ > 0x40) {
            timer_ = 0;
            g.sound().stop(0xB5);
            state_ = State::top;
        }
        show(g, s, lift, 0x10, 0x20);
    }

    // 0808AB68
    void top(Game& g, StageState& s) {
        handle_.x = pole_.x = s16(x_ - s.camera.x);
        handle_.y = s16(y_ - s.camera.y - 0x80);
        if (out_of_range(s, x_, y_, handle_.x, handle_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        Player& p = rider(s);
        if (!(p.flags & player_flag::dead)) {
            if (p.pressed & s.jump_buttons) {
                p.flags &= ~(player_flag::no_movement | player_flag::jump_cancel);
                p.speed_x = p.ground_speed = 0;
                p.speed_y = -0x400;
                end_player_moves(g, p);
                timer_ = 0;
                wait_ = 0x3C;
                state_ = State::lowering;
                p.state = p.character == 3 ? 0x55 : 5;
                p.power &= 0x7F;
            }
        } else {
            p.flags &= ~player_flag::no_movement;
            s.player[0].power &= 0x7F;
            timer_ = 0;
            wait_ = 0x3C;
            state_ = State::lowering;
        }
        draw(g, handle_);
        for (int i = 0; i < 4; ++i) {
            pole_.y = s16(y_ - s.camera.y - 0x90 - i * 0x10);
            draw(g, pole_);
        }
    }

    // 0808AD6C
    void lower(Game& g, StageState& s) {
        const s16 lift = s16(divide((0x1E - timer_) * 0x80, 0x1E));
        if (wait_ != 0) {
            --wait_;
        } else if (++timer_ > 0x1E) {
            timer_ = 0;
            rider_ = 0;
            state_ = State::waiting;
        }
        show(g, s, lift, 0x10, 0x20);
    }

    Game& game_;
    Sprite handle_; // +0C
    Sprite pole_;   // +3C
    u16 timer_{};   // +9C
    s16 wait_{};    // +9E
    u8 rider_{};    // +A0 the rider's index
    State state_ = State::waiting;
};

// 08094CD8, 08094DCC: a spring in the ground (only its bounce is drawn,
// 08094E60): a player touching it (or hitting it with Amy's hammer, then
// half as fast again) is thrown up at record +4 x 40h, springing (record
// +3 bit 0 clear) or spinning.
class GroundSpring final : public Interactive {
public:
    GroundSpring(Game& g, const ObjectSpawn& o) : Interactive(o), game_(g) {
        sprite_.start(g.sprite_tiles.allocate(0x10), 0x1C7, 0, x_, y_, 0x480, 0x2000);
        animate(g, sprite_);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 08095190

    void update(Game& g) override {
        StageState& s = g.stage();
        sprite_.x = s16(x_ - s.camera.x);
        sprite_.y = s16(y_ - s.camera.y);
        if (!bouncing_) {
            if (throw_players(g, s)) {
                bouncing_ = true;
            }
            if (!near_view(sprite_.x, sprite_.y)) {
                give_back();
                g.scheduler().destroy(*this);
            }
            return;
        }
        // 08094E60
        if (!near_view(sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        throw_players(g, s);
        if (animate(g, sprite_) == 0) {
            sprite_.variant = 0;
            animate(g, sprite_);
            bouncing_ = false;
        }
        draw(g, sprite_);
    }

private:
    // 08094F2C
    bool throw_players(Game& g, StageState& s) {
        bool thrown = false;
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if (p.flags & (player_flag::dead | player_flag::no_movement)) {
                continue;
            }
            bool hammered = false;
            if (!touches_player(sprite_, x_, y_, p)) {
                hammered = hammer_hits(sprite_, x_, y_, p);
                if (!hammered) {
                    continue;
                }
            }
            land_player(g, p);
            p.flags = (p.flags & 0xFFFFFEF7u) | player_flag::in_air;
            p.special = 0;
            p.speed_y = s16(spawn_.record[4] * -0x40);
            if (hammered) {
                p.speed_y = s16(p.speed_y + s16((u32(s32(p.speed_y)) & 0x1FFFF) >> 1));
            }
            end_player_moves(g, p);
            p.state = (spawn_.record[3] & 1) ? 0x15 : 0x11;
            p.body->sprite.previous_variant = 0xFF;
            sprite_.variant = 1;
            sprite_.previous_variant = 0xFF;
            set_player_radius(g, p, 6, 0xE);
            g.sound().start(0xAA);
            thrown = true;
        }
        return thrown;
    }

    Game& game_;
    Sprite sprite_;   // +0C
    bool bouncing_{}; // the update is 08094E60
};

// 080757F8 (in Angel Island), 08075C04: a waterfall that holds players
// (the box from record +4 eight-pixel units below its place, record +5 x
// +6 units): a player inside not rising stops (Knuckles' glide ends),
// sinks B0h a frame and is pushed slightly by the direction buttons (left
// 4, right -4, else slowing by 1/20); a jump leaves it (270h up, spinning).
// It is drawn on each player inside. Its sound (C9h) starts when a player
// comes in and fades (music player 2) when one is outside. (The
// per-player flags +38/+3A are never set.)
class HoldingWaterfall final : public Interactive {
public:
    HoldingWaterfall(Game& g, const ObjectSpawn& o) : Interactive(o), game_(g) {
        const u16 animation = g.assets().u16(0x086C8C44 + u32(s32(g.stage().zone)) * 2);
        sprite_.start(g.sprite_tiles.allocate(0x12), animation, 0, 0, 0, 0x140, 0x2000);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 08076214

    void update(Game& g) override {
        StageState& s = g.stage();
        const s16 sx = s16(x_ - s.camera.x);
        const s16 sy = s16(y_ - s.camera.y);
        if (u32(s32(sx) + 0x80) > 0x1F0 || sy + 0x80 < 0 || sy > 0x120) {
            if (sounding_ != 0) {
                g.sound().fade_out(2, 4);
            }
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        animate(g, sprite_);
        const u8* record = spawn_.record;
        const s32 top = y_ + s8(record[4]) * 8;
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if (p.flags & player_flag::dead) {
                continue;
            }
            const s32 px = p.x >> 8, py = p.y >> 8;
            if (px < x_ || px > x_ + record[5] * 8 || py < top || py > top + record[6] * 8) {
                if (sounding_ == 1) {
                    g.sound().fade_out(2, 4);
                    sounding_ = 0;
                } else if (sounding_ != 0) {
                    --sounding_;
                }
                continue;
            }
            if (p.speed_y >= 0) {
                hold(g, s, p);
            }
            // (Where the player is now.)
            sprite_.x = s16((p.x >> 8) - s.camera.x);
            sprite_.y = s16((p.y >> 8) - s.camera.y + 10);
            draw(g, sprite_);
        }
    }

private:
    void hold(Game& g, StageState& s, Player& p) {
        if (sounding_ == 0) {
            sounding_ = 1;
            g.sound().start(0xC9);
        }
        if (p.pressed & s.jump_buttons) {
            end_player_moves(g, p);
            p.state = p.character == 3 ? 0x55 : 5;
            p.speed_y = s16(-divide(0x4E0, 2));
            return;
        }
        if (p.character == 2 && p.state == 0x3E) {
            // Knuckles gliding: the glide ends.
            p.state = 4;
            p.speed_x = p.ground_speed = 0;
            p.flags &= player_flag::facing_left;
        }
        if (!(p.flags & player_flag::in_air) ? (p.pressed & s.jump_buttons)
                                             : (p.pressed & s.other_buttons)) {
            p.speed_x = p.ground_speed = 0;
        }
        end_player_moves(g, p);
        p.flags &= ~(player_flag::in_air | player_flag::rolling);
        p.state = 4;
        p.y += 0xB0;
        if (p.speed_y > 0) {
            p.speed_y = 0;
        }
        if (p.held & 0x20) {
            p.ground_speed = s16(p.ground_speed + 4);
        } else if (p.held & 0x10) {
            p.ground_speed = s16(p.ground_speed - 4);
        } else {
            p.ground_speed = s16(p.ground_speed - divide(p.ground_speed, 0x14));
        }
    }

    Game& game_;
    Sprite sprite_; // +04
    u8 sounding_{}; // +3D
};

} // namespace

void create_holding_waterfall(Game& g, const ObjectSpawn& o) {
    g.scheduler().create<HoldingWaterfall>(g, o);
}

bool create_angel_island_object(Game& g, u32 creator, const ObjectSpawn& o) {
    switch (creator) {
    case 0x0807578C:
        g.scheduler().create<Launcher>(o);
        return true;
    case 0x0808B998:
        g.scheduler().create<SwingingHazard>(g, o);
        return true;
    case 0x0808C450:
        g.scheduler().create<HangingBar>(o);
        return true;
    case 0x0808F418:
        g.scheduler().create<Updraft>(o);
        return true;
    case 0x08090448:
        g.scheduler().create<BoulderSpawner>(o);
        return true;
    case 0x0809215C: { // record +3 picks the picture
        const u8 kind = o.record[3];
        g.scheduler().create<Decoration>(g, o,
                                         kind == 0   ? 0x234
                                         : kind == 1 ? 0x236
                                                     : 0x235,
                                         kind <= 1 ? 1 : 2, 0x480);
        return true;
    }
    case 0x0808EDD0: { // behind the level's front layer
        const s8 zone = g.stage().zone;
        if ((zone >> 1) == 5 || zone == 0x11) {
            const bool first = o.record[3] == 0;
            g.scheduler().create<Decoration>(g, o, first ? 0x200 : 0x1FF, first ? 8 : 4, 0x700);
        } else {
            g.scheduler().create<Decoration>(g, o, 0x22E, 0xF, 0x700);
        }
        return true;
    }
    case 0x0808E484:
        g.scheduler().create<Cart>(g, o);
        return true;
    case 0x0808A608:
        g.scheduler().create<Lift>(g, o);
        return true;
    case 0x08076A78:
        create_dash_panel(g, o, 0xC, 0x23B, 0);
        return true;
    case 0x0807696C:
        create_dash_panel(g, o, 8, 0x1D8, 0);
        return true;
    case 0x08094CD8:
        g.scheduler().create<GroundSpring>(g, o);
        return true;
    default:
        return false;
    }
}

bool create_angel_island_enemy(Game& g, u32 creator, const ObjectSpawn& o) {
    switch (creator) {
    case 0x0806E698:
        // Not made in Angel Island while the save's byte +18 is set.
        if ((g.stage().zone >> 1) != 4 || g.save().unknown_5178 == 0) {
            g.scheduler().create<Lunger>(g, o);
        }
        return true;
    case 0x0806EDE8:
        g.scheduler().create<Serpent>(g, o);
        return true;
    default:
        return false;
    }
}

} // namespace sonic_advance_gba
