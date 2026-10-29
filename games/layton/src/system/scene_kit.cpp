// The minigame and menu toolkit. See scene_kit.hpp.
#include "system/scene_kit.hpp"
#include "puzzles/judgment.hpp"
#include <algorithm>
#include <cctype>
#include <functional>
#include <stdexcept>

namespace picarat {
namespace {
// fontevent.NFTR line feed (FINF +1), read by the text layer at font +1.
constexpr int kLineFeed = 12;
constexpr int kSpacing = 1; // text layer +2 character spacing

// 0200dc44 returns the drawn glyph's NFTR advance; 02072ab0/020731b8 step by
// it, while the centring path (02072bc0) steps by the 02072608 width table.
int glyphAdvance(const Font& font, unsigned c) {
    auto it = font.glyphs.find(c);
    return it == font.glyphs.end() ? 0 : it->second.advance;
}

std::uint16_t fontColor(char code, std::uint16_t current) {
    // 020726cc: '#r' palette 0xc (red), '#x' palette 0xf (black); other
    // codes keep the current colour here (their palettes are not needed yet).
    switch (code) {
    case 'r':
    case 'R':
        return 0x801f;
    case 'x':
    case 'X':
        return 0x8000;
    default:
        return current;
    }
}
} // namespace

SceneKit::SceneKit(GraphicsAssets& a, PresentFrame present, AudioService* audio, SceneKind scene)
    : assets(a), text(a.root), present_(std::move(present)), audio_(audio), scene_(scene),
      lowerText_{256, 192, std::vector<std::uint16_t>(256 * 192, 0)},
      upperText_{256, 192, std::vector<std::uint16_t>(256 * 192, 0)} {}

// ---------------------------------------------------------------------------
// Frame cycle.

void SceneKit::poll() {
    // 0206c7b4: key and touch state machines, then 0206d950 starts a new
    // 3D/OAM frame (submissions are rebuilt every iteration).
    if (researchPoll) {
        input_ = researchPoll(polls, input_);
    }
    ++polls;
    touch = filter_.update(input_.down, input_.x, input_.y);
    draws_.clear();
    depthCounter_ = 0;
}

void SceneKit::present() {
    // 0206cfb8: 0206ce04 swaps the 3D and OAM buffers, then one 0206ce18 frame.
    frame(true);
    // The first adventure's present (02003ef8) runs the background manager
    // after the VBlank (02003ed0 -> 020097dc): automatic scrolling steps on
    // every second call and shows from the next frame.
    if (++presents_ % 2 == 1) {
        if (lowerAutoScroll.on) {
            lowerScrollX += lowerAutoScroll.dx;
            lowerScrollY += lowerAutoScroll.dy;
        }
        if (lowerOverlayAutoScroll.on) {
            lowerOverlayScrollX += lowerOverlayAutoScroll.dx;
        }
    }
}

void SceneKit::refresh() {
    frame(true);
}

void SceneKit::frame(bool swap) {
    // 0206ce18 iteration: sound (0206cb00), VBlank. Measured (ov16 intro,
    // tea): the sprites and BG scroll of a swap appear one frame later than
    // register writes (brightness, windows); waits do not swap, so the last
    // swapped sprites stay on screen.
    if (sceneSound) {
        sceneSound->frame();
    } else if (auto s = sound()) {
        s->frame(); // ov2 0206cb10 -> 0207faf4 once per main-loop frame
    }
    GameSnapshot snapshot{scene_, {}, tick};
    snapshot.detail = detail;
    snapshot.skippable = skippable;
    snapshot.touchAreas = std::move(touchAreas);
    touchAreas.clear();

    // The shake offset drawn this frame reaches the BG registers with the
    // next one (measured: the ROM sequence lags one frame, ref-myst-reveal).
    shake_ = pendingShake_;
    pendingShake_ = 0;
    if (lowerShake > 0) {
        auto& state = random ? *random : ownRandom_;
        gameRandom(state);
        const int r = int(gameRandom(state));
        --lowerShake;
        pendingShake_ = r % 3 - 1;
    }
    if (swap && !spriteLatency) {
        shown_ = latch();
        shownLowerScroll_ = lowerScrollY;
        shownUpperScroll_ = upperScrollY;
    }
    // The host's skip (system/game_types.hpp) fast-forwards a skippable
    // stretch: its frames run without being shown, so its timing, sound
    // requests and state stay the game's own. One frame in 64 still goes to
    // the host, which keeps its input current and can stop the game.
    if (skippable && input_.skip && frames % 64 != 0) {
        if (swap) {
            shown_ = latch();
            shown3D_ = layer3D;
            shownLowerScroll_ = lowerScrollY;
            shownUpperScroll_ = upperScrollY;
        }
        ++frames;
        ++tick;
        return;
    }
    const auto image = compose();
    if (swap) {
        shown_ = latch();
        shown3D_ = layer3D;
        shownLowerScroll_ = lowerScrollY;
        shownUpperScroll_ = upperScrollY;
    }
    ++frames;
    input_ = present_(image, snapshot);
    if (input_.restart) {
        throw RestartGame{};
    }
    if (input_.puzzle) {
        throw JumpToPuzzle{};
    }
    ++tick;
}

void SceneKit::hold(int count) {
    for (int i = 0; i < count; ++i) {
        if (sceneSound) {
            SceneSound* sound = sceneSound;
            sceneSound = nullptr; // no sound frame while loading
            frame(false);
            sceneSound = sound;
            sound->idle();
        } else {
            frame(false);
        }
        --tick;
    }
}

void SceneKit::stall(int count) {
    for (int i = 0; i < count; ++i) {
        frame(false);
        --tick;
    }
}

void SceneKit::wait(int count) {
    // 0206ce18: no buffer swap, so the last presented sprites stay on screen;
    // from the second iteration 0206d950 starts new (empty) submissions.
    if (count < 1) {
        count = 1;
    }
    for (int i = 0; i < count; ++i) {
        if (i > 0) {
            draws_.clear();
        }
        frame(false);
    }
}

void SceneKit::fadeIn() {
    // 02071790: 17-iteration bound, +1 per frame on both screens, early exit
    // once both are 0.
    for (int i = -16; i <= 0; ++i) {
        wait(1);
        brightness_ = std::min(0, brightness_ + 1);
        lowerBrightness_ = std::min(0, lowerBrightness_ + 1);
        if (brightness_ == 0 && lowerBrightness_ == 0) {
            break;
        }
    }
}

void SceneKit::fadeOut() {
    // 02071e84: always 17 waits; levels clamp at -16.
    for (int i = 0; i >= -16; --i) {
        wait(1);
        brightness_ = std::max(-16, brightness_ - 1);
        lowerBrightness_ = std::max(-16, lowerBrightness_ - 1);
    }
}

void SceneKit::fadeOutLower() {
    for (int i = 0; i <= 16; ++i) {
        wait(1);
        lowerBrightness_ = std::clamp(lowerBrightness_ - 1, -16, 0);
    }
    lowerBrightness_ = -16;
}

void SceneKit::fadeInLower(int step) {
    for (int i = -16; i < 1; i += step) {
        wait(1);
        lowerBrightness_ = std::clamp(lowerBrightness_ + step, -16, 0);
    }
    lowerBrightness_ = 0;
}

void SceneKit::waitRelease() {
    // 0206cec0: keys and the touch state machine are sampled (without the
    // scene's 0206c7b4); a release ends it, otherwise one more frame.
    for (;;) {
        touch = filter_.update(input_.down, input_.x, input_.y);
        if (filter_.state == 3) {
            return;
        }
        draws_.clear();
        frame(false);
    }
}

// ---------------------------------------------------------------------------
// Text layers.

const Font& SceneKit::font() {
    return assets.font("fontevent.NFTR");
}

void SceneKit::clearText(Screen s) {
    auto& layer = s == Upper ? upperText_ : lowerText_;
    std::fill(layer.pixels.begin(), layer.pixels.end(), 0);
}

const Font& SceneKit::fontq() {
    return assets.font("fontq.NFTR");
} // 0207289c: layer font +0x14

void SceneKit::clearTextRect(int x, int y, int w, int h, Screen s) {
    auto& layer = s == Upper ? upperText_ : lowerText_;
    for (int yy = std::max(0, y); yy < std::min(192, y + h); ++yy) {
        for (int xx = std::max(0, x); xx < std::min(256, x + w); ++xx) {
            layer.pixels[std::size_t(yy) * 256 + std::size_t(xx)] = 0;
        }
    }
}

int SceneKit::lineWidth(const std::string& line, bool q) {
    // 02072bc0: spacing plus advance per character (02072608, or 02072634
    // for fontq); '#' and its code are skipped.
    const Font& f = q ? fontq() : font();
    int width = 0;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '#') {
            ++i;
            continue;
        }
        width += kSpacing + textAdvance(f, std::uint8_t(line[i]));
    }
    return width;
}

