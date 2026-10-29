#include "wings_of_fury_amiga/game/saved_game.hpp"
#include <sstream>

namespace wings::game {
namespace {

constexpr const char* header = "Wings of Fury saved game 1";

// One list of the fields, for writing and for reading alike.
class Writer {
public:
    void operator()(long long v) { out_ << ' ' << v; }
    void text(std::string& s) { out_ << ' ' << (s.empty() ? "-" : s); }
    template <typename T> void list(std::vector<T>& v, auto&& each) {
        (*this)(static_cast<long long>(v.size()));
        for (auto& e : v) each(e);
    }
    bool reading() const { return false; }
    std::string str() const { return out_.str(); }

private:
    std::ostringstream out_;
};

class Reader {
public:
    explicit Reader(const std::string& s) : in_(s) {}
    long long get() {
        long long v = 0;
        if (!(in_ >> v)) good_ = false;
        return v;
    }
    void operator()(long long) { get(); } // (unused: fields go through field())
    void text(std::string& s) {
        std::string t;
        if (!(in_ >> t)) good_ = false;
        s = t == "-" ? std::string{} : t;
    }
    template <typename T> void list(std::vector<T>& v, auto&& each) {
        const long long n = get();
        if (n < 0 || n > 100000) {
            good_ = false;
            return;
        }
        v.assign(std::size_t(n), T{});
        for (auto& e : v) each(e);
    }
    bool reading() const { return true; }
    bool good() const { return good_; }

private:
    std::istringstream in_;
    bool good_ = true;
};

template <typename A, typename T> void field(A& a, T& v) {
    if constexpr (std::is_same_v<A, Reader>) v = static_cast<T>(a.get());
    else a(static_cast<long long>(v));
}

template <typename A> void transfer(A& a, Shelter& s) {
    field(a, s.dugout), field(a, s.offset), field(a, s.left_x), field(a, s.right_x), field(a, s.inside), field(a, s.island);
    field(a, s.leaving), field(a, s.leave_timer), field(a, s.gun_delay), field(a, s.refill);
}

template <typename A> void transfer(A& a, Islands& is) {
    a.list(is.huts, [&](Shelter& s) { transfer(a, s); });
    a.list(is.dugouts, [&](Shelter& s) { transfer(a, s); });
    a.list(is.pillboxes, [&](Pillbox& b) {
        field(a, b.offset), field(a, b.island), field(a, b.destroyed), field(a, b.smoke_left), field(a, b.smoke_timer);
    });
    a.list(is.soldiers, [&](Soldier& s) {
        field(a, s.x), field(a, s.direction), field(a, s.step), field(a, s.timer), field(a, s.island), field(a, s.state);
    });
    a.list(is.targets, [&](IslandTargets& t) {
        field(a, t.soldiers), field(a, t.pillboxes), field(a, t.first_dugout), field(a, t.last_dugout);
    });
    field(a, is.islands_left), field(a, is.ships_left), field(a, is.soldiers_killed);
}

template <typename A> void transfer(A& a, ShipState& s) {
    field(a, s.first), field(a, s.last), field(a, s.present), field(a, s.afloat), field(a, s.ready), field(a, s.deck);
    field(a, s.value), field(a, s.sunk), field(a, s.sink_timer), field(a, s.sink_delay), field(a, s.bob), field(a, s.overlay);
    a.list(s.guns, [&](ShipGun& g) {
        field(a, g.x), field(a, g.lift), field(a, g.destroyed), field(a, g.smoke_left), field(a, g.smoke_timer);
    });
    auto& p = s.planes;
    field(a, p.count), field(a, p.limit), field(a, p.low), field(a, p.high), field(a, p.shown_count);
    a.list(p.planes, [&](ShipPlanes::Plane& q) { field(a, q.flag), field(a, q.x), field(a, q.altitude), field(a, q.facing); });
}

template <typename A> void transfer(A& a, Enemies& e) {
    for (auto& o : e.planes) {
        field(a, o.state), field(a, o.mode), field(a, o.side), field(a, o.hit), field(a, o.oil), field(a, o.hit_timer);
        field(a, o.order), field(a, o.attack_timer), field(a, o.firing), field(a, o.facing), field(a, o.turn);
        field(a, o.turn_wait), field(a, o.loops), field(a, o.speed), field(a, o.target_speed), field(a, o.x);
        field(a, o.fall_speed), field(a, o.target_altitude), field(a, o.altitude), field(a, o.distance), field(a, o.flash);
        field(a, o.picture), field(a, o.turn_delay);
    }
    for (auto& f : e.airfields) {
        field(a, f.left), field(a, f.right), field(a, f.limit), field(a, f.parked), field(a, f.takeoff_x), field(a, f.speed);
        field(a, f.direction), field(a, f.shown_parked), field(a, f.shown_takeoff);
    }
    field(a, e.launch_cooldown), field(a, e.deck_roll), field(a, e.fighters_out), field(a, e.attacker_step);
    field(a, e.skid_counter), field(a, e.invulnerable);
    a.list(e.wrecks, [&](int& x) { field(a, x); });
}

template <typename A> void transfer(A& a, PlayerFlight& p) {
    field(a, p.altitude), field(a, p.x);
    a.text(p.shape);
    field(a, p.mode), field(a, p.fuel), field(a, p.spare), field(a, p.oil), field(a, p.facing), field(a, p.speed_x);
    field(a, p.speed_y), field(a, p.extra), field(a, p.enemy_timer), field(a, p.climb), field(a, p.speed), field(a, p.throttle);
    field(a, p.engine_loud), field(a, p.turn), field(a, p.smoothed_climb), field(a, p.climb_kick), field(a, p.pulling_up);
    field(a, p.lift), field(a, p.gear), field(a, p.signal), field(a, p.fresh_on_deck), field(a, p.wire_x), field(a, p.attitude);
    field(a, p.eighth_scale);
    a.text(p.wheels);
    field(a, p.wheel_attitude), field(a, p.deck_low), field(a, p.deck_high), field(a, p.oil_counter), field(a, p.fuel_counter);
    field(a, p.fuel_period), field(a, p.sea_step), field(a, p.sea_counter), field(a, p.sea_period), field(a, p.sea);
    field(a, p.turn_delay), field(a, p.sink_step), field(a, p.wreck_time), field(a, p.next_blast);
}

template <typename A> void transfer(A& a, FlightWorld& w, PlayerFlight& p) {
    field(a, w.rank), field(a, w.mission), field(a, w.score), field(a, w.spare_planes), field(a, w.kills), field(a, w.night);
    field(a, w.celebration), field(a, w.mission_accomplished), field(a, w.weapon), field(a, w.weapon_load);
    field(a, w.gravity), field(a, w.carrier_x), field(a, w.elevator), field(a, w.lift), field(a, w.menu), field(a, w.menu_repeat);
    field(a, w.input_delay), field(a, w.gun_supply), field(a, w.caught_wire_x);
    a.list(w.mission_map.words, [&](std::uint16_t& word) { field(a, word); });
    transfer(a, w.islands);
    for (auto& s : w.ships) transfer(a, s);
    transfer(a, w.enemies);
    transfer(a, p);
}

} // namespace

std::string save_game_text(const FlightWorld& world, const PlayerFlight& plane) {
    FlightWorld w = world;
    PlayerFlight p = plane;
    Writer out;
    std::string name = w.mission_map.name;
    out.text(name);
    transfer(out, w, p);
    return std::string(header) + "\n" + out.str() + "\n";
}

std::optional<SavedGame> load_game_text(const std::string& text, const std::vector<data::MissionMap>& maps) {
    const std::string first = std::string(header) + "\n";
    if (text.compare(0, first.size(), first) != 0) return std::nullopt;
    Reader in(text.substr(first.size()));
    std::string name;
    in.text(name);
    SavedGame g;
    const data::MissionMap* map = nullptr;
    for (const auto& m : maps) {
        if (m.name == name) map = &m;
    }
    if (map == nullptr) return std::nullopt;
    g.world.set_map(*map);
    const auto columns = map->words.size();
    transfer(in, g.world, g.plane);
    if (!in.good() || g.world.mission_map.words.size() != columns) return std::nullopt;
    return g;
}

} // namespace wings::game
