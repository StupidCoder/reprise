#include "game/world_map.hpp"

#include "data/anim_sequences.hpp"
#include "game/level.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace doc::game {
namespace {

constexpr float kScreenW = 480.0f, kScreenH = 320.0f;
constexpr float kPadReach = 32.0f;     // a touch counts as on a pad within this (mouseEvent)
constexpr float kPadRadius = 16.0f;    // hypothesis: a pad's touch radius (Object +0x1c0) for snapping paths
constexpr float kCloudScale = 2.0f;    // clouds are drawn at twice their size
constexpr float kCloudTilt = 0.577350259f;  // tan 30 degrees: they drift up and to the right

// Effects (SoundList indices) the map plays.
constexpr int kSoundWalk = sfx::UiWobble;  // the mouse sets off
constexpr int kSoundPlay = 0x8c;       // a world pad's bubble pressed
constexpr int kSoundUnlock = 0x2b;     // a newly opened level is about to be revealed
constexpr int kSoundAppear = 0x29;     // its grass and house appear
constexpr int kSoundPadPop = 0x2a;     // its pad pops up

Vec2 toMap(Vec2 screen) { return {screen.x, kScreenH - screen.y}; }

}  // namespace

WorldMap::WorldMap(std::uint32_t seed, std::vector<ParticleKind> kinds) : particles_(random_, std::move(kinds)) {
    random_.seed(seed);
}

