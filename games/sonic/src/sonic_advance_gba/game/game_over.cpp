// The game over (08055D9C-08056F9C): "GAME" and "OVER" slide in from the
// right over the stopped act, the screen fades, the two words fold away;
// when the jingle has ended the save is written and the game starts over
// from the SEGA logo.

#include "sonic_advance_gba/game/game_over.hpp"
#include "sonic_advance_gba/game/fade.hpp"
#include "sonic_advance_gba/game/game.hpp"
#include "sonic_advance_gba/game/graphics.hpp"
#include "sonic_advance_gba/game/math.hpp"
#include "sonic_advance_gba/game/stage.hpp"

namespace sonic_advance_gba {

namespace {

constexpr u16 words_animation = 0x2CB; // variants 0 "GAME", 1 "OVER"
constexpr u16 game_over_jingle = 0x25;

// 08055C1C: three sprite pictures (slots 4, 5 and 0Eh).
void load_pictures(Game& g, u32 tiles_a, u32 tiles_b, u32 tiles_c) {
    if (g.screen_graphics) {
        g.screen_graphics->reset_pool();
    }
    GraphicsLoad l;
    l.color_bytes = 0x20;
    l.palette = 6;
    l.parts = load::tiles | load::colors | load::sprite;
    l.key = 4;
    l.extra = {1, 0, 2, 2};
    l.tiles_to = tiles_b;
    l.tiles_from = 0x086661F8;
    l.tile_bytes = 0x500;
    l.colors_from = 0x08666778;
    l.slot = 4;
    l.options = 0x20;
    load_graphics(g, l);
    l.key = 0x10;
    l.extra = {2, 0, 4, 4};
    l.tiles_to = tiles_a;
    l.tiles_from = 0x08666798;
    l.tile_bytes = 0x200;
    l.colors_from = 0x08666998;
    l.slot = 5;
    l.options = 0x21;
    load_graphics(g, l);
    l.tiles_to = tiles_c;
    l.tiles_from = 0x08674F38;
    l.colors_from = 0x08675138;
    l.slot = 0xE;
    l.options = 0x2D;
    load_graphics(g, l);
}

// 08056DF0, 08056E30: after two seconds the view fades out (window 0 over
// the whole screen). It holds the pictures' tiles (08056EFC).
class GameOverFade final : public Process {
public:
    GameOverFade(Game& g, u32 tiles_a, u32 tiles_b, u32 tiles_c)
        : Process(0x2000), game_(g), tiles_a_(tiles_a), tiles_b_(tiles_b), tiles_c_(tiles_c) {
        fade_ = {.delay = 0, .window = 1, .kind = 1, .level = 0, .speed = 0x60, .blend = 1};
    }

    void on_destroy() override {
        game_.sprite_tiles.free(tiles_a_);
        game_.sprite_tiles.free(tiles_c_);
        game_.sprite_tiles.free(tiles_b_);
    }

    void update(Game& g) override {
        auto& v = g.video().settings;
        if (done_) {
            g.scheduler().destroy(*this);
            return;
        }
        if (!fading_) {
            if (++timer_ > 0x77) {
                v.set_control(v.control() & 0x1FFF);
                fading_ = true;
            }
            return;
        }
        const u16 progress = u16(divide(s32(u16(timer_ - 0x78)) << 5, 0x60));
        step_fade(g.video(), fade_);
        v.window0 = true;
        ++timer_;
        if (progress > 0x5F) {
            done_ = true;
        }
    }

private:
    Game& game_;
    ScreenFade fade_; // +00
    u32 tiles_a_;     // +0C
    u32 tiles_b_;     // +10
    u32 tiles_c_;     // +14
    s16 timer_{};     // +18
    bool fading_{}, done_{};
};

// 08056050: the two words.
class GameOver final : public Process {
public:
    GameOver(Game& g, u8 mode) : Process(0x2000), game_(g), mode_(mode) {
        game_word_.start(g.sprite_tiles.allocate(0x40), 0, 0, 0x100, 0, 3, 0x80);
        g.sound().start(game_over_jingle);
        if (mode & 3) {
            game_word_.animation = words_animation;
            game_word_.variant = 0;
        } else if (mode & 4) {
            game_word_.animation = words_animation;
            game_word_.variant = 2;
        }
        over_word_.start(g.sprite_tiles.allocate(0x40), 0, 0, 0x14, 0x78, 3, 0x80);
        if (mode & 3) {
            over_word_.animation = words_animation;
            over_word_.variant = 1;
        } else if (mode & 4) {
            over_word_.animation = words_animation;
            over_word_.variant = 3;
        }
    }

