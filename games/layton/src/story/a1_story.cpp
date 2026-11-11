// The first adventure from the logos to the staff roll: the main loop, the
// scene router and the boot sequence. See a1_story.hpp.
#include "story/a1_story.hpp"
#include "menus/a1_bonus.hpp"
#include "menus/a1_challenge.hpp"
#include "menus/a1_hotel.hpp"
#include "menus/a1_mysteries.hpp"
#include "menus/a1_name_entry.hpp"
#include "menus/a1_painting.hpp"
#include "menus/a1_puzzle_index.hpp"
#include "menus/a1_robot_dog.hpp"
#include "menus/a1_title.hpp"
#include "menus/a1_trunk.hpp"
#include "menus/a1_wifi.hpp"
#include "puzzles/judgment.hpp"
#include "sound/a1_sound.hpp"
#include "story/a1_event.hpp"
#include "story/a1_room.hpp"
#include "story/a1_staff_roll.hpp"
#include "system/a1_script.hpp"
#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace picarat::adventure1 {

Story::Story(GraphicsAssets& assets, PresentFrame present, const std::filesystem::path& pack,
             const std::string& language, Adventure1Sound* sound)
    : kit(assets, present, nullptr, SceneKind::PuzzleCard), catalogue(pack, language), sound(sound),
      pack(pack), language(language), presentFrame_(present) {
    kit.sceneSound = sound;
    kit.language = language;
    // As in the puzzle scenes: a frame's sprites show with its text, sprites
    // are layered by their depth, and the game's 3D camera (020040d0).
    kit.spriteLatency = false;
    kit.depthOrder = true;
    kit.ds3D = SceneKit::Camera3D{dspipe::spriteCamera()};
    kit.random = &state.puzzle.random;
}

Story::~Story() = default;

std::uint32_t Story::programWord(std::uint32_t address) const {
    if (program_.empty()) {
        const_cast<Bytes&>(program_) = readFile(pack / "code/arm9.bin");
    }
    const std::size_t at = address - 0x02000000u;
    if (address < 0x02000000u || at + 4 > program_.size()) {
        throw std::runtime_error("program address outside the ARM9 image");
    }
    return std::uint32_t(program_[at]) | std::uint32_t(program_[at + 1]) << 8 |
           std::uint32_t(program_[at + 2]) << 16 | std::uint32_t(program_[at + 3]) << 24;
}

std::string Story::programString(std::uint32_t pointerAddress) const {
    const std::uint32_t address = programWord(pointerAddress);
    std::string out;
    for (std::size_t at = address - 0x02000000u; at < program_.size() && program_[at]; ++at) {
        out += char(program_[at]);
    }
    return out;
}

int Story::randomBelow(int n) {
    return int(gameRandom(state.puzzle.random) % std::uint32_t(n));
}

void Story::setPicture(std::string& layer, const std::string& scriptName) {
    const std::string name = kit.localized(pictureName(scriptName));
    if (fileExists(pack / "bg" / (name + ".l2bg"))) {
        layer = name;
    }
}

const SaveImage& Story::saves() {
    if (!saves_) {
        // The name an empty slot holds (02024464: the string at 020d3808).
        const std::string empty = programString(0x020244f0);
        if (!saveFile.empty() && fileExists(saveFile)) {
            saves_.emplace(empty, readFile(saveFile));
        } else {
            saves_.emplace(empty);
        }
        state.wifiPuzzles = saves_->wifiPuzzles(); // 0201a7fc
    }
    return *saves_;
}

