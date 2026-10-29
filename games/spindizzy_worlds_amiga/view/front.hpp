#pragma once
#include "spindizzy_worlds_amiga/data/assets.hpp"
#include "spindizzy_worlds_amiga/view/screen.hpp"
#include <array>
#include <cstdint>
#include <vector>

// The front end's pictures: the attract pages' blocks and sprites, the
// world map's cluster of planets and the Easy/Hard thumbnails of it.
namespace spindizzy::view {

// $17242: a block type's picture (its pieces' tiles OR'ed in, no masks)
// with its top-left corner at a buffer byte offset (row × 40 + byte); a
// lowered block (attribute bit 1) one tile row down; unless the word's
// bit 15 is set, a base row below from the type & $C0.
void draw_block_picture(Canvas&, const data::Assets&, int offset, std::uint16_t word);

// The attract pages' animated sprites (object kinds −10 … −13, created
// from 12-byte entries by $179F2, stepped by $17A1E): copied opaquely
// into the shown buffer at a byte offset, 16 ($178E8) or 32 ($178CA)
// pixels wide, the frame offset from a list at $17A5E.
struct AttractSprite {
    bool wide{};          // 32 pixels
    int rows{};           // +$2C + 1
    std::uint32_t frames{}; // +$1A
    std::uint32_t list{};   // $17A5E + +$1E
    int offset{};         // +8
    int countdown{};      // +$A
    int period{};         // +$C
    int entry{};          // +$2E / 2
    template <class A> void persist(A& ar) { ar(wide, rows, frames, list, offset, countdown, period, entry); }
};
std::vector<AttractSprite> read_attract_sprites(const data::Memory& main, std::uint32_t address);
// One object tick ($17A1E): when its countdown passes zero it shows its
// next frame.
void step_attract_sprite(Canvas&, const data::Memory& main, AttractSprite&);

// A planet of a cluster list ($119A6 reads x, y, z, picture, world).
struct Planet {
    std::int16_t x{}, y{}, z{};
    int picture{};  // 0-3 ($7D0 each at SELECT.SWP + $1404); −1 the "?" one ($3344)
    int world{};
    template <class A> void persist(A& ar) { ar(x, y, z, picture, world); }
};
std::vector<Planet> read_cluster(const data::Memory& main, std::uint32_t list);

// The world map's turning cluster ($16704, $16756, $168BE): three angles
// over the sine table $164EC, the joystick turning two of them with
// damped speeds.
class ClusterView {
public:
    // The three angles ($166EC, $166F0, $166F4): never reset, so the map
    // keeps its turn from one visit to the next (only the speeds are
    // cleared, $1169A).
    struct Turn {
        std::int32_t angle[3]{};
        template <class A> void persist(A& ar) { ar(angle); }
    };
    Turn turn() const { return {{angle_[0], angle_[1], angle_[2]}}; }
    void set_turn(const Turn& t) { for (int i = 0; i < 3; ++i) angle_[i] = t.angle[i]; }

    void steer(std::uint8_t joystick); // $168BE
    // One frame's planets ($11686): turned and projected, sorted far to
    // near; the nearest is the chosen one ($1445A), whose picture counter
    // moves ($16A50). Returns its index, or −1. Without `choose` (the map
    // after a world, $11838) nothing is chosen. `prefer` (the "All worlds
    // open" cheat): that planet is chosen instead when none is more than
    // `slack` nearer than it.
    int update(const data::Memory& main, const std::vector<Planet>&, const std::vector<bool>& shown, bool choose = true,
               int prefer = -1, int slack = 0);

    // $16A86: the planet at `index` (the world just done, kind −7) breaks
    // up: $16ADE steps its frames ($10058 over the list $16B38) `steps`
    // times a frame, its picture still drawn under the first four; once
    // the list's last entry ($FF ticks) is reached $16AA6 removes it.
    void break_up(int index) { breaking_ = index; break_ticks_ = 4; break_entry_ = 0; break_gone_ = false; }
    void step_break_up(const data::Memory& main, int steps, bool slow);

    template <class A> void persist(A& ar) {
        ar(placed_, chosen_, angle_, speed_, spin_, breaking_, break_ticks_, break_entry_, break_offset_, break_under_,
           break_gone_, break_removed_);
    }
    // Draws the box, the shadows and the planets of the last update.
    void draw(Canvas&, const data::Assets&, const data::Memory& main, const std::vector<Planet>&) const;

private:
    struct Placed {
        int index{};
        std::int16_t x{}, y{}, z{};
        std::uint16_t sort{};
        template <class A> void persist(A& ar) { ar(index, x, y, z, sort); }
    };
    std::vector<Placed> placed_;
    int chosen_{-1};
    std::int32_t angle_[3]{};   // $166EC, $166F0, $166F4 (16.16, index = whole part & $FF)
    std::int32_t speed_[2]{};   // $166F8, $166FC
    std::vector<int> spin_;     // +$1E of each planet
    int breaking_{-1};          // the kind −7 planet
    int break_ticks_{}, break_entry_{}; // its +$1E and +$1F
    std::uint32_t break_offset_{};      // the frame $10058 returned
    bool break_under_{}, break_gone_{}, break_removed_{};
};

// $16B62: picture n of the break-up series (SELECT.SWP + $35C0 + n × $280,
// 32 rows) at (x, y).
void draw_break_frame(Canvas&, const data::Assets&, int n, int x, int y);

// $11D2E: a 2/3-size copy of the map's 192 × 192 box (from buffer row 4,
// byte 8) into another canvas at a byte offset: 48 pixels become 32
// ($11D88 keeps two of every three), every third row is left out.
void shrink_cluster(Canvas& to, const Canvas& from, int offset);

} // namespace spindizzy::view
