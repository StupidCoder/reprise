#pragma once
// The judgment sequence after "Submit" (overlay 2, 0206b4fc).
//
// When the player submits an answer, the game plays a short animated
// sequence before revealing "correct" or "wrong": a series of full-screen
// pictures from one of two picture sets (judge_l* / judge_r*), each fading
// in from black, with sound effects, ending on the verdict picture. Then the
// pictures slide between the two screens and a voice line is spoken.
//
// The sequence is data driven: a 114-byte table in overlay 2 (at 02086f18,
// copied into the pack as puzzle/judgment.tbl) holds 19 rows of three 16-bit
// values: the picture number for "correct", the picture number for "wrong",
// and how many frames the picture fades in. Picture 0 means "fade to black
// and pause"; 0xffff ends the sequence. Pictures 14 and 114 are the verdict
// pictures (with German text, so they live in the "ge" folder).
//
// The host draws, waits and plays sounds; runJudgment() makes every decision
// in the original order.
#include "system/resources.hpp"
#include <functional>
#include <string>

namespace picarat {

// What the judgment sequence needs from the scene that shows it.
struct JudgmentHost {
    virtual ~JudgmentHost() = default;
    virtual bool aborted() = 0;
    // Shows a judgment picture on the upper screen; `final` marks the verdict.
    virtual void image(const std::string& path, bool final) = 0;
    // Screen brightness: -16 (black) .. 0 (normal).
    virtual void brightness(int value) = 0;
    virtual void present() = 0;

    // 0206b8d0: the frame after the slide is presented without polling input.
    virtual void presentWithoutPoll() { present(); }

    // Waits `frames` frames; wait(0) still waits one frame, as in the game.
    virtual void wait(int frames) = 0;
    // Vertical offsets of the lower and upper screen pictures during the slide.
    virtual void slide(int bottomOffset, int topOffset) = 0;
    virtual void sound(unsigned id) = 0;

    // Loads sound-effect bank `number` (0207f884(sound, 2, number)).
    virtual void effectBank(int number) { (void)number; }

    // Hardware display windows used while sliding (0206b5bc, the loop,
    // 0206b8d4, 0206b940) and the upper screen's brightness reset after the
    // last picture (0206b7e4). Hosts that compose the slide themselves may
    // ignore these.
    virtual void openWindows() {}

    virtual void clipWindows(int bottomOffset, int topOffset) {
        (void)bottomOffset;
        (void)topOffset;
    }

    virtual void clearWindows() {}

    virtual void windowsOff() {}

    virtual void upperBrightness(int value) { (void)value; }

    // Plays a spoken voice line.
    virtual void voice(unsigned id) = 0;
};

// The picture part of the judgment, shared by the first adventure (ARM9
// 0200bde0, called by its result screen, which then slides and speaks
// itself) and the second (inside 0206b4fc). Both run the same rows: show a
// picture at the current brightness, play the verdict sound with picture
// 10 / 110, fade in by 7 per frame (2 for the verdict pictures 14 / 114),
// pause rows (0) wait 6 frames, fade to black over 17 frames and hold black
// for the row's count, and 0xffff ends with the upper screen's brightness
// back at 0. What differs is where the pictures are and the sound numbers.
struct JudgmentPictures {
    // The picture of `index` from set 'l' or 'r'; `verdict` for 14 / 114.
    std::function<std::string(char set, unsigned index, bool verdict)> path;
    unsigned correctSound = 0, wrongSound = 0;
};

void playJudgmentPictures(std::span<const std::uint8_t> table, unsigned mode,
                          const JudgmentPictures& pictures, JudgmentHost& host);

// Plays the judgment (overlay 2, 0206b4fc).
//   mode bit 0: the answer was wrong; mode bit 1: use the "r" picture set and
//               its voice lines.
//   special:    the fixed voice lines used by a few puzzles.
//   randomValue picks one of three voice lines.
void runJudgment(std::span<const std::uint8_t> table, unsigned mode, bool special,
                 unsigned randomValue, JudgmentHost& host);

// The game's random number generator (ARM9 02034dac): a linear congruential
// generator returning 15-bit values, 0..32767.
std::uint32_t gameRandom(std::uint32_t& state);

} // namespace picarat
