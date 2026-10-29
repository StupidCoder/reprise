#pragma once
#include "story/a2_game_state.hpp"
#include "system/interaction.hpp"
#include <filesystem>
#include <optional>

namespace picarat {
// Picarat's own save format (not a DS save file). The story saves itself at
// safe points: entering a room, or the start of an event or puzzle. A
// checkpoint holds the game's 0x320-byte progress block (the same bytes the
// original keeps at state +0xc8: flags, puzzles, items, picarats, ...) plus
// the few scene-router fields needed to continue from that point.
struct Checkpoint {
    NameEditor name;
    SaveProgress progress;
    std::uint8_t scene = scene::Room, following = scene::Room; // state +0x478 / +0x479
    std::uint16_t eventId = 0;                                 // state +0x5e7c
    std::int16_t puzzle = -1;                                  // state +0x7e88 (internal ID)
    bool retryVariant = false, revisitVariant = false;         // state +0x7ec1 / +0x8228
    bool cleared = false;                                      // state +0x821e
    std::uint32_t randomState = 1;
};

// Reads and writes one checkpoint file; a damaged file is rejected.
class CheckpointStore {
public:
    explicit CheckpointStore(std::filesystem::path path) : path_(std::move(path)) {}

    std::optional<Checkpoint> load() const;
    void save(const Checkpoint& checkpoint) const;

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};
} // namespace picarat
