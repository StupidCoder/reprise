// Things that come by now and then along a route of their own: cars (types 410-412) and
// butterflies (420, 421). Object::init @0x11aa30 gives each a speed ("car_speed" 4.9,
// "butterfly_speed" 0.9) and a first wait; Object::update @0x118dd4 counts the wait down, puts
// it at its route's start and lets it follow the route (Object::updatePath @0x115614, with
// the generic turn and Object::updatePositions); at the route's end it waits again, the cars'
// time either way ("car_appear_time" 28..42 s). Objects only run while their floor is shown.
#pragma once

#include "game/cat.hpp"
#include "game/geometry.hpp"
#include "game/level_geometry.hpp"
#include "game/random.hpp"
#include "game/sprite_anim.hpp"

namespace doc::game {

inline bool isRoamer(int type) { return (type >= 410 && type <= 412) || type == 420 || type == 421; }

class Roamer {
public:
    Roamer(int objectId, int type, Vec2 position, float radius, CatRoute route, GameRandom& random);

    void update(int dtMs, const LevelGeometry& walls, float viewLeft, GameRandom& random);

    int objectId() const { return objectId_; }
    int type() const { return type_; }
    Vec2 position() const { return position_; }
    float rotation() const { return rotation_; }  // +0x108
    bool waiting() const { return waitMs_ >= 1; }
    bool facingBack() const { return direction_ < 1; }  // +0x9c for cars
    SpriteAnim anim;  // butterflies flap (frames 0-2, 200 ms)

private:
    float followRoute(float speed);  // updatePath: the speed, 0 at the route's end

    int objectId_ = 0;
    int type_ = 0;
    Vec2 position_;
    float radius_ = 0.0f;   // +0x104
    CatRoute route_;
    int index_ = 0;         // +0x16c
    int direction_ = 1;     // +0x174
    float speed_ = 0.0f;    // +0x24
    float heading_ = 0.0f;  // +0x10c
    float rotation_ = 0.0f; // +0x108
    int waitMs_ = 0;        // +0x874
public:
    // Quick saves (state_io.hpp).
    template <class Io>
    void io(Io& io) { io(anim, objectId_, type_, position_, radius_, route_, index_, direction_, speed_,
                           heading_, rotation_, waitMs_); }
};

}  // namespace doc::game
