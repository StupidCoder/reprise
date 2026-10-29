// Starting an act and the processes that run it (0803C944-0803DD88,
// 08008784, 0804C5A4).

#include "sonic_advance_gba/game/stage.hpp"
#include "sonic_advance_gba/game/boss.hpp"
#include "sonic_advance_gba/game/camera.hpp"
#include "sonic_advance_gba/game/demo.hpp"
#include "sonic_advance_gba/game/game.hpp"
#include "sonic_advance_gba/game/game_over.hpp"
#include "sonic_advance_gba/game/objects.hpp"
#include "sonic_advance_gba/game/rings.hpp"
#include "sonic_advance_gba/game/searchlight.hpp"
#include "sonic_advance_gba/game/sprite.hpp"
#include "sonic_advance_gba/game/title_card.hpp"
#include "sonic_advance_gba/game/water.hpp"

namespace sonic_advance_gba {

namespace {

constexpr u32 zone_songs = 0x084A69E4;  // per act: its music
constexpr u32 jingles = 0x084A6A08;     // the music manager's queued songs
constexpr u32 one_up_song = 0x087B9458; // the headers the manager looks for
constexpr u32 drowning_song = 0x087A86B8, invincible_song = 0x087A8A98;
constexpr u32 zone_starts = 0x084A6A18; // per act: its start routine

// 0801A674, 0801A530: the pause (priority FFFE, running in the pause
// mode): "PAUSE" in the middle of the screen; Start goes on. Its tiles
// are freed with it (0801A6C8).
class Pause final : public Process {
public:
    explicit Pause(Game& g) : Process(0xFFFE), game_(g) {
        runs_in_pause = true;
        if (g.game_mode == 1) {
            throw Unrecovered("the time attack pause (0801A348)");
        }
        label_.start(g.sprite_tiles.allocate(0x30), 0x2D0, 0, 0x78, 0x50, 0x40, 0);
        animate(g, label_);
        frame_.start(g.sprite_tiles.allocate(0x14), 0x2CE, 0, 0x78, 0x50, 0x40, 0);
        animate(g, frame_);
    }

    void on_destroy() override {
        game_.sprite_tiles.free(frame_.tiles.to);
        game_.sprite_tiles.free(label_.tiles.to);
    }

    void update(Game& g) override {
        if (!started_) {
            // 0801A674: the music and effects stop (not the voices).
            for (u8 n = 0; n < 3; ++n) {
                g.sound().stop_player(n);
            }
            g.sound().start(0x8E);
            g.pause_requested = true;
            started_ = true;
            return;
        }
        if (g.controller().pressed_once & button::start) {
            g.pause_requested = false;
            g.sound().resume_all();
            g.scheduler().destroy(*this);
            return;
        }
        // Half transparent while the picture is faded.
        for (Sprite* s : {&frame_, &label_}) {
            if (g.video().settings.blend.evy != 0) {
                s->flags |= 0x80;
            } else {
                s->flags &= ~u32(0x180);
            }
        }
        draw(g, frame_);
        draw(g, label_);
    }

private:
    Game& game_;
    Sprite label_; // +00
    Sprite frame_; // +30
    bool started_ = false;
};

// 0801A348: Start during an act.
void start_pause(Game& g) {
    if (!(g.stage().flags & stage_flag::title_card)) {
        g.scheduler().create<Pause>(g);
    }
}

// 0803D25C: the act's clock.
class StageProcess final : public Process {
public:
    explicit StageProcess(Game& g) : Process(0xFF00), game_(g) {}

