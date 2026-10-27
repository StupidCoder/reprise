#pragma once
#include <array>
#include <cstdint>
#include <functional>

// The game's display list ($10060F, $100436): the moving things on the
// screen in the order they are drawn, the nearest last. Each entry is a
// slot (14 bytes: type, index, a box in course space); the list holds slot
// numbers. An object joins where its box first sorts before an entry
// ($112004), leaves when it goes ($1120F4), and once per world update every
// box is refreshed and the list gets three partial bubble passes (gaps 1,
// 2, 3; $111BB4), so the order follows the objects over a few updates.
//
// Types: 1 a player's marble, 2 a steelie, 4 a slinky, $2A a drop marker,
// regions by their terrain code (5 the goal flags, ...), 0 a free slot.
// Region keyframes add an entry each time (a region of code 0 adds one
// whose slot reads as free at once), as the original does.
namespace marble_madness::game {

class DisplayList {
public:
    static constexpr int capacity = 31;
    // Room for the crowd race's six more marbles (not the original game).
    static constexpr int max_capacity = capacity + 9;
    void set_limit(int limit) { limit_ = limit; }

    struct Box {
        std::int16_t x0{}, y0{}, z0{}, x1{}, y1{}, z1{};
    };
    struct Slot {
        std::uint8_t type{}, index{};
        // A slot's box is refreshed only by the sort; a new entry is placed
        // by comparing with the boxes the slots hold, so entries added in
        // the same update compare with what their slots held before (a
        // free slot's box, all $7FFF, at first).
        Box box{0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF};
    };
    using BoxOf = std::function<Box(std::uint8_t type, std::uint8_t index)>;

    DisplayList() { order_.fill(0xFF); }

    void add(std::uint8_t type, std::uint8_t index, const BoxOf& box_of); // $112004
    void remove(std::uint8_t type, std::uint8_t index);                  // $1120F4
    void sort(const BoxOf& box_of);                                      // $111BB4's first part

    // The drawing order: slot numbers, $FF ends.
    const std::array<std::uint8_t, max_capacity + 1>& order() const { return order_; }
    const Slot& slot(int n) const { return slots_[std::size_t(n)]; }

    // $1113DC: a belongs after (in front of) b.
    static bool in_front(const Box& a, const Box& b);

private:
    std::array<Slot, max_capacity> slots_{};
    std::array<std::uint8_t, max_capacity + 1> order_{};
    int limit_{capacity};
};

} // namespace marble_madness::game
