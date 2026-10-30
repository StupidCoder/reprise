// The world map between levels (the original's SceneMenuMap): the levels of one world as
// pads on a painted landscape, joined by roads; the mouse walks between them, and the Play
// bubble next to the chosen pad starts that level.
//
// What the map shows follows the player's progress: every open level has its patch of grass,
// its house and the road leading to it; a level opened by the last win is revealed with a
// short animation (its road fades in from the previous pad, then its grass and house appear).
// The mouse walks along the roads (MapPaths) when a pad is tapped, or along a path drawn with
// the finger; standing away from every pad for a moment, it walks to the nearest one.
//
// Coordinates are the map's (480 x 320, y up), as in a level. Reconstructed from
// SceneMenuMap::initMenuMap @0x1665e0, update @0x15c0c4, mouseEvent @0x15b5a4,
// updateTouchPoints @0x164710, onNodeReleased @0x163f94, updatePlayButton @0x161ee4,
// updateClouds @0x15f678 and render @0x15e5d8.
#pragma once

#include "data/level_table.hpp"
#include "game/ambience.hpp"
#include "data/map_file.hpp"
#include "game/drawn_path.hpp"
#include "game/level_geometry.hpp"
#include "game/map_paths.hpp"
#include "game/mouse.hpp"
#include "game/particles.hpp"
#include "game/profile.hpp"
#include "game/random.hpp"
#include "game/sound.hpp"
#include "game/tuning.hpp"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace doc::game {

class WorldMap {
public:
    // Map entity types (world/world_NN.mbn vertices with flag 2).
    enum NodeType : int {
        kLevelPad = 990,     // d: the level's uid
        kBuilding = 993,     // d: the uid of the level whose house stands here
        kWorldMarker = 997,  // (not used by the reconstructed rules)
        kWorldPrevious = 998,
        kWorldNext = 999,
    };
    struct Node {
        int type = 0;
        Vec2 position;
        int tag = 0;               // the entity's d
        const data::LevelInfo* level = nullptr;  // level pads
        float highlight = 0.0f;    // +0x118: glow of an open level not yet won (0..1)
        bool active = true;        // building markers are used up once matched to a level
    };
    // One level of the world in the original's order (the pads' levels plus the levels they
    // lead into, by uid): its picture pieces are numbered by this order.
    struct WorldLevel {
        const data::LevelInfo* info = nullptr;
        int node = -1;             // its pad, -1 for levels without one (the boss fight)
    };
    // The pieces of the landscape that are shown (by their number in the world's files).
    struct Scenery {
        std::vector<int> grass;          // world01_grassNN
        std::vector<int> roadPads;       // world01_nodeNN (the pads under the level icons)
        std::vector<int> roads;          // world01_road_NN
        std::vector<int> housesFront;    // world01_buildingNN in front of the mouse
        std::vector<int> housesBack;     // behind the landscape details (world_bg levels)
        std::vector<std::pair<int, Vec2>> housePositions;  // where each house stands
    };
    struct Cloud {
        bool active = false;
        Vec2 position;
        float speed = 0.0f;
    };
    // The bubble that starts the chosen level (GuiMenuMapPlayItem).
    struct PlayBubble {
        bool shown = false;
        Vec2 node;       // the pad it points at
        Vec2 base;       // its resting centre
        Vec2 centre;     // with its sway
        float swayPhase = 0.0f;
        int worldNumber = -1;  // world pads show "World N" instead of "Play!"
        int ageMs = 0;          // since it appeared (it pops in)
    };
    enum class State { Opening, Revealing, Ready, Booting, Leaving, Arriving };

    static std::optional<WorldMap> load(const data::GameFiles& files, const data::LevelTable& table,
                                        Profile& profile, int world, std::uint32_t seed, std::string& error);

    // Touches in screen points (y down).
    void touchDown(Vec2 screen);
    void touchMove(Vec2 screen);
    void touchUp(Vec2 screen);
    // The Play bubble was pressed.
    void pressPlay();
    // Shown by the world card's continue (SceneWorldCompleteResults::guiEvent: setState 8):
    // the map waits for the iris to open and is ready, with no reveal of a new level.
    void arriveFromWorldCard();
    // Once a world pad's iris has shut: +1 for the next world, -1 for the previous (else 0).
    int worldStep() const { return worldStep_; }
    void tick(int dtMs);

    State state() const { return state_; }
    // The level to start once the iris has closed (Booting finished), else null.
    const data::LevelInfo* levelToStart() const { return bootDone_ ? bootLevel_ : nullptr; }
    int timeMs() const { return timeMs_; }
    int world() const { return world_; }