    void update(Game& g) override {
        StageState& s = g.stage();
        if (g.game_mode >= 2) {
            throw Unrecovered("the multiplayer act clock (0803D25C)");
        }
        if (!(s.flags & stage_flag::title_card) && (g.controller().pressed_once & button::start) &&
            !(s.flags & stage_flag::demo)) {
            start_pause(g);
        }
        ++s.ticks;
        s.previous_flags = s.flags;
        if (s.flags & stage_flag::paused) {
            return;
        }
        ++s.frames;
        if (s.flags & stage_flag::countdown) {
            // Counting down (act 10's rocket): at zero the time is over.
            if (--s.clock > 0) {
                return;
            }
            s.flags |= stage_flag::paused;
            if (g.save().time_limit_off != 0) {
                return;
            }
            Player& p = s.player[0];
            p.power = 0;
            p.speed_y = s16((p.flags & player_flag::underwater) ? -0x2A0 : -0x4E0);
            if (s.zone == 10) {
                shake_view(g, 0x800, 8, 0x10, -1, 0xD0);
            }
            p.flags |= player_flag::dead;
            g.sound().start(0xA6);
            return;
        }
        ++s.clock;
        if (s.clock < 0x8CA1) {
            return;
        }
        // Ten minutes: time over.
        s.flags |= stage_flag::paused;
        if (g.save().time_limit_off != 0 && g.game_mode != 1) {
            return;
        }
        Player& p = s.player[0];
        p.power = 0;
        p.speed_y = s16((p.flags & player_flag::underwater) ? -0x2A0 : -0x4E0);
        p.flags |= player_flag::dead;
        g.sound().start(0xA6);
    }

    void on_destroy() override {
        // 0803DC50
        Game& g = game_;
        if (u8(g.game_mode - 1) <= 2 || (g.stage().flags & stage_flag::demo)) {
            g.save().unknown_5178 = g.stage().saved_5178;
        }
        g.sound().stop_all();
    }

private:
    Game& game_;
};

// 0803C944: the act's music: its song, restarted when another ended, and
// the jingles and power-up songs the other parts ask for.
class MusicManager final : public Process {
public:
    MusicManager() : Process(0x4000) {}

    void update(Game& g) override {
        StageState& s = g.stage();
        Sound& snd = g.sound();
        const Assets& a = g.assets();
        const u8 one_up = s.music_one_up;
        if (s.music_off != 0 || (s.player[0].flags & player_flag::dead)) {
            return;
        }
        if (snd.song(0) == drowning_song && snd.playing(0)) {
            s.music_invincible = 0;
            s.music_one_up = 0;
            return;
        }
        if (s.music_drowning != 0) {
            s.music_drowning = 0;
            snd.start(0x1B);
            return;
        }
        if (snd.song(0) == invincible_song && snd.playing(0) &&
            !(s.player[0].power & player_power::invincible)) {
            snd.stop(0x1C);
            return;
        }
        const auto zone_song = [&] {
            snd.start(a.u16(zone_songs + u32(s32(s.zone)) * 2));
            restore_music(g);
        };
        const bool other_song = snd.song(0) == one_up_song || snd.song(0) == invincible_song;
        if (other_song || !(s.player[0].power & player_power::invincible)) {
            if (s.music_invincible != 0) {
                s.music_invincible = 0;
                snd.start(0x1C);
                return;
            }
            if (s.music_one_up != 0) {
                s.music_one_up = 0;
                snd.start(0x133);
                return;
            }
            const u8 request = s.music_queued & 0xF0;
            if (request == 0x10) {
                s.music_queued &= 0xF;
                snd.start(a.u16(jingles + s.music_queued * 2u));
                s.music_queued |= 0x20;
                return;
            }
            if (request == 0x30) {
                s.music_queued &= 0xF;
                snd.stop(a.u16(jingles + s.music_queued * 2u));
                zone_song();
                if (u8(s.music_queued - 4) > 1) {
                    s.music_off = 0xFF;
                }
                s.music_queued = one_up;
                return;
            }
            if (snd.playing(0)) {
                return;
            }
            if (request != 0x20) {
                zone_song();
                return;
            }
            snd.start(a.u16(jingles + (s.music_queued & 0xFu) * 2));
        } else {
            if (s.music_invincible != 0) {
                s.music_invincible = 0;
                snd.start(0x1C);
                return;
            }
            snd.start(0x1C);
        }
        restore_music(g);
    }

private:
    // 0803CB4C: the music's tracks start over and fade in (fast with
    // speed shoes).
    static void restore_music(Game& g) {
        g.sound().restore_music(g.stage().player[0].power & player_power::fast);
    }
};

// 0800CF04 + 0800CF90: a background colour cycle: an animation of the
// background variant, ended with its script.
class ColorCycle final : public Process {
public:
    ColorCycle(u16 priority, u16 animation, u8 variant) : Process(priority) {
        // (Its tile commands, if any, go to address 0: nowhere.)
        sprite_.start(TileCopy::nowhere, animation, variant, 0, 0, 0, 0);
    }

