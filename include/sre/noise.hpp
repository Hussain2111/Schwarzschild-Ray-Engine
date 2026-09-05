// Small deterministic value-noise kit, shared by the disk texture and the sky.
#pragma once

#include <cmath>

#include "sre/vec.hpp"

namespace sre {
namespace noise {

/// Cheap 3D hash in [0,1). Adequate for texture-scale noise (the star field
/// uses a proper integer hash instead, where banding would be visible).
inline double hash31(double x, double y, double z) {
    const double s = std::sin(x * 127.1 + y * 311.7 + z * 74.7) * 43758.5453123;
    return s - std::floor(s);
}

/// Trilinearly blended value noise with a smoothstep fade.
inline double value(const Vec3& p) {
    const double xi = std::floor(p.x), yi = std::floor(p.y), zi = std::floor(p.z);
    const double xf = p.x - xi, yf = p.y - yi, zf = p.z - zi;
    auto fade = [](double t) { return t * t * (3.0 - 2.0 * t); };
    const double u = fade(xf), v = fade(yf), w = fade(zf);
    double acc = 0.0;
    for (int k = 0; k < 2; ++k)
        for (int j = 0; j < 2; ++j)
            for (int i = 0; i < 2; ++i) {
                const double weight = (i ? u : 1.0 - u) * (j ? v : 1.0 - v) * (k ? w : 1.0 - w);
                acc += weight * hash31(xi + i, yi + j, zi + k);
            }
    return acc;
}

/// Fractal (sum-of-octaves) noise, normalised to roughly [0,1].
inline double fbm(Vec3 p, int octaves = 4) {
    double amp = 0.5, sum = 0.0, norm = 0.0;
    for (int i = 0; i < octaves; ++i) {
        sum += amp * value(p);
        norm += amp;
        p *= 2.03;
        amp *= 0.5;
    }
    return norm > 0.0 ? sum / norm : 0.0;
}

}  // namespace noise
}  // namespace sre
