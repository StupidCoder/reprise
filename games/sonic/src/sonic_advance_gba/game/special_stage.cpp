// The special stages (08068654-08068CD4: the welcome scene; 080298D8-
// 0802D678: the stage; 0805C414-0805C8C8: its card; 0805D014-0805E1B0: the
// results; 08068CD8-08069654: the emerald's scene).

#include "sonic_advance_gba/game/special_stage.hpp"
#include "sonic_advance_gba/game/controller.hpp"
#include "sonic_advance_gba/game/fade.hpp"
#include "sonic_advance_gba/game/game.hpp"
#include "sonic_advance_gba/game/graphics.hpp"
#include "sonic_advance_gba/game/math.hpp"
#include "sonic_advance_gba/game/stage.hpp"
#include "sonic_advance_gba/game/video.hpp"
#include <algorithm>
#include <array>

namespace sonic_advance_gba {

namespace {

// ---------------------------------------------------------------------------
// The welcome scene (08068654).

constexpr u32 entry_falling =
    0x08684480;                          // per character: animation, variant (falling into the sky)
constexpr u32 entry_diving = 0x08684490; // per character: the dive

constexpr u16 music_welcome = 0x29;

// 08068C64 / 08068ADC: the hero falls past the view, then (once the words
// show) dives in from the right.
class EntryHero final : public Process {
public:
    explicit EntryHero(Game& g) : Process(0x2120) {
        const Assets& a = g.assets();
        const u8 ch = g.stage().character;
        sprite_.start(0x10020, a.u16(entry_falling + ch * 4u),
                      u8(a.u16(entry_falling + ch * 4u + 2)), 0x78, 0xB4, 0x180, 0x1000);
        sprite_.palette = 1;
        animate(g, sprite_);
    }

    u32 time{}; // +30
    bool diving{};

    void update(Game& g) override {
        if (!diving) {
            // 08068C64
            y_ -= 0x3D0;
            show(g);
            return;
        }
        // 08068ADC
        if (time == 0) {
            const Assets& a = g.assets();
            const u8 ch = g.stage().character;
            sprite_.animation = a.u16(entry_diving + ch * 4u);
            sprite_.variant = u8(a.u16(entry_diving + ch * 4u + 2));
            sprite_.previous_variant = 0xFF;
            fall_ -= 0x500;
            y_ = 0xB400;
            x_ = 0x9600;
        }
        if (time > 0x28) {
            x_ += drift_;
            fall_ += 0x40;
            y_ += fall_;
            show(g);
        }
    }

private:
    void show(Game& g) {
        sprite_.x = s16(x_ >> 8);
        sprite_.y = s16(y_ >> 8);
        animate(g, sprite_);
        draw(g, sprite_);
    }

    Sprite sprite_;      // +00
    s32 fall_ = -0x3D0;  // +34
    s32 y_ = 0xB400;     // +38
    s32 drift_ = -0x100; // +3C
    s32 x_ = 0x7800;     // +40
};

// 08068B7C: sixteen streaks rushing down past the hero, each placed anew
// every 16 frames.
class EntryStreaks final : public Process {
public:
    explicit EntryStreaks(Game& g) : Process(0x2120), game_(g) {
        StageState& s = g.stage();
        for (unsigned i = 0; i < 16; ++i) {
            y_[i] = remainder(s32(s.next_random()), 0x28) - 0x14;
            x_[i] = remainder(s32(s.next_random()), 0xDC) + 0x14;
        }
        sprite_.start(0x10820, 0x308, 0, 0x78, 0x50, 0, 0x1000);
        animate(g, sprite_);
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        ++time_;
        sprite_.previous_variant = 0xFF;
        animate(g, sprite_);
        for (u32 i = 0; i < 16; ++i) {
            if (((time_ + i) & 0xF) == 0) {
                y_[i] = remainder(s32(s.next_random()), 0x28) - 0x14;
                x_[i] = remainder(s32(s.next_random()), 0xDC) + 0x14;
            } else {
                y_[i] += 0x10;
            }
            sprite_.x = s16(x_[i]);
            sprite_.y = s16(y_[i]);
            draw(g, sprite_);
        }
    }

    void on_destroy() override { game_.stage().special.counter_leftover = u8(y_[15]); }

private:
    Game& game_;
    Sprite sprite_;           // +00
    u32 time_{};              // +30
    std::array<s32, 16> x_{}; // +44
    std::array<s32, 16> y_{}; // +84
};

// 08068CA0: "welcome to the special stage", blinking every 30 frames.
class EntryWords final : public Process {
public:
    explicit EntryWords(Game& g) : Process(0x2120) {
        sprite_.start(0x10C80, 0x32B, 0, 0x78, 0x78, 0, 0x1000);
        animate(g, sprite_);
    }

    void update(Game& g) override {
        ++time_;
        if ((divide(s32(time_), 0x1E) & 1) == 0) {
            animate(g, sprite_);
            draw(g, sprite_);
        }
    }

private:
    Sprite sprite_; // +00
    u32 time_{};    // +30
};

// 08068914 / 08068A44: the scene's clock.
class EntryDirector final : public Process {
public:
    EntryDirector(EntryHero& hero, EntryStreaks& streaks)
        : Process(0x2100), hero_(hero), streaks_(streaks) {
        fade_ = {0, 1, 2, 0, 0x150, 1};
    }

    ScreenFade fade_; // +00

    void update(Game& g) override {
        const s16 t = s16(time_ + 1);
        const u32 last = u32(divide(0x2000, 0x150)); // the fade's frames
        auto& vs = g.video();
        if (!words_) {
            step_fade(vs, fade_);
            if (t == 1) {
                // 080688B0: the sky.
                g.screen_graphics->reset_pool();
                GraphicsLoad l;
                l.options = 0x80;
                l.slot = 0;
                l.parts = load::tiles | load::colors | load::map;
                l.layer = 0;
                l.palette = 0;
                l.tiles_from = 0x086B4D10;
                l.tile_bytes = 0x2980;
                l.colors_from = 0x086B4B10;
                l.color_bytes = 0x200;
                l.map_from = 0x086B7690;
                l.map_bytes = 0x1000;
                load_graphics(g, l);
            }
            if (u32(t) < 0x5B) {
                if (0x5A - last == u32(t)) {
                    fade_ = {0, 1, 1, 0, 0x150, 1};
                }
                step_fade(vs, fade_);
                time_ = t;
                hero_.time = u32(t);
                return;
            }
            time_ = 0;
            hero_.time = 0;
            vs.settings.bg[0].scroll_y = 0;
            fade_ = {0, 1, 2, 0, 0x150, 1};
            g.scheduler().create<EntryWords>(g);
            g.scheduler().destroy(streaks_);
            hero_.diving = true;
            words_ = true;
            return;
        }
        if (u32(t) < 0x97) {
            if (0x96 - last == u32(t)) {
                fade_ = {0, 1, 1, 0, 0x150, 1};
            }
            step_fade(vs, fade_);
            time_ = t;
            hero_.time = u32(t);
            return;
        }
        g.clear_screen_processes();
        start_special_stage(g);
    }

private:
    EntryHero& hero_;       // +10
    EntryStreaks& streaks_; // +0C
    s16 time_{};            // +18
    bool words_{};
};

// ---------------------------------------------------------------------------
// The stage (080298D8).

constexpr u32 stage_of_act = 0x08480034; // per act: its special stage (0-6)
constexpr u32 ring_targets =
    0x08480040; // per stage: the rings for the first gate and the end (u16 each)
constexpr u32 stage_objects =
    0x087B97F8; // per stage: its list (12 bytes: s16 x, y; s32 depth; s16 kind; s16)
constexpr u32 track_layers[5] = {0x0847FF34, 0x0847FF74, 0x0847FFB4, 0x0847FFF4, 0x0847FEF4};
constexpr u32 lean_animations = 0x0848005C;  // by the view's ninth (1-9): animation offset, variant
constexpr u32 steer_animations = 0x08480084; // by the direction held: animation offset, variant
constexpr u32 steer_push = 0x084800C4;       // by the direction held: push x, y (s16)
constexpr u32 spring_animations = 0x08480114;    // spring kinds 4-9: animation offset, variant
constexpr u32 spring_throws = 0x0848012C;        // spring kinds 4-9: speed x, y, depth (s16)
constexpr u32 object_animations = 0x0848018C;    // by kind: animation, variant (u16 each)
constexpr u32 object_tiles = 0x084801C4;         // by kind: tile offset (in 32 bytes)
constexpr u32 capsule_ring_places = 0x084801E0;  // 12 x (s16 x, y), times 40h
constexpr u32 burst_places = 0x08480210;         // 8 x (s16 x, y), times 100h
constexpr u32 lost_ring_throws = 0x08480230;     // 10 x (s16 x, y)
constexpr u32 character_animations = 0x084A6F04; // per character: its first animation
constexpr u32 ring_sound = 0x087B66AC;           // the ring sound's song header

constexpr u16 music_special = 0x2A;
constexpr u16 sound_spring = 0x94, sound_jump = 0x95, sound_capsule = 0x96, sound_item = 0x97,
              sound_bomb = 0x90;
constexpr u16 sound_rings_lost = 0x76, sound_cleared = 0x2C;

// The player in the stage (03005690): a point in the tube (x and y across,
// depth along; 8.8), drawn scaled at its projection.
struct SpecialPlayer {
    s32 x{}, y{}, z{};                   // +00, +04, +08
    s16 screen_x{}, screen_y{};          // +0C, +0E
    s16 bob{};                           // +10 the scale's wobble (and on failing, the shrinking)
    u16 bob_phase{};                     // +12
    s16 speed_x{}, speed_y{}, speed_z{}; // +14, +16, +18
    s16 accel_x{}, accel_y{}, accel_z{}; // +1C, +1E, +20
    u8 state{};            // +28 0 running, 1 thrown by a spring, 2 hit, 3 jumping, 4 dashing
    u8 flags = 1;          // +29 player_flag below
    u8 exit{};             // +2A 40h past the first gate, 80h past the end
    s16 end_timer{};       // +32
    s16 hit_timer{};       // +34
    s16 jump_timer{};      // +3A
    u8 jump_kind{};        // +3C
    u8 dash_wait{};        // +3D frames a dash may follow a capsule
    u8 jump_wait{};        // +3E frames before the next jump
    u16 held{}, pressed{}; // +42, +44
    s16 animation{};       // +48
    u16 variant = 4;       // +4A
    u8 character{};        // +4C
    u16 touched_spring{};  // +50 the list index of the spring touched
    u16 last_spring{};     // +52
    u16 spring_kind{};     // +54
};

namespace special_flag {
inline constexpr u8 still = 0x01;  // no control (the start, the gates)
inline constexpr u8 fast = 0x02;   // the speed held above 800h
inline constexpr u8 sprung = 0x04; // a spring was touched
inline constexpr u8 bombed = 0x08; // a bomb was touched
inline constexpr u8 jumping = 0x10;
inline constexpr u8 dashing = 0x20;
} // namespace special_flag

// The stage's shared state (the processes all work on it).
struct SpecialRun {
    explicit SpecialRun(u8 stage) : stage(stage) {}

    u8 stage;
    SpecialPlayer player;
    TransformedSprite body; // 030055F0 / 030055FC
    // The tube: its view's centre and the player's offset (03005780-86),
    // the picture's step (03005792).
    s16 centre_x = 0x88, centre_y = 0x20;
    s16 offset_x{}, offset_y{};
    u8 phase{};
    std::array<Background, 5> layers; // 03005630, 030055A0, 03005740, 03005800, 030057A0
    // The depths drawn this frame (03005670; 7FFFh free): each gets a
    // rotation set (its index + 1) for its scale.
    std::array<s16, 16> depths{};
    std::array<u8, 32> scale_set{}; // 030057E0 the set's scale computed this frame
    // 030056F0: the pictures (animation, variant) loaded this frame by an
    // object; another one with the same picture keeps the tiles.
    std::array<std::pair<u16, u16>, 16> pictures{};
    u8 outcome{};                   // 030055E0 0 running, 1 past the end, 2 past the first gate
    s16 outcome_timer{};            // 030055E2
    u16 fade{};                     // 030055E4
    std::array<u16, 16> segments{}; // 03005840 the list index where each 1000h of depth starts
};

// 0802D55C: the rotation set (1-16; 17 when all are taken) for depth d.
u8 depth_set(SpecialRun& r, s16 d) {
    for (u32 i = 0; i < 16; ++i) {
        if (r.depths[i] == d) {
            return u8(i + 1);
        }
        if (r.depths[i] == 0x7FFF) {
            r.depths[i] = d;
            return u8(i + 1);
        }
    }
    return 17;
}

// The scale of a depth: the original's fourth power curve (0802BC3C).
struct Projection {
    s32 curve;  // quotient((k)^4, 198h)
    s32 factor; // (curve >> 9) + 38h, the position's
    s16 scale;  // (curve >> 10) + 40h, the picture's
};

Projection project_depth(s16 d) {
    s32 k = (d + 0x300) >> 3;
    if (k == 0) {
        k = 1;
    }
    const s32 curve = quotient(k * k * k * k, 0x198);
    return {curve, ((curve < 0 ? curve + 0x1FF : curve) >> 9) + 0x38,
            s16(((curve < 0 ? curve + 0x3FF : curve) >> 10) + 0x40)};
}

// 0802BDDC: the scale's rotation set (computed once a frame) and the
// sprite's place so that its frame is scaled about the given point.
void place_scaled(Game& g, SpecialRun& r, Sprite& sp, s16 scale_x, s16 scale_y, s16 x, s16 y) {
    if (sp.frame == 0xFFFFFFFF) {
        return;
    }
    const Assets& a = g.assets();
    const unsigned set = sp.flags & 0x1F;
    if (r.scale_set[set] == 0) {
        r.scale_set[set] = 0xFF;
        gba::Rotation& m = g.video().rotations.at(set);
        m.pa = s16(divide(0x10000, scale_x));
        m.pb = 0;
        m.pc = 0;
        m.pd = s16(divide(0x10000, scale_y));
    }
    const s32 w = a.u16(sp.frame + 4) >> 1, h = a.u16(sp.frame + 6) >> 1;
    sp.x = s16(x - ((scale_x * (a.s16(sp.frame + 8) - w) + w * 0x100) >> 8));
    sp.y = s16(y - ((scale_y * (a.s16(sp.frame + 10) - h) + h * 0x100) >> 8));
}

// 0802B5AC: an object's picture: one already loaded this frame is reused.
void register_picture(SpecialRun& r, Sprite& sp) {
    if (sp.animation == 0x31D && sp.variant == 0) {
        return;
    }
    for (auto& p : r.pictures) {
        if (p.first == sp.animation && p.second == sp.variant) {
            sp.flags |= sprite_flag::keep_colors | sprite_flag::keep_tiles;
            return;
        }
    }
    for (auto& p : r.pictures) {
        if (p.first == 0 && p.second == 0) {
            p = {sp.animation, sp.variant};
            return;
        }
    }
}

// Two boxes meet (the original's hitbox test).
bool spans_meet(s32 a, s32 a_width, s32 b, s32 b_width) {
    return !(b + b_width < a) && !(a + a_width < b);
}

// 0802D5BC: the ring sound, alternating sides.
void ring_chime(Game& g) {
    const u16 rings = g.stage().special.rings;
    g.sound().play_on((rings & 1) == 0 ? 1 : 2, ring_sound, (rings & 1) == 0 ? 0x40 : -0x40);
}

constexpr u32 song_table = 0x086D76AC; // 8 bytes a song: header, player

// 08025950: a jingle (song `jingle`, on music player 3) unless that player
// holds it already; the effects stay quiet until it ends (0802587C), then
// song `after` goes on, fading in from its player's quiet.
class Jingle final : public Process {
public:
    explicit Jingle(u16 after) : Process(0x3000), after_(after) {}

