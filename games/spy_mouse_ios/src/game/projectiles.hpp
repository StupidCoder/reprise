// Things thrown in a boss fight: the Spitter's forks, spoons and knives in world 2. The
// original keeps 16 of them in its ProjectileManager (@0x18a680..0x178e3c: createProjectile,
// fireProjectile, update, triggerProjectile, destroyProjectile, the onCollide family and
// render); the scene tests them against the mouse, objects and walls
// (SceneGame::collideObjects @0x1009dc) and the session reacts to what they hit.
//
// A projectile is made (free slot taken), fired (flying), and once it hits something it is
// "triggered": a straight one then sticks where it hit for 2 s (fading out over the last
// second; something it stuck in keeps it with it) and is gone. One that leaves the screen, or
// flies on a floor not shown, is gone at once.
//
// Only what world 2 uses is reconstructed: straight flight (kind 2) and the cutlery pictures.
// The other kinds (homing 5, lobbed 4, timed 3, the player's own 0), pictures 1, 2, 7 and 9,
// bouncing off walls and moving a trigger object are not.
#pragma once

#include "game/geometry.hpp"
#include "game/random.hpp"
#include "game/sound.hpp"
#include "game/sprite_anim.hpp"

#include <array>
#include <functional>
#include <vector>

namespace doc::game {

// What a projectile is asked to be (the original's ProjInfo).
struct ProjectileSpec {
    int flight = 2;          // +0x00: how it flies (2 straight)
    int playerTrigger = 0;   // +0x04: whether the mouse can catch it (0/2) and what it does
    int hits = 0;            // +0x08: bits: 1 the mouse, 2 cats, 4 bosses, 8 balloons
    float timerMs = -1.0f;   // +0x0c
    int triggerObject = -1;  // +0x10
    int picture = 0;         // +0x14: 4 fork, 5 spoon, 6 knife
    int layer = 8;           // +0x1c: the drawing pass it shows in
    int floor = 0;           // +0x24
};

enum class ProjectileStatus : int { Made = 0, Flying = 1, Hit = 2, Fading = 3, Gone = 4 };

struct Projectile {
    int slot = -1;  // +0x00: its own index while in use, -1 when free
    ProjectileStatus status = ProjectileStatus::Gone;
    int flight = 0, playerTrigger = 0, hits = 0, picture = 0, layer = 0, floor = 0;
    SpriteAnim anim;           // +0x1c
    Vec2 position;             // +0xdc (its height, +0xe4, only lobbed ones use)
    float angle = 0.0f;        // +0xe8: the picture's turn, radians
    float size = 0.0f;         // +0xec: touch radius
    Vec2 velocity;             // +0xf0, units a second
    float spin = 0.0f;         // +0xfc, radians a second
    float lift = 0.0f;         // +0x100: the lob's upward speed (kept, unused when straight)
    float timerMs = -1.0f;     // +0x104: a fading one's time left
    int triggerObject = -1;    // +0x108
    int stuckTo = -1;          // +0x10c: the object (or player) it hit
    bool stuckToMouse = false; // +0x110
    Vec2 stuckOffset;          // +0x114: from that object's middle
    bool inUse() const { return slot >= 0; }
};

class Projectiles {
public:
    static constexpr int kSlots = 16;
    // ProjectileManager::load @0x18ca5c: the templates of the cutlery pictures (30 wide).
    // Sheet sizes from the images.
    void setPictureSheet(int picture, int sheetWidth, int sheetHeight);

    const std::array<Projectile, kSlots>& slots() const { return slots_; }
    Projectile& slot(int i) { return slots_[std::size_t(i)]; }

    // createProjectile @0x18cb84: the first free slot takes the spec (index, or -1).
    int create(const ProjectileSpec& spec);
    // fireProjectile @0x18b790: on its way from `at` with `velocity`; its picture turns along
    // it. Sound by picture.
    void fire(int index, Vec2 at, Vec2 velocity, float spin, float lift, std::vector<SoundEvent>& sounds);
    // triggerProjectile @0x18b208: it hit something (status Hit, no more spin), with the
    // sound of what it hit.
    void trigger(int index, GameRandom& random, std::vector<SoundEvent>& sounds);
    // onCollide(int, Object const*) @0x18b4cc: stuck in that object (`offset` from its middle),
    // then triggered.
    void stickInto(int index, int objectId, Vec2 objectPosition, GameRandom& random, std::vector<SoundEvent>& sounds);
    // destroyProjectile @0x18bc48: the slot is free again.
    void destroy(int index);

    // What the update needs from the scene.
    struct Scene {
        int shownFloor = 0;         // SceneGame +0x130
        float viewLeft = 0.0f;      // +0x13838
        std::function<const Vec2*(int objectId)> objectPosition;  // null: not there
        GameRandom* random = nullptr;
        std::vector<SoundEvent>* sounds = nullptr;
    };
    // update @0x18bee4. `dtMs` is the update's own time (0 for the extra update a rail sphere
    // runs when it is hit); the flight always moves by the frame's tick time (s_nTickTime).
    // Note the original's quirk: a projectile that hit something this tick ends the update,
    // the slots after it wait for the next one.
    void update(int dtMs, int tickMs, const Scene& scene);

private:
    std::array<Projectile, kSlots> slots_{};
    std::array<SheetSize, 10> pictures_{};
};

}  // namespace doc::game
