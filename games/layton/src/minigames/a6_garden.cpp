// The sixth adventure's Flower Garden: flowers placed on a dry garden bloom
// in a chain reaction from the start cell. See a6_garden.hpp for the rules;
// the routines cited are lt6CLoopMiniGarden's (sixth adventure code).
#include "minigames/a6_garden.hpp"
#include "import/a56_formats.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace picarat::adventure6 {

using adventure5::Game;
using adventure56::Object;
using adventure56::Object3D;
using adventure56::Resource;
using xq::Value;

namespace {

// Hashes the class looks for (motions of the models, cells of the
// animation sets; resolved against the game's files).
constexpr std::uint32_t kWait = 0x7dee83e5, kOpen = 0xa47083a4, kClose = 0x130181c4,
                        kCharge = 0x556ba434, kLapse = 0x74412a85, kWaveIn = 0x8b31701a,
                        kWaveOut = 0x94965622, kHit = 0x0d6e4e9a;
// 0x3b2e28: the shared motions by state (none, resting, opening, closing).
constexpr std::uint32_t kStateMotion[4] = {0, kWait, kOpen, kClose};
// 0x3b2e04: the wave slot for a cell of the 3x3 patch around a red flower.
constexpr int kPatchSlot[9] = {0, 1, 2, 3, 0, 4, 5, 6, 7};
// The flowers' patterns (the model's image motions by colour).
constexpr const char* kPattern[Garden::kColours + 1] = {nullptr, "四角", "十字", "ヨコ", "タテ"};
// 0x3b2d24: the pallet's touch boxes by colour (x, y, w, h from its object).
constexpr int kPalletBox[Garden::kColours + 1][4] = {
    {0, 0, 0, 0}, {297, 60, 24, 24}, {297, 9, 24, 24}, {297, 111, 24, 24}, {297, 162, 24, 24}};
// Cells of the 2D sets (0x3b2d0c ...).
constexpr std::uint32_t kChipB[2] = {0x2e148f46, 0x5913bfd0}, // b_00, b_01
    kChipLit[2] = {0xfddc77d2, 0xfddc77d2},                   // (table +8, +0xc)
    kChipA[2] = {0x3ca120a8, 0x4ba6103e},                     // a_00, a_01
    kChipC[2] = {0x96a8e823, 0xe1afd8b5},                     // c_00, c_01
    kIcon[Garden::kColours + 1] = {0, 0x6f1afedf, 0x70c2f3d6, 0xebfc8038, 0xc5ad20f3},
                        kPalletIcon[Garden::kColours + 1] = {0, 0xecefa32f, 0xee61c4fc, 0xb4d09a3c,
                                                             0x517dd041},
                        kDigit[10] = {0xf4dbdf21, 0x83dcefb7, 0x1ad5be0d, 0x6dd28e9b, 0xf3b61b38,
                                      0x84b12bae, 0x1db87a14, 0x6abf4a82, 0xfa005713, 0x8d076785},
                        kSmallDigit[10] = {0x483f5ba5, 0x3f386b33, 0xa6313a89, 0xd1360a1f,
                                           0x4f529fbc, 0x3855af2a, 0xa15cfe90, 0xd65bce06,
                                           0x46e4d397, 0x31e3e301};
constexpr std::uint32_t kPoisonChip = 0xf9741949, kTreeLitChip = 0xb73e5edc,
                        kTreeDryChip = 0x0c0e4f16, kPoisonIcon = 0x8c25629a,
                        kPalletBase = 0x9ea551e9, kTreesLeftBase = 0x4a8468df;
// 3D grid meshes (0x3b2d74: base_a_00, base_a_01, base_c_00, base_c_01).
constexpr std::uint32_t kGridMesh[4] = {0xaac2ee58, 0xddc5dece, 0x00cb26d3, 0x77cc1645};

std::array<float, 3> cellPoint(int x, int y) {
    return {10.0f + float(x) * 20.0f, 0.0f, 10.0f + float(y) * 20.0f};
}

int intOf(const import::adventure56::Operand& o) {
    if ((o.type & 15) == 3) {
        float f;
        std::memcpy(&f, &o.value, sizeof f);
        return int(f);
    }
    return std::int32_t(o.value);
}

float realOf(const import::adventure56::Operand& o) {
    if ((o.type & 15) == 3) {
        float f;
        std::memcpy(&f, &o.value, sizeof f);
        return f;
    }
    return float(std::int32_t(o.value));
}

std::string objectName(const char* format, int a, int b = -1) {
    char text[40];
    if (b < 0) {
        std::snprintf(text, sizeof text, format, a);
    } else {
        std::snprintf(text, sizeof text, format, a, b);
    }
    return text;
}

// The flowers a garden can keep in the save, by garden and colour
// (0x21c998).
int keptLimit(int garden, int colour) {
    static const int kLimits[11][5] = {{0, 0, 0, 0, 0}, {0, 1, 3, 1, 2}, {0, 2, 3, 3, 0},
                                       {0, 1, 3, 4, 2}, {0, 4, 3, 3, 2}, {0, 4, 5, 3, 3},
                                       {0, 5, 6, 2, 5}, {0, 6, 5, 5, 2}, {0, 7, 5, 7, 5},
                                       {0, 3, 4, 8, 5}, {0, 8, 7, 6, 8}};
    return garden >= 1 && garden <= 10 && colour >= 1 && colour <= 4 ? kLimits[garden][colour] : 0;
}

} // namespace

Garden::Garden(Game& game) : game_(game) {}

// ---------------------------------------------------------------- objects

Object3D* Garden::object3D(std::uint32_t hash) const {
    return hash ? game_.scene.object3D(hash) : nullptr;
}

Object* Garden::object2D(std::uint32_t hash) const {
    return hash ? game_.scene.object(hash) : nullptr;
}

// 0x27e794: a model motion by its name's hash; looping unless once.
void Garden::playMotion(std::uint32_t hash, std::uint32_t motion, bool once) {
    Object3D* o = object3D(hash);
    if (!o || !o->model) {
        return;
    }
    if (motion == 0) {
        o->motion.clear();
        return;
    }
    for (const auto& m : o->model->motions) {
        if (m.hash == motion || xq::hash(m.name) == motion) {
            if (o->motion != m.name || o->motionDone) {
                o->motionTime = 0;
            }
            o->motion = m.name;
            o->motionOnce = once;
            o->motionDone = false;
            o->motionStops = false;
            return;
        }
    }
}

// 0x21bbd4: a planted object's shared motion for its state (0 none, 1
// resting, 2 opening; a tree opening charges), started in step with an
// object of its kind already playing it. *Port*: the shared clock is a copy
// of that object's motion time.
void Garden::syncMotion(int kind, std::uint32_t hash, int state) {
    Object3D* o = object3D(hash);
    if (!o || !o->visible || state < 0 || state > 3) {
        return;
    }
    std::uint32_t want = kStateMotion[state];
    if (kind == 1 && state == 2) {
        want = kCharge;
    }
    if (want == 0) {
        o->motion.clear();
        return;
    }
    const auto& list = kind == 0 ? flowers_ : kind == 1 ? trees_ : poisons_;
    for (const std::uint32_t other : list) {
        const Object3D* p = object3D(other);
        if (other != hash && p && p->model == o->model && !p->motion.empty() &&
            xq::hash(p->motion) == want) {
            o->motion = p->motion;
            o->motionTime = p->motionTime;
            o->motionOnce = false;
            o->motionDone = false;
            return;
        }
    }
    playMotion(hash, want, false);
}

// 0x21bb4c: a wave effect from the pool, coming in.
std::uint32_t Garden::takeWave() {
    for (auto& w : wavePool_) {
        if (w) {
            const std::uint32_t hash = w;
            playMotion(hash, kWaveIn, false);
            w = 0;
            return hash;
        }
    }
    return 0;
}

