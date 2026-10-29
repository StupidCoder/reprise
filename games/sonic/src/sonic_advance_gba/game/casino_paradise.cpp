// Casino Paradise's objects (zone 3).

#include "sonic_advance_gba/game/casino_paradise.hpp"
#include "sonic_advance_gba/game/collision.hpp"
#include "sonic_advance_gba/game/game.hpp"
#include "sonic_advance_gba/game/interactive.hpp"
#include "sonic_advance_gba/game/math.hpp"
#include "sonic_advance_gba/game/object_base.hpp"
#include "sonic_advance_gba/game/object_util.hpp"
#include "sonic_advance_gba/game/player_effects.hpp"
#include "sonic_advance_gba/game/solid.hpp"
#include "sonic_advance_gba/game/special_stage.hpp"
#include "sonic_advance_gba/game/stage.hpp"
#include <algorithm>
#include <array>

namespace sonic_advance_gba {

namespace {

// 0807EB3C, 0807EC50: a bounce pad (record +3 its picture: 0-2): a player
// touching it (not carried by an object) is thrown straight up and the
// pad plays its bounce for 21h frames (0807EF2C), then is made anew.
class BouncePadCP final : public Interactive {
public:
    BouncePadCP(Game& g, const ObjectSpawn& o) : Interactive(o), game_(g) {
        const u8 picture = o.record[3];
        const u16 animation = picture == 0   ? 0x1DF
                              : picture == 1 ? 0x1E0
                              : picture == 2 ? 0x1E1
                                             : 0;
        sprite_.start(g.sprite_tiles.allocate(0x12), animation, 0, x_, y_, 0x480, 0x2000);
        animate(g, sprite_);
    }

    // 0807EFE0
    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); }

    void update(Game& g) override {
        StageState& s = g.stage();
        sprite_.x = s16(x_ - s.camera.x);
        sprite_.y = s16(y_ - s.camera.y);
        if (!near_view(sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        if (bouncing_) {
            // 0807EF2C
            if (--timer_ == 0) {
                give_back();
                g.scheduler().destroy(*this);
                return;
            }
            animate(g, sprite_);
            draw(g, sprite_);
            return;
        }
        if (g.game_mode >= 2) {
            throw Unrecovered("bounce pads in multiplayer (0807EC50)");
        }
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if (!touches_player(sprite_, x_, y_, p) || (p.flags & player_flag::no_movement)) {
                continue;
            }
            end_player_moves(g, p);
            p.flags = (p.flags & 0xFFFFFEE3u) | player_flag::in_air;
            set_player_radius(g, p, 6, 0xE);
            p.radius_x = 6;
            p.radius_y = 0xE;
            p.speed_y = -0x600;
            p.state = 0x15;
            p.body->sprite.previous_variant = 0xFF;
            timer_ = 0x21;
            sprite_.previous_variant = 0xFF;
            sprite_.variant = 1;
            set_player_radius(g, p, 6, 0xE);
            p.radius_x = 6;
            p.radius_y = 0xE;
            g.sound().start(0xBD);
            bouncing_ = true;
        }
        animate(g, sprite_);
        draw(g, sprite_);
    }

private:
    Game& game_;
    Sprite sprite_;   // +0C
    u8 timer_{};      // +40
    bool bouncing_{}; // the update is 0807EF2C
};

// A point offset that swings along a sine with the clock (as the
// swinging platforms): record +5 the horizontal reach, +6 the vertical (in
// 8 pixels; the larger is used), +3/+4 negative: half a swing later.
struct Swing {
    explicit Swing(const u8* r) {
        if (r[6] < r[5]) {
            speed_x = 4;
            phase = s8(r[3]) < 0 ? 0x80 : 0;
        } else {
            speed_y = 4;
            phase = s8(r[4]) < 0 ? 0x80 : 0;
        }
        if (r[5] == 0 && r[6] == 0) {
            speed_x = 0;
        }
    }

    void step(const Assets& a, const u8* r, u32 ticks) {
        if (speed_x != 0) {
            x = (r[5] * 0x800 * s32(sine(a, (speed_x * ((ticks + phase) & 0xFF)) & 0x3FF))) >> 14;
        }
        if (speed_y != 0) {
            y = (r[6] * 0x800 * s32(sine(a, (speed_y * ((ticks + phase) & 0xFF)) & 0x3FF))) >> 14;
        }
    }

    // 080786CC: swinging only sideways (record +5 the reach; +3 negative:
    // half a swing later).
    static Swing sideways(const u8* r) {
        Swing w(r);
        w.speed_x = (r[5] == 0 && r[6] == 0) ? 0 : 4;
        w.speed_y = 0;
        w.phase = s8(r[3]) < 0 ? 0x80 : 0;
        return w;
    }

