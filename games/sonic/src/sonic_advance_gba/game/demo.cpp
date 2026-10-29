#include "sonic_advance_gba/game/demo.hpp"
#include "sonic_advance_gba/game/game.hpp"
#include "sonic_advance_gba/game/stage.hpp"

namespace sonic_advance_gba {

namespace {

constexpr u32 demo_inputs = 0x087B97E8;        // per demo: LZ77 recorded buttons
constexpr u32 demo_characters = 0x080B832F;    // per demo
constexpr u32 demo_acts = 0x080B832B;          // per demo
constexpr u32 overlay_animations = 0x080B83E4; // by language: the "DEMO" text

// 08012874: the overlay and the demo's end.
class DemoOverlay final : public Process {
public:
    explicit DemoOverlay(Game& g) : Process(1), game_(g) {
        const Assets& a = g.assets();
        StageState& s = g.stage();
        const bool blending = (g.video().settings.blend.control() & 0xC0) == 0x40;
        const u32 flags = blending ? 0x1000 : 0x1080;
        time_limit_off_ = g.save().time_limit_off;
        s.flags |= stage_flag::demo;
        label_.start(g.sprite_tiles.allocate(0x2E),
                     a.u16(overlay_animations + 2u * g.save().language), 0, 0x78, 0x71, 0x3C0,
                     flags);
        animate(g, label_);
        hint_.start(g.sprite_tiles.allocate(3), 0x2D1, 0, 0x78, 0x50, 0x3C0, flags);
        animate(g, hint_);
    }

    void on_destroy() override {
        // 08012C24
        game_.sprite_tiles.free(label_.tiles.to);
        game_.sprite_tiles.free(hint_.tiles.to);
        game_.stage().start_pressed = 0;
        game_.stage().flags &= ~stage_flag::demo;
    }

    void update(Game& g) override {
        switch (state_) {
        case State::playing:
            play(g);
            break;
        case State::fading:
            fade(g);
            break;
        case State::waiting:
            wait(g);
            break;
        }
    }

private:
    enum class State { playing, fading, waiting } state_ = State::playing;

    // 080129EC
    void play(Game& g) {
        StageState& s = g.stage();
        auto& v = g.video().settings;
        const bool start = g.controller().raw & button::start;
        if (start || s.frames > 0x708) {
            if (start) {
                Player& p = s.player[0];
                p.flags |= player_flag::frozen;
                p.held = 0;
                s.start_pressed = 1;
            }
            back_to_title_ = start;
            state_ = State::fading;
            v.blend.set_control(0xBF);
            v.blend.evy = 0;
            s.music_off = 0xFF;
            for (u8 n = 0; n < 4; ++n) {
                g.sound().fade_out(n, 4);
            }
        }
        const auto set_mode = [&](Sprite& sp) {
            if (v.blend.evy == 0) {
                sp.flags &= ~sprite_flag::mode;
            } else {
                sp.flags |= 0x80;
            }
        };
        if (s.frames & 0x20) {
            set_mode(label_);
            draw(g, label_);
        }
        set_mode(hint_);
        draw(g, hint_);
    }

    // 08012BE8: to black.
    void fade(Game& g) {
        level_ = u16(level_ + 0x40);
        g.video().settings.blend.evy = u8(level_ >> 8);
        if (level_ > 0xFFF) {
            state_ = State::waiting;
        }
    }

    // 08012B5C: then the SEGA logo (time up) or the title (Start).
    void wait(Game& g) {
        if (++wait_ > 0x2F) {
            g.save().time_limit_off = time_limit_off_;
            g.clear_screen_processes();
            if (!back_to_title_) {
                g.start_sega_logo();
            } else {
                g.start_title(true);
            }
        }
    }

    Game& game_;
    Sprite label_;         // +00
    Sprite hint_;          // +30
    u16 level_{};          // +60
    u16 wait_{};           // +62
    bool back_to_title_{}; // +64
    u8 time_limit_off_{};  // +65
};

} // namespace

void start_demo(Game& g) {
    StageState& s = g.stage();
    const Assets& a = g.assets();
    g.start_demo_playback(a.u32(demo_inputs + 4u * s.demo_index));
    s.character = a.u8(demo_characters + s.demo_index);
    s.zone = a.s8(demo_acts + s.demo_index);
    s.demo_index = u8((s.demo_index + 1) & 3);
    g.game_mode = 0;
    g.scheduler().create<DemoOverlay>(g);
    start_game_act(g);
}

} // namespace sonic_advance_gba
