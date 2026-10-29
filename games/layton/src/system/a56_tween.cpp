// Tween curves of the 3DS adventures' engine. See a56_tween.hpp.
#include "system/a56_tween.hpp"
#include <cmath>

namespace picarat::adventure56 {

float tweenCurve(int curve, float t) {
    constexpr float kPi = 3.14159265358979f;
    switch (curve) {
    case 1:
        return t * t;
    case 2:
        return 1.0f - (1.0f - t) * (1.0f - t);
    case 3:
        return t > 0.5f ? 1.0f - (1.0f - t) * (1.0f - t) * 2.0f : t * t * 2.0f;
    case 4:
        // The angle goes through the engine's 16-bit circle units.
        return std::sin(float(int(t * 0.5f * 65536.0f + 0.5f) & 0xffff) * (2 * kPi / 65536.0f));
    case 5:
        return (1.0f - t) * t * 4.0f;
    case 0x21:
        return 0.5f - std::cos(float(int(t * 65536.0f + 0.5f) & 0xffff) * (2 * kPi / 65536.0f)) *
                          0.5f;
    default:
        // 0 and 0x20 linear; 0x22..0x24 (random) and other numbers are
        // approximated linearly (hypothesis; not used by the puzzle frame).
        return t;
    }
}

} // namespace picarat::adventure56
