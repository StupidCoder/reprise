// =============================================================================================
// OpeningSoundKeeper.hpp - which sounds an actor plays, and when
// =============================================================================================
//
// Like effects (EffectKeeper.hpp), sounds are attached to actors through a data-driven "keeper".
// Each actor has an entry in the sound database SeDataBase (exported by tools/export_sound.py
// into sedb.json):
//   * action entries: when the actor plays an animation (an "action"), each PlayInfo of that
//     action plays as the clip frame passes its StartFrame (previous < start <= current; for
//     looping clips the check also works across the wrap). An entry without a StartFrame plays
//     when the action starts;
//   * a PlayInfo lists resources (sound script names) with an attenuation name, which selects a
//     distance-attenuation preset (preset map 0x2390a0);
//   * floor-material variants (SePropertyList MaterialInfoList) swap a script for another one
//     depending on what the owner stands on, so steps sound different on grass and stone;
//   * the player's footsteps come from code instead (0x15db84 -> 0x3e0d84): for Move-type
//     actions at level 1 (animation rate <= 2.6) the Move clip frames 0 / 15 / 30 / 45 play
//     StepR / SubStepR / StepL / SubStepL; at level 2 (faster) frames 0 / 10 / 30 / 40 play
//     RunR / SubRunR / RunL / SubRunL.
// Each play creates a sound object (OpeningSound.hpp) that follows the owner's position.
// =============================================================================================
#pragma once
#include "audio/OpeningSound.hpp"
#include "data/Package.hpp"
#include <filesystem>
#include <functional>
#include <map>
#include <set>

namespace captain_toad::OpeningSoundKeeper {

// A PlayInfo reference inside an action, with its trigger frames.
struct Entry {
    std::string play;
    float start = -1;   // -1: play when the action starts
    float end = -1;
    bool oneTime = false;
};

// One sound resource of a PlayInfo.
struct Resource {
    std::string name;   // sound script name
    int preset = 10;    // attenuation preset
};

// One owner's database: actions -> entries, PlayInfo name -> resources.
struct Db {
    std::map<std::string, std::vector<Entry>> actions;
    std::map<std::string, std::vector<Resource>> plays;
    std::set<std::string> levels;   // PlayInfos with IsLevel: held while the action that started them lasts
    std::string parent;             // ParentName: entries it inherits
    std::map<std::string, std::vector<std::string>> reactions;   // HitReactionInfoList: name -> PlayInfos
};

// Attenuation name -> SND3DLIST preset (0x2390a0; absent: "Default", 10). The names are data
// literals from the game's tables.
inline int presetOf(const std::string& n) {
    static const std::map<std::string, int> presets = {{"KyoroHei", 16},
                                                       {"Heyho", 14},
                                                       {"FlyDropper", 12},
                                                       {"Teren", 23},
                                                       {"BossBird", 2},
                                                       {"BossBirdEnding", 3},
                                                       {"Hud", 15},
                                                       {"Player", 19},
                                                       {"BossMagma", 4},
                                                       {"BossMagmaDurian", 5},
                                                       {"Radish", 24},
                                                       {"RadishBullet", 25},
                                                       {"KinokoOneUp", 18},
                                                       {"Nukkun", 17},
                                                       {"DotKinopio", 11},
                                                       {"CityWorldLiftA", 6},
                                                       {"CityWorldLiftB", 7},
                                                       {"CityWorldUndergroundMachine", 8},
                                                       {"ClockDiscClockParts", 9},
                                                       {"Gem", 13},
                                                       {"SandWorldLift", 21},
                                                       {"SandWorldPyramid", 22},
                                                       {"Block", 1},
                                                       {"PoisonLake", 20},
                                                       {"Bazooka", 0}};
    auto it = presets.find(n);
    return it == presets.end() ? 10 : it->second;
}

// A sound-emitting actor as the keeper sees it.
struct Owner {
    std::string name;
    std::string db;                                         // database entry name
    std::function<captain_toad::audio::Vec3f()> position;   // empty: a 2D sound
    std::function<std::string()> material;                  // floor MaterialCode ("" = default resource)
    int forced = -1;       // forced script entry (voices: the protagonist 0, the companion 1)
    std::string action;    // current action
    float prevFrame = 0;   // clip frame at the previous update
    bool fresh = true;     // the action has just started
    std::vector<std::pair<std::string, int>> levelHandles;   // level sounds of the current action (PlayInfo, handle)
};

class Keeper {
public:
    captain_toad::audio::SoundSystem& sys;
    std::map<std::string, Db> dbs;
    std::map<std::string, std::map<std::string, std::string>> materials;   // script -> material -> script
    std::vector<Owner> owners;
    std::string atmosphere;   // the stage's ambient sound
    bool debug = getenv("DIORAMA_SOUND_DEBUG") != nullptr;
    long stamp = 0;   // caller's frame number for debug output
    // Each requested resource draws the shared game RNG once (0x3340f4: request initialiser,
    // random start delay in [0, DelayRandom]; the draw happens even for a zero range).
    std::function<void()> onRequest;

