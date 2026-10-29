// The bonus section ("Bonus" on the title): the secret menu and its pages.
// The secret menu leads to the WiFi menu, the puzzle index, the challenges
// and the extras; the extras (once the game is finished) hold the art
// gallery, the character files, the music and voice rooms and the movie
// theatre, each unlocked by enough picarats, and the passcode page, where
// two codes computed from the console unlock two extra events.
//
// ARM9 overlay 23 (scenes 28..36 and 40, run by ov2 0206a168..0206a89c) and
// overlay 11 (scenes 41/42, code entry, 020690b4). Every page follows the
// same frame: poll, draw, present (one SceneKit), fade in, then input until a
// button leaves for another scene.
#pragma once
#include "story/a2_game_state.hpp"
#include "system/console_id.hpp"
#include "system/a2_save_file.hpp"
#include "system/scene_kit.hpp"
#include <array>
#include <string>

namespace picarat {
// consoleCode (system/console_id.hpp) is 0206ba48.

// The pages run on one SceneKit each; `run` sets the next scene in `state`.
class BonusPage {
public:
    BonusPage(GraphicsAssets& assets, PresentFrame present, AudioService* audio, GameState& state,
              SaveFile* file);

protected:
    using SpriteObject = SceneKit::SpriteObject;
    using SpriteButton = SceneKit::SpriteButton;
    SceneKit kit_;
    GameState& state_;
    SaveFile* file_;
    // 0208003c with the same animation name for pressed, idle and click and
    // the press offset (1, 1) the pages set at +0x14/+0x18.
    SpriteButton picture(const std::string& path, int x, int y, int sound,
                         const std::string& animation);

    void live(SpriteButton& b) {
        kit_.updateButton(b);
        kit_.drawButton(b);
    }

    void waitForMusic(); // 0207fa78 loop: until the BGM controller settles
    void leave(std::uint8_t scene, int following = -1);
};

class SecretMenu : BonusPage { // scene 28: ov23 020efda0 / 020f0234 / 020f0110
public:
    using BonusPage::BonusPage;
    void run();

private:
    SpriteButton wifi_, index_, challenge_, extras_, back_;
    void build();
};

class WifiMenu : BonusPage { // scene 29: ov23 020f0420 / 020f07c4 / 020f06ac
public:
    using BonusPage::BonusPage;
    void run();

private:
    SpriteButton puzzles_, download_, settings_, cancel_;
    SpriteObject badge_;
    bool fresh_ = false;
};

class ExtrasMenu : BonusPage { // scene 30: ov23 020f0928 / 020f0ff0 / 020f0e40
public:
    using BonusPage::BonusPage;
    void run();

private:
    std::array<SpriteButton, 6> pages_; // characters, art, music, voices, movies, passcode
    SpriteButton back_;
    SpriteObject digits_;
    void draw();
};

class ArtGallery : BonusPage { // scene 32: ov23 020f12ec / 020f15ec / 020f1514
public:
    using BonusPage::BonusPage;
    void run();

private:
    SpriteButton previous_, next_, back_;
    SpriteObject digits_;
    int picture_ = 0; // +0x10
    void draw();      // 020f15a8
    void show();      // 020f16f4
};

class CharacterFiles : BonusPage { // scene 33: ov23 020f1944 / 020f1e5c / 020f1d44
public:
    using BonusPage::BonusPage;
    void run();

private:
    std::array<SpriteButton, 90> files_;  // +0x000: six pages of 15
    SpriteButton previous_, next_, back_; // +0x168 / +0x16c / +0x170
    int pages_ = 5;                       // +0x174
    int shown_ = -1;                      // +0x178
    void draw(int page);                  // 020f1de8
    void show(int page, int file);        // 020f1f9c
};

class MusicRoom : BonusPage { // scene 34: ov23 020f2134 / 020f2470 / 020f2360
public:
    using BonusPage::BonusPage;
    void run();

private:
    std::array<SpriteButton, 19> tracks_;
    SpriteButton back_;
    SpriteObject number_;
    bool idle_ = true;      // +0x54: nothing chosen yet
    void draw(bool active); // 020f2418
    void play(int track);   // 020f2550
};

class VoiceRoom : BonusPage { // scene 35: ov23 020f27a0 / 020f2a70 / 020f29ac
public:
    using BonusPage::BonusPage;
    void run();

private:
    std::array<SpriteButton, 30> voices_;
    SpriteButton back_;
    void play(int voice); // 020f2b44
};

class MovieTheatre : BonusPage { // scene 36: ov23 020f2bdc / 020f2f1c / 020f2e70
public:
    using BonusPage::BonusPage;
    void run();

private:
    std::array<SpriteButton, 23> movies_;
    SpriteButton back_;
};

class PasscodePage : BonusPage { // scene 40: ov23 020f306c / 020f35b0 / 020f34fc
public:
    using BonusPage::BonusPage;
    void run();

private:
    SpriteButton first_, second_, back_;
};

class CodeEntry : BonusPage { // scenes 41/42: ov11 020efda0 / 020f0594 / 020f0064
public:
    using BonusPage::BonusPage;
    void run();

private:
    std::array<SpriteButton, 36> keys_; // +0x40: spriteless 19x19
    SpriteButton ok_, erase_, back_;    // +0x38 / +0x3c / +0xd4
    SpriteObject cursor_, badCode_;     // +0x30 / +0xd0
    std::string keyChars_;              // +0x0c: secret.txt (36 characters)
    std::array<char, 8> input_{};       // +0x04
    int position_ = 0;                  // +0x00
    void clear();                       // 020f0280
    void type(char c);                  // 020f02ac
    void erase();                       // 020f02cc
    void keyboard();                    // 020f046c
    void showInput();                   // 020f056c (020f03c0 / 020f042c)
    bool wrong();                       // 020f0160
};
} // namespace picarat
