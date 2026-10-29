// 2D vector maths with the original's single-precision operation order (class Vec2D in the
// original: dot @0xf2918, magSqr @0xf28fc, mag @0xf2c64, normalise @0xf2c84).
// Two-term sums are order-independent in IEEE arithmetic; longer sums keep the original order
// at their call sites. Built with -ffp-contract=off so no fused multiply-adds appear.
#pragma once

#include <cmath>

namespace doc::game {

// The original calls the C library's sin and cos one at a time. Compilers like to merge a sin
// and a cos of the same angle into one sincos call, whose result can differ in the last bit;
// these go through the plain library calls (the build turns the sin/cos builtins off).
inline float libSin(float a) { return ::sinf(a); }
inline float libCos(float a) { return ::cosf(a); }
inline double libSin(double a) { return ::sin(a); }
inline double libCos(double a) { return ::cos(a); }

struct Vec2 {
    float x = 0, y = 0;

    friend Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
    friend Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
    friend Vec2 operator*(Vec2 a, float s) { return {a.x * s, a.y * s}; }
    Vec2& operator+=(Vec2 b) { x += b.x; y += b.y; return *this; }
    Vec2& operator-=(Vec2 b) { x -= b.x; y -= b.y; return *this; }
    Vec2& operator*=(float s) { x *= s; y *= s; return *this; }
    friend bool operator==(Vec2 a, Vec2 b) { return a.x == b.x && a.y == b.y; }

    float dot(Vec2 b) const { return x * b.x + y * b.y; }
    float lengthSquared() const { return x * x + y * y; }
    float length() const { return std::sqrt(x * x + y * y); }

    // Scales to `newLength` and returns the previous length; vectors shorter than 1e-4
    // become zero (the original's threshold).
    float normalise(float newLength = 1.0f) {
        float len = std::sqrt(x * x + y * y);
        if (len > 1.0e-4f) {
            float scale = (1.0f / len) * newLength;
            x *= scale;
            y *= scale;
        } else {
            x = 0;
            y = 0;
        }
        return len;
    }

    // Vec2D::rotate @0xf2b00: by `degrees` counter-clockwise; the original converts to radians
    // in single precision and rotates in double precision.
    void rotateDegrees(float degrees) {
        const double r = double(degrees * 0.0174532924f);
        const double s = std::sin(r), c = std::cos(r);
        const double dx = double(x), dy = double(y);
        x = float(c * dx - dy * s);
        y = float(s * dx + dy * c);
    }
    // Vec2D::get2DAngle @0xf2bb4: direction in degrees.
    float angleDegrees() const { return float(std::atan2(double(y), double(x))) * 57.2957764f; }
};

// Map space is y-up with the origin at the bottom-left of the 480x320 play area.
// Sheet row for a heading (getSpriteDirFromAngle @0x19d724): `directions` sectors, the
// first centred on straight up (pi/2), counted anticlockwise.
inline int spriteDirection(float angle, int directions) {
    const float sector = 6.2831855f / float(directions);
    float a = angle + sector * 0.5f;
    a = float(std::fmod(double(a), 6.2831854820251465));
    if (a < 0.0f) a = a + 6.2831855f;
    const int dir = int(a / sector);
    return (dir + 3 * (directions >> 2)) % directions;
}

constexpr float kScreenWidth = 480.0f;
constexpr float kScreenHeight = 320.0f;

}  // namespace doc::game
