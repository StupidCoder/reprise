#pragma once
#include "bruce_lee_c64/data/assets.hpp"
#include "bruce_lee_c64/game/playfield.hpp"
#include "bruce_lee_c64/game/terrain.hpp"
#include <array>
#include <cstdint>
#include <vector>
#include <string_view>

// The actors: Bruce, the Yamo and the Ninja. Each pass an actor either waits
// out the delay of its last move or decides what to do from its stick, the
// terrain it touches and the move in progress ($1C6D), then picks the pose
// to show ($276D). Coordinates: x in pixel pairs (13-163), y in lines.
namespace bruce_lee::game {

// Move codes. The values are the original's (they index its delay table);
// the names say what each move was seen to do (hypotheses where marked).
namespace move {
inline constexpr std::uint8_t stand = 0x00;
inline constexpr std::uint8_t climb_up = 0x01;
inline constexpr std::uint8_t climb_down = 0x02;
inline constexpr std::uint8_t walk_left = 0x03;
inline constexpr std::uint8_t walk_right = 0x04;
inline constexpr std::uint8_t jump = 0x05;
inline constexpr std::uint8_t jump_left = 0x06;
inline constexpr std::uint8_t jump_right = 0x07;
inline constexpr std::uint8_t kick_left = 0x0C;
inline constexpr std::uint8_t kick_right = 0x0D;
inline constexpr std::uint8_t fall = 0x0E;
inline constexpr std::uint8_t pushed_back = 0x0F; // hypothesis: knocked away from its facing
inline constexpr std::uint8_t turn_to_strike = 0x10;
inline constexpr std::uint8_t chop = 0x11;        // fire with the stick at rest (hypothesis: punch/chop)
inline constexpr std::uint8_t strike = 0x12;      // the enemies' strike
inline constexpr std::uint8_t thrown = 0x13;      // hypothesis: hit and thrown back
inline constexpr std::uint8_t enemy_think = 0x14;
inline constexpr std::uint8_t enemy_walk = 0x15;
inline constexpr std::uint8_t low_strike = 0x16;  // hypothesis
inline constexpr std::uint8_t ride = 0x17;        // hypothesis: on a moving thing
inline constexpr std::uint8_t bump = 0x18;
inline constexpr std::uint8_t dying = 0x19;
inline constexpr std::uint8_t duck = 0x1A;        // down on the floor (hypothesis: crouch-roll)
} // namespace move

// Stick values as the C64 reads them (a 0 bit is pressed).
namespace stick {
inline constexpr std::uint8_t none = 0x0F, up = 0x0E, down = 0x0D, left = 0x0B, right = 0x07;
inline constexpr std::uint8_t up_left = 0x0A, up_right = 0x06, down_left = 0x09, down_right = 0x05;
} // namespace stick

// Terrain flags (TerrainContact bits).
namespace terrain {
inline constexpr std::uint8_t ground = 0x01;  // standing on something (with 4: any support)
inline constexpr std::uint8_t ladder = 0x04;  // hypothesis: climbable
inline constexpr std::uint8_t solid = 0x08;   // a wall or obstacle ahead: the move is undone
} // namespace terrain

struct Actor {
    std::uint8_t flags{};            // bit 0 present; bits 6-7 the character kind and who controls it
    std::uint8_t stick{stick::none};
    std::uint8_t fire{1};            // 0 = pressed
    std::uint8_t x{}, y{}, facing{}; // facing 1 = right
    TerrainContact terrain;
    std::uint8_t move{}, last_move{}, repeat{}, step{}, delay{};
    std::uint8_t fall_steps{};       // $B0: passes of sideways drift left while falling
    std::uint8_t climb_lock{};       // $C8: bit 0 stops climbing up
    std::uint8_t ride_flag{}, ride_wait{}; // $AA, $AD (move ride)
    std::uint8_t posed{};            // $4FE8: the pose was chosen this pass
    // $4F64 + 3j: entries 0-3 the steps moved this pass (up, down, left,
    // right), then the same for earlier passes, newest first.
    std::array<std::uint8_t, 44> steps{};
    std::uint8_t frame{}, previous_frame{}, pose{}; // $4F55, $4F61, $4F5B
    std::array<bool, 2> sprite_wide{};             // $D4, $D7 (from the last shown poses)
    std::uint8_t d1{};               // $D1: energy (an actor whose energy goes negative is out)
    std::uint8_t carried_x{}, carried_y{}, carried_energy{}; // $8A, $8C, $8E: following Bruce to the next room
    std::pair<std::uint8_t, std::uint8_t> sprite_codes{};   // $EC, $EF

