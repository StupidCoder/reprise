// The ARM7 sound driver's sequencer. See sdat_sequencer.hpp.
#include "sound/sdat_sequencer.hpp"
#include "system/resources.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace picarat::sdat {

namespace {

constexpr std::uint32_t kProgramBase = 0x02380000;
constexpr std::uint32_t kAutoloadParameters = 0x023801d0; // list start, end, source
constexpr int kSilent = -723;                             // centibels: -72.3 dB
constexpr std::int32_t kInfinite = -1;

// Reads `size` bytes at `address` of the ARM7 program after its start-up code
// has copied the autoload segments (02380118).
std::span<const std::uint8_t> programBytes(std::span<const std::uint8_t> program,
                                           std::uint32_t address, std::size_t size) {
    auto at = [&](std::uint32_t a) {
        return read32(program, a - kProgramBase);
    };
    std::uint32_t list = at(kAutoloadParameters), end = at(kAutoloadParameters + 4);
    std::uint32_t source = at(kAutoloadParameters + 8) - kProgramBase;
    for (; list < end; list += 12) {
        const std::uint32_t start = at(list), length = at(list + 4);
        if (address >= start && address + size <= start + length) {
            return program.subspan(source + (address - start), size);
        }
        source += length;
    }
    if (address >= kProgramBase && address - kProgramBase + size <= program.size()) {
        return program.subspan(address - kProgramBase, size);
    }
    throw std::runtime_error("address outside the ARM7 program");
}

} // namespace

DriverTables DriverTables::fromProgram(std::span<const std::uint8_t> program) {
    DriverTables t;
    const auto decibels = programBytes(program, 0x03806e84, 256);
    for (unsigned i = 0; i < 128; ++i) {
        t.decibels[i] = std::int16_t(read16(decibels, i * 2));
    }
    const auto attack = programBytes(program, 0x03807098, t.attack.size());
    std::copy(attack.begin(), attack.end(), t.attack.begin());
    const auto sine = programBytes(program, 0x03806e60, t.sine.size());
    std::transform(sine.begin(), sine.end(), t.sine.begin(), [](auto b) { return std::int8_t(b); });
    const auto order = programBytes(program, 0x03807088, 16);
    std::copy(order.begin(), order.end(), t.channelOrder.begin());
    const auto shift = programBytes(program, 0x03807084, 4);
    std::copy(shift.begin(), shift.end(), t.volumeShift.begin());
    t.randomSeed = read32(programBytes(program, 0x03807404, 4), 0);
    return t;
}

std::uint16_t pitchCurve(unsigned i) {
    // Matches the table the reference emulator's BIOS returns, all 768 steps.
    return std::uint16_t(std::lround((std::exp2(double(i) / 768.0) - 1.0) * 65536.0));
}

std::uint8_t volumeCurve(unsigned i) {
    // 128 x 10^((i - 723) / 200) times the divider of its range (16 below
    // -24 dB, 4 below -12 dB, 2 below -6 dB), rounded and capped at 127.
    // Steps 602, 662 and 722 hold 126 in the table the reference emulator's
    // BIOS returns (measured; one below the rounded value).
    if (i == 602 || i == 662 || i == 722) {
        return 126;
    }
    const double scale = i < 483 ? 16.0 : i < 603 ? 4.0 : i < 663 ? 2.0 : 1.0;
    const long v = std::lround(128.0 * std::pow(10.0, (double(i) - 723.0) / 200.0) * scale);
    return std::uint8_t(std::min(127L, v));
}

std::optional<Instrument> findInstrument(std::span<const std::uint8_t> bank, int program, int key) {
    if (program < 0 || bank.size() < 0x3c || std::uint32_t(program) >= read32(bank, 0x38)) {
        return std::nullopt;
    }
    const std::uint32_t record = read32(bank, 0x3c + std::size_t(program) * 4);
    const std::size_t offset = record >> 8;
    auto entry = [&](std::size_t at, std::uint8_t type) -> std::optional<Instrument> {
        if (at + 10 > bank.size()) {
            return std::nullopt;
        }
        return Instrument{type,         read16(bank, at), read16(bank, at + 2),
                          bank[at + 4], bank[at + 5],     bank[at + 6],
                          bank[at + 7], bank[at + 8],     bank[at + 9]};
    };
    switch (record & 0xff) {
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
        return entry(offset, std::uint8_t(record));
    case 0x10: { // drum set: low key, high key, a 12-byte entry per key
        if (offset + 2 > bank.size() || key < bank[offset] || key > bank[offset + 1]) {
            return std::nullopt;
        }
        const std::size_t at = offset + 2 + std::size_t(key - bank[offset]) * 12;
        return at + 12 <= bank.size() ? entry(at + 2, bank[at]) : std::nullopt;
    }
    case 0x11: { // key split: eight top keys, then a 12-byte entry per range
        unsigned i = 0;
        while (offset + i < bank.size() && bank[offset + i] < key) {
            if (++i > 7) {
                return std::nullopt;
            }
        }
        const std::size_t at = offset + 8 + i * 12;
        return at + 12 <= bank.size() ? entry(at + 2, bank[at]) : std::nullopt;
    }
    default:
        return std::nullopt;
    }
}

Sequencer::Sequencer(DsSound& hardware, DriverTables tables)
    : hardware_(hardware), tables_(tables), random_(tables.randomSeed) {
    // 037ff164 / 037ffef0 / 020a09ac.
    for (unsigned i = 0; i < 16; ++i) {
        channels_[i].index = i;
        players_[i].tracks.fill(0xff);
        variables_[i].fill(-1);
    }
    globals_.fill(-1);
}

// --- commands -------------------------------------------------------------

void Sequencer::prepare(unsigned player, std::span<const std::uint8_t> code, std::uint32_t offset,
                        const LoadedBank* bank) {
    building_.push_back({Command::Prepare, player, offset, 0, 0, 0, code, bank});
}

void Sequencer::start(unsigned player) {
    post(Command::Start, player);
}

void Sequencer::stop(unsigned player) {
    post(Command::Stop, player);
}

void Sequencer::pause(unsigned player, bool on) {
    post(Command::Pause, player, on);
}

