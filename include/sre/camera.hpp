// Pinhole camera with orbit controls, in Schwarzschild-radius units.
#pragma once

#include <cmath>

#include "sre/constants.hpp"
#include "sre/vec.hpp"

namespace sre {

/// A camera at rest (a "static observer") at a finite Schwarzschild radius,
/// looking at the black hole.
///
/// Directions produced here are unit vectors in the camera's *local
/// orthonormal frame* -- the angles a real observer at that point would
/// measure. sre::makeFrame() applies the tetrad factor that converts them to
/// coordinate components, so the camera stays correct all the way down to the
/// horizon rather than only in the far field.
///
/// What is *not* modelled: an observer in motion. A camera on a circular orbit
/// would see this image aberrated and Doppler shifted by its own velocity, on
/// top of everything the disk does. See docs/PHYSICS.md.
struct Camera {
    Vec3 position{0.0, 2.0, 20.0};
    Vec3 target{0.0, 0.0, 0.0};
    Vec3 up{0.0, 1.0, 0.0};
    double fovDegrees = 55.0;

    /// Orthonormal camera basis: forward, right, trueUp.
    void basis(Vec3& forward, Vec3& right, Vec3& trueUp) const {
        forward = normalize(target - position);
        Vec3 u = up;
        // Degenerate if the camera looks straight along `up`; nudge it.
        if (std::fabs(dot(forward, normalize(u))) > 0.9999) u = Vec3{1.0, 0.0, 0.0};
        right = normalize(cross(forward, u));
        trueUp = cross(right, forward);
    }

    /// Primary ray direction for normalised device coordinates in [-1,1],
    /// where +x is right and +y is up. `aspect` is width/height.
    Vec3 rayDirection(double ndcX, double ndcY, double aspect) const {
        Vec3 forward, right, trueUp;
        basis(forward, right, trueUp);
        const double tanHalf = std::tan(0.5 * fovDegrees * phys::pi / 180.0);
        return normalize(forward + right * (ndcX * aspect * tanHalf) + trueUp * (ndcY * tanHalf));
    }

    /// Place the camera on an orbit around the target.
    /// `azimuth` and `elevation` are in radians; `distance` in r_s.
    static Camera orbit(double azimuth, double elevation, double distance,
                        const Vec3& target = {0, 0, 0}, const Vec3& up = {0, 1, 0}) {
        Camera c;
        c.target = target;
        c.up = up;
        const double ce = std::cos(elevation);
        c.position = target + Vec3{distance * ce * std::sin(azimuth),
                                   distance * std::sin(elevation),
                                   distance * ce * std::cos(azimuth)};
        return c;
    }

    double distanceToTarget() const { return length(position - target); }
};

}  // namespace sre
