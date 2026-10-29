#pragma once
// The sound archive the first adventure keeps its music and effects in
// (sound/sound_data.sdat, the SDK's SDAT container): info tables that say
// which files make up a sequence (its bank of instruments, the bank's wave
// archives), which player plays it and how loud, how sequences are grouped
// for loading, and the files themselves.
//
// File kinds inside: sequences (a byte-code score per piece of music),
// sequence archives (many short sequences: the effects), banks (instruments:
// which sample or tone, its key range, envelope and pan) and wave archives
// (the samples). The game reads the tables through the SDK's archive routines
// (ARM9 020976c8 sequences, 0209772c sequence archives, 02097790 banks,
// 020977f4 wave archives, 02097858 players, 020978bc groups; the file table
// at 02097a10).
#include "system/resources.hpp"
#include <array>
#include <optional>
#include <span>
#include <vector>

namespace picarat::sdat {

struct SequenceInfo {
    std::uint32_t file = 0;
    std::uint16_t bank = 0;
    std::uint8_t volume = 0, channelPriority = 0, playerPriority = 0, player = 0;
};

struct BankInfo {
    std::uint32_t file = 0;
    std::array<std::uint16_t, 4> waveArchives{}; // 0xffff: none
};

struct PlayerInfo {
    std::uint8_t maxSequences = 0;
    std::uint16_t channels = 0; // channels its sequences may take (0: any)
    std::uint32_t heapSize = 0;
};

// One entry of a load group: what to load and which parts of it.
struct GroupItem {
    enum Kind : std::uint8_t { Sequence = 0, Bank = 1, WaveArchive = 2, SequenceArchive = 3 };

    Kind kind = Sequence;
    std::uint8_t flags = 0; // 1 sequence, 2 bank, 4 wave archives, 8 sequence archive
    std::uint32_t id = 0;
};

class Archive {
public:
    explicit Archive(Bytes data);

    std::optional<SequenceInfo> sequence(std::uint32_t id) const;
    std::optional<std::uint32_t> sequenceArchive(std::uint32_t id) const; // its file
    std::optional<BankInfo> bank(std::uint32_t id) const;
    std::optional<std::uint32_t> waveArchive(std::uint32_t id) const; // its file
    std::optional<PlayerInfo> player(std::uint32_t id) const;
    std::vector<GroupItem> group(std::uint32_t id) const;

    std::size_t fileCount() const { return files_.size(); }

    // A file's bytes (empty when the id is out of range).
    std::span<const std::uint8_t> file(std::uint32_t id) const;

private:
    Bytes data_;
    std::size_t info_ = 0;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> files_; // offset, size
    // The info record of `id` in table `table` (0 sequences ... 5 groups), or
    // nothing when absent.
    std::optional<std::size_t> record(unsigned table, std::uint32_t id) const;
};

// A sequence archive (SSAR): its sequences share one byte-code block; each
// entry gives where its sequence starts and how it plays (ARM9 02098e48).
struct ArchivedSequence {
    std::uint32_t offset = 0; // into the archive's byte code
    std::uint16_t bank = 0;
    std::uint8_t volume = 0, channelPriority = 0, playerPriority = 0, player = 0;
};

// The byte code of a sequence (SSEQ) or sequence archive (SSAR) file.
std::span<const std::uint8_t> sequenceCode(std::span<const std::uint8_t> file);
std::optional<ArchivedSequence> archivedSequence(std::span<const std::uint8_t> file,
                                                 std::uint32_t index);

} // namespace picarat::sdat
