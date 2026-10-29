// The arrow pad of the sixth adventure's special play loops (see
// a6_arrow_pad.hpp). Reconstructed from 0x32cbbc (11000), 0x32d0ec (11001),
// 0x32d054 (11002), 0x32d1a8 (11003) and 0x32c6e8 (11010).
#include "puzzles/a6_arrow_pad.hpp"

namespace picarat::adventure5 {

using adventure56::AnimationSet;
using adventure56::Object;
using adventure56::Resource;
using xq::Value;

namespace {

const AnimationSet* animationSet(Game& game, std::uint32_t hash) {
    const Resource* r = game.scene.resource(hash, Resource::Kind::Animation);
    return r ? r->animation.get() : nullptr;
}

// An animation of the set by name hash (0x2731c8); the pad's names are
// its animations ("right_off", "up_on", ...).
const AnimationSet::Animation* animationOf(const AnimationSet& set, std::uint32_t hash) {
    for (const auto& a : set.animations) {
        if (a.hash == hash) {
            return &a;
        }
    }
    return nullptr;
}

} // namespace

// The arrows' object draws one cell per slot that 11001 picked.
void ArrowPad::rebuild(Game& game) {
    Object* o = game.scene.object(object_);
    const AnimationSet* set = animationSet(game, set_);
    if (!o || !set) {
        return;
    }
    // Each slot shows its animation's first key: that key's cells at the
    // key's offset (hypothesis: the pad does not play the animations on).
    o->pieces.clear();
    for (int i = 0; i < count_; ++i) {
        const auto* a =
            cells_[std::size_t(i)] ? animationOf(*set, cells_[std::size_t(i)]) : nullptr;
        if (!a || a->count == 0 || a->first >= int(set->keys.size())) {
            continue;
        }
        const auto& key = set->keys[std::size_t(a->first)];
        if (key.composite < 0 || key.composite >= int(set->composites.size())) {
            continue;
        }
        const auto& comp = set->composites[std::size_t(key.composite)];
        for (int k = comp.first; k < comp.first + comp.count; ++k) {
            // At the key's offset moved by the animation's origin, as
            // 11003 places the touch areas (135's origins are 0; 040 /
            // 128 / 130 keep their pad round an origin).
            o->pieces.push_back({set->cellList[std::size_t(k)], key.offset[0] + a->origin[0],
                                 key.offset[1] + a->origin[1]});
        }
    }
    o->pieceCapacity = int(o->pieces.size());
}

bool ArrowPad::command(Game& game, int number, xq::Call& c, std::size_t first) {
    const std::size_t count = c.count() - first;
    auto hashOf = [&](std::size_t i) {
        const Value& v = c.at(first + i);
        return v.isText() ? xq::hash(v.text) : v.toUint();
    };
    auto yes = [&](bool b) {
        c.result = Value::ofInt(b ? 1 : 0);
        return true;
    };
    switch (number) {
    case 11000: {
        // 0x32cbbc: the pad on an object (object, animation set, slots): the
        // object draws the set's cells, one per slot.
        Object* o = count >= 3 ? game.scene.object(hashOf(0)) : nullptr;
        const AnimationSet* set = o ? animationSet(game, hashOf(1)) : nullptr;
        if (!set) {
            return yes(false);
        }
        object_ = hashOf(0);
        set_ = hashOf(1);
        count_ = std::min(int(c.integer(first + 2)), int(cells_.size()));
        cells_.fill(0);
        levels_.fill(1.0f);
        o->animation = game.scene.resource(set_, Resource::Kind::Animation)->animation;
        rebuild(game);
        return yes(true);
    }
    case 11001: {
        // 0x32d0ec: the animation a slot shows (object, slot, set, name).
        if (count < 4 || hashOf(0) != object_ || hashOf(2) != set_) {
            return yes(false);
        }
        const int slot = int(c.integer(first + 1));
        if (slot < 0 || slot >= count_) {
            return yes(false);
        }
        cells_[std::size_t(slot)] = hashOf(3);
        rebuild(game);
        return yes(true);
    }
    case 11002: {
        // 0x32d054: a slot's level (object, slot, value; the scripts dim a
        // blocked arrow to 0.4). Kept; not drawn yet.
        if (count < 3 || hashOf(0) != object_) {
            return yes(false);
        }
        const int slot = int(c.integer(first + 1));
        if (slot < 0 || slot >= count_) {
            return yes(false);
        }
        levels_[std::size_t(slot)] = c.real(first + 2);
        return yes(true);
    }
    case 11003: {
        // 0x32d1a8: a touch area on an object (object, direction, set,
        // animation name) over the animation's box moved by its origin
        // (0x2731c8: origin +0x14, box +0x20 .. +0x2c; 0x28a594 adds them;
        // 0x25c6f8 keeps the area). The pads of 020 / 135 have origin 0,
        // 040's lobes are boxes round their centres.
        Object* o = count >= 4 ? game.scene.object(hashOf(0)) : nullptr;
        const AnimationSet* set = o ? animationSet(game, hashOf(2)) : nullptr;
        const auto* a = set ? animationOf(*set, hashOf(3)) : nullptr;
        if (!a || o->areas.size() >= 16) {
            return yes(false);
        }
        const float* box = a->box;
        o->areas.push_back(
            {1, box[0] + a->origin[0], box[1] + a->origin[1], box[3] - box[0], box[4] - box[1]});
        return yes(true);
    }
    case 11010: {
        // 0x32c6e8: which way the pad is pressed (object, blocked
        // directions as bits 1 right, 2 up, 4 left, 8 down, register) as the
        // result: a touched area of the object (in the order 11003 made
        // them; 5 while the pen is down outside them) or the pad's buttons
        // (then the register gets 1, else 0); 0 for none, and nothing while
        // the loop's input is off.
        Object* o = count >= 3 ? game.scene.object(hashOf(0)) : nullptr;
        if (!o) {
            return yes(false);
        }
        const std::uint32_t blocked = c.unsignedInt(first + 1);
        int way = 0;
        bool pad = false;
        if (!game.objectTouchLocked) {
            if (game.input.touching) {
                way = 5;
                const auto [lx, ly] =
                    game.scene.toLocal(*o, float(game.input.x), float(game.input.y));
                for (std::size_t k = 0; k < 4 && k < o->areas.size(); ++k) {
                    const auto& a = o->areas[k];
                    if (!(blocked & (1u << k)) && lx >= a.x && lx <= a.x + a.w && ly >= a.y &&
                        ly <= a.y + a.h) {
                        way = int(k) + 1;
                        break;
                    }
                }
            } else {
                // The pad: right, up, left, down (0x25f79c on game +0x31c,
                // bits 1, 2, 0, 3 in the game's order).
                const std::uint32_t b = game.input.buttons;
                if ((b & Input::kRight) && !(blocked & 1)) {
                    way = 1;
                } else if ((b & Input::kUp) && !(blocked & 2)) {
                    way = 2;
                } else if ((b & Input::kLeft) && !(blocked & 4)) {
                    way = 3;
                } else if ((b & Input::kDown) && !(blocked & 8)) {
                    way = 4;
                }
                pad = way != 0;
            }
        }
        if (Value* r = c.reg(first + 2)) {
            *r = Value::ofInt(pad ? 1 : 0);
        }
        c.result = Value::ofInt(way);
        return true;
    }
    default:
        return false;
    }
}

} // namespace picarat::adventure5