void Sequencer::setPlayerPriority(unsigned player, std::uint8_t priority) {
    post(Command::Priority, player, priority);
}

void Sequencer::setPlayerVolume(unsigned player, std::int16_t centibels) {
    post(Command::Volume, player, std::uint32_t(std::uint16_t(centibels)));
}

void Sequencer::setTrackChannels(unsigned player, std::uint16_t tracks, std::uint16_t channels) {
    post(Command::Channels, player, tracks, channels);
}

void Sequencer::muteTracks(unsigned player, std::uint16_t tracks, unsigned mode) {
    post(Command::Mute, player, tracks, mode);
}

void Sequencer::setVariable(unsigned player, unsigned index, std::int16_t value) {
    post(Command::Variable, player, index, std::uint32_t(std::uint16_t(value)));
}

void Sequencer::setGlobalVariable(unsigned index, std::int16_t value) {
    post(Command::Global, 0, index, std::uint32_t(std::uint16_t(value)));
}

void Sequencer::lockChannels(std::uint32_t channels, bool lockedOnly) {
    post(Command::Lock, 0, channels, lockedOnly);
}

void Sequencer::unlockChannels(std::uint32_t channels, bool lockedOnly) {
    post(Command::Unlock, 0, channels, lockedOnly);
}

void Sequencer::stopUsingCode(std::span<const std::uint8_t> code) {
    building_.push_back({Command::StopCode, 0, 0, 0, 0, 0, code, nullptr});
}

void Sequencer::stopUsingBank(const LoadedBank* bank) {
    building_.push_back({Command::StopBank, 0, 0, 0, 0, 0, {}, bank});
}

void Sequencer::stopUsingWaves(std::span<const std::uint8_t> waves) {
    building_.push_back({Command::StopWaves, 0, 0, 0, 0, 0, waves, nullptr});
}

void Sequencer::setupWave(unsigned channel, unsigned format, std::span<const std::uint8_t> data,
                          bool loop, unsigned loopStart, unsigned length, unsigned volume,
                          unsigned shift, unsigned timer, unsigned pan) {
    // 0209ff9c packs these into command 0x0e.
    building_.push_back({Command::SetupWave, channel, format | (loop ? 1u : 2u) << 8 | shift << 12,
                         volume | pan << 8 | timer << 16, loopStart, length, data, nullptr});
}

void Sequencer::startDirect(std::uint32_t channels, std::uint32_t alarms) {
    post(Command::StartDirect, 0, channels, alarms);
}

void Sequencer::stopDirect(std::uint32_t channels, std::uint32_t alarms) {
    post(Command::StopDirect, 0, channels, alarms);
}

void Sequencer::setupAlarm(unsigned id, std::uint32_t tick, std::uint32_t period) {
    post(Command::SetupAlarm, id, tick, period);
}

void Sequencer::setDirectVolume(std::uint32_t channels, unsigned volume, unsigned shift) {
    post(Command::DirectVolume, 0, channels, volume | shift << 8);
}

void Sequencer::flush() {
    if (building_.empty()) {
        return;
    }
    for (auto& c : building_) {
        commands_.push_back(c);
    }
    building_.clear();
    commands_.push_back({Command::EndBatch, 0, 0, 0, 0, 0, {}, nullptr});
    ++batchesSent_;
}

void Sequencer::execute(const Command& c) {
    // 03801f1c.
    Player& player = players_[c.player & 15];
    switch (c.kind) {
    case Command::Prepare:
        preparePlayer(c.player & 15, c.code, c.a, c.bank);
        break;
    case Command::Start:
        player.started = true;
        break;
    case Command::Stop:
        stopPlayer(c.player & 15);
        break;
    case Command::Pause:
        // 0380039c: pausing releases every note (rate 127) and lets go of
        // the channels.
        player.paused = c.a != 0;
        if (player.paused) {
            for (unsigned s = 0; s < 16; ++s) {
                if (Track* t = track(player, s)) {
                    releaseTrack(*t, player, 127);
                    freeTrackChannels(*t);
                }
            }
        }
        break;
    case Command::Priority:
        player.priority = std::uint8_t(c.a);
        break;
    case Command::Volume:
        player.volumeOffset = std::int16_t(c.a);
        break;
    case Command::Channels:
        // 0380054c.
        for (unsigned s = 0; s < 16; ++s) {
            if (c.a >> s & 1) {
                if (Track* t = track(player, s)) {
                    t->channelMask = std::uint16_t(c.b);
                    t->ownChannels = true;
                }
            }
        }
        break;
    case Command::Mute:
        for (unsigned s = 0; s < 16; ++s) {
            if (c.a >> s & 1) {
                if (Track* t = track(player, s)) {
                    muteTrack(*t, player, c.b);
                }
            }
        }
        break;
    case Command::Variable:
        variables_[c.player & 15][c.a & 15] = std::int16_t(c.b);
        break;
    case Command::Global:
        globals_[c.a & 15] = std::int16_t(c.b);
        break;
    case Command::Lock:
        // 037ffbe0: locked channels stop what they play and are left out of
        // allocation; `locked` ones only for tracks without a channel mask
        // of their own, reserved ones for all.
        for (unsigned i = 0; i < 16; ++i) {
            if ((c.a >> i & 1) && !(reserved_ >> i & 1)) {
                Channel& ch = channels_[i];
                if (ch.owner >= 0) {
                    releasedBy(ch, false);
                }
                hardware_.stopChannels(1u << i, false);
                ch.priority = 0;
                ch.owner = -1;
                ch.active = false;
                ch.startPending = false;
                ch.updates = 0;
            }
        }
        (c.b ? locked_ : reserved_) |= c.a;
        break;
    case Command::Unlock:
        (c.b ? locked_ : reserved_) &= ~c.a;
        break;
    case Command::StopCode: {
        auto inside = [&](const std::uint8_t* at) {
            return at >= c.code.data() && at <= c.code.data() + c.code.size();
        };
        for (unsigned p = 0; p < 16; ++p) {
            Player& pl = players_[p];
            if (!pl.active) {
                continue;
            }
            for (unsigned s = 0; s < 16; ++s) {
                const Track* t = track(pl, s);
                if (t && t->position < pl.code.size() && inside(pl.code.data() + t->position)) {
                    stopPlayer(p);
                    break;
                }
            }
        }
        break;
    }
    case Command::StopBank:
        for (unsigned p = 0; p < 16; ++p) {
            if (players_[p].active && players_[p].bank == c.bank) {
                stopPlayer(p);
            }
        }
        break;
    case Command::StopWaves:
        for (Channel& ch : channels_) {
            if (ch.active && ch.type == Channel::Wave && !ch.data.empty() &&
                ch.data.data() >= c.code.data() &&
                ch.data.data() <= c.code.data() + c.code.size()) {
                ch.active = false;
                hardware_.stopChannels(1u << ch.index, false);
            }
        }
        break;
    case Command::SetupWave: {
        // 037fe804: control (stopping the channel), timer, loop start,
        // length, source.
        if (beforeWrite) {
            beforeWrite(Write::Direct, c.player);
        }
        hardware_.setupPcm(c.player & 15, c.a & 3, c.code.data(), c.code.data() + c.code.size(),
                           (c.a >> 8) & 3, c.c, c.d, c.b & 0x7f, (c.a >> 12) & 3, c.b >> 16,
                           (c.b >> 8) & 0x7f);
        break;
    }
    case Command::StartDirect:
        // Inline in 03801f1c: the start bits, then the alarms (03801dac).
        if (beforeWrite) {
            beforeWrite(Write::Direct, 0);
        }
        hardware_.startChannels(c.a & 0xffff);
        for (unsigned i = 0; i < 8; ++i) {
            if ((c.b >> i & 1) && alarmChanged) {
                alarmChanged(i, true, alarms_[i].tick, alarms_[i].period);
            }
        }
        break;
    case Command::StopDirect:
        // The alarms (03801e68) first, then the channels (037fea20).
        for (unsigned i = 0; i < 8; ++i) {
            if ((c.b >> i & 1) && alarmChanged) {
                alarmChanged(i, false, 0, 0);
            }
        }
        if (beforeWrite) {
            beforeWrite(Write::Direct, 0);
        }
        hardware_.stopChannels(c.a & 0xffff, false);
        break;
    case Command::SetupAlarm:
        alarms_[c.player & 7] = {c.a, c.b};
        break;
    case Command::DirectVolume:
        if (beforeWrite) {
            beforeWrite(Write::Direct, 0);
        }
        hardware_.setVolume(c.a & 0xffff, c.b & 0x7f, (c.b >> 8) & 3);
        break;
    case Command::EndBatch:
        ++commandsDone_;
        break;
    }
}

