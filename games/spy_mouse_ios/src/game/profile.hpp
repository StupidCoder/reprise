// The player's progress (the original keeps it in its Profile: per level the best total, the
// goals won, the times played, and whether the level is open, new or won). The port stores it
// in its own small text file in the app's data folder; the original's save format is not
// decoded yet. Levels are identified by their uid (two table entries share "1-Boss").
#pragma once

#include "data/level_table.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>

namespace doc::game {

struct LevelRecord {
    int best = -1;                 // best total (+0xf8), -1 before the first completion
    std::array<bool, 3> goals{};   // goals won (+0x138, +0x150, +0x168)
    int plays = 0;                 // completions (+0x114)
    bool completed = false;        // +0xf6
    bool unlocked = false;         // +0xf7: shown on the map, playable
    bool fresh = false;            // +0xf4: unlocked since the map last showed it
    // Quick saves (state_io.hpp).
    template <class Io>
    void io(Io& io) { io(best, goals, plays, completed, unlocked, fresh); }
};

// What the profile keeps about Kiska's offer (Profile +0xd4..+0xf0; see Session::offerKiska).
struct KiskaOffers {
    bool owned = false;        // +0xd4: Kiska bought in the store (never, in the port)
    bool offeredHere = false;  // +0xe4: offered on map 758 (2-8) or after many lost levels
    bool offeredLater = false; // +0xe5: offered on map 521 (3-6)
    std::int64_t lastShown = 0;  // +0xe8: time() when it last showed
    int step = 0;              // +0xec: which lost-level count asks next (0..2)
    int shown = 0;             // +0xf0: how often it showed (at most twice)

    bool operator==(const KiskaOffers&) const = default;
    // Quick saves (state_io.hpp).
    template <class Io>
    void io(Io& io) { io(owned, offeredHere, offeredLater, lastShown, step, shown); }
};

class Profile {
public:
    bool load(const std::filesystem::path& file);
    bool save(const std::filesystem::path& file) const;
    // The same text as the file holds (quick saves keep a copy of the profile).
    std::string toText() const;
    bool fromText(const std::string& text);
    // Takes over every unlock, best total, won goal and completion `other` has beyond this
    // profile; nothing is lowered. (Loading an earlier quick save keeps progress made since.)
    void keepProgressOf(const Profile& other);
    LevelRecord record(int uid) const;
    LevelRecord& at(int uid) { return levels_[uid]; }
    // Profile::completeLevel @0x1735e8: the best total rises, won goals stay won, the count
    // goes up, and every locked level waiting for this one opens. Secret levels open only
    // from a run that found the secret, ordinary ones only from a run that did not.
    void complete(const data::LevelTable& table, int uid, int total, const std::array<bool, 3>& goalsWon,
                  bool foundSecret = false);
    // How many levels have been completed at least once (the launcher's summary).
    int completedLevels() const {
        int n = 0;
        for (const auto& entry : levels_) n += entry.second.completed ? 1 : 0;
        return n;
    }
    // Profile::isBossDefeated @0x171fec: the world's boss fight (mode "boss") is won.
    bool bossDefeated(const data::LevelTable& table, int world) const;

    // The level the mouse stands at on the map (the original's current level, +0x14); -1
    // before the first visit (the map then starts at uid 1).
    int currentLevel = -1;
    // The world the map shows (+0x18): moved on by the world card's continue after a world's
    // first boss win (SceneWorldCompleteResults::guiEvent @0x2187c8). Not saved: started again,
    // the original shows the current level's world (run w2_back); -1 means just that.
    int world = -1;
    // How many cheeses the mouse can carry (+0xc4, raised by levels that need more:
    // SceneGame::initGame @0x10639e with Profile::incPlayerStrength).
    int strength = 1;
    KiskaOffers kiska;
    // (1.1.1) The cheese tracker (Profile +0x1b0, CheeseTracker): counts by kind: 0..4 cheese
    // brought home by size, 5 world bonuses, 6 pickups, 7 bought, 8 spent. The balance the
    // results show (getCheeseBalance @0x221178) is kinds 0..5 and 7, less 8.
    std::array<int, 9> cheese{};
    // (1.1.1) The dossiers (Profile +0x20, 8 bytes each: found +4, new +5; the count +0x14).
    std::array<bool, 21> dossiers{};
    std::array<bool, 21> newDossiers{};   // found and not yet looked at in the backpack
    int dossiersFound() const {
        int n = 0;
        for (bool d : dossiers) n += d ? 1 : 0;
        return n;
    }
    // Profile::foundDossier @0x179d40.
    void foundDossier(int id) {
        if (id < 0 || id >= int(dossiers.size())) return;
        dossiers[std::size_t(id)] = true;
        newDossiers[std::size_t(id)] = true;
    }
    int cheeseBalance() const {
        int n = 0;
        for (int k = 0; k <= 5; ++k) n += cheese[std::size_t(k)];
        return n + cheese[7] - cheese[8];
    }

private:
    std::map<int, LevelRecord> levels_;
};

}  // namespace doc::game
