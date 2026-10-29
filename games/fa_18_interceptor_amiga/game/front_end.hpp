#pragma once
#include "fa_18_interceptor_amiga/data/assets.hpp"
#include "fa_18_interceptor_amiga/game/pilot_record.hpp"
#include "fa_18_interceptor_amiga/game/scene.hpp"
#include "fa_18_interceptor_amiga/game/sounds.hpp"
#include "fa_18_interceptor_amiga/game/text_screen.hpp"
#include <array>
#include <functional>
#include <optional>

// The front end (game.md "The front end"): the title with its tune, the
// credits, the pilot's welcome or call-sign entry, the main menu, the
// flight log, and free flight's start map with the place and aircraft
// choice. The original runs these as screen procedures ($101962-$103C1A),
// each frame one of them, mostly waiting for the frame countdown or a key;
// here they are the screens of one state machine.
//
// Text screens run a frame per vertical blank (50 Hz: the main loop only
// waits for the blank there); the map is drawn by the 3D view and runs at
// the flight's 15 frames a second.
namespace interceptor::game {

// The keys the front end reads, pressed this frame.
struct FrontKeys {
    bool any = false;      // any key ($13AA93)
    char typed = 0;        // a character for the call-sign field
    bool enter = false, backspace = false;
    bool escape = false;   // Esc (the port's Tab)
    bool shift_escape = false;
    int digit = -1;        // 1-9 of the top row
    bool shift_2 = false;
};

// What the menu started ($13AB96): free flight (1), the demo ($7F, menu
// item 1), the maneuvers' demo (2), their practice ($7D), the
// qualification (9).
enum class FlightMode { free_flight, demo, maneuvers, practice, qualification };

// A flight as the player chose it.
struct FlightStart {
    int place = 0;         // 0-2: the runways, 3 the carrier ($13AB38)
    int aircraft = 0x11;   // $11 the F/A-18, $10 the F-16 ($13AB39)
    FlightMode mode = FlightMode::free_flight;
    // Started again (Esc, a qualifying landing): the security check counts
    // as passed (`$13AB1B`) and the qualification's briefing is left out.
    bool again = false;
};

class FrontEnd {
public:
    FrontEnd(const data::Assets& assets, const Scene& scene, Sounds& sounds, PilotRecord& pilot,
             std::function<void()> write_pilot);

    void start_title();
    // Where the carrier lies (32-bit x and z): place 4's marker.
    void set_carrier(std::array<std::int32_t, 2> xz) { carrier_ = xz; }
    // $101A70: back to the main menu (Shift-Esc, the end of free flight).
    void to_menu();
    // Free flight's start map again.
    void to_start_map();
    // $101A96 → $101E04 (Esc, a qualifying landing): the chosen item's
    // line again for 150 frames, then the item starts again.
    void again(int item);

    // One frame; the start of free flight when the player has chosen it.
    std::optional<FlightStart> tick(const FrontKeys& keys);
    // 50 frames a second on the text screens, 15 on the map.
    bool fast() const { return screen_ != Screen::map && screen_ != Screen::map_wait; }

    void draw(IndexedImage& image, int margin) const;
    const data::Palette& palette() const;

private:
    enum class Screen {
        title,          // $1003CE: the title picture and the tune
        title_fade,     // $107D48: 16 steps to black
        credits,        // $101962 / $103596: block 15
        credits_wait,   // $1035C8: any key
        pilot_check,    // $103622 / $10370A: the record read
        first_tour,     // $1037EA: block 1
        call_sign,      // $10381E / $103888: block 3, typed in
        welcome,        // $103800: block 2
        menu,           // $101D30 / $101E04
        chosen,         // $101E04 / $10201E: the chosen line, the tune fading
        log,            // $101F86: block 87
        map_wait,       // $10234C .. $102AFC: the map coming up
        map,            // $102B74: a place, then the aircraft
    };

    void go(Screen s) { screen_ = s; }
    void menu_screen();
    void show_chosen(int item, int line);
    void flight_log();
    void choose(const FrontKeys& keys);
    data::TextBlock block(int number) const;

    const data::Assets& assets_;
    const Scene& scene_;
    Sounds& sounds_;
    PilotRecord& pilot_;
    std::function<void()> write_pilot_;
    TextRunner text_;
    Screen screen_ = Screen::title;
    int countdown_ = 0;           // $13ADC6
    int frame_ = 0;               // frames on this screen
    int choice_ = 0;              // $13AB96: the menu item
    bool unbuilt_ = false;        // the chosen item is not rebuilt yet
    bool again_ = false;          // `$13AB1B`: started again, the check passed
    int place_ = -1;              // $13AB38, chosen
    bool asking_aircraft_ = false;// $13AA81
    int palette_ = 0;             // which of the game's palettes shows
    data::Palette title_palette_{};
    int fade_ = 0;                // title fade steps done
    std::array<std::int32_t, 2> carrier_{};
};

} // namespace interceptor::game