void Garden::giveBackWave(std::uint32_t hash) {
    if (!hash) {
        return;
    }
    for (auto& w : wavePool_) {
        if (!w) {
            if (Object3D* o = object3D(hash)) {
                o->visible = false;
            }
            w = hash;
            return;
        }
    }
}

// 0x2fecc4 and the like: a flower's object back to its colour's free list,
// its record cleared.
void Garden::returnObject(int colour, std::uint32_t hash) {
    if (!hash || colour < 1 || colour > kColours) {
        return;
    }
    syncMotion(0, hash, 0);
    if (Object3D* o = object3D(hash)) {
        o->visible = false;
    }
    for (auto& f : freeFlowers_[std::size_t(colour)]) {
        if (!f) {
            f = hash;
            break;
        }
    }
    if (auto it = records_.find(hash); it != records_.end()) {
        it->second.waves.fill(0);
        it->second.ended = 0;
    }
}

Garden::Flower* Garden::flowerAt(int x, int y) {
    if (!inGrid(x, y)) {
        return nullptr;
    }
    const auto it = records_.find(objects_[std::size_t(y)][std::size_t(x)]);
    return it == records_.end() ? nullptr : &it->second;
}

// 0x21c8a0: a flower of a colour planted (its object from the free list).
void Garden::plant(int x, int y, int colour) {
    layout_.at(x, y).colour = std::int8_t(colour);
    std::uint32_t hash = 0;
    for (auto& f : freeFlowers_[std::size_t(colour)]) {
        if (f) {
            hash = f;
            f = 0;
            break;
        }
    }
    objects_[std::size_t(y)][std::size_t(x)] = hash;
    if (Object3D* o = object3D(hash)) {
        o->visible = true;
        o->position.set(cellPoint(x, y));
    }
    syncMotion(0, hash, 1);
}

// Taking a flower off a cell (pick-up, all back).
void Garden::releaseFlower(int x, int y) {
    std::uint32_t& hash = objects_[std::size_t(y)][std::size_t(x)];
    returnObject(layout_.at(x, y).colour, hash);
    hash = 0;
}

// 0x2ff5c8 (2110 2): the garden's objects - its trees and poison
// mushrooms where the grid has them, ten flowers of each colour on the
// pallet (hidden), the bloom bursts, the spores and the waves.
void Garden::createObjects() {
    trees_.clear();
    poisons_.clear();
    flowers_.clear();
    records_.clear();
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const Cell& cell = layout_.cells[std::size_t(y)][std::size_t(x)];
            objects_[std::size_t(y)][std::size_t(x)] = 0;
            if (cell.type != WitheredTree && cell.type != Poison) {
                continue;
            }
            const char* res = cell.type == WitheredTree ? "res_tree" : "res_doku";
            const Resource* r = game_.scene.resource(xq::hash(res), Resource::Kind::Model);
            if (!r || !r->model) {
                continue;
            }
            const std::string name = objectName("obj_stage_%d_%d", x, y);
            Object3D& o = game_.scene.create3D(name, 4, 0x6e, 0xe);
            o.model = r->model;
            o.position.set(cellPoint(x, y));
            objects_[std::size_t(y)][std::size_t(x)] = o.hash;
            (cell.type == WitheredTree ? trees_ : poisons_).push_back(o.hash);
            syncMotion(cell.type == WitheredTree ? 1 : 2, o.hash, 1);
        }
    }
    if (const Resource* r = game_.scene.resource(xq::hash("res_flower"), Resource::Kind::Model);
        r && r->model) {
        int index = 0;
        for (int colour = 0; colour <= kColours; ++colour) {
            auto& list = freeFlowers_[std::size_t(colour)];
            list.fill(0);
            for (int i = 0;
                 i < std::min(int(layout_.pallet[std::size_t(colour)]), kFlowersPerColour); ++i) {
                Object3D& o =
                    game_.scene.create3D(objectName("obj_flower_%d_%d", colour, i), 4, 0x6e, 0xe);
                o.model = r->model;
                o.visible = false;
                list[std::size_t(i)] = o.hash;
                flowers_.push_back(o.hash);
                records_[o.hash].index = index++;
                if (colour >= 1) {
                    o.faces[0].motion = kPattern[colour];
                    o.faces[0].time = 0;
                }
            }
        }
    }
    auto pool = [&](const char* res, const char* format, std::uint32_t* out, int count) {
        const Resource* r = game_.scene.resource(xq::hash(res), Resource::Kind::Model);
        for (int i = 0; i < count; ++i) {
            out[i] = 0;
            if (r && r->model) {
                Object3D& o = game_.scene.create3D(objectName(format, i), 4, 0x6e, 0xf);
                o.model = r->model;
                o.visible = false;
                out[i] = o.hash;
            }
        }
    };
    pool("res_eff_flwr_power", "obj_eff_open_%d", bursts_.data(), kBursts);
    pool("res_eff_doku_houshi", "obj_eff_doku_%d", spores_.data(), kBursts);
    pool("res_eff_flwr_wave", "obj_eff_wave_%d", wavePool_.data(), kWaves);
}

// ---------------------------------------------------------------- garden

bool Garden::inGrid(int x, int y) const {
    return x >= 0 && y >= 0 && x < layout_.width && y < layout_.height;
}

// 0x2fd6b4 (2110 1): the garden read, its 2D board centred on (147, 103).
bool Garden::loadGarden(int number) {
    char path[48];
    std::snprintf(path, sizeof path, "res/sub/garden_stage_%02d.xs", number);
    const auto file = game_.pack.find(path, "");
    if (file.empty()) {
        return false;
    }
    const auto list = import::adventure56::parseXscr(game_.pack.read(file));
    Layout g;
    int row = 0, wallRow = 0, wallColumn = 0;
    for (const auto& c : list.commands) {
        auto op = [&](int i) -> const import::adventure56::Operand& {
            return list.operands[std::size_t(c.first + i)];
        };
        switch (c.opcode) {
        case 8010:
            if (c.count >= 2) {
                g.width = std::clamp(intOf(op(0)), 0, kSize);
                g.height = std::clamp(intOf(op(1)), 0, kSize);
            }
            break;
        case 8011:
            for (int i = 0; i < c.count && i < kSize && row < kSize; ++i) {
                g.cells[std::size_t(row)][std::size_t(i)].type = std::int8_t(intOf(op(i)));
            }
            ++row;
            break;
        case 8012:
            for (int i = 0; i < c.count && i < 16 && wallRow < 16; ++i) {
                g.wallLeft[std::size_t(wallRow)][std::size_t(i)] = std::uint8_t(intOf(op(i)));
            }
            ++wallRow;
            break;
        case 8013:
            for (int i = 0; i < c.count && i < 16 && wallColumn < 16; ++i) {
                g.wallAbove[std::size_t(wallColumn)][std::size_t(i)] = std::uint8_t(intOf(op(i)));
            }
            ++wallColumn;
            break;
        case 8014:
            for (int i = 0; i < 4 && i < c.count; ++i) {
                g.pallet[std::size_t(i + 1)] = intOf(op(i)) & 0xff;
            }
            break;
        case 8015:
        case 8016:
            if (c.count >= 11) {
                View& v = g.views[c.opcode - 8015];
                for (int i = 0; i < 3; ++i) {
                    v.eye[std::size_t(i)] = realOf(op(i));
                    v.target[std::size_t(i)] = realOf(op(3 + i));
                }
                v.fov = realOf(op(6)) * 0.0174533f;
                v.stereoDistance = realOf(op(7));
                v.stereoStrength = realOf(op(8));
                v.nearZ = realOf(op(9));
                v.farZ = realOf(op(10));
            }
            break;
        default:
            break;
        }
    }
    loaded_ = g;
    layout_ = g;
    boardX_ = 147.0f - float(g.width) * 22.0f * 0.5f;
    boardY_ = 103.0f - float(g.height) * 22.0f * 0.5f;
    gardenNumber_ = number;
    return g.width > 0;
}