    void update(Game& g) override {
        if (animate_background(g, sprite_) == 0) {
            g.scheduler().destroy(*this);
        }
    }

private:
    Sprite sprite_;
};

// The act's own start (084A6A18).
void start_zone(Game& g) {
    StageState& s = g.stage();
    switch (g.assets().u32(zone_starts + u32(s32(s.zone)) * 4)) {
    case 0x0803DC99: // 0803DC98: Neo Green Hill act 1: two colour cycles, the music
        g.scheduler().create<ColorCycle>(0x2000, 0x32E, 0);
        g.scheduler().create<ColorCycle>(0x2000, 0x32F, 0);
        g.sound().start(0xC);
        break;
    case 0x0803DCCD: // 0803DCCC: Neo Green Hill act 2: four colour cycles; the
                     // view stops 300h before the act's end (the boss)
        g.scheduler().create<ColorCycle>(0x2000, 0x32E, 0);
        g.scheduler().create<ColorCycle>(0x2000, 0x32F, 0);
        g.scheduler().create<ColorCycle>(0x2000, 0x334, 1);
        g.scheduler().create<ColorCycle>(0x2000, 0x334, 0);
        if (g.game_mode < 2) {
            s.camera.max_x = s16(s.map.width - 0x300);
        }
        g.sound().start(0xD);
        break;
    case 0x0803D855: // 0803D854: Casino Paradise act 1: eleven colour cycles, the music
    case 0x0803D90D: // 0803D90C: act 2: the same with its own music
        for (const u16 animation :
             {0x330, 0x331, 0x332, 0x338, 0x339, 0x33B, 0x33B, 0x33C, 0x34B, 0x34C, 0x33A}) {
            g.scheduler().create<ColorCycle>(0x2000, animation, 0);
        }
        g.sound().start(s.zone == 4 ? 0x10 : 0x11);
        break;
    case 0x0803DDCD: // 0803DDCC: Ice Paradise act 1: two colour cycles, the water, the music
    case 0x0803DE11: // 0803DE10: act 2: the same, the water deeper
        g.scheduler().create<ColorCycle>(0x2000, 0x336, 0);
        g.scheduler().create<ColorCycle>(0x2000, 0x335, 0);
        set_water(g, s.zone == 6 ? 0x60A : 0x680, 0x7F207F20, 0);
        g.sound().start(s.zone == 6 ? 0x12 : 0x13);
        break;
    case 0x0803DD45: // 0803DD44: Secret Base act 1: three colour cycles, the music
    case 0x0803DD89: // 0803DD88: Secret Base act 2: the same with its own music
        g.scheduler().create<ColorCycle>(0x2000, 0x333, 0);
        g.scheduler().create<ColorCycle>(0x2000, 0x34D, 0);
        g.scheduler().create<ColorCycle>(0x2000, 0x351, 0);
        g.sound().start(s.zone == 2 ? 0xE : 0xF);
        break;
    case 0x0803DE55: // 0803DE54: act 8: a colour cycle, the music
    case 0x0803DE75: // 0803DE74: act 9: the same with its own music
        g.scheduler().create<ColorCycle>(0x2000, 0x337, 0);
        g.sound().start(s.zone == 8 ? 0x14 : 0x15);
        break;
    case 0x0803D9C5: { // 0803D9C4: act 10 (the rocket): four colour cycles, the music;
                       // restarting past a checkpoint, the rocket is up already
        for (const u16 animation : {0x347, 0x348, 0x349, 0x34A}) {
            g.scheduler().create<ColorCycle>(0x2000, animation, 0);
        }
        g.sound().start(0x16);
        auto& c = s.camera;
        auto& far = g.video().settings.bg[3];
        // The rocket's clock: five minutes, counting down.
        const auto countdown = [&] {
            if (g.game_mode == 0) {
                s.flags |= stage_flag::countdown;
                s.clock = 18000;
            }
        };
        if (s.checkpoint > 0) {
            c.min_x = 0x422;
            c.locks |= camera_lock::rocket_up;
            far.size = 0;
            countdown();
        }
        if (s.checkpoint > 1) {
            c.max_y = 0x15C0;
            c.locks |= camera_lock::rocket_up;
            far.size = 0;
            countdown();
        }
        if (s.checkpoint > 2) {
            far.size = 0;
            c.locks = u16((c.locks & ~camera_lock::rocket_up) | camera_lock::rocket_space);
            c.max_y = 0xCC0;
            countdown();
        }
        break;
    }
    case 0x0803DE95: // 0803DE94: act 11: eight colour cycles, the music
        for (const u16 animation : {0x33D, 0x33E, 0x33F, 0x352, 0x353, 0x350, 0x34E, 0x34F}) {
            g.scheduler().create<ColorCycle>(0x2000, animation, 0);
        }
        g.sound().start(0x17);
        break;
    case 0x0803DF25: // 0803DF24: act 12: the music
        g.sound().start(0x18);
        break;
    case 0x0803DF31: // 0803DF30: act 13: the music
        g.sound().start(0x1A);
        break;
    default:
        throw Unrecovered("the start of act " + std::to_string(s.zone) + " (084A6A18)");
    }
}

// 080177B4, 08017874, 08017920: Secret Base act 1 opens dark, lit only
// by two searchlights (the windows); it brightens once the player is past
// 559h, and the lights go at 5D1h, with a death or with Start in a demo.
class SecretBaseLights final : public Process {
public:
    SecretBaseLights(Game& g) : Process(0x2000), game_(g) {
        auto& sched = g.scheduler();
        left_ = &sched.create<Searchlight>(g.video());
        left_->x = 0x3C;
        left_->y = 200;
        left_->angle = 0;
        left_->turn_speed = 0x40;
        left_->swing = 0x800;
        left_->window = 2;
        left_->width = 0x20;
        right_ = &sched.create<Searchlight>(g.video());
        right_->x = 200;
        right_->y = 0xF0;
        right_->angle = 0;
        right_->turn_speed = 8;
        right_->swing = 0x400;
        right_->window = 3;
        right_->width = 0x10;
        auto& v = g.video().settings;
        v.blend.set_control(0x3FDF);
        v.blend.evy = u8(s8(darkness_ >> 8));
    }

