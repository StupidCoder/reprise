// DoubleOCheese shell: the launcher (import the game from the user's IPA, Play) and the game.
//
//   doubleocheese                      the launcher (the app bundle starts this)
//   doubleocheese FILE.ipa             the launcher, importing FILE first
//   doubleocheese --import FILE.ipa    import on the command line
//   doubleocheese --preview-launcher OUT.bmp   headless picture of the launcher
//   doubleocheese <path to SpyMouse.app> [level number, default 1-1]   play from an unpacked app
//   doubleocheese <path to SpyMouse.app> 1-1 --replay SCRIPT FIRST-FRAME --shot FRAME FILE.bmp ...
//                 [--seed N] [--strength N] [--record BEST G1 G2 G3] [--world-goals WON TOTAL]
//
// The replay form runs headless: touch lines of a reference-harness script (FRAME down|move|up
// X Y) are fed in one 17 ms tick per frame from FIRST-FRAME on, and the given frames are saved.
#include "game/float_mode.hpp"
#include "app/game_flow.hpp"
#include "app/script_pokes.hpp"
#include "game/state_io.hpp"
#include "app/launcher.hpp"
#include "app/sound_output.hpp"
#include "data/level_table.hpp"
#include "importer/ipa_import.hpp"
#include "game/session.hpp"
#include "platform/host.hpp"
#include "game/world_map.hpp"
#include "render/level_view.hpp"
#include "render/map_view.hpp"

#include <chrono>
#include <cstdio>
#include <memory>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <ctime>
#include <vector>

using namespace doc;

