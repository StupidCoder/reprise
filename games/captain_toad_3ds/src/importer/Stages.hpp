// The game's course list and the stage package cache.
//
// SystemData/StageList.szs (StageList.byml, read by 0x1991f8's loader) lists the courses of
// each season in book order: the stage name, the course and page ids, the stage type (prologue,
// normal course, book illustration, illustrated cutscene, bonus, game over, ...), the number of
// collectible gems, the pages a clear opens and the course's challenge.
//
// Stage packages are built from the verified ROM copy on first use and kept in DATA/Stages; the
// least recently used ones are removed so that the cache stays small.
#pragma once
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace captain_toad {

// One StageList entry. The type strings are the ROM's own (Japanese) values.
struct CourseEntry {
    int season = 0;
    int courseId = 0;
    int stageId = 0;
    int pageId = 0;
    int openPageNum = 0;      // pages the clear opens
    int collectItemNum = 0;   // gems in the course
    int collectLockNum = 0;   // gems needed to open it
    int challengeTime = 0;
    std::string stage;   // "PullUpStage", or an illustration page such as "Chapter1_1"
    std::string type;    // "プロローグ", "通常", "挿絵", "挿絵デモ", "ボーナス", "ミニゲーム", ...
    std::string player;
    std::string badgeCondition;
    int badgeParam = 0;
    bool clipping = true;
    // True when the entry is a stage that can be built and played (it has a map).
    bool playable = false;
};

// Readable English for the type value.
std::string courseTypeName(const std::string& type);

// The course list of the imported ROM (DATA/Rom). Throws ImportError when it is missing.
std::vector<CourseEntry> courseList(const std::filesystem::path& dataDirectory);

std::filesystem::path stageDirectory(const std::filesystem::path& dataDirectory, const std::string& stage);
bool stageReady(const std::filesystem::path& dataDirectory, const std::string& stage);
// Builds the stage's package unless it is ready; returns its directory.
std::filesystem::path buildStage(const std::filesystem::path& dataDirectory,
                                 const std::filesystem::path& cacheDirectory, const std::string& stage,
                                 const std::function<void(double, const std::string&)>& progress = {});

}   // namespace captain_toad
