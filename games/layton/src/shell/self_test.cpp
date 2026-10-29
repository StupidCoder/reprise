// The headless self-test: plays the opening and the first puzzles through
// the real game loop and sound output (without a window or sound device)
// and checks the results. build.sh runs it when a test pack is present.
#include "shell/self_test.hpp"
#include "shell/audio_output.hpp"
#include "sound/sad_audio.hpp"
#include "story/a2_game.hpp"
#include "system/a2_checkpoint.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>

namespace picarat::shell {

int selfTest(const std::filesystem::path& output, const std::filesystem::path& assets) {
    struct Complete {};

    try {
        std::filesystem::create_directories(output);
        AudioOutput audio(assets, false);
        SadPcm recording;
        recording.header.channels = 2;
        SadPcm full;
        full.header.channels = 2; // whole route: streams plus sequenced music/effects
        unsigned sampleRemainder = 0;
        std::size_t nonzeroSamples = 0;
        unsigned frames = 0, age = 0, puzzleVisits = 0, wrong = 0;
        SceneKind previous = SceneKind::Logos;
        int previousDetail = -1;
        bool accepted = false, pausedSilent = false, pauseChecked = false;
        std::vector<int> events;
        std::vector<unsigned> rooms;
        CheckpointStore saves(output / "checkpoint.l2cp");
        std::ofstream trace(output / "scene-trace.tsv");
        Game game(
            assets,
            [&](const Surface& image, const GameSnapshot& state) {
                if (++frames > 25000) {
                    throw std::runtime_error("self-test did not solve first puzzle");
                }
                if (state.scene != previous || state.detail != previousDetail) {
                    age = 0;
                    previous = state.scene;
                    previousDetail = state.detail;
                    trace << frames << '\t' << Game::sceneName(state.scene) << '\t' << state.detail
                          << '\n';
                    trace.flush();
                    if (state.scene == SceneKind::Story) {
                        events.push_back(state.detail);
                    }
                    if (state.scene == SceneKind::Room) {
                        rooms.push_back(state.detail);
                    }
                    if (state.scene == SceneKind::Puzzle) {
                        ++puzzleVisits;
                    }
                } else {
                    ++age;
                }
                // Pull decoded samples through the same transport at the native
                // 60 Hz cadence. This tests PCM delivery without an audio device.
                sampleRemainder += 32728;
                auto pcm = audio.renderOffline(sampleRemainder / 60);
                sampleRemainder %= 60;
                nonzeroSamples +=
                    std::count_if(pcm.begin(), pcm.end(), [](auto v) { return v != 0; });
                if (!accepted) {
                    recording.samples.insert(recording.samples.end(), pcm.begin(), pcm.end());
                }
                full.samples.insert(full.samples.end(), pcm.begin(), pcm.end());
                HostInput input;
                // The chapter card must never be resurrected by the empty event
                // that hands off to narration. Inspect every presented frame.
                if (state.scene == SceneKind::Story && state.detail == 10020 &&
                    std::any_of(image.pixels.begin(), image.pixels.end(),
                                [](auto p) { return (p & 0x7fff) != 0; })) {
                    throw std::runtime_error("chapter card leaked into narration handoff");
                }
                if (state.scene == SceneKind::Title) {
                    if (age == 200) {
                        writePPM(image, output / "title.ppm");
                    }
                    if (age >= 210 && age < 213) {
                        input.down = true;
                        input.x = 128;
                        input.y = 66;
                    }
                }
                if (state.scene == SceneKind::Name) {
                    if (age >= 30 && age < 33) {
                        input.down = true;
                        input.x = 42;
                        input.y = 96;
                    }
                    if (age == 50) {
                        if (state.name.value() != "a") {
                            throw std::runtime_error("bundled keyboard self-test failed");
                        }
                        writePPM(image, output / "name.ppm");
                    }
                    if (age >= 60 && age < 63) {
                        input.down = true;
                        input.x = 128;
                        input.y = 172;
                    }
                }
                if (state.scene == SceneKind::Accepted && !accepted) {
                    if (state.name.value() != "a") {
                        throw std::runtime_error("unexpected accepted name");
                    }
                    const std::vector<std::string> expected{"play ST_001.SAD", "stop 0.000000",
                                                            "play bgm/BG_001.SAD", "stop 3.000000"};
                    std::vector<std::string> streams;
                    for (const auto& e : audio.events()) {
                        if (e.rfind("play ", 0) == 0 || e.rfind("stop ", 0) == 0) {
                            streams.push_back(e);
                        }
                    }
                    if (streams != expected || nonzeroSamples < 1000 ||
                        std::any_of(pcm.begin(), pcm.end(), [](auto v) { return v != 0; })) {
                        throw std::runtime_error("bundled stream start/fade/output check failed");
                    }
                    const auto wav = sadWave(recording);
                    std::ofstream audioFile(output / "opening-audio.wav", std::ios::binary);
                    audioFile.write(reinterpret_cast<const char*>(wav.data()), wav.size());
                    if (!audioFile) {
                        throw std::runtime_error("cannot write self-test PCM recording");
                    }
                    accepted = true;
                }
                auto tap = [&](int x, int y, unsigned start) {
                    if (age >= start && age < start + 4) {
                        input.down = true;
                        input.x = x;
                        input.y = y;
                    }
                };
                // The mysteries list (ov20, script 0x71 in event 10030) closes
                // with its OK button at the same place.
                if (state.scene == SceneKind::Mysteries && age >= 200 && age % 60 >= 20 &&
                    age % 60 < 24) {
                    input.down = true;
                    input.x = 128;
                    input.y = 178;
                }
                if (state.scene == SceneKind::Story) {
                    if (age % 60 >= 20 && age % 60 < 24) {
                        input.down = true;
                        input.x = 128;
                        input.y = 178;
                    }
                    if (age == 100) {
                        writePPM(image,
                                 output / ("event-" + std::to_string(state.detail) + ".ppm"));
                    }
                }
                if (state.scene == SceneKind::Movie && age == 1000) {
                    writePPM(image, output / "movie.ppm");
                }
                // Lid close during the first movie (ov6 resync path) for 60 frames.
                if (state.scene == SceneKind::Movie && state.detail == 1 && age >= 300 &&
                    age < 360) {
                    input.sleep = true;
                }
                // Whole-system pause in the first room (0207f6f4 / 0207f724): the
                // frames between must be exactly silent.
                if (state.scene == SceneKind::Room && rooms.size() == 1) {
                    if (age == 10) {
                        audio.sequenced()->pauseSound();
                        pausedSilent = true;
                    } else if (age > 11 && age < 40 &&
                               std::any_of(pcm.begin(), pcm.end(), [](auto v) { return v != 0; })) {
                        pausedSilent = false;
                    }
                    if (age == 40) {
                        audio.sequenced()->resumeSound();
                        pauseChecked = true;
                    }
                }
                if (state.scene == SceneKind::Room) {
                    if (age == 30) {
                        writePPM(image, output / ("room-" + std::to_string(state.detail) + "-" +
                                                  std::to_string(rooms.size()) + ".ppm"));
                    }
                    bool drawerDone =
                        std::find(events.begin(), events.end(), 10035) != events.end();
                    if (!drawerDone) {
                        tap(72, 146, 60);
                    } else {
                        tap(240, 176, 60);
                        tap(245, 95, 100);
                    }
                    // Room tutorial uses the same room scene before interaction.
                    if (!drawerDone && age > 100 && age % 120 >= 60 && age % 120 < 64) {
                        input.down = true;
                        input.x = 72;
                        input.y = 146;
                    }
                }
                // The puzzle card (ov27 020f0860) waits for a touch: 154 frames of build-up first.
                if (state.scene == SceneKind::PuzzleCard) {
                    if (age == 170 && !fileExists(output / "card.ppm")) {
                        writePPM(image, output / "card.ppm");
                    }
                    tap(128, 160, 180);
                }
                if (state.scene == SceneKind::PuzzleIntro) {
                    tap(128, 160, 20);
                    tap(128, 160, 40);
                }
                if (state.scene == SceneKind::Puzzle) {
                    if (age == 1) {
                        writePPM(image,
                                 output / ("puzzle-" + std::to_string(puzzleVisits) + ".ppm"));
                    }
                    if (puzzleVisits == 1) {
                        tap(220, 170, 20);
                    } else {
                        const int moves[][4] = {{80, 153, 80, 32},
                                                {12, 153, 48, 64},
                                                {148, 153, 80, 64},
                                                {114, 153, 112, 64},
                                                {46, 153, 80, 96}};
                        for (unsigned i = 0; i < 5; ++i) {
                            unsigned n = 20 + 40 * i;
                            if (age >= n && age < n + 16) {
                                input.down = true;
                                input.x = moves[i][age < n + 4 ? 0 : 2] + 15;
                                input.y = moves[i][age < n + 4 ? 1 : 3] + 15;
                            }
                        }
                        if (age == 220) {
                            if (state.pieceSlots != std::array<int, 6>{0, 1, 2, 3, 4, 9}) {
                                throw std::runtime_error("bundled puzzle arrangement");
                            }
                            writePPM(image, output / "arrangement.ppm");
                        }
                        tap(220, 170, 230);
                    }
                }
                if (state.scene == SceneKind::PuzzleWrong) {
                    if (age == 1) {
                        ++wrong;
                        writePPM(image, output / "wrong.ppm");
                    }
                    tap(128, 160, 20);
                    tap(100, 40, 50);
                }
                if (state.scene == picarat::SceneKind::PuzzleReward) {
                    tap(128, 160, 30);
                }
                if (state.scene == SceneKind::PuzzleSolved) {
                    if (age == 1) {
                        writePPM(image, output / "solved.ppm");
                    }
                    tap(128, 160, 20);
                }
                if (state.storyBoundary) {
                    throw std::runtime_error("story boundary before puzzle 116: " + state.boundary);
                }
                if (state.scene == SceneKind::PuzzleCard && state.detail == 116) {
                    // The story router reached puzzle 002 (internal 116) itself.
                    const auto saved = saves.load();
                    if (!saved || saved->scene != picarat::scene::Puzzle ||
                        saved->eventId != 20030 || saved->puzzle != 116 ||
                        saved->progress.puzzleByte(0) != 6 || saved->progress.picarats() != 9 ||
                        saved->progress.hintCoins() != 10) {
                        throw std::runtime_error("bundled checkpoint round trip");
                    }
                    if (!accepted || wrong != 1) {
                        throw std::runtime_error("bundled story/puzzle result");
                    }
                    for (int id : {18390, 10001, 10015, 10020, 10021, 10030, 10035, 10036, 20000,
                                   20003, 20030}) {
                        if (std::find(events.begin(), events.end(), id) == events.end()) {
                            throw std::runtime_error("missing story event " + std::to_string(id));
                        }
                    }
                    if (std::find(rooms.begin(), rooms.end(), 91) == rooms.end()) {
                        throw std::runtime_error("hallway not reached");
                    }
                    {
                        const auto wav = sadWave(full);
                        std::ofstream f(output / "route-audio.wav", std::ios::binary);
                        f.write(reinterpret_cast<const char*>(wav.data()), wav.size());
                    }
                    std::ofstream audioEvents(output / "audio-events.txt");
                    for (const auto& event : audio.events()) {
                        audioEvents << event << '\n';
                    }
                    {
                        const auto all = audio.events();
                        auto has = [&](const std::vector<std::string>& seq) {
                            auto it = all.begin();
                            for (const auto& want : seq) {
                                it = std::find_if(it, all.end(), [&](const std::string& e) {
                                    return e.rfind(want, 0) == 0;
                                });
                                if (it == all.end()) {
                                    return false;
                                }
                                ++it;
                            }
                            return true;
                        };
                        if (!pauseChecked || !pausedSilent || !has({"pause", "resume"})) {
                            throw std::runtime_error("sound pause/resume check failed");
                        }
                        if (!has({"stream-prepare 3 1", "stream-start", "stream-stop-fade", "pause",
                                  "resume", "stream-restart", "stream-seek", "stream-volume",
                                  "stream-stop 0.5"})) {
                            throw std::runtime_error(
                                "movie lid-close stream sequence check failed");
                        }
                    }
                    std::ofstream report(output / "self-test.json");
                    report << "{\"passed\":true,\"native_frames\":" << frames
                           << ",\"accepted_name\":\"a\",\"audio_nonzero_samples\":"
                           << nonzeroSamples
                           << ",\"story_events_checked\":11,\"reached_puzzle_internal_id\":116,"
                              "\"first_puzzle_solved\":true,\"picarats\":9,\"progress_byte\":6,"
                              "\"wrong_answer_then_retry\":true,\"audio_device_tested\":false,"
                              "\"gui_tested\":false}\n";
                    throw Complete{};
                }
                return input;
            },
            &audio, &saves);
        try {
            game.run();
        } catch (const Complete&) {
            std::cout << "Bundled story and first-puzzle self-test passed\n";
            return 0;
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
    }
    return 1;
}

} // namespace picarat::shell
