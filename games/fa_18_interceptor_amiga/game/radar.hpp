#pragma once
#include "fa_18_interceptor_amiga/game/flight_tables.hpp"
#include "fa_18_interceptor_amiga/game/object_step.hpp"
#include <cstdint>
#include <utility>
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
        template <class A> void persist(A& ar) { ar(x, y, colour, wide); }
    };
    struct Entry {
        std::int32_t x{}, z{};   // turned by the heading, scaled back up
        int record{};
        bool on_scope{};         // flags bit 5
        template <class A> void persist(A& ar) { ar(x, z, record, on_scope); }
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
    // `viewed`: the record the view follows (`$13ABCC`, the player's 0).
    void panel(Records& records, std::uint16_t frame, int look_shift, int range_shift, std::uint8_t mode,
               int viewed = 0);
    void end_panel(std::uint16_t frame);

    // `$10DEF8`: key T asks for the next target.
    void request_target() { target_request_ = true; }
    int locked() const { return locked_; }          // `$13ACB0` / 512, -1: none
    std::int8_t confirmation() const { return confirm_; } // `$13AB5F`: < 0 confirmed
    void unlock() { locked_ = -1; }
    const std::vector<Entry>& entries() const { return entries_; }
    const std::vector<Blip>& blips() const { return blips_; }
    std::int8_t redraw() const { return redraw_; }
    // The threats the last pass saw within $10000 units (`$13AB5E`, kept
    // while the panel is not on screen): bit 1 a friendly aircraft, 2 an
    // enemy, 3 the cruise missile, 4 / 5 a Sidewinder / AMRAAM on the
    // viewed record. New ones ask for warning beeps (`$13AE44` bits: 1 a
    // missile, 2 an aircraft or the cruise missile, $800 a friendly but in
    // mission 5), taken here once.
    std::uint8_t threats() const { return threats_; }
    std::uint32_t take_warnings() { return std::exchange(warnings_, 0); }
    // `$13AB77`: set to $18 by a new enemy or cruise missile (its reader is
    // not traced).
    std::uint8_t threat_timer() const { return threat_timer_; }
    void set_state(std::int8_t redraw, int locked, std::int8_t confirm) {
        redraw_ = redraw;
        locked_ = locked;
        confirm_ = confirm;
    }

private:
    void scan(Records& records, std::uint8_t mode, int range_shift, int viewed);
    void threats_seen(std::uint8_t found, std::uint8_t mode);
    void pick_target(Records& records);

    std::vector<Entry> entries_;
    std::vector<Blip> blips_;
    std::uint8_t passes_{};     // `$13AB73`: the locked contact blinks by it
    bool scan_due_{};           // `$13AB28`
    bool target_request_{};     // `$13AAA9`
    int locked_{-1};
    std::int8_t confirm_{};
    std::int8_t redraw_{-1};    // `$13AB27`
    std::uint8_t threats_{};    // `$13AB5E`
    std::uint32_t warnings_{};
    std::uint8_t threat_timer_{};
public:
    template <class A> void persist(A& ar) { ar(entries_, blips_, passes_, scan_due_, target_request_, locked_, confirm_, redraw_, threats_, warnings_, threat_timer_); }
};

} // namespace interceptor::game
