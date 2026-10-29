#pragma once
#include "sonic1_gg/game/body.hpp"

namespace sonic1_gg {

struct Game;
struct Level;

// Sonic: his body, animation, power-ups and damage state, and his behaviour.
//
// In the cartridge Sonic is not one of the 32 object slots. He has his own RAM
// block, updates after all objects (Game::tick), is never switched off by the
// activation scan, and every object contact is tested against him. His code
// therefore lives here rather than among the objects. It reads and changes the
// rest of the world (rings, sounds, the camera, terrain), which it reaches
// through the Game passed to each call.
//
// RAM addresses are those of the original variables.
struct Sonic : Body {
    // --- Animation ---
    int animation{5};          // D411: animation sequence number
    int animation_step{};      // D410: position within the sequence
    int previous_animation{5}; // D2E0: restarts the sequence when it changes
    int animation_fraction{};  // D2E1: speed-dependent animation accumulator
    int frame{};               // Selected sprite frame of the current sequence

    // --- Movement ---
    int jump_hold{};          // D288: updates left in which a held jump keeps rising
    int idle_ticks{};         // D294: 360 idle updates start the waiting animation
    int previous_flags{};     // Flags at the end of the last update (springs fire on landing)
    int previous_gravity{56}; // D2E7: gravity added last update (platforms cancel it)
    int skid_ticks{};         // D413: minimum interval between skid sounds

    // --- Power-ups ---
    int shoes_ticks{};      // Speed shoes: remaining updates / 4
    int invincible_ticks{}; // Invincibility: remaining updates / 4
    bool shield{};
    std::array<int, 2> star_phase{0x18, 0}; // D2F4/D2F5: invincibility star orbit positions

    // --- Damage and death ---
    bool hurt{};            // Knocked back; controls are ignored until landing
    bool lost_ring_guard{}; // The lost-ring animation is running: no further damage
    bool dead{};
    bool drowned{};
    int death_ticks{};   // Countdown of the death sequence (decremented every 2nd update)
    bool life_debited{}; // The life has been subtracted (once Sonic falls off screen)

    // --- Underwater (Labyrinth) ---
    int air_ticks{};       // Updates spent underwater (the countdown starts at 768)
    int air_digit{-1};     // Countdown number to draw this update (-1: none)
    int breathing_ticks{}; // Breathing pose after touching an air bubble
    int current_ticks{};   // A water current is carrying Sonic

    // One update of Sonic's own logic (4B63-4FFB): scripted states, running,
    // rolling, jumping, gravity, terrain actions and rings. move() follows.
    void update(Game&, Input input);
    // Moves by the current velocity and resolves terrain collision (2CD4).
    void move(const Game&);
    // The death sequence: jump up, then fall through everything.
    void update_dead(Game&);

    // A hit by an enemy or hazard (2FD9): lose the shield or the rings, or die.
    void take_hit(Game&);
    // Death by any cause (2FF4).
    void die(Game&);

    // The terrain action of the block under Sonic's centre (terrain.cpp).
    void terrain_actions(Game&);
    // Collects a ring from the block under Sonic, if any.
    void collect_rings(Game&);

    // Selects the sprite frame for the current animation (549E, 4F99).
    void animate(const Level&);

private:
    void roll(Game&, Input input, int braking);
    void finish_update(Game&);
    void update_stars(Game&);
};

} // namespace sonic1_gg
