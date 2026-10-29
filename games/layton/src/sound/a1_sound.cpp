// The first adventure's sound object. See a1_sound.hpp.
#include "sound/a1_sound.hpp"
#include "system/resources.hpp"

namespace picarat {

namespace {

constexpr std::uint32_t kProgram = 0x02000000;
constexpr std::uint64_t kAlarmClocks = 0xaa8ull * 64; // the driver's alarm (037ff008)
// The stream's refill: when its effect is applied after the alarm (four
// samples; see streamAlarm) and when the ARM9 runs it (the median measured).
constexpr std::uint64_t kRefillDelay = 4 * 1024, kRefillLatency = 1437;

// Tables in the ARM9 program (02004990 / 02004a50 / 02004b08 / 02004bbc /
// 02004f3c / 02096984).
constexpr std::uint32_t kMusicByKind = 0x020cea86;      // s16 per kind - 1 (-1, 0: keep)
constexpr std::uint32_t kEffectsAByKind = 0x020d1a86;   // s16 per kind - 1
constexpr std::uint32_t kEffectsBByKind = 0x020d0a86;   // s16 per kind - 1
constexpr std::uint32_t kGroupByKind = 0x020cea38;      // s16 per kind
constexpr std::uint32_t kEffectsAByPuzzle = 0x020cfa86; // s16 per puzzle - 1
constexpr std::uint32_t kEffectsBByPuzzle = 0x020d1e86; // s16 per puzzle - 1
constexpr std::uint32_t kGroupByPuzzle = 0x020d0e86;    // s16 per puzzle
constexpr std::uint32_t kMusicByRoom = 0x020cee86;      // s16 per room - 1
constexpr std::uint32_t kEffectsAByRoom = 0x020d1686;   // s16 per room - 1
constexpr std::uint32_t kEffectsBByRoom = 0x020d1286;   // s16 per room - 1
constexpr std::uint32_t kGroupByRoom = 0x020d0286;      // s16 per room
constexpr std::uint32_t kMusicByEvent = 0x020cf286;     // s16 per event - 1 (-1: reload)
constexpr std::uint32_t kEffectsAByEvent = 0x020d0686;  // s16 per event - 1
constexpr std::uint32_t kEffectsBByEvent = 0x020cfe86;  // s16 per event - 1
constexpr std::uint32_t kGroupByEvent = 0x020cf686;     // s16 per event
constexpr std::uint32_t kVoiceArchives = 0x020d2288;    // byte per voice group
constexpr std::uint32_t kDecibels = 0x020cd908;         // the archive player's volumes
constexpr std::uint32_t kStreamSteps = 0x020c92d8;      // s16 x 89, the stream decoder's steps

} // namespace

struct Adventure1Sound::Tables {
    Bytes arm9;

