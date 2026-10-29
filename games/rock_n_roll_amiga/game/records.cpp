#include "rock_n_roll_amiga/game/records.hpp"

namespace rocknroll::game {
namespace {

constexpr std::uint32_t table_lines = 0x6A110;   // ten texts, 28 bytes apart: column 4, row 6 + i, "(1" …
constexpr std::uint32_t line_stride = 0x1C;
constexpr std::uint32_t press_button = 0x6A228;  // "PRESS BUTTON TO CONTINUE" (3, 12)
constexpr std::uint32_t no_entry = 0x6A260;      // "SORRY, NO HIGH-SCORE ENTRY" (3, 1)
constexpr std::uint32_t congratulations = 0x6A27E;
constexpr std::uint32_t finished_level = 0x6A0C0; // "YOU FINISHED LEVEL " (2, 3)
constexpr std::uint32_t level_digits = 0x6A0D8;   // its two digits ($6AA8E)
constexpr std::uint32_t blank_row4 = 0x6A0DC;
constexpr std::uint32_t place_texts = 0x6A299;   // "IN 1ST PLACE." …, 16 bytes apart

char digit(int v) { return char('0' + v); }

// $6F5A4 and the tenths: seconds as MM:SS: then the tenth.
std::string time_text(std::uint32_t value) {
    int s = int((value >> 8) & 0xFFFF);
    std::string t;
    t += digit(s / 600);
    s %= 600;
    t += digit(s / 60);
    s %= 60;
    t += ':';
    t += digit(s / 10);
    t += digit(s % 10);
    t += ':';
    t += digit(int(value & 0xFF) / 10);
    return t;
}

// $6F5D6: five digits (no blanks), then a '0' and a space.
std::string score_text(std::uint32_t value) {
    std::string t;
    int v = int(value);
    for (const int unit : {10000, 1000, 100, 10, 1}) {
        t += digit(v / unit);
        v %= unit;
    }
    return t + "0(";
}

} // namespace

std::vector<ScreenText> record_lines(const data::Program& program, const data::ScoreTable& scores, int table, int highlighted) {
    const bool hall = table == data::ScoreTable::hall_of_fame;
    const int places = hall ? data::ScoreTable::score_places : data::ScoreTable::time_places;
    std::vector<ScreenText> lines;
    for (int p = 1; p <= places; ++p) {
        auto line = read_text(program, table_lines + std::uint32_t(p - 1) * line_stride, p == highlighted ? 0 : 2);
        const auto entry = scores.entry(table, p);
        const std::string written = "(" + entry.name + "(" + (hall ? score_text(entry.value) : time_text(entry.value));
        line.text.replace(4, written.size(), written); // after column, row and the place's "(1"
        lines.push_back(std::move(line));
    }
    return lines;
}

std::uint32_t finished_time(int seconds, int hundredths) {
    return ((std::uint32_t(seconds - 1) << 8) | std::uint32_t(hundredths & 0xFF)) & 0xFFFFFF;
}

std::vector<ScreenText> results_texts(const data::Program& program, const data::ScoreTable& scores, int level_index, int place) {
    auto texts = record_lines(program, scores, level_index, place);
    texts.push_back(read_text(program, press_button));
    if (place != 0) {
        texts.push_back(read_text(program, congratulations));
        auto level = read_text(program, finished_level);
        const int number = level_index + 1;
        const auto at = std::size_t(level_digits - finished_level); // the two 0 bytes in the text
        if (level.text.size() >= at + 2) {
            level.text[at] = digit(number / 10);
            level.text[at + 1] = digit(number % 10);
        }
        texts.push_back(level);
        texts.push_back(read_text(program, blank_row4));
        texts.push_back(read_text(program, place_texts + std::uint32_t(place - 1) * 16));
    } else {
        texts.push_back(read_text(program, no_entry));
        texts.push_back(read_text(program, blank_row4));
    }
    return texts;
}

std::uint16_t results_flash_colour(int frames) {
    // $6B9F4: colour 1 starts at 4 (its own register offset), then each
    // pass d0 = ((d0 + 2) & $FFF) | $335 and a delay loop counts up to d0's
    // low byte. At about 22 cycles a count and 60 a pass, one PAL frame
    // (141,875 cycles) holds tens of passes.
    constexpr int frame_cycles = 141875;
    std::uint16_t value = 4;
    long budget = long(frames) * frame_cycles;
    while (budget > 0) {
        value = std::uint16_t(((value + 2) & 0xFFF) | 0x335);
        budget -= 60 + 22 * ((value & 0xFF) + 1);
    }
    return value;
}

} // namespace rocknroll::game
