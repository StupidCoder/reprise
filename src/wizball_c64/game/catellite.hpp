#pragma once
#include "wizball_c64/game/random_source.hpp"
#include "wizball_c64/game/wiz.hpp"
#include <array>
#include <cstdint>

// The catellite ($8640): the wiz's companion sprite, sprite 1. With its
// stick idle it trails the wiz, eight passes behind, on the side the wiz
// last moved away from; any stick direction gives the stick ten passes of
// direct control, and the trailing pull then dies away by one a pass.
// $B26C counts its hits: at 9 it is lost. Catching one kind of droplet sets
// it wandering (random sticks, cycling colours) at 8, so one more hit ends
// it. It has its own shield. Collisions and shots live in Play.
namespace wizball::game {

class Catellite {
public:
    // Sprite 1 as the game leaves it in $E7C6/$E7C7, $B526, $E7EC.
    struct Sprite {
        int x{}, y{};
        std::uint8_t pointer{}, colour{};
    };

    void lose();  // $8645: off, at 0, 0, no shield
    // $7CDA at a life's start clears $B20D-$B26C: all but the colour
    // clock, the wandering and the shield; and the sprite's position.
    void clear_for_life();
    void hide() { sprite_.y = 0; }   // $AACF
    void drop_shield() { shield_ = 0; } // $AB17
    void stop() {                     // $ABBA
        stick_dx_ = stick_dy_ = 0;
        vx_ = vy_ = 0;
    }
    // $6648: sprite 1 borrowed for the bonus count's icon.
    void show_icon(int x, int y, std::uint8_t pointer, std::uint8_t colour) { sprite_ = {x, y, pointer, colour}; }
    // $8254: where a place change leaves it.
    void place_at(std::uint8_t x, std::uint8_t y) {
        x_ = x;
        y_ = y;
    }
    // The token (token 3): healthy and steady again (the position stays).
    void reset() {
        hits_ = 0;
        wandering_ = false;
    }
    // $8673-$87CA: one pass of movement; `stick` is $B23E.
    void move(Stick stick, const Wiz& wiz, bool bonus);
    // $87CD-$881E: animation and colour (`wander_colours`: $B5DF).
    void animate(const std::array<std::uint8_t, 8>& wander_colours);

    std::uint8_t x() const { return x_; } // $B261: half pixels
    std::uint8_t y() const { return y_; } // $B262
    const Sprite& sprite() const { return sprite_; }
    bool following() const { return following_; } // $B26A
    std::int8_t stick_dx() const { return stick_dx_; } // $B263

    // $B26C: hits taken; 8 wandering, 9 gone, $80 on: a thing it caught
    // that has no use (it then never counts hits again).
    std::uint8_t hits() const { return hits_; }
    void set_hits(std::uint8_t v) { hits_ = v; }
    bool wandering() const { return wandering_; } // $861A
    void start_wandering() {
        wandering_ = true;
        hits_ = 8;
    }
    // $861C: every sixteenth pass a new random stick (it beeps on some).
    // Returns the stick to use; `beep` asks for effect $0F.
    std::uint8_t wander_stick(RandomSource& random, bool& beep);

    // The shield ($89B6-$89B9).
    std::uint8_t shield() const { return shield_; }
    // $AB12 (token 7). The clock and the warning are not reset: the game
    // warns about the catellite's shield once per load ($89B9 is never
    // cleared).
    void raise_shield() { shield_ = 0x28; }
    // $895B: the shield runs down one a second; true when the warning
    // sound (effect $13) is due.
    bool shield_step();
    // A hit ($8998): the shield takes it, else it counts. True when the
    // hit counted and the catellite still lives (effect 4).
    bool hit();

    struct Probe {
        std::uint8_t stick_dx, stick_dy, vx, vy, anim_clock, anim_frame, following, steer_clock, offset, colour_clock;
        std::uint8_t wander_clock, wander_value, shield, shield_clock, shield_blink, shield_warned;
    };
    Probe probe() const {
        return {std::uint8_t(stick_dx_), std::uint8_t(stick_dy_), std::uint8_t(vx_), std::uint8_t(vy_), anim_clock_,
                anim_frame_, std::uint8_t(following_), steer_clock_, offset_, colour_clock_, wander_clock_,
                wander_value_, shield_, shield_clock_, shield_blink_, shield_warned_};
    }

private:
    void follow(const Wiz& wiz);

    std::uint8_t x_{}, y_{};
    std::int8_t stick_dx_{}, stick_dy_{}; // $B263, $B264
    std::int8_t vx_{}, vy_{};             // $B265, $B266: the pull towards the trail
    std::uint8_t anim_clock_{}, anim_frame_{}; // $B267, $B268
    std::uint8_t offset_{};               // $B269: 0 behind a wiz moving right, $15 for left
    bool following_{};                    // $B26A
    std::uint8_t steer_clock_{};          // $B26B
    std::uint8_t hits_{};                 // $B26C
    std::uint8_t colour_clock_{};         // $B26D
    bool wandering_{};                    // $861A
    std::uint8_t wander_clock_{}, wander_value_{}; // $8619, $861B
    std::uint8_t shield_{}, shield_clock_{}, shield_blink_{}, shield_warned_{}; // $89B6-$89B9
    Sprite sprite_{};
};

} // namespace wizball::game
