// Zone 6's objects (act 10, the rocket, and act 11; the two acts' title
// cards name them Egg Rocket and Cosmic Angel) and act 10's rocket: at its
// checkpoints it lifts off and drops its spent stages.

#include "sonic_advance_gba/game/egg_rocket.hpp"
#include "sonic_advance_gba/game/boss.hpp"
#include "sonic_advance_gba/game/game.hpp"
#include "sonic_advance_gba/game/math.hpp"
#include "sonic_advance_gba/game/object_base.hpp"
#include "sonic_advance_gba/game/object_util.hpp"
#include "sonic_advance_gba/game/player.hpp"
#include "sonic_advance_gba/game/solid.hpp"
#include "sonic_advance_gba/game/sound.hpp"
#include "sonic_advance_gba/game/stage.hpp"

namespace sonic_advance_gba {

namespace {

// Whether a short-lived thing drawn at (sx, sy) is still near the view.
bool shot_in_view(s16 sx, s16 sy) {
    return u16(sx + 0x14) < 0x115 && sy + 0x14 >= 0 && sy < 0xF1;
}

// 080904E4, 08090674: a platform swinging to and fro like the springy
// platform (record +5 across or +6 up and down, the larger; 8 pixels a unit,
// 256 frames a swing; record +3 / +4 negative: half a swing on). It sparks
// in a cycle (16 frames on, 48 off; the first 16 on): while on, its second
// hitbox hurts the players' bodies (sound ABh). It carries the players on
// it and dips under them.
class SparkPlatform final : public Interactive {
public:
    SparkPlatform(Game& g, const ObjectSpawn& o) : Interactive(o), game_(g) {
        if (o.record[6] < o.record[5]) {
            step_x_ = 4;
            phase_ = s8(o.record[3]) < 0 ? 0x80 : 0;
        } else {
            step_y_ = 4;
            phase_ = s8(o.record[4]) < 0 ? 0x80 : 0;
        }
        sprite_.start(g.sprite_tiles.allocate(0x1E), 0x204, 0, x_, y_, 0x480, 0x2000);
        animate(g, sprite_);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 08090AF8

    void update(Game& g) override {
        StageState& s = g.stage();
        const Assets& a = g.assets();
        const u32 at = (s.ticks + phase_) & 0xFF;
        // How far it moved since the last frame (it carries its riders).
        s32 moved_x = 0, moved_y = 0;
        if (step_x_ == 0) {
            offset_x_ = 0;
        } else {
            const s32 before = offset_x_;
            offset_x_ = (spawn_.record[5] * 0x800 * sine(a, (step_x_ * at) & 0x3FF)) >> 14;
            moved_x = offset_x_ - before;
        }
        if (step_y_ == 0) {
            offset_y_ = 0;
        } else {
            const s32 before = offset_y_;
            offset_y_ = (spawn_.record[6] * 0x800 * sine(a, (step_y_ * at) & 0x3FF)) >> 14;
            moved_y = offset_y_ - before;
        }
        sprite_.x = s16(x_ - s.camera.x + divide(offset_x_, 0x100));
        sprite_.y = s16(y_ - s.camera.y + divide(offset_y_, 0x100));
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if ((p.flags & player_flag::on_object) && p.standing_on == &sprite_) {
                p.x += moved_x;
                p.y += 0x100 + moved_y;
            }
            collide_platform(g, sprite_, s16(x_ + (offset_x_ >> 8)), s16(y_ + (offset_y_ >> 8)), p);
            const Hitbox& spark = sprite_.hitboxes[1];
            if ((cycle_ == 0 || cycle_ == 2) && spark.index != 0xFFFFFFFF &&
                hitboxes_meet(x_ + divide(offset_x_, 0x100), y_ + divide(offset_y_, 0x100), spark,
                              p.x >> 8, p.y >> 8, p.body->sprite.hitboxes[0]) &&
                !(s.player[0].flags & player_flag::dead) && hurt_player(g, p)) {
                g.sound().start(0xAB);
            }
        }
        if (out_of_range(s, x_, y_, sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
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
        sprite_.y = s16(sprite_.y + (sine(a, dip_) >> 12));
        if (--timer_ == 0) {
            timer_ = (cycle_ == 0 || cycle_ == 2) ? 0x30 : 0x10;
            cycle_ = u8((cycle_ + 1) & 3);
            sprite_.previous_variant = 0xFF;
            sprite_.variant = cycle_;
        }
        animate(g, sprite_);
        draw(g, sprite_);
    }

private:
    Game& game_;
    Sprite sprite_;               // +0C
    u16 phase_{};                 // +44
    s32 offset_x_{}, offset_y_{}; // +48, +4C 8.8
    s16 step_x_{}, step_y_{};     // +50, +52
    u16 dip_{};                   // +54 a quarter turn of the sine at most: 4 pixels
    u8 cycle_{};                  // +56 0 and 2: sparking
    u8 timer_ = 0x10;             // +57
};

// 08092C38, 08092DE8: a solid block. Its picture by the act (even or odd)
// and record +3: 0/1 the large one, 2/3 the small one; odd values show its
// second variant.
class Block final : public Interactive {
public:
    Block(Game& g, const ObjectSpawn& o) : Interactive(o), game_(g) {
        const bool odd_act = g.stage().zone & 1;
        const u8 look = o.record[3];
        const bool large = look < 2;
        const u16 animation = large ? (odd_act ? 0x25C : 0x1FC) : (odd_act ? 0x25B : 0x1FD);
        sprite_.start(g.sprite_tiles.allocate(large ? 0x24 : 0x1B), animation, u8(look & 1), x_, y_,
                      0x480, 0x2000);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 08092EB4

    void update(Game& g) override {
        StageState& s = g.stage();
        sprite_.x = s16(x_ - s.camera.x);
        sprite_.y = s16(y_ - s.camera.y);
        for (unsigned n = 0; n < s.players; ++n) {
            collide_solid(g, sprite_, x_, y_, s.player[n]);
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
    Game& game_;
    Sprite sprite_; // +0C
};

// 08093420, 08093514: decoration (animation 203h), its colours left as they
// are (sprite flag 40000h).
class Lamp final : public Interactive {
public:
    Lamp(Game& g, const ObjectSpawn& o) : Interactive(o), game_(g) {
        sprite_.start(g.sprite_tiles.allocate(0x36), 0x203, 0, x_, y_, 0x480,
                      0x2000 | sprite_flag::keep_colors);
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); } // 0809359C

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

// 08095380, 080951A4: an invisible line that turns gravity around when the
// first player crosses it. Taller than wide (record +6 >= +5) it stands
// upright at its x (from record +4 to +4 + +6 eight-pixel units down) and
// record +3 says what crossing it does; else it lies flat at its y (from
// record +3 to +3 + +5 units across) and record +4 does: 0 / 3 turn it
// around, 1 / 4 make it normal, 2 / 5 upside down. Not while the player
// is dead.
class GravitySwitch final : public Interactive {
public:
    GravitySwitch(Game& g, const ObjectSpawn& o) : Interactive(o) {
        const Player& p = g.stage().player[0];
        if (o.record[6] < o.record[5]) {
            mode_ = o.record[4];
            last_ = p.y;
        } else {
            mode_ = o.record[3];
            last_ = p.x;
        }
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        const Player& p = s.player[0];
        if (!(p.flags & player_flag::dead)) {
            const u8* r = spawn_.record;
            const bool upright = mode_ < 3;
            // The span along the line, and where the player is across it.
            const s32 from = upright ? y_ + s8(r[4]) * 8 : x_ + s8(r[3]) * 8;
            const s32 along = upright ? p.y >> 8 : p.x >> 8;
            const s32 length = (upright ? r[6] : r[5]) * 8;
            const s32 now = upright ? p.x : p.y;
            if (from <= along && along <= from + length) {
                const s32 line = s32(upright ? x_ : y_) << 8;
                if ((last_ > line && now < line) || (last_ < line && now > line)) {
                    const u8 kind = upright ? mode_ : u8(mode_ - 3);
                    if (kind == 0) {
                        s.flags ^= stage_flag::upside_down;
                    } else if (kind == 1) {
                        s.flags &= ~stage_flag::upside_down;
                    } else {
                        s.flags |= stage_flag::upside_down;
                    }
                }
            }
            last_ = now;
        }
        if (!near_view(s16(x_ - s.camera.x), s16(y_ - s.camera.y))) {
            give_back();
            g.scheduler().destroy(*this);
        }
    }

private:
    u8 mode_;  // +41
    s32 last_; // +3C the player's x or y when last looked at (8.8)
};

// 080965BC, 080966D0: a platform that gives way: standing on it lowers
// the player two pixels a frame. Record +3 mirrors it (1 across, 2 up and
// down, 3 both). Its picture's tiles are a fixed place (06012680).
class SoftPlatform final : public Interactive {
public:
    SoftPlatform(Game& g, const ObjectSpawn& o) : Interactive(o) {
        u32 flags = 0x2000;
        switch (o.record[3]) {
        case 1:
            flags = 0x2400;
            break;
        case 2:
            flags = 0x2800;
            break;
        case 3:
            flags = 0x2C00;
            break;
        default:
            break;
        }
        sprite_.start(0x12680, 0x250, 0, x_, y_, 0x480, flags);
        animate(g, sprite_);
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        sprite_.x = s16(x_ - s.camera.x);
        sprite_.y = s16(y_ - s.camera.y);
        for (unsigned n = 0; n < s.players; ++n) {
            Player& p = s.player[n];
            if (!(p.flags & (player_flag::dead | player_flag::no_movement)) &&
                (collide_platform(g, sprite_, x_, y_, p) & solid::on_top)) {
                p.y += 0x200;
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
    Sprite sprite_; // +0C
};

// 0806FCB0, 0806FD4C: the side shooter's shot: flies across at 2 pixels a
// frame, hurting players it touches. Its tiles are a fixed place
// (06012580).
class SideShot final : public Process {
public:
    SideShot(Game& g, s16 x, s16 y, bool rightward)
        : Process(0x3000), x_(s32(x) << 8), speed_(rightward ? 0x200 : -0x200) {
        sprite_.start(0x12580, 0x1B3, 0, x, y, 0x240, 0);
        animate(g, sprite_);
    }

    void update(Game& g) override {
        const auto& c = g.stage().camera;
        x_ += speed_;
        const s16 x = s16(x_ >> 8), y = sprite_.y;
        projectile_hits(g, sprite_, x, y);
        sprite_.x = s16(x - c.x);
        sprite_.y = s16(y - c.y);
        if (!shot_in_view(sprite_.x, sprite_.y)) {
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
    s32 x_;         // +30 8.8
    s16 speed_;     // +34
};

// 0806F7B8, 0806F8E4: walks to and fro within its range (record +3: the
// start, +5: the width, in 8 pixels) at 0A0h; its body hurts the player
// (it cannot be attacked while walking). Every 90 frames it stops facing
// the player (0806FB34: 60 frames; on the 20th a shot leaves from its
// front, 0806FCB0); stopped, it can be attacked. Not made in zone 6 while
// the save's byte +18 is set.
class SideShooter final : public Enemy {
public:
    SideShooter(Game& g, const ObjectSpawn& o) : Enemy(g, o, 0x14) {
        sprite_.start(tiles_, 0x1B0, 0, s16(base_x_), s16(base_y_), 0x480, 0x2000);
        animate(g, sprite_);
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        if (!shooting_) {
            offset_ += speed_;
        }
        const s16 x = s16(base_x_ + (offset_ >> 8)), y = s16(base_y_);
        sprite_.x = s16(x - s.camera.x);
        sprite_.y = s16(y - s.camera.y);
        if (out_of_range(s, base_x_, base_y_, sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        if (shooting_) {
            shoot(g, x, y);
            return;
        }
        Player& p = s.player[0];
        const Hitbox& h = sprite_.hitboxes[0];
        if (h.index != 0xFFFFFFFF &&
            hitboxes_meet(x, y, h, p.x >> 8, p.y >> 8, p.body->sprite.hitboxes[0])) {
            hurt_player(g, p);
        }
        const s32 along = offset_ >> 8;
        if (along <= param(3) * 8) {
            speed_ = 0xA0;
        } else if (along >= (param(3) + spawn_.record[5]) * 8) {
            speed_ = -0xA0;
        } else if (cooldown_ != 0) {
            --cooldown_;
        } else {
            if (x < (p.x >> 8)) {
                sprite_.flags |= sprite_flag::flip_x;
            } else {
                sprite_.flags &= ~sprite_flag::flip_x;
            }
            cooldown_ = 0x5A;
            timer_ = 0;
            sprite_.variant = 1;
            shooting_ = true; // from the next frame
        }
        animate(g, sprite_);
        draw(g, sprite_);
    }

private:
    // 0806FB34
    void shoot(Game& g, s16 x, s16 y) {
        if (enemy_touched(g, sprite_, x, y)) {
            g.scheduler().destroy(*this);
            return;
        }
        if (++timer_ == 0x14) {
            const bool right = sprite_.flags & sprite_flag::flip_x;
            g.scheduler().create<SideShot>(g, s16(right ? x + 8 : x - 8), s16(y - 0x10), right);
        }
        if (timer_ == 0x3C) {
            sprite_.variant = 0;
            shooting_ = false;
        }
        animate(g, sprite_);
        draw(g, sprite_);
    }

    Sprite sprite_;     // +0C
    s32 offset_{};      // +3C 8.8
    s16 speed_ = -0xA0; // +40
    s16 timer_{};       // +42
    s16 cooldown_{};    // +46
    bool shooting_{};   // the update is 0806FB34
};

// 08070564, 08070614: the down shooter's shot: falls at 2 pixels a frame,
// hurting players it touches. Its tiles are a fixed place (06012600).
class DownShot final : public Process {
public:
    DownShot(Game& g, s16 x, s16 y) : Process(0x3000), x_(s32(x) << 8), y_(s32(y) << 8) {
        sprite_.start(0x12600, 0x1B4, 0, x, y, 0x240, 0x2000);
        animate(g, sprite_);
    }

    void update(Game& g) override {
        const auto& c = g.stage().camera;
        y_ += 0x200;
        const s16 x = s16(x_ >> 8), y = s16(y_ >> 8);
        projectile_hits(g, sprite_, x, y);
        sprite_.x = s16(x - c.x);
        sprite_.y = s16(y - c.y);
        if (!shot_in_view(sprite_.x, sprite_.y)) {
            g.scheduler().destroy(*this);
            return;
        }
        animate(g, sprite_);
        draw(g, sprite_);
    }

private:
    Sprite sprite_; // +00
    s32 x_, y_;     // +30, +34 8.8
};

// 0806FDDC: drawn as two halves (the second mirrored). It waits
// (0806FEFC) until the player is within 78h pixels across, then moves to
// and fro within its range (08070044: record +3 the start, +5 the width,
// in 8 pixels; at 0C0h, turning at the ends with a swing, 08070224: 30
// frames); every 60 frames it stops and drops a shot (080703F8: on the 20th
// of 26 frames, 08070564).
class DownShooter final : public Enemy {
public:
    DownShooter(Game& g, const ObjectSpawn& o) : Enemy(g, o, 8) {
        sprite_.start(tiles_, 0x1B1, 0, s16(base_x_), s16(base_y_), 0x480, 0x2000);
        animate(g, sprite_);
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        if (state_ == State::moving) {
            offset_ += speed_;
        } else if (state_ == State::turning) {
            // The turn's swing: a sine wave over 30 frames.
            const s32 swing =
                divide(sine(g.assets(), u32(timer_ * divide(0x400, 0x1E)) & 0x3FF), 0x37);
            offset_ = speed_ < 0 ? offset_ + swing : offset_ - swing;
        }
        const s16 x = s16(base_x_ + (offset_ >> 8)), y = s16(base_y_);
        sprite_.x = s16(x - s.camera.x);
        sprite_.y = s16(y - s.camera.y);
        if (out_of_range(s, base_x_, base_y_, sprite_.x, sprite_.y)) {
            give_back();
            g.scheduler().destroy(*this);
            return;
        }
        switch (state_) {
        case State::waiting:
            wait(g, x, y);
            break;
        case State::moving:
            move(g, x, y);
            break;
        case State::shooting:
            shoot(g, x, y);
            break;
        case State::turning:
            turn(g, x, y);
            break;
        }
    }

private:
    enum class State { waiting, moving, shooting, turning };

    void draw_halves(Game& g) {
        draw(g, sprite_);
        sprite_.flags |= sprite_flag::flip_x;
        draw(g, sprite_);
        sprite_.flags &= ~sprite_flag::flip_x;
    }

    // 0806FEFC
    void wait(Game& g, s16 x, s16 y) {
        if (enemy_touched(g, sprite_, x, y)) {
            g.scheduler().destroy(*this);
            return;
        }
        const s32 px = g.stage().player[0].x >> 8;
        if (x - 0x78 < px && px < x + 0x78) {
            sprite_.variant = 0;
            state_ = State::moving;
        }
        animate(g, sprite_);
        draw_halves(g);
    }

    // 08070044
    void move(Game& g, s16 x, s16 y) {
        animate(g, sprite_);
        if (enemy_touched(g, sprite_, x, y)) {
            g.scheduler().destroy(*this);
            return;
        }
        const s32 along = offset_ >> 8;
        if (along > param(3) * 8 && along < (param(3) + spawn_.record[5]) * 8) {
            turned_ = false;
            if (cooldown_ != 0) {
                --cooldown_;
            } else {
                cooldown_ = 0x3C;
                timer_ = 0;
                sprite_.variant = 1;
                state_ = State::shooting;
            }
        } else if (!turned_) {
            turned_ = true;
            speed_ = along <= param(3) * 8 ? 0xC0 : -0xC0;
            timer_ = 0;
            state_ = State::turning;
        }
        draw_halves(g);
    }

    // 080703F8
    void shoot(Game& g, s16 x, s16 y) {
        if (enemy_touched(g, sprite_, x, y)) {
            g.scheduler().destroy(*this);
            return;
        }
        if (++timer_ == 0x14) {
            g.scheduler().create<DownShot>(g, x, s16(y - 2));
        }
        if (timer_ == 0x1A) {
            back_to_moving();
        }
        animate(g, sprite_);
        draw_halves(g);
    }

    // 08070224
    void turn(Game& g, s16 x, s16 y) {
        if (enemy_touched(g, sprite_, x, y)) {
            g.scheduler().destroy(*this);
            return;
        }
        if (++timer_ > 0x1D) {
            back_to_moving();
        }
        animate(g, sprite_);
        draw_halves(g);
    }

    void back_to_moving() {
        sprite_.previous_variant = 0xFF;
        sprite_.animation = 0x1B1;
        sprite_.variant = 0;
        state_ = State::moving;
    }

    Sprite sprite_;     // +0C
    s32 offset_{};      // +3C 8.8
    s16 speed_ = -0xC0; // +40
    s16 timer_{};       // +42
    s16 cooldown_{};    // +44
    bool turned_{};     // +46 turned at this end already
    State state_ = State::waiting;
};

// 08028C54 and its states (08028CB4, 08028EF0, 08029040, 08029164,
// 08029478): at checkpoints 2 and 3 the rocket drops its spent stage, the
// part of the act below `bottom`. The player is held still; the view sinks
// until the line is 50h above its middle, then debris rains (sound ADh)
// and the stage falls: the level layer (layer 2) below the line is shown
// lower each frame, the gap between showing its empty line F0h. Then the
// next stage fires (debris every 16 frames, sound 90h) and the view comes
// back; the clock starts again at five minutes.
class StageSeparation final : public Process {
public:
    StageSeparation(Game& g, s16 bottom) : Process(0x4000), bottom_(bottom) {
        StageState& s = g.stage();
        Player& p = s.player[0];
        p.flags |= player_flag::frozen;
        p.held = 0;
        p.speed_x = 0;
        p.ground_speed = 0;
        s.camera.max_y = s16(bottom + 0xA0);
    }

    void update(Game& g) override {
        switch (state_) {
        case State::sinking:
            sink(g);
            break;
        case State::falling:
            fall(g);
            break;
        case State::gone:
            gone(g);
            break;
        case State::firing:
            fire(g);
            break;
        case State::returning:
            come_back(g);
            break;
        }
    }

private:
    enum class State { sinking, falling, gone, firing, returning };

    // 08028CB4
    void sink(Game& g) {
        auto& c = g.stage().camera;
        ++c.shift_y;
        if (bottom_ - c.y + 0x50 <= c.shift_y) {
            rain_debris(g);
            shake_view(g, 0x800, 8, 0x10, 10, 0x80);
            g.sound().start(0xAD);
            state_ = State::falling;
        }
    }

    // Layer 2 for the frame: unchanged down to the line, the empty line
    // F0h from there to `gap_end`, below that the part shifted down by
    // `drop` pixels.
    void show_gap(Game& g, u16 gap_end, u16 drop) {
        auto& vs = g.video();
        const auto& bg = vs.settings.bg[2];
        const s16 top = s16(bottom_ - g.stage().camera.y);
        if (top < 1) {
            line_scroll_stretch(vs, 2, 0, u8(gap_end), bg.scroll_x, 0xF0);
        } else {
            line_scroll_fill(vs, 2, 0, u8(top), bg.scroll_x, bg.scroll_y);
            line_scroll_stretch(vs, 2, u8(top), u8(gap_end), bg.scroll_x, 0xF0);
        }
        line_scroll_fill(vs, 2, u8(gap_end), 0xA0, bg.scroll_x, u16(bg.scroll_y - drop));
    }

    // The falling part's top (`near`) and the gap's end, on screen.
    void show_falling(Game& g, u16 near, u16 gap_end) {
        if (s16(near) < 1 || s16(gap_end) > 0x9F) {
            g.video().line_effect_on = false;
            return;
        }
        show_gap(g, gap_end, u16(fall_ >> 8));
    }

    u16 on_screen(Game& g, u16 v) const { return u16(bottom_ + (v >> 8) - g.stage().camera.y); }

    // 08028EF0
    void fall(Game& g) {
        StageState& s = g.stage();
        top_ = u16(top_ + 8);
        fall_ = u16(fall_ + 0x10);
        if (top_ > 0x3FF && (s.checkpoint == 2 || ((u32(-s32(s.ticks) * 2)) & 0x1FF) == 0x102)) {
            state_ = State::gone;
            timer_ = 0;
            if (s.checkpoint == 3) {
                s.camera.locks |= camera_lock::rocket_space;
            }
            gone(g); // at once (0809984C)
            return;
        }
        show_falling(g, on_screen(g, top_), on_screen(g, fall_));
    }

    // 08029040
    void gone(Game& g) {
        top_ = u16(top_ + 8);
        fall_ = u16(fall_ + 0x10);
        const u16 near = on_screen(g, top_), gap_end = on_screen(g, fall_);
        if (timer_++ <= 0x28) {
            show_falling(g, near, gap_end);
            return;
        }
        state_ = State::firing;
        shake_view(g, 0x800, 8, 0x10, -1, 0xD0);
        timer_ = 0;
        fire(g); // at once (08099854)
    }

    // 08029164
    void fire(Game& g) {
        fall_ = u16(fall_ + 0x200);
        top_ = u16(top_ + 0x100);
        const u16 near = on_screen(g, top_), gap_end = on_screen(g, fall_);
        if ((timer_++ & 0xF) == 0) {
            rain_debris(g);
            g.sound().start(0x90);
        }
        if (s16(gap_end) >= 0xA1) {
            state_ = State::returning;
            come_back(g); // at once (08099844)
            return;
        }
        show_falling(g, near, gap_end);
    }

    // 08029478
    void come_back(Game& g) {
        StageState& s = g.stage();
        auto& c = s.camera;
        --c.shift_y;
        if ((timer_++ & 0xF) == 0) {
            rain_debris(g);
            g.sound().start(0x90);
        }
        if (bottom_ - 0xA0 < c.y) {
            auto& vs = g.video();
            const auto& bg = vs.settings.bg[2];
            const s16 top = s16(bottom_ - c.y);
            if (top < 1) {
                line_scroll_stretch(vs, 2, 0, 0xA0, bg.scroll_x, 0xF0);
            } else {
                line_scroll_fill(vs, 2, 0, u8(top), bg.scroll_x, bg.scroll_y);
                line_scroll_stretch(vs, 2, u8(top), 0xA0, bg.scroll_x, 0xF0);
            }
            return;
        }
        c.shift_y = 0;
        const auto restart_clock = [&] {
            s.flags &= ~stage_flag::paused;
            if (g.game_mode == 0) {
                s.flags |= stage_flag::countdown;
                s.clock = 18000;
            }
        };
        if (s.checkpoint > 1) {
            c.max_y = 0x15C0;
            restart_clock();
        }
        if (s.checkpoint > 2) {
            c.max_y = 0xCC0;
            restart_clock();
        }
        Player& p = s.player[0];
        p.flags &= ~player_flag::frozen;
        p.held = 0;
        g.scheduler().destroy(*this);
        g.video().line_effect_on = false;
        c.locks &= ~camera_lock::rocket_up;
    }

    s16 bottom_;  // +04 the act's line the stage separates at
    u16 top_{};   // +06 8.8
    u16 fall_{};  // +08 8.8 how far the stage has fallen
    u16 timer_{}; // +0C
    State state_ = State::sinking;
};

// 08029830, 08029890, 080297B8: the lift-off at the first checkpoint: the
// view shakes (a second, then five seconds with the engines' sound every
// 16 frames); the player is held still meanwhile and the view's left edge
// moves to 422h. Then the clock counts down from five minutes.
class LiftOff final : public Process {
public:
    explicit LiftOff(Game& g) : Process(0x4000) {
        StageState& s = g.stage();
        shake_view(g, 0x800, 8, 0x10, 10, 0x80);
        s.camera.min_x = 0x422;
        Player& p = s.player[0];
        p.flags |= player_flag::frozen;
        p.held = 0;
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        if (!rumbling_) {
            // 08029890
            if (--timer_ == 0) {
                timer_ = 300;
                shake_view(g, 0x800, 8, 0x10, 300, 0x90);
                rumbling_ = true;
            }
            return;
        }
        // 080297B8
        if ((timer_ & 0xF) == 0) {
            g.sound().start(0x90);
        }
        if (--timer_ == 0) {
            if (g.game_mode == 0) {
                s.flags |= stage_flag::countdown;
                s.clock = 18000;
            }
            Player& p = s.player[0];
            p.flags &= ~player_flag::frozen;
            p.held = 0;
            g.scheduler().destroy(*this);
        }
    }

private:
    s16 timer_ = 0x3C; // +0C
    bool rumbling_{};  // the update is 080297B8
};

} // namespace

void rocket_checkpoint(Game& g, u8 checkpoint) {
    StageState& s = g.stage();
    Player& p = s.player[0];
    switch (checkpoint) {
    case 1:
        p.speed_x = 0;
        p.ground_speed = 0;
        g.scheduler().create<LiftOff>(g);
        break;
    case 2:
    case 3:
        if (g.game_mode == 0) {
            s.flags |= stage_flag::paused;
        }
        g.scheduler().create<StageSeparation>(g, s16(checkpoint == 2 ? 0x15BF : 0xCBF));
        break;
    default:
        break;
    }
}

bool create_egg_rocket_object(Game& g, u32 creator, const ObjectSpawn& o) {
    switch (creator) {
    case 0x080904E4:
        g.scheduler().create<SparkPlatform>(g, o);
        return true;
    case 0x08092C38:
        g.scheduler().create<Block>(g, o);
        return true;
    case 0x08093420:
        g.scheduler().create<Lamp>(g, o);
        return true;
    case 0x08095380:
        g.scheduler().create<GravitySwitch>(g, o);
        return true;
    case 0x080965BC:
        g.scheduler().create<SoftPlatform>(g, o);
        return true;
    default:
        return false;
    }
}

bool create_egg_rocket_enemy(Game& g, u32 creator, const ObjectSpawn& o) {
    switch (creator) {
    case 0x0806F7B8:
        // Not made in zone 6 while the save's byte +18 is set.
        if ((g.stage().zone >> 1) != 5 || g.save().unknown_5178 == 0) {
            g.scheduler().create<SideShooter>(g, o);
        }
        return true;
    case 0x0806FDDC:
        g.scheduler().create<DownShooter>(g, o);
        return true;
    default:
        return false;
    }
}

} // namespace sonic_advance_gba
