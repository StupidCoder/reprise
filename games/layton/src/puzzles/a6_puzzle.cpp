// The sixth adventure's puzzle types and their play loops. See
// a6_puzzle.hpp.
#include "puzzles/a6_puzzle.hpp"

namespace picarat::adventure6 {

int playLoop(int type) {
    // 0x1d7888: the switch (types 41 and 42 are tested before it).
    switch (type) {
    case 7:
        return 3130;
    case 8:
        return 3110;
    case 9:
        return 3120;
    case 10:
        return 3230;
    case 11:
        return 3240;
    case 12:
        return 3250;
    case 13:
        return 3260;
    case 14:
        return 3270;
    case 16:
        return 3290;
    case 17:
        return 3300;
    case 18:
        return 3310;
    case 20:
        return 3140;
    case 21:
        return 3150;
    case 22:
    case 42:
        return 3160;
    case 25:
        return 3320;
    case 26:
        return 3170;
    case 27:
    case 30:
    case 31:
    case 32:
    case 41:
        return 3180;
    case 28:
        return 3330;
    case 29:
        return 3340;
    default:
        return kCommonLoop;
    }
}

int fifthTypeOfSameClass(int type) {
    switch (playLoop(type)) {
    case 3130:
        return 14; // Ltgram
    case 3110:
        return 12; // Slide
    case 3120:
        return 11; // Line
    case 3230:
        return 10; // Cut
    case 3240:
        return 15; // Pile
    case 3250:
        return 9; // Mosaic
    case 3300:
        return 36; // Othello
    case 3310:
        return 27; // Vanish
    default:
        return 0;
    }
}

bool hasOwnClass(int type) {
    const int loop = playLoop(type);
    return loop == 3140 || loop == 3160 || loop == 3170 || loop == 3180 || loop == 3230 ||
           loop == 3260 || loop == 3270 || loop == 3290 || loop == 3320 || loop == 3330 ||
           loop == 3340;
}

int fifthTypeWithSixthRules(int type) {
    switch (playLoop(type)) {
    case 3110:
    case 3120:
    case 3130:
    case 3240:
    case 3250:
    case 3300:
    case 3310:
        return fifthTypeOfSameClass(type);
    default:
        return 0;
    }
}

} // namespace picarat::adventure6
