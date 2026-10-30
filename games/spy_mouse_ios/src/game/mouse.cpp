#include "game/mouse.hpp"

#include <algorithm>
#include <cmath>

namespace doc::game {

void Mouse::place(Vec2 entity, Vec2 spriteSize) {
    size = spriteSize;
    position = {entity.x, entity.y + size.y * 0.5f};
    lastFootprint = position;
    heading = 4.712389f;
    pathIndex = 0;
    velocity = {};
    lastStepLength = 0.0f;
}

void Mouse::normaliseHeadingForFrame() {
    float h = std::fmod(heading, 6.2831855f);
    if (h < 0.0f) h = h + 6.2831855f;
    heading = h;
}

float carriedWeight(int objectType, const Tuning& tuning) {
    if (objectType >= 6 && objectType <= 10) return tuning.cheeseWeight[10 - objectType];
    return tuning.otherWeight;
}

float trailWeight(const int* objectTypes, int count, const Tuning& tuning) {
    if (count <= 0) return 0.0f;
    // A balloon on the trail carries it all (Player::updateTrail @0x123abe: weight 0; run
    // l211_win).
    for (int i = 0; i < count; ++i)
        if (objectTypes[i] == 18) return 0.0f;
    float sum = 0.0f;
    for (int i = 0; i < count; ++i) sum = sum + carriedWeight(objectTypes[i], tuning);
    // The average is truncated to a whole number, then every further item adds a fraction.
    float average = float(int(sum / float(count)));
    float weight = average;
    for (int i = 1; i < count; ++i) weight = weight + average * tuning.chainMod;
    return weight;
}

void Mouse::tick(int dtMs, DrawnPath& path, float weight, const Tuning& tuning, bool infinitePath) {
    // Recover from slow-downs over player_speed_recover milliseconds.
    if (acceleration < 1.0f) acceleration = acceleration + float(dtMs) / tuning.playerSpeedRecoverMs;
    if (acceleration >= 0.0f) {
        if (acceleration > 1.0f) acceleration = 1.0f;
    } else {
        acceleration = 0.0f;
    }
    if (boostTimerMs > 0) boostTimerMs -= dtMs;

    // Base speed (1.1.1: faster on roller skates or in the Invinciball, Player::update
    // @0x125d38), slowed by a heavy trail of cheese.
    float speed = tuning.playerRunSpeed;
    if (skating) speed = speed * 2.0999999f;
    else if (inBall) speed = speed * 1.35000002f;
    // Their pictures grow to full size (@0x125f40: by (1 - s) x dt x 0.02).
    if (inBall && ballScale < 1.0f) ballScale = std::min(1.0f, ballScale + (1.0f - ballScale) * float(dtMs) * 0.0199999996f);
    if (skating && skateScale < 1.0f)
        skateScale = std::min(1.0f, skateScale + (1.0f - skateScale) * float(dtMs) * 0.0199999996f);
    if (weight > tuning.totalWeight) {
        float factor = state == 8 ? tuning.playerCarrySpeed : tuning.totalWeight / weight;
        speed = factor * speed;
    } else if (state == 8) {
        speed = tuning.playerCarrySpeed * speed;
    }
    if (state == 4) speed = tuning.waterSpeedMult * speed;

    // Lifting the finger ends a boost.
    if (!path.drawing && wasDrawing) {
        ++releases;
        boostTimerMs = 0;
    }
    wasDrawing = path.drawing;
    if (state == 3 || state == 7) speed = 0.0f;
    if (state == 6) speed = speed * 0.5f;

    // Head for the next path point; points within reach are passed (and dropped). Passing the
    // last one still turns the mouse towards it (Player::update @0x126108; 1.0.5 kept the old
    // heading for that tick).
    if (pathIndex >= 0 && pathIndex < path.size()) {
        float dx = path[pathIndex].x - position.x;
        float dy = path[pathIndex].y - position.y;
        while (dx * dx + dy * dy <= reachDistance) {
            ++pathIndex;
            if (pathIndex >= path.size()) break;
            if (infinitePath) {
                path.trim(1);
                --pathIndex;
            }
            dx = path[pathIndex].x - position.x;
            dy = path[pathIndex].y - position.y;
        }
        heading = std::atan2(dy, dx);
    } else {
        speed = 0.0f;
    }

    speed_ = speed;
    if (growing) {
        // (Player::update: no step until it has grown to its full size, run l18_holes.)
        velocity = {};
        moving_ = false;
        return;
    }
    if (std::fabs(speed) >= 0.001f) {
        float step = float(dtMs) * ((tuning.playerScoreSpeedMult * speed) * scoreBoost + speed);
        step = step * kSpeedScale;
        if (acceleration > 0.0f) step = acceleration * step;
        if (boostTimerMs >= 1) step = step * 5.0f;
        if (seenByBoss) {
            // The boss cat has seen it (state 0x10): the mouse freezes, picture and all
            // (Player::update @0x126eb8).
            step = 0.0f;
            sprite.pause(true);
        }
        velocity = {libCos(heading) * step, libSin(heading) * step};
        position += velocity;
        lastStepLength = velocity.length();
        distanceWalked = distanceWalked + lastStepLength;
        moving_ = true;
    } else {
        // Standing still: no step this tick (the original leaves the last step length).
        velocity = {};
        moving_ = false;
    }
}

const char* const Mouse::kSequenceFiles[14] = {
    "anim_sequences/idle.xml.brd",           "anim_sequences/idle_scratch.xml.brd",
    "anim_sequences/flying.xml.brd",         "anim_sequences/surprised.xml.brd",
    "anim_sequences/player_checkmap.xml.brd", "anim_sequences/player_sniff.xml.brd",
    "anim_sequences/player_yoyo.xml.brd",    "anim_sequences/player_juggle.xml.brd",
    "anim_sequences/player_shurikendeath.xml.brd", "anim_sequences/player_flamedeath.xml.brd",
    "anim_sequences/player_flamedeath_2.xml.brd",  "anim_sequences/player_pushwall.xml.brd",
    "anim_sequences/player_eatcheese.xml.brd",
    "anim_sequences/player_cutlerydeath.xml.brd",  // (new in 1.1.1)
};

const char* const Mouse::kSequenceNames[kSequenceCount] = {
    "idle1",     "scratch1",  "flying", "landing", "surprised1",     "sniff1",     "checkmap1",
    "pushwall1", "eatcheese1", "yoyo1", "juggle_loop", "shurikendeath1", "flamedeath", "flamedeath2",
    "cutlerydeath1",
};

void Mouse::setUpSprite(const std::vector<data::AnimSequence>& sequences, bool useBucket) {
    sprite = SpriteAnim{};
    sprite.setSheet(37 * 4, 37, 37.0f);
    sprite.setAnim(0, 3, SpriteAnim::Loop, 75);
    for (const data::AnimSequence& s : sequences) sprite.addSequence(s);
    for (int i = 0; i < kSequenceCount; ++i) sequenceIds[i] = sprite.findSequence(kSequenceNames[i]);
    cupSprite = SpriteAnim{};
    bucket = useBucket;
    if (bucket) cupSprite.setSheet(98, 49, 49.0f);
    else cupSprite.setSheet(68, 34, 34.0f);
    cupSprite.setAnim(0, 1, SpriteAnim::Loop, 200);
}

void Mouse::updateAnimation(int dtMs, GameRandom& random, bool cupRuns) {
    const int current = sprite.currentSequence();
    // (Growing out of a hole the mouse stands, path or not: its speed counts as nothing.)
    if (std::fabs(speed_) >= 0.001f && !growing) {
        if (state == 8) return;
        // Walking: the walk cycle (unless it is already running) on the picture that runs, the
        // worn cup's while it shows (whose two frames always count as walking): the mouse's
        // own idle stays current under the cup (Player::update @0x1269d2, run l35_win).
        idleStep = 1;
        idleMs = 0;
        SpriteAnim& running = cupRuns ? cupSprite : sprite;
        running.pause(false);
        const bool walking = running.frame() < 4 && running.currentSequence() == -1;
        if (current != sequenceIds[kSurprised] && !walking) running.setAnim(0, 3, SpriteAnim::Loop, 75);
        return;
    }
    if (state == 8 || state == 9 || state == 2) return;
    idleMs += dtMs;
    // Under a worn teacup the idles start over every tick (Player::update @0x126d1c, +0x180).
    if (cup >= 0) idleStep = 1;
    // Not while one of the idle animations (or a landing) is still playing.
    // A wall push (the view's edge pushing the mouse in an infinite level) holds only while the
    // view moves faster than 0.1 (@0x126a84).
    if (current == sequenceIds[kPushWall] && viewScrolling) return;
    static const Sequence kIdleKinds[] = {kIdle, kScratch, kSniff, kCheckMap, kYoyo, kJuggle, kEatCheese, kLanding};
    for (Sequence k : kIdleKinds)
        if (current == sequenceIds[k]) return;
    if (idleMs <= 99 || sprite.frame() >= 4 || current == sequenceIds[kSurprised]) return;

    // The facing quadrant for the plain idle: heading - 45 degrees, in quarter turns.
    float angle = std::fmod(heading + -0.785398185f, 6.28318548f);
    if (angle < 0.0f) angle = angle + 6.28318548f;
    angle = angle * 0.636619747f;
    if (idleStep == 1) {
        int row = int(angle);
        if (row >= 3) row = 3;
        sprite.playSequence(sequenceIds[kIdle], SpriteAnim::SequenceOnce, row);
    } else if (idleStep == 0) {
        sprite.playSequence(sequenceIds[kCheckMap], SpriteAnim::SequenceOnce, 0);
    } else if (idleMs > 119999) {
        static const Sequence kLong[2] = {kYoyo, kJuggle};
        sprite.playSequence(sequenceIds[kLong[random.next() % 2]], SpriteAnim::SequenceOnce, 0);
        idleStep = 0;
        idleMs = 0;
    } else if (idleStep & 1) {
        sprite.playSequence(sequenceIds[kIdle], SpriteAnim::SequenceOnce, 2);
    } else {
        static const Sequence kTricks[2] = {kScratch, kSniff};
        sprite.playSequence(sequenceIds[kTricks[random.next() % 2]], SpriteAnim::SequenceOnce, 0);
    }
    ++idleStep;
}

}  // namespace doc::game
