#pragma once
// The first adventure's save and load screens (ARM9: saving from an event
// 020287ac, from the trunk 02023340; loading from the title 0202dd5c). All
// three show the same list of slots, drawn by copies of one function on
// different objects (02028318, 02022e9c, 0202d8c0).
//
// How it plays
// ------------
// The lower screen shows the three slots: for a slot in use the player's
// name and the place where it was saved, the play time, the puzzles solved
// and found and a "cleared" mark once the story was finished; an empty slot
// says so. Touching a slot saves into it - a slot in use first asks whether
// to overwrite it, naming its player. While it saves a "saving" picture lies
// over the slots for a second, then the slots show the new line for another
// second and the screen closes. The button at the lower right closes it
// without saving.
#include "system/a1_ui.hpp"
#include <functional>

namespace picarat::adventure1 {
class Story;

// The list of slots and the close button.
class SlotList {
public:
    explicit SlotList(Story& story);
    // What the owner draws with the list (the trunk's page, the title's
    // logo); may be empty.
    std::function<void()> drawOwner;

protected:
    Story& story;
    SceneKit& kit;
    Button slots_[3];                // event +0xc, trunk +0x74, title +0x20
    SpriteButton back_;              // event +0x18, trunk +0x80, title +0x2c
    SceneKit::SpriteObject numbers_; // file_numbers.spr
    SceneKit::SpriteObject message_; // the screen's title (?/save_mes.spr, ?/load_mes.spr)

    // 0200b968: the names and places, or "no data".
    void writeSlotTexts();
    // 02028318: the cleared marks, play times, puzzle counts, the title,
    // the slots' backings (0200bb00), the close button.
    void draw();
    // The display set-up of all three (0200436c, 02004290): the 3D layer
    // (digits, backings) blends 15:5 over the picture; priorities 3D 2,
    // text 1, BG2 0, BG3 3. The title sprite centred at row `titleY`.
    void enter(const std::string& title, int titleY);
};

class SaveScreen : SlotList {
public:
    enum class From { Event, Trunk };
    // `window` is the owner's question window (prize_window.spr "window" at
    // (0x18, 0x38) in both owners), shown behind the overwrite question.
    SaveScreen(Story& story, From from, SceneKit::SpriteObject& window);
    using SlotList::drawOwner;
    // Runs until a slot was saved (true) or the screen was closed (false).
    bool run();

private:
    From from;
    SceneKit::SpriteObject& window_;
    Button yes_, no_; // event +0x38 / +0x3c

    bool confirm(); // 02028b94 / 02022b54
};

class LoadScreen : SlotList {
public:
    // `bonus`: the title's "Bonus" button (0202e050), the same screen with
    // its own title and picture, loading a game for the bonus section.
    explicit LoadScreen(Story& story, bool bonus = false) : SlotList(story), bonus_(bonus) {}

    using SlotList::drawOwner;
    // 0202dd5c / 0202e050: until a slot was loaded into the story's state
    // (true) or the screen was closed (false). Ends faded out: the lower
    // screen when closed, both after loading.
    bool run();

private:
    bool bonus_;
};

} // namespace picarat::adventure1
