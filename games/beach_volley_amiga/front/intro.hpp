#pragma once
#include "amiga/paula_channels.hpp"
#include "beach_volley_amiga/data/assets.hpp"
#include "platform/image.hpp"
#include <array>
#include <cstdint>
#include <vector>

// The intro (intro.code; game.md "Intro"): the Ocean logo rising, the
// trophy sliding down, "PRESENT", the Beach Volley titles and the credits
// scrolling up, then the comic pages with their voices, ending on the
// players menu (F1 one player, F2 two players). It runs from the vertical
// blank: its main loop draws a frame every third blank on a double-buffered
// 320 x 256 screen with three palettes split by the copper; the comic draws
// straight onto the shown screen between fixed pauses.
namespace beach_volley::front {

// intro.code's copy of the music player: the tables after it (the samples'
// addresses, sample n at $2457A + 4n; the 36 periods), and its channels:
// notes on 0-2 ($24336 reads three), effects on all four ($24194); channel
// 3 is the comic's voices.
constexpr std::uint32_t intro_sample_table = 0x2457E, intro_period_table = 0x245BA;
constexpr unsigned intro_note_channels = 0x7, intro_effect_channels = 0xF;

// The intro's pictures and voices, decoded from its files.
struct IntroArt {
    data::Picture ocean;                   // 304 x 83, the start of the block
    data::Picture trophy;                  // 224 x 399
    std::vector<std::uint8_t> above_trophy; // the row before the trophy's first (see intro.cpp)
    // From intro.code's table of pictures ($23B5C).
    data::Picture small_ocean, beach, volley, present, credits;
    std::array<data::Picture, 7> pages;    // in the order shown
    std::array<data::Picture, 2> choices;  // "F1 1PLAYER", "F2 2PLAYERS"
    data::Palette gold_blue{}, gold_red{}, gold_white{}, comic{};
    // Voices (channel 3) in the block: where, words, period.
    struct Voice {
        std::uint32_t offset{};
        std::uint16_t length{}, period{};
    };
    std::array<Voice, 6> voices{};
};

IntroArt decode_intro(const data::IntroFiles& files);

class Intro {
public:
    static constexpr int width = 320, height = 256;
    enum class Phase { titles, comic, menu, chosen };

    // `voices_at`: where the block starts in the sample memory channel 3 plays from.
    Intro(const IntroArt& art, std::uint32_t voices_at);

    // One vertical blank; `players` is 1 or 2 while that key is down.
    void tick(int players, amiga::PaulaChannels& paula);

    Phase phase() const { return phase_; }
    int players() const { return players_; } // once chosen
    platform::Image picture() const;
    // The screen positions of the two choices (for pointers), once shown.
    static constexpr std::array<std::array<int, 4>, 2> choice_areas{{{10, 200, 112, 12}, {198, 200, 112, 12}}};

    template <class A> void persist(A& ar) {
        ar(phase_, players_, wait_, steps_, comic_time_, shown_, back_, copper_, trophy_, ocean_rows_, small_ocean_y_,
           beach_y_, volley_y_, credits_y_);
    }

private:
    struct Copper { // the palettes above, between and below two beam lines
        std::array<data::Palette, 3> palettes{};
        std::uint8_t first_line{}, second_line{};
        template <class A> void persist(A& ar) { ar(palettes, first_line, second_line); }
    };
    struct TrophyScroll {
        int rows{1}, top{398}; // rows shown, from this picture row
        bool half{};           // growing: every second step only
        template <class A> void persist(A& ar) { ar(rows, top, half); }
    };
    using Screen = std::vector<std::uint8_t>; // 320 x 256 colour indices

    void titles_step();
    void comic_tick(amiga::PaulaChannels& paula);
    void draw(Screen& screen, const data::Picture& picture, int x, int y) const;
    void play_voice(int voice, amiga::PaulaChannels& paula) const;

    const IntroArt* art_;
    std::uint32_t voices_at_;
    Phase phase_{Phase::titles};
    int players_{};
    int wait_{};          // vertical blanks to the next step
    int steps_{};         // steps of the titles' loop so far
    int comic_time_{};    // vertical blanks since the comic began
    Screen shown_, back_;
    Copper copper_;
    TrophyScroll trophy_;
    int ocean_rows_{1};
    int small_ocean_y_{-116}, beach_y_{25}, volley_y_{255}, credits_y_{265};
};

} // namespace beach_volley::front
