#pragma once
#include "sonic_advance_gba/game/scheduler.hpp"
#include "sonic_advance_gba/game/types.hpp"

// A searchlight beam drawn with a window (08011DBC creates the task 08011E08,
// priority 2000, 0xC bytes): a wedge from (x, y) below the screen, turning
// with its angle, its two sides `width` apart.
namespace sonic_advance_gba {

struct VideoState;

class Searchlight final : public Process {
public:
    explicit Searchlight(VideoState& v) : Process(0x2000), video_(v) {}

    void update(Game& g) override;
    // 08012284: the line effect ends with the beam.
    void on_destroy() override;

    u16 angle{};           // +00 0-3FF
    u16 turn_speed = 0x40; // +02
    s16 swing = 0x800;     // +04 the turning rate (8.8), changed by turn_speed toward the middle
    u16 x = 0x78;          // +06 the beam's foot
    s16 y = 200;           // +08
    u8 window{};           // +0A as in window_shapes.hpp (2/3: one of both windows)
    u8 width = 0x20;       // +0B the half angle between the sides

private:
    VideoState& video_;
};

} // namespace sonic_advance_gba
