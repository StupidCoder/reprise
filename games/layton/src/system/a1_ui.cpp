// The first adventure's touch state and buttons. See a1_ui.hpp.
#include "system/a1_ui.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace picarat::adventure1 {

namespace {
constexpr int kNone = -16;           // 0xfff0: no position
constexpr float kJump = 64.0f;       // 0200eeb4: literal 0x42800000
constexpr unsigned kClickWindow = 5; // 020512d0: frames since the last press inside
} // namespace

bool TouchState::steady() {
    // 0200eeb4. A sample without a valid position (the stylus is up: the
    // touch panel reports -16, validity 3) forgets the remembered one.
    if (!down) {
        seenX_ = seenY_ = kNone;
        return false;
    }
    // 0200c15c distance, compared as binary32 with 64.0 (020b8374).
    const float dx = float(x - seenX_), dy = float(y - seenY_);
    const bool near = std::sqrt(dx * dx + dy * dy) < kJump;
    seenX_ = x;
    seenY_ = y;
    return near;
}

void TouchState::update(bool nowDown, int nowX, int nowY) {
    ++frame;
    auto sample = [&] {
        // 0200ed1c: the new calibrated sample; without contact the position
        // is -16.
        down = nowDown;
        x = nowDown ? nowX : kNone;
        y = nowDown ? nowY : kNone;
    };
    if (down) {
        // The stylus was down: the press starts with the first steady sample
        // (normally the second frame of contact; see steady()).
        pressed = false;
        if (pressX == kNone && pressY == kNone && steady()) {
            pressed = true;
            pressX = x;
            pressY = y;
        }
        previousX = x;
        previousY = y;
        sample();
        if (!down) {
            pressed = false;
            released = true;
        }
        return;
    }
    released = false;
    pressX = pressY = kNone;
    previousX = previousY = kNone;
    sample();
    if (steady()) {
        pressed = true;
        pressX = x;
        pressY = y;
    }
}

void Button::setSprite(SceneKit& kit, int px, int py, const std::string& name,
                       const std::string& animation, bool shadow) {
    // 02050f1c: the sprite instance takes the button's position; the
    // rectangle is its current frame's size (0200c680 / 0200c630).
    x = px;
    y = py;
    active_ = armed_ = false;
    hasSprite_ = true;
    sprite = kit.load(name, px, py, 1);
    if (!animation.empty()) {
        kit.select(sprite, animation);
    }
    sprite.x = px; // the first adventure never takes "pos" here
    sprite.y = py;
    idleAnimation_ = sprite.player.animation;
    width = kit.frameWidth(sprite);
    height = kit.frameHeight(sprite);
    hasShadow_ = shadow;
    if (shadow) {
        // The shadow copy sits 2 pixels lower right in the animation named
        // by 02051028 (not established yet; animation 1 is used).
        shadowSprite = kit.load(name, px + 2, py + 2, 1);
        shadowSprite.x = px + 2;
        shadowSprite.y = py + 2;
    }
}

void Button::setRect(int px, int py, int w, int h) {
    x = px;
    y = py;
    width = w;
    height = h;
    active_ = armed_ = false;
}

void Button::setPressedAnimation(SceneKit&, const std::string& animation) {
    // 02050ef8: the current animation stays the idle one; the named one is
    // shown while the button is held (0200c5d0 finds it by name).
    idleAnimation_ = sprite.player.animation;
    swap_ = false;
    if (sprite.sprite) {
        const auto& names = sprite.sprite->animationNames;
        if (const auto it = names.find(animation); it != names.end()) {
            swap_ = true;
            pressedAnimation_ = it->second;
        }
    }
}

void Button::draw(SceneKit& kit) {
    // 020510a8: a toggle button shows its sprite only while on. (The
    // blinking variant, +0x1d, is not used by the puzzles reconstructed so
    // far.)
    if (toggle && !on) {
        return;
    }
    kit.draw(sprite);
}

void Button::update(SceneKit& kit, TouchState& touch) {
    // 02051138.
    if (drawOnly) {
        draw(kit);
        return;
    }
    auto inside = [&](int px, int py) {
        return x <= px && px <= x + width && y <= py && py <= y + height;
    };
    if (touch.steady() && inside(touch.pressX, touch.pressY) && inside(touch.x, touch.y)) {
        if (hasSprite_) {
            sprite.x = x + pressDx;
            sprite.y = y + pressDy;
            if (swap_) {
                kit.select(sprite, pressedAnimation_);
            }
            draw(kit);
            sprite.x = x;
            sprite.y = y;
            if (swap_) {
                kit.select(sprite, idleAnimation_);
            }
        }
        active_ = armed_ = true;
        stamp_ = touch.frame;
    } else {
        // One frame of grace before the button stops being held.
        if (armed_) {
            armed_ = false;
        } else {
            active_ = false;
        }
        if (visibleIdle && hasSprite_) {
            draw(kit);
            if (hasShadow_) {
                kit.draw(shadowSprite);
            }
        }
        if (toggle && hasSprite_) {
            draw(kit);
        }
    }
    if (touch.pressed && active_ && !touch.steady()) {
        active_ = false;
    }
}

