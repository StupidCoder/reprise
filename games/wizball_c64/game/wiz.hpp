#pragma once
#include "wizball_c64/data/assets.hpp"
#include "wizball_c64/game/playfield.hpp"
#include <array>
#include <cstdint>

// The wiz: a bouncing, spinning ball. Without the thrust power its stick
// only spins it, and each bounce rolls it along at a speed set by the spin;
// thrust lets the stick drive it; antigravity replaces the bounce by
// direct up/down control. ($7DF4 and the routines it calls.)
namespace wizball::game {

// The stick as the game reads it ($B23F): bits low while pressed.
struct Stick {
    std::uint8_t bits{0xFF}; // bit 0 up, 1 down, 2 left, 3 right, 4 fire
    bool up() const { return !(bits & 1); }
    bool down() const { return !(bits & 2); }
    bool left() const { return !(bits & 4); }
    bool right() const { return !(bits & 8); }
    bool fire() const { return !(bits & 16); }

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) { ar(bits); }
};

struct Powers {
    bool thrust{};      // $B231
    bool antigravity{}; // $B232
    bool beam{};        // $B233
    bool catellite{};   // $B234
    bool spray{};       // $B235
    bool doubled{};     // $B236
    bool blazers{};     // $B237
    bool blazers_on_catellite{}; // $B238: the catellite fires them, not the wiz
    bool smart_bomb{};  // $B239 (hypothesis)

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) {
        ar(thrust, antigravity, beam, catellite, spray, doubled, blazers, blazers_on_catellite, smart_bomb);
    }
};

class Wiz {
public:
    explicit Wiz(const data::GameImage& image);

    void reset_for_life();   // $7CDA (the wiz's part)
    void start_level();      // $8040 (the wiz's part)
    void set_start_x() { x_ = 0xAC; }

    // One pass of movement ($7F3D, $7F62, $80F8, $7FEA, $8097) and, once it
    // has appeared, its collisions with the landscape ($836E).
    void move(const Stick& stick, const Powers& powers, Playfield& playfield, bool bonus, bool no_landscape = false);
    // $818E: the scroll speed index from the horizontal speed.
    void update_speed_index();
    // Tunnels: $81C9 stops the wiz and starts it sinking for 21 passes;
    // $8216 counts a pass down (true when it is through: $8239 follows);
    // $8239 puts it at the new place's start; the sequence ends with the
    // spin cleared.
    void enter_tunnel() {
        tunnel_ = 0x15;
        speed_index_ = 0;
        vx_ = 0;
    }
    bool sink();
    void arrive() {
        y_ = 0x4C;
        x_ = 0xAC;
        vy_ = vy_fraction_ = 0;
        vy_direct_ = 0;
    }
    void end_arrival() { spin_ = 0; }
    // $646F: rings open out, the wiz goes. $6537: rings close in again.
    void start_vanish() {
        appear_ = 2;
        appear_frame_ = 0;
        vx_ = 0;
        vy_direct_ = 0;
    }
    void start_appear() {
        appear_ = 1;
        appear_frame_ = 0x17;
    }
    void stop_horizontal() { vx_ = 0; }
    // $AC59: placed for the lab's walk; $ADCD: out of it.
    void enter_lab() {
        y_ = 0x3D;
        vy_ = 0;
        vx_ = 0;
    }
    void leave_lab() {
        spin_ = 0;
        vx_ = 0;
        vy_ = vy_fraction_ = 0;
    }
    void record_trail(); // $857B

    // State.
    int x() const { return x_; }                 // $B20D/$B20E (9 bits)
    std::uint8_t y() const { return y_; }        // $B20F
    std::uint8_t frame() const { return frame_; } // sprite number ($B525)
    std::int8_t speed_index() const { return speed_index_; } // $B1F4
    void set_speed_index(std::int8_t v) { speed_index_ = v; }
    bool appearing() const { return appear_ != 0; }
    bool behind_landscape() const { return behind_; } // $E7DF
    std::int8_t horizontal() const { return vx_; }    // $B21C
    std::int8_t last_horizontal() const { return last_vx_; } // $B21D
    std::uint8_t tunnel() const { return tunnel_; }    // $B213
    int place_change() const { return place_change_; } // set by a tunnel this pass: -1, 0, +1
    bool bumped() const { return bumped_; } // ran into a solid cell this pass

    struct Probe {
        std::uint8_t y_fraction, vy, vy_fraction, falling, vy_direct, spin, spin_fraction, vx, last_vx;
        std::uint8_t appear, appear_frame, appear_clock, trail_index;

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) {
            ar(y_fraction, vy, vy_fraction, falling, vy_direct, spin, spin_fraction, vx, last_vx, appear,
               appear_frame, appear_clock, trail_index);
        }
    };
    Probe probe() const;
    const std::array<std::uint8_t, 8>& trail_y() const { return trail_y_; }
    const std::array<std::uint8_t, 8>& trail_x() const { return trail_x_; }

private:
    void vertical(const Powers& powers, bool bonus);
    void horizontal_input(const Stick& stick, const Powers& powers, bool bonus);
    void appear_step();
    void clamp();
    void landscape(Playfield& playfield, bool bonus, const Powers& powers);
    void fall_step();   // $7F99
    void rise_step();   // $7FD0
    void direct_vertical(const Stick& stick); // $8BEA

    int x_{0xAC};
    std::uint8_t y_{0x49}, y_fraction_{};
    std::uint8_t vy_{}, vy_fraction_{}; // $B211.$B212
    bool falling_{};                     // $B217
    std::int8_t vy_direct_{};            // $B218
    std::uint8_t spin_{}, spin_fraction_{}; // $B21A.$B21B
    std::int8_t vx_{}, last_vx_{};       // $B21C, $B21D
    std::uint8_t appear_{}, appear_frame_{}, appear_clock_{}; // $B216, $B214, $B215
    std::uint8_t tunnel_{};              // $B213
    std::uint8_t gravity_{};             // $B27C
    std::uint8_t direct_clock_{};        // $B219
    std::int8_t speed_index_{};
    std::uint8_t frame_{};
    bool behind_{};
    int place_change_{};
    bool bumped_{};
    std::uint8_t trail_index_{};
    std::array<std::uint8_t, 8> trail_y_{}, trail_x_{};
    std::array<std::uint8_t, 8> probe_offsets_{};
    std::array<std::array<std::uint16_t, 2>, 8> probe_handlers_{};
    Stick stick_{};

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) {
        ar(x_, y_, y_fraction_, vy_, vy_fraction_, falling_, vy_direct_, spin_, spin_fraction_, vx_, last_vx_,
           appear_, appear_frame_, appear_clock_, tunnel_, gravity_, direct_clock_, speed_index_, frame_,
           behind_, place_change_, bumped_, trail_index_, trail_y_, trail_x_, probe_offsets_, probe_handlers_,
           stick_);
    }
};

} // namespace wizball::game
