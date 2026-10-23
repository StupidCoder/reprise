#pragma once
#include "app/game_module.hpp"

namespace fort_apocalypse::game {

// The function key the game sees ($18, read by $AFED): it checks keyboard
// column 0 for exactly one of F7, F5 or F3; anything else counts as none.
// While joystick 2 is moved or fired the keyboard is not read at all
// (the port shares its lines with the keyboard matrix), so no key counts.
enum class FunctionKey { none, f3, f5, f7 };

// The controls as the game reads them once per frame.
struct Controls {
    bool up{}, down{}, left{}, right{}, fire{};
    FunctionKey key{FunctionKey::none};
    bool space{}; // the space bar, alone in its keyboard column ($19 = $EF)
};

inline Controls read_controls(const app::GameInput& in) {
    Controls c{in.up, in.down, in.left, in.right, in.fire};
    if (in.up || in.down || in.left || in.right || in.fire) {
        return c; // $AFF2: the joystick drives the lines; no keyboard
    }
    // Column 0 holds F1, F3, F5 and F7 (and other keys we do not offer);
    // only a single one of the three is recognised. F1 alone reads as none.
    const int pressed = in.f3 + in.f5 + in.f7 + in.f1;
    if (pressed == 1) {
        c.key = in.f7 ? FunctionKey::f7 : in.f5 ? FunctionKey::f5 : in.f3 ? FunctionKey::f3
                                                                          : FunctionKey::none;
    }
    c.space = in.space;
    return c;
}

} // namespace fort_apocalypse::game
