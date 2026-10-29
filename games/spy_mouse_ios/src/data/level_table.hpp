// The level table (res/level_data.xml.brd): worlds and their levels with map number, theme,
// music, medal times and goals. Only the fields the reconstruction uses so far are typed;
// the full node stays available.
#pragma once

#include "data/brd.hpp"

#include <optional>
#include <string>
#include <vector>

namespace doc::data {

struct LevelInfo {
    int uid = 0;
    int map = 0;
    std::string number;    // "1-1"
    std::string nameKey;   // text key, e.g. "LEVEL_1_1"
    std::string theme;     // "house", ...
    std::string mode;      // "normal", "infinite" (the boss chase), "boss"
    int trailLength = 0;
    std::string story;     // cutscene played before the first visit (the IPA ships none)
    int world = 0;         // index of the <world> chunk it is listed in
    bool worldBackground = false;  // world_bg: its house sits behind the map's details
    // <unlocked>: finishing level `by1` or `by2` (uids, -1 none) unlocks this one; a secret
    // level only unlocks when that run found the secret. `autostart`: the level that follows
    // at once when this one is won (the boss chase leads into the fight). `clue`: the
    // backpack clue this level hands out (-1 none).
    int by1 = -1, by2 = -1;
    bool secret = false;
    int autostart = -1;
    int clue = -1;
    // attemptsBeforeShowingTutorial: the in-game tutorial's texts and pictures show from this
    // many attempts on (0: always).
    int tutorialAfterAttempts = 0;
    std::string music;     // "sound/music/CM_Apartments.m4a"
    int startTime = 0;     // seconds on the level clock
    int bronze = 0, silver = 0, gold = 0, platinum = 0;
    // <goals type="score, paths, time" value="95, 1, 5">: the level's three medals
    // (LevelGoal::parseGoalDefinition @0x1ff2ac splits both lists at commas).
    // A goal in parentheses, "(safe_hole, balloon)" with "(0, 0)", is several conditions that
    // must all hold: `parts` lists them; `type` and `value` are the first one's (its picture
    // and text; hypothesis, no combined text exists).
    struct Goal {
        std::string type;
        int value = 0;
        std::vector<std::pair<std::string, int>> parts;
    };
    std::vector<Goal> goals;
    const BrdNode* node = nullptr;  // the level's chunk (owned by LevelTable)
};

class LevelTable {
public:
    static std::optional<LevelTable> load(const GameFiles& files, std::string& error);
    const std::vector<LevelInfo>& levels() const { return levels_; }
    const LevelInfo* byNumber(const std::string& number) const;
    const LevelInfo* byUid(int uid) const;
    // Index in the table (the original's level index, Profile::getLevelIndex @0x171168), -1.
    int indexOf(int uid) const;

private:
    BrdNode root_;
    std::vector<LevelInfo> levels_;
};

}  // namespace doc::data
