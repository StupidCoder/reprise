// The title screen and the main menu (0800D4E0-0800E3F0).

#include "sonic_advance_gba/game/fade.hpp"
#include "sonic_advance_gba/game/game.hpp"
#include "sonic_advance_gba/game/sprite.hpp"
#include <array>
#include <string>

namespace sonic_advance_gba {

namespace {

constexpr u16 title_song = 2;
constexpr u16 sound_select = 0x6A, sound_move = 0x6C, sound_leave_menu = 0x1D;
constexpr u8 menu_items = 5;

// Tables of the title and menu.
constexpr u32 press_start_animations = 0x080B8344; // by language
constexpr u32 menu_animations = 0x080B8318;        // by language
constexpr u32 menu_variants = 0x080B831C;          // by item and language
constexpr u32 menu_item_tiles = 0x080B8326;        // tiles per item

// The title's two backgrounds: the picture on layer 0 and the logo on the
// rotated layer 2 (by language).
void set_up_picture(Background& bg) {
    bg.tiles.to = 0;
    bg.map_to = 0xA000;
    bg.asset = 0x4B;
    bg.source_x = bg.source_y = 0;
    bg.x = bg.y = 0;
    bg.width = 0x1E;
    bg.height = 0x14;
    bg.layer = 0;
    bg.flags_bit2 = true;
}

void set_up_logo(Background& bg, u8 language) {
    bg.tiles.to = 0xC000;
    bg.map_to = 0xB000;
    bg.asset = u16(0x49 + language);
    bg.source_x = bg.source_y = 0;
    bg.x = bg.y = 0;
    bg.width = 0x18;
    bg.height = 10;
    bg.layer = 2;
    bg.flags_bit2 = true;
}

// Both screens start from this: mode 1 with layers 0 and 2, the maps of
// layers 0 and 2 cleared (to 0 and FF), the first tiles of both layers'
// tile blocks cleared.
void clear_title_layers(Game& g, u8 layer2_clear) {
    auto& q = g.backgrounds();
    q.clear_value[0] = 0;
    q.clears[0] = {0, 0, 0xFF, 0x20};
    q.clear_value[2] = layer2_clear;
    q.clears[2] = {0, 0, 0xFF, 0x20};
    auto& m = g.video_memory_now().memory;
    const auto& s = g.video().settings;
    const u32 bg0 = s.bg[0].tile_block * 0x4000u, bg2 = s.bg[2].tile_block * 0x4000u + 0x3FC0;
    std::fill(m.begin() + bg0, m.begin() + bg0 + 0x40, u8(0));
    std::fill(m.begin() + bg2, m.begin() + bg2 + 0x80, u8(0));
}

void title_layer_settings(Game& g) {
    auto& s = g.video().settings;
    s.bg[2].set_control(0x568D);
    s.bg[0].set_control(0x1482);
    for (int i = 0; i < 3; ++i) {
        s.bg[std::size_t(i)].scroll_x = 0;
        s.bg[std::size_t(i)].scroll_y = 0;
    }
}

// Task 0800D71C (priority 2000, 0xEC bytes, destructor 0800E020).
class TitleScreen final : public Process {
public:
    TitleScreen(Game& g, bool with_music) : Process(0x2000), game_(g) {
        auto& v = g.video();
        v.settings.set_control(0x41);
        title_layer_settings(g);
        clear_title_layers(g, 0xFF);
        fade_ = 0x1000;
        clock_ = 0;
        v.settings.blend.set_control(0x20A5);
        v.settings.blend.evy = u8(s8(u16(fade_) >> 8) & 0x1F);
        const Assets& a = g.assets();
        const u8 language = g.save().language;
        press_start_.start(g.sprite_tiles.allocate(0x2E),
                           a.u16(press_start_animations + 2u * language), 0, 0x78, 0x71, 0x400,
                           0x1000);
        animate(g, press_start_);
        copyright_.start(g.sprite_tiles.allocate(0x5A), 0x30C, 1, 0, 0x86, 0x400, 0x1000);
        animate(g, copyright_);
        set_up_picture(picture_);
        load_background(a, v, g.backgrounds(), picture_);
        if (with_music) {
            g.sound().start(title_song);
        }
    }

    void on_destroy() override {
        // 0800E020
        game_.sprite_tiles.free(press_start_.tiles.to);
        game_.sprite_tiles.free(copyright_.tiles.to);
    }

