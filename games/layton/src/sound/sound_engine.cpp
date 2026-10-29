#include "sound/sound_engine.hpp"
#include <algorithm>

namespace picarat {

SoundEngine::SoundEngine(std::filesystem::path assets)
    : SoundEngine(SoundLayout::adventure2(assets)) {}

SoundEngine::SoundEngine(SoundLayout layout) {
    object_ =
        std::make_unique<SoundObject>(std::move(layout), static_cast<dse::SoundCommands&>(*this));
    status_ = hardware_.busy();
}

SoundEngine::~SoundEngine() = default;

void SoundEngine::setupPcm(unsigned channel, unsigned format, const std::uint8_t* data,
                           unsigned repeat, unsigned loopStart, unsigned loopLength,
                           unsigned volume, unsigned shift, unsigned timer, unsigned pan) {
    hardware_.setupPcm(channel, format, data, object_ ? object_->memoryLimit(data) : data, repeat,
                       loopStart, loopLength, volume, shift, timer, pan);
}

void SoundEngine::render(std::span<std::int16_t> stereo) {
    for (std::size_t i = 0; i + 1 < stereo.size(); i += 2) {
        for (;;) { // alarm expiries and stream passes due at this clock, earliest first
            Alarm* due = nullptr;
            for (auto& a : alarms_) {
                if (a.running && a.period && clocks_ >= a.next && (!due || a.next < due->next)) {
                    due = &a;
                }
            }
            const bool refill = !refills_.empty() && clocks_ >= refills_.front() &&
                                (!due || refills_.front() <= due->next);
            if (refill) {
                refills_.erase(refills_.begin());
                object_->driver().streamAlarm();
                continue;
            }
            if (!due) {
                break;
            }
            const std::uint64_t expiry = due->next;
            due->next += std::uint64_t(due->period) * 64;
            if (due == &alarms_[1]) {
                object_->tick();
                status_ = hardware_.busy(); // ARM7 status refresh after this batch
                ++iterations_;
            } else {
                refills_.push_back(expiry + kRefillLatency);
            }
        }
        hardware_.render(stereo.subspan(i, 2));
        clocks_ += DsSound::clocksPerSample;
    }
}

void SoundEngine::advance(std::uint64_t clocks) {
    const std::uint64_t target = clocks_ + clocks;
    while (clocks_ < target) {
        const std::uint64_t frames = std::min<std::uint64_t>(
            1024, (target - clocks_ + DsSound::clocksPerSample - 1) / DsSound::clocksPerSample);
        scratch_.resize(std::size_t(frames) * 2);
        render(scratch_);
    }
}

} // namespace picarat
