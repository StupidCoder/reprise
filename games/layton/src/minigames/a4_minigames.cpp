// See a4_minigames.hpp.
#include "minigames/a4_minigames.hpp"
#include "minigames/a4_fish.hpp"
#include "minigames/a4_mouse.hpp"
#include "minigames/a4_puppet.hpp"
#include "minigames/a4_train.hpp"
#include "story/a4_script_loop.hpp"
#include <cstdlib>

namespace picarat::adventure4 {

std::unique_ptr<Loop> makeMinigameLoop(Game& game, int id, std::span<const gss::Value> values) {
    if (id < 6000 || id > 6401 || values.size() < 7 || !values[6].isText()) {
        return nullptr;
    }
    if (id == 6100) {
        return std::make_unique<PuppetLoop>(game, id, values);
    }
    if (id == 6200) {
        return std::make_unique<TrainLoop>(game, id, values);
    }
    if (id == 6300) {
        return std::make_unique<MouseLoop>(game, id, values);
    }
    if (id == 6400) {
        return std::make_unique<FishLoop>(game, id, values);
    }
    if (id == 6401) {
        return std::make_unique<FishMenuLoop>(game, id, values);
    }
    return std::make_unique<ScriptLoop>(game, id, values);
}

// Flags by their numbers in lt4_def 2001 (a name's run continues with its
// indices; the scripts use the names): the railway's courses "＜電車＞コース"
// 87 + course (lt4_mini_train Select_CreateObject) and their clears
// "＜トレインクリア＞" 590 + course (ResultGoal); the acting actions
// "＜演技＞" 100 + action (ov38 0210673c) and the plays' clears
// "＜人形クリア＞演目１..３" 560..562; the fish tanks "＜フィッシュ＞" 58 + tank
// (lt4_mini_fish_intr_menu BuildObj) and their clears "＜フィッシュクリア＞"
// 570 + tank (lt4_mini_fish_intr_main Fish_Clear); the bag's tutorials
// 1339..1341.
namespace {
constexpr int kCourses = 87, kCourseCleared = 590, kActions = 100, kPlayCleared = 560;
constexpr int kTanks = 58, kTankCleared = 570, kTutorials = 1339;
constexpr int kCourseCount = 10, kActionCount = 24, kPlayCount = 3, kTankCount = 10;
} // namespace

void openMinigames(GameData& data) {
    for (int i = 1; i <= kCourseCount; ++i) {
        data.setFlagAt(kCourses + i, true);
    }
    for (int i = 1; i <= kActionCount; ++i) {
        data.setFlagAt(kActions + i, true);
    }
    for (int i = 1; i <= kTankCount; ++i) {
        data.setFlagAt(kTanks + i, true);
    }
    for (int i = 0; i < 3; ++i) {
        data.setFlagAt(kTutorials + i, true);
    }
}

std::vector<std::uint8_t> minigameRecord(const GameData& data) {
    // "PCR4MG1\n", then a line per entry: kind (F flag, V variable), key,
    // value, separated by tabs.
    std::string out = "PCR4MG1\n";
    for (const auto& [k, v] : data.flags) {
        out += "F\t" + k + "\t" + std::to_string(v) + "\n";
    }
    for (const auto& [k, v] : data.variables) {
        out += "V\t" + k + "\t" + std::to_string(v) + "\n";
    }
    return {out.begin(), out.end()};
}

void applyMinigameRecord(GameData& data, std::span<const std::uint8_t> record) {
    const std::string text(record.begin(), record.end());
    if (text.rfind("PCR4MG1\n", 0) != 0) {
        openMinigames(data);
        return;
    }
    std::size_t at = 8;
    while (at < text.size()) {
        const auto end = std::min(text.find('\n', at), text.size());
        const std::string line = text.substr(at, end - at);
        at = end + 1;
        const auto t1 = line.find('\t'), t2 = line.rfind('\t');
        if (line.size() < 3 || t1 != 1 || t2 <= t1) {
            continue;
        }
        const std::string key = line.substr(2, t2 - 2);
        const int value = std::atoi(line.c_str() + t2 + 1);
        (line[0] == 'F' ? data.flags : data.variables)[key] = value;
    }
}

MinigameProgress minigameProgress(const GameData& data) {
    MinigameProgress p;
    for (int i = 1; i <= kCourseCount; ++i) {
        p.courses += data.flagAt(kCourseCleared + i);
    }
    for (int i = 0; i < kPlayCount; ++i) {
        p.plays += data.flagAt(kPlayCleared + i);
    }
    for (int i = 1; i <= kTankCount; ++i) {
        p.tanks += data.flagAt(kTankCleared + i);
    }
    return p;
}

} // namespace picarat::adventure4
