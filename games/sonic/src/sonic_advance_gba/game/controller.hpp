#pragma once
#include "sonic_advance_gba/game/types.hpp"
#include <array>
#include <vector>

// The buttons as the game reads them once per frame (08001224).
namespace sonic_advance_gba {

// Button bits (the GBA's KEYINPUT order).
namespace button {
inline constexpr u16 a = 0x001, b = 0x002, select = 0x004, start = 0x008;
inline constexpr u16 right = 0x010, left = 0x020, up = 0x040, down = 0x080;
inline constexpr u16 r = 0x100, l = 0x200;
} // namespace button

// The demo recorder and player (030060D8: 1 records, 2 plays): 0x800
// frames of held buttons (030060DC; positions 030060D0, 030060D4).
enum class DemoMode : u8 { off, record, play };

struct Controller {
    u16 raw{};          // 03001140 the buttons really held
    u16 held{};         // 03001B50 the buttons the game sees
    u16 previous{};     // 03002328
    u16 pressed{};      // 03001B78 newly pressed, plus auto-repeats
    u16 pressed_once{}; // 0300237C newly pressed only
    u16 released{};     // 03001FAC
    // Auto-repeat: a button held for `first_delay` frames repeats every
    // `repeat_delay` frames (03001FA0, 030020F0; 20 and 8 unless a screen
    // changes them). 03001B60 counts down.
    std::array<u8, 10> first_delay{}, repeat_delay{}, countdown{};

    Controller() {
        first_delay.fill(20);
        repeat_delay.fill(8);
    }

    DemoMode demo{};
    std::vector<u16> demo_input;
    unsigned demo_read{}, demo_written{};

    void read(u16 buttons) {
        raw = u16(buttons & 0x3FF);
        held = raw;
        if (demo == DemoMode::record) {
            // 0804CCF8
            if (demo_written < 0x800) {
                demo_input.at(demo_written++) = raw;
            }
        } else if (demo == DemoMode::play) {
            // 0804CCCC
            held = demo_read < 0x800 && demo_read < demo_input.size() ? demo_input[demo_read++] : 0;
        }
        pressed = u16((held ^ previous) & held);
        released = u16((held ^ previous) & previous);
        previous = held;
        pressed_once = pressed;
        for (unsigned i = 0; i < 10; ++i) {
            if (!(held & (1u << i))) {
                countdown[i] = first_delay[i];
            } else if (countdown[i] == 0) {
                pressed = u16(pressed | (1u << i));
                countdown[i] = repeat_delay[i];
            } else {
                --countdown[i];
            }
        }
    }
};

} // namespace sonic_advance_gba
