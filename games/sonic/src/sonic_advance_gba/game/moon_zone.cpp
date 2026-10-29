// Act 13, The Moon Zone: Super Sonic and the last boss (0804A184-0804B6E8,
// 0804FE20-08052858).

#include "sonic_advance_gba/game/moon_zone.hpp"
#include "sonic_advance_gba/game/boss_parts.hpp"
#include "sonic_advance_gba/game/camera.hpp"
#include "sonic_advance_gba/game/collision.hpp"
#include "sonic_advance_gba/game/ending.hpp"
#include "sonic_advance_gba/game/game.hpp"
#include "sonic_advance_gba/game/math.hpp"
#include "sonic_advance_gba/game/object_util.hpp"
#include "sonic_advance_gba/game/objects.hpp"
#include "sonic_advance_gba/game/player_effects.hpp"
#include "sonic_advance_gba/game/stage.hpp"
#include "sonic_advance_gba/game/video.hpp"
#include "sonic_advance_gba/game/window_shapes.hpp"
#include <algorithm>

namespace sonic_advance_gba {

namespace {

using boss_parts::create_smoke;
using boss_parts::flash_off;
using boss_parts::flash_on;
using boss_parts::floor_distance;
using boss_parts::Particle;

constexpr u32 super_animations = 0x084A70AC; // per player state: animation, variant

constexpr u16 sound_jump = 0x74;
constexpr u16 sound_dash = 0x70;
constexpr u16 sound_hit = 0x77;
constexpr u16 sound_rings_low = 0x8B;

// The parts of the boss and its effects share one record layout (0804CD4C,
// 0804CF28): a sprite with its transform, an act position with speeds and
// accelerations, a frame counter, and for the boss's parts a joint on a
// parent. Made at one priority after their maker's (0804CEA8, 0804CEE8);
// those with their own tiles free them at the end (0804CF54).
class MoonSprite : public Process {
public:
    MoonSprite(Game& g, u16 priority, u32 tiles, bool own_tiles, u16 animation, u8 variant)
        : Process(priority), game_(g), own_tiles_(own_tiles) {
        Sprite& sp = view_.sprite;
        sp.tiles.to = tiles;
        sp.tiles.bytes = 0;
        sp.animation = animation;
        sp.variant = variant;
        sp.previous_variant = 0xFF;
        sp.frame = 0xFFFFFFFF;
        sp.flags = 0;
        sp.position = 0;
        sp.layer = 0;
        sp.timer = 0;
        sp.speed = 0x10;
        sp.palette = 0;
        sp.hitboxes[0].index = 0xFFFFFFFF;
        sp.hitboxes[1].index = 0xFFFFFFFF;
        view_.angle = 0;
        view_.scale_x = view_.scale_y = 0x100;
        view_.x = view_.y = 0;
    }

    void on_destroy() override {
        if (own_tiles_) {
            game_.sprite_tiles.free(view_.sprite.tiles.to);
        }
    }

protected:
    Sprite& sprite() { return view_.sprite; }

    // The view position of act position (x, y) (8.8).
    s16 view_x(const StageState& s) const { return s16((x_ >> 8) - s.camera.x); }

    s16 view_y(const StageState& s) const { return s16((y_ >> 8) - s.camera.y); }

    // 0804CF6C
    void move() {
        x_ += speed_x_;
        y_ += speed_y_;
    }

    // Turned and scaled with the next free rotation set.
    void draw_turned(Game& g, u32 rotation_flags) {
        Sprite& sp = view_.sprite;
        StageState& s = g.stage();
        sp.flags =
            (sp.flags & ~sprite_flag::rotation_set) | u8(s.next_rotation_set++ | rotation_flags);
        animate(g, sp);
        transform(g, view_);
        draw(g, sp);
    }

    Game& game_;
    bool own_tiles_;
    s16 index_{};               // +00
    u8 stage_{};                // +02
    s32 timer_{};               // +04
    TransformedSprite view_;    // +0C
    s32 x_{}, y_{};             // +50, +54
    s16 speed_x_{}, speed_y_{}; // +58, +5A
    s16 accel_x_{}, accel_y_{}; // +5C, +5E
    u16 spin_{};                // +72 (turning effects)
};

// 0804A464 / 0804A518: a sparkle left behind Super Sonic, drifting back
// faster and faster; gone off the view or at its animation's end. At most
// four at a time (0804A7FC gives one back).
class SuperSparkle final : public MoonSprite {
public:
    SuperSparkle(Game& g, s32 x, s32 y, u8 variant)
        : MoonSprite(g, 0x3001, g.sprite_tiles.allocate(4), true, 0x307, variant) {
        x_ = x;
        y_ = y;
        speed_x_ = 0;
        accel_x_ = -0x40;
        sprite().layer = 0x200;
        sprite().flags = 0x1400;
    }

    void on_destroy() override {
        ++game_.stage().super_sparkles;
        MoonSprite::on_destroy();
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        const s32 sx = (x_ >> 8) - s.camera.x;
        const s16 sy = s16((y_ >> 8) - s.camera.y);
        Sprite& sp = sprite();
        if ((sp.flags & sprite_flag::ended) || u32(sx + 0x10) >= 0x110) {
            g.scheduler().destroy(*this);
            return;
        }
        sp.x = s16(sx);
        sp.y = sy;
        animate(g, sp);
        draw(g, sp);
        x_ += speed_x_;
        speed_x_ = s16(speed_x_ + accel_x_);
    }
};

void create_sparkle(Game& g, s32 x, s32 y, u8 variant) {
    StageState& s = g.stage();
    if (s8(s.super_sparkles) > 0) {
        --s.super_sparkles;
        g.scheduler().create<SuperSparkle>(g, x, y, variant);
    }
}

// 0804A820 / 0804A38C: the dash's flash on Super Sonic, flickering in front
// of and behind the level; its second part (variant 1) once the dash is
// over or a claw holds the player; gone when that ends or the player dies.
class SuperDash final : public MoonSprite {
public:
    explicit SuperDash(Game& g)
        : MoonSprite(g, 0x3001, g.sprite_tiles.allocate(0x28), true, 400, 0) {
        sprite().layer = 0x200;
        sprite().flags = 0x1400;
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        const Player& p = s.player[0];
        x_ = p.x;
        y_ = p.y;
        Sprite& sp = sprite();
        if ((p.flags & player_flag::dead) || (stage_ != 0 && (sp.flags & sprite_flag::ended))) {
            g.scheduler().destroy(*this);
            return;
        }
        if (p.move == 0 || (p.flags & player_flag::grabbed)) {
            stage_ = 1;
            sp.animation = 400;
            sp.variant = 1;
        }
        sp.x = view_x(s);
        sp.y = view_y(s);
        sp.flags = (sp.flags & ~sprite_flag::priority) | ((s.ticks & 2) ? 0x2000 : 0x1000);
        animate(g, sp);
        draw(g, sp);
    }
};

class BossPart;

// 0805230C / 080504A8 / 0804FE20: the boss. Its position is kept apart from
// its base (the layout's row; x 0) and moves by speed and acceleration; it
// stands on the ground below its feet, or drops onto it. Its state (+6E)
// steps through the fight; most states end once a part reports its move
// done (a bit per part in +5C). At each state change the parts start
// their move afresh.
class MoonBoss final : public Process {
public:
    MoonBoss(Game& g, const ObjectSpawn& o);
    void build(Game& g);
    void update(Game& g) override;

    static constexpr unsigned part_count = 18;

    // The boss's flags (+58).
    static constexpr u32 falling = 0x2; // not on its feet (it drops onto the ground)
    static constexpr u32 beaten = 0x80;
    static constexpr u32 waiting = 0x400000;       // until the warning's half
    static constexpr u32 debug_start = 0x80000000; // made with buttons FAh held (not read further)

    // States (+6E).
    enum State : u8 {
        arriving = 0, // walks in from the left side of the act
        braking = 1,  // slows down to its place (x 1A000h)
        settling = 2, // until on its feet
        standing = 3, // a second
        step_a = 4,
        step_b = 5,
        step_c = 6,
        step_d = 7,   // the walking steps
        stomp = 8,    // its feet down (rocks)
        pounding = 9, // shaking the ground for 12Ch frames
        aiming = 10,  // the head turns down
        beam = 11,    // the head's beam
        sparks = 12,  // sparks run along the ground (then the beam again, four rounds)
        broken = 13,  // beaten: it sinks
        leaving = 14, // and walks off to the left, smoking
    };

