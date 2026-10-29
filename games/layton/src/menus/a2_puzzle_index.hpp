#pragma once
#include "graphics/text.hpp"
#include "story/a2_game_state.hpp"
#include "system/a2_save_file.hpp"
#include "system/scene_kit.hpp"

namespace picarat {
// The list of all puzzles met so far, with their details and a replay
// option for solved ones.
//
// The puzzle index, ARM9 overlay 19 (load 020efda0) with
// overlay 27 loaded for its puzzle-data object, run by ov2 scenes 19 (bag),
// 22 (WiFi puzzles) and 31 (02069174): constructor 020efda0, run 020f15f0
// (vtable 020f3964 +0x14), destructor 020f096c. Reconstructed: scenes 19
// and 31 (from the bonus menu: back to scene 28, saving on close).
// Two tabs list the encountered puzzles (status != 0) or the favourites
// (progress bit 7); five rows scroll by buttons or a scroll bar; the upper
// screen shows the selected puzzle's picture, title, type, place, prize,
// hints and picarats. Solved puzzles can be replayed (scene 10, following 19).
class PuzzleIndex {
public:
    PuzzleIndex(GraphicsAssets& assets, PresentFrame present, AudioService* audio,
                GameState& state);
    void run();                   // 020f15f0; leaves scene 17 (close) or 10 (replay)
    SaveFile* saveFile = nullptr; // scene 31's save on close
private:
    using SpriteObject = SceneKit::SpriteObject;
    using SpriteButton = SceneKit::SpriteButton;

    // ov2 scroll-bar widget (ctor 020814e0, 0x34 bytes): a thumb between top
    // and bottom for `range + 5` entries, five visible.
    struct ScrollBar {
        int x = 0, pos = 0, top = 0, maxPos = 0, bottom = 0, range = 0, index = 0,
            thumb = 0;                          // +0 +4 +8 +0xc +0x10 +0x18 +0x1c +0x20
        SpriteObject sprite;                    // +0x28 jiten_scroll.spr
        bool dragging = false, changed = false; // +0x2c / +0x2d
        int lastY = 0;                          // +0x30
    };

    SceneKit kit_;
    GameState& state_;
    int scene_ = 19; // +0x564
    SpriteObject title_, tag_, favIcon_, digits_,
        guard_;                                         // +0x04 +0x08 +0x0c +0x10 +0x14 (3D, lower)
    SpriteObject question_, prize_, numbers_, hintBox_; // +0x20 +0x24 +0x28 +0x2c (2D, upper)
    SpriteButton start_, close_, up_, upFast_, down_, downFast_, allTab_,
        favTab_;                                   // +0x30 +0x34 +0x3c..+0x48 +0x4c +0x50
    std::array<SpriteButton, 5> rows_, favorites_; // +0x54 / +0x68
    ScrollBar bar_;                                // +0x7c
    std::array<int, 153> all_{}, favoriteList_{};  // +0x80 / +0x2e4
    int count_ = 0, top_ = 0, selected_ = 0;       // +0x548 +0x54c +0x550
    int favCount_ = 0, favTop_ = 0, favSelected_ = 0, mode_ = 0; // +0x554 +0x558 +0x55c +0x560
    std::string type_, place_;                                   // +0x568 / +0x5a8
    bool redraw_ = true;                                         // +0x774 list text needs drawing
    std::optional<PuzzleData> data_;                             // 020889f8 (ov27 puzzle object)

    void barRecompute();      // 0208159c
    void barCount(int count); // 02081878
    void barIndex(int index); // 02081b28
    void barUpdate();         // 0208188c (+ 0208165c drawing)
    void initLists();         // 020f2130
    void buildAll();          // 020f2208
    void buildFavorites();    // 020f22bc
    bool anyFavorite() const; // 020f20fc
    void numberDigits(SpriteObject& s, int x, int y, int value, int digits); // 020f24d8 / 020f3258
    void favoriteIcon(int x, int y, bool on);                                // 020f2484
    void entryIcons(int y, int number);                                      // 020f2588
    void highlight(int y);                                                   // 020f27b0
    void drawList(int offset, int textOffset);                               // 020f2834
    void scrollUp(int step);                                                 // 020f2b70
    void scrollDown(int step);                                               // 020f2cc8
    void switchMode(int mode);                                               // 020f2e2c
    void selectChanged();                                                    // 020f2efc
    void prizeIcon(int number);                                              // 020f319c
    void hintBoxes(int number);                                              // 020f3304
    void picarats(int number);                                               // 020f3368
    void typeAndPlace();                                                     // 020f33f4
    void upperInfo();                                                        // 020f346c
    void upperText();                                                        // 020f34f4

    void clearList() { kit_.clearTextRect(91, 58, 136, 100); } // 020f35b0

    void draw();                            // 020f12f4
    bool startPuzzle(int number, int mode); // the start-button tails of 020f15f0

    static std::uint16_t rgb(int r, int g, int b) {
        return std::uint16_t((r >> 3) | (g >> 3) << 5 | (b >> 3) << 10);
    } // 0206b114
};
} // namespace picarat
