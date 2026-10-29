#pragma once
#include "rock_n_roll_amiga/data/assets.hpp"
#include "rock_n_roll_amiga/game/font.hpp"
#include <vector>

// The time records and the hall of fame as the game shows them: the table's
// lines ($6A7BC into the texts at $6A110), the screen after a finished
// level ($6A344 → $6A874/$6AA6A) and the record a finished level makes.
namespace rocknroll::game {

// A table's lines: place p at column 4, row 5 + p ("(1" … "10"), then the
// stored name and its value: a time as MM:SS:T ($6F5A4 and the tenths), a
// score as five digits and a '0' ($6F5D6). The highlighted place (a new
// entry) is printed into plane 0 instead of plane 2 ($6AE12 = 0).
std::vector<ScreenText> record_lines(const data::Program&, const data::ScoreTable&, int table, int highlighted = 0);

// The value a finished level records ($6C61E, $6A36A): the clock as shown,
// one second less (the panel shows the second before), seconds << 8 |
// hundredths.
std::uint32_t finished_time(int seconds, int hundredths);

// The texts shown in the view after a level ($6A874, $6AA6A): the level's
// five records with the new one highlighted, "PRESS BUTTON TO CONTINUE",
// then "CONGRATULATIONS / YOU FINISHED LEVEL nn / IN nTH PLACE." or "SORRY,
// NO HIGH-SCORE ENTRY".
std::vector<ScreenText> results_texts(const data::Program&, const data::ScoreTable&, int level_index, int place);

// The highlighted entry's colour while the results wait for the button
// ($6B9F4 cycling colour 1): the value after `frames` frames (an estimate
// of the loop's speed; the original changes it many times a frame).
std::uint16_t results_flash_colour(int frames);

} // namespace rocknroll::game
