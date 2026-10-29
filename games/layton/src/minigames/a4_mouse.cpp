// The whack-a-mouse. See a4_mouse.hpp.
#include "minigames/a4_mouse.hpp"
#include <algorithm>
#include <cstdio>

namespace picarat::adventure4 {

namespace {
constexpr const char* kData = "res/sub/@/lt4_sub_mouse.cb"; // 02107100

// The game's shared random numbers (0207b7e0: the next number below n).
int below(gss::Machine& m, int n) {
    return int(m.random(std::uint32_t(n)));
}
} // namespace

MouseLoop::MouseLoop(Game& game, int id, std::span<const Value> values)
    : ScriptLoop(game, id, values) {
    load();
}

int MouseLoop::spotByName(const std::string& name) const {
    const std::uint32_t h = nameHash(name);
    for (std::size_t i = 0; i < spots_.size(); ++i) {
        if (spots_[i].hash == h) {
            return int(i);
        }
    }
    return -1;
}

int MouseLoop::patternByName(const std::string& name) const {
    const std::uint32_t h = nameHash(name);
    for (std::size_t i = 0; i < patterns_.size(); ++i) {
        if (patterns_[i].hash == h) {
            return int(i);
        }
    }
    return -1;
}

void MouseLoop::load() {
    // 021063d8 (the loop's start, 021066c8): the data with handlers
    // 5300 (sizes), 5310 a spot (021057c4), 5320 a pattern (02105adc),
    // 5322 its steps (02105d34), 5330 a schedule (02105f40), 5332 its
    // waves (02106198), 5340 a title (02106360); the first schedule is the
    // one played (021067c0). Spots and patterns are kept sorted by their
    // names' hashes (the lists are searched by hash), the rest in order.
    const auto list = game_.assets.data(kData);

    struct PendingStep {
        int pattern;
        Step step;
        std::vector<std::string> spots;
    };

    struct PendingWave {
        Wave wave;
        std::vector<std::string> patterns;
    };

    std::vector<PendingStep> steps;
    std::vector<PendingWave> waves;
    std::vector<std::string> patternNames;
    int schedules = 0;
    for (const auto& cmd : list.commands) {
        auto integer = [&](unsigned i) {
            return i < cmd.count ? std::int32_t(list.operands[cmd.first + i].value) : 0;
        };
        auto text = [&](unsigned i) {
            return i < cmd.count ? list.text(list.operands[cmd.first + i]) : std::string();
        };
        switch (cmd.opcode) {
        case 5310:
            if (cmd.count > 4) {
                Spot s;
                s.name = text(0);
                s.hash = nameHash(s.name);
                s.place = text(1);
                s.layer = integer(2);
                s.moves = unsigned(integer(3));
                s.block = text(4);
                for (unsigned i = 5; i < cmd.count; ++i) {
                    s.extra.push_back(Value::ofInt(integer(i)));
                }
                spots_.push_back(std::move(s));
            }
            break;
        case 5320:
            patternNames.push_back(text(0));
            break;
        case 5322:
            if (cmd.count > 4 && !patternNames.empty()) {
                PendingStep p{int(patternNames.size()) - 1, {}, {}};
                for (unsigned k = 0; k < 4; ++k) {
                    p.step.mice[k] = std::uint8_t(integer(k));
                    p.step.total = std::uint8_t(p.step.total + p.step.mice[k]);
                }
                p.step.moves = unsigned(integer(4));
                for (unsigned i = 5; i < cmd.count; ++i) {
                    p.spots.push_back(text(i));
                }
                steps.push_back(std::move(p));
            }
            break;
        case 5330:
            ++schedules;
            break;
        case 5332:
            if (cmd.count > 2 && schedules == 1) {
                PendingWave w;
                w.wave.set = integer(0);
                w.wave.most = integer(1);
                for (unsigned i = 2; i < cmd.count; ++i) {
                    w.patterns.push_back(text(i));
                }
                waves.push_back(std::move(w));
            }
            break;
        case 5340:
            if (cmd.count > 2) {
                titles_.push_back({integer(1), integer(0), text(2)});
            }
            break;
        default:
            break;
        }
    }
    std::stable_sort(spots_.begin(), spots_.end(),
                     [](const Spot& a, const Spot& b) { return a.hash < b.hash; });
    for (const std::string& name : patternNames) {
        patterns_.push_back({nameHash(name), {}});
    }
    for (PendingStep& p : steps) {
        for (const std::string& name : p.spots) {
            if (const int s = spotByName(name); s >= 0) {
                p.step.spots.push_back(s);
            }
        }
        patterns_[std::size_t(p.pattern)].steps.push_back(std::move(p.step));
    }
    std::stable_sort(patterns_.begin(), patterns_.end(),
                     [](const Pattern& a, const Pattern& b) { return a.hash < b.hash; });
    for (PendingWave& w : waves) {
        for (const std::string& name : w.patterns) {
            if (const int p = patternByName(name); p >= 0) {
                w.wave.patterns.push_back(p);
            }
        }
        waves_.push_back(std::move(w.wave));
    }
}

bool MouseLoop::nextStep() {
    // 021067e4: the next step of the wave's pattern (a pattern picked at
    // random from the wave's when the last one ended). Its mice in random
    // order (each inserted at a random place, then 256 random swaps), at
    // most the wave's number of them; on free spots (no object of the
    // spot's place) that allow one of the step's moves - the step's own
    // spots or, without any, all - shuffled the same way. After the
    // pattern's last step the next wave follows (round the schedule).
    out_.clear();
    if (waves_.empty()) {
        return false;
    }
    gss::Machine& rng = scene_.machine;
    const Wave& wave = waves_[std::size_t(wave_)];
    if (pattern_ < 0) {
        if (wave.patterns.empty()) {
            return false;
        }
        pattern_ = wave.patterns[std::size_t(below(rng, int(wave.patterns.size())))];
    }
    const Pattern& pattern = patterns_[std::size_t(pattern_)];
    if (!pattern.steps.empty() && wave.most != 0) {
        const Step& step = pattern.steps[std::size_t(step_)];
        std::vector<int> kinds;
        std::vector<int> free;
        int count = 0;
        if (step.total != 0) {
            for (int k = 0; k < 4; ++k) {
                for (int n = 0; n < step.mice[std::size_t(k)]; ++n) {
                    if (kinds.empty()) {
                        kinds.push_back(k);
                    } else {
                        const int at = below(rng, int(kinds.size()) + 1);
                        kinds.insert(kinds.begin() + at, k);
                    }
                }
            }
            if (!kinds.empty()) {
                for (int i = 0; i < 0x100; ++i) {
                    const int a = below(rng, int(kinds.size()));
                    const int b = below(rng, int(kinds.size()));
                    if (a != b) {
                        std::swap(kinds[std::size_t(a)], kinds[std::size_t(b)]);
                    }
                }
            }
            count = std::min(int(kinds.size()), wave.most);
            auto allowed = [&](int s) {
                const Spot& spot = spots_[std::size_t(s)];
                return (spot.moves & step.moves) != 0 && !scene_.find(spot.place);
            };
            if (step.spots.empty()) {
                for (int s = 0; s < int(spots_.size()); ++s) {
                    if (allowed(s)) {
                        free.push_back(s);
                    }
                }
            } else {
                for (int s : step.spots) {
                    if (allowed(s) && std::find(free.begin(), free.end(), s) == free.end()) {
                        free.push_back(s);
                    }
                }
            }
            if (!free.empty()) {
                for (int i = 0; i < 0x100; ++i) {
                    const int a = below(rng, int(free.size()));
                    const int b = below(rng, int(free.size()));
                    if (a != b) {
                        std::swap(free[std::size_t(a)], free[std::size_t(b)]);
                    }
                }
            }
        }
        count = std::min(count, int(free.size()));
        for (int i = 0; i < count && out_.size() < 64; ++i) {
            out_.push_back({free[std::size_t(i)], kinds[std::size_t(i)], wave.set});
        }
    }
    if (++step_ >= int(pattern.steps.size())) {
        step_ = 0;
        wave_ = (wave_ + 1) % int(waves_.size());
        pattern_ = -1;
    }
    return true;
}

bool MouseLoop::command(gss::Call& c, int op) {
    const std::size_t n = c.count() > 0 ? c.count() - 1 : 0;
    auto result = [&](Value v) {
        c.result = std::move(v);
        return true;
    };
    auto out = [&](std::size_t i, Value v) {
        if (Value* r = c.reg(i)) {
            *r = std::move(v);
        }
    };
    switch (op) { // 02106bc4
    case 0:
        return result(Value::ofInt(nextStep() ? 1 : 0));
    case 1: // how many come out
        return result(Value::ofInt(int(out_.size())));
    case 2: { // {i, -> place, -> kind (1..4), -> layer x 2, -> block, -> 4 values}
        if (n < 9) {
            return false;
        }
        const int i = c.integer(1);
        if (i < 0 || i >= int(out_.size())) {
            return result(Value::ofInt(0));
        }
        const Mouse& m = out_[std::size_t(i)];
        const Spot& s = spots_[std::size_t(m.spot)];
        char block[96];
        std::snprintf(block, sizeof block, "%s%d", s.block.c_str(), m.set); // 0210711c
        out(2, Value::ofText(s.place));
        out(3, Value::ofInt(m.kind + 1));
        out(4, Value::ofInt(s.layer << 1));
        out(5, Value::ofText(block));
        for (std::size_t k = 0; k < 4; ++k) {
            out(6 + k, k < s.extra.size() ? s.extra[k] : Value{});
        }
        return result(Value::ofInt(1));
    }
    case 3: // the titles
        return result(Value::ofInt(int(titles_.size())));
    case 4: { // {title}: its seconds (30 for none)
        if (n == 0) {
            return false;
        }
        const int i = c.integer(1);
        return result(
            Value::ofInt(i >= 0 && i < int(titles_.size()) ? titles_[std::size_t(i)].seconds : 30));
    }
    case 5: { // {title}: the score it needs
        if (n == 0) {
            return false;
        }
        const int i = c.integer(1);
        return result(
            Value::ofInt(i >= 0 && i < int(titles_.size()) ? titles_[std::size_t(i)].score : 0));
    }
    case 6: { // {title}: its name
        if (n == 0) {
            return false;
        }
        const int i = c.integer(1);
        return result(i >= 0 && i < int(titles_.size())
                          ? Value::ofText(titles_[std::size_t(i)].text)
                          : Value{});
    }
    default:
        return ScriptLoop::command(c, op);
    }
}

} // namespace picarat::adventure4
