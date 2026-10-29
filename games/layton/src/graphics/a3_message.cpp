// The third adventure's messages. See a3_message.hpp.
#include "graphics/a3_message.hpp"
#include "graphics/text.hpp"
#include <algorithm>
#include <cctype>

namespace picarat::adventure3 {
namespace {

bool digit(char c) {
    return c >= '0' && c <= '9';
}

// The digits from `from` up to `end` as a number (non-digits count as 0,
// as in 02069f5c).
int number(const std::string& s, std::size_t from, std::size_t end) {
    int n = 0;
    for (std::size_t i = from; i < end; ++i) {
        n = n * 10 + (digit(s[i]) ? s[i] - '0' : 0);
    }
    return n;
}

// 0206a1c4: the colour letters of <C?>.
int colourOf(char c) {
    switch (std::toupper(static_cast<unsigned char>(c))) {
    case 'B':
        return 1;
    case 'C':
        return 0;
    case 'F':
        return 6;
    case 'G':
        return 5;
    case 'O':
        return 7;
    case 'R':
        return 3;
    case 'S':
        return 4;
    case 'T':
        return 8;
    case 'W':
        return 2;
    default:
        return 0;
    }
}

// The layout state of 02069f5c.
struct Cursor {
    int x = 0, y = 0;
    std::size_t lineStart = 0; // the current line's step
    std::vector<int> colours;  // the colour stack (seven deep)
    int colour = 0;
    bool voiceOpen = false;
    long openPart = -1; // the open <T>'s step
};

class Compiler {
public:
    Compiler(MessageText::Style& s, const MessageText::Hooks& h, std::vector<MessageStep>& out)
        : style_(s), hooks_(h), steps_(out) {}

    void run(const std::string& text) {
        cursor_.colour = style_.defaultColour;
        cursor_.lineStart = steps_.size();
        steps_.push_back({MessageStep::Line, 0, 0, false, 0, 0, 0, cursor_.y});
        layout(text, 0);
        // 0206ad94: the last line closed, the text's end.
        closeLine();
        if (cursor_.voiceOpen) {
            MessageStep v{MessageStep::Voice, 0, 0, true};
            steps_.push_back(v);
        }
        if (style_.waitAtEnd) {
            if (!steps_.empty() && steps_.back().kind == MessageStep::Wait) {
                steps_.back().a = -1;
            } else {
                steps_.push_back({MessageStep::Wait, -1});
            }
        }
        if (cursor_.openPart >= 0) {
            closePart();
        }
        centre();
    }

private:
    int lineHeight() const { return style_.fontHeight + style_.lineSpacing; }

    void closeLine() {
        // A line's step: how many steps it holds, its width.
        MessageStep& l = steps_[cursor_.lineStart];
        l.b = int(steps_.size() - cursor_.lineStart);
        l.a = cursor_.x - style_.characterSpacing;
        totalHeight_ = cursor_.y + lineHeight();
    }

    void newLine() {
        closeLine();
        cursor_.x = 0;
        cursor_.y += lineHeight();
        cursor_.lineStart = steps_.size();
        steps_.push_back({MessageStep::Line, 0, 0, false, 0, 0, 0, cursor_.y});
    }

    void glyph(unsigned code) {
        MessageStep g{MessageStep::Glyph};
        g.code = code;
        g.colour = cursor_.colour;
        g.x = cursor_.x;
        g.y = cursor_.y + style_.baseline;
        steps_.push_back(g);
        cursor_.x += (style_.font ? textAdvance(*style_.font, code) : 0) + style_.characterSpacing;
    }

    void closePart() {
        steps_[std::size_t(cursor_.openPart)].a = int(steps_.size());
        steps_.push_back({MessageStep::PartEnd});
        cursor_.openPart = -1;
    }

    void layout(const std::string& t, int depth) {
        for (std::size_t i = 0; i < t.size(); ++i) {
            const char c = t[i];
            if (c > 0 && c < ' ' && c != '\n') {
                continue;
            }
            if (c == '\n') {
                newLine();
                continue;
            }
            if (c != '<') {
                glyph(std::uint8_t(c));
                continue;
            }
            const std::size_t end = t.find('>', i + 1);
            if (end == std::string::npos) {
                glyph('<');
                continue;
            }
            tag(t, i + 1, end, depth);
            i = end;
        }
    }