    Game& game;
    std::array<BossPart*, part_count> parts{}; // 030061F4-03006238
    s32 x{}, y{};                              // +44, +48 (8.8, from the base)
    s16 speed_x{}, speed_y{};                  // +4C, +4E
    s16 accel_x{}, accel_y{};                  // +50, +52
    s16 timer{};                               // +54
    s16 rounds{};                              // +56 beam rounds
    u32 flags = falling | waiting;             // +58
    u32 parts_done{};                          // +5C
    s32 base_x{}, base_y{};                    // +60, +64 pixels
    u8 hits_left{};                            // +68
    s8 flash{};                                // +69 frames of the hit flash
    s16 missile_timer = 0xF0;                  // +6C
    u8 state = arriving;                       // +6E
    u8 next_state = arriving;                  // +6F

private:
    s32 ground() const;
    void smoke(Game& g);
};

bool boss_beaten(const StageState& s) {
    return s.moon_boss && (static_cast<const MoonBoss*>(s.moon_boss)->flags & MoonBoss::beaten);
}

// 0804A5A4 / 0804A6E8: one of the warning sign's four parts, at the view's
// middle. It opens (grows from flat) over its first eight frames, stands,
// and from frame 1Ch of each 32 widens and flattens; gone after C0h frames.
// The first part lets the boss go at frame 80h.
class WarningPart final : public MoonSprite {
public:
    WarningPart(Game& g, u16 priority, s16 index)
        : MoonSprite(g, priority, g.sprite_tiles.allocate(0x10), true, 0x2CF, u8(index)) {
        index_ = index;
        x_ = 0x7800;
        y_ = 0x5000;
        sprite().layer = 0;
        // The first part loads the colours; the others keep them.
        sprite().flags = index == 0 ? 0 : sprite_flag::keep_colors;
    }

    void update(Game& g) override;
};

// ---------------------------------------------------------------------------
// Super Sonic (08049864 and its parts).

class SuperMovement {
public:
    SuperMovement(Game& g, Player& p) : g(g), a(g.assets()), s(g.stage()), p(p) {}

    // 08049D48
    void frame() {
        if (p.slip_timer == 0) {
            if (p.flags & player_flag::in_air) {
                in_air();
            } else {
                on_ground();
            }
            // Now and then a sparkle somewhere around the body.
            if ((s.ticks & 3) == 0 && (s.next_random() & 0x10000)) {
                const u32 r1 = s.next_random();
                const u32 r2 = s.next_random();
                const u32 r3 = s.next_random();
                create_sparkle(g, p.x + s32((r1 & 0xF0000) >> 8),
                               p.y + s32((r2 & 0xF0000) >> 8) - 0x800, u8((r3 & 0x10000) >> 16));
            }
        } else {
            // Knocked back: spinning.
            p.angle = u8(p.angle - 0xC);
            move();
            move();
            follow_ground();
        }
    }

    // 0804A084: keeps the feet on the ground below, as long as it is at most
    // Bh pixels down (or anywhere up to Bh pixels inside); further down the
    // player is in the air, unless a claw holds it.
    void follow_ground() {
        s32 y = p.y;
        const s32 d = ground_below();
        if (d != 0) {
            if (d < 0) {
                if (d < -0xB) {
                    return;
                }
            } else if (d > 0xB) {
                if (!(p.flags & player_flag::grabbed)) {
                    p.flags |= player_flag::in_air;
                    return;
                }
                p.y = y;
                return;
            }
            y += d * 0x100;
        }
        p.y = y;
    }

private:
    Game& g;
    const Assets& a;
    StageState& s;
    Player& p;

    // The distance to the ground below the feet (the nearer of both sides),
    // at the ground's position under the player.
    s32 ground_below() const {
        const auto& c = s.camera;
        const s32 x = (p.x + (moon_ground_x(s, c.x) - c.x) * 0x100) >> 8;
        const s32 feet = (p.y >> 8) + p.radius_y;
        const s32 left =
            probe(a, s.map, feet, x - 2 - p.radius_x, p.layer, 8, nullptr, Sensor::floor);
        const s32 right =
            probe(a, s.map, feet, x + 2 + p.radius_x, p.layer, 8, nullptr, Sensor::floor);
        return std::min(left, right);
    }

    // 080470B4
    void move() {
        p.x += p.speed_x;
        if ((s.flags ^ s.previous_flags) & stage_flag::upside_down) {
            p.speed_y = s16(-p.speed_y);
        }
        p.y += (s.flags & stage_flag::upside_down) ? -p.speed_y : p.speed_y;
    }

    // Within the view's 1E0h pixels; at an edge the run stops.
    void keep_in_view() {
        const s32 x = std::clamp(p.x, 0, 0x1E000);
        if (x != p.x) {
            if (!(p.flags & player_flag::in_air)) {
                p.ground_speed = 0;
            }
            p.speed_x = 0;
        }
        p.x = x;
    }

    void moves_twice() {
        keep_in_view();
        move();
        move();
    }

    // 08049B78: the other button dashes ahead (8 frames before the next).
    bool dash() {
        if ((p.special == 0 || --p.special == 0) && (s.other_buttons & p.pressed)) {
            p.special = 8;
            p.move = 1;
            p.ground_speed = 0x400;
            p.speed_x = 0;
            p.speed_y = 0;
            g.scheduler().create<SuperDash>(g);
            g.sound().start(sound_dash);
            return true;
        }
        return false;
    }

    // 0804A2C8: the dash slows down 40h a frame.
    void slow_dash() {
        p.ground_speed = s16(p.ground_speed - 0x40);
        if (p.ground_speed <= 0) {
            p.move = 0;
            p.speed_x = 0;
            p.speed_y = 0;
        } else {
            p.speed_x = p.ground_speed;
        }
    }

    // 08049A84
    void on_ground() {
        if (p.move == 0) {
            if (dash()) {
                return;
            }
            if (s.jump_buttons & p.pressed) {
                p.flags |= player_flag::jumped | player_flag::in_air;
                p.speed_y = s16(p.speed_y - 0x440);
                g.sound().start(sound_jump);
                return;
            }
            steer();
            moves_twice();
            follow_ground();
            p.state = 2;
        } else {
            slow_dash();
            moves_twice();
            follow_ground();
            p.state = 3;
        }
    }

    // 08049A00: right and left speed up 24h a frame (to 200h); neither
    // slows down 60h a frame.
    void steer() {
        s16 v = p.ground_speed;
        switch (p.held & (button::right | button::left)) {
        case button::right:
            v = std::min<s16>(s16(v + 0x24), 0x200);
            break;
        case button::left:
            v = std::max<s16>(s16(v - 0x24), -0x200);
            break;
        case 0:
            if (v > 0) {
                v = std::max<s16>(s16(v - 0x60), 0);
            } else if (v < 0) {
                v = std::min<s16>(s16(v + 0x60), 0);
            }
            break;
        default:
            break;
        }
        p.ground_speed = v;
        p.speed_x = v;
    }

    // 08049BD8
    void in_air() {
        if (p.move != 0) {
            slow_dash();
            moves_twice();
            follow_ground();
            p.state = 3;
            return;
        }
        if (dash()) {
            return;
        }
        // Letting go of jump caps the rise.
        if (!(s.jump_buttons & p.held) && (p.flags & player_flag::jumped)) {
            p.speed_y = std::max<s16>(p.speed_y, -0x180);
        }
        switch (p.held & (button::right | button::left)) {
        case button::right:
            p.speed_x = std::min<s16>(s16(p.speed_x + 0x48), 0x200);
            break;
        case button::left:
            p.speed_x = std::max<s16>(s16(p.speed_x - 0x48), -0x200);
            break;
        default:
            break;
        }
        moves_twice();
        p.speed_y = s16(p.speed_y + 0x20);
        // The body turns back upright, 2 a frame.
        s32 angle = s8(p.angle);
        if (angle < 0) {
            angle = std::min(angle + 2, 0);
        } else if (angle > 0) {
            angle = std::max(angle - 2, 0);
        }
        p.angle = u8(angle);
        if (p.speed_y >= 0) {
            land();
        }
        p.state = 2;
    }