    explicit Keeper(captain_toad::audio::SoundSystem& s) : sys(s) {}

    // Read sedb.json from the package's sound directory.
    void load(const std::filesystem::path& dir) {
        if (!std::filesystem::exists(dir / "sedb.json")) {
            throw std::runtime_error("sound/sedb.json missing");
        }
        const captain_toad::data::Value document = captain_toad::data::readJson(dir / "sedb.json");
        captain_toad::data::JsonView j = document;

        captain_toad::data::JsonView ownerTable = j["owners"];
        for (const std::string& name : ownerTable.keys()) {
            Db db;
            captain_toad::data::JsonView o = ownerTable[name];

            captain_toad::data::JsonView actionList = o["ActionInfoList"];
            if (actionList.isArray()) {
                for (captain_toad::data::JsonView a : (actionList).items()) {
                    std::vector<Entry> entries;
                    captain_toad::data::JsonView list = a["PlayInfoInActionList"];
                    if (list.isArray()) {
                        for (captain_toad::data::JsonView e : (list).items()) {
                            Entry x;
                            x.play = e["Name"].str();
                            if (e["StartFrame"]) {
                                x.start = e["StartFrame"].f();
                            }
                            if (e["EndFrame"]) {
                                x.end = e["EndFrame"].f();
                            }
                            x.oneTime = e["IsOneTime"].b();
                            entries.push_back(x);
                        }
                    }
                    db.actions[a["Name"].str()] = entries;
                }
            }

            for (captain_toad::data::JsonView r : o["HitReactionInfoList"].items()) {
                db.reactions[r["Name"].str()].push_back(r["PlayName"].str());
            }
            captain_toad::data::JsonView playList = o["PlayInfoList"];
            if (playList.isArray()) {
                for (captain_toad::data::JsonView p : (playList).items()) {
                    std::vector<Resource> resources;
                    captain_toad::data::JsonView list = p["ResourceInfoList"];
                    if (list.isArray()) {
                        for (captain_toad::data::JsonView r : (list).items()) {
                            Resource x;
                            x.name = r["Name"].str();
                            if (r["Attenuation"]) {
                                x.preset = presetOf(r["Attenuation"].str());
                            }
                            resources.push_back(x);
                        }
                    }
                    db.plays[p["Name"].str()] = resources;
                    if (p["IsLevel"].b()) {
                        db.levels.insert(p["Name"].str());
                    }
                }
            }
            if (o["ParentName"].isString()) {
                db.parent = o["ParentName"].str();
            }
            dbs[name] = db;
        }
        // ParentName: an entry inherits the actions and PlayInfos it does not define itself.
        for (auto& [name, db] : dbs) {
            std::set<std::string> seen{name};
            for (std::string p = db.parent; !p.empty() && dbs.count(p) && !seen.count(p); p = dbs.at(p).parent) {
                seen.insert(p);
                const Db& base = dbs.at(p);
                for (const auto& [k, v] : base.actions) {
                    db.actions.emplace(k, v);
                }
                for (const auto& [k, v] : base.plays) {
                    db.plays.emplace(k, v);
                }
                db.levels.insert(base.levels.begin(), base.levels.end());
            }
        }

        captain_toad::data::JsonView props = j["properties"];
        for (const std::string& n : props.keys()) {
            captain_toad::data::JsonView list = props[n]["MaterialInfoList"];
            if (list.isArray()) {
                for (captain_toad::data::JsonView m : (list).items()) {
                    materials[n][m["Name"].str()] = m["ResourceName"].str();
                }
            }
        }
        atmosphere = j["stage"]["AtmosphereSeName"].str();
    }

    Owner& owner(const std::string& n) {
        for (auto& o : owners) {
            if (o.name == n) {
                return o;
            }
        }
        throw std::runtime_error("no sound owner " + n);
    }