// 0x21c80c: a wave's effect goes out; its slot has ended.
void Garden::endWave(Flower* f, int slot) {
    if (!f || slot < 0 || slot >= Slots) {
        return;
    }
    const std::uint32_t bit = 1u << slot;
    if (f->waves[std::size_t(slot)] && !(f->ended & bit)) {
        playMotion(f->waves[std::size_t(slot)], kWaveOut, true);
        f->ended |= bit;
    }
}

// 0x21c740: a wave reaches a cell: its effect heads there (0x2fc47c), a
// flower not yet in bloom will start its own wave, the cell blooms and
// remembers the flower; in a preview only the preview mark.
void Garden::reach(Flower* f, int slot, int fromX, int fromY, int x, int y, int angle,
                   bool preview) {
    if (preview) {
        preview_[std::size_t(y)][std::size_t(x)] = 1;
        return;
    }
    if (f && slot >= 0 && slot < Slots) {
        if (Object3D* e = object3D(f->waves[std::size_t(slot)])) {
            if (!e->visible) {
                e->visible = true;
                e->position.set(cellPoint(fromX, fromY));
                e->position.start(cellPoint(x, y), waveFrames_, 0);
                e->rotation3.set({0.0f, float(angle) * 0.0174533f, 0.0f});
            } else {
                e->position.start(cellPoint(x, y), waveFrames_, 0);
            }
        }
    }
    Cell& cell = layout_.at(x, y);
    if (cell.colour > 0 && cell.lit == 0) {
        wave_[std::size_t(y)][std::size_t(x)] = 1;
    }
    cell.lit = 1;
    if (f && f->index < 64) {
        reached_[std::size_t(y)][std::size_t(x)] |= Mask(1) << f->index;
    }
}

// 0x21c510 / 0x21c2dc / 0x21c09c / 0x21be5c: a straight wave `distance`
// cells from (x, y) for the flower at (fx, fy). It passes the cells this
// flower already reached; it ends at a wall, a hole, the grid's side or a
// cell in bloom before the step (which blinks); with `stopAtWall` a wall
// leaves the wave running (the red patch's inner spreads).
bool Garden::spread(int x, int y, int dx, int dy, int distance, int fx, int fy, int slot,
                    bool preview, bool stopAtWall) {
    Flower* f = flowerAt(fx, fy);
    if (f && !preview) {
        if (slot < 0 || slot >= Slots || f->waves[std::size_t(slot)] == 0 ||
            (f->ended & (1u << slot))) {
            return false;
        }
    }
    auto end = [&] {
        endWave(f, slot);
        return false;
    };
    const int tx = x + dx * distance, ty = y + dy * distance;
    if (tx < 0 || ty < 0 || tx >= layout_.width || ty >= layout_.height) {
        return end();
    }
    for (int k = 1; k <= distance; ++k) {
        const int cx = x + dx * k, cy = y + dy * k;
        // The wall crossed into this cell.
        bool wall = false;
        if (dx > 0) {
            wall = layout_.wallLeft[std::size_t(cy)][std::size_t(cx)] != 0;
        } else if (dx < 0) {
            wall = layout_.wallLeft[std::size_t(cy)][std::size_t(cx + 1)] != 0;
        } else if (dy > 0) {
            wall = layout_.wallAbove[std::size_t(cy)][std::size_t(cx)] != 0;
        } else {
            wall = layout_.wallAbove[std::size_t(cy + 1)][std::size_t(cx)] != 0;
        }
        if (wall) {
            return stopAtWall ? false : end();
        }
        Cell& cell = layout_.at(cx, cy);
        if (cell.type == Poison) {
            endWave(f, slot);
        } else if (cell.type == NoCell) {
            return end();
        }
        if (!preview) {
            if (cell.colour < 1 && f && f->index < 64 &&
                (reachedAtStep_[std::size_t(cy)][std::size_t(cx)] & (Mask(1) << f->index))) {
                continue;
            }
            if (cellsAtStep_[std::size_t(cy)][std::size_t(cx)].lit > 0) {
                cell.blink = 40;
                return end();
            }
        } else if (cell.colour > 0 && preview_[std::size_t(cy)][std::size_t(cx)] > 0) {
            return end();
        }
    }
    int angle = 0;
    if (dx > 0) {
        angle = 90;
    } else if (dx < 0) {
        angle = 270;
    } else if (dy > 0) {
        angle = 0;
    } else {
        angle = 180;
    }
    reach(f, slot, x, y, tx, ty, angle, preview);
    return true;
}

// 0x2fcd54: a red flower's wave. At distance 1 the four straight cells and
// the four corners (each corner only with no wall on either way round it);
// further on, `distance - 1` rounds in which every cell of the 3x3 patch
// the flower reached spreads to its neighbours inside the patch that were
// not in bloom when the step began.
bool Garden::redSpread(int x, int y, int distance, bool preview) {
    const int left = std::max(x - 1, 0), top = std::max(y - 1, 0);
    const int right = std::min(x + 1, layout_.width - 1),
              bottom = std::min(y + 1, layout_.height - 1);
    Flower* f = flowerAt(x, y);
    bool any = false;
    if (distance == 1) {
        static constexpr int kStraight[4][3] = {
            {0, -1, Up}, {0, 1, Down}, {-1, 0, Left}, {1, 0, Right}};
        for (const auto& s : kStraight) {
            if (spread(x, y, s[0], s[1], 1, x, y, s[2], preview, true)) {
                endWave(f, s[2]);
                any = true;
            }
        }
        auto wallLeft = [&](int cx, int cy) {
            return layout_.wallLeft[std::size_t(cy)][std::size_t(cx)] != 0;
        };
        auto wallAbove = [&](int cx, int cy) {
            return layout_.wallAbove[std::size_t(cy)][std::size_t(cx)] != 0;
        };

        struct Corner {
            int cx, cy, slot, angle;
            bool open;
        };

        const Corner corners[4] = {
            {left, top, UpLeft, 225,
             !wallLeft(x, y) && !wallLeft(x, top) && !wallAbove(x, y) && !wallAbove(left, y)},
            {right, top, UpRight, 135,
             !wallLeft(right, y) && !wallLeft(right, top) && !wallAbove(x, y) &&
                 !wallAbove(right, y)},
            {left, bottom, DownLeft, 315,
             !wallLeft(x, y) && !wallLeft(x, bottom) && !wallAbove(x, bottom) &&
                 !wallAbove(left, bottom)},
            {right, bottom, DownRight, 45,
             !wallLeft(right, y) && !wallLeft(right, bottom) && !wallAbove(x, bottom) &&
                 !wallAbove(right, bottom)}};
        for (const Corner& k : corners) {
            const Cell& was = cellsAtStep_[std::size_t(k.cy)][std::size_t(k.cx)];
            if (was.type == NoCell || !k.open) {
                continue;
            }
            if (was.lit) {
                if (!preview) {
                    layout_.at(k.cx, k.cy).blink = 40;
                }
                continue;
            }
            reach(f, k.slot, x, y, k.cx, k.cy, k.angle, preview);
            endWave(f, k.slot);
            any = true;
        }
        return any;
    }
    for (int round = 0; round < distance - 1; ++round) {
        for (int cy = top; cy <= bottom; ++cy) {
            for (int cx = left; cx <= right; ++cx) {
                if ((cx == x && cy == y) || layout_.at(cx, cy).colour > 0) {
                    continue;
                }
                auto slotFor = [&](int tx, int ty) {
                    return kPatchSlot[(ty - y + 1) * 3 + (tx - x + 1)];
                };
                auto from = [&](int tx, int ty) {
                    return preview ? preview_[std::size_t(ty)][std::size_t(tx)] == 0
                                   : cellsAtStep_[std::size_t(ty)][std::size_t(tx)].lit == 0;
                };
                if (preview) {
                    if (!preview_[std::size_t(cy)][std::size_t(cx)]) {
                        continue;
                    }
                } else {
                    if (!cellsAtStep_[std::size_t(cy)][std::size_t(cx)].lit || !f ||
                        f->index >= 64 ||
                        !(reachedAtStep_[std::size_t(cy)][std::size_t(cx)] &
                          (Mask(1) << f->index))) {
                        continue;
                    }
                }
                if (top <= cy - 1 && from(cx, cy - 1)) {
                    any |= spread(cx, cy, 0, -1, 1, x, y, slotFor(cx, cy - 1), preview, true);
                }
                if (cy + 1 <= bottom && from(cx, cy + 1)) {
                    any |= spread(cx, cy, 0, 1, 1, x, y, slotFor(cx, cy + 1), preview, true);
                }
                if (left <= cx - 1 && from(cx - 1, cy)) {
                    any |= spread(cx, cy, -1, 0, 1, x, y, slotFor(cx - 1, cy), preview, true);
                }
                if (cx + 1 <= right && from(cx + 1, cy)) {
                    any |= spread(cx, cy, 1, 0, 1, x, y, slotFor(cx + 1, cy), preview, true);
                }
            }
        }
    }
    return any;
}

