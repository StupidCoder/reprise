#pragma once
#include "amiga/iff.hpp"
#include "amiga/paula_channels.hpp"
#include "platform/image.hpp"
#include "rock_n_roll_amiga/data/assets.hpp"
#include "rock_n_roll_amiga/sound/tfmx.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <vector>

// The band intro of the compilation disk (file $44, unpacked to $1087E,
// entry $7BC44; games/rock_n_roll_amiga/game.md "Band intro").
//
// One 320 x 200 screen of five interleaved bitplanes (200 bytes a line) and
// the curtain picture's 32 colours. In order:
//   - the curtain ($7C452): a little fellow carries a sign across the stage
//     from right to left, "PRESENTED BY", then "Rainbow Arts", each crossing
//     followed by a pause; a click ends the crossing at once;
//   - the stage is swapped in line by line from the bottom ($7C384);
//   - the band plays ($7BC72): the TFMX song, and eleven animations blitted
//     from a store of frames, two a frame in turn, started and stopped by a
//     timeline of frame counts ($7BD14); a click (or the timeline's end)
//     fades the music and lets the band play 300 frames more;
//   - the credits are swapped in line by line from the top ($7C3DA) and stay
//     until the loader starts on the game.
// The program draws everything with the blitter; its blits are rebuilt here
// as such (Blit) over the screen and the intro's memory.
namespace rocknroll::intro {

class BandIntro {
public:
    static constexpr int width = 320, height = 200;

    explicit BandIntro(const data::Memory& intro);

    // One frame. `click`: the left mouse button. Returns true once the
    // credits have been shown as long as the original shows them.
    bool tick(bool click);
    bool done() const { return stage_ == Stage::done; }

    platform::Image picture() const;
    void render(std::span<float> stereo, int rate) { paula_.render(stereo, rate); }

    // Research: the screen's bytes (interleaved) and the frame count.
    std::span<const std::uint8_t> screen() const { return std::span(screen_).first(screen_bytes); }
    int timeline() const { return timeline_; }
    bool pausing() const { return stage_ == Stage::pause; }
    bool wiping_in() const { return stage_ == Stage::wipe_in; }
    bool playing() const { return stage_ == Stage::band || stage_ == Stage::ending; }
    // Research: the slots as the original keeps them ($7C018) and the turn ($7C044 / 4).
    std::vector<std::uint16_t> slot_words() const {
        std::vector<std::uint16_t> w;
        for (const auto& s : slots_) w.push_back(s.position), w.push_back(s.end);
        return w;
    }
    int turn() const { return turn_; }
    const sound::Tfmx& player() const { return player_; }

    static constexpr std::size_t screen_bytes = 40000;

private:
    enum class Stage { curtain, pause, wipe_in, band, ending, wipe_out, credits, done };
    struct Blit { // the blitter's registers for one area blit (ascending)
        std::uint16_t con0{}, con1{}, first_mask{0xFFFF}, last_mask{0xFFFF};
        std::uint32_t a{}, b{}, c{}, d{};
        std::int16_t amod{}, bmod{}, cmod{}, dmod{};
        std::uint16_t size{};
    };
    struct Slot { // $7C018: an animation's frame position (-1: stopped) and end
        std::uint16_t position, end;
    };

    std::uint16_t word(std::uint32_t address) const;
    void set_word(std::uint32_t address, std::uint16_t value);
    void blit(const Blit&);

    void curtain_draw();
    void curtain_restore(bool click);
    void table_blit(std::uint32_t table, int index); // $7C5E2
    void swap_line(std::uint32_t picture, std::uint32_t offset);
    void step_timeline();                            // $7BD14
    void animate();                                  // $7C150
    void music_frame() { player_.frame(paula_, sound_memory_); }

    const data::Memory* intro_;
    std::vector<std::uint8_t> screen_;          // $10938 on, with the lines below it the copies reach
    std::vector<std::uint8_t> stage_picture_, credits_picture_; // swapped with the screen's lines
    std::array<amiga::Rgb4, 32> colours_{};
    Stage stage_{Stage::curtain};

    // The curtain ($7C43E-$7C452) and the blit table entry it rewrites ($7CE1A).
    Blit blitter_;
    int pass_{}, pass_kept_{}, shift_{}, shift_kept_{}, pass_end_{};
    bool drawn_{}, second_sign_{};
    std::uint32_t sign_picture_{};
    std::uint32_t copy_to_{}, copy_from_{}, copy_from_kept_{}, copy_to_kept_{};
    int copy_words_{6};
    int pause_left_{};

    // The wipes ($7C1F0) and the band.
    std::uint32_t wipe_at_{};
    int timeline_{}, ending_frames_{};
    std::array<Slot, 11> slots_{};
    int turn_{};
    int credits_left_{};

    // Music: the game's TFMX player on the intro's song.
    sound::Tfmx player_;
    std::vector<std::int8_t> sound_memory_;
    amiga::PaulaChannels paula_;
};

} // namespace rocknroll::intro
