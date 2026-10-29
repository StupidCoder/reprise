#pragma once
#include "fa_18_interceptor_amiga/game/flight_tables.hpp"
#include "fa_18_interceptor_amiga/game/object_step.hpp"
#include <cstdint>
#include <vector>

// The radar (game.md "The radar"): after the scene is drawn, in the cockpit
// view, a list of every other active record's place relative to the viewed
// aircraft, turned by its heading (`$10FF26`, `$119DDA`: list `$1435A8`);
// the panel's radar pass (`$125E22` → `$125F98`) walks it: records within
// the 40-mile window are "on the radar" (state $4000), those near enough
// are seen (+$20 bit 6) - and, while something is locked, mission 3's
// confirmation counts down (`$13AB5F`) - and those within the chosen range
// are on the scope (the entry's bit 5), which key T's lock picks from
// (`$126408`).
namespace interceptor::game {

class Radar {
public:
    // A contact on the scope as the pass draws it (`$126230`): the panel's
    // pixel, colour (`$13AC44`), two pixels wide when at or above the
    // viewer's height (`$12420A`).
    struct Blip {
        int x{}, y{}, colour{};
        bool wide{};
    };
    struct Entry {
        std::int32_t x{}, z{};   // turned by the heading, scaled back up
        int record{};
        bool on_scope{};         // flags bit 5
    };

    void reset();

    // `$10F024`: the panel is drawn anew (a view or look change, every 32
    // frames): for three frames the radar works every frame, twice.
    void redraw_panel() { redraw_ = 3; }
    // `$10FF26`, after the scene: the list, every 4th frame (every frame
    // while the panel is redrawn); none outside.
    void after_scene(Records& records, std::uint16_t frame, bool outside, const FlightTables& tables);
    // `$125E22`: the panel's radar, when on screen (`look_shift` = `$13AC76`
    // within 11) - the pass over the list and key T's request; then the
    // panel's end of frame (`$1013DC`, `$101492`).
    void panel(Records& records, std::uint16_t frame, int look_shift, int range_shift, std::uint8_t mode);
    void end_panel(std::uint16_t frame);

    // `$10DEF8`: key T asks for the next target.
    void request_target() { target_request_ = true; }
    int locked() const { return locked_; }          // `$13ACB0` / 512, -1: none
    std::int8_t confirmation() const { return confirm_; } // `$13AB5F`: < 0 confirmed
    void unlock() { locked_ = -1; }
    const std::vector<Entry>& entries() const { return entries_; }
    const std::vector<Blip>& blips() const { return blips_; }
    std::int8_t redraw() const { return redraw_; }
    void set_state(std::int8_t redraw, int locked, std::int8_t confirm) {
        redraw_ = redraw;
        locked_ = locked;
        confirm_ = confirm;
    }

private:
    void scan(Records& records, std::uint8_t mode, int range_shift);
    void pick_target(Records& records);

    std::vector<Entry> entries_;
    std::vector<Blip> blips_;
    std::uint8_t passes_{};     // `$13AB73`: the locked contact blinks by it
    bool scan_due_{};           // `$13AB28`
    bool target_request_{};     // `$13AAA9`
    int locked_{-1};
    std::int8_t confirm_{};
    std::int8_t redraw_{-1};    // `$13AB27`
};

} // namespace interceptor::game
