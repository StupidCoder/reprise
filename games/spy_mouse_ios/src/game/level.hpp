// A loaded level: its table entry, walls and the entities placed on its map.
#pragma once

#include "data/level_table.hpp"
#include "data/map_file.hpp"
#include "game/level_geometry.hpp"
#include "game/objects.hpp"
#include "game/ambience.hpp"
#include "game/particles.hpp"
#include "game/notice.hpp"
#include "game/hand_tutorial.hpp"
#include "game/mouse.hpp"
#include "data/anim_sequences.hpp"
#include "data/cutscenes.hpp"
#include "data/strings.hpp"

#include <memory>
#include "data/program_tables.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace doc::game {

// Entity types seen on the first levels' maps (vertex type with flag 2). Behaviour of most is
// not recovered yet; see .analysis/docs/level-1-1.md.
enum EntityType : int {
    kEntityMouseStart = 1,
    kEntityCheeseFirst = 6,  // 6..10: cheese, heaviest first
    kEntityCheeseLast = 10,
    kEntityFluffFirst = 500,  // 500..574: map decoration, owned by the map
    kEntityFluffLast = 574,
};

// The in-game tutorial's pieces (SceneInGameTutorial::loadTexturesAndText @0x29548c): the first
// object of type 1100 + i shows the map folder's picture intro_text.png (i = 0) or
// intro_text_i.png at its place; the first of type 1110 + i shows the string its map text
// names, wrapped to its tag's width.
struct TutorialPiece {
    int slot = 0;           // i (0 or 1)
    bool picture = false;
    Vec2 position;
    std::string image;      // picture: path under res/
    std::string textKey;    // text: string name
    float wrapWidth = 0.0f;
};

// A map decoration with an animation (Map fluff slot).
struct FluffItem {
    int type = 0;            // 500..573
    Vec2 anchor;             // its map vertex
    Vec2 position;           // where it is drawn (and where its effects start)
    int floor = 0;
    data::FluffKindData kind;
    SheetSize sheet;
};

struct Entity {
    Vec2 position;
    int type = 0;
    int d = 0, e = 0;
    std::string name;
    int floor = 0;  // the floor whose map holds it
};

// One floor of a level: its map file (mapNN.mbn, then mapNN_1.mbn, ...), walls and ambience.
struct LevelFloor {
    data::MapFile map;
    // A map too wide for one picture is drawn from strips ("mapNN/chunks/mapNN-NN.jpg", Map::load
    // @0x13c9c8), side by side from x = 0; the map is as wide as they are together.
    std::vector<std::string> chunks;
    float chunkWidth = 0.0f;
    float width = 480.0f;
    std::optional<LevelGeometry> geometry;
    AmbienceScheme ambience;
};

// Size of a PNG image from its header (sprite sheets hold one row of frames).
SheetSize pngSize(const data::GameFiles& files, const std::string& relative);
// Particle types (sprite sheets and priorities from the program image).
std::vector<ParticleKind> loadParticleKinds(const data::GameFiles& files);

class Level {
public:
    static std::optional<Level> load(const data::GameFiles& files, const data::LevelInfo& info, std::string& error);

