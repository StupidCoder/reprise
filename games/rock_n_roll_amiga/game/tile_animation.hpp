#pragma once
#include "rock_n_roll_amiga/data/assets.hpp"
#include <array>
#include <cstdint>
#include <vector>

// Which picture each cell value shows, and the animations that change it.
// The original keeps a table of bank offsets per cell value ($72CBE, built
// by $70954 at each level's set-up) that the view drawer blits from; the
// animations ($6C106, $6C004, each update before the mouse is read) point
// some of its entries at other pictures. Since the railing test reads the
// drawn view, an animated cell's frame also decides where the ball can
// pass: doors and arcs open and close this way.
namespace rocknroll::game {

class TileTable {
public:
    // $70954: entry v (1-240) is sheet cell v - 1, 20 a row, 16 x 16:
    // offset row × 3200 + column × 2.
    TileTable();
    // The picture's top-left in the bank for cell value v (1-240).
    std::uint32_t offset(std::uint8_t v) const { return entries_[v]; }
    void set(std::uint8_t v, std::uint32_t offset) { entries_[v] = offset; }
    int x(std::uint8_t v) const { return int(entries_[v] % data::GraphicsBank::row_bytes) * 8; }
    int y(std::uint8_t v) const { return int(entries_[v] / data::GraphicsBank::row_bytes); }
    // The entry points at the abyss pattern, as $14's and $21's do (the
    // drawer re-points those each update, $6C972); a sinking block's
    // destination borrows it ($6B49E).
    void show_abyss(std::uint8_t v) { entries_[v] = abyss; }
    bool shows_abyss(std::uint8_t v) const { return entries_[v] == abyss; }

private:
    static constexpr std::uint32_t abyss = 0xFFFFFFFF;
    std::array<std::uint32_t, 241> entries_{};
};

// The pictures of a sliding block ($6B372): strips for horizontal and
// vertical moves ($6B194, $6B198), and per direction 1-4 eight frame
// offsets for the cell it leaves ($C1, table $6B1B2) and the one it enters
// ($C2, $6B252; a second set at +$80 for a block sinking, where $FE means
// "the abyss pattern").
struct SlidePictures {
    std::uint32_t across{}, along{};       // bank offsets: even directions (2, 4), odd (1, 3)
    std::array<std::array<std::uint32_t, 8>, 5> leaving{};
    std::array<std::array<std::array<std::uint32_t, 8>, 5>, 2> entering{}; // [sinking][direction][frame]
    explicit SlidePictures(const data::Program&);
    SlidePictures() = default;
};

// One animation ($715D4 …): a picture strip, a counter that a step is
// added to (back to 0 at the period), and per 256 of the counter a frame
// (an offset into the strip). Its counter also times its cell's rule.
struct TileAnimation {
    std::uint32_t strip{};   // +0: bank address of the strip
    std::uint16_t period{};  // +4
    std::uint16_t counter{}; // +6
    std::uint8_t cell{};     // +8: the cell value whose picture it sets
    std::uint16_t step{};    // +$A
    std::vector<std::uint16_t> frames; // +$C: byte offsets into the strip
};

// The animations of $6C106: 18 slots, three advanced each update in turn
// (so each one every sixth update). The counters run on from level to
// level (the program never resets them); only the tile table is rebuilt.
class TileAnimator {
public:
    TileAnimator() = default;
    explicit TileAnimator(const data::Program&);

    // $6C0E0, at each level's set-up: slot 17 (cell $51's 2 x 2 picture)
    // runs except in world 6.
    void set_world(int world) { slot17_on_ = world != 6; world_ = world; }
    // $6C106 then $6C004: one update's steps into the table.
    void update(TileTable&);
    // Every slot's current frame into the table, as if each had just had
    // its turn (for starting from a recorded state mid-level; the game
    // itself starts each level from the plain table, $70954).
    void show_all(TileTable&) const;
    // The counter of the animation for a cell value (its rule reads it).
    std::uint16_t counter(std::uint8_t cell) const;
    void set_counter(std::uint8_t cell, std::uint16_t value);
    bool ready() const { return !slots_.empty(); }
    // The slots as the recorder logs them ($6C08A and each counter).
    int cursor() const { return cursor_; }
    void set_cursor(int cursor) { cursor_ = cursor; }
    std::uint16_t slot_counter(int slot) const { return slots_.at(std::size_t(slot)).counter; }
    void set_slot_counter(int slot, std::uint16_t value) { slots_.at(std::size_t(slot)).counter = value; }
    static constexpr int slots = 18;
    const SlidePictures& slide_pictures() const { return slide_pictures_; }
    // $6AC56: the magnets' pushes, 9 rows (view row) of 8 pairs (view
    // column / 2) of signed bytes x, y. (Kept with the other tables the
    // program reads at run time.)
    const std::array<std::int8_t, 144>& magnet_pushes() const { return magnet_pushes_; }

private:
    void show(const TileAnimation&, TileTable&) const;
    void follow(TileTable&) const;
    std::vector<TileAnimation> slots_; // 18
    SlidePictures slide_pictures_;
    std::array<std::int8_t, 144> magnet_pushes_{};
    int cursor_{};                     // $6C08A
    bool slot17_on_{true};
    int world_{};
};

} // namespace rocknroll::game