    void update(Game& g) override {
        if (!started_) {
            started_ = true; // 08025868
            return;
        }
        const u32 status = g.sound().engine().player(3).status;
        if (!(s32(status) < 0 || (status & 0xFFFF) == 0)) {
            return;
        }
        g.sound().start_or_continue(after_);
        g.sound().fade_in(u8(g.assets().u16(song_table + after_ * 8u + 4)), 4);
        g.scheduler().destroy(*this);
        g.stage().special.quiet = 0;
    }

private:
    u16 after_;
    bool started_{};
};

void play_jingle(Game& g, u16 jingle, u16 after) {
    const u32 header = g.assets().u32(song_table + jingle * 8u);
    if (g.sound().song(3) != header) {
        g.scheduler().create<Jingle>(after);
        g.stage().special.quiet = 0xFF;
        g.sound().start_header(3, header);
    }
}

// 0802BEAD and the object routines: a thing of the stage's list (rings
// after a capsule, springs, bombs, capsules, items), the sparkle of a
// collected ring, the lost rings, the messages; 0x74-byte records.
class SpecialObject : public Process {
public:
    SpecialObject(SpecialRun& r, u16 priority, u16 kind) : Process(priority), run(r), kind(kind) {}

    SpecialRun& run;
    // +00 the scale and the point the picture is scaled about.
    s16 scale_x{}, scale_y{};            // +02, +04
    s16 centre_x{}, centre_y{};          // +06, +08
    Sprite sprite;                       // +0C
    s32 x{}, y{}, z{};                   // +3C, +40, +44
    s16 proj_x{}, proj_y{};              // +48, +4A
    s16 screen_x{}, screen_y{};          // +4C, +4E
    s16 depth{};                         // +50 the player's depth minus this one's
    s16 speed_x{}, speed_y{}, speed_z{}; // +52, +54, +56
    s16 accel_x{}, accel_y{}, accel_z{}; // +58, +5A, +5C
    u16 kind;                            // +60
    u16 animation{}, variant{};          // +62, +64
    u8 set{};                            // +66
    u8 used{};                           // +67 bit 0: touched (or not touchable)
    u16 index{};                         // +68 its place in the list
    u8 lost_index{}, lost_count{};       // +6A, +6B
    u8 blink{};                          // +6C
    u8 timer{};                          // +6D
    s16 wait{};                          // +6E
    u16 delay{};                         // +70

protected:
    // 0802BBC0
    void move() {
        speed_x = s16(speed_x + accel_x);
        speed_y = s16(speed_y + accel_y);
        speed_z = s16(speed_z + accel_z);
        x += speed_x;
        y += speed_y;
        z += speed_z;
    }

    // 0802BC3C: the projection by depth; false (and the object ended)
    // once behind the player, or out of the drawn depths.
    bool project(Game& g) {
        SpecialRun& r = run;
        const s32 d = r.player.z - z;
        depth = s16(u32(d) >> 8);
        if (!(kind == 3 || s32(u32(d >> 8) * 0x10000u) < 0x2400001)) {
            g.scheduler().destroy(*this);
            return false;
        }
        if (depth >= 0xC1 || depth <= -0x301) {
            return false;
        }
        set = depth_set(r, depth);
        const Projection p = project_depth(depth);
        proj_x = s16(quotient(p.factor * x, 0x280) >> 8);
        proj_y = s16(quotient(p.factor * y, 0x280) >> 8);
        screen_x = s16(proj_x + 0x78 + r.offset_x);
        screen_y = s16(0x50 - proj_y - r.offset_y);
        sprite.x = screen_x;
        sprite.y = screen_y;
        centre_x = screen_x;
        centre_y = sprite.y;
        scale_x = scale_y = p.scale;
        sprite.flags = set | 0x2020u;
        if (scale_x > 0x100) {
            sprite.flags = set | 0x2060u;
        }
        s32 l = depth - 0xC0;
        if (l < 0) {
            l = depth - 0xA1;
        }
        sprite.layer = u16(s16(l >> 5) * -0x40);
        return true;
    }

    void show(Game& g) {
        register_picture(run, sprite);
        animate(g, sprite);
        place_scaled(g, run, sprite, scale_x, scale_y, centre_x, centre_y);
        draw(g, sprite);
    }

    // Its picture by kind, from the stage's tables.
    void take_kind_picture(Game& g) {
        const Assets& a = g.assets();
        animation = a.u16(object_animations + kind * 4u);
        variant = a.u16(object_animations + kind * 4u + 2);
        sprite.tiles.to = 0x10000 + a.u16(object_tiles + kind * 2u) * 0x20u;
        sprite.tiles.bytes = 0;
        sprite.animation = animation;
        sprite.variant = u8(variant);
        sprite.previous_variant = 0xFF;
        sprite.timer = 0;
        sprite.speed = 0x10;
    }

    void start_sprite(Game&, u16 layer) {
        set = depth_set(run, s16(((run.player.z >> 8) - (z >> 8)) * 0x10000 >> 16));
        sprite.x = s16((x >> 8) + 0x78);
        sprite.y = s16(0x50 - (y >> 8));
        sprite.layer = layer;
        sprite.palette = 0;
        sprite.flags = set | 0x2020u;
        scale_x = scale_y = 0x40;
        centre_x = sprite.x;
        centre_y = sprite.y;
    }
};

// 0802D3B4 (then 0802D420): a picture playing out once where it is (a
// collected ring's sparkle, a burst star), then gone.
class Sparkle : public SpecialObject {
public:
    using SpecialObject::SpecialObject;

    void update(Game& g) override {
        if (ending_) {
            g.scheduler().destroy(*this); // 0802D420
            return;
        }
        move();
        if (!project(g)) {
            return;
        }
        if (sprite.flags & sprite_flag::ended) {
            ending_ = true;
            return;
        }
        register_picture(run, sprite);
        if (animate(g, sprite) == 0) {
            ending_ = true;
        }
        place_scaled(g, run, sprite, scale_x, scale_y, centre_x, centre_y);
        draw(g, sprite);
    }

protected:
    bool ending_{};
};

// 0802C53C / 0802D494: a word at the top or the bottom ("not enough
// rings", the gates' words), shown for its time.
class StageMessage final : public SpecialObject {
public:
    StageMessage(Game& g, SpecialRun& r, u8 which) : SpecialObject(r, 0x1500, 0) {
        u8 frames = 0;
        s16 y = 0;
        switch (which) {
        case 0:
            y = 0x28;
            animation = 0x32D;
            variant = 0;
            kind = 5;
            frames = 0xB4;
            break;
        case 1:
            y = 0x28;
            animation = 0x32D;
            variant = 1;
            kind = 5;
            frames = 0xB4;
            break;
        case 2:
            y = 0x88;
            animation = 0x32C;
            variant = 0;
            kind = 2;
            frames = 0x78;
            break;
        default:
            break;
        }
        timer = frames;
        sprite.start(0x10000 + g.assets().u16(object_tiles + kind * 2u) * 0x20u, animation,
                     u8(variant), 0x78, y, 0x40, 0x2000);
    }

    void update(Game& g) override {
        if (ending_) {
            g.scheduler().destroy(*this); // 0802D63C
            return;
        }
        animate(g, sprite);
        draw(g, sprite);
        if (--timer == 0) {
            ending_ = true;
        }
    }

private:
    bool ending_{};
};

void show_message(Game& g, SpecialRun& r, u8 which) {
    g.scheduler().create<StageMessage>(g, r, which);
}

class ListObject;
void touch(Game& g, ListObject& o);

// The list's things (0802BEAD, then 0802D34C, with the touch 0802B854).
class ListObject : public SpecialObject {
public:
    enum class Mode : u8 { starting, waiting, sparkle, capsule, message, gather, home, ending };

    ListObject(SpecialRun& r, u16 priority, u16 kind) : SpecialObject(r, priority, kind) {}

    Mode mode = Mode::starting;

    void update(Game& g) override {
        switch (mode) {
        case Mode::starting:
            start(g);
            return;
        case Mode::waiting:
            wait_frame(g);
            return;
        case Mode::sparkle:
            sparkle(g);
            return;
        case Mode::capsule:
            capsule(g);
            return;
        case Mode::message:
            message(g);
            return;
        case Mode::gather:
            gather(g);
            return;
        case Mode::home:
            home(g);
            return;
        case Mode::ending:
            g.scheduler().destroy(*this);
            return;
        }
    }

    // A ring or the like touched: a sparkle (kind 1, or 0Ch for a bomb)
    // from now on. A capsule's ring keeps its depth's speed.
    void become_sparkle(Game& g, u16 new_kind, bool keep_depth = false) {
        kind = new_kind;
        animation = new_kind == 0xC ? 0x322 : 0x31C;
        variant = g.assets().u16(object_animations + kind * 4u + 2);
        used |= 1;
        speed_x = speed_y = accel_x = accel_y = 0;
        if (!keep_depth) {
            speed_z = accel_z = 0;
        }
        sprite.tiles.to = 0x10000 + g.assets().u16(object_tiles + kind * 2u) * 0x20u;
        sprite.tiles.bytes = 0;
        sprite.animation = animation;
        sprite.variant = u8(variant);
        sprite.previous_variant = 0xFF;
        sprite.timer = 0;
        sprite.speed = 0x10;
    }

private:
    // 0802BEAD
    void start(Game& g) {
        speed_x = speed_y = speed_z = accel_x = accel_y = accel_z = 0;
        depth = 0;
        take_kind_picture(g);
        used = 0;
        start_sprite(g, 0x7C0);
        mode = Mode::waiting;
        register_picture(run, sprite);
        animate(g, sprite);
        place_scaled(g, run, sprite, scale_x, scale_y, centre_x, centre_y);
    }

    // 0802D34C
    void wait_frame(Game& g) {
        if (kind == 3) {
            // 0802D434: the capsule's rings: plain rings once the task
            // before them has counted down; drawn to a dashing player.
            const Process* before = g.scheduler().before(*this);
            const auto* o = dynamic_cast<const SpecialObject*>(before);
            if (!o || o->timer == 0) {
                kind = 0;
            } else if (run.player.state == 4) {
                mode = Mode::home;
                speed_x = -0x300;
            }
        }
        move();
        if (!project(g)) {
            return;
        }
        if (delay == 0) {
            touch(g, *this); // (the frame's drawing goes on)
        } else {
            --delay;
        }
        show(g);
    }

    // 0802D3B4
    void sparkle(Game& g) {
        move();
        if (!project(g)) {
            return;
        }
        if (sprite.flags & sprite_flag::ended) {
            mode = Mode::ending;
            return;
        }
        register_picture(run, sprite);
        if (animate(g, sprite) == 0) {
            mode = Mode::ending;
        }
        place_scaled(g, run, sprite, scale_x, scale_y, centre_x, centre_y);
        draw(g, sprite);
    }

    // 0802C458: the opened capsule: after Bh frames its word (a dash
    // after it says more).
    void capsule(Game& g) {
        if (--timer != 0) {
            return;
        }
        timer = 0x3C;
        animation = 0x32C;
        variant = run.player.state == 4 ? 1 : 2;
        sprite.tiles.to = 0x10000 + g.assets().u16(object_tiles + kind * 2u) * 0x20u;
        sprite.tiles.bytes = 0;
        sprite.animation = animation;
        sprite.variant = u8(variant);
        sprite.previous_variant = 0xFF;
        sprite.timer = 0;
        sprite.speed = 0x10;
        sprite.x = 0x78;
        sprite.y = 0x28;
        sprite.layer = 0x40;
        sprite.flags = 0x2000;
        mode = Mode::message;
    }

    // 0802D494
    void message(Game& g) {
        animate(g, sprite);
        draw(g, sprite);
        if (--timer == 0) {
            mode = Mode::ending;
        }
    }

    // 0802C1F4: a capsule's ring taken: it flies to the player's depth.
    void gather(Game& g) {
        if (sprite.flags & sprite_flag::ended) {
            mode = Mode::ending;
            return;
        }
        follow_depth();
        if (project(g)) {
            register_picture(run, sprite);
            animate(g, sprite);
            place_scaled(g, run, sprite, scale_x, scale_y, centre_x, centre_y);
            draw(g, sprite);
        }
    }

    // 0802C09C: a capsule's ring drawn to the dashing player.
    void home(Game& g) {
        const Assets& a = g.assets();
        const s16 dir = octant_angle(a, s16(run.player.screen_y - screen_y),
                                     s16(run.player.screen_x - screen_x));
        speed_x = s16(speed_x + 0x15);
        x += (sine(a, u32(dir + 0x100)) * speed_x) >> 14;
        y -= (sine(a, u32(dir)) * speed_x) >> 14;
        follow_depth();
        if (!project(g)) {
            return;
        }
        if (delay == 0) {
            touch(g, *this);
        } else {
            --delay;
        }
        show(g);
    }

