#pragma once
// Written answers in the second adventure (puzzle types 16, 20, 21, 22, 28,
// 32 and 35): reads the puzzle's parameters into the shared written-answer
// engine (written_answer.hpp) and keeps the answer screen's layout and the
// delayed character previews of this game.
// ARM9 overlay 42: ROM 0xb2600 (+0x18b0 bytes), loaded at 020f78a0, vtable 020f9f08.
//
// How the puzzles play
// --------------------
// These puzzles are answered by writing on the touch screen. The "answer"
// button opens the answer screen with one to four boxes; the player writes
// one character (a digit or a letter) in each box. Each box is read as soon
// as its stroke ends; the character read appears as a small preview above
// the boxes after a short delay, or "?" if nothing was read. An eraser
// button per box clears it. "Submit" checks the characters against the
// expected answer (rules in written_answer.hpp).
//
// The puzzle type selects the input mode (mode 0..6, field +0x6c):
//   type 22 -> mode 0, digits (layout by number of boxes)
//   type 16 -> mode 1, letters (compared case-insensitively)
//   type 21 -> mode 2, type 20 -> mode 3 (the readings are left at -1 in
//              these modes; two values are compared per box)
//   type 28 -> mode 4, digits in two boxes
//   type 32 -> mode 5, digits in three boxes
//   type 35 -> mode 6, digits in four boxes, laid out as two pairs
//
// The parameter script gives the answer slots (command 0x41), the expected
// characters (0x42) and the answer screen's background picture (0x43); the
// commands arrive through overlay 2 (02079dd0, 02079e50, 02079eac) at
// virtual functions +160/+164/+168.
#include "puzzles/written_answer.hpp"
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace picarat {
class AnswerPuzzle {
public:
    using Submit = WrittenAnswer::Submit;

    AnswerPuzzle(std::span<const std::uint8_t> script, unsigned type, unsigned internalId,
                 handwriting::Recognizer& recognizer);

    // Clears the ink and text layers, the boxes and the written values
    // (virtual function +204, 020f800c).
    void reset();

    // The button handlers of 020f89cc: "answer" (di_kaito) opens the answer
    // screen, "back" (di_modoru) returns to the question.
    void openAnswerScreen();
    void closeAnswerScreen();

    // "Try again" after a wrong answer: the original recreates the object,
    // so the question screen returns with empty boxes.
    void restart();

    // The eraser button of box `box`.
    void erase(int box);

    // "Submit" (di_hantei).
    Submit submit();

    // The answer check (virtual function +1fc, 020f8078): only on the
    // answer screen.
    bool solved() const;

    // One frame of writing in box `index` (020f8788 -> 020f9a28).
    void updateBox(int index, const Touch& touch);

    // Advances the delayed preview of each character read (020f9068).
    void updatePreview();

    // A preview cell above box i (layout fields +0xac/+0xb0/+0xb4): a digit
    // image, a letter as text, or nothing yet.
    struct Preview {
        int x = 0, y = 0;
        bool clear = false;
        std::optional<int> digit;
        std::optional<std::u16string> text;
    };

    std::vector<Preview> previews() const { return previews_; }

    WrittenAnswer& engine() { return *answer_; }

    bool answerScreen = false;                            // +0x68
    int mode = 0;                                         // +0x6c: input mode (see top)
    int boxCount = 1;                                     // +0x94
    std::string background = "nazo/drawinput/drawinput4"; // +0xd5: answer screen picture
    std::string eraserPosition = "pos4";                  // layout variable of the eraser buttons
    std::array<std::string, 5> eraserVariables{};         // per box (mode 6 splits pos6 / pos7)
    bool alternateBackground = false; // +0x56: picture toggle of internal puzzle 58
    std::vector<InkCommand> ink;      // ink drawn since the scene last took it
    std::vector<Rect> textClears;     // text-layer rectangles to clear
    bool clearText = false;           // whole text layer to clear

private:
    handwriting::Recognizer& recognizer_;
    std::optional<WrittenAnswer> answer_;
    std::array<std::uint8_t, 5> timers_{};              // +0xd0: preview delay per box
    int previewX_ = 0, previewY_ = 0, previewStep_ = 0; // +0xac: preview row layout
    std::vector<Preview> previews_;
};
} // namespace picarat
