#include "rock_n_roll_amiga/game/front_end.hpp"
#include "rock_n_roll_amiga/data/preview.hpp"
#include "rock_n_roll_amiga/game/records.hpp"
#include <algorithm>

namespace rocknroll::game {
namespace {

constexpr int title_world = 8;
constexpr int bank_rows = 336;            // the title's file fills rows 0-335
constexpr int scroll_rows = 0x8FC0 / 200; // $69854 scrolls to $8FC0: row 184
constexpr int shorten_from = 0x6978 / 200; // past row 135 the display ends earlier ($732A6)
constexpr int box_row = 0x60E0 / 200;     // $693D8 copies the name box to $1F0E0: row 124
constexpr int records_x = 32, records_y = 181; // $19000 + $8728 + $644: texts' origin
constexpr int name_x = 32, name_y = 40;   // $1A900 + $644: the name box's texts

constexpr std::uint32_t hall_header = 0x69060;    // "HALL OF FAME" (4, 4)
constexpr std::uint32_t records_header = 0x69042; // " TIMERECORDS OF LEVEL nn " (3, 4)
constexpr std::uint32_t records_digits = 0x6905A;
constexpr std::uint32_t blank_rows = 0x690B8;     // five blank texts for rows 11-15, 28 bytes apart
constexpr std::uint32_t place_text = 0x6907E;     // "CONGRATULATIONS PLACE 10" (4, 4)
constexpr std::uint32_t place_digits = 0x69096;
constexpr std::uint32_t no_entry_text = 0x6909A;  // "    SORRY, NO ENTRY       " (4, 4)
constexpr std::uint32_t letter_text = 0x6840F;    // the wheel's letter (23, 14)
constexpr std::uint32_t name_text = 0x68414;      // the name (7, 14), 13 characters
// Names that do more ($6941C compares the name's first eleven characters
// with these texts').
constexpr std::uint32_t magic_map_name = 0x68424, rainbow_name = 0x68434, country_name = 0x68444;

constexpr std::uint32_t jukebox_texts = 0x68206; // "ROCK AND ROLL JUKEBOX" and the titles, (3, 4) on
constexpr int jukebox_text_count = 11;
constexpr std::uint32_t jukebox_number_text = 0x6832F; // "TITLE NR.: 1" (3, 15), the digit at $68344
constexpr std::uint32_t jukebox_number = 0x68344;
constexpr int jukebox_pass_frames = 2 * 655360 * 50 / 7093790; // two delays ($6B66E) a pass of its loop

constexpr char end_letter = '[', delete_letter = '\'', blank = '(';

std::string name_of(const data::Program& program, std::uint32_t text) {
    return read_text(program, text).text.substr(2);
}

} // namespace

FrontEnd::FrontEnd(const data::Assets& assets, const data::ScoreTable& scores)
    : assets_(&assets), scores_(&scores), world_(&assets.world(title_world)) {
    name_ = name_of(assets.program, name_text); // 13 blanks at start-up; kept from game to game
}

void FrontEnd::open(std::optional<int> hall_of_fame_place) {
    // $68F40: the bank cleared, world 8 loaded over it.
    rows_ = bank_rows;
    pixels_.assign(world_->bank.pixels.begin(), world_->bank.pixels.begin() + std::ptrdiff_t(bank_rows * width));
    scroll_ = 0;
    state_ = State::title;
    pending_ = pending_text_ = hall_of_fame_place.has_value();
    pending_place_ = hall_of_fame_place.value_or(0);
    grey_ = false;
}

void FrontEnd::scroll_step(int direction) {
    scroll_ = std::clamp(scroll_ + direction, 0, scroll_rows);
}

void FrontEnd::draw_records() {
    // $6918A
    const auto& program = assets_->program;
    for (const auto& line : record_lines(program, *scores_, table_)) print_at(canvas(), world_->bank, records_x, records_y, line);
    if (table_ == data::ScoreTable::hall_of_fame) {
        print_at(canvas(), world_->bank, records_x, records_y, read_text(program, hall_header));
        if (pending_text_) { // $68E90 bit 0: once
            pending_text_ = false;
            auto text = read_text(program, pending_place_ == 0 ? no_entry_text : place_text);
            if (pending_place_ != 0) {
                const auto at = std::size_t(place_digits - place_text);
                text.text[at] = pending_place_ == 10 ? '1' : '0';
                text.text[at + 1] = char('0' + pending_place_ % 10);
            }
            print_at(canvas(), world_->bank, records_x, records_y, text);
        }
    } else {
        for (std::uint32_t i = 0; i < 5; ++i) print_at(canvas(), world_->bank, records_x, records_y, read_text(program, blank_rows + i * 0x1C));
        auto header = read_text(program, records_header);
        const auto at = std::size_t(records_digits - records_header);
        header.text[at] = char('0' + (table_ + 1) / 10);
        header.text[at + 1] = char('0' + (table_ + 1) % 10);
        print_at(canvas(), world_->bank, records_x, records_y, header);
    }
}

void FrontEnd::step_records(bool left) {
    // $69306: the left button steps to the next table, at once and then
    // faster the longer it is held (a period from 20 frames down to 1).
    redraw_ = false;
    if (!left) {
        hold_count_ = 0;
        hold_period_ = 20;
        return;
    }
    ++hold_count_;
    if (hold_count_ != 1) {
        if (hold_count_ != hold_period_) return;
        hold_count_ = 1;
    }
    table_ = table_ + 1 == data::ScoreTable::hall_of_fame + 1 ? 0 : table_ + 1;
    hold_period_ -= 2;
    if (hold_period_ == 0) hold_period_ = 2;
    redraw_ = true;
}

void FrontEnd::begin_name(bool code) {
    // $693D8: the name box over the title's button texts.
    std::copy(assets_->name_box.pixels.begin(), assets_->name_box.pixels.end(),
              pixels_.begin() + std::ptrdiff_t(box_row * width));
    code_ = code;
    if (code) { // $694B2: colour 0 grey, twelve zeros to edit
        grey_ = true;
        grey_colour_ = 0x555;
        std::fill(name_.begin(), name_.begin() + 12, '0');
    }
    cursor_ = 0;
    waiting_release_ = false;
    pass_frame_ = 0;
    pass_mouse_x_ = 0;
    state_ = code ? State::code_entry : State::name_entry;
    print_name();
}

void FrontEnd::draw_jukebox_number() {
    auto text = read_text(assets_->program, jukebox_number_text);
    text.text[jukebox_number - jukebox_number_text] = jukebox_title_;
    print_at(canvas(), world_->bank, records_x, records_y, text);
}

void FrontEnd::print_name() {
    auto letter = read_text(assets_->program, letter_text);
    letter.text[2] = char(letter_);
    print_at(canvas(), world_->bank, name_x, name_y, letter);
    auto name = read_text(assets_->program, name_text);
    name.text.replace(2, name_.size(), name_);
    print_at(canvas(), world_->bank, name_x, name_y, name);
}

// One accepted letter ($697CC): the end, a deletion, or the next character
// (the thirteenth is taken back at once, so a name holds twelve).
static bool accept(std::string& name, int& cursor, std::uint8_t letter) {
    if (letter == end_letter) return true;
    if (letter == delete_letter) {
        if (cursor > 0) name[std::size_t(--cursor)] = blank;
        return false;
    }
    name[std::size_t(cursor++)] = char(letter);
    if (cursor == 13) name[std::size_t(--cursor)] = blank;
    return false;
}

bool FrontEnd::name_pass(const FrontEndInput& in, bool left) {
    // A typed key is taken at once ($69672); otherwise the mouse turns the
    // wheel: $7040C's velocity from rest is exactly ±$200 for two counts or
    // more ($69696), one letter a pass.
    if (in.key != 0) {
        letter_ = std::uint8_t(in.key);
        return accept(name_, cursor_, letter_);
    }
    if (pass_mouse_x_ >= 2) ++letter_;
    else if (pass_mouse_x_ <= -2) --letter_;
    // The wheel: ' (delete), ( (space), 0-9, A-Z, [ (end), round again.
    switch (letter_) {
    case 0x26: letter_ = 0x5B; break;
    case 0x5C: letter_ = 0x27; break;
    case 0x29: letter_ = 0x30; break;
    case 0x2F: letter_ = 0x28; break;
    case 0x3A: letter_ = 0x41; break;
    case 0x40: letter_ = 0x39; break;
    default: break;
    }
    if (in.right) letter_ = end_letter; // $69786
    if (left) waiting_release_ = true;   // accepted once the button is let go ($697BC)
    return false;
}

std::optional<GameStart> FrontEnd::name_finished() {
    const auto& program = assets_->program;
    GameStart start;
    const auto rainbow = name_of(program, rainbow_name);
    if (code_) {
        // $694D6: a level code. Digits 1-2 and 11-12 must mirror, 1-2 give
        // the level (1-33) and digits 5-8 add up to the level's index + 1.
        const auto n = [&](int i) { return std::uint8_t(name_[std::size_t(i)]); };
        bool good = n(0) == n(11) && n(1) == n(10);
        std::uint8_t sum = 0;
        for (int i = 4; i < 8; ++i) sum = std::uint8_t(sum + std::uint8_t(n(i) - '0'));
        sum = std::uint8_t(sum - 1);
        std::uint8_t tens = std::uint8_t(n(0) - '0'), level = std::uint8_t(n(1) - '0');
        while (tens != 0) {
            level = std::uint8_t(level + 10);
            --tens;
        }
        good = good && level <= 0x21 && level != 0;
        level = std::uint8_t(level - 1);
        good = good && level == sum;
        if (good) {
            start.level_index = level;
            start.from_code = true;
        }
        // $69598: the name becomes RAINBOW ARTS either way; colour 0 $333.
        std::copy(rainbow.begin(), rainbow.begin() + 12, name_.begin());
        grey_colour_ = 0x333;
        start.name = rainbow.substr(0, 12);
        return start;
    }
    const auto is = [&](const std::string& other) { return name_.compare(0, 11, other, 0, 11) == 0; };
    if (is(rainbow)) {
        begin_name(true);
        return std::nullopt;
    }
    if (is(name_of(program, country_name))) {
        // The jukebox ($68CD4): all music stops ($709E6) and the panel
        // below the title scrolls into view; afterwards the name is asked
        // again ($719AC).
        jukebox_request_ = 0;
        state_ = State::jukebox_down;
        return std::nullopt;
    }
    start.magic_map = is(name_of(program, magic_map_name));
    start.name = name_.substr(0, 11) + "("; // $6941C copies eleven characters and a space
    return start;
}

std::optional<GameStart> FrontEnd::tick(const FrontEndInput& in) {
    switch (state_) {
    case State::release_left: // $68FBE
        if (!in.left) state_ = State::title;
        break;
    case State::title: // $68FCA
        if (in.right) begin_name(false);
        else if (pending_ || in.left) state_ = State::scroll_down;
        break;
    case State::scroll_down:
        scroll_step(1);
        if (scroll_ == scroll_rows) { // $69144
            table_ = data::ScoreTable::hall_of_fame;
            draw_records();
            state_ = State::records;
        }
        break;
    case State::records:
        if (in.right) {
            pending_ = false; // $68FFC: shown once
            state_ = State::scroll_up;
            break;
        }
        step_records(in.left);
        if (redraw_) draw_records();
        break;
    case State::scroll_up:
        scroll_step(-1);
        if (scroll_ == 0) state_ = State::release_left;
        break;
    case State::jukebox_down: // $68CD8: down to the panel, then the titles
        scroll_step(1);
        if (scroll_ == scroll_rows) {
            std::uint32_t at = jukebox_texts;
            for (int i = 0; i < jukebox_text_count; ++i) {
                const auto t = read_text(assets_->program, at);
                print_at(canvas(), world_->bank, records_x, records_y, t);
                at += std::uint32_t(t.text.size()) + 1;
            }
            draw_jukebox_number();
            jukebox_wait_ = 0;
            state_ = State::jukebox;
        }
        break;
    case State::jukebox: // $68D88: a pass every two delays
        if (++jukebox_wait_ < jukebox_pass_frames) break;
        jukebox_wait_ = 0;
        if (in.left) jukebox_title_ = jukebox_title_ == '9' ? '0' : char(jukebox_title_ + 1); // held: on and on
        draw_jukebox_number();
        if (!in.right) break;
        if (jukebox_title_ == '0') { // $68E82: exit
            jukebox_request_ = 0;
            state_ = State::jukebox_up;
        } else { // $68DE6: the title's music, until the left button
            jukebox_request_ = jukebox_title_ - '0';
            state_ = State::jukebox_play;
        }
        break;
    case State::jukebox_play:
        if (in.left) { // $68E76: everything stops, back to the loop
            jukebox_request_ = 0;
            state_ = State::jukebox;
            jukebox_wait_ = 0;
        }
        break;
    case State::jukebox_up:
        scroll_step(-1);
        if (scroll_ == 0) begin_name(false);
        break;
    case State::name_entry:
    case State::code_entry: {
        if (waiting_release_) {
            if (in.left) break;
            waiting_release_ = false;
            if (accept(name_, cursor_, letter_)) return name_finished();
            print_name();
            break;
        }
        pass_mouse_x_ += in.mouse_x;
        if (in.key != 0 || ++pass_frame_ == 2) {
            pass_frame_ = 0;
            if (in.key == 0 && !grip_) { // $70746: the read is skipped once (or until the 25th)
                grip_ = true;
                if (++reads_skipped_ < 25) {
                    const int counts = pass_mouse_x_;
                    pass_mouse_x_ = 0;
                    const bool ended = name_pass(in, in.left); // (the wheel stands still)
                    pass_mouse_x_ = counts;                     // the next read sees them
                    if (ended) return name_finished();
                    print_name();
                    break;
                }
            }
            if (in.key == 0) reads_skipped_ = 0;
            const bool ended = name_pass(in, in.left);
            pass_mouse_x_ = 0;
            if (ended) return name_finished();
            print_name();
        }
        break;
    }
    }
    return std::nullopt;
}

platform::Image FrontEnd::picture() const {
    platform::Image image(width, height);
    auto palette = world_->tiles.palette;
    if (grey_) palette[0] = amiga::Rgb4(grey_colour_);
    const int shown = height - std::max(0, scroll_ - shorten_from); // $732A6: the display's last line
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int row = scroll_ + y;
            const std::uint8_t index = y < shown && row < rows_ ? pixels_[std::size_t(row) * width + std::size_t(x)] : 0;
            image.pixels[std::size_t(y) * width + std::size_t(x)] = data::to_color(palette[index]);
        }
    }
    return image;
}

} // namespace rocknroll::game
