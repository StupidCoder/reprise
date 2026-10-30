#include "render/level_view.hpp"

#include <cstdlib>

#include "data/program_tables.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace doc::render {
namespace {

// Object render passes (Object+0x1c0 bits; SceneGame::render @0xff8d0 runs 1, 2, 4, 8).
constexpr int kPassLow = 1, kPassNormal = 2, kPassHigh = 8;

float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

}  // namespace

int LevelView::spriteDirection(float angle, int directions) { return game::spriteDirection(angle, directions); }

// The engine clock (Engine +0x1c over the tick time) counts the app's frames; the pattern's
// position in the original's shots matches it exactly (frame 1024: no shift). The level's first
// tick comes after the menus, so its frame number depends on the run; the port starts the
// clock so that it matches the reference runs (ticks since the boot + 1006: the shot of frame F
// shows the state after F - 1007 ticks, like the watches; until 2026-10-24 the comparison was
// one tick ahead and this was 1005).
// The app's frames before the level's boot: 1006 when 1-1 is played from the main menu as in
// the reference runs; a run that came another way (1-2 from the map boots at 2054) passes its
// own with DOC_BOOT_FRAME.
std::int64_t irisClockTicks() {
    static const std::int64_t ticks = std::getenv("DOC_BOOT_FRAME") ? std::atoll(std::getenv("DOC_BOOT_FRAME")) : 1006;
    return ticks;
}

const platform::Texture* LevelView::texture(const std::string& relative) { return canvas_.texture(relative); }

bool LevelView::load(platform::Host& host, const data::GameFiles& files, const game::Level& level,
                     std::string& error) {
    host_ = &host;
    files_ = &files;
    canvas_.bind(host, files);
    const bool chunked = !level.floor(0).chunks.empty();
    if ((!chunked && !texture(level.map().background)) || !texture("sprites/player_run.png")) {
        error = "level images missing";
        return false;
    }
    background_ = level.map().background;
    // App::loadFonts @0x1841ac: FNT_TXT is the game's TrueType font at 14 pixels.
    if (auto ttf = files.readResource("fonts/SPYmouse_efigs.ttf")) {
        std::string fontError;
        font_ = Font::load(*ttf, 14, fontError);
        mediumFont_ = Font::load(*ttf, 20, fontError);
        headingFont_ = Font::load(std::move(*ttf), 25, fontError);
    }
    std::string textError;
    strings_ = data::StringTable::load(files, "EN", textError);
    loadButtons(files);
    goalStars_ = data::readGoalStarImages(files);
    for (const auto& [x, y] : data::readMouseEyeOffsets(files)) eyeOffsets_.push_back({x, y});
    return true;
}

void LevelView::loadButtons(const data::GameFiles& files) {
    std::string guiError;
    popupLayout_ = data::loadGuiLayout(files, "popup_tutorial", guiError);
    resultsLayout_ = data::loadGuiLayout(files, "game_results", guiError);
    worldLayout_ = data::loadGuiLayout(files, "world_complete", guiError);
    pauseLayout_ = data::loadGuiLayout(files, "pause", guiError);
    kiskaLayout_ = data::loadGuiLayout(files, "level_skip", guiError);  // (1.1.1; "levelSkip" in 1.0.5)
    dossierLayout_ = data::loadGuiLayout(files, "dossier", guiError);
    msgboxLayout_ = data::loadGuiLayout(files, "msgbox", guiError);
    dossiers_ = data::readDossiers(files);
    levelTable_ = data::LevelTable::load(files, guiError);
    if (auto bytes = files.readResource("hud/hud_paws.png"))
        if (auto image = platform::decodeImage(*bytes)) pausePaws_ = {float(image->width), float(image->height)};
}

// Sprite::render @0x132c58: one frame (column `frame`, row `row`) of a sheet, centred on a map
// point, scaled, optionally mirrored, rotated `degrees` counter-clockwise (map space), tinted.
void LevelView::drawSprite(const platform::Texture& sheet, int frame, int row, float frameW, float frameH,
                           game::Vec2 centre, float scale, bool flip, float degrees, const Tint& tint,
                           platform::DrawStyle::Blend blend) {
    canvas_.drawSprite(sheet, frame, row, frameW, frameH, centre, scale, flip, degrees, tint, blend);
}

void LevelView::draw(platform::Host& host, const game::Session& session) {
    sheenUsed_ = 0;
    host_ = &host;
    canvas_.bind(host, *files_);
    // The floor on screen: its picture, decorations, objects and particles.
    const int floor = session.shownFloor();
    // The level scrolls in an infinite level: everything on the map is drawn moved left by the
    // view's scroll (the original translates the scene by SceneGame +0x13838).
    canvas_.viewX = session.viewLeft();
    // A screen shake grows and moves the whole picture up to the iris (SceneGame::render
    // @0xffd24 changes the projection: scale 1 + 0.05a / 1 + 0.075a, then a move of
    // (-20a, -25a) plus 3 toward the shake's corner, y up).
    // It stays for everything after the level's picture (HUD, results card) except the iris,
    // which PinholeManager::render draws with its own projection.
    applyShake(session);
    const game::LevelFloor& shown = session.level().floor(floor);
    if (!shown.chunks.empty()) {
        // Map::render: the strips in view, side by side from x = 0. (Placed on whole pixels: at
        // a fractional view a strip edge on a pixel centre left a black column between strips.)
        const float viewX = std::floor(canvas_.viewX + 0.5f);
        for (std::size_t i = 0; i < shown.chunks.size(); ++i) {
            const float x = float(i) * shown.chunkWidth - viewX;
            if (x + shown.chunkWidth <= 0.0f || x >= game::kScreenWidth) continue;
            if (const platform::Texture* strip = texture(shown.chunks[i]))
                host.draw(*strip, 0, 0, float(strip->width()), float(strip->height()), x, 0, float(strip->width()), 320);
        }
    } else if (const platform::Texture* bg = texture(shown.map.background)) {
        host.draw(*bg, 0, 0, float(bg->width()), float(bg->height()), 0, 0, 480, 320);
    }
    if (floor == 0) drawInGameTutorial(session);

    drawFluff(session, false);
    drawParticles(session, true);

    // Objects (and the mouse) in depth order: higher on the map first (QSort_ObjDepthCompare
    // @0xf7320), once per pass.
    std::vector<const game::LevelObject*> order;
    for (const game::LevelObject& o : session.objects())
        if (o.active && o.floor == floor) order.push_back(&o);
    std::stable_sort(order.begin(), order.end(),
                     [](const game::LevelObject* a, const game::LevelObject* b) { return a->position.y > b->position.y; });
    const float mouseY = session.mouse().position.y;

    for (const game::LevelObject* o : order) drawObject(session, *o, kPassLow);
    drawPath(host, session);  // Player::renderLow
    drawProjectiles(session, kPassLow);  // ProjectileManager::render(pass) after each pass's objects
    bool mouseDrawn = false;
    for (const game::LevelObject* o : order) {
        if (!mouseDrawn && o->position.y <= mouseY) {
            drawMouse(session);
            mouseDrawn = true;
        }
        drawObject(session, *o, kPassNormal);
    }
    if (!mouseDrawn) drawMouse(session);
    (void)mouseDrawn;
    drawProjectiles(session, kPassNormal);
    for (const game::LevelObject* o : order) drawObject(session, *o, kPassHigh);
    drawProjectiles(session, kPassHigh);  // ProjectileManager::render(8), after that pass's objects

    drawFluff(session, true);
    drawParticles(session, false);
    drawCaughtAnimation(session);
    canvas_.viewX = 0.0f;  // the HUD and the scenes above are on the screen

    // SceneGame::render @0xff8d0: the pause darkening (0, 0.01, 0.12) at 0.6 of its fade, under
    // the HUD.
    if (session.dimFade() > 0.0f)
        host.fillRect(0, 0, game::kScreenWidth, game::kScreenHeight,
                      (3u << 16) | (31u << 8) | std::uint32_t(std::lround(session.dimFade() * 0.6f * 255.0f)));
    drawHud(session);
    drawHandTutorial(session);  // its scene is above the level's (and below the intro's)
    if (session.intro().active) drawIntro(session);
    drawSpeech(session);  // SceneSpeech, above the game and its cutscene
    drawPause(session);         // ScenePause renders its items (the iris comes after)
    if (session.results().active()) {
        // The results scene on top: its iris, then its card (SceneGameResults::render).
        drawIris(session);
        applyShake(session);
        drawResults(session);
    } else if (session.worldComplete().active()) {
        // SceneWorldCompleteResults::render @0x218f4c: the iris, then the card.
        drawIris(session);
        applyShake(session);
        drawWorldComplete(session);
    } else {
        drawNotice(session);
        drawKiska(session);  // SceneLevelSkip, above the level's intro
        drawDossier(session);  // SceneDossier (1.1.1), above the level
        drawIris(session);
    }
    host.setTransform(1.0f, 1.0f, 0.0f, 0.0f);
    drawScreenCorners();
}

// SceneHandTutorial::render @0x1f739c: while the finger touches down, the ripple (59 x 40
// frames) centred a quarter of its size right of and below the fingertip; then renderHand
// @0x1f76dc: the shadow and the finger (both 512 x 384) hang from the fingertip at their
// top-left corner, the finger raised above its shadow by the lift (5 pressed, 25 lifted).
void LevelView::drawHandTutorial(const game::Session& session) {
    const game::HandTutorial& hand = session.hand();
    // (Under the intro, as after a retry from the pause menu, it is not drawn; under the pause
    // menu it is.)
    if (!hand.visible() || session.intro().active) return;
    const game::Vec2 tip = hand.position();
    if (hand.rippleVisible()) {
        const game::SpriteAnim& ripple = hand.ripple();
        if (const platform::Texture* sheet = texture("sprites/node_ripple.png"))
            drawSprite(*sheet, ripple.frame(), 0, ripple.frameWidth(), ripple.frameHeight(),
                       tip + game::Vec2{ripple.frameWidth() * 0.25f, -ripple.frameHeight() * 0.25f}, 1.0f, false, 0.0f,
                       {});
    }
    const platform::Texture* finger = texture("sprites/tut_finger.png");
    const platform::Texture* shadow = texture("sprites/tut_finger_shadow.png");
    if (!finger) return;
    const float w = float(finger->width()), h = float(finger->height());
    const game::Vec2 centre = tip + game::Vec2{w * 0.5f, -h * 0.5f};
    if (shadow) drawSprite(*shadow, 0, 0, w, h, centre, 1.0f, false, 0.0f, {});
    drawSprite(*finger, 0, 0, w, h, centre + game::Vec2{0.0f, hand.lift()}, 1.0f, false, 0.0f, {});
}

