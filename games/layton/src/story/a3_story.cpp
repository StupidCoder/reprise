// The third adventure's loop stack. See a3_story.hpp.
#include "story/a3_story.hpp"
#include "sound/a3_sound.hpp"
#include "story/a3_loops.hpp"
#include <stdexcept>

namespace picarat::adventure3 {

// --- Loop ------------------------------------------------------------------------

Loop::Loop(Story& story, std::string name, LoopArgs args)
    : story_(story), name_(std::move(name)), args_(args) {}

Loop::~Loop() = default;

bool Loop::busy() const {
    return scene_ && (scene_->brightness[0].active() || scene_->brightness[1].active());
}

Surface Loop::render() const {
    return scene_ ? scene_->render()
                  : Surface{256, 384, std::vector<std::uint16_t>(256 * 384, 0x8000)};
}

std::unique_ptr<lscr::Scene> Loop::makeScene() {
    lscr::Scene::Host host;
    host.sound = story_.sound;
    host.memos = &story_.memos;
    auto scene = std::make_unique<lscr::Scene>(story_.assets, story_.text, host, &story_.state);
    for (int s = 0; s < 2; ++s) {
        scene->brightness[size_t(s)].set({story_.brightness[size_t(s)]});
    }
    scene->backgroundFilter = story_.backgroundFilter;
    scene->stage = story_.stage;
    if (story_.guide.id >= 0) {
        const lscr::Scene::Guide guide = story_.guide;
        story_.setGuide(*scene, guide.id);
        scene->guide.shown = guide.shown;
    }
    installStoryCommands(*scene, story_, *this);
    return scene;
}

// --- Story -----------------------------------------------------------------------

const std::vector<std::string>& Story::residentScripts() {
    // 0211f740: the GAME loop's programs, resident while the game runs.
    static const std::vector<std::string> files{"evt/lt3_common.cbin", "evt/lt3_place.cbin",
                                                "evt/lt3_event.cbin", "evt/lt3_memo.cbin",
                                                "evt/lt3_saveload.cbin"};
    return files;
}

Story::Story(Assets& a, const GameText& t, Adventure3Sound* s, std::filesystem::path savesFolder,
             bool boot)
    : assets(a), text(t), sound(s), saves(std::move(savesFolder)) {
    state.declare(assets.script("rc/ge/lt3_rc/lt3_gmd.lbin"));
    data.load(assets);
    // The program's start (main, frame 43 of a power-on): BOOT, then GAME
    // on top of it. BOOT only sets the hardware up; GAME is the game.
    if (boot) {
        request(Request::Push, "GAME", {0, 0, 1, 0});
    }
}

Story::~Story() = default;

lscr::Value Story::codeName(std::uint32_t address, int overlay) const {
    // Overlay load addresses (the ROM's overlay table).
    static const std::map<int, std::uint32_t> kLoads{
        {6, 0x020a7980},  {12, 0x0211f6e0}, {13, 0x0211dae0}, {14, 0x0211dae0}, {15, 0x0211dae0},
        {16, 0x0211e0a0}, {33, 0x0211dae0}, {35, 0x0211dae0}, {9, 0x02103fe0},  {24, 0x0211dae0},
        {25, 0x0211dae0}, {26, 0x0211dae0}, {19, 0x0211dae0}, {20, 0x0211dae0}, {21, 0x0211f060},
        {22, 0x0211f060}, {23, 0x0211f060}, {18, 0x0211df00}, {27, 0x0211df00}, {28, 0x0211df00},
        {29, 0x0211df00}, {30, 0x0211df00}, {31, 0x0211df00}, {32, 0x0211df00}};
    return lscr::Value::ofText(assets.codeText(overlay, kLoads.at(overlay), address));
}

void Story::request(Request kind, const std::string& name, LoopArgs args) {
    requests_.push_back({kind, name, args});
}

std::vector<std::string> Story::stackNames() const {
    std::vector<std::string> names;
    for (const auto& l : stack_) {
        names.push_back(l->name());
    }
    return names;
}

void Story::handleRequests() {
    // 02064580: the requests in order. Before anything replaces or covers
    // the top loop it is put to sleep - unless it is busy, in which case the
    // requests wait for a later frame. An overlay leaves it awake.
    bool handled = false;
    while (!requests_.empty()) {
        const Pending r = requests_.front();
        if (!stack_.empty() && r.kind != Request::Overlay && activated_) {
            if (stack_.back()->busy()) {
                break;
            }
            stack_.back()->deactivate();
            activated_ = false;
        }
        requests_.pop_front();
        if (r.kind != Request::Pop && skipLoop && !knows(r.name) && skipLoop(r.name)) {
            handled = true; // research runs: a loop not reconstructed yet is left out
            continue;
        }
        if (r.kind == Request::Pop || r.kind == Request::Replace) {
            if (!stack_.empty()) {
                stack_.back()->end();
                stack_.pop_back();
            }
        }
        if (r.kind != Request::Pop) {
            stack_.push_back(create(r.name, r.args));
            stack_.back()->start();
            activated_ = false;
        }
        handled = true;
    }
    // Then the top loop, if it is not awake, wakes up (+0x1c).
    if (handled && !stack_.empty() && !activated_) {
        activated_ = true;
        stack_.back()->activate();
        if (onActivate) {
            onActivate(*stack_.back());
        }
    }
}

void Story::frame(const lscr::SceneInput& input) {
    // The play time runs on with every frame (the game keeps the tick it
    // started at, 020ddc00 / 1680).
    ++playFrames;
    handleRequests();
    if (Loop* t = top()) {
        t->frame(input);
        if (const lscr::Scene* s = t->scene()) {
            for (int k = 0; k < 2; ++k) {
                brightness[size_t(k)] = s->brightness[size_t(k)].value[0];
            }
            backgroundFilter = s->backgroundFilter;
            stage = s->stage;
            guide = s->guide;
        }
    }
}

Surface Story::render() const {
    if (!stack_.empty()) {
        return stack_.back()->render();
    }
    return Surface{256, 384, std::vector<std::uint16_t>(256 * 384, 0x8000)};
}

bool Story::knows(const std::string& name) const {
    static const LoopArgs none{};
    return name == "GAME" || scriptLoopFile(assets, name) ||
           makeStoryLoop(const_cast<Story&>(*this), name, none) != nullptr;
}

std::unique_ptr<Loop> Story::create(const std::string& name, const LoopArgs& args) {
    if (name == "GAME") {
        return std::make_unique<GameLoop>(*this, args);
    }
    if (auto file = scriptLoopFile(assets, name)) {
        return std::make_unique<ScriptLoop>(*this, name, args, *file);
    }
    if (auto loop = makeStoryLoop(*this, name, args)) {
        return loop;
    }
    throw std::runtime_error("loop " + name + " is not reconstructed yet");
}

} // namespace picarat::adventure3
