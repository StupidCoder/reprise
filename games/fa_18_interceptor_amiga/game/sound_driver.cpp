#include "fa_18_interceptor_amiga/game/sound_driver.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace interceptor::game {
namespace {

constexpr std::uint32_t scripts_at = 0x145F50, scripts_end = 0x1460A8;
// Where each effect script starts ($109A92, $109AD6, $10A222, $109B8A, $109B26).
constexpr std::uint32_t script_addresses[] = {0x145F50, 0x145FB0, 0x145FD8, 0x146008, 0x146048};
constexpr std::uint32_t shift_register_at = 0x145F3C; // its first value
constexpr int lowest_period = 0x7C;

const data::Sound* find_sound(const data::Assets& assets, const char* file) {
    for (const auto& s : assets.sounds)
        if (s.file == file && !s.samples.empty()) return &s;
    return nullptr;
}

} // namespace

SoundDriver::SoundDriver(const data::Assets& assets) {
    const auto& program = assets.program;
    shift_register_ = program.u32(shift_register_at);

    // The scripts: slot fields by their offset in the original's slot
    // record ($1454F2), the wait, and the loops (counter + $40).
    for (std::uint32_t a = scripts_at; a < scripts_end; a += 8) {
        const std::uint32_t field = program.u32(a);
        const std::int32_t value = program.s32(a + 4);
        Entry::Kind kind{};
        switch (field) {
        case 0x08: kind = Entry::set_period; break;
        case 0x0C: kind = Entry::set_volume; break;
        case 0x18: kind = Entry::set_period_step; break;
        case 0x1C: kind = Entry::set_volume_step; break;
        case 0x24: kind = Entry::set_loop0; break;
        case 0x28: kind = Entry::set_loop1; break;
        case 0x2C: kind = Entry::wait; break;
        case 0x40: kind = Entry::loop0; break;
        case 0x44: kind = Entry::loop1; break;
        default: throw std::runtime_error("F/A-18 sound script: unknown field " + std::to_string(field));
        }
        entries_.push_back({kind, value});
    }
    for (int s = 0; s < script_count; ++s) script_start_[std::size_t(s)] = int((script_addresses[s] - scripts_at) / 8);

    // A new slot ($145784): the samples, period $166, volume 63, played once.
    const auto add = [&](int n, std::span<const std::int8_t> samples) {
        auto& s = slots_[std::size_t(n)];
        s = Slot{};
        s.present = true;
        s.data = std::uint32_t(memory_.size());
        s.length = std::uint32_t(samples.size());
        s.period = 0x166 << 16;
        s.volume = 0x3F << 16;
        memory_.insert(memory_.end(), samples.begin(), samples.end());
    };
    const auto load = [&](const char* file, int n) {
        const auto* sound = find_sound(assets, file);
        if (!sound) return false;
        add(n, sound->samples);
        return true;
    };

    // $109660: text201 and a copy, both looping, a little apart in pitch.
    if (load("text/text201", 35) && copy(35, 36)) {
        slots_[35].repeat = slots_[36].repeat = -1;
        slots_[35].period = 0xB3 << 16;
        slots_[36].period = 0xB5 << 16;
    }
    // $1099CA, the flight sounds.
    if ((load("text/textegn", 2) || load("text/textegn2", 2)) && copy(2, 3)) {
        slots_[2].repeat = slots_[3].repeat = -1;
        slots_[3].data += 0x370;
        slots_[3].length -= 0x370;
    }
    {
        // Slot 4: a square wave of 32 samples ($145E20), the beeps' voice.
        std::vector<std::int8_t> square(0x20);
        std::fill(square.begin(), square.begin() + 0x10, std::int8_t(0x7F));
        std::fill(square.begin() + 0x10, square.end(), std::int8_t(-0x7E));
        add(4, square);
        slots_[4].script = script_start_[beep_script];
        slots_[4].repeat = -1;
    }
    {
        // Slot 6: 2048 bytes of noise from the shift register ($145EFE).
        std::vector<std::int8_t> noise(0x800);
        for (auto& b : noise) b = std::int8_t(random_bits(8));
        add(6, noise);
        slots_[6].repeat = -1;
        slots_[6].script = script_start_[noise_script];
    }
    load("text/texttre", 5);
    if (slots_[6].present && copy(6, 10)) {
        slots_[10].script = script_start_[gun_script];
        slots_[10].period = 0x96 << 16;
    }
    if (load("text/textger", 11)) slots_[11].repeat = -1;
    if (slots_[2].present && copy(2, 12)) slots_[12].script = script_start_[thump_script];
    if (load("text/textcpt", 0) && copy(0, 1)) {
        slots_[0].repeat = slots_[1].repeat = -1;
        slots_[1].length = 0x15CC;
    }
    if (load("text/textwnd", 8) && copy(8, 9)) {
        slots_[9].repeat = slots_[8].repeat = -1;
        slots_[9].data += 0x792;
        // (The loader takes the length from slot 3, the engine's copy, not
        // from slot 8: kept, it is what the game plays.)
        slots_[9].length = slots_[3].length - 0x792;
    }

    // $1096BA: the title tune, pieces chained slot to slot (only with every
    // piece on the disk). Slot n plays n + 2 after its repeats; channel 0
    // takes the odd slots, channel 1 the copies; 33/34 go back to 17/18.
    {
        bool all = load("text/texti0a", 13) && copy(13, 14) && load("text/texti0b", 15) && copy(15, 16) &&
                   load("text/texti1", 17) && copy(17, 18) && load("text/texti2", 19) && copy(19, 20);
        all = all && copy(17, 21) && copy(17, 22) && copy(19, 23) && copy(19, 24);
        all = all && load("text/texti3", 25) && copy(25, 26) && load("text/texti4", 27) && copy(27, 28);
        all = all && copy(25, 29) && copy(25, 30) && copy(27, 31) && copy(27, 32);
        all = all && load("text/texti5", 33) && copy(33, 34);
        if (all) {
            for (int n = 13; n <= 34; ++n) {
                auto& s = slots_[std::size_t(n)];
                s.repeat = s.repeat_reload = 0;
                s.next = n + 2;
            }
            const auto repeats = [&](int n, int count) {
                slots_[std::size_t(n)].repeat = slots_[std::size_t(n)].repeat_reload = count;
            };
            repeats(13, 13), repeats(14, 13), repeats(15, 1), repeats(16, 1);
            for (int n : {17, 18, 21, 22, 25, 26, 29, 30}) repeats(n, 2);
            repeats(33, 3), repeats(34, 3);
            slots_[33].next = 17;
            slots_[34].next = 18;
            tune_ = true;
        } else {
            for (int n = 13; n <= 34; ++n) slots_[std::size_t(n)] = Slot{};
        }
    }

    paula_.set_memory(memory_);
    paula_.block_started = [this](int channel) { interrupt(channel); };
}

