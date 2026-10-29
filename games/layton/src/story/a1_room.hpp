#pragma once
// The first adventure's room scene (RoomObject: ARM9 constructor 0201c524,
// run 02020280, rebuild 0201d714 / 0201dbb4, virtual functions
// +0x148..+0x17c and +0x214..+0x234).
//
// How it plays
// ------------
// The upper screen shows the town map with a marker for where the player
// is, the place's name, the chapter's title and the number of puzzles
// solved. The lower screen shows the place itself. Touching a character or
// an object of interest starts its event; touching other spots shows a
// short remark; hidden hint coins pop out when their spot is touched. The
// suitcase button opens the trunk; the shoe button switches to "move" mode,
// in which the ways out of the place are marked; touching one moves there.
//
// Everything in a room comes from its two scripts: room<N>_in.gds (run on
// entering, may start events instead) and room<N>_param.gds (the room's
// contents).
#include "story/a1_story.hpp"

namespace picarat::adventure1 {

class RoomScene {
public:
    explicit RoomScene(Story& story);
    // 02020280: until a way out, an event, a puzzle or the trunk is chosen.
    void run();
    // 0201cdac / 0201d25c (the destructor; it presents a frame).
    void finish();

private:
    Story& story;
    SceneKit& kit;

    struct Exit { // 0x22 (V150 0201e77c)
        Button button;
        bool arrow = false;           // drawn exit_<n>.spr (an arrow); else a plain rectangle
        int target = 0;               // the room it leads to (+0x1b8)
        int type = 0;                 // 0x5b (V168): the sound of the way (+0x1f8)
        int markerX = 0, markerY = 0; // the map marker's next place (+0x200 / +0x210)
    };

    struct Examine { // 0x43 (V158 0201e8d4): a remark (room/tobj t_<n>.txt)
        Button button;
        int icon = 0; // +0xf0: the window's icon
        int text = 0; // +0x100
    };

    struct Zone { // the touch zones (02051858): hint coins, event marks
        int x = 0, y = 0, w = 0, h = 0;
        SceneKit::SpriteObject sprite;
        bool touched = false, latch = false;
        int state = 0;  // +0x28: 0 still, 1 popping up, 2 turning
        float time = 0; // +0x24
        int event = 0;  // +0x1c: hidden once this event is marked 2
        int coin = -1;  // the hint coin's number
    };

    struct Character { // 0x50 (V15c 0201e964): an event to start
        Button button;
        bool hasPicture = false;
        SceneKit::SpriteObject picture; // obj_<n>.spr
        int event = 0;                  // +0x2d0 (< 0: a puzzle for the shack)
        int puzzle = 0;                 // +0x1d8: the shack's puzzle
        // The shack's puff of smoke (020667f8, nazobaba_kemuri.spr): its
        // frames since a shuffle, drawn while below 100.
        bool hasPuff = false;
        SceneKit::SpriteObject puff;
        int puffFrames = 0;
    };

    // The complete robot dog in a room (02065a94, room +0x3fc). Rooms list
    // spots where something waits (0xd1 / 0xd2). Entering, the dog picks
    // one of them by chance; 140 frames later it barks, the room stops
    // taking touches, and it walks into the spot (fading in), sits there
    // a second in its pose and fades out again.
    struct DogSpot {   // the list (eight at most)
        int coin = -1; // the hint coin; -1: an event spot (+4)
        int x = 0, y = 0;
        bool facing = false; // +0xa4: comes from the left, drawn mirrored
        int chance = 0, pose = 0;
    };

    struct RobotDog {
        SceneKit::SpriteObject sprite; // +0xd0: cydog.spr
        bool active = false;           // +0xcd
        bool busy = false;             // +0xce: barked, touches wait (02065d2c)
        int coin = -1;                 // +0xac
        int x = 0, y = 0, target = 0;  // +0xb0, +0xb4, +0xb8
        int index = 0;                 // +0xc0
        int timer = 0;                 // +0xc4
        int alpha = 0;                 // +0xc8
        bool facing = false;           // +0xcc
    };

    std::vector<DogSpot> dogSpots_;
    RobotDog dog_;
    std::vector<Exit> exits_;
    std::vector<Examine> examines_;
    std::vector<Character> characters_;
    std::vector<Zone> markZones_, coinZones_;
    std::vector<SceneKit::SpriteObject> animations_, decorations_;
    Button options_, moveMode_;
    // The puzzle shack (room 'X' = 88): the question window and its buttons,
    // the puzzles' numbers, the button that shuffles the puzzles shown.
    bool shack_ = false;
    SceneKit::SpriteObject shackWindow_, shackNumbers_;
    Button shackYes_, shackNo_, shackShuffle_;
    SceneKit::SpriteObject mapIcon_, numbers_, touchIcon_, examineWindow_, examineIcon_, cursor_,
        door_, firstTouch_;
    bool showFirstTouch_ = false;
    bool iconShown_ = false;  // +0x408
    int alpha_ = 0x1f;        // +0x9c
    int eventAfterCoin_ = -1; // +0x3f8 (0xab)
    int mapPicture_ = 0;      // +0x3f4

    // The touch-area help (TouchArea): characters and things (an event
    // that leads to a puzzle not done yet: "puzzle"), remarks, hint coins
    // not taken.
    std::vector<TouchArea> touchAreas();
    // Whether touching a character starting `event` plays a puzzle now; read
    // once per room visit.
    bool eventLeadsToPuzzle(int event);
    std::map<int, bool> eventPuzzles_;

    void build();       // the constructor's part that 0201dbb4 repeats
    void pickDogSpot(); // 02065d34
    void updateDog();   // 02065e34 (draws with 02065de8)
    ScriptContext commands();
    void draw(bool interactive); // 0201f34c
    void drawUpper();            // 0201f1dc and the map marker
    void updateZone(Zone& zone, bool interactive);
    void examine(int index);           // 0201f5e8
    void takeCoin(int index);          // 0201f238
    bool moveMode();                   // 0201fce8
    void drawMoveMode(float phase);    // 0201fb08
    void reportTargets(bool moveMode); // research: Story::researchRoom
    void placeDoor(const Exit& exit);
    void walkMarker(const Exit& exit); // 0201f858
    void leaveBy(const Exit& exit);
    bool askPuzzle();                               // 02020b14
    void shuffleShack();                            // 0201ed20
    void drawShackNumber(int x, int y, int puzzle); // 0201efa0
    int shackNumber(int puzzle) const;
};

} // namespace picarat::adventure1
