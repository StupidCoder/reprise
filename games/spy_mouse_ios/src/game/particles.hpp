// The original's particle pool (class Particles: addParticle @0x138b88, update @0x139bac):
// 32 slots of short-lived sprites and floating texts (sparkles, footprints, stars, smoke,
// fireworks, score popups). They do not affect play, but many types draw random numbers when
// they start or move, so the pool is part of the game state: without it the random sequence,
// and with it everything random later in a level, would differ from the original.
#pragma once

#include "game/geometry.hpp"
#include "game/sprite_anim.hpp"

#include <array>
#include <vector>
#include <string>

namespace doc::game {

class GameRandom;

// Per-type data, read from the original's program image (data::readParticleKinds): priority
// when the pool is full (0: never replaces another particle), sprite sheet and its size, frame
// width and drawing layer.
struct ParticleKind {
    int priority = 0;
    std::string sheet;
    SheetSize sheetSize;
    float frameWidth = 0;
    int layer = 0;
};
constexpr int kSpriteParticleTypes = 0x32;  // 0x32.. are text particles (1.1.1; 1.0.5: 0x35..)
// Text kinds (1.1.1 numbers).
constexpr int kTextBubble = 0x33;     // over a cat, a hole or the screen ("Busted!", "Hidden Area!")
constexpr int kTextPoints = 0x34;     // crumb points, flying to the score
constexpr int kTextMission = 0x35;    // "Mission ...": pops in with its sound
constexpr int kTextMessage = 0x36;    // (Object::receiveMessage)
constexpr int kTextLevelOver = 0x37;  // "Try Again!" / "Keep Going!"
constexpr int kTextStun = 0x38;       // (ObjControllerCat::stun)
constexpr int kTextRising = 0x39;

struct Particle {
    bool active = false;
    int type = 0;
    SpriteAnim sprite;
    std::string text;           // text particles (kinds >= 0x32)
    Vec2 position, velocity;
    float scale = 1.0f, alpha = 1.0f, rotation = 0.0f;  // rotation in degrees
    int phase = 0;              // per-type counter (fireworks bursts, popup phases)
    int ageMs = 0;
    int floor = 0;
    int life = 0;               // lifetime, or per-type data (variant, raw random number)
    int aux = 0;                // per-type data (fade time, linked particle)
    bool flag = false;          // per-type flag (drawn additive / faster fade)
    int delayMs = 0;            // not shown or updated until the delay has passed
    float red = 1.0f, green = 1.0f, blue = 1.0f;
    int* owner = nullptr;       // set to the slot while alive, -1 afterwards
    // Quick saves (state_io.hpp). Not saved: owner (Session::particleOwners).
    template <class Io>
    void io(Io& io) { io(active, type, sprite, text, position, velocity, scale, alpha, rotation, phase, ageMs,
                           floor, life, aux, flag, delayMs, red, green, blue); }
};

class Particles {
public:
    static constexpr int kSlots = 32;

    Particles(GameRandom& random, std::vector<ParticleKind> kinds);

    // Starts a particle; returns its slot or -1 when the pool is full of more important ones.
    // `direction` is used by the types that need one (sparks); `clipText` keeps texts on
    // screen.
    int add(int type, int floor, Vec2 position, const std::string& text = {}, int* owner = nullptr,
            int delayMs = 0, Vec2 direction = {-1.0f, -1.0f}, bool clipText = true);
    void update(int dtMs);
    // "Mission" text particles (kTextMission) that appeared since the last call: each plays the
    // text pop (Particles::addParticle @0x138cbe, update @0x139ce0).
    int takeTextPops() { const int n = textPops_; textPops_ = 0; return n; }
    // Sounds the pool started since the last call (a firework bursting: 0x67..0x6a).
    std::vector<int> takeSounds() { std::vector<int> out; out.swap(sounds_); return out; }
    void clear();

    const std::array<Particle, kSlots>& slots() const { return slots_; }
    // A live particle by slot (its owner's handle), else null.
    // The handle a slot's particle reports to (quick saves keep it as its cat or droid).
    int* owner(int slot) const { return slots_[std::size_t(slot)].owner; }
    void setOwner(int slot, int* owner) { slots_[std::size_t(slot)].owner = owner; }
    Particle* at(int slot) {
        return slot >= 0 && slot < kSlots && slots_[std::size_t(slot)].active ? &slots_[std::size_t(slot)] : nullptr;
    }
    const ParticleKind& kind(int type) const;

    // The level's display offset (LevelState +0x68, zero on single-screen levels), as the
    // original reads it for on-screen tests and bug paths, and the screen size.
    Vec2 viewOffset;
    float viewLeft = 0.0f;
    Vec2 screenSize{480.0f, 320.0f};
    float textureScale = 1.0f;  // g_pVal +0x60

private:
    int textPops_ = 0;
    std::vector<int> sounds_;
    bool onScreen(const Particle& p) const;
    int priorityOf(int type) const;

    GameRandom& random_;
    std::vector<ParticleKind> kinds_;
    std::array<Particle, kSlots> slots_{};
public:
    // Quick saves (state_io.hpp).
    // Not saved: random_ (the scene's generator), kinds_ (the game's particle table).
    template <class Io>
    void io(Io& io) { io(viewOffset, viewLeft, screenSize, textureScale, textPops_, sounds_, slots_); }
};

}  // namespace doc::game
