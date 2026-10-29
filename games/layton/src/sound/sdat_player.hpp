#pragma once
// The game's side of the first adventure's sound: the sound-archive player
// of the SDK libraries on the main processor (ARM9 02096458..02098f14),
// rewritten from that code. It loads archive files into sound heaps, starts
// sequences on players and fades them, and hands commands to the driver's
// sequencer (sdat_sequencer.hpp) once a frame.
//
// Heaps: files load into the top level of a heap; saving a level opens a
// new one, restoring a level unloads everything loaded since (stopping what
// still plays from those files). A sequence needs its file, its bank and
// the bank's wave archives loaded; the player never loads them itself here
// (the game's players have no heaps of their own).
//
// Players and slots: 16 sequence slots, each started on one of the archive's
// players. A player plays at most its count of sequences; a new one takes
// the place of the lowest priority one if its own priority is not lower;
// when all 16 slots are busy the lowest priority slot overall goes. Every
// frame a slot's volume is the sum of the player's, its own, the sequence's
// and a fader's (in centibels, from the program's table at 020cd908); a
// fade-out stops the slot when it ends.
#include "sound/sdat.hpp"
#include "sound/sdat_sequencer.hpp"
#include <array>
#include <functional>
#include <list>
#include <map>
#include <memory>

namespace picarat::sdat {

class ArchivePlayer {
public:
    // A caller's reference to what it started (NNSSndHandle); unbound when
    // the sequence ends or another start takes it.
    struct Handle {
        int slot = -1;
    };

    // `decibels`: the ARM9 program's volume table (128 entries).
    ArchivePlayer(const Archive& archive, Sequencer& sequencer,
                  std::array<std::int16_t, 128> decibels);
    ArchivePlayer(const ArchivePlayer&) = delete;
    ArchivePlayer& operator=(const ArchivePlayer&) = delete;

    // Heaps (02097a78 create, 02097c24 save a level, 02097c70 restore one).
    int createHeap();
    int saveLevel(int heap);
    void restoreLevel(int heap, int level);
    // Loading (02097de4 a group, 02097dfc a sequence with its bank and
    // waves); false when something is missing.
    bool loadGroup(std::uint32_t group, int heap);
    bool loadSequence(std::uint32_t sequence, int heap);
    bool loadSequenceArchive(std::uint32_t archive, int heap);

    // 020988a0 / 020988e8: false when it cannot start.
    bool startSequence(Handle& handle, std::uint32_t sequence);
    bool startArchived(Handle& handle, std::uint32_t archive, std::uint32_t index);
    // 02096bdc: stop, fading out over `frames` (0: at once).
    void stop(Handle& handle, int frames);
    // 02096814: fade the slot's volume to `target` (0..127) over `frames`.
    void moveVolume(Handle& handle, int target, int frames);
    // 02096800.
    void setVolume(Handle& handle, int volume);
    // 020966e4 / 02096738 / 0209678c: sequences playing.
    int playingSequence(std::uint32_t sequence) const;
    int playingArchive(std::uint32_t archive) const;
    int playingArchived(std::uint32_t archive, std::uint32_t index) const;

    // 020964e0, once a frame: slot volumes, starts, ends; then the
    // commands go to the sequencer.
    void frame();

    // Called when a level's files are freed (02097dc4: the game flushes and
    // waits for the driver to take the stop commands).
    std::function<void()> synced;

    // What the frame reads as the players running (the driver's shared word
    // +4, 020a0958); unset: the sequencer's current value.
    std::function<std::uint32_t()> runningPlayers;

private:
    struct Fader { // 02098e88..02098f14, 8.8 fixed point
        int origin = 0, target = 0, counter = 0, frames = 0;
        int value() const;
        void set(int to, int length);
        void step();

        bool done() const { return frames <= counter; }
    };

    struct Slot { // 0x44-byte sequence records
        Handle* handle = nullptr;
        int player = -1;
        int state = 0; // 0 free, 1 playing, 2 fading out to stop
        bool prepared = false, startPending = false;
        std::uint32_t tag = 0;
        int kind = 0; // 1 sequence, 2 archived sequence
        std::uint32_t id = 0, index = 0;
        std::uint8_t priority = 0, volume = 127, userVolume = 127;
        std::int16_t centibels = 0;
        Fader fader;
    };

    struct PlayerState {
        std::uint8_t maxSequences = 0, volume = 127;
        std::uint16_t channels = 0;
        std::list<int> slots; // by priority, lowest first
    };

    struct Heap {
        std::vector<std::vector<std::uint32_t>> levels{{}}; // files per level
    };

    const Archive& archive_;
    Sequencer& sequencer_;
    std::array<std::int16_t, 128> decibels_;
    std::array<Slot, 16> slots_{};
    std::array<PlayerState, 32> players_{};
    std::list<int> free_, active_; // active: by priority, lowest first
    std::vector<Heap> heaps_;
    std::map<std::uint32_t, int> loaded_;                        // file -> heap
    std::map<std::uint32_t, std::unique_ptr<LoadedBank>> banks_; // bank id -> links

    bool loadFile(std::uint32_t file, int heap);

    bool isLoaded(std::uint32_t file) const { return loaded_.contains(file); }

    void unloadFile(std::uint32_t file);
    bool loadBank(std::uint32_t bank, unsigned flags, int heap);
    const LoadedBank* bank(std::uint32_t id);

    int allocate(Handle& handle, unsigned player, int priority);
    void insertByPriority(std::list<int>& list, int slot);
    void prepare(int slot, std::span<const std::uint8_t> code, std::uint32_t offset,
                 const LoadedBank* bank);
    void stopNow(int slot);
    void release(int slot);
    void setPriority(int slot, int priority);
};

} // namespace picarat::sdat