void SceneKit::glyphs(Surface& layer, const std::string& t, int x, int y, bool q) {
    const Font& f = q ? fontq() : font();
    std::uint16_t color = 0x8000;
    for (std::size_t i = 0; i < t.size(); ++i) {
        const char c = t[i];
        if (c == '#' && i + 1 < t.size()) {
            color = fontColor(t[++i], color);
            continue;
        }
        if (c == '\n') {
            return;
        }
        textGlyph(layer, f, std::uint8_t(c), x, y, color);
        x += textAdvance(f, std::uint8_t(c)) + kSpacing;
    }
}

void SceneKit::textQuestion(int x, int y, const std::string& t, Screen s, int skip) {
    // 02072ff8: '#' colour codes, newlines (feed + 4), fontq glyphs; the
    // advance is the 02072634 table width plus spacing, also when skipped.
    auto& layer = s == Upper ? upperText_ : lowerText_;
    std::uint16_t color = 0x8000;
    int cx = x;
    for (int i = 0; i < int(t.size()); ++i) {
        const char c = t[std::size_t(i)];
        if (c == '\n') {
            y += questionLineStep;
            cx = x;
            continue;
        }
        if (c == '#' && i + 1 < int(t.size())) {
            color = fontColor(t[std::size_t(++i)], color);
            continue;
        }
        if (c == '@') {
            ++i;
            continue;
        }
        if (i >= skip) {
            textGlyph(layer, fontq(), std::uint8_t(c), cx, y, color);
        }
        cx += textAdvance(fontq(), std::uint8_t(c)) + kSpacing;
    }
}

namespace {
std::vector<std::string> splitLines(const std::string& t) {
    // 020736c4: carriage returns are dropped, newlines split lines.
    std::vector<std::string> lines(1);
    for (char c : t) {
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            lines.emplace_back();
        } else {
            lines.back() += c;
        }
    }
    return lines;
}
} // namespace

void SceneKit::textCentered(int centerY, const std::string& t, int extra, int centerX, Screen s) {
    // 02072f18: line step extra+feed+5; the block is centred on centerY with
    // a binary32 half step times (lines-1), truncated (0203aa94/0203afd4/0203aed0).
    if (t.empty()) {
        return;
    }
    const auto lines = splitLines(t);
    const int step = extra + kLineFeed + 5;
    int y = centerY - int(float(step) / 2.0f * float(int(lines.size()) - 1));
    auto& layer = s == Upper ? upperText_ : lowerText_;
    for (const auto& line : lines) {
        glyphs(layer, line, centerX - (lineWidth(line) >> 1), y);
        y += step;
    }
}

void SceneKit::textTitle(int y, const std::string& t, int extra, int centerX, Screen s,
                         bool fontq) {
    // 02072d80 / 02072e34: centred lines from y downwards (line step from
    // the event font's feed either way).
    if (t.empty()) {
        return;
    }
    auto& layer = s == Upper ? upperText_ : lowerText_;
    for (const auto& line : splitLines(t)) {
        glyphs(layer, line, centerX - (lineWidth(line, fontq) >> 1), y, fontq);
        y += extra + kLineFeed + 5;
    }
}

void SceneKit::textAt(int x, int y, const std::string& t, Screen s) {
    // 02072ab0: '#' colour codes, "@l"/"\n" new lines (feed + 4).
    auto& layer = s == Upper ? upperText_ : lowerText_;
    std::uint16_t color = 0x8000;
    int cx = x;
    for (std::size_t i = 0; i < t.size(); ++i) {
        const char c = t[i];
        if (c == '#' && i + 1 < t.size()) {
            color = fontColor(t[++i], color);
            continue;
        }
        if (c == '@') {
            if (i + 1 < t.size() && (t[i + 1] == 'l' || t[i + 1] == 'L')) {
                y += kLineFeed + 4;
                cx = x;
            }
            ++i;
            continue;
        }
        if (c == '\n') {
            y += kLineFeed + 4;
            cx = x;
            continue;
        }
        textGlyph(layer, font(), std::uint8_t(c), cx, y, color);
        cx += glyphAdvance(font(), std::uint8_t(c)) + kSpacing;
    }
}