bool Story::storeGame(int slot) {
    saves();
    saves_->store(slot, state);
    state.slot = std::int8_t(slot);
    if (saveFile.empty()) {
        return true;
    }
    std::error_code error;
    std::filesystem::create_directories(saveFile.parent_path(), error);
    const auto& bytes = saves_->bytes();
    std::ofstream out(saveFile, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    out.close();
    return !error && out.good();
}

void Story::saveWriteError() {
    // 0200b590: effect 4 and the music out over 30 frames, both screens to
    // black, the text gone, then save_writeerror on the lower screen fading
    // in (0200fb90) and, 30 VBlanks later, nothing more: the game waits for
    // a reset.
    if (sound) {
        sound->effect(4);
        sound->stopMusicSoftly();
    }
    fadeOut(kit, Screens::Both, 1);
    kit.clearText(SceneKit::Lower);
    kit.clearText(SceneKit::Upper);
    kit.lowerOverlay.clear();
    kit.clearSubmissions(); // the save screen's sprites go with the menu
    kit.lowerBackground = kit.localized("?/save_writeerror");
    kit.lowerTint = {};
    fadeIn(kit, Screens::Lower, 1);
    stall(0x1e);
    for (;;) {
        poll();
        present();
    }
}

bool Story::loadGame(int slot) {
    saves();
    GameState loaded;
    if (!saves_->load(slot, loaded)) {
        return false;
    }
    loaded.slot = std::int8_t(slot);
    loaded.playerName = saves_->slot(slot).name;
    loaded.wifiPuzzles = std::move(state.wifiPuzzles);
    state = std::move(loaded);
    countedFrames_ = kit.frames; // the clock starts again (+0x53c)
    return true;
}

void Story::saveCheckpoint() {
    if (checkpointFile.empty()) {
        return;
    }
    SaveImage image(programString(0x020244f0));
    image.store(0, state);
    std::error_code error;
    std::filesystem::create_directories(checkpointFile.parent_path(), error);
    const auto temporary = checkpointFile.string() + ".new";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        const auto& bytes = image.bytes();
        out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        if (!out) {
            return;
        }
    }
    std::filesystem::rename(temporary, checkpointFile, error);
}

bool Story::loadCheckpoint() {
    if (checkpointFile.empty() || !fileExists(checkpointFile)) {
        return false;
    }
    saves(); // the cartridge backup's WiFi puzzles
    const SaveImage image(programString(0x020244f0), readFile(checkpointFile));
    GameState loaded;
    if (!image.slot(0).used || !image.load(0, loaded)) {
        return false;
    }
    loaded.slot = -1;
    loaded.playerName = image.slot(0).name;
    loaded.wifiPuzzles = std::move(state.wifiPuzzles);
    state = std::move(loaded);
    countedFrames_ = kit.frames;
    return true;
}

void Story::present() {
    kit.present();
    countPlayTime();
}

void Story::countPlayTime() {
    // 02019be0 measures real time (OS ticks since the start or load), so
    // every frame counts, the loading pauses and fades as well.
    state.sessionFrames += kit.frames - countedFrames_;
    countedFrames_ = kit.frames;
}

void Story::poll() {
    kit.poll();
    const HostInput& host = kit.hostInput();
    touch.update(host.down, host.x, host.y);
    if (actorsActive) {
        moveAndDrawActors();
    }
}

int Story::addActor(bool upper, int x, int y, const std::string& sprite,
                    const std::string& animation) {
    auto& list = upper ? upperActors : actors;
    if (list.size() >= 16) {
        return -1;
    }
    Actor a;
    a.upper = upper;
    a.sprite = kit.load(sprite, x, y, 1);
    a.x = a.targetX = x;
    a.y = a.targetY = y;
    setActorAnimation(a, animation);
    // 02007d5c: a sprite whose first animation's name starts with '*' is a
    // mouth.
    if (!upper && a.sprite.sprite) {
        for (const auto& [name, index] : a.sprite.sprite->animationNames) {
            if (index == 1 && !name.empty() && name[0] == '*' &&
                a.sprite.sprite->animations.size() > 2) {
                a.kind = 2;
            }
        }
    }
    list.push_back(std::move(a));
    return int(list.size()) - 1;
}

void Story::setActorAnimation(Actor& actor, const std::string& name) {
    // 02007e3c: an unknown name keeps the current animation.
    if (!actor.sprite.sprite) {
        return;
    }
    const auto& names = actor.sprite.sprite->animationNames;
    auto it = names.find(name);
    if (it == names.end()) {
        for (auto i = names.begin(); i != names.end(); ++i) {
            if (i->first.size() == name.size() &&
                std::equal(name.begin(), name.end(), i->first.begin(), [](char a, char b) {
                    return std::tolower(std::uint8_t(a)) == std::tolower(std::uint8_t(b));
                })) {
                it = i;
                break;
            }
        }
    }
    if (it != names.end() && it->second != actor.sprite.player.animation) {
        kit.select(actor.sprite, it->second);
    }
}