    // 080179A8
    void on_destroy() override {
        auto& v = game_.video();
        v.settings.window0 = v.settings.window1 = false;
        v.settings.blend.set_control(0);
        v.settings.blend.evy = 0;
        v.settings.windows[0] = v.settings.windows[1] = {};
        v.line_effect_on = false;
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        auto& v = g.video().settings;
        const s32 px = s.player[0].x >> 8;
        if (!brightening_) {
            // 08017874
            v.window0 = v.window1 = true;
            v.inside[0] = v.inside[1] = 0x1F;
            v.outside = 0x3F;
            v.inside_sprite_window = 0;
            darkness_ = s16(darkness_ - 0x40);
            if (darkness_ < 0x800) {
                darkness_ = 0x800;
            }
            v.blend.evy = u8(s8(darkness_ >> 8));
            if (px >= 0x559) {
                brightening_ = true;
            } else if ((s.player[0].flags & player_flag::dead) || s.start_pressed) {
                end(g);
            }
            return;
        }
        // 08017920
        darkness_ = s16(darkness_ - 0x40);
        if (darkness_ < 0 || px >= 0x5D1 || (s.player[0].flags & player_flag::dead) ||
            s.start_pressed) {
            end(g);
            return;
        }
        v.blend.evy = u8(s8(darkness_ >> 8));
    }

private:
    void end(Game& g) {
        g.scheduler().destroy(*left_);
        g.scheduler().destroy(*right_);
        g.scheduler().destroy(*this);
    }

