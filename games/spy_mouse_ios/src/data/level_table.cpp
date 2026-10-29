#include "data/level_table.hpp"

#include <cstdlib>

namespace doc::data {

std::optional<LevelTable> LevelTable::load(const GameFiles& files, std::string& error) {
    auto root = loadChunkTree(files, "level_data.xml.brd");
    if (!root) {
        error = "level table missing or damaged";
        return std::nullopt;
    }
    LevelTable table;
    table.root_ = std::move(*root);
    int worldIndex = 0;
    for (const BrdNode* world : table.root_.childrenNamed("world")) {
        for (const BrdNode* node : world->childrenNamed("level")) {
            LevelInfo info;
            info.world = worldIndex;
            info.worldBackground = node->getBool("world_bg").value_or(false);
            info.tutorialAfterAttempts = node->getInt("attemptsBeforeShowingTutorial").value_or(0);
            info.node = node;
            info.uid = node->getInt("uid").value_or(0);
            info.map = node->getInt("map").value_or(0);
            info.number = node->getString("num").value_or("");
            info.nameKey = node->getString("name").value_or("");
            if (const BrdNode* type = node->child("type")) {
                info.theme = type->getString("theme").value_or("");
                info.mode = type->getString("mode").value_or("");
                info.trailLength = type->getInt("trailLength").value_or(0);
                info.story = type->getString("story").value_or("");
            }
            if (const BrdNode* unlocked = node->child("unlocked")) {
                info.by1 = unlocked->getInt("by1").value_or(-1);
                info.by2 = unlocked->getInt("by2").value_or(-1);
                info.secret = unlocked->getBool("secret").value_or(false);
                info.autostart = unlocked->getInt("autostart").value_or(-1);
                info.clue = unlocked->getInt("clue").value_or(-1);
            }
            if (const BrdNode* music = node->child("music")) info.music = music->getString("play").value_or("");
            if (const BrdNode* times = node->child("times")) {
                info.startTime = times->getInt("start").value_or(0);
                info.bronze = times->getInt("bronze").value_or(0);
                info.silver = times->getInt("silver").value_or(0);
                info.gold = times->getInt("gold").value_or(0);
                info.platinum = times->getInt("platinum").value_or(0);
            }
            if (const BrdNode* goals = node->child("goals")) {
                // Top-level items at commas; an item in parentheses is a group of items.
                auto split = [](const std::string& list) {
                    std::vector<std::vector<std::string>> items(1);
                    std::string item;
                    int depth = 0;
                    auto take = [&] {
                        const auto a = item.find_first_not_of(' '), b = item.find_last_not_of(' ');
                        items.back().push_back(a == std::string::npos ? std::string() : item.substr(a, b - a + 1));
                        item.clear();
                    };
                    for (char c : list + ",") {
                        if (c == '(') {
                            ++depth;
                        } else if (c == ')') {
                            --depth;
                        } else if (c == ',') {
                            take();
                            if (depth == 0) items.emplace_back();
                        } else {
                            item.push_back(c);
                        }
                    }
                    items.pop_back();
                    return items;
                };
                const auto types = split(goals->getString("type").value_or(""));
                const auto values = split(goals->getString("value").value_or(""));
                for (std::size_t i = 0; i < types.size(); ++i) {
                    LevelInfo::Goal g;
                    for (std::size_t j = 0; j < types[i].size(); ++j) {
                        const int v = i < values.size() && j < values[i].size() ? std::atoi(values[i][j].c_str()) : 0;
                        g.parts.emplace_back(types[i][j], v);
                    }
                    if (!g.parts.empty()) g.type = g.parts.front().first, g.value = g.parts.front().second;
                    info.goals.push_back(g);
                }
            }
            table.levels_.push_back(std::move(info));
        }
        ++worldIndex;
    }
    return table;
}

const LevelInfo* LevelTable::byNumber(const std::string& number) const {
    // "#uid" picks a level by its uid (the boss chase and the boss fight share "1-Boss").
    if (!number.empty() && number[0] == '#') return byUid(std::atoi(number.c_str() + 1));
    for (const LevelInfo& level : levels_)
        if (level.number == number) return &level;
    return nullptr;
}

const LevelInfo* LevelTable::byUid(int uid) const {
    const int i = indexOf(uid);
    return i < 0 ? nullptr : &levels_[std::size_t(i)];
}

int LevelTable::indexOf(int uid) const {
    for (std::size_t i = 0; i < levels_.size(); ++i)
        if (levels_[i].uid == uid) return int(i);
    return -1;
}

}  // namespace doc::data