int SoundDriver::copy(int from, int to) {
    const auto& f = slots_[std::size_t(from)];
    if (!f.present) return 0;
    auto& t = slots_[std::size_t(to)];
    t = Slot{};
    t.present = true;
    t.data = f.data;
    t.length = f.length;
    t.period = f.period;
    t.volume = f.volume;
    t.repeat = f.repeat;
    t.period_step = f.period_step;
    t.volume_step = f.volume_step;
    t.next = f.next;
    t.wait = f.wait;
    t.period_ticks = f.period_ticks;
    t.volume_ticks = f.volume_ticks;
    return 1;
}

std::int32_t SoundDriver::random_bits(int bits) {
    std::int32_t value = 0;
    for (int i = 0; i < bits; ++i) {
        const std::uint32_t bit = ((shift_register_ >> 3) ^ shift_register_) & 1u;
        shift_register_ = (shift_register_ >> 1) | (bit << 31);
        value = std::int32_t(bit) + value * 2;
    }
    return value;
}

void SoundDriver::stop(int channel) {
    if (log) log->push_back({0x109C58, channel});
    cells_[std::size_t(channel)] = -1;
    interrupt(channel);
}

void SoundDriver::play(int n, int channel, int volume) {
    if (log) log->push_back({0x109C7C, n, channel, volume});
    if (!has(n)) return;
    stop(channel);
    slots_[std::size_t(n)].volume = volume << 16;
    cells_[std::size_t(channel)] = n;
    interrupt(channel);
}

void SoundDriver::start_script(int n, Script script) {
    auto& s = slots_[std::size_t(n)];
    s.script = script_start_[script];
    s.position = 0;
    s.wait = 1;
}

std::int32_t& SoundDriver::script_value(Script script, int entry) {
    return entries_[std::size_t(script_start_[script] + entry)].value;
}

// The audio interrupt runs one at a time: one raised while another runs
// (a channel's DMA switched on starts its block at once) waits for it.
void SoundDriver::interrupt(int channel) {
    if (in_interrupt_) {
        pending_[std::size_t(channel)] = true;
        return;
    }
    in_interrupt_ = true;
    audio_interrupt(channel);
    for (bool again = true; again;) {
        again = false;
        for (int c = 0; c < 4; ++c) {
            if (!pending_[std::size_t(c)]) continue;
            pending_[std::size_t(c)] = false;
            audio_interrupt(c);
            again = true;
        }
    }
    in_interrupt_ = false;
}

