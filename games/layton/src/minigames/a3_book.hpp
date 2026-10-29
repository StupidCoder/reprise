#pragma once
// The third adventure's picture book: a bag minigame. MINI_BOOK (overlay 19,
// lt3CLoopMiniBook, evt/prg/lt3_mini_book_main) opens one of three books
// (picked on MINI_BOOK_SELECT, a script loop it pushes first) whose pages
// tell a story with gaps; stickers found during the story fill the gaps.
// The lower screen shows the page's picture with a marker on each gap, the
// upper screen the page's text - a gap reads as its hint in brackets until
// a sticker is put on it, then as the sticker's word in the form the
// sentence needs (rc/mode/?/lt3_mini_book: twenty forms a sticker). The
// stickers not on a page wait in a tray of six places that scrolls. A book
// is solved when every sticker is on its own gap.
//
// The script does the buttons, page turns and dragging, and talks to the
// loop through the game variables ワーク[0..10]: the mode (0), the book
// picked (1), the page (2), the last page (3), a sticker dropped (4) at
// (5, 6) - which one (7), the tray's scroll (8), "take every sticker off"
// (9) and the tray's size (10).
//
// Reconstructed from overlay 19: 0211e7d0 (start), 0211eccc (activate),
// 0211edf0 (each frame), 0211f434 (loaded), 0211f4ec (a sticker moved),
// 0211f8ac (a page), 0211fe80 (the page's text again), 02120414 /
// 02120538 (a line), 021207ec / 021206d4 (the tray), 02120ab4 / 02120a2c
// (stickers in and out of the save), 02120134 (solved), 02120be4 (the
// book picker again).
#include "story/a3_story.hpp"
#include <array>
#include <string>
#include <vector>

namespace picarat::adventure3 {

// rc/mode/?/lt3_mini_book (0211e7d0's handlers 0211dae0..0211e5e8).
struct BookData {
    // 4032 {line, (text, other text, gap)...}: a '#' in a line reads one of
    // two words, as the sticker on that gap needs (none in this ROM's data).
    struct Choice {
        std::string first, second;
        int gap = 0;
    };

    // 4030 {message, gap, text, hint, form}: a line of a page; with a hint
    // its %s is a gap.
    struct Line {
        std::string message, text, hint;
        int form = 0, gap = -1;
        std::vector<Choice> choices;
    };

    // 4031 {gap, x, y}: where a gap's sticker goes on the picture (the game
    // keys all spots by gap + 100 * page + 10000 * book, 0211e100).
    struct Spot {
        int gap = 0, x = 0, y = 0;
    };

    struct Page { // 4020 .. 4021
        std::vector<Line> lines;
        std::vector<Spot> spots;
        const Spot* spot(int gap) const;
    };

    struct Book { // 4010 .. 4011
        std::vector<Page> pages;
    };

    // 4041 {found, placed, book, gap, name, choice} + 4042 x5 (four forms each).
    struct Sticker {
        std::string found, placed; // flag: found in the story; variable: its gap
        int book = 0, gap = 0;     // the gap it belongs on
        std::string icon, image;   // "i_name" in the tray, "s_name" on a page
        int choice = 0;            // which word a '#' takes
        std::array<std::string, 20> forms;
    };

    std::vector<Book> books;
    std::vector<Sticker> stickers;

    static const BookData& get(const Story& story);
};

class BookLoop : public Loop {
public:
    BookLoop(Story& story, LoopArgs args) : Loop(story, "MINI_BOOK", args) {}

    void activate() override;
    void deactivate() override;
    void frame(const lscr::SceneInput& input) override;
    bool busy() const override;

private:
    // A sticker of the open book that has been found (+0x850, 0x28 bytes).
    struct Seal {
        int sticker = 0;
        int gap = 0; // 0: in the tray
    };

    int work(int i) const;
    void setWork(int i, int value);
    std::string name(std::uint32_t address) const;
    std::string format(std::uint32_t address, int a, int b = 0) const;

    void open();                            // 0211eccc with a book picked
    void pickAgain();                       // 02120be4
    void loadSeals();                       // 02120ab4
    void keepSeals();                       // 02120a2c
    bool solved() const;                    // 02120134
    void viewButton();                      // 02120194
    void layers();                          // 021200a8
    void setUp(lscr::Object& o, int seal);  // 02120220
    void makeSeal(int seal);                // 020f29d8
    void showTray(int scroll);              // 021207ec
    void arrows(int scroll);                // 021206d4
    bool move(int sticker, int gap, int x); // 0211f4ec
    void showPage(int page);                // 0211f8ac
    void retext();                          // 0211fe80
    void writeLine(lscr::Object& message, const BookData::Line& line, int seal); // 02120414
    void dropped(); // 0211edf0's second half
    int sealOnGap(int gap) const;
    lscr::Object* sealObject(int seal);
    const BookData::Page* page(int number) const; // 1..

    enum class State { Idle, Picking, Loading, Running };
    State state_ = State::Idle;
    bool picked_ = false;           // +0x7b4: a book was asked for
    int book_ = 0;                  // +0x838
    int page_ = 0;                  // +0x83c
    int trayAt_ = 0;                // +0x840
    int bookStickers_ = 0;          // +0x844
    std::vector<Seal> seals_;       // +0x850
    std::array<int, 16> tray_{};    // +0xad0: stickers, -1 empty
    int trayCount_ = 0;             // +0x84c
    std::vector<std::string> made_; // the page's messages and points
};

// The app's minigame free play: every sticker found, and the books' state
// kept apart from the story - three bytes "solved" (＜絵本クリアー＞初級
// [book]) then one byte per sticker, the gap it was left on.
constexpr int kBooks = 3;
void findAllStickers(Story& story);
std::vector<std::uint8_t> bookProgress(const Story& story);
void setBookProgress(Story& story, const std::vector<std::uint8_t>& progress);
int bookStickerCount(const Story& story);

} // namespace picarat::adventure3
