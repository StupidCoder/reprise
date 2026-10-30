#pragma once
// The fourth adventure as a whole: a stack of loops run by a list of
// requests, from the logos to the end credits.
//
// How the game is put together
// ----------------------------
// Everything the player sees is one loop at a time. Loops sit on a stack;
// the top one runs. Scripts and loops ask for loops by number (the logos
// 2010, the title 2000, an event 1200, a room 1100, a puzzle 3000, the bag
// and the other menus 21xx..26xx); the requests are carried out at the
// start of the next frame, in order:
//
//   push  the loop below is suspended (what it showed on top goes) and a
//         new loop is made and started on top of it;
//   pop   the top loop is suspended and ends; the one below carries on.
//
// After the requests, the loop that is now on top resumes; once nothing
// loads, it begins (most loops start their script's Main then). The
// bottom of the stack is the game loop: it loads the game's data and the
// resident scripts, and whenever it comes to the top it asks for the title
// (with the logos in front of it at first). When it comes to the top a
// second time it ends itself, and an empty stack starts a new game loop.
//
// Reconstructed from ov6 (gnCLoopManager: 020aa650 requests, 020aae6c
// begin, 020aaee4 frame, 020aaf0c drawing), ov7 (020b6820 the main loop,
// 020c971c the loop numbers, 020cc2e0 pop), ov13 (lt4CLoopGame), ov14
// (lt4CLoopScript) and ov16 (lt4CLoopEvent). See
// .analysis/adventure4/docs/story.md.
#include "system/a4_loop.hpp"
#include "system/a4_saves.hpp"
#include <deque>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace picarat {
class Adventure4Sound;
}

namespace picarat::adventure4 {

class Game;

// The loop numbers of ov7 020c971c.
enum LoopId : int {
    GameLoopId = 1000,
    PlaceLoopId = 1100,
    EventLoopId = 1200,
    TitleLoopId = 2000,
    LogoLoopId = 2010,
    PuzzleLoopId = 3000,
};

// A loop of the story: knows the game it runs in and its number.
class StoryLoop : public Loop {
public:
    StoryLoop(Game& game, int id, std::span<const Value> values);

protected:
    Game& game_;
};

class Game final : public LoopRequests {
public:
    // `sound`: the sound manager (null: silent). `withGameLoop`: an empty
    // stack starts the game loop (ov7 020b6820); a stack of its own (a
    // puzzle from the index) runs without one.
    // `savesFolder`: where the save slots live (empty: in memory only).
    Game(Assets& assets, Adventure4Sound* sound = nullptr, bool withGameLoop = true,
         std::filesystem::path savesFolder = {});
    ~Game() override;

    // The game loop's resident scripts and effect bank (ov13 02105370).
    void loadResident();

    // ov7 020b6820: one frame of the main loop.
    // `keys`: the buttons held (KEYINPUT bits, set = held).
    void frame(bool touching, int x, int y, std::uint16_t keys = 0);

    void render(Surface& out) const { scene.render(out); }

    // A typed key for the top loop (see Loop::keyboard).
    void keyboard(const NameInput& key) {
        if (Loop* t = top()) {
            t->keyboard(key);
        }
    }

    // LoopRequests.
    bool pending() const override { return !requests_.empty(); }

    bool request(int id, std::array<int, 6> values) override;
    bool pop() override;
    bool restart() override;

    int depth() const override { return int(stack_.size()); }

    Loop* top() const { return stack_.empty() ? nullptr : stack_.back().get(); }

    // The loop numbers from the bottom of the stack up.
    std::vector<int> stackIds() const {
        std::vector<int> ids;
        for (const auto& l : stack_) {
            ids.push_back(l->id);
        }
        return ids;
    }

    // The same with each loop's first value ("1200:1000").
    std::string stackText() const {
        std::string s;
        for (const auto& l : stack_) {
            s += (s.empty() ? "" : " ") + std::to_string(l->id) + ":" +
                 std::to_string(l->values()[0].toInt());
        }
        return s;
    }

    // The app's boot: the title without the logos in front of it.
    bool startAtTitle = false;

    // The app's checkpoint (a host convenience; see checkpoint()): written
    // to `checkpointFile` (none: not written). With `continueFrom` the game
    // loop restores it and opens its room instead of the title - the
    // title's Continue without its recap event.
    std::filesystem::path checkpointFile;
    std::optional<Checkpoint> continueFrom;

    // Loop numbers the game has no loop for yet: the request is refused and
    // reported here (research runs).
    std::function<void(int id)> unknownLoop;

    Assets& assets;
    Scene scene;
    GameData data;
    SaveSlots saves;
    Services services{scene, data};
    Adventure4Sound* sound;

private:
    // 020aa650's request kinds: 0 pop, 1 restart, 2 push.
    struct Request {
        int kind = 0;
        int id = 0;
        std::array<Value, 8> values{};
    };

    // 020aa650: carries out the requests; false while the top loop holds
    // them.
    void carryOut();
    std::unique_ptr<Loop> make(int id, std::span<const Value> values);
    // After a frame: once a room entered plainly is ready for the player,
    // its checkpoint is written (once each time it comes to the top).
    void checkpoint();

    std::vector<std::unique_ptr<Loop>> stack_;
    bool withGameLoop_ = true;
    std::deque<Request> requests_;
    // Counts the times a loop came to the top; the count the last
    // checkpoint was written in; frames the room has been ready in a row.
    std::uint64_t turn_ = 0, checkpointTurn_ = 0;
    int readyFrames_ = 0;
};

} // namespace picarat::adventure4
