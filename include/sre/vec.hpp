// Minimal 3D vector / matrix math for the Schwarzschild Ray Engine.
//
// Deliberately dependency-free: the engine should build with nothing but a
// C++17 compiler so it can be dropped onto any machine (or a CI runner)
// without a package manager in the loop.
#pragma once

#include <cmath>

namespace sre {

struct Vec3 {
    double x = 0.0, y = 0.0, z = 0.0;

    constexpr Vec3() = default;
    constexpr Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}

    constexpr Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator-() const { return {-x, -y, -z}; }
    constexpr Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    constexpr Vec3 operator/(double s) const { return {x / s, y / s, z / s}; }

    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(double s) { x *= s; y *= s; z *= s; return *this; }
};

constexpr Vec3 operator*(double s, const Vec3& v) { return v * s; }

constexpr double dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

constexpr Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}

inline double length(const Vec3& v) { return std::sqrt(dot(v, v)); }
constexpr double length2(const Vec3& v) { return dot(v, v); }

inline Vec3 normalize(const Vec3& v) {
    const double n = length(v);
    return n > 0.0 ? v / n : Vec3{};
}

// Component-wise helpers, handy for colour work where Vec3 doubles as RGB.
constexpr Vec3 mul(const Vec3& a, const Vec3& b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }

inline Vec3 lerp(const Vec3& a, const Vec3& b, double t) { return a + (b - a) * t; }

/// Rotate `v` about a unit axis by `angle` radians (Rodrigues' formula).
inline Vec3 rotateAxis(const Vec3& v, const Vec3& axis, double angle) {
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    return v * c + cross(axis, v) * s + axis * (dot(axis, v) * (1.0 - c));
}

/// Build an orthonormal basis whose third vector is `n`.
inline void orthonormalBasis(const Vec3& n, Vec3& t, Vec3& b) {
    // Duff et al., "Building an Orthonormal Basis, Revisited" (JCGT 2017).
    const double sign = std::copysign(1.0, n.z);
    const double a = -1.0 / (sign + n.z);
    const double d = n.x * n.y * a;
    t = {1.0 + sign * n.x * n.x * a, sign * d, -sign * n.x};
    b = {d, sign + n.y * n.y * a, -n.y};
}

}  // namespace sre