    void update(Game& g) override {
        auto& v = g.video();
        switch (state_) {
        case State::fade_in: {
            // 0800D71C: the picture comes out of white; the logo is loaded.
            v.settings.sprites_shown = true;
            v.settings.bg_shown[0] = true;
            if (fade_ == 0x1000) {
                set_up_logo(logo_, g.save().language);
                load_background(g.assets(), v, g.backgrounds(), logo_);
            }
            fade_ = s16(fade_ - 0x55);
            v.settings.blend.evy = u8(s8(u16(fade_) >> 8) & 0x1F);
            if (fade_ < 1) {
                zoom_ = 0x400;
                zoom_speed_ = -0x10;
                v.settings.blend.evy = 0;
                state_ = State::zoom;
            }
            break;
        }
        case State::zoom:
            // 0800D7EC: the logo shrinks from four times its size.
            v.settings.bg_shown[2] = true;
            zoom_ = s16(zoom_ + zoom_speed_);
            zoom_speed_ = s16(zoom_speed_ - 8);
            if (zoom_ < 0x100) {
                zoom_ = 0x100;
                clock_ = 0;
                state_ = State::press_start;
            }
            rotate_layer(v.settings.bg[2], g.assets(), 0, zoom_, zoom_, 0x8D, 0x1A, 0xC0, 0x1E);
            break;
        case State::press_start:
            // 0800D878: "PRESS START" blinks after a second; Start opens
            // the menu, waiting 15 seconds starts a demo.
            clock_ = u16(clock_ + 1);
            if (clock_ > 0x3C) {
                if (g.controller().pressed_once & button::start) {
                    g.sound().start(sound_select);
                    state_ = State::to_menu;
                    break;
                }
                if (((clock_ - 0x3C) & 0x20) == 0) {
                    draw(g, press_start_);
                }
            }
            draw(g, copyright_);
            if (clock_ >= 0x385) {
                state_ = State::to_demo;
            }
            break;
        case State::to_menu:
            // 0800E008
            g.scheduler().destroy(*this);
            g.start_main_menu(false);
            break;
        case State::to_demo:
            // 0800DF88
            g.scheduler().destroy(*this);
            g.start_demo();
            break;
        }
    }

private:
    enum class State { fade_in, zoom, press_start, to_menu, to_demo } state_ = State::fade_in;
    Game& game_;
    Sprite press_start_; // +00
    Sprite copyright_;   // +30
    Background picture_; // +60
    Background logo_;    // +A0
    s16 fade_{};         // +E0
    u16 clock_{};        // +E2
    s16 zoom_{};         // +E4
    s16 zoom_speed_{};   // +E6
};

// Task 0800DB6C (priority 2000, 0x1B4 bytes): GAME START, VS, TIME
// ATTACK, OPTIONS, TINY CHAO GARDEN.
class MainMenu final : public Process {
public:
    MainMenu(Game& g, bool load) : Process(0x2000), load_(load) {
        auto& v = g.video();
        g.game_mode = 0;
        g.continues = 0;
        v.settings.set_control(0x1541);
        title_layer_settings(g);
        cursor_.tiles.to = 0x40;
        cursor_.animation = 1;
        cursor_.x = 0x78;
        cursor_.y = 0x50;
        cursor_.flags = 0x10000;
        if (load) {
            clear_title_layers(g, 0xFF);
        }
        selected_ = 0;
        set_up_picture(picture_);
        if (load) {
            load_background(g.assets(), v, g.backgrounds(), picture_);
        }
        set_up_logo(logo_, g.save().language);
        if (load) {
            load_background(g.assets(), v, g.backgrounds(), logo_);
        }
        rotate_layer(v.settings.bg[2], g.assets(), 0, 0x100, 0x100, 0x8D, 0x1A, 0xC0, 0x1E);
        fade_ = {0, 1, 2, 0, 0x200, 1};
        if (load) {
            step_fade(v, fade_);
        }
    }

    void update(Game& g) override {
        switch (state_) {
        case State::appear:
            appear(g);
            break;
        case State::choose:
            choose(g);
            break;
        case State::fade_out:
            fade_out(g);
            break;
        case State::leave:
            leave(g);
            break;
        }
    }

private:
    enum class State { appear, choose, fade_out, leave };