// SceneLevelIntro::renderStaticPinhole @0x14933c: the level dimmed by a dark blue veil
// (0, 0, 0.12, 0.6 x fade) with a round hole per spotlight (black_hole.png squares, the same
// colour with a clear disc, laid into gaps left in the veil), and the level's name at the top,
// fading in over 1-2 s and out after 3 s.
// PinholeManager::render @0x1f1890, kind 0 while the hole opens: the frame's alpha is cleared
// to opaque, pin_hole.png (opaque outside a clear disc) is drawn into the alpha channel over the
// hole's square with the blend (1 - a, a), which leaves 2a - a^2, and ui/bg_pattern.png is laid
// over the whole screen with the destination alpha as its weight: the pattern at native size,
// moving one 255th of its size diagonally every tick (Engine +0x1c over the 17 ms tick time).
// The port composes the same picture on the CPU. While opening the hole is at least 25 across
// its half (radius).
void LevelView::applyShake(const game::Session& session) {
    const game::Session::Shake& shake = session.shake();
    if (!shake.active()) {
        host_->setTransform(1.0f, 1.0f, 0.0f, 0.0f);
        return;
    }
    const float a = shake.amount;
    const float sx = a * 0.0500000007f + 1.0f, sy = a * 0.075000003f + 1.0f;
    const float tx = a * -20.0f + shake.direction.x * 3.0f, ty = a * -25.0f + shake.direction.y * 3.0f;
    host_->setTransform(sx, sy, tx, game::kScreenHeight * (1.0f - sy) - ty);
}

// SceneGame::renderCaughtAnimation @0xff42c: while the boss cat stands having seen the mouse
// (state 0x10), a dotted line (caught_line.png every 18 units, from 30 short of the mouse back
// to the cat, at most 63) runs from the cat to the mouse in 6 ms per unit of their distance, each
// piece fading in over its share of the time, and then a ring (caught_ring.png) fades in around
// the mouse over 250 ms. Both on the time SceneGame +0x138a0 counts during the wait.
void LevelView::drawCaughtAnimation(const game::Session& session) {
    const game::LevelObject* cat = nullptr;
    for (const game::LevelObject& o : session.objects())
        if (o.type == game::kBossCat) {
            cat = &o;
            break;
        }
    if (!cat || cat->state != game::ObjectState(0x12)) return;
    const game::Vec2 mouse = session.mouse().position;
    game::Vec2 dir = mouse - cat->position;
    const float distance = dir.normalise(1.0f);
    const int total = int((distance / 500.0f) * 3000.0f);
    const int timer = session.spottedTimerMs();
    const float elapsed = float(std::min(timer, total));
    const int count = std::min(int((distance + -28.0499992f) / 18.0f), 63);
    // (Positions in map space; the canvas takes off the view.)
    if (distance > 50.0f && count >= 1)
        if (const platform::Texture* line = texture("sprites/caught_line.png")) {
            const float degrees = std::atan2(dir.y, dir.x) * 57.2957764f;
            const float perPiece = float(total) / float(count);
            game::Vec2 at = mouse - dir * 30.0f;
            std::vector<game::Vec2> pieces(static_cast<std::size_t>(count));
            for (int k = count - 1; k >= 0; --k) {
                pieces[std::size_t(k)] = at;
                at = at - dir * 18.0f;
            }
            for (int i = 0; i < count; ++i) {
                const float alpha = std::min(1.0f, (elapsed - perPiece * float(i)) / perPiece);
                if (!(alpha > 0.0f)) continue;
                drawSprite(*line, 0, 0, float(line->width()), float(line->height()), pieces[std::size_t(i)], 1.0f,
                           false, degrees, Tint{1.0f, 1.0f, 1.0f, alpha});
            }
        }
    if (timer >= total)
        if (const platform::Texture* ring = texture("sprites/caught_ring.png")) {
            const float alpha = std::min(1.0f, float(timer - total) * 0.00400000019f);
            drawSprite(*ring, 0, 0, float(ring->width()), float(ring->height()), mouse, 1.0f, false, 0.0f,
                       Tint{1.0f, 1.0f, 1.0f, alpha});
        }
}

void LevelView::drawIris(const game::Session& session) {
    host_->setTransform(1.0f, 1.0f, 0.0f, 0.0f);
    const game::Session::Iris& iris = session.iris();
    if (!iris.opening && !iris.closing && !iris.closed) return;
    // While opening the hole keeps at least 25 of radius; shut, there is none.
    float r = std::max(0.0f, iris.radius);
    if (iris.opening) r = std::max(25.0f, r);
    iris_.draw(canvas_, iris.kind, iris.centre, r, iris.closed, int((session.frames() + irisClockTicks()) & 0xff));
}

void LevelView::drawIntro(const game::Session& session) {
    const game::Session::Intro& intro = session.intro();
    const float fade = std::min(1.0f, std::max(0.0f, float(intro.fadeMs) / float(intro.totalFadeMs)));
    struct Hole {
        float x, y, size;  // GL corner (y up)
    };
    std::vector<Hole> holes;
    for (const game::Session::Spotlight& l : intro.spotlights)
        if (l.size > 0.0f) holes.push_back({l.centre.x - l.size * 0.5f, l.centre.y - l.size * 0.5f, l.size});
    const float W = game::kScreenWidth, H = game::kScreenHeight;
    auto rectGl = [&](float x, float y, float w, float h, const platform::Texture* t, float r, float g, float b, float a) {
        if (w < 0.0f) x += w, w = -w;
        if (h < 0.0f) y += h, h = -h;
        // Whole pixels, so neighbouring pieces neither overlap nor leave seams (GL fills pixels
        // whose centres are inside).
        const float x0 = std::round(x), x1 = std::round(x + w), y0 = std::round(y), y1 = std::round(y + h);
        x = x0, w = x1 - x0, y = y0, h = y1 - y0;
        if (w <= 0.0f || h <= 0.0f) return;
        if (t) {
            platform::DrawStyle style;
            style.a = a;
            host_->draw(*t, 0, 0, float(t->width()), float(t->height()), x, H - y - h, w, h, style);
        } else {
            host_->fillRect(x, H - y - h, w, h,
                            (std::uint32_t(r * 255.0f + 0.5f) << 24) | (std::uint32_t(g * 255.0f + 0.5f) << 16) |
                                (std::uint32_t(b * 255.0f + 0.5f) << 8) | std::uint32_t(a * 255.0f + 0.5f));
        }
    };
    if (const platform::Texture* hole = texture("black_hole.png"))
        for (const Hole& h : holes) rectGl(h.x, h.y, h.size, h.size, hole, 1, 1, 1, fade);
    // The veil around the holes: strips left of each hole (sorted by x), the rest of the
    // screen, then above and below each hole.
    std::sort(holes.begin(), holes.end(), [](const Hole& a, const Hole& b) { return a.x < b.x; });
    const float veilA = fade * 0.600000024f, veilB = 0.119999997f;
    float cursor = 0.0f;
    for (const Hole& h : holes) {
        rectGl(cursor, 0.0f, h.x - cursor, H, nullptr, 0, 0, veilB, veilA);
        cursor = h.x + h.size;
    }
    rectGl(cursor, 0.0f, W - cursor, H, nullptr, 0, 0, veilB, veilA);
    for (const Hole& h : holes) {
        rectGl(h.x, 0.0f, h.size, h.y, nullptr, 0, 0, veilB, veilA);
        rectGl(h.x, h.y + h.size, h.size, H, nullptr, 0, 0, veilB, veilA);
    }
    // The level's name.
    const int age = intro.ageMs;
    float title = std::min(1.0f, std::max(0.0f, float(age - 1000) * 0.00100000005f));
    if (age > 3000) title = 1.0f - std::max(0.0f, float(age - 3000) * 0.00200000009f);
    if (intro.skipped) title *= fade;
    if (title > 0.0f && font_ && strings_) {
        const std::string& name = strings_->text(session.level().info().nameKey);
        // renderStr at (240, 320 - 40) with flags 0x42: centred, the bottom of 'y' on the point.
        if (!name.empty()) font_->drawFlags(*host_, name, W * 0.5f, 40.0f, 0x42, 1.0f, 0.0f, TextStyle{}, {1, 1, 1, title});
    }
}

// App::render @0x182cb6: the screen's rounded corners (res/corner.png, mirrored per corner) over
// everything.
void LevelView::drawScreenCorners() {
    const platform::Texture* corner = texture("corner.png");
    if (!corner) return;
    const float w = float(corner->width()), h = float(corner->height());
    for (int k = 0; k < 4; ++k) {
        platform::DrawStyle style;
        style.flipX = (k & 1) != 0;
        style.flipY = (k & 2) != 0;
        host_->draw(*corner, 0, 0, w, h, style.flipX ? game::kScreenWidth - w : 0.0f,
                    style.flipY ? game::kScreenHeight - h : 0.0f, w, h, style);
    }
}

// Map::renderFluff @0x13d440: the animated decorations, the plain ones under the objects and
// the ones flagged high above them; type 0x220 adds light.
void LevelView::drawFluff(const game::Session& session, bool high) {
    if (high) return;  // no decoration of the reconstructed levels is flagged high
    const std::vector<game::FluffItem>& items = session.level().fluff();
    for (std::size_t i = 0; i < items.size() && i < session.fluff().size(); ++i) {
        const game::FluffItem& f = items[i];
        if (f.floor != session.shownFloor()) continue;
        const platform::Texture* sheet = texture(f.kind.sheet);
        if (!sheet) continue;
        const game::SpriteAnim& anim = session.fluff()[i].anim;
        drawSprite(*sheet, anim.frame(), 0, anim.frameWidth(), anim.frameHeight(), f.position, 1.0f, false, 0.0f, {},
                   f.type == 0x220 ? platform::DrawStyle::Add : platform::DrawStyle::Alpha);
    }
}