    u16 phase{};              // +3C
    s32 x{}, y{};             // +40, +44 8.8
    s16 speed_x{}, speed_y{}; // +48, +4A
};

// 08078E2C, 08078FA0: a bumper, perhaps moving on a swing: a player
// touching it (not carried by an object) is knocked away from its middle
// at 4.5 pixels a frame, curled up, for 10 points (080792CC); it wobbles for
// 9 frames (0807911C). Drawn with fixed tiles.
class Bumper final : public Interactive {
public:
    Bumper(Game& g, const ObjectSpawn& o) : Interactive(o), swing_(o.record) {
        sprite_.start(0x12E20, 0x1E5, 0, x_, y_, 0x480, 0x2000);
        animate(g, sprite_);
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        const Assets& a = g.assets();
        swing_.step(a, spawn_.record, s.ticks);
        if (wobbling_) {
            // 0807911C (the last frame of the wobble still runs it)
            if (++wobble_ > 8) {
                wobbling_ = false;
            }
            touch(g, s);
            // (Wobble steps past 8 read beyond the sine table, as the
            // original does.)
            sprite_.x = s16(x_ - s.camera.x + s16(u32(swing_.x) >> 8) -
                            (sine(a, wobble_ * 0x80u + 0x100) >> 12));
            sprite_.y =
                s16(y_ - s.camera.y + s16(u32(swing_.y) >> 8) + (sine(a, wobble_ * 0x80u) >> 12));
        } else {
            sprite_.x = s16(x_ - s.camera.x + s16(u32(swing_.x) >> 8));
            sprite_.y = s16(y_ - s.camera.y + s16(u32(swing_.y) >> 8));
            if (touch(g, s)) {
                wobbling_ = true;
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
    // 080792CC: true when a player was knocked away.
    bool touch(Game& g, StageState& s) {
        const Assets& a = g.assets();
        const s16 bx = s16(x_ + (swing_.x >> 8)), by = s16(y_ + (swing_.y >> 8));
        bool hit = false;
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if (!touches_player(sprite_, bx, by, p) || (p.flags & player_flag::no_movement)) {
                continue;
            }
            if ((p.character == 1 || p.character == 2) && p.special != 0) {
                end_player_moves(g, p);
                p.state = 5;
            }
            p.flags = (p.flags & 0xFFFFFEE7u) | player_flag::in_air | player_flag::rolling;
            p.radius_x = 6;
            p.radius_y = 9;
            const s16 angle = octant_angle(a, s16((p.y >> 8) - y_ - (swing_.y >> 8)),
                                           s16((p.x >> 8) - x_ - (swing_.x >> 8)));
            p.speed_x = s16(divide(sine(a, unsigned(angle + 0x100)), 0xF));
            p.ground_speed = p.speed_x;
            p.speed_y = s16(divide(sine(a, unsigned(angle)), 0xF));
            if (p.speed_x == 0) {
                p.speed_x = 8;
            }
            const u32 before = s.score;
            s.score += 10;
            if (divide(s32(s.score), 50000) != divide(s32(before), 50000) && g.game_mode == 0) {
                ++s.lives;
                s.music_one_up = 0x10;
            }
            g.sound().start(0xB7);
            wobble_ = 0;
            hit = true;
        }
        return hit;
    }

    Sprite sprite_; // +0C
    Swing swing_;
    u8 wobble_{};     // +4E
    bool wobbling_{}; // the update is 0807911C
};

// 080786CC, 080787FC: a bumper that may slide from side to side (record
// +5 the reach). A player touching it (not carried by an object) is
// knocked away curled up in one of eight directions by where it touched
// (08078AAC; the straight sideways ones keep the vertical speed, no
// points); it wobbles for 9 frames (0807893C).
class SlidingBumper final : public Interactive {
public:
    SlidingBumper(Game& g, const ObjectSpawn& o)
        : Interactive(o), game_(g), swing_(Swing::sideways(o.record)) {
        sprite_.start(g.sprite_tiles.allocate(0x1E), 0x1E4, 0, x_, y_, 0x480, 0x2000);
        animate(g, sprite_);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 08078E18

    void update(Game& g) override {
        StageState& s = g.stage();
        const Assets& a = g.assets();
        if (wobbling_) {
            if (++wobble_ > 8) {
                wobbling_ = false;
            }
            swing_.step(a, spawn_.record, s.ticks);
            touch(g, s);
            // (Wobble steps past 8 read beyond the sine table, as the
            // original does.)
            sprite_.x = s16(x_ - s.camera.x + s16(u32(swing_.x) >> 8) -
                            (sine(a, wobble_ * 0x80u + 0x100) >> 12));
            sprite_.y =
                s16(y_ - s.camera.y + s16(u32(swing_.y) >> 8) + (sine(a, wobble_ * 0x80u) >> 12));
        } else {
            swing_.step(a, spawn_.record, s.ticks);
            sprite_.x = s16(x_ - s.camera.x + s16(u32(swing_.x) >> 8));
            sprite_.y = s16(y_ - s.camera.y + s16(u32(swing_.y) >> 8));
            if (touch(g, s)) {
                wobbling_ = true;
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
    // 08078AAC: true when a player was knocked away.
    bool touch(Game& g, StageState& s) {
        const Assets& a = g.assets();
        const s16 bx = s16(x_ + (swing_.x >> 8)), by = s16(y_ + (swing_.y >> 8));
        bool hit = false;
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if (!touches_player(sprite_, bx, by, p) || (p.flags & player_flag::no_movement)) {
                continue;
            }
            if ((p.character == 1 || p.character == 2) && p.special != 0) {
                end_player_moves(g, p);
                p.state = 5;
            }
            p.flags = (p.flags & 0xFFFFFEE7u) | player_flag::in_air | player_flag::rolling;
            p.radius_x = 6;
            p.radius_y = 9;
            const u32 angle = u32(s32(octant_angle(a, s16((p.y >> 8) - by), s16((p.x >> 8) - bx))));
            constexpr s16 slant_x = 0x297, slant_y = 0x5D9, straight = 0x666;
            if (angle < 0x40 || angle > 0x3C0) {
                p.speed_x = straight;
            } else if (angle >= 0x1C1 && angle <= 0x249) {
                p.speed_x = -straight;
            } else if (angle <= 0x100) {
                p.speed_x = slant_x;
                p.speed_y = slant_y;
            } else if (angle <= 0x1C0) {
                p.speed_x = -slant_x;
                p.speed_y = slant_y;
            } else if (angle <= 0x300) {
                p.speed_x = -slant_x;
                p.speed_y = -slant_y;
            } else {
                p.speed_x = slant_x;
                p.speed_y = -slant_y;
            }
            g.sound().start(0xB7);
            wobble_ = 0;
            hit = true;
        }
        return hit;
    }

    Game& game_;
    Sprite sprite_; // +0C
    Swing swing_;
    u8 wobble_{};     // +4E
    bool wobbling_{}; // the update is 0807893C
};

// 08079650, 080797A0: a bumper going round a circle (record +5 or +6,
// the larger: the radius in 8 pixels; +4 the signed speed, +3 the start
// angle in eighths). A player touching it is knocked away from it at 4.5
// pixels a frame, curled up, for 10 points (08079B80; the direction is
// taken from the middle with the offset 8 times too large, as the
// original does); it wobbles for 9 frames (0807996C). Drawn with fixed
// tiles.
class OrbitingBumper final : public Interactive {
public:
    OrbitingBumper(Game& g, const ObjectSpawn& o)
        : Interactive(o), phase_(u16((o.record[3] & 7) << 8)), speed_(s8(o.record[4])),
          forward_(o.record[6] < o.record[5]), radius_(std::max(o.record[5], o.record[6])) {
        sprite_.start(0x12E20, 0x1E5, 0, x_, y_, 0x480, 0x2000);
        animate(g, sprite_);
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        const Assets& a = g.assets();
        if (wobbling_) {
            // (The last frame of the wobble still runs it.)
            if (++wobble_ > 8) {
                wobbling_ = false;
            }
            move(a, s.ticks);
            place(g, s, true);
            touch(g, s);
        } else {
            move(a, s.ticks);
            place(g, s, false);
            if (touch(g, s)) {
                wobbling_ = true;
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
    void move(const Assets& a, u32 ticks) {
        const s32 k = forward_ ? speed_ : -speed_;
        const unsigned angle = ((u32(k) * ticks + phase_) & 0x7FF) >> 1;
        x_offset_ = sine(a, angle) * radius_;
        y_offset_ = sine(a, angle + 0x100) * radius_;
    }

    void place(Game& g, const StageState& s, bool wobble) {
        const Assets& a = g.assets();
        sprite_.x = s16(x_ - s.camera.x + (x_offset_ >> 11));
        sprite_.y = s16(y_ - s.camera.y + (y_offset_ >> 11));
        if (wobble) {
            // (Wobble steps past 8 read beyond the sine table, as the
            // original does.)
            sprite_.x = s16(sprite_.x - (sine(a, wobble_ * 0x80u + 0x100) >> 12));
            sprite_.y = s16(sprite_.y + (sine(a, wobble_ * 0x80u) >> 12));
        }
    }

    // 08079B80: true when a player was knocked away.
    bool touch(Game& g, StageState& s) {
        const Assets& a = g.assets();
        bool hit = false;
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if ((p.flags & player_flag::dead) ||
                !touches_player(sprite_, s16(x_ + (x_offset_ >> 11)), s16(y_ + (y_offset_ >> 11)),
                                p)) {
                continue;
            }
            if ((p.character == 1 || p.character == 2) && p.special != 0) {
                end_player_moves(g, p);
                p.state = 5;
            }
            p.flags = (p.flags & 0xFFFFFEE7u) | player_flag::in_air | player_flag::rolling;
            p.radius_x = 6;
            p.radius_y = 9;
            const s16 angle = octant_angle(a, s16((p.y >> 8) - y_ - (y_offset_ >> 8)),
                                           s16((p.x >> 8) - x_ - (x_offset_ >> 8)));
            p.speed_x = s16(divide(sine(a, unsigned(angle + 0x100)), 0xF));
            p.ground_speed = p.speed_x;
            p.speed_y = s16(divide(sine(a, unsigned(angle)), 0xF));
            if (p.speed_x == 0) {
                p.speed_x = 8;
            }
            const u32 before = s.score;
            s.score += 10;
            if (divide(s32(s.score), 50000) != divide(s32(before), 50000) && g.game_mode == 0) {
                ++s.lives;
                s.music_one_up = 0x10;
            }
            g.sound().start(0xB7);
            wobble_ = 0;
            hit = true;
        }
        return hit;
    }

    Sprite sprite_;               // +0C
    u16 phase_;                   // +3C
    s32 x_offset_{}, y_offset_{}; // +40, +44 (11 fraction bits)
    s8 speed_;                    // record +4
    bool forward_;                // +4E
    u8 radius_;                   // +4F
    u8 wobble_{};                 // +51
    bool wobbling_{};             // the update is 0807996C
};

// Fixed-shape bumpers: a half-round one, 08079ED8, 0807A26C (kind 51: the
// dome up when record +3 is nonzero, else down) and 0807A000, 0807A4D0
// (kind 52: the dome right when +3 is nonzero, else left), and a
// triangular one, 0807A12C, 0807A730 (kind 53: the slant faces up-right;
// record +3 zero mirrors it, +4 zero flips it). A player touching it (not
// carried by an object; on the corner bumper, only on the slant side) is
// knocked away curled up (0807A984, 0807ADA8, 0807B13C): off a dome's
// slopes diagonally and off its flat side straight away keeping the other
// speed; off the slant diagonally. It wobbles for 9 frames (0807A368,
// 0807A5CC, 0807A81C). Drawn with fixed tiles.
class ShapedBumper final : public Interactive {
public:
    enum class Shape { dome, side_dome, corner };

    ShapedBumper(Game& g, const ObjectSpawn& o, Shape shape) : Interactive(o), shape_(shape) {
        if (shape != Shape::side_dome && (g.game_mode == 3 || g.game_mode == 5)) {
            throw Unrecovered("a bumper in a linked game (0807A984, 0807B13C)");
        }
        const bool turned = o.record[3] == 0;
        switch (shape) {
        case Shape::dome:
            sprite_.start(0x12B20, 0x1DC, 0, x_, y_, 0x480, turned ? 0x2800 : 0x2000);
            break;
        case Shape::side_dome:
            sprite_.start(0x12CA0, 0x1DD, 0, x_, y_, 0x480, turned ? 0x2400 : 0x2000);
            break;
        case Shape::corner:
            sprite_.start(0x126A0, 0x1DB, 0, x_, y_, 0x480,
                          (turned ? 0x2400 : 0x2000) |
                              (o.record[4] == 0 ? sprite_flag::flip_y : 0));
            break;
        }
        animate(g, sprite_);
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        const Assets& a = g.assets();
        sprite_.x = s16(x_ - s.camera.x);
        sprite_.y = s16(y_ - s.camera.y);
        if (wobbling_) {
            // (The last frame of the wobble still runs it.)
            if (++wobble_ > 8) {
                wobbling_ = false;
            }
            // The side dome's wobble (0807A5CC) meets the players before
            // it is placed, so a new hit restarts it at once.
            if (shape_ == Shape::side_dome) {
                touch(g, s);
            }
            // (Wobble steps past 8 read beyond the sine table, as the
            // original does.)
            const s16 dx = s16(sine(a, wobble_ * 0x80u + 0x100) >> 12);
            sprite_.x =
                s16((sprite_.flags & sprite_flag::flip_x) ? sprite_.x + dx : sprite_.x - dx);
            sprite_.y = s16(sprite_.y + (sine(a, wobble_ * 0x80u) >> 12));
            if (shape_ != Shape::side_dome) {
                touch(g, s);
            }
        } else if (touch(g, s)) {
            wobbling_ = true;
        }
        if (out_of_range(s, x_, y_, sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        draw(g, sprite_);
    }

private:
    // 0807A984 / 0807ADA8 / 0807B13C: true when a player was knocked away.
    bool touch(Game& g, StageState& s) {
        const Assets& a = g.assets();
        bool hit = false;
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if ((p.flags & (player_flag::dead | player_flag::no_movement)) ||
                !touches_player(sprite_, x_, y_, p) || !on_slant(p)) {
                continue;
            }
            if ((p.character == 1 || p.character == 2) && p.special != 0) {
                end_player_moves(g, p);
                p.state = 5;
            }
            p.flags = (p.flags & 0xFFFFFEE7u) | player_flag::in_air | player_flag::rolling;
            p.radius_x = 6;
            p.radius_y = 9;
            knock(a, p);
            g.sound().start(0xB8);
            wobble_ = 0;
            hit = true;
        }
        return hit;
    }

    // The corner bumper only knocks a player on its slant side (the line
    // 60 pixels from the corner).
    bool on_slant(const Player& p) const {
        if (shape_ != Shape::corner) {
            return true;
        }
        const s32 px = p.x >> 8, dy = (p.y >> 8) - y_;
        const s32 h = (sprite_.flags & sprite_flag::flip_x) ? x_ - px : px - x_;
        return (sprite_.flags & sprite_flag::flip_y) ? dy < 0x41 - h : dy > h - 0x3C;
    }

    void knock(const Assets& a, Player& p) const {
        constexpr s16 slant = 0x297, steep = 0x5D9, straight = 0x666, corner = 0x4B0;
        const bool flip_x = sprite_.flags & sprite_flag::flip_x,
                   flip_y = sprite_.flags & sprite_flag::flip_y;
        if (shape_ == Shape::corner) {
            p.speed_x = flip_x ? -corner : corner;
            p.speed_y = flip_y ? corner : -corner;
            return;
        }
        // The angle from the middle (0 to the right, 100h below).
        const s16 angle = octant_angle(a, s16((p.y >> 8) - y_), s16((p.x >> 8) - x_));
        if (shape_ == Shape::dome) {
            const u16 u = u16(angle);
            if (!flip_y) {
                if (u < 0x15 || u >= 0x301) {
                    p.speed_x = slant;
                    p.speed_y = -steep;
                } else if (u < 0x1EF) {
                    p.speed_y = straight;
                } else {
                    p.speed_x = -slant;
                    p.speed_y = -steep;
                }
            } else if (u < 0x100) {
                p.speed_x = slant;
                p.speed_y = steep;
            } else if (u < 0x200) {
                p.speed_x = -slant;
                p.speed_y = steep;
            } else {
                p.speed_y = -straight;
            }
            return;
        }
        const u32 u = u32(s32(angle));
        if (!flip_x) {
            if (u >= 0x104 && u <= 0x2FC) {
                p.speed_x = -straight;
            } else {
                p.speed_x = steep;
                p.speed_y = u < 0x201 ? slant : -slant;
            }
        } else if (u >= 0xFD && u <= 0x303) {
            p.speed_x = -steep;
            p.speed_y = u < 0x201 ? slant : -slant;
        } else {
            p.speed_x = straight;
        }
    }

    Sprite sprite_; // +0C
    Shape shape_;
    u8 wobble_{};     // +4C
    bool wobbling_{}; // the update is 0807A368 / 0807A5CC / 0807A81C
};

// 0807B9A8, 0807BACC: a pinball flipper (pointing right; record +3 zero
// mirrors it). A player landing on it is held curled up on its slope
// (rolling down at 50h, the buttons ignored); pressing jump flips it,
// throwing the player up and along at speeds that grow with the distance
// from the pivot (sound B7h), and it stays up for 8 frames (0807C830).
// Knuckles gliding or climbing only stands on it. From below it pushes
// players down.
class Flipper final : public Interactive {
public:
    Flipper(Game& g, const ObjectSpawn& o) : Interactive(o), game_(g) {
        sprite_.start(g.sprite_tiles.allocate(0x18), 0x1DE, 0, x_, y_, 0x480,
                      o.record[3] == 0 ? 0x2400 : 0x2000);
        animate(g, sprite_);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 0807CE84

    void update(Game& g) override {
        StageState& s = g.stage();
        sprite_.x = s16(x_ - s.camera.x);
        sprite_.y = s16(y_ - s.camera.y);
        if (up_ > 0) {
            // 0807C830
            if (!near_view(sprite_.x, sprite_.y)) {
                give_back();
                g.scheduler().destroy(*this);
                return;
            }
            if (--up_ == 0) {
                show(0);
            }
            animate(g, sprite_);
            draw(g, sprite_);
            return;
        }
        for (unsigned n = 0; n < s.players; ++n) {
            step(g, s, s.player[n]);
        }
        if (near_view(sprite_.x, sprite_.y)) {
            draw(g, sprite_);
            return;
        }
        for (unsigned n = 0; n < s.players; ++n) {
            if (riding_ & (1u << n)) {
                release(s, s.player[n]);
            }
        }
        give_back();
        g.scheduler().destroy(*this);
    }

private:
    void show(u8 variant) {
        sprite_.animation = 0x1DE;
        sprite_.variant = variant;
        sprite_.previous_variant = 0xFF;
    }

    static void release(const StageState& s, Player& p) {
        p.flags &= ~player_flag::frozen;
        p.held |= s.jump_buttons | s.other_buttons;
    }

    // Curled up, the moves ended (the size stays where the feet are).
    static void curl(Game& g, Player& p) {
        p.state = 5;
        set_player_radius(g, p, 6, 9);
    }

    void step(Game& g, StageState& s, Player& p) {
        const bool mirrored = sprite_.flags & sprite_flag::flip_x;
        const u8 bit = u8(1u << p.index);
        const bool riding = riding_ & bit;
        // The distance from the pivot along the flipper.
        const s32 along = mirrored ? x_ - (p.x >> 8) : (p.x >> 8) - x_;
        if (!touches_player(sprite_, x_, y_, p)) {
            if (riding) {
                release(s, p);
                p.speed_x = mirrored ? -0x100 : 0x100;
            }
            riding_ &= u8(~bit);
            return;
        }
        if (p.character == 2 && p.state >= 0x46 && p.state <= 0x4B) {
            // Knuckles gliding or climbing.
            if (p.state == 0x49) {
                return;
            }
            collide_solid(g, sprite_, x_, y_, p);
            if ((p.flags & player_flag::on_object) && p.standing_on == &sprite_) {
                end_player_moves(g, p);
                p.state = 5;
                p.flags = (p.flags & ~player_flag::on_object) | player_flag::in_air |
                          player_flag::rolling;
                if (mirrored) {
                    p.flags |= player_flag::facing_left;
                } else {
                    p.flags &= ~player_flag::facing_left;
                }
            }
            return;
        }
        const s32 base = y_ * 0x100;
        const bool standing = (p.flags & player_flag::on_object) && p.standing_on == &sprite_;
        if ((p.speed_y < 0 && !standing) || p.y >= base + 0x1300 ||
            !(p.flags & player_flag::in_air)) {
            // Below it.
            if (p.y > base) {
                if (p.y < base + 0x1800) {
                    p.speed_y = 10;
                    p.speed_x = 0;
                    p.ground_speed = 0;
                    p.y = base + 0x1800;
                }
            } else {
                p.flags |= player_flag::in_air;
            }
            return;
        }
        if ((g.controller().pressed_once & s.jump_buttons) && riding) {
            flip(g, s, p, along, mirrored);
            riding_ &= u8(~bit);
            return;
        }
        // The slope's surface under the player.
        const s32 surface = (y_ + (s16(along) >> 1) - 0x11) * 0x100;
        const s16 roll = mirrored ? -0x50 : 0x50;
        if (p.y > surface) {
            // Lands on it.
            p.y = surface + 1;
            p.speed_x = roll;
            p.speed_y = 0;
            riding_ |= bit;
            p.angle = 0;
            p.held = 0;
            p.pressed = 0;
            end_player_moves(g, p);
            p.flags |= player_flag::frozen | player_flag::rolling;
            curl(g, p);
        } else if (!riding) {
            release(s, p);
        } else {
            // Above the surface while held: kept on it.
            if (mirrored) {
                end_player_moves(g, p);
                p.flags = (p.flags & ~(player_flag::jump_cancel | player_flag::jumped)) |
                          player_flag::in_air | player_flag::rolling;
            } else {
                p.flags = (p.flags & ~(player_flag::jump_cancel | player_flag::jumped)) |
                          player_flag::in_air | player_flag::rolling;
                end_player_moves(g, p);
            }
            curl(g, p);
            p.y = surface + 1;
            p.speed_x = roll;
            p.speed_y = 0;
            p.held = 0;
            p.pressed = 0;
            p.flags |= player_flag::frozen;
        }
    }

    void flip(Game& g, const StageState& s, Player& p, s32 along, bool mirrored) {
        const auto& c = g.controller();
        p.flags =
            (p.flags & ~(player_flag::on_object | player_flag::jump_cancel | player_flag::jumped)) |
            player_flag::in_air | player_flag::rolling;
        if (c.held & 0x20) {
            p.flags |= player_flag::facing_left;
        } else if (c.held & 0x10) {
            p.flags &= ~player_flag::facing_left;
        }
        p.speed_y = s16(-s16(along * 16) - 0x7C0);
        const s16 sideways = s16(along * 32);
        p.speed_x = mirrored ? s16(-sideways) : sideways;
        p.ground_speed = p.speed_x;
        end_player_moves(g, p);
        p.state = 5;
        up_ = 8;
        set_player_radius(g, p, 6, 9);
        release(s, p);
        show(1);
        g.sound().start(0xB7);
    }

    Game& game_;
    Sprite sprite_; // +0C
    u8 up_{};       // +40 frames left up (the update is 0807C830)
    u8 riding_{};   // +41 a bit per player held on it
};

// 0807C8FC, 0807CA04: a flipper lying flat (the pinball flipper's
// picture). It is solid to stand on; a player running into it on the
// ground is kicked away from it at 0A00h (the flipper turning that way,
// sound B7h), and it stays up for 8 frames (0807CDB8).
class FlatFlipper final : public Interactive {
public:
    FlatFlipper(Game& g, const ObjectSpawn& o) : Interactive(o), game_(g) {
        sprite_.start(g.sprite_tiles.allocate(0x1E), 0x1DE, 2, x_, y_, 0x480, 0x2000);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 0807CE84

    void update(Game& g) override {
        StageState& s = g.stage();
        sprite_.x = s16(x_ - s.camera.x);
        sprite_.y = s16(y_ - s.camera.y);
        if (up_ > 0) {
            // 0807CDB8
            if (!near_view(sprite_.x, sprite_.y)) {
                give_back();
                g.scheduler().destroy(*this);
                return;
            }
            if (--up_ == 0) {
                show(2);
            }
            animate(g, sprite_);
            draw(g, sprite_);
            return;
        }
        for (unsigned n = 0; n < s.players; ++n) {
            step(g, s.player[n]);
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
    void show(u8 variant) {
        sprite_.animation = 0x1DE;
        sprite_.variant = variant;
        sprite_.previous_variant = 0xFF;
    }

    void step(Game& g, Player& p) {
        const u8 bit = u8(1u << p.index);
        const auto get_off = [&] {
            if (standing_ & bit) {
                standing_ &= u8(~bit);
                p.flags &= ~player_flag::on_object;
            }
        };
        if (p.flags & player_flag::dead) {
            get_off();
            return;
        }
        if (!(p.flags & player_flag::in_air) && !(standing_ & bit)) {
            if (p.y <= y_ * 0x100) {
                return;
            }
            const bool left = p.x < x_ * 0x100;
            if (!touches_player(sprite_, s16(left ? x_ - 6 : x_ + 7), y_, p)) {
                return;
            }
            const s16 kick = left ? -0xA00 : 0xA00;
            if (left) {
                p.flags |= player_flag::facing_left;
                sprite_.flags |= sprite_flag::flip_x;
            } else {
                p.flags &= ~player_flag::facing_left;
                sprite_.flags &= ~sprite_flag::flip_x;
            }
            p.speed_x = kick;
            p.ground_speed = kick;
            up_ = 8;
            show(3);
            g.sound().start(0xB7);
            return;
        }
        if (!(collide_solid(g, sprite_, x_, y_, p) & solid::on_top)) {
            get_off();
            return;
        }
        standing_ |= bit;
        if (!touches_player(sprite_, x_, y_, p) && (p.flags & player_flag::on_object) &&
            p.standing_on == &sprite_) {
            p.flags &= ~player_flag::on_object;
        }
    }

    Game& game_;
    Sprite sprite_; // +0C
    u8 up_{};       // +40 frames left up (the update is 0807CDB8)
    u8 standing_{}; // +41 a bit per player standing on it
};

// 0807CE98, 0807D0F0: a solid block swinging along a sine (as Swing; the
// picture by zone and record +3/+4). A player on top rides along, kept
// out of walls; one pressing against a side may push into it (but not move
// it); stepping off its side corner puts the player beside it
// (0807E61C). A player caught between it and a wall, floor or ceiling as it
// moves is crushed (dead).
class SwingingBlock final : public Interactive {
public:
    SwingingBlock(Game& g, const ObjectSpawn& o) : Interactive(o), game_(g), swing_(o.record) {
        const u8* r = o.record;
        const u8 picture = r[6] < r[5] ? r[4] : r[3]; // +50
        const s8 act = g.stage().zone;
        const s32 zone = act >> 1;
        u32 tiles = 0x24;
        u16 animation = 0x1EE;
        if (zone != 2 && act != 0x10) {
            if (zone == 5 || act == 0x11) {
                const bool first = (act & 1) == 0 && act != 0x11;
                tiles = picture == 0 ? 0x24 : 0x28;
                animation = u16((first ? 0x206 : 0x257) + (picture == 0 ? 0 : 1));
            } else if (zone == 4) {
                tiles = 0x28;
                animation = picture == 0 ? 0x218 : 0x219;
            } else if (zone == 3) {
                animation = 0x20C;
            }
        }
        sprite_.start(g.sprite_tiles.allocate(tiles), animation, 0, x_, y_, 0x480, 0x2000);
        animate(g, sprite_);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 0807E778

    void update(Game& g) override {
        StageState& s = g.stage();
        const s32 old_x = swing_.x, old_y = swing_.y;
        swing_.step(g.assets(), spawn_.record, s.ticks);
        const s32 dx = swing_.x - old_x, dy = swing_.y - old_y;
        const s16 bx = s16(x_ + (swing_.x >> 8)), by = s16(y_ + (swing_.y >> 8));
        sprite_.x = s16(x_ - s.camera.x + (swing_.x >> 8));
        sprite_.y = s16(y_ - s.camera.y + (swing_.y >> 8));
        u32 met = 0; // (kept from one player to the next, as the original)
        for (unsigned n = 0; n < s.players; ++n) {
            step(g, s.player[n], bx, by, dx, dy, met);
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
    static void crush(Player& p) {
        p.flags = (p.flags & ~player_flag::pushing) | player_flag::dead | player_flag::no_movement;
    }

    // The distance to the map's surface (negative: inside it).
    s32 wall(const StageState& s, s32 x, s32 y, s32 step, const Player& p) const {
        return probe(game_.assets(), s.map, x, y, p.layer, step, nullptr, Sensor::wall);
    }

    s32 floor(const StageState& s, s32 y, s32 x, s32 step, const Player& p) const {
        return probe(game_.assets(), s.map, y, x, p.layer, step, nullptr, Sensor::floor);
    }

    // The nearer of the surfaces above (step -8) or below the player's two
    // sides.
    s32 floors(const StageState& s, s32 y, s32 step, const Player& p) const {
        const s32 px = p.x >> 8;
        return std::min(floor(s, y, px - p.radius_x, step, p),
                        floor(s, y, px + p.radius_x, step, p));
    }

    void step(Game& g, Player& p, s16 bx, s16 by, s32 dx, s32 dy, u32& met) {
        const StageState& s = g.stage();
        if (p.flags & (player_flag::dead | player_flag::no_movement)) {
            return;
        }
        const u8 bit = u8(1u << p.index);
        const s32 start_x = p.x;
        if (riding_ & bit) {
            p.x += dx;
            p.y += dy + 0x100;
            const s32 py = p.y >> 8;
            if (dx > 0) {
                const s32 wx = (p.x >> 8) + 2 + p.radius_x;
                const s32 d = std::min(wall(s, wx, py, 8, p), wall(s, wx, py - p.radius_y, 8, p));
                if (d < 0) {
                    p.x += d * 0x100;
                }
            } else {
                const s32 wx = (p.x >> 8) - 2 - p.radius_x;
                const s32 d = std::min(wall(s, wx, py, -8, p), wall(s, wx, py - p.radius_y, -8, p));
                if (d < 0) {
                    p.x -= d * 0x100;
                }
            }
            if (floors(s, (p.y >> 8) - p.radius_y, -8, p) < 0) {
                // Carried into a ceiling.
                if (swing_.speed_y != 0) {
                    p.x = start_x;
                    crush(p);
                }
            } else if (!touches_player(sprite_, bx, by, p)) {
                riding_ &= u8(~bit);
            }
        } else {
            const s32 start_y = p.y;
            if (floors(s, (p.y >> 8) - p.radius_y, -8, p) < 0) {
                step_aside(s, bx, by, p);
            } else {
                met = collide_solid(g, sprite_, bx, by, p);
            }
            if (!(met & (solid::from_below | solid::from_left | solid::from_right | 0x100000))) {
                if ((touched_ & bit) && p.state == 0xE) {
                    // Was pushing it.
                    touched_ &= u8(~bit);
                    p.ground_speed = 0;
                    end_player_moves(g, p);
                    p.flags &= ~player_flag::pushing;
                    if (p.state == 0xE) {
                        p.state = (p.flags & player_flag::in_air) ? 5 : 4;
                    }
                }
            } else {
                touched_ |= bit;
                const s32 px = p.x >> 8, py = p.y >> 8;
                const bool on_ground = !(p.flags & player_flag::in_air);
                const s32 bottom = by + sprite_.hitboxes[0].bottom;
                if (met & solid::from_right) {
                    if (swing_.speed_x != 0) {
                        if (wall(s, px + p.radius_x, py, 8, p) <= 0) {
                            crush(p);
                        } else if ((p.held & 0x20) && on_ground) {
                            p.x -= 0x100;
                            p.flags |= player_flag::pushing;
                        }
                    }
                } else if (met & solid::from_left) {
                    if (swing_.speed_x != 0) {
                        if (wall(s, px - p.radius_x, py, -8, p) < 0) {
                            crush(p);
                        } else if ((p.held & 0x10) && on_ground) {
                            p.x += 0x100;
                            p.flags |= player_flag::pushing;
                        }
                    }
                } else if (met & solid::from_below) {
                    p.x = start_x;
                    if (swing_.speed_y != 0 && floors(s, (p.y >> 8) + p.radius_y, 8, p) <= 0 &&
                        bottom < (p.y >> 8)) {
                        // Pressed down onto the floor.
                        p.x = start_x;
                        p.y = start_y;
                        crush(p);
                    }
                } else if (swing_.speed_y != 0 && floors(s, (p.y >> 8) - p.radius_y, -8, p) < 0 &&
                           bottom < (p.y >> 8)) {
                    p.x = start_x;
                    p.y = start_y;
                    crush(p);
                }
            }
        }
        // Landing on it.
        if (!(p.flags & player_flag::dead) && p.speed_y >= 0 && by > (p.y >> 8)) {
            step_aside(s, bx, by, p);
            const s32 before = p.x;
            const u32 r = collide_solid(g, sprite_, bx, by, p);
            if (r & solid::on_top) {
                riding_ |= bit;
                p.y += 0x100;
            } else if (r & solid::from_below) {
                p.x = before;
            }
        }
    }

    // 0807E61C: a player meeting the block's side below its top edge is put
    // beside it (the nearer side, within 8 pixels of it) and settled on the
    // map; it no longer stands on anything.
    bool step_aside(const StageState& s, s16 bx, s16 by, Player& p) const {
        const Hitbox& h = sprite_.hitboxes[0];
        const s32 bl = -(p.radius_x + 5), bt = 1 - p.radius_y, br = p.radius_x + 5,
                  bb = p.radius_y - 1;
        const s32 px = p.x >> 8, py = p.y >> 8;
        if (!spans_meet(px + bl, px + bl + (br - bl), bx + h.left,
                        bx + h.left + (h.right - h.left)) ||
            !spans_meet(py + bt, py + bt + (bb - bt), by + h.top,
                        by + h.top + (h.bottom - h.top))) {
            return false;
        }
        if (py <= by + h.top - bb + 2) {
            return false;
        }
        s32 nx = bx + h.left - br;
        if (px > nx + 8) {
            nx = bx + h.right - bl;
            if (px < nx - 7) {
                return false;
            }
            ++nx;
        }
        p.x = nx * 0x100;
        s32 d = floor(s, py + 9, nx, 8, p);
        if (d < 0) {
            p.y += d * 0x100;
        }
        d = floor(s, p.y >> 8, p.x >> 8, -8, p);
        if (d < 0) {
            p.y += d * 0x100;
        }
        p.flags &= ~(player_flag::pushing | player_flag::on_object);
        return true;
    }

    Game& game_;
    Sprite sprite_; // +0C
    Swing swing_;
    u8 riding_{};  // +4E a bit per player riding on top
    u8 touched_{}; // +4F a bit per player against it
};

// 0807F694, 0807F834: a block swinging along a sine (as Swing) with spikes
// on top (Casino Paradise's picture, or record +3 zero) or underneath. A
// player landing on its top is hurt or rides along; meeting its spiked
// underside hurts. Pressing against a side while it moves sideways, the
// player (if the buttons push that way, on the ground) pushes in two
// pixels a frame; caught against a wall, or pressed down on the ground or
// carried into a ceiling, the player is crushed (dead). Players coming
// down beside its top corner are put beside it (080802D0).
class SpikedBlock final : public Interactive {
public:
    SpikedBlock(Game& g, const ObjectSpawn& o) : Interactive(o), game_(g), swing_(o.record) {
        const s8 act = g.stage().zone;
        u32 tiles = 0x24;
        u16 animation = 0x21A;
        if ((act >> 1) == 2 || act == 0x10) {
            tiles = 0x14;
            animation = 0x209;
        } else if (o.record[3] != 0) {
            animation = 0x21B;
            spikes_on_top_ = false;
        }
        sprite_.start(g.sprite_tiles.allocate(tiles), animation, 0, x_, y_, 0x480, 0x2000);
        animate(g, sprite_);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 08080424

    void update(Game& g) override {
        StageState& s = g.stage();
        const s32 old_x = swing_.x, old_y = swing_.y;
        swing_.step(g.assets(), spawn_.record, s.ticks);
        const s32 dx = swing_.x - old_x, dy = swing_.y - old_y;
        const s16 bx = s16(x_ + (swing_.x >> 8)), by = s16(y_ + (swing_.y >> 8));
        sprite_.x = s16(x_ - s.camera.x + (swing_.x >> 8));
        sprite_.y = s16(y_ - s.camera.y + (swing_.y >> 8));
        for (unsigned n = 0; n < s.players; ++n) {
            step(g, s.player[n], bx, by, dx, dy);
        }
        if (out_of_range(s, x_, y_, sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        draw(g, sprite_);
    }

private:
    static void crush(Player& p) {
        p.flags = (p.flags & ~player_flag::pushing) | player_flag::dead;
    }

    s32 near_wall(const StageState& s, s32 x, s32 y, s32 step, const Player& p) const {
        return probe_short(game_.assets(), s.map, x, y, p.layer, step, nullptr, Sensor::wall);
    }

    void step(Game& g, Player& p, s16 bx, s16 by, s32 dx, s32 dy) {
        const StageState& s = g.stage();
        if (p.flags & (player_flag::dead | player_flag::no_movement)) {
            return;
        }
        const u8 bit = u8(1u << p.index);
        const s32 start_x = p.x;
        if (riding_ & bit) {
            p.x += dx;
            p.y += dy + 0x100;
            if (dx > 0) {
                const s32 d = near_wall(s, (p.x >> 8) + 4, p.y >> 8, 8, p);
                if (d < 0) {
                    p.x += d * 0x100;
                }
            } else {
                const s32 d = near_wall(s, p.x >> 8, p.y >> 8, -8, p);
                if (d < 0) {
                    p.x -= d * 0x100;
                }
            }
            if (probe_short(game_.assets(), s.map, (p.y >> 8) - 5, p.x >> 8, p.layer, -8, nullptr,
                            Sensor::floor) < 0) {
                // Carried into a ceiling.
                if (swing_.speed_y != 0) {
                    p.x = start_x;
                    crush(p);
                }
            } else if (!touches_player(sprite_, bx, by, p)) {
                riding_ &= u8(~bit);
            }
        } else {
            push_aside(s, bx, by, p);
            const s32 before_x = p.x, before_y = p.y;
            const u32 r = collide_solid(g, sprite_, bx, by, p);
            if (!(r & (solid::from_below | solid::from_left | solid::from_right))) {
                if ((touched_ & bit) && p.state == 0xE) {
                    // Was pushing it.
                    touched_ &= u8(~bit);
                    p.ground_speed = 0;
                    end_player_moves(g, p);
                    p.flags &= ~player_flag::pushing;
                    if (p.state == 0xE) {
                        p.state = !(p.flags & player_flag::in_air) ? 4
                                  : p.character == 3               ? 0x55
                                                                   : 5;
                    }
                }
            } else {
                touched_ |= bit;
                const u16 held = g.controller().held;
                const bool on_ground = !(p.flags & player_flag::in_air);
                if (r & (solid::from_right | solid::from_left)) {
                    if (swing_.speed_x != 0) {
                        const bool right = r & solid::from_right;
                        if ((held & (right ? 0x20 : 0x10)) && on_ground) {
                            p.x += right ? -0x200 : 0x200;
                            p.flags |= player_flag::pushing;
                        }
                        if (near_wall(s, p.x >> 8, p.y >> 8, right ? -8 : 8, p) < 0) {
                            crush(p);
                        }
                    }
                } else {
                    // Its underside.
                    bool crushed = false;
                    if (swing_.speed_y != 0) {
                        p.x = before_x;
                        if (on_ground) {
                            p.y = before_y;
                            crush(p);
                            crushed = true;
                        }
                    }
                    if (!crushed && !spikes_on_top_) {
                        hurt_player(g, p);
                    }
                }
            }
        }
        // Landing on it.
        if (!(p.flags & player_flag::dead) && p.speed_y >= 0 && by > (p.y >> 8)) {
            push_aside(s, bx, by, p);
            if (collide_solid(g, sprite_, bx, s16(by - 8), p) & solid::on_top) {
                if (spikes_on_top_) {
                    hurt_player(g, p);
                } else {
                    riding_ |= bit;
                    p.y += 0x800;
                }
            }
        }
    }

    // 080802D0: a player meeting the block's side is put beside it (the
    // nearer side, within 8 pixels of it; a speed towards it stops) and
    // settled on the map; it no longer stands on anything.
    bool push_aside(const StageState& s, s16 bx, s16 by, Player& p) const {
        const Hitbox& h = sprite_.hitboxes[0];
        const s32 bl = -(p.radius_x + 5), bt = 1 - p.radius_y, br = p.radius_x + 5,
                  bb = p.radius_y - 1;
        const s32 px = p.x >> 8, py = p.y >> 8;
        if (!spans_meet(px + bl, px + bl + (br - bl), bx + h.left,
                        bx + h.left + (h.right - h.left)) ||
            !spans_meet(py + bt, py + bt + (bb - bt), by + h.top,
                        by + h.top + (h.bottom - h.top))) {
            return false;
        }
        const s32 left = bx + h.left - br;
        if (px <= left + 8) {
            p.x = left * 0x100;
            if (p.speed_x > 0) {
                p.speed_x = 0;
                p.ground_speed = 0;
            }
        } else {
            const s32 right = bx + h.right - bl;
            if (px < right - 8) {
                return false;
            }
            p.x = right * 0x100;
            if (p.speed_x < 0) {
                p.speed_x = 0;
                p.ground_speed = 0;
            }
        }
        const Assets& a = game_.assets();
        s32 d = probe(a, s.map, (p.y >> 8) + 9, p.x >> 8, p.layer, 8, nullptr, Sensor::floor);
        if (d < 0) {
            p.y += d * 0x100;
        }
        d = probe(a, s.map, p.y >> 8, p.x >> 8, p.layer, -8, nullptr, Sensor::floor);
        if (d < 0) {
            p.y += d * 0x100;
        }
        p.flags &= ~(player_flag::pushing | player_flag::on_object);
        return true;
    }

    Game& game_;
    Sprite sprite_; // +0C
    Swing swing_;
    u8 riding_{};               // +4E a bit per player riding on top
    u8 touched_{};              // +4F a bit per player against it
    bool spikes_on_top_ = true; // +50
};

// 08081EB4, 08081F60: a waypoint of a pipe (not drawn; record +6 its
// stage). A player within 8 pixels across and 10 up or down, at stage 0 or
// already in the pipe at a lower stage, is caught (sound AAh): rolled up,
// the buttons and the movement routines off. It is carried in record +5
// steps by (+3, +4) x 8 pixels; then the next waypoint takes over, or
// (none) it is let go standing still.
class PipeWaypoint final : public Interactive {
public:
    explicit PipeWaypoint(const ObjectSpawn& o)
        : Interactive(o, u16(0x2000 | o.record[6])), stage_(o.record[6]), steps_(o.record[5]),
          dx_(s8(o.record[3]) * 8), dy_(s8(o.record[4]) * 8) {}

    void update(Game& g) override {
        StageState& s = g.stage();
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if (p.flags & player_flag::dead) {
                continue;
            }
            const u8 bit = u8(1u << n);
            if (!(carried_ & bit)) {
                const bool waiting =
                    !(p.flags & player_flag::no_movement) || stage_ <= p.tube_stage;
                if (waiting && stage_ != 0) {
                    continue;
                }
                const s32 px = p.x >> 8, py = p.y >> 8;
                if (px >= x_ - 8 && px <= x_ + 8 && py >= y_ - 10 && py <= y_ + 10) {
                    catch_player(g, p, carry_[n], bit);
                }
                continue;
            }
            if (p.tube_stage != s8(stage_)) {
                carried_ &= u8(~bit);
                continue;
            }
            carry(g, s, p, carry_[n], bit);
        }
        if ((carried_ & 3) == 0 && !near_view(s16(x_ - s.camera.x), s16(y_ - s.camera.y))) {
            give_back();
            g.scheduler().destroy(*this);
        }
    }

private:
    struct Carry {
        u8 step{};                // +3E
        s32 start_x{}, start_y{}; // +40, +48 (pixels)
        s32 last_x{}, last_y{};   // +58, +60 (24.8)
    };

    static void hold(Game& g, Player& p) {
        p.ground_speed = 0;
        p.held = 0;
        p.pressed = 0;
        set_player_radius(g, p, 6, 0xE);
    }

    void catch_player(Game& g, Player& p, Carry& c, u8 bit) {
        c.step = 0;
        p.flags |= player_flag::frozen | player_flag::no_movement | player_flag::rolling;
        c.start_x = p.x >> 8;
        c.start_y = p.y >> 8;
        p.speed_x = 0;
        p.speed_y = 0;
        hold(g, p);
        g.sound().start(0xAA);
        carried_ |= bit;
        p.tube_stage = s8(stage_);
        p.power |= 0x80;
    }

    void carry(Game& g, const StageState& s, Player& p, Carry& c, u8 bit) {
        p.x = (c.start_x + divide(c.step * dx_, steps_)) * 0x100;
        p.y = (c.start_y + divide(c.step * dy_, steps_)) * 0x100;
        p.speed_x = s16(s16(p.x) - s16(c.last_x));
        p.speed_y = s16(s16(p.y) - s16(c.last_y));
        if (++c.step < steps_ && s32(stage_) >= p.tube_stage) {
            p.flags |= player_flag::frozen | player_flag::no_movement;
            hold(g, p);
            carried_ |= bit;
        } else {
            // The end of this stretch (or a later waypoint has it).
            if (p.tube_stage == s8(stage_)) {
                p.flags &= ~player_flag::frozen;
                p.held |= s.jump_buttons | s.other_buttons;
                p.flags &= ~player_flag::no_movement;
                p.power &= 0x7F;
                p.speed_y = 0;
                p.speed_x = 0;
                p.ground_speed = 0;
            }
            carried_ &= u8(~bit);
        }
        c.last_x = p.x;
        c.last_y = p.y;
    }

    u8 stage_;    // +3C
    u8 steps_;    // +3D
    s32 dx_, dy_; // +50, +54
    std::array<Carry, 2> carry_{};
    u8 carried_{}; // +68 a bit per player
};

// 08085270, 08085408: a spinning lift (two pictures: the disc 1E7h and its
// shaft 247h). A player landing on its middle (not Knuckles gliding or
// climbing) is caught spinning (state 21h, the movement routines off,
// sound D1h) and the disc sinks, speeding up (080858B4; its whirr D1h, D2h,
// D3h faster as it spins up), others may still jump on. At the bottom
// (about 481 pixels down) the riders spin on the spot, thrown out along the
// ground after 300 frames or when left or right is held (the way held, or
// the way they came); then it is a platform that stays down (08086128).
class SpinningLift final : public Interactive {
public:
    SpinningLift(Game& g, const ObjectSpawn& o) : Interactive(o), game_(g) {
        disc_.start(g.sprite_tiles.allocate(0x3C), 0x1E7, 0, x_, y_, 0x480, 0x2000);
        shaft_.start(g.sprite_tiles.allocate(10), 0x247, 0, x_, y_, 0x480, 0x2000);
        animate(g, disc_);
        animate(g, shaft_);
    }

    // 080862B0
    void on_destroy() override {
        game_.sprite_tiles.free(disc_.tiles.to);
        game_.sprite_tiles.free(shaft_.tiles.to);
    }

    void update(Game& g) override {
        switch (state_) {
        case State::waiting:
            wait(g);
            break;
        case State::sinking:
            sink(g);
            break;
        case State::down:
            stay_down(g);
            break;
        }
    }

private:
    static constexpr u32 bottom = 0x3C2A58; // the depth reached (pixels << 13)

    s32 depth_pixels() const { return s32(depth_ >> 13); }

    bool leave_if_away(Game& g) {
        if (!out_of_range(g.stage(), x_, y_, disc_.x, disc_.y)) {
            return false;
        }
        give_back();
        g.scheduler().destroy(*this);
        return true;
    }

    static bool in_reach(const Player& p, s32 x, s32 y) {
        const s32 px = p.x >> 8, py = p.y >> 8;
        return px >= x - 8 && px <= x + 8 && py >= y - 0x30 && py <= y + 0xC;
    }

    // A player caught on the disc spins with the movement routines off.
    void catch_player(Game& g, Player& p) {
        riding_ |= u8(1u << p.index);
        p.speed_x = 0;
        p.ground_speed = p.ground_speed > 0 ? 0x780 : -0x780;
        p.held = 0;
        p.pressed = 0;
        end_player_moves(g, p);
        p.state = 0x21;
        p.flags |= player_flag::no_movement;
    }

    // 08085408
    void wait(Game& g) {
        StageState& s = g.stage();
        disc_.x = shaft_.x = s16(x_ - s.camera.x);
        disc_.y = shaft_.y = s16(y_ - s.camera.y);
        if (leave_if_away(g)) {
            return;
        }
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if ((p.flags & player_flag::dead) ||
                (p.character == 2 && p.state >= 0x46 && p.state <= 0x4B)) {
                continue;
            }
            collide_platform(g, disc_, x_, y_, p);
            if (!in_reach(p, x_, y_)) {
                continue;
            }
            spin_ = u16(std::abs(s32(p.speed_x)) << 2);
            const s32 a = divide((((p.x >> 8) - x_) + 0x30) << 9, 0x60);
            angle_ = u16((a < 0 ? 0x300 - a : a + 0x300) & 0x3FF);
            depth_ = 0;
            timer_ = 0;
            whirr_ = 0;
            way_ = p.ground_speed > 0;
            catch_player(g, p);
            g.sound().start(0xD1);
            state_ = State::sinking;
            p.power |= 0x80;
        }
        draw(g, disc_);
        draw(g, shaft_);
    }

    // 080858B4
    void sink(Game& g) {
        StageState& s = g.stage();
        disc_.x = shaft_.x = s16(x_ - s.camera.x);
        disc_.y = s16(y_ - s.camera.y + depth_pixels() + 2);
        shaft_.y = s16(disc_.y + 2);
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            const bool riding = riding_ & (1u << n);
            if (riding) {
                if (!(p.flags & player_flag::dead)) {
                    p.x = x_ << 8;
                    p.y = (y_ + depth_pixels()) << 8;
                }
                continue;
            }
            const s32 top = y_ + s16((depth_ << 3) >> 16);
            if (collide_platform(g, disc_, x_, s16(top), p) & solid::on_top) {
                p.y = top << 8;
            } else if (!touches_player(disc_, x_, s16(top), p)) {
                p.flags &= ~player_flag::on_object;
            }
            if (in_reach(p, x_, top) && depth_ < bottom) {
                catch_player(g, p);
                p.power |= 0x80;
            }
        }
        if (depth_ >= bottom) {
            for (unsigned n = 0; n < s.players; ++n) {
                Player& p = s.player[n];
                if (!(p.flags & player_flag::dead) && (riding_ & (1u << n))) {
                    p.speed_x = s16(divide(((spin_ >> 2) - 2000) * 2, 3));
                    p.ground_speed = s16(divide(((spin_ >> 2) - 2000) * 2, 3));
                }
            }
        } else {
            spin_ = u16(spin_ + 0x40);
        }
        if (spin_ > 0x8000) {
            spin_ = 0x8000;
        }
        disc_.speed = u8(divide(spin_ * 35, 0xA000));
        if (disc_.speed > 0x10) {
            disc_.speed = 0x10;
        }
        depth_ += spin_;
        if (leave_if_away(g)) {
            return;
        }
        for (unsigned n = 0; n < s.players; ++n) {
            release(g, s.player[n]);
        }
        ++whirr_;
        if (spin_ < 0x3200) {
            if (spin_ != 0 && remainder(whirr_, 25) == 0) {
                g.sound().start(0xD1);
            }
        } else if (spin_ < 0x4B00) {
            if (remainder(whirr_, 16) == 0) {
                g.sound().start(0xD2);
            }
        } else if (remainder(whirr_, 8) == 0) {
            g.sound().start(0xD3);
        }
        animate(g, disc_);
        draw(g, disc_);
        draw(g, shaft_);
    }

    // At the bottom: riders are let go after 300 frames or when left or
    // right is held.
    void release(Game& g, Player& p) {
        const u8 bit = u8(1u << p.index);
        if (!(p.flags & player_flag::dead) && (riding_ & bit)) {
            if (depth_ < bottom) {
                return;
            }
            depth_ = bottom;
            if (p.index == 0) {
                ++timer_;
            }
            if (timer_ < 300 && !(p.held & 0x30)) {
                return;
            }
            timer_ = 300;
            if (p.held & 0x20) {
                if (p.ground_speed > 0) {
                    p.ground_speed = s16(-p.ground_speed);
                }
            } else if (p.held & 0x10) {
                if (p.ground_speed < 0) {
                    p.ground_speed = s16(-p.ground_speed);
                }
            } else if (!way_) {
                p.ground_speed = s16(-p.ground_speed);
            }
            p.speed_x = p.ground_speed;
            riding_ &= u8(~bit);
            end_player_moves(g, p);
            p.flags &= ~(player_flag::no_movement | player_flag::on_object);
            p.flags |= player_flag::rolling | player_flag::jump_cancel | player_flag::in_air;
            p.state = 4;
            set_player_radius(g, p, 6, 0xE);
            p.speed_y = 0;
            p.x = x_ << 8;
            state_ = State::down;
            p.power &= 0x7F;
            p.body->sprite.layer = 0x400;
            return;
        }
        if (!(p.flags & player_flag::dead)) {
            return;
        }
        if (depth_ > bottom) {
            depth_ = bottom;
        }
        p.flags &= ~player_flag::no_movement;
        if (p.index == 0) {
            ++timer_;
        }
        if (timer_ > 300) {
            state_ = State::down;
        }
    }

    // 08086128
    void stay_down(Game& g) {
        StageState& s = g.stage();
        disc_.x = shaft_.x = s16(x_ - s.camera.x);
        disc_.y = shaft_.y = s16(y_ - s.camera.y + depth_pixels());
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if (!(p.flags & player_flag::dead)) {
                collide_platform(g, disc_, x_, s16(y_ + depth_pixels()), p);
            }
        }
        if (leave_if_away(g)) {
            return;
        }
        draw(g, disc_);
        draw(g, shaft_);
    }

    enum class State { waiting, sinking, down } state_ = State::waiting;
    Game& game_;
    Sprite disc_;  // +0C
    Sprite shaft_; // +3C
    u16 angle_{};  // +6C (kept; nothing reads it)
    u16 spin_{};   // +6E
    u32 depth_{};  // +70 (pixels << 13)
    u16 whirr_{};  // +74 frames, for the sound
    u16 timer_{};  // +76 frames at the bottom
    bool way_{};   // +78 caught moving right
    u8 riding_{};  // +79 a bit per player
};

// 08082BE0, 08082D28: the mouth of a funnel (record +6: the pipe stage
// it starts). A player landing on it outside any pipe is caught (sound
// AAh): rolled up (state 20h) and shrunk to 0B0h, the movement routines
// off, then falls through it under gravity (2Ah a frame) while drawn
// across by record +4 pixels over record +3 frames, growing back to full
// size; it is let go after those frames, after record +5 pixels of fall,
// or when a later pipe stage has it. Knuckles climbing below it is held
// under its bottom.
class Funnel final : public Interactive {
public:
    Funnel(Game& g, const ObjectSpawn& o)
        : Interactive(o, u16(0x1900 | o.record[6])), game_(g), stage_(o.record[6]),
          frames_(o.record[3]), across_(s8(o.record[4])), depth_(o.record[5]) {
        sprite_.start(g.sprite_tiles.allocate(0x10), 0x1E8, 0, s16(o.cell_x * 0x100 + 0x7F0), y_,
                      0x480, 0x2000);
        animate(g, sprite_);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 080833BC

    void update(Game& g) override {
        StageState& s = g.stage();
        sprite_.x = s16(x_ - s.camera.x);
        sprite_.y = s16(y_ - s.camera.y);
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if (p.flags & player_flag::dead) {
                continue;
            }
            if (falling_ & (1u << n)) {
                fall(g, p, n);
            } else {
                meet(g, s, p, n);
            }
        }
        if ((falling_ & 3) == 0 && !near_view(sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        animate(g, sprite_);
        draw(g, sprite_);
    }

private:
    void meet(Game& g, StageState& s, Player& p, unsigned n) {
        const s32 before_x = p.x;
        // 080549C8: Knuckles (the first player) gliding or climbing.
        if (s.character == 2 && u8(s.player[0].state - 0x46) < 6 && p.state != 0x49) {
            if (p.y > y_ * 0x100 + 0xC00 &&
                (collide_solid(g, sprite_, x_, y_, p) & solid::from_below)) {
                p.y = (y_ + 0x17) << 8;
            }
            return;
        }
        if (p.tube_stage != 0x7F || !(collide_solid(g, sprite_, x_, y_, p) & solid::on_top)) {
            return;
        }
        step_[n] = 0;
        fallen_[n] = 0;
        end_player_moves(g, p);
        p.flags |= player_flag::rolling;
        p.flags &= ~player_flag::on_object;
        p.flags |= player_flag::no_movement;
        p.tube_stage = s8(stage_);
        p.speed_y = 0;
        p.speed_x = 0;
        p.ground_speed = 0;
        p.pressed = 0;
        p.held = 0;
        p.state = 0x20;
        p.scale_x = 0xB0;
        p.scale_y = 0xB0;
        set_player_radius(g, p, 6, 0xE);
        g.sound().start(0xAA);
        falling_ |= u8(1u << n);
        p.x = before_x;
        p.power |= 0x80;
    }

    void fall(Game& g, Player& p, unsigned n) {
        p.speed_y = s16(p.speed_y + 0x2A);
        const s32 nx = (x_ + divide(across_ * step_[n], frames_)) * 0x100;
        p.y += p.speed_y;
        fallen_[n] += p.speed_y;
        p.scale_x = s16(p.scale_x + 4);
        p.x = nx;
        if (p.scale_x > 0x100) {
            p.scale_x = 0x100;
        }
        p.scale_y = p.scale_x;
        if (step_[n] == 0x19) {
            create_chute_tails(g, p);
        }
        const u8 bit = u8(1u << n);
        if (++step_[n] < frames_ && (fallen_[n] >> 8) <= depth_ &&
            (p.tube_stage == 0x7F || s32(stage_) >= p.tube_stage)) {
            end_player_moves(g, p);
            p.flags |= player_flag::no_movement;
            p.speed_x = 0;
            p.ground_speed = 0;
            p.state = 0x20;
            set_player_radius(g, p, 6, 0xE);
            falling_ |= bit;
        } else {
            falling_ &= u8(~bit);
        }
    }

    Game& game_;
    Sprite sprite_;               // +0C
    std::array<u8, 2> step_{};    // +3C
    u8 falling_{};                // +3E a bit per player
    u8 stage_;                    // record +6
    u8 frames_;                   // +3F
    std::array<s32, 2> fallen_{}; // +40 (8.8)
    s32 across_;                  // +4A
    u16 depth_;                   // +48
};

// 08080438, 0808056C: a vortex (the act's picture, 086C8CB4). A player
// running into its middle is caught (sound CEh; the movement routines off)
// and carried round it on a sine, sinking ever faster (08080880; whoever
// comes later joins at the same depth and speed). Near the bottom the
// circles widen and the riders drop until they are let go rolling in the
// air; it then stays still (08080F90). Its sounds CFh and D0h come 56 and
// 112 frames after the first catch.
class Vortex final : public Interactive {
public:
    Vortex(Game& g, const ObjectSpawn& o) : Interactive(o), game_(g) {
        sprite_.start(g.sprite_tiles.allocate(0x3C),
                      g.assets().u16(0x086C8CB4 + u32(s32(g.stage().zone)) * 2), 0, x_, y_, 0x140,
                      0x2000);
        animate(g, sprite_);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 080810E4

    void update(Game& g) override {
        switch (state_) {
        case State::waiting:
            wait(g);
            break;
        case State::turning:
            turn(g);
            break;
        case State::still:
            stay(g);
            break;
        }
    }

private:
    static constexpr u32 bottom = 0xD8108; // the depth reached (pixels << 13)

    bool leave_if_away(Game& g) {
        if (!out_of_range(g.stage(), x_, y_, sprite_.x, sprite_.y)) {
            return false;
        }
        give_back();
        g.scheduler().destroy(*this);
        return true;
    }

    // Touching its middle (10 pixels in from the player's side), `down`
    // pixels below its place.
    bool meets(const Player& p, s32 down) const {
        const s32 side = (p.x >> 8) > x_ ? 10 : -10;
        return touches_player(sprite_, s16(x_ - side), s16(y_ + down), p);
    }

    s32 start_angle(const Player& p, s32 base) const {
        const s32 a = divide((((p.x >> 8) - x_) + 0x30) << 9, 0x60);
        return a < 0 ? base + 0x300 - a : a + base + 0x300;
    }

    void catch_player(Game& g, Player& p) {
        p.held = 0;
        p.pressed = 0;
        end_player_moves(g, p);
        p.state = 5;
        p.flags |= player_flag::no_movement;
    }

    void sound_after_catch(Game& g) {
        if (++timer_ > 0xF0) {
            timer_ = 0xF0;
        }
        const StageState& s = g.stage();
        const u16 sound = timer_ == 0x38 ? 0xCF : timer_ == 0x70 ? 0xD0 : 0;
        if (sound != 0) {
            for (unsigned n = 0; n < s.players; ++n) {
                if (!(s.player[n].flags & player_flag::dead)) {
                    g.sound().start(sound);
                }
            }
        }
    }

    // 0808056C
    void wait(Game& g) {
        StageState& s = g.stage();
        sprite_.x = s16(x_ - s.camera.x);
        sprite_.y = s16(y_ - s.camera.y);
        if (leave_if_away(g)) {
            return;
        }
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if ((p.flags & player_flag::dead) || !meets(p, 5)) {
                continue;
            }
            riding_ |= u8(1u << n);
            spin_[n] = u16(std::abs(s32(p.speed_x)) << 2);
            angle_[n] = u16(start_angle(p, 0) & 0x3FF);
            depth_[n] = 0;
            circles_ = 0;
            timer_ = 0;
            catch_player(g, p);
            g.sound().start(0xCE);
            state_ = State::turning;
            p.power |= 0x80;
        }
        draw(g, sprite_);
    }

    // 08080880
    void turn(Game& g) {
        StageState& s = g.stage();
        const Assets& a = g.assets();
        const u32 deepest = std::max(depth_[0], depth_[1]);
        sprite_.x = s16(x_ - s.camera.x);
        sprite_.y = s16(y_ - s.camera.y + s32(deepest >> 13));
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if ((p.flags & player_flag::dead) || (riding_ & (1u << n)) ||
                !meets(p, 0xD + s32(deepest >> 13))) {
                continue;
            }
            // A later rider joins the earlier one.
            if (spin_[0] > spin_[1]) {
                spin_[1] = spin_[0];
                depth_[1] = depth_[0];
            } else {
                spin_[0] = spin_[1];
                depth_[0] = depth_[1];
            }
            riding_ |= u8(1u << n);
            angle_[n] = u16(start_angle(p, s32(depth_[0] >> 8)) & 0x3FF);
            catch_player(g, p);
            p.power |= 0x80;
        }
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if (!(riding_ & (1u << n)) || (p.flags & player_flag::dead)) {
                continue;
            }
            const unsigned i = ((depth_[n] >> 8) + angle_[n] + (circles_ & 0xF) * 0x40u) & 0x3FF;
            p.x = (x_ << 8) + divide(sine(a, i) * 25, circles_ * 4 + 50);
            p.y = (y_ << 8) - 0x1100 + s32(depth_[n] >> 5);
        }
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if (!(riding_ & (1u << n))) {
                continue;
            }
            if (depth_[n] < bottom) {
                spin_[n] = u16(spin_[n] + 0x40);
            } else if (!(p.flags & player_flag::dead)) {
                p.speed_y = s16(((spin_[n] >> 2) - 2000) * 2);
            }
            if (spin_[n] > 0x8000) {
                spin_[n] = 0x8000;
            }
            sprite_.speed = u8(divide(spin_[n] * 35, 0xA000));
            if (sprite_.speed > 0x10) {
                sprite_.speed = 0x10;
            }
            depth_[n] += spin_[n];
        }
        if (leave_if_away(g)) {
            return;
        }
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            const u8 bit = u8(1u << n);
            if (!(riding_ & bit) || depth_[n] < bottom) {
                continue;
            }
            depth_[n] = bottom;
            if (!(p.flags & player_flag::dead)) {
                p.y += circles_ < 0x10 ? circles_ * 0xB4 : 0xB40;
            }
            if (n == 0) {
                ++circles_;
            }
            if (circles_ <= 0x40) {
                continue;
            }
            if (!(p.flags & player_flag::dead)) {
                p.flags &= ~player_flag::no_movement;
                p.flags |= player_flag::rolling | player_flag::jump_cancel | player_flag::in_air;
                p.speed_x = 0;
                p.ground_speed = 0;
                p.x = x_ << 8;
                riding_ &= u8(~bit);
            }
            state_ = State::still;
            p.power &= 0x7F;
        }
        sound_after_catch(g);
        animate(g, sprite_);
        draw(g, sprite_);
    }

    // 08080F90
    void stay(Game& g) {
        StageState& s = g.stage();
        sprite_.x = s16(x_ - s.camera.x);
        sprite_.y = s16(y_ - s.camera.y + 0x6C);
        if (leave_if_away(g)) {
            return;
        }
        sound_after_catch(g);
        draw(g, sprite_);
    }

    enum class State { waiting, turning, still } state_ = State::waiting;
    Game& game_;
    Sprite sprite_;              // +0C
    std::array<u16, 2> angle_{}; // +3C
    std::array<u16, 2> spin_{};  // +40
    u16 timer_{};                // +44 frames since the first catch
    std::array<u32, 2> depth_{}; // +48 (pixels << 13)
    u8 circles_{};               // +50 frames at the bottom
    u8 riding_{};                // +51 a bit per player
};

// 080810F8, 08081208: a one-way gate (opening to the right; record +3
// nonzero: to the left). A player running at it from the open side swings
// it open (sound B9h, curled up) for 125 frames (08081300); from the other
// side it is a wall (0808190C).
class OneWayGate final : public Interactive {
public:
    OneWayGate(Game& g, const ObjectSpawn& o) : Interactive(o), game_(g) {
        sprite_.start(g.sprite_tiles.allocate(8), 0x1F0, 0, x_, y_, 0x480,
                      o.record[3] != 0 ? 0x2400 : 0x2000);
        animate(g, sprite_);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 08081EA0

    void update(Game& g) override {
        StageState& s = g.stage();
        sprite_.x = s16(x_ - s.camera.x);
        sprite_.y = s16(y_ - s.camera.y);
        if (open_ > 0) {
            swinging(g, s);
            return;
        }
        // (The test runs over all players once per living player.)
        for (unsigned n = 0; n < s.players; ++n) {
            if (!(s.player[n].flags & player_flag::dead)) {
                meet_all(g, s);
            }
        }
        if (!near_view(sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        draw(g, sprite_);
    }

private:
    // 0808190C: a player running at it from the open side swings it open.
    void meet_all(Game& g, StageState& s) {
        const bool left = sprite_.flags & sprite_flag::flip_x;
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            const s32 kept_y = p.y;
            const s32 px = p.x >> 8, py = p.y >> 8;
            const auto open = [&] {
                open_ = 0x7D;
                sprite_.variant = 1;
                set_player_radius(g, p, 6, 0xE);
                g.sound().start(0xB9);
            };
            if (py >= y_ - 0x28 && py <= y_ + 0x28) {
                if (left) {
                    if (px >= x_ && px <= x_ + 0x20) {
                        if (p.speed_x < 0) {
                            open();
                        } else {
                            collide_wall(g, sprite_, x_, y_, p, false);
                        }
                    }
                } else if (px >= x_ - 0x20 && px <= x_ + 0x10) {
                    if (px <= x_) {
                        if (p.speed_x > 0) {
                            open();
                        } else {
                            collide_wall(g, sprite_, x_, y_, p, false);
                        }
                    } else if (p.speed_x < 0) {
                        collide_wall(g, sprite_, x_, y_, p, false);
                        p.ground_speed = 0;
                        p.speed_x = 0;
                    }
                }
            }
            p.y = kept_y;
        }
    }

    // 08081300: open; it stays a wall against players coming from behind.
    void swinging(Game& g, StageState& s) {
        if (!near_view(sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        const bool left = sprite_.flags & sprite_flag::flip_x;
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if (p.flags & player_flag::dead) {
                continue;
            }
            if (touches_player(sprite_, s16(left ? x_ - 6 : x_ + 6), y_, p) &&
                (left ? p.speed_x > 0 : p.speed_x < 0)) {
                collide_wall(g, sprite_, x_, y_, p, false);
            }
        }
        if (--open_ == 0) {
            sprite_.variant = 0;
        }
        animate(g, sprite_);
        draw(g, sprite_);
    }

    Game& game_;
    Sprite sprite_; // +0C
    u8 open_{};     // +3C frames left open (the update is 08081300)
};

// 08081508, 08081618: a one-way trapdoor (opening downwards; record +3
// nonzero: upwards). A player coming through it the open way swings it open
// (sound B9h, curled up) for 125 frames (08081710); otherwise it is solid
// (08081C58).
class OneWayTrapdoor final : public Interactive {
public:
    OneWayTrapdoor(Game& g, const ObjectSpawn& o) : Interactive(o), game_(g) {
        sprite_.start(g.sprite_tiles.allocate(8), 0x1F1, 1, x_, y_, 0x480,
                      o.record[3] != 0 ? 0x2800 : 0x2000);
        animate(g, sprite_);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 08081EA0

    void update(Game& g) override {
        StageState& s = g.stage();
        sprite_.x = s16(x_ - s.camera.x);
        sprite_.y = s16(y_ - s.camera.y);
        if (open_ > 0) {
            swinging(g, s);
            return;
        }
        // (The test runs over all players once per living player.)
        for (unsigned n = 0; n < s.players; ++n) {
            if (!(s.player[n].flags & player_flag::dead)) {
                meet_all(g, s);
            }
        }
        if (!near_view(sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        draw(g, sprite_);
    }

private:
    // 08081C58: a player coming through the open way swings it open.
    void meet_all(Game& g, StageState& s) {
        const bool up = sprite_.flags & sprite_flag::flip_y;
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            const s32 px = p.x >> 8, py = p.y >> 8;
            const bool across = px >= x_ && px <= x_ + 0x20;
            const bool near = up ? py >= y_ && py <= y_ + 0x1C : py >= y_ - 0x1C && py <= y_;
            if (across && near && (up ? p.speed_y < 0 : p.speed_y > 0)) {
                open_ = 0x7D;
                sprite_.variant = 0;
                set_player_radius(g, p, 6, 0xE);
                g.sound().start(0xB9);
                continue;
            }
            collide_solid(g, sprite_, x_, y_, p);
        }
    }

    // 08081710: open; it stays solid against players coming from behind.
    void swinging(Game& g, StageState& s) {
        if (!near_view(sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        const bool up = sprite_.flags & sprite_flag::flip_y;
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if (p.flags & player_flag::dead) {
                continue;
            }
            if (touches_player(sprite_, x_, s16(up ? y_ + 5 : y_ - 5), p) &&
                (up ? p.speed_y > 0 : p.speed_y < 0)) {
                collide_solid(g, sprite_, x_, y_, p);
            }
        }
        if (--open_ == 0) {
            sprite_.variant = 1;
        }
        animate(g, sprite_);
        draw(g, sprite_);
    }

    Game& game_;
    Sprite sprite_; // +0C
    u8 open_{};     // +3C frames left open (the update is 08081710)
};

// 0808462C, 080846D8: a warp (not drawn). A player within 12 pixels is
// rolled up, the movement routines off, and put at the act position
// (record +3 x 256 + +5, +4 x 256 + +6) pixels (sound AAh). It is held there
// until the camera has stopped with the player in view, then dropped into
// the air. A player dying meanwhile is put at the view's middle and the
// camera stops following.
class Warp final : public Interactive {
public:
    explicit Warp(const ObjectSpawn& o)
        : Interactive(o), target_x_(u16(s8(o.record[3]) << 8)),
          target_y_(u16(s8(o.record[4]) << 8)), fine_x_(u16(o.record[5] << 8)),
          fine_y_(u16(o.record[6] << 8)) {}

    void update(Game& g) override {
        StageState& s = g.stage();
        auto& c = s.camera;
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            const u16 bit = u16(1u << n);
            const s32 px = p.x >> 8, py = p.y >> 8;
            const auto hold = [&] {
                p.speed_x = 0;
                p.speed_y = 0;
                p.ground_speed = 0;
                p.state = 5;
                set_player_radius(g, p, 6, 0xE);
            };
            if (!(held_ & bit)) {
                if ((p.flags & player_flag::dead) || px < x_ - 0xC || px > x_ + 0xC ||
                    py < y_ - 0xC || py > y_ + 0xC) {
                    continue;
                }
                end_player_moves(g, p);
                p.flags |= player_flag::no_movement | player_flag::rolling;
                p.x = target_x_ * 0x100 + fine_x_;
                p.y = target_y_ * 0x100 + fine_y_;
                hold();
                g.sound().start(0xAA);
                held_ |= bit;
                p.power |= 0x80;
                camera_x_ = c.x;
                camera_y_ = c.y;
                continue;
            }
            if (c.x == camera_x_ && c.y == camera_y_ && px >= c.x && px <= c.x + 0xF0 &&
                py >= c.y && py <= c.y + 0xA0) {
                // Let go.
                p.flags = (p.flags & ~player_flag::no_movement) | player_flag::in_air;
                held_ &= u16(~bit);
                p.power &= 0x7F;
                p.speed_x = 0;
                p.speed_y = 0;
                p.ground_speed = 0;
            } else {
                end_player_moves(g, p);
                p.flags |= player_flag::no_movement;
                hold();
                held_ |= bit;
            }
            if (p.flags & player_flag::dead) {
                held_ &= u16(~bit);
                p.flags &= ~player_flag::no_movement;
                if (p.index == 0) {
                    p.y = (c.y + 0x50) * 0x100;
                    c.max_step_x = 0;
                    c.max_step_y = 0;
                    p.x = (c.x + 0x78) * 0x100;
                }
            }
            camera_x_ = c.x;
            camera_y_ = c.y;
        }
        if ((held_ & 3) == 0 && !near_view(s16(x_ - c.x), s16(y_ - c.y))) {
            give_back();
            g.scheduler().destroy(*this);
        }
    }

private:
    u16 target_x_, target_y_;     // +3C, +3E
    s16 camera_x_{}, camera_y_{}; // +40, +42 the camera last frame
    u16 held_{};                  // +44 a bit per player
    u16 fine_x_, fine_y_;         // +46, +48
};

// 08084C04, 08084D40: a bumper inside a pipe (record +4: its pipe stage;
// +5 mirrors it). A player carried through a pipe at a lower stage meeting
// it is thrown up at 0C90h (sound B7h), rolled up and falling back at 2Ah a
// frame along its line for record +3 frames, then let go (if no later stage
// has it). It flashes for 24 frames.
class PipeBumper final : public Interactive {
public:
    PipeBumper(Game& g, const ObjectSpawn& o)
        : Interactive(o, u16(s8(o.record[4]) + 0x2000)), game_(g), frames_(o.record[3]),
          stage_(o.record[4]) {
        sprite_.start(g.sprite_tiles.allocate(0x10), 0x20A, 0, x_, y_, 0x480,
                      o.record[5] == 0 ? 0x2000 : 0x2400);
        animate(g, sprite_);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 0808525C

    void update(Game& g) override {
        StageState& s = g.stage();
        sprite_.x = s16(x_ - s.camera.x);
        sprite_.y = s16(y_ - s.camera.y);
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if (!(p.flags & player_flag::dead)) {
                step(g, p, n);
            }
        }
        if ((carried_ & 3) == 0 && !near_view(sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        animate(g, sprite_);
        draw(g, sprite_);
    }

private:
    void show(u16 animation, u8 variant) {
        sprite_.animation = animation;
        sprite_.variant = variant;
        sprite_.previous_variant = 0xFF;
    }

    static void hold(Game& g, Player& p) {
        end_player_moves(g, p);
        p.flags |= player_flag::no_movement;
        p.state = 0x20;
    }

    void step(Game& g, Player& p, unsigned n) {
        const u8 bit = u8(1u << n);
        Ride& r = ride_[n];
        if (!(carried_ & bit)) {
            if (r.flash != 0) {
                if (--r.flash == 0) {
                    show(0x20A, 0);
                }
                return;
            }
            if (!(p.flags & player_flag::no_movement) || p.tube_stage >= s32(stage_) ||
                !touches_player(sprite_, x_, y_, p)) {
                return;
            }
            r.frame = 0;
            end_player_moves(g, p);
            p.flags |= player_flag::no_movement | player_flag::rolling;
            p.tube_stage = s8(stage_);
            r.y = p.y;
            r.x = p.x;
            r.speed = -0xC90;
            p.state = 0x20;
            p.speed_y = 0;
            p.speed_x = 0;
            p.ground_speed = 0;
            set_player_radius(g, p, 6, 0xE);
            g.sound().start(0xB7);
            carried_ |= bit;
            r.flash = 0x18;
            show(0x20A, 1);
            return;
        }
        if (r.flash != 0 && --r.flash == 0) {
            show(0x1DE, 0);
        }
        r.speed += 0x2A;
        r.y += r.speed;
        p.y = r.y;
        p.x = r.x;
        p.speed_x = 0;
        p.speed_y = s16(r.speed);
        p.ground_speed = 0;
        if (++r.frame < frames_) {
            hold(g, p);
            p.speed_x = 0;
            p.ground_speed = 0;
            set_player_radius(g, p, 6, 0xE);
            carried_ |= bit;
        } else {
            if (p.tube_stage == s8(stage_)) {
                p.flags &= ~player_flag::no_movement;
            }
            p.speed_y = s16(r.speed);
            carried_ &= u8(~bit);
            r.flash = 0;
        }
    }

    struct Ride {
        s32 speed{}; // +40
        s32 y{};     // +48
        s32 x{};     // +50
        u8 frame{};  // +58
        u8 flash{};  // +5A
    };

    Game& game_;
    Sprite sprite_; // +0C
    u8 carried_{};  // +3C a bit per player
    u8 frames_;     // +3D
    u8 stage_;      // +3E
    std::array<Ride, 2> ride_{};
};

// 0808F4AC, 0808F6B4: a platform going round a circle on a chain (the
// act's picture, 086C8D48; the pivot and two chain links are drawn outside
// Casino Paradise; motion as the orbiting bumper: record +5/+6 the radius,
// +4 the signed speed, +3 the start angle in sixteenths). Riders are carried
// along (an eighth of the motion a frame), held on its top, and it dips
// while stood on.
class CirclingPlatform final : public Interactive {
public:
    CirclingPlatform(Game& g, const ObjectSpawn& o)
        : Interactive(o), game_(g), phase_(u16((o.record[3] & 0xF) << 7)), speed_(s8(o.record[4])),
          forward_(o.record[6] < o.record[5]), radius_(std::max(o.record[5], o.record[6])) {
        const s8 act = g.stage().zone;
        chained_ = (act >> 1) != 2 && act != 0x10;
        sprite_.start(g.sprite_tiles.allocate(0x12), g.assets().u16(0x086C8D48 + u32(s32(act)) * 2),
                      0, x_, y_, 0x480, 0x2000);
        animate(g, sprite_);
        pivot_.start(0x13020, 0x212, 0, 0, 0, 0x480, 0x2000);
        animate(g, pivot_);
        link_.start(0x130A0, 0x213, 0, 0, 0, 0x480, 0x2000);
        animate(g, link_);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 0808FB64

    void update(Game& g) override {
        StageState& s = g.stage();
        const Assets& a = g.assets();
        const s32 k = forward_ ? speed_ : -speed_;
        const unsigned angle = ((u32(k) * s.ticks + phase_) & 0x7FF) >> 1;
        x_offset_ = sine(a, angle) * radius_;
        y_offset_ = sine(a, angle + 0x100) * radius_;
        const s32 dy = y_offset_ >> 11;
        sprite_.x = s16(x_ - s.camera.x + (x_offset_ >> 11));
        sprite_.y = s16(y_ - s.camera.y + dy);
        if (chained_) {
            pivot_.x = s16(x_ - s.camera.x);
            pivot_.y = s16(y_ - s.camera.y);
        }
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            const u8 bit = u8(1u << n);
            if (riding_ & bit) {
                p.x += ((x_offset_ - last_x_offset_) >> 3) + ((s.ticks & 1) ? 1 : 0);
            }
            if (p.flags & player_flag::dead) {
                riding_ &= u8(~bit);
                continue;
            }
            collide_platform(g, sprite_, s16(x_ + (x_offset_ >> 11)), s16(y_ + dy), p);
            if ((p.flags & player_flag::on_object) && p.standing_on == &sprite_) {
                riding_ |= bit;
                dip_ = u16(std::min(dip_ + 0x10, 0x100));
                const s32 lift =
                    (p.flags & (player_flag::rolling | player_flag::spin_dash)) ? 0xF : 0x14;
                p.y = (y_ + dy - lift) * 0x100;
            } else {
                riding_ &= u8(~bit);
                dip_ = dip_ <= 0x10 ? 0 : u16(dip_ - 0x10);
            }
        }
        if (out_of_range(s, x_, y_, sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        sprite_.y = s16(sprite_.y + divide(sine(a, dip_), 0x9C4));
        draw(g, sprite_);
        if (chained_) {
            draw(g, pivot_);
            const s32 step_x = divide(x_offset_, 3), step_y = divide(y_offset_, 3);
            for (s32 i = 1; i <= 2; ++i) {
                link_.x = s16(x_ - s.camera.x + ((step_x * i) >> 11));
                link_.y = s16(y_ - s.camera.y + ((step_y * i) >> 11));
                draw(g, link_);
            }
        }
        last_x_offset_ = x_offset_;
    }

private:
    Game& game_;
    Sprite sprite_;               // +0C
    Sprite link_;                 // +3C
    Sprite pivot_;                // +6C
    u16 phase_;                   // +9C
    s32 last_x_offset_{};         // +A0
    s32 x_offset_{}, y_offset_{}; // +A4, +A8 (11 fraction bits)
    u16 dip_{};                   // +B0
    s8 speed_;                    // record +4
    bool forward_;                // +B2
    u8 radius_;                   // +B3
    u8 riding_{};                 // +B6 a bit per player
    bool chained_;
};

// 08092EC8, 08092FC4: the special stage spring (story mode only). Landing
// on it (or Amy's hammer) throws the player up at 780h (hammer: 0B40h,
// sound AAh); unless the player's +6E is set, the player then rises with
// the movement routines off and the camera held (080931FC) until well
// above the view, and the special stage starts. The partner just bounces.
class SpecialSpring final : public Interactive {
public:
    SpecialSpring(Game& g, const ObjectSpawn& o) : Interactive(o), game_(g) {
        sprite_.start(g.sprite_tiles.allocate(0x18), 0x23C, 1, x_, y_, 0x480, 0x2000);
        animate(g, sprite_);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 0809340C

    void update(Game& g) override {
        StageState& s = g.stage();
        sprite_.x = s16(x_ - s.camera.x);
        sprite_.y = s16(y_ - s.camera.y);
        if (launched_) {
            rise(g, s);
            return;
        }
        if (sprite_.variant == 0 && (sprite_.flags & sprite_flag::ended)) {
            sprite_.variant = 1;
            sprite_.previous_variant = 0xFF;
        }
        bounce_partner(g, s);
        Player& p = s.player[0];
        if (!(p.flags & player_flag::dead)) {
            bool hammer = false;
            if ((collide_solid(g, sprite_, x_, y_, p) & solid::on_top) ||
                (hammer = hammer_hits(sprite_, x_, y_, p))) {
                land_player(g, p);
                p.flags = (p.flags & ~(player_flag::on_object | player_flag::jumped)) |
                          player_flag::in_air;
                p.speed_y = hammer ? -0xB40 : -0x780;
                p.state = 0x15;
                camera_x_ = s.camera.x;
                camera_y_ = s.camera.y;
                if (p.back_from_special == 0) {
                    p.flags |= player_flag::no_movement;
                    s.flags |= stage_flag::paused;
                }
                launch_picture();
                set_player_radius(g, p, 6, 0xE);
                g.sound().start(0xAA);
                launched_ = true;
            }
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
    void launch_picture() {
        sprite_.variant = 0;
        sprite_.previous_variant = 0xFF;
    }

    void bounce_partner(Game& g, StageState& s) {
        if (s.players != 2) {
            return;
        }
        Player& q = s.player[1];
        if (!(collide_solid(g, sprite_, x_, y_, q) & solid::on_top)) {
            return;
        }
        land_player(g, q);
        q.flags = (q.flags & ~(player_flag::on_object | player_flag::jumped)) | player_flag::in_air;
        q.speed_y = -0x780;
        q.state = 0x15;
        launch_picture();
        set_player_radius(g, q, 6, 0xE);
        g.sound().start(0xAA);
    }

    // 080931FC
    void rise(Game& g, StageState& s) {
        if (!near_view(sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        bounce_partner(g, s);
        Player& p = s.player[0];
        if (p.back_from_special == 0) {
            s.camera.x = camera_x_;
            s.camera.y = camera_y_;
            p.y -= 0x780;
            if ((p.y >> 8) - s.camera.y < -0x80) {
                // The act's restart point is kept; the special stage
                // returns to the spring.
                s.saved_start_x = p.start_x;
                s.saved_start_y = p.start_y;
                p.start_x = x_;
                p.start_y = y_;
                g.scheduler().destroy(*this);
                g.clear_screen_processes();
                start_special_stage_entry(g);
                return;
            }
            animate(g, sprite_);
        } else if (animate(g, sprite_) == 0) {
            sprite_.previous_variant = 0xFF;
            sprite_.animation = 0x23C;
            sprite_.variant = 1;
            launched_ = false;
            animate(g, sprite_);
        }
        draw(g, sprite_);
    }

    Game& game_;
    Sprite sprite_;               // +0C
    s16 camera_x_{}, camera_y_{}; // +3C, +3E the camera when launched
    bool launched_{};             // the update is 080931FC
};

// 0807E090, 0807E190: a springy platform (solid from above). Landing on
// it presses it down by 6/10 of the landing speed; it springs back
// (pulled 30h a frame towards its rest, its speed damped to 0.99), carrying
// whoever stands on it. Jumping off throws the player up at half its speed
// less 400h; down with jump curls the player up instead (Amy: drops
// through; down with the other button: Amy is thrown up at 600h).
class SpringyPlatform final : public Interactive {
public:
    SpringyPlatform(Game& g, const ObjectSpawn& o) : Interactive(o), game_(g) {
        sprite_.start(g.sprite_tiles.allocate(0x14), 0x1ED, 0, x_, y_, 0x480, 0x2000);
        animate(g, sprite_);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 0807E778

    void update(Game& g) override {
        StageState& s = g.stage();
        if (offset_ != 0) {
            speed_ += offset_ < 0 ? 0x30 : -0x30;
        }
        sprite_.x = s16(x_ - s.camera.x);
        sprite_.y = s16(y_ - s.camera.y + (offset_ >> 8));
        const s16 by = s16(y_ + (offset_ >> 8));
        for (unsigned n = 0; n < s.players; ++n) {
            step(g, s, s.player[n], by);
        }
        speed_ = divide(speed_ * 0x26AC, 10000);
        if (u32(speed_ + 0x2F) <= 0x5E && offset_ > -0x300 && offset_ <= 0x2FF) {
            speed_ = 0;
        }
        offset_ += speed_;
        if (out_of_range(s, x_, y_, sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        draw(g, sprite_);
    }

private:
    void step(Game& g, const StageState& s, Player& p, s16 by) {
        const s32 landing = p.speed_y;
        if (!((p.flags & player_flag::on_object) && p.standing_on == &sprite_)) {
            if (collide_platform(g, sprite_, x_, by, p) & solid::on_top) {
                speed_ += divide(landing * 6, 10);
            }
            return;
        }
        p.speed_y = 0;
        p.y += speed_;
        const bool down = p.held & 0x80;
        if ((p.pressed & s.other_buttons) && down) {
            if (p.character == 3) {
                p.flags &= ~player_flag::on_object;
                p.speed_y = -0x600;
            }
        } else if ((p.pressed & s.jump_buttons) && down) {
            if (p.character == 3) {
                p.flags &= ~player_flag::on_object;
            } else {
                set_player_radius(g, p, 6, 9);
            }
        } else if (p.pressed & s.jump_buttons) {
            p.speed_y = s16((speed_ >> 1) - 0x400);
            p.flags &= ~player_flag::on_object;
        } else {
            // Still on it?
            p.y += 0x800;
            p.speed_y = 1;
            if (collide_platform(g, sprite_, x_, by, p) == 0) {
                p.flags &= ~player_flag::on_object;
                p.y -= 0x800;
            }
            p.speed_y = 0;
        }
    }

    Game& game_;
    Sprite sprite_; // +0C
    s32 speed_{};   // +3C
    s32 offset_{};  // +44 below its place (8.8)
};

// 08020080, 0802032C: spikes pointing down: solid; hitting them from
// below hurts (08020E68). Drawn with fixed tiles.
class CeilingSpikes final : public Interactive {
public:
    CeilingSpikes(Game& g, const ObjectSpawn& o) : Interactive(o) {
        if (g.game_mode == 6) {
            throw Unrecovered("spikes in the chao hunt (0802032C)");
        }
        sprite_.start(0x11B00, 0x1C6, 2, x_, y_, 0x440, 0x2800);
        animate(g, sprite_);
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        sprite_.x = s16(x_ - s.camera.x);
        sprite_.y = s16(y_ - s.camera.y);
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if (s.flags & stage_flag::upside_down) {
                // 08020D14 (the floor spikes' touch): with gravity reversed
                // standing on their points hurts.
                touching_[p.index & 1] = collide_solid(g, sprite_, x_, y_, p);
                if ((touching_[p.index & 1] & solid::on_top) && hurt_player(g, p)) {
                    g.sound().start(0xAB);
                }
                continue;
            }
            if (p.flags & player_flag::no_movement) {
                continue;
            }
            touching_[p.index & 1] = collide_solid(g, sprite_, x_, y_, p);
            if ((touching_[p.index & 1] & solid::from_below) && hurt_player(g, p)) {
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
    std::array<u32, 2> touching_{}; // +3C
};

// 0807E78C, 0807E8E0: a block that comes and goes with the clock
// (record +4: the cycle, from 086C8CAC; +5 the phase; +3 its picture
// group). It is solid while shown; as it vanishes it drops who stands on
// it.
class TimedBlock final : public Interactive {
public:
    TimedBlock(Game& g, const ObjectSpawn& o)
        : Interactive(o), game_(g), group_(o.record[3]), cycle_(o.record[4] & 3),
          phase_(o.record[5]) {
        if (cycle_ == 3) {
            throw Unrecovered("a timed block with cycle 3 (reads beyond 086C8CAC)");
        }
        period_ = g.assets().u16(periods + cycle_ * 2u);
        hold_ = u8(s8((s32(period_) - 0xA1) >> 1));
        sprite_.start(g.sprite_tiles.allocate(0x10), 0x1EC, u8(group_ << 2), x_, y_, 0x480, 0x2000);
        animate(g, sprite_);
    }

    // 0807EB28
    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); }

    void update(Game& g) override {
        StageState& s = g.stage();
        sprite_.x = s16(x_ - s.camera.x);
        sprite_.y = s16(y_ - s.camera.y);
        if (!near_view(sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        const u32 t = u32(remainder(s32(s.ticks - phase_), period_));
        const u32 shown_until = hold_ + 0x3Au, leave = period_ - hold_;
        bool solid = true;
        const auto show = [&](u8 variant) {
            sprite_.previous_variant = 0xFF;
            sprite_.variant = u8((group_ << 2) + variant);
        };
        if (t < 0x3A) {
            if (t == 0) {
                show(1);
                solid = false;
            }
        } else if (t < shown_until) {
            if (t == 0x3A) {
                show(3);
            }
        } else if (t >= leave) {
            if (t == leave) {
                show(2);
            }
            solid = false;
            for (unsigned n = 0; n < s.players; ++n) {
                Player& p = s.player[n];
                if (touches_player(sprite_, x_, y_, p)) {
                    p.flags &= ~player_flag::on_object;
                }
            }
        } else if (t == shown_until) {
            show(0);
            solid = false;
        }
        if (solid) {
            for (unsigned n = 0; n < s.players; ++n) {
                collide_solid(g, sprite_, x_, y_, s.player[n]);
            }
        }
        animate(g, sprite_);
        draw(g, sprite_);
    }

private:
    static constexpr u32 periods = 0x086C8CAC; // three cycle lengths (frames)

    Game& game_;
    u8 group_; // +3C
    u8 cycle_; // +3E
    u8 phase_; // +40
    u16 period_{};
    u8 hold_{};     // +3F
    Sprite sprite_; // +0C
};

// 080845C0, 080842D0: an invisible box (record +5 x +6 eight-pixel units)
// that flings players inside it: across at 5 pixels a frame (record +3 bit
// 0: to the right), up at 7 (+3 below 3) or down at 6.
class FlingBox final : public Interactive {
public:
    explicit FlingBox(const ObjectSpawn& o) : Interactive(o), kind_(o.record[3]) {}

    void update(Game& g) override {
        StageState& s = g.stage();
        const u8* r = spawn_.record;
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            const s32 px = p.x >> 8, py = p.y >> 8;
            if ((p.flags & player_flag::dead) || px < x_ || px > x_ + r[5] * 8 || py < y_ ||
                py > y_ + r[6] * 8) {
                continue;
            }
            g.sound().start(0xB8);
            p.flags = (p.flags & 0xFFFFFEEFu) | player_flag::in_air;
            p.speed_x = (kind_ & 1) ? 0x500 : -0x500;
            p.speed_y = s8(kind_) < 3 ? -0x700 : 0x600;
            if ((p.character == 1 || p.character == 2) && p.special != 0) {
                end_player_moves(g, p);
                p.state = 5;
                p.flags |= player_flag::rolling;
                p.radius_x = 6;
                p.radius_y = 9;
            }
        }
        const auto& c = s.camera;
        if (x_ <= c.x + 0x170 && c.x - 0x80 <= x_ && y_ <= c.y + 0x120 && c.y - 0x80 <= y_) {
            return;
        }
        give_back();
        g.scheduler().destroy(*this);
    }

private:
    u8 kind_; // +3C
};

// 080833D0, 080834F8: a launcher at a tube's end: a player arriving
// through the tubes (stage set) within 18h pixels is caught and shot
// straight up (-500h, gravity 2Ah), shrinking, until the rise ends; the
// player then falls free. Solid to players not being launched.
class TubeLauncher final : public Interactive {
public:
    TubeLauncher(Game& g, const ObjectSpawn& o)
        : Interactive(o, u16(0x2100 + s8(o.record[4]))), game_(g),
          left_shift_(o.record[5] != 0 ? -0x18 : 0) {
        sprite_.start(g.sprite_tiles.allocate(0x10), 0x1E8, 0, s16(o.cell_x * 0x100 + 0x7F0), y_,
                      0x480, 0x2000);
        animate(g, sprite_);
    }

    // 08083B68
    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); }

    void update(Game& g) override {
        StageState& s = g.stage();
        sprite_.x = s16(x_ - s.camera.x);
        sprite_.y = s16(y_ - s.camera.y);
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            const u8 bit = u8(1 << n);
            if (p.flags & player_flag::dead) {
                continue;
            }
            if (launching_ & bit) {
                launch(g, s, p, n);
                continue;
            }
            const s32 px = p.x >> 8, py = p.y >> 8, left = x_ + left_shift_;
            if (p.tube_stage != 0x7F && left <= px && px <= left + 0x18 && y_ - 0x18 <= py &&
                py <= y_ + 0x18) {
                start_x_[n] = p.x;
                start_y_[n] = p.y;
                end_player_moves(g, p);
                p.flags = (p.flags & ~player_flag::jumped) | 0x600016u;
                p.speed_y = -0x500;
                p.speed_x = 0;
                p.ground_speed = 0;
                p.held = 0;
                p.pressed = 0;
                set_player_radius(g, p, 6, 0xE);
                p.radius_x = 6;
                p.radius_y = 0xE;
                g.sound().start(0xAA);
                p.tube_stage = 0x7F;
                launching_ |= bit;
                p.power |= 0x80;
            }
            if (!(p.flags & player_flag::frozen)) {
                collide_solid(g, sprite_, x_, y_, p);
            }
        }
        if (near_view(sprite_.x, sprite_.y)) {
            animate(g, sprite_);
            draw(g, sprite_);
            return;
        }
        for (unsigned n = 0; n < s.players; ++n) {
            if (launching_ & (1 << n)) {
                Player& p = s.player[n];
                p.flags &= ~player_flag::frozen;
                p.held = u16(p.held | s.jump_buttons | s.other_buttons);
                p.flags &= ~player_flag::no_movement;
            }
        }
        give_back();
        g.scheduler().destroy(*this);
    }

private:
    void launch(Game& g, const StageState& s, Player& p, unsigned n) {
        p.speed_y = s16(p.speed_y + 0x2A);
        start_y_[n] += p.speed_y;
        p.y = start_y_[n];
        p.x = start_x_[n];
        p.scale_x = s16(p.scale_x - 4);
        if (p.scale_x < 0xB0) {
            p.scale_x = 0xB0;
        }
        p.flags |= player_flag::frozen | player_flag::no_movement;
        p.scale_y = p.scale_x;
        if (p.speed_y > 0) {
            end_player_moves(g, p);
            p.state = 5;
            p.flags &= ~player_flag::frozen;
            p.held = u16(p.held | s.jump_buttons | s.other_buttons);
            p.flags &= ~player_flag::no_movement;
            launching_ &= u8(~(1 << n));
            p.scale_x = p.scale_y = 0x100;
            p.power &= 0x7F;
        }
    }

    Game& game_;
    s16 left_shift_;
    Sprite sprite_;                // +0C
    u8 launching_{};               // +40 bit per player
    std::array<s32, 2> start_x_{}; // +44
    std::array<s32, 2> start_y_{}; // +4C
};

// 080946C0, 08094754: a turntable (record +3 its exits): a player within
// 10 pixels is caught, curled up and turned about its middle until
// pressing an exit's direction, which fires the player that way (9 pixels
// a frame; type 1 exits up and to either side). For 20 frames after, it
// cannot catch that player again.
class Turntable final : public Interactive {
public:
    explicit Turntable(const ObjectSpawn& o) : Interactive(o), exits_(o.record[3]) {}

    void update(Game& g) override {
        StageState& s = g.stage();
        const Assets& a = g.assets();
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            const u8 bit = u8(1 << n);
            if (p.flags & player_flag::dead) {
                continue;
            }
            if (!(holding_ & bit)) {
                if (cooldown_[n] != 0) {
                    --cooldown_[n];
                    continue;
                }
                const s32 px = p.x >> 8, py = p.y >> 8;
                if (x_ - 10 <= px && px <= x_ + 10 && y_ - 10 <= py && py <= y_ + 10) {
                    p.flags |= player_flag::no_movement | player_flag::rolling;
                    turn_[n] = 0;
                    p.x = x_ * 0x100;
                    p.y = y_ * 0x100;
                    p.speed_x = p.speed_y = p.ground_speed = 0;
                    set_player_radius(g, p, 6, 0xE);
                    p.radius_x = 6;
                    p.radius_y = 0xE;
                    g.sound().start(0xAA);
                    holding_ |= bit;
                }
                continue;
            }
            s16 out_x = 0, out_y = 0;
            s32 lift = 0;
            const u16 pressed = p.pressed;
            switch (exits_) {
            case 0:
                lift = 4;
                if (pressed & 0x10) {
                    out_x = 0x900;
                } else if (pressed & 0x80) {
                    out_y = 0x900;
                }
                break;
            case 1:
                if (pressed & 0x10) {
                    out_x = 0x700;
                    out_y = 0x800;
                } else if (pressed & 0x20) {
                    out_x = -0x900;
                    out_y = 0x700;
                }
                break;
            case 2:
                if (pressed & 0x10) {
                    out_x = 0x900;
                }
                break;
            case 3:
                if (pressed & 0x80) {
                    out_y = 0x900;
                }
                break;
            default:
                break;
            }
            const u16 before = turn_[n];
            turn_[n] = u16(before + 4);
            if (turn_[n] > 0x400) {
                turn_[n] = u16(before - 0x3FC);
            }
            p.x = x_ * 0x100 + divide(sine(a, (turn_[n] & 0x7Fu) * 8 + 0x100), 0x10);
            p.y = (y_ - lift) * 0x100 + divide(sine(a, (turn_[n] & 0x7Fu) * 8), 0x10);
            end_player_moves(g, p);
            p.state = 5;
            set_player_radius(g, p, 6, 0xE);
            p.radius_x = 6;
            p.radius_y = 0xE;
            p.flags |= player_flag::no_movement | player_flag::in_air | player_flag::rolling;
            if (out_x != 0 || out_y != 0) {
                p.x = x_ * 0x100;
                p.y = y_ * 0x100;
                cooldown_[n] = 0x14;
                holding_ &= u8(~bit);
                p.flags &= ~player_flag::no_movement;
                p.angle = 0;
                p.speed_x = out_x;
                p.speed_y = out_y;
                p.ground_speed = out_x;
            }
        }
        if ((holding_ & 3) == 0 && !near_view(s16(x_ - s.camera.x), s16(y_ - s.camera.y))) {
            give_back();
            g.scheduler().destroy(*this);
        }
    }

private:
    u8 exits_;                     // +3D
    u8 holding_{};                 // +3C bit per player
    std::array<u8, 2> cooldown_{}; // +3E
    std::array<u16, 2> turn_{};    // +40
};

// 0807EFF4, 0807F1F8: a platform hanging from a pivot on five links,
// swinging; the pivot's top and the platform carry players (a player who
// leaves the platform cannot land on it again for 5 frames). The platform
// is drawn twice, the second mirrored.
class HangingSwing final : public Interactive {
public:
    HangingSwing(Game& g, const ObjectSpawn& o) : Interactive(o, 0x7000), game_(g) {
        platform_.start(g.sprite_tiles.allocate(0x1E), 0x1EB, 0, x_, y_, 0x100, 0x2000);
        pivot_.start(g.sprite_tiles.allocate(9), 0x1EA, 0, x_, y_, 0x480, 0x2000);
        link_.start(g.sprite_tiles.allocate(1), 0x1E9, 0, x_, y_, 0x480, 0x2000);
        animate(g, platform_);
        animate(g, pivot_);
        animate(g, link_);
    }

    // 0807F670
    void on_destroy() override {
        game_.sprite_tiles.free(platform_.tiles.to);
        game_.sprite_tiles.free(link_.tiles.to);
        game_.sprite_tiles.free(pivot_.tiles.to);
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        const Assets& a = g.assets();
        u32 t = u32(remainder(s32(s.ticks << 1), 0x200));
        s8 lean;
        if (t < 0x101) {
            lean = s8(-s8(u16(sine(a, t * 2)) >> 8));
        } else {
            t = 0x200 - t;
            lean = s8(u16(sine(a, t * 2)) >> 8);
        }
        const u32 angle = u32(s32(sine(a, ((t - 0x7E) & 0x1FF) * 2)) >> 7) & 0x1FF;
        const s32 sin = sine(a, angle * 2), cos = sine(a, angle * 2 + 0x100);
        const s16 sx = s16(x_ - s.camera.x), sy = s16(y_ - s.camera.y);
        platform_.x = pivot_.x = sx;
        platform_.y = pivot_.y = sy;
        const auto& c = s.camera;
        if (!(x_ <= c.x + 0x170 && c.x - 0x80 <= x_ && y_ <= c.y + 0x120 && c.y - 0x80 <= y_) &&
            !near_view(platform_.x, platform_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        draw(g, pivot_);
        s32 along = 0, down = 0;
        for (int k = 0; k < 5; ++k) {
            along += divide(sin * 0x6E, 100);
            down += divide((cos + lean) * 0x6E, 100);
            link_.x = s16(sx + (along >> 10));
            link_.y = s16(sy + (down >> 10));
            link_.layer = k == 4 ? 0xC0 : 0x480;
            draw(g, link_);
        }
        platform_.x = link_.x;
        platform_.y = link_.y;
        const s32 px_platform = x_ + (along >> 10), py_platform = y_ + (down >> 10);
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            const u8 bit = u8(1 << n);
            if (p.flags & player_flag::dead) {
                continue;
            }
            if (p.y < (y_ - 4) * 0x100) {
                if (collide_platform(g, pivot_, x_, y_, p) & solid::on_top) {
                    on_pivot_ |= bit;
                } else if (on_pivot_ & bit) {
                    on_pivot_ &= u8(~bit);
                    p.flags &= ~(player_flag::on_object | player_flag::pushing);
                }
            }
            if (cooldown_[n] != 0) {
                --cooldown_[n];
                continue;
            }
            if (!(collide_platform(g, platform_, s16(px_platform), s16(py_platform), p) &
                  solid::on_top)) {
                if (on_platform_ & bit) {
                    on_platform_ &= u8(~bit);
                    cooldown_[n] = 5;
                    p.flags &= ~(player_flag::on_object | player_flag::pushing);
                }
                continue;
            }
            if (!(on_platform_ & bit)) {
                on_platform_ |= bit;
            } else {
                p.x += (px_platform - last_x_[n]) * 0x100;
                p.y = (py_platform - 5) * 0x100;
            }
            last_x_[n] = px_platform;
        }
        draw(g, platform_);
        platform_.flags |= sprite_flag::flip_x;
        draw(g, platform_);
        platform_.flags &= ~sprite_flag::flip_x;
    }

private:
    Game& game_;
    Sprite platform_;              // +0C
    Sprite link_;                  // +3C
    Sprite pivot_;                 // +6C
    std::array<s32, 2> last_x_{};  // +9C
    u8 on_platform_{};             // +A4
    std::array<u8, 2> cooldown_{}; // +A5
    u8 on_pivot_{};                // +A7
};

// 08077258: a launcher on the ground (record +4 set: facing left). A
// player on the ground reaching it fast enough (300h, rolling 540h) the
// way it faces is taken (state 1Eh), pushed through it over 14 frames
// (08077468) and let go at least A80h fast; then it resets (08077620).
class GroundLauncher final : public Interactive {
public:
    GroundLauncher(Game& g, const ObjectSpawn& o)
        : Interactive(o), game_(g), leftward_(o.record[4] != 0) {
        sprite_.start(g.sprite_tiles.allocate(0xC), 0x1D5, 0, x_, y_, 0x480,
                      leftward_ ? 0x2400 : 0x2000);
        animate(g, sprite_);
    }

    // 08078040
    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); }

    void update(Game& g) override {
        StageState& s = g.stage();
        sprite_.x = s16(x_ - s.camera.x);
        sprite_.y = s16(y_ - s.camera.y);
        if (state_ == State::waiting) {
            if (!(s.player[0].flags & player_flag::dead) &&
                (leftward_ ? take_leftward(g, s) : take_rightward(g, s))) {
                state_ = State::pushing;
            }
            if (!near_view(sprite_.x, sprite_.y)) {
                give_back();
                g.scheduler().destroy(*this);
                return;
            }
            animate(g, sprite_);
            draw(g, sprite_);
            return;
        }
        if (!near_view(sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        if (state_ == State::pushing) {
            push(s);
        } else if (animate(g, sprite_) == 0) {
            // 08077620
            count_ = 0;
            sprite_.previous_variant = 0xFF;
            sprite_.animation = 0x1D5;
            sprite_.variant = 0;
            state_ = State::waiting;
            draw(g, sprite_);
            return;
        } else {
            draw(g, sprite_);
            return;
        }
        animate(g, sprite_);
        draw(g, sprite_);
    }

private:
    enum class State : u8 { waiting, pushing, resetting };

    bool fast_enough(const Player& p, bool leftward) const {
        const s16 need = (p.flags & player_flag::rolling) ? 0x540 : 0x300;
        return leftward ? (p.ground_speed < -need && (p.flags & player_flag::facing_left))
                        : (p.ground_speed > need && !(p.flags & player_flag::facing_left));
    }

    void take(Game& g, StageState& s, Player& p, s32 x) {
        end_player_moves(g, p);
        g.sound().start(0xAF);
        p.x = x;
        p.flags |= player_flag::no_movement;
        s.player[0].power |= 0x80;
        sprite_.previous_variant = 0xFF;
        sprite_.animation = 0x1D5;
        sprite_.variant = 1;
        rider_ = p.index;
        state_before_ = p.state;
        p.state = 0x1E;
    }

    // 08077DBC: the first player that qualifies.
    bool take_rightward(Game& g, StageState& s) {
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if (touches_player(sprite_, x_, y_, p) && !(p.flags & player_flag::in_air) &&
                fast_enough(p, false)) {
                take(g, s, p, (x_ - 0xF) * 0x100);
                return true;
            }
        }
        return false;
    }

    // 08077CAC: every player that qualifies.
    bool take_leftward(Game& g, StageState& s) {
        bool taken = false;
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if (touches_player(sprite_, x_, y_, p) && !(p.flags & player_flag::in_air) &&
                fast_enough(p, true)) {
                take(g, s, p, (x_ + 0xF) * 0x100);
                taken = true;
            }
        }
        return taken;
    }

    // 08077468
    void push(StageState& s) {
        Player& p = s.player[rider_];
        if (s.player[0].flags & player_flag::dead) {
            p.flags &= ~player_flag::no_movement;
            s.player[0].power &= 0x7F;
            return;
        }
        if (count_ > 6) {
            const s32 step = count_ == 7 ? 0xA00 : 0x780;
            p.x += leftward_ ? -step : step;
        }
        if (++count_ <= 0xD) {
            return;
        }
        p.state = state_before_;
        const u32 f = p.flags;
        p.flags = f & ~player_flag::rolling;
        if (p.state == 9) {
            p.state = 4;
        }
        if (!(f & player_flag::facing_left)) {
            if (p.ground_speed < 0xA80) {
                p.ground_speed = p.speed_x = 0xA80;
            }
        } else if (p.ground_speed > -0xA80) {
            p.ground_speed = p.speed_x = -0xA80;
        }
        p.flags &= ~player_flag::no_movement;
        s.player[0].power &= 0x7F;
        state_ = State::resetting;
    }

    Game& game_;
    bool leftward_;     // +3C
    Sprite sprite_;     // +0C
    u8 count_{};        // +3D
    u8 state_before_{}; // +3E
    u8 rider_{};        // +3F
    State state_ = State::waiting;
};

// 080776F4: a launcher pointing up: a player touching it (not Tails or
// Knuckles in a special move) is taken (state 1Fh), lifted and dropped back
// along 086C8C68 over 14 frames (08077A24), then fired up at 780h; then it
// resets (08077BA4). Its picture differs outside the first zone.
class UpLauncher final : public Interactive {
public:
    UpLauncher(Game& g, const ObjectSpawn& o) : Interactive(o), game_(g) {
        const s8 z = g.stage().zone;
        animation_ = ((z >> 1) == 0 || z == 0xE) ? 0x1D4 : 0x1D6;
        sprite_.start(g.sprite_tiles.allocate(0xC), animation_, 0, x_, y_, 0x480, 0x2000);
    }

    // 08078040
    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); }

    void update(Game& g) override {
        StageState& s = g.stage();
        sprite_.x = s16(x_ - s.camera.x);
        sprite_.y = s16(y_ - s.camera.y);
        if (state_ == State::waiting) {
            // 08077964
            if (!(s.player[0].flags & player_flag::dead) && take(g, s)) {
                state_ = State::lifting;
            }
            if (!near_view(sprite_.x, sprite_.y)) {
                give_back();
                g.scheduler().destroy(*this);
                return;
            }
            animate(g, sprite_);
            draw(g, sprite_);
            return;
        }
        if (!near_view(sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        if (state_ == State::lifting) {
            lift(g, s);
            animate(g, sprite_);
        } else if (animate(g, sprite_) == 0) {
            // 08077BA4
            count_ = 0;
            sprite_.previous_variant = 0xFF;
            sprite_.animation = animation_;
            sprite_.variant = 0;
            state_ = State::waiting;
        }
        draw(g, sprite_);
    }

private:
    enum class State : u8 { waiting, lifting, resetting };

    // 08077EEC
    bool take(Game& g, StageState& s) {
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if (!touches_player(sprite_, x_, y_, p) ||
                (u8(p.character - 1) <= 1 && p.special != 0)) {
                continue;
            }
            end_player_moves(g, p);
            g.sound().start(0xAF);
            p.flags |= player_flag::no_movement;
            s.player[0].power |= 0x80;
            p.y = (y_ + 0x10) * 0x100;
            if (!(sprite_.flags & sprite_flag::flip_x)) {
                p.x = (x_ + 8) * 0x100;
                p.flags |= player_flag::facing_left;
            } else {
                p.x = (x_ - 8) * 0x100;
                p.flags &= ~player_flag::facing_left;
            }
            sprite_.previous_variant = 0xFF;
            sprite_.animation = animation_;
            sprite_.variant = 1;
            rider_ = p.index;
            state_before_ = p.state;
            p.state = 0x1F;
            return true;
        }
        return false;
    }

    // 08077A24
    void lift(Game& g, StageState& s) {
        Player& p = s.player[rider_];
        if (s.player[0].flags & player_flag::dead) {
            p.flags &= ~player_flag::no_movement;
            s.player[0].power &= 0x7F;
            return;
        }
        const s32 step = g.assets().s16(heights + count_ * 2u);
        p.y += count_ < 7 ? step * 0x100 : -step * 0x100;
        if (++count_ <= 0xD) {
            return;
        }
        p.flags = (p.flags & 0xFFFFFEEBu) | player_flag::in_air;
        set_player_radius(g, p, 6, 0xE);
        p.radius_x = 6;
        p.radius_y = 0xE;
        p.state = 0x15;
        p.speed_y = -0x780;
        p.flags &= ~player_flag::no_movement;
        s.player[0].power &= 0x7F;
        state_ = State::resetting;
    }

    static constexpr u32 heights = 0x086C8C68; // 15 steps (pixels)

    Game& game_;
    u16 animation_;
    Sprite sprite_;     // +0C
    u8 count_{};        // +3D
    u8 state_before_{}; // +3E
    u8 rider_{};        // +3F
    State state_ = State::waiting;
};

// 08021C70, 08021D64: a spring on a ceiling: a player hitting it from
// below is pushed down (record +3: one of four strengths from 080B84FC, or
// 4 and up: +4 x 16); it bounces (08021E40). Solid otherwise; standing on
// or pushing it lands the player.
class CeilingSpring final : public Interactive {
public:
    CeilingSpring(Game& g, const ObjectSpawn& o) : Interactive(o), game_(g) {
        sprite_.start(g.sprite_tiles.allocate(0x10), 0x1C7, 0, x_, y_, 0x480, 0x2800);
        animate(g, sprite_);
    }

    // 0802313C
    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); }

    void update(Game& g) override {
        StageState& s = g.stage();
        const bool bouncing = bouncing_;
        for (unsigned n = 0; n < s.players; ++n) {
            // (With gravity reversed it throws like a spring on the ground.)
            const bool thrown =
                (s.flags & stage_flag::upside_down)
                    ? spring_throws_up(g, sprite_, x_, y_, spawn_.record, s.player[n])
                    : spring_throws_down(g, sprite_, x_, y_, spawn_.record, s.player[n]);
            if (thrown) {
                bouncing_ = true;
            }
        }
        if (!near_view(sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        if (bouncing && animate(g, sprite_) == 0) {
            sprite_.variant = 0;
            animate(g, sprite_);
            bouncing_ = false;
        }
        draw(g, sprite_);
    }

private:
    Game& game_;
    Sprite sprite_;   // +0C
    bool bouncing_{}; // the update is 08021E40
};

} // namespace

// 0806D6B4, 0806D788: a bomb tossed straight up (at 3B0h, falling at 2Ch
// a frame); just after the top of its flight it bursts (0806D868: the
// burst drawn four ways mirrored, for 21 frames). It hurts players it
// touches.
class Bomb final : public Process {
public:
    Bomb(Game& g, s16 x, s16 y) : Process(0x3000), game_(g), x4_(s32(x) * 4) {
        sprite_.start(g.sprite_tiles.allocate(0x10), 0x1A3, 0, x, y, 0x240, 0x2000);
        animate(g, sprite_);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 0806D964

    void update(Game& g) override {
        const auto& c = g.stage().camera;
        sprite_.x = s16(divide(x4_, 4));
        if (!bursting_) {
            speed_y_ = s16(speed_y_ + 0x2C);
        }
        sprite_.y = s16(sprite_.y + s8(u16(speed_y_) >> 8));
        const s16 x = sprite_.x, y = sprite_.y;
        projectile_hits(g, sprite_, x, y);
        sprite_.x = s16(x - c.x);
        sprite_.y = s16(y - c.y);
        const bool shown =
            u16(sprite_.x + 0x3C) < 0x169 && sprite_.y + 0x3C >= 0 && sprite_.y <= 0x12C;
        if (!bursting_) {
            if (!shown) {
                g.scheduler().destroy(*this);
                return;
            }
            animate(g, sprite_);
            draw(g, sprite_);
            if (speed_y_ > -0x10) {
                sprite_.animation = 0x1A0;
                sprite_.variant = 0;
                sprite_.previous_variant = 0xFF;
                bursting_ = true;
            }
        } else {
            if (!shown || ++timer_ >= 0x15) {
                g.scheduler().destroy(*this);
                return;
            }
            animate(g, sprite_);
            draw(g, sprite_);
            sprite_.flags |= sprite_flag::flip_y;
            draw(g, sprite_);
            sprite_.flags |= sprite_flag::flip_x;
            draw(g, sprite_);
            sprite_.flags &= ~sprite_flag::flip_y;
            draw(g, sprite_);
            sprite_.flags &= ~sprite_flag::flip_x;
        }
        sprite_.x = x;
        sprite_.y = y;
    }

private:
    Game& game_;
    Sprite sprite_;        // +00 (x, y in the act between frames)
    s32 x4_;               // +30 (quarter pixels)
    s16 speed_y_ = -0x3B0; // +34
    u8 timer_{};           // +36
    bool bursting_{};      // the update is 0806D868
};

// 0806D1D0, 0806D2F0: walks to and fro on the ground within its range
// (record +3: the start, +5: the width, in 8 pixels) at 0A0h; when the
// player is within 100 pixels it stops to toss a bomb (0806D50C: on the
// 7th of 19 frames; then 100 frames before the next).
class Bomber final : public Enemy {
public:
    Bomber(Game& g, const ObjectSpawn& o) : Enemy(g, o, 0x14) {
        sprite_.start(tiles_, 0x19F, 0, s16(base_x_), s16(base_y_), 0x480, 0x2000);
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
        if (out_of_range(s, base_x_, base_y_, sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        if (enemy_touched(g, sprite_, x, y)) {
            g.scheduler().destroy(*this);
            return;
        }
        offset_y_ += probe_short(g.assets(), s.map, y, x, 1, 8, nullptr, Sensor::floor);
        if (throwing_) {
            throw_bomb(g, x, y);
        } else {
            walk(s, x, y);
        }
        animate(g, sprite_);
        draw(g, sprite_);
    }

private:
    void walk(const StageState& s, s16 x, s16 y) {
        const Player& p = s.player[0];
        const s16 dx = s16((p.x >> 8) - x), dy = s16((p.y >> 8) - y);
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
        } else if (s32(s16(dx * dx)) + s32(s16(dy * dy)) < 10000) {
            // (The squares wrap at 16 bits, as the original computes.)
            timer_ = 0;
            cooldown_ = 100;
            sprite_.variant = 1;
            sprite_.previous_variant = 0xFF;
            throwing_ = true;
        }
    }

    // 0806D50C
    void throw_bomb(Game& g, s16 x, s16 y) {
        if (++timer_ == 7) {
            const bool right = sprite_.flags & sprite_flag::flip_x;
            g.scheduler().create<Bomb>(g, s16(right ? x + 4 : x - 6), s16(y - 0x14));
        }
        if (timer_ == 0x13) {
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
    bool throwing_{};   // the update is 0806D50C
};

// 0806D978, 0806DA88: walks to and fro on the ground within its range
// (record +3: the start, +5: the width, in 8 pixels) at 0A0h, 48 pixels
// above its place, with a spiked part (its second hitbox): touching that
// hurts the player, unless rolling, when the player bounces back off it.
class Spiker final : public Enemy {
public:
    Spiker(Game& g, const ObjectSpawn& o) : Enemy(g, o, 0x24) {
        sprite_.start(tiles_, 0x1A2, 0, s16(base_x_), s16(base_y_), 0x480, 0x2000);
        animate(g, sprite_);
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        offset_x_ += speed_;
        const s16 x = s16(base_x_ + (offset_x_ >> 8));
        const s16 y = s16(base_y_ + offset_y_ - 0x30);
        sprite_.x = s16(x - s.camera.x);
        sprite_.y = s16(y - s.camera.y);
        if (out_of_range(s, base_x_, base_y_, sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        Player& p = s.player[0];
        if (!(p.flags & player_flag::dead)) {
            const Hitbox& spikes = sprite_.hitboxes[1];
            if (spikes.index != 0xFFFFFFFF &&
                hitboxes_meet(x, y, spikes, p.x >> 8, p.y >> 8, p.body->sprite.hitboxes[0])) {
                if (!(p.flags & player_flag::rolling)) {
                    hurt_player(g, p);
                } else {
                    // Bounced back.
                    s32 d;
                    if (!(p.flags & player_flag::facing_left)) {
                        d = -std::abs(s32(p.ground_speed));
                        p.flags |= player_flag::facing_left;
                    } else {
                        d = std::abs(s32(p.ground_speed));
                        p.flags &= ~player_flag::facing_left;
                    }
                    p.x += d;
                    p.ground_speed = s16(-p.ground_speed);
                    p.speed_x = s16(-p.speed_x);
                }
            }
            if (enemy_touched(g, sprite_, x, y)) {
                g.scheduler().destroy(*this);
                return;
            }
        }
        offset_y_ =
            s16(offset_y_ + probe_short(g.assets(), s.map, y, x, 1, 8, nullptr, Sensor::floor));
        const s32 along = offset_x_ >> 8;
        if (along <= param(3) * 8) {
            speed_ = 0xA0;
            sprite_.flags |= sprite_flag::flip_x;
        } else if (along >= (param(3) + spawn_.record[5]) * 8) {
            speed_ = -0xA0;
            sprite_.flags &= ~sprite_flag::flip_x;
        }
        animate(g, sprite_);
        draw(g, sprite_);
    }

private:
    Sprite sprite_;     // +0C
    s32 offset_x_{};    // +44 8.8
    s16 speed_ = -0xA0; // +48
    s16 offset_y_{};    // +4A
};

// 0806E1E8, 0806E298: a chip dropped from above, falling at 28h a frame;
// on the ground (or hitting a player) it shatters into six pieces thrown
// apart by the slope and the fall's speed, which bounce for 45 frames
// (0806E4E8; blinking at the end). The chip hurts players it touches.
class Chip final : public Process {
public:
    Chip(Game& g, s16 x, s16 y) : Process(0x3000) {
        pieces_[0].x = s32(x) << 8;
        sprite_.start(0x12600, 0x1A4, 0, x, y, 0x240, 0x2000);
        animate(g, sprite_);
    }

    void update(Game& g) override {
        if (timer_ != 0) {
            fly_apart(g);
        } else {
            fall(g);
        }
    }

private:
    struct Piece {
        s32 x{};        // +34 8.8
        s16 speed_y{};  // +4C
        s16 sx{}, sy{}; // +58, +64 act pixels
        s32 speed_x{};  // +70
        bool alive{};   // +88
    };

    static bool shown(s16 sx, s16 sy) {
        return u16(sx + 0x14) < 0x115 && sy + 0x14 >= 0 && sy < 0xF1;
    }

    void fall(Game& g) {
        StageState& s = g.stage();
        const Assets& a = g.assets();
        Piece& chip = pieces_[0];
        sprite_.x = s16(divide(chip.x, 0x100));
        chip.speed_y = s16(chip.speed_y + 0x28);
        sprite_.y = s16(sprite_.y + s8(u16(chip.speed_y) >> 8));
        const s16 x = sprite_.x, y = sprite_.y;
        const bool hit = projectile_hits(g, sprite_, x, y);
        sprite_.x = s16(x - s.camera.x);
        sprite_.y = s16(y - s.camera.y);
        if (!shown(sprite_.x, sprite_.y)) {
            g.scheduler().destroy(*this);
            return;
        }
        u8 angle = 0;
        const s32 d = probe_short(a, s.map, y, x, 1, 8, &angle, Sensor::floor);
        if (d < 1 || hit) {
            if (hit) {
                angle = 0;
            }
            const s16 fall_speed = chip.speed_y;
            const s32 x8 = chip.x;
            timer_ = 0x2D;
            for (unsigned i = 0; i < pieces_.size(); ++i) {
                Piece& p = pieces_[i];
                const s32 spread =
                    divide(fall_speed * -10, remainder(s32(s.next_random()), 4) + 0x10);
                p.speed_y = s16(quotient(sine(a, (angle & 0x1F) * 4u + 0x100) * spread, 20000));
                p.sx = x;
                p.sy = s16(y + d);
                p.x = x8;
                p.speed_x = divide(sine(a, ((i * 8 - 0x18 + angle) * 4) & 0x3FF), 0x1E);
                p.alive = true;
            }
            sprite_.previous_variant = 0xFF;
            sprite_.tiles.to = 0x12680;
            sprite_.animation = 0x1A5;
            sprite_.variant = 0;
        }
        animate(g, sprite_);
        draw(g, sprite_);
        sprite_.x = x;
        sprite_.y = y;
    }

    // 0806E4E8
    void fly_apart(Game& g) {
        StageState& s = g.stage();
        const Assets& a = g.assets();
        animate(g, sprite_);
        if (--timer_ == 0) {
            g.scheduler().destroy(*this);
            return;
        }
        for (Piece& p : pieces_) {
            if (!p.alive) {
                continue;
            }
            p.speed_y = s16(divide(p.speed_y * 1000, 0x415 - timer_));
            p.x += p.speed_x;
            p.sx = s16(p.x >> 8);
            p.speed_y = s16(p.speed_y + 0x28);
            p.sy = s16(p.sy + s8(u16(p.speed_y) >> 8));
            const s16 x = p.sx, y = p.sy;
            sprite_.x = s16(x - s.camera.x);
            sprite_.y = s16(y - s.camera.y);
            if (!shown(sprite_.x, sprite_.y)) {
                p.alive = false;
                continue;
            }
            u8 angle = 0;
            const s32 d = probe_short(a, s.map, y, x, 1, 8, &angle, Sensor::floor);
            if (d < 1) {
                // Bounces.
                p.sy = s16(p.sy + d);
                p.speed_y = s16(divide(p.speed_y * -10, 0x15));
                p.speed_x += divide(sine(a, angle * 4u), 0x1E);
            }
            if (timer_ >= 0x1E || (timer_ & 2)) {
                draw(g, sprite_);
            }
            sprite_.x = x;
            sprite_.y = y;
        }
    }

    Sprite sprite_; // +00 (x, y in the act between frames)
    s16 timer_{};   // +30 frames left in pieces
    std::array<Piece, 6> pieces_{};
};

// 0806DD6C, 0806DE98: hovers to and fro within its range (record +3: the
// start, +5: the width, in 8 pixels) at 0A0h, bobbing; when the player is
// below (within 16 pixels across) it drops a chip (0806E07C: 20 frames; then
// 100 frames before the next). Drawn twice, the second mirrored. (Not in
// Casino Paradise when save byte +18 is set.)
class Dropper final : public Enemy {
public:
    Dropper(Game& g, const ObjectSpawn& o) : Enemy(g, o, 0xF) {
        sprite_.start(tiles_, 0x1A1, 0, s16(base_x_), s16(base_y_), 0x480, 0x2000);
        animate(g, sprite_);
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        if (!dropping_) {
            phase_ = u16((phase_ + 1) & 0x1F);
            bob_ = u16(divide(sine(g.assets(), phase_ * 0x10u), 800));
            offset_x_ = u16(offset_x_ + speed_);
        }
        const s16 x = s16(base_x_ + s8(offset_x_ >> 8));
        const s16 y = s16(base_y_ - bob_);
        sprite_.x = s16(x - s.camera.x);
        sprite_.y = s16(y - s.camera.y);
        if (out_of_range(s, base_x_, base_y_, sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        if (enemy_touched(g, sprite_, x, y)) {
            g.scheduler().destroy(*this);
            return;
        }
        if (dropping_) {
            if (++timer_ > 0x13) {
                timer_ = 0;
                sprite_.variant = 0;
                dropping_ = false;
            }
        } else {
            hover(g, s, x, y);
        }
        animate(g, sprite_);
        draw(g, sprite_);
        sprite_.flags |= sprite_flag::flip_x;
        draw(g, sprite_);
        sprite_.flags &= ~sprite_flag::flip_x;
    }

private:
    void hover(Game& g, const StageState& s, s16 x, s16 y) {
        const s32 along = s8(offset_x_ >> 8);
        if (along <= param(3) * 8) {
            speed_ = 0xA0;
        } else if (along >= (param(3) + spawn_.record[5]) * 8) {
            speed_ = -0xA0;
        } else if (cooldown_ != 0) {
            --cooldown_;
        } else {
            const s32 px = s.player[0].x >> 8;
            if (px <= x + 0x10 && px >= x - 0x10) {
                cooldown_ = 100;
                sprite_.variant = 1;
                g.scheduler().create<Chip>(g, x, s16(y - 6));
                dropping_ = true;
            }
        }
    }

    Sprite sprite_;     // +0C
    u16 offset_x_{};    // +3C 8.8
    s16 speed_ = -0xA0; // +3E
    s16 timer_{};       // +40
    s16 cooldown_{};    // +42
    u16 bob_{};         // +44
    u16 phase_{};       // +46
    bool dropping_{};   // the update is 0806E07C
};

bool create_casino_paradise_object(Game& g, u32 creator, const ObjectSpawn& o) {
    switch (creator) {
    case 0x0807EB3C:
        g.scheduler().create<BouncePadCP>(g, o);
        return true;
    case 0x08078E2C:
        g.scheduler().create<Bumper>(g, o);
        return true;
    case 0x08020080:
        g.scheduler().create<CeilingSpikes>(g, o);
        return true;
    case 0x080786CC:
        g.scheduler().create<SlidingBumper>(g, o);
        return true;
    case 0x08079650:
        g.scheduler().create<OrbitingBumper>(g, o);
        return true;
    case 0x08079ED8:
        g.scheduler().create<ShapedBumper>(g, o, ShapedBumper::Shape::dome);
        return true;
    case 0x0807A000:
        g.scheduler().create<ShapedBumper>(g, o, ShapedBumper::Shape::side_dome);
        return true;
    case 0x0807A12C:
        g.scheduler().create<ShapedBumper>(g, o, ShapedBumper::Shape::corner);
        return true;
    case 0x0807B9A8:
        g.scheduler().create<Flipper>(g, o);
        return true;
    case 0x0807C8FC:
        g.scheduler().create<FlatFlipper>(g, o);
        return true;
    case 0x0807CE98:
        g.scheduler().create<SwingingBlock>(g, o);
        return true;
    case 0x0807E090:
        g.scheduler().create<SpringyPlatform>(g, o);
        return true;
    case 0x0807F694:
        g.scheduler().create<SpikedBlock>(g, o);
        return true;
    case 0x08081EB4:
        g.scheduler().create<PipeWaypoint>(o);
        return true;
    case 0x08085270:
        g.scheduler().create<SpinningLift>(g, o);
        return true;
    case 0x08082BE0:
        g.scheduler().create<Funnel>(g, o);
        return true;
    case 0x08080438:
        g.scheduler().create<Vortex>(g, o);
        return true;
    case 0x080810F8:
        g.scheduler().create<OneWayGate>(g, o);
        return true;
    case 0x08081508:
        g.scheduler().create<OneWayTrapdoor>(g, o);
        return true;
    case 0x0808462C:
        g.scheduler().create<Warp>(o);
        return true;
    case 0x08084C04:
        g.scheduler().create<PipeBumper>(g, o);
        return true;
    case 0x0808F4AC:
        g.scheduler().create<CirclingPlatform>(g, o);
        return true;
    case 0x08092EC8:
        if (g.game_mode == 0) {
            g.scheduler().create<SpecialSpring>(g, o);
        } else {
            o.record[0] = 0xFE;
        }
        return true;
    case 0x0807E78C:
        g.scheduler().create<TimedBlock>(g, o);
        return true;
    case 0x080845C0:
        g.scheduler().create<FlingBox>(o);
        return true;
    case 0x080833D0:
        g.scheduler().create<TubeLauncher>(g, o);
        return true;
    case 0x080946C0:
        g.scheduler().create<Turntable>(o);
        return true;
    case 0x0807EFF4:
        g.scheduler().create<HangingSwing>(g, o);
        return true;
    case 0x08077258:
        g.scheduler().create<GroundLauncher>(g, o);
        return true;
    case 0x080776F4:
        g.scheduler().create<UpLauncher>(g, o);
        return true;
    case 0x08021C70:
        g.scheduler().create<CeilingSpring>(g, o);
        return true;
    default:
        return false;
    }
}

bool create_casino_paradise_enemy(Game& g, u32 creator, const ObjectSpawn& o) {
    switch (creator) {
    case 0x0806D1D0:
        g.scheduler().create<Bomber>(g, o);
        return true;
    case 0x0806D978:
        g.scheduler().create<Spiker>(g, o);
        return true;
    case 0x0806DD6C:
        if ((g.stage().zone >> 1) != 2 || g.save().unknown_5178 == 0) {
            g.scheduler().create<Dropper>(g, o);
        }
        return true;
    default:
        return false;
    }
}

} // namespace sonic_advance_gba