    // 0800DB6C: after the fade (when loading), the items appear.
    void appear(Game& g) {
        bool ready = true;
        if (load_) {
            ready = step_fade(g.video(), fade_);
        }
        if (!ready) {
            return;
        }
        if (cursor_.tiles.bytes == 0) {
            // From the next frame on the menu takes input (0800E044 switches
            // to 0800DCFC and runs it at once).
            cursor_.start(0x10000, 0x2BB, 0, 0, 0, 0x3C0, 0x400);
            state_ = State::choose;
        }
        const Assets& a = g.assets();
        const u8 language = g.save().language;
        for (u8 i = 0; i < menu_items; ++i) {
            Sprite& s = items_[i];
            s.start(g.sprite_tiles.allocate(a.u8(menu_item_tiles + i)),
                    a.u16(menu_animations + 2u * language), a.u8(menu_variants + language + i * 2u),
                    0x78, s16(i * 0xE + 0x5D), 0x3C0, 0);
            animate(g, s);
        }
        auto& c = g.controller_settings();
        c.first_delay.fill(20);
        c.repeat_delay.fill(8);
        auto& v = g.video();
        v.settings.window0 = v.settings.window1 = false;
        v.settings.blend.set_control(0);
        v.settings.blend.evy = 0;
    }

    // 0800DCFC: Up and Down move the cursor (repeating), A chooses.
    void choose(Game& g) {
        const u16 pressed = g.controller().pressed;
        if (pressed & button::up) {
            g.sound().start(sound_move);
            if (u8(--selected_) > 4) {
                selected_ = 4;
            }
        } else if (pressed & button::down) {
            g.sound().start(sound_move);
            if (u8(++selected_) > 4) {
                selected_ = 0;
            }
        }
        cursor_.x = 0x48;
        cursor_.y = s16(selected_ * 0xE + 0x6B);
        for (;;) {
            const u32 r = animate(g, cursor_);
            if (r == 1 || r == 0) {
                break;
            }
        }
        for (u8 i = 0; i < menu_items; ++i) {
            Sprite& s = items_[i];
            // The chosen item takes palette 1 (GAME START: 0); the others 0
            // (GAME START: FF, which wraps to palette 15).
            if (i == selected_) {
                s.palette = i != 0 ? 1 : 0;
            } else {
                s.palette = i == 0 ? 0xFF : 0;
            }
            animate(g, s);
            draw(g, s);
        }
        if (g.controller().pressed_once & button::a) {
            g.sound().start(sound_select);
            state_ = State::fade_out;
            fade_out_level_ = 0;
            auto& v = g.video();
            v.settings.blend.set_control(selected_ == 4 ? 0xBF : 0xFF);
            v.settings.blend.evy = 0;
        }
    }

    // 0800DE44: the screen goes to white (Tiny Chao Garden) or black.
    void fade_out(Game& g) {
        auto& v = g.video();
        fade_out_level_ = u16(fade_out_level_ + 0x55);
        if (fade_out_level_ >= 0x1000 && fade_out_level_ < 0x8000) {
            fade_out_level_ = 0x1000;
            state_ = State::leave;
            auto& q = g.backgrounds();
            q.clear_value[0] = 0;
            q.clears[0] = {0, 0, 0xFF, 0x20};
            q.clear_value[2] = 0;
            q.clears[2] = {0, 0, 0xFF, 0x20};
            v.line_effect_on = false;
        }
        v.settings.blend.evy = u8(s8(fade_out_level_ >> 8) & 0x1F);
        for (auto& s : items_) {
            draw(g, s);
        }
    }

    // 0800DEE4
    void leave(Game& g) {
        for (auto& s : items_) {
            g.sprite_tiles.free(s.tiles.to);
        }
        g.scheduler().destroy(*this);
        g.sound().start(sound_leave_menu);
        g.sound().restart_tracks(0);
        g.sound().stop(sound_leave_menu);
        if (selected_ == 0) {
            g.game_mode = 0;
            g.start_character_select(0);
        } else {
            auto& v = g.video();
            v.settings.window0 = v.settings.window1 = false;
            v.settings.blend.set_control(0);
            v.settings.blend.evy = 0;
            static constexpr const char* names[4] = {"VS (0800E470)", "time attack (08010078)",
                                                     "options (080105EC)",
                                                     "Tiny Chao Garden (0800E060)"};
            throw Unrecovered(std::string("the main menu's ") + names[selected_ - 1]);
        }
    }

    State state_ = State::appear;
    bool load_;
    Sprite cursor_;                        // +00
    std::array<Sprite, menu_items> items_; // +30
    Background picture_;                   // +120
    Background logo_;                      // +160
    ScreenFade fade_;                      // +1A0
    u8 selected_{};                        // +1AE
    u16 fade_out_level_{};                 // +1B0
};

} // namespace

void Game::start_title(bool with_music) {
    // 0800D4E0
    scheduler_.create<TitleScreen>(*this, with_music);
}

void Game::start_main_menu(bool load) {
    scheduler_.create<MainMenu>(*this, load);
}

} // namespace sonic_advance_gba