// Particles::render @0x138714: particles of the mouse's floor whose delay has passed; colour and
// alpha (at most 1) per particle; firework bursts (0x18..0x1b) and kind 0x2a add light.
void LevelView::drawParticles(const game::Session& session, bool lowLayer) {
    // The first call draws particles flagged for under the objects (byte +0x1f4); in levels no
    // particle sets it (only the world map does), so everything is drawn by the second.
    if (lowLayer) return;
    platform::Host& host = *host_;
    const game::Particles& particles = session.particles();
    for (const game::Particle& p : particles.slots()) {
        if (!p.active || p.delayMs >= 1) continue;
        const game::ParticleKind& kind = particles.kind(p.type);
        if (p.floor != session.shownFloor()) continue;
        if (p.type >= game::kSpriteParticleTypes) {
            // Text particles: FNT_TXT for crumb points, else FNT_HEADING; a brown copy 2 below,
            // then cream (a cat's stun text: orange, then yellow), centred on the particle,
            // scaled by it.
            Font* font = p.type == game::kTextPoints ? font_.get() : headingFont_.get();
            if (!font || p.text.empty()) continue;
            // (Particles::render: the colour's alpha is +0x1d4, `scale` here; the size +0x1d8,
            // `alpha`, times the level's text scale, LevelState +0x64 = 1.)
            const float a = std::min(1.0f, std::max(0.0f, p.scale));
            const float sx = game::kScreenHeight - p.position.y;
            const float x = p.position.x - canvas_.viewX;  // (drawn in the scrolled scene)
            const bool stun = p.type == game::kTextStun;
            font->drawFlags(host, p.text, x, sx + 2.0f, 0x22, p.alpha, 0.0f, TextStyle{},
                            stun ? Colour{0xFA / 255.0f, 0x8C / 255.0f, 0x0A / 255.0f, a}
                                 : Colour{0x72 / 255.0f, 0x23 / 255.0f, 0x0A / 255.0f, a});
            font->drawFlags(host, p.text, x, sx, 0x22, p.alpha, 0.0f, TextStyle{},
                            stun ? Colour{1.0f, 0xDF / 255.0f, 0x3B / 255.0f, a}
                                 : Colour{1.0f, 0xF7 / 255.0f, 0xBE / 255.0f, a});
            continue;
        }
        const platform::Texture* sheet = texture(kind.sheet);
        if (!sheet) continue;
        const bool add = p.type == 0x2A || (p.type >= 0x18 && p.type <= 0x1B);
        // Particles::render @0x138714: the colour's alpha is the particle's +0x1d4 (`scale` here,
        // which also ends it at 0.01), at most 1; the picture's size is +0x1d8 (`alpha`).
        Tint tint{p.red, p.green, p.blue, std::min(1.0f, p.scale)};
        drawSprite(*sheet, p.sprite.frame(), 0, p.sprite.frameWidth(), p.sprite.frameHeight(), p.position, p.alpha,
                   false, p.rotation, tint, add ? platform::DrawStyle::Add : platform::DrawStyle::Alpha);
    }
}

// SceneGame::renderIntroTutorial @0xfce9c → SceneInGameTutorial::renderManual @0x295308: on
// the floor right after the map, each picture centred on its marker and each text wrapped to
// its width, in the map's colour (setTextColorForMap @0x294c38; alpha 0.8 except on map 10).
void LevelView::drawInGameTutorial(const game::Session& session) {
    const game::Session::TutorialView view = session.tutorialView();
    if (!view.shown) return;
    const int map = session.level().info().map;
    Colour colour{1.0f, 1.0f, 1.0f, view.alpha * 0.8f};
    switch (map) {
    case 10: colour = {0.447058827f, 0.654901981f, 0.792156875f, view.alpha}; break;
    case 11: colour = {0.772549033f, 0.772549033f, 0.850980401f, view.alpha * 0.8f}; break;
    case 98:
    case 705: colour = {1.0f, 0.788235307f, 0.674509823f, view.alpha * 0.8f}; break;
    case 507: colour = {0.882352948f, 0.572549045f, 0.439215690f, view.alpha * 0.8f}; break;
    case 534:
    case 703: colour = {0.972549021f, 0.713725507f, 0.596078455f, view.alpha * 0.8f}; break;
    case 708: colour = {0.717647076f, 0.792156875f, 0.901960790f, view.alpha * 0.8f}; break;
    case 736: colour = {0.760784328f, 0.572549045f, 0.447058827f, view.alpha * 0.8f}; break;
    default: break;
    }
    for (const game::TutorialPiece& piece : session.level().tutorial()) {
        if (view.slot >= 0 && piece.slot != view.slot) continue;
        game::Vec2 at = piece.position;
        // Mirrored, a picture is turned round in place and a text moves across the screen.
        if (view.mirrored && !piece.picture) at.x = 480.0f - at.x;
        if (piece.picture) {
            if (const platform::Texture* t = texture(piece.image))
                drawSprite(*t, 0, 0, float(t->width()), float(t->height()), at, 1.0f, view.mirrored, 0.0f, {});
        } else if (font_ && strings_) {
            TextStyle style;
            style.fillColourSet = true;
            style.fillColour = colour;
            font_->drawFlags(*host_, strings_->text(piece.textKey), at.x - canvas_.viewX, game::kScreenHeight - at.y, 0x22, 1.0f,
                             piece.wrapWidth, style, colour);
        }
    }
}

