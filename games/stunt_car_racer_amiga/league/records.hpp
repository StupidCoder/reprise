#pragma once
#include "stunt_car_racer_amiga/race/race.hpp"
#include <array>
#include <optional>
#include <string>

// The track records the Hall of Fame shows and the fixture screen quotes
// (game.md, "Hall of Fame"): for each of the eight tracks the fastest lap
// and the fastest race, with who drove them, kept apart for the Super
// League. Only the player's laps count ($5F16C, called from the player's
// lap counting); a race counts once all its laps are done.
namespace stuntcar::league {

struct Record {
    std::string holder;            // 12 characters
    std::optional<race::Time> time;
};

class Records {
  public:
    static constexpr int tracks = 8;

    const Record& lap(bool super, int track) const { return laps_[super][std::size_t(track)]; }
    const Record& race(bool super, int track) const { return races_[super][std::size_t(track)]; }

    // A completed lap or race; true when it sets a new record.
    bool offer_lap(bool super, int track, const std::string& holder, race::Time);
    bool offer_race(bool super, int track, const std::string& holder, race::Time);

  private:
    std::array<std::array<Record, tracks>, 2> laps_{}, races_{};
};

// Times in BCD minutes, seconds and hundredths, added as the game adds
// them ($5EE04) and compared as it compares them ($5DC38).
race::Time add_times(race::Time, race::Time);
bool faster(race::Time, race::Time);

} // namespace stuntcar::league