std::unique_ptr<WorldMap> WorldMap::load(const data::GameFiles& files, const data::LevelTable& table, Profile& profile,
                                         int world, std::uint32_t seed, std::string& error) {
    std::unique_ptr<WorldMap> made(new WorldMap(seed, loadParticleKinds(files)));
    WorldMap& m = *made;
    m.table_ = &table;
    m.profile_ = &profile;
    m.world_ = world;
    char name[64];
    std::snprintf(name, sizeof name, "world/world_%02d.mbn", world + 1);
    auto map = data::loadMapFile(files, std::string(name), error);
    if (!map) return nullptr;
    m.map_ = std::move(*map);
    m.walls_.emplace(m.map_);
    m.paths_ = MapPaths::load(files, world, error);
    // (SceneMenuMap::loadMenuMap: the teleport's blurs, 32 wide.)
    const SheetSize blurOut = pngSize(files, "sprites/player_blur1.png"), blurIn = pngSize(files, "sprites/player_blur2.png");
    m.teleport_.out.setSheet(blurOut.width, blurOut.height, 32.0f);
    m.teleport_.in.setSheet(blurIn.width, blurIn.height, 32.0f);
    if (!m.paths_) return nullptr;

    // loadSceneGraph: how many grass patches, roads and pads, and houses each world has.
    static const int kCounts[6][3] = {{13, 13, 12}, {12, 14, 12}, {9, 15, 12}, {11, 12, 10}, {12, 14, 12}, {13, 13, 12}};
    for (int i = 0; i < 3; ++i) m.sceneryCounts_[i] = kCounts[std::clamp(world, 0, 5)][i];
    // (World 4 has 11 grass and 10 houses; world 6 none of its own: a guess until those
    // worlds are ported. Worlds 1, 2, 3 and 5 are read from the code's switch.)

    for (const data::MapVertex& v : m.map_.vertices) {
        if (!v.isEntity() || v.type == 1) continue;  // type 1: the mouse's start (unused here)
        Node n;
        n.type = v.type;
        n.position = {v.x, v.y};
        n.tag = v.d;
        if (n.type == kLevelPad) n.level = table.byUid(n.tag);
        // The way on to the next world opens with the boss beaten (hypothesis: the pad's
        // object stays inactive until then; the previous world's pad is always there).
        if (n.type == kWorldNext) n.active = profile.bossDefeated(table, world);
        m.nodes_.push_back(n);
    }
    // The world's levels: every pad's level and the level it leads into, by uid.
    for (std::size_t i = 0; i < m.nodes_.size(); ++i) {
        const Node& n = m.nodes_[i];
        if (n.type != kLevelPad || !n.level) continue;
        m.levels_.push_back({n.level, int(i)});
        if (n.level->autostart >= 0)
            if (const data::LevelInfo* next = table.byUid(n.level->autostart)) m.levels_.push_back({next, -1});
    }
    std::stable_sort(m.levels_.begin(), m.levels_.end(),
                     [](const WorldLevel& a, const WorldLevel& b) { return a.info->uid < b.info->uid; });

    // The pad the mouse stands at: the level played last (for the boss fight, the chase before
    // it), else this world's first level. It is open, and no longer new.
    int currentUid = profile.currentLevel >= 0 ? profile.currentLevel : 1;
    if (const data::LevelInfo* l = table.byUid(currentUid); l && l->mode == "boss") currentUid = l->by1;
    int pad = -1;
    for (std::size_t i = 0; i < m.nodes_.size(); ++i)
        if (m.nodes_[i].type == kLevelPad && m.nodes_[i].tag == currentUid) pad = int(i);
    if (pad < 0) {
        int best = 1 << 30;
        for (std::size_t i = 0; i < m.nodes_.size(); ++i)
            if (m.nodes_[i].type == kLevelPad && m.nodes_[i].level) {
                const int index = table.indexOf(m.nodes_[i].tag);
                if (index < best) {
                    best = index;
                    pad = int(i);
                }
            }
        if (pad >= 0) profile.at(m.nodes_[std::size_t(pad)].tag).fresh = false;
    }
    if (pad >= 0) {
        profile.currentLevel = m.nodes_[std::size_t(pad)].tag;
        profile.at(profile.currentLevel).unlocked = true;
    }
    m.current_ = pad;
    m.focusedFallback_ = pad;
    m.setFocus(pad);

    // Houses stand at their markers; every open level that is not new shows its pieces.
    int frontier = -1;
    for (std::size_t i = 0; i < m.levels_.size(); ++i) {
        const data::LevelInfo* level = m.levels_[i].info;
        for (Node& marker : m.nodes_)
            if (marker.active && marker.type == kBuilding && marker.tag == level->uid) {
                marker.active = false;
                if (!level->secret) m.scenery_.housePositions.emplace_back(int(i) + 1, marker.position);
                break;
            }
        const LevelRecord r = m.record(level);
        if (r.unlocked && !r.fresh && !level->secret) {
            m.attach(int(i));
            if (!r.completed) frontier = int(i);
        }
    }
    m.revealLevel_ = frontier;
    if (pad >= 0) m.mouse_.position = m.nodes_[std::size_t(pad)].position;
    m.updateWalls();
    m.paths_->updateStatus([&m](int tag) { return tag == kWorldNext ? !m.profile_->bossDefeated(*m.table_, m.world_)
                                                                       : m.locked(m.table_->byUid(tag)); },
                           [&m](int tag) { return m.record(m.table_->byUid(tag)).completed; });

    // The mouse: the level's walk cycle and idle animations.
    std::vector<data::AnimSequence> sequences;
    for (const char* file : Mouse::kSequenceFiles)
        for (data::AnimSequence& s : data::loadAnimSequences(files, file)) sequences.push_back(std::move(s));
    m.mouse_.setUpSprite(sequences);

    // Clouds drift across at 30 degrees from fixed lanes (loadMenuMap, vCloudStartPos).
    static const Vec2 kLanes[5] = {{0, -239}, {0, 295}, {0, 235}, {0, -75}, {0, 60}};
    for (int i = 0; i < 5; ++i) {
        Vec2 start = kLanes[i];
        if (start.y < 0.0f) {
            start.x = start.x - start.y / kCloudTilt;
            start.y = 0.0f;
        }
        Vec2 end = start + Vec2{kScreenW, kScreenW * kCloudTilt};
        if (end.y > kScreenH) {
            end.x = start.x + (kScreenH - start.y) / kCloudTilt;
            end.y = kScreenH;
        }
        m.cloudStart_[std::size_t(i)] = start;
        m.cloudEnd_[std::size_t(i)] = end;
    }
    for (int i = 0; i < 5; ++i) m.spawnCloud(i, false);
    m.cloudSpawnMs_ = 0;

    // showScene: the world's menu music (playWorldMusic) and the birds of its ambience.
    {
        SoundEvent music;
        music.kind = SoundEvent::Music;
        music.file = "sound/music/CM_Menu1.m4a";  // (world 1; the other worlds' tracks not checked yet)
        music.loop = true;
        m.sounds_.push_back(music);
        m.ambience_.start(AmbienceScheme::loadNamed(files, "world" + std::to_string(world + 1)), m.random_);
        for (const std::string& loop : m.ambience_.loops()) {
            SoundEvent e;
            e.file = loop;
            e.loop = true;
            m.sounds_.push_back(e);
        }
    }
    m.mouse_.lastFootprint = m.mouse_.position;
    // The iris opens on the mouse (PinholeManager::startOpenHole).
    m.irisCentre_ = m.mouse_.position;
    m.iris_ = 0.0f;
    m.state_ = State::Opening;
    return made;
}