void LevelView::drawObject(const game::Session& session, const game::LevelObject& o, int pass) {
    for (const game::Cat& cat : session.cats())
        if (cat.objectId() == o.id) {
            drawCat(session, cat, o, pass);
            return;
        }
    if (game::isHomeHole(o.type)) {
        drawHomeHole(session, o, pass);
    } else if (game::isPipe(o.type) || game::isPipeFlap(o.type)) {
        // ObjControllerHideable::render @0x1b55f8: pipes lie on the floor (low pass) in their
        // tag's colour, grown while they pulse; flip-flaps stand in the normal pass, same colour.
        if (pass != (game::isPipe(o.type) ? kPassLow : kPassNormal)) return;
        if (const platform::Texture* sheet = texture(o.sprite)) {
            static const unsigned char kTagColours[8][3] = {{255, 216, 0}, {214, 18, 18}, {253, 131, 0},
                                                            {207, 40, 232}, {69, 208, 32}, {109, 113, 115},
                                                            {255, 255, 255}, {25, 98, 246}};
            const auto& c = kTagColours[((o.tag % 8) + 8) % 8];
            const float k = 0.00392156886f;
            const float scale = (o.pulseMs * 0.16f) * 0.00555555569f + 1.0f;
            drawSprite(*sheet, o.anim.frame(), 0, 32.0f, o.size.y, o.position, scale, false, 0.0f,
                       Tint{float(c[0]) * k, float(c[1]) * k, float(c[2]) * k, 1.0f});
        }
    } else if (game::isWallHole(o.type)) {
        // ObjControllerHideable::render @0x1b53f0: holes lie on the floor (low pass, like home
        // holes), grown while they pulse, in their tag's colour (getColourBasedTag @0x19c750).
        // While the mouse hides in a hole with the same tag, a flare pulses behind each of them
        // (Object::renderFlareEffect @0x113cf0: 1 + 0.16 sin(0.00667 t) times its size).
        if (pass != kPassLow) return;
        const game::Mouse& mouse = session.mouse();
        if (mouse.holeTag >= 0 && mouse.holeTag == o.tag && mouse.floor == o.floor)
            if (const platform::Texture* flare = texture("sprites/flare.png")) {
                const float s = std::sin(float(session.gameTimeMs()) * 0.00666666683f) * 0.16f + 1.0f;
                drawSprite(*flare, 0, 0, float(flare->width()), float(flare->height()), o.position, s, false, 0.0f, {});
            }
        if (const platform::Texture* sheet = texture(o.sprite)) {
            static const unsigned char kTagColours[8][3] = {{255, 216, 0}, {214, 18, 18}, {253, 131, 0},
                                                            {207, 40, 232}, {69, 208, 32}, {109, 113, 115},
                                                            {255, 255, 255}, {25, 98, 246}};
            const auto& c = kTagColours[((o.tag % 8) + 8) % 8];
            const float k = 0.00392156886f;  // 1/255
            const float scale = (o.pulseMs * 0.16f) * 0.00555555569f + 1.0f;
            drawSprite(*sheet, 0, 0, o.size.x, o.size.y, o.position, scale, false, 0.0f,
                       Tint{float(c[0]) * k, float(c[1]) * k, float(c[2]) * k, 1.0f});
        }
    } else if (o.type == game::kExitDoor || o.type == game::kEntranceDoor || o.type == game::kBossExitDoor) {
        // Object::render @0x11401c: the door's picture (9 or 8 frames of 48 x 96; open is the
        // last, closing runs back to the first). The boss exit is also drawn low
        // (Object::renderLow @0x1147ca): the map's exit picture (exit_map%d.png) 19.5 right and
        // 1 up of it, then the door (turned a quarter for tag 1).
        if (pass == kPassLow && o.type == game::kBossExitDoor) {
            char file[40];
            std::snprintf(file, sizeof file, "sprites/exit_map%d.png", session.level().info().map);
            const float turn = o.tag == 1 ? 90.0f : 0.0f;
            const game::Vec2 off = o.tag == 1 ? game::Vec2{-1.0f, 19.5f} : game::Vec2{19.5f, 1.0f};
            if (const platform::Texture* t = texture(file))
                drawSprite(*t, 0, 0, float(t->width()), float(t->height()), o.position + off, 1.0f, false, turn, {});
            if (const platform::Texture* sheet = texture(o.sprite))
                drawSprite(*sheet, o.anim.frame(), 0, o.size.x, o.size.y, o.position, 1.0f, false, turn, {});
            return;
        }
        if (pass != kPassNormal) return;
        if (const platform::Texture* sheet = texture(o.sprite))
            drawSprite(*sheet, o.anim.frame(), 0, o.size.x, o.size.y, o.position, 1.0f, false, 0.0f, {});
    } else if (game::isMunchHole(o.type)) {
        // ObjControllerHideable::render: a munch hole lies on the floor in the low pass, its
        // picture's frame (closed: 0), grown while it pulses.
        if (pass != kPassLow) return;
        const float scale = (o.pulseMs * 0.16f) * 0.00555555569f + 1.0f;
        if (const platform::Texture* sheet = texture(o.sprite))
            drawSprite(*sheet, o.anim.frame(), 0, o.size.x, o.size.y, o.position, scale, false, 0.0f, {});
    } else if (o.type == game::kBalloon) {
        // ObjControllerPickups::render: the picture's frame grown while it pulses (the generic
        // pickup path @0x1b8ed0; how a carried balloon is drawn is still to be checked).
        if (pass != kPassNormal) return;
        const float scale = (o.pulseMs * 0.16f) * 0.00555555569f + 1.0f;
        if (const platform::Texture* sheet = texture(o.sprite))
            drawSprite(*sheet, o.anim.frame(), 0, o.size.x, o.size.y, o.position, scale, false, 0.0f, {});
    } else if (o.type == game::kTeacup) {
        // ObjControllerPickups::render @0x1b8a1c: on the floor, the picture grown while it
        // pulses (the generic pickup path @0x1b8ed0); worn, the player draws it.
        if (pass != kPassNormal || o.state != game::ObjectState(0x21) || session.mouse().cup == o.id) return;
        const float scale = (o.pulseMs * 0.16f) * 0.00555555569f + 1.0f;
        if (const platform::Texture* sheet = texture(o.sprite))
            drawSprite(*sheet, 0, 0, o.size.x, o.size.y, o.position, scale, false, 0.0f, {});
    } else if (o.type == game::kCouch) {
        // ObjControllerHideable::render @0x1b5820: furniture stands in the normal pass, its
        // picture's frame grown while it pulses by its own 0.175 (+0x30), mirrored while it faces
        // left (a heading between pi/2 and 3pi/2; never for the couch so far).
        if (pass != kPassNormal) return;
        const float scale = (o.pulseMs * 0.174999997f) * 0.00555555569f + 1.0f;
        if (const platform::Texture* sheet = texture(o.sprite))
            drawSprite(*sheet, o.anim.frame(), 0, o.size.x, o.size.y, o.position, scale, false, 0.0f, {});
    } else if (o.type == game::kTv) {
        // Object::render: the left-facing set turned towards its seats, grown while it pulses.
        if (pass != kPassNormal) return;
        const float scale = (o.pulseMs * 0.16f) * 0.00555555569f + 1.0f;
        if (const platform::Texture* sheet = texture(o.sprite))
            drawSprite(*sheet, o.anim.frame(), 0, o.size.x, o.size.y, o.position, scale, false, o.pictureDegrees, {});
    } else if (o.type == game::kOilPatch) {
        // Object::renderLow @0x1147d4: the oil on the floor with the screen's glossy sheen.
        if (pass != kPassLow) return;
        if (const platform::Texture* sheet = texture(o.sprite))
            drawSprite(*sheet, 0, 0, o.size.x, o.size.y, o.position, 1.0f, false, 0.0f, {});
        drawSheen(session, o, 1.0f);
    } else if (o.type == game::kDroid) {
        drawDroid(session, o, pass);
    } else if (o.type == game::kSpitter || o.type == game::kRailTarget || o.type == game::kBeamEmitter) {
        drawSpitterRoom(session, o, pass);
    } else if (o.type == game::kVacuum || o.type == game::kBomb) {
        drawVacuumRoom(session, o, pass);
    } else if (game::isWallSpawner(o.type) || o.type == game::kBombTarget) {
        return;  // (pass mask 0: not drawn)
    } else if (o.type == game::kLauncher || o.type == game::kReflector || o.type == game::kBreakable) {
        // World 4's fight: the launcher (ObjControllerSpawner::render @0x1bb69c, turned by its
        // heading) and the reflectors and the light (Object::render: their frame) in their
        // pass (+0x1c0: 2; the second reflector 8, the fifth 8 while it turns left).
        int mask = kPassNormal;
        if (o.type == game::kReflector && (o.tag == 2 || (o.tag == 5 && o.state == game::ObjectState(0x3d)))) mask = kPassHigh;
        if (pass != mask || o.sprite.empty()) return;
        const float turn = o.type == game::kLauncher ? 360.0f : 0.0f;
        if (const platform::Texture* sheet = texture(o.sprite))
            drawSprite(*sheet, o.anim.frame(), 0, o.size.x, o.size.y, o.position, 1.0f, false, turn, {});
    } else if (game::isSwitch(o.type) || game::isDoor(o.type)) {
        drawSwitchOrDoor(session, o, pass);
    } else if (game::isRoomDoor(o.type)) {
        // ObjControllerFloorTransport::render @0x1b23e8 (trapdoors and house doors alike): the frame in the low pass, then
        // Object::renderBouncyArrowOverObject @0x112110 while the opening's 5 s run: the arrow
        // 35 above (below, turned over, within 64 of the top), bobbing 5 on sin(0.009 t); it
        // fades in over the first 0.6 s and out over the last second (hypothesis: the fade-out,
        // the decompiled arithmetic is unclear there).
        if (pass != kPassLow) return;
        if (const platform::Texture* sheet = texture(o.sprite))
            drawSprite(*sheet, o.anim.frame(), 0, o.size.x, o.size.y, o.position, 1.0f, false, 0.0f, {});
        if (o.timerMs > 0) {
            if (const platform::Texture* arrow = texture("sprites/arrow.png")) {
                const float t = float(o.timerMs);
                const float side = game::kScreenHeight - o.position.y < 64.0f ? -1.0f : 1.0f;
                const float lift = side * 35.0f + std::sin(t * 0.00899999961f) * side * 5.0f;
                Tint tint;
                if (t > 4400.0f) tint.a = (5000.0f - t) * 0.00166666671f;
                else if (t < 1000.0f) tint.a = t * 0.00100000005f;
                // (Turned over by a half turn: the arrow is symmetric.)
                canvas_.drawSprite(*arrow, 0, 0, float(arrow->width()), float(arrow->height()),
                                   {o.position.x, o.position.y + lift}, 1.0f, false, side < 0.0f ? 180.0f : 0.0f, tint);
            }
        }
    } else if (o.type == game::kKey) {
        // ObjControllerPickups::render: like a cheese, the picture grown while it pulses, with
        // the sheen; not while hidden in a hole.
        if (pass != kPassNormal || o.state == game::ObjectState::Hidden) return;
        const float scale = (o.pulseMs * 0.16f) * 0.00555555569f + 1.0f;
        if (const platform::Texture* sheet = texture(o.sprite))
            drawSprite(*sheet, 0, 0, o.size.x > 0.0f ? o.size.x : float(sheet->width()),
                       o.size.y > 0.0f ? o.size.y : float(sheet->height()), o.position, scale, false, 0.0f, {});
        drawSheen(session, o, scale);
    } else if (game::isSpeedItem(o.type)) {
        // ObjControllerPickups::render: the picture, grown while it pulses, with the sheen.
        if (pass != kPassNormal) return;
        const float scale = (o.pulseMs * 0.16f) * 0.00555555569f + 1.0f;
        if (const platform::Texture* sheet = texture(o.sprite))
            drawSprite(*sheet, 0, 0, o.size.x, o.size.y, o.position, scale, false, 0.0f, {});
        drawSheen(session, o, scale);
    } else if (o.type == game::kMouseTrap) {
        // Object::render @0x11401c (type 60): the trap (fading over its last 1.5 s), and while
        // armed the cheese in it (mouse_trap_cheesemask.png) with the cheese's sheen.
        if (pass != kPassNormal) return;
        const float alpha = std::min(1.0f, float(o.timerMs) * 0.000666666660f);
        const float scale = (o.pulseMs * 0.16f) * 0.00555555569f + 1.0f;
        if (const platform::Texture* sheet = texture(o.sprite))
            drawSprite(*sheet, o.anim.frame(), 0, o.size.x, o.size.y, o.position, scale, false, 0.0f,
                       Tint{1.0f, 1.0f, 1.0f, alpha});
        if (o.state == game::ObjectState(0x1d)) {
            if (const platform::Texture* mask = texture("sprites/mouse_trap_cheesemask.png"))
                drawSprite(*mask, 0, 0, float(mask->width()), float(mask->height()), o.position, scale, false, 0.0f,
                           Tint{1.0f, 1.0f, 1.0f, alpha});
            drawSheen(session, o, scale);
        }
    } else if (o.type == game::kClockworkMouse) {
        // ObjControllerPickups::render @0x1b88b8: on the floor or carried, the picture for its
        // facing out of 16 (cwmouse01..09.png, 10-16 mirrored).
        if (pass != kPassNormal) return;
        const float scale = (o.pulseMs * 0.16f) * 0.00555555569f + 1.0f;
        int dir = game::spriteDirection(o.rotation, 16);
        bool mirror = false;
        // (Thrown, the original keeps the set's last picture: the facing it was carried with.)
        if (dir > 8) {
            mirror = true;
            dir = 16 - dir;
        }
        char file[40];
        std::snprintf(file, sizeof file, "sprites/cwmouse%02d.png", dir + 1);
        if (const platform::Texture* sheet = texture(file))
            drawSprite(*sheet, o.anim.frame(), 0, 32.0f, float(sheet->height()), o.position, scale, mirror, 0.0f, {});
    } else if (game::isCheese(o.type)) {
        // (A cheese still in a cheese machine is not drawn.)
        if (pass == kPassNormal && o.state != game::ObjectState(game::kHeldInMachine)) drawCheese(session, o);
    } else if (o.type == game::kTwine || o.type == game::kCatnip) {
        if (pass == kPassNormal) drawLure(session, o);
    } else if (o.type == game::kCheeseMachine) {
        // Object::render @0x11403c: the machine, and the cheeses it has left in the text font,
        // 8 to the right of its middle.
        if (pass != kPassNormal) return;
        if (const platform::Texture* sheet = texture(o.sprite))
            drawSprite(*sheet, o.anim.frame(), 0, o.size.x, o.size.y, o.position, 1.0f, false, 0.0f, {});
        if (o.machineCount >= 1)
            font_->drawFlags(*host_, std::to_string(o.machineCount), o.position.x + 8.0f - canvas_.viewX,
                             game::kScreenHeight - o.position.y, 0, 1.0f, 0.0f, TextStyle{}, {1, 1, 1, 1});
    } else if (o.type == game::kCrumb || game::isBonusPickup(o.type)) {
        // ObjControllerPickups::render: the picture (grown while it pulses) and its sheen.
        if (pass != kPassNormal) return;
        const float scale = (o.pulseMs * 0.16f) * 0.00555555569f + 1.0f;
        if (const platform::Texture* sheet = texture(o.sprite))
            drawSprite(*sheet, o.anim.frame(), 0, o.size.x, o.size.y, o.position, scale, false, 0.0f, {});
        drawSheen(session, o, scale);
    } else if (game::isRoamer(o.type)) {
        // Object::render @0x11401c (default case): the picture at the object's place, grown while
        // it pulses, mirrored while the rotation points left (pi/2..3pi/2); never turned.
        if (pass != kPassNormal) return;
        for (const game::Roamer& r : session.roamers())
            if (r.objectId() == o.id) {
                const float scale = (o.pulseMs * 0.16f) * 0.00555555569f + 1.0f;
                const bool left = 1.57079637f < r.rotation() && r.rotation() < 4.71238899f;
                const platform::Texture* sheet = texture(o.sprite);
                if (!sheet) return;
                if (o.type == 420 || o.type == 421)
                    drawSprite(*sheet, r.anim.frame(), 0, r.anim.frameWidth(), r.anim.frameHeight(), r.position(),
                               scale, left, 0.0f, {});
                else
                    drawSprite(*sheet, 0, 0, float(sheet->width()), float(sheet->height()), r.position(), scale, left,
                               0.0f, {});
            }
    }
}