void Story::moveAndDrawActors() {
    // 0200f090: movement (0205a30c / 0205a634 "Move To Target": a step of
    // `speed` toward the target on each axis).
    auto move = [](Actor& a) {
        if (!a.moveToTarget) {
            return;
        }
        auto step = [&](int& v, int target) {
            if (v < target) {
                v = std::min(target, v + a.speed);
            } else if (v > target) {
                v = std::max(target, v - a.speed);
            }
        };
        step(a.x, a.targetX);
        step(a.y, a.targetY);
    };
    for (auto& a : actors) {
        move(a);
    }
    for (auto& a : upperActors) {
        move(a);
    }
    // The name of an animation index.
    auto nameOf = [](const Actor& a, unsigned index) -> std::string {
        for (const auto& [name, i] : a.sprite.sprite->animationNames) {
            if (i == index) {
                return name;
            }
        }
        return {};
    };
    auto starred = [&](const Actor& a, unsigned index) {
        const std::string n = nameOf(a, index);
        return !n.empty() && n[0] == '*';
    };
    // 0205a198 (lower) / 0205a528 (upper): drawing with the jitter.
    auto draw = [&](Actor& a) {
        int dx = 0, dy = 0;
        if (a.shake > 0) {
            --a.shake;
            const auto r1 = int(gameRandom(state.puzzle.random));
            const auto r2 = int(gameRandom(state.puzzle.random));
            dx = r1 % 3 - 1;
            dy = r2 % 3 - 1;
        }
        a.sprite.x = a.x + dx;
        a.sprite.y = a.y + dy;
        if (a.upper) {
            if (a.visible) {
                kit.draw(a.sprite);
            }
            return;
        }
        a.alpha = std::clamp(a.alpha + a.fade, 0.0f, 31.0f);
        const int alpha = int(a.alpha);
        if (a.visible && alpha >= 2) {
            kit.draw(a.sprite, alpha); // (mirrored drawing not reconstructed yet)
        }
    };
    // 0200f0c4.
    for (std::size_t i = 0; i < actors.size(); ++i) {
        Actor& a = actors[i];
        const bool isSpeaker = int(i) == speaker;
        if (a.kind == 0) {
            draw(a);
        } else if (a.kind == 1) {
            if (talking && isSpeaker) {
                draw(a);
            }
        } else if (a.sprite.sprite) {
            const unsigned current = a.sprite.player.animation;
            if (starred(a, current) && isSpeaker && talking) {
                kit.select(a.sprite, current + 1); // +0x210: the talking animation
            } else if (current > 1 && starred(a, current - 1) && (!isSpeaker || !talking)) {
                kit.select(a.sprite, current - 1); // +0x208: back to the quiet one
            }
            draw(a);
        }
    }
    for (auto& a : upperActors) {
        draw(a);
    }
}

void Story::stall(int frames) {
    kit.stall(frames);
}

void Story::stallUntilRelease(int frames) {
    // 02003d94: the touch state is updated every wait; a release ends it.
    for (int i = 0; i < frames; ++i) {
        const HostInput& host = kit.hostInput();
        touch.update(host.down, host.x, host.y);
        if (touch.released) {
            return;
        }
        kit.stall(1);
    }
}

void Story::waitForTouch() {
    // 02003de0: keys and touch every wait, until a release (or A).
    for (;;) {
        const HostInput& host = kit.hostInput();
        touch.update(host.down, host.x, host.y);
        if (touch.released) {
            return;
        }
        kit.stall(1);
    }
}

std::string Story::text(const std::string& folder, const std::string& archive,
                        const std::string& name) const {
    const auto path = pack / folder / language / archive / name;
    if (!fileExists(path)) {
        return {};
    }
    const Bytes raw = readFile(path);
    return kit.text.expand(std::string(raw.begin(), raw.end()));
}

bool Story::hasText(const std::string& folder, const std::string& archive,
                    const std::string& name) const {
    return fileExists(pack / folder / language / archive / name);
}

Bytes Story::script(const std::string& path) const {
    const auto file = pack / path;
    if (!fileExists(file)) {
        return {};
    }
    return readFile(file);
}

void Story::runScript(const std::string& path, const ScriptContext& context) {
    const Bytes data = script(path);
    if (data.size() < 4) {
        return; // a missing file: the loader's buffer holds an empty script
    }
    const std::string previous = currentScript_;
    currentScript_ = path;
    ScriptInterpreter interpreter(data, scripts());
    if (onCommand) {
        interpreter.onCommand = [this](CommandBoundary b) {
            if (b.entering) {
                onCommand(currentScript_, b.command);
            }
        };
    }
    interpreter.run(context);
    currentScript_ = previous;
}

void Story::missing(const std::string& what) const {
    throw std::runtime_error(std::string("first adventure: ") + what + " is not reconstructed yet");
}