    void follow_depth() {
        const s32 d = run.player.z - z;
        accel_z = s16(u32(d) >> 8);
        s32 v = s16(speed_z) + (d >> 8);
        v = s16(v);
        speed_z = s16(std::clamp(v, -0xE00, 0xE00));
        z += speed_z;
        if (run.player.z + 0x100 < z) {
            z = run.player.z;
        }
    }
};

void start_burst(Game& g, ListObject& from);
void start_capsule_rings(Game& g, ListObject& from);

// 0802B854: the player meets a thing.
void touch(Game& g, ListObject& o) {
    SpecialRun& r = o.run;
    SpecialPlayer& p = r.player;
    StageState& s = g.stage();
    if ((o.used & 1) || (p.flags & special_flag::still) || p.state == 2) {
        return;
    }
    // 0802B63C
    const Sprite& sp = o.sprite;
    if (sp.hitboxes[0].index == 0xFFFFFFFF || r.player.z < o.z - 0x4000 || o.z < r.player.z) {
        return;
    }
    const Hitbox& hb = sp.hitboxes[0];
    const Hitbox& ph = r.body.sprite.hitboxes[0];
    const s32 px = p.x >> 8, py = p.y >> 8;
    const s32 ox = o.proj_x + hb.left, oy = o.proj_y + hb.top;
    bool met;
    if (o.kind < 4 || (o.kind > 9 && o.kind != 0xB)) {
        met = spans_meet(px + ph.left, ph.right - ph.left, ox, hb.right - hb.left) &&
              spans_meet(py + ph.top, ph.bottom - ph.top, oy, hb.bottom - hb.top);
    } else {
        // Springs and bombs: the player as a point, 10 pixels around (3
        // when fast).
        const s32 m = (p.flags & special_flag::fast) ? 3 : 10;
        met = spans_meet(px - m, 2 * m, ox, hb.right - hb.left) &&
              spans_meet(py - m, 2 * m, oy, hb.bottom - hb.top);
    }
    if (!met) {
        return;
    }
    switch (o.kind) {
    case 0: // a ring
        o.become_sparkle(g, 1);
        ++s.special.rings;
        ring_chime(g);
        o.mode = ListObject::Mode::sparkle;
        break;
    case 2: // a capsule: rings around it, a burst
        o.used |= 1;
        if (s.special.quiet == 0) {
            g.sound().start(sound_capsule);
        }
        start_burst(g, o);
        start_capsule_rings(g, o);
        o.mode = ListObject::Mode::capsule;
        p.dash_wait = 10;
        o.timer = 0xB;
        break;
    case 3: // a capsule's ring
        o.become_sparkle(g, 1, true);
        ++s.special.rings;
        ring_chime(g);
        o.mode = ListObject::Mode::gather;
        break;
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 9: // springs
        p.flags |= special_flag::sprung;
        p.touched_spring = o.index;
        p.spring_kind = o.kind;
        break;
    case 10: // an item
        g.scheduler().destroy(o);
        if (s.special.quiet == 0) {
            g.sound().start(sound_item);
        }
        ++s.special.items;
        break;
    case 0xB: // a bomb
        o.become_sparkle(g, 0xC);
        p.flags |= special_flag::bombed;
        if (s.special.quiet == 0) {
            g.sound().start(sound_bomb);
        }
        o.mode = ListObject::Mode::sparkle;
        break;
    default:
        break;
    }
}

// 0802C2AC: eight stars bursting from an opened capsule (sparkles at 1510h).
void start_burst(Game& g, ListObject& from) {
    const Assets& a = g.assets();
    for (u32 i = 0; i < 8; ++i) {
        auto& o = g.scheduler().create<Sparkle>(from.run, 0x1510, 0xD);
        const s16 bx = a.s16(burst_places + i * 4), by = a.s16(burst_places + i * 4 + 2);
        o.x = from.x + bx * 0x100;
        o.y = from.y + by * 0x100;
        o.z = from.z;
        o.speed_x = s16(bx << 3);
        o.speed_y = s16(by << 3);
        o.accel_x = s16(-quotient(o.speed_x, 32));
        o.accel_y = s16(-quotient(o.speed_y, 32));
        o.animation = a.u16(object_animations + 0xD * 4);
        o.variant = a.u16(object_animations + 0xD * 4 + 2);
        o.set = from.set;
        o.used = 1;
        o.sprite.tiles.to = 0x10000 + a.u16(object_tiles + 0xD * 2) * 0x20u;
        o.sprite.animation = o.animation;
        o.sprite.variant = u8(o.variant);
        o.sprite.previous_variant = 0xFF;
        o.sprite.palette = 0;
        o.sprite.flags = o.set | 0x2020u;
        o.sprite.layer = 0x7C0;
    }
}

// 0802C01C: twelve rings around an opened capsule (list things of kind 3
// at 1510h, touchable after 15 frames).
void start_capsule_rings(Game& g, ListObject& from) {
    const Assets& a = g.assets();
    for (u32 i = 0; i < 12; ++i) {
        auto& o = g.scheduler().create<ListObject>(from.run, 0x1510, 3);
        o.x = from.x + a.s16(capsule_ring_places + i * 4) * 0x40;
        o.y = from.y + a.s16(capsule_ring_places + i * 4 + 2) * 0x40;
        o.z = from.z;
        o.index = from.index;
        o.delay = 0xF;
    }
}

// 0802C694 / 0802C86C: a ring lost on a bomb, flying out and on ahead,
// blinking.
class LostRing final : public SpecialObject {
public:
    LostRing(SpecialRun& r, u8 index, u8 count) : SpecialObject(r, 0x1500, 0) {
        lost_index = index;
        lost_count = count;
        x = r.player.x;
        y = r.player.y;
        z = r.player.z;
        this->index = 0xFFFF;
    }

    void update(Game& g) override {
        if (!started_) {
            start(g);
            return;
        }
        accel_x = s16(-quotient(speed_x, 32));
        accel_y = s16(-quotient(speed_y, 32));
        move();
        if (project(g)) {
            register_picture(run, sprite);
            animate(g, sprite);
            place_scaled(g, run, sprite, scale_x, scale_y, centre_x, centre_y);
            if (blink++ & 1) {
                draw(g, sprite);
            }
        }
    }

private:
    void start(Game& g) {
        const Assets& a = g.assets();
        started_ = true;
        depth = 0;
        speed_x = a.s16(lost_ring_throws + lost_index * 4u);
        speed_y = a.s16(lost_ring_throws + lost_index * 4u + 2);
        speed_z = 0x800;
        accel_x = s16(-quotient(speed_x, 32));
        accel_y = s16(-quotient(speed_y, 32));
        accel_z = s16(-quotient(speed_z, 64));
        animation = 0x31B;
        variant = 0;
        used = 0;
        set = depth_set(run, s16(((run.player.z >> 8) - (z >> 8)) * 0x10000 >> 16));
        sprite.tiles.to = 0x10000 + a.u16(object_tiles + kind * 2u) * 0x20u;
        sprite.tiles.bytes = 0;
        sprite.animation = animation;
        sprite.variant = 0;
        sprite.previous_variant = 0xFF;
        sprite.timer = 0;
        sprite.speed = 0x10;
        sprite.x = s16((x >> 8) + 0x78);
        sprite.y = s16(0x50 - (y >> 8));
        sprite.layer = 0x180;
        sprite.palette = 0;
        sprite.flags = set | 0x2020u;
        scale_x = scale_y = 0x100;
        centre_x = sprite.x;
        centre_y = sprite.y;
        register_picture(run, sprite);
        animate(g, sprite);
        place_scaled(g, run, sprite, scale_x, scale_y, centre_x, centre_y);
    }

    bool started_{};
};

// 0802B1E4 / 0802D4D8: the hero tumbling away after too few rings.
class TumblingHero final : public SpecialObject {
public:
    TumblingHero(Game& g, SpecialRun& r) : SpecialObject(r, 0x10FF, 0xE) {
        const Assets& a = g.assets();
        StageState& s = g.stage();
        const SpecialPlayer& p = r.player;
        x = p.x;
        y = p.y;
        z = p.z - 0x100;
        depth = p.bob;
        const u32 rnd = s.next_random();
        accel_x = s16(rnd & 0xF);
        accel_y = s16(-s32((rnd & 0x70) >> 4));
        accel_z = -0x10;
        if (rnd & 0x80) {
            accel_x = s16(-accel_x);
        }
        wait = 0;
        animation = u16(a.u8(character_animations + p.character) + 0x22);
        variant = 1;
        used = 0;
        sprite.tiles.to = 0x11400;
        sprite.tiles.bytes = 0;
        sprite.animation = animation;
        sprite.variant = 1;
        sprite.previous_variant = 0xFF;
        sprite.timer = 0;
        sprite.speed = 0x10;
        set = depth_set(r, s16(((p.z >> 8) - (z >> 8)) * 0x10000 >> 16));
        sprite.x = s16((x >> 8) + 0x78);
        sprite.y = s16(0x50 - (y >> 8));
        sprite.layer = 0x100;
        sprite.palette = 0;
        sprite.flags = set | 0x2020u;
        scale_x = scale_y = 0x40;
        centre_x = sprite.x;
        centre_y = sprite.y;
    }

    void update(Game& g) override {
        if (wait <= -0xA0) {
            return;
        }
        --wait;
        move();
        if (project(g)) {
            sprite.layer = 0x140;
            animate(g, sprite);
            place_scaled(g, run, sprite, scale_x, scale_y, centre_x, centre_y);
            draw(g, sprite);
        }
    }
};

// 0802D30C: a gate in the list: reached, it ends the run's part (1 the
// end, 2 the first gate).
class GateMarker final : public Process {
public:
    GateMarker(SpecialRun& r, u8 kind, s32 z) : Process(0x1F00), run_(r), kind_(kind), z_(z) {}

    void update(Game& g) override {
        if (z_ <= run_.player.z) {
            run_.outcome = kind_;
            g.scheduler().destroy(*this);
        }
    }

private:
    SpecialRun& run_;
    u8 kind_; // +60
    s32 z_;   // +44
};

// 0802B3B4: the list's things come into being 300h ahead of the player
// (rings excepted: the ring manager draws those).
class Spawner final : public Process {
public:
    explicit Spawner(SpecialRun& r) : Process(0x1400), run_(r) {}

    void update(Game& g) override {
        const Assets& a = g.assets();
        const u32 list = a.u32(stage_objects + run_.stage * 4u);
        const auto entry_z = [&](u32 i) {
            return a.s32(list + i * 12 + 4);
        };
        if (done_ || run_.outcome == 1 || entry_z(next_) >= (run_.player.z >> 8) + 0x300) {
            return;
        }
        do {
            const u32 e = list + next_ * 12u;
            const s16 kind = a.s16(e + 8);
            if (kind == -1) {
                g.scheduler().create<GateMarker>(run_, 1, a.s32(e + 4) << 8);
                last_z_ = s16(a.s32(e + 4));
                ++next_;
                done_ = true;
                return;
            }
            if (kind == 0x7FFF) {
                g.scheduler().create<GateMarker>(run_, 2, a.s32(e + 4) << 8);
                last_z_ = s16(a.s32(e + 4));
            } else if (kind != 0) {
                auto& o = g.scheduler().create<ListObject>(run_, 0x1500, u16(kind));
                o.x = a.s16(e) << 8;
                o.y = a.s16(e + 2) << 8;
                o.z = a.s32(e + 4) << 8;
                o.index = next_;
                o.timer = 0xB;
                o.delay = 0;
                last_z_ = s16(a.s32(e + 4));
            }
            ++next_;
        } while (entry_z(next_) < (run_.player.z >> 8) + 0x300);
    }

private:
    SpecialRun& run_;
    u16 next_{};   // +02
    s16 last_z_{}; // +04
    bool done_{};  // +06
};

// 0802C904 / 0802CA60: the list's rings, drawn as copies of one sprite
// (each depth with its own scale); taken ones are marked in a bit array.
class RingManager final : public Process {
public:
    RingManager(Game& g, SpecialRun& r) : Process(0x1300), run_(r) {
        const Assets& a = g.assets();
        const u32 list = a.u32(stage_objects + r.stage * 4u);
        u32 i = 0;
        for (u32 seg = 0; seg < 16; ++seg) {
            const u32 d = seg * 0x1000;
            if (u32(a.s32(list + i * 12 + 4) + 0x300) < d) {
                while (a.s16(list + i * 12 + 8) != -1) {
                    ++i;
                    if (d <= u32(a.s32(list + i * 12 + 4) + 0x300)) {
                        break;
                    }
                }
            }
            r.segments[seg] = u16(i);
        }
        taken_.assign(((i >> 3) + 4) & ~3u, 0);
        sprite_.start(0x10800, 0x31B, 0, 0, 0, 0x500, 0x2030);
    }

    void update(Game& g) override {
        SpecialRun& r = run_;
        StageState& s = g.stage();
        const Assets& a = g.assets();
        auto& v = g.video();
        r.depths.fill(0x7FFF);
        std::fill(r.scale_set.begin(), r.scale_set.begin() + 16, u8(0));
        const u32 list = a.u32(stage_objects + r.stage * 4u);
        animate(g, sprite_);
        const u32 frame = sprite_.frame;
        const s32 pz = r.player.z >> 8;
        const s32 far = pz + 0x300;
        const s32 near = pz - 0xC0;
        u32 i = r.segments[(far >> 12) & 0xFF];
        u32 drawn = 0;
        bool after_take = false; // the scale check then reads the sparkle's kind (1)
        const Sprite& body = r.body.sprite;
        while (a.s32(list + i * 12 + 4) < far) {
            const u32 e = list + i * 12;
            const s16 kind = a.s16(e + 8);
            if (kind == -1) {
                return;
            }
            const s32 ez = a.s32(e + 4);
            if (kind == 0 && near <= ez && !((taken_[i >> 3] >> (i & 7)) & 1)) {
                const s32 x = a.s16(e) << 8, y = a.s16(e + 2) << 8, z = ez << 8;
                const s16 d = s16(pz - ez);
                const Projection p = project_depth(d);
                const s16 px = s16(quotient(p.factor * x, 0x280) >> 8);
                const s16 py = s16(quotient(p.factor * y, 0x280) >> 8);
                const SpecialPlayer& pl = r.player;
                const Hitbox& hb = sprite_.hitboxes[0];
                const Hitbox& ph = body.hitboxes[0];
                if (!(pl.flags & special_flag::still) && pl.state != 2 && (z >> 8) - 0x20 <= pz &&
                    pz <= (z >> 8) &&
                    spans_meet((pl.x >> 8) + ph.left, ph.right - ph.left, px + hb.left,
                               hb.right - hb.left) &&
                    spans_meet((pl.y >> 8) + ph.top, ph.bottom - ph.top, py + hb.top,
                               hb.bottom - hb.top)) {
                    take(g, i, x, y, z, d);
                    after_take = true;
                } else {
                    const u8 set = depth_set(r, d);
                    const s16 sx = s16(px + r.offset_x + 0x78);
                    const s16 sy = s16(0x50 - r.offset_y - py);
                    const s16 scale = p.scale;
                    sprite_.flags = set | 0x2020u;
                    if (scale > 0x100) {
                        sprite_.flags = set | 0x2060u;
                    }
                    const s32 w = a.u16(frame + 4) >> 1, h = a.u16(frame + 6) >> 1;
                    sprite_.x = s16(sx - ((scale * (a.s16(frame + 8) - w) + w * 0x100) >> 8));
                    sprite_.y = s16(sy - ((scale * (a.s16(frame + 10) - h) + h * 0x100) >> 8));
                    const unsigned check = after_take ? 1u : set;
                    const auto compute_scale = [&] {
                        if (r.scale_set[check] == 0) {
                            r.scale_set[set] = 0xFF;
                            gba::Rotation& m = v.rotations.at(set);
                            m.pa = s16(divide(0x10000, scale));
                            m.pb = m.pc = 0;
                            m.pd = s16(divide(0x10000, scale));
                        }
                    };
                    if (drawn == 0 || sprite_.first_entry == 0xFF) {
                        sprite_.first_entry = 0xFF;
                        compute_scale();
                        draw(g, sprite_);
                    } else {
                        if (sprite_.flags & 0x40) {
                            sprite_.x = s16(sprite_.x - (a.u16(frame + 4) >> 1));
                            sprite_.y = s16(sprite_.y - (a.u16(frame + 6) >> 1));
                        }
                        compute_scale();
                        const auto at = v.queued_entries[sprite_.first_entry].attributes();
                        gba::Sprite c;
                        c.set_attributes(
                            u16(u8(sprite_.y) | ((sprite_.flags & 0x40) << 3) | (at[0] & 0xFD00)),
                            u16((sprite_.x & 0x1FF) | ((sprite_.flags & 0x1F) << 9) |
                                (at[1] & 0xC000)),
                            at[2]);
                        if (!queue_sprite(v, (sprite_.layer & 0x7C0) >> 6, c)) {
                            return;
                        }
                    }
                    ++drawn;
                }
            }
            ++i;
        }
        (void)s;
    }

private:
    // A ring taken: its sparkle (as a list thing of kind 1).
    void take(Game& g, u32 i, s32 x, s32 y, s32 z, s16 d) {
        SpecialRun& r = run_;
        const Assets& a = g.assets();
        auto& o = g.scheduler().create<ListObject>(r, 0x1500, 1);
        o.mode = ListObject::Mode::sparkle;
        o.x = x;
        o.y = y;
        o.z = z;
        o.animation = a.u16(object_animations + 4);
        o.variant = a.u16(object_animations + 6);
        o.set = depth_set(r, d);
        o.used = 1;
        o.sprite.tiles.to = 0x10000 + a.u16(object_tiles + 2) * 0x20u;
        o.sprite.tiles.bytes = 0;
        o.sprite.animation = o.animation;
        o.sprite.variant = u8(o.variant);
        o.sprite.previous_variant = 0xFF;
        o.sprite.timer = 0;
        o.sprite.speed = 0x10;
        o.sprite.palette = 0;
        o.sprite.flags = o.set | 0x2020u;
        o.sprite.layer = 0x7C0;
        StageState& s = g.stage();
        ++s.special.rings;
        ring_chime(g);
        taken_[i >> 3] = u8(taken_[i >> 3] | (1u << (i & 7)));
    }