    // 08049F9C: falling onto the ground lands (the dash's wait goes on).
    void land() {
        const u8 wait = p.special;
        const s32 d = ground_below();
        if (d < 0) {
            p.y += d * 0x100;
            p.ground_speed = p.speed_x;
            p.speed_y = 0;
            land_player(g, p);
            p.special = wait;
        }
    }
};

// 08049E08: the body, rotated while knocked back (state 4); every other
// two frames three afterimages follow at the positions of 2, 4 and 6
// frames before, each 8 pixels further back.
void draw_super_sonic(Game& g, Player& p) {
    StageState& s = g.stage();
    TransformedSprite& t = *p.body;
    Sprite& sp = t.sprite;
    sp.flags = (sp.flags &
                ~u32(sprite_flag::rotation_set | sprite_flag::rotated | sprite_flag::double_size)) |
               sprite_flag::flip_x;
    animate(g, sp);
    const bool shown = (p.flags & player_flag::dead) ||
                       (!(p.flags & player_flag::hidden) &&
                        (p.slip_timer != 0 || p.hurt_timer == 0 || (s.ticks & 2) == 0));
    if (!shown) {
        return;
    }
    const auto place = [&](s32 x, s32 y, u32 set) {
        sp.x = t.x = s16((x >> 8) - s.camera.x);
        sp.y = t.y = s16((y >> 8) - s.camera.y);
        if (p.state == 4) {
            sp.flags = (sp.flags & ~u32(sprite_flag::rotation_set | sprite_flag::flip_x)) |
                       sprite_flag::rotated | set;
            t.angle = u16(p.angle << 2);
            t.scale_x = -0x100;
            transform(g, t);
        }
        draw(g, sp);
    };
    place(p.x, p.y, 0);
    sp.flags |= sprite_flag::keep_colors | sprite_flag::keep_tiles;
    if ((s.ticks & 2) == 0) {
        u32 at = p.trail_newest;
        for (u32 i = 1; i < 4; ++i) {
            at -= 2;
            place(p.trail_x[at & 0xF] - s32(i) * 0x800, p.trail_y[at & 0xF], i);
        }
    }
    sp.flags &= ~(sprite_flag::keep_colors | sprite_flag::keep_tiles);
}

constexpr u32 part_groups = 0x084A745C; // per part: bit 0 picks the parts moving in the first step

// sine and cosine of a 16-bit angle (the table's 1024 steps), 8.8.
s32 sin16(const Assets& a, u16 angle) {
    return sine(a, angle >> 6) >> 6;
}

s32 cos16(const Assets& a, u16 angle) {
    return sine(a, (angle >> 6) + 0x100) >> 6;
}

// 0804CFAC: an angle turns toward `target` by `step`; true once there.
// (Turning down, arriving exactly counts at once; turning up, only on the
// next call.)
bool turn_toward(u16& angle, u16 target, u16 step) {
    if (target == angle) {
        return true;
    }
    if (s16(target - angle) < 0) {
        angle = u16(angle - step);
        if (s16(target - angle) < 0) {
            return false;
        }
    } else {
        angle = u16(angle + step);
        if (s16(target - angle) >= 0) {
            return false;
        }
    }
    angle = target;
    return true;
}

// A part of the boss: placed at its offset from its parent's joint, turned
// with the parent, and turned further by its own angle, which each part's
// move (by the boss's state) brings toward a target.
class BossPart : public MoonSprite {
public:
    BossPart(Game& g, MoonBoss& boss, BossPart* parent, s16 index, u32 tiles, bool own_tiles,
             u16 animation, u8 variant, s32 offset_x, s32 offset_y, u16 layer, u32 flags)
        : MoonSprite(g, 0x2001, tiles, own_tiles, animation, variant), boss_(boss), parent_(parent),
          offset_x_(offset_x), offset_y_(offset_y) {
        index_ = index;
        sprite().layer = layer;
        sprite().flags = flags;
    }

    u16 angle() const { return angle_; }

    u16 own_angle() const { return own_angle_; }

    s32 x() const { return x_; }

    s32 y() const { return y_; }

    // The boss's state changed: the move starts again.
    void restart() {
        stage_ = 0;
        timer_ = 0;
    }

    void set_priority_bits(u32 bits) {
        sprite().flags = (sprite().flags & ~sprite_flag::priority) | bits;
    }

    void set_scale(s16 scale) { view_.scale_x = view_.scale_y = scale; }

    void set_own_angle(u16 a) { own_angle_ = a; }

protected:
    // The joint: the offset turned by the parent's angle.
    void follow_parent(const Assets& a) {
        const u16 pa = parent_->angle_;
        const s32 c = cos16(a, pa), s = sin16(a, pa);
        dx_ = (c * offset_x_ - s * offset_y_) >> 8;
        dy_ = (s * offset_x_ + c * offset_y_) >> 8;
        x_ = dx_ + parent_->x_;
        y_ = dy_ + parent_->y_;
        angle_ = u16(pa + own_angle_);
    }

    bool turn(u16 target, u16 step) { return turn_toward(own_angle_, target, step); }

    bool first_group() const { return game_.assets().u8(part_groups + u32(index_)) & 1; }

    void report_done() { boss_.parts_done |= 1u << (index_ & 0xFF); }

    // Rotated by `angle` (0-3FF) at its view position.
    void draw_part(Game& g, u16 angle) {
        const StageState& s = g.stage();
        view_.x = view_x(s);
        view_.y = view_y(s);
        view_.angle = angle;
        draw_turned(g, sprite_flag::rotated);
    }

    // Not rotated.
    void draw_plain(Game& g) {
        const StageState& s = g.stage();
        Sprite& sp = sprite();
        sp.x = view_x(s);
        sp.y = view_y(s);
        animate(g, sp);
        draw(g, sp);
    }

    u8 state() const { return boss_.state; }

    bool beaten() const { return boss_.flags & MoonBoss::beaten; }