// --- the thread's pass ------------------------------------------------------

void Sequencer::pass(bool tick) {
    // 037ff008.
    work_ = Work{};
    applyChannels();
    if (beforeCommands) {
        beforeCommands();
    }
    while (!commands_.empty() && commandsDone_ < arrived_) {
        if (beforeCommand) {
            const auto kind = commands_.front().kind;
            beforeCommand(kind == Command::SetupWave      ? Step::Setup
                          : kind == Command::StartDirect  ? Step::Start
                          : kind == Command::StopDirect   ? Step::Stop
                          : kind == Command::SetupAlarm   ? Step::Alarm
                          : kind == Command::DirectVolume ? Step::Volume
                                                          : Step::Other);
        }
        execute(commands_.front());
        commands_.pop_front();
        ++work_.commands;
    }
    tickPlayers(tick);
    updateChannels(tick);
    random();
}

void Sequencer::post(Command::Kind kind, unsigned player, std::uint32_t a, std::uint32_t b) {
    building_.push_back(Command{kind, player, a, b, 0, 0, {}, nullptr});
}

std::uint32_t Sequencer::random() {
    // 037feec4.
    random_ = random_ * 0x19660d + 0x3c6ef35f;
    return random_ >> 16;
}

std::int16_t Sequencer::variable(unsigned player, unsigned index) const {
    return index < 16 ? variables_[player & 15][index] : globals_[(index - 16) & 15];
}

std::int16_t* Sequencer::variablePointer(unsigned p, unsigned index) {
    // 03801840: 0..15 the player's own, 16..31 the global ones.
    return index < 16 ? &variables_[p][index] : &globals_[(index - 16) & 15];
}

// --- players and tracks -----------------------------------------------------

Sequencer::Track* Sequencer::track(const Player& player, unsigned slot) {
    // 03800a7c.
    return slot < 16 && player.tracks[slot] != 0xff ? &tracks_[player.tracks[slot]] : nullptr;
}

int Sequencer::allocateTrack() {
    // 03801884.
    for (int i = 0; i < 32; ++i) {
        if (!tracks_[std::size_t(i)].active) {
            tracks_[std::size_t(i)].active = true;
            return i;
        }
    }
    return -1;
}

void Sequencer::resetTrack(Track& t) {
    // 038008f0.
    const bool active = t.active;
    t = Track{};
    t.active = active;
}

void Sequencer::preparePlayer(unsigned p, std::span<const std::uint8_t> code, std::uint32_t offset,
                              const LoadedBank* bank) {
    // 038000b4.
    Player& player = players_[p];
    if (player.active) {
        closeTracks(player);
        player.active = false;
    }
    player.paused = false;
    player.bank = bank;
    player.code = code;
    player.tempo = 120;
    player.tempoRatio = 256;
    player.tickCounter = 240;
    player.volume = 127;
    player.volumeOffset = 0;
    player.priority = 64;
    player.tracks.fill(0xff);
    playerTicks_[p] = 0;
    variables_[p].fill(-1);
    const int first = allocateTrack();
    if (first < 0) {
        return;
    }
    Track& t = tracks_[std::size_t(first)];
    resetTrack(t);
    t.base = 0;
    t.position = offset;
    player.tracks[0] = std::uint8_t(first);
    // A sequence that starts with 0xfe names the other tracks it opens.
    if (read(player, t) == 0xfe) {
        unsigned mask = read(player, t);
        mask |= unsigned(read(player, t)) << 8;
        for (unsigned slot = 1; (mask >>= 1) != 0; ++slot) {
            if (mask & 1) {
                const int other = allocateTrack();
                if (other < 0) {
                    break;
                }
                resetTrack(tracks_[std::size_t(other)]);
                player.tracks[slot] = std::uint8_t(other);
            }
        }
    } else {
        --t.position;
    }
    player.active = true;
    player.started = false;
    activePlayers_ |= 1u << p;
}

