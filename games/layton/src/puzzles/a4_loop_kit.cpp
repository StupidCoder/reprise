// Shared pieces of the special puzzle loops. See a4_loop_kit.hpp.
#include "puzzles/a4_loop_kit.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace picarat::adventure4::kit {

std::string text(const gss::Call& c, std::size_t i) {
    const std::string* t = c.text(i);
    return t ? *t : std::string();
}

Box turnBox(const Box& b, unsigned mirror, int quarter) {
    const int sx = (mirror & 1) ? -1 : 1, sy = (mirror & 2) ? -1 : 1;
    const int x1 = s16(sx * b.x1), x2 = s16(sx * b.x2);
    const int y1 = s16(sy * b.y1), y2 = s16(sy * b.y2);
    int ax = x1, ay = y1, bx = x2, by = y2;
    switch (quarter) {
    case 1:
        ax = s16(-y1), ay = x1, bx = s16(-y2), by = x2;
        break;
    case 2:
        ax = s16(-x1), ay = s16(-y1), bx = s16(-x2), by = s16(-y2);
        break;
    case 3:
        ax = y1, ay = s16(-x1), bx = y2, by = s16(-x2);
        break;
    default:
        break;
    }
    return {std::min(ax, bx), std::min(ay, by), std::max(ax, bx), std::max(ay, by)};
}

unsigned mirrorOf(const Chara& o) {
    const auto s = o.scale.value();
    if (std::abs(s[0]) != kOne || std::abs(s[1]) != kOne) {
        return 3;
    }
    return (s[0] < 0 ? 1u : 0u) | (s[1] < 0 ? 2u : 0u);
}

void setMirror(Chara& o, unsigned mirror) {
    if (mirror > 3) {
        return;
    }
    o.scale.set({(mirror & 1) ? -kOne : kOne, (mirror & 2) ? -kOne : kOne});
}

int quarterOf(const Chara& o) {
    // *Hypothesis*: +0xcc is the step of the rotation's step mode.
    return o.steps.step;
}

bool active(const Chara& o) {
    return o.switches.on(Switches::Master) && o.switches.on(Switches::Update);
}

int screenOf(const Chara& o) {
    return o.visual == Chara::Visual::None ? -1 : o.screen;
}

std::array<int, 2> pixels(const Chara& o) {
    const auto p = o.position.value(), d = o.offset.value();
    return {s16((p[0] + d[0] + 0x800) >> 12), s16((p[1] + d[1] + 0x800) >> 12)};
}

bool sliding(const Chara& o) {
    return o.switches.on(Switches::Master) && o.position.moving() &&
           unsigned(o.position.curve()) < 0x20;
}

bool moving(const Chara& o) {
    return o.switches.on(Switches::Master) &&
           ((o.position.moving() && unsigned(o.position.curve()) < 0x20) ||
            (o.offset.moving() && unsigned(o.offset.curve()) < 0x20));
}

std::vector<Box> obstacles(Scene& scene, int screen, int kind, int areaId, const Chara* exclude,
                           std::size_t max) {
    std::vector<Box> out;
    for (Chara* o : scene.objects()) {
        if (o == exclude || !o->switches.on(Switches::Master) || screenOf(*o) != screen ||
            (kind != -1 && o->kind != kind)) {
            continue;
        }
        const auto at = pixels(*o);
        const unsigned mirror = mirrorOf(*o);
        for (const auto& a : o->touchAreas) {
            if (areaId != -1 && a[4] != areaId) {
                continue;
            }
            const Box b = turnBox(areaCorners(a), mirror, quarterOf(*o));
            out.push_back(
                {s16(b.x1 + at[0]), s16(b.y1 + at[1]), s16(b.x2 + at[0]), s16(b.y2 + at[1])});
            if (out.size() >= max) {
                return out;
            }
        }
    }
    return out;
}

bool overlaps(const Chara& piece, std::array<int, 2> at, const std::vector<Box>& walls) {
    std::vector<Box> own;
    for (const auto& a : piece.touchAreas) {
        if (a[4] == 1 && own.size() < 16) {
            own.push_back({s16(a[0] + at[0]), s16(a[1] + at[1]), s16(a[0] + a[2] + at[0]),
                           s16(a[1] + a[3] + at[1])});
        }
    }
    if (own.empty() || walls.empty()) {
        return false;
    }
    Box all = own[0];
    for (const Box& b : own) {
        all = {std::min(all.x1, b.x1), std::min(all.y1, b.y1), std::max(all.x2, b.x2),
               std::max(all.y2, b.y2)};
    }
    auto overlap = [](const Box& a, const Box& w) {
        return a.x1 < w.x2 && a.y1 < w.y2 && a.x2 > w.x1 && a.y2 > w.y1;
    };
    for (const Box& w : walls) {
        if (!overlap(all, w)) {
            continue;
        }
        if (own.size() < 2) {
            return true;
        }
        for (const Box& b : own) {
            if (overlap(b, w)) {
                return true;
            }
        }
    }
    return false;
}

