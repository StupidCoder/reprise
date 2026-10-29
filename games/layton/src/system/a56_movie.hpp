#pragma once
// A movie of the 3DS adventures as the scripts use it: a resource opened
// by name (3802, fifth adventure 0x125d04 -> 0x32f938), started (3803),
// shown on an object (5410, 0x12de9c -> 0x2cd3d4) and asked for its clock
// (3820) and its end (3812). The pictures come from the native moflex
// decoder (video/moflex.hpp); the clock is the movie loop's (0x381dfc):
// display ticks counted from the start, the wanted stereo pair = ticks x
// pairs per second / 59.831 (0x382098). Docs:
// .analysis/adventure56/docs/movies.md.
#include "graphics/ctr3d/raster.hpp"
#include "video/moflex.hpp"
#include <cstdint>
#include <memory>
#include <vector>

namespace picarat::adventure56 {

class MoviePlayer {
public:
    // The 3DS display rate the movie clock counts in (0x382098).
    static constexpr double kDisplayRate = 59.831;

    MoviePlayer(std::vector<std::uint8_t> file, moflex::Tables tables);

    // 3803: the clock starts; the first picture shows at once.
    void start();
    // One game frame of `displayFrames` display ticks.
    void tick(float displayFrames);

    // 3802's third operand (0x32f938's set-up 1, 2, 1 instead of 0, 1, -1):
    // *hypothesis*, the movie plays over and over (the secrets' video
    // thumbnails, m070, keep moving past its ten seconds on the console).
    void setLooping(bool on) { looping_ = on; }

    bool started() const { return started_; }
    // 3812 (0x343278): the last picture has had its time.
    bool ended() const { return ended_; }
    // 3820 (0x343480): display ticks since the start (subtitle times
    // compare with it).
    float ticks() const { return ticks_; }
    // The picture shown (the left eye of a stereo pair), 400 x 240.
    const std::shared_ptr<ctr3d::Texture>& picture() const { return picture_; }

private:
    bool showNext(); // decodes up to and including the next shown picture
    moflex::Movie movie_;
    std::shared_ptr<ctr3d::Texture> picture_;
    std::vector<std::uint8_t> rgba_;
    bool started_ = false, ended_ = false, exhausted_ = false, looping_ = false;
    float ticks_ = 0;
    long shown_ = -1; // the pair (or mono picture) on screen
};

} // namespace picarat::adventure56