    SpecialRun& run_;
    Sprite sprite_;         // +0C
    std::vector<u8> taken_; // +3C (08006A88)
};

void start_card(Game& g, bool first);

// The player's routines (0802A530 and the ones it switches to).
class SpecialHero final : public Process {
public:
    SpecialHero(Game& g, SpecialRun& r) : Process(0x1100), run_(r) {
        // 08029DDC / 08029E78
        const Assets& a = g.assets();
        SpecialPlayer& p = r.player;
        p.character = g.stage().character;
        p.bob = -0x34;
        p.speed_z = 0x800;
        p.animation = s16(a.u8(character_animations + p.character) + 0x1B);
        p.variant = 4;
        Sprite& sp = r.body.sprite;
        sp.start(0x10000, u16(p.animation), u8(p.variant), s16((p.x >> 8) + 0x78),
                 s16(0x50 - (p.y >> 8)), 0x180, 0x2020);
        r.body.angle = 0;
        r.body.scale_x = r.body.scale_y = 0x100;
        r.body.x = r.body.y = 0;
    }

    void update(Game& g) override {
        switch (move_) {
        case Move::run:
            run(g);
            return;
        case Move::spring:
            spring(g);
            return;
        case Move::hit:
            hit(g);
            return;
        case Move::jump:
            jump(g);
            return;
        case Move::dash:
            dash(g);
            return;
        case Move::to_gate:
            to_gate(g);
            return;
        case Move::fail:
            fail(g);
            return;
        case Move::second_half:
            second_half(g);
            return;
        case Move::cleared:
            cleared(g);
            return;
        }
    }

private:
    enum class Move : u8 { run, spring, hit, jump, dash, to_gate, fail, second_half, cleared };

    SpecialPlayer& p() { return run_.player; }

    const Assets& assets(Game& g) { return g.assets(); }

    s16 base(Game& g) { return s16(g.assets().u8(character_animations + p().character)); }

    void read_buttons(Game& g) {
        p().held = g.controller().held;
        p().pressed = g.controller().pressed_once;
    }

    void bob() { p().bob_phase = u16((p().bob_phase + 0x10) & 0x3FF); }

    void wobble(Game& g) {
        bob();
        p().bob = s16(sine(g.assets(), p().bob_phase) >> 10);
    }

    void set_picture(s16 animation, u16 variant) {
        p().animation = animation;
        p().variant = variant;
        Sprite& sp = run_.body.sprite;
        sp.animation = u16(animation);
        sp.variant = u8(variant);
        sp.previous_variant = 0xFF;
    }

    void count_down_dash_wait() {
        if (p().dash_wait != 0) {
            --p().dash_wait;
        }
    }

    // 0802A038: the direction held pushes (from the table); without it the
    // player drifts to a stop.
    void steer(Game& g) {
        const Assets& a = g.assets();
        SpecialPlayer& q = p();
        const u32 dir = (q.held & 0xF0) >> 2;
        const auto settle = [](s16 speed) {
            const s32 v = speed;
            const s32 d = (v < 0 ? v + 3 : v) >> 2;
            return s16(d == 0 ? -v : -d);
        };
        if ((q.held & 0xC0) == 0) {
            q.accel_y = settle(q.speed_y);
        } else {
            q.accel_y = a.s16(steer_push + dir + 2);
            if (q.speed_y > 0 && q.accel_y < 0) {
                q.speed_y = 0;
            }
            if (q.speed_y < 0 && q.accel_y > 0) {
                q.speed_y = 0;
            }
        }
        if ((q.held & 0x30) == 0) {
            q.accel_x = settle(q.speed_x);
        } else {
            q.accel_x = a.s16(steer_push + dir);
            if (q.speed_x > 0 && q.accel_x < 0) {
                q.speed_x = 0;
            }
            if (q.speed_x < 0 && q.accel_x > 0) {
                q.speed_x = 0;
            }
        }
    }

    // 0802A104: leaning by the direction held, or by where in the view.
    void choose_picture(Game& g) {
        const Assets& a = g.assets();
        SpecialPlayer& q = p();
        if (!(q.flags & special_flag::fast)) {
            if ((q.held & 0xF0) == 0) {
                u32 k = q.x < -0x2800 ? 1 : (q.x < 0x2801 ? 2 : 3);
                if (q.y < 0x1E00) {
                    k += q.y < -0x1DFF ? 6 : 3;
                }
                q.animation = s16(base(g) + a.s16(lean_animations + k * 4));
                q.variant = a.u16(lean_animations + k * 4 + 2);
            } else {
                const u32 dir = (q.held & 0xF0) >> 2;
                q.animation = s16(base(g) + a.s16(steer_animations + dir));
                q.variant = a.u16(steer_animations + dir + 2);
            }
        } else {
            q.animation = s16(base(g) + 0x1D);
            q.variant = q.speed_z < 0xC01 ? 0 : 1;
        }
        Sprite& sp = run_.body.sprite;
        if (s16(sp.animation) != q.animation || sp.variant != q.variant) {
            sp.variant = u8(q.variant);
            sp.previous_variant = 0xFF;
            sp.animation = u16(q.animation);
        }
    }

    // 0802A218: the move: speeds and the tube's edges; faster than 800h
    // the player is drawn in towards the middle.
    void move() {
        SpecialPlayer& q = p();
        const s16 ax = q.accel_x, ay = q.accel_y;
        q.speed_x = s16(q.speed_x + ax);
        q.speed_y = s16(q.speed_y + ay);
        q.speed_z = s16(q.speed_z + q.accel_z);
        const auto clamp400 = [](s16& v) {
            if (v > 0x400) {
                v = 0x400;
            } else if (v < -0x400) {
                v = -0x400;
            }
        };
        const auto by_depth_speed = [&](s16 v) {
            return s32(quotient(s32(v) << 3, s16(q.speed_z) >> 8));
        };
        if (q.state == 1) {
            if (q.spring_kind == 4 || q.spring_kind == 5) {
                q.speed_x = s16(q.speed_x + ax);
                q.speed_y = s16(q.speed_y + ay);
                clamp400(q.speed_x);
                clamp400(q.speed_y);
                const s32 m = s16(q.speed_z) >> 8;
                if (q.spring_kind == 4) {
                    const auto scaled = [&](s16 v) {
                        const s32 t = m * v;
                        return s16((t < 0 ? t + 7 : t) >> 3);
                    };
                    q.speed_x = scaled(q.speed_x);
                    q.speed_y = scaled(q.speed_y);
                } else {
                    q.speed_x = s16(by_depth_speed(q.speed_x));
                    q.speed_y = s16(by_depth_speed(q.speed_y));
                }
            }
            q.x += q.speed_x;
            q.y += q.speed_y;
            if (!(q.flags & special_flag::still)) {
                q.z += q.speed_z;
            }
        } else if (q.state <= 5) {
            clamp400(q.speed_x);
            clamp400(q.speed_y);
            if (q.speed_z > 0xC00) {
                q.speed_z = 0xC00;
            } else if (q.speed_z < 0x800) {
                q.speed_z = 0x800;
            }
            if (q.speed_z == 0x800) {
                q.flags &= ~special_flag::fast;
            } else {
                q.flags |= special_flag::fast;
            }
            if (!(q.flags & special_flag::fast)) {
                q.x += q.speed_x;
                q.y += q.speed_y;
            } else {
                const s32 lim = q.y >> 5;
                if ((lim < 0 && q.speed_y < lim) || (lim >= 0 && lim < q.speed_y)) {
                    q.speed_y = s16(lim);
                }
                const s32 nx = q.x + by_depth_speed(q.speed_x);
                q.x = nx - (nx >> 5);
                const s32 ny = q.y + by_depth_speed(q.speed_y);
                q.y = ny - (ny >> 5);
            }
            if (!(q.flags & special_flag::still)) {
                q.z += q.speed_z;
            }
        }
        q.x = std::clamp(q.x, -0x8600, 0x8600);
        q.y = std::clamp(q.y, -0x5E00, 0x5E00);
    }

    // 0802A494: the projection and the scale (the wobble).
    void project() {
        SpecialRun& r = run_;
        SpecialPlayer& q = p();
        const s32 k = q.bob + 0x240;
        const s16 px = s16(quotient(q.x * k, 0x240) >> 8);
        const s16 py = s16(quotient(q.y * k, 0x240) >> 8);
        q.screen_x = s16(px + 0x78 + r.offset_x);
        q.screen_y = s16(0x50 - py - r.offset_y);
        r.body.sprite.x = q.screen_x;
        r.body.sprite.y = q.screen_y;
        r.body.x = q.screen_x;
        r.body.y = q.screen_y;
        r.body.angle = 0;
        r.body.scale_x = r.body.scale_y = s16(q.bob + 0x100);
        r.body.sprite.flags = 0x2020;
    }

    void show(Game& g, bool drawn = true) {
        move();
        project();
        Sprite& sp = run_.body.sprite;
        animate(g, sp);
        place_scaled(g, run_, sp, run_.body.scale_x, run_.body.scale_y, run_.body.x, run_.body.y);
        if (drawn) {
            draw(g, sp);
        }
    }

    void show_steered(Game& g, bool drawn = true) {
        steer(g);
        show(g, drawn);
    }

    // 08029F00: a bomb or a spring touched (bombs first).
    bool touched(Game& g) {
        SpecialPlayer& q = p();
        if (q.flags & special_flag::still) {
            return false;
        }
        if (q.flags & special_flag::bombed) {
            q.state = 2;
            q.flags &= ~special_flag::bombed;
            start_hit(g);
            return true;
        }
        if ((q.flags & special_flag::sprung) && q.last_spring != q.touched_spring) {
            q.state = 1;
            q.flags &= ~special_flag::sprung;
            q.last_spring = q.touched_spring;
            start_spring(g);
            return true;
        }
        return false;
    }

    // 08029F74: the other button: a jump, or (just after a capsule) a dash.
    bool jumped(Game& g) {
        SpecialPlayer& q = p();
        const StageState& s = g.stage();
        if (q.jump_wait != 0) {
            --q.jump_wait;
            q.dash_wait = 0;
            return false;
        }
        if (q.dash_wait == 0) {
            if ((s.other_buttons & q.pressed) && !(q.flags & special_flag::still)) {
                q.jump_kind = (q.pressed & button::l) ? u8(q.flags & special_flag::still) : 0xFF;
                start_jump(g);
                return true;
            }
            return false;
        }
        --q.dash_wait;
        if (s.other_buttons & q.pressed) {
            start_dash(g);
            g.sound().stop_player(0);
            play_jingle(g, 0x2B, 0x2A);
            return true;
        }
        return false;
    }

    // 0802A530: running down the tube; jump held speeds up.
    void run(Game& g) {
        SpecialPlayer& q = p();
        read_buttons(g);
        if (q.exit & 0xC0) {
            // The gate (or the end) is passed: the player moves to the
            // middle over 78h frames.
            q.end_timer = 0x78;
            q.speed_x = s16(quotient(-q.x, 0x78));
            q.speed_y = s16(quotient(-q.y, 0x78));
            q.accel_x = q.accel_y = 0;
            move_ = Move::to_gate;
            to_gate(g);
            return;
        }
        if (touched(g) || jumped(g)) {
            return;
        }
        wobble(g);
        const StageState& s = g.stage();
        if (!(s.jump_buttons & q.held) || (q.flags & special_flag::still)) {
            q.accel_z = (q.flags & special_flag::fast) ? -0x100 : 0;
        } else {
            q.accel_z = 0x100;
        }
        steer(g);
        choose_picture(g);
        show(g);
    }

    // 0802A658: a spring throws (by kind 4-9 from the tables; a third
    // more when fast; kinds 6-9 one and a half times, slowing three times
    // as fast).
    void start_spring(Game& g) {
        const Assets& a = g.assets();
        SpecialPlayer& q = p();
        const u32 k = u16(q.spring_kind - 4);
        set_picture(s16(base(g) + a.s16(spring_animations + k * 4)),
                    a.u16(spring_animations + k * 4 + 2));
        const s16 tx = a.s16(spring_throws + k * 6), ty = a.s16(spring_throws + k * 6 + 2),
                  tz = a.s16(spring_throws + k * 6 + 4);
        if (!(q.flags & special_flag::fast)) {
            q.speed_x = tx;
            q.speed_y = ty;
            q.speed_z = tz;
        } else {
            q.speed_x = s16(quotient(tx << 2, 3));
            q.speed_y = s16(quotient(ty << 2, 3));
            q.speed_z = s16(quotient(tz << 2, 3));
        }
        if (k == 0) {
            q.accel_z = 0x40;
            q.accel_x = q.accel_y = 0;
        } else if (k == 1) {
            q.accel_x = q.accel_y = 0;
            q.accel_z = -0x40;
        } else {
            q.accel_x = s16(-(tx >> 7));
            q.accel_y = s16(-(ty >> 7));
            q.accel_z = -0x20;
            // (Software floating point in the original.)
            q.speed_x = s16(s32(double(q.speed_x) * 1.5));
            q.speed_y = s16(s32(double(q.speed_y) * 1.5));
            q.speed_z = s16(s32(double(q.speed_z) * 1.5));
            q.accel_x = s16(s32(double(q.accel_x) * 3.0));
            q.accel_y = s16(s32(double(q.accel_y) * 3.0));
            q.accel_z = -0x60;
        }
        if (g.stage().special.quiet == 0) {
            g.sound().start(sound_spring);
        }
        q.flags &= ~(special_flag::fast | special_flag::sprung);
        show(g);
        move_ = Move::spring;
    }