void Sequencer::stopPlayer(unsigned p) {
    // 03800340.
    Player& player = players_[p];
    if (player.active) {
        closeTracks(player);
        player.active = false;
        activePlayers_ &= ~(1u << p);
    }
}

void Sequencer::closeTrack(Player& player, unsigned slot) {
    // 03800aa8.
    if (Track* t = track(player, slot)) {
        releaseTrack(*t, player, -1);
        freeTrackChannels(*t);
        t->active = false;
        player.tracks[slot] = 0xff;
    }
}

void Sequencer::closeTracks(Player& player) {
    // 03800b00.
    for (unsigned s = 0; s < 16; ++s) {
        closeTrack(player, s);
    }
}

void Sequencer::tickPlayers(bool tick) {
    // 037fff54.
    std::uint32_t running = 0;
    for (unsigned p = 0; p < 16; ++p) {
        Player& player = players_[p];
        if (!player.active) {
            continue;
        }
        if (player.started) {
            if (tick && !player.paused) {
                unsigned steps = 0;
                while (player.tickCounter >= 240) {
                    ++steps;
                    player.tickCounter = std::uint16_t(player.tickCounter - 240);
                }
                unsigned done = 0;
                ++work_.playerTicks;
                for (; done < steps; ++done) {
                    ++work_.steps;
                    if (step(p, true)) {
                        stopPlayer(p);
                        break;
                    }
                }
                playerTicks_[p] += done;
                player.tickCounter = std::uint16_t(
                    player.tickCounter + (unsigned(player.tempo) * player.tempoRatio >> 8));
            }
            for (unsigned s = 0; s < 16; ++s) {
                if (Track* t = track(player, s)) {
                    updateTrack(*t, player, true);
                }
            }
        }
        if (player.active) {
            running |= 1u << p;
        }
    }
    activePlayers_ = running;
}

std::uint8_t Sequencer::read(const Player& player, Track& t) {
    // 03800290 (the original reads through a 16-byte cache).
    const std::uint32_t at = t.position++;
    return at < player.code.size() ? player.code[at] : 0;
}

std::int32_t Sequencer::argument(const Player& player, unsigned p, Track& t, int type) {
    // 038007f0.
    switch (type) {
    case 0:
        return read(player, t);
    case 1: {
        const unsigned lo = read(player, t);
        return std::int32_t(lo | unsigned(read(player, t)) << 8);
    }
    case 2: { // variable length: 7 bits a byte, high bit set on all but the last
        std::uint32_t v = 0, b;
        do {
            b = read(player, t);
            v = (v << 7) | (b & 0x7f);
        } while (b & 0x80);
        return std::int32_t(v);
    }
    case 3: { // a random value between two signed words
        unsigned lo = read(player, t);
        const auto low = std::int16_t(lo | unsigned(read(player, t)) << 8);
        lo = read(player, t);
        const auto high = std::int16_t(lo | unsigned(read(player, t)) << 8);
        return std::int32_t((random() * std::uint32_t(high - low + 1)) >> 16) + low;
    }
    case 4: // a variable's value
        return *variablePointer(p, read(player, t));
    default:
        return 0;
    }
}