    const data::LevelInfo& info() const { return info_; }
    // "infinite" levels (the boss chase, SceneGame +0x13828 = 1): the view scrolls along a map
    // wider than the screen.
    bool infinite() const { return info_.mode == "infinite"; }
    // Floors (SceneGame::initGame: up to five maps, all loaded at once; every entity belongs to
    // the floor whose map holds it). Floor 0 is where the mouse starts.
    int floorCount() const { return int(floors_.size()); }
    const LevelFloor& floor(int i) const { return floors_[std::size_t(i)]; }
    const data::MapFile& map() const { return floors_.front().map; }
    const LevelGeometry& geometry() const { return *floors_.front().geometry; }
    const std::vector<Entity>& entities() const { return entities_; }
    std::optional<Vec2> mouseStart() const;
    // The level's objects as placed at the start, in map order (the original's object ids).
    const std::vector<LevelObject>& objects() const { return objects_; }
    // An object made during play (SceneGame::addEntity: a sprung trap's cheese), as a placed
    // one would be.
    LevelObject spawnObject(int id, int type, Vec2 vertex, int tag, int floor) const;
    const std::vector<FluffItem>& fluff() const { return fluff_; }
    const std::vector<ParticleKind>& particleKinds() const { return particleKinds_; }
    const AmbienceScheme& ambience() const { return floors_.front().ambience; }
    // The popup announcing that the mouse now carries `strength` cheeses (2..5), if any.
    const LevelNotice* carryNotice(int strength) const;
    const std::vector<TutorialPiece>& tutorial() const { return tutorial_; }
    // The popup shown when the level starts before it has ever been won, if any.
    const LevelNotice* firstVisitNotice() const { return firstVisitNotice_ ? &*firstVisitNotice_ : nullptr; }
    const PosedMouse::EyeMapping& eyeMapping() const { return eyeMapping_; }
    // Number of a level goal kind by its name (g_LevelGoalNames), -1 if unknown.
    int goalKind(const std::string& name) const;
    const std::vector<int>& scoreNotes() const { return scoreNotes_; }
    // The mouse's animation sequences (Mouse::kSequenceFiles, in order).
    const std::vector<data::AnimSequence>& mouseSequences() const { return mouseSequences_; }
    // The game's text (English), for texts the level shows (null if missing).
    const data::StringTable* strings() const { return strings_.get(); }
    // A boss level's scripted scenes (map_events) and the game's dialogues; null elsewhere.
    const data::MapEvents* events() const { return events_.get(); }
    const std::vector<data::Speech>* dialogue(const std::string& name) const {
        if (!dialogues_) return nullptr;
        const auto it = dialogues_->find(name);
        return it == dialogues_->end() ? nullptr : &it->second;
    }
    // The demonstrating finger's route (map type-142 lines) and its ripple's sheet.
    const std::vector<HandTutorial::Sample>& handRoute() const { return handRoute_; }
    SheetSize rippleSheet() const { return rippleSheet_; }
    int hudLeftHeight() const { return hudLeftHeight_; }
    int goalPopupHeight() const { return goalPopupHeight_; }
    // A cat's walking and idle sheets (ObjControllerCat::loadAssets: the brown cat's
    // browncat01.png / browncat_idle01.png, the boss cat's bosscat01.png / bosscat_sit.png) and
    // its common one (<cat>_common.png).
    // (Fluffy, types 0x3a and 0x3b, wears the boss cat's pictures: ObjControllerCat::loadAssets
    // @0x1a01a6.)
    SheetSize catSheet(bool idle, int type) const {
        if (wearsBossCat(type)) return idle ? bossIdleSheet_ : bossWalkSheet_;
        if (type == kBlackCat) return idle ? blackIdleSheet_ : blackWalkSheet_;
        if (type == kNinjaCat) return idle ? ninjaIdleSheet_ : ninjaWalkSheet_;
        if (type == kBlindCat) return idle ? SheetSize{} : blindWalkSheet_;  // (no idle pictures)
        return idle ? catIdleSheet_ : catWalkSheet_;
    }
    SheetSize catCommonSheet(int type) const {
        if (wearsBossCat(type)) return bossCommonSheet_;
        if (type == kBlindCat) return SheetSize{};  // (no common picture)
        if (type == kNinjaCat) return ninjaCommonSheet_;
        return type == kBlackCat ? blackCommonSheet_ : catCommonSheet_;
    }
    // A cat kind's sitting sequences (ObjControllerCat::loadAssets @0x19fc7c: the black cat's
    // anim_sequences/black_cat_idle, the brown cat's brown_cat_string), else none.
    const std::vector<data::AnimSequence>& catIdleSequences(int type) const {
        static const std::vector<data::AnimSequence> none;
        return type == kBlackCat ? blackIdleSequences_ : type == 0x32 ? brownStringSequences_ : none;
    }
    SheetSize droidBodySheet() const { return droidBodySheet_; }  // boss1/boss1_part1.png
    SheetSize droidBaseSheet() const { return droidBaseSheet_; }  // boss1/boss1_part0.png
    SheetSize droidWreckSheet() const { return droidWreckSheet_; }  // boss1/boss1_destroyed.png
    SheetSize switchTimerSheet() const { return switchTimerSheet_; }  // timer.png (timed buttons)
    // Pictures of later bosses and their projectiles by file (boss levels only; empty if not
    // loaded).
    SheetSize bossSheet(const std::string& file) const {
        const auto it = bossSheets_.find(file);
        return it == bossSheets_.end() ? SheetSize{} : it->second;
    }
    SheetSize catEyesSheet() const { return catEyesSheet_; }      // cat_eyes.png  // ui/rosetta_popup_mid.png    // hud/hud_elem_left.png
    int hudBonusHeight() const { return hudBonusHeight_; }  // hud/hud_score_bonus.png
    int hudBonusWidth() const { return hudBonusWidth_; }

private:
    data::LevelInfo info_;
    std::vector<LevelFloor> floors_;
    std::vector<Entity> entities_;
    std::vector<LevelObject> objects_;
    std::map<int, SheetSize> spawnSheets_;  // sheet sizes of the types made during play
    std::vector<FluffItem> fluff_;
    std::vector<ParticleKind> particleKinds_;
    std::vector<LevelNotice> carryNotices_;
    std::optional<LevelNotice> firstVisitNotice_;
    std::vector<TutorialPiece> tutorial_;
    std::vector<std::string> goalNames_;
    std::vector<int> scoreNotes_;  // strength 2.. (new_cheese_slot01..)
    PosedMouse::EyeMapping eyeMapping_{};
    std::vector<data::AnimSequence> mouseSequences_;
    std::shared_ptr<const data::StringTable> strings_;
    std::shared_ptr<const data::MapEvents> events_;
    std::shared_ptr<const std::map<std::string, std::vector<data::Speech>>> dialogues_;
    std::vector<HandTutorial::Sample> handRoute_;
    SheetSize rippleSheet_;
    int goalPopupHeight_ = 0;
    SheetSize catWalkSheet_, catIdleSheet_, catCommonSheet_, catEyesSheet_;
    SheetSize blackWalkSheet_, blackIdleSheet_, blackCommonSheet_, blindWalkSheet_;
    SheetSize ninjaWalkSheet_, ninjaIdleSheet_, ninjaCommonSheet_;
    std::vector<data::AnimSequence> blackIdleSequences_;
    std::vector<data::AnimSequence> brownStringSequences_;
    SheetSize bossWalkSheet_, bossIdleSheet_, bossCommonSheet_, droidBodySheet_, droidBaseSheet_, droidWreckSheet_;
    std::map<std::string, SheetSize> bossSheets_;
    SheetSize switchTimerSheet_;
    int hudLeftHeight_ = 0, hudBonusHeight_ = 0, hudBonusWidth_ = 0;
};

}  // namespace doc::game