void SceneKit::textTyped(int x, int y, const std::string& t, int count, int skip, int extra) {
    // 020731b8 on the upper layer: the first `count` characters (all for -1);
    // characters before `skip` advance 12 px undrawn; a newline steps
    // feed + 4 + extra (ov18/ov21 pass -1: 15 px, measured ref-notes/-diary).
    std::uint16_t color = 0x8000;
    int cx = x;
    const int n = (count < 0 || std::size_t(count) > t.size()) ? int(t.size()) : count;
    for (int i = 0; i < n; ++i) {
        const char c = t[std::size_t(i)];
        if (c == '#' && i + 1 < int(t.size())) {
            color = fontColor(t[std::size_t(++i)], color);
            continue;
        }
        if (c == '@') {
            ++i;
            continue;
        }
        if (c == '\n') {
            y += kLineFeed + 4 + extra;
            cx = x;
            continue;
        }
        if (i < skip) {
            cx += 12 + kSpacing;
            continue;
        }
        textGlyph(upperText_, font(), std::uint8_t(c), cx, y, color);
        cx += glyphAdvance(font(), std::uint8_t(c)) + kSpacing;
    }
}

std::string SceneKit::systemText(int id) {
    // 0206b2c0: /data_lt2/txt/%s/txt.plz member tx_%i.txt, expanded.
    const auto b = readFile(assets.root / "text" / ("tx_" + std::to_string(id) + ".txt"));
    return text.expand({reinterpret_cast<const char*>(b.data()), b.size()});
}

std::string SceneKit::member(const std::string& directory, const std::string& name) {
    const auto path = assets.root / "text" / directory / name;
    if (!fileExists(path)) {
        return {};
    }
    const auto b = readFile(path);
    std::string s(reinterpret_cast<const char*>(b.data()), b.size());
    if (auto z = s.find('\0'); z != s.npos) {
        s.resize(z);
    }
    return s;
}

// ---------------------------------------------------------------------------
// Sprites.

std::string SceneKit::localized(std::string path) const {
    if (auto p = path.find('?'); p != path.npos) {
        path.replace(p, 1, language);
    }
    if (auto p = path.find("%s"); p != path.npos) {
        path.replace(p, 2, language);
    }
    return path;
}

SceneKit::SpriteObject SceneKit::load(const std::string& raw, int x, int y, unsigned animation) {
    SpriteObject s;
    auto path = localized(raw);
    const bool upper = path.size() > 4 && path.back() == 'j';
    if (path.ends_with(".spr")) {
        path.replace(path.size() - 4, 4, ".arc");
    } else if (path.ends_with(".sbj") || path.ends_with(".spj")) {
        path.replace(path.size() - 4, 4, ".arj");
    }
    s.sprite = &assets.sprite(path);
    s.upper = upper;
    s.x = x;
    s.y = y;
    // 020758fc: a zero position takes the sprite's "pos" variable (02075a08).
    if (!upper && x == 0 && y == 0) {
        variable(s, "pos", s.x, s.y);
    }
    select(s, animation);
    return s;
}

void SceneKit::select(SpriteObject& s, unsigned index) {
    if (s.sprite) {
        s.player.select(index, tick, s.sprite->animations);
    }
}

void SceneKit::select(SpriteObject& s, const std::string& name) {
    if (!s.sprite) {
        return;
    }
    auto it = s.sprite->animationNames.find(name);
    if (it == s.sprite->animationNames.end()) {
        // The first adventure's lookup (0200c794) ignores case: its scale
        // asks for "Plate" in a sprite that names it "plate".
        auto lower = [](std::string t) {
            for (auto& c : t) {
                c = char(std::tolower(static_cast<unsigned char>(c)));
            }
            return t;
        };
        const std::string wanted = lower(name);
        it = std::find_if(s.sprite->animationNames.begin(), s.sprite->animationNames.end(),
                          [&](const auto& entry) { return lower(entry.first) == wanted; });
        if (it == s.sprite->animationNames.end()) {
            return;
        }
    }
    select(s, it->second);
}

void SceneKit::restart(SpriteObject& s) {
    if (!s.sprite || s.player.animation >= s.sprite->animations.size() ||
        s.sprite->animations[s.player.animation].empty()) {
        return;
    }
    s.player.step = 0;
    s.player.lastTick = tick;
    s.player.duration = s.sprite->animations[s.player.animation][0].duration;
}

bool SceneKit::variable(const SpriteObject& s, const std::string& name, int& x, int& y) const {
    if (!s.sprite) {
        return false;
    }
    auto it = s.sprite->variables.find(name);
    if (it == s.sprite->variables.end()) {
        return false;
    }
    x = it->second[0];
    y = it->second[1];
    return true;
}

void SceneKit::drawNumber(SpriteObject& s, unsigned value, const std::string& name, int dx, int dy,
                          bool pad) {
    if (!s.sprite) {
        return;
    }
    auto it = s.sprite->variables.find(name);
    if (it == s.sprite->variables.end()) {
        return;
    }
    const auto& v = it->second; // 02075a08 position, 02075aa0 step, 02075b38 maximum
    const long maximum = v.size() > 5 && v[5] ? v[5] : 99;
    if (maximum <= 1) {
        return;
    }
    int x = v[0];
    const int y = v[1], step = v.size() > 2 ? v[2] : 0;
    for (long place = 1, next = 10; place < maximum; place *= 10, next *= 10) {
        const unsigned digit = unsigned((long(value) % next) / place);
        if (place == 1 || digit > 0 || pad || long(value) >= next) {
            select(s, digit + 1);
            s.x = x + dx;
            s.y = y + dy;
            draw(s);
        }
        x -= step;
    }
}

int SceneKit::frameWidth(const SpriteObject& s) const {
    if (!s.sprite || !s.player.animation || s.sprite->animations.at(s.player.animation).empty()) {
        return 0;
    }
    return int(
        s.sprite->images.at(s.sprite->animations[s.player.animation][s.player.step].image).width);
}

int SceneKit::frameHeight(const SpriteObject& s) const {
    if (!s.sprite || !s.player.animation || s.sprite->animations.at(s.player.animation).empty()) {
        return 0;
    }
    return int(
        s.sprite->images.at(s.sprite->animations[s.player.animation][s.player.step].image).height);
}

void SceneKit::drawRotated(SpriteObject& s, unsigned turns) {
    // 02076214 -> 020744f0 -> 0206ecfc per texture chunk: the frame rotated
    // about (x, y); the animation then steps like 02075e50.
    if (!s.sprite || !s.player.animation) {
        return;
    }
    const auto& steps = s.sprite->animations.at(s.player.animation);
    if (steps.empty()) {
        return;
    }
    const auto image = steps[s.player.step].image;
    Draw d{&s.sprite->images.at(image),
           s.x,
           s.y,
           31,
           s.upper, // a 2D sprite turns on the upper screen (affine OBJ)
           int(turns & 65535),
           s.sprite->chunks.empty() ? nullptr : &s.sprite->chunks.at(image)};
    d.quads = image < s.sprite->quads.size() ? &s.sprite->quads[image] : nullptr;
    submit(d);
    s.player.advance(tick, s.sprite->animations);
}