void WorldMap::setTeleportState(int state) {
    if (state == 0) {
        // It stands at the pad, facing right if the pad lay to its right, else left.
        mouse_.heading = teleport_.from.x > 0.0f ? 0.0f : 3.14159274f;
        mouse_.position = teleport_.at;
    } else if (state == 2) {
        teleport_.in.setAnim(0, teleport_.in.frameCount() - 1, SpriteAnim::Once, 50);
        teleport_.at = nodes_[std::size_t(focused_)].position;
    } else {
        teleport_.out.setAnim(0, teleport_.out.frameCount() - 1, SpriteAnim::Once, 50);
        teleport_.at = mouse_.position;
        teleport_.from = nodes_[std::size_t(focused_)].position - mouse_.position;  // (run w2_back2)
        path_.clear();  // (+0x112ac: the walk ends where it is)
        mouse_.pathIndex = 0;
    }
    teleport_.state = state;
}

// SceneMenuMap::setState(8) @0x1624b4: the mouse starts off the map beside the "previous
// world" node, by (-50, 30) in worlds 2 and 5, (-50, -30) in world 4 and (-75, 0) otherwise;
// it is to end at the current level's pad. Without either node the map is ready at once.
void WorldMap::arriveFromWorldCard() {
    arriveNode_ = -1;
    for (std::size_t i = 0; i < nodes_.size(); ++i)
        if (nodes_[i].type == kWorldPrevious) arriveNode_ = int(i);
    arrivePad_ = current_;
    if (arriveNode_ < 0 || arrivePad_ < 0) {
        arriveNode_ = -1;
        state_ = State::Ready;
        return;
    }
    const Vec2 offset = world_ == 1 || world_ == 4 ? Vec2{-50.0f, 30.0f}
                      : world_ == 3                ? Vec2{-50.0f, -30.0f}
                                                   : Vec2{-75.0f, 0.0f};
    mouse_.position = nodes_[std::size_t(arriveNode_)].position + offset;
    mouse_.lastFootprint = mouse_.position;
    irisKind_ = 5;
    state_ = State::Arriving;
}

const LevelRecord WorldMap::record(const data::LevelInfo* level) const {
    return level ? profile_->record(level->uid) : LevelRecord{};
}

bool WorldMap::locked(const data::LevelInfo* level) const { return !level || !record(level).unlocked; }

// attachGraphicNodes @0x162578: a level's grass, pad, road (none for a world's first level) and
// house. Secret levels have pieces of their own only in some worlds (not in the first).
void WorldMap::attach(int i) {
    const data::LevelInfo* level = levels_[std::size_t(i)].info;
    if (level->secret) return;
    auto add = [](std::vector<int>& to, int piece, int count) {
        if (piece >= 1 && piece <= count && std::find(to.begin(), to.end(), piece) == to.end()) to.push_back(piece);
    };
    add(scenery_.grass, i + 1, sceneryCounts_[0]);
    add(scenery_.roadPads, i + 1, sceneryCounts_[1]);
    add(scenery_.roads, i + (world_ != 0 ? 1 : 0), sceneryCounts_[1]);
    add(level->worldBackground ? scenery_.housesBack : scenery_.housesFront, i + 1, sceneryCounts_[2]);
}

// updateMapWalls @0x15ff0c: the gate after each level stays shut until that level is won.
void WorldMap::updateWalls() {
    for (const Node& n : nodes_) {
        if (n.type != kLevelPad || !n.level || locked(n.level)) continue;
        for (int before : {n.level->by1, n.level->by2})
            if (const data::LevelInfo* b = table_->byUid(before)) walls_->setTagEnabled(b->uid, !record(b).completed);
        if (n.level->mode == "infinite") walls_->setTagEnabled(n.level->uid, !record(n.level).completed);
    }
}

// setFocusedNode @0x163ed8: the title bar names the pad's level (or world).
void WorldMap::setFocus(int node) {
    if (node < 0) return;
    const Node& n = nodes_[std::size_t(node)];
    titleAgeMs_ = 0;
    if (n.type == kLevelPad && n.level) {
        titleKey_ = n.level->nameKey;
        titleNumber_ = n.level->number;
    } else if (n.type == kWorldNext || n.type == kWorldPrevious) {
        titleKey_ = n.type == kWorldNext ? "#924" : "#925";  // string ids 0x39c, 0x39d
        titleNumber_.clear();
    }
}

int WorldMap::padFrame(int node) const {
    const Node& n = nodes_[std::size_t(node)];
    const bool pressed = node == pressed_;
    if (n.type == kWorldNext || n.type == kWorldPrevious) return n.active ? (pressed ? 7 : 6) : -1;
    if (n.type != kLevelPad || !n.level) return -1;
    const LevelRecord r = record(n.level);
    if (!r.unlocked) return -1;
    // A new level's pad appears only at the end of its reveal.
    if (r.fresh && !n.level->secret && (state_ == State::Opening || state_ == State::Revealing)) return -1;
    int frame = 0;
    if (n.level->mode == "infinite") frame = 4;
    else if (r.completed && r.goals[0] && r.goals[1] && r.goals[2]) frame = 2;
    return pressed ? frame + 1 : frame;
}

