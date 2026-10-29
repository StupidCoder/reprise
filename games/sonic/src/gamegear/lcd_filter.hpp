#pragma once
#include "gamegear/video.hpp"

namespace gg {
// Optional native presentation enhancement imitating the Game Gear screen.
// General hardware documentation (SMS Power "Game Gear LCD") identifies the
// panel as a Citizen UC-320 3.2" passive Super-Twisted Nematic colour LCD with
// a CCFL backlight, 160x146 dots. Its Master System scaling rule
// "[RGB]1 [RGB]2 [RGB]3 = [RG1B2] [R2GB3]" implies three horizontally ordered
// R, G, B subpixels per dot, i.e. vertical RGB stripes. STN panels of this kind
// have low contrast, lifted blacks, muted colour and slow pixel response.
// Every numeric look parameter below is an interpretation, not a measurement.
// The filter never changes game state, audio, saves or the logical frame
// given to it; it only transforms what is presented.
class LcdFilter {
public:
    // Forget persistence history (new game, size change, filter toggled on).
    void reset();
    // Colour response plus pixel persistence at logical resolution. elapsed is
    // real presentation time since the previous call; 0 leaves history alone.
    const platform::Image& apply(const platform::Image& frame, double elapsed);
    // Static STN/CCFL colour transfer used by apply().
    static Color respond(Color);
    // Multiplicative subpixel mask for a destination area in device pixels.
    // cell_width/height: device pixels per logical pixel; origin: device
    // position of a logical pixel's left/top edge. strength 0 = plain white.
    static platform::Image mask(int width, int height, double cell_width, double cell_height,
                                double origin_x, double origin_y);
    // Mask strength for a cell size: fades out where stripes cannot resolve.
    static double mask_strength(double cell_width);
    // Bilinear "diffusion" copy added on top of the masked image (0..255).
    static constexpr int glow = 96;

private:
    std::vector<float> state_; // persisted colour, 3 floats per logical pixel
    int width_{}, height_{};
    platform::Image output_{0, 0};
};
} // namespace gg