    void tag(const std::string& t, std::size_t from, std::size_t end, int depth) {
        std::size_t p = from;
        const bool closing = t[p] == '/';
        if (closing) {
            ++p;
        }
        const char letter = char(std::toupper(static_cast<unsigned char>(t[p])));
        switch (letter) {
        case 'A': { // <A#/#>
            const std::size_t slash = t.find('/', p + 1);
            if (!closing && slash != std::string::npos && slash <= end) {
                steps_.push_back(
                    {MessageStep::Animation, number(t, p + 1, slash), number(t, slash + 1, end)});
            }
            break;
        }
        case 'C':
            if (closing) {
                if (!cursor_.colours.empty()) {
                    cursor_.colour = cursor_.colours.back();
                    cursor_.colours.pop_back();
                }
            } else {
                if (cursor_.colours.size() < 7) {
                    cursor_.colours.push_back(cursor_.colour);
                }
                cursor_.colour = colourOf(t[p + 1]);
            }
            break;
        case 'N':
        case 'I': // a name / an item, laid out in place (vtable +0x28 / +0x2c)
            if (hooks_.insert && depth < 4) {
                layout(hooks_.insert(letter, number(t, p + 1, end)), depth + 1);
            }
            break;
        case 'K':
            steps_.push_back({MessageStep::Thinking, closing ? 0 : 1});
            break;
        case 'Q':
            steps_.push_back({MessageStep::Quiet, closing ? 0 : 1});
            break;
        case 'T':
            if (closing) {
                if (cursor_.openPart >= 0) {
                    closePart();
                }
            } else {
                cursor_.openPart = long(steps_.size());
                steps_.push_back({MessageStep::PartBegin});
            }
            break;
        case 'E':
        case 'F':
        case 'J':
        case 'P':
        case 'S':
        case 'V':
            // Only S, J and V have closing forms (0206a7a0).
            if (!closing || letter == 'S' || letter == 'J' || letter == 'V') {
                std::size_t q = p + 1;
                while (q < end && digit(t[q])) {
                    ++q;
                }
                MessageStep s{};
                s.kind = letter == 'E'   ? MessageStep::Expression
                         : letter == 'F' ? MessageStep::Face
                         : letter == 'J' ? MessageStep::Jingle
                         : letter == 'P' ? MessageStep::Pose
                         : letter == 'S' ? MessageStep::Effect
                                         : MessageStep::Voice;
                s.a = number(t, p + 1, q);
                s.closing = closing;
                if (s.kind == MessageStep::Voice) {
                    cursor_.voiceOpen = !closing;
                }
                steps_.push_back(s);
            }
            break;
        case 'W':
            if (!closing) {
                MessageStep w{MessageStep::Wait, 60};
                if (p + 1 < end) {
                    if (digit(t[p + 1])) {
                        w.a = number(t, p + 1, end);
                    } else if (std::toupper(static_cast<unsigned char>(t[p + 1])) == 'T') {
                        if (cursor_.voiceOpen) {
                            break; // a voice decides when the line ends
                        }
                        w.a = -1;
                    }
                }
                steps_.push_back(w);
            }
            break;
        default:
            break;
        }
    }

    void centre() {
        // 0206ad94: lines centred about x 0 (left moves by half the width,
        // rounded down), the whole text about y 0 (up by half its height,
        // rounded up).
        if (style_.centerX) {
            for (std::size_t i = 0; i < steps_.size(); ++i) {
                if (steps_[i].kind != MessageStep::Line) {
                    continue;
                }
                const int half = (steps_[i].a - steps_[i].x) >> 1;
                steps_[i].x -= half;
                steps_[i].a -= half;
                for (std::size_t k = i + 1;
                     k < i + std::size_t(std::max(steps_[i].b, 1)) && k < steps_.size(); ++k) {
                    if (steps_[k].kind == MessageStep::Glyph ||
                        steps_[k].kind == MessageStep::Inline) {
                        steps_[k].x -= half;
                    }
                }
            }
        }
        if (style_.centerY) {
            const int total = totalHeight_ - style_.lineSpacing + style_.baseline;
            const int half = total - (total >> 1);
            for (auto& s : steps_) {
                if (s.kind == MessageStep::Line || s.kind == MessageStep::Glyph ||
                    s.kind == MessageStep::Inline) {
                    s.y -= half;
                }
            }
        }
    }

