#pragma once
#include "amiga/hunk.hpp"
#include "fa_18_interceptor_amiga/game/aircraft.hpp"
#include "fa_18_interceptor_amiga/game/autopilot.hpp"
#include <array>
#include <functional>
#include <cstdint>
#include <optional>
#include <vector>

// The player's weapons (game.md "Weapons"): the stores, the selection
// (Return), the gun's bursts, the missiles and the flares and chaff.
//
// - The stores ($10F184 at a new start): 2 Sidewinders, 4 AMRAAMs (the
//   two nibbles of +$5F), 500 rounds (+$60), 16 flares and 16 chaff
//   ($13AB3D, $13AB3C). Nothing is selected at the start.
// - The gun ($1072DC, every frame): while the trigger is held, one round a
//   frame into a free one of ten projectile slots, five to a burst, 3 off
//   the count each; a burst refills when no slot fired or waited for 20
//   frames. Rounds fly 20 frames, falling by $100 a frame; below height 0
//   they spark for two frames.
// - Missiles ($11764C, $1176D8, in the object step after the player): a
//   launch takes the next free of the three missile records, copied from
//   the aircraft, at the next loaded station; with no target ($13ACB0 -1,
//   as always in free flight) it flies on straight, its speed rising by
//   $420 a frame to $4200 and falling by $240 to $3000 over its last 50
//   frames; Sidewinders live 150 frames, AMRAAMs 200.
// - Flares and chaff ($10EE34, $10EEC6): one each a key, from the same
//   projectile slots as the gun's rounds (and its burst count, and only
//   while rounds are left, as in the original); flares burn 60 frames,
//   slowing by a quarter a frame, chaff hangs 30 frames, slowing by 1/64
//   and falling.
namespace interceptor::game {

class Messages;
class Sounds;

enum class Weapon : std::uint8_t { none, gun, sidewinder, amraam }; // +$63 high nibble 0, $10, $20, $30

struct Stores {
    Weapon selected = Weapon::none;
    int sidewinders = 2, amraams = 4; // +$5F high and low nibble
    int rounds = 500;                 // +$60
    int flares = 16, chaff = 16;      // $13AB3D, $13AB3C
};

// One of the ten projectile slots ($13AF62, $80 bytes apart): a round, a
// flare or a chaff cloud.
struct Shot {
    enum class Kind : std::uint8_t { round, flare, chaff };
    Kind kind = Kind::round;
    std::int32_t x{}, y{}, z{};    // world x 256 (+0, +4, +8)
    std::int32_t vx{}, vy{}, vz{}; // a frame's move (+$C, +$10, +$14)
    int life{};                    // frames left (+$28)
    bool moved{};                  // +$24: the first frame stands still
    bool hit{};                    // +$26 bit 1: sparks where it hit
    std::int32_t ex{}, ey{}, ez{}; // an eighth of the first move (+$18 .. +$20), for the hit tests
};

// A missile in flight: an object record of its own (records 1-3).
struct Missile {
    Aircraft body;   // the copied record: position, velocity, matrices
    int type{};      // +$62: 0 Sidewinder, 1 AMRAAM (the shape's type)
    int life{};      // +$4C
    // +0 bit 10: it blows up - its last 12 frames, or the 15 after it hit
    // the ground (+$4C then counts those); it touches nothing more and is
    // drawn as its explosion from 12 frames left (`$111F66`).
    bool fading{};
    bool fresh{};    // +0 bit 8: launched this frame (the object step skips its move)
    std::uint8_t pilot{}; // +$64
    Course course;   // where it heads for (the locked record's place)
    bool stopped{};    // +0 bit 12 cleared: it hit the ground and moves no more
};

class Weapons {
public:
    Weapons(const amiga::LoadedImage& program, const FlightTables& tables) : program_(program), tables_(tables) {}

    // $10F184 (a new start): full stores, nothing in flight.
    void reset();

    // Keys. Return ($10E90E): the next weapon (none, AMRAAM, Sidewinder,
    // gun, none), with the refusing beep the original gives every key here.
    void select(Sounds& sounds);
    // Space down / up ($10F0AA, $10F100).
    void trigger(bool down);
    // F ($10EE34) and C ($10EEC6).
    void flare(Messages& messages);
    void chaff(Messages& messages);