    MoonBoss& boss_;
    BossPart* parent_;        // +08
    s32 dx_{}, dy_{};         // +60, +64 the turned offset
    s32 offset_x_, offset_y_; // +68, +6C
    u16 angle_{};             // +70
    u16 own_angle_{};         // +76
};

// 0804B53C / 0804B3EC: a spark on the ground where it was made (riding
// the ground's surface as it rolls by); it hurts unless the player dashes.
// Gone at its animation's end or off the view.
class GroundSpark final : public MoonSprite {
public:
    GroundSpark(Game& g, s32 x, s32 y)
        : MoonSprite(g, 0x2002, g.sprite_tiles.allocate(0x18), true, 0x2A0, 0) {
        x_ = x;
        y_ = y;
        sprite().layer = 0x300;
        sprite().flags = 0x1000;
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        Sprite& sp = sprite();
        if ((sp.flags & sprite_flag::ended) || u32(x_ + 0x2000) > 0x22000) {
            g.scheduler().destroy(*this);
            return;
        }
        const auto& c = s.camera;
        y_ += floor_distance(g, (x_ >> 8) + (moon_ground_x(s, c.x) - c.x), y_ >> 8) * 0x100;
        Player& p = s.player[0];
        const MoonBoss& boss = *static_cast<const MoonBoss*>(s.moon_boss);
        if (p.move == 0 && p.hurt_timer == 0 && !(boss.flags & MoonBoss::beaten)) {
            hazard_hits_second(g, sp, s16(x_ >> 8), s16(y_ >> 8), p);
        }
        sp.x = view_x(s);
        sp.y = view_y(s);
        animate(g, sp);
        draw(g, sp);
        x_ += speed_x_;
        y_ += speed_y_;
        speed_x_ = s16(speed_x_ + accel_x_);
        speed_y_ = s16(speed_y_ + accel_y_);
    }
};

// 0804A874 / 0804AA90: a rock thrown up where a fist pounds the ground
// (kinds 0 and 1; kind 2 the bits of a broken rock), turning as it flies.
// It hurts; a dash breaks it into smaller ones.
class Rock final : public MoonSprite {
public:
    Rock(Game& g, u16 priority, s32 x, s32 y, s16 kind)
        : MoonSprite(g, priority,
                     kind == 0   ? 0x12E80
                     : kind == 1 ? 0x13080
                                 : 0x13100,
                     false, u16(0x2A4 + kind), 0) {
        StageState& s = g.stage();
        index_ = kind;
        x_ = x;
        y_ = y;
        if (x == 0x20000) {
            speed_x_ = s16(-((s.next_random() >> 8) & 0x3FF));
            speed_y_ = s16((s.next_random() >> 8) & 0xFF);
        } else {
            speed_x_ = s16(((s.next_random() >> 8) & 0x3FF) - 0x300);
            speed_y_ = s16(-0x80 - s32((s.next_random() >> 8) & 0x3FF));
        }
        if (kind == 2) {
            speed_x_ = s16(speed_x_ >> 2);
            speed_y_ = s16(speed_y_ >> 2);
        }
        accel_y_ = 7;
        turning_ = u16(s.next_random() >> 8);
        spin_ = u16(((s.next_random() >> 8) & 0x1FFF) - 0x1000);
        sprite().layer = 0x300;
        sprite().flags = 0x1000;
        if (kind == 0) {
            spin_ = u16(s16(spin_) >> 1);
            view_.scale_x = s16(view_.scale_x - ((s.next_random() >> 8) & 0x40));
            view_.scale_y = s16(view_.scale_y - ((s.next_random() >> 8) & 0x40));
        }
        if (s.next_random() & 0x10000) {
            view_.scale_y = s16(-view_.scale_y);
        }
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        if (u32(x_ + 0x2000) > 0x22000 || u32(y_ + 0x2000) > 0x16000) {
            g.scheduler().destroy(*this);
            return;
        }
        Player& p = s.player[0];
        Sprite& sp = sprite();
        const s16 ax = s16(x_ >> 8), ay = s16(y_ >> 8);
        if (p.move == 0) {
            if (p.hurt_timer == 0 && !boss_beaten(s)) {
                hazard_hits_second(g, sp, ax, ay, p);
            }
        } else if (attack_bounces(g, sp, ax, ay, p)) {
            for (s32 n = 3 - index_; n != 0; --n) {
                g.scheduler().create<Rock>(g, u16(priority() + 1), x_, y_, 2);
            }
            g.sound().start(0x7E);
            g.scheduler().destroy(*this);
            return;
        }
        view_.x = view_x(s);
        view_.y = view_y(s);
        view_.angle = u16(turning_ >> 6);
        draw_turned(g, sprite_flag::rotated);
        x_ += speed_x_;
        y_ += speed_y_;
        speed_x_ = s16(speed_x_ + accel_x_);
        speed_y_ = s16(speed_y_ + accel_y_);
        turning_ = u16(turning_ + spin_);
    }

private:
    u16 turning_{}; // +70
};

// 0804AF98 / 0804B0A4: a half of a broken claw, thrown back and up,
// turning, falling.
class ClawPiece final : public MoonSprite {
public:
    ClawPiece(Game& g, u16 priority, s32 x, s32 y, bool second)
        : MoonSprite(g, priority, 0x12580, false, 0x29F, 0) {
        StageState& s = g.stage();
        x_ = x;
        y_ = y;
        speed_x_ = s16(-(s32((s.next_random() >> 8) & 0x7F) + 0x80));
        speed_y_ = s16(((s.next_random() >> 8) & 0x7F) + 0x40);
        accel_y_ = 7;
        spin_ = u16(((s.next_random() >> 8) & 0x7FF) - 0x400);
        if (second) {
            view_.scale_x = -0x100;
            speed_x_ = s16(-speed_x_);
        }
        sprite().layer = 0x300;
        sprite().flags = 0x1000;
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        if (u32(x_ + 0x2000) > 0x22000 || u32(y_ + 0x2000) > 0x16000) {
            g.scheduler().destroy(*this);
            return;
        }
        view_.x = view_x(s);
        view_.y = view_y(s);
        view_.angle = u16(turning_ >> 6);
        draw_turned(g, sprite_flag::rotated);
        x_ += speed_x_;
        y_ += speed_y_;
        speed_x_ = s16(speed_x_ + accel_x_);
        speed_y_ = s16(speed_y_ + accel_y_);
        turning_ = u16(turning_ + spin_);
    }

private:
    u16 turning_{}; // +70
};

void break_claw(Game& g, u16 priority, s32 x, s32 y) {
    for (int i = 0; i < 2; ++i) {
        g.scheduler().create<ClawPiece>(g, priority, x, y, i == 1);
    }
}

// 0804B288 and its stages: a claw shot from the boss's chest. It flies
// left for 30h frames (0804AC18), opens and follows the player's height
// (0804ACD8) until level with it, then shoots ahead (0804B33C). Dashing
// into it breaks it (eight rings fly out); otherwise touching it, it grabs
// the player (0804ADE0), closes (0804AECC) and holds on for F0h frames
// (shorter for each left or right pressed), the player hidden in it.
class Claw final : public MoonSprite {
public:
    Claw(Game& g, s32 x, s32 y)
        : MoonSprite(g, 0x2001, g.sprite_tiles.allocate(8), true, 0x29E, 0) {
        x_ = x;
        y_ = y;
        speed_x_ = -0x100;
        timer_ = 0x30;
        sprite().layer = 0x300;
        sprite().flags = 0x1000;
    }

    void update(Game& g) override {
        switch (phase_) {
        case Phase::flying:
            fly(g);
            break;
        case Phase::homing:
            home(g);
            break;
        case Phase::shooting:
            shoot(g);
            break;
        case Phase::grabbing:
            grab(g);
            break;
        case Phase::holding:
            hold(g);
            break;
        }
    }

private:
    enum class Phase : u8 { flying, homing, shooting, grabbing, holding };

    // 0804B2F8: drawn twice, the second time upside down (the claw's
    // lower half).
    void draw_both(Game& g, s16 x, s16 y) {
        Sprite& sp = sprite();
        sp.x = x;
        sp.y = y;
        animate(g, sp);
        sp.flags |= sprite_flag::keep_colors | sprite_flag::keep_tiles | sprite_flag::flip_y;
        draw(g, sp);
        sp.flags &= ~(sprite_flag::keep_colors | sprite_flag::keep_tiles | sprite_flag::flip_y);
        draw(g, sp);
    }

    void set_variant(u8 v) {
        sprite().animation = 0x29E;
        sprite().variant = v;
        sprite().previous_variant = 0xFF;
    }

    bool out_of_reach() const { return u32(x_ + 0x2000) > 0x20000; }

    // 0804AC18
    void fly(Game& g) {
        StageState& s = g.stage();
        if (out_of_reach()) {
            g.scheduler().destroy(*this);
            return;
        }
        draw_both(g, view_x(s), view_y(s));
        move();
        if (--timer_ < 1) {
            set_variant(2);
            speed_x_ = speed_y_ = 0;
            phase_ = Phase::homing;
        }
    }

    // 0804B1C8: the player's attack meets it: a dash breaks it (-1); a
    // player not blinking is grabbed (1).
    int touched(Game& g) {
        StageState& s = g.stage();
        Player& p = s.player[0];
        if ((p.flags & player_flag::grabbed) ||
            !attack_bounces(g, sprite(), s16(x_ >> 8), s16(y_ >> 8), p)) {
            return 0;
        }
        if (p.move != 0) {
            g.sound().start(0x8A);
            spread_rings(g, x_ >> 8, y_ >> 8, 8);
            break_claw(g, u16(priority() + 1), x_, y_);
            g.scheduler().destroy(*this);
            return -1;
        }
        if (p.hurt_timer == 0 && !boss_beaten(s)) {
            set_variant(3);
            p.flags |= player_flag::grabbed;
            phase_ = Phase::grabbing;
            return 1;
        }
        return 0;
    }

    // 0804ACD8 and 0804B33C.
    bool chase(Game& g) {
        StageState& s = g.stage();
        if (out_of_reach()) {
            g.scheduler().destroy(*this);
            return false;
        }
        const s16 sx = view_x(s), sy = view_y(s);
        const int t = touched(g);
        if (t < 0) {
            return false;
        }
        draw_both(g, sx, sy);
        if (t > 0) {
            return false;
        }
        move();
        return true;
    }

    void home(Game& g) {
        if (!chase(g)) {
            return;
        }
        const s32 dy = g.stage().player[0].y - y_;
        if (dy < -0x800) {
            speed_y_ = -0x100;
        } else if (dy <= 0x800) {
            speed_x_ = -0x300;
            speed_y_ = 0;
            phase_ = Phase::shooting;
        } else {
            speed_y_ = 0x100;
        }
    }

    void shoot(Game& g) { chase(g); }

    // 0804ADE0: closes on the player; once closed the player is gone in it.
    void grab(Game& g) {
        StageState& s = g.stage();
        Player& p = s.player[0];
        if (p.hurt_timer != 0) {
            p.flags &= ~player_flag::grabbed;
            break_claw(g, u16(priority() + 1), x_, y_);
            g.scheduler().destroy(*this);
            return;
        }
        x_ = p.x;
        y_ = p.y;
        draw_both(g, s16((p.x >> 8) - s.camera.x), s16((p.y >> 8) - s.camera.y));
        if (sprite().flags & sprite_flag::ended) {
            timer_ = 0xF0;
            p.ground_speed = p.speed_x = p.speed_y = p.hurt_timer;
            p.move = 0;
            p.flags |= player_flag::hidden | player_flag::no_movement;
            set_variant(1);
            phase_ = Phase::holding;
        }
    }

    // 0804AECC: shaken left and right with the buttons.
    void hold(Game& g) {
        StageState& s = g.stage();
        Player& p = s.player[0];
        if (p.hurt_timer == 0) {
            const s32 before = timer_;
            if (--timer_ >= 0) {
                if (p.pressed & (button::right | button::left)) {
                    timer_ = before - 0x15;
                }
                SuperMovement(g, p).follow_ground();
                x_ = p.x + shake_;
                y_ = p.y;
                shake_ =
                    (s32(p.pressed & button::right) * 2 - s32(p.pressed & button::left)) * 0x20;
                draw_both(g, view_x(s), view_y(s));
                return;
            }
        }
        p.flags &= ~(player_flag::grabbed | player_flag::no_movement | player_flag::hidden);
        break_claw(g, u16(priority() + 1), x_, y_);
        g.scheduler().destroy(*this);
    }