    const std::vector<Node>& nodes() const { return nodes_; }
    const std::vector<WorldLevel>& levels() const { return levels_; }
    const Scenery& scenery() const { return scenery_; }
    const std::array<Cloud, 5>& clouds() const { return clouds_; }
    const PlayBubble& playBubble() const { return play_; }
    const Mouse& mouse() const { return mouse_; }
    const DrawnPath& path() const { return path_; }
    const Particles& particles() const { return particles_; }
    const data::MapFile& map() const { return map_; }
    // The pad drawn with the selection ring (-1 none) and the pad pressed right now.
    int focusedNode() const { return focused_ >= 0 ? focused_ : focusedFallback_; }
    int pressedNode() const { return pressed_; }
    // Frame of ui/map_nodes.png for a node, -1 when it is not shown.
    int padFrame(int node) const;
    // The title bar's text: the focused level's number and name (string key), or a world pad's.
    const std::string& titleKey() const { return titleKey_; }
    const std::string& titleNumber() const { return titleNumber_; }
    int titleAgeMs() const { return titleAgeMs_; }
    // The reveal of a newly opened level: which (index into levels()), its phase (0 road,
    // 1 waiting, 2 house popping in, 3 done), the road's mask shift and the phase's time left.
    struct Reveal {
        int level = -1;
        int phase = 0;
        float shift = 0.0f;
        int ms = 0;
        float housePop = 0.0f;  // +0x1570c: 0..1 while the house pops in
    };
    Reveal reveal() const {
        if (state_ != State::Revealing) return {};
        const float rate = revealLevel_ >= 0 && levels_[std::size_t(revealLevel_)].info->secret ? 0.00100000005f
                                                                                                : 0.000505000004f;
        return {revealLevel_, revealPhase_, revealScroll_ * rate, revealMs_, housePop_};
    }
    // The bounce of the pads after the mouse sets off (+0x15c60, -1 when still).
    float walkBounce() const { return walkBounce_; }
    // The iris: 0 shut .. 1 open.
    float irisOpening() const { return iris_; }
    // PinholeManager's pattern: the menu's (0), or the bonus pattern (5) the world card closed
    // with, which the map opens on after it (run w2_enter); closing for a level is the menu's.
    int irisKind() const { return irisKind_; }
    // Play pressed while the mouse is not at the chosen pad (setPlayerTeleportState @0x163980):
    // 1 a blur where it was (sprites/player_blur1), 2 a blur at the pad (player_blur2), 0 the
    // mouse stands there. The mouse itself is not drawn in 1 and 2.
    struct Teleport {
        int state = 0;
        Vec2 at;         // +0x15664: where the blur is drawn (the mouse's place, then the pad)
        Vec2 from;       // +0x1566c: the pad, relative to where it went from
        SpriteAnim out;  // +0x154ec, player_blur1
        SpriteAnim in;   // +0x155a8, player_blur2
    };
    const Teleport& teleport() const { return teleport_; }
    Vec2 irisCentre() const { return irisCentre_; }

    std::vector<SoundEvent> takeSoundEvents() { return std::move(sounds_); }
    const Profile& profile() const { return *profile_; }

private:
    WorldMap(std::uint32_t seed, std::vector<ParticleKind> kinds);
    const LevelRecord record(const data::LevelInfo* level) const;
    bool locked(const data::LevelInfo* level) const;
    void attach(int levelIndex);        // attachGraphicNodes with all pieces
    void updateWalls();                 // updateMapWalls @0x15ff0c
    void setFocus(int node);            // setFocusedNode @0x163ed8
    void walkTo(int node);              // onNodeReleased's walk
    bool startWalk(Vec2 target);
    int closestNode(Vec2 p) const;      // findClosestNode @0x16039c
    void updatePlayBubble();            // updatePlayButton @0x161ee4
    void updateHighlights(int dtMs);
    void updateTouch();                 // updateTouchPoints @0x164710
    bool snapDrawnPath();
    void updateClouds(int dtMs);
    void spawnCloud(int i, bool atStart);
    void sound(int effect) {
        SoundEvent e;
        e.effect = effect;
        sounds_.push_back(e);
    }

    const data::LevelTable* table_ = nullptr;
    Profile* profile_ = nullptr;
    int world_ = 0;
    data::MapFile map_;
    std::optional<LevelGeometry> walls_;
    std::optional<MapPaths> paths_;
    std::vector<Node> nodes_;
    std::vector<WorldLevel> levels_;
    Scenery scenery_;
    int sceneryCounts_[3] = {13, 13, 12};  // grass, roads/pads, houses per world (loadSceneGraph)

    GameRandom random_;
    Particles particles_;
    Tuning tuning_;
    Mouse mouse_;
    // The arrival from the world card (state 8): the "previous world" node the mouse first
    // walks to, and the pad it then takes the road to (SceneMenuMap +0x15c50, +0x15c54).
    int arriveNode_ = -1, arrivePad_ = -1;
    int irisKind_ = 0;
    int leavingStep_ = 0, worldStep_ = 0, leavingShutTicks_ = 0;
    Teleport teleport_;
    void setTeleportState(int state);
    DrawnPath path_;
    std::vector<SoundEvent> sounds_;
    Ambience ambience_;
    void onFootprint();

    State state_ = State::Opening;
    int timeMs_ = 0;
    int current_ = -1;          // +0x152ac the pad the mouse stands at
    int focused_ = -1;          // +0x15804 the chosen pad
    int focusedFallback_ = -1;  // +0x15c58
    int pressed_ = -1;          // +0x11a8c pad under a touch that went down on it
    int pressedStart_ = -1;     // +0x11a90
    int playShownFor_ = -2;     // +0x15c7c
    int sparkleMs_ = -1;        // +0x154e8
    PlayBubble play_;
    std::string titleKey_, titleNumber_;
    int titleAgeMs_ = 0;
    std::array<Cloud, 5> clouds_{};
    int cloudSpawnMs_ = 0;
    std::array<Vec2, 5> cloudStart_{}, cloudEnd_{};

    // The finger (touch 1 of the original's three slots).
    struct Finger {
        bool held = false, began = false;
        Vec2 down, at;        // map coordinates
        bool extend = false;  // went down at the end of the mouse's path
    } finger_;
    bool drawing_ = false;

    // Reveal of a newly opened level (+0x15700 phase, +0x15704 timer, +0x15710 road scroll).
    int revealLevel_ = -1;
    int revealPhase_ = 0;
    int revealMs_ = 0;
    float revealScroll_ = 0.0f;

    float housePop_ = 0.0f;
    float walkBounce_ = -1.0f;
    float iris_ = 0.0f;
    Vec2 irisCentre_;
    const data::LevelInfo* bootLevel_ = nullptr;
    bool bootDone_ = false;
};

}  // namespace doc::game
