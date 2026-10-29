// Written answers in the second adventure (ARM9 overlay 42). See
// a2_answer_puzzle.hpp; the rules are in written_answer.cpp.
#include "puzzles/a2_answer_puzzle.hpp"
#include "system/script.hpp"
#include <stdexcept>

namespace picarat {
namespace {
// Letter case folding (resident ARM9 table 02043218): a..z become A..Z.
int fold(int c) {
    return c >= 0 && c < 0x80 ? ((c >= 'a' && c <= 'z') ? c - 0x20 : c) : c;
}

// One answer slot as overlay 42 stores it (0xa8-byte record, 020f9c2c).
struct Slot {
    int mode = 0, count = 0; // count: number of boxes (at most 16)
    std::array<int, 16> expected{};
};

struct Layout {
    std::vector<Rect> boxes;
    int previewX = 0, previewY = 0, previewStep = 0;
    std::array<std::string, 5> erasers{};
};

// Box rectangles, preview row and eraser variables for the mode and number
// of boxes (020f8114..020f8628). Modes 4..6 have fixed layouts (and box
// counts); the others depend on the number of boxes. Lower-screen pixels.
Layout layoutFor(int mode, int boxCount) {
    Layout l;
    auto boxesAt = [&](std::initializer_list<int> xs, int y, int w, int h) {
        for (int x : xs) {
            l.boxes.push_back({x, y, w, h});
        }
    };
    if (mode == 4) {
        l.previewX = 0x65, l.previewY = 0x18, l.previewStep = 0x2e;
        boxesAt({0x39, 0x95}, 0x43, 0x37, 0x40);
        l.erasers.fill("pos5");
        return l;
    }
    if (mode == 5) {
        l.previewX = 0x48, l.previewY = 0x18, l.previewStep = 0x2e;
        boxesAt({0x15, 0x61, 0xad}, 0x43, 0x37, 0x40);
        l.erasers.fill("pos3");
        return l;
    }
    if (mode == 6) {
        l.previewX = 0x57, l.previewY = 0x19, l.previewStep = 0x16;
        boxesAt({0xb, 0x47, 0x87, 0xc3}, 0x43, 0x37, 0x40);
        l.erasers = {"pos6", "pos6", "pos7", "pos7", ""};
        return l;
    }
    switch (boxCount) {
    case 2:
        l.previewX = 0x6f, l.previewY = 0x15, l.previewStep = 0x18;
        boxesAt({0x35, 0x85}, 0x3a, 0x4b, 0x4a);
        l.erasers.fill("pos2");
        break;
    case 3:
        l.previewX = 100, l.previewY = mode == 0 ? 0x19 : 0x1a, l.previewStep = 0x18;
        boxesAt({0x2b, 0x67, 0xa3}, 0x43, 0x37, 0x40);
        l.erasers.fill("pos3");
        break;
    case 4:
        l.previewX = mode < 2 ? 0x58 : 0x56, l.previewY = 0x19, l.previewStep = 0x18;
        boxesAt({0xd, 0x49, 0x85, 0xc1}, 0x43, 0x37, 0x40);
        l.erasers.fill("pos4");
        break;
    default:
        // One box; with five boxes only the first gets a rectangle.
        l.previewX = 0x7c, l.previewY = 0x15, l.previewStep = 0;
        boxesAt({0x5d}, 0x3a, 0x4b, 0x4a);
        l.erasers.fill("pos1");
        break;
    }
    return l;
}
} // namespace

AnswerPuzzle::AnswerPuzzle(std::span<const std::uint8_t> script, unsigned type, unsigned internalId,
                           handwriting::Recognizer& recognizer)
    : recognizer_(recognizer) {
    // 020f78a0: the input mode follows the puzzle type.
    switch (type) {
    case 16:
        mode = 1;
        break;
    case 20:
        mode = 3;
        break;
    case 21:
        mode = 2;
        break;
    case 22:
        mode = 0;
        break;
    case 28:
        mode = 4;
        break;
    case 32:
        mode = 5;
        break;
    case 35:
        mode = 6;
        break;
    default:
        throw std::runtime_error("answer engine used for an unexpected puzzle type");
    }
    (void)internalId;
    std::array<Slot, 4> slots{};
    int slotCount = 0;
    boxCount = 1;
    ScriptContext context;
    context.bind(0x41, [&](CommandArguments& a) {
        // AnswerSlot(slot, x, y, boxes): 020f9234. The number of boxes (at
        // most 16 per slot) also sets the number of boxes shown.
        int slot = a.integer(), x = a.integer(), y = a.integer(), count = a.integer();
        (void)x;
        (void)y; // read by the handler but not used by overlay 42
        if (slot < 0 || slot >= int(slots.size())) {
            throw std::runtime_error("answer slot outside the four ov42 slots");
        }
        slots[size_t(slot)].count = count > 16 ? 16 : count;
        boxCount = count;
        ++slotCount;
    });
    context.bind(0x42, [&](CommandArguments& a) {
        // AnswerText(slot, characters): 020f9264. Digits are stored as their
        // value, letters folded to upper case.
        int slot = a.integer();
        std::string text = a.string();
        if (slot >= slotCount) {
            return;
        }
        Slot& s = slots[size_t(slot)];
        s.mode = mode;
        for (size_t i = 0; i < text.size() && i < 16; ++i) {
            int c = static_cast<signed char>(text[i]);
            s.expected[i] = s.mode != 1 ? c - 0x30 : fold(c);
        }
    });
    context.bind(0x43, [&](CommandArguments& a) {
        // AnswerBackground(name): 020f9294, "nazo/drawinput/<name>" without
        // extension; a leading '?' is the language folder ("ge").
        std::string name = a.string();
        auto dot = name.rfind('.');
        if (dot != std::string::npos) {
            name.resize(dot);
        }
        if (!name.empty() && name[0] == '?') {
            name = "ge" + name.substr(1);
        }
        background = "nazo/drawinput/" + name;
    });
    ScriptInterpreter(script).run(context);
    if (boxCount < 1 || boxCount > 5) {
        throw std::runtime_error("answer box count outside the five ov42 boxes");
    }

    // The answer screen's layout (modes 4..6 also fix the number of boxes).
    if (mode == 4) {
        boxCount = 2;
    } else if (mode == 5) {
        boxCount = 3;
    } else if (mode == 6) {
        boxCount = 4;
    }
    const Layout layout = layoutFor(mode, boxCount);
    previewX_ = layout.previewX;
    previewY_ = layout.previewY;
    previewStep_ = layout.previewStep;
    eraserVariables = layout.erasers;

    WrittenAnswer::Setup setup;
    setup.rules = WrittenAnswer::Rules::Adventure2;
    for (int i = 0; i < boxCount; ++i) {
        // 020f9510 then 020f9694: which character classes the recogniser may
        // return (letters for mode 1, digits otherwise), and its writing
        // guides (baseline 0x50, help line 0x28).
        WrittenAnswer::BoxSetup b;
        b.area = size_t(i) < layout.boxes.size() ? layout.boxes[size_t(i)] : Rect{};
        b.recognition.categories = mode == 1 ? 0x0c : (mode == 2 || mode == 3) ? 0x1c : 0x10;
        b.recognition.categories2 = -1;
        b.recognition.baseline = 0x50;
        b.recognition.helpline = 0x28;
        b.reading = mode == 1                  ? WrittenAnswer::Reading::Letter
                    : (mode == 2 || mode == 3) ? WrittenAnswer::Reading::Unread
                                               : WrittenAnswer::Reading::Digit;
        setup.boxes.push_back(b);
    }
    for (int i = 0; i < slotCount; ++i) {
        // Every slot must match; two values per box in modes 2 and 3.
        const Slot& s = slots[size_t(i)];
        WrittenAnswer::Answer answer;
        answer.values.assign(s.expected.begin(), s.expected.end());
        answer.compared = (s.mode == 2 || s.mode == 3) ? s.count * 2 : s.count;
        answer.boxes = s.count;
        setup.answers.push_back(answer);
    }
    // 020f89cc: which empty boxes block "submit". Mode 0 (as recorded in
    // slot 0) tests whether the last box has candidates; mode 6 the second
    // box of each pair; every other mode tests every box for strokes.
    if (slots[0].mode == 0) {
        setup.requireCandidates = true;
        setup.requiredBoxes = {boxCount - 1};
    } else if (mode == 6) {
        setup.requiredBoxes = {1, 3};
    } else {
        for (int i = 0; i < boxCount; ++i) {
            setup.requiredBoxes.push_back(i);
        }
    }
    answer_.emplace(std::move(setup), recognizer_);
}

void AnswerPuzzle::reset() {
    // 020f800c: clear the text layer, every box (020f9638) and the written
    // values (020f9de8).
    clearText = true;
    answer_->clear();
    timers_.fill(0);
}

void AnswerPuzzle::openAnswerScreen() {
    // 020f89cc "di_kaito": switch to the answer screen with empty boxes.
    answerScreen = true;
    reset();
}

void AnswerPuzzle::closeAnswerScreen() {
    // 020f89cc "di_modoru": back to the question screen.
    answerScreen = false;
    reset();
    clearText = true;
    alternateBackground = false;
}

void AnswerPuzzle::restart() {
    // The original recreates the object: boxes empty, pens lifted.
    closeAnswerScreen();
    WrittenAnswer::Setup setup = answer_->setup();
    answer_.emplace(std::move(setup), recognizer_);
}

void AnswerPuzzle::erase(int i) {
    // 020f89cc eraser: clear the box's preview cell and its ink (a little
    // wider than the box), then the box itself.
    int cell = previewStep_ + 1;
    textClears.push_back({i * previewStep_ + previewX_ - 3, previewY_ - 3, cell, cell});
    const Rect& r = answer_->box(i).setup.area;
    textClears.push_back({r.x - 5, r.y, r.width + 4, r.height + 5});
    answer_->erase(i);
    timers_[size_t(i)] = 0;
}

AnswerPuzzle::Submit AnswerPuzzle::submit() {
    return answer_->submit();
}

bool AnswerPuzzle::solved() const {
    return answerScreen && answer_->solved();
}

void AnswerPuzzle::updateBox(int index, const Touch& touch) {
    answer_->write(index, touch);
    for (const auto& c : answer_->ink) {
        ink.push_back(c);
    }
    answer_->ink.clear();
}

void AnswerPuzzle::updatePreview() {
    // 020f9068: for each box with writing, show the character read 16 frames
    // after the stroke, or "?" after 60 frames if nothing was read. Digits
    // are drawn as images 1..10 (value + 1); image 11 is the "?".
    previews_.clear();
    int x = previewX_;
    for (int i = 0; i < boxCount; ++i) {
        const auto& b = answer_->box(i);
        if (b.written()) {
            Preview p;
            p.x = x;
            p.y = previewY_;
            p.clear = mode == 1;
            auto& timer = timers_[size_t(i)];
            if (b.results[0] != -1) {
                if (timer < 0x10) {
                    ++timer;
                } else if (mode == 1) {
                    p.text = std::u16string(1, char16_t(b.results[0]));
                } else {
                    p.digit = b.results[0] + 1;
                }
            } else {
                if (timer < 0x3c) {
                    ++timer;
                } else if (mode == 1) {
                    p.text = u"?";
                } else {
                    p.digit = 0xb;
                }
            }
            previews_.push_back(p);
        }
        x += previewStep_;
        if (mode == 6 && i == 1) {
            x += 7;
        }
    }
}
} // namespace picarat
