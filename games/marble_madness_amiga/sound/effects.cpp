#include "marble_madness_amiga/sound/effects.hpp"
#include <climits>
#include <map>

namespace marble_madness::sound {
namespace {

// Program tables.
constexpr std::uint32_t right_pairs = 0x1217BC;  // 2 channel masks for pan -2
constexpr std::uint32_t left_pairs = 0x1217C0;   // 2 for pan -1
constexpr std::uint32_t stereo_pairs = 0x1217C4; // 4 left + right masks for a screen x
constexpr std::uint32_t steady = 0x121938;       // the default envelope: 1.0

constexpr int first_write = 0, second_write = 1; // the two writes kept queued per channel

} // namespace

// $12070C: every envelope starts on the steady list.
Effects::Effects(const Memory& memory) : memory_(&memory) {
    for (auto& ch : channels_) {
        ch.volume.list = ch.pitch.list = steady;
    }
}

Chain Effects::read_chain(std::uint32_t first) const {
    Chain chain;
    std::map<std::uint32_t, int> index;
    // Number the segments in the order they are reached.
    for (std::uint32_t at = first; at != 0 && !index.contains(at);) {
        index[at] = int(chain.size());
        chain.push_back({});
        at = memory_->u32(at);
    }
    for (const auto& [at, i] : index) {
        auto& s = chain[std::size_t(i)];
        const std::uint32_t next = memory_->u32(at);
        s.next = next == 0 ? -1 : index.at(next);
        s.count = memory_->s16(at + 4);
        s.skip = memory_->u32(at + 6);
        const std::uint32_t wave = memory_->u32(at + 10);
        if (wave != 0) {
            s.wave = {memory_->u32(wave), memory_->u16(wave + 4), memory_->u16(wave + 6), memory_->u32(wave + 8)};
        }
    }
    return chain;
}

Handle Effects::play(const Chain& chain, std::uint32_t volume_envelope, std::uint32_t pitch_envelope,
                     std::uint16_t duration, std::int8_t priority, std::int16_t pan) {
    // A request to reserve channels first, then one to start ($12156E).
    const std::uint16_t mask = reserve(priority, pan);
    if (mask == 0 || chain.empty()) {
        return {};
    }
    // $120C30: the first free slot, with a new generation.
    int s = 0;
    while (s < 4 && !slots_[std::size_t(s)].free) {
        ++s;
    }
    if (s == 4) {
        return {};
    }
    auto& slot = slots_[std::size_t(s)];
    slot.free = false;
    slot.generation = std::uint16_t((slot.generation + 1) & 0x7FFF);
    // $120F46.
    slot.priority = priority;
    slot.pan = pan;
    slot.duration = duration;
    slot.mask = mask;
    for (int c = 0; c < 4; ++c) {
        if (mask >> c & 1) {
            channels_[std::size_t(c)].owner = std::uint8_t(s);
        }
    }
    slot.positional = pan >= 0 && pan <= 0x13F;
    if (slot.positional) {
        pan_channels(s);
    }
    for (int c = 0; c < 4; ++c) {
        if ((mask >> c & 1) == 0) {
            continue;
        }
        auto& ch = channels_[std::size_t(c)];
        assign(ch.volume, volume_envelope);
        assign(ch.pitch, pitch_envelope);
        if (!slot.positional) {
            ch.pan_scale = 0x100;
        }
        ch.ended = false;
        ch.chain = chain;
        ch.segment = 0;
        ch.state = chain[0];
        next_write(c, first_write, true);
        next_write(c, second_write, false);
    }
    slot.active = true;
    return {s, slot.generation};
}

void Effects::stop(Handle h) {
    if (alive(h)) {
        stop_slot(h.slot);
    }
}

void Effects::set_pan(Handle h, std::int16_t pan) {
    if (alive(h)) {
        slots_[std::size_t(h.slot)].pan = pan;
        pan_channels(h.slot);
    }
}

void Effects::stop_all() {
    for (int s = 0; s < 4; ++s) {
        if (slots_[std::size_t(s)].active) {
            stop_slot(s);
        }
    }
}

// $120A4E: the first choice whose channels are all free; else one used
// by exactly one sound of lower priority, which stops; else stop a lower
// sound that shares a channel with a choice and look again.
std::uint16_t Effects::reserve(std::int8_t priority, std::int16_t pan) {
    const auto& p = memory_->program();
    const std::uint32_t table = pan == -2 ? right_pairs : pan == -1 ? left_pairs : stereo_pairs;
    const int choices = pan == -2 || pan == -1 ? 2 : 4;
    auto take = [&](std::uint16_t mask) {
        for (int c = 0; c < 4; ++c) {
            if (mask >> c & 1) {
                channels_[std::size_t(c)].owner = 4;
            }
        }
        return mask;
    };
    while (true) {
        for (int i = 0; i < choices; ++i) {
            const std::uint16_t mask = p.u16(table + std::uint32_t(i) * 2);
            bool free = true;
            for (int c = 0; c < 4; ++c) {
                if ((mask >> c & 1) && channels_[std::size_t(c)].owner != 0xFF) {
                    free = false;
                }
            }
            if (free) {
                return take(mask);
            }
        }
        for (int i = 0; i < choices; ++i) {
            const std::uint16_t mask = p.u16(table + std::uint32_t(i) * 2);
            for (int s = 0; s < 4; ++s) {
                if (slots_[std::size_t(s)].mask == mask && slots_[std::size_t(s)].priority < priority) {
                    stop_slot(s);
                    return take(mask);
                }
            }
        }
        bool stopped = false;
        for (int i = 0; i < choices && !stopped; ++i) {
            const std::uint16_t mask = p.u16(table + std::uint32_t(i) * 2);
            for (int s = 0; s < 4; ++s) {
                if ((slots_[std::size_t(s)].mask & mask) != 0 && slots_[std::size_t(s)].priority < priority) {
                    stop_slot(s);
                    stopped = true;
                    break;
                }
            }
        }
        if (!stopped) {
            return 0;
        }
    }
}

void Effects::stop_slot(int s) {
    auto& slot = slots_[std::size_t(s)];
    slot.active = false;
    if (slot.free) {
        return;
    }
    for (int c = 0; c < 4; ++c) {
        if (slot.mask >> c & 1) {
            paula_.flush(c);
            channels_[std::size_t(c)].owner = 0xFF;
        }
    }
    slot.mask = 0;
    slot.free = true;
    slot.generation = std::uint16_t(slot.generation + 1);
}

// $120CBA: a positioned sound's two channels share its volume by screen x
// (0-$13F): the right channels (1, 2) get x * $CD / 256, the left ones the
// rest of $100.
void Effects::pan_channels(int s) {
    const auto& slot = slots_[std::size_t(s)];
    if (!slot.positional) {
        return;
    }
    const auto right = std::uint16_t(std::uint16_t(slot.pan * 0xCD) >> 8);
    for (int c = 0; c < 4; ++c) {
        if (slot.mask >> c & 1) {
            channels_[std::size_t(c)].pan_scale = (c == 1 || c == 2) ? right : std::uint16_t(0x100 - right);
        }
    }
}

// $120D52: the channel's next write from its chain. A segment plays its
// wave `count` times in one write (-2: forever); then the next segment.
// Only the first write sets the period and volume (from its wave, through
// the envelopes); later segments keep them.
void Effects::next_write(int c, int tag, bool first) {
    auto& ch = channels_[std::size_t(c)];
    while (!ch.ended) {
        auto& s = ch.state;
        if (s.count >= 0) {
            --s.count;
        }
        if (s.count + 1 == 0) {
            if (s.next < 0) {
                ch.ended = true;
                return;
            }
            ch.segment = s.next;
            ch.state = ch.chain[std::size_t(s.next)];
            continue;
        }
        if (s.skip != 0) {
            continue;
        }
        if (first) {
            ch.base_volume = s.wave.volume;
            ch.base_period = s.wave.period;
        }
        amiga::Paula::Write w;
        w.image = &memory_->at(s.wave.data);
        w.data = s.wave.data;
        w.length = s.wave.words * 2;
        ch.last_period = period(ch);
        ch.last_volume = volume(ch);
        w.period = ch.last_period;
        w.volume = ch.last_volume;
        if (s.count == -2) {
            w.cycles = 0;
        } else {
            ++s.count;
            w.cycles = s.count;
        }
        s.count = 0;
        w.set_period_volume = first;
        w.tag = tag;
        paula_.write(c, w);
        return;
    }
}

void Effects::write_done(int c, int tag) {
    // Only channels that belong to a sound go on ($1217B8 bit 2 clear).
    if ((channels_[std::size_t(c)].owner & 4) == 0) {
        next_write(c, tag, false);
    }
}

// $120EE2: an envelope starts from 0 and takes its first step at once.
void Effects::assign(Envelope& e, std::uint32_t list) {
    Envelope start{0, list == 0 ? steady : list};
    step(start);
    e = start;
}

// $122168: the envelope's list holds (rate, target) pairs of 16.16
// values. The value moves by the rate each tick; reaching the target it
// goes on to the next pair (keeping the overshoot) or, where the next rate
// turns the other way or is 0, settles on the target. Rate 0 holds.
void Effects::step(Envelope& e) {
    const auto& m = *memory_;
    std::int32_t rate = m.s32(e.list);
    if (rate == 0) {
        return;
    }
    const std::int64_t sum = std::int64_t(e.value) + rate;
    std::int32_t v;
    if (rate > 0) {
        if (sum > INT_MAX) {
            v = INT_MAX;
        } else if (std::int32_t(sum) < m.s32(e.list + 4)) {
            e.value = std::int32_t(sum);
            return;
        } else {
            v = std::int32_t(sum);
        }
        do {
            e.list += 8;
            rate = m.s32(e.list);
            if (rate < 1) {
                v = m.s32(e.list - 4);
                break;
            }
        } while (m.s32(e.list + 4) <= v);
    } else {
        if (sum < INT_MIN) {
            v = INT_MIN;
        } else if (m.s32(e.list + 4) < std::int32_t(sum)) {
            e.value = std::int32_t(sum);
            return;
        } else {
            v = std::int32_t(sum);
        }
        do {
            e.list += 8;
            rate = m.s32(e.list);
            if (rate >= 0) {
                v = m.s32(e.list - 4);
                break;
            }
        } while (v <= m.s32(e.list + 4));
    }
    e.value = v;
}

// $121EF8: the wave's period times the pitch envelope (8.8).
std::uint16_t Effects::period(const Channel& ch) const {
    if (ch.pitch.value == 0x10000) {
        return ch.base_period;
    }
    return std::uint16_t((std::uint32_t(ch.pitch.value >> 8 & 0xFFFF) * ch.base_period) >> 8);
}

// $122040: the wave's volume times the pan share and the volume envelope.
std::uint16_t Effects::volume(const Channel& ch) const {
    const std::uint32_t envelope = std::uint32_t(ch.volume.value) >> 8;
    const std::uint32_t scale = ch.pan_scale;
    if (envelope == 0x100) {
        return scale == 0x100 ? ch.base_volume : std::uint16_t((ch.base_volume * scale) >> 8);
    }
    const std::uint32_t panned = scale == 0x100 ? ch.base_volume : (ch.base_volume * scale) >> 8;
    return std::uint16_t(((panned & 0xFFFF) * (envelope & 0xFFFF)) >> 8);
}

// $121F30: every tick all envelopes step; each sound counts down its
// duration (ending at 0) and sends changed periods and volumes. Periods
// under $72 are sent as $48.
void Effects::tick() {
    for (auto& ch : channels_) {
        step(ch.volume);
        step(ch.pitch);
    }
    for (int s = 0; s < 4; ++s) {
        auto& slot = slots_[std::size_t(s)];
        if (!slot.active) {
            continue;
        }
        if (slot.duration == 0) {
            stop_slot(s);
            continue;
        }
        for (int c = 0; c < 4; ++c) {
            if ((slot.mask >> c & 1) == 0) {
                continue;
            }
            auto& ch = channels_[std::size_t(c)];
            std::uint16_t p = period(ch);
            const std::uint16_t v = volume(ch);
            if (v != ch.last_volume || p != ch.last_period) {
                if (p < 0x72) {
                    p = 0x48;
                }
                ch.last_period = p;
                ch.last_volume = v;
                paula_.set_period_volume(c, p, v);
            }
        }
        --slot.duration;
    }
}

} // namespace marble_madness::sound
