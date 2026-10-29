// One running game on its own thread. See game_session.hpp.
#include "shell/game_session.hpp"
#include "graphics/ctr3d/raster.hpp"
#include "menus/a1_bonus.hpp"
#include "menus/a1_hotel.hpp"
#include "menus/a1_painting.hpp"
#include "menus/a1_robot_dog.hpp"
#include "minigames/a3_book.hpp"
#include "minigames/a3_car.hpp"
#include "minigames/a3_parrot.hpp"
#include "minigames/a4_minigames.hpp"
#include "platform/host.hpp"
#include "puzzles/a1_puzzle.hpp"
#include "puzzles/a3_puzzle_loop.hpp"
#include "puzzles/a4_puzzle.hpp"
#include "puzzles/a5_puzzle.hpp"
#include "shell/audio_output.hpp"
#include "shell/minigames.hpp"
#include "sound/a1_sound.hpp"
#include "sound/a3_sound.hpp"
#include "sound/a4_sound.hpp"
#include "sound/a56_sound.hpp"
#include "story/a1_event.hpp"
#include "story/a1_story.hpp"
#include "story/a2_game.hpp"
#include "story/a3_loops.hpp"
#include "story/a3_story.hpp"
#include "story/a4_game.hpp"
#include "story/a5_story.hpp"
#include "system/a2_checkpoint.hpp"
#include "system/a5_game.hpp"
#include "system/a5_saves.hpp"
#include <cmath>

