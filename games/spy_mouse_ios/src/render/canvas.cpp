#include "render/canvas.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace doc::render {
namespace {

constexpr float kMouseFrame = 37.0f;  // player_run.png: 4 frames x 16 directions of 37x37

}  // namespace

const platform::Texture* Canvas::texture(const std::string& relative) {
    if (relative.empty()) return nullptr;
    auto it = textures_.find(relative);
    if (it != textures_.end()) return it->second ? &it->second : nullptr;
    platform::Texture t;
    if (const platform::Image* picture = image(relative)) t = host_->createTexture(*picture);
    auto& slot = textures_[relative] = std::move(t);
    return slot ? &slot : nullptr;
}

const platform::Image* Canvas::image(const std::string& relative) {
    auto it = images_.find(relative);
    if (it != images_.end()) return it->second ? &*it->second : nullptr;
    std::optional<platform::Image> decoded;
    if (auto bytes = files_->readResource(relative)) decoded = platform::decodeImage(*bytes);
    auto& slot = images_[relative] = std::move(decoded);
    return slot ? &*slot : nullptr;
}

void Canvas::drawSprite(const platform::Texture& sheet, int frame, int row, float frameW, float frameH,
                        game::Vec2 centre, float scale, bool flip, float degrees, const Tint& tint,
                        platform::DrawStyle::Blend blend) {
    const float w = frameW * scale, h = frameH * scale;
    if (w <= 0.0f || h <= 0.0f) return;
    platform::DrawStyle style;
    style.flipX = flip;
    style.degrees = -degrees;  // map space is y-up, the screen y-down
    style.r = tint.r;
    style.g = tint.g;
    style.b = tint.b;
    style.a = tint.a;
    style.blend = blend;
    host_->draw(sheet, float(frame) * frameW, float(row) * frameH, frameW, frameH, centre.x - viewX - w * 0.5f,
                game::kScreenHeight - centre.y - h * 0.5f, w, h, style);
}

void Canvas::drawSprite2(const platform::Texture& sheet, int frame, int row, float frameW, float frameH,
                         game::Vec2 centre, float scaleX, float scaleY, float degrees, const Tint& tint) {
    const float w = frameW * scaleX, h = frameH * scaleY;
    if (w <= 0.0f || h <= 0.0f) return;
    platform::DrawStyle style;
    style.degrees = -degrees;
    style.r = tint.r;
    style.g = tint.g;
    style.b = tint.b;
    style.a = tint.a;
    host_->draw(sheet, float(frame) * frameW, float(row) * frameH, frameW, frameH, centre.x - viewX - w * 0.5f,
                game::kScreenHeight - centre.y - h * 0.5f, w, h, style);
}

void drawMouseFigure(Canvas& canvas, const game::Mouse& mouse, game::Vec2 at, const Tint& tint) {
    const game::SpriteAnim& sprite = mouse.sprite;
    const data::AnimSequence* seq = sprite.sequence(sprite.currentSequence());
    const int seqFrame = sprite.sequenceFrame();
    if (seq && seqFrame >= 0 && seqFrame < int(seq->frames.size())) {
        if (const platform::Texture* sheet = canvas.texture(seq->image)) {
            const data::SequenceFrame& f = seq->frames[std::size_t(seqFrame)];
            canvas.drawSprite(*sheet, f.column, f.row + sprite.sequenceRow(), float(seq->frameWidth),
                              float(seq->frameHeight), {at.x + f.offsetX, at.y + f.offsetY}, mouse.drawScale, false,
                              0.0f, tint);
            return;
        }
    }
    if (const platform::Texture* run = canvas.texture("sprites/player_run.png"))
        canvas.drawSprite(*run, sprite.frame() & 3, game::spriteDirection(mouse.heading, 16), kMouseFrame, kMouseFrame,
                          at, mouse.drawScale, false, 0.0f, tint);
}

// A quad every 9.5 units, 8 along x 13 across, showing the dash cell of sprites/line.png
// (cells from the bottom: dot, arrow, dash). The end piece is twice as long and 1.2 times as
// wide: the arrow cell when the path snapped onto something, else the dot cell; the next piece
// follows 11.5 units on. Colour: (1, 0.84, 0.3) while the snap glow is above 625 ms, fading to
// white over the last 625 ms; alpha 0.9.
void drawMousePath(Canvas& canvas, const game::DrawnPath& path, const game::Mouse& mouse) {
    const int start = mouse.pathIndex;
    const platform::Texture* line = canvas.texture("sprites/line.png");
    if (path.size() < 1 || !line || start > path.size() - 1) return;
    float r = 1.0f, g = 1.0f, b = 1.0f;
    if (mouse.snapGlowMs >= 1) {
        float x = float(390625 - mouse.snapGlowMs * mouse.snapGlowMs) * 2.56e-6f;
        if (x < 0.0f) x = 0.0f;
        g = std::min(1.0f, x + 0.84f);
        b = std::min(1.0f, x + 0.3f);
    }
    const float a = 0.9f;
    const float cell = 1.0f / 3.0f;
    std::vector<platform::Vertex> v;
    std::vector<int> idx;
    auto quad = [&](game::Vec2 c, game::Vec2 d, game::Vec2 p, float v0) {
        const float v1 = v0 + cell;
        const int base = int(v.size());
        auto put = [&](game::Vec2 q, float u, float tv) {
            // The original's textures are bottom-up (v = 0 is the image's last row).
            v.push_back({q.x - canvas.viewX, game::kScreenHeight - q.y, u, 1.0f - tv, r, g, b, a});
        };
        put(c + d + p, 1, v1);
        put(c - d - p, 0, v0);
        put(c + d - p, 0, v1);
        put(c - d + p, 1, v0);
        for (int k : {0, 1, 2, 0, 3, 1}) idx.push_back(base + k);
    };
    game::Vec2 c{path[path.size() - 1].x, path[path.size() - 1].y};
    float spacing = 9.5f;
    bool first = true;
    for (int i = path.size() - 1; i >= start; --i) {
        const game::Vec2 target{path[i].x, path[i].y};
        for (;;) {
            game::Vec2 dir = target - c;
            const float len = dir.normalise(1.0f);
            if (len < spacing) {
                spacing -= len;
                c = target;
                break;
            }
            c += dir * spacing;
            game::Vec2 perp{-dir.y, dir.x};
            dir = dir * 4.0f;
            perp = perp * 6.5f;
            float v0 = 2.0f * cell;
            spacing = 9.5f;
            if (first) {
                first = false;
                dir = dir * 2.0f;
                perp = perp * 1.2f;
                v0 = path.snapped ? cell : 0.0f;
                spacing = 11.5f;
            }
            quad(c, dir, perp, v0);
        }
    }
    canvas.host().drawTriangles(line, v, idx);
}