void Story::run() {
    // 02000cb4 (after the start-up 02002f4c): the router, then the scenes
    // the router leaves to the loop itself.
    if (researchStart) {
        state.reset();
        researchStart(state);
    }
    if (continueCheckpoint) {
        // The app's checkpoint: the boot's display and sound set-up, then
        // its room (a loaded game enters it without the first touch hint).
        continueCheckpoint = false;
        state.scene = scene::Reset;
        state.reset();
        if (loadCheckpoint()) {
            present();
            bootSound();
            state.firstTouchHint = false;
            state.scene = scene::Room;
        }
    }
    for (;;) {
        route();
        if (state.scene == 6 || state.scene == 0x10) {
            boot();
        }
        if (state.scene == scene::Reset) {
            boot();
        }
        if (state.scene == scene::Taiken) {
            missing("the demo ending (02002558)");
        }
        if (state.scene == scene::Room) {
            roomScene();
        }
    }
}

void Story::route() {
    // 02000d28.
    switch (state.scene) {
    case scene::Reset:
    case scene::Room:
    case 6:
    case scene::Taiken:
    case 0x10:
        return; // left to the main loop
    case scene::Question:
        playQuestion();
        return;
    case scene::StaffRoll: { // 02002b9c: the staff roll, then event 0x156
        staffRoll(*this);
        state.event = 0x156;
        EventScene event(*this);
        event.run();
        event.finish();
        // 02024538 reads the backup's header again; here it is in memory.
        return;
    }
    case scene::HotelRoom: // 02001a94
        // The first visit explains the hotel (event 0xb6, until flag 505).
        if (!state.flag(0x1f9)) {
            state.event = 0xb6;
            EventScene event(*this);
            event.run();
            event.finish();
        }
        hotelPage(*this);
        if (state.scene == scene::Event) {
            EventScene event(*this);
            event.run();
            event.finish();
        }
        state.scene = scene::TrunkMenu;
        return;
    case scene::Painting: // 020019bc
        // The first visit explains the painting (event 0xb7, until flag 504).
        if (!state.flag(0x1f8)) {
            state.event = 0xb7;
            EventScene event(*this);
            event.run();
            event.finish();
        }
        paintingPage(*this);
        if (state.scene == scene::Event) {
            EventScene event(*this);
            event.run();
            event.finish();
        }
        state.scene = scene::TrunkMenu;
        return;
    case scene::RobotDog: // 02001b88
        // The first visit explains the dog (event 0xb5, until flag 503).
        if (!state.flag(0x1f7)) {
            state.event = 0xb5;
            EventScene event(*this);
            event.run();
            event.finish();
        }
        robotDogPage(*this);
        if (state.scene == scene::Event) {
            // Complete: its event, then it is named (scene 0x28, 0200298c).
            EventScene event(*this);
            event.run();
            event.finish();
            state.scene = 0x28;
            nameEntry(*this);
        }
        state.scene = scene::TrunkMenu;
        return;
    case scene::PuzzleIndex: // 02001d84
        puzzleIndex(*this);
        return;
    case scene::IndexPuzzle: // the router's case 4: a puzzle from the index, then the index
        playQuestion();
        state.scene = scene::IndexPuzzle;
        puzzleIndex(*this);
        return;
    case scene::TitleScreen: // the bonus section's way back (left to the boot loop)
        return;
    case scene::Continue:                      // 020029ec: the secret menu
        state.wifiShown = state.wifiFirst = 0; // 020368d4
        state.indexList = 0;                   // 02031ec8
        state.indexFirst[0] = state.indexFirst[1] = 0;
        state.indexShown = -1;
        secretMenu(*this);
        return;
    case 0xd: // 02001d04
        extrasMenu(*this);
        return;
    case 0xe: // 02002104
        characterFiles(*this);
        return;
    case 0xf: // 020022e4
        movieTheatre(*this);
        return;
    case 0x11: // 02002164
        artGallery(*this);
        return;
    case 0x12: // 020021c4
        musicRoom(*this);
        return;
    case 0x13: // 02002224
        voiceRoom(*this);
        return;
    case 0x14: // 02002284
        passcodeNotes(*this);
        return;
    case 0x25: { // 02002aac: the passcode, or its event once seen
        if (!(state.events[0x103] & 1)) {
            passcodePage(*this);
        } else {
            state.scene = scene::Event;
            state.event = 0x103;
        }
        // The event ends with a reset (0x51 "reset", the program's reset
        // flag 0200bd3c): saved first, then back to the logos.
        bool routed = false;
        while (state.scene != 0xd && state.scene != scene::Reset) {
            route();
            routed = true;
        }
        if (routed) {
            const bool reset = state.scene == scene::Reset;
            saveBonusProgress();
            state.scene = reset ? scene::Reset : std::uint8_t(0xd);
        }
        return;
    }
    case scene::EventThenQuestion: { // 02001dcc: the puzzle index from the bonus section
        const std::uint8_t room = state.room;
        state.room = 1;
        puzzleIndex(*this);
        state.room = room;
        if (state.scene == scene::TrunkMenu) { // closed
            saveBonusProgress();
            state.scene = scene::Continue;
        } else if (state.scene == scene::IndexPuzzle) {
            saveBonusProgress();
            state.scene = scene::IndexPuzzle;
            playQuestion();
            state.scene = scene::EventThenQuestion;
        }
        return;
    }
    case scene::Challenge: // 02002a34: the game is saved first
        saveBonusProgress();
        state.scene = scene::Challenge;
        challengeMenu(*this);
        return;
    case 0x1d: // 02002650
        wifiMenu(*this);
        return;
    case 0x1e: // 02002690
        wifiPuzzles(*this);
        return;
    case 0x1f: // 020026d8: a WiFi puzzle, then the list
        playQuestion();
        saveBonusProgress();
        state.scene = 0x1e;
        return;
    case 0x20: // 0200274c: the console's WiFi settings
        wifiSettings(*this);
        return;
    case 0x21: // 020028cc: the download
        wifiDownload(*this);
        return;
    case scene::Mysteries: // 02001ca4: then back to the trunk
        mysteriesPage(*this);
        state.scene = scene::TrunkMenu;
        return;
    case scene::TrunkMenu: { // 02001958
        // 02031ec8: the puzzle index starts afresh.
        state.indexList = 0;
        state.indexFirst[0] = state.indexFirst[1] = 0;
        state.indexShown = -1;
        Trunk trunk(*this);
        trunk.run();
        trunk.finish();
        return;
    }
    case scene::Event: { // 0200190c
        EventScene event(*this);
        event.run();
        event.finish();
        return;
    }
    case scene::Movie: // 02002430: the music fades out first
        if (sound) {
            sound->fadeOutMusic();
        }
        stall(0x3c);
        if (sound) {
            sound->stopMusicSoftly();
        }
        stall(0x3c);
        playMovie(*this, movieCues, false);
        movieCues = MovieCues{};
        state.scene = state.returnScene;
        return;
    case scene::EventMovie: // 020024bc
        playMovie(*this, movieCues, false);
        movieCues = MovieCues{};
        state.scene = state.returnScene;
        return;
    case scene::DualMovie: // 02002508
        fadeOut(kit, Screens::Both);
        playMovie(*this, movieCues, true);
        movieCues = MovieCues{};
        state.scene = state.returnScene;
        return;
    default:
        missing("scene " + std::to_string(state.scene));
    }
}

