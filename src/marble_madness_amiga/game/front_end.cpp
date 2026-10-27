#include "marble_madness_amiga/game/front_end.hpp"
#include <algorithm>

namespace marble_madness::game {
namespace {

// Program data (research layout).
constexpr std::uint32_t options_texts = 0x10D152;    // "Press Left Mouse Button" ... " GO!"
constexpr std::uint32_t red_player_texts = 0x10D186; // "Red Player:", "Input Device:"
constexpr std::uint32_t device_names = 0x10D206;     // 4 pointers to pointers: Joy Stick ... Mouse
constexpr std::uint32_t port_names = 0x10D236;       // 2 pointers to pointers: Rear Port, Front Port
constexpr std::uint32_t device_rows = 0x10D260;      // per player, longs
constexpr std::uint32_t time_to_finish = 0x102756;   // the introduction's box, courses 0 and 1
constexpr std::uint32_t extra_time_for = 0x1027CE;   // the later courses'
constexpr std::uint32_t course_lines = 0x1022F0;     // per course: "PRACTICE RACE:" ...
constexpr std::uint32_t silly_texts = 0x102946;      // "EVERYTHING YOU KNOW IS WRONG!"
constexpr std::uint32_t course_times = 0x102350;     // per difficulty table: a byte per course
constexpr std::uint32_t difficulty_tables = 0x102370; // difficulty -> table
constexpr std::uint32_t bonus_for = 0x102A54, time_left = 0x102A68;  // pointers to texts ($10BC28)
constexpr std::uint32_t bonus_box_x = 0x10BE64, bonus_box_y = 0x10BE68; // words per player
constexpr std::uint32_t blank_line = 0x102AA4, out_of_time = 0x102A7C, game_over = 0x102A90;
constexpr std::uint32_t game_over_columns = 0x10C51B; // bytes by players + player
constexpr std::uint32_t finish_sounds = 0x102124;    // per course, longs ($10BC28)
constexpr std::uint32_t end_texts = 0x1025D6, end_tally = 0x102694, end_final = 0x1026A6, end_score = 0x1026BA;

constexpr int out_of_time_sound = 0x19;
constexpr int end_sound = 0x20;      // $10C728: the music stops
constexpr int stop_rolling = 0xF, stop_rolling_too = 0x10; // $12250A (the second not on Intermediate)

// Waits in vertical blanks; a world update takes two.
constexpr int blanks_per_update = 2;
// The disk's loading time between courses (the program shows the last
// course while it loads, then a moment of black while the new display is
// set up). Measured in FS-UAE as several seconds; the port keeps a short
// pause of each.
constexpr int loading_blanks = 100, black_blanks = 20;

} // namespace

std::string read_string(const amiga::LoadedImage& program, std::uint32_t address) {
    std::string out;
    for (std::uint32_t a = address; program.contains(a) && program.u8(a) != 0; ++a) {
        out.push_back(char(program.u8(a)));
    }
    return out;
}

// $10A46A: entries of a column byte, a row byte, a text pointer, a flag for
// a next entry, a pad byte and the next entry's address.
std::vector<TextLine> read_text_list(const amiga::LoadedImage& program, std::uint32_t list) {
    std::vector<TextLine> out;
    for (int guard = 0; list != 0 && guard < 64; ++guard) {
        out.push_back({program.s8(list), program.s8(list + 1), read_string(program, program.u32(list + 2))});
        list = program.u8(list + 6) != 0 ? program.u32(list + 8) : 0;
    }
    return out;
}

std::string number_text(std::int32_t value, int digits, int style) {
    const char pad = style == 1 ? '0' : ' ';
    std::string out(std::size_t(std::max(digits, 0)), pad);
    bool negative = value < 0;
    std::uint32_t v = negative ? std::uint32_t(-value) : std::uint32_t(value);
    bool first = v == 0;
    for (int i = digits - 1; i >= 0; --i) {
        if (v != 0 || first) {
            out[std::size_t(i)] = char('0' + v % 10);
            v /= 10;
            first = false;
        } else if (negative) {
            out[std::size_t(i)] = '-';
            negative = false;
        }
    }
    if (style == 2) {
        // Left-aligned, then cut at the first space ($10A550).
        const auto start = out.find_first_not_of(pad);
        out = start == std::string::npos ? std::string() : out.substr(start);
        if (const auto space = out.find(' '); space != std::string::npos) {
            out.resize(space);
        }
    }
    return out;
}

GameFlow::GameFlow(const data::GameFiles& files, const data::Assets& assets)
    : files_(&files), assets_(&assets), random_(Race::program_random(files)) {
    // Until a race has run, the screens take Practice's colours.
    load_course(data::Course::practice);
    start_options();
}

void GameFlow::play(int sound) {
    sounds_->play(sound, false);
}

// The race and its view for a course (the course start $103326 with the
// player records as the last race left them).
void GameFlow::load_course(data::Course course) {
    course_ = course;
    const int frame = race_ ? race_->frame() : 1;
    race_ = std::make_unique<Race>(*files_, course, 1, settings_.difficulty, &random_);
    race_->set_frame(frame);
    auto& m = race_->marbles[0];
    m.score = score_;
    m.respawns = respawns_;
    // $114AA2: the clock restarts on the first two courses; the
    // introduction counts the course's time in.
    m.time = course < data::Course::intermediate ? 0 : time_;
    race_->set_crowd(crowd_);
    view_ = std::make_unique<View>(*files_, *assets_, *race_);
    view_->update(*race_);
    sounds_ = std::make_unique<sound::SoundBoard>(*files_, course);
}

void GameFlow::playfield_text(int column, int row, const std::string& text, std::vector<View::Text>& out) const {
    // $10A2FA: from column 3 on, eight pixels a column and a row; at most
    // $24 columns.
    const int x = std::max(column - 3, 0);
    const int length = std::min(int(text.size()), 0x24 - x);
    if (length > 0) {
        // Moved to the row's line + 7, topaz 9's baseline 6 below the top.
        out.push_back({x * 8, std::clamp(row, 0, 0x19) * 8 + 1, text.substr(0, std::size_t(length)), 7, 4});
    }
}

void GameFlow::playfield_texts(std::uint32_t list, std::vector<View::Text>& out) const {
    for (const auto& line : read_text_list(files_->program(), list)) {
        playfield_text(line.column, line.row, line.text, out);
    }
}

// --- The options screen ($10CCDC) ---------------------------------------

void GameFlow::start_options() {
    stage_ = Stage::options;
    step_ = 0;
    // State 1 turns the display off while it draws the page.
    wait(10);
    pointer_x_ = 0xA0;
    pointer_y_ = 0xBE;
    fire_held_ = true;
    title_blanks_ = 0;
    course_ = data::Course::practice;
}

void GameFlow::options_tick(const Controls& in) {
    if (step_ == 0) {
        overlay_ = {};
        overlay_.base = View::Overlay::Base::black;
        if ((wait_ -= blanks_per_update) > 0) {
            return;
        }
        step_ = 1;
    }
    // Moves of the pointer (the original's follows the mouse; here the
    // joystick moves it), clicks after each change wait $14 blanks.
    if (wait_ > 0) {
        wait_ -= blanks_per_update;
    } else {
        constexpr int speed = 4;
        pointer_x_ = std::clamp(pointer_x_ + (in.right ? speed : 0) - (in.left ? speed : 0), 0, View::width - 8);
        pointer_y_ = std::clamp(pointer_y_ + (in.down ? speed : 0) - (in.up ? speed : 0), 0, View::height - 8);
        if (in.fire && !fire_held_) {
            click_options();
        }
        fire_held_ = in.fire;
    }
    if (stage_ != Stage::options) {
        return;
    }
    // The page: cleared to pen 4, the texts, the values, the pointer.
    const auto& program = files_->program();
    overlay_ = {};
    overlay_.base = View::Overlay::Base::cleared;
    overlay_.title = true;
    overlay_.title_blanks = title_blanks_;
    title_blanks_ += blanks_per_update;
    playfield_texts(options_texts, overlay_.texts);
    playfield_texts(red_player_texts, overlay_.texts);
    playfield_text(0x20, 8, number_text(settings_.players, 1, 0), overlay_.texts);
    playfield_text(0x20, 10, number_text(settings_.difficulty, 1, 0), overlay_.texts);
    playfield_text(0x18, 0xD, read_string(program, program.u32(program.u32(port_names + std::uint32_t(settings_.port) * 4))),
                   overlay_.texts);
    playfield_text(0x17, int(program.u32(device_rows)),
                   read_string(program, program.u32(program.u32(device_names + std::uint32_t(settings_.device[0]) * 4))),
                   overlay_.texts);
    overlay_.balls.push_back({pointer_x_, 0x100 - (pointer_y_ + 8), 0});
}

// $10CCDC state 2: a click by the pointer's line.
void GameFlow::click_options() {
    const int y = pointer_y_;
    if (y < 0x40) {
        return;
    }
    if (y >= 0xB0) {
        start_game();
        return;
    }
    if (y < 0x50) {
        // Number of Players: two players racing at once are not rebuilt.
    } else if (y < 0x68) {
        settings_.difficulty = (settings_.difficulty + 1) & 7;
    } else if (y < 0x78) {
        settings_.port ^= 1;
    } else if (y < 0x90) {
        settings_.device[0] = (settings_.device[0] + 1) & 3;
    } else {
        return; // the blue player's lines (two players)
    }
    wait(0x14);
}

// $10CCDC state 3 and $10B3DC: the game starts with the course the options
// set ($102378, Practice).
void GameFlow::start_game() {
    play(3);
    play(4);
    play(2); // one player
    score_ = 0;
    time_ = 0;
    respawns_ = 0;
    stage_ = Stage::loading;
    step_ = 0;
    wait(black_blanks);
    course_ = data::Course::practice;
}

// --- Between courses -----------------------------------------------------

void GameFlow::tick(const Controls& in) {
    switch (stage_) {
    case Stage::options:
        options_tick(in);
        break;
    case Stage::loading:
        if (step_ == 0) {
            // The last course while the next one loads, then black.
            overlay_.base = View::Overlay::Base::course_without_sprites;
            overlay_.boxes.clear();
            overlay_.texts.clear();
            overlay_.balls.clear();
            if ((wait_ -= blanks_per_update) > 0) {
                return;
            }
            step_ = 1;
            wait(black_blanks);
        }
        overlay_ = {};
        overlay_.base = View::Overlay::Base::black;
        if ((wait_ -= blanks_per_update) > 0) {
            return;
        }
        load_course(course_);
        start_course();
        break;
    case Stage::intro:
        intro_tick();
        break;
    case Stage::race:
        race_tick(in);
        break;
    case Stage::finish:
        finish_tick();
        break;
    case Stage::out_of_time:
        out_of_time_tick();
        break;
    case Stage::ending:
        ending_tick();
        break;
    }
}

// $106A64: the course's introduction over the course: "TIME TO FINISH"
// (Practice, Beginner) or "EXTRA TIME FOR", the course's line and its
// seconds, counted into the clock five at a time.
void GameFlow::start_course() {
    stage_ = Stage::intro;
    step_ = 0;
    const auto& program = files_->program();
    const std::uint32_t table = program.u32(course_times + std::uint32_t(program.u8(difficulty_tables + std::uint32_t(settings_.difficulty & 7))) * 4);
    count_ = program.u8(table + std::uint32_t(course_));
    total_ = count_;
    wait(0x3C);
}

void GameFlow::intro_tick() {
    const auto& program = files_->program();
    overlay_ = {};
    playfield_texts(course_ < data::Course::intermediate ? time_to_finish : extra_time_for, overlay_.texts);
    playfield_texts(program.u32(course_lines + std::uint32_t(course_) * 4), overlay_.texts);
    if (course_ == data::Course::silly) {
        playfield_texts(silly_texts, overlay_.texts);
    }
    playfield_text(0x20, 10, number_text(count_, 2, 0), overlay_.texts);
    if ((wait_ -= blanks_per_update) > 0) {
        return;
    }
    if (count_ > 0) {
        count_ -= 5;
        race_->marbles[0].time = std::int16_t(race_->marbles[0].time + 5);
        wait(count_ > 0 ? 0x14 : std::max(0x14, 0x14 + 0x3C - total_));
        return;
    }
    // The race: the course's music ($1030B4), the boxes go.
    overlay_ = {};
    for (const auto& e : race_->take_sounds()) {
        sounds_->play(e.id, e.right);
    }
    stage_ = Stage::race;
}

void GameFlow::race_tick(const Controls& in) {
    race_->cheats = cheats;
    race_->update({in.push, Push{}});
    view_->update(*race_);
    for (const auto& e : race_->take_sounds()) {
        sounds_->play_after_blank(e.id, e.right);
    }
    random_ = race_->random;
    const auto& m = race_->marbles[0];
    score_ = m.score;
    time_ = m.time;
    respawns_ = m.respawns;
    if (race_->game_mode == 3) {
        // $10B2AA: the rolling sounds end, the course counts on, the bonus.
        play(stop_rolling);
        if (course_ != data::Course::intermediate) {
            play(stop_rolling_too);
        }
        stage_ = Stage::finish;
        step_ = 0;
    } else if (race_->game_mode == 4) {
        // $10BE7C: the tune, the rolling sounds end, the box.
        play(out_of_time_sound);
        play(stop_rolling);
        if (course_ != data::Course::intermediate) {
            play(stop_rolling_too);
        }
        if (m.air == 2) {
            play(0xC);
        }
        if (m.region_code == 0x10) {
            play(10);
        }
        stage_ = Stage::out_of_time;
        step_ = 0;
    }
}

// $10BC28: "BONUS FOR TIME LEFT:" with a hundred points a second (up to
// 99) in a box ($96 x $14) for $180 blanks, then the course's finish sound
// and the points.
void GameFlow::finish_tick() {
    const auto& program = files_->program();
    if (step_ == 0) {
        const int seconds = std::min<int>(race_->marbles[0].time, 99);
        total_ = seconds * 100;
        overlay_ = {};
        const int x = program.s16(bonus_box_x), y = program.s16(bonus_box_y);
        overlay_.boxes.push_back({x, y, 0x96, 0x14, 4});
        // $10A0D4: moved to y + 6, the baseline: the cells start at y.
        overlay_.texts.push_back({x + 2, y + 2, read_string(program, program.u32(bonus_for)), 7, 4});
        overlay_.texts.push_back({x + 2, y + 0xB, read_string(program, program.u32(time_left)), 7, 4});
        overlay_.texts.push_back({x + 0x6C, y + 0xB, number_text(total_, 4, 0), 7, 4});
        step_ = 1;
        wait(0x180);
    }
    if (step_ == 1) {
        if ((wait_ -= blanks_per_update) > 0) {
            return;
        }
        play(int(program.u32(finish_sounds + std::uint32_t(course_) * 4)));
        race_->marbles[0].score += total_;
        score_ = race_->marbles[0].score;
        overlay_ = {};
        overlay_.base = View::Overlay::Base::course_without_sprites;
        if (course_ == data::Course::ultimate) {
            // Mode 6: the end.
            stage_ = Stage::ending;
            step_ = 0;
            return;
        }
        course_ = data::Course(int(course_) + 1);
        stage_ = Stage::loading;
        step_ = 0;
        wait(loading_blanks);
    }
}

// $10BE7C: "OUT OF TIME", "GAME OVER" in a box ($90 x $41) for $B4 blanks,
// then the options screen.
void GameFlow::out_of_time_tick() {
    const auto& program = files_->program();
    if (step_ == 0) {
        overlay_ = {};
        const int column = program.s8(game_over_columns + 1);
        const int x = std::max(column - 3, 0) * 8, y = 0x80;
        overlay_.boxes.push_back({x, y, 0x90, 0x41, 4});
        const std::string blank = read_string(program, program.u32(blank_line));
        const int rows[] = {0x14, 0x1D, 0x26, 0x2F, 0x38};
        const std::string texts[] = {blank, read_string(program, program.u32(out_of_time)), blank,
                                     read_string(program, program.u32(game_over)), blank};
        for (int i = 0; i < 5; ++i) {
            overlay_.texts.push_back({x, y + rows[i], texts[i], 9, 4});
        }
        step_ = 1;
        wait(0xB4);
    }
    if ((wait_ -= blanks_per_update) > 0) {
        return;
    }
    start_options();
}

// $10C728: the end after Ultimate: the playfield cleared, sixteen marbles
// bouncing about ($11E9A8, $11EB10), the tally ($10C530) counted into the
// score $FA points an update ($10C6BE), then the options screen.
void GameFlow::ending_tick() {
    auto& m = race_->marbles[0];
    switch (step_) {
    case 0: {
        play(end_sound);
        overlay_ = {};
        overlay_.base = View::Overlay::Base::cleared;
        overlay_.bar_score = m.score;
        overlay_.bar_time = m.time;
        balls_.clear();
        for (int i = 0; i < 0x10; ++i) {
            Ball b;
            b.x = std::int16_t((int(random_.next(0x100) & 0xFF) + 0xC) * 0x10);
            b.y = std::int16_t((int(random_.next(0x80)) + 0x30) * 0x10);
            auto speed = [&] {
                const int s = int(random_.next(0x60)) - 0x30;
                return std::int16_t((s < 0 ? s - 0x10 : s + 0x10) * 2);
            };
            b.vx = speed();
            b.vy = speed();
            b.ramp = std::int16_t(random_.next(0x10));
            balls_.push_back(b);
        }
        count_ = 2;
        step_ = 1;
        break;
    }
    case 1:
        if (--count_ == 0) {
            step_ = 2;
        }
        break;
    case 2:
        // $10C530: the tally.
        total_ = std::min<int>(m.time, 99) * 1000 + 20000 - std::min<int>(m.respawns, 0x14) * 1000;
        step_ = 3;
        break;
    case 3:
        // $10C6BE.
        m.score += 0xFA;
        total_ -= 0xFA;
        if (total_ < 1) {
            count_ = 0xB4;
            step_ = 4;
        }
        break;
    case 4:
        if (--count_ == 0) {
            score_ = m.score;
            start_options();
            return;
        }
        break;
    default:
        break;
    }
    // What the playfield shows by now (the program draws each text once and
    // redraws the two numbers that count).
    overlay_.texts.clear();
    if (step_ >= 3) {
        playfield_texts(end_texts, overlay_.texts);
        playfield_texts(end_tally, overlay_.texts);
        const int seconds = std::min<int>(m.time, 99), died = std::min<int>(m.respawns, 0x14);
        playfield_text(4, 0xC, number_text(seconds, 2, 1), overlay_.texts);
        playfield_text(0x20, 0xC, number_text(seconds * 1000, 5, 0), overlay_.texts);
        playfield_text(9, 0xE, number_text(died, 2, 1), overlay_.texts);
        playfield_text(0x20, 0xE, number_text(died * 1000, 5, 0), overlay_.texts);
        playfield_text(0x1F, 0x10, number_text(total_, 6, 0), overlay_.texts);
        playfield_texts(end_final, overlay_.texts);
        playfield_texts(end_score, overlay_.texts);
        playfield_text(0x17, 0x15, number_text(m.score, 7, 0), overlay_.texts);
    }
    // $11EB10: the marbles move; one at a wall turns back (and stays put
    // that update).
    overlay_.balls.clear();
    for (auto& b : balls_) {
        const std::int16_t nx = std::int16_t(b.vx + b.x), ny = std::int16_t(b.vy + b.y);
        const int sx = nx >> 4, sy = ny >> 4;
        if ((sx < 8 && b.vx < 0) || (sx > 0x118 && b.vx > 0)) {
            b.vx = std::int16_t(-b.vx);
        }
        if ((sy < 0x30 && b.vy < 0) || (sy > 0xF8 && b.vy > 0)) {
            b.vy = std::int16_t(-b.vy);
        }
        if (sx > 7 && sx < 0x119 && sy > 0x2F && sy < 0xF9) {
            b.x = nx;
            b.y = ny;
        }
        overlay_.balls.push_back({b.x >> 4, b.y >> 4, b.ramp});
    }
}

platform::Image GameFlow::picture() const {
    return view_->draw(*race_, overlay_);
}

} // namespace marble_madness::game
