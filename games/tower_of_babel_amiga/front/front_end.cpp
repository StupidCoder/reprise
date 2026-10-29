#include "tower_of_babel_amiga/front/front_end.hpp"
#include <algorithm>
#include <random>
#include <sstream>
#include <type_traits>
#include <utility>

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
    tasks_.emplace_back(tasks::Game{});
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
    if (active_play()->shows_view()) {
        active_play()->redraw(canvas, margin, alpha);
        if (margin > 0) {
            const auto colour = base.at(15, 15);
            for (int x = 15; x <= w - 16; ++x) canvas.set(x, 15, colour), canvas.set(x, 128, colour);
            for (int y = 15; y <= 128; ++y) canvas.set(15, y, colour), canvas.set(w - 16, y, colour);
        }
    }
    return canvas;
}

platform::Image FrontEnd::picture(int margin, bool smooth, double fraction) const {
    const bool live = active_play() && screen_.copper == CopperMode::game;
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
    run_tasks();
}

const Play* FrontEnd::active_play() const {
    for (auto it = tasks_.rbegin(); it != tasks_.rend(); ++it)
        if (const auto* t = std::get_if<tasks::PlayTower>(&*it); t && t->play) return &*t->play;
    return interlude_.get();
}

// --- the tasks -------------------------------------------------------------

void FrontEnd::run_tasks() {
    // The task last called goes on; one that ends hands its result to the
    // one below, which goes on in the same blank.
    while (!tasks_.empty()) {
        const Go go = std::visit([this](auto& task) { return run(task); }, tasks_.back());
        if (go == Go::wait) return;
        if (go == Go::end) tasks_.pop_back();
    }
}

void FrontEnd::sound_frame(const Play& play) {
    sounds_.push_back({SoundEvent::Kind::frame, play.sound_frame(), 0, play.volume_offset()});
}

FrontEnd::Go FrontEnd::run(tasks::WaitFrames& t) {
    if (t.left-- <= 0) return Go::end;
    return Go::wait;
}

FrontEnd::Go FrontEnd::run(tasks::WaitRelease&) {
    return in_.left || in_.right ? Go::wait : Go::end;
}

FrontEnd::Go FrontEnd::run(tasks::Select& t) {
    // $112EA: the vertical blank keeps the area under the pointer lit; a
    // press of the left button over one chooses it.
    // The choice starts as none ($E00 = -1) until a vertical blank has looked.
    if (!std::exchange(t.looking, true)) {
        t.menu = Menu(assets_.program, t.areas);
        return Go::wait;
    }
    const int x = in_.pointer ? in_.x : 0x1F40, y = in_.pointer ? in_.y : 0x8000;
    t.menu.hover(screen_, x, y);
    const int choice = t.menu.at(x, y);
    if (!in_.left || choice < 0) return Go::wait;
    t.menu.unhover(screen_);
    return end(choice);
}

FrontEnd::Go FrontEnd::run(tasks::GameFrameWait&) {
    // $87EE: the frame ends once the timer has counted 14 since the last.
    if (ticks_ < 14) return Go::wait;
    ticks_ -= 14;
    return Go::end;
}

tasks::Fade FrontEnd::fade(const std::array<data::Colour, 48>& target, int first, int count) const {
    tasks::Fade f;
    f.target = target;
    f.first = first;
    f.count = count;
    return f;
}

tasks::Fade FrontEnd::fade_out() const {
    std::array<data::Colour, 48> black{};
    for (std::size_t i = 0; i < 48; ++i) black[i] = assets_.program.u16(black_palette + std::uint32_t(i) * 2);
    return fade(black);
}

tasks::Fade FrontEnd::fade_in_program(std::uint32_t palette) const {
    std::array<data::Colour, 48> target{};
    for (std::size_t i = 0; i < 48; ++i) target[i] = assets_.program.u16(palette + std::uint32_t(i) * 2);
    return fade(target);
}

FrontEnd::Go FrontEnd::run(tasks::Fade& t) {
    if (!std::exchange(t.started, true)) {
        t.fast = fast_fades_;
        if (t.fast) { // $12628: at once, then two frames
            for (int i = t.first; i < t.first + t.count; ++i) current_[std::size_t(i)] = t.target[std::size_t(i)];
            screen_.palette = current_;
            return Go::wait;
        }
        // $125B2 / $1255C: each component in 16 steps of (target - start) / 16,
        // from start * 16 + 7, one step a frame.
        for (std::size_t i = 0; i < 48; ++i)
            for (int k = 0; k < 3; ++k) {
                const int shift = 8 - 4 * k;
                const int from = current_[i] >> shift & 15, to = t.target[i] >> shift & 15;
                t.value[i][std::size_t(k)] = from * 16 + 7;
                t.step[i][std::size_t(k)] = to - from;
            }
    }
    if (t.fast) return t.steps++ == 0 ? Go::wait : Go::end;
    if (t.steps == 16) return Go::end;
    for (int i = t.first; i < t.first + t.count; ++i) {
        auto& v = t.value[std::size_t(i)];
        data::Colour c = 0;
        for (int k = 0; k < 3; ++k) {
            v[std::size_t(k)] += t.step[std::size_t(i)][std::size_t(k)];
            c = data::Colour(c << 4 | (v[std::size_t(k)] >> 4 & 15));
        }
        current_[std::size_t(i)] = c;
    }
    screen_.palette = current_;
    ++t.steps;
    return Go::wait;
}

// --- the game ---------------------------------------------------------------

FrontEnd::Go FrontEnd::run(tasks::Game& t) {
    using enum tasks::Game::At;
    if (t.at == start) {
        t.at = player;
        return call(tasks::Title{}, tasks::Credits{});
    }
    return call(tasks::NameEntry{}, tasks::MainMenu{}); // the main menu ends for Restart Game
}

FrontEnd::Go FrontEnd::run(tasks::Title& t) {
    using enum tasks::Title::At;
    if (t.at == start) {
        // $7EF6: the logo and the globe in the title's three colour bands, the
        // title music; the program then loads its main data (left out here).
        screen_.copper = CopperMode::title;
        screen_.clear();
        piece_ = 0;
        piece_frames_ = int(assets_.pieces.at(0).samples.size() * 50 / (3546895 / std::max(assets_.pieces.at(0).period, 1))) + 1;
        screen_.picture(assets_.title_memory, logo_at, 0, 10, 20, 35);
        screen_.picture(assets_.title_memory, globe_at, 80, 61, 10, 133);
        t.at = music;
        return call(fade_in_program(title_palette));
    }
    return piece_frames_ > 0 ? Go::wait : Go::end; // until the music ends
}

