#pragma once
#include "wizball_c64/data/assets.hpp"
#include <array>
#include <cstdint>

// The top scores ($BBA0-$BDA4). Two tables, one-player games and
// two-player games, of eight records each plus one for a newcomer; a
// record is text, as the title prints it: six digits, a space and a name
// of seven characters ("020000 JOP    "). The best of the two tables is
// the title's top line ("1. 050000" and a ten-character name, "SENSI
// SOFT"); the title prints the other places from a text block that is
// rebuilt from the records ($BEB5).
namespace wizball::game {

class Scores {
public:
    explicit Scores(const data::GameImage& image);

    using Record = std::array<std::uint8_t, 14>;

    // $BBA0: the players' scores into the tables (each player's game type
    // picks the table: 1 = one-player), sorted; then each player's place:
    // 0 none, 1 top of the one-player table and of all, 2 in it, 3 top of
    // the two-player table and of all, 4 in it.
    struct Entry {
        bool one_player_table;
        std::array<std::uint8_t, 6> digits; // 0-9
    };
    std::array<std::uint8_t, 2> enter(const Entry& first, const Entry* second);
    // $BD32: the name typed (seven characters) to the record marked with
    // the player's number ('1' or '2'); the title's block rebuilt.
    void name(std::uint8_t marker, const std::array<std::uint8_t, 7>& name);
    // $6CAD: the top name (ten characters).
    void top_name(const std::array<std::uint8_t, 10>& name) { top_name_ = name; }

    // What the title prints.
    std::uint8_t block_byte(std::uint16_t address) const; // the $BEB5 block and the $BFE8 line
    std::uint8_t top_name_byte(std::uint16_t address) const; // the $B69B block
    // The top score's digits as the score line shows them (0-9).
    std::array<std::uint8_t, 6> top_digits() const;

private:
    void sort(int first);       // $BC23
    void rebuild_block();       // $BD52-$BD95
    Record& record(int index) { return records_[std::size_t(index)]; } // 0-8 one-player, 9-17 two-player

    const data::GameImage& image_;
    std::array<Record, 18> records_{};
    std::uint8_t best_table_{};                    // $BDA5: 1 or 2
    std::array<std::uint8_t, 6> top_{};            // $BFEB-$BFF0 (characters)
    std::array<std::uint8_t, 10> top_name_{};      // $B6AA-$B6B3
    std::array<std::array<Record, 6>, 2> block_{}; // the rows of the $BEB5 block (at $BEDF + 40 * row + 20 * column)
};

} // namespace wizball::game
