// =============================================================================================
// BookActor.hpp - the course book's actors: layouts with state machines that talk by messages
// =============================================================================================
//
// The course book (the game's CourseSelectScene, set up at 0x1b5268) is a handful of small state
// machines: the scene, two sequencers, the book on the top screen, one actor per page and per
// page part, the touch screen and its buttons. Each machine is a "nerve" keeper (game/Nerve.hpp):
// one state function runs per 30 Hz tick with a step counter. The machines notify each other with
// messages (the dispatcher 0x27f5cc delivers them at once, inside the sender's tick) and call
// each other's methods directly.
//
// A layout actor owns one layout instance and plays its animations with the game's rules
// (render/UiLayout.hpp, Instance::start ...). Its layout is drawn, and its state machine runs,
// only after it appeared (0x2faafc).
//
// Messages carry the game's type numbers (the predicates 0x3355c4, 0x336d24, ... compare one word
// each); see .analysis/docs/book.md for each sender and receiver.
// =============================================================================================
#pragma once
#include "game/Nerve.hpp"
#include "render/UiLayout.hpp"

#include <cstdio>
#include <string>

namespace book {

class Sound;

enum class Msg : int {
    Appear = 0x00,            // scene → book, touch screen: appear
    Appeared = 0x01,          // book → scene: AppearBook has ended
    BottomIntro = 0x03,       // opening sequencer → touch screen
    Leave = 0x05,             // scene → touch screen: leave (back to the title; as after Chosen)
    Open = 0x06,              // opening sequencer → book: open the book
    Opened = 0x07,            // book → scene: the book is open; sequencer → touch screen: curtain
    BottomReady = 0x08,       // touch screen → scene: curtain and buttons are in
    Close = 0x09,             // scene → book: close the book (back to the title)
    Closed = 0x0a,            // book → scene: the book has closed
    Chosen = 0x0b,            // input → book, touch screen, scene: a course was chosen
    ChosenDone = 0x0c,        // book → scene: the chosen page's press has played
    EnableInput = 0x0e,       // scene → book: page input on, arrows in
    DisableInput = 0x0f,      // scene → book: page input off
    CourseOpenDone = 0x10,    // book → sequencer: the opened page has shown itself
    ShowOpen = 0x14,          // scene → book: show the book open at once (no opening animation)
    CourseOpen = 0x21,        // sequencer → book: open the page of course `arg`
    MarkLocked = 0x23,        // scene → book: course `arg` waits for gems
    TurnDone = 0x28,          // book → scene: a page turn has settled
    NewLoop = 0x29,           // scene → book: every page starts its NEW loop
    CloseWipeFilter = 0x2b,   // scene → book: end the wipe filter if it waits
    SpotlightAppear = 0x2c,   // sequencer → book: the course-open spotlight closes in
    SpotlightEnd = 0x2d,      // sequencer → book: the spotlight opens again
};

const char* msgName(Msg m);

// Research: a line per layout action and message, by tick, for the comparison with the original's
// traced runs (DIORAMA_BOOK_JOURNAL=file; .analysis/tools/compare_book.py).
class Journal {
public:
    explicit Journal(const char* path);
    ~Journal();
    bool on() const { return f_ != nullptr; }
    int tick = 0;
    void action(const std::string& actor, const std::string& tag, const std::string& slot, float frame);
    void message(const std::string& from, const std::string& to, Msg m, int arg);

private:
    FILE* f_ = nullptr;
};

class LayoutActor {
public:
    LayoutActor(std::string name, captain_toad::ui::Instance* layout, Journal& journal)
        : name_(std::move(name)), layout_(layout), journal_(journal) {
        if (layout_) {
            layout_->setShown(false);
        }
    }
    virtual ~LayoutActor() = default;

    // One tick of the actor's state machine (only once it appeared).
    virtual void update() {}

    const std::string& name() const { return name_; }
    captain_toad::ui::Instance* layout() const { return layout_; }
    bool alive() const { return alive_; }
    // A part actor appears with its page.
    void appearPart() { appearLayout(); }

    // 0x2fad5c: start `tag` in `slot`; nothing happens when the layout lacks the tag.
    bool start(const std::string& tag, const std::string& slot = "", float frame = 0);
    // 0x2d5318: bind `tag` held at `frame`.
    bool hold(const std::string& tag, const std::string& slot = "", float frame = 0);
    // 0x2f32f4: stop `tag` in `slot` (every slot when empty).
    void stop(const std::string& tag, const std::string& slot = "") {
        if (layout_) {
            layout_->stop(tag, slot);
        }
    }
    // 0x2fa6c4 (tag empty: the actor's last started animation).
    bool ended(const std::string& tag = "", float margin = 0) const { return !layout_ || layout_->ended(tag, margin); }
    bool playing(const std::string& tag) const { return layout_ && layout_->playing(tag); }
    // 0x2f8358: a pane's visibility.
    void showPane(const std::string& pane, bool visible) {
        if (layout_) {
            layout_->writeVisible(pane, visible);
        }
    }
    // The actor's sound user (SeDataBase name; BookSound.hpp): its started actions reach the sound
    // keeper, and playSound() plays its PlayInfos.
    void setSound(Sound* sound, std::string db) {
        sound_ = sound;
        soundDb_ = std::move(db);
    }
    void playSound(const std::string& playInfo, float param = -1);

    // 0x2faafc: from now on the layout is drawn and the actor updated.
    void appearLayout() {
        alive_ = true;
        if (layout_) {
            layout_->setShown(true);
        }
    }

protected:
    std::string name_;
    captain_toad::ui::Instance* layout_;
    Journal& journal_;
    bool alive_ = false;
    Sound* sound_ = nullptr;
    std::string soundDb_;
};

}   // namespace book
