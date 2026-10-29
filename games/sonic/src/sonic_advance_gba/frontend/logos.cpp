// The SEGA and Sonic Team logos after power-on (0800CFB8-0800D4B0).

#include "sonic_advance_gba/game/game.hpp"

namespace sonic_advance_gba {

namespace {

constexpr u16 sega_logo_asset = 0x56;
constexpr u16 sonic_team_asset = 0x57;
constexpr u16 sonic_team_jingle = 0x66;
constexpr u8 logo_layer = 2;

// Both logos: a 20x10-tile picture at tile (5, 5) of layer 2 (map at
// 0xF000, 512x256, tiles at 0), the layer's map cleared first, nothing shown
// until the first update.
void set_up_logo_screen(Game& g, Background& bg, u16 asset) {
    auto& v = g.video();
    v.settings.blend.set_control(0); // 030011A8
    v.settings.blend.evy = 0;        // 030011AC
    g.reset_disabled = true;         // 03001100 bit 15
    v.settings.set_control(0x40);
    v.settings.bg[logo_layer].set_control(0x5E01);
    v.settings.bg[logo_layer].scroll_x = 0;
    v.settings.bg[logo_layer].scroll_y = 0;
    // The first tile (0x40 bytes of video memory) is cleared at once.
    auto& memory = g.video_memory_now().memory;
    std::fill(memory.begin(), memory.begin() + 0x40, u8(0));
    g.backgrounds().clear_value[logo_layer] = 0;
    g.backgrounds().clears[logo_layer] = {0, 0, 0xFF, 0x40};
    bg.tiles.to = 0;
    bg.map_to = 0xF000;
    bg.asset = asset;
    bg.source_x = bg.source_y = 0;
    bg.x = bg.y = 5;
    bg.width = 0x14;
    bg.height = 10;
    bg.layer = logo_layer;
    load_background(g.assets(), v, g.backgrounds(), bg);
}

// Task 0800D3C4: the SEGA logo unrolls upwards, stays for two seconds and
// rolls away downwards.
class SegaLogo final : public Process {
public:
    explicit SegaLogo(Game& g) : Process(0x2000) {
        set_up_logo_screen(g, background_, sega_logo_asset);
    }

    void update(Game& g) override {
        auto& v = g.video();
        const auto& layer = v.settings.bg[logo_layer];
        switch (state_) {
        case State::unroll: {
            // 0800D098: lines above 160 - n all show the logo's line 160 - n.
            v.settings.bg_shown[logo_layer] = true;
            ++counter_;
            line_scroll_stretch(v, logo_layer, 0, u8(0xA0 - counter_), layer.scroll_x,
                                u16(layer.scroll_y + 0xA0 - counter_));
            line_scroll_fill(v, logo_layer, u8(0xA0 - counter_), 0xA0, layer.scroll_x,
                             layer.scroll_y);
            if (counter_ == 0xA0) {
                state_ = State::show;
                counter_ = 0;
            }
            break;
        }
        case State::show:
            // 0800D3E0
            if (++counter_ == 0x78) {
                state_ = State::roll_away;
                counter_ = 0;
            }
            break;
        case State::roll_away:
            // 0800D11C: the lines below 160 - n show the logo's last line.
            ++counter_;
            line_scroll_fill(v, logo_layer, 0, u8(0xA0 - counter_), layer.scroll_x, layer.scroll_y);
            line_scroll_stretch(v, logo_layer, u8(0xA0 - counter_), 0xA0, layer.scroll_x,
                                u16(layer.scroll_y + 0xA0 - counter_));
            if (counter_ == 0xA0) {
                // 0800D40C: the last frame shows no line effect.
                g.reset_disabled = false;
                v.line_effect_on = false;
                g.scheduler().destroy(*this);
                g.start_sonic_team_logo();
            }
            break;
        }
    }

private:
    enum class State { unroll, show, roll_away } state_ = State::unroll;
    s16 counter_{};         // +00
    Background background_; // +04
};

// Task 0800D434: the Sonic Team logo slides together from alternating
// lines, the jingle plays, and after two seconds the picture darkens.
class SonicTeamLogo final : public Process {
public:
    explicit SonicTeamLogo(Game& g) : Process(0x2000) {
        set_up_logo_screen(g, background_, sonic_team_asset);
    }

    void update(Game& g) override {
        auto& v = g.video();
        const auto& layer = v.settings.bg[logo_layer];
        switch (state_) {
        case State::slide: {
            // 0800D268: lines 40-119 come together from both sides.
            v.settings.bg_shown[logo_layer] = true;
            counter_ = u16(counter_ + 2);
            if (counter_ < 0x118) {
                line_scroll_fill(v, logo_layer, 0, 0x28, layer.scroll_x, layer.scroll_y);
                line_scroll_fill(v, logo_layer, 0x78, 0xA0, layer.scroll_x, layer.scroll_y);
                const s16 offset = s16(0x118 - counter_);
                if (counter_ < 200) {
                    line_scroll_interleave(v, logo_layer, 0x28, 0x78, offset, -1,
                                           s16(layer.scroll_x), layer.scroll_y);
                } else {
                    line_scroll_interleave(v, logo_layer, 0x28, u8(0x40 - counter_), offset, -1,
                                           s16(layer.scroll_x), layer.scroll_y);
                    line_scroll_fill(v, logo_layer, u8(0x40 - counter_), 0x78, layer.scroll_x,
                                     layer.scroll_y);
                }
            } else {
                g.sound().start(sonic_team_jingle);
                v.line_effect_on = false;
                state_ = State::show;
                counter_ = 0;
            }
            break;
        }
        case State::show:
            // 0800D450
            if (counter_++ > 0x78) {
                fade_ = 0;
                state_ = State::fade;
                v.settings.blend.set_control(0xFF); // darken everything
                v.settings.blend.evy = 0;
            }
            break;
        case State::fade:
            // 0800D364: A or Start goes to the title screen.
            if (g.controller().pressed_once & (button::a | button::start)) {
                state_ = State::skip;
                break;
            }
            fade_ = s16(fade_ + 0x80);
            if (fade_ <= 0x1000) {
                v.settings.blend.evy = u8(s8(u16(fade_) >> 8));
            } else {
                // BLDY is set to 1000 (which the hardware reads as 0).
                v.settings.blend.evy = u8(0x1000 & 0x1F);
                g.scheduler().destroy(*this);
                g.start_intro();
            }
            break;
        case State::skip:
            // 0800D4B0
            g.reset_disabled = false;
            v.line_effect_on = false;
            g.scheduler().destroy(*this);
            g.start_title(true);
            break;
        }
    }

private:
    enum class State { slide, show, fade, skip } state_ = State::slide;
    u16 counter_{};         // +00
    Background background_; // +04
    s16 fade_{};            // +44
};

} // namespace

void Game::start_sega_logo() {
    scheduler().create<SegaLogo>(*this);
}

void Game::start_sonic_team_logo() {
    scheduler().create<SonicTeamLogo>(*this);
}

} // namespace sonic_advance_gba