void Story::saveBonusProgress() {
    // 0201a8e8 with the scene set to 3 first: the bonus section keeps its
    // progress in the slot it was opened with (a game loaded there resumes
    // in its room).
    const std::uint8_t scene = state.scene;
    state.scene = scene::Room;
    if (state.slot >= 0) {
        storeGame(state.slot);
    }
    state.scene = scene;
}

void Story::playQuestion() {
    // 02001044: the title card, the puzzle and its result (PuzzlePlay, with
    // its own display state), then the reward event for a newly solved
    // story puzzle.
    const int number = state.puzzleNumber;
    if (number <= 0 || number >= 0xff) {
        return;
    }
    const bool solvedBefore = state.solved(number);
    marker("question");
    if (researchSolvePuzzle) {
        auto& progress = state.progress(number);
        progress |= 1;
        if (researchSolvePuzzle(unsigned(number))) {
            progress |= 2;
            state.puzzle.solved = true;
        }
    } else {
        PuzzlePlay play(kit.assets, presentFrame_, catalogue, language, unsigned(number),
                        state.puzzle, sound);
        play.play();
    }
    kit.setBrightness(-16);
    // The engine's destructor (0204060c): the event runs on after its
    // puzzle, in the scene the script left to return to.
    state.afterPuzzle = 1;
    const bool question = state.scene == scene::Question;
    if (state.scene != 0x1f) {
        state.scene = state.returnScene;
    }
    (void)solvedBefore;
    if (question && !(state.progress(number) & 4) && state.reward) {
        state.afterPuzzle = 1;
        EventScene event(*this);
        event.run();
        event.finish();
    }
}