bool Sequencer::step(unsigned p, bool play) {
    // 03800cf0: one sequence step of every track; true when all have ended.
    Player& player = players_[p];
    bool running = false;
    for (unsigned slot = 0; slot < 16; ++slot) {
        Track* tp = track(player, slot);
        if (!tp) {
            continue;
        }
        Track& t = *tp;
        for (int c = t.channels; c >= 0; c = channels_[std::size_t(c)].nextInTrack) {
            Channel& ch = channels_[std::size_t(c)];
            if (ch.length > 0) {
                --ch.length;
            }
            if (!ch.autoSweep && ch.sweepCounter < ch.sweepLength) {
                ++ch.sweepCounter;
            }
        }
        if (t.noteEndWait) {
            if (t.channels >= 0) {
                running = true;
                continue;
            }
            t.noteEndWait = false;
        }
        if (t.wait > 0) {
            --t.wait;
        }
        if (t.wait > 0) {
            running = true;
            continue;
        }
        bool ended = false;
        while (t.wait == 0 && !t.noteEndWait) {
            bool doIt = true;
            int type = -1; // the argument type a prefix forces
            unsigned command = read(player, t);
            ++work_.commandsRead;
            if (command == 0xa2) { // execute only if the last comparison held
                command = read(player, t);
                doIt = t.condition;
            }
            if (command == 0xa0) { // random argument
                command = read(player, t);
                type = 3;
            }
            if (command == 0xa1) { // argument from a variable
                command = read(player, t);
                type = 4;
            }
            auto arg = [&](int normal) {
                return argument(player, p, t, type >= 0 ? type : normal);
            };
            if (!(command & 0x80)) { // a note: key, velocity, length
                const int velocity = read(player, t);
                const std::int32_t length = arg(2);
                if (!doIt) {
                    continue;
                }
                const int key = std::clamp(int(command) + t.transpose, 0, 127);
                if (!t.muted && play) {
                    noteOn(p, t, key, velocity, length);
                }
                t.portamentoKey = std::uint8_t(key);
                if (t.noteWait) {
                    t.wait = length;
                    if (length == 0) {
                        t.noteEndWait = true;
                    }
                }
                continue;
            }
            switch (command & 0xf0) {
            case 0x80: {
                const std::int32_t v = arg(2);
                if (doIt && command == 0x80) {
                    t.wait = v;
                } else if (doIt && command == 0x81 && v < 0x10000) {
                    t.program = std::uint16_t(v);
                }
                break;
            }
            case 0x90:
                if (command == 0x93) { // open a track at an offset
                    const unsigned number = read(player, t);
                    std::uint32_t offset = read(player, t);
                    offset |= std::uint32_t(read(player, t)) << 8;
                    offset |= std::uint32_t(read(player, t)) << 16;
                    Track* other = track(player, number);
                    if (doIt && other && other != &t) {
                        releaseTrack(*other, player, -1);
                        freeTrackChannels(*other);
                        other->base = t.base;
                        other->position = t.base + offset;
                    }
                } else if (command == 0x94 || command == 0x95) { // jump, call
                    std::uint32_t offset = read(player, t);
                    offset |= std::uint32_t(read(player, t)) << 8;
                    offset |= std::uint32_t(read(player, t)) << 16;
                    if (doIt && command == 0x94) {
                        t.position = t.base + offset;
                    } else if (doIt && t.depth < 3) {
                        t.stack[t.depth++] = t.position;
                        t.position = t.base + offset;
                    }
                }
                break;
            case 0xb0: { // variables: set, add, sub, mul, div, shift, random; compare
                const unsigned index = read(player, t);
                const auto v = std::int16_t(arg(1));
                std::int16_t* var = variablePointer(p, index);
                if (!doIt) {
                    break;
                }
                switch (command) {
                case 0xb0:
                    *var = v;
                    break;
                case 0xb1:
                    *var = std::int16_t(*var + v);
                    break;
                case 0xb2:
                    *var = std::int16_t(*var - v);
                    break;
                case 0xb3:
                    *var = std::int16_t(*var * v);
                    break;
                case 0xb4:
                    if (v != 0) {
                        *var = std::int16_t(*var / v);
                    }
                    break;
                case 0xb5:
                    *var = std::int16_t(v < 0 ? *var >> -v : *var << v);
                    break;
                case 0xb6: {
                    const int range = v < 0 ? -v : v;
                    auto r = std::int16_t((random() * std::uint32_t(range + 1)) >> 16);
                    *var = v < 0 ? std::int16_t(-r) : r;
                    break;
                }
                case 0xb8:
                    t.condition = *var == v;
                    break;
                case 0xb9:
                    t.condition = *var >= v;
                    break;
                case 0xba:
                    t.condition = *var > v;
                    break;
                case 0xbb:
                    t.condition = *var <= v;
                    break;
                case 0xbc:
                    t.condition = *var < v;
                    break;
                case 0xbd:
                    t.condition = *var != v;
                    break;
                default:
                    break;
                }
                break;
            }
            case 0xc0:
            case 0xd0: {
                const std::uint32_t v = std::uint32_t(arg(0));
                const auto b = std::uint8_t(v);
                if (!doIt) {
                    break;
                }
                switch (command) {
                case 0xc0:
                    t.pan = std::int8_t(b - 0x40);
                    break;
                case 0xc1:
                    t.volume = b;
                    break;
                case 0xc2:
                    player.volume = b;
                    break;
                case 0xc3:
                    t.transpose = std::int8_t(b);
                    break;
                case 0xc4:
                    t.pitchBend = std::int8_t(b);
                    break;
                case 0xc5:
                    t.bendRange = b;
                    break;
                case 0xc6:
                    t.priority = b;
                    break;
                case 0xc7:
                    t.noteWait = v & 1;
                    break;
                case 0xc8:
                    t.tie = v & 1;
                    releaseTrack(t, player, -1);
                    freeTrackChannels(t);
                    break;
                case 0xc9:
                    t.portamentoKey = std::uint8_t(b + t.transpose);
                    t.portamento = true;
                    break;
                case 0xca:
                    t.lfo.depth = b;
                    break;
                case 0xcb:
                    t.lfo.speed = b;
                    break;
                case 0xcc:
                    t.lfo.type = b;
                    break;
                case 0xcd:
                    t.lfo.range = b;
                    break;
                case 0xce:
                    t.portamento = v & 1;
                    break;
                case 0xcf:
                    t.portamentoTime = b;
                    break;
                case 0xd0:
                    t.attack = b;
                    break;
                case 0xd1:
                    t.decay = b;
                    break;
                case 0xd2:
                    t.sustain = b;
                    break;
                case 0xd3:
                    t.release = b;
                    break;
                case 0xd4: // loop start with a count (0: forever)
                    if (t.depth < 3) {
                        t.stack[t.depth] = t.position;
                        t.loops[t.depth] = b;
                        ++t.depth;
                    }
                    break;
                case 0xd5:
                    t.expression = b;
                    break;
                case 0xd6:
                    break; // prints a variable (development aid)
                case 0xd7:
                    muteTrack(t, player, b);
                    break;
                default:
                    break;
                }
                break;
            }
            case 0xe0: {
                const auto v = std::uint16_t(arg(1));
                if (doIt && command == 0xe0) {
                    t.lfo.delay = v;
                } else if (doIt && command == 0xe1) {
                    player.tempo = v;
                } else if (doIt && command == 0xe3) {
                    t.sweepPitch = std::int16_t(v);
                }
                break;
            }
            case 0xf0:
                if (!doIt) {
                    break;
                }
                if (command == 0xfc && t.depth != 0) { // loop end
                    std::uint8_t& count = t.loops[t.depth - 1u];
                    if (count == 0 || --count != 0) {
                        t.position = t.stack[t.depth - 1u];
                    } else {
                        --t.depth;
                    }
                } else if (command == 0xfd && t.depth != 0) { // return
                    t.position = t.stack[--t.depth];
                } else if (command == 0xff) { // end of track
                    ended = true;
                }
                break;
            default:
                break;
            }
            if (ended) {
                break;
            }
        }
        if (ended) {
            closeTrack(player, slot);
        } else {
            running = true;
        }
    }
    return !running;
}

