#pragma once
#include "cannon_fodder_amiga/game/tables.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// The game beyond a phase (.analysis/cannon_fodder_amiga/game.md "Campaign
// state"): the missions and phases, the recruits on the roster, the pool
// of recruits still to come, the fallen ("heroes"), the score and the hall
// of fame. The original keeps all of it in one block of memory ($80626,
// 1832 bytes) that a saved game copies; here it is this struct.
namespace fodder::game {

// One place on the roster ($80664, 12 bytes each).
struct Recruit {
    int name{-1};      // +0: index into the recruits' names ($917E0), -1 empty
    int rank{};        // +2: 0-15
    int served{};      // +3: phases won this mission
    bool on_map{};     // +4: linked to a soldier in this phase
    int weapon{3};     // +6
    bool marked{};     // +9 bit 0: picked on the panel
    int kills{};       // +$A
};

struct Hero {          // a fallen recruit ($80A16 list)
    int name{}, rank{};
};

struct FameEntry {     // $80CF8: the five best
    int rank{}, name{-1}, kills{};
};

struct Campaign {
    int phase{};               // $80626: 0-71
    int missions_started{};    // $80632: the mission number (1-24) once started
    int phase_in_mission{};    // $80634: 0-based
    int phases_left{1};        // $80658: phases of the mission still to play
    int phases_in_mission{};   // $8065A
    std::array<Recruit, 8> roster{};
    int pool{};                // $80638: recruits still to come
    int pool_at_mission{};     // $80652
    int next_name{};           // $8065E
    bool new_recruits{};       // $80654: the hill shows new recruits arriving
    std::vector<Hero> heroes;          // all the fallen, in order ($80A16…)
    std::size_t heroes_this_mission{}; // where the current mission's fallen start ($80CE8)
    std::array<FameEntry, 5> fame{};   // $80CF8
    int home{}, away{};        // $80D1C recruits killed, $80D1E enemies killed
    // This phase ($9E19A): new recruits wanted and placed soldiers.
    int joining{};             // $80D48
    int returning{};           // $80D4A
    int soldiers{};            // $80D4C

    // $85E62: a new game.
    void start(const MissionTables& tables);
    // $871AE, before every phase: a phase gone; a new mission when none is left.
    void before_phase(const MissionTables& tables);
    // $85ECA: after a won phase the next one (false after the last).
    bool next_phase(const MissionTables& tables);
    // $9E19A + $89E7E + $89F12: the recruits for `placed` soldiers: who comes
    // back, who joins (from the pool), the roster compacted and sorted. The
    // roster places linked to soldiers this phase are the first `soldiers`.
    void call_up(int placed);
    // $8725E: a won phase counts for every recruit on the map.
    void phase_won();
    // A recruit fell (`$96CAA`): to the heroes, his place cleared, HOME +1.
    void fallen(int place);
    // $A8E10/$A8E6A: the mission's end: each recruit on the map is promoted
    // by the phases he served (at most rank 15).
    void promote();
    // $96DF6: a recruit leaving (fallen or at the end) may enter the hall of
    // fame.
    void record_fame(const Recruit& r);
    bool game_over() const { return pool == 0 && alive() == 0; }
    // A saved game ($8A01C copies the block; here a text of numbers).
    std::string write() const;
    bool read(const std::string& text);
    int alive() const;
};

} // namespace fodder::game
