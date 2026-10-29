// The ending after act 12 (08065850-08067078, 080683A4-080684B8), the staff
// roll (0805E1B4-0805E97C) and the last picture (08069658-0806995C).

#include "sonic_advance_gba/game/ending.hpp"
#include "sonic_advance_gba/game/fade.hpp"
#include "sonic_advance_gba/game/game.hpp"
#include "sonic_advance_gba/game/graphics.hpp"
#include "sonic_advance_gba/game/hud.hpp"
#include "sonic_advance_gba/game/math.hpp"
#include "sonic_advance_gba/game/results.hpp"
#include "sonic_advance_gba/game/stage.hpp"
#include "sonic_advance_gba/game/video.hpp"
#include <algorithm>
#include <array>

namespace sonic_advance_gba {

namespace {

// Per character (4 bytes each: animation, variant).
constexpr u32 falling_animations = 0x086842D4;  // the hero falling
constexpr u32 backdrop_animations = 0x086842E4; // the close-up picture
constexpr u32 pilot_animations = 0x086842F4;    // the pilot (and the hero on the wing)
constexpr u32 pilot_turn_animations = 0x08684304;
constexpr u32 catch_animations[3] = {0x08684314, 0x08684324, 0x08684334};
constexpr u32 riding_animations = 0x08684344;
constexpr u32 jump_animations[3] = {0x08684354, 0x08684364, 0x08684374};
constexpr u32 jump_offsets[2] = {0x08684384, 0x0868438C}; // per character: s8 (x, y nudges)
constexpr u32 thumbs_up_animations = 0x086843A4;
constexpr u32 flock_places = 0x08684394;   // 8 birds: s8 x, y
constexpr u32 message_stops = 0x086843B4;  // 3 lines: s16 last y
constexpr u32 message_starts = 0x086843BA; // 3 lines: s16 first frame

constexpr u16 music_ending = 0x26;
constexpr u16 music_staff_roll = 0x28;

void set_animation(const Assets& a, Sprite& s, u32 table, u8 character) {
    s.animation = a.u16(table + character * 4u);
    s.variant = u8(a.u16(table + character * 4u + 2));
    s.previous_variant = 0xFF;
}

// s16 sine >> 8: the bobbing of the riders, a quarter turn every 64 frames.
s32 bob(const Assets& a, u32 t) {
    return sine(a, (t & 0xFF) * 4) >> 8;
}

// The cut scene's sprites (0x164-byte records at 2120h): a picture with a
// position (8.8) and a speed, each moved by its current routine; the
// director hands every one its time (+3C) each frame.
class Actor : public Process {
public:
    Actor(Game& g, u32 tiles, u16 animation, u8 variant, s16 x, s16 y, u16 layer, bool first_frame)
        : Process(0x2120), game_(g) {
        sprite.start(tiles, animation, variant, x, y, layer, 0x1000);
        if (first_frame) {
            animate(g, sprite);
        }
    }

    Sprite sprite; // +00
    u16 time{};    // +3C
    s32 x{};       // +40
    s32 speed{};   // +44
    s32 y{};       // +48
    s32 extra{};   // +4C

protected:
    void show() {
        animate(game_, sprite);
        draw(game_, sprite);
    }

    Game& game_;
};

// The hero: falls from space, is caught on the plane's wing, rides it,
// jumps up and grows as the picture closes.
class Hero final : public Actor {
public:
    using Actor::Actor;
    enum class Move { fall, caught, ride, bob, finale } move = Move::fall;

    void update(Game& g) override {
        const Assets& a = g.assets();
        const u8 ch = g.stage().character;
        switch (move) {
        case Move::fall:
            show();
            break;           // 08066D9C
        case Move::caught: { // 080665F4
            if (time == 0xF || time == 0x14 || time == 0x1E) {
                set_animation(a, sprite,
                              catch_animations[time == 0xF    ? 0
                                               : time == 0x14 ? 1
                                                              : 2],
                              ch);
            }
            if (time > 0x14) {
                y = std::max(y - 0x800, -0x4000);
                sprite.y = s16(y >> 8);
                x = std::max(x - speed, 0);
                sprite.x = s16(x >> 8);
            }
            show();
            break;
        }
        case Move::ride: // 08066DC0
            x += speed;
            sprite.x = s16((x >> 8) - 0x14);
            y += 0x800;
            sprite.y = s16(y >> 8);
            show();
            break;
        case Move::bob: // 080666AC
            if (time == 300) {
                set_animation(a, sprite, riding_animations, ch);
            }
            y += bob(a, time);
            sprite.y = s16(y >> 8);
            show();
            break;
        case Move::finale:
            finale(g);
            break;
        }
    }

private:
    // 08066734: waves, jumps off (from F0h), and from 300 on grows (to
    // 1.5 times) while rising out of the picture.
    void finale(Game& g) {
        const Assets& a = g.assets();
        const u8 ch = g.stage().character;
        const u32 t = time;
        if (t == 1) {
            set_animation(a, sprite, jump_animations[0], ch);
        } else if (t == 0xF0) {
            set_animation(a, sprite, jump_animations[1], ch);
            extra = -0x400;
        }
        s32 dy;
        if (t < 300) {
            dy = bob(a, t);
        } else {
            if (t == 300) {
                set_animation(a, sprite, jump_animations[2], ch);
                turn_ = 0;
                scale_x_ = scale_y_ = s16(divide(0x700, 10));
                centre_x_ = sprite.x;
                centre_y_ = sprite.y;
            }
            extra += 0x1E;
            dy = extra;
        }
        y += dy;
        sprite.y = s16(y >> 8);
        if (t < 300) {
            sprite.flags = 0x1000;
        } else {
            if (t == 300) {
                const s8 n = a.s8(jump_offsets[0] + ch * 2u);
                centre_x_ = s16(centre_x_ - n);
                centre_y_ = s16(sprite.y - n);
            } else if (t == 0x12D) {
                const s8 n = a.s8(jump_offsets[1] + ch * 2u);
                centre_x_ = s16(centre_x_ + n + 1);
                centre_y_ = s16(sprite.y + n);
            } else {
                centre_x_ = s16(centre_x_ + (sine(a, 0x20 + (t - 300) * 12) >> 12));
                centre_y_ = sprite.y;
            }
            scale_x_ = std::min<s16>(s16(scale_x_ + 4), 0x180);
            scale_y_ = std::min<s16>(s16(scale_y_ + 4), 0x180);
            sprite.flags = 0x1061; // rotation set 1, double size
            transform(g, sprite, turn_, scale_x_, scale_y_, centre_x_, centre_y_);
        }
        animate(g, sprite);
        if (t < 0x169) {
            draw(g, sprite);
        }
    }

