#pragma once
#include "tower_of_babel_amiga/data/assets.hpp"
#include "tower_of_babel_amiga/game/animation.hpp"
#include "tower_of_babel_amiga/game/tower_state.hpp"
#include <vector>

// The tower's rules, frame by frame: how spiders and other objects move,
// ride lifts and run their programs (the object logic $BC7A and the action
// routines $B44C-$B66E), and what each kind of object does on its own (the
// per-frame update $BEE4: enemies firing, walkers deciding, mines, bombs,
// timers, and the frame each object shows).
namespace babel::game {

class Rules {
public:
    // Tables from the program: claim points per type ($CB28), pushed-move
    // steps ($CB3C), lift steps ($CB4C).
    explicit Rules(const data::Memory& program);

    // One frame of $BC7A: spiders decide (program) and move, other objects
    // move once the game has started, the forcers work.
    void step(TowerState&) const;
    // Then each object's own behaviour and frame ($BEE4), from slot 4 on.
    void update(TowerState&) const;

    // The spider actions the panel and programs use. Each returns false
    // where the original returns -1 (not possible now).
    bool walk(TowerState&, int slot, int direction) const; // $B510 (direction 0-3, or 8/10 a lift ride)
    bool back(TowerState&, int slot) const;                 // $B4B4
    void turn_left(TowerState&, int slot) const;            // $B64E
    void turn_right(TowerState&, int slot) const;           // $B65E
    bool fire(TowerState&, int slot) const;                 // $B66E: zap, push or grab

    void step_forcers(TowerState&) const; // $BCE4

private:
    // The objects' own behaviour (game/behaviour.cpp).
    void spider(TowerState&, int slot) const;   // $BF6E: footsteps, the reversed-ride frame
    void watcher(TowerState&, int slot) const;  // $C1FC: follows the wall on its left
    void hopper(TowerState&, int slot) const;   // $C382: turns right where it can
    void lizard(TowerState&, int slot) const;   // $C66E: eats the floor where it turned
    void wiper(TowerState&, int slot) const;    // $C868: four sweeps that take the floor
    void blast(TowerState&, int slot, bool hole) const; // mines and time bombs
    bool free_ahead(const TowerState&, int slot, int direction) const; // $C34A
    void show(TowerObject&, const TowerObject& before, const TowerState&) const;

    void decide(TowerState&, int slot) const;   // $CB6C
    void advance(TowerState&, int slot) const;  // $C97E
    bool reverse(TowerState&, int slot) const;  // $B5E2
    bool zap(TowerState&, int direction, CellIndex from, int range, int shooter) const;  // $B684
    bool push(TowerState&, int direction, CellIndex from, int range, int shooter) const; // $B83C
    bool grab(TowerState&, int slot) const;          // $B90E
    bool raise_lifts(TowerState&) const;             // $BAAA
    bool lower_lifts(TowerState&) const;             // $BB42
    int claim_point(int type) const { return std::int8_t(program_.u8(0xCB28 + std::uint32_t(type))); }
    int pushed_step(int progress) const { return std::int8_t(program_.u8(0xCB3C + std::uint32_t(progress - 1))); }
    static constexpr int move_frames = 32; // a move to the next cell takes 32 frames
    int lift_step(int progress) const { return std::int8_t(program_.u8(0xCB4C + std::uint32_t(progress - 1))); }

    const data::Memory& program_;
    Animation animation_;
};

} // namespace babel::game
