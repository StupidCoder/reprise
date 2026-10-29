#pragma once

// Cheats (not the original game; all off is the original). Set from the
// pause menu; the stages read them.
namespace mindwalker {

struct Cheats {
    bool god{};            // landscape: no tethers; arena: no strength lost, no wall deaths
    bool minimap{};        // a small map in a corner (landscape: the way to the next beacon; arena: the maze)
    bool auto_transform{}; // landscape: the walker takes the form of each cell's surface on entering it

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) { ar(god, minimap, auto_transform); }
};

} // namespace mindwalker
