// =============================================================================================
// Progress.hpp - the player's progress through the course book
// =============================================================================================
//
// Which courses are open follows the game's course list (SystemData/StageList, see
// importer/Stages.hpp): clearing an entry opens the entries whose page ids lie up to its
// OpenPageNum pages beyond its own (e.g. the prologue, page 0, opens pages 1 and 2: the first
// illustration and the first course). Observed on the original after the prologue: the first
// spread and the first course are open, the course is marked new, later courses are locked.
//
// Storage is Diorama's own (DATA/progress.json); the game's save format is not reproduced.
// =============================================================================================
#pragma once
#include "data/Package.hpp"
#include "importer/Stages.hpp"

#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace captain_toad::session {

struct Progress {
    std::set<std::string> cleared;     // stage names (and illustration pages) completed
    std::map<std::string, int> gems;   // collected gems per stage, bits 0-2
    int season = 1;
    int lives = 2;   // extra lives (the book shows 2 after the prologue)
    int page = 0;    // the spread last shown in the book (index into the season's spreads)
    // Courses whose page has been opened in the book (the game's course record bit 0, set by
    // 0x138134 when the course-open sequence shows the new page). A course that the clear rule
    // opens but that is not revealed yet is opened with the course-open sequence on the next
    // visit to the book.
    std::set<std::string> revealed;
    bool hasRevealed = false;   // false for progress files written before "revealed" existed
    // Courses started at least once (their record's bit 3, "new", is cleared when played).
    std::set<std::string> played;
    // The course played last (the game state's +0x3c +0xc, 0x2b89b8): the book opens at its spread.
    std::string lastPlayed;
    // True right after a cleared course (0x2b8470): the book then looks for pages to open.
    bool returnedFromClear = false;
    // True when the book follows a played course (cleared or left): it then opens without its
    // opening animation unless that course was a season's prologue (0x2facbc). Not saved.
    bool returnedFromStage = false;

    static std::filesystem::path file(const std::filesystem::path& dataDirectory) {
        return dataDirectory / "progress.json";
    }

    void load(const std::filesystem::path& dataDirectory) {
        const auto path = file(dataDirectory);
        if (!std::filesystem::exists(path)) {
            return;
        }
        const captain_toad::data::Value doc = captain_toad::data::readJson(path);
        captain_toad::data::JsonView d = doc;
        for (captain_toad::data::JsonView s : d["cleared"].items()) {
            cleared.insert(s.str());
        }
        for (const std::string& k : d["gems"].keys()) {
            gems[k] = d["gems"][k].i();
        }
        if (d["season"]) {
            season = d["season"].i();
        }
        if (d["page"]) {
            page = d["page"].i();
        }
        if (d["lives"]) {
            lives = d["lives"].i();
        }
        if (d["revealed"]) {
            hasRevealed = true;
            for (captain_toad::data::JsonView s : d["revealed"].items()) {
                revealed.insert(s.str());
            }
        }
        for (captain_toad::data::JsonView s : d["played"].items()) {
            played.insert(s.str());
        }
        if (d["last_played"]) {
            lastPlayed = d["last_played"].str();
        }
    }

    bool save(const std::filesystem::path& dataDirectory) const {
        std::ofstream f(file(dataDirectory));
        if (!f) {
            return false;
        }
        f << "{\n  \"cleared\": [";
        bool first = true;
        for (const auto& s : cleared) {
            f << (first ? "" : ", ") << "\"" << s << "\"";
            first = false;
        }
        f << "],\n  \"gems\": {";
        first = true;
        for (const auto& [k, v] : gems) {
            f << (first ? "" : ", ") << "\"" << k << "\": " << v;
            first = false;
        }
        f << "},\n  \"revealed\": [";
        first = true;
        for (const auto& s : revealed) {
            f << (first ? "" : ", ") << "\"" << s << "\"";
            first = false;
        }
        f << "],\n  \"played\": [";
        first = true;
        for (const auto& s : played) {
            f << (first ? "" : ", ") << "\"" << s << "\"";
            first = false;
        }
        f << "],\n  \"last_played\": \"" << lastPlayed << "\"";
        f << ",\n  \"season\": " << season << ",\n  \"page\": " << page << ",\n  \"lives\": " << lives << "\n}\n";
        return bool(f);
    }

    // True when the entry is open: the season's prologue counts as played once the opening has
    // been seen (cleared), and a cleared entry opens the next OpenPageNum page ids.
    bool open(const std::vector<captain_toad::CourseEntry>& courses, const captain_toad::CourseEntry& e) const {
        if (cleared.count(e.stage)) {
            return true;
        }
        for (const auto& c : courses) {
            if (c.season != e.season || !cleared.count(c.stage)) {
                continue;
            }
            if (e.pageId > c.pageId && e.pageId <= c.pageId + c.openPageNum) {
                return true;
            }
        }
        return false;
    }
};

}   // namespace captain_toad::session
