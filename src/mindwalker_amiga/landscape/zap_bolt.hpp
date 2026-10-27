#pragma once
#include "mindwalker_amiga/world/random.hpp"
#include <cstdint>
#include <vector>

// The zap's lightning (A $2061BA with mode 1): a jagged bolt from the walker
// to the target cursor, drawn as lines into the tether layer (display plane
// 4, display rows), and now and then four branches from the cursor towards
// the screen's corners. Each segment wider than 8 pixels across is split at
// its middle, the middle moved up or down by a random amount below the
// spread ($2060BC); the spread halves at each split. A's mixer at $20648E,
// seeded at the stage's start ($20ADF6), makes the randomness.
namespace mindwalker::landscape {

class ZapBolt {
public:
    void seed(std::uint16_t high, std::uint16_t low) { random_ = world::Mixer::seeded(high, low); }
    // One frame's bolt from (x0, y0) to the cursor (x1, y1), display rows.
    void draw(std::vector<std::uint8_t>& layer, int x0, int y0, int x1, int y1);
    const world::Mixer& random() const { return random_; }
    std::uint16_t frames() const { return frames_; }

private:
    void segment(std::vector<std::uint8_t>& layer, int x0, int y0, int x1, int y1, int spread);
    int toward(int corner, int target); // $206388

    world::Mixer random_;
    std::uint16_t frames_{}; // $206496
};

} // namespace mindwalker::landscape
