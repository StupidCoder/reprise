#pragma once
#include "fa_18_interceptor_amiga/game/world_objects.hpp"
#include <cstdint>
#include <functional>
#include <vector>

// The trigger areas (game.md "The trigger areas"): boxes of cells in the
// program (`$11DF00`) whose entries are spawned when the player flies into
// one (`$11D17A`, every 16th frame), spawned again while the player stays
// (records gone come back), and sent back to their places when the player
// leaves. An aircraft out of the area that spawned it turns for home
// (`$11D60C`).
namespace interceptor::game {

class TriggerAreas {
public:
    explicit TriggerAreas(const ObjectLists& lists) : lists_(lists) {}

    // $10D37A: none entered at a flight's start.
    void reset() { inside_ = 0; }

    // `$11D17A` for the player's cell (record 0's +6 / +8). `spawn` is
    // `$11D2FA` for one area and the walk's bit number; it returns what
    // the original leaves in that register.
    using Spawn = std::function<int(const ObjectLists::TriggerArea&, int index)>;
    void check(std::int16_t cell_x, std::int16_t cell_z, std::vector<WorldObject>& objects, const Spawn& spawn);

    // `$11D60C`, a record's turn in its step: an aircraft (not in role 8)
    // spawned by an area and now out of it heads for its entry's place.
    void send_home(WorldObject& o) const;

    std::uint8_t inside() const { return inside_; }
    void set_inside(std::uint8_t bits) { inside_ = bits; }

private:
    void leave(const ObjectLists::TriggerArea& area, std::vector<WorldObject>& objects) const;

    const ObjectLists& lists_;
    std::uint8_t inside_{}; // `$13AA8D`: a bit per area entered (by the walk's bit number)
public:
    template <class A> void persist(A& ar) { ar(inside_); }
};

} // namespace interceptor::game