// 0x2ff034 (2110 22): the held flower's whole reach marked where the pen
// is (red: until nothing more; yellow two, blue and purple four cells).
void Garden::previewAt(int x, int y) {
    for (auto& r : preview_) {
        r.fill(0);
    }
    hoverX_ = x;
    hoverY_ = y;
    preview_[std::size_t(y)][std::size_t(x)] = 1;
    switch (held_) {
    case 1:
        for (int d = 1; redSpread(x, y, d, true); ++d) {
        }
        break;
    case 2:
        for (int d = 1; d < 3; ++d) {
            spread(x, y, 0, -1, d, x, y, Up, true, false);
            spread(x, y, 0, 1, d, x, y, Down, true, false);
            spread(x, y, -1, 0, d, x, y, Left, true, false);
            spread(x, y, 1, 0, d, x, y, Right, true, false);
        }
        break;
    case 3:
        for (int d = 1; d < 5; ++d) {
            spread(x, y, -1, 0, d, x, y, Left, true, false);
            spread(x, y, 1, 0, d, x, y, Right, true, false);
        }
        break;
    case 4:
        for (int d = 1; d < 5; ++d) {
            spread(x, y, 0, -1, d, x, y, Up, true, false);
            spread(x, y, 0, 1, d, x, y, Down, true, false);
        }
        break;
    default:
        break;
    }
}

// 0x3002ac (2110 31): the bloom starts - every wave reset, the start cell
// in bloom with its flower's wave at distance 1.
void Garden::start() {
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            Cell& cell = layout_.cells[std::size_t(y)][std::size_t(x)];
            wave_[std::size_t(y)][std::size_t(x)] = cell.colour == 0 ? -1 : 0;
            cell.lit = 0;
            cell.blink = 0;
        }
    }
    for (auto& [hash, f] : records_) {
        for (auto& w : f.waves) {
            giveBackWave(w);
            w = 0;
        }
    }
    for (const std::uint32_t w : wavePool_) {
        if (Object3D* o = object3D(w)) {
            o->visible = false;
        }
    }
    for (int y = 0; y < layout_.height; ++y) {
        for (int x = 0; x < layout_.width; ++x) {
            if (layout_.at(x, y).type == Start) {
                cellsAtStep_ = layout_.cells;
                wave_[std::size_t(y)][std::size_t(x)] = 1;
                layout_.at(x, y).lit = 1;
                showStep();
                for (int py = 0; py < kSize; ++py) {
                    for (int px = 0; px < kSize; ++px) {
                        preview_[std::size_t(py)][std::size_t(px)] =
                            layout_.cells[std::size_t(py)][std::size_t(px)].lit;
                    }
                }
                return;
            }
        }
    }
}

// 0x21aec8: the step shown - cells just in bloom open (the poison sends
// spores, flowers and trees burst and get their waves: red eight, yellow
// four, blue two across, purple two up and down), the others rest; a wave
// done waits for its effects to go out and end. Events: 1 a flower or soil
// burst, 2 a tree.
unsigned Garden::showStep() {
    unsigned events = 0;
    for (const std::uint32_t b : bursts_) {
        if (Object3D* o = object3D(b)) {
            o->visible = false;
        }
    }
    for (const std::uint32_t s : spores_) {
        if (Object3D* o = object3D(s)) {
            o->visible = false;
        }
    }
    int burst = 0, spore = 0;
    auto kindOf = [](int type) {
        return type == WitheredTree ? 1 : type == Poison ? 2 : 0;
    };
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const std::uint32_t hash = objects_[std::size_t(y)][std::size_t(x)];
            if (!hash) {
                continue;
            }
            Cell& cell = layout_.cells[std::size_t(y)][std::size_t(x)];
            if (!cellsAtStep_[std::size_t(y)][std::size_t(x)].lit) {
                if (!cell.lit) {
                    syncMotion(kindOf(cell.type), hash, 1);
                } else {
                    syncMotion(kindOf(cell.type), hash, 2);
                    if (cell.colour < 1 && cell.type != WitheredTree) {
                        if (cell.type == Poison && spore < kBursts) {
                            const std::uint32_t s = spores_[std::size_t(spore++)];
                            if (Object3D* o = object3D(s)) {
                                o->visible = true;
                                o->position.set(cellPoint(x, y));
                            }
                            playMotion(s, kHit, true);
                            game_.playEffect(0x11c);
                        }
                    } else {
                        events |= cell.type == WitheredTree ? 2u : 1u;
                        if (burst < kBursts) {
                            const std::uint32_t b = bursts_[std::size_t(burst++)];
                            if (Object3D* o = object3D(b)) {
                                o->visible = true;
                                o->position.set(cellPoint(x, y));
                            }
                            playMotion(b, kHit, true);
                        }
                        if (Flower* f = flowerAt(x, y)) {
                            static constexpr int kWaveSlots[kColours + 1][8] = {
                                {},
                                {UpLeft, Up, UpRight, Left, Right, DownLeft, Down, DownRight},
                                {Up, Left, Right, Down, -1, -1, -1, -1},
                                {Left, Right, -1, -1, -1, -1, -1, -1},
                                {Up, Down, -1, -1, -1, -1, -1, -1}};
                            if (cell.colour >= 1 && cell.colour <= kColours) {
                                for (const int s : kWaveSlots[cell.colour]) {
                                    if (s >= 0) {
                                        giveBackWave(f->waves[std::size_t(s)]);
                                        f->waves[std::size_t(s)] = takeWave();
                                    }
                                }
                            }
                        }
                    }
                }
            }
            std::int8_t& w = wave_[std::size_t(y)][std::size_t(x)];
            Flower* f = flowerAt(x, y);
            if (waveAtStep_[std::size_t(y)][std::size_t(x)] >= 0 && w == -1) {
                if (f) {
                    w = -2;
                }
            } else if (w == -2 && f) {
                bool settled = true;
                for (int s = 0; s < Slots; ++s) {
                    if (const Object3D* e = object3D(f->waves[std::size_t(s)])) {
                        if (e->visible && (e->position.moving() || e->offset.moving())) {
                            settled = false;
                        } else {
                            endWave(f, s);
                        }
                    }
                }
                if (settled) {
                    w = -3;
                }
            } else if (w == -3 && f) {
                bool cleared = true;
                for (auto& e : f->waves) {
                    if (!e) {
                        continue;
                    }
                    const Object3D* o = object3D(e);
                    if (o && o->visible && !(o->motionOnce && o->motionDone)) {
                        cleared = false;
                    } else {
                        giveBackWave(e);
                        e = 0;
                    }
                }
                if (cleared) {
                    w = -1;
                }
            }
        }
    }
    return events;
}

