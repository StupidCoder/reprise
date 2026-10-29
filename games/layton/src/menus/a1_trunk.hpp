#pragma once
// The first adventure's trunk (the bag menu of the rooms; ARM9 object
// 020d31e4: constructor 02020e8c, run 02023818, destructor 02021c14).
//
// How it plays
// ------------
// The upper screen sums up the game: puzzles solved and found, picarats,
// hint coins with a medal for how many, the play time, and a little figure
// that walks (it stops once the clock reaches 100 hours). The lower screen
// is the open trunk with its compartments: the story memo, the save
// screen, the puzzle index, the mysteries, and - once the story has handed
// them over - the robot dog, the torn painting and the hotel rooms. "New"
// marks show where something was added. The place's name is printed at
// the bottom; the clasp closes the trunk.
//
// The story memo lists the chapters' entries six to a page (unread ones
// marked); touching one shows its text.
#include "system/a1_ui.hpp"

namespace picarat::adventure1 {
class Story;

class Trunk {
public:
    explicit Trunk(Story& story); // 02020e8c
    // 02023818: until a compartment leaves the trunk (the state's scene
    // says where to: the room, a page of its own, the puzzle index).
    void run();
    // 02021c14 (the destructor).
    void finish();

private:
    Story& story;
    SceneKit& kit;
    int mode_ = 0; // +0xa4: 0 the trunk, 1 the story memo's index

    // Upper screen (2D sprites).
    SceneKit::SpriteObject coinsDigits_, picaratDigits_, walker_,
        medal_; // +0x10, +0x1c, +0x18, +0x14
    // Lower screen.
    Button close_, save_, memo_, index_, mysteries_, hotel_, dog_, painting_; // +0x30..+0x4c
    SceneKit::SpriteObject items_[3], newMark_, bagTitle_, memoIcon_; // +0x64.., +0x60, +0x20, +0xc
    SceneKit::SpriteObject window_, pageDigits_;                      // +4, +0x70
    Button memoClose_, memoNext_, memoBack_, memoReturn_;             // +0x50..+0x5c
    Button entries_[6];                                               // +0x84

    void setBackground(); // 02022444
    void draw();          // 02022c64: the page and the status
    void drawStatus();    // 0202267c + 02022a40
    void drawCount(SceneKit::SpriteObject& digits, int x, int y, int value, int step, int width);
    bool mainPage();                         // 02023950: true when the trunk closes
    void memoIndex();                        // 02023e00
    void drawEntries(int first, bool texts); // 02023c68 (with 02023bfc, the page number)
    void memoPage(int entry);                // 02024178
};

} // namespace picarat::adventure1
