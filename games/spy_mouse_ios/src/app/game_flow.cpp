#include "app/game_flow.hpp"

#include <algorithm>
#include <cstring>
#include <ctime>
#include <stdexcept>

namespace doc::app {
namespace {

std::uint32_t clockSeed() { return std::uint32_t(std::time(nullptr)); }

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
    if (button == "GAME_RESULTS_CONTINUE") {
        if (session.worldComplete().active()) session.pressWorldContinue();
        else session.pressResultsButton(false);
    }
    if (button == "GAME_RESULTS_RETRY") session.pressResultsButton(true);
    if (button.rfind("LEVEL_GOAL_BTN_", 0) == 0) session.pressResultsGoal(button.back() - '1');
}

// Touches reach the level unless a menu above it or a button takes them.
bool touchesLevel(const game::Session& session, const std::string& button) {
    return !session.pause().active() && !session.kiska().active() && button != "PAUSE";
}

// Quick-save encoding: little-endian integers and length-prefixed strings.
constexpr char kMagic[8] = {'S', 'P', 'Y', 'F', 'L', 'O', 'W', '1'};
constexpr std::uint32_t kStateVersion = 1;

struct Writer {
    std::vector<std::uint8_t> bytes;
    void u32(std::uint32_t v) {
        for (int i = 0; i < 4; ++i) bytes.push_back(std::uint8_t(v >> (8 * i)));
    }
    void i64(std::int64_t v) {
        for (int i = 0; i < 8; ++i) bytes.push_back(std::uint8_t(std::uint64_t(v) >> (8 * i)));
    }
    void f32(float v) {
        std::uint32_t u;
        std::memcpy(&u, &v, 4);
        u32(u);
    }
    void str(const std::string& s) {
        u32(std::uint32_t(s.size()));
        bytes.insert(bytes.end(), s.begin(), s.end());
    }
};

struct Reader {
    std::span<const std::uint8_t> bytes;
    std::size_t at = 0;
    void need(std::size_t n) {
        if (bytes.size() - at < n) throw std::runtime_error("The quick save is incomplete.");
    }
    std::uint32_t u32() {
        need(4);
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= std::uint32_t(bytes[at++]) << (8 * i);
        return v;
    }
    std::int64_t i64() {
        need(8);
        std::uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v |= std::uint64_t(bytes[at++]) << (8 * i);
        return std::int64_t(v);
    }
    float f32() {
        const std::uint32_t u = u32();
        float v;
        std::memcpy(&v, &u, 4);
        return v;
    }
    std::string str() {
        const std::uint32_t n = u32();
        need(n);
        std::string s(reinterpret_cast<const char*>(bytes.data() + at), n);
        at += n;
        return s;
    }
};

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
        if (!startLevel(1, clockSeed(), std::int64_t(std::time(nullptr)))) {
            error = failure_;
            return false;
        }
        return true;
    }
    const data::LevelInfo* current = table_->byUid(profile_.currentLevel);
    startMap(profile_.world >= 0 ? profile_.world : current ? current->world : 0, false, clockSeed());
    if (!map_) {
        error = failure_;
        return false;
    }
    return true;
}

void GameFlow::saveProfile() {
    if (!replaying_) profile_.save(profileFile_);
}

void GameFlow::startMap(int world, bool arriving, std::uint32_t seed) {
    session_.reset();
    info_ = nullptr;
    if (sound_) sound_->stopAll();
    sound_ = std::make_unique<SoundOutput>(files_, audio_);
    const bool silent = replaying_;
    sound_->setSilent(silent);
    start_ = {Scene::Map, world, arriving, 0, seed, 0, profile_.toText()};
    sceneTicks_ = 0;
    log_.clear();
    ++sceneSerial_;
    std::string error;
    map_.reset();
    auto map = game::WorldMap::load(files_, *table_, profile_, world, seed, error);
    if (!map) {
        failure_ = "The world map could not be opened: " + error;
        return;
    }
    map_.emplace(std::move(*map));
    if (arriving) map_->arriveFromWorldCard();
    saveProfile();
}

bool GameFlow::startLevel(int uid, std::uint32_t seed, std::int64_t clock) {
    map_.reset();
    if (sound_) sound_->stopAll();
    sound_ = std::make_unique<SoundOutput>(files_, audio_);
    sound_->setSilent(replaying_);
    start_ = {Scene::Level, 0, false, uid, seed, clock, profile_.toText()};
    sceneTicks_ = 0;
    log_.clear();
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
        if (!startLevel(next, clockSeed(), std::int64_t(std::time(nullptr)))) return;
        levelWon_ = false;
        return;
    }
    const data::LevelInfo* current = table_->byUid(profile_.currentLevel);
    const int world = profile_.world >= 0 ? profile_.world : current ? current->world : 0;
    levelWon_ = false;
    startMap(world, nextWorld, clockSeed());
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
        log_.push_back({sceneTicks_, t});
        if (map_) handleMapTouch(t);
        else if (session_) handleLevelTouch(t);
        if (left_) return;
    }
    ++sceneTicks_;
    if (map_) tickMap();
    else if (session_) tickLevel();
}

