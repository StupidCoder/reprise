#include "tower_of_babel_amiga/front/front_end.hpp"
#include <algorithm>
#include <random>
#include <sstream>

namespace babel::front {
namespace {

// Palettes (48 colours) in the program.
constexpr std::uint32_t menu_palette = var(0x23F0);
constexpr std::uint32_t title_palette = var(0x2498);
constexpr std::uint32_t credits_palette = var(0x24D8);
constexpr std::uint32_t black_palette = var(0x25B6);
constexpr std::uint32_t panel_band_menu = var(0x23F2); // 15 colours for the panel band under a demonstration

// Texts.
constexpr std::uint32_t credits_text = var(0x490D), name_text = var(0x4A7C), main_text = var(0x4AB7);
constexpr std::uint32_t restart_title = var(0x4B45), header_text = var(0x4B58), game_type_text = var(0x4B74);
constexpr std::uint32_t chooser_text = var(0x4BC8), information_text = var(0x4BED);
constexpr std::uint32_t zapper_name = var(0x4C6B), pusher_name = var(0x4C72), grabber_name = var(0x4C79);
constexpr std::uint32_t comma = var(0x4C81), and_text = var(0x4C84), no_spiders = var(0x4C8A);
constexpr std::uint32_t last_group = var(0x4C9E), next_group = var(0x4CB8), group_text = var(0x4CD5), percent_text = var(0x4CE2);
constexpr std::uint32_t score_text = var(0x4CF4), final_text = var(0x4D51);
constexpr std::uint32_t collect = var(0x4E41), klondike = var(0x4E4A), and_word = var(0x4E54), destroy = var(0x4E59);
constexpr std::uint32_t object_word = var(0x4E62), within = var(0x4E6A), time_limit_text = var(0x4E72), second = var(0x4E82);
constexpr std::uint32_t no_task1 = var(0x4E8A), no_task2 = var(0x4EAB), no_task3 = var(0x4ECB);
constexpr std::uint32_t custom_text = var(0x4EED), pause_bar = var(0x4FD6), setup_text = var(0x4FFC), view_text = var(0x5105);
constexpr std::uint32_t info_text = var(0x51EA), info_not = var(0x5211), info_allowed = var(0x5216), info_dont = var(0x5229);
constexpr std::uint32_t info_floor = var(0x5230), task_text = var(0x5253), exit_text = var(0x5269);
constexpr std::uint32_t abandon_title = var(0x5297), restart_tower_title = var(0x52AA);
constexpr std::uint32_t not_named = var(0x52BD), anonymous = var(0x52C7), default_names = var(0x52D1);
constexpr std::uint32_t ok_text = var(0x532B), sure_text = var(0x6409);
constexpr std::uint32_t save_load_text = var(0x4F4E), high_score_text = var(0x4F99);
constexpr std::uint32_t load_title = var(0x5618), save_title = var(0x564A), update_title = var(0x567C), group_title = var(0x54DA);
constexpr std::uint32_t requester_text = var(0x56C0), disk_error_text = var(0x5751), replace_text = var(0x5721);
constexpr std::uint32_t score_table = var(0x122E);
constexpr std::uint32_t save_load_areas = var(0x3842), high_score_areas = var(0x3862), requester_areas = var(0x3DA8);
constexpr std::uint32_t error_areas = var(0x453C), replace_areas = var(0x3E4A);

// Menus' click areas.
constexpr std::uint32_t main_menu_areas = var(0x3878), game_type_areas = var(0x38C2), chooser_areas = var(0x38EC);
constexpr std::uint32_t information_areas = var(0x3970), pause_areas = var(0x3986), final_areas = var(0x38B6);
constexpr std::uint32_t custom_areas = var(0x3818), view_areas = var(0x3A88), setup_areas = var(0x39C6);
constexpr std::uint32_t info_areas = var(0x3B20), task_areas = var(0x3B36), exit_areas = var(0x3B42), yes_areas = var(0x4526);

// Radio button lists.
constexpr std::uint32_t radio_backwards = var(0x4566), radio_link = var(0x4570), radio_info = var(0x457A);
constexpr std::uint32_t radio_timer = var(0x4588), radio_screen_x = var(0x4596), radio_screen_y = var(0x45A4);
constexpr std::uint32_t radio_view_side = var(0x45B2), radio_view_height = var(0x45C0), radio_view_distance = var(0x45CE);
constexpr std::uint32_t radio_sights = var(0x45DC), radio_panel = var(0x45E6);

// Pictures in the main data and the title data.
constexpr std::uint32_t tower_buttons = 0x283C0; // 9 towers x 4 states, 2 words x 32 rows each
constexpr std::uint32_t group_arrows = 0x2CDC0;  // 3 states
constexpr std::uint32_t logo_at = 0x18200, globe_at = 0x197E0;

constexpr std::uint32_t neighbours = 0x8E52;     // per tower the towers next to it (bit mask)
constexpr std::uint32_t lose_sound = var(0x3120);
constexpr double ticks_per_vblank = 709379.0 / 7160.0 / 50.0; // CIA-A timer B ($1BF8) per PAL frame
constexpr int message_end = 0x66;                // + the end reason: why the tower ended

} // namespace

FrontEnd::FrontEnd(const data::Assets& assets, std::filesystem::path saves)
    : assets_(assets), disk_(std::move(saves)), screen_(assets) {
    random_ = std::uint16_t(std::random_device{}()); // the original seeds $890 from the CIA clock
    // $122E: the table the program starts with (30-byte names, long scores).
    for (int i = 0; i < 10; ++i) {
        const std::uint32_t at = score_table + std::uint32_t(i) * 0x22;
        scores_.push_back({assets.program.text(at, 0x1E), long(assets.program.u32(at + 0x1E))});
    }
    custom_towers_ = assets.groups.at(15).towers;
    runner_.start(run());
}

view::Canvas FrontEnd::wide_picture(int margin, double alpha) const {
    // The screen in the middle, the title line stretched to the full width,
    // the view drawn again across it with its frame.
    const int w = Screen::width + 2 * margin;
    const auto& base = screen_.canvas();
    view::Canvas canvas(w, Screen::height);
    for (int y = 0; y < Screen::height; ++y)
        for (int x = 0; x < Screen::width; ++x) canvas.set(x + margin, y, base.at(x, y));
    if (margin > 0) {
        const auto edge = base.at(0, 0), fill = base.at(1, 1);
        for (int y = 0; y < 12; ++y) {
            for (int x = 0; x < margin; ++x) {
                const bool frame = y == 0 || y == 11 || x == 0;
                canvas.set(x, y, frame ? edge : fill);
                canvas.set(w - 1 - x, y, frame ? edge : fill);
            }
            if (y > 0 && y < 11) {
                if (base.at(0, y) == edge) canvas.set(margin, y, fill);
                if (base.at(Screen::width - 1, y) == edge) canvas.set(margin + Screen::width - 1, y, fill);
            }
        }
    }
    if (active_play_->shows_view()) {
        active_play_->redraw(canvas, margin, alpha);
        if (margin > 0) {
            const auto colour = base.at(15, 15);
            for (int x = 15; x <= w - 16; ++x) canvas.set(x, 15, colour), canvas.set(x, 128, colour);
            for (int y = 15; y <= 128; ++y) canvas.set(15, y, colour), canvas.set(w - 16, y, colour);
        }
    }
    return canvas;
}

platform::Image FrontEnd::picture(int margin, bool smooth, double fraction) const {
    const bool live = active_play_ && screen_.copper == CopperMode::game;
    if (margin == 0 && (!smooth || !live || play_paused_)) return screen_.image();
    const int w = Screen::width + 2 * margin;
    const auto& base = screen_.canvas();
    if (!live) { // other screens in the middle, black beside them
        view::Canvas canvas(w, Screen::height);
        for (int y = 0; y < Screen::height; ++y)
            for (int x = 0; x < Screen::width; ++x) canvas.set(x + margin, y, base.at(x, y));
        return screen_.image_of(canvas, margin);
    }
    if (play_paused_) {
        // The wide picture as the pause began (drawn once more at the frame
        // itself), with what the menus have drawn since over it.
        if (!paused_wide_ || wide_.width != w) {
            const auto now = screen_.canvas();
            wide_ = wide_picture(margin, 1.0);
            for (int y = 0; y < Screen::height; ++y)
                for (int x = 0; x < Screen::width; ++x)
                    if (now.at(x, y) != paused_screen_.at(x, y)) wide_.set(x + margin, y, paused_screen_.at(x, y));
            paused_wide_ = true;
        }
        auto canvas = wide_;
        for (int y = 0; y < Screen::height; ++y)
            for (int x = 0; x < Screen::width; ++x)
                if (base.at(x, y) != paused_screen_.at(x, y)) canvas.set(x + margin, y, base.at(x, y));
        return screen_.image_of(canvas, margin);
    }
    const double alpha = smooth ? std::clamp((ticks_ + fraction * ticks_per_vblank) / 14.0, 0.0, 1.0) : 1.0;
    return screen_.image_of(wide_picture(margin, alpha), margin);
}

void FrontEnd::vblank(const Input& raw) {
    previous_ = in_;
    Input input = raw;
    input.x -= margin_; // the game's own picture is in the middle
    in_ = input;
    if (in_.left && !previous_.left) {
        clicked_ = true;
        clicked_x_ = in_.x;
        clicked_y_ = in_.y;
    }
    ticks_ += ticks_per_vblank;
    if (piece_frames_ > 0) --piece_frames_;
    runner_.resume();
}

// --- waiting ---------------------------------------------------------------

void FrontEnd::sound_frame(const Play& play) {
    sounds_.push_back({SoundEvent::Kind::frame, play.sound_frame(), 0, play.volume_offset()});
}

Flow FrontEnd::wait_frames(int n) {
    for (int i = 0; i < n; ++i) co_await next_frame();
}

Flow FrontEnd::wait_release() {
    while (in_.left || in_.right) co_await next_frame();
}

Flow FrontEnd::select(std::uint32_t areas, int& choice) {
    // $112EA: the vertical blank keeps the area under the pointer lit; a
    // press of the left button over one chooses it.
    // The choice starts as none ($E00 = -1) until a vertical blank has looked.
    Menu menu(assets_.program, areas);
    for (;;) {
        co_await next_frame();
        const int x = in_.pointer ? in_.x : 0x1F40, y = in_.pointer ? in_.y : 0x8000;
        menu.hover(screen_, x, y);
        choice = menu.at(x, y);
        if (in_.left && choice >= 0) break;
    }
    menu.unhover(screen_);
}

Flow FrontEnd::game_frame_wait() {
    // $87EE: the frame ends once the timer has counted 14 since the last.
    while (ticks_ < 14) co_await next_frame();
    ticks_ -= 14;
}

Flow FrontEnd::fade(const std::array<data::Colour, 48>& target, int first, int count) {
    if (fast_fades_) { // $12628: at once, then two frames
        for (int i = first; i < first + count; ++i) current_[std::size_t(i)] = target[std::size_t(i)];
        screen_.palette = current_;
        co_await next_frame();
        co_await next_frame();
        co_return;
    }
    // $125B2 / $1255C: each component in 16 steps of (target - start) / 16,
    // from start * 16 + 7, one step a frame.
    std::array<std::array<int, 3>, 48> value{}, step{};
    for (std::size_t i = 0; i < 48; ++i)
        for (int k = 0; k < 3; ++k) {
            const int shift = 8 - 4 * k;
            const int from = current_[i] >> shift & 15, to = target[i] >> shift & 15;
            value[i][std::size_t(k)] = from * 16 + 7;
            step[i][std::size_t(k)] = to - from;
        }
    for (int s = 0; s < 16; ++s) {
        for (int i = first; i < first + count; ++i) {
            auto& v = value[std::size_t(i)];
            data::Colour c = 0;
            for (int k = 0; k < 3; ++k) {
                v[std::size_t(k)] += step[std::size_t(i)][std::size_t(k)];
                c = data::Colour(c << 4 | (v[std::size_t(k)] >> 4 & 15));
            }
            current_[std::size_t(i)] = c;
        }
        screen_.palette = current_;
        co_await next_frame();
    }
}

Flow FrontEnd::fade_out() {
    std::array<data::Colour, 48> black{};
    for (std::size_t i = 0; i < 48; ++i) black[i] = assets_.program.u16(black_palette + std::uint32_t(i) * 2);
    co_await fade(black);
}

Flow FrontEnd::fade_in_program(std::uint32_t palette) {
    std::array<data::Colour, 48> target{};
    for (std::size_t i = 0; i < 48; ++i) target[i] = assets_.program.u16(palette + std::uint32_t(i) * 2);
    co_await fade(target);
}

// --- the game ---------------------------------------------------------------

Flow FrontEnd::run() {
    co_await title();
    co_await credits();
    for (;;) {
        co_await name_entry();
        co_await main_menu(); // returns for Restart Game
    }
}

Flow FrontEnd::title() {
    // $7EF6: the logo and the globe in the title's three colour bands, the
    // title music; the program then loads its main data (left out here).
    screen_.copper = CopperMode::title;
    screen_.clear();
    piece_ = 0;
    piece_frames_ = int(assets_.pieces.at(0).samples.size() * 50 / (3546895 / std::max(assets_.pieces.at(0).period, 1))) + 1;
    screen_.picture(assets_.title_memory, logo_at, 0, 10, 20, 35);
    screen_.picture(assets_.title_memory, globe_at, 80, 61, 10, 133);
    co_await fade_in_program(title_palette);
    while (piece_frames_ > 0) co_await next_frame(); // until the music ends
}

Flow FrontEnd::credits() {
    // $15A2E: the lower bands alternate between the credits and the globe
    // (about ten seconds each) until Space is pressed.
    std::array<data::Colour, 48> black{}, credits_colours{}, title_colours{};
    for (std::size_t i = 0; i < 48; ++i) {
        black[i] = assets_.program.u16(black_palette + std::uint32_t(i) * 2);
        credits_colours[i] = assets_.program.u16(credits_palette + std::uint32_t(i) * 2);
        title_colours[i] = assets_.program.u16(title_palette + std::uint32_t(i) * 2);
    }
    co_await fade(black, 16, 32);
    screen_.copper = CopperMode::title;
    const auto wait = [this]() -> Flow {
        // 100 waits of 11 timer ticks, watching for Space (or a click).
        for (int i = 0; i < 100; ++i) {
            const double start = ticks_;
            while (ticks_ - start <= 10) {
                if (in_.key == ' ' || (in_.left && !previous_.left)) co_return;
                co_await next_frame();
            }
        }
    };
    for (bool done = false; !done;) {
        screen_.fill(0, 0x3A, 0x13F, 0xC7, 0);
        screen_.text_at(credits_text);
        co_await fade(credits_colours, 16, 32);
        co_await wait();
        if (in_.key == ' ' || in_.left) break;
        co_await fade(black, 16, 32);
        screen_.fill(0, 0x3A, 0x13F, 0xC7, 0);
        screen_.picture(assets_.title_memory, globe_at, 80, 61, 10, 133);
        co_await fade(title_colours, 16, 32);
        co_await wait();
        if (in_.key == ' ' || in_.left) break;
        co_await fade(black, 16, 32);
    }
    co_await fade_out();
    screen_.copper = CopperMode::plain;
}

Flow FrontEnd::name_entry() {
    co_await fade_out();
    screen_.copper = CopperMode::plain;
    screen_.clear();
    screen_.title_bar();
    screen_.frame(0x36, 0x4C, 0x109, 0x80, 13);
    screen_.text_at(name_text);
    co_await fade_in_program(menu_palette);
    co_await edit_name();
    if (name_.empty()) {
        // $8C90: one of eight names for a player who gives none.
        random_ = std::uint16_t(random_ * 0x5E5 + 0x29);
        std::uint32_t at = default_names;
        for (int skip = random_ & 7; skip > 0; --skip) at += std::uint32_t(assets_.program.text(at).size()) + 1;
        name_ = assets_.program.text(at);
    }
    standard_progress_.fill(0);
    custom_progress_.fill(0);
    tutorial_progress_.fill(0);
    group_ = 0;
    all_done_ = false;
    score_ = 0;
}

void FrontEnd::draw_editor(const LineEditor& e, bool with_cursor) {
    // $12304: the line cleared, the text with the cursor ("|" in pen 3)
    // before the character it stands at, then a blank.
    screen_.fill(e.x, e.y, e.right, e.y + 7, 0);
    screen_.cursor.x = e.x;
    screen_.cursor.y = e.y;
    screen_.cursor.paper = 0;
    screen_.cursor.ink = 15;
    for (int i = 0;; ++i) {
        if (with_cursor && i == e.cursor) {
            screen_.cursor.ink = 3;
            screen_.glyph(0x7C);
            screen_.cursor.ink = 15;
        }
        if (i >= int(e.text.size())) break;
        screen_.glyph(std::uint8_t(e.text[std::size_t(i)]));
    }
    screen_.glyph(0x80);
}

bool FrontEnd::edit_key(LineEditor& e, int key, int end_x) {
    if (key == 13) return false;
    if (key == 8) {
        if (e.cursor > 0) e.text.erase(std::size_t(--e.cursor), 1);
    } else if (key == 0x7F) {
        if (e.cursor < int(e.text.size())) e.text.erase(std::size_t(e.cursor), 1);
    } else if (key == 27) {
        e.text.clear();
        e.cursor = 0;
    } else if (key >= 0x20 && key < 0x7F && end_x < e.right && int(e.text.size()) < e.most) {
        e.text.insert(std::size_t(e.cursor++), 1, char(key));
    }
    return true;
}

void FrontEnd::edit_click(LineEditor& e, int x) {
    // $120F2: the cursor goes to the first character whose start is past the pointer.
    int left = e.x, i = 0;
    while (i < int(e.text.size()) && left < x) left += screen_.text_width(e.text.substr(std::size_t(i++), 1));
    e.cursor = i;
}

Flow FrontEnd::edit_name() {
    // The name record ($E46): 28 characters at (82, 110) short of x 260; a
    // click in the box (54, 76)-(265, 128) moves the cursor, outside it ends.
    LineEditor e{0x52, 0x6E, 0x1C, 0x104, 0x36, 0x4C, 0x109, 0x80, {}, 0};
    for (;;) {
        draw_editor(e);
        const int end_x = screen_.cursor.x;
        co_await next_frame();
        if (!edit_key(e, in_.key, end_x)) break;
        if (in_.left && !previous_.left) {
            if (in_.x >= e.box_x0 && in_.x < e.box_x1 && in_.y >= e.box_y0 && in_.y < e.box_y1) edit_click(e, in_.x);
            else break;
        }
    }
    draw_editor(e, false);
    name_ = e.text;
    co_await wait_release();
}

Flow FrontEnd::disk_error(int code) {
    screen_.open_window(0x1C, 0x88, 0x124, 0xB8, 0);
    screen_.text_at(disk_error_text);
    screen_.number(code, 2);
    screen_.glyph(')');
    screen_.cursor.ink = 9;
    const auto message = screen_.message_text(0x80 + (code & 15));
    screen_.centre(message);
    screen_.cursor.y = 0xA2;
    screen_.text(message);
    screen_.cursor.ink = 15;
    co_await wait_frames(50); // $12654 twice: 50 timer ticks each
    int choice = 0;
    co_await select(error_areas, choice);
    screen_.close_window();
}

Flow FrontEnd::replace_file(bool& yes) {
    co_await wait_release();
    screen_.open_window(0x4C, 0x48, 0xF3, 0x7D, 0);
    screen_.text_at(replace_text);
    screen_.frame(0x8F, 0x6B, 0xB1, 0x78, 13);
    int choice = 0;
    co_await select(replace_areas, choice);
    screen_.close_window();
    co_await wait_release();
    yes = choice == 1;
}

Flow FrontEnd::requester(FileKind kind, std::uint32_t title, std::function<int(const std::string&, bool)> action) {
    screen_.open_window(0x40, 0x18, 0xFF, 0xC2, 0);
    screen_.frame(0x45, 0x47, 0x93, 0xC0, 13);
    screen_.frame(0x9D, 0x47, 0xC9, 0x54, 13);
    screen_.frame(0x9D, 0xB3, 0xC9, 0xC0, 13);
    screen_.frame(0x9D, 0x83, 0xF1, 0x90, 13);
    screen_.frame(0x9D, 0x9B, 0xF1, 0xA8, 13);
    screen_.text_at(title);
    screen_.text_at(requester_text);
    int page = 0, error = 0;
    std::vector<std::string> names;
    const auto draw_page = [&] {
        // $1297A: the page number, then up to ten names.
        screen_.cursor.x = 0x97;
        screen_.cursor.y = 0x38;
        screen_.cursor.ink = 9;
        screen_.number(page + 1, 2);
        screen_.cursor.ink = 15;
        screen_.fill(0x48, 0x4A, 0x90, 0xBE, 0);
        for (int i = 0; i < 10 && page * 10 + i < int(names.size()); ++i) {
            screen_.cursor.x = 0x48;
            screen_.cursor.y = 0x4A + 12 * i;
            screen_.text(names[std::size_t(page * 10 + i)].substr(0, 8));
        }
    };
    const auto read_directory = [&] {
        // $12A10 / $12AFA: the files of this kind, the room left, the pages.
        names = disk_.names(kind);
        page = 0;
        screen_.cursor.ink = 9;
        screen_.cursor.x = 0x55;
        screen_.cursor.y = 0x2C;
        screen_.number(long(names.size()), 3);
        screen_.cursor.x = 0xDB;
        screen_.number(DataDisk::entries - disk_.files(), 3);
        screen_.cursor.x = 0xBC;
        screen_.cursor.y = 0x38;
        screen_.number((std::max<int>(int(names.size()), 1) - 1) / 10 + 1, 2);
        screen_.cursor.ink = 15;
        draw_page();
    };
    read_directory();
    auto& e = file_name_;
    e.cursor = int(e.text.size());
    Menu menu(assets_.program, requester_areas);
    co_await wait_release();
    bool ok = false;
    for (;;) {
        draw_editor(e);
        const int end_x = screen_.cursor.x;
        co_await next_frame();
        const int x = in_.pointer ? in_.x : 0x1F40, y = in_.pointer ? in_.y : 0x8000;
        menu.hover(screen_, x, y);
        const int key = in_.key;
        if (key != 0 && key != '.' && key != ' ') {
            if (!edit_key(e, key, end_x)) {
                ok = true;
                break;
            }
            continue;
        }
        const int choice = menu.at(x, y);
        if (!in_.left || choice < 0) continue;
        if (choice == 5) {
            edit_click(e, in_.x);
            continue;
        }
        if (choice < 2) {
            ok = choice == 1;
            break;
        }
        if (choice == 2 && page > 0) {
            --page;
            draw_page();
        } else if (choice == 3 && (page + 1) * 10 < int(names.size())) {
            ++page;
            draw_page();
        } else if (choice == 4) {
            read_directory();
        } else if (choice >= 6 && page * 10 + choice - 6 < int(names.size())) {
            e.text = names[std::size_t(page * 10 + choice - 6)];
            e.cursor = int(e.text.size());
        }
        co_await wait_release();
    }
    menu.unhover(screen_);
    if (ok && !e.text.empty()) error = action(e.text, false);
    if (error == -1) { // the file exists: replace it?
        bool yes = false;
        co_await replace_file(yes);
        error = yes ? action(e.text, true) : 0;
    }
    if (error > 0) co_await disk_error(error);
    screen_.close_window();
    co_await wait_release();
}

// --- saved positions and high scores ----------------------------------------

std::vector<std::uint8_t> FrontEnd::position() const {
    // The player's settings, name, score, group and every tower's progress,
    // as text lines.
    std::string t = "tower of babel position 1\n";
    const auto line = [&](const char* key, long v) { t += std::string(key) + " " + std::to_string(v) + "\n"; };
    line("backwards", settings_.backwards_allowed);
    line("link", settings_.link_cameras);
    line("info", settings_.info);
    line("timer", settings_.timer);
    line("view_side", settings_.view_side);
    line("view_height", settings_.view_height);
    line("view_distance", settings_.view_distance);
    line("sights", settings_.sights);
    line("panel", settings_.panel_style);
    line("screen_x", screen_x_);
    line("screen_y", screen_y_);
    t += "name " + name_ + "\n";
    line("score", score_);
    line("group", group_);
    line("all_done", all_done_);
    const auto bytes = [&](const char* key, const auto& a) {
        t += key;
        for (const auto b : a) t += " " + std::to_string(int(b));
        t += "\n";
    };
    bytes("standard", standard_progress_);
    bytes("custom", custom_progress_);
    bytes("tutorial", tutorial_progress_);
    return {t.begin(), t.end()};
}

bool FrontEnd::load_position(const std::vector<std::uint8_t>& file) {
    std::istringstream in(std::string(file.begin(), file.end()));
    std::string line;
    if (!std::getline(in, line) || line != "tower of babel position 1") return false;
    Settings s = settings_;
    while (std::getline(in, line)) {
        std::istringstream w(line);
        std::string key;
        w >> key;
        long v = 0;
        if (key == "name") {
            name_ = line.size() > 5 ? line.substr(5) : "";
            continue;
        }
        if (key == "standard" || key == "custom" || key == "tutorial") {
            auto* a = key == "standard" ? standard_progress_.data() : key == "custom" ? custom_progress_.data() : tutorial_progress_.data();
            const std::size_t n = key == "standard" ? standard_progress_.size() : 9;
            for (std::size_t i = 0; i < n && (w >> v); ++i) a[i] = std::uint8_t(v);
            continue;
        }
        w >> v;
        if (key == "backwards") s.backwards_allowed = v != 0;
        else if (key == "link") s.link_cameras = v != 0;
        else if (key == "info") s.info = int(v);
        else if (key == "timer") s.timer = int(v);
        else if (key == "view_side") s.view_side = int(v);
        else if (key == "view_height") s.view_height = int(v);
        else if (key == "view_distance") s.view_distance = int(v);
        else if (key == "sights") s.sights = v != 0;
        else if (key == "panel") s.panel_style = int(v);
        else if (key == "screen_x") screen_x_ = int(v);
        else if (key == "screen_y") screen_y_ = int(v);
        else if (key == "score") score_ = v;
        else if (key == "group") group_ = int(v);
        else if (key == "all_done") all_done_ = v != 0;
    }
    settings_ = s;
    return true;
}

void FrontEnd::sort_scores() {
    // $AA24: neighbours swapped while one is higher, until none is.
    for (bool swapped = true; swapped;) {
        swapped = false;
        for (std::size_t i = 0; i + 1 < scores_.size(); ++i)
            if (scores_[i].score < scores_[i + 1].score) {
                std::swap(scores_[i], scores_[i + 1]);
                swapped = true;
            }
    }
}

void FrontEnd::update_scores() {
    // $AA8C: the player's entry takes the score; a new player replaces the
    // last entry when the score is above it.
    auto it = std::find_if(scores_.begin(), scores_.end(), [&](const Score& e) { return e.name == name_; });
    if (it != scores_.end()) it->score = score_;
    else if (score_ > scores_.back().score) scores_.back() = {name_, score_};
    sort_scores();
}

Flow FrontEnd::save_load() {
    co_await fade_out();
    screen_.clear();
    screen_.title_bar();
    menu_boxes(0x1C);
    screen_.text_at(save_load_text);
    co_await fade_in_program(menu_palette);
    for (;;) {
        co_await wait_release();
        int choice = 0;
        co_await select(save_load_areas, choice);
        if (choice == 2) co_return;
        bool loaded = false;
        if (choice == 0) {
            co_await requester(FileKind::position, load_title, [&](const std::string& n, bool) {
                const auto file = disk_.read(FileKind::position, n);
                if (!file) return 6;               // File Not Found
                if (!load_position(*file)) return 0xB; // Invalid File
                loaded = true;
                return 0;
            });
        } else {
            co_await requester(FileKind::position, save_title, [&](const std::string& n, bool replace) {
                if (!replace && disk_.exists(FileKind::position, n)) return -1;
                return disk_.write(FileKind::position, n, position()) ? 0 : 8; // Unable To Write to Disk
            });
        }
        if (loaded) co_return; // back to the main menu with the loaded name and score
    }
}

Flow FrontEnd::high_scores() {
    co_await fade_out();
    screen_.clear();
    screen_.title_bar();
    menu_boxes(0x60);
    screen_.text_at(high_score_text);
    for (;;) {
        screen_.bevel_box(0, 0x16, 0x13F, 0x91, 13);
        screen_.cursor.paper = 13;
        screen_.cursor.ink = 15;
        for (std::size_t i = 0; i < 10 && i < scores_.size(); ++i) {
            screen_.cursor.y = 0x1A + 12 * int(i);
            screen_.cursor.x = 0x10C;
            screen_.number(scores_[i].score, 6);
            screen_.cursor.x = 4;
            screen_.text(scores_[i].name);
        }
        screen_.cursor.paper = 0;
        screen_.cursor.ink = 15;
        co_await fade_in_program(menu_palette);
        int choice = 0;
        co_await select(high_score_areas, choice);
        if (choice == 0) co_return;
        // $A8D4: merged with the table in the file (the higher score for a
        // name, new names added), the best ten kept and written back.
        co_await requester(FileKind::scores, update_title, [&](const std::string& n, bool) {
            if (const auto file = disk_.read(FileKind::scores, n)) {
                std::istringstream in(std::string(file->begin(), file->end()));
                long v = 0;
                std::string name;
                while (in >> v && std::getline(in, name)) {
                    if (!name.empty() && name[0] == ' ') name.erase(0, 1);
                    auto it = std::find_if(scores_.begin(), scores_.end(), [&](const Score& e) { return e.name == name; });
                    if (it == scores_.end()) scores_.push_back({name, v});
                    else if (v >= it->score) it->score = v;
                }
                sort_scores();
                scores_.resize(10);
            }
            std::string t;
            for (const auto& e : scores_) t += std::to_string(e.score) + " " + e.name + "\n";
            return disk_.write(FileKind::scores, n, {t.begin(), t.end()}) ? 0 : 8;
        });
    }
}

void FrontEnd::header() {
    screen_.bevel_box(4, 0x16, 0x13B, 0x25, 13);
    screen_.text_at(header_text);
    screen_.cursor.x = 0x2E;
    screen_.cursor.y = 0x1A;
    screen_.cursor.paper = 13;
    screen_.cursor.ink = 15;
    screen_.text(name_);
    screen_.cursor.x = 0x108;
    screen_.cursor.y = 0x1A;
    screen_.number(score_, 6);
}

void FrontEnd::menu_boxes(int rows) {
    for (int y = 0x23; y < 0xB7; y += 0x18, rows >>= 1)
        if (rows & 1) screen_.frame(0x47, y, 0xF8, y + 13, 13);
}

Flow FrontEnd::confirm(std::uint32_t title_text, bool& yes) {
    co_await wait_release();
    screen_.open_window(0x4C, 0x48, 0xF3, 0x7D, 0);
    screen_.text_at(title_text);
    screen_.text_at(sure_text);
    screen_.frame(0x8F, 0x6B, 0xB1, 0x78, 13);
    int choice = 0;
    co_await select(yes_areas, choice);
    screen_.close_window();
    co_await wait_release();
    yes = choice == 0;
}

Flow FrontEnd::main_menu() {
    update_scores(); // $8D28
    for (;;) {
        co_await fade_out();
        screen_.clear();
        screen_.title_bar();
        menu_boxes(0x7E);
        screen_.text_at(main_text);
        header();
        co_await fade_in_program(menu_palette);
        co_await wait_release();
        for (bool redraw = false; !redraw;) {
            int choice = 0;
            co_await select(main_menu_areas, choice);
            switch (choice) {
            case 0:
                co_await play_game();
                redraw = true;
                break;
            case 1:
                co_await save_load();
                redraw = true;
                break;
            case 2:
                co_await high_scores();
                redraw = true;
                break;
            case 4:
                co_await demo_sequence();
                redraw = true;
                break;
            case 5: {
                bool yes = false;
                co_await confirm(restart_title, yes);
                if (yes) co_return;
                break;
            }
            case 3:
                co_await designer();
                redraw = true;
                break;
            default:
                break;
            }
        }
    }
}

Flow FrontEnd::play_game() {
    co_await fade_out();
    screen_.clear();
    screen_.title_bar();
    screen_.text_at(game_type_text);
    menu_boxes(0x1E);
    header();
    co_await fade_in_program(menu_palette);
    for (;;) {
        int choice = 0;
        co_await select(game_type_areas, choice);
        if (choice == 0 && all_done_) continue; // nothing left to play in the standard game
        if (choice == 0) co_await standard_game();
        else if (choice == 1) co_await custom_game();
        else if (choice == 2) co_await tutorial_game();
        co_return;
    }
}

const data::Tower& FrontEnd::tower_in(int game_type, int index) const {
    // Groups in the assets: 0 tutorial, 1-13 standard, 14 demonstrations, 15 custom.
    if (game_type == 0) return assets_.groups.at(std::size_t(1 + group_)).towers.at(std::size_t(index));
    if (game_type == 1) return custom_towers_.at(std::size_t(index));
    return assets_.groups.at(0).towers.at(std::size_t(index));
}

std::uint8_t& FrontEnd::progress(int game_type, int index) {
    if (game_type == 0) return standard_progress_.at(std::size_t(group_ * 9 + index));
    if (game_type == 1) return custom_progress_.at(std::size_t(index));
    return tutorial_progress_.at(std::size_t(index));
}

int FrontEnd::completed_mask(int game_type, int& count) const {
    // $8E6C: towers whose byte is $FF are solved.
    count = 0;
    int mask = 0;
    for (int i = 0; i < 9; ++i) {
        const auto b = game_type == 0 ? standard_progress_[std::size_t(group_ * 9 + i)]
                       : game_type == 1 ? custom_progress_[std::size_t(i)]
                                        : tutorial_progress_[std::size_t(i)];
        if (b & 0x80) {
            mask |= 1 << i;
            ++count;
        }
    }
    return mask;
}

void FrontEnd::draw_towers(int game_type, int completed, int available) {
    for (int i = 0; i < 9; ++i) {
        int state = 0;
        if (!(completed >> i & 1)) {
            state = 1;
            if (available >> i & 1) state = selected_ == i ? 3 : 2;
        }
        screen_.picture(assets_.main, tower_buttons + std::uint32_t(i) * 0x800 + std::uint32_t(state) * 0x200, 0x70 + 32 * (i % 3),
                        0x32 + 32 * (i / 3), 2, 32);
    }
    if (game_type == 0) {
        int left = 0, right = 0;
        if (group_ != 0) left = selected_ == 9 ? 2 : 1;
        if (completed_count_ >= 7 && group_ < 12) right = selected_ == 10 ? 2 : 1;
        screen_.picture(assets_.main, group_arrows + std::uint32_t(left) * 0x200, 0x30, 0x52, 2, 32);
        screen_.picture(assets_.main, group_arrows + std::uint32_t(right) * 0x200, 0xF0, 0x52, 2, 32);
    }
    // The bar under the towers: the chosen tower's name or where an arrow leads.
    screen_.fill(10, 0x9E, 0x135, 0xA5, 13);
    screen_.cursor.paper = 13;
    screen_.cursor.ink = 15;
    screen_.cursor.y = 0x9E;
    if (selected_ >= 0 && selected_ < 9) {
        const auto& name = tower_in(game_type, selected_).name;
        screen_.centre(name);
        screen_.text(name);
    } else if (selected_ == 9 || selected_ == 10) {
        const auto s = screen_.string_at(selected_ == 9 ? last_group : next_group);
        screen_.centre(s); // $12058 measures the pen codes' bytes as glyphs too
        screen_.text(s);
    }
}

Flow FrontEnd::choose_tower(int game_type, int& choice) {
    int completed = completed_mask(game_type, completed_count_);
    int available = 0x1FF;
    if (game_type == 0) {
        available = 0;
        for (int i = 0; i < 9; ++i)
            if (completed >> i & 1) available |= assets_.program.u16(neighbours + std::uint32_t(i) * 2);
        available &= ~completed;
        if (completed == 0) available = 7;
    } else if (game_type == 1) {
        available = ~completed & 0x1FF;
    }
    if (selected_ >= 9) selected_ = -1;
    co_await fade_out();
    screen_.clear();
    screen_.title_bar();
    screen_.text_at(chooser_text);
    screen_.bevel_box(4, 0x9A, 0x13B, 0xA9, 13);
    screen_.frame(0x6F, 0xB3, 0x9E, 0xC0, 13);
    screen_.frame(0xA3, 0xB3, 0xD0, 0xC0, 13);
    header();
    if (game_type == 0) {
        screen_.text_at(group_text);
        screen_.number(group_, 2);
        screen_.text_at(percent_text);
        const int solved = int(std::count(standard_progress_.begin(), standard_progress_.end(), 0xFF));
        int percent = solved * 100 / 117;
        if (percent != 99 && solved * 100 % 117 != 0) ++percent;
        screen_.number(percent, 3);
    }
    draw_towers(game_type, completed, available);
    co_await fade_in_program(menu_palette);
    co_await wait_release();
    for (;;) {
        int c = 0;
        co_await select(chooser_areas, c);
        if (c == 12) {
            choice = -1;
            co_return;
        }
        if (c == 11) {
            if (selected_ < 0) continue;
            choice = selected_;
            co_return;
        }
        if (c == 9 || c == 10) {
            if (game_type != 0) continue;
            if (c == 9 && group_ == 0) continue;
            if (c == 10 && (completed_count_ < 7 || group_ >= 12)) continue;
        } else if (!(available >> c & 1)) {
            continue;
        }
        selected_ = c;
        draw_towers(game_type, completed, available);
    }
}

void FrontEnd::objective(const data::Tower& tower) {
    // $A12A: the tower's task in up to three centred lines.
    const auto line = [this](const std::string& s) {
        screen_.centre(s);
        screen_.text(s);
    };
    const int k = tower.klondikes, d = tower.objects_to_destroy;
    const auto text = [this](std::uint32_t a) { return assets_.program.text(a); };
    if (k == 0 && d == 0) {
        for (const auto a : {no_task1, no_task2, no_task3}) {
            line(text(a));
            screen_.cursor.y += 12;
        }
        return;
    }
    if (k != 0) {
        std::string s = text(collect) + char('0' + k) + text(klondike);
        if (k != 1) s += 's';
        if (d != 0) s += text(and_word);
        else if (tower.has_time_limit()) s += text(within);
        line(s);
        screen_.cursor.y += 12;
    }
    if (d != 0) {
        std::string s = text(destroy) + char('0' + d) + text(object_word);
        if (d != 1) s += 's';
        if (tower.has_time_limit()) s += text(within);
        line(s);
        screen_.cursor.y += 12;
    }
    if (tower.has_time_limit()) {
        std::string s = text(time_limit_text) + std::to_string(tower.time_limit) + text(second);
        if (tower.time_limit != 1) s += 's';
        line(s);
    }
}

Flow FrontEnd::information(const data::Tower& tower, int letter, bool& play) {
    screen_.copper = CopperMode::plain;
    screen_.clear();
    screen_.title_bar();
    screen_.frame(0x6F, 0xB3, 0x9E, 0xC0, 13);
    screen_.frame(0xA3, 0xB3, 0xD0, 0xC0, 13);
    screen_.bevel_box(6, 0x1C, 0x139, 0x37, 13);
    screen_.bevel_box(6, 0x3A, 0x139, 0x55, 13);
    screen_.bevel_box(6, 0x58, 0x139, 0x73, 13);
    screen_.bevel_box(6, 0x76, 0x139, 0xA9, 13);
    screen_.text_at(information_text);
    screen_.glyph('A' + letter);
    screen_.cursor.paper = 13;
    screen_.cursor.ink = 15;
    const auto centred = [this](int y, const std::string& s) {
        screen_.cursor.y = y;
        screen_.centre(s);
        screen_.text(s);
    };
    centred(0x2C, tower.name.empty() ? assets_.program.text(not_named) : tower.name);
    centred(0x4A, tower.author.empty() ? assets_.program.text(anonymous) : tower.author);
    // "Zapper, Pusher and Grabber".
    int present = 0;
    for (int s = 0; s < 3; ++s) present += tower.spider_available(s);
    std::string spiders;
    if (present == 0) {
        spiders = assets_.program.text(no_spiders);
    } else {
        const std::uint32_t names[3] = {zapper_name, pusher_name, grabber_name};
        int left = present;
        for (int s = 0; s < 3 && left > 0; ++s) {
            if (!tower.spider_available(s)) continue;
            spiders += assets_.program.text(names[s]);
            if (--left > 0) spiders += assets_.program.text(left == 2 ? comma : and_text);
        }
    }
    centred(0x68, spiders);
    screen_.cursor.y = 0x86;
    objective(tower);
    screen_.cursor.paper = 0;
    screen_.cursor.ink = 15;
    co_await fade_in_program(menu_palette);
    co_await wait_release();
    int choice = 0;
    co_await select(information_areas, choice);
    co_await fade_out();
    play = choice == 0;
}

Flow FrontEnd::standard_game() {
    selected_ = -1;
    for (;;) {
        int choice = -1;
        co_await choose_tower(0, choice);
        if (choice < 0) co_return;
        if (choice >= 9) {
            group_ += choice == 10 ? 1 : -1;
            selected_ = -1;
            continue;
        }
        const auto& tower = tower_in(0, choice);
        bool go = false;
        co_await information(tower, choice, go);
        if (!go) continue;
        int reason = 0;
        co_await play_tower(tower, 0, reason);
        if (reason == 1) {
            co_await won_screen(choice, progress(0, choice));
        } else {
            co_await end_screen(0, reason);
        }
        update_scores(); // $94C6
        screen_.copper = CopperMode::plain;
        if (std::all_of(standard_progress_.begin(), standard_progress_.end(), [](std::uint8_t b) { return b == 0xFF; })) {
            // $95A0: every tower solved.
            all_done_ = true;
            screen_.clear();
            screen_.bevel_box(0x20, 0x20, 0x11F, 0x56, 6);
            screen_.bevel_box(0x20, 0x5E, 0x11F, 0x7A, 3);
            screen_.frame(0x87, 0xB7, 0xB8, 0xC4, 13);
            screen_.text_at(ok_text);
            screen_.text_at(final_text);
            screen_.number(score_, 6);
            co_await fade_in_program(menu_palette);
            co_await wait_release();
            int c = 0;
            co_await select(final_areas, c);
            co_await fade_out();
            co_return;
        }
    }
}

Flow FrontEnd::tutorial_game() {
    selected_ = -1;
    for (;;) {
        int choice = -1;
        co_await choose_tower(2, choice);
        if (choice < 0) co_return;
        const auto& tower = tower_in(2, choice);
        bool go = false;
        co_await information(tower, choice, go);
        if (!go) continue;
        int reason = 0;
        co_await play_tower(tower, 2, reason);
        co_await end_screen(2, reason);
        screen_.copper = CopperMode::plain;
    }
}

Flow FrontEnd::custom_game() {
    co_await fade_out();
    screen_.clear();
    screen_.title_bar();
    menu_boxes(0x1E);
    screen_.text_at(custom_text);
    header();
    co_await fade_in_program(menu_palette);
    for (;;) {
        int choice = 0;
        co_await select(custom_areas, choice);
        if (choice == 3) co_return;
        if (choice == 2) { // $9144: a group from the data disk becomes the custom group
            bool loaded = false;
            co_await requester(FileKind::group, group_title, [&](const std::string& n, bool) {
                const auto file = disk_.read(FileKind::group, n);
                if (!file) return 6;
                if (file->size() < 9 * data::tower_record_size) return 0xB;
                for (std::size_t i = 0; i < 9; ++i)
                    custom_towers_[i] = data::decode_tower(std::span<const std::uint8_t>(*file).subspan(i * data::tower_record_size, data::tower_record_size));
                custom_progress_.fill(0);
                loaded = true;
                return 0;
            });
            if (!loaded) continue;
        }
        if (choice == 1) { // the designer's nine towers ($9046)
            if (designer_group_.empty()) custom_towers_ = assets_.groups.at(15).towers;
            else
                for (std::size_t i = 0; i < 9; ++i) custom_towers_[i] = data::decode_tower(designer_group_[i]);
            custom_progress_.fill(0);
        }
        selected_ = -1;
        for (;;) {
            int t = -1;
            co_await choose_tower(1, t);
            if (t < 0) co_return;
            const auto& tower = tower_in(1, t);
            bool go = false;
            co_await information(tower, t, go);
            if (!go) continue;
            int reason = 0;
            co_await play_tower(tower, 1, reason);
            co_await end_screen(1, reason);
            screen_.copper = CopperMode::plain;
        }
    }
}

Flow FrontEnd::play_tower(const data::Tower& tower, int game_type, int& reason, bool frozen) {
    for (;;) {
        // $969A: the game screen, faded in from black.
        co_await fade_out();
        Play play(assets_, screen_, tower, settings_, game_type);
        active_play_ = &play;
        struct Forget {
            Play*& play;
            ~Forget() { play = nullptr; }
        } forget{active_play_};
        play_paused_ = false;
        last_tower_ = tower;
        if (frozen) play.freeze_enemies();
        sound(SoundEvent::Kind::stop); // the unpacker ($135C6)
        play.draw_screen();
        co_await fade(play.target_palette());
        clicked_ = false;
        int leave = 0;
        sound(SoundEvent::Kind::resume); // the play loop's start ($9976)
        for (;;) {
            co_await game_frame_wait();
            Play::Pointer p{in_.pointer ? in_.x : -1, in_.pointer ? in_.y : -1, in_.left || clicked_};
            if (clicked_ && !in_.left) {
                p.x = clicked_x_;
                p.y = clicked_y_;
            }
            clicked_ = false;
            const auto outcome = play.frame(p);
            sound_frame(play);
            if (outcome == Play::Outcome::playing) continue;
            if (outcome == Play::Outcome::ended) {
                reason = play.end_reason();
                survivors_ = play.survivors();
                // $9A6C: the jingle for a solved tower, otherwise a short sound.
                sound(SoundEvent::Kind::stop);
                if (reason == 1) {
                    piece_ = 1;
                    piece_frames_ = int(assets_.pieces.at(1).samples.size() * 50 / (3546895 / std::max(assets_.pieces.at(1).period, 1))) + 1;
                    while (piece_frames_ > 0) co_await next_frame();
                } else {
                    sound(SoundEvent::Kind::start, lose_sound);
                }
                co_await wait_release();
                co_return;
            }
            sound(SoundEvent::Kind::pause);
            paused_screen_ = screen_.canvas();
            paused_wide_ = false;
            play_paused_ = true;
            co_await wait_release();
            co_await pause_menu(play, leave);
            play_paused_ = false;
            if (leave != 0) break;
            play.resume();
            sound(SoundEvent::Kind::resume);
        }
        if (leave == 6) { // Restart Tower: one more try counted, the tower unpacked again
            if (game_type < 3 && selected_ >= 0) {
                auto& b = progress(game_type, selected_);
                if (b < 0x64) ++b;
            }
            continue;
        }
        reason = leave; // 5: abandoned
        co_return;
    }
}

Flow FrontEnd::pause_menu(Play& play, int& leave) {
    // $9704: the menu bar over the title line; the pause button again (or a
    // click on it) continues.
    leave = 0;
    screen_.fill(1, 1, 318, 10, 0);
    screen_.text_at(pause_bar);
    for (;;) {
        int choice = 0;
        co_await select(pause_areas, choice);
        co_await wait_release();
        switch (choice) {
        case 0:
            co_await setup_page();
            break;
        case 1: {
            co_await view_page();
            // $9F48: the view offsets, the panel in its (new) style and colours.
            play.set_settings(settings_);
            play.redraw_panel();
            const auto target = play.target_palette();
            for (std::size_t i = 33; i < 48; ++i) current_[i] = target[i];
            screen_.palette = current_;
            break;
        }
        case 2:
            co_await info_page(play.tower());
            break;
        case 3:
            co_await exit_page(play.game_type(), leave);
            if (leave != 0) co_return;
            break;
        default:
            // Back to the tower: the title line shows its name again.
            screen_.fill(1, 1, 318, 10, 0);
            play.draw_title();
            play.state().panel.turn = -1;
            co_return;
        }
    }
}

Flow FrontEnd::setup_page() {
    screen_.open_window(0, 0, 0xAF, 0xA8, 0);
    screen_.text_at(setup_text);
    for (;;) {
        screen_.radio(radio_backwards, settings_.backwards_allowed ? 0 : 1);
        screen_.radio(radio_link, settings_.link_cameras ? 0 : 1);
        screen_.radio(radio_info, 2 - settings_.info);
        screen_.radio(radio_timer, 2 - settings_.timer);
        screen_.radio(radio_screen_x, 1); // the picture's place on the monitor: the host frames it
        screen_.radio(radio_screen_y, 0);
        co_await wait_release();
        int choice = 0;
        co_await select(setup_areas, choice);
        if (choice == 0) break; // outside the window
        const int i = choice - 1;
        if (i < 2) settings_.backwards_allowed = i == 0;
        else if (i < 4) settings_.link_cameras = i == 2;
        else if (i < 7) settings_.info = 6 - i;
        else if (i < 10) settings_.timer = 9 - i;
    }
    screen_.close_window();
}

Flow FrontEnd::view_page() {
    screen_.open_window(0x40, 0, 0x11F, 0x92, 0);
    screen_.text_at(view_text);
    for (;;) {
        if (settings_.sights && !(settings_.view_side == 1 && settings_.view_height == 0 && settings_.view_distance == 0))
            settings_.sights = false; // sights only from the spider's own eye
        screen_.radio(radio_sights, settings_.sights ? 0 : 1);
        screen_.radio(radio_view_side, settings_.view_side);
        screen_.radio(radio_view_height, settings_.view_height);
        screen_.radio(radio_view_distance, settings_.view_distance);
        screen_.radio(radio_panel, settings_.panel_style);
        co_await wait_release();
        int choice = 0;
        co_await select(view_areas, choice);
        if (choice == 0) break;
        const int i = choice - 1;
        if (i < 3) {
            settings_.view_side = i;
            if (settings_.view_side != 1 && settings_.view_distance == 2) settings_.view_distance = 1;
        } else if (i < 6) {
            settings_.view_height = i - 3;
        } else if (i < 9) {
            settings_.view_distance = i - 6;
            if (settings_.view_distance == 2) settings_.view_side = 1;
        } else if (i < 11) {
            settings_.sights = i == 9;
            if (settings_.sights) {
                settings_.view_side = 1;
                settings_.view_height = 0;
                settings_.view_distance = 0;
            }
        } else {
            settings_.panel_style = i - 11;
        }
    }
    screen_.close_window();
}

Flow FrontEnd::info_page(const data::Tower& tower) {
    screen_.open_window(0x90, 0, 0x137, 0x48, 0);
    screen_.text_at(info_text);
    if (tower.flags[4]) screen_.text_at(info_not);
    screen_.text_at(info_allowed);
    if (tower.flags[8]) screen_.text_at(info_dont);
    screen_.text_at(info_floor);
    screen_.message(tower.settings[0] + 0x6C);
    screen_.cursor.paper = 0;
    screen_.cursor.ink = 15;
    int choice = 0;
    co_await select(info_areas, choice);
    if (choice != 0) {
        screen_.open_window(0x20, 0x3C, 0x11F, 0x86, 0);
        screen_.text_at(task_text);
        screen_.cursor.y = 0x54;
        objective(tower);
        co_await wait_release();
        co_await select(task_areas, choice);
        screen_.close_window();
    }
    screen_.close_window();
}

Flow FrontEnd::exit_page(int game_type, int& leave) {
    screen_.open_window(0xD8, 0, 0x13F, 0x30, 0);
    screen_.text_at(exit_text);
    int choice = 0;
    co_await select(exit_areas, choice);
    if (choice == 1 || choice == 2) {
        bool yes = game_type == 3;
        if (!yes) co_await confirm(choice == 1 ? abandon_title : restart_tower_title, yes);
        if (yes) leave = choice == 1 ? 5 : 6;
    }
    screen_.close_window();
}

void FrontEnd::start_interlude(int index) {
    const auto& tower = assets_.groups.at(14).towers.at(std::size_t(index % 18));
    interlude_ = std::make_unique<Play>(assets_, screen_, tower, settings_, 5);
    active_play_ = interlude_.get();
    last_tower_ = tower;
    sound(SoundEvent::Kind::stop);
    interlude_->start_demo(index % 18);
    interlude_->draw_demo_screen();
}

void FrontEnd::draw_score_numbers() {
    const int xs[3] = {0x2A, 0x95, 0xFF};
    for (int i = 0; i < 3; ++i) {
        screen_.cursor.x = xs[i];
        screen_.cursor.y = 0x9C;
        screen_.number(shown_bonus_[i], 3);
    }
    screen_.cursor.x = 0xAF;
    screen_.cursor.y = 0xA8;
    screen_.number(score_, 6);
}

Flow FrontEnd::interlude_ok() {
    // $948C: the OK button lights while the pointer is on it; the
    // demonstration runs on until it is clicked.
    bool lit = false;
    const auto toggle = [&] {
        lit = !lit;
        for (int y = 184; y < 196; ++y)
            for (int x = 136; x < 184; ++x) screen_.canvas().set(x, y, std::uint8_t(screen_.canvas().at(x, y) ^ 15));
    };
    for (;;) {
        const bool over = in_.pointer && in_.x >= 0x88 && in_.x < 0xB8 && in_.y >= 0xB8 && in_.y < 0xC3;
        if (over != lit) toggle();
        if (in_.left && lit) break;
        co_await game_frame_wait();
        interlude_->demo_frame();
        sound_frame(*interlude_);
    }
    if (lit) toggle();
    sound(SoundEvent::Kind::pause); // $15A10
    co_await wait_release();
    co_await fade_out();
    screen_.copper = CopperMode::plain;
    interlude_.reset();
    active_play_ = nullptr;
}

Flow FrontEnd::won_screen(int letter, int tries) {
    // $92EC: the tower's bonus, a bonus for few tries and 10 for each spider
    // still standing, counted into the score in steps of up to 20.
    shown_bonus_[0] = 100;
    auto& b = progress(0, letter);
    shown_bonus_[1] = std::max(0, 100 - 4 * tries);
    shown_bonus_[2] = 10 * survivors_;
    b = 0xFF;
    co_await fade_out();
    ++interludes_;
    start_interlude(interludes_ % 6);
    screen_.bevel_box(0, 0x82, 0x13F, 0xB4, 6);
    screen_.frame(0x87, 0xB7, 0xB8, 0xC4, 13);
    screen_.text_at(ok_text);
    screen_.text_at(score_text);
    draw_score_numbers();
    interlude_->demo_frame();
    sound_frame(*interlude_);
    auto target = interlude_->target_palette();
    for (std::size_t i = 0; i < 15; ++i) target[33 + i] = assets_.program.u16(panel_band_menu + std::uint32_t(i) * 2);
    co_await fade(target);
    for (int i = 0; i < 4; ++i) {
        co_await game_frame_wait();
        interlude_->demo_frame();
        sound_frame(*interlude_);
    }
    for (int k = 0; k < 3; ++k) {
        for (int chunk = 20; chunk != 0;) {
            if (chunk > shown_bonus_[k]) {
                chunk >>= 1;
                continue;
            }
            score_ += chunk;
            shown_bonus_[k] -= chunk;
            draw_score_numbers();
            co_await game_frame_wait();
            interlude_->demo_frame();
            sound_frame(*interlude_);
            co_await next_frame();
        }
        if (k < 2)
            for (int i = 0; i < 3; ++i) {
                co_await game_frame_wait();
                interlude_->demo_frame();
                sound_frame(*interlude_);
            }
    }
    co_await interlude_ok();
    selected_ = -1;
}

Flow FrontEnd::end_screen(int game_type, int reason) {
    // $97AA: a solved tower is marked, an unsolved one counts two more
    // tries; a demonstration runs above the reason (won: 0-5 in turn, lost: 6-11).
    if (selected_ >= 0) {
        auto& b = progress(game_type, selected_);
        if (reason == 1) {
            b = 0xFF;
            selected_ = -1;
        } else if (b < 0x63) {
            b = std::uint8_t(b + 2);
        }
    }
    co_await fade_out();
    ++interludes_;
    start_interlude(interludes_ % 6 + (reason == 1 ? 0 : 6));
    const int pen = reason == 1 ? 6 : 3;
    screen_.bevel_box(0x40, 0x82, 0xFF, 0xA8, pen);
    screen_.cursor.paper = reason == 1 ? 6 : 3;
    screen_.cursor.ink = reason == 1 ? 8 : 5;
    screen_.message(message_end + reason);
    screen_.frame(0x87, 0xB7, 0xB8, 0xC4, 13);
    screen_.text_at(ok_text);
    interlude_->demo_frame();
    sound_frame(*interlude_);
    auto target = interlude_->target_palette();
    for (std::size_t i = 0; i < 15; ++i) target[33 + i] = assets_.program.u16(panel_band_menu + std::uint32_t(i) * 2);
    co_await fade(target);
    co_await interlude_ok();
}

Flow FrontEnd::demo_sequence() {
    // $15780: demonstrations 12-17 in turn; a click in the view's left half
    // goes back one, in its right half on one, on the logo below (or Space) ends.
    int index = 12;
    co_await fade_out();
    for (;;) {
        start_interlude(index);
        screen_.picture(assets_.title_memory, logo_at, 0, 0x90, 20, 35);
        interlude_->demo_frame();
        sound_frame(*interlude_);
        std::array<data::Colour, 48> target = interlude_->target_palette();
        for (std::size_t i = 0; i < 15; ++i) target[33 + i] = assets_.program.u16(var(0x249A) + std::uint32_t(i) * 2);
        co_await fade(target);
        int next = index;
        for (;;) {
            co_await game_frame_wait();
            interlude_->demo_frame();
            sound_frame(*interlude_);
            if (in_.key == ' ') {
                sound(SoundEvent::Kind::pause);
                co_await fade_out();
                interlude_.reset();
                active_play_ = nullptr;
                screen_.copper = CopperMode::plain;
                co_return;
            }
            if (in_.left) {
                if (in_.y >= 0x80) {
                    sound(SoundEvent::Kind::pause);
                    co_await wait_release();
                    co_await fade_out();
                    interlude_.reset();
                    active_play_ = nullptr;
                    screen_.copper = CopperMode::plain;
                    co_return;
                }
                next = in_.x < 0xA0 ? index - 1 : index + 1;
                break;
            }
            if (interlude_->demo_script_ended()) {
                next = index + 1;
                break;
            }
        }
        co_await fade_out();
        index = next < 12 ? 17 : next > 17 ? 12 : next;
    }
}

} // namespace babel::front
