#pragma once
#include "app/game_module.hpp"
#include "stunt_car_racer_amiga/data/front_end.hpp"
#include "stunt_car_racer_amiga/league/league.hpp"
#include "stunt_car_racer_amiga/league/records.hpp"
#include "stunt_car_racer_amiga/race/opponent.hpp"
#include "stunt_car_racer_amiga/race/random.hpp"
#include "stunt_car_racer_amiga/render/front_end_screen.hpp"
#include <functional>
#include <optional>
#include <string>
#include <vector>

// The front end (game.md, "Front end"): the SELECT menu, the name, the
// divisions, the division's menu (Hall of Fame, Practise, the racing
// season, Load/Save/Replay), the track picture before a race, and the
// league's screens around each race. The race itself is the module's; the
// front end asks for one and is told how it went.
namespace stuntcar::game {

// A race the front end wants driven.
struct RaceRequest {
    int track{};
    std::optional<race::Driver> opponent; // a league race
    bool super_league{};
    int roll_bar_marks = 10;
};

// How a league race went for the player.
struct RaceResult {
    bool won{}, fastest_lap{}, wrecked{}, quit{};
    int roll_bar_marks = 10;
    std::vector<race::Time> laps; // the player's completed laps
    bool finished{};              // all laps done
};

class FrontEnd {
  public:
    // `draw_track` draws a track picture for (track, view 0-3) into the
    // canvas's view window.
    using TrackPicture = std::function<void(render::FrontEndCanvas&, int track, int view)>;
    FrontEnd(const data::Program&, const data::FrontEndArt&, race::Random&, TrackPicture);

    // A 50 Hz tick of the menus.
    void tick(const app::GameInput&);
    platform::Image picture() const;

    // A race to drive, once (the front end then waits for race_over).
    std::optional<RaceRequest> take_race();
    void race_over(const RaceResult&);

  private:
    enum class Screen {
        select, name, divisions, division_menu, practise_divisions, practise_tracks, load_save,
        hall_of_fame, track_picture, racing, fixture, scene, result, standings, changes, promotion,
    };
    struct Menu {
        int base{};          // the first entry in the menu table ($5BCD0)
        int count{};
        int chosen{};
        bool main{};         // texts from the main table (the SELECT screen)
        int taken = -1;      // ticks the chosen bar stays brown
    };

    void show(Screen);
    void open_menu(int base, int count, bool main, int chosen = 0);
    void menu_tick(const app::GameInput&);
    void menu_chosen(int item);
    void name_tick(const app::GameInput&);
    void next_league_race();
    void after_season();

    void draw_menu(render::FrontEndCanvas&) const;
    void draw_panel_title(render::FrontEndCanvas&) const;
    void draw_divisions(render::FrontEndCanvas&) const;
    void draw_name(render::FrontEndCanvas&) const;
    void draw_race_heading(render::FrontEndCanvas&, int row) const;
    void draw_fixture(render::FrontEndCanvas&) const;
    void draw_result(render::FrontEndCanvas&) const;
    void draw_standings(render::FrontEndCanvas&) const;
    void draw_changes(render::FrontEndCanvas&) const;
    void draw_track_picture(render::FrontEndCanvas&) const;
    void draw_hall(render::FrontEndCanvas&) const;
    void print_time(render::FrontEndCanvas&, const std::optional<race::Time>&) const;
    void print_holder(render::FrontEndCanvas&, const league::Record&) const;
    std::string name(int driver) const;

    const data::Program& program_;
    const data::FrontEndArt& art_;
    race::Random& random_;
    TrackPicture track_picture_;
    league::League league_;
    league::Records records_;
    std::string player_name_;

    Screen screen_ = Screen::select;
    Menu menu_;
    int practise_division_{};
    int division_choice_ = 1; // the menu shows the last choice again ($1BBD6)
    int picture_track_{}, picture_view_{};
    bool picture_league_{};
    std::optional<RaceRequest> pending_race_;
    league::Outcome last_outcome_{};
    league::League::Changes changes_;
    data::Scene scene_{};

    // Input edges and the joystick's repeat.
    bool fire_was_{}, up_was_{}, down_was_{}, left_was_{}, right_was_{};
    int repeat_{};
    std::array<bool, 64> keys_was_{};
    bool space_was_{};
};

} // namespace stuntcar::game
