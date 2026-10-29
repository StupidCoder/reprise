// Score, high score and the big digits of the score tally (1D2C-1EC8).

#include "sonic2_gg/game/bcd.hpp"
#include "sonic2_gg/game/game.hpp"

namespace sonic2_gg {

// 1D2C: adds a 3-byte BCD value (in tens of points) from bank 0 to the score
// (not in demos), capping at 999,990. Entry points 1D14/1D1A/1D20/1D26 pass
// 1ECC (100 points), 1ECF (500), 1ED2 (1000) and 1EF0.
void Game::add_score(Table points) {
    if (flow.attract_flags) {
        return;
    }
    bool carry = false;
    for (int i = 0; i < 3; ++i) {
        score.points[i] = add_bcd(score.points[i], rom.byte(points, unsigned(i)), carry);
    }
    if (carry) {
        score.points[0] = 0x90;
        score.points[1] = 0x99;
        score.points[2] = 0x99;
        score.changed = 2;
    } else {
        score.changed = 1;
    }
    update_high_score(); // 1D61
    // 1D90: one extra life at 30,000 points.
    if (score.extra_life_awarded) {
        return;
    }
    if (score.points[2] < 0x03) {
        return;
    }
    score.extra_life_awarded = 1;
    ++score.lives;
    if (flow.game_flags & game_flag::zone_complete) {
        return;
    }
    show_lives();
}

// 1D61: a score above the high score replaces it.
void Game::update_high_score() {
    for (int i = 2; i >= 0; --i) {
        if (score.points[i] < score.high[i]) {
            return;
        }
        if (score.points[i] > score.high[i]) {
            break;
        }
    }
    for (int i = 0; i < 3; ++i) {
        score.high[i] = score.points[i];
    }
    score.changed = 2;
}

// 1DAB / 1DBC / 1DCB: the tally's ring count, bonus and score as big digits.
// The bonus entry continues into the score, as in the original.
void Game::show_ring_tally() {
    di(); // 1DAB
    draw_big_number(std::span(&score.rings, 1), {18, 14});
    ei(); // 1DBA
}

void Game::show_bonus_tally() {
    di(); // 1DBC
    draw_big_number(std::span(score.bonus).first(2), {18, 16});
    show_score_tally();
}

void Game::show_score_tally() {
    draw_big_number(score.points, {16, 18});
    ei(); // 1DD9
}

// 1DDB/1E93: a BCD number (least significant byte first) as big digits,
// most significant first, leading zeros (all but the last) blank. Each digit
// is 2x2 tiles from the table at 1E63 (four entries per digit; 10 is the
// blank). (1E93 skips input that is not BCD; the numbers drawn here always
// are.)
void Game::draw_big_number(std::span<const u8> bcd, Display::Cell at) {
    bool leading = true;
    for (int n = int(bcd.size()) * 2 - 1; n >= 0; --n) {
        unsigned digit = (bcd[std::size_t(n / 2)] >> ((n & 1) * 4)) & 0x0F;
        if (leading && n > 0 && digit == 0) {
            digit = 10;
        } else {
            leading = false;
        }
        const auto glyph = rom::big_digits + digit * 4;
        display.entry(at) = rom.word(glyph);
        display.entry({at.column, at.row + 1}) = rom.word(glyph, 2);
        ++at.column;
    }
}

} // namespace sonic2_gg
