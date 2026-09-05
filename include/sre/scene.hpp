// The scene description and the per-ray shader that turns a traced geodesic
// into a colour. This is the whole renderer, minus the pixel loop.
#pragma once

#include <algorithm>
#include <vector>

#include "sre/blackhole.hpp"
#include "sre/camera.hpp"
#include "sre/disk.hpp"
#include "sre/geodesic.hpp"
#include "sre/starfield.hpp"

namespace sre {

struct RenderQuality {
    int maxSteps = 2048;
    double escapeRadius = 900.0;
    double tolerance = 5e-7;     ///< per-step error tolerance on u
    double maxStep = 0.35;       ///< radians of phi
    int samplesPerPixel = 2;     ///< NxN stratified supersampling (2 -> 4 rays)

    MarchConfig marchConfig() const {
        MarchConfig c;
        c.maxSteps = maxSteps;
        c.escapeRadius = escapeRadius;
        c.tolerance = Tolerance{tolerance * 1e-3, tolerance, 1e-6, maxStep, 0.9};
        c.initialStep = 5e-3;
        return c;
    }
};

struct Scene {
    BlackHole hole{phys::M_sgr_a};
    Camera camera{};
    DiskParams disk{};
    SkyParams sky{};
    RenderQuality quality{};

    double exposure = 0.5;
    /// Faint glow just outside the horizon. Not a physical effect on its own --
    /// it stands in for the photon-ring pileup that a finite step count and a
    /// finite disk cannot resolve -- so it defaults to a small value and can be
    /// switched off entirely.
    double photonRingGlow = 0.0;
};

/// Trace one primary ray and return its radiance (linear RGB, pre-exposure).
inline Color traceRay(const Scene& scene, const Vec3& origin, const Vec3& dir) {
    const double r0 = length(origin);

    // Camera inside the horizon: nothing to see.
    if (r0 <= 1.0) return {0, 0, 0};

    const GeodesicFrame frame = makeFrame(origin, dir);
    const MarchConfig cfg = scene.quality.marchConfig();

    // Disk plane basis, for texturing.
    const Vec3 axis = normalize(scene.disk.axis);
    Vec3 du, dv;
    orthonormalBasis(axis, du, dv);

    const DiskIntersector intersector(frame, scene.disk);

    Color accumulated{0, 0, 0};
    DiskHit firstHit;
    double transmittance = 1.0;

    auto visitor = [&](const MarchSegment& seg) -> bool {
        const DiskHit hit = intersector.test(seg);
        if (!hit.valid) return true;

        Color emission = disk::shade(hit, scene.disk);
        const double azimuth = intersector.azimuthOf(hit, du, dv);
        emission *= disk::texture(hit.radius, azimuth, scene.disk) *
                    disk::edgeFade(hit.radius, scene.disk);

        if (scene.disk.opaque) {
            accumulated += emission * transmittance;
            firstHit = hit;
            return false;  // stop the march: the disk is optically thick
        }
        // Optically thin: keep going and let further crossings add light.
        accumulated += emission * transmittance * 0.6;
        transmittance *= 0.55;
        return transmittance > 0.02;
    };

    PhotonState finalState{};
    double finalPhi = 0.0;
    const RayOutcome outcome = marchPhoton(frame, cfg, visitor, &finalState, &finalPhi);

    if (outcome == RayOutcome::Escaped) {
        const Vec3 skyDir = frame.directionAt(finalPhi, finalState);
        accumulated += sampleSky(skyDir, scene.sky) * transmittance;
    } else if (outcome == RayOutcome::Horizon || outcome == RayOutcome::Exhausted) {
        if (scene.photonRingGlow > 0.0) {
            // Rays that skim the photon sphere wind up many times before
            // falling in; b close to b_c is the marker for that.
            const double b = impactParameter(frame.start);
            const double bc = BlackHole::criticalImpactParameter();
            const double t = std::clamp(1.0 - std::fabs(b - bc) / (0.06 * bc), 0.0, 1.0);
            accumulated += Color{1.0, 0.85, 0.62} * (t * t * scene.photonRingGlow * transmittance);
        }
    }

    return accumulated;
}

/// Trace a pixel with NxN stratified supersampling.
/// Anti-aliasing matters more here than in an ordinary renderer: the Einstein
/// ring is a near-discontinuity in the image, and stars are sub-pixel points.
inline Color tracePixel(const Scene& scene, int px, int py, int width, int height) {
    const int n = std::max(1, scene.quality.samplesPerPixel);
    const double aspect = static_cast<double>(width) / height;
    Color sum{0, 0, 0};

    for (int sy = 0; sy < n; ++sy) {
        for (int sx = 0; sx < n; ++sx) {
            const double ox = (sx + 0.5) / n;
            const double oy = (sy + 0.5) / n;
            const double ndcX = (2.0 * (px + ox) / width) - 1.0;
            const double ndcY = 1.0 - (2.0 * (py + oy) / height);
            const Vec3 dir = scene.camera.rayDirection(ndcX, ndcY, aspect);
            sum += traceRay(scene, scene.camera.position, dir);
        }
    }
    return sum / (n * n);
}

}  // namespace sre
