#pragma once
#include "stunt_car_racer_amiga/data/assets.hpp"
#include "stunt_car_racer_amiga/race/random.hpp"
#include <array>
#include <cstdint>
#include <optional>

// The single-player league (game.md, "The league"): twelve drivers in four
// divisions of three, each division racing a season of six races on its
// two tracks, every pair of its drivers twice. The player is driver 11 and
// starts in Division 4. Races without the player are decided by ratings
// rolled afresh each round; points are two for a win and one for the
// fastest lap. After the season each division's winner moves up and its
// last moves down; winning Division 1 leads to the Super League.
namespace stuntcar::league {

constexpr int drivers = 12, divisions = 4, per_division = 3, races_per_season = 6;
constexpr int player = 11;

// What one race decided.
struct Outcome {
    int winner{}, fastest{};
};

// The player's own race: lost, and whether the opponent had the fastest
// lap (a wreck or quitting counts as both).
struct PlayerResult {
    bool lost{}, opponent_fastest{};
};

class League {
  public:
    explicit League(const data::Program&);

    // A new game ($5A9B6): everyone back in their first division.
    void new_game(race::Random&);
    // A new season's records ($5AA38): nothing raced, standings by place.
    void new_season();

    // Divisions are counted from 0 (Division 4) to 3 (Division 1), as the
    // game does; the driver in each place of each division.
    int division_of_player() const { return player_division_; }
    int first_slot(int division) const;
    int driver_in(int slot) const { return slots_[std::size_t(slot)]; }
    bool super_league() const { return super_league_; }

    // The race number of the season (0-5), the pair racing it in a division
    // ($5AB46) and the track it is on ($648C2).
    int race() const { return race_; }
    std::array<int, 2> pair(int division) const;
    int track(int division) const;
    int track_of(int division, int which) const { return tracks_[std::size_t(division * 2 + which)]; }
    bool player_races() const;
    int opponent() const;

    // A round: every division's race of this number is decided ($5AA84);
    // the player's own by `result`. Returns the player division's outcome.
    Outcome play_round(race::Random&, std::optional<PlayerResult> result);
    // On to the next race; false once the season is over.
    bool next_race();

    // Standings ($5ACFE): a division's drivers by points, then wins.
    std::array<int, per_division> standings(int division) const;
    int raced(int driver) const { return raced_[std::size_t(driver)]; }
    int wins(int driver) const { return wins_[std::size_t(driver)]; }
    int fastest_laps(int driver) const { return laps_[std::size_t(driver)]; }
    int points(int driver) const { return laps_[std::size_t(driver)] + 2 * wins_[std::size_t(driver)]; }

    // The season's end ($64916): who went up, who went down in the player's
    // division (no one goes down from Division 4; from Division 1 only the
    // player goes up, into the Super League), then the moves themselves.
    struct Changes {
        std::optional<int> promoted, relegated;
        bool to_super_league{}, league_won{};
    };
    Changes end_season();

    // Research tools: the state as a capture of the original holds it.
    struct ToolState {
        std::array<int, drivers> slots, order, raced, wins, laps;
        std::array<std::uint8_t, drivers> ratings;
        int race, player_division;
        bool super_league;
    };
    void set_for_tools(const ToolState&);
    ToolState state_for_tools() const;

    // The car's roll-bar marks carry over from race to race ($1C9CF).
    int roll_bar_marks() const { return marks_; }
    void set_roll_bar_marks(int marks) { marks_ = marks; }

  private:
    void sort(int division, race::Random&);

    std::array<std::uint8_t, drivers> base_ratings_{};   // $5AAF4
    std::array<std::uint8_t, divisions> first_slots_{};  // $5AB42
    std::array<std::uint8_t, races_per_season> pair_first_{}, pair_second_{}; // $5ABC8, $5ABCE
    std::array<std::uint8_t, divisions * 2> tracks_{};   // $648C2
    std::array<std::uint8_t, drivers> division_by_slot_{}; // $64AC6

    std::array<int, drivers> slots_{};    // the driver in each place
    std::array<int, drivers> order_{};    // the standings, place by place
    std::array<std::uint8_t, drivers> ratings_{};
    std::array<int, drivers> raced_{}, wins_{}, laps_{};
    int race_{};
    int player_division_{};
    bool super_league_{};
    int marks_ = 10;
};

} // namespace stuntcar::league