bool Button::clicked(SceneKit& kit, const TouchState& touch, bool withSound) {
    // 020512d0: a release within 5 frames of the last frame inside.
    if (unsigned(touch.frame - stamp_) >= kClickWindow || !touch.released) {
        return false;
    }
    active_ = false;
    if (toggle) {
        on = !on;
    }
    if (sound && withSound) {
        kit.effect(sound); // 020053ac
    }
    return true;
}

void SpriteButton::load(SceneKit& kit, const std::string& name, int effect) {
    // 02051390 with the position variable "pos" (0200c50c).
    sprite = kit.load(name, 0, 0, 1);
    x = sprite.x;
    y = sprite.y;
    sound = effect;
    inside_ = false;
    enter(kit, State::Idle);
}

void SpriteButton::load(SceneKit& kit, const std::string& name, int effect, int px, int py) {
    load(kit, name, effect);
    x = sprite.x = px;
    y = sprite.y = py;
}

void SpriteButton::enter(SceneKit& kit, State state) {
    // 020514a8: idle and clicked show "off" (a click restarts the count),
    // held shows "on".
    state_ = state;
    if (state == State::Clicked) {
        counter_ = 0;
    }
    kit.select(sprite, state == State::Held ? "on" : "off");
}

bool SpriteButton::hit(SceneKit& kit, const TouchState& touch) const {
    // 0200c904 -> 02007034: the current frame's rectangle, 2 pixels up and
    // left of the sprite's position, edges inclusive (0200eaa4). Sprites
    // with a per-cell touch mask (a 0x1235 block in the file) are also
    // tested cell by cell; the buttons using this class have none.
    const int left = x - 2, top = y - 2;
    return left <= touch.x && touch.x <= left + kit.frameWidth(sprite) && top <= touch.y &&
           touch.y <= top + kit.frameHeight(sprite);
}

void SpriteButton::update(SceneKit& kit, const TouchState& touch) {
    // 020515c8.
    if (touch.pressed && state_ != State::Clicked && hit(kit, touch)) {
        enter(kit, State::Held);
    }
    if (touch.down && state_ == State::Held) {
        inside_ = hit(kit, touch);
        kit.select(sprite, inside_ ? "on" : "off");
    } else if (!touch.down && state_ == State::Held) {
        if (!inside_) {
            enter(kit, State::Idle);
        } else {
            inside_ = false;
            enter(kit, State::Clicked);
            if (sound != -1) {
                kit.effect(sound); // 020053ac
            }
        }
    }
    if (state_ == State::Clicked) {
        if (counter_ < 6) {
            ++counter_;
        } else {
            enter(kit, State::Idle);
        }
    }
    draw(kit);
}

void SpriteButton::draw(SceneKit& kit) {
    kit.draw(sprite);
}

bool SpriteButton::clicked() {
    // 020514a8(0) also selects "off" again, which a clicked button shows
    // already.
    if (state_ == State::Clicked && counter_ > 5) {
        state_ = State::Idle;
        return true;
    }
    return false;
}

std::uint16_t textColour(int index) {
    // 0200d4ec: 0200c1ac(r, g, b) with 8-bit components; index 0xfe is
    // 0xffff, 0xff stays 0 (black).
    auto rgb = [](int r, int g, int b) {
        return std::uint16_t(0x8000 | (r >> 3) | (g >> 3) << 5 | (b >> 3) << 10);
    };
    switch (index) {
    case 0xfa:
        return rgb(0xa0, 0xfa, 0xf5);
    case 0xfb:
        return rgb(0, 0xff, 0);
    case 0xfc:
        return rgb(0xff, 0, 0);
    case 0xfe:
        return 0xffff;
    default:
        return 0x8000;
    }
}