FrontEnd::Go FrontEnd::run(tasks::Credits& t) {
    // $15A2E: the lower bands alternate between the credits and the globe
    // (about ten seconds each) until Space is pressed.
    using enum tasks::Credits::At;
    const auto colours = [this](std::uint32_t palette) {
        std::array<data::Colour, 48> c{};
        for (std::size_t i = 0; i < 48; ++i) c[i] = assets_.program.u16(palette + std::uint32_t(i) * 2);
        return c;
    };
    const auto stop = [this] { return in_.key == ' ' || in_.left; };
    for (;;) switch (t.at) {
        case start:
            t.at = faded;
            return call(fade(colours(black_palette), 16, 32));
        case faded:
            screen_.copper = CopperMode::title;
            t.at = show_credits;
            continue;
        case show_credits:
            screen_.fill(0, 0x3A, 0x13F, 0xC7, 0);
            screen_.text_at(credits_text);
            t.at = credits_wait;
            return call(fade(colours(credits_palette), 16, 32), tasks::CreditsWait{});
        case credits_wait:
            if (stop()) {
                t.at = leave;
                continue;
            }
            t.at = credits_shown;
            return call(fade(colours(black_palette), 16, 32));
        case credits_shown:
            screen_.fill(0, 0x3A, 0x13F, 0xC7, 0);
            screen_.picture(assets_.title_memory, globe_at, 80, 61, 10, 133);
            t.at = globe_wait;
            return call(fade(colours(title_palette), 16, 32), tasks::CreditsWait{});
        case globe_wait:
            if (stop()) {
                t.at = leave;
                continue;
            }
            t.at = globe_shown;
            return call(fade(colours(black_palette), 16, 32));
        case globe_shown:
            t.at = show_credits;
            continue;
        case leave:
            t.at = left;
            return call(fade_out());
        case left:
            screen_.copper = CopperMode::plain;
            return Go::end;
        }
}

FrontEnd::Go FrontEnd::run(tasks::CreditsWait& t) {
    // 100 waits of 11 timer ticks, watching for Space (or a click).
    for (;;) {
        if (!t.timing) {
            if (t.count == 100) return Go::end;
            t.start = ticks_;
            t.timing = true;
        }
        if (ticks_ - t.start <= 10) {
            if (in_.key == ' ' || (in_.left && !previous_.left)) return Go::end;
            return Go::wait;
        }
        ++t.count;
        t.timing = false;
    }
}

FrontEnd::Go FrontEnd::run(tasks::NameEntry& t) {
    using enum tasks::NameEntry::At;
    switch (t.at) {
    case start:
        t.at = faded;
        return call(fade_out());
    case faded:
        screen_.copper = CopperMode::plain;
        screen_.clear();
        screen_.title_bar();
        screen_.frame(0x36, 0x4C, 0x109, 0x80, 13);
        screen_.text_at(name_text);
        t.at = shown;
        // The name record ($E46): 28 characters at (82, 110) short of x 260; a
        // click in the box (54, 76)-(265, 128) moves the cursor, outside it ends.
        return call(fade_in_program(menu_palette), tasks::EditField{{0x52, 0x6E, 0x1C, 0x104, 0x36, 0x4C, 0x109, 0x80, {}, 0, false}});
    case shown:
        break;
    }
    name_ = edited_;
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
    return Go::end;
}

