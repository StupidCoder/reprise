// The 3DS adventures' message engine. See a56_message.hpp.
#include "graphics/a56_message.hpp"
#include "graphics/a56_font.hpp"
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace picarat::adventure56 {

using xq::Value;

namespace {

// Item types of the timeline (0x172190 / 0x16db60).
enum : int {
    kGlyph = 0,       // a character
    kRubyBase = 1,    // a character under a reading
    kRubyReading = 2, // a character of the reading
    kRubyEnd = 3,
    kIcon = 4,
    kNewLine = 5,
    kWait = 6,       // <W n>
    kMark = 7,       // a tap ends the wait
    kStretch = 8,    // <T>
    kStretchEnd = 9, // </T>
    kColour = 10,    // <C...>
    kColourEnd = 11, // </C>
    kSpeed = 12,     // <R s>
    kSpeedEnd = 13,  // </R>
    kOffsetX = 14,   // <X n>
    kOffsetY = 15,   // <Y n>
    // Talk commands (0x16db60 -> 0x16e678).
    kPose = 0x3e9,     // <P n>
    kFace = 0x3ea,     // <F n>
    kAnim = 0x3eb,     // <A a/b>
    kMotion = 0x3ec,   // <M a/b/c[/d]>
    kMotionOf = 0x3ed, // <ME name/a/b/c[/d]>
    kUsers = 0x3ee,    // <U names...>
    kKeep = 0x3ef,     // <K>
    kQuiet = 0x3f0,    // <Q>
    kTyping = 0x3f1,   // <O n>
    kSound = 0x3f2,    // <S n>
    kScript = 0x3f3,   // <SEQ ...>
    kVoice = 0x3f4,    // <V n [f]> / </V>
    kJingle = 0x3f5,   // <J n> / </J>
    kDelay = 0x3f6,    // <L s>
    kSelect = 0x3f8,   // <SEL a [b]>
    kLock = 0x3f9,     // <LOCK> / <UNLOCK>
    kPage = 0x3fa,     // <B>
    kPageI = 0x3fb,    // <I>
    kCount = 0x3fc,    // <D>
};

bool space(unsigned char c) {
    return c == ' ' || c == '\t' || c == 7 || c == 8 || c == 0xc || c == 0xd || c == 0xb ||
           c == '\n';
}

// 0x3349ec: one value of a command (a number, else a text up to '/' or
// '>').
Value parseValue(const std::string& s, std::size_t& at) {
    const std::size_t from = at;
    while (at < s.size() && s[at] != '/' && s[at] != '>') {
        ++at;
    }
    std::string t = s.substr(from, at - from);
    while (!t.empty() && space(static_cast<unsigned char>(t.back()))) {
        t.pop_back();
    }
    if (t.empty()) {
        return Value{};
    }
    char* end = nullptr;
    const long n = std::strtol(t.c_str(), &end, 10);
    if (end && *end == 0) {
        return Value::ofInt(std::int32_t(n));
    }
    const double f = std::strtod(t.c_str(), &end);
    if (end && *end == 0) {
        return Value::ofFloat(float(f));
    }
    return Value::ofText(t);
}

} // namespace

void MessageFlow::clear() {
    items_.clear();
    glyphs_.clear();
    index_ = 0;
    shown_ = 0;
    line_ = 0;
    colour_ = -1;
    printing_ = atMark_ = tapped_ = waitAtEnd_ = stretch_ = skipping_ = instant_ = false;
    wait_ = 0;
}

void MessageFlow::set(const std::string& text, bool instant, bool waitAtEnd, MessageHost* host) {
    // 0x172c20: a reset (+0x84), the flags, the items (0x174934).
    host_ = host;
    clear();
    delay_ = 0;
    clock_ = 0;
    voiceWait_ = false;
    if (instant) {
        progress_ = 1;
        instant_ = true;
    }
    waitAtEnd_ = waitAtEnd;
    std::size_t at = 0;
    while (at < text.size()) {
        if (text[at] == '<') {
            // '<', blanks, '/' for a closing command, the name's letters
            // (16 at most), blanks, values separated by '/', '>'.
            std::size_t p = at + 1;
            while (p < text.size() && space(static_cast<unsigned char>(text[p]))) {
                ++p;
            }
            const bool close = p < text.size() && text[p] == '/';
            if (close) {
                ++p;
            }
            std::string name;
            while (p < text.size() && std::isalpha(static_cast<unsigned char>(text[p])) &&
                   name.size() < 16) {
                name += text[p++];
            }
            std::vector<Value> values;
            while (p < text.size() && space(static_cast<unsigned char>(text[p]))) {
                ++p;
            }
            while (p < text.size() && text[p] != '>') {
                values.push_back(parseValue(text, p));
                while (p < text.size() && space(static_cast<unsigned char>(text[p]))) {
                    ++p;
                }
                if (p < text.size() && text[p] == '/') {
                    ++p;
                }
            }
            at = p < text.size() ? p + 1 : p;
            if (!name.empty()) {
                command(name, close, std::move(values));
            }
            continue;
        }
        // A run up to the next '<' that is not inside a "{..}" escape.
        std::size_t end = at;
        while (end < text.size()) {
            if (text[end] == '<') {
                const bool inEscape = (end >= 1 && text[end - 1] == '{' && end + 2 < text.size() &&
                                       text[end + 2] == '}') ||
                                      (end >= 2 && text[end - 2] == '{' && end + 1 < text.size() &&
                                       text[end + 1] == '}');
                if (!inEscape) {
                    break;
                }
            }
            ++end;
        }
        plain(text.substr(at, end - at));
        at = end;
    }
    if (waitAtEnd_) {
        items_.push_back({kMark, 0, {}, false});
    }
}

void MessageFlow::plain(const std::string& run) {
    // 0x172190 with no name: characters (escapes "{xy}" are one), line
    // breaks ("\n" and the two characters '\' 'n'), readings "[base/
    // reading]" (base characters kind 1, the reading kind 2, an end mark).
    int ruby = 0; // 0 none, 1 base, 2 reading
    std::string chars;
    auto flush = [&]() {
        if (chars.empty()) {
            return;
        }
        for (const std::uint32_t c : decodeText(chars)) {
            Glyph g;
            g.code = c;
            g.colour = colour_;
            g.line = line_;
            g.ruby = ruby == 2;
            glyphs_.push_back(g);
            items_.push_back({ruby == 0   ? kGlyph
                              : ruby == 1 ? kRubyBase
                                          : kRubyReading,
                              std::uint32_t(glyphs_.size() - 1),
                              {},
                              false});
        }
        chars.clear();
    };
    for (std::size_t i = 0; i < run.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(run[i]);
        if (c == 0xd || c == 7 || c == 8 || c == 0xc) {
            continue;
        }
        if (c == '\n' || (c == '\\' && i + 1 < run.size() && run[i + 1] == 'n')) {
            flush();
            items_.push_back({kNewLine, 0, {}, false});
            ++line_;
            if (c == '\\') {
                ++i;
            }
            continue;
        }
        if (c == '[' && ruby == 0 && run.find(']', i) != std::string::npos &&
            run.find('/', i) < run.find(']', i)) {
            flush();
            ruby = 1;
            continue;
        }
        if (c == '/' && ruby == 1) {
            flush();
            ruby = 2;
            continue;
        }
        if (c == ']' && ruby != 0) {
            flush();
            items_.push_back({kRubyEnd, 0, {}, false});
            ruby = 0;
            continue;
        }
        // A Shift-JIS lead byte takes its second byte along.
        chars += char(c);
        if ((c >= 0x81 && c <= 0x9f) || (c >= 0xe0 && c <= 0xfc)) {
            if (i + 1 < run.size()) {
                chars += run[++i];
            }
        }
    }
    flush();
}

void MessageFlow::command(const std::string& name, bool close, std::vector<Value> values) {
    Item it;
    it.close = close;
    it.values = std::move(values);
    // 0x172190: the plain commands.
    if (name == "W") {
        it.type = kWait;
        if (it.values.empty()) {
            it.values.push_back(Value::ofInt(30));
        }
    } else if (name == "C" || (name.size() == 2 && name[0] == 'C')) {
        if (close) {
            it.type = kColourEnd;
            colour_ = -1;
        } else {
            // <C>/<Cn>: a palette index; <CR> 3, <CG> 5, <CB> 1, <CF> 0,
            // <CN> 6, <CO> 8, <CS> 7, <CT> 9, <CW> 2, <CY> 4.
            int index = 1;
            if (name.size() == 2) {
                switch (name[1]) {
                case 'R':
                    index = 3;
                    break;
                case 'G':
                    index = 5;
                    break;
                case 'F':
                    index = 0;
                    break;
                case 'N':
                    index = 6;
                    break;
                case 'O':
                    index = 8;
                    break;
                case 'S':
                    index = 7;
                    break;
                case 'T':
                    index = 9;
                    break;
                case 'W':
                    index = 2;
                    break;
                case 'Y':
                    index = 4;
                    break;
                default:
                    index = 1;
                    break;
                }
            } else if (!it.values.empty() && it.values[0].isNumber()) {
                index = it.values[0].toInt();
            }
            it.type = kColour;
            colour_ = index;
        }
    } else if (name == "R") {
        it.type = close ? kSpeedEnd : kSpeed;
    } else if (name == "T") {
        it.type = close ? kStretchEnd : kStretch;
    } else if (name == "X") {
        it.type = kOffsetX;
    } else if (name == "Y") {
        it.type = kOffsetY;
    }
    // 0x16db60: the talk commands.
    else if (name == "A") {
        it.type = kAnim;
    } else if (name == "B") {
        it.type = kPage;
    } else if (name == "D") {
        it.type = kCount;
    } else if (name == "F") {
        it.type = kFace;
    } else if (name == "P") {
        it.type = kPose;
    } else if (name == "I") {
        it.type = kPageI;
    } else if (name == "J") {
        it.type = kJingle;
    } else if (name == "K") {
        it.type = kKeep;
    } else if (name == "LOCK" || name == "UNLOCK") {
        it.type = kLock;
        it.values = {Value::ofInt(name == "LOCK" ? 1 : 0)};
    } else if (name == "L") {
        it.type = kDelay;
    } else if (name == "ME") {
        it.type = kMotionOf;
    } else if (name == "M") {
        it.type = kMotion;
    } else if (name == "N") {
        // A character's name as text (event 0x19).
        if (!close && !it.values.empty()) {
            const Value n = event(0x19, {it.values[0]});
            if (n.isText()) {
                plain(n.text);
            }
        }
        return;
    } else if (name == "O") {
        it.type = kTyping;
    } else if (name == "Q") {
        it.type = kQuiet;
    } else if (name == "SEQ") {
        it.type = kScript;
    } else if (name == "SEL") {
        it.type = kSelect;
    } else if (name == "S") {
        it.type = kSound;
    } else if (name == "U") {
        it.type = kUsers;
    } else if (name == "V") {
        it.type = kVoice;
    } else {
        return; // not a command of this engine
    }
    items_.push_back(std::move(it));
}

Value MessageFlow::event(int code, std::vector<Value> args) const {
    return host_ ? host_->messageEvent(code, args) : Value{};
}

float MessageFlow::number(const Item& item, std::size_t i, float fallback) const {
    return i < item.values.size() && item.values[i].isNumber() ? item.values[i].toFloat()
                                                               : fallback;
}

bool MessageFlow::voiceBusy(int which) const {
    // 0x2b24a8: event 5 (the game's vtable +0xfc).
    return event(5, {Value::ofInt(which)}).truthy();
}

void MessageFlow::setSpeed(float speed) {
    speed_ = speed;
    const int every = speed > 0 ? int(1.0f / speed + 0.5f) : 4;
    typingEvery_ = std::max(4, every);
}

void MessageFlow::setTypingSound(int sound, int interval) {
    typingCount_ = 10000;
    typingSound_ = sound;
    typingEvery_ = interval;
}

bool MessageFlow::done() const {
    if (voiceBusy(1) || voiceWait_) {
        return false;
    }
    return index_ >= items_.size();
}

bool MessageFlow::talking() const {
    if (keep_) {
        return false;
    }
    if (voiceBusy(1)) {
        return event(3, {Value::ofInt(1)}).truthy();
    }
    return !done() && printing_;
}

bool MessageFlow::waitingMark() const {
    if (voiceBusy(1) || voiceWait_) {
        return false;
    }
    return atMark_;
}

bool MessageFlow::play(const Item& it) {
    // 0x16e678: the talk commands; false holds the timeline here.
    const Value who = Value::ofInt(speaker);
    switch (it.type) {
    case kPose:
        event(0x13, {who, Value::ofInt(int(number(it, 0)))});
        break;
    case kFace:
        event(0x14, {who, Value::ofInt(int(number(it, 0)))});
        break;
    case kAnim:
        event(0x15, {who, Value::ofInt(int(number(it, 0))), Value::ofInt(int(number(it, 1)))});
        break;
    case kMotion:
        // a, b, c [, blend frames (30)].
        event(0x16, {who, Value::ofInt(int(number(it, 0))), Value::ofInt(int(number(it, 1))),
                     Value::ofInt(int(number(it, 2))), Value::ofInt(0),
                     Value::ofFloat(number(it, 3, 30))});
        break;
    case kMotionOf:
        if (!it.values.empty()) {
            event(0x17, {it.values[0], Value::ofInt(int(number(it, 1))),
                         Value::ofInt(int(number(it, 2))), Value::ofInt(int(number(it, 3))),
                         Value::ofInt(0), Value::ofFloat(number(it, 4, 30))});
        }
        break;
    case kKeep:
        keep_ = !it.close;
        break;
    case kQuiet:
        quiet_ = !it.close;
        break;
    case kTyping: {
        int n = it.close ? -1 : int(number(it, 0));
        if (n < 0) {
            const Value v = event(0x10, {Value::ofInt(speaker)});
            if (!v.isNumber() || v.toInt() < 0) {
                return true;
            }
            n = v.toInt();
        }
        typingSound_ = n;
        break;
    }
    case kSound:
        event(1, {Value::ofInt(int(number(it, 0)))});
        break;
    case kScript:
        event(0xf, it.values);
        break;
    case kVoice:
        if (!it.close) {
            if (voiceBusy(1)) {
                return false;
            }
            // 0x16d9dc: the event's voice n (event 8), then (when it
            // started) event 0xc; the typing sound stops while it speaks.
            const Value started =
                event(8, {Value::ofInt(int(number(it, 0))), Value::ofInt(0), Value::ofInt(1)});
            if (started.truthy()) {
                event(0xc, {Value::ofInt(1), Value::ofFloat(number(it, 1, 1))});
            }
            quiet_ = true;
            keep_ = false;
        } else {
            if (event(4, {Value::ofInt(1)}).truthy()) {
                return false;
            }
            voiceWait_ = true;
        }
        break;
    case kJingle:
        if (it.close) {
            jingle_ = true;
            if (event(4, {Value::ofInt(2)}).truthy() || event(3, {Value::ofInt(2)}).truthy()) {
                return false;
            }
            jingle_ = false;
        } else {
            if (voiceBusy(2)) {
                return false;
            }
            event(7, {Value::ofInt(int(number(it, 0))), Value::ofInt(0), Value::ofInt(1)});
        }
        break;
    case kDelay:
        delay_ = 0;
        if (!skipping_ && !stretch_) {
            delay_ = number(it, 0) * 60.0f;
        }
        break;
    case kLock:
        event(0x18, it.values);
        break;
    case kPage:
        if (!tapped_) {
            atMark_ = true;
            return false;
        }
        atMark_ = false;
        break;
    case kPageI:
    case kCount:
    case kSelect:
    case kUsers:
        break; // *not reconstructed yet* (no story text found using them)
    default:
        break;
    }
    return true;
}

void MessageFlow::update(float step) {
    // 0x170154.
    clock_ += step;
    if (voiceWait_) {
        if (event(3, {Value::ofInt(1)}).truthy() && !tapped_) {
            return;
        }
        if (event(3, {Value::ofInt(1)}).truthy()) {
            event(10, {Value::ofInt(1), Value::ofInt(0)});
        }
        voiceWait_ = false;
        if (waitAtEnd_ && index_ >= items_.size()) {
            atMark_ = true;
        }
    }
    if (delay_ > 0) {
        if (!skipping_ && !instant_) {
            if (clock_ < delay_) {
                return;
            }
            delay_ = 0;
        } else {
            delay_ = 0;
        }
    }
    // 0x175330: the timeline.
    if (!items_.empty()) {
        if (tapped_ && stretch_) {
            tapped_ = false;
            skipping_ = true;
            stretch_ = false;
            progress_ = 1;
        }
        bool run = true;
        if (wait_ > 0) {
            if (!skipping_ && !instant_) {
                wait_ = std::max(0.0f, wait_ - step);
                run = false;
            } else {
                wait_ = 0;
                progress_ = 1;
            }
        }
        while (run && index_ < items_.size()) {
            const Item& it = items_[index_];
            const bool fast = skipping_ || instant_;
            switch (it.type) {
            case kGlyph:
            case kRubyBase:
            case kIcon:
                progress_ += step * speed_;
                printing_ = true;
                if (progress_ >= 1.0f || instant_) {
                    ++shown_;
                    ++index_;
                    if (fast) {
                        progress_ = 1;
                        continue;
                    }
                    progress_ = 0;
                    // A reading appears with its base character.
                    if (it.type == kRubyBase && index_ < items_.size() &&
                        items_[index_].type == kRubyReading) {
                        continue;
                    }
                }
                run = false;
                break;
            case kRubyReading:
                ++shown_;
                ++index_;
                if (fast || (index_ < items_.size() && items_[index_].type == kRubyReading)) {
                    continue;
                }
                run = false;
                break;
            case kRubyEnd:
                ++index_;
                if (!fast) {
                    run = false;
                }
                break;
            case kWait:
                ++index_;
                if (!fast) {
                    printing_ = false;
                    wait_ = number(it, 0);
                    run = false;
                }
                break;
            case kMark:
                atMark_ = true;
                printing_ = false;
                stretch_ = false;
                if (!tapped_) {
                    run = false;
                    break;
                }
                atMark_ = false;
                tapped_ = false;
                ++index_;
                break;
            case kStretch:
                if (!instant_) {
                    stretch_ = true;
                }
                ++index_;
                break;
            case kStretchEnd:
                ++index_;
                if (!instant_) {
                    skipping_ = false;
                    tapped_ = false;
                    progress_ = 0;
                    run = false;
                }
                break;
            case kSpeed:
                savedSpeed_ = speed_;
                setSpeed(number(it, 0, 1));
                ++index_;
                break;
            case kSpeedEnd:
                setSpeed(savedSpeed_);
                ++index_;
                break;
            default:
                if (!play(it)) {
                    run = false;
                    break;
                }
                ++index_;
                break;
            }
        }
    }
    // The typing sound every so many frames while characters appear.
    if (typingEvery_ != 0 && ++typingCount_ >= typingEvery_ && !done() && printing_ && !quiet_) {
        if (typingSound_ >= 0) {
            event(1, {Value::ofInt(typingSound_)});
        }
        typingCount_ = 0;
    }
}

} // namespace picarat::adventure56
