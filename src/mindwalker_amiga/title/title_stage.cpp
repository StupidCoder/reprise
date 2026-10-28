#include "mindwalker_amiga/title/title_stage.hpp"
#include "amiga/intuition_imagery.hpp"
#include <algorithm>
#include <string>

namespace mindwalker::title {
namespace {

// T's data (research layout, .analysis/mindwalker_amiga/game.md).
constexpr std::uint32_t new_window = 0x60156A;      // the window and its gadget list
constexpr std::uint32_t menu_colours = 0x600AD8;    // 16 base colours; their values double as pens
constexpr std::uint32_t screen_title = 0x601526;  // "Click on Pause Box to Resume Game."
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

// The command screen ($601E96 on: texts, lengths, x, y, back pens; $6020C6
// the commands' own back pens; $6020DA the items a click can hit).
constexpr std::uint32_t item_texts_c = 0x601E96, item_lengths_c = 0x601F06, item_x = 0x601F76, item_y = 0x601FE6,
                        item_backs = 0x602056, command_backs = 0x6020C6, clickable = 0x6020DA;
constexpr std::uint32_t band_steps = 0x601C30, band_ham = 0x601CB0; // h9 and $601CB0: the screen's colour bands
// The initials screen ($604652 on: 34 texts, lengths, x, y, pens; the letters).
constexpr std::uint32_t entry_texts = 0x604652, entry_lengths = 0x6046DA, entry_x = 0x604762, entry_y = 0x6047EA,
                        entry_pens = 0x604872, letters = 0x604617;
// The Hall of Fame: IntuiTexts, images (S's +$2D0 frames as data), the
// background's bands, the title's colour cycle, the number divisors.
constexpr std::uint32_t number_text = 0x603C72, rank_mark = 0x603CF4, blank_initials = 0x603E22,
                        initials_text = 0x603E08, fame_title = 0x603CBC, fame_title_cycle = 0x603CDE;
constexpr std::uint32_t fame_images = 0x603E36, fame_rows = 0x603E72, fame_pens = 0x603EB6, fame_cycle = 0x603C18,
                        divisors = 0x603C86;
constexpr std::uint32_t s_small_frames = 0x702E48; // S's +$2D0 frames (the images' data, 4 planes of 14 rows)
constexpr int playfield_top = 10; // BorderTop + 1 ($601628; the command screen's dump: from row 10)
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

HallOfFame HallOfFame::from_file(std::span<const std::uint8_t> bytes) {
    HallOfFame fame;
    for (std::size_t i = 0; i < 32 && i * 8 + 8 <= bytes.size(); ++i) {
        const auto* e = &bytes[i * 8];
        fame.entries[i].score = std::uint32_t(e[0]) << 24 | std::uint32_t(e[1]) << 16 | std::uint32_t(e[2]) << 8 | e[3];
        for (int c = 4; c < 8 && e[c] != 0; ++c) fame.entries[i].initials.push_back(char(e[c]));
    }
    return fame;
}

std::vector<std::uint8_t> HallOfFame::to_file() const {
    std::vector<std::uint8_t> bytes(256);
    for (std::size_t i = 0; i < 32; ++i) {
        const auto score = entries[i].score;
        bytes[i * 8] = std::uint8_t(score >> 24);
        bytes[i * 8 + 1] = std::uint8_t(score >> 16);
        bytes[i * 8 + 2] = std::uint8_t(score >> 8);
        bytes[i * 8 + 3] = std::uint8_t(score);
        for (std::size_t c = 0; c < 3 && c < entries[i].initials.size(); ++c) bytes[i * 8 + 4 + c] = std::uint8_t(entries[i].initials[c]);
    }
    return bytes;
}

TitleStage::TitleStage(const data::GameFiles& files, std::span<const std::uint8_t> title_picture,
                       std::uint32_t clock_seed, bool first, Session session)
    : t_(files.program(data::Stage::title)), picture_file_(title_picture), clock_seed_(clock_seed), first_(first),
      session_(std::move(session)) {
    const auto& setup = files.program(data::Stage::setup);
    bar_ = screen::GadgetBar(t_, new_window,
                             {{0x6013A6, bytes_at(setup, pause_box_icon, 20)},
                              {0x601392, bytes_at(setup, knob_icon, 24)},
                              {0x6013CE, bytes_at(t_, disk_image, 80)}});
    // $601082: the disk and Fame gadgets start disabled; selected, they are
    // complemented on T's six-plane screen (GADGHCOMP).
    bar_.find(disk)->enabled = false;
    bar_.find(fame)->enabled = false;
    bar_.find(disk)->complement = bar_.find(fame)->complement = 0x3F;
    small_image_ = bytes_at(setup, s_small_frames, 112);
    for (std::uint32_t i = 0; i < 28; ++i) {
        for (std::uint32_t c = 0; c < std::uint32_t(t_.s32(item_lengths_c + i * 4)); ++c)
            item_text_[i].push_back(char(t_.u8(t_.u32(item_texts_c + i * 4) + c)));
        item_back_[i] = t_.s32(item_backs + i * 4);
    }
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
    if (shrink_frames_ > 0) { // $600E20's 15 vertical blanks, the window shrunk to its bar
        --shrink_frames_;
        return;
    }
    if (paused_) { // the loop Waits on the window's port: only its gadgets
        const bool press = pointer.button && !button_was_down_;
        button_was_down_ = pointer.button;
        if (press && pointer.y < 10) {
            const int id = gadget_at_pointer(pointer);
            if (id == pause_box) resume();
            if (id == quit) phase_ = Phase::quit; // ($601620 ends the pause loop and then T)
        }
        return;
    }
    if (resume_frames_ > 0) { // $600EE6's 30 vertical blanks, then the screen drawn again
        if (--resume_frames_ == 0) redraw_after_pause();
        return;
    }
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
            phase_ = Phase::menu;
            phase_frames_ = 0;
            if (!first_) check_high_score(); // h17
            if (phase_ == Phase::menu) {
                draw_menu();
                bar_.find(disk)->enabled = true;
            }
            bar_.find(fame)->enabled = phase_ != Phase::initials;
            bar_.draw(canvas_);
        }
        break;
    case Phase::initials: {
        // $6043D2's loop: the button read again and again (h20); a letter,
        // then ten frames' wait.
        if (wait_ > 0) {
            --wait_;
            break;
        }
        const bool press = pointer.button && !button_was_down_;
        if (pause_clicked(pointer, press)) return;
        button_was_down_ = pointer.button;
        if (press && pointer.y < 10 && gadget_at_pointer(pointer) == quit) {
            phase_ = Phase::quit;
            return;
        }
        if (press) initials_click(pointer.x, pointer.y);
        if (typed_ >= 3 || forget_) {
            const bool keep = !forget_;
            if (keep) {
                session_.fame.entries[std::size_t(new_entry_)].initials = initials_;
                if (session_.fame_changed) session_.fame_changed(session_.fame); // h11
            }
            bar_.find(disk)->enabled = true;
            bar_.find(fame)->enabled = true;
            draw_menu_background();
            if (keep) { // h2: the Hall of Fame shown, the disk off (its bands reach into the bar's left column)
                bar_.find(disk)->enabled = false;
                bar_.find(fame)->selected = true;
                bar_.draw(canvas_);
                show_fame();
            } else {
                draw_menu();
                phase_ = Phase::menu;
                bar_.draw(canvas_);
            }
        }
        break;
    }
    case Phase::fame: {
        const bool press = pointer.button && !button_was_down_;
        if (pause_clicked(pointer, press)) return;
        button_was_down_ = pointer.button;
        if (press && pointer.y < 10) gadget_clicked(gadget_at_pointer(pointer));
        if (phase_ == Phase::fame && (++fame_count_ & 1) == 0) animate_fame(); // two vertical blanks a step
        break;
    }
    case Phase::command: {
        if (draw_items_ < 15) { // $601976: one item a frame
            draw_item(draw_items_++);
            break;
        }
        const bool press = pointer.button && !button_was_down_;
        if (pause_clicked(pointer, press)) return;
        button_was_down_ = pointer.button;
        if (press && pointer.y < 10) {
            gadget_clicked(gadget_at_pointer(pointer));
        } else if (press) {
            command_click(pointer.x, pointer.y); // h8
        }
        break;
    }
    case Phase::menu: {
        // The main loop ($600276): gadget messages, one step of the "New
        // Game" seed, the menu click, one pillar's animation.
        const bool press = pointer.button && !button_was_down_;
        if (pause_clicked(pointer, press)) return;
        if (press && pointer.y < 10) {
            const int id = gadget_at_pointer(pointer);
            if (id == quit) {
                phase_ = Phase::quit;
                return;
            }
            if (id == disk || id == fame) {
                button_was_down_ = pointer.button;
                gadget_clicked(id);
                return;
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
void TitleStage::draw_loading_screen() { loading_ = loading_picture(new_game_->stage); }

// (Every stage program draws the same picture on leaving, with the progress
// of the moment: B's, C's $4069F0, P's $506CA0.)
screen::Canvas TitleStage::loading_picture(int stage) const {
    screen::Canvas canvas_;
    canvas_.clear(0);
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
    return canvas_;
}

int TitleStage::gadget_at_pointer(const Pointer& pointer) const {
    for (const auto& gadget : bar_.gadgets())
        if (gadget.enabled && pointer.x >= gadget.x() && pointer.x < gadget.x() + gadget.width && pointer.y >= gadget.top &&
            pointer.y < gadget.top + gadget.height)
            return gadget.id;
    return -1;
}

// h5 / $600CB4: a toggle's message: Fame ($600DA8) shows the Hall of Fame or
// goes back to the menu; the disk ($600D5C) the command screen or back.
void TitleStage::gadget_clicked(int id) {
    if (id == quit) {
        phase_ = Phase::quit;
        return;
    }
    if (id != disk && id != fame) return;
    auto& g = *bar_.find(id);
    g.selected = !g.selected;
    auto& other = *bar_.find(id == disk ? fame : disk);
    other.enabled = !g.selected;
    bar_.refresh(canvas_, disk);
    bar_.refresh(canvas_, fame);
    if (!g.selected) back_to_menu();
    else if (id == fame) show_fame();
    else open_command();
}

// The pause box (a toggle; h5 hands $600DF4 its SELECTED flag), in the
// loops that read the window's messages: the menu, the Hall of Fame, the
// command screen and the initials board.
bool TitleStage::pause_clicked(const Pointer& pointer, bool press) {
    if (!press || pointer.y >= 10 || gadget_at_pointer(pointer) != pause_box) return false;
    button_was_down_ = pointer.button;
    pause();
    return true;
}

// $600E20: the disk and Fame off (ghosted), the window shrunk to its bar,
// 15 frames (then a MoveWindow without visible effect); the loop waits.
void TitleStage::pause() {
    paused_ = true;
    bar_.find(pause_box)->selected = true;
    bar_.find(fame)->enabled = false;
    bar_.find(disk)->enabled = false;
    bar_.draw(canvas_);
    shrink_frames_ = 15;
}

// $600EE6: the window back up and to its size, 30 frames, then the screen
// drawn again.
void TitleStage::resume() {
    paused_ = false;
    bar_.find(pause_box)->selected = false;
    resume_frames_ = 30;
}

void TitleStage::redraw_after_pause() {
    if (bar_.find(disk)->selected) { // $601ADA: the command screen, its choices kept
        draw_command_background();
        draw_items_ = 0;
        bar_.find(disk)->enabled = true;
    } else if (bar_.find(fame)->selected) { // $603728
        bar_.find(fame)->enabled = true;
        show_fame();
    } else if (phase_ != Phase::initials) { // $6004F0
        back_to_menu();
        bar_.find(disk)->enabled = true;
        bar_.find(fame)->enabled = true;
    } else { // $6043B6: the initials board again
        draw_menu_background();
        draw_initials_screen();
    }
    bar_.draw(canvas_);
}

void TitleStage::back_to_menu() { // $6004F0
    draw_menu_background();
    draw_menu();
    phase_ = Phase::menu;
    phase_frames_ = 0;
}

void TitleStage::print_itext(std::uint32_t at, int x, int y, const std::string* text) {
    for (; at != 0; at = t_.u32(at + 16)) {
        std::string s = text ? *text : c_string(t_, t_.u32(at + 12));
        canvas_.text(x + t_.s16(at + 4), y + t_.s16(at + 6) + 6, s, t_.u8(at), screen::TextFont::topaz8, screen::plain,
                     t_.u8(at + 2) == 1, t_.u8(at + 1));
        if (text) break;
    }
}

// $603AFC: a number with commas between thousands (padded to 7 places).
int TitleStage::print_number(std::uint32_t value, int x, int y, bool pad) {
    std::string out;
    bool leading = true;
    for (std::uint32_t k = 0; k < 10; ++k) {
        const std::uint32_t d = t_.u32(divisors + k * 4);
        const std::uint32_t digit = value / d;
        value -= digit * d;
        if (leading) leading = digit == 0;
        if (!leading) {
            out.push_back(char('0' + digit));
            if (k == 0 || k == 3 || k == 6) out.push_back(',');
        }
    }
    int length = int(out.size());
    if (pad && length < 7) {
        x += (7 - length) * 8;
        length = 7;
    }
    print_itext(number_text, x, y, &out);
    return length;
}

// $6035D4: the entries ranked by score (ties: the later entry first).
void TitleStage::sort_fame() {
    std::array<bool, 32> left{};
    left.fill(true);
    rank_.fill(0x20);
    for (int r = 0; r < 32; ++r) {
        int best = 31;
        std::uint32_t high = 0;
        for (int c = 0; c < 32; ++c)
            if (left[std::size_t(c)] && high <= session_.fame.entries[std::size_t(c)].score) {
                high = session_.fame.entries[std::size_t(c)].score;
                best = c;
            }
        left[std::size_t(best)] = false;
        rank_[std::size_t(best)] = r + 1;
        ranked_[std::size_t(r)] = session_.fame.entries[std::size_t(best)];
        ranked_[std::size_t(r)].initials = ranked_[std::size_t(r)].initials.substr(0, 3);
    }
}

// h17: a game's score at least the last entry's takes its place; then the
// initials ($6043D2).
void TitleStage::check_high_score() {
    if (session_.where == 0 || session_.score == 0) return;
    sort_fame();
    int c = 0;
    while (c < 0x1F && rank_[std::size_t(c)] != 0x20) ++c;
    auto& entry = session_.fame.entries[std::size_t(c)];
    if (entry.score > session_.score) return;
    entry.score = session_.score;
    entry.initials.clear();
    sort_fame();
    new_entry_ = c;
    initials_ = "   ";
    typed_ = 0;
    forget_ = false;
    bar_.find(disk)->enabled = false;
    draw_initials_screen();
    phase_ = Phase::initials;
}

void TitleStage::draw_initials_screen() { // $604526, $6042B0
    canvas_.fill(0x1E, 0x19, 0x121, 0xBB, 0xF);
    canvas_.fill(0x23, 0x1E, 0x11C, 0xB6, 0);
    for (std::uint32_t k = 0; k < 0x22; ++k) {
        std::string text;
        for (std::uint32_t c = 0; c < std::uint32_t(t_.s32(entry_lengths + k * 4)); ++c) text.push_back(char(t_.u8(t_.u32(entry_texts + k * 4) + c)));
        canvas_.text(t_.s32(entry_x + k * 4), t_.s32(entry_y + k * 4), text, t_.s32(entry_pens + k * 4), screen::TextFont::topaz8,
                     screen::plain, true, 0);
    }
    print_number(session_.score, 0xA8, 0x27, false);
    print_number(std::uint32_t(rank_[std::size_t(new_entry_)]), 0x108, 0x31, false);
    draw_initials();
}

void TitleStage::draw_initials() { // $604246
    canvas_.text(0x94, 0x4B, initials_, 0x3F, screen::TextFont::topaz8, screen::plain, true, 0);
}

// $60413A: a letter (or the space) typed, "Forget It", "Start Over".
void TitleStage::initials_click(int x, int y) {
    for (int k = 0; k < 0x1D; ++k) {
        const std::uint32_t i = std::uint32_t(k + 5) * 4;
        const int ix = t_.s32(entry_x + i), iy = t_.s32(entry_y + i);
        if (iy - 6 < y && y < iy + 2 && ix < x && x < t_.s32(entry_lengths + i) * 8 + ix) {
            if (k == 0x1B) {
                forget_ = true;
            } else if (k == 0x1C) {
                initials_ = "   ";
                typed_ = 0;
                draw_initials();
            } else {
                initials_[std::size_t(typed_++)] = char(t_.u8(letters + std::uint32_t(k)));
                draw_initials();
                wait_ = 10;
            }
            return;
        }
    }
}

// $603714: the Hall of Fame: bands of colour down the left edge carried
// across by hold-and-modify, the title, 32 places in two columns, three
// creatures.
void TitleStage::show_fame() {
    for (std::uint32_t k = 0; k < 16; ++k)
        canvas_.line(0, t_.s32(fame_rows + k * 4), 0, t_.s32(fame_rows + k * 4 + 4) - 1, t_.s32(fame_pens + k * 4));
    canvas_.fill(1, playfield_top, 0x13F, 199, 0x20);
    sort_fame();
    fame_count_ = 0;
    print_itext(fame_title, 0x70, 0xF);
    int last = 1;
    for (int k = 1; k <= 32; ++k) {
        const int a = k - 1;
        const int column = a < 16 ? 0 : 0xA0;
        int x = column + (k < 10 ? 9 : 1);
        const int y = a < 16 ? a * 10 + 0x23 : a * 10 - 0x7D;
        x += print_number(std::uint32_t(k), x, y, false) * 8;
        print_itext(rank_mark, x, y);
        x += 0x10;
        const auto& entry = ranked_[std::size_t(a)];
        if (entry.score != 0) {
            last = k;
            x += print_number(entry.score, x, y, true) * 8 + 8;
            const std::string blank = c_string(t_, t_.u32(blank_initials + 12));
            print_itext(blank_initials, x, y, &blank);
            print_itext(initials_text, x, y, &entry.initials);
        }
    }
    (void)last;
    const auto image = [&](int k, int x, int y) { // $603A4A
        const std::uint32_t at = fame_images + std::uint32_t(k * 20);
        const int on = t_.u8(at + 15);
        for (int row = 0; row < 14; ++row)
            for (int col = 0; col < 16; ++col) {
                int v = 0;
                for (int p = 0; p < 4; ++p) v |= (small_image_[std::size_t(p * 28 + row * 2 + col / 8)] >> (7 - col % 8) & 1) << p;
                canvas_.plot(x + col, y + row, v | on);
            }
        canvas_.line(x + 16, y + 4, x + 16, y + 10, t_.u8(at + 15));
    };
    image(1, 0x23, 0xF);
    image(0, 0xDC, 0xD);
    image(0, 0x12E, 0x11);
    image(2, 0x11F, 0x15);
    phase_ = Phase::fame;
}

// $603814: the title's colours cycle (hold-and-modify red, then green).
void TitleStage::animate_fame() {
    ++fame_step_;
    const int front = ((fame_step_ >> 2) & 0x10) + t_.s16(fame_cycle + std::uint32_t((fame_step_ >> 1 & 0x1F) * 2));
    const std::string title = c_string(t_, t_.u32(fame_title_cycle + 12));
    canvas_.text(0x70, 0xF + 6, title, front & 0xFF, screen::TextFont::topaz8, screen::plain, true, front & 0x30);
}

// $601AB6: the command screen: its colour bands ($601AE2), the saved
// worlds looked for, the items drawn one a frame.
void TitleStage::open_command() {
    draw_command_background();
    command_ = t_.s32(clickable - 4); // $6020D6's first value
    for (int i = 1; i < 4; ++i) {
        item_back_[std::size_t(i)] = t_.s32(command_backs + std::uint32_t(i * 4));
        item_text_[std::size_t(i)][0] = ' ';
    }
    item_back_[std::size_t(command_)] = 0;
    item_text_[std::size_t(command_)][0] = '>';
    draw_items_ = 0;
    confirming_ = false;
    phase_ = Phase::command;
}

void TitleStage::draw_command_background() {
    const auto band = [&](int y) { return t_.u16(menu_colours + std::uint32_t(t_.s32(band_steps + std::uint32_t((y >> 3 & 0x1F) * 4)) * 2)); };
    for (int y = playfield_top; y < 200; ++y) canvas_.plot(0, y, band(y));
    for (int x = 1; x < 0x140; ++x)
        canvas_.line(x, playfield_top, x, 199,
                     t_.s32(band_steps + std::uint32_t((x >> 3 & 0x1F) * 4)) + t_.s32(band_ham + std::uint32_t((x >> 8 & 1) * 4)));
    canvas_.fill(0, 0xB6, 0x13F, 199, 7);
}

// $6019A2: an item in pen $3F on its back pen; the column after it set
// back to the band's base colour (for the hold-and-modify after it).
void TitleStage::draw_item(int item) {
    const auto i = std::size_t(item);
    const int x = t_.s32(item_x + std::uint32_t(item * 4)), y = t_.s32(item_y + std::uint32_t(item * 4));
    canvas_.text(x, y, item_text_[i], 0x3F, screen::TextFont::topaz8, screen::plain, true, item_back_[i]);
    if (y < 0xBC) {
        const int end = x + int(item_text_[i].size()) * 8;
        for (int row = y - 6; row < y + 2; ++row)
            canvas_.plot(end, row, t_.u16(menu_colours + std::uint32_t(t_.s32(band_steps + std::uint32_t((row >> 3 & 0x1F) * 4)) * 2)));
    }
}

void TitleStage::select_command(int command) { // $6017E8
    command_ = command;
    for (int i = 1; i < 4; ++i) {
        item_back_[std::size_t(i)] = t_.s32(command_backs + std::uint32_t(i * 4));
        item_text_[std::size_t(i)][0] = ' ';
    }
    item_back_[std::size_t(command)] = 0;
    item_text_[std::size_t(command)][0] = '>';
    for (int i = 1; i < 4; ++i) draw_item(i);
    canvas_.fill(0, 0xB6, 0x13F, 199, 7);
    if (command == 3) {
        if (session_.where == 0) {
            draw_item(0x19); // "There's no game to save."
        } else {
            draw_item(0x12);
            draw_item(0x13);
        }
    } else {
        draw_item(4);
        draw_item(5);
    }
}

void TitleStage::message(int item) { // $602754
    canvas_.fill(0, 0xB6, 0x13F, 199, 7);
    draw_item(item);
}

// $6016B6: a click on a command, a world, or (asked) yes or no.
void TitleStage::command_click(int x, int y) {
    const int count = confirming_ ? 13 : 11;
    for (int k = 0; k < count; ++k) {
        const int item = t_.s32(clickable + std::uint32_t(k * 4));
        const int ix = t_.s32(item_x + std::uint32_t(item * 4)), iy = t_.s32(item_y + std::uint32_t(item * 4));
        if (!(iy - 6 < y && y < iy + 2 && ix < x && x < int(item_text_[std::size_t(item)].size()) * 8 + ix)) continue;
        if (!confirming_) {
            if (item >= 1 && item <= 3) select_command(item);
            else if (item > 6 && item < 15) world_clicked(item - 7);
        } else if (item == 0x11) { // $6026AE: "Yes, do it."
            confirming_ = false;
            select_command(1);
            const bool ok = session_.save_world && session_.save_world(confirm_world_);
            message(ok ? 0x14 : 0x16);
        } else {
            confirming_ = false;
            canvas_.fill(0, 0xB6, 0x13F, 199, 7);
            draw_item(4);
            draw_item(5);
            select_command(1);
        }
        return;
    }
}

// $601868: saving asks first; Continue Game and New Game, Same World need
// a saved world, then the game restarts ($602248).
void TitleStage::world_clicked(int world) {
    if (command_ == 3) {
        if (session_.where == 0) return;
        confirming_ = true;
        confirm_world_ = world;
        item_text_[0xF][0x10] = char('A' + world);
        canvas_.fill(0, 0xB6, 0x13F, 199, 7);
        for (int i = 0xF; i < 0x12; ++i) draw_item(i);
    } else if (!(session_.world_saved && session_.world_saved(world))) {
        message(0x1B);
    } else if (session_.restart && session_.restart(world, command_ == 2)) {
        message(0x18); // "Restarting game..."
        restarted_ = true;
        phase_ = Phase::loading;
    } else {
        message(0x1A);
    }
}

void TitleStage::gadget(int id) {
    if (id != pause_box) gadget_clicked(id);
    else if (paused_) resume();
    else pause();
}

screen::Canvas TitleStage::screen() const {
    if (!paused_ && shrink_frames_ == 0 && resume_frames_ == 0) return canvas_;
    // The window shrunk to its bar: the screen under it pen 0 (dumps
    // shrunk, resuming).
    screen::Canvas shown = canvas_;
    shown.fill(0, playfield_top, screen::Canvas::width - 1, screen::Canvas::height - 1, 0);
    if (paused_ && shrink_frames_ == 0) {
        // Moved down 10 (Intuition does it a moment after the call): the
        // screen's title bar above it, in BlockPen 15 with the title in
        // DetailPen 0 at (2, 7), and the depth gadgets (dump paused_later).
        const screen::Canvas bar = shown;
        for (int y = 0; y < playfield_top; ++y)
            for (int x = 0; x < screen::Canvas::width; ++x) shown.plot(x, y + 10, bar.pen(x, y));
        shown.fill(0, 0, screen::Canvas::width - 1, 9, 0xF);
        shown.text(2, 7, c_string(t_, screen_title), 0, screen::TextFont::topaz8, screen::plain, false, 0);
        for (const auto& g : amiga::screen_depth_gadgets()) {
            const int x0 = screen::Canvas::width - 1 + g.left;
            for (int y = 0; y < g.height; ++y)
                for (int x = 0; x < g.width; ++x)
                    shown.plot(x0 + x, y, int(g.planes[0][std::size_t(y)] >> (31 - x) & 1) | int(g.planes[1][std::size_t(y)] >> (31 - x) & 1) << 1);
        }
    }
    return shown;
}

platform::Image TitleStage::picture() const {
    // S's screen colours: 0 black, 1 green (S $700040's SetRGB4 calls).
    if (phase_ == Phase::loading) return loading_.picture({0x000, 0x0F0});
    return screen().ham_picture(colours_);
}

} // namespace mindwalker::title