namespace picarat::shell {

namespace {
// Thrown in the game thread to end it (window closed, back to the library).
struct Stop {};

// Thrown when a puzzle started from the collection has ended.
struct ReturnToLibrary {};

// The third adventure: an event waiting on time, i.e. its script thread is
// in one of the shared timed functions of evt/lt3_common - WAIT_FRAME (a
// frame count) or a BRIGHT..._FADE (until the fade is done). Talk, which
// waits for a tap, is never one of them.
bool timedWait(adventure3::Story& story) {
    const adventure3::Loop* top = story.top();
    if (!top || top->name() != "EVENT" || !top->scene()) {
        return false;
    }
    const lscr::Thread* thread = top->scene()->mainThread();
    const lscr::Frame* frame = thread ? thread->top() : nullptr;
    if (!frame || !frame->section) {
        return false;
    }
    const std::string& name = frame->section->name;
    return name == "WAIT_FRAME" || name.rfind("BRIGHT", 0) == 0;
}

// Collection sessions of the third adventure: the game's flags and variables
// (hint coins, picarat total...) and the puzzles' records last while the app
// runs; they are not a save game.
std::mutex collectionMutex;
std::unique_ptr<lscr::GameState> collectionState;
std::map<unsigned, adventure3::PuzzleRecord> records;
// The first adventure's collection state (hint coins, picarats, the
// puzzles' progress bytes), for the app's run as well; a new game starts
// with 10 hint coins (measured).
std::unique_ptr<adventure1::PuzzleState> adventure1State;
// The fourth adventure's puzzle records (state, wrong answers, hints) for
// the app's run.
std::map<int, adventure4::PuzzleSession::Progress> adventure4Records;

// The 3DS accelerometer (in G) for the host's tilt and shake. Flat and
// still the console reads (0, -1, 0); x grows as it tips left and z as it
// tips away (the tilt readings below). With a real accelerometer (phones)
// the device stands in for the console, measured from the way it was held
// when the game started (`rest`, the first reading), as if that were
// lying flat. Without one the keys tilt it up to about 45 degrees. A shake
// (the S key) swings x back and forth through zero, 1.2 G each way over
// twelve game frames: the sixth adventure's shake puzzles count changes in
// the reading's size (126: |x| by 0.5 then 0.3 within 60 frames; 037: the
// vector's length by 0.3 within 180), five in a row.
std::array<float, 3> consoleAcceleration(const HostInput& in, std::uint64_t frame,
                                         std::optional<std::array<float, 3>>& rest) {
    std::array<float, 3> a;
    if (in.motion) {
        if (!rest) {
            rest = *in.motion;
        }
        // The rotation taking the rest direction to the screen's normal
        // (0, 0, 1), applied to the reading (Rodrigues).
        const auto& r = *rest;
        const float n = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
        std::array<float, 3> u = *in.motion;
        if (n > 0.1f) {
            const float ux = r[0] / n, uy = r[1] / n, uz = r[2] / n;
            // axis = u x (0,0,1) = (uy, -ux, 0); cos = uz.
            const float kx = uy, ky = -ux, s = std::sqrt(kx * kx + ky * ky), c = uz;
            if (s > 1e-4f) {
                const float ax = kx / s, ay = ky / s;
                const std::array<float, 3> v = u;
                const float dot = ax * v[0] + ay * v[1];
                // v cos + (k x v) sin + k (k.v)(1 - cos), k = (ax, ay, 0).
                const std::array<float, 3> cross{ay * v[2], -ax * v[2], ax * v[1] - ay * v[0]};
                for (int i = 0; i < 3; ++i) {
                    const float k = i == 0 ? ax : i == 1 ? ay : 0.0f;
                    u[std::size_t(i)] =
                        v[std::size_t(i)] * c + cross[std::size_t(i)] * s + k * dot * (1 - c);
                }
            } else if (c < 0) {
                u = {-u[0], u[1], -u[2]}; // held face down: half a turn about y
            }
        }
        a = {u[0], -u[2], -u[1]};
    } else {
        const float x = -0.7f * in.tiltX, z = -0.7f * in.tiltY;
        a = {x, -std::sqrt(std::max(0.0f, 1.0f - x * x - z * z)), z};
    }
    if (in.shake) {
        a[0] += 1.2f * std::sin(float(frame % 12) * (2.0f * 3.14159265f / 12.0f));
    }
    return a;
}
} // namespace

// What the window's thread and the game thread share. Guarded by `mutex`;
// the game thread waits on `clock` for the next 60 Hz tick.
struct GameSession::Shared {
    mutable std::mutex mutex;
    std::condition_variable clock;
    bool stopped = false, tick = false, returnToLibrary = false;
    bool puzzleSolved = false; // a collection puzzle was solved
    // The host's skip request (system/game_types.hpp), held while the game
    // reports skippable and dropped as soon as it does not.
    bool skipping = false;
    SceneKind skipScene{};            // the kind of scene the request was made in
    std::vector<std::uint8_t> result; // a minigame's progress, once it ends
    bool sleeping = false;            // the window is minimised: the DS lid is closed
    Bytes pixels;
    ScreenGeometry geometry; // the screens' sizes (the DS's by default)
    GameSnapshot state{SceneKind::Logos, {}, 0};
    std::deque<HostInput> events; // stylus and name input, one per frame
    HostInput input;              // the held state and one-frame requests
    std::string error;
};

GameSession::GameSession(std::filesystem::path assets, std::filesystem::path checkpoint, int puzzle,
                         unsigned movieEvent)
    : GameSession(std::move(assets), std::move(checkpoint), puzzle,
                  [movieEvent](Game& game, Shared&) {
                      if (movieEvent) {
                          game.replayMovie(movieEvent);
                          throw ReturnToLibrary{};
                      }
                      game.startAtTitle = true; // the app's boot: no logos
                      game.run();
                  }) {}

std::unique_ptr<GameSession> GameSession::adventure2Continue(std::filesystem::path assets,
                                                             std::filesystem::path checkpoint) {
    return std::unique_ptr<GameSession>(
        new GameSession(std::move(assets), std::move(checkpoint), 0, [](Game& game, Shared&) {
            game.startAtTitle = true;
            game.continueCheckpoint = true;
            game.run();
        }));
}

std::unique_ptr<GameSession> GameSession::adventure2Minigame(std::filesystem::path assets,
                                                             std::filesystem::path checkpoint,
                                                             std::uint8_t scene,
                                                             std::vector<std::uint8_t> progress) {
    return std::unique_ptr<GameSession>(new GameSession(
        std::move(assets), std::move(checkpoint), 0, [scene, progress](Game& game, Shared& shared) {
            SaveProgress block;
            std::copy_n(progress.begin(), std::min(progress.size(), block.bytes.size()),
                        block.bytes.begin());
            struct Result { // handed over however the minigame ends
                Shared& shared;
                const SaveProgress& block;
                ~Result() {
                    std::lock_guard lock(shared.mutex);
                    shared.result.assign(block.bytes.begin(), block.bytes.end());
                }
            } result{shared, block};
            game.playMinigame(scene, block);
            throw ReturnToLibrary{};
        }));
}

GameSession::GameSession(std::filesystem::path assets, std::filesystem::path checkpoint, int puzzle,
                         std::function<void(Game&, Shared&)> body)
    : shared_(std::make_shared<Shared>()) {
    if (puzzle > 0) {
        shared_->input.collection = puzzle;
    }
    worker_ = std::thread([shared = shared_, assets, checkpoint, puzzle, body] {
        try {
            AudioOutput audio(assets);
            CheckpointStore saves(checkpoint);
            int wake = 0;
            Game game(
                assets,
                [shared, &audio, puzzle, &wake](const Surface& image, const GameSnapshot& state) {
                    if (puzzle > 0 && state.scene == SceneKind::Collection) {
                        throw ReturnToLibrary{};
                    }
                    audio.check();
                    auto bytes = rgba(image);
                    std::unique_lock lock(shared->mutex);
                    shared->pixels = std::move(bytes);
                    shared->state = state;
                    if (!state.skippable || state.scene != shared->skipScene) {
                        shared->skipping = false; // over, or something else plays now
                    }
                    if (puzzle > 0 && state.scene == SceneKind::PuzzleSolved) {
                        shared->puzzleSolved = true;
                    }
                    // ov2 0206aebc: on lid close, once no stream stop is pending and
                    // the BGM is settled (0207f760), pause the sound (0207f6f4) and
                    // sleep; after waking wait two frames, then resume (0207f724).
                    // The movie scene handles the lid itself (HostInput::sleep).
                    if (shared->sleeping && !wake && state.scene != SceneKind::Movie &&
                        audio.sequenced()->canSleep()) {
                        audio.sequenced()->pauseSound();
                        shared->clock.wait(lock,
                                           [&] { return !shared->sleeping || shared->stopped; });
                        if (shared->stopped) {
                            throw Stop{};
                        }
                        wake = 3;
                    }
                    if (wake && --wake == 0) {
                        audio.sequenced()->resumeSound();
                    }
                    shared->clock.wait(lock, [&] { return shared->tick || shared->stopped; });
                    if (shared->stopped) {
                        throw Stop{};
                    }
                    shared->tick = false;
                    // One queued stylus or keyboard event per frame, so quick
                    // taps and typed names are never merged.
                    if (!shared->events.empty()) {
                        const auto event = shared->events.front();
                        shared->events.pop_front();
                        if (event.name.action != NameInput::Action::None) {
                            if (state.scene == SceneKind::Name) {
                                shared->input.name = event.name;
                            }
                        } else {
                            shared->input.down = event.down;
                            shared->input.x = event.x;
                            shared->input.y = event.y;
                        }
                    }
                    auto input = shared->input;
                    input.skip = shared->skipping;
                    input.sleep = shared->sleeping && state.scene == SceneKind::Movie;
                    // Requests last one frame.
                    shared->input.name = {};
                    shared->input.restart = false;
                    shared->input.resume = false;
                    shared->input.skip = false;
                    shared->input.puzzle = false;
                    shared->input.load = false;
                    shared->input.collection = 0;
                    return input;
                },
                &audio, &saves);
            body(game, *shared);
        } catch (const Stop&) {
        } catch (const ReturnToLibrary&) {
            std::lock_guard lock(shared->mutex);
            shared->returnToLibrary = true;
        } catch (const std::exception& error) {
            std::lock_guard lock(shared->mutex);
            shared->error = error.what();
        }
    });
}

std::unique_ptr<GameSession> GameSession::adventure3Puzzle(std::filesystem::path pack, int number) {
    std::unique_ptr<GameSession> session(new GameSession());
    session->shared_ = std::make_shared<Shared>();
    session->worker_ = std::thread([shared = session->shared_, pack, number] {
        try {
            adventure3::Assets assets(pack);
            GameText text(pack);
            std::lock_guard collection(collectionMutex);
            if (!collectionState) {
                collectionState = std::make_unique<lscr::GameState>();
                collectionState->declare(assets.script("rc/ge/lt3_rc/lt3_gmd.lbin"));
            }
            // The third adventure's sound: the same driver and sound object
            // as the second's, its files where this game keeps them.
            AudioOutput audio(picarat::SoundLayout::adventure3(pack));
            picarat::Adventure3Sound sound(*audio.sequenced(), adventure3::soundSets(assets));
            auto& record = records[unsigned(number)];
            const bool solvedBefore = record.state == 2;
            adventure3::PuzzleLoop loop(assets, text, *collectionState, unsigned(number), record,
                                        &sound);
            loop.setBrightness(-lscr::fx::kOne);
            bool held = false;
            std::uint32_t tick = 0;
            while (!loop.finished()) {
                GameSnapshot snap{SceneKind::Puzzle, {}, tick++};
                snap.detail = number;
                const auto bytes = rgba(loop.render());
                HostInput input;
                {
                    std::unique_lock lock(shared->mutex);
                    shared->pixels = bytes;
                    shared->state = snap;
                    if (!snap.skippable || snap.scene != shared->skipScene) {
                        shared->skipping = false; // over, or something else plays now
                    }
                    if (shared->sleeping) {
                        shared->clock.wait(lock,
                                           [&] { return !shared->sleeping || shared->stopped; });
                    }
                    shared->clock.wait(lock, [&] { return shared->tick || shared->stopped; });
                    if (shared->stopped) {
                        throw Stop{};
                    }
                    shared->tick = false;
                    if (!shared->events.empty()) {
                        const auto event = shared->events.front();
                        shared->events.pop_front();
                        if (event.name.action == NameInput::Action::None) {
                            shared->input.down = event.down;
                            shared->input.x = event.x;
                            shared->input.y = event.y;
                        }
                    }
                    input = shared->input;
                }
                lscr::SceneInput in;
                in.held = input.down && input.x >= 0;
                in.pressed = in.held && !held;
                in.released = !in.held && held;
                held = in.held;
                if (in.held) {
                    in.x = input.x;
                    in.y = input.y;
                }
                loop.frame(in);
                audio.check();
            }
            std::lock_guard lock(shared->mutex);
            shared->returnToLibrary = true;
            shared->puzzleSolved = record.state == 2 && !solvedBefore;
        } catch (const Stop&) {
        } catch (const std::exception& error) {
            std::lock_guard lock(shared->mutex);
            shared->error = error.what();
        }
    });
    return session;
}

std::unique_ptr<GameSession> GameSession::adventure4Puzzle(std::filesystem::path pack, int id) {
    std::unique_ptr<GameSession> session(new GameSession());
    session->shared_ = std::make_shared<Shared>();
    session->worker_ = std::thread([shared = session->shared_, pack, id] {
        try {
            adventure4::Assets assets(pack);
            std::lock_guard collection(collectionMutex);
            auto& record = adventure4Records[id];
            const bool solvedBefore = record.state == 2;
            // The fourth adventure's sound: the shared driver and sound object
            // with this game's files, its sound manager's requests on top.
            AudioOutput audio(picarat::SoundLayout::adventure4(pack));
            picarat::Adventure4Sound sound(*audio.sequenced(), adventure4::soundSets(assets));
            adventure4::PuzzleSession puzzle(assets, id, true, record, &sound);
            Surface image;
            std::uint32_t tick = 0;
            while (!puzzle.finished()) {
                GameSnapshot snap{SceneKind::Puzzle, {}, tick++};
                snap.detail = id;
                puzzle.render(image);
                const auto bytes = rgba(image);
                HostInput input;
                {
                    std::unique_lock lock(shared->mutex);
                    shared->pixels = bytes;
                    shared->state = snap;
                    if (shared->sleeping) {
                        shared->clock.wait(lock,
                                           [&] { return !shared->sleeping || shared->stopped; });
                    }
                    shared->clock.wait(lock, [&] { return shared->tick || shared->stopped; });
                    if (shared->stopped) {
                        throw Stop{};
                    }
                    shared->tick = false;
                    if (!shared->events.empty()) {
                        const auto event = shared->events.front();
                        shared->events.pop_front();
                        if (event.name.action == NameInput::Action::None) {
                            shared->input.down = event.down;
                            shared->input.x = event.x;
                            shared->input.y = event.y;
                        }
                    }
                    input = shared->input;
                }
                const bool down = input.down && input.x >= 0;
                puzzle.frame(down, down ? input.x : 0, down ? input.y : 0);
                sound.sound().frame();
                audio.check();
                record = puzzle.progress();
            }
            record = puzzle.progress();
            std::lock_guard lock(shared->mutex);
            shared->returnToLibrary = true;
            shared->puzzleSolved = record.state == 2 && !solvedBefore;
        } catch (const Stop&) {
        } catch (const std::exception& error) {
            std::lock_guard lock(shared->mutex);
            shared->error = error.what();
        }
    });
    return session;
}

std::unique_ptr<GameSession> GameSession::adventure5Story(std::filesystem::path pack,
                                                          std::filesystem::path saves) {
    std::unique_ptr<GameSession> session(new GameSession());
    session->shared_ = std::make_shared<Shared>();
    session->shared_->geometry = ScreenGeometry::ctr();
    session->worker_ = std::thread([shared = session->shared_, pack, saves] {
        try {
            adventure5::Game game(pack.string(), adventure5::Edition::Fifth);
            game.saves = std::make_shared<adventure5::SaveSlots>(saves); // (slot0..2.sav)
            adventure56::SoundPlayer sound(pack / "snd");
            game.sound = &sound;
            platform::AudioStream device(
                adventure56::SoundPlayer::kRate,
                [&sound](std::int16_t* stereo, int frames) { sound.render(stereo, frames); });
            adventure5::Story story(game);
            const ScreenGeometry g = ScreenGeometry::ctr();
            ctr3d::Target top(g.topWidth, g.topHeight), bottom(g.bottomWidth, g.bottomHeight);
            Bytes bytes(std::size_t(g.frameWidth()) * std::size_t(g.frameHeight()) * 4);
            std::uint32_t tick = 0;
            int displayFrames = 0;
            std::optional<std::array<float, 3>> motionRest; // consoleAcceleration
            for (;;) {
                // What the host shows about the scene: the top loop.
                const adventure5::Loop* loop = story.top();
                const int id = loop ? loop->id : 0;
                GameSnapshot snap{id >= 3000 && id < 3300        ? SceneKind::Puzzle
                                  : id == adventure5::NameLoopId ? SceneKind::Name
                                                                 : SceneKind::Story,
                                  {},
                                  tick++};
                snap.detail = id;
                game.render(top, bottom);
                auto put = [&](const ctr3d::Target& t, int y0) {
                    for (int y = 0; y < t.height; ++y) {
                        for (int x = 0; x < t.width; ++x) {
                            const auto& c = t.colour[std::size_t(y * t.width + x)];
                            std::uint8_t* o =
                                bytes.data() + (std::size_t(y0 + y) * std::size_t(g.frameWidth()) +
                                                std::size_t(x)) *
                                                   4;
                            o[0] = c.r, o[1] = c.g, o[2] = c.b, o[3] = 255;
                        }
                    }
                };
                put(top, 0);
                put(bottom, g.topHeight);
                HostInput input;
                NameInput typed;
                const bool steps = displayFrames + 1 >= std::max(1, game.displayFramesPerFrame);
                {
                    std::unique_lock lock(shared->mutex);
                    shared->pixels = bytes;
                    shared->state = snap;
                    if (shared->sleeping) {
                        shared->clock.wait(lock,
                                           [&] { return !shared->sleeping || shared->stopped; });
                    }
                    shared->clock.wait(lock, [&] { return shared->tick || shared->stopped; });
                    if (shared->stopped) {
                        throw Stop{};
                    }
                    shared->tick = false;
                    // Input is taken only on frames the game steps.
                    if (steps && !shared->events.empty()) {
                        const auto event = shared->events.front();
                        shared->events.pop_front();
                        if (event.name.action == NameInput::Action::None) {
                            shared->input.down = event.down;
                            shared->input.x = event.x;
                            shared->input.y = event.y;
                        } else if (snap.scene == SceneKind::Name) {
                            typed = event.name; // the name entry's typed keys
                        }
                    }
                    input = shared->input;
                }
                adventure5::Input in;
                in.touching = input.down && input.x >= 0;
                in.x = in.touching ? input.x : 0;
                in.y = in.touching ? input.y : 0;
                in.buttons = input.pad & 15u; // left, right, up, down: the game's order
                in.tiltX = -input.tiltX;
                in.tiltZ = -input.tiltY;
                in.acceleration = consoleAcceleration(input, game.frame, motionRest);
                if (input.motion) {
                    // A real accelerometer tilts as the keys do at about 45 degrees.
                    in.tiltX = std::clamp(in.acceleration[0] / 0.7f, -1.0f, 1.0f);
                    in.tiltZ = std::clamp(in.acceleration[2] / 0.7f, -1.0f, 1.0f);
                }
                if (typed.action != NameInput::Action::None) {
                    story.keyboard(typed);
                }
                // The game steps once per that many display frames (2400).
                if (++displayFrames >= std::max(1, game.displayFramesPerFrame)) {
                    displayFrames = 0;
                    story.frame(in);
                }
            }
        } catch (const Stop&) {
        } catch (const std::exception& error) {
            std::lock_guard lock(shared->mutex);
            shared->error = error.what();
        }
    });
    return session;
}

std::unique_ptr<GameSession> GameSession::adventure5Puzzle(std::filesystem::path pack, int id,
                                                           bool sixth) {
    std::unique_ptr<GameSession> session(new GameSession());
    session->shared_ = std::make_shared<Shared>();
    session->shared_->geometry = ScreenGeometry::ctr();
    session->worker_ = std::thread([shared = session->shared_, pack, id, sixth] {
        try {
            adventure5::Game game(pack.string(),
                                  sixth ? adventure5::Edition::Sixth : adventure5::Edition::Fifth);
            // The sound: the player mixes on the audio thread.
            adventure56::SoundPlayer sound(pack / "snd");
            game.sound = &sound;
            platform::AudioStream device(
                adventure56::SoundPlayer::kRate,
                [&sound](std::int16_t* stereo, int frames) { sound.render(stereo, frames); });
            adventure5::PuzzleSession puzzle(game, id);
            const ScreenGeometry g = ScreenGeometry::ctr();
            ctr3d::Target top(g.topWidth, g.topHeight), bottom(g.bottomWidth, g.bottomHeight);
            Bytes bytes(std::size_t(g.frameWidth()) * std::size_t(g.frameHeight()) * 4);
            std::uint32_t tick = 0;
            int displayFrames = 0;
            std::optional<std::array<float, 3>> motionRest; // consoleAcceleration
            bool running = true;
            while (running) {
                GameSnapshot snap{SceneKind::Puzzle, {}, tick++};
                snap.detail = id;
                game.render(top, bottom);
                // Both screens into one frame: the upper above, the lower
                // below at the left.
                auto put = [&](const ctr3d::Target& t, int y0) {
                    for (int y = 0; y < t.height; ++y) {
                        for (int x = 0; x < t.width; ++x) {
                            const auto& c = t.colour[std::size_t(y * t.width + x)];
                            std::uint8_t* o =
                                bytes.data() + (std::size_t(y0 + y) * std::size_t(g.frameWidth()) +
                                                std::size_t(x)) *
                                                   4;
                            o[0] = c.r, o[1] = c.g, o[2] = c.b, o[3] = 255;
                        }
                    }
                };
                put(top, 0);
                put(bottom, g.topHeight);
                HostInput input;
                {
                    std::unique_lock lock(shared->mutex);
                    shared->pixels = bytes;
                    shared->state = snap;
                    if (shared->sleeping) {
                        shared->clock.wait(lock,
                                           [&] { return !shared->sleeping || shared->stopped; });
                    }
                    shared->clock.wait(lock, [&] { return shared->tick || shared->stopped; });
                    if (shared->stopped) {
                        throw Stop{};
                    }
                    shared->tick = false;
                    // Input is taken only on frames the game steps.
                    if (displayFrames + 1 >= std::max(1, game.displayFramesPerFrame) &&
                        !shared->events.empty()) {
                        const auto event = shared->events.front();
                        shared->events.pop_front();
                        if (event.name.action == NameInput::Action::None) {
                            shared->input.down = event.down;
                            shared->input.x = event.x;
                            shared->input.y = event.y;
                        }
                    }
                    input = shared->input;
                }
                adventure5::Input in;
                in.touching = input.down && input.x >= 0;
                in.x = in.touching ? input.x : 0;
                in.y = in.touching ? input.y : 0;
                in.buttons = input.pad & 15u; // left, right, up, down: the game's order
                // The sensor reads positive x when the console tips left and
                // positive z when it tips away (blocks of type 16 slide that
                // way; checked against the reference's motion input).
                in.tiltX = -input.tiltX;
                in.tiltZ = -input.tiltY;
                in.acceleration = consoleAcceleration(input, game.frame, motionRest);
                if (input.motion) {
                    // A real accelerometer tilts as the keys do at about 45 degrees.
                    in.tiltX = std::clamp(in.acceleration[0] / 0.7f, -1.0f, 1.0f);
                    in.tiltZ = std::clamp(in.acceleration[2] / 0.7f, -1.0f, 1.0f);
                }
                // 2400: the game steps once per that many display frames
                // (the verdict runs at 30 per second).
                if (++displayFrames >= std::max(1, game.displayFramesPerFrame)) {
                    displayFrames = 0;
                    running = puzzle.frame(in);
                }
            }
            // The verdict variable holds 1 after a correct answer.
            const bool solved = game.variable(adventure5::names::verdict) == 1;
            std::lock_guard lock(shared->mutex);
            shared->returnToLibrary = true;
            shared->puzzleSolved = solved;
        } catch (const Stop&) {
        } catch (const std::exception& error) {
            std::lock_guard lock(shared->mutex);
            shared->error = error.what();
        }
    });
    return session;
}

namespace {
// The fourth adventure's frames for a session: what the host is shown (the
// top loop's scene), the stylus, typed keys and the skip, until `after`
// (called after each frame) says the session is over.
template <typename SharedPtr, typename After>
void runAdventure4(const SharedPtr& shared, adventure4::Game& game, picarat::Adventure4Sound& sound,
                   AudioOutput& audio, After after) {
    Surface image;
    std::uint32_t tick = 0;
    for (;;) {
        // What the host shows about the scene: the top loop (films play
        // over any loop). The name keyboard (2300; 4260 the secret door's)
        // takes typed keys.
        const adventure4::Loop* top = game.top();
        const int id = top ? top->id : 0;
        const bool film = game.scene.film.state >= 2 && game.scene.film.state <= 3;
        GameSnapshot snap{film                       ? SceneKind::Movie
                          : id >= 3000 && id < 3300  ? SceneKind::Puzzle
                          : id == 2000               ? SceneKind::Title
                          : id == 2300 || id == 4260 ? SceneKind::Name
                                                     : SceneKind::Story,
                          {},
                          tick++};
        snap.skippable = game.scene.film.state == 3;
        snap.detail = id; // the top loop's number
        game.render(image);
        const auto bytes = rgba(image);
        HostInput input;
        NameInput typed;
        {
            std::unique_lock lock(shared->mutex);
            shared->pixels = bytes;
            shared->state = snap;
            if (shared->sleeping) {
                shared->clock.wait(lock, [&] { return !shared->sleeping || shared->stopped; });
            }
            shared->clock.wait(lock, [&] { return shared->tick || shared->stopped; });
            if (shared->stopped) {
                throw Stop{};
            }
            shared->tick = false;
            if (!shared->events.empty()) {
                const auto event = shared->events.front();
                shared->events.pop_front();
                if (event.name.action == NameInput::Action::None) {
                    shared->input.down = event.down;
                    shared->input.x = event.x;
                    shared->input.y = event.y;
                } else if (snap.scene == SceneKind::Name) {
                    typed = event.name;
                }
            }
            input = shared->input;
            input.skip = shared->skipping;
        }
        if (typed.action != NameInput::Action::None) {
            game.keyboard(typed);
        }
        const bool down = input.down && input.x >= 0;
        // The host's skip is the film's Start (the scripts let Start end a
        // skippable film; KEYINPUT bit 3), pressed anew every few frames
        // while the host asks.
        const std::uint16_t keys = input.skip && snap.skippable && tick % 8 < 2 ? 1u << 3 : 0;
        game.frame(down, down ? input.x : 0, down ? input.y : 0, keys);
        sound.sound().frame();
        audio.check();
        if (after()) {
            return;
        }
    }
}
} // namespace

std::unique_ptr<GameSession> GameSession::adventure4Story(std::filesystem::path pack,
                                                          std::filesystem::path saves) {
    std::unique_ptr<GameSession> session(new GameSession());
    session->shared_ = std::make_shared<Shared>();
    session->worker_ = std::thread([shared = session->shared_, pack, saves] {
        try {
            adventure4::Assets assets(pack);
            AudioOutput audio(picarat::SoundLayout::adventure4(pack));
            picarat::Adventure4Sound sound(*audio.sequenced(), adventure4::soundSets(assets));
            adventure4::Game game(assets, &sound, true, saves);
            game.startAtTitle = true; // the app's boot: no logos
            runAdventure4(shared, game, sound, audio, [] { return false; });
        } catch (const Stop&) {
        } catch (const std::exception& error) {
            std::lock_guard lock(shared->mutex);
            shared->error = error.what();
        }
    });
    return session;
}

std::unique_ptr<GameSession> GameSession::adventure4Minigame(std::filesystem::path pack, int which,
                                                             std::vector<std::uint8_t> progress) {
    std::unique_ptr<GameSession> session(new GameSession());
    session->shared_ = std::make_shared<Shared>();
    session->worker_ = std::thread([shared = session->shared_, pack, which, progress] {
        // The record goes back however the session ends.
        struct Deliveries {
            Shared& shared;
            std::vector<std::uint8_t> latest;
            ~Deliveries() {
                if (!latest.empty()) {
                    std::lock_guard lock(shared.mutex);
                    shared.result = latest;
                }
            }
        } deliveries{*shared, {}};
        try {
            adventure4::Assets assets(pack);
            AudioOutput audio(picarat::SoundLayout::adventure4(pack));
            picarat::Adventure4Sound sound(*audio.sequenced(), adventure4::soundSets(assets));
            // No game loop: the minigame's loop alone, as the bag pushes it.
            adventure4::Game game(assets, &sound, false);
            adventure4::applyMinigameRecord(game.data, progress);
            game.data.normalize();
            game.request(adventure4::kMinigameLoops[std::clamp(which, 0, 2)],
                         {-1, -1, -1, -1, -1, -1});
            deliveries.latest = adventure4::minigameRecord(game.data);
            runAdventure4(shared, game, sound, audio, [&] {
                deliveries.latest = adventure4::minigameRecord(game.data);
                if (!game.top() && !game.pending()) {
                    std::lock_guard lock(shared->mutex);
                    shared->returnToLibrary = true; // the minigame has ended
                    return true;
                }
                return false;
            });
        } catch (const Stop&) {
        } catch (const std::exception& error) {
            std::lock_guard lock(shared->mutex);
            shared->error = error.what();
        }
    });
    return session;
}

std::unique_ptr<GameSession> GameSession::adventure1Puzzle(std::filesystem::path pack, int id,
                                                           std::string language) {
    std::unique_ptr<GameSession> session(new GameSession());
    session->shared_ = std::make_shared<Shared>();
    session->worker_ = std::thread([shared = session->shared_, pack, id, language] {
        try {
            GraphicsAssets assets(pack);
            const adventure1::Catalogue catalogue(pack, language);
            std::lock_guard collection(collectionMutex);
            if (!adventure1State) {
                adventure1State = std::make_unique<adventure1::PuzzleState>();
                adventure1State->coins = 10;
            }
            auto& state = *adventure1State;
            const bool solvedBefore = state.puzzles.at(std::size_t(id)) & 2;
            std::uint32_t tick = 0;
            PresentFrame present = [&](const Surface& image, const GameSnapshot& snapshot) {
                const auto bytes = rgba(image);
                std::unique_lock lock(shared->mutex);
                shared->pixels = bytes;
                shared->state = snapshot;
                shared->state.tick = tick++;
                shared->state.detail = id;
                if (shared->sleeping) {
                    shared->clock.wait(lock, [&] { return !shared->sleeping || shared->stopped; });
                }
                shared->clock.wait(lock, [&] { return shared->tick || shared->stopped; });
                if (shared->stopped) {
                    throw Stop{};
                }
                shared->tick = false;
                if (!shared->events.empty()) {
                    const auto event = shared->events.front();
                    shared->events.pop_front();
                    if (event.name.action == NameInput::Action::None) {
                        shared->input.down = event.down;
                        shared->input.x = event.x;
                        shared->input.y = event.y;
                    }
                }
                HostInput input = shared->input;
                input.restart = input.puzzle = false; // not offered for this game yet
                return input;
            };
            // The game's sound, played by the device at the hardware's rate
            // (the device's thread renders, the game thread calls the sound
            // object; it locks itself). Without a device the frames clock it.
            picarat::Adventure1Sound sound(pack);
            std::unique_ptr<platform::AudioStream> device;
            try {
                device = std::make_unique<platform::AudioStream>(
                    32728, [&sound](std::int16_t* stereo, int frames) {
                        sound.render({stereo, std::size_t(frames) * 2});
                    });
            } catch (const std::exception&) {
                sound.clockWithFrames(true);
            }
            adventure1::PuzzlePlay play(assets, present, catalogue, language, unsigned(id), state,
                                        &sound);
            play.play();
            std::lock_guard lock(shared->mutex);
            shared->returnToLibrary = true;
            shared->puzzleSolved = (state.puzzles.at(std::size_t(id)) & 2) && !solvedBefore;
        } catch (const Stop&) {
        } catch (const std::exception& error) {
            std::lock_guard lock(shared->mutex);
            shared->error = error.what();
        }
    });
    return session;
}

std::unique_ptr<GameSession> GameSession::adventure1Story(std::filesystem::path pack,
                                                          std::filesystem::path backup,
                                                          std::filesystem::path checkpoint,
                                                          std::string language, bool resume) {
    return adventure1Session(std::move(pack), std::move(backup), std::move(language), 0, -1, {},
                             std::move(checkpoint), resume);
}

std::unique_ptr<GameSession> GameSession::adventure1Movie(std::filesystem::path pack,
                                                          std::string language, int movie) {
    return adventure1Session(std::move(pack), {}, std::move(language), movie);
}

std::unique_ptr<GameSession> GameSession::adventure1Minigame(std::filesystem::path pack,
                                                             std::string language, int which,
                                                             std::vector<std::uint8_t> progress) {
    return adventure1Session(std::move(pack), {}, std::move(language), 0, which,
                             std::move(progress));
}

std::unique_ptr<GameSession>
GameSession::adventure1Session(std::filesystem::path pack, std::filesystem::path backup,
                               std::string language, int movie, int minigame,
                               std::vector<std::uint8_t> progress, std::filesystem::path checkpoint,
                               bool resume) {
    std::unique_ptr<GameSession> session(new GameSession());
    session->shared_ = std::make_shared<Shared>();
    session->worker_ = std::thread([shared = session->shared_, pack, backup, language, movie,
                                    minigame, progress, checkpoint, resume] {
        try {
            GraphicsAssets assets(pack);
            std::uint32_t tick = 0;
            PresentFrame present = [&](const Surface& image, const GameSnapshot& snapshot) {
                const auto bytes = rgba(image);
                std::unique_lock lock(shared->mutex);
                shared->pixels = bytes;
                shared->state = snapshot;
                if (!snapshot.skippable || snapshot.scene != shared->skipScene) {
                    shared->skipping = false; // over, or something else plays now
                }
                shared->state.tick = tick++;
                if (shared->sleeping) {
                    shared->clock.wait(lock, [&] { return !shared->sleeping || shared->stopped; });
                }
                shared->clock.wait(lock, [&] { return shared->tick || shared->stopped; });
                if (shared->stopped) {
                    throw Stop{};
                }
                shared->tick = false;
                HostInput input = shared->input;
                // One queued stylus or keyboard event per frame; typed keys
                // only reach the name entry.
                if (!shared->events.empty()) {
                    const auto event = shared->events.front();
                    shared->events.pop_front();
                    if (event.name.action == NameInput::Action::None) {
                        shared->input.down = event.down;
                        shared->input.x = event.x;
                        shared->input.y = event.y;
                        input = shared->input;
                    } else if (snapshot.scene == SceneKind::Name) {
                        input.name = event.name;
                    }
                }
                input.skip = shared->skipping;
                input.restart = input.puzzle = false; // not offered for this game yet
                return input;
            };
            picarat::Adventure1Sound sound(pack);
            std::unique_ptr<platform::AudioStream> device;
            try {
                device = std::make_unique<platform::AudioStream>(
                    32728, [&sound](std::int16_t* stereo, int frames) {
                        sound.render({stereo, std::size_t(frames) * 2});
                    });
            } catch (const std::exception&) {
                sound.clockWithFrames(true);
            }
            adventure1::Story story(assets, present, pack, language, &sound);
            story.saveFile = backup;
            if (minigame >= 0) {
                // Free play: one of the trunk's pages on its own record,
                // handed back however the page is left.
                shell::Adventure1Minigames::apply(progress, story.state);
                struct Delivery {
                    Shared& shared;
                    const adventure1::GameState& state;
                    ~Delivery() {
                        std::lock_guard lock(shared.mutex);
                        shared.result = shell::Adventure1Minigames::capture(state);
                    }
                } delivery{*shared, story.state};
                story.kit.setBrightness(-16);
                if (minigame == 0) {
                    adventure1::robotDogPage(story);
                } else if (minigame == 1) {
                    adventure1::hotelPage(story);
                } else {
                    adventure1::paintingPage(story);
                }
                // Completed: its event, as in the story (the dog's naming
                // is left out).
                if (story.state.scene == adventure1::scene::Event) {
                    adventure1::EventScene event(story);
                    event.run();
                    event.finish();
                }
                std::lock_guard lock(shared->mutex);
                shared->returnToLibrary = true;
                return;
            }
            if (movie > 0) {
                adventure1::replayMovie(story, movie);
                std::lock_guard lock(shared->mutex);
                shared->returnToLibrary = true;
                return;
            }
            story.startAtTitle = true; // the app's boot: no logos
            story.checkpointFile = checkpoint;
            story.continueCheckpoint = resume;
            story.run();
        } catch (const Stop&) {
        } catch (const std::exception& error) {
            std::lock_guard lock(shared->mutex);
            shared->error = error.what();
        }
    });
    return session;
}

std::unique_ptr<GameSession>
GameSession::adventure3Story(std::filesystem::path pack, std::filesystem::path saves, bool resume) {
    return adventure3Session(std::move(pack), std::move(saves), 0, std::nullopt, 0, resume);
}

std::unique_ptr<GameSession> GameSession::adventure3Movie(std::filesystem::path pack, int movie) {
    return adventure3Session(std::move(pack), {}, movie, std::nullopt);
}

std::unique_ptr<GameSession> GameSession::adventure3Minigame(std::filesystem::path pack, int which,
                                                             std::vector<std::uint8_t> progress) {
    return adventure3Session(std::move(pack), {}, 0, std::move(progress), which);
}

std::unique_ptr<GameSession>
GameSession::adventure3Session(std::filesystem::path pack, std::filesystem::path saves, int movie,
                               std::optional<std::vector<std::uint8_t>> parrot, int which,
                               bool resume) {
    std::unique_ptr<GameSession> session(new GameSession());
    session->shared_ = std::make_shared<Shared>();
    session->worker_ =
        std::thread([shared = session->shared_, pack, saves, movie, parrot, which, resume] {
            try {
                adventure3::Assets assets(pack);
                GameText text(pack);
                AudioOutput audio(picarat::SoundLayout::adventure3(pack));
                picarat::Adventure3Sound sound(*audio.sequenced(), adventure3::soundSets(assets));
                adventure3::Story story(assets, text, &sound, saves, movie == 0 && !parrot);
                story.startAtTitle = true; // the app's boot: no logos
                // The story keeps the app's checkpoint beside its slots.
                story.checkpoints = movie == 0 && !parrot && !saves.empty();
                story.continueCheckpoint = resume;
                // A minigame free play hands back its progress however it ends.
                struct Deliveries {
                    Shared& shared;
                    std::vector<std::uint8_t> latest;
                    ~Deliveries() {
                        if (!latest.empty()) {
                            std::lock_guard lock(shared.mutex);
                            shared.result = latest;
                        }
                    }
                } deliveries{*shared, {}};
                // The free play's progress record (shell/minigames.hpp): the
                // parrot's 12 stages, the car's 10 courses, the 3 books, the
                // stickers' gaps.
                constexpr std::size_t kCar = 12, kBooks = 22;
                auto progress = [&] {
                    auto out = adventure3::parrotDelivered(story);
                    const auto car = adventure3::carCleared(story);
                    const auto book = adventure3::bookProgress(story);
                    out.insert(out.end(), car.begin(), car.end());
                    out.insert(out.end(), book.begin(), book.end());
                    return out;
                };
                if (parrot) {
                    // Everything open, as the bag offers it after the story
                    // (evt/prg/lt3_menu_bag: 1202 MINI_PARROT_MENU / MINI_CAR_SLCT
                    // / MINI_BOOK).
                    const auto& p = *parrot;
                    auto part = [&](std::size_t from, std::size_t n) {
                        std::vector<std::uint8_t> v(n, 0);
                        for (std::size_t i = 0; i < n && from + i < p.size(); ++i) {
                            v[i] = p[from + i];
                        }
                        return v;
                    };
                    adventure3::openParrotStages(story);
                    adventure3::setParrotDelivered(story, part(0, adventure3::kParrotStages));
                    adventure3::openCarCourses(story);
                    adventure3::setCarCleared(story, part(kCar, adventure3::kCarCourses));
                    adventure3::findAllStickers(story);
                    adventure3::setBookProgress(
                        story, part(kBooks, adventure3::kBooks +
                                                std::size_t(adventure3::bookStickerCount(story))));
                    deliveries.latest = progress();
                    static const char* const kLoops[] = {"MINI_PARROT_MENU", "MINI_CAR_SLCT",
                                                         "MINI_BOOK"};
                    story.request(adventure3::Story::Request::Push, kLoops[std::clamp(which, 0, 2)],
                                  {0, 0, 0, 0});
                }
                if (movie) {
                    // The movie page's RUN_MOVIE (evt/lt3_event): command 5000
                    // {movie, fade in 0, fade out 60} pushes the movie loop.
                    story.request(adventure3::Story::Request::Push, "MOVIE", {movie, 0, 60, 0});
                }
                bool held = false;
                std::uint32_t tick = 0;
                for (;;) {
                    // What the host shows about the scene: the top loop.
                    const adventure3::Loop* top = story.top();
                    const std::string loop = top ? top->name() : "";
                    GameSnapshot snap{loop == "MOVIE"         ? SceneKind::Movie
                                      : loop == "NAME_RESIST" ? SceneKind::Name
                                      : loop == "NAZO"        ? SceneKind::Puzzle
                                                              : SceneKind::Story,
                                      {},
                                      tick++};
                    // Skippable: a film, or the logos (a timed script).
                    snap.skippable = loop == "MOVIE" || loop == "MENU_LOGO" || timedWait(story);
                    if (const auto* place = dynamic_cast<const adventure3::PlaceLoop*>(top)) {
                        snap.touchAreas = place->touchAreas();
                    }
                    const auto bytes = rgba(story.render());
                    HostInput input;
                    NameInput typed;
                    {
                        std::unique_lock lock(shared->mutex);
                        shared->pixels = bytes;
                        shared->state = snap;
                        if (shared->sleeping) {
                            shared->clock.wait(
                                lock, [&] { return !shared->sleeping || shared->stopped; });
                        }
                        shared->clock.wait(lock, [&] { return shared->tick || shared->stopped; });
                        if (shared->stopped) {
                            throw Stop{};
                        }
                        shared->tick = false;
                        if (!shared->events.empty()) {
                            const auto event = shared->events.front();
                            shared->events.pop_front();
                            if (event.name.action == NameInput::Action::None) {
                                shared->input.down = event.down;
                                shared->input.x = event.x;
                                shared->input.y = event.y;
                            } else if (snap.scene == SceneKind::Name) {
                                typed = event.name; // the name entry's typed keys
                            }
                        }
                        input = shared->input;
                        input.skip = shared->skipping;
                    }
                    lscr::SceneInput in;
                    in.held = input.down && input.x >= 0;
                    in.pressed = in.held && !held;
                    in.released = !in.held && held;
                    held = in.held;
                    if (in.held) {
                        in.x = input.x;
                        in.y = input.y;
                    }
                    // The host's skip ends a movie or fast-forwards a timed
                    // sequence (after the frame below).
                    in.keysHeld = in.keysPressed = 0;
                    if (typed.action != NameInput::Action::None) {
                        story.keyboard(typed);
                    }
                    story.frame(in);
                    sound.sound().frame();
                    audio.check();
                    if (input.skip) {
                        if (auto* film = dynamic_cast<adventure3::MovieLoop*>(story.top())) {
                            film->skip(); // ends as START ends a skippable film
                        } else if (story.top() && story.top()->name() == "MENU_LOGO") {
                            // The logos are a timed script: fast-forward its own
                            // frames until it hands over, then quiet its jingle.
                            for (int i = 0;
                                 i < 60 * 60 && story.top() && story.top()->name() == "MENU_LOGO";
                                 ++i) {
                                story.frame(lscr::SceneInput{});
                                sound.sound().frame();
                            }
                            sound.stopStream(0);
                        } else if (timedWait(story)) {
                            // An event's timed wait: its own frames, fast, until
                            // the script moves on.
                            for (int i = 0; i < 60 * 60 && timedWait(story); ++i) {
                                story.frame(lscr::SceneInput{});
                                sound.sound().frame();
                            }
                        }
                    }
                    if (parrot) {
                        deliveries.latest = progress();
                    }
                    if ((movie || parrot) && !story.top() && !story.requestsPending()) {
                        std::lock_guard lock(shared->mutex);
                        shared->returnToLibrary = true; // the movie loop has popped itself
                        return;
                    }
                }
            } catch (const Stop&) {
            } catch (const std::exception& error) {
                std::lock_guard lock(shared->mutex);
                shared->error = error.what();
            }
        });
    return session;
}

GameSession::~GameSession() {
    {
        std::lock_guard lock(shared_->mutex);
        shared_->stopped = true;
    }
    shared_->clock.notify_one();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void GameSession::tick() {
    {
        std::lock_guard lock(shared_->mutex);
        shared_->tick = true;
    }
    shared_->clock.notify_one();
}

ScreenGeometry GameSession::geometry() const {
    std::lock_guard lock(shared_->mutex);
    return shared_->geometry;
}

bool GameSession::frame(std::vector<std::uint8_t>& rgba) const {
    std::lock_guard lock(shared_->mutex);
    if (shared_->pixels.empty()) {
        return false;
    }
    rgba = shared_->pixels;
    return true;
}

GameSnapshot GameSession::state() const {
    std::lock_guard lock(shared_->mutex);
    return shared_->state;
}

std::string GameSession::error() const {
    std::lock_guard lock(shared_->mutex);
    return shared_->error;
}

bool GameSession::returnedToLibrary() const {
    std::lock_guard lock(shared_->mutex);
    return shared_->returnToLibrary;
}

void GameSession::touch(bool down, int x, int y, bool dragged) {
    HostInput input;
    input.down = down;
    input.x = x;
    input.y = y;
    std::lock_guard lock(shared_->mutex);
    // While dragging, only the newest position matters: replace a queued
    // move instead of making the stroke lag behind the mouse.
    if (dragged && shared_->input.down && !shared_->events.empty() && shared_->events.back().down) {
        shared_->events.back() = input;
    } else {
        shared_->events.push_back(input);
    }
}

void GameSession::liftStylus() {
    std::lock_guard lock(shared_->mutex);
    shared_->input.down = false;
    shared_->events.clear();
}

void GameSession::nameInput(NameInput name) {
    HostInput input;
    input.name = name;
    std::lock_guard lock(shared_->mutex);
    if (shared_->state.scene == SceneKind::Name) {
        shared_->events.push_back(input);
    }
}

void GameSession::blow(bool on) {
    std::lock_guard lock(shared_->mutex);
    shared_->input.blow = on;
}

void GameSession::pad(unsigned bits) {
    std::lock_guard lock(shared_->mutex);
    shared_->input.pad = bits;
}

void GameSession::tilt(float x, float y) {
    std::lock_guard lock(shared_->mutex);
    shared_->input.tiltX = x;
    shared_->input.tiltY = y;
}

void GameSession::shake(bool on) {
    std::lock_guard lock(shared_->mutex);
    shared_->input.shake = on;
}

void GameSession::motion(const std::optional<std::array<float, 3>>& up) {
    std::lock_guard lock(shared_->mutex);
    shared_->input.motion = up;
}

void GameSession::sleep(bool on) {
    {
        std::lock_guard lock(shared_->mutex);
        shared_->sleeping = on;
    }
    shared_->clock.notify_one();
}

void GameSession::restart() {
    std::lock_guard lock(shared_->mutex);
    shared_->input.restart = true;
    shared_->input.down = false;
    shared_->events.clear();
}

void GameSession::loadCheckpoint() {
    std::lock_guard lock(shared_->mutex);
    shared_->input.load = true;
    shared_->input.down = false;
    shared_->events.clear();
}

void GameSession::skip() {
    std::lock_guard lock(shared_->mutex);
    if (shared_->state.skippable) {
        shared_->skipping = true;
        shared_->skipScene = shared_->state.scene;
    }
}

bool GameSession::puzzleSolved() const {
    std::lock_guard lock(shared_->mutex);
    return shared_->puzzleSolved;
}

void GameSession::forgetAdventure3Collection() {
    std::lock_guard collection(collectionMutex);
    collectionState.reset();
    records.clear();
}

void GameSession::forgetAdventure4Collection() {
    std::lock_guard collection(collectionMutex);
    adventure4Records.clear();
}

std::vector<std::uint8_t> GameSession::finish() {
    {
        std::lock_guard lock(shared_->mutex);
        shared_->stopped = true;
    }
    shared_->clock.notify_one();
    if (worker_.joinable()) {
        worker_.join();
    }
    std::lock_guard lock(shared_->mutex);
    return shared_->result;
}

} // namespace picarat::shell