// SceneSpeech's dialogue box (GuiDialogueBox: GuiResizableFrame::render @0x1dded0, placed by
// SceneSpeech::configureDialogueBox @0x186e84 and GuiDialogueBox::update @0x198ad0): a box
// 200 wide beside the speaker (right of it on the left half of the screen, else left of it,
// 4.5 apart; pulled down to stay on screen), its body stretched to the text wrapped 25 narrower,
// the header above and the footer below, and the tail on the speaker's side with its top at
// the speaker's middle (kept within the body). The whole box bounces about its middle; the
// text (black, centred) shows once the box is fully in.
void LevelView::drawSpeech(const game::Session& session) {
    const game::BounceFrame* box = session.speechBox();
    if (!session.inCutscene() || !box || !box->visible() || !font_ || !strings_) return;
    const data::Speech& line = session.speechLine();
    const int speaker = line.actor == "fluffy" ? game::kFluffy : line.actor == "henchman" ? game::kFluffy2 : -1;
    const game::LevelObject* who = nullptr;
    for (const game::LevelObject& o : session.objects())
        if (o.type == speaker) {
            who = &o;
            break;
        }
    if (!who) return;  // (the mouse and the bosses of later worlds speak too; not in world 1)
    const platform::Texture* body = texture("ui/speechbubble_body.png");
    const platform::Texture* header = texture("ui/speechbubble_header.png");
    const platform::Texture* footer = texture("ui/speechbubble_footer.png");
    if (!body || !header || !footer) return;
    const std::string text = strings_->text(line.textKey);
    const float w = float(body->width());
    const float wrapH = font_->wrapHeight(*host_, text, w + -25.0f, 1.0f);
    const game::Vec2 at{who->position.x - session.viewLeft(), who->position.y};
    // configureDialogueBox: beside the speaker, kept on screen.
    float bx = at.x < 240.0f ? at.x + who->size.x * 0.5f + 4.5f : at.x - w - who->size.x * 0.5f - 4.5f;
    float by = at.y;
    const float boxHeight = float(header->height()) + float(footer->height()) + wrapH + 10.0f;
    if (by - boxHeight < 0.0f) by = boxHeight + 10.0f;
    // The frame drawn: body from by - wrapH up to by, header above, footer below.
    const float bodyY = by - wrapH;
    float sx = 1.0f, sy = 1.0f;
    box->scales(sx, sy);
    const float cx = bx + w * 0.5f, cy = game::kScreenHeight - (bodyY + wrapH * 0.5f);
    const Affine xf{sx, cx * (1.0f - sx), sy, cy * (1.0f - sy)};
    auto put = [&](const platform::Texture& t, float x, float y, float pw, float ph, bool mirror = false) {
        platform::DrawStyle style;
        style.flipX = mirror;
        host_->draw(t, 0, 0, float(t.width()), float(t.height()), xf.x(x), xf.y(game::kScreenHeight - (y + ph)), pw * xf.ax,
                    ph * xf.ay, style);
    };
    put(*body, bx, bodyY, w, wrapH);
    put(*header, bx, bodyY + wrapH, w, float(header->height()));
    put(*footer, bx, bodyY - float(footer->height()), w, float(footer->height()));
    // GuiDialogueBox::update: the tail on the speaker's side.
    const bool left = bx > at.x;
    if (const platform::Texture* tail = texture(left ? "ui/speechbubble_tail.png" : "ui/speechbubble_tail_r.png")) {
        const float th = float(tail->height()), tw = float(tail->width());
        const float ty = std::min(wrapH, std::max(-wrapH, at.y - by + wrapH - th));
        const float tx = left ? 10.0f - tw : w - 10.0f;
        put(*tail, bx + tx, bodyY + ty, tw, th);
    }
    if (!box->finished() || !box->isShowing()) return;
    TextStyle style;
    style.fillColourSet = true;
    style.fillColour = {0.0f, 0.0f, 0.0f, 1.0f};
    const float lineH = font_->lineHeight();
    font_->drawFlags(*host_, text, bx + w * 0.5f, game::kScreenHeight - (by - lineH), 0x02, 1.0f, w + -25.0f, style, {0, 0, 0, 1}, {});
}

// ObjControllerSwitch::render @0x1aeab4 (low pass): the button's frame, grown while it pulses,
// with the sheen while it is raised; a counting timed button shows its countdown over it at 0.8
// alpha. ObjControllerDoors::render @0x1b0ab0: the bars turned by their facing, in the low pass
// while open (always for types 215 and 217), else the normal one.
void LevelView::drawSwitchOrDoor(const game::Session& session, const game::LevelObject& o, int pass) {
    const platform::Texture* sheet = texture(o.sprite);
    if (!sheet) return;
    for (const game::Switch& s : session.switches()) {
        if (s.objectId != o.id) continue;
        if (pass != kPassLow) return;
        const float scale = (o.pulseMs * 0.16f) * 0.00555555569f + 1.0f;
        drawSprite(*sheet, s.anim.frame(), 0, 20.0f, float(sheet->height()), o.position, scale, false, 0.0f, {});
        if (s.state == game::kSwitchUp) drawSheen(session, o, scale);
        if (game::isTimedSwitch(s.type) && s.timerMs > 0)
            if (const platform::Texture* timer = texture("sprites/timer.png"))
                drawSprite(*timer, s.timer.frame(), 0, 18.0f, float(timer->height()), o.position, 1.0f, false, 0.0f,
                           Tint{1.0f, 1.0f, 1.0f, 0.800000012f});
        return;
    }
    for (const game::Door& d : session.doors()) {
        if (d.objectId != o.id) continue;
        const int want = d.pass == 1 ? kPassLow : kPassNormal;
        if (pass != want) return;
        drawSprite(*sheet, d.anim.frame(), 0, 32.0f, float(sheet->height()), o.position, 1.0f, false,
                   d.rotation * 57.2957764f, {});
        return;
    }
}

// World 2's fight. The Spitter (ObjControllerBoss::render @0x1aa1ae, normal pass): its facing
// frame of boss2.png, or broken the looping wreck. A rail sphere (Object::render's default
// case): its frame, mirrored while it heads left. A wall device: its frame in the high pass
// (Object::loadAssets sets the pass); in the normal pass, while it shows the beam, sprites/
// bolt.png stretched from it to its partner, 8 high, its texture scrolling 0.008 a frame by the
// device's clock (Object::render @0x1142a2; both devices draw it).
void LevelView::drawSpitterRoom(const game::Session& session, const game::LevelObject& o, int pass) {
    const platform::Texture* sheet = texture(o.sprite);
    if (o.type == game::kSpitter) {
        if (pass != kPassNormal) return;
        for (const game::Spitter& s : session.spitters())
            if (s.objectId() == o.id) {
                if (s.state() == 0x1c) {
                    if (const platform::Texture* wreck = texture("sprites/boss2/boss2_destroyed.png"))
                        drawSprite(*wreck, s.wreck.frame(), 0, 64.0f, float(wreck->height()), s.position(), 1.0f,
                                   false, 0.0f, {});
                } else if (sheet) {
                    drawSprite(*sheet, s.body.frame(), 0, 64.0f, float(sheet->height()), s.position(), 1.0f, false,
                               0.0f, {});
                }
            }
        return;
    }
    if (o.type == game::kRailTarget) {
        if (pass != kPassNormal || !sheet) return;
        for (const game::RailTarget& t : session.railTargets())
            if (t.objectId == o.id) {
                const bool left = 1.57079637f < t.heading && t.heading < 4.71238899f;
                drawSprite(*sheet, t.anim.frame(), 0, 32.0f, float(sheet->height()), t.position, 1.0f, left, 0.0f, {});
            }
        return;
    }
    for (const game::BeamEmitter& e : session.emitters()) {
        if (e.objectId != o.id) continue;
        if (pass == kPassHigh && sheet) {
            drawSprite(*sheet, e.anim.frame(), 0, 32.0f, float(sheet->height()), o.position, 1.0f, false, 0.0f, {});
        } else if (pass == kPassNormal && e.beamShown()) {
            const game::LevelObject* partner = nullptr;
            for (const game::LevelObject& other : session.objects())
                if (other.id == e.partner) partner = &other;
            const platform::Texture* bolt = texture("sprites/bolt.png");
            if (!partner || !bolt) return;
            const float length = partner->position.x - o.position.x;
            const float h = float(bolt->height());
            const float w = float(bolt->width());
            // The scroll: whole frames of the clock (its ms over the tick time), 0.008 of the
            // texture each; the picture repeats.
            float offset = float(e.clockMs / game::kTickMs) * 0.00800000038f;
            offset = offset - std::floor(offset);
            // Drawn as two pieces so the texture can wrap: [offset, 1) then [0, offset).
            const float x0 = o.position.x, y = o.position.y;
            const float split = length * (1.0f - offset);
            platform::DrawStyle style;
            auto piece = [&](float u0, float u1, float from, float to) {
                if (to == from) return;
                const float left = std::min(from, to), right = std::max(from, to);
                const bool mirrored = to < from;
                platform::DrawStyle st = style;
                st.flipX = mirrored;
                host_->draw(*bolt, u0 * w, 0.0f, (u1 - u0) * w, h, left - canvas_.viewX,
                            game::kScreenHeight - y - h * 0.5f, right - left, h, st);
            };
            piece(offset, 1.0f, x0, x0 + split);
            piece(0.0f, offset, x0 + split, x0 + length);
        }
    }
}

