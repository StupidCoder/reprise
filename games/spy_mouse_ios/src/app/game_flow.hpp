// The game as the player moves through it: the world map, the levels, and the way between them
// (the first start straight into 1-1, a won level's results back to the map or on to the level
// it leads into, a world's boss fight on to the next world's map). One 17 ms tick at a time, so
// the standalone app and an embedding launcher (Reprise) run it the same way.
//
// Touches are queued in order and handled at the start of the next tick, as the original
// handles the touches of a frame before its update. Drawing shows the latest state and changes
// nothing in the game: the per-frame presentation step the original's render performs
// (Session::prepareFrame) runs once after every tick instead.
//
// Quick saves: the rules are deterministic for a given start (scene, profile, seed and clock)
// and the same touches at the same ticks, so a save keeps the current scene's start and the
// touches since then; restoring builds that scene again and runs it to the saved tick.
#pragma once

#include "app/sound_output.hpp"
#include "data/game_files.hpp"
#include "data/level_table.hpp"
#include "game/profile.hpp"
#include "game/session.hpp"
#include "game/world_map.hpp"
#include "platform/audio.hpp"
#include "platform/host.hpp"
#include "render/level_view.hpp"
#include "render/map_view.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace doc::app {

class GameFlow {
public:
    struct Touch {
        enum Kind : std::uint8_t { Down, Move, Up };
        Kind kind = Down;
        float x = 0.0f, y = 0.0f;  // play-area points (480 x 320, y down)
    };

    // `profileFile`: the campaign progress (read now, written as the original saves its
    // profile). `audio` plays the game's sound (an output device, or pulled by the embedder).
    GameFlow(const data::GameFiles& files, std::filesystem::path profileFile, platform::Audio& audio);
    ~GameFlow();
    GameFlow(const GameFlow&) = delete;
    GameFlow& operator=(const GameFlow&) = delete;

    // The first scene: level 1-1 the very first time, else the map of the current level's world.
    bool begin(std::string& error);
    // Queued; handled in order at the start of the next tick.
    void touch(const Touch& touch);
    void tick();
    // The latest state into the host's frame (views are loaded for the host on first use).
    void draw(platform::Host& host);

    // The map's quit button was pressed: back to the launcher.
    bool left() const { return left_; }
    // Why the game cannot go on (a level that does not load), or empty.
    const std::string& failure() const { return failure_; }
    // The level being played, or null on the map.
    const game::Session* session() const { return session_.get(); }
    const game::Profile& profile() const { return profile_; }

    // Quick saves (see the file comment). restore() is called on a flow that has not begun;
    // on failure the flow is unchanged and `error` says why. Progress made since the save
    // (unlocks, best totals, goals) is kept: the restored profile takes it over.
    std::vector<std::uint8_t> saveState() const;
    bool restore(std::span<const std::uint8_t> state, std::string& error);

private:
    enum class Scene : std::uint8_t { None, Map, Level };
    // What a scene starts from; with the touches since, everything a quick save needs.
    struct SceneStart {
        Scene scene = Scene::None;
        int world = 0;
        bool arriving = false;     // the map, opened by the world card's continue (state 8)
        int levelUid = 0;
        std::uint32_t seed = 1;    // rand()'s seed (the original seeds it from the clock)
        std::int64_t clock = 0;    // time() for Kiska's offer
        std::string profile;       // the profile as the scene started
    };
    struct LoggedTouch {
        std::uint32_t tick = 0;  // ticks of the scene before it was handled
        Touch touch;
    };

    void startMap(int world, bool arriving, std::uint32_t seed);
    bool startLevel(int uid, std::uint32_t seed, std::int64_t clock);
    void showMapAfterLevel(bool nextWorld);
    void handleMapTouch(const Touch& t);
    void handleLevelTouch(const Touch& t);
    void tickMap();
    void tickLevel();
    void retryLevel();
    void saveProfile();
    void playSounds(std::vector<game::SoundEvent> events, std::int64_t nowMs);

    const data::GameFiles& files_;
    std::filesystem::path profileFile_;
    platform::Audio& audio_;
    std::optional<data::LevelTable> table_;
    game::Profile profile_;
    std::unique_ptr<SoundOutput> sound_;

    SceneStart start_;
    std::uint32_t sceneTicks_ = 0;
    std::vector<LoggedTouch> log_;
    std::vector<Touch> queued_;
    bool replaying_ = false;  // restoring: no profile writes
    bool left_ = false;
    std::string failure_;

    // The map.
    std::optional<game::WorldMap> map_;
    render::MapView mapButtons_;
    // The level.
    const data::LevelInfo* info_ = nullptr;
    std::unique_ptr<game::Session> session_;
    render::LevelView levelButtons_;
    bool firstWin_ = false;
    bool recorded_ = false;  // this run's completion is in the profile
    bool levelWon_ = false;  // won in this visit (a retry after a win keeps it)

    // Drawing (loaded for a host when a scene starts or the host changes).
    platform::Host* viewHost_ = nullptr;
    std::uint64_t sceneSerial_ = 0, viewSerial_ = ~std::uint64_t(0);
    std::unique_ptr<render::MapView> mapView_;
    std::unique_ptr<render::LevelView> levelView_;
};

}  // namespace doc::app
