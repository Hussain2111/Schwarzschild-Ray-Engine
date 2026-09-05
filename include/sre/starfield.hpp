// Procedural star background.
//
// The background is what *shows* the lensing. A flat colour behind the black
// hole produces a black disc and nothing else; a field of point sources
// produces the Einstein ring, the mirrored copies of the sky wrapped around
// the shadow, and the smeared arcs that make the geometry legible.
//
// It is generated rather than loaded so the engine stays asset-free and every
// render is reproducible from a single integer seed.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "sre/color.hpp"
#include "sre/noise.hpp"
#include "sre/vec.hpp"

namespace sre {

struct SkyParams {
    double starDensity = 1.0;      ///< multiplies the number of visible stars
    double starBrightness = 1.0;
    double starSize = 1.0;         ///< angular size multiplier
    double galaxyBrightness = 0.14;///< the Milky Way band
    Vec3 galaxyAxis{0.32, 0.86, 0.4};  ///< normal of the galactic plane
    uint32_t seed = 12345;
    bool enabled = true;
};

namespace sky {

/// Integer hash (a 32-bit finaliser); avoids the sin() hashes that band badly
/// at the cell counts a star field needs.
inline uint32_t hashU32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

inline uint32_t hash3(int32_t a, int32_t b, int32_t c, uint32_t seed) {
    uint32_t h = seed;
    h = hashU32(h ^ static_cast<uint32_t>(a) * 0x9e3779b9U);
    h = hashU32(h ^ static_cast<uint32_t>(b) * 0x85ebca6bU);
    h = hashU32(h ^ static_cast<uint32_t>(c) * 0xc2b2ae35U);
    return h;
}

inline double toUnit(uint32_t h) { return (h >> 8) * (1.0 / 16777216.0); }

/// Map a direction onto a cube face plus face-local uv in [-1,1].
/// A cube map keeps the cell area roughly uniform over the sphere; a naive
/// lat/long grid would pile stars up at the poles.
inline void cubeFace(const Vec3& d, int& face, double& u, double& v) {
    const double ax = std::fabs(d.x), ay = std::fabs(d.y), az = std::fabs(d.z);
    if (ax >= ay && ax >= az) {
        face = d.x > 0 ? 0 : 1; u = d.z / ax * (d.x > 0 ? -1 : 1); v = d.y / ax;
    } else if (ay >= az) {
        face = d.y > 0 ? 2 : 3; u = d.x / ay; v = d.z / ay * (d.y > 0 ? -1 : 1);
    } else {
        face = d.z > 0 ? 4 : 5; u = d.x / az * (d.z > 0 ? 1 : -1); v = d.y / az;
    }
}

/// Stars, drawn as small Gaussian points in a hashed cube-map grid.
inline Color stars(const Vec3& dir, const SkyParams& p) {
    constexpr int kCells = 220;   // cells per cube-face edge
    int face; double u, v;
    cubeFace(dir, face, u, v);

    const double cu = (u * 0.5 + 0.5) * kCells;
    const double cv = (v * 0.5 + 0.5) * kCells;
    const int ci = static_cast<int>(std::floor(cu));
    const int cj = static_cast<int>(std::floor(cv));

    Color acc{0, 0, 0};
    // Angular size of one cell, used to give the Gaussian a sensible width.
    const double cellAngle = 1.5708 / kCells;
    const double sigma = cellAngle * 0.16 * p.starSize;

    for (int dj = -1; dj <= 1; ++dj) {
        for (int di = -1; di <= 1; ++di) {
            const int i = ci + di, j = cj + dj;
            if (i < 0 || j < 0 || i >= kCells || j >= kCells) continue;

            const uint32_t h = hash3(i, j, face * 7919 + 1, p.seed);
            // Only a fraction of cells hold a star, so the field is not a grid.
            if (toUnit(hashU32(h)) > 0.09 * p.starDensity) continue;

            const double su = i + toUnit(hashU32(h ^ 0xa511e9b3U));
            const double sv = j + toUnit(hashU32(h ^ 0x63d5a1c7U));

            // Distance in cell units -> angular distance.
            const double ddu = (cu - su) * cellAngle;
            const double ddv = (cv - sv) * cellAngle;
            const double d2 = ddu * ddu + ddv * ddv;
            const double falloff = std::exp(-0.5 * d2 / (sigma * sigma));
            if (falloff < 1e-4) continue;

            // Power-law magnitude distribution: a few bright stars, many faint.
            const double q = toUnit(hashU32(h ^ 0x1b873593U));
            const double mag = std::pow(q, 6.0) * 9.0 + 0.02;

            // Stellar temperatures, weighted towards cool stars like the real
            // sky, so the field is mostly white-yellow with occasional blue.
            const double tq = toUnit(hashU32(h ^ 0x5bd1e995U));
            const double kelvin = 2600.0 + std::pow(tq, 2.2) * 22000.0;

            acc += color::blackbodyTable()(kelvin) * (mag * falloff);
        }
    }
    return acc * p.starBrightness;
}

/// A faint dusty band, so the sky has large-scale structure for the lensing to
/// bend as well as point sources.
inline Color galaxyBand(const Vec3& dir, const SkyParams& p) {
    if (p.galaxyBrightness <= 0.0) return {0, 0, 0};
    const Vec3 axis = normalize(p.galaxyAxis);
    const double s = dot(dir, axis);
    const double band = std::exp(-0.5 * (s / 0.16) * (s / 0.16));

    // Two octaves at different scales give a mottled, dusty look.
    const double clumps = noise::fbm(dir * 9.0, 4) * 0.7 + noise::fbm(dir * 31.0, 3) * 0.3;
    const double intensity = band * (0.35 + 0.9 * clumps) * p.galaxyBrightness;
    return Color{0.55, 0.62, 0.95} * intensity;
}

}  // namespace sky

/// Full sky radiance for an escaping ray direction.
inline Color sampleSky(const Vec3& dir, const SkyParams& p) {
    if (!p.enabled) return {0, 0, 0};
    return sky::stars(dir, p) + sky::galaxyBand(dir, p);
}

}  // namespace sre