FrontEnd::Go FrontEnd::run(tasks::EditField& t) {
    // $12096 until Return or a click outside the box; the text in edited_.
    using enum tasks::EditField::At;
    auto& e = t.editor;
    for (;;) switch (t.at) {
        case start:
            e.cursor = int(e.text.size());
            t.at = draw;
            continue;
        case draw:
            draw_editor(e);
            t.end_x = screen_.cursor.x;
            t.at = typed;
            return Go::wait;
        case typed: {
            const int key = in_.key;
            t.at = draw;
            if (e.digits && key >= 0x20 && (key < '0' || key > '9')) continue;
            bool finished = !edit_key(e, key, t.end_x);
            if (!finished && in_.left && !previous_.left) {
                if (in_.x >= e.box_x0 && in_.x < e.box_x1 && in_.y >= e.box_y0 && in_.y < e.box_y1) edit_click(e, in_.x);
                else finished = true;
            }
            if (!finished) continue;
            draw_editor(e, false); // $12304 once more, the cursor gone
            t.at = released;
            return call(tasks::WaitRelease{});
        }
        case released:
            edited_ = e.text;
            return Go::end;
        }
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

FrontEnd::Go FrontEnd::run(tasks::DiskError& t) {
    using enum tasks::DiskError::At;
    switch (t.at) {
    case start: {
        screen_.open_window(0x1C, 0x88, 0x124, 0xB8, 0);
        screen_.text_at(disk_error_text);
        screen_.number(t.code, 2);
        screen_.glyph(')');
        screen_.cursor.ink = 9;
        const auto message = screen_.message_text(0x80 + (t.code & 15));
        screen_.centre(message);
        screen_.cursor.y = 0xA2;
        screen_.text(message);
        screen_.cursor.ink = 15;
        t.at = chosen;
        return call(tasks::WaitFrames{50}, select(error_areas)); // $12654 twice: 50 timer ticks each
    }
    case chosen:
        break;
    }
    screen_.close_window();
    return Go::end;
}

FrontEnd::Go FrontEnd::run(tasks::ReplaceFile& t) {
    using enum tasks::ReplaceFile::At;
    switch (t.at) {
    case start:
        t.at = released;
        return call(tasks::WaitRelease{});
    case released:
        screen_.open_window(0x4C, 0x48, 0xF3, 0x7D, 0);
        screen_.text_at(replace_text);
        screen_.frame(0x8F, 0x6B, 0xB1, 0x78, 13);
        t.at = chosen;
        return call(select(replace_areas));
    case chosen:
        t.yes = result_ == 1;
        screen_.close_window();
        t.at = answered;
        return call(tasks::WaitRelease{});
    case answered:
        break;
    }
    return end(t.yes);
}

void FrontEnd::draw_requester_page(const tasks::Requester& t) {
    // $1297A: the page number, then up to ten names.
    screen_.cursor.x = 0x97;
    screen_.cursor.y = 0x38;
    screen_.cursor.ink = 9;
    screen_.number(t.page + 1, 2);
    screen_.cursor.ink = 15;
    screen_.fill(0x48, 0x4A, 0x90, 0xBE, 0);
    for (int i = 0; i < 10 && t.page * 10 + i < int(t.names.size()); ++i) {
        screen_.cursor.x = 0x48;
        screen_.cursor.y = 0x4A + 12 * i;
        screen_.text(t.names[std::size_t(t.page * 10 + i)].substr(0, 8));
    }
}

void FrontEnd::read_directory(tasks::Requester& t) {
    // $12A10 / $12AFA: the files of this kind, the room left, the pages.
    t.names = disk_.names(t.kind);
    t.page = 0;
    screen_.cursor.ink = 9;
    screen_.cursor.x = 0x55;
    screen_.cursor.y = 0x2C;
    screen_.number(long(t.names.size()), 3);
    screen_.cursor.x = 0xDB;
    screen_.number(DataDisk::entries - disk_.files(), 3);
    screen_.cursor.x = 0xBC;
    screen_.cursor.y = 0x38;
    screen_.number((std::max<int>(int(t.names.size()), 1) - 1) / 10 + 1, 2);
    screen_.cursor.ink = 15;
    draw_requester_page(t);
}

FrontEnd::Go FrontEnd::run(tasks::Requester& t) {
    // $126C0: a window listing the files of one kind ten to a page, a name
    // to type or pick, the action and Change Disk.
    using enum tasks::Requester::At;
    auto& e = file_name_;
    for (;;) switch (t.at) {
        case start:
            screen_.open_window(0x40, 0x18, 0xFF, 0xC2, 0);
            screen_.frame(0x45, 0x47, 0x93, 0xC0, 13);
            screen_.frame(0x9D, 0x47, 0xC9, 0x54, 13);
            screen_.frame(0x9D, 0xB3, 0xC9, 0xC0, 13);
            screen_.frame(0x9D, 0x83, 0xF1, 0x90, 13);
            screen_.frame(0x9D, 0x9B, 0xF1, 0xA8, 13);
            screen_.text_at(t.title);
            screen_.text_at(requester_text);
            read_directory(t);
            e.cursor = int(e.text.size());
            t.menu = Menu(assets_.program, requester_areas);
            t.at = draw;
            return call(tasks::WaitRelease{});
        case draw:
            draw_editor(e);
            t.end_x = screen_.cursor.x;
            t.at = typed;
            return Go::wait;
        case typed: {
            t.at = draw;
            const int x = in_.pointer ? in_.x : 0x1F40, y = in_.pointer ? in_.y : 0x8000;
            t.menu.hover(screen_, x, y);
            const int key = in_.key;
            if (key != 0 && key != '.' && key != ' ') {
                if (!edit_key(e, key, t.end_x)) {
                    t.ok = true;
                    t.at = act;
                }
                continue;
            }
            const int choice = t.menu.at(x, y);
            if (!in_.left || choice < 0) continue;
            if (choice == 5) {
                edit_click(e, in_.x);
                continue;
            }
            if (choice < 2) {
                t.ok = choice == 1;
                t.at = act;
                continue;
            }
            if (choice == 2 && t.page > 0) {
                --t.page;
                draw_requester_page(t);
            } else if (choice == 3 && (t.page + 1) * 10 < int(t.names.size())) {
                ++t.page;
                draw_requester_page(t);
            } else if (choice == 4) {
                read_directory(t);
            } else if (choice >= 6 && t.page * 10 + choice - 6 < int(t.names.size())) {
                e.text = t.names[std::size_t(t.page * 10 + choice - 6)];
                e.cursor = int(e.text.size());
            }
            return call(tasks::WaitRelease{});
        }
        case act:
            t.menu.unhover(screen_);
            if (t.ok && !e.text.empty()) {
                t.error = disk_action(t.action, e.text, false);
                t.done = t.error == 0;
            }
            if (t.error == -1) { // the file exists: replace it?
                t.at = replace_answered;
                return call(tasks::ReplaceFile{});
            }
            t.at = error_shown;
            continue;
        case replace_answered:
            t.error = result_ ? disk_action(t.action, e.text, true) : 0;
            t.done = result_ && t.error == 0;
            t.at = error_shown;
            continue;
        case error_shown:
            t.at = closed;
            if (t.error > 0) return call(tasks::DiskError{t.error});
            continue;
        case closed:
            screen_.close_window();
            t.at = finished;
            return call(tasks::WaitRelease{});
        case finished:
            return end(t.done);
        }
}

int FrontEnd::disk_action(DiskAction action, const std::string& n, bool replace) {
    switch (action) {
    case DiskAction::load_position: {
        const auto file = disk_.read(FileKind::position, n);
        if (!file) return 6;                   // File Not Found
        if (!load_position(*file)) return 0xB; // Invalid File
        return 0;
    }
    case DiskAction::save_position:
        if (!replace && disk_.exists(FileKind::position, n)) return -1;
        return disk_.write(FileKind::position, n, position()) ? 0 : 8; // Unable To Write to Disk
    case DiskAction::update_scores: {
        // $A8D4: merged with the table in the file (the higher score for a
        // name, new names added), the best ten kept and written back.
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
    }
    case DiskAction::load_custom_group: { // $9144: a group from the data disk becomes the custom group
        const auto file = disk_.read(FileKind::group, n);
        if (!file) return 6;
        if (file->size() < 9 * data::tower_record_size) return 0xB;
        for (std::size_t i = 0; i < 9; ++i)
            custom_towers_[i] = data::decode_tower(std::span<const std::uint8_t>(*file).subspan(i * data::tower_record_size, data::tower_record_size));
        custom_progress_.fill(0);
        return 0;
    }
    case DiskAction::load_group: {
        const auto f = disk_.read(FileKind::group, n);
        if (!f) return 6;
        if (f->size() < 9 * data::tower_record_size) return 0xB;
        for (std::size_t i = 0; i < 9; ++i)
            std::copy_n(f->begin() + long(i * data::tower_record_size), data::tower_record_size, designer_group_[i].begin());
        return 0;
    }
    case DiskAction::save_group: {
        if (!replace && confirm_replace_ && disk_.exists(FileKind::group, n)) return -1;
        std::vector<std::uint8_t> bytes;
        for (const auto& r : designer_group_) bytes.insert(bytes.end(), r.begin(), r.end());
        return disk_.write(FileKind::group, n, bytes) ? 0 : 8;
    }
    case DiskAction::load_tower: {
        const auto f = disk_.read(FileKind::tower, n);
        if (!f) return 6;
        if (f->size() < data::tower_record_size) return 0xB;
        std::copy_n(f->begin(), data::tower_record_size, loaded_tower_.begin());
        return 0;
    }
    case DiskAction::save_tower: {
        if (!replace && confirm_replace_ && disk_.exists(FileKind::tower, n)) return -1;
        design_.update_spiders();
        const auto r = pack_design();
        return disk_.write(FileKind::tower, n, {r.begin(), r.end()}) ? 0 : 8;
    }
    case DiskAction::delete_group:
        return disk_.remove(FileKind::group, n) ? 0 : 6;
    case DiskAction::delete_tower:
        return disk_.remove(FileKind::tower, n) ? 0 : 6;
    }
    return 0;
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

FrontEnd::Go FrontEnd::run(tasks::SaveLoad& t) {
    using enum tasks::SaveLoad::At;
    for (;;) switch (t.at) {
        case start:
            t.at = drawn;
            return call(fade_out());
        case drawn:
            screen_.clear();
            screen_.title_bar();
            menu_boxes(0x1C);
            screen_.text_at(save_load_text);
            t.at = choose;
            return call(fade_in_program(menu_palette));
        case choose:
            t.at = chosen;
            return call(tasks::WaitRelease{}, select(save_load_areas));
        case chosen:
            t.choice = result_;
            if (t.choice == 2) return Go::end;
            t.at = done;
            if (t.choice == 0) return call(tasks::Requester{FileKind::position, load_title, DiskAction::load_position});
            return call(tasks::Requester{FileKind::position, save_title, DiskAction::save_position});
        case done:
            if (t.choice == 0 && result_) return Go::end; // back to the main menu with the loaded name and score
            t.at = choose;
            continue;
        }
}

void FrontEnd::draw_high_scores() {
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
}

FrontEnd::Go FrontEnd::run(tasks::HighScores& t) {
    using enum tasks::HighScores::At;
    switch (t.at) {
    case start:
        t.at = faded;
        return call(fade_out());
    case faded:
        screen_.clear();
        screen_.title_bar();
        menu_boxes(0x60);
        screen_.text_at(high_score_text);
        [[fallthrough]];
    case updated:
        draw_high_scores();
        t.at = chosen;
        return call(fade_in_program(menu_palette), select(high_score_areas));
    case chosen:
        if (result_ == 0) return Go::end;
        t.at = updated;
        return call(tasks::Requester{FileKind::scores, update_title, DiskAction::update_scores});
    }
    return Go::end;
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

FrontEnd::Go FrontEnd::run(tasks::Confirm& t) {
    // $110D2 (with its own question: the designer's letters).
    using enum tasks::Confirm::At;
    switch (t.at) {
    case start:
        t.at = released;
        return call(tasks::WaitRelease{});
    case released:
        screen_.open_window(0x4C, 0x48, 0xF3, 0x7D, 0);
        if (t.question.empty()) screen_.text_at(t.title);
        else screen_.text(t.question);
        screen_.text_at(sure_text);
        screen_.frame(0x8F, 0x6B, 0xB1, 0x78, 13);
        t.at = chosen;
        return call(select(yes_areas));
    case chosen:
        t.yes = result_ == 0;
        screen_.close_window();
        t.at = answered;
        return call(tasks::WaitRelease{});
    case answered:
        break;
    }
    return end(t.yes);
}

FrontEnd::Go FrontEnd::run(tasks::MainMenu& t) {
    using enum tasks::MainMenu::At;
    for (;;) switch (t.at) {
        case start:
            update_scores(); // $8D28
            t.at = redraw;
            continue;
        case redraw:
            t.at = drawn;
            return call(fade_out());
        case drawn:
            screen_.clear();
            screen_.title_bar();
            menu_boxes(0x7E);
            screen_.text_at(main_text);
            header();
            t.at = choose;
            return call(fade_in_program(menu_palette), tasks::WaitRelease{});
        case choose:
            t.at = chosen;
            return call(select(main_menu_areas));
        case chosen:
            t.at = redraw;
            switch (result_) {
            case 0: return call(tasks::PlayGame{});
            case 1: return call(tasks::SaveLoad{});
            case 2: return call(tasks::HighScores{});
            case 3: return call(tasks::Designer{});
            case 4: return call(tasks::DemoSequence{});
            case 5:
                t.at = restart;
                return call(tasks::Confirm{restart_title});
            default:
                t.at = choose;
                continue;
            }
        case restart:
            if (result_) return Go::end; // Restart Game
            t.at = choose;
            continue;
        }
}

FrontEnd::Go FrontEnd::run(tasks::PlayGame& t) {
    using enum tasks::PlayGame::At;
    for (;;) switch (t.at) {
        case start:
            t.at = drawn;
            return call(fade_out());
        case drawn:
            screen_.clear();
            screen_.title_bar();
            screen_.text_at(game_type_text);
            menu_boxes(0x1E);
            header();
            t.at = choose;
            return call(fade_in_program(menu_palette));
        case choose:
            t.at = chosen;
            return call(select(game_type_areas));
        case chosen:
            if (result_ == 0 && all_done_) { // nothing left to play in the standard game
                t.at = choose;
                continue;
            }
            t.at = played;
            if (result_ == 0) return call(tasks::StandardGame{});
            if (result_ == 1) return call(tasks::CustomGame{});
            if (result_ == 2) return call(tasks::TutorialGame{});
            return Go::end;
        case played:
            return Go::end;
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

FrontEnd::Go FrontEnd::run(tasks::ChooseTower& t) {
    using enum tasks::ChooseTower::At;
    for (;;) switch (t.at) {
        case start:
            t.completed = completed_mask(t.game_type, completed_count_);
            t.available = 0x1FF;
            if (t.game_type == 0) {
                t.available = 0;
                for (int i = 0; i < 9; ++i)
                    if (t.completed >> i & 1) t.available |= assets_.program.u16(neighbours + std::uint32_t(i) * 2);
                t.available &= ~t.completed;
                if (t.completed == 0) t.available = 7;
            } else if (t.game_type == 1) {
                t.available = ~t.completed & 0x1FF;
            }
            if (selected_ >= 9) selected_ = -1;
            t.at = faded;
            return call(fade_out());
        case faded:
            screen_.clear();
            screen_.title_bar();
            screen_.text_at(chooser_text);
            screen_.bevel_box(4, 0x9A, 0x13B, 0xA9, 13);
            screen_.frame(0x6F, 0xB3, 0x9E, 0xC0, 13);
            screen_.frame(0xA3, 0xB3, 0xD0, 0xC0, 13);
            header();
            if (t.game_type == 0) {
                screen_.text_at(group_text);
                screen_.number(group_, 2);
                screen_.text_at(percent_text);
                const int solved = int(std::count(standard_progress_.begin(), standard_progress_.end(), 0xFF));
                int percent = solved * 100 / 117;
                if (percent != 99 && solved * 100 % 117 != 0) ++percent;
                screen_.number(percent, 3);
            }
            draw_towers(t.game_type, t.completed, t.available);
            t.at = choose;
            return call(fade_in_program(menu_palette), tasks::WaitRelease{});
        case choose:
            t.at = chosen;
            return call(select(chooser_areas));
        case chosen: {
            const int c = result_;
            t.at = choose;
            if (c == 12) return end(-1);
            if (c == 11) {
                if (selected_ < 0) continue;
                return end(selected_);
            }
            if (c == 9 || c == 10) {
                if (t.game_type != 0) continue;
                if (c == 9 && group_ == 0) continue;
                if (c == 10 && (completed_count_ < 7 || group_ >= 12)) continue;
            } else if (!(t.available >> c & 1)) {
                continue;
            }
            selected_ = c;
            draw_towers(t.game_type, t.completed, t.available);
            continue;
        }
        }
}

FrontEnd::Go FrontEnd::run(tasks::Information& t) {
    using enum tasks::Information::At;
    switch (t.at) {
    case start: {
        const auto& tower = tower_in(t.game_type, t.letter);
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
        screen_.glyph('A' + t.letter);
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
        t.at = chosen;
        return call(fade_in_program(menu_palette), tasks::WaitRelease{}, select(information_areas));
    }
    case chosen:
        t.play = result_ == 0;
        t.at = faded;
        return call(fade_out());
    case faded:
        break;
    }
    return end(t.play);
}

FrontEnd::Go FrontEnd::run(tasks::StandardGame& t) {
    using enum tasks::StandardGame::At;
    for (;;) switch (t.at) {
        case start:
            selected_ = -1;
            t.at = choose;
            continue;
        case choose:
            t.at = chosen;
            return call(tasks::ChooseTower{0});
        case chosen:
            t.choice = result_;
            if (t.choice < 0) return Go::end;
            if (t.choice >= 9) {
                group_ += t.choice == 10 ? 1 : -1;
                selected_ = -1;
                t.at = choose;
                continue;
            }
            t.at = informed;
            return call(tasks::Information{0, t.choice});
        case informed:
            if (!result_) {
                t.at = choose;
                continue;
            }
            t.at = played;
            return call(tasks::PlayTower{tower_in(0, t.choice), 0});
        case played:
            t.at = after;
            if (result_ == 1) return call(tasks::WonScreen{t.choice, progress(0, t.choice)});
            return call(tasks::EndScreen{0, result_});
        case after:
            update_scores(); // $94C6
            screen_.copper = CopperMode::plain;
            t.at = choose;
            if (!std::all_of(standard_progress_.begin(), standard_progress_.end(), [](std::uint8_t b) { return b == 0xFF; }))
                continue;
            // $95A0: every tower solved.
            all_done_ = true;
            screen_.clear();
            screen_.bevel_box(0x20, 0x20, 0x11F, 0x56, 6);
            screen_.bevel_box(0x20, 0x5E, 0x11F, 0x7A, 3);
            screen_.frame(0x87, 0xB7, 0xB8, 0xC4, 13);
            screen_.text_at(ok_text);
            screen_.text_at(final_text);
            screen_.number(score_, 6);
            t.at = final_faded;
            return call(fade_in_program(menu_palette), tasks::WaitRelease{}, select(final_areas), fade_out());
        case final_faded:
            return Go::end;
        }
}

FrontEnd::Go FrontEnd::run(tasks::TutorialGame& t) {
    using enum tasks::TutorialGame::At;
    for (;;) switch (t.at) {
        case start:
            selected_ = -1;
            t.at = choose;
            continue;
        case choose:
            t.at = chosen;
            return call(tasks::ChooseTower{2});
        case chosen:
            t.choice = result_;
            if (t.choice < 0) return Go::end;
            t.at = informed;
            return call(tasks::Information{2, t.choice});
        case informed:
            if (!result_) {
                t.at = choose;
                continue;
            }
            t.at = played;
            return call(tasks::PlayTower{tower_in(2, t.choice), 2});
        case played:
            t.at = after;
            return call(tasks::EndScreen{2, result_});
        case after:
            screen_.copper = CopperMode::plain;
            t.at = choose;
            continue;
        }
}

FrontEnd::Go FrontEnd::run(tasks::CustomGame& t) {
    using enum tasks::CustomGame::At;
    for (;;) switch (t.at) {
        case start:
            t.at = drawn;
            return call(fade_out());
        case drawn:
            screen_.clear();
            screen_.title_bar();
            menu_boxes(0x1E);
            screen_.text_at(custom_text);
            header();
            t.at = choose;
            return call(fade_in_program(menu_palette));
        case choose:
            t.at = chosen;
            return call(select(custom_areas));
        case chosen:
            if (result_ == 3) return Go::end;
            if (result_ == 2) {
                t.at = group_loaded;
                return call(tasks::Requester{FileKind::group, group_title, DiskAction::load_custom_group});
            }
            if (result_ == 1) { // the designer's nine towers ($9046)
                if (designer_group_.empty()) custom_towers_ = assets_.groups.at(15).towers;
                else
                    for (std::size_t i = 0; i < 9; ++i) custom_towers_[i] = data::decode_tower(designer_group_[i]);
                custom_progress_.fill(0);
            }
            selected_ = -1;
            t.at = choose_tower;
            continue;
        case group_loaded:
            if (!result_) {
                t.at = choose;
                continue;
            }
            selected_ = -1;
            t.at = choose_tower;
            continue;
        case choose_tower:
            t.at = tower_chosen;
            return call(tasks::ChooseTower{1});
        case tower_chosen:
            t.choice = result_;
            if (t.choice < 0) return Go::end;
            t.at = informed;
            return call(tasks::Information{1, t.choice});
        case informed:
            if (!result_) {
                t.at = choose_tower;
                continue;
            }
            t.at = played;
            return call(tasks::PlayTower{tower_in(1, t.choice), 1});
        case played:
            t.at = after;
            return call(tasks::EndScreen{1, result_});
        case after:
            screen_.copper = CopperMode::plain;
            t.at = choose_tower;
            continue;
        }
}

FrontEnd::Go FrontEnd::run(tasks::PlayTower& t) {
    using enum tasks::PlayTower::At;
    for (;;) switch (t.at) {
        case start:
            // $969A: the game screen, faded in from black.
            t.at = unpack;
            return call(fade_out());
        case unpack:
            t.play.emplace(assets_, screen_, t.tower, settings_, t.game_type);
            play_paused_ = false;
            last_tower_ = t.tower;
            if (t.frozen) t.play->freeze_enemies();
            sound(SoundEvent::Kind::stop); // the unpacker ($135C6)
            t.play->draw_screen();
            t.at = faded_in;
            return call(fade(t.play->target_palette()));
        case faded_in:
            clicked_ = false;
            t.leave = 0;
            sound(SoundEvent::Kind::resume); // the play loop's start ($9976)
            t.at = frame;
            continue;
        case frame:
            t.at = framed;
            return call(tasks::GameFrameWait{});
        case framed: {
            auto& play = *t.play;
            Play::Pointer p{in_.pointer ? in_.x : -1, in_.pointer ? in_.y : -1, in_.left || clicked_};
            if (clicked_ && !in_.left) {
                p.x = clicked_x_;
                p.y = clicked_y_;
            }
            clicked_ = false;
            const auto outcome = play.frame(p);
            sound_frame(play);
            t.at = frame;
            if (outcome == Play::Outcome::playing) continue;
            if (outcome == Play::Outcome::ended) {
                t.reason = play.end_reason();
                survivors_ = play.survivors();
                // $9A6C: the jingle for a solved tower, otherwise a short sound.
                sound(SoundEvent::Kind::stop);
                if (t.reason == 1) {
                    piece_ = 1;
                    piece_frames_ = int(assets_.pieces.at(1).samples.size() * 50 / (3546895 / std::max(assets_.pieces.at(1).period, 1))) + 1;
                    t.at = jingle;
                    continue;
                }
                sound(SoundEvent::Kind::start, lose_sound);
                t.at = released;
                return call(tasks::WaitRelease{});
            }
            sound(SoundEvent::Kind::pause);
            paused_screen_ = screen_.canvas();
            paused_wide_ = false;
            play_paused_ = true;
            t.at = paused;
            return call(tasks::WaitRelease{}, tasks::PauseMenu{});
        }
        case jingle:
            if (piece_frames_ > 0) return Go::wait;
            t.at = released;
            return call(tasks::WaitRelease{});
        case released:
            return end(t.reason);
        case paused:
            t.leave = result_;
            play_paused_ = false;
            if (t.leave == 0) {
                t.play->resume();
                sound(SoundEvent::Kind::resume);
                t.at = frame;
                continue;
            }
            t.at = after;
            continue;
        case after:
            t.play.reset();
            if (t.leave == 6) { // Restart Tower: one more try counted, the tower unpacked again
                if (t.game_type < 3 && selected_ >= 0) {
                    auto& b = progress(t.game_type, selected_);
                    if (b < 0x64) ++b;
                }
                t.at = start;
                continue;
            }
            return end(t.leave); // 5: abandoned
        }
}

FrontEnd::Go FrontEnd::run(tasks::PauseMenu& t) {
    // $9704: the menu bar over the title line; the pause button again (or a
    // click on it) continues.
    using enum tasks::PauseMenu::At;
    auto& play = *active_play();
    for (;;) switch (t.at) {
        case start:
            screen_.fill(1, 1, 318, 10, 0);
            screen_.text_at(pause_bar);
            t.at = choose;
            continue;
        case choose:
            t.at = chosen;
            return call(select(pause_areas));
        case chosen:
            t.choice = result_;
            t.at = released;
            return call(tasks::WaitRelease{});
        case released:
            t.at = choose;
            switch (t.choice) {
            case 0: return call(tasks::SetupPage{});
            case 1:
                t.at = viewed;
                return call(tasks::ViewPage{});
            case 2: return call(tasks::InfoPage{});
            case 3:
                t.at = exited;
                return call(tasks::ExitPage{play.game_type()});
            default:
                // Back to the tower: the title line shows its name again.
                screen_.fill(1, 1, 318, 10, 0);
                play.draw_title();
                play.state().panel.turn = -1;
                return end(0);
            }
        case viewed: {
            // $9F48: the view offsets, the panel in its (new) style and colours.
            play.set_settings(settings_);
            play.redraw_panel();
            const auto target = play.target_palette();
            for (std::size_t i = 33; i < 48; ++i) current_[i] = target[i];
            screen_.palette = current_;
            t.at = choose;
            continue;
        }
        case exited:
            if (result_ != 0) return end(result_);
            t.at = choose;
            continue;
        }
}

FrontEnd::Go FrontEnd::run(tasks::SetupPage& t) {
    using enum tasks::SetupPage::At;
    for (;;) switch (t.at) {
        case start:
            screen_.open_window(0, 0, 0xAF, 0xA8, 0);
            screen_.text_at(setup_text);
            t.at = choose;
            continue;
        case choose:
            screen_.radio(radio_backwards, settings_.backwards_allowed ? 0 : 1);
            screen_.radio(radio_link, settings_.link_cameras ? 0 : 1);
            screen_.radio(radio_info, 2 - settings_.info);
            screen_.radio(radio_timer, 2 - settings_.timer);
            screen_.radio(radio_screen_x, 1); // the picture's place on the monitor: the host frames it
            screen_.radio(radio_screen_y, 0);
            t.at = chosen;
            return call(tasks::WaitRelease{}, select(setup_areas));
        case chosen: {
            if (result_ == 0) { // outside the window
                screen_.close_window();
                return Go::end;
            }
            const int i = result_ - 1;
            if (i < 2) settings_.backwards_allowed = i == 0;
            else if (i < 4) settings_.link_cameras = i == 2;
            else if (i < 7) settings_.info = 6 - i;
            else if (i < 10) settings_.timer = 9 - i;
            t.at = choose;
            continue;
        }
        }
}

FrontEnd::Go FrontEnd::run(tasks::ViewPage& t) {
    using enum tasks::ViewPage::At;
    for (;;) switch (t.at) {
        case start:
            screen_.open_window(0x40, 0, 0x11F, 0x92, 0);
            screen_.text_at(view_text);
            t.at = choose;
            continue;
        case choose:
            if (settings_.sights && !(settings_.view_side == 1 && settings_.view_height == 0 && settings_.view_distance == 0))
                settings_.sights = false; // sights only from the spider's own eye
            screen_.radio(radio_sights, settings_.sights ? 0 : 1);
            screen_.radio(radio_view_side, settings_.view_side);
            screen_.radio(radio_view_height, settings_.view_height);
            screen_.radio(radio_view_distance, settings_.view_distance);
            screen_.radio(radio_panel, settings_.panel_style);
            t.at = chosen;
            return call(tasks::WaitRelease{}, select(view_areas));
        case chosen: {
            if (result_ == 0) {
                screen_.close_window();
                return Go::end;
            }
            const int i = result_ - 1;
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
            t.at = choose;
            continue;
        }
        }
}

FrontEnd::Go FrontEnd::run(tasks::InfoPage& t) {
    using enum tasks::InfoPage::At;
    const auto& tower = active_play()->tower();
    switch (t.at) {
    case start:
        screen_.open_window(0x90, 0, 0x137, 0x48, 0);
        screen_.text_at(info_text);
        if (tower.flags[4]) screen_.text_at(info_not);
        screen_.text_at(info_allowed);
        if (tower.flags[8]) screen_.text_at(info_dont);
        screen_.text_at(info_floor);
        screen_.message(tower.settings[0] + 0x6C);
        screen_.cursor.paper = 0;
        screen_.cursor.ink = 15;
        t.at = chosen;
        return call(select(info_areas));
    case chosen:
        if (result_ != 0) {
            screen_.open_window(0x20, 0x3C, 0x11F, 0x86, 0);
            screen_.text_at(task_text);
            screen_.cursor.y = 0x54;
            objective(tower);
            t.at = task_closed;
            return call(tasks::WaitRelease{}, select(task_areas));
        }
        break;
    case task_closed:
        screen_.close_window();
        break;
    }
    screen_.close_window();
    return Go::end;
}

FrontEnd::Go FrontEnd::run(tasks::ExitPage& t) {
    using enum tasks::ExitPage::At;
    switch (t.at) {
    case start:
        screen_.open_window(0xD8, 0, 0x13F, 0x30, 0);
        screen_.text_at(exit_text);
        t.at = chosen;
        return call(select(exit_areas));
    case chosen:
        t.choice = result_;
        if (t.choice == 1 || t.choice == 2) {
            if (t.game_type != 3) {
                t.at = confirmed;
                return call(tasks::Confirm{t.choice == 1 ? abandon_title : restart_tower_title});
            }
            t.leave = t.choice == 1 ? 5 : 6;
        }
        break;
    case confirmed:
        if (result_) t.leave = t.choice == 1 ? 5 : 6;
        break;
    }
    screen_.close_window();
    return end(t.leave);
}

void FrontEnd::start_interlude(int index) {
    interlude_index_ = index % 18;
    const auto& tower = assets_.groups.at(14).towers.at(std::size_t(interlude_index_));
    interlude_ = std::make_unique<Play>(assets_, screen_, tower, settings_, 5);
    last_tower_ = tower;
    sound(SoundEvent::Kind::stop);
    interlude_->start_demo(interlude_index_);
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

void FrontEnd::toggle_ok(bool& lit) {
    lit = !lit;
    for (int y = 184; y < 196; ++y)
        for (int x = 136; x < 184; ++x) screen_.canvas().set(x, y, std::uint8_t(screen_.canvas().at(x, y) ^ 15));
}

FrontEnd::Go FrontEnd::run(tasks::InterludeOk& t) {
    // $948C: the OK button lights while the pointer is on it; the
    // demonstration runs on until it is clicked.
    using enum tasks::InterludeOk::At;
    for (;;) switch (t.at) {
        case start: {
            const bool over = in_.pointer && in_.x >= 0x88 && in_.x < 0xB8 && in_.y >= 0xB8 && in_.y < 0xC3;
            if (over != t.lit) toggle_ok(t.lit);
            if (in_.left && t.lit) {
                t.at = clicked;
                continue;
            }
            t.at = framed;
            return call(tasks::GameFrameWait{});
        }
        case framed:
            interlude_->demo_frame();
            sound_frame(*interlude_);
            t.at = start;
            continue;
        case clicked:
            if (t.lit) toggle_ok(t.lit);
            sound(SoundEvent::Kind::pause); // $15A10
            t.at = faded;
            return call(tasks::WaitRelease{}, fade_out());
        case faded:
            screen_.copper = CopperMode::plain;
            interlude_.reset();
            return Go::end;
        }
}

FrontEnd::Go FrontEnd::run(tasks::WonScreen& t) {
    // $92EC: the tower's bonus, a bonus for few tries and 10 for each spider
    // still standing, counted into the score in steps of up to 20.
    using enum tasks::WonScreen::At;
    const auto demo_frame = [this] {
        interlude_->demo_frame();
        sound_frame(*interlude_);
    };
    for (;;) switch (t.at) {
        case start:
            shown_bonus_[0] = 100;
            shown_bonus_[1] = std::max(0, 100 - 4 * t.tries);
            shown_bonus_[2] = 10 * survivors_;
            progress(0, t.letter) = 0xFF;
            t.at = faded;
            return call(fade_out());
        case faded: {
            ++interludes_;
            start_interlude(interludes_ % 6);
            screen_.bevel_box(0, 0x82, 0x13F, 0xB4, 6);
            screen_.frame(0x87, 0xB7, 0xB8, 0xC4, 13);
            screen_.text_at(ok_text);
            screen_.text_at(score_text);
            draw_score_numbers();
            demo_frame();
            auto target = interlude_->target_palette();
            for (std::size_t i = 0; i < 15; ++i) target[33 + i] = assets_.program.u16(panel_band_menu + std::uint32_t(i) * 2);
            t.i = 0;
            t.at = settle;
            return call(fade(target));
        }
        case settle: // four frames before the counting
            if (t.i == 4) {
                t.k = 0;
                t.at = bonus;
                continue;
            }
            t.at = settled;
            return call(tasks::GameFrameWait{});
        case settled:
            demo_frame();
            ++t.i;
            t.at = settle;
            continue;
        case bonus:
            if (t.k == 3) {
                t.at = ok;
                continue;
            }
            t.chunk = 20;
            t.at = count;
            continue;
        case count:
            if (t.chunk == 0) {
                t.at = next_bonus;
                continue;
            }
            if (t.chunk > shown_bonus_[t.k]) {
                t.chunk >>= 1;
                continue;
            }
            score_ += t.chunk;
            shown_bonus_[t.k] -= t.chunk;
            draw_score_numbers();
            t.at = counted;
            return call(tasks::GameFrameWait{});
        case counted:
            demo_frame();
            t.at = shown;
            return Go::wait;
        case shown:
            t.at = count;
            continue;
        case next_bonus: // three frames between the bonuses
            if (t.k < 2) {
                t.i = 0;
                t.at = pause;
            } else {
                ++t.k;
                t.at = bonus;
            }
            continue;
        case pause:
            if (t.i == 3) {
                ++t.k;
                t.at = bonus;
                continue;
            }
            t.at = paused;
            return call(tasks::GameFrameWait{});
        case paused:
            demo_frame();
            ++t.i;
            t.at = pause;
            continue;
        case ok:
            t.at = finished;
            return call(tasks::InterludeOk{});
        case finished:
            selected_ = -1;
            return Go::end;
        }
}

FrontEnd::Go FrontEnd::run(tasks::EndScreen& t) {
    // $97AA: a solved tower is marked, an unsolved one counts two more
    // tries; a demonstration runs above the reason (won: 0-5 in turn, lost: 6-11).
    using enum tasks::EndScreen::At;
    switch (t.at) {
    case start:
        if (selected_ >= 0) {
            auto& b = progress(t.game_type, selected_);
            if (t.reason == 1) {
                b = 0xFF;
                selected_ = -1;
            } else if (b < 0x63) {
                b = std::uint8_t(b + 2);
            }
        }
        t.at = faded;
        return call(fade_out());
    case faded: {
        ++interludes_;
        start_interlude(interludes_ % 6 + (t.reason == 1 ? 0 : 6));
        const int pen = t.reason == 1 ? 6 : 3;
        screen_.bevel_box(0x40, 0x82, 0xFF, 0xA8, pen);
        screen_.cursor.paper = t.reason == 1 ? 6 : 3;
        screen_.cursor.ink = t.reason == 1 ? 8 : 5;
        screen_.message(message_end + t.reason);
        screen_.frame(0x87, 0xB7, 0xB8, 0xC4, 13);
        screen_.text_at(ok_text);
        interlude_->demo_frame();
        sound_frame(*interlude_);
        auto target = interlude_->target_palette();
        for (std::size_t i = 0; i < 15; ++i) target[33 + i] = assets_.program.u16(panel_band_menu + std::uint32_t(i) * 2);
        t.at = finished;
        return call(fade(target), tasks::InterludeOk{});
    }
    case finished:
        break;
    }
    return Go::end;
}

FrontEnd::Go FrontEnd::run(tasks::DemoSequence& t) {
    // $15780: demonstrations 12-17 in turn; a click in the view's left half
    // goes back one, in its right half on one, on the logo below (or Space) ends.
    using enum tasks::DemoSequence::At;
    for (;;) switch (t.at) {
        case start:
            t.at = show;
            return call(fade_out());
        case show: {
            start_interlude(t.index);
            screen_.picture(assets_.title_memory, logo_at, 0, 0x90, 20, 35);
            interlude_->demo_frame();
            sound_frame(*interlude_);
            std::array<data::Colour, 48> target = interlude_->target_palette();
            for (std::size_t i = 0; i < 15; ++i) target[33 + i] = assets_.program.u16(var(0x249A) + std::uint32_t(i) * 2);
            t.next = t.index;
            t.at = frame;
            return call(fade(target));
        }
        case frame:
            t.at = framed;
            return call(tasks::GameFrameWait{});
        case framed:
            interlude_->demo_frame();
            sound_frame(*interlude_);
            t.at = frame;
            if (in_.key == ' ') {
                sound(SoundEvent::Kind::pause);
                t.at = ended;
                return call(fade_out());
            }
            if (in_.left) {
                if (in_.y >= 0x80) {
                    sound(SoundEvent::Kind::pause);
                    t.at = ended;
                    return call(tasks::WaitRelease{}, fade_out());
                }
                t.next = in_.x < 0xA0 ? t.index - 1 : t.index + 1;
                t.at = next;
                return call(fade_out());
            }
            if (interlude_->demo_script_ended()) {
                t.next = t.index + 1;
                t.at = next;
                return call(fade_out());
            }
            continue;
        case next:
            t.index = t.next < 12 ? 17 : t.next > 17 ? 12 : t.next;
            t.at = show;
            continue;
        case ended:
            interlude_.reset();
            screen_.copper = CopperMode::plain;
            return Go::end;
        }
}

// --- quick saves -----------------------------------------------------------

namespace {

// A task of the kind with this index, default made, at the end of `tasks`.
template <std::size_t I = 0> void emplace_task(std::deque<Task>& tasks, std::size_t index) {
    if constexpr (I < std::variant_size_v<Task>) {
        if (index == I) tasks.emplace_back(std::in_place_index<I>);
        else emplace_task<I + 1>(tasks, index);
    } else {
        state::Reader::fail();
    }
}

} // namespace

template <class A> void FrontEnd::persist_task(A& ar, Task& task) {
    std::visit(
        [&](auto& t) {
            ar(t);
            if constexpr (std::is_same_v<std::decay_t<decltype(t)>, tasks::PlayTower>) {
                // The tower being played, made again from the task's tower.
                bool playing = t.play.has_value();
                ar(playing);
                if (!playing) return;
                if constexpr (A::reading) t.play.emplace(assets_, screen_, t.tower, settings_, t.game_type);
                t.play->persist(ar);
            }
        },
        task);
}

template <class A> void FrontEnd::persist(A& ar) {
    const auto check = [](bool ok) {
        if (!ok) A::fail();
    };
    // The player, the progress and the tables.
    ar(name_, score_, group_, all_done_, standard_progress_, custom_progress_, tutorial_progress_, selected_,
       completed_count_, interludes_, survivors_, shown_bonus_, settings_, screen_x_, screen_y_, scores_, file_name_,
       custom_towers_, last_tower_);
    // The designer.
    ar(design_, time_text_, designer_group_, undo_, undo_ready_, floor_, slot_, palette_, hover_x_, hover_y_,
       depth_cue_, enemy_movement_, auto_repeat_, confirm_replace_, bright_, painting_, colour_, whole_tower_,
       mouse_scaling_, mouse_style_, previous_colours_, loaded_tower_);
    if constexpr (A::reading)
        check(group_ >= 0 && group_ < 13 && !scores_.empty() && custom_towers_.size() == 9 &&
              (designer_group_.empty() || designer_group_.size() == 9) && settings_.panel_style >= 0 &&
              settings_.panel_style < 3 && settings_.view_distance >= 0 && settings_.view_distance < 4);
    // The machine: the screen, the colours faded to, the timer, the inputs
    // as the last blank left them, the random number.
    ar(screen_, current_, fast_fades_, ticks_, random_, piece_frames_, clicked_, clicked_x_, clicked_y_, in_, previous_);
    // A pause: the screen as it began, and the wide picture once drawn.
    ar(play_paused_, paused_wide_);
    if (play_paused_) ar(paused_screen_.pixels);
    if (paused_wide_) ar(wide_.width, wide_.height, wide_.pixels);
    if constexpr (A::reading)
        check(paused_screen_.pixels.size() == std::size_t(Screen::width * Screen::height) &&
              wide_.pixels.size() == std::size_t(wide_.width) * std::size_t(wide_.height));
    // The demonstration running, if any.
    bool interlude = interlude_ != nullptr;
    ar(interlude);
    if (interlude) {
        ar(interlude_index_);
        if constexpr (A::reading) {
            check(interlude_index_ >= 0 && interlude_index_ < 18);
            interlude_ = std::make_unique<Play>(assets_, screen_, assets_.groups.at(14).towers.at(std::size_t(interlude_index_)),
                                                settings_, 5);
        }
        interlude_->persist(ar);
    }
    // The tasks under way, from the first.
    std::size_t count = tasks_.size();
    ar(count);
    if constexpr (A::reading) {
        check(count > 0 && count < 64);
        tasks_.clear();
    }
    for (std::size_t i = 0; i < count; ++i) {
        std::size_t index = A::reading ? 0 : tasks_[i].index();
        ar(index);
        if constexpr (A::reading) emplace_task(tasks_, index);
        persist_task(ar, tasks_[i]);
    }
}

void FrontEnd::write_state(state::Writer& ar) {
    persist(ar);
}

void FrontEnd::read_state(state::Reader& ar) {
    persist(ar);
}

} // namespace babel::front
