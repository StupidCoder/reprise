#pragma once
#include "amiga/hunk.hpp"
#include "amiga/paula_voices.hpp"
#include "mindwalker_amiga/world/random.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <vector>

// The landscape's sound (A $20B678-$20C8B0): the program writes the four
// channels' registers itself, each time timer.device's request comes back
// (80 ms at stage 0, shorter at later stages):
//
// - a sequencer plays the music and the effects from A's tables: song
//   parts ($20C710: three patterns each) of steps (pattern A: length and
//   volume, B: note and slide, C: the notes' periods); every tick the note
//   and its decaying volume go into a 16-step ring, which the four channels
//   play at different delays and volumes (an echo) on a 16-sample wave;
//   every fourth time round the music changes key; an effect (requested by
//   number) interrupts it and returns to it;
// - while the walker aims or zaps, a pulse wave (the walker) and noise from
//   image memory (the bolt, a hit) take over, panned by the cursor;
// - marking a cell adds a short blip on channel 0;
// - the moment of a catch rumbles on all four channels.
namespace mindwalker::landscape {

class LandscapeSound {
public:
    // `landscape` A, `setup` S (its image memory is the noise).
    LandscapeSound(const amiga::LoadedImage& landscape, const amiga::LoadedImage& setup, int noise_frame);

    // Effects by number ($20C856): 1 find, 2 landmark, 3 creature's place,
    // 4 pushing at a held view's edge, 6 the catch survived, 7 after the
    // catch, 11 beacon, 12 the catch lost, 5 and 8-10 the forms (pyramids).
    void request(int effect) { request_ = effect; }
    int requested() const { return request_; }
    // For replays: the sequencer's state after its last tick (song, step,
    // the ring's index and newest period and volume, key) and the aim's
    // synth (pulse, level, aim, bolt, hit).
    std::array<int, 6> music_state() const {
        return {song_, step_, ring_at_, ring_periods_[std::size_t(ring_at_)], ring_volumes_[std::size_t(ring_at_)], key_};
    }
    std::array<int, 5> aim_state() const { return {pulse_, level_, aim_, bolt_, hit_}; }
    // For replays: every tick's music_state() when set.
    std::vector<std::array<int, 6>>* trace{};
    int song() const { return song_; }
    // $209F34: a cell marked: a blip of 7 ticks at period `base` + 200.
    void blip(int base) {
        blip_ = 7;
        blip_base_ = base;
    }
    // While fire is held ($20A45C): the aim's sound for 16 ticks more;
    // released: 8.
    void aim(int ticks) { aim_ = ticks; }
    // $2061BA: the bolt drawn (its noise for `length` ticks, pitch `pitch`).
    void bolt(int length);
    // $206F40: a creature hit.
    void hit() { hit_ = 5; }
    // $205EC4: the catch's rumble at step `step` (0-23); 24 ends it.
    void rumble(int step) { rumble_ = step < 24 ? 24 - step : 0; }
    bool rumbling() const { return rumble_ != 0; }
    // $20BB8A / $20BED6: all channels quiet (DMA off) / on again.
    void stop();
    // $20BDFE: the echo's ring emptied (when the aim starts).
    void stop_rest() {
        ring_periods_.fill(0);
        ring_volumes_.fill(0);
    }
    void start() { voices_.set_dma(true); }

    // One display frame (20 ms) of time: timer requests that come back
    // are served. `stage` sets the music's pace; `slow` ($20052A) slows it;
    // `stick` whether the stick is held; `cursor_x` pans the aim's sound.
    void frame(int stage, int slow, bool stick, int cursor_x);
    void render(std::span<float> stereo, int rate) { voices_.render(stereo, rate); }

private:
    void sequencer_tick();           // $20BFA6
    void aim_tick(bool stick, int x, int slow); // $20B858
    void pulse_tick(int x, int slow);           // $20B902
    void rumble_tick();              // h56
    void music_channel(int channel); // $20BE32
    void square_channel(int channel); // $20BE84
    void aim_channel(int channel);   // $20B794

    const amiga::LoadedImage& a_;
    amiga::PaulaVoices voices_;
    std::array<int, 4> channel_kind_{}; // $20C892: 1 music wave, 2 aim waves, 3 square
    std::vector<std::int8_t> wave_;     // h61: the music's 16 samples (from h60)
    std::vector<std::int8_t> pulse_wave_ = std::vector<std::int8_t>(34); // h58
    std::vector<std::int8_t> noise_;    // image memory (shared +$1C6)
    std::array<std::int8_t, 2> square_{std::int8_t(0x81), 0x7F}; // $20C834

    // The sequencer.
    int song_{}, step_{};               // $20C844, $20C846
    std::uint32_t lengths_{}, notes_{}, periods_{}; // $20C838 / $20C83C / $20C840
    int ticks_left_{};                  // $20C84A
    int volume_{}, decay_{};            // $20C84B, $20C84C
    int period_{};                      // $20C84E
    bool slide_{};                      // $20C850
    std::array<int, 16> ring_periods_{}, ring_volumes_{}; // $20C85E, $20C87E
    int ring_at_{};                     // $20C88E
    int loops_{}, saved_song_{}, saved_loops_{}, key_{}; // $20C85C, $20C85A, $20C85B, $20C896
    int request_{};                     // $20C856
    int blip_{}, blip_base_{};          // $20C898, $20C89C
    // The aim's sound.
    int pulse_{}, pitch_{}, vibrato_{}, level_{}, bolt_{}, bolt_pitch_{}, aim_{}, hit_{}; // $20BBC8-$20BBE4
    world::ShiftRegister noise_random_; // h57
    int rumble_{};                      // $20BBE8
    // timer.device.
    int timer_left_{80000};             // microseconds until the request comes back
};

} // namespace mindwalker::landscape