    Phase phase_ = Phase::flying;
    s32 shake_{}; // +68
};

// 08052804 / 08050854: the body, drawn into the turned layer 2 (its own
// tiles at 4040h there, its colours as background colours). It hurts.
class Body final : public BossPart {
public:
    Body(Game& g, MoonBoss& boss)
        : BossPart(g, boss, nullptr, 0, 0x4040, false, 0x29A, 0, 0, 0, 0x15 << 6, 0x2000) {}

    void update(Game& g) override {
        StageState& s = g.stage();
        const Assets& a = g.assets();
        Player& p = s.player[0];
        x_ = dx_ + boss_.base_x * 0x100 + boss_.x;
        y_ = dy_ + boss_.base_y * 0x100 + boss_.y;
        angle_ = own_angle_;
        const s16 sx = view_x(s), sy = view_y(s);
        if (p.hurt_timer == 0 && !beaten()) {
            hazard_hits_second(g, sprite(), s16(x_ >> 8), s16(y_ >> 8), p);
        }
        Sprite& sp = sprite();
        sp.x = 0x80;
        sp.y = 0x80;
        sp.flags = (sp.flags & ~0x18000u) | 0x10000; // into layer 2
        animate_background(g, sp);
        draw_into_map(g, sp);
        rotate_layer(g.video().settings.bg[2], a, u16(angle_ >> 6), 0x100, 0x100, 0x7C, 0x80, sx,
                     sy);
        const auto lean_with = [&](const BossPart& shoulder, bool turning) {
            const s16 lean = s16(shoulder.own_angle() - 0x4000);
            if (turning) {
                turn(u16(lean >> 2), 0x40);
            } else {
                own_angle_ = u16(lean >> 2);
            }
        };
        switch (state()) {
        case MoonBoss::arriving:
            turn(0x800, 0x80);
            break;
        case MoonBoss::braking:
            turn(0xF800, 0x80);
            break;
        case MoonBoss::step_a:
            lean_with(*boss_.parts[2], true);
            break;
        case MoonBoss::step_b:
            lean_with(*boss_.parts[2], false);
            break;
        case MoonBoss::step_c:
            lean_with(*boss_.parts[7], true);
            break;
        case MoonBoss::step_d:
            lean_with(*boss_.parts[7], false);
            break;
        case MoonBoss::broken:
            turn(0, 0x80);
            break;
        case MoonBoss::leaving:
            turn(0x4000, 0x200);
            break;
        default:
            turn(0, 0x80);
            break;
        }
    }
};

// 08052440 / 08050A54: the head, the boss's weak spot: a dash into it is a
// hit (it flashes for 20h frames; the player is thrown back). Otherwise it
// hurts. It aims and fires the beam, and sends the sparks.
class Head final : public BossPart {
public:
    Head(Game& g, MoonBoss& boss, BossPart& body)
        : BossPart(g, boss, &body, 1, g.sprite_tiles.allocate(0x40), true, 0x299, 0, 0x400, -0x1C00,
                   0x16 << 6, 0x2000) {}

    void update(Game& g) override;

private:
    bool beam_window(Game& g, s32 x, s32 y, u16 direction);
};

// 080524BC / 08050F80: the shoulders (parts 2 and 7), with the upper arms.
class Shoulder final : public BossPart {
public:
    using BossPart::BossPart;

    void update(Game& g) override {
        follow_parent(g.assets());
        draw_part(g, 0); // placed and scaled, not turned
        const auto swing = [&](u16 base) {
            const s32 t = std::min<s32>(timer_, 0x10);
            const s32 c = sine(game_.assets(), u32((t & 0x3F) * 0x10 + 0x100)) >> 6;
            turn(u16(base + u16(0x100 - c) * -0x40), 0x800);
            if (timer_++ >= 0x4C) {
                report_done();
            }
        };
        const auto raise = [&] {
            turn(0x6000, 0x100);
            if (own_angle_ == 0x6000 && timer_++ >= 0x1E) {
                report_done();
            }
        };
        switch (state()) {
        case MoonBoss::settling:
            turn(0, 0x100);
            break;
        case MoonBoss::standing:
            turn(first_group() ? 0x1000 : 0x2000, 0x200);
            break;
        case MoonBoss::step_a:
            if (first_group()) {
                raise();
            }
            break;
        case MoonBoss::step_b:
            if (first_group()) {
                swing(0x5800);
            }
            break;
        case MoonBoss::step_c:
            if (!first_group()) {
                raise();
            }
            break;
        case MoonBoss::step_d:
            if (!first_group()) {
                swing(0x6000);
            }
            break;
        case MoonBoss::stomp:
        case MoonBoss::pounding:
            if (turn(0xF000, 0x100) && boss_.timer++ > 0x3B) {
                report_done();
            }
            break;
        case MoonBoss::broken:
            turn(0xD000, 0x400);
            break;
        case MoonBoss::leaving:
            turn(0xF000, 0x100);
            break;
        default:
            turn(0, 0x100);
            break;
        }
    }
};

// 08052544 / 080518B4: the arms' round joints (parts 3, 5, 8, 10).
class ArmJoint final : public BossPart {
public:
    using BossPart::BossPart;

    void update(Game& g) override {
        follow_parent(g.assets());
        draw_plain(g);
        turn(0, 0x100);
    }
};

// 080525AC / 08051310: the forearms (parts 4 and 9).
class Forearm final : public BossPart {
public:
    using BossPart::BossPart;

    void update(Game& g) override {
        follow_parent(g.assets());
        draw_part(g, 0); // placed and scaled, not turned
        const bool first = first_group();
        switch (state()) {
        case MoonBoss::settling:
            turn(0, 0x100);
            break;
        case MoonBoss::standing:
            turn(first ? 0x6000 : 0x6800, 0x200);
            break;
        case MoonBoss::step_a:
            if (first) {
                turn(0x6000, 0x100);
            }
            break;
        case MoonBoss::step_b:
            if (first) {
                turn(0x1000, 0x400);
            }
            break;
        case MoonBoss::step_c:
            if (!first) {
                turn(0x6000, 0x100);
            }
            break;
        case MoonBoss::step_d:
            if (!first) {
                turn(0x1000, 0x400);
            }
            break;
        case MoonBoss::stomp:
            turn(0x4000, 0x200);
            break;
        case MoonBoss::pounding:
            // Shaking.
            turn(u16(0x4000 - ((g.stage().next_random() & 0xFFFFF) >> 8)), 0x1000);
            break;
        case MoonBoss::broken:
            turn(0x2000, 0x400);
            break;
        case MoonBoss::leaving:
            turn(0x1000, 0x100);
            break;
        default:
            turn(0, 0x100);
            break;
        }
    }
};

// 08052628 / 080515D0: the fists (parts 6 and 11): their knuckles hurt;
// pounding the ground they throw up rocks.
class Fist final : public BossPart {
public:
    using BossPart::BossPart;

    void update(Game& g) override {
        StageState& s = g.stage();
        const Assets& a = g.assets();
        Player& p = s.player[0];
        follow_parent(a);
        if (p.hurt_timer == 0 && !beaten()) {
            const u16 k = u16(angle_ + 0xC000);
            hazard_hits_second(g, sprite(), s16((x_ >> 8) + ((cos16(a, k) * -0x1700) >> 16)),
                               s16((y_ >> 8) + ((sin16(a, k) * -0x1700) >> 16)), p);
        }
        draw_part(g, u16(u16(angle_ - 0x4000) >> 6));
        switch (state()) {
        case MoonBoss::settling:
            turn(0x4000, 0x200);
            break;
        case MoonBoss::standing:
            turn(0, 0x200);
            break;
        case MoonBoss::stomp:
            turn(u16(-parent_->angle()), 0x200);
            break;
        case MoonBoss::pounding:
            turn(u16(-parent_->angle()), 0x200);
            if ((s.ticks & 0x1F) == 0) {
                const s16 kind = (s.next_random() & 0x30000) == 0 ? 1 : 0;
                g.scheduler().create<Rock>(g, u16(priority() + 1), x_, y_ + 0x1700, kind);
            }
            break;
        case MoonBoss::broken:
            turn(0xB000, 0x400);
            break;
        default:
            turn(0, 0x100);
            break;
        }
    }
};

// 08052690 / 080519B4: the hips (parts 12 and 15), with the thighs.
class Hip final : public BossPart {
public:
    using BossPart::BossPart;

