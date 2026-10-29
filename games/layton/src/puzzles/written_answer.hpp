#pragma once
// Written answers: the puzzles the player answers by writing characters
// (digits, letters, a few symbols) into boxes on the touch screen. This
// engine holds the native rules. The second adventure's puzzle is native and
// uses all of it (a2_answer_puzzle.cpp). The third adventure scripts its answer
// screen; there the engine is only the writing box behind the script's
// canvases (a3_lscr_scene.cpp, 47xx: write, erase, the character read) and the
// scripts submit and judge themselves.
//
// How the puzzles play
// --------------------
// The answer screen shows one to six boxes. The player writes one character
// into each box. Whenever a stroke ends, the box is read by the games'
// handwriting recogniser (handwriting.hpp; both adventures link the same
// recogniser and database) and the character it read is shown with the box.
// An eraser per box and a "clear" button empty boxes. "Submit" checks the
// boxes: writing that could not be read, or boxes left empty, stop the
// submission; otherwise the characters are compared with the accepted
// answers.
//
// Where the adventures differ, the Rules say which behaviour applies:
//
//   Writing (Box::write)
//     second: a point is recorded every frame while the stylus is held
//       inside the box, in screen coordinates; at most 256 points and 8
//       strokes; the ink is drawn only inside the box (ov42 020f9a28).
//     third: a stroke starts only when the stylus touches down inside the
//       box and then follows it anywhere; a point is recorded once the
//       stylus has moved more than 2 pixels (|dx|+|dy|) from the last point,
//       and at the release; points are relative to the box's corner; at
//       most 256 points and 32 strokes (ov6 020cbc6c, 020cbd88, 020cbea8).
//     first: as the second (ARM9 02052634, the same routine down to its
//       quirks), but only while the stylus sample is steady, and a box can
//       be an ink-only memo pad that records and reads nothing.
//   Reading (Box::read)
//     second: up to three distinct candidates are kept, as digit values or
//       letters folded to upper case, by input mode (ov42 020f9780).
//     first: the same (ARM9 020523bc), with -2 instead of -1 for "nothing".
//     third: only the first character of the best candidate is kept, "?"
//       when nothing was read (ov6 020cbea8, 020eac4c).
//   Submitting (submit, second adventure)
//     an unreadable box shows "input failed"; a box left empty (which boxes
//     count depends on the mode) only plays a sound (ov42 020f89cc).
//   Judging (solved, second adventure)
//     every answer slot must match, digit by digit (ov42 020f8078).
#include "puzzles/handwriting.hpp"
#include "system/interaction.hpp"
#include <array>
#include <map>
#include <string>
#include <vector>

namespace picarat {
// One drawing command for the ink layer, in screen coordinates (second
// adventure: ov2 02071470 dot / 020714dc line; third: segments of the pen
// path, ov0 0206caec).
struct InkCommand {
    bool line = false;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};

class WrittenAnswer {
public:
    enum class Rules { Adventure1, Adventure2, Adventure3 };

    // How a box's reading becomes a value (second adventure: by input mode).
    enum class Reading {
        Digit,     // the digit's value 0..9, or -1 (ov42 020f94f4)
        Letter,    // the letter folded to upper case (ov42 020f9780)
        Unread,    // the value stays -1 (second adventure's modes 2 and 3)
        Character, // the character's code; 0 = empty (third adventure)
    };

    struct BoxSetup {
        Rect area;
        handwriting::Settings recognition;
        Reading reading = Reading::Digit;
        // First adventure: false for the memo pad (canvas +0xd1): ink only.
        bool record = true;
    };

    // One answer slot: the values expected in the boxes, in order, and how
    // many of them the check compares (second adventure: the slot's box
    // count times valuesPerBox, at most 16; third: all of them).
    struct Answer {
        std::vector<int> values;
        int compared = 0;
        int boxes = 0; // second adventure: boxes written into slot 0 at submission
    };

    struct Setup {
        Rules rules = Rules::Adventure2;
        std::vector<BoxSetup> boxes;
        // Second adventure: every slot must match.
        std::vector<Answer> answers;
        // Second adventure: which boxes must be written before submitting
        // (mode 0: the last box only, mode 6: boxes 1 and 3, else all).
        std::vector<int> requiredBoxes;
        bool requireCandidates = false; // mode 0 tests the candidates, not the strokes
        // Third adventure: the recogniser's characters (UTF-16) as the
        // game's single-byte codes (dcm/code, loaded by ov6 020ea6c8); a
        // character missing from the table reads as nothing.
        std::map<std::uint16_t, std::uint8_t> characterCodes;
    };

    // One writing box.
    struct Box {
        BoxSetup setup;
        std::vector<handwriting::Point> points;   // every stroke's points, at most 256
        std::vector<std::pair<int, int>> strokes; // {count, first point}
        int strokeStart = 0, strokePoints = 0;    // the stroke being written
        bool drawing = false;                     // the stylus is writing here
        int lastX = 0, lastY = 0;                 // previous stylus point (-16: none)
        std::vector<InkCommand> pendingInk;       // third: ink not drawn yet
        std::uint8_t distinct = 0;                // distinct candidates kept
        std::array<int, 3> results{};             // best candidates (-1 / -2 = none)

        bool written() const { return !strokes.empty(); }
    };

    // What "submit" found (the scene reacts in the game's way).
    enum class Submit { Unrecognized, Missing, Judge };

    WrittenAnswer(Setup setup, handwriting::Recognizer& recognizer);

    const Setup& setup() const { return setup_; }

    Rules rules() const { return setup_.rules; }

    int boxCount() const { return int(boxes_.size()); }

    const Box& box(int index) const { return boxes_.at(size_t(index)); }

    // One frame of stylus input for box `index`; returns true when a stroke
    // ended and the box was read again.
    bool write(int index, const Touch& touch);

    // Empties one box, or all of them.
    void erase(int index);
    void clear();

    // "Submit": checks the boxes and, when they may be judged, copies their
    // readings into the written values.
    Submit submit();

    // The written values match the accepted answers.
    bool solved() const;

    // The character shown for box `index` in the third adventure: empty,
    // "?" or the character read.
    std::string shown(int index) const;

    // Ink drawn since the scene last took it.
    std::vector<InkCommand> ink;

private:
    void writeAdventure2(Box& box, const Touch& touch, bool& read);
    void writeAdventure3(Box& box, const Touch& touch, bool& read);
    void read(Box& box);
    static void empty(Box& box);

    Setup setup_;
    std::vector<Box> boxes_;
    std::vector<int> written_; // the values copied at the last submission
    handwriting::Recognizer& recognizer_;
};
} // namespace picarat