    u16 turn_{};                  // +30
    s16 scale_x_{}, scale_y_{};   // +32, +34
    s16 centre_x_{}, centre_y_{}; // +36, +38
};

// The pilot and the plane (Tornado): fly in from the right, pass under
// the falling hero, carry it.
class Flyer final : public Actor {
public:
    Flyer(Game& g, u32 tiles, u16 animation, u8 variant, s16 x, s16 y, u16 layer, bool pilot)
        : Actor(g, tiles, animation, variant, x, y, layer, true), pilot_(pilot) {}
    enum class Move { wait, arrive, pass, bob } move = Move::wait;
    bool turn_next{}; // +50 (the pilot): a new animation at the next cue

    void update(Game& g) override {
        const Assets& a = g.assets();
        switch (move) {
        case Move::wait: // 08066E24 (the pilot) / 08066EE0 (the plane)
            if (!pilot_) {
                animate(g, sprite);
            }
            break;
        case Move::arrive: // 08066E28 / 08066EFC
            x = std::max(x - speed, 0);
            sprite.x = s16(x >> 8);
            if (time > 0x14) {
                y = std::max(y - 0x800, -0x1E00);
                sprite.y = s16(y >> 8);
            }
            show();
            break;
        case Move::pass: // 08066E88 / 08066F5C
            x += speed;
            if (x < 0) {
                x = 0;
            }
            sprite.x = s16(x >> 8);
            y += 0x800;
            if (pilot_ && y < -0x1E00) {
                y = -0x1E00;
            }
            sprite.y = s16(y >> 8);
            show();
            break;
        case Move::bob: // 0806696C / 08066FA8
            if (pilot_ && turn_next && (time == 300 || time == 1)) {
                set_animation(a, sprite, time == 300 ? pilot_turn_animations : pilot_animations,
                              g.stage().character);
                turn_next = false;
            }
            y += bob(a, time);
            sprite.y = s16(y >> 8);
            show();
            break;
        }
    }

private:
    bool pilot_;
};

// The flock of eight birds, bobbing on a wave; flies in from the right,
// then stays.
class Flock final : public Actor {
public:
    using Actor::Actor;
    enum class Move { wait, fly, stay } move = Move::wait;

    void update(Game& g) override {
        const Assets& a = g.assets();
        // 08066A28: the wave's next value.
        if (++index_ > 0x3F) {
            index_ = 0;
        }
        const s16 w = s16(sine(a, u32(remainder(time, 0x40)) * 8) >> 10);
        wave_a_[std::size_t(index_)] = w;
        wave_[std::size_t(index_)] = w;
        if (move == Move::wait) {
            return;
        }
        // 08066A98 / 08066B70
        animate(g, sprite);
        for (u32 i = 0; i < 8; ++i) {
            const s16 bx =
                s16(a.s8(flock_places + i * 2) + (move == Move::fly ? 0xF8 - s16(time) : 0xBC));
            sprite.x = bx;
            sprite.y =
                s16(a.s8(flock_places + i * 2 + 1) + wave_[(i * 8 + u8(index_)) & 0x3F] + 0x50);
            draw(g, sprite);
        }
    }

private:
    std::array<s16, 64> wave_a_{}; // +50 (written, not read)
    std::array<s16, 64> wave_{};   // +D6
    s8 index_{};                   // +15C
};

// The close-up picture and the last big picture of the hero; first only
// the clouds' layer moves (up a line every other frame).
class Backdrop final : public Actor {
public:
    using Actor::Actor;
    enum class Move { clouds, none, close_up, thumbs_up } move = Move::clouds;

    void update(Game& g) override {
        switch (move) {
        case Move::clouds: // 08066FEC
            if (remainder(time, 2) == 0) {
                ++g.video().settings.bg[0].scroll_y;
            }
            break;
        case Move::none:
            break;           // 0806701C
        case Move::close_up: // 08067020: held at its first frame
            sprite.previous_variant = 0xFF;
            show();
            break;
        case Move::thumbs_up: // 08067050
            if (time > 0x167) {
                if (time == 0x168) {
                    set_animation(g.assets(), sprite, thumbs_up_animations, g.stage().character);
                }
                show();
            }
            break;
        }
    }
};

// 08066C44: fifteen streaks rushing up past the falling hero, each placed
// anew every 16 frames.
class Streaks final : public Actor {
public:
    using Actor::Actor;

    void update(Game& g) override {
        StageState& s = g.stage();
        ++time;
        sprite.previous_variant = 0xFF;
        animate(g, sprite);
        for (u32 i = 0; i < 15; ++i) {
            if (((time + i) & 0xF) == 0) {
                y_[i] = s16(remainder(s32(s.next_random()), 0x28) + 0x8C);
                x_[i] = s16(remainder(s32(s.next_random()), 0xDC) + 0x14);
            } else {
                y_[i] = s16(y_[i] - 0x10);
            }
            sprite.x = x_[i];
            sprite.y = y_[i];
            draw(g, sprite);
        }
    }

private:
    std::array<s16, 15> x_{}; // +50
    std::array<s16, 15> y_{}; // +D6
};

// The three lines of the message ("congratulations", "thank you", "for
// playing"), each rising into place from its own frame on.
class MessageLine final : public Actor {
public:
    // `at_once` (the true ending): rising from the start, its first frame
    // taken at once.
    MessageLine(Game& g, u32 tiles, u8 line, bool at_once = false)
        : Actor(g, tiles, 0x311, line, 0xB4, 0x50, 0, at_once), rising(at_once), line_(line) {}

    bool rising;

    void update(Game& g) override {
        if (!rising) {
            return; // 08066D94
        }
        // 08066D30
        const Assets& a = g.assets();
        const s16 start = a.s16(message_starts + line_ * 2u);
        if (u32(s32(start)) < time) {
            sprite.x = 0x78;
            sprite.y = s16((time - start) * -3 + 0xB4);
            sprite.y = std::max(sprite.y, a.s16(message_stops + line_ * 2u));
            show();
        }
    }

private:
    u8 line_; // +44
};

// The clouds (100-byte records): drift up past the fall, leave, rush by
// diagonally, then wrap around the view.
class Cloud final : public Process {
public:
    Cloud(u32 tiles, u16 animation, u16 layer, s32 x, s32 speed_x, s32 speed_y)
        : Process(0x2120), x(x), speed_x(speed_x), speed_y(speed_y) {
        sprite.start(tiles, animation, 0, 0xB4, 0x50, layer, 0x1000);
    }
    enum class Move { drift, leave, rush, wrap } move = Move::drift;

