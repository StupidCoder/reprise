#pragma once
// The first adventure's event scene (EventScreenObject: ARM9 constructor
// 02025d48, run 0202aea4, destructor 02026994, virtual functions
// +0x164..+0x270 for the event commands).
//
// How it plays
// ------------
// An event is a scene of the story: characters stand on the lower screen
// over a darkened picture of the place, the upper screen shows a picture
// too, and the characters talk in a window at the bottom, line by line
// (a touch skips to the end of a line, "@p" in a text waits for a touch).
// Events give items, hint coins and gizmos, ask the player to choose,
// start puzzles and movies and set story flags; all of it is in the event's
// script (script/event/e<N>.gds); this class supplies the screens its
// commands use. When the script ends, the scene it named runs next (a room
// unless the script said otherwise).
#include "story/a1_story.hpp"

namespace picarat::adventure1 {

class EventScene {
public:
    explicit EventScene(Story& story);
    // 0202aea4: the event's script, then the puzzles sent to the shack.
    void run();
    // 02026994 (the destructor; it presents a frame).
    void finish();

private:
    Story& story;
    SceneKit& kit;
    // The talk window (+0x1c event_window_1, +8 its talk object at (0xe, 0x8d)).
    SceneKit::SpriteObject window_, prizeWindow_, prize_, cursor_;
    std::vector<SceneKit::SpriteObject> plates_; // +0x254: name plates
    bool fixedStyle_ = false;                    // +0x64: window style set by the script
    int talkSpeaker_ = -1;                       // the talk object's +8
    // Choices (+0x294 question, +0x394.. three answers; +0x6a8.. their flags).
    std::string question_, answers_[3];
    int answerFlags_[3] = {-1, -1, -1};
    Button answerButtons_[5];
    Button yes_, no_;
    Button roomA_, roomB_; // the hotel rooms (furniture)
    // Puzzles sent to the shack during the event (+0x66 count, +0x68 lines).
    std::vector<std::string> shackLines_;

    ScriptContext commands();
    Story::Actor* actor(int n);
    Story::Actor* upperActor(int n);
    void drawWindow(int alpha);             // 02066038: the window and the speaker's plate
    void windowStyle(int style);            // V19c / V1a0 / V1a4
    void talk(int text, int speaker);       // V198 (0202ac28)
    void typeText(const std::string& text); // 02066340
    void waitTouch();                       // 0206629c
    void choose(int count);                 // V190 (020274b0) / V194 (02027828)
    // The prize windows' common parts (02028f74 ...): the window fades in,
    // `draw` shows its contents, a touch closes it.
    template <class Draw>
    void prizeWindow(const std::string& text, int textY, Draw draw);
    void item(int n);          // V1ac (02028f74)
    void puzzleNotice();       // V1b0 (02029238)
    void gizmo(int n);         // V1b4 (02029790)
    void paintingPiece(int n); // V1b8 (02029a4c)
    void furniture(int n);     // V1c0 (02029d2c)
    void savePrompt();         // V1bc (02028c8c)
    void shackPuzzle(int n);   // V1c4 (0202a384)
    void mystery(int n);       // V1c8 (0202a930)
    void showShackList();      // 0202a5dc
};

} // namespace picarat::adventure1