// 0x2fde70 (2110 32): one step of the bloom. The waves go faster each step
// (their effects move over 12 / speed frames, the speed up by 0.3 to 3). A
// poison mushroom in bloom first withers its 3x3 surroundings (their waves
// and blooms undone); otherwise every flower with a running wave spreads
// one cell further. 1 while anything still moves.
int Garden::step(unsigned& events) {
    speed_ += 0.3f;
    if (int(speed_) >= 3) {
        speed_ = 3.0f;
    }
    waveFrames_ = 12.0f / speed_;
    bool withered = false;
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            Cell& p = layout_.cells[std::size_t(y)][std::size_t(x)];
            if (p.type != Poison || p.lit <= 0) {
                continue;
            }
            const int l = std::max(x - 1, 0), t = std::max(y - 1, 0);
            const int r = std::min(x + 1, layout_.width - 1),
                      b = std::min(y + 1, layout_.height - 1);
            for (int cy = t; cy <= b; ++cy) {
                for (int cx = l; cx <= r; ++cx) {
                    Cell& cell = layout_.at(cx, cy);
                    if (cell.colour > 0 && cell.lit == 1) {
                        wave_[std::size_t(cy)][std::size_t(cx)] = 0;
                        if (Flower* f = flowerAt(cx, cy)) {
                            for (int s = 0; s < Slots; ++s) {
                                endWave(f, s);
                            }
                            for (auto& e : f->waves) {
                                giveBackWave(e);
                                e = 0;
                            }
                            f->ended = 0;
                        }
                    }
                    reached_[std::size_t(cy)][std::size_t(cx)] = 0;
                    cell.lit = 0;
                    cell.blink = 0;
                }
            }
            withered = true;
            p.lit = 0;
            p.blink = 0;
        }
    }
    waveAtStep_ = wave_;
    cellsAtStep_ = layout_.cells;
    reachedAtStep_ = reached_;
    if (!withered) {
        for (int y = 0; y < kSize; ++y) {
            for (int x = 0; x < kSize; ++x) {
                const int d = waveAtStep_[std::size_t(y)][std::size_t(x)];
                if (d <= 0) {
                    continue;
                }
                std::int8_t& w = wave_[std::size_t(y)][std::size_t(x)];
                bool spreads = false;
                switch (layout_.at(x, y).colour) {
                case 1:
                    spreads = redSpread(x, y, d, false);
                    w = std::int8_t(d + 1);
                    break;
                case 2:
                    if (d < 3) {
                        spreads |= spread(x, y, 0, -1, d, x, y, Up, false, false);
                        spreads |= spread(x, y, 0, 1, d, x, y, Down, false, false);
                        spreads |= spread(x, y, -1, 0, d, x, y, Left, false, false);
                        spreads |= spread(x, y, 1, 0, d, x, y, Right, false, false);
                        w = std::int8_t(d + 1);
                    }
                    break;
                case 3:
                    if (d <= 4) {
                        spreads |= spread(x, y, -1, 0, d, x, y, Left, false, false);
                        spreads |= spread(x, y, 1, 0, d, x, y, Right, false, false);
                        w = std::int8_t(d + 1);
                    }
                    break;
                case 4:
                    if (d < 5) {
                        spreads |= spread(x, y, 0, -1, d, x, y, Up, false, false);
                        spreads |= spread(x, y, 0, 1, d, x, y, Down, false, false);
                        w = std::int8_t(d + 1);
                    }
                    break;
                default:
                    break;
                }
                if (!spreads) {
                    w = -1;
                }
            }
        }
    }
    events = showStep();
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            preview_[std::size_t(y)][std::size_t(x)] =
                layout_.cells[std::size_t(y)][std::size_t(x)].lit;
        }
    }
    if (withered) {
        return 1;
    }
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const int w = wave_[std::size_t(y)][std::size_t(x)];
            const Cell& cell = layout_.cells[std::size_t(y)][std::size_t(x)];
            if ((w != 0 && w != -1) || (cell.type == Soil && cell.blink > 1)) {
                return 1;
            }
        }
    }
    return 0;
}

// 0x2fe970 (2110 6): the bloom undone (the flowers stay).
void Garden::undoBloom() {
    cellsAtStep_ = layout_.cells;
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            Cell& cell = layout_.cells[std::size_t(y)][std::size_t(x)];
            cell.lit = 0;
            cell.blink = 0;
            wave_[std::size_t(y)][std::size_t(x)] = 0;
            const std::uint32_t hash = objects_[std::size_t(y)][std::size_t(x)];
            if (!hash) {
                continue;
            }
            if (cell.type == WitheredTree) {
                syncMotion(1, hash, 1);
            } else if (cell.type == Poison) {
                syncMotion(2, hash, 1);
            } else if (cell.type == Soil || cell.type == Start) {
                syncMotion(0, hash, 1);
            }
            if (Flower* f = flowerAt(x, y)) {
                for (int s = 0; s < Slots; ++s) {
                    endWave(f, s);
                }
                for (auto& e : f->waves) {
                    giveBackWave(e);
                    e = 0;
                }
                f->ended = 0;
            }
        }
    }
    held_ = 0;
    heldFromBoard_ = false;
    perfect_ = false;
    treesLeft_ = -1;
    for (auto& r : reached_) {
        r.fill(0);
    }
    for (auto& r : reachedAtStep_) {
        r.fill(0);
    }
    hoverX_ = hoverY_ = -1;
    speed_ = 1;
    waveFrames_ = 12;
    showStep();
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            preview_[std::size_t(y)][std::size_t(x)] =
                layout_.cells[std::size_t(y)][std::size_t(x)].lit;
        }
    }
}

// 0x3000d8 (2110 5): every flower back on the pallet, the garden as read.
void Garden::allBack() {
    for (int y = 0; y < layout_.height; ++y) {
        for (int x = 0; x < layout_.width; ++x) {
            const Cell& cell = layout_.at(x, y);
            if (cell.colour > 0 && cell.type != WitheredTree && cell.type != Poison &&
                objects_[std::size_t(y)][std::size_t(x)]) {
                releaseFlower(x, y);
            }
        }
    }
    layout_ = loaded_;
    held_ = 0;
    heldFromBoard_ = false;
    perfect_ = false;
    treesLeft_ = -1;
    for (auto& r : reached_) {
        r.fill(0);
    }
    for (auto& r : reachedAtStep_) {
        r.fill(0);
    }
    for (auto& r : preview_) {
        r.fill(0);
    }
    hoverX_ = hoverY_ = -1;
    speed_ = 1;
    waveFrames_ = 12;
}

// ---------------------------------------------------------------- views

std::array<float, 2> Garden::objectPosition(std::uint32_t hash) const {
    if (const Object* o = object2D(hash)) {
        return {o->position[0], o->position[1]};
    }
    return {0, 0};
}

// 0x2ff4a8 (2110 4): the camera to the garden's far view (8016) over frames.
void Garden::cameraView(int which, float frames, int curve) {
    const View& v = layout_.views[which];
    auto& cam = game_.scene.cameras[4];
    cam.eye.start(v.eye, frames, curve);
    cam.target.start(v.target, frames, curve);
    cam.fov.start({v.fov}, frames, curve);
    cam.stereoDistance.start({v.stereoDistance}, frames, curve);
    cam.stereoStrength.start({v.stereoStrength}, frames, curve);
    cam.nearZ = v.nearZ;
    cam.farZ = v.farZ;
}

