#include "wizball_c64/game/sound.hpp"

namespace wizball::game {
namespace {
constexpr int hum = 0x18;          // $4553: the effect re-rolled on each start
constexpr int busy_fallback = 0x10; // effect 0 on a busy slot plays $10 instead
} // namespace

Sound::Sound(const data::GameImage& image, RandomSource& random) : image_(image), random_(random), music_(image) {
    for (int e = 0; e < int(records_.size()); ++e) {
        const auto address = image.word(std::uint16_t(0xBACA + 2 * e));
        const auto bytes = image.bytes(address, 25);
        records_[std::size_t(e)].assign(bytes.begin(), bytes.end());
    }
    last_effect_ = image.byte(0xBB02);
    toggle_ = image.byte(0xBB03);
    fade_level_ = image.byte(0x45CF);
}

void Sound::set_slot(int slot, const std::uint8_t* bytes) {
    std::vector<std::uint8_t> keep(bytes, bytes + 25);
    std::swap(records_[0], keep);
    slots_[std::size_t(slot)] = read_record(0);
    std::swap(records_[0], keep);
}

Sound::Slot Sound::read_record(int number) const {
    const auto& r = records_[std::size_t(number)];
    Slot s;
    s.voice = r[0];
    s.priority = r[1];
    s.gate1 = r[2];
    s.gate2 = r[3];
    s.waveform = r[4];
    s.attack_decay = r[5];
    s.started = r[6];
    s.frequency = std::uint16_t(r[7] | r[8] << 8);
    s.frequency_step = std::uint16_t(r[9] | r[10] << 8);
    s.up_time = r[11];
    s.down_time = r[12];
    s.pulse = std::uint16_t(r[13] | r[14] << 8);
    s.pulse_step = std::uint16_t(r[15] | r[16] << 8);
    s.pulse_up = r[17];
    s.pulse_down = r[18];
    s.rising = r[19];
    s.count = r[20];
    s.pulse_rising = r[21];
    s.pulse_count = r[22];
    s.sustain_release = r[23];
    s.spare = r[24];
    return s;
}

void Sound::start_effect(int number) {
    for (;;) {
        if (number == hum) {
            auto& r = records_[hum];
            for (int i = 12; i >= 7; --i) r[std::size_t(i)] = random_.next(); // $455B: $3EAB down to $3EA6
        }
        last_effect_ = std::uint8_t(number);
        if (number == 0) { // $456E: the shot sound alternates between effects 0 and $10 (not while a tune plays)
            toggle_ ^= 0x10;
            number = music_mode_ == 0 ? toggle_ : 0;
        }
        if (number >= int(records_.size())) return;
        const auto record = read_record(number);
        const int slot = image_.byte(std::uint16_t(0xBA7A + record.voice)) / 0x20;
        if (slot < 0 || slot > 2) return;
        auto& current = slots_[std::size_t(slot)];
        if (current.gate1 != 0 && record.priority > current.priority) {
            if (last_effect_ == 0) {
                number = busy_fallback;
                continue;
            }
            return;
        }
        current = record;
        return;
    }
}

void Sound::silence() {
    for (int r = 0; r < 0x18; ++r) sid_.write(r, 0);
    for (auto& s : slots_) {
        s.gate1 = 0;
        s.gate2 = 0;
    }
}

void Sound::tick(int index, int v) {
    auto& s = slots_[std::size_t(index)];
    if (s.started == 0) {
        s.started = 1;
        sid_.write(v + 4, 0);
    }
    // Frequency: up for up_time frames, down for down_time, and so on.
    if (s.rising == 0) {
        if (s.count == 0) {
            s.count = s.up_time;
            s.rising = 1;
            --s.count;
            s.frequency = std::uint16_t(s.frequency + s.frequency_step);
        } else {
            --s.count;
            s.frequency = std::uint16_t(s.frequency - s.frequency_step);
        }
    } else if (s.count != 0) {
        --s.count;
        s.frequency = std::uint16_t(s.frequency + s.frequency_step);
    } else {
        s.count = s.down_time;
        s.rising = 0;
        --s.count;
        s.frequency = std::uint16_t(s.frequency - s.frequency_step);
    }
    if (s.pulse_rising == 0) {
        if (s.pulse_count == 0) {
            s.pulse_count = s.pulse_up;
            s.pulse_rising = 1;
            --s.pulse_count;
            s.pulse = std::uint16_t(s.pulse + s.pulse_step);
        } else {
            --s.pulse_count;
            s.pulse = std::uint16_t(s.pulse - s.pulse_step);
        }
    } else if (s.pulse_count != 0) {
        --s.pulse_count;
        s.pulse = std::uint16_t(s.pulse + s.pulse_step);
    } else {
        s.pulse_count = s.pulse_down;
        s.pulse_rising = 0;
        --s.pulse_count;
        s.pulse = std::uint16_t(s.pulse - s.pulse_step);
    }
    sid_.write(v + 5, s.attack_decay);
    sid_.write(v + 6, s.sustain_release);
    sid_.write(v + 0, std::uint8_t(s.frequency));
    sid_.write(v + 1, std::uint8_t(s.frequency >> 8));
    sid_.write(v + 2, std::uint8_t(s.pulse));
    sid_.write(v + 3, std::uint8_t(s.pulse >> 8));
    std::uint8_t control = 0;
    if (s.gate1 != 0) {
        --s.gate1;
        control = std::uint8_t(s.waveform | 1);
    } else if (s.gate2 != 0) {
        --s.gate2;
        control = std::uint8_t(s.waveform | 1);
    }
    sid_.write(v + 4, control);
}

void Sound::frame() {
    // $45D0: the volume fades in or out by one a frame.
    if (fade_direction_ & 0x80) {
        if (fade_level_ == 0) fade_direction_ = 0;
        else --fade_level_;
    } else if (fade_direction_ != 0) {
        if (fade_level_ == 0x0F) fade_direction_ = 0;
        else ++fade_level_;
    }
    sid_.write(0x18, std::uint8_t(fade_level_ | music_.filter_mode_bits()));
    if (music_mode_ == 0) {
        tick(2, 0x0E);
        tick(1, 0x07);
        tick(0, 0x00);
        return;
    }
    if (music_mode_ == 1) {
        tick(1, 0x07);
        tick(0, 0x00);
    }
    music_tick();
}

void Sound::music_tick() {
    // $B925
    if (music_mode_ == 0) return;
    music_.tick(sid_, music_mode_ == 1 ? 2 : 0);
    sid_.write(0x18, std::uint8_t(fade_level_ | music_.filter_mode_bits()));
}

void Sound::extra_interrupt(std::uint16_t handler) {
    const bool tick = handler == 0xB8FC ? music_speed_ != 0 : music_speed_ == 2;
    if (tick) music_tick();
}

void Sound::start_tune(int tune, int mode, int speed) {
    music_mode_ = std::uint8_t(mode);
    if (speed >= 0) music_speed_ = std::uint8_t(speed);
    music_.start(tune);
}

void Sound::check_tune() {
    if (music_.third_voice_playing()) return;
    music_mode_ = 0;
    music_.set_filter_mode_bits(0);
    sid_.write(0x17, 0);
}

} // namespace wizball::game
