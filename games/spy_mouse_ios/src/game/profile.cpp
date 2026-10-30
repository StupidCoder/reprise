#include "game/profile.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace doc::game {

// "current <uid>", "strength <n>", "kiska <owned> <here> <later> <last> <step> <shown>", then
// one line per level (a "world <n>" line is read but not written: the original starts on the
// current level's world, run w2_back):
// "<uid> <best> <goal1> <goal2> <goal3> <plays> <completed> <unlocked> <fresh>".
bool Profile::load(const std::filesystem::path& file) {
    std::ifstream in(file);
    if (!in) return false;
    std::stringstream text;
    text << in.rdbuf();
    return fromText(text.str());
}

bool Profile::fromText(const std::string& text) {
    std::istringstream in(text);
    levels_.clear();
    for (std::string line; std::getline(in, line);) {
        std::istringstream ss(line);
        std::string first;
        if (!(ss >> first)) continue;
        if (first == "current") {
            ss >> currentLevel;
            continue;
        }
        if (first == "world") {
            ss >> world;
            continue;
        }
        if (first == "strength") {
            ss >> strength;
            continue;
        }
        if (first == "dossiers") {
            for (std::size_t i = 0; i < dossiers.size(); ++i) {
                int found = 0, fresh = 0;
                ss >> found >> fresh;
                dossiers[i] = found != 0;
                newDossiers[i] = fresh != 0;
            }
            continue;
        }
        if (first == "cheese") {
            for (int& n : cheese) ss >> n;
            continue;
        }
        if (first == "powerups") {
            // (Kinds come from the program. The active flags belong to the attempt being played,
            // kept for quick saves; a new attempt clears them.)
            int met = 0, done = 0;
            ss >> met >> done >> sincePowerupUse;
            metDigger = met != 0;
            powerupTutorialDone = done != 0;
            for (PowerupRecord& r : powerups) {
                int active = 0, introduced = 0;
                ss >> r.count >> r.bought >> r.used >> active >> introduced;
                r.active = active != 0;
                r.introduced = introduced != 0;
            }
            continue;
        }
        if (first == "kiska") {
            int owned = 0, here = 0, later = 0;
            ss >> owned >> here >> later >> kiska.lastShown >> kiska.step >> kiska.shown;
            kiska.owned = owned != 0;
            kiska.offeredHere = here != 0;
            kiska.offeredLater = later != 0;
            continue;
        }
        LevelRecord r;
        int g0 = 0, g1 = 0, g2 = 0, completed = 0, unlocked = 0, fresh = 0;
        if (ss >> r.best >> g0 >> g1 >> g2 >> r.plays >> completed >> unlocked >> fresh) {
            r.goals = {g0 != 0, g1 != 0, g2 != 0};
            r.completed = completed != 0;
            r.unlocked = unlocked != 0;
            r.fresh = fresh != 0;
            levels_[std::atoi(first.c_str())] = r;
        }
    }
    return true;
}

bool Profile::save(const std::filesystem::path& file) const {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    // Written next to the file and renamed over it, so a failed write keeps the earlier one.
    std::filesystem::path staging = file;
    staging += ".new";
    {
        std::ofstream out(staging);
        if (!out) return false;
        out << toText();
        if (!out) return false;
    }
    std::filesystem::rename(staging, file, ec);
    return !ec;
}