// 0x300728 (2110 7): the camera between the near view (8015) and the far
// one (8016) by how far the bloom has spread from the start: the larger of
// the blooming soil's row and column reach as a share of all the soil's.
// False without a start or with the soil all in one row or column.
bool Garden::closeView(float frames) {
    int sx = -1, sy = -1;
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            if (layout_.cells[std::size_t(y)][std::size_t(x)].type == Start) {
                sx = x;
                sy = y;
            }
        }
    }
    if (sx < 0) {
        return false;
    }
    float rows = 0, columns = 0, litRows = 0, litColumns = 0;
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const Cell& cell = layout_.cells[std::size_t(y)][std::size_t(x)];
            if (cell.type != Soil) {
                continue;
            }
            const float dy = std::fabs(float(sy - y)), dx = std::fabs(float(sx - x));
            rows = std::max(rows, dy);
            columns = std::max(columns, dx);
            if (cell.lit) {
                litRows = std::max(litRows, dy);
                litColumns = std::max(litColumns, dx);
            }
        }
    }
    if (rows == 0 || columns == 0) {
        return false;
    }
    const float share = std::max(litRows / rows, litColumns / columns);
    const View& a = layout_.views[0];
    const View& b = layout_.views[1];
    auto mix = [&](float u, float v) {
        return u * (1.0f - share) + v * share;
    };
    auto& cam = game_.scene.cameras[4];
    cam.eye.start({mix(a.eye[0], b.eye[0]), mix(a.eye[1], b.eye[1]), mix(a.eye[2], b.eye[2])},
                  frames, 1);
    cam.target.start({mix(a.target[0], b.target[0]), mix(a.target[1], b.target[1]),
                      mix(a.target[2], b.target[2])},
                     frames, 1);
    cam.fov.start({mix(a.fov, b.fov)}, frames, 1);
    cam.stereoDistance.start({mix(a.stereoDistance, b.stereoDistance)}, frames, 1);
    cam.stereoStrength.start({mix(a.stereoStrength, b.stereoStrength)}, frames, 1);
    cam.nearZ = mix(a.nearZ, b.nearZ);
    cam.farZ = mix(a.farZ, b.farZ);
    return true;
}

// 0x3014e0 (2110 10): the 3D grid - a tile per cell, a checkerboard of
// base_a, base_c under the preview (or everywhere once perfect).
void Garden::buildGrid3D() {
    game_.scene.remove3D(xq::hash("obj_stage_map"));
    const Resource* r = game_.scene.resource(xq::hash("res_map_m"), Resource::Kind::Model);
    if (!r || !r->model) {
        return;
    }
    Object3D& o = game_.scene.create3D("obj_stage_map", 4, 0x6e, 2);
    auto merged = std::make_shared<Object3D::Merged>();
    merged->model = r->model;
    merged->positionsOnly = true;
    auto meshIndex = [&](std::uint32_t h) {
        const auto& meshes = r->model->meshes;
        for (std::size_t i = 0; i < meshes.size(); ++i) {
            const auto dot = meshes[i].name.rfind('.');
            if (xq::hash(meshes[i].name) == h ||
                (dot != std::string::npos && xq::hash(meshes[i].name.substr(dot + 1)) == h)) {
                return int(i);
            }
        }
        return 0;
    };
    for (int y = 0; y < layout_.height; ++y) {
        for (int x = 0; x < layout_.width; ++x) {
            if (layout_.at(x, y).type <= 0) {
                continue;
            }
            const int parity = (x ^ y) & 1;
            const int lit = preview_[std::size_t(y)][std::size_t(x)] ? 1 : 0;
            const std::uint32_t mesh =
                perfect_ ? kGridMesh[2 + parity] : kGridMesh[lit * 2 + parity];
            Object3D::Merged::Instance in;
            in.mesh = meshIndex(mesh);
            in.position[0] = float(x) * 20.0f;
            in.position[1] = 0.0f;
            in.position[2] = float(y) * 20.0f;
            merged->instances.push_back(in);
        }
    }
    merged->added = merged->instances.size();
    merged->built = !merged->instances.empty();
    o.merged = merged;
}

// 0x300c0c (2110 11): the 2D board on the lower screen, built from chips:
// the soil checkerboard, the preview and blooms, blinking cells, the
// poison's reach while the pen is over it, then the flowers, trees and
// mushrooms on top.
void Garden::buildBoard2D() {
    game_.scene.remove(xq::hash("obj_stage_map_anm"));
    Object& o = game_.scene.create("obj_stage_map_anm", 1, 0x1e, 1);
    if (!game_.setAnimationSet(o, 0x998b876e)) {
        return;
    }
    o.pieces.clear();
    o.pieceCapacity = 0x7fff;
    auto add = [&](std::uint32_t cell, float x, float y) {
        for (std::size_t i = 0; i < o.animation->cells.size(); ++i) {
            if (o.animation->cells[i].hash == cell) {
                o.pieces.push_back({int(i), x, y});
                return;
            }
        }
    };
    auto at = [&](int x, int y) {
        return std::array<float, 2>{float(x) * 22.0f + boardX_, float(y) * 22.0f + boardY_};
    };
    const float frameStep = float(std::max(1, game_.displayFramesPerFrame));
    for (int y = 0; y < layout_.height; ++y) {
        for (int x = 0; x < layout_.width; ++x) {
            Cell& cell = layout_.at(x, y);
            if (cell.type <= 0) {
                continue;
            }
            const int parity = (x ^ y) & 1;
            const auto p = at(x, y);
            const bool flower = cell.colour >= 1 && cell.colour <= kColours;
            if (flower) {
                add(kChipLit[parity], p[0], p[1]);
            }
            if (preview_[std::size_t(y)][std::size_t(x)] < 1) {
                if (cell.type == Poison) {
                    add(kPoisonChip, p[0], p[1]);
                    cell.blink = 0;
                }
            } else if (!held_ || hoverX_ != x || hoverY_ != y) {
                if (cell.blink < 1 || flower || held_) {
                    add(kChipB[parity], p[0], p[1]);
                    cell.blink = 0;
                } else {
                    if (cell.blink > 1) {
                        int b = cell.blink - int(frameStep);
                        cell.blink = std::int8_t(std::max(b, 1));
                        if (b > 1) {
                            add(((int(float(cell.blink) * 0.1f)) & 1) == 0 ? kChipC[0] : kChipC[1],
                                p[0], p[1]);
                            continue;
                        }
                    }
                    add(kChipC[0], p[0], p[1]);
                }
            } else {
                add(kChipLit[parity], p[0], p[1]);
                cell.blink = 0;
            }
        }
    }
    // The poison's surroundings while the preview covers it.
    for (int y = 0; y < layout_.height; ++y) {
        for (int x = 0; x < layout_.width; ++x) {
            if (layout_.at(x, y).type != Poison || preview_[std::size_t(y)][std::size_t(x)] < 1) {
                continue;
            }
            for (int cy = std::max(y - 1, 0); cy <= std::min(y + 1, layout_.height - 1); ++cy) {
                for (int cx = std::max(x - 1, 0); cx <= std::min(x + 1, layout_.width - 1); ++cx) {
                    if (layout_.at(cx, cy).type > 0) {
                        const auto p = at(cx, cy);
                        add(kChipA[(cy % 2 + cx) % 2], p[0], p[1]);
                    }
                }
            }
        }
    }
    for (int y = 0; y < layout_.height; ++y) {
        for (int x = 0; x < layout_.width; ++x) {
            const Cell& cell = layout_.at(x, y);
            if (cell.type <= 0) {
                continue;
            }
            const auto p = at(x, y);
            if (cell.colour >= 1 && cell.colour <= kColours) {
                add(kIcon[cell.colour], p[0], p[1]);
            }
            if (cell.type == WitheredTree) {
                add(cell.lit ? kTreeLitChip : kTreeDryChip, p[0], p[1]);
            }
            if (cell.type == Poison) {
                add(kPoisonIcon, p[0], p[1]);
            }
        }
    }
    o.position.set({0, 0, 0});
}