    void update(Game& g) override {
        switch (move) {
        case Move::drift: // 08068418
            y += speed_y;
            if (y < -0x1000) {
                y = 0xAC00;
                x += speed_x * 200;
                if (x > 0x11000) {
                    x -= 0x12C00;
                }
            }
            break;
        case Move::leave: // 08068480
            if (y < -0x1000) {
                return;
            }
            y += speed_y;
            break;
        case Move::rush: // 080683A4
            y += 0x800;
            x += 0x800 + speed_x;
            if (x > 0x11000) {
                x -= 0x12C00;
            }
            y += speed_y;
            if (y > 0xAC00) {
                y -= 0x12C00;
            }
            break;
        case Move::wrap: // 0806832C
            x += speed_x;
            if (x > 0x11000) {
                x = -0x7800;
            }
            if (x < -0x7800) {
                x = 0x11000;
            }
            y += speed_y;
            if (y > 0xAC00) {
                y = -0x1000;
            }
            if (y < -0x1000) {
                y = 0xAC00;
            }
            break;
        }
        sprite.x = s16(x >> 8);
        sprite.y = s16(y >> 8);
        animate(g, sprite);
        draw(g, sprite);
    }

    Sprite sprite;  // +00
    s32 x;          // +44
    s32 speed_x;    // +48
    s32 y = 0xC800; // +54
    s32 speed_y;    // +58
};

// 08065F28 and its stages: the scene's timeline. Each stage counts its own
// frames and hands the time to the sprites.
class EndingDirector final : public Process {
public:
    EndingDirector(Hero& hero, Flyer& pilot, Flyer& plane, Flock& flock, Backdrop& backdrop,
                   Streaks& streaks, std::array<MessageLine*, 3> lines)
        : Process(0x2100), hero_(hero), pilot_(pilot), plane_(plane), flock_(flock),
          backdrop_(backdrop), streaks_(streaks), lines_(lines) {
        fade_ = {0, 1, 8, 0, 0x80, 1};
    }

    ScreenFade fade_; // +00

    void update(Game& g) override {
        u16 t = u16(time_ + 1);
        switch (stage_) {
        case 0: // 08065F28: the fall; the clouds come up
            step_fade(g.video(), fade_);
            if (t == 0xB4) {
                static constexpr struct {
                    s32 x, speed_x, speed_y;
                    u32 tiles;
                    u16 animation, layer;
                } kinds[4] = {
                    {0x3C00, 0x80, -0x3C0, 0x12E80, 0x316, 0x4C0},
                    {0, 0x40, -0x400, 0x12FC0, 0x317, 0x480},
                    {0x7800, 0x100, -0x380, 0x13260, 0x318, 0x440},
                    {0xB400, 0xC0, -0x480, 0x13620, 0x319, 0x400},
                };

                for (unsigned i = 0; i < 4; ++i) {
                    const auto& k = kinds[i];
                    clouds_[i] = &g.scheduler().create<Cloud>(k.tiles, k.animation, k.layer, k.x,
                                                              k.speed_x, k.speed_y);
                }
            }
            if (t == 0x168) {
                for (Cloud* c : clouds_) {
                    c->move = Cloud::Move::leave;
                }
            }
            if (t > 400) {
                t = 0;
                g.scheduler().destroy(streaks_);
                pilot_.move = Flyer::Move::arrive;
                plane_.move = Flyer::Move::arrive;
                hero_.move = Hero::Move::caught;
                backdrop_.move = Backdrop::Move::none;
                stage_ = 1;
            }
            time_ = backdrop_.time = hero_.time = plane_.time = pilot_.time = t;
            return;
        case 1: // 08066174: the catch; the clouds rush by
            if (t == 0xD0) {
                static constexpr struct {
                    s32 x, y, speed_x;
                } places[4] = {{-0x1E00, -0x7800, 0x80},
                               {-0x3C00, -0x5A00, 0x140},
                               {-0x5A00, -0x3C00, 0x100},
                               {-0x7800, -0x1E00, 0xC0}};

                for (unsigned i = 0; i < 4; ++i) {
                    clouds_[i]->x = places[i].x;
                    clouds_[i]->y = places[i].y;
                    clouds_[i]->speed_x = places[i].speed_x;
                    clouds_[i]->speed_y = 0;
                    clouds_[i]->move = Cloud::Move::rush;
                }
            }
            if (t > 0x118) {
                t = 0;
                pilot_.move = Flyer::Move::pass;
                plane_.move = Flyer::Move::pass;
                hero_.move = Hero::Move::ride;
                stage_ = 2;
            }
            time_ = hero_.time = plane_.time = pilot_.time = t;
            return;
        case 2: // 0806629C
            if (t > 0x10) {
                t = 0;
                for (Cloud* c : clouds_) {
                    c->move = Cloud::Move::wrap;
                }
                pilot_.move = Flyer::Move::bob;
                plane_.move = Flyer::Move::bob;
                hero_.move = Hero::Move::bob;
                stage_ = 3;
            }
            time_ = hero_.time = plane_.time = pilot_.time = t;
            return;
        case 3: // 08066338: riding; the birds come
            if (t > 400) {
                t = 0;
                flock_.move = Flock::Move::fly;
                stage_ = 4;
            }
            if (t == 300) {
                pilot_.turn_next = true;
            }
            time_ = hero_.time = plane_.time = pilot_.time = t;
            return;
        case 4: // 080663B8
            if (t == 0x3C) {
                flock_.move = Flock::Move::stay;
            }
            if (t > 200) {
                t = 0;
                backdrop_.move = Backdrop::Move::close_up;
                stage_ = 5;
            }
            time_ = t;
            hero_.time = u16(t + 0x3FF);
            flock_.time = plane_.time = pilot_.time = t;
            return;
        case 5: // 08066444: the close-up
            if (t > 0xF0) {
                t = 0;
                for (MessageLine* l : lines_) {
                    l->rising = true;
                }
                hero_.move = Hero::Move::finale;
                backdrop_.move = Backdrop::Move::thumbs_up;
                stage_ = 6;
            }
            if (t == 1) {
                pilot_.turn_next = true;
            }
            time_ = hero_.time = flock_.time = plane_.time = pilot_.time = t;
            return;
        default: // 080664EC: the message; the picture fades out
            if (t >= 0x2BD) {
                g.clear_screen_processes();
                start_staff_roll(g);
                return;
            }
            if (t > 0x29D) {
                if (t == 0x29E) {
                    fade_ = {0, 1, 1, 0, 0xC0, 1};
                }
                step_fade(g.video(), fade_);
            }
            time_ = backdrop_.time = t;
            for (MessageLine* l : lines_) {
                l->time = t;
            }
            hero_.time = flock_.time = plane_.time = pilot_.time = t;
            return;
        }
    }

private:
    Hero& hero_;                        // +18
    Flyer& pilot_;                      // +20
    Flyer& plane_;                      // +1C
    Flock& flock_;                      // +14
    Backdrop& backdrop_;                // +0C
    Streaks& streaks_;                  // +10
    std::array<MessageLine*, 3> lines_; // +24
    std::array<Cloud*, 4> clouds_{};    // +30
    u16 time_{};                        // +40
    u8 stage_{};
};

// ---------------------------------------------------------------------------
// The staff roll.

constexpr u32 staff_lines = 0x086815B4; // 20h bytes a line: count, palette, s8 y nudge, text
constexpr u32 logo_rows = 0x0868159C;   // 4 words: the rows at which the logos show

// The roll's position (8.8 lines... in 1/256 pixels): the frame count times
// 1 + 143000h / 201Ch, taken every other frame.
u32 roll_position(u32 time, u32 previous) {
    return (time & 1) == 0 ? time * u32(divide(0x143000, 0x201C) + 1) : previous;
}

// 0805E854: the logos between the lines (the game's, the company's, and the
// last one with its second part below it).
class StaffLogos final : public Process {
public:
    explicit StaffLogos(Game& g) : Process(0x6200) {
        logo_.start(0x14000, 0x2E4, 0, 0, 0, 0x480, 0);
        animate(g, logo_);
        under_.start(0x16000, 0x2E5, 1, 0x78, 0, 0x480, 0);
        animate(g, under_);
    }

