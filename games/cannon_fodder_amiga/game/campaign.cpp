#include "cannon_fodder_amiga/game/campaign.hpp"
#include <algorithm>
#include <sstream>
#include <utility>

namespace fodder::game {

void Campaign::start(const MissionTables& tables) {
    *this = Campaign{};
    for (auto& r : roster) r = Recruit{}; // $89E48
    for (auto& f : fame) f = FameEntry{}; // $96DE2
    phase = 0;
    missions_started = 0;
    phases_left = 1;
    before_phase(tables);
}

void Campaign::before_phase(const MissionTables& tables) {
    ++phase_in_mission;
    if (--phases_left != 0) return;
    for (auto& r : roster) r.served = 0;
    phases_left = phases_in_mission = tables.mission_phases(missions_started);
    phase_in_mission = 0;
    ++missions_started;
    pool_at_mission = pool;
    new_recruits = true;
    heroes_this_mission = heroes.size();
    pool += 15;
}

bool Campaign::next_phase(const MissionTables& tables) {
    if (phase >= MissionTables::phases - 1) return false;
    ++phase;
    before_phase(tables);
    return true;
}

void Campaign::call_up(int placed) {
    // $9E19A
    returning = 0;
    for (const auto& r : roster) returning += r.name != -1;
    joining = placed - returning;
    if (joining >= 0) {
        joining = std::min(joining, pool);
        pool -= joining;
    }
    soldiers = returning + joining;
    // $89E7E: empty places out, then the same ranks by kills.
    std::stable_partition(roster.begin(), roster.end(), [](const Recruit& r) { return r.name != -1; });
    for (auto& r : roster) {
        if (r.name == -1) r = Recruit{};
    }
    for (std::size_t a = 0; a < roster.size(); ++a) {
        for (std::size_t b = 0; b < roster.size(); ++b) {
            if (a == b || roster[a].name == -1 || roster[b].name == -1) continue;
            if (roster[a].rank == roster[b].rank && roster[b].kills < roster[a].kills) std::swap(roster[a], roster[b]);
        }
    }
    // $89FAE for each new one: the next name, a rank by the mission.
    for (int n = 0; n < joining; ++n) {
        auto place = std::find_if(roster.begin(), roster.end(), [](const Recruit& r) { return r.name == -1; });
        if (place == roster.end() || next_name >= 0x168) break;
        *place = Recruit{};
        place->name = next_name++;
        place->rank = (missions_started - 1) / 3;
        place->weapon = 3;
    }
    // $9E21C: the first `soldiers` places go on the map.
    for (std::size_t k = 0; k < roster.size(); ++k) roster[k].on_map = int(k) < soldiers && roster[k].name != -1;
}

void Campaign::phase_won() {
    for (auto& r : roster) {
        if (r.on_map) ++r.served;
    }
}

void Campaign::fallen(int place) {
    auto& r = roster[std::size_t(place)];
    if (r.name == -1) return;
    heroes.push_back({r.name, r.rank});
    record_fame(r);
    r = Recruit{};
    ++home;
}

void Campaign::promote() {
    // (The original writes the list of new ranks of the recruits on the map
    // back to the roster's taken places in order.)
    std::vector<int> ranks;
    for (const auto& r : roster) {
        if (r.name != -1 && r.on_map) ranks.push_back(std::min(r.rank + r.served, 15));
    }
    std::size_t next = 0;
    for (auto& r : roster) {
        if (r.name == -1) continue;
        if (next >= ranks.size()) break;
        r.rank = ranks[next++];
    }
}

void Campaign::record_fame(const Recruit& r) {
    if (r.kills == 0) return;
    int at = 5;
    while (at > 0) {
        const auto& e = fame[std::size_t(at - 1)];
        if (r.kills < e.kills || (r.kills == e.kills && r.rank <= e.rank)) break;
        --at;
    }
    if (at >= 5) return;
    for (int i = 4; i > at; --i) fame[std::size_t(i)] = fame[std::size_t(i - 1)];
    fame[std::size_t(at)] = {r.rank, r.name, r.kills};
}

int Campaign::alive() const {
    int n = 0;
    for (const auto& r : roster) n += r.name != -1;
    return n;
}

std::string Campaign::write() const {
    std::ostringstream out;
    out << "cannon-fodder-campaign 1\n";
    out << phase << ' ' << missions_started << ' ' << phase_in_mission << ' ' << phases_left << ' ' << phases_in_mission << ' '
        << pool << ' ' << pool_at_mission << ' ' << next_name << ' ' << new_recruits << ' ' << home << ' ' << away << ' '
        << heroes_this_mission << '\n';
    for (const auto& r : roster)
        out << r.name << ' ' << r.rank << ' ' << r.served << ' ' << r.on_map << ' ' << r.weapon << ' ' << r.marked << ' ' << r.kills << '\n';
    for (const auto& f : fame) out << f.rank << ' ' << f.name << ' ' << f.kills << '\n';
    out << heroes.size() << '\n';
    for (const auto& h : heroes) out << h.name << ' ' << h.rank << '\n';
    return out.str();
}

bool Campaign::read(const std::string& text) {
    std::istringstream in(text);
    std::string tag;
    int version = 0;
    if (!(in >> tag >> version) || tag != "cannon-fodder-campaign" || version != 1) return false;
    Campaign c;
    in >> c.phase >> c.missions_started >> c.phase_in_mission >> c.phases_left >> c.phases_in_mission >> c.pool >>
        c.pool_at_mission >> c.next_name >> c.new_recruits >> c.home >> c.away >> c.heroes_this_mission;
    for (auto& r : c.roster) in >> r.name >> r.rank >> r.served >> r.on_map >> r.weapon >> r.marked >> r.kills;
    for (auto& f : c.fame) in >> f.rank >> f.name >> f.kills;
    std::size_t n = 0;
    in >> n;
    for (std::size_t i = 0; i < n && in; ++i) {
        Hero h;
        in >> h.name >> h.rank;
        c.heroes.push_back(h);
    }
    if (!in) return false;
    *this = c;
    return true;
}

} // namespace fodder::game