// ProjectileManager::render @0x18b8dc for one pass: what is in the air or stuck, of that pass's
// layer, on the shown floor; its picture turned along its flight; a straight one fading out
// over its last second.
void LevelView::drawProjectiles(const game::Session& session, int pass) {
    static const char* const kPictures[6] = {"sprites/shuriken.png", nullptr, "sprites/boss4_shuriken.png", "sprites/boss2/boss2_fork.png",
                                             "sprites/boss2/boss2_spoon.png", "sprites/boss2/boss2_knife.png"};
    for (const game::Projectile& p : session.projectiles().slots()) {
        if (!p.inUse() || p.status == game::ProjectileStatus::Made || p.status == game::ProjectileStatus::Gone) continue;
        if (p.layer != pass || p.floor != session.shownFloor() || p.picture < 0 || p.picture > 5 || !kPictures[p.picture])
            continue;
        const platform::Texture* sheet = texture(kPictures[p.picture]);
        if (!sheet) continue;
        float alpha = 1.0f;
        if (p.flight == 2 && p.status == game::ProjectileStatus::Fading) alpha = clamp01(p.timerMs * 0.00100000005f);
        const float frameW = game::isShuriken(p.picture) ? float(sheet->width()) : 30.0f;
        drawSprite(*sheet, p.anim.frame(), 0, frameW, float(sheet->height()), p.position, 1.0f, false,
                   p.angle * 57.2957764f, Tint{1.0f, 1.0f, 1.0f, alpha});
    }
}

// ObjControllerBoss::renderAccela @0x1acbf4 (normal pass), about the droid's place: broken,
// only its wreck; else the crash's spark flash (turned along the roll), the back part, the dome
// with Fluffy turning by its wobble 7 up, then 15 up the body (or the sensor head facing its
// heading while it watches, rages, charges, rolls or slides) and the base over it.
void LevelView::drawDroid(const game::Session& session, const game::LevelObject& o, int pass) {
    if (pass != kPassNormal) return;
    const game::Droid* droid = nullptr;
    for (const game::Droid& d : session.droids())
        if (d.objectId() == o.id) droid = &d;
    if (!droid) return;
    const game::Vec2 at = droid->position();
    auto part = [&](const char* file, int frame, float frameW, game::Vec2 offset, float degrees) {
        const platform::Texture* t = texture(file);
        if (!t) return;
        const float w = frameW > 0.0f ? frameW : float(t->width());
        drawSprite(*t, frame, 0, w, float(t->height()), at + offset, 1.0f, false, degrees, {});
    };
    if (droid->state() == 0x1c) {
        part("sprites/boss1/boss1_destroyed.png", droid->wreck.frame(), 90.0f, {}, 0.0f);
        return;
    }
    if (droid->sparkShown())
        part("sprites/boss1/boss1_spark_base.png", 0, 64.0f, {}, droid->coastDirection().angleDegrees());
    part("sprites/boss1/boss1_part3.png", 0, 0.0f, {}, 0.0f);
    // (The dome turns about its own middle, 7 up.)
    part("sprites/boss1/boss1_part2.png", 0, 0.0f, {0.0f, 7.0f}, droid->wobble());
    if (droid->showsSensor())
        part("sprites/boss1/boss1_part1_sensed.png", game::spriteDirection(droid->heading(), 8), 64.0f, {0.0f, 15.0f},
             0.0f);
    else
        part("sprites/boss1/boss1_part1.png", droid->body.frame(), 64.0f, {0.0f, 15.0f}, 0.0f);
    part("sprites/boss1/boss1_part0.png", droid->base.frame(), 64.0f, {0.0f, 15.0f}, 0.0f);
}

// World 3's fight (ObjControllerBoss::render @0x1a9e24, normal pass): the vacuum's body on its
// facing frame with the nozzle (boss3_part0 frame 0) over it; while it sucks the suction picture
// (boss3_part2) instead, the nozzle's frame 1, and the swirl (vacuum_effect) turned with the
// heading, 0.7 of its height ahead. Broken, the body alone. A bomb: its frame in the high pass
// (Object::renderHigh @0x114b4c).
void LevelView::drawVacuumRoom(const game::Session& session, const game::LevelObject& o, int pass) {
    if (o.type == game::kBomb) {
        if (pass != kPassHigh) return;
        if (const platform::Texture* sheet = texture(o.sprite))
            drawSprite(*sheet, o.anim.frame(), 0, 28.0f, float(sheet->height()), o.position, 1.0f, false, 0.0f, {});
        return;
    }
    if (pass != kPassNormal) return;
    for (const game::Vacuum& v : session.vacuums()) {
        if (v.objectId() != o.id) continue;
        auto part = [&](const char* file, int frame, float frameW, game::Vec2 at, float degrees) {
            if (const platform::Texture* t = texture(file))
                drawSprite(*t, frame, 0, frameW, float(t->height()), at, 1.0f, false, degrees, {});
        };
        const game::Vec2 at = v.position();
        if (!v.sucking()) {
            part("sprites/boss3/boss3_part1.png", v.body.frame(), 64.0f, at, 0.0f);
            if (v.state() != 0x1c) part("sprites/boss3/boss3_part0.png", 0, 64.0f, at, 0.0f);
            return;
        }
        part("sprites/boss3/boss3_part2.png", v.suction.frame(), 64.0f, at, 0.0f);
        part("sprites/boss3/boss3_part0.png", 1, 64.0f, at, 0.0f);
        // (glRotatef by (heading + pi/2) degrees, then glTranslatef(0, -0.7 h).)
        const float turn = v.heading() + 1.57079637f;
        const float ahead = v.size().y * 0.699999988f;
        const game::Vec2 swirl{at.x + ahead * std::sin(turn), at.y - ahead * std::cos(turn)};
        part("sprites/vacuum_effect.png", v.effect.frame(), 34.0f, swirl, turn * 57.2957764f);
        return;
    }
}

namespace {

// A cat kind's pictures (ObjControllerCat::loadAssets @0x19fc7c): walking in five facings
// (<walk>1..5.png, the other three mirrored), sitting either in five facings too or in one
// picture, and the stunned/aiming one.
struct CatPictures {
    std::string walk, idle, common;
    bool idleSingle = false;
    std::string idleOneFacing() const { return idleSingle ? idle : idle + "1.png"; }
};

CatPictures catPictures(int type) {
    if (game::wearsBossCat(type)) return {"sprites/bosscat0", "sprites/bosscat_sit.png", "sprites/bosscat_common.png", true};
    // The black cat's sitting is one picture played by anim_sequences/black_cat_idle.
    if (type == 0x34) return {"sprites/blackcat0", "sprites/blackcat_idle.png", "sprites/blackcat_common.png", true};
    // The blind cat loads walking pictures only (sitting or stunned it draws nothing).
    if (type == 0x36) return {"sprites/blindcat0", "", "", true};
    if (type == 0x38) return {"sprites/ninjacat0", "sprites/ninjacat_idle0", "sprites/ninjacat_common.png", false};
    return {"sprites/browncat0", "sprites/browncat_idle0", "sprites/browncat_common.png", false};
}

}  // namespace