void WorldMap::touchDown(Vec2 screen) {
    const Vec2 p = toMap(screen);
    finger_.held = true;
    finger_.began = true;
    finger_.down = p;
    finger_.at = p;
    if (state_ != State::Ready) return;
    // mouseEvent: a touch on an open pad remembers it until the finger lifts.
    const int stop = paths_->closestStop(p);
    if (stop < 0 || (p - paths_->stopPosition(stop)).length() > kPadReach) return;
    const int tag = paths_->stopTag(stop);
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        const Node& n = nodes_[i];
        const bool match = n.type == kLevelPad ? n.tag == tag
                                               : (n.type == kWorldNext || n.type == kWorldPrevious) && n.type == tag;
        if (!match) continue;
        const bool open = n.type == kLevelPad ? !locked(n.level) : n.active;
        if (open) pressed_ = int(i);
    }
    pressedStart_ = pressed_;
}

void WorldMap::touchMove(Vec2 screen) {
    const Vec2 p = toMap(screen);
    finger_.at = p;
    if (pressedStart_ < 0) return;
    // Sliding off the pad lets it go.
    pressed_ = (nodes_[std::size_t(pressedStart_)].position - p).length() <= kPadReach ? pressedStart_ : -1;
}

void WorldMap::touchUp(Vec2 screen) {
    finger_.at = toMap(screen);
    finger_.held = false;
    if (state_ == State::Ready && pressed_ >= 0) walkTo(pressed_);
    pressed_ = -1;
    pressedStart_ = -1;
}

// onNodeReleased @0x163f94: the mouse walks to the released pad along the roads.
void WorldMap::walkTo(int node) {
    if (startWalk(nodes_[std::size_t(node)].position)) {
        if (focused_ != node) setFocus(node);
        focused_ = node;
        focusedFallback_ = -1;
    }
    const Node& n = nodes_[std::size_t(node)];
    if (n.type == kLevelPad && locked(n.level)) {
        focused_ = -1;
        focusedFallback_ = -1;
    }
}

bool WorldMap::startWalk(Vec2 target) {
    if (!paths_->createPath(target, mouse_.position, path_, false)) return false;
    path_.resampledEnd = 0;
    path_.resample(true, true, nullptr, 0.0f);  // (HYPOTHESIS: 1.1.1 map calls not checked yet)
    path_.snapped = true;
    path_.drawing = false;
    drawing_ = false;
    mouse_.pathIndex = 0;
    mouse_.snapGlowMs = 1250;
    walkBounce_ = 0.0f;
    sound(kSoundWalk);
    return true;
}

// findClosestNode @0x16039c: the nearest open pad in sight.
int WorldMap::closestNode(Vec2 p) const {
    int best = -1;
    float bestDistance = 99999.0f;
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        const Node& n = nodes_[i];
        const bool candidate = n.type == kWorldNext || n.type == kWorldPrevious || (n.type == kLevelPad && !locked(n.level));
        if (!candidate) continue;
        const float d = (p - n.position).lengthSquared();
        if (d < bestDistance * bestDistance && !walls_->lineTouches(p, n.position, 0.0f, 0)) {
            bestDistance = (p - n.position).length();
            best = int(i);
        }
    }
    return best;
}

void WorldMap::pressPlay() {
    if (!play_.shown || state_ != State::Ready || focused_ < 0) return;
    // (SceneMenuMap::guiEvent @0x15beb0: a mouse still on its way to the pad stops and
    // teleports there, under the closing iris.)
    if (current_ != focused_) setTeleportState(1);
    current_ = focused_;
    const Node& n = nodes_[std::size_t(focused_)];
    play_.shown = false;
    if (n.type == kLevelPad && n.level) {
        // GameBoot::requestLevelBoot: the iris shuts on the pad, then the level starts.
        bootLevel_ = n.level;
        irisCentre_ = n.position;
        profile_->currentLevel = n.level->uid;
        state_ = State::Booting;
        irisKind_ = 0;
    } else if (n.type == kWorldNext || n.type == kWorldPrevious) {
        // A world pad: state 5, the menu pinhole shuts on it (sound 0x8c, the music pauses);
        // the pinhole's callback moves on to state 6, which steps the world and rebuilds the
        // map (update case 6).
        sound(kSoundPlay);
        irisCentre_ = n.position;
        irisKind_ = 0;
        leavingStep_ = n.type == kWorldNext ? 1 : -1;
        leavingShutTicks_ = 0;
        state_ = State::Leaving;
    }
}

