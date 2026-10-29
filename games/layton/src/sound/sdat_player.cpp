// The SDK sound-archive player. See sdat_player.hpp.
#include "sound/sdat_player.hpp"
#include <algorithm>

namespace picarat::sdat {

namespace {
constexpr std::int16_t kStopVolume = -723; // 02096d9c
constexpr int kMaxVolume = 0x7fff;         // 02096ae4
} // namespace

int ArchivePlayer::Fader::value() const {
    // 02098ec8.
    return counter < frames ? origin + counter * (target - origin) / frames : target;
}

void ArchivePlayer::Fader::set(int to, int length) {
    // 02098ea0.
    origin = value();
    target = to;
    frames = length;
    counter = 0;
}

void ArchivePlayer::Fader::step() {
    // 02098efc.
    if (counter < frames) {
        ++counter;
    }
}

ArchivePlayer::ArchivePlayer(const Archive& archive, Sequencer& sequencer,
                             std::array<std::int16_t, 128> decibels)
    : archive_(archive), sequencer_(sequencer), decibels_(decibels) {
    // 020968d0 / 02098804: the slots in order on the free list; every
    // player's count and channels from the archive.
    for (int i = 0; i < 16; ++i) {
        free_.push_back(i);
    }
    for (unsigned p = 0; p < players_.size(); ++p) {
        if (const auto info = archive_.player(p)) {
            players_[p].maxSequences = info->maxSequences;
            players_[p].channels = info->channels;
        }
    }
}

// --- heaps and loading ------------------------------------------------------

int ArchivePlayer::createHeap() {
    heaps_.emplace_back();
    return int(heaps_.size()) - 1;
}

int ArchivePlayer::saveLevel(int heap) {
    // 02097c24: the new level's number (the levels before it).
    Heap& h = heaps_.at(std::size_t(heap));
    h.levels.emplace_back();
    return int(h.levels.size()) - 1;
}

void ArchivePlayer::restoreLevel(int heap, int level) {
    // 02097c70: free the levels from `level` on (all for 0), then open a
    // fresh one.
    Heap& h = heaps_.at(std::size_t(heap));
    const std::size_t keep = level <= 0 ? 0 : std::size_t(level);
    while (h.levels.size() > keep) {
        for (auto it = h.levels.back().rbegin(); it != h.levels.back().rend(); ++it) {
            unloadFile(*it);
        }
        h.levels.pop_back();
    }
    h.levels.emplace_back();
    if (synced) {
        synced();
    }
}

bool ArchivePlayer::loadFile(std::uint32_t file, int heap) {
    if (isLoaded(file)) {
        return true;
    }
    if (heap < 0 || std::size_t(heap) >= heaps_.size() || archive_.file(file).empty()) {
        return false;
    }
    // The heaps' sizes (0x9c400, 0x4b000) are not modelled: the game's
    // groups fit.
    heaps_[std::size_t(heap)].levels.back().push_back(file);
    loaded_[file] = heap;
    return true;
}

void ArchivePlayer::unloadFile(std::uint32_t file) {
    // The heap's free callbacks (0209858c / 020985b4 / 020985e4): what plays
    // from the file stops, banks forget wave archives.
    if (!loaded_.erase(file)) {
        return;
    }
    const auto data = archive_.file(file);
    for (auto& [id, b] : banks_) {
        const auto info = archive_.bank(id);
        if (info && info->file == file) {
            sequencer_.stopUsingBank(b.get());
        }
        for (auto& w : b->waveArchives) {
            if (w.data() == data.data()) {
                w = {};
            }
        }
    }
    sequencer_.stopUsingCode(data);
    sequencer_.stopUsingWaves(data);
}

bool ArchivePlayer::loadBank(std::uint32_t id, unsigned flags, int heap) {
    // 02098010: the bank file (flag 2) and its wave archives (flag 4).
    const auto info = archive_.bank(id);
    if (!info) {
        return false;
    }
    if ((flags & 2) ? !loadFile(info->file, heap) : false) {
        return false;
    }
    for (const std::uint16_t w : info->waveArchives) {
        if (w == 0xffff) {
            continue;
        }
        const auto file = archive_.waveArchive(w);
        if (!file || ((flags & 4) ? !loadFile(*file, heap) : !isLoaded(*file))) {
            return false;
        }
    }
    return true;
}

const LoadedBank* ArchivePlayer::bank(std::uint32_t id) {
    // The bank as the driver sees it, with the wave archives linked into
    // its slots (020a0a98).
    const auto info = archive_.bank(id);
    if (!info || !isLoaded(info->file)) {
        return nullptr;
    }
    auto& b = banks_[id];
    if (!b) {
        b = std::make_unique<LoadedBank>();
    }
    b->data = archive_.file(info->file);
    for (unsigned i = 0; i < 4; ++i) {
        const auto file = info->waveArchives[i] == 0xffff
                              ? std::nullopt
                              : archive_.waveArchive(info->waveArchives[i]);
        b->waveArchives[i] =
            file && isLoaded(*file) ? archive_.file(*file) : std::span<const std::uint8_t>{};
    }
    return b.get();
}

bool ArchivePlayer::loadSequence(std::uint32_t sequence, int heap) {
    // 02097f2c with all flags: the bank, its waves and the sequence.
    const auto info = archive_.sequence(sequence);
    return info && loadBank(info->bank, 0xff, heap) && loadFile(info->file, heap);
}

bool ArchivePlayer::loadSequenceArchive(std::uint32_t id, int heap) {
    // 02097fb0.
    const auto file = archive_.sequenceArchive(id);
    return file && loadFile(*file, heap);
}

bool ArchivePlayer::loadGroup(std::uint32_t group, int heap) {
    // 02097e28: stops at the first failure.
    for (const GroupItem& item : archive_.group(group)) {
        bool ok = true;
        switch (item.kind) {
        case GroupItem::Sequence: {
            const auto info = archive_.sequence(item.id);
            ok = info && loadBank(info->bank, item.flags, heap) &&
                 (!(item.flags & 1) || loadFile(info->file, heap));
            break;
        }
        case GroupItem::Bank:
            ok = loadBank(item.id, item.flags, heap);
            break;
        case GroupItem::WaveArchive: {
            const auto file = archive_.waveArchive(item.id);
            ok = file && (!(item.flags & 4) || loadFile(*file, heap));
            break;
        }
        case GroupItem::SequenceArchive:
            ok = !(item.flags & 8) || loadSequenceArchive(item.id, heap);
            break;
        }
        if (!ok) {
            return false;
        }
    }
    return true;
}

// --- slots ------------------------------------------------------------------

void ArchivePlayer::insertByPriority(std::list<int>& list, int slot) {
    // 02096cc4 / 02096d14: after every entry of equal or lower priority.
    auto it = list.begin();
    while (it != list.end() &&
           slots_[std::size_t(*it)].priority <= slots_[std::size_t(slot)].priority) {
        ++it;
    }
    list.insert(it, slot);
}

int ArchivePlayer::allocate(Handle& handle, unsigned player, int priority) {
    // 02096ae8.
    if (handle.slot >= 0) { // 020966c4: the old sequence plays on, unbound
        slots_[std::size_t(handle.slot)].handle = nullptr;
        handle.slot = -1;
    }
    PlayerState& p = players_.at(player);
    if (p.slots.size() >= p.maxSequences) {
        if (p.slots.empty() || priority < slots_[std::size_t(p.slots.front())].priority) {
            return -1;
        }
        stopNow(p.slots.front());
    }
    // 02096da0.
    if (free_.empty()) {
        if (active_.empty() || priority < slots_[std::size_t(active_.front())].priority) {
            return -1;
        }
        stopNow(active_.front());
    }
    const int s = free_.front();
    free_.pop_front();
    Slot& slot = slots_[std::size_t(s)];
    slot = Slot{};
    slot.priority = std::uint8_t(priority);
    insertByPriority(active_, s);
    insertByPriority(p.slots, s);
    slot.player = int(player);
    slot.handle = &handle;
    handle.slot = s;
    return s;
}

void ArchivePlayer::prepare(int s, std::span<const std::uint8_t> code, std::uint32_t offset,
                            const LoadedBank* bank) {
    // 02096b8c: the driver prepares the slot's player; it starts in a later
    // frame, once the driver has taken the command.
    Slot& slot = slots_[std::size_t(s)];
    sequencer_.prepare(unsigned(s), code, offset, bank);
    if (players_[std::size_t(slot.player)].channels != 0) {
        sequencer_.setTrackChannels(unsigned(s), 0xffff,
                                    players_[std::size_t(slot.player)].channels);
    }
    // 02096c7c.
    slot.prepared = false;
    slot.centibels = 0;
    slot.volume = 127;
    slot.userVolume = 127;
    slot.fader = Fader{};
    slot.fader.set(0x7f00, 1);
    slot.tag = sequencer_.batchesSent() + 1;
    slot.startPending = true;
    slot.state = 1;
}

void ArchivePlayer::release(int s) {
    // 02096e08: back on the free list.
    Slot& slot = slots_[std::size_t(s)];
    if (slot.handle) {
        slot.handle->slot = -1;
        slot.handle = nullptr;
    }
    if (slot.player >= 0) {
        players_[std::size_t(slot.player)].slots.remove(s);
    }
    active_.remove(s);
    free_.push_back(s);
    slot.state = 0;
    slot.player = -1;
}

void ArchivePlayer::stopNow(int s) {
    // 02096d68.
    if (slots_[std::size_t(s)].state == 2) {
        sequencer_.setPlayerVolume(unsigned(s), kStopVolume);
    }
    sequencer_.stop(unsigned(s));
    release(s);
}

void ArchivePlayer::setPriority(int s, int priority) {
    // 02096edc.
    Slot& slot = slots_[std::size_t(s)];
    active_.remove(s);
    if (slot.player >= 0) {
        players_[std::size_t(slot.player)].slots.remove(s);
    }
    slot.priority = std::uint8_t(priority);
    if (slot.player >= 0) {
        insertByPriority(players_[std::size_t(slot.player)].slots, s);
    }
    insertByPriority(active_, s);
}

bool ArchivePlayer::startSequence(Handle& handle, std::uint32_t sequence) {
    // 020988a0 / 02098968.
    const auto info = archive_.sequence(sequence);
    if (!info) {
        return false;
    }
    const int s = allocate(handle, info->player, info->playerPriority);
    if (s < 0) {
        return false;
    }
    const LoadedBank* b = loadBank(info->bank, 0, -1) ? bank(info->bank) : nullptr;
    if (!b || !isLoaded(info->file)) {
        release(s);
        return false;
    }
    prepare(s, sequenceCode(archive_.file(info->file)), 0, b);
    slots_[std::size_t(s)].volume = info->volume;
    sequencer_.setPlayerPriority(unsigned(s), info->channelPriority);
    slots_[std::size_t(s)].kind = 1;
    slots_[std::size_t(s)].id = sequence;
    return true;
}

bool ArchivePlayer::startArchived(Handle& handle, std::uint32_t archive, std::uint32_t index) {
    // 020988e8 / 02098a64.
    const auto file = archive_.sequenceArchive(archive);
    if (!file || !isLoaded(*file)) {
        return false;
    }
    const auto data = archive_.file(*file);
    const auto entry = archivedSequence(data, index);
    if (!entry) {
        return false;
    }
    const int s = allocate(handle, entry->player, entry->playerPriority);
    if (s < 0) {
        return false;
    }
    const LoadedBank* b = loadBank(entry->bank, 0, -1) ? bank(entry->bank) : nullptr;
    if (!b) {
        release(s);
        return false;
    }
    prepare(s, sequenceCode(data), entry->offset, b);
    slots_[std::size_t(s)].volume = entry->volume;
    sequencer_.setPlayerPriority(unsigned(s), entry->channelPriority);
    slots_[std::size_t(s)].kind = 2;
    slots_[std::size_t(s)].id = archive;
    slots_[std::size_t(s)].index = index;
    return true;
}

void ArchivePlayer::stop(Handle& handle, int frames) {
    // 02096bdc: a fade-out drops the slot's priority to 0.
    if (handle.slot < 0 || slots_[std::size_t(handle.slot)].state == 0) {
        return;
    }
    const int s = handle.slot;
    if (frames == 0) {
        stopNow(s);
        return;
    }
    slots_[std::size_t(s)].fader.set(0, frames);
    setPriority(s, 0);
    slots_[std::size_t(s)].state = 2;
}

void ArchivePlayer::moveVolume(Handle& handle, int target, int frames) {
    if (handle.slot >= 0 && slots_[std::size_t(handle.slot)].state != 2) {
        slots_[std::size_t(handle.slot)].fader.set(target << 8, frames);
    }
}

void ArchivePlayer::setVolume(Handle& handle, int volume) {
    if (handle.slot >= 0) {
        slots_[std::size_t(handle.slot)].volume = std::uint8_t(volume);
    }
}

int ArchivePlayer::playingSequence(std::uint32_t sequence) const {
    return int(std::count_if(active_.begin(), active_.end(), [&](int s) {
        return slots_[std::size_t(s)].kind == 1 && slots_[std::size_t(s)].id == sequence;
    }));
}

int ArchivePlayer::playingArchive(std::uint32_t archive) const {
    return int(std::count_if(active_.begin(), active_.end(), [&](int s) {
        return slots_[std::size_t(s)].kind == 2 && slots_[std::size_t(s)].id == archive;
    }));
}

int ArchivePlayer::playingArchived(std::uint32_t archive, std::uint32_t index) const {
    return int(std::count_if(active_.begin(), active_.end(), [&](int s) {
        const Slot& slot = slots_[std::size_t(s)];
        return slot.kind == 2 && slot.id == archive && slot.index == index;
    }));
}

void ArchivePlayer::frame() {
    // 02096984, every slot in priority order.
    const std::uint32_t running = runningPlayers ? runningPlayers() : sequencer_.activePlayers();
    for (auto it = active_.begin(); it != active_.end();) {
        const int s = *it++;
        Slot& slot = slots_[std::size_t(s)];
        if (!slot.prepared && sequencer_.batchesDone() >= slot.tag) {
            slot.prepared = true;
        }
        if (slot.prepared && !(running >> s & 1)) {
            release(s); // the sequence has ended
            continue;
        }
        slot.fader.step();
        const int player = players_[std::size_t(slot.player)].volume;
        const int total = decibels_[std::size_t(player & 127)] + decibels_[slot.userVolume & 127u] +
                          decibels_[slot.volume & 127u] +
                          decibels_[std::size_t((slot.fader.value() >> 8) & 127)];
        const auto centibels = std::int16_t(std::clamp(total, -0x8000, kMaxVolume));
        if (centibels != slot.centibels) {
            sequencer_.setPlayerVolume(unsigned(s), centibels);
            slot.centibels = centibels;
        }
        if (slot.state == 2 && slot.fader.done()) {
            stopNow(s);
            continue;
        }
        if (slot.startPending) { // queued behind the prepare command
            sequencer_.start(unsigned(s));
            slot.startPending = false;
        }
    }
    sequencer_.flush(); // 020a0428
}

} // namespace picarat::sdat
