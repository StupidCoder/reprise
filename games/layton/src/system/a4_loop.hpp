#pragma once
// The fourth adventure's loops: everything the player sees is one loop at a
// time (the logos, the title, a scene, a room, a puzzle, a menu), each
// running the game's scripts on two threads.
//
// How a loop lives
// ----------------
// Loops sit on a stack (see story/a4_game.hpp). A loop is made and started
// when it is pushed: it opens a scope level of its own (what its scripts
// make there lasts as long as the loop) and hands its eight values to both
// threads. Whenever it comes to the top it resumes: a second scope level
// opens for what it shows while on top. Once nothing is loading, it begins:
// most loops start their script's Main here. When another loop covers it,
// it is suspended: the second level goes (its objects, pictures, scripts),
// its threads keep their place. When it is popped it ends and the first
// level goes as well.
//
// Reconstructed from ov6 (gnCLoop 020a9a9c..020a9c84: flags +0x30, bit 0
// started, bit 1 on top, bit 2 begun; resume count +0x2c) and ov7
// (lt4CLoop: 020e2004 start, 020e20c8 end, 020e2128 resume, 020e21a0
// suspend, 020e1b00 "still loading", 020e22e4 frame, 020e1dec thread run).
// See .analysis/adventure4/docs/story.md.
#include "system/a4_services.hpp"
#include "system/game_types.hpp"
#include <memory>

namespace picarat::adventure4 {

using gss::Value;

class Loop : public LoopHost {
public:
    // 020e2004 (start): a scope level of the loop's own; the pen counts from
    // its next press; both threads get the loop's eight values.
    Loop(Scene& scene, Services& services, std::span<const Value> arguments);
    // 020e20c8 (end): everything the loop made goes.
    ~Loop() override;

    // 020e2128 (resume, vtable +0x20): on top again; a second scope level
    // for what it shows there.
    virtual void resume();
    // 020e21a0 (suspend, +0x24): covered by another loop; what it showed
    // while on top goes.
    virtual void suspend();
    // vtable +0x28: once on top and nothing loads (ov6 020aae6c). Play
    // loops start Main of the newest script (ov17 02105cb4).
    virtual void begin();

    // vtable +0x14: a pop waits while this is true (the manager asks
    // again next frame).
    virtual bool holdsEnd() const { return false; }

    // 020e1d80: starts a thread at a block of `program` (at a label when
    // given).
    bool start(const gss::Program& program, std::string_view block, int thread = 0,
               std::string_view label = {});
    // 020e22e4: one frame, with the pen on the lower screen or not.
    void frame(bool touching = false, int x = 0, int y = 0);

    // 020e1d34: either thread runs.
    bool running() const;
    // 020e1d0c: thread `t` runs.
    bool running(int t) const;

    bool command(gss::Call& call, int id) override;

    void setBusy(bool busy) override { busy_ = busy; }

    bool callback(int thread, const std::string& block, std::span<const gss::Value> args) override;
    bool startThread(int thread, const std::string& block) override;

    gss::Value loopValue(int index) const override {
        return index >= 0 && index < 8 ? values_[std::size_t(index)] : gss::Value{};
    }

    bool busy() const { return busy_; }

    bool inputOff() const override { return busy_; }

    bool holding() const override { return target_.object && target_.area >= 0; }

    // Typed keys from the computer keyboard (a native convenience; only the
    // name keyboard takes them).
    virtual void keyboard(const NameInput& key) { (void)key; }

    // The loop's values (+4: eight of them; requests fill the first six).
    const std::array<Value, 8>& values() const { return values_; }

    // The loop's number (ov7 020c971c) and the manager's state (gnCLoop
    // +0x2c / +0x30).
    int id = 0;
    int resumes = 0;
    bool onTop = false, begun = false;
    // Events and play loops end themselves after their updates once begun,
    // with no requests waiting and Main done (vtable +0x5c: ov16 02104e24,
    // ov17 02105cdc).
    bool endsWithMain = false;
    bool beginsWithMain = false;

    std::array<std::unique_ptr<gss::Thread>, 2> threads;
    std::function<bool(gss::Call&, int)> commands; // 2400 handler of the loop kind

protected:
    // Hooks the puzzle loops override (lt4CLoop vtable, empty in the base,
    // called from 020e22e4): +0x54 after the pen went to its target (only
    // while the loop takes input), +0x5c after the scene's updates, +0x60
    // at the end of the frame.
    virtual void afterPen() {}

    virtual void afterUpdate() {}

    virtual void endFrame() {}

    Scene& scene_;
    Services& services_;
    std::array<Value, 8> values_{};

private:
    // What the pen holds (loop +0xfc): an object by name hash and the area
    // it touched (-1: the pen left it this frame, 3: dragged off it).
    struct Target {
        std::uint32_t hash = 0;
        bool object = false;
        int area = -1;
        bool paint = false; // a paint (kind 2) rather than an object (kind 0)
    };

    Target dispatch(const Target& previous) const;
    void transition(Target& current, const Target& previous);
    void paintTouch(Target& current, const Target& previous);
    void paintTransition(Target& current, const Target& previous);

    const gss::Program* program_ = nullptr;
    bool busy_ = false;
    Target target_;
    int level_ = 0;     // the scope level below the loop's own (+0x44)
    int topLevel_ = -1; // the one below its on-top level (+0x46), -1 none
    int upper_ = 1;     // the screen on top when it started (+0x38)
};

} // namespace picarat::adventure4