std::string Profile::toText() const {
    std::ostringstream out;
    out << "current " << currentLevel << '\n' << "strength " << strength << '\n';
    out << "kiska " << kiska.owned << ' ' << kiska.offeredHere << ' ' << kiska.offeredLater << ' ' << kiska.lastShown
        << ' ' << kiska.step << ' ' << kiska.shown << '\n';
    out << "dossiers";
    for (std::size_t i = 0; i < dossiers.size(); ++i) out << ' ' << dossiers[i] << ' ' << newDossiers[i];
    out << '\n';
    out << "cheese";
    for (int n : cheese) out << ' ' << n;
    out << '\n';
    out << "powerups " << metDigger << ' ' << powerupTutorialDone << ' ' << sincePowerupUse;
    for (const PowerupRecord& r : powerups) out << ' ' << r.count << ' ' << r.bought << ' ' << r.used << ' ' << r.active << ' ' << r.introduced;
    out << '\n';
    for (const auto& [uid, r] : levels_)
        out << uid << ' ' << r.best << ' ' << r.goals[0] << ' ' << r.goals[1] << ' ' << r.goals[2] << ' ' << r.plays
            << ' ' << r.completed << ' ' << r.unlocked << ' ' << r.fresh << '\n';
    return out.str();
}

void Profile::keepProgressOf(const Profile& other) {
    for (const auto& [uid, theirs] : other.levels_) {
        LevelRecord& r = levels_[uid];
        if (theirs.best > r.best) r.best = theirs.best;
        for (std::size_t i = 0; i < 3; ++i) r.goals[i] = r.goals[i] || theirs.goals[i];
        r.plays = std::max(r.plays, theirs.plays);
        r.completed = r.completed || theirs.completed;
        r.unlocked = r.unlocked || theirs.unlocked;
    }
    strength = std::max(strength, other.strength);
    for (std::size_t k = 0; k < cheese.size(); ++k) cheese[k] = std::max(cheese[k], other.cheese[k]);
    for (std::size_t i = 0; i < dossiers.size(); ++i) dossiers[i] = dossiers[i] || other.dossiers[i];
    if (other.kiska.shown > kiska.shown || other.kiska.lastShown > kiska.lastShown) kiska = other.kiska;
    // The stock follows the newer profile (its cheese was spent there); which power-ups are on
    // belongs to the restored attempt.
    for (std::size_t t = 0; t < powerups.size(); ++t) {
        const bool active = powerups[t].active;
        powerups[t] = other.powerups[t];
        powerups[t].active = active;
    }
    metDigger = metDigger || other.metDigger;
    powerupTutorialDone = powerupTutorialDone || other.powerupTutorialDone;
}

void Profile::usePowerup(Powerup t) {
    PowerupRecord& r = powerup(t);
    if (r.kind > 1 || (r.count < 1 && r.count != -1)) return;
    if (r.kind == 1) {
        if (r.active) return;
        r.active = true;
    }
    ++r.used;
    if (r.count != -1) --r.count;
}

void Profile::purchasePowerup(Powerup t, int n) {
    PowerupRecord& r = powerup(t);
    if (r.kind > 1) return;
    if (n == -1) {
        r.count = -1;
        return;
    }
    r.count += n;
    r.bought += n;
}

LevelRecord Profile::record(int uid) const {
    auto it = levels_.find(uid);
    return it == levels_.end() ? LevelRecord{} : it->second;
}

void Profile::complete(const data::LevelTable& table, int uid, int total, const std::array<bool, 3>& goalsWon,
                       bool foundSecret) {
    LevelRecord& r = levels_[uid];
    r.completed = true;
    if (r.best < 0 || r.best < total) r.best = total;
    for (std::size_t i = 0; i < 3; ++i) r.goals[i] = r.goals[i] || goalsWon[i];
    ++r.plays;
    for (const data::LevelInfo& level : table.levels()) {
        LevelRecord& other = levels_[level.uid];
        // (The original keeps by1/by2 as table indices; uids that name no level never match.)
        const bool waits = (level.by1 == uid || level.by2 == uid) && table.byUid(uid);
        if (!waits || other.unlocked) {
            other.fresh = false;
            continue;
        }
        other.fresh = foundSecret == level.secret;
        if (other.fresh) other.unlocked = true;
    }
}

bool Profile::bossDefeated(const data::LevelTable& table, int world) const {
    for (const data::LevelInfo& level : table.levels())
        if (level.world == world && level.mode == "boss") return record(level.uid).completed;
    return false;
}

}  // namespace doc::game