    u32 time = 0x30; // +78

    void update(Game& g) override {
        position_ = roll_position(time, position_);
        const u32 p = position_ >> 8;
        if (p - 0xC <= 0x9A) {
            return;
        }
        const Assets& a = g.assets();
        const u32 row = (p - 0x96) >> 4;
        const s32 fine = remainder(s32(p - 0xA6), 0x10);
        u32 which = 0xFF, at = 0;
        for (u32 r = 0; r < 10 && which == 0xFF; ++r) {
            for (u32 j = 0; j < 4; ++j) {
                if (a.u32(logo_rows + j * 4) == row + r) {
                    which = j;
                    at = r;
                    break;
                }
            }
        }
        // Past the last logo's row it stays (at the top).
        if (row >= a.u32(logo_rows + 12) - 3) {
            which = 3;
            at = 0;
        }
        if (which == 0xFF) {
            return;
        }
        logo_.y = s16((0x18 - (fine + 4)) + s32(at) * 0x10);
        if (logo_.y >= 0x9B) {
            return;
        }
        logo_.x = 0x78;
        if (which != 0) {
            logo_.animation = 0x2E4;
            if (which == 1 || which == 2) {
                logo_.variant = u8(which);
            } else {
                logo_.variant = 3;
                logo_.y = std::max<s16>(logo_.y, 0x50);
                under_.x = 0x78;
                under_.y = s16(logo_.y + 0x28);
                animate(g, under_);
                draw(g, under_);
            }
        }
        animate(g, logo_);
        draw(g, logo_);
    }

private:
    Sprite logo_;    // +00
    Sprite under_;   // +30
    u32 position_{}; // +7C
};

// 0805E724: up to seven lines of names in view, rolling up.
class StaffNames final : public Process {
public:
    StaffNames() : Process(0x6120) {
        text_.x = -0x50;
        text_.y = 0;
        text_.count = 3;
        text_.layer = 0;
        text_.slot = 7;
        text_.palette = 0xE;
        text_.rows = 5;
        text_.single_row = true;
    }

    u32 time = 0x30; // +78

    void update(Game& g) override {
        const Assets& a = g.assets();
        position_ = roll_position(time, position_);
        const u32 p = position_ >> 8;
        const u32 rise = p - 0xC;
        u32 first = 0;
        s16 fine = 0;
        if (rise >= 0x8B) {
            first = (p - 0x86) >> 4;
            fine = s16(remainder(s32(p - 0x96), 0x10) + 8);
        }
        const u32 lines = first < 0x139 ? 7 : u8(0x43 - first);
        s16 y = 0x24;
        for (u32 i = 0; i < lines; ++i, y = s16(y + 0x10)) {
            const u32 e = staff_lines + (first + i) * 0x20;
            const u8 n = a.u8(e);
            if (n == 0) {
                continue;
            }
            text_.y =
                rise < 0x8B ? s16(a.s8(e + 2) + 0x8A - s16(rise) + y) : s16(a.s8(e + 2) + y - fine);
            if (text_.y > 0x8A) {
                return;
            }
            text_.x = s16(n * -4 + 0x78);
            text_.count = n;
            text_.layer = u16(i << 4);
            text_.palette = u16(a.u8(e + 1) + 0xE);
            const auto chars = a.bytes(e + 3, n);
            draw_credits_text(g, chars.data(), text_);
        }
    }

private:
    SpriteText text_; // +60
    u32 position_{};  // +7C
};

void start_last_picture(Game& g);

// 0805E664: the roll's clock; at its end a fade, then the last picture (or
// straight the SEGA logo after the true ending's special results, save byte
// +1C = 7Fh).
class StaffRoll final : public Process {
public:
    StaffRoll(StaffLogos& logos, StaffNames& names)
        : Process(0x6000), logos_(logos), names_(names) {
        fade_ = {0, 0, 1, 0, 0x80, 8};
    }