// 0x2fed84 (2110 12): the pallet - its frame, each colour's icon and how
// many are left.
void Garden::buildPallet() {
    game_.scene.remove(xq::hash("obj_menu_anm"));
    Object& o = game_.scene.create("obj_menu_anm", 1, 0x1e, 1);
    if (!game_.setAnimationSet(o, 0xa11861d0)) {
        return;
    }
    o.pieces.clear();
    o.pieceCapacity = 0x7fff;
    auto add = [&](std::uint32_t cell, float x, float y) {
        for (std::size_t i = 0; i < o.animation->cells.size(); ++i) {
            if (o.animation->cells[i].hash == cell) {
                o.pieces.push_back({int(i), x, y});
                return;
            }
        }
    };
    add(kPalletBase, 0, 210);
    static constexpr float kRow[kColours + 1] = {0, 51, 0, 102, 153};
    for (int colour = 1; colour <= kColours; ++colour) {
        add(kPalletIcon[colour], 297.0f, kRow[colour] + 9.0f);
        const int n = layout_.pallet[std::size_t(colour)];
        if (n >= 0 && n < 10) {
            add(kDigit[n], 302.0f, kRow[colour] + 35.0f);
        }
    }
}

// 0x30052c (2110 13): the withered trees still dry (the count only after a
// bloom, `count`).
void Garden::buildTreesLeft(bool count) {
    game_.scene.remove(xq::hash("obj_rest_tree_anm"));
    Object& o = game_.scene.create("obj_rest_tree_anm", 1, 0x1e, 1);
    if (!game_.setAnimationSet(o, 0xa11861d0)) {
        return;
    }
    o.pieces.clear();
    o.pieceCapacity = 0x7fff;
    auto add = [&](std::uint32_t cell, float x, float y) {
        for (std::size_t i = 0; i < o.animation->cells.size(); ++i) {
            if (o.animation->cells[i].hash == cell) {
                o.pieces.push_back({int(i), x, y});
                return;
            }
        }
    };
    add(kTreesLeftBase, 0, 0);
    if (count && treesLeft_ >= 0 && treesLeft_ < 10) {
        add(kSmallDigit[treesLeft_], 39.0f, -1.0f);
    }
}

// ---------------------------------------------------------------- commands

