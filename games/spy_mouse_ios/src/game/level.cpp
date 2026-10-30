#include "game/level.hpp"

#include "game/gcc_sort.hpp"

#include "data/brd.hpp"

#include <cstdio>

namespace doc::game {

SheetSize pngSize(const data::GameFiles& files, const std::string& relative) {
    auto bytes = files.readResource(relative);
    if (!bytes || bytes->size() < 24) return {};
    const auto& b = *bytes;
    auto be32 = [&b](std::size_t at) { return int(b[at]) << 24 | int(b[at + 1]) << 16 | int(b[at + 2]) << 8 | int(b[at + 3]); };
    return {be32(16), be32(20)};
}

namespace {

// Width of a JPEG image from its start-of-frame marker (0 if not found).
int jpegWidth(const data::GameFiles& files, const std::string& relative) {
    auto bytes = files.readResource(relative);
    if (!bytes) return 0;
    const auto& b = *bytes;
    std::size_t at = 2;
    while (at + 9 < b.size()) {
        if (b[at] != 0xFF) return 0;
        const int marker = b[at + 1];
        const std::size_t length = std::size_t(b[at + 2]) << 8 | b[at + 3];
        if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC)
            return int(b[at + 7]) << 8 | b[at + 8];
        at += 2 + length;
    }
    return 0;
}

}  // namespace

std::vector<ParticleKind> loadParticleKinds(const data::GameFiles& files) {
    std::vector<ParticleKind> kinds;
    for (const data::ParticleKindData& k : data::readParticleKinds(files)) {
        ParticleKind kind;
        kind.priority = k.priority;
        kind.sheet = k.sheet;
        kind.frameWidth = k.frameWidth;
        kind.layer = k.layer;
        if (!k.sheet.empty()) kind.sheetSize = pngSize(files, k.sheet);
        kinds.push_back(std::move(kind));
    }
    return kinds;
}

