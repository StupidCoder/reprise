#include "app/game_flow.hpp"

#include "game/state_io.hpp"

#include <algorithm>
#include <cstring>
#include <ctime>
#include <stdexcept>

namespace doc::app {
namespace {

// A button released over (GuiButton answers on release): the popup's continue closes it; on
// the results, continue leaves the level and retry plays it again; the medals show their goal;
// the pause button opens the pause menu, whose buttons go to it.
void releaseButton(game::Session& session, const std::string& button) {
    if (button.empty()) return;
    if (session.pause().active()) {
        session.pressPauseButton(button);
        return;
    }
    if (session.kiska().active()) {
        session.pressKiskaButton(button);
        return;
    }
    if (button == "PAUSE") session.pressPause();
    if (button == "BTN_CONTINUE") session.pressNoticeButton();
    if (button == "BTN_BACK") session.pressDossierBack();
    if (button == "BTN_OK") session.pressDossierNote();
    if (button == "GAME_RESULTS_CONTINUE") {
        if (session.worldComplete().active()) session.pressWorldContinue();
        else session.pressResultsButton(false);
    }
    if (button == "GAME_RESULTS_RETRY") session.pressResultsButton(true);
    if (button.rfind("LEVEL_GOAL_BTN_", 0) == 0) session.pressResultsGoal(button.back() - '1');
}

// Digger's tutorial starts by itself from world 1's sixth level and a later world's second on
// (SceneGame::startGame @0x10681c: the level data's world, +0xc, and its index there, +0x10).
bool lateForDigger(const data::LevelTable& table, int uid) {
    const data::LevelInfo* info = table.byUid(uid);
    if (!info) return false;
    int index = 0;
    for (const data::LevelInfo& level : table.levels()) {
        if (level.uid == uid) break;
        if (level.world == info->world) ++index;
    }
    return info->world == 0 ? index >= 5 : index >= 1;
}

// Touches reach the level unless a menu above it or a button takes them.
bool touchesLevel(const game::Session& session, const std::string& button) {
    return !session.pause().active() && !session.kiska().active() && button != "PAUSE";
}

// Quick saves: a header, what the scene was built from, the flow's own state, then the
// scene's and the sound's (game/state_io.hpp).
constexpr char kMagic[8] = {'S', 'P', 'Y', 'F', 'L', 'O', 'W', '1'};
constexpr std::uint32_t kStateVersion = 2;

template <class Io>
void ioTouch(Io& io, GameFlow::Touch& t) {
    io(t.kind, t.x, t.y);
}

}  // namespace

GameFlow::GameFlow(const data::GameFiles& files, std::filesystem::path profileFile, platform::Audio& audio)
    : files_(files), profileFile_(std::move(profileFile)), audio_(audio) {
    std::string error;
    table_ = data::LevelTable::load(files_, error);
    profile_.load(profileFile_);
    mapButtons_.loadButtons(files_);
    levelButtons_.loadButtons(files_);
}

GameFlow::~GameFlow() = default;

bool GameFlow::begin(std::string& error) {
    if (!table_) {
        error = "The game's level table could not be read.";
        return false;
    }
    if (profile_.currentLevel < 0) {
        // The very first time: straight into 1-1 (the tutorial).
        profile_.currentLevel = 1;
        if (!startLevel(1, std::uint32_t(now()), now())) {
            error = failure_;
            return false;
        }
        return true;
    }
    const data::LevelInfo* current = table_->byUid(profile_.currentLevel);
    startMap(profile_.world >= 0 ? profile_.world : current ? current->world : 0, false, std::uint32_t(now()));
    if (!map_) {
        error = failure_;
        return false;
    }
    return true;
}

std::int64_t GameFlow::now() const { return clock_ ? *clock_ : std::int64_t(std::time(nullptr)); }

void GameFlow::saveProfile() {
    if (!restoring_) profile_.save(profileFile_);
}

void GameFlow::startMap(int world, bool arriving, std::uint32_t seed) {
    session_.reset();
    info_ = nullptr;
    if (sound_) sound_->stopAll();
    sound_ = std::make_unique<SoundOutput>(files_, audio_);
    const bool silent = restoring_;
    sound_->setSilent(silent);
    start_ = {Scene::Map, world, arriving, 0, seed, 0};
    ++sceneSerial_;
    std::string error;
    map_.reset();
    auto map = game::WorldMap::load(files_, *table_, profile_, world, seed, error);
    if (!map) {
        failure_ = "The world map could not be opened: " + error;
        return;
    }
    map_ = std::move(map);
    if (arriving) map_->arriveFromWorldCard();
    saveProfile();
}

bool GameFlow::startLevel(int uid, std::uint32_t seed, std::int64_t clock) {
    map_.reset();
    if (sound_) sound_->stopAll();
    sound_ = std::make_unique<SoundOutput>(files_, audio_);
    sound_->setSilent(restoring_);
    start_ = {Scene::Level, 0, false, uid, seed, clock};
    ++sceneSerial_;
    info_ = table_->byUid(uid);
    std::string error;
    auto level = info_ ? game::Level::load(files_, *info_, error) : std::nullopt;
    if (!level) {
        failure_ = "Level " + (info_ ? info_->number : std::to_string(uid)) + " could not be opened: " + error;
        session_.reset();
        return false;
    }
    firstWin_ = !profile_.record(uid).completed;
    session_ = std::make_unique<game::Session>(std::move(*level), seed, game::Tuning{}, profile_.strength,
                                               profile_.record(uid).completed);
    session_->setRecord(profile_.record(uid));
    session_->setCheeseBalance(profile_.cheeseBalance());
    session_->setDossiersFound(profile_.dossiers);
    session_->setPowerups(profile_.powerups, profile_.powerupTutorialDone, profile_.metDigger, lateForDigger(*table_, uid));
    session_->setKiskaOffers(profile_.kiska, clock);
    profile_.strength = std::max(profile_.strength, info_->trailLength);
    recorded_ = false;
    if (session_->keepsMusic()) {
        // A boss fight starts no music: the map's goes on (world 1's, WorldMap::showScene).
        game::SoundEvent music;
        music.kind = game::SoundEvent::Music;
        music.file = "sound/music/CM_Menu1.m4a";
        music.loop = true;
        sound_->play({music});
    }
    return true;
}

// After a level: on to the level it leads into (the boss chase into the fight) when won, else
// the map of the current world; after a world's first boss win, the next world's map.
void GameFlow::showMapAfterLevel(bool nextWorld) {
    const bool won = recorded_ || levelWon_;
    if (!nextWorld && won && info_ && info_->autostart >= 0) {
        const int next = info_->autostart;
        profile_.currentLevel = next;
        if (!startLevel(next, std::uint32_t(now()), now())) return;
        levelWon_ = false;
        return;
    }
    const data::LevelInfo* current = table_->byUid(profile_.currentLevel);
    const int world = profile_.world >= 0 ? profile_.world : current ? current->world : 0;
    levelWon_ = false;
    startMap(world, nextWorld, std::uint32_t(now()));
}

void GameFlow::touch(const Touch& touch) { queued_.push_back(touch); }

void GameFlow::handleMapTouch(const Touch& t) {
    const game::Vec2 p{t.x, t.y};
    switch (t.kind) {
    case Touch::Down:
        if (mapButtons_.buttonAt(*map_, p).empty()) map_->touchDown(p);
        break;
    case Touch::Move: map_->touchMove(p); break;
    case Touch::Up: {
        const std::string button = mapButtons_.buttonAt(*map_, p);
        if (button == "PLAY") map_->pressPlay();
        if (button == "BTN_QUIT") {
            left_ = true;
            return;
        }
        map_->touchUp(p);
        break;
    }
    }
}

void GameFlow::handleLevelTouch(const Touch& t) {
    const game::Vec2 p{t.x, t.y};
    game::Session& session = *session_;
    switch (t.kind) {
    case Touch::Down: {
        const std::string button = levelButtons_.buttonAt(session, p);
        if (session.results().active() && button.empty()) session.touchResults();
        if (touchesLevel(session, button)) session.fingerDown(p);
        break;
    }
    case Touch::Move:
        if (touchesLevel(session, levelButtons_.buttonAt(session, p))) session.fingerMove(p);
        break;
    case Touch::Up: {
        const std::string button = levelButtons_.buttonAt(session, p);
        releaseButton(session, button);
        if (touchesLevel(session, button)) session.fingerUp(p);
        break;
    }
    }
}

void GameFlow::retryLevel() {
    if (session_->keepsMusic()) sound_->stopEffects();
    else sound_->stopAll();
    session_ = session_->retry();
    session_->setRecord(profile_.record(info_->uid));
    session_->setCheeseBalance(profile_.cheeseBalance());
    session_->setDossiersFound(profile_.dossiers);
    session_->setPowerups(profile_.powerups, profile_.powerupTutorialDone, profile_.metDigger,
                          lateForDigger(*table_, info_->uid));
    recorded_ = false;
}

void GameFlow::playSounds(std::vector<game::SoundEvent> events, std::int64_t nowMs) {
    sound_->play(sound_->filter(events, nowMs));
}

void GameFlow::tick() {
    if (left_ || !failure_.empty()) return;
    std::vector<Touch> touches;
    touches.swap(queued_);
    for (const Touch& t : touches) {
        if (map_) handleMapTouch(t);
        else if (session_) handleLevelTouch(t);
        if (left_) return;
    }
    if (map_) tickMap();
    else if (session_) tickLevel();
}

void GameFlow::tickMap() {
    map_->tick(game::kTickMs);
    sound_->play(map_->takeSoundEvents());
    if (const data::LevelInfo* level = map_->levelToStart()) {
        saveProfile();
        profile_.currentLevel = level->uid;
        startLevel(level->uid, std::uint32_t(now()), now());
        return;
    }
    if (map_->worldStep() != 0) {
        // update case 6: the profile's world steps and the map is built again.
        profile_.world = map_->world() + map_->worldStep();
        startMap(profile_.world, false, std::uint32_t(now()));
    }
}

void GameFlow::tickLevel() {
    if (session_->retryRequested()) {
        // SceneGame::restartGame: everything stops, the level starts again at once.
        playSounds(session_->takeSoundEvents(), session_->clockMs());
        retryLevel();
    }
    session_->tick(game::kTickMs);
    playSounds(session_->takeSoundEvents(), session_->clockMs());
    if (const int dossier = session_->takeFoundDossier(); dossier >= 0) {
        // (1.1.1) Profile::foundDossier saves at once.
        profile_.foundDossier(dossier);
        saveProfile();
    }
    if (session_->powerups() != profile_.powerups || session_->powerupTutorialDone() != profile_.powerupTutorialDone) {
        // (1.1.1) A power-up used, or the button's tutorial run: the profile keeps it at once
        // (a use also restarts the store reminder's count, Profile +0x2c4).
        for (std::size_t t = 0; t < profile_.powerups.size(); ++t)
            if (session_->powerups()[t].used != profile_.powerups[t].used) profile_.sincePowerupUse = 0;
        profile_.powerups = session_->powerups();
        profile_.powerupTutorialDone = session_->powerupTutorialDone();
        saveProfile();
    }
    if (!(session_->kiskaOffers() == profile_.kiska)) {
        // Kiska's offer has shown (or been counted): the profile keeps that at once.
        profile_.kiska = session_->kiskaOffers();
        saveProfile();
    }
    if (session_->exitRequested()) {
        // SceneWorldCompleteResults::guiEvent @0x2187c8: a boss fight won for the first time
        // moves the profile on to the next world (+0x18, up to the sixth).
        if (session_->worldComplete().active() && firstWin_ && info_->world < 5) {
            profile_.world = info_->world + 1;
            saveProfile();
            showMapAfterLevel(true);
            return;
        }
        showMapAfterLevel(false);
        return;
    }
    if (session_->retryRequested()) {
        // From the pause menu: the level again once the iris has shut.
        retryLevel();
        playSounds(session_->takeSoundEvents(), session_->clockMs());
    }
    const int uid = info_->uid;
    if (session_->worldComplete().active() && !recorded_) {
        // A world's boss fight won for the first time: recorded as the card appears, and the
        // card counts the world's goals after that (updateDynamicLabels).
        profile_.complete(*table_, uid, session_->recordedScore(), session_->goalsWon(), session_->progress().foundSecret);
        for (std::size_t k = 0; k < 5; ++k) profile_.cheese[k] += session_->progress().deliveredKinds[k];
        profile_.currentLevel = uid;
        saveProfile();
        recorded_ = levelWon_ = true;
        int goalsWon = 0, goalsTotal = 0;
        for (const data::LevelInfo& l : table_->levels()) {
            if (l.world != info_->world) continue;
            const game::LevelRecord r = profile_.record(l.uid);
            for (std::size_t g = 0; g < l.goals.size() && g < 3; ++g) {
                if (l.goals[g].type.empty()) continue;  // (no goal kind: -1)
                ++goalsTotal;
                if (r.goals[g]) ++goalsWon;
            }
        }
        session_->setWorldGoals(goalsWon, goalsTotal);
    }
    if (session_->results().active() && !recorded_) {
        // Profile::completeLevel runs as the results are asked for.
        profile_.complete(*table_, uid, session_->recordedScore(), session_->goalsWon(), session_->progress().foundSecret);
        for (std::size_t k = 0; k < 5; ++k) profile_.cheese[k] += session_->progress().deliveredKinds[k];
        profile_.currentLevel = uid;
        saveProfile();
        recorded_ = levelWon_ = true;
    }
    if (session_->results().done()) {
        showMapAfterLevel(false);
        return;
    }
    // The original's render step (Player::render, the cats' facings): once per tick.
    session_->prepareFrame();
}

void GameFlow::draw(platform::Host& host) {
    if (viewHost_ != &host || viewSerial_ != sceneSerial_) {
        mapView_.reset();
        levelView_.reset();
        std::string error;
        if (map_) {
            mapView_ = std::make_unique<render::MapView>();
            if (!mapView_->load(host, files_, *map_, error)) mapView_.reset();
        } else if (session_) {
            levelView_ = std::make_unique<render::LevelView>();
            if (!levelView_->load(host, files_, session_->level(), error)) levelView_.reset();
        }
        viewHost_ = &host;
        viewSerial_ = sceneSerial_;
    }
    if (map_ && mapView_) mapView_->draw(host, *map_);
    else if (session_ && levelView_) levelView_->draw(host, *session_);
}

std::vector<std::uint8_t> GameFlow::saveState() const {
    game::StateWriter w;
    w.bytes.insert(w.bytes.end(), kMagic, kMagic + 8);
    std::uint32_t version = kStateVersion;
    w(version);
    SceneStart start = start_;
    w(start.scene, start.world, start.arriving, start.levelUid, start.seed, start.clock);
    std::string profile = profile_.toText();
    bool firstWin = firstWin_, recorded = recorded_, levelWon = levelWon_;
    w(profile, firstWin, recorded, levelWon);
    w.count(queued_.size());
    for (Touch t : queued_) ioTouch(w, t);
    if (map_) w.save(*map_);
    else if (session_) w.save(*session_);
    w.save(*sound_);
    return std::move(w.bytes);
}

bool GameFlow::restore(std::span<const std::uint8_t> state, std::string& error) {
    if (!table_) {
        error = "The game's level table could not be read.";
        return false;
    }
    const game::Profile before = profile_;
    try {
        game::StateReader r{state};
        if (state.size() < 8 || !std::equal(kMagic, kMagic + 8, state.begin()))
            throw game::StateError("This is not a Spy Mouse save.");
        r.at = 8;
        std::uint32_t version = 0;
        r(version);
        if (version == 1) restoreFromTouches(r);
        else if (version == kStateVersion) restoreState(r);
        else throw game::StateError("This save needs a different version of the game.");
        // Progress made since the save stays: the profile keeps every unlock, best total and won
        // goal of the one on disk.
        game::Profile onDisk;
        if (onDisk.load(profileFile_)) profile_.keepProgressOf(onDisk);
        saveProfile();
        sound_->resume();
        return true;
    } catch (const std::exception& e) {
        restoring_ = false;
        profile_ = before;
        failure_.clear();
        map_.reset();
        session_.reset();
        start_ = {};
        error = e.what();
        return false;
    }
}

void GameFlow::restoreState(game::StateReader& r) {
    SceneStart start;
    r(start.scene, start.world, start.arriving, start.levelUid, start.seed, start.clock);
    if (start.scene != Scene::Map && start.scene != Scene::Level) throw game::StateError("The quick save is damaged.");
    std::string profile;
    bool firstWin = false, recorded = false, levelWon = false;
    r(profile, firstWin, recorded, levelWon);
    std::vector<Touch> queued(r.count());
    for (Touch& t : queued) ioTouch(r, t);
    if (!profile_.fromText(profile)) throw game::StateError("The saved progress could not be read.");

    // The scene as built at its start (silently, without writing the profile), then the
    // saved state over it.
    restoring_ = true;
    failure_.clear();
    left_ = false;
    queued_.clear();
    if (start.scene == Scene::Map) startMap(start.world, start.arriving, start.seed);
    else startLevel(start.levelUid, start.seed, start.clock);
    if (!failure_.empty()) throw game::StateError(failure_);
    if (map_) r.load(*map_);
    else r.load(*session_);
    r.load(*sound_);
    if (r.at != r.bytes.size()) throw game::StateError("The quick save is damaged.");
    restoring_ = false;
    firstWin_ = firstWin;
    recorded_ = recorded;
    levelWon_ = levelWon;
    queued_ = std::move(queued);
}

// Version 1 (until 2026-10-29) kept the scene's start and the touches since, by tick: the
// scene runs again silently to the saved tick.
void GameFlow::restoreFromTouches(game::StateReader& r) {
    std::uint32_t scene = 0, world = 0, arriving = 0, uid = 0, seed = 0, ticks = 0;
    std::int64_t clock = 0;
    std::string profile;
    r(scene, world, arriving, uid, seed, clock, profile, ticks);
    struct Logged {
        std::uint32_t tick = 0, kind = 0;
        float x = 0, y = 0;
    };
    std::vector<Logged> log(r.count());
    for (Logged& t : log) r(t.tick, t.kind, t.x, t.y);
    std::vector<Touch> queued(r.count());
    for (Touch& t : queued) {
        std::uint32_t kind = 0;
        r(kind, t.x, t.y);
        t.kind = Touch::Kind(std::min<std::uint32_t>(kind, Touch::Up));
    }
    if (r.at != r.bytes.size() || (scene != std::uint32_t(Scene::Map) && scene != std::uint32_t(Scene::Level)))
        throw game::StateError("The quick save is damaged.");
    if (!profile_.fromText(profile)) throw game::StateError("The saved progress could not be read.");
    restoring_ = true;
    failure_.clear();
    left_ = false;
    queued_.clear();
    if (scene == std::uint32_t(Scene::Map)) startMap(int(world), arriving != 0, seed);
    else startLevel(int(uid), seed, clock);
    std::size_t next = 0;
    std::uint32_t tick = 0;
    for (; tick < ticks && failure_.empty() && !left_; ++tick) {
        for (; next < log.size() && log[next].tick == tick; ++next)
            queued_.push_back({Touch::Kind(std::min<std::uint32_t>(log[next].kind, Touch::Up)), log[next].x, log[next].y});
        const std::uint64_t serial = sceneSerial_;
        this->tick();
        if (sceneSerial_ != serial) break;  // (cannot happen in a save made within the scene)
    }
    if (!failure_.empty() || tick != ticks || next != log.size())
        throw game::StateError(failure_.empty() ? "The saved moment could not be restored." : failure_);
    restoring_ = false;
    queued_ = std::move(queued);
}

}  // namespace doc::app
