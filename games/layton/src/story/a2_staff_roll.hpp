#pragma once
#include "system/scene_kit.hpp"
#include <array>

namespace picarat {
// The staff roll at the end of the game: credits scroll up the lower
// screen while story pictures fade in and out on the upper screen.
//
// ARM9 overlay 22 (load 020efda0, ROM 0x9ce00+0x728), run by ov2
// scene 21 (0206a0f4): constructor 020eff04, run 020f0148, destructor
// 020f00e4. The lower screen scrolls 36 credit pages (bg/ending/<lang>/NN)
// one pixel per step through two BG layers; the upper screen fades 19 story
// pictures (bg/ending/NN) in and out over a tiled paper sprite. One step is
// three frames (020f01f0); the scene ends after 20280 frames of steps.
class StaffRoll {
public:
    StaffRoll(GraphicsAssets& assets, PresentFrame present, AudioService* audio);
    void run(); // 020f0148
    // Research observer (tools): called after every step's update.
    std::function<void(int step, int scroll, int blend, int picture, int credit)> researchStep;

private:
    // Background loader record (0x90 bytes at object +0x20/+0xb0/+0x140;
    // 020efdc0 reset, 020efde4 request, 020efe24 one streaming step).
    struct Loader {
        int id = -1;
        std::string path; // +0x02 page/picture number, +0x04 path
        bool requested = false, opened = false, loaded = false; // +0x8c / +0x8d / +0x8e
    };

    SceneKit kit_;
    int picture_ = 0, credit_ = 0;                     // +0x00 / +0x04
    int blend_ = 0, blendTarget_ = 0, blendSpeed_ = 0; // +0x0c / +0x10 / +0x14 (sub BLDALPHA EVA)
    int pictureClock_ = -1, creditClock_ = -1;         // +0x18 / +0x1c
    std::array<Loader, 3> loaders_;                    // lower A, lower B, upper picture
    // Layer-manager state the original writes through 0206ffec/020702c8/
    // 0207064c and +0x74/+0x78 (main BG scroll).
    std::string lowerA_, lowerB_, picture_image_;
    int scroll_ = 0;
    int shownScroll_ = 0;                                     // scroll latched at the buffer swap
    int upper_ = -16, lower_ = -16;                           // master brightness (both screens)
    void reset(Loader& l);                                    // 020efdc0
    void request(Loader& l, int id, const std::string& path); // 020efde4
    bool stream(Loader& l);                                   // 020efe24
    void streamOne();                                         // 020f0258: first busy loader only
    void step();                                              // 020f01f0
    void update();                                            // 020f0290
    void paint(Surface& image);                               // 020f05a4 + layer composition
    void fade(bool in, int interval);                         // 02071790 / 02071e84 (step 1)
    static std::string asset(const std::string& romPath);
};
} // namespace picarat