    void update(Game& g) override {
        const u32 t = std::min<u32>(time_, 0x208E);
        names_.time = t;
        logos_.time = t;
        ++time_;
        const bool faded = time_ >= 0x2149 && step_fade(g.video(), fade_);
        if (time_ > 0x2166 && faded) {
            auto& v = g.video().settings;
            v.set_control(v.control() & 0x1FFF);
            v.blend.set_control(0);
            v.blend.evy = 0;
            auto& sch = g.scheduler();
            sch.destroy(names_);
            sch.destroy(logos_);
            sch.destroy(*this);
            if (g.save().emeralds == 0x7F) {
                g.start_sega_logo();
            } else {
                start_last_picture(g);
            }
        }
    }

private:
    ScreenFade fade_;   // +00
    StaffLogos& logos_; // +0C
    StaffNames& names_; // +10
    u32 time_ = 0x30;   // +14
};

// 0805E1B4: the roll's pictures: the logos (slot 8), the names' glyphs
// (slot 7), and one plain tile for layer 1.
void load_staff_graphics(Game& g) {
    GraphicsLoad l;
    l.slot = 8;
    l.options = 0x3B;
    l.parts = load::tiles | load::colors | load::sprite;
    l.tiles_from = 0x0867C578;
    l.tiles_to = 0x10400;
    l.tile_bytes = 0x20;
    l.colors_from = 0x0867C598;
    l.color_bytes = 0x20;
    l.palette = 0xF;
    l.key = 1;
    l.extra = {0, 0, 1, 1};
    load_graphics(g, l);
    l.slot = 7;
    l.options = 0;
    l.tiles_from = 0x08658078;
    l.tile_bytes = 0x1D80;
    l.colors_from = 0x0867C638;
    l.palette = 0xE;
    l.key = 2;
    l.extra = {0, 2, 1, 2};
    load_graphics(g, l);
    l.options = 0x2D;
    l.slot = 0;
    l.tiles_from = 0x08674F38;
    l.colors_from = 0x08675138;
    l.tile_bytes = 0x20;
    l.palette = 0;
    l.layer = 1;
    l.map_from = 0x0867D738;
    l.map_bytes = 0;
    l.parts = load::tiles | load::colors;
    l.key = 0x10;
    l.extra = {2, 0, 4, 4};
    load_graphics(g, l);
}

// 080698C0: the last picture ("the end" in the save's language, with two
// parts) fading in and, from frame 154h, out; then the SEGA logo.
class LastPicture final : public Process {
public:
    explicit LastPicture(Game& g) : Process(0x2000) {
        const Assets& a = g.assets();
        const u8 language = g.save().language;
        parts_[0].start(0x10000, a.u16(0x08684550 + language * 2u), a.u8(0x08684554 + language),
                        0x78, 0x78, 0x480, 0x2000);
        animate(g, parts_[0]);
        parts_[1].start(0x12000, 0x2E3, 0, 0x88, 0x5A, 0x480, 0x2000);
        animate(g, parts_[1]);
        parts_[2].start(0x14000, 0x2E3, 1, 0x88, 0x5A, 0x480, 0x2000);
        animate(g, parts_[2]);
        fade_ = {0, 1, 2, 0, 0x100, 8};
        step_fade(g.video(), fade_);
    }

    void update(Game& g) override {
        if (++time_ < 0x191) {
            if (time_ == 0x154) {
                fade_.level = 0;
                fade_.kind = 1;
            }
            step_fade(g.video(), fade_);
            for (Sprite& s : parts_) {
                draw(g, s); // 08069930
            }
            return;
        }
        auto& v = g.video().settings;
        v.set_control(v.control() & 0x1FFF);
        v.blend.set_control(0);
        v.blend.evy = 0;
        g.scheduler().destroy(*this);
        g.start_sega_logo();
    }

private:
    std::array<Sprite, 3> parts_; // +00, +30, +60
    ScreenFade fade_;             // +90
    u32 time_{};                  // +9C
};

// 080696DC / 08069658
void start_last_picture(Game& g) {
    ensure_screen_graphics(g);
    auto& v = g.video().settings;
    v.set_control(0x1140);
    v.bg[0].set_control(0x1D03);
    v.bg[0].scroll_x = v.bg[0].scroll_y = 0;
    g.screen_graphics->reset_pool();
    GraphicsLoad l;
    l.options = 0x2B;
    l.slot = 0;
    l.parts = load::tiles | load::colors;
    l.layer = 0;
    l.palette = 0;
    l.tiles_from = 0x08674BF8;
    l.tile_bytes = 0x20;
    l.colors_from = 0x086C5650;
    l.color_bytes = 0x20;
    l.map_from = 0x086C5710;
    l.map_bytes = 0x500;
    load_graphics(g, l);
    auto& q = g.backgrounds();
    q.clear_value[0] = 1;
    q.clears[0] = {0, 0, 0xFF, 0x14};
    g.scheduler().create<LastPicture>(g);
}

// ---------------------------------------------------------------------------
// The true ending (080670C4-08068038, 080685EC-080685F8).

// A sprite of the true ending (100-byte records at 2120h): its time (+3C)
// comes from the director.
class Figure : public Process {
public:
    Figure(u32 tiles, u16 animation, u8 variant, s16 x, s16 y, u16 layer, u32 flags)
        : Process(0x2120) {
        sprite.start(tiles, animation, variant, x, y, layer, flags);
    }

    Sprite sprite; // +00
    u32 time{};    // +3C
    u32 since{};   // +40 the director's mark (the results' end)
    s32 x{};       // +44
    s32 y{};       // +54

protected:
    void show(Game& g) {
        sprite.x = s16(x >> 8);
        sprite.y = s16(y >> 8);
        animate(g, sprite);
        draw(g, sprite);
    }
};

// 08068038: four stars twinkling one after another (the third and fourth
// together, then the last alone) once the results are over.
class Stars final : public Figure {
public:
    Stars() : Figure(0x10020, 0x2DB, 0, 0x78, 0x50, 0x3C0, 0x2000) {}

    void update(Game& g) override {
        if (time <= since) {
            return;
        }
        u32 k = u32(divide(s32(time - since), 0x10)) & 0xFF;
        const s8 phase = s8(remainder(s32(time - since), 0x10));
        if (k <= 2 && phase == 0) {
            sprite.previous_variant = 0xFF; // each star from its first frame
        }
        if (k == 0 || k == 2) {
            if (k == 2) {
                k = 3;
            }
            sprite.x = s16(places_x[k]);
            sprite.y = s16(places_y[k]);
            animate(g, sprite);
            draw(g, sprite);
        } else if (k == 1) {
            sprite.x = s16(places_x[1]);
            sprite.y = s16(places_y[1]);
            animate(g, sprite);
            draw(g, sprite);
            sprite.x = s16(places_x[2]);
            sprite.y = s16(places_y[2]);
            draw(g, sprite);
        }
    }

private:
    static constexpr s32 places_x[4] = {0x69, 0x2F, 0xD5, 0xA8}; // +44-+50
    static constexpr s32 places_y[4] = {0x11, 0x5F, 0x45, 0x5F}; // +54-+60
};

// The story's cards in turn: "a few days later" (by language; it only
// moves the layers), three pictures, the last two (the hero's face)
// drawn over the colour shift.
class StoryCard final : public Figure {
public:
    explicit StoryCard(u8 language)
        : Figure(0x100A0, language == 0 ? 0x312 : 0x30F, 0, 0x78, 0x50, 0, 0x80) {}
    enum class Move { lift, caption, none, plane, wave, face, glow } move = Move::lift;

