#pragma once
// The third adventure's menu loops with native parts: the puzzle index
// (MENU_JITEN, overlay 18) and the secret pages of the bonus menu (SCRT_*,
// overlays 27..32). Each is a script loop (lt3CLoopScript) whose program
// does the buttons and pages; the class adds the commands 1280.. the
// program calls and, for some, a step after the program's each frame.
//
// The tables these pages read (character names, movie and voice lists, the
// music list, the pictures) are constants in the overlays; they are read
// from the ROM's code, not copied here.
#include "story/a3_loops.hpp"
#include <array>
#include <string>

namespace picarat::adventure3 {

// MENU_JITEN (lt3CLoopMenuJiten, evt/prg/lt3_menu_jiten): the bag's puzzle
// index - every puzzle found, or the ones marked as favourites (ワーク[4]),
// five rows at a time with a scroll bar; the list of titles is one long
// message that scrolls behind the rows.
//
// Commands (vtable +0x58.., 0211eb10..0211eb64):
//   1280 {index, out}: the slot at `index` of the list (0211df00);
//   1281: finds the row objects (0211df50);
//   1282 {row, slot}: one row (0211e090 -> 0211e5ac);
//   1283 {top}: the five rows from `top` (0211e0d4 -> 0211e74c);
//   1284 {count, top}: rebuilds the list's titles next frame (0211e104);
//   1285 {top}: the rows and their places (0211e178);
//   1286 {top}: the cursor (0211e1b0 -> 0211e858);
//   1287 {count, top}: the scroll bar's knob and its touch box (0211e1e0).
class MenuJitenLoop : public ScriptLoop {
public:
    MenuJitenLoop(Story& story, LoopArgs args);

    void activate() override;
    void frame(const lscr::SceneInput& input) override;
    lscr::Result command(int index, lscr::Call& call) override;

private:
    std::string name(std::uint32_t address) const;
    std::string rowObject(std::uint32_t format, int row) const;
    int entry(int index) const;
    void showRow(int row, int slot);
    void fillRows(int top);
    void layRows();
    void showCursor(int top);

    // 1284's request (+0x81c, +0x7ac, +0x820): the titles of the first
    // `listCount_` entries, the message `listShift_` pixels up.
    bool listPending_ = false;
    int listCount_ = 0;
    float listShift_ = 0;
};

// SCRT_CHARA (lt3CLoopSecretChara, overlay 27, evt/prg/lt3_scrt_chara):
// the characters - pages of twenty buttons; picking one shows the character
// standing on the upper screen with its name and profile. Picking the one
// shown again plays one of its animations at random.
//   1280 {first, second, dx}: twenty buttons of each page slide by dx
//        (0211df00);
//   1281 {page, column, on}: a column of four buttons shown or hidden
//        (0211e090);
//   1282 {button}: pick (0211e170).
// Each frame (0211e370) the pick goes on in steps: number and name plate,
// profile, the old character fading out, the new one in.
class SecretCharaLoop : public ScriptLoop {
public:
    SecretCharaLoop(Story& story, LoopArgs args);

    void activate() override;
    void deactivate() override;
    void frame(const lscr::SceneInput& input) override;
    lscr::Result command(int index, lscr::Call& call) override;

private:
    std::string name(std::uint32_t address) const;
    std::string characterName(int index) const;

    int step_ = 0;            // +0x7ac
    std::string shown_;       // +0x7b0: the character's object
    int character_ = 0;       // +0x7b4: its record's number
    int index_ = 0;           // +0x7b8: which entry of the list
    bool startPicture_ = true; // +0x7bc: the page's first picture still up
    int previous_ = 0;        // +0x7c0
    std::string previousName_;
};

// SCRT_VOICE (lt3CLoopSecretVoice, overlay 28, evt/prg/lt3_scrt_voice):
// voices - 35 buttons, each a line of the story spoken again with its
// speaker's portrait and text.
//   1280: finds the buttons and the thirteen portraits (0211df00).
// Each frame (0211e078) the button the program put in ワーク[0] plays.
class SecretVoiceLoop : public ScriptLoop {
public:
    SecretVoiceLoop(Story& story, LoopArgs args);

    void activate() override;
    void frame(const lscr::SceneInput& input) override;

private:
    std::string name(std::uint32_t address) const;
};

// SCRT_MOVIE (lt3CLoopSecretMovie, overlay 29, evt/prg/lt3_scrt_movie):
// the movies - 37 buttons on two pages.
//   1280 {dx}: every button slides by dx (0211df00);
//   1281: finds the buttons (0211dfb8);
//   1282 {button, out}: the movie of a button (0211e038).
class SecretMovieLoop : public ScriptLoop {
public:
    SecretMovieLoop(Story& story, LoopArgs args);

    void start() override;
    lscr::Result command(int index, lscr::Call& call) override;

private:
    std::string name(std::uint32_t address) const;
};

// SCRT_IMAGE (lt3CLoopSecretImage, overlay 30, evt/prg/lt3_scrt_image):
// the art - twenty pictures, a page each.
//   1280 {dx}: the next / previous picture (0211df00).
// Each frame (0211e024): once the picture has loaded it shows with its
// number, title and text; the one before goes the frame after.
class SecretImageLoop : public ScriptLoop {
public:
    SecretImageLoop(Story& story, LoopArgs args);

    void activate() override;
    void frame(const lscr::SceneInput& input) override;
    lscr::Result command(int index, lscr::Call& call) override;

private:
    std::string name(std::uint32_t address) const;

    int step_ = 0;     // +0x7ac
    int picture_ = 1;  // +0x7b0
    bool first_ = false; // +0x7f4: the page's first picture (input stays off)
    std::string resource_, object_, oldResource_, oldObject_; // +0x7c4.. +0x7b4..
};

// SCRT_SOUND (lt3CLoopSecretSound, overlay 31, evt/prg/lt3_scrt_sound):
// the music - 26 pieces on two pages.
//   1280 {dx}: the other page (0211df00);
//   1281: creates the buttons (0211e050);
//   1282 {piece}: play (0211e184);
//   1283: stop (0211e3c0).
// Each frame (0211e458): input back once the page has slid; a piece starts
// 30 frames after it was asked for.
class SecretSoundLoop : public ScriptLoop {
public:
    SecretSoundLoop(Story& story, LoopArgs args);

    void activate() override;
    void frame(const lscr::SceneInput& input) override;
    lscr::Result command(int index, lscr::Call& call) override;

private:
    std::string name(std::uint32_t address) const;
    std::array<int, 2> piece(int index) const; // {kind (0 music, 1 stream), number}

    int step_ = 0;            // +0x7ac
    int page_ = 1;            // +0x7b0
    bool startPicture_ = true; // +0x81c
    int current_ = 0;         // +0x820
    int wait_ = 0;            // +0x824
};

// SCRT_ROOM (lt3CLoopSecretRoom, overlay 32, evt/prg/lt3_scrt_room): the
// hidden room. Activated (0211df00), it writes the room's password: eight
// characters made from the console's Wi-Fi address (0211e00c).
class SecretRoomLoop : public ScriptLoop {
public:
    SecretRoomLoop(Story& story, LoopArgs args);

    void activate() override;

    // 0211e00c: the password for a console address and the game's salt.
    static std::string password(const std::array<std::uint8_t, 6>& address, int salt);
};

// The loops above by name (nullptr: none of them).
std::unique_ptr<Loop> makeMenuLoop(Story& story, const std::string& name, const LoopArgs& args);

} // namespace picarat::adventure3
