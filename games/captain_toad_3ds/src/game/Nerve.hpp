// Nerve keeper: the state machine of the original's actors and demos.
//
// The engine calls an actor's state a "nerve". Each nerve is a function that runs once per game
// tick with a step counter (0 on the first tick in that state, then 1, 2, ...). Nerves change state
// by "setting" the next nerve; most scripted behaviour in the opening (the companion's walk, the
// goal sequence, the stage-start demo) is a chain of nerves that wait for a step count or an
// animation end and then set the next one.
//
// Recovered from the keeper update 0x2c8700 and setNerve 0x2fd644:
//   - setNerve stores the next nerve (keeper +8) and step -1 (+0xc); it does not run it.
//   - update: a pending next nerve becomes current with step 0 (0x2c8740); the current nerve
//     executes with its step; the step increments; and a nerve set during the execute becomes
//     current with step 0 right after it (0x2c8794), but first executes on the next update.
// Consequence for timing: a nerve set from a message handler (outside the execute) runs its step 0
// in the same update, while a nerve set by an execute runs its step 0 on the following update.
#pragma once

namespace captain_toad::RecoveredNerve {

template <class Owner>
struct Keeper {
    // A nerve is a member function of the owning actor that receives the step counter.
    using Nerve = void (Owner::*)(int step);

    Nerve current = nullptr;
    Nerve next = nullptr;
    int step = 0;

    // 0x2fd644: request a state change (taken over at the next update boundary).
    void set(Nerve nerve) {
        next = nerve;
        step = -1;
    }

    // The 0x3eb4a8 pattern: "is the actor in (or about to enter) this nerve?"
    bool is(Nerve nerve) const { return (next ? next : current) == nerve; }

    // 0x2c8700: one tick of the state machine.
    void update(Owner& owner) {
        if (next) {
            current = next;
            next = nullptr;
            step = 0;
        }
        if (!current) {
            return;
        }
        (owner.*current)(step);
        ++step;
        if (next) {
            current = next;
            next = nullptr;
            step = 0;
        }
    }
};

}   // namespace captain_toad::RecoveredNerve