void drawTextBox(SceneKit& kit, SceneKit::Screen screen, int x0, int y0, std::string_view text,
                 std::size_t skip, std::size_t count) {
    // 0200df2c with the box state of 0200d258 (colour 0xff, spacing 1).
    const Font& font = kit.assets.font("fontq.NFTR");
    Surface& layer = kit.textLayer(screen);
    count = std::min(count, text.size());
    int colour = 0xff, x = x0, y = y0;
    bool cleared = false;
    for (std::size_t i = 0; i < count; ++i) {
        const auto c = std::uint8_t(text[i]);
        if (c == '\n') {
            y += 12;
            x = x0;
        } else if (c == '#') {
            // 0200d41c: the code letter, upper-cased through 020ce3b8.
            if (++i < text.size()) {
                const int code = std::toupper(std::uint8_t(text[i]));
                colour = code == 'G'   ? 0xfb
                         : code == 'R' ? 0xfc
                         : code == 'W' ? 0xfe
                         : code == 'X' ? 0xff
                                       : colour;
            }
        } else if (c == '@') {
            ++i;
            if (i < text.size() && text[i] == 'c') {
                if (!cleared) {
                    // 0200d8f4: the lower layer from y0 down (hypothesis for
                    // the height: to the bottom of the layer).
                    Surface& lower = kit.textLayer(SceneKit::Lower);
                    for (int yy = std::max(0, y0); yy < int(lower.height); ++yy) {
                        std::fill_n(lower.pixels.begin() + std::ptrdiff_t(yy) * lower.width,
                                    lower.width, std::uint16_t(0));
                    }
                    cleared = true;
                }
                while (i + 1 < text.size() && (text[i + 1] == '\n' || text[i + 1] == '\r')) {
                    ++i;
                }
                x = x0;
                y = y0;
            }
        } else {
            if (i >= skip) {
                textGlyph(layer, font, c, x, y, textColour(colour));
                cleared = false;
            }
            x += textAdvance(font, c) + 1;
        }
    }
}

void drawEventText(SceneKit& kit, SceneKit::Screen screen, int x0, int y0, std::string_view text) {
    const Font& font = kit.assets.font("fontevent.NFTR");
    Surface& layer = kit.textLayer(screen);
    int colour = 0xff, x = x0, y = y0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = std::uint8_t(text[i]);
        if (c == '#') {
            if (++i < text.size()) {
                const int code = std::toupper(std::uint8_t(text[i]));
                colour = code == 'G'   ? 0xfb
                         : code == 'R' ? 0xfc
                         : code == 'W' ? 0xfe
                         : code == 'X' ? 0xff
                                       : colour;
            }
        } else if (c == '@') {
            if (++i < text.size() && (text[i] == 'l' || text[i] == 'L')) {
                y += 12 + 4;
                x = x0;
            }
        } else if (c == '\n') {
            y += 12 + 4;
            x = x0;
        } else if (c != '\r') {
            // The step is what the character canvas returns (02095678: the
            // NFTR glyph's own advance), not the width table, plus 1.
            textGlyph(layer, font, c, x, y, textColour(colour));
            const auto it = font.glyphs.find(c);
            x += (it == font.glyphs.end() ? 0 : it->second.advance) + 1;
        }
    }
}

std::string formatText(std::string_view format, std::initializer_list<TextArg> args) {
    std::string out;
    auto next = args.begin();
    for (std::size_t i = 0; i < format.size(); ++i) {
        if (format[i] != '%') {
            out += format[i];
            continue;
        }
        std::size_t j = i + 1;
        while (j < format.size() && std::strchr("-+ #0123456789.", format[j])) {
            ++j;
        }
        if (j >= format.size()) {
            break;
        }
        const char kind = format[j];
        const std::string spec(format.substr(i, j - i + 1));
        char buffer[0x100] = {};
        if (kind == '%') {
            out += '%';
        } else if ((kind == 'd' || kind == 'i') && next != args.end() && next->isNumber) {
            std::snprintf(buffer, sizeof buffer, (spec.substr(0, spec.size() - 1) + "d").c_str(),
                          next->number);
            out += buffer;
            ++next;
        } else if (kind == 's' && next != args.end() && !next->isNumber) {
            std::snprintf(buffer, sizeof buffer, spec.c_str(), next->text.c_str());
            out += buffer;
            ++next;
        }
        i = j;
    }
    return out;
}

bool typingWaits(std::string_view text, std::size_t skip, std::size_t count) {
    count = std::min(count, text.size());
    if (skip >= text.size()) {
        return false;
    }
    for (std::size_t i = 0; i < count; ++i) {
        const char c = text[i];
        if (c == '\n') {
            continue;
        }
        if (c == '#' || c == '@') {
            ++i;
            continue;
        }
        if (i >= skip && i == count - 1 && i % 5 == 0) {
            return true;
        }
    }
    return false;
}