    void update(Game& g) override {
        follow_parent(g.assets());
        draw_part(g, u16(angle_ >> 6));
        const bool first = first_group();
        switch (state()) {
        case MoonBoss::arriving:
            turn(0x1000, 0x80);
            break;
        case MoonBoss::settling:
        case MoonBoss::standing:
            turn(0, 0x200);
            break;
        case MoonBoss::step_a:
            turn(parent_->own_angle(), 0x100);
            break;
        case MoonBoss::step_b:
            if (!first) {
                turn(0xE800, 0x200);
            }
            break;
        case MoonBoss::step_c:
            break;
        case MoonBoss::step_d:
            if (first) {
                turn(0xE800, 0x200);
            }
            break;
        case MoonBoss::broken:
            turn(0x4000, 0x200);
            if (boss_.flash == 0 && own_angle_ == 0x4000) {
                report_done();
            }
            break;
        case MoonBoss::leaving:
            turn(0xE800, 0x100);
            break;
        default:
            turn(0, 0x80);
            break;
        }
    }
};

// 080526F0 / 08051FF8: the cover over a hip joint, unturned.
class HipCover final : public BossPart {
public:
    using BossPart::BossPart;

    void update(Game& g) override {
        x_ = dx_ + parent_->x();
        y_ = dy_ + parent_->y();
        angle_ = u16(parent_->angle() + own_angle_);
        draw_plain(g);
        turn(0, 0x100);
    }
};

// 0805274C / 08051C10: the legs below the knees (parts 13 and 16).
class Leg final : public BossPart {
public:
    using BossPart::BossPart;

    void update(Game& g) override {
        follow_parent(g.assets());
        draw_part(g, u16(angle_ >> 6));
        switch (state()) {
        case MoonBoss::arriving:
            turn(0x2000, 0x80);
            break;
        case MoonBoss::braking:
            turn(0xE000, 0x80);
            break;
        case MoonBoss::settling:
        case MoonBoss::stomp:
        case MoonBoss::pounding:
            turn(0, 0x200);
            break;
        case MoonBoss::broken:
            turn(0, 0x400);
            break;
        case MoonBoss::leaving:
            turn(0x1800, 0x100);
            break;
        default:
            turn(0x2000, 0x100);
            break;
        }
    }
};

// 080527A8 / 08051E04: the feet (parts 14 and 17), kept level while
// walking.
class Foot final : public BossPart {
public:
    using BossPart::BossPart;