// updatePlayButton @0x161ee4: the bubble goes to whichever of six spots around the chosen pad
// keeps farthest from the other pads (and on screen).
void WorldMap::updatePlayBubble() {
    if (playShownFor_ == focused_) return;
    playShownFor_ = focused_;
    if (focused_ < 0) {
        play_.shown = false;
        return;
    }
    const Node& n = nodes_[std::size_t(focused_)];
    if (n.type != kLevelPad && n.type != kWorldNext && n.type != kWorldPrevious) return;
    if (n.type == kLevelPad && locked(n.level)) return;
    const Vec2 at = n.position;
    const bool left = at.x < 240.0f;
    const float side = left ? 40.0f : -40.0f, far = left ? -60.0f : 60.0f;
    const Vec2 spots[6] = {{side, 40.0f}, {-side, 40.0f}, {far, 0.0f}, {-far, 0.0f}, {side, -40.0f}, {-side, -40.0f}};
    Vec2 best = at;
    float bestClearance = 0.0f;
    for (const Vec2& offset : spots) {
        const Vec2 spot = at + offset;
        if (spot.x < 20.0f || spot.x > 460.0f || spot.y < 50.0f || spot.y > 260.0f) continue;
        float clearance = 100000000.0f;
        for (std::size_t i = 0; i < nodes_.size(); ++i) {
            const Node& o = nodes_[i];
            if (int(i) == focused_ || !(o.type == kLevelPad || o.type == kWorldPrevious || o.type == kWorldNext)) continue;
            clearance = std::min(clearance, (o.position - spot).lengthSquared());
        }
        if (bestClearance < clearance) {
            bestClearance = clearance;
            best = spot;
        }
    }
    play_.shown = true;
    play_.node = at;
    play_.base = best;
    play_.centre = best;
    play_.ageMs = 0;
    play_.worldNumber = n.type == kWorldNext ? world_ + 2 : n.type == kWorldPrevious ? world_ : -1;
}

// update's first loop: open levels not yet won glow (and sparkle) unless they are chosen.
void WorldMap::updateHighlights(int dtMs) {
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        Node& n = nodes_[i];
        if (n.type != kLevelPad || !n.level || state_ != State::Ready) continue;
        const LevelRecord r = record(n.level);
        if (!r.unlocked) continue;
        if (!r.completed && int(i) != focused_ && int(i) != focusedFallback_) {
            n.highlight = std::min(1.0f, n.highlight + float(dtMs) * 0.005f);
            if (sparkleMs_ >= 0) {
                sparkleMs_ -= dtMs;
            } else {
                // Hypothesis: the sparkle's offset range (the arguments were not recovered).
                const float dx = random_.within(-12.0f, 12.0f);
                const float dy = random_.within(-12.0f, 12.0f);
                particles_.add(0x1c, -3, n.position + Vec2{dx, dy});
                sparkleMs_ = random_.next() % 100 + 150;
            }
        } else {
            n.highlight = std::max(0.0f, n.highlight - float(dtMs) * 0.005f);
        }
    }
}

// snapPath: a path drawn to (near) an open pad ends on it, and that pad is chosen.
bool WorldMap::snapDrawnPath() {
    if (path_.size() < 2) return false;
    const Vec2 end = path_.back().xy();
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        const Node& n = nodes_[i];
        const bool open = (n.type == kLevelPad && !locked(n.level)) || n.type == kWorldNext || n.type == kWorldPrevious;
        if (!open || (kPadRadius + mouse_.reachDistance) - (n.position - end).length() <= 0.0f) continue;
        if (walls_->lineTouches(end, n.position, 0.0f, 0)) continue;
        focused_ = int(i);
        setFocus(int(i));
        if (path_.size() < DrawnPath::kCapacity) path_.append(n.position);
        path_.resample(true, true, nullptr, 0.0f);  // (HYPOTHESIS: 1.1.1 map calls not checked yet)
        return true;
    }
    return false;
}

