#pragma once
#include "amiga/paula_channels.hpp"
#include "beach_volley_amiga/data/assets.hpp"
#include "beach_volley_amiga/sound/soundtracker.hpp"
#include "platform/image.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

// The venue card (animation.code; game.md "Venue card"): before each match
// of a one-player tour, the map of the tour fades in column by column, the
// routes already flown are on it and the next one is drawn dot by dot;
// a short scene plays in the strip at the bottom (the venue's own: a plane,
// boats, a camel...), the map fades out, the venue's picture shows in a
// frame of tiles while its palette fades in, and the venue's song plays.
// Fire stops the song and a voice names the venue.
//
// The program draws straight onto one shown screen 400 pixels wide (320
// shown, the rest for its horizontal scroll), waiting for the beam between
// its frames; the port keeps that screen and replays the program's drawing
// blank by blank. Its scenes are code in the original (hand-written motion
// per venue); the port rewrites them, reading their tables from the file.
namespace beach_volley::front {

struct CardFiles {
    int venue{};                       // 1-8
    data::CodeFile code;               // animation.code, as loaded at $3BC50
    std::vector<std::uint8_t> map;     // carte: 4 planes of 400 x 200, rows of 50 bytes
    std::vector<std::uint8_t> anim;    // animN (loaded at $49F20): the picture, strip, sprites, tiles
    sound::Song song;                  // the venue's song in the card
    std::vector<std::uint8_t> voice;   // dnvN (loaded at $51E40)
    std::vector<std::uint8_t> instruments;
};

// Reads the files animation.code loads for `venue` ($3F716); throws
// std::runtime_error when they are missing or not as expected.
CardFiles load_card_files(const std::filesystem::path& content, int venue);

class VenueCard {
public:
    static constexpr int width = 320, height = 200;

    // One drawing step of the program, replayed in order; `wait` ends a
    // blank's steps.
    struct Op {
        enum class Kind : std::uint8_t {
            wait,       // a: blanks
            wait_fire,  // $3BF4A
            dot,        // $3C20E: a route dot at (a, b); c: into the map
            dissolve,   // $3C034/$3C0CA: the map onto the screen through the mask a
            scroll,     // $3C162/$3C1B6: a: the bitplane pointers' bytes, b: the fine scroll
            strip,      // $3ECCC: the strip's background back
            sprite,     // $3F3B4: at (a, b), c x d pixels, image `at` in the anim file; e: clipped to the strip
            tile,       // $3ED70: a 16 x 16 tile from `at` to the screen byte a
            upper,      // the palette above the split: palettes[a]
            lower,      // $3C81A: palettes[a] from row b (-1: no split)
            stop_music, // $3BF54
            voice,      // $3C7DC: a words
            end,
        };
        Kind kind{};
        int a{}, b{}, c{}, d{}, e{};
        std::uint32_t at{};
    };

    VenueCard(const CardFiles& files, amiga::PaulaChannels& paula);
    // The card's start: its song (not when it is read from a quick save).
    void begin();

    // One vertical blank; `fire` is the button as it stands.
    void tick(bool fire);
    bool finished() const { return finished_; }
    platform::Image picture() const;

    // After a quick save is read: the memory and song again.
    void bind(const CardFiles& files);
    template <class A> void persist(A& ar) {
        bool playing = player_.playing();
        ar(screen_, map_, upper_, lower_, split_row_, pointer_bytes_, fine_scroll_, at_, wait_, music_on_, player_,
           playing, finished_);
        if constexpr (A::reading) player_.bind(playing ? &files_->song : nullptr);
    }

    // For research tools: the screen buffer as colour indices (400 x 200),
    // the palettes and the scroll.
    std::vector<std::uint8_t> screen() const;
    const std::array<std::uint16_t, 16>& upper() const { return upper_; }
    int split_row() const { return split_row_; }
    int pointer_bytes() const { return pointer_bytes_; }
    int fine_scroll() const { return fine_scroll_; }
    int at() const { return at_; }

private:
    void apply(const Op& op);

    const CardFiles* files_;
    amiga::PaulaChannels* paula_;
    std::vector<Op> tape_;
    std::vector<std::array<std::uint16_t, 16>> palettes_;
    std::vector<std::int8_t> memory_; // the instruments, then the voice
    sound::Player player_;

    // The screen ($32000) and the map ($402E0) as the program has them: four
    // bitplanes of 400 x 200 one after the other (a row 50 bytes, a plane
    // 10,000), so that drawing past a plane's end runs into the next one's
    // start as it does on the Amiga; the screen keeps the 16 bytes after it
    // that such drawing can reach.
    std::vector<std::uint8_t> screen_, map_;
    std::array<std::uint16_t, 16> upper_{}, lower_{};
    int split_row_{-1};
    int pointer_bytes_{}, fine_scroll_{};
    int at_{}, wait_{};
    bool music_on_{};
    bool finished_{};
};

} // namespace beach_volley::front