    bool present() const { return flags & 1; }
    int kind() const { return flags >> 6; } // $2998: 0 Bruce, 1 Ninja, 2 Yamo
};

// What the actors' rules read besides the actors themselves.
struct ActorWorld {
    const data::Assets& assets;
    std::uint8_t clock{};        // $88: counts up while a room is played (hypothesis)
    bool flag40{};               // $40 (hypothesis: difficulty)
    bool two_players{};          // $42B7: the second player controls the Yamo
    std::uint8_t lift{}, lift_down{}; // $60, $65 (move ride)
    std::vector<std::uint8_t> sounds; // effects requested this pass ($24F6)
    bool death_finished{};       // Bruce's dying move ended ($1830): the flow takes over
    bool restart_position{};     // a bump with no steps to undo ($24C8): back to the room's start (flow)
};

class Actors {
public:
    std::array<Actor, 3> slots{};
    // Read-only teaching telemetry: the last grounded decision, not a new AI state.
    struct Decision {
        bool sampled{}, supported{}, level{}, facing{};
        int horizontal{}, vertical{};
        std::string_view outcome{"Waiting for a decision"};
    };
    std::array<Decision, 3> decisions{};

    // $1C6D for one actor.
    void update(int slot, ActorWorld&);
    // $276D for one actor.
    void choose_pose(int slot, const ActorWorld&);
    // $2D69: idle computer-controlled enemies start thinking.
    void wake_enemies(ActorWorld&);

private:
    // One step in a direction; when it cannot be taken the actor stands
    // instead ($1F9B). Each sets the move it stands for.
    void step_up(Actor&, ActorWorld&);
    void step_down(Actor&, ActorWorld&);
    void step_left(Actor&, ActorWorld&);
    void step_right(Actor&, ActorWorld&);
    void step_left_or_kick(Actor&, ActorWorld&);  // $1E82
    void step_right_or_kick(Actor&, ActorWorld&); // $1EC3
    void climb_or_jump(Actor&, ActorWorld&);      // $1DFE
    void stand(Actor&, ActorWorld&);               // $1F9B
    void start(Actor&, const ActorWorld&, std::uint8_t move, std::uint8_t step); // $1FA4 family
    void carry_on(Actor&, const ActorWorld&, std::uint8_t move, int step);       // $24E1
    void finish(Actor&, int slot, const ActorWorld&); // $2279
    void begin_strike(Actor&, ActorWorld&);         // $2231
    void run(Actor&, int slot, ActorWorld&);        // $1C7A
    bool undo_and_bump(Actor&, ActorWorld&);        // $1CE6
    void enemy_down(Actor&, int slot, ActorWorld&); // $1F0C
    void decide(Actor&, int slot, ActorWorld&);     // $1D90
    void timed(Actor&, int slot, ActorWorld&);      // $1FF5
    void think(Actor&, int slot, ActorWorld&, int step); // $22E7
    void start_fall(Actor&, ActorWorld&);           // $1DD6
    void walk_on(Actor&, ActorWorld&);              // $1EC0 / $1E7F
    struct Distance {
        std::uint8_t y{}, x{}; // $62, $63
    };
    Distance distance_to_bruce(const Actor&) const; // $2DCA

    int budget_{8}; // $86: rounds of undoing left in this actor's pass
};

} // namespace bruce_lee::game