    MessageText::Style& style_;
    const MessageText::Hooks& hooks_;
    std::vector<MessageStep>& steps_;
    Cursor cursor_;
    int totalHeight_ = 0;
};

} // namespace

void MessageText::clear() {
    steps_.clear();
    shown_ = 0;
    progress_ = 0;
    countdown_ = 0;
    inPart_ = waitingTap_ = quiet_ = thinking_ = false;
    typingCount_ = 10000;
}

void MessageText::set(const std::string& text, const GameText& escapes, bool instant) {
    clear();
    Compiler(style, hooks, steps_).run(escapes.expand(text));
    if (instant) {
        update(false, true, 0);
    }
}

void MessageText::update(bool tapped, bool instant, int elapsed) {
    // 0206b5b4.
    if (steps_.empty()) {
        return;
    }
    ++typingCount_;
    bool stopped = false;
    if (waitingTap_) {
        if (!tapped && !instant) {
            return;
        }
        // The tap that ends the wait does nothing else.
        waitingTap_ = false;
        tapped = false;
        stopped = true;
    }
    bool skip = instant;
    if (!instant) {
        if (tapped && inPart_ && std::size_t(partEnd_) > shown_) {
            // A tap inside a <T> part jumps to its end.
            countdown_ = 0;
            progress_ = partEnd_ << 12;
            inPart_ = false;
            skip = true;
            stopped = true;
        }
        if (countdown_ > 0) {
            --countdown_;
            return;
        }
    }
    if (!instant) {
        progress_ += int((std::int64_t(elapsed) * style.speed) >> 12);
        progress_ = std::min(progress_, int(steps_.size()) << 12);
    } else {
        progress_ = int(steps_.size()) << 12;
    }
    std::size_t limit = std::size_t(progress_ >> 12);
    std::size_t i = shown_;
    for (; i < limit && i < steps_.size(); ++i) {
        const MessageStep& s = steps_[i];
        bool hold = false;
        switch (s.kind) {
        case MessageStep::Glyph:
            break;
        case MessageStep::Inline:
            ++limit;
            break;
        case MessageStep::Wait:
            countdown_ = 0;
            if (!instant) {
                if (s.a < 0) {
                    waitingTap_ = true;
                    hold = true;
                } else if (!skip) {
                    countdown_ = s.a;
                    hold = true;
                }
            }
            if (hold) {
                ++i; // the wait itself is passed
            }
            break;
        case MessageStep::PartBegin:
            ++limit;
            partEnd_ = s.a;
            inPart_ = true;
            break;
        case MessageStep::PartEnd:
            ++limit;
            inPart_ = false;
            break;
        case MessageStep::Quiet:
            ++limit;
            quiet_ = s.a != 0;
            break;
        case MessageStep::Thinking:
            ++limit;
            thinking_ = s.a != 0;
            break;
        default: // lines and the owner's steps (vtable +0x30)
            if (hooks.step && !hooks.step(s)) {
                stopped = true;
                hold = true;
            } else {
                ++limit;
            }
            break;
        }
        if (hold) {
            break;
        }
    }
    shown_ = std::min(i, steps_.size());
    progress_ = int(shown_ << 12) | (progress_ & 0xfff);
    if (progress_ > int(steps_.size()) << 12) {
        progress_ = int(steps_.size()) << 12;
    }
    // The typing sound while glyphs keep coming.
    if (shown_ != 0 && !quiet_ && !stopped && !finished() && typingInterval_ > 0 &&
        typingCount_ >= typingInterval_) {
        if (hooks.typing && typingSound_ >= 0) {
            hooks.typing(typingSound_);
        }
        typingCount_ = 0;
    }
    if (finished()) {
        typingCount_ = 10000;
        inPart_ = false;
    }
}

void MessageText::draw(Surface& image, int x, int y, const std::vector<std::uint16_t>& palette,
                       int alpha) const {
    if (alpha <= 0 || !style.font) {
        return;
    }
    for (std::size_t i = 0; i < shown_ && i < steps_.size(); ++i) {
        const auto& s = steps_[i];
        if (s.kind != MessageStep::Glyph) {
            continue;
        }
        const std::uint16_t colour = s.colour >= 0 && std::size_t(s.colour) < palette.size()
                                         ? palette[std::size_t(s.colour)]
                                         : 0;
        textGlyph(image, *style.font, s.code, x + s.x, y + s.y, std::uint16_t(colour | 0x8000));
    }
}

} // namespace picarat::adventure3