    // 0802A860
    void spring(Game& g) {
        SpecialPlayer& q = p();
        read_buttons(g);
        count_down_dash_wait();
        if (q.spring_kind == 4) {
            if (q.speed_z > 0) {
                steer(g);
            }
        } else if (q.spring_kind == 5) {
            steer(g);
        }
        if (touched(g)) {
            return;
        }
        const bool landed = q.spring_kind == 4 ? q.speed_z >= 0x800 : q.speed_z < 0x801;
        if (landed) {
            move_ = Move::run;
            q.state = 0;
            q.speed_z = 0x800;
            q.accel_z = 0;
            q.jump_wait = 0;
        }
        show(g);
    }

    // 0802A958: a bomb: rings lost (up to ten), knocked back to 800h.
    void start_hit(Game& g) {
        StageState& s = g.stage();
        SpecialPlayer& q = p();
        if (s.special.rings != 0) {
            lose_rings(g);
            if (s.special.quiet == 0) {
                g.sound().start(sound_rings_lost);
            }
        }
        set_picture(s16(base(g) + 0x20), 0);
        q.speed_x = q.speed_y = 0;
        q.speed_z = 0x800;
        q.accel_x = q.accel_y = q.accel_z = 0;
        q.flags &= ~special_flag::still;
        q.hit_timer = 0x3C;
        show(g);
        move_ = Move::hit;
    }

    // 0802AAC0
    void lose_rings(Game& g) {
        StageState& s = g.stage();
        const u16 n = std::min<u16>(s.special.rings, 10);
        s.special.rings = u16(s.special.rings - n);
        for (u16 k = n; k != 0; --k) {
            g.scheduler().create<LostRing>(run_, u8(k - 1), u8(n));
        }
    }

    // 0802AA18: blinking for a second.
    void hit(Game& g) {
        SpecialPlayer& q = p();
        read_buttons(g);
        count_down_dash_wait();
        if (--q.hit_timer == 0) {
            move_ = Move::run;
            q.state = 0;
            q.jump_wait = 0;
        }
        if (q.hit_timer < 0x1F) {
            steer(g);
        }
        show(g, (q.hit_timer & 2) == 0);
    }

    // 0802AB70 / 0802AC20: a jump (2Dh frames).
    void start_jump(Game& g) {
        SpecialPlayer& q = p();
        set_picture(s16(base(g) + 0x1E), 0);
        q.speed_x = q.speed_y = 0;
        q.accel_x = q.accel_y = q.accel_z = 0;
        q.state = 3;
        q.flags |= special_flag::jumping;
        q.jump_timer = 0x2D;
        if (g.stage().special.quiet == 0) {
            g.sound().start(sound_jump);
        }
        show(g);
        move_ = Move::jump;
    }

    void jump(Game& g) {
        SpecialPlayer& q = p();
        count_down_dash_wait();
        if (touched(g)) {
            return;
        }
        q.accel_z = (q.flags & special_flag::fast) ? -0x80 : 0;
        if (--q.jump_timer == 0) {
            move_ = Move::run;
            q.flags &= ~special_flag::jumping;
            q.state = 0;
            q.jump_wait = 0xC;
        }
        show(g);
    }

    // 0802ACC0 / 0802AD6C: a dash (its animation's length).
    void start_dash(Game& g) {
        SpecialPlayer& q = p();
        set_picture(s16(base(g) + 0x1F), 0);
        q.speed_x = q.speed_y = 0;
        q.accel_x = q.accel_y = q.accel_z = 0;
        q.state = 4;
        q.flags |= special_flag::dashing;
        if (g.stage().special.quiet == 0) {
            g.sound().start(sound_jump);
        }
        show(g);
        move_ = Move::dash;
    }

    void dash(Game& g) {
        SpecialPlayer& q = p();
        count_down_dash_wait();
        if (touched(g)) {
            return;
        }
        q.accel_z = (q.flags & special_flag::fast) ? -0x80 : 0;
        if (run_.body.sprite.flags & sprite_flag::ended) {
            move_ = Move::run;
            q.flags &= ~special_flag::dashing;
            q.state = 0;
            q.jump_wait = 0xC;
        }
        show(g);
    }

    // 0802AE10: into the middle; then the gate's (or the end's) outcome.
    void to_gate(Game& g) {
        SpecialPlayer& q = p();
        StageState& s = g.stage();
        q.held = q.pressed = 0;
        wobble(g);
        q.accel_z = (q.flags & special_flag::fast) ? -0x100 : 0;
        choose_picture(g);
        show(g);
        if (--q.end_timer != 0) {
            return;
        }
        q.variant = 0;
        const u16 needed = q.exit == 0x40 ? s.special.target : s.special.second_target;
        if (s.special.rings < needed) {
            q.animation = s16(base(g) + 0x22);
            q.end_timer = 0;
            move_ = Move::fail;
            g.scheduler().create<TumblingHero>(g, run_);
            show_message(g, run_, 1);
        } else if (q.exit == 0x40) {
            q.end_timer = 0x14A;
            q.animation = s16(base(g) + 0x21);
            move_ = Move::second_half;
            show_message(g, run_, 0);
        } else {
            q.animation = s16(base(g) + 0x21);
            move_ = Move::cleared;
            g.sound().start(sound_cleared);
            show_message(g, run_, 0);
        }
        Sprite& sp = run_.body.sprite;
        sp.previous_variant = 0xFF;
        sp.variant = u8(q.variant);
        sp.animation = u16(q.animation);
    }

    // 0802AFD8: too few rings: the player shrinks away.
    void fail(Game& g) {
        SpecialPlayer& q = p();
        if (q.end_timer <= -0x78) {
            return;
        }
        q.bob = s16(q.end_timer - 1);
        q.held = q.pressed = 0;
        q.accel_z = (q.flags & special_flag::fast) ? -0x100 : 0;
        q.end_timer = q.bob;
        show_steered(g);
    }

    // 0802B04C: past the first gate: the second half's card.
    void second_half(Game& g) {
        SpecialPlayer& q = p();
        StageState& s = g.stage();
        q.held = q.pressed = 0;
        wobble(g);
        q.accel_z = (q.flags & special_flag::fast) ? -0x100 : 0;
        show_steered(g);
        if (q.end_timer == 0xB4) {
            s.special.shown_target = s.special.second_target;
            g.scheduler().destroy_range(0x1180, 0x1181); // the counter
            start_card(g, false);
        }
        if (q.end_timer != 0 && --q.end_timer == 0) {
            q.variant = 1;
            run_.body.sprite.variant = 1;
            run_.body.sprite.previous_variant = 0xFF;
        }
        if (run_.body.sprite.flags & sprite_flag::ended) {
            move_ = Move::run;
            q.flags = 0;
            q.exit = 0;
            q.jump_wait = 0xC;
        }
    }

    // 0802B15C: past the end with enough rings.
    void cleared(Game& g) {
        SpecialPlayer& q = p();
        q.held = q.pressed = 0;
        wobble(g);
        q.accel_z = (q.flags & special_flag::fast) ? -0x100 : 0;
        show_steered(g);
    }

    SpecialRun& run_;
    Move move_ = Move::run;
};

// 0802D28C / 08029CAC: the tube: layer 2 (and 3, an eighth as far) scrolls
// against the player's place; the tube's picture steps through 16 frames
// (assets 37h-46h) with the player's speed.
class Tube final : public Process {
public:
    explicit Tube(SpecialRun& r) : Process(0x1200), run_(r) {}

    void update(Game& g) override {
        if (!started_) {
            started_ = true;
            auto& v = g.video().settings;
            v.blend.set_control(0);
            v.blend.evy = 0;
            run_.phase = 0;
        }
        SpecialRun& r = run_;
        const SpecialPlayer& p = r.player;
        r.offset_x = s16(u32(quotient(p.x, 0x86) * -0x20) >> 8);
        r.offset_y = s16(u32(quotient(p.y, 0x5E) * -0x20) >> 8);
        r.offset_x = std::clamp<s16>(r.offset_x, -0x20, 0x20);
        r.offset_y = std::clamp<s16>(r.offset_y, -0x20, 0x20);
        auto& bg = g.video().settings.bg;
        const s32 fx = r.centre_x - r.offset_x;
        bg[3].scroll_x = u16((fx < 0 ? fx + 7 : fx) >> 3);
        const s32 fy = r.centre_y + r.offset_y;
        bg[3].scroll_y = u16((fy < 0 ? fy + 7 : fy) >> 3);
        bg[2].scroll_x = u16(r.centre_x - r.offset_x);
        bg[2].scroll_y = u16(r.offset_y + r.centre_y);
        r.phase = u8((s8(p.speed_z >> 9) + r.phase) & 0x3F);
        const u16 picture = u16((s8(r.phase) >> 2) + 0x37);
        r.layers[0].asset = picture;
        r.layers[1].asset = picture;
        r.layers[0].tiles_queued = false;
        r.layers[2].asset = picture;
        r.layers[3].asset = picture;
        for (unsigned i = 0; i < 4; ++i) {
            load_background(g.assets(), g.video(), g.backgrounds(), r.layers[i]);
        }
    }

private:
    SpecialRun& run_;
    bool started_{};
};

// 0802D650: Start pauses (not at the ends).
class SpecialPauseWatch final : public Process {
public:
    SpecialPauseWatch() : Process(0x1000) {}

    void update(Game& g) override {
        const StageState& s = g.stage();
        if (!(s.flags & 0x20) && (g.controller().pressed_once & button::start) &&
            !(s.flags & 0x40)) {
            request_pause(g);
        }
    }
};

void start_special_results(Game& g, bool emerald);

// 0802D1DC and its stages: the run's course: four seconds' start, the
// first gate (and its check), the end; then the fade to white and the
// results (or the emerald).
class SpecialCourse final : public Process {
public:
    SpecialCourse(u8 stage) : Process(0x1000), run(stage) {}

    SpecialRun run;

    void update(Game& g) override {
        SpecialRun& r = run;
        StageState& s = g.stage();
        switch (stage_) {
        case 0: // 0802D1DC
            r.outcome = 0;
            r.outcome_timer = 0xF0;
            r.fade = 0;
            stage_ = 1;
            g.sound().start(music_special);
            return;
        case 1: // 0802D208
            if (--r.outcome_timer < 1) {
                stage_ = 2;
                r.player.flags &= ~special_flag::still;
                r.pictures.fill({0, 0});
            }
            return;
        case 2: // 08029980
            if (r.outcome == 2) {
                r.outcome_timer = 0x1A4;
                r.player.flags |= special_flag::still;
                r.player.exit = 0x40;
                stage_ = 3;
                if (s.special.rings < s.special.target) {
                    show_message(g, r, 2);
                }
            }
            if (r.outcome == 1) {
                r.outcome_timer = 0x1A4;
                r.player.flags |= special_flag::still;
                r.player.exit = 0x80;
                stage_ = 4;
                if (s.special.rings < s.special.second_target) {
                    show_message(g, r, 2);
                }
            }
            return;
        case 3: // 08029A20: the first gate
            if (--r.outcome_timer < 1) {
                if (s.special.rings < s.special.target) {
                    s.flags |= 0x20;
                    stage_ = 5;
                } else {
                    r.outcome = 0;
                    r.player.flags &= ~special_flag::still;
                    r.pictures.fill({0, 0});
                    stage_ = 2;
                }
            }
            return;
        case 4: // 0802D244: the end
            if (--r.outcome_timer < 1) {
                s.flags |= 0x20;
                stage_ = 5;
            }
            return;
        default: { // 08029A94: to white
            r.fade = u16(r.fade + 0x40);
            auto& v = g.video().settings;
            v.blend.set_control(0x3F9F);
            v.blend.eva = 0;
            v.blend.evb = 0;
            v.blend.evy = u8(r.fade >> 8);
            if (r.fade > 0xFFF) {
                v.blend.eva = 0x10;
                const bool first_gate = r.outcome == 2;
                const bool short_of = s.special.rings < s.special.shown_target;
                g.clear_screen_processes();
                v.blend.set_control(v.blend.control() | 0x3F1F);
                start_special_results(g, !(first_gate || short_of));
            }
            return;
        }
        }
    }

private:
    u8 stage_{};
};

// ---------------------------------------------------------------------------
// The counter (08053CE4) and the card (0805C414).

// 08053CE4: the rings taken and the rings asked for, sliding in.
class SpecialCounter final : public Process {
public:
    explicit SpecialCounter(u8 leftover) : Process(0x1180), blink_(leftover) {}

    void update(Game& g) override {
        const StageState& s = g.stage();
        const s16 t = ++time_;
        const auto slide = [&](s32 from, s16 lead, s16 start, s16 rest) -> s16 {
            const u32 u = u32(t - from);
            if ((u & 0xFFFF) < 0x14) {
                return s16(divide(s32(u * 0x40), 0x14) + start);
            }
            return t < from + 0x14 ? start : rest;
            (void)lead;
        };
        SpriteText text;
        text.layer = 0x18;
        text.x = 0x5E;
        text.y = slide(10, 0, -0x46, -6);
        text.count = 1;
        text.slot = 3;
        text.palette = 10;
        text.single_row = true;
        const auto icon = g.assets().bytes(0x08658074, 1);
        draw_text(g, icon.data(), text, false);
        digits(s.special.rings, rings_);
        text.y = slide(0x14, 0, -0x40, 0);
        text.palette = 6;
        text.layer = 0x10;
        text.slot = 0;
        number(g, text, s.special.rings, rings_, 0x66, 0x62, 0x5E);
        digits(s.special.shown_target, target_);
        text.y = slide(0xF, 0, -0x34, 0xC);
        text.palette = 6;
        text.layer = 0x10;
        text.slot = 0;
        number(g, text, s.special.shown_target, target_, 0x7E, 0x7A, 0x76);
    }

private:
    static void digits(u16 n, std::array<u8, 3>& out) {
        if (n < 1000) {
            for (int i = 0; i < 3; ++i) {
                const u32 q = u32(divide(n, 10));
                out[std::size_t(2 - i)] = u8(n - q * 10 + 0x20);
                n = u16(q);
            }
        } else {
            out = {0x29, 0x29, 0x29};
        }
    }

    void number(Game& g, SpriteText& text, u16 n, const std::array<u8, 3>& d, s16 x1, s16 x2,
                s16 x3) {
        if (n < 10) {
            text.x = x1;
            text.count = 1;
            if (n == 0) {
                // A blinking zero.
                const auto zero = g.assets().bytes(0x08658070 + (blink_ >> 3), 1);
                draw_text(g, zero.data(), text, false);
                blink_ = u8((blink_ + 1) & 0x1F);
            } else {
                draw_text(g, &d[2], text, false);
            }
        } else if (n < 100) {
            text.x = x2;
            text.count = 2;
            draw_text(g, &d[1], text, false);
        } else {
            text.x = x3;
            text.count = 3;
            draw_text(g, d.data(), text, false);
        }
    }

