// The Schwarzschild black hole: mass, horizon, and the characteristic radii
// that the renderer and the tests both need.
#pragma once

#include <cmath>

#include "sre/constants.hpp"

namespace sre {

/// A non-rotating (Schwarzschild) black hole.
///
/// The object stores its mass in SI so it can report physically meaningful
/// numbers, but every radius it hands to the integrator is in units of the
/// Schwarzschild radius, where the geometry is completely scale free: a
/// stellar-mass black hole and Sagittarius A* bend light through *identical*
/// angles at the same r/r_s. That scale invariance is why the renderer never
/// needs to know the mass at all.
class BlackHole {
public:
    explicit BlackHole(double mass_kg = phys::M_sgr_a) : mass_(mass_kg) {}

    static BlackHole fromSolarMasses(double m) { return BlackHole(m * phys::M_sun); }

    double mass() const { return mass_; }                       // kg

    /// r_s = 2GM/c^2, in metres.
    double schwarzschildRadius() const {
        return 2.0 * phys::G * mass_ / (phys::c * phys::c);
    }

    /// r_g = GM/c^2, in metres. The "gravitational radius"; r_s = 2 r_g.
    double gravitationalRadius() const { return 0.5 * schwarzschildRadius(); }

    // --- Landmarks, in units of r_s (see constants.hpp) ---------------------

    /// Radius of the unstable circular photon orbit, r = 3GM/c^2 = 1.5 r_s.
    static constexpr double photonSphere() { return 1.5; }

    /// Innermost stable circular orbit for massive particles, 6GM/c^2 = 3 r_s.
    static constexpr double isco() { return 3.0; }

    /// Critical impact parameter b_c = 3 sqrt(3) GM/c^2 = (3 sqrt(3) / 2) r_s.
    /// Rays with b < b_c are captured; b > b_c escape. The apparent size of
    /// the "shadow" seen by a distant observer is exactly 2 b_c across.
    static double criticalImpactParameter() { return 1.5 * std::sqrt(3.0); }

    /// Metric factor f(r) = 1 - r_s/r, with r in units of r_s.
    static constexpr double f(double r) { return 1.0 - 1.0 / r; }

    /// Coordinate angular velocity of a circular Keplerian orbit at radius r
    /// (in r_s units), as measured by a distant observer:
    ///     Omega = sqrt(GM/r^3)  ->  sqrt(1 / (2 r^3))  with c = r_s = 1.
    /// This Newtonian-looking form is *exact* in Schwarzschild coordinates.
    static double orbitalOmega(double r) { return std::sqrt(0.5 / (r * r * r)); }

    /// Lorentz-like time dilation factor u^t = dt/dtau for that circular
    /// orbit: 1 / sqrt(1 - 3 r_s / (2 r)). Diverges at the photon sphere,
    /// which is why no material disk exists inside r = 1.5.
    static double circularOrbitGamma(double r) {
        const double s = 1.0 - 1.5 / r;
        return s > 0.0 ? 1.0 / std::sqrt(s) : 0.0;
    }

    // --- Unit conversion ----------------------------------------------------

    double toMeters(double r_in_rs) const { return r_in_rs * schwarzschildRadius(); }
    double fromMeters(double r_in_m) const { return r_in_m / schwarzschildRadius(); }

    /// Light-crossing time of one Schwarzschild radius, in seconds. Useful for
    /// putting an animation's frame rate on a physical footing.
    double lightCrossingTime() const { return schwarzschildRadius() / phys::c; }

private:
    double mass_;  // kg
};

}  // namespace sre