    void update(Game& g) override {
        switch (move) {
        case Move::lift: { // 08067F04: the layers sink away (at three speeds)
            if (since + 0x3C > time) {
                return;
            }
            auto& bg = g.video().settings.bg;
            layer_y_[0] += 0x55;
            bg[0].scroll_y = std::min<u16>(u16(layer_y_[0] >> 8), 0x47);
            layer_y_[1] += 0x60;
            bg[1].scroll_y = std::min<u16>(u16(layer_y_[1] >> 8), 0x50);
            layer_y_[2] += 0xC0;
            bg[2].scroll_y = std::min<u16>(u16(layer_y_[2] >> 8), 0xA0);
            return;
        }
        case Move::caption: // 08068508
            if (time < 600) {
                animate(g, sprite);
                draw(g, sprite);
            }
            return;
        case Move::none:
            return;       // 08068538
        case Move::plane: // 0806853C / 08068594
        case Move::wave:
            if (time == 0) {
                change(0x2DD, move == Move::plane ? 0 : 1, 0);
            }
            break;
        case Move::face: // 08067F68 / 08067FD0
        case Move::glow:
            if (time == 0) {
                change(0x2DE, move == Move::face ? 0 : 1, 0x2000);
                sprite.layer = 0x3C0;
            }
            break;
        }
        animate(g, sprite);
        draw(g, sprite);
    }

private:
    void change(u16 animation, u8 variant, u32 flags) {
        sprite.previous_variant = 0xFF;
        sprite.tiles.to = 0x126E0;
        sprite.animation = animation;
        sprite.variant = variant;
        sprite.x = 0x78;
        sprite.y = 0x50;
        sprite.flags = flags;
    }

    std::array<s32, 3> layer_y_{}; // +54, +58, +5C
};

// 080685F8: a glint drifting up and left from frame B4h.
class Glint final : public Figure {
public:
    Glint() : Figure(0x12560, 0x2DC, 0, s16(0xA000), s16(0xE000), 0xC0, 0x1000) {
        x = 0xA000;
        y = -0x2000;
    }

    bool moving{};

    void update(Game& g) override {
        if (!moving || time <= 0xB4) {
            return;
        }
        x = std::max(x - 0x60, -0x4000);
        y = std::min(y + 0x30, 0x2000);
        show(g);
    }
};

// 08068114: Tails in the plane, bobbing; waves at 78h and 1E0h; swings
// down from 600 on and stops at x 8Ch.
class Pilot final : public Figure {
public:
    Pilot() : Figure(0x10440, 0x325, 1, 0x78, 0x50, 0xC0, 0) {
        x = 0x7800;
        y = 0x5A00;
    }

    bool moving{};

    void update(Game& g) override {
        if (!moving) {
            return;
        }
        const Assets& a = g.assets();
        if (time == 0x78 || time == 0x1E0) {
            sprite.animation = 0xBF;
            sprite.variant = time == 0x78 ? 0 : 1;
            sprite.previous_variant = 0xFF;
        }
        if (time < 600) {
            y += sine(a, (time & 0xFF) * 4) >> 9;
        } else if (time < 0x297) {
            y += divide(sine(a, (time - 600) * 4), 0x1A);
        } else {
            x = 0x8C00;
        }
        show(g);
    }
};

// 080681E0: the plane, bobbing, swinging down; from 699 a shining star
// that turns and shrinks as it rises.
class Plane final : public Figure {
public:
    Plane() : Figure(0x10560, 0x2E0, 0, 0x78, 0x50, 0x180, 0x1000) {
        x = 0x7800;
        y = 0x5A00;
    }

    bool moving{};

    void update(Game& g) override {
        if (!moving) {
            return;
        }
        const Assets& a = g.assets();
        s32 dy;
        if (time < 600) {
            dy = sine(a, (time & 0xFF) * 4) >> 9;
        } else if (time < 0x297) {
            dy = divide(sine(a, (time - 600) * 4), 0x1A);
        } else {
            if (time < 700) {
                x = y = 0xB400;
                if (time == 699) {
                    sprite.animation = 0x2DF;
                    sprite.variant = 0;
                    sprite.previous_variant = 0xFF;
                    sprite.flags = 0x106B; // rotation set 11, double size
                    animate(g, sprite);
                }
                return;
            }
            const s16 s = sine(a, (0x3BB - time) * 4);
            y += divide(s, 0x24);
            x += divide(s, 0x12);
            const s16 n = s16(time - 700);
            const u16 turn = u16(~(n * 2) & 0x3FF);
            const s16 scale = s16(n * -3 + 0x180);
            sprite.x = s16(x >> 8);
            sprite.y = s16(y >> 8);
            transform(g, sprite, turn, scale, scale, sprite.x, sprite.y);
            draw(g, sprite);
            return;
        }
        y += dy;
        show(g);
    }
};

// The night's and the day's clouds (as the ending's: 08068414 still, then
// 0806832C around the view).
class SkyCloud final : public Figure {
public:
    SkyCloud(u32 tiles, u16 animation, s32 x0, s32 y0, s32 speed)
        : Figure(tiles, animation, 0, 0xB4, 0x50, 0x300, 0x1000), speed_(speed) {
        x = x0;
        y = y0;
    }

    bool moving{};

    void update(Game& g) override {
        if (!moving) {
            return;
        }
        x += speed_;
        if (x > 0x11000) {
            x = -0x7800;
        }
        if (x < -0x7800) {
            x = 0x11000;
        }
        if (y > 0xAC00) {
            y = -0x1000;
        }
        if (y < -0x1000) {
            y = 0xAC00;
        }
        show(g);
    }

private:
    s32 speed_; // +48 (+58, its vertical speed, is 0)
};

void start_true_staff_roll(Game& g) {
    g.clear_screen_processes();
    start_staff_roll(g);
}

// 080677F0 and its stages: the true ending's timeline.
class TrueEndingDirector final : public Process {
public:
    TrueEndingDirector(StoryCard& card, Stars& stars, Glint& glint, Plane& plane, Pilot& pilot,
                       std::array<SkyCloud*, 4> clouds)
        : Process(0x2100), card_(card), stars_(stars), glint_(glint), plane_(plane), pilot_(pilot),
          clouds_(clouds) {
        fade_ = {0, 1, 8, 0, 0x80, 1};
    }

    ScreenFade fade_; // +00