    std::array<u8, 3> rings_{};  // +00
    std::array<u8, 3> target_{}; // +10
    u8 blink_;                   // +48 (never set: StageState::special.counter_leftover)
    s16 time_{};                 // +4C
};

// 080541BC / 08054034
void start_special_counter(Game& g) {
    g.scheduler().create<SpecialCounter>(g.stage().special.counter_leftover);
    g.screen_graphics->reset_pool();
    GraphicsLoad l;
    l.options = 0;
    l.slot = 0;
    l.tiles_from = 0x08658FF8;
    l.tiles_to = 0x178A0;
    l.tile_bytes = 0xC0;
    l.colors_from = 0x0865A078;
    l.color_bytes = 0x20;
    l.palette = 6;
    l.parts = load::tiles | load::sprite;
    l.key = 2;
    l.extra = {0, 2, 1, 2};
    load_graphics(g, l);
    l.tiles_from = 0x08658478;
    l.tiles_to = 0x175E0;
    l.tile_bytes = 0x2C0;
    l.parts = load::tiles | load::colors | load::sprite;
    load_graphics(g, l);
    l.options = 0x2E;
    l.slot = 3;
    l.tiles_from = 0x08675158;
    l.colors_from = 0x08675558;
    l.tile_bytes = 0x400;
    l.palette = 10;
    l.tiles_to = 0x17BE0;
    l.key = 0x20;
    l.extra = {3, 1, 8, 4};
    load_graphics(g, l);
}

constexpr u32 pictures = 0x08680A38; // 7 words a picture (see title_card.cpp)

void take_picture(const Assets& a, GraphicsLoad& l, u8 index) {
    const u32 p = pictures + index * 28u;
    l.options = index;
    l.tiles_from = a.u32(p);
    l.colors_from = a.u32(p + 4);
    l.key = a.u32(p + 8);
    l.extra = {a.u8(p + 12), a.u8(p + 16), a.u8(p + 20), a.u8(p + 24)};
}

// 0805BC60: the card's pictures: the band (picture 32h), the words'
// letters (slot 8), the "get ... rings" picture by the number's digits
// (pictures 2Fh-31h) and the number's digits (two halves each).
void load_card_pictures(Game& g, u16 target) {
    const Assets& a = g.assets();
    const u32 digits = divide(target, 100) != 0 ? 3 : divide(target, 10) != 0 ? 2 : 1;
    GraphicsLoad l;
    take_picture(a, l, 0x32);
    l.slot = 4;
    l.tiles_to = 0x15DA0;
    l.tile_bytes = 0xA00;
    l.color_bytes = 0x20;
    l.palette = 6;
    l.parts = load::tiles | load::colors | load::sprite;
    load_graphics(g, l);
    l.options = 0x10;
    l.slot = 8;
    l.tiles_from = 0x0865F078;
    l.colors_from = 0x0865F278;
    l.tile_bytes = 0x200;
    l.tiles_to = 0x177A0;
    l.key = 0x10;
    l.extra = {2, 0, 4, 4};
    load_graphics(g, l);
    take_picture(a, l, u8(0x2E + digits));
    l.slot = 2;
    l.tile_bytes = 0xE00;
    l.tiles_to = 0x167A0;
    load_graphics(g, l);
    const auto digit = [&](u32 d, u32 top_to, u32 bottom_to) {
        l.options = 0x20;
        l.slot = 3;
        l.key = 4;
        l.extra = {1, 0, 2, 2};
        l.colors_from = 0x08666778;
        l.tile_bytes = 0x40;
        l.tiles_from = 0x086661F8 + d * 0x80;
        l.tiles_to = top_to;
        load_graphics(g, l);
        l.tiles_from = 0x08666238 + d * 0x80;
        l.tiles_to = bottom_to;
        load_graphics(g, l);
    };
    u32 n = target;
    if (digits == 3) {
        bool leading = true;
        const u32 hundreds = u32(divide(s32(n), 100)) & 0xFF;
        n = u32(remainder(s32(n), 100)) & 0xFFFF;
        if (hundreds != 0) {
            leading = false;
            digit(hundreds, 0x169E0, 0x16A60);
        }
        const u32 tens = u32(divide(s32(n), 10)) & 0xFF;
        n = u32(remainder(s32(n), 10)) & 0xFFFF;
        if (tens != 0 || !leading) {
            leading = false;
            digit(tens, 0x16BA0, 0x16C20);
        }
        if ((n & 0xFF) == 0 && leading) {
            return;
        }
        digit(n & 0xFF, 0x16BE0, 0x16C60);
    } else if (digits == 2) {
        const u32 tens = u32(divide(s32(n), 10)) & 0xFF;
        n = u32(remainder(s32(n), 10)) & 0xFFFF;
        digit(tens, 0x169E0, 0x16A60);
        digit(n & 0xFF, 0x16BA0, 0x16C20);
    } else {
        digit(n & 0xFF, 0x169E0, 0x16A60);
    }
}

// The card's pieces (0x1C-byte records at 2120h): their time comes from
// the card.
class CardPiece : public Process {
public:
    CardPiece() : Process(0x2120) {}

    s16 time{}; // +18

protected:
    SpriteText text; // +08
    u16 phase{};     // +1A
};

// 0805C67C: the white band, in from the right, up to the top, out to the
// left.
class CardBand final : public CardPiece {
public:
    CardBand() {
        text = {.layer = 10,
                .x = 0xF0,
                .y = 0x7E,
                .count = 8,
                .slot = 8,
                .palette = 6,
                .single_row = true};
    }

    void update(Game& g) override {
        static constexpr u8 blanks[8] = {0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20};
        if (time < 0xF6) {
            if (time < 0x2E) {
                if (time < 0x22) {
                    if (time < 0x1A) {
                        if (time > 0xF) {
                            text.x = std::max<s16>(s16(text.x - 0x1A), -0x10);
                        }
                    } else {
                        text.x = -0x10;
                    }
                } else {
                    text.y = s16(text.y - 8);
                }
            } else {
                text.y = 0x11;
            }
        } else {
            if (text.x < -0x20) {
                text.x = s16(text.x + 0x20);
                if (text.count != 0) {
                    --text.count;
                }
            }
            text.x = s16(text.x - 0x1A);
        }
        draw_text(g, blanks, text, true);
    }
};

// 0805C70C: "get ... rings" running in on the band, cycling its glyphs.
class CardWords final : public CardPiece {
public:
    CardWords() {
        text = {.layer = 0,
                .x = 0x100,
                .y = 0x80,
                .count = 9,
                .slot = 2,
                .palette = 6,
                .single_row = true};
    }

    void update(Game& g) override {
        static constexpr u8 glyphs[21] = {0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26,
                                          0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26,
                                          0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26};
        const auto next_glyph = [&] {
            phase = u16(remainder(phase + 1, 7));
        };
        if (time < 0xF6) {
            s16 before;
            if (time < 0x2E) {
                if (time < 0x22) {
                    if (time < 0x1A) {
                        if (time < 0x10) {
                            return;
                        }
                        const u16 old = u16(text.x);
                        text.x = s16(old - 0x1C);
                        if (text.x > 0xEF) {
                            return;
                        }
                        text.count = s16(9 - (s32(text.x) >> 5));
                        if (text.x < -0x1F) {
                            text.x = s16(old + 4);
                            next_glyph();
                        }
                        draw_text(g, glyphs + phase, text, true);
                        return;
                    }
                    before = text.x;
                    text.x = s16(before - 2);
                    text.count = 9;
                } else {
                    text.y = s16(text.y - 8);
                    before = text.x;
                    text.x = s16(before - 2);
                }
            } else {
                text.y = 0x13;
                before = text.x;
                text.x = s16(before - 2);
            }
            if (text.x > -0x20) {
                draw_text(g, glyphs + phase, text, true);
                return;
            }
            text.x = s16(before + 0x1E);
        } else {
            const u16 old = u16(text.x);
            text.x = s16(old - 0x1C);
            if (text.x > -0x20) {
                draw_text(g, glyphs + phase, text, true);
                return;
            }
            if (text.count != 0) {
                --text.count;
            }
            text.x = s16(text.x + 0x20);
        }
        next_glyph();
        draw_text(g, glyphs + phase, text, true);
    }
};

// 0805C808: "start!", unfolding from flat, then off to the left.
class CardStart final : public CardPiece {
public:
    CardStart() {
        text = {.layer = 0,
                .x = 0x39,
                .y = 0x49,
                .count = 5,
                .slot = 4,
                .palette = 6,
                .single_row = true};
    }

    void update(Game& g) override {
        static constexpr u8 glyphs[5] = {0x20, 0x21, 0x22, 0x23, 0x24};
        if (time < 0xF6) {
            if (time < 0x2E) {
                if (time > 0x21) {
                    text.x = 0x29;
                    text.y = s16(0x54 - (scale_y_ >> 3));
                    scale_y_ = std::min<u16>(u16(scale_y_ + 0x10), 0x100);
                    draw_text_scaled(g, glyphs, text, s16(scale_x_), s16(scale_y_), 0, 0);
                }
                return;
            }
            const u16 grown = u16(scale_y_ + 0x10);
            scale_y_ = grown;
            if (grown < 0x100) {
                text.x = 0x29;
                text.y = s16(0x54 - (grown >> 3));
                draw_text_scaled(g, glyphs, text, s16(scale_x_), s16(scale_y_), 0, 0);
                return;
            }
            scale_y_ = 0x100;
            text.x = 0x39;
            text.y = 0x49;
        } else {
            text.y = 0x49;
            const u16 old = u16(text.x);
            text.x = s16(old - 0x1C);
            if (text.x < -0x80) {
                text.count = 0;
                text.x = -0x80;
                return;
            }
        }
        draw_text(g, glyphs, text, true);
    }

private:
    u16 scale_x_ = 0x100; // +00
    u16 scale_y_ = 0;     // +02
};

// 0805C560: the card's clock: it fades the stage in (the first time),
// loads the pictures at frame 2, and at 100h ends, letting the run go.
class SpecialCard final : public Process {
public:
    SpecialCard(CardBand& band, CardWords& words, CardStart* start)
        : Process(0x2100), band_(band), words_(words), start_(start) {
        fade_ = {0, 1, 2, 0, 0x60, 1};
    }

    ScreenFade fade_; // +00

    void update(Game& g) override {
        band_.time = time_;
        words_.time = time_;
        if (start_) {
            start_->time = time_;
            if (time_ < 0x6A) {
                if (time_ < 0x15) {
                    apply_fade(g.video(), fade_);
                } else {
                    step_fade(g.video(), fade_);
                }
            }
        }
        ++time_;
        if (time_ < 0x100) {
            if (time_ == 2) {
                load_card_pictures(g, g.stage().special.shown_target);
            }
            return;
        }
        StageState& s = g.stage();
        auto& v = g.video().settings;
        v.set_control(v.control() & 0x1FFF);
        v.blend.set_control(0);
        v.blend.evy = 0;
        s.flags &= ~stage_flag::paused;
        for (Player& p : s.player) {
            p.flags &= ~player_flag::frozen;
            p.held = u16(p.held | s.jump_buttons | s.other_buttons);
        }
        start_special_counter(g);
        auto& sch = g.scheduler();
        sch.destroy(words_);
        sch.destroy(band_);
        if (start_) {
            sch.destroy(*start_);
        }
        sch.destroy(*this);
    }

private:
    CardBand& band_;   // +10
    CardWords& words_; // +14
    CardStart* start_; // +0C
    s16 time_{};       // +18
};

void start_card(Game& g, bool first) {
    ensure_screen_graphics(g);
    g.screen_graphics->reset_pool();
    auto& sch = g.scheduler();
    CardBand& band = sch.create<CardBand>();
    CardWords& words = sch.create<CardWords>();
    CardStart* start = first ? &sch.create<CardStart>() : nullptr;
    SpecialCard& card = sch.create<SpecialCard>(band, words, start);
    if (first) {
        apply_fade(g.video(), card.fade_);
    }
}

// ---------------------------------------------------------------------------
// The results (0805D014): the bonuses count into a total, the emeralds
// held are shown, then the act starts again at the special spring.

constexpr u32 label_continue = 0x08681580; // 3 glyphs each (slot 3): "continue"
constexpr u32 glyph_zero = 0x08681584;     // one glyph: the digit 0 (slot 6), an emerald (slot 4)
constexpr u32 label_total = 0x08681588;    // "total bonus"
constexpr u32 label_emerald = 0x0868158C;  // "clear bonus" (with the emerald)
constexpr u32 label_rings = 0x08681590;    // "ring bonus"
constexpr u32 glyph_emerald_odd = 0x08681594; // the odd emeralds' glyph (slot 4)
constexpr u32 glyph_no_emerald = 0x08681598;  // an emerald not held (slot 4)
constexpr u16 music_failed = 0x2E, music_got_emerald = 0x2F;
constexpr u16 sound_count = 0x8C, sound_counted = 0x8D, sound_continue = 0x9A;
constexpr u32 emerald_bonus = 10000;

// What every part of the results is given (+18-+2A of each part).
struct SpecialTally {
    u32 emerald_bonus{}; // +18
    u16 rings{};         // +26 (counted down by the rings line)
    bool emerald{};      // +28
    u8 emeralds{};       // +29 the save's emerald bits (with this one)
    u8 continues{};      // +2A continues won
};

std::array<u8, 4> glyphs_at(const Assets& a, u32 at) {
    return {a.u8(at), a.u8(at + 1), a.u8(at + 2), a.u8(at + 3)};
}

// A part (priority 2120): its sprite text (+08-+16) and scale (+00-+06),
// the tally's clock (+20, set by the tally) and its length (+22).
class ResultsPiece : public Process {
public:
    ResultsPiece(const SpecialTally& t, u16 delay, SpriteText text)
        : Process(0x2120), tally(t), text(text), delay_(delay) {}

    SpecialTally tally;
    u16 clock{}; // +20

protected:
    bool leaving() const { return s32(s16(clock)) > delay_ + 0x221; }

    // How far the part has moved left since the leaving started.
    s16 leave_offset() const {
        return u32(clock) > delay_ + 0x221u ? s16(u16((clock - 0x221 - delay_) * 0x1C)) : 0;
    }

    SpriteText text;
    u16 scale_y{};     // +02 (scale_x +00 is 100h, angle +04 0)
    u8 rotation_set{}; // +06
    u16 delay_;        // +22
};

// 0805D650: the title's band of colour (blank glyphs of slot 8): slides in
// from the right, rises to the top; it leaves left at the end.
class ResultsHeader final : public ResultsPiece {
public:
    ResultsHeader(const SpecialTally& t, u16 delay)
        : ResultsPiece(t, delay,
                       {.layer = 10,
                        .x = 0xF0,
                        .y = 0x7E,
                        .count = 8,
                        .slot = 8,
                        .palette = 0xE,
                        .single_row = true}) {}

    void update(Game& g) override {
        const s32 c = s16(clock);
        if (leaving()) {
            if (text.x < -0x20) {
                text.x = s16(text.x + 0x20);
                if (text.count != 0) {
                    --text.count;
                }
            }
            text.x = s16(text.x - 0x1A);
        } else if (c >= 0x32) {
            text.y = 0x11;
        } else if (c >= 0x22) {
            text.y = s16(text.y - 6);
        } else if (c >= 0x1A) {
            text.x = -0x10;
        } else if (c > 0xF) {
            text.x = s16(text.x - 0x1A);
            if (text.x < -0x10) {
                text.x = -0x10;
            }
        }
        static constexpr u8 blanks[] = "        ";
        draw_text(g, blanks, text, true);
    }
};

// 0805D6EC: the title (slot 2: "special stage", or the character's name
// and "got a chaos emerald" after the emerald's scene) scrolling through a
// band from the right, ten glyphs round.
class ResultsTitleBand final : public ResultsPiece {
public:
    ResultsTitleBand(const SpecialTally& t, u16 delay)
        : ResultsPiece(t, delay,
                       {.layer = 0,
                        .x = 0x100,
                        .y = 0x80,
                        .count = 9,
                        .slot = 2,
                        .palette = 0xF,
                        .single_row = true}) {}

