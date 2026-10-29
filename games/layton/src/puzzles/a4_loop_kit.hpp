#pragma once
// Small pieces the fourth adventure's special puzzle loops (overlays 18..27)
// share: reading the loop command's operands, objects' boxes as the engine
// sees them (ov6/ov7 routines the overlays call), playing sequences.
#include "system/a4_scene.hpp"
#include <optional>
#include <string>
#include <vector>

namespace picarat::adventure4::kit {

inline std::int16_t s16(int v) {
    return std::int16_t(v);
}

// A text operand of a call (01ffabd8), empty when it is not text.
std::string text(const gss::Call& c, std::size_t i);

// A box in pixels, x2/y2 exclusive.
struct Box {
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
};

// 020ab124: a box (corners x1, y1, x2, y2) after mirroring (bit 0 x, bit 1 y)
// and `quarter` quarter turns.
Box turnBox(const Box& b, unsigned mirror, int quarter);

// A touch area (x, y, w, h, id) as corners.
inline Box areaCorners(const std::array<int, 5>& a) {
    return {a[0], a[1], s16(a[0] + a[2]), s16(a[1] + a[3])};
}

// 0208e5bc: mirroring bits of a scale of exactly +-1 (anything else: 3).
unsigned mirrorOf(const Chara& o);
// 020abe90: the scale set at once to +-1 by the mirroring bits.
void setMirror(Chara& o, unsigned mirror);
// The object's quarter-turn step (+0xcc as 020abd8c reads it).
int quarterOf(const Chara& o);
// lt4CChara vtable +0x24 (020d1bec): master and update switches on.
bool active(const Chara& o);
// 020ac4b4: the screen of what the object shows (-1 nothing).
int screenOf(const Chara& o);
// 020aba64: position + offset in pixels, rounded.
std::array<int, 2> pixels(const Chara& o);
// 020abb88: on its way somewhere (a position tween other than a pulse).
bool sliding(const Chara& o);
// 020abc00: position or offset on their way.
bool moving(const Chara& o);
// 020ddd14: every other live object's touch areas (id `areaId`, -1 any) on
// a screen, of a kind (-1 any), placed where the object is; at most `max`.
std::vector<Box> obstacles(Scene& scene, int screen, int kind, int areaId, const Chara* exclude,
                           std::size_t max = 128);

// 021086fc (ov21) / 02108168 (ov22): whether the piece's own boxes (area
// id 1, at most 16) placed at `at` overlap any of `walls`; their union is
// tested first, then each box when there are several.
bool overlaps(const Chara& piece, std::array<int, 2> at, const std::vector<Box>& walls);
// 0207b8bc / 0207b934 / 0207b828: the main and the side unit step of a move
// (x right, y down); none for a zero move.
std::optional<std::array<int, 2>> mainStep(int dx, int dy);
std::optional<std::array<int, 2>> sideStep(int dx, int dy);
// One pixel of a dragged piece toward `target` along the longer axis, the
// other axis following the slope (single precision, rounded with +-0.5).
std::array<int, 2> stepToward(std::array<int, 2> at, std::array<int, 2> target);

// Sequences of the object's animation by name (020ac6c4) or name hash
// (020ac69c).
void play(Chara& o, const std::string& name);
void play(Chara& o, std::uint32_t hash);
// 020d1b44: shows an animation resource, at a draw priority.
bool attach(Scene& scene, Chara& o, const std::string& animation, int priority);
// A value of the object as text (empty unless it is text).
std::string valueText(const Chara& o, std::size_t i);
int valueInt(const Chara& o, std::size_t i);

// 20.12 arithmetic as the engine does it: products and quotients toward zero
// (the DS divider), the square root unit's root (0200a734, rounded) and
// inverse root (0200a768).
inline std::int32_t mulFx(std::int64_t a, std::int64_t b) {
    return std::int32_t((a * b) / 4096);
}

std::int32_t sqrtFx(std::int64_t v);
std::int32_t inverseSqrtFx(std::int64_t v);

} // namespace picarat::adventure4::kit