void Sequencer::noteOn(unsigned p, Track& t, int key, int velocity, std::int32_t length) {
    // 03800cf0, note part.
    ++work_.notes;
    Player& player = players_[p];
    int c = -1;
    if (t.tie && t.channels >= 0) { // a tie changes the note that sounds
        c = t.channels;
        channels_[std::size_t(c)].key = std::uint8_t(key);
        channels_[std::size_t(c)].velocity = std::uint8_t(velocity);
    } else {
        if (!player.bank) {
            return;
        }
        const auto instrument = findInstrument(player.bank->data, t.program, key);
        if (!instrument) {
            return;
        }
        std::uint32_t mask;
        switch (instrument->type) {
        case 1:
        case 4:
            mask = 0xffff;
            break;
        case 2:
            mask = 0x3f00;
            break; // tones: channels 8..13
        case 3:
            mask = 0xc000;
            break; // noise: channels 14, 15
        default:
            return;
        }
        c = allocateChannel(mask & t.channelMask, player.priority + t.priority, t.ownChannels,
                            int(&t - tracks_.data()));
        if (c < 0) {
            return;
        }
        Channel& ch = channels_[std::size_t(c)];
        // A tied note, or one of length 0, plays until the sample ends
        // (03800f60).
        const std::int32_t duration = t.tie || length <= 0 ? kInfinite : length;
        if (!startNote(ch, key, velocity, duration, *player.bank, *instrument)) {
            ch.priority = 0;
            ch.owner = -1;
            return;
        }
        ch.nextInTrack = t.channels;
        t.channels = c;
    }
    Channel& ch = channels_[std::size_t(c)];
    if (t.attack != 0xff) {
        ch.attack = attackRate(t.attack);
    }
    if (t.decay != 0xff) {
        ch.decay = fallRate(t.decay);
    }
    if (t.sustain != 0xff) {
        ch.sustain = t.sustain;
    }
    if (t.release != 0xff) {
        ch.release = fallRate(t.release);
    }
    ch.sweep = t.sweepPitch;
    if (t.portamento) {
        ch.sweep = std::int16_t(ch.sweep + (t.portamentoKey - key) * 64);
    }
    if (t.portamentoTime == 0) {
        ch.sweepLength = length > 0 ? length : kInfinite;
        ch.autoSweep = false;
    } else {
        const int distance = ch.sweep < 0 ? -ch.sweep : ch.sweep;
        ch.sweepLength = int(unsigned(t.portamentoTime) * t.portamentoTime * distance) >> 11;
    }
    ch.sweepCounter = 0;
}

void Sequencer::updateTrack(Track& t, const Player& player, bool releaseFinished) {
    // 03800b98: track parameters to the track's channels.
    ++work_.trackUpdates;
    int pan = t.pan;
    if (t.panRange != 127) {
        pan = (pan * t.panRange + 64) >> 7;
    }
    pan = std::clamp(pan + t.extPan, -128, 127);
    const int volume =
        std::max(-0x8000, tables_.decibels[player.volume] + tables_.decibels[t.volume] +
                              tables_.decibels[t.expression]);
    const int offset = std::max(-0x8000, t.extVolume + player.volumeOffset);
    const auto pitch = std::int16_t(t.extPitch + ((t.pitchBend * t.bendRange * 64) >> 7));
    for (int c = t.channels; c >= 0; c = channels_[std::size_t(c)].nextInTrack) {
        Channel& ch = channels_[std::size_t(c)];
        ch.volumeOffset = std::int16_t(offset);
        if (ch.state == Channel::Release) {
            continue;
        }
        ch.volume = std::int16_t(volume);
        ch.pitch = pitch;
        ch.trackPan = std::int8_t(pan);
        ch.panRange = t.panRange;
        ch.lfo.type = t.lfo.type;
        ch.lfo.speed = t.lfo.speed;
        ch.lfo.depth = t.lfo.depth;
        ch.lfo.range = t.lfo.range;
        ch.lfo.delay = t.lfo.delay;
        if (ch.length == 0 && releaseFinished) {
            ch.priority = 1;
            ch.state = Channel::Release;
        }
    }
}

void Sequencer::releaseTrack(Track& t, const Player& player, int release) {
    // 038009e0.
    updateTrack(t, player, false);
    for (int c = t.channels; c >= 0; c = channels_[std::size_t(c)].nextInTrack) {
        Channel& ch = channels_[std::size_t(c)];
        if (ch.active) {
            if (release >= 0) {
                ch.release = fallRate(release & 0xff);
            }
            ch.priority = 1;
            ch.state = Channel::Release;
        }
    }
}

void Sequencer::freeTrackChannels(Track& t) {
    // 03800a48: the channels play on, no longer the track's.
    for (int c = t.channels; c >= 0;) {
        Channel& ch = channels_[std::size_t(c)];
        c = ch.nextInTrack;
        ch.owner = -1;
    }
    t.channels = -1;
}

void Sequencer::muteTrack(Track& t, const Player& player, unsigned mode) {
    // 038018c8: 0 unmute, 1 mute, 2 also release, 3 also release fast and
    // let go of the channels.
    switch (mode) {
    case 0:
        t.muted = false;
        break;
    case 1:
        t.muted = true;
        break;
    case 2:
        t.muted = true;
        releaseTrack(t, player, -1);
        break;
    case 3:
        t.muted = true;
        releaseTrack(t, player, 127);
        freeTrackChannels(t);
        break;
    default:
        break;
    }
}

// --- channels ---------------------------------------------------------------

void Sequencer::releasedBy(Channel& c, bool ended) {
    // 03800b38: the owning track forgets a channel that finished or was
    // taken by another note.
    if (ended) {
        c.priority = 0;
    }
    const int owner = c.owner;
    c.owner = -1;
    if (owner < 0) {
        return;
    }
    Track& t = tracks_[std::size_t(owner)];
    const int self = int(c.index);
    if (t.channels == self) {
        t.channels = c.nextInTrack;
        return;
    }
    for (int k = t.channels; k >= 0; k = channels_[std::size_t(k)].nextInTrack) {
        if (channels_[std::size_t(k)].nextInTrack == self) {
            channels_[std::size_t(k)].nextInTrack = c.nextInTrack;
            return;
        }
    }
}

