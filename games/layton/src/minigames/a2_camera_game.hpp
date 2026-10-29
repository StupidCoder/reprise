#pragma once
#include "story/a2_game_state.hpp"
#include "system/scene_kit.hpp"

namespace picarat {
// Camera repair.
//
// How it plays: the broken camera's parts, collected during the story, lie
// in a tray. The player drags them onto the assembly grid and turns them in
// quarter turns; when every part is in its place the right way round, the
// camera works again (and can take photos for the photo hunt).
//
// Camera repair, ARM9 overlay 13 (load 020efda0), run by ov2 scene 14
// (02069f4c): constructor 020efda0 (0x250 bytes), run 020f0e90 (draw
// 020f1bac before logic 020f0f00 each frame), destructor 020f003c.
// The ten camera parts (CameraPrt bits, save +0x24c) are dragged from a
// five-slot tray onto a 16x8 grid of 16-pixel cells and turned in quarter
// steps; placements persist in save +0x24e (one packed halfword per part).
// With every part at its place and unrotated, CameraSolved (bit 10) is set
// and event 18430 follows.
class CameraGame {
public:
    CameraGame(GraphicsAssets& assets, PresentFrame present, AudioService* audio, GameState& state);
    void run();

    std::function<HostInput(unsigned, const HostInput&)>& researchPoll() {
        return kit_.researchPoll;
    } // research only

    std::function<void(unsigned poll, const std::string& state)> researchTrace; // research only
private:
    using SpriteObject = SceneKit::SpriteObject;
    using SpriteButton = SceneKit::SpriteButton;

    struct Part {                                 // object +0x6c + 0x30 * i
        bool present = false;                     // +0x00 CameraPrt bit set
        int pivotX = 0, pivotY = 0;               // +0x02 / +0x04 rotation centre in the part
        int width = 0, height = 0;                // +0x06 / +0x08 pixels (cells << 4)
        int cellX = -1, cellY = -1;               // +0x0a / +0x0c (-1: in the tray)
        float fx = 0, fy = 0, angle = 0;          // +0x10 / +0x14 eased centre, +0x18 shown angle
        int px = 0, py = 0, drawX = 0, drawY = 0; // +0x1c / +0x1e cell origin, +0x20 / +0x22 centre
        int rotation = 0;                         // +0x24 quarter turns
        float target = 0;                         // +0x28 angle of the rotation
        int savedRotation = 0;                    // +0x2c rotation when picked up
    };

    SceneKit kit_;
    GameState& state_;
    SpriteButton exit_;                                                           // +0x00
    SpriteObject cursor_, parts_, icons_, arrows_, congratulation_, photoWindow_; // +0x08..+0x1c
    int state_100 = 100, timer_ = 0, touchX_ = -1, touchY_ = -1,
        dragging_ = -1;                     // +0x20 / +0x22 / +0x24 / +0x26 / +0x28
    std::vector<int> owned_, order_, tray_; // +0x2a / +0x40 / +0x54 (counts +0x3e / +0x68)
    std::array<Part, 10> parts{};           // +0x6c
    int scroll_ = 0;                        // +0x24c
    void reset();                           // 020f0214
    void save();                            // 020f003c placement save
    bool complete() const;                  // 020f0508
    bool pick(int x, int y);                // 020f0584
    bool drop(int x, int y);                // 020f09c8
    void rebuildTray(int keep);             // 020f0d54
    bool logic();                           // 020f0f00
    void draw();                            // 020f1bac
    // Offsets of the rotated part's centre from its cell origin (stack tables
    // in 020f0214/020f0584/020f09c8): x = A[r] + B[r], y = C[r] + D[r].
    static int offsetX(const Part& p, int r);
    static int offsetY(const Part& p, int r);

    static int rotatedWidth(const Part& p, int r) { return (r & 1) ? p.height : p.width; }

    static int rotatedHeight(const Part& p, int r) { return (r & 1) ? p.width : p.height; }
};
} // namespace picarat
