#pragma once
#include "marble_madness_amiga/data/game_files.hpp"
#include "marble_madness_amiga/game/crowd.hpp"
#include "marble_madness_amiga/game/display_list.hpp"
#include "marble_madness_amiga/game/marble.hpp"
#include "marble_madness_amiga/game/probe.hpp"
#include "marble_madness_amiga/game/random.hpp"
#include "marble_madness_amiga/game/regions.hpp"
#include "marble_madness_amiga/game/terrain.hpp"
#include <array>
#include <cstdint>
#include <map>
#include <memory>
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

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) { ar(forward, side); }
};

// What the sound code is asked to play: an entry of the course's sound
// table ($1222E4) and the side of the screen to play it on (right: the
// Amiga's right channels).
struct SoundEvent {
    int id{};
    bool right{};
    int marble{-1}; // a crowd race marble's sound (its slot), or -1

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) { ar(id, right, marble); }
};

class Race {
public:
    // A race from its start ($1030B4); difficulty 0-7 sets the time.
    // `random` is the generator to go on with (the game's, carried from race
    // to race); without one the race starts its own from the fixed seed.
    Race(const data::GameFiles&, data::Course, int players = 1, int difficulty = 0,
         const Random* random = nullptr);
    // Races copy as independent races (quick saves, look-ahead).

    // $10B1EE: one world update with the players' pushes. The game reads the
    // controls at the end of an update ($119944) for the next one, so the
    // caller passes what it read after the previous update.
    void update(const std::array<Push, 2>& pushes);

    // Marbles 0 and 1 are the players' (as in the original); 2-7 only race
    // in the crowd race.
    static constexpr int max_marbles = 8;
    std::array<Marble, max_marbles> marbles{};
    std::array<Steelie, 2> steelies{};      // $100BD6 (Beginner, Intermediate, Ultimate)
    std::array<Slinky, 4> slinkies{};       // $101548
    std::array<Acid, 5> acids{};            // $1016F0 (Intermediate, Ultimate)
    std::array<Piston, 36> pistons{};           // $1018BE (Aerial)
    std::array<PistonGroup, 3> piston_groups{}; // Track region block +$23C (Aerial)
    std::array<Critter, 9> critters{};      // $101AFE (Silly)
    std::array<Bird, 10> birds{};           // $101C66 (Silly)
    bool pistons_on() const { return pistons_on_; }
    // Pictures the pistons put into the playfield since the last call (as the
    // regions' blits: item, x, screen line).
    std::vector<Regions::Blit> take_piston_blits() { return std::exchange(piston_blits_, {}); }
    Random random;                          // $109136 (a standard generator)
    int players() const { return players_; }
    // The marbles in the race: the players', and the crowd's while it runs.
    int marble_count() const { return marble_count_; }

    // The crowd race (not the original game, crowd.cpp): the other seven
    // marbles race too, steered by the computer. Turning it on puts them on
    // the course's start; turning it off takes them away.
    void set_crowd(bool on);
    bool crowd() const { return crowd_; }
    const CrowdMap* crowd_map() const { return crowd_map_.get(); }
    // The crowd race's standings (marble slots, the best first): those
    // home in the order they came in, then the others by their distance
    // to the goal; and a marble's place in them (1-based, 0: not racing).
    std::vector<int> standings() const;
    int place(int marble) const;
    // The crowd holds back while ahead of the players (rubber banding).
    bool crowd_close{true};
    // The crowd's skill: 0 easy (slower, no fire, frequent lapses), 1
    // normal (no fire, the odd lapse), 2 hard (fire, no lapses).
    enum Skill { easy = 0, normal = 1, hard = 2 };
    int crowd_skill{normal};
    // Research: the crowd's driving steers marble 0 too (tools).
    bool bot_drives_player{};
    // A crowd marble (not a player's).
    bool is_bot(const Marble& m) const { return &m >= &marbles[std::size_t(players_)] && &m < marbles.data() + max_marbles; }
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

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(x, y, z, screen, kind); }
    };
    const std::array<DropMark, 2>& drop_marks() const { return drop_marks_; }

    // The drawing order of the moving things (nearest last).
    const DisplayList& display() const { return display_; }
    Regions& regions() { return regions_; }
    // The sounds asked for since the last call (the course start's music
    // first).
    std::vector<SoundEvent> take_sounds() { return std::exchange(sounds_, {}); }

    // $1005EA: 0 a race, 1 the attract demo (a stub in this release), 3
    // the race is over (all finished), 4 out of time; others between races.
    int game_mode{};

    // Not the original game (pause menu cheats).
    struct Cheats {
        bool stop_timer{};  // the race clock stands still
        bool safe_falls{};  // drops onto the course never break the marble

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(stop_timer, safe_falls); }
    };
    Cheats cheats;

    // Colour values the game has written into the Track's copper lists
    // (instruction address -> colour), over the stored ones.
    const std::map<std::uint32_t, std::uint16_t>& copper_colours() const { return copper_colours_; }
    // Ultimate's colour cycling ($1006A7: a count, -1 off; $1006A6: the
    // phase), for checks against recordings.
    int colour_count() const { return colour_count_; }
    int colour_phase() const { return colour_phase_; }

    // The view (for the picture): the screen's offset in course space and
    // the scroll in lines from the top.
    std::int32_t view() const { return view_; }
    std::int16_t scroll_lines() const { return scroll_; }
    int frame() const { return frame_; }
    // $100248 counts world updates from the game's start, not the race's:
    // a race continues the count (the scroll looks every eighth update).
    void set_frame(int frame) { frame_ = frame; }

