#pragma once
#include "marble_madness_amiga/data/game_files.hpp"
#include "marble_madness_amiga/sound/effects.hpp"
#include "marble_madness_amiga/sound/music.hpp"
#include <memory>
#include <span>
#include <vector>

// A course's sounds: the table at the start of its sound file (a word
// count, then 8 bytes per entry: kind, link, address) and the engines
// that play it ($1222E4), rendered to the host.
//
// Kinds: 0 music (address: score, instrument, tempo), 1 a sound effect
// (address: 18-byte record of segments, volume and pitch envelopes,
// duration, priority, pan; with a link, the started sound is kept in that
// entry so that it can be stopped), 2 stop the music, 3 count down the
// sounds kept here and stop the last one at 0, 4 and 5 set and clear a
// flag nothing reads.
namespace marble_madness::sound {

class SoundBoard {
public:
    SoundBoard(const data::GameFiles&, data::Course);
    // A copy plays on from the same point (quick save); it shares the
    // read-only sample memory.
    SoundBoard(const SoundBoard&);
    SoundBoard& operator=(const SoundBoard&) = delete;

    // $1222E4: plays table entry `id` on the right or left channels.
    void play(int id, bool right);
    // Whether an entry starts or stops the music.
    bool music(int id) const {
        return id >= 0 && std::size_t(id) < entries_.size() && (entries_[std::size_t(id)].kind == 0 || entries_[std::size_t(id)].kind == 2);
    }
    // The same from a world update: the update runs into the next vertical
    // blank before it gets to its sounds, so they start after that blank's
    // steps.
    void play_after_blank(int id, bool right) { pending_.push_back({id, right}); }
    // Renders the next `stereo.size() / 2` frames at `rate`; the engines
    // tick every vertical blank (50 per second) in between.
    void render(std::span<float> stereo, int rate);
    // Research: the channels' requests are logged here when set.
    void log_requests(std::vector<amiga::Paula::Request>* log) { effects_->paula().log = log; }

private:
    struct Entry {
        std::uint16_t kind{};
        std::int16_t link{-1};
        std::uint32_t address{};
        std::int16_t count{}; // kind 3: sounds kept
        Handle kept;          // kind 3: the last of them

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(kind, link, address, count, kept); }
    };

    std::shared_ptr<const Memory> memory_;
    std::unique_ptr<Effects> effects_;
    std::unique_ptr<Music> music_;
    std::vector<Entry> entries_;
    double until_tick_{}; // seconds to the next vertical blank
    struct Pending {
        int id{};
        bool right{};

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(id, right); }
    };
    std::vector<Pending> pending_;

  public:
    template <class A> void persist(A &ar) { ar(*effects_, *music_, entries_, until_tick_, pending_); }
};

} // namespace marble_madness::sound
