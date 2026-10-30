#pragma once
#include "platform/image.hpp"
#include "stunt_car_racer_amiga/data/cockpit.hpp"
#include "stunt_car_racer_amiga/render/race_view.hpp"
#include <optional>
#include <vector>

// The whole race screen: the cockpit around the view through the windscreen,
// the car's front wheels and engine, the crane's chains, boost flames and
// smoke, the tyre treads, and the dashboard (game.md, "Race screen").
namespace stuntcar::render {

// What the screen shows besides the view; the race fills it in each frame.
struct Cockpit {
    int steering = 1;                  // wheel frame 0-2 (1 straight ahead)
    int right_wheel_y = 122;           // top rows of the front wheels, 101-135
    int left_wheel_y = 122;
    std::optional<int> chain_bottom;   // while the crane holds the car (game units)
    bool boost = false;
    int boost_phase = 0;               // 0-2, the flames' frame
    int intact_marks = 10;             // whole marks along the roll bar ($1C9CF), the rest holes
    bool cracking = false;             // the first hole is still cracking
    bool flag_lamp = false;            // the chequered-flag lamp (lit or dim)
    bool right_lamp = false;           // the lamp right of the steering wheel
    int speed_bar = 0;                 // 0-127
    int distance = 0;                  // to the opponent, shown signed in four digits
    int lap = 0;                       // shown after "L" when not 0
    struct Time {
        int minutes{}, seconds{}, hundredths{};
        bool show_hundredths{};
    };
    std::optional<Time> lap_time;      // the right-hand box, upper line
    std::optional<Time> lower_time;    // lower line (the race or best lap time)
    std::optional<int> boost_left;     // shown after "B" in league races (0-99)
    std::optional<data::Message> message; // in the view, black (white when `message_white`)
    bool message_white = false;
    bool message_box = false;          // the grey box behind PRESS FIRE
    struct Puff {
        int x{}, y{}, size{};          // screen pixels; puff 0-7
    };
    std::vector<Puff> smoke;
    struct Spark {
        int x{}, y{};                  // view pixels
    };
    std::vector<Spark> sparks;         // dust thrown up in front of the car
    struct Pixel {
        int x{}, y{};
        std::uint8_t colour{};
    };
    std::vector<Pixel> crack;          // along the roof (screen pixels)
};

platform::Image draw_race_screen(const data::CockpitArt&, const View&, const Palette&, const Cockpit&);

// A still for the launcher: the car hanging from the crane at the start of
// `track`, "DROP / START" in the view, the opponent a step and a half ahead
// on the left.
platform::Image start_screen(const data::Program&, const data::Circuits&, const data::Scenery&,
                             const data::CockpitArt&, const data::Track&);

} // namespace stuntcar::render