// $1453BC: the channel's next block.
void SoundDriver::audio_interrupt(int channel) {
    const int n = cells_[std::size_t(channel)];
    if (n < 0) {
        paula_.set_volume(channel, 0);
        paula_.set_period(channel, lowest_period);
        paula_.write_dmacon(std::uint16_t(1u << channel));
        return;
    }
    auto& s = slots_[std::size_t(n)];
    paula_.set_location(channel, s.data);
    paula_.set_length(channel, std::uint16_t((s.length >> 1) & 0xFFFF));
    paula_.write_dmacon(std::uint16_t(0x8000u | (1u << channel)));
    write_registers(channel, s);
    if (s.repeat != -1 && --s.repeat < 0) {
        cells_[std::size_t(channel)] = s.next;
        s.repeat = s.repeat_reload;
    }
}

// $1454C0: the period (at least $7C) and the volume (at most the master
// volume) of the slot's high words.
void SoundDriver::write_registers(int channel, const Slot& s) {
    const int period = std::int16_t(std::uint32_t(s.period) >> 16);
    const int volume = int(std::uint32_t(s.volume) >> 16) & 0x3F;
    paula_.set_period(channel, std::uint16_t(std::max(period, lowest_period)));
    paula_.set_volume(channel, std::uint16_t(std::min(volume, master_volume_)));
}

// $1454F2: when the wait runs out, the entries up to the next wait.
void SoundDriver::step_script(int channel, Slot& s) {
    if (s.wait == 0 || --s.wait != 0 || s.script < 0) return;
    int position = s.position;
    for (int guard = 0; guard < 256; ++guard) {
        const auto& e = entries_[std::size_t(s.script + position++)];
        switch (e.kind) {
        case Entry::set_period: s.period = e.value; break;
        case Entry::set_volume: s.volume = e.value; break;
        case Entry::set_period_step: s.period_step = e.value; break;
        case Entry::set_volume_step: s.volume_step = e.value; break;
        case Entry::set_loop0: s.loops[0] = e.value; break;
        case Entry::set_loop1: s.loops[1] = e.value; break;
        case Entry::wait:
            s.wait = e.value;
            s.position = position;
            if (e.value == 0) { // the end: the channel stops
                cells_[std::size_t(channel)] = -1;
                interrupt(channel);
            }
            return;
        case Entry::loop0:
        case Entry::loop1: {
            // The counter runs down; at 0 the script goes on, before that
            // it jumps (a counter already 0 jumps for ever).
            auto& counter = s.loops[e.kind == Entry::loop0 ? 0 : 1];
            if (counter != 0 && --counter == 0) break;
            position = e.value / 8;
            break;
        }
        }
    }
    s.wait = 0; // (a script that never waits: stopped)
}

// $11954C, from the vertical-blank server while the tune plays: the master
// volume a step of $4000 nearer its target.
void SoundDriver::set_master_volume(int volume) {
    master_ = std::clamp(volume, 0, 0x3F) << 16;
    master_volume_ = master_ >> 16;
}

void SoundDriver::fade_master() {
    if (!tune_playing_ || master_ == master_target_) return;
    if (master_ < master_target_) master_ = std::min(master_ + 0x4000, 0x3F0000);
    else master_ = std::max(master_ - 0x4000, 0);
    master_volume_ = master_ >> 16;
}

// $145438: per channel, the script, the registers, then the steps.
void SoundDriver::vertical_blank() {
    fade_master();
    master_volume_ = master_ >> 16;
    for (int c = 0; c < 4; ++c) {
        const int n = cells_[std::size_t(c)];
        if (n < 0) continue;
        auto& s = slots_[std::size_t(n)];
        step_script(c, s);
        write_registers(c, s);
        s.period += s.period_step;
        s.volume += s.volume_step;
        if (s.period_ticks != 0 && --s.period_ticks == 0) s.period_step = 0;
        if (s.volume_ticks != 0 && --s.volume_ticks == 0) s.volume_step = 0;
    }
}

void SoundDriver::render(std::span<float> stereo, int rate) {
    const std::size_t frames = stereo.size() / 2;
    std::size_t done = 0;
    while (done < frames) {
        if (to_blank_ <= 0) {
            vertical_blank();
            to_blank_ += rate / 50.0;
        }
        const std::size_t n = std::min(frames - done, std::size_t(std::ceil(to_blank_)));
        paula_.render(stereo.subspan(done * 2, n * 2), rate);
        done += n;
        to_blank_ -= double(n);
    }
}

} // namespace interceptor::game