void Story::bootSound() {
    // 02001efc: sounds of kind 1, voice group 0.
    if (sound) {
        const auto wait = [this] {
            present();
        };
        sound->setupScene(1, wait);
        sound->loadVoice(0, wait);
    }
}

void Story::logos() {
    // 02001efc: the boot's sounds, then logo.gds on the base object;
    // afterwards the scene the script left in +0x11.
    bootSound();
    if (!startAtTitle) {
        // The logos are timed: the host's skip fast-forwards them.
        SceneKit::Skippable skippable(kit);
        runScript("script/logo.gds", commonCommands());
    }
    state.scene = state.returnScene;
}

void Story::boot() {
    // 0200129c.
    state.scene = scene::Reset;
    if (sound) {
        sound->stopMusicSoftly();
    }
    state.reset();
    state.setPuzzle(1);
    // The display set up with a frame presented (0200432c, 02004254,
    // 02003ef8).
    present();
    logos();
    // 0201abec reads the three slots' headers from the cartridge.
    for (;;) {
        const TitleChoice choice = titleScreen(*this);
        if (choice == TitleChoice::NewGame) {
            newGame();
            return;
        }
        if (choice == TitleChoice::Continue) {
            // A loaded game starts with event 0x12d (the story so far),
            // without the first room's touch hint.
            state.firstTouchHint = false;
            state.event = 0x12d;
            state.scene = scene::Event;
            return;
        }
        // The bonus section (scene 0x24) until its back button returns to
        // the title (scene 0x15); then the title again on a fresh state.
        do {
            route();
        } while (state.scene != scene::TitleScreen && state.scene != scene::Reset);
        if (state.scene == scene::Reset) {
            return; // a reset: the boot again, from the logos
        }
        state.reset();
        state.setPuzzle(1);
    }
}

void Story::newGame() {
    // 0200129c, a new game.
    if (sound) {
        sound->stopMusicSoftly();
    }
    stall(0x1e);
    state.reset();
    countedFrames_ = kit.frames; // the clock starts
    state.setPuzzle(1);
    nameEntry(*this);
    openingText(*this);
    // The opening movie (020024bc) with its music loaded first.
    state.movie = 1;
    const auto loading = kit.frames;
    if (sound) {
        sound->forgetLoadedMusic();
        sound->loadMusic(0x11, [this] { stall(1); });
    }
    loadingPause(loading, 13); // measured: ROM frames 2471..2484
    playMovie(*this, MovieCues{}, false);
    // The intro conversation, the first puzzle (no quit button), the
    // conversation's second part, the title logo.
    state.introPart = 0;
    introTalk(*this, 0);
    state.returnScene = 0;
    state.setPuzzle(0x18);
    state.puzzle.hideQuit = true;
    playQuestion();
    state.puzzle.hideQuit = false;
    // 02019afc(0x18): the first puzzle goes to the list at +0x54c.
    for (auto& slot : state.items) {
        if (slot == 0) {
            slot = 0x18;
            break;
        }
    }
    state.puzzle.solved = true;
    state.progress(state.puzzleNumber) |= 2;
    if (sound) {
        sound->fadeOutMusic();
    }
    stall(0x3c);
    state.introPart = 1;
    introTalk(*this, 1);
    titleLogo(*this);
    stall(0x32);
    // script/story.gds is not on the cartridge: nothing runs.
    runScript("script/story.gds", commonCommands());
    if (sound) {
        sound->forgetLoadedMusic();
    }
    state.room = 1;
    state.scene = scene::Room;
    if (sound) {
        sound->setupRoom(state.room, [this] { stall(1); });
        sound->startLoadedMusic(true);
    }
}

void Story::roomScene() {
    // 02002324: room<N>_in.gds on the base object; the scenes it starts
    // run until the scene is a room again; then the room itself.
    marker("room");
    runScript("script/rooms/room" + std::to_string(state.room) + "_in.gds", commonCommands());
    while (state.scene != scene::Room) {
        route();
        if (state.scene == scene::Reset) {
            return;
        }
    }
    RoomScene room(*this);
    room.run();
    room.finish();
}

} // namespace picarat::adventure1
