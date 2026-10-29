#pragma once
#include "story/a2_game_state.hpp"
#include "system/scene_kit.hpp"

namespace picarat {
// Herb tea.
//
// How it plays: herbs found during the story can be brewed into teas. The
// player puts three herbs into the pot and closes the lid; the herbs, in any
// order, form a recipe code, and if one of the twelve recipes matches, that
// tea is brewed and added to the menu. Characters in the story sometimes
// ask for a tea; serving the tea they want completes their request (and
// unlocks what follows), and every tea can also be tasted, with comments.
//
// Herb tea subgame, ARM9 overlay 14 (load 020efda0), run by ov2 scene 15
// (02069fd8; scene 25 from script 0x73 sets the following scene to 3 first).
// Two screens alternate inside one scene object (0x10 bytes, 020efda0):
//   TeaMenu    (0x1c4 bytes, 020f00e8): the twelve brewed teas, serving,
//              tasting talk and the bag-menu tea requests (protagonist or assistant);
//   TeaBrewer  (0x20c bytes, 020f230c): herbs, pot and lid, brewing result.
// A request (scene 25) starts on the menu; from the bag (scene 15) it starts
// on the brewer. Field names below give the original object offsets.
class TeaGame {
public:
    TeaGame(GraphicsAssets& assets, PresentFrame present, AudioService* audio, GameState& state,
            std::uint32_t& random);
    // 020efda0 constructor, 020eff9c run, 020efe88 destructor (scene changes).
    void run();

    // Research observers (local probes); the game never sets them.
    std::function<HostInput(unsigned poll, const HostInput& host)>& researchPoll() {
        return kit_.researchPoll;
    }

    std::function<void(unsigned poll, const std::string& state)> researchTrace;

private:
    using SpriteObject = SceneKit::SpriteObject;
    using SpriteButton = SceneKit::SpriteButton;
    SceneKit kit_;
    GameState& state_;
    std::uint32_t& random_;
    bool request_ = false; // scene +0x00 (state +0x479 == 3 at construction)

    // --- TeaMenu (020f00e8).
    struct Menu {
        SpriteButton serve, quit, brew;    // +0x00 furumau, +0x04 yametoku (requests), +0x08 chougo
        std::array<SpriteButton, 12> teas; // +0x0c tea_icon
        std::array<SpriteObject, 12> numbers; // +0x3c tea_number
        SpriteObject arrow, assistant, protagonist,
            window;                        // +0x6c/+0x70/+0x74 (sbj), +0x7c window.sbj
        std::array<SpriteObject, 3> herbs; // +0x80 icon.spr (selected recipe)
        int result = 0;                    // +0x8c: 1 brew, 2 leave
        std::string text;
        int count = -1, length = -1, speaker = -1;             // +0x90 buffer, +0x190/+0x192/+0x194
        int tastedTea = -1, line = -1;                         // +0x196 / +0x198
        int protagonistFace = 1, assistantFace = 1;            // +0x19c / +0x1a0
        int selected = -1;                                     // +0x1a4
        bool request = false, talking = false, cursor = false; // +0x1a8 / +0x1a9 / +0x1aa
        SpriteObject help, wait, touchWindow, limitIcon,
            limitNumber; // +0x1ac/+0x1b0/+0x1b4/+0x1b8/+0x1bc
        int correct = 0; // +0x1c0
    };

    void menuConstruct(Menu& m);                                             // 020f00e8
    void menuDestruct(Menu& m);                                              // 020f0a08
    void menuRun(Menu& m);                                                   // 020f1014
    void menuDraw(Menu& m, bool update);                                     // 020f0cc8
    void menuClearTalk(Menu& m);                                             // 020f0c3c
    void menuTalk(Menu& m, int speaker, const std::string& raw, bool typed); // 020f0c70
    bool menuTutorial(Menu& m);                                              // 020f16dc
    void menuHintCoin(Menu& m);                                              // 020f1edc
    void menuDrink(Menu& m);                                                 // 020f2138
    void menuCorrectCount(Menu& m);                                          // 020f2204
    void menuRecipeHerbs(Menu& m);                                           // 020f22cc
    void loadTalk(); // ht_tlk.plz (0206c230)

    // --- TeaBrewer (020f230c).
    struct Item {
        int x = 0, y = 0;
        float fx = 0, fy = 0;
    }; // target and eased position (12 bytes)

    struct Brewer {
        int state = 1000, timer = 0, touchX = -1, touchY = -1; // +0x00 / +0x02 / +0x04 / +0x06
        SpriteButton exit, reset, recipes,
            cover; // +0x08 modoru, +0x0c return, +0x10 recipe/serve/help, +0x14 cover
        SpriteObject icon, name, teapot, lid, window, wait, touchWindow; // +0x18..+0x30 (3D)
        SpriteObject protagonist, assistant, arrow, windowTop;           // +0x34..+0x40 (2D)
        int selected = -1, previous = -2;                                // +0x44 / +0x46
        std::array<int, 3> pot{};
        int count = 0;                              // +0x48 / +0x4e
        int protagonistFace = 1, assistantFace = 1; // +0x50 / +0x54
        int lidX = 0x9f, lidY = 0x61;
        float lidFx = 0, lidFy = 0;            // +0x58 / +0x5c
        std::array<Item, 3> potItems;          // +0x64
        std::array<Item, 8> shelf;             // +0x88
        bool request = false, success = false; // +0xe8 / +0xe9
        int code = -1;                         // +0xea
        std::string text;
        std::string textSource;                     // +0xec buffer, +0x1ec source identity
        int count2 = -1, length = -1, speaker = -1; // +0x1f2 / +0x1f4 / +0x1f6
        int talkId = -1, line = -1;                 // +0x1f8 / +0x1fa
        SpriteObject help, limitIcon, limitNumber;  // +0x1fc / +0x200 / +0x204
        int correct = 0;                            // +0x208
    };

    void brewerConstruct(Brewer& b); // 020f230c
    void brewerRun(Brewer& b);       // 020f2f18
    void brewerFrame(Brewer& b);     // 020f2fdc
    void brewerLogic(Brewer& b);     // 020f3010
    void brewerDraw(Brewer& b);      // 020f41e8
    void brewerReset(Brewer& b);     // 020f2b98
    void brewerClearTalk(Brewer& b); // 020f2c88
    void brewerTalk(Brewer& b, int speaker, const std::string& raw, const std::string& source,
                    bool typed);   // 020f2cc0
    void brewerComment(Brewer& b); // 020f2d28
    void brewerLimit(Brewer& b);   // 020f4c18

    int correctTeas() const; // TeaCorrectFlag 0..23 plus hint coins 127:0/127:1

    std::string talkText(const std::string& name) {
        return kit_.member("ht", name);
    } // ht_tlk.plz member

    static std::string format(std::string pattern,
                              const std::vector<std::string>& args); // %s substitutions
};
} // namespace picarat
