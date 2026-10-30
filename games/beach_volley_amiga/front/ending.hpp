#pragma once
#include "amiga/paula_channels.hpp"
#include "beach_volley_amiga/data/assets.hpp"
#include "beach_volley_amiga/sound/soundtracker.hpp"
#include "platform/image.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

// The ending (final.code; game.md "Ending"): after the eighth win the
// screen scrolls up over the trophy while musicfinal plays, then two
// figures circle in front of it, drawn into the screen's fourth bitplane
// (the trophy has three), turning and changing colour once they have gone
// round for a while, until fire; then the game starts again from the intro.
namespace beach_volley::front {

struct EndingFiles {
    data::CodeFile code;               // final.code, as loaded at $49800
    std::vector<std::uint8_t> trophy;  // trophe: 3 planes of 224 x 399, rows of 28 bytes
    std::vector<std::uint8_t> figures; // final: eight one-plane pictures of 128 x 128
    sound::Song song;                  // musicfinal
    std::vector<std::uint8_t> instruments;
};

EndingFiles load_ending_files(const std::filesystem::path& content);

class Ending {
public:
    // The display window: 276 lines from line $25 ($49F2E's copper list).
    static constexpr int width = 320, height = 276;

    Ending(const EndingFiles& files, amiga::PaulaChannels& paula);
    // The start: the song (not when read from a quick save).
    void begin();

    // One vertical blank; `fire` is the button as it stands.
    void tick(bool fire);
    // Fire was pressed and the sound stopped ($499B0): intro.code follows.
    bool finished() const { return phase_ == Phase::finished; }
    platform::Image picture() const;

    void bind(const EndingFiles& files);
    template <class A> void persist(A& ar) {
        bool playing = player_.playing();
        ar(screen_, colours_, top_row_, phase_, wait_, steps_, figures_, path_at_, frame_at_, frame_delay_,
           frames_, animating_, cycle_at_, player_, playing);
        if constexpr (A::reading) player_.bind(playing ? &files_->song : nullptr);
    }

    // Research: the frame loop's count and the figures (x, y, picture).
    int frames() const { return frames_; }
    struct Figure {
        int x{}, y{};
        std::uint32_t picture{}; // offset in the final file
        template <class A> void persist(A& ar) { ar(x, y, picture); }
    };
    const std::array<Figure, 2>& figures() const { return figures_; }
    const std::array<std::uint16_t, 16>& colours() const { return colours_; }
    int top_row() const { return top_row_; }
    const std::vector<std::uint8_t>& screen() const { return screen_; }

private:
    enum class Phase : std::uint8_t { starting, scrolling, holding, looping, finished };
    void draw(const Figure& f);
    void loop_frame();
    void animate();

    const EndingFiles* files_;
    amiga::PaulaChannels* paula_;
    std::vector<std::int8_t> memory_;
    sound::Player player_;

    // The screen at $32000: four planes of 320 x 600, rows of 40 bytes.
    std::vector<std::uint8_t> screen_;
    std::array<std::uint16_t, 16> colours_{};
    int top_row_{}; // the bitplane pointers' row
    Phase phase_{Phase::starting};
    int wait_{}, steps_{};
    std::array<Figure, 2> figures_{};
    std::array<int, 2> path_at_{}, frame_at_{}, frame_delay_{}; // $4A710/2, $49C68/A, $49C6C/E
    int frames_{};      // $499E6
    bool animating_{};  // $499E4
    int cycle_at_{};    // $49A24
};

} // namespace beach_volley::front