int Sequencer::allocateChannel(std::uint32_t mask, int priority, bool ownChannels, int owner) {
    // 037ff950: the lowest priority, the quietest of equals, in channel
    // order; none if that priority is above the note's.
    mask &= ~reserved_;
    if (!ownChannels) {
        mask &= ~locked_;
    }
    auto loudness = [&](const Channel& c) {
        return int((c.hardwareVolume & 0xff) << 4) >>
               tables_.volumeShift[(c.hardwareVolume >> 8) & 3];
    };
    Channel* best = nullptr;
    for (unsigned k = 0; k < 16; ++k) {
        const unsigned i = tables_.channelOrder[k];
        if (!(mask >> i & 1)) {
            continue;
        }
        Channel& c = channels_[i];
        if (!best || c.priority < best->priority ||
            (c.priority == best->priority && loudness(*best) > loudness(c))) {
            best = &c;
        }
    }
    if (!best || priority < best->priority) {
        return -1;
    }
    Channel& c = *best;
    if (c.owner >= 0) {
        releasedBy(c, false);
    }
    c.updates = 2; // stop what it plays
    c.active = false;
    c.nextInTrack = -1;
    c.owner = owner;
    c.length = 0;
    c.priority = std::uint8_t(priority);
    c.hardwareVolume = 127;
    c.startPending = false;
    c.autoSweep = true;
    c.key = 60;
    c.baseKey = 60;
    c.velocity = 127;
    c.instrumentPan = 0;
    c.volume = 0;
    c.volumeOffset = 0;
    c.pitch = 0;
    c.trackPan = 0;
    c.panRange = 127;
    c.sweep = 0;
    c.sweepLength = 0;
    c.sweepCounter = 0;
    c.attack = 0;
    c.decay = 0xffff;
    c.sustain = 127;
    c.release = 0xffff;
    c.lfo = Lfo{};
    return int(c.index);
}

bool Sequencer::startNote(Channel& c, int key, int velocity, std::int32_t length,
                          const LoadedBank& bank, const Instrument& instrument) {
    // 03801b08. Release 0xff: the note plays to the sample's end.
    int release = instrument.release;
    if (release == 0xff) {
        length = kInfinite;
        release = 0;
    }
    switch (instrument.type) {
    case 1: { // 037ff760, a sample of a wave archive linked into the bank
        if (instrument.archive >= 4) {
            return false;
        }
        const auto archive = bank.waveArchives[instrument.archive];
        if (archive.size() < 0x3c || instrument.wave >= read32(archive, 0x38)) {
            return false;
        }
        const std::uint32_t at = read32(archive, 0x3c + std::size_t(instrument.wave) * 4);
        if (at == 0 || std::size_t(at) + 12 > archive.size()) {
            return false;
        }
        c.type = Channel::Wave;
        c.format = archive[at];
        c.loop = archive[at + 1];
        c.baseTimer = read16(archive, at + 4);
        c.loopStart = read16(archive, at + 6);
        c.loopLength = read32(archive, at + 8);
        c.data = archive.subspan(at + 12);
        break;
    }
    case 2: // 037ff79c, a tone on channels 8..13
        if (c.index < 8 || c.index >= 14) {
            return false;
        }
        c.type = Channel::Tone;
        c.duty = instrument.wave;
        c.baseTimer = 0x1f46;
        break;
    case 3: // 037ff7e8, noise on channels 14, 15
        if (c.index < 14) {
            return false;
        }
        c.type = Channel::Noise;
        c.baseTimer = 0x1f46;
        break;
    default: // 4 (by address) is not used by this game's banks
        return false;
    }
    startEnvelope(c, length);
    c.key = std::uint8_t(key);
    c.baseKey = instrument.baseKey;
    c.velocity = std::uint8_t(velocity);
    c.attack = attackRate(instrument.attack);
    c.decay = fallRate(instrument.decay);
    c.sustain = instrument.sustain;
    c.release = fallRate(release);
    c.instrumentPan = std::int8_t(instrument.pan - 0x40);
    return true;
}

void Sequencer::startEnvelope(Channel& c, std::int32_t length) {
    // 037ffeb8.
    c.level = kSilent * 128;
    c.state = Channel::Attack;
    c.length = length;
    c.lfo.counter = 0;
    c.lfo.phase = 0;
    c.active = true;
    c.startPending = true;
}

std::uint8_t Sequencer::attackRate(int attack) const {
    // 037ff8d4.
    return attack < 109 ? std::uint8_t(255 - attack) : tables_.attack[std::size_t(127 - attack)];
}

std::uint16_t Sequencer::fallRate(int rate) {
    // 037ffe64: decay and release, centibels x 128 per tick.
    if (rate == 127) {
        return 0xffff;
    }
    if (rate == 126) {
        return 0x3c00;
    }
    if (rate < 50) {
        return std::uint16_t(rate * 2 + 1);
    }
    return std::uint16_t(0x1e00 / (126 - rate));
}

std::int32_t Sequencer::envelope(Channel& c, bool tick) {
    // 037ff82c.
    if (tick) {
        switch (c.state) {
        case Channel::Attack:
            c.level = -((-c.level * c.attack) >> 8);
            if (c.level == 0) {
                c.state = Channel::Decay;
            }
            break;
        case Channel::Decay: {
            const std::int32_t sustain = tables_.decibels[c.sustain & 127];
            c.level -= c.decay;
            if (c.level <= sustain * 128) {
                c.level = sustain << 7;
                c.state = Channel::Sustain;
            }
            break;
        }
        case Channel::Sustain:
            break;
        case Channel::Release:
            c.level -= c.release;
            break;
        }
    }
    return c.level >> 7;
}

int Sequencer::sine(int x) const {
    // 037fee58: x in 0..127 is a whole wave.
    if (x < 32) {
        return tables_.sine[std::size_t(x)];
    }
    if (x < 64) {
        return tables_.sine[std::size_t(64 - x)];
    }
    if (x < 96) {
        return -tables_.sine[std::size_t(x - 64)];
    }
    return -tables_.sine[std::size_t(128 - x)];
}

std::int32_t Sequencer::lfoValue(const Lfo& l) const {
    // 037ffe14.
    if (l.depth == 0 || l.counter < l.delay) {
        return 0;
    }
    return sine(l.phase >> 8) * l.range * l.depth;
}

void Sequencer::advanceLfo(Lfo& l) {
    // 037ffdb4: after the delay, the phase (8.8, 128 a wave) advances by
    // speed / 4 a tick.
    if (l.delay <= l.counter) {
        unsigned whole = (unsigned(l.phase) + l.speed * 64u) >> 8;
        while (whole > 0x7f) {
            whole -= 0x80;
        }
        l.phase = std::uint16_t(((l.phase + l.speed * 64u) & 0xff) | (whole << 8));
        return;
    }
    ++l.counter;
}

