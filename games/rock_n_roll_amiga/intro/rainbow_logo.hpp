#pragma once
#include "amiga/paula_channels.hpp"
#include "amiga/iff.hpp"
#include "platform/image.hpp"
#include "rock_n_roll_amiga/data/assets.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <random>
#include <span>
#include <vector>

// The Rainbow Arts logo the compilation disk boots into before the band
// intro ("CODED IN 1990 BY MANFRED TRENZ", the boot program unpacked to
// $30000; games/rock_n_roll_amiga/game.md "Rainbow Arts logo").
//
// Four bitplanes of 320 x 220 pixels and sixteen colours. Once a frame (its
// copper interrupt at line $F0):
//   - the pen ($3025A): a sparkler (a ten-frame bob) runs once around the
//     logo's box, 2 pixels a frame, a fuse line trailing from it to a point
//     below the box; seven layers of the burnt edge follow it, each a few
//     frames after the one before, from a list of pieces ($30572);
//   - the reveal ($302B0): the box's contents roll down, laying the logo
//     picture down from the box's top edge, the box's bottom edge after it
//     ($30634); then five coloured bars grow in from the screen's edges
//     ($307D0). Sounds: a crackle while the pen runs, a fading chord at the
//     end, both played from the program's own bytes;
//   - after a time the main loop counts out (or a mouse button or a key),
//     the colours fade to black ($302C6).
// Everything drawn is read from the logo program (data::Assets::logo).
namespace rocknroll::intro {

class RainbowLogo {
public:
    static constexpr int width = 320, height = 220;
    static constexpr int planes = 4, row_bytes = 40;

    explicit RainbowLogo(const data::Memory& logo);

    // One frame. `skip`: a mouse button or a key, which starts the fade.
    // Returns true once the fade has ended.
    bool tick(bool skip);
    bool done() const { return stage_ == Stage::done; }

    platform::Image picture() const;
    void render(std::span<float> stereo, int rate) { paula_.render(stereo, rate); }

    // Research: the four planes as shown (pen and fuse included), plane
    // after plane, 40 bytes a row.
    std::vector<std::uint8_t> shown_planes() const;
    int frame() const { return frame_; }
    const std::array<amiga::Rgb4, 16>& colours() const { return colours_; }
    // Research: the sound's state as the original keeps it, by address.
    std::vector<std::pair<std::uint32_t, std::uint32_t>> sound_state() const {
        return {{0x30AE2, std::uint32_t(sound_frames_)},      {0x30AE4, std::uint32_t(sweep_frames_)},
                {0x30AE6, std::uint32_t(sweeps_)},            {0x30AEC, sweep_at_[0]},
                {0x30AF0, sweep_at_[1]},                      {0x30B10, std::uint32_t(std::uint16_t(wobble_))},
                {0x30B00, std::uint32_t(std::uint8_t(wobble_step_))}, {0x30B03, std::uint32_t(wobble_count_)},
                {0x30B2C, fade_volume_[0]},                   {0x30B30, fade_volume_[1]}};
    }

private:
    enum class Stage { pen, reveal, fade, done };
    struct Point {
        int x, y;
    };
    struct Piece { // $30B6E: one piece of the burnt edge
        std::uint16_t to, from;
        bool words;
        int rows;
    };
    struct Segment {
        int dx, dy, steps;
    };
    struct Bar { // $307D0
        std::uint32_t at;   // offset into the planes
        int left;           // rows (or frames) still to draw
        int pattern;        // >= 0: the pattern's offset; < 0: a widening line in plane 3
        int step;           // bytes to the next row (pattern) or byte (line)
    };

    // A byte of plane p. The planes lie one after the other in the original,
    // so rows past the last one are the next plane's first rows (and past
    // plane 3, memory nothing shows).
    std::uint8_t& plane(int p, std::uint32_t offset);
    void pen_frame();
    void burn();
    void move_pen();
    void pen_sound();
    void save_box_bottom();
    void reveal_frame();
    void roll();
    void bars();
    void end_sound();
    void fade();
    void silence();
    void compose(std::array<std::vector<std::uint8_t>, planes>& out) const;

    const data::Memory* logo_;
    std::array<std::vector<std::uint8_t>, planes> planes_;
    std::uint8_t beyond_{};
    std::array<amiga::Rgb4, 16> colours_{};
    Stage stage_{Stage::pen};
    int frame_{};

    // The pen.
    Point anchor_{}, pen_{};
    std::vector<Segment> path_;
    std::size_t segment_{};
    int segment_left_{};
    bool pen_done_{};
    int bob_frame_{};
    std::optional<Point> bob_;  // drawn at the pen's place before it moved
    std::optional<Point> fuse_; // the fuse to the pen's place after it
    int bob_frame_shown_{};

    // The burnt edge: per layer a delay counter, the next piece and where
    // its pictures start.
    std::vector<Piece> pieces_;
    std::array<int, 7> layer_wait_{}, layer_piece_{};
    std::array<std::uint32_t, 7> layer_source_{};

    // The reveal.
    int roll_row_{}, speed_index_{};
    std::uint32_t logo_source_{}; // the next logo row ($35F00 on)
    std::size_t saved_row_{};     // the next saved bottom-edge row
    std::array<std::vector<std::uint8_t>, planes> box_bottom_;
    std::array<Bar, 5> bars_{};

    // The fade.
    int fade_wait_{};

    // Sound ($3085E, $30972): the channels play from the program's bytes.
    std::vector<std::int8_t> sound_memory_;
    amiga::PaulaChannels paula_;
    int sound_frames_{}, sweep_frames_{}, sweeps_{};
    std::array<std::uint32_t, 2> sweep_at_{};
    int wobble_reload_{}, wobble_count_{}, wobble_step_{}, wobble_{}, wobble_base_{};
    std::array<std::uint16_t, 2> fade_volume_{}, fade_step_{};
    std::minstd_rand random_{0x2689};
};

} // namespace rocknroll::intro
