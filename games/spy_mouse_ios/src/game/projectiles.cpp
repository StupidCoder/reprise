#include "game/projectiles.hpp"

#include "game/object_motion.hpp"

#include <cmath>

namespace doc::game {
namespace {

// Sounds (sound table).
constexpr int kSoundFireCutlery = 0x3c;  // fireProjectile, pictures 4..6
constexpr int kSoundFireDart = 0x3e;     // pictures 0 and 3
constexpr int kSoundFireBall = 0x40;     // pictures 1 and 2
constexpr int kSoundHitCutlery = 0x3d;   // triggerProjectile, pictures 4..6
constexpr int kSoundHitBall = 0x41;      // pictures 1 and 2
constexpr int kSoundHitFirst = 0x39;     // other pictures: one of three
constexpr int kSoundHitObject = 0x3f;    // stuck in an object

void play(std::vector<SoundEvent>& sounds, int effect) {
    SoundEvent e;
    e.kind = SoundEvent::Play;
    e.effect = effect;
    sounds.push_back(e);
}

}  // namespace

void Projectiles::setPictureSheet(int picture, int sheetWidth, int sheetHeight) {
    if (picture >= 0 && picture < int(pictures_.size())) pictures_[std::size_t(picture)] = {sheetWidth, sheetHeight};
}

int Projectiles::create(const ProjectileSpec& spec) {
    for (int i = 0; i < kSlots; ++i) {
        Projectile& p = slots_[std::size_t(i)];
        if (p.inUse()) continue;
        p.slot = i;
        p.status = ProjectileStatus::Made;
        p.picture = spec.picture;
        p.flight = spec.flight;
        p.playerTrigger = spec.playerTrigger;
        p.hits = spec.hits;
        p.timerMs = spec.timerMs;
        p.triggerObject = spec.triggerObject;
        p.floor = spec.floor;
        p.stuckTo = -1;
        p.stuckToMouse = false;
        p.layer = spec.layer;
        // The picture's template (load @0x18ca5c: 30 wide). Sprite::clone @0x133688 copies the
        // template's loop but sets the image again, which leaves it looping over frame 0 alone
        // at 250 ms (run lbf2_world2: a flying knife never changes its frame).
        p.anim = SpriteAnim{};
        if (spec.picture >= 4 && spec.picture <= 6) {
            const SheetSize sheet = pictures_[std::size_t(spec.picture)];
            p.anim.setSheet(sheet.width, sheet.height, 30.0f);
            p.anim.setAnim(0, 0, SpriteAnim::Loop, 250);
        } else if (spec.picture == 0 || spec.picture == 3) {
            // The shurikens (sprites/shuriken.png, boss4_shuriken.png: the whole picture one
            // frame).
            const SheetSize sheet = pictures_[std::size_t(spec.picture)];
            p.anim.setSheet(sheet.width, sheet.height, float(sheet.width));
            p.anim.setAnim(0, 0, SpriteAnim::Loop, 250);
        }
        // Touch radius: 5 at the level's scale (LevelState +0x64, 1 on the phone).
        p.size = 5.0f;
        return i;
    }
    return -1;
}

void Projectiles::fire(int index, Vec2 at, Vec2 velocity, float spin, float lift, std::vector<SoundEvent>& sounds) {
    Projectile& p = slots_[std::size_t(index)];
    if (!p.inUse() || p.status == ProjectileStatus::Gone) return;
    p.status = ProjectileStatus::Flying;
    p.position = at;
    p.velocity = velocity;
    p.spin = spin;
    p.lift = lift;
    Vec2 along = velocity;
    along.normalise(1.0f);
    p.angle = wrapAngle(std::atan2(along.y, along.x));
    switch (p.picture) {
    case 0: case 3: play(sounds, kSoundFireDart); break;
    case 1: case 2: play(sounds, kSoundFireBall); break;
    case 4: case 5: case 6: play(sounds, kSoundFireCutlery); break;
    default: break;
    }
}

void Projectiles::trigger(int index, GameRandom& random, std::vector<SoundEvent>& sounds) {
    Projectile& p = slots_[std::size_t(index)];
    if (!p.inUse() || p.status == ProjectileStatus::Gone) return;
    p.status = ProjectileStatus::Hit;
    p.spin = 0.0f;
    int effect = kSoundHitObject;
    if (p.stuckTo == -1) {
        switch (p.picture) {
        case 1: case 2: effect = kSoundHitBall; break;
        case 4: case 5: case 6: effect = kSoundHitCutlery; break;
        default: effect = random.next() % 3 + kSoundHitFirst; break;
        }
    }
    play(sounds, effect);
}

void Projectiles::stickInto(int index, int objectId, Vec2 objectPosition, GameRandom& random, std::vector<SoundEvent>& sounds) {
    Projectile& p = slots_[std::size_t(index)];
    if (!p.inUse() || p.status == ProjectileStatus::Gone) return;
    p.stuckOffset = {p.position.x - objectPosition.x, p.position.y - objectPosition.y};
    p.stuckTo = objectId;
    p.stuckToMouse = false;
    trigger(index, random, sounds);
}

void Projectiles::destroy(int index) {
    Projectile& p = slots_[std::size_t(index)];
    p = Projectile{};
}

void Projectiles::update(int dtMs, int tickMs, const Scene& scene) {
    const float dtSeconds = float(tickMs) * 0.00100000005f;
    for (int i = 0; i < kSlots; ++i) {
        Projectile& p = slots_[std::size_t(i)];
        if (!p.inUse()) continue;
        switch (p.status) {
        case ProjectileStatus::Made:
            continue;
        case ProjectileStatus::Flying:
            if (p.floor != scene.shownFloor) {
                p.status = ProjectileStatus::Gone;
                continue;
            }
            break;
        case ProjectileStatus::Hit:
            // Straight ones stay stuck for 2 s; their picture wobbles once (frames 1.. at 80
            // ms). Either way the update ends here for this tick.
            if (p.flight == 2) {
                p.status = ProjectileStatus::Fading;
                p.timerMs = 2000.0f;
                if (p.anim.frameCount() >= 2) p.anim.playOneShot(1, p.anim.frameCount() - 1, 80);
            } else {
                p.status = ProjectileStatus::Gone;
            }
            return;
        case ProjectileStatus::Fading:
            if (p.timerMs <= 0.0f) p.status = ProjectileStatus::Gone;
            p.velocity = {};
            p.timerMs = p.timerMs - float(dtMs);
            break;
        case ProjectileStatus::Gone:
            destroy(i);
            continue;
        }

        // On its way (or stuck): turning, the step, carried along with what it is stuck in.
        p.angle = wrapAngle(p.angle + p.spin * dtSeconds);
        const Vec2 step = (p.velocity * 1.0f) * dtSeconds;
        p.position += step;
        if (p.stuckTo != -1 && !p.stuckToMouse && scene.objectPosition)
            if (const Vec2* at = scene.objectPosition(p.stuckTo)) p.position = *at + p.stuckOffset;
        p.anim.update(dtMs);
        // Out of the view (whole units, the size's margin round it): gone.
        const float x = float(int(p.position.x));
        const float y = float(int(p.position.y));
        if (x > (kScreenWidth + scene.viewLeft) + p.size || x < scene.viewLeft - p.size ||
            y > kScreenHeight + p.size || y < -p.size)
            p.status = ProjectileStatus::Gone;
    }
}

}  // namespace doc::game
