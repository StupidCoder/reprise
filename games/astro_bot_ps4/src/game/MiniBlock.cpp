#include "game/MiniBlock.hpp"

#include "data/Bytes.hpp"
#include "data/Xml.hpp"
#include "game/LevelDocument.hpp"

#include <algorithm>
#include <cmath>

namespace beacon::game {

using math::Vec3;

namespace {
constexpr float kPi = 3.14159265f;
constexpr float kSpreadDelay = 0.05f;   // 0x580960: neighbours break 0.05 s / BreakSpeed later
constexpr float kFirstDelay = 1.1920929e-07f;   // 0x582840: the hit cube goes on its next update
}

MiniBlockTuning loadMiniBlockTuning(const std::filesystem::path& root) {
    MiniBlockTuning t;
    data::Bytes bytes;
    if (!data::loadFile(root / "data" / "multi_platformer" / "multi_platformer.xml", bytes)) {
        return t;
    }
    auto xml = data::parseXml(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    const data::XmlNode* m = xml ? xml->child("MiniBlock") : nullptr;
    auto read = [m](const char* name, float& value) {
        if (m) {
            if (auto text = m->childText(name)) {
                data::parseFloatList(*text, &value, 1);
            }
        }
    };
    read("Impluse", t.impulse);
    read("HorizontalAngleMin", t.horizontalMin);
    read("HorizontalAngleMax", t.horizontalMax);
    read("VerticalAngleMin", t.verticalMin);
    read("VerticalAngleMax", t.verticalMax);
    read("Restitution", t.restitution);
    read("BreakRotation", t.breakRotation);
    read("DisappearEffectDelay", t.disappearDelay);
    return t;
}

// 0x57ca00: the edge is the box's height / VerticalDivision (the model is a unit cube); along
// each axis floor(size / edge) cubes, at least one; cube centres (i + 0.5) edges from the box's
// corner (the placement's origin) along its axes (run roof_g2: all 80 cubes of ROOFTOPS' three
// walls), each cube linked to its six neighbours.
MiniBlock::MiniBlock(std::size_t placement, const LevelObject& o, const math::Mat4& world, const MiniBlockTuning& t)
    : placement_(placement), world_(world), t_(t),
      random_(std::uint32_t(o.number("Seed").value_or(5489.0f))) {
    const int division = std::max(1, int(o.number("VerticalDivision").value_or(1)));
    breakSpeed_ = std::max(o.number("BreakSpeed").value_or(1.0f), 1.1920929e-07f);
    attackable_ = o.flag("Attackable").value_or(true);
    const bool round = o.text("DivisionNumberFilter") == "Round";
    float size[3];
    for (int a = 0; a < 3; ++a) {
        size[a] = std::sqrt(world[a * 4] * world[a * 4] + world[a * 4 + 1] * world[a * 4 + 1] + world[a * 4 + 2] * world[a * 4 + 2]);
    }
    edge_ = size[1] / float(division);
    for (int a = 0; a < 3; ++a) {
        const float n = size[a] / edge_;
        count_[std::size_t(a)] = std::max(1, int(round ? std::round(n) : std::floor(n + 1e-4f)));
    }
    auto index = [this](int x, int y, int z) { return (y * count_[2] + z) * count_[0] + x; };
    cubes_.resize(std::size_t(count_[0] * count_[1] * count_[2]));
    for (int y = 0; y < count_[1]; ++y) {
        for (int z = 0; z < count_[2]; ++z) {
            for (int x = 0; x < count_[0]; ++x) {
                Cube& c = cubes_[std::size_t(index(x, y, z))];
                // In box units: (i + 0.5) edges from the box's corner (its origin) along each axis.
                c.local = {(float(x) + 0.5f) * edge_ / size[0], (float(y) + 0.5f) * edge_ / size[1],
                           (float(z) + 0.5f) * edge_ / size[2]};
                c.neighbours = {x > 0 ? index(x - 1, y, z) : -1, x + 1 < count_[0] ? index(x + 1, y, z) : -1,
                                y > 0 ? index(x, y - 1, z) : -1, y + 1 < count_[1] ? index(x, y + 1, z) : -1,
                                z > 0 ? index(x, y, z - 1) : -1, z + 1 < count_[2] ? index(x, y, z + 1) : -1};
            }
        }
    }
}

math::Mat4 MiniBlock::cubeWorld(int i) const {
    const Vec3& l = cubes_[std::size_t(i)].local;
    const math::Mat4& w = world_;
    const Vec3 p{w[0] * l.x + w[4] * l.y + w[8] * l.z + w[12], w[1] * l.x + w[5] * l.y + w[9] * l.z + w[13],
                 w[2] * l.x + w[6] * l.y + w[10] * l.z + w[14]};
    math::Mat4 m = w;   // the box's rotation, the cube's edge as scale
    for (int a = 0; a < 3; ++a) {
        const float len = std::sqrt(w[a * 4] * w[a * 4] + w[a * 4 + 1] * w[a * 4 + 1] + w[a * 4 + 2] * w[a * 4 + 2]);
        for (int r = 0; r < 3; ++r) {
            m[a * 4 + r] = w[a * 4 + r] / std::max(len, 1e-6f) * edge_;
        }
    }
    m[12] = p.x;
    m[13] = p.y;
    m[14] = p.z;
    return m;
}

bool MiniBlock::hit(const Vec3& point, const Vec3& direction) {
    if (!attackable_) {
        return false;
    }
    int best = -1;
    float bestDistance = 3.4028235e38f;
    for (std::size_t i = 0; i < cubes_.size(); ++i) {
        if (cubes_[i].hits > 0) {
            continue;
        }
        const math::Mat4 w = cubeWorld(int(i));
        const Vec3 d{w[12] - point.x, w[13] - point.y, w[14] - point.z};
        const float distance = math::length(d);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = int(i);
        }
    }
    if (best < 0) {
        return false;
    }
    breakCube(best, kFirstDelay, direction);
    return true;
}

void MiniBlock::breakCube(int i, float delay, const Vec3& direction) {
    Cube& c = cubes_[std::size_t(i)];
    if (c.hits++ > 0) {
        return;   // only the first request counts
    }
    c.delay = delay;
    c.direction = direction;
}

std::vector<MiniBlock::Launch> MiniBlock::update(float dt, std::vector<OutsideBreak>& outside) {
    std::vector<Launch> launches;
    std::uniform_real_distribution<float> unit(0, 1), sym(-1, 1);
    for (std::size_t i = 0; i < cubes_.size(); ++i) {
        Cube& c = cubes_[i];
        if (c.hits > 0 && !c.flying && !c.gone && c.delay > 0) {
            c.delay -= dt;
            if (c.delay <= 0) {
                // The push: the hit's direction turned by a random angle across (about up) and a
                // random angle up/down within the tuning's ranges, Impluse strong, for one step;
                // a random spin. **[guess]** how the two angles compose (0x580960 not followed).
                Vec3 dir = c.direction;
                dir.y = 0;
                if (math::length(dir) < 1e-5f) {
                    dir = {0, 0, 1};
                }
                dir = math::normalize(dir, {0, 0, 1});
                const float h = (t_.horizontalMin + unit(random_) * (t_.horizontalMax - t_.horizontalMin)) * kPi / 180;
                const float v = (t_.verticalMin + unit(random_) * (t_.verticalMax - t_.verticalMin)) * kPi / 180;
                const Vec3 across{dir.x * std::cos(h) + dir.z * std::sin(h), 0, -dir.x * std::sin(h) + dir.z * std::cos(h)};
                const Vec3 push = across * std::cos(v) - Vec3{0, std::sin(v), 0};
                Vec3 spin;
                do {
                    spin = {sym(random_), sym(random_), sym(random_)};
                } while (math::dot(spin, spin) >= 1 || math::dot(spin, spin) < 1e-6f);
launches.push_back({int(i), push * t_.impulse, math::normalize(spin, {0, 1, 0}) * t_.breakRotation});
                c.flying = true;
                c.life = t_.disappearDelay;
                for (int n : c.neighbours) {
                    if (n >= 0) {
                        breakCube(n, kSpreadDelay / breakSpeed_, c.direction);
                    }
                }
                if (c.outsideBlock >= 0) {
                    outside.push_back({c.outsideBlock, c.outsideCube, kSpreadDelay / breakSpeed_, c.direction});
                }
            }
        } else if (c.flying && !c.gone) {
            c.life -= dt;
            if (c.life <= 0) {
                c.gone = true;
            }
        }
    }
    return launches;
}

}   // namespace game