    // The object step's missile slots ($116C9E-$116D5C), after the
    // player's flight.
    // `locked`: the radar's locked record (`$13ACB0`) the missiles home on.
    void move_missiles(const Aircraft& player, const FlightRules& rules, Sounds& sounds, const Aircraft* locked = nullptr,
                       int locked_record = -1, const Autopilot* autopilot = nullptr);
    std::array<std::optional<Missile>, 3>& missiles() { return missiles_; }
    // The missiles whose time ran out in the last move (`$11A3F8`: they go
    // without asking for new scene lists), as they were last.
    struct Ended {
        int slot{};
        Missile missile;
    };
    const std::vector<Ended>& ended() const { return ended_; }
    // $1072DC: the projectile slots, after the flight.
    // `hits`: the records' test ($11AE96's object part) before a round's
    // move; true when it hit one.
    void move_shots(const Aircraft& player, bool outside, std::uint16_t frame, Sounds& sounds,
                    const std::function<bool(Shot&)>& hits = {});

    const Stores& stores() const { return stores_; }
    // The latest flare or chaff still out (`$13AB7C`, `$13AB7D`): the
    // decoys of the enemies' missiles on the player (32-bit place).
    std::optional<std::array<std::int32_t, 3>> decoy(Shot::Kind kind) const {
        const auto& at = kind == Shot::Kind::flare ? flare_slot_ : chaff_slot_;
        if (!at || !shots_[*at]) return std::nullopt;
        const auto& s = *shots_[*at];
        return std::array<std::int32_t, 3>{s.x, s.y, s.z};
    }
    const std::array<std::optional<Shot>, 10>& shots() const { return shots_; }
    const std::array<std::optional<Missile>, 3>& missiles() const { return missiles_; }

    // The count the readouts show for the selected weapon (rounds for the
    // gun; 0 without a weapon).
    int count() const;
    // Flares or chaff out: their 30 frames running (`$13AB3F`, `$13AB3E`).
    bool decoys_out() const { return flare_time_ > 0 || chaff_time_ > 0; }

    // The pieces, also for research: a missile of `weapon` launched from
    // `player` at the station for `left` missiles left after it; a
    // missile's frame (false: it is gone); a projectile leaving `player` on
    // frame `frame`; a projectile's frame.
    Missile launched(const Aircraft& player, Weapon weapon, int left) const;
    // $1176D8, the launch from any aircraft (the enemies' too): its record
    // copied, the station for `left` missiles left after this one.
    static Missile launch(const amiga::LoadedImage& program, const Aircraft& shooter, Weapon weapon, int left);
    // Where a missile not of the player's (state bit 3 clear) heads
    // (`$1182F2`): its target record's place, or on the player a decoy's -
    // the latest flare for a Sidewinder, chaff for an AMRAAM - whose
    // nearness (within $4800 on each axis, 32-bit units) it marks in +2 bit
    // 0 and whose loss in +$20 bit 5.
    struct Homing {
        Waypoint place;
        std::optional<std::array<std::int32_t, 3>> decoy;
    };
    static bool fly_missile(Missile& m, const FlightRules& rules, const FlightTables& tables, const Aircraft* locked,
                            int locked_record, const Autopilot* autopilot, const Homing* homing = nullptr);
    // $11B6A2's ground test after a missile's move (from `from`) when it
    // touched no record: below height 0 it goes back along the move onto
    // the ground (`$11AB0E`), looks whether that is water (`$11F83A`: +4
    // bit 1, which also colours its explosion) and blows up for 15 frames
    // ($11AA4A). True when it hit; `smoke` then
    // whether it leaves smoke (not on water), from `smoke_at`, its place
    // after the move ($11AB3E copies the record as `$11A86A` left it).
    static bool hit_ground(Missile& m, const std::array<std::int32_t, 3>& from, const data::World* world,
                           std::array<std::int32_t, 3>& smoke_at, bool& smoke);
    Shot fired(Shot::Kind kind, const Aircraft& player, std::uint16_t frame) const;
    static void fly_shot(Shot& s);

private:

    const amiga::LoadedImage& program_;
    const FlightTables& tables_;
    Stores stores_;
    bool firing_ = false;            // $13ABB6 bit 3: the gun's trigger
    bool launch_asked_ = false;      // $13AAAA
    int decoy_asked_ = 0;            // $13AB7B: 1 chaff, 2 flare
    int burst_left_ = 5;             // +$7D high nibble
    std::int8_t burst_hold_ = 0;     // +$3B (a byte: it wraps)
    int flare_time_ = 0, chaff_time_ = 0; // $13AB3F, $13AB3E (missiles' decoys)
    std::array<std::optional<Shot>, 10> shots_{};
    std::optional<std::size_t> flare_slot_, chaff_slot_; // `$13AB7C`, `$13AB7D`
    std::vector<Ended> ended_;
    std::array<std::optional<Missile>, 3> missiles_{};
};

} // namespace interceptor::game