namespace {

// Game ticks from real time. The original's scheduler (iPhone::Update @0x111098) runs whole
// ticks while at least half a tick is pending, the last one possibly shorter, at most three per
// frame. On its 60 Hz screen that gives steady 16-17 ms ticks; on a faster display it would
// give mostly 8-9 ms ticks, and rules that act per tick rather than per millisecond (a carried
// cheese closes 20 % of its gap each tick, two random numbers per tick, ...) would run at twice
// the rate and drift as the tick lengths vary. The port therefore runs only whole 17 ms ticks
// (at most three per frame, then the backlog is dropped) and draws the latest state on every
// display frame.
class TickClock {
public:
    std::vector<int> advance(std::uint64_t nowMs) {
        std::vector<int> steps;
        if (!started_) {
            last_ = nowMs;
            started_ = true;
        }
        if (nowMs < last_) last_ = nowMs;
        const std::uint64_t tick = std::uint64_t(game::kTickMs);
        while (nowMs - last_ >= tick) {
            last_ += tick;
            steps.push_back(game::kTickMs);
            if (steps.size() == 3) {
                if (nowMs - last_ >= tick) last_ = nowMs;
                break;
            }
        }
        return steps;
    }

private:
    bool started_ = false;
    std::uint64_t last_ = 0;
};

// A button released over (GuiButton answers on release): the popup's continue closes it; on
// the results, continue leaves the level (the original goes on to the map, which the port does
// not have yet) and retry plays it again; the medals show their goal; the pause button opens
// the pause menu, whose buttons go to it.
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

// Touches reach the level unless a menu above it or a button takes them.
bool touchesLevel(const game::Session& session, const std::string& button) {
    return !session.pause().active() && !session.kiska().active() && button != "PAUSE";
}

// Headless replay of a harness script (see the file comment).
int replay(const data::GameFiles& files, game::Level level, int argc, char** argv) {
    const char* script = nullptr;
    int first = 0;
    std::uint32_t seed = 1;
    std::map<int, std::string> shots;
    game::LevelRecord record;
    int worldGoals[2] = {0, 0};
    int strength = 1;
    for (int i = 3; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--replay") && i + 2 < argc) {
            script = argv[++i];
            first = std::atoi(argv[++i]);
        } else if (!std::strcmp(argv[i], "--record") && i + 4 < argc) {
            // What the profile knew before: best total and the three goals.
            record.best = std::atoi(argv[++i]);
            for (int g = 0; g < 3; ++g) record.goals[std::size_t(g)] = std::atoi(argv[++i]) != 0;
        } else if (!std::strcmp(argv[i], "--seed") && i + 1 < argc) {
            seed = std::uint32_t(std::strtoul(argv[++i], nullptr, 10));
        } else if (!std::strcmp(argv[i], "--strength") && i + 1 < argc) {
            strength = std::atoi(argv[++i]);  // the profile's carrying strength
        } else if (!std::strcmp(argv[i], "--world-goals") && i + 2 < argc) {
            // The world's goals for a world card (won, total), as the profile would count them.
            worldGoals[0] = std::atoi(argv[++i]);
            worldGoals[1] = std::atoi(argv[++i]);
        } else if (!std::strcmp(argv[i], "--shot") && i + 2 < argc) {
            int frame = std::atoi(argv[++i]);
            shots[frame] = argv[++i];
        }
    }
    struct Event { int frame; std::string kind; float x, y; };
    std::vector<Event> events;
    std::ifstream in(script ? script : "");
    for (std::string line; std::getline(in, line);) {
        std::istringstream ss(line);
        Event e{};
        if ((ss >> e.frame >> e.kind) && (e.kind == "down" || e.kind == "move" || e.kind == "up") && (ss >> e.x >> e.y))
            events.push_back(e);
    }
    std::string error;
    auto host = platform::Host::createOffscreen(error);
    render::LevelView view;
    if (!host || !view.load(*host, files, level, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    auto owned = std::make_unique<game::Session>(std::move(level), seed, game::Tuning{}, strength, record.best >= 0);
    owned->setRecord(record);
    owned->setKiskaOffers({}, std::int64_t(std::time(nullptr)));  // a profile that has not seen it
    if (script) {
        const app::ScriptProfile poked = app::readScriptProfile(script);
        owned->setPowerups(poked.powerups, poked.tutorialDone, poked.metDigger);
    }
    // Sound events are decoded (not played) to check that every file the run needs is there.
    platform::Audio silent;
    app::SoundOutput sound(files, silent);
    const int last = shots.empty() ? first : shots.rbegin()->first;
    // The level's start (music, ambience) belongs to the boot frame, before the first tick.
    if (std::getenv("DOC_SOUND_LOG"))
        for (const game::SoundEvent& e : owned->peekSoundEvents())
            std::printf("%d %s%s %s\n", first - 1, e.kind == game::SoundEvent::Music ? "music" : "play",
                        e.loop ? " loop" : "", sound.fileOf(e).c_str());
    sound.play(owned->takeSoundEvents());
    for (int frame = first; frame <= last; ++frame) {
        for (const Event& e : events) {
            if (e.frame != frame - 1) continue;  // handled in the next tick, as in the harness
            const std::string button = view.buttonAt(*owned, {e.x, e.y});
            if (e.kind == "up") releaseButton(*owned, button);
            if (e.kind == "down" && owned->results().active() && button.empty()) owned->touchResults();
            if (touchesLevel(*owned, button)) {
                if (e.kind == "down") owned->fingerDown({e.x, e.y});
                if (e.kind == "move") owned->fingerMove({e.x, e.y});
                if (e.kind == "up") owned->fingerUp({e.x, e.y});
            }
            if (owned->retryRequested()) {
                // SceneGame::restartGame: everything stops, the level starts again at once.
                const std::vector<game::SoundEvent> lastSounds = owned->takeSoundEvents();
                if (std::getenv("DOC_SOUND_LOG"))
                    for (const game::SoundEvent& s : lastSounds) std::printf("%d play %s\n", frame, sound.fileOf(s).c_str());
                sound.play(lastSounds);
                sound.stopAll();
                if (std::getenv("DOC_SOUND_LOG"))
                    for (const game::SoundEvent& s : lastSounds) std::printf("%d stop %s\n", frame, sound.fileOf(s).c_str());
                owned = owned->retry();
            }
        }
        owned->tick(game::kTickMs);
        if (owned->worldComplete().active()) owned->setWorldGoals(worldGoals[0], worldGoals[1]);
        const std::vector<game::SoundEvent> soundEvents = sound.filter(owned->takeSoundEvents(), std::int64_t(frame) * game::kTickMs);
        // DOC_SOUND_LOG: every event with the frame it belongs to (compare with
        // .analysis/tools/soundtrace.py on a reference run's trace).
        static const bool logSound = std::getenv("DOC_SOUND_LOG") != nullptr;
        if (logSound)
            for (const game::SoundEvent& e : soundEvents) {
                static const char* kinds[] = {"play", "once", "stop", "music", "music stop"};
                std::printf("%d %s%s %s\n", frame, kinds[e.kind], e.loop ? " loop" : "", sound.fileOf(e).c_str());
            }
        sound.play(soundEvents);
        if (owned->exitRequested()) break;  // the map (not ported)
        if (owned->retryRequested()) {
            // From the pause menu: SceneGame::reloadFromSnapshot in this frame, after its tick.
            if (logSound) {
                for (const std::string& loop : owned->level().ambience().loops) std::printf("%d stop %s\n", frame, loop.c_str());
                for (const std::string& file : owned->ambienceSounding()) std::printf("%d stop %s\n", frame, file.c_str());
            }
            sound.stopAll();
            owned = owned->retry();
            if (logSound)
                for (const game::SoundEvent& e : owned->peekSoundEvents())
                    std::printf("%d %s%s %s\n", frame, e.kind == game::SoundEvent::Music ? "music" : "play",
                                e.loop ? " loop" : "", sound.fileOf(e).c_str());
            sound.play(owned->takeSoundEvents());
        }
        // (Planning aid: the frame the level is won or lost, once.)
        if (static bool reported = false; !reported && owned->progress().over) {
            reported = true;
            std::printf("level %s at frame %d\n", owned->progress().won ? "won" : "lost", frame);
        }
        // DOC_OBJ_LIST: every object once (id, type, tag, floor, place; route planning).
        if (static bool listed = false; !listed && std::getenv("DOC_OBJ_LIST")) {
            listed = true;
            for (const game::LevelObject& o : owned->objects())
                std::printf("obj %d type %d tag %d floor %d at %.0f,%.0f\n", o.id, o.type, o.tag, o.floor, o.position.x,
                            game::kScreenHeight - o.position.y);
        }
        // DOC_WALLS=1: the shown floor's wall lines once, in screen coordinates (route planning);
        // DOC_WALLS=N (2..) the walls of floor N-1.
        if (static bool dumped = false; !dumped && std::getenv("DOC_WALLS")) {
            dumped = true;
            const int f = std::atoi(std::getenv("DOC_WALLS"));
            const game::LevelGeometry& geometry = f >= 1 && f <= owned->floorCount() ? owned->wallsOfFloor(f - 1) : owned->walls();
            for (const game::Wall& w : geometry.walls())
                std::printf("wall %.0f,%.0f %.0f,%.0f type %d tag %d %s\n", w.a.x, game::kScreenHeight - w.a.y, w.b.x,
                            game::kScreenHeight - w.b.y, w.type, w.tag, w.enabled ? "on" : "off");
        }
        // DOC_OBJ_LOG=id[,id...]: those objects' and the mouse's positions every 10 frames (route
        // planning against patrols; DOC_OBJ_LOG_EVERY=N for every N frames).
        static const int logEvery = std::getenv("DOC_OBJ_LOG_EVERY") ? std::max(1, std::atoi(std::getenv("DOC_OBJ_LOG_EVERY"))) : 10;
        if (static const char* ids = std::getenv("DOC_OBJ_LOG"); ids && frame % logEvery == 0) {
            const game::Vec2 m = owned->mouse().position;
            std::printf("objlog %d view %.2f mouse %.0f,%.0f st%d pred%d/%d", frame, owned->viewLeft(), m.x,
                        game::kScreenHeight - m.y, owned->mouse().state, int(owned->prediction().enoughCheese),
                        owned->prediction().lastTarget);
            for (const char* p = ids; *p;) {
                const int id = std::atoi(p);
                if (id >= 0 && id < int(owned->objects().size())) {
                    const game::LevelObject& o = owned->objects()[std::size_t(id)];
                    std::printf(" o%d %.0f,%.0f s%d%s", id, o.position.x, game::kScreenHeight - o.position.y, int(o.state),
                                o.inTrail ? "c" : o.active ? "" : "x");
                    if (std::getenv("DOC_OBJ_LOG_FRAMES")) std::printf(" f%d/%d", o.anim.frame(), o.anim.frameCount());
                    for (const game::Droid& d : owned->droids())
                        if (d.objectId() == id) std::printf(" step%d", d.step());
                }
                while (*p && *p != ',') ++p;
                if (*p) ++p;
            }
            if (std::getenv("DOC_OBJ_LOG_CATS"))
                for (const game::Cat& c : owned->cats())
                    std::printf(" cat%d/%d %.0f,%.0f s%d h%.0f", c.objectId(), c.floor(), c.position().x,
                                game::kScreenHeight - c.position().y, c.state(), c.heading() * 57.2958f);
            std::printf("\n");
        }
        // DOC_TRACE_DRAWS: random draws per frame (compare with the reference trace's rand lines).
        static const bool traceDraws = std::getenv("DOC_TRACE_DRAWS") != nullptr;
        if (traceDraws) {
            // (A retry's session continues the generator, and its count.)
            static std::uint64_t before = 0;
            std::printf("draws %d %llu pause %d\n", frame, (unsigned long long)(owned->random().draws() - before),
                        int(owned->pause().active()));
            before = owned->random().draws();
        }
        owned->prepareFrame();
        auto shot = shots.find(frame);
        if (shot == shots.end()) continue;
        host->beginFrame();
        view.draw(*host, *owned);
        host->endFrame();
        if (!host->saveFrame(shot->second)) std::fprintf(stderr, "could not write %s\n", shot->second.c_str());
    }
    std::printf("sound events: %d\n", sound.played());
    for (const std::string& file : sound.missing()) std::printf("sound missing: %s\n", file.c_str());
    return 0;
}

// Headless replay on the world map: `--map PROGRESS` names a progress file (the port's format)
// to start from; touches of a harness script drive the map from FIRST-FRAME on; shots as for
// levels. The map's first tick is FIRST-FRAME.
int mapReplay(const data::GameFiles& files, int argc, char** argv) {
    const char* script = nullptr;
    const char* progress = nullptr;
    int first = 0;
    int world = 0;
    bool arrive = false;  // opened by the world card's continue (state 8)
    std::map<int, std::string> shots;
    for (int i = 2; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--map") && i + 1 < argc) progress = argv[++i];
        else if (!std::strcmp(argv[i], "--world") && i + 1 < argc) world = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--arrive")) arrive = true;
        else if (!std::strcmp(argv[i], "--replay") && i + 2 < argc) {
            script = argv[++i];
            first = std::atoi(argv[++i]);
        } else if (!std::strcmp(argv[i], "--shot") && i + 2 < argc) {
            const int frame = std::atoi(argv[++i]);
            shots[frame] = argv[++i];
        }
    }
    std::string error;
    auto table = data::LevelTable::load(files, error);
    game::Profile profile;
    if (progress) profile.load(progress);
    auto map = table ? game::WorldMap::load(files, *table, profile, world, 1, error) : nullptr;
    if (map && arrive) map->arriveFromWorldCard();
    auto host = platform::Host::createOffscreen(error);
    render::MapView view;
    if (!map || !host || !view.load(*host, files, *map, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    struct Touch { int frame; std::string kind; float x, y; };
    std::vector<Touch> touches;
    std::ifstream in(script ? script : "");
    for (std::string line; std::getline(in, line);) {
        std::istringstream ss(line);
        Touch t{};
        if ((ss >> t.frame >> t.kind) && (t.kind == "down" || t.kind == "move" || t.kind == "up") && (ss >> t.x >> t.y))
            touches.push_back(t);
    }
    const int last = shots.empty() ? first : shots.rbegin()->first;
    // DOC_QUICKSAVE=K: every K frames the map is quick-saved and loaded into a new one built from
    // its files (game/state_io.hpp), which plays on; an uninterrupted twin plays the same touches,
    // and the two states must stay the same byte for byte.
    const int quickSaveEvery = std::getenv("DOC_QUICKSAVE") ? std::atoi(std::getenv("DOC_QUICKSAVE")) : 0;
    std::unique_ptr<game::WorldMap> twin;
    if (quickSaveEvery > 0) {
        twin = game::WorldMap::load(files, *table, profile, world, 1, error);
        if (twin && arrive) twin->arriveFromWorldCard();
    }
    auto feed = [&](game::WorldMap& m, const Touch& t) {
        const game::Vec2 p{t.x, t.y};
        const std::string button = view.buttonAt(m, p);
        if (t.kind == "down" && button.empty()) m.touchDown(p);
        if (t.kind == "move") m.touchMove(p);
        if (t.kind == "up") {
            if (button == "PLAY") m.pressPlay();
            m.touchUp(p);
        }
    };
    // update case 6: the map of the next or previous world is built (as the app does).
    auto stepWorld = [&](std::unique_ptr<game::WorldMap>& m) {
        if (m->worldStep() == 0) return;
        const int next = m->world() + m->worldStep();
        m = game::WorldMap::load(files, *table, profile, next, 1, error);
    };
    auto saved = [](const game::WorldMap& m) {
        game::StateWriter w;
        w.save(m);
        return std::move(w.bytes);
    };
    int quickSaves = 0;
    for (int frame = first; frame <= last; ++frame) {
        for (const Touch& t : touches) {
            if (t.frame != frame - 1) continue;
            feed(*map, t);
            if (twin) feed(*twin, t);
        }
        map->tick(game::kTickMs);
        const bool stepped = map->worldStep() != 0;
        stepWorld(map);
        if (stepped && (!map || !view.load(*host, files, *map, error))) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
        if (twin) {
            twin->tick(game::kTickMs);
            stepWorld(twin);
            if ((frame - first) % quickSaveEvery == 0) {
                const std::vector<std::uint8_t> bytes = saved(*map);
                auto loaded = game::WorldMap::load(files, *table, profile, map->world(), 1, error);
                game::StateReader r{bytes};
                try {
                    if (!loaded) throw game::StateError(error);
                    r.load(*loaded);
                    if (r.at != bytes.size()) throw game::StateError("bytes left over");
                } catch (const std::exception& e) {
                    std::printf("quick save: loading at frame %d failed: %s\n", frame, e.what());
                    return 1;
                }
                map = std::move(loaded);
                ++quickSaves;
            }
            if (!twin || saved(*map) != saved(*twin)) {
                std::printf("quick save: first difference at frame %d (after %d loads)\n", frame, quickSaves);
                return 1;
            }
        }
        if (std::getenv("DOC_MAP_LOG"))
            std::printf("%d mouse %.3f %.3f state %d\n", frame, map->mouse().position.x, map->mouse().position.y,
                        int(map->state()));
        auto shot = shots.find(frame);
        if (shot == shots.end()) continue;
        host->beginFrame();
        view.draw(*host, *map);
        host->endFrame();
        if (!host->saveFrame(shot->second)) std::fprintf(stderr, "could not write %s\n", shot->second.c_str());
    }
    return 0;
}

// Plays one level in the window until it is left: for the map (results continued, the pause
// menu's exit), Escape (back to the launcher) or the window closing (quit). A won level is
// recorded in the profile as its results appear.
// NextWorld: the world card's continue after a world's first boss win (the profile has moved
// on to the next world; its map opens in state 8).
enum class LevelExit { Map, Back, Quit, Failed, NextWorld };

LevelExit playLevel(platform::Host& host, const data::GameFiles& files, const data::LevelTable& table,
                    game::Profile& profile, const std::filesystem::path& profileFile, const data::LevelInfo& info,
                    bool* won = nullptr) {
    std::string error;
    auto level = game::Level::load(files, info, error);
    render::LevelView view;
    if (!level || !view.load(host, files, *level, error)) {
        std::fprintf(stderr, "level %s: %s\n", info.number.c_str(), error.c_str());
        return LevelExit::Failed;
    }
    const int uid = info.uid;
    const bool firstWin = !profile.record(uid).completed;
    // The original seeds rand() from the clock at start-up (Engine::initEngine).
    const game::Level levelData = *level;
    auto session = std::make_unique<game::Session>(game::Level(levelData), std::uint32_t(std::time(nullptr)),
                                                   game::Tuning{}, profile.strength, profile.record(uid).completed);
    session->setRecord(profile.record(uid));
    session->setCheeseBalance(profile.cheeseBalance());
    session->setDossiersFound(profile.dossiers);
    session->setKiskaOffers(profile.kiska, std::int64_t(std::time(nullptr)));
    profile.strength = std::max(profile.strength, info.trailLength);
    bool recorded = false;  // this run's completion is in the profile
    if (won) *won = false;
    // Sound: silent when no output device opens.
    platform::Audio audio;
    audio.open();
    app::SoundOutput sound(files, audio);
    if (session->keepsMusic()) {
        // A boss fight starts no music: the map's goes on (world 1's, WorldMap::showScene).
        game::SoundEvent music;
        music.kind = game::SoundEvent::Music;
        music.file = "sound/music/CM_Menu1.m4a";
        music.loop = true;
        sound.play({music});
    }

    TickClock clock;
    for (;;) {
        for (const platform::Event& e : host.pollEvents()) {
            const game::Vec2 p{e.x, e.y};
            switch (e.kind) {
            case platform::Event::Quit: return LevelExit::Quit;
            case platform::Event::Key:
                if (e.key == platform::Event::Escape) return LevelExit::Back;
                break;
            case platform::Event::PointerDown: {
                const std::string button = view.buttonAt(*session, p);
                if (session->results().active() && button.empty()) session->touchResults();
                if (touchesLevel(*session, button)) session->fingerDown(p);
                break;
            }
            case platform::Event::PointerMove:
                if (touchesLevel(*session, view.buttonAt(*session, p))) session->fingerMove(p);
                break;
            case platform::Event::PointerUp: {
                const std::string button = view.buttonAt(*session, p);
                releaseButton(*session, button);
                if (touchesLevel(*session, button)) session->fingerUp(p);
                break;
            }
            default: break;
            }
        }
        if (session->retryRequested()) {
            // SceneGame::restartGame: everything stops, the level starts again at once.
            sound.play(sound.filter(session->takeSoundEvents(), session->clockMs()));
            if (session->keepsMusic()) sound.stopEffects();
            else sound.stopAll();
            session = session->retry();
            session->setRecord(profile.record(uid));
            session->setCheeseBalance(profile.cheeseBalance());
            session->setDossiersFound(profile.dossiers);
            recorded = false;
        }
        std::uint64_t now = host.milliseconds();
        for (int step : clock.advance(now)) {
            session->tick(step);
            if (session->retryRequested() || session->exitRequested()) break;
        }
        sound.play(sound.filter(session->takeSoundEvents(), session->clockMs()));
        if (const int dossier = session->takeFoundDossier(); dossier >= 0) {
            profile.foundDossier(dossier);
            profile.save(profileFile);
        }
        if (!(session->kiskaOffers() == profile.kiska)) {
            // Kiska's offer has shown (or been counted): the profile keeps that at once.
            profile.kiska = session->kiskaOffers();
            profile.save(profileFile);
        }
        if (session->exitRequested()) {
            // SceneWorldCompleteResults::guiEvent @0x2187c8: a boss fight won for the first time
            // moves the profile on to the next world (+0x18, up to the sixth).
            if (session->worldComplete().active() && firstWin && info.world < 5) {
                profile.world = info.world + 1;
                profile.save(profileFile);
                return LevelExit::NextWorld;
            }
            return LevelExit::Map;
        }
        if (session->retryRequested()) {
            // From the pause menu: the level again once the iris has shut.
            if (session->keepsMusic()) sound.stopEffects();
            else sound.stopAll();
            session = session->retry();
            session->setRecord(profile.record(uid));
            session->setCheeseBalance(profile.cheeseBalance());
            session->setDossiersFound(profile.dossiers);
            recorded = false;
            sound.play(sound.filter(session->takeSoundEvents(), session->clockMs()));
        }
        if (session->worldComplete().active() && !recorded) {
            // A world's boss fight won for the first time: recorded as the card appears, and the
            // card counts the world's goals after that (updateDynamicLabels).
            profile.complete(table, uid, session->recordedScore(), session->goalsWon(), session->progress().foundSecret);
            for (std::size_t k = 0; k < 5; ++k) profile.cheese[k] += session->progress().deliveredKinds[k];
            profile.currentLevel = uid;
            profile.save(profileFile);
            recorded = true;
            if (won) *won = true;
            int goalsWon = 0, goalsTotal = 0;
            for (const data::LevelInfo& l : table.levels()) {
                if (l.world != info.world) continue;
                const game::LevelRecord r = profile.record(l.uid);
                for (std::size_t g = 0; g < l.goals.size() && g < 3; ++g) {
                    if (l.goals[g].type.empty()) continue;  // (no goal kind: -1)
                    ++goalsTotal;
                    if (r.goals[g]) ++goalsWon;
                }
            }
            session->setWorldGoals(goalsWon, goalsTotal);
        }
        if (session->results().active() && !recorded) {
            // Profile::completeLevel runs as the results are asked for.
            profile.complete(table, uid, session->recordedScore(), session->goalsWon(), session->progress().foundSecret);
            for (std::size_t k = 0; k < 5; ++k) profile.cheese[k] += session->progress().deliveredKinds[k];
            profile.currentLevel = uid;
            profile.save(profileFile);
            recorded = true;
            if (won) *won = true;
        }
        if (session->results().done()) return LevelExit::Map;
        // Drawing changes some game state (Session::prepareFrame).
        session->prepareFrame();
        host.beginFrame();
        view.draw(host, *session);
        host.endFrame();
    }
}

// The game from the launcher's Play (app/game_flow.hpp: the very first time straight into 1-1,
// then the map, levels and the way between them). Escape returns to the launcher; returns
// false when the window closed.
bool runGame(platform::Host& host, const data::GameFiles& files) {
    platform::Audio audio;
    audio.open();  // silent when no output device opens
    app::GameFlow flow(files, platform::Host::dataDirectory() / "progress.txt", audio);
    std::string error;
    if (!flow.begin(error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return true;
    }
    TickClock clock;
    for (;;) {
        for (const platform::Event& e : host.pollEvents()) {
            switch (e.kind) {
            case platform::Event::Quit: return false;
            case platform::Event::Key:
                if (e.key == platform::Event::Escape) return true;
                break;
            case platform::Event::PointerDown: flow.touch({app::GameFlow::Touch::Down, e.x, e.y}); break;
            case platform::Event::PointerMove: flow.touch({app::GameFlow::Touch::Move, e.x, e.y}); break;
            case platform::Event::PointerUp: flow.touch({app::GameFlow::Touch::Up, e.x, e.y}); break;
            default: break;
            }
        }
        for (int step : clock.advance(host.milliseconds())) {
            (void)step;
            flow.tick();
            if (flow.left() || !flow.failure().empty()) break;
        }
        if (flow.left()) return true;
        if (!flow.failure().empty()) {
            std::fprintf(stderr, "%s\n", flow.failure().c_str());
            return true;
        }
        host.beginFrame();
        flow.draw(host);
        host.endFrame();
    }
}

// The app: the launcher (import, Play), then the game; Escape in the game returns here.
int runLauncher(const std::filesystem::path& importFirst) {
    std::string error;
    auto host = platform::Host::create("DoubleOCheese", error);
    if (!host) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    const std::filesystem::path data = platform::Host::dataDirectory();
    app::Launcher launcher(*host, data);
    if (!importFirst.empty()) launcher.startImport(importFirst);
    for (;;) {
        app::Launcher::Action action = app::Launcher::Action::None;
        for (const platform::Event& e : host->pollEvents(true))
            if (auto a = launcher.handle(e); a != app::Launcher::Action::None) action = a;
        if (auto a = launcher.update(); a != app::Launcher::Action::None) action = a;
        if (action == app::Launcher::Action::Quit) return 0;
        if (action == app::Launcher::Action::Play) {
            auto files = data::GameFiles::open(importer::gameDirectory(data), error);
            if (!files) {
                std::fprintf(stderr, "%s\n", error.c_str());
            } else if (!runGame(*host, *files)) {
                return 0;
            }
            launcher.readProgress();  // (the game saved as it went)
        }
        host->beginFrame();
        launcher.draw();
        host->endFrame();
    }
}

// Headless check of quick saves through the whole game flow (map, levels, results, sound):
// `--flow-check PROFILE SCRIPT FIRST TICKS` plays from a copy of the progress file PROFILE, with
// a harness script's touches (frame FIRST is the flow's first tick), for TICKS ticks. Every
// DOC_QUICKSAVE=K ticks (default 1) the flow is saved and restored into a new one that plays
// on, while an uninterrupted twin plays the same touches; their saved states must stay equal.
int flowCheck(const data::GameFiles& files, const char* profile, const char* script, int first, int ticks) {
    const int every = std::getenv("DOC_QUICKSAVE") ? std::max(1, std::atoi(std::getenv("DOC_QUICKSAVE"))) : 1;
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "doc-flow-check";
    std::filesystem::create_directories(dir);
    const std::filesystem::path mainFile = dir / "main.txt", twinFile = dir / "twin.txt";
    for (const auto& f : {mainFile, twinFile}) {
        std::filesystem::remove(f);
        if (*profile) std::filesystem::copy_file(profile, f);
    }
    platform::Audio audio;
    audio.openPull();
    constexpr std::int64_t kClock = 1700000000;  // (both flows seed their scenes alike)
    auto make = [&](const std::filesystem::path& file) {
        auto flow = std::make_unique<app::GameFlow>(files, file, audio);
        flow->setClock(kClock);
        return flow;
    };
    auto flow = make(mainFile), twin = make(twinFile);
    std::string error;
    if (!flow->begin(error) || !twin->begin(error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    struct Touch { int frame; std::string kind; float x, y; };
    std::vector<Touch> touches;
    std::ifstream in(script);
    for (std::string line; std::getline(in, line);) {
        std::istringstream ss(line);
        Touch t{};
        if ((ss >> t.frame >> t.kind) && (t.kind == "down" || t.kind == "move" || t.kind == "up") && (ss >> t.x >> t.y))
            touches.push_back(t);
    }
    int loads = 0;
    std::size_t largest = 0;
    double slowestMs = 0;
    for (int tick = 0; tick < ticks; ++tick) {
        for (const Touch& t : touches) {
            if (t.frame != first + tick - 1) continue;
            const auto kind = t.kind == "down" ? app::GameFlow::Touch::Down
                              : t.kind == "move" ? app::GameFlow::Touch::Move : app::GameFlow::Touch::Up;
            flow->touch({kind, t.x, t.y});
            twin->touch({kind, t.x, t.y});
        }
        flow->tick();
        twin->tick();
        if (tick % every == 0) {
            const std::vector<std::uint8_t> bytes = flow->saveState();
            const auto started = std::chrono::steady_clock::now();
            auto loaded = make(mainFile);
            if (!loaded->restore(bytes, error)) {
                std::printf("flow check: loading at tick %d failed: %s\n", tick, error.c_str());
                return 1;
            }
            largest = std::max(largest, bytes.size());
            const std::chrono::duration<double, std::milli> took = std::chrono::steady_clock::now() - started;
            slowestMs = std::max(slowestMs, took.count());
            flow = std::move(loaded);
            ++loads;
        }
        if (flow->saveState() != twin->saveState()) {
            std::printf("flow check: first difference at tick %d (after %d loads)\n", tick, loads);
            return 1;
        }
        if (flow->left() || !flow->failure().empty()) break;
    }
    std::printf("flow check: %d ticks, %d loads, states equal (largest save %zu bytes, slowest load %.1f ms); %s %s\n",
                ticks, loads, largest, slowestMs, flow->session() ? "in level" : "on the map",
                flow->session() ? flow->session()->level().info().number.c_str() : "");
    return 0;
}

// Command-line import into the per-user data folder.
int importOnly(const std::filesystem::path& ipa) {
    std::string error;
    int lastPercent = -1;
    const bool ok = importer::importIpa(
        ipa, platform::Host::dataDirectory(),
        [&](double fraction, const std::string& step) {
            const int percent = int(fraction * 100);
            if (percent / 10 != lastPercent / 10) std::printf("%3d%% %s\n", percent, step.c_str());
            lastPercent = percent;
        },
        nullptr, error);
    if (!ok) std::fprintf(stderr, "%s\n", error.c_str());
    else std::printf("imported into %s\n", importer::gameDirectory(platform::Host::dataDirectory()).string().c_str());
    return ok ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    doc::game::flushDenormalsToZero();
    // No arguments (or the arguments macOS passes to an app): the launcher.
    if (argc < 2 || std::strncmp(argv[1], "-psn", 4) == 0) return runLauncher({});
    if (!std::strcmp(argv[1], "--import")) {
        if (argc < 3) {
            std::fprintf(stderr, "usage: doubleocheese --import FILE.ipa\n");
            return 2;
        }
        return importOnly(argv[2]);
    }
    if (std::filesystem::path(argv[1]).extension() == ".ipa") return runLauncher(argv[1]);
    if (!std::strcmp(argv[1], "--preview-launcher") && argc > 2) {
        // Headless picture of the launcher as it would open now (layout checks).
        std::string error;
        auto host = platform::Host::createOffscreen(error);
        if (!host) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
        app::Launcher launcher(*host, platform::Host::dataDirectory());
        host->beginFrame();
        launcher.draw();
        host->endFrame();
        return host->saveFrame(argv[2]) ? 0 : 1;
    }

    // Research form: play from an unpacked app folder.
    std::string error;
    auto files = data::GameFiles::open(argv[1], error);
    if (!files) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    const std::string number = argc > 2 ? argv[2] : "1-1";
    // --res DIR: files replacing the bundle's (the original's Library/Caches/res; test levels).
    for (int i = 3; i + 1 < argc; ++i)
        if (!std::strcmp(argv[i], "--res")) files->setOverrideDir(argv[i + 1]);
    for (int i = 2; i < argc; ++i)
        if (!std::strcmp(argv[i], "--map")) return mapReplay(*files, argc, argv);
    if (argc > 6 && !std::strcmp(argv[2], "--flow-check"))
        return flowCheck(*files, argv[3], argv[4], std::atoi(argv[5]), std::atoi(argv[6]));
    for (int i = 3; i < argc; ++i)
        if (!std::strcmp(argv[i], "--replay")) {
            auto table = data::LevelTable::load(*files, error);
            const data::LevelInfo* info = table ? table->byNumber(number) : nullptr;
            auto level = info ? game::Level::load(*files, *info, error) : std::nullopt;
            if (!level) {
                std::fprintf(stderr, "level %s: %s\n", number.c_str(), error.c_str());
                return 1;
            }
            return replay(*files, std::move(*level), argc, argv);
        }
    auto host = platform::Host::create("DoubleOCheese", error);
    if (!host) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    auto table = data::LevelTable::load(*files, error);
    const data::LevelInfo* info = table ? table->byNumber(number) : nullptr;
    if (!info) {
        std::fprintf(stderr, "level %s not found\n", number.c_str());
        return 1;
    }
    game::Profile profile;
    const std::filesystem::path profileFile = platform::Host::dataDirectory() / "progress.txt";
    profile.load(profileFile);
    return playLevel(*host, *files, *table, profile, profileFile, *info) == LevelExit::Failed ? 1 : 0;
}
