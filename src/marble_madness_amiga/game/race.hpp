#pragma once
#include "marble_madness_amiga/data/game_files.hpp"
#include "marble_madness_amiga/game/marble.hpp"
#include "marble_madness_amiga/game/probe.hpp"
#include "marble_madness_amiga/game/regions.hpp"
#include "marble_madness_amiga/game/terrain.hpp"
#include <array>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

// One race on one course: the marbles, the ground, the regions and the
// view's scroll, advanced one world update (two display frames) at a time.
namespace marble_madness::game {

// A player's push for one world update, as the input code leaves it
// ($119C62 / $119C66: +-$20 per axis from a joystick, doubled by fire).
struct Push {
    std::int16_t forward{}; // adds to vx (joystick down)
    std::int16_t side{};    // subtracts from vy (joystick right is negative)
};

// What the sound code is asked to play: an entry of the course's sound
// table ($1222E4) and the side of the screen to play it on (right: the
// Amiga's right channels).
struct SoundEvent {
    int id{};
    bool right{};
};

class Race {
public:
    // A race from its start ($1030B4); difficulty 0-7 sets the time.
    Race(const data::GameFiles&, data::Course, int players = 1, int difficulty = 0);
    // Races copy as independent races (quick saves, look-ahead).

    // $10B1EE: one world update with the players' pushes. The game reads the
    // controls at the end of an update ($119944) for the next one, so the
    // caller passes what it read after the previous update.
    void update(const std::array<Push, 2>& pushes);

    std::array<Marble, 2> marbles{};
    int players() const { return players_; }
    data::Course course() const { return course_; }
    const Terrain& terrain() const { return terrain_; }
    const Regions& regions() const { return regions_; }

    // Practice's drop markers ($100BBE, 12 bytes per player): where the
    // player landed for the drop bonus, with its screen position; kind -1:
    // none, 0: zones 1 and 2, 1: zone 3.
    struct DropMark {
        std::int16_t x{}, y{}, z{};
        std::int32_t screen{};
        std::int8_t kind{-1};
    };
    const std::array<DropMark, 2>& drop_marks() const { return drop_marks_; }
    Regions& regions() { return regions_; }
    // The sounds asked for since the last call (the course start's music
    // first).
    std::vector<SoundEvent> take_sounds() { return std::exchange(sounds_, {}); }

    // $1005EA: 0 a race, 1 the attract demo, other values between races.
    int game_mode{};

    // Colour values the game has written into the Track's copper lists
    // (instruction address -> colour), over the stored ones.
    const std::map<std::uint32_t, std::uint16_t>& copper_colours() const { return copper_colours_; }

    // The view (for the picture): the screen's offset in course space and
    // the scroll in lines from the top.
    std::int32_t view() const { return view_; }
    std::int16_t scroll_lines() const { return scroll_; }
    int frame() const { return frame_; }

private:
    void scroll();                                 // $10D618
    bool scroll_down(int lines);                   // $10D278
    bool scroll_up(int lines);                     // $10D384
    int band(std::int32_t view) const;             // $10D9BC
    void count_down();                             // $10B7E6

    // Marble rules (marble_rules.cpp).
    void update_marble(Marble&);                   // $114402
    void roll(Marble&);                            // $115470
    void push(Marble&);                            // $1134C4
    void cap_speed(Marble&);                       // $1153E6
    void friction(Marble&, int kind);              // $115088
    void slope_force(Marble&);                     // $114FF0
    void keep_on_ground(Marble&, std::int32_t ground); // $118BF4
    void push_off_walls(Marble&);                  // $119328
    void bounce_off_edges(Marble&);                // $10EF4C
    void land(Marble&, int impact);                // $114DEC
    void set_mode(Marble&, int mode);              // $114C6E
    void respawn(Marble&);                         // $1149F6
    void choose_respawn_point(Marble&);            // $112CA4
    bool appear(Marble&);                          // $1138D4
    int occupied(const Marble*, int x, int y, int radius) const; // $116578
    void track_zones(Marble&);                     // $1130BE
    void save_grounded(Marble&);                   // $1141A0
    void animate(Marble&);                         // $1141E0
    void project(Marble&, bool reload);            // $10EADC
    void touch_regions(Marble&);                   // $116EE0
    void zone_changed(Marble&);                    // $112F20
    void reach_goal(Marble&, std::int8_t zone_before);
    void award(Marble&, int bonus);                // $10A82A
    void colour_goal(int player);                  // $108710
    void drop_bonus(Marble&);                      // $11573A
    void project_drop_marks();                     // $11DAAC
    void project(DropMark&);                       // $106A00
    void reset_shine(Marble&);                     // $115D68
    void rotate_shine(Marble&);                    // $115DFA
    void check_visible(Marble&);                   // $11F486
    void build_covers(Marble&);                    // $11EC42
    bool is_player(const Marble& m) const { return &m == &marbles[0] || &m == &marbles[1]; }

    std::int32_t ground_at(std::int32_t x, std::int32_t y);
    void locate(const Marble& m) { probe_.locate(terrain_, m.x, m.y, m.z); }
    // $1224D8: on the side of the point last looked up in the height map.
    void sound(int id) { sound(id, probe_.x() < probe_.y()); }
    void sound(int id, bool right) { sounds_.push_back({id, right}); } // $1222E4
    // $12250A: ends the rolling sounds (entries $F and, except on
    // Intermediate, $10).
    void sound_stop() {
        sound(0xF);
        if (course_ != data::Course::intermediate) {
            sound(0x10);
        }
    }

    const data::GameFiles* files_{};
    data::Course course_{};
    int players_{1};
    Terrain terrain_;
    Regions regions_;
    Probe probe_;
    std::array<Push, 2> push_{};
    std::vector<SoundEvent> sounds_;

    // The marble's position at the start of its update ($1006D0-$1006D8)
    // and the bounce flags of the edge and region code ($1006B9, $1006BA).
    std::int32_t start_x_{}, start_y_{}, start_z_{};
    std::int16_t start_cell_x_{}, start_cell_y_{};
    bool bounce_x_{}, bounce_y_{};
    // The respawn point chosen last ($1006A8, $1006AC, $1006B8).
    std::int16_t respawn_x_{}, respawn_y_{};
    std::int8_t respawn_feature_{};
    // The view: course-space offset of the screen ($1009D2), the scroll in
    // lines ($10D5F4) and its limit ($10D5F2).
    std::int32_t view_{};
    std::int16_t first_view_{};    // descriptor +$10
    std::int16_t scroll_{}, scroll_limit_{};
    std::int8_t first_home_{-1};   // $1005FC: the player who reached the goal first
    std::uint8_t score_changed_{}; // $1005F4: bit per player
    std::map<std::uint32_t, std::uint16_t> copper_colours_;
    std::array<DropMark, 2> drop_marks_{};
    std::uint8_t drop_bonus_taken_{}; // $10060E: bit per player (player + 1), once a game
    int frame_{1};                 // $100248: world updates so far
    bool scrolling_{};             // $100242
    std::int8_t scroll_direction_{}; // $100240: 1 down the course, -1 up
    std::int8_t scroll_speed_{};   // $100243
    std::int16_t scroll_goal_{};   // $100244

    // Track and marble data addresses.
    std::uint32_t descriptor_{};   // header +0
    std::uint32_t respawn_points_{}; // header +4: x, y (cells), zone; $FF ends
    std::uint32_t zones_{};        // header +8: zone boundary lines
    std::uint32_t marble_data_{};  // marbdat's table
};

} // namespace marble_madness::game