// setPinholeType @0x1f0f6c: kind 0 the menu pattern, kind 5 the bonus pattern, both with a round
// hole; kind 2 (the level lost to a cat) the death pattern with a hole in the shape of a cat's
// head. The pattern scrolls diagonally with the engine clock.
void IrisPainter::draw(Canvas& canvas, int kind, game::Vec2 centre, float radius, bool shut, int phase) {
    // Kind 1: a star over the bonus pattern (doors), 3: the mouse's silhouette, 4: tonsils.
    const char* patternFile = (kind == 5 || kind == 1) ? "ui/bg_bonus.png"
                              : (kind == 2 || kind == 4) ? "ui/bg_death.png" : "ui/bg_pattern.png";
    const char* holeFile = kind == 1 ? "pin_star.png" : kind == 2 ? "cat_silhoutte.png" : kind == 3 ? "mouse_silhoutte.png"
                           : kind == 4 ? "pin_tonsills.png" : "pin_hole.png";
    const platform::Image* patternImage = canvas.image(patternFile);
    const platform::Image* holeImage = canvas.image(holeFile);
    if (!patternImage || !holeImage || patternImage->width <= 0 || holeImage->width <= 0) return;
    const platform::Image& pattern = *patternImage;
    const platform::Image& hole = *holeImage;
    const int w = int(game::kScreenWidth), h = int(game::kScreenHeight);
    float r = std::max(0.0f, radius);
    if (shut) r = 0.0f;
    const float left = centre.x - r, top = (game::kScreenHeight - centre.y) - r, size = 2.0f * r;
    const float shift = float(phase & 0xff) / 255.0f;
    const float shiftX = shift * float(pattern.width), shiftY = shift * float(pattern.height);
    auto holeAlpha = [&](float u, float v) {  // bilinear, u and v in 0..1
        const float fx = u * float(hole.width) - 0.5f, fy = v * float(hole.height) - 0.5f;
        const int x0 = int(std::floor(fx)), y0 = int(std::floor(fy));
        const float ax = fx - float(x0), ay = fy - float(y0);
        auto at = [&](int x, int y) {
            x = std::clamp(x, 0, hole.width - 1);
            y = std::clamp(y, 0, hole.height - 1);
            return float(hole.rgba[(std::size_t(y) * std::size_t(hole.width) + std::size_t(x)) * 4 + 3]) / 255.0f;
        };
        return (at(x0, y0) * (1 - ax) + at(x0 + 1, y0) * ax) * (1 - ay) +
               (at(x0, y0 + 1) * (1 - ax) + at(x0 + 1, y0 + 1) * ax) * ay;
    };
    platform::Image frame;
    frame.width = w;
    frame.height = h;
    frame.rgba.resize(std::size_t(w) * std::size_t(h) * 4);
    for (int y = 0; y < h; ++y) {
        const float glY = game::kScreenHeight - (float(y) + 0.5f);
        // Texture rows count from the bottom.
        const int fromBottom = int(std::floor(std::fmod(glY + shiftY, float(pattern.height)) + float(pattern.height))) %
                               pattern.height;
        const int row = pattern.height - 1 - fromBottom;
        for (int x = 0; x < w; ++x) {
            const int col = int(std::floor(std::fmod(float(x) + 0.5f - shiftX, float(pattern.width)) +
                                           float(pattern.width))) % pattern.width;
            const std::uint8_t* src = &pattern.rgba[(std::size_t(row) * std::size_t(pattern.width) + std::size_t(col)) * 4];
            std::uint8_t* dst = &frame.rgba[(std::size_t(y) * std::size_t(w) + std::size_t(x)) * 4];
            float weight = 1.0f;
            const float u = (float(x) + 0.5f - left) / size, v = (float(y) + 0.5f - top) / size;
            if (r > 0.0f && u >= 0 && u < 1 && v >= 0 && v < 1) {
                const float a = holeAlpha(u, v);
                weight = std::clamp(2.0f * a - a * a, 0.0f, 1.0f);
            }
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
            dst[3] = std::uint8_t(std::lround(weight * 255.0f));
        }
    }
    frame_ = canvas.host().createTexture(frame);
    if (frame_) canvas.host().draw(frame_, 0, 0, float(w), float(h), 0, 0, float(w), float(h));
}

}  // namespace doc::render
