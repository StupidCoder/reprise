// The 3DS adventures' effect sequences. See a56_sequence.hpp.
#include "sound/a56_sequence.hpp"
#include <algorithm>
#include <cmath>

namespace picarat::adventure56 {

namespace {

constexpr float kSilentDb = -90.0f; // a releasing note below this is done

float square(int v) {
    const float f = float(std::clamp(v, 0, 127)) / 127.0f;
    return f * f;
}

// Hypothesis: seconds an envelope stage takes for a 0..127 setting
// (127 instant). Only a few regions use slower settings.
float stageSeconds(int value, float longest) {
    const float f = float(127 - std::clamp(value, 0, 127)) / 127.0f;
    return f * f * longest;
}

} // namespace

SequencePlayer::SequencePlayer(std::shared_ptr<const std::vector<std::uint8_t>> data,
                               std::size_t start, std::vector<Instrument> instruments)
    : data_(std::move(data)), instruments_(std::move(instruments)) {
    tracks_[0].active = true;
    tracks_[0].pc = start;
}

void SequencePlayer::release() {
    released_ = true;
    for (auto& t : tracks_) {
        t.active = false;
    }
    for (auto& n : notes_) {
        releaseNote(n);
    }
}

void SequencePlayer::releaseNote(Note& n) {
    if (n.phase != Note::Phase::Done) {
        n.phase = Note::Phase::Release;
    }
}

void SequencePlayer::startNote(Track& t, int key, int velocity, long length) {
    if (t.program < 0 || std::size_t(t.program) >= instruments_.size()) {
        return;
    }
    const Instrument& in = instruments_[std::size_t(t.program)];
    if (!in.wave || in.wave->frames() == 0) {
        return;
    }
    Note n;
    n.instrument = &in;
    n.length = length > 0 ? length : -1;
    n.step = double(in.pitch) * std::exp2(double(key + t.transpose - in.originalKey) / 12.0);
    const float amplitude = square(velocity) * square(t.volume) * square(in.volume);
    // Square-root pan law: the centre plays at 1/sqrt(2) on each side
    // (matches the console's effect levels in puzzle 1's recording).
    const float pan =
        std::clamp(float(t.pan - 64) / 63.0f + float(in.pan - 64) / 63.0f, -1.0f, 1.0f);
    n.left = amplitude * std::sqrt((1 - pan) / 2);
    n.right = amplitude * std::sqrt((1 + pan) / 2);
    if (in.attack >= 127) {
        n.level = 1;
        n.phase = Note::Phase::Decay;
    }
    notes_.push_back(n);
}

void SequencePlayer::step(Track& t) {
    const auto& d = *data_;
    auto u8 = [&]() -> int {
        return t.pc < d.size() ? d[t.pc++] : 0xff;
    };
    auto u16 = [&]() {
        const int hi = u8();
        return hi << 8 | u8();
    };
    auto u24 = [&]() {
        const int hi = u16();
        return std::size_t(hi) << 8 | std::size_t(u8());
    };
    auto varlen = [&]() {
        long v = 0;
        for (int i = 0; i < 4; ++i) {
            const int b = u8();
            v = v << 7 | (b & 0x7f);
            if (!(b & 0x80)) {
                break;
            }
        }
        return v;
    };
    // Commands until the track waits or ends.
    for (int guard = 0; t.active && t.wait <= 0 && guard < 1000; ++guard) {
        if (t.pc >= d.size()) {
            t.active = false;
            break;
        }
        const int op = u8();
        if (op < 0x80) {
            const int velocity = u8();
            const long length = varlen();
            startNote(t, op, velocity, length);
            if (t.noteWait) {
                t.wait = length;
            }
            continue;
        }
        switch (op) {
        case 0x80:
            t.wait = varlen();
            break;
        case 0x81:
            t.program = int(varlen());
            break;
        case 0x88: {
            const int n = u8();
            const std::size_t at = u24();
            if (n >= 0 && n < int(tracks_.size()) && &tracks_[std::size_t(n)] != &t) {
                Track& o = tracks_[std::size_t(n)];
                o = Track{};
                o.active = true;
                o.pc = at;
            }
            break;
        }
        case 0x89:
            t.pc = u24();
            break;
        case 0x8a: {
            const std::size_t at = u24();
            if (t.calls.size() < 8) {
                t.calls.push_back(t.pc);
                t.pc = at;
            }
            break;
        }
        case 0xb0:
            timebase_ = std::max(1, u8());
            break;
        case 0xc0:
            t.pan = u8();
            break;
        case 0xc1:
            t.volume = u8();
            break;
        case 0xc3:
            t.transpose = int(std::int8_t(u8()));
            break;
        case 0xc7:
            t.noteWait = u8() != 0;
            break;
        case 0xd4:
            t.loops.push_back({t.pc, u8()});
            break;
        case 0xe1:
            tempo_ = std::max(1, u16());
            break;
        case 0xfc:
            if (!t.loops.empty()) {
                auto& l = t.loops.back();
                if (l.count == 0 || --l.count > 0) {
                    t.pc = l.pc;
                } else {
                    t.loops.pop_back();
                }
            }
            break;
        case 0xfd:
            if (t.calls.empty()) {
                t.active = false;
            } else {
                t.pc = t.calls.back();
                t.calls.pop_back();
            }
            break;
        case 0xfe:
            u16();
            break;
        case 0xff:
            t.active = false;
            break;
        default:
            if (op >= 0xb0 && op < 0xe0) {
                u8(); // other one-byte settings
            } else if (op >= 0xe0 && op < 0xf0) {
                u16(); // other two-byte settings
            } else {
                t.active = false; // not used by the effects
            }
            break;
        }
    }
}

void SequencePlayer::tick() {
    for (auto& t : tracks_) {
        if (t.active) {
            step(t);
        }
    }
    for (auto& t : tracks_) {
        if (t.active && t.wait > 0) {
            --t.wait;
        }
    }
    for (auto& n : notes_) {
        if (n.length > 0 && --n.length == 0) {
            releaseNote(n);
        }
    }
}

void SequencePlayer::mixNote(Note& n, float* mix, int frames, int rate, float gain) {
    const Instrument& in = *n.instrument;
    const Pcm& p = *in.wave;
    const std::size_t total = p.frames();
    const double step = n.step * double(p.rate) / double(rate);
    // Envelope steps per output frame.
    const float attackSeconds = stageSeconds(in.attack, 2.0f);
    const float attackStep = attackSeconds > 0 ? 1.0f / (attackSeconds * float(rate)) : 1.0f;
    const float sustain = square(in.sustain);
    auto dbStep = [&](int value) {
        const float s = stageSeconds(value, 4.0f);
        return s > 0 ? -kSilentDb / (s * float(rate)) : 1000.0f;
    };
    const float decayDb = dbStep(in.decay), releaseDb = dbStep(in.release);
    const std::size_t c = std::size_t(p.channels);
    for (int i = 0; i < frames && n.phase != Note::Phase::Done; ++i) {
        if (n.position >= double(total)) {
            if (p.loop) {
                n.position = double(p.loopStart) +
                             std::fmod(n.position - double(total), double(total - p.loopStart));
            } else {
                n.phase = Note::Phase::Done;
                break;
            }
        }
        switch (n.phase) {
        case Note::Phase::Attack:
            n.level += attackStep;
            if (n.level >= 1) {
                n.level = 1;
                n.phase = Note::Phase::Decay;
            }
            break;
        case Note::Phase::Decay: {
            const float db = 20 * std::log10(std::max(n.level, 1e-6f)) - decayDb;
            n.level = std::pow(10.0f, db / 20);
            if (n.level <= sustain) {
                n.level = sustain;
                n.phase = Note::Phase::Sustain;
            }
            break;
        }
        case Note::Phase::Sustain:
            break;
        case Note::Phase::Release: {
            const float db = 20 * std::log10(std::max(n.level, 1e-6f)) - releaseDb;
            if (db <= kSilentDb) {
                n.phase = Note::Phase::Done;
                continue;
            }
            n.level = std::pow(10.0f, db / 20);
            break;
        }
        case Note::Phase::Done:
            break;
        }
        const std::size_t at = std::size_t(n.position);
        const float t = float(n.position - double(at));
        const std::size_t next = at + 1 < total ? at + 1 : (p.loop ? p.loopStart : at);
        // Mono waves; a stereo wave is mixed down.
        float s = p.samples[at * c] * (1 - t) + p.samples[next * c] * t;
        if (c == 2) {
            s = (s + p.samples[at * c + 1] * (1 - t) + p.samples[next * c + 1] * t) / 2;
        }
        s *= n.level * gain / 32768.0f;
        mix[std::size_t(i) * 2] += s * n.left;
        mix[std::size_t(i) * 2 + 1] += s * n.right;
        n.position += step;
    }
}

bool SequencePlayer::render(float* mix, int frames, int rate, float gain) {
    // Ticks happen between output frames: render up to the next tick, run
    // it, continue.
    int done = 0;
    while (done < frames) {
        if (tickClock_ <= 0 && !released_) {
            tick();
            tickClock_ += 60.0 / (double(tempo_) * double(timebase_));
        }
        const int n = released_
                          ? frames - done
                          : std::clamp(int(std::ceil(tickClock_ * double(rate))), 1, frames - done);
        for (auto& note : notes_) {
            mixNote(note, mix + std::size_t(done) * 2, n, rate, gain);
        }
        tickClock_ -= double(n) / double(rate);
        done += n;
    }
    notes_.erase(std::remove_if(notes_.begin(), notes_.end(),
                                [](const Note& n) { return n.phase == Note::Phase::Done; }),
                 notes_.end());
    const bool tracksRunning =
        std::any_of(tracks_.begin(), tracks_.end(), [](const Track& t) { return t.active; });
    return tracksRunning || !notes_.empty();
}

} // namespace picarat::adventure56