bool Garden::command(int number, xq::Call& c, std::size_t first) {
    const std::size_t n = c.count() > first ? c.count() - first : 0;
    // 0x2fc65c starts every command with the result 0; most leave it so.
    c.result = Value::ofInt(0);
    auto result = [&](int v) {
        c.result = Value::ofInt(v);
        return true;
    };
    auto handled = [&] {
        return true;
    };
    auto out = [&](std::size_t i, const Value& v) {
        if (i < n) {
            if (Value* r = c.reg(first + i)) {
                *r = v;
            }
        }
    };
    auto cellUnder = [&](int px, int py, int& cx, int& cy) {
        const auto base = objectPosition(xq::hash("obj_stage_map_anm"));
        for (int y = 0; y < layout_.height; ++y) {
            for (int x = 0; x < layout_.width; ++x) {
                const int t = layout_.at(x, y).type;
                if (t == NoCell || t == WitheredTree || t == Poison) {
                    continue;
                }
                const int x0 = int(base[0] + boardX_ + float(x) * 22.0f);
                const int y0 = int(base[1] + boardY_ + float(y) * 22.0f);
                if (x0 <= px && !(float(x0) + 22.0f <= float(px)) && y0 <= py &&
                    !(float(y0) + 22.0f <= float(py))) {
                    cx = x;
                    cy = y;
                    return true;
                }
            }
        }
        return false;
    };
    switch (number) {
    case 1:
        if (n < 1) {
            return false;
        }
        loadGarden(c.integer(first));
        return handled();
    case 2:
        createObjects();
        return handled();
    case 3: // 0x115c18: the state cleared
        layout_ = {};
        loaded_ = {};
        objects_ = {};
        wave_ = {};
        waveAtStep_ = {};
        preview_ = {};
        reached_ = {};
        reachedAtStep_ = {};
        held_ = 0;
        heldFromBoard_ = false;
        perfect_ = false;
        treesLeft_ = -1;
        hoverX_ = hoverY_ = -1;
        speed_ = 1;
        waveFrames_ = 12;
        boardX_ = boardY_ = 0;
        return handled();
    case 4:
        cameraView(1, n > 0 ? c.real(first) : 0.0f, 0);
        return handled();
    case 5:
        allBack();
        return handled();
    case 6:
        undoBloom();
        return handled();
    case 7:
        return n > 0 && closeView(c.real(first));
    case 8: { // the flowers placed kept for the garden
        auto& kept = kept_[n > 0 ? c.integer(first) : gardenNumber_];
        kept.clear();
        for (int y = 0; y < layout_.height; ++y) {
            for (int x = 0; x < layout_.width; ++x) {
                if (const int colour = layout_.at(x, y).colour; colour >= 1 && colour <= kColours) {
                    kept.push_back({colour, x, y});
                }
            }
        }
        return handled();
    }
    case 9: { // 0x2fc008: the flowers kept for the garden planted again
        const int garden = n > 0 ? c.integer(first) : gardenNumber_;
        const auto it = kept_.find(garden);
        if (it != kept_.end()) {
            int used[kColours + 1] = {};
            for (const auto& [colour, x, y] : it->second) {
                if (used[colour] < keptLimit(garden, colour) &&
                    layout_.pallet[std::size_t(colour)] > 0 && inGrid(x, y)) {
                    ++used[colour];
                    --layout_.pallet[std::size_t(colour)];
                    plant(x, y, colour);
                }
            }
        }
        return handled();
    }
    case 10:
        buildGrid3D();
        return handled();
    case 11:
        buildBoard2D();
        return handled();
    case 12:
        buildPallet();
        return handled();
    case 13: { // the dry trees left (-1 without the operand's switch), the
               // counter rebuilt when it changes
        if (n < 1) {
            return false;
        }
        int left = -1;
        const bool show = c.integer(first) != 0;
        if (show) {
            left = 0;
            for (int y = 0; y < layout_.height; ++y) {
                for (int x = 0; x < layout_.width; ++x) {
                    left += layout_.at(x, y).type == WitheredTree && !layout_.at(x, y).lit;
                }
            }
            if (left == treesLeft_) {
                return handled();
            }
        }
        treesLeft_ = left;
        buildTreesLeft(show);
        return handled();
    }
    case 20: { // 0x2fe594: pick up from the pallet, or a flower off the board
        if (n < 2) {
            return false;
        }
        if (held_ == 0) {
            const int px = c.integer(first), py = c.integer(first + 1);
            heldFromBoard_ = false;
            if (object2D(xq::hash("obj_menu_anm"))) {
                const auto base = objectPosition(xq::hash("obj_menu_anm"));
                for (int colour = 0; colour <= kColours; ++colour) {
                    const auto& b = kPalletBox[colour];
                    if (float(px) >= base[0] + float(b[0]) &&
                        float(px) < base[0] + float(b[0] + b[2]) &&
                        float(py) >= base[1] + float(b[1]) &&
                        float(py) < base[1] + float(b[1] + b[3])) {
                        if (layout_.pallet[std::size_t(colour)] > 0) {
                            held_ = colour;
                            --layout_.pallet[std::size_t(colour)];
                        }
                        break;
                    }
                }
            }
            int cx = 0, cy = 0;
            if (held_ == 0 && object2D(xq::hash("obj_stage_map_anm")) &&
                cellUnder(px, py, cx, cy)) {
                held_ = layout_.at(cx, cy).colour;
                heldX_ = cx;
                heldY_ = cy;
                heldFromBoard_ = true;
                releaseFlower(cx, cy);
                layout_.at(cx, cy).colour = 0;
            }
            return result(held_);
        }
        return handled();
    }
    case 21: { // 0x2fd96c: put down - on a flower it swaps (or sends the
               // pallet's back), over the pallet a flower from the board goes
               // back, elsewhere it returns where it came from
        if (n < 2) {
            return false;
        }
        if (held_ == 0) {
            return handled();
        }
        const int px = c.integer(first), py = c.integer(first + 1);
        int cx = 0, cy = 0;
        if (object2D(xq::hash("obj_stage_map_anm")) && cellUnder(px, py, cx, cy)) {
            Cell& cell = layout_.at(cx, cy);
            if (cell.colour >= 1) {
                if (heldFromBoard_) {
                    // The flower there moves to where the held one came from.
                    layout_.at(heldX_, heldY_).colour = cell.colour;
                    std::uint32_t& from = objects_[std::size_t(heldY_)][std::size_t(heldX_)];
                    from = objects_[std::size_t(cy)][std::size_t(cx)];
                    if (Object3D* o = object3D(from)) {
                        o->position.set(cellPoint(heldX_, heldY_));
                    }
                    objects_[std::size_t(cy)][std::size_t(cx)] = 0;
                } else {
                    ++layout_.pallet[std::size_t(cell.colour)];
                    releaseFlower(cx, cy);
                }
            }
            plant(cx, cy, held_);
        } else if (heldFromBoard_) {
            if (float(px) > 297.0f) {
                ++layout_.pallet[std::size_t(held_)];
            } else {
                plant(heldX_, heldY_, held_);
            }
        } else {
            ++layout_.pallet[std::size_t(held_)];
        }
        held_ = 0;
        heldX_ = heldY_ = 0;
        heldFromBoard_ = false;
        return handled();
    }
    case 22: { // 0x2ff034: the preview under the pen (1 when it shows one)
        if (n < 2) {
            return false;
        }
        auto clear = [&] {
            for (auto& r : preview_) {
                r.fill(0);
            }
            hoverX_ = hoverY_ = -1;
        };
        if (held_ != 0) {
            const Object* map = object2D(xq::hash("obj_stage_map_anm"));
            if (!map) {
                return handled();
            }
            const float fx = float(c.integer(first)) - map->position[0] - boardX_;
            const float fy = float(c.integer(first + 1)) - map->position[1] - boardY_;
            if (fx < 0 || fy < 0) {
                return handled();
            }
            const int x = int(fx * 0.0454545f), y = int(fy * 0.0454545f);
            const int t =
                x < kSize && y < kSize ? layout_.cells[std::size_t(y)][std::size_t(x)].type : 0;
            if (t != NoCell && t != WitheredTree && t != Poison) {
                if (x == hoverX_ && y == hoverY_) {
                    return handled();
                }
                previewAt(x, y);
                return result(1);
            }
        }
        if (hoverX_ >= 0 && hoverY_ >= 0) {
            c.result = Value::ofInt(1);
        }
        clear();
        return handled();
    }
    case 30: { // a flower on the start
        for (int y = 0; y < layout_.height; ++y) {
            for (int x = 0; x < layout_.width; ++x) {
                if (layout_.at(x, y).type == Start && layout_.at(x, y).colour > 0) {
                    return result(1);
                }
            }
        }
        return result(0);
    }
    case 31:
        start();
        return handled();
    case 32: {
        unsigned events = 0;
        const int more = step(events);
        out(0, Value::ofInt(int(events)));
        return result(more);
    }
    case 33: { // the verdict: 0 trees left dry, 1 done, 2 the whole garden
        for (int y = 0; y < layout_.height; ++y) {
            for (int x = 0; x < layout_.width; ++x) {
                if (layout_.at(x, y).type == WitheredTree && !layout_.at(x, y).lit) {
                    return result(0);
                }
            }
        }
        for (int y = 0; y < layout_.height; ++y) {
            for (int x = 0; x < layout_.width; ++x) {
                if (layout_.at(x, y).type == Soil && !layout_.at(x, y).lit) {
                    return result(1);
                }
            }
        }
        perfect_ = true;
        return result(2);
    }
    case 34: // the poison mushrooms fade, their cells become soil in bloom
        for (int y = 0; y < kSize; ++y) {
            for (int x = 0; x < kSize; ++x) {
                Cell& cell = layout_.cells[std::size_t(y)][std::size_t(x)];
                if (cell.type == Poison && objects_[std::size_t(y)][std::size_t(x)]) {
                    playMotion(objects_[std::size_t(y)][std::size_t(x)], kLapse, true);
                    cell.type = Soil;
                    cell.lit = 1;
                }
            }
        }
        return handled();
    case 35: { // 0x3017a0: everything settled (no wave moving, no tree opening)
        for (int y = 0; y < kSize; ++y) {
            for (int x = 0; x < kSize; ++x) {
                const std::uint32_t hash = objects_[std::size_t(y)][std::size_t(x)];
                const Object3D* o = object3D(hash);
                if (!o) {
                    continue;
                }
                bool moving = false;
                if (const auto it = records_.find(hash); it != records_.end()) {
                    for (const std::uint32_t w : it->second.waves) {
                        const Object3D* e = object3D(w);
                        moving |= e && e->visible && (e->position.moving() || e->offset.moving());
                    }
                }
                if (xq::hash(o->motion) == kOpen || moving) {
                    return result(0);
                }
            }
        }
        return result(1);
    }
    case 36: { // 0x2febac: trees in bloom open (in step); any
        bool any = false;
        for (int y = 0; y < kSize; ++y) {
            for (int x = 0; x < kSize; ++x) {
                const std::uint32_t hash = objects_[std::size_t(y)][std::size_t(x)];
                const Cell& cell = layout_.cells[std::size_t(y)][std::size_t(x)];
                if (hash && cell.type == WitheredTree && cell.lit) {
                    any = true;
                    if (Object3D* o = object3D(hash)) {
                        o->motionTime = 0; // flag 4: from the start, all in step
                    }
                    playMotion(hash, kOpen, false);
                }
            }
        }
        return result(any ? 1 : 0);
    }
    case 37: { // a tree still playing "open"
        for (int y = 0; y < kSize; ++y) {
            for (int x = 0; x < kSize; ++x) {
                const Object3D* o = object3D(objects_[std::size_t(y)][std::size_t(x)]);
                if (o && layout_.cells[std::size_t(y)][std::size_t(x)].type == WitheredTree &&
                    xq::hash(o->motion) == kOpen) {
                    return result(1);
                }
            }
        }
        return result(0);
    }
    default:
        return false;
    }
}

std::string Garden::describe() const {
    std::ostringstream o;
    o << "garden " << gardenNumber_ << " " << layout_.width << "x" << layout_.height << " held "
      << held_ << " pallet";
    for (int k = 1; k <= kColours; ++k) {
        o << " " << layout_.pallet[std::size_t(k)];
    }
    o << " speed " << speed_ << "\n";
    for (int y = 0; y < layout_.height; ++y) {
        for (int x = 0; x < layout_.width; ++x) {
            const Cell& cell = layout_.at(x, y);
            o << int(cell.type) << (cell.colour ? char('0' + cell.colour) : '.')
              << (cell.lit ? '*' : ' ') << int(wave_[std::size_t(y)][std::size_t(x)]) << " ";
        }
        o << "\n";
    }
    for (int y = 0; y < layout_.height; ++y) {
        for (int x = 0; x < layout_.width; ++x) {
            const Object3D* t = object3D(objects_[std::size_t(y)][std::size_t(x)]);
            if (t && layout_.at(x, y).type == WitheredTree) {
                o << "tree " << x << "," << y << " " << t->motion << " " << t->motionTime
                  << (t->motionDone ? " done" : "") << "\n";
            }
        }
    }
    return o.str();
}

} // namespace picarat::adventure6