void drawCentredText(SceneKit& kit, Surface& layer, int centreY, std::string_view text, bool fontq,
                     int extra, int centreX, int colour) {
    // The line feed is always the event font's (NFTR FINF byte +1: 12; 0200de38
    // reads it from the text object's first font even when the glyphs come
    // from fontq; checked against the ROM's empty save slots).
    const Font& font = kit.assets.font(fontq ? "fontq.NFTR" : "fontevent.NFTR");
    const int lineFeed = 12;
    // 0200e9b4: carriage returns are dropped before the lines are measured.
    std::string copy;
    for (const char c : text) {
        if (c != '\r') {
            copy += c;
        }
    }
    text = copy;
    std::vector<std::string_view> lines;
    for (std::size_t start = 0;;) {
        const auto end = text.find('\n', start);
        lines.push_back(text.substr(start, end == text.npos ? text.npos : end - start));
        if (end == text.npos) {
            break;
        }
        start = end + 1;
    }
    // (line feed + 5 + extra) / 2 * (lines - 1) in floats, truncated.
    const int step = lineFeed + 5 + extra;
    int y = centreY - int(float(step) / 2.0f * float(lines.size() - 1));
    for (const auto line : lines) {
        unsigned width = 0;
        for (const char c : line) {
            width += unsigned(textAdvance(font, std::uint8_t(c)) + 1);
        }
        // 0200dac8: a negative centre is a left edge (0200de00).
        int x = centreX < 0 ? -centreX : centreX - int(width >> 1);
        for (const char c : line) {
            textGlyph(layer, font, std::uint8_t(c), x, y, textColour(colour));
            x += textAdvance(font, std::uint8_t(c)) + 1;
        }
        y += step;
    }
}

void drawCentredLine(SceneKit& kit, SceneKit::Screen screen, int y, std::string_view text,
                     int centreX) {
    // 0200dc8c / 0200dd50: carriage returns dropped and the text split at
    // newlines (0200e9b4); each line centred (0200dac8), the lines 12 + 5
    // apart.
    const Font& font = kit.assets.font("fontevent.NFTR");
    Surface& layer = kit.textLayer(screen);
    std::string copy;
    for (const char c : text) {
        if (c != '\r') {
            copy += c;
        }
    }
    std::string_view rest = copy;
    for (;;) {
        const auto end = rest.find('\n');
        const std::string_view line = rest.substr(0, end);
        unsigned width = 0;
        for (const char c : line) {
            width += unsigned(textAdvance(font, std::uint8_t(c)) + 1);
        }
        int x = centreX - int(width >> 1);
        for (const char c : line) {
            textGlyph(layer, font, std::uint8_t(c), x, y, textColour(0xff));
            x += textAdvance(font, std::uint8_t(c)) + 1;
        }
        if (end == rest.npos) {
            return;
        }
        rest.remove_prefix(end + 1);
        y += 12 + 5;
    }
}

namespace {
// One fade: 16 / step + 1 frames (a counter from -16 in steps while <= 0),
// each one VBlank (the write right after it shows in that frame) and one
// count of the game's frame counter (021ebb30). `early`: stop once the
// target is reached (only 0200f7b4 does); the others always run every
// frame, the last ones at the target.
void fade(SceneKit& kit, Screens screens, int step, int target, bool early, int frames) {
    const bool lower = screens != Screens::Upper, upper = screens != Screens::Lower;
    auto move = [&](int value) {
        return target == 0 ? std::min(0, value + step) : std::max(-16, value - step);
    };
    for (int i = -16; i <= 0; i += step) {
        if (frames > 1) {
            kit.stall(frames - 1); // the first waits of a step; the last presents
        }
        if (lower) {
            kit.setLowerBrightness(move(kit.lowerBrightness()));
        }
        if (upper) {
            kit.setUpperBrightness(move(kit.upperBrightness()));
        }
        kit.refresh();
        // 0200f7b4 asks after the frame, so it always takes one.
        if (early && (!lower || kit.lowerBrightness() == target) &&
            (!upper || kit.upperBrightness() == target)) {
            return;
        }
    }
}
} // namespace

void fadeIn(SceneKit& kit, Screens screens, int step, int frames) {
    fade(kit, screens, step, 0, screens == Screens::Both, frames);
}

void fadeOut(SceneKit& kit, Screens screens, int step, int frames) {
    fade(kit, screens, step, -16, false, frames);
}

} // namespace picarat::adventure1