void SceneKit::drawScaled(SpriteObject& s, float scale) {
    // 02076124 -> 02074458 -> 0206eab0, one quad per texture chunk; the
    // animation then steps like 02075e50 (see compose for the geometry).
    if (!s.sprite || !s.player.animation) {
        return;
    }
    const auto& steps = s.sprite->animations.at(s.player.animation);
    if (steps.empty()) {
        return;
    }
    const auto image = steps[s.player.step].image;
    Draw d{&s.sprite->images.at(image), s.x, s.y, 31, false};
    d.scale = scale;
    d.quads = image < s.sprite->quads.size() ? &s.sprite->quads[image] : nullptr;
    submit(d);
    s.player.advance(tick, s.sprite->animations);
}

void SceneKit::submit(Draw d, Depth depth) {
    if (depthOrder && !d.upper) {
        d.z = depth == Depth::Fixed     ? 2
              : depth == Depth::Current ? 2 + depthCounter_
                                        : 2 + depthCounter_++;
    }
    draws_.push_back(d);
}

std::vector<SceneKit::Draw> SceneKit::latch() const {
    if (!depthOrder) {
        return draws_;
    }
    // Lower-screen 3D polygons by depth: farther first, and among equal
    // depths the one submitted first last (so that it shows).
    std::vector<std::size_t> lower;
    for (std::size_t i = 0; i < draws_.size(); ++i) {
        if (!draws_[i].upper) {
            lower.push_back(i);
        }
    }
    std::vector<std::size_t> order = lower;
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return draws_[a].z != draws_[b].z ? draws_[a].z < draws_[b].z : a > b;
    });
    std::vector<Draw> out = draws_;
    for (std::size_t k = 0; k < lower.size(); ++k) {
        out[lower[k]] = draws_[order[k]];
    }
    return out;
}

void SceneKit::rectangle(int x, int y, int w, int h, std::uint16_t color, int alpha, Depth depth) {
    // A zero-height quad still rasterises as a one-pixel line (measured:
    // the puzzle index highlight clipped to h = 0, ref-index frame 534).
    if (alpha <= 0) {
        return;
    }
    Draw d{nullptr, x, y, std::min(alpha, 31), false};
    d.w = w;
    d.h = h == 0 ? 1 : h;
    d.fill = color;
    submit(d, depth);
}

void SceneKit::triangle(int cx, int cy, const std::array<std::array<int, 2>, 3>& corners,
                        std::uint16_t color, Depth depth) {
    Draw d{nullptr, cx, cy, 31, false};
    d.triangle = true;
    d.corners = corners;
    d.fill = color;
    submit(d, depth);
}

void SceneKit::draw(SpriteObject& s, int alpha) {
    drawSprite(s, alpha, false);
}

void SceneKit::drawMirrored(SpriteObject& s, int alpha) {
    drawSprite(s, alpha, true);
}

void SceneKit::drawSprite(SpriteObject& s, int alpha, bool mirrored) {
    // 02075e50 (3D polygon, lower screen) / 02076534 (OAM, upper screen; not
    // submitted at y >= 0xc0). Both submit the current frame, then step.
    if (!s.sprite || !s.player.animation) {
        return;
    }
    const auto& steps = s.sprite->animations.at(s.player.animation);
    if (steps.empty()) {
        return;
    }
    if (!(s.upper && s.y >= 0xc0)) {
        Draw d{&s.sprite->images.at(steps[s.player.step].image), s.x, s.y, alpha, s.upper};
        d.mirrored = mirrored;
        submit(d);
    }
    s.player.advance(tick, s.sprite->animations);
}

// ---------------------------------------------------------------------------
// Buttons.

SceneKit::SpriteButton SceneKit::button(const std::string& path, int x, int y, int sound,
                                        const std::string& positionVariable) {
    // 0208003c: the sprite's position variable replaces (x, y) when present;
    // bounds come from frame 1 (02080204 via 02075cc0/02075c70).
    SpriteButton b;
    b.exists = true;
    b.x = x;
    b.y = y;
    b.sprite = load(path, x, y, 1);
    variable(b.sprite, positionVariable, b.x, b.y);
    b.sprite.x = b.x;
    b.sprite.y = b.y;
    setButtonAnimations(b, "on", "off", "click");
    const int w = frameWidth(b.sprite), h = frameHeight(b.sprite);
    b.button = Button({b.x, b.y, w, h}, sound);
    return b;
}

void SceneKit::setButtonAnimations(SpriteButton& b, const std::string& pressed,
                                   const std::string& idle, const std::string& click) {
    auto index = [&](const std::string& n) -> unsigned {
        auto it = b.sprite.sprite->animationNames.find(n);
        return it == b.sprite.sprite->animationNames.end() ? 0u : it->second;
    };
    b.pressedAnim = index(pressed);
    b.idleAnim = index(idle);
    b.clickAnim = index(click);
    select(b.sprite, b.idleAnim);
}

void SceneKit::setButtonIdle(SpriteButton& b, const std::string& idle) {
    auto it = b.sprite.sprite->animationNames.find(idle);
    b.idleAnim = it == b.sprite.sprite->animationNames.end() ? 0u : it->second;
    select(b.sprite, b.idleAnim);
}

void SceneKit::updateButton(SpriteButton& b) {
    // 02080300 ordinary button: idle / pressed-inside / click animations.
    if (!b.exists) {
        return;
    }
    b.button.update(touch);
    switch (b.button.state) {
    case Button::State::Armed:
        select(b.sprite, b.button.inside || touch.pressed ? b.pressedAnim : b.idleAnim);
        break;
    case Button::State::Clicked:
        select(b.sprite, b.clickAnim);
        break;
    case Button::State::Idle:
        select(b.sprite, b.idleAnim);
        break;
    }
    // 020803e0 / 02080450: pressed sprites move by the press offset.
    const bool down = b.button.state == Button::State::Armed && (b.button.inside || touch.pressed);
    b.sprite.x = b.x + (down ? b.pressX : 0);
    b.sprite.y = b.y + (down ? b.pressY : 0);
}

void SceneKit::drawButton(SpriteButton& b) {
    if (b.exists) {
        draw(b.sprite);
    }
} // 020805e0, ordinary mode

// ---------------------------------------------------------------------------
// Composition (native; see the class comment for the layer order).

bool SceneKit::Windows::visible(unsigned layer, int x, int y) const {
    // Hardware window test: WIN0 before WIN1, then the outside mask; a
    // left/top edge beyond the right/bottom edge wraps around.
    if (!enabled[0] && !enabled[1]) {
        return true;
    }
    auto in = [](int v, int a, int b) {
        return a <= b ? (v >= a && v < b) : (v >= a || v < b);
    };
    for (int w = 0; w < 2; ++w) {
        if (enabled[w] && in(x, x1[w], x2[w]) && in(y, y1[w], y2[w])) {
            return (inside[w] & layer) != 0;
        }
    }
    return (outside & layer) != 0;
}

