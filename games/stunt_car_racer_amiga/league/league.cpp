#include "stunt_car_racer_amiga/league/league.hpp"
#include <algorithm>

namespace stuntcar::league {
namespace {

// Ratings: a base by driver (120 for driver 0 down to 10 for the player),
// plus 0-63 drawn every round ($5AA84).
constexpr std::uint32_t base_rating_table = 0x5AAF4;
constexpr std::uint8_t rating_spread = 0x3F;
// The first place of each division, Division 4 first ($5AB42).
constexpr std::uint32_t first_slot_table = 0x5AB42;
// The season's pairs: places within the division ($5ABC8, $5ABCE).
constexpr std::uint32_t pair_first_table = 0x5ABC8, pair_second_table = 0x5ABCE;
// The two tracks of each division, by race parity ($648C2).
constexpr std::uint32_t track_table = 0x648C2;
// The fastest lap goes to the second of the pair unless a draw of $A0 or
// more hands it to the first, before the winner is settled ($5ABD4).
constexpr std::uint8_t first_fastest_draw = 0xA0;
// The division a place belongs to ($64AC6), by place.
constexpr std::uint32_t division_by_slot_table = 0x64AC6;
constexpr int fresh_roll_bar = 10;

} // namespace

League::League(const data::Program& program) {
    for (int d = 0; d < drivers; ++d) base_ratings_[std::size_t(d)] = program.u8(base_rating_table + std::uint32_t(d));
    for (int k = 0; k < divisions; ++k) first_slots_[std::size_t(k)] = program.u8(first_slot_table + std::uint32_t(k));
    for (int r = 0; r < races_per_season; ++r) {
        pair_first_[std::size_t(r)] = program.u8(pair_first_table + std::uint32_t(r));
        pair_second_[std::size_t(r)] = program.u8(pair_second_table + std::uint32_t(r));
    }
    for (int k = 0; k < divisions * 2; ++k) tracks_[std::size_t(k)] = program.u8(track_table + std::uint32_t(k));
    for (int k = 0; k < drivers; ++k)
        division_by_slot_[std::size_t(k)] = program.u8(division_by_slot_table + std::uint32_t(k));
    for (int d = 0; d < drivers; ++d) slots_[std::size_t(d)] = order_[std::size_t(d)] = d;
}

void League::new_game(race::Random& random) {
    for (int d = drivers - 1; d >= 0; --d) {
        slots_[std::size_t(d)] = d;
        random.next(); // the set-up stirs the generator once a driver
    }
    super_league_ = false;
    player_division_ = 0;
    new_season();
    marks_ = fresh_roll_bar;
}

void League::new_season() {
    race_ = 0;
    raced_.fill(0), wins_.fill(0), laps_.fill(0);
    for (int d = 0; d < drivers; ++d) order_[std::size_t(d)] = d;
}

int League::first_slot(int division) const { return first_slots_[std::size_t(division)]; }

std::array<int, 2> League::pair(int division) const {
    const int first = first_slot(division);
    return {slots_[std::size_t(first + pair_first_[std::size_t(race_)])],
            slots_[std::size_t(first + pair_second_[std::size_t(race_)])]};
}

int League::track(int division) const { return tracks_[std::size_t(division * 2 + (race_ & 1))]; }

bool League::player_races() const {
    const auto p = pair(player_division_);
    return p[0] == player || p[1] == player;
}

int League::opponent() const {
    const auto p = pair(player_division_);
    return p[0] == player ? p[1] : p[0];
}

Outcome League::play_round(race::Random& random, std::optional<PlayerResult> result) {
    for (int d = drivers - 1; d >= 0; --d)
        ratings_[std::size_t(d)] = std::uint8_t((random.next() & rating_spread) + base_ratings_[std::size_t(d)]);
    Outcome mine;
    for (int division = 0; division < divisions; ++division) {
        const auto [first, second] = pair(division);
        // Bit 7: the first of the pair wins; bit 6: the first has the
        // fastest lap. Computer drivers: the higher rating wins (a tie is
        // a coin toss).
        std::uint8_t flags = random.next() >= first_fastest_draw ? 0x40 : 0;
        ++raced_[std::size_t(first)], ++raced_[std::size_t(second)];
        const auto player_flags = [&] {
            const auto r = result.value_or(PlayerResult{true, true});
            return std::uint8_t((r.lost ? 0x80 : 0) | (r.opponent_fastest ? 0x40 : 0));
        };
        if (second == player) {
            flags = player_flags();
        } else if (first == player) {
            flags = player_flags() ^ 0xC0;
        } else {
            const auto a = ratings_[std::size_t(first)], b = ratings_[std::size_t(second)];
            const bool keep = a < b || (a == b && (random.next() & 1));
            if (!keep) flags ^= 0xC0;
        }
        const Outcome outcome{(flags & 0x80) ? first : second, (flags & 0x40) ? first : second};
        ++wins_[std::size_t(outcome.winner)];
        ++laps_[std::size_t(outcome.fastest)];
        if (division == player_division_) mine = outcome;
        sort(division, random);
    }
    return mine;
}

bool League::next_race() {
    ++race_;
    return race_ < races_per_season;
}

// The standings: places in slot order, then bubbled by points, then wins,
// a tie broken by a draw for each comparison; passes repeat until none
// swaps.
void League::sort(int division, race::Random& random) {
    const int first = first_slot(division), end = first + per_division;
    for (int k = first; k < end; ++k) order_[std::size_t(k)] = slots_[std::size_t(k)];
    for (bool swapped = true; swapped;) {
        swapped = false;
        for (int k = first; k + 1 < end; ++k) {
            auto& a = order_[std::size_t(k)];
            auto& b = order_[std::size_t(k + 1)];
            bool swap = points(a) < points(b);
            if (!swap && points(a) == points(b)) {
                swap = wins(a) < wins(b);
                if (!swap && wins(a) == wins(b)) swap = random.next() & 1;
            }
            if (swap) std::swap(a, b), swapped = true;
        }
    }
}

std::array<int, per_division> League::standings(int division) const {
    const int first = first_slot(division);
    return {order_[std::size_t(first)], order_[std::size_t(first + 1)], order_[std::size_t(first + 2)]};
}

League::Changes League::end_season() {
    Changes changes;
    slots_ = order_;
    const int first = first_slot(player_division_);
    const int last = first + per_division - 1;
    if (first == 0 && slots_[0] == player) {
        if (super_league_) changes.league_won = true;
        else changes.promoted = player, changes.to_super_league = true;
    } else {
        if (first != 0) changes.promoted = slots_[std::size_t(first)];
        if (last != drivers - 1) changes.relegated = slots_[std::size_t(last)];
    }
    // Each division's winner changes places with the last of the one above.
    for (int division = divisions - 2; division >= 0; --division) {
        const int top = first_slot(division);
        std::swap(slots_[std::size_t(top)], slots_[std::size_t(top - 1)]);
    }
    if (order_[0] == player) {
        if (!super_league_) {
            super_league_ = true;
            for (int d = 0; d < drivers; ++d) slots_[std::size_t(d)] = d;
            player_division_ = 0;
        }
        marks_ = fresh_roll_bar;
    } else {
        const auto at = std::find(slots_.begin(), slots_.end(), player) - slots_.begin();
        player_division_ = division_by_slot_[std::size_t(at)];
        if (player_division_ == 0) marks_ = fresh_roll_bar;
    }
    new_season();
    return changes;
}

void League::set_for_tools(const ToolState& s) {
    slots_ = s.slots, order_ = s.order, raced_ = s.raced, wins_ = s.wins, laps_ = s.laps, ratings_ = s.ratings;
    race_ = s.race, player_division_ = s.player_division, super_league_ = s.super_league;
}

League::ToolState League::state_for_tools() const {
    return {slots_, order_, raced_, wins_, laps_, ratings_, race_, player_division_, super_league_};
}

} // namespace stuntcar::league