    // Play a PlayInfo of the owner (looked up in its database; players fall back to the shared
    // player tables). `choice` >= 0 picks the entry of a choice script (a PlayInfo fed by a local
    // variable); otherwise the owner's forced choice applies.
    void play(Owner& o, const std::string& playName, int choice = -1) {
        const std::vector<Resource>* resources = nullptr;
        for (const std::string& db : {o.db, std::string("PlayerBase"), std::string("PlayerActor")}) {
            auto d = dbs.find(db);
            if (d == dbs.end()) {
                continue;
            }
            auto p = d->second.plays.find(playName);
            if (p != d->second.plays.end()) {
                resources = &p->second;
                break;
            }
            if (o.db != "PlayerBase") {
                break;   // only the player falls back to the shared tables
            }
        }
        if (!resources) {
            if (debug) {
                std::fprintf(stderr, "sound: %s has no PlayInfo %s\n", o.name.c_str(), playName.c_str());
            }
            return;
        }
        for (const auto& r : *resources) {
            // Floor-material variant of the script, if any.
            std::string script = r.name;
            if (o.material) {
                auto m = materials.find(script);
                std::string material = o.material();
                if (m != materials.end() && !material.empty()) {
                    auto v = m->second.find(material);
                    if (v != m->second.end()) {
                        script = v->second;
                    }
                }
            }
            std::function<captain_toad::audio::Vec3f()> where = o.position;
            if (onRequest) {
                onRequest();
            }
            // A level sound's handle arrives when it starts (with the command latency the request
            // waits; the object list's last entry is then another sound, or none).
            std::function<void(int)> started;
            if (auto d = dbs.find(o.db); d != dbs.end() && d->second.levels.count(playName)) {
                const size_t index = size_t(&o - owners.data());
                started = [this, index, playName](int handle) {
                    owners[index].levelHandles.push_back({playName, handle});
                };
            }
            bool ok = sys.play(script, where, r.preset, choice >= 0 ? choice : o.forced, std::move(started));
            if (debug) {
                std::fprintf(stderr, "sound: %ld %s %s -> %s%s vol %d\n", stamp, o.name.c_str(), playName.c_str(),
                             script.c_str(), ok ? "" : " (unknown)",
                             ok && !sys.objects.empty() ? int(sys.objects.back().objectVolume) : -1);
            }
        }
    }

    // One keeper update with the owner's current action and clip frame (and the clip length for
    // looping clips, 0 = one-shot).
    // window > 0: the frame advances in steps of `window` with no special first update (the SE
    // keeper 0x28d870 for actors with named nerves: fire when frame - window < StartFrame <= frame).
    void update(Owner& o, const std::string& action, float frame, float length, float window = 0.0f) {
        auto d = dbs.find(o.db);
        if (action != o.action) {
            o.action = action;
            o.fresh = true;
            for (const auto& [play, h] : o.levelHandles) {   // level sounds end with their action
                (void)play;
                sys.stopHandle(h);
            }
            o.levelHandles.clear();
        }
        const std::vector<Entry>* entries = nullptr;
        if (d != dbs.end()) {
            auto a = d->second.actions.find(action);
            if (a != d->second.actions.end()) {
                entries = &a->second;
            }
        }
        if (window > 0.0f) {
            o.fresh = false;
            o.prevFrame = frame - window;
        }
        // A clip already running when the keeper first sees it (the checkpoint) continues from
        // the previous tick's frame (2 frames per tick).
        if (o.fresh && frame > 2.0f) {
            o.fresh = false;
            o.prevFrame = frame - 2.0f;
        }
        if (entries) {
            for (const auto& e : *entries) {
                bool fire;
                if (window > 0.0f) {
                    fire = o.prevFrame < e.start && e.start <= frame;
                } else if (e.start < 0) {
                    fire = o.fresh;
                } else if (o.fresh) {
                    fire = e.start <= frame;
                } else if (frame >= o.prevFrame) {
                    fire = o.prevFrame < e.start && e.start <= frame;
                } else {
                    // The clip wrapped since the previous update.
                    fire = (o.prevFrame < e.start && e.start <= length) || e.start <= frame;
                }
                // A restarted action does not start a level sound that is still playing.
                for (const auto& [play, h] : o.levelHandles) {
                    if (fire && play == e.play && sys.handleAlive(h)) {
                        fire = false;
                    }
                }
                if (fire) {
                    play(o, e.play);
                }
            }
        }
        o.prevFrame = frame;
        o.fresh = false;
    }

    // Player footsteps (0x15db84): Move-type actions, step marks crossed in the clip, by speed
    // level. Returns false when the action is not Move-type (the caller then stores -1 as the
    // previous frame).
    bool playerSteps(Owner& o, const std::string& action, float prev, float frame, float rate) {
        static const char* kMove[] = {"Move", "MoveWater", "Invincible", "MovePanic"};
        bool move = false;
        for (const char* m : kMove) {
            move |= action == m;
        }
        if (!move) {
            return false;
        }
        if (frame == prev) {
            return true;
        }
        int level = action == "Invincible" ? 4 : rate <= 2.6f ? 1 : 2;
        struct Mark {
            int frame;
            const char* name;
        };
        static const Mark walkMarks[] = {{0, "StepR"}, {15, "SubStepR"}, {30, "StepL"}, {45, "SubStepL"}};
        static const Mark runMarks[] = {{0, "RunR"}, {10, "SubRunR"}, {30, "RunL"}, {40, "SubRunL"}};
        static const Mark invincibleMarks[] = {{7, "RunR"}, {22, "RunL"}, {37, "RunR"}, {53, "RunL"}};
        const Mark* marks = level == 1 ? walkMarks : level == 2 ? runMarks : invincibleMarks;
        for (int i = 0; i < 4; ++i) {
            float f = float(marks[i].frame);
            // 0x2d6a64: crossed between prev and frame, across the wrap when frame < prev.
            bool crossed = frame > prev ? (prev < f && f <= frame) : (prev < f || f <= frame);
            if (crossed) {
                play(o, marks[i].name);
            }
        }
        return true;
    }
};

}   // namespace captain_toad::OpeningSoundKeeper