namespace {
// 02008e70: c = ((256 - f) * c * 8 + t * f) >> 11 per 5-bit component.
std::uint16_t tinted(std::uint16_t c, const SceneKit::Tint& t) {
    if (t.factor == 0) {
        return c;
    }
    const int keep = 0x100 - t.factor;
    const int target[3] = {t.r, t.g, t.b};
    std::uint16_t out = c & 0x8000;
    for (int k = 0; k < 3; ++k) {
        const int v = (c >> (5 * k)) & 31;
        out |= std::uint16_t(((keep * v * 8 + target[k] * t.factor) >> 11) & 31) << (5 * k);
    }
    return out;
}
} // namespace

Surface SceneKit::compose() {
    Surface image{256, 384, std::vector<std::uint16_t>(256 * 384, 0x8000)};
    if (painter) {
        painter(image);
        return image;
    }
    // The lower backdrop (BG palette colour 0) when a scene sets it: seen
    // where every layer is transparent, e.g. a BG3 behind another layer.
    if (lowerBackdrop) {
        std::fill(image.pixels.begin() + 256 * 192, image.pixels.end(), *lowerBackdrop);
    }
    std::vector<std::uint8_t> polygon(256 * 384, 0);
    // Model pixels keep their own 6-bit low bits (0x80 | r | g<<1 | b<<2).
    std::vector<std::uint8_t> low(256 * 384, 0);
    // Backgrounds with their vertical scroll, clipped to their own screen.
    auto background = [&](const std::string& name, int top, int scroll, int scrollX = 0) {
        if (name.empty()) {
            return;
        }
        const auto& bg = assets.background(name);
        const auto& win = windows[top ? Lower : Upper];
        for (int y = 0; y < 192; ++y) {
            const int sy = y + scroll;
            if (sy < 0 || sy >= int(bg.height)) {
                continue;
            }
            for (int x = 0; x < 256; ++x) {
                const int sx = scrollX ? (x + scrollX) & 255 : x;
                if (sx >= int(bg.width)) {
                    continue;
                }
                const auto c = bg.pixels[std::size_t(sy) * bg.width + std::size_t(sx)];
                if ((c & 0x8000) && win.visible(BG3, x, y)) {
                    image.pixels[std::size_t(y + top) * 256 + std::size_t(x)] =
                        tinted(c, top ? lowerTint : upperTint);
                }
            }
        }
    };
    background(upperBackground, 0, shownUpperScroll_);
    if (!upperOverlayBlend && !upperOverlay.empty()) {
        // Colour 0 (the blank tile's colour) shows the picture beneath.
        const auto& bg = assets.background(upperOverlay);
        const auto* tile = assets.backgroundTile(upperOverlay);
        const std::uint16_t clear = tile ? tile->pixels[0] : 0;
        for (int y = 0; y < 192; ++y) {
            const int sy = y + shownUpperScroll_;
            if (sy < 0 || sy >= int(bg.height)) {
                continue;
            }
            for (int x = 0; x < 256 && x < int(bg.width); ++x) {
                const auto c = bg.pixels[std::size_t(sy) * bg.width + std::size_t(x)];
                if ((c & 0x8000) && (!tile || c != clear) && windows[Upper].visible(BG2, x, y)) {
                    image.pixels[std::size_t(y) * 256 + std::size_t(x)] = tinted(c, upperTint);
                }
            }
        }
    }
    if (shake_ && !lowerBackground.empty()) {
        // Shaken BG3: the 256x256 map scrolled by (d, d) with wrap-around;
        // map rows past the picture hold tile 0 (measured, ref-myst-reveal).
        const auto& bg = assets.background(lowerBackground);
        const auto* tile = assets.backgroundTile(lowerBackground);
        for (int y = 0; y < 192; ++y) {
            for (int x = 0; x < 256; ++x) {
                const int mx = (x + shake_) & 255, my = (y + shake_ + shownLowerScroll_) & 255;
                std::uint16_t c = 0;
                if (mx < int(bg.width) && my < int(bg.height)) {
                    c = bg.pixels[std::size_t(my) * bg.width + std::size_t(mx)];
                } else if (tile) {
                    c = tile->pixels[std::size_t(my & 7) * 8 + std::size_t(mx & 7)];
                }
                if ((c & 0x8000) && windows[Lower].visible(BG3, x, y)) {
                    image.pixels[std::size_t(y + 192) * 256 + std::size_t(x)] = c;
                }
            }
        }
    } else if (!lowerPriority) {
        background(lowerBackground, 192, shownLowerScroll_, lowerScrollX);
        if (!lowerBackgroundBelow.empty() && !lowerBackground.empty()) {
            const int height = int(assets.background(lowerBackground).height);
            background(lowerBackgroundBelow, 192, shownLowerScroll_ - height, lowerScrollX);
        }
    }
    // A lower picture (BG2 / BG3) with its scroll; `transparent`: colour 0
    // (the blank tile's colour) shows what lies behind.
    auto lowerPicture = [&](const std::string& name, int scrollX, int scrollY, bool transparent) {
        if (name.empty()) {
            return;
        }
        const auto& bg = assets.background(name);
        const auto* tile = assets.backgroundTile(name);
        const std::uint16_t clear = tile ? tile->pixels[0] : 0;
        for (int y = 0; y < 192; ++y) {
            const int sy = y + scrollY;
            if (sy < 0 || sy >= int(bg.height)) {
                continue;
            }
            for (int x = 0; x < 256; ++x) {
                const int sx = (x + scrollX) & 255;
                if (sx >= int(bg.width)) {
                    continue;
                }
                const auto c = bg.pixels[std::size_t(sy) * bg.width + std::size_t(sx)];
                if ((c & 0x8000) && !(transparent && c == clear)) {
                    image.pixels[std::size_t(y + 192) * 256 + std::size_t(x)] =
                        tinted(c, lowerTint);
                }
            }
        }
    };
    // The text layers are BG1 of their screen and scroll with it.
    auto layer = [&](const Surface& text, int top, int scroll) {
        const auto& win = windows[top ? Lower : Upper];
        const int textAlpha = top ? lowerTextAlpha : -1;
        for (int y = 0; y < 192; ++y) {
            for (int x = 0; x < 256; ++x) {
                const int ty = y + scroll;
                if (ty < 0 || ty >= 192) {
                    continue;
                }
                auto c = text.pixels[std::size_t(ty) * 256 + std::size_t(x)];
                if ((c & 0x8000) && win.visible(BG1, x, y)) {
                    const auto i = std::size_t(y + top) * 256 + std::size_t(x);
                    if (textAlpha >= 0) {
                        std::uint16_t mixed = 0x8000;
                        for (int k = 0; k < 15; k += 5) {
                            const int a = (c >> k) & 31, b = (image.pixels[i] >> k) & 31;
                            mixed |= std::uint16_t(
                                std::min(31, (a * textAlpha + b * (16 - textAlpha)) >> 4) << k);
                        }
                        c = mixed;
                    }
                    image.pixels[i] = c;
                    polygon[i] = 0;
                    low[i] = 0;
                }
            }
        }
    };
    // The first adventure's 3D pipeline (ds3D): which lower pixels the 3D
    // layer holds, the coverage of anti-aliased edge pixels, the 3D pixel an
    // edge pixel was drawn over (the hardware keeps two layers) and the 2D
    // picture under the 3D layer, all as 6-bit colours.
    const bool ds = ds3D.has_value();
    constexpr std::size_t kLower = 256 * 192;
    std::vector<std::uint8_t> drawn3D, coverage, under;
    std::vector<std::uint32_t> under6, base6;
    auto six = [&](std::size_t i) {
        std::uint32_t out = 0;
        for (int k = 0; k < 3; ++k) {
            const int c = (image.pixels[i] >> (5 * k)) & 31;
            const int c6 =
                2 * c + (low[i] & 0x80 ? (low[i] >> k) & 1 : (polygon[i] && c != 0 ? 1 : 0));
            out |= std::uint32_t(c6) << (6 * k);
        }
        return out;
    };
    auto putSix = [&](std::size_t i, std::uint32_t c6) {
        std::uint16_t out = 0x8000;
        std::uint8_t bits = 0x80;
        for (int k = 0; k < 3; ++k) {
            const int c = int((c6 >> (6 * k)) & 63);
            out |= std::uint16_t((c >> 1) << (5 * k));
            bits |= std::uint8_t((c & 1) << k);
        }
        image.pixels[i] = out;
        low[i] = bits;
        polygon[i] = 0;
    };
    auto mark3D = [&](std::size_t i) {
        if (ds && i >= kLower) {
            drawn3D[i - kLower] = 1;
            coverage[i - kLower] = 31;
        }
    };
    // A pixel of the DS pipeline: opaque, an edge pixel pushing the 3D pixel
    // under it down for the anti-aliasing.
    auto put3D = [&](int x, int y, std::uint16_t color, int cover) {
        if (x < 0 || y < 0 || x >= 256 || y >= 192 || !windows[Lower].visible(BG0, x, y)) {
            return;
        }
        const auto j = std::size_t(y) * 256 + std::size_t(x), i = j + kLower;
        if (cover < 31) {
            under[j] = drawn3D[j];
            if (drawn3D[j]) {
                under6[j] = six(i);
            }
        }
        image.pixels[i] = std::uint16_t(color | 0x8000);
        polygon[i] = 1;
        low[i] = 0;
        drawn3D[j] = 1;
        coverage[j] = std::uint8_t(cover);
    };
    auto pipeline = [&](const Draw& d) {
        const dspipe::SpriteCamera& cam = ds3D->camera;
        using namespace dspipe;
        if (d.triangle) {
            // 0200f250: scale 1, then (256, 192), turned by angle 0.
            Matrix m = cam.view;
            translate(m, d.x << 12, d.y << 12, 0);
            scale(m, 0x1000, 0x1000, 0x1000);
            scale(m, 0x100000, 0xc0000, 0x1000);
            multiply3x3(m, rotationZ(cam.sine[0], cam.sine[1]));
            std::array<std::array<int, 4>, 3> corners{};
            for (std::size_t k = 0; k < 3; ++k) {
                corners[k] = {d.corners[k][0], d.corners[k][1], 0, 0};
            }
            drawPolygon(cam, m, corners,
                        [&](const Fragment& f) { put3D(f.x, f.y, d.fill, f.coverage); });
            return;
        }
        const Surface& img = *d.image;
        const int W = int(img.width), H = int(img.height);
        std::vector<Rect> whole{Rect{0, 0, W, H}};
        const auto& parts = d.quads && !d.quads->empty() ? *d.quads : whole;
        for (const auto& q : parts) {
            auto texel = [&](const Fragment& f, int u, int v) {
                const int px = q.x + u, py = q.y + v;
                if (px >= W || py >= H) {
                    return; // texture outside the frame is transparent
                }
                const auto c = img.pixels[std::size_t(py) * img.width + std::size_t(px)];
                if (c & 0x8000) {
                    put3D(f.x, f.y, c, f.coverage);
                }
            };
            if (d.turns >= 0) {
                // 02005e78.
                drawTurnedChunk(cam, d.x, d.y, unsigned(d.turns), W, H, q.x, q.y, q.width, q.height,
                                texel);
                continue;
            }
            // 02005bc0: sizes and offsets scaled in floats, truncated; a
            // centred unit square.
            auto by = [&](int v) {
                return int(float(v) * d.scale);
            };
            const int ox = by(q.x), oy = by(q.y), cw = by(q.width), ch = by(q.height);
            Matrix m = cam.view;
            translate(m, d.x << 12, d.y << 12, 0);
            translate(m, (ox + (cw >> 1) - (by(W) >> 1)) << 12,
                      (oy + (ch >> 1) - (by(H) >> 1)) << 12, 0);
            scale(m, cw << 12, ch << 12, 0x1000);
            const std::array<std::array<int, 4>, 4> corners{
                {{-0x800, -0x800, 0, 0},
                 {0x800, -0x800, q.width << 4, 0},
                 {0x800, 0x800, q.width << 4, q.height << 4},
                 {-0x800, 0x800, 0, q.height << 4}}};
            drawPolygon(cam, m, corners, [&](const Fragment& f) {
                texel(f, std::clamp(f.s >> 4, 0, q.width - 1),
                      std::clamp(f.t >> 4, 0, q.height - 1));
            });
        }
    };
    // 3D anti-aliasing at the end of the 3D frame: an edge pixel mixes with
    // the 3D pixel it covered by its coverage; over nothing it becomes
    // partly transparent and the 2D compositor mixes it with the picture.
    auto antialias = [&] {
        for (std::size_t j = 0; j < kLower; ++j) {
            if (!drawn3D[j] || coverage[j] >= 31) {
                continue;
            }
            const auto i = j + kLower;
            const std::uint32_t top6 = six(i);
            if (coverage[j] == 0) {
                putSix(i, under[j] ? under6[j] : base6[j]);
                continue;
            }
            const int cov = coverage[j] + 1;
            std::uint32_t out = 0;
            if (under[j]) {
                for (int k = 0; k < 3; ++k) {
                    const int t = int((top6 >> (6 * k)) & 63), b = int((under6[j] >> (6 * k)) & 63);
                    out |= std::uint32_t((t * cov + b * (32 - cov)) >> 5) << (6 * k);
                }
            } else if (ds3D->blendOverPicture) {
                const int a = (31 * cov) >> 5;
                for (int k = 0; k < 3; ++k) {
                    const int t = int((top6 >> (6 * k)) & 63), b = int((base6[j] >> (6 * k)) & 63);
                    out |= std::uint32_t(std::min(63, (t * (a + 1) + b * (31 - a) + 16) >> 5))
                           << (6 * k);
                }
            } else {
                continue; // its colour stays; only the 3D alpha drops
            }
            putSix(i, out);
        }
    };
    auto sprites = [&](bool upper) {
        if (ds && !upper) {
            drawn3D.assign(kLower, 0);
            coverage.assign(kLower, 31);
            under.assign(kLower, 0);
            under6.assign(kLower, 0);
            base6.resize(kLower);
            for (std::size_t j = 0; j < kLower; ++j) {
                base6[j] = six(j + kLower);
            }
        }
        for (const auto& d : shown_) {
            if (d.upper != upper) {
                continue;
            }
            const int top = upper ? 0 : 192;
            if (ds && !upper &&
                (d.triangle || d.turns >= 0 || (d.scale > 0 && ds3D->scaledSprites))) {
                pipeline(d);
                continue;
            }
            if (!d.image) {
                // Untextured polygon (02070e8c): measured on every pixel of
                // the hanko pulse (ref-myst-stamp): c6 = (f6 (a+1) + b6 (31-a)
                // [+16]) / 32 with the 6-bit colour behind (polygon low bits kept).
                for (int y = std::max(0, d.y); y < std::min(192, d.y + d.h); ++y) {
                    for (int x = std::max(0, d.x); x < std::min(256, d.x + d.w); ++x) {
                        if (!windows[Lower].visible(BG0, x, y)) {
                            continue;
                        }
                        const auto i = std::size_t(y + top) * 256 + std::size_t(x);
                        const auto below = image.pixels[i];
                        std::uint16_t out = 0x8000;
                        std::uint8_t bits = 0x80;
                        for (int k = 0; k < 3; ++k) {
                            const int f = (d.fill >> (5 * k)) & 31, b = (below >> (5 * k)) & 31;
                            const int f6 = 2 * f + (f != 0),
                                      b6 = 2 * b + (low[i] & 0x80 ? (low[i] >> k) & 1
                                                                  : (polygon[i] && b != 0 ? 1 : 0));
                            // Over a 3D pixel the engine blends without rounding; over
                            // the clear plane the 2D compositor adds 16 (row 283/284 of
                            // ref-myst-stamp, where the pulse extends below the entry).
                            const bool over3D = (low[i] & 0x80) || polygon[i];
                            const int c6 = d.alpha >= 31
                                               ? f6
                                               : (f6 * (d.alpha + 1) + b6 * (31 - d.alpha) +
                                                  (over3D ? 0 : 16)) /
                                                     32;
                            out |= std::uint16_t((c6 >> 1) << (5 * k));
                            bits |= std::uint8_t((c6 & 1) << k);
                        }
                        image.pixels[i] = out;
                        polygon[i] = 0;
                        low[i] = bits;
                        mark3D(i);
                    }
                }
                continue;
            }
            if (d.scale > 0) {
                // 0206eab0 in 20.12 fixed point (binary32 products truncated
                // by 0203aed0): A = ox*s, Q = w*s, Hw = W/2*s; the quad spans
                // X + A - Hw + (Q >> 1) + [floor(-Q/2), Q >> 1]. The hardware
                // covers [floor(L), floor(R)) and spreads the chunk's texels
                // evenly over that integer span (exact on all ten frames of
                // the mystery reveal, ref-myst-reveal). Later chunks cover
                // earlier ones on shared edge pixels.
                const int W = int(d.image->width), H = int(d.image->height);
                auto fx = [&](int v, int unit) {
                    return std::int64_t(float(v * unit) * d.scale);
                };
                auto span = [&](int origin, int offset, int size, int frame, int& from, int& to) {
                    const std::int64_t q = fx(size, 4096), base = std::int64_t(origin) * 4096 +
                                                                  fx(offset, 4096) -
                                                                  fx(frame, 2048) + (q >> 1);
                    from = int((base + ((-q) >> 1)) >> 12);
                    to = int((base + (q >> 1)) >> 12);
                };
                std::vector<Rect> whole{Rect{0, 0, W, H}};
                const auto& parts = d.quads && !d.quads->empty() ? *d.quads : whole;
                for (const auto& c : parts) {
                    int l, r, t, b;
                    span(d.x, c.x, c.width, W, l, r);
                    span(d.y, c.y, c.height, H, t, b);
                    if (r <= l || b <= t) {
                        continue;
                    }
                    for (int py = t; py < b; ++py) {
                        for (int px = l; px < r; ++px) {
                            if (px < 0 || py < 0 || px >= 256 || py >= 192) {
                                continue;
                            }
                            const int sx = c.x + (px - l) * c.width / (r - l),
                                      sy = c.y + (py - t) * c.height / (b - t);
                            if (sx >= W || sy >= H) {
                                continue; // texture area outside the frame is transparent
                            }
                            const auto col =
                                d.image->pixels[std::size_t(sy) * d.image->width + std::size_t(sx)];
                            if (!(col & 0x8000) || !windows[Lower].visible(BG0, px, py)) {
                                continue;
                            }
                            const auto i = std::size_t(py + top) * 256 + std::size_t(px);
                            image.pixels[i] = blend3D(col, d.alpha, image.pixels[i]);
                            polygon[i] = 1;
                            low[i] = 0;
                            mark3D(i);
                        }
                    }
                }
                continue;
            }
            if (d.turns >= 0) {
                blitRotatedTurns(image, *d.image, d.x, top + d.y, unsigned(d.turns), top,
                                 d.quads && !d.quads->empty() ? std::span<const Rect>(*d.quads)
                                 : d.chunks                   ? std::span<const Rect>(*d.chunks)
                                                              : std::span<const Rect>{});
                continue;
            }
            for (unsigned sy = 0; sy < d.image->height; ++sy) {
                for (unsigned sx = 0; sx < d.image->width; ++sx) {
                    const int dx = d.x + int(sx), dy = d.y + int(sy);
                    if (dx < 0 || dy < 0 || dx >= 256 || dy >= 192) {
                        continue;
                    }
                    const auto c = d.image->pixels[sy * d.image->width +
                                                   (d.mirrored ? d.image->width - 1 - sx : sx)];
                    if (!(c & 0x8000) ||
                        !windows[upper ? Upper : Lower].visible(upper ? OBJ : BG0, dx, dy)) {
                        continue;
                    }
                    const auto i = std::size_t(dy + top) * 256 + std::size_t(dx);
                    if (upper && upperSpriteBlend) {
                        const auto below = image.pixels[i];
                        std::uint16_t out = 0x8000;
                        for (int k = 0; k < 3; ++k) {
                            const int a = (c >> (5 * k)) & 31, b = (below >> (5 * k)) & 31;
                            out |= std::uint16_t(std::min(31, (a * upperSpriteBlend->eva +
                                                               b * upperSpriteBlend->evb) >>
                                                                  4)
                                                 << (5 * k));
                        }
                        image.pixels[i] = out;
                    } else {
                        image.pixels[i] = blend3D(c, d.alpha, image.pixels[i]);
                    }
                    polygon[i] = !upper;
                    low[i] = 0;
                    if (!upper) {
                        mark3D(i);
                    }
                }
            }
        }
        if (ds && !upper) {
            antialias();
        }
    };
    // 3D models (BG0): alpha blended over the background with the 3D alpha
    // (eva = a+1 of 32, 6-bit components, +16 rounding).
    auto models = [&] {
        if (shown3D_.size() != 256 * 192) {
            return;
        }
        const auto& win = windows[Lower];
        for (int y = 0; y < 192; ++y) {
            for (int x = 0; x < 256; ++x) {
                const auto v = shown3D_[std::size_t(y) * 256 + std::size_t(x)];
                const unsigned a = (v >> 24) & 31;
                if (!a || !win.visible(BG0, x, y)) {
                    continue;
                }
                const auto i = std::size_t(y + 192) * 256 + std::size_t(x);
                const auto below = image.pixels[i];
                std::uint16_t out = 0x8000;
                std::uint8_t bits = 0x80;
                for (int k = 0; k < 3; ++k) {
                    int c = int((v >> (8 * k)) & 63);
                    if (a < 31) {
                        const int b6 =
                            2 * ((below >> (5 * k)) & 31) +
                            (low[i] & 0x80 ? (low[i] >> k) & 1
                                           : (polygon[i] && ((below >> (5 * k)) & 31) ? 1 : 0));
                        c = std::min(63, (c * int(a + 1) + b6 * int(31 - a) + 16) >> 5);
                    }
                    out |= std::uint16_t((c >> 1) << (5 * k));
                    bits |= std::uint8_t((c & 1) << k);
                }
                image.pixels[i] = out;
                low[i] = bits;
                polygon[i] = 0;
            }
        }
    };
    models();
    if (lowerPriority) {
        // Back to front: larger priority values first; at equal values the
        // higher BG number lies behind.
        std::array<int, 4> order{3, 2, 1, 0};
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
            return (*lowerPriority)[std::size_t(a)] > (*lowerPriority)[std::size_t(b)];
        });
        bool behind = false;
        for (int bg : order) {
            if (bg == 3) {
                lowerPicture(lowerBackground, lowerScrollX, shownLowerScroll_, behind);
            } else if (bg == 2) {
                lowerPicture(lowerOverlay, lowerOverlayScrollX, 0, behind);
            } else if (bg == 1) {
                layer(lowerText_, 192, shownLowerScroll_);
            } else {
                sprites(false);
            }
            behind = behind || (bg == 3 && !lowerBackground.empty()) ||
                     (bg == 2 && !lowerOverlay.empty()) || bg < 2;
        }
    } else if (textAbove3D) {
        sprites(false);
        layer(lowerText_, 192, shownLowerScroll_);
    } else {
        layer(lowerText_, 192, shownLowerScroll_);
        sprites(false);
    }
    if (!lowerOverlay.empty() && !lowerPriority) {
        // BG2 shows no colour 0. The packed picture keeps colour 0 opaque,
        // but its blank tile (tile 0, all colour 0) gives its value.
        const auto& bg = assets.background(lowerOverlay);
        const auto* tile = assets.backgroundTile(lowerOverlay);
        const std::uint16_t clear = tile ? tile->pixels[0] : 0;
        for (int y = 0; y < 192 && y < int(bg.height); ++y) {
            for (int x = 0; x < 256 && x < int(bg.width); ++x) {
                const auto c = bg.pixels[std::size_t(y) * bg.width + std::size_t(x)];
                if ((c & 0x8000) && c != clear) {
                    image.pixels[std::size_t(y + 192) * 256 + std::size_t(x)] = c;
                    polygon[std::size_t(y + 192) * 256 + std::size_t(x)] = 0;
                    low[std::size_t(y + 192) * 256 + std::size_t(x)] = 0;
                }
            }
        }
    }
    layer(upperText_, 0, shownUpperScroll_);
    sprites(true);
    if (upperOverlayBlend && !upperOverlay.empty()) {
        // Colour 0 (the blank tile's colour) is transparent.
        const auto& bg = assets.background(upperOverlay);
        const auto* tile = assets.backgroundTile(upperOverlay);
        const std::uint16_t clear = tile ? tile->pixels[0] : 0;
        for (int y = 0; y < 192 && y < int(bg.height); ++y) {
            for (int x = 0; x < 256 && x < int(bg.width); ++x) {
                const auto c = bg.pixels[std::size_t(y) * bg.width + std::size_t(x)];
                if (!(c & 0x8000) || c == clear) {
                    continue;
                }
                auto& p = image.pixels[std::size_t(y) * 256 + std::size_t(x)];
                std::uint16_t out = 0x8000;
                for (int k = 0; k < 15; k += 5) {
                    const int a = (c >> k) & 31, b = (p >> k) & 31;
                    out |= std::uint16_t(
                        std::min(31, (a * upperOverlayBlend->eva + b * upperOverlayBlend->evb) >> 4)
                        << k);
                }
                p = out;
            }
        }
    }
    // Master brightness (0201abc8 writes 0x0400006c / 0x0400106c). Measured
    // in ref-tea-b for all 16 levels: the hardware darkens the 6-bit colour
    // as c6 * (16 - L) >> 4.
    for (int screen = 0; screen < 2; ++screen) {
        const int level = std::clamp(screen ? lowerBrightness_ : brightness_, -16, 16);
        if (level == 0) {
            continue;
        }
        for (std::size_t i = std::size_t(screen) * 256 * 192;
             i < std::size_t(screen + 1) * 256 * 192; ++i) {
            auto& p = image.pixels[i];
            std::uint16_t r = 0x8000;
            for (int shift : {0, 5, 10}) {
                const int c = (p >> shift) & 31;
                const int c6 = (low[i] & 0x80) ? 2 * c + ((low[i] >> (shift / 5)) & 1)
                                               : 2 * c + (polygon[i] && c != 0);
                // Brightening (positive levels, ref-room-a): c6+((63-c6)*L>>4).
                const int v =
                    level < 0 ? (c6 * (16 + level)) >> 4 : c6 + (((63 - c6) * level) >> 4);
                r |= std::uint16_t((v >> 1) << shift);
            }
            p = r;
        }
    }
    if (swapScreens) {
        // POWCNT1 bit 15 set: the main engine (here "lower") on the upper
        // screen and the sub engine on the lower one.
        std::swap_ranges(image.pixels.begin(), image.pixels.begin() + 256 * 192,
                         image.pixels.begin() + 256 * 192);
    }
    return image;
}
} // namespace picarat
