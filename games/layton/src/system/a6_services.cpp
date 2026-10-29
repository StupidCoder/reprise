// The sixth adventure's own script services, registered on top of the
// shared 3DS runtime's (Game::registerSixthServices, edition 6 only).
//
// The 20000 family: vector helpers of the sixth adventure's game service
// table (0x4238bc), used by the puzzles that move things in 3D. Each takes
// its vectors as three floats; results go to the result register or to the
// registers named by the last operands. Reconstructed from the handlers'
// floating-point code (the decompiler hides those arguments; read from the
// disassembly).
#include "puzzles/a6_pad_hooks.hpp"
#include "system/a5_game.hpp"
#include <array>
#include <chrono>
#include <cmath>
#include <ctime>

namespace picarat::adventure5 {

namespace {

using Vec = std::array<float, 3>;

Vec vecAt(const xq::Call& c, std::size_t i) {
    return {c.real(i), c.real(i + 1), c.real(i + 2)};
}

float length(const Vec& v) {
    // 0x251c94 and the handlers' own: sqrt only when the square is above 0.
    const float sq = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
    return sq > 0.0f ? std::sqrt(sq) : 0.0f;
}

// 0x251c94: the vector scaled to length 1 (the zero vector stays zero).
Vec normalised(const Vec& v) {
    const float sq = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
    if (!(sq > 0.0f)) {
        return {0, 0, 0};
    }
    const float k = 1.0f / std::sqrt(sq);
    return {v[0] * k, v[1] * k, v[2] * k};
}

std::uint32_t hashOf(const xq::Call& c, std::size_t i) {
    const xq::Value& v = c.at(i);
    return v.isText() ? xq::hash(v.text) : v.toUint();
}

void store(xq::Call& c, std::size_t i, float v) {
    if (xq::Value* r = c.reg(i)) {
        *r = xq::Value::ofFloat(v);
    }
}

} // namespace

void Game::registerSixthServices() {
    auto& m = machine;
    // 20000 (0x19e0f0): the cosine of the angle between two vectors (a, b):
    // the dot product of both normalised (0x251c94); 0 for fewer than six
    // operands.
    m.service(20000, [](xq::Call& c) {
        if (c.count() < 6) {
            c.result = xq::Value::ofFloat(0.0f);
            return false;
        }
        const Vec a = normalised(vecAt(c, 0)), b = normalised(vecAt(c, 3));
        c.result = xq::Value::ofFloat(a[0] * b[0] + a[1] * b[1] + a[2] * b[2]);
        return true;
    });
    // 20001 (0x1a7460): the cross product a x b (0x28a6b4) into the
    // registers named by operands 6..8; result 1 (0 below nine operands).
    m.service(20001, [](xq::Call& c) {
        c.result = xq::Value::ofInt(0);
        if (c.count() < 9) {
            return false;
        }
        const Vec a = vecAt(c, 0), b = vecAt(c, 3);
        store(c, 6, a[1] * b[2] - a[2] * b[1]);
        store(c, 7, a[2] * b[0] - a[0] * b[2]);
        store(c, 8, a[0] * b[1] - a[1] * b[0]);
        c.result = xq::Value::ofInt(1);
        return true;
    });
    // 20002 (0x1aadd4): a vector's length (0 below three operands).
    m.service(20002, [](xq::Call& c) {
        c.result = xq::Value::ofInt(0);
        if (c.count() < 3) {
            return false;
        }
        c.result = xq::Value::ofFloat(length(vecAt(c, 0)));
        return true;
    });
    // 20003 (0x1af938): a vector rotated about an axis (a, axis, angle,
    // three registers): the unit quaternion (axis normalised x sin(angle/2),
    // cos(angle/2)) (0x2517c0) made a rotation matrix (0x2827a0, scale
    // 2 / |q|^2) applied to a (0x2828e8); the result into the registers named
    // by operands 7..9; result 1 (0 below ten operands). Puzzle 061 places
    // its five eggs on a tilted ring so.
    m.service(20003, [](xq::Call& c) {
        c.result = xq::Value::ofInt(0);
        if (c.count() < 10) {
            return false;
        }
        const Vec a = vecAt(c, 0), n = normalised(vecAt(c, 3));
        const float half = c.real(6) * 0.5f, s = std::sin(half);
        const float x = n[0] * s, y = n[1] * s, z = n[2] * s, w = std::cos(half);
        const float k = 2.0f / (x * x + y * y + z * z + w * w);
        const float kx = k * x, ky = k * y, kz = k * z; // 0x2827a0's order
        const float xx = x * kx, yy = y * ky, zz = z * kz;
        const float xy = x * ky, yz = y * kz, xz = z * kx;
        const float wx = w * kx, wy = w * ky, wz = w * kz;
        store(c, 7, (1 - (yy + zz)) * a[0] + (xy - wz) * a[1] + (xz + wy) * a[2]);
        store(c, 8, (xy + wz) * a[0] + (1 - (zz + xx)) * a[1] + (yz - wx) * a[2]);
        store(c, 9, (xz - wy) * a[0] + (yz + wx) * a[1] + (1 - (xx + yy)) * a[2]);
        c.result = xq::Value::ofInt(1);
        return true;
    });
    // 1113 (library table, 0x182bac): a mask of 1 << each operand (pad
    // bits for 1110's pad hooks); 0 without operands.
    m.service(1113, [](xq::Call& c) {
        if (c.count() == 0) {
            c.result = xq::Value::ofInt(0);
            return false;
        }
        std::uint32_t mask = 0;
        for (std::size_t i = 0; i < c.count(); ++i) {
            mask |= 1u << (c.integer(i) & 0xff);
        }
        c.result = xq::Value::ofInt(std::int32_t(mask));
        return true;
    });
    // 20650 (0x1ab958): always 0 in this build (the Nut Roller's Main
    // asks it every frame).
    m.service(20650, [](xq::Call& c) {
        c.result = xq::Value::ofInt(0);
        return true;
    });
    // 5070 / 5071 / 5073 / 5072 (0x190e5c / 0x190ec4 / 0x190f2c / 0x190f94):
    // an object's keep options for the scope switches (bytes +0x128 ..
    // +0x12b; result 1 when the object exists). Not modelled (the scope
    // stacks keep every switch); the result is.
    for (const int op : {5070, 5071, 5072, 5073}) {
        m.service(op, [this](xq::Call& c) {
            if (c.count() < 2) {
                return false;
            }
            const bool found = scene.object(hashOf(c, 0)) != nullptr;
            c.result = xq::Value::ofInt(found ? 1 : 0);
            return found;
        });
    }
    // 1110 (library table, 0x182b2c -> 0x258f34): a pad hook (mask, kind,
    // block, up to four arguments); a6_pad_hooks.hpp. Result 1 while the
    // table has room (32 slots).
    m.service(1110, [this](xq::Call& c) {
        auto& hooks = adventure5::padHooks(*this);
        const std::string* block = c.count() >= 3 ? c.text(2) : nullptr;
        if (!block || hooks.size() >= 32) {
            c.result = xq::Value::ofInt(0);
            return c.count() >= 3;
        }
        adventure5::PadHook h{std::uint32_t(c.integer(0)), c.integer(1), *block, {}};
        for (std::size_t i = 3; i < c.count() && h.arguments.size() < 4; ++i) {
            h.arguments.push_back(c.at(i));
        }
        hooks.push_back(std::move(h));
        c.result = xq::Value::ofInt(1);
        return true;
    });
    // 5110 (library table, 0x1a2754): an object's colour still changing
    // (5113's tween), as 5100 is the alpha's (hypothesis by that pairing;
    // the Transfer puzzles wait on it before flashing a marker again).
    m.service(5110, [this](xq::Call& c) {
        const xq::Value& v = c.count() ? c.at(0) : xq::Value::ofInt(0);
        const adventure56::Object* o =
            c.count() ? scene.object(v.isText() ? xq::hash(v.text) : v.toUint()) : nullptr;
        c.result = xq::Value::ofInt(o && o->colour.moving() ? 1 : 0);
        return o != nullptr;
    });
    // 10010 (0x1b13fc): whether the play loop's input is off (the loop's
    // +0x1d, which also stops Fit's pen calls and 11010); the port's object
    // touch lock stands for it (hypothesis).
    m.service(10010, [this](xq::Call& c) {
        c.result = xq::Value::ofInt(objectTouchLocked ? 1 : 0);
        return true;
    });
    // 20100 (0x1af760): a signed turn between two angles a and b about the
    // y axis (a, b, register): both made rotations (0x2517c0, 0x2827a0),
    // the matrices' words 6..8 (+0x18) u = (sin a, 0, cos a) and v
    // normalised, then (u.v - 1) * pi/2, negated when the cross product
    // u x v (0x28a6b4) points up (y > 0). 0 for equal angles, -pi..pi
    // otherwise; result 1 (0 below three operands). The Fit puzzles' turns
    // use its sign: the sign of sin(b - a). (The direction was checked
    // against the console: puzzle 156's first swing, runs/l6/s156.)
    m.service(20100, [](xq::Call& c) {
        c.result = xq::Value::ofInt(0);
        if (c.count() < 3) {
            return false;
        }
        const float a = c.real(0), b = c.real(1);
        const Vec u = normalised({std::sin(a), 0.0f, std::cos(a)});
        const Vec v = normalised({std::sin(b), 0.0f, std::cos(b)});
        float turn = (u[0] * v[0] + u[1] * v[1] + u[2] * v[2] - 1.0f) * 1.5707964f;
        if (turn < 0.0f && u[2] * v[0] - u[0] * v[2] > 0.0f) {
            turn = -turn;
        }
        store(c, 2, turn);
        c.result = xq::Value::ofInt(1);
        return true;
    });
    // 13200 (0x1aab8c): a lower-screen object's position and depth (5242 /
    // 5262; object +0x28..+0x30) into two or three registers; result 1 when
    // the object exists (168 tells its stacked pieces apart by the depth).
    m.service(13200, [this](xq::Call& c) {
        if (c.count() < 3) {
            c.result = xq::Value::ofInt(0);
            return false;
        }
        const adventure56::Object* o = scene.object(hashOf(c, 0));
        if (o) {
            const auto p = o->position.value();
            store(c, 1, p[0]);
            store(c, 2, p[1]);
            if (c.count() > 3) {
                store(c, 3, o->depth.value()[0]);
            }
        }
        c.result = xq::Value::ofInt(o ? 1 : 0);
        return true;
    });
    // 5628 (0x18ffe0 -> 0x1c95c0): an animation object's frame (5629's
    // +0xac) set, when 0 <= frame <= the playing animation's length; 140 /
    // 144 carry the frame over when they switch animations.
    m.service(5628, [this](xq::Call& c) {
        adventure56::Object* o = c.count() >= 2 ? scene.object(hashOf(c, 0)) : nullptr;
        bool ok = false;
        if (o && o->animation && o->playing >= 0 &&
            std::size_t(o->playing) < o->animation->animations.size()) {
            const float f = c.real(1);
            if (f >= 0.0f && f <= float(o->animation->animations[std::size_t(o->playing)].length)) {
                o->frame = unsigned(f);
                o->finished = false;
                ok = true;
            }
        }
        c.result = xq::Value::ofInt(ok ? 1 : 0);
        return o != nullptr;
    });
    // 6070 (0x194f70): whether a 3D object is shown (the inverse of +0x114
    // bit 0, the word 6005 sets; as 5005's on lower-screen objects).
    m.service(6070, [this](xq::Call& c) {
        const adventure56::Object3D* o = c.count() ? scene.object3D(hashOf(c, 0)) : nullptr;
        if (o) {
            c.result = xq::Value::ofInt(o->visible ? 1 : 0);
        }
        return o != nullptr;
    });
    // 20700 (0x1aac24): a merged object's copy (object, index): its
    // position (record +0x10, 0x2f2204; zeros for another kind of object
    // or an index past the copies) into three registers; result 1 when
    // the object exists (020 / 021 aim at the pike's parts).
    m.service(20700, [this](xq::Call& c) {
        if (c.count() < 5) {
            c.result = xq::Value::ofInt(0);
            return false;
        }
        const adventure56::Object3D* o = scene.object3D(hashOf(c, 0));
        if (o) {
            const auto i = std::size_t(c.integer(1));
            const bool has = o->merged && i < o->merged->instances.size();
            for (std::size_t k = 0; k < 3; ++k) {
                store(c, 2 + k, has ? o->merged->instances[i].position[k] : 0.0f);
            }
        }
        c.result = xq::Value::ofInt(o ? 1 : 0);
        return true;
    });
    // 2900 (0x172164): a part of the clock's local date and time (0 year, 1
    // month, 2 day, 3 hour, 4 minute, 5 second, 6 millisecond; 0x37a604..
    // 0x37a834 split the millisecond count by day 86400000, then 3600000 /
    // 24, 60000 / 60, 1000 / 60, % 1000); 0 and false for other parts.
    // Puzzle 061 also times a held press with the millisecond (a drag
    // starts after 100 ms). The port's clock is the host's at the first
    // call, then advances with the game's frames (1000 / 60 ms each), so
    // timings follow the game as on the console, also when the game runs
    // faster than real time (the research runner).
    m.service(2900, [this](xq::Call& c) {
        c.result = xq::Value::ofInt(0);
        const int part = c.count() ? c.integer(0) : -1;
        if (part < 0 || part > 6) {
            return false;
        }
        static const auto base = std::chrono::system_clock::now();
        static const long long baseFrame = static_cast<long long>(frame);
        const auto now = base + std::chrono::milliseconds(
                                    (static_cast<long long>(frame) - baseFrame) * 1000 / 60);
        const std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm local{};
        localtime_r(&t, &local);
        const auto ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() %
            1000;
        const int parts[] = {local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour,
                             local.tm_min,         local.tm_sec,     int(ms)};
        c.result = xq::Value::ofInt(parts[part]);
        return true;
    });
    // 6816 (0x1a6324): a model object's bone-motion layer [layer] cleared
    // (0x1e440c: the layer record's motion state reset), every layer without
    // one (0x1e46d8); 6826 (0x1a61f0 -> 0x1e4318): the same for a face
    // motion layer (default 0). Result 1. Puzzle 062 clears them each frame.
    m.service(6816, [this](xq::Call& c) {
        adventure56::Object3D* o = c.count() ? scene.object3D(hashOf(c, 0)) : nullptr;
        if (!o) {
            return false;
        }
        const int layer = c.count() > 1 ? c.integer(1) : -1;
        if (layer < 0 || layer == 0) {
            o->motion.clear();
            o->motionTime = 0;
        }
        for (int l = 1; l <= int(o->upperMotions.size()); ++l) {
            if (layer < 0 || layer == l) {
                o->upperMotions[std::size_t(l - 1)] = {};
            }
        }
        c.result = xq::Value::ofInt(1);
        return true;
    });
    m.service(6826, [this](xq::Call& c) {
        adventure56::Object3D* o = c.count() ? scene.object3D(hashOf(c, 0)) : nullptr;
        if (!o) {
            return false;
        }
        const int layer = c.count() > 1 ? c.integer(1) : 0;
        if (layer >= 0 && layer < int(o->faces.size())) {
            o->faces[std::size_t(layer)].motion.clear();
            o->faces[std::size_t(layer)].time = 0;
        }
        c.result = xq::Value::ofInt(1);
        return true;
    });
    // 20300 (0x1b4404 -> 0x24cfac): a sprite batch's built entry moved
    // (object, index, x, y, degrees): 3573's offset; the degrees x
    // 0.0174533 go to the entry's +0xc, which the drawing does not turn by
    // (005's console frames: its 90 / 180 / 270 pieces are drawn as built;
    // hypothesis: the turn is taken only when the batch is built, 3572).
    // Puzzles 010 / 147 (angle 0).
    // 20302 (0x1b5170) does the same on the batch 20301 handed out
    // (0x1b5004: the object's content, +0x138, when it is a sprite batch,
    // content 9; else 0). The port's handle is the object's hash. Puzzle
    // 005's lines.
    auto movePiece = [this](xq::Call& c) {
        c.result = xq::Value::ofInt(0);
        if (c.count() < 5) {
            return false;
        }
        adventure56::Object* o = scene.object(hashOf(c, 0));
        const int i = c.integer(1);
        if (!o || !o->animation || o->content != 9 || i < 0 || i >= int(o->pieces.size())) {
            return false;
        }
        auto& piece = o->pieces[std::size_t(i)];
        piece.dx = c.real(2);
        piece.dy = c.real(3);
        c.result = xq::Value::ofInt(1);
        return true;
    };
    m.service(20300, movePiece);
    m.service(20302, movePiece);
    m.service(20301, [this](xq::Call& c) {
        const adventure56::Object* o = c.count() ? scene.object(hashOf(c, 0)) : nullptr;
        const bool batch = o && o->content == 9;
        c.result = batch ? xq::Value::ofUint(o->hash) : xq::Value::ofInt(0);
        return batch;
    });
    // 20402 (0x1add7c): a model's mesh (name or hash) shown or not, as 6865
    // (the mesh record's +0xa6), [flag] also passing it on through the
    // record's +0x84 link (0x272d34; not modelled: hypothesis, child
    // meshes). Result 1 when found. Puzzle 110.
    m.service(20402, [this](xq::Call& c) {
        c.result = xq::Value::ofInt(0);
        if (c.count() < 3) {
            return false;
        }
        adventure56::Object3D* o = scene.object3D(hashOf(c, 0));
        const std::uint32_t h = hashOf(c, 1);
        if (!o || !o->model) {
            return false;
        }
        bool found = false;
        for (const auto& mesh : o->model->meshes) {
            found = found || xq::hash(mesh.name) == h;
        }
        if (!found) {
            return false;
        }
        if (c.at(2).truthy()) {
            o->hiddenMeshes.erase(h);
        } else {
            o->hiddenMeshes.insert(h);
        }
        c.result = xq::Value::ofInt(1);
        return true;
    });
    // 6877 (0x19c7f8 -> 0x2519f8 / 0x36e914 -> 0x370ae8): where a bone of a
    // model object is (object, bone name hash, x, y, z registers [, offset x,
    // y, z in the bone's space]); the posed bone's world matrix applied to
    // the offset (as 6898). Result 1 when the bone exists. Puzzle 083.
    m.service(6877, [this](xq::Call& c) {
        c.result = xq::Value::ofInt(0);
        if (c.count() < 5) {
            return false;
        }
        const adventure56::Object3D* o = scene.object3D(hashOf(c, 0));
        if (!o || !o->model) {
            return false;
        }
        const int bone = o->model->bone(hashOf(c, 1));
        if (bone < 0) {
            return true;
        }
        const auto pose = scene.posedModel(*o, *o->model);
        adventure56::Affine w = scene.worldMatrix(*o, 1.0f, true);
        if (std::size_t(bone) < pose.size()) {
            w = w * pose[std::size_t(bone)];
        }
        const float offset[3] = {c.count() >= 8 ? c.real(5) : 0.0f,
                                 c.count() >= 8 ? c.real(6) : 0.0f,
                                 c.count() >= 8 ? c.real(7) : 0.0f};
        const auto p = w.apply(offset);
        store(c, 2, p[0]);
        store(c, 3, p[1]);
        store(c, 4, p[2]);
        c.result = xq::Value::ofInt(1);
        return true;
    });
    // 20401 (0x1a7b84) / 20400 (0x1abc40): a model mesh's value (see
    // Object::meshAlpha) set (object, mesh, value) or tweened (object, mesh,
    // target, frames, curve). Result 1 when the mesh exists.
    auto meshValue = [this](xq::Call& c, bool tween) {
        c.result = xq::Value::ofInt(0);
        if (c.count() < (tween ? 5u : 3u)) {
            return false;
        }
        adventure56::Object3D* o = scene.object3D(hashOf(c, 0));
        const std::uint32_t h = hashOf(c, 1);
        if (!o || !o->model) {
            return false;
        }
        bool found = false;
        for (const auto& mesh : o->model->meshes) {
            found = found || xq::hash(mesh.name) == h;
        }
        if (!found) {
            return false;
        }
        auto [it, fresh] = o->meshAlpha.try_emplace(h);
        if (fresh) {
            it->second.set({1.0f});
        }
        if (tween) {
            it->second.start({c.real(2)}, c.real(3), c.integer(4));
        } else {
            it->second.set({c.real(2)});
        }
        c.result = xq::Value::ofInt(1);
        return true;
    };
    m.service(20400, [meshValue](xq::Call& c) { return meshValue(c, true); });
    m.service(20401, [meshValue](xq::Call& c) { return meshValue(c, false); });
    // 6838 / 5838 (0x1990e0 -> 0x1c95dc -> 0x1dfba8): a model object's
    // material (name or hash) gets a value (object, material, value); 1 when
    // the model has the material. See Object::materialValues (hypothesis).
    m.service(6838, [this](xq::Call& c) {
        c.result = xq::Value::ofInt(0);
        if (c.count() < 3) {
            return false;
        }
        auto hashOf = [&](std::size_t i) {
            const xq::Value& v = c.at(i);
            return v.isText() ? xq::hash(v.text) : v.toUint();
        };
        adventure56::Object3D* o = scene.object3D(hashOf(0));
        if (!o || !o->model) {
            return true;
        }
        const std::uint32_t h = hashOf(1);
        for (const auto& material : o->model->materials) {
            if (material.hash == h) {
                o->materialValues[h] = c.real(2);
                c.result = xq::Value::ofInt(1);
                break;
            }
        }
        return true;
    });
    // The motion sensor (puzzles 037 and 126 count shakes with it).
    // 20500 (0x19b588 -> 0x36affc) starts it: the system's sensor object
    // (global 0x3d9420 +8) through which the input update (0x127a48) reads
    // the accelerometer at the start of every later frame; 20502 (0x19e654
    // -> 0x36afac) stops it, and the last reading stays. 20501 (0x1a7de4 ->
    // 0x249c80) aligns the gyroscope's fusion with gravity; the scripts
    // never read the fusion, so it has nothing to do here. 20503 (0x19b4d8)
    // copies the reading (game +0x548: the raw accelerometer x 1/512, in G)
    // into three registers (result 0 below three operands). All others
    // answer 1.
    auto reading = [this]() {
        const MotionSensor& s = motionSensor;
        return s.on && frame > s.since ? input.acceleration : s.held;
    };
    m.service(20500, [this](xq::Call& c) {
        if (!motionSensor.on) {
            motionSensor.on = true;
            motionSensor.since = frame;
        }
        c.result = xq::Value::ofInt(1);
        return true;
    });
    m.service(20501, [](xq::Call& c) {
        c.result = xq::Value::ofInt(1);
        return true;
    });
    m.service(20502, [this, reading](xq::Call& c) {
        motionSensor.held = reading();
        motionSensor.on = false;
        c.result = xq::Value::ofInt(1);
        return true;
    });
    m.service(20503, [reading](xq::Call& c) {
        c.result = xq::Value::ofInt(0);
        if (c.count() < 3) {
            return false;
        }
        const auto r = reading();
        for (std::size_t i = 0; i < 3; ++i) {
            store(c, i, r[i]);
        }
        c.result = xq::Value::ofInt(1);
        return true;
    });
    // 6878 (0x1b4744 -> 0x24ce48): a model object plays another's
    // animation (object, source; result 1 when the first has a model):
    // model +0x1fc points at the source's. Puzzle 126's fish all follow the
    // hidden "origin". Without a source the link ends; the original then
    // carries the source's layer times over to the object's own layers
    // (0x24ce5c..; not modelled: no script unlinks).
    m.service(6878, [this](xq::Call& c) {
        c.result = xq::Value::ofInt(0);
        if (c.count() < 2) {
            return false;
        }
        adventure56::Object3D* o = scene.object3D(hashOf(c, 0));
        if (!o || !o->model) {
            return true;
        }
        const adventure56::Object3D* source = scene.object3D(hashOf(c, 1));
        o->motionSource = source && source->model ? source->hash : 0;
        c.result = xq::Value::ofInt(1);
        return true;
    });
    // 20004 (0x1ab96c): whether two points lie within a distance (a, b,
    // limit [, register]): 1 when the limit is not below the distance; the
    // distance into the register when given.
    m.service(20004, [](xq::Call& c) {
        c.result = xq::Value::ofInt(0);
        if (c.count() < 7) {
            return false;
        }
        const Vec a = vecAt(c, 0), b = vecAt(c, 3);
        const float d = length({a[0] - b[0], a[1] - b[1], a[2] - b[2]});
        if (!(c.real(6) < d)) {
            c.result = xq::Value::ofInt(1);
        }
        if (c.count() > 7) {
            store(c, 7, d);
        }
        return true;
    });
}

} // namespace picarat::adventure5