    void update(Game& g) override {
        const s32 c = s16(clock);
        s16 before = text.x;
        if (leaving()) {
            text.x = s16(text.x - 0x1C);
            if (text.x > -0x20) {
                draw(g);
                return;
            }
            if (text.count != 0) {
                --text.count;
            }
            text.x = s16(text.x + 0x20);
        } else if (c < 0x1A) {
            if (c < 0x10) {
                return;
            }
            text.x = s16(before - 0x1C);
            if (text.x > 0xEF) {
                return;
            }
            text.count = s16(9 - (text.x >> 5));
            if (text.x < -0x1F) {
                text.x = s16(before + 4);
                next_glyph();
            }
            draw(g);
            return;
        } else {
            if (c >= 0x32) {
                text.y = 0x13;
            } else if (c >= 0x22) {
                text.y = s16(text.y - 6);
            } else {
                text.count = 9;
            }
            text.x = s16(before - 2);
            if (text.x > -0x20) {
                draw(g);
                return;
            }
            text.x = s16(before + 0x1E);
        }
        next_glyph();
        draw(g);
    }

private:
    void next_glyph() { glyph_ = u16(remainder(glyph_ + 1, 10)); }

    void draw(Game& g) {
        static constexpr u8 band[] = " !\"#$%&'() !\"#$%&'() !\"#$%&'()";
        draw_text(g, band + glyph_, text, true);
    }

    u16 glyph_{}; // +24
};

// A bonus line (0805D8B8 continues, 0805DAD0 the total, 0805DCDC the
// emerald, 0805DEF8 the rings): its label unfolds (rotation set 0-3) at
// its time, then its number (0805D7E8 / 0805D9B0 / 0805DBBC / 0805DDD8);
// at the end the label slides out left, the number with it.
class ResultsLine final : public ResultsPiece {
public:
    enum class Kind { continues, total, emerald, rings };

    ResultsLine(Game& g, const SpecialTally& t, u16 delay, Kind kind)
        : ResultsPiece(t, delay,
                       {.layer = layer_of(kind),
                        .x = 0x39,
                        .y = kind == Kind::continues ? s16(0x49) : s16(0x39),
                        .count = 3,
                        .slot = 3,
                        .palette = 0xE,
                        .single_row = true}),
          kind_(kind) {
        rotation_set = u8(kind);
        static constexpr u32 labels[] = {label_continue, label_total, label_emerald, label_rings};
        label_ = glyphs_at(g.assets(), labels[unsigned(kind)]);
        zero_ = glyphs_at(g.assets(), glyph_zero);
        if (kind == Kind::emerald) {
            value = tally.emerald_bonus;
        } else if (kind == Kind::rings) {
            value = tally.rings;
        }
    }

    // The emerald line: the bonus left (+18); the total: counted so far
    // (+1C); the rings line: rings left (+26).
    u32 value{};

    void update(Game& g) override {
        switch (kind_) {
        case Kind::continues: // 0805D8B8: only with continues won, after the count
            if (tally.continues == 0 || s32(s16(clock)) < delay_ + 0xAA) {
                return;
            }
            show_label(g, 0x81, 0x22);
            return;
        case Kind::total:
            show_label(g, 0x6D, 0x28);
            return;
        case Kind::emerald: // 0805DCDC: only with the emerald
            if (!tally.emerald) {
                return;
            }
            show_label(g, 0x5B, 0x2E);
            return;
        case Kind::rings:
            show_label(g, 0x49, 0x34);
            return;
        }
    }

private:
    static u16 layer_of(Kind k) { return u16((3 - unsigned(k)) * 8); }

    void draw_scaled(Game& g, const u8* chars) {
        draw_text_scaled(g, chars, text, 0x100, s16(scale_y), 0, rotation_set);
    }

    // The label at row `row`, unfolding from `start` for six frames.
    // (The continues line comes after the count, never in that window; its
    // copy of the window does not clamp the scale.)
    void show_label(Game& g, s16 row, s32 start) {
        const s32 c = s16(clock);
        text.count = 3;
        text.slot = 3;
        text.palette = 0xE;
        if (leaving()) {
            text.y = row;
            text.x = s16(text.x - 0x1C);
            if (text.x < -0x80) {
                text.count = 0;
                text.x = -0x80;
            } else {
                draw_text(g, label_.data(), text, true);
            }
        } else if (c < start + 6) {
            if (c < start) {
                return;
            }
            text.x = 0x19;
            text.y = s16(row + 10 - (scale_y >> 4));
            scale_y = u16(scale_y + 0x20);
            if (scale_y > 0xFF && kind_ != Kind::continues) {
                scale_y = 0x100;
            }
            draw_scaled(g, label_.data());
            show_number(g);
            return;
        } else {
            scale_y = u16(scale_y + 0x20);
            if (scale_y < 0x100) {
                text.x = 0x19;
                text.y = s16(row + 10 - (scale_y >> 4));
                draw_scaled(g, label_.data());
            } else {
                scale_y = 0x100;
                text.x = 0x29;
                text.y = row;
                draw_text(g, label_.data(), text, true);
            }
        }
        show_number(g);
    }

    // 0805C8CC: the number's seven digit glyphs (' ' + digit). The
    // continues use '+' + digit for the digits that are not 0.
    void compose(u32 v) {
        for (s32 i = 6; i >= 0; --i) {
            const u32 q = u32(divide(s32(v), 10));
            const u8 d = u8(v - q * 10);
            digits_[std::size_t(i)] = u8(d + ' ');
            if (kind_ == Kind::continues && v != q * 10) {
                digits_[std::size_t(i)] = u8(d + '+');
            }
            v = q;
        }
    }

    void show_number(Game& g) {
        if (u32(clock) > delay_ + 0x22Eu) {
            return;
        }
        const s16 out = leave_offset();
        const s16 x = text.x, y = text.y;
        compose(kind_ == Kind::continues ? tally.continues
                : kind_ == Kind::rings   ? value * 100
                                         : value);
        unsigned first = 0;
        while (digits_[first] == ' ' && ++first < 7) {
        }
        if (kind_ == Kind::continues) {
            // 0805D7E8: one glyph (slot 3).
            text.count = 1;
            if (scale_y == 0x100) {
                text.y = 0x7E;
                text.x = s16(0xB3 - out);
                draw_text(g, digits_.data() + first, text, true);
            } else {
                text.y = s16(0x8F - (scale_y >> 4) - divide(scale_y, 0x19));
                text.x = 0xA3;
                draw_scaled(g, digits_.data() + first);
            }
        } else {
            // 0805D9B0 / 0805DBBC / 0805DDD8: the digits (slot 6), right-aligned.
            const s16 row = kind_ == Kind::total ? 0x6A : kind_ == Kind::emerald ? 0x58 : 0x46;
            text.slot = 6;
            text.palette = 9;
            const bool zero = value == 0;
            if (scale_y == 0x100) {
                text.y = row;
                if (zero) {
                    text.count = 1;
                    text.x = s16(0xBB - out);
                    draw_text(g, zero_.data(), text, true);
                } else {
                    text.count = s16(7 - first);
                    text.x = s16(0xC3 - out - text.count * 8);
                    draw_text(g, digits_.data() + first, text, true);
                }
            } else {
                text.y = s16(row + 0xD - (scale_y >> 4) - divide(scale_y, 0x19));
                if (zero) {
                    text.count = 1;
                    text.x = 0xB7;
                    draw_scaled(g, zero_.data());
                } else {
                    text.count = s16(7 - first);
                    text.x = s16((6 - text.count) * 8 + 0x8F);
                    draw_scaled(g, digits_.data() + first);
                }
            }
        }
        text.x = x;
        text.y = y;
    }

    Kind kind_;
    std::array<u8, 4> label_{}, zero_{};
    std::array<u8, 7> digits_{}; // +2B
};

// 0805DFE4: the seven emeralds (slot 4; one not held is grey) roll in from
// the right one after another, and leave left at the end.
class ResultsEmeralds final : public ResultsPiece {
public:
    ResultsEmeralds(Game& g, const SpecialTally& t, u16 delay)
        : ResultsPiece(t, delay,
                       {.layer = 0,
                        .x = 0,
                        .y = 0x29,
                        .count = 1,
                        .slot = 4,
                        .palette = 0xD,
                        .single_row = true}) {
        const Assets& a = g.assets();
        held_even_ = glyphs_at(a, glyph_zero);
        held_odd_ = glyphs_at(a, glyph_emerald_odd);
        missing_ = glyphs_at(a, glyph_no_emerald);
    }

    void update(Game& g) override {
        const u16 c = clock;
        if (u32(c) > delay_ + 0x221u) {
            const s16 out = s16(u16((c - 0x221 - delay_) * 0x1C));
            for (unsigned i = 0; i < 7; ++i) {
                text.x = s16(i * 0x20 - (out - 5));
                if (text.x > -0x20) {
                    show(g, i);
                }
            }
        } else if (c < 0x87) {
            if (c <= 0x31) {
                return;
            }
            const Assets& a = g.assets();
            for (unsigned i = 0; i < 7; ++i) {
                u32 t = c - 0x31u;
                if (i * 5 > t) {
                    continue;
                }
                t = std::min<u32>(t - i * 5, 0x32);
                text.x = s16(i * 0x20 + divide(0x4000, 0x50) + 5 - divide(sine(a, t * 5), 0x50));
                show(g, i);
            }
        } else {
            for (unsigned i = 0; i < 7; ++i) {
                text.x = s16(i * 0x20 + 5);
                show(g, i);
            }
        }
    }

private:
    void show(Game& g, unsigned i) {
        if (!((tally.emeralds >> i) & 1)) {
            text.palette = 0xB;
            draw_text(g, missing_.data(), text, true);
            return;
        }
        text.palette = u16(0xD - (i >> 1));
        draw_text(g, (i & 1) ? held_odd_.data() : held_even_.data(), text, true);
    }

    std::array<u8, 4> held_even_{}, held_odd_{}, missing_{};
};

// 0805D4BC: the tally (priority 2100): gives the parts the clock; from AAh
// on it moves the rings (100 points each) and the emerald bonus into the
// total, with a tick every fourth frame; then back to the act.
class SpecialResults final : public Process {
public:
    SpecialResults(ResultsHeader& header, ResultsTitleBand& band, ResultsLine& continues,
                   ResultsLine& total, ResultsLine& emerald, ResultsLine& rings,
                   ResultsEmeralds& emeralds, u16 delay)
        : Process(0x2100), header_(header), band_(band), continues_(continues), total_(total),
          emerald_(emerald), rings_(rings), emeralds_(emeralds), delay_(delay) {}

    void update(Game& g) override {
        for (ResultsPiece* p : std::array<ResultsPiece*, 7>{&header_, &band_, &continues_, &total_,
                                                            &emerald_, &rings_, &emeralds_}) {
            p->clock = u16(clock_);
        }
        if (clock_ > 0xA9) {
            if (rings_.value != 0) {
                --rings_.value;
                total_.value += 100;
            }
            if (emerald_.value != 0) {
                emerald_.value -= 100;
                total_.value += 100;
            }
        }
        ++clock_;
        if (clock_ > delay_ + 0x267) {
            back_to_act(g);
            return;
        }
        if (clock_ == delay_ + 0xAA) {
            g.sound().start(continues_.tally.continues == 0 ? sound_counted : sound_continue);
        }
        if (clock_ > 0xAA && clock_ < delay_ + 0xAA && remainder(clock_, 4) == 0) {
            g.sound().start(sound_count);
        }
    }

private:
    // The act starts again 16 pixels up and left of the special spring (the
    // clock goes on), with the act's own restart point back.
    static void back_to_act(Game& g) {
        StageState& s = g.stage();
        Player& p = s.player[0];
        p.start_x = s16(p.start_x - 0x10);
        p.start_y = s16(p.start_y - 0x10);
        g.clear_screen_processes();
        const s32 clock = s.clock;
        restart_act(g);
        p.start_x = s.saved_start_x;
        p.start_y = s.saved_start_y;
        p.back_from_special = 1;
        s.clock = clock;
    }