    void update(Game& g) override {
        follow_parent(g.assets());
        draw_part(g, u16(angle_ >> 6));
        switch (state()) {
        case MoonBoss::arriving:
        case MoonBoss::beam:
        case MoonBoss::sparks:
            turn(0xE000, 0x100);
            break;
        case MoonBoss::settling:
        case MoonBoss::standing:
            break;
        case MoonBoss::step_a:
        case MoonBoss::step_b:
        case MoonBoss::step_c:
        case MoonBoss::step_d:
            if (angle_ > 0xFFF) {
                turn(0, 0x100);
            } else {
                turn(u16(-angle_), 0x100);
            }
            break;
        case MoonBoss::broken:
            turn(0xF800, 0x400);
            break;
        default:
            turn(0, 0x100);
            break;
        }
    }
};

// 0805211C: the beam from the head's mouth towards the left, along
// `direction` (16-bit angle): a band 3 lines high in window 0 (drawn line
// by line, 08005CFC), brightened inside. False when nothing of it is in
// view.
bool Head::beam_window(Game& g, s32 x, s32 y, u16 direction) {
    const Assets& a = g.assets();
    const s32 c = cos16(a, angle_), s = sin16(a, angle_);
    x += (s * 0x1000 + c * -0xF00) >> 16;
    y += (c * -0x1000 + s * -0xF00) >> 16;
    s32 y_low = y + 3;
    s32 end_x = 0, end_x_low = 0;
    const s32 slope = s16(divide(sin16(a, direction) << 8, cos16(a, direction)));
    const s32 step = divide(0x10000, slope);
    s32 end_y = y - ((slope * x) >> 8); // at the left edge
    s32 end_y_low = end_y + 3;
    if (x > 0xEF) {
        const s32 d = (slope * (0xF0 - x)) >> 8;
        y += d;
        y_low += d;
        x = 0xEF;
    }
    if (y >= 0x9F) {
        return false;
    }
    s32 x_top = x, x_low = x;
    if (y < 0) {
        x_top = x - ((step * y) >> 8);
        y = 0;
        if (y_low < 0) {
            x_low = x - ((step * y_low) >> 8);
            y_low = 0;
        }
    }
    y_low = std::clamp(y_low, 0, 0x9F);
    if (end_y_low > 0x9F) {
        end_x_low += (step * (0xA0 - end_y_low)) >> 8;
        end_y_low = 0x9F;
        if (end_y > 0x9F) {
            end_x = (step * (0xA0 - end_y)) >> 8;
            end_y = 0x9F;
        }
    }
    if (end_x >= 0xEF || x == end_x || y == end_y || y_low == end_y_low) {
        return false;
    }
    end_x = std::clamp(end_x, 0, 0xEF);
    end_x_low = std::clamp(end_x_low, 0, 0xEF);
    const std::array<u8, 8> points = {u8(x_top), u8(y),     u8(end_x),     u8(end_y),
                                      u8(x_low), u8(y_low), u8(end_x_low), u8(end_y_low)};
    auto& vs = g.video();
    window_band(vs, 0, points, x + 1);
    auto& v = vs.settings;
    v.window0 = true;
    v.windows[0].top = u8(y);
    v.windows[0].bottom = u8(end_y_low);
    v.inside[0] = 0x3F;
    v.inside[1] = 0x3F;
    v.outside = 0x1F;
    v.inside_sprite_window = 0x1F;
    v.blend.set_control(0x3FBF);
    v.blend.evy = 0xE;
    return true;
}

void Head::update(Game& g) {
    StageState& s = g.stage();
    Player& p = s.player[0];
    auto& vs = g.video();
    auto& v = vs.settings;
    follow_parent(g.assets());
    const s16 sx = view_x(s), sy = view_y(s);
    Sprite& sp = sprite();
    const s16 ax = s16(x_ >> 8), ay = s16(y_ >> 8);
    if (p.move == 0 || !attack_bounces(g, sp, ax, ay, p)) {
        if (p.hurt_timer == 0 && !beaten()) {
            hazard_hits_second(g, sp, ax, ay, p);
        }
    } else if (!beaten() && boss_.flash < 1) {
        // A hit: the player bounces off.
        boss_.flash = 0x20;
        p.move = 0;
        p.hurt_timer = 8;
        p.speed_x = -0x200;
        p.ground_speed = -0x200;
        if (s8(--boss_.hits_left) < 1) {
            v.window0 = false;
            v.blend.set_control(0);
            vs.line_effect_on = false;
            boss_.state = boss_.next_state = MoonBoss::broken;
            boss_.speed_x = 0x180;
            boss_.speed_y = -0x100;
            boss_.flags |= MoonBoss::beaten;
            boss_.timer = 0;
            s.flags |= stage_flag::paused;
            const u32 before = s.score;
            s.score += 1000;
            if (divide(s32(s.score), 50000) != divide(s32(before), 50000) && g.game_mode == 0) {
                ++s.lives;
            }
        }
    }
    // The hit flash (the head as the sprite window, everything in it white).
    sp.flags &= ~sprite_flag::mode;
    if (!beaten() || boss_.flash != 0) {
        flash_off(g);
        if (boss_.flash != 0) {
            if ((s.ticks & 2) == 0) {
                sp.flags |= 0x100;
                flash_on(g);
            }
            --boss_.flash;
        }
    }
    view_.x = sx;
    view_.y = sy;
    view_.angle = u16(angle_ >> 6);
    draw_turned(g, sprite_flag::rotated);
    switch (state()) {
    case MoonBoss::settling:
    case MoonBoss::standing:
        turn(0, 0x200);
        break;
    case MoonBoss::aiming:
        if (turn(0xF000, 0x200)) {
            report_done();
        }
        break;
    case MoonBoss::beam: {
        v.window0 = false;
        if (timer_ == 0) {
            sp.animation = 0x299;
            sp.variant = 1;
            sp.previous_variant = 0xFF;
            g.sound().start(0x9B);
        }
        if (timer_ == 10) {
            g.sound().stop(0x9B);
        }
        if (timer_ > 0xF) {
            report_done();
            v.blend.set_control(0);
            vs.line_effect_on = false;
            sp.animation = 0x299;
            sp.variant = 0;
            sp.previous_variant = 0xFF;
            return;
        }
        // The beam sweeps: its direction follows a cosine of the time.
        const s32 c = sine(g.assets(), u32((timer_ << 4) & 0x3FF) + 0x100) >> 6;
        if (!(p.flags & player_flag::dead) &&
            !beam_window(g, sx, sy, u16((0x100 - c) * 0x20 + 0x5C00))) {
            vs.line_effect_on = false;
        }
        ++timer_;
        break;
    }
    case MoonBoss::sparks:
        // Every eighth frame a spark further left on the ground.
        if ((timer_ & 7) == 0) {
            const s32 at = x_ - timer_ * 0x400 - 0x6400;
            if (at < -0x2000) {
                report_done();
            } else {
                g.scheduler().create<GroundSpark>(g, at, boss_.base_y << 8);
            }
        }
        ++timer_;
        break;
    case MoonBoss::broken:
        turn(0x1800, 0x100);
        break;
    case MoonBoss::leaving:
        turn(0x2000, 0x100);
        break;
    default:
        turn(0, 0x100);
        break;
    }
}

MoonBoss::MoonBoss(Game& g, const ObjectSpawn& o) : Process(0x2000), game(g) {
    o.record[0] = 0xFE;
    base_y = o.cell_y * 0x100 + o.record[1] * 8;
    x = -0x10000;
    y = -0x3600;
    hits_left = u8(8 - g.save().unknown_5178 * 2);
}

void MoonBoss::build(Game& g) {
    auto& sch = g.scheduler();
    auto& sp = g.sprite_tiles;
    constexpr u32 arm_tiles = 0x12E00, fist_tiles = 0x12600;
    // The arms in front (layer bits 1000h) or behind.
    const auto front = [](BossPart& p) -> BossPart& {
        p.set_priority_bits(0x1000);
        return p;
    };
    BossPart& body = sch.create<Body>(g, *this);
    parts[0] = &body;
    parts[1] = &sch.create<Head>(g, *this, body);
    BossPart& shoulder_a = sch.create<Shoulder>(g, *this, &body, 2, arm_tiles, false, 0x2A2, 0,
                                                0xB00, -0x1700, 0xF << 6, 0x2040);
    shoulder_a.set_scale(0x180);
    parts[2] = &front(shoulder_a);
    parts[3] = &front(sch.create<ArmJoint>(g, *this, parts[2], 3, arm_tiles, false, 0x2A2, 0, 0,
                                           0x1400, 0x10 << 6, 0x2000));
    BossPart& forearm_a = sch.create<Forearm>(g, *this, parts[3], 4, arm_tiles, false, 0x2A2, 0, 0,
                                              0x1200, 0xF << 6, 0x2040);
    forearm_a.set_scale(0x140);
    parts[4] = &front(forearm_a);
    parts[5] = &front(sch.create<ArmJoint>(g, *this, parts[4], 5, arm_tiles, false, 0x2A2, 0, 0,
                                           0x1200, 0x10 << 6, 0x2000));
    parts[6] = &front(sch.create<Fist>(g, *this, parts[5], 6, fist_tiles, false, 0x2A1, 0, 0, 0x800,
                                       0xE << 6, 0x2000));
    BossPart& shoulder_b = sch.create<Shoulder>(g, *this, &body, 7, arm_tiles, false, 0x2A2, 0,
                                                -0x500, -0x1900, 0x1B << 6, 0x2040);
    shoulder_b.set_scale(0x180);
    parts[7] = &shoulder_b;
    parts[8] = &sch.create<ArmJoint>(g, *this, parts[7], 8, arm_tiles, false, 0x2A2, 0, 0, 0x1400,
                                     0x1C << 6, 0x2000);
    BossPart& forearm_b = sch.create<Forearm>(g, *this, parts[8], 9, arm_tiles, false, 0x2A2, 0, 0,
                                              0x1200, 0x1B << 6, 0x2040);
    forearm_b.set_scale(0x140);
    parts[9] = &forearm_b;
    parts[10] = &sch.create<ArmJoint>(g, *this, parts[9], 10, arm_tiles, false, 0x2A2, 0, 0, 0x1200,
                                      0x1C << 6, 0x2000);
    parts[11] = &sch.create<Fist>(g, *this, parts[10], 11, fist_tiles, false, 0x2A1, 0, 0, 0x800,
                                  0x1A << 6, 0x2000);
    BossPart& hip_a = sch.create<Hip>(g, *this, &body, 12, sp.allocate(0x10), true, 0x29B, 0, 0x200,
                                      0x500, 0x14 << 6, 0x2000);
    hip_a.set_own_angle(0xE000);
    parts[12] = &front(hip_a);
    front(sch.create<HipCover>(g, *this, parts[12], 0, sp.allocate(9), true, 0x29D, 0, 0, 0,
                               0x13 << 6, 0x2000));
    parts[13] = &front(sch.create<Leg>(g, *this, parts[12], 13, sp.allocate(0x40), true, 0x29C, 0,
                                       -0x900, 0xC00, 0x12 << 6, 0x2000));
    parts[14] = &front(sch.create<Foot>(g, *this, parts[13], 14, sp.allocate(0x10), true, 0x29B, 2,
                                        0x300, 0x1B00, 0x11 << 6, 0x2000));
    BossPart& hip_b = sch.create<Hip>(g, *this, &body, 15, sp.allocate(0x10), true, 0x29B, 1,
                                      -0x600, 0x500, 0x17 << 6, 0x2000);
    hip_b.set_own_angle(0x2000);
    parts[15] = &hip_b;
    parts[16] = &sch.create<Leg>(g, *this, parts[15], 16, sp.allocate(0x40), true, 0x29C, 1, -0x900,
                                 0xB00, 0x16 << 6, 0x2000);
    parts[17] = &sch.create<Foot>(g, *this, parts[16], 17, sp.allocate(0x10), true, 0x29B, 3, 0x300,
                                  0x1B00, 0x18 << 6, 0x2000);
    if (g.controller().held == 0xFA) {
        flags |= debug_start;
    }
}

// 08050160: how far the boss is above the ground: the nearer of both feet
// (two points each, at the ground's position below them) and a point
// under the body. A foot on the ground now and then kicks up dust (one
// foot on odd frames, the other on even ones).
s32 MoonBoss::ground() const {
    Game& g = game;
    StageState& s = g.stage();
    const Assets& a = g.assets();
    const auto ground_x = [&](s32 x) {
        x += s32(s.ticks * 8);
        if (x - 0x48 > 0xA7F) {
            x = remainder(x - 0x48, 0xA80) + 0x48;
        }
        return x;
    };
    const auto foot = [&](const BossPart& f, const BossPart& leg, bool odd_frames) {
        const s32 gx = ground_x(f.x() >> 8);
        const u16 fa = f.angle(), la = leg.angle();
        const s32 heel = (f.y() >> 8) + ((cos16(a, fa) * 0x900 + sin16(a, fa) * -0x1000) >> 16) + 6;
        const s32 d_heel = floor_distance(g, gx + 0x10, heel);
        const s32 toe = (f.y() >> 8) + ((sin16(a, la) * 0x1500 + cos16(a, la) * 0x800) >> 16) + 6;
        const s32 d = std::min(d_heel, floor_distance(g, gx + 0x15, toe));
        if (d < 1 && state != braking && bool(s.ticks & 1) == odd_frames &&
            (s.next_random() & 0x30000) == 0) {
            create_skid_dust(g, s16(f.x() >> 8), s16(std::max(toe, heel) - 2), -4);
        }
        return d;
    };
    const s32 near = foot(*parts[14], *parts[13], true);
    const s32 far = foot(*parts[17], *parts[16], false);
    const BossPart& body = *parts[0];
    const u16 ba = body.angle();
    const s32 c = cos16(a, ba), sn = sin16(a, ba);
    const s32 bx = ground_x((body.x() >> 8) + ((c * 0x1B00 + sn * 0x1600) >> 16));
    const s32 under = floor_distance(g, bx, (body.y() >> 8) + ((sn * 0x1B00 + c * -0x1600) >> 16));
    return std::min({near, far, under});
}

// 08052084: smoke from the beaten boss (every 16th frame; leaving, every
// eighth).
void MoonBoss::smoke(Game& g) {
    StageState& s = g.stage();
    if ((s.ticks & 0xF) == 0 || (state == leaving && (s.ticks & 7) == 0)) {
        const u32 r1 = s.next_random();
        const u32 r2 = s.next_random();
        Particle& p = create_smoke(g, x + base_x * 0x100 + s32((r1 & 0x3FFFFF) >> 8) - 0x2000,
                                   y + base_y * 0x100 + s32((r2 & 0x7FFFFF) >> 8) - 0x4000);
        p.sprite.flags = 0x1000;
        p.sprite.layer = 0x380;
    }
}

void MoonBoss::update(Game& g) {
    // 080523A0 / 080523C4 and 080523F0: standing, or dropping onto the
    // ground.
    const auto move = [&] {
        x += speed_x;
        y += speed_y;
        speed_x = s16(speed_x + accel_x);
        speed_y = s16(speed_y + accel_y);
    };
    if (!(flags & waiting)) {
        if (!(flags & falling)) {
            const s32 d = ground();
            if (d < 4) {
                speed_y = s16(d << 8);
            } else {
                flags |= falling;
            }
            move();
        } else {
            move();
            speed_y = s16(speed_y + 0x10);
            const s32 d = ground();
            if (d < 0) {
                y += d * 0x100;
                flags &= ~falling;
                speed_y = 0;
            }
        }
    }
    switch (state) {
    case arriving:
        if (x < 0x1A000) {
            y = -0x8000;
            speed_y = 0;
            speed_x = 0x400;
        } else {
            accel_x = -0x10;
            next_state = braking;
        }
        break;
    case braking:
        if (x < 0x1A000) {
            accel_x = 0;
            speed_x = 0;
            x = 0x1A000;
            next_state = settling;
        } else {
            speed_x = std::max<s16>(speed_x, -0x200);
        }
        break;
    case settling:
        if (!(flags & falling)) {
            next_state = u8(next_state + 1);
        }
        break;
    case standing:
        if (timer++ >= 0x3C) {
            next_state = u8(next_state + 1);
        }
        break;
    case step_a:
    case step_b:
    case step_c:
    case step_d:
    case stomp:
    case aiming:
    case beam:
        if (parts_done != 0) {
            next_state = u8(next_state + 1);
        }
        break;
    case pounding:
        if (timer++ > 0x12B) {
            rounds = 0;
            next_state = u8(next_state + 1);
        }
        break;
    case sparks:
        if (parts_done != 0) {
            next_state = ++rounds < 4 ? beam : standing;
        }
        break;
    case broken:
        if (parts_done != 0) {
            next_state = leaving;
        }
        break;
    case leaving:
        if (!(flags & falling)) {
            accel_x = -0x10;
            if (speed_x < -0x600) {
                speed_x = -0x600;
                accel_x = 0;
            }
            if (x < -0x77FF) {
                speed_x = 0;
            }
        }
        break;
    default:
        speed_x = 0;
        accel_x = 0;
        break;
    }
    if (!(flags & beaten)) {
        // A claw every F0h frames once it stands.
        if (state > braking && --missile_timer < 1) {
            missile_timer = 0xF0;
            g.scheduler().create<Claw>(g, base_x * 0x100 + x - 0x1500, base_y * 0x100 + y - 0xA00);
        }
    } else {
        // Beaten: after two seconds everything fades to white; then the
        // ending.
        const s32 fade = std::min(std::max(timer - 0x78, 0) >> 2, 0x10);
        if (flash == 0) {
            auto& v = g.video().settings;
            v.sprite_window = false;
            v.outside = 0x3F;
            v.inside_sprite_window = 0x3F;
            v.blend.set_control(0xBF);
            v.blend.evy = u8(fade);
        }
        if (fade == 0x10) {
            g.clear_screen_processes();
            start_true_ending(g);
            return;
        }
        smoke(g);
        if (s16(timer + 1) >= 0) {
            ++timer;
        }
    }
    if (state != next_state) {
        for (BossPart* p : parts) {
            p->restart();
        }
        parts_done = 0;
        timer = 0;
        state = next_state;
    }
}

// 0804A6E8
void WarningPart::update(Game& g) {
    StageState& s = g.stage();
    if (timer_ > 0xBF) {
        g.scheduler().destroy(*this);
        return;
    }
    if (timer_ == 0x80 && index_ == 0) {
        static_cast<MoonBoss*>(s.moon_boss)->flags &= ~MoonBoss::waiting;
    }
    const Assets& a = g.assets();
    const u32 t = u32(timer_) & 0x1F;
    const u32 at = t * 0x800;
    s32 scale_x = 0x100, scale_y = 0x100;
    if (t != 0) {
        if (at < 0x4000) {
            scale_y = sine(a, t * 0x20) >> 6;
        } else if (at >= 0xE000) {
            const s32 k = sine(a, (((at - 0xE000) * 2) >> 6) & 0x3FF) >> 6;
            scale_y = 0x100 - k;
            scale_x = k + 0x100;
        }
    }
    view_.x = s16(x_ >> 8);
    view_.y = s16(y_ >> 8);
    view_.scale_x = s16(scale_x == 0 ? 2 : scale_x);
    view_.scale_y = s16(scale_y == 0 ? 2 : scale_y);
    draw_turned(g, sprite_flag::rotated | sprite_flag::double_size);
    ++timer_;
}

} // namespace

void start_super_sonic(Game& g, Player& p) {
    p.trail_newest = 0;
    p.trail_x.fill(p.x);
    p.trail_y.fill(p.y);
    StageState& s = g.stage();
    s.rings = 50;
    Sprite& body = p.body->sprite;
    body.flags = (body.flags & ~sprite_flag::priority) | 0x1000;
    s.super_sparkles = 4;
}

bool super_sonic_frame(Game& g, Player& p) {
    StageState& s = g.stage();
    const Assets& a = g.assets();
    auto& c = s.camera;
    c.min_y = s16(s.map.height - 200);
    c.max_x = 0x1E0;
    c.shift_x = 0x40;
    c.shift_y = 0;
    // 0804A220: the buttons.
    const u16 before = p.held;
    if (!(p.flags & player_flag::frozen)) {
        p.held = g.controller().held;
    }
    p.pressed = u16((before ^ p.held) & p.held);
    bool died = false;
    if (p.flags & player_flag::dead) {
        // As 080454D8's start of dying, without its sound.
        died = true;
        p.state = 1;
        p.speed_x = 0;
        p.hurt_timer = 2;
        p.power = 0;
        c.locks |= 3;
        if (g.game_mode < 2) {
            s.flags |= stage_flag::paused;
        }
        Sprite& body = p.body->sprite;
        body.flags = (body.flags & ~sprite_flag::priority) | 0x1000;
        p.scale_x = p.scale_y = 0x100;
        g.sound().stop(0x1C);
        g.sound().stop(0x1B);
        g.sound().stop(0x78);
    } else if (!(p.flags & player_flag::no_movement)) {
        SuperMovement(g, p).frame();
    }
    // 0804A1D8: the animation for the state.
    Sprite& body = p.body->sprite;
    const u32 entry = super_animations + u32(s32(s8(p.state)) * 4);
    if (body.animation != a.u16(entry) || body.variant != a.u8(entry + 2)) {
        body.animation = a.u16(entry);
        body.variant = a.u8(entry + 2);
        body.previous_variant = 0xFF;
    }
    p.trail_newest = (p.trail_newest + 1) & 0xF;
    p.trail_x[p.trail_newest] = p.x;
    p.trail_y[p.trail_newest] = p.y;
    draw_super_sonic(g, p);
    if (p.slip_timer != 0) {
        --p.slip_timer;
    } else if (p.hurt_timer > 0) {
        --p.hurt_timer;
    }
    // 08049998: a ring a second runs out while the act goes on and the
    // boss stands; at none left the player dies.
    if (!(s.flags & stage_flag::paused) && !boss_beaten(s)) {
        if (++p.power_timer > 0x3B) {
            p.power_timer = s16(p.power_timer - 0x3C);
            --s.rings;
            if (s.rings < 0xB) {
                g.sound().start(sound_rings_low);
            }
            if (s.rings == 0) {
                p.flags |= player_flag::dead;
            }
        }
    }
    return died;
}

bool super_sonic_hit(Game& g, Player& p) {
    if (p.invincible_timer >= 1 || p.hurt_timer >= 1) {
        return false;
    }
    p.hurt_timer = 0x1E;
    p.speed_x = -0x300;
    p.ground_speed = -0x300;
    p.speed_y = 0;
    p.slip_timer = 0x18;
    p.state = 4;
    p.special = 0;
    p.move = 0;
    g.sound().start(sound_hit);
    return true;
}

void start_warning(Game& g, u16 priority) {
    for (s16 i = 0; i < 4; ++i) {
        g.scheduler().create<WarningPart>(g, priority, i);
    }
}

bool create_moon_enemy(Game& g, u32 creator, const ObjectSpawn& o) {
    if (creator != 0x0805230C) {
        return false;
    }
    if (g.game_mode >= 2) {
        o.record[0] = 0xFE;
        return true;
    }
    MoonBoss& boss = g.scheduler().create<MoonBoss>(g, o);
    boss.build(g);
    g.stage().moon_boss = &boss;
    return true;
}

} // namespace sonic_advance_gba