private:
    friend class MechanicsChapter; // Isolated teaching scenarios and read-only drawing inspection.
    void scroll();                                 // $10D618
    bool scroll_down(int lines);                   // $10D278
    bool scroll_up(int lines);                     // $10D384
    int band(std::int32_t view) const;             // $10D9BC
    void count_down();
    void dizzy_surprise();                         // $10AAD0                             // $10B7E6

    // Marble rules (marble_rules.cpp).
    void update_marble(Marble&);                   // $114402
    void bands_changed(int from, int to);          // $10D9BC
    DisplayList::Box box_of(std::uint8_t type, std::uint8_t index) const; // $110CD4
    int frame_box_byte(std::uint32_t frame, int k) const;
    void show(std::uint8_t type, std::uint8_t index);   // $112004
    void hide(std::uint8_t type, std::uint8_t index);   // $1120F4
    void take_region_display_events();
    // The steelies (steelies.cpp).
    void spawn_steelies(int from, int to);         // $119E5A
    void update_steelie(Steelie&);                 // $119C7C
    void steer(Steelie&);                          // $11365A
    void think(Steelie&);                          // $11A378
    void choose_mode(Steelie&, bool at_waypoint);  // $11A49A
    void nearest_waypoint(Steelie&);               // $11A250
    void return_to_path(Steelie&);                 // $11A7AE
    void remove(Steelie&, bool bonus, bool gone);  // $11A09C
    Steelie* steelie_of(Marble&);
    // The slinkies (slinkies.cpp).
    void spawn_slinkies(int from, int to);         // $11ABF8
    void update_slinky(Slinky&);                   // $11A81E
    void slinky_step(Slinky&);                     // $11B904
    void slinky_animation(Slinky&);                // $11B2E0
    void slinky_lookout(Slinky&);                  // $11B5F6
    void slinky_nearest_waypoint(Slinky&);         // $11AE1E
    void slinky_place(Slinky&);                    // $11B248
    bool touch_slinkies(Marble&);                  // $11AF24
    // The acid (acid.cpp).
    void spawn_acid(int from, int to);             // $11BE98
    void start_acid(Acid&, std::uint32_t path, std::uint32_t entry); // $11BE98's set-up
    void update_acid(Acid&);                       // $11BD58
    void acid_next(Acid&);                         // $11C54C
    void acid_heading(Acid&);                      // $11C5BA
    void acid_place(Acid&);                        // $11C48E
    bool touch_acid(Marble&);                      // $11C1C0
    void pull_into_acid(Marble&);                  // $11C34C
    // Aerial's pistons (pistons.cpp).
    void switch_pistons();                           // $11DFDE
    void update_pistons();                           // $11DB4C
    bool touch_pistons(Marble&);                     // $11E1E0
    void take_region_sounds();
    // Silly's critters and birds (silly.cpp).
    std::uint32_t silly_list(std::uint32_t entry) const;
    void silly_bands(int from, int to);            // $10DAAC
    void spawn_critters();                         // $11C78C
    bool critter_blocked(const Critter&) const;    // $11CBCA
    void update_critters();                        // $11C8DA
    void critter_choose(Critter&);                 // $11CD5E
    void critter_animation(Critter&);              // $11CDFC
    void critter_wander(Critter&);                 // $11D002
    void critter_walk(Critter&);                   // $11CF30
    void critter_flee(Critter&);                   // $11D182
    void critter_dodge(Critter&);                  // $11D08E
    void critter_step(Critter&);                   // $11D3D2
    void critter_place(Critter&);                  // $11D42E
    void touch_critters(Marble&);                  // $11CA44
    void spawn_birds();                            // $11D506
    void update_birds();                           // $11D6B0
    void bird_place(Bird&);                        // $11D9CC
    void touch_birds(Marble&);                     // $11D8EA
    bool collide(Marble&);                         // $1169C4
    void roll(Marble&);                            // $115470
    void push(Marble&);                            // $1134C4
    void cap_speed(Marble&);                       // $1153E6
    void friction(Marble&, int kind);              // $115088
    void friction(std::int32_t& vx, std::int32_t& vy, int kind);
    void slope_force(Marble&);                     // $114FF0
    void keep_on_ground(Marble&, std::int32_t ground); // $118BF4
    void push_off_walls(Marble&);                  // $119328
    void bounce_off_edges(Marble&);                // $10EF4C
    void land(Marble&, int impact);                // $114DEC
    void set_mode(Marble&, int mode);              // $114C6E
    void respawn(Marble&);                         // $1149F6
    void choose_respawn_point(Marble&);            // $112CA4
    bool appear(Marble&);                          // $1138D4
    bool into_vacuum(Marble&);                     // $113C2C
    int occupied(const Marble*, int x, int y, int radius) const; // $116578
    void track_zones(Marble&);                     // $1130BE
    void save_grounded(Marble&);                   // $1141A0
    void animate(Marble&);                         // $1141E0
    void project(Marble&, bool reload);            // $10EADC
    void touch_regions(Marble&);                   // $116EE0
    bool funnel_walls(Marble&, const Region&, int part); // $1186A4
    void zone_changed(Marble&);                    // $112F20
    void reach_goal(Marble&, std::int8_t zone_before);
    void award(Marble&, int bonus);                // $10A82A
    void colour_goal(int player);                  // $108710
    void cycle_colours();                          // $108664
    void drop_bonus(Marble&);                      // $11573A
    void project_drop_marks();                     // $11DAAC
    void project(DropMark&);                       // $106A00
    void reset_shine(Marble&);                     // $115D68
    void rotate_shine(Marble&);                    // $115DFA
    void check_visible(Marble&);                   // $11F486
    void build_covers(Marble&);                    // $11EC42
    struct CoverArea {
        std::int32_t x, y, z;          // the point (16.16)
        std::int16_t left, top, bottom; // the sprite on the screen
        int centre_shift;              // added to the sprite's centre column
        bool lists;                    // use the Track's hand-placed covers

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(x, y, z, left, top, bottom, centre_shift, lists); }
    };
    unsigned find_covers(const CoverArea&, int& columns, std::array<Marble::Cover, 5>& out);
    // A racer's marble, not a steelie's body (the crowd's marbles race by
    // the players' rules).
    bool is_player(const Marble& m) const { return &m >= marbles.data() && &m < marbles.data() + max_marbles; }
    // The crowd (crowd.cpp).
    void spawn_bot(int index);
    void steer_bot(Marble&);
    void plan(Marble&);
    void carry_off_screen(Marble&);
    void careful_near_vacuums(const Marble&, Push&) const;
    void keep_the_pack_close(const Marble&, Push&) const;
    void within_skill(const Marble&, Push&) const;
    Push usual_push(Marble&);
    CrowdDriver new_driver(int index) const;


    std::int32_t ground_at(std::int32_t x, std::int32_t y);
    void locate(const Marble& m) { probe_.locate(terrain_, m.x, m.y, m.z); }
    // $1224D8: on the side of the point last looked up in the height map.
    void sound(int id) { sound(id, probe_.x() < probe_.y()); }
    void sound(int id, bool right) { // $1222E4
        sounds_.push_back({id, right, sound_source_});
    }
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
    std::array<Push, max_marbles> push_{};
    int marble_count_{1};
    bool crowd_{};
    int sound_source_{-1}; // the crowd marble being updated (its sounds are its own)
    bool planning_{}; // a copy the crowd tries pushes on
    std::shared_ptr<const CrowdMap> crowd_map_; // read-only: shared by copies
    std::array<CrowdDriver, max_marbles> drivers_{};
    std::vector<int> home_order_; // the crowd race: marbles home, in order
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
    // The start offsets the vacuum and bumper tests leave on the stack
    // ($116EE0 locals; code 13 reads code 11's or 12's last ones).
    std::int16_t vacuum_start_dx_{}, vacuum_start_dy_{};
    bool birds_on_{};              // $1007A7
    bool pistons_on_{};              // $1007A6
    std::uint32_t piston_pattern_{}; // $1007A8
    std::vector<Regions::Blit> piston_blits_;
    std::int16_t zone_sound_timer_{-1}; // $1007AC: updates to sound 6
    std::uint8_t zone_sounds_{};   // $1007AE: bits of the once-a-race zone sounds
    bool surprise_armed_{};        // $1007AF
    std::uint8_t surprise_roll_{}; // $10068E
    std::int8_t first_home_{-1};   // $1005FC: the player who reached the goal first
    std::uint8_t score_changed_{}; // $1005F4: bit per player
    std::map<std::uint32_t, std::uint16_t> copper_colours_;
    std::int8_t colour_count_{-1};
    std::int8_t colour_phase_{};
    std::array<DropMark, 2> drop_marks_{};
    DisplayList display_;
    std::uint8_t drop_bonus_taken_{}; // $10060E: bit per player (player + 1), once a game
    int frame_{};                  // $100248: world updates so far
    bool first_update_{true};      // the count goes 0, 2, 3, ... (measured)
    bool scrolling_{};             // $100242
    std::int8_t scroll_direction_{}; // $100240: 1 down the course, -1 up
    std::int8_t scroll_speed_{};   // $100243
    std::int16_t scroll_goal_{};   // $100244

    // Track and marble data addresses.
    std::uint32_t descriptor_{};   // header +0
    std::uint32_t respawn_points_{}; // header +4: x, y (cells), zone; $FF ends
    std::uint32_t zones_{};        // header +8: zone boundary lines
    std::uint32_t marble_data_{};  // marbdat's table

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) {
        ar(marbles, steelies, slinkies, acids, pistons, piston_groups, critters, birds, random, crowd_close,
           crowd_skill, bot_drives_player, game_mode, cheats, course_, players_, terrain_, regions_, probe_,
           push_, marble_count_, crowd_, sound_source_, planning_, drivers_, home_order_, sounds_, start_x_,
           start_y_, start_z_, start_cell_x_, start_cell_y_, bounce_x_, bounce_y_, respawn_x_, respawn_y_,
           respawn_feature_, view_, first_view_, scroll_, scroll_limit_, vacuum_start_dx_, vacuum_start_dy_,
           birds_on_, pistons_on_, piston_pattern_, piston_blits_, zone_sound_timer_, zone_sounds_,
           surprise_armed_, surprise_roll_, first_home_, score_changed_, copper_colours_, colour_count_,
           colour_phase_, drop_marks_, display_, drop_bonus_taken_, frame_, first_update_, scrolling_,
           scroll_direction_, scroll_speed_, scroll_goal_, descriptor_, respawn_points_, zones_,
           marble_data_);
        // This route map was built when the crowd joined. Rebuilding it
        // from today's animated terrain can give the bots a different route.
        bool has_map = bool(crowd_map_);
        ar(has_map);
        if constexpr (A::reading) {
            crowd_map_.reset();
            if (has_map) {
                auto map = std::make_shared<CrowdMap>(*files_, course_, terrain_);
                ar(*map);
                crowd_map_ = std::move(map);
            }
        } else if (has_map) {
            auto map = *crowd_map_;
            ar(map);
        }
    }
};

} // namespace marble_madness::game
