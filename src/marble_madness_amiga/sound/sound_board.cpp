#include "marble_madness_amiga/sound/sound_board.hpp"
#include <algorithm>
#include <utility>

namespace marble_madness::sound {
namespace {

// The effect engine's timer asks for 1/60 s on timer.device's vertical
// blank unit ($121214), which answers at the next blank: on PAL both
// engines step 50 times a second.
constexpr double tick_seconds = 1.0 / 50.0;

} // namespace

SoundBoard::SoundBoard(const data::GameFiles& files, data::Course course)
    : memory_(std::make_shared<const Memory>(files.program(), files.sounds(course))),
      effects_(std::make_unique<Effects>(*memory_)),
      music_(std::make_unique<Music>(*memory_, *effects_)) {
    const auto& bank = files.sounds(course);
    const std::uint32_t table = bank.first_filled_hunk();
    const int count = bank.u16(table);
    for (int i = 0; i < count; ++i) {
        const std::uint32_t at = table + 2 + std::uint32_t(i) * 8;
        entries_.push_back({bank.u16(at), bank.s16(at + 2), bank.u32(at + 4), 0, {}});
    }
}

SoundBoard::SoundBoard(const SoundBoard& other)
    : memory_(other.memory_),
      effects_(std::make_unique<Effects>(*other.effects_)),
      music_(std::make_unique<Music>(*other.music_)),
      entries_(other.entries_),
      until_tick_(other.until_tick_),
      pending_(other.pending_) {
    music_->use(*effects_);
}

void SoundBoard::play(int id, bool right) {
    if (id < 0 || std::size_t(id) >= entries_.size()) {
        return;
    }
    auto& e = entries_[std::size_t(id)];
    const auto& m = *memory_;
    switch (e.kind) {
    case 0:
        if (e.address != 0) {
            music_->start(e.address);
        }
        break;
    case 1: {
        if (e.address == 0) {
            break;
        }
        // The record's own pan is used only for sides other than 0 and 1.
        const std::int16_t pan = right ? -2 : -1;
        const auto chain = effects_->read_chain(m.u32(e.address));
        auto start = [&] {
            return effects_->play(chain, m.u32(e.address + 4), m.u32(e.address + 8), m.u16(e.address + 12),
                                  m.s8(e.address + 14), pan);
        };
        if (e.link < 0 || std::size_t(e.link) >= entries_.size()) {
            start();
        } else {
            auto& keeper = entries_[std::size_t(e.link)];
            effects_->stop(keeper.kept);
            keeper.kept = start();
            ++keeper.count;
        }
        break;
    }
    case 2:
        music_->stop();
        break;
    case 3:
        if (e.count > 0 && --e.count == 0) {
            effects_->stop(e.kept);
        }
        break;
    default:
        break;
    }
}

void SoundBoard::render(std::span<float> stereo, int rate) {
    std::fill(stereo.begin(), stereo.end(), 0.0f);
    std::size_t at = 0;
    const auto done = [this](int channel, int tag) { effects_->write_done(channel, tag); };
    while (at < stereo.size()) {
        const auto frames_left = (stereo.size() - at) / 2;
        const auto to_tick = std::size_t(std::max(0.0, until_tick_ * rate));
        const auto frames = std::min(frames_left, std::max<std::size_t>(to_tick, 0));
        if (frames > 0) {
            effects_->paula().render(stereo.subspan(at, frames * 2), rate, done);
            at += frames * 2;
            until_tick_ -= double(frames) / rate;
        }
        if (at < stereo.size() && until_tick_ * rate < 1.0) {
            // A vertical blank: the effects' timer, then the music task.
            effects_->tick();
            music_->vertical_blank();
            for (const auto& p : std::exchange(pending_, {})) {
                play(p.id, p.right);
            }
            until_tick_ += tick_seconds;
        }
    }
}

} // namespace marble_madness::sound