    // 08056C80
    void on_destroy() override {
        game_.sprite_tiles.free(game_word_.tiles.to);
        game_.sprite_tiles.free(over_word_.tiles.to);
        auto& b = game_.video().settings.blend;
        b.set_control(0xFF);
        b.evy = 0x10;
    }

    void update(Game& g) override {
        switch (state_) {
        case State::waiting: // 08056050
            place(0x130);
            if (++timer_ > 0x3B) {
                game_word_.flags = 0;
                over_word_.flags = 0;
                state_ = State::sliding;
            }
            show(g);
            break;
        case State::sliding: // 080560CC
            place(s16(0x130 - divide((timer_ - 0x3C) * 0xB8, 0x1E)));
            if (++timer_ > 0x59) {
                state_ = State::holding;
            }
            show(g);
            break;
        case State::holding: // 08056158
            place(0x80);
            if (++timer_ > 0x77) {
                game_word_.flags = 0x80;
                over_word_.flags = 0x80;
                if (mode_ & 1) {
                    throw Unrecovered("the game over before a continue (08056314)");
                }
                if (!(mode_ & 2)) {
                    throw Unrecovered("the time over's words (08056590)");
                }
                state_ = State::folding;
            }
            show(g);
            break;
        case State::folding:
            fold(g);
            break;
        case State::ending:
            end(g);
            break;
        }
    }

private:
    enum class State : u8 { waiting, sliding, holding, folding, ending };

    void place(s16 x) {
        game_word_.x = over_word_.x = x;
        game_word_.y = over_word_.y = 0x3C;
    }

    void show(Game& g) {
        animate(g, game_word_);
        draw(g, game_word_);
        animate(g, over_word_);
        draw(g, over_word_);
    }

    // 080561E4: the words narrow to nothing (from frame D9h), then a pause.
    void fold(Game& g) {
        StageState& s = g.stage();
        const s16 t = ++timer_;
        if (t > 0xD8) {
            over_word_.flags = u32(s.next_rotation_set++) | 0xA0;
            game_word_.flags = u32(s.next_rotation_set++) | 0xA0;
            const u32 turn = u32(s32(u32(0x20 - u16(t - 0xF8)) << 16) >> 12) + 0x100;
            s16 width = s16(sine(g.assets(), turn) >> 6);
            if (width < 10) {
                width = 10;
            }
            transform(g, game_word_, 0, width, 0x100, 0x7F, 0x3C);
            transform(g, over_word_, 0, width, 0x100, 0x7F, 0x3C);
        }
        if (t < 0xE9) {
            show(g);
        } else if (t > 0xF7) {
            state_ = State::ending;
        }
    }

    // 08056A94: once the jingle has ended, the save is written and the
    // game starts over.
    void end(Game& g) {
        auto& b = g.video().settings.blend;
        b.set_control(0xFF);
        b.evy = 0x10;
        if (g.sound().playing(0)) {
            return;
        }
        g.sound().stop_all();
        g.sound().restart_output();
        auto& v = g.video().settings;
        v.set_control(v.control() & 0x1FFF);
        g.clear_screen_processes();
        if (continuing_) {
            start_game_act(g);
        } else {
            g.start_sega_logo();
        }
    }

    Game& game_;
    u8 mode_;           // +7C
    Sprite game_word_;  // +00
    Sprite over_word_;  // +3C
    s16 timer_{};       // +78
    bool continuing_{}; // +80
    State state_ = State::waiting;
};

} // namespace

void start_game_over(Game& g, u8 mode) {
    StageState& s = g.stage();
    if (s.zone == 10) {
        s.camera.locks |= camera_lock::far_still;
    }
    auto& v = g.video().settings;
    v.blend.set_control(0);
    v.blend.evy = 0;
    v.windows[0] = v.windows[1] = {};
    g.scheduler().create<GameOver>(g, mode);
    if (mode & 5) {
        throw Unrecovered("the continue screen (08056F20)");
    }
    if (!(mode & 2)) {
        return;
    }
    const u32 a = g.sprite_tiles.allocate(0x10);
    const u32 b = g.sprite_tiles.allocate(0x10);
    const u32 c = g.sprite_tiles.allocate(0x28);
    g.scheduler().create<GameOverFade>(g, b, c, a);
    load_pictures(g, b, c, a);
}

} // namespace sonic_advance_gba