    int s16(std::uint32_t table, int index) const {
        return std::int16_t(read16(arm9, table - kProgram + std::size_t(index) * 2));
    }
};

namespace {

std::array<std::int16_t, 128> decibels(const Bytes& arm9) {
    std::array<std::int16_t, 128> t{};
    for (unsigned i = 0; i < 128; ++i) {
        t[i] = std::int16_t(read16(arm9, kDecibels - kProgram + i * 2));
    }
    return t;
}

} // namespace

Adventure1Sound::Adventure1Sound(const std::filesystem::path& pack)
    : tables_(std::make_unique<Tables>(Tables{readFile(pack / "code/arm9.bin")})),
      archive_(readFile(pack / "sound/sound_data.sdat")),
      sequencer_(hardware_, sdat::DriverTables::fromProgram(readFile(pack / "code/arm7.bin"))),
      player_(archive_, sequencer_, decibels(tables_->arm9)), pack_(pack) {
    std::vector<std::int16_t> steps(89);
    for (int i = 0; i < 89; ++i) {
        steps[std::size_t(i)] = std::int16_t(tables_->s16(kStreamSteps, i));
    }
    stream_ = std::make_unique<Adventure1Stream>(std::move(steps));
    player_.synced = [this] {
        sync();
    };
    // 02004fdc: the music heap (0x9c400 bytes) and the effect heap
    // (0x4b000), then the levels the object keeps.
    musicHeap_ = player_.createHeap();
    effectHeap_ = player_.createHeap();
    levels_[0] = player_.saveLevel(musicHeap_);  // 020047e8
    levels_[2] = player_.saveLevel(musicHeap_);  //   020048bc
    levels_[6] = player_.saveLevel(musicHeap_);  //   02004974
    levels_[6] = player_.saveLevel(musicHeap_);  //   02004974
    levels_[1] = player_.saveLevel(effectHeap_); // 02004814
    levels_[2] = player_.saveLevel(musicHeap_);  // 020048bc
    levels_[6] = player_.saveLevel(musicHeap_);
    levels_[3] = player_.saveLevel(effectHeap_); // 020048e0
    levels_[4] = player_.saveLevel(effectHeap_); // 020048fc
}

Adventure1Sound::~Adventure1Sound() = default;

void Adventure1Sound::frame() {
    std::lock_guard lock(mutex_);
    if (!frameClock_) {
        playerFrame();
        return;
    }
    // A DS frame (263 lines of 2,130 clocks); the game's frame, and with it
    // the archive player's, runs partway in.
    renderUntil(frameClocks_ + flushAt_);
    playerFrame();
    frameClocks_ += 560190;
    renderUntil(frameClocks_);
}

void Adventure1Sound::idle() {
    std::lock_guard lock(mutex_);
    if (frameClock_) {
        frameClocks_ += 560190;
        renderUntil(frameClocks_);
    }
}

void Adventure1Sound::clockWithFrames(bool on) {
    std::lock_guard lock(mutex_);
    frameClock_ = on;
}

void Adventure1Sound::setFrameFlush(std::uint64_t clocks) {
    std::lock_guard lock(mutex_);
    flushAt_ = clocks % 560190;
}

void Adventure1Sound::renderUntil(std::uint64_t clock) {
    callClock_ = clock;
    const std::uint64_t target = clock / DsSound::clocksPerSample;
    const std::uint64_t done = clocks_ / DsSound::clocksPerSample - mixed_.size() / 2;
    if (target > done) {
        scratch_.resize(std::size_t(target - done) * 2);
        renderLocked(scratch_);
    }
}

void Adventure1Sound::render(std::span<std::int16_t> stereo) {
    std::lock_guard lock(mutex_);
    renderLocked(stereo);
}

void Adventure1Sound::renderLocked(std::span<std::int16_t> stereo) {
    while (mixed_.size() < stereo.size()) {
        if (streamAlarm_ != 0 && clocks_ >= streamAlarm_) {
            streamAlarm();
        } else if (wakeAt_ != 0 && clocks_ >= wakeAt_ && wakeAt_ < nextAlarm_) {
            const std::uint64_t at = wakeAt_;
            wakeAt_ = 0;
            driverPass(at, false, true);
        } else if (clocks_ >= nextAlarm_) {
            driverPass(nextAlarm_ + writeLatency_, true, false);
            nextAlarm_ += kAlarmClocks;
            if (wakeAt_ != 0 && wakeAt_ < nextAlarm_) {
                wakeAt_ = 0; // the alarm's pass took the commands
            }
        } else {
            mixOne();
        }
    }
    for (auto& v : stereo) {
        v = mixed_.front();
        mixed_.pop_front();
    }
}

void Adventure1Sound::mixOne() {
    std::int16_t frame[2];
    hardware_.render(frame);
    mixed_.push_back(frame[0]);
    mixed_.push_back(frame[1]);
    clocks_ += DsSound::clocksPerSample;
}

void Adventure1Sound::playerFrame() {
    player_.frame(); // flushes at its end
    flushCommands(now());
}

void Adventure1Sound::flushCommands(std::uint64_t clock) {
    // A flush hands a batch to the driver (PXI); it arrives `postLatency_`
    // clocks after the call.
    sequencer_.flush();
    const std::uint32_t sent = sequencer_.batchesSent();
    if (sent != sentBatches_) {
        sentBatches_ = sent;
        arrivals_.emplace_back(sent, clock + postLatency_);
    }
}

std::uint64_t Adventure1Sound::now() const {
    // The output position, or the exact clock the game's call was timed to
    // when that lies within the sample.
    return std::max(callClock_, clocks_ - mixed_.size() / 2 * DsSound::clocksPerSample);
}

void Adventure1Sound::setCallClock(std::uint64_t clock) {
    std::lock_guard lock(mutex_);
    callClock_ = clock;
}

void Adventure1Sound::sync() {
    // 02097dc4, after the heap frees what a level held: flush, then wait for
    // the driver; when the batch is not yet done, 020a05e4 wakes the driver
    // (020a07e4, PXI message 0) for a pass without a tick.
    flushCommands(now());
    if (sequencer_.batchesDone() >= sequencer_.batchesSent()) {
        return;
    }
    wakeAt_ = now() + wakeLatency_;
    if (frameClock_) {
        renderUntil(wakeAt_ + DsSound::clocksPerSample); // the game waits for the reply
    }
}

void Adventure1Sound::driverPass(std::uint64_t entry, bool tick, bool woken) {
    // The driver's thread wakes on the alarm (or a wake message) and writes the registers one
    // after another while the hardware mixes on: each write lands when the
    // ARM7 gets to it (measured in a reference run with the ARM7 clock of
    // every write: 97.7% of 14,906 writes within 2 clocks of this model).
    // First loop (037ff1c0) over the 16 channels: 14 clocks a channel plus
    // what its work took (volume 71, setup 45, stop 46, stop and setup
    // 162); a volume write lands as the loop reaches the channel, a control
    // word (stop or setup) 38 clocks earlier, a setup after a stop 117
    // after the stop. Second loop (the starts): from 44 clocks before the
    // first loop's end, 14 clocks a channel and 12 more for channels that
    // were written. Timer and pan writes did not occur in the run; they are
    // timed like volume writes (hypothesis). The rest of the pass follows.
    using W = sdat::Sequencer::Write;
    int current = -1;        // channel the first loop is at
    std::uint64_t extra = 0; // work of the channels before it
    std::uint64_t work = 0;  // the current channel's
    bool stopped = false;
    std::uint32_t written = 0;
    bool starting = false;
    std::uint64_t startClock = 0;
    int startChannel = 0;
    auto mixUntil = [&](std::uint64_t at) {
        while (clocks_ <= at) {
            mixOne();
        }
    };
    std::uint64_t commandClock = entry, directClock = entry;
    sequencer_.beforeWrite = [&](W w, unsigned channel) {
        const int ch = int(channel);
        if (w == W::Direct) { // the commands step writes directly used channels
            mixUntil(commandClock);
            return;
        }
        if (w == W::Start) {
            if (!starting) {
                extra += work;
                starting = true;
                startClock = entry + 14 * 16 + extra - 44;
                startChannel = 0;
            }
            for (; startChannel < ch; ++startChannel) {
                startClock += 14 + (written >> startChannel & 1 ? 12 : 0);
            }
            mixUntil(startClock);
            return;
        }
        if (ch != current) {
            extra += work;
            work = 0;
            stopped = false;
            current = ch;
            written |= 1u << channel;
        }
        const std::uint64_t reach = entry + 14 * std::uint64_t(ch) + extra;
        switch (w) {
        case W::Stop:
            mixUntil(reach - 38);
            work = 46;
            stopped = true;
            break;
        case W::Setup:
            mixUntil(reach - 38 + (stopped ? 117 : 0));
            work = stopped ? 162 : 45;
            break;
        default: // timer, volume, pan
            mixUntil(reach + work);
            work += 71;
            break;
        }
    };
    // The channel update reads the busy bits later in the pass, after the
    // commands and the players' sequence steps. Fitted to the busy-bit reads
    // of the reference run (half of 5,775 passes within 250 clocks; the
    // driver's execution time, not reproduced exactly): 1,830 clocks after
    // the first write, plus 97 a command, 127 a player tick, 722 a sequence
    // step, 410 a sequence byte read, 137 a track update; then 495 for each
    // active channel the loop passes and 16 for each inactive one.
    // The player step ends (and writes the players-running word) where the
    // channel update starts.
    auto playersDone = [&] {
        const auto w = sequencer_.lastWork();
        return entry + 1830 + 97 * w.commands + 127 * w.playerTicks + 722 * w.steps +
               410 * w.commandsRead + 137 * w.trackUpdates;
    };
    bool updating = false;
    std::uint64_t updateClock = 0;
    int updateChannel = 0;
    sequencer_.beforeChannelUpdate = [&](unsigned channel, bool reads) {
        if (!updating) {
            updateClock = playersDone();
            updating = true;
        }
        updateClock += 16 * std::uint64_t(int(channel) - updateChannel);
        updateChannel = int(channel) + 1;
        if (reads) {
            mixUntil(updateClock);
        }
        updateClock += 495;
    };
    // The commands step (03801f1c) follows the writes: it takes the batches
    // that have arrived by then.
    sequencer_.beforeCommands = [&] {
        std::uint64_t at = entry + 14 * 16 + extra + work;
        if (starting) {
            for (; startChannel < 16; ++startChannel) {
                startClock += 14 + (written >> startChannel & 1 ? 12 : 0);
            }
            at = std::max(at, startClock);
        }
        // A wake message comes after the batches it is sent for.
        while (!arrivals_.empty() && (woken || arrivals_.front().second <= at)) {
            arrivedBatches_ = arrivals_.front().first;
            arrivals_.pop_front();
        }
        sequencer_.setArrived(arrivedBatches_);
        directClock = at + commandStep_;
    };
    // The commands step's time per command (measured in a reference run:
    // the clocks between the writes of consecutive stream commands; other
    // commands as in the busy-read fit below). A direct command writes when
    // it is reached; the alarm starts with the channels.
    sequencer_.beforeCommand = [&](sdat::Sequencer::Step step) {
        using S = sdat::Sequencer::Step;
        commandClock = directClock;
        directClock += step == S::Setup   ? 184
                       : step == S::Alarm ? 362
                       : step == S::Start ? 32
                       : step == S::Stop  ? 818
                                          : 97;
    };
    // The stream's alarm (03801dac): first after `tick` units of 64 clocks.
    sequencer_.alarmChanged = [&](unsigned, bool start, std::uint32_t tick, std::uint32_t period) {
        streamAlarm_ = start ? commandClock + std::uint64_t(tick) * 64 + kRefillDelay : 0;
        streamPeriod_ = std::uint64_t(period) * 64;
    };
    playersBefore_ = sequencer_.activePlayers();
    sequencer_.pass(tick);
    playersAt_ = playersDone();
    sequencer_.beforeWrite = nullptr;
    sequencer_.beforeChannelUpdate = nullptr;
    sequencer_.beforeCommands = nullptr;
    sequencer_.alarmChanged = nullptr;
    sequencer_.beforeCommand = nullptr;
    if (passTrace) {
        passTrace(entry, sequencer_.lastWork());
    }
}

std::uint32_t Adventure1Sound::runningPlayers() const {
    // The game reads at the output position, which may lie before the end
    // of the latest pass's player step (the mix runs ahead of it).
    return now() < playersAt_ ? playersBefore_ : sequencer_.activePlayers();
}

void Adventure1Sound::setPostLatency(std::uint64_t clocks) {
    std::lock_guard lock(mutex_);
    postLatency_ = clocks;
}

void Adventure1Sound::setCommandStep(std::uint64_t clocks) {
    std::lock_guard lock(mutex_);
    commandStep_ = clocks;
}

void Adventure1Sound::setWriteLatency(std::uint64_t clocks) {
    std::lock_guard lock(mutex_);
    writeLatency_ = clocks;
}

std::uint64_t Adventure1Sound::mixedSamples() const {
    std::lock_guard lock(mutex_);
    return clocks_ / DsSound::clocksPerSample;
}

void Adventure1Sound::waitWhile(const std::function<bool()>& busy, const WaitFrame& wait) {
    // The object's loops around 02003d64(.., 1): a frame at a time. The lock
    // is the caller's; it is held by the calling thread only between frames.
    while (busy()) {
        mutex_.unlock();
        wait();
        mutex_.lock();
    }
}

void Adventure1Sound::effect(int id) {
    // 020053ac.
    std::lock_guard lock(mutex_);
    if (id == -1) {
        return;
    }
    if (id == 0) {
        player_.startArchived(voice_, std::uint32_t(voiceArchive_), 0);
        return;
    }
    if (!player_.startArchived(effects_, std::uint32_t(effectArchiveA_), std::uint32_t(id))) {
        player_.startArchived(effects_, std::uint32_t(effectArchiveB_), std::uint32_t(id));
    }
}

void Adventure1Sound::voice() {
    // 02005380.
    std::lock_guard lock(mutex_);
    player_.startArchived(voice_, std::uint32_t(voiceArchive_), 0);
}

void Adventure1Sound::stopLoops() {
    // 02005594 (the looped effect, out over 5 frames), 0200562c (effect 15,
    // over 30); 0200548c, 02005668, 0200566c do nothing.
    if (loop2On_) {
        player_.stop(loop2_, 5);
        loop2On_ = false;
    }
    if (loop3On_) {
        player_.stop(loop3_, 30);
        loop3On_ = false;
    }
}

void Adventure1Sound::loadEffects(int archiveA, int archiveB, int group, const WaitFrame& wait) {
    std::lock_guard lock(mutex_);
    loadEffectsLocked(archiveA, archiveB, group, wait);
}

void Adventure1Sound::setAlarmPhase(std::uint64_t clocks) {
    std::lock_guard lock(mutex_);
    nextAlarm_ = clocks_ + clocks % kAlarmClocks;
}

void Adventure1Sound::setRawOutput(bool raw) {
    std::lock_guard lock(mutex_);
    hardware_.dac10 = !raw;
}

void Adventure1Sound::setRegisterTrace(
    std::function<void(std::uint32_t, unsigned, std::uint32_t)> trace) {
    std::lock_guard lock(mutex_);
    hardware_.registerTrace = std::move(trace);
}

void Adventure1Sound::loadEffectsLocked(int archiveA, int archiveB, int group,
                                        const WaitFrame& wait) {
    // 02004cb4.
    if (effectArchiveA_ == archiveA && effectArchiveB_ == archiveB) {
        return;
    }
    stopLoops();
    const auto oldA = std::uint32_t(effectArchiveA_), oldB = std::uint32_t(effectArchiveB_);
    waitWhile([&] { return player_.playingArchive(oldA) || player_.playingArchive(oldB); }, wait);
    player_.restoreLevel(effectHeap_, levels_[1]); // 02004874
    levels_[3] = levels_[4] = levels_[5] = -1;
    player_.loadGroup(std::uint32_t(group), effectHeap_);
    levels_[4] = player_.saveLevel(effectHeap_); // 020048fc
    effectArchiveA_ = archiveA;
    effectArchiveB_ = archiveB;
}

void Adventure1Sound::setupScene(int kind, const WaitFrame& wait) {
    // 02004b08.
    std::unique_lock lock(mutex_);
    if (kind == 4) {
        kind = 3;
    }
    const int i = kind - 1;
    if (i >= 0 && i < 0x200) {
        const int music = tables_->s16(kMusicByKind, i);
        if (music != -1 && music > 0) {
            lock.unlock();
            loadMusic(music, wait);
            lock.lock();
        }
    }
    const bool known = i < 0x200;
    loadEffectsLocked(known ? tables_->s16(kEffectsAByKind, i) : 0,
                      known ? tables_->s16(kEffectsBByKind, i) : 7,
                      known ? tables_->s16(kGroupByKind, i + 1) : 0x16, wait);
}

void Adventure1Sound::setupPuzzle(int number, int engineKind, const WaitFrame& wait) {
    // 02004a50.
    std::lock_guard lock(mutex_);
    if (number < 0) {
        number = engineKind == 3 ? 3 : 0x1e;
    }
    const int i = number - 1;
    if (i >= 0 && i < 0x200) {
        loadEffectsLocked(tables_->s16(kEffectsAByPuzzle, i), tables_->s16(kEffectsBByPuzzle, i),
                          tables_->s16(kGroupByPuzzle, number), wait);
    } else {
        loadEffectsLocked(4, 7, 0x5b, wait);
    }
}

void Adventure1Sound::loadMusic(int sequence, const WaitFrame& wait) {
    // 02004dc4.
    std::lock_guard lock(mutex_);
    if (loadedMusic_ == sequence) {
        wantedMusic_ = music_seq_;
        return;
    }
    player_.stop(music_, 30); // 02005354
    wantedMusic_ = -1;
    if (sequence == 0x13 && loadedMusic_ == 0x12) { // loaded together with 0x12
        music_seq_ = sequence;
        return;
    }
    stopLoops();
    waitWhile(
        [&] {
            return player_.playingArchive(std::uint32_t(effectArchiveA_)) ||
                   player_.playingArchive(std::uint32_t(effectArchiveB_)) ||
                   player_.playingArchive(std::uint32_t(voiceArchive_)) ||
                   player_.playingSequence(std::uint32_t(music_seq_));
        },
        wait);
    loadedMusic_ = sequence;
    player_.restoreLevel(musicHeap_, levels_[0]); // 02004830
    levels_[2] = levels_[6] = -1;
    if (sequence == 0x12) {
        player_.loadSequence(0x12, musicHeap_);
        player_.loadSequence(0x13, musicHeap_);
    } else {
        player_.loadSequence(std::uint32_t(sequence), musicHeap_);
    }
    music_seq_ = sequence;
    wantedMusic_ = -1;
    levels_[2] = player_.saveLevel(musicHeap_); // 020048bc
    levels_[6] = player_.saveLevel(musicHeap_);
    previousMusic_ = sequence; // +0xe74e4
}

void Adventure1Sound::loadVoice(int group, const WaitFrame& wait) {
    // 02004f3c.
    std::lock_guard lock(mutex_);
    player_.stop(voice_, 5);
    waitWhile([&] { return player_.playingArchive(std::uint32_t(voiceArchive_)) != 0; }, wait);
    player_.restoreLevel(effectHeap_, levels_[4]); // 02004918
    levels_[5] = -1;
    player_.loadGroup(std::uint32_t(group), effectHeap_);
    levels_[5] = player_.saveLevel(effectHeap_); // 02004958
    voiceArchive_ = tables_->arm9.at(kVoiceArchives - kProgram + std::size_t(group));
}

void Adventure1Sound::playMusic(int sequence) {
    // 0200530c.
    std::lock_guard lock(mutex_);
    wantedMusic_ = music_seq_;
    player_.stop(music_, 0);
    player_.startSequence(music_, std::uint32_t(sequence));
}

void Adventure1Sound::stopMusic(int frames) {
    std::lock_guard lock(mutex_);
    player_.stop(music_, frames);
    wantedMusic_ = -1;
}

void Adventure1Sound::stopMusicSoftly() {
    std::lock_guard lock(mutex_);
    player_.stop(music_, 30);
    wantedMusic_ = -1;
}

void Adventure1Sound::fadeOutMusic() {
    std::lock_guard lock(mutex_);
    player_.moveVolume(music_, 0, 60);
    wantedMusic_ = -1;
}

void Adventure1Sound::fadeInMusic() {
    std::lock_guard lock(mutex_);
    player_.moveVolume(music_, 127, 60);
    wantedMusic_ = music_seq_;
}

void Adventure1Sound::restoreMusicVolume() {
    std::lock_guard lock(mutex_);
    player_.moveVolume(music_, 127, 0);
    wantedMusic_ = music_seq_;
}

void Adventure1Sound::moveMusicVolume(int target, int frames) {
    std::lock_guard lock(mutex_);
    player_.moveVolume(music_, target, frames);
}

void Adventure1Sound::typingLocked() {
    // 02005414.
    const auto id = std::uint32_t(typingEffect_);
    if (player_.playingArchived(std::uint32_t(effectArchiveA_), id) == 0) {
        player_.startArchived(typing_, std::uint32_t(effectArchiveA_), id);
        typingPending_ = false;
    } else {
        typingPending_ = true;
    }
}

void Adventure1Sound::typingSound(const WaitFrame& wait) {
    std::lock_guard lock(mutex_);
    // Measured in a reference run: the text box runs 12,600..26,700 clocks
    // before the main loop's archive player frame, and a try of the loop
    // takes 451 clocks.
    constexpr std::uint64_t kLead = 15000, kTry = 451;
    std::uint64_t at = frameClocks_ + flushAt_ - std::min(flushAt_, kLead);
    for (;;) {
        if (frameClock_) {
            renderUntil(at);
        }
        typingLocked();
        playerFrame(); // 020964e0
        if (!typingPending_) {
            return;
        }
        if (frameClock_) {
            at += kTry;
            if (at < frameClocks_ + 560190) {
                continue;
            }
        }
        mutex_.unlock(); // a frame passes without the game's frame
        wait();
        mutex_.lock();
    }
}

void Adventure1Sound::typingTry() {
    std::lock_guard lock(mutex_);
    typingLocked();
    playerFrame();
}

bool Adventure1Sound::typingPending() const {
    std::lock_guard lock(mutex_);
    return typingPending_;
}

void Adventure1Sound::retryTyping() {
    std::lock_guard lock(mutex_);
    if (typingPending_) {
        typingLocked();
    }
}

void Adventure1Sound::startLoopedEffect(int id) {
    // 0200552c: the first effect archive, else the second.
    std::lock_guard lock(mutex_);
    if (loop2On_) {
        return;
    }
    if (!player_.startArchived(loop2_, std::uint32_t(effectArchiveA_), std::uint32_t(id))) {
        player_.startArchived(loop2_, std::uint32_t(effectArchiveB_), std::uint32_t(id));
    }
    loopedEffect_ = id;
    loop2On_ = true;
}

void Adventure1Sound::stopLoopedEffect() {
    std::lock_guard lock(mutex_);
    if (loop2On_) {
        player_.stop(loop2_, 5);
        loop2On_ = false;
    }
}

void Adventure1Sound::startLoopedEffect15() {
    // 020055d0.
    std::lock_guard lock(mutex_);
    if (loop3On_) {
        return;
    }
    if (!player_.startArchived(loop3_, std::uint32_t(effectArchiveA_), 0xf)) {
        player_.startArchived(loop3_, std::uint32_t(effectArchiveB_), 0xf);
    }
    loop3On_ = true;
}

void Adventure1Sound::stopLoopedEffect15() {
    std::lock_guard lock(mutex_);
    if (loop3On_) {
        player_.stop(loop3_, 30);
        loop3On_ = false;
    }
}

void Adventure1Sound::setupRoom(int room, const WaitFrame& wait) {
    // 02004990: tables by room; -1 in the effect tables picks the defaults.
    std::unique_lock lock(mutex_);
    const int i = room - 1;
    const bool known = i < 0x200;
    lock.unlock();
    loadMusic(known ? tables_->s16(kMusicByRoom, i) : 2, wait);
    lock.lock();
    int archiveA = known ? tables_->s16(kEffectsAByRoom, i) : 1;
    int archiveB = known ? tables_->s16(kEffectsBByRoom, i) : 0x39;
    const int group = known ? tables_->s16(kGroupByRoom, i + 1) : 0x5b;
    if (archiveA == -1) {
        archiveA = 1;
    }
    if (archiveB == -1) {
        archiveB = 0x39;
    }
    loadEffectsLocked(archiveA, archiveB, group, wait);
}

void Adventure1Sound::setupEvent(int event, const WaitFrame& wait) {
    // 02004bbc: tables by event; music -1 reloads the remembered music.
    std::unique_lock lock(mutex_);
    if (event == 0x154) {
        return;
    }
    const int i = event - 1;
    const bool known = i < 0x200;
    const int music = known ? tables_->s16(kMusicByEvent, i) : -1;
    lock.unlock();
    if (music != -1) {
        loadMusic(music, wait);
    } else {
        reloadMusic(wait);
    }
    lock.lock();
    int archiveA = known ? tables_->s16(kEffectsAByEvent, i) : 2;
    int archiveB = !known ? 7 : event == 0xab ? 0x48 : tables_->s16(kEffectsBByEvent, i);
    int group = known ? tables_->s16(kGroupByEvent, i + 1) : 0x28f;
    if (archiveB == -1) {
        archiveB = 7;
    }
    if (archiveA == -1) {
        archiveA = 2;
    }
    if (group == -1 || group == 0) {
        group = 0x28f;
    }
    loadEffectsLocked(archiveA, archiveB, group, wait);
}

void Adventure1Sound::startLoadedMusic(bool enabled) {
    // 02004768.
    std::lock_guard lock(mutex_);
    if (!enabled) {
        return;
    }
    if (!firstStart_ && (music_seq_ == 0 || music_seq_ == wantedMusic_)) {
        return;
    }
    firstStart_ = false;
    wantedMusic_ = music_seq_;
    player_.stop(music_, 0); // 0200530c
    player_.startSequence(music_, std::uint32_t(music_seq_));
    player_.moveVolume(music_, 127, 0); // 0200527c
}

void Adventure1Sound::forgetLoadedMusic() {
    std::lock_guard lock(mutex_);
    loadedMusic_ = -1;
}

void Adventure1Sound::loadAndPlayMusic(int sequence, const WaitFrame& wait) {
    // 02004d94.
    loadMusic(sequence, wait);
    std::lock_guard lock(mutex_);
    playMusic(music_seq_);
    restoreMusicVolume();
}

void Adventure1Sound::fadeOutMusicSlowly() {
    std::lock_guard lock(mutex_);
    player_.moveVolume(music_, 0, fadeOutFrames_);
    wantedMusic_ = -1;
}

void Adventure1Sound::fadeOutMusicQuickly() {
    std::lock_guard lock(mutex_);
    player_.moveVolume(music_, 0, shortFadeFrames_);
    wantedMusic_ = -1;
}

void Adventure1Sound::fadeOutMusic30() {
    std::lock_guard lock(mutex_);
    player_.moveVolume(music_, 0, 30);
    wantedMusic_ = -1;
}

void Adventure1Sound::fadeInMusicSlowly() {
    std::lock_guard lock(mutex_);
    player_.moveVolume(music_, 127, fadeInFrames_);
    wantedMusic_ = music_seq_;
}

void Adventure1Sound::startRoomLoop() {
    // 02005490: the flag is set even when neither archive has the effect.
    std::lock_guard lock(mutex_);
    if (loop1On_) {
        return;
    }
    if (!player_.startArchived(loop1_, std::uint32_t(effectArchiveA_), 0x2bd)) {
        player_.startArchived(loop1_, std::uint32_t(effectArchiveB_), 0x2bd);
    }
    loop1On_ = true;
}

void Adventure1Sound::stopRoomLoop(int frames) {
    std::lock_guard lock(mutex_);
    if (loop1On_) {
        player_.stop(loop1_, frames);
        loop1On_ = false;
    }
}

void Adventure1Sound::playStream(const std::string& name, int first, int second, int volume) {
    // 020045f8: close any stream, open the file, start it.
    std::lock_guard lock(mutex_);
    stopStream();
    stream_->open(readFile(pack_ / "stream" / name));
    streamOpen_ = true;
    streamChannels_ = {unsigned(first) & 15, unsigned(second) & 15};
    streamVolume_ = volume;
    startStream();
}

void Adventure1Sound::setStreamVolume(int volume) {
    // 02074b60: the channels' volume (v * v / 127) at once.
    std::lock_guard lock(mutex_);
    streamVolume_ = volume;
    if (streamPlaying_) {
        sequencer_.setDirectVolume((1u << streamChannels_[0]) | (1u << streamChannels_[1]),
                                   unsigned(volume * volume / 127), 0);
        flushCommands(now());
    }
}

void Adventure1Sound::setTypingEffect(int id) {
    std::lock_guard lock(mutex_);
    typingEffect_ = id;
}

void Adventure1Sound::closeStream() {
    // 020046ac: 02074a84 stops the channels if they play, then the file
    // closes; the decoder stays for the next stream (open()).
    std::lock_guard lock(mutex_);
    stopStream();
    streamOpen_ = false;
}

void Adventure1Sound::pauseStream() {
    std::lock_guard lock(mutex_);
    stopStream(); // 02074b24
}

void Adventure1Sound::resumeStream(const WaitFrame& wait) {
    std::lock_guard lock(mutex_);
    if (!streamOpen_) {
        return;
    }
    for (int i = 0; i < 20; ++i) { // 02003d64(.., 0x14)
        mutex_.unlock();
        wait();
        mutex_.lock();
    }
    startStream(); // 02074ae4 with the volume it was started with
}

bool Adventure1Sound::streamPlaying() const {
    std::lock_guard lock(mutex_);
    return streamPlaying_;
}

void Adventure1Sound::startStream() {
    // 02074dfc: clear the ring, set both channels up to loop over their part
    // of it as 16-bit samples at 32,728 Hz (timer 0x200), volume v * v / 127,
    // the alarm every refill (0x800 bytes: 0x4000 units of 64 clocks), start
    // channels and alarm together, flush.
    if (streamPlaying_) {
        return;
    }
    stream_->clearRing();
    const unsigned volume = unsigned(streamVolume_ * streamVolume_ / 127);
    std::uint32_t mask = 0;
    for (unsigned i = 0; i < 2; ++i) {
        sequencer_.setupWave(streamChannels_[i], DsSound::Pcm16, stream_->channelRing(i), true, 0,
                             Adventure1Stream::kChannelRing / 4, volume, 0, 0x200,
                             i == 0 ? 0 : 0x7f);
        mask |= 1u << streamChannels_[i];
    }
    sequencer_.setupAlarm(0, 0x4000, 0x4000);
    sequencer_.startDirect(mask, 1);
    // The flush comes after clearing and flushing the ring from the cache
    // (measured: a start 2,797 clocks before an alarm waits a pass, one
    // 5,110 clocks before does not; 3,200 clocks lies between the bounds).
    flushCommands(now() + 3200);
    streamPlaying_ = true;
}

void Adventure1Sound::stopStream() {
    // 02074f64: channels and alarm stop together.
    if (!streamPlaying_) {
        return;
    }
    sequencer_.stopDirect(1u << streamChannels_[0] | 1u << streamChannels_[1], 1);
    flushCommands(now());
    streamPlaying_ = false;
}

void Adventure1Sound::streamAlarm() {
    // The alarm's message wakes the stream's thread (02074bac, message 5),
    // which refills the half the channels have just left (measured: 1,437
    // clocks after the alarm in the median, up to 37,000 when the ARM9 is
    // busy). The hardware fetches samples ahead, so the refill never
    // reaches the samples being played; the native hardware reads at each
    // step, so the refill runs here four samples after the alarm.
    const std::uint64_t at = streamAlarm_ - kRefillDelay + kRefillLatency;
    streamAlarm_ += streamPeriod_;
    if (!stream_->refill()) {
        sequencer_.stopDirect(1u << streamChannels_[0] | 1u << streamChannels_[1], 1);
        flushCommands(at);
        streamPlaying_ = false;
        streamAlarm_ = 0;
    }
}

void Adventure1Sound::freeLevel(int level) {
    std::lock_guard lock(mutex_);
    freeLevelLocked(level);
}

void Adventure1Sound::freeLevelLocked(int level) {
    switch (level) {
    case 0: // 02004830
        player_.restoreLevel(musicHeap_, levels_[0]);
        levels_[2] = levels_[6] = -1;
        break;
    case 1: // 02004874
        player_.restoreLevel(effectHeap_, levels_[1]);
        levels_[3] = levels_[4] = levels_[5] = -1;
        break;
    case 4: // 02004918
        player_.restoreLevel(effectHeap_, levels_[4]);
        levels_[5] = -1;
        break;
    default:
        break;
    }
}

void Adventure1Sound::saveLevel(int level) {
    std::lock_guard lock(mutex_);
    switch (level) {
    case 2: // 020048bc, then 02004974
        levels_[2] = player_.saveLevel(musicHeap_);
        levels_[6] = player_.saveLevel(musicHeap_);
        break;
    case 3: // 020048e0
    case 4: // 020048fc
    case 5: // 02004958
        levels_[level] = player_.saveLevel(effectHeap_);
        break;
    case 6: // 02004974
        levels_[6] = player_.saveLevel(musicHeap_);
        break;
    default:
        break;
    }
}

void Adventure1Sound::rememberMusic() {
    std::lock_guard lock(mutex_);
    rememberedMusic_ = loadedMusic_;
}

void Adventure1Sound::reloadMusic(const WaitFrame& wait) {
    int music;
    {
        std::lock_guard lock(mutex_);
        music = rememberedMusic_;
    }
    loadMusic(music, wait);
}

int Adventure1Sound::loadedMusic() const {
    std::lock_guard lock(mutex_);
    return loadedMusic_;
}

} // namespace picarat