    Game& game_;
    Searchlight* left_{};   // +00
    Searchlight* right_{};  // +04
    s16 darkness_ = 0x1000; // +08 the brightness effect's level (8.8)
    bool brightening_{};
};

// 0803CF68: the act's processes.
void set_up_act(Game& g) {
    StageState& s = g.stage();
    g.scheduler().create<StageProcess>(g);
    s.ring_sparkles = 0;
    s.flags =
        u16((s.flags & ~stage_flag::upside_down) | stage_flag::paused | stage_flag::title_card);
    // 0803CB18
    g.scheduler().create<MusicManager>();
    s.music_off = s.music_queued = s.music_invincible = s.music_one_up = s.music_drowning = 0;
    if (g.game_mode == 6) {
        throw Unrecovered("the chao hunt act (0801B544)");
    }
    TitleCard& card = start_title_card(g);
    s.unknown_4fe0 = 0;
    start_player(g, s.player[0], u8(s.zone));
    if (s.players == 2 && s.zone != 0xD) {
        start_player(g, s.player[1], u8(s.zone));
    }
    set_up_act_view(g, s.zone);
    s.flags &= ~u16(stage_flag::frozen | stage_flag::countdown);
    if (g.game_mode > 3) {
        s.flags |= stage_flag::countdown;
    }
    start_rings(g);
    start_spawner(g);
    if ((s.zone >> 1) != 3) {
        set_water(g, -1, 0, 0);
    }
    start_zone(g);
    if (s.zone == 2 && s.player[0].start_x < 0x559) {
        g.scheduler().create<SecretBaseLights>(g);
        leave_blending_to_others(card);
    }
    g.scene = {};
    g.scene.centre_x = g.scene.focus_x = 0x78;
    g.scene.centre_y = g.scene.focus_y = 0x50;
    g.scene.depth = 0x80;
    s.player_blinks.fill(0xFF);
}

} // namespace

void create_color_cycle(Game& g, u16 priority, u16 animation, u8 variant) {
    g.scheduler().create<ColorCycle>(priority, animation, variant);
}

void request_pause(Game& g) {
    start_pause(g);
}

void restart_act(Game& g) {
    set_up_act(g);
}

void start_act(Game& g) {
    StageState& s = g.stage();
    s.unknown_501c = 0;
    s.checkpoint = 0;
    s.rings = 0;
    s.players = 1;
    if (g.game_mode >= 2) {
        throw Unrecovered("the act start of multiplayer (0803CE20)");
    }
    place_player(g, s.player[0], s.character, u8(s.zone), 0);
    // Act 13 is played alone.
    if (s.tails_follows && s.character == 0 && s.zone != 0xD) {
        s.players = 2;
        place_player(g, s.player[1], 1, u8(s.zone), 1);
    }
    s.ticks = 0;
    s.flags &= ~stage_flag::upside_down;
    s.frames = 0;
    s.clock = 0;
    set_up_act(g);
}

void player_died(Game& g) {
    StageState& s = g.stage();
    const bool time_over = !g.save().time_limit_off &&
                           (s.clock > 36000 || ((s.flags & stage_flag::countdown) && s.clock == 0));
    if (time_over) {
        // 0803D638
        s.flags |= stage_flag::title_card;
        if (g.game_mode == 1) {
            throw Unrecovered("the time attack's time over (0806A170)");
        }
        if (--s.lives == 0) {
            throw Unrecovered("the game over after a time over (08055D9C)");
        }
        throw Unrecovered("the time over (0805B9B4)");
    }
    s.rings = 0;
    // 0803D55C
    if (g.game_mode == 1) {
        throw Unrecovered("the time attack's restart (0806A170)");
    }
    if (g.game_mode < 2 && --s.lives == 0) {
        s.flags |= stage_flag::paused | stage_flag::title_card;
        if (g.continues < 1) {
            start_game_over(g, 2);
        } else {
            --g.continues;
            start_game_over(g, 1);
        }
        return;
    }
    s.flags |= stage_flag::title_card;
    g.clear_screen_processes();
    set_up_act(g);
}

void start_game_act(Game& g) {
    StageState& s = g.stage();
    s.score = 0;
    s.lives = 3;
    s.previous_zone = s.zone;
    if (g.game_mode > 1) {
        s.lives = 1;
    }
    if (u8(g.game_mode - 1) < 3 || (s.flags & stage_flag::demo)) {
        s.saved_5178 = g.save().unknown_5178;
        g.save().unknown_5178 = 0;
    }
    set_button_layout(g, g.save().button_layout);
    start_act(g);
}

} // namespace sonic_advance_gba