void GameFlow::tickMap() {
    map_->tick(game::kTickMs);
    sound_->play(map_->takeSoundEvents());
    if (const data::LevelInfo* level = map_->levelToStart()) {
        saveProfile();
        profile_.currentLevel = level->uid;
        startLevel(level->uid, clockSeed(), std::int64_t(std::time(nullptr)));
        return;
    }
    if (map_->worldStep() != 0) {
        // update case 6: the profile's world steps and the map is built again.
        profile_.world = map_->world() + map_->worldStep();
        startMap(profile_.world, false, clockSeed());
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
        profile_.complete(*table_, uid, session_->progress().total, session_->goalsWon(), session_->progress().foundSecret);
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
        profile_.complete(*table_, uid, session_->progress().total, session_->goalsWon(), session_->progress().foundSecret);
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
    Writer w;
    w.bytes.insert(w.bytes.end(), kMagic, kMagic + 8);
    w.u32(kStateVersion);
    w.u32(std::uint32_t(start_.scene));
    w.u32(std::uint32_t(start_.world));
    w.u32(start_.arriving ? 1 : 0);
    w.u32(std::uint32_t(start_.levelUid));
    w.u32(start_.seed);
    w.i64(start_.clock);
    w.str(start_.profile);
    w.u32(sceneTicks_);
    w.u32(std::uint32_t(log_.size()));
    for (const LoggedTouch& t : log_) {
        w.u32(t.tick);
        w.u32(t.touch.kind);
        w.f32(t.touch.x);
        w.f32(t.touch.y);
    }
    w.u32(std::uint32_t(queued_.size()));
    for (const Touch& t : queued_) {
        w.u32(t.kind);
        w.f32(t.x);
        w.f32(t.y);
    }
    return w.bytes;
}

bool GameFlow::restore(std::span<const std::uint8_t> state, std::string& error) {
    if (!table_) {
        error = "The game's level table could not be read.";
        return false;
    }
    try {
        Reader r{state};
        r.need(8);
        if (!std::equal(kMagic, kMagic + 8, state.begin())) throw std::runtime_error("This is not a Spy Mouse save.");
        r.at = 8;
        if (r.u32() != kStateVersion) throw std::runtime_error("This save needs a different version of the game.");
        SceneStart start;
        start.scene = Scene(r.u32());
        start.world = int(r.u32());
        start.arriving = r.u32() != 0;
        start.levelUid = int(r.u32());
        start.seed = r.u32();
        start.clock = r.i64();
        start.profile = r.str();
        const std::uint32_t ticks = r.u32();
        std::vector<LoggedTouch> log(r.u32());
        for (LoggedTouch& t : log) {
            t.tick = r.u32();
            t.touch.kind = Touch::Kind(std::min<std::uint32_t>(r.u32(), Touch::Up));
            t.touch.x = r.f32();
            t.touch.y = r.f32();
        }
        std::vector<Touch> queued(r.u32());
        for (Touch& t : queued) {
            t.kind = Touch::Kind(std::min<std::uint32_t>(r.u32(), Touch::Up));
            t.x = r.f32();
            t.y = r.f32();
        }
        if (r.at != state.size()) throw std::runtime_error("The quick save is damaged.");
        if (start.scene != Scene::Map && start.scene != Scene::Level) throw std::runtime_error("The quick save is damaged.");

        // Run the scene again, silently and without writing the profile.
        const game::Profile before = profile_;
        if (!profile_.fromText(start.profile)) throw std::runtime_error("The saved progress could not be read.");
        replaying_ = true;
        failure_.clear();
        left_ = false;
        queued_.clear();
        if (start.scene == Scene::Map) startMap(start.world, start.arriving, start.seed);
        else startLevel(start.levelUid, start.seed, start.clock);
        start_.profile = start.profile;  // (as saved, also when the file was older)
        std::size_t next = 0;
        for (std::uint32_t tick = 0; tick < ticks && failure_.empty() && !left_; ++tick) {
            while (next < log.size() && log[next].tick == tick) queued_.push_back(log[next++].touch);
            const std::uint64_t serial = sceneSerial_;
            this->tick();
            if (sceneSerial_ != serial) break;  // (cannot happen in a save made within the scene)
        }
        replaying_ = false;
        if (!failure_.empty() || sceneTicks_ != ticks || next != log.size()) {
            const std::string why = failure_;
            profile_ = before;
            failure_.clear();
            map_.reset();
            session_.reset();
            start_ = {};
            throw std::runtime_error(why.empty() ? "The saved moment could not be restored." : why);
        }
        queued_ = std::move(queued);
        // Progress made since the save stays: the profile keeps every unlock, best total and won
        // goal of the one on disk.
        game::Profile onDisk;
        if (onDisk.load(profileFile_)) profile_.keepProgressOf(onDisk);
        saveProfile();
        sound_->resume();
        return true;
    } catch (const std::exception& e) {
        replaying_ = false;
        error = e.what();
        return false;
    }
}

}  // namespace doc::app
