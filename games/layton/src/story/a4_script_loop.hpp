#pragma once
// The fourth adventure's script loop (ov14 lt4CLoopScript): a loop that runs
// Main of the script named by its seventh value. The menus, the secrets and
// the minigames are these; the minigames add native commands of their own
// (2400 sub-commands) in classes derived from it.
#include "story/a4_game.hpp"

namespace picarat::adventure4 {

// ---------------------------------------------------------- script loop

// lt4CLoopScript (ov14): runs Main of the script named by its seventh
// value; the menus and many screens are these.
class ScriptLoop : public StoryLoop {
public:
    ScriptLoop(Game& game, int id, std::span<const Value> values) : StoryLoop(game, id, values) {}

    bool command(gss::Call& call, int id) override { return command2400(call, id); }

    void resume() override {
        // 02104d68: the scripts the values name load again on top
        // ("evt/prg/%s", 02104dfc).
        Loop::resume();
        for (std::size_t i = 6; i < 8; ++i) {
            if (values_[i].isText()) {
                scene_.loadScript("evt/prg/" + values_[i].text);
            }
        }
    }

    void begin() override {
        // 02104e18: Main from the newest script.
        if (const gss::Program* p = scene_.newestScript()) {
            start(*p, "Main");
        }
    }

protected:
    void endFrame() override {
        // 02104e40 (vtable +0x60, the end of the frame): once begun, with
        // no requests waiting and both threads done, the loop ends itself.
        if (begun && !game_.pending() && !running()) {
            game_.pop();
        }
    }

public:
private:
    // 02104e94: a list of values the menu scripts keep while the loop
    // lives (2400 1000 clear, 1001 {n} n values, 1002 {i} read, 1003
    // {i, value} write).
    bool command2400(gss::Call& c, int op) {
        switch (op) {
        case 1000:
            list_.clear();
            c.result = Value::ofInt(1);
            return true;
        case 1001:
            if (c.count() < 2) {
                return false;
            }
            list_.assign(std::size_t(std::max(0, c.integer(1))), Value::ofInt(0));
            c.result = Value::ofInt(list_.empty() ? 0 : 1);
            return true;
        case 1002: {
            if (c.count() < 2) {
                return false;
            }
            const auto i = std::size_t(c.integer(1));
            c.result = i < list_.size() ? list_[i] : Value{};
            return true;
        }
        case 1003: {
            if (c.count() < 3) {
                return false;
            }
            const auto i = std::size_t(c.integer(1));
            if (i >= list_.size()) {
                return false;
            }
            list_[i] = c.at(2);
            c.result = Value::ofInt(1);
            return true;
        }
        default:
            return false;
        }
    }

protected:
    std::vector<Value> list_;
};

} // namespace picarat::adventure4