// ObjControllerCat::render @0x1a100c. Low pass (renderLow @0x1a4528): the route as faint dots,
// and the cat's eyes (cat_eyes.png at 0.2) where it last saw the mouse while it runs there
// without seeing it. Normal pass (renderCat @0x1a41cc): the picture getActiveSprite picks;
// walking and idle pictures face the cat's facing row (8 rows in 5 files, rows 5-7 the
// mirror images of 3-1); the others, enlarged by the pulse, mirrored when heading left.
void LevelView::drawCat(const game::Session& session, const game::Cat& cat, const game::LevelObject& o, int pass) {
    // The boss cat shows its view in every pass it is drawn in (ObjControllerCat::render
    // @0x1a106e). SceneGame::render draws an object only in the passes of its mask (Object
    // +0x1c0): a cat's leaves out the high pass, the cone's tint is two layers (run lb3_watch;
    // the mask's value itself is not traced).
    if (pass == kPassHigh) return;
    if (cat.type() == game::kBossCat) drawViewArc(session, cat);
    if (pass == kPassLow) {
        if (cat.route().startMode != 1) {
            drawCatRoute(cat);
            // Path::renderPathPoint @0x11dd54: sleepspot.png where the cat sits, at its own size,
            // in the same faint added colour.
            if (const platform::Texture* spot = texture("sprites/sleepspot.png"))
                for (const game::CatRoute::Point& p : cat.route().points)
                    if (p.pauseSeconds >= 1)
                        drawSprite(*spot, 0, 0, float(spot->width()), float(spot->height()), p.position, 1.0f, false,
                                   0.0f, Tint{1.0f, 1.0f, 1.0f, 0.7f}, platform::DrawStyle::Add);
        }
        if (!cat.sawMouse() && cat.state() == 5 && cat.chaseStep() == 1)
            if (const platform::Texture* eyes = texture("sprites/cat_eyes.png")) {
                const game::SpriteAnim& anim = session.catEyes();
                Tint tint{1.0f, 1.0f, 1.0f, 0.2f};
                drawSprite(*eyes, anim.frame(), 0, anim.frameWidth(), anim.frameHeight(), cat.seenAt(), 1.0f, false,
                           0.0f, tint);
            }
        return;
    }
    if (pass != kPassNormal) return;
    // (ObjControllerCat::getActiveSprite @0x18c994 (1.0.5): Fluffy out of play, state 0x12, has no
    // picture: run lbf2_world2, where he waits by the exit in view.)
    if ((cat.type() == game::kFluffy || cat.type() == game::kFluffy2) && cat.state() == 0x16) return;
    const game::SpriteAnim& sprite = cat.activeSprite();
    const game::Cat::Picture picture = cat.picture();
    if (cat.type() == game::kFluffy2 && cat.henchmanLaunched() && picture == game::Cat::Picture::Common) {
        // World 3's henchman after his launch (renderCat @0x1a436a, +0x208 set): his cry
        // picture (boss3_cry.png, 64 wide: flying, then the splash in the pool) turned by +0x108
        // as degrees.
        if (const platform::Texture* sheet = texture("sprites/boss3/boss3_cry.png"))
            drawSprite(*sheet, sprite.frame(), 0, 64.0f, float(sheet->height()), cat.position(), 1.0f, false,
                       cat.heading(), {});
        return;
    }
    if (cat.type() == game::kFluffy2 && cat.ridesBalloon() && picture == game::Cat::Picture::Common) {
        // World 4's henchman dunked (ObjControllerCat::loadAssets: boss4_cry.png, 64 wide, as his
        // +0x240 picture), at his place.
        if (const platform::Texture* sheet = texture("sprites/boss4/boss4_cry.png"))
            drawSprite(*sheet, sprite.frame(), 0, 64.0f, float(sheet->height()), cat.position(), 1.0f, false, 0.0f, {});
        return;
    }
    if (picture == game::Cat::Picture::Pose) {
        // A cutscene's part picture (Object +0x21c = 2: the sprite at +0x3b8, Fluffy's
        // bosscat_sit.png) at the cat's place, whatever its facing (ObjControllerCat::render).
        if (const platform::Texture* sheet = texture("sprites/bosscat_sit.png"))
            drawSprite(*sheet, sprite.frame(), 0, 50.0f, float(sheet->height()), cat.position(), 1.0f, false, 0.0f, {});
        return;
    }
    // The black cat sits through sequences: a cell of its grid sheet and an offset per frame
    // (mirrored with the offset flipped for facings 5..7).
    if (picture == game::Cat::Picture::Idle && cat.idle.currentSequence() >= 0) {
        const data::AnimSequence* seq = cat.idle.sequence(cat.idle.currentSequence());
        const int index = cat.idle.sequenceFrame();
        if (seq && index >= 0 && index < int(seq->frames.size()))
            if (const platform::Texture* sheet = texture(seq->image)) {
                const data::SequenceFrame& f = seq->frames[std::size_t(index)];
                const bool mirror = cat.idleMirrored();
                const game::Vec2 at = cat.position() + game::Vec2{mirror ? -f.offsetX : f.offsetX, f.offsetY};
                drawSprite(*sheet, f.column, f.row, float(seq->frameWidth), float(seq->frameHeight), at, 1.0f, mirror,
                           0.0f, {});
            }
        return;
    }
    constexpr float kPulseAmount = 0.16f;  // Object+0x30
    if (picture == game::Cat::Picture::Balloon) {
        // World 4's henchman in his balloon (+0x474, boss4.png): one picture whatever his
        // heading, mirrored when it points left as the other single pictures are.
        const float h = cat.heading();
        const bool left = 1.57079637f < h && h < 4.71238899f;
        if (const platform::Texture* sheet = texture("sprites/boss4/boss4.png"))
            drawSprite(*sheet, sprite.frame(), 0, sprite.frameWidth(), sprite.frameHeight(), cat.position(), 1.0f,
                       left, 0.0f, {});
        return;
    }
    if (cat.state() == 9 || picture == game::Cat::Picture::Common || cat.state() == 0x11) {
        const float scale = (o.pulseMs * kPulseAmount) * 0.00555555569f + 1.0f;
        const float h = cat.heading();
        const bool left = 1.57079637f < h && h < 4.71238899f;
        const CatPictures look = catPictures(cat.type());
        const std::string file = picture == game::Cat::Picture::Common ? look.common
                               : picture == game::Cat::Picture::Idle   ? look.idleOneFacing() : look.walk + "1.png";
        if (const platform::Texture* sheet = texture(file))
            drawSprite(*sheet, sprite.frame(), 0, sprite.frameWidth(), sprite.frameHeight(), cat.position(), scale,
                       left, 0.0f, {});
        return;
    }
    int row = cat.facing();
    if (row < 0) return;  // (before its first facing the original indexes outside its pictures)
    bool mirror = false;
    if (row > 4) {
        mirror = true;
        row = 8 - row;
    }
    // (The boss cat and the black cat sit in one picture whatever their facing.)
    const CatPictures look = catPictures(cat.type());
    if (picture == game::Cat::Picture::Idle && look.idleSingle) {
        if (const platform::Texture* sheet = texture(look.idle))
            drawSprite(*sheet, sprite.frame(), 0, sprite.frameWidth(), sprite.frameHeight(), cat.position(), 1.0f, false,
                       0.0f, {});
        return;
    }
    const std::string file = (picture == game::Cat::Picture::Idle ? look.idle : look.walk) + char('1' + row) + ".png";
    if (const platform::Texture* sheet = texture(file))
        drawSprite(*sheet, sprite.frame(), 0, sprite.frameWidth(), sprite.frameHeight(), cat.position(), 1.0f, mirror,
                   0.0f, {});
}

// ObjControllerCat::renderViewArc @0x1a4804: a fan from the cat, 150 long (the boss cat; the
// sight distance for others), across the view angle (145 degrees) about its heading in 5 degree
// steps, flat red (0.89, 0.16, 0.16) at 0.1; the boss cat's fades out between 2 and 3 s of play
// time (LevelState +0x34).
void LevelView::drawViewArc(const game::Session& session, const game::Cat& cat) {
    constexpr float kViewAngle = 145.0f, kStep = 5.0f;
    float alpha = float(session.progress().playTimeMs + -2000) * -0.00100000005f + 1.0f;
    alpha = std::min(1.0f, std::max(0.0f, alpha)) * 0.100000001f;
    if (!(alpha > 0.0f)) return;
    const float radius = 150.0f;
    const game::Vec2 centre = cat.position();
    std::vector<platform::Vertex> v;
    std::vector<int> idx;
    auto put = [&](game::Vec2 q) {
        v.push_back({q.x - canvas_.viewX, game::kScreenHeight - q.y, 0, 0, 0.889999986f, 0.159999996f, 0.159999996f, alpha});
    };
    put(centre);
    const float first = cat.heading() * 57.2957764f - kViewAngle * 0.5f;
    for (float a = 0.0f; a <= kViewAngle; a += kStep) {
        const float r = (first + a) * 0.0174532924f;
        put(centre + game::Vec2{std::cos(r), std::sin(r)} * radius);
    }
    for (int i = 1; i + 1 < int(v.size()); ++i) {
        idx.push_back(0);
        idx.push_back(i);
        idx.push_back(i + 1);
    }
    host_->drawTriangles(nullptr, v, idx);
}

// Path::renderTexture @0x11e05c, mode 1, for a cat's route: from the last point back to the
// first, a footprints.png dot (16 x 8, along the route) every 14 units, white at 0.7 and added
// to the picture; none within 12 of a point where the cat sits (Path::renderPathPoint draws
// sleepspot.png there).
void LevelView::drawCatRoute(const game::Cat& cat) {
    const auto& points = cat.route().points;
    const platform::Texture* dot = texture("sprites/footprints.png");
    if (points.empty() || !dot) return;
    // The image's width runs along the route. (Drawn as turned sprites: SDL's software
    // renderer copies a two-triangle quad that is an axis-aligned rectangle as a plain
    // rectangle and loses a quarter turn of its texture, run l21_watch.)
    auto quad = [&](game::Vec2 c, game::Vec2 d) {
        const float degrees = float(std::atan2(double(d.y), double(d.x))) * 57.2957764f;
        canvas_.drawSprite(*dot, 0, 0, float(dot->width()), float(dot->height()), c, 1.0f, false, degrees,
                           Tint{1.0f, 1.0f, 1.0f, 0.7f}, platform::DrawStyle::Add);
    };
    auto nearSleepSpot = [&](game::Vec2 c) {
        const auto near = [&](const game::CatRoute::Point& pt) {
            return pt.pauseSeconds >= 1 && (pt.position - c).lengthSquared() < 144.0f;
        };
        return near(points.front()) || near(points.back());
    };
    game::Vec2 c = points.back().position;
    float spacing = 14.0f;
    for (int i = int(points.size()) - 1; i >= 0; --i) {
        const game::Vec2 target = points[std::size_t(i)].position;
        for (;;) {
            game::Vec2 dir = target - c;
            const float len = dir.normalise(1.0f);
            if (len < spacing) {
                spacing -= len;
                c = target;
                break;
            }
            c += dir * spacing;
            spacing = 14.0f;
            if (nearSleepSpot(c)) continue;
            quad(c, dir);
        }
    }
}

// ObjControllerHideable::render @0x1b53f0 for home holes (types 0xf0..0xf3): in the low pass the
// hole, enlarged by its pulse ((pulse x 0.16) / 180 + 1) and mirrored when it faces left; in the
// high pass the bouncing arrow while the hole's opening timer runs.
void LevelView::drawHomeHole(const game::Session& session, const game::LevelObject& o, int pass) {
    constexpr float kPulseAmount = 0.16f;  // Object+0x30
    if (pass == kPassLow) {
        // (A flare is drawn behind holes matching the mouse's key colour, Player+0x1e8; no key
        // holes on the reconstructed levels.)
        if (const platform::Texture* sheet = texture(o.sprite)) {
            const float scale = (o.pulseMs * kPulseAmount) * 0.00555555569f + 1.0f;
            drawSprite(*sheet, o.anim.frame(), 0, o.size.x, o.size.y, o.position, scale, false, 0.0f, {});
        }
    } else if (pass == kPassHigh && o.timerMs >= 1) {
        drawBouncyArrow(o);
    }
}

