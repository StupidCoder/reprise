#pragma once
// The third adventure's name entry (NAME_RESIST, overlay 33): a keyboard on
// the lower screen for the player's name when a new game starts (mode 0),
// for the second name the story asks for later (mode 1) and for the secret
// door's password (mode 2, not part of the story; not reconstructed).
//
// How it plays: four rows of keys (digits, then the three letter rows) in
// three layouts - small letters, capitals (the caps key, row three's left
// end) and shifted symbols (the shift key, row four's left end; it falls
// back to small letters after one key) - plus a page of accented letters.
// A key types at the press, plays a click and locks the keyboard for nine
// frames. The name has at most ten characters; typing on a full name
// replaces its last one. Space and delete, the layout buttons, OK and back
// are script buttons (evt/prg/lt3_name_resist) that leave a request in the
// game variable ワーク[1]. OK refuses an empty name (a buzz) and the two
// lead characters' names, in any case (two strings of the overlay; an error
// window until the next touch).
//
// Reconstructed from overlay 33: 0211db30 (begin), 0211e42c (layout and
// fades), 0211e03c (touches and buttons), 0211e7d0 / 0211e9d4 (keys),
// 0211eb64 / 0211ec18 (caps, shift), 0211ecf8 / 0211ee08 (cursor, delete),
// 0211f320 (the name check), 0211eed8 (stored). Key rows and layouts are
// read from the overlay's tables at run time. The computer keyboard types
// too (keyboard() below).
#include "story/a3_loops.hpp"
#include <array>
#include <deque>

namespace picarat::adventure3 {

class NameEntryLoop : public Loop {
public:
    NameEntryLoop(Story& story, LoopArgs args) : Loop(story, "NAME_RESIST", args) {}

    void activate() override;
    void deactivate() override;
    void frame(const lscr::SceneInput& input) override;

    // Typed keys (a native convenience, as in the second adventure's name
    // entry): the on-screen keyboard's characters type, Return and
    // Backspace / Delete ask what OK and delete ask, any key closes the
    // error window. Taken one a frame once the screen has faded in.
    void keyboard(const NameInput& key) override { typed_.push_back(key); }

private:
    void setLayout(int layout);
    void press(int x, int y);
    void type(char c);
    void erase();
    void closeError();
    void moveCursor();
    bool nameAccepted();
    void runToEnd(const std::string& section);
    int cellX(int i) const;
    std::string name() const;

    int mode_ = 0, maxLength_ = 10;
    int layout_ = 0;         // 1 small letters, 2 capitals, 3 shifted, 4 accents
    int cursor_ = -1;        // the last character typed
    std::string characters_; // the name, one byte per character (cp1252)
    int lockout_ = 0;        // frames the keyboard ignores the pen
    int fade_ = 0;           // 1..3 while fading in (0211e42c)
    bool errorShown_ = false;
    bool highlighted_ = false;
    std::array<int, 2> highlight_{};
    bool finishing_ = false;
    std::deque<NameInput> typed_;
    // The keys: layouts 1..3, four rows each (0211e7d0's tables), and the
    // accented letters, thirteen to a row (0211e9d4).
    std::array<std::array<std::string, 4>, 4> rows_;
    std::string accents_;
};

} // namespace picarat::adventure3