std::optional<std::array<int, 2>> mainStep(int dx, int dy) {
    if (dx == 0 && dy == 0) {
        return std::nullopt;
    }
    if (std::abs(dx) < std::abs(dy)) {
        return std::array<int, 2>{0, dy < 0 ? -1 : 1};
    }
    return std::array<int, 2>{dx < 0 ? -1 : 1, 0};
}

std::optional<std::array<int, 2>> sideStep(int dx, int dy) {
    if (std::abs(dx) < std::abs(dy)) {
        if (dx == 0) {
            return std::nullopt;
        }
        return std::array<int, 2>{dx < 1 ? -1 : 1, 0};
    }
    if (dy == 0) {
        return std::nullopt;
    }
    return std::array<int, 2>{0, dy < 1 ? -1 : 1};
}

std::array<int, 2> stepToward(std::array<int, 2> at, std::array<int, 2> target) {
    const int dx = s16(target[0] - at[0]), dy = s16(target[1] - at[1]);
    std::array<int, 2> next = at;
    if (std::abs(dx) < std::abs(dy)) {
        const float q = float(dx) / float(dy);
        if (dy < 0) {
            next[1] = s16(next[1] - 1);
            next[0] = s16(next[0] - std::int32_t(q - 0.5f));
        } else {
            next[1] = s16(next[1] + 1);
            next[0] = s16(next[0] + std::int32_t(0.5f + q));
        }
    } else {
        const float q = float(dy) / float(dx);
        if (dx < 0) {
            next[0] = s16(next[0] - 1);
            next[1] = s16(next[1] - std::int32_t(q - 0.5f));
        } else {
            next[0] = s16(next[0] + 1);
            next[1] = s16(next[1] + std::int32_t(0.5f + q));
        }
    }
    return next;
}

void play(Chara& o, const std::string& name) {
    if (o.animation.resource()) {
        o.animation.play(o.animation.resource()->animation->sequence(name), 0);
    }
}

void play(Chara& o, std::uint32_t hash) {
    if (!o.animation.resource()) {
        return;
    }
    for (const auto& s : o.animation.resource()->animation->sequences) {
        if (nameHash(s.name) == hash) {
            o.animation.play(&s, 0);
            return;
        }
    }
}

bool attach(Scene& scene, Chara& o, const std::string& animation, int priority) {
    const AnimationResource* a = scene.animation(animation);
    if (!a) {
        return false;
    }
    o.visual = Chara::Visual::Animation;
    o.animation.attach(a);
    scene.place(o, a->screen, priority);
    return true;
}

std::string valueText(const Chara& o, std::size_t i) {
    if (i >= o.thread.values.size() || o.thread.values[i].kind != gss::Value::Kind::Text) {
        return {};
    }
    return o.thread.values[i].text;
}

int valueInt(const Chara& o, std::size_t i) {
    return i < o.thread.values.size() ? o.thread.values[i].toInt() : 0;
}

namespace {
std::uint64_t floorSqrt(std::uint64_t n) {
    auto r = std::uint64_t(std::sqrt(double(n)));
    while (r * r > n) {
        --r;
    }
    while ((r + 1) * (r + 1) <= n) {
        ++r;
    }
    return r;
}
} // namespace

std::int32_t sqrtFx(std::int64_t v) {
    if (v <= 0) {
        return 0;
    }
    const auto r = floorSqrt(std::uint64_t(std::uint32_t(v)) << 32);
    return std::int32_t((std::uint32_t(r) + 0x200) >> 10);
}

std::int32_t inverseSqrtFx(std::int64_t v) {
    if (v <= 0) {
        return 0;
    }
    const std::uint64_t q = (std::uint64_t(1) << 44) / std::uint64_t(v);
    const auto s = floorSqrt(std::uint64_t(v) << 32);
    return std::int32_t(std::uint32_t((q * s) >> 32) + 0x200) >> 10;
}

} // namespace picarat::adventure4::kit
