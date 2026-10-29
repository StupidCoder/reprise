// The written-answer engine shared by the adventures. See written_answer.hpp.
#include "puzzles/written_answer.hpp"
#include <cstdlib>
#include <stdexcept>

namespace picarat {
namespace {
// Letter case folding (second adventure, resident ARM9 table 02043218):
// a..z become A..Z, everything else stays.
int fold(int c) {
    return c >= 0 && c < 0x80 ? ((c >= 'a' && c <= 'z') ? c - 0x20 : c) : c;
}

// The value of a decimal digit character, or -1 (ov42 020f94f4).
int digitValue(unsigned c) {
    return c > 0x2f && c < 0x3a ? int(c) - 0x30 : -1;
}

// Point and stroke capacities of a box (second adventure: ov42 020f96e4 and
// 020f9728; third: the lists created by ov6 020cb984, 0x100 points and
// 0x20 strokes).
constexpr size_t kMaxPoints = 256;
constexpr size_t kMaxStrokes2 = 8, kMaxStrokes3 = 32;
} // namespace

WrittenAnswer::WrittenAnswer(Setup setup, handwriting::Recognizer& recognizer)
    : setup_(std::move(setup)), recognizer_(recognizer) {
    if (setup_.boxes.empty() || setup_.boxes.size() > 6) {
        throw std::runtime_error("written answer needs one to six boxes");
    }
    for (const auto& b : setup_.boxes) {
        Box box;
        box.setup = b;
        boxes_.push_back(std::move(box));
    }
    written_.assign(16, 0);
    clear();
}

void WrittenAnswer::empty(Box& b) {
    // Second adventure 020f9638, third 020cc0c4: no strokes, nothing read.
    b.points.clear();
    b.strokes.clear();
    b.strokeStart = b.strokePoints = 0;
    b.lastX = b.lastY = 0;
    b.results = {0, 0, 0};
    b.distinct = 0;
}

void WrittenAnswer::erase(int index) {
    empty(boxes_.at(size_t(index)));
}

void WrittenAnswer::clear() {
    for (int i = 0; i < boxCount(); ++i) {
        erase(i);
    }
    written_.assign(16, 0);
}

bool WrittenAnswer::write(int index, const Touch& touch) {
    Box& b = boxes_.at(size_t(index));
    bool read = false;
    if (setup_.rules != Rules::Adventure3) {
        writeAdventure2(b, touch, read);
    } else {
        writeAdventure3(b, touch, read);
    }
    return read;
}

void WrittenAnswer::writeAdventure2(Box& b, const Touch& t, bool& read) {
    // ov42 020f9a28 (first adventure: ARM9 02052634). A stroke ends when the
    // stylus is lifted; the box is then read again with all its strokes (at
    // most eight are kept). The first adventure's scenes update their
    // buttons first, so its own steadiness test (0200eeb4) always passes
    // here: the scene passes only steady samples.
    if (t.released && b.drawing) {
        b.lastX = b.lastY = -16;
        b.drawing = false;
        if (!b.setup.record) {
            return; // the memo pad: neither strokes nor reading
        }
        // 020f9728: close the stroke.
        if (b.strokes.size() < kMaxStrokes2) {
            b.strokes.push_back({b.strokePoints, b.strokeStart});
            b.strokeStart = int(b.points.size());
            b.strokePoints = 0;
        }
        this->read(b);
        read = true;
    }
    const Rect& r = b.setup.area;
    if (!(t.held && r.contains(t.x, t.y))) {
        b.lastX = b.lastY = -16;
        return;
    }
    const int x = t.x, y = t.y;
    if (!t.pressed) {
        if (r.contains(x, y) && r.contains(b.lastX, b.lastY)) {
            b.drawing = true;
            if (b.lastX != -16 && b.lastY != -16 && x != -16 && y != -16) {
                ink.push_back({true, b.lastX, b.lastY, x, y});
            }
        }
    } else if (r.contains(x, y) && x != -16 && y != -16) {
        ink.push_back({false, x, y, x, y});
        b.drawing = true;
    }
    // 020f96e4: record the point (up to 256 per box).
    if (b.setup.record && b.points.size() < kMaxPoints) {
        b.points.push_back({std::int16_t(x), std::int16_t(y)});
        ++b.strokePoints;
    }
    b.lastX = x & 0xffff;
    b.lastY = y & 0xffff;
}

void WrittenAnswer::writeAdventure3(Box& b, const Touch& t, bool& read) {
    // The paint layer's input (ov0 0206c9d8) with the box's stroke
    // recording (ov6). lastX/lastY hold the pen position relative to the
    // box; strokePoints > 0 marks a stroke being recorded.
    const Rect& r = b.setup.area;
    if (!b.drawing && t.pressed) {
        // 0206c6c8: only a press inside the box starts writing (the right
        // and bottom edges are outside).
        const unsigned dx = unsigned(t.x - r.x), dy = unsigned(t.y - r.y);
        if (dx < unsigned(r.width) && dy < unsigned(r.height)) {
            b.drawing = true;
            b.lastX = int(dx);
            b.lastY = int(dy);
            b.pendingInk.clear();
            // 020cbc6c: the first point opens a new stroke (if both lists
            // have room).
            b.strokePoints = 0;
            if (b.points.size() < kMaxPoints) {
                b.points.push_back({std::int16_t(dx), std::int16_t(dy)});
                if (b.strokes.size() < kMaxStrokes3) {
                    b.strokeStart = int(b.points.size()) - 1;
                    b.strokePoints = 1;
                    b.strokes.push_back({1, b.strokeStart});
                }
            }
        }
    }
    if (!b.drawing) {
        return;
    }
    // 0206c774: the pen follows the stylus, inside the box or not; the ink
    // is drawn along every position (0206caec, pen mode 5). On the release
    // frame the touch state still reports the last position (observed in
    // ROM runs: the final point equals the last held position).
    const int x = t.held ? t.x - r.x : b.lastX, y = t.held ? t.y - r.y : b.lastY;
    // The ink trails the pen by three frames and what is still pending at
    // the release is never drawn (ROM captures: every stroke lacks its last
    // three segments; the paint layer's buffering behind it is not
    // recovered).
    b.pendingInk.push_back({true, b.lastX + r.x, b.lastY + r.y, x + r.x, y + r.y});
    if (!t.held) {
        b.pendingInk.clear();
    } else if (b.pendingInk.size() > 3) {
        ink.push_back(b.pendingInk.front());
        b.pendingInk.erase(b.pendingInk.begin());
    }
    b.lastX = x;
    b.lastY = y;
    auto add = [&](int px, int py) {
        if (b.points.size() < kMaxPoints) {
            b.points.push_back({std::int16_t(px), std::int16_t(py)});
            b.strokes.back().first = ++b.strokePoints;
        }
    };
    if (b.strokePoints > 0) {
        // 020cbd88: a new point once the pen is more than 2 pixels away from
        // the last one.
        const auto& last = b.points.back();
        if (std::abs(last.x - x) + std::abs(last.y - y) > 2) {
            add(x, y);
        }
    }
    if (t.held) {
        return;
    }
    // 0206c800 + 020cbea8: the release adds the final position (if it
    // differs from the last point) and reads the box.
    b.drawing = false;
    if (b.strokePoints > 0) {
        const auto& last = b.points.back();
        if (last.x != x || last.y != y) {
            add(x, y);
        }
        this->read(b);
        read = true;
    }
    b.strokePoints = 0;
}

void WrittenAnswer::read(Box& b) {
    if (b.strokes.empty() || b.points.empty()) {
        return;
    }
    std::vector<handwriting::Stroke> strokes;
    for (auto [count, start] : b.strokes) {
        strokes.emplace_back(b.points.begin() + start, b.points.begin() + start + count);
    }
    std::vector<handwriting::Result> out;
    if (setup_.rules == Rules::Adventure3) {
        // ov6 020cbea8 -> 020eac4c: four candidates are asked for, only the
        // first character of the best one is kept, as the game's byte code
        // (020eab84); "?" when nothing was read. More than six strokes read
        // as nothing (the recogniser refuses them).
        recognizer_.recognize(strokes, b.setup.recognition, 4, out);
        int code = '?';
        if (!out.empty()) {
            const std::uint16_t c = out[0].text.empty() ? 0 : out[0].text[0];
            const auto it = setup_.characterCodes.find(c);
            code = it == setup_.characterCodes.end() ? 0 : it->second;
        }
        b.results = {code, -1, -1};
        b.distinct = code != '?' && code != 0 ? 1 : 0;
        return;
    }
    // Second adventure, ov42 020f9780 (first: 020523bc): keep up to three
    // distinct candidates, best first (-1 for none; the first adventure
    // uses -2, also for a character that is not a digit).
    const int none = setup_.rules == Rules::Adventure1 ? -2 : -1;
    recognizer_.recognize(strokes, b.setup.recognition, 8, out);
    b.distinct = std::uint8_t(out.size());
    b.results = {none, none, none};
    unsigned k = 0;
    switch (b.setup.reading) {
    case Reading::Digit:
        for (size_t j = 0; j < out.size(); ++j) {
            b.results[k] = digitValue(out[j].text.empty() ? 0 : out[j].text[0]);
            if (b.results[k] < 0) {
                b.results[k] = none;
            }
            bool dup = false;
            for (unsigned r = 0; r < k; ++r) {
                if (b.results[k] == b.results[r]) {
                    --b.distinct;
                    dup = true;
                    break;
                }
            }
            if (!dup && ++k >= 3) {
                break;
            }
        }
        break;
    case Reading::Letter:
        // Letters are folded to upper case. (Two non-puzzle scenes, 0x12 and
        // 0x25, keep the raw codes; every puzzle scene folds.)
        for (size_t j = 0; j < out.size(); ++j) {
            int c = out[j].text.empty() ? 0 : out[j].text[0];
            b.results[k] = fold(c);
            if (b.results[k] == 0 && none == -1) {
                b.results[k] = -1;
            }
            bool dup = false;
            for (unsigned r = 0; r < k; ++r) {
                if (b.results[k] == b.results[r]) {
                    --b.distinct;
                    dup = true;
                    break;
                }
            }
            if (!dup) {
                if (++k >= 3) {
                    break;
                }
                if (b.results[k] == 0) {
                    b.results[k] = none;
                }
            }
        }
        break;
    case Reading::Unread:
    case Reading::Character:
        break; // every result stays -1
    }
    if (b.distinct > 3) {
        b.distinct = 3;
    }
    if (b.results[0] == none) {
        b.distinct = 0;
    }
}

WrittenAnswer::Submit WrittenAnswer::submit() {
    // Second adventure, ov42 020f89cc "di_hantei": a box whose writing could
    // not be read blocks the submission, then a required box left empty.
    for (const auto& b : boxes_) {
        if (b.results[0] == -1) {
            return Submit::Unrecognized;
        }
    }
    for (int i : setup_.requiredBoxes) {
        const Box& b = boxes_.at(size_t(i));
        if (setup_.requireCandidates ? b.distinct == 0 : b.strokes.empty()) {
            return Submit::Missing;
        }
    }
    // 020f9e50: each box's best candidate becomes the written value of
    // answer slot 0 (as many boxes as the slot has).
    const int count = setup_.answers.empty() ? 0 : setup_.answers[0].boxes;
    for (int i = 0; i < boxCount(); ++i) {
        if (i < count) {
            written_[size_t(i)] = boxes_[size_t(i)].results[0];
        }
    }
    return Submit::Judge;
}

bool WrittenAnswer::solved() const {
    const auto& answers = setup_.answers;
    // Second adventure, ov42 020f8078: every slot needs a first value; then
    // every slot must equal its expected values. Only slot 0 receives the
    // written values; the others hold zeros.
    for (size_t i = 0; i < answers.size(); ++i) {
        if ((i == 0 ? written_[0] : 0) < 0) {
            return false;
        }
    }
    for (size_t i = 0; i < answers.size(); ++i) {
        const Answer& a = answers[i];
        for (int k = 0; k < a.compared && k < 16; ++k) {
            const int value = i == 0 ? written_[size_t(k)] : 0;
            if (value != a.values[size_t(k)]) {
                return false;
            }
        }
    }
    return true;
}

std::string WrittenAnswer::shown(int index) const {
    const int c = boxes_.at(size_t(index)).results[0];
    return c > 0 ? std::string(1, char(c)) : std::string();
}
} // namespace picarat
