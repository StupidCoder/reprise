#include "game/particles.hpp"

#include "game/random.hpp"
#include "game/tuning.hpp"

#include <algorithm>
#include <cmath>

namespace doc::game {

Particles::Particles(GameRandom& random, std::vector<ParticleKind> kinds) : random_(random), kinds_(std::move(kinds)) {
    kinds_.resize(kSpriteParticleTypes);
}

const ParticleKind& Particles::kind(int type) const { return kinds_[type < kSpriteParticleTypes ? type : 0x34]; }

int Particles::priorityOf(int type) const { return type < kSpriteParticleTypes ? kinds_[type].priority : 100; }

void Particles::clear() {
    for (Particle& p : slots_) p.active = false;
}

bool Particles::onScreen(const Particle& p) const {
    // Particles::isParticleOnScreen @0x1383dc.
    const float left = float(int(-viewOffset.x));
    if (p.position.x < left) return false;
    const float right = float(int(viewOffset.x + 480.0f));
    if (p.position.x > right) return false;
    const float bottom = float(int(-viewOffset.y));
    if (p.position.y < bottom) return false;
    const float top = float(int(viewOffset.y + 320.0f));
    return p.position.y <= top;
}

int Particles::add(int type, int floor, Vec2 start, const std::string& text, int* owner, int delayMs, Vec2 direction,
                   bool clipText) {
    // A slot: the owner's own, else the first free one, else the first holding a particle of
    // lower priority (never for types of priority 0).
    int slot = owner && *owner != -1 ? *owner : -1;
    if (slot < 0) {
        slot = 0;
        while (slot < kSlots && slots_[slot].active) ++slot;
    }
    if (slot == kSlots) {
        const int priority = priorityOf(type);
        if (type < kSpriteParticleTypes && priority == 0) return -1;
        slot = 0;
        while (slot < kSlots && !(priorityOf(slots_[slot].type) < priority)) ++slot;
        if (slot == kSlots) return -1;
    }
    (void)clipText;  // keeping texts on screen needs the font's metrics (drawing, not reconstructed)

    Particle& p = slots_[slot];
    p.active = true;
    p.type = type;
    p.floor = floor;
    p.position = start;
    p.velocity = {};
    p.scale = 1.0f;
    p.alpha = 1.0f;
    p.phase = 0;
    p.ageMs = 0;
    p.life = 0;
    p.rotation = 0.0f;
    p.red = p.green = p.blue = 1.0f;
    p.owner = owner;
    if (owner) *owner = slot;
    p.flag = false;
    p.delayMs = delayMs;
    if (type == 0x38 && delayMs == 0) ++textPops_;
    if (type < kSpriteParticleTypes) {
        const ParticleKind& k = kinds_[type];
        p.sprite.setSheet(k.sheetSize.width, k.sheetSize.height, k.frameWidth * textureScale);
        p.text.clear();
    } else {
        p.text = text.size() < 0x40 ? text : std::string("Str Too Long!");
    }
    const int last = p.sprite.frameCount() - 1;
    auto snapToPixel = [&p] {
        p.position.x = float(int(p.position.x));
        p.position.y = float(int(p.position.y));
    };
    // Snow, sparks and blossoms: a slow drift with a random sway (types 0x14, 0x29, 0x2a..0x2c).
    auto driftStart = [&](float lifeMin, float lifeMax) {
        p.velocity = {random_.within(-0.01f, 0.0f), 0.09f};
        const float x = randomWithinFrom(0.001f, 0.0f, 4) + -0.05f;
        p.velocity = {x, random_.within(0.05f, 0.09f)};
        p.life = int(random_.within(lifeMin, lifeMax));
        p.scale = 0.011f;
    };

    switch (type) {
    case 0x00: case 0x0E:
        p.sprite.setAnim(0, last, SpriteAnim::Loop, 150);
        p.life = 2000;
        p.aux = 5000;
        break;
    case 0x01: case 0x02:
        p.sprite.setAnim(0, last, SpriteAnim::Once, 120);
        p.life = 2000;
        p.aux = 5000;
        break;
    case 0x03: p.sprite.setAnim(0, last, SpriteAnim::Once, 80); break;
    case 0x04:
        p.sprite.setAnim(0, last, SpriteAnim::Loop, 200);
        p.position.x = p.position.x + 9.0f;
        p.position.y = p.position.y + -2.0f;
        p.life = 2000;
        p.aux = 5000;
        break;
    case 0x05: case 0x06: case 0x07: case 0x08: case 0x0F: case 0x10:
        snapToPixel();
        p.sprite.setAnim(0, last, SpriteAnim::Loop, 200);
        p.position.y = p.position.y + 16.0f;
        break;
    case 0x09:
        snapToPixel();
        p.sprite.setAnim(0, last, SpriteAnim::Once, 150);
        p.position.y = p.position.y + 16.0f;
        break;
    case 0x0A: {
        p.sprite.setAnim(0, last, SpriteAnim::Loop, 200);
        p.alpha = 1.0f;
        p.position.x = p.position.x + float((random_.next() & 0x3FF) - 0x200) * 0.02f;
        const float y = p.position.y;
        const float offset = float((random_.next() & 0x3FF) - 0x200) * 0.0117647061f;
        p.position.y = float((double(y) + double(offset)) + 12.0);
        break;
    }
    case 0x0B: {
        p.sprite.setAnim(0, last, SpriteAnim::Loop, 200);
        p.alpha = 0.25f;
        const int x = int(p.position.x);
        p.position.x = float(x + random_.next() % 5 - 2);
        p.position.y = float(int(p.position.y));
        p.position.y = p.position.y + 8.0f;
        break;
    }
    case 0x0D: p.scale = 0.65f; break;
    case 0x11: p.sprite.setAnim(0, last, SpriteAnim::Once, 70); break;
    case 0x12:
        p.alpha = 0.5f;
        p.sprite.setAnim(0, last, SpriteAnim::Once, 50);
        break;
    case 0x13:
        p.sprite.setAnim(0, last, SpriteAnim::Loop, 200);
        p.alpha = 1.0f;
        p.velocity = {1.0f, 0.0f};
        p.velocity.rotateDegrees(float(random_.next() % 360));
        p.flag = true;
        break;
    case 0x14: case 0x2A: case 0x2B: case 0x2C:
        driftStart(2000.0f, 3000.0f);
        p.sprite.setAnim(0, last, SpriteAnim::Loop, int(random_.within(130.0f, 220.0f)));
        break;
    case 0x29:
        driftStart(1000.0f, 2000.0f);
        p.sprite.setAnim(0, last, SpriteAnim::Loop, 150);
        break;
    case 0x15:
        p.sprite.setAnim(0, last, SpriteAnim::Once, 70);
        p.flag = true;
        break;
    case 0x16:
        p.velocity = p.position;  // remembers where it rose from
        p.alpha = 0.0f;
        p.aux = 1;
        break;
    case 0x17: case 0x18: case 0x19: case 0x1A: {
        // A firework rocket: its burst (type + 4) starts now and the rocket follows it.
        const int burst = add(type + 4, floor, start, {}, nullptr, 0, {-1.0f, -1.0f}, true);
        if (burst == -1) {
            p.active = false;
            break;
        }
        const Particle& b = slots_[burst];
        p.alpha = b.alpha;
        p.velocity = b.velocity;
        p.phase = b.phase;
        p.life = b.life;
        p.sprite.setAnim(0, -1, SpriteAnim::Hold, 200);
        break;
    }
    case 0x1B: case 0x1C: case 0x1D: case 0x1E:
        p.sprite.setAnim(2, -1, SpriteAnim::Hold, 200);
        p.alpha = 0.05f;
        p.velocity = {0.0f, 0.9f};
        p.velocity.rotateDegrees(float(((random_.next() >> 4) & 0x3F) - 31));
        p.life = ((random_.next() >> 4) & 0x7FF) + 700;
        p.phase = 0;
        break;
    case 0x1F:
        p.velocity = {0.0f, 0.6f};
        p.flag = true;
        break;
    case 0x20: case 0x21: {
        // HUD stars bursting from the score panel (left: 0x20, right: 0x21).
        const float shift = float(random_.next() % 120);
        p.position.x = type == 0x20 ? p.position.x + shift : p.position.x - shift;
        p.life = random_.next();  // used as the random value of the twinkle
        p.velocity = {0.0f, -1.5f};
        p.velocity.rotateDegrees(float(random_.next() % 30) + (type == 0x20 ? 45.0f : 315.0f));
        p.flag = true;
        break;
    }
    case 0x22: {
        // Sparkles from cheese just picked up.
        p.life = random_.next();
        const float speed = random_.within(0.1f, 5.0f);
        p.velocity = {0.0f, speed * -1.5f};
        p.velocity.rotateDegrees(float(random_.next() % 360));
        p.flag = false;
        break;
    }
    case 0x23: {
        p.sprite.setAnim(0, 2, SpriteAnim::PingPong, 100);
        p.alpha = 0.05f;
        p.velocity = {0.0f, 1.5f};
        p.position.x = p.position.x + float(((random_.next() >> 5) & 0x3F) - 31);
        random_.next();  // drawn and not used
        p.life = 1400;
        // Its glow follows it.
        p.aux = add(0x24, floor, p.position, {}, nullptr, 0, {-1.0f, -1.0f}, true);
        if (p.aux == -1) p.active = false;
        break;
    }
    case 0x24:
        p.sprite.setAnim(0, last, SpriteAnim::Loop, 100);
        p.alpha = 0.0f;
        break;
    case 0x25:
        p.sprite.setAnim(0, 2, SpriteAnim::PingPong, 100);
        p.alpha = 0.05f;
        p.life = 1400;
        p.aux = -1;
        break;
    case 0x27:
        p.sprite.setAnim(0, last, SpriteAnim::Once, 150);
        p.velocity = {0.0f, 0.9f};
        p.life = 1200;
        break;
    case 0x28:
        p.sprite.setAnim(0, -1, SpriteAnim::Hold, 200);
        p.velocity = {0.0f, 0.9f};
        p.life = 600;
        break;
    case 0x2D:
        p.flag = true;
        p.sprite.setAnim(0, last, SpriteAnim::Loop, 150);
        break;
    case 0x2E: p.sprite.setAnim(0, last, SpriteAnim::Once, 115); break;
    case 0x2F: {
        // A spark flying off within 80 degrees either side of `direction`.
        p.sprite.setAnim(0, -1, SpriteAnim::Hold, 200);
        const int side = random_.next() % 2;
        const int spread = random_.next() % 40;
        Vec2 dir = direction;
        dir.rotateDegrees(float(side == 1 ? spread - 80 : 80 - spread));
        p.rotation = dir.angleDegrees();
        p.velocity = dir * random_.within(0.5f, 1.0f);
        p.life = int(random_.within(50.0f, 150.0f));
        p.flag = true;
        break;
    }
    case 0x30: {
        p.sprite.setAnim(0, -1, SpriteAnim::Hold, 200);
        p.alpha = 0.5f;
        const float vx = random_.within(-1.5f, -2.5f);
        const float vy = random_.within(0.3f, 0.5f);
        p.velocity = {vx, vy};
        p.life = random_.next() % 15 + 10;
        break;
    }
    case 0x31: p.flag = true; break;
    case 0x32: {
        const int first = p.sprite.frameCount() >= 2 ? random_.next() % (p.sprite.frameCount() - 1) : 0;
        p.sprite.setAnim(first, -1, SpriteAnim::Hold, 200);
        p.flag = true;
        p.scale = 0.65f;
        const int a = random_.next();
        const int b = random_.next();
        p.position.x = p.position.x + float(a & 5);
        p.position.y = p.position.y + float(b & 5);
        break;
    }
    case 0x33:
        p.alpha = 0.0f;
        p.life = random_.next() % 3;  // which of three sway patterns
        p.position.x = p.position.x + random_.within(-8.0f, 8.0f) * textureScale;
        p.position.y = p.position.y + 10.0f * textureScale;
        p.sprite.setAnim(0, -1, SpriteAnim::Hold, 200);
        p.velocity.x = p.position.x;
        break;
    case 0x34: {
        // A bug crawling away from the nearest screen edge.
        p.life = random_.next() % 2500;
        p.flag = true;
        p.scale = 1.0f;
        const float halfX = viewOffset.x * 0.5f, halfY = viewOffset.y * 0.5f;
        const float toLeft = start.x - halfX, toRight = (halfX + 480.0f) - start.x;
        const float toBottom = start.y - halfY, toTop = (halfY + 320.0f) - start.y;
        float dx = std::fmin(toRight, toLeft);
        float dy = std::fmin(toTop, toBottom);
        if (dx < dy) {
            if (start.x < 240.0f) dx = -dx;
            p.velocity = {dx, 0.0f};
        } else {
            if (start.y < 160.0f) dy = -dy;
            p.velocity = {0.0f, dy};
        }
        p.velocity.normalise(0.01f);
        p.velocity.rotateDegrees(random_.within(-60.0f, 60.0f));
        p.sprite.setAnim(0, last, SpriteAnim::Loop, 200);
        break;
    }
    case 0x36: case 0x3A: case 0x3B:
        if (type == 0x3B) p.velocity.y = 0.2f;
        p.alpha = 0.2f;
        p.scale = 0.4f;
        p.life = 1200;
        break;
    case 0x38: p.alpha = 0.0f; break;
    default: break;
    }
    return slot;
}

void Particles::update(int dtMs) {
    const float dt = float(dtMs);
    const double dtD = double(dtMs);
    // Per-tick steps the original computes once before the loop.
    const float fade00075 = dt * 0.00075f, grow0006 = dt * 0.006f, rise0009 = dt * 0.009f;
    const float spin012 = dt * 0.12f, shrink066 = dt * -0.000666666660f, shrink015 = dt * -0.000150000007f;
    const float move0005 = dt * 0.005f, move04 = dt * 0.4f, fade0025 = dt * 0.0025f;
    const float shrink0008 = dt * -0.00079999998f, move001 = dt * 0.01f, move01 = dt * 0.1f;
    const float rise0025 = dt * 0.025f, move002 = dt * 0.02f;

    for (int i = 0; i < kSlots; ++i) {
        Particle& p = slots_[i];
        if (!p.active) continue;
        if (p.delayMs >= 1) {
            p.delayMs -= dtMs;
            if (p.delayMs > -1) continue;
            p.delayMs = 0;
            if (p.type == 0x38) ++textPops_;
        }
        bool kill = false;
        bool skipTail = false;
        switch (p.type) {
        case 0x00: case 0x01: case 0x02: case 0x04: case 0x0E:
            if (p.life <= 0) p.life = 2000;
            if (p.aux <= 0) p.aux = 5000;
            p.scale = float(p.aux - p.ageMs) / float(p.life);
            break;
        case 0x0D: p.rotation = p.rotation + move01; break;
        case 0x05: case 0x06: case 0x07: case 0x08:
            p.scale = (1500.0f - float(p.ageMs)) * 0.005f;
            break;
        case 0x0F: case 0x10:
            p.scale = (500.0f - float(p.ageMs)) * 0.005f;
            kill = p.sprite.finished();
            break;
        case 0x03: case 0x09: case 0x11: case 0x12: case 0x15: case 0x2E:
            kill = p.sprite.finished();
            break;
        case 0x0A:
            p.scale = (700.0f - float(p.ageMs)) * 0.00142857141f;
            p.position.y = p.position.y + move002;
            break;
        case 0x0B: {
            float sway = std::fmax(float(kTickMs), float(p.life)) / float(p.aux);
            if (p.aux <= 4000) sway = sway * 0.25f;
            else if (p.aux <= 8000) sway = sway * 0.5f;
            else if (p.aux <= 16000) sway = sway * 0.75f;
            const float span = (sway * 0.899999976f + move001) * 2500.0f;
            const float age = float(p.ageMs);
            float value = 0.0f;
            if (!(age > span)) {
                float t = age / span + -0.5f;
                t = t + t;
                value = (1.0f - std::fabs(t)) * 0.75f + 0.25f;
            }
            p.scale = p.alpha = value;
            p.position.x = p.position.x + std::sin(float(p.ageMs) * 0.0133333337f) * sway;
            p.position.y = p.position.y + rise0025;
            break;
        }
        case 0x13:
            p.scale = (700.0f - float(p.ageMs)) * 0.00142857141f;
            p.position += p.velocity * move001;
            break;
        case 0x22: {
            const float fade = (1000.0f - float(p.ageMs)) * 0.00100000005f;
            p.scale = fade;
            p.alpha = (randomWithinFrom(0.0f, 0.5f, p.life) + 0.800000012f) * fade;
            p.position += p.velocity * move002;
            break;
        }
        case 0x20: case 0x21: {
            const float fade = (2000.0f - float(p.ageMs)) * 0.000500000024f;
            p.scale = fade;
            p.alpha = (randomWithinFrom(0.0f, 0.5f, p.life) + 0.800000012f) * fade;
            p.position += p.velocity * move002;
            break;
        }
        case 0x16: {
            p.position.y = p.velocity.y + std::sin(float(p.ageMs) * 0.01f) * 5.0f;
            const int end = (p.aux < 0 ? 0x9882F7 : 0) + 5000;
            const int left = end - p.ageMs;
            if (left < 1000) p.scale = float(left) * 0.00100000005f;
            p.alpha = p.ageMs < 300 ? float(p.ageMs) * 0.00333333341f : 1.0f;
            kill = p.ageMs > end;
            break;
        }
        case 0x17: case 0x18: case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E:
            if (p.phase == 0) {
                const float wobble = random_.within(0.0f, 0.1f);
                p.position += p.velocity * (dt * (wobble + move001));
                p.scale = 1.0f;
                p.velocity *= 0.99000001f;
                if (p.ageMs > p.life) {
                    p.ageMs = 0;
                    ++p.phase;
                    p.life = ((random_.next() >> 4) & 0x1FF) + 1000;
                }
            } else {
                p.alpha = p.alpha + (dt / (p.alpha * float(p.life))) * 5.0f;
                p.velocity *= 0.959999979f;
                p.velocity.y = p.velocity.y + -0.00800000038f;
                p.position += p.velocity * move001;
                p.scale = ((p.type <= 0x1A ? 700.0f : 1400.0f) - float(p.ageMs)) * 0.00100000005f;
            }
            break;
        case 0x1F: {
            p.scale = p.scale + shrink0008;
            p.alpha = p.scale + 0.300000012f;
            p.position += (p.velocity * 0.05f) * dt;
            break;
        }
        case 0x23: case 0x25: {
            const Vec2 step = p.velocity * move01;
            p.position += step;
            if (p.type == 0x23) p.velocity.y = p.velocity.y + -0.0250000004f;
            p.scale = (float(p.life) - float(p.ageMs)) * 0.00333333341f;
            if (p.velocity.y < 0.0f && p.sprite.mode() == SpriteAnim::PingPong)
                p.sprite.setAnim(3, 5, SpriteAnim::Once, 100);
            if (p.alpha < 0.800000012f) p.alpha = p.alpha + fade0025;
            if (p.aux != -1) {
                // The glow (type 0x24) follows, fading in from below.
                Particle& glow = slots_[p.aux];
                glow.scale = float((p.life - 350) - p.ageMs) * 0.00333333341f + -0.300000012f;
                if (glow.scale < 0.01f) p.aux = -1;
                if (glow.alpha < 0.800000012f) {
                    glow.alpha = glow.alpha + step.y / glow.sprite.frameHeight();
                    glow.position += step * 0.5f;
                } else {
                    glow.alpha = 0.800000012f;
                    glow.position += step;
                }
            }
            if (p.scale > 0.0149999997f) {
                // A flicker on top of the fade.
                float scale = p.scale;
                if (scale > 1.0f) p.scale = scale = 1.0f;
                const float flicker = (std::sin(float(p.ageMs) * 0.0133333337f) + 1.0f) * -0.200000003f + -0.300000012f;
                p.scale = scale + flicker;
                if (p.scale < 0.0149999997f) p.scale = 0.0149999997f;
            }
            break;
        }
        case 0x27:
            p.position += p.velocity * move01;
            p.scale = (float(p.life) - float(p.ageMs)) * 0.00333333341f;
            break;
        case 0x28:
            p.position += p.velocity * move01;
            p.scale = (float(p.life) - float(p.ageMs)) * 0.00200000009f;
            break;
        case 0x14: case 0x2A: case 0x2B: case 0x2C:
            p.position += (p.velocity * dt) * 0.800000012f;
            if (int(p.position.y) > int(screenSize.y)) p.active = false;
            p.scale = 1.0f;
            break;
        case 0x29: {
            p.position += p.velocity * dt;
            p.ageMs += dtMs;
            if (p.ageMs >= p.life) p.active = false;
            if (p.ageMs < 500) p.scale = std::fmax(0.0109999999f, float(p.ageMs) * 0.00200000009f);
            const int left = p.life - p.ageMs;
            if (left < 800) p.scale = float(left) * 0.00124999997f;
            break;
        }
        case 0x2D: {
            const int left = 1600 - p.ageMs;
            if (left < 500) p.scale = float(left) * 0.00200000009f;
            break;
        }
        case 0x2F:
            p.scale = (float(p.life) - float(p.ageMs)) / float(p.life);
            p.position += p.velocity * move04;
            break;
        case 0x30: {
            const float age = float(p.ageMs);
            const float grow = (2000.0f - age) * 0.000500000024f;
            const float shrink = (4000.0f - age) * 0.000250000012f;
            const float alpha = float(double(grow) * -0.69999999999999996 + 1.0);
            p.scale = shrink;
            p.alpha = std::fmin(1.0f, alpha);
            p.position += p.velocity * move0005;
            p.rotation = float(double(p.rotation) + double(p.life) * dtD * 0.001);
            break;
        }
        case 0x31: p.scale = p.scale + shrink015; break;
        case 0x32:
            p.scale = p.scale + shrink066;
            p.alpha = std::fmax(0.0f, p.alpha + shrink066);
            p.rotation = p.rotation + spin012;
            break;
        case 0x33: {
            float amplitude = 5.0f, rise = 0.0399999991f, maxAlpha = 0.899999976f;
            if (p.life == 1) amplitude = -4.0f, maxAlpha = 1.10000002f, rise = 0.0500000007f;
            else if (p.life == 2) amplitude = 6.0f, maxAlpha = 1.0f, rise = 0.0599999987f;
            p.position.y = p.position.y + dt * rise;
            p.position.x = p.velocity.x + std::sin(float(p.ageMs) * 0.01f) * amplitude;
            if (p.alpha < maxAlpha) p.alpha = std::fmin(maxAlpha, p.alpha + move0005);
            if (p.ageMs > 600) kill = true;
            else if (p.ageMs >= 551) p.sprite.setAnim(2, -1, SpriteAnim::Hold, 200);
            else if (p.ageMs > 500) p.sprite.setAnim(1, -1, SpriteAnim::Hold, 200);
            break;
        }
        case 0x34: {
            p.life -= dtMs;
            if (p.life < 1) {
                p.life = int(random_.within(2000.0f, 6000.0f));
                p.velocity.rotateDegrees(random_.within(-15.0f, 15.0f));
            }
            p.rotation = p.velocity.angleDegrees() + -90.0f;
            p.position += p.velocity * dt;
            p.sprite.update(dtMs);
            kill = !onScreen(p);
            break;
        }
        case 0x36: case 0x3B: {
            const float life = float(p.life);
            p.scale = (life - float(p.ageMs)) / (life * 0.5f);
            p.alpha = std::fmin(1.0f, p.alpha + (p.flag ? 0.00875000004f : 0.0175000001f));
            p.position += p.velocity;
            break;
        }
        case 0x37:
            if (p.phase == 0) {
                p.position.y = p.position.y + rise0009;
                p.position.x = p.position.x + std::sin(float(p.ageMs) * 0.01f) * 0.25f;
                p.scale = std::fmin(1.0f, p.scale + grow0006);
                if (p.ageMs >= 700) p.phase = 1;
            } else if (p.phase == 1) {
                p.alpha = std::fmin(1.0f, p.alpha + fade00075);
                p.velocity = Vec2{viewLeft, 320.0f} - p.position;
                const float distanceSquared = p.velocity.lengthSquared();
                p.velocity.normalise(1.0f);
                const float near = distanceSquared * 9.95024893e-05f;
                p.scale = std::fmin(1.0f, near * near);
                p.position += (p.velocity * dt) * 0.400000006f;
                kill = p.position.y > 320.0f;
            }
            break;
        case 0x38: case 0x39: {
            const float t = float(p.ageMs) * 0.000500000024f;
            float scale = 1.0f;
            float alpha = std::fmin(t * 5.0f, 1.0f);
            if (t > 0.850000024f) {
                const float k = (t + -0.850000024f) * -6.66666794f + 1.0f;
                scale = k * k;
                alpha = (1.0f - scale) + alpha;
            }
            p.scale = scale;
            p.alpha = alpha;
            p.ageMs += dtMs;
            if (float(p.ageMs) >= 2000.0f) p.active = false;
            skipTail = true;
            break;
        }
        case 0x3A:
            if (p.alpha >= 0.5f) {
                p.alpha = 0.5f;
                const int shown = p.ageMs - p.life;
                p.scale = shown < p.aux ? 1.0f : float(shown - p.aux) * -0.000833333354f + 1.0f;
            } else {
                const float t = (float(p.ageMs) * 0.5f) / float(p.life);
                p.alpha = t;
                p.scale = t > move001 ? t + t : 0.200000003f;
                if (t > 0.5f) p.alpha = 0.5f;
            }
            break;
        default: break;
        }
        if (skipTail) continue;
        if (kill) p.scale = 0.0f;
        if (p.type < kSpriteParticleTypes) p.sprite.update(dtMs);
        p.ageMs += dtMs;
        if (!(p.scale > 0.01f)) p.active = false;
        if (!p.active && p.owner) *p.owner = -1;
    }
}

}  // namespace doc::game
