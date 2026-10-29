#pragma once
// Puzzles the native engines play, in the order the pack's puzzle/library.txt
// lists them (internal IDs, the n%u.dat numbering). Since v0.26 this is every
// named record in the ROM. A local research recipe holds the same list;
// import_tests compares the two when it is given that file.
#include <array>

namespace picarat::import {
inline constexpr std::array<unsigned, 187> kPuzzleLibrary{
    1,   116, 3,   6,   26,  40,  45,  47,  48,  52,  55,  62,  64,  75,  76,  77,  89,  96,  129,
    130, 205, 135, 140, 145, 153, 166, 167, 178, 186, 190, 2,   39,  115, 198, 203, 5,   82,  83,
    141, 7,   24,  49,  73,  110, 120, 131, 134, 181, 8,   15,  22,  27,  28,  31,  35,  50,  60,
    72,  78,  84,  95,  114, 117, 124, 132, 137, 164, 168, 169, 170, 171, 175, 188, 34,  38,  80,
    86,  189, 191, 105, 106, 107, 147, 100, 101, 102, 103, 104, 138, 4,   183, 9,   10,  11,  12,
    13,  14,  16,  17,  18,  19,  20,  21,  23,  25,  29,  30,  32,  33,  36,  37,  41,  42,  43,
    44,  46,  51,  53,  54,  56,  57,  58,  59,  61,  63,  65,  66,  67,  68,  69,  70,  71,  74,
    79,  81,  85,  87,  88,  90,  91,  92,  93,  94,  97,  98,  99,  108, 109, 111, 112, 113, 118,
    119, 121, 122, 123, 125, 126, 127, 128, 133, 136, 139, 142, 143, 144, 146, 148, 149, 150, 152,
    155, 157, 158, 159, 160, 161, 162, 174, 176, 179, 180, 182, 194, 199, 204, 206};
} // namespace picarat::import