// Object::renderBouncyArrowOverObject @0x112110 (t = the hole's timer, counting down from 5000):
// 35 units above the object, bobbing 5 units (sin(0.009 t)), fading in over its first 600 ms
// and out over its last second; below the object, upside down, near the top of the screen.
void LevelView::drawBouncyArrow(const game::LevelObject& o) {
    const platform::Texture* arrow = texture("sprites/arrow.png");
    if (!arrow) return;
    const float t = float(o.timerMs);
    const float side = (game::kScreenHeight - o.position.y) < 64.0f ? -1.0f : 1.0f;
    const float lift = side * 35.0f + std::sin(t * 0.009f) * side * 5.0f;
    float alpha = t < 1000.0f ? t * 0.001f : 1.0f;
    if (t > 4400.0f) alpha = float(5000 - o.timerMs) * 0.00166666671f;
    platform::DrawStyle style;
    style.a = clamp01(alpha);
    style.flipY = side < 0.0f;
    const float w = float(arrow->width()), h = float(arrow->height());
    const game::Vec2 c{o.position.x, o.position.y + lift};
    host_->draw(*arrow, 0, 0, w, h, c.x - canvas_.viewX - w * 0.5f, game::kScreenHeight - c.y - h * 0.5f, w, h, style);
}

// ObjControllerPickups::render @0x1b88b8 for cheese: its current frame (carried cheese faces
// along the trail), enlarged by its pulse; while the trail is full, loose cheese is drawn from
// the grey sheet (frame 10 - type). The sweeping sheen over loose cheese is not drawn yet.
void LevelView::drawCheese(const game::Session& session, const game::LevelObject& o) {
    if (o.state == game::ObjectState::Hidden) return;
    // The burst behind it while its sparkle plays (timer -2): cheesetrail_effect, 0.4 of the
    // cheese's height up.
    if (o.timerMs == -2)
        if (const platform::Texture* burst = texture("sprites/cheesetrail_effect.png"))
            drawSprite(*burst, o.sparkle.frame(), 0, o.sparkle.frameWidth(), o.sparkle.frameHeight(),
                       {o.position.x, o.position.y + o.size.y * 0.4f}, 1.0f, false, 0.0f, {});
    constexpr float kPulseAmount = 0.16f;
    // A cheese waiting for its turn after the intro stays at its normal size.
    const float scale = o.pulseDelayMs > 0 ? 1.0f : (o.pulseMs * kPulseAmount) * 0.00555555569f + 1.0f;
    const bool full = !o.inTrail && session.carriedPickups() >= session.strength();
    if (full) {
        if (const platform::Texture* grey = texture("sprites/grey_pickups.png")) {
            const float fw = float(grey->height());
            drawSprite(*grey, 10 - o.type, 0, fw, fw, o.position, scale, false, 0.0f, {});
            return;
        }
    }
    if (const platform::Texture* sheet = texture(o.sprite))
        drawSprite(*sheet, o.anim.frame(), 0, o.size.x, o.size.y, o.position, scale, false, 0.0f, {});
    if (!o.inTrail) drawSheen(session, o, scale);
}

// ObjControllerPickups::render @0x1b8930 for twine and catnip: loose and with the trail full,
// the grey picture (grey_pickups.png frame 7 for twine, 6 for catnip); else its picture grown
// while it pulses, with the sheen while loose. (Twine let go, state 0x1e, shows neither grey nor
// sheen; catnip fading out, 0x21, is drawn at its time's share of alpha: not reached yet.)
void LevelView::drawLure(const game::Session& session, const game::LevelObject& o) {
    if (o.state == game::ObjectState::Hidden) return;
    const float scale = (o.pulseMs * 0.16f) * 0.00555555569f + 1.0f;
    const bool letGo = o.state == game::ObjectState(0x22) && !o.inTrail;
    const bool full = !o.inTrail && !letGo && session.carriedPickups() >= session.strength();
    if (full) {
        if (const platform::Texture* grey = texture("sprites/grey_pickups.png")) {
            const float fw = float(grey->height());
            drawSprite(*grey, o.type == game::kTwine ? 7 : 6, 0, fw, fw, o.position, scale, false, 0.0f, {});
            return;
        }
    }
    if (const platform::Texture* sheet = texture(o.sprite))
        drawSprite(*sheet, o.anim.frame(), 0, o.size.x, o.size.y, o.position, scale, false, 0.0f, {});
    if (!o.inTrail && !letGo) drawSheen(session, o, scale);
}

// Object::renderScreenGlowySheen @0x112418 (from ObjControllerPickups::render @0x1b91a2 for loose
// cheese): near the sweep (SceneGame +0x138fc) the cheese is drawn again, added to the screen,
// with its colour multiplied by pickup_sheen.png (a grey band, texture unit 1 sliding across the
// cheese by 1 - 2 c / w, c the sweep's way into the cheese clamped to its width) and its alpha
// squared (combiner alpha from the previous stage twice) times 1 - |sweep - x| / (2.5 w). The
// port composes that image on the CPU.
void LevelView::drawSheen(const game::Session& session, const game::LevelObject& o, float scale) {
    const float w = o.size.x, h = o.size.y;
    if (w <= 0.0f || h <= 0.0f) return;
    const float sweep = session.sheenX();
    const float a = std::max(0.0f, std::fabs(sweep - o.position.x) / (w * -2.5f) + 1.0f);
    if (a <= 0.0f) return;
    const platform::Image* sheet = image(o.sprite);
    const platform::Image* band = image("pickup_sheen.png");
    if (!sheet || !band || band->width <= 0) return;
    const float left = o.position.x - w * 0.5f;
    const float c = std::min(w, std::max(0.0f, sweep - left));
    const float shift = (c / w) * -2.0f + 1.0f;
    const int fw = int(w), fh = int(h), frame = o.anim.frame();
    if (fw <= 0 || fh <= 0 || (frame + 1) * fw > sheet->width || fh > sheet->height) return;
    platform::Image out;
    out.width = fw;
    out.height = fh;
    out.rgba.resize(std::size_t(fw) * std::size_t(fh) * 4);
    for (int x = 0; x < fw; ++x) {
        // Texture coordinates wrap (GL_REPEAT, the default).
        float u = (float(x) + 0.5f) / float(fw) + shift;
        u -= std::floor(u);
        const int bx = std::min(band->width - 1, int(u * float(band->width)));
        const float g = float(band->rgba[std::size_t(bx) * 4]) / 255.0f;
        for (int y = 0; y < fh; ++y) {
            const std::uint8_t* src =
                &sheet->rgba[(std::size_t(y) * std::size_t(sheet->width) + std::size_t(frame * fw + x)) * 4];
            std::uint8_t* dst = &out.rgba[(std::size_t(y) * std::size_t(fw) + std::size_t(x)) * 4];
            const float alpha = float(src[3]) / 255.0f * a;
            dst[0] = std::uint8_t(std::lround(float(src[0]) * g));
            dst[1] = std::uint8_t(std::lround(float(src[1]) * g));
            dst[2] = std::uint8_t(std::lround(float(src[2]) * g));
            dst[3] = std::uint8_t(std::lround(alpha * alpha * 255.0f));
        }
    }
    if (sheenUsed_ >= sheenFrames_.size()) sheenFrames_.resize(sheenUsed_ + 1);
    platform::Texture& t = sheenFrames_[sheenUsed_++];
    t = host_->createTexture(out);
    if (!t) return;
    platform::DrawStyle style;
    style.blend = platform::DrawStyle::Add;
    const float dw = w * scale, dh = h * scale;
    host_->draw(t, 0, 0, w, h, o.position.x - canvas_.viewX - dw * 0.5f, game::kScreenHeight - o.position.y - dh * 0.5f, dw, dh,
                style);
}

const platform::Image* LevelView::image(const std::string& relative) { return canvas_.image(relative); }

// The mouse (while hidden, its drawn copy slides into the hole and shrinks) and its path.
void LevelView::drawMouse(const game::Session& session) {
    const game::Mouse& mouse = session.mouse();
    // Player::render @0x122a60: red while the speed cheese lasts, flashing white in its last
    // stretch (below the flashing mark, a tenth of the mark per flash).
    Tint tint;
    if (mouse.chilliMs >= 1) {
        tint = {1.0f, 0.305000007f, 0.305000007f, 1.0f};
        const float half = float(int(float(mouse.chilliTotalMs) * 0.5f));
        float m = half * 0.5f;
        if (float(mouse.chilliMs) > m) m = half;
        const int mark = int(m);
        if (mouse.chilliMs < mark) {
            const int period = int(float(mark) * 0.100000001f);
            if (period >= 1 && mouse.chilliMs % period < (period >> 1)) tint = {};
        }
    }
    if (mouse.cup >= 0) {
        // Player::render @0x1225f6: a worn teacup shows in the mouse's place (sprites/teacup01..08
        // by its facing, Player +0x2b0) except in the bare half of its flashing.
        const game::LevelObject& cup = session.objects()[std::size_t(mouse.cup)];
        if (game::teacupFlashPhase(cup.timerMs, cup.playMs) != -1) {
            char file[32];
            std::snprintf(file, sizeof file, mouse.bucket ? "sprites/bucket%02d.png" : "sprites/teacup%02d.png",
                          game::spriteDirection(mouse.heading, 8) + 1);
            const float size = mouse.bucket ? 49.0f : 34.0f;
            if (const platform::Texture* t = texture(file))
                canvas_.drawSprite(*t, mouse.cupSprite.frame(), 0, size, size, mouse.position, mouse.drawScale, false,
                                   0.0f, tint);
            return;
        }
    }
    drawMouseFigure(canvas_, mouse, mouse.state == 2 ? mouse.slidePosition : mouse.position, tint);
}

void LevelView::drawPath(platform::Host& host, const game::Session& session) {
    drawMousePath(canvas_, session.path(), session.mouse());
}

}  // namespace doc::render
