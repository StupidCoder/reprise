// The third adventure's puzzle screens. See a3_puzzle_loop.hpp.
#include "puzzles/a3_puzzle_loop.hpp"
#include "puzzles/a3_engines.hpp"
#include "sound/a3_sound.hpp"
#include "sound/sound_object.hpp"
#include <algorithm>
#include <set>
#include <stdexcept>

namespace picarat::adventure3 {
namespace {
using lscr::Value;

// Overlay 35 (loaded at 0211dae0) names the game variables and flags it
// sets; the names are read from the game's code.
constexpr int kOverlay = 35;
constexpr std::uint32_t kLoad = 0x0211dae0;
constexpr std::uint32_t kRetryFlag = 0x0211f2d8;  // flag: tried again
constexpr std::uint32_t kVerdict = 0x0211f2e4;    // -1 left, 0 wrong, 1 right, 2 again
constexpr std::uint32_t kMode = 0x0211f2f4;       // what the puzzle screen is doing
constexpr std::uint32_t kMemoPad = 0x0211f338;    // the memo pad's name
constexpr std::uint32_t kAnswerer = 0x0211f25c;   // 3050 operand 3
constexpr std::uint32_t kHelp = 0x0211f268;       // 3050 operand 4
constexpr std::uint32_t kDictionary = 0x0211f274; // flag: opened from the dictionary
constexpr std::uint32_t kSoundSet = 0x0211f280;   // 3050 operand 5 or 6
constexpr std::uint32_t kBackground = 0x0211f294; // 3050 operand 7
constexpr std::uint32_t kHintCount = 0x0211f300;  // hints opened

int integer(const import::adventure3::LscrCommand& c, size_t i) {
    return i < c.operands.size() ? int(std::int32_t(c.operands[i].value)) : 0;
}
} // namespace

namespace {
// Type numbers (lt3_nazo row operand 4) of the special engines (0211e6c8).
const std::set<int> kSpecialEngines{9, 10, 11, 12, 14, 18, 19, 21, 22};
} // namespace

PuzzleSupport::PuzzleSupport(Assets& assets, const GameText& text)
    : assets_(assets), scene_(assets, text, {}) {
    for (const char* f : {"evt/lt3_common.lbin", "evt/prg/lt3_nazo_start.lbin", "evt/lt3_nazo.lbin",
                          "evt/prg/lt3_nazo_hint.lbin", "evt/prg/lt3_nazo_quit.lbin",
                          "evt/prg/lt3_nazo_input.lbin"}) {
        scene_.load(f);
    }
}

std::string PuzzleSupport::check(unsigned number) {
    int type = -1;
    const auto rows = assets_.script("rc/ge/lt3_rc/lt3_nazo.lbin");
    for (const auto& r : rows.commands) {
        if (r.opcode == 3001 && integer(r, 0) == int(number)) {
            type = integer(r, 4);
        }
    }
    if (type < 0) {
        return "not in the catalogue";
    }
    if (kSpecialEngines.contains(type) && !makeEngine(assets_, type)) {
        return "engine " + std::to_string(type) + " not reconstructed";
    }
    char path[64];
    std::snprintf(path, sizeof path, "rc/nazo/naz_sc%u/n%03u.lbin", number / 100, number);
    if (!assets_.exists(path)) {
        return "no puzzle script";
    }
    std::set<int> missing;
    // Every section a reachable statement names, from the programs' entries
    // (`own` sections first); commands whose operands start a condition have
    // that condition checked too.
    auto walk = [&](const std::vector<lscr::Section>& own, std::vector<std::string> todo) {
        auto section = [&](const std::string& name) -> const lscr::Section* {
            for (const auto& s : own) {
                if (s.name == name) {
                    return &s;
                }
            }
            return scene_.machine.find(name);
        };
        std::set<std::string> seen;
        while (!todo.empty()) {
            const std::string name = todo.back();
            todo.pop_back();
            const lscr::Section* s = seen.insert(name).second ? section(name) : nullptr;
            if (!s) {
                continue;
            }
            for (const auto& c : s->commands) {
                if (!scene_.machine.hasCommand(c.opcode)) {
                    missing.insert(c.opcode);
                }
                int first = -1; // the operand holding a condition number
                switch (c.opcode) {
                case 1010:
                case 1011:
                case 1020:
                case 1021:
                case 1022:
                case 1031:
                case 1032:
                    first = 0;
                    break;
                case 1041:
                case 1042:
                case 1120:
                case 1121:
                case 1322:
                    first = 1;
                    break;
                default:
                    break;
                }
                if (first >= 0 && size_t(first) < c.operands.size() &&
                    !c.operands[size_t(first)].variable) {
                    const int cond = c.operands[size_t(first)].constant.toInt();
                    if (!scene_.machine.hasCondition(cond)) {
                        missing.insert(cond);
                    }
                }
                for (const auto& o : c.operands) {
                    if (!o.variable &&
                        (o.constant.isText() || o.constant.kind == lscr::Value::Kind::Name)) {
                        todo.push_back(o.constant.toText());
                    }
                }
            }
        }
    };
    std::vector<std::string> entries{"MAIN", "NAZO_MAIN"};
    for (auto& s : engineSections(assets_, type)) {
        entries.push_back(s);
    }
    walk(lscr::compile(assets_.script(path), path), entries);
    const std::string result = "evt/prg/lt3_nazo_result.lbin";
    walk(lscr::compile(assets_.script(result), result), {"MAIN"});
    if (missing.empty()) {
        return {};
    }
    std::string out = "not reconstructed:";
    for (int m : missing) {
        out += " " + std::to_string(m);
    }
    return out;
}

PuzzleLoop::PuzzleLoop(Assets& assets, const GameText& text, lscr::GameState& state,
                       unsigned number, PuzzleRecord& record, Adventure3Sound* sound,
                       std::function<void(int, int)> setState)
    : assets_(assets), text_(text), state_(state), record_(record), sound_(sound),
      setState_(std::move(setState)) {
    const auto rows = assets_.script("rc/ge/lt3_rc/lt3_nazo.lbin");
    for (const auto& r : rows.commands) {
        if (r.opcode == 3001 && integer(r, 0) == int(number)) {
            id_ = number_ = int(number);
            type_ = integer(r, 4);
        }
    }
    if (!id_) {
        throw std::runtime_error("no puzzle " + std::to_string(number));
    }
    prepare();
    begin(Phase::Start);
}

PuzzleLoop::~PuzzleLoop() = default;

void PuzzleLoop::setBrightness(int level) {
    brightness_ = {level, level};
    if (scene_) {
        for (auto& b : scene_->brightness) {
            b.set({level});
        }
    }
}

void PuzzleLoop::prepare() {
    // 0211e100: the puzzle's game variables and texts.
    auto name = [&](std::uint32_t a) {
        return Value::ofText(assets_.codeText(kOverlay, kLoad, a));
    };
    state_.setFlag(name(kRetryFlag), false);
    state_.setVariable(name(kVerdict), -1, 0, true);
    state_.setVariable(name(kMode), 0, 0, true);
    if (record_.state == 0) {
        if (setState_) {
            setState_(number_, 1);
        } else {
            record_.state = 1;
        }
    }
    state_.setVariable(name(kHintCount), record_.state == 2 ? 0 : record_.hints, 0, true);

    // rc/nazo/?/naz_df<N%10>/nNNN: 3050 header, 3051 statement, 3052
    // correct, 3053 wrong, 3054..3057 the hints (0211db04 and neighbours).
    char path[64];
    std::snprintf(path, sizeof path, "rc/nazo/?/naz_df%d/n%03d.lbin", id_ % 10, id_);
    const auto texts = assets_.script(path);
    for (const auto& c : texts.commands) {
        if (c.opcode == 3050 && c.operands.size() >= 10) {
            state_.setVariable(name(kAnswerer), integer(c, 3), 0, true);
            state_.setVariable(name(kHelp), integer(c, 4), 0, true);
            const bool dictionary = state_.flag(name(kDictionary));
            state_.setVariable(name(kSoundSet), integer(c, dictionary ? 5 : 6), 0, true);
            state_.setVariable(name(kBackground), integer(c, 7), 0, true);
            strings_[0] = texts.text(c.operands[0]) + ".lbin";
            // The question pictures: the language's own folder when the
            // header says so, else the pictures shared by the European
            // languages (this German game; English would use "nazo/en").
            for (int k = 1; k <= 2; ++k) {
                const std::string folder = integer(c, size_t(7 + k)) ? "nazo/?/" : "nazo/eu/";
                strings_[size_t(k)] = folder + texts.text(c.operands[size_t(k)]) + ".cimg";
            }
        } else if (c.opcode >= 3051 && c.opcode <= 3057 && !c.operands.empty()) {
            strings_[size_t(c.opcode - 3048)] = texts.text(c.operands[0]);
        }
    }
}

void PuzzleLoop::begin(Phase phase) {
    phase_ = phase;
    if (phase == Phase::Done) {
        return;
    }
    if (scene_) {
        brightness_ = {scene_->brightness[0].value[0], scene_->brightness[1].value[0]};
    }
    lscr::Scene::Host host;
    host.puzzleState = [this] {
        return record_.state;
    };
    host.puzzleFailures = [this] {
        return record_.failures;
    };
    // The record keeps three bits each for failures and hints (020da8b0,
    // 020da928), so both stop at 7.
    host.puzzleHints = [this] {
        return record_.hints;
    };
    host.setPuzzleHints = [this](int n) {
        record_.hints = std::clamp(n, 0, 7);
    };
    host.pieces = &pieces_;
    host.memos = &memos_;
    host.sound = sound_;
    host.puzzleText = [this](int i) {
        return i >= 0 && i < 16 ? strings_[size_t(i)] : "";
    };
    // The scene of the engine phase gets the puzzle's engine, a fresh one
    // each time.
    auto engine = phase == Phase::Engine ? makeEngine(assets_, type_) : nullptr;
    scene_ = std::make_unique<lscr::Scene>(assets_, text_, host, &state_);
    engine_ = std::move(engine);
    scene_->engine = engine_.get();
    for (int s = 0; s < 2; ++s) {
        scene_->brightness[size_t(s)].set({brightness_[size_t(s)]});
    }
    scene_->load("evt/lt3_common.lbin");
    std::string section;
    switch (phase) {
    case Phase::Start:
        scene_->load("evt/prg/lt3_nazo_start.lbin");
        section = "MAIN";
        break;
    case Phase::Engine: {
        // 0211e6c8 / 0211eda8: the common engine or a special one (a
        // separate overlay each).
        if (kSpecialEngines.contains(type_) && !engine_) {
            throw std::runtime_error("puzzle type " + std::to_string(type_) +
                                     " uses an engine that is not reconstructed yet");
        }
        auto name = [&](std::uint32_t a) {
            return Value::ofText(assets_.codeText(kOverlay, kLoad, a));
        };
        state_.setFlag(name(kRetryFlag), false);
        state_.setVariable(name(kMode), 0, 0, true);
        state_.setVariable(name(kVerdict), -1, 0, true);
        scene_->load("evt/lt3_nazo.lbin");
        scene_->load("evt/prg/lt3_nazo_hint.lbin");
        scene_->load("evt/prg/lt3_nazo_quit.lbin");
        scene_->load("evt/prg/lt3_nazo_input.lbin");
        char path[64];
        std::snprintf(path, sizeof path, "rc/nazo/naz_sc%d/%s", id_ / 100, strings_[0].c_str());
        scene_->load(path);
        // 0211e4c4: the puzzle screen's memo pad (020f316c, ov6
        // lt3CPaintMemo), not taking the pen until the memo starts it.
        if (lscr::Object* pad = scene_->createCanvas(assets_.codeText(kOverlay, kLoad, kMemoPad))) {
            pad->memo = true;
            pad->switches[1].on = false;
        }
        section = "NAZO_MAIN";
        break;
    }
    case Phase::Result:
        scene_->load("evt/prg/lt3_nazo_result.lbin");
        section = "MAIN";
        break;
    case Phase::Done:
        break;
    }
    const std::vector<Value> vars{Value::of(id_), Value::of(number_), Value::of(type_)};
    scene_->start(section, vars);
    if (onScene) {
        onScene(*scene_);
    }
}

void PuzzleLoop::ended() {
    // 0211e6c8: after the title card the engine; after the engine the
    // verdict unless the player left; after the verdict the record, and
    // either the end or another try.
    const Value verdictName = Value::ofText(assets_.codeText(kOverlay, kLoad, kVerdict));
    const int verdict = state_.variable(verdictName);
    switch (phase_) {
    case Phase::Start:
        begin(Phase::Engine);
        break;
    case Phase::Engine:
        begin(verdict == -1 ? Phase::Done : Phase::Result);
        break;
    case Phase::Result:
        if (verdict == 1) {
            if (setState_) {
                setState_(number_, 2);
            } else {
                record_.state = 2;
            }
        } else if (record_.state != 2) {
            record_.failures = std::min(record_.failures + 1, 7);
        }
        begin(verdict == 2 ? Phase::Start : Phase::Done);
        break;
    case Phase::Done:
        break;
    }
}

std::map<int, std::array<int, 3>> soundSets(Assets& assets) {
    std::map<int, std::array<int, 3>> sets;
    for (const auto& r : assets.script("rc/ge/lt3_rc/lt3_snd.lbin").commands) {
        if (r.opcode == 5301 && r.operands.size() >= 4) {
            sets[integer(r, 0)] = {integer(r, 1), integer(r, 2), integer(r, 3)};
        }
    }
    return sets;
}

void PuzzleLoop::frame(const lscr::SceneInput& input) {
    if (sound_ && drivesSound) {
        sound_->sound().frame();
    }
    if (!scene_ || phase_ == Phase::Done) {
        return;
    }
    scene_->frame(input);
    if (!scene_->running()) {
        ended();
    }
}

Surface PuzzleLoop::render() const {
    return scene_ ? scene_->render()
                  : Surface{256, 384, std::vector<std::uint16_t>(256 * 384, 0x8000)};
}

} // namespace picarat::adventure3