    void update(Game& g) override {
        u32 t = time_ + 1;
        const u32 quarter = u32(divide(0x2000, 0x80)); // the fade's frames
        const auto fade = [&](u16 kind) {
            fade_ = {0, 1, kind, 0, 0x80, 1};
        };
        switch (stage_) {
        case 0: // 080677F0: the results under the night sky
            if (t == 0x19) {
                g.screen_graphics->reset_pool();
                load_hud_pictures(g);
            } else if (t == 0x1E) {
                StageState& s = g.stage();
                results_end_ = start_results(g, s.rings, u32(s.clock));
            }
            if (t == results_end_) {
                g.sound().start(0x27);
            }
            if (quarter + results_end_ + 0x21C < t) {
                time_ = 0;
                card_.time = 0;
                g.scheduler().destroy(stars_);
                card_.move = StoryCard::Move::caption;
                stage_ = 1;
                return;
            }
            if (quarter + results_end_ + 0x21C - quarter == t) {
                fade(1);
            }
            step_fade(g.video(), fade_);
            time_ = card_.time = stars_.time = t;
            card_.since = stars_.since = results_end_;
            return;
        case 1: // 080678F4: "a few days later"
            if (quarter + 600 < t) {
                auto& v = g.video().settings;
                v.set_control(0x1140);
                v.bg[0].set_control(0x9B83);
                fade(2);
                step_fade(g.video(), fade_);
                t = 0;
                for (SkyCloud* c : clouds_) {
                    c->moving = true;
                }
                glint_.moving = true;
                plane_.moving = true;
                pilot_.moving = true;
                card_.move = StoryCard::Move::none;
                stage_ = 2;
            }
            time_ = card_.time = t;
            return;
        case 2: // 080679B0: the plane in the day's sky
            if (t == 1) {
                // 08067790: the sky (layer 0).
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
            g.video().settings.bg[0].scroll_y = 0x128;
            if (t < 0x337) {
                if (0x336 - quarter == t) {
                    fade(1);
                }
                step_fade(g.video(), fade_);
                time_ = card_.time = pilot_.time = glint_.time = plane_.time = t;
                return;
            }
            time_ = 0;
            card_.time = 0;
            {
                auto& sch = g.scheduler();
                for (SkyCloud* c : clouds_) {
                    sch.destroy(*c);
                }
                sch.destroy(pilot_);
                sch.destroy(glint_);
                sch.destroy(plane_);
            }
            card_.move = StoryCard::Move::plane;
            stage_ = 3;
            {
                auto& q = g.backgrounds();
                q.clear_value[0] = 0;
                q.clears[0] = {0, 0, 0xFF, 0x14};
            }
            g.video().settings.bg[0].scroll_y = 0;
            fade(2);
            return;
        case 3: // 08067AE0 / 08067B68: two picture cards
        case 4:
            if (t < 0x191) {
                if (400 - quarter == t) {
                    fade(1);
                }
                step_fade(g.video(), fade_);
                time_ = card_.time = t;
                return;
            }
            time_ = 0;
            card_.time = 0;
            fade(2);
            card_.move = stage_ == 3 ? StoryCard::Move::wave : StoryCard::Move::face;
            ++stage_;
            return;
        case 5: { // 08067BF0: the face takes its colours (the sprite colours
                  // E2h-EFh from the sepia set to the real one over 128 frames)
            const u32 shift = time_ - 0xB3;
            if (shift < 0x81) {
                const Assets& a = g.assets();
                const u32 step = shift >> 4;
                auto& vs = g.video();
                for (u32 i = 2; i < 0x10; ++i) {
                    const u16 from = a.u16(0x0867CED8 + i * 2), to = a.u16(0x0867CF18 + i * 2);
                    const auto channel = [&](unsigned bit) {
                        const u32 c0 = (from >> bit) & 0x1F, c1 = (to >> bit) & 0x1F;
                        const u32 d =
                            c1 < c0 ? u32(-s32((step * (c0 - c1)) >> 3)) : (step * (c1 - c0)) >> 3;
                        return u16(((c0 + d) & 0x1F) << bit);
                    };
                    vs.sprite_colors[0xE0 + i] = u16(channel(0) | channel(5) | channel(10));
                }
                vs.sprite_colors_changed = true;
            }
            if (t > 400) {
                t = 0;
                constexpr u32 line_tiles[3] = {0x16060, 0x16920, 0x16E20};
                for (u8 i = 0; i < 3; ++i) {
                    lines_[i] = &g.scheduler().create<MessageLine>(g, line_tiles[i], i, true);
                }
                card_.move = StoryCard::Move::glow;
                stage_ = 6;
            }
            step_fade(g.video(), fade_);
            time_ = card_.time = t;
            return;
        }
        default: { // 08067E34: the message; then the staff roll
            const u32 before = time_;
            if (t >= 0x191) {
                start_true_staff_roll(g);
                return;
            }
            if (400 - quarter == t) {
                fade(1);
            }
            step_fade(g.video(), fade_);
            time_ = card_.time = t;
            for (MessageLine* l : lines_) {
                l->time = u16(before + 0x14B);
            }
            return;
        }
        }
    }

private:
    StoryCard& card_;                     // +0C
    Stars& stars_;                        // +10
    Glint& glint_;                        // +14
    Plane& plane_;                        // +18
    Pilot& pilot_;                        // +1C
    std::array<SkyCloud*, 4> clouds_;     // +20-+2C
    std::array<MessageLine*, 3> lines_{}; // +2C-+34 (the first over the last cloud's place)
    u32 time_{};                          // +38
    u32 results_end_ = 0x3C;              // +3C
    u8 stage_{};
};

} // namespace

void start_ending(Game& g) {
    // The save is written with the interrupts off; the sound output starts
    // over around it.
    g.clear_screen_processes();
    g.video().line_effect_on = false;
    g.sound().stop_all();
    g.sound().restart_output();
    ensure_screen_graphics(g);
    g.screen_graphics->reset_pool();
    auto& vs = g.video();
    auto& v = vs.settings;
    v.set_control(0x1140);
    v.bg[0].set_control(0x9D86);
    v.bg[1].set_control(0x1B8B);
    v.bg[2].set_control(0x9880);
    for (unsigned i = 0; i < 3; ++i) {
        v.bg[i].scroll_x = v.bg[i].scroll_y = 0;
    }
    const Assets& a = g.assets();
    const u8 ch = g.stage().character;
    auto& sch = g.scheduler();
    const auto anim = [&](u32 table) {
        return a.u16(table + ch * 4u);
    };
    const auto variant = [&](u32 table) {
        return u8(a.u16(table + ch * 4u + 2));
    };
    Hero& hero = sch.create<Hero>(g, 0x10020, anim(falling_animations), variant(falling_animations),
                                  0x78, 0x50, 0x180, true);
    hero.speed = 0x780;
    hero.x = 0x7800;
    hero.y = 0x5000;
    Flyer& pilot = sch.create<Flyer>(g, 0x17D00, anim(pilot_animations), variant(pilot_animations),
                                     300, 0x6C, 0x240, true);
    pilot.speed = 0x780;
    pilot.x = 0x12C00;
    pilot.y = 0x6C00;
    Flyer& plane = sch.create<Flyer>(g, 0x10BA0, 0x2E0, 0, 300, 0x6C, 0x300, false);
    plane.speed = 0x780;
    plane.x = 0x12C00;
    plane.y = 0x6C00;
    Flock& flock = sch.create<Flock>(g, 0x10940, 0x314, 0, 0xF8, 0x50, 0x3C0, true);
    flock.speed = 0x100;
    flock.x = 0xF800;
    flock.y = 0x5000;
    Backdrop& backdrop = sch.create<Backdrop>(g, 0x14020, anim(backdrop_animations),
                                              variant(backdrop_animations), 0x78, 0x50, 0xC0, true);
    Streaks& streaks = sch.create<Streaks>(g, 0x109C0, 0x308, 0, 0x78, 0x50, 0, true);
    streaks.extra = 0xF;
    std::array<MessageLine*, 3> lines{};
    constexpr u32 line_tiles[3] = {0x11AA0, 0x12340, 0x12820};
    for (u8 i = 0; i < 3; ++i) {
        lines[i] = &sch.create<MessageLine>(g, line_tiles[i], i);
    }
    EndingDirector& director =
        sch.create<EndingDirector>(hero, pilot, plane, flock, backdrop, streaks, lines);
    apply_fade(vs, director.fade_);
    // The sky (layer 0).
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
    g.sound().start(music_ending);
}

void start_true_ending(Game& g) {
    SaveData& save = g.save();
    save.zones_reached[0] = 0xF;
    save.unknown_5584 = 1;
    g.sound().stop_all();
    g.sound().restart_output(); // around the save write
    g.clear_screen_processes();
    g.video().line_effect_on = false;
    ensure_screen_graphics(g);
    g.screen_graphics->reset_pool();
    auto& vs = g.video();
    auto& v = vs.settings;
    v.set_control(0x1740);
    v.bg[0].set_control(0x8583);
    v.bg[1].set_control(0x9E86);
    v.bg[2].set_control(0x9B89);
    for (unsigned i = 0; i < 3; ++i) {
        v.bg[i].scroll_x = v.bg[i].scroll_y = 0;
    }
    auto& sch = g.scheduler();
    Glint& glint = sch.create<Glint>();
    Pilot& pilot = sch.create<Pilot>();
    Plane& plane = sch.create<Plane>();
    Stars& stars = sch.create<Stars>();

    static constexpr struct {
        s32 x, y, speed;
        u32 tiles;
        u16 animation;
    } kinds[4] = {
        {0x5000, 0x3200, 0x80, 0x16060, 0x316},
        {0x2800, 0x6400, 0x40, 0x161A0, 0x317},
        {0x7800, 0x7800, 0x100, 0x16440, 0x318},
        {0xA000, 0x1400, 0xC0, 0x170A0, 0x319},
    };

    std::array<SkyCloud*, 4> clouds{};
    for (unsigned i = 0; i < 4; ++i) {
        clouds[i] = &sch.create<SkyCloud>(kinds[i].tiles, kinds[i].animation, kinds[i].x,
                                          kinds[i].y, kinds[i].speed);
    }
    StoryCard& card = sch.create<StoryCard>(save.language);
    TrueEndingDirector& director =
        sch.create<TrueEndingDirector>(card, stars, glint, plane, pilot, clouds);
    apply_fade(vs, director.fade_);

    // The night sky (layer 0), the friends (layer 2), the trees (layer 1).
    static constexpr struct {
        u8 slot, layer;
        u32 tiles, tile_bytes, colors, map;
    } layers[3] = {
        {2, 0, 0x086BEC50, 0x1EC0, 0x086BEA50, 0x086C0B10},
        {0, 2, 0x086B8890, 0x57C0, 0x086B8690, 0x086BE050},
        {1, 1, 0x086C1710, 0x3540, 0x086C1510, 0x086C4C50},
    };

    GraphicsLoad l;
    for (const auto& k : layers) {
        l.options = 0x80;
        l.slot = k.slot;
        l.layer = k.layer;
        l.palette = 0;
        l.parts = load::tiles | load::colors | load::map;
        l.tiles_from = k.tiles;
        l.tile_bytes = k.tile_bytes;
        l.colors_from = k.colors;
        l.color_bytes = 0x200;
        l.map_from = k.map;
        l.map_bytes = 0xA00;
        load_graphics(g, l);
    }
}

void start_staff_roll(Game& g) {
    // (Everything but the screens' graphics process ends; it was ended
    // already.)
    auto& sch = g.scheduler();
    sch.destroy_range(1, 0x1800);
    sch.destroy_range(0x1801, 0xFFFF);
    ensure_screen_graphics(g);
    g.screen_graphics->reset_pool();
    auto& vs = g.video();
    auto& v = vs.settings;
    v.set_control(0x1240);
    v.bg[1].set_control(0x1907);
    v.bg[1].scroll_x = v.bg[1].scroll_y = 0;
    v.bg[2].scroll_x = v.bg[2].scroll_y = 0;
    StaffLogos& logos = sch.create<StaffLogos>(g);
    StaffNames& names = sch.create<StaffNames>();
    StaffRoll& roll = sch.create<StaffRoll>(logos, names);
    (void)roll;
    // Layer 1's first tile cleared at once, its map filled with tile 1.
    auto& memory = g.video_memory_now().memory;
    const u32 tiles = v.bg[1].tile_block * 0x4000u;
    std::fill(memory.begin() + tiles, memory.begin() + tiles + 0x40, u8(0));
    auto& q = g.backgrounds();
    q.clear_value[1] = 1;
    q.clears[1] = {0, 0, 0xFF, 0x14};
    // Windows 0 (lines 0-23h) and 1 (lines 84h-9Fh) cover the top and the
    // bottom; between them (outside) the colour effect fades the names in
    // and out.
    v.window0 = v.window1 = true;
    v.windows[0] = {0, 0xF0, 0, 0x24};
    v.windows[1] = {0, 0xF0, 0x84, 0xA0};
    v.inside[0] = v.inside[1] = 0x3F;
    v.outside = 0x1F;
    v.inside_sprite_window = 0;
    v.blend.set_control(0x1FDF);
    v.blend.eva = 0x10;
    v.blend.evb = 0;
    v.blend.evy = 0x10;
    load_staff_graphics(g);
    g.sound().start(music_staff_roll);
}

} // namespace sonic_advance_gba