std::optional<Level> Level::load(const data::GameFiles& files, const data::LevelInfo& info, std::string& error) {
    Level level;
    level.info_ = info;
    level.info_.node = nullptr;  // the table may not outlive the level
    // Map::initialise @0x13e614: "mapNN/mapNN.mbn" for floor 0, "mapNN/mapNN_F.mbn" above it.
    for (int f = 0; f < 5; ++f) {
        char name[64];
        if (f == 0) std::snprintf(name, sizeof name, "map%02d/map%02d.mbn", info.map, info.map);
        else std::snprintf(name, sizeof name, "map%02d/map%02d_%d.mbn", info.map, info.map, f);
        if (f > 0 && !files.readResource(name)) break;
        auto map = data::loadMapFile(files, std::string(name), error);
        if (!map) return std::nullopt;
        LevelFloor floor;
        floor.map = std::move(*map);
        floor.geometry.emplace(floor.map);
        floor.ambience = AmbienceScheme::load(files, info.map, f);
        for (int c = 0;; ++c) {
            char chunk[64];
            std::snprintf(chunk, sizeof chunk, "map%02d/chunks/map%02d-%02d.jpg", info.map, info.map, c);
            if (!files.readResource(chunk)) break;
            if (c == 0) floor.chunkWidth = float(jpegWidth(files, chunk));
            floor.chunks.push_back(chunk);
        }
        if (!floor.chunks.empty()) floor.width = floor.chunkWidth * float(floor.chunks.size());
        for (const data::MapVertex& v : floor.map.vertices)
            if (v.isEntity()) level.entities_.push_back({{v.x, v.y}, v.type, v.d, v.e, v.name, f});
        level.floors_.push_back(std::move(floor));
    }
    level.spawnSheets_[10] = pngSize(files, spriteFor(10).path);
    level.spawnSheets_[kKey] = pngSize(files, spriteFor(kKey).path);
    level.spawnSheets_[kBomb] = pngSize(files, spriteFor(kBomb).path);  // (dropped in world 3's fight)
    level.spawnSheets_[kTrapPrize] = pngSize(files, spriteFor(kTrapPrize).path);  // (a trap's, in the boss modes)  // (thrown by the boss droid)
    // An infinite level makes its objects in the order of its entities sorted by x (the
    // original's std::sort over the whole MapEntityContainer, the start point and decorations
    // included: they change where equal x end up, run lb4_watch).
    if (level.infinite())
        gccSort(level.entities_, [](const Entity& a, const Entity& b) { return a.position.x < b.position.x; });
    // Every entity except the mouse start and map decorations becomes an object
    // (SceneGame::addEntity @0x1026a8).
    for (const Entity& e : level.entities_) {
        if (e.type == kEntityMouseStart) continue;
        if (e.type >= kEntityFluffFirst && e.type <= kEntityFluffLast) continue;
        const int type = placedType(e.type, e.d);
        const SpriteSheet sheet = spriteFor(type, e.d, info.world, info.map, e.floor);
        const SheetSize sheetSize = sheet.path.empty() ? SheetSize{} : pngSize(files, sheet.path);
        level.objects_.push_back(makeObject(int(level.objects_.size()), type, e.position, e.d, sheetSize, info.world,
                                            info.map, e.floor));
        LevelObject& o = level.objects_.back();
        o.floor = e.floor;
        o.text = e.name;
        if (o.frameWidth > 0) o.frames = int(float(sheetSize.width) / float(o.frameWidth) + 0.5f);
        o.anim.setSheet(sheetSize.width, sheetSize.height, float(o.frameWidth));
        // Object::loadAssets @0x117cbe: a reflector turning right shows its second frame.
        if (type == kReflector && o.state == ObjectState(0x3e)) o.anim.setFrame(1);
        if (isCheeseMachineEntity(e.type)) {
            // Object::init @0x11aa30: the cheeses it holds are made at once (SceneGame::addEntity,
            // so they follow it in the object list): type 10, 6 right of and 7.75 below its
            // vertex, held (0x31) and keeping its tag.
            const int count = cheeseMachineLoad(e.type), floor = e.floor, owner = e.d;
            const Vec2 at{e.position.x + 6.0f, e.position.y + -7.75f};
            const SpriteSheet cheeseSheet = spriteFor(10);
            const SheetSize cheeseSize = pngSize(files, cheeseSheet.path);
            for (int i = 0; i < count; ++i) {
                LevelObject c = makeObject(int(level.objects_.size()), 10, at, 0, cheeseSize, info.world);
                c.floor = floor;
                c.state = ObjectState(kHeldInMachine);
                c.heldBy = owner;
                if (c.frameWidth > 0) c.frames = int(float(cheeseSize.width) / float(c.frameWidth) + 0.5f);
                c.anim.setSheet(cheeseSize.width, cheeseSize.height, float(c.frameWidth));
                level.objects_.push_back(std::move(c));
            }
            continue;
        }
        if (e.type == 601) {
            // Object::postInit @0x116e98: "rand(base,spread)" or a fixed delay in the entity text.
            int a = 0, b = 0;
            if (e.name.find("rand") != std::string::npos) {
                if (std::sscanf(e.name.c_str(), "rand(%d,%d)", &a, &b) == 2) o.triggerDelayMs = a, o.triggerSpreadMs = b;
            } else {
                std::sscanf(e.name.c_str(), "%d", &o.triggerDelayMs);
            }
        }
    }

    // Map decorations with animations (Map::load @0x13c9c8).
    const std::vector<data::FluffKindData> fluffKinds = data::readFluffKinds(files);
    for (const Entity& e : level.entities_) {
        if (e.type < kEntityFluffFirst || e.type > kEntityFluffLast) continue;
        const std::size_t index = std::size_t(e.type - kEntityFluffFirst);
        if (index >= fluffKinds.size()) continue;
        FluffItem f;
        f.type = e.type;
        f.floor = e.floor;
        f.anchor = e.position;
        f.kind = fluffKinds[index];
        f.sheet = pngSize(files, f.kind.sheet);
        const float levelScale = 1.0f;  // LevelState +0x64
        f.position = {e.position.x, e.position.y + levelScale * ((float(f.sheet.height) + 1.10000002f) * 0.5f)};
        level.fluff_.push_back(std::move(f));
    }

    // Particle types (sprite sheets and priorities from the program image).
    level.particleKinds_ = loadParticleKinds(files);
    for (int slot = 0; slot < 2; ++slot) {
        for (const Entity& e : level.entities_)
            if (e.type == 1100 + slot) {
                char path[64];
                if (slot == 0) std::snprintf(path, sizeof path, "map%d/intro_text.png", info.map);
                else std::snprintf(path, sizeof path, "map%d/intro_text_%d.png", info.map, slot);
                if (files.readResource(path)) level.tutorial_.push_back({slot, true, e.position, path, {}, 0.0f});
                break;
            }
        for (const Entity& e : level.entities_)
            if (e.type == 1110 + slot) {
                level.tutorial_.push_back({slot, false, e.position, {}, e.name, float(e.d)});
                break;
            }
    }
    // Tutorial popups (ScenePopupTutorial::load @0x225208): "carry more" per strength, then
    // the first-visit ones of particular maps (GameBoot::bootLevel @0x1f5142).
    auto loadNotice = [&files](const std::string& name) -> std::optional<LevelNotice> {
        auto root = data::loadChunkTree(files, name + ".xml.brd");
        if (!root) return std::nullopt;
        LevelNotice n;
        n.layout = name;
        if (const data::BrdNode* m = root->child("mouse")) {
            n.pose = m->getInt("pose").value_or(1);
            n.flip = m->getBool("flip").value_or(false);
        }
        if (const data::BrdNode* t = root->child("title_text")) n.titleText = t->getString("id").value_or("");
        if (const data::BrdNode* t = root->child("body_text")) n.bodyText = t->getString("id").value_or("");
        if (const data::BrdNode* t = root->child("img")) n.image = t->getString("file").value_or("");
        if (const data::BrdNode* t = root->child("body_text_extra")) n.bodyExtraText = t->getString("id").value_or("");
        n.wifiHint = root->child("wifi_hint") != nullptr;
        return n;
    };
    for (int slot = 1;; ++slot) {
        char name[64];
        std::snprintf(name, sizeof name, "popup_tutorials/new_cheese_slot%02d", slot);
        auto n = loadNotice(name);
        if (!n) break;
        level.carryNotices_.push_back(std::move(*n));
    }
    // SceneGame::initGame @0x10639e: map 704 (1-3) shows the level goals, map 26 (2-4) the safe
    // holes, until the level is won.
    if (info.map == 704) level.firstVisitNotice_ = loadNotice("popup_tutorials/level_goals");
    if (info.map == 26) level.firstVisitNotice_ = loadNotice("popup_tutorials/safe_holes");
    for (const char* file : Mouse::kSequenceFiles)
        for (data::AnimSequence& seq : data::loadAnimSequences(files, file)) level.mouseSequences_.push_back(std::move(seq));
    level.goalNames_ = data::readLevelGoalNames(files);
    level.scoreNotes_ = data::readScoreNotes(files);
    const std::vector<std::uint8_t> eyes = data::readEyeMapping(files);
    for (std::size_t i = 0; i < eyes.size() && i < level.eyeMapping_.size(); ++i) level.eyeMapping_[i] = eyes[i];
    {
        std::string textError;
        if (auto table = data::StringTable::load(files, "EN", textError))
            level.strings_ = std::make_shared<const data::StringTable>(std::move(*table));
    }
    level.hudLeftHeight_ = pngSize(files, "hud/hud_elem_left.png").height;
    level.handRoute_ = HandTutorial::route(level.map());
    level.goalPopupHeight_ = pngSize(files, "ui/rosetta_popup_mid.png").height;
    level.catWalkSheet_ = pngSize(files, "sprites/browncat01.png");
    level.catIdleSheet_ = pngSize(files, "sprites/browncat_idle01.png");
    level.catCommonSheet_ = pngSize(files, "sprites/browncat_common.png");
    level.blackWalkSheet_ = pngSize(files, "sprites/blackcat01.png");
    level.blindWalkSheet_ = pngSize(files, "sprites/blindcat01.png");
    level.blackIdleSheet_ = pngSize(files, "sprites/blackcat_idle.png");
    level.blackCommonSheet_ = pngSize(files, "sprites/blackcat_common.png");
    // The ninja cat (ObjControllerCat::loadAssets @0x1a008e): 32 wide walking, 50 wide sitting.
    level.ninjaWalkSheet_ = pngSize(files, "sprites/ninjacat01.png");
    level.ninjaIdleSheet_ = pngSize(files, "sprites/ninjacat_idle01.png");
    level.ninjaCommonSheet_ = pngSize(files, "sprites/ninjacat_common.png");
    level.blackIdleSequences_ = data::loadAnimSequences(files, "anim_sequences/black_cat_idle.xml.brd");
    level.brownStringSequences_ = data::loadAnimSequences(files, "anim_sequences/brown_cat_string.xml.brd");
    level.bossWalkSheet_ = pngSize(files, "sprites/bosscat01.png");
    level.bossIdleSheet_ = pngSize(files, "sprites/bosscat_sit.png");
    level.bossCommonSheet_ = pngSize(files, "sprites/bosscat_common.png");
    level.droidBodySheet_ = pngSize(files, "sprites/boss1/boss1_part1.png");
    level.droidBaseSheet_ = pngSize(files, "sprites/boss1/boss1_part0.png");
    level.droidWreckSheet_ = pngSize(files, "sprites/boss1/boss1_destroyed.png");
    level.switchTimerSheet_ = pngSize(files, "sprites/timer.png");
    // The ninja cat's shuriken (ProjectileManager::load @0x18ca5c, picture 0).
    level.bossSheets_["sprites/shuriken.png"] = pngSize(files, "sprites/shuriken.png");
    if (info.mode == "boss") {
        // World 2's Spitter (ObjControllerBoss::loadAssets) and its cutlery
        // (ProjectileManager::load @0x18ca5c).
        for (const char* file : {"sprites/boss2/boss2.png", "sprites/boss2/boss2_destroyed.png", "sprites/boss2/boss2_fork.png",
                                 "sprites/boss2/boss2_spoon.png", "sprites/boss2/boss2_knife.png"})
            level.bossSheets_[file] = pngSize(files, file);
        // World 3's vacuum (ObjControllerBoss::loadAssets @0x1a91d0).
        for (const char* file : {"sprites/boss3/boss3_part0.png", "sprites/boss3/boss3_part1.png",
                                 "sprites/boss3/boss3_part2.png", "sprites/vacuum_effect.png",
                                 "sprites/boss3/boss3_cry.png", "sprites/boss3/boss3_splash.png"})
            level.bossSheets_[file] = pngSize(files, file);
        // World 4's henchman in his balloon (ObjControllerCat::loadAssets) and the launcher's
        // shuriken (ProjectileManager::load).
        for (const char* file : {"sprites/boss4/boss4.png", "sprites/boss4_shuriken.png", "sprites/boss4/boss4_cry.png",
                                 "sprites/boss4/boss4_splash.png"})
            level.bossSheets_[file] = pngSize(files, file);
        if (auto events = data::loadMapEvents(files, info.map))
            level.events_ = std::make_shared<const data::MapEvents>(std::move(*events));
        level.dialogues_ = std::make_shared<const std::map<std::string, std::vector<data::Speech>>>(data::loadDialogues(files));
    }
    level.catEyesSheet_ = pngSize(files, "sprites/cat_eyes.png");
    level.rippleSheet_ = pngSize(files, "sprites/node_ripple.png");
    level.hudBonusHeight_ = pngSize(files, "hud/hud_score_bonus.png").height;
    level.hudBonusWidth_ = pngSize(files, "hud/hud_score_bonus.png").width;
    // Cheese: the sparkle burst after a pickup (ObjControllerPickups::loadAssets @0x1b6f6c).
    const SheetSize sparkle = pngSize(files, "sprites/cheesetrail_effect.png");
    for (LevelObject& o : level.objects_)
        if (isCheese(o.type)) o.sparkle.setSheet(sparkle.width, sparkle.height, 60.0f);
    return level;
}

int Level::goalKind(const std::string& name) const {
    for (std::size_t i = 0; i < goalNames_.size(); ++i)
        if (goalNames_[i] == name) return int(i);
    return -1;
}

const LevelNotice* Level::carryNotice(int strength) const {
    const int slot = strength - 1;
    return slot >= 1 && slot <= int(carryNotices_.size()) ? &carryNotices_[std::size_t(slot - 1)] : nullptr;
}

std::optional<Vec2> Level::mouseStart() const {
    for (const Entity& e : entities_)
        if (e.type == kEntityMouseStart && e.floor == 0) return e.position;
    return std::nullopt;
}

}  // namespace doc::game

namespace doc::game {

LevelObject Level::spawnObject(int id, int type, Vec2 vertex, int tag, int floor) const {
    auto it = spawnSheets_.find(type);
    const SheetSize sheet = it == spawnSheets_.end() ? SheetSize{} : it->second;
    LevelObject o = makeObject(id, type, vertex, tag, sheet);
    o.floor = floor;
    if (o.frameWidth > 0) o.frames = int(float(sheet.width) / float(o.frameWidth) + 0.5f);
    o.anim.setSheet(sheet.width, sheet.height, float(o.frameWidth));
    return o;
}

}  // namespace doc::game
