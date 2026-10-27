#include "mindwalker_amiga/title/title_stage.hpp"
#include "amiga/intuition_imagery.hpp"
#include <algorithm>
#include <string>

namespace mindwalker::title {
namespace {

// T's data (research layout, .analysis/mindwalker_amiga/game.md).
constexpr std::uint32_t new_window = 0x60156A;      // the window and its gadget list
constexpr std::uint32_t menu_colours = 0x600AD8;    // 16 base colours; their values double as pens
constexpr std::uint32_t title_colours = 0x6032B0;   // the title picture's colours
constexpr std::uint32_t twinkle_colours = 0x6032D0; // the twinkle's working copy
constexpr std::uint32_t picture_planes = 0x603350;  // MindB's planes -> bitplanes
constexpr std::uint32_t credit_lists[][3] = {        // PrintIText lists and their positions ($602D4C)
    {0x6033B6, 4, 170}, {0x60340E, 16, 135}, {0x603460, 4, 56}, {0x603480, 4, 37}};
constexpr std::uint32_t twinkle_random = 0x603494, twinkle_choice_random = 0x603498, twinkle_table = 0x60349C;
constexpr std::uint32_t initial_seed = 0x600AF8;
constexpr std::uint32_t item_tops = 0x600AFC, item_texts = 0x600B3E, item_lengths = 0x600B52;
constexpr std::uint32_t pillar_x = 0x600B66, pillar_colour = 0x600B86;
constexpr std::uint32_t progress_table = 0x600BA6, preset_seeds = 0x600BC6;
constexpr std::uint32_t loading_points = 0x604C9C, loading_text = 0x604CF0;
constexpr std::uint32_t disk_image = 0x601338;      // copied to chip memory for the disk gadget ($601014)
// S's icons that T puts into its gadget images ($600040: +$300 the pause box,
// +$304 the slider's knob).
constexpr std::uint32_t pause_box_icon = 0x700A40, knob_icon = 0x700A54;

constexpr int playfield_top = 11; // BorderTop + 1 ($601628; hypothesis: the top-border gadgets make it 10)
constexpr int menu_items = 5, pillars = 8;
constexpr std::uint32_t zero_seed = 0xFED45123; // used instead of a zero shift register

enum GadgetId { pause_box = 16, slider = 32, disk = 48, fame = 64, quit = 128 };

std::vector<std::uint8_t> bytes_at(const amiga::LoadedImage& program, std::uint32_t at, std::size_t count) {
    std::vector<std::uint8_t> out(count);
    for (std::size_t i = 0; i < count; ++i) out[i] = program.u8(at + std::uint32_t(i));
    return out;
}

std::vector<screen::Rgb4> colours_at(const amiga::LoadedImage& program, std::uint32_t at, int count) {
    std::vector<screen::Rgb4> out;
    for (int i = 0; i < count; ++i) out.push_back(program.u16(at + std::uint32_t(i * 2)) & 0xFFF);
    return out;
}

std::string c_string(const amiga::LoadedImage& program, std::uint32_t at) {
    std::string text;
    for (char c; (c = char(program.u8(at))) != 0; ++at) text += c;
    return text;
}

} // namespace

TitleStage::TitleStage(const data::GameFiles& files, std::span<const std::uint8_t> title_picture,
                       std::uint32_t clock_seed, bool first)
    : t_(files.program(data::Stage::title)), picture_file_(title_picture), clock_seed_(clock_seed), first_(first) {
    const auto& setup = files.program(data::Stage::setup);
    bar_ = screen::GadgetBar(t_, new_window,
                             {{0x6013A6, bytes_at(setup, pause_box_icon, 20)},
                              {0x601392, bytes_at(setup, knob_icon, 24)},
                              {0x6013CE, bytes_at(t_, disk_image, 80)}});
    // $601082: the disk and Fame gadgets start disabled.
    bar_.find(disk)->enabled = false;
    bar_.find(fame)->enabled = false;
    title_colours_ = colours_at(t_, title_colours, 16);
    twinkle_work_ = colours_at(t_, twinkle_colours, 16);
    twinkle_pick_.state = t_.u32(twinkle_random);
    twinkle_colour_pick_.state = t_.u32(twinkle_choice_random);
    seed_.state = t_.u32(initial_seed);
    canvas_.clear(0);
    bar_.draw(canvas_);
    if (first_) {
        draw_title();
    } else {
        enter_menu();
    }
}

// $602C70: the picture and the credits on the title colours; the twinkle
// starts after 80 frames ($6034E8 = -1, h16 = $50).
void TitleStage::draw_title() {
    canvas_.fill(0, playfield_top, screen::Canvas::width - 1, screen::Canvas::height - 1, 0);
    constexpr int width = 320, rows = 160, plane_bytes = width / 8 * rows, first_row = 15;
    for (int p = 0; p < 5; ++p) {
        const int bitplane = t_.s32(picture_planes + std::uint32_t(p * 4));
        for (int y = 0; y < rows; ++y) {
            for (int x = 0; x < width; ++x) {
                if (picture_file_[std::size_t(p * plane_bytes + y * (width / 8) + x / 8)] >> (7 - x % 8) & 1) {
                    canvas_.plot(x, first_row + y, canvas_.pen(x, first_row + y) | 1 << bitplane);
                }
            }
        }
    }
    // The credits: IntuiText lists drawn with PrintIText; each text's own
    // TextAttr picks topaz 8 or 9 and the algorithmic style. The "X" that
    // starts the copyright lines becomes "©" ($602D4C).
    for (const auto& list : credit_lists) {
        for (std::uint32_t t = list[0]; t != 0; t = t_.u32(t + 16)) {
            const auto attr = t_.u32(t + 8);
            const bool topaz9 = t_.u16(attr + 4) == 9;
            std::string text = c_string(t_, t_.u32(t + 12));
            if (text.starts_with("X ")) text[0] = char(0xA9);
            canvas_.text(int(list[1]) + t_.s16(t + 4), int(list[2]) + t_.s16(t + 6) + 6, text, t_.u8(t),
                         topaz9 ? screen::TextFont::topaz9 : screen::TextFont::topaz8, t_.u8(attr + 6), t_.u8(t + 2) == 1,
                         t_.u8(t + 1));
        }
    }
    colours_ = twinkle_work_;
    twinkle_colour_ = -1;
    twinkle_last_ = 0;
    twinkle_delay_ = 0x50;
    phase_ = Phase::title;
    phase_frames_ = 0;
}

// $602B34: now and then a colour of the title picture flashes white and fades
// back in 15 steps; never the same colour twice in a row if two more picks
// can avoid it.
void TitleStage::twinkle() {
    if (twinkle_colour_ == -1) {
        if ((twinkle_pick_.next() & 0x296) == 0) {
            const auto pick = [&] { return t_.s32(twinkle_table + std::uint32_t((twinkle_colour_pick_.next() & 15) * 4)); };
            twinkle_colour_ = pick();
            if (twinkle_colour_ == twinkle_last_) twinkle_colour_ = pick();
            if (twinkle_colour_ == twinkle_last_) twinkle_colour_ = pick();
            twinkle_level_ = 0xFFF;
            twinkle_last_ = twinkle_colour_;
        }
        return;
    }
    twinkle_level_ -= 0x111;
    const auto c = std::size_t(twinkle_colour_);
    if (twinkle_level_ < 1) {
        twinkle_work_[c] = title_colours_[c];
        twinkle_colour_ = -1;
    } else {
        twinkle_work_[c] = screen::Rgb4(twinkle_level_ ^ title_colours_[c]);
    }
    colours_ = twinkle_work_;
}

// After the title ($600128-$6001F6): the menu colours, the seed mixed from
// the clock (T also sets colour 1 of S's screen to green, for the loading
// picture), the pillar generator seeded from it, the
// "New Game" seed taken from it, then ten frames for the sound check
// ($600A5E) before the menu is drawn.
void TitleStage::enter_menu() {
    canvas_.fill(0, playfield_top, screen::Canvas::width - 1, screen::Canvas::height - 1, 0);
    colours_ = colours_at(t_, menu_colours, 16);
    seed_.state ^= clock_seed_;
    if (seed_.state == 0) seed_.state = zero_seed;
    const auto high = seed_.next();
    const auto low = seed_.next();
    pillar_random_ = world::Mixer::seeded(high, low);
    new_game_seed_ = seed_;
    phase_ = Phase::pause;
    phase_frames_ = 0;
}

// $600558: the grid behind the menu, sixteen 8-pixel stripes each way in the
// blue base pens.
void TitleStage::draw_menu_background() {
    canvas_.fill(0, playfield_top, screen::Canvas::width - 1, screen::Canvas::height - 1, 0);
    for (int i = 0; i < 16; ++i) {
        const int x0 = i * 20, y0 = i * 11 + 20;
        for (int c = 0; c < 8; ++c) {
            const int pen = t_.u16(menu_colours + std::uint32_t((i / 2 + c) * 2));
            canvas_.line(x0 + c, 12, x0 + c, 199, pen);
            canvas_.line(6, y0 + c, 310, y0 + c, pen);
        }
    }
}

// $600694: the five items on 16-row blue bands, text in pen $3F (hold red
// and blue, green 15), then the eight pillars.
void TitleStage::draw_menu() {
    for (int k = 0; k < menu_items; ++k) {
        const int top = t_.s32(item_tops + std::uint32_t(k * 4));
        for (int c = 0; c < 16; ++c) {
            canvas_.line(6, top + c, 310, top + c, t_.u16(menu_colours + std::uint32_t(c * 2)));
        }
        const int length = t_.s32(item_lengths + std::uint32_t(k * 4));
        const auto text = c_string(t_, t_.u32(item_texts + std::uint32_t(k * 4))).substr(0, std::size_t(length));
        canvas_.text(length * -4 + 160, top + 12, text, 0x3F);
    }
    for (int p = 0; p < pillars; ++p) {
        pillar_offset_[std::size_t(p)] = 0;
        draw_pillar(p);
    }
}

// $6007FC: a pillar: a base-colour column, then a column whose red steps up
// every 12 rows (hold-and-modify red), then 15 columns ramping green.
void TitleStage::draw_pillar(int pillar) {
    const int x = t_.s32(pillar_x + std::uint32_t(pillar * 4));
    const int colour = t_.s32(pillar_colour + std::uint32_t(pillar * 4));
    canvas_.line(x, 12, x, 199, t_.u16(menu_colours + std::uint32_t(colour * 2)));
    for (int v = 0; v < 16; ++v) canvas_.line(x + 1, v * 12 + 12, x + 1, 199, 0x20 + v);
    for (int v = 1; v < 16; ++v) canvas_.line(x + 1 + v, 12, x + 1 + v, 199, 0x30 + v);
}

// $60094E: moves a pillar's red bands three steps (up for even pillars, down
// for odd ones) as a triangle wave of the band number plus the offset.
void TitleStage::animate_pillar(int pillar) {
    auto& offset = pillar_offset_[std::size_t(pillar)];
    offset += (pillar & 1) == 0 ? -3 : 3;
    const int x = t_.s32(pillar_x + std::uint32_t(pillar * 4));
    for (int v = 0; v < 16; ++v) {
        int red = (offset + v) & 31; // two's complement, as the original's AND
        if ((offset + v) & 16) red = 31 - red;
        const int top = v * 12 + 12;
        canvas_.line(x + 1, top, x + 1, std::min(top + 11, 199), 0x20 + red);
    }
}

void TitleStage::set_slider(std::uint16_t pot) {
    bar_.find(slider)->horizontal_pot = pot;
}

// Intuition's dragging of the slider's knob (FREEHORIZ, its own knob image):
// a press on the knob grabs it and the knob follows the pointer within the
// container; the pot is the knob's place in its travel scaled to $FFFF.
// (Hypothesis: modelled on Intuition's behaviour, not compared with the
// original yet; clicks in the container beside the knob do nothing here.)
void TitleStage::drag_slider(const Pointer& pointer) {
    auto& gadget = *bar_.find(slider);
    const auto& knob = gadget.images.front();
    const int travel = gadget.width - 8 - knob.width;
    const int knob_x = gadget.x() + 4 + travel * gadget.horizontal_pot / 0xFFFF;
    if (pointer.button && !button_was_down_ && pointer.y >= gadget.top && pointer.y < gadget.top + gadget.height &&
        pointer.x >= knob_x && pointer.x < knob_x + knob.width) {
        slider_held_ = true;
        slider_grab_ = pointer.x - knob_x;
    }
    if (!pointer.button) slider_held_ = false;
    if (slider_held_) {
        const int place = std::clamp(pointer.x - slider_grab_ - (gadget.x() + 4), 0, travel);
        gadget.horizontal_pot = std::uint16_t(place * 0xFFFF / travel);
        bar_.draw(canvas_);
    }
}

void TitleStage::frame(const Pointer& pointer) {
    if (phase_ == Phase::title || phase_ == Phase::menu) drag_slider(pointer);
    switch (phase_) {
    case Phase::title:
        // $603202: one vertical blank, the twinkle (after the delay), then
        // the left button ends the title (held or new).
        if (twinkle_delay_ < 1) {
            twinkle();
        } else {
            --twinkle_delay_;
        }
        read_colours_ = colours_;
        button_was_down_ = pointer.button;
        ++phase_frames_;
        if (pointer.button) enter_menu();
        break;
    case Phase::pause:
        if (++phase_frames_ == 10) {
            draw_menu_background();
            draw_menu();
            bar_.find(disk)->enabled = true;
            bar_.find(fame)->enabled = true;
            bar_.draw(canvas_);
            phase_ = Phase::menu;
            phase_frames_ = 0;
        }
        break;
    case Phase::menu: {
        // The main loop ($600276): gadget messages, one step of the "New
        // Game" seed, the menu click, one pillar's animation.
        const bool press = pointer.button && !button_was_down_;
        if (press && pointer.y < 10) {
            for (const auto& gadget : bar_.gadgets()) {
                if (gadget.enabled && pointer.x >= gadget.x() && pointer.x < gadget.x() + gadget.width &&
                    gadget.id == quit) {
                    phase_ = Phase::quit;
                    return;
                }
            }
        }
        new_game_seed_.next();
        // $6003EE: a new press with the pointer over the column x 127-191
        // picks the item whose band holds y.
        bool chosen = false;
        if (press && pointer.x > 0x7E && pointer.x < 0xC0) {
            for (int k = 0; k < menu_items; ++k) {
                const int top = t_.s32(item_tops + std::uint32_t(k * 4));
                if (top < pointer.y && pointer.y < top + 16) {
                    chosen = true;
                    choice_ = k;
                }
            }
        }
        button_was_down_ = pointer.button;
        animate_pillar(pillar_random_.step() >> 5 & 7);
        ++phase_frames_;
        if (chosen) start_game();
        break;
    }
    case Phase::loading:
    case Phase::quit:
        break;
    }
}

// $6002F2-$6003E8: the new game's record, then the loading screen.
void TitleStage::start_game() {
    if (new_game_seed_.state == 0) new_game_seed_.state = zero_seed;
    NewGame game;
    // The slider's position picks the starting stage in steps of 7, at most 35.
    const auto pot = bar_.find(slider)->horizontal_pot;
    game.stage = std::min(int(pot / 0x618u / 7u * 7u), 35);
    game.progress = t_.u32(progress_table + std::uint32_t(game.stage / 7 * 4));
    game.seed = choice_ < 4 ? t_.u32(preset_seeds + std::uint32_t(choice_ * 4)) : new_game_seed_.state;
    new_game_ = game;
    draw_loading_screen();
    phase_ = Phase::loading;
    phase_frames_ = 0;
}

// $604AB2/$604AF4: on S's one-plane screen (shared +8, which shows once T
// closes its own): cleared, seven threads of the arrow (all 42 for a stage
// past 41) in pen 1, "Loading..." in pen 1 on pen 0, then ShowTitle: the
// screen's title bar in its BlockPen (1) with Intuition's two depth gadgets
// (plane 0 only: S's screen has one plane).
void TitleStage::draw_loading_screen() {
    auto& canvas_ = loading_;
    canvas_.clear(0);
    const int stage = new_game_->stage;
    const int from = stage < 42 ? stage : 0;
    const int to = stage < 42 ? std::min(stage + 7, 42) : 42;
    for (int i = from; i < to; ++i) {
        const int x = t_.u8(loading_points + std::uint32_t(i * 2)), y = t_.u8(loading_points + std::uint32_t(i * 2 + 1));
        canvas_.line(160, 47, x, y, 1);
        canvas_.line(x, y, 160, 165, 1);
        canvas_.line(255, 106, x, y, 1);
        canvas_.line(x, y, 65, 106, 1);
    }
    canvas_.text(120, 30, c_string(t_, loading_text), 1, screen::TextFont::topaz8, screen::plain, true, 0);
    canvas_.fill(0, 0, screen::Canvas::width - 1, 9, 1);
    for (const auto& gadget : amiga::screen_depth_gadgets()) {
        const int x0 = screen::Canvas::width - 1 + gadget.left;
        for (int y = 0; y < gadget.height; ++y)
            for (int x = 0; x < gadget.width; ++x) canvas_.plot(x0 + x, y, gadget.planes[0][std::size_t(y)] >> (31 - x) & 1);
    }
}

platform::Image TitleStage::picture() const {
    // S's screen colours: 0 black, 1 green (S $700040's SetRGB4 calls).
    if (phase_ == Phase::loading) return loading_.picture({0x000, 0x0F0});
    return canvas_.ham_picture(colours_);
}

} // namespace mindwalker::title