// updateTouchPoints @0x164710: a finger near the mouse draws it a path, as in a level.
void WorldMap::updateTouch() {
    if (!finger_.held) {
        if (drawing_) {
            drawing_ = false;
            path_.drawing = false;
            if (snapDrawnPath()) {
                path_.smooth(true, true, &*walls_, 0.0f);
                path_.snapped = true;
                mouse_.snapGlowMs = 1250;
                walkBounce_ = 0.0f;
                sound(kSoundWalk);
            } else {
                path_.resample(true, true, nullptr, 0.0f);  // (HYPOTHESIS: 1.1.1 map calls not checked yet)
                path_.smooth(true, true, nullptr, 0.0f);
            }
        }
        return;
    }
    const float reach = tuning_.playerTouchSize * tuning_.playerTouchSize;
    if (finger_.began) {
        // (allow_path_extend) a touch at the end of the mouse's path continues it.
        if (path_.size() > 1 && (path_.back().xy() - finger_.at).lengthSquared() < reach) finger_.extend = true;
        return;
    }
    auto beginDrawing = [&] {
        drawing_ = true;
        path_.drawing = true;
        path_.snapped = false;
        titleKey_.clear();
        titleNumber_.clear();
        focused_ = -1;
        focusedFallback_ = -1;
        playShownFor_ = -2;
        play_.shown = false;
        pressed_ = -1;
        pressedStart_ = -1;
    };
    if (finger_.extend) {
        if ((finger_.down - finger_.at).lengthSquared() > 250.0f) {
            beginDrawing();
            finger_.extend = false;
        }
        return;
    }
    if (!drawing_) {
        if ((mouse_.position - finger_.at).lengthSquared() < reach && (finger_.down - finger_.at).lengthSquared() > 200.0f) {
            beginDrawing();
            path_.clear();
            path_.drawing = true;
            path_.fingerOffset = mouse_.position - finger_.at;
            mouse_.pathIndex = 0;
        }
        return;
    }
    // Drawing: points follow the finger (offset converging onto it), kept out of walls.
    Vec2 from = path_.empty() ? mouse_.position : path_.back().xy();
    Vec2 to = finger_.at + path_.fingerOffset;
    float minimum = path_.empty() ? 100.0f : 1.0f;
    bool accept = true;
    if (walls_->clipLine(from, to, 0.0f)) {
        accept = !walls_->lineTouches(from, to, 0.0f, 0);
        minimum = 4.0f;
    }
    if (!accept || !((from - to).lengthSquared() > minimum)) return;
    if (path_.size() >= DrawnPath::kCapacity) {
        path_.resample(true, true, nullptr, 0.0f);  // (HYPOTHESIS: 1.1.1 map calls not checked yet)
        path_.drawing = false;
        drawing_ = false;
        return;
    }
    if (path_.size() > 3) path_.fingerOffset *= 0.95f;
    path_.append(to);
    path_.resample(false, true, nullptr, 0.0f);
    path_.smooth(false, true, &*walls_, 0.0f);
}

void WorldMap::spawnCloud(int i, bool atStart) {
    Cloud& c = clouds_[std::size_t(i)];
    if (c.active) return;
    c.active = true;
    const float width = 128.0f * kCloudScale;  // the cloud pictures are 128 wide
    const Vec2 start = cloudStart_[std::size_t(i)], end = cloudEnd_[std::size_t(i)];
    if (atStart) {
        c.position = {start.x - width, start.y - width * kCloudTilt};
    } else {
        const int spanX = std::max(1, int(end.x - start.x)), spanY = int(end.y - start.y);
        c.position.x = float(random_.next() % spanX) + start.x;
        c.position.y = float(spanY != 0 ? random_.next() % spanY : random_.next() * 0) + start.y;
    }
    c.speed = float(random_.next() % 120 + 60) * 0.01f;
    cloudSpawnMs_ = 50;
}

// updateClouds @0x15f678 (without the tilt-sensor parallax).
void WorldMap::updateClouds(int dtMs) {
    const float w = 128.0f * kCloudScale, h = 64.0f * kCloudScale;
    for (Cloud& c : clouds_) {
        if (!c.active) continue;
        const float step = float(dtMs) * c.speed * 0.00120000006f;
        c.position.x += step;
        if (c.position.x > kScreenW + w || c.position.x < -w || c.position.y < -2.0f * h ||
            c.position.y + -100.0f > kScreenH + 2.0f * h) {
            c.active = false;
            continue;
        }
        c.position.y += kCloudTilt * step;
    }
    if (cloudSpawnMs_ < 1) {
        if (random_.next() % 10 == 0) spawnCloud(random_.next() % 5, true);
    } else {
        cloudSpawnMs_ -= dtMs;
    }
}

// Player::onFootprint @0x121874: a step every 10 units, alternately left and right.
void WorldMap::onFootprint() {
    const bool wasLeft = mouse_.footprintSide;
    mouse_.footprintSide = !wasLeft;
    Vec2 side{-libSin(mouse_.heading), libCos(mouse_.heading)};
    side.normalise(2.0f);
    mouse_.lastFootprint = mouse_.position + side * (wasLeft ? -1.0f : 1.0f);
    const int pick = random_.next() % 2;
    sound((mouse_.footprintSide ? sfx::WalkLeft1 : sfx::WalkRight1) + pick);
}

