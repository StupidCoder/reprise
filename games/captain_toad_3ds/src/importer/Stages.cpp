// See Stages.hpp.
#include "Stages.hpp"

#include "Importer.hpp"
#include "Inputs.hpp"
#include "StageExport.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <unistd.h>

namespace captain_toad {

namespace fs = std::filesystem;
using json::Value;

std::string courseTypeName(const std::string& type) {
    static const std::pair<const char*, const char*> names[] = {
        {"プロローグ", "Prologue"},
        {"通常", "Course"},
        {"挿絵", "Illustration"},
        {"挿絵デモ", "Cutscene"},
        {"ボーナス", "Bonus"},
        {"ミニゲーム", "Minigame"},
        {"特殊(3Dワールド)", "Special (3D World)"},
        {"特殊(チェリー)", "Special (cherry)"},
        {"特殊(マネック)", "Special (mimic)"},
        {"特殊(プロローグ)", "Special (prologue)"},
        {"特殊(その他)", "Special (other)"},
        {"特殊(ボーナス)", "Special (bonus)"},
        {"トリックアート", "Trick art"},
    };
    for (const auto& [jp, en] : names) {
        if (type == jp) {
            return en;
        }
    }
    return type;
}

std::vector<CourseEntry> courseList(const fs::path& dataDirectory) {
    Inputs in(romDirectory(dataDirectory));
    const Value list = in.byml("SystemData/StageList.szs", "StageList.byml");
    auto integer = [](const Value& o, const char* k) {
        const Value* v = o.find(k);
        return v && v->isNumber() ? int(v->integer()) : 0;
    };
    auto text = [](const Value& o, const char* k) {
        const Value* v = o.find(k);
        return v && v->isString() ? v->str() : std::string();
    };
    std::vector<CourseEntry> result;
    for (const auto& season : list.at("SeasonList").items()) {
        for (const auto& s : season.at("StageList").items()) {
            CourseEntry e;
            e.season = integer(season, "SeasonId");
            e.courseId = integer(s, "CourseId");
            e.stageId = integer(s, "StageId");
            e.pageId = integer(s, "PageId");
            e.openPageNum = integer(s, "OpenPageNum");
            e.collectItemNum = integer(s, "CollectItemNum");
            e.collectLockNum = integer(s, "CollectLockNum");
            e.challengeTime = integer(s, "ChallengeTime");
            e.stage = text(s, "StageName");
            e.type = text(s, "StageType");
            e.player = text(s, "Player");
            e.badgeCondition = text(s, "BadgeCondition1");
            e.badgeParam = integer(s, "BadgeCondition1Param");
            e.clipping = text(s, "Clipping") != "無効";
            e.playable = Inputs::has("StageData/" + e.stage + "Map1.szs");
            result.push_back(e);
        }
    }
    return result;
}

fs::path stageDirectory(const fs::path& dataDirectory, const std::string& stage) {
    return dataDirectory / "Stages" / stage;
}

bool stageReady(const fs::path& dataDirectory, const std::string& stage) {
    std::ifstream f(stageDirectory(dataDirectory, stage) / "scene.json");
    if (!f) {
        return false;
    }
    std::stringstream s;
    s << f.rdbuf();
    return s.str().find("\"stage_schema\": " + std::to_string(kStageSchema) + ",") != std::string::npos;
}

fs::path buildStage(const fs::path& dataDirectory, const std::string& stage,
                    const std::function<void(double, const std::string&)>& progress) {
    const fs::path target = stageDirectory(dataDirectory, stage);
    if (!stageReady(dataDirectory, stage)) {
        const fs::path staging = dataDirectory / "Stages" / (".build-" + std::to_string(getpid()));
        fs::remove_all(staging);
        try {
            Inputs in(romDirectory(dataDirectory));
            exportStage(in, stage, staging, progress);
            fs::remove_all(target);
            fs::rename(staging, target);
        } catch (...) {
            std::error_code ignored;
            fs::remove_all(staging, ignored);
            throw;
        }
    }
    // Keep the most recently used stages only (the cache is rebuilt from the ROM copy anyway).
    constexpr size_t kKeep = 6;
    fs::last_write_time(target, fs::file_time_type::clock::now());
    std::vector<std::pair<fs::file_time_type, fs::path>> built;
    for (const auto& d : fs::directory_iterator(dataDirectory / "Stages")) {
        if (d.is_directory() && d.path().filename().string()[0] != '.') {
            built.emplace_back(d.last_write_time(), d.path());
        }
    }
    std::sort(built.begin(), built.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    for (size_t i = kKeep; i < built.size(); ++i) {
        std::error_code ignored;
        fs::remove_all(built[i].second, ignored);
    }
    return target;
}

}   // namespace captain_toad