    ResultsHeader& header_;     // +1C
    ResultsTitleBand& band_;    // +18
    ResultsLine& continues_;    // +0C
    ResultsLine& total_;        // +24
    ResultsLine& emerald_;      // +10
    ResultsLine& rings_;        // +14
    ResultsEmeralds& emeralds_; // +20
    s32 clock_{};               // +28
    s32 delay_;                 // +2A
};

// 0805C960: the results' pictures: slot 8 the header's band, slot 2 the
// title (picture 35h "special stage"; after the emerald's scene picture
// 33h and the character's name), slot 3 the labels (34h), slot 4 the
// emeralds (36h) with the colours of slot 5 (38h-3Ah, colours only), slot
// 6 the digits (the font's from 400h).
void load_results_pictures(Game& g, bool after_scene) {
    const Assets& a = g.assets();
    GraphicsLoad l;
    l.options = 0x10;
    l.slot = 8;
    l.tiles_from = 0x0865F078;
    l.colors_from = 0x0865F278;
    l.tile_bytes = 0x200;
    l.color_bytes = 0x20;
    l.palette = 0xE;
    l.tiles_to = 0x177A0;
    l.parts = load::tiles | load::colors | load::sprite;
    l.key = 0x10;
    l.extra = {2, 0, 4, 4};
    load_graphics(g, l);
    const auto picture = [&](u8 index, u8 slot, u32 to, u32 bytes, u32 colors, u8 palette,
                             u8 parts) {
        take_picture(a, l, index);
        l.slot = slot;
        l.tiles_to = to;
        l.tile_bytes = bytes;
        l.color_bytes = colors;
        l.palette = palette;
        l.parts = parts;
        load_graphics(g, l);
    };
    constexpr u8 all = load::tiles | load::colors | load::sprite,
                 no_tiles = load::colors | load::sprite;
    if (!after_scene) {
        picture(0x35, 2, 0x163A0, 0x1400, 0x20, 0xF, all);
    } else {
        picture(0x33, 2, 0x169A0, 0xE00, 0x20, 0xF, all);
        picture(u8(g.stage().character + 0xB), 2, 0x163A0, 0x600, 0xC, 0xF, all);
    }
    picture(0x34, 3, 0x152A0, 0x1100, 0x20, 0xE, all);
    picture(0x36, 4, 0x14CA0, 0x600, 0x20, 0xD, all);
    picture(0x38, 5, 0x14CA0, 0x20, 0x20, 0xC, no_tiles);
    picture(0x39, 5, 0x14CA0, 0x20, 0x20, 0xB, no_tiles);
    picture(0x3A, 5, 0x14CA0, 0x20, 0x20, 0xA, no_tiles);
    take_picture(a, l, 0);
    l.tiles_from += 0x400;
    l.slot = 6;
    l.tiles_to = 0x14A20;
    l.tile_bytes = 0x280;
    l.color_bytes = 0x20;
    l.palette = 9;
    l.parts = all;
    load_graphics(g, l);
}

// 0805D014: the bonuses go into the score, the emerald into the save, the
// continues won (capsule items, or an emerald held already) are added.
void start_results_screen(Game& g, bool after_scene) {
    ensure_screen_graphics(g);
    StageState& s = g.stage();
    auto& v = g.video().settings;
    for (auto& bg : v.bg) {
        bg.scroll_x = bg.scroll_y = 0;
    }
    SpecialTally t;
    t.rings = s.special.rings;
    u8 already = 0;
    if (s.special.rings < s.special.shown_target) {
        g.sound().start(music_failed);
    } else {
        g.sound().start(music_got_emerald);
        t.emerald = true;
        t.emerald_bonus = emerald_bonus;
        const u8 bit = u8(1u << g.assets().u8(stage_of_act + u32(s32(s.zone))));
        if (g.save().emeralds & bit) {
            already = 1;
        } else {
            g.save().emeralds |= bit;
        }
    }
    t.emeralds = g.save().emeralds;
    t.continues = u8(s.special.items + already);
    g.continues = s8(g.continues + t.continues);
    const u32 before = s.score;
    s.score += t.emerald_bonus + t.rings * 100u;
    if (divide(s32(s.score), 50000) != divide(s32(before), 50000) && g.game_mode == 0) {
        ++s.lives;
    }
    const u16 delay =
        t.rings * 100u < t.emerald_bonus ? u16(divide(s32(t.emerald_bonus), 100)) : t.rings;
    g.screen_graphics->reset_pool();
    auto& sch = g.scheduler();
    auto& header = sch.create<ResultsHeader>(t, delay);
    auto& band = sch.create<ResultsTitleBand>(t, delay);
    auto& continues = sch.create<ResultsLine>(g, t, delay, ResultsLine::Kind::continues);
    auto& total = sch.create<ResultsLine>(g, t, delay, ResultsLine::Kind::total);
    auto& emerald = sch.create<ResultsLine>(g, t, delay, ResultsLine::Kind::emerald);
    auto& rings = sch.create<ResultsLine>(g, t, delay, ResultsLine::Kind::rings);
    auto& emeralds = sch.create<ResultsEmeralds>(g, t, delay);
    sch.create<SpecialResults>(header, band, continues, total, emerald, rings, emeralds, delay);
    // The layers' maps are cleared (rows 0-20h).
    auto& q = g.backgrounds();
    for (unsigned i = 0; i < 4; ++i) {
        q.clear_value[i] = 0;
        q.clears[i] = {0, 0, 0xFF, 0x20};
    }
    load_results_pictures(g, after_scene);
}

// ---------------------------------------------------------------------------
// The emerald's scene (08068CD8): in the sky the biplane flies in, the
// hero drops onto it and holds up the emerald; the view whitens and the
// results follow.

constexpr u32 scene_hero_falling = 0x086844A0; // per character: animation, variant
constexpr u32 scene_hero_holding = 0x08684530; // per character: animation, variant
constexpr u32 scene_plane = 0x086844B0;        // per character: animation, variant (the biplane)
constexpr u32 scene_emeralds = 0x086844C0;     // per stage, per character: animation, variant
constexpr u16 scene_pilot = 0x2E0, scene_glint = 0x2FD, scene_sparkles = 0x316;
constexpr u16 music_emerald = 0x2D;

// A picture of the scene: x (+40) and y (+38) in 8.8.
class ScenePicture : public Process {
public:
    ScenePicture(Game& g, u32 tiles, u16 animation, u8 variant, s16 x, s16 y, u16 layer, bool start)
        : Process(0x2120) {
        sprite.start(tiles, animation, variant, x, y, layer, 0x1000);
        if (start) {
            animate(g, sprite);
        }
    }

    Sprite sprite; // +00
    s32 y{};       // +38
    s32 x{};       // +40

protected:
    void show(Game& g) {
        sprite.x = s16(x >> 8);
        sprite.y = s16(y >> 8);
        animate(g, sprite);
        draw(g, sprite);
    }

    // 08069568: one pixel left a frame, to -64h.
    void drift_left() {
        x -= 0x100;
        if (x < -0x6400) {
            x = -0x6400;
        }
    }
};

// 08069568: the biplane, its pilot and the emerald.
class SceneDrifter final : public ScenePicture {
public:
    using ScenePicture::ScenePicture;

    void update(Game& g) override {
        drift_left();
        show(g);
    }
};

// 0806832C: a sparkle crossing the sky, round again at the edges.
class SceneSparkle final : public ScenePicture {
public:
    SceneSparkle(Game& g, unsigned i)
        : ScenePicture(g, tiles_of[i], u16(scene_sparkles + i), 0, 0xB4, 0x50, 0x6C0, false) {
        x_ = starts[i].x;
        y_ = starts[i].y;
        speed_ = starts[i].speed;
    }

    void update(Game& g) override {
        x_ += speed_;
        if (x_ > 0x11000) {
            x_ = -0x7800;
        }
        if (x_ < -0x7800) {
            x_ = 0x11000;
        }
        // (No vertical speed is ever set.)
        if (y_ > 0xAC00) {
            y_ = -0x1000;
        }
        if (y_ < -0x1000) {
            y_ = 0xAC00;
        }
        x = x_;
        y = y_;
        show(g);
    }

private:
    static constexpr u32 tiles_of[4] = {0x16040, 0x16180, 0x16420, 0x17080};

    static constexpr struct {
        s32 x, y, speed;
    } starts[4] = {{0x5000, 0x3200, 0x80},
                   {0x2800, 0x6400, 0x40},
                   {0x7800, 0x7800, 0x100},
                   {0xA000, 0x1400, 0x120}};

    s32 x_{};     // +44
    s32 speed_{}; // +48
    s32 y_{};     // +54
};

// 08069518 (nothing), 0806951C (drops from above onto the plane, 50h),
// 0806939C (holds the emerald up, flying left with the plane).
class SceneHero final : public ScenePicture {
public:
    enum class Mode { waiting, falling, holding };

    explicit SceneHero(Game& g)
        : ScenePicture(g, 0x10020, g.assets().u16(scene_hero_falling + g.stage().character * 4u),
                       u8(g.assets().u16(scene_hero_falling + g.stage().character * 4u + 2)), 0x78,
                       0xB4, 0x180, true) {
        x = 0xAA00;
        y = -0x2000;
    }

    Mode mode = Mode::waiting;
    u32 time{}; // +30

    void update(Game& g) override {
        switch (mode) {
        case Mode::waiting:
            return;
        case Mode::falling:
            y += 0x4A0;
            if (y > 0x5000) {
                y = 0x5000;
            }
            show(g);
            return;
        case Mode::holding:
            if (time == 0) {
                const Assets& a = g.assets();
                const u8 ch = g.stage().character;
                sprite.animation = a.u16(scene_hero_holding + ch * 4u);
                sprite.variant = u8(a.u16(scene_hero_holding + ch * 4u + 2));
                sprite.previous_variant = 0xFF;
                y += 0x1000;
            }
            drift_left();
            show(g);
            return;
        }
    }
};

// 08069600: the emerald's glint, drawn as it is and mirrored, until its
// animation ends.
class SceneGlint final : public ScenePicture {
public:
    using ScenePicture::ScenePicture;

    void update(Game& g) override {
        sprite.x = s16(x >> 8);
        sprite.y = s16(y >> 8);
        if (animate(g, sprite) == 0) {
            g.scheduler().destroy(*this);
            return;
        }
        sprite.flags &= ~0x400u;
        draw(g, sprite);
        sprite.flags |= 0x400;
        draw(g, sprite);
    }
};

// 080694B4 / 080690CC (priority 2100): the fade in; at 8Ch the hero falls
// onto the plane; at A2h the jingle, the hero holds up the emerald and
// everything rises a little; then the fade to white and the results.
class SceneDirector final : public Process {
public:
    SceneDirector(SceneHero& hero, SceneDrifter& plane, SceneDrifter& pilot)
        : Process(0x2100), hero_(hero), plane_(plane), pilot_(pilot) {
        fade_ = {0, 1, 8, 0, 0x128, 1};
    }

    ScreenFade fade_; // +00

    void update(Game& g) override {
        const u32 t = u32(time_ + 1);
        auto& vs = g.video();
        if (!holding_) {
            // 080694B4
            step_fade(vs, fade_);
            if (t == 0x8C) {
                hero_.mode = SceneHero::Mode::falling;
            }
            if (t < 0xA2) {
                time_ = s16(t);
                hero_.time = t;
                return;
            }
            g.sound().start(music_emerald);
            time_ = 0;
            hero_.time = 0;
            hero_.mode = SceneHero::Mode::holding;
            holding_ = true;
            return;
        }
        // 080690CC
        if (t == 1) {
            const Assets& a = g.assets();
            const StageState& s = g.stage();
            auto& sch = g.scheduler();
            SceneGlint& glint =
                sch.create<SceneGlint>(g, 0x13820, scene_glint, 0, 0x78, 0xB4, 0, true);
            glint.x = hero_.x - 0x800;
            glint.y = hero_.y + 0xA00;
            const u32 e =
                scene_emeralds + s.character * 4u + a.u8(stage_of_act + u32(s32(s.zone))) * 0x10u;
            emerald_ = &sch.create<SceneDrifter>(g, 0x10AA0, a.u16(e), u8(a.u16(e + 2)), 0x78, 0xB4,
                                                 0x180, true);
            emerald_->x = hero_.x;
            emerald_->y = hero_.y;
        }
        if (t > 7 && pilot_.y >= 0x6400) {
            for (ScenePicture* p :
                 std::array<ScenePicture*, 4>{&pilot_, &plane_, &hero_, emerald_}) {
                p->y -= 0x100;
            }
        }
        if (t == 1) {
            for (ScenePicture* p :
                 std::array<ScenePicture*, 4>{&pilot_, &plane_, &hero_, emerald_}) {
                p->y += 0xA00;
            }
        }
        if (step_fade(vs, fade_) && fade_.kind == 4) {
            if (whitened_ > 10) {
                g.clear_screen_processes();
                start_results_screen(g, true);
                return;
            }
            ++whitened_;
        }
        if (t == u32(300 - divide(0x2000, 0x128))) {
            fade_ = {0, 1, 4, 0, 0x128, 1};
        }
        time_ = s16(t);
        hero_.time = t;
    }

private:
    SceneHero& hero_;         // +0C
    SceneDrifter& plane_;     // +18
    SceneDrifter& pilot_;     // +14
    SceneDrifter* emerald_{}; // +10
    s16 time_{};              // +1C
    u8 whitened_{};           // +24 frames since the fade to white ended
    bool holding_{};
};

void start_emerald_scene(Game& g) {
    ensure_screen_graphics(g);
    g.screen_graphics->reset_pool();
    auto& vs = g.video();
    auto& v = vs.settings;
    v.set_control(0x1140);
    v.bg[0].set_control(0x9D86);
    v.bg[0].scroll_x = 0;
    v.bg[0].scroll_y = 0x128;
    const Assets& a = g.assets();
    const u8 ch = g.stage().character;
    auto& sch = g.scheduler();
    SceneHero& hero = sch.create<SceneHero>(g);
    SceneDrifter& plane =
        sch.create<SceneDrifter>(g, 0x10820, a.u16(scene_plane + ch * 4u),
                                 u8(a.u16(scene_plane + ch * 4u + 2)), 300, 100, 0x240, true);
    plane.x = 0x15E00;
    plane.y = 0x6400;
    SceneDrifter& pilot =
        sch.create<SceneDrifter>(g, 0x112A0, scene_pilot, 0, 0x15E, 100, 0x300, true);
    pilot.x = 0x15E00;
    pilot.y = 0x6400;
    for (unsigned i = 0; i < 4; ++i) {
        sch.create<SceneSparkle>(g, i);
    }
    SceneDirector& director = sch.create<SceneDirector>(hero, plane, pilot);
    step_fade(vs, director.fade_);
    // The sky (as in the welcome scene).
    GraphicsLoad l;
    l.options = 0x80;
    l.slot = 0;
    l.parts = load::tiles | load::colors | load::map;
    l.layer = 0;
    l.palette = 0;
    l.tiles_from = 0x086B4D10;
    l.tile_bytes = 0x2980;
    l.colors_from = 0x086B4B10;
    l.color_bytes = 0x200;
    l.map_from = 0x086B7690;
    l.map_bytes = 0x1000;
    load_graphics(g, l);
}

void start_special_results(Game& g, bool emerald) {
    if (emerald) {
        start_emerald_scene(g);
    } else {
        start_results_screen(g, false);
    }
}

} // namespace

void start_special_stage_entry(Game& g) {
    ensure_screen_graphics(g);
    g.screen_graphics->reset_pool();
    auto& vs = g.video();
    auto& v = vs.settings;
    v.set_control(0x1140);
    v.bg[0].set_control(0x9D86);
    v.bg[0].scroll_x = 0;
    v.bg[0].scroll_y = 0x128;
    auto& sch = g.scheduler();
    EntryHero& hero = sch.create<EntryHero>(g);
    EntryStreaks& streaks = sch.create<EntryStreaks>(g);
    EntryDirector& director = sch.create<EntryDirector>(hero, streaks);
    apply_fade(vs, director.fade_);
    g.sound().start(music_welcome);
}

void start_special_stage(Game& g) {
    StageState& s = g.stage();
    const Assets& a = g.assets();
    const u8 stage = a.u8(stage_of_act + u32(s32(s.zone)));
    auto& counts = s.special;
    counts.quiet = 0;
    counts.items = 0;
    counts.rings = 0;
    counts.target = a.u16(ring_targets + stage * 4u);
    counts.second_target = a.u16(ring_targets + stage * 4u + 2);
    s.player[0].power &= u8(~4);
    counts.shown_target = counts.target;
    set_button_layout(g, g.save().button_layout);
    auto& sch = g.scheduler();
    // 0802D128
    SpecialCourse& course = sch.create<SpecialCourse>(stage);
    SpecialRun& r = course.run;
    sch.create<SpecialPauseWatch>();
    // 0802D160
    sch.create<SpecialHero>(g, r);
    // 0802D1A8
    sch.create<Spawner>(r);
    // 08029B44: the display and the tube's layers.
    auto& v = g.video().settings;
    v.set_control(0x1C40);
    v.bg[1].set_control(0x0201);
    v.bg[2].set_control(0x5806);
    v.bg[3].set_control(0x1C0B);
    v.bg[3].scroll_x = 0x11;
    v.bg[3].scroll_y = 4;
    v.bg[2].scroll_x = 0x88;
    v.bg[2].scroll_y = 0x20;
    for (unsigned i = 0; i < 5; ++i) {
        r.layers[i].set_from_template(a, track_layers[i]);
    }
    for (auto& layer : r.layers) {
        load_background(a, g.video(), g.backgrounds(), layer);
    }
    sch.create<Tube>(r);
    create_color_cycle(g, 0x2000, u16(stage + 0x340), 0);
    create_color_cycle(g, 0x2000, u16(stage + 0x340), 1);
    // 0802C904
    sch.create<RingManager>(g, r);
    start_card(g, true);
}

} // namespace sonic_advance_gba