void WorldMap::tick(int dtMs) {
    timeMs_ += dtMs;
    titleAgeMs_ += dtMs;
    particles_.update(dtMs);
    updatePlayBubble();
    if (play_.shown) {
        // GuiMenuMapPlayItem::update @0x217df4: a slow sway around its spot.
        play_.ageMs += dtMs;
        play_.swayPhase += float(dtMs) * 0.00300000003f;
        play_.centre = play_.base + Vec2{libSin(play_.swayPhase) * 4.0f, libCos(play_.swayPhase) * 5.0f};
    }
    if (state_ == State::Ready) updateTouch();
    if (walkBounce_ >= 0.0f) {
        walkBounce_ += float(dtMs) * 0.00300000003f;
        if (walkBounce_ >= 1.0f) walkBounce_ = -1.0f;
    }

    const int previous = current_;
    if (state_ == State::Booting) current_ = -1;
    updateHighlights(dtMs);
    // The pad the mouse is at (within twice its reach); away from all of them for a moment,
    // it heads for the nearest (after 0.5 s within 30, 1 s within 60, else 2 s).
    current_ = -1;
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        if (nodes_[i].type == kBuilding) continue;
        if ((mouse_.position - nodes_[i].position).lengthSquared() < mouse_.reachDistance * mouse_.reachDistance * 4.0f) {
            current_ = int(i);
            break;
        }
    }
    if (current_ < 0) {
        const int nearest = closestNode(mouse_.position);
        if (nearest >= 0) {
            const float d = (mouse_.position - nodes_[std::size_t(nearest)].position).lengthSquared();
            int waitMs = d > 900.0f ? 2000 : 1000;
            if (d <= 3600.0f) waitMs = int(float(waitMs) * 0.5f);
            if (waitMs < mouse_.idleMs && !drawing_ && state_ == State::Ready &&
                startWalk(nodes_[std::size_t(nearest)].position)) {
                focused_ = nearest;
                setFocus(nearest);
            }
        }
    } else if (current_ != previous && current_ == focused_ && nodes_[std::size_t(current_)].type == kLevelPad &&
               path_.size() < 2) {
        setFocus(current_);  // arrived: the title names it again
    }

    // The iris opens on its own while the map already runs (PinholeManager: the hole grows
    // ever faster until it uncovers the screen).
    if (iris_ < 1.0f && state_ != State::Booting && state_ != State::Leaving) {
        iris_ += float(dtMs) * 0.00120000006f * std::max(0.1f, iris_) * 4.0f;
        iris_ = std::min(iris_, 1.0f);
    }
    switch (state_) {
    case State::Opening: {
        // update case 3 (the first tick): a level opened by the last win is revealed first.
        revealLevel_ = -1;
        for (std::size_t i = 0; i < levels_.size(); ++i) {
            const LevelRecord r = record(levels_[i].info);
            if (r.fresh && !r.completed) {
                revealLevel_ = int(i);
                break;
            }
        }
        if (revealLevel_ >= 0) {
            SoundEvent e;
            e.kind = SoundEvent::PlayOnce;
            e.effect = kSoundUnlock;
            sounds_.push_back(e);
            state_ = State::Revealing;
            revealPhase_ = 0;
            revealMs_ = 1200;
            revealScroll_ = 0.0f;
        } else {
            state_ = State::Ready;
        }
        break;
    }
    case State::Revealing: {
        revealMs_ = std::max(0, revealMs_ - dtMs);
        const data::LevelInfo* level = levels_[std::size_t(revealLevel_)].info;
        const int piece = revealLevel_ + 1;
        if (revealPhase_ == 0) {
            // The road grows from the previous pad (its alpha mask scrolls at 0.000505 per ms
            // until every edge has passed the middle: 990 ms for the shipped masks).
            if (revealScroll_ == 0.0f) {
                if (std::find(scenery_.roadPads.begin(), scenery_.roadPads.end(), piece) == scenery_.roadPads.end())
                    scenery_.roadPads.push_back(piece);
                const int road = revealLevel_ + (world_ != 0 ? 1 : 0);
                if (road >= 1 && std::find(scenery_.roads.begin(), scenery_.roads.end(), road) == scenery_.roads.end())
                    scenery_.roads.push_back(road);
                revealScroll_ += float(dtMs);
            } else {
                revealScroll_ += float(dtMs);
                if (revealScroll_ * (level->secret ? 0.00100000005f : 0.000505000004f) >= 0.5002f) {
                    revealPhase_ = 1;
                    revealScroll_ = 0.0f;
                    revealMs_ = 500;
                }
            }
        } else if (revealPhase_ == 1) {
            if (revealMs_ < 1) {
                auto add = [](std::vector<int>& to, int p) { if (std::find(to.begin(), to.end(), p) == to.end()) to.push_back(p); };
                if (piece <= sceneryCounts_[0]) add(scenery_.grass, piece);
                if (piece <= sceneryCounts_[2]) add(level->worldBackground ? scenery_.housesBack : scenery_.housesFront, piece);
                if (record(level).fresh) sound(kSoundAppear);
                revealMs_ = 800;
                housePop_ = 0.0f;
                revealPhase_ = 2;
            }
        } else if (revealPhase_ == 2) {
            housePop_ = std::min(1.0f, housePop_ + float(dtMs) * 0.00200000009f);
            if (revealMs_ < 1) revealPhase_ = 3;
        } else {
            state_ = State::Ready;
            for (const Node& n : nodes_)
                if (n.type == kLevelPad && n.level && record(n.level).fresh) {
                    sound(kSoundPadPop);
                    particles_.add(0x11, -3, n.position);
                    break;
                }
        }
        break;
    }
    case State::Arriving:
        // update case 8: once the pinhole has stopped, the mouse walks straight to the node at
        // the run speed (player_run_speed x 0.05 per ms); within a unit of it, the road to the
        // pad is laid out, and once that is under way the map is ready.
        if (iris_ < 1.0f) break;
        if (arriveNode_ >= 0) {
            Vec2 d = nodes_[std::size_t(arriveNode_)].position - mouse_.position;
            if (d.normalise(1.0f) >= 1.0f) {
                mouse_.heading = float(std::atan2(double(d.y), double(d.x)));
                mouse_.position += d * ((float(dtMs) * tuning_.playerRunSpeed) * 0.0500000007f);
            } else {
                arriveNode_ = -1;
                if (arrivePad_ >= 0 && paths_->createPath(nodes_[std::size_t(arrivePad_)].position, mouse_.position, path_, false)) {
                    path_.drawing = false;
                    mouse_.pathIndex = 0;
                }
                focused_ = arrivePad_;  // (+0x15804: where it stops, the Play bubble's pad)
            }
        } else if (path_.size() < 2) {
            state_ = State::Ready;
        }
        break;
    case State::Leaving:
        // (It shuts in 50 ticks; the pinhole's callback sets state 6 the tick after, and update
        // case 6 builds the next map: state 5 at 5733, 6 at 5783, the new map from 5784 in run
        // w2_back2.)
        if (iris_ <= 0.0f) {
            if (++leavingShutTicks_ >= 2) worldStep_ = leavingStep_;
            break;
        }
        iris_ -= float(dtMs) * 0.00120000006f;
        if (iris_ <= 0.0f) iris_ = 0.0f;
        break;
    case State::Booting:
        iris_ -= float(dtMs) * 0.000800000038f;
        if (iris_ <= 0.0f) {
            iris_ = 0.0f;
            bootDone_ = true;
        }
        break;
    default: break;
    }

    // Player::update: footsteps, then the mouse walks its path.
    if (int((mouse_.position - mouse_.lastFootprint).lengthSquared()) >= 101) onFootprint();
    mouse_.sprite.update(dtMs);
    mouse_.tick(dtMs, path_, 0.0f, tuning_);
    mouse_.updateAnimation(dtMs, random_);
    // The teleport's blurs play once each (50 ms a frame), then the next step.
    if (teleport_.state == 2) {
        teleport_.in.update(dtMs);
        if (teleport_.in.finished()) setTeleportState(0);
    } else if (teleport_.state == 1) {
        teleport_.out.update(dtMs);
        if (teleport_.out.finished()) setTeleportState(2);
    }
    if (mouse_.snapGlowMs > 0) mouse_.snapGlowMs -= dtMs;
    // Standing still, it settles exactly on the chosen pad.
    if (mouse_.idleMs > 100 && focused_ >= 0 && state_ != State::Arriving) mouse_.position = nodes_[std::size_t(focused_)].position;
    Vec2 edge;
    // (Walking in from off the map, the mouse crosses the map's edge freely: run w2_enter.)
    if (arriveNode_ < 0 && (walls_->collideSphere(mouse_.position, mouse_.reachDistance, 0.0f, &edge) & contact::kTrigger)) {
        // A hidden trigger line (type 125) tagged with a secret level's uid: walking over it
        // finds the level (Object::receiveMessage @0x115f18: it is open and new).
        const int tag = int(edge.y);
        for (const WorldLevel& l : levels_)
            if (l.info->uid == tag && locked(l.info)) {
                LevelRecord& r = profile_->at(tag);
                r.unlocked = true;
                r.fresh = true;
                sound(kSoundPadPop);
                if (l.node >= 0) particles_.add(0x11, -3, nodes_[std::size_t(l.node)].position);
            }
        walls_->setTagEnabled(tag, false);
        paths_->updateStatus([this](int t) { return t == kWorldNext ? !profile_->bossDefeated(*table_, world_)
                                                                   : locked(table_->byUid(t)); },
                             [this](int t) { return record(table_->byUid(t)).completed; });
    }
    finger_.began = false;
    updateClouds(dtMs);
    for (const std::string& file : ambience_.update(dtMs, random_)) {
        SoundEvent e;
        e.file = file;
        sounds_.push_back(e);
    }
    mouse_.normaliseHeadingForFrame();
}

}  // namespace doc::game