std::uint16_t Sequencer::hardwareVolume(int centibels) const {
    // 037fedd8: the BIOS volume and the divider of its range.
    centibels = std::clamp(centibels, kSilent, 0);
    const unsigned shift = centibels < -240 ? 3 : centibels < -120 ? 2 : centibels < -60 ? 1 : 0;
    return std::uint16_t(volumeCurve(unsigned(centibels - kSilent)) | shift << 8);
}

std::uint16_t Sequencer::timer(std::uint16_t base, int pitch) {
    // 037fecac: the timer for `pitch` (64 a semitone, 768 an octave) above
    // the sample's base timer.
    int octave = 0;
    int step = -pitch;
    while (step < 0) {
        --octave;
        step += 768;
    }
    while (step >= 768) {
        ++octave;
        step -= 768;
    }
    std::uint64_t t = std::uint64_t(base) * (std::uint64_t(pitchCurve(unsigned(step))) + 0x10000);
    const int shift = octave - 16;
    if (shift <= 0) {
        t >>= -shift;
    } else if (shift < 32 && (t >> (64 - shift)) == 0) {
        t <<= shift;
    } else {
        return 0xffff;
    }
    return std::uint16_t(std::clamp<std::uint64_t>(t, 0x10, 0xffff));
}

void Sequencer::updateChannels(bool tick) {
    // 037ff3ac.
    for (Channel& c : channels_) {
        if (!c.active) {
            continue;
        }
        ++work_.channelsActive;
        if (beforeChannelUpdate) {
            beforeChannelUpdate(c.index, !c.startPending);
        }
        if (c.startPending) {
            c.updates |= 1;
            c.startPending = false;
        } else if (!(hardware_.busy() >> c.index & 1)) { // the sample ended
            if (c.owner >= 0) {
                releasedBy(c, true);
            } else {
                c.priority = 0;
            }
            c.hardwareVolume = 0;
            c.active = false;
            continue;
        }
        int volume = tables_.decibels[c.velocity & 127];
        volume += envelope(c, tick);
        int pitch = (c.key - c.baseKey) * 64;
        int sweep = 0;
        if (c.sweep != 0 && c.sweepCounter < c.sweepLength) {
            sweep = int(std::int64_t(c.sweep) * (c.sweepLength - c.sweepCounter) / c.sweepLength);
            if (tick && c.autoSweep) {
                ++c.sweepCounter;
            }
        }
        volume += c.volume + c.volumeOffset;
        pitch += sweep + c.pitch;
        std::int64_t lfo = lfoValue(c.lfo);
        std::int32_t scaled = 0;
        if (lfo != 0) {
            lfo *= c.lfo.type == 1 ? 60 : 64;
            scaled = std::int32_t(std::uint64_t(lfo) >> 14);
        }
        if (tick) {
            advanceLfo(c.lfo);
        }
        int lfoPan = 0;
        switch (c.lfo.type) {
        case 0:
            pitch += scaled;
            break;
        case 1:
            if (volume > -0x8000) {
                volume += scaled;
            }
            break;
        case 2:
            lfoPan = scaled;
            break;
        default:
            break;
        }
        int pan = lfoPan + c.instrumentPan;
        if (c.panRange != 127) {
            pan = (pan * c.panRange + 64) >> 7;
        }
        pan += c.trackPan;
        if (c.state == Channel::Release && volume <= kSilent) {
            c.updates = 2; // stop
            if (c.owner >= 0) {
                releasedBy(c, true);
            } else {
                c.priority = 0;
            }
            c.hardwareVolume = 0;
            c.active = false;
            continue;
        }
        const std::uint16_t v = hardwareVolume(volume);
        std::uint16_t t = timer(c.baseTimer, pitch);
        if (c.type == Channel::Tone) {
            t &= 0xfffc;
        }
        const auto hp = std::uint8_t(std::clamp(pan + 0x40, 0, 127));
        if (v != c.hardwareVolume) {
            c.hardwareVolume = v;
            c.updates |= 8;
        }
        if (t != c.hardwareTimer) {
            c.hardwareTimer = t;
            c.updates |= 4;
        }
        if (hp != c.hardwarePan) {
            c.hardwarePan = hp;
            c.updates |= 0x10;
        }
    }
}

void Sequencer::applyChannels() {
    // 037ff1c0: write what the last pass decided; then, in a second loop,
    // start the channels set up.
    auto before = [&](Write w, unsigned channel) {
        if (beforeWrite) {
            beforeWrite(w, channel);
        }
    };
    for (Channel& c : channels_) {
        if (!c.updates) {
            continue;
        }
        const std::uint32_t bit = 1u << c.index;
        if (c.updates & 2) {
            before(Write::Stop, c.index);
            hardware_.stopChannels(bit, false);
        }
        const unsigned volume = c.hardwareVolume & 0xff, shift = c.hardwareVolume >> 8;
        if (c.updates & 1) {
            before(Write::Setup, c.index);
            switch (c.type) {
            case Channel::Wave:
                hardware_.setupPcm(c.index, c.format, c.data.data(), c.data.data() + c.data.size(),
                                   c.loop ? 1 : 2, c.loopStart, c.loopLength, volume, shift,
                                   c.hardwareTimer, c.hardwarePan);
                break;
            case Channel::Tone:
                hardware_.setupPsg(c.index, c.duty, volume, shift, c.hardwareTimer, c.hardwarePan);
                break;
            case Channel::Noise:
                hardware_.setupNoise(c.index, volume, shift, c.hardwareTimer, c.hardwarePan);
                break;
            }
        } else {
            if (c.updates & 4) {
                before(Write::Timer, c.index);
                hardware_.setTimer(bit, c.hardwareTimer);
            }
            if (c.updates & 8) {
                before(Write::Volume, c.index);
                hardware_.setVolume(bit, volume, shift);
            }
            if (c.updates & 0x10) {
                before(Write::Pan, c.index);
                hardware_.setPan(bit, c.hardwarePan);
            }
        }
    }
    for (Channel& c : channels_) {
        if (c.updates & 1) {
            before(Write::Start, c.index);
            hardware_.startChannels(1u << c.index);
        }
        c.updates = 0;
    }
}

} // namespace picarat::sdat
